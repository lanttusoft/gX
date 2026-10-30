# Bloom pipeline audit

`make bloom-audit` runs the dedicated bloom verification gate
(`scripts/bloom_audit.py`). It isolates each pipeline stage on a deterministic
showcase capture and asserts directional behavior, not just bands:

```
extract (threshold, knee) -> blur H -> blur V -> blur H -> blur V
                          -> composite (intensity, exposure, vignette,
                                        saturation, contrast, tonemap, dither)
                          -> auto-exposure (luminance readback, Krawczyk key)
```

## Running

```bash
make bloom-audit                     # full gate (10 families, 22 checks)
make bloom-audit-quick               # isolation + intensity + emissive only
python3 scripts/bloom_audit.py --only threshold,knee,blur
python3 scripts/bloom_audit.py --report          # metrics only (calibration)
python3 scripts/bloom_audit.py --jobs 4 --cache  # parallel + capture reuse
python3 scripts/bloom_audit.py --json /tmp/opencode/bloom.json
```

Captures are 1024x768@60 at shot frame 120 on `--showcase` (the intensity/emissive probes
uses `--showcase-minimal` with the orb-mapped diagnostic cube). All other
effects are forced off, auto-exposure is off for the stage probes, and the
showcase HUD is disabled because its glyph pixels jitter with frame timing.

## Probe families

| family | probes | pinned behavior |
| --- | --- | --- |
| `isolation` | intensity 0 vs 6 | glow fraction in 0.00001..0.2, no collapse, no sentinel |
| `intensity` | 0.75, 1.5, 3, 6 | procedural emissive source: strict fraction/luma ramp, max <= 0.25; original showcase: strict brightening and bounded footprint |
| `threshold` | 0.1, 0.85, 2, 10 | non-increasing sweep with strict head; threshold 10 -> ~0 |
| `knee` | 0, 1 at threshold 0.85 | knee 1 widens the soft band (frac up), bounded |
| `blur` | 0.5 vs 2 | wider blur grows the low-delta (>2) footprint |
| `composite` | exposure 0.5/2, vignette 0/1, saturation 0/2, contrast 0.5/2 | exposure and contrast raise luma/contrast; saturation 0 is grayscale; vignette darkens edges |
| `tonemap` | `GX_T_TONEMAP=0`, `GX_NO_AGX=1` | the chain changes the frame; ACES vs AgX differ measurably |
| `autoexposure` | `GX_T_BLOOMAE=1` vs fixed | adapted exposure changes mean luma (and stays valid) |
| `emissive` | diag cube + emissive map, intensity 0 vs 6 | deterministic orb halo in 0.015..0.20 |
| `msaa` | `GX_T_MSAA=4`, intensity 0 vs 6 | glow survives the MSAA resolve path |

<!-- //ai: retain acceptance cuts while making the spatial witness independent of local asset/settings details. -->
The spatial intensity ramp uses the existing procedural diagnostic cube/emissive
map, with threshold `.85`, knee `.3`, blur/exposure/saturation/contrast `1`,
vignette `.5` and auto-exposure off. Its control and all intensity endpoints
differ only in intensity. The pixel difference cut remains **>10**, the intensity
values and strict ordering remain unchanged, and the footprint ceiling is `.25`.
The original showcase ramp is still captured and must strictly brighten without
exceeding that footprint ceiling. Selected-family runs now include their actual
controls, so `--only intensity` can perform the same checks as the full audit.

The old clean-checkout showcase produced positive brightness changes whose
maximum mean-RGB deltas at `.75/1.5` were only `3.67/7.33`: both fractions were
necessarily zero at the 10-step cut. This is an insufficient spatial test input,
not evidence that production bloom needs amplification. No runtime bloom strength
or acceptance cutoff was changed. `scripts/test_bloom_intensity.py` rejects flat,
reversed and subcut ramps, washout, missing captures and lost showcase brightening.
Finite image witnesses do not certify universal GPU/numerical behavior.

Every capture is additionally rejected on:

- **magenta sentinel** — the extract/blur/composite shaders write magenta when
  a pass fails to assign `finalColor`; any surviving sentinel is a defect.
- **black collapse** — more than 50% of pixels below 8/255.
- **washout** — family-specific ceilings (a full-frame glow is a regression).

## Window isolation

GPU audits share the desktop with the user. `GX_AUDIT_WINDOW` controls how the
engine window is created:

- `hidden` (default for `make` gates and `audit_common`): the window is never
  mapped — nothing appears on screen, no focus steal, no pointer grab, and the
  captures are pixel-identical to visible runs.
- `1`: unfocused and parked off-screen (best effort; Wayland compositors ignore
  absolute positioning). Useful for interactive debugging.
- empty: a normal visible window (timing targets clear the variable on purpose,
  because skipping presentation would skew benchmark wall cost).

The cursor guard in `src/input.c` keeps the pointer enabled in audit windows
even for free-look scenes (`INPUT: audit window keeps the cursor`);
`GX_AUDIT_WINDOW` empty opts a run back into a normal visible window
(the audit helpers default it to `hidden`).

## Shared helpers

`scripts/audit_common.py` holds the helpers the per-effect checks share:

- `settings_preserved()` / `with_settings_preserved()` — restore
  the local `settings.json` under `resources/` if a probe rewrites it; this
  generated file need not exist in a clean checkout.
- `scene_env()` / `run_scene()` — deterministic env (no settings save, audit
  window) and one scene run with optional screenshot.
- `CapturePool` — parallel captures with private per-probe directories, a
  serial retry phase for GPU-contention stragglers, and opt-in fingerprint
  caching (`--cache`, binary mtime/size + full request).
- image metrics: `diff_frac`, `mean_luma`, `black_fraction`,
  `magenta_fraction`, `chroma_spread`, `luma_std`, `edge_center_ratio`.

The per-effect checks (`gfx-rig-inspector-check`, `gfx-label-font-check`,
`gfx-light-picker-check`, `gfx-motion-path-check`, `gfx-preset-overwrite-check`,
`gfx-reflection-ui-check`) import these helpers, so settings safety and window
isolation have a single implementation.

## Scoreboard integration

`make scoreboard` (full run) executes this audit as bloom's owning correctness
gate: it runs `bloom_audit.py --json` with hidden windows, parses the report,
shows `bloom:A`/`bloom:F` in the correctness column, and fails the scoreboard
when the audit fails. The feature-matrix row stays as the fast smoke, and the
`--skip-gates` / `--only` subset modes skip the audit like the other slow
gates. Capture runs stay hidden even though the scoreboard clears
`GX_AUDIT_WINDOW` for its timing rows (presentation cost must be measured on
visible windows).

## Hooks used

| hook | effect |
| --- | --- |
| `GX_T_BLOOM`, `GX_T_BLOOMINT`, `GX_T_BLOOMTHRESH` | enable, intensity, extract threshold |
| `GX_T_BLOOMKNEE`, `GX_T_BLOOMBLUR` | extract knee, blur tap spacing |
| `GX_T_BLOOMEXPOSURE`, `GX_T_BLOOMSAT`, `GX_T_BLOOMCONTRAST` | composite exposure/saturation/contrast (needs `GX_T_BLOOMAE=0`) |
| `GX_T_BLOOMAE` / `GX_NO_AE` | auto-exposure toggle |
| `GX_T_VIGNETTE` | composite vignette |
| `GX_T_TONEMAP`, `GX_NO_AGX` | chain force / AgX opt-out |
| `GX_T_HUD` | showcase HUD on/off (diff stability) |
| `GX_T_MSAA` | MSAA resolve interaction |
| `GX_TONEMAP_TRACE` | one-shot `BLOOM: tonemap aces=.. agx=..` state echo |

## Pitfalls the audit encodes

- `GX_T_TONEMAP=1` forces ACES **and** AgX **and** gamma, overriding
  `GX_NO_AGX`; the ACES-vs-AgX probe must leave `GX_T_TONEMAP` unset.
- The showcase HUD re-renders glyph pixels with frame timing; keep `GX_T_HUD=0`
  in any capture-diff audit (the feature matrix and scoreboard set it too).
- `GX_AI_INPUT` advances on the frame delta when `GX_FIXED_DT` is set, so click
  schedules are independent of render speed (they used to follow wall time,
  which shifted hidden-window runs).
- Editor checks whose click offsets assume the fitted visible client height
  must not use `hidden` windows (the reflection UI check requests mode `1`).
