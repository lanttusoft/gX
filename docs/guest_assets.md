# gX — Guest Assets

Runtime registration of external resource directories ("guest roots") for
testing gX's gfx pipeline against foreign content — another game's models,
textures, and scenes. Designed for perf work: point gX at a real asset
pool, inject models into the showcase, and measure with the normal capture
tooling. Nothing is committed to the repo and no code references any
specific foreign project; the paths live only in the caller's environment.

## Quick start

```bash
# 1. Register a guest root (an external asset pool as the example)
export GX_ASSETS=<external-assets>/resources

# 2. Spawn guest models in the showcase (additive ring around the cluster)
GX_GUEST_MODEL=model/teapot.glb:6 ./build/gX_build --showcase

# 3. Heavy foreign content through the same chain
GX_GUEST_MODEL=model/guest/terrain.glb:4 ./build/gX_build --showcase
```

CLI equivalents (both forms always available): `--assets <dir>` (repeatable
up to 8 roots) and `--guest-model <subpath>[:count]`.

All existing perf tooling applies unchanged: `--ai-screenshot-final`,
`GX_SHOT_FRAME`, the GPU timers, and the capture-pair scripts.

## Editor browse roots

The GFX editor's asset browser indexes the same guest roots and adds an
in-session manager: type a folder into **Add root** (the folder may hold
`model/`/`models/` and `texture/`/`textures/` subdirectories, like the
`GX_ASSETS` roots) and the browser rescans. Roots added this way persist in
the locally generated `settings.json` under `resources/` (`toolAssetRoots`, colon-separated) so they survive
restarts without touching any preset; **remove** drops one from the list.

Browsing is strictly read-in-place: assigned assets keep their external path,
nothing is copied into the repository, and the copyright audit never sees
foreign content — you can point the browser at an external asset tree, author with it
locally, and keep those files out of gX entirely.

## Layer 1 — Guest roots

- `GX_ASSETS` is colon-separated (PATH-style); each root goes through an
  existence check (missing roots log a WARNING and are skipped).
- Roots are registered through the path system (`src/common/path.c` —
  `gX_Path_Init` / `gX_Path_AddSearchDir`), **after** `gX_Path_Init` so
  they never enter the shader compiler's include-path chain.
- **Fallback-only priority**: guests are appended after the default dirs,
  so gX's own resources always win. A guest file can never shadow an
  engine asset — in particular a guest shader can never replace gX's
  (which matters because shader hot-reload would silently eat it).
- Zero repo footprint: no symlinks, no committed config, no hardcoded
  external paths. Any external asset directory works.

## Layer 2 — Wrapper fallback

`gX_Model_Load` and `gX_Texture_Load` (`src/wrappers/`) retry failed loads
through `gX_Path_GuestFallback(type, fullPath)`: the subpath after the
first `/<type>/` segment (e.g. `resources/model/props/x.glb` →
`props/x.glb`) is resolved against the registered roots. Every existing
load site gains guest support without touching the ~300 `GX_PATH` call
sites. Fallback fires only on load failure, so engine assets are never
double-loaded.

## Layer 3 — Model injection

`--guest-model <subpath>[:count]` / `GX_GUEST_MODEL` spawns `count`
instances (default 1, cap 64) of a guest model in an additive ring
(radius 3.8 u) around the showcase's demo cluster:

- Instances normalize through the model's vertex AABB to ~1.2 u max
  extent, so foreign content reads at demo scale (a 312 k-vertex terrain
  and a teapot both land at sensible sizes; the scale factor and vertex
  count are logged).
- Material: PBR, metalness 0.35, roughness 0.4.
- The demo cluster stays untouched, so capture pairs (fog on/off, effect
  toggles) keep a clean baseline.
- Verification scenes (`--showcase-minimal`, `--showcase-planar`,
  `--reflectrow`) return before the injection point and stay pristine.

## Recipes

```bash
# Perf A/B: guests with fog on/off
GX_ASSETS=.../resources GX_GUEST_MODEL=model/guest/terrain.glb:8 \
  GX_T_FOG=0 ./build/gX_build --showcase --ai-screenshot-final
GX_ASSETS=.../resources GX_GUEST_MODEL=model/guest/terrain.glb:8 \
  GX_T_FOG=1 ./build/gX_build --showcase --ai-screenshot-final

# Pulled-back framing to see the whole ring
GX_CAM_DIST=14 ./build/gX_build --showcase

# Multiple roots (first listed wins on duplicate hits)
GX_ASSETS=/path/gameA/resources:/path/gameB/resources

# Per-frame GPU timing CSV (shadow/reflections/opaque/bloom/ssr per frame)
./build/gX_build --showcase --ai-screenshot-final --gfx-profile=/tmp/gfx.csv
```

## DAE sources → GLB

COLLADA (`.dae`) is not loaded natively, so convert it before pointing a
guest root at the result. Assimp 6.0 is the reliable path for rigged
characters:

```bash
assimp export "Character.dae" Character.glb -fglb2
```

- `glb2` keeps the mesh split, base-colour textures and skin weights.
  Textures are written as **relative URIs** (original PNG names or a
  `Textures/` subdirectory), so keep them beside the GLB or under the same
  parent folder.
- Blender 5.2's bundled `simple_collada_importer` imported a test character
  with 0 bones and exported 0 glTF skins; do not use it for rigged COLLADA.
- gX uploads at most 128 bone matrices per draw (`boneMatrices[128]`), and
  raylib stores joint indices as u8. A rig above the cap is rendered in its
  authored bind pose with a WARNING naming the bone count instead of
  collapsing vertices. Retarget or reduce to ≤128 joints when the model must
  animate.
- Assimp omits `doubleSided` and usually `alphaMode`; closed meshes are
  unaffected, but hair/card layers may need a manual double-sided or alpha
  pass.
- Assimp applies the COLLADA `<up_axis>` through a root rotation. Assets
  authored in a different axis (for example a world map whose geometry lies
  in X/Y) still need a scene-level rotation after import.

## Capture behavior under heavy guests

`--ai-screenshot-final` waits for every scene model to load (frame gate,
`GX_SHOT_FRAME` overrides the target, `GX_SHOT_FORCE=1` bypasses). Heavy
multi-mesh guests collapse the frame rate so the fixed 180-frame target can
become unreachable; a slow-scene escape captures `GX_SHOT_READY_FRAMES`
(default 30) after the last model finishes loading once the run has taken
> 30 s wall-clock. Normal scenes still capture at the fixed target and stay
byte-identical. Failed/blacklisted loads no longer block the gate.

`--no-gl-debug` skips the GL debug callback entirely (headless/harness runs).

## Known behavior (verified 2026-08-28)

- The GL debug storm once attributed to exotic GLBs is a **pre-existing
  reflections-pass artifact** (planar pass, ~14 `GL_INVALID_OPERATION` per
  frame, harmless) — it fires identically with zero guests. The RHI debug
  callback now dedupes identical messages: 2500+ lines per run became ~12.
- The heavy multi-mesh guest (2643 tiny meshes, 1162 per-tile materials) is
  **reflections-pass-bound**: one instance costs ~100 ms/frame of the
  reflections timer (the planar + env-cubemap + point-shadow passes redraw
  every mesh), ~1.1 fps at an 8-ring. The engine renders it correctly
  (vision-verified textured terrain), and captures now fire via the slow
  escape. Multi-mesh GLBs with per-mesh materials cannot be batch-merged
  (u16 index limit + per-tile textures); same-material multi-mesh models
  stay the cheap case.
- Measured averages (GPU timers, 179 frames): baseline ~12.0 ms/frame
  (shadow 0.05, reflections 2.4, opaque 3.9, bloom 1.2, SSR 4.5); teapot
  ring x6 +1.6 ms; the heavy guest x1 +100 ms (reflections-dominated); x8
  ~890 ms/frame (reflections 819, opaque 40, shadow 33).

## Limitations (v1)

- **glb (embedded textures) guests render fully**; obj/mtl guests with
  external textures load untextured (raylib resolves materials
  internally, bypassing the texture fallback hook).
- Guests are models-only for the injection hook; textures and audio
  reach guests only through the fallback chain.
- Material sanitization for guest models (obj/mtl texture resolution)
  is future work.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `MODEL: Failed to load` for a guest file | root missing the trailing content, or the model failed to parse — check the `GUEST: registered` line and the `MODEL: guest fallback '...' -> '...'` INFO line |
| No `GUEST: registered` line | root path typo, or root doesn't exist (WARNING is logged) |
| Guests render white | obj/mtl with external textures (see Limitations) |
| `resources/model/model/...` double segment in a log | fixed in v0.3.8; the injection expects type-relative subpaths (`model/x.glb`, not `x.glb`) |
