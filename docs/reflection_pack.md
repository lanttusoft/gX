# gX Reflection Pack

Techniques, selection rules, verification, and known limits for reflections
in the gX GFX forward renderer. Status: **Beta** (SSR receiver-routing closure,
2026-09-23).

## Techniques

| Technique | Path | Surfaces | Fidelity (yaw45, diag cube) |
|---|---|---|---|
| **Planar RT** | `draw.c` planar pass → `reflection.planar.wrapper.rt` → `standard.frag` planar branch | Flat mirror surfaces (`params.planarReflection`) | **54.69% (A)** |
| **Non-planar analytic** | per-fragment local plane → global mirror-plane intersect → planar RT (`standard.frag` non-planar branch) | Curved mirrors (`planarReflection && nonPlanarReflection`, model.c:802) | **A (suite-gated)** — `reflect_suite.py` nonplanar row: two-component oracle (`probe_nonplanar_verify.py`) scores planar-RT floor-bounce 0.57 (gate 0.30) + cubemap-fallback direct image 0.71 (gate 0.50) |
| **SSR** | Hi-Z ray march (`ssr.*`) → composite | Model receivers (`params.receiveSSR`, default on); dedicated water receiver | 44.86–57.25% across the current three-angle gate |
| **Probe grid** | 4×128² baked probes, nearest + second-nearest distance crossfade on shared cubemap slots, box projection unmodified | Direct env, planar fallback, SSR env (`GX_PROBE_GRID=1`, opt-in; `GX_PROBE_BLEND=0` disables crossfade) | bisector fixture live; full-scene fidelity unchanged |
| **Dynamic cubemap** | `gX_Cubemap_RenderEx`, 6 faces/frame | IBL fallback + SSR env fallback + direct samplers | direct A/B + face audit (A) |
| **IBL** | BRDF LUT + irradiance + prefilter | All PBR when `ENABLE_ENV_AMBIENT` and maps generated | n/a (ambient) |

### Optional SSR shading-normal buffer

Set `GX_SSR_NORMALS=1` to render a full-resolution, rasterized interpolated
shading-normal buffer for the SSR pass. This adds one opaque model pass and
replaces depth-reconstructed normals for covered model pixels, reducing
faceting on curved mirrors. The default remains disabled for GL33 production
behavior; terrain and uncovered pixels continue to use depth reconstruction.
For the colored-model material check, combine it with `--ledrow` and
`GX_T_SSR_NORMALMAP=1`; that opt-in fixture injects the existing brick normal
map into the teapot and colored models. Water is separate and retains its
analytic normal path.

<!-- //ai: optional target precision repair, not a default-on promotion or broader visibility certificate. -->
The optional target uses RGBA16F: packed `0.5` must represent zero normal
components exactly. RGBA8 biased even a flat normal and shifted environment
lookups past an occluder. `scripts/ssr_normal_precision_check.py` verifies native
normal/ray precision and the owned occlusion fixture; `--before-binary` adds
normals-disabled identity against an explicit snapshot. See
[`ssr_coverage_contract.md`](ssr_coverage_contract.md) for evidence and limits.

Default-on evaluation (2026-09-20, verdict: remain opt-in): the corrected
paired capture found that the raw planar-fill path reused the shared standard
shader after the normal pre-pass without clearing `ssrNormalPass`. The final
normal-pass model therefore left the next frame's planar fill emitting encoded
normals instead of lit reflection content. The fill now explicitly resets that
uniform before drawing. With the fix, normal-on planar rows match the normal-off
baseline (53.77 / 55.01 / 55.98%), and the regular SSR rows retain their
baseline grades. The curved non-planar fixture still diverges because the
buffer changes the SSR ray normal on the sphere (planar 0.253, cubemap 0.653,
F versus 0.458 / 0.711, A− off); it is not promoted until that quality tradeoff
is understood. Perf cost remains unmeasurable (~8.3s vs ~8.2s per 180 showcase
frames). The flag stays opt-in.

Historical investigation evidence (before the precision repair): the corrected RenderDoc pair shows a populated RGBA8
normal target with binary coverage, world-normal encoding, and the SSR sampler
bound to the same resource. The planar model/cube region is identical after
the uniform reset; the remaining non-planar difference is in the intended SSR
shading-normal path, not normal-target coverage, orientation, or binding.

The repeatable diagnostic lane is `make ssr-normal-check`. It captures
normals-off/on minimal and planar scenes, validates the `GXNRM1` RGBA8
compatibility conversion (binary alpha coverage, unit-length decode, and bottom-left readback),
and writes decoded normal views, the `GX_SSR_DEBUG=28` disagreement heatmap,
plus `report.json` to
`/tmp/opencode/ssr_normal_check`. The lane is advisory and does not change
`make reflect-check` grades or the production normals-off default. A single
raw target dump can be requested with
`GX_SSR_NORMAL_DUMP=/tmp/opencode/normals.rgba`.

Debug mode 28 renders green for camera-oriented agreement, red for angular
disagreement, and blue for water/uncovered pixels. The comparison orients
each candidate independently before measuring the angle, so a back-face sign
convention cannot masquerade as a full-frame disagreement. The current fixture
reads 99.9% aligned overall, with 0.2% cube-edge and 0.4% sphere-edge
disagreement; this rules out a global normal encoding/orientation failure, but
does not justify graduation because the curved-scene A/B quality tradeoff
remains.

The shared SDF/raster path has a separate attributed reflection fixture:
`make gfx-raymarch-reflection-check` analytically traces a sphere's specular rays
to a visible green cube and removes the cube for the negative control. It tests
accepted hit/color recall, false hits, authored roughness early-outs, and
environment miss routing from two cameras; this is not a promotion of the
shading-normal path above. See `docs/raymarch.md` for the bounded gap traversal
and planar-depth interpolation contract. Debug 32 returns native candidate
world gap, signed view-depth residual, and candidate presence for rejected-hit
triage.

### SSR receiver routing (Beta closure)

`params.receiveSSR` controls whether a model's visible surface can receive the
screen-space reflection overlay. It defaults to true so existing scenes keep
their current coverage. Setting it false writes the receiver marker into the
existing HDR alpha channel; roughness remains in `[0,1]`, and `2.0` marks an
opted-out surface. The transparent roughness-repair pass writes the same
marker. No additional render target is required.

This flag is independent of `params.enableReflections`, which controls direct
environment/IBL shading. An SSR-opted-out model still appears in the scene
color/depth used as a reflection source for other receivers. Water remains a
dedicated SSR receiver with its analytic wave normal. `GX_SSR_DEBUG=31` shows
receivers green and opted-out model pixels red; `reflect_suite.py` checks both
classes in the minimal fixture.

For edge experiments only, `GX_SSR_NORMAL_FALLBACK_DEG=N` makes SSR select the
depth-reconstructed candidate when the raster/depth disagreement exceeds `N`
degrees. The default is zero (disabled). The current sweep is not a candidate:
1° worsens the minimal fixture, 5° trades a small planar loss for a small cube
gain, and 10° is effectively unchanged. The hook remains diagnostic and does
not alter production presets.

The same lane captures `GX_SSR_DEBUG=2`, which encodes the active primary
reflection ray for normals-off/on comparison, and correlates its angular delta
with hit-class changes from `GX_SSR_DEBUG=14`. The current fixture reads a
0.764°/0.833° mean delta and a roughly 1° p95, but the rare hit-class-changing
pixels move by 25.642°/22.890° versus 0.752°/0.806° for stable hits. This
points to edge-ray classification sensitivity rather than a global normal
space error. The follow-up A/B isolates the silhouette-jump gate as the active
classifier; see `docs/ssr_edge_ray_2026-09-23.md`.

The lane also captures `GX_SSR_DEBUG=30` for the primary lift-off vector and
`GX_SSR_DEBUG=11` for confirmation, phantom-rejection, and crossing counts.
The start delta mean/p95 is 0.00286/0.00784 in the minimal fixture and
0.00312/0.00784 in the planar fixture; class-changing pixels reach
0.08923/0.07917 versus 0.00285/0.00305 for stable hits. Confirmation remains
effectively unchanged, while phantom/crossing deltas are 0.0302/0.1282
(minimal) and 0.0403/0.1390 (planar), versus 0.0005/0.0001 for stable hits.
The evidence now points specifically at lift-off-induced crossing and phantom
classification, not LOD confirmation.

The controlled lift-off variants use `GX_SSR_NORMAL_LIFTOFF_MODE=1` for the
depth normal only at ray start, `2` to remove the normal-aligned lift, and `3`
for half-strength lift. Mode `4` uses the depth normal only when the normal
disagreement exceeds `GX_SSR_NORMAL_LIFTOFF_DEG`.
They leave the raster normal in the reflection direction and are captured
automatically by `make ssr-normal-check`; mode zero is the production/default
path. The current sweep leaves the minimal score at
55.06% with depth-only lift, while no-lift and half-lift fall to 53.99% and
54.49%. On the curved fixture, depth-only lift changes planar/cube from
0.582/0.693 to 0.587/0.688; no-lift and half-lift are worse. Lift-off is
therefore causal but has no safe universal variant yet. The disagreement-gated
hybrid is also mixed at 0.587/0.686, so further lift heuristics are deferred.

`GX_SSR_NORMAL_ACCEPT_MODE=1` is a separate diagnostic that skips only the
final phantom-hit rejection while leaving ray construction unchanged. It is
used by `make ssr-normal-check` to test the acceptance-side hypothesis; mode
zero remains the production/default path. The relaxed result is identical to
normal-on in both fixtures, ruling out that final gate. A controlled bypass of
the earlier self-plane swallow changes no hit class in the planar fixture;
varying the silhouette-jump threshold changes about 1.8% of classified hits
and improves the curved-reflector cube-zone oracle at yaw 30/45. These remain
opt-in experiments pending near-contact/ghost-ring validation; production
values remain `maxDepthDelta=0.6` and distance scale `0.5`. Reproduce with
`GX_T_MAXDEPTH`, `GX_T_MAXDEPTH_SCALE`, and `GX_SSR_SELFPLANE_REJECT`; details
and multi-angle numbers are in the audit report.

## Optional normal/height/parallax material gate

Run `DISPLAY=:1 make material-maps-check` for a live-only A/B gate over the
`--ledrow` fixture. It compares flat materials, the injected brick normal map,
and the existing displacement texture through parallax modes 0 and 2. The
fixture is enabled only by `GX_T_MATERIAL_MAPS=normal|parallax`; production
scenes remain unchanged. `GX_T_PARALLAX_MODE=0|1|2` selects the per-model
quality mode for manual captures.

## Per-surface gating rules (current)

<!-- //ai: auxiliary images do not establish arbitrary off-plane ray visibility. -->
The optional SSR auxiliary view reuses a mirrored plane camera. Its origin
lookup is not a general off-plane intersection: even matching reflection
directions can have different ray origins. The current missing-depth-mask gate
also retains permissive legacy behavior. See
[`ssr_coverage_contract.md`](ssr_coverage_contract.md) for Lean counterexamples,
current routing invariants and a separate, unimplemented optional-camera policy.

- **Planar pass** runs iff ≥1 loaded model has `planarReflection=1`.
- **SSR post-pass** runs when enabled and its shader, target, and Hi-Z chain are
  available, including in scenes with planar surfaces. When a planar surface
  exists, the planar-defer uniform suppresses SSR output only for origins on
  the owned reflection plane; other receivers continue using SSR.
- **SSR receiver** is selected per model by `receiveSSR` (default true). This
  gates reflection origins, not ray-hit sources. Water is handled by its own
  surface test and remains a receiver independently of model flags.
- **Cubemap render** (6 faces) runs only when a consumer exists:
  1. a model with `enableReflections` samples it directly,
  2. the SSR env fallback — but only if the SSR pass will run,
  3. IBL maps are absent (fallback irradiance/prefilter),
  4. **the water pass** samples it for its sky reflection
     (`ENABLE_WATER && loaded`) — a water-only scene with SSR off must
     still render faces or the reflection collapses to black.
  A planar-only scene renders zero cubemap faces.
- **Water**: object reflections come from SSR with analytic Gerstner wave
  normals (`isWater` branch in gfx_ssr.frag); the water pass itself paints
  the sky via the dynamic cubemap. Water and planar reflections coexist: SSR
  still runs, while plane-origin results defer to the forward planar sample.
  Water uses a 12% Fresnel floor at normal incidence so high/face-on views
  keep visible object reflections instead of reading as "no reflection"; the
  water march step is capped at 0.5 u so thin props survive the coarser water
  cadence. A bright procedural sky is not a reflection gate — it can still
  wash out a weak reflection visually. The above-water surface also applies the
  scene fog (`enableFog`, near/far/color), and `standard.frag` evaluates SSAO
  before fog so a fully fogged surface converges to the fog color instead of
  being re-darkened by AO.
- **Planar receivers**: `standard.frag` samples the planar RT in the forward
  pass; the SSR composite defers on-plane origins to that result instead of
  globally disabling SSR for the frame.
- **Planar capture clip**: the full-lit planar fill discards fragments on the
  back side of the mirror plane (`dot(worldPos, n) < d`), so an object crossing
  or behind the plane cannot contribute an opposite-side ghost. The plane uses
  the same authored normal/distance as the receiver. `GX_PLANAR_NO_CLIP=1` is
  the A/B bypass; the debug solid-fill path is not clipped.
- **Non-planar gating**: the non-planar analytic branch fires only when a
  model sets BOTH `planarReflection` and `nonPlanarReflection`
  (model.c:802); the planar RT must exist, so the scene still needs ≥1
  planar surface. `nonPlanarReflection` defaults to false — enabled only
  by the `GX_NONPLANAR_TEST=1` fixture (scene.c), which adds a chrome
  sphere to `--showcase-planar`. The outgoing local reflection ray, projective
  `reflMVP` sample, and plane-membership gate are aligned with the CPU oracle;
  the production path remains disabled.
- **Probe grid** (`GX_PROBE_GRID=1`, `GX_PROBE_SIZE`, `GX_PROBE_REFRESH`):
  opt-in; bake-after-settle (frame≥30 + consumer loaded); feeds direct
  env, planar fallback, and SSR env. With the grid on, `GX_PROBE_BLEND`
  defaults on: the nearest probe is sampled on the normal material map,
  the second-nearest on unit 31, and its weight falls from 0.5 at the
  bisector to 0 over a 3m distance gap. `GX_PROBE_BLEND=0` restores the
  MVP-1 nearest-only path. `GX_RT_VIEW=probe0-3/cube` inspects.
- **TAA default on** in showcase: retains SSR, hf −30%, edge −36%, fps
  unchanged (11=11). Harnesses pin `GX_T_TAA` explicitly for determinism.
- **Direct cubemap verification**: `reflect_suite.py` runs an isolated A/B on
  `--showcase-minimal` with `GX_CUBEMAP_TEST=1`. This marks the floating cube
  as a direct cubemap consumer while SSR, IBL ambient, and post effects are
  disabled. The gate requires finite, non-black frames plus a mean delta of at
  least `0.20` and at least 5% of the consumer ROI changing by more than 2
  levels. The ROI excludes the HUD and the lower mirror slab.
- **Six-face audit**: with `GX_CUBEMAP_FACE_DEBUG=1`, the cubemap pass writes
  six unique colors and reads back the center of every face. The suite checks
  the expected target/up vectors, `TEXTURE_CUBE_MAP_SEAMLESS`, repeated face
  updates, and a visible response when the reflective cube rotates. This is
  an orientation/attachment/seam-state diagnostic, not production content.

## The x-flip sampler convention (formally verified)

The planar pass renders UNREFLECTED geometry from the MIRRORED camera
(`LookAt(reflect(pos), reflect(tgt), reflect(up))`). gxmath's LookAt
re-orthogonalizes to a right-handed frame whose right axis is the NEGATED
main-camera right axis (`cross((0,-1,0), yflip v) = -cross((0,1,0), v)`).
The RT therefore stores the ideal mirror image **horizontally flipped**;
the floor shader compensates:

```glsl
vec2 suv = vec2(1.0 - gl_FragCoord.x / resolution.x,
                gl_FragCoord.y / resolution.y);
```

Formally proven over exact rationals in gameX `lean/GameXProofs/PlanarReflection.lean`:
x negates, y/z preserve exactly, and clip-w of unreflected X equals clip-w
of R(X) from the main camera (above-plane geometry can never fall behind
the virtual camera).

## gxmath rotation convention (oracle-critical)

`gX_Matrix_RotateXYZ` stores the Rz·Ry·Rx layout row-major; transforms use
ROW vectors (`p @ M`), so the ACTIVE rotation is the transpose — angles
negate in effect (+30° yaw acts as −30°). Any analytic prediction of model
silhouettes must apply `(Rz @ Ry @ Rx).T`. Isolation captures with
`GX_CUBE_ROT=0|Y|Z` verify conventions against the engine within 1px.

## Verification

```bash
# full fidelity table + gates (needs GPU display)
DISPLAY=:1 make reflect-check

# single capture trace
DISPLAY=:1 env GX_DIAG_CUBE=1 ./build/gX_build --showcase-planar \
    --ai-screenshot-final
python3 scripts/probe_reflect_verify.py --trace <png> --yaw 45 --pitch 25
```

- The oracle uses fixed projection dimensions, so the suite preserves the
  engine-sized viewport rather than the smaller default audit window. Captures
  run serially by default: concurrent full-resolution GL contexts caused the
  planar capture to exit 139 on this GPU.
- Oracle: luminance-aware magenta detection + exact mirrored-hull convex
  zone (`scripts/probe_reflect_verify.py --trace`).
- The current three-angle sweep measures SSR `45.82–58.43%` and planar
  `55.41–57.61%`; these are strict diagnostic-cube hull scores, not a claim
  that every visible reflection pixel must be magenta.
- The suite reports a calibrated A-F grade for every row and uses the worst
  row as its overall grade. Default bands are SSR A/B/C/D at `45/35/25/20`,
  planar at `50/40/35/30`, and water at `95/90/85/80` percent. Cubemap uses
  ROI mean/coverage bands of A `0.60/10%`, B `0.40/8%`, C `0.30/6%`, and D
  `0.20/5%`. The face audit uses A/B/C bands of minimum face count
  `60/20/3`, colored ROI fraction `0.08/0.05/0.02`, and rotation delta
  `6.0/3.0/1.0`. A row below its configured floor, or with missing/invalid/black
  captures, receives F and the suite exits nonzero. The configured floors
  remain planar ≥30%, SSR ≥20% per angle, water ≥80% non-black, and the direct
  cubemap ROI checks above.
- Always pin `DISPLAY=:1` (NVIDIA); `:2` is software and renders no models.

## History note

Two consecutive "engine bugs" (v0.1.34 failure ring, v0.1.35 wedge) were
verification-oracle defects: inflated AABB+margin zones, absolute
thresholds missing dimmed reflections, and the rotation-convention error
above. Rule: before hunting engine bugs in a mismatch, validate the
predictor with zero/single-axis isolation captures.

## Temporal convergence (TAA + oracle jitter)

- TAA source fix (`taa.c`): NULL `compositeTarget` resolves to the canvas,
  not pre-composite `hdrScene` — TAA previously ate SSR output.
- Static-pixel clamp bypass (`gfx_taa_resolve.frag`): still pixels skip the
  neighborhood clamp; churn 11.0→1.31/frame, near the oracle ceiling.
- E7 velocity fix: explicit `layout(location)` 0/4/8/12/16 in
  `gfx_velocity.vert/frag` — the dense linker bases overlapped and smeared
  motion; spin captures now bit-identical run-to-run.
- Oracle jitter (Paper 1): coherent per-frame phase/lobe shift converges in
  ~10 TAA frames. Tested alternatives all lost and were fully reverted:
  fwidth weight (checkerboard), static jitter (grain), hit-LOD, thickness
  min-run, and IGN spatiotemporal noise (E8: lower churn 1.23 but coarser
  residual grain hf 3.55 vs ~2.75; coherent beats incoherent at TAA
  timescales). Rule: single-sample SSR wants temporal coherence, not
  spatial decorrelation.
- Root cause of residual faceting: single-sample march-step quantization at
  grazing silhouettes (inter-bracket jumps bisection cannot fix); step
  0.0625 helps at ~37% cost. Production answer is TAA convergence, not
  finer marching.

## Known limits

- SSR structural ceiling: far faces absent from screen space (~74%).
- Planar requires flat mirrors; curved surfaces use SSR/cubemap/probes,
  or the non-planar analytic path (`GX_NONPLANAR_TEST`, suite-gated).
- Reflection color mixes at `bias+0.45`; raise for truer mirrors,
  lower for artistic blending (standard.frag planar branch).
- Vision-noted ghost band near contact region and plane scanlines are
  scene lighting/shadow features, not reflection-path defects.
