# gX — editors and tooling surfaces

<!-- //ai: plan — inventory + conventions + ordered slices (author-approved 2026-10-12). -->
Status: **technical-preview inventory** — the slice list below is author-ordered;
all slices have landed (the physics editor was author-built; the sky editor
closes the list). `tooling_scope.md` is the contract: gX builds engine-native
editing surfaces that drive and validate the engine, not authoring applications.
The GFX editor is the reference surface. Its isolated rework (Phases 1–5:
hierarchy/layout, identity, default showcase, accessibility/scaling and
re-review) is **complete on this line** for the technical preview; the
remaining notes are low-severity layout debt, not missing workflow phases.
See [`gfx_editor_rework.md`](gfx_editor_rework.md).

## What an editor is here

An editor is a small ImGui surface over engine state with a persisted data
path — the GFX editor is the reference: it edits the live renderer scene and
round-trips presets under `resources/tools/`.

| Surface | Source | Edits | Tier | Gate / gap |
|---|---|---|---|---|
| Global editor host | `src/tools/editor_host.cpp` | always-summonable menu bar (`gX/File/Edit/View/<Active Editor>/Window/Help`), editor registry (GUI, theme, sky, background, physics, wavetable, textwriter, rail, GFX; GFX scene/inspector/asset/footer panels summonable, renderer labs (Models/Sky/World/FX/Lights/Render/Water/Ray/Particles/Refl/Debug) as separate panels, per-suite panel and lab masks), workspace tabs, built-in and user-saved suites (`gx_editor_<suite>.ini`, `gx_editor_suites.txt`, `gx_editor_workspace.txt`), console/quit | Alpha/WIP | `editor-host`; per-panel draw, GFX migration and layout polish pending |
| GFX editor | `src/tools/gfx/gfx_editor.cpp` | renderer scene (models/materials/lights/camera/sky/weather/decals/water/particles/SSAO/SSR/quality) + blockout/texture/export/bake panels | Beta | full audit matrix; rework Phases 1–5 complete; F1/Help guide; `gfx-editor-layout` covers 1024/1600 and 100–175% scale |
| GUI editor | `src/tools/editor/editor.cpp` | markup templates on the GUIPLAYGROUND canvas | Alpha/WIP | `gui-editor` gate; canvas click/drag/resize, tree filter, undo/redo, first-run tour + F1 guide, shipped-markup guard, deferred `--tools` markup open and readable tree labels |
| Theme editor | `src/tools/theme/theme_editor.cpp` | GUI theme tokens (live) | Alpha/WIP | `ui-audit` open path; token-list surface, manual workflow |
| ThemLab / MaterialsLab / ShaderLab | GUI templates | design previews (no data path) | Alpha/WIP | preview only, no data path |
| Script debugger + console | `gx_shell` module panels | scripting shell (CLI/`GX_TOOLS_OPEN` only — module-gated, so no EXTRAS card) | Alpha/WIP | opt-in module build; CLI-only panel |
| Wavetable editor | `src/tools/synth/wavetable_editor.cpp` | synth wavetable slots + `.f32` fixtures | Alpha/WIP | `wavetable-editor`; no undo/redo, freehand drawing is one-way, no tutorial |
| Textwriter editor | `src/tools/textwriter/textwriter_editor.cpp` | textwriter lines/tags/choices with live scene preview | Alpha/WIP | `textwriter-editor`; no undo/redo, line-list editing, no tutorial |
| Physics editor | `src/tools/physics/physics_editor.cpp` | sandbox/applier tuning (world/content/EM/thermal/fluid) + JSON presets under `resources/tools/physics`; modal free camera (WASD + mouse look, Esc returns — the GFX editor's pattern; `GX_PHYSICS_FREECAM=1` capture hook) | Alpha/WIP | `physics-editor`; no undo, parameter surface, no guided workflow/tutorial |
| Camera rail editor | `src/tools/tour/rail_editor.cpp` (**gx_shell module**) | tour camera keyframes: grabs, t/pos/target/fov/bezier, play, JSON presets | Alpha/WIP | no shipped gate; module-side, no undo/tutorial |
| Background editor | `src/tools/background/background_editor.cpp` (+ shared `bg_panel.cpp`) | background palette/decomposer (shared body), preset files under `resources/tools/background`, the Background polisher entries, reaction pulses | Alpha/WIP | `background-editor`; tuning panel only, authoring loop incomplete, no undo |
| Sky editor | `src/tools/sky/sky_editor.cpp` | sky look parameters + the **ephemeris clock** (scrub/play), preset files, helpers overlay | Alpha/WIP | `sky-editor`; parameter panel, no undo |

### GFX editor guide (2026-11-08)

The reference GFX editor now has a step-by-step **F1/Help > Editor guide**
window covering the stage, selection/transform keys, panel layout, preset
save/load and camera/report routes. `GX_GFX_GUIDE=force` opens it for headless
coverage; `make gfx-editor-guide-check` pins the GFX-stage open and guide
trace. The completed Phase 1–5 rework is documented separately in
[`gfx_editor_rework.md`](gfx_editor_rework.md); this guide is an onboarding
layer.

### GFX editor narrow layout (2026-11-08)

The remaining low-severity Phase 1/4/5 debt is closed for the standard
captures. When the full scene header or selection row would not fit, they
shorten to **Pick models/lights** and **Frame** / **Free camera** with hover
tooltips instead of clipping. The preset path stacks **Browse** beneath
the field when the filename would be cut, so the 1024 inspector shows the
whole `resources/tools/gfx_showcase.json` path. At stage widths under 420 px
the shortcut footer keeps only the state line plus one short hint; at 150/175%
scale it yields the stage entirely.

`make gfx-editor-layout-check` proves the rows and the renderer-lab strip fit
at 1024x768 and 1600x900 for 100/125/150/175% scale (including all eleven
Expert labs) and that `resources/settings.json` is unchanged.
`GX_UI_SCALE=<1.0..1.75>` pins the scale for a headless run, and
`GX_GFX_LAYOUT_TRACE=1` emits the `GFX_LAYOUT` fit row. The left pane still
scrolls for its lower sections at the lean floor; that is expected, not
clipping.

The asset-browser grid keeps long model paths on one line with a trailing
ellipsis and a full-path hover tooltip, so they no longer clip at the cell's
right edge or wrap into the next row. `GX_GFX_ASSET_BROWSER=1` opens the grid
headlessly, and `gfx-editor-layout-check` pins every grid path to its cell
width and one line of text at 100/175%.

### GFX preset picker (2026-11-12)

The inspector's **Load preset** button now opens the shared picker instead of
confirming the path already in the field. **Reload current** keeps the
confirming reload path, and **Browse** opens the same picker. Select a row and
press **Enter**, double-click, or use **Open**; accepting stages the path and
opens the existing replace confirmation. Paths are one-line ellipsized with a
full-path tooltip, and the picker scales with the tool UI (including 175%).

The actions are registered commands — `gfx.preset.load`, `gfx.preset.reload`
and `gfx.preset.save` — so menus, buttons and headless probes dispatch the
same IDs. `GX_GFX_PRESET_PICKER=1` opens the picker headlessly;
`GX_EDITOR_PICKER_TRACE=1` emits its geometry and row fit. `gfx-editor-layout-check`
pins the window inside the display and every row inside its cell at 100/175%.

The preset is also the first document-backed surface (GX-I043): model/light
history edits mark it **modified**, Save/load clear the flag, and the replace
confirmation warns that unsaved changes will be lost. `GX_GFX_PRESET_TOUCH=1`
marks it dirty for headless captures; `gfx-editor-layout-check` verifies a
preset load clears the state. A full close guard is still pending because the
host owns tool close.

### GUI editor usability (2026-11-08)

The GUI editor's existing direct-manipulation workflow is now explicitly
covered instead of described as missing: click an element on the canvas to
select it, drag to move with the 8px snap/grid/guides, use the eight resize
handles, or nudge with the arrow keys. The left tree has a filter and
add/duplicate/remove; the right properties panel has plain-language tooltips;
Ctrl+Z/Ctrl+Y cover value and structural edits, Ctrl+D duplicates, Ctrl+S
saves, Esc deselects, and F1 opens the guide. First run also shows the
five-step tour.

### GUI editor tour scale (2026-11-09)

The first-run tour card scales with the tool UI at 100–175%; its Back/Next/
Skip row lays out only the buttons that exist, so the first step cannot push
Next off the card. `gui-editor-check` pins the card size and button right
edges at 100% and 175%. Dense title EXTRAS cards keep their maturity
description for pointer hover but do not draw a focus tooltip over the next
grid row. The Settings scale slider is labeled **Tool UI**.

### GUI editor canvas lock (2026-11-12)

The editor canvas starts **locked**: left-drag cannot select, move or resize
elements; the wheel zooms (0.5–4×) and middle-drag pans. The toolbar
**Unlock** toggle enables element gestures and changes to **Lock**; **Fit**
resets the editor-local view. The view is restored when the editor closes, so
the runtime canvas is never left zoomed. Wheel/pan gestures are confined to
the free viewport: scrolling the tree, Properties, host chrome or the report
form never moves the canvas. The toolbar wraps its action row at 100–175%
text scale, and the status strip grows with the scaled text so both stay
inside the left dock.

The editor state model is explicit in the gate: `closed -> open (locked) ->
unlocked -> locked -> close -> closed`, and a reopen starts locked again with
a restored runtime transform. `editor-system-check` pins the default lock,
unlock/lock toggle, zoom state, view reset, toolbar/status overlap at 100%
and 175%, report action-row fit, report open/close/reopen, host open/close,
Theme Lab Back and title return. The tool/lab Back path is also pinned to not
flash the whole frame to black; scene switches keep the global transition fade.

Saves honor the same harness guard as the other editors: a gate-launched run
with `GX_NO_SETTINGS_SAVE=1` refuses to write shipped markup. The headless hooks
(`GX_TOOLS_SELECT`, `GX_TOOLS_FILTER`, `GX_TOOLS_SOLO`, `GX_TOOLS_ADD`,
`GX_TOOLS_REMOVE`, `GX_TOOLS_UNDO`, `GX_TOOLS_REDO`, `GX_TOOLS_SAVE`,
`GX_TOUR=force`) make this testable without a human:

```bash
make gui-editor-check
make editor-system-check
make editor-input-check
```

`editor-system-check` is the cross-editor lifecycle gate: it verifies toolbar
activation for the theme/sky/background/textwriter editors, close/reopen for
every editor and lab, Shift+F10 report opening (with plain F10 still reserved
for the console), and that closing Theme Lab, Materials Lab, Shader Lab or the
theme editor returns to the title. It also pins toolbar/status overlap at
100%/175%, the theme panel below the host workspace tabs, the report form's
action row at 175%, the report Reset control, report/host/canvas state
transitions and the Theme Lab **Back to title** route and a no-black-flash check for that route.
It runs in the full audit matrix.

`editor-input-check` is the input-arbitration gate. It verifies that a wheel
or middle-drag over an ImGui panel does not zoom or pan the canvas, that the
same gestures do work in the free viewport, that a click on an ImGui form
cannot activate a title button underneath, and that arrow keys during text
entry do not move native GUI focus. ImGui capture is read from the previous
frame's `WantCaptureMouse`/`WantCaptureKeyboard`; the host and report probes
cover the first frame of an overlay. It runs in `check-fast` and the quick
tier. The floating theme editor starts below the host workspace tabs (including
175%) and caps its width so its preview and Back route remain visible; closing
the last runtime editor hides the host workspace strip.

### v0.42.0 dogfooding fixes (2026-11-08)

The published-beta dogfooding pass found three editor route defects, fixed here:
`--tools <markup>` now defers its first open until the markup canvas is built
and reapplies the template once if the clean playground default build races it
(the shipped demo shows `bg`, `hello`, `stripe` and `gradient` instead of a
blank canvas with `UNKNOWN (hidden)` rows), the element tree falls back to
authored text and the generic `element` type instead of bare `UNKNOWN`, and
`--theme-editor <path>` now loads ImGui before opening so its panel is visible
like the `GX_TOOLS_OPEN=theme` runtime route. `gui-editor-check` pins the
deferred open and named demo rows; `editor-host-check` pins the CLI theme
panel. The GFX guide and Y2K OSD/pause-layout fixes are recorded in the
[rework](gfx_editor_rework.md#narrow-layout-polish-2026-11-08) and
[console](console_240p.md#presets) docs.

The v0.42.2 follow-up removes the floating-tool dock gutter: sky, background,
wavetable, textwriter and physics keep the scene full-width and float their
panels, while the GUI/GFX/theme/module editors still dock. `--tools` with no
value now fails closed instead of consuming the next flag, and the GUI
playground switch no longer logs expected renderer/scene/camera failures.
`editor-host-check` pins the floating sky-editor route; `cli-check` pins the
missing-value error.

Tier separates maturity from coverage:

- **alpha/WIP** — a usable preview; the workflow may be incomplete, and gates
  may still exist (the gate column says what is covered).
- **beta** — workflow-complete for its stated use, gate-covered and documented.

The GFX editor is the only beta editor. The tier ships with the tool: the
EXTRAS cards carry the `gX_Tools_Status` description (hover/focus tooltip)
from `src/tools/tools.c`, with `GX_TOOL_TIER_ALPHA/BETA`.

## Conventions (follow these for every new editor)

1. **Placement**: default build, Release `-Os` cold tier (tool UIs favor size;
   see the cold-tier set in `CMakeLists.txt`). Nothing lands on hot paths. The
   vendored Dear ImGui runtime (`src/tools/imgui/*.cpp`) is in the same tier;
   its GX-I017 recovery measured 278,144 B payload in Release, while Debug
   code generation is unchanged.
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
