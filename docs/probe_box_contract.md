<!-- //ai: exact-rational reflection box contract and diagnostic limits. -->
# Probe box-projection contract

Run the CPU fixtures with:

```sh
python3 scripts/test_probe_box_contract.py
```

This is a mathematical contract with an opt-in shader candidate, not an
admitted production repair or GPU/IEEE certification. No budgets are changed.

## Inside-box ray

Let `P` be the surface position, `C` the box/probe center, `H` positive box
half-extents, and `D` a nonzero **outgoing world-space ray**. Require
`abs(P[i] - C[i]) <= H[i]` on every axis.

For every nonzero component of `D`, compute the forward exit time:

```text
t[i] = (C[i] + sign(D[i]) * H[i] - P[i]) / D[i]
tExit = min(t[i])
hit = P + tExit * D
lookup = hit - captureCenter
```

Parallel axes impose no exit-time candidate: do not replace a zero component
with an epsilon. `lookup` is a world-space cubemap direction, not divided by
`H`. Box center and capture center may differ; the fixtures cover both
coincident and distinct centers. Normalize only if needed; cubemap
sampling accepts non-unit directions.

A boundary-origin outward ray exits at time zero; an inward ray reaches the
opposite boundary; a tangent ray follows the boundary to its next exit.
The proposed fallback for outside-box origins is the unprojected outgoing
direction, even if that ray enters the box. A zero `hit - captureCenter` also
falls back to the outgoing direction. These are CPU reference policies only;
default production shading has not adopted them.

## Counterexample to the production approximation

At `P = C = (0,0,0)`, `H = (50,12,50)`, `D = (1,2,0)`, the exact exit is
`t = 6`, `hit = (6,12,0)`. The production expression
`(P-C + D*H/max(abs(D),0.001))/H` instead gives `(1,1,0)`.
These directions are not parallel, even when the surface is at the capture
center. The expression scales each ray component independently rather than
using one intersection time. This counterexample does not depend on a GPU.

The fixtures compare 702 strictly interior rays against an independent
face-enumeration oracle using exact rational arithmetic. Additional fixtures
cover parallel/near-parallel axes, boundary origins, corners, translation,
positive direction rescaling, and rejection of undefined inputs.

## Evidence and remaining work

The opt-in pixel trace at `(609,142)` measured 24–29 degree differences
between current and ray/AABB directions. Sampling the existing cubemaps at
the latter directions reduced that pixel's summed absolute RGB difference
by about 72%. This is local diagnostic evidence, not a full-frame passing
probe gate. Trace inputs/outputs in `/tmp/opencode/probe_trace/report.json`
are temporary and are not required by the fixtures.

The diagnostic helper's epsilon treatment
is not the exact parallel-axis contract above. RGBA16F readbacks lose precision,
especially near zero; they must not be treated as exact shader inputs.

Lean 4 could prove containment and boundary attainment for the selected exit
time over reals. Such a proof would not certify GLSL floats, sampling, or
capture-center bindings; the exact counterexample already establishes that
the current approximation is not general ray/AABB projection.

## Ray sign and capture ownership review

`standard.frag:calc_positions` defines the view vector as surface-to-camera.
GLSL's outgoing reflection is therefore `reflect(-viewDir, normal)`, not the
current direct-cubemap `reflect(viewDir, normal)`. With `V=(1,2,3)` and
`N=(0,1,0)`, the outgoing result is `(-1,2,-3)`; the current result is its
opposite. Flipping the normal does not compensate for this sign error.
The mapped-normal branch computes in tangent space and transforms the ray
back through `transpose(matTNB)`; that transformation does not remove the
need to negate `V`. The non-planar intersection branch already negates it.

`src/renderer/cubemap.c:gX_Cubemap_RenderEx` captures each GL face looking
outward from the supplied position (`target = position + face direction`).
There is no face-target inversion compensating for the direct lookup sign.

Probe ownership is consistent: `draw.c` passes `probe->pos` to the bake;
`model.c` uploads the selected probe's same position to `cubePos`/`cubePosB`.
The dynamic cubemap is different: `draw.c` picks the **first loaded reflective
model's** world plane and captures its mirrored camera, while `model.c`
recomputes a center from **each consuming model's** plane. Two planes at
`y=0` and `y=1` mirror camera `(2,4,5)` to `(2,-4,5)` and `(2,-2,5)`;
they cannot both describe the same captured texture. This is a latent
multi-plane ownership bug, not a measured explanation of the original
single-plane highlight residual.

For a correction, persist the dynamic capture's actual position alongside
the texture and use it for every dynamic consumer. Keep the existing
`(50,12,50)` half-extents and per-capture-centered virtual boxes initially:
these approximate the scene, not authored shared room bounds. Changing to
one shared scene box requires a separate authored-bound contract. Probe
selection and blending weights need no change. Use an inside-box helper and
the fallback above, with the outgoing ray for primary and secondary lookups
(direct and planar fallback), preserving mirror-RT projection and confidence
blending. Validate the full image with unchanged budgets: the earlier 72%
diagnostic improvement used the old ray sign and does not predict the
corrected shader's gate result.

## Opt-in candidate (not admitted)

`GX_PROBE_BOX_CORRECT=1` enables outgoing-ray sign, one ray/AABB exit time,
outside-box fallback, and actual dynamic capture-origin binding. Unset or
invalid values retain the legacy default. Capture calls record the dynamic
origin in `reflection.dynamicCapturePosition`; probe positions and blend
weights are unchanged. The shader uses the existing per-center half-extents.

The first full-image candidate run **failed** the unchanged probe budgets:
near mean channel difference `3.1451`, maximum `131`, blend mean `0.6766`;
refresh and repeat remained exact. Reflection face-color correspondence
fell to `0.000` (grade F). Thus the candidate is deliberately not the default.
The geometric contract does not certify captured content or existing pixel
fixtures. Before admission, diagnose the broader content differences and
face-color expectation with independent asymmetric evidence; do not change
budgets or expected faces merely to make this candidate pass.

## Candidate failure diagnosis

The unchanged `reflect_suite.py:cubemap_face_case` color metric uses
`maxChannel - secondLargestChannel > 12`. The capture palette contains red,
green, blue, yellow, magenta, and cyan: the latter three are chromatic but
have equal largest channels and fail this predicate. Independent captures
under `/tmp/opencode/probe_face_diagnosis/` recorded:

| Metric | Legacy | Candidate |
|---|---:|---:|
| Six-face attachment readback | pass | pass |
| Seamless enabled | yes | yes |
| Max minus second-largest fraction | 0.4525 | 0.0000 |
| Max minus minimum fraction (>12) | 0.4537 | 0.4544 |
| Cube-rotation mean pixel delta | 10.972 | 11.618 |

This explains the color-metric failure, not complete physical fidelity.
No grading predicate, palette, expected face, or threshold was changed.
The CPU fixtures retain this metric counterexample.

Cubemap-content inspector captures under
`/tmp/opencode/probe_content_diagnosis/` compare the same equirectangular
directions from dynamic capture and probes 1/3. At probe size 512 (matching
dynamic size), the screenshot content below the HUD is pixel-identical.
At size 128, mean absolute channel difference is 0.073835 on the 0..255
scale, with 0.1846% of pixels exceeding mean difference 10. Probes 1 and 3
give the same comparison. These are LDR inspector measurements, not proofs
that the HDR textures or every direction are identical.

In this fixture the floor and cube are both marked reflective and excluded
from `gfx_cubemap_draw_face`. The procedural sky is rendered with camera
position zero and each face's world orientation; it is direction-only.
Applying a finite per-capture box to such content introduces artificial
positional parallax. The CPU fixture shows that two centers return different
lookups for the same surface and ray, although a direction-only environment
must not depend on capture position. Thus the exact intersection fixes one
geometric error but does not establish that finite box projection belongs
on the captured sky at all.

Next separate finite local reflection from infinite sky lookup. A production
solution needs either separate sky/local terms or distance/validity evidence
for the cubemap sample; blindly changing box bounds or disabling projection
for all local geometry would change the contract. The candidate remains
opt-in until independently checked and admitted by the unchanged gates.

## Capture-level sky-only bypass

The candidate now records whether any loaded, non-reflective model was
submitted across the six capture callbacks. Reflective models remain excluded
as before. `dynamicSkyOnly` and each probe's `skyOnly` are owned by their
captures and uploaded by selected texture identity. Classification is
conservative: a submitted model counts as local even if it is outside the
frustum or contributes no visible fragments. No new textures or units are
allocated. An unbaked probe is not classified as sky-only.

With `GX_PROBE_BOX_CORRECT=1`, a sky-only capture uses the outgoing world ray
without box projection. Mixed captures retain finite-box projection and
require a separate per-sample coverage solution. The default path ignores
the new flags and keeps legacy projection.

The same seven candidate probe captures now satisfy the existing budgets:
near mean `0.014866`, fraction over 2 `0.001745`, maximum `12`; far fraction
over 2 `0.001286`; blend, refresh, and repeat differences all zero.
Measurements are in `/tmp/opencode/probe_sky_candidate/report.json`.
The candidate reflection suite still grades F solely on the face-color
metric (`colored_fraction=0.000`); face attachment readbacks pass, cubemap
response grades S. Consequently the correction remains opt-in, not admitted.

Release size remains under the unchanged cap, with 85 bytes headroom at this
worktree state. To stay within that budget, the optional trace uploads query
locations directly and the temporary pinned-probe position log was removed;
`GX_PROBE_DEBUG_INDEX` still selects the same probe. No user-authored settings,
art, gate thresholds, or payload cap were changed.

## Independent face and mixed-capture witnesses

The sky-only candidate was traced with `GX_CUBEMAP_FACE_DEBUG=1` and an
unrotated cube. At pixel `(609,142)`, world position readback is approximately
`(0.402588,1.5,-0.470215)`, on the cube's horizontal top. Reconstructing the
camera from the fixed orbit `(yaw=30,pitch=25,distance=6)` about the origin
and reflecting its incident vector about `(0,1,0)` predicts the shader's
outgoing direction within `0.052` degrees. The shader's sample direction is
approximately `(-0.401864,0.178715,-0.898090)`: standard GL selection chooses
face 5, **-Z**, whose debug palette color is cyan. Raw HDR sample RGB is
exactly `(0,1,1)`. Report: `/tmp/opencode/probe_face_correspondence/report.json`.
This is one non-seam correspondence witness, not a six-face physical-fidelity
certificate. Do not infer the main camera by mirroring a capture center:
capture ownership and viewer position are separate contracts.

A temporary editor preset used only `resources/model/primitive/cube.glb`:
a reflective slab plus an unreflective emissive magenta cube at `(8,3,-3)`.
Comparing sky-only and mixed captures at equal 512px cube resolution produced
3,329 magenta pixels in the dynamic inspector and 13,777 in probe 1, in the
same UI-excluded viewport. Changed-pixel fractions (mean difference >10)
were approximately `0.010643` and `0.043769`, respectively. The emitter is
therefore present in both capture types, rather than silently skipped.
Evidence and temporary presets: `/tmp/opencode/probe_mixed_capture/`.

These measurements deliberately exclude editor panels: comparing whole
screenshots would confound captured content with model-list and preset-path
text changes. They establish local-content visibility, not ray/surface
intersection accuracy. The conservative capture-level classifier sends such
mixed captures down the finite-box path, including their sky pixels. Per-sample
sky/local separation remains unresolved; no grading rule was changed and
the candidate is still opt-in.

## Capture-only coverage experiment

`GX_PROBE_COVERAGE_EXPERIMENT` compile-isolates diagnostic C hooks. Configure
an isolated build with:

```sh
cmake -S . -B build/probe-coverage -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-DGX_PROBE_COVERAGE_EXPERIMENT=1
cmake --build build/probe-coverage -j4
```

In that binary only, `GX_PROBE_COVERAGE=1` marks procedural sky/clear pixels
with cubemap alpha zero and standard-shader geometry with alpha one. The
shader uniforms are reset after their capture draws; main-scene alpha retains
its SSR roughness contract. Sky uses overwrite blending during capture so
zero alpha does not suppress sky RGB. This does **not** change reflection
selection. `GX_PROBE_COVERAGE_DIR` enables float RGBA face dumps on frames
30–33 to an existing scratch directory. Normal builds contain no dump hooks.

For the temporary axis-aligned cube fixture above, all 48 mixed face dumps
matched an independent pixel-center ray/AABB reference exactly: **46,452
local pixels, zero disagreements**. All 48 sky-only faces had alpha zero;
all dump values were finite. Main-scene HDR captures with the marker off/on
had maximum RGB difference **0** and maximum alpha difference **0**.
Evidence: `/tmp/opencode/probe_coverage/report.json`. Reproduce the offline
oracle checks with:

```sh
python3 scripts/test_probe_coverage_check.py
python3 scripts/probe_coverage_check.py /tmp/opencode/probe_coverage/sky
python3 scripts/probe_coverage_check.py /tmp/opencode/probe_coverage/mixed \
  --center 8 3 -3 --half-extents 1.2 1.2 1.2
```

The marker is validated only for base-level opaque standard-shader geometry
and procedural sky in this fixture, not other shaders or transparency. A CPU
2x2 box-filter estimate already yields **419 fractional mip-1 pixels** across
the mixed dumps; this is not a GPU mip readback. Filtered alpha becomes
coverage, not a binary label, and filtered RGB mixes sky/local contributions.
Moreover, the unprojected and projected lookups can disagree on coverage.
Consequently a threshold-based switch at one lookup is not yet a justified
mixed-reflection contract. Next experiments must distinguish both lookup
coverages and test silhouettes/disocclusion independently; separate sky/local
resources or per-sample distance may still be needed. No coverage-driven
reflection sampling has been enabled.

## Dual-lookup coverage and distance ambiguity

`scripts/probe_lookup_coverage_check.py` sweeps a 401x401 world-space grid
on the temporary reflective slab's top `(y=0.1,x/z in [-8,8])`. This is not
a screen-visible-pixel count: camera clipping and occlusion are not applied.
Outgoing rays reflect the incident direction from camera `(4,4,6)` about
the slab normal. Each ray has three independently evaluated labels:

1. Local geometry along the outgoing direction **from the capture origin**.
2. Local geometry along the candidate box-projected direction from that origin.
3. Actual emitter intersection along the outgoing direction **from the receiver**.

The exact continuous-direction reference has 3,678 actual local hits among
160,801 receiver samples. Taking the union of labels 1 and 2 still gives:

| Capture origin | False local hits | Missed actual local hits |
| --- | ---: | ---: |
| Dynamic `(4,-4,6)` | 118 | 207 |
| Probe `(-3,1.5,-3)` | 0 | 3,678 |
| Probe `(3,1.5,-3)` | 700 | 3,678 |
| Probe `(-3,1.5,3)` | 0 | 3,678 |
| Probe `(3,1.5,3)` | 732 | 3,638 |

These are projection/reference disagreements, not marker readback errors.
All nearest base-level GPU dump samples match independent ray/AABB tests
at the selected texel's center, with zero disagreements. Repeating probe
captures at 512px instead of 128px leaves the continuous-direction table
unchanged. Nearest-texel discretization disagreements for projected directions
drop from 68 to 0 at `(3,1.5,-3)` and from 75 to 8 at `(3,1.5,3)`; they are
separate from the much larger projection mismatch. Dynamic captures already
use 512px and retain 62 outgoing / 72 projected discretization disagreements.
Evidence: `/tmp/opencode/probe_coverage/dual_lookup{,512}.json`.

Exact-rational tests in `scripts/test_probe_lookup_coverage_check.py` pin
nonzero directions and the following dynamic-capture witnesses:

- Receiver `(168/25,1/10,-1)`: both capture lookups say sky, but its actual
  outgoing ray intersects the emitter.
- Receiver `(27/5,1/10,11/5)`: both capture lookups say local, but its actual
  outgoing ray misses the emitter.

There is also an information-loss witness independent of this fixture.
A capture at the origin sees identical binary angular coverage for a cube
centered at `(1,0,2)` with half-extents `(1/5,1/5,1/5)` and its 4x-scaled
counterpart centered at `(4,0,8)`. A receiver at `(1,0,0)` looking along `+Z`
hits the near cube but misses the far cube. Thus even the complete binary
coverage cubemap cannot generally recover translated receiver intersections:
distance is missing. Two coverage samples cannot repair that ambiguity.

Separate sky/local resources can prevent filtered RGB contamination and
preserve direction-only sky lookup, but do not alone establish local
parallax correctness. A justified next experiment needs local distance (or
an independently specified local geometry proxy), with silhouette and
disocclusion fallbacks. Finite-box projection remains an approximation for
objects not lying on the box walls. No dual-lookup switching rule was added
to the shader, and the candidate remains opt-in.

```sh
python3 scripts/test_probe_lookup_coverage_check.py
python3 scripts/probe_lookup_coverage_check.py /tmp/opencode/probe_coverage/mixed
python3 scripts/probe_lookup_coverage_check.py /tmp/opencode/probe_coverage/mixed512
```

## First-hit local-distance upper bound

`scripts/probe_distance_check.py` uses analytic first-hit distances for the
same opaque emitter, not GPU distance readbacks. It first computes actual
receiver intersections, then queries capture distance **at the known true hit
direction** and reconstructs the world point. This deliberately measures an
upper bound on available information; it does not discover a valid lookup
direction and must not be presented as a reflection algorithm.

| Capture origin | Ideal reconstructible hits | Hidden hits | Missing visible hits at simulated 128px / 512px |
| --- | ---: | ---: | ---: |
| Dynamic `(4,-4,6)` | 3,678 | 0 | 67 / 0 |
| Probe `(-3,1.5,-3)` | 2,262 | 1,416 | 1,764 / 39 |
| Probe `(3,1.5,-3)` | 2,262 | 1,416 | 54 / 17 |
| Probe `(-3,1.5,3)` | 3,678 | 0 | 1,799 / 97 |
| Probe `(3,1.5,3)` | 3,678 | 0 | 626 / 178 |

Visible ideal hits reconstruct within `1.2e-14` world units. Distance therefore
resolves the homothetic near/far ambiguity which binary coverage cannot. But
two probes retain 1,416 hidden hits even with continuous, exact depth: their
first visible surface is not the surface seen by the receiver. For example,
receiver `(6.08,0.1,1.32)` hits `(7.466667,2.7,-1.8)`, while probe `(3,1.5,-3)`
returns approximately `(6.8,2.520896,-1.979104)` on the emitter's nearer face,
an error of about `0.713163` world units. Single-layer capture depth cannot
reconstruct the hidden point from that direction.

The simulated nearest-texel depth maps are sampled at the true hit direction
too. They still miss thin silhouettes and introduce reconstruction error:
the 512px dynamic case has mean error about `0.017946` and maximum about
`0.097609` world units despite having no missing visible samples. Error means
are conditional on a non-missing visible sample; do not compare conditional
means across resolutions as a universal improvement claim. None of these
figures validates GPU distance storage, filtering, rough reflections, an
iterative search, or screen-visible fidelity. Reports:
`/tmp/opencode/probe_coverage/distance{128,512}.json`.

Six self-contained tests cover radial-distance reconstruction, inside-origin
exit handling, parallel misses, the near/far distance distinction, a hidden
back-face counterexample, and 104 independent exact-rational face-intersection
comparisons. The next implementation experiment must validate actual capture
distance storage before adding a bounded lookup search, and explicitly reject
uncertain/disoccluded local samples rather than silently claiming a correct
sky hit. Separate sky/local RGB remains a separate filtering concern.

```sh
python3 scripts/test_probe_distance_check.py
python3 scripts/probe_distance_check.py --size 128
python3 scripts/probe_distance_check.py --size 512
```

## GPU capture-only radial distance

The isolated `build/probe-coverage` binary now accepts
`GX_PROBE_COVERAGE=2`. Sky/clear alpha remains zero; opaque standard-shader
geometry writes `length(fs_in.fragPosition - viewPosition)` into RGBA16F
cubemap alpha. Both inputs are world-space, avoiding tangent-space normal-map
coordinates. Geometry uses overwrite blending in this mode so distances above
one survive. The capture uniform and blend mode are reset afterward. No
reflection consumer samples this distance yet. Normal builds contain no C
activation/readback hooks; their shader uniforms remain zero.

Distance dumps have magic `GXDVF32\0`, distinct from binary-coverage dumps,
and otherwise share the face/origin/float-RGBA layout. The offline checker
`scripts/probe_distance_capture_check.py` rejects the wrong dump type and
compares base-level distance against the known opaque AABB intersection.

| Run | Faces | Local pixels | Exact nearest-half reference matches | One-half-step differences | Larger differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sky-only | 48 | 0 | 0 | 0 | 0 |
| Mixed, 128px probes | 48 | 46,452 | 23,392 | 23,060 | 0 |
| Mixed, 512px probes | 48 | 100,776 | 50,903 | 49,873 | 0 |

Dynamic capture remains 512px in both mixed runs. All coverage labels match
the reference, sky distance is exactly zero, and all readbacks are finite,
nonnegative and representable as half floats. Maximum absolute radial errors
are approximately `0.007957` and `0.008055` world units. These are storage
errors, not the much larger directional/disocclusion errors above. One
representable half step is the diagnostic comparison bound, accounting for
float arithmetic/conversion; this does not change any reflection gate.
The GPU values are not all identical to CPU nearest-half rounding.

Main HDR frame comparisons with mode 0 versus mode 1 and mode 2 both yield
maximum RGB difference **0** and alpha difference **0**, with finite values
throughout. Evidence: `/tmp/opencode/probe_distance_gpu/`, including
`isolation.json`, `sky_report.json`, and `mixed{128,512}_report.json`.

This validates only base-level, opaque standard-shader radial-distance
storage for the fixed fixture. It does not validate GPU mips, transparent
capture surfaces, other shaders, distance filtering or a lookup algorithm.
Next is an offline bounded search over the GPU distance dumps with explicit
uncertain/disoccluded outcomes; a search failure must not masquerade as proof
that a ray hits sky.

```sh
python3 scripts/test_probe_distance_capture_check.py
python3 scripts/probe_distance_capture_check.py /tmp/opencode/probe_distance_gpu/sky
python3 scripts/probe_distance_capture_check.py /tmp/opencode/probe_distance_gpu/mixed128 \
  --center 8 3 -3 --half-extents 1.2 1.2 1.2
python3 scripts/probe_distance_capture_check.py /tmp/opencode/probe_distance_gpu/mixed512 \
  --center 8 3 -3 --half-extents 1.2 1.2 1.2
```

## Bounded offline distance search

`scripts/probe_distance_search.py` searches the actual GPU distance dumps
without using a known true hit to choose the lookup direction. It marches
the receiver's normalized outgoing ray up to 32 world units, evaluating
`length(point-captureOrigin) - capturedDistance(direction)` at nearest
base-level texels. A candidate requires adjacent local samples bracketing
negative-to-nonnegative residual, eight bisection refinements, and a final
residual within one upward representable half-float step of sampled distance.
These are diagnostic search parameters, not modified reflection thresholds.

Missing brackets, initially being behind the first layer, sky-to-behind
coverage jumps, refinement through sky, and excessive residual have distinct
**uncertain** outcomes. None is classified as a verified sky hit. Candidate
status also does not certify a local hit: the independent receiver-ray/AABB
reference is evaluated afterward, not used by the search.

The first complete cube per capture origin is used from the mixed GPU dumps.
On the same 401x401 slab grid, with 3,678 true intersections per capture,
the 512px dynamic distance map produces:

| March steps | True intersections with a candidate | False local candidates | True hits left uncertain |
| --- | ---: | ---: | ---: |
| 32 | 3,615 | 23 | 63 |
| 128 | 3,648 | 28 | 30 |
| 512 | 3,664 | 28 | 14 |

At 128 march steps, the four 512px probes find only 457, 484, 1,008 and
1,634 true intersections respectively (origins `(-3,1.5,-3)`, `(3,1.5,-3)`,
`(-3,1.5,3)`, `(3,1.5,3)`). The first has one false local candidate, the
others zero at that budget. Increasing the budget to 512 steps introduces
additional probe false candidates; more marching is not a correctness proof.
Reducing probe resolution to 128px also increases false-candidate and timing
errors. Dynamic resolution stays 512px in both capture sets.

For the 128-step dynamic run, accepted true-hit distance errors average
approximately `0.012545` world units and reach `0.087999`. Its 28 false
candidate points lie within `0.017488` world units of the emitter but their
actual rays miss it. A small radial residual therefore does not certify
silhouette membership. The search uses at most `1+steps+refinements+1` queries
per ray; the maximum observed at the 128-step budget is 129.

Eight self-contained tests include a genuine 4px analytic depth-map control:
an emitter at `(0.75,0.75,3)` with half-extents `0.2` generates first-hit
distances, quantized to half floats. A receiver at `(0,0,1)` looking along
`(0.2,0.25,1)` misses the emitter, but the nearest-texel radial search returns
a local candidate. This pins sampling-footprint ambiguity independently of
the GPU fixture and prevents calling candidate status a certified hit.

Reports: `/tmp/opencode/probe_distance_gpu/search512_{32,128,512}steps.json`
and `search128_128steps.json`. These are offline, nearest-base-level search
results, not runtime shader sampling or reflection admission. Next work must
establish a conservative angular-footprint/silhouette uncertainty contract
and preserve disocclusion fallbacks. Do not promote this search or tune its
acceptance bound to hide the false candidates.

```sh
python3 scripts/test_probe_distance_search.py
python3 scripts/probe_distance_search.py /tmp/opencode/probe_distance_gpu/mixed512
python3 scripts/probe_distance_search.py /tmp/opencode/probe_distance_gpu/mixed512 --steps 512
```

## Discrete silhouette guard and its certification limit

`scripts/probe_footprint_check.py` applies a fixed 3x3 local-coverage stencil
to each bounded-search candidate. Any sky texel in that neighborhood produces
`uncertain_silhouette`. Texels at a face boundary produce
`uncertain_face_seam`; the diagnostic does not clamp a partial stencil and
pretend it is complete. A full local stencil retains only an
`interior_candidate`, not a certified intersection. Observed depth spans are
reported without smoothing or an acceptance cutoff. The guard does not
consult receiver geometry, the reference intersections, or true hit directions.

On the 401x401 slab sweep, with 512px captures and 128 march steps:

| Capture origin | Retained true candidates | Retained false candidates | True candidates reclassified uncertain |
| --- | ---: | ---: | ---: |
| Dynamic `(4,-4,6)` | 3,476 | 0 | 172 |
| Probe `(-3,1.5,-3)` | 447 | 0 | 10 |
| Probe `(3,1.5,-3)` | 484 | 0 | 0 |
| Probe `(-3,1.5,3)` | 987 | 0 | 21 |
| Probe `(3,1.5,3)` | 1,614 | 0 | 20 |

All 28 false dynamic candidates and the one false probe candidate are rejected
in this fixture. Dynamic true hits left uncertain total 202, including 30
already unresolved by search. At 512 march steps the guard retains 3,480 true
dynamic candidates and rejects every false candidate in all five captures.
With 128px probes and 128 march steps it likewise rejects all observed false
candidates, but leaves more true probe hits uncertain. None of these results
justifies admitting the guard as conservative **geometric** coverage.

Six tests include a separate counterexample. Compare a solid panel with
bounds `x/y in [-3,3], z in [2,21/10]` against two panels leaving a slit
`x in (-1/32,1/32)`. A 4px-per-face cubemap at the origin has **identical
first-hit distances at all 96 texel-center rays** for both scenes. This is
also checked with exact-rational face intersections, before quantization.
Receiver `(1/64,0,1)` looking along `+Z` hits the solid panel but passes
through the slit. The quantized distance search returns a local candidate
and its 3x3 stencil is fully local in both scenes. The guard therefore retains
a false candidate in the slit scene, even though it rejected all false
candidates in the mixed-cube fixture.

This is an information-loss limit, not a stencil-size tuning request. An
entire finite point-sampled distance map can miss subtexel holes, so nearby
texel agreement cannot certify continuous silhouette membership without an
additional geometry/coverage assumption. The guard is conservative only
about incomplete **sampled** neighborhoods and face seams. It does not fix
capture-hidden surfaces or certify sky on search failure.

Reports: `/tmp/opencode/probe_distance_gpu/footprint512_{128,512}steps.json`
and `footprint128_128steps.json`. Runtime sampling remains unchanged. A next
experiment should state an explicit local-geometry proxy contract (or use
actual geometric ray queries), independently preserve direction-only sky,
and retain uncertainty where the proxy cannot represent the geometry. Do not
promote the stencil merely because this fixture has zero retained false hits.

```sh
python3 scripts/test_probe_footprint_check.py
python3 scripts/probe_footprint_check.py /tmp/opencode/probe_distance_gpu/mixed512
python3 scripts/probe_footprint_check.py /tmp/opencode/probe_distance_gpu/mixed512 --steps 512
```

## Explicit exact-solid proxy contract

`scripts/probe_proxy_contract.py` tests an explicitly authored
`solid_aabb` proxy for the fixture's opaque cube, with `exact: true` and an
explicit scene-completeness assertion. It does **not** promote arbitrary mesh
bounds to solid geometry, infer geometry from capture depth, or reuse the
large reflection influence box as an object intersection proxy. The exact
fixture proxy is center `(8,3,-3)`, half-extents `(1.2,1.2,1.2)`. The reflective
slab is excluded from capture, so the cube represents all local capture
geometry for this test. These declarations are assumptions, not an automatic
verification of a user-authored scene.

Three contracts remain separate:

1. **World geometry:** intersect the receiver ray with all authored solids,
   keep the nearest object-owned hit, and form its capture lookup as
   `hitPoint - actualCaptureOrigin`. A complete exact scene may classify a
   resolved miss as `sky_direction`, preserving the outgoing direction. If
   completeness is false or any proxy is approximate, both hits and misses
   remain `uncertain_scene_contract`; an unknown object might occlude a hit.
2. **Capture visibility:** independently intersect the ray from the actual
   capture origin toward that world hit. First-hit object ownership must also
   agree; a nearer first surface makes the
   hit `uncertain_capture_hidden`. A zero lookup remains uncertain and falls
   back to the outgoing direction, never a zero-vector texture sample.
3. **Sampled representation:** separately inspect GPU texels and the prior
   discrete footprint guard. Neither geometry nor visibility alone certifies
   reflected RGB, filtering, roughness or a texture's subtexel coverage.

Initial floating slab/triangle comparisons had 13 grazing label disagreements.
The proxy now outward-rounds each plane subtraction and division with
`nextafter`, enclosing intersections for the stored floating inputs.
Ambiguous hit/miss intervals remain `uncertain_boundary` rather than changing
the triangle reference tolerance or treating them as definite sky. Capture
grazing ambiguity also has a separate uncertain outcome.

The 401x401 receiver sweep now has 3,676 resolved local intersections,
157,110 resolved sky directions and 15 uncertain world boundaries for each
capture. An independent 12-triangle reference agrees with **every resolved
label**; maximum resolved intersection-distance difference is approximately
`2.7e-15` world units. These are geometry comparisons, not a replacement for
the reflection suite. At 512px, capture representation remains:

| Capture origin | Visible resolved hits | Hidden resolved hits | Uncertain capture boundaries | Visible hits selecting sky texels |
| --- | ---: | ---: | ---: | ---: |
| Dynamic `(4,-4,6)` | 3,676 | 0 | 0 | 0 |
| Probe `(-3,1.5,-3)` | 2,261 | 1,414 | 1 | 38 |
| Probe `(3,1.5,-3)` | 2,261 | 1,414 | 1 | 17 |
| Probe `(-3,1.5,3)` | 3,676 | 0 | 0 | 95 |
| Probe `(3,1.5,3)` | 3,676 | 0 | 0 | 178 |

Eleven self-contained tests cover actual hit-point/capture ownership, nearest
object selection, direction-only sky, incomplete/approximate scenes, hidden
surfaces, zero lookups, input validation and 78 exact-rational face-reference
comparisons. Rational edge hits remain uncertain where floating interval
classification cannot resolve them. Explicitly authoring the two slit panels
correctly gives sky for the slit ray and a local hit for the solid panel,
unlike their indistinguishable point-sampled distance maps.

Evidence: `/tmp/opencode/probe_distance_gpu/proxy{128,512}.json`. No renderer
or shader integration was added. The contract is scoped to declared exact
opaque solids; extending it to arbitrary meshes requires another geometry
contract or actual geometric queries. Next integration experiments must keep
world classification, capture visibility and sampled color separate, and must
not substitute sky merely because local color is unavailable. The opt-in box
candidate and all reflection thresholds remain unchanged.

Visibility distance agreement accounts for 64 machine-epsilon-scaled floating
roundoff; this diagnostic is not a formal real-number visibility proof for
arbitrary nearly coincident surfaces. Other-object first hits never pass just
because their distances fall within that roundoff band.

```sh
python3 scripts/test_probe_proxy_contract.py
python3 scripts/probe_proxy_contract.py /tmp/opencode/probe_distance_gpu/mixed512
python3 scripts/probe_proxy_contract.py /tmp/opencode/probe_distance_gpu/mixed128
```

## Isolated GPU proxy diagnostics

The compile-isolated `GX_PROBE_COVERAGE_EXPERIMENT` build now also accepts
`GX_PROBE_PROXY_FIXTURE=1` plus `GX_PROBE_PROXY_DIAGNOSTIC=1..9`. The first
hook is an explicit assertion of the fixed exact-solid fixture, not an
automatic mesh/schema check. Normal C builds cannot activate the diagnostic;
its shader uniform defaults to zero. The implementation does **not** replace
reflection sampling or reflected RGB. When deliberately activated it replaces
the reflective slab's final HDR output with diagnostic data, marked by alpha
seven; capture draws remain independent of this main-view diagnostic.

Modes expose world position (1), world outgoing ray (2), geometry/visibility/
hit distance (3), actual primary capture origin (4), normalized capture lookup
(5), world normal (6), and nine direct ray/AABB controls (7). Geometry codes
are sky/miss `0`, local hit `1`, boundary or invalid-ray uncertainty `2`.
Visibility codes are not evaluated `0`, visible `1`, hidden `2`, uncertain `3`.
Float32 plane/division intervals use adjacent representable values, and a
zero ray is uncertain rather than sky. Capture lookup uses the computed world
hit and actual texture-owned `cubePos`; normal mapping is evaluated in world
space for this diagnostic, not through tangent-space `Positions` fields.

The original temporary camera framing did not show any slab points whose
reflected rays hit the emitter. Those negative-control captures are preserved
under `/tmp/opencode/probe_proxy_gpu/`. The revised temporary preset changes
only camera **target** to `(5.8,0.1,0.6)`, retaining camera position `(4,4,6)`,
the same objects, and the same dynamic/probe capture origins. It is saved as
`/tmp/opencode/probe_proxy_gpu/framed.json`; no shipped preset was edited.

Final-source captures under `framed/` contain 575,985 diagnostic slab pixels:
517,842 GPU sky labels and 58,143 local labels, identical across all five
primary capture selections. Dynamic and the two z=+3 probes classify all
local points as capture-visible. Probe 0 `(−3,1.5,−3)` classifies 28,802 as
visible and 29,341 as hidden; probe 1 `(3,1.5,−3)` classifies 28,801 as visible
and 29,342 as hidden. No captured RGB is substituted for hidden points.

`scripts/probe_proxy_gpu_check.py` compares position, outgoing ray, hit time,
capture ownership, normalized lookup and visibility, enclosing neighboring
half-float values because HDR readback is RGBA16F. It finds zero resolved
conflicts. **675 world-label cases remain reference-ambiguous at that readback
precision**, and 674 local visibility cases per selection are likewise
reference-ambiguous. They are explicitly reported, not counted as exact
agreement. This checker validates only the framed slab/solid-box fixture;
it does not infer scene completeness from pixels.

Nine direct GPU controls cover forward and backward rays, parallel outside
misses, inside-origin exits, zero-ray uncertainty, a definite oblique miss,
parallel grazing, boundary-away uncertainty, and a nonunit ray's parameter
distance. All status and half-readback time checks pass. Self-contained
reader/source-contract tests also pin activation isolation and readback bounds.
Evidence: `/tmp/opencode/probe_proxy_gpu/framed/report.json`.

Requesting diagnostic mode 7 **without** the fixture assertion produces no
diagnostic pixels and has maximum RGB and alpha difference zero versus mode
off, with finite HDR values. Evidence: `activation_isolation.json` in the same
scratch root. The fixed-fixture GLSL visibility comparison uses a float32
roundoff band; this is not a general arbitrary-mesh visibility certificate.
Sampled color, silhouettes, roughness, filtering and disocclusion fallback
remain separate integration work. The original opt-in box candidate and all
reflection grades/thresholds are unchanged.

```sh
python3 scripts/test_probe_proxy_gpu_check.py
python3 scripts/probe_proxy_gpu_check.py /tmp/opencode/probe_proxy_gpu/framed
```

## GPU sampled-color availability

Diagnostic modes 8 and 9 extend the same isolated path without replacing
reflected RGB: mode 8 exposes raw base-level cubemap RGB at the computed
lookup, and mode 9 exposes sampled cubemap alpha plus geometry/visibility
codes. These captures enable `GX_PROBE_COVERAGE=2` so alpha means radial
capture distance (sky zero), not ordinary capture alpha. The existing sampler
uses filtered base-level lookup, so a positive distance response proves some
local capture contribution, not an unmixed local pixel, full coverage or the
correct surface color. A zero response likewise is not a universal geometric
sky certificate at finite texture/readback precision.

The reproducible capture helper validates the fixed two-owned-cube transforms,
reflection flags and framed camera before asserting the proxy fixture. It
uses the shared hidden-window/settings-preserving audit helpers and refuses
to overwrite existing HDR captures. Readbacks are stored as verified lossless
`.hdr.gz` archives; the checker accepts both raw and compressed HDR files.
This avoids exhausting the scratch quota on repeated large float readbacks.
The first uncompressed color run exhausted quota; its interrupted artifacts
are archived under `color/` and are **not** the validation dataset. The complete
run is under `/tmp/opencode/probe_proxy_gpu/color2/`, with report
`/tmp/opencode/probe_proxy_gpu/color2_report.json`.

The complete run retains the previous geometry/lookup/visibility checks:
575,985 diagnostic pixels, 58,143 local labels, zero resolved interval conflicts,
and 675 readback-ambiguous world classifications. Sample availability gives:

| Capture | GPU sky labels with nonzero local-distance response | Visible local labels with zero response | Hidden local labels with nonzero response |
| --- | ---: | ---: | ---: |
| Dynamic | 3,443 | 0 | 0 |
| Probe 0 | 0 | 0 | 29,341 |
| Probe 1 | 0 | 0 | 29,342 |
| Probe 2 | 0 | 0 | 0 |
| Probe 3 | 0 | 0 | 0 |

For the dynamic capture, **3,181** of those nonzero responses lie on sky rays
whose world classification is also resolved by the readback-aware reference.
Thus even correctly leaving sky rays unprojected does not recover pure sky
from a mixed capture: local geometry from the capture's direction can occupy
that lookup. The other 262 are not claimed as reference-resolved evidence.
All visible local labels have some sampled local contribution, but filtered
distance/RGB can still blend sky at silhouettes. The nonzero responses at
hidden local points do not certify the receiver-hit surface's color; a nearer
captured surface remains a different surface, even if this emitter's uniform
magenta makes their colors look similar.

This establishes two separate remaining requirements: a direction-only sky
resource independent of local occlusion, and an explicit fallback for local
hits whose surface the chosen capture cannot see. A positive capture response
cannot satisfy the latter by itself. The eight reader/fixture/availability
tests cover these distinctions; no reflection threshold, blend weight or
normal sampling path was promoted or changed.

```sh
python3 scripts/test_probe_proxy_gpu_check.py
python3 scripts/probe_proxy_gpu_check.py /tmp/opencode/probe_proxy_gpu/color2
# Capture to a NEW scratch directory; the framed preset is an explicit test fixture.
python3 scripts/probe_proxy_gpu_check.py /tmp/opencode/probe_proxy_gpu/recheck \
  --capture --preset /tmp/opencode/probe_proxy_gpu/framed.json
```

## Isolated separate sky/local resources

<!-- //ai: resource routing remains diagnostic-only; local RGB is not certified. -->

`GX_PROBE_PROXY_SPLIT=1`, the fixture assertion and coverage mode `2` now
enable a separate-resource experiment in the compile-isolated build. The
validated fixture requires SSR **off**. Its existing managed `ssr.skyLut`
render target is allocated at 1024x512 and refreshed before main model drawing,
independent of SSR. It contains direction-only procedural sky, with no local
geometry. The experimental cubemaps skip sky rendering and retain local RGB
and radial-distance alpha over black clear. Normal builds and production
reflection sampling are unchanged; this is not a mixed-capture RGB repair.

The LUT uses the existing equirect convention (+X at u=0, +Y at v=0), horizontal
repeat and vertical edge clamp. Raw target-correct 2D binding uses the existing
sky-LUT unit 23, with the shader activated before uniform uploads. Resource
allocation/unloading stays with the managed SSR target; this is not an
additional unmanaged cubemap or texture-unit reservation. The experiment does
not support simultaneous SSR or nonprocedural sky.

Additional slab HDR diagnostic modes are:

| Mode | Output |
| --- | --- |
| 10 | Separate sky LUT RGB along the outgoing direction |
| 11 | Diagnostic routed RGB, or black for explicitly unavailable results |
| 12 | Source, unavailability reason, world-geometry code |
| 13 | Six cardinal and two asymmetric encoded world directions |
| 14 | LUT samples for those same direction controls |

Source codes: `0` unavailable, `1` independent sky resource, `2` sampled
local **candidate**. Reason codes: `0` resource selected, `1` missing sky
resource, `2` hidden/uncertain capture visibility, `3` uncertain world geometry,
`4` visible geometry with no positive sampled local-distance response.
Black here is a diagnostic marker paired with the reason code, **not** an
approved production material fallback. In particular, hidden local hits do
not become sky merely because their surface RGB is unavailable.

GPU capture visibility now additionally requires unique interval-resolved
receiver/capture box faces and matching face ownership. Distance agreement
alone cannot admit opposite faces; ambiguous face assignment stays uncertain.
This remains a fixed exact-box diagnostic, not arbitrary-mesh visibility.

The pre-face-guard `split2/` dataset routes all 517,842 GPU sky-labelled pixels
through the independent LUT, with **zero RGB difference across all five
capture selections** and zero source/reason/geometry-code conflicts. Probe
0/1 leave 29,341/29,342 hidden local pixels unavailable, with exactly zero
routed RGB. Geometry readback still has 675 ambiguous reference cases, not
certified sky/hit labels. Visible local samples are candidates only: black-clear
filtering at silhouettes and roughness remain unresolved. The face-guard
revision is being recaptured separately under `split3/`; do not attribute the
earlier measurements to that revision. The final face-guard `split3/` run now
passes: sky counts and zero routing/cross-capture RGB differences are unchanged.
Probe 0/1 each retain 28,800 local candidates and mark 29,343 local hits
unavailable, rejecting two/one additional wrong-face candidates respectively.
Resolved geometry/time/visibility/lookup conflicts remain zero; the 675 world
and 674 per-capture local-visibility readback ambiguities remain reported.

An independent encoded-direction LUT control (`sky_direction/`) checks all
six axes and `(1,2,3)` / `(1,2,-3)`. There are zero direction conflicts; maximum
angular error is 0.175813 degrees at the poles. The comparison bound derives
from LUT angular spacing and half-value quantization, not a changed reflection
quality threshold. The direction override is reset before other sky draws.
A separate missing-resource GPU control (`missing_sky/report.json`) leaves all
517,842 sky-labelled pixels unavailable with reason `1`, zero source/reason
conflicts and zero routed RGB; it does not consult a mixed cubemap as fallback.

Reports are under `/tmp/opencode/probe_proxy_gpu/`: `split2_report.json`,
`sky_direction_report.json` and `missing_sky/report.json`. All are diagnostic
evidence, not reflection admission. The prior mode-8/9 gates completed:
`make check`, default reflection A-, bloom (22 checks) and GL-state (0 errors,
13 known warnings) pass; feature matrix still fails `mirror`, `particles`,
`probes`, and opt-in candidate reflection remains F. Protected hashes are
unchanged. Final split/face-guard validation under `split_validation/` has also
completed: isolated build, all 72 CPU tests, fresh capture/routing report,
`make check`, default reflection A-, bloom (22 checks) and GL-state (0 errors,
13 known warnings) pass. Feature matrix retains the same three failures and
candidate reflection remains F (`faces` color metric `0.000`). Protected hashes
are unchanged and Release payload remains 3,906,491 bytes (85 bytes headroom).
This does not admit the candidate or certify local sampled RGB.

```sh
python3 scripts/test_probe_split_check.py
# Always choose a NEW capture directory; explicit fixed fixture, SSR off.
python3 scripts/probe_proxy_gpu_check.py /tmp/opencode/probe_proxy_gpu/split_recheck \
  --capture --split --preset /tmp/opencode/probe_proxy_gpu/framed.json
python3 scripts/probe_split_check.py /tmp/opencode/probe_proxy_gpu/split_recheck
python3 scripts/probe_split_check.py /tmp/opencode/probe_proxy_gpu/sky_direction --direction
```
