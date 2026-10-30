# Theme editor tutorial

Live design-token surgery: every color/number the GUI reads from
`gui.style.*`, editable with instant re-skin, saved back to a theme
JSON. Pair with **EXTRAS → THEME LAB** (widget gallery + blueprint
preview) to see what you're doing.

## Open it

- **EXTRAS → THEME EDITOR** (targets `theme.json`), or
- `gX --theme-editor resources/style/theme.json` (any scene behind), or
- `GX_TOOLS_OPEN=theme gX --title` (headless hook).
- Headless edits: `GX_THEME_EDIT="group.token=R,G,B,A" ...` applies
  before first frame (automation/screenshots).

Window X or Close dismisses. Edits mark `(edited)` in the title.

## Core loop (try this)

1. Filter `button` — every button token across groups.
2. Tweak `core.buttonBase` — all buttons re-skin **in the same frame**.
3. Hover any **group header** — it says what the group controls
   (title2 = title screen, blueprint = line-art preview, ...).
4. Press **Blueprint** (preset row) — whole UI flips to the blueprint
   theme. Press **Default** to flip back. Both are undoable.
5. **Ctrl+Z** undoes (Ctrl+Shift+Z / Ctrl+Y redoes). Undo is value
   snapshots; R (per token) / Reset group / Reset all restore file state.
6. **Save theme** writes the JSON. **Reload from file** discards edits.

## Groups (what lives where)

- `title2` hero band, wordmark, menu ramps, save slots.
- `blueprint` ink/accent ink, lattice grid (Theme Lab preview).
- `core` fonts, radius, borders, shadows, widget look.
- `shared` accents shared across screens (bars, highlights).
- `escape` pause menu. `battle` battle HUD.

## Dock mode

Like the GUI editor: widen to 16:9, theme panel slim-docks left, game
centered. `GX_TOOLS_CLOSE=<frame>` closes headless.

## Limits

- No structural ops (tokens are fixed by the style struct; groups grow
  in code, not here). Adding a token = code + theme.json + schema.
- Save path is a text field — check it before Save (it writes anywhere).
- Dirty tracking compares against file baseline; reloading re-baselines.
