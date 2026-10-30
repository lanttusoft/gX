# gX — tooling scope

Decision (2026-09-27): gX builds **engine-native tooling surfaces**, not
authoring applications. The GFX editor exists to drive and validate the
engine; it is deliberately not on a path to becoming a modeller or a paint
program.

Dedicated subsystem editors and the conventions each one follows are indexed
in [`editors.md`](editors.md) (wavetable editor landed 2026-10-12; textwriter,
camera rail, background and sky/physics follow).

## In scope

1. **Interop over modeling** *(implemented, OBJ + MTL — `make gfx-export-check`)*.
   Export what the engine holds so dedicated tools
   can take over: scene/mesh export (OBJ through raylib's `ExportMesh` with
   model transforms baked, plus MTL material sidecars and copied override
   maps, and native GLB export with embedded override maps), while keeping
   GLB import first-class.
   The intended loop is *blockout in gX -> detail in Blender/Krita -> import
   back*.
2. **Blockout primitives** *(implemented, `make gfx-primitive-check`)*. An
   Add-primitive panel (cube, plane, sphere,
   cylinder, cone, torus with size/segment parameters) built from raylib's
   `GenMesh*` plus `gX_RendererGFX_Model_AddGeneratedMesh` — no asset files
   required. Pairs with the prefab system for scene composition.
3. **Texture utilities** *(implemented, `make gfx-texture-check`)*. A
   generator panel for project-owned textures:
   solid/checker/gradient/value noise, normal-from-height, and PBR channel
   packing (roughness/metalness/AO -> ORM), exported through `ExportImage`.
   These directly serve the AI-assisted asset pipeline by normalizing and
   packing generated maps.
4. **Scene-space painting** *(implemented, `make gfx-decal-bake-check`)*.
   Decals stake out "painting in the scene"; the bake step projects them
   top-down into albedo (bake color + coverage alpha) and ORM (AO / bake
   roughness / bake metalness) PNGs, closing the texturing loop without a
   brush engine.

## Out of scope

- Mesh topology editing (extrude/bevel/loop tools, UV unwrap).
- Layer-based raster paint (brush engine, layers, blend modes, large canvases).
- Anything that turns the editor into an application rather than an engine
  surface.

## Why

- **Identity**: gX is the release-bound distillation of gameX as an engine;
  authoring applications are a different product class with a different
  maintenance burden.
- **Lean gate**: the editor ships inside the binary and the Release payload is
  gated, so every tool surface must earn its bytes. The surfaces above are
  small because the mesh/image generators already exist in raylib and the
  wrappers.
- **Interop is cheaper and better**: Blender and Krita are decades ahead;
  gX's job is to get data in and out cleanly, not to duplicate them.

## Slices

1. ~~Add-primitive panel (blockout)~~ — done (`make gfx-primitive-check`).
2. ~~Texture generator panel (including channel packing)~~ — done
   (`make gfx-texture-check`).
3. ~~Scene/mesh export (OBJ first)~~ — done (`make gfx-export-check`).
4. ~~Decal bake (later, medium)~~ — done (`make gfx-decal-bake-check`).
