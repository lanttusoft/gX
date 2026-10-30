# gX — editors and tooling surfaces

<!-- //ai: plan — inventory + conventions + ordered slices (author-approved 2026-10-12). -->
Status: **technical-preview inventory** — the slice list below is author-ordered;
all slices have landed (the physics editor was author-built; the sky editor
closes the list). `tooling_scope.md` is the contract: gX builds engine-native
editing surfaces that drive and validate the engine, not authoring applications.
The GFX editor is the reference surface but is **work in progress and not yet
release-grade UI**: its panel hierarchy, identity, default showcase,
accessibility and scaling remain beta work. A dedicated rework is planned and
will land in future betas.

## What an editor is here

An editor is a small ImGui surface over engine state with a persisted data
path — the GFX editor is the reference: it edits the live renderer scene and
round-trips presets under `resources/tools/`.

| Surface | Source | Edits |
|---|---|---|
| GFX editor | `src/tools/gfx/gfx_editor.cpp` | renderer scene (models/materials/lights/camera/sky/weather/decals/water/particles/SSAO/SSR/quality) + blockout/texture/export/bake panels |
| GUI editor | `src/tools/editor/editor.cpp` | markup templates on the GUIPLAYGROUND canvas |
| Theme editor | `src/tools/theme/theme_editor.cpp` | GUI theme tokens (live) |
| ThemLab / MaterialsLab / ShaderLab | GUI templates | design previews (no data path) |
| Script debugger + console | `gx_shell` module panels | scripting shell (CLI/`GX_TOOLS_OPEN` only — module-gated, so no EXTRAS card) |
| Wavetable editor | `src/tools/synth/wavetable_editor.cpp` | synth wavetable slots + `.f32` fixtures |
| Textwriter editor | `src/tools/textwriter/textwriter_editor.cpp` | textwriter lines/tags/choices with live scene preview |
| Physics editor | `src/tools/physics/physics_editor.cpp` | sandbox/applier tuning (world/content/EM/thermal/fluid) + JSON presets under `resources/tools/physics`; modal free camera (WASD + mouse look, Esc returns — the GFX editor's pattern; `GX_PHYSICS_FREECAM=1` capture hook) |
| Camera rail editor | `src/tools/tour/rail_editor.cpp` (**gx_shell module**) | tour camera keyframes: grabs, t/pos/target/fov/bezier, play, JSON presets |
| Background editor | `src/tools/background/background_editor.cpp` (+ shared `bg_panel.cpp`) | background palette/decomposer (shared body), preset files under `resources/tools/background`, the Background polisher entries, reaction pulses |
| Sky editor | `src/tools/sky/sky_editor.cpp` | sky look parameters + the **ephemeris clock** (scrub/play), preset files, helpers overlay |

## Conventions (follow these for every new editor)

1. **Placement**: default build, Release `-Os` cold tier (tool UIs favor size;
   see the cold-tier set in `CMakeLists.txt`). Nothing lands on hot paths.
2. **Runtime**: one exclusive `s_runtime<Name>` flag in `src/tools/tools.c`;
   the opener `gX_Tools_Open<Name>_Runtime` loads ImGui, sets the flag and
   clears the other exclusive tools; `gX_Tools_Close_Runtime` resets every
   flag and calls the editor's shutdown hook; the panel draws from the
   exclusive chain in `src/tools/imgui/imgui_gx.cpp`.
3. **Open paths**: `--<name>-editor` (load time), `GX_TOOLS_OPEN=<name>`
   (automation/captures), and a title EXTRAS card (`titlegx.c` +
   `binding.c` handler). Update the unknown-value hint text when adding one.
4. **Data**: editor files live under `resources/...`; writes into shipped
   paths are refused while `GX_NO_SETTINGS_SAVE` is set (harness guard —
   `gX_RendererGFX_Preset_Save_New` and `gX_Synth_WT_SaveFile` are the
   precedents): gates must never rewrite what they verify.
5. **Gate**: engine-side data paths get unit tests in `--test-only`; the panel
   gets a `make <name>-check` open-path smoke registered in
   `scripts/audit_summary.py` (full tier).
6. **Docs**: this inventory + a tutorial once the editor has a real workflow
   (the theme editor precedent).

## Planned (ordered)

1. **Wavetable editor** *(landed 2026-10-12)* — synth wavetable authoring:
   slot select, generate/normalize, freehand waveform drawing, preview note,
   raw `.f32` load/save with the shipped-fixture guard. Gate:
   `make wavetable-editor-check` (both open paths) plus
   `syn_test_wavetable_save_round_trip` in `--test-only`.
2. **Textwriter editor** *(landed 2026-10-12)* — authors `resources/text/*.txt`
   through the live writer buffer: per-line editing with `>`-preview on the
   textwriter scene, a tag palette (colors/styles/ruby/font/jump/choice
   snippets), add/delete lines, a fixture list, and guarded save/load
   (`gX_TextWriter_SaveFileTXT`; shipped text paths are refused under
   `GX_NO_SETTINGS_SAVE`). Opened by `--textwriter-editor` (routes to the
   scene), `GX_TOOLS_OPEN=textwriter`, or its EXTRAS card. Gate:
   `make textwriter-editor-check` + the saver round-trip/guard tests in
   `--test-only`. EXTRAS is now a two-column grid (up to ten cards).
3. **Camera rail editor** *(landed 2026-10-14)* — tour camera keyframes:
   grabbable from the live camera, per-keyframe t/pos/target/fov/bezier
   editing, reorder/remove, play/stop, and JSON presets. **Placement decision
   (slice time): module-side** — the rail is shell-owned
   (`src/scripting/tour/tour_camera.cpp`), so the panel ships in `gx_shell`
   (`src/tools/tour/rail_editor.cpp`) under this file's module escape hatch:
   the default payload pays nothing, the exe owns only the open
   flag/card/CLI (`--rail-editor`, `GX_TOOLS_OPEN=rail`, EXTRAS card) and the
   module multiplexes its panels off the exe flags in the single tools-draw
   slot. Persistence (`Camera_Rail_Save/Load`, JSON under the tools tree)
   refuses shipped preset paths under `GX_NO_SETTINGS_SAVE`; a sample
   `orbit_sample.json` ships in the camera preset folder. Gate: the scripting
   gate (`make check-scripting`) pins the round trip, the guard and the
   module-drawn open path. EXTRAS now uses all ten grid slots.
4. **Background editor** *(landed 2026-10-14)* — the GFX editor's
   background panel was **extracted into a shared body**
   (`src/tools/background/bg_panel.cpp`, one control surface) that the GFX
   editor still embeds (its `GFX_BG_UI` marker is unchanged) and the new
   standalone editor draws directly. The standalone adds a preset workflow
   with a fixture list under the background preset folder (a sample
   `palette_showcase.json` ships), the **Background polisher** entries with
   per-entry reset, and **reaction pulses** (Pulse/Shake/Flash/Warp). Opened
   by `--background-editor` (routes to the background scene),
   `GX_TOOLS_OPEN=background`, or its EXTRAS card (eleven cards). Gate:
   `make background-editor-check` (both open paths with routing, the panel
   trace, the sample preset staying loadable) plus the existing
   `make gfx-background-check` for the shared GFX-hosted path.
5. **Sky/time-of-day + physics editors** — both landed. **Physics**
   *(landed 2026-10-13, author-built)*: `src/tools/physics/physics_editor.cpp`
   over the sandbox's tuning surface (`gX_ScenePhysics_Tuning` in
   `src/scene/physics.{h,c}`) — world (gravity/quality pin), content
   toggles, EM/magnets, thermal and fluid settings apply live; presets are
   JSON under `resources/tools/physics` (`GX_NO_SETTINGS_SAVE` refuses
   shipped paths; `GX_PHYSICS_PRESET=<json>` seeds a run). Opened by
   `--physics-editor`, `GX_TOOLS_OPEN=physics` or its EXTRAS card (routes
   to the sandbox). Gate: `make physics-editor-check` plus the tuning
   round-trip/live-apply/guard pins in `--test-only`. **Sky editor**
   *(landed 2026-10-14)*: the sky renderer's sun/moon/planet directions come
   from `gamedata.ephemeris`, so the editor drives the **ephemeris clock**
   (scrub/play at a speed, with a sun-elevation readout) plus the look
   parameters (density/hue/glow/contrast/altitude/turbidity/disks/stars/
   moon/helpers). New engine preset IO (`gX_RendererSky_Preset_Save/Load`,
   JSON; shipped paths refused under the guard) captures the look + the
   clock; a sample `sky_sample.json` ships. Opened by `--sky-editor` (routes
   to the sky scene), `GX_TOOLS_OPEN=sky` or its EXTRAS card (twelfth slot —
   the grid is full). Gate: `make sky-editor-check` + the preset round-trip/
   sample/guard pins in `--test-only`.
6. **Toolgun mode** *(landed 2026-10-15)* — a *second interaction mode for
   editors in general*, GMod-style: a crosshair at the screen centre, a
   blocky procedural viewmodel (no assets — a few primitives, a bob/sway and
   a recoil kick), primary/secondary fire act on whatever the crosshair ray
   hits, and the mouse wheel cycles tools. It rides the existing modal free
   camera — the mode *is* the free camera plus the gun, so `Esc` leaves both
   in one step — and lives in the tools framework (`src/tools/toolgun.{h,c}`,
   `gX_Tools_Toolgun_*`: open a tool table, step the input each frame, draw
   the overlay from the host's 2D pass), so every editor that adopts a free
   camera can adopt this with one call. First adopter: the physics sandbox
   (**Toolgun** button or `GX_TOOLS_TOOLGUN=1`), whose aimed actions are the
   `gX_ScenePhysics_Tool*`/`AimAtModel`/pin surface — grab (spring drag on
   the aim ray), remove, and the magneto-style extras: freeze (dynamic/
   kinematic), heat, impulse and pin (ball joint to the static frame; RMB
   releases, `R` releases all, `K` draws them). The sandbox's own mouse
   interaction stands down while the gun owns the buttons (its keys keep
   working). Gate: `make physics-editor-check` (a toolgun leg asserts the
   mode opens, hands the mouse input over and renders a capture) plus the
   headless toolgun/scene suites in `--test-only`. The GFX editor (author
   WIP) adopts the same mode after the physics path proves the surface.

## Placement policy

Editors ship inside the binary by default (identity: `tooling_scope.md`) and
pay the lean gate — a new editor lands cold-tiered with a reviewed bump when
it doesn't fit. The dynamic-module route (`docs/dynamic_modules.md`,
`gx_shell.so` precedent) is the escape hatch for authoring surfaces that
should *not* ship; decide per editor at slice time, not up front.
