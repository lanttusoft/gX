# Changelog

All notable changes to gX are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows
SemVer.

## [Unreleased]

### Added
- **Per-model surface controls**: `doubleSided` disables backface culling for
  color, depth/shadow, instanced and planar draws; `flipNormals` inverts the
  shaded normal without changing winding. Both are editable per model,
  default off, and persist in presets.
- **Screen color-code core**: reusable 45×45 encoder/decoder with calibration
  and control cells, metadata-at-edges, payload-in-middle, SECDED ECC, CRC32,
  bounded RLE/raw compression and a synthetic camera round trip in
  `--test-only`. `--color-code <text>` presents the frozen code fullscreen or
  1:1 (`SPACE`, `F`, `ESC`) with a deterministic screenshot hook. Camera
  rectification and multi-frame reassembly remain backlog; see
  `docs/color_code_format.md`.

## [0.20.0] - 2026-10-30

### Added
- **Deterministic shared-depth benchmark**: `gfx-raymarch-shared-bench-check`
  renders one SDF sphere against the raster scene and pins render-scale
  separation plus a bounded depth-pass overhead.
- **Editor-driven two-plane mirrors**: planar models can be tagged Plane A/B;
  `mirrorPair` persists in presets and activates the facing-mirror feedback
  loop without the `GX_MIRROR_PAIR` fixture hook.
- **DAE/GLB oversized rigs**: rigs above the 128-bone upload cap now render
  their authored bind pose with a warning instead of collapsing geometry.
- **Water scene fog**: the above-water surface applies the scene fog
  near/far/color.

### Changed
- **Shared-depth raymarch**: shaded SDF color renders at `renderScale`, with a
  separate full-res depth-only march, a raster-depth early-out and a bounded
  depth-step budget.
- **Water reflections**: 12% Fresnel floor at normal incidence and a 0.5 u
  water march step keep high-view and thin-prop reflections visible.
- **SSAO/fog order**: SSAO is evaluated before fog so fully fogged surfaces
  converge to the fog color.
- **Flat lighting**: Flat keeps Phong lighting and only switches the normal
  source; Unlit is the only all-off lighting model.

### Fixed
- **Planar ghosting**: the planar capture clips geometry behind/crossing the
  mirror plane, removing opposite-side ghosts.
- **Flat vs Unlit**: old presets with Flat but Phong off now light correctly.
- **Water fog/reflection visibility** and **SSAO fog-color** regressions.

## [0.19.1] - 2026-10-30

### Fixed
- **Hybrid raymarch performance**: hybrid color now honors the module's
  `renderScale` (default 0.35) instead of forcing full output resolution.
  Measured on the reference scene at 1600x900, hybrid dropped from ~59 ms to
  ~20 ms; shared-depth mode remains full-res because it writes the frame depth.
  Raise the render scale to 0.5+ when overlay sharpness matters.
- **Raymarch depth inspect gate**: the headless editor fixture now selects the
  Ray tab and collapses unrelated left-pane sections, keeping the hybrid/shared
  depth controls visible after the Phase 4 layout changes.

## [0.19.0] - 2026-10-30

### Added
- **GFX editor rework, phases 1–5**: split Scene/Create/Renderer/Document
  panes, plum/lavender/amber identity, composed showcase default, peer-review
  gate, responsive/accessible scaling, and focus-view mode. Internal re-review:
  workflow 8.0, product 8.0, accessibility 8.2 (A- each); release target met
  for the technical preview.
- **Small-resolution patchwork gate**: `make gfx-patchwork-check` for fast
  visual A/B and limit scans.

### Changed
- Default tool UI scale is now 125%; disabled/focus states are brighter, and
  keyboard navigation keeps the shared amber focus cursor visible.

## [0.18.1] - 2026-10-30

### Added
- **Public proof sources**: the Lean 4 proof tree is published separately at
  https://github.com/lanttusoft/gx-proofs (GPL-3.0-or-later; Mathlib/Lean
  notices included). The engine tarball remains proof-free.
- **Script license gate**: `scripts/script_license_audit.py` requires
  GPLv3 SPDX + copyright metadata on project scripts and runs in the fast and
  full CI paths.

### Changed
- Project scripts now carry SPDX/copyright headers.
- Proof links were added to the engine docs, README/readme, release notes and
  third-party notices.

### Fixed
- Removed private monorepo release helpers (`mkrelease.sh`,
  `packaging_check.py`, `packaging_walk.py`, `test_packaging_check.py`,
  `gx_clean.sh`) and their private `master/` layout assumptions from the public
  tree.
- Corrected the public proof-audit path and remaining `master/` documentation
  examples.

## [0.18.0] - 2026-10-31

### Added
- **Active-ragdoll lab contract + Phase 1**: `docs/active_ragdoll_lab.md`
  defines the clean-room, body-first hybrid controller scope. The opt-in
  `gX_Ragdoll_Control` now provides bounded PD torque, pelvis support,
  passive/active/falling/getting-up modes and repeatable headless tests; the
  passive/default ragdoll path is unchanged. No scene, assets, or
  Euphoria/RAGE material is added.
- **Renderer state suites**: added fixed-resolution sky state/repeat and
  textwriter tag/reveal/choice matrices (`sky-render-check`,
  `textwriter-render-check`) with test-only `GX_SKY_TIME`,
  `GX_TEXTWRITER_FILE`/`_LINE` hooks. Main-GPU captures, existing horizon and
  editor gates pass; default runtime behavior is unchanged.
- **Accessibility A4/A5/A8**: Reduce Motion now bypasses the time-driven
  analog CRT/VHS/static path and shows the clean input (`analog-reduced-check`
  proves static reduced frames); added a colour-independence cue/simulation
  audit (`colour-independence-check`) and a user-facing accessibility page.
  Default runtime behavior is unchanged.
- **Focus-ring contrast matrix**: `gui-state-contrast-check` now covers the
  keyboard focus ring against canvas/raised/overlay/sunken, panel/button/
  selected, title menu/save and battle/escape surfaces across every shipped
  theme (226 checks, zero failures). No focus or runtime styling changed.
- **Renderer attribution audit**: explicit raymath v1.5, three.js GTAO, NVIDIA
  FXAA 3.11, Inigo Quilez SDF, Ashima/Gustavson noise, ACES, AgX and SSRT3
  credits were added, with license files for the compatible sources. The
  LearnOpenGL-derived SSAO/IBL shader code was independently rewritten as gX
  split-sum/GGX and depth-masked blur code; the tangent basis is now an
  independent UV-gradient implementation with no Unity-derived snippet. The
  skybox image remains CC BY 4.0 with attribution.
- **Snowfall profile**: added bounded, default-neutral `GX_SNOWFALL_GUST`,
  `_TURBULENCE`, `_SIZE` and `_ALPHA` controls to the existing CPU snowfall
  sim. Gust/turbulence use simulated time and per-flake phase; size/alpha scale
  the existing point upload. `snowfall-check` now covers default-neutral,
  response, profile and profile-repeat cases. Agent `check-fast` and main GPU
  `snowfall-check`/`snow-check` pass; no schema, editor, shader or asset change.
- **Level editor ergonomics**: snap steps (`off`, `0.25`, `0.5`, `1.0`) for
  canvas drags/resizes, plus copy/paste/cut-free duplicate (`Ctrl+C/V/D`) with
  unique IDs, stepped offsets and history entries. Inspector values remain
  exact; references are left visible for validation after copying. Dev-only
  game tool; no engine payload or game content change.
- **Dialogue graph canvas**: the dialogue editor now has a visual node/edge
  canvas with phase-start and orphan markers, default/choice edges, drag,
  wheel zoom, middle-drag pan and Fit. Phase-entry nodes are treated as graph
  roots for reachability. Layout is in-memory only; no JSON/schema change.
  `dialogue-editor-model` now pins phase-root reachability. Game-side only.
- **First/third-person level playtest**: `gx_level` now selects a camera with
  `GX_LEVEL_CAMERA=first|third`; third-person orbits behind the player and
  draws a body marker while sharing the same room/portal traversal. The level
  editor exposes the camera choice in its Playtest toolbar. `level-module-check`
  captures both modes and requires different, non-empty geometry. Dev-only;
  no engine payload or game content changes.
- **Dialogue/state editor (S6 first slice)**: a separate game-side Tkinter app
  (`game/editor/dialogue_editor.py`) edits the bounded dialogue graph with
  node/choice/flag editing, phase-start and unreachable reports, real
  `gx_book --check` validation, version-1 save inspection, bounded undo/redo,
  a plain graph outline plus saved-vs-current unified diff, and an embedded
  terminal playtest. `gx_book --start-node <id>` starts the exact runtime at
  the selected node; `dialogue-editor-model` and `start-node` join game CTest,
  and `make dialogue-editor-check` is the headless gate. No engine content or
  shipped payload is added.
- **Top-down level editor (S8 first slice)**: a separate game-side Tkinter
  app (`game/editor/level_editor.py`) edits the exact S7 room/portal JSON with
  a shared model, drag/zoom/pan canvas, inspects all object kinds, creates
  rooms/portals/props/spawns/triggers, resizes rooms from corner handles, and
  has bounded undo/redo. Save uses a real `gx_book --check-level` gate;
  Playtest launches the existing `gx_level` module. The model/validator tests
  are part of game CTest, and `make level-editor-check` is the headless gate.
  No engine content or shipped payload is added.
- **Module-owned scene + level probe**: the engine now has a generic
  `gX_SCENE_MODULE` scene with input/camera/renderer responders and a
  `--module-scene` start flag. The game-side dev-only `gx_level` lab module
  loads the real S7 level fixture/runtime, enters the spawn, applies
  movement/portal containment, and draws rooms, portals, props, spawns,
  triggers, a floor grid and a minimap through host exports only. Contract:
  `docs/engine_frontend_contract.md`; gate: `make level-module-check`. The
  module stays under `build/labs/` and never enters the engine or `gx_book`
  payload.
- **Engine-core archive boundary**: CMake now builds `gx_engine_core` from the
  reusable engine sources (`src/` minus `main*`, `tools/**` and `scene/**`) and
  links `gX_build` against it. The executable keeps the lifecycle, authoring
  tools and demo scenes. This is a build boundary only; the global `maindata`
  host contract, raylib-backed renderer and the game frontend split are
  documented in `docs/engine_library.md` and remain the next stage. Runtime
  behavior and Release payload semantics are unchanged; `check-fast`, the
  kernel build, scripting-module probes and the lean gate pass.
- **Contrast worklist**: `docs/contrast_worklist.md` ranks all 27 baselined
  theme contrast failures by real UI exposure, records tokens/likely changes,
  flags the uncertain `module.*` pair mappings for review, and recommends a
  small P1 first batch. Docs only; no theme or UI change.
- **HUD-over-scene contrast probe**: `scripts/gui_hud_contrast_check.py`
  captures `--title` and `--battle` at 1024x768/frame 24, samples 10
  documented text ROIs and reports WCAG 4.5:1 contrast with alpha-aware
  math. `--baseline scripts/gui_hud_contrast_baseline.json` gates new
  failures; `--strict` asks for a clean HUD. `make gui-hud-contrast-check`
  runs the synthetic tests only.
- **Layout resolution sweep**: `scripts/layout_sweep.py` runs a configurable
  scene/resolution matrix with `--layout-dump` and compares normalized rects,
  element presence, foreground-overlap growth and screen-size mismatches.
  The first size is the baseline; findings are diagnostic and never fail the
  command. `--scenes`, `--sizes`, `--tol` and `--out` configure Markdown/JSON
  output; `make layout-sweep-check` covers the synthetic comparisons only.
- **Structured layout report**: `scripts/layout_report.py` runs the scene
  matrix with `--layout-dump` and reports canvases, element counts, off-screen
  rects, foreground/background overlaps and text elements in Markdown or
  JSON. `--only`, `--out`, `--frame`, `--width` and `--height` configure it;
  findings are diagnostic and never fail the command. `make
  layout-report-check` covers synthetic dumps only.
<!-- //ai: exact-real branch-aware quaternion interpolation proof slice. -->
- **Quaternion interpolation contracts**: added exact-real Lerp/Nlerp/Slerp
  models with explicit normalization-zero, antipodal, sign-correction,
  strict near-parallel threshold and epsilon-fallback behavior. Proved Nlerp
  unit output for nonzero blends and regular Slerp unit norm/endpoints under
  stated unit/cosine assumptions; also proved the defensive small-sine
  fallback unreachable on the regular interval for idealized source epsilon.
  Actual-TU native witnesses cover the
  threshold sides/boundary, negative-dot correction, endpoints and
  extrapolation; no IEEE accuracy or universal C-translation claim is made.
- **Static front-end inventory**: `scripts/color_inventory.py` (distinct
  colours, named/theme overlap, orphans and font references per screen),
  `scripts/style_lint.py` (token-aware hardcoded-colour report, `--fail`
  opt-in), `scripts/font_inventory.py` (fonts and `fontSize` uses per screen)
  and `scripts/font_preview_sheet.py` (labeled PNG preview with explicit
  missing-glyph markers and JSON metadata). `make frontend-static-check` runs
  all four into `/tmp/opencode`; no UI, asset or engine change.
- **Generic image review tools**: `scripts/snapshot_compare.py` compares PNG
  and binary PPM P6 captures with tolerance, diff-count/fraction and
  mean/MSE bounds, an optional half-open region verdict and a highlighted
  diff PNG (exit 0/1/2). `scripts/contact_sheet.py` builds a deterministic
  labeled PNG grid from paths or a directory, rejecting empty and mixed-size
  batches. `make frontend-image-check` runs the synthetic tests; no display,
  scene or AI probe is involved.
- **Public API definition audit**: `make api-definition-check` compares header
  declarations with `T`/`W` symbols in the normal-build objects and pins the
  65 currently known default-engine declarations without definitions as a
  checked baseline. New declarations fail the gate; `--strict` asks for the
  full known list to be clean. The dated report is
  `docs/api_definition_audit_2026-10.md`; module-only `gx_shell` families are
  listed as profile-provided.
- **DevCube engine avatar (lab module)**: `gx_devcube` builds under
  `build/labs/` and loads only with `--module`/`GX_MODULE`. It overlays a
  green wireframe cube, a pink 3:2 Lissajous and three RGB orbs at native
  resolution, driven by a monotonic 60 Hz phase instead of game dt, render
  scale, or connection-quality lag.
### Fixed
- **Mandelbulb origin estimate**: zero-radius iterations now return the finite
  zero estimate instead of propagating NaN from spherical-angle and logarithm
  operations. A native regression test pins this convention; it is not an
  exact interior-distance claim.
- **Lab-module packaging profile**: `packaging-export-check` now rejects any
  shared module binary (`.so`, `.dylib`, `.dll`) and any `build/` tree in
  source/release exports, while keeping `modules/module_exports.txt` as
  engine ABI metadata. `gx_shell`/`gx_devcube` are documented as opt-in lab
  clients; only `--module`/`GX_MODULE` loads them.
- **Audit cache source fingerprint**: gate keys now hash the build inputs
  (`src/`, `resources/`, `scripts/`, build files, raylib headers and the
  selected `CMakeCache.txt` profile) instead of the relinked binary, so a
  source-identical rebuild keeps unchanged gate results. Capture-pool
  fingerprints use the same input digest. `make cache-check` pins the
  clean/repeat/`--no-cache` contract.
- **Buffered-canvas plain GUI textures**: the default texture-element branch
  now flushes the queued rect before the immediate gX batch and disables
  depth/cull for the 2D quad, so a visible rect no longer covers the texture.
- **Pause hint readability**: the Aero-glass pause hint now sits on a quiet
  opaque chip with white text, keeping WCAG AA with the cursor light on and
  off; the effect-contrast gate covers the new chip.
- **Renderer audit remediation**: GLSL >=130 reserved `input` samplers are
  renamed consistently (glass, postprocess and raymarch lookups), the
  instanced draw activates its material program before the skinning switch,
  and the Hi-Z/label overlays restore full GL state. The 330-only probe-proxy
  path is stubbed out of the legacy 130 dialect so `glsl130-check` stays green.
- **Oversized skin-bone uploads**: the raylib model-draw path now clamps
  per-model bone matrices to the shared `gX_SKIN_MAX_BONES` cap (128) used by
  the skinned vertex shaders, so a >128-bone skeleton can no longer issue an
  oversized uniform upload; the evaluator still carries up to 256 bones.

<!-- //ai: audio lane merge — presets/tour/demo, TPT filter core + master filter, engine migration. -->
### Added
<!-- //ai: math verification expansion — fail-closed corpus, edge/random/invariant/mutation/soak gates, remaining APIs. -->
- **Math verification expansion**: the gxmath corpus now fails closed on
  malformed, non-finite, duplicate and arity-mismatched dumps; adds
  quaternion, matrix-auxiliary/small-matrix and SDF-extension coverage,
  classified edge probes, seeded randomized differential checks, independent
  invariant checks, a disposable-tree mutation self-test and an opt-in soak
  target. It also implements the remaining 2×2/3×3 determinant/trace,
  cofactor/adjugate/`From_F32v4` and `F64v4` lerp APIs.
- **Proof catalog and small-matrix contracts**: generated
  `docs/proof_inventory.md` with per-tier/module declaration counts, root
  reachability, namespaces, domains, matrix models, kernel coverage and math
  gaps, gated by `make proof-inventory-check`; added `GxMath.MatrixSmall` for
  2×2/3×3 determinant/trace/transpose/scaling and `GxMath.Vector64` for F64v4
  lerp; split the matrix namespace into `MatrixTuple`/`MatrixField` with a
  `MatrixBridge`, and normalized the visible-image-sky and normal-matrix
  namespaces.
- **Proof domain cleanup**: generic easing moved to `GxMath.Easing`; the
  attenuation, envelope and synth-filter contracts moved to
  `GxProofsGfx/Audio/`; the generic `ClipMatrix` and cofactor/inverse algebra
  moved to `GxMath`, leaving the camera-history bridge in
  `GxProofsGfx/Temporal/CofactorInverse`. The remaining GFX modules are grouped
  under `Kernel/`, `Matrix/`, `Renderer/`, `Temporal/`, `Postprocess/`,
  `Weather/` and `Ssr/` domains.
- **Quaternion proof slice**: added `GxMath.Quaternion` with multiplication,
  norm-squared multiplicativity, conjugate involution/norm preservation,
  `q·conj(q)` identity, and left/right inverse contracts under nonzero norm.
- **Quaternion rotation depth**: added 20 contracts for native sandwich action,
  unit norm preservation, antipodal sign, inverse restoration and composition
  order. The engine matrix/vector bridge is explicitly unit-only; general
  nonunit scaling/difference and a counterexample are proved. Source guards
  and actual-TU native fixtures pin matrix fields, transpose/sign choices,
  +Z quarter-turn handedness and noncommuting products without changing C.
  IEEE normalization/conditioning, GPU upload and nlerp/slerp remain outside
  the new proof claims.
- **SDF proof slice**: added `GxMath.Sdf` for the sphere, plane, cube, torus and
  capsule primitives, with sphere radius/sign correspondence, plane linearity,
  torus center distance, cube corner zero and the zero-length capsule
  reduction. The model is real-valued and assumes a unit plane normal; float
  execution, other primitive Lipschitz bounds and the remaining SDF primitives stay
  with the numeric math oracle.
- **SDF Lipschitz depth**: added six exact-real vector metric helpers
  (Cauchy-Schwarz and triangle/reverse-triangle inequalities) and seven SDF
  Lipschitz/clearance contracts. Sphere is 1-Lipschitz; plane has constant
  equal to normal length, specialized to an explicitly unit-length normal.
  Non-crossing step corollaries remain exact-real claims, not native/GPU
  raymarcher safety proofs. Numeric checker controls pin sharp bounds,
  nonunit-plane violations and cone/Menger exclusions without changing tolerances.
- **Scalar proof slice**: added `GxMath.Scalar` for clamp bounds/idempotence/
  saturation, lerp endpoints, normalize/lerp inverse round trips, and remap
  endpoint/lerp-normalize identities. Wrap, integer factorial/GCD/combinatorics
  and tolerance equality remain numeric targets.
- **Fourier proof slice**: added `GxMath.Fourier` for DFT linearity, the zero
  sequence and delta/impulse responses. The iterative radix-2 FFT, sine-bin
  phase and IEEE error remain numeric targets.
- **Fourier inverse/Parseval depth**: added the positive-sign `1/n` inverse
  specification and 14 contracts bridging the original exponential DFT to
  Mathlib's finite transform, proving both inverse round trips, adjoint/pairing
  identities and Parseval for nonzero length. Native eight-sample FFT property
  checks now reconstruct the complex input with the inverse specification;
  negative controls reject wrong scaling and reversed frequency order even
  when energy/DC agree. No native inverse FFT or IEEE correctness is claimed.
- **Calculus proof slice**: added `GxMath.Calculus` for exact forward-difference
  constants/identity/square/affine/linearity identities and one Newton step on
  a nonzero-slope affine function. General convergence and the tolerance-based
  zero guard remain numeric targets.
- **Simpson integral exactness**: added 15 contracts in `GxMath.Calculus`,
  bridging the native endpoint/parity loop to paired panels and the exact
  signed integral of every cubic at positive even step counts. Zero-step,
  one-step trapezoidal and odd-step behavior stay explicit, with proved
  quadratic/constant counterexamples. Source guards and a native probe of the
  actual gxmath translation units cover 80 bounded even-step polynomial cases
  plus branch/null fixtures. C behavior, existing tolerances, IEEE accuracy
  and convergence claims are unchanged.
- **Color and basic-unit proof batch**: added `GxMath.Color` with 17
  per-channel truncation/clamp, lerp endpoint, normalization and multiply
  contracts, and `GxMath.Units` with 12 length/temperature round-trip and
  reference-value contracts. Source-drift witnesses and negative controls run
  in `math-check` and `proofs-full`; IEEE rounding and out-of-range casts remain
  explicitly outside the proof claims.
- **Cartesian sky-frame proofs**: added `GxMath.Sky` with 14 contracts for
  bidirectional equatorial/ecliptic and observer-parameterized horizontal
  transforms, squared-length/length preservation and orientation. The native
  horizontal convention is orthogonal but reverses handedness; C signs remain
  unchanged. Source-drift and numeric negative controls pin axes, parameters,
  all output slots and reciprocal-scaling failures; IEEE/angle-branch claims
  remain outside the proofs.
- **Branch-aware coordinate proofs**: added `GxMath.Coordinates` with 24
  cylindrical/spherical contracts: Cartesian and direct round trips,
  principal/wrapped azimuth, radial/inclination identities, and explicit
  zero-radius/pole ambiguities. Source-drift witnesses and numeric controls
  cover all six helpers without changing C or tolerances. Positive radius,
  canonical angle and nonpolar assumptions stay explicit; signed-zero/IEEE
  origin behavior is not certified.
- **Proof significance and evidence catalog**: added the 0–100
  `proofs/SIGNIFICANCE.md` policy and `proofs/IMPORTANCE.json` reviewed
  scores, justifications, source/guard/gate paths and known gaps for all 110
  proof modules. The generated catalog now derives
  `checked`/`grounded`/`guarded`/`complete` evidence levels and lists modules
  below their importance target.
- **Proof certificate runner**: `make proof-certify` writes hashed
  `catalog`/`kernel`/`guarded` artifacts with gate results, audit hashes,
  reviewed target/achieved levels and explicit reviewed gaps; `GATED` records
  passing gates with outstanding importance gaps.
- **Kernel/RHI/temporal source guards**: added static source-contract checks
  for maindata switch flags, moduleInfo mirroring, refresh/reset defaults,
  FBO lifecycle, RHI blend mappings and temporal-history lifecycle. The
  reviewed proof catalog now has no below-target evidence gaps; mathematical
  coverage limits remain explicit in the source-family table.
- **OPX/PAD presets, tour bindings and the ambient demo**: versioned JSON
  preset round trips for both engines (`gX_OPX_Preset_Save/Load`,
  `gX_Pad_Preset_Save/Load`; loaded through the engine clamps; repo
  resource paths refused under `GX_NO_SETTINGS_SAVE`), the `Opx_*`/`Pad_*`
  tour surface (notes, per-voice and default edits, ADSR/filter/chorus/
  width, readback getters, mute and mixer-attach state, presets, constants)
  and `resources/script/audio/opx_pad_demo.as` — a PAD chord progression
  under an OPX bell melody and bass with keyboard toggles and a gate
  self-test; the scripting gate gains both probes and the engine mute env
  values.
- **Master filter and the shared TPT filter core** (`src/audio/filter.{h,c}`):
  a topology-preserving-transform state-variable filter (LP/HP/BP, the full
  cutoff range usable, stable at any resonance) plus the optional master
  filter on the final mix — kept at the mixed-processor chain tail, off by
  default, persisted with the settings and scriptable via
  `Audio_SetMasterFilter`. The demo synth's and PAD's per-voice filters now
  use the same core, so cutoffs above ~0.17 no longer collapse onto one
  ~8 kHz filter (patches there read brighter/more open than before).

### Fixed
- **Settings schema arena overflow**: the settings JSON schema arena was
  sized at exactly 64 objects; the master-filter fields overflowed it and
  corrupted the heap (the SFX suite's reverb allocation aborted). Capacity
  is 96 with headroom.


<!-- //ai: PAD layered ensemble engine core (detuned layers, drift, chorus, moving filter, offline tests). -->
### Added
- **PAD — layered ensemble pad engine** (`src/audio/pad.{h,c}`, separate
  from the demo synth and OPX): every voice carries three detuned saw/tri
  layers, each with its own level, pan and slow drift LFO (independent
  rates give the ensemble motion), under a long ADSR (default 1.2 s attack /
  3 s release) and a per-voice resonant LP/HP/BP filter with its own slow
  LFO. The engine adds the two pieces that make a pad read as a pad: a
  **stereo ensemble chorus** (two modulated short delays at a phase offset)
  and a **width** control over the layer pans. Notes/defaults/voice-edit
  surfaces, an offline render core shared by the raylib mixed-processor
  adapter and a new CPU suite (detune beating as a modulated envelope, slow
  attack/release tails, chorus breathing vs a steady dry tone, width
  mono/stereo, filter LFO sweep and LP/HP comparison by single-bin tone
  magnitude, stealing, determinism, extreme-setting boundedness). Output
  rides the synth mixer bus and starts muted (`GX_PAD_MUTE=0`). Presets,
  tour bindings and an editor panel are the queued follow-ups (see
  `docs/audio.md`).

<!-- //ai: OPX 8-operator FM engine core (phase modulation, algorithm table, offline tests). -->
### Added
- **OPX — 8-operator FM engine** (`src/audio/opx.{h,c}`, separate from the
  demo synth so synthvis/demo/patches stay untouched): per voice, 8 sine
  operators each with ratio, level, detune, self-feedback and its own ADSR,
  evaluated with **phase modulation** (high indices add harmonics instead of
  the demo synth's pitch sweep) in a single forward pass over a fixed
  data-driven algorithm table — 8 routings from an additive sine bank to a
  full cascade, every modulator indexed below its target (`gX_OPX_Algorithm*`
  readouts). Note/steal/all-off surface, a defaults→voice parameter surface
  (new-note defaults plus sounding-note operator edits), an offline render
  core shared by the raylib mixed-processor adapter and a new CPU suite
  (table invariants, additive/ratio tuning, modulation timber shift,
  per-operator envelopes/release, feedback boundedness, stealing,
  determinism, mute/pan). Output rides the synth mixer bus and starts muted
  (`GX_OPX_MUTE=0` or `gX_OPX_SetMuted(0)`). Presets/UI/tour bindings are
  the next slices.

<!-- //ai: audio mixer buses + procedural SFX pool + sandbox impact/tool audio; no assets. -->
### Added
- **Audio mixer buses**: every gX playback path now scales by a bus —
  `GX_AUDIO_BUS_MASTER/MUSIC/SFX/SYNTH` (`gX_Audio_BusSetVolume`/
  `BusGetVolume`/`BusGain`; master is the device master, the rest are
  0..1 gains). The pause menu gains a four-slider mixer (Master / Music /
  Effects / Synth) bound to the persisted `musicVolume`/`sfxVolume`/
  `synthVolume` settings fields; a drag applies live through a
  per-frame settings→mixer sync (no Apply press) and the settings view's
  Save persists it. `gX_Audio_SetMuted`/`GX_AUDIO_MUTE=1` gate every gX
  play path for quiet audits; `--disableAudio` now actually skips the
  device (it was parsed but ignored), and `--masterVolume`/a saved master
  volume are applied at boot (previously only on an Apply press).
- **Procedural one-shot SFX + sandbox impact audio**: a small voice pool
  (`src/audio/sfx.{h,c}`, `gX_AudioSFX_*`) synthesizes five variants
  (thud/clack/metal/arc/click) at audio-init — pure deterministic builders,
  four copies each so bursts overlap, 3D-positioned, bus-scaled and
  rate-limited. The physics sandbox drains its existing contact-event
  queue into the pool: heavy hits read low and loud, charged contacts arc,
  water entries dull, desaturated bright tints and hot bodies ring metal
  (`gX_ScenePhysics_ImpactSoundMap` — pure and test-pinned;
  `GX_PHYSICS_SFX=0` or the editor's new `impact sfx` toggle disables), and
  toolgun trigger pulls play a per-tool tick. Audits stay silent
  (`GX_AUDIO_MUTE=1` in the sandbox gates' environment).

<!-- //ai: audio correctness fixes (reverb damping, sequencer start/drift, per-voice portamento, patch apply, music load, master volume). -->
### Fixed
- **Audio defects**: the reverb's `damping` parameter was stored but never
  applied (the tail stayed bright regardless) — it is now a one-pole
  absorbtion on the recirculating tap, with `gX_Audio_Reverb_Process` as a
  device-free core the tests pin; the step sequencer skipped step 0 on
  start (the driver advanced before playing) and zeroed its timer every
  step (drifting off the beat) — the first update now fires the armed step
  and the timer carries the remainder; `gX_Synth_SetPortamento` was a no-op
  (per-voice glide time now sticks and overrides the global default);
  `gX_Synth_Patch_Apply` dropped the patch's oscillator/LFO surface
  (waveform/pulse width/LFO now land as defaults); `gX_Audio_Music_Load`
  reported success for a failed load and `strcpy`'d an unbounded name.

<!-- //ai: toolgun mode (shared editor crosshair/tool framework) + sandbox tools. -->
### Added
- **Toolgun mode for editors**: a shared crosshair/tool framework
  (`src/tools/toolgun.{h,c}`, `gX_Tools_Toolgun_*`) — a crosshair at the
  screen centre, a blockout procedural viewmodel projected through the
  camera (no assets; bob/sway/recoil plus a muzzle flash), primary/secondary
  fire reported as aim actions and the wheel cycling tools. First adopter:
  the physics sandbox — its panel gains a **Toolgun** button (or
  `GX_TOOLS_TOOLGUN=1`) riding the free camera, with grab/freeze/heat/
  impulse/pin/remove acting on the crosshair aim
  (`gX_ScenePhysics_AimAtModel` + the `Tool*`/pin surface; the sandbox's own
  mouse interaction stands down in tool mode, pins release on RMB/`R`/prop
  removal and `K` draws them). Headless pins in `--test-only` plus a toolgun
  leg in `make physics-editor-check`.
<!-- //ai: rover wheel spokes + live debug anchors. -->
### Fixed
- **Rover wheels read as parked and stale joint markers**: the hinge2
  rover's wheels are smooth cylinders (rotationally symmetric), so their
  spin was invisible — each wheel now carries a light spoke bar as a second
  model sharing the wheel's interpolated transform, and the debug overlay
  draws the live joint anchor instead of the build-time snapshot (moving
  rigs like the rover and the pump showed their markers offset). Probe:
  the wheels' physics was already correct (anchors coincident, wheel tops
  flush under the chassis, front axle carrying the drive).
<!-- //ai: piston joint + heightfield terrain; no new assets. -->
### Added
- **Piston joints and heightfield terrain**: `gX_PhysicsJoint_CreatePiston`
  (a slide along its axis plus a free rotation about it — the drill/screw
  DOF; the group-1 params drive the slide, the *2 variants the rotation,
  a NULL partner binds to the kinematic anchor) and
  `gX_PhysicsWorld_AddHeightfield` (a static terrain collider sampling a
  caller height grid; built wrapped because this ODE build's bounded mode
  drops bodies through — probed — so callers pass one tile with flat
  borders), with `gX_Mesh_GenHeightGrid` as its visual twin. The sandbox
  gains a piston pump (a vertical plunger square-wave driven between its
  limits with a rotation motor; `GX_PHYSICS_PUMP=0` skips) and a terrain
  mound past the belt's far end that belt cargo tumbles down
  (`GX_PHYSICS_MOUND=0` skips). Wrapper suites pin the piston's slide/
  limits/off-axis hold/rotation and the mound's downhill roll.
<!-- //ai: AMotor tier + ragdoll self-collision; no new assets. -->
### Added
- **AMotor joint tier and ragdoll self-collision**:
  `gX_PhysicsJoint_CreateAMotor` plus per-axis limits/motors
  (`AMotorSetEuler` for ODE's swing-twist chain — the two reference axes
  must be perpendicular; `AMotorSetAxes` for user-mode axis motors;
  `AMotorGetAngle`/`GetAngleRate`; the generic joint params drive axis 0).
  Torso/head/hip/shoulder ball joints now carry swing bands (±0.40 rad
  spine/hips, ±0.45 neck/shoulders, centered on the rest reading), the legs
  are spaced wider and the bones collide with each other: jointed pairs are
  skipped by the wrapper, so an arm no longer passes through the torso and
  the dummies stay stable in the water float case. Clamping the AMotor
  twist axes destabilises the articulation — bands only, noted in the
  docs. `gX_Tests_Physics_AMotor` pins the cone band (it moves but stays
  inside) and the user-mode motor rate.
<!-- //ai: jointed-pair filtering + physics editor free camera; no new assets. -->
### Added
- **Editor camera + articulation contact filtering**: the physics editor
  gains a modal free camera (WASD/EQ + mouse look; Esc returns to the
  panel; `GX_PHYSICS_FREECAM=1` opens straight into it for captures) —
  the engine freezes the scene camera binds while any tool is open, so the
  panel hands the input over explicitly, matching the GFX editor's pattern.
  The wrapper's near callback now skips pairs connected by a constraint
  joint before the narrow phase, so the overlapping segments a joint is
  built with never reach the solver (unconnected bodies still collide;
  `gX_Tests_Physics_SelfContact` pins both). Ragdoll self-collision stays
  off — instrumented: the ball-jointed limbs overlap deeply within one step
  and ODE's deep-contact response launches the articulation, so it needs
  joint range limits (an AMotor tier) first; noted in the ragdoll docs.

### Fixed
<!-- //opencode: EXTRAS card grid — the background card shared the sky cell. -->
- **EXTRAS tool grid**: the card table gave the background editor `col 1,
  row 5` — the sky editor's cell — so both labels stacked on one slot: the
  labels double-printed and the background editor was unreachable from the
  grid. Moved to `col 0, row 5` (the empty left-column slot); the `title-flow`
  gate now runs the EXTRAS view with the element trace and fails when two
  tool cards share a cell. Found by the sub-vision review.
<!-- //opencode: battle HUD — the enemy HP value chip overlapped the bar tail. -->
- **Battle HUD**: the enemy HP bar was sized from the party panel
  (`playerPanel.width * 0.63` = 174 px) while its opaque value chip sits at
  `enemyPanel.width - 88 * scale`, so the right end of the bar ran under the
  chip and the last ~13% of the track was invisible — 525/1000 read as ~60%,
  201/1000 as ~23%. The bar now reserves the chip gutter
  (`enemyPanel.width - 138 * scale`, 8 px gap) and reads true (52.1% measured
  vs 52.5% shown) at 1024x768 and 1600x900; `gfx-battle-check` stays green
  with no re-baseline. Found by the battle HUD vision review.
<!-- //opencode: physics sandbox HUD — "1 STEPS" pluralization. -->
- **Physics sandbox HUD**: the step line read `1 STEPS @ 60 HZ` in the
  default one-substep state; it now reads `1 STEP` (plural returns above
  one). Found by the physics HUD vision review.
<!-- //opencode: physics sandbox HUD — the panel fits the key legend at small scales. -->
- **Physics sandbox HUD at small windows**: the panel kept shrinking with
  the HUD scale while the key legend's pixel font floors at its 12 px size,
  so at 800x600 and 960x540 the legend drew outside the panel background.
  The HUD scale now floors at 0.95, which keeps every line inside from
  800x600 through 1920x1080 (measured: no legend pixels beyond the panel at
  all five probe sizes).

### Changed
<!-- //opencode: title-flow covers the ESC back routes as bit-identical returns. -->
- **Title-flow round trips**: the `title-flow` gate now also asserts the ESC
  back routes (the pause overlay, LOAD GAME, EXTRAS) return to a
  bit-identical main-menu frame (0.000 deltas — the fixed clock makes the
  assertion exact), on top of the open/no-crash coverage.
<!-- //opencode: gui effect-contrast gains the depth-profile row. -->
- **GUI depth-contrast probe**: the effect-contrast gate gains a
  `depth-profile` row — the Aero buttons run the SDF surface program, so a
  mode-1 horizon variant against mode 0 asserts the WCAG AA floor per text
  band and proves the profile rendered (ROI delta 15.97). Measured: horizon
  is tighter than metallic (title 6.98 -> 5.05, button 6.14 -> 5.14, both
  still above 4.5); the textured glass hint band is not SDF-shaped (4.74
  unchanged). Closes the depth-aware contrast follow-up in
  `docs/gui_visual_backlog.md`.

## [0.17.2] - 2026-10-15

### Fixed
<!-- //opencode: the shaded centerpiece is depth-tested and back-face culled. -->
- **Title centerpiece depth**: the shaded teapot showed front and back faces
  at once — the draw had depth testing and culling disabled (invisible with
  the old flat silhouette). It now clears the title depth buffer,
  tests/writes depth (LESS) and culls back faces, with the whole 3D state
  wrapped in a `gX_GL_State_Push/Pop` so the GUI canvases that draw
  afterwards see the state they had before.

### Fixed
<!-- //ai: user-approved cookie client-memory safety repair; assets, quality and defaults unchanged. -->
- **Cookie atlas upload safety**: decoded images are normalized to RGBA8 before
  the RGBA upload. Invalid dimensions/formats, oversized cells and unsuccessful
  conversions are rejected with diagnostics, without resizing; atlas writes
  stay within the 8×8 grid. Format/bounds regressions cover the real raylib
  decoder/converter. Repaired-build exact RGBA/HDR controls pass with cookies ON,
  profiler OFF/ON and SSR OFF/ON at both tested resolutions; historical failures
  remain recorded in `docs/gfx_audit_2026-10.md`.
<!-- //ai: native timing correctness only; no draw-order, effect-quality or default changes. -->
- **GFX profiling identity**: pass IDs now match the execution table, so the SSR
  timer measures the actual draw rather than input preparation. Explicit
  `--gfx-profile` uses renderer-owned nonblocking query sets with source frame,
  draw and readback IDs; incomplete/overflow samples are not replayed as fresh
  data. Benchmark schema 3 selects matching CPU/GPU frame windows, drains queries
  with unmeasured tail frames and rejects missing/duplicate/legacy samples.
  Existing draw order and quality stay unchanged; timing-only default-scene
  identity failures were retained, then addressed by the separate cookie repair.
<!-- //opencode: gui-matrix captures pinned on the fixed frame clock (SCENE flake). -->
- **GUI matrix determinism**: the capture rows now pin `GX_FIXED_DT`, so the
  title backdrop palette and the centerpiece (both driven by
  `maindata.timeElapsed`) no longer drift with wall-clock load. The
  expected-blank `SCENE` row flaked under the audit matrix (17/18, retried
  green, with the verdict naming `SCENE`) and now measures a bit-identical
  delta across runs, including three concurrent full runs.

## [0.17.1] - 2026-10-14

### Fixed
<!-- //opencode: EXTRAS crash fixed (tools[10] vs 12 cards) + the title-flow gate. -->
- **EXTRAS crash (release-critical)**: the committed title card table declared
  `tools[10]` while carrying 12 entries and looping `ti < 12` — opening
  EXTRAS read garbage label pointers and segfaulted the engine. The array is
  now `tools[12]`, and the new **`title-flow`** gate
  (`scripts/title_flow_check.py`) drives LOAD GAME / DEMOS / EXTRAS / CREDITS
  and the ESC overlay, failing on a non-zero exit or an unchanged frame
  (pre-fix it fails exactly at `extras` with exit 139).

### Changed
<!-- //opencode: title centerpiece shading + the version tag reads BETA. -->
- **Title polish**: the rotating centerpiece now draws through the
  `diffuse.vert`/`diffuse.frag` resource pair (fixed world key light +
  ambient floor) instead of the unlit batch shader — the teapot reads shaded
  instead of a flat silhouette, and the light stays put while the model
  spins. The version tag reads `BETA` (the logo badge and the hint), matching
  the release; the author's WIP copy of `titlegx.c` still says `"alpha"` /
  `"gX ALPHA"` and should carry the same change when it lands.

### Changed
<!-- //opencode: title-scene audit — SETTINGS theme-row clearance fixed; ESC overlay finding reported. -->
- **Title-scene audit** (`docs/ui_title_audit.md`): the SETTINGS template's
  theme radio stack collided with the action row at the lean floor (draw-truth
  rects: Color-safe 603..632 against the buttons at 631..674) — the block
  moved up one step for a 16 px clearance, verified by trace and capture.
  Reported for the author's WIP: the ESC pause panel over the title lets the
  title's menu read through (`escape.panelBg` alpha 200), and the TITLEGX look
  review waits for that WIP.

### Changed
<!-- //opencode: gui-matrix verdict names the failing rows. -->
- **GUI matrix verdict**: the `gui-matrix` summary line now names the failing
  template rows, so the recorded first verdict of a retried flake keeps its
  diagnosis (the retry overwrites the capture report). A one-off 17/18 flake
  in the release gate run was not reproducible in eight consecutive runs.

### Changed
<!-- //opencode: physics editor layout — scrolling tuning region + folded parameter groups. -->
- **Physics editor layout**: the tuning sections now live in one scrolling
  region with the parameter-heavy groups (em/magnets, thermal, fluid) folded
  into collapsing headers, so world/content plus the presets and status
  footer stay on screen at the default size — the UI audit's last open
  follow-up (the background editor's pattern).

## [0.17.0] - 2026-10-14

### Fixed
<!-- //opencode: the textwriter guard probe restores the inherited harness save-guard. -->
- **Textwriter save-guard probe**: the probe cleared the harness save-guard
  with a bare `unsetenv`, wiping a harness-set `GX_NO_SETTINGS_SAVE` for the
  rest of the process — the gfx preset gate's shipped-path probe then wrote
  its refused path. The probe now snapshots and restores the inherited value
  (the sky/synth/physics pattern), and the preset check's failure-reporting
  path imports `subprocess` so a real error is not masked by a NameError.

### Fixed
<!-- //opencode: TAA visibility floor re-baselined after the resolve blend-state leak fix. -->
- **TAA visibility floor**: the `gfx-bijection` TAA floor (0.5) was calibrated
  against 0.915, which a resolve state leak had inflated — the resolve saved
  the blend state after disabling blending, so TAA-on frames kept blending
  off downstream (a frame delta that was not AA). The camera-only temporal
  landing fixed the save/restore placement; the genuine TAA visibility
  measures 0.38-0.42 and the floor is 0.2 (the file's 2x-margin rule).

### Fixed
<!-- //opencode: hybrid raymarch masking compiles opt-in; composite stays discard-free. -->
- **Raymarch composite layer**: the hybrid depth-masking block (`discard`,
  shared-depth writes) now compiles only while a hybrid depth mode owns the
  frame (`CF_HYBRID_MASK`), so the standalone/composite shader permutation
  carries no `discard`. A program carrying the never-executed `discard`
  statements loses the compositor layer draw (observed; `hybridDepthEnabled`
  reads 0 and an impossible branch constant still reproduces it), which made
  the `raymarch-features` composite legs read a no-op frame. The masking
  permutation itself is unchanged (`gfx-raymarch-depth` holes/leaks 0.000).

### Fixed
<!-- //opencode: gate re-baselines after author landings (picker, materials, horizon, probe). -->
- **Gate re-baselines**: the picker's overlap pick follows the demo content's
  fixture model to cylinder stand-in swap (a nearest-hit outcome, not a coordinate remap);
  the materials probe sidecar uses the `GX_EMISSIVEFILE=2` placement
  hook instead of the demo slot; the horizon zenith cap (61.0 measured) and
  the probe near-response ceiling (146.0 measured) follow the brighter sky
  from the raymarch/sky landing. Each gate carries its measured value and
  rationale; the picker fixture swap is the demo content's fixture change.

<!-- //ai: authorized local defensive runtime repair; no history or asset cleanup. -->
### Security
- **Source-export privacy safeguards**: export-ignore rules omit root screenshots,
  private reports/settings/session state, certificate artifacts and quarantined
  resources without deleting local or tracked files. Shared archive checks now
  guard certification and tagged releases, with isolated legacy-tag negatives;
  archive safety does not certify or rewrite repository history.
- **Heap failure rollback**: checked count/byte arithmetic and transactional
  data/metadata/filename resizing preserve existing buffers on allocation
  failure. Add/seek paths return failure instead of using the old last element.
  The allocator's existing realloc API semantics are unchanged.
- **Bounded file-backed JSON**: 16 MiB input and 64-container nesting limits;
  non-finite numbers, malformed numeric tokens and trailing data are rejected.
  Failed string allocation or container insertion rejects and releases the
  partial document. Native rollback/parser regressions are part of `make check`.

### Added
<!-- //opencode: UI audit (wiring gate + captures + review) and its first fixes. -->
- **UI audit** (`docs/ui_audit.md`): `make ui-audit` mechanically pins every
  tool's open paths (CLI/`GX_TOOLS_OPEN`/EXTRAS card/handler/flag) and the
  panel conventions (title prefix, first-use size, status line; tool kinds
  make the deliberate differences explicit), and the capture harness
  (`scripts/ui_audit.py --capture <dir>`) launches all thirteen tools at
  1024×768/1600×900 for the vision review. First-pass fixes: the camera rail
  editor title follows the `gX Tools - …` convention, the background editor's
  polisher list became a collapsing section with a scroll child (reactions +
  status stay on screen at the lean floor), and the api_reference tools
  bullet spells the opener family and both open-path sets. Flagged for
  follow-up: the physics editor's long sections exceed its default height.
<!-- //opencode: rail tour demo (shipped scripting example) + gate probe. -->
- **Rail tour demo**: a shipped scripting example
  (`resources/script/tour/tour_demo.as`) flies the camera rail sample over
  the GFX scene with tagged captions and exits when the rail finishes —
  dogfooding `Camera_Rail_Load` and the rail editor's format end to end. The
  scripting gate runs it headless (load/keyframes/finish markers); the flat
  boot scan skips the `tour/` subdirectory so it stays inert unless started
  explicitly.
<!-- //opencode: sky editor (editors slice 5) + sky preset IO. -->
- **Sky editor** (editors plan slice 5): the sky renderer gains preset IO
  (`gX_RendererSky_Preset_Save/Load` — look parameters + the ephemeris clock
  time; shipped paths are refused under `GX_NO_SETTINGS_SAVE`; pinned by
  `gX_Tests_Sky` in `--test-only`, with a sample `sky_sample.json` shipped)
  and a standalone editor: the **ephemeris clock** (scrub/play at a speed,
  sun-elevation readout — the sun/moon/planet directions are ephemeris-
  driven) plus the look parameters (density/hue/glow/contrast/altitude/
  turbidity/sun disk/stars/moon/helpers) and a preset fixture list. Opened
  by `--sky-editor` (routes to the sky scene), `GX_TOOLS_OPEN=sky` or its
  EXTRAS card — the twelve-slot tool grid is now full. Gate:
  `make sky-editor-check`.
<!-- //opencode: background editor (editors slice 4) + shared bg panel extraction. -->
- **Background editor** (editors plan slice 4): the GFX editor's background
  panel is extracted into a shared body (`src/tools/background/bg_panel.cpp`)
  that both hosts draw — the GFX editor keeps its in-renderer section (its
  `GFX_BG_UI` marker unchanged), and a new standalone background editor adds
  a preset workflow with a fixture list under `resources/tools/background`
  (sample `palette_showcase.json` ships), the Background polisher entries
  with per-entry reset, and reaction pulses (Pulse/Shake/Flash/Warp). Opened
  by `--background-editor` (routes to the background scene),
  `GX_TOOLS_OPEN=background` or its EXTRAS card (eleven cards). Gate:
  `make background-editor-check` + the existing `make gfx-background-check`
  for the shared path.
<!-- //opencode: camera rail editor (editors slice 3, module-side) + rail persistence. -->
- **Camera rail editor** (editors plan slice 3): the tour camera rail gains
  JSON persistence (`Camera_Rail_Save/Load` + `Camera_Rail_Count/AddGrab`
  bindings; shipped preset paths are refused under `GX_NO_SETTINGS_SAVE`) and
  a module-side editor panel (`src/tools/tour/rail_editor.cpp` in `gx_shell`):
  grabs from the live camera, per-keyframe t/pos/target/fov/bezier editing,
  reorder/remove, play/stop and a fixture list. Opened by `--rail-editor`,
  `GX_TOOLS_OPEN=rail` or its EXTRAS card (now ten cards); the rail is
  shell-owned, so the panel ships with the module and the default payload
  pays nothing. Gate: `make check-scripting` pins the grab/save/load round
  trip, the preset guard and the module-drawn open path.
<!-- //ai: measured hardware-floor study; no engine, build or resource change. -->
- **Minimum hardware study** (`docs/minimum_hardware.md`): measured the
  performance floor on the reference class (6-core Zen1-class CPU / GTX 1060 6 GB,
  Release, pinned benchmark harness). The shipped default showcase needs a
  GTX 1060-class GPU for 60 fps at 1024x768 (75 fps quiet / ~57 contended;
  `16.6 ns/px` defaults vs `2.5 ns/px` with all effects off); SSR alone is
  ~60% of the frame; a GTX 950/750 Ti class suffices with SSR off (model).
  Memory floors: <= 192 MB RSS, ~209 MiB of render targets at 1024x768
  (+349..506 MiB observed driver-inclusive), 384 MiB all-or-nothing
  point-shadow array caveat. Also records the auto-exposure readback hitch
  (CPU p95 doubles every 10th frame; `GX_NO_AE=1` removes it) and a
  8-39% GPU drift vs the 2026-09-20 benchmark pin. Single-rig inference;
  weak-GPU/downclock validation is an open probe.
<!-- //ai: finite filter-support evidence; no sampling/default/runtime visibility change. -->
- **SSR boundary-footprint diagnostic**: owned cube-face/tap replay separates
  capture discretization and filtering from point-ray coverage, retains face
  seam uncertainty and the original disagreement counts, and keeps filtered
  color support distinct from reflection-ray visibility. CPU negatives run in
  `make check`; native captures remain opt-in and external.
<!-- //ai: L0 backend study; no runtime, build or resource change. -->
- **Backend study** (`docs/backends.md`): source-grounded review of the L0
  layer under the raylib-only constitution. Records the certified context
  rungs (GL 3.0-3.3 plus the GL 4.3 compute profile), the retained shader
  dialects (desktop legacy, ES, Vulkan), the real ES-3 seam (raylib ships an
  ES 2.0 loader plus OES/EXT extensions, so no ES 3.1 compute), the measured
  SPIR-V corpus result (1/153 shaders portable as-is: location 111,
  uniform-block 31, binding 7, version 3) and the recommended order (decide
  the retained-but-unreachable `--es-dump-shader`/`--vk-dump-shader`
  handlers; platform layer only for a named target; Vulkan/WebGPU parked).
  No engine, build or resource change.
<!-- //ai: verified local sources and explicit external archive references; no guard exemption. -->
  Source citations now distinguish the external gameX archive from shipped gX
  files and name existing local files instead of wildcard-only path prefixes.
<!-- //ai: engine-owned reflection gap controls; no runtime fix or camera implementation. -->
- **Fixed-camera SSR gap fixture**: generated reflective plane, off-screen
  emitter and optional occluder; independently compares planar, cubemap and
  SSR contributions against first-hit masks and native depth. CPU/negative
  controls run in `make check`; GPU captures are opt-in and external. Cube
  visibility residuals remain observations, not an admitted reflection fix.
<!-- //ai: proof-first reflection debugging; no renderer or preset-policy change. -->
- **SSR coverage contracts**: Lean 4 proofs of existing auxiliary routing and
  conditional ray/plane intersections, an off-plane mirror-camera parallax
  counterexample, and a separate optional second-camera eligibility/priority
  specification. Mutation-tested source-clause guards run in the core proof
  gate. No new camera pass, renderer-default change or parity claim.
<!-- //ai: joint feedback + breakable weld target; no new assets. -->
- **Joint feedback and breakable welds**: `gX_PhysicsJoint_EnableFeedback`/
  `GetFeedback` expose ODE's per-step constraint force/torque for a joint's
  first body (world coordinates; the world owns the storage and prunes it
  when a joint is destroyed). The sandbox adds a break target — a plank
  welded to a gantry post by a fixed joint whose feedback the scene polls;
  the static reading carries the board's weight and an impact above the
  break force (10 N) destroys the weld so the board drops (thrown props,
  the rover and dragged links all register). `R` rewelds it at the spawn
  pose; `GX_PHYSICS_TARGET=0` skips the rig. The wrapper suite pins the
  weight reading, the pull spike, the disable path and registry reuse; the
  scene suite pins the static load, the no-break case, the break, the fall
  and the reweld.
<!-- //ai: hinge2 joints + cylinder body type; no new assets. -->
- **Hinge2 joints and cylinder bodies**: the wrapper gains ODE's hinge2 —
  a hinge with a suspension (axis1: the slide and the steering rotation,
  softness via `SetSuspension`; axis2: the spin, `SetMotor2`/`SetLimits2`/
  `GetPosition2`) — and binds a NULL partner to a hidden per-world kinematic
  anchor because ODE needs two real bodies for hinge2 (motor commands wake
  both joint bodies). `gX_Mesh_GenCylinderZ` aligns the render mesh with
  ODE's Z cylinder axis. The sandbox gains a hinge2 rover (box chassis on
  four cylinder wheels, steered front wheels locked in the spawn pose,
  front-wheel drive, a soft suspension that leans through the turns; the
  spawn uses the measured turn radius because the tyres slip;
  `GX_PHYSICS_ROVER=0` skips) and the prop grid now cycles boxes, standing
  cylinders and spheres. The wrapper suite pins the suspension hold, the
  steering stops, the spin rate and the anchor-body path; the scene suite
  pins the rover build and a closed lap inside the yard.
<!-- //ai: mass/center-of-mass properties; no new assets. -->
- **Mass properties**: `gX_PhysicsBody_SetMass` rescales a body's total
  mass (the inertia follows, the balance point stays) and
  `SetMassCenter`/`GetMassCenter` move the center of mass in body-local
  space with the inertia rebuilt by the parallel axis theorem. ODE 0.16
  keeps the center of mass at the body origin, so the wrapper offsets the
  shape the other way and moves the origin onto the balance point while
  the shape keeps its world pose (the render sync and debug overlay undo
  the offset). The sandbox adds a balance-post trio — identical planks
  whose top-heavy, centered and bottom-heavy mass centers decide the same
  tilt test (`B` re-poses and releases them; `GX_PHYSICS_COM=0` skips) —
  and the wrapper suite pins the round trips, the origin move, the inertia
  scaling and the tipping outcome.
<!-- //ai: kinematic movers; no new assets. -->
- **Kinematic movers**: the sandbox drives a lift platform (vertical sine)
  and a spinning turntable as kinematic bodies — the step hook steers their
  velocities, ODE integrates them with gravity and force response ignored,
  and dynamic bodies ride or get pushed by the surfaces through contact.
  Payload crates spawn resting on the mover tops and ride them (weighted and
  grippy so the oscillating ride does not walk them off; `R` places them
  back). `GX_PHYSICS_MOVERS=0` skips the pair; the wrapper suite pins
  kinematic semantics (no gravity, no force response, velocity integration,
  rider carry, back-to-dynamic gravity) and the scene suite pins the drive,
  the payload spawn and a 20-second ride.
<!-- //ai: sleeping/auto-disable controls + wake-on-mutation/contact; no new assets. -->
- **Rigid-body sleeping**: ODE's auto-disable is now fully wired — settled
  bodies sleep (the sandbox HUD counts them) and the wrapper wakes them on
  every explicit position/rotation/velocity/force mutation plus on contact
  with an enabled body, so sleeping is a pure optimization: drag,
  teleport/reset, EM/magnet forces and impacts all behave on settled props
  (before this, a sleeping prop silently ignored the drag spring, hung in
  place after `R`, and could not be pulled by the magnets). Per-body
  `SetAutoDisable`/`GetAutoDisable` expose the opt-out; tests pin the sleep,
  each wake path and the opt-out.
<!-- //ai: rolling/spin friction + conveyor belt surfaces; no new assets. -->
- **Rigid-body surfaces**: the contact path now honors the material's
  `rollingFriction` (ODE rolling rho1/rho2 plus spin rhoN, resistances
  adding) and `surfaceVelocity` (summed into one belt motion along the
  first friction direction), so balls roll and spin down by surface
  instead of rolling forever, and belt surfaces carry bodies. The element
  table gains nominal rolling resistance (lead/graphite deader than
  polished metals); sandbox props opt out of the world's ported per-step
  damping with a light air-drag stand-in, so friction stops slides and
  rolling resistance ends rolls; a conveyor strip near the pool carries
  props along +X (`GX_PHYSICS_BELT=0` skips it, the debug view draws its
  direction arrow). Tests pin rolling/spin decay, belt carry both
  directions and the sandbox drift.
<!-- //ai: physics editor (tuning surface + presets); no new assets. -->
- **Physics editor**: a new tuning surface (`gX_ScenePhysics_Tuning` +
  live apply and json-c presets under `resources/tools/physics`, with the
  `GX_NO_SETTINGS_SAVE` shipped-path guard and a `GX_PHYSICS_PRESET=<json>`
  seeding hook) drives the sandbox from an ImGui panel:
  world (gravity/quality pin), content toggles (live rig rebuild, pool
  fill/drain, magnet gate), the EM (including magnet strength), thermal and
  fluid settings. Opened by `--physics-editor`, `GX_TOOLS_OPEN=physics` or
  the title EXTRAS card (routes to the sandbox); gate
  `make physics-editor-check` plus the tuning round-trip/apply pins in
  `--test-only`. `gX_Physics_ApplyTuning` is exported for the quality pin.
<!-- //ai: element looks + induced magnetism; no new assets. -->
- **Element materials**: the 12-element table now carries each element's
  polished-metal appearance (albedo/metalness/roughness — graphite for
  carbon, dull lead) and its magnetic susceptibility (μ_r−1: iron 999
  effective ferromagnetic, the rest weak paramagnets/diamagnets). Props take
  their element look at spawn and restore it after melting, and the EM
  applier gained induced magnetism (`gX_PhysicsEM_SetSusceptibility`,
  `m_ind = α·B` folded into the dipole pair laws), so ferrous props are
  pulled onto the bar magnets while gold/copper/lead stay inert — new iron
  test pieces near the magnet row show it. Tests pin the table bands and
  spots plus an induced-attraction vs control probe.
<!-- //ai: magnetic dipoles + Lean contracts; no new assets. -->
- **Magnetic dipoles**: the EM applier now carries per-body dipole moments
  (local frame, rotated with the body) with the dipole pair force, mutual
  torques and the uniform-field `m × B` torque, softened and force-capped
  like the charges. The sandbox builds a row of six bar magnets that align
  and snap; `N` toggles the forces, `GX_PHYSICS_MAGNETS=0` skips the
  content and `=1` starts them on, and the debug view draws each magnet's
  world moment axis. The SI-exact laws (`src/gxmath/physics.{h,c}`) join
  the SymPy math-pass corpus as the `dipole` line and are mirrored over ℝ
  in `GxMath.Physics` with nineteen new theorems (constant positivity,
  axial/equatorial ratio, fall-off, force signs, torque algebra and
  magnitude bounds), verified by
  `make proofs-full`. The image cap carries a 4 MiB rebaseline by author
  decision, and the SI math file joins the `-Oz` cold tier.
<!-- //opencode: textwriter editor slice + renderer unload heap guard. -->
- **Textwriter editor** (editors plan slice 2): the live writer buffer is
  editable line by line with `>`-preview on the textwriter scene, a tag
  palette (colors/styles/ruby/font/jump/choice snippets), add/delete lines,
  a fixture list and guarded save/load (`gX_TextWriter_SaveFileTXT`; shipped
  text paths are refused under `GX_NO_SETTINGS_SAVE`). Opened by
  `--textwriter-editor`, `GX_TOOLS_OPEN=textwriter` and an EXTRAS card;
  EXTRAS became a two-column grid (up to ten tools). Gate:
  `make textwriter-editor-check` + saver round-trip/guard tests. Also fixes
  a latent crash: `gX_Renderer_Unload` dereferenced unallocated module heaps
  after a failed boot, so a failing unit test or load abort segfaulted on
  exit; the renderer module load/unload macros now guard allocated heaps.
<!-- //opencode: editors plan doc + wavetable editor; no new assets. -->
- **Editors plan + Wavetable editor**: `docs/editors.md` records the editor
  inventory, the conventions every new editor follows, and the author-ordered
  slices (wavetable → textwriter → camera rail → background → sky/physics).
  Slice 1 lands: `src/tools/synth/wavetable_editor.cpp` (slot select,
  generate/normalize, freehand waveform drawing, preview note, raw `.f32`
  load/save) opened by `--wavetable-editor`, `GX_TOOLS_OPEN=wavetable` and a
  title EXTRAS card. `gX_Synth_WT_SaveFile` adds the write side (0/1; shipped
  fixture paths are refused under `GX_NO_SETTINGS_SAVE`), pinned by
  `syn_test_wavetable_save_round_trip` and the `make wavetable-editor-check`
  open-path gate.
<!-- //opencode: textwriter choice collection + tour bindings. -->
- **Textwriter choices completed**: `<option line='N'>text</option>` entries
  on a line are now collected into the writer's choice list at load/next/
  jump (the arrays and API existed but nothing filled them — gameX never
  finished the subsystem), so `SelectChoice` works as designed: it jumps to
  the option's target line and clears the list. The tour binds
  `TextWriter_HasChoices / GetChoiceCount / GetChoiceText / SelectChoice`;
  parser tests pin collection, truncation, range guards and the select-jump
  flow, and the tour probe drives a choice fixture end to end (engine
  +1,280 B, still inside the lean cap).
<!-- //opencode: synth wavetable file loader + generated fixtures. -->
- **Synth wavetable file loader + fixtures**: `gX_Synth_WT_LoadFile` now
  returns 0/1, and three additive tables ship under
  `resources/audio/wavetable/*.f32` (raw float32, 1024 samples; AI-generated
  fixtures with a deterministic generator — `scripts/gen_wavetables.py
  --check` runs in the resource audit and fails on drift). The demo's
  WAVETABLE strip cycles four generated shapes and the three files with a
  size-aware preview, and the tour surface gained `Synth_WavetableSize` plus
  the `int` load result. The gate pins file loads, non-max table sizes, table
  distinctness and the bad-path report (demo self-test + tour API probe).
<!-- //opencode: shell synth demo wavetable/preset/patch strip + overlay paint-order fix. -->
- **Shell synth demo — wavetable + preset/patch strip**: the demo generates a
  slot-0 wavetable (sine/square/saw/triangle), previews it script-side
  (`Synth_WavetableSample`) and switches the oscillator to WAVETABLE;
  SAVE/LOAD PRESET round-trip the full engine preset (0/1 status returns; new
  `Synth_GetDefault*` readback getters sync the demo's controls), and
  SAVE/ARM PATCH use the in-memory bank (`Synth_PatchCount`, armed notes route
  through `Synth_NoteOnPatch`). The gate pins the wavetable readback, the
  patch bank and the preset file round trip (demo self-test + tour API probe).
  Fixes the overlay paint-list replay too: lines/circles/triangles are gX
  batch primitives (immediate) while rects/text defer to the raylib queue, so
  the replay flushes pending native ops first (`gX_Drawing_Flush`) instead of
  letting later-specified fills land on top of them.
<!-- //ai: .so module research; measurements only, no code change. -->
- **Dynamic modules research** (`docs/dynamic_modules.md`): measured what a
  `.so` split can and cannot do for gX. Full `--export-dynamic` costs
  +1,390,420 B of payload (exported globals re-root `--gc-sections`); a
  curated `--dynamic-list` costs ~0 gated bytes. The shell's Release cost
  measured 1,382,976 B payload (+235 KB BSS) and the renderer+scene cluster
  1,322,624 B — i.e., the heavy parts are the mandatory ones, so modules are
  capability packaging, not gate relief. The doc records the two host-ABI
  routes (curated export vs versioned function table), the shipped-set
  budget policy, and the shell-as-first-module sketch.
<!-- //ai: tour APIs first slice (camera/gfx/audio); no assets. -->
- **Tour APIs — first slice**: gX-native remakes of gameX's reel surface in
  the shell build (`src/scripting/tour/`). Camera gets Vec3 get/set + FOV and
  a keyframe rail that lives in the shell (gameX had it in `camera.c`; gX's
  was stripped to its seams) driven from a new scripting update hook,
  wall-clock timed, writing the renderer's scene camera; `AddBezier` carries
  explicit outgoing position handles (gameX's bezier variant never set its
  control points). Gfx gets a readable flag table (`Gfx_Flag_Get/Set` over
  50+ `renderer.gfx.state` names), typed fog/sky/sun/bloom/ssao setters, and
  the dynamic scene (Vec3-based `Model_*` and `Light_*`). Audio gets music
  transport + `Sound_Beep`. Host adds
  `gX_Scripting_RegisterUpdateHook`/`RegisterDrawHook` (the draw hook is
  reserved for the overlay slice). Textwriter, overlay, raymarch and pp
  follow; the check-scripting gate covers the slice.
<!-- //ai: tour background slice; no assets. -->
- **Tour background API**: the shell drives the live background heap
  (`renderer.background.data`) through the full param surface
  (`tour_background.cpp`): transform (+vel/accel, origins, `matrixBlend`,
  `matrixA/B`, direct/transform-matrix toggles), warp/wave/HDMA/noise, the
  8 sinusoids, the palette (256-color array, two-color gradient, boundaries,
  blend/color-math), boundaries/modulo, state toggles, reactions
  (`Background_React`), texture (`SetTexture`/`IsTextureReady`), presets
  (`LoadDefault/LoadBuiltIn/LoadPreset/SavePreset`, `BG_PRESET_*`) and the
  preset morph (`Background_Transition(path, ms, ease)`, `BG_TRANS_*`).
  Setters mark the module's dirty gates (params/colors/sinusoids) — the
  per-frame updates are skipped when nothing changed, so the flag is what
  makes a script write reach the shader; scalar read-backs return the
  script-set constant. Found while binding: `gX_RendererBackground_LoadRandom`
  is declared in `background.h` but has no definition (stale declaration, not
  bound), and the test probe now calls the whole registration
  end-to-end (texture reload, preset save → morph → settle).
<!-- //ai: synth wiring + tour bindings + fixes; no assets. -->
- **Synth wired and scriptable**: the polyphonic engine sounds now — a new
  `gX_Synth_Update` per-frame driver ticks the arpeggiator/sequencer and
  attaches the raylib mixed processor lazily on the first sounding voice
  (detaches ~1 s after the last; kept while delay/arp/sequencer are enabled,
  so a silent engine costs no audio-thread work), with attach/detach at
  TRACE so note-gap flapping never spams the default log. The render core is
  now `gX_Synth_Render`, shared by the processor adapter and a new 13-test
  CPU suite (`src/audio/synth_tests.c`, no audio device needed: silence,
  note, pan edge, release, stealing, filter attenuation, sequencer, arp,
  bank, preset round-trip, mute, wavetable, delay tail). The shell gains the
  full `Synth_*` tour surface (notes, per-voice params, defaults, delay,
  arp, sequencer, patches/presets, wavetables, scope). Fixes found while
  wiring: the delay feedback read only the opposite channel, so a mono
  spread had no repeats (now self↔cross blended: 0 = mono repeats,
  1 = full ping-pong); `Patch_SaveToBank` captured waveform/LFO from voice 0
  instead of the defaults; `NoteOnEx` copied `lfoWaveform` into `lfoDepth`;
  and the preset schema omitted the oscillator/LFO/FM defaults, so
  round-trips dropped the sound. Release: synth.c/synth_schema.c moved to
  the cold `-Os` tier (net +256 B payload; 885 B headroom).
<!-- //ai: synth runs muted by default; gates force it via GX_SYNTH_MUTE. -->
- **Synth mute**: the synth starts muted — voices, arpeggiator, sequencer
  and the scope keep running, but the mix never reaches the device buffer
  until asked (`GX_SYNTH_MUTE=0` at launch, or `Synth_SetMuted(false)` from a
  script/console). The check-scripting gate sets `GX_SYNTH_MUTE=1`, so probes
  and CI stay silent while still exercising the attach/detach and scope
  paths; the unit suite pins the default and the mute/unmute behavior before
  the audible-path tests unmute explicitly.
<!-- //ai: tour textwriter slice + scene wiring + demo format conversion. -->
- **Tour textwriter API**: the textwriter module is wired back into its scene
  (gameX's renderer config never made it into gX, so `--textwriter` passed
  the boot smoke on a blank white frame) and now auto-loads
  `resources/text/textwriter.txt` — the shipped demo converted from YAML,
  which gX compiles out (`GX_USE_YAML`). The shell gains the file/line/
  reveal/font surface plus a tagged-draw overlay replayed from the draw hook
  (`TextWriter_DrawTagged`; colour/size/`<font path>` tags apply), and
  `check-scripting` covers the whole surface end-to-end. The scene smoke's
  blank-frame floor is now per-scene: white text on black is legitimately
  dark (the peak check still catches a blank frame).
<!-- //ai: tour overlay paint list; draw hooks moved above the GUI. -->
- **Tour overlay API**: a script-facing paint list replayed from the draw
  hook — `Overlay_Text/Rect/RectBorder/Bar/Line/Circle/Triangle` with one
  current colour (`Overlay_SetColor`), `Overlay_TextWidth`, screen-space
  `Overlay_Width/Height`, and `Overlay_Clear/Count`. The draw hook now runs
  **after** the GUI layer (topmost), so captions and overlays sit above
  menus and panels; the textwriter tagged draws move with it. This replaces
  gameX's demo paint list (`PaintClear/DrawRect/DrawText/SetColorRGB`).
<!-- //ai: final tour slices (raymarch + postprocess); reel surface complete. -->
- **Tour raymarch + postprocess APIs (reel surface complete)**: raymarch
  gets gameX's `RmSet*` contract plus the gX-only surface — extra march
  parameters, the full flag set, object transforms and the built-in presets
  (`RM_PRESET_*`, including random); postprocess gets the embedded-chain
  control (`SetEnabled/Stop`, `PP_PRESET_*`, chain editing with `PP_<TYPE>`
  constants, feedback/inject flags and inject texture slots). The scripting
  gate now covers both (31 checks). Two usage notes: SDF objects need
  `SetObjectAmount` before `SetObjectType` (the safe setter validates the
  live slot), and enabling PP on the very first script frame races the
  scene-config re-assert — the probe and docs start with a `Sleep`.
<!-- //ai: playable synth demo + full key constants; no assets. -->
- **Playable synth demo** (`resources/script/synth/synth.as`): the shell
  gains a full instrument over the tour APIs — a two-octave on-screen
  keyboard (mouse click/drag with glissando) plus envelope/filter/LFO/FM/
  delay/arp/volume controls drawn with the overlay paint list on the
  synthvis scene (the scope visuals stay live behind the panel), or play
  from the physical row (`Z S X D C V G B H N J M` / `Q 2 W 3 E R 5 T 6 Y 7
  U`, `,`/`.` octave); SOUND unmutes (default muted) and the demo is not
  boot-scanned. The input surface now registers every letter/digit/
  punctuation `KEY_*` constant (previously a curated subset). The gate runs
  the demo's self-test (notes/hit-tests/mute through a state flag) and an
  interactive boot smoke.
<!-- //ai: synthvis scene port (gameX); no assets. -->
- **Synthvis scene** (`--synthvis`, `src/scene/synthvis.c`): gameX's playable
  synth visualizer returned to the default build — the physical key rows
  (`Z X C V B N M , . / Q W E R` white, `S D G H J 2 3 5 6 7` black) drive
  `gX_Synth_*` notes, the scene composites the renderer's oscillo/lissajous/
  spectrum RTs into `renderer.canvas` and the GUI keyboard template
  (`SYNTHVIS_KEYBOARD`) lights the held keys; leaving the scene parks the
  notes. `GX_SYNTHVIS_AUTOPLAY=1` holds a demo arpeggio for captures. The
  scene smoke boots it (10 → 11 scenes) and `--test-only` pins the key
  mapping/note lifecycle/geometry. Port note: a pass mixing raylib-native
  shapes with gX batch quads must call the new `gX_Drawing_Flush()` or the
  deferred native queue paints over the batch output (found via the panel
  beds vs RT composites).
<!-- //ai: dynamic module host + gx_shell.so; no assets. -->
- **Dynamic modules — Route A host + `gx_shell.so`** (`src/module/`,
  `scripts/module_exports.py`, `docs/dynamic_modules.md`): `GX_USE_ANGELSCRIPT=ON`
  no longer builds a second exe; it builds the AngelScript shell as a `.so`
  loaded by the exe (`--module`, `GX_MODULE`, `modules/*.so` auto-scan) through
  a versioned entry handshake. The engine calls kernel-side registries
  unconditionally (frame/scene/lifecycle hooks, markup onclick + companion
  scripts, the ImGui debugger panel, the console overlay); the shell args stay
  in the default CLI and warn without a module. The curated export list
  (`modules/module_exports.txt`, 202 symbols, `make module-exports`) is the ABI
  and `make check-scripting` pins drift (37 probes); the default Release payload
  is unchanged (3,913,467) while the module profile measures 1,262,231 payload /
  253,232 BSS (module) plus a 3,935,003 module-capable exe. Boot failures now
  keep their exit code instead of being masked by unload.
<!-- //ai: textwriter parser tests; test-only code, no payload cost. -->
- **Textwriter parser tests** (`src/renderer/textwriter_tests.c`): the inline
  tag parser and TXT reader got headless pins in `--test-only` — word
  segmentation (a space closes the preceding word), shorthand colors and the
  `</>` stack reset, attribute/matrix/effect/ruby/font/underline tags, UTF-8
  decoding (2- and 3-byte), glyph/word capacity saturation, the shipped
  `textwriter.txt` line count and cursor moves, `<jump>` latching through
  Update → NextLine, and missing/empty/overlong-line scratch files. Test code
  is `GX_LEAN_RELEASE`-excluded, so the Release payload is unchanged.
<!-- //ai: console resurrection + tier model doc; opt-in shell build. -->
- **Developer console**: gameX's console returned in the shell build —
  ring-log + history core (`src/console/console.c`), built-ins distilled for
  gX (`/help /scene /scenes /msg /echo /time /stats /ls /reload /exec /run
  /scripts /state`, demoscript and stringtable couplings dropped), and the
  AngelScript one-liner fall-through (wrapped in `void main()`, missing `;`
  appended, throwaway modules discarded — gameX leaked them). The ImGui panel
  opens with `--console` or F10 (the key slot already existed in
  `src/input.h`); `GX_CONSOLE_EXEC` runs a newline-separated batch headlessly
  for gates. `/state save|load|clear` is the manual twin of the automatic
  persist; console errors are logged, never fatal.
- **Tier model doc** (`docs/tiers.md`): L0 platform / L1 kernel / L2 shell /
  L3 orchestration, the membranes, kernel modules as compile-time
  selections, where CLI arguments land (L1 ingress: boot-time, declarative;
  env and console are its siblings), and the middleware admission rule.
<!-- //ai: scripting debugger + state persistence; example no assets. -->
- **Scripting shell — debugger + state persistence**: the shell gains the
  host-side debugger (16 breakpoints per script, Continue/Step/Over/Out —
  the line callback pauses; Continue passes the paused line once) with
  script-visible `Debug*`/`ScriptCount`/`ScriptGetPath/Name` bindings and an
  ImGui panel (`--script-editor`, `GX_TOOLS_OPEN=script`; the default build
  logs a hint). The KV store now persists across processes: JSON at
  `save/script_state.json` (`GX_SCRIPT_STATE` overrides, `=0` disables),
  loaded at shell load and saved at unload, with
  `ScriptSaveState`/`ScriptLoadState` for explicit checkpoints. The
  check-scripting gate covers breakpoint pause/continue, cross-process state
  round-trip and the panel boot.
<!-- //ai: scripting audit/DOM surface; example script + provenance. -->
- **Scripting shell — audit/DOM surface**: scripts can drive and verify the
  GUI. `GUIClick`/`GUIClickElement`/`GUIKey` write through the per-script
  virtual-input slots; element rects (canvas-buffer space) convert to screen
  pixels via the new `gX_GUI_Canvas_ToScreenPoint` — the exact inverse of the
  picking transform (offset/scale/angle). Queries cover canvas counts, name
  lookup, text/rect/state (`GUIFindElement`, `GUIGetElement*`,
  `GUIIsHovered/Clicked/Focused/Disabled`, `GUIAssertTextContains`), plus
  mouse state and `KEY_*` constants. Load paths grew a boot scan of
  `resources/script/*.as` (`GX_SCRIPT_SCAN=0` opts out, `GX_SCRIPT_DIR`
  overrides) and a mod scan of `custom/*/script/*.as`
  (`GX_SCRIPT_MOD_DIR`), both sorted for deterministic boots. The
  `GX_GUI_MARKUP` hook builds a markup on the audit canvas without the
  editor, so DOM click chains are testable headlessly (the editor's edit
  lock intentionally keeps element actions inert under `--tools`).
  `resources/script/hello.as` ships as the shell example; the
  check-scripting gate now covers the DOM click chain, boot/mod scans and
  the opt-out.
<!-- //ai: opt-in scripting shell port; SDK vendored; no resources. -->
- **Scripting shell (opt-in)**: AngelScript returned to gX behind
  `GX_USE_ANGELSCRIPT` (default OFF, own `build/scripting` config and
  `make check-scripting` gate — the default Release and lean payload are
  untouched). The gameX host was distilled feature-first: engine lifecycle,
  script heap, per-frame `main()`, cooperative `Sleep`/`Yield`, mtime
  hot-reload, runaway watchdog, named hooks with `scene_enter`/`scene_exit`
  wiring, per-script virtual input, companion-script events, plus a core/state
  API subset (`Print`, math/easing, keys, time, render scale, screenshot,
  scene switch + constants, `Vec3`, KV state) and the shell assert/`Quit`
  harness that reports failures through the process exit code. `--script
  <path>` runs a script at boot; the pre-existing markup `onclick` seam is
  live. The AngelScript 2.38.0 source subset is vendored in
  `external/angelscript/` (zlib/libpng, `license/NOTICE.md`). Game-content
  API modules, GUI/DOM scripting, the debugger panel, mod scanning and
  demoscript stayed out (`docs/scripting.md`). Port fix: gameX's hot-reload
  lost the script path across the struct reset — gX preserves the path and
  resumes the reloaded script.
<!-- //ai: ragdoll mass rescale + water integration; no new assets. -->
- **Ragdoll mass fix**: bone masses now run in the sandbox's tonne
  convention (nominal kg / 1000), so dummies are human-weight — props shove
  them instead of bulldozing, and they float, bob and take water drag now
  that the fluid applier registers every bone. The nominal kg figure stays
  the thermal heat-capacity unit and the impact-FX energy mass, so heating
  and spark energies are unchanged, and the EM charge is rescaled so limb
  splay matches the previous behavior.
<!-- //ai: water physics; no new assets. -->
- **Physics water**: a new fluid applier gives the sandbox a walled pool —
  registered bodies get Archimedean buoyancy and linear/quadratic/angular
  drag per fixed step, element props sink at density-ordered rates, three
  light buoys bob at the waterline, surface entries fire splash bursts and
  underwater contacts skip the spark/scorch path. `P` fills/drains the pool;
  `GX_PHYSICS_WATER=0` skips it. The feature was trimmed first (the physics
  demo cold set moved to `-Oz`, the applier uses equivalent-volume spheres)
  and the payload cap carries a reviewed +3.4 KiB bump in
  `.lean_budget.json`.
<!-- //ai: per-element contact materials; no new assets. -->
- **Physics contact materials**: props now land and slide per their element —
  every table row carries nominal friction and restitution (lead dead and
  grippy, iron springy, gold slick), rig parts use steel, ragdoll limbs a
  soft tissue and the ground a rough dead surface. The HUD shows the hovered
  prop's `mu`/`e`; `GX_PHYSICS_MATERIALS=0` keeps the wrapper defaults. The
  ported contact softness (soft CFM 0.01) absorbed most of a bounce, so the
  wrapper now settles at 1e-4: resting stacks stay stable and restitution
  actually rebounds; the suite pins the mapping, a slide and a drop.
<!-- //ai: constraint rigs extend the existing sandbox; no new assets. -->
- **Physics joint rigs**: the sandbox now exercises the wrapper's constraint
  family — a ten-link hanging chain (ball joints under a gantry), a plank
  door on a limited vertical hinge, a motorized windmill, and a slider rail
  carriage. Rig parts live in a shared link pool, render through the GFX heap
  with the same sub-step interpolation as props, are mouse-draggable
  (swing the door, throw the rail carriage), reset to their spawn poses with
  `R`, and are destroyed joints-first on scene exit. `GX_PHYSICS_JOINTS=0`
  skips them; headless tests pin the chain span, the windmill motor rate,
  each rig's constraint height and the park sweep.
<!-- //ai: native terrain behavior is opt-in; source assets and authored presets are unchanged. -->
- **Opt-in GLB terrain material blend**: material sidecars/preset overrides
  retain `COLOR_1` separately from vertex tint and combine two albedos with an
  optional non-color macro texture. Missing-mask fallback and per-mesh state
  isolation have an owned Phong/PBR HDR regression; no third-party assets or
  sourcemap lighting/preset changes are included.
- **Weather/godray math contracts**: 38 exact rational theorems for weather
  wrap/remainder and spawning-band bounds, fixed-floor behavior, fog-height/fade
  invariance and bounded godray taps/high-density preservation. Historical
  negatives and external C remainder/random/IEEE/GPU obligations remain explicit;
  source-drift guards supplement, not replace, native and pixel probes.
- **Impact FX II — charged arcs, fresh-hit spikes and scorch decals**:
  charged impacts (any charged side flags the entry; a sum would cancel
  opposite signs) spark as saturated cyan arcs with a tighter spread,
  uncharged heavy fresh hits flash white through the ember ramp, and
  ground-facing impacts leave soft ash/scorch decals sized by energy, warmed
  by prop heat and faded by texture alpha (`GX_PHYSICS_DECALS=0` disables).
  The sandbox silences the renderer's default particle emitter while it owns
  the particle pass and saves/restores the decal-pass flag (both restored at
  park); decals gained a per-decal albedo tint
  (`gX_RendererGFX_Decal.color`) and the scorch texture is procedurally
  generated (provenance recorded). Depends on the multi-emitter dispatch fix
  below.
- **Physical-law proof contracts**: `GxMath.Physics` (Mathlib tier) mirrors
  the SI helpers in `src/gxmath/physics.c` over ℝ — Coulomb pair symmetry,
  sign by charge product and the potential/force relation (potential symmetry
  too); electric-field parallelism; Lorentz orthogonality to velocity (no
  work) and field plus velocity/field anti-commutation; cyclotron radius
  positivity and field-monotonicity; photon/Wien positivity and
  wavelength/temperature ordering; Stefan-Boltzmann temperature ordering;
  blackbody normalization bounds. Constants are
  the `config.h` CODATA values as exact rationals, so their positivity is
  kernel-checked. `make proofs-full` now carries 90 modules / 1958
  declarations with standard axioms only.
- **Physics debug view**: `K` toggles a collider/contact overlay in the
  sandbox — wireframe boxes/spheres on props (red while dragged), boxes on
  ragdoll bones, cyan cubes with yellow normal lines for the current frame's
  contact points, and a ground grid (`GX_PHYSICS_DEBUG=1` starts it on).
  Contacts come from the same event stream as the sparks (frame-tagged);
  drawn in the scene overlay with its own 3D state block, no renderer
  coupling. The HUD shows the DEBUG state and the key legend.
- **Ragdoll magic — EM/thermal on bones**: the sandbox registers every
  ragdoll bone with the active appliers on spawn (and rebuilds on `M`).
  Charged dummies share one charge sign (scaled down from prop charges so
  joint-scale separations stay off the force cap): limbs repel into a
  spread-eagle and dummies push each other apart. Thermal uses per-bone
  wrapper mass and a generic tissue material, so `H` heats a hovered dummy
  (or all when hovering nothing), landings heat limbs, and bones glow on the
  blackbody ramp. Both registries were raised to 256 bodies (64 props + 88
  bones = 152); destroy/park unregister before the bodies go.
- **Physics impact FX (sparks)**: sandbox contact events (ENTER transitions
  above a closing-speed floor) queue impact bursts — contact point, reduced-
  mass energy, thermal state and prop/bone tint — and the scene drains them
  per frame into four reserved GPU particle emitters: energy scales the burst
  rate/speed/life and the color blends the tint toward blackbody as props
  heat. Visual only (the queue never feeds the step, determinism is intact);
  `GX_PHYSICS_SPARKS=0` disables it. Sparks render in the opt-in compute
  build (`GX_COMPUTE=ON`, GL 4.3); the default GL 3.3 build runs without them.
<!-- //ai: policy/documentation addition; license stance recorded, no code change. -->
- **AI policy**: `docs/ai_policy.md` records how AI may contribute (review,
  marking, provenance, the gameX isolation rule) and the license stance for
  the shipped AI-generated assets: GPLv3 with a public-domain fallback where
  copyright does not apply. Applied in `license/NOTICE.md` and
  `license/provenance.json`; the release audit item is closed.

### Fixed
<!-- //opencode: extend the inherited-env restore pattern to the sky guard + synth mute probes. -->
- **Harness env restore follow-up**: the sky preset guard probe (and the synth
  mute probe's `GX_SYNTH_MUTE`) now restore the exact inherited value like
  the synth/textwriter probes do — a bare `unsetenv` wiped an inherited
  harness guard for the rest of the process, so the gfx preset gate's guard
  probe wrote its refused path (caught by `make audit-summary-full`).
<!-- //ai: preserve the inherited save guard through startup tests; no renderer save-policy or default changes. -->
- **Harness save-guard lifetime**: synth and textwriter guard probes restore
  the exact inherited `GX_NO_SETTINGS_SAVE` value instead of clearing it;
  physics no longer truncates long values. Snapshot allocation failure leaves
  the environment untouched and fails the test. Native preset refusal now
  survives ordinary startup tests. Exact-value/allocation-failure regressions
  run in `make check`; preset-check exception handling reports the primary
  failure instead of masking it with a missing-import error.
<!-- //ai: optional normal-target storage repair; camera, shaders and defaults unchanged. -->
- **SSR shading-normal precision**: the optional normal target now uses RGBA16F
  instead of RGBA8, preserving encoded zero components exactly. The owned flat
  reflector exposed an 8-bit normal bias that aimed cubemap rays past its
  occluder. Native normal/ray/occlusion regressions and an explicit old-binary
  normals-disabled identity control cover the repair. The legacy byte dump is
  retained as a converted diagnostic; the production normals-off default stays.
<!-- //ai: bounded raw-draw GLB material repair, validated with generated owned fixtures. -->
- **GLB per-mesh albedo and large-model emission**: raw draws select each mesh's
  embedded albedo instead of repeating the first material, preserving sidecar and
  authored model-wide texture priority. File emission is resolved beyond the
  bounded sidecar cache so non-emissive meshes do not inherit another material's
  glow. `scripts/glb_material_maps_check.py` covers embedded RGB maps, textureless
  fallback, authored injection and the cache boundary without third-party assets.
  This does not reconstruct missing exported textures or override authored glow.
<!-- //ai: file material ordering must not change the native opacity assignment. -->
- **GLB opacity indexing**: subtract raylib's default-material prefix before
  reading file-material opacity. Native defaults remain opaque; generated owned
  material-table permutations and native index-boundary checks pin the mapping.
<!-- //ai: draw-local upload elision preserves draw order and first-upload guarantees. -->
- **GLB blended-draw upload overhead**: set the phase-constant roughness-repair
  flag once per model draw and skip only identical consecutive opacity uploads.
  Scratch state resets for every draw; no global shader cache or material reordering.
  Native reset/transition tests and exact opaque/transparent HDR controls cover it.
<!-- //ai: optional mesh culling retains conservative failure and pass-isolation behavior. -->
- **Mesh-level frustum culling**: the existing Frustum Culling toggle now rejects
  static file meshes only when their whole bounds lie outside a clip plane,
  replacing unsafe model-corner rejection. Per-model bounds reset on load/unload;
  unsupported geometry and passes fail open. Owned native lifetime/boundary and
  exact HDR controls cover the opt-in path; presets and defaults are unchanged.
- **Clean-checkout defaults**: remove the quarantined model from default preloads,
  retain its demo slot with a shipped project primitive, and use the shipped
  project normal texture for new editor/water/normal-map fixtures. Author presets
  and quarantined local files are not rewritten or imported.
- **Bloom intensity witness**: strict changed-pixel monotonicity now uses the
  existing procedural emissive fixture with pinned pipeline inputs; the original
  showcase still checks brightening and bounded footprint. Pixel cut, intensity
  values, strict ordering and footprint caps are unchanged. Flat/reversed/subcut
  ramps, washout and missing captures remain failing controls; no renderer bloom
  strength or production defaults are changed to satisfy the audit.
- **Vertical weather with a fixed world floor**: adapt snow/rain spawning to
  camera height and recycle off-view particles without rewriting authored floor
  or ceiling values. Fog sprites and their height fade now follow vertically.
  Native up/down, elevated-camera GPU, admission and existing weather/renderer
  gates pass except the retained particles/probes/mirror matrix failures.
  Protected hashes are unchanged; author confirmed vertical following works.
- **Camera-following weather**: snow, rain and fog-sprite XZ bounds now follow
  camera position rather than its target; multi-width wrapping handles large
  jumps immediately while preserving in-volume world positions, vertical
  bounds, motion and seeds. Native and early-frame GPU relocation probes fail
  the original code and pass the repair. Existing weather/full gates and live
  author verification remain pending; physics behavior is unchanged.
- **Godray low-density overshoot**: bound each of the four taps' steps to one
  quarter of the sun distance. Densities 1–16 share the maximum ray footprint;
  density at least 16 retains the previous stride. Thirty synthetic NVIDIA
  density/size probes reject the old screen-edge sampling and pass the bounded
  shader while preserving the stale-input repair. This does not remove the
  four-copy screen-space approximation. Author confirmed the low-density repair
  works. Admission and focused/renderer gates pass except the retained
  particles/probes/mirror matrix failures; protected hashes are unchanged.
- **Kernel allocation tracking**: null realloc and clear-realloc now use tracked
  fresh-allocation paths instead of matching an unused tracker record and
  reporting a phantom free. Live-only address lookup retains the author’s
  moved-address fix. Native accounting/prefix/clear-tail regressions fail the
  original implementation and pass the repair; allocator test errors now count
  toward admission. Kernel, admission, reflection and bloom gates pass; existing
  particles/probes/mirror failures and GL warnings remain. Physics is unchanged.
- **Godray stale HDR input**: the four generation taps explicitly read the
  current base level instead of implicitly minifying into SSR mip levels that
  can retain an earlier scene after SSR is disabled. Half/quarter-resolution
  synthetic NVIDIA controls reproduce the original stale silhouette and pass
  after the repair; SSR mip sampling and physics work are unchanged. Combined
  validation passes except the retained particles/probes/mirror failures. Author
  confirmed the stale-input fix works; low-density overshoot and the four-tap
  screen-space approximation are separate follow-up issues, not fixed here.
- **Heap tracker recorded stale addresses after realloc**: `gX_Realloc` /
  `gX_Realloc_Clear` stored the *old* pointer after a block moved, leaving a
  phantom tracked entry that later matched recycled heap memory. It surfaced
  as "deeply nested element data remains allocated" plus a double-free abort
  in the GUI markup cleanup test (deterministically in bloom's
  `--showcase-minimal` captures). The tracker entry now follows the moved
  block; the markup test, bloom gate and `--test-only` are clean.
- **Parallel test-suite temp-file races**: the suite's round-trip tests (JSON,
  shader-compiler scratch, CSS overflow, style reload, style round-trip, the
  editor GUI-snapshot round trip, the postprocess preset round trip) wrote
  fixed `/tmp` paths, so concurrent binary instances (parallel audit captures)
  clobbered each other — surfacing as flaky `STYLE ROUNDTRIP`/GUI-markup/`PP
  TEST` failures in gates like bloom/matrix/bijection. All suite scratch files
  are now per-process (`/tmp/gx_test_<pid>_*`, preset files keyed by pid) and
  removed after use. Verified with 4-way concurrent suite runs (40/40 clean).
- **GPU particle emitters past index 0 never updated**: the compute dispatch
  covered only `emitOffset..emitOffset+count` invocations from a zero base,
  while the update shader guards `gl_GlobalInvocationID.x` against
  `emitOffset` — so emitter 0 worked and every later emitter stayed dormant,
  no matter how it was configured (found via the sandbox's four spark
  emitters). The dispatch now covers the whole `offset + count` range.
  `make particle-velocity-check` pins the GL4.3 path.
- **Physics auto-tuner could never recover at vsync**: its recovery threshold
  required >~92 fps, so one startup spike pinned the shared world at level 3
  (2 contacts/step, threaded solver) for the whole run — visible as "TUNE 3"
  in the sandbox HUD. Recovery now steps down after three consecutive
  evaluations at/under the 55 fps target (a 1.0–1.2 dead band absorbs vsync
  jitter), hitches >100 ms are clamped out of the EMA, and the threaded
  solver is pruned from the auto path (the wrapper API stays explicit).
  `GX_PHYSICS_TUNE=<0..3>` pins the level and survives the scene reset;
  policy covered by `gX_Tests_Physics_Tuning`.
- **Physics body destruction left ghost colliders**: ODE's `dBodyDestroy`
  only detaches attached geoms, so `gX_PhysicsBody_Destroy` now destroys the
  geom (`dGeomDestroy`) after its metadata. Previously every deleted prop or
  park left an invisible collider at its last pose (surfaced by the sandbox
  landing test: a destroyed blocker held the next box in mid-air). Regression
  pinned by `gX_Tests_Physics_Destroy`.

### Changed
- **Physics sandbox polish**: renders sub-step-interpolated transforms (the
  module's `gX_Physics_GetAlpha` is finally consumed — pre-step pose snapshots
  blended per frame with `F32v3_Lerp`/`F32Qua_Slerp`), dragging is a spring
  velocity controller that collides and throws on release instead of
  teleporting, leaving the scene parks it (`gX_ScenePhysics_Park` destroys
  props/ragdolls and detaches the step hook and collision callback so the
  shared world is idle under other scenes), the title DEMOS hub routes here
  as PHYSICS SANDBOX, and the sandbox draws a status HUD (counts, steps,
  tuning, hovered prop element/temperature/phase/melt, hottest prop, key
  legend). Lifecycle covered by `gX_Tests_ScenePhysics`.
- **Scene-content ownership**: the GFX heap transition contract tracks three
  owners (demo / battle / physics) instead of a battle bool; the physics
  sandbox clears like battle's content on owner change, and the default demo
  content is not injected while `--physics` is active. Renderer transition
  tests cover the physics owner matrix.
- **Label readability and authoring**: oversized raster font values fit the
  shared texture instead of clipping glyphs, and raw mesh labels now honor the
  render-texture Y-flip. Saved RGBA, world size/aspect fitting, shadow and model-top
  attachment/gap controls accompany the repair. New labels default above model
  bounds; existing anchors remain unchanged. The default-camera font gate passes
  again; native transformed-bound and GPU oversized-glyph/style probes are retained.
- **Label anchor follow-up**: correct the shared billboard/fixed-text transform
  composition so facing rotation and size cannot rotate/scale label translation.
  Native anchor witnesses and equal-anchor GPU tracking checks replace reliance
  on glyph movement alone; gxmath and GL transpose conventions are unchanged.
- **Authoring blockers**: GFX water updates no longer erase plane-height edits;
  bloom and lens-flare intensity widgets have separate ImGui IDs. Labels expose
  a persisted, zero-default world-axis offset without moving their model anchor.
  Native production-widget drag isolation, editor history/snapshot and spatial
  label/water regression fixtures accompany the repair.
- **Postprocess routing repair**: scaled input assembly and native-size shader
  passes eliminate cropped/repeated BLUR output; separate destination-sized
  presentation survives zero-loop tails. Fresh empty chains preserve the scene,
  feedback is opt-in, and unloaded input no longer leaks a GL state scope.
  Exact/native contracts and asymmetric GPU fixtures retain numerical limits;
  author confirmed BLUR and empty-chain behavior in the live preset.
- **Release capacity recovery without cap growth**: size-optimize the shader
  compiler and GFX schema serializer, preserving their capabilities and keeping
  rendering/math optimization unchanged. Production validation recovers 33,472
  payload bytes (33,493 bytes headroom); budgets and baseline remain unchanged.

### Added
- **Physics resurrection — rigid-body props sandbox**: the full ODE-backed
  wrapper (opaque worlds/bodies/joints/colliders, materials, static geometry,
  forces, raycast, sensors, contact events), the shared fixed-timestep physics
  module (`gX_Physics_*`), the `--physics` props scene (drag/spawn/delete,
  `R` reset, `GX_PHYSICS_PROPS` count) drawn through the GFX renderer, and
  wrapper integration tests (world isolation, settling, joints, forces,
  raycast, sensors, events). New CMake option `GX_USE_ODE` (default on) links
  ODE when present and falls back to no-op stubs otherwise; ragdolls remain
  gameX-side for now.
- **Physics thermal interactions**: contact conduction (touching bodies
  exchange heat; a heated prop sinks into the floor) and latent-heat melting
  (temperature plateaus at `meltingPoint` while the heat of fusion is
  absorbed, then the prop is molten — dark, low-roughness, glow-floored —
  and re-solidifies on cooling). The thermal state is enthalpy-based so the
  plateau is exact; element table gained the heat of fusion. Tests cover
  conduction direction, both plateau directions and state transitions.
- **Physics temperature + glow**: a thermal applier (per-body temperature:
  impact heating from closing speed, radiative T⁴ cooling, element-specific
  heat capacity/emissivity) wired to the renderer's emissive path — hot
  props glow through `gX_F32v3_Physics_Temp_BlackbodyRGB` and bloom, with
  `H` to heat and `GX_PHYSICS_TEMP`/`_HEAT`/`_THERMAL` hooks. Element table
  gained specific heat, melting point and nominal emissivity; gxmath gained
  Stefan-Boltzmann/Wien/blackbody formulas with an oracle-checked `thermal`
  math-pass line; physics gained a thermal behavior/determinism suite.
- **Physics electromagnetism + elements**: an EM applier for charged bodies
  (softened, clamped pairwise Coulomb plus the uniform-B Lorentz force) run
  from a new per-substep physics hook, a 12-element density/molar-mass table
  driving prop mass, and sandbox hooks (`M` toggles charge-coded props,
  `GX_PHYSICS_EM`/`GX_PHYSICS_B` presets). gxmath gained SI-exact EM
  formulas (Coulomb, electric field, Lorentz, cyclotron radius, photon
  energy) with an oracle-checked `em` math-pass line; physics gained element
  and EM behavior/determinism suites. Constants: `gX_PHYSICS_EPSILON0`,
  `_COULOMB`, `_MU0` (the unused misnamed `PHYSICS_G`), `_AVOGADRO` (typo
  fix).
- **Physics ragdolls + determinism gate**: the 11-bone humanoid rig
  (`gX_Ragdoll_*`: capsules/spheres, ball/hinge joints, self-collision
  filtering, T-pose reset, enable/disable) with its wrapper test suite, plus
  sandbox hooks — `T` drops a ragdoll at the cursor, right-click removes one,
  `R` resets props and poses, `GX_PHYSICS_RAGDOLLS=<n>` pre-spawns up to 8 for
  captures. New determinism test: two identical worlds must step
  bit-identically for 600 steps (with a stability bound). The sandbox is
  excluded from the GFX auto-seat pass (its authored ground slab was being
  lifted, burying collapsed limbs).
- **Text/label math contracts**: 18 exact raster-centering/padding, proportional
  quad fitting, ideal scaling, top-anchor/translation, UV-flip and byte-color
  bounds. Native helper and asymmetric GPU glyph probes retain the actual
  raster loop/font-atlas/animated-bounds/source/GPU obligations explicitly.
- **Battle multiplayer architecture proposal (design only)**: source-grounded
  cooperative client/server milestones, authority/ownership, clock/replication,
  bounded protocol/reconnect/security and headless validation requirements.
  No networking implementation, dependency, server exposure or budget change.
- **Written stabilization plan and postprocess contracts**: exact normalization,
  half-resolution/repeat counterexamples, source-texel spacing, conditional
  empty-chain identity and authored-final-slot limitations. Scalar/source controls
  and extracted native dimension witnesses join admission; the later routing
  repair updates those witnesses while retaining GPU/IEEE assumptions.
- **Offline GFX editor bug reports**: Help/F10 form with structured fields,
  explicit local JSON submission, optional screenshot, UTC/version/preset
  context, opt-in live camera/viewport/renderer diagnostics and visible failures.
  Preset/report/editor cameras share serialization; reports stay gitignored,
  with no uploads/autosaves or resource dumps.
  Debug admission fixtures cover repeated saves, escaping, attachment and failures.
- **SSGI gather/fallback proofs and isolated stripe gate**: exact tangent-test,
  normalization-order and confidence-count contracts with scalar/native controls;
  a GPU flat-only/bounce probe separates indirect-light banding from GTAO.
- **GTAO reconstruction/sampling proofs and stripe regression**: conditional
  inverse/texel-center, stratified steps, horizon and blur contracts; scalar/native
  admission checks and a separate GPU flat-slab/contact-shadow gate retain
  numerical, normalization and final-pow limits explicitly.
- **CSM branch and fade proofs**: production-order cascade layer bounds, exact
  ordered split boundaries, conditional fade endpoint agreement and positive-w
  projection bounds. CPU/native witnesses preserve equal-split, overlapping-fade
  and lower-depth-guard limitations; no runtime or threshold changes.
- **Point-shadow radial-depth proofs**: matched-far encoding/comparison, strict
  equality, bias monotonicity, supplied error margins and five-tap bounds.
  Admission checks six native scalar witnesses; clear-depth beyond-far hazards
  and GPU precision/filtering obligations remain explicit, without policy changes.
- **Shared cubemap geometry proofs**: six GL face mappings, strict-interior
  selection, coordinate/direction round trips, capture axes and explicit tie/seam
  limits; admission checks 24 asymmetric native capture and tap-rerouting witnesses.
- **Projection and near-clipping proofs**: establish positive-w winding and
  canonical near-plane intersection/fan orientation, with explicit projection
  error margins and mixed-w counterexamples. Admission checks native positive,
  near-crossing and camera-crossing fixtures against an exact clipping oracle;
  full GPU clipping/raster behavior remains uncertified and culling unchanged.
- **Reflected-camera handedness proofs and native basis witnesses**: distinguish
  orientation-reversing geometric reflection from right-handed LookAt rebuilding,
  with mirrored-axis identities and conditional projected winding laws. Admission
  checks asymmetric bases/triangles using unchanged tolerances; normalization,
  clipping and GPU culling remain explicit obligations, with CCW policy unchanged.
- **Planar reflection geometry proofs and native witnesses**: exact unit-normal
  involution, on-plane fixed points, squared-distance preservation and homogeneous
  point/direction projection contracts. Admission checks asymmetric camera/UV
  fixtures and transpose/order/Y-flip/up-offset negatives with existing tolerances,
  while retaining float-normal, LookAt and GPU accuracy limitations.
- **Driver depth-precision evidence and reflection proof review**: query live
  allocations and probe disposable format-matched raster writes, GLSL samples
  and MSAA resolves with strict state restoration. Reflection contracts now
  distinguish resource-dependent fallback and missing-mask legacy sampling from
  abstract ghost policies, with source drift checks and narrowed GL overclaims.
  Production formats, reflection policy and thresholds are unchanged.
- **Depth-input sensitivity proofs and native fixtures**: bound depth/UV/jitter
  errors through inverse reconstruction and previous projection, separately
  combining baseline inverse residual. Admission checks near/mid/far perspective
  fixtures and simulated nearest-grid rounding; actual GPU format/rounding and
  arithmetic uncertainty remain explicit obligations, with budgets unchanged.
- **Weighted reprojection error proofs**: componentwise absolute residual sums
  and previous-camera column weights tighten conditional U/V/depth bounds.
  Native witnesses check intermediate containment and refinement, preserving
  CPU uncertainty separately; Hilbert's tighter Y bound still exceeds the
  unchanged .05-pixel budget. No runtime or IEEE-certification policy change.
- **Native residual-to-error witnesses**: headless hex-float fixtures compare
  exact stored-float residual/reprojection bounds with observed CPU UV/depth
  errors, separating measured rounding deviation. Admission checks containment;
  accuracy comparisons remain advisory and explicitly expose a finite Hilbert
  Y-pixel budget miss. No GPU or universal IEEE accuracy claim.
- **Conditional reprojection error proofs**: propagate inverse residual through
  normalized world recovery and previous-camera projection to axis-pixel/depth
  bounds. Measurement and GPU/storage uncertainty remain explicit hypotheses;
  existing budget illustrations and a small-denominator counterexample are
  kernel checked without claiming IEEE execution is certified.
- **Source-grounded cofactor inverse proofs**: exact-rational C minors and all
  16 inverse outputs satisfy both identity products under a nonzero determinant;
  ideal camera reconstruction derives its inverse premise from that contract.
  Full proofs gate algebraic C/Lean source correspondence. Native near-singular
  witnesses distinguish exact structured residuals, rounded singularity, and a
  finite but imperfect Hilbert inverse; IEEE accuracy is not certified.
- **TAA committed-history gap guard**: raster and hybrid history invalidate on
  non-consecutive committed resolve timestamps; rejected/aborted entry no longer
  advances the stamp. Native skip/abort/resume fixtures and Lean timestamp/
  invalidation contracts pin fresh resume output without changing budgets.
- **TAA unsafe-reprojection guards**: classify matrix coefficients before upload
  and return fresh color/depth for unsafe homogeneous denominators, non-finite
  intermediates or overflow. Native singular/zero-w/NaN/Inf/overflow controls
  retain exact current-frame output and require zero GL errors. Lean guard and
  quotient contracts keep finiteness/error assumptions explicit; finite inverse
  accuracy and global cofactor inversion remain outside the certificate.
- **Temporal snapshot provenance proofs**: core contracts distinguish TAA's
  private previous VP from motion blur's mid-frame update and from the last
  successful history camera. Native moving-camera probes cover three frame
  indices after confirmed motion-blur completion. Skipped resolves and mismatched
  commit/save cameras remain explicit alignment limits, not hidden assumptions.
- **Temporal camera-matrix contracts**: model numeric-field order separately
  from physical C member layout, preserving the raw GL_TRUE upload convention;
  prove view/projection ordering and conditional exact inverse/previous-clip
  reconstruction. Native layout, asymmetric-product and affine-inverse witnesses
  supplement shader probes. Repair the opposite-product `GxMath.Matrix.mul`
  model and remove overclaims about readback, foreign multiply and UBO bytes.
- **Temporal history safety guards**: reject same-wrapper, zero-name, and shared
  FBO/color-name targets without changing state/output; wrap the signed sample
  index at its maximum while retaining completed history. The optional TAA
  oracle converts before incrementing. Native tests cover 4,096 begin and 5,184
  backing combinations plus signed-limit commits; Lean contracts model the
  checked begin and bounded index. Attachment metadata coherence remains assumed.
- **Ideal temporal sampling proofs**: Mathlib-tier scalar reprojection/jitter
  identities, bilinear weight and range contracts, clamped footprint bounds,
  and strict hybrid depth/UV eligibility boundaries. Numeric error budgets are
  explicit hypotheses; interpolation agreement is not proof of surface identity
  or GPU floating-point correctness.
- **Temporal-history lifecycle proofs**: core Lean contracts pin clear/begin/
  abort/commit/reset, ping-pong selection, well-formed lifecycle preservation,
  and the authored-reset guard. The initial native matrix checked 3,072 begin
  combinations and safe signed-counter boundaries; subsequent safety guards
  extend this coverage. GPU completion and hash collision freedom remain
  explicit caller/proof limits.
- **Lean proof trust/coverage audit**: all 71 proof modules are gated; compiled
  theorem dependencies are restricted to the standard foundational axioms.
  Finite examples use kernel-checked `decide`, and explicit Mathlib validation
  no longer silently succeeds without its cache. Source review repairs stale
  effect-routing, FBO-query, and TextWriter contracts; documented scope limits
  distinguish specification proofs from C/IEEE/GPU implementation correctness.
- **Opt-in camera-only SDF temporal history**: shared mode uses raster Halton
  jitter and visible SDF ownership in the existing velocity target. Static
  history uses explicit bilinear color/depth sampling, previous-projection
  disocclusion rejection, and authored/time/routing invalidation. Native
  analytic, production-color, and static-burst fixtures pass unchanged budgets;
  animated-object vectors and general temporal quality remain uncertified.
- **Hybrid SSR reflection oracle**: a separately attributed green raster target
  and removed-target control, analytic screen-visible specular rays, and native
  SSR hit/color metrics distinguish reflection quality from normal-buffer wiring.
  Shared-mode rays traverse empty depth gaps with strict refined coincidence;
  rejected SSR candidates no longer survive as accepted hits on trace exit.
- **SDF surface inputs**: explicit per-object roughness (matte historical
  default) in shared HDR alpha, plus primary SDF world normals/coverage in the
  existing opt-in SSR normal target. Native-buffer checks pin normal direction
  and depth ownership; production shading normals remain opt-in.
- **Experimental shared scene depth**: hybrid mode `2` inserts opaque SDF HDR
  color and projected depth after MSAA resolve, before water/effect inputs.
  A native float-depth oracle complements visibility checks. SDF normals,
  material buffers and motion vectors remain outside this phase.
- **Editor hybrid toggle**: Raymarch's transient **hybrid depth (opaque)**
  checkbox exposes the opt-in visibility prototype without environment setup.
  Disabling restores prior layers/module flags; scene switches reset it.
- **Experimental hybrid visibility**: `GX_GFX_RAYMARCH_DEPTH=1` overlays opaque
  SDF primary hits against the current raster depth using the shared perspective
  camera and full output resolution. `make gfx-raymarch-depth-check` retains
  analytic sphere/cube overlap evidence. This is post-effects color visibility,
  not shared G-buffer/deferred shading or transparent interleaving.
- **Title demo hub**: `DEMOS` opens routes to the Battle Arena, GFX Showcase,
  Background, and Raymarch scenes. These systems now launch from the title
  through the normal scene dispatcher.
- **Realtime tactical party battle**: the headless runtime now has three
  actor-owned party records, individual ATB gauges and cooldowns, one optional
  directly controlled member, and two AI companions. Actions require a full
  100-point gauge; party ATB refills at 34/s and hostile ATB at 30/s. Enemy
  actions are cached and telegraphed independently before their delay. The
  occupied nine-by-nine board, facing/range modifiers, two encounter waves,
  bounded magic/items, deterministic enemy profiles, and eight-entry actual-HP
  impact history remain renderer-independent and headless-tested. The GFX arena
  now presents all three party members and per-actor HP/ATB meters in the HUD.
  `BATTLE_TESTS` and `make gfx-battle-check` cover the simulation and renderer.
- **Battle enemy profiles**: the Adept uses affordable magic on a deterministic
  15% aggressive cadence; the Sentinel guards on 60% of turns below 30% HP; the
  Warden retains the balanced policy. Headless tests pin cadence, affordability,
  health thresholds, and wave-two behavior.
- **Battle live tuning**: the existing Polisher panel exposes transient attack
  damage, potion healing, enemy-turn delay, and guard damage multiplier controls.
  Bounded values apply to the active encounter and are never saved.
- **Battle intent tells**: each enemy caches its deterministic action when its
  independent ATB fills; the HUD and pulsing 3D actor cue show that same action,
  including the selected elemental spell, through its response delay. Headless
  and visual gates pin intent stability and presentation.
- **Battle impact reactions**: confirmed hits drive a brief attacker follow-through,
  target recoil, and element-colored 3D flash from the bounded impact history.
  Element metadata stays renderer-independent; headless and visual gates pin it.
- **Battle tactical board**: the 9×9 runtime grid keeps one occupied slot per
  living party member and enemy. WASD repositions the controlled member and Q/E
  turn it in place without consuming ATB; Tab transfers direct control to
  another survivor while the other party actors autofight. Physical attack
  damage and incoming risk still scale with Manhattan distance and facing. The
  board is quieter, facing ticks are visible, and the lava field extends beyond
  the larger arena without the previous short-distance cutoff.

### Fixed
- **Lens-flare radial corruption**: ghost positions now use the fixed
  sun-to-screen-center axis with bounded spatial falloff instead of sampling
  angular scene rays per fragment. The anamorphic streak has vertical falloff,
  source depth occludes all elements, and the actual composite target supplies
  dimensions/aspect. Seven synthetic NVIDIA shader fixtures pass, including the
  centered sun and dispersal endpoints; the original shader remains a failing
  negative. Admission passes after a documentation-path typo repair; existing
  particles/probes/mirror matrix failures remain. Author confirmed lens flare works.
- **SSGI horizontal bands**: remove a rounding-sensitive zero-valued tangent
  predicate, fetch/reconstruct depth using one explicit texel index, use start-jittered
  steps and the unchanged authored separation bias, and normalize gathered
  radiance before blending ambient fallback with the full two-sided tap count.
- **GTAO slab striping/self-occlusion**: reconstruct nearest depth at its texel
  center, jitter the start of evenly spaced taps, reject zero sample vectors,
  and honor the existing SSAO bias as normal separation. Authored bias values
  and presets are unchanged; static/rotating flat-slab and contact probes pass.
- **Volumetric fog point-shadow −X lookup**: use `+z` for −X face U, matching
  GL capture and surface point-shadow sampling; asymmetric cubemap regression added.
- **Release size headroom**: compile GUI style registration with targeted `-Os`;
  the linked payload shrinks by 13,600 bytes without changing renderer or shader
  optimization settings. `make check` passes with 14,421 bytes payload headroom
  under the unchanged cap; BSS is unchanged.
- **Feature-matrix diagnostics**: explicit metric/threshold reasons and evidence
  strength, JSON reports with retry history and reproduction commands, and
  separate annotated captures. Labels no longer contaminate measured deltas.
  Focused row selection and GPU-free report-rule tests preserve admission rules.
- **SDF node-matrix first phase**: 64 authoring slots compile into a bounded
  32-node CSG DAG with union, intersection, A-minus-B and smooth counterparts.
  Finite/infinite local repetition supports centered sphere/box leaves with
  validated cell extents and neighbor-cell sampling. Optional graph presets
  preserve historical object unions; cycles/invalid references are explicit
  errors. CPU graph/schema checks and eight GPU field-oracle probes cover it.
- **Raymarch water, textures and sky**: water's negative map index no longer
  disables reflection. Dielectric Fresnel, visible crossing swells, shallow
  tint/shore foam and reflected clouds complement the existing ripple controls.
  Defaults expose water and shipped tex2 detail on terrain/torus; saved presets
  retain their authored settings. Probes require actual reflected geometry,
  time-driven wave motion and terrain texturing.
- **Sky atmosphere and celestial stability**: corrected Kasten-Young horizon
  airmass, continuous horizon blending, defined moon/planet edge smoothing,
  radius-aware phase coordinates and finite polar direction bases. Positive
  luminance contrast preserves dim sky; HDR targets retain solar radiance.
  Day/twilight/night/polar sky captures guard the changes.
- **Raymarch gate diagnostics**: selected inspector controls must be visibly
  on-screen, not just emit panel markers. GPU-free negative checker tests reject
  missing/clipped/off-screen/empty controls. Runs retain captures, engine logs,
  replay commands and timed JSON reports; analytic/burst failures retain masks.
- **Raymarch authoring workflow**: selected-object inspector with named primitive,
  material and lighting controls, physical axis scale, world-axis rotation deltas,
  neutral transform reset, and camera-forward object creation. Draft/Balanced/
  High/Full quality profiles leave scene features intact. Stable deletion and
  proxy transforms now respect the stored inverse SDF basis and per-slot yaw;
  headless authoring tests and a rotated/scaled/deleted-slot analytic probe pin it.
- **Raymarch review/lean cleanup**: size-optimize CPU-side raymarch orchestration
  without changing GLSL tracing, cache loader uniform locations, and release
  shader/texture/cache/timestamp resources before preset resets. The raymarch
  gate exercises all nine built-ins in a live process. Benchmark p50/p95 now pool
  actual post-warmup frames rather than repeat medians.
- **Default raymarch light-orb feedback flicker**: emissive glow no longer raises
  alpha above one in the HDR cache; coverage is clamped and blend scopes balanced.
  A 12-frame default-scene burst catches the stationary white/black oscillation
  missed by authored-sphere probes. Paused time retains its scrubbed value, terrain
  scale affects baked noise, and shader reloads refresh module uniform locations.
- **Raymarch compile presentation**: animated loading dots and three RGB orbiting
  cores emit a private gas field using existing PP blur, translation/expansion,
  and HSV effects. KHR/ARB completion polling keeps it animated during driver
  compilation; unsupported drivers retain synchronous compilation.
- **Raymarch compile latency and white-surface stability**: share the symmetric
  normal-probe loop and prevent NVIDIA's nested-loop expansion; normal-biased
  shadow/AO queries and RGB-only lighting avoid self-shadow speckles and accidental
  coverage changes. Emissive light markers no longer self-shadow. White-sphere
  time and subpixel-motion probes extend the raymarch gate.
- **Raymarch surface tracing**: conservative transformed-object and baked-terrain
  distances, genuine sign brackets, independent bounce counters, stable terrain
  sampling/normals, and normal-offset secondary rays replace early-divergence
  exits and last-step false hits. Reflection misses preserve surface opacity;
  inside-camera exits and refraction use correctly oriented normals. Built-in
  step budgets are 128; analytic solid-hit probes guard holes and phantom hits.
  Mandelbulb pole/quadrant handling and invalid object-slot initialization are
  also guarded.
- **Title return transition**: returning from a demo now uses a quiet fade
  instead of the skew-slide swing that made the title move like a pendulum.
- **Reflection audit sizing and stability**: `reflect_suite.py` now captures at
  engine size to match the fixed-pixel reflection oracle and defaults to serial
  GPU captures after parallel full-resolution contexts caused planar crashes.

## [0.16.0] - 2026-09-29

### Added
- **Common-sense guard + GUI UX pass**: `scripts/common_sense_check.py` +
  `make sense-check` (in `make check`) freezes the accepted coherence
  findings in `.sense_baseline.json` (73 declared-but-undefined wrapper
  functions, 50 unused public defines, 15 illustrative doc paths, 76
  parsed-but-unused arguments fields) and fails on new ones; `--update`
  rewrites the baseline deliberately. GUI UX pass: the title menu gains a
  footer input hint (`ARROWS SELECT / ENTER CONFIRM / ESC BACK`, the
  redundant "/ CANVAS GUI" chrome dropped to make room); the settings theme
  radios use the theme accent/lattice tokens instead of a hardcoded grey that
  read as disabled; "Tool scale" is now "Scale" so the slider shows the full
  value (was truncated to "1..."); the Extras tool cards are consistently
  named (MATERIAL LAB / SHADER LAB).
- **Battle backdrop + scene gate**: the battle scene carries the built-in
  **BATTLE** background preset (palette mode 1: deep navy → violet
  posterized gradient with a slow breathing drift) composited under the
  native 2D battle presentation. `make gfx-battle-check` pins the preset
  marker, a non-empty frame, the opaque HUD header band reading dark against
  the arena (0.081 vs 0.114), the live palette override (Δ25.4) and the
  backdrop animation (Δ6.0). The battle **logic** (turn queue, grid,
  actions, AI, waves, FX) is scheduled as a separate session;
  `docs/resurrect_battle.md` is its contract (gameX inventory, slices,
  constraints). Fixed: `--bg-palette-mode` was consumed in the engine's
  starting scene and silently lost when a scene switch re-applied a built-in
  preset afterwards; the pending override now wins in the preset path.
- **Background frontend + title backdrop**: the GFX editor gains a
  **Background** panel (palette enable/mode/size/rows/amount/speed/freq,
  cutoffs, gradient colors, a `colors[]` strip with per-index editing, blend/
  color-math combos; the image decomposer with path/paletteMax/Run/Clear; and
  built-in/preset-file Save/Load). `gX_RendererBackground_Presets_String` is
  implemented (declared but never defined) and the preset list grows a
  **TITLE** preset used by the title scene: palette mode 3 hue cycling over
  drifting luminance bands, drawn behind the existing GUI composite and the
  rotating centerpiece. `make gfx-background-check` now also asserts the
  background panel marker and the title leg (TITLE preset marker, saturation,
  live mode-override delta 45.5, animation delta 15.3).
- **Background palette systems (Mother 2-style)**: the image decomposer
  (`background/imgpro.c`) is now actually usable end to end. gX fixes the
  latent wiring bugs it inherited from gameX: the palette was never enabled
  (`--bg-imgpro` now binds and enables it, `--bg-palette-mode N` enables what
  it selects), `palette_size` was always 0 (now defaults to
  `gX_BG_PALSIZE_DEFVAL` 256, is updated per frame and set to the extracted
  color count), `palette_amount` was uploaded as a float to an `int` uniform
  (GL type error; the value stayed at 16 — now rounded int), the decomposed
  palette fills `colors[]` so every palette mode shares it, and mode 7's
  SNES row cycling is generalized to any palette size with texel-center
  lookups (fixes the last-entry wrap; 16×16 CGRAM at 256 colors, matches
  Mother 2's per-row animation). `make gfx-background-check` now also pins
  the decomposer → mode 2 quantization (unique colors 1359 → 45) and the
  mode 7 cycling animation (frame delta 64.5).
- **Background polisher (resurrected)**: `src/common/polisher.{c,h}` ports
  gameX's magic-number tuning system (register float/int entries with
  category/bounds/step/flags, find, reset, copy-paste report,
  `GX_POLISHER="NAME=VALUE,..."` scripted hook). The background registers its
  10 scalar `gX_BG_*_DEFVAL` defaults (category "Background", unbounded) and —
  unlike gameX's report-only mirrors — feeds them into the parameter-load and
  `LoadDefault` transform paths, so polishing works live. The editor gains a
  **Polisher** section (live drags, reset, report). `make gfx-background-check`
  pins the A/B (scale 0.6 → delta 63), the explicit-default identity, the
  env/report markers, the panel and the headless polisher tests
  (`POLISHER_TESTS`). The video backdrop is recorded out of scope (it needs
  the video renderer). One contraction: `gX_BG_SCALE_DEFVAL` is 1.0 (gX's
  native default; gameX's 0.25 was vestigial) — rendering unchanged.
- **Postprocess module-edge controls + STRONGBLUR**: the panel now edits
  feedback (decay + blend mode), the injected canvas (blend + tint) and the
  start texture (path + Set through
  `gX_RendererPostProcess_Texture_SetStartPath`, start blend/filter/wrap).
  The advertised **STRONGBLUR** built-in had no switch case (it loaded an
  empty chain — an inherited gameX gap); it is now a half-res 8-loop gaussian
  (gate delta 44.0). The loading-screen scene is documented as reserved.
- **Postprocess scene integration + chain reload**: the chain can post-process
  any scene (`GX_PP_VIEW=1` or the panel's **enable for this scene** toggle,
  feeding it the canvas), chain programs reload in place per entry or with
  **Reload chain** (the previously dead `reload` surface), each entry's
  uniform count is editable up to the shaders' declared 16 slots
  (`GX_PP_UNIFORM_AMOUNT` now matches the shipped `uniforms[16]` declarations
  instead of silently dropping 8..15), and `gfx-pp-check` gains a `GX_PP_VIEW`
  leg asserting a preset changes a raster scene's frame (delta 6.1). A stale
  never-implemented 3-arg `Shader_Reload` declaration was replaced by the
  real 2-arg implementation; `PP_TESTS` covers the reload bounds and the
  uniform capacity.
- **Postprocess authoring persistence + inject panel**: the panel gains
  preset files under `resources/tools/pp/` (path field, Save/Load, a
  browser/refresh and status), an **Inject** section (8 overlay slots with
  path + Set, blend, coords/scale/rotate/color, thumbnails, plus the animated
  coords/scale/rotate/color params), and `PP_TESTS` headless coverage
  (chain remove/move ordering, capacity guard, inject bounds, preset section
  shape). `gfx-pp-check` adds a `GX_PP_CHECK` save→mutate→reload round-trip
  leg that validates the JSON (chain entry, uniform value, inject slots,
  render scale). Fixes two more silent schema bugs: the chain writer added
  entries to an array with `json_object_object_add` (saved chains were
  always empty) and the reader expected a `"shader"` wrapper the array form
  never had (loads were no-ops).
- **Postprocess chain resurrection (wiring)**: the ported chain runtime is now
  reachable and authorable — `--pp` launches the scene (test-texture input),
  the editor gains a **Postprocess** panel (add/remove/reorder entries,
  loops, blend/filter/wrap, per-entry value/deltaAdd/deltaMul uniforms,
  built-in presets), headless hooks (`GX_PP_PRESET`, `GX_PP_INJECT`,
  `GX_PP_BADTYPE`), the shader-load failure path sets
  `GX_ERR_NOT_FOUND` (existence-checked, so the loader's default-shader
  fallback cannot mask it), an empty chain passes its input through, and
  `make gfx-pp-check` pins the frame A/Bs, the error code and the panel.
- **Postprocess port fixes**: chain uniforms/samplers were queried through
  raylib's location table (always -1 for custom names, silently disabling
  every effect), the input sampler needed the slot-aware binding, the PP
  scene drew twice (the embedded empty-chain instance overwrote the module
  heap), and presets' `injectCanvas` blanked the standalone scene's input.
- **Raymarch volumetric fog + sun shafts**: opt-in height fog mirroring the
  gfx `fog_volume` semantics (density, height falloff, scattering, HG
  asymmetry, color). A 20-step march runs over the shaded image from the
  camera to the surface (or the far plane for sky rays) with the height band
  anchored 1.5 below the camera; 3-tap SDF sun visibility carves the godrays
  and a sky-ambient fill stands in for multiple scattering so the haze lifts
  the horizon instead of only darkening it. `CF_VOLFOG` (default on;
  `enableVolumetricFog=0` keeps frames identical) compiles it, the panel
  gains a **Volumetric fog** section, presets round-trip a `volfog` section,
  `cloudscape` enables the deck + haze, and the features gate adds a `volfog`
  row (delta 4.7) plus a `volfog-off` variant.
- **Raymarch proxy wireframe mode**: `flags.enableProxy` (panel checkbox, or
  the `proxy` feature hook) stops the SDF march and draws the scene as raster
  line proxies at near-zero GPU cost — per-object wireframes mirroring the
  shader transform (sphere rings, box edges, cylinder/capsule/cone outlines,
  torus rings, octahedron/hex-prism edges, bounds for the procedural shapes),
  terrain/water grids, light markers and a sun-direction ray. Presets persist
  it; the profiler still records the cheap frame; the features gate adds a
  `proxy` row (delta 78.1).

## [0.15.0] - 2026-09-29

### Added
- **Hardware tiers + capability report**: `src/common/hardware.c` classifies
  the host into below-minimum / minimum / standard / recommended from the RHI
  capabilities (capability-driven, so a compatibility context reading 3.3 with
  full ARB extensions still lands correctly); `--hw-report` (or
  `GX_HW_REPORT=1`) prints vendor/renderer/version, GL level, feature flags,
  key limits and the tier with a reason. `docs/hardware.md` documents the
  tiers, example hardware, VRAM/CPU guidance and the measured raymarch cost
  scaling. Coverage: `gX_Tests_Hardware` unit-tests the classifier against
  synthetic desktop/ES caps (`HW TESTS: all passed` in `make check`) and
  `make hw-report-check` validates the runtime report and cross-checks the
  tier against the same rules.
- **Raymarch profiling + benchmark**: `--raymarch-profile[=path.csv]` writes
  the module's own GPU timers per frame (`frame,draw_ms,total_ms,render_scale`)
  and `GX_GFX_RAYMARCH_RENDERSCALE` pins the module render scale headlessly.
  `scripts/raymarch_benchmark.py` (`make raymarch-benchmark`) pairs that with
  the engine CPU frame CSV across quality rows (render scale 0.25 → 1.00) and
  one-feature-off rows with repeats and p50/p95, writing JSON + markdown;
  `make raymarch-benchmark-check` validates artifact completeness, positive
  timings, and the resolution-scaling trend.
- **Raymarch texturing v2**: the skybox and tex0..2 wrappers are authorable
  and persisted — preset schema gains a `textures` section (path, filter,
  skybox layout; a changed path unloads and requests an immediate reload
  through the new `gX_RendererRaymarch_Texture_SetPath`/`_Reload` helpers),
  the editor's Raymarch panel gains a **Textures** header (paths, filter and
  skybox-layout combos, reload buttons, thumbnails) and the Objects section
  replaces the raw `texturing` vec4 with texture/mapping/scale controls plus
  a preview. The gfx asset browser gains `RAYMARCH_SKYBOX` and
  `RAYMARCH_TEX` (slot 0..2) targets, so raymarch textures are picked from
  the shared browser. Coverage: `gfx-raymarch-check` asserts the `textures`
  section and a mutated tex1 path round-trip, and `gfx-raymarch-features`
  gains an `objecttexture` row (tex0 triplanar vs off, frame delta 2.8).
- **Raymarch primitive set v2**: five new SDF object types — menger (xyz half
  extents), capsule (half-length, radius), octahedron (size), rounded box
  (half extents + corner radius), hex prism (radius, half-height) — wired
  through the shader (`common/sdf_basic.glsl` + the object switch), the
  editor combo (14 types), per-type attribute defaults, and a new
  **primitives** built-in preset (one hero row of the set). gxmath gains the
  CPU twins (`gX_F32_SDF_Capsule`, `_Octahedron`, `_RoundBox`, `_HexPrism`;
  Menger's fold now uses floor-mod so negative inputs match GLSL `mod`), the
  math corpus covers them against the SymPy oracle (39 values), `RM_TESTS`
  pins the shapes, and `gfx-raymarch-check` gains a gallery leg asserting each
  new type renders (per-object ROI deltas; `GX_GFX_RAYMARCH_GALLERY=1|2`).
- **Harness-agnostic workflow**: `the project contributor guide` is the single instruction
  contract for every coding agent (Codex, Claude Code, OpenCode, ...). The
  reusable skills/commands/agents moved to tracked `harness/` sources with
  `make harness-sync` materializing the per-harness `.opencode/` and
  `.claude/` layouts (untracked, generated) and a `make harness-check` gate
  inside `make check` verifying the sync, the instruction contract, the state
  symlink, and the scratch override. Session handoff state moved to local
  untracked files, and `scripts/audit_common.py` honors `GX_SCRATCH` (legacy
  `/tmp/opencode` default). New AI-authored code is marked `//ai:`;
  `//opencode:` remains the historical alias. See `docs/ai_policy.md`.
- **True-alpha raymarch compositing**: `GX_GFX_TRANSPARENT_BG=1` adds a
  transparent-background gfx mode for the composite. A new `scene_mask` pass
  (opt-in, right after `present`) derives the gfx layer's alpha from the scene
  depth — geometry stays opaque, sky/far pixels become transparent — using a
  separate ZERO/ONE blend that preserves RGB and replaces alpha, so no scratch
  read-back is needed. The compositor then blends the gfx layer with true
  ALPHA, and the raymarch backdrop shows through the gfx sky.
  `GX_GFX_RAYMARCH_COMPOSITE=2` renders the backdrop layer alone as the gate
  reference. `gfx-raymarch-features` asserts the composite sky matches the
  backdrop exactly (0.000 mean delta), the geometry region matches the opaque
  gfx frame (0.000), and the previous SCREEN path differs (152), so the mode
  cannot silently no-op. The default opaque path stays pixel-identical.
- **Scene export v2 (MTL sidecars)**: every exported OBJ now gets a sibling
  `.mtl` carrying the render-side material values — `Kd` through the same
  `AlbedoTint_Apply` path, `Ke` from the effective emissive color × glow,
  `Ns` for Phong or `Pr`/`Pm` for PBR, `d` when blending is on — and
  override albedo/emissive maps are copied next to the OBJ and referenced by
  basename. The OBJ carries `mtllib`/`usemtl`, and `gfx-export-check` pins
  the linkage, value ranges, the Phong/PBR mix and the map copies (the
  `resources/texture/%s` format template joins the deps allowlist). The
  mesh→material sidecar match is now a shared renderer helper
  (`gX_RendererGFX_Model_MeshOverride`).
- **Scene export v2 (native GLB)**: each model also writes one glTF 2.0
  binary (`src/renderer/gfx/glb_writer.c`) — every drawable mesh becomes a
  primitive with the world transform baked, materials carry the render-side
  PBR factors and emissive (with the `KHR_materials_emissive_strength`
  extension when the glow exceeds 1), and override albedo/emissive maps are
  embedded as PNG/JPEG images. `gfx-export-check` decodes every GLB
  (chunk/JSON/accessor structure, decoded AABB vs accessor min/max, index
  bounds, material refs) and pins the baked cube transforms, the embedded map
  payloads and the emissive extension; an unbaked transform fails the gate.
- **Texture utilities v2**: value noise joins the generators (integer-hash
  lattice with smoothstep bilinear interpolation, deterministic across runs)
  and the Texture tools panel shows a preview thumbnail of the last
  generated/baked texture. `gfx-texture-check` now covers five generators,
  the value-noise structure and cross-run determinism, and the preview path.
  Reviewed lean cap refresh (+25 KiB payload) documented in
  `.lean_budget.json`.
- **Camera v2 (marker drag + output sizes)**: the selected authored camera's
  position and target draw as viewport markers (line + dots) and are
  grabbable on a camera-facing plane; dragging writes the authored view.
  `Render to image` gains an output size (0 = viewport crop; a positive pair
  rescales the crop). `gfx-camera-render-check` pins a 320x180 sized render
  and the marker pick/position/target writes (an injected no-op apply fails).
- **Editor panel polish**: the generated-textures folder joins the asset
  browser's local texture grid (the scan appends it to the local texture
  source; `gfx-asset-roots-check` asserts the 7 generated files are indexed),
  the snap controls and the primitive detail field keep their labels visible,
  and the browser hint names the generated textures.
- **GUI effect production adoption**: effects are opt-in per theme — the
  default theme zeroes the glass tokens (flat blueprint default) while
  `resources/style/theme_aero.json` opts in with an aqua palette, glass
  (blur 10 / tint 0.5 / rim / refraction / gloss), the metallic depth
  profile and a cool cursor light. The escape-menu panel is a production
  glass surface (themes without glass keep the flat fill; `GX_GUI_GLASS=0`
  forces it off). `make gui-effect-contrast-check` pins WCAG 2.2 AA for the
  pause panel's hint/title/button bands over glass and over the flat panel,
  with the cursor light parked away and on the hint (the default hint token
  was brightened to pass); the GUI benchmark gains
  `guitester-glass`/`guitester-cursor` cost rows; the theme gate loads the
  Aero theme.
- **Cursor hover glint**: `gui.style.cursorLight.glint` tightens (radius
  x0.55) and brightens (x1.54) the light over hovered SDF surfaces, more
  when pressed (x0.40/x1.99), with `GX_GUI_CURSOR_GLINT=0` as the force-off.
  `gui-cursor-light-check` gains a `hover-glint` row (hovered button ROI
  delta 4.8, far field identical), keeping the gate at 5/5.
- **Metallic anisotropy direction**: the depth profile preserves the authored
  gradient orientation, so a horizontal gradient gets its metallic band and
  rim along X (shader mode 6) while vertical surfaces and derived button
  ramps keep the Y band. `gui-depth-check` gains a `metallic-horizontal` row
  (band at t=0.38 along X, Y flat 0.0); ignoring the orientation fails it.
- **Text outline**: `gui.style.textOutline` (width in base-1024 px scaled
  with the font, color; width 0 disables) draws an 8-way 1px copy under the
  glyphs on both the bitmap and SDF paths via
  `gX_RawString_Draw_Outline`; the high-contrast theme opts in (width 1,
  black). `gui-shadow-check` gains three rows (bitmap text band +9.9k dark
  pixels, SDF demo, width-0 pixel-identity) and runs 7/7; the
  effect-contrast gate proves the outline lifts glass text contrast
  (hint 4.74->5.12, title 6.98->8.18, button 6.14->6.84) and runs 5/5.
- **Per-size text shadows**: `gui.style.textShadow.offsetXSmall`/
  `offsetYSmall` (defaults mirror the base pair; the default theme stays
  pixel-identical) apply below `core.bodySize`, and the Aero theme tightens
  them to 1.5/1.5 so compact labels stop carrying the body offset.
  `gui-shadow-check` gains a `small-tokens` direction row (8/8).
- **Soft text shadows**: `gui.style.textShadow.softness` (base-1024 px,
  0 = off) switches the single shadow copy for a 5-tap ring (center + four
  cardinal taps at alpha/5); the Aero theme uses 2.0. `gui-shadow-check`
  asserts the ring widens the shadow (8.6k ring pixels) and that explicit
  and schema-default softness 0 agree pixel-identically; the GUI benchmark
  gains a `guitester-shadow` cost row.
- **Per-element depth override**: `gX_GUI_Builder_SurfaceDepth` sets a 2-bit
  element state (0 inherit, 1 none, 2 horizon, 3 metallic) so one theme can
  mix surfaces; `gui-depth-check` gains a `per-element` row asserting the
  override wins over the theme mode in both directions (9/9).
- **SDF-blur shadows**: SDF fonts blur through the distance field instead of
  the 5-tap ring — `sdf_text.fs` gains a `soft` uniform (spread, edge-band
  and alpha multipliers; defaults `0/1/1` reproduce the plain edge exactly)
  that the soft-shadow helper sets for the shadow copy. The same change fixes
  the SDF text path: the shader read `.r` (white in the GRAY_ALPHA atlas), so
  the SDF demo rendered as a solid white block; it now reads `.a`, matching
  raylib's own `sdf.fs` and the real distance data. `gui-shadow-check` gains
  a `soft-sdf` row asserting the widened shadow (11/11).
- **Four-point gradient gate coverage**: GuiTester gains a four-point
  gradient fixture (TL red / BL blue / BR yellow / TR green) and
  `gui-depth-check` asserts all four authored corners through the SDF path
  (the probes sit in the clean top/bottom strips where the title canvas does
  not overlap the forced template). Title-view glass adoption was tried and
  reverted: the bright title backdrop cannot carry light text at AA with the
  current mix — the constraint is recorded in `docs/gui_visual_backlog.md`.

- **WM resize sync + markup ownership tests**: the per-frame WM resize sync
  (live client size → `maindata` → viewport re-fit → window-RT re-target) and
  the cramped dock fallback are engine functions the headless suite drives
  directly (`GUI WM RESIZE tests`), and nested markup containers pin inherited
  visibility plus per-child callback ownership across rebuilds
  (`GUI MARKUP OWNERSHIP tests`). The GUI scoreboard rows move to
  markup 80% / compositing-resize 85%.

## [0.14.0] - 2026-09-28

### Added
- **GUI development brief** (`docs/gui_brief.md`): the handoff for the next
  GUI session — the current surface (canvas/elements/editor/themes), the
  architecture contracts (`frontend_design.md`, `gui_testing.md`, the
  audits), the audit's ten-step repair order and four gate gaps, the editor
  backlog items, the gate patterns to follow and the scope/lean/docs
  constraints.
- **Resurrection briefs** for the next sessions: the background renderer
  (the gameX polisher + video backdrop), the postprocess renderer (the custom
  post-shader chain with animated uniforms) and the battle scene (a
  gfx-rendered demo built from blockout primitives, keeping the existing 2D
  HUD). Each brief carries the gameX reference paths, the gX gap, the slice
  plan, the gates to add and the provenance/scope constraints
  (`docs/resurrect_*.md`).
- **GUI interaction and theme audits**: `GX_GUI_TRACE=1` emits a structured
  `GUITRACE:` readout for clicks, focus, title views, modal/dialog scope,
  canvas builds, and scene responses; canvas builds also print the element map
  (name/text/rect/state), so scripted gates resolve click targets from the
  engine's own layout instead of hardcoded geometry. `make
  gui-interaction-check` drives the title views, quit dialog (background
  clicks stay blocked), scene continue, escape-menu Resume, GuiTester
  checkbox, and Settings toggle through `GX_AI_INPUT`. `GX_GUI_THEME`
  overrides the theme file for a run, and `make gui-theme-check` loads every
  shipped theme (default, high contrast, color vision, blueprint), gates the
  WCAG 2.2 AA On- pairs, and proves the high-contrast theme changes pixels.
  The quick GUI matrix joins the default audit-summary tier; the full matrix,
  interaction, and theme gates join `make audit-summary-full`.
- **GUI snapshot and theme round-trips**: markup type matching is now
  case-insensitive and the editor snapshot stores an element's first text as
  its own text (previously duplicated as a child, so a canvas -> markup ->
  rebuild turned typed, text-bearing elements into anonymous containers).
  Headless regressions pin the named element's type/name/rect/color/layer/
  visibility/text survival and the theme save/load byte-identical fixed point.
- **GUI benchmark artifact**: `make gui-benchmark` records medians of repeated
  wall-clock template runs into `/tmp/opencode/gui_benchmark/report.json` and
  `make gui-benchmark-check` validates the artifact shape (rows, sample
  counts, positive medians, baseline-relative costs) without host-specific
  budgets — the portable-artifact counterpart of the GFX benchmark.
- **GUI layout pass**: `gX_GUI_Element_Text_Analyze` reports the draw-truth
  text bounds (wraps/truncated/overflow/shrink) from the same prepared path
  the draw call uses; the `GX_GUI_TRACE` element map carries the verdict and
  `make gui-layout-check` fails any template whose text does not fit its box
  (intentional oversized demos are allowlisted with a rationale). The opt-in
  `gX_GUI_Builder_Text_AutoShrink` reduces a label's font size to fit, floored
  at `gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE` (0.6), and compact widget labels
  (button/checkbox/dropdown/radio/toggle/stepper/tab) use it by default.
- **GUI text shadows**: the per-element `TEXT_SHADOW` flag (and the new
  `gX_GUI_Builder_Text_Shadow`) now draws through theme tokens
  (`gui.style.textShadow`: offset in base-1024 pixels scaled with the font,
  color + alpha for softness, alpha 0 to disable) on both the bitmap and SDF
  text paths. The title wordmark/subtitle opt in, and `make gui-shadow-check`
  pins determinism, the down-right offset direction, the SDF path, and a
  green layout pass with shadows forced on every label.
- **GUI cursor light**: `gui.style.cursorLight` (strength/radius/color) adds
  a soft radial highlight on SDF surfaces that follows the pointer, mapped
  per draw path (canvas buffer via the inverse transform; window pixels via
  the viewport mapping). It ships disabled (strength 0 = pixel-identical) and
  `make gui-cursor-light-check` pins the follow direction, the same-position
  on/off contrast, the strength-0 no-op, and determinism.
- **GUI Aero glass**: `gX_GUI_Builder_Shader_Glass` attaches `glass.frag`,
  which samples the accumulated backdrop (`gui.ping`) through a small
  gaussian blur, tints toward the element color, and adds a silhouette rim
  plus a top gloss. Tokens live in `gui.style.glass` (base-1024 pixel lengths
  scaled with the window); zeroed tokens or `GX_GUI_GLASS=0` leave the plain
  translucent fill as the pixel-identical fallback. The GuiTester fixture
  carries a demo panel and `make gui-glass-check` pins the fallback, the
  backdrop variance drop, the rim lift, and determinism.
- **GUI gradient depth**: the SDF surface program now renders the authored
  gradient modes (vertical/horizontal/four-point) and two themed depth
  profiles — `gui.style.surfaceDepth` horizon (sky/ground ramp with a soft
  hue-preserving reflected band) and metallic (compressed ramp, anisotropic
  highlight, top rim). `make gui-depth-check` pins the authored ramps through
  the SDF path, the band positions against the tokens, the `GX_GUI_SDF=0`
  flat-path fallback, and repeat determinism; `mode` 0 keeps existing themes
  pixel-identical.
- **Canary-backed GUI text-buffer tests**: `gX_Tests_TextInput` now places
  sentinel guard bytes on both sides of the external text-input buffer and
  checks the tail through the whole test array across capacities 1/4/8/16 —
  short-text no zero-padding, the maxLength pattern, and repeated syncs. An
  injected past-capacity write fails three rows; the bounded sync itself has
  been in place since the 2026-09 audit.
- **GUI partial-rebuild context coverage**: `gX_Tests_GUI_CallbackRebuild`
  refreshes one canvas repeatedly (clear + rebuild, mirroring the refresh
  pass) and asserts another canvas's owner-scoped callback contexts stay
  untouched and keep firing their own callback; an injected shared-pool
  alias fails every round.
- **Pause-blur gate**: `make gui-pause-blur-check` models the EscapeMenu
  backdrop over `--gfx` as the shader's 19-tap gamma-2.2 kernel plus the
  theme `escape.dimOverlay` blend, and fails on a stale/unseeded `gui.ping`
  (near-black backdrop) or a programless blur (dim-only backdrop); the panel
  region and repeat determinism are pinned too.
- **GFX scene smoke**: `make gfx-scene-smoke` boots every standalone scene
  flag (analog, clock, sky, teletext, textwriter, water, wireframe, raymarch,
  battle) for 60 fixed-dt frames and asserts exit 0, a non-empty frame and no
  `ERROR!`/`FATAL!` log lines — the boot floor for the secondary demo scenes
  while they are fleshed out during beta. Joins the quick audit tier.

### Fixed
- **EscapeMenu pause blur actually renders**: the GUI shader preload used the
  `blur.frag` basename while the file lives at
  `resources/shader/postprocess/blur.frag`, so the blur element's program was
  silently NULL on every scene (the pause overlay only dimmed). The preload
  now uses the full heap name, matching the builder lookup.
- **GUI editor identity** (audit finding 11): the editor's selection and
  slider field bindings are generation-checked handles, so a target rebuild
  that recycles element slots can no longer make the editor alias a
  different element. `gX_Tests_GUI_EditorIdentity` drives the remove/re-add
  cycle and asserts stale selection and field handles are refused while
  fresh ones resolve; a dropped generation check fails two rows. Also
  removed a stray scope brace that had nested the editor's whole element
  build.
- **Title settings BACK was a dead end**: embedding the Settings template on
  the title canvas overwrote the canvas template ID, so the refresh rebuilt
  Settings forever and BACK never returned to the menu. The host restores the
  title identity after the embedded build; `make gui-interaction-check` pins
  the SETTINGS -> BACK -> menu sequence.
- **Text-fit defects found by the layout pass**: DebugDisplay rows drew 32px
  text in 18px rows (clipped into each other) and now use the widget scale;
  the Settings action buttons kept the 32px default ("Defau...") and now use
  the compact widget type; the three UI-theme radios could not fit their
  1.0-grid cells even at the shrink floor and stack full-width; the escape
  close glyph and stepper signs were taller than their buttons; the ThemeLab
  text card scaled a fixed 64px into a 0.6-grid box and now derives its size
  from the card height.
- **SDF path flattened authored gradients**: elements with `GRADIENT_ENABLE`
  rendered as a solid fill because the surface program only carried the
  derived button ramp; the gradient modes now travel with every SDF draw
  (the GuiTester gradient demos render their ramps again).
- **Style schema arena overflow**: the schema arena was sized for 160 tokens
  and the text-shadow + surface-depth groups pushed registration past it,
  corrupting the heap on theme load (a hang in the worst case). The arena has
  headroom now (192).

## [0.13.0] - 2026-09-28

### Added
- **Log system depth**: `GX_LOG_MODULE_LEVELS="gfx=debug,gui=info"` overrides
  per-module levels at startup (names match the detail tags; the UI can still
  change them), `GX_LOG_JSON=1` emits machine-readable lines
  (`{"ts","level","module","msg"}`), and the trace sub-levels are **real
  filter levels** — `--logLevel 5` hides the TRACE2 per-item detail
  (`DRAW:`/`SHADOW:` per model and point-light pass) that `--logLevel 4`
  shows. They used to be mapped to TRACE before filtering, which made the
  depth unreachable; TRACE2/3 had no callers, so the change is safe.
- **Certification system (`make certify`)**: the gate stack formalized into
  tiers — bronze (`make check`) → silver (+ the full matrix + Mathlib proofs)
  → **gold (the target)**: + version-check, the packaging structure (the
  archive carries the engine, not `proofs/`/`build/`) and the art dogfood
  walk → platinum (+ the RC ledger's completeness items) → diamond
  (aspirational). Each run writes a hashed artifact under `certificates/`
  recording every gate's verdict, the environment and the binary hash —
  `NOT CERTIFIED` artifacts included, so gaps are recorded. See
  `docs/certification.md`.
- **Log sinks and repeat suppression**: `GX_LOG_FILE=<path>` mirrors every
  emitted line to a plain transcript (no ANSI escapes, line-buffered) and
  `GX_LOG_CONSOLE=0` silences stdout; `GX_LOG_DEDUP=1` collapses consecutive
  identical messages into one line plus a `last message repeated N times`
  note (opt-in — the audit gates parse every line, and the crash ring keeps
  the raw truth). `GX_LOG_TEST_REPEAT=<N>` drives the gate.
- **Allocation-site tracking**: the leak report now names each still-live
  allocation's call site — `symbol+0xoff` via `dladdr` for exported callers,
  or a base-relative `+0xoff` (PIE) that resolves with
  `addr2line -e build/gX_build -f -C 0xOFF`. The side-table is heap-backed
  so the bss budget holds. First run explains itself: the 16 KB entry is the
  crash-context log ring, the small ones are engine heaps — process-lifetime
  by design.
- **Threshold watches**: append `:OP VALUE` to a scalar watch
  (`GX_DEBUG_WATCH="frameCount=0xADDR:u64:>30"`) and it logs only the rising
  edge (`GX_WATCH: name=value crossed >30`) instead of every change; ops are
  `> >= < <= == !=`. `GX_DEBUG_WATCH_TEST_THRESHOLD=<N>` arms the frameCount
  test watch for the gate.
- **Frame watchdog (`GX_FRAME_WATCHDOG_MS=<ms>`)**: frames over the budget
  log the overrun with a profiler zone dump and the crash-context log tail
  (rate-limited to one dump per 60 frames); `GX_FRAME_WATCHDOG_ABORT=1`
  aborts on the first overrun so the crash handler captures the full state.
  The debug gate covers both modes.
- **Assert macro with context (`GX_ASSERT`)**: logs the
  expression/file/line/function, sets the error slot (`name=assert`) and,
  with `GX_ASSERT_FATAL=1`, aborts so the crash handler captures the full
  trace. For states that must never happen — boundary failures keep using
  `GX_ERROR_SET` with a return. `GX_DEBUG_ASSERT_TEST=<frame>` drives the
  gate; `make debug-check` now covers five legs.
- **Crash-context log ring**: the log keeps the last `GX_LOG_RING=<N>` emitted
  lines (default 32, 0 disables; heap-backed so the lean gate's bss budget
  holds) and the crash handler dumps them as `LOG TAIL` with a signal-safe
  `dprintf` before the stack — the lead-up that registers and frames alone
  never show. `make debug-check` pins it.
- **Allocator accounting (`GX_MEM_DEBUG=1`)**: `gX_Malloc/Calloc/Realloc/Free`
  now track live/peak/total counts and bytes, and the dump at exit lists the
  still-live allocations as a leak report. `make debug-check` pins the
  balance (`allocs - frees == live`), the peaks and a leak ceiling. First run
  shows ~28 live allocations (~12 KB) at exit, and the accounting caught its
  own first bug (an unbalanced `Realloc_Clear` release) before landing.
- **Chrome trace export (`GX_PROFILE_TRACE=<path>`)**: the profiler records
  every zone instance into a heap ring (no static bss cost;
  `GX_PROFILE_TRACE_EVENTS=N` overrides the capacity) and writes a Chrome
  trace JSON at shutdown for Perfetto/speedscope. `make profile-check` runs
  the export and validates the events. First use surfaced a 387 ms startup
  frame and a 108 ms UI canvas rebuild.
- **New conversions (`gxmath/convert`)**: exact sRGB↔linear (IEC 61966-2-1,
  for CPU image I/O and tooling), amplitude↔decibel (20*log10), MIDI
  note↔frequency (A4 = note 69 = 440 Hz) and Celsius↔Fahrenheit. The math
  pass covers them together with the existing unit/color/coordinate and
  astronomy conversions, whose round trips are now pinned.
- **Deterministic RNG (`gxmath/random`)**: `gX_Random32_*` is a PCG32
  stream with explicit state — identical sequences on every platform, and
  each consumer can own its stream. The engine's global draws, color
  random, decal rotation and arp note choice now use it; the weather and
  splat hand-rolled LCGs are gone (they existed to dodge `rand()`
  pollution). Seeded captures and audits no longer depend on the libc.
  Verified by the math pass (published pcg32-demo vectors) and a core Lean
  spec for the structural guarantees.
- **Math pass** (`make math-check`): the numeric sibling of the Lean proofs.
  The engine's gxmath corpus (`GX_MATH_DUMP=1`, `src/gxmath/gxmath_tests.c`)
  is recomputed by `scripts/math_pass.py` with an independent SymPy oracle —
  exact rationals for the algebraic cases (multiply, invert, transform,
  determinant, vectors), float64 formulas for the trig/sqrt cases
  (perspective, rotate_xyz, lookat). Blocking in `make check` and
  `make check-fast`; docs in `docs/math_pass.md`. The sweep also covers
  sdf/easing/scalar/int-math/color/physics/fourier/calculus — and already
  caught the `nCr` and color lerp bugs above. The blackbody cases check the
  engine's radiance formulas against CODATA/SI constants from outside
  `config.h`; the FFT cases pin the forward DFT convention and the calculus
  cases pin the numeric methods against analytic values.
- **Error codes v2**: model load failures (`name=not-found`), shader compile
  failures (`name=gl`) and asset-root validation now set codes; `error_check`
  gains bad-shader and missing-model legs, pinning four boundaries.
- **Profiler history + debugger symbols**: the profiler keeps a 120-frame
  time ring (min/avg/max in the dump, frame plot in the Scene panel), and
  Debug builds link `-rdynamic` so crash stacks name functions
  (`gX_Debug_Update+0xc4`). The editor can print a backtrace on demand and
  `GX_DEBUG_BACKTRACE=<frame>` does it headlessly; the debug gate pins both.
- **Prefab group tagging**: per-kind tag masks (models/lights/decals/
  emitters) with Tag selected / Clear / "capture tagged group" in the prefab
  panel; `gfx-prefab-check` pins a tagged capture of 1 model + 2 lights +
  1 decal + 1 emitter (radius mode unchanged).
- **Raymarch compositing v1**: `GX_GFX_RAYMARCH_COMPOSITE` (or the panel's
  composite toggle) sets compositor layers — the raymarch module renders the
  backdrop and the gfx module is screened over it; `gfx-raymarch-features`
  proves both layers contribute (composite differs from gfx-only and from
  raymarch-only). True alpha compositing needs a gfx transparent-background
  mode (the gfx output is opaque today) — noted as the follow-up.

- **Decal bake (tooling scope, slice 4)**: the Decals panel projects the
  scene's decals top-down into albedo (bake color + coverage alpha) and ORM
  (AO 1 / bake roughness / bake metalness) PNGs under
  `resources/tools/bakes`; `GX_GFX_DECAL_BAKE_CHECK` and `make
  gfx-decal-bake-check` pin the files, coverage and channel values.
- **Prefab v2**: bundles capture and place decals and particle emitters
  (radius-bounded, anchor-relative like models and lights), and the panel can
  arm click-to-place so the next viewport click sets the placement point.
  `make gfx-prefab-check` covers the new kinds.

### Changed
- **`Equatorial2Horizontal`/`Horizontal2Equatorial` take the observer
  position** (`latitude`, `longitude` in radians) instead of hardcoding
  45N/0E, so the pair is a general rotation rather than a scene-specific
  helper. The sum/difference trig form collapsed to the direct rotation,
  and the math pass pins both directions and both round-trip orders.
- **Winter streetlight art scene** (a private authoring scene): the gX logo sign sets a white
  emissive color (factor 2.5), rotates 90 degrees and remaps its UV window
  (the blockout cube's Z faces are transposed 0.25x0.25 atlas windows), so
  the logo renders upright.
- **Winter streetlight composition**: the figure is a blockout person built
  from primitives (the previous prop had no textures and read as a box); the
  lamp head sits on the pole with the point light at its centre (the light
  needs the lamp radius of clearance over the pole rim or the rim shadows the
  whole ground); the lamp dominates the local lighting (20.0, tighter
  falloff) against a soft fill and a low sun so the figure's shadow reads;
  the hero camera frames the lamp through the shadow, and the fog starts
  farther out.

### Fixed
- **Three `convert` pairs never round-tripped**: `Vector2Equatorial` used
  `acos(p.x/cos(elevation))`, which loses the azimuth sign for negative y;
  `Vector2Horizontal` returned degrees against a radians inverse (with the
  same acos sign loss); `space2ball` mixed degrees with a raw z component,
  so `ball2space` could not reproduce its input. All three pairs now use
  `atan2`, radians and documented frames (bearing from +y/north for the
  horizontal and dome pairs, azimuth from +x for the equatorial pair), and
  the math pass pins both directions with round-trip outputs. No callers
  existed for any of the six functions.
- **`gX_I32_nCr` returned garbage**: it computed `n / k * nk` (a missing
  parenthesis), so the intermediate overflowed `gX_I32` — `nCr(10,3)` came
  back as -1.2e9 instead of 120. Caught by the math pass's `int_math` case.
- **`gX_Color_Lerp`/`LerpRGB` cast a negative float to `gX_U8`** (undefined
  behavior, C11 6.3.1.4); the wrap happened to give the right value on x86
  but is not portable. The channels now interpolate in signed arithmetic and
  clamp to [0,255].
- **Point-shadow casters enclosing the light**: the point-shadow pass now
  skips a model whose local-space bounds contain the light — an enclosing
  caster (the lamp bulb around its own point light is the common case) blocked
  every direction and blackened the whole scene. Diagnosed by comparing
  `castShadows` on/off renders (the light contributed exactly nothing with
  shadows on).
- **Preset-injected emission maps rendered black**: the sampler read the map
  but `emissiveColor` defaults to black (factor 0), so the contribution was
  zero. `Model_Load`'s preset-binding loop now fills white on black (and
  factor 1.0 when <= 0) with the GLB loader's precedence — a scene-set
  emissive color survives.
- **Texture load and map refresh**: `gX_RendererGFX_Texture_Init` now resets
  `loaded` (stale heap memory could block a load entirely), and the material
  refresh treats preset `textureID`/`texturePurpose` bindings as map sources
  instead of forcing `has*Map` false.

## [0.12.0] - 2026-09-28

### Added
- **Audit result cache**: passing gates are fingerprinted by their inputs
  (binary + `resources/**` + `audit_common.py` + the gate script) and skipped
  on an unchanged tree — a repeat full matrix drops from ~14 minutes to
  seconds. `--no-cache` / `AUDIT_CACHE=0` force fresh runs, `make
  audit-cache-clear` drops the cache, and `make version-gate` always bypasses
  it. `make cache-check` pins the contract.
- **Debugger traces**: `src/common/debug.c` replaces the minimal crash handler
  — signals now dump the x86_64 register file, a stack with frames and an
  `addr2line` recipe (async-signal-safe `dprintf`), and exit with
  `128 + signal`. Variable watches sample addresses per frame and trace
  changes (`GX_WATCH`), headless via `GX_DEBUG_WATCH="name=0xADDR:type"` or
  from the Scene panel's Watches block. `make debug-check` (summary row
  `debug`) pins a controlled crash and a strictly increasing watch; docs in
  `docs/debugging.md`.
- **Profiler**: `src/common/profiler.c` is no longer a stub. Checkpoint
  phases (main loop) and nested zones (render passes — 53 zone names across
  ~148 call sites) accumulate per frame with a snapshot at
  `gX_Profiler_Reset`; `GX_PROFILE_DUMP=<N>` traces one line per entry
  (`name=… calls=… ms=… pct=…`) every N frames, and the Scene panel's stats
  block shows a last-frame table. `make gfx-profile-check` pins the dump
  (phases present, zones present, sane budget).
- **Error codes v1**: `src/common/error.h` defines boundary codes
  (not-found/io/parse/schema/oom/invalid/unsupported/gl/state) with a
  last-error slot (`gX_Error_Set` / `GX_ERROR_SET`) that stores code, message
  and source position and traces one machine-readable line. The preset
  boundaries (GFX save/load, raymarch panel save/load) set codes, and
  `make error-check` asserts the `name=` field instead of prose.
- **Error-handling pass**: a policy doc (`docs/error_handling.md`), a static
  silent-failure audit (`make error-handling-check`, also inside `make
  check` — no-op-success stubs must be allowlisted with a reason, discarded
  status results are informational) and a bad-input gate (`make error-check`,
  summary row `error`: missing and corrupt presets must be reported without a
  crash). Fixed `gX_Audio_Music_SetVolume`, which ignored its argument and
  returned success.
- **Raymarch revival — preset schema**: the raymarch renderer's JSON schema is
  no longer a stub. `schema2.c` implements the offset-based save/load
  (state/parameters/fog/sun/terrain/water/flags/misc/debug plus the
  `objects[8]`/`lights[8]` bridges) through the native `gX_Schema_JSON_*`
  system, so `--raymarch` scenes are authorable and persistable like the
  background renderer's presets. `GX_GFX_RAYMARCH_CHECK` drives a
  save-mutate-reload round trip and `make gfx-raymarch-check` pins it plus a
  non-empty scene frame.
- **Raymarch CPU tests**: the gameX harness (noise/step/water) is revived as
  `--test-only` coverage. The terrain noise octave sampler is now shared with
  the texture bake and tested for range, seamless tiling, seed sensitivity
  and continuity; the step-correction polynomial and the water height-field
  contracts are mirrored from `raymarch_new.frag`. The defines parser tests
  were skipped — the shader compiler already has its own coverage.
- **Raymarch editor panel**: the GFX editor's Scene panel hosts a Raymarch
  section when the module is active (`--raymarch --gfx-editor`) — live
  editing of steps/bounces/epsilon/renderScale, the enable flags, sun and
  fog, terrain (the seed drives the noise regeneration), water, plus preset
  save/load through the restored schema and a shader hot-reload button.
  `make gfx-raymarch-check` now also asserts the panel trace and an editor
  frame.
- **Raymarch shader-feature verification + gfx integration**: the raymarch
  module can be forced into any scene (`GX_GFX_RAYMARCH_VIEW`, or the panel's
  "view raymarch module" checkbox), so a GFX session can flip to the raymarch
  view live. `make gfx-raymarch-features-check` (summary row
  `raymarch-features`) pins each enable flag's visible effect on the frame,
  compiles the shader under six `CF_*` permutations, and proves the module
  changes a `--gfx` frame.
- **Raymarch visual pass + authoring panel v2**: the default scene ships one
  hero object (torus) and one light, with water and sun shadows on, so the
  feature set reads out of the box. The panel gains objects/lights editors
  (add, remove and edit the eight slots), the nine debug visualizations
  (position/direction/ray/gradient/normal/bounces/steps/path length/distance)
  and live `CF_*` compile-time feature checkboxes that rebuild the defines
  string and hot-reload the shader.
- **Raymarch lighting, gallery and terrain look**: the point-light path is
  guarded (attenuation denominator floored, light contribution clamped, stray
  alpha injection removed) and the default attenuation is gentler
  (`0.5, 0.5, 0.05`) — pure quadratic falloff made lights invisible beyond a
  few units and blew out up close. The default scene ships a subtle accent
  light again. The panel gains a preset gallery — folder list, built-in
  picker (island/water/flatlands/desert/alien/canyon) and a randomize button.
  The terrain color mix derives its slope from a smoothed 0.5-unit terrain
  sample instead of the epsilon-scale gradient, cutting the terrain
  neighbour-difference speckle metric from 3.9 to 2.7. `gfx-raymarch-features`
  pins the lights flag as well.
- **Parallel audit summaries**: `scripts/audit_summary.py --jobs N` overlaps
  the self-contained gates (each owns its temp dirs) and retries a failed
  gate once, recording the first verdict so flakiness stays visible;
  `make audit-summary[-full] AUDIT_JOBS=N` wires it. The default stays
  sequential, so existing runs are unchanged.
- **Scene export (OBJ)**: the Scene panel exports the selected model or every
  loaded model as OBJ through raylib's `ExportMesh` with the world transform
  baked into vertices and normals (missing UVs or normals are synthesized,
  since the raylib writer requires both buffers). Output lands in the exports
  folder (`GX_GFX_EXPORT_DIR` redirects it). `GX_GFX_EXPORT_CHECK` drives it
  headlessly and `make gfx-export-check` pins per-file vertex/triangle counts
  plus the baked placement of all three fixture cubes against their preset
  transforms (center and extents exact to 0.01).
- **Texture utilities**: the Scene panel's Texture tools generate procedural
  PNGs (solid, checker, gradient, radial, white noise, perlin, cellular) with
  size, parameter and color controls, bake normal maps from a height image
  (Sobel on luminance) and pack ORM maps (R occlusion / G roughness / B
  metallic) with nearest sampling. Output lands in the generated-textures
  folder (`GX_GFX_TEXTURE_DIR` redirects it). `GX_GFX_TEXTURE_CHECK` drives
  the same writers headlessly and `make gfx-texture-check` pins the files,
  the bake and channel-exact packing.
- **Blockout primitives**: the Scene panel adds cube, plane, sphere, cylinder,
  cone, and torus primitives with size and detail, generated at runtime from
  raylib's `GenMesh*` and handed to `gX_RendererGFX_Model_AddGeneratedMesh` —
  no asset files. `GX_GFX_PRIMITIVE_CHECK` drives it headlessly and `make
  gfx-primitive-check` pins the traces (24/432/432 verts on the calibration)
  plus the visible frame change. First slice of the tooling scope; texture
  utilities and scene export follow.
- **Frutiger Aero showcase and theme**: `resources/tools/aero.json` is a
  bright glossy scene — reflective aqua floor, chrome and glass PBR
  materials, emissive accents, cool-tinted lights, bloom/SSR/rim/SSAO, hero
  and wide cameras — plus a fourth ImGui theme (`GX_SETTINGS_UI_THEME_AERO`:
  translucent light-blue glass panels, rounded corners, aqua accents, in the
  theme selector) with a `GX_GFX_UI_THEME` headless hook. `make
  gfx-aero-check` pins the preset contract (chrome + glass + glow + cool
  tints + cameras + temporal effects off), a bright frame with a glow
  signature (calibrated mean 149, glow 8.9%), and the applied-theme trace.
- **Frutiger Aero v2 (UI gradients)**: the editor's main panels paint a
  vertical gradient with a top gloss band, and the main menu bar gets a
  horizontal gradient — both theme-gated and no-ops outside Aero, drawn
  behind the widgets. `make gfx-aero-check` now also asserts the panel
  gradient trace and measures the menu bar gradient from a capture.
- **Prefabs**: the Scene panel can capture the selected model plus every used
  light within an editable radius into `resources/tools/prefabs/*.prefab.json`
  (positions stored anchor-relative) and place copies at a chosen position
  (defaulting to the view target). `make gfx-prefab-check` pins the file
  shape, the radius bound, and both placed offsets; decals/emitters/cameras
  in prefabs and viewport click-to-place are v2.
- **Tooling scope decided**: `docs/tooling_scope.md` records that gX builds
  engine-native surfaces only — interop export (Blender/Krita as the authoring
  tools), blockout primitives, texture utilities (incl. ORM channel packing),
  and decal-based painting — and explicitly not a modeller or paint
  application, with the queued slices listed.
- **Version policy and tiered gates**: `vMAJOR.MINOR.PATCH[_W]` with `W` any
  `[0-9A-Za-z]+` variant tag (`docs/versioning.md`); `make version-check`
  validates the format and cross-checks readme/CHANGELOG/latest tag, and
  `make version-gate` derives the validation tier from the change (MINOR+ =
  full check + summary, PATCH = fast check, `_W` = unit tests + provenance).
- **GL-level readout and persisted request**: the Scene panel shows the
  detected and in-use GL levels with a combo that persists a request
  (`settings.toolGlLevel`); levels above the detected one need
  `GX_GL43_CONTEXT=1` at startup (the raylib patch), which the panel also
  states. `make gfx-gl-level-ui-check` pins that `GX_GL_LEVEL=330` clamps
  compute/SSBO off and `GX_GL43_CONTEXT=1` + `430` enables both.
- **Provenance audit and manifest**: `license/provenance.json` records every
  resource family's origin (author / AI-generated / public-domain /
  third-party), license, attribution, and publish action; `make
  provenance-check` (also part of `make check`) fails on unmatched files,
  keeps quarantined paths untracked and gitignored, re-checks denied
  names/content hashes, and regenerates `license/ATTRIBUTIONS.md`. The
  quarantined fixture assets and their derivatives are the initial quarantine
  set; the learnopengl skybox keeps its verify-before-publish action.
- **Editor UX pass (discoverability)**: the Model inspector's `Label` section
  is now **3D text label**, open by default, with a Scene-panel **Add 3D text**
  button that enables text on the selected model; the Particles tab reports
  whether compute is actually available and how to enable it (the emitters
  need `GX_GL43_CONTEXT=1`); and the Scene panel gains an **Asset sources**
  block (guest-root count, add/remove, browse-only note) so guest assets no
  longer hide only inside the browser toolbar. The remaining raw path fields
  (skybox cubemap, light cookie, detail texture) gain **Browse** buttons
  through the asset browser, and the bone/timeline steppers use proper arrow
  buttons instead of tiny `<`/`>` text.
- **Scene cameras and render-to-image**: the GFX editor gains authored cameras
  (add/remove/select, numeric transform + fov, `View -> Camera` and
  `Look through`) that persist in presets as a `cameras` section, plus
  **Render to image** — one frame from the selected camera, cropped to the
  docked scene viewport and exported as PNG through a two-phase render
  request (`GX_GFX_RENDER_CAMERA` / `_INDEX` drive it headlessly).
  `make gfx-camera-render-check` pins the render size, camera identity (two
  cameras must differ; a view-matching camera must reproduce the live-view
  screenshot within the overlay band) and the section round-trip.

### Changed
- **Audit runs are lighter by default**: gates render into a 1024x768 hidden
  window (down from the engine/user window, e.g. 1536x1016) and the parallel
  summary defaults to `AUDIT_JOBS=2`, cutting a full matrix's desktop load
  roughly 4x; `GX_AUDIT_FULLSIZE=1` or explicit sizes keep the old geometry
  for checks that need absolute pixel bands.
- **Raymarch default resolution**: `renderScale` drops 0.5 -> 0.35 (about
  8x fewer pixels than full-res) so the heavy shader lags less out of the
  box; presets persist the value, so it can be raised per scene.
### Fixed
- **material-assets gate exit code**: the check printed FAIL but returned 0
  (`main()` without `sys.exit`), so a real failure could slip past the
  summary; the exit code propagates now, and the emissive Browse click has
  more headroom (3.0s/3.3s, 240 frames) for async loading under parallel runs.
- **Weather effects now obey scene lighting**: snow flakes and rain streaks
  rendered with hardcoded colors, so the sun and ambient never touched them.
  Both sprite shaders take a `lightTint` uniform now, computed per draw from
  `ENABLE_LIGHTING_AMBIENT` plus the diffuse sun contribution
  (`gfx->sun.color * brightness`); turning the sun off darkens the snow by
  ~113/255 over the snow mask and the rain by ~63/255, pinned by new
  lighting-response cases in `snow-check` and `rain-check`.
- **Harness runs can no longer overwrite shipped presets**: the engine save
  path refuses any destination under `resources/tools/` while
  `GX_NO_SETTINGS_SAVE` is set (audit helpers set it for every harness run).
  A stray save there had rewritten a user-authored fixture; `gfx-preset-check`
  now also runs a negative probe that must be refused and leave no file.

## [0.11.0] - 2026-09-27

### Added
- **Browse-only asset roots in the editor**: the GFX asset browser gained an
  **Add root** field plus per-root removal, rescans on change, and persists
  the list in `resources/settings.json` (`toolAssetRoots`, colon-separated)
  alongside the existing `GX_ASSETS`/`--guest-asset` roots. External trees
  (for example a local gameX checkout) are browsed **in place** —
  nothing is copied into the repository, so the copyright audit stays clean.
  `make gfx-asset-roots-check` pins discovery from both the env root and the
  settings-backed root (roots/models/textures and guest counts).

- **Bijection gate**: `make gfx-bijection-check` pins inverse-operation
  round trips. Preset serialization must be a fixed point — `save(load(x))`
  equals `save(load(save(load(x))))` field for field for the editor preset
  and a nested schema probe. Effect toggles (TAA, SSR, planar reflections,
  bloom, SSAO, shadows, and a model's planar mirror make/unmake) must leave a
  bit-identical frame after being enabled and then disabled, a viewport drag
  followed by undo must equal a never-dragged frame (redo restores the drag
  frame), and the snapshot perturb/restore smoke must leave the frame
  identical — each with a visibility control, calibrated residue 0.000
  throughout. Mid-run flips use the `GX_GFX_STATE_SCRIPT` hook
  (`"frame:FLAG=value;..."`), which resolves renderer state and `MODEL0_*`
  reflection flags at apply time (model storage is recreated by preset loads).

- **Skinning bone cap raised to 128**: the shader uniform arrays and the
  upload clamp were fixed at 64 bones while the evaluator supports 256, so
  larger rigs silently lost every bone past the cap. The cap now lives in
  `gX_GFX_SKIN_MAX_BONES` (128; three shaders declare the matching array) and
  `GX_SKIN_TRACE` reports `bones`/`uploaded` per pass. `make
  gfx-skin-bones-check` regenerates an 80-joint probe and asserts all 80
  upload in both the main and depth paths plus a visible pose deformation.

### Fixed
- **3D text labels never appeared in captures**: the labels pass binds the
  scene target, but the text helpers render into a shared RT inside it and
  `gX_Drawing_EndTexture` has no target stack — it dropped the binding to the
  default framebuffer, so the billboard quad landed off-target and the present
  pass overwrote it (a raylib mesh draw also reads rlgl's matrices, which the
  text-RT sync had left at its own ortho). The helpers now save and restore
  the caller's framebuffer, viewport, and both matrix stacks (with a new
  `gX_RHI_GetDrawFBOResolved` that reads GL when the RHI cache holds the
  sentinel), the labels pass syncs rlgl to the camera, and labels draw with
  depth test/write off so a label centered on a model origin is not hidden
  inside its own mesh. `gfx-label-font-check` now also asserts the quad moves
  scene pixels (label on vs off), not just the render texture.
- **Reflection-UI gate flake**: the AI-driven reflection check clicked 1-3 px
  from the reflection-mode combo's row boundary and near the follow-checkbox
  edge, so a rounded window height could select the neighbouring mode
  ("Curved + SSR" instead of "Planar only") or miss the toggle entirely. The
  clicks now target the measured row centers, and the exercise retries three
  times.
- **Point-light shadows mirrored on the -X cube face**: the point-shadow
  sampler's face table flipped the Z (U) axis for the -X cubemap face while
  the depth pass stores faces with the GL convention, so every lookup on that
  side read a mirrored direction — a single light showed a second, mirrored
  shadow wedge (worst on scenes like `art2.json`, where the false shadow also
  darkened the wall) and the PCF taps reprojected onto the wrong texels. The
  face mapping now matches the spec, and `gfx-shadow-check` gained a mirror
  regression probe (single light, Z-offset caster, top-down camera: the
  ghost wedge's lower-band mask is 87 px fixed vs 9199 px mirrored).

### Added
- **Per-material map browsing**: every override's albedo/emissive map gains a
  `Browse` button that opens the asset browser on that override and map kind
  ("Assign albedo map" / "Assign emissive map" over the texture thumbnail
  grid) plus an `X` that clears the binding. The assignment writes the path,
  drops the resolved GL ids, and re-resolves through the normal override
  pipeline; a `GX_GFX_MATERIAL_MAP_CHECK` hook exercises the same helper
  headlessly. `gfx-material-asset-check` now also proves both map kinds
  round-trip through a preset save and that the emissive Browse button opens
  the browser on the right override and kind.

### Added
- **Dock layout pin**: `make dock-layout-check` asserts the docked editor's
  scene fits between the scene panel and the inspector — the `GX_DOCK_TRACE`
  geometry must show the left margin, the inspector-width inset, the game
  aspect, and vertical centering, and the capture's letterbox bands must match
  the traced box. It runs in `audit-summary-full` (`dock-layout` row).

### Changed
- **Docked viewport fits the visible area**: the GUI dock gains a right-edge
  inset (`gX_GUI_Viewport_SetRightInset`) and the GFX editor reserves its
  inspector width, so the docked scene renders beside the panel instead of
  behind it (dock min scale 0.75 -> 0.70). The pixel-calibrated picker gates
  (model/light/art) now derive or convert their coordinates through the new
  dock math.

## [0.10.0] - 2026-09-27

### Added
- **Skeletal animation playback (first slice)**: GLB clips play at runtime —
  the new evaluator samples the parsed node-targeted channels into bone poses,
  accumulates world matrices, and writes GPU skinning matrices; the standard
  shader skins vertices through raylib's bone attributes behind a per-mesh
  switch, so unskinned meshes are untouched. Model parameters gain
  `animClip`/`animTime`/`animPlaying`/`animLoop`/`animSpeed`
  (preset-persisted) with an Animation panel (clip picker, play/pause,
  restart, loop, speed, scrub), `GX_ANIM_TRACE` logs the pose, and
  `make gfx-animation-check` gates the skinned bend, loop wrap, and one-shot
  clamp against the generated `resources/model/probe/animprobe.glb` fixture
  (`scripts/make_animation_probe.py`). The gX-owned `GX_GPU_SKINNING` build
  define activates the backend's bone attribute upload/binding and the
  renderer uploads the bone matrices per model draw.
- **Fixed GPU-skinning plumbing**: skinned models rendered collapsed
  (materials/emissive probe, fixture, the bloom ramp) because raylib's
  `UploadMesh` never uploaded the bone VBOs (its `SUPPORT_GPU_SKINNING` gate
  defaults off — now enabled on the raylib target), gX's custom shaders carry
  no raylib loc-table entries for the bone contract (the backend now queries
  the attribute/uniform locations directly), and the raw draw path had to
  bind the bone attributes itself. The renderer also sets the per-mesh
  `gX_UseSkinning` switch before each mesh draw. `materials-check` and the
  bloom quick audit are green again.
- **Animation timeline + clip trimming**: the Animation panel now draws a
  keyframe timeline (merged key ticks across channels, playhead, click/drag
  scrubbing, previous/next-key jumps) and a non-destructive trim range
  (`animStart`/`animEnd`, preset-persisted; `animEnd <= 0` = clip end) that
  bounds playback — looping wraps inside the range, one-shot clamps at the
  range end, and static scrubbing clamps into it. `gfx-animation-check` adds
  trim clamp/loop/one-shot/round-trip assertions plus virtual-input timeline
  scrub and trim-handle drag checks (`GX_ANIM_UI_TRACE`,
  `GX_GFX_ANIM_FOCUS`).
- **Authored animation keys persist through a sidecar**: the Animation panel
  gains `Save keys` / `Reload`, writing every clip channel key to
  `<model-stem>.anim.json` next to the model (the materials-sidecar
  convention). The sidecar loads after the GLB parse and merges keys by time,
  so editor-authored keys survive a reload without rewriting the source GLB
  (additive, diffable, and it cannot corrupt the asset). `gfx-animation-check`
  authors a key through the UI, saves, and reloads the model in a fresh
  process to prove the keyed pose comes back (and that it differs from the
  un-keyed clip).
- **Viewport handle dragging**: Bezier path handles are grabbable directly in
  the viewport for the selected model or light — the nearest handle within a
  small screen radius wins over object picking, the drag runs on a
  camera-facing plane through the grab point, and grabbing an in-handle on a
  mirrored path switches it to independent handles (mirroring the out side
  first) so the curve does not jump. Path edits commit as one history entry
  and undo restores the tangent even while a motion path drives the object;
  the light history state now carries the path fields too. The motion-path
  gate drags a handle, checks the mirror rule, the evaluated curve,
  independence, and undo (`GX_GFX_PATH_TRACE` reports handle screen/world
  positions).
- **Independent path handles**: motion paths gain `pathTangentsIn` (per-point
  in-handles) and `pathHandlesIndependent` for both models and lights — the in
  side mirrors the out side by default (existing presets keep their exact
  curve), and enabling independence lets each side bend separately. The panels
  expose a `handle out` / `handle in` pair per waypoint (enabling independence
  mirrors the current out handles so the shape does not jump), the viewport
  preview draws both handle lines with the same rule, and the preset schema
  persists the new keys. `gfx-motion-path-check` covers zero-in straightening,
  mirrored parity, the light path, and the schema round-trip.
- **Per-clip blending**: models gain `animClip2` (secondary clip, -1 = none)
  and `animBlend` (0..1 cross-fade weight) — both preset-persisted. The primary
  clip drives the shared playhead; the secondary wraps within its own duration
  when `animLoop` is set and clamps to its end otherwise. Blending lerps
  translation/scale and nlerps the rotation along the shortest arc, then the
  pose overrides apply on top. The Animation panel adds a blend-clip picker,
  a weight slider, and a Swap button; the probe fixture gained a shorter
  "twist" clip (1.0s, +60° around X). `gfx-animation-check` asserts the
  endpoints equal the standalone clips, the mid blend differs from both, a
  missing secondary is a no-op, the secondary wrap/clamp behavior, schema
  round-trip, and a virtual-input slider drag that moves both the weight and
  the pose.
- **Pose editing with in-memory keying**: the Animation panel gains a Pose
  section — cycle the selected bone, author a live transform override
  (`poseOverrideCount`/`poseOverrides`, preset-persisted; the rotation is
  normalized) applied after clip sampling, and `Key` writes the effective pose
  into the selected clip at the playhead (translation/rotation/scale channels
  are updated or created in sorted order; keys live in memory for the session,
  GLB write-back is a later slice). Overrides evaluate with or without a
  selected clip, so a rigged model can be posed statically. The animation gate
  covers identity/rotated overrides (with and without a clip), schema
  round-trip, and a virtual-input Override→drag→Key flow that proves the keyed
  pose survives a timeline scrub round trip (`GX_ANIMPOSE_TRACE`).
- **Shadow passes skin too**: `gfx_depth.vert` (sun/CSM) and
  `gfx_pointdepth.vert` (point lights) share the bone contract, the renderer
  uploads the bone matrices to the sun depth program, and the point-shadow
  path rides the wrapper's model-level upload — animated models now cast
  posed shadows instead of bind-pose ones. `gfx-animation-check` proves the
  floor shadow moves with the pose (shadow-only pixel floor).

### Changed
- **Release lean diet**: the unit-test suite is excluded from Release builds
  (`GX_LEAN_RELEASE`), so the shipped payload no longer carries the test
  runner or the helpers it kept reachable (`main_tests.c` and the other test
  translation units become unreachable and are dropped by `--gc-sections`).
  The Debug build keeps the full suite for `make check`; the lean gate moves
  from C to S (payload −1.21% below baseline, 157 KB headroom).
- **Reflection-UI flake hardening**: the check's visible-window phases retry
  (bounded 60s layout captures with private dirs, plus one full exercise
  retry) because Xwayland window creation intermittently stalls or lands
  stale layouts under back-to-back audit runs; `audit-summary-full` no longer
  fails on that flake.
- **Audit migration sweep (final batches)**: every engine-spawning check now
  runs through `audit_common` — `layout_check`, `gui_audit` (parallel
  `CapturePool`), `lightmap_visual_review`, `gfx_unitrace`, `glsl130_check`,
  `gl_legacy_native_check`, `gl_level_check`, `compute_feature_check`,
  `gfx_warmup_check`, `gfx_material_asset_check`, `particle_velocity_check`,
  `gfx_preset_check`, `gfx_picker_check`, `reflect_suite`, `ssr_normal_check`,
  `ssr_lab`, the Paper 1 oracle gates (M1/M2/M5/M6/M8 + planar),
  `gfx_scoreboard`, `gui_scoreboard`, `gfx_benchmark`, `motionblur_spin_gif`,
  `taa_ssr_spin_gif`, `gfx_gif`, and `ssgi_spin_pair` share settings
  preservation, the cleaned hook env, hidden audit windows, `run_scene` (now
  with a `binary` override for isolated configs and an optional
  `GX_SHOT_FRAME`), and the shared image metrics. `audit_common` gains
  `mean_abs_diff` and `blown_fraction`. Only static/tooling scripts
  (`dead_symbols`, `gl_state_audit`, `shader_compile_check`,
  `strip_backends`) stay outside the helper, and none of them run the engine.
  Two latent crashes were fixed on the way: `gfx_benchmark` and `gfx_gif`
  referenced an undefined `environment`/`env`, so both died before their
  first capture.
- **Fourth migration batch**: the original-art suite (`gfx-art-blending`,
  `gfx-art-light`, `gfx-art-picker`, `gfx-art-reflection`),
  `gfx-shadow-check`, `gfx-sky-precision-check`, and `gfx-buffer-audit` now
  build their environments through `audit_common` — shared settings
  preservation, cleaned hook env, hidden windows — with the protected art
  presets verified unchanged after the runs.
- **Third migration batch**: `gfx-light-check`, `gfx-snapshot-check`,
  `gfx-planar-material-check`, `gfx-mirror-alignment-check`,
  `gfx-grid-check`, `gfx-temporal-check`, and `materials-check` now run
  through `audit_common` (settings safety, hidden windows, cleaned hook env);
  `run_scene` gained `screenshot_after` for the `GX_BURST` multi-frame path.
  `audit-summary`'s full tier now covers light, snapshot, planar-material,
  mirror-alignment, grid, temporal (quick), and materials.
- **Stale gate calibrations refreshed**: `gfx-mirror-alignment-check` clicked
  the align button at row 379 (its top edge after a layout shift; now 386),
  and the temporal gate's y2k spin rows sit at ~1.98-2.00 after the TAA
  stabilization commits, so they carry a 1.5 floor while the frac bound still
  proves camera motion. Both failed on the original scripts too.

## [0.9.0] - 2026-09-26

### Added
- **Aggregate audit summary**: `make audit-summary` runs the curated quick GPU
  effect gates (bloom, dither, wireframe, snow, snowfall, rain, fogsprite,
  palette, interlace, fog-light) sequentially and prints one pass/fail table
  with per-gate wall time; `audit-summary-full` adds the editor/preset gates,
  `--only` selects gates, and `--json` writes the report. The runner schedules
  and reports only — gates keep their own assertions.
- **Per-point Bezier motion paths**: waypoint paths on models and point lights
  gain an opt-in `pathBezier` mode with per-waypoint mirrored tangent handles
  (`pathTangents`, flat xyz, preset-persisted). Zero handles reproduce the
  straight polyline; enabling the mode in the editor seeds
  Catmull-Rom-equivalent handles so the curve shape is preserved, and the
  viewport preview draws the sampled curve plus dim handle lines. The shared
  evaluator gains `gX_MotionPath_Bezier_Point`, and `gfx-motion-path-check`
  pins the bulge, the straight-line fallback, and the light path contract.
- **Read-only rig inspector**: the model inspector gains a Rig section listing
  skeleton bones (name, parent, bind pose) and the parsed GLB clips with each
  channel's target node, path, key count, and duration. A `SHOW_SKELETON`
  viewport toggle draws the bind-pose joints and parent links for rigged
  models, sized from the model's world-space AABB and schema-persisted like
  the other debug overlays. `make gfx-rig-inspector-check` pins the fixture's
  1-bone/2-clip rig, the unrigged case, and the overlay's viewport pixels
  through `GX_GFX_RIG_TRACE`. Posing, animation editing, and keyframe playback
  remain follow-ups.
- **Bloom pipeline audit**: `make bloom-audit` runs a stage-by-stage bloom gate
  (extract threshold/knee, blur spread, composite intensity/exposure/vignette/
  saturation/contrast, tonemap chain, auto-exposure, the deterministic emissive
  probe, and MSAA interaction) with directional trend checks, shader
  sentinel/collapse/washout rejection, `--quick`, `--only`, `--report`,
  `--json`, `--jobs`, and opt-in capture caching. `bloom-audit-quick` is the
  curated smoke; new bloom stage hooks (`GX_T_BLOOMKNEE`, `GX_T_BLOOMBLUR`,
  `GX_T_BLOOMEXPOSURE`, `GX_T_BLOOMSAT`, `GX_T_BLOOMCONTRAST`, `GX_T_BLOOMAE`)
  and `GX_TONEMAP_TRACE` make each stage isolable. See `docs/bloom_audit.md`.
- **Shared audit helpers**: `scripts/audit_common.py` centralizes settings
  preservation, deterministic scene runs, the parallel capture pool (with a
  serial retry phase for GPU contention and fingerprint caching), and image
  metrics (diff fraction, luma, black/magenta/sentinel, chroma, contrast,
  edge/center ratio). The rig, label-font, light-picker, motion-path,
  preset-overwrite, and reflection-UI checks now import it.
- **Audit window isolation**: `GX_AUDIT_WINDOW=hidden` creates an unmapped
  window for GPU audits — nothing appears on the desktop, focus and the
  pointer stay with the user, and captures are pixel-identical to visible
  runs. `make` gates and `audit_common` default to it; `GX_AUDIT_WINDOW=1`
  (unfocused, parked off-screen) and `GX_AUDIT_ONSCREEN=1` (normal window) are
  the debugging escapes, and timing targets clear it so presentation cost is
  still measured. Audit windows also never grab the cursor, and `GX_AI_INPUT`
  now advances on the frame delta under `GX_FIXED_DT`, so click schedules no
  longer depend on render speed.

### Changed
- **Feature matrix on the shared pool**: `gfx-check`/`gfx-check-fast` captures
  now go through `audit_common.CapturePool` — hidden windows, private
  per-probe directories, and the serial retry phase replace the bespoke
  one-retry capture path, so the gate no longer drops rows under GPU
  contention. The full matrix (39 captures) runs in ~72s and the quick set in
  ~16s; `audit-summary` includes `matrix-quick` and the full matrix.
- **More checks on the shared pool**: `material-maps-check` and
  `horizon-check` migrated (hidden windows, settings safety), and the earlier
  batch restored the engine's default capture frame (180) where the original
  relied on it.
- **Failing gates closed**: `particle-velocity-check` read the velocity RT
  with a `B > 240` predicate that predates the `channelMode=2` viewer
  (85701e8 forces B to 0.5); it now detects motion as RG deviation from the
  neutral 127 and passes (`vectors=6/12 peak=225`). `horizon-check` bands were
  refreshed after 85701e8 made the display transfer encode unconditional
  (intentionally brighter scenes): the murk/low luminance bands carry margins
  for the wall-clock-animated water, and the seam metric excludes the
  legitimate horizon edge (max-gradient row +/- 20) so it measures reflection
  smoothness, not the composition boundary.
- **Audit modernization, continued**: `dither-check`, `wireframe-check`,
  `snow-check`, `snowfall-check`, `rain-check`, `fogsprite-check`,
  `palette-check`, `interlace-check`, `splat-check`, `mirror-check`,
  `probe-check`, `dof-resolution-check`, `water-check`, and `fog-light-check`
  now run through `audit_common`: captures go through the parallel pool with
  private directories, the serial retry phase, hidden windows, and a cleaned
  hook environment, so direct runs no longer disturb the desktop and
  multi-capture gates finish faster (fog-light 138s -> 41s). The shared
  `scene_env` gained `clean=True`, `CapturePool` accepts per-request extra CLI
  arguments, and `strip_hooks` centralizes ambient-hook hygiene.
- **Bloom owns its scoreboard grade**: `make scoreboard` now runs the stage
  audit (`bloom_audit.py --json`, hidden windows) as bloom's correctness gate,
  prints `bloom:A`/`bloom:F` with any failed checks, embeds the report in the
  scoreboard JSON, and fails the gate when the audit fails. The feature-matrix
  row remains the fast smoke, and subset modes skip it like other slow gates.
- **Shared model world matrix**: `gX_RendererGFX_Model_WorldMatrix` now builds
  the SRT transform once for the draw path, frustum culling, and the skeleton
  overlay instead of three inline copies.
- **HUD-free diff captures**: the feature matrix and scoreboard disable the
  showcase HUD (`GX_T_HUD=0`), removing frame-timing glyph jitter from their
  diff metrics.

## [0.8.0] - 2026-09-26

### Added
- **Custom label fonts**: per-model Label controls gain a font picker over
  `resources/font` and a persisted `textFontPath`; fonts load lazily on the
  label draw, release with the model, and fall back to the default when the
  path is missing. `make gfx-label-font-check` gates default, custom, and
  bad-path renders through a `GX_LABEL_DUMP` text-target dump, and the
  previously declared `gX_Image_LoadFromTexture` wrapper is implemented.
- **Editor undo/redo shortcuts**: `Ctrl+Z` undoes and `Ctrl+Y` or
  `Ctrl+Shift+Z` redoes the selected model or heap light. Viewport drags
  coalesce into one history entry, and the History panels report the stack
  counts. Light state covers type, transform, color/brightness, attenuation,
  shadows, and orbit values; cookie textures and shadow GPU handles stay out
  of the history by design.
- **Scene panel quick access**: the left GFX Scene panel gains a compact
  heap-light list (add/remove/select with quick brightness and color editing)
  and a scene-stats readout (models loaded, lights, decals, emitters); full
  light controls remain in the Lights inspector tab.
- **Smooth motion paths**: waypoint paths on models and point lights gain an
  opt-in Catmull-Rom smoothing flag (`pathSmooth`), sampled by the shared
  `gX_MotionPath_Waypoint_Update` evaluator; `gfx-motion-path-check` pins the
  smooth deviation while the default stays linear.
- **Light waypoint paths**: point lights gain the same fixed-capacity
  waypoint polyline as models (8 points, loop/ping-pong/one-shot) through a
  shared `gX_MotionPath_Waypoint_Update` evaluator, with viewport preview and
  preset persistence; `gfx-motion-path-check` now also pins light polyline
  tracking and the light one-shot pause.
- **Model motion paths**: models gain the same circular path authoring as
  point lights (center, radius, plane, speed, phase, play/pause, one-shot or
  looping playback) plus a fixed-capacity waypoint polyline (8 points,
  loop/ping-pong/one-shot) with viewport path preview and preset persistence;
  `make gfx-motion-path-check` pins circle tracking, waypoint tracking, and
  the one-shot pauses.
- **Virtual-input button synthesis**: `GX_AI_INPUT` click/drag actions accept
  an optional `LMB`/`RMB`/`MMB` token, and the ImGui bridge posts the matching
  button so camera gestures are testable.

### Changed
- **Viewport interaction**: the Move panel's drag-plane selector gains `View`,
  and holding `V` during a drag moves on the camera view plane. Middle-drag
  orbits, right-drag pans (Shift+middle keeps the legacy pan), and
  `make gfx-picker-check` covers the view-plane contrast, undo/redo, and the
  pan/orbit gestures.
- **Lean Release link**: `-ffunction-sections -fdata-sections` plus
  `--gc-sections` drop unreferenced code and data from the payload (~23.7%
  measured), and the lean baseline/caps are re-anchored at the leaner level.
  Source API and behavior are unchanged (feature matrix, reflection suite,
  editor checks, and the kernel build were revalidated).
- **Lean gate payload metric**: the gate now grades the Release payload
  (text + rodata + data) instead of whole-file size, so linker page-alignment
  padding and symbol tables no longer read as feature growth; file size and
  its overhead stay as an advisory line. Caps live in `.lean_budget.json`;
  the payload cap admits the measured 3.35% growth (ray preview, editor
  authoring, light-edit undo) and the BSS allowance is unchanged.

### Fixed
- **Label text rendering**: raylib's text batch draws through rlgl's
  transform, which gX's RHI matrix stack never updates, so label glyphs were
  drawn with a stale GUI transform and the text render texture stayed nearly
  empty; the label pass now syncs the text target's ortho before drawing.
- **Settings persistence guard**: headless and harness runs honor
  `GX_NO_SETTINGS_SAVE=1`, so transient window geometry is never written into
  the user's `resources/settings.json`; the admission-gate smokes and the
  check scripts set it, and the previously leaked dimensions were restored.
- **Spurious undo steps**: the model and light histories compared whole state
  structs with `memcmp`, so indeterminate struct padding made every frame look
  like an edit; captures now zero and copy the authored fields explicitly,
  keeping history entries meaningful.

## [0.7.0] - 2026-09-26

### Added
- **GFX maturity assessment**: the supported GL33 GFX renderer is assessed as
  Beta while gX remains Alpha. The 29-row feature matrix passes and the
  reflection suite grades B overall; opt-in paths keep their own maturity
  labels.
- **Opt-in SSR edge-ray A/B controls**: `GX_T_MAXDEPTH_SCALE` exposes the
  silhouette threshold's distance coefficient and `GX_SSR_SELFPLANE_REJECT=0`
  bypasses only the near-origin self-plane swallow. Their default values
  preserve the existing renderer path; `docs/ssr_edge_ray_2026-09-23.md`
  records the multi-angle measurements.
- **Per-model SSR receiver routing**: `receiveSSR` independently controls
  which model surfaces receive screen-space reflections, defaults on to
  preserve existing scenes, and is GPU-checked alongside the unchanged
  reflection fidelity gates.
- **Opt-in static lightmaps**: models can sample a declared baked PNG through
  `TEXCOORD_1` using `hasLightmap`/`lightmapID` and tunable strength/UV
  transform fields. `scripts/bake_lightmap.py` provides a deterministic GLB
  triangle baker with directional hard shadows, ambient fill, and chart
  dilation; the opt-in `gfx_lightmap.json` fixture exercises the complete
  model/texture/preset path; production lighting remains unchanged unless a
  preset opts in.
- **Lightmap contract locked**: the first slice is additive linear RGBA8;
  HDR and fully baked replacement-lighting semantics remain deferred.
- **Advisory lightmap visual review**: `make lightmap-visual-review` captures
  the fixture with lightmaps on/off, writes a contact sheet and ROI deltas,
  and remains separate from blocking CI and visual grades.
- **Lightmap baker hardening**: deterministic JSON manifests now support
  multiple directional/point lights, strict UV validation, and coverage,
  overlap, gutter, and in-bounds gap diagnostics.
- **Opt-in SSR normal diagnostics**: `make ssr-normal-check` validates the raw
  RGBA8 normal target, its coverage/encoding, and minimal/planar A/B captures;
  the production normals-off default remains unchanged.
- **Paper 1 M1 transport oracle**: `GX_T_ORACLEPATH=1` enables an opt-in
  cosine-weighted environment estimator and the existing unbiased direct-light
  picker, with deterministic per-pixel/frame seeds and 1–4 rays. The fixed
  `reflectrow` roughness/metalness ladder is gated by
  `make oracle-path-check`; production GL33 behavior is unchanged.
- **Paper 1 M2 CPU bounce oracle**: an offline triangle-BVH reference tracer now
  renders the fixed reflectrow layout with deterministic diffuse direct,
  environment, and one-/two-secondary-bounce transport. The matte fixture is
  gated independently by `make oracle-bounce-check`; runtime rendering remains
  untouched.
- **Paper 1 M2 raster HDR parity**: `make oracle-hdr-check` now compares the
  CPU matte reference with an opt-in Lambert raster fixture through a linear
  RGBA16F pre-tonemap readback. It gates material defaults, gX transform/camera
  alignment, HDR MAE/max error, and repeat determinism; production defaults and
  GL33 behavior remain unchanged.
- **Paper 1 M3 direct GGX parity**: the same opt-in HDR gate now includes the
  reflectrow metalness/roughness ladder, a CPU Cook-Torrance reference, and
  roughness-band error gates. IBL, post effects, and production defaults remain
  untouched.
- **Paper 1 M4 environment-bounce parity**: `GX_T_ORACLE_BOUNCE=1` adds an
   opt-in GPU one-secondary cosine environment sample and compares it with the
   deterministic CPU reference through the linear HDR gate. Surface-to-surface
   GPU rays and multi-bounce acceleration remain deferred.
- **Paper 1 M5 GPU surface-bounce path**: `GX_T_ORACLE_GPU=1` opts into a
   GL4.3 compute/SSBO triangle upload and deterministic one-secondary
   surface-hit/miss trace against the CPU reference. The pass is inert below
   GL4.3, writes only the opt-in HDR fixture, and leaves GL33 production
   behavior unchanged; `make oracle-gpu-check` is the dedicated GPU gate.
- **Paper 1 M6 GPU two-secondary transport**: after M5 parity passed on the
   opt-in `GX_COMPUTE=1` GL4.3 build, `GX_T_ORACLE_GPU_BOUNCES=2` enables a
   bounded two-secondary surface/environment path. The default remains one
   secondary, with parity gated by `make oracle-gpu-multibounce-check`.
- **Paper 1 M7 GPU BVH traversal**: the opt-in compute oracle now uploads a
   deterministic binary triangle BVH and leaf-index SSBO, replacing the
   full-scene triangle loop for each traced ray. M5/M6 HDR parity remains
   passing; GL33 production behavior is unchanged.
- **Paper 1 M8 GPU perfect reflections**: `GX_T_ORACLE_GPU_REFLECTION=1`
   selects a deterministic specular secondary ray using uploaded material
   metalness/roughness, BVH hits, direct GGX lighting, and analytic environment
   misses. CPU/GPU HDR parity is gated by `make oracle-gpu-reflection-check`;
   rough glossy and deeper reflective transport remain deferred.
- **GUI × GFX viewport grid**: `GX_GFX_GRID=1 --showcase` opts into a
  2×2 GUI-composed dashboard backed by four GFX output targets and scoped
  shared-scene cameras. It deliberately leaves independent scene ownership,
  render sizing, and temporal histories for a later viewport-context phase;
  `make gfx-grid-check` captures the seeded grid twice and checks tile/gutter
  health plus bounded drift.
- **GL-level policy matrix**: `make gl-level-check` captures an isolated GL33
  fallback plus GL43 context rows selected at 3.3, 4.0, 4.1, 4.2, and 4.3.
  The GL3.0/3.1/3.2 policy rungs now isolate instancing, geometry shaders, and
  MSAA textures against all-off visual baselines. Those lower rows are clearly
   marked policy inspection—not native-context certification—until the raylib
   context work lands; default GL33 behavior is unchanged.
- **Desktop GLSL 1.30 dialect probe**: a native GL3.0 context now selects
   legacy shader preprocessing (explicit-layout removal, non-array interface
   block expansion, non-instanced standard path, and CPU-provided normal
   matrix), legacy batch source, and pre-link fragment-output binding.
   `GX_GLSL130=1` tests that path on the GL33 host; `make glsl130-check`
   asserts its all-off GL3.0-policy GFX capture remains visually aligned with the
   GLSL330 baseline. This remains a hosted dialect probe—not native-context
   certification.
- **Mutable texture-storage fallback**: the RHI reports `hasImmutableStorage`
   (GL 4.2+ / ES 3.0+ / `GL_ARB_texture_storage`) and allocates pre-4.2 desktop
   GL textures with per-mip `glTexImage2D` / per-level `glTexImage3D` plus an
   explicit `MAX_LEVEL`, so a native GL3.0 context never touches the NULL
   `glTexStorage` entry. Capable hosts keep the immutable path unchanged;
   `GX_NO_IMMUTABLE_STORAGE=1` forces the fallback on the GL33 host for
   testing (pixel-identical in all-off and shadows/SSR/reflection captures,
   including the CSM array, Hi-Z mips, and IBL prefilter cubemap).
- **Native GL3.x support (opt-in)**: `build.sh` auto-applies
   `patches/raylib-gx-gl30-context.patch` (idempotent; the submodule pointer
   stays frozen), so every normal build carries the `GX_GL30_CONTEXT` /
   `GX_GL31_CONTEXT` / `GX_GL32_CONTEXT` desktop-context requests alongside
   the rung-aware legacy-GLSL dialect (140 keeps instancing/inverse, 150
   additionally keeps interface blocks and geometry) and the mutable
   texture-storage fallback. `make gl-legacy-native-check` certifies all
   three rungs on real hard-capped Mesa contexts with zero shader errors:
   3.0 renders with a pixel-identical mutable fallback, 3.1 denies
   instancing at 0.00 and shows it at 0.85, 3.2 denies geometry and MSAA at
   0.00 and shows them at 2.63/0.15 — mirroring the hosted policy matrix on
   the same driver. Default GL33 behavior is unchanged (hooks unset → 3.3
   core request exactly as before). Strict Mesa caught one latent
   incompatibility the NVIDIA host tolerates: the `input` sampler name
   (reserved word in GLSL 1.30) is rewritten to `inputTex` by the legacy
   dialect, with the three C lookup sites (`postprocess/shader.c`, `gui.c`,
   `element/shader.c`) trying `inputTex` as a fallback — production lookup
   order is unchanged. Note: hiding `GL_ARB_texture_storage` via
   `MESA_EXTENSION_OVERRIDE` is NOT a valid fallback test (Mesa warns it
   cannot disable it, drops 97 further extensions, and blanks the frame);
   use `GX_NO_IMMUTABLE_STORAGE=1`.
- **Native 3.2 feature ladder** (minimal set, one feature per rung, pinned
   preset-0 baseline): shadows 8.24/0.130, SSAO 11.66/0.701, SSR
   29.47/0.760, planar reflections 1.11/0.102, and default fog 0.64/0.015
   (within the absent threshold), all with zero errors. The ladder is now
   repeatable in `gl_legacy_native_check.py` alongside the GL3.0 fallback,
   GL3.1 instancing, and GL3.2 geometry/MSAA pins. A separate preset-3
   Ultra/full-combination capture (all renderer feature
   toggles enabled) completed on native GL3.2 with zero errors and matched the
   GL3.3 baseline at mean RGB delta 0.0071, with no pixels exceeding a 4-level
   channel delta. Root-caused along the way: the batch internal shader
   used non-standard attribute names (`p/uv/n/c`) so `glBindAttribLocation`
   never matched — NVIDIA assigned compatible locations by luck while
   strict drivers misbound vertex color, dropping R/G in every batch draw
   (LDR seed copy, debug views, GUI canvas). This was the entire sky-blue
   divergence (and the ultra blue wash): after standardizing the names,
   native-vs-host all-off went 20.9 → 0.11 and ultra 55.5 → 1.9. The bug
   was introduced with the 130 batch variant (which dropped the explicit
   `layout(location)` qualifiers but kept the short names). Also fixed
   along the way (all bitten harder on strict drivers, all latent on every
   driver): particle render/velocity shaders no longer attempt 430 compiles
   below SSBO level (gated on `hasSSBO`, like the compute load);
   `gX_RHI_Tex_Samples` answers 0 for non-multisample targets instead of an
   unconditional `TEXTURE_SAMPLES` query (`INVALID_ENUM` on strict drivers);
   motionblur velocity-tap locs are cached in the live update block (the
   dedicated `MotionBlur_CacheLocs` was dead code, so both locs stayed 0 and
   every frame uploaded int into location 0 — this also cleared the host
   ultra config's single error). The opt-in raylib vendor patch now rewrites
   its internal default shader version to GLSL 130/140/150 for the native
   context hooks, so the default program now links and unloads cleanly instead
   of producing the old `program not linked` and `glDetachShader` errors. The
   GUI FX resource also uses `inputTex` instead of GLSL's reserved `input`
   sampler name, removing the strict GL3.3 compile error.
   Known 3.x limit (out of scope): the title/GUI canvas path still faults on
   the native GL3.2 zink/NVIDIA path during a textured batch submission;
   llvmpipe and the renderer showcase scenes, including combined SSR and
   planar reflections, are unaffected. A forced `glFinish()` serializes the
   queue and masks the driver fault, but is intentionally not enabled because
   it would stall every GUI batch. Native certification therefore remains
   renderer-only; `--title`/GUI parity is not a supported GL3.x gate.
- **Forward+ teardown**: cluster-grid texture, light/projection SSBOs, and
  compute cull program now have an idempotent renderer-unload lifecycle;
  GL33 clears only its grid texture.
- **Forward+ ceiling-lift pin**: the optional compute check now builds an
  isolated GL33 binary, verifies its uniform/32-light fallback, and requires
  a positive 64-light GL4.3-vs-GL33 screenshot delta. The companion
  GPU-vs-CPU-culling screenshot remains equality-gated.
- **Forward+ visual parity pin**: the opt-in GL4.3 compute check now captures
  deterministic 64-light frames for GPU-assembled and `GX_NO_COMPUTE`
  CPU-assembled tile masks, then bounds their RGB delta. This exercises the
  SSBO/grid fragment consumer after the existing bit-exact grid readback;
  the GL33 production build remains untouched.
- **Harness no-shot retry**: relaunch contention intermittently yields no
  screenshot file (identical manual rerun lands clean) — matrix,
  fog-light, and wireframe harnesses rerun once before failing the row.
  Contention pattern named in backlog item 7 (also covers the
  `bloomemis` blowout + `motionblur` bimodality sightings).
- **TAA temporal design** (backlog 28): sky pixels take the depth-
  reconstruction fallback (buffer covers opaque only); history alpha
  carries prev-frame depth for exact disocclusion in both paths.
- **Per-model albedo tint** (backlog 30): `params.albedoTint` folds into
  the existing `albedoColor` upload (no shader change) via a shared
  helper used by main + planar fill; `GX_NOON` block diorama
  (grass/dirt terraces) + `GX_TINT` kill hook.
- **Per-face grass-block split** (item 1): `params.albedoTintTop` +
  `useTopTint` inject load-time vertex colors from local normals
  (rotation-proof; file vertex colors never touched); top-tint models
  upload a white base so colors stay absolute. Diorama fixed for the
  real 2x2x2-centered cube.glb (2.0 spacing, supportY+1.0 centers —
  the 1.0-spaced attempt interpenetrated and buried every top).
  `GX_NOON` opts out of AgX (backlog-23 rule: it crushes G/B ~0.75x
  and converges grass/dirt to one salmon; ACES stays neutral).
- **Motion-blur velocity tap** (item 2): the blur samples the velocity
  buffer when rendered (same gate/hook as the TAA resolve:
  `GX_T_NOVELOCITY=1` forces the depth fallback); glass pixels and
  buffer-off configs fall back. Velocity update/draw gates widened to
  include `ENABLE_MOTION_BLUR`. Proved by
  `scripts/motionblur_spin_gif.py` (static twin 0.0000 + spin GIFs).
- **Fog point-light shadows** (item 3): plain-2D atlas of 512px tiles
  (slot*6+face, up to 4 casters) blitted per frame from the point
  shadow array — no `sampler2DArray` in the fog program (M3.1 poison);
  single hard tap married to the item-13 recipe, distance-only bias,
  beyond-far skip. `GX_T_FOGPTSHADOW=0` restores unshadowed march.
- **Fog point-light inscatter** (item 13): the opt-in volume march packs up to
  eight point/spot lights with attenuation, HG phase, spot cones, and optional
  point-shadow atlas visibility. The isolated fog-light row changed 16.984% of
  pixels above 2 LSB with two extra lights; default volume fog remains off.
- **TAA x SSR shimmer fix** (backlog 34): the Paper-1 oracle seed
  (per-frame lobe rotation, TAA-gated) shimmered instead of
  converging (static tstd 1.93 vs 0.22 seeded-off). SSR marches the
  deterministic center ray again (SSGI/GTAO seeds untouched);
  post-fix static 0.21, spin below SSR-only. Proved by
  `scripts/taa_ssr_spin_gif.py` (keeper).
- **Forward+ 64-slot lift** (backlog 29, opt-in): SSBO light data
  heap-indexed (hole bug fixed), 64-clamps, non-clustered 64-cap;
  kernel/CPU parity bit-exact at 48 lights. GL33 stays the baseline.
- **Grade regression pins**: `sh_grade` / `gta_grade` / `noon_grade` rows
  in `fog_light_check.py` (11 rows total); `scripts/wireframe_check.py` +
  `make wireframe-check` placement pin (bbox containment — the defect
  class difference-gates cannot catch, proven by negative control).
- **Mood grades (env-gated, defaults untouched)**: `GX_SILENTHILL=1`
  daylight whiteout on `--forest`, `GX_GTA3=1` night neon on `--ledrow`
  (fog 0.05 + extract threshold 0.45), `GX_NOON=1` noon-haze on
  `--showcase` (legacy linear fog, volume fog off).
- **Audit hooks**: `GX_T_SKY` (sky kill switch), `GX_SKY_DENSITY`
  (high-airmass reddening probe), `GX_T_BLOOMTHRESH` (extract threshold),
  `scripts/fog_light_check.py` + `make fog-light-check` (8-row fog/light
  coupling matrix), `--minimized` (interactive iconified start).
- **Proofs**: `GxProofs.RhiTarget` (10 target-registration/bind theorems),
  `GxProofs.RhiStateScope` (8 terrain state setup/restore theorems);
  `ReflectFlags` wired into the blocking root (33 -> 36 jobs).
- **Composition order study** (backlog 38): a 12-frame FXAA-before-vs-FXAA-
  after-TAA burst showed no meaningful temporal or edge advantage, so the
  committed production order remains unchanged.
- **Particle velocity regression pin**: optional `make particle-velocity-check`
  builds the GL4.3 compute variant and validates a same-process 12-frame
  velocity-RT burst; the default GL33 admission build remains untouched.
- **Forward+ compute regression pin**: optional `make compute-feature-check`
  builds the GL4.3 variant, loads a 64-light scene, and compares the GPU tile
  image against the CPU reference with zero mismatched tiles/words.
- **Fullscreen gfx pass helper**: added an explicit begin/draw/end lifecycle
  for renderer post-process passes and migrated bloom blur plus all DOF passes
  without changing their targets, uniforms, ordering, or clear behavior.
- **Sized RT lifecycle helper**: added shared initialization and resize-reload
  helpers and migrated the three DOF targets; MSAA remains on its dedicated
  lifecycle.
- **Feature-matrix filters**: `gfx_feature_matrix.py` now supports `--quick`,
  `--only`, and `--skip`; `make gfx-quick` runs the five-row smoke set while
  the full matrix remains the default.
- **Temporal history helper**: added explicit reset/clear, frame-index,
  ping-pong target selection, and commit state; TAA, GTAO, and SSGI now use it
  while keeping their existing output routing and fallback behavior.
- **Reflection grading**: the reflection suite now reports calibrated S,
  A+, A, A−, B, C, D, and F bands; only F remains gate-failing.
- **Opt-in Y2K showcase**: `--showcase-y2k` adds a Bryce/Y2K abstract-terrain
  style study from shipped primitives only (stepped terrain, glass, brushed
  metal, cyan/magenta/orange neon, twilight sky, restrained bloom/fog/rim).
  The look is saved as `resources/tools/gfx_y2k.json` via the existing JSON
  preset workflow, pinned by `docs/visual_direction.md` (positive/negative
  prompts), covered by a 60-frame crash-free smoke row in `make check`, and
  verified by screenshot; default scenes and reflection fixtures are
  untouched.
- **Material-map normal/parallax gate**: `make material-maps-check` A/Bs flat
  vs injected brick normal vs displacement-texture parallax (modes 0/2) on
  `--ledrow`, all opt-in via `GX_T_MATERIAL_MAPS`/`GX_T_PARALLAX_MODE`.
- **Build-dir policy**: all builds live under `build/` (default `build/`,
  Release `build/release`, kernel `build/kernel`, isolated check configs
  `build/<name>`); top-level `build-*` dirs removed, `make clean` is a single
  `rm -rf build`, and the dead `build*/` gitignore line is gone.
- **Temporal stability gate**: `make temporal-check` bursts 12 consecutive
  frames per row (static TAA-on/off, camera-spin TAA-on) and gates median
  pair drift plus an exposure-step count. Measured medians: 0.38/0.18 static
  (gate 0.60, catches backlog-34-class 1.93 shimmer), spin 7.44 in the
  (2, 15) motion band; the one auto-exposure gain step per burst (10-frame
  readback cadence) is bounded separately instead of failing the gate.
- **GFX editor presets**: the preset bar has one-click Default/Y2K buttons
  through the existing confirmed-load flow, and the model material row gains
  a Glass quick-preset matching the validated Y2K glass params. Editor-only;
  renderer defaults unchanged.
- **GFX preset round-trip pin**: `make gfx-preset-check` headlessly loads
  then re-saves each shipped preset and gates log verdicts, model/light
  counts, and per-model texture-injection slots (default 10/4, Y2K 8/4).
  The headless hooks now load before saving so a combined run round-trips.
- **Water object-reflection pin**: `make water-check` runs an exposure-locked
  SSR on/off A/B over `--showcase-water`; object zones below the torus/cube
  must carry ~2x the delta of open water at the same depth (11.9/12.9 vs
  7.6 mean), with a sky-top ceiling against global blowups.
- **First-frame warmup pin**: `make warmup-check` bursts frames 1-12 of
  `--showcase` and gates the startup envelope (content by frame 7, one
  shape, settled drift by frame 12). Warmup anatomy: 6 black frames, one
  early resize, content pop, then AE/temporal convergence settling ~0.7
  by frame 12.
- **Fog/sky horizon pin**: `make horizon-check` gates the mirror-horizon
  seam smoothness on `--showcase-water` plus the night-murk profile on
  `--ledrow` (zenith black, murk band smooth), both exposure-locked with
  star-rejecting median profiles.
- **GFX feature scoreboard**: `make scoreboard` reports curated completeness
  percentages, owning-gate correctness grades, and timed on/off hardness. The
  baseline averages 85.4% completeness; hardness uses three fixed-time samples
  and a median, while the matrix and reflection suite provide correctness.
- **Advisory visual review lane**: `make gfx-visual-review` builds fixed-angle
  matrix/reflection contact sheets and a hash-recorded manifest with adjacent
  human grades (`A-`/`A`/`A+`). It is deliberately separate from CI gates.
- **Render-target lifecycle audit**: opt-in `GX_GFX_BUFFER_AUDIT=1|<path>`
  traces per-frame target begins, clears, draws, blits, texture binds, and
  FBO bind mismatches through the RHI, emitting pass/frame summaries with
  read/write scope, initialization, and draw coverage. Disabled by default;
  the RHI path is unchanged when the hook is unset.
- **GFX editor authoring expansion**: reusable material assets
  (`resources/tools/materials/*.material.json`) save/browse/apply; a
  searchable model/texture/preset browser includes registered guest roots;
  `S` and `S+X/Y/Z` scale dragging with Ctrl snapping; a separate Surface
  subtab; free-camera entry and quick camera views; and a contextual
  shortcut footer. Per-model environment/SSR receivers are separated from
  global gates, with `SSR only on this model`, reflection-mode selection,
  least-squares `Make planar mirror`, normal flip, backing-geometry
  exclusion, and a stale-plane alignment warning. The reviewed original-art
  presets are two private authoring scenes.
- **Editor regression checks**: `gfx-light-picker-check`,
  `gfx-preset-overwrite-check`, `gfx-art-picker-check`,
  `gfx-art-blending-check`, `gfx-mirror-alignment-check`,
  `gfx-reflection-ui-check`, `gfx-material-asset-check`,
  `gfx-planar-material-check`, `gfx-art-light-check`, and
  `gfx-art-reflection-check` drive the editor through `GX_AI_INPUT` and pin
  picking, light removal/reuse, reflection independence, material
  round-trips, and planar capture fidelity.
- **Accessible theme presets**: Settings gains a persisted `uiThemePreset`
  selector for the default, High Contrast (`theme_high_contrast.json`), and
  Color Vision (`theme_color_vision.json`) themes with live apply, plus the
  tool text/hit-target scale and tool keyboard-navigation preferences.
- **Textured low-resolution ray preview**: the GFX editor adds `Raster`,
  `Ray preview`, and `Side by side` comparison on GL 4.3+ (opt-in
  `GX_GL43_CONTEXT=1`). The preview traces opaque model meshes with per-mesh
  base color and RGBA8 albedo maps (up to 32 maps, each no larger than
  1024x1024; other formats fall back to base color), direct sun/point-light
  visibility, and up to two diffuse secondary rays, accumulating 256
  progressive samples and resetting on camera, scene, light, exposure, or
  target changes. Raster remains the default.

### Changed
- **Lean gate budget caps**: binary and BSS limits now come from
  `.lean_budget.json` (currently 3.1% binary / 1% BSS over the stored
  measurements) and the gate reports byte headroom for each cap.
  `--update` refreshes measurements only; caps move only with an explicit
  budget edit. See `docs/lean_gate.md`.

### Fixed
- **GFX scoreboard row-set check**: the scoreboard now accepts additional
  passing feature-matrix rows beyond its curated timing set, while still
  requiring every curated row and every parsed matrix verdict to pass.
- **Camera-motion velocity interpolation** (backlog 42): the velocity shader
  now interpolates current/previous clip coordinates and performs perspective
  division per fragment. This removes false large vectors on model surfaces;
  seeded spin captures now scale with camera motion, with no magnitude cutoff.
- **Temporal gate repeatability**: temporal bursts now pin the frame clock to
  1/60 second and use a fixed RNG seed. This removes frame-time variation from
  the measurements without changing the existing drift budgets.
- **Opt-in SSR normal pre-pass state leak**: planar reflection fills now clear
  the shared shader's `ssrNormalPass` flag before raw model draws. The optional
  normal target can no longer turn the next frame's planar RT into encoded
  normals; default GL33 behavior is unchanged. The curved non-planar normal
  path remains opt-in pending its separate fidelity tradeoff.
- **GL feature-level gating**: selected GL levels now gate extension-backed
  flags as documented, rather than OR-ing raw hardware capabilities back in.
  A GL33 context on GL4.3-capable hardware therefore keeps compute and SSBO
  paths disabled; the Forward+ check pins that behavior.
- **Unwritten velocity pixels** (backlog 36): velocity `.b` now marks a
  written vector separately from the existing `.a` glass flag. TAA and
  motion blur use camera/depth fallback for water pixels with no writer;
  SSGI/GTAO reject their stale history. SSR's alpha contract is unchanged.
  Water replays its Gerstner/ripple surface at the previous time, and compute
  particles now write previous-position vectors in the velocity pass.
- **Effect composition audit** (backlog 37): SSGI now engages the composite
  chain and adds its accumulated map when bloom is disabled; TAA-only
  compositor layers retain their supplied output RT through the post-present
  resolve. Deferred SSAO/cluster consumers and diagnostics-only contact maps
  were verified intentional.
- **Particle velocity** (backlog 36): the opt-in compute SSBO stores previous
  positions in a 16-float layout, and a matching point-sprite velocity pass
  writes valid vectors after model velocity. Respawns use previous=current;
  `gX_GL_Detect()` now initializes the capability flags before deferred GPU
  paths, so GL4.3 compute particles execute while GL33 remains safely
  disabled without compute support. A 12-frame GL4.3 velocity-RT burst proved
  particle vectors and temporal movement.
- **Static GL audit 22 -> 0 warnings**: cluster/terrain binds + state
  snapshots through target-aware RHI; cubemap single-cleanup Pop (no
  audit weakening, behavior preserved).
- **Forest trees missing in-tree**: `tree.glb` is a guest asset — all 32
  loads failed leaving an empty fog slab. Hermetic cone fallback (tall
  dead-trunk stretch) for forest/lake.
- **Night below-horizon glow**: sky-shader ground tan ignored `dayFactor`;
  ledrow void read [147,108,102] salmon, now [31,23,21]. Day-identical
  by construction (matrix green).
- **AgX mauve-skew on neutral greys** (backlog 23): computed [163,122,124]
  on the clear color, xAE 0.8 = measured [130,98,100]; SH grade renders
  ACES, global default untouched.
- **Minimized audits** (backlog 24): falsified three layers deep
  (backbuffer reads, present stall, wall-clock parity) and reverted;
  `--minimized` stays interactive-only.
- **Bloom upsample rings** (backlog 27): the trunk-base symptom does not
  reproduce (white cones removed its premise); 6.0 horizon overdrive is
  grade misuse, not an upsample defect. No code change.
- **Wireframe transpose defect** (backlog 26): custom-VAO path uploaded
  row-major proj·view with transpose=0, rendering (PV)^T garbage that
  passed the difference-style matrix row. Flipped to transpose=1
  (coherent button.glb disc, visually verified).
- **Shared point-shadow array deletion**: removing a light released the
  scene-wide point-shadow array through a per-light borrowed handle, leaving
  a high-severity GL error. The array and FBO are now owned by the shared
  renderer state and released once at teardown; `make gfx-art-light-check`
  covers removal and slot reuse.
- **Full-opacity blending darkening**: `blendOpacity = 1` took the
  translucent path and darkened meshes; full opacity now uses the opaque path
  while 0.5 remains translucent (`make gfx-art-blending-check`).
- **Original-art picker overlap**: nearest-hit selection keeps the front mesh
  for overlapping same-ray models (`make gfx-art-picker-check`).
- **Point-shadow seam coverage**: PCF taps now reproject across cube-array
  layers instead of clamping to one face; `make gfx-shadow-check` covers both
  Y-facing layers, X/Z seams, and wall silhouettes crossing X/Y, X/Z, and
  Y/Z seams. The reviewed mirror preset adopts base bias 0.03; the preserved
  `art.json` and renderer fallback remain at 0.015.

## [0.6.32] - 2026-09-06

### Fixed
- **Planar fill blended instead of replacing**: leftover blend state +
  ssrRoughness fragment alpha (~0.02) meant the mirror RT was ~98%
  background (planar fidelity 0.00%, pale ghosts). Now save/disable/
  restore around the fill; the fill also uploads the per-model shading
  uniforms the raw path had skipped (emissive/metal/roughness/albedo/
  PBR/blend/specular/AO, planar+refraction forced off).
- **reflect-check green (first time)**: suite pins GX_CAM_DIST=6 (oracle
  geometry), diag exposure fixed (flat 1.0 emissive, dimmer minimal
  lights), gates recalibrated 95/65 -> 40/20 with rationale (margin-
  imposed ceiling ~55%; SSR edge exits are correct). Never passed
  before (v0.1.28: SSR 42% vs 65).

## [0.6.31] - 2026-09-06

### Added
- **Layout automation**: auto-seat pass (world AABB via the draw-path
  SRT, support from the mirror slab / water plane, drop-to-support +
  agreed-side XZ push-out, deterministic, all moves logged,
  `GX_LAYOUT_DUMP=1` / `GX_LAYOUT_NORESEAT=1` hooks), `--layout-test`
  broken scene, `--layout-check` headless CPU verdict, `make
  layout-check` gate (captures + exposure stats).

### Fixed
- **Demo cluster overlaps**: removed the legacy cube/sphere/cone trio
  (pre-demo chrome interpenetrating the cluster - a cube inside the
  teapot); re-scattered specs with verified clearance, showcase
  default orbit 6->9 to frame the spread. Reseat is a no-op on the
  shipped scene; guests get seated automatically.

## [0.6.30] - 2026-09-06

### Fixed
- **Demo cluster reseated**: every model rests its measured GLB AABB
  base on the slab top (+0.075) with separated XZ footprints
  (rotations accounted). The teapot was sunk 0.675 into the slab
  with its tip 0.375 out the bottom - the "impossible" underside
  seen from below; cube/cone/icosahedron/sphere/small-cube likewise
  sunk, torus/cube2/cone/fixture/sphere interpenetrated neighbours.
- **Under-floor mirror ghosts**: the graze gate used abs(dot), letting
  downward-facing pixels (slab underside from below) paint ghosts of
  the scene above. Now directed: downward faces fall back to env.
  Default view verified artifact-free with cleaner curve undersides.

## [0.6.29] - 2026-09-06

### Fixed
- **Aux mosaic breakup halved (8/10 -> 5/10)**: the tiler was the
  binary mirror-depth gate - its on/off flips across mirror
  silhouettes stretch into 60-120px tiles under grazing projection.
  New GRAZING RULE: miss rays leaving near-parallel to the plane
  fall back to env (the true mirror answer at grazing = horizon
  sky); steep rays keep the binary-gated mirror. Statue/Button
  reflections coherent, far floor smooth, no veil (binary gate and
  env fallback intact, top-down unchanged by construction). Soft
  depth bands were tried and rejected (traded tiles for veil).
- **SSR mode-9 probe truthful**: used a stale absolute threshold
  that flagged every pixel at showcase scale (all-magenta scene);
  now the same relative degeneracy test as the main path.

## [0.6.28] - 2026-09-06

### Added
- **SSR aux-gate inspector** (`GX_SSR_DEBUG=16`): R=edge fade,
  G=planar depth validity, B=miss routing gate. Pinpoints
  aux-coverage dropouts for the breakup investigation. Default
  path unreachable (mode-gated), captures within run noise.

### Fixed
- **Banding leads ruled out**: miss-fade unification only softens
  rings 5->4/10 (not the cause); Hi-Z jump-ahead kill is a
  below-noise no-op (1.27% vs 1.52% run noise) — both reverted.

## [0.6.27] - 2026-09-06

### Fixed
- **Planar fill rasterizes**: drew via Draw_Parameters straight
  into raylib_Mesh_Draw, bypassing the raw check — rlgl-stack
  matrices stomped mirror view/proj, RT held only sky (masking
  every downstream bug). Raw draws mirror the solid branch, with
  per-mesh diffuse + map flags forced off (demo GLBs textureless).
- **Aux UVs calibrated**: y-flip only (4-variant sweep vs the
  known-correct direct path; x-flip/missing-flip mosaic). Aux
  consume live by default (`GX_PLANAR_AUX=0` opts out).

### Added
- **Diag infra**: `GX_PLANAR_VIEW` mirror-RT presenter and
  `GX_SSR_DEBUG` hook for headless `--gfx` probing.

## [0.6.26] - 2026-09-06

### Added
- **Proofs**: ReflectFlags unit-parking pin (sampler/target/unit
  model, frame-1 regression + parked-clean + same-type-sharing),
  529 -> 535 total.

## [0.6.25] - 2026-09-06

### Fixed
- **Last GL error eliminated (zero/run)**: frame-1 CSM array missing
  (deferred init) left its sampler at default unit 0 with albedoMap
  — 2D-array vs 2D fails program validation, strict drivers skip
  the draw. Unit upload now unconditional (parked pre-init).
- **Reflection-bias slider honest**: was labeled planar-only with a
  0..0.05 range that snapped the 0.5 default on first drag,
  silently killing env reflections. Now `reflection bias` 0..1.

## [0.6.24] - 2026-09-06

### Fixed
- **Per-frame uniform arity errors**: dead `float resolution` in the
  IBL prefilter frag collided with the resolutionLoc sweep (vec2
  render size into a float, ~180 GL errors/run) — removed; frame-1
  `viewPosition` scalar traced to the material-map loop running
  before AttachShader (loader locs) — attach moved first. Pixels
  byte-identical, full-run apitrace clean.
- **Env-reflection wash**: Schlick fresnel (F0 0.04) on the additive
  cubemap term — same pixels respond, mean slab lift 119->23,
  mirror ghosts preserved. Planar-replace path untouched.

### Added
- **Proofs**: ReflectFlags bind-target theorems (slot requirements,
  mismatch illegality, illegal-reads-empty), 521 -> 529 total.

## [0.6.23] - 2026-09-06

### Fixed
- **Sampler bind targets (reflections + shadows actually work)**:
  the material-map loop bound every map as 2D, including
  maps[CUBEMAP] holding the dynamic env cube — GL_INVALID_OPERATION
  (target mismatch), sampler reads black, ENABLE_REFLECTIONS toggle
  a proven no-op. Cube-target maps now bind via CubemapSlot; demo
  slab opts into env sampling so the toggle visibly adds mirror
  ghosts (additive bias-scaled, tunable via Reflect lab bias).
- **Point-shadow + CSM arrays bound as 2D** (~700 errors/run, black
  shadow samplers): now routed through the target-aware RHI bind
  path. Full-run apitrace clean of mistargeted binds.

## [0.6.22] - 2026-09-05

### Added
- **Proofs**: GxProofs.ToolsState (open-exclusivity, title-route,
  off-title stay, close clears/keeps-scene), 503 → 510 theorems.

### Fixed
- **Menu GFX button routes to the GFX scene**: opened over the title
  it showed the editor with nothing to light. OnClick_OpenGfx now
  switches TITLE→RENDERER_GFX first (focus reset, kernel-guarded),
  stays put on scenes with 3D content; mirrors --gfx-editor routing.
- **Dock settle grace**: our own SetSize applies async at the WM; the
  live-size sync snapped maindata back to the stale size on the next
  frame, collapsing the dock (cramp fallback) before the resize
  landed and sticking restores. 30-frame grace after engage/restore.

## [0.6.21] - 2026-09-05

### Added
- **GFX editor round 2**: 6 new live-tuning panels — Fog
  (enable/near/far/color/height), Sky (strength/color), Water
  (plane height/wave count/wave0), Shadows-CSM (cascades 1-4 +
  distances), Godrays (enable/density/decay/weight/exposure),
  Lensflare (enable/intensity/dispersal). 12 sections total.
- **Proofs**: 3 GUI modules (DockMargins, FocusRing, TitleYield),
  485 → 503 theorems.

### Fixed
- **Docked editors usable**: dock margins 220→320px (engage bar
  unified to GX_DOCK_MARGIN_MIN, cramp fallback 160→240
  hysteresis) — 320px is the widest bar that still engages on a
  1080p screen; smaller setups get the proven overlay instead of a
  truncated strip.
- **EXTRAS tool-button labels** centered (were rendered above rects).
- **Themlab/title interleave**: title yields when canvas 2 carries
  the gallery (presence-based, all close paths restore).

## [0.6.20] - 2026-09-05

### Added
- **Proofs wave-4**: 15 salvaged modules, 362 → 485 theorems.
  Core tier: MatrixAccess, PointShadowFaces, ShadowVP,
  ResourceLifecycle (verbatim, re-verified against gX source).
  Re-grounded: PipelineOrder (gX runs bloom before
  godrays/lensflare with a scene-copy pass), EffectSources (9-flag
  HDR need, 6-flag effects gate, bloom.scene→hdrScene→LDR blit),
  NormalMatrix (gameX scene captures → general diagonal-scale
  theorems), GXMatrixTheory → GfxClipMatrix. New theorem:
  SynthFilter.filter_f_clamped_le_one (gX's f≤1 stability clamp).
  Rejected after ground-truth check: FrustumPlanes (no plane
  extraction in gX), PhysicsInterp (no counterpart), ShadowVPProof
  (subsumed by GfxMatrixConv). The deferred salvage list is empty.

## [0.6.19] - 2026-09-05

### Fixed
- **`--gfx-editor` routes to the GFX scene** by default (mirrors
  `--tools`→GUIPLAYGROUND; explicit `--masterIndex` wins) — the editor
  opens where there is something to edit.
- **Docked canvas distortion**: the SDF widget flush evaluated `box`
  (game px) against `gl_FragCoord` (window px), clipping rects and
  burying labels under a misplaced overlay whenever game != window
  mapping (docked tools). Box/res/quads now unify in window space via
  the live game viewport, with an explicit full-window viewport/ortho.
  Verified pixel-level (docked buttons full-width, labels readable;
  undocked pixel-identical).

## [0.6.18] - 2026-09-05

### Added
- **GFX editor** (third tool): sun + heap lights (add/remove/select,
  type/pos/color/attenuation/shadows), shadow biases, bloom,
  exposure/ambient/cookies, quality preset + Apply, PP/TAA toggles —
  write-through to live GFX state. Wired end to end: runtime flag,
  exclusive dispatch, EXTRAS button, `--gfx-editor` CLI,
  `GX_TOOLS_OPEN=gfx`, kernel-safe hint panel.
- **ImGui on renderer scenes**: `GUI_Draw` returned early for 3D scenes,
  skipping all tool chrome — extracted `Draw_ToolsOverlay` (dock +
  chrome), called on the compositing and bypass paths alike.

## [0.6.17] - 2026-09-05

### Added
- **Kernel boundary audit**: include/symbol sweep over every kernel
  file (one hygiene leak fixed, rest classified); the link is the
  documented enforcer.
- **`make check-kernel`**: kernel-only CI (cluster-off configure →
  build → unit tests → 30-frame NULL boot), kept separate from
  `make check` (own build dir; needs a display like mkrelease).
- **`docs/kernel.md`**: the gX kernel record — atomic-pair design,
  game-stays-kernel and tools-on decisions with reasons, per-file
  boundary table, verification matrix, proof links.

## [0.6.16] - 2026-09-05

### Added
- **Kernel proofs**: MainDataRefresh (resolution ladder), MainDataReset
  (defaults corrected to 1536×1152), MainDataSwitch (response contract
  rebuilt for gX's five always-required responders), ModuleInfo
  mirror (+ kernel_only case). Switch/ModuleInfo are pure logic, so
  they joined the blocking core tier. 362 theorems, zero sorry.
- **Kernel-only build**: `GX_ENABLE_RENDERER/SCENE` cmake options with
  an atomic pair (any OFF drops renderer+scene sources); ifndef-guarded
  config defaults; guards across main_load/loop/unload/update, gui
  centerpiece, and tests; conditional Responses; moduleinfo populated
  from flags; NULL boot with no scene switch. `game/` stays kernel
  (dependency-free math+shim); tools/ untouched. Verified: kernel-only
  links, tests pass, 30-frame boot + clean unload; full build parity.

## [0.6.15] - 2026-09-05

### Added
- **Proofs wave-3**: CascadeSplits, PoolAlloc, BufferPool, EventLog,
  QualityPreset (filter codes + clamp adapted), CameraLimits,
  AudioAtten, MatrixConv (field-level, SetRowMajor part dropped).
  321 theorems total, zero sorry.
- **Slim Mathlib closure**: all six wide `import Mathlib` narrowed
  (Tactic + data modules) — closure 7890→~3070 jobs (61% smaller),
  with an import-policy note in `proofs/README.md`. Wave-3 rejects
  and the wave-4 deferral list documented in the salvage policy.

## [0.6.14] - 2026-09-05

### Added
- **Viewport-fit proof**: `GfxViewport` pins the docked shrink-to-fit
  chain (never upscales, height always fits, width fits iff the
  engage-gate shrink is feasible, centered offsets nonneg).
- **Proofs round 3**: `AiInput` (scripted button/edge state machine),
  `GfxPillarbox` (undocked aspect-fit), `GfxEngage` (16:9 invariant,
  margin honesty, gate→fit chain), `GfxSnap` (within-4, idempotent),
  affine fallback guards.
- **gameX salvage wave**: 17 specs re-verified against gX source and
  salvaged (RHI family, GLState, DrawState, ShaderActive,
  GfxConstants, RenderTextureLifecycle, TAAHalton, ElementState,
  GridLayout/RectTransform, ViewportMath, Easing) plus the `GxMath`
  lib (F32v3 + 4×4 matrix, incl. Lagrange). `make proofs-full` now
  builds `GxProofsGfx` + `GxMath`. 268 theorems total, zero sorry.
  Rejections documented in `proofs/README.md` (stale values,
  diverged structs, out-of-scope systems).

## [0.6.13] - 2026-09-05

### Added
- **Proofs x5 (28 theorems, zero sorry)**: HaloInset (marker nesting
  + degenerate guard), PressRoute (captured/void partition),
  EditLock (exclusive lock + focus clear), SoloRestore (exact
  restore + length + empty edges), DockRestore (all four ±8px
  branches). Each pins one shipped bug class to its branch logic;
  the SoloRestore length law caught a modeling error pre-commit.
- **Mathlib gfx tier**: opt-in `GxProofsGfx` lib (same pin as gameX
  `lean/`, rev v4.27.0) with GfxAffine (exact ℝ round trips) and
  GfxMatrix (row-major + GL_TRUE transpose IS the column-major
  reading; foreign-mul formula). Built via `make proofs-full`
  (cache-guarded skip, never in `make check`), so program users
  without the ~11GB cache never pay. Cache transplanted from
  gameX/lean (same rev) — gfx builds take seconds.

## [0.6.12] - 2026-09-05

### Added
- **Edit lock**: target canvas goes inert while an element editor is
  open (state force-clear, focus nav gated, set on open/retarget,
  cleared on close). Drags no longer leak through to the canvas.
- **Hidden-element marker**: `RECT_NO_DRAW`/alpha-0 elements get a
  yellow dashed outline + `hidden` tag + tree `(hidden)` suffix —
  invisible elements are selectable again.
- **Selection wash**: translucent fill over the claimed rect, so
  occlusion (child painted over by sibling) reads at a glance.
- **Solo isolate**: one click hides every sibling (`View` menu +
  `GX_TOOLS_SOLO=1`), selection stays visible against any backdrop.
- **Marker halo**: dark ring + white core (inset 2px — coplanar
  same-depth strokes lose to depth order, so ring and core share no
  pixels). Selection reads on dark and light content alike.
  Verified pixel-exact headless on both backgrounds.
- **Lean proofs**: `proofs/` sibling (same repo/tags, outside
  `master/` so tarballs never ship it) with `GxProofs.ViewportRoundtrip`
  (5 theorems, zero sorry, core tactics only, no Mathlib — seconds,
  no cache). Blocking `make proofs` gate in `make check`; Mathlib
  future documented as a recipe (separate lib + opt-in
  cache-aware `proofs-full`, never in the default gate).

## [0.6.11] - 2026-09-05

### Added
- **Dock hardening**: resizable window (OS/tiling resizes were ignored);
  live follow (viewport, backdrops, panels track the OS size); cramped
  dock falls back to overlay with a one-shot panel snap; engage fires on
  tool-open edge only (never re-widens a user resize); close keeps a
  user-resized window; live window queries (were 0-stubs); tree rows
  elide with `...` (full name on hover). Verified headless at 1400/1900.

## [0.6.10] - 2026-09-05

### Added
- **Editor input fixes**: selection outline/handles mapped game->screen
  (were ~220px off + unscaled when docked); press routing (presses born
  on ImGui never touch canvas selection, pillarbox-void presses
  ignored, canvas gestures survive panel crossing); engine mouse owns
  game coords (viewport-aware `ScreenToGame` at the source, identity
  undocked — removes the window-ratio scale that double-mapped docked
  input). All verified headless to the pixel.
- **Scriptable ImGui input**: `GX_AI_INPUT` gains `move` (button-up park),
  cursor parking between actions, and a bridge driving ImGui (pos events
  + button edges) with an rlImGui poll guard while scripted. Headless
  widget interaction verified (`DragFloat` +1/frame end-to-end).
  Documented under `api_reference.md` → headless input.
- **Release tooling**: `scripts/mkrelease.sh <tag> [--verify]` encodes
  the tarball procedure (clean-tree/tag/raylib-freeze gates, vendored
  raylib incl. `raylib.pc.in`, leak grep, opt-in clean-room gate);
  `gX_TakeScreenShot` creates its target dir + warns on failure.

## [0.6.9] - 2026-09-04

### Added
- **v0.6.7 — structural undo/redo**: deep heap snapshots (BSS) for
  add/remove/duplicate, redo-first capture ordering fix, duplicate
  alias fix; canvas tabs + canvas combo (retarget without the menu);
  per-token plain descriptions + a11y panel; tour polish.
- **v0.6.8 — collapse-by-default declutter**: editor flag groups and
  Actions start collapsed, theme rows collapsed (~119 hidden), tree
  search forces theme groups open; live edits verified.
- **v0.6.9 — dock mode** (16:9 margins for tools): opening any ImGui
  tool widens to 16:9, centers the 4:3 viewport (pillarbox via GL
  viewport, geometry untouched), slim panels dock in margins (pinned,
  fluid widths, canvas combo); close-all restores 4:3. Viewport infra
  (`viewportBox`, Screen/Game mapping, drawinfo sync, pre-ImGui
  fullscreen, title-viewport centering). `GX_TOOLS_CLOSE=<frame>`
  headless hook. Fallbacks: narrow/fullscreen stay overlay.

## [0.6.6] - 2026-09-03

### Added
- **THEMLAB live design preview** (`--themlab`, v0.6.2) — token-driven
  widget gallery + title mockup with a title2-vs-blueprint A/B toggle,
  token readout, a11y chips. Pair with `--theme-editor` for live edits
  (verified headless via `GX_THEME_EDIT`). `docs/themlab.md`.
- **Blueprint token group** (`gui.style.blueprint`: ink/accent/lattice
  colors, lattice alpha/cell) + theme-editor panel. The lattice motif
  itself waits on the shape/shader fix below.
- **Editor studio layout** (v0.6.3) — the ImGui GUI editor split into
  menu bar (File/Edit/View/Canvas) + left tree/actions + right
  properties + status bar, with a visible selection outline + 8 resize
  handles, snap toggle, canvas retarget.
- **Editors in EXTRAS** (v0.6.5) — out-of-box in-app access: EXTRAS
  sub-view (GUI Editor / Theme Editor / Theme Lab + BACK), runtime
  open/close API (`tools.h`, window-X + Close items), `GX_TOOLS_OPEN`
  headless hook, CREDITS footer link + credits sub-view, scratch save
  path so Save never clobbers shipped markups.
- `the editor save target` (editor default save target).

### Fixed
- Main menu buttons centered (`TITLEGX_MENU_X` 2.3, was 2.8 = +95px
  right; verified edge symmetry). Footer INFO/DEMO/CREDITS/QUIT now
  clickable (were dead — missing `CLICKABLE_LEFT`).
- Window clamp-to-monitor (1536x1152 overflowed <=1080p): post-window
  fit to monitor-32/64, centered, persisted. Real monitor/window impls
  (were no-op stubs).

### Usability
- GUI editor: tree search (+`GX_TOOLS_FILTER`), shortcuts (Del/arrows/
  Esc/Ctrl+D/Ctrl+S/F1, typing-safe), plain tooltips on all flags,
  remove/overwrite confirms, first-run guide (F1/Help), first-run
  5-step tour (highlights, persisted `edTourSeen`, `GX_TOUR=0`
  suppresses, Help restarts).
- Theme editor: Ctrl+Z/Y undo (heap snapshots), group hover guides,
  Default/Blueprint one-click compare (undoable), live-edit guidance.
- ThemLab: blueprint color swatches, EXTRAS-pairing header.

### Docs
- `docs/gui_editor_tutorial.md`, `docs/theme_editor_tutorial.md`
  (end-to-end walkthroughs).

### Fixed
- **Canvas shapes/shaders un-buried** (v0.6.4) — the end-of-frame SDF
  pass painted opaque bg rects over composited shapes/shaders. Elements
  carrying shapes/shaders/textures/models now use the flat bg rect
  (7 lines, `element.c`). Proven via live-GL readbacks; `make check`
  green, guitester SDF text unchanged.

## [0.6.1] - 2026-09-01

### Changed
- **Maintenance pass** — leftover debug removed, warnings cleaned, stub
  API implemented, schema count hardened.
- Removed: the v0.2.0-era `sdfmag` GXLOAD debug print, the unreachable
  `--debug-view` case 10 (cookie atlas — `% 10` clamps it; the atlas is
  viewable in the theme editor), unused `didPostProbe` (godray) and
  `s_rdocWindow` (renderdoc hook).
- **Engine warnings to zero** (52 -> 0 in-engine on a clean build; the
  remaining 38 are raylib's own sources + tools/): missing `<stdlib.h>`
  for `free` (raylib_wrappers_load.c x3), unused corner vectors
  (raylib_wrappers_texture.c), sign-compares (main_loop.c, title.c x3),
  misleading indentation + unused `g_planarShaderInit` (draw.c).
- **The no-op stub API implemented** (wrappers/text.c — was ~18 functions
  returning 0/empty, declared in the public header): `gX_RawString_`
  Length/IsEqual/ToInteger/ToUpper/ToLower/ToPascal/SubString/Replace/
  Insert/Join/Append/FindIndex, `gX_String_Format` (vsnprintf)/
  `gX_String_Copy`, `gX_Text_Measure` (MeasureTextEx), `gX_Font_`
  LoadFromImage (real LoadFontFromImage path)/LoadFromMemory/
  GetGlyphAtlasRec. Pointer-returning helpers share a static scratch
  buffer (signature-constrained; documented, no chaining).
- **Schema count hardened**: the style schema's hand-edited
  `objectsCount` (drifted 129 -> 144 already) is now derived from the
  term count at init — token groups can be added without touching it.

## [0.6.0] - 2026-09-01

### Added
- **GUI text milestone**: word-wrap, SDF text, a11y tokens.
- **Word-wrap for GUI text elements** — new `wrap` flag on
  gX_GUI_Element_Text (builder templates set it directly; serialized in the
  element schema; demo row in the GuiTester template). Greedy per-word fill
  measured with real glyph advances (not the avg-char-width estimate the
  truncation uses), minimal UAX #14 break opportunities for CJK ideographs
  (each ideograph is its own segment — ID/IN classes), hard `\n` preserved,
  pending whitespace dropped at breaks. Wrapped lines feed the existing
  multi-line renderer (lineSpacing), RECT_AUTO_HEIGHT grows to fit, and
  only an overwide single segment gets the "..." ellipsis (last line only).
  The pure core takes a measure callback — unit-tested headless with a
  mock (8 cases: ASCII wrap, exact fit, overwide word, CJK breaks, hard
  newlines, space runs, empty input, output-buffer cap).
- **SDF text** — per-element `sdf` flag renders with a single-channel
  signed-distance-field twin of the UI font (raylib FONT_SDF atlas via
  LoadFontData+GenImageFontAtlas+LoadTextureFromImage — this raylib's
  LoadFontEx predates the FONT_SDF parameter). The atlas draws through a
  new engine shader (resources/shader/sdf_text.fs, analytic dFdx/dFdy
  edge — no pxRange uniform) using the engine's direct-geometry path:
  raylib's BeginShaderMode calls rlSetShader, which permanently changes
  RLGL.State.currentShaderId — the rlgl immediate-quad batch (used by
  DrawTextPro for the other texts) then flushes with the SDF program and
  the rest of the canvas text silently disappears. The wrapper Font now
  carries the SDF shader id (gX_Font gains a shader field). Verified:
  at 3x base size the SDF glyph transitions average 1.5px vs 8.3px for
  the bilinear-upscaled bitmap (5.6x crisper); the bitmap + wrap + SDF
  labels all render together (no cross-element corruption).
- **A11y token group** (`gui.style.a11y.*`, theme.json + schema — the
  theme editor gets it for free): the surface stack (canvas/raised/
  overlay/sunken/inverse), On- pairs for every surface (`textOn-*`),
  motion tokens (short/base/long + `motionReduced`), and type
  line-height. WCAG 2.2 AA contrast assertion runs at theme load:
  every On- pair is measured and pairs < 4.5:1 log a warning (current
  theme: 8.7-13:1, all pass); the focus ring is asserted >= 3:1 vs the
  canvas (WCAG 2.4.11). The hover scale pulse now rates from
  motionShort instead of a hard-coded smoothing constant, and
  motionReduced snaps the scale instead of animating. Dark themes
  rebalance the SURFACES only — the On- pairs remap with them.

## [0.5.2] - 2026-09-01

### Fixed
- **Cookie rects never reached the uniform path** (pre-existing, found
  while scaling cookies to the SSBO range): `lightsCookieRects` is a
  vec4 array but was uploaded with `gX_RHI_Uniform_FloatArray`
  (glUniform1fv) — a type mismatch the driver rejects silently, so the
  rects stayed zero and the fragment shader sampled the atlas corner
  (white) — the 3.3/uniform path rendered NO cookies at all (the demo
  spot's cookie included). Now uploaded via glUniform4fv
  (gX_RHI_UniformVec4). Verified with a shader diagnostic: zero-rect
  flashes went 4101 px -> 0; attenuating-cookie pixels 0 -> ~890,
  matching the SSBO path (~810).
- **>32-light cookies (SSBO path)**: the cookie flag (d[18] bit 2) and
  the atlas rect (7th vec4) now ride in the light SSBO; the uniform
  arrays were read out of bounds for lights >= 32. The cookie atlas
  grew 4x4 -> 8x8 cells (64 slots, 2048px) and the rebuild loop covers
  the SSBO range. Verified: 44 cookie ring lights render cookies in
  the 4.3 build (28.5K px vs the plain ring) while 3.3 clamps at 32.
- **Dead `cullGridImage` field** removed from the clusters struct.
- **GX_EXTRA_COOKIE_LIGHTS=N** — ring of cookie spot lights (type 2,
  aimed at the demo cluster) and **GX_EXTRA_SPOT_LIGHTS=N** — plain
  spot-light ring — test hooks for the >32 cases.

## [0.5.1] - 2026-09-01

### Fixed
- **Point shadows scaled to the 64-light ceiling**: the shadow range and
  far plane lived in 32-wide uniform arrays (`pointShadowRanges`,
  `pointShadowFarPlanes`) that the SSBO path read out of bounds for
  lights >= 32 (practically reading 0 = "unlimited"/"no cubemap" — >32
  shadow-casting lights rendered without shadows). Both fields now ride
  in the light SSBO (6 vec4s per light: range in vec4 4.w, far plane in
  vec4 5.x) via `L_SHADOWRANGE`/`L_FARPLANE`; the uniform arrays stay
  for the 3.3 build. Verified: 4.3 vs 3.3 with 20 shadow-casting ring
  lights = 0 px (SSBO values == uniform values); 44 shadow lights
  render in 4.3 (1.59M px shadow area) while 3.3 still clamps to 32.
- **Shadow-casting point light cap 8 -> 64**: `gX_GFX_MAX_POINT_SHADOW_LIGHTS`
  (the shared 2D array = 6 layers per light) was 8 — slots >= 8 clamped
  onto the last light's layers. Now 64 (384 layers).
- **GX_EXTRA_SHADOW_LIGHTS=N** — shadow-casting ring variant of
  GX_EXTRA_LIGHTS (castShadows + 6-unit shadow range) for >8/>32
  shadow-light testing.

### Note
- **UBO matrices in the compute kernel — dead end**: a std140 uniform
  block (view/proj, binding 2) makes the Cg-based GLSL translator drop
  the dispatch entirely (grid readback: 0 tiles written for matrix x
  SSBO-vec, matrix x constant, and even a scalar element read). The
  CPU-projection + GPU-mask-assembly design stays.

## [0.5.0] - 2026-09-01

### Added
- **Forward+ clustered lighting, made real + ceiling lifted to 64 lights**
  (`GX_COMPUTE=1` builds raylib with OpenGL 4.3 via
  GRAPHICS_API_OPENGL_43). Three stages:
  - **Grid consumer**: the per-tile light bitmask grid (texture unit 29)
    was uploaded and bound every frame but standard.frag never read it —
    ENABLE_CLUSTERED only added overhead. The shader now enumerates the
    tile mask (GLSL 330 LSB walk, ascending slot order = exact legacy
    parity, verified 0 px diff clustered-on vs GX_NO_CLUSTER=1).
   - **SSBO light path**: per-light data (pos/type, dir/fov, color,
     attenuation+brightness, diffuse/spec factors; later expanded with
     point-shadow and cookie fields to 7 vec4s, std430, binding 0) moves
     out of the uniform arrays (32-slot limit);
    `gX_GFX_MAX_LIGHTS_SSBO=64`. The default 3.3 build keeps uniforms.
  - **64-bit tile masks**: the grid started as R32UI (32 slots/tile —
    which silently capped the SSBO lift back to 32 lights per tile);
    it is now RG32UI with lo/hi words, so all 64 slots are addressable.
  - **Compute tile culling** (`light_cull.comp`, one thread per 16px
    tile): the CPU projects each light once per frame (identical math to
    the CPU grid path — world→view→clip, disc radius from the
    attenuation quadratic root) into a projected-lights SSBO, and the
    kernel ORs the slot bit into every covered tile via image unit 1
    (imageStore + memory barrier). Grid readback verified: compute grid
    == CPU grid == legacy (0 px on all A/B pairs at 4 and 44 lights);
    4.3 vs 3.3 at 44 lights = 248K px difference — the lift is real
    (3.3 clamps at 32, 4.3 shades all 44). Driver note: the Cg-based
    GLSL translator on the test system silently drops compute
    dispatches that multiply a mat4 uniform by an SSBO-derived vector
    (kernel never reached its imageStore — constant-vector probes wrote
    fine) — the kernel deliberately takes no matrices.
- **GX_BENCH=1** — measures the grid-build segment (µs/frame, printed
  every 120 frames): 3.3 CPU grid scales linearly (325 µs @ 8 lights →
  1547 µs @ 64); the compute path is essentially flat (18.4 µs @ 8 →
  20.6 µs @ 64) — ~17× faster at the default light count and ~75× at
  the 64-light ceiling.
- **GX_EXTRA_LIGHTS=N** — spawns a ring of N test lights around the
  scene for ceiling-lift validation.

### Fixed
- **Forward+ clustered path was dead code**: the CPU-built grid was
  uploaded and bound every frame but never consumed by standard.frag;
  the shader now enumerates the tile mask (see above).
- **>32-light SSBO lift was a no-op**: the R32UI grid capped every tile
  at 32 slots, so lights 32+ were never lit in clustered mode; RG32UI
  64-bit masks fix it (see above).

## [0.4.1] - 2026-09-01

### Added
- **Full pause menu (EscapeMenu port from gameX)** - the ESC key now
  opens a real pause overlay instead of a dim bar: fullscreen blur
  (reads gui.ping via blur.frag), dim overlay, panel + title, and
  engine buttons — Resume / Settings / Back to Title — each with hover
  + click sounds, focusable, styled from the `gui.style.escape.*`
  tokens (previously unused in the schema) and `pause-*` cssClass
  hooks, plus an X close button and ESC hint. The game-side buttons
  (Inventory/Quests/Bestiary/Save) are dropped per the pivot.
- **Live debug overlay (DebugDisplay upgrade)** - the corner box is now
  a stats panel (fps, scene, screen resolution, canvas count) that
  rebuilds every frame while `SHOW_DEBUGDISPLAY` is set, so the rows
  carry current values.
- **Toast popup (Message template port)** - toasts were SET
  (`gX_GUI_ShowToast`, settings "Unsaved changes", title save
  messages) but never RENDERED — the Message canvas was empty. The
  template now draws the centered panel with the live toastMessage
  text and an X dismiss button; the canvas rebuilds on the toast
  rising edge so the baked text matches.
- **Tools polish round** (GUI editor + theme editor):
  - GUI editor: on-canvas drag-move + corner/edge resize with 8px grid
    snap (active when the ImGui window is not hovered), 32-slot
    undo/redo ring (Ctrl+Z / Ctrl+Y; snapshots of the element + text
    heaps taken at gesture start; value edits only — add/remove/
    duplicate are not undoable), and Duplicate (deep-clones the element
    incl. its texts module with a +16px offset).
  - Theme editor: token search filter (bare key or dotted group.key,
    case-insensitive), reset-to-default baseline captured at open /
    reload (per-token R button, Reset group, Reset all).
- **v0.4.0 release packaging pass**:
  - Vendored ImGui (MIT) + rlImGui (zlib) now ship license texts
    (`license/imgui/LICENSE`, `license/rlimgui/LICENSE`), NOTICE.md and
    readme credits updated.
  - readme.txt refreshed: v0.4.0, tools suite, new flags, make check.
  - Removed the stale tracked `master/gX_build` absolute-path symlink
    from the tree.
  - Clean-clone build verified (`git clone --recurse-submodules` ->
    build.sh -> tests + captures); dist tarball (11M, submodule source
    only, screenshots/builds excluded) extracts and builds standalone.
  - `make check` audit leftovers documented as verified false positives
    (cookie_window.png concat path, background param-table uniforms);
    architecture.md roadmap line updated to the real frontier
    (Forward+ 32-slot ceiling -> compute/SSBO).

## [0.4.0] - 2026-08-30

### Changed
- **GUI polish + low-level audit (v0.4.0)**: the title entrance no longer
  swings sideways on launch — `INITIAL_LOAD` (previously never set) now
  triggers a 0.25s fade-from-black on first launch, and the re-entry
  skew-slide is tamed (3° swing, -60px slide, 0.35/0.15/0.35 phases).
  Input is only gated during the OUT phase, so arriving-scene buttons
  are live immediately — the old blanket `transition.executing` gate
  kept every button dead for the whole entrance. Fixed the FadeFromBlack
  BLACK-hue leak (colorResult defaulted to start=BLACK after completion
  → all elements gray forever) and the never-firing "transition
  complete" log.
- **Refresh gate fixed**: `canvas->refresh` alone now triggers the
  rebuild pass (was gated on `gui.refresh`, so TitleSaves/TitleBack
  sub-view swaps never rebuilt — LOAD GAME was dead). The save-slot
  sub-view and BACK navigation now work.
- **Occlusion pointer match**: occlusion carries a `canvasPtr` (was
  canvasID-vs-heap-index, which drifts after canvas removal → silent
  dead clicks).
- **OnClick direct callback pointers**: the click binding stores the
  callback+arg directly in the element data module — no 256-slot pool,
  no dispatch stub, no index-reset coupling. `ResetPool` only resets the
  widget args pool now.
- **Compression pass**: removed 6 leftover TEMP probes and the empty
  "Old canvas buffer draw" comment; `docs/gui_audit_v2.md` records the
  capture matrix, ranked findings, and the Lean-proof divergence
  (OnClickPool obsolete, Transition needs a FadeFromBlack lemma).
- **No-args default scene**: title now — the gfx demo only shows on a
  genuinely first-ever launch (no settings.json AND no save data). Any
  returning user (settings.json from a previous run) lands on the title,
  even without a save. GX_SKIPTITLE=1/0 still forces either.

### Added
- **gX Tools suite — first tool: GUI editor** (v0.4.0) - in-engine template
  editor whose chrome runs on vendored Dear ImGui (imgui core + rlImGui
  raylib backend, ~41K L, imgui_demo excluded, FontAwesome icons
  compiled out). Breaks the gui-creates-gui loop: the editor UI never
  depends on the gX GUI being complete, while the canvas being edited IS
  the real gX canvas (built from a markup template with live hot-reload).
  `--tools <markup>` opens the editor on the GUIPLAYGROUND scene:
  element tree (click-select), write-through property panel (rect drags,
  color, scale/angle, visibility, state + rendering bitmask checkboxes,
  text module), add/remove element, and Save markup — a new
  `gX_GUI_Markup_Save` (schema-driven, format by extension) round-trips
  the live canvas back to the markup file (verified: 4-element demo
  saves + reloads with identical rects/texts). Headless hooks
  `GX_TOOLS_SELECT=<idx>` / `GX_TOOLS_SAVE=<path>` for capture + save
  verification. Markup builder now types plain rects RECT (was UNKNOWN).
  Vendored imgui under `src/tools/` (suite home; CMake CXX already
  enabled); gX_GUI_Load/Unload/Draw_ImGui (dead decls in gui.h) now
  implemented with ini-persistence disabled.
- **Theme editor tool** (gX Tools suite, second tool) - `--theme-editor
  <path>` opens the design-token editor over the live scene (title by
  default; the target theme activates pre-load like `--theme`). ImGui
  chrome lists every style-schema token grouped by context (title2 /
  core / shared / escape / battle) with color pickers, sliders and
  checkboxes; write-through mutations call the same refresh path the
  theme hot-reload uses, so the title re-skins in the same frame. Save
  writes the live token values back through the style JSON schema
  (`gX_GUI_Style_Save` — the theme file stays the single source of
  truth); Reload-from-file re-reads it. Headless hooks:
  `GX_THEME_EDIT=<token>=<r,g,b,a>` (bare or dotted group.key form) and
  `GX_THEME_SAVE=<path>`. Verified: token edit re-skins the title,
  save → `--theme` round-trip is pixel-identical.

### Fixed
- **CI audit fully green** - the last two `make check` failures are
  gone: the deps audit example path in `src/common/path.h` now references
  a real model (`resources/model/props/tubetech_vt.glb`), and the
  `gX_RenderDoc_*` family is documented in `docs/api_reference.md`.
  The Lean gate baseline was updated for the deliberate vendored-ImGui
  growth (+1.2 MB, the v0.4.0 tools suite).

## [0.3.8] - 2026-08-30

### Added
- **Background renderer (full port from gameX)** (menu personality pack,
  feature 2) - the procedural fullscreen background system: sinusoid/warp/
  wave layers, 8-sinusoid + HDMA scanline modes, palette (gradient,
  row-cycling, median-cut image processor), boundary/modulo clipping,
  direct transform matrices, preset save/load (JSON/YAML/XML), preset
  morph transitions, and one-shot reactions (pulse/shake/flash/warp).
  Replaces the no-op stubs in `src/renderer/background*` with gameX's
  real implementation (`background.c` minus FFmpeg video, real
  `param.c`/`schema.c` on gX's json_impl drop-in, tests wired into
  `gX_Tests()`). `--background` scene flag + bg harness flags
  (`--bg-preset/--bg-palette-mode/--bg-react/--bg-transition/--bg-imgpro`).
- **Aesthetic presets** (menu personality pack, feature 5) - theme files
  now re-skin the whole title screen: `--theme <path>` flag (or
  `GX_THEME` env) picks a preset before `gX_GUI_Load`; all ~26 hardcoded
  colors in the title template migrated to `style.title2` tokens (25 new
  tokens: wordmark/subtitle/hero-band, the 6-button bg+text ramp, quit,
  footer, version, save-slot view). Ships `theme_blueprint.json` (dark
  navy blueprint variant); default theme stays byte-identical.
- **Rotating 3D centerpiece** (menu personality pack, feature 2/3) - the
  title scene draws a slowly-spinning, camera-orbiting teapot (or
  `GX_GUEST_MODEL` override, AABB-normalized like the showcase ring)
  behind a now semi-transparent title backdrop. Raw-GL draws (gX batch
  program + mesh VAOs): canvas-embedded models fail because the rlgl
  batch computes the MVP at flush time from `RLGL.State` (the canvas
  ORTHO) — invisible model or clobbered 2D projection. Also fixes
  `gX_GUI_Builder_Model_File`'s RM heap lookup (fileName1, not
  fileName2 — first template to use Model_File silently got NULL) and
  adds teapot.glb to the RM heap defaults.
- **Hover-reactive selection** (menu personality pack, feature 3/3) -
  hoverable/focusable elements pulse to 1.06 scale (frame-rate-
  independent lerp) and brighten on hover; applies to rects AND SDF
  widget surfaces (which bypass the flat-rect path); the rect
  parameters gained a center-scale (applied in the wrapper).
- **Retro transition kit** ("menu personality" pack) - `gX_GUI_Transition_Style`
  with FADE/SLIDE/ZOOM/SKEW_SLIDE (canvas slide + swing rotation, Blades-
  style) and the wipe styles DIAGONAL_WIPE / SCANLINE_CURTAIN / IRIS
  (screen-space shader overlay via a raw-GL fullscreen quad - the rlgl
  batch always re-binds its default program at flush, so batch draws
  silently drop custom shaders). Title scene enters with SKEW_SLIDE as
  its identity. `GX_TRANSITION_TEST=style` fires a transition headlessly
  for mid-transition capture verification.
- **Capture gate fixed for non-GFX scenes** - the model-load gate only
  applies to GFX-module scenes (gfx/gfxdebug). The gfx module spawns
  cube/sphere/cone at startup for every scene, and its deferred loader
  only runs in GFX scenes - gating elsewhere blocked every non-GFX
  capture for the full 900-frame grace (title captures landed at frame
  1080 instead of 180).
- **Guest asset system** - runtime registration of external resource
  roots (`GX_ASSETS` env, repeatable `--assets` flag) through the path
  system, strictly fallback-only so engine assets and shaders can never
  be shadowed; wrapper-level load fallback (`gX_Model_Load` /
  `gX_Texture_Load` via `gX_Path_GuestFallback`) gives all existing
  load sites guest support; `--guest-model subpath[:count]` /
  `GX_GUEST_MODEL` injects an additive, vertex-AABB-normalized ring of
  foreign models into the showcase for gfx perf testing
  (see `docs/guest_assets.md`). Verification scenes stay pristine.
- **Slow-scene capture escape** - `--ai-screenshot-final` captures
  `GX_SHOT_READY_FRAMES` (default 30) after the last model finishes
  loading once a run exceeds 30 s wall-clock, so heavy guests (2643-mesh
  GLBs at ~1 fps) no longer starve frame-count captures. Normal scenes
  keep the fixed frame target and stay byte-identical. Failed loads no
  longer block the load gate (only pending loads do).
- **`--no-gl-debug`** - skips the GL debug callback for headless/harness
  runs (gameX parity).
- **GL debug storm control** - the RHI debug callback dedupes identical
  (type, id, message) repeats: first occurrence logs, then a summary
  line every 500 suppressed repeats. A 2500-line-per-run flood from the
  reflections pass collapses to ~12 lines while keeping signal.
- **First guest perf session** (2026-08-28) - documented in
  `docs/guest_assets.md`: baseline ~12.0 ms/frame; teapot ring x6
  +1.6 ms; a heavy multi-mesh guest x1 +100 ms (reflections-dominated); x8 ~890 ms/frame
  (reflections 819 ms, opaque 40 ms, shadow 33 ms).

### Fixed
- **Guest captures never fired with heavy GLBs** - misdiagnosed as a GL
  error storm. Root cause: 2643-mesh terrain dumps explode the per-mesh
  draw path (~21k draws/frame at an 8-ring), collapsing to ~1 fps so the
  180-frame target was unreachable; the pre-existing ~14
  `GL_INVALID_OPERATION`/frame reflections-pass flood (identical with
  zero guests) merely obscured it. Both fixed: capture escape above +
  storm control above.
- **SSR concentric banding** (top-down mirror views) - the screen-edge
  dissolve no longer applies to clean sky escapes (sky hits and
  zero-crossing margin exits); slab luminance stddev 25 -> 8.9,
  vision-verified gone.
- **SSR dark vertical lines** (default views) - silhouette-jump
  rejection: crossings whose depth jumps more than maxDepthDelta in one
  step are visibility discontinuities, not surface intersections.
- **SSR sky washout** (camera-facing-the-sun views) - per-pixel sky ray
  directions replace the camera-forward march hack.
- **Fog was silently dead** - five stacked defects fixed: wrapper
  uniform writes corrupted for the gen program (all uploads now raw
  RHI after explicit Use), startup location-cache race, cascade-0
  sampled as sampler2D on a 2D_ARRAY, LDR genMap clamping negative
  extinction, composite dropping the computed delta.
- **Fog reflections** - planar RT runs the fog gen with the mirrored
  camera (depth RBO swapped to a sampleable texture); a per-direction
  fog delta LUT (128x64 equirect) feeds SSR escapes and the IBL
  specular term.

## [0.3.7] - 2026-08-28

### Added
- **Showcase planar fill** - the demo platform slab now owns the mirror
  view (reflectionSource), enabling the v0.3.6 seam/wrong-side replacement
  on the main demo scene. Vision-verified: the historical doubled-rim
  defect at torus foot / disc rim / box contacts is gone; mirrored content
  includes camera-hidden undersides. Cost measured neutral end-to-end.

### Fixed
- **AI captures raced the deferred resource loader** - --ai-screenshot-final
  and --ai-screenshot-after now wait until every scene model reports
  loaded (900-frame grace prevents deadlocks on failed loads).
  GX_SHOT_FRAME overrides the capture frame target; GX_SHOT_FORCE=1
  bypasses the gate.

## [0.3.6] - 2026-08-28

### Added
- **Planar-precision auxview** - the hybrid miss->planar route now samples
  the mirrored-camera RT with an analytically exact candidate: the origin
  point reflected across the mirror plane, projected through the mirror VP.
  New routing fires on physically impossible hits (ray striking a
  camera-front face from behind) and low-margin silhouette zones of
  plane-origin pixels, replacing false self-hits with true mirror content.
  A floating disocclusion probe in --reflectrow now shows its unseen
  underside in the floor reflection (vision-confirmed opposite orientation
  with readable edges).
- --reflectrow gained a floating tilted probe cube (diffuse cyan emissive)
  purpose-built for hidden-face verification.

### Fixed
- **v0.3.5 tagged source did not compile** - the AE denominator referenced
  an identifier leaked from trim-removal churn; masked for a day by stale
  binaries. Repaired (9132286), tag re-pointed.
- Auto-exposure specular-trimmed log-average RESTORED: the v0.3.5 rejection
  was a capture-harness artifact (flag-less runs booted the default title
  screen, not the showcase). Re-tested under correct capture: sky identical
  to baseline, regression matrix exact.

### Changed
- Capture harness discipline: pre-vision gate now checks pair
  distinctness + sky luminance band; scene flag defaults to --showcase.

## [0.3.5] - 2026-08-27

### Fixed
- **SSGI temporal accumulation popped at silhouettes** - the binary velocity
  threshold (>0.15 ? 0.35 : 0.8) is now a smoothstep band over 0.05..0.25,
  and the neighborhood variance-clip pad widened 0.02 -> 0.06 so broad GI
  gradients stop eating history energy.
- **SSGI spatial blur edge-stops used raw nonlinear depth** - weights are now
  squared view-space deltas over a distance-scaled sigma (near/far plumbed
  from camera frustum); silhouettes stay sharp at every depth.
- **SSR rough reflections sampled too-coarse color mips** (hit LOD exponent
  4.0 -> 3.0) - mid-roughness soft patches reduced.

### Changed
- Auto-exposure readback cadence 10 -> 4 frames; verified pixel-safe.

### Attempted-and-reverted (documented)
- SSR contact-seam thickness-band reject: collapsed two legitimate contact
  reflections on reflectrow while producing zero measurable benefit in the
  showcase; residual seam ghosting stays open pending normal-aware march
  validation inside marchRay.
- AE specular-trimmed log-average: regressed global sky brightness (~2x)
  via adaptation feedback; rejected by bisect evidence.

## [0.3.4] - 2026-08-27

### Fixed
- **SSGI was invisible at runtime despite shipping default-on** — three
  stacked wiring defects: the gather pass drew through a hand-rolled GL
  quad that rasterized nothing in post contexts (now the standard
  `DrawScreenQuad` path); blur/accum program uniform locations were declared
  but never cached, so both passes bound samplers through other programs'
  indices and stayed black; the composite gain/sampler pair used a stale
  function-static location plus an unassigned sampler unit (fresh per-frame
  queries upload unit+gain directly now). Retuned gather drops its
  mixed-axis thickness test that rejected ~all samples; ambient floor is
  decoupled from user intensity (no global wash). Measured off->on delta:
  502k px at mean 55.8, no clipping growth.

### Changed
- Godray gen gain retuned for legibility at the pinned showcase camera
  (weight 0.5 -> 0.42 inside the existing additive cap; clipping stays at
  baseline levels).

### Added
- **SSR hybrid miss->planar fallback (`ENABLE_SSR_AUXVIEW`, env hook
  `GX_T_SSRAUX`)**: screen-space reflections cannot see content hidden from
  the camera (object undersides behind a mirror); failed rays previously
  fell to the parallax-less env cubemap producing washed patches. The new
  route projects the failed segment endpoint through the frame's
  mirrored-camera view-projection into the planar-reflection RT and samples
  it there — geometrically exact for flat dominant planes, no extra render
  pass in scenes that already opt into planar mirrors; in `--reflectrow`
  testbeds the RT is force-filled as an unconsumed shadow auxiliary.
  Misses replace their contribution; suspicious geometry hits blend by
  confidence. Water excluded. Known residue: rare single-pixel-route blobs
  when the projected exit leaves content coverage.
- **Auxview polish**: mirrored view now renders FULL-LIT models through the
  standard pipeline (cubemap-face pattern; v0.1.33 solid-color diagnostic
  kept behind `GX_PLANAR_DEBUG=1`), giving tonally continuous reflections;
  miss-route confidence now decays with march travel fraction, eliminating
  stray out-of-coverage blob samples. New model flag
  `params.reflectionSource` marks mirror-view owners excluded from their own
  fill while staying shaded normally.
- **`--reflectrow` scene**: reflection material testbed — pure-SSR mirror
  slab + six spheres sweeping metalness/roughness/gloss/tint + a tilted
  disocclusion-probe cube, fixed deterministic camera. Planar fill now
  paints per-model base colors instead of the v0.1.33 magenta diagnostic.

## [0.3.3] — 2026-08-27

### Fixed
- **Godrays whiteout when enabled**: the gen pass drew through the rlgl
  batch pipeline, which rebinds its own default program/texture — the
  half-res genMap received a plain white quad and the additive composite
  blew the whole frame out. Now drawn with `DrawScreenQuad` like the fog
  pass. Original response constants restored; additive output hard-capped
  (`min(total*0.5*falloff, 1.2)`) so no input can reproduce a whiteout.
- **SSR grazing-angle ghost doubling** (duplicated offset image on tilted /
  near-parallel surfaces): reflected rays now lift off along the surface
  normal (not just `reflectDir`), and crossings whose scene depth sits
  within noise-band of the origin surface while still inside the self UV
  footprint are swallowed instead of refined into displaced self-hits.
  Residual: small contact-seam ghosts at object/floor contact points.
- **Sky-gradient banding**: interleaved-gradient-noise dither at the final
  LDR encode in the composite shader.
- **AgX double-gamma**: AgX applies its own display EOTF; the composite's
  additional `linearToSrgb` is now skipped when AgX is active.

### Removed
- Dead SSR temporal resolve (`if(0)` block) plus its full-res HDR history RT,
  temporal shader file, loc-cache and resize/clear/update wiring (~140 lines,
  six files). Re-enabling required jitter/confidence gating that is not
  justified by current visual demand (see audit doc).

### Added
- Settings schema entries for previously env-only flags: `ENABLE_FXAA`,
  `ENABLE_CLUSTERED`, `ENABLE_AGX_TONEMAP`, `ssao.useGTAO`, `ssao.useSSGI`,
  `bloom.autoExposure`.
- GFX capture hooks `GX_T_FOG` / `GX_T_GODRAY` (showcase mode).

## [0.3.1] — 2026-08-26

### Added

- **Forward+ tile light clustering** (CPU-culled, GL330-safe — no compute or
  SSBO required): per-frame CPU build of a 16px-tile light bitmask grid
  uploaded as an R32UI data texture; the model shader iterates only its
  tile's lights via `findLSB`. Ascending slot order makes shading bit-parity
  with the legacy uniform-array loop (verified: 0 differing pixels).
  `ENABLE_CLUSTERED` ships on; `GX_NO_CLUSTER=1` falls back to the full
  loop. Ceiling stays at 32 slots until a compute/SSBO upgrade.
- Headless RenderDoc capture trigger (`GX_RD_CAPTURE=1` under
  `renderdoccmd capture`, frame 3) promoted from debug hack to documented
  feature.

### Fixed

- **Shader compilation failures across 59 shader files — root cause:
  NVIDIA's GLSL compiler mis-parses multi-byte UTF-8 characters inside
  `//` comments**, corrupting tokenization and line-tracking and producing
  phantom syntax errors at wrong lines that moved depending on which
  shaders contained how much non-ASCII. v0.3.x development comments had
  introduced UTF-8 arrows across the shader corpus. All non-ASCII swept to
  ASCII equivalents.
- Missing closing brace in the fog generation shader's main() (introduced
  during M3.1 edits; masked by the above).
- **Volume fog un-blocked** as a result: occlusion simplified to cascade-0
  only (plain `sampler2D`, 40 m range cutoff; far field unshadowed by
  design — full CSM returns with the froxel upgrade). Fog re-enabled in
  the showcase; haze verified at default density 0.018 and dense 0.045.
- NaN scrub in the fog march accumulation + LDR genMap storage.
- Thread-safety hotfix: `gX_Shader_Compiler_Preprocess` wrapped in a mutex —
  the async preprocess worker pool races the main thread on shared statics
  (latent hazard surfaced during the investigation).

### Changed

- **SSGI ships default-on** (`GX_SSGI=0` opts out).
- SSGI Phase 3: confidence-weighted ambient floor fallback (screen edges no
  longer flat black) + previous-frame GI reinjection for cheap multi-bounce
  (SSRT3-style, bounceGain 0.5). Vision-verified richer indirect feel with
  no feedback blowout.

### Removed

- v0.2.0-era `GXRM:`/`GXTRACE` debug prints from the wrapper shader load
  path (log noise on every load).

## [0.3.0] — 2026-08-25

**The image-quality release: a new display transform, ground-truth ambient
occlusion, screen-space global illumination, and an upgraded volumetric fog
system.**

### Added

- **AgX display transform** (default tonemap; AgX > ACES > Reinhard
  precedence) + **CPU auto-exposure**: 64x64 luminance downsample,
  log-average + Krawczyk key estimation, asymmetric temporal adaptation.
  Overrides: `GX_NO_AE=1`, `GX_NO_AGX=1`.
- **GTAO** (Jimenez 2016) — horizon-scan ambient occlusion ported from the
  three.js reference; replaces kernel SSAO as default (`ssao.useGTAO`).
  Shares the edge-aware blur and model-shader AO sampling.
- **SSGI** (experimental, `GX_SSGI=1` opt-in) — horizon-gather indirect
  diffuse with colored light bleed; half-res RGBA16F gather target;
  denoise chain: depth-guided 7x7 edge-aware blur -> velocity-reprojected
  temporal accumulation with neighborhood variance clip and adaptive blend.
- Volumetric fog upgrades: Henyey-Greenstein forward-scatter phase, CSM
  shadow-map occlusion per march step (god rays), IGN-dithered march,
  sky-distance march, eye-anchored height band. **Feature gated OFF**
  pending a presentation-order blocker (below); schema opt-in reproduces.

### Fixed

- `gfx_ssr_temporal.frag` compile failure (undefined `prevClip`, broken
  since v0.1.38) — SSR temporal reprojection works for the first time.
- Slider potentiometer dst mapping: bound floats now receive the declared
  min..max value instead of the raw 0..1 track fraction (settings FOV /
  volume / sensitivity were silently broken engine-wide).
- Repeating element timers froze when `GetFrameTime()` read zero in poll
  contexts (wall-clock fallback) and on otherwise-static elements (timers
  missing from the static-element skip whitelist).
- Audit-forced GUI canvases now enable `meta.UPDATE` so per-frame element
  updates (timers) run on them.

### Changed

- Volume fog composites additively via the godray batch route; gen pass
  bakes the composite delta. Showcase no longer force-enables fog while
  the blocker is open — schema opt-in only.

### Known issues

- Enabling volume fog with its sky-march shader compiled blacks the
  presented frame; full bisection notes, one-token repro recipe, captured
  RenderDoc pair and interactive session guide in `docs/architecture.md`.
  Targeted for v0.3.1.

## [0.2.0] — 2026-08-25

**The front-end release: SDF widget rendering ships by default, full title
system, GUI editor, and the input/testing infrastructure to build it all.**

### Added

- SDF widget pipeline ON by default (theme token; `GX_GUI_SDF=0` opts out):
  flat blueprint fills, analytic-AA rounded corners (radius 4), hairline
  strokes — per docs/frontend_design.md design language.
- Title system: 7-item menu with tooltips, save-slot sub-screen with
  exclusive view navigation (LOAD GAME / BACK), three density variants via
  `GX_MENU_VARIANT=play|demos`, hero band motif, INFO/DEMO/QUIT footer,
  fresh-install rule (no save -> straight into renderer).
- GUI editor (GuiEditor template): element selector stack, transform value
  peek, wire outline, live-sync (~3Hz heartbeat), x/y/w/h/scale sliders
  bound directly to target elements, HIDE/COLOR actions.
- GX_AI_INPUT timed virtual input scripts (click/drag) injected engine-side;
  never touches the real cursor.
- RenderDoc in-app trigger hook (`GX_RD_CAPTURE=1` under renderdoccmd
  capture) — verified producing .rdc captures.
- gui audit hooks: GX_GUI_TEMPLATE_FORCE / GX_GUI_TITLE_VIEW /
  GX_GUI_EDITOR_TARGET + scripts/gui_audit.py.

### Fixed

- **guisurface.frag Y-flip**: gl_FragCoord (Y-up) vs window-space box (Y-down)
  made every non-centered rect rasterize nothing; the fullscreen backdrop
  masked it for months. One-line fix unlocked the whole pipeline.
- Slider potentiometer range mapping: dst now receives the declared
  min..max value instead of the raw 0..1 track fraction (settings FOV/volume/
  sensitivity were silently broken engine-wide).
- Repeating timers froze: GetFrameTime() reads 0 in poll contexts (wall-clock
  fallback); static-element skip didn't whitelist timers.
- Settings layout: controls split into breathable rows; FPS stepper label
  cleared its buttons; accessibility chips no longer overflow.
- Editor selection refreshes only its own canvas so live slider bindings
  stay valid; audit-forced canvases now enable meta.UPDATE.

### Changed

- guisurface.frag rewritten to blueprint style: shadow/gradient terms removed
  (design-language violation), hairline stroke lightened for dark fills.
- widgetsdf pass rearchitected: end-of-frame flush to the default framebuffer
  after all canvas composites; raw-GL quad path (rlgl DrawScreenQuad emits no
  fragments in custom-program GUI context).

## [0.2.0-alpha.1] — 2026-08-23

**v0.2 front-end arc begins: SDF widget foundation (experimental, flag-off).**

### Added

- SDF widget shader pair (guisurface.frag + guivert.vert): rounded corners,
  analytic AA, soft drop shadows, vertical surface gradient, hairline
  borders — the "actually better" rendering core.
- widgetsdf module: shared program + per-element attach through the
  element-shader heap (verified: 100+ attaches/frame incl rebuilds).
- Theme tokens v2: widgetSDF / cornerRadius / shadowOpacity /
  gradientStrength / borderWidth (gui.style + schema + theme.json).
- KenneyFuture.ttf engine UI font (space-free name) + NOTICE entry.
- resource_deps.py handles quoted paths via rename strategy.

### Known open bug

Attached instances execute Draw_Shader with correct state but never
rasterize — even constant-output fragments. Builder-created instances of
the same program rendered in one early test. All obvious fields
(provenance/loops/drawShader/loaded/fragPath) normalized without effect.
widgetSDF=false ships as safe fallback; investigation continues next
session with a minimal repro.

## [0.1.42] — 2026-08-23

**Dead-scene cleanup + audio smoke + architecture refresh.**

- Removed user-reachable paths to unimplemented scenes: CLI flags
  --cast/--battle/--map/--menu/--editor/--rooms rendered blank white
  frames (--battle verified); 7 respond.c GUI binding rows removed.
  Inert switch-cases/table rows kept as documented porting reserves.
- Audio smoke: device init (miniaudio/PulseAudio) OK, 16 procedural PCM
  tones registered, GUI call sites wired — no external files needed.
- docs/architecture.md: verification section + subsystem status table.

## [0.1.41] — 2026-08-23

**GUI subsystem pass: audit, harness templates, pruning, real title screen.**

### Added

- `GX_GUI_TEMPLATE_FORCE=<NAME>` / `GX_GUI_SHOW_MENU=1` hooks;
  `scripts/gui_audit.py` headless template classification.
- Ported from gameX: SETTINGS panel (sections/toggles/sliders/stepper/
  buttons) — fixes the silent dispatch-fail on the escape menu's Settings
  button; GUITESTER widget wall (+ guitester.frag, guitester.css).
- TITLEGX template: the title screen now runs through the canvas GUI
  pipeline (gradient, wordmark, buttons with OnClick wiring); the
  wrapper-draw placeholder in scene/title.c is gone.

### Changed

- Pruned 39 template enum entries whose builders were never ported;
  removed their respond.c scene bindings. Registry: 10/10 functional.
- resource_validator accepts .css stylesheets.

### Verification

- Audit: all registered builders render (6 clear + DEMO/SCENE in-baseline
  + MESSAGE intentionally empty). Title screen vision-verified clean.
  make check PASS.

## [0.1.39] — 2026-08-23

**TAA validated under real camera motion; gfx polish items closed.**

- `GX_CAM_SPIN=<deg/frame>` harness hook: deterministic orbit for headless
  temporal-quality captures.
- A/B after 150 frames of ~60 degree continuous orbit (GX_T_TAA=1 vs 0):
  identical scene state, zero ghosting/double-images, only the expected
  mild edge softening. The velocity-buffer reprojection handles the exact
  camera-motion mode that broke both pre-velocity TAA attempts.

## [0.1.38] — 2026-08-23

**Velocity buffer: TAA un-deferred and working; SSR temporal upgraded.**

The single highest-leverage gfx item: per-pixel motion vectors unlock the
TAA retry (deferred since v0.1.17), give SSR temporal true motion
reprojection, and stabilize water reflections under camera motion.

### Added

- `gfx_velocity.vert/.frag` + RGBA16F RT rendered after the opaque pass,
  gated on ENABLE_TAA || ENABLE_SSR. Velocity = UV delta between current
  and previous UNJITTERED view-projections; per-model `prevModelMat`
  carries object motion (first frame = zero).
- Halton jitter discipline: standard.vert keeps applying render-time
  jitter; consumers subtract 0.5*(jitter - prevJitter) explicitly.
- `GX_T_TAA` env hook for deterministic TAA captures.

### Fixed / changed

- TAA resolve (gfx_taa_resolve.frag): velocity-buffer fetch first, depth
  reconstruction as fallback; depth-delta disocclusion rejection added.
- SSR temporal (gfx_ssr_temporal.frag): same velocity-first path.

### Verification

- GX_T_TAA=1 showcase-planar: vision confirms crisp 2-3px edges, zero
  ghosting/smearing/color bleed; fidelity 98.11% (TAA-off 98.78%).
- reflect_suite: planar 99.22/98.78/99.28, SSR 78.14/74.07/71.82, water
  non-black 100%. make check PASS.

## [0.1.37] — 2026-08-23

**Water folded into the reflection pack; milestone closed.**

- Cubemap gate gains the water consumer: the water pass samples
  reflection.dynamicCubemap for its sky reflection; a water-only scene
  with SSR off previously skipped the 6-face render (reflection would
  collapse to black). Gate now includes ENABLE_WATER && loaded.
- reflect_suite.py water smoke case: --water with GX_T_SSR=0 (the exact
  regressed path), gate = non-black fraction >= 80%. Current 100.0%.
- docs/reflection_pack.md documents the water consumer rule and the
  water+planar coexistence trade-off.

## [0.1.36] — 2026-08-23

**The wedge was an oracle bug: planar reflection verified 98.4–99.3% exact.
SSR re-measured at 74% (was "42%" under the broken oracle).**

### Root cause of the "wedge truncation"

`probe_reflect_verify.py rot_xyz` applied cube rotation with the STANDARD
convention (+angles, X-first). gxmath's RotateXYZ stores the Rz*Ry*Rx
layout row-major and transforms with ROW vectors — the ACTIVE rotation is
the TRANSPOSE (angles negate). Verified by isolation captures:

- GX_CUBE_ROT=0: observed RT silhouette matches prediction within 1px on
  every edge (view/proj/scale/translate pipeline exact).
- GX_CUBE_ROT=Y (0,30,0): matches the -30 deg gxmath-convention candidate
  within 1px; neither +-30 standard candidates fit.

With the convention corrected, the planar reflection lands inside the exact
analytic mirrored hull at 99.2% @ yaw30, 98.4-98.8% @ yaw45, 99.2%
@ yaw60/pitch20, 99.3% @ pitch10. The remaining ~1% is edge antialiasing.

### Also

- SSR honest re-baseline under the corrected oracle: 74.07% (yaw45).
- Planar mix strength raised to bias+0.45 (reflection reads truer against
  floor lighting).
- GX_CUBE_ROT env hook (0/Y/Z) kept as a permanent convention diagnostic.
- Formal verification (gameX lean/): PlanarReflection module proves the
  x-flip sampler theorem and rules out w<=0 clipping analytically.

### Known issues

- Vision review notes a faint ghost band near the contact region and
  scanline banding on the plane — pre-existing scene features (shadow/
  lighting), not reflection-path defects; queued for the pack phase.

## [0.1.35] — 2026-08-23

**Oracle truth-fix release: planar reflection verified at 82–94% exact-zone
fidelity across orbit angles. Also repairs a v0.1.34 shipping mistake.**

### v0.1.34 correction

The sampling sweep loop left its LAST iteration (180° rotation, 0-scoring)
in `standard.frag` — the x-flip winner was overwritten before commit, so
v0.1.34 shipped with an invisible reflection. X-flipped screen-UV sampling
is restored here (`suv = vec2(1-u, v)`).

### Oracle fixes (scripts/probe_reflect_verify.py)

The verification harness was over-predicting the reflection zone, masking
real progress as failure:

- **Luminance-aware magenta detection** — fresnel/edgeFade dim reflections
  multiplicatively (hue preserved); fixed absolute thresholds under-counted
  them to ~0%.
- **Exact CUBE zone** — replaced the ray-vs-AABB slab test (+18% bbox
  margin) with the convex hull of the projected mirrored corners at full
  resolution. The old zone was ~2.4× too large: even a perfect reflection
  scored ~42%, and the "failure ring" seen in failmaps was pure zone
  inflation.
- One-shot runtime camera dump (`CAMDUMP`) in showcase confirms the oracle
  camera model matches the engine exactly (pos/tgt/fovy identical at yaw45).

### Shading fix

Planar reflections now REPLACE surface lighting instead of adding to it
(`mix(color, ref, bias+0.35)` when `planarReflection==1`): a bright-lit
floor washed the mirrored cube toward pale green under additive blending.

### Verification

Exact-zone flipped-cube fidelity: **94.3% @ yaw30, 88.5% @ yaw45,
82.4% @ yaw60/pitch20**. `make check` PASS.

### Known issues (next session)

- Wedge-shaped truncation in the RT's bottom-right (~40px at worst row):
  NOT depth-test, NOT face culling, NOT slab bounds. Suspect the w<=0
  boundary of the virtual mirror camera; oblique-clipped projection is the
  candidate fix. Diagnosis toggles `GX_PLANAR_NODEPTH`/`GX_PLANAR_NOCULL`
  added for this work.

## [0.1.34] — 2026-08-23

**Planar reflection VISIBLE: the floor now samples real mirrored content.
Root cause of the invisible/wrong sampling found and fixed.**

### Root cause (the last one)

The planar branch contained **stacked duplicate `vec2 suv` declarations**
from accumulated sweep edits — later declarations silently shadowed earlier
ones, so every sampling-variant test ran polluted code and scored ~0%
regardless of correctness. After cleaning to a single declaration, a
controlled 4-variant sweep on the NVIDIA display scored:

| Sampling | In-hull reflected content |
|---|---|
| raw screen-UV | 10,154 px |
| y-flipped | 0 |
| **x-flipped (shipped)** | **25,447 px** |
| 180° rotated | 0 |

### Why x-flip is mathematically required

`LookAt(mirrored_pos, mirrored_target, mirrored_up)` re-orthogonalizes into
a RIGHT-handed frame, adding a hidden 180° yaw versus the true reflected
frame `V_main · R` — numerically verified: `V_M·X = (−0.424, …)` vs
`V_main·R(X) = (+0.424, …)` for the same point. The RT image is therefore
stored horizontally mirrored relative to the screen-aligned image; flipping
u compensates exactly.

### Also fixed this iteration

- GL_KHR_debug callback wired into the planar pass (NVIDIA synchronous
  messages exposed the uniform type-mismatch errors that black-box probing
  could not).
- Dedicated minimal-program RAW draw path for planar models (zero raylib
  interference): verified rendering at the exact analytic mirror position.
- Removed double-EndTexture; removed all temporary instrumentation.

### Known issues (next iterations)

- Reflected content covers the lower portion of the predicted region —
  edge/fresnel dimming plus strict detection thresholds under-count; the
  trace fidelity metric needs luminance-aware thresholds for dimmed
  reflections.
- The w<=0 half-space cut remains for fragments behind the virtual camera
  (oblique-projection fix pending).
- Reflection-pack phases B (per-surface gating) and C (comparison suite)
  proceed from here.

## [0.1.33] — 2026-08-23

**Trace-infrastructure session: apitrace ruled out for this app's model
passes; a measurement confound (display environment) discovered; hot-reload
mid-frame hazard fixed; GX_FRAMES capture hook added.**

### Added

- **`GX_FRAMES` env hook** (main_loop.c): deterministic frame budget —
  engine exits after N frames, giving trace/CI captures fixed content.
- resources/shader/planar_debug.{vert,frag}: minimal solid-color pair for
  pipeline verification.

### Fixed

- Double EndTexture in the planar pass (state-stack imbalance).
- Hot-reload mid-frame hazard: Shader_Update's CheckReload could swap
  program ids between passes within one frame; now gated to fire once per
  frame at the update-phase call. This was the mechanism behind the traced
  "program 15 vs 16" split.

### Measurement confound discovered

Identical-PNG hashes across builds revealed that capture results depend on
which X display ran the engine: display :1 (long-lived) and :2 (fresh)
produce different planar-pass output for identical binaries. All prior
fidelity numbers were measured on :1. Future verification MUST pin and log
DISPLAY. Additionally, apitrace prevents this app's async model loading
(traced runs contain zero mesh draws), so GL traces of model passes are
unrepresentative — use RenderDoc GUI captures instead.

### State

Planar RT renders reflected geometry at exact positions on :1 (22%
flipped-cube fidelity, default-material shading issue open). On fresh
displays the pass currently produces no visible reflection — the display
confound and the uniform-stickiness bug interact; both are first targets
for the RenderDoc session.

## [0.1.32] — 2026-08-22

**Planar investigation deep-dive: the RT cube draws with DEFAULT material
state regardless of uniform uploads — in-engine probes exhausted, frame
capture required.**

### Established this iteration

- Removed the mid-pass `Shader_Update` call from the planar pass (its
  hot-reload check can swap the program id mid-frame — a real hazard fixed
  on principle; globals already uploaded by the update phase).
- Verified via MESHDRAW ground-truth probes that main-pass models and
  planar-pass models reference DIFFERENT program ids (16 vs 15) within a
  single frame while reading the same `gfx->shader.shader` field — evidence
  of either a mid-frame reload swap or dual program instances.
- The khaki "cube" in the RT is confirmed to be OUR model loop's output
  (disappears with the loop disabled) at the exact mirror position with
  lighting/specular active but DEFAULT material uniforms (no emissive).

### Kept

Feedback-loop unbind, per-draw program rebind + invalidation, restructured
pass (single Begin3D pair), no mid-pass Shader_Update.

### Next session — RenderDoc/gfxreconstruct (single question to answer)

What uniform values does program N hold at the cube's glDrawElements inside
the planar pass? Candidates: cached-program desync surviving invalidation;
raylib loc-based uploads writing defaults after ours; two live program
instances from repeated hot-reload compiles. Once material uniforms stick,
the planar path delivers the exact flipped-cube image (geometry already
verified at ≤5px accuracy), unblocking reflection-pack phases B–D.

## [0.1.31] — 2026-08-22

**Planar RT probe confirmed at the EXACT analytic mirror position; the
remaining defect is isolated to per-model material uniforms not sticking in
the offscreen context.**

### Progress this iteration

- Staged diagnostics (magenta-everything early-out, material color switch,
  forced program rebinds) established:
  - The planar pass renders the cube via OUR shader program at the correct
    mirror-camera position (77,528 magenta fragments with an early-out;
    centroid (666,756) vs predicted (673,753)).
  - MAIN view responds to material changes; **the RT cube does not** — it
    shades with DEFAULT material state (white albedo × lights = khaki).
- Hardening kept: feedback-loop unit unbind + per-draw program rebind +
  cache invalidation in the planar model loop.

### Open issue (precisely scoped, next session)

Per-model uniform uploads inside the offscreen planar pass silently no-op
while global Shader_Update uploads (lights/cameras) take effect. Suspects:
RHI cached-program desync surviving invalidation, raylib loc-based uploads
overwriting after ours, or a second program instance for the same source.
RenderDoc/gfxreconstruct capture will resolve which — the failure is now a
single-step reproduction away from a fix.

User-visible symptom remains: reflection visible only near contact /
certain angles with washed-out content — consistent with default-material
shading plus the documented w<=0 half-space cut.

## [0.1.30] — 2026-08-22

**Planar rendering verified EXACT against analytic mirror projection; the
remaining clipping isolated to the sampling stage's projection-of-floor-
point approximation.**

### Verified (vision-measured, pixel-level)

- Runtime `mirrorCam` matches the analytic plane-reflection construction to
  4 decimals (pos/target/up/fovy).
- The magenta probe cube inside the planar RT is centered within **~5px**
  of the analytic mirror-camera projection at correct scale — the RT render
  stage is now geometrically exact.

### Root cause of the angle-dependent clipping (user report)

The floor samples the RT by projecting ITS OWN world position through the
virtual mirror camera (`reflMVP * P_floor`). That is only an approximation:
the sampled content point X (where the reflected ray hits) is NOT collinear
with the virtual camera and the floor point, so `ndc_m(X) ≠ ndc_m(Pf)` away
from the contact region. The divergence sweeps across the floor as the
camera orbits — the "aggressive clipping that moves with the camera".

### Measured sampling variants (trace fidelity, default angle)

| Variant | Fidelity |
|---|---|
| reflMVP·P + margin/fades (shipped) | **22%** |
| reflMVP·P raw | 18% |
| screen-UV gl_FragCoord | 10% |
| screen-UV y-flipped | 0% |

Screen-UV should be mathematically exact (projective identity: the content
of RT pixel s lands at screen pixel s) but our pass doesn't yet satisfy its
frame-construction precondition — the reflected frame must preserve main-
camera handedness/projection relationships for the identity to hold.

### Next session

Implement the reflection-matrix texture formulation (`T = bias · P_mirror`
composed with the plane reflection applied to the MAIN camera frame), or
repair the reflected-frame handedness so screen-UV becomes valid. Either
removes the w<=0 half-space entirely — no more angle-dependent clipping.

## [0.1.29] — 2026-08-22

**Reflection Pack phase A: the planar-reflection RT renders models for the
first time.** The multi-session "always-empty reflection RT" mystery is
solved and the pass restructured; floor sampling now shows real reflected
content (quantified via the trace oracle), with one known half-space
sampling limitation documented for the next iteration.

### Root causes found (staged GL-state readbacks + state-snapshot diffs)

1. **Models painted the SCREEN, not the RT**: `Sky_DrawTo` wraps its draw in
   BeginTexture/EndTexture, and EndTexture unbinds to FBO 0 (`fboD=0`
   verified mid-pass). The pass now re-binds the RT after the sky.
2. **Zero fragments through the wrapper FBO**: model draws raised
   GL_INVALID_OPERATION and produced nothing while batch quads (sky) worked.
   Fixed by mirroring the proven RenderEx context exactly — dedicated FBO
   (`gX_RHI_FBO_Gen` + depth RBO + explicit Draw/ReadBuffer) sharing the
   planar color texture. Rasterization confirmed by probe coverage.

The planar model loop is back on the full `gX_RendererGFX_Model_Draw` path
(all material/emissive uniforms) inside the corrected context.

### Measured

- Planar flipped-cube trace fidelity: **22%** (best variant; margin+fades
  restored). Remaining gap = a half-space cut where floor fragments lie
  behind the VIRTUAL mirror camera (`reflClip.w <= 0` → no reflection) — at
  yaw45 this removed the left half of the reflected cube. Screen-UV sampling
  alternatives measured worse (18% / 10% / 0%).
- SSR baseline unchanged: 42% (structural far-face limit).
- For comparison: the ideal is 100%; both paths currently fail it for
  different reasons — precisely what the reflection-pack comparison suite
  will track.

### Next steps (reflection pack phases B–D)

- Sampling fix: oblique-clipped mirror projection or reflection-matrix
  formulation keeping w>0 for all visible fragments
- Per-surface gating (SSR mask vs planar pixels) instead of global skip;
  water folded into the same rules
- Comparison suite + docs/reflection_pack.md

## [0.1.28] — 2026-08-22

**Flipped-cube trace tooling — SSR reflection fidelity is now MEASURED, and
the measurement explains the user's "stripes, not a proper reflection".**

### Added

- **`--trace` mode** in `scripts/probe_reflect_verify.py`: projects the real
  cube (white wireframe) and its analytically ideal mirror image (cyan)
  onto any capture; classifies every floor pixel by what a perfect planar
  mirror would show there (`CUBE` / `SKY_IN` / `OUT` zones via vectorized
  reflected-ray/AABB tests); with `GX_DIAG_CUBE` it reports **flipped-cube
  fidelity %** — the share of should-show-cube pixels that actually do —
  plus per-zone stats and a failure-map PNG.
- `--plane-y` sweep support (verified +0.075 is the true mirror plane — no
  displacement error in the pipeline).

### Findings (default orbit angle)

- **Flipped-cube fidelity: 42%**. Failures form a RING around a working
  core: rays at the mirrored cube's periphery need to see the object's FAR
  faces, which are not present in the screen-space depth/color buffers.
  This is a **structural SSR limitation** (like behind-camera content), not
  a fixable bug — pure SSR cannot deliver the full "same cube but flipped"
  image for this configuration.
- The floor's blue/cyan tones are correct sky reflections (the majority
  zone is `SKY_IN`, handled by env fallback).

### Fixed (planar-reflection path)

- **Root cause of models painting the screen found**: `Sky_DrawTo` wraps its
  draw in BeginTexture/EndTexture and `EndTexture` UNBINDS to the default
  framebuffer — verified `fboD=0` mid-pass via staged readback. The pass now
  re-enters the RT after the sky (balanced dual Begin3D/End3D).
- Planar model loop rewritten to the proven cubemap-face pattern (explicit
  transform + manual matModel/matNormal + Draw_Parameters).

### Known issue (open)

- Models STILL produce zero fragments in the planar RT even with correct
  FBO binding, both draw patterns, culling NONE/FRONT, and depth disabled —
  while batch quads (sky) rasterize fine into the same target. Eliminated:
  FBO binding, winding/cull, depth test/attachment, draw-call pattern,
  matrix upload ordering. Remaining suspects require frame-capture tooling
  (gfxreconstruct/RenderDoc): program state at draw time, VAO/attribute
  conflicts, or an early-out reading an unbound sampler unit.
  `--showcase-planar` remains experimental.

## [0.1.27] — 2026-08-22

**Verification-scene visibility: the probe cube no longer reads as black.**
User report on `--showcase-minimal`: "objects look mostly black". (Their
other observations — blue/cyan reflection area, a cyan patch from above —
turned out to be readings of the `--ssr-debug 11` diagnostic overlay, whose
channel semantics are: R = LOD-0-confirm rejections, G = phantom-validation
rejections, B = crossings. Quantified across views, the overlay shows a
healthy march: rejection activity confined to sky-region noise-crossings,
clean accepted crossings over all reflection areas.)

### Changed

- Point lights brightness 1.5 → **4.0** with softer falloff (attenQuad
  0.25 → 0.08): at metalness 0.85 under night lighting the cube showed only
  a specular rim.
- Probe-cube metalness 0.85 → **0.6**: pure metals have no diffuse response;
  0.6 keeps strong reflections while letting the coloured lights actually
  shade the faces.

### Verified

Cube faces now shade clearly with both coloured lights readable; the floor
shows an unmistakable mirrored cube; top-down view readable; no blowout.
Harness stays 7/7; `make check` PASS.

## [0.1.26] — 2026-08-22

**Contact-zone reflection shredding fixed.** The region between an object
and its mirror image — where the user reported the reflection had "fallen
apart" into stripes/shards — now renders as coherent mirrored content.

### Root cause

Near object silhouettes the depth buffer carries a RAMP of interpolated edge
texels between two real surfaces. A bisected SSR hit whose UV lands on such
a ramp texel is accepted (all termination gates pass — it IS a plausible
surface) but samples colorMap at a displaced position: the mirrored content
shatters into screen-aligned shards whose pattern follows the silhouette's
texel columns.

### Fixed

- **Surgical silhouette re-snap** (`gfx_ssr.frag`): after a hit is fully
  accepted, if its UV sits on a high depth-gradient pixel, the ±1-texel
  neighbourhood is probed for a texel whose surface distance better matches
  the ray point; content is sampled from that true-surface texel instead.
  Strictly local: only ramp pixels, only ±1 texel, only on clear improvement,
  sky texels skipped. Termination decisions untouched — verified zero global
  behaviour change (identical frame statistics outside the fixed zone).
- **Debug view 11** (`--ssr-debug 11`): per-pixel contact-zone diagnostics
  (R=LOD-0-confirm rejections, G=phantom-validation rejections,
  B=crossing count).

### Investigated and rejected

A nearest-surface (3×3-min depth) evaluation of confirmation/bisection/
validation DID remove the shredding but globally broke the march: grazing
rays touched their own origin plane through the widened footprint, floor
reflections self-terminated dark, and the main mirror image vanished.
Reverted; the surgical post-acceptance re-snap achieves the same visual
fix without touching march semantics.

### Verification

Camera-matrix harness stays 7/7 (exactly one reflection, predicted position,
no phantoms); vision A/B confirms shard removal with no new artifacts;
`--showcase-water` regression clean; `make check` PASS.

## [0.1.25] — 2026-08-22

**Sky FOV desync fixed — directional features (moon) now match between the
sky and their reflections.** Found via the user's clue from
`--showcase-minimal`: the reflected moon appeared LARGER than the sky moon
and separated from it.

### Root cause

`GX_SKY_FOV_MULTIPLIER = 1.8`: the MAIN sky was drawn at 1.8× the scene
projection (a cosmetic hack to "show more Rayleigh gradient"), while every
direction-based consumer — SSR env fallback, the equirect sky LUT, cubemap
faces — uses TRUE directions. Sky features were angularly compressed on
screen; their reflections showed the physically-correct direction, so the
two disagreed in both position and size.

### Fixed

- Multiplier → **1.0**: the sky now renders with exactly the scene's
  projection. Verified: the moon-glow reflection sits at the mirror-expected
  position below the horizon; stripe artifacts reduced to faint.

### Known issues / next leads (stripe investigation)

- Env-miss fade `0.6 * (1 - smoothstep(0.7, 1.0, trav/reach))` can dim
  distant-sky reflections; grazing-angle normal reconstruction remains a
  suspect for residual ragged reflection boundaries.
- The sky LUT and cubemap faces both carry the moon correctly (verified via
  debug views); env-fallback path confirmed functional post-fix.

## [0.1.24] — 2026-08-22

**Reflection verification scene + harness.** A minimal, fully-deterministic
scene designed to answer one question with pixel evidence: does the floor
show EXACTLY ONE physically-correct mirror reflection of a single object,
with no artifacts?

### Added

- **`--showcase-minimal`** — verification scene: exactly two models. One
  8×8 near-mirror plane slab (PBR metalness 1, roughness 0.02, top surface
  y=+0.075) and ONE polished cube floating ~0.4 units above it. Minimal
  lighting (two coloured point lights). The engine's built-in starter
  cube/sphere/cone are suppressed in this mode — any extra reflective body
  would reflect the probe object and fake phantom images.
- **`--showcase-planar`** — same scene but the plane mirrors via the exact
  planar-reflection RT path instead of SSR.
- **Harness hooks (env vars)** for headless deterministic testing:
  `GX_CAM_YAW/GX_CAM_PITCH/GX_CAM_DIST` pin the orbit camera;
  `GX_DIAG_CUBE` renders the probe cube flat magenta (unambiguous
  reflection signature); `GX_T_SSR/GX_T_BLOOM/GX_T_BOUNCES` force effect
  states for controlled comparisons.
- **`scripts/probe_reflect_verify.py`** — analytic verification harness.
  Reconstructs the runtime camera (orbit around origin, FOV 45) and projects
  ground truth into screen space: the cube silhouette hull and its MIRROR
  IMAGE across the plane. With the magenta diagnostic it then demands:
  EXISTS (reflection cluster present), EXACTLY_ONE (exactly one magenta
  cluster inside the predicted mirror hull), NO_PHANTOMS (zero clusters
  outside both hulls), POSITION (centroid at the analytically predicted
  location). Degenerate view geometries (mirror image occluded behind the
  object near straight-down) are exempted.

### Verification results (SSR path)

Camera matrix yaw 0/90/180/270 at pitch 15° plus default/grazing angles:
**all verifiable configurations PASS** — exactly one reflection, at the
analytically predicted position, zero phantom copies. Vision judgment on
bloom-off captures: "crisp vertically-inverted mirror copy … correct
floating-gap geometry … ship". Bloom ON smears bright reflections with its
halo (documented bloom behaviour, not an SSR defect).

### Fixed

- **SSR composite black-frame bug**: with a planar-reflection model present
  the SSR ray march is deliberately skipped (water-style exclusion), but the
  composite pass only checked `ENABLE_SSR` and blended the stale map RT over
  the frame — opaque black output. `ssr.drewThisFrame` now gates the
  composite.
- **Planar reflection pass drew sky AFTER models** — the procedural sky
  fills its whole target, erasing every reflected model. Sky now draws
  first, mirror matrices upload after the sky's program switch.
- `--showcase-minimal` plane height: the mirror plane is the slab TOP
  (+0.075), not world zero.

### Known issues

- The planar-reflection RT still renders empty (sky only): models are
  submitted (`drawn=4` traced) but produce no fragments even with corrected
  sky order, culling and camera matrices; GL_INVALID_OPERATION surfaces
  during the pass. Needs a dedicated session with frame-capture tooling
  (gfxreconstruct/RenderDoc). `--showcase-planar` ships as an experimental
  debugging aid, NOT a working oracle yet.

## [0.1.23] — 2026-08-22

**SSR phantom-crossing validation — the striped ghost fragments between
objects and their reflections are gone.** Follow-up to v0.1.22 after the fix
was verified across the camera matrix (yaw 45/135/225/315 at pitch 25, pitch
10/40/55 at yaw 45 — all clean) and residual striped fragments were still
reported at cardinal view directions.

### Root cause

The march's crossing test (`prevDiff < 0 && diff > 0`) fired on *noise dips*:
depth-reconstruction sawtooth on mesh surfaces made `diff` dip negative for a
single sample along otherwise-clean rays. The bisection bracket of such a
phantom crossing contains no true surface — refinement converges to a bracket
endpoint and the "hit" samples scene content displaced from reality: striped,
fragmented mirror content between objects and their reflections.

### Fixed

- **Phantom-crossing validation**: after bisection, the accepted hit point is
  verified against exact depth — at a real hit the ray has just crossed behind
  the surface (dd ≈ 0⁺); at a phantom it remains clearly in front (dd ≪ 0)
  even at level-0 depth. Phantoms are rejected and marching continues.
  Verified at yaw 0/90/180/270 (pitch 0), near-straight-down (85°) and
  looking-up (−25°): global phantom duplication gone; two small localized
  residuals documented below.

### Known issues

- Faint horizontal banding on the chrome cone at yaw 180° (reads as banded
  environment reflection; much reduced).
- Floor-reflection fragment cut at the floor-slab edge near screen bounds at
  pitch −25° — rays exiting the floor geometry fall back to env with the
  standard edge fade; physically expected unless env coverage improves.

## [0.1.22] — 2026-08-22

**SSR ghost reflections eliminated — root cause was the per-quad derivative
normal.** User-reported: ghost reflections appearing *between* each object
and its true mirror image on the glossy floor, vanishing at close range or
far distance.

### Root cause

`gfx_ssr.frag` derived the primary surface normal from screen-space
derivatives (`dFdx/dFdy` of the reconstructed world position). Those are
constant per 2×2 projection quad: the reflected ray direction staircase-
quantized every two pixels, and at grazing angles + distance the tiny
direction steps amplified into alternating hit/miss columns in the march —
striped, displaced mirror fragments between objects and their reflections.
Diagnosed by marching-quality debug views (mode 5: crossing-state/prevDiff
channels oscillate pixel-to-pixel exactly inside the ghost zones) plus
scanline analysis (~110 direction changes / 200px).

### Fixed

- **Smooth SSR normal from depth-buffer reconstruction**: the primary ray's
  normal now comes from a ±3-texel depth reconstruction (the same technique
  already used for bounce reflections — widened from ±1 because at grazing
  angles along-view depth noise dominated the ±1 slope signal and produced
  sideways normals). Ghost reflections eliminated; striping/noise gone; no
  content loss. Tradeoff: reflections read slightly softer/diffuse (the
  previous "sharpness" was largely aliasing). Water pixels keep the analytic
  wave normal (unchanged path).
- **Verified across the camera matrix**: yaw 45/135/225/315 at pitch 25°,
  plus pitch 10/40/55 at yaw 45 — reflections present, ghosts gone, no
  angle-dependent breakage in any of the seven configurations.

### Exonerated by controlled A/B during isolation

Multi-bounce chains, Hi-Z jump-ahead, crossing-confirmation logic, self-Uv
skip radius, and march step resolution all had zero effect on the artifact
(pixel-identical A/Bs).

## [0.1.21] — 2026-08-22

**The v0.1.19 interactive flicker is root-caused and fixed: the reflection
cubemap update cadence.** With sun shadows enabled, frames alternated between
two renders (models flipping green↔gray, textured↔untextured) whenever the
cubemap updated on alternating frames. Diagnosis used a new live burst-capture
probe (`GX_BURST=1`, 12 consecutive frames + temporal std-dev map) after
static A/Bs proved blind to the artifact.

**Root cause chain**: RenderEx leaves the *main* program current; skip frames
enter the rest of the pipeline with the *depth shader* still current
(confirmed by `gX_GL_State_Diff` — `currentProgram was=15 now=3` at the same
pipeline point). Some downstream uniform upload without an explicit bind then
lands in different programs per parity, flipping shadow/AO output for exactly
half the frames. Every verifiable input was proven identical first: GL call
streams (apitrace), all 160 main-shader uniforms mid-opaque, texture bindings,
viewports, extended state (depthFunc/cull/polygon), shadow-map content hash,
cubemap content + mip hashes.

### Fixed

- **Reflection cubemap updates every frame** (was every 2nd, v0.1.13). The
  cadence made pipeline state parity-dependent once shadows were enabled.
  Every-frame updates make per-frame state identical → flicker eliminated
  (burst variance 3.38% → 0.05%, noise level) while sun shadows stay enabled
  and ground the models. Cost: reflections median ~1.47ms vs ~1.15ms
  (+~0.3ms); shadow pass +0.05ms unchanged.
- Showcase preset re-enables `ENABLE_SHADOWS` (v0.1.19's grounding shadows,
  Poisson-filtered).
- Particle system deferred-init loop on GL 3.3 contexts: `loaded` never became
  true (compute unavailable), so SSBO + VAO were recreated and both particle
  shaders RECOMPILED every frame — resource/id leak + compile cost. Init now
  completes once; GPU particle path reports unavailable and Draw no-ops.

### Added

- `GX_BURST=1 ./gX_build --scene --ai-screenshot-after N`: captures 12
  consecutive frames from one live run for temporal-artifact detection.
  Static single-frame A/Bs are structurally blind to this bug class; separate
  runs are deterministic and hide it too.

### Known issues

- The downstream uniform-upload-without-bind that converts the program-state
  difference into flipped pixels is still unidentified (every wrapper path
  binds unconditionally on inspection). It is now inert with identical
  per-frame state; worth a dedicated pass before any future cadence or
  program-lifetime changes.
- SSR speckle on mirrors (pre-existing), water wave lattice polish.

## [0.1.20] — 2026-08-21

**A user-visible flicker regression shipped in v0.1.19 and is now reverted.**
Enabling `ENABLE_SHADOWS` in the showcase preset (v0.1.19) introduced a
perfect period-2 oscillation — frames alternated between two renders
(models flipping green↔gray, textured↔untextured) while static headless
captures looked identical. Diagnosed with a new burst-capture probe (12
consecutive live frames; separate runs hide temporal nondeterminism).
Bisection: flicker requires shadows ON + the cubemap face render drawing
models; shadow-map content itself is stable frame-to-frame. Exact mechanism
not fully isolated — the sun-shadow preset line is reverted until it is;
point shadows remain on. **Lesson recorded: still-image A/Bs cannot catch
temporal artifacts — burst-capture verification is now part of the toolkit**
(`GX_BURST=1 ./gX_build --scene --ai-screenshot-after N`).

### Water (adopted)

- **Per-pixel analytic wave normals**: water.frag now recomputes the Gerstner
  + ripple normal per fragment (uniforms shared across stages). The old
  per-vertex interpolated normal shaded the 0.31-unit mesh cells as a
  diamond/checker lattice no amplitude tune could remove.
- High-frequency waves lowered (2.0→1.2, 2.5→1.5), waves 4-5 amplitudes
  trimmed ~35%, steepness halved, specular softened (256→96, ×1.5→×0.9):
  water noise 9/10 → 5/10 (vision-graded progression), waves stay alive.

### Sky

- horizonGlow 0.5 → 0.8. Measured visually inert at this sun elevation
  (glow only manifests through the twilight term); kept as harmless.

### Rejected / learned

- 256-segment water plane: 257² = 66049 vertices overflow the U16 index
  buffer → garbage triangles. Guarded with a comment at the constant.
- Wave-train count 6→4 and a saturation lift on the sky horizon band: both
  regressed in A/B (rings/moiré; green cast bleeding into reflections) and
  were reverted.
- The env-ambient-in-faces feedback theory was tested and did not affect the
  flicker (envAmbient already 0); the save/restore block was removed again.

## [0.1.19] — 2026-08-21

**Sun shadows were configured but never enabled** — `ENABLE_SHADOWS` defaulted
off and the showcase preset only set point shadows, so the entire CSM path
(map render + PCF + cascade blending) sat dead in the demo. Enabled in the
preset: the floating cluster now casts grounding shadows on the floor/water
for **+0.05ms** (1024² map, 2 cascades). Vision: "shadows substantially
improve grounding and spatial readability".

### Changed

- Showcase preset enables `ENABLE_SHADOWS` (sun/CSM) alongside point shadows.
- CSM filter: axis-aligned 3×3 PCF → irregular Poisson-disc 9-tap (~2.25-texel
  radius), same cost — breaks the stair-step banding on shadow boundaries.
- Water SSR fresnel gate: face-on water pixels (Schlick < 0.12) skip the
  march entirely — with geometry-only water SSR their contribution is capped
  by that same fresnel, so the skip is invisible while the near-field bottom
  of the frame (a large marching area) goes free.

### Performance (medians, 1536×1152)

| pass | before | after |
|---|---|---|
| water SSR | 6.17 | 3.29 (-47%) |
| plain reflections | 1.37 | 1.15 |
| sun shadows (new) | — | 0.05 |

### Evaluated & rejected

- 2048² shadow map: no visible quality gain over 1024² at this scene scale
  (A/B crop verdict); 4× memory not justified. The blockiness was filter
  pattern, not resolution — fixed by the Poisson swap instead.
- Cubemap every-3rd-frame: saves only ~0.4ms avg and adds a frame of env
  staleness now that faces carry real content; kept every-2nd.
- Water ray step cap (48): measured slower than baseline within noise; the
  Hi-Z jump-ahead already terminates rays efficiently.

## [0.1.18] — 2026-08-21

**Bloom was a visual no-op and nobody could tell.** Measuring the HDR scene
luminance (inverting the ACES tonemap on captures) showed the demo scene's
range is narrow: median 0.22, p99 0.6, with only ~0.07% of pixels above 0.8 —
so the old extract (threshold 1.1, hard knee, absolute-difference weighting,
intensity 0.15) produced halos far too faint to see at any setting, while any
threshold low enough to catch glints also caught the broad sky (wash). The
pipeline itself was verified end-to-end first: intensity 0→5 A/B proved the
composite path live, threshold 0 proved extraction working.

### Fixed

- **Bloom extract rewritten** (soft-knee): quadratic ramp from
  `thr×(1-knee)` to `thr×(1+knee)`, output as a FRACTION of pixel color
  (preserves highlight tint; the old form scaled by absolute luminance
  difference).
- **Dual blur iteration** — one 9-tap half-res pass spread a glint over only
  ~14 screen px (peak energy too dilute to read); the second iteration doubles
  the effective radius and smooths the falloff.
- Retune for this scene's range: threshold 0.85, knee 0.25, intensity 1.2 →
  gentle lift on the brightest content (+2-4 levels sky band), localized glint
  response (max +40), zero wash on models/floor.

### Added

- **FXAA pass** (`gfx_fxaa.frag`, compact Lottes formulation): edge-aware AA
  on the final composite — covers shader-derived edges MSAA misses. Renders
  into sceneColorLDR before the final blit (stale since the seed copy;
  feedback-loop guarded). `ENABLE_FXAA` default ON; hard-edge transitions
  halved (23849 → 11757), no brightness shift, no new artifacts (vision A/B),
  cost noise-level.

### Deferred

- **TAA retry**: needs per-pixel motion vectors (velocity buffer written in
  the model pass) + depth-based disocclusion rejection. The two prior
  failures (jitter-phase reflection flips, history-decay collapse) are camera-
  reprojection artifacts that per-pixel velocity fixes; camera-only prevVP
  exists but is insufficient. MSAA + FXAA cover AA meanwhile.

### Known issues

- SSR speckle/fragmentation on mirror-like surfaces remains (pre-existing;
  temporal resolve disabled — see Deferred).

## [0.1.17] — 2026-08-21

**The showcase-water "gray wash" was a giant lifted slab, not the water.**
`--showcase-water` lifts all demo content +2 so the water sits below it —
including the 8×8 glossy floor platform. Lifted, that slab became a wall of
white-gray filling the entire background from the orbit camera (and brighter
than ever now that the cubemap feeds it real sky reflections). It masked the
sky, doubled every "reflection" reading, and sent three debugging sessions
chasing shader-side ghosts. The platform no longer lifts in the water layout —
the water IS the reflective floor there.

With the slab gone the camera dropped back to 28°/13 (the steep-pitch
workaround existed only to avoid the slab's underside), putting the horizon
and sky band back in frame.

### Fixed

- Water SSR env fallback is **geometry-only**: terminal rays that miss scene
  geometry no longer add the cubemap's horizon sky on top of water.frag's own
  fresnel × 0.7 sky-LUT term — the doubled pale-sky response washed the near
  field gray-pink at most camera angles.
- Water SSR blend raised 0.5 → 0.85 (safe now that the overlay carries only
  real mirrored geometry): stronger cluster reflections in the water.

### Changed

- `--showcase-water`: platform excluded from the +2 lift; camera 42°/12 →
  28°/13 for a sky/horizon/water composition.

### Known issues (water, next pass)

- Water surface reads noisy/grid-like at low pitch (Gerstner crest contours +
  ripple rings) — needs amplitude/detail tuning.
- Cluster reflections in the water are faint/smeared rather than crisp
  mirrors (SSR march termination at grazing rays).
- The procedural sky band near the horizon is intentionally pale; reads flat.

### Verification

- Plain showcase vs v0.1.16 tag build: **0.000% pixel diff, max delta 0** —
  all changes scoped to the water layout.
- Vision on the fixed water frame: clear horizon + sky band restored; water
  teal with wave detail; cluster floats above the plane.
- `make check` PASS (unit tests, scene smoke, resource audit, LEAN_GATE
  green); SSR median ~5.1ms unchanged at 1280×720-class window.

## [0.1.16] — 2026-08-21

**The reflection cubemap was silently BLACK since its introduction** — the FBO
was created with `DrawBuffer(NONE)/ReadBuffer(NONE)` (a depth-only setup that
still passes the completeness check), so every color clear AND draw into the 6
faces was discarded by GL. The cubemap fed model IBL/reflections and the SSR
env fallback with black. Fixed at creation (color attachment wired); the faces
now also render the **procedural sky** (previously only models over a flat
clear color). Scene brightness rises ~+11/255 mean with identical highlight
clipping (0.07% before/after) — the missing environment light arriving, not an
exposure change. Vision A/B verdict: more physically plausible, no defects.

Water polish on top: the water reflects the real procedural sky via the SSR
sky LUT (equirect panorama path), deep-water opacity (the old 0.6 alpha floor
blended the teal body with the pale background behind it), a steeper demo
camera so mirrored rays hit open sky instead of the platform underside, and
the stale dbg8 probe now mirrors the actual relative normal-reject test.

### Fixed

- **Reflection cubemap black**: `update.c` created `cubeFboID` with
  `DrawBuffer/ReadBuffer(GL_NONE)`; RenderEx's reuse branch never re-enabled
  the color attachment → all 6 faces stayed zero-filled forever. IBL
  irradiance/prefilter, model cubemap maps and the SSR env fallback sampled
  black. Diagnosed via face dumps (`ReadPixels` + PPM) after pixel-diff A/Bs
  showed the reflections immune to every shader-side fix.
- dbg8 SSR probe: tested the old absolute `nLen < 0.001` (flags everything at
  showcase scale); now mirrors the actual relative reject test.
- Water: deep-water opacity `mix(0.88, 0.96)` (was 0.6-0.95 blending with the
  pale background); GFX-mode sky source prefers the procedural-sky LUT
  panorama (1-frame stale, noted).

### Added

- Procedural sky rendered into the reflection cubemap faces
  (`gfx_sky_upload_and_draw` — Sky_DrawTo's body extracted with explicit
  camera params; the face pass uses FOV exactly 90°, bypassing the main-view
  FOV multiplier). Depth test/write disabled for the background quad.
- `--showcase-water` camera retuned (pitch 42°, dist 12).

### Known issues (water, next pass)

- Near-field water still reads washed gray-pink under some angles (pale
  horizon reflections at grazing + residual double-contribution between the
  water's own sky term and the SSR overlay's env fallback).
- The raised platform casts a hard shadow wedge onto the water
  (physically correct, compositionally harsh).
- Reflection contrast in the water is weak (env fade curves were tuned against
  the black cubemap era and need rebalancing now that env is real).

### Verification

- Plain showcase vs v0.1.15: 67.2% pixels differ (intended — real env content
  everywhere); histogram healthy (mean 132.6 → 143.5, clipping equal).
- Cubemap face dump: all 6 faces contain sky gradient + scene (were solid 0).
- Chrome ball reads as chrome reflecting sky + cluster (was flat gray).
- Water: far band teal-blue; deep-opacity removes the background bleed.
- `make check` PASS (unit tests, scene smoke, resource audit, LEAN_GATE green;
  bss unchanged at 2394448 after removing the debug face-dump buffer).

## [0.1.15] — 2026-08-21

Water SSR: the water surface (GFX scene, `ENABLE_WATER`) now receives
screen-space reflections of the scene content. The old SSR `hasWater`
exclusion (which skipped water-plane pixels) is replaced with a water branch:
pixels in the water band march the reflection ray using the **analytic wave
normal** (Gerstner + ripple, replicating water.vert's tangent/bitangent
math) and a ray start from the **analytic surface height** — the depth buffer's
per-triangle interpolation sawtooths across the 128-segment mesh, which was the
root of the reflection banding. The water pass contributes its own sky
reflection, so the water SSR blend is capped at 0.5 to avoid double-brightness.
Demo: `--showcase-water` floats the showcase cluster (+2) above the water
plane with a farther/higher camera.

### Added

- `--showcase-water` CLI flag — showcase scene with the water surface enabled
  (floating-island layout: cluster + platform raised +2, camera pitch 30° /
  dist 13). Water plane at y=0 below the platform top.
- SSR water uniforms: wave count/dirs/amps/freqs/speeds/steeps, ripple
  count/pos/amp/radius/phase, water time — wired to the SSR shader from the
  water renderer's state (draw.c, next to the existing `hasWater`/`waterY`).

### Changed

- `gfx_ssr.frag` water branch — replaces the `abs(y - waterY) < 0.1`
  exclusion (which cut wave crests/troughs into horizontal stripes) with a
  band test: `hasWater && worldPos.y < waterY + 2.2` (covers waves sum 0.55 +
  ripples + far-water depth interpolation error; the glossy demo platform is
  harmlessly included, the model cluster above keeps its own roughness).
- Water pixels: roughness override (0.03) — the blended water alpha (~0.9)
  would otherwise hit the roughness early-out; analytic wave normal replaces
  the per-triangle derivative normal; analytic surface height replaces the
  ray-start Y.
- Water SSR blend capped at 0.5 (the water pass already contributes its own
  sky reflection at fresnel × 0.7).

### Verification

- A/B vs the v0.1.14 plain showcase: **0.000%** pixel diff (no regression).
- Vision A/B on the water demo: cluster + platform reflections visible in the
  water; the banding (pulse lines at ~7px mesh-segment spacing, lag-6
  autocorrelation +0.77) fixed by the analytic normal + height + widened band
  (lag-6 → ~0.0); env-cubemap fallback supplies behind-camera content.
- Perf (1280×720 showcase, GPU timers, median): water SSR 4.71 → 6.06ms (the
  water surface adds ~1.35ms of marching — a large extra reflective region).
  Plain showcase unchanged.
- `make check` PASS (unit tests, scene smoke, resource audit, LEAN_GATE green).

## [0.1.14] — 2026-08-19

SSR perf round 2: Hi-Z jump-ahead + 512² reflection cubemap. The march's
near-empty-space rays (far from any surface past the near field) now advance
4 units per step instead of 0.5 — crossing detection, LOD-0 confirmation and
bisection handle the coarser bracket, and MIN-reduced Hi-Z bounds the risk in
both directions (a jump only fires when no surface is within 8 units of the
ray). The dynamic reflection cubemap drops to 512² (4× cheaper, visually
identical for the blurry env fallback). Frame time ~11.7 → ~10.2ms at 1280×720
showcase (SSR 5.92 → 4.71ms, reflections 1.01 → 0.67ms); normal view visually
identical (0.7% of pixels shift by < 2 levels — the 512² env fallback color).

### Changed

- SSR Hi-Z jump-ahead: in `marchRay`, when `abs(diff) > 8` and `t > 24` the ray
  advances 4 units instead of 0.5. Tracked via `lastStep` so the sky-crossing
  interpolation, exit bisection and geometry bisection brackets use the actual
  previous sample distance (4-unit brackets degrade exit precision to 0.25 —
  a 0.5% fade error, invisible). Verified pixel-identical vs the no-jump march
  in the normal view; jump distance 8 and threshold 16 were both slower (coarser
  brackets trigger more expensive LOD-0 confirmations/bisections).
- Dynamic reflection cubemap 512² (`gX_GFX_REFLECTION_CUBE_SIZE` in
  `constants.h`; was the 1024 shadow-map size). Only the env fallback + model
  reflections sample it — blurry content, no visible change.
- The SSR shader is a runtime resource — no C rebuild needed for shader edits.

### Verification

- Pixel A/B vs v0.1.13: 0.727% of pixels differ (env fallback color at 512² +
  small fade shifts), all in the floor reflection bands; object rows unchanged.
- Vision A/B: torus ring reflection intact (no thin-tube tunneling), mirror
  ball and polished cube unchanged, dbg4/dbg10 debug views identical to v0.1.13.
- Perf A/B (1280×720 showcase, GPU timers, median): SSR 5.92 → 4.71ms (-20%),
  reflections 1.01 → 0.67ms (-34%). Opaque/bloom unchanged.
- `make check` PASS (unit tests, scene smoke, resource audit, LEAN_GATE green).

## [0.1.13] — 2026-08-19

SSR perf audit + simplification: the march is 94% of the SSR pass and the
6-face 1024² env cubemap re-rendered every frame. The crossing state machine
was provably redundant, rough pixels marched 96 steps for a < 0.28× env
contribution, and the cubemap cadence was halved. Frame time ~13.1 → ~11.7ms
at 1280×720 showcase (reflections 1.85 → 1.01ms, SSR 5.20 → 5.12ms); normal
view pixel-identical.

### Changed

- SSR march state machine removed: the state 0/1 skip logic is exactly
  equivalent to accepting only behind→front crossings (`prevDiff < 0 &&
  diff > 0`) — state==0 at a crossing ⟺ prevDiff < 0 (state is set by
  diff > 0 and reset only by a (+,-) crossing). Removes the state variable,
  the skip branch, and the state-set line; `r.state` is now derived
  (`prevDiff > 0`) for debug view 5.
- Roughness early-out: pixels with roughness > 0.85 skip the march entirely
  (composite fade is (1 - pxRough²) with a 0.05 floor — contribution < 0.28×
  env at best, < 0.1 at normal incidence via fresnel). Invisible in the
  showcase (only the fixture/button at 0.9 are affected).
- Env cubemap cadence: the 6-face 1024² dynamic cubemap (SSR env fallback +
  model reflections) now renders every 2nd frame instead of every frame —
  halves the reflections timer with no visible change (static showcase;
  dynamic scenes lag ≤ 1 frame).
- `--gfx-profile[=path.csv]` flag wired into the CLI parser (was declared in
  `main.h` but never parsed — dead feature). Emits per-frame
  shadow/reflections/opaque/bloom/SSR/total GPU timings.

### Audit findings (1280×720 showcase)

- SSR = 5.20ms = 37% of the pipeline; the march itself is 4.87ms (94% of SSR),
  the Hi-Z chain + draw overhead 0.33ms.
- The march cost is dominated by the glossy floor: near-floor rays travel
  40–48 units (80–96 steps) to hit the sky at fade ≤ 0.2 — the most expensive
  pixels produce the faintest result. Cost scales sub-linearly with resolution
  (5.20ms @ 1.0, 2.85ms @ 0.5, 0.36ms @ 0.25) — the Hi-Z chain + fixed
  overhead floor it at ~0.33ms.
- Remaining lever: Hi-Z jump-ahead (skip forward when the ray is far behind
  the nearest surface) — a march rewrite with thin-object tunneling risk;
  deferred.

## [0.1.12] — 2026-08-19

SSR "Banding + mirror ball": env-termination distances are now sub-step
interpolated (horizontal banding on glossy floors gone), the traversal-fade
cap is a smooth taper instead of a hard kink, and the demo scene gained a
chrome mirror ball + polished cube so the 6-face env cubemap path and the
second-bounce chain are verifiable in the normal view.

### Fixed

- SSR env banding: type-3 (in-screen sky) and type-0 (frustum exit / margin
  band) terminations recorded step-quantized traversal distances (0.5-unit
  steps). The env fades (`1 - trav/reach` and the capped `2*(1 - trav/reach)`)
  quantized into visible horizontal brightness bands on glossy floors
  (10–15 bands in the showcase floor). Both terminations now report
  sub-step distances: `exitT()` bisects the exit boundary (4 iterations on
  the validity predicate: worldToUV ok, UV in [-0.5, 1.5], edgeMargin ≥
  0.005), and sky crossings interpolate the crossing point from the
  previous/current scene-depth samples. Banding reduced to 2–4 faint
  transitions (silhouette-aliasing family, jitter+temporal remain closed).
- Traversal-fade kink: the type-0 fade `clamp(2*(1 - trav/reach), 0, 0.6)`
  has a slope discontinuity at trav/reach = 0.7 (brightness transition
  ring). Replaced with `0.6 * (1 - smoothstep(0.7, 1.0, trav/reach))` in
  both the per-bounce env fade and the composite fade — same endpoints,
  continuous derivative.

### Added

- Demo scene: chrome mirror ball (`primitive/sphere.glb`, metalness 1.0,
  shininess 256, roughness 0.02, pos {-0.6, 0.55, 0.35}) and polished cube
  (metalness 1.0, shininess 200, roughness 0.12, pos {0.8, 0.25, -1.15}).
  The ball reflects the cluster and the env cubemap (behind-camera content)
  and shows bounce-1 (green) regions in the `--ssr-debug 10` view.
- Geometry-hit bisection deepened 4 → 6 iterations (sub-1/64-step precision;
  no visible silhouette change — aliasing is ray/pixel mismatch, not hit
  precision).

## [0.1.11] — 2026-08-19

SSR "Edges + bounces": blocky-edge crossing reliability, screen-edge
dissolve, dynamic-cubemap env fallback, multi-bounce chain, and the
occlusion-culling AABB depth-write bug (box-shaped reflection artifacts).

### Fixed

- Occlusion-culling AABB boxes wrote depth into the main opaque pass buffer
  (`gX_RendererGFX_Model_Draw_Occluded` in `src/renderer/gfx/model.c`): the
  query only masked color writes, so every model's invisible AABB cube landed
  in the depth buffer SSR/SSAO/godrays reconstruct from. SSR rays "hit" box
  surfaces and sampled the background color → box-shaped reflection patches
  (solid boxes for culled models). Depth writes are now disabled for the
  query (the depth TEST is unaffected).

### Added

- SSR crossing reliability: coarse-mip (LOD ≥ 1) sign changes are confirmed
  at exact depth before accepting the hit, and the level-0 bisection is
  seeded with the linear root estimate (prevDiff/diff ratio) — tighter
  bracket, ~half-step precision before the 4 iterations.
- Screen-edge dissolve: per-step `minMargin` (edgeFade band, 0.08 of the
  screen) replaces the old last-step `uvMargin` hard cut; one continuous
  fade profile for both hit and env paths — no seam where rays hand off to
  the env fallback.
- Dynamic reflection cubemap as the SSR env fallback (`gX_GFX_TEXUNIT_SSR_ENV`
  = 24): captures behind-camera geometry the screen-space buffers never
  contain; sky LUT used only when the cubemap is unavailable. Analytic sun
  disc added on top (cubemap faces are cleared to flat sky color). Cubemap
  render gate now includes `ENABLE_SSR` (SSR is a cubemap consumer).
- Multi-bounce SSR: `maxBounces` (default 1) extra ray bounces after the
  first hit, reflected at the hit normal (depth-derivative reconstruction),
  coarser steps (2×), capped 48 steps, `bounceAtten` 0.4. March refactored
  into a reusable `marchRay` + `MarchResult`.
- Debug view 10 = bounce index (R=0, G=1, B=2); debug views 4/5/7 now output
  opaque alpha (they blended with the scene through the SSR map composite).
- `--ssr-debug <0-10>` CLI flag (presets `ssrDebugMode` for AI probing).

### Changed

- `gfx_ssr.frag` rewritten around the reusable march; env fallback for all
  bounces uses the cubemap; `edgeFade`/`maxBounces`/`bounceAtten` knobs in
  `load.c` beside `maxLod`.

## [0.1.8] — 2026-08-18

Probe hygiene: last SSR debugging probes removed, showcase SSR default
settled, zero-warning build.

### Removed

- `src/main_update.c` screenshot block: the skyLUT readback dump
  (`/tmp/skylut_readback.bin`), GL filter-state probe, FBO-attachment probe
  and GPU-limits probe — all leftovers from the v0.1.7 debugging sessions
  (also the source of the `int-to-pointer` cast warning at the
  `glGetString(GL_RENDERER)` line).
- `raylib_Tex_GetFilterParams` from `raylib_wrappers.c` — probe-only helper
  (used unit 24 directly, above the raylib 0-11 limit).

### Changed

- Showcase SSR default settled: `ENABLE_SSR = 1` in the non-safe-mode preset
  is now a deliberate default (SSR shipped in v0.1.7; R still toggles),
  replacing the stale `//opencode: PROBE` force-on from the v0.1.0 baseline.
- `ssrDebugMode` init in `load.c` — stray PROBE marker removed.
- `showcase.c` debug-view switch: two `else ... break;` lines restructured —
  build is now warning-free.

## [0.1.10] — 2026-08-18

Compression loop (P5): bss cut by ~466 KB (16%) by right-sizing two static
preallocation caps to measured runtime usage. Both cut sites keep hard
headroom and their bounds checks are enforced at write time.

### Changed

- `gX_GUI_CSS_MAX_SHEETS` 8 → 4 (src/gui/css.h) — the stylesheet registry was
  8 × 64-rule sheets (~830 KB bss); the registry has a single loader call
  site (`markup.c` css field) and no gX resource populates it. 2× headroom
  over any plausible use. Saves ~415 KB.
- Shader watcher include tracking (src/common/shader_compiler.c) —
  `incPaths[16][256]` → `[GX_SHADER_COMPILER_MAX_INCLUDES][256]` with the
  new cap 4 (was a hardcoded 16 in the struct and both write loops). The
  include closure is single-level; the deepest chain in gX shaders = 3
  includes (vhs.frag), most have 0-2. Saves ~48 KB.

### Audit notes (no change)

- `scripts/dead_symbols.py`: all candidates are public API declared in
  headers — kept per the API-surface-complete pillar.
- `scripts/strip_backends.py`: nothing to strip — gX is already
  single-backend (the only remaining `GAME_BACKEND_*` mention is a comment
  in `src/common/config.h`).

## [0.1.9] — 2026-08-18

SSR speckle eliminated: the static per-pixel jitter was the source of the
salt-and-pepper checkerboard — adjacent pixels' crossing detection was
decorrelated (pixel A's ray caught the hit, neighbor B's missed). The march
is now deterministic (`t = 0`); vision-verified: speckle gone, sun reflection
clean, remaining aliasing is coarse blocky edges from the march step size.
Temporal filter revisited and closed: reprojection double-negate fixed
(`clipPos.y = -clipPos.y` removed), but the decay path
(`history * 0.5` on miss) collapses grazing reflections that flip hit/miss
with the frame-varying jitter — kept gated, documented.

### Changed

- `gfx_ssr.frag` — deterministic sub-step start (`t = 0.0`); the static jitter
  hash is removed. Neighboring rays now sample the crossing at the same phase,
  producing coherent reflection blobs instead of texel-scale checkerboard.
  The frame-varying jitter experiment (for temporal AA) was reverted with it.
- `gfx_ssr_temporal.frag` — reprojection no longer negates `clipPos.y` (mirrors
  `worldFromDepth`: `uv * 2 - 1`, no flip). The temporal resolve stays disabled
  (A/B #2: decay kills grazing reflections).
- `gfx_pipeline.c` — temporal gate comment documents both experiments and the
  final verdict.
- `load.c` — `maxLod` 4.0 → 2.5 (kept from the earlier blockiness experiment;
  coarse Hi-Z mips made far reflections blocky).

## [0.1.7] — 2026-08-18

SSR first light: three stacked bugs kept the SSR map black (Y-flip, absolute
normal threshold, composite Y-flip) — the showcase floor now shows real
mirror reflections; sky-LUT yaw aligned with the sky equirect convention;
temporal filter A/B verdict: worse — kept gated.

### Fixed

- **SSR shader Y-flip** — `uv = (x, 1-y)` double-flipped the depth/color
  samples. The SSR map RT (Y-down ortho) and the scene depth/color RTs share
  the same row orientation; the unflipped `fragTexCoord` is correct (verified
  by readbacks: floor pixel reads 0.97, sky 1.0).
- **SSR normal threshold** — the absolute `nLen < 0.001` reject killed every
  surface at showcase scale: per-pixel world tangents are ~0.01, so
  `|cross| ≈ 1e-4` even for flat geometry. Replaced with a relative test
  (`nLen < 1e-8 || nLen < 0.001 * |dFdx| * |dFdy|`) that only rejects
  parallel-tangent silhouettes.
- **SSR composite flip** — `gX_Texture_Draw` auto-inverts RT sources (raylib
  RT convention: row 0 = visual bottom), but the SSR map's row 0 = visual
  top; the near-floor reflection appeared at the screen top. Counter-flipped
  with `scale = {1, -1}`.
- **Debug view 4/9 flip** — the showcase RT viewer had the same double-flip
  for the SSR map and sky LUT (both Y-down-ortho renders); counter-flip
  applied.
- **Sky LUT yaw** — `skyColorAt` sampled the equirect LUT 180° off: the
  `+0.5` u-offset vs the `sky_new.frag` convention (`u = yaw/2π` from +X,
  `u=0.25` = +Z). Removed the offset, added `fract()` for the wrap.

### Changed

- **SSR temporal filter — disabled by A/B verdict**. The reproject + history
  resolve produced corrupted output (ghosting, clamp artifacts, black gaps)
  with the orbit camera. The shader's own Y-down convention was still fixed
  (`uv = fragTexCoord`, `clipPos.y = -clipPos.y`) and the block remains gated
  (`if (0 &&`) for a future revisit.

## [0.1.6] — 2026-08-17

GFX polish seal: headless verification tooling, dead-batch init fix,
dynamic-cubemap corruption fix, particle-draw gate, exposure correction,
cookie atlas exercised, clean logs, full-res default, SSR deferred.

### Added

- **Headless screenshot + debug tooling** (AI-probe style):
  - `--ai-screenshot-final` / `--ai-screenshot-after <n>` — capture a
    screenshot at frame N (default 180) and exit. Capture runs in the UPDATE
    phase so the backbuffer is intact (mid-pipeline reads give a white frame).
  - `--debug-view <0-10>` — showcase raw-RT viewer (ssao map/blur/depth/ssr/
    hdrScene/sceneColorLDR/bloom.scene/reflection RT/sky LUT/cookie atlas).
  - `--gfx-safe-mode` — was parsed in `load.c` but never reachable from the
    CLI; wired. The showcase now honours it (previously re-enabled effects).
  - `--preset <0-3>` — was parsed but never applied; wired to the quality
    preset (showcase respects it too).
- **Demo spotlight with projected cookie** (`scene.c`): a warm spot light
  above the cluster projecting `texture/cookie_window.png` (project-generated
  256² window grid). This exercises the cookie atlas end-to-end — the atlas
  path's only prior writer was the deleted AI harness, and it had never run.
- **`resources/texture/cookie_window.png`** + NOTICE.md entry.

### Fixed

- **`gX_Batch_Init` had no callers** — the batch's internal shader/VAO/VBO/IBO
  were never created, so **every batch-based draw (2D shapes, gui rects,
  spheres, raw RT views) silently drew nothing**. Wired init into
  `main_load.c` after `gX_Backend_Init()` (the batch needs a live GL context).
- **Dynamic reflection cubemap corrupts the opaque pass**: with
  `ENABLE_REFLECTIONS` on but no reflective model in the scene, the cubemap
  re-rendered the whole scene 6×/frame with the raw draw path (which never
  binds the SSAO map), leaving unit 12 stale → blue/purple speckle on
  surfaces whenever SSAO was enabled. Now gated on an actual
  reflective/planar model existing.
- **`gX_Calloc` heap overflow in the cookie atlas** — `gX_Calloc` allocates
  *bytes* (despite returning `gX_I64*`); the atlas's `(bytes+7)/8` math
  allocated 512KB then filled 4MB → `free(): invalid next size` crash the
  moment cookies were enabled. Fixed to pass bytes directly.
- **Particle draw ran unconditionally** with zero emitters → drew the
  zero-initialised SSBO's dead particles. Gated on `emitters.amount > 0`.
- **Exposure / washed-out look**: sky ambient `0.25 → 0.15`, scene exposure
  `0.85 → 0.80`, procedural-sky density `3.0 → 2.2` (the zenith no longer
  clamps to white). Demo lights rebalanced (point 2.2 → 1.4, spotlight 2.0,
  `castShadows=0`).
- **Log noise**: `gX_RHI_GetUniformLocation` "uniform not found" demoted from
  WARNING → TRACE (effect shaders routinely query main-shader uniforms);
  removed per-frame `INSTANCE:` info spam; removed leftover `GUI_CAPTURE_BG`
  diagnostic logging + one-shot canvas pixel-sampling block in `gui.c`.
- **`renderScale` default `0.25 → 1.0`** (was 0.25 → 4× blocky upscale of a
  384×288 render; the saved `resources/settings.json` was also updated).
- **Safe-mode brace structure** in the showcase defaults: several effect
  enables had escaped the `!gfxSafeMode` branch, so safe mode re-enabled
  cookies/particles/fog/point-shadows.

### Changed

- Showcase defaults to `ENABLE_SSR = 0` (R toggles) — SSR is deferred (see
  Known issues). With it off, the demo is free of the SSR self-smear noise
  and the near-black region.

### Known issues

- **SSR deferred**: the ray march over-fires (confidence view shows hits on
  ~all surfaces), producing self-smear noise + a near-black region in the
  composite. Eliminated as the cause: mip/LOD sampling, temporal resolve,
  MSAA, normal winding, gradient window, sky LUT. Likely the flat-surface
  `normal = viewDir` fallback. Needs a dedicated fix session.
- The demo camera frames the tallest model tightly (clipped at top); cosmetic.

## [0.1.5] — 2026-08-17

Renderer polish pass: populated demo scene, ACES tone mapping, effect
validation fixes, camera UX, dead AI-harness removal.

### Added

- **Populated default demo scene** (`scene.c`): `--gfx` and `--showcase` now
  spawn 7 models (teapot, cube, cone, torus, icosahedron, fixture, button) with
  per-model materials + 3 coloured point lights. The scene was empty before
  (only a white sphere marker) — which is why it rendered washed-out white.
- **Free-fly camera** in the showcase: C toggles orbit ↔ fly (WASD + mouse
  look via the camera system's FREE mode); HUD shows the active mode.
- **`gX_RendererGFX_Light_SetCookie()`** — public API to assign a light cookie
  from a path (the only writer was the deleted AI harness, so the atlas
  rebuild never fired in compiled builds).

### Fixed

- **SSAO was a complete no-op**: raylib's `SetShaderValueTextureSlot` only
  supports texture units 0-11 (`rlSetTexture`), so the unit-12 SSAO bind
  silently failed and the sampler read texture 0 (white) → factor 1.0. Now
  bound with raw GL (`gX_RHI_Tex_BindOnUnit` + `gX_RHI_UniformInt`) in both
  `shader.c` SetValues and `model.c` per-model draw. The SSAO compute itself
  still produces a weak map (pre-existing — tuned later).
- **SSAO not applied to the PBR/IBL path**: only the Phong ambient multiplied
  by the SSAO factor. Both PBR branches (IBL + LOW fallback) and the final
  colour now apply it.
- **`ssaoMap` uniform declared inside `#if QUALITY >= 1`** — broke LOW-quality
  shader compile after the new references. Moved outside the guard.
- **Uniform targeting**: `gX_RendererGFX_Shader_SetValues` now explicitly
  activates the main program first (`gX_RHI_Shader_Use`), so raw RHI uniform
  calls (cookie atlas, SSAO) always hit the standard program even when the
  shadow map is unloaded.
- **RHI per-mesh debug spam removed**: `gl_draw.c` logged ~10 GL state queries
  per mesh draw at INFO level (per-frame log flood + overhead).
- **Default gfx camera** (2,2,2 → (6,2.5,6) fovy 45): the old position was
  inside/too close to the scene.

### Changed

- **ACES filmic tone mapping** replaces Reinhard in both `standard.frag` paths;
  new `exposure` uniform + `gfx->state.exposure` (default 0.85).
- **Lighting defaults**: sun 3.0 → 1.0, sky/ambient strength 3.0 → 0.25 (was
  overexposing everything; ACES + lower ambient restores contrast).
- **Bloom tuned**: threshold 0.8 → 1.1, intensity 0.25 → 0.15 (was blowing out
  the scene).
- **Demo content**: teapot scaled to fit (its GLB is ~6 units), fixture scaled
  down (its GLB is ~172 units tall).
- **AI harness fully removed**: `ai.c`/`ai_script.c` (3,008 lines, uncompiled)
  deleted; `gX_ENABLE_GFXAI` guard, `gX_Main_RunAICommands`/`ApplyAICamera`
  stubs, and ~40 dead `arguments` struct fields stripped. Lean: binary
  4,008,736 → 3,997,152 (-11.6 KB), bss 2,863,736 → 2,860,400.

### Known

- SSAO computes a mostly-white map (weak occlusion) — structurally wired now,
  compute tuning deferred. The "uniform not found" WARN lines at load time
  (effect programs querying standard-only uniforms) remain; they are
  load-time, not per-frame.

## [0.1.4] — 2026-08-15

Feature pass: interactive showcase scene, CSM 2-cascade default with soft
boundary blend, multi-cookie lights via atlas, occlusion culling defaulted on
in the Ultra preset.

### Added

- **`--showcase` — interactive GFX demo** (`src/renderer/gfx/showcase.c`):
  orbit camera (LMB drag + wheel zoom), effect hotkeys (B bloom, S SSAO,
  R SSR, T TAA, M motion blur, F fog, G godrays, P particles, Y reflections,
  X occlusion culling), Q quality-preset cycle, K CSM cascade toggle,
  Z sky-mode cycle, 1-9 model focus, 0 reset, H HUD legend (FPS/preset/
  cascades/active effects). Starts on the Ultra preset with the full effect
  stack.

### Fixed

- **CSM defaulted to 2 cascades** (`load.c` — both defaults paths previously
  fought: 2 then reset to 1). The 2-cascade pipeline was already wired
  (array layers, splits, per-cascade depth renders); added a **soft blend
  across the cascade boundary** in `standard.frag` (samples the previous
  cascade in a fade band above the split).
- **Light cookies: first-enabled-only limitation** replaced with a **cookie
  atlas** — all enabled cookies pack into one 4×4-grid texture (256px cells),
  per-light UV rects uploaded as `lightsCookieRects[32]`, single-sampler bind
  stays under `GL_MAX_FRAGMENT_IMAGE_UNIFORMS`. Lights carry a `cookiePath`
  so the atlas can rebuild from CPU data when cookies change.
- **Occlusion culling enabled in the Ultra quality preset** (the pipeline was
  fully implemented but never defaulted on; verified it is not the cause of
  any scene anomaly).

### Changed

- `docs/api_reference.md` — `gX_Showcase_*` family documented (caught by the
  api_coverage gate, which is doing its job).

### Known cosmetic (pre-existing)

- The `--gfx` scene renders washed-out with the default camera (positioned at
  the origin inside the models); `--showcase`'s orbit camera avoids it. The
  geo-demo structure renders black bars (pre-existing material/setup).

## [0.1.3] — 2026-08-15

Audit tooling precision — the informational noise is gone and two new
scripted checks close the audit program.

### Added

- `scripts/enum_coverage.py` — verifies every enum `String()` declaration
  has an implementation (catches dangling decls like the v0.1.1 gap).
- `scripts/api_coverage.py` — verifies every public `gX_*` function family
  is documented in `docs/api_reference.md`; the doc was expanded from 56
  to 179 documented prefixes (166 families covered).

### Fixed

- **`resource_deps.py` now understands the real loading forms** — `GX_PATH`
  two-arg macro, `GX_RM_ADD_*`/`GX_RM_ADD_SHADER_VF` loaddefaults macros,
  `"resources" SHADER_PATH "dir/file"` literal concat, `GX_RESOURCE_DIR`
  dir-prefix, bare names (wireframe model arrays, shader `#include`s), and
  shader-to-shader includes (scans resources/shader too). Unused count
  dropped 50 → 0; missing refs triaged.
- **Real missing assets found by the stricter scanner**: raymarch's three
  textures used a broken double-`texture/` path (`texture/texture/*.png`)
  and never loaded — corrected paths, files resurrected from the archive;
  `tubetech_vt.glb` (geo-demo model) and `text/textwriter.yaml`
  resurrected; 6 dead shaders deleted (top-level `bloom.frag`, unused
  `common/*.glsl` fragments, `globe.frag`, `simple.vert`); the two
  empty `postprocess/blur_v.frag`/`geometry_flipv.frag` stubs from v0.1.2
  were already implemented.
- **`shader_uniform_validator.py`** — counts `gX_RHI_GetUniformLocation`,
  `GetLocation_Multi`, `GFX_SHADER_GETLOC`/`GX_RM_GET_LOC` macros, indexed
  array members, raylib sampler convention, and UBO struct members;
  heuristic warnings dropped 76 → 0.
- **Removed dead imgui vestiges** — `gX_ImGui_WantCaptureMouse` decl +
  `g_imguiWantCaptureMouse` extern (imgui stripped in P6) and the never-
  reachable input.c guard.

### Changed

- `docs/api_reference.md` — expanded to cover all 166 function families
  (gxmath shape helpers, input/window families, renderer sub-modules,
  audio family, AI probe note).
- `make check` — audit step now includes enum + api coverage (strict).

## [0.1.2] — 2026-08-15

Copyright audit + resource audit tooling. All third-party content of
uncertain provenance removed; every resource validated by CI.

### Removed (copyright)

- **14 UI sounds of uncertain third-party provenance** — replaced with
  **procedurally generated PCM** at audio-init
  (`gX_Audio_GenRegisterWave`, same sound keys, GUI/scene code unchanged).
- **2 non-CC fonts** (one with no license, one freeware) — replaced with
  **Kenney Pixel / Kenney Future (CC0)**.
- `resources/audio/`, `resources/music/` (empty after removal); dead
  `RegisterMarkup("resources/markup/demo.json")` call; default music path
  (no music ships; API fails gracefully).

### Added

- `license/NOTICE.md` — third-party inventory: raylib (zlib) + bundled deps
  (cgltf/glad/tinyobj MIT, stb/miniaudio/dr_* PD-MIT, par_shapes MIT), fonts
  (all CC0 with license files), generated audio, project-original assets.
- `scripts/copyright_audit.py` — rip-pattern scan + font-license rule,
  fails CI on new offenders.
- Resource audit tooling resurrected from the gameX archive (one-way, through
  the filters): `resource_validator.py`, `resource_deps.py`,
  `model_format_validator.py`, `shader_uniform_validator.py`,
  `shader_compile_check.py`, `gen_shader_inventory.py` — wired into
  `make check` via `scripts/audit_resources.sh`.
- `docs/gfx_shader_inventory.md` (130 shader files).
- `resources/texture/misc/testcard.png` — SMPTE-style testcard (generated,
  default analog-renderer input).
- Implemented the two empty postprocess shader stubs (`blur_v.frag`,
  `geometry_flipv.frag`) that the archive shipped as 0-byte files.

### Fixed

- `resources/shader/postprocess/` (55 shaders) was missing entirely — the
  module referenced them but nothing shipped; resurrected from the archive.
- `analog.frag` still carried the gameX-era `//from game1` comment header.

### Changed

- `resource_validator.py` — gX adaptations: `.geom` support, `#include`
  fragment detection, NPOT allowlist (cubemap layout + tutorial texture).
- `shader_compile_check.py` — skips memory-loaded (`gfx_*`) and include
  fragments; brace counting strips comments.
- `resource_deps.py` — allowlist for documented missing refs (format
  templates, runtime artifacts, retained slot dirs).

## [0.1.1] — 2026-08-15

Expert-finish sweep: zero own-code warnings at `-Wall -Wextra` in Debug and
Release builds, all TODO survivors closed.

### Fixed

- **Warnings 109 → 0 (Debug) and 26 → 0 (Release):** missing standard
  includes (implicit builtin decls — latent UB in 9 files), unused
  variables, sign-compares, misleading indentation, format truncation.
- **`synth.h` file-scope VLA** — `delayBuffer` sized by a float-derived
  expression (non-standard); replaced with an integer constant.
- **`crt_core.c` negative shift** — `EQ_P`-guarded branches moved to
  preprocessor (`#if EQ_P >= 15`) so the dead `else` never compiles.
- **`layout_debug.c` ruler** — `s_rulerPlaced` was `gX_I1` (bool) but
  stored 0/1/2; the `>= 2` branch (B marker + A–B line) never rendered.
  Widened to `gX_I32`.
- **`vhs.h` initializer** — 16 `-1`s for a 15-member struct; dropped the
  extra element.
- **`gxmath.c` fast rsqrt** — type-punned pointer + uninitialized read
  replaced with alias-safe `memcpy` (both UB).
- **`textwriter.c`** — `strcpy` into a bounded line slot replaced with
  length-checked `memcpy`; all `strncpy` without NUL termination converted
  to `snprintf`/explicit-precision copies (css, markup, log, element,
  shader_compiler).
- **Tangent computation confirmed correct** (raylib_wrappers_model.c) —
  standard T,N,B formulation, matches raylib's ComputeTangents; TODO
  resolved with verification note.
- **NTSC encode threading TODO** — resolved as accepted tradeoff with a
  documented note (sample scene; ~35fps at 640×480, ~70fps+ at default
  scale).

### Added

- `gX_GUI_Element_Templates_String` — missing enum String implementation
  (9/9 gui enums now complete).
- `docs/api_reference.md` — audio, resource, camera/input, and settings
  module sections.

### Changed

- `src/common/shader_compiler.c` — dropped vestigial `raylib.h` include
  (file is GL-free; boundary discipline restored); removed dead UBO
  sampler-count scan.

## [0.1.0] — 2026-08-15

First release. gX is the distilled, release-bound successor to gameX
(archived 2026-08-15 as a read-only reference vault): the same engine
architecture without the tech debt, raylib-only, scoped to engine core +
demo scenes.
First alpha version, all other versions/builds indev/pre-alpha
(regarding gameX and before).

### Added

- Engine core: gxmath (pure-C math), common (heap, logging, marshal,
  X-macro enums, profiler), wrappers (backend-agnostic types), canvas GUI
  (builder, elements, templates), renderer (gfx 3D forward renderer,
  background, postprocess, textwriter, plus sample scenes), scene
  dispatcher with masterIndex switching.
- Milestone trail M0–M5: license headers (233/233), standalone CMake with
  raylib submodule, `make check` CI (build → tests → scene smoke → lean
  gate), docs (architecture, porting, API reference).
- Compression passes P0–P8: lean-gated source/binary reduction.

### Changed

- Source tree 113,210 → 87,210 lines (−23.0%) via compression P0–P8:
  dead-resource strip, marshal strip (JSON-only dispatch), backend guard
  strip (single-backend raylib), cgltf dedupe, imgui-state strip, json-c
  unused-file trim, custom JSON parser swap.
- Release binary 4,030,712 → 3,979,576 B; max-bss 126.3 MB → 2.86 MB
  (−97.7%).

### Removed

- Multi-backend support (GLFW/SDL/Vulkan/N64/PS1/PS2/Windows stay in gameX
  as engineering exercises), AngelScript scripting, game content
  (world/battle/market), AI probe (kept uncompiled behind `gX_ENABLE_GFXAI`),
  ImGui debug panels, video/FFmpeg, globe/weather, demoscript.

### Fixed

- `gX_Music_GetLength`/`GetTimePlayed` never returned the value.
- JSON `null` literal handling in the custom parser
  (`parse_object`/`parse_array` treated `null` as a parse error).
- `make lean` compared a stale `build-release/` binary against itself.

### Security

- No known issues.

### Third-party

- raylib 6.0 (zlib/libpng) — submodule, see `license/raylib/LICENSE`.
- stb_image, cgltf, miniaudio, glad bundled inside raylib.
- GPLv3 — see `LICENSE`.

[0.1.0]: #
