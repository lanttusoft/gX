# Hardware requirements

gX targets a modern OpenGL stack. The tiers below are **feature levels** the
engine classifies at runtime (`--hw-report`); the example hardware is guidance
for planning, not a support list. Performance bands are measured separately in
[`minimum_hardware.md`](minimum_hardware.md) — notably, the shipped default
showcase needs a GTX 1060 6 GB class GPU for 60 fps at 1024x768. Everything is
measured/validated with the gates named at the bottom.

## Tiers

| Tier | Requires (detected capabilities) | Unlocks | Example hardware |
| --- | --- | --- | --- |
| **below minimum** | — | not supported: no GLSL 3.30 (desktop) / 3.00 (ES) or no framebuffer objects | GL 2.x / ES 2.0-era GPUs |
| **minimum** | desktop GL 3.3 / GLSL 330 (ES 3.0 best-effort) + FBOs | forward renderer, GUI/editor, demo scenes, raymarch at low render scale | Intel HD 4000, GTX 460, Radeon HD 5000 |
| **standard** | compute + SSBO (GL 4.3 / `ARB_compute_shader`), or the GL 4.2 bindless-era raster path | compute-tier paths (GPU light culling, compute effect gates), raymarch at mid render scale | GTX 750 Ti, RX 550, Iris Xe |
| **recommended** | full desktop set: compute + SSBO + bindless + cube-map arrays + tessellation | the whole engine plus raymarch at render scale 1.0 with every shader feature | GTX 1060 6GB, RX 580, Arc A380 and up |

Notes:

- The engine requests a desktop **compatibility** context; the GL version
  string may read `3.3` even when the driver exposes every ARB extension. The
  classifier therefore uses capabilities, not the version string. The
  educational `GX_GL_LEVEL` ladder (`docs/architecture.md`) can force lower
  levels to test forward compatibility.
- The shader texture-unit budget is ≤ 31 fragment-stage units
  (`src/renderer/gfx/constants.h`); the report's `texture_units` is the
  combined GL limit (often much higher on NVIDIA).
- VRAM/CPU guidance: 2 GB + any 2015-era dual/quad-core covers the minimum
  tier at 1080p; 4 GB+ for HDR/bloom-heavy presets; 6 GB+ and a discrete GPU
  for raymarch at 1.0 render scale.
- Raymarch cost scales with `parameters.renderScale` (the module renders at a
  fraction of the window and upscales). Measured on GTX 1060 6GB at 1024x768
  with `make raymarch-benchmark` (GPU draw p50): 0.25 → 0.63 ms, 0.35 → 1.06,
  0.5 → 1.57, 0.75 → 2.73, 1.0 → 4.44; terrain is ~76% of the default row and
  objects ~40%. These numbers are advisory and host-specific.

## Runtime report

```text
./build/gX_build --hw-report          # or GX_HW_REPORT=1
HW_REPORT: vendor="..." renderer="..." version="..." glsl="..."
HW_REPORT: gl=4.6 es=0 shader=460
HW_REPORT: flags compute=1 ssbo=1 bindless=1 tess=1 ...
HW_REPORT: limits texture_units=32 texture_size=32768 color_attachments=8 uniform_components=4096
HW_REPORT: tier=recommended reason="full feature set: ..."
```

`make hw-report-check` runs the report under the hidden audit window, validates
the block, and cross-checks the tier against the same rules; the classifier
itself is unit-tested headlessly (`HW TESTS: all passed` in `--test-only`,
part of `make check`).

## Gates

- `make hw-report-check` — runtime report + tier consistency + classifier tests.
- `make raymarch-benchmark` / `make raymarch-benchmark-check` — raymarch cost
  rows and the resolution-scaling trend.
- `make gfx-benchmark` / `make gfx-benchmark-reference-check` — the renderer's
  CPU/GPU benchmark and the host-pinned reference budgets.
