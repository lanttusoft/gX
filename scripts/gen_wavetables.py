#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: wavetable fixture generator — deterministic additive tables for the synth
#   demo's WavetableLoad path (raw little-endian float32, values 0..1 like
#   gX_Synth_WT_Generate). Regenerate with:
#     python3 scripts/gen_wavetables.py
#   Verify the shipped files match this script (run from `make check`):
#     python3 scripts/gen_wavetables.py --check
#
#   Samples are rounded to 6 decimals before packing so the bytes are stable
#   across libm versions; the loader accepts 1..GX_SYNTH_WT_SIZE samples.
import argparse
import hashlib
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DIR = ROOT / "resources" / "audio" / "wavetable"
SAMPLE_COUNT = 1024  # half the engine max — proves non-max sizes load fine


def harmonic_sum(phase, partials):
    """Additive sum over [(ratio, amplitude), ...] at phase 0..1."""
    total = 0.0
    for ratio, amplitude in partials:
        total += amplitude * math.sin(2.0 * math.pi * ratio * phase)
    return total


TABLES = {
    # band-limited saw — odd/even alternating series, the classic warm ramp
    "warm_saw": [(float(k), (1.0 / k) * (1.0 if k % 2 == 1 else -1.0))
                 for k in range(1, 13)],
    # octave-stacked drawbars (1', 2', 3'-color) — hollow organ registration
    "organ": [(1.0, 1.0), (2.0, 0.6), (4.0, 0.4), (3.0, 0.15)],
    # bell — inharmonic partials (2.76 / 5.40 / 8.93 ratios, tubular-bell-ish)
    "bell": [(1.0, 0.6), (2.76, 0.45), (5.40, 0.3), (8.93, 0.18)],
}


def render(name, partials):
    raw = [harmonic_sum(i / SAMPLE_COUNT, partials) for i in range(SAMPLE_COUNT)]
    peak = max(abs(v) for v in raw) or 1.0
    out = bytearray()
    for value in raw:
        unipolar = 0.5 + 0.5 * (value / peak)  # [0,1] like the generated shapes
        out += struct.pack("<f", round(unipolar, 6))
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description="gX wavetable fixture tool")
    parser.add_argument("--check", action="store_true",
                        help="verify the shipped files match a fresh render")
    parser.add_argument("--out", type=Path, default=DEFAULT_DIR)
    args = parser.parse_args()

    if args.check:
        stale = []
        for name, partials in TABLES.items():
            path = args.out / f"{name}.f32"
            expected = render(name, partials)
            if not path.is_file() or path.read_bytes() != expected:
                stale.append(str(path))
        if stale:
            print("WAVETABLE_CHECK: STALE — regenerate with "
                  "python3 scripts/gen_wavetables.py", file=sys.stderr)
            for path in stale:
                print(f"  {path}", file=sys.stderr)
            return 1
        print(f"WAVETABLE_CHECK: OK ({len(TABLES)} tables, "
              f"{SAMPLE_COUNT} samples each)")
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    for name, partials in TABLES.items():
        data = render(name, partials)
        path = args.out / f"{name}.f32"
        path.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        print(f"wrote {path.relative_to(ROOT)} ({len(data)} bytes, sha256 {digest[:16]}...)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
