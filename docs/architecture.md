# gX — Architecture

## Overview

gX is the release-bound distillation of the gameX research engine: raylib-only,
scoped to engine core + demo scenes, kept lean by construction. gameX remains
the experimental lab; gX carries only what passed the scope/distill/debt
filters (see `docs/pivot_gx.md` in the gameX repo). The tier model
(platform → kernel → shell → orchestration) is documented in
[`docs/tiers.md`](tiers.md).

## Dependency order

```
gxmath → common → wrappers → {input,camera,audio} → physics → {gui,renderer} → scene → main
```

- `gxmath/` — standalone math library (vectors, matrices, quaternions,
  calculus, easing, SDF). Zero dependencies.
- `common/` — heap, logging, X-macro enums, marshal (JSON/YAML/TOML/XML),
  shader compiler, path resolution, hashmap, pool, profiler, random,
  utils (string helpers, billboard, pointer casts).
- `wrappers/` — backend-agnostic types (Texture, Shader, Model, Font, Sound,
  Music, Mesh, Rectangle, Window, Input, Camera) + drawing/shape/text API.
  The only layer allowed to touch raylib types. `wrappers/physics` is the
  rigid-body wrapper — opaque world/body/joint/collider handles over ODE
  (CMake `GX_USE_ODE`, default on; no-op stubs when ODE is absent).
- `physics/` — shared simulation world module: fixed 60 Hz timestep +
  accumulator, sub-step cap, auto-tuning, and the body→matrix helper. Scenes
  spawn bodies into it; the main loop steps it (`gX_Physics_Update`).
- `rhi/` — thin GL surface on raylib's bundled glad (GL core 4.3 + GLES 3.2).
  Texture/FBO/buffer/state/caps/matrix helpers + batch + bindless + mesh.
- `backend/raylib/` — raylib wrapper implementations + default shader/texture
  accessors. The only place raylib headers are included.
- `renderer/` — gfx (3D forward PBR: models, lights, shadows, CSM, IBL, SSR,
  SSAO, bloom, TAA, reflections, decals, particles), background, postprocess,
  textwriter, sky, water, raymarch, analog (CRT/VHS), fractal, teletext,
  wireframe, generate, oscillo/lissajous/spectrum, ws2d, compositor.
- `gui/` — canvas-based immediate-mode GUI: builder (widgets), elements
  (15 sub-modules), style/css/markup/transition, templates (registry +
  engine demo templates), respond (scene→canvas routing).
- `scene/` — dispatcher (masterIndex switch, state flags) + title/battle/
  physics/synthvis demo scenes. Renderer scenes respond through the renderer's
  scene-config table.
- `main*` — parse (CLI flags), tests, load (order matters — backend/GL
  context before renderer/gui), loop (update/draw), unload.
- `module/` — dynamic module host (dlopen loader + hook registry) and
  `gx_shell.c`, the AngelScript shell module entry (`docs/dynamic_modules.md`).

## Load order (main_load.c)

1. MainData init → Settings init/load
2. GameData shim (ephemeris only) → Input → Camera → Random
3. **Backend init** (window, GL context, audio device) — must precede any GPU
   resource creation
4. Resource manager → GUI → Renderer → Scene → Audio

## Draw order (main_loop.c)

`ClearBG → Scene_Draw → Renderer_Draw → GUI_Draw`

Scenes own their background (title gradient, GFX lavender sky, renderer
canvases); GUI canvases composite on top at their canvasBox.

**Canvas-pass draw order**: raylib-native shapes/text queue until raylib's
flush (e.g. `gX_Drawing_BeginTexture`/`EndTexture`), while gX batch primitives
(lines, textured quads) execute immediately — a pass mixing both must call
`gX_Drawing_Flush()` between them or the deferred native geometry lands on top
regardless of source order (the synthvis panel beds exposed this).

## Scene switching

`--<scene>` CLI flag → `masterIndex` → `gX_MainData_Switch` → each subsystem
responds via `masterIndexSwitchResponse` (scene table + renderer scene-config
table + gui respond templates) → `Masterindex switched`.

## Configuration flags (common/config.h)

- `gX_ENABLE_GAME 0` — game content dropped (gamedata = ephemeris shim only)
- `gX_ENABLE_GFXAI 0` — AI probe not ported
- `gX_GUI_USE_IMGUI 0` — debug panels dropped
- `GX_USE_ANGELSCRIPT` (CMake, default OFF) — builds the `gx_shell.so`
  dynamic module (scripting + console + tour APIs; the exe stays lean and
  module-capable via `src/module/`); docs `scripting.md` + `dynamic_modules.md`,
  gates `make check-scripting` and the default build's `make check`
- Dropped subsystems: video (FFmpeg), globe (weather dep), demoscript
  (`.gxdemo`), game templates (MENU/QUESTLOG/BESTIARY/ESCAPEMENU → gX stubs)

## Modes of operation

`gX_SCENES` / `masterIndex` select the active **mode**; the code name is
`scene` for historical reasons. A mode owns:

- its renderer configuration (module update/draw flags, compositor layers,
  post-process, background preset) in `rendererSceneConfigs`;
- its input binds and camera response (`gX_Input_Respond`,
  `gX_Camera_Respond`);
- its GUI surfaces (`gX_GUI_Respond` routing or scene-owned
  `gX_GUI_AddMake`);
- its update/draw state in the scene dispatcher, plus a park hook for shared
  resources (physics world, PP chain, audio) when another mode takes over.

Mode switching is serialized through `masterIndex` (`gX_MainData_Switch` →
per-subsystem respond → `Masterindex switched`); every mode gets the same
composition stages in the main loop (`Scene_Draw → Renderer_Draw →
GUI_Draw`). Game content is one mode's payload, not the engine identity:
title is the launcher, battle carries the game demo, synthvis an instrument,
physics a simulation; renderer modes are composition studies. The identifiers keep the `scene` name (`gX_Scene`,
`gX_SCENES`, `masterIndex`) — read "scene" as "mode of operation".

`--title`, `--battle`, `--gfx` (cube/sphere/cone + lighting), `--showcase-y2k` (opt-in
Bryce/Y2K abstract terrain with glass/metal/neon materials, style pinned in
`docs/visual_direction.md`, preset `resources/tools/gfx_y2k.json`),
`--cartridge-y2k` (the Y2K cartridge: 320x240 Soft, authored 128 px
brushed-metal maps, Max quality, diffraction), `--console-shell` (the
gX-240 boot view with cartridge select and video output), `--analog`,
`--clock`, `--sky`, `--water`, `--textwriter`, `--synthvis`, `--fractal`,
`--teletext`, `--wireframe`, `--raymarch`, `--generate`.
`--physics` is the rigid-body props sandbox: bodies spawn into the shared
physics world and are drawn by the GFX renderer with interpolated transforms
(left-click spring-drag/spawn, right-click delete, `R` reset; `GX_PHYSICS_PROPS`
sets the count). It draws a status HUD and parks its content when another
scene takes over; the title DEMOS hub routes here.

**Guest assets** — external resource roots (other games' asset pools) can
be registered at runtime and injected into the showcase for perf testing:
`GX_ASSETS` / `--assets` (fallback-only search dirs) and
`--guest-model subpath[:count]` / `GX_GUEST_MODEL` (additive ring).
See `docs/guest_assets.md`.

## Feature closure (2026-11-03)

- `renderer/gfx/features.c` owns the scene-feature predicates
  (`HasEffects`, `NeedHdr`, `HasSsgi`). The pass table, the composite-target
  lifecycle and the final blit share them instead of hand-maintained lists.
- First fix: DOF/motion-blur/particle-only scenes now allocate and update
  `bloom.scene`; the old update list missed them, so their writes could land
  in the canvas after present or read an unloaded target.
- `GX_GFX_FEATURE_TRACE=1` logs the one-shot closure and whether the HDR and
  composite targets are loaded, so allocation drift is observable.
- Water, labels, transparent and decals now resolve the scene target through
  `Feature_SceneColorTarget` (HDR when allocated, LDR otherwise).
- Disabling SSAO neutral-clears `ssao.map`/`blurMap`, drops the GTAO/SSGI
  final-texture aliases and resets both temporal histories.
- SSAO is no longer toggled by the shader-quality cascade; a PBR-driven
  quality raise cannot silently re-enable it. Lazy HDR allocation can
  therefore trust `NeedHdr`. The display transform still depends on bloom.

## Threading model (2026-11-03)

- The **main thread** owns the GL context, scene graph, UI/input, audio
  control and every GPU upload. Workers never call GL.
- CPU workers are used for the large-OBJ parser (shared two-worker pool, up
  to four slots, >5 MB `.obj`), while the audio callback (miniaudio) and ODE's
  optional threaded solver keep their own threads. Shader preprocessing is
  synchronous on the main thread (the old async preprocess farm was removed);
  the shader cache covers warm starts.
- `gX_ThreadPool_Wait` blocks until every submitted item has completed
  (queued + in-flight), not merely until the queue is empty. `Destroy` is
  defined after `Wait`; queued-but-unstarted work is discarded. `Submit`
  returns `-2` when the bounded queue is full, so callers apply backpressure.
- `gX_AtomicI32_*` provides acquire/release loads and add-fetch counters for
  cross-thread completion flags (GCC/Clang `__atomic`; MinGW uses the same).
- The planned master-worker lane adds asset decode and measured CPU culling/
  terrain jobs; render passes and GL state stay on the master thread.

## Runtime complexity policy (2026-11-13)

gX is a runtime engine, not a solver. Default frame-path work should be
polynomial-time and bounded, with explicit input, time and memory caps. Do not
put exponential search, unbounded backtracking or a general-purpose solver in
the default build, scene load or frame loop.

- **P-first:** prefer graph/path/geometry algorithms with known polynomial
  bounds — ordering, topological sorts, shortest paths on a single agent,
  sweep/interval structures, bounded spatial hashing and grids.
- **Quadratic or below by default:** aim for O(n), O(n log n) or O(n^2) with a
  bounded n. A cubic-or-worse frame-path algorithm needs a fixed-size
  justification (3x3/4x4 algebra is constant) or must move offline/precompute.
  O(n^2) memory is usually worse than O(n^2) time; prefer linear or
  log-linear storage.
- **All-pairs is a cap, not a growth strategy:** O(n^2) pair checks are
  acceptable only with a hard body/light/element cap; beyond it use spatial
  grids, hashing or a BVH. Do not let the cap quietly become the design.
- **Bounded heuristics for hard problems:** NP-hard optimization is allowed
  only as a bounded approximation with a measured budget — greedy/list
  scheduling, shelf or skyline atlas packing, greedy set cover, SAH-style BVH
  construction, priority culling. Record what is approximated and its cost.
- **Exact solving is offline and tiny:** ILP/CP-SAT/brute force belong in
  tooling and tests on small instances, never in the shipped frame path. The
  NP-hard lab and candidate queue are in
  [`speculative_ideas.md`](speculative_ideas.md) under S-010.
- **State the problem precisely:** single-agent shortest path is in P;
  multi-agent collision-free planning, coverage and general set cover are not.
  A polynomial special case is preferred over a general hard problem.
- **Budget failure means reduce scope:** if a heuristic cannot meet its frame
  or memory budget, lower the instance size, precompute offline or cut the
  feature. Do not hide the cost in unbounded search or an unmeasured fallback.
- **Determinism:** bounded work should also be repeatable; record caps and
  seeds so gates and captures stay stable.

### Performance is the contract

Complexity is a proxy; wall-clock frame time is the actual requirement.

- Hot paths need a measured budget — frame/p95 time, allocation count, draw
  calls or VRAM — and a before/after number. Use the existing tools:
  `GX_PROFILER` / `make profile-check`, `make gfx-benchmark` and
  `make gfx-benchmark-check`, the feature matrix and the lean caps. See
  [`minimum_hardware.md`](minimum_hardware.md),
  [`rendering_backlog.md`](rendering_backlog.md) and
  [`lean_gate.md`](lean_gate.md).
- Avoid per-frame heap allocation, redundant GL state changes and repeated
  full scans in hot loops where a bounded structure or cached result fits.
- A change that regresses a measured budget needs an explicit trade or is
  reverted; “it is only 1 ms” is still a budget decision.
- Speed does not excuse unbounded memory, nondeterminism or an unmeasured
  fallback.

This is a design policy, not a claim that every current implementation is
optimal or that every problem has a fast exact algorithm.

## Verification

The GFX feature matrix supports focused validation with
`python3 scripts/gfx_feature_matrix.py <output-dir> --only bloom,ssao,occlusion
--jobs 1` (or `--quick`). Output explains the measured changed-pixel fraction,
threshold band, brightness/collapse guards and evidence strength. Every
positive row now requires a visible changed-fraction floor; only the negative
controls permit zero change. The cookie row uses a visible window-gobo map,
and a zero-response fixture fails every positive row by construction.
`report.json` retains metrics, first-attempt failures before retries, capture
paths and OFF/ON reproduction commands. Comparisons use raw pixels; annotated
copies are separate. Per-row evaluation time is not GPU frame time. GPU-free
report-rule tests run before `make gfx-check` and `make gfx-quick`.

<!-- //ai: capability-specific matrix captures keep fixture controls symmetric. -->
Selecting `particles` rebuilds the isolated Debug GL 4.3 profile under
`build/compute/`; both particle arms use it, while all other rows retain
`build/gX_build`. Controls, retries, cache fingerprints and reproduction commands
retain the selected executable. The probe row enables its cubemap consumer in
both arms, so only the probe-grid switch differs. These fixture corrections do
not change renderer defaults or admission thresholds. The mirror row uses the
dedicated mirror check's camera in both arms, rather than the generic stage view.
The probe matrix row uses the minimal scene's native six-unit camera distance;
the generic ten-unit showcase distance left its sky-only response below the
unchanged positive floor. This row measures visible response, not geometric
correctness; it does not waive the dedicated probe contract's independent
parity requirements.

<!-- //ai: portable owned probe diagnostics do not import the parked renderer experiment. -->
The fixed-box local-reflection diagnostic tooling is self-contained:

```sh
python3 scripts/test_probe_local_regression.py
```

The generated GLB has six outward-facing, distinctly emissive materials. Its
temporary preset explicitly sets the reflection plane, camera, exposure and
scene emission defaults; no user preset or asset provenance is rewritten.
GPU-free tests cover the fixed-box mip-footprint witness and the chosen policy:
omit unreliable local contributions without replacing other material lighting.
The evidence reader checks routing-code consistency and exact omission controls,
not geometric visibility, face-color fidelity or renderer admission.

Optional `capture <new-output> --binary <experimental-binary> --shader-root
<experimental-roots>` requires an explicitly supplied diagnostic build and the
private `lod0`, `lod1`, `lod2`, `lod4`, and `omit_control` resource profiles. It
rejects missing contracts instead of silently skipping coverage. Those renderer
profiles remain parked, uncommitted experiments; this tooling does not install
them or certify the normal engine. The retained disabled-path HDR outlier remains
unresolved, and arbitrary-geometry coverage, real roughness and motion remain
production-integration blockers.

- `./build/gX_build --test-only` — unit tests, exits before any window
- All scenes verified crash-free; zero own-code warnings at -Wall -Wextra
- `make check-fast` — public source-beta CI: incremental build, unit tests,
  static-lightmap contract, and 24-frame title/GFX/Y2K smokes
- The full release matrix (GPU audits, Lean proofs, certification) is
  maintained outside this source beta; use `make check-fast` here.
- `make gfx-check-fast`, `make temporal-check-fast`, and
  `make reflect-check-fast` — reduced GPU suites for the feature matrix,
  temporal bursts, and SSR/planar reflections; full targets remain the gates
- `make reflect-check` — GPU-gated reflection fidelity suite (DISPLAY=:1);
  planar 98.4–99.3%, SSR ~74% (see `docs/reflection_pack.md`)
- GUI: `scripts/gui_audit.py` template classification; all registered
  builders render (see `docs/gui_audit.md`)

## Subsystem and module status (2026-11-01)

gX remains **Alpha** as a product. The supported GL33 GFX renderer is assessed
as **Beta**; opt-in or incomplete paths keep their individual status. The tier
ships in the source metadata: each renderer module's `description` ends with
`(alpha)`/`(beta)`, and the EXTRAS cards take their description from
`gX_Tools_Status` in `src/tools/tools.c`. Alpha = usable preview without a
shipped gate; beta = gate-covered and documented.

Renderer modules — **beta**: gfx, sky, water, background, postprocess,
textwriter. **Alpha**: analog, clock, fractal, teletext, wireframe, generate,
raymarch, oscillo, lissajous, spectrum, ws2d.

Editor surfaces — **beta**: GFX editor only (the reference surface).
**Alpha/WIP** (usable previews, gates noted in the docs): the global editor
host (toolbar pilot), GUI editor, theme editor, wavetable editor, textwriter
editor, physics editor, background editor, sky editor, ThemeLab, MaterialsLab,
ShaderLab, camera rail editor, script debugger. The long-form inventory, gate coverage and per-editor gaps
are in `docs/editors.md`.

| Subsystem | State |
|---|---|
| gfx | Beta — supported GL33 renderer; 29-row feature matrix passes with visible-effect floors, reflection suite grades A- overall; experimental paths remain individually gated |
| gui | Complete — canvas compositing, builder widgets, CSS tokens + live reload, audit-verified templates, real title screen |
| physics | Props + ragdoll + EM sandbox — shared ODE world (optional `GX_USE_ODE`), fixed 60 Hz stepping with interpolated rendering, per-step field forces (softened Coulomb + Lorentz) and magnetic dipoles (pair forces, mutual torques, uniform-field alignment, `N` toggle), per-element material looks (polished metals, graphite carbon), rolling/spin resistance, induced magnetism (iron pulls toward the magnets) and sleeping/auto-disable (wake on interaction or impact), impact heating + radiative cooling + contact conduction with blackbody glow and latent-heat melting, spring drag-and-throw, joint rigs (hanging chain, hinged door, motorized windmill, slider rail — all draggable), a walled pool (Archimedean buoyancy + drag via the fluid applier, floating buoys, splash FX, `P` toggle), a conveyor belt surface (`GX_PHYSICS_BELT=0` skips), kinematic movers (a driven lift platform + a spinning turntable with payload crates; `GX_PHYSICS_MOVERS=0` skips), mass properties (a top-heavy/centered/bottom-heavy balance-post trio tests center-of-mass effects; `B` re-poses the tilt test, `GX_PHYSICS_COM=0` skips), a hinge2 rover (cylinder-wheel cart, front-wheel drive, suspension lean; `GX_PHYSICS_ROVER=0` skips) and standing cylinder props in the grid mix, a feedback-welded break target whose weld snaps above the break force and is rewelded by `R` (`GX_PHYSICS_TARGET=0` skips), a piston pump (a vertical plunger on a piston joint, square-wave driven into its limits with a rotation motor; `GX_PHYSICS_PUMP=0` skips), a heightfield terrain mound just past the belt's far end that items tumble down (`GX_PHYSICS_MOUND=0` skips), a runtime tuning surface with JSON presets (physics editor; `--physics-editor`/EXTRAS, plus a modal free camera walking the sandbox), scene-exit park, status HUD, collider/contact debug overlay, ragdoll bones in the EM/thermal/fluid appliers (charged splay, heated glow, water float) with AMotor swing bands and self-collision (an arm no longer passes through the torso), contact-event spark bursts through the GPU particle emitters and scorch decals on heavy impacts (compute build for sparks), element densities/thermal data/contact fits, wrapper/ragdoll/EM/dipole/element/surface/sleep/amotor/piston/heightfield/thermal/fluid/determinism/lifecycle/rig/destroy-cleanup tests |
| audio | Healthy — procedural PCM registry (16 tones), device init verified, GUI call sites wired; mixer buses (master/music/effects/synth — persisted, live pause-menu sliders), procedural 8-voice synth + per-voice portamento/sequencer, **OPX 8-operator phase-modulation FM engine** and **PAD layered ensemble engine** (own algorithm/operator and layer/chorus models, offline-testable cores), procedural one-shot SFX pool (sandbox impacts + toolgun), damping reverb core, shared TPT/ZDF filter core + tail-kept master filter across the mix, global `GX_AUDIO_MUTE`/`--disableAudio` quiet paths |
| input/camera | Per-scene binds for implemented scenes only |
| scene | TITLE + 16 renderer demo scenes; gameX game-scene entries pruned from CLI/respond (inert porting reserves remain) |

## v0.3-M1 — AgX tonemap + auto-exposure (2026-08-25)

- Composite pass (`gfx_bloom_composite.frag`) precedence: AgX > ACES > Reinhard.
  AgX = Sobotka display transform (inset mat, log2 EV[-12.47,4.03], polynomial
  sigmoid, outset + 2.2 EOTF). `ENABLE_AGX_TONEMAP` state flag (default on).
- Auto-exposure: `gfx_luminance.frag` downsamples hdrScene to a 64x64 RGBA16F
  target (`bloom.luminanceRT`, lazy-loaded via RenderTexture_Update). Every
  10th frame the CPU readbacks ~16KB, log-averages luminance, applies Krawczyk
  key (1.03 - 2/(2+log2(L+1))), asymmetric adaptation (fast toward bright
  1.5/s, slow toward dark 0.6/s), clamps to [0.25, 6.0] and drives the same
  `exposure` uniform so bloom extract sees consistent brightness.
- Overrides: `GX_NO_AE=1` disables adaptation; `GX_NO_AGX=1` falls back to ACES.
- Roadmap next (shipped through v0.3.2): GTAO -> volumetric fog ->
  SSGI (denoise + multibounce, default-on) -> Forward+ tile clustering
  (CPU-culled). Next frontier: lift the Forward+ 32-slot uniform ceiling
  (compute shader + SSBO light culling; the bitmask-grid interface
  survives the upgrade).

## v0.3-M2 — GTAO (2026-08-25)

- gfx_gtao.frag: horizon-scan GTAO (Jimenez 2016) ported from the three.js
  GTAOPass reference. 4 dirs x 6 steps, cos-horizon tracking, normal-clamped
  analytic slice integral, IGN jitter, thickness heuristic, distance-falloff.
- ssao.useGTAO default on; renders into ssao.map (half-res), shares the
  existing edge-aware blur + model AO sampling. Legacy kernel SSAO fallback.
- Fixed pre-existing gfx_ssr_temporal.frag compile failure (prevClip scope,
  broken since v0.1.38): hoisted declaration; velocity path seeds w=1.
- Next: M3 volumetric fog/god rays (froxels + HG phase + shadow-map sampling).

## v0.3-M3 — Volumetric fog / god rays (2026-08-25)

- gfx_fog_vol_gen.frag: HG forward-scatter (hgAsymmetry 0.7) + per-step
  CSM shadow occlusion (hard 1-tap; PCF too expensive at 32 steps) +
  IGN-dithered march + sky-distance march + eye-anchored height band +
  transmittance floor 0.12.
- Gen bakes full delta (scene*T + inscatter - scene); blend = additive
  gX_Texture_Draw (godray.c recipe). rlgl DrawScreenQuad AND raw-GL quads
  both silently no-op when compositing into bloom.scene — only the
  Texture_Draw batch path works there (second confirmation of the class).
- Pipeline: FogVolume must run AFTER SSR composite (SSR rewrites
  bloom.scene from pre-fog content).
- Tunables live in schema: density / heightFalloff / scattering /
  hgAsymmetry / color / matchSceneFog. Defaults: density 0.003, white
  in-scatter tint, and `matchSceneFog` on so the scene fog color drives the
  volume tint. The editor also exposes a "visible defaults" action.

## v0.3-M4 — SSGI: OPEN (2026-08-25)

Horizon-gather SSGI shader written and validated for syntax (reference copy
preserved in the author's notes); integration attempted via ssao-module
piggyback (RT + shader + draw + composite hook).

BLOCKER discovered while validating: enabling the fog gen shader's sky-march
version (farPlane fix) blacks the entire frame even though genMap probes read
zero-delta — i.e., the black frame is NOT the fog blend. Something about the
compiled-with-array-sampler state interacts with later passes. Two confirmed
engine quirks complicate debugging: (1) rlgl DrawScreenQuad AND raw-GL quads
can silently no-op into bloom.scene (only Texture_Draw batch path works);
(2) screenshot capture methods disagree (in-app vs window grab), which
poisoned early bisection.

NEXT SESSION: RenderDoc capture of a good frame vs black frame (in-app trigger
hook exists, GX_RD_CAPTURE=1); diff the two to find the pass that goes dark.
Then re-land SSGI (shader is ready; wiring pattern proven by GTAO).

## v0.3-M4 blocker — REPRO RECIPE (2026-08-25)

> **RESOLVED** — root cause was UTF-8 in GLSL comments corrupting NVIDIA's
> compiler tokenizer, not anything in this recipe's suspect list. Section
> kept as a record of the headless bisection method; see the v0.3.1
> ROOT CAUSE section below. (RenderDoc captures turned out unnecessary:
> injection suppressed the bug.)

Minimal repro of the black-frame bug, for the RenderDoc session:
1. HEAD renders showcase fine (~143k unique colors).
2. In resources/shader/gfx_fog_vol_gen.frag change ONE token:
   `cameraFar` -> `farPlane` in the sky-march viewDist line.
   This makes the M3-committed fog gen shader COMPILE (it previously
   failed on the undefined identifier and was skipped harmlessly).
3. Rebuild, run --showcase: entire frame black except GUI/HUD.

Established by bisection (all headless, in-app screenshots):
- NOT the delta math: additive-only output (inscatter, cannot be negative)
  still blacks; shader that only writes red/green sentinels still blacks.
- NOT the blend content: tint=gX_BLACK (adds literal zero) still blacks.
- NOT pipeline position: before-godray / after-SSR both black.
- NOT CSM bindings: skipping all shadow uniform/texture binds still blacks.
- NOT march length / sky branch: old sky early-out still blacks.
- genMap float readback: RGB = NaN on ~75% of geometry pixels (alpha=1),
  sky pixels zero — but sentinel-shader proof shows NaN is irrelevant:
  even a no-op main() blacks the frame.

CONCLUSION: executing ANYTHING from FogVolume_Draw while the fog gen
shader is in compiled state corrupts the frame through a GL-state
interaction invisible to glGetError probes — most likely the array-
sampler shader object's program bind via gX_Drawing_BeginShader
poisoning rlgl batch state for every later pass. RenderDoc capture
(GX_RD_CAPTURE=1 + renderdoccmd capture) of HEAD vs one-token-diff
will show exactly which draw goes dark first.

SSGI reference implementation preserved:
resources/shader/gfx_ssgi.frag.experimental (horizon-based gather,
validated syntax; wiring pattern identical to GTAO's).

## v0.3-M4 — SSGI LANDED (experimental) (2026-08-25)

- gfx_ssgi.frag: horizon-gather indirect diffuse (cosine-weighted radiance
  from hdrScene), 4x6 slices/steps, IGN jitter. Half-res RGBA16F ssgiMap.
- Composite adds ssgi * gain (dedicated sampler unit 10 in composite).
- GX_SSGI=1 opt-in. Known v1 limitation: banding/grain at low sample counts —
  Phase 2 = color-aware edge blur + temporal accumulation via velocity
  buffer; Phase 3 = off-screen ambient fallback + previous-frame reinjection.
- Landed independently of the fog black-frame blocker (fog gated off).

## v0.3-M4 Phase2 — SSGI denoise (2026-08-25)

- Chain: gather(ssgiMap) -> depth-guided 7x7 blur(ssgiBlurMap) ->
  temporal accum(ssgiAccumA/B parity) -> composite reads latest.
- Temporal: velocity reproject (unit 21) + neighborhood variance clip +
  adaptive blend (0.8 static, 0.35 fast motion via velocity length).
- Vision: banding eliminated; bounce preserved; mild uniform over-blur.
- Remaining M4 polish queue: off-screen ambient fallback, previous-frame
  GI reinjection (cheap multibounce), intensity tuning pass.

## v0.3 release gate status

SHIPPED: AgX+autoexposure, GTAO, SSGI(+denoise, opt-in GX_SSGI=1).
GATED: volume fog (black-frame blocker; RenderDoc captures were used during
investigation and kept outside the repository).

## v0.3.1 — fog blocker RESOLVED (2026-08-26) — ROOT CAUSE: UTF-8 in GLSL comments

TRUE ROOT CAUSE (found via glslangValidator cross-check on dumped runtime
sources): **NVIDIA's GLSL compiler mis-handles multi-byte UTF-8 characters
inside // comments** — corrupting its tokenizer/line-tracking, producing
phantom syntax errors ("unexpected {" at wrong lines, "undefined variable")
that move around depending on which shaders contained how much non-ASCII.
Today's v0.3.x commits had introduced `//opencode:` comments using UTF-8
arrows (->) across 59 shader files; each additional non-ASCII comment
shifted which programs failed — creating the illusion of a moving,
content-independent curse. RenderDoc injection also suppressed it (its
source handling normalizes encoding), which is why captures looked fine.

FIXES SHIPPED:
1. ASCII sweep of all 59 shader files (non-ASCII -> ASCII equivalents).
2. Missing closing brace in gfx_fog_vol_gen.frag main() (introduced during
   M3.1 edits; masked by the above).
3. Volume fog occlusion simplified to cascade-0 only (plain sampler2D,
   shadowRange=40m cutoff) — the array-sampler removal ALSO turned out
   necessary-or-benign; kept for robustness. Full CSM returns with froxels.
4. NaN scrub + LDR genMap storage retained.
5. Thread-safety hotfix: gX_Shader_Compiler_Preprocess wrapped in a mutex —
   the async preprocess worker pool races the main thread on shared statics
   (searchPaths/marker); a related latent hazard spotted during the hunt.

SSGI Phase 3 also landed: ambient floor fallback + previous-frame GI
reinjection (cheap multibounce); SSGI ships DEFAULT-ON.

## v0.3.2 — Forward+ tile clustering (CPU-culled, GL330-safe) (2026-08-26)

- clusters.c: per-frame CPU build; 16px tiles; grid[tile] = BITMASK of
  upload-slot indices (R32UI texture, unit-free texelFetch). Radius from
  attenuation quadratic root, clamp by shadowRange. Projection: manual
  row-major world->view(LookAt)->clip two-stage (sun_uv.c pattern).
- standard.frag: applyLight(i,...) shared function; clustered path walks
  findLSB over mask — ascending order = legacy parity (verified 0 px diff).
- ENABLE_CLUSTERED default on; GX_NO_CLUSTER=1 legacy fallback.
- Ceiling: 32 slots (= uniform cap). Lifting needs compute/SSBO upgrade;
  the bitmask-grid interface survives it.
- Debug lesson: GX_HEAP_FOREACH bodies + inserted probes — verify build
  SUCCEEDS before trusting run output (a broken probe silently left a
  stale binary and sent bisection in circles for several runs).
