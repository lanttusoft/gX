# THEMLAB — live design preview

`--themlab` — a canvas-GUI design preview for deciding the frontend look.
Every widget on screen is drawn from the LIVE style tokens (`gui.style.*`),
so token edits re-skin the gallery in the same frame.

## Run

```bash
./build/gX_build --themlab                                   # preview only
./build/gX_build --themlab --theme-editor resources/style/theme.json   # + token editor
GX_THEME_EDIT="blueprint.accentColor=255,100,50,255" ./build/gX_build --themlab --theme-editor resources/style/theme.json --ai-screenshot-final  # headless live-edit proof
```

## Layout (canvas 2, over the title scene)

- **Header** — title + pairing hint.
- **Left: widget gallery** — Buttons (base/play/extra/back/quit), Inputs
  (slider/checkbox/toggle/radio/dropdown/stepper/progress/textinput),
  Lists & tabs (scrollable list, tab panel). All token-driven.
- **Right: blueprint preview** — hero band + mini title menu
  (CONTINUE/NEW GAME/LOAD GAME + wordmark) + a `blueprint` toggle that
  re-skins the mockup between the current `title2` palette and the
  `blueprint` ink/accent palette (the A/B decision aid) + token readout
  + a11y surface chips.

## Blueprint token group (`gui.style.blueprint`)

`inkColor`, `accentColor`, `latticeColor`, `latticeAlpha`, `latticeCell`.
Editable in the theme editor under "blueprint (line-art preview)".
Saved to `theme.json` like any token. The right preview panel uses these
tokens in the optional `guisurface.frag` lattice mode.

## Known limits

- The gallery remains rect/text-first; the shader lattice is intentionally
  limited to the blueprint preview panel so widget surfaces stay flat and
  predictable.
- Triangle hero geometry is still represented by stacked bands in the title
  template; it is a separate visual experiment from the panel lattice.
- The `blueprint` toggle is preview-local state (not a token).
- Widget state resets per build; use the theme editor Save for persistence.

## Files

- `src/gui/templates/themlab.c` — the template (+ `ResetState`)
- `src/gui/gui.h` / `src/gui/style.c` / `src/gui/gui.c` — blueprint group
- `resources/style/theme.json` — blueprint group
- `src/tools/theme/theme_editor.cpp` — blueprint panel
- `src/main_parse.c` (`--themlab`), `src/main.h` (`arguments.themlab`),
  `src/tools/tools.c` (`gX_Tools_ThemLab_Init`), `src/main_load.c` (hook)
- `src/gui/enum.h` (`THEMLAB`), `template_registry.c` (register),
  `CMakeLists.txt` (source)
