# Rendering backlog

Proposed rendering work. Items are exploratory until they have a deterministic
fixture, an opt-in switch, and a dedicated validation target. Production GL33
defaults remain unchanged during investigation.

## Priority order

### Experimental GFX/raymarch visibility

The opt-in `GX_GFX_RAYMARCH_DEPTH=1` prototype compares primary SDF view-axis
depth with current HDR raster depth, after GFX effects. It uses the same external
perspective camera at full output resolution. The focused
`make gfx-raymarch-depth-check` retains analytic overlap metrics, captures,
logs and replays; see [Raymarch](raymarch.md) for limitations.

Mode `2` now prototypes HDR color plus projected depth after MSAA resolve,
before water and effect inputs. The shared-depth gate adds native float readback
and a depth-only DOF consumer/control probe. This is not full deferred integration:
SDF primary world normals now join the existing opt-in SSR normal buffer, and
explicit object roughness rides HDR alpha. Full material buffers and motion
vectors are absent; SSR quality and temporal effects are not certified.
Shared material buffers, transparent ordering, secondary
ray raster sampling and cross-renderer shadows remain separate future phases.

### P0 — Performance benchmark system

Build the measurement foundation before adding expensive visual paths.

- Default baseline: **1024×768 at 60 FPS**.
- Fixed timestep, camera, scene seed, and warm-up interval.
- Capture CPU frame time, GPU frame time when available, percentile frame
  times, pass timings, resolution, backend/context, and enabled feature flags.
- Provide repeatable baseline and feature-comparison reports, not only a single
  average FPS number.
- Keep benchmark runs separate from correctness gates until variance and driver
  coverage are understood.

Suggested targets: `make gfx-benchmark` for a report and
`make gfx-benchmark-check` for bounded regression checks.

Initial foundation: `scripts/gfx_benchmark.py` runs the default
`--showcase-minimal` scene at 1024×768 with a fixed 1/60 timestep and seed
424242, discards a warm-up window, records CPU frame timings from the main loop,
and combines them with the existing `--gfx-profile` GPU pass CSV. It runs three
repeats by default and reports production-default, isolated-control, SSR, SSAO,
bloom, and TAA rows. Feature rows use the feature-matrix all-off contract so
their p95 deltas are attributable to one opt-in path. Use `--rows` to select
additional rows such as `motionblur`, and `make gfx-benchmark-release` to run
the same suite against the Release binary. The check target validates both
streams and their row counts; host-specific performance thresholds remain
deferred from portable CI. The checked-in GTX 1060-class reference can be
run explicitly with `make gfx-benchmark-reference-check`; its p95 budgets are
host-pinned and are not part of the default admission gate.

<!-- //ai: native timing repair preserves source-frame ownership rather than replaying cached timer values. -->
The GFX CSV now retains `frame,draw,readback_frame` followed by interval times.
`ssr_ms` covers the actual SSR draw, `ssr_inputs_ms` covers its preparation;
`pre_ssr_ms`, `pre_bloom_ms` and `post_ms` expose the remaining ordered regions.
`opaque_ms` retains its coarse scene-setup-through-water region. A per-renderer
16-slot timestamp ring publishes only complete matched sets without flush/finish;
overflow or incomplete shutdown tails remain missing and are logged. Renderer
unload owns query/file cleanup. Profiling resources are allocated only for an
explicit `--gfx-profile` request; graphics quality and pass execution order do
not change. Same-frame viewport draws have distinct `draw` IDs.

Benchmark schema 3 selects CPU/GPU frames `warmup+1 .. warmup+frames`, adds 16
unmeasured simulation frames to drain asynchronous results, and rejects legacy
CSV identity, duplicates, missing source frames and invalid timings. Its
single-draw workload contract rejects multiple draws for a measured frame rather
than silently aggregating viewport tiles. Old pinned reports remain historical;
this repair does not automatically adopt new host performance references or
certify default-scene image identity.

Raymarch profiling: `--raymarch-profile[=path.csv]` writes the module's own
GPU timers per frame (`frame,draw_ms,total_ms,render_scale`) and
`GX_GFX_RAYMARCH_RENDERSCALE=<0.1..1.0>` pins the module render scale headlessly
(the same knob the preset exposes). `scripts/raymarch_benchmark.py`
(`make raymarch-benchmark`) combines that CSV with the engine CPU frame CSV
across quality rows (render scale 0.25 → 1.00) and one-feature-off rows
(terrain/water/objects/fog/grid), three repeats, p50/p95, and writes JSON +
markdown reports. `make raymarch-benchmark-check` validates row/sample
completeness, positive timings, and the resolution-scaling trend (0.25 → 1.00
must grow ≥ 2x); host-specific thresholds stay out of the admission gate. On
an earlier tracing build the default row was ~1.06 ms GPU (terrain ≈ 76% of it,
objects ≈ 40%, water ≈ flat).

The benchmark's aggregate p50/p95 now pool all post-warmup frame samples across
repeats (450 samples with the default 150 frames × 3), not percentiles of three
repeat medians. Each repeat still records its own statistics; artifacts validate
the aggregate sample counts. Older reports' aggregate p95 should not be read as
frame-tail latency.

#### 2026-10-01 raymarch review

GTX 1060 6GB-class, GL33, 1024×768, Release, fixed timestep,
45 warmup frames + 150 measured frames × 3 repeats. No overlapping GPU probes.
Before = `049f2be`; after = CPU-side raymarch `-Os`, loader-location caching,
and preset resource cleanup. GLSL, tracing steps, and render-scale defaults are
unchanged. Each table percentile uses 450 post-warmup frame samples.

| render scale | GPU p50 before → after (ms) | GPU p95 before → after (ms) |
| --- | --- | --- |
| default (0.35) | 2.077 → 2.081 | 2.471 → 2.719 |
| 0.25 | 1.284 → 1.283 | 1.615 → 1.566 |
| 0.50 | 3.613 → 3.602 | 3.988 → 4.148 |
| 0.75 | 7.290 → 7.556 | 7.954 → 7.991 |
| 1.00 | 12.061 → 12.225 | 12.700 → 12.783 |

CPU-frame p50: default 2.189 → 2.216 ms, full resolution 12.205 → 12.350 ms.
Median default/full module cost remains close; tail changes and the 0.75-scale
drift are recorded, not treated as proof of an FPS improvement or attributed
solely to compiler flags. These are module GPU timings, not whole-scene FPS.
Terrain remains the largest cost: before-cleanup terrain-off GPU p50 0.577 ms
vs default 2.077 ms. Feature-off deltas overlap; they are not additive budgets.

Release payload shrank 15,200 bytes (3,905,947 → 3,890,747); approved headroom
increased from 629 to 15,829 bytes with unchanged caps/baseline. Reports/raw CSVs:
`/tmp/opencode/raymarch_review/{before,after}/` (session-local, not shipped).

These measurements precede the subsequent water/texture/sky repairs: enabling
water by default, actual secondary water rays, reflected clouds and terrain
detail changes GPU work. Do not treat this historical table as a benchmark of
the repaired scene; it needs a fresh isolated measurement before a performance
claim.

### P1 — Dithering

Add an opt-in deterministic dither stage with selectable ordered/Bayer and
noise modes. Evaluate it before palette quantization so banding reduction can
be measured independently.

Initial implementation: the shipped interleaved-gradient-noise path remains
mode `1`; `GX_T_DITHER_MODE=0` disables it, `2` selects deterministic 4×4
Bayer, and `3` selects seeded hash noise. `GX_T_DITHER_SEED` fixes the spatial
seed, `GX_T_DITHER_STRENGTH` bounds the LSB amplitude, and
`GX_T_DITHER_ANIMATE=1` opts into frame-varying noise. Run `make dither-check`
for the fixed 1024×768 sky fixture and deterministic-repeat gate.

- Preserve linear/HDR values until the documented quantization point.
- Use a frame-stable seed for screenshot gates and an optional animated seed
  for interactive presentation.
- Compare gradient, sky, fog, and shadow fixtures with banding/error metrics.

### P1 — Limited color palettes

Add opt-in **16-color** and **256-color** output modes, preferably through a
palette/LUT quantizer after tonemapping.

Initial implementation: `GX_T_PALETTE_MODE=1` selects a fixed 16-color EGA
palette and `=2` selects deterministic RGB332 (256-color) quantization. Mode
`0` is the production default. Quantization occurs after tonemapping/gamma;
dither remains independently controlled by `GX_T_DITHER_MODE`. Run
`make palette-check` for color-count, dither-toggle, and repeatability checks.
The benchmark can measure palette cost separately with
`--rows isolated,palette16,palette256 --compare-to isolated`.
It reports non-gating overhead budgets of +2.0 ms CPU/+3.0 ms GPU p95 for
16-color and +2.0 ms CPU/+2.0 ms GPU p95 for RGB332 on the isolated row;
these remain advisory because GPU timing is host/driver-specific.

- Keep palette selection explicit and deterministic.
- Measure color error, banding, and silhouette preservation separately.
- Dithering must be independently switchable so palette quality is not hidden
  by noise.

Dependency: dithering design and the benchmark baseline.

### P1 — Normal-map experiments

Extend normal-map experiments beyond direct PBR shading while keeping the
current SSR-normal path advisory and opt-in.

- Use deterministic flat-plane, curved-object, mirrored-UV, and grazing-light
  fixtures to compare flat, geometric, tangent-space, and detail-normal paths.
- Verify tangent-frame construction, handedness, UV scale/offset, mip/LOD
  stability, normal-strength limits, and energy/roughness interaction.
- Keep direct shading and screen-space consumers separate: a normal map may
  perturb local lighting without silently changing SSR until the normal-buffer
  graduation criteria are met.
- Continue `GX_SSR_NORMALS=1` and `GX_T_SSR_NORMALMAP=1` as diagnostic hooks
  only; the current non-planar SSR tradeoff remains `HOLD`.
- Add light-sweep, normal-buffer, reflection, and cost metrics with a flat-map
  fallback for missing tangents, unsupported assets, and GL33-safe operation.

Dependencies: tangent-frame validation, deterministic material fixtures, and
the SSR normal-buffer evidence in `docs/reflection_pack.md`.

### P1 — Interlacing

Prototype an opt-in display treatment with independently selectable horizontal,
vertical, and dot interlacing patterns.

Initial implementation: `GX_T_INTERLACE_MODE=1` selects horizontal lines, `=2`
selects vertical lines, and `=3` selects a checker/dot pattern. Mode `0` or an
unset variable is the production default. `GX_T_INTERLACE_STRENGTH` controls
attenuation, `GX_T_INTERLACE_PHASE` controls pattern phase, and
`GX_T_INTERLACE_ANIMATE=1` opts into frame-varying phase. Run
`make interlace-check` for cadence, phase, strength-zero, and repeatability
checks; benchmark rows are `interlace_h`, `interlace_v`, and `interlace_dot`.

- Keep the effect after tonemapping and expose `off`, `horizontal`, `vertical`,
  and `dot` modes without changing the production path.
- Measure line/dot cadence, motion stability, luminance error, and readability
  on text, gradients, and high-contrast silhouettes.
- Use a deterministic scanline fixture plus an explicit strength/phase control;
  animated phase is interactive-only until temporal artifacts are characterized.

### P2 — Interactive low-resolution path-tracing comparison (prototype)

The GFX editor now exposes optional `Raster`, `Ray preview`, and synchronized
`Side by side` modes. Raster remains the default. The late-1990s Bryce/Y2K
style target is already described in
`docs/visual_direction.md`; the path tracer should make the authored geometry,
materials, lighting, and atmosphere easier to compare, not impose a separate
post-process style.

`GX_T_ORACLE_GPU=1` remains the GL 4.3+ compute audit hook. The editor preview
uses a separate RGBA16F target (320 pixels wide at full view; proportional
height, 320×180 at 16:9) and a separate deterministic accumulation image. It
traces primary hits and misses, direct sun/point-light visibility (when the
global shadow toggle and light's cast-shadows option are enabled), and up to
two diffuse secondary rays.
The right side of `Side by side` uses the same vertical field of view as a
center-cropped raster view. Scene geometry/BVH uploads are cached by model,
transform, coarse material, and mesh-buffer signature; progressive samples
reset when the camera, scene signature, lights, exposure, or target dimensions
change. Accumulation is capped at 256 samples.

The prototype is deliberately limited to loaded opaque model meshes, per-mesh
base color and RGBA8 albedo textures (up to 32 distinct maps, each no larger
than 1024×1024), coarse per-model metalness/roughness, one sun, and up to 32
point lights. Other texture formats or larger maps fall back to the material
base color. Terrain, spotlights, emissive and non-albedo material maps,
instancing, transparent materials, deformed vertex buffers,
skybox/procedural atmosphere, fog, reflections, and raster post effects are not
traced. Primary misses use the scene clear color.
The editor reports the GL 4.3 compute/SSBO requirement;
normal GL 3.3 operation and the production default remain raster-only. When
raylib is configured for GL 3.3, `GX_GL43_CONTEXT=1` explicitly requests a
desktop GL 4.3 core context at startup; with the variable unset, raylib's
configured context request is unchanged.

Prototype gates:

- Give the ray preview its own initially 320×180 HDR target and primary-hit,
  primary-miss, direct-light visibility, and environment outputs. Trace opaque
  base-color textures and document unsupported material/effect features rather
  than silently mixing them with raster output.
- Accumulate deterministic samples progressively while the scene is static;
  reset accumulation when camera, geometry, material, or lighting changes.
- Cache triangle/BVH uploads behind scene revisions instead of rebuilding them
  each frame. Keep the raster and preview cameras, framing, exposure, and
  tonemapping matched for an honest split comparison.
- Gate availability on GL 4.3 compute/SSBO support. GL 3.3 and production
   defaults retain the existing raster path.
- Add CPU/GPU parity, convergence/reset, raster-versus-preview capture, and
  low-resolution timing checks on a GL 4.3+ device before enabling the editor
  mode by default. `GX_GL43_CONTEXT=1` created a GL 4.3 context on a
  GTX 1060-class display. Base-color texture sampling, 256-sample convergence,
  explicit reset, camera edits, model transforms, and side-by-side capture were
  verified through `GX_AI_INPUT`; the final `glCopyImageSubData` texture-array
  path was recaptured at 256/256 samples, and the button's baked label is visible
  in the ray view. At 320×212, GPU total-time medians for frames 201–359 were
  4.62 ms in raster mode and 24.51 ms while tracing (19.90 ms incremental).
  This is a machine-specific whole-frame timer comparison, not a portable
  budget.

### P2 — Reduced-resolution depth of field — implemented opt-in

Evaluate half- and quarter-resolution DOF blur/composite paths as an opt-in
performance experiment. `GX_T_DOF_RES=0` retains the full-resolution path;
`=1` selects half-resolution blur and `>=2` selects quarter-resolution blur.
Run `make dof-resolution-check` for the deterministic fixture and use the
`dof_full`, `dof_half`, and `dof_quarter` rows in `gfx_benchmark.py` for timing.

- Preserve full-resolution circle-of-confusion and depth classification where
  needed, then validate foreground/background edges with the depth-aware
  four-tap upsample.
- Compare quality against the current path for thin geometry, transparent
  surfaces, HUD/text, bokeh highlights, and camera motion.
- The fixture covers a thin foreground silhouette, a transparent panel, distant
  emissive bokeh points, and deterministic camera motion; the full-resolution
  path remains the fallback for unsupported or unstable cases.

### P2 — Snow on floor — implemented opt-in decal fixture

Prototype an opt-in snow layer for floor/terrain surfaces. `GX_T_SNOW=1`
enables six bounded floor decals; `GX_SNOW_COVERAGE`, `GX_SNOW_MELT`,
`GX_SNOW_HEIGHT`, and `GX_SNOW_SLOPE` control the deterministic fixture.
Run `make snow-check` for captures and use the `snow` benchmark row for cost.

- Start with a bounded height/decal or material-mask fixture rather than a
  scene-wide weather system.
- Cover accumulation, edge/facing response, shadowing, melt/coverage limits,
  and camera-distance behavior.
- Gate with deterministic winter-floor captures and a cost row in the
  benchmark system; production defaults remain snow-free.

### P2 — Weather systems — falling-snow slice implemented opt-in

Build a proper opt-in weather layer covering rain, falling snow, and fog
particles rather than treating weather as a single screen effect.

Initial slice: CPU falling snow (`GX_T_SNOWFALL=1`, GLSL 330 VBO point
sprites — no compute/SSBO, GL33-safe by design). Deterministic LCG sim is
separate from rendering: `GX_SNOWFALL_SEED/RATE/WIND/WINDZ/SPEED` control
spawn, precipitation count, wind, and fall speed; flakes live in a
camera-relative box with floor collision/respawn and distance LOD.
Accumulation reuses the snow-decal fixture (`GX_T_SNOW=1` runs both).
Run `make snowfall-check` for seed/wind/rate/distance/accumulation captures
and use the `snowfall` rows in `gfx_benchmark.py` and the feature matrix.
Rain streaks and fog sprites remain open following the same CPU-sim pattern.

Second slice: CPU rain streaks (`GX_T_RAIN=1`, GLSL 330 GL line segments
along the wind-slanted fall vector — points cannot stretch). `GX_RAIN_SEED/
RATE/WIND/WINDZ/SPEED` mirror the snowfall contract at a 7 m/s fall speed;
floor collision respawns (splashes deferred). Run `make rain-check` and use
the `rain` benchmark/matrix rows. Fog sprites remain open.

Third slice: CPU fog sprites (`GX_T_FOGSPRITE=1`, camera-facing billboards
as GL triangles — back-to-front sorted, depth-tested, tinted from
`fog.color`). `GX_FOGSPRITE_SEED/COUNT/WIND/OPACITY` control the bank;
distance and height fades bound influence, and a max-delta budget guards
silhouette readability. True depth-texture soft particles are deferred.
Run `make fogsprite-check` and use the `fogsprite` benchmark/matrix rows.

- Separate deterministic weather simulation from rendering: seeded spawn,
  wind, gravity, lifetime, precipitation rate, and camera-relative bounds.
- Make rain and snow depth-/collision-aware where practical, with bounded
  particle counts, distance LOD, and a GL33-safe fallback.
- Treat fog particles as depth-aware sprites or bounded volumes with explicit
  sorting/blending rules; define interaction with lighting, shadows, and the
  existing volumetric fog path.
- Validate with fixed weather seeds, wind changes, accumulation/visibility
  fixtures, silhouette readability, and dedicated benchmark rows for rain,
  snow, and fog.

### P2 — Reflection map / Gaussian splat

Investigate two bounded reflection representations separately:

1. Reflection-map capture/projection for a cheap environment or local probe
   path.
2. Gaussian-splat reconstruction for an opt-in research fixture.

Initial slice: genuine miniature rasterizer (`GX_T_SPLAT=1`) — perspective-
projected 2D covariance, conic exponent falloff, back-to-front sorted alpha
compositing over a procedural seeded fibonacci-sphere cloud (normal-mapped
colors, no assets). `GX_SPLAT_SEED/COUNT` control the fixture; distance LOD
halves/quarters the count. Memory budget (1024 max): ~40 KB cloud, ~36 KB
projection temps, ~384 KB transient upload — all heap, zero BSS growth.
GL33-safe by construction (plain triangles, GLSL 330). Run `make
splat-check` and use the `splat` benchmark/matrix rows. Verdict so far:
rasterizer proven deterministic and cheap; reconstruction-from-data (.ply
loading, tile rasterizer) stays deferred until a mesh-fidelity use case
needs it — no value demonstrated over planar/SSR/cubemap yet.

Define memory limits, update cadence, blend/occlusion rules, and GL33 fallback
behavior before implementation. Do not introduce a mandatory asset or runtime
dependency until a fixture demonstrates stable value over the existing planar,
SSR, and cubemap paths.

Dependencies: benchmark system and reflection regression fixtures.

Memory budget (128px default): 4 probes × 6 faces × 128² RGBA16F ≈ 3.2 MB
plus 4 × 128² DEPTH24 RBOs ≈ 0.25 MB. Update cadence: bake one probe per
frame from frame 30 until all baked, then static; `GX_PROBE_REFRESH=N`
re-renders one probe every N frames (bit-stable on static scenes, gated by
`make probe-check`). Blend rule: nearest-only by default is off — blending
is on unless `GX_PROBE_BLEND=0`; crossfade 3 m width, second probe on unit
31. Occlusion: probes mute specular/reflections/ambient during capture (no
recursive env). GL33: the bake path is plain forward draws (no compute or
SSBO) — GL33-safe by construction. Run `make probe-check` for the
minimal-scene cubemap fixture and use the `probes` benchmark/matrix rows.

### P3 — Infinite two-mirror temporal feedback — implemented opt-in

Research a bounded ping-pong render-target loop for facing mirrors.
`GX_MIRROR_PAIR=1` adds two facing slabs plus an emissive seed bar to the
minimal scene and engages the loop; `GX_MIRROR_ENERGY` (default 0.90,
clamped 0..1) bounds feedback energy.

- Recursion budget: 1 stored bounce per mirror (plane 0 reuses the legacy
  full-res planar RT for oracle parity; plane 1 and both histories are
  half-res). Regress depth grows with settled frames, energy-capped.
- Validity is frame-global and conservative: camera VP, render size, or
  exposure change clears both histories (black rebuild fallback); per-pixel
  velocity masking is deferred.
- Blend/occlusion rules: counterparts draw in the facing plane's view
  sampling history with the history MVP; plane-1 documents its gaps
  (unfogged fill, no terrain march, no stats — plane-0-only by design).
- GL33: plain forward draws + one fullscreen multiply (no compute/SSBO).
- Cost: ~2x planar renders (mirror benchmark row: +4.8 ms CPU / +3.4 ms GPU
  at 1024x768 — research-grade, never default-on).
- Run `make mirror-check` (response, settle convergence, energy mechanism,
  camera-motion churn, exact repeat) and use the `mirror` benchmark/matrix
  rows. Known geometric limits: edge-on faces sample thin patches (verified
  numerically); inside-the-pair views correctly show no regress.

- Use an explicit recursion/depth budget and history validity mask; “infinite”
  means visually persistent, never unbounded GPU recursion.
- Add disocclusion, camera-motion, exposure, and resize invalidation rules.
- Provide a black/fallback path for unsupported GL33 configurations.
- Require a temporal stability gate, feedback-energy bound, and frame-time row.

Dependencies: benchmark system, reflection-map ownership, and temporal-history
invalidation infrastructure.

## Shared acceptance rules

- Every item starts behind an environment variable or opt-in preset.
- Production presets and default GL33 behavior remain unchanged.
- New visual paths need deterministic A/B captures and a documented fixture.
- Expensive paths need benchmark evidence at 1024×768@60 before graduation.
- Research representations must have an explicit fallback and resource budget.

## Raymarch revival (2026-09-28)

Decision: raymarch is no longer schema-less in gX. The renderer (866 lines),
the SDF shader library (`common/sdf_basic.glsl`, `sdf_ops.glsl`,
`sdf_deform.glsl`, `noise.glsl`) and the `--raymarch` scene were already
present; the JSON scene schema (`schema2.c` save/load) was a no-op stub. It
now implements the offset-based schema natively (state, parameters, fog, sun,
terrain, water, flags, misc, debug, plus the `objects[8]`/`lights[8]`
bridges) through `gX_Schema_JSON_*`, so raymarch scenes are authorable and
persistable like the background renderer's presets.

- Gate: `make gfx-raymarch-check` (summary row `raymarch`) — a
  save-mutate-reload round trip plus a non-empty scene frame.
- Done since: the harness tests are `--test-only` coverage (`RM_TESTS`), the
  editor's Raymarch panel edits parameters and saves/loads presets, and the
  gate checks the panel too.
- Integration: the module can be forced into any scene (panel checkbox /
  `GX_GFX_RAYMARCH_VIEW`), and `gfx-raymarch-features` verifies each enable
  flag's frame effect plus the `CF_*` defines compile matrix.
- Compositing v1: `GX_GFX_RAYMARCH_COMPOSITE` / the panel's composite toggle
  sets compositor layers (raymarch backdrop, gfx screened over it) — both
  layers visibly contribute.
- Compositing v2 (2026-09-29): `GX_GFX_TRANSPARENT_BG=1` adds a
  transparent-background gfx mode — the `scene_mask` pass writes the layer's
  alpha from the scene depth (geometry 1, sky/far 0, RGB preserved via a
  separate ZERO/ONE blend, no scratch read-back) and the compositor blends the
  gfx layer with true ALPHA, so the raymarch backdrop shows through the gfx
  sky. `GX_GFX_RAYMARCH_COMPOSITE=2` renders the backdrop layer alone (gate
  reference). `gfx-raymarch-features` asserts the sky strip matches the
  backdrop exactly (0.000), the geometry region matches the opaque gfx frame
  (0.000), and the SCREEN path differs (152).
- Visual pass v1: the default scene ships a torus + light, water and sun
  shadows on; the panel edits objects/lights, the debug visualizations and
  live `CF_*` flags (hot reload).
- Gallery + lighting pass: the panel lists saved presets, built-ins and
  randomize; the light path is guarded (attenuation floor, alpha injection)
  and the terrain slope mix uses smooth bands against speckle.
- Speckle: the smoothed-slope color mix cut the terrain neighbour-difference
  metric from 3.9 to 2.7 (calibrated in the visual pass); a deeper look at
  the noise texture's high-frequency content is optional now.

- Volumetric fog + sun shafts (2026-09-29): opt-in height fog with the gfx
  `fog_volume` semantics — density / height falloff / scattering / HG
  asymmetry / color, a 20-step march over the shaded image with the height
  band anchored at eye level − 1.5, 3-tap SDF sun visibility for the shafts,
  and a sky-ambient fill for multiple scattering so the haze brightens the
  horizon instead of only darkening it. `CF_VOLFOG` compiles the path
  (default on; `enableVolumetricFog=0` keeps frames identical), the panel
  gains a **Volumetric fog** section, the schema round-trips a `volfog`
  section, `cloudscape` enables it, and `gfx-raymarch-features` adds a
  `volfog` row (delta 4.7) plus a `volfog-off` define variant.
- Proxy wireframe mode (2026-09-29): `flags.enableProxy` (panel checkbox +
  feature hook `proxy`) stops the SDF march and draws raster line proxies —
  object wireframes at their exact shader transforms, terrain/water grids,
  light/sun markers — for layout work and low-power sessions; the profiler
  still records the cheap frame. `gfx-raymarch-features` gains a `proxy` row
  (delta 78.1).

Resolution: the default `renderScale` dropped 0.5 -> 0.35 (2026-09-28) to cut
the desktop lag of the heavy shader; presets persist it, so quality can be
raised per scene.

- Primitive set v2 (2026-09-29): five new object types join the shader and the
  panel — **menger** (xyz half extents), **capsule** (x half-length, y radius),
  **octahedron** (x size), **rounded box** (xyz half extents, w corner
  radius) and **hex prism** (x radius, y half-height) — with CPU twins in
  gxmath (`gX_F32_SDF_Capsule`/`_Octahedron`/`_RoundBox`/`_HexPrism`, plus the
  existing `_Menger`, floor-mod fold so negative inputs match GLSL `mod`). The
  panel combo lists all 14 types, `Object_Init` ships per-type attribute
  defaults, and a new built-in **primitives** preset renders the set as one
  hero row. Coverage: `gfx-raymarch-check`'s gallery leg asserts the five new
  types each change their frame ROI (row delta 7.4), the math corpus carries
  the new twins vs the oracle (39 values), and `RM_TESTS` pins the shapes.
  `GX_GFX_RAYMARCH_GALLERY=1|2|3` drives the gallery/reference/new-types
  captures (the row checks stay ROI-symmetric because the shader camera basis
  mirrors X; per-type attribution is done by the compacted five-object row).
- Raymarch texturing v2 (2026-09-29): the skybox and tex0..2 are authorable
  end to end. Presets carry a `textures` section (paths, filters, skybox
  layout) and a changed path unloads/requests a reload through
  `gX_RendererRaymarch_Texture_SetPath`/`_Reload`; the editor panel gains a
  **Textures** header (path fields, filter/layout combos, reload buttons,
  48px thumbnails) and the Objects section replaces the raw `texturing` vec4
  with texture/mapping/scale controls plus a preview. The gfx asset browser
  gains two raymarch targets (`RAYMARCH_SKYBOX`, `RAYMARCH_TEX` slot 0..2) so
  textures are picked from the same browser as the rest of the scene, and the
  headless feature hook adds an `objecttexture` row (tex0 triplanar vs off on
  a diffuse-biased torus; frame delta 2.8).
- Volumetric clouds (2026-09-29): an opt-in cloud deck — 3D simplex FBM
  density in a `bottom..top` slab, a 4-sample sun march for self-shadowing,
  forward-scatter phase plus an ambient sky fill, 28 fixed view steps with a
  2% transmittance early-out, composited after fog/sky. `CF_CLOUDS` compiles
  the path (default on, `enableClouds=0` keeps frames identical), the panel
  gains a **Clouds** section, the `cloudscape` built-in turns the deck on, the
  schema round-trips a `clouds` section, and `gfx-raymarch-features` gains a
  `clouds` row (delta 58.0) plus a `clouds-off` define variant.
  Fixing the cloud field uncovered two latent noise bugs in
  `common/noise.glsl`: `gX_Noise_Hash3` returned unsigned values and the
  simplex lacked Ashima's 42x normalization, so the field sat at
  0.5 +/- 0.002 and every coverage threshold collapsed. The fixed field has
  std 0.049 and 80+ distinct levels; the opt-in `CF_FBMNOISE3D` terrain path
  uses the same simplex and is improved by the fix.
