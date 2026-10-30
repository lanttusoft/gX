#!/usr/bin/env bash
# gX Lean gate — grades the Release payload (text + rodata + data) and max-bss
# against a baseline, with absolute caps in .lean_budget.json.
#
# Why payload, not whole-file size: file bytes include symbol tables, unwind
# data, and linker page-alignment padding, so a section crossing a 4 KiB page
# boundary can add several KiB of file size for ~1 KiB of code. The payload
# tracks what the build actually adds; whole-file size and its overhead stay
# in the report as an advisory line.
#
# Usage:
#   scripts/lean_gate.sh            # compare against stored baseline
#   scripts/lean_gate.sh --update   # store current build as baseline
#   scripts/lean_gate.sh --json     # print current metrics as JSON (no compare)
set -euo pipefail

BIN_ARG="${1:-}"
MODE="${2:-}"
if [ "$BIN_ARG" = "--update" ] || [ "$BIN_ARG" = "--json" ]; then
    MODE="$BIN_ARG"
    BIN_ARG=""
fi
#opencode: default Release binary under build/ per repo policy.
BIN="${BIN_ARG:-build/release/gX_build}"
BASELINE=".lean_baseline"
BUDGET=".lean_budget.json"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$(cd "$ROOT" && realpath -m "$BIN")"

if [ ! -f "$BIN" ]; then
    echo "LEAN_GATE: binary not found: $BIN (build Release first)" >&2
    exit 2
fi

#opencode: section breakdown. size(1) gives binary/bss; sysv format adds the
#   payload split the gate and heuristic grades need:
#     text   = functional code (.text + .plt)
#     rodata = string tables, lookup data, copy-only assets
#     data   = writable initialized data (.data + .data.rel.ro + .got*)
#   payload = text + rodata + data (the gated image content).
SIZE_RAW=$(size "$BIN" 2>/dev/null | tail -1)
BIN_BYTES=$(stat -c%s "$BIN")
TEXT_BYTES=$(size --format=sysv "$BIN" 2>/dev/null | awk '$1==".text"{print $2}')
BSS=$(echo "$SIZE_RAW" | awk '{print $3}')
BSS_ALL=$(size --format=sysv "$BIN" 2>/dev/null | awk '$1==".bss"{print $2}')
RODATA_BYTES=$(size --format=sysv "$BIN" 2>/dev/null | awk '$1==".rodata"{print $2}')
DATA_BYTES=$(size --format=sysv "$BIN" 2>/dev/null | awk '$1==".data"{print $2}')
if [ -z "${TEXT_BYTES:-}" ] || [ -z "${RODATA_BYTES:-}" ] || [ -z "${DATA_BYTES:-}" ]; then
    echo "LEAN_GATE: section breakdown unavailable (size --format=sysv)" >&2
    exit 2
fi
IMAGE_BYTES=$(( TEXT_BYTES + RODATA_BYTES + DATA_BYTES ))

json() {
    printf '{"image_bytes": %d, "binary_bytes": %d, "bss_bytes": %d, "text_bytes": %d, "rodata_bytes": %d, "data_bytes": %d}' \
        "$IMAGE_BYTES" "$BIN_BYTES" "$BSS" "$TEXT_BYTES" "$RODATA_BYTES" "$DATA_BYTES"
}

case "$MODE" in
    --update)
        json > "$ROOT/$BASELINE"
        echo "LEAN_GATE: baseline updated; approved size caps unchanged: $(cat "$ROOT/$BASELINE")"
        exit 0
        ;;
    --json)
        json
        exit 0
        ;;
esac

if [ ! -f "$ROOT/$BASELINE" ]; then
    echo "LEAN_GATE: no baseline yet — run with --update" >&2
    exit 2
fi

CUR=$(json)
BASE=$(cat "$ROOT/$BASELINE")
CUR_IMAGE=$(echo "$CUR" | python3 -c "import sys,json;print(json.load(sys.stdin)['image_bytes'])")
BASE_IMAGE=$(echo "$BASE" | python3 -c "
import sys, json
b = json.load(sys.stdin)
if 'image_bytes' in b:
    print(b['image_bytes'])
elif 'text_bytes' in b and 'rodata_bytes' in b and 'data_bytes' in b:
    print(b['text_bytes'] + b['rodata_bytes'] + b['data_bytes'])
else:
    sys.exit('LEAN_GATE: baseline has no payload fields — run with --update')
")
CUR_BSS=$(echo "$CUR" | python3 -c "import sys,json;print(json.load(sys.stdin)['bss_bytes'])")
BASE_BSS=$(echo "$BASE" | python3 -c "import sys,json;print(json.load(sys.stdin)['bss_bytes'])")

if [ ! -f "$ROOT/$BUDGET" ]; then
    echo "LEAN_GATE: no size budget at $ROOT/$BUDGET" >&2
    exit 2
fi

BUDGET_VALUES=$(python3 - "$ROOT/$BUDGET" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as budget_file:
        budget = json.load(budget_file)
    image_max = int(budget["image_max_bytes"])
    bss_max = int(budget["bss_max_bytes"])
    jitter = int(budget["jitter_bytes"])
    if min(image_max, bss_max, jitter) < 0 or image_max == 0 or bss_max == 0:
        raise ValueError("budget values must be positive (jitter may be zero)")
except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
    print(f"LEAN_GATE: invalid size budget: {error}", file=sys.stderr)
    sys.exit(2)

print(image_max, bss_max, jitter)
PY
)
read -r MAX_IMAGE MAX_BSS JITTER_BYTES <<< "$BUDGET_VALUES"

ASSESSMENT=$(python3 - "$CUR_IMAGE" "$BASE_IMAGE" "$CUR_BSS" "$BASE_BSS" "$MAX_IMAGE" "$MAX_BSS" "$JITTER_BYTES" <<'PY'
import sys

cur_image, base_image, cur_bss, base_bss, max_image, max_bss, jitter = map(int, sys.argv[1:])

def grade(current, baseline, hard_limit):
    delta = (current - baseline) * 100.0 / baseline
    jitter_pct = jitter * 100.0 / baseline
    adjusted = max(0.0, delta - jitter_pct)
    acceptable_delta = max(0.0, (hard_limit - baseline - jitter) * 100.0 / baseline)
    if delta <= -1.0:
        return delta, "S"
    if delta <= 0.0:
        return delta, "A+"
    if adjusted <= 0.25:
        return delta, "A"
    if adjusted <= 0.50:
        return delta, "A-"
    if adjusted <= 0.75:
        return delta, "B"
    if adjusted <= 0.90:
        return delta, "C"
    if adjusted <= acceptable_delta:
        return delta, "D"
    return delta, "F"

image_delta, image_grade = grade(cur_image, base_image, max_image)
bss_delta, bss_grade = grade(cur_bss, base_bss, max_bss)
rank = {"S": 0, "A+": 1, "A": 2, "A-": 3, "B": 4,
        "C": 5, "D": 6, "F": 7}
overall = max((image_grade, bss_grade), key=lambda item: rank[item])
print(f"{image_delta:.3f} {bss_delta:.3f} {image_grade} {bss_grade} {overall}")
PY
)
read -r IMAGE_DELTA BSS_DELTA IMAGE_GRADE BSS_GRADE OVERALL <<< "$ASSESSMENT"

PASS=1
if [ "$CUR_IMAGE" -gt "$MAX_IMAGE" ]; then IMAGE_GRADE=F; PASS=0; fi
if [ "$CUR_BSS" -gt "$MAX_BSS" ]; then BSS_GRADE=F; PASS=0; fi
if [ "$IMAGE_GRADE" = F ] || [ "$BSS_GRADE" = F ]; then OVERALL=F; fi
IMAGE_HEADROOM=$(( MAX_IMAGE - CUR_IMAGE ))
BSS_HEADROOM=$(( MAX_BSS - CUR_BSS ))
echo "LEAN_GATE: payload $CUR_IMAGE (base $BASE_IMAGE, ${IMAGE_DELTA}%, $IMAGE_GRADE; cap $MAX_IMAGE, ${IMAGE_HEADROOM} bytes headroom) | bss $CUR_BSS (base $BASE_BSS, ${BSS_DELTA}%, $BSS_GRADE; cap $MAX_BSS, ${BSS_HEADROOM} bytes headroom)"

# ---------------------------------------------------------------------------
# Advisory heuristics (non-blocking; the hard caps above still gate).
#   Q: does the build earn its bytes? Functional code vs copy-data split,
#      data-to-code ratio, file overhead, and physical-media fit are reported
#      every run so drift shows up early without inventing new blocking rules.
# ---------------------------------------------------------------------------
ADVISORY_LINES=$(python3 - "$TEXT_BYTES" "$RODATA_BYTES" "$DATA_BYTES" "$IMAGE_BYTES" "$BIN_BYTES" "$BSS_ALL" <<'PY'
import sys

text, rodata, data, payload, file_total, bss = map(int, sys.argv[1:])
data_total = rodata + data
ratios = text * 100.0 / payload

def ratio_grade(pct, good, fair, poor):
    if pct >= good:
        return "S"
    if pct >= fair:
        return "A"
    if pct >= poor:
        return "B"
    return "C"

code_grade = ratio_grade(ratios, 80.0, 70.0, 60.0)
data_ratio = data_total * 100.0 / text
data_grade = ratio_grade(100.0 - data_ratio, 65.0, 55.0, 45.0)

#opencode: physical-media fit — label first disk family that holds the
#   shipped file, with a fit ratio so near-misses are visible. Sizes are the
#   published user capacities (MB = 10^6 for optical, MiB for disks).
media = [
    ("3.5in 1.44MB floppy", 1440 * 1024),
    ("CD-ROM 650MB", 650 * 1000 * 1000),
    ("CD-R 700MB", 700 * 1000 * 1000),
    ("DVD-5 4.7GB", 4700 * 1000 * 1000),
    ("DVD-9 8.5GB", 8500 * 1000 * 1000),
    ("BD-25", 25 * 1000 * 1000 * 1000),
    ("BD-50", 50 * 1000 * 1000 * 1000),
]
fit = None
for name, cap in media:
    if file_total <= cap:
        fit = (name, cap)
        break
fit_text = fit[0] if fit else "exceeds BD-50"
#opencode: densest media the image does NOT fit — the classic "density
#   ceiling" readout for the disk-image mental model.
miss = None
for name, cap in media:
    if file_total > cap:
        miss = (name, file_total * 100.0 / cap)
    else:
        break
fit_ratio = file_total * 100.0 / (fit[1] if fit else media[-1][1])

#opencode: file overhead — symbol tables, unwind data, and linker padding on
#   top of the payload. Advisory so page-alignment shifts stay visible without
#   re-introducing a file-size ceiling.
overhead = file_total - payload
print(f"H  file      {file_total:>9} bytes (payload {payload}; overhead {overhead})")
print(f"H  code      {text:>9} bytes ({ratios:5.1f}% of payload) {code_grade}")
print(f"H  data      {data_total:>9} bytes (rodata {rodata}, data {data}; {data_ratio:5.1f}% of code) {data_grade}")
print(f"H  media     fits {fit_text} ({fit_ratio:.1f}% of media)", end="")
if miss:
    print(f"; misses {miss[0]} at {miss[1]:.1f}%")
else:
    print()
#opencode: program-vs-assets proxy — bss is runtime state; payload bytes are
#   program content. Payload-sized bss vs payload class matters for embedded.
print(f"H  state     bss {bss} bytes ({bss * 100.0 / (payload + bss):.1f}% of memory footprint)")
PY
)
echo "$ADVISORY_LINES"

if [ "$PASS" = 1 ]; then
    echo "LEAN_GATE: PASS — assessment $OVERALL"
    exit 0
else
    echo "LEAN_GATE: FAIL — assessment F (hard cap exceeded)" >&2
    exit 1
fi
