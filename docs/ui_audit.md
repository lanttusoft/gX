# gX — UI audit (layout, discoverability, ease of use)

<!-- //opencode: first pass 2026-10-14 — mechanical wiring gate + captures at
     1024x768/1600x900; per-tool notes below are from the vision review. -->
Status: **first pass landed**. The mechanical part is a gate; the review below
is the judgment layer on top of it.

## What is audited, and how

- **Mechanical wiring** (`make ui-audit`, full matrix): every tool must be
  reachable through all of its declared open paths — CLI flag, `GX_TOOLS_OPEN`
  value, EXTRAS card (where it has one), runtime flag/handler, and an
  `api_reference` mention. Panel conventions are checked too: single-window
  editors use the `gX Tools - <Name>` title, a first-use window size, and a
  status/help line. Tool *kinds* are explicit (window / canvas gallery / dock
  suite / module panel), so the checker fails on real gaps and advises on
  deliberate style differences.
- **Captures** (`python3 scripts/ui_audit.py --capture <dir>`): launches all
  thirteen tools headlessly at 1024×768 (the lean floor) and 1600×900.
- **Vision review**: the per-tool notes below were written by looking at
  those captures.

## Cross-cutting findings

- **One convention, three classes.** Single-window editors all share the
  `gX Tools - <Name>` title, a first-use size and a status line; the two dock
  suites (GFX editor, GUI editor) keep their own coherent prefixes (`GFX …`,
  `Editor …`) because they are multi-window; the two module panels (script
  debugger, rail editor) draw from `gx_shell`. `make ui-audit` pins the first
  class and records the other two as advisories.
- **Open paths are uniform.** Every tool has a CLI flag, and all but the
  ShaderLab canvas have a `GX_TOOLS_OPEN` value; every tool with a runtime
  window has an EXTRAS card (twelve cards — the grid is full). The script
  debugger is deliberately CLI/`GX_TOOLS_OPEN`-only: it needs the `gx_shell`
  module, and a card on a module-less build could only show a hint.
- **Preview routing.** Editors that need a stage route to it in the opener
  (textwriter → its scene, physics → the sandbox, background → the background
  scene, sky → the sky scene); the wavetable and rail editors are
  scene-agnostic and open over whatever is up; the GFX editor routes from the
  title. Nothing opens over a scene it cannot preview.
- **Empty states.** Fixture lists show an explicit empty message
  (`(no … found)`), status lines always report the last action, and guarded
  saves report refusals with the path. The review found no silent failures.
- **First-use sizes + scrolling.** ImGui remembers a user's resize; the
  captures confirm every panel fits at the lean floor. The two panels whose
  content exceeded the default height now fold it the same way: the
  background editor's polisher list is a collapsing section with its own
  scroll child (reactions stay visible), and the physics editor's tuning
  lives in one scrolling region with the parameter-heavy groups
  (em/thermal/fluid) folded, so world/content plus the presets and status
  footer stay on screen.

## Per-tool notes (captures, 1024×768 / 1600×900)

| Tool | Layout at the lean floor | Notes |
|---|---|---|
| GUI editor (`--tools`) | three-column dock (`Editor - …`) + status bar + first-run tour popup | the tour popup is good onboarding; status bar reports canvas/element/selection |
| Theme editor | `gX Tools - Theme Editor`, fits | token tree + search + compare presets + save path |
| Theme lab / Materials lab / Shader lab | canvas galleries (no window) | readable at 1024; each pairs with an editor or stands alone |
| GFX editor | three-column dock (`GFX …`) + menu bar + shortcut footer | the reference surface; routes to the GFX scene |
| Script debugger | `Scripting - Debug` (module), fits | CLI/env-only (module-gated); console overlay draws on top |
| Wavetable editor | `gX Tools - Wavetable Editor`, fits | slot/generate/draw/preview; fixture list; opens over any scene |
| Textwriter editor | `gX Tools - Textwriter Editor`, fits | line list scrolls inside the panel; tag palette compact |
| Physics editor | `gX Tools - Physics Editor`, fits after the fix | tuning scrolls in one region; em/thermal/fluid folded; world/content + presets + status visible at the default size |
| Camera rail editor | `gX Tools - Camera Rail Editor`, fits | title aligned to the convention in this pass; grab/play/presets |
| Background editor | `gX Tools - Background Editor`, fits after the fix | polisher is a collapsing section with a scroll child; reactions + status visible |
| Sky editor | `gX Tools - Sky Editor`, fits | ephemeris clock + look + presets all visible at 1024×768 |

## Fixes landed in this pass

- Camera rail editor title `gX Shell - …` → `gX Tools - …` (module panel now
  matches the tool convention).
- Background editor: the Background-polisher list became a collapsing section
  with a scrolling child, so the reactions row and status line stay on screen
  at the lean floor.
- Physics editor: the tuning sections moved into one scrolling region with
  the parameter-heavy groups (em/magnets, thermal, fluid) folded into
  collapsing headers, so world/content and the presets + status footer stay
  on screen at the default size — the last open audit follow-up.
- `docs/api_reference.md`: the tools bullet now spells the opener family and
  the full CLI/`GX_TOOLS_OPEN` sets (the checker reads them; readers stop
  guessing).

## Follow-ups (not blocking)

- A **13th tool** would need a grid restructure (two columns × six rows is
  full); the checker will not catch that by itself — add a layout test when a
  new card lands.
- The dock suites could adopt a shared title convention if they ever unify
  into one dock host; today their prefixes are deliberate.
- Card hover tooltips were dropped early because they collided in the tight
  stack; if the grid restructures, subtitles/tooltips become cheap again.
