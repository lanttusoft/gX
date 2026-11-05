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
  fog, diffraction spikes, 320x240 Soft output and Max quality. Boots with
  `--cartridge-y2k` (the same composition as `--showcase-y2k` plus the
  authored 128 px brushed-metal set, diffraction and the console profile).
- **RAY-1 tech demo** — raymarched SDF scene at full quality, reflections and
  shadows through the ray path.
- **Particle/physics sandbox** — uses the fast CPU and cheap low-res overdraw.
- **Console shell** — boot menu, cartridge select, video options, save states,
  controller mapping, emulator-style OSD. First slice: `--console-shell` opens
  the gX-240 boot view (cartridge slot, video output, save states); the Y2K
  card cold-boots the cartridge at 320x240 Soft with Max quality and
  diffraction.

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
| Aperture diffraction | PRISM-1 |
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

### PRISM-1 diffraction spikes

Bright HDR glints (water sun sparkles, chrome, neon) can emit aperture
diffraction spikes. The feature is scene-authored and fully tunable:

- threshold + soft knee on HDR luma,
- intensity and spike length,
- aperture blade count (4/6/8/9 rays) and rotation,
- chromatic dispersion,
- twinkle/sparkle amount,
- tap count (4–16; half-res map).

The pass runs **after bloom** so the spikes stay sharp instead of dissolving
into the halo: a half-res star map is generated from `hdrScene`, then added
additively to the composite. The `diffraction.map` target is registered in the
buffer audit. The FX panel exposes every parameter; the feature depends on
bloom/HDR and is not forced by Console Max.

`GX_T_DIFFRACTION*` sets the tunables for headless probes;
`GX_T_DIFFRACTION_FIXTURE=1` gives a dark studio with the effect enabled.
`diffraction-check` runs the production shader on a synthetic point and pins
six/eight-ray stars, threshold/midtone rejection and the chromatic centroid
order. It runs as part of `make check`.

<!-- //ai: backlog reconciled against the generic rendering experiments. -->
The console shell boot view, Y2K cartridge, save states, CRT-safe idle blanking,
boot OSD and ESC system menu now exist. The native GUI remains window-sized;
only the scene is 320x240. The current render-then-upscale path is not a
supersampled render/output split.

Generic deterministic dither, palette quantization and interlace experiments
already have opt-in hooks and gates in `docs/rendering_backlog.md`:
`make dither-check`, `make palette-check` and `make interlace-check`. They are
not yet wired as a gX-240/PRISM-1 profile. The existing EGA16/RGB332 modes are
not 16-bit output quantization, and interlace patterns do not provide a full
CRT mask or scanline treatment.

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
- **Console 240p — Dither/CRT** (later): expose the existing deterministic
  dither/interlace experiments through the console profile, then define the
  PRISM-1 output format and any composite/scanline/phosphor-mask treatment.
  The generic palette modes are reduced palettes, not 16-bit output. Keep the
  effects opt-in and independently testable.
- **gX-240 console shell** — `--console-shell` opens the boot view: cartridge
  select (Y2K CGI, RAY-1, particle sandbox, empty slot), video output
  (320x240 Soft/Sharp, Native) and save-state slots. Booting Y2K from the
  shell applies the 320x240 Soft profile, cold-boots the GFX renderer and
  rebuilds the authored scene; the shell keeps the native GUI at window size.
  After 120 s without input the shell (and cartridge playback) fades to a
  black screen with a dim drifting gX mark; the first input is swallowed so
  it wakes the display without activating a hidden control.
  `GX_CONSOLE_IDLE_SECONDS` shortens the timeout for probes. Cartridge boot
  shows a short boot banner (gX-240 / CARTRIDGE 01 Y2K CGI) that fades after
  seven seconds; `GX_T_CARTRIDGE_OSD=0` disables that scene-rendered banner.
- <!-- //ai: GX-I010 live status slice; timing controls remain separate work. -->
  **Live console OSD** — a native-resolution status chip shows actual scene
  render dimensions, Soft/Sharp or Native presentation, current cartridge and
  last operated slot (slot 1 on entry). Loading another slot from the shell
  displays that slot; F5/F8 and pause actions still target slot 1. READY means
  a slot file exists, not that it has been
  validated; EMPTY means no file exists. Save/load hotkeys and menu actions
  share SAVED, LOADED, SAVE FAILED and LOAD FAILED feedback.
  Status appears for four seconds on entry, video changes and idle wake, stays
  visible while the pause menu is open, and fades over the final half-second.
  Action feedback also lasts four seconds, then returns to READY/EMPTY if the
  menu remains open. The chip is hidden throughout idle fading/blanking;
  renderer-path blanking covers the cartridge and overlays too. It never
  changes the display mode or claims a nominal FPS is a monitor refresh rate.
  `GX_CONSOLE_OSD=0` disables this native chip independently of the boot banner.
  Pause controls own only their explicit callbacks: Resume, Save and Video
  must not inherit a legacy switch-to-title action.
- <!-- //ai: ESC system-menu behavior and its focused gate are session additions. -->
  **Cartridge system menu** — ESC opens the native-resolution pause overlay
  during cartridge playback. Resume/ESC closes it; Save Slot 1 and Load Slot 1
  mirror F5/F8, a successful save refreshes the slot label in place, Video
  cycles Soft → Sharp → Native, and Exit to Shell returns to the gX-240 boot
  view. Controller slot 0 maps D-pad to navigate, south face to confirm, east
  face to go back, and Start to open/close the menu during playback. The shell
  view and overlay use the shared keyboard/gamepad focus navigation.
- <!-- //ai: compact native shell, save browser and cartridge pause layout. -->
  **Compact native UI** — the shell groups cartridges and video/save controls
  in two columns inside a centered, aspect-fitted 720x560 panel (reference
  window 1024x768). The cartridge pause panel uses 560x560. Both keep native
  GUI coordinates and scale to fit 4:3 or widescreen windows, independently of
  the 320x240 scene target. Save-slot descriptions are labels, not dead focus
  stops; LOAD has its own non-overlapping control. Save-browser BACK/East
  returns to the console shell. Native editor/tool panels are unchanged.
- **Save states** — three slots in `save/console_slot_N.json` hold the
  cartridge id, video profile and orbit camera. While a cartridge runs, **F5**
  saves slot 1 and **F8** loads it; the shell's Save states view lists the
  slots and loads them. `GX_CONSOLE_SAVE_DIR` redirects the folder for probes,
  and the harness guard (`GX_NO_SETTINGS_SAVE=1`) refuses writes.
- **Y2K CGI cartridge** — `--cartridge-y2k` boots the fixed profile: 320x240
  scene with Soft upscale, the Y2K composition, 128 px brushed-metal
  albedo/normal/ORM on the low-poly forms, Max-quality cascade (4x MSAA, 4 CSM
  cascades, 64-sample GTAO, full-res SSR when authored, auto exposure) and
  6-blade diffraction on the neon/glass glints. `GX_CARTRIDGE_DIFFRACTION=0`
  gives the A/B control.

## Non-goals

- No ROM loading or real-console emulation.
- No borrowed console names, logos or BIOS text.
- No change to the default crisp/modern presentation.
- No requirement that every scene fit 240p; it is an opt-in profile.

## Gates

- Virtual screen trace: `virtual=320x240`, `window=1024x768`,
  `render=320x240`, `filter=soft|sharp`.
- Soft vs Sharp high-frequency-energy difference.
- Diffraction shape: six/eight-ray stars, threshold/midtone rejection,
  chromatic dispersion (`make diffraction-check`).
- Max quality preserves the authored mask (planar/FXAA/gfx-scene, no forced
  SSR/TAA/DOF) and applies shader 2 + 4x MSAA.
- Full Max 320x240 vs 1024x768 relative frame budget (>=10% faster).
- Input mapping round-trip at the virtual scale.
- Retro asset budget (texture dimensions, triangle/material counts).
- Resolution/buffer audit: cold 320 vs runtime-resized 320 target sizes and
  image parity (33 active targets, ~0.62/255 mean difference).
- Y2K cartridge: 320x240 Soft trace, 4x MSAA quality 3, textured low-poly
  scene, Max quality, diffraction contribution and the 160x120 star-map
  target (`make cartridge-check`).
- Console shell: boot-view trace, Sharp video route (320x240 filter 0),
  save-slot route, CRT-safe idle blank/wake with the wake input swallowed,
  the cartridge OSD banner, F5/F8 save-load with nondefault camera restore
  after a camera cut (plus a no-load control), the shell
  LOAD slot route, and a cartridge boot from the shell that cold-boots GFX
  and reaches the 320x240 Soft/4x MSAA/diffraction profile. The ESC menu gate
  covers save/load, video cycling, Exit to Shell and the controller
  Start/D-pad/south/east mapping, including held-Start edge behavior and
  nonprimary-pad isolation. Shell, populated saves and pause layouts are
  checked at 640x480, 1024x768 and 1280x720: complete element maps, text fit,
  usable control sizes, no overlapping labels/controls, and four-direction
  controller focus traversal/wrap. Empty-save focus and save-browser return
  routes are covered too. Clicks resolve from the engine's current element
  map rather than fixed pixel coordinates; the wake test clicks a cartridge
  while blanked. Every capture rejects engine errors and writes its log beside
  the screenshot under the scratch directory
  (`make console-check`).
  Ten display-free gate tests reject missing/partial telemetry, stale or
  ambiguous click targets, clipped/tiny/off-screen/overlapping controls,
  missing or incorrectly ordered controller focus events, and ambient input/UI
  hooks leaking back into the subprocess environment. They also run
  in `make check-fast` and `make check`. This is focused console coverage, not
  physical-controller, CRT, every-theme or arbitrary-window-size validation.
- <!-- //ai: OSD draw/effect and intentional-fault coverage. -->
  Native OSD: three-size text/bounds and visible-pixel A/B checks, disable and
  auto-hide, live Soft → Sharp → Native changes, hotkey save/load and explicit
  failure feedback, renderer idle blank/wake, and a slot-3 shell load
  (`scripts/console_osd_check.py`, included by `make console-check` and
  `make check`). Five display-free rejection tests cover missing/wrong OSD
  evidence, clipped/tiny/off-screen text and narrow expected-error allowlists;
  they also run in `make check-fast`.
- No UI clipping with native tool panels at window resolution.

## Follow-up

<!-- //ai: retain idea outcomes as well as the current work list. -->
The [idea ledger](ideas.md) records decisions and completed/deferred variants
(`GX-I001`–`GX-I014`). Check it before proposing these items again; this section
remains the console's detailed open-work list.

- Frame-advance/fast-forward controls with defined simulation timing semantics
  (the remaining part of `GX-I010`); live status/slot feedback is implemented.
- Supersampled render/output split, separately specified from the current
  320x240 scene render followed by window upscale.
- PRISM-1 presentation work: choose 16-bit quantization semantics and validate
  scanline/composite/phosphor-mask variants. Reuse the generic dither,
  quantization and interlace hooks where appropriate; keep each effect
  deterministic, opt-in and independently gated. Film grain remains an
  optional experiment, not a decided console requirement.
- Complete or add bootable cartridge profiles beyond the Y2K path.
- Validate on a physical CRT across supported modes, checking monitor limits
  before testing.
