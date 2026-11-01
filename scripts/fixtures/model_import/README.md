# Model-import fixtures

Owned by the gX project, GPL-3.0-or-later.

- `quad.dae` — one textured quad (4 vertices, 2 triangles, one material).
- `quad.png` — 16×16 four-colour checker used by the fixture material.

`make model-import-check` converts the quad with assimp, verifies the texture
copy, and renders the result through the GFX editor. These files live outside
`resources/`, so they are test inputs rather than shipped assets.
