# Render-target diet (VRAM mode)

`ENABLE_RT_DIET` is an optional renderer state/preset field, exposed in the
editor under **Quality → compressed aux targets (VRAM)**. Default: off.

When enabled, auxiliary targets use smaller formats and drop depth
attachments they never sample:

| Target | Default | Diet |
| --- | --- | --- |
| SSAO map/blur | RGBA8 + depth | R8, no depth |
| GTAO accum A/B | RGBA16F + depth | RG16F, no depth |
| SSGI map/blur/accum | RGBA16F + depth | R11G11B10F, no depth |
| DOF CoC | RGBA16F + depth | R16F, no depth |
| DOF blur/ping/composite, motion blur | RGBA16F + depth | RGB10_A2, no depth |
| bloom extract/ping/pong | RGBA16F + depth | R11G11B10F, no depth |
| bloom.scene | RGBA16F + depth | RGBA16F, no depth |
| SSR map, refraction, LUTs, godray/fog, mirror | RGBA16F + depth | RGBA16F, no depth |
| TAA history/resolve | RGBA16F + depth | unchanged |
| hdrScene, velocity, reflection.planar, MSAA | unchanged | unchanged |

Formats are requested at allocation sites; `gX_RenderTexture_Load_Format`
checks FBO completeness and falls back to the legacy target if a format is
unavailable. The path is therefore GL 3.3-safe; no 4.x feature is required.
Changing the field bumps a generation and every allocated target reloads on
its next update (preset or editor), so no restart is needed.

## Measured trade (GTX 1060, 1024x768)

| Metric | Result |
| --- | --- |
| VRAM, env A/B | 118.484 → 90.281 MB (**-23.8%**) |
| VRAM, preset path | 118.484 → 92.531 MB (-21.9%) |
| Image vs off | PSNR **51.27 dB**, mean 0.076, max 70 |
| GPU time | neutral to slightly positive; VRAM is the delivered win |

The residual is quantization of AO/SSGI/bloom buffers, concentrated on sharp
specular edges (the analytic sun disc). Full/off captures are byte-identical.

## Gate

`make gfx-rt-diet-check` / `python3 scripts/gfx_rt_diet_check.py`:

- runs the default fixture at frame 300 with off, off, on captures;
- requires the off path to be byte-identical;
- requires on PSNR >= 48 dB, mean <= 0.20, max channel <= 96;
- requires the env switch and the preset field to both reduce measured RT
  VRAM by at least 15%.
