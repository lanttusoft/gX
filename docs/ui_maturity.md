# UI maturity levels

Settings and editor surfaces have three presentation levels:

- **Basic** — console-style surface: the controls a player needs.
- **Advanced** — PC-style surface: the practical control set (default).
- **Expert** — every persisted value with its type, current value,
  recommended range/unit, and a live override.

The level is persisted as `settings.uiLevel` and selected in the native
Settings panel (top row), under **Tools → Accessibility → UI level**, or in
the GFX inspector header. `GX_UI_LEVEL=basic|advanced|expert` (or `0..2`)
overrides the effective level for a headless run without rewriting the
persisted setting.

## Metadata

`src/settings.c` owns one settings manifest (`gX_Settings_FieldInfo`): key,
label, type (`bool`/`i32`/`u32`/`f32`), struct offset, level, min/max,
recommended value and unit. The Expert list renders directly from it in the
Accessibility menu (`Expert values`), so a new persisted scalar field needs
one metadata row to appear there.

Current native filtering:

- Basic keeps display, audio, theme, sensitivity/invert, reduce-motion and
  the actions.
- Advanced adds tool scale/navigation and camera FOV.
- Expert shows the full native panel; the typed value table lives in the
  Accessibility menu because the native GUI template is fixed-layout.

## GFX editor

`src/tools/gfx/gfx_editor.cpp` gates whole labs and the fine-tuning sections
with `gx_ui_section()` / `gx_ui_lab()`. A level change closes labs above the
active tier; the `GX_GFX_PANEL_LABS` hook is filtered by the same gate.

| Surface | Basic | Advanced | Expert |
| --- | --- | --- | --- |
| Labs | Models, Sky, World, FX, Lights, Render | + Water, Particles, Refl | + Ray, Debug |
| Scene renderer | viewport helpers, stats | + texture tools, profiler | + GL level, watches |
| Render lab | Features, Quality, Look, SSAO | + toggles, shader quality, kernel | + advanced shader tuning, SSGI, aux RT/layered shadows |
| Look | exposure, model, maps, culling, tone map | + detail, rim/env/cookies, clustering | — |
| Bloom | enable, threshold, knee, intensity | + shape controls | + auto exposure |
| Effects | fog | + volumes, godrays, lens flare | — |
| Lights | lights, shadows | + CSM | — |
| Models | model list, transforms, material | + per-material overrides | + map tuning |
| Raymarch | scene panel, render modes, textures | — | + advanced tracing |

## Audit override

`GX_UI_LEVEL` is read by `gX_Settings_UILevelEffective()`; every filtering
site uses that accessor instead of `settings.uiLevel` directly. With
`GX_UI_TRACE=1`, the GFX editor logs one `GFX_UI_LAB` / `GFX_UI_SECTION` row
per surface and level (`level=`, `name=`, `min=`, `visible=`).

## Gate

- `--test-only` pins the settings manifest: at least the core scalar rows
  exist, every offset is inside `gX_Settings`, levels are valid,
  min <= recommended <= max, and the level parser/gate accepts both names and
  numeric spellings.
- `editor-host-check` opens the GFX editor at all three levels with every lab
  forced and verifies every traced row against its declared minimum, the
  complete lab set, and the Basic/Advanced/Expert section matrix.
- The editor-host gate also pins the menu open path.

## Follow-up

- Move the remaining settings rows/sections onto metadata-driven rendering
  and add a UI-level screenshot gate.
- Apply the same lab/section table to the markup, sky and background
  editors; the shared helper is already in `src/settings.c`.
