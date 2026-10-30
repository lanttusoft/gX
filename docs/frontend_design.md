# gX v0.2 Front-End Design Specification

> **Historical note (2026-10-28):** the title-screen pyramid hero motif and
teapot centerpiece were removed. The current title uses the author logo and an
opaque gradient with no 3D centerpiece.

Source: user hand-drawings at gameX `/drawings/` (frontend.png,
frontend_iteration1.png, gui.png, gui2.png). Analyzed 2026-08-24.

## Design language: FLAT BLUEPRINT LINE-ART

- Thin uniform strokes (1-2 device px), light backgrounds OR dark ink-on-dark
- Corner radius SMALL and uniform (2-5 px @1080p) — not pill-like
- NO drop shadows, NO glow — panels and text stay flat; buttons may use a
  restrained vertical value gradient to identify controls and support label contrast
- Procedural lattice/crosshatch tile texture as canvas background (low alpha)
- Inverted triangle/pyramid hero motif behind title menus (outline or low-alpha)
- Accent-driven single-hue theming: green=structure, violet=CAD grid,
  blue=tooling, magenta=decoration. One accent uniform swaps the theme.

## Front-end menu system (frontend.png + iteration1)

- Entry: `[g_ui_template_style.c]`
- Business rule: fresh install skips title → straight to game
- Main menu, three density variants of one screen:
  - full ("iteration1", call: all bars): continue / new game / load game /
    settings / demos / extras; footer: version badge, the input hint
    (arrows/enter/esc) and CREDITS (the dead info/demo/quit links were
    removed)
  - reduced (only play…): new game / load game / quit
  - demos-only: demos / quit
- Sub-screens: load-save grid (save1 stats / save2 / save3, click-icon-to-
  load, circular pager arrows, back); version info bottom-left
- Hero: title text at apex of inverted triangle; menu items inside triangle
  body; bottom-right 5 icon-button cluster (+ nav)

## GUI editor (gui2.png — "big editor pls")

- Top menu bar: File · edit · show … help … transform
- Left sidebar "selector": category stack (label / btn / lst / txt)
- Center: preview area ("stuff happening")
- Right panel "transform": value peek (scale 0.73, rotate 0.168, …)
- Bottom: manipulate row — slider (track + fill + thumb) wired to values
  ("rotate->vel 0.123"), plus multi-purpose control
- This is the v0.2 editor target; it dogfoods the widget toolkit

## SDF shader requirements (guisurface.frag v2)

1. sdRoundBox fill + 1px AA border band via abs(d) + fwidth — stroke color
   uniform separate from fill
2. Small radius default (theme cornerRadius 2-5)
3. NO shadow/glow terms; button-only value gradients are allowed to distinguish
   controls from text labels without adding effects to panels or backgrounds
4. Optional lattice mode: tile UV → square cell + diagonal X lines, low alpha
5. Triangle SDF variant for hero motif (separate element type or mode flag)
6. Uniform plumbing: col(fill), borderColor, accentColor, latticeColor,
   box, res, uniforms[0]=(radius, borderWidth, latticeAlpha, mode), plus
   gradientTop, gradientBottom, gradientEnabled for button surfaces

## Z-order: lattice bg → hero graphic → panels → buttons → icons → text

## Theme.json additions: inkColor, accentColor, latticeColor, latticeAlpha

Effects stay opt-in per theme: this spec's flat default ships with zeroed
glass tokens, cursor-light strength 0 and depth mode 0; `theme_aero.json`
demonstrates the Aero direction (glass pause panel, metallic depth, cursor
light) without changing the default look.

## Static inventory tooling (2026-10-22)

Four report tools read the current tree without a display, engine launch or
asset change. They cover GUI templates, UI-producing scenes, the teletext
renderer and `resources/style/*.css`; test files are skipped. Outputs default
under `/tmp/opencode`, and `make frontend-static-check` runs all four.

- `scripts/color_inventory.py --json [PATH]` — distinct raw colours, exact
  named-colour/theme-token overlap, orphan colours, and font references per
  screen. Theme tokens come from every `resources/style/theme.json*.json`; named
  colours from `src/common/macros.h`. An orphan is a colour with no exact
  RGBA match, not automatically a defect.
- `scripts/style_lint.py [--json PATH] [--fail]` — reports raw
  `(gX_Color){...}` and CSS literals outside the token policy. A built-in
  allowlist covers deliberate palettes (GuiTester, material/lighting labs,
  scene art, the CSS cascade demo); `--fail` is opt-in and fails only on
  non-allowlisted literals. The default invocation always exits 0.
- `scripts/font_inventory.py --json [PATH]` — project font files, explicit
  `GX_PATH("font", ...)` / `gX_Font_Load*` references (or `<default>`), and
  `fontSize` assignment uses per screen; theme size tokens are listed
  separately.
- `scripts/font_preview_sheet.py --out PATH` — one labeled PNG with every
  `resources/font` face and a sample line; characters matching the font's
  replacement glyph are marked red `[?]` and listed in the JSON metadata
  (`<out>.meta.json` by default). The missing-glyph check is deliberately
  conservative because PIL exposes no shaping/glyph-coverage API.

Current snapshot (2026-10-22, not defect claims): 240 raw colour
occurrences, 151 distinct, 142 orphans across 13 screens; style lint reports
214 raw literals (148 allowlisted, 66 findings); 2 project fonts, both
referenced, 74 `fontSize` uses across 12 screens; the preview sheet renders
both fonts, with 4 missing-glyph markers (the arrow set in
`KenneyMiniSquareMono.ttf`). The counts drift with the UI and are not gated.
