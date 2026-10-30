# Postprocess presets

JSON chain presets for the postprocess renderer (`--pp`), saved and loaded
from the GFX editor's **Postprocess** panel. The schema covers the module
state, the ordered `shaders` chain (type, loops, blend/filter/wrap, and each
entry's `value`/`deltaAdd`/`deltaMul` uniforms), the `inject` slots (paths,
blend modes, coords/scale/rotate/color and the animated params) and the
`texture` settings (start blend/filter/wrap, render scale).

Usage and the headless hooks are documented in
`docs/resurrect_postprocess.md`; the built-in chain presets (test, blur,
strongblur, battle, battle2, vignette, pixelation) are available from the
panel without a file.

The folder ships empty; files written here are authored content and can be
adjusted or deleted freely.
