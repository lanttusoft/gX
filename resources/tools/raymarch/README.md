# Raymarch presets

JSON scene presets for the raymarch renderer (`--raymarch`), saved and loaded
from the GFX editor's **Raymarch** panel (in the Scene panel). The schema
covers state, parameters, fog, clouds, sun, terrain, water, flags, misc and
debug, plus the fixed objects/lights arrays and the `textures` section
(skybox + tex0..2 paths/filters).

Usage, authoring and the headless hooks are documented in `docs/raymarch.md`;
the built-in presets (including `primitives` and `cloudscape`) are available
from the panel without a file.

The folder ships empty; files written here are authored content and can be
adjusted or deleted freely.
