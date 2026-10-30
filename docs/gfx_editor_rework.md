# GFX editor rework

<!-- //ai: isolated editor-rework plan; branch editor/rework-2026-10-28. -->

Status: Phases 1–5 complete (2026-10-30) on `release/v0.19`. Internal
re-review meets the 8.0 release target in all three lenses. This branch is isolated from the `v0.18.1` public-beta
release worktree and is not release-blocking; the beta ships the editor as a
technical preview.

The current public beta ships the GFX editor as a **technical preview**. This
rework is the path from “dense internal tool” to the release-grade primary
surface. It does not add new renderer features or a modelling/painting app.

## Baseline findings

Three independent reviews scored the current editor:

- workflow/UX: 5/10
- product/credibility: 5/10
- accessibility: 4/10

Consensus blockers:

1. Left pane mixes scene tree, renderer settings, creation tools, models,
   lights and cameras; labels/tabs clip.
2. Inspector is crowded; selected-object properties compete with global
   presets/history/snapshots.
3. Editor uses default blue-gray ImGui styling while the title is purple/amber.
4. Default scene reads as a primitive test scene, not a renderer showcase.
5. Small, low-contrast labels and no clear keyboard-focus state.

## Non-goals

- New renderer or physics features.
- Asset creation/modelling/painting.
- A second UI framework or a web frontend.
- Changing scene/preset schemas without a migration note.

## Phase 1 — hierarchy and layout

- Left pane: separate **Scene** (objects/groups), **Create** (models, text,
  primitives, lights, cameras) and **Renderer** settings; expandable sections.
- Right pane: selected-object inspector first; global presets/history/
  snapshots in distinct collapsible groups or a secondary tab.
- Stop label/tab clipping at 1600x900 and 1024x768; make all tabs reachable.
- Keep the central 3D viewport dominant.

Acceptance: no clipped labels/tabs in the standard captures; selection of
models/lights/cameras is explicit; creation actions are not mixed into the
object tree.

Phase 1 status (2026-10-28):

- Left pane is split into **Scene**, **Create**, **Renderer** and **Document**
  collapsible groups; the raymarch/postprocess panels moved under Renderer.
- The scene tree is selection-only; imports, typed paths, primitives, 3D text,
  lights, cameras and prefabs live in Create.
- The inspector is selection-first, with presets/history under a separate
  Document group; renderer labs remain in scrollable tabs.
- Captures at 1024x768, 1600x900 and 1920x1080 were reviewed from the
  rework branch.
- Remaining Phase 1 debt: the 1024x768 left pane still scrolls for lower
  sections; long preset path labels and some right-pane labels remain tight.

## Phase 2 — identity and interaction states

- Derive an ImGui palette from the title’s plum surfaces, lavender borders and
  amber focus.
- Shared idle/hover/selected/focus treatment; focus must not be color-only.
- Gradient-backed widget prototype: `src/tools/imgui/imgui_gx_widgets.h` adds
  `GradientButton`/`GradientAccent` through the ImGui draw list while keeping
  the standard button ID, hover, pressed and focus path. It is used for the
  Create and Save actions; extend it into token-driven primary/secondary/
  destructive variants without editing the vendored ImGui tree.
- Clarify preset save/load, free-camera exit, drag-plane choice and undo/
  snapshot actions with nearby labels or grouped controls.
- Keep the viewport help card, but reduce its competition with the scene.

Acceptance: editor and title look like one product; keyboard focus visible in
live captures; no hover-only critical action.

Phase 2 status (2026-10-28):

- A scoped plum/lavender/amber ImGui palette applies only while the default
  theme is active; high-contrast, color-vision and Aero themes keep their
  existing accessible palettes.
- Primary Create/Save actions use the gradient widget prototype; the
  Selection band carries a plum-to-amber accent.
- Focus is shape-plus-colour: focused gradient actions gain a thicker border,
  and the navigation highlight is amber.
- Remaining Phase 2 debt: tokenize the palette from the title theme, cover
  every hover/selected/disabled state, and add a non-colour focus marker to
  standard ImGui controls.

First gate pass (2026-10-28): workflow/UX **7.2 (B-)**, product/credibility
**6.6 (C)**, accessibility **6.1 (C-)**. Phase 3 (showcase scene) and Phase 4
(accessibility/scaling) closed those gaps before the final review.

## Phase 3 — showcase scene

- Replace the primitive test scene with a composed default: deliberate camera,
  model grouping, readable lighting, shadows/reflection evidence, no cropped
  placeholder objects.
- Keep load time and 4 GB constraints in mind.
- Provide a second minimal “test scene” for diagnostics, not the default.

Acceptance: a screenshot alone communicates what the renderer can do.

Phase 3 status (2026-10-28):

- Added `resources/tools/gfx_showcase.json` as the editor default. It composes
  a matte checker floor, reflective stage disc, pedestal, teapot, sphere,
  torus, icosahedron and cone, with warm/cool/rim lights and a deliberate
  camera.
- `--gfx-editor` loads the showcase only when no explicit
  `GX_GFX_PRESET_LOAD` is present. Audit helpers set
  `GX_EDITOR_DEFAULT_SCENE=0`, so gates keep the deterministic demo scene and
  their calibrated coordinates.
- `resources/tools/gfx_editor.json` remains the minimal diagnostic scene.
- Captures at 1024x768, 1600x900 and 1920x1080 are recorded under the release
  folder as `gfx-editor-phase3-*.png`.
- Remaining Phase 3 debt: verify shadow readability on the matte floor, and
  add a deliberate no-selection default state.

## Phase 4 — accessibility and scaling

- Minimum readable UI text at 1024x768 and 1920x1080; expose text/UI scale.
- High-contrast labels and disabled states.
- Responsive/collapsible side panels at narrower widths.
- Verify keyboard traversal and focus at 1600x900 and 1920x1080.

Acceptance: 27-inch 1920x1080 at 2 ft is usable with default scale; all
interactive labels meet the agreed minimum.

Phase 4 status (2026-10-30):

- Default tool UI scale is **125%** (13 px base -> ~16 px); the Accessibility
  menu exposes 100–175%, keyboard navigation and a 125% reset.
- Disabled controls use `DisabledAlpha` 0.75 and a brighter disabled text
  colour; keyboard navigation keeps the shared nav cursor visible.
- Side panels scale with text, cap at 28% (scene) / 38% (inspector) below
  1440 px, and the dock's left margin follows the scene panel.
- **View > Focus viewport** hides both panels and centers the stage; the
  `GX_EDITOR_FOCUS_VIEWPORT=1` capture hook exercises the same state.
- Preset path/Browse and the Create action row now fit at the capped widths.
- Captures: `gfx-editor-phase4-{1024,1600,1920,focus,keyboard}-2026-10-30.png`.
  The keyboard capture uses three Tab presses through `GX_AI_INPUT` and shows
  the amber focus ring on the Default preset; 150/175% captures are also
  reviewed.
- Remaining debt: peer-review lenses, verify 150/175% scale, and consider
  condensing the shortcut footer further at the narrowest width.

Phase 5 status (2026-10-30, re-review):

- Internal three-lens re-review after the accessibility polish:
  workflow/UX **8.0 (A-)**, product/credibility **8.0 (A-)**,
  accessibility **8.2 (A-)**.
- Merge gate (7.0) and release-line target (8.0) both pass; no high/medium
  open blocker.
- Residual low-severity debt: overlay layout at 1024 and tab-strip overflow at
  the narrow inspector.
- The editor is release-grade for the v0.19 **technical preview**.

## Peer-review gate

The reusable contract is [`peer_review_gate.md`](peer_review_gate.md). For this
rework the minimum merge score is **7.0/10 (B-) in workflow/UX,
product/credibility and accessibility**, with no high-severity blocker; the
target is **8.0/10 (A-)** in all three. Scores are evidence-based and kept
separate, not averaged; `scripts/peer_review_grade.py` maps raw scores to the
S/A–F bands and checks the gate.

## Phase 5 — verification and peer review

- Captures: 1024x768, 1600x900, 1920x1080; editor with no selection, model,
  light, camera, transform, preset and history states.
- Interaction: add/duplicate/remove model, select light, save/load preset,
  enter/exit free camera, undo/redo, snapshot/restore.
- Re-run the three peer-review lenses after Phases 1–4 using
  [`peer_review_gate.md`](peer_review_gate.md). Merge gate: every lens at
  least 7.0/10 (B-) and no high-severity blocker; target 8.0/10 (A-) for
  release-line merge.
- Do not declare it the main delivery until the scores and blockers improve.

## Release hygiene

- The v0.19 editor is a technical preview, not a claim that the editor is the
  release-grade primary surface.
- The v0.18.x tags and assets remain immutable.
- Capture evidence is kept with the release audit record; private author data
  and bundles stay out of the public tree.
