# gX-240 — the lost console profile

Status: opt-in presentation profile. gX-240 is a **fictional console**, not
an emulator: it does not load ROMs, run console software, or claim
compatibility with any real hardware. The name, chips and history are
original lore for gX's Y2K presentation mode.

## Thesis

> Fewer pixels. Better pixels.

The industry sold resolution; gX-240 sold **quality per pixel**. Its 320x240
output is deliberate: the same silicon budget went into color depth,
per-pixel lighting, ray effects, filtering and a high-quality composite
instead of more output pixels. The result reads as late-1990s/early-2000s
pre-rendered CGI in motion rather than a low-resolution game.

The trade is measurable:

| Budget | Pixels | Relative |
| --- | ---: | ---: |
| 320x240 output | 76,800 | 1.0x |
| 640x480 (4x SSAA) | 307,200 | 4.0x |
| 960x720 (9x SSAA) | 691,200 | 9.0x |
| 1024x768 native | 786,432 | 10.2x |

At 320x240 you can afford roughly **8–10 samples per output pixel** for the
same sample budget as 1024x768 native. Supersampling improves edges and
shading, not asset detail; that is why low-resolution textures and low-poly
geometry still look intentional.

## Hardware

| Part | Role |
| --- | --- |
| **LX-1** | 1.1 GHz dual-issue 64-bit RISC, 32 KB L1 / 512 KB L2, no SIMD, narrow memory bus. Fast for 2003, but bandwidth-starved: developers used it for software SDF raymarching, physics and particles. |
| **RASTER-1** | Fixed-function triangle rasterizer, 320x240, four texture units, bilinear/trilinear, no shaders, 16-bit depth, 24-bit color. Affine texture mapping and vertex snapping on some paths. |
| **TEXEL-1** | Texture/filter ASIC: mipmaps, S3TC-like compression, palette support, 2x anisotropic. |
| **RAY-1** | Fixed-function ray ASIC: ray-box/ray-sphere intersect, low precision, 320x240. Used for reflections, shadows and raymarch tech demos. |
| **PRISM-1** | Post/display ASIC: dither to 16-bit, composite blur, scanlines, gamma, 240p/480i output. |
| **AUDIO-1** | 64-channel wavetable/synth DSP. |

Memory: 16 MB VRAM, 64 MB main, 2 MB texture cache. Media: cartridge or a
proprietary disc. Output: 320x240 at 60 Hz, 640x480 at 30, composite/S-video.

## Why it was lost

gX-240 launched in 2003, between the 6th and 7th generations. It was expensive
to build, hard to tool for, and shipped a 240p signal into an HD race. The
fast CPU and the RAY-1 ASIC made beautiful pixels, but reviewers counted
pixels. The console died; gX remembers it.

## Cartridges

A cartridge is a gX scene preset plus an asset set and a settings profile:

- **Y2K CGI demo** — low-poly, 128–256 px textures, PBR, soft shadows, bloom,
  lens flare, fog, 320x240 Soft output.
- **RAY-1 tech demo** — raymarched SDF scene at full quality, reflections and
  shadows through the ray path.
- **Particle/physics sandbox** — uses the fast CPU and cheap low-res overdraw.
- **Console shell** — boot menu, cartridge select, video options, save states,
  controller mapping, emulator-style OSD.

## gX mapping

| gX surface | gX-240 use |
| --- | --- |
| `maindata.screenWidth/Height` | GUI virtual screen (window-sized by default) |
| `maindata.windowWidth/Height` | actual window and native ImGui tools |
| `maindata.renderWidth/Height` | scene render target; `--virtualWidth/Height` sets it explicitly |
| `gX_RHI_Blit` present filter | Soft (linear) or Sharp (nearest) |
| Raster path | RASTER-1 |
| Texture filtering / BC sidecars | TEXEL-1 |
| Raymarch / SSR / ray effects | RAY-1 |
| Present/post filter | PRISM-1 |
| Synth / wavetable | AUDIO-1 |
| Scene preset | cartridge |
| Basic UI level | console-style shell controls |

## Current slice

The first implementation adds:

- `--virtualWidth/--virtualHeight` — set the **scene render target**
  independently of the window and GUI. The native title/settings menus stay at
  window resolution, so they remain readable; the scene is 320x240.
- Scene mode — `renderScale = 320 / windowWidth` is the equivalent for 4:3
  windows and remains available for custom scaling.
- `--presentFilter sharp|soft` — nearest or linear upscale of the scene into
  the window.
- `GX_VIRTUAL_WIDTH/HEIGHT` and `GX_PRESENT_FILTER` audit hooks.
- A persisted `presentFilter` settings row and an Accessibility **Console
  display** submenu with 320x240 scene Soft/Sharp and Native.
- A **Console Max** button in the GFX Features panel: Ultra quality
  (16x AF, high shader, 4x MSAA, occlusion culling) applied to the scene's
  **authored** feature set. It raises 4 CSM cascades when shadows exist,
  64-sample SSAO/GTAO when AO exists, full-res SSR when SSR exists, auto
  exposure when bloom exists, layered point shadows on 4.3 and raymarch
  quality 3 when active. It does **not** force SSR, TAA, DOF, motion blur,
  volumetrics or parallax.
- The existing **Full** template remains the experimental all-effects path.
  `GX_CONSOLE_MAX=1` applies the quality tier; `GX_CONSOLE_MAX_FULL=1` applies
  Full + quality for demos and the budget gate.
- The editor-host gate pins the virtual/render/window trace, the
  Soft-vs-Sharp high-frequency difference, the quality mask (planar + FXAA +
  gfx-scene only, no forced SSR/TAA/DOF), the quality tier, and the Full
  320-vs-1024 frame budget.
- Resolution/buffer audit: `GX_RESIZE_TEST=WxH@frame` changes the virtual
  target at runtime; `GX_GFX_BUFFER_AUDIT` target summaries now include their
  size. The editor-host gate compares a cold 320 start with a 1024→320 runtime
  resize over all 32 active targets and requires image parity (measured
  0.62/255 mean difference).

Peer review found why the old Max looked broken: the default scene declares
only **planar reflections + FXAA**, while Full forced 18 features. SSR
reflected the studio geometry into the floor (screen-space ghosts), TAA then
accumulated those reflections (tails), and DOF/motion blur softened the frame.
On the default scene: default mean luma **123**, old full Max **173**,
Max-without-SSR/TAA/DOF/SSGI/volumetrics **138**. Max Quality now preserves
the scene mask, so that artifact chain is gone; Full is still available for
scenes authored around those effects.

`editor-host-check` measures quality Max at **~4.7 ms at either resolution**
— the default scene is fixed-cost-bound, so quality Max makes no
resolution-speed claim. The **Full** path is the headroom proof:
**9.8 ms at 320x240 vs 22.9 ms at 1024x768** (120-frame post-warmup averages
on the reference GTX 1060), and the gate requires at least 10% headroom.

The console shell, cartridge browser, OSD, save states, a compact 320x240
native GUI, supersampled render/output split, dither and CRT filters are
follow-ups. Until the shell exists, the native GUI stays at window resolution
and only the scene is 240p.

## CRT notes

A 70 kHz VGA monitor handles these modes without custom modelines in most
cases:

| Mode | Hsync | Scale of 320x240 | Use |
| --- | ---: | ---: | --- |
| 640x480 @ 100 Hz | ~52.5 kHz | 2x | `--virtualWidth 320 --virtualHeight 240` |
| 800x600 @ 100 Hz | ~62.8 kHz | 2.5x | same; 640x480 render stays exact |
| 960x720 @ 85 Hz | ~63.8 kHz | 3x | same |
| 1280x960 @ 60 Hz | ~60 kHz | 4x | same |
| 320x240 @ 120 Hz | ~31.5 kHz | 1x | true 240p, scene-only |

Do not use 320x240@60 on a VGA monitor: its ~15.7 kHz hsync is below the
normal VGA sync range. Set the CRT mode with the OS/GPU first; gX uses the
current monitor mode in fullscreen. The native GUI stays at the window size;
only the scene render target is 320x240.

## Presets

- **Console 240p — Sharp**: 320x240 scene render, point upscale.
- **Console 240p — Soft**: 320x240 scene render, bilinear upscale.
- **Console 240p — Max**: 320x240 scene render, Ultra quality applied to the
  scene's authored features, Soft/Sharp present filter. Current: GFX Features
  panel button and `GX_CONSOLE_MAX=1`.
- **Console 240p — Full** (experimental): all effects + Ultra quality; use
  only for scenes authored around SSR/TAA/DOF/volumetrics. `GX_CONSOLE_MAX_FULL=1`
  for demos/gates.
- **Console 240p — Dither/CRT** (later): 16-bit quantization, Bayer dither,
  scanlines/CRT mask.

## Non-goals

- No ROM loading or real-console emulation.
- No borrowed console names, logos or BIOS text.
- No change to the default crisp/modern presentation.
- No requirement that every scene fit 240p; it is an opt-in profile.

## Gates

- Virtual screen trace: `virtual=320x240`, `window=1024x768`,
  `render=320x240`, `filter=soft|sharp`.
- Soft vs Sharp high-frequency-energy difference.
- Max quality preserves the authored mask (planar/FXAA/gfx-scene, no forced
  SSR/TAA/DOF) and applies shader 2 + 4x MSAA.
- Full Max 320x240 vs 1024x768 relative frame budget (>=10% faster).
- Input mapping round-trip at the virtual scale.
- Retro asset budget (texture dimensions, triangle/material counts).
- Resolution/buffer audit: cold 320 vs runtime-resized 320 target sizes and
  image parity (32 active targets, ~0.6/255 mean difference).
- No UI clipping with native tool panels at window resolution.

## Follow-up

- **PRISM-1 diffraction spikes**: threshold the HDR bright pass, convolve with
  a 6/8-blade star kernel, add chromatic dispersion and rotate with the
  camera. Intended for water sun sparkles, neon, chrome and specular glints;
  scene-authored (Look/Bloom controls), not forced by Console Max.
- Console shell and boot sequence.
- OSD: `320x240 · SOFT · 60Hz`, save state, frame advance, fast-forward.
- Supersampled render/output split.
- Dither/CRT/film-grain variants.
