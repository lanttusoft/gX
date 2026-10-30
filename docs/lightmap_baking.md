# Static lightmaps

gX has an opt-in static-lightmap consumption path and a small offline baker.
The normal forward renderer is unchanged unless a model explicitly enables a
lightmap.

The current contract is intentionally locked to **additive linear RGBA8**:
the baked RGB is added as irradiance over the material albedo. HDR maps and a
fully baked replacement-lighting mode are deferred so this first slice stays
compatible with existing GL33 assets and defaults.

## Asset contract

- The model must contain a second UV channel, `TEXCOORD_1`.
- The baked image is a linear RGBA8 PNG. RGB is an additive irradiance
  multiplier over the model's material albedo; alpha is ignored.
- The image is registered in the GFX preset's `textures` heap.
- A model enables it with `params.hasLightmap=true` and points
  `params.lightmapID` at that texture entry.
- `params.lightmapStrength`, `params.lightmapScale`, and `params.lightmapOffset`
  tune the contribution. Defaults are `1`, `[1,1]`, and `[0,0]`.

The term is additive rather than a replacement for dynamic lighting. For a
fully baked static object, disable the relevant dynamic diffuse/ambient terms
in the scene or use a dedicated preset; this keeps existing scenes and
presets bit-stable by default.

## Baking

```bash
python3 scripts/bake_lightmap.py \
  scene_with_uv2.glb resources/texture/baked/scene_lightmap.png \
  --size 512 --light-direction 0.4,0.8,0.3 --ambient 0.15 --intensity 0.8
```

The baker supports triangle GLBs, indexed or non-indexed primitives, node
transforms, interpolated vertex normals, directional direct light, hard
shadow rays, ambient fill, and chart-gutter dilation. For repeatable scenes,
put the lighting and bake settings in a manifest:

```bash
python3 scripts/bake_lightmap.py \
  scene_with_uv2.glb resources/texture/baked/scene_lightmap.png \
  --manifest resources/tools/lightmap_fixture_bake.json --strict-uv
```

Manifest lights use `type: "directional"` with a surface-to-light
`direction`, or `type: "point"` with a world-space `position`. Both accept
`color` and `intensity`; point lights additionally accept `range` (zero uses
deterministic inverse-square falloff). Manifest `size`, `ambient`, `bias`,
`dilate`, and `strict_uv` override the corresponding command-line settings.
`--strict-uv` fails on overlapping or degenerate charts and out-of-bounds UVs.
The baker reports these diagnostics even without strict mode: chart coverage,
overlap texels, gutter texels filled by dilation, and remaining gaps inside the
UV bounds. Unwrapped assets should be authored with a non-overlapping
`TEXCOORD_1` chart layout first.

## Preset wiring

Add the generated image to the preset texture heap, then use its zero-based
entry index in the model parameters:

```json
{
  "textures": [
    { "path": "resources/texture/baked/scene_lightmap.png" }
  ],
  "models": [
    {
      "path": "resources/model/scene_with_uv2.glb",
      "params": {
        "hasLightmap": true,
        "lightmapID": 0,
        "lightmapStrength": 1.0,
        "lightmapScale": [1.0, 1.0],
        "lightmapOffset": [0.0, 0.0]
      }
    }
  ]
}
```

The sampler uses texture unit 26 through the RHI because raylib's convenience
texture setter only covers units 0–11. The lightmap uniform is explicitly
disabled for depth/shadow draws and invalid or unloaded texture IDs.

## Shipped fixture

`resources/tools/gfx_lightmap.json` is an opt-in end-to-end fixture using
`resources/model/primitive/lightmap_fixture.glb` and its generated PNG. It is
not loaded by the normal showcase presets. To capture it manually:

```bash
DISPLAY=:1 GX_GFX_PRESET_LOAD=resources/tools/gfx_lightmap.json \
GX_SCREENSHOT_DIR=/tmp/gx-lightmap-on \
./build/gX_build --gfx --gfx-editor --ai-screenshot-after 60
```

For an A/B comparison, copy the preset, set `hasLightmap` to `false` and
`lightmapID` to `-1`, and capture to a separate directory. `make lightmap-check`
also verifies the fixture GLB, PNG dimensions, preset heap ID, and a fresh
secondary-UV bake, including the manifest's directional-plus-point lighting
and UV diagnostics.

The repeatable advisory version is:

```bash
make lightmap-visual-review
```

It writes on/off captures, `lightmap_ab.png`, and `lightmap_metrics.json` to
`/tmp/opencode/lightmap_visual_review`. The metric output is evidence for
human review, not an automated visual-quality grade.
