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
- The editor-host gate pins the virtual/render/window trace and the
  Soft-vs-Sharp high-frequency difference.

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
- **Console 240p — Max** (later): 320x240 output with a higher internal render
  size (4x/9x supersample), all GFX features on, raymarch full quality.
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
- Input mapping round-trip at the virtual scale.
- Max-effects frame budget at 320x240.
- Retro asset budget (texture dimensions, triangle/material counts).
- No UI clipping with native tool panels at window resolution.

## Follow-up

- Console shell and boot sequence.
- OSD: `320x240 · SOFT · 60Hz`, save state, frame advance, fast-forward.
- Supersampled render/output split.
- Dither/CRT/film-grain variants.
