# gX — UI title-scene audit (front-end flows)

> **Historical note (2026-10-28):** the teapot centerpiece and pyramid hero
> motif were removed from the title. This audit records earlier centerpiece
> work and is retained as evidence, not as a description of the current title.

<!-- //opencode: first pass 2026-10-14 — title + overlay-template captures at
     1024x768/1600x900; notes from the vision review; the title's own menu code
     (titlegx.c) is mid-flight in the author's WIP, so its findings are
     reported, not edited. -->
Status: **first pass complete** — the title, its sub-views (LOAD GAME /
DEMOS / EXTRAS / CREDITS) and the overlay templates are reviewed; the
mechanical layer is the existing GUI gate family (template matrix, text-fit
layout, interaction checks) plus the new `title-flow` row, and this doc is
the judgment layer on top. TITLEGX's own re-review waits for the author's
WIP.

## Scope

- The title itself (`--title`, the TITLEGX look) at the lean floor and
  1600x900.
- The title's own sub-views (LOAD GAME, DEMOS, EXTRAS, CREDITS) and the
  user-facing overlay templates over the title: ESC MENU (the title's ESC
  routes there), SETTINGS, MESSAGE (toast), DIALOG; the dev/demo templates
  are noted for completeness.
- Existing coverage this builds on: `gui-matrix` (18 templates render / SCENE
  blank, retried flakes now name their rows), `gui-layout-check` (text fit per
  template, shrink-guarded), `gui-interaction-check` (12 rows), plus
  `gui-theme-check`, `gui-depth-check`, `gui-glass-check`,
  `gui-pause-blur-check`, `gui-cursor-light-check`, `gui-effect-contrast-check`.

## Findings

### Fixed in this pass

- **EXTRAS grid: the background editor card was unreachable.** The card table
  gave it `col 1, row 5` — the sky editor's cell — so both labels stacked on
  one slot (the sub-view vision review caught the ghosted "BACKGROUND EDITOR"
  text behind "SKY EDITOR"). Moved to `col 0, row 5`, restoring the clean 2x6
  grid; the `title-flow` gate now runs the EXTRAS view with the element trace
  and fails when two tool cards share a cell.
- **Centerpiece depth (found with the shading).** The shaded teapot showed
  front and back faces at once: the centerpiece draw had depth testing and
  culling disabled — invisible with the old flat silhouette, exposed by the
  lambert light. The draw now clears the title depth buffer, tests/writes
  depth (LESS) and culls back faces, with the whole 3D state wrapped in a
  `gX_GL_State_Push/Pop` so the GUI canvases drawing after it keep their
  state. Capture-verified (solid front-facing teapot).
- **EXTRAS crashed the engine (release-critical).** The committed card table
  declared `tools[10]` but carries 12 entries while the loop iterates
  `ti < 12`: the two past-the-end cards read garbage label pointers and
  `gX_GUI_Builder_Text_Set` segfaulted the moment EXTRAS was opened (stack:
  `gX_GUI_Template_TitleGX` -> `gX_GUI_Builder_Text_Set`). Fixed by growing
  the array to `tools[12]`, and pinned by the new **`title-flow`** gate
  (`scripts/title_flow_check.py`, matrix row `title-flow`): it drives the
  menu like a user (LOAD GAME / DEMOS / EXTRAS / CREDITS / ESC), fails on any
  non-zero exit or an unchanged frame, and asserts the ESC back routes return
  to a bit-identical main menu (0.000 deltas — the fixed clock makes it
  exact). Verified: the gate FAILs on the pre-fix binary exactly at `extras`
  (exit 139) and PASSes on the fixed tree (open deltas 23.0-33.6).
- **SETTINGS: the theme radio stack collided with the action row.** Draw-truth
  rects at 1024x768: `( ) Color-safe` 603..632 against
  Defaults/Key Bindings/Apply/Save 631..674 — the last theme option's box
  touched the button row, and the "UI Theme" section header sat on the first
  radio. The block moved up one step (section 5.15 → 5.02, radios
  5.62/5.95/6.28 → 5.44/5.77/6.10): Color-safe now ends at 615 with a 16 px
  gap, verified in the trace and the capture. Gates: `gui-layout-check` 18/18,
  `gui-check` 18/18, `gui-interaction-check` 12/12.

### Reported for the author's in-flight work (not edited)

- **ESC MENU over the title lets the title's menu read through the pause
  panel.** The title routes ESC to the overlay menu (its own hint: "ESC
  MENU"), so this composition is a real flow. The pause panel's fill is
  `gui.style.escape.panelBg` = `{20,15,35,200}` (78% opaque) and the title's
  menu rows stay readable inside the panel (see the ESCAPEMENU capture).
  Options: raise the pause panel's alpha, or dim the title menu harder while
  an overlay is open. Both belong to the WIP files (`titlegx.c`, `style.c`).
- **TITLEGX look and detail review pending** — the title template is
  mid-flight; the captures here show the interim state, which already reads
  well at both sizes (logo block, the seven-item menu with the gold focus
  ring, the hint bar).

### Notes (deliberate, not findings)

- **MESSAGE is a toast** (fill `{10,10,20,200}` by design, ~78% opaque). The
  matrix forces it over the title menu with no live toast, so the empty body
  and the see-through rows in that capture are harness artifacts, not a
  front-end defect; real toasts appear over gameplay with content.
- **DEMO / DEMO2 / EXAMPLE / SDFT** are widget/feature demos; GUITESTER,
  DEBUGDISPLAY, VIEWPORTGRID and the lab templates are developer surfaces —
  out of the front-end scope (their text-fit rows still gate in
  `gui-layout-check`).
- The title's hint bar (`ARROWS SELECT`, `ENTER CONFIRM`, `ESC MENU`,
  `CREDITS`) is the discoverability anchor and is correct at both sizes.
- **Sub-view review (vision pass)**: LOAD GAME (three slots, dim `EMPTY`
  labels, focused back), DEMOS (five scene entries + BACK) and CREDITS
  (engine + tool credits, focused back) read clean at 1024x768 — the EXTRAS
  grid finding above came from this pass. Minor note: the DEMOS labels
  `BACKGROUND` / `RAYMARCH` are engine-internal terms; plain-language
  subtitles would help a newcomer (welcome, not required).

## Follow-ups

- Re-review TITLEGX and the ESC overlay composition once the author's WIP
  lands (the two reported items).
- The **front-end flow gate landed** as `scripts/title_flow_check.py`
  (matrix row `title-flow`): arrow navigation to LOAD GAME / DEMOS / EXTRAS,
  the CREDITS link, and the ESC overlay — each must open and leave the frame
  changed; ESC closes the overlay, LOAD GAME and EXTRAS back to a
  bit-identical menu frame; a non-zero exit fails the gate.
