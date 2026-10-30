<!-- //ai: proof-first SSR coverage debug slice; no optional camera implementation or renderer-default change. -->
# SSR coverage and optional second camera

## Scope

This is an engine-only proof/debug slice, not Source-map parity work. Rendering
defaults, assets and author presets are unchanged. The existing auxiliary view
is a **mirrored plane camera**, not an arbitrary second camera or ray tracer.

## What Lean 4 establishes

`GxProofs/SsrCoverage.lean` (separate proof repository, not included) is in the blocking core gate:

- Current auxiliary routes exclude water and require the auxiliary-view flag.
- A miss with no crossings, grazes or phantom rejections keeps its environment
  result. Zero crossings alone does **not** establish a clean miss.
- A front-side geometry hit away from the seam keeps its march result.
- Stale planar captures cannot satisfy the C binding gate.
- A selected usable baked probe overrides the dynamic SSR environment. Without
  one, dynamic selection requires freshness and a nonzero resource; otherwise
  the shader uses its sky path. This does not establish sky-LUT readiness.
- An available but empty depth mask rejects the sample. **An unavailable mask
  currently permits it**; this is a retained legacy counterexample, not a repair.

`GxProofsGfx/Ssr/SsrRayCoverage.lean` (separate proof repository, not included) is in the Mathlib gate:

- A ray/plane intersection is on the plane and unique when its denominator is
  nonzero. A parallel off-plane ray has no intersection; a negative parameter
  cannot be a forward hit.
- An on-plane receiver is fixed by the global reflection transform.
- An exact off-plane witness disproves general mirror-camera ray coverage,
  **even when the local and global reflection normals agree**.

For the witness, the mirror is `y=0`, eye `(0,2,0)`, receiver `(1,1,0)`.
Both the true reflection and auxiliary lookup have direction `(1,1,0)`, but
their origins differ. The receiver ray reaches `(2,2,0)`; the auxiliary image
ray from mirrored eye `(0,-2,0)` cannot reach that point for **any** parameter.
Matching directions, fresh textures and nonempty image pixels do not fix this
parallax error. This is ideal geometry, not a measured GPU artifact.

The Boolean inputs abbreviate actual shader comparisons (crossCount>0,
grazeCount>0, phantomRejects>0, hitType==1, dnWrong>0.05, plane membership and
margin<0.35). The core model does not prove those comparisons or march accuracy.
`scripts/test_ssr_coverage_contract.py` pins selected C/GLSL expressions and has
mutation/comment negatives. It is a source-drift guard, **not** a translator
proof or shader/GPU correctness certificate.

## Optional-camera design contract (not implemented)

The new core module also proves a deliberately separate `optionalFallback`
specification:

1. A valid screen-space reflection wins.
2. Otherwise use a valid analytic scene hit, then a valid cubemap contribution.
3. Only if those cannot supply the contribution may an **enabled**, validated
   second camera fill the gap.
4. Otherwise omit the extra contribution, retaining the material's existing
   lighting rather than painting an unverified image.

`analyticValid` means a usable scene-reflection contribution, not merely the
procedural sky/sun function. `cubemapValid` is contribution-level evidence, not
just a texture ID: local parallax and visibility remain obligations. This order
is **not** the current renderer, whose optional auxiliary branch can replace
wrong-side or seam SSR hits after environment accumulation, even with a cubemap.

Proposed second-camera eligibility requires all of:

- current-frame freshness and available color/depth resources;
- positive clip w and in-frame projection;
- an actual reflection-ray hit, validated against the secondary depth field.

The final condition is essential: projecting an origin or closest-approach
point into an unrelated camera is not an intersection or visibility test.
Future implementation must additionally own capture matrix/extent/depth
conventions, tolerance/error margins, state restoration, lifecycle and
feedback exclusion; test occlusion, disocclusion, motion and disabled-path
identity in owned fixtures. A single camera cannot cover every hidden surface.
No new pass, settings toggle or texture unit is introduced by this slice.

## Reproduction and limits

<!-- //ai: generated first-hit fixture; initial observations retained before the optional-normal precision repair. -->
### Fixed-camera gap fixture

```sh
python3 scripts/test_ssr_gap_check.py
DISPLAY=:1 python3 scripts/ssr_gap_check.py
```

The CPU tests also run in `make check`; the GPU experiment is opt-in. It writes
three generated GLBs, private presets, raw HDR/depth/SSR readbacks, logs and a
JSON report into a fresh directory under `/tmp/opencode/ssr_gap_check/`. An
explicit `--output` must be empty and outside the repository. No shipped assets,
author presets, camera fitting, grading or runtime shader edits are used.

The camera is at `(0,3,5)`, looks at the origin and has vertical FOV 45 degrees
at 800x600. A flat receiver spans `y=0`, `x,z=-5..5`; the red emissive box spans
`(-0.7,3.3,-2.7)..(0.7,4.7,-1.3)` and is entirely outside the primary view.
The optional green occluder spans `(-0.8,1.8,-0.25)..(0.8,2.6,0.25)`.
Ideal CPU slab intersections select the first reflected hit, including
occlusion. Ground depth is independently compared with native readback using
the logged near/far frustum, without fitting the projection.

The experiment separates baseline, plane-mirror capture, direct legacy cube,
the existing default-off cube correction, SSR without environment, SSR with
environment, and hit-class diagnostics with/without environment. Auxiliary
SSR is disabled to avoid confusing the sources. The reflective receiver uses
PBR to carry its requested sharp roughness; Phong carries a fixed roughness
instead. Reflection strength is `reflection.bias`, not a per-model
`reflectionBias` field. The fixtures use supported schema fields only.

The planar control must recover at least 99% of the three-pixel-eroded red and
green masks and must not paint red beyond a three-pixel boundary allowance or
inside the eroded occluder mask. The primary source must remain offscreen;
native ground depth must agree within `2e-6`; an all-zero hit-class diagnostic
cannot count as successful execution. These are **fixture control gates**, not
a certificate for arbitrary reflection coverage. Cubemap visibility errors
remain reported observations rather than acceptable visibility routing.
The HDR dump is the main material buffer, not a claim about the final presented
SSR composite; SSR contribution metrics use the native SSR target directly.
Direct-cube results retain absolute color metrics and separately report the
material-buffer difference from the reflection-disabled baseline. Neutral
lighting can conceal a weak colored reflection from the absolute dominant-color
test; failure of that predicate does not establish zero reflected radiance.

Initial supported-fixture measurements recovered all 17,176 red pixels without
the occluder, and all 5,914 red plus 16,998 green pixels with it through the
planar path, with no false red. The native ground depth error was at most
`1.621981429e-7`. SSR-only supplied no red source contribution; the red-source
interior had no screen-space geometry hit. Cubemap-assisted SSR recovered
99.12% / 88.30% of the red interiors, but painted 1,715 / 1,735 false red ground
pixels (occluder off/on). These observations do not validate the cube sample's
reflection-ray visibility: 268 red pixels lay inside the eroded occluder mask,
away from its boundary. Neither direct-cube variant passed the absolute
dominant-red test; the corrected variant nevertheless changed the red channel.
Its contribution is measured separately by baseline subtraction, not certified
as a valid reflection-ray hit. Remaining content/visibility diagnosis is open.

The planar path therefore covers this particular flat-receiver gap accurately:
there is no evidence here requiring another camera. The initial fixture slice
adopted no runtime fix; the later optional-normal repair is described below.
No default change or second-camera implementation is adopted. Off-plane receivers
remain subject to the parallax counterexample above. Original setup trials and
the rejected sampler-alias hypothesis are retained privately rather than being
used as evidence for a shader repair.

<!-- //ai: actual native buffer and unfitted ray/capture controls isolate UNORM8 normal bias. -->
### Optional-normal precision repair

The occluded-pixel investigation compared all six native 512x512 cubemap faces
with the declared mirrored capture origin `(0,-3,5)` and ideal first-hit rays:
red and green texel classifications matched exactly. Sampling those faces with
the exact reflection rays produced no red inside the eroded occluder mask,
using nearest, bilinear, or the fixture's fractional mip level. Disabling the
optional normal buffer also removed that interior leak. Filtering and capture
origin were therefore not sufficient explanations for this particular error.

Native float inspection of the old RGBA8 normal target showed packed ground
normals `(127/255,1,127/255,1)`, not `(0.5,1,0.5,1)`. Decode produces nonzero
X/Z components `-1/255`; normalization retains the tilt and reflection amplifies
it. The resulting GPU rays differed from the ideal rays by up to 0.69 degrees.
Reflecting with the measured quantized normal accounts for that direction
offset within the half-float diagnostic's encoding allowance. The exact
rational CPU witness establishes that no 8-bit UNORM code decodes to zero under
`2*packed-1`; it does not certify a GPU or general visibility.

`ssr.normalMap` now uses RGBA16F, which represents packed `0.5` exactly. This
changes only the existing opt-in target's storage; its point filter, coverage,
depth ownership, resize/unload lifecycle, sampler unit and shader encoding are
unchanged. The normals-disabled production default still allocates no such
target. The enabled color payload doubles from four to eight bytes per pixel;
this is an opt-in GPU-memory cost, not a new camera pass. The historical
`GXNRM1` byte dump remains a compatibility conversion and cannot establish
native storage precision; the RT inspector's float readback can.

```sh
python3 scripts/test_ssr_normal_precision.py
DISPLAY=:1 python3 scripts/ssr_normal_precision_check.py
# Optional explicit snapshot comparison; the script never builds or rewrites it:
DISPLAY=:1 python3 scripts/ssr_normal_precision_check.py --before-binary /path/to/old/gX_build
```

The regression requires exact flat-normal components within `1e-6`, encoded
reflection-ray error at most `0.0015`, at least 99% unoccluded red-interior
coverage, and **zero** red inside the eroded occluder mask. With an explicit old
binary it also compares the normals-disabled SSR target and main material
buffer bit-for-bit. Without one, identity is reported as not run, not passed.
Source/precision/occlusion negative controls run in `make check`; GPU captures
are opt-in and external. This repair does not validate arbitrary off-plane
cube visibility, border filtering, rough reflection footprints or camera
coverage, and does not promote the existing direct-cube correction.

The owned old/new-binary regression measured 268 → 0 red pixels inside the
eroded green mask, 88.30% → 100% red-interior coverage, and exact packed flat
normals after the repair. Maximum encoded-ray component error fell from
`0.01121234` to `0.00097767`, within the predeclared half-float diagnostic
allowance. Normals-disabled SSR and main material buffers were bit-identical.
There remain 580 false red ground pixels near the color boundary, but none
beyond the existing three-pixel boundary allowance. They are retained filtering
residuals, not reclassified as exact visibility.

<!-- //ai: finite sampling-footprint investigation; point-oracle counts and runtime visibility rules unchanged. -->
### Boundary-footprint diagnostic

```sh
python3 scripts/test_ssr_boundary_footprint.py
DISPLAY=:1 python3 scripts/ssr_boundary_footprint_check.py
```

The CPU tests run in `make check`; the GPU replay is opt-in. It regenerates the
same owned fixed-camera fixture and retains native cube faces, material alpha,
SSR contributions and direct environment samples outside the repository. The
point-oracle color predicate, three-pixel allowance and renderer settings are
unchanged; there is no new camera, sampling mode or contribution-validity gate.

In the fresh corrected-normal capture, all 580 red point-oracle disagreements
had red-bearing base texels in the positive-weight filter support. No such
pixel lay outside that support or in a face-crossing stencil, and no red
remained in the eroded occluder interior. Nearest base-level sampling still
had 250 red point-oracle disagreements: a finite cubemap texel's raster sample
is not the same ray as every query mapped to it. The model therefore records
both finite capture discretization and bilinear mixing, not just blur width.

The declared replay is bilinear L0 plus bilinear L1 at the native material
alpha's fractional LOD (`0.00299835205078125`). L1 uses an explicit 2x2
arithmetic box-reduction hypothesis. Each active coarse texel's support expands
to its four base texels; coarse dominant-color classification is **not** a
support test. The boundary samples match the model's dominant-red class, with
maximum raw RGB difference about `0.004064`. This is observed agreement, not
a bound on every driver's mip generation or filter arithmetic. Face ties and
crossing stencils stay uncertain rather than being certified by clamping.

The entire-frame RGB replay is deliberately incomplete: `envColorAt` also adds
an analytic sun term, separate from the cube color. Whole-frame differences
are reported rather than hidden or fitted away. The exact-rational CPU control
checks a finite corpus of convex-combination ceiling witnesses: normalized
nonnegative mixtures stay below the ceiling when every participating base
sample is below it. This is not a new Lean theorem. Native first-hit controls
and the pixel/tap ledger connect that
limited statement to this fixture; they do not prove general receiver-ray
visibility, subtexel occlusion, rough-lobe integration or seamless cube edges.

**The point-oracle disagreement count remains 580.** Filter support is evidence
about where color came from, not a reason to mark those center rays visible or
promote a cubemap fallback. No further runtime repair is adopted by this slice.

<!-- //ai: owned curved-normal triage retains failed frozen checks; no default or runtime repair. -->
### Curved-normal triage (2026-10-14)

The existing advisory OFF/ON result reproduced at its unchanged camera and
1024x768 resolution: planar/cube color scores `0.654/0.618` became
`0.483/0.612`. Native primary depth was byte-identical. Of 52 lost planar-zone
magenta pixels, 44 retained a first geometry hit and eight switched from geometry
to environment fallback; six other pixels gained magenta. The cube zone had
60 losses and 56 gains across mixed hit classes. First-hit classification alone
therefore does not explain the color regression. These remain advisory color
counts, not independent certificates of the sampled geometry or radiance.

A separate external, owned fixture uses 512 outward octasphere triangles with
identical positions under smooth and face-normal variants. CPU ray/triangle
intersections and barycentric normal interpolation are frozen before rendering;
the primary oracle is the actual float32 mesh, not an ideal sphere intersection.
Native depth error was at most `2.127e-7`. Smooth packed normals differed by at
most `0.000496`; the normal-on ray's mean angular error was `0.077°`, versus
`5.947°` with depth normals. However, the frozen `0.0015` component allowance
against ideal smooth rays **failed** (maximum `0.002798`, 13,155 interior pixels
outside the allowance). It is not relaxed or reported as a passing quality gate.

Replaying reflection from the actual native normal/depth inputs separately
matched the encoded rays within `0.000978`. Four face-normal outliers lie within
`0.000213` screen pixels of shared triangle edges and match the adjacent face's
normal; the original failures remain recorded rather than changing edge masks.
This evidence distinguishes input/readback precision and raster edge ownership
from a general shader orientation defect. It does not establish arbitrary
curved-surface cubemap visibility, or make more accurate normals a guarantee of
better composed reflections. No runtime repair, new camera or normal-default
promotion follows from this triage; promotion remains **HOLD**.

<!-- //ai: readout-only accepted-hit attribution; retain advisory mask/score and the remaining uninvestigated losses. -->
The follow-up fixed the cohort to the 44 lost planar-zone pixels retaining
geometry hits in both arms. Readout-only branches in a private shader copy
exposed accepted positions and post-resnap lookup UVs; the repository shader
was unchanged. All eight OFF/ON controls (normal output, hit class, first-hit
color and candidate residual) were entire-file byte-identical to the original.

All 44 primary receivers are on the sphere and all 88 accepted lookups address
the floor, also the first geometry along the decoded lifted rays. OFF/ON lookup
locations move by a mean 114 screen pixels (maximum 354); post-resnap ray/depth
world gaps stay below `0.012`. This is floor-image lookup displacement, not
evidence of a wrong-object hit or a large phantom gap in this cohort.

Crucially, none of these 44 pixels is in the advisory's undilated sampled zone.
Independent pixel-center checks with the ideal sphere and the actual mesh's
interpolated normals both predict **no** cube lookup under the same conditional
mirror-camera/plane-image contract. The decoded normals-off rays instead predict
28 unlifted or 35 lifted cube lookups; normals-on predicts zero for both, matching
the absence of magenta first-hit color. Normals-on rays differ from the exact
mesh-normal direction by a mean `0.092°`, versus `4.747°` off. The three-pixel
dilation had rewarded nearby color spill; losing it is not a demonstrated
center-ray accuracy regression for these 44 pixels. The original scores, mask
and graduation HOLD remain unchanged. Later-bounce/composite color and general
curved visibility remain separate work;
this attribution does not justify a runtime repair or another camera.

<!-- //ai: fixed remaining eight geometry-to-exit switches are expected viewport misses, not rejected visible hits. -->
The remaining eight geometry-to-environment switches were traced separately.
All normals-on rays still intersect the floor, but outside the primary camera's
1024x768 frame; exact mesh-normal rays agree on that coverage limit. Native
readouts show viewport-margin exits before those intersections, with **zero**
crossings, confirmation rejections or phantom rejections in all eight traces.
Eight entire-file OFF/ON identity controls again match the original shader;
the repository shader, camera and acceptance thresholds are unchanged.

These eight pixels are also outside the undilated sampled zone. Both ideal
sphere and actual mesh-normal pixel-center checks predict no cube lookup under
the same conditional plane-image contract. Thus all 52 lost planar-zone magenta
pixels in this fixed advisory comparison lie in its three-pixel fringe; the
44 accepted floor-image shifts and eight expected viewport exits do not establish
a center-ray accuracy regression. The advisory scores and HOLD are retained,
not replaced with a more favorable metric. Correct SSR termination does not
certify the environment contribution's visibility or any optional second-camera
fallback. No runtime repair follows from these two cohorts.

```sh
python3 scripts/test_ssr_coverage_contract.py
make proofs
make proofs-full
DISPLAY=:1 make reflect-check
```

The normal admission gates still apply (`make check`, reflection, feature
matrix, bloom and GL-state audits). Lean proves conditional exact arithmetic and
Boolean contracts; it does not prove GPU completion, floating-point error,
source texture validity, scene coverage, radiance, temporal stability or image
quality. Existing reflection thresholds and renderer policy are unchanged.
