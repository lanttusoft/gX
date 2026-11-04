# UI maturity levels

Settings and editor surfaces have three presentation levels:

- **Basic** — console-style surface: the controls a player needs.
- **Advanced** — PC-style surface: the practical control set (default).
- **Expert** — every persisted value with its type, current value,
  recommended range/unit, and a live override.

The level is persisted as `settings.uiLevel` and selected in the native
Settings panel (top row) or under **Tools → Accessibility → UI level**.

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

## Gate

`--test-only` pins the manifest: at least the core scalar rows exist, every
offset is inside `gX_Settings`, levels are valid, and min <= recommended <=
max. The editor-host gate pins the menu open path.

## Follow-up

- Move the remaining settings rows/sections onto metadata-driven rendering
  and add a UI-level screenshot gate.
- Extend the same metadata to the GFX editor panels and the other editors.
