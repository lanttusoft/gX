# Decal bakes

PNG output of the Decals panel's **Bake** tool (tooling scope, slice 4): the
scene's decals are projected top-down onto the XZ plane.

- `<name>_albedo.png` — the bake color with the decal coverage in the alpha
  channel (texture-mapped decals bake their footprint solid in v1).
- `<name>_orm.png` — R ambient occlusion (1.0), G bake roughness, B bake
  metalness inside coverage; full rough / no metal outside.

The folder ships empty; files written here are authored content and can be
adjusted or deleted freely. `GX_GFX_DECAL_BAKE_CHECK` drives a bake into a
temporary folder for `make gfx-decal-bake-check`.
