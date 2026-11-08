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
  TAA is off in the console profile; FXAA and optional 2x SSAA handle AA
  (`GX_T_TAA=1` is the A/B probe hook). The mirror floor uses the exact
  planar-reflection path and the cartridge runs SSR-free, so the mirror shows
  one pre-bloom reflection instead of an SSR/env double image and bloom keeps
  the light glow around it.
- **RAY-1 tech demo** — bootable raymarch profile (`--cartridge-ray1` or the
  shell): the default SDF scene at Full quality (render scale 1.0, 256 steps,
  4 bounces), an authored three-quarter overview camera, free-camera movement,
  the console pause menu and save-state camera restore. Defaults to 320x240
  Soft; shell selection can layer SSAA and PRISM-1.
- **Particle/physics sandbox** — bootable `--cartridge-particle` profile:
  deterministic sandbox rebuild, authored overview camera, console pause menu
  and save-state camera restore. Uses the fast CPU and cheap low-res overdraw.
  Its sandbox status HUD stays visible by design; Y2K is the clean
  dev-HUD-free presentation profile.
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
only the scene is 320x240. Standard 240p renders directly at 320x240; the
opt-in 2x SSAA path renders at 640x480 and resolves back to 320x240.

Generic deterministic dither, palette quantization and interlace experiments
remain available through `docs/rendering_backlog.md`: `make dither-check`,
`make palette-check` and `make interlace-check`. PRISM-1 is a separate
console-specific output pass after the 320x240 resolve. It does not map the
EGA16/RGB332 palette modes onto 16-bit output and does not treat the generic
interlace pass as a CRT mask; it uses a fixed 4x4 Bayer threshold, RGB565
quantization, horizontal scanlines and a subtle RGB phosphor triad.

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

Avoid 120 Hz modes unless the monitor is confirmed free of coil whine; check
the monitor's documented limits first. Physical CRT validation remains parked
by the user; do not change display modes automatically.

## Presets

- **Console 240p — Sharp**: 320x240 scene render, point upscale.
- **Console 240p — Soft**: 320x240 scene render, bilinear upscale.
- **Console 240p — Max**: 320x240 scene render, Ultra quality applied to the
  scene's authored features, Soft/Sharp present filter. Current: GFX Features
  panel button and `GX_CONSOLE_MAX=1`.
- **Console 240p — SSAA 2x** (opt-in): 640x480 scene render, resolved to a
  320x240 output, then Soft-upscaled to the window. The native GUI/OSD stay
  window-sized. Available in the shell video column and the pause Video cycle;
  this slice covers Y2K, RAY-1 and the particle sandbox (the last through its
  GUI-composite background capture).
- **Console 240p — Full** (experimental): all effects + Ultra quality; use
  only for scenes authored around SSR/TAA/DOF/volumetrics. `GX_CONSOLE_MAX_FULL=1`
  for demos/gates.
- **Console 240p — PRISM-1** (opt-in toggle): RGB565 (5:6:5) quantization with
  fixed 4x4 Bayer dither, horizontal scanlines and a subtle RGB phosphor triad
  on the 320x240 output. It layers over Soft/Sharp and 2x SSAA after the
  resolve, leaves the native GUI/OSD at window size, and adds no NTSC composite
  smear. Available in the shell and pause menu; this slice covers Y2K, RAY-1
  and the particle sandbox.
- **Console 240p — RAY-1** (cartridge): the raymarch tech demo at Full
  quality with a fixed authored camera, boots with `--cartridge-ray1` or the
  shell RAY-1 card. F5/F8 and the pause menu save/load the video profile and
  free-camera view.
- **Console 240p — Particle Sandbox** (cartridge): boots with
  `--cartridge-particle` or the shell card, rebuilds the deterministic
  rigid-body sandbox and installs the authored overview camera. F5/F8 and the
  pause menu save/load the video profile and free-camera view.
- **Console 240p — Film grain** (opt-in): subtle animated monochrome grain on
  the 320x240 console output, independent of PRISM-1 and available with
  Soft/Sharp/SSAA and the particle composite route. The frame-indexed 24 Hz
  pattern is deterministic for fixed captures; `GX_CONSOLE_GRAIN_ANIMATE=0`
  freezes it for stills.
- **gX-240 console shell** — `--console-shell` opens the boot view: cartridge
  select (Y2K CGI, RAY-1, particle sandbox, empty slot), video output
  (320x240 Soft/Sharp/SSAA 2x, Native), PRISM-1 and film-grain toggles and
  save-state slots. Booting Y2K from the shell honors the selected 240p filter/SSAA
  (native falls back to Soft), cold-boots the GFX renderer and rebuilds the
  authored scene; booting RAY-1 applies the same video selection and resets the
  authored raymarch scene/camera; booting the particle sandbox resets its
  authored content/camera. The shell keeps the native GUI at window size.
  After 120 s without input the shell (and cartridge playback) fades to a
  black screen with a dim drifting gX mark; the first input is swallowed so
  it wakes the display without activating a hidden control.
  `GX_CONSOLE_IDLE_SECONDS` shortens the timeout for probes. Cartridge boot
  shows a short boot banner (gX-240 / CARTRIDGE 01 Y2K CGI) that fades after
  seven seconds; `GX_T_CARTRIDGE_OSD=0` disables that scene-rendered banner.
- <!-- //ai: GX-I010 live status slice; timing controls remain separate work. -->
  **Live console OSD** — a native-resolution status chip shows actual scene
  render dimensions (and, for SSAA, the 320x240 output), Soft/Sharp/Native or
  SSAA 2x presentation, PRISM-1/film-grain on/off, current cartridge and
  last operated slot (slot 1 on entry). Loading another slot from the shell
  displays that slot; F5/F8 and pause actions still target slot 1. READY means
  a slot file exists, not that it has been
  validated; EMPTY means no file exists. Save/load hotkeys and menu actions
  share SAVED, LOADED, SAVE FAILED and LOAD FAILED feedback.
  Status appears for four seconds on entry, video changes and idle wake, stays
  visible while the pause menu is open, and fades over the final half-second.
  While the console pause menu is open, the chip appends `PAUSED`, `STEP` or
  the armed `FF 2x`/`FF 4x`/`FF 8x` state; normal playback has no playback
  suffix. Action feedback also lasts four seconds, then returns to READY/EMPTY
  if the menu remains open. The chip is hidden throughout idle fading/blanking;
  renderer-path blanking covers the cartridge and overlays too. It never
  changes the display mode or claims a nominal FPS is a monitor refresh rate.
  `GX_CONSOLE_OSD=0` disables this native chip independently of the boot banner.
  Pause controls own only their explicit callbacks: Resume, Save and Video
  must not inherit a legacy switch-to-title action.
- <!-- //ai: ESC system-menu behavior and its focused gate are session additions. -->
  **Cartridge system menu** — ESC opens the native-resolution pause overlay
  during cartridge playback. The menu is a real pause: scene/renderer
  simulation freezes while it is open; UI, OSD and audio feedback keep real
  time. Resume/ESC closes it; Save Slot 1 and Load Slot 1 mirror F5/F8, a
  successful save refreshes the slot label in place, Video cycles
  Soft → Sharp → SSAA 2x → Native, separate PRISM-1 and GRAIN rows toggle
  the output treatments, and Exit to Shell returns to the gX-240 boot view.
  Step advances exactly one 1/60 s simulation tick and keeps the menu open.
  Fast Forward cycles Off → 2x → 4x → 8x while paused; Resume runs at the
  selected speed. F6 and F7 mirror Step and Fast Forward while the menu is
  open. Loading a save, changing scene or exiting to the shell resets playback
  to normal speed. Controller slot 0 maps D-pad to navigate, south face to
  confirm, east face to go back, and Start to open/close the menu during
  playback. The shell view and overlay use the shared keyboard/gamepad focus
  navigation.
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
  cascades, 64-sample GTAO, auto exposure), an exact planar floor reflection
  (the cartridge is SSR-free; the floor opts out of SSR) and 6-blade
  diffraction on the neon/glass glints. `GX_CARTRIDGE_DIFFRACTION=0` gives the
  A/B control.

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
  scene, Max quality, the exact planar floor with SSR disabled (one pre-bloom
  reflection), diffraction contribution and the 160x120 star-map target
  (`make cartridge-check`).
- Console shell: boot-view trace, Sharp video route (320x240 filter 0),
  save-slot route, CRT-safe idle blank/wake with the wake input swallowed,
  the cartridge OSD banner, F5/F8 save-load with nondefault camera restore
  after a camera cut (plus a no-load control), the shell
  LOAD slot route, and a cartridge boot from the shell that cold-boots GFX
  and reaches the 320x240 Soft/4x MSAA/diffraction profile. A second cold
  boot after an authored camera cut must return to the authored view, and
  save slots store an explicit native/virtual mode so a native slot stays
  native after a window-size mismatch, and the modal pause claims the UI
  cursor over the captured 3D scene and restores the captured state on close.
  The ESC menu gate covers save/load, video cycling, Exit to Shell and the
  controller Start/D-pad/south/east mapping, including held-Start edge
  behavior and nonprimary-pad isolation. Shell, populated saves and pause layouts are
  checked at 640x480, 1024x768 and 1280x720: complete element maps, text fit,
  usable control sizes, no overlapping labels/controls, and four-direction
  controller focus traversal/wrap. Empty-save focus and save-browser return
  routes are covered too. Clicks resolve from the engine's current element
  map rather than fixed pixel coordinates; the wake test clicks a cartridge
  while blanked and a second wake presses F5; neither reaches a card nor the
  save path. Every capture rejects engine errors, `GL_INVALID_OPERATION` and
  HIGH GL debug messages while keeping MEDIUM shader-recompile notices
  informational, and writes its log beside the screenshot under the scratch
  directory (`make console-check`).
  Eleven display-free gate tests reject missing/partial telemetry, stale or
  ambiguous click targets, clipped/tiny/off-screen/overlapping controls,
  missing or incorrectly ordered controller focus events, and ambient input/UI
  hooks leaking back into the subprocess environment. They also run
  in `make check-fast` and `make check`. This is focused console coverage, not
  physical-controller, CRT, every-theme or arbitrary-window-size validation.
- <!-- //ai: OSD draw/effect and intentional-fault coverage. -->
  Native OSD: three-size text/bounds and visible-pixel A/B checks, disable and
  auto-hide, live Soft → Sharp → Native changes, hotkey save/load and explicit
  failure feedback, renderer idle blank/wake, a slot-3 shell load and a
  native-mode slot load after a saved window-size mismatch
  (`scripts/console_osd_check.py`, included by `make console-check` and
  `make check`). Eight display-free rejection tests cover missing/wrong OSD
  evidence, clipped/tiny/off-screen text, the PRISM flag and narrow
  expected-error allowlists; they also run in `make check-fast`.
- <!-- //ai: GX-I010 playback timing and control coverage. -->
  Playback: the pause menu freezes simulation; F6 and the menu Step button
  advance exactly 1/60 s; F7 cycles 2x/4x/8x/off; 4x playback advances 4/60
  per frame; controller Start/D-pad/South resumes at 2x; an F8 load resets
  playback; the OSD reports PAUSED/STEP/FF (`scripts/console_playback_check.py`,
  included by `make console-check` and `make check`). Four display-free tests
  reject missing/malformed traces, wrong step counts/deltas, wrong FF
  multipliers and missing OSD playback state; they also run in
  `make check-fast`.
- <!-- //ai: approved 2x SSAA slice; 4x remains follow-up. -->
  SSAA 2x: the pause menu and shell select the mode; the OSD reports
  `render=640x480 out=320x240 mode=SSAA2x`; Y2K and RAY-1 use the resolved
  direct-blit path and the particle sandbox consumes the resolve through its
  GUI-composite background capture (`CONSOLE_COMPOSITE: present=320x240
  source=640x480`); save state stores and restores `ssaa=2`; a matched 60 Hz
  `--gfx-profile` run must
  stay at or below **3x** the 320x240 GPU frame cost (about 1.5x on the
  current host; `scripts/console_ssaa_check.py`, included by
  `make console-check` and `make check`). Five display-free tests reject wrong
  OSD splits, bad save factors, malformed profiles and over-budget ratios; they
  also run in `make check-fast`. `GX_CONSOLE_SSAA=1` is an automation hook for
  matched performance runs, not a second product default.
- <!-- //ai: 4x probe measured, not a product mode. -->
  4x SSAA probe: `GX_CONSOLE_SSAA=4` renders 1280x960 and resolves to 320x240
  through the same path, but remains hidden from shell/pause/save states.
  `make ssaa4x-probe` requires deterministic 4x output and records the cost.
  Measured **3.507x** the 320x240 frame (2.229x the 2x frame) on the current
  host, above the 3x product cap; 4x is declined as a product mode. The hook
  stays for future hardware/quality re-evaluation.
- <!-- //ai: approved PRISM-1 output slice; composite/particle remain follow-up. -->
  PRISM-1: the shell and pause menu toggle it independently of the video mode;
  the OSD reports `prism=on/off` and appends `PRISM-1` to the header. A
  deterministic screenshot differential on the smooth showcase fixture requires
  RGB565 channel-level counts (red/blue ≤40, green ≤72 in the measured phase
  band), a 1.15..1.9 scanline ratio, the expected red/green/blue triad phase,
  full-viewport coverage and a byte-identical repeat. The gate also covers Y2K
  Soft and SSAA, RAY-1, the particle composite route (`CONSOLE_COMPOSITE:
  present=320x240 source=320x240 prism=1`), save/load of `video.prism=1`,
  native-mode disablement and label bounds. A matched full-frame CPU run must
  stay at or below **1.25x** the baseline (about 1.0x measured; the GPU pass is
  one 320x240 fullscreen shade) (`scripts/console_prism_check.py`, included by
  `make console-check` and `make check`). Five display-free tests reject bad
  budgets, scanline/triad/level fixtures and unquantized gradients; they also
  run in `make check-fast`. `GX_CONSOLE_PRISM=0/1` is an automation hook for
  deterministic A/B runs, not a second product default.
- <!-- //ai: GX-I011 RAY-1 authored profile and save-camera gate. -->
  RAY-1 profile: `--cartridge-ray1` and shell boot trace Full quality
  (`renderScale 1.00`, 256 steps, 4 bounces) and the authored camera; the OSD
  reports `cart=RAY-1` at 320x240 Soft by default. The raw `--raymarch` scene
  is the visual baseline, the profile frame must be deterministic, the pause
  menu must freeze/report PAUSED, and F5/F8 must persist/restore the video
  profile plus `camera3d` (`scripts/console_ray1_check.py`, included by
  `make console-check` and `make check`). Three display-free tests reject wrong
  quality/camera traces and save JSON; they also run in `make check-fast`.
- <!-- //ai: GX-I013 opt-in animated film grain. -->
  Film grain: the shell and pause menu toggle a subtle animated monochrome
  grain independently of PRISM; the OSD reports `grain=on/off` and appends
  `GRAIN` to the header. A fixed-frame repeat must be byte-identical, a flat
  showcase crop must gain high-frequency texture, and a later frame must
  change while the grain-free control stays closer. The gate also covers
  shell boot, pause toggle labels, save/load of `video.grain=1`, the particle
  SSAA composite route and native-mode disablement
  (`scripts/console_grain_check.py`, included by `make console-check` and
  `make check`).
  Three display-free tests cover flat-field separation and save fields; they
  also run in `make check-fast`. `GX_CONSOLE_GRAIN=0/1` is an automation hook;
  `GX_CONSOLE_GRAIN_ANIMATE=0` freezes the pattern for stills.
- <!-- //ai: GX-I011 particle authored profile and save-camera gate. -->
  Particle sandbox: `--cartridge-particle` and shell boot rebuild the sandbox,
  install the authored camera and report `cart=PARTICLE SANDBOX`; a
  byte-identical repeat must hold, the pause menu must freeze/report PAUSED,
  and F5/F8 must persist/restore the video profile plus `camera3d`
  (`scripts/console_particle_check.py`, included by `make console-check` and
  `make check`). Three display-free tests reject wrong camera/content traces
  and save JSON; they also run in `make check-fast`.
- No UI clipping with native tool panels at window resolution.

## Follow-up

<!-- //ai: retain idea outcomes as well as the current work list. -->
The [idea ledger](ideas.md) records decisions and completed/deferred variants
(`GX-I001`–`GX-I014`). Check it before proposing these items again; this section
remains the console's detailed open-work list.

- GX-I010 live status, save feedback and playback timing are implemented
  locally (real pause, 1/60 step, 2x/4x/8x); no further timing controls are
  planned in this slice.
- GX-I008 2x SSAA is implemented for Y2K, RAY-1 and the particle sandbox.
  The 4x probe measured 3.507x the 320x240 frame cost, above the 3x cap, and is
  declined as a product mode; only the hidden re-evaluation hook remains.
- PRISM-1 RGB565, fixed Bayer, scanlines and the subtle RGB triad are
  implemented for Y2K, RAY-1 and the particle sandbox in standard 240p and 2x
  SSAA. NTSC composite smear was not part of the approved contract.
- Film grain is implemented as an opt-in experiment, not a required PRISM-1
  feature. It can be disabled by default and is persisted only when selected.
- Y2K, RAY-1 and the particle sandbox now have authored cartridge profiles
  with save-state camera restore. Further work is optional content expansion,
  not a missing console integration.
- Physical CRT/controller validation is parked by the user as of 2026-11-06.
  Do not schedule it or change display modes without new hardware and explicit
  approval.
