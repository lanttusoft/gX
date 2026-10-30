# Scene exports

OBJ output of the Scene panel's **Scene export**: every exported file has the
model's world transform baked into the vertices (and normals), so a mesh
imported elsewhere lands where it stood in the scene. Materials and lighting
stay in the preset; re-import through the GLB path for shading.

The folder ships empty; files written here are authored content and can be
adjusted or deleted freely. `GX_GFX_EXPORT_DIR` redirects output (used by the
headless `make gfx-export-check`).
