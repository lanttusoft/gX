gX — a distilled game engine
=============================

v0.21.0 (beta)

gX is the release-bound distillation of gameX (the research engine): the same
engine architecture without the tech debt, raylib-only, scoped to engine core
+ demo scenes. gameX is archived (2026-08-15) as a read-only reference vault;
gX carries only what has passed the scope/distill/debt filters. Modules return
only through the feature-first porting rule in `docs/porting.md`.

Features
--------
- gxmath: pure-C math library (vectors, matrices, quaternions, calculus, SDF)
- common: heap, logging, marshal, X-macro enums, profiler, random
- wrappers: backend-agnostic types (Texture, Shader, Model, Font, ...)
- physics: rigid-body wrapper (ODE-backed, optional `GX_USE_ODE` with no-op
  stubs), shared fixed-timestep world module, `--physics` props + ragdoll
  sandbox (bodies spawn into the shared world and render through the GFX
  renderer, with sub-step-interpolated transforms, spring drag-and-throw,
  scene-exit park, a status HUD and a title DEMOS route), a 12-element
  table with density/molar mass/thermal/contact data, per-element looks
  and rolling resistance, and a magnetic susceptibility (iron, …), and an EM applier
  (softened Coulomb + uniform-B Lorentz, `M` toggles, `GX_PHYSICS_EM`/`_B`
  envs), plus a thermal model (impact heating, radiative cooling, blackbody
  glow via the renderer's emissive path, plus contact conduction and
  latent-heat melting/re-solidification; `H` heats, `GX_PHYSICS_TEMP` pre-heats)
  and contact-event spark bursts through the GPU particle emitters (the
  opt-in compute build renders them; charged impacts read as cyan arcs,
  hits leave scorch decals, `GX_PHYSICS_TUNE` pins the quality level), joint
  rigs exercising the constraint family (hanging chain, hinged door,
  motorized windmill, slider rail — all mouse-draggable,
  `GX_PHYSICS_JOINTS=0` skips), contact materials (per-element friction/
  restitution from the element table, steel rigs, tissue ragdolls, earth
  ground; `GX_PHYSICS_MATERIALS=0` keeps the defaults), sleeping/auto-disable
  (settled bodies sleep — the HUD counts them — and wake on interaction or
  impact), a walled water pool
  (Archimedean buoyancy and drag per element, floating buoys and dummies,
  splash FX; `P` toggles, `GX_PHYSICS_WATER=0` skips), magnetic dipoles
  (bar magnets align and snap — pair forces, mutual torques and the
  uniform-field `m × B`; `N` toggles, `GX_PHYSICS_MAGNETS=0` skips; iron
  test pieces snap in through the element susceptibility; the
  SI-exact laws are in the math-pass corpus and mirrored in the Lean
  `GxMath.Physics` contracts), a conveyor belt strip that carries bodies
  along +X (`GX_PHYSICS_BELT=0` skips), and a physics editor (live
  sandbox/applier tuning + JSON presets under `resources/tools/physics`;
  `--physics-editor`, `GX_TOOLS_OPEN=physics` or the EXTRAS card);
  ragdoll bones join the EM/thermal appliers (charged dummies splay, heated
  dummies glow; `K` toggles a collider/contact debug overlay)
- gui: canvas-based immediate-mode GUI (builder, elements, templates, design
  tokens with theme files) — title screen with transitions, rotating 3D
  centerpiece, hover-reactive selection; pause menu (blur + dim overlay,
  Resume/Settings/Back to Title); title Demos hub for Battle, GFX Showcase,
  Background, and Raymarch; live debug overlay, toast popups;
  word-wrap for text elements (real glyph metrics + UAX#14 CJK breaks),
  SDF text (crisp at scale via a distance-field UI font), a11y tokens
  (surface stack + On- pairs with WCAG 2.2 AA contrast assertions, motion
  tokens with reduced-motion support); user-facing controls are collected in
  docs/accessibility.md (Reduce Motion, high-contrast/color-safe themes,
  tool UI scale and terminal plain mode)
- renderer: gfx 3D forward renderer (PBR, GTAO, SSGI, volumetric fog,
  Forward+ clustering with compute tile culling — 64-light ceiling via
  SSBO light data + GPU-assembled tile mask grids, GX_COMPUTE=1 build),
  background (procedural presets), postprocess,
  textwriter, plus sample scenes (water, raymarch, analog, fractal, sky,
  synthvis, ...)
- gX Tools suite (--tools, --theme-editor): in-engine GUI editor + theme
  editor on vendored Dear ImGui — build templates and re-skin the title
  without leaving the running engine
- gfx editor (--gfx-editor, **work in progress / technical preview**): scene authoring on the live renderer — models and
  lights, authored cameras with render-to-image, per-material overrides with
  map browsing, 3D text labels, blockout primitives (GenMesh*), prefabs
  (radius-captured model + light bundles), decals, particle emitters, and
  browse-only guest asset roots; themes include a Frutiger Aero preset
  (resources/tools/aero.json)
- validation: unit tests plus scene smokes in `make check-fast`; the full
  release audit matrix, proof suite, and certification ritual are maintained
  outside this source beta
- release hygiene: license/provenance.json is the resource source of truth,
  and license/ATTRIBUTIONS.md is generated from it
- scene dispatcher with masterIndex switching; title is the default scene
- raylib backend only

Requirements
------------
- CMake >= 3.10
- A C99 (and C++ for regex + the tools suite) compiler
- raylib fetched as a git submodule (external/raylib)

Build
-----
    git clone <repo-url>
    ./build.sh
    ./build/gX_build            # run (windowed)
    ./build/gX_build --test-only  # run unit tests, exit immediately
    make check-fast             # public fast CI: build, tests, scene smoke

raylib source is vendored in `external/raylib` for this beta (upstream
6.0-58-gcf9f27db). The release helper vendors the same source into tarballs.

Common flags: `--title` (default scene), `--gfx`, `--gfx-editor`, `--background`,
`--battle`,
`--pp`, `--theme <file>` (aesthetic preset), `--tools <markup>` (GUI editor),
`--theme-editor <file>` (theme editor). See `./build/gX_build --help`.

Battle controls: Tab switches the directly controlled party member; the other
two companions autofight. Each actor has an independent ATB gauge; commands need
a full gauge. Arrows select commands/targets, Enter/Space confirms, WASD moves,
and Q/E turns in place. Movement is realtime; position and facing affect physical
damage and incoming risk.

Guest assets: `GX_ASSETS=/path` (or `--assets`, or the editor's Asset sources
block) registers browse-only external roots; nothing is copied into the repo
(see docs/guest_assets.md). A GL 4.3 context (compute, SSBO, GPU particles)
is opt-in: `GX_GL43_CONTEXT=1 ./build/gX_build --gfx-editor`; the editor's
GL level panel persists the preference.

License
-------
gX is licensed under GPLv3 (see LICENSE).

Third-party components:
- raylib (zlib/libpng) — external/raylib, see license/raylib/LICENSE
- Dear ImGui (MIT) — src/tools/imgui, see license/imgui/LICENSE
- rlImGui (zlib) — src/tools/imgui, see license/rlimgui/LICENSE
- Full list in license/NOTICE.md; generated attributions in
  license/ATTRIBUTIONS.md. Third-party content that may not ship is
  excluded from this beta.

AI assistance
-------------
AI tools assisted with code, tests, documentation and author-prompted assets.
They can make mistakes. See `docs/ai_disclosure.md` for the disclosure,
review limits and the as-is disclaimer.

Docs
----
The documentation index lives in `docs/README.md`.
Lean proof sources: https://github.com/lanttusoft/gx-proofs

Credits
-------
- lanttu / lanttusoft — artist and engine author
- opencode (AI) — contributions marked with `//opencode:` or `//ai:` in the source
