# Sky render tests

<!-- //ai: renderer-level sky state/repeat matrix; complements sky_tests.c. -->

`scripts/sky_render_check.py` is the fixed-resolution sky appearance suite. It
adds the visual matrix that the preset/clock tests in
`src/renderer/sky_tests.c` and the precision/horizon helpers do not cover.

## Matrix

The script captures at 1024×768, frame 8 (12 frames), with a fixed timestep:

| State | Mechanism | Purpose |
|---|---|---|
| `dawn` | `GX_SKY_CHECK=1` | sun near the horizon |
| `noon` | `GX_SKY_CHECK=0` | sun above the horizon |
| `dusk` | `GX_SKY_TIME=15000` | low-sun ephemeris epoch |
| `night` | `GX_SKY_CHECK=2` | sun below the horizon |
| `moon` | `GX_SKY_CHECK=3` | polar/moon conjunction |
| `control-nosky` | `--showcase-y2k`, `GX_T_SKY=0` | existing no-sky control |

`GX_SKY_TIME` is a test-only hook in `src/renderer/sky.c`. It sets
`gamedata.ephemeris.timeSec`, places the ephemeris observer at Earth so the
Sun/Moon directions are meaningful, and rebuilds the ephemeris. When the hook
is unset, preset/game time and the existing observer are untouched.

The control uses the existing showcase-y2k path, not a second sky renderer.
It is not compared for sky-state content; it proves the control frame is
finite and non-empty.

## Commands

Agent/local closure (synthetic tests only):

```bash
python3 -m py_compile scripts/sky_render_check.py
python3 -m unittest scripts/test_sky_render_check.py
make check-fast
```

GPU captures (main, after integration):

```bash
make sky-render-check
make horizon-check
```

The script captures into `/tmp/opencode/sky_render_check` by default and
writes `report.json` plus `report.md` with per-state ROI/band/horizon metrics,
state deltas, repeat hashes and failures. `--only dawn,noon` and `--out DIR`
select a subset and output directory.

## What it checks

- Every capture has finite pixels and the size is exactly 1024×768.
- The upper-left sky ROI is non-empty and has band variation.
- A same-state repeat is byte-identical.
- Every pair of sky states differs by at least the documented mean/fraction
  threshold (dawn/noon/dusk/night/moon).
- The detected horizon row stays within the documented tolerance across sky
  states, and each row is inside the image.
- Per-state top/middle/bottom band means are reported for review.

State distinctness and repeat checks are thresholds for regression detection,
not aesthetic approval. A threshold must not be lowered to make a state pass;
if a state is genuinely equivalent, remove or re-specify the state with a
documented reason instead of hiding the difference.

## Limits / non-goals

- Only the default sky scene/look and the fixed 1024×768 frame are covered.
  Presets, editor settings, weather interaction, planets and sun/moon
  occlusion are not exhaustively covered.
- `dawn`/`noon`/`night`/`moon` use the existing deterministic
  `GX_SKY_CHECK` diagnostic states. They are not civil clock times; `dusk`
  uses an ephemeris epoch selected for a low-sun response.
- The no-sky control is from a different scene (`--showcase-y2k`), so it is
  a finite/non-empty control, not a same-camera sky-on comparison. The
  existing sky precision gate remains the source-pair/precision authority.
- `horizon-check`, `gfx_sky_precision_check.py` and `sky_tests.c` remain the
  precision, preset and parser/clock authorities. This suite does not replace
  them.
- GPU/Xvfb captures are not authoritative for the main review; main runs the
  targets on the real GPU and reviews the captures and report.
