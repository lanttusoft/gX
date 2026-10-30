# UI diff

<!-- //ai: 2026-10-22 textual layout-dump diff for front-end reviews. -->

`scripts/ui_diff.py` compares two `--layout-dump` JSON files and describes
added, removed, moved and resized elements without a screenshot/vision pass.

```bash
./build/gX_build --title --layout-dump /tmp/before.json --layout-dump-frame 8
./build/gX_build --title --layout-dump /tmp/after.json  --layout-dump-frame 8
python3 scripts/ui_diff.py /tmp/before.json /tmp/after.json --tol 2
```

- Exit `0`: no position/size change beyond `--tol` pixels, and no added/removed
  elements or screen-size change.
- Exit `1`: meaningful changes; the report is textual.
- Exit `2`: malformed/missing dump or bad arguments.

`--json <path>` writes `{"pass", "tolerance", "changes"}` for a harness. The
gate is `make ui-diff-check` (unit tests only; no display).

The current dump schema uses `screenW`, `screenH`, and per-canvas element
`name`, `type`, `text`, `x`, `y`, `w`, `h`.

## Resolution sweep

`scripts/layout_sweep.py` runs a scene set across a resolution matrix and
compares normalized layouts. The first `--sizes` entry is the baseline; every
rect is normalized by the dump's actual screen (`x/W`, `y/H`, `w/W`, `h/H`),
so pure scaling does not look like a shift. The default matrix is
`800x600,1024x768,1280x720,1920x1080` over `title,themlab`; both are
configurable:

```bash
python3 scripts/layout_sweep.py --scenes title,themlab \
  --sizes 800x600,1280x720 --tol 0.02 \
  --out /tmp/opencode/layout-sweep.md
python3 scripts/layout_sweep.py --scenes title --sizes 800x600,1920x1080 \
  --format json --out /tmp/opencode/layout-sweep.json
```

Reported findings: missing/added elements, normalized shifts/resizes beyond
`--tol` (default `0.02`), foreground-overlap growth (normalized screen area
and pair ratio; full-screen backgrounds excluded), and screen-size mismatches
when the engine clamped a window. Findings never fail the command; only
tool/scene-run/parse failures return nonzero. Element identity is
`canvas:name#index`; a reorder is reported as missing/added. Run
`make layout-sweep-check` for the synthetic comparison tests (no display or
engine launch).

On 2026-10-22 a reduced 800x600→1280x720 title sweep reported 0 missing,
0 added, 7 normalized shifts and 1 overlap change, with no screen-size
mismatches. The shifts are responsive-layout evidence, not a
pass/fail claim.
