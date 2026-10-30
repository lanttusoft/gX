<!-- //ai: distinguish proof checking, trusted assumptions, gate coverage, and source correspondence. -->
# Lean 4 proof audit

> The Lean proof sources are published separately at
> https://github.com/lanttusoft/gx-proofs. They are not included in this
> source tarball or linked into the engine executable. `make proofs` and
> `make proofs-full` expect a sibling `../proofs` checkout.

## What the gates establish

`make proofs` builds the core tier. `make proofs-full` builds the Mathlib gfx
and gxmath tiers; requesting it without the pinned cache is an error, not a
successful skip. The proof tree is published in the separate
[gX proofs](https://github.com/lanttusoft/gx-proofs) repository; it is never
part of the engine release payload. Both run `scripts/proof_audit.py`, which checks:

- Every project module in the selected tiers is reachable from its root and
  present in the compiled environment.
- Project theorem dependencies contain only `propext`, `Classical.choice`, and
  `Quot.sound`, with no project axioms, `sorryAx`, or compiler-trust axioms.
- Finite witnesses are kernel-checked rather than delegated to `native_decide`.

The audit includes generated theorem declarations. Its count is not the number
of independently authored engine properties. For a complete JSON ledger:

```bash
python3 scripts/proof_audit.py --full --report /tmp/opencode/lean-proof-audit.json
```

## Current corpus inventory

[`proof_inventory.md`](proof_inventory.md) is generated from the Lean sources
by `make proof-inventory` and checked by `make proof-inventory-check`. It maps
every module to its namespace and counts authored axioms, definitions,
structures, inductives, instances, lemmas and theorems. Lean has no
`corollary` keyword; corollaries are `theorem` or `lemma` declarations.

At this inventory the corpus has 110 modules: 49 core, 43 Mathlib-GFX and 18
GxMath. The static source count is 1,320 `theorem` declarations plus 2
`lemma` declarations, 702 `def`, 59 `structure`, 17 `inductive`, 11
`instance`, 5 `abbrev`, and zero project `axiom`/`opaque` declarations. The
kernel audit reports 110 reachable modules and 2,572 theorem declarations
including generated helpers, with standard axioms only. The small-matrix
module adds 12 contracts for the 2×2/3×3 determinant/trace helpers; the F64v4
module adds 4 lerp contracts; `GxMath.Quaternion` covers algebra/norm/inverse,
rotation and branch-aware Lerp/Nlerp/Slerp contracts. Native vector action is
`q * pure(v) * conj(q)`, with
length scaled by `normSq(q)` off the unit sphere; exact unit quaternions
preserve length. Inverse action, antipodal sign invariance and composition
(apply `b` then `a` for `mul a b`) are proved. The existing engine matrix
transform agrees only under an exact-unit hypothesis; the general difference
is `(1 - normSq(q)) * v`, and a nonunit counterexample is explicit. No matrix
convention is changed. IEEE normalization/inverse conditioning and GPU upload
remain outside the rotation claims. Interpolation models the zero-normalizer
fallback and exact-real Slerp branches (with `0.95f` idealized as `19/20`);
regular Slerp norm preservation assumes unit endpoints and the exact dot/cosine
relation. The antipodal Nlerp midpoint is explicitly zero. The Slerp
small-sine midpoint fallback is modeled and proved, and is unreachable in the
regular interval for the idealized `1/1,000,000` epsilon because the half-sine
is at least `sqrt(39/400)`. IEEE branch-boundary, trigonometric and
normalization accuracy remain outside the claims.
`GxMath.Sdf` has 15 value/sign/linearity,
sphere/unit-normal plane Lipschitz and clearance contracts. Six metric helpers
in `GxMath.Vector` supply Cauchy-Schwarz and triangle/reverse-triangle bounds.
Plane unit length is an explicit hypothesis, not a C normalization accuracy
claim; clearance does not certify a float/GPU raymarcher. Other SDF Lipschitz
bounds remain missing. `GxMath.Scalar` adds 14 clamp/lerp/
normalize/remap bounds, endpoints and round trips; `GxMath.Fourier` has 19
contracts, including explicit Fin/ZMod index/character bridges, both inverse
round trips, adjoint/pairing identities and Parseval for nonzero length.
The forward exponential has negative sign and no normalization; inverse uses
positive sign and `1/n`. This is an exact-complex specification, not a proof
of the radix-2 C implementation or IEEE accuracy; no native inverse FFT is
asserted. `GxMath.Calculus` has 23 contracts: exact finite differences, one
affine Newton step, and 15 Simpson additions. The native parity-weighted
endpoint/loop model is bridged to paired panels, telescoped through a cubic
primitive, and linked to Mathlib's signed interval integral for every cubic
and positive even step count. Zero-step, one-step trapezoidal, degenerate
interval and odd-step counterexamples remain explicit. These real-number
identities assume a pure nonnull callback and do not certify IEEE arithmetic,
machine-index casts or general quadrature/Newton convergence.
`GxMath.Color` adds 17 per-channel contracts for signed truncation/clamp,
lerp endpoints, normalization and normalized multiplication; `GxMath.Units`
adds 12 basic length/temperature round trips and reference values. These are
exact-real/explicit-quantization models, not IEEE accuracy proofs. Lerp's C
correspondence requires finite, representable casts and I32 addition; ideal
normalization round trips do not guarantee float byte round trips. The
source-drift guard `scripts/test_color_units_contract.py` runs in both
`math-check` and `proofs-full`, alongside the existing numeric oracles.
`GxMath.Sky` adds 14 Cartesian sky-frame contracts: both inverse round trips,
squared-length/length preservation in all four directions, and orientation.
Ecliptic obliquity is a parameter (C supplies `23.4f * gX_DEG2RAD`); horizontal
latitude/longitude are parameters in both directions. Ecliptic orientation
is +1, while the native horizontal frame is orthogonal with orientation -1.
The source guard `scripts/test_sky_contract.py` pins that convention without
changing C; no IEEE trig/degree-constant accuracy or angular-coordinate branch
claim is made. Zero and nonunit Cartesian vectors are allowed by the proofs.
`GxMath.Coordinates` adds 24 cylindrical/spherical contracts, with both
Cartesian round-trip directions, direct interconversion, principal/wrapped
azimuth, radial/inclination helpers, and zero-radius/pole ambiguity results.
Spherical Cartesian reconstruction requires positive total radius; full
spherical tuple recovery also excludes poles and restricts azimuth to
`(-π, π]`. The model uses `Complex.arg` for ideal `atan2(y,x)`; signed-zero C
branch behavior and IEEE exceptions are not certified. See the domain table
in [`math_pass.md`](math_pass.md) for the precise assumptions and source guard.
`MatrixBridge` records conversions between the flat field, clip and logical
matrix models; the shared matrix namespace is split into `MatrixTuple` and
`MatrixField`. The generic clip
model and cofactor/inverse algebra now live in `GxMath.ClipMatrix` and
`GxMath.CofactorInverse`; `GfxCofactorInverse` retains the camera-history
bridge.

The [gX proofs repository](https://github.com/lanttusoft/gx-proofs)
maintains its significance scale and reviewed importance catalog (target
evidence levels, source/guard/gate paths and known gaps). Neither file is
included in this source beta. The catalog derives `checked`/`grounded`/`guarded`/`complete`
labels from those declarations; they are static evidence labels, not
execution certificates.

## Proof certificates

`make proof-certify PROOF_CERT_TIER=<tier>` writes a hashed artifact under
`certificates/proofs/`:

| Tier | Runs |
|---|---|
| `catalog` | inventory/root/static-level check only |
| `kernel` | catalog + `make proofs-full` + a full audit report |
| `guarded` | kernel + every unique native/source gate declared in `IMPORTANCE.json` |

The artifact records the commit, manifest/catalog/audit hashes, gate results,
reviewed modules and their target/achieved levels, reviewed gaps, and
unreviewed-module count. The verdict is:

- `CERTIFIED` — gates/audit pass and no reviewed importance gap remains
- `GATED` — gates/audit pass but reviewed gaps remain
- `NOT CERTIFIED` — a gate or audit failed

`make proofs` also runs `scripts/test_proof_source_contract.py`, which pins
the maindata switch/moduleInfo branches, refresh/reset defaults, FBO
lifecycle, RHI blend mappings and temporal-history lifecycle against the
current C sources. This is a source-drift guard, not a translation proof.

`proof-certify` is a release-ritual artifact, not an admission gate. The
levels are static evidence labels; the kernel audit remains the authority on
transitive axioms and the C/Lean correspondence remains unproved.

## Findings from the existing corpus

<!-- //ai: current SSR routing vs optional-camera design are distinct proof claims. -->
`SsrCoverage.lean` pins optional auxiliary routing, clean-miss/water exclusion,
freshness and the permissive missing-mask counterexample. Its optional-camera
selection is explicitly a design specification, not current production routing.
`Ssr/SsrRayCoverage.lean` proves conditional analytic ray/plane intersection
validity and an off-plane parallax witness: equal lookup/reflection directions
do not imply the auxiliary image sees the actual hit. The selected source
clauses have mutation-tested drift guards; GPU visibility remains unproved.
See [`ssr_coverage_contract.md`](ssr_coverage_contract.md).

<!-- //ai: glass proofs record a current routing defect, not a certified renderer repair. -->
`GfxGlassCapture` records the current opacity-one routing counterexample:
refractive models could enter the opaque pass before the refraction-background
copy. The native repair uses a shared post-capture routing predicate and
phase-local capture readiness; its validation is tracked separately. The module proves
conditional distinct-target/current-frame/current-camera obligations, plus
ideal feedback recurrence algebra. A separate counterexample shows that an
alpha-blended background draw retains history when HDR roughness alpha is used
as coverage; the repaired copy uses a color framebuffer blit. Native routing, finite-float predicates,
capture completion and texture/camera correspondence must still be tested.
These theorems neither repair the engine nor certify moving-camera image quality.

The initial corpus contained 71 modules. Nineteen were unreachable from the
configured gate roots. Explicitly building those modules exposed obsolete
Mathlib imports in `ClipMatrix` and `GfxTextwriterLayout`; both imports are
updated for the existing pin. All modules are now included in their roots.

The original compiled corpus had 48 theorem declarations, including generated
helpers, depending on `Lean.ofReduceBool` and `Lean.trustCompiler`. Their finite
`native_decide` witnesses are replaced with kernel-checked `decide`.

Focused source review found these specification problems:

| Model | Repair or explicit limit |
|---|---|
| `RhiFbo` | Distinguish raw cached fields from public sentinel-clamping getters and the resolved query. Add coherent-cache contracts for attachment-inspection draw restoration and read preservation. |
| `EffectSources` | Match `Pass_HasEffects`, including motion blur, DOF, particles, and SSAO-dependent SSGI; add missing-consumer witnesses. |
| `GfxTextwriterLayout` | Model measured glyph advance and the below-one fallback. The old fallback-sized lower bound is false: a one-unit glyph can be below `24 × 0.6`. Prove the actual `min(1, fallback)` bound and positivity. |
| `GfxTextwriterLayout` hue | Model a signed remainder. Nonnegative hue requires nonnegative phase; negative phase is not an unsigned fractional part. Retain a negative-phase witness. |
| `TAAHalton` | Natural-number increment/loop arithmetic does not certify signed-C overflow, history resets, or jitter bounds. |
| `PipelineOrder` | Ordering assigned labels does not certify the C pass table, conditional dispatch, or omitted stages. |
| `GxMath.Matrix.mul` | Subsequent camera review found the opposite-product formula despite an asserted C correspondence. Replaced all 16 fields with the actual `gX_Matrix_Multiply` expressions; retained an asymmetric translation/scale witness. |
| `GfxMatrix` / `GxMath.ClipMatrix` commentary | Flat numeric-field order is not physical C member order. Abstract double transpose does not certify readback, and row/column duality does not certify UBO serialization. Physical raw upload is modeled separately below. |

## Postprocess normalization and empty-chain contracts

`GxProofsGfx.Postprocess.PostprocessContracts` contains 33 exact-rational/functional contracts
grounded in `src/renderer/postprocess.c` and
`resources/shader/postprocess/blur_gauss.frag`:

- Matching target normalization preserves normalized coordinates and the unit
  interval; a mismatched denominator multiplies coordinates by target/source.
- Half-resolution `res` on a full-resolution pass doubles the base coordinate;
  supplied repeat-tile semantics give duplicate interior lookups. This is a
  retained historical counterexample reproduced on an asymmetric GPU fixture.
- Target-normalized base UV and source-texel offsets are different quantities.
- An empty ideal effect list preserves its supplied input. New chains default
  to the scene with feedback off; unconfigured empty/all-zero chains bypass the
  effect buffers. Explicit feedback/injection still assembles the authored input.
- Historical authored-last-slot finality retains its trailing-zero counterexample.
  Repaired routing counts executed passes: the next target differs from the
  current source, the committed source equals that target, and a zero-loop tail
  cannot change presentation. Counts are nonoverflowing and backing names distinct.
- Default canvas selection preserves the simplified opaque assembly; explicitly
  enabled auxiliary layers prevent the empty-chain bypass.

`make postprocess-contract-check` runs six scalar/source controls and six native
source/target-policy witnesses. The checker compiles the production C input helper,
ping/pong target selection and per-pass resolution in a scratch harness; float-division witnesses
have an explicit `1e-6` finite tolerance. The shader formula is guarded and
modeled scalarly, not compiled as GLSL in that harness. Each effect pass now runs
at matching start/ping/pong dimensions, keeping both base UV and source-texel
spacing valid; a separate presentation uses the actual destination dimensions.
Source guards track this repaired policy and retain historical scalar negatives.

`make gfx-pp-check` includes `scripts/pp_routing_check.py`: 16 asymmetric GPU
fixtures cover scale 1/0.5, odd dimensions, one/multiple passes, destination RTs,
default/empty/all-zero input, a zero-loop tail, explicit feedback/injection/tint,
canvas aliasing and an unloaded-start early return. Corner/notch evidence and
scope-depth logs live under `/tmp/opencode/pp_routing_check`; the opt-in probe is
Debug-only. The earlier four-repeat/cropped BLUR and black-default outputs fail
the same spatial metrics. This is finite GPU evidence, not universal accuracy.

Valid names and matching allocated effect-buffer dimensions, input population,
opaque transfer, coordinate orientation, wrap/filter behavior and GPU accuracy
remain external proof premises. Feedback convergence, transparent/non-alpha
blend equivalence and arbitrary malformed/aliased effect targets are not proved.
The author confirmed BLUR and empty-chain behavior in the live preset; that
signoff does not discharge the stated numerical/GPU proof premises.

## Physics force and radiation contracts

`GxMath.Physics` (Mathlib tier) mirrors the SI-exact helpers in
`src/gxmath/physics.c` over ℝ — the C `Float` code approximates the modelled
reals, with rounding bounded by the unit tests and the SymPy oracle:

- Coulomb signed magnitude is symmetric in the charge pair, positive for like
  charges and negative for opposite ones; the pair potential is symmetric too
  and equals the force times the separation.
- The electric field is parallel to the offset vector.
- The Lorentz force is perpendicular to the velocity (magnetic fields do no
  work) and to the field, and flips under the velocity/field swap.
- The cyclotron radius is positive on its defined domain and antitone in the
  field magnitude.
- Magnetic dipoles: the on-axis field is twice the equatorial field; both are
  positive for positive moments and fall off with distance; coaxial moments
  attract or repel by the sign of the moment product; the coaxial force is
  the aligned pair potential's radial derivative in algebraic form
  (`F = 3·U/r`); the torque vector is perpendicular to the moment and the
  field, flips under the swap, vanishes for parallel moments, and the
  textbook magnitude peaks at `m·B` (θ = π/2) and never exceeds it.
- Photon energy and the Wien peak are positive on their domains and antitone
  in their denominators (wavelength/temperature); Stefan-Boltzmann power is
  monotone in temperature (nonnegative emission factors).
- The blackbody sample normalization returns the zero vector for a
  non-positive sample and maps nonnegative samples into `[0, 1]`.

The physical constants are the `config.h` CODATA values as exact rationals,
so their positivity is kernel-checked rather than assumed.

## Label fitting, placement and padding contracts

<!-- //ai: exact contracts retain the rasterizer/atlas/UV/source-correspondence obligations. -->
`GxProofsGfx.Renderer.LabelContracts` adds 18 exact rational contracts grounded in
`common/utils/billboard.c` and the GFX label helpers:

- Quad fitting preserves the 512:64 texture aspect and stays inside a positive
  requested world-size box.
- Centering preserves the midpoint. **Given** final measured extents within the
  eight-pixel reserved margin, a 0–4 pixel shadow stays within the texture.
- An ideal homothetic fit scale is nonnegative, at most one, and fits positive
  extents to the available capacity. This is explicitly **not** a proof that the
  production eight-step remeasure loop with fixed spacing always fits all fonts.
- The label's lower edge sits at bounds-top plus gap; nonnegative gap keeps it
  above every corner supplied below that top. Maximum/center/top operations
  commute with translation. Correct transformed mesh bounds remain a premise.
- UV Y inversion is involutive, preserves [0,1], and swaps top/bottom. This does
  not establish RT classification, VBO uploads or sampler/rasterizer behavior.
- Byte-color normalization lies in [0,1] under the supplied byte bounds; actual
  blending, gamma, premultiplication and readable contrast are not certified.

`make gfx-authoring-check` extracts the real anchor/top/quad helper bodies and
links unchanged gxmath, checks history and saved styles, tests oversized text
at raster size 256, and compares an asymmetric F in the presented scene to its
RT glyph with the vertically flipped glyph as a negative. CPU tolerances and
finite pixel thresholds are explicit in the scripts. Glyph content, atlas
coverage/overhang, unsupported codepoints, animated bounds, arbitrary cameras,
GPU precision and the handwritten proof/source mapping remain external.

## Weather and bounded godray contracts

<!-- //ai: exact rational policies retain C remainder/random/float and GPU premises. -->
`GfxWeatherContracts` adds 27 authored theorems grounded in
`src/renderer/gfx/weather.c`: signed-remainder correction, wrap containment,
in-bounds identity, conditional modulo congruence and translation, positive or
fallback band height, fixed-floor/minimum-ceiling containment, exact band span,
ground/high-eye regimes, initial/collision/horizontal spawn bounds, and
eye-relative fog height/fade bounds and translation. The old one-width wrap has
a retained multi-width-jump counterexample.

The wrap remainder is **supplied**, with strict signed bounds; congruence also
requires a supplied exact integer-quotient relation. These are not proofs of
libc `fmodf` or IEEE subtraction/division. Random spawn bounds require picks in
[0,1]; they do not certify generator implementation or temporal independence.
The decimal rationals model intended constants, not their rounded binary32
representations. Field preservation and camera/update chronology remain native
obligations; no particle-motion or terrain-collision correctness is inferred.

`GfxGodrayContracts` adds 11 authored theorems for the positive denominator,
nonnegative/bounded stride, unchanged high-density stride, low-density maximum
footprint, four-tap travel/fraction bounds, and both directions of scalar
pixel-to-sun segment containment. Density 1 and 15 retain exact old-stride
overshoot counterexamples. Positive-distance assumptions correspond to the shader
normalization guard; finite floating normalization, texture LOD/source freshness,
depth/occlusion, optical scattering quality and GPU execution remain external.

`scripts/weather_godray_contract_check.py` supplies explicit source-drift guards;
target-anchoring, moving-floor and unbounded-stride mutations are rejected. This
is not an AST/translation proof. `make weather-follow-check` runs those guards,
the extracted real CPU helpers/loops, native vertical checks and GPU relocation
checks; `make godray-check` retains actual production-shader density/freshness
controls. Full reachability/axiom audit and admission are separate gates; both
pass for this batch. The retained full ledger reports 92 reachable modules and
2,000 declarations including generated helpers, with standard axioms only:
`/tmp/opencode/weather_godray_proof_validation/axiom_ledger.json`.

## What remains unproved

Lean validates the encoded propositions, not whether handwritten specifications
faithfully describe the engine. The source review above is focused, not a
comprehensive correspondence proof for the entire corpus. In particular:

- ℝ/ℚ algebra is not a proof of IEEE float rounding, NaN behavior, or GPU results.
- FBO restoration assumes valid GL names, a coherent known cache or invalidated
  cache, and a read-only attachment query; driver error behavior is not modeled.
- State-scope models that define restoration as returning the saved state express
  a caller contract, not a proof that every C restoration path implements it.
- GPU clear/draw completion, camera reprojection, bilinear footprints, resource
  independence, and authored-signature collision freedom are not implied by the
  temporal lifecycle contracts below.

## Temporal-history lifecycle contracts

`GxProofs.TemporalHistory` is a core-tier model grounded in
`src/renderer/gfx/temporal_history.c` and the authored-reset predicate in `taa.c`.
The lifecycle addition brought the corpus to 72 modules. Its contracts cover:

- Init/reset invalidate history, request clearing, and restart index/parity.
- `MarkCleared` invalidates history and clears the pending flag, but deliberately
  does not abort a pass or alter its index/parity.
- Begin rejects pending clear, an active pass, unloaded targets, or missing
  pointers, preserving both state and the caller's output target pair.
- A successful begin changes only `passActive`, selects the current read target,
  and rejects a nested begin.
- Active commit flips parity exactly once, advances or wraps the index, and makes the
  written target current; inactive/repeated commit is a no-op. Abort preserves
  prior history and prevents a subsequent commit from advancing it.
- Clear-before-use is preserved as an inductive invariant from well-formed
  states, not asserted for arbitrary corrupted state. Commit itself does not
  validate clearing or rendering.
- Routing changes, unequal active authored signatures, and non-consecutive
  committed resolves with valid history trigger the exact reset-policy guard.
  The chronology guard applies to raster and hybrid history alike.

`src/main_tests.c` independently exercises 4,096 begin combinations across
four counter witnesses, all state flags, target-loading flags, and nullable
arguments, plus 5,184 backing-name combinations, clear/reset/abort/commit,
nullable queries, and same-wrapper rejection.
This is finite native correspondence evidence, not an automatic proof of C.

The runtime guards and remaining assumptions are intentionally explicit:

1. C begin rejects identical wrappers, missing framebuffer/color names, shared
   framebuffer names, or shared color-texture names, preserving state and output.
   `beginPass` is the model's selection core; `beginCheckedPass` models the full
   hardened entry point. Accepted checked passes have distinct wrapper labels
   and distinct nonzero backing names. Actual attachment identity still assumes
   coherent wrapper metadata, live GL names, and no external texture-view aliases.
   No per-frame GL query or complete GPU allocation proof is added.
2. C's signed `gX_I32` sample index wraps from `2147483647` to zero on active
   commit. The newly written history remains valid and parity flips exactly once;
   no clear/reset is requested. The Nat model proves every active next index is
   bounded and preserves ordinary increments below the limit. Native tests
   include active/idle maximum commits. A focused UBSan witness checks the
   maximum-minus-one → maximum → zero → one path without signed overflow.
   Nat still excludes arbitrary negative/corrupt C state.
3. The optional TAA oracle converts the index to float **before** adding one,
   eliminating its separate signed `index + 1` overflow. This retains ordinary
   running-average behavior; after wrap it restarts the sample count, not an
   unbounded lifetime average. Float precision at very large indices is not
   certified, and the ideal increment lemmas in `TAAHalton` do not describe the
   resettable/wrapping runtime trace.

Signature equality is an input comparison, not proof that FNV64 is collision-free
or detects every semantic edit. Likewise, the UInt64 successor comparison is not
a claim about unbounded frame chronology. GPU completion remains a caller duty.

## Ideal reprojection and bilinear sampling

`GxProofsGfx.Temporal.TemporalSampling` is a Mathlib-tier, exact-rational model of
`resources/shader/gfx_taa_resolve.frag`; it brings the corpus to 73 modules.
It proves componentwise projection/jitter identities, not complete camera-matrix
inversion or shader execution:

- Removing current NDC jitter reconstructs the homogeneous component ratio.
  Projection is invariant under nonzero homogeneous scaling.
- Camera fallback samples `prevUV + prevJitter/2`; removing the jitter delta
  from motion classification yields zero motion for an ideal static camera.
- With positive homogeneous w, normalized depth lies in `[0,1]` exactly when
  the clip z component lies in `[-w,w]`.
- Bilinear weights are nonnegative and sum to one. Nested mixes equal the four
  weighted taps, preserve constants, and remain within a common tap range.
  Normalized depth taps therefore interpolate to normalized depth.
- The `uv*size - 1/2` footprint maps texel centers to integers; its floor
  fraction lies in `[0,1)`. Clamped tap indices stay within a positive-size
  texture, including singleton textures. Boundary witnesses show duplicate
  edge taps rather than out-of-range fetches.
- For hybrid-owned pixels, eligibility matches the shader's strict comparisons:
  coverage below `-1.5`, w at/below `1e-5`, UV/depth outside the closed unit
  interval, or depth difference above `.001` reject. Exact UV edges and exact
  depth tolerance are allowed. This is eligibility for the hybrid rejection
  branch, **not** proof that later history checks accept or accumulation occurs.

The error-margin theorem is conditional: supplied sample/projection error bounds
plus ideal depth mismatch must fit within `.001`. No bound on IEEE division,
half-float storage, matrix conditioning, NaNs/infinities, or texture size/index
conversion is inferred. Subsequent runtime guards check homogeneous w and
finiteness before division and check the results again; the rational sampling
proofs alone do not establish safe IEEE execution on every runtime path.
Matrix inversion, matrix upload conventions, rasterization, and RT Y-flip
correctness remain independent obligations.

A retained counterexample shows that interpolation can match expected depth
even when none of the taps do. Convexity and depth agreement are not a proof of
surface identity or universal disocclusion safety. Native temporal coordinate,
rejection, production-color, stability, and invalidation probes remain necessary.

## Camera-matrix correspondence

`GxProofsGfx.Temporal.TemporalMatrix` brought the corpus to 74 modules. The model uses
the field formulas in `GxMath.ClipMatrix`, matching numeric-field rows
`m0..m3`, `m4..m7`, etc. This is distinct from physical C struct memory:

```
numeric-field R row 0:  m0, m1, m2, m3
physical struct row 0: m0, m4, m8, m12
raw GL_TRUE matrix:    transpose R
shader column action: transpose R * world = world * R (row action)
```

The physical struct is passed through `&matrix.m0` in the raw path of
`gX_Shader_SetValue_Matrix`, with the program activated and transpose enabled.
For the TAA `gX_Matrix_Multiply(view, projection)` path, this yields view action
followed by projection action; **no runtime transpose or multiplication change**
is made. The abstract flat-tuple lemmas in `GfxMatrix` do not by themselves
establish this layout correspondence or constrain every foreign backend path.

The new ideal contracts prove:

- The raw transpose flag and physical layout implement the row action of the
  numeric-field matrix; asymmetric witnesses distinguish the wrong flag/order.
- An exact inverse satisfying `current * inverse = identity` restores the world
  point from normalized clip coordinates, assuming world w=1 and current clip
  w≠0. Inverse-transformed homogeneous w is exactly `1/currentClip.w`.
- Applying previous VP to that restored point yields the true previous clip
  vector. Its positive-w guard is preserved under a supplied previous-w bound;
  previous history X then matches the scalar jitter-corrected fallback theorem.

These are conditional inverse contracts, not verification of C's cofactor
inversion, determinant safety, or IEEE conditioning. Inputs are already
de-jittered using the scalar contracts; near/far mapping, viewport Y-flip,
orthographic support, actual previous-frame snapshot chronology, and all UBO or
non-raw upload paths remain independent correspondence obligations.

Native `gxmath_tests.c` tests all 16 physical memory slots, asymmetric products
and transformed points, and an affine inverse roundtrip. The independent hybrid
temporal probe exercises the actual shader's camera fallback, motion, previous
depth, eligibility, and rejected production color. These finite witnesses do not
prove every camera or singular/ill-conditioned matrix safe.

## Previous-frame snapshot provenance

`GxProofs.TemporalSnapshots` is core-tier and brought the corpus to 75 modules.
It models assignments in `motionblur.c`, the end-of-frame save in
`gfx_pipeline.c`, and temporal invalidation/commit. Matrix labels are abstract;
`historyCamera` is proof-only provenance, **not a new runtime field**.

- Motion blur changes its own VP copy while leaving TAA's private copy and
  retained history unchanged. Thus, after the prior completed pipeline saved
  camera A, a current-frame motion-blur save of camera B cannot change TAA's A.
- Pipeline completion records the current camera in both VP snapshots but does
  not itself commit temporal history.
- Reset invalidates temporal history/provenance without rewriting either VP.
  Snapshot/history alignment is then vacuous, not proof that an old VP is valid
  for a new image. Actual clearing and first-frame rejection remain independent
  obligations.
- A successful history commit followed by a pipeline save of the **same** camera
  establishes snapshot/history alignment. If the camera changes between them,
  or a resolve is skipped while a later pipeline still saves its VP, alignment
  is not automatic; explicit counterexamples retain this limitation.

The native temporal probe's `--only snapshots` captures frames 10, 11, and 12
with moving camera, forced camera fallback, and motion blur enabled. A gated
stamp at motion-blur completion must precede the TAA diagnostic; actual shader
motion/previous depth must match the independently computed prior-frame camera.
The fixture also requires the expected motion to be separated from the wrong
current-camera prediction by more than twice the unchanged `.05` pixel budget.
These cases are included in `--only all`; existing invalidation fixtures remain.

No proof certifies every conditional pipeline schedule, every viewport-grid
save/restore path, or shader-resource availability. Keep last completed pipeline
VP and last successful history camera distinct when interpreting the results.
The subsequent commit-gap guard below closes the ordinary skipped-resolve path;
it does not eliminate the same-frame camera/scheduling assumptions.

## Committed-resolve gap invalidation

TAA's `lastResolveFrame` advances only after `TemporalHistory_CommitPass`, not at
entry or after a rejected/aborted begin. With valid retained history, any frame
comparison other than the UInt64 successor of that committed timestamp clears
both targets and resets history before selection. This includes skipped frames,
duplicate timestamps and out-of-order frames, for raster and hybrid alike.
Already-invalid history does not need another gap clear; normal target-clearing
obligations still apply.

`TemporalHistory.needsHistoryReset` models the new combined routing/signature/
validity/chronology guard. `needsHybridReset` is a convenience specialization for
valid history, not a separate hybrid-only runtime policy. `TemporalSnapshots`
adds timestamp contracts: inactive commit and abort preserve the stamp; active
commit records the supplied frame; gap invalidation restores vacuous alignment
without pretending that an old pipeline VP matches retained history. The old
unchecked skipped-resolve counterexample remains meaningful: the guard must run
before that history is consumed.

Native `--only gaps` (also included in `--only all`) omits frame 19 either before
begin or through an explicit begin/abort. Both raster and hybrid variants must
retain committed stamp 18, reset on frame 20, and output exactly current color/
depth. Frame 21 must resume without another reset; hybrid coordinates/depth are
checked against the independent prior-camera oracle with unchanged budgets.

The timestamp proves chronology only under the supplied UInt64 successor
comparison; no unbounded or wrap-unique clock is modeled. Correct pipeline
completion, a stable camera between resolve and final snapshot save, coherent
targets, and successful GPU clear/draw remain separate obligations. Finite
native fixtures are evidence, not a universal conditional-scheduler proof.

## Unsafe inverse and homogeneous division guards

TAA now classifies current VP, inverse VP, and previous VP coefficients for
finiteness before upload. Non-finite candidates upload harmless identity
matrices with `reprojectionValid=0`; the shader emits current color/depth without
attempting reprojection. The active `update.c` location cache refreshes this
uniform on initial load and hot reload, not just the unused cache helper.

The shader checks finite world components and `abs(world.w)>1e-5` **before**
world normalization, then checks normalized components again. It checks finite
previous clip components and `previous.w>1e-5` before perspective division, and
checks the resulting UV/depth, motion and history UV before history sampling.
Unsafe candidates return current color/depth. The existing hybrid depth/UV
eligibility thresholds remain unchanged; these safety guards apply to raster
and hybrid reprojection alike.

`GfxTemporalSampling` proves that the ideal denominator guards imply nonzero
world w/positive previous w, rejects boundary/non-finite classification inputs,
and bounds a rational quotient under the supplied denominator bound. Finiteness
is an explicit Boolean classification input; rationals do not represent NaN or
Inf. A bounded denominator can still amplify a numerator beyond IEEE range,
which is why post-division checks remain necessary.

Native `--only safety` fixtures (also in `--only all`) exercise a singular current
VP, zero/tiny world w, zero/negative previous w, NaN/Inf coefficients, and finite
coefficients causing world/previous overflow. Every unsafe output must be finite
and exactly equal to the actual current color/depth witness; GL diagnostic errors
must be zero. A normal-history/current contrast control prevents a vacuous test.

The unchanged global `gX_Matrix_Invert` cofactor implementation divides by its
determinant without a status return. This TAA boundary classifies its result; it
does not prove the inversion algorithm, install a global checked-inverse API,
or support trapping floating-point environments. Finite but inaccurate or
ill-conditioned inverses are not universally detected or certified. Current
input color/depth quality is also outside these reprojection safety contracts.

## Source-grounded cofactor inverse

`GxMath.CofactorInverse` (with the camera bridge in `GfxCofactorInverse`) brings the corpus to 76 modules. It encodes the
12 cached minors, six-term determinant expression, and all 16 output numerators
of `gX_Matrix_Invert` over exact rationals in numeric-field row order. Kernel
proofs establish the row-zero cofactor expansion and both adjugate products:

```
M * adj(M) = determinant(M) * identity
adj(M) * M = determinant(M) * identity
```

Therefore, when the **exact** determinant expression is nonzero, both products
with the encoded cofactor inverse equal identity. The camera reconstruction
theorem now derives its inverse premise from that determinant condition instead
of requiring an unrelated abstract inverse hypothesis. This is exact algebra,
not a guarantee that the C float determinant or inverse equals its rational
interpretation after rounding.

`scripts/test_inverse_contract.py`, run by `make proofs-full`, compares the C
cache-to-field mappings, 12 minors, determinant expression and 16 numerators
with their Lean counterparts as canonical integer-coefficient polynomials. It
also has sign/field negative controls. This standard-library-only check detects
algebraic source drift; it is neither a C compiler verification nor a proof of
IEEE evaluation order. The native SymPy math-pass oracle remains independent.

`gxmath_tests.c` adds four stored-float accuracy witnesses. Residual is the maximum
entrywise error of either product, evaluated using double products of the stored
float input/inverse. The condition proxy is `||M||∞ * ||computedInverse||∞`, not
a verified exact condition number:

| Witness | Native result |
|---|---|
| Power-of-two diagonal | Finite; residual exactly 0; condition proxy 1,048,576. |
| Almost-dependent 2×2 block | Finite; residual exactly 0; condition proxy 4,194,308. |
| Half-ULP perturbation rounded away | Stored matrix is singular; output is non-finite, as explicitly checked. |
| Stored-float 4×4 Hilbert | Finite; residual 0.00146873047061; condition proxy 28,393.3678709. Measurement is advisory, not an accuracy pass predicate. |

The rational zero-matrix inverse uses total division at zero and is proved not
to be an inverse. It does **not** model C's Inf/NaN singular result. No universal
IEEE residual/condition bound, checked-inverse runtime API, or new matrix-failure
policy is introduced. Finite inverse accuracy remains a measured limitation.

## Conditional residual-to-reprojection bounds

<!-- //ai: quantitative exact bounds require independently justified uncertainty and margins. -->
`GxProofsGfx.Temporal.ReprojectionError` extends the corpus to 77 modules. For the
numeric row-action convention, it defines the relevant inverse residual as
`E = current * candidateInverse - identity`. If every entry of `E` is bounded
in absolute value by `e`, and every world-vector component by `W`, then the
unnormalized reconstruction error is at most `K = 4 W e` per component.
For world `w = 1` and an actual recovered homogeneous denominator with absolute
value at least `mu > 0`, normalized world error is at most
`delta = (K + W K) / mu`. Exact homogeneous scaling proves this also applies to
the shader's normalize-clip-before-inverse path, assuming nonzero current clip w.

If previous-camera matrix entries are bounded by `P`, previous clip error is
at most `k = 4 delta P`. For each U/V/depth coordinate, with ideal ratio `r`,
nonzero ideal denominator, and actual previous clip denominator of absolute
value at least `nu > 0`, projected error is bounded by
`(k + |r| k) / (2 nu)`. A composed theorem derives all three coordinate bounds
from the inverse residual. Multiplying the U/V bound by viewport width/height
gives axis pixel error. This is not automatically a Euclidean motion-vector
bound, nor a bound on history sampling, quantized depth, or disocclusion.
The shader requires positive previous clip w; the absolute-margin theorem also
covers negative denominators algebraically, without asserting they are eligible
for history. Equal exact jitter offsets cancel in differences; jitter mismatch
or rounding must be budgeted separately.

Separate theorems add residual-measurement uncertainty and observed GPU/storage
uncertainty via triangle inequalities. Those uncertainties are **supplied
hypotheses**, not inferred from double arithmetic, a finite inverse, or shader
guards. Input depth reconstruction, matrix uploads, arithmetic and texture
storage errors must be included in an independently justified uncertainty
budget before using the result as a runtime certificate. The recovered `mu`
margin refers to the unscaled recovery used in the exact bound, not directly
to the shader's normalize-first denominator guard.

Kernel-checked illustrative rationals fit the existing .05 axis-pixel and .0005
depth budgets; they do not certify actual engine cameras. The depth illustration
includes an explicitly assumed `1/2048` storage/evaluation allowance, **not** a
proved texture-format bound. A negative witness has numerator error `1e-5`,
zero denominator error, and denominator `1e-5`, but 320 pixels of projected
error at width 640. Nonzero/small-w rejection alone cannot certify accuracy.
No runtime code, tolerances, payload caps, or IEEE accuracy policy changes.

<!-- //ai: native fixtures instantiate exact bounds without upgrading empirical uncertainty to a theorem. -->
The subsequent `make reprojection-residual-check` gate (included in `make check`)
instantiates these formulas using exact fractions of native hex-float exports.
It compares an ordinary perspective camera and three synthetic conditioning
matrices, with a separate singular fixture. Residual-measurement and CPU
projection deviations are measured exactly for these samples, not proved for
all inputs or GPUs. The finite Hilbert fixture exceeds the .05 Y-pixel budget;
the gate checks containment and records accuracy comparisons as advisory.
See [`math_pass.md`](math_pass.md) for the measurements and limits.

## Weighted componentwise reprojection bounds

<!-- //ai: retain per-coordinate information, with no cancellation or IEEE overclaim. -->
`GfxWeightedReprojection` extends the corpus to 78 modules and replaces global
maxima by absolute weighted sums. For exact residual `E`, unnormalized component
error is bounded by `a[j] = sum_i |world[i]| |E[i,j]|`. For affine world w=1,
normalized X/Y/Z bounds are `(a[j] + |world[j]| a[w]) / mu`; normalized W error
is **exactly zero**. The previous camera then gives separate column bounds
`b[j] = sum_i delta[i] |previous[i,j]|`. U/V/depth use numerator bound `b[j]`
and denominator bound `b[w]` separately:

```
projectedError[j] <= (b[j] + |idealClip[j]/idealClip[w]| b[w]) / (2 nu)
```

The composed theorem covers the shader's exact normalize-first path. Weighted
dot sums are proved no larger than the corresponding uniform `4 W e` envelope.
No sign cancellation is used. As before, positive denominator margins are
required, and input/evaluation/storage uncertainty remains external.

The native fraction oracle checks every intermediate weighted bound, final
containment, and that weighted projected bounds do not exceed the old uniform
bounds for all four finite fixtures. Three additional oracle tests pin columns
rather than rows, sparse-coordinate propagation, and zero normalized-W error.
Including the same measured CPU deviation, perspective X bound drops from
`0.009392110537002675` to `0.00006782470344206597` pixels. Hilbert Y bound drops
from `28.675144274901967` to `0.5666920080280106` pixels, still containing its
`0.27278900146484375` observed error and still exceeding .05. These are
fixture-specific exact bounds plus empirical CPU deviations, not GPU or
universal IEEE error certificates. No runtime code, tolerances or caps change.

## Depth, UV and jitter input sensitivity

<!-- //ai: ideal clip inputs no longer hide supplied depth/jitter errors. -->
`GfxDepthInputError` extends the corpus to 79 modules. It mirrors the TAA shader
constructor `(2u-1-jx, 2v-1-jy, 2depth-1, 1)` and proves component bounds
`(2eu+ejx, 2ev+ejy, 2ed, 0)`. Weighted inverse-column sums and homogeneous
normalization propagate these through world recovery and previous-camera
U/V/depth projection. Input sensitivity is compared to the same candidate
inverse with ideal inputs; a separate componentwise triangle theorem adds the
baseline inverse-residual bound rather than conflating the two sources.
Previous-history jitter error adds `ej/2` to a projected coordinate bound.

The conditional nearest-grid theorem assumes `|index-levels*depth| <= 1/2`
and positive levels, then proves depth error at most `1/(2*levels)`.
It does not establish a texture's format, raster/interpolation behavior,
conversion rounding, resolve semantics, or the nearest-index premise for GPUs.
In the GL3.3 backend, resolved scene depth uses raylib `rlLoadTextureDepth`,
which requests **unsized** `GL_DEPTH_COMPONENT` (`external/raylib/src/rlgl.h`);
the wrapper's `format=19` comment is not proof of driver precision. Explicit
24-bit MSAA allocation in `gfx/load.c` does not certify the resolved texture
or sampled depth rounding. No raylib or renderer source is changed.

`make depth-input-check`, included in admission, checks native CPU perspective
fixtures at distances .125, 3 and 900, near/far planes .1/1000, and a small
known jitter mismatch. It exports exact hex samples, simulates nearest rounding
on a 16,777,215-level grid, and separately measures forward-depth arithmetic,
grid rounding, stored-float conversion, input world sensitivity, baseline
inverse residual and CPU evaluation deviation. At distance 900, depth-input
error `1.916454897986518e-8` yields input world-Z bound `0.3966795721679814`;
the projected depth bound including CPU deviation is `0.0004581341404745216`.
These fixtures fit the existing advisory budgets, but neither the grid simulation
nor measured CPU deviation is a GPU/IEEE certificate. All margins and uncertainty
obligations remain explicit; no tolerance, cap or runtime policy changes.

## Empirical GPU depth precision and reflection correspondence audit

<!-- //ai: observed driver behavior is evidence, not a theorem about all GPUs. -->
`make gpu-depth-precision-check` queries the live scene and MSAA depth textures,
then raster-writes constant near/mid/far perspective depths into **disposable
format-matched** textures. Queried format/bit-depth/sample count must match the
live allocations. The fixture separately checks GLSL depth sampling, float
readback and depth-only nearest MSAA resolve. Production target contents and
allocation policy are untouched; the opt-in hook is absent from lean Release.
State restoration (including VAO, bindings, samplers, pack state and capabilities)
and zero GL errors are required. The probe invalidates RHI caches before restoring
raw GL changes; an initial strict state check caught and rejected a stale-cache
restore, with failure logs retained rather than weakening the check.

On the tested GTX 1060-class GL3.3 driver, resolved scene depth
reports unsized format `GL_DEPTH_COMPONENT` with 24 depth bits; MSAA reports
`GL_DEPTH_COMPONENT24` with 24 bits and 4 samples. Three samples match
hypothetical nearest UNORM24-to-float conversion. All MS samples agree, sampling
equals readback, and resolve adds zero measured error relative to the MS sample.
Total resolved depth-input errors (including float submission) are approximately
`2.81407e-8`, `2.54160e-8`, `2.57584e-8` for .125, 3, 900 distance. Reports compare
these with the conditional grid-plus-float-conversion bounds from the proof
contract. This is not a presented-scene interpolation test, an edge/sample-choice
resolve test, an allocation guarantee for other drivers, or a universal GPU/IEEE
certificate. Numeric agreement in three samples does not prove a rounding rule.

The requested reflection proof review found `ReflectFlags` to be an abstract
freshness/parking policy, not a proof of reflection geometry or SSR ray accuracy.
The correspondence repairs and limits are:

- `planarDepthValid` is a ghost per-pixel premise, not a runtime member.
  `renderPlanar` setting it cannot certify all rendered pixels contain geometry.
  The historical `depth_cleared_far` theorem proves a Boolean policy, not a
  numerical depth clear/threshold fact.
- Production planar binding requires override, aux enable, filled-this-frame
  and map-location availability. Pixel masking is a separate shader subgate;
  **missing depth mask allows legacy sampling**, rather than forcing environment
  fallback. Added a kernel-checked counterexample to the stronger old claim.
- Production IBL routing prioritizes precomputed IBL; dynamic fallback needs a
  nonzero cube ID and freshness. Dummy fallback requires a dummy ID; if neither
  exists, that branch makes no new binding. Added source-grounded branch proofs.
- A failed GL bind can leave the previous binding intact. The old `envTerm`
  zero-result model is now explicitly illustrative, not a hardware guarantee.
- Selected sky sampler parking now uses source constant unit 9, not stale 7;
  CSM stays 10. These are selected initial parking facts, not the whole live map.

`test_reflection_contract.py` runs in the core gate to pin reset ordering,
IBL resource branches, missing-mask behavior and authoritative parking constants.
These focused text checks do not verify the C compiler or GLSL execution.
`RhiStateScope`'s terrain mirror scope matches the selected saved/restored fields,
but assumes successful queries/wrappers/restoration; it proves neither all GL
state nor GPU draw completion. Planar geometry, mirror feedback convergence,
SSR traversal completeness/false-hit bounds and comprehensive handwritten
proof-to-source correspondence remain uncertified. No reflection runtime policy
or audit threshold is changed by this review.

## Exact planar reflection geometry

<!-- //ai: geometry proofs are exact; camera-frame and GPU obligations are not collapsed into them. -->
`GfxPlanarReflection` extends the corpus to 80 modules. In homogeneous numeric
row coordinates it models `R(p) = p - 2 n (dot(n,p.xyz) - d*p.w)` with W
unchanged. A point (w=1) receives the plane offset; a direction (w=0), including
the camera up vector, does not. Kernel proofs establish:

- With **exactly unit** normal, signed plane distance negates and reflection
  is an involution; affine point squared distances are preserved.
- Points satisfying the exact plane equation are fixed. This theorem does not
  require a unit normal, but involution/isometry do; a nonunit counterexample
  pins that necessary hypothesis.
- A homogeneous reflection matrix has translation in the last numeric row and
  implements the same point/direction action. GL_TRUE physical upload plus
  C view-then-projection multiplication implements reflected row projection.
- On-plane points have identical direct and SSR reflected-point clips. For
  clip w > .001, SSR's positive-w gate and the direct shader's .0001 denominator
  floor agree on `(ndc+1)/2` UV mapping, with no additional Y flip. A rational
  asymmetric UV witness distinguishes flipped V from the correct mapping.

The denominator floors in this rational model are ideal decimal values; actual
GLSL literal rounding and evaluation are part of the explicit IEEE/GPU gap.

This is **not** a proof of `gX_Matrix_LookAt` orthonormalization/handedness,
nondegenerate camera frames, float plane-normal accuracy, off-plane/non-planar
surface behavior, shader confidence fades, texture filtering, GPU winding or
SSR ray correctness. The upload/composition theorem accepts supplied view and
projection matrices; it does not assert a mirrored LookAt view equals the
original camera view multiplied by a reflection matrix. LookAt re-orthogonalizes
the reflected camera to a right-handed frame, which must not be conflated with
an orientation-reversing geometric reflection.

`make planar-geometry-check` runs in admission. Native axis and tilted-plane
fixtures use asymmetric position/target/up vectors and on-plane lookup points.
Exact stored-float formulas check reflected points and up directions, a double
oracle checks nondegenerate LookAt, and rational products check View*Projection
and row clip/UV correspondence. Existing math-pass `TOL_EXACT=1e-5` and
`TOL_FLOAT=1e-4` are reused without relaxation. Both fixtures distinguish wrong
transpose, Projection*View, Y flip, and treating up as a translated point.
Four oracle/source tests pin exact unit-normal geometry, nonunit/up negatives,
UV Y direction, and production camera/GLSL formulas. These are focused source
drift checks, not a C/GLSL compiler verification.

The native tilted normal (.6f,.8f,0) has squared-length defect
`4.7683716530855236e-8`; its involution error is `9.5367431640625e-7` and
squared-distance error `6.6695217588730316e-6`. Those are separate IEEE witnesses,
not applications of the exact unit-normal theorem to an inexact normal. Native
UV arithmetic errors are below `1.4e-8` in these samples, not a universal bound.
No reflection runtime, matrix convention, tolerance or payload cap changes.

## Reflected-camera handedness and conditional winding

<!-- //ai: orientation algebra is conditional on exact unit normal/valid normalization, not GPU culling. -->
`GfxReflectionHandedness` extends the corpus to 81 modules. Exact proofs show
that unit-normal reflection preserves direction dot products and negates scalar
triple-product orientation. The position-minus-target delta reflects as a
direction (plane-offset translation cancels). With LookAt's cross-product order
`x = normalize(up cross z)`, `y = z cross x`, a normalized nondegenerate x/z
frame has scalar triple product **+1**. Reflection of that whole frame has **−1**.

For supplied equal exact normalization factors, the rebuilt mirrored LookAt axes
satisfy `xMirror = -R(x)`, `yMirror = R(y)`, `zMirror = R(z)` and retain positive
orientation. Thus geometric reflection and mirrored-camera LookAt are not the
same handedness transform. Isometry justifies equal normalization magnitudes in
exact arithmetic; the proofs still require normalized x/z hypotheses and do not
prove C's sqrt/reciprocal accuracy, nondegenerate source camera frames, or exact
normalization-factor equality for floating inputs.

A separate 2D theorem proves horizontal projected-coordinate reflection reverses
signed triangle area; swapping CW/CCW compensates **if that area relation holds**.
Zero-area triangles are not front-facing in the model. These statements do not
deduce arbitrary projected winding from a 3D determinant: camera placement,
perspective division, clipping, viewport conventions, vertex order and actual
GL state must also be accounted for. The production mirror pass continues to
select **CCW** and restore its prior front-face state; no culling change is made.

`make reflection-handedness-check` is now in admission. Existing asymmetric axis
and oblique native fixtures also export the original LookAt matrix. The oracle
compares original, geometrically reflected and native rebuilt basis columns,
using the unchanged math-pass tolerances. It projects one asymmetric positive-w
triangle in the raw reflected frame and rebuilt frame, checks the expected
horizontal flip and opposite signed areas, and reports the conditional front-face
classification. Four tests pin raw/rebuilt orientation, area/winding negatives,
basis column indexing, C cross-product order and CCW save/restore.

In the oblique sample, raw reflected orientation is `-1.000000080353062`, rebuilt
native orientation is `1.00000012331127`, axis-identity error is
`1.0545728083240826e-7`, and projected area-reversal error is
`1.698229405151041e-9`. These approximate basis witnesses are not applications
of exact unit-normal theorems to inexact floats, nor GPU raster/culling proofs.
Normal/normalization IEEE accuracy and general clip/viewport correspondence
remain explicit obligations. No runtime, threshold, convention or cap changes.

<!-- //ai: SSGI banding correspondence, exact estimator algebra and explicit GPU/radiance limits. -->
## SSGI gather and ambient-fallback contracts

`GfxSsgiContracts` proves that projecting an offset onto a unit normal's tangent
plane makes its normal dot product zero, even after supplied scalar normalization.
The former SSGI hemisphere predicate tested the sign of that zero expression;
rounding could therefore randomly reject taps and normalization could be
degenerate. It is removed. Eligibility now relies on the **reconstructed**
sample delta beyond the unchanged authored normal-separation bias, alongside
the existing distance cap. Depth fetch and reconstruction now share an explicit
integer texel index (`texelFetch`), avoiding half-resolution boundary ties between
hardware nearest sampling and independent UV flooring. Reconstruction uses texel
centers, steps are start-jittered rather than radius-collapsed, and zero deltas
are rejected. Defaults/presets/bias values are not increased.

The gather estimate is normalized by accepted sample count **before** mixing
ambient fallback. Previously the mixture, including ambient radiance, was
divided by count, creating count-dependent dark bands. Exact proofs establish
normalization/mix correspondence, stability when the sample average matches
ambient and an old-order counterexample. Confidence now counts all 4*6*2=48
candidates, with bounds for counts in [0,48]. Nonnegative and capped estimates
require supplied sum/count/radiance hypotheses. This is not a physically exact
GI integral or a certificate of multi-bounce convergence: HDR radiance is not
universally bounded, and authored intensity/bounce gain can amplify feedback.

`make ssgi-contract-check` admits six scalar/source tests and a shared native
perspective reconstruction/flip witness. Source checks are textual guards, not
formal C/GLSL correspondence. The separate `make ssgi-stripe-check` isolates SSGI
with GTAO disabled, checks raw/accumulated flat-only slab RGB profiles for static
and rotating taps, and retains localized bounce around scene objects above the
ambient fallback. Disposable presets and the editor's legacy screenshot are
preserved. It is a fixed-camera fixture, not every scene or depth format.
Thickness's declared uniform is currently not used in the gather predicate;
filtering, normal degeneracy, homogeneous division safety, temporal reinjection,
and GPU accuracy remain explicit limitations. User confirmed GTAO stripes gone;
original live-view SSGI signoff is still required after validation.

The first flat-only gate failed after estimator/separation fixes: raw blue row
range .0119261 remained. A diagnostic measured false normal separation up to
.130127 despite the unchanged .025 bias being uploaded. The shared-index fix
passes the same gate without changing thresholds: raw RGB row ranges are zero;
accumulated ranges are .0000610352. Localized accumulated bounce p90 RGB
.1321/.1266/.1452 exceeds flat fallback .0628/.0733/.0944. These are fixture
measurements, not universal IEEE/GPU error bounds; the failed gate and diagnostic
logs remain under `/tmp/opencode/ssgi_contracts/` and `/tmp/opencode/ssgi_stripes*`.

<!-- //ai: current GTAO SSAO reconstruction/sampling contracts and artifact evidence. -->
## SSAO/GTAO reconstruction and sampling

`GfxGtaoContracts` models the current GTAO generator (legacy kernel SSAO was
removed). UV/depth-to-NDC conversion and projection-only inverse reconstruction
reuse the exact matrix/upload contract, conditional on inverse identity and
nonzero homogeneous w. Depth is nearest-filtered: reconstruction now uses the
selected depth texel center, including clamp-to-edge indexing. The selected
index is supplied in Lean; a half-texel coordinate error bound and affine-plane
coherence/mismatch identities do not prove GLSL floor or hardware tie behavior.
Initial fullscreen UV is flipped once; projected sample UV is not flipped again.

The module proves uniformly spaced, start-jittered six-step distances with last
step at least 5/6 of radius, avoiding the old schedule's collapse of the entire
radius by a small random fraction. Repeated samples returning zero view delta
are rejected before normalization. Existing authored SSAO bias is now uploaded
as normal-separation bias (world/view-distance units); tangent/below-surface
samples are rejected. Conditional separation-error bounds prove self rejection
when the supplied uncertainty is at most the bias. **No universal uncertainty
bound is asserted**. This makes a formerly inert GTAO bias control effective;
defaults/presets are not increased or rewritten. Small contacts below the bias
can be excluded by design, so this is an explicit estimator behavior change.

Conditional horizon update bounds preserve [-1,1] and monotonicity for supplied
valid cosine/weight inputs. Progressive weights are 2/(step+2). The planar
normal stencil identity does not establish valid normals at discontinuities or
degenerate crosses. Background rejection uses ideal rational .999 (not literal
rounding certification), which can also reject sufficiently far real geometry.
Blur range is proved for nonnegative bounded weighted sums divided by the
larger of total weight and the positive floor; zero weight produces zero, not
neutral white. sqrt/acos/trigonometry, final strength pow, finite normalization,
physical AO completeness, temporal padded-history range and GPU precision remain
external. In particular arbitrary negative strength does not preserve [0,1].

`make gtao-contract-check` is in admission: seven scalar/source tests plus an
existing asymmetric native perspective inverse witness check coordinate
conversion, reconstruction and a double-Y-flip negative using unchanged math
tolerances. Source guards are textual drift checks, not C/GLSL equivalence.
`make gtao-stripe-check` is a separate GPU gate using disposable matte editor
presets, an above-slab camera, raw/blur AO dumps, static/rotating taps and a
zero-bias negative. The accepted flat patch must stay within one 8-bit output
code quantum; cube/sphere contact regions must retain dark samples. It guards
the fixed fixture, not every camera or depth format. Screenshot/preset resources
are preserved. SSGI is disabled here to isolate GTAO.

User screenshot `screenshot/gXss_261003_170714.png` motivated the probe. In the
approximate reproduction, texel-center reconstruction reduced blurred row std
from .00747807 to .00210326 but did not eliminate stripes. Disabling horizon
updates isolated remaining sample self-occlusion. Honoring the unchanged .025
bias makes the measured flat patch exactly white (row std/range/step all zero),
with blurred cube/sphere p10 approximately .788/.402. Zero bias retains row
range .00740743. These measurements do not certify the user's exact live camera
or every contact shadow, and retained diagnostic artifacts are under
`/tmp/opencode/gtao_stripes*` and `/tmp/opencode/gtao_stripe_check/`.

<!-- //ai: source-grounded CSM branch order and conditional fade agreement. -->
## CSM selection, projection bounds and fade contracts

`GfxCsmContracts` models current shader branch order rather than the legacy
`ShadowVP` defaults: CSM is enabled only for count>1 and first split>0, then
tests split2/count>=4, split1/count>=3, split0, in that order with strict `>`.
For count>=1 it proves selected layer<count and layer<=3. Strictly ordered
positive splits make exact boundaries belong to the preceding layer. Equal
splits can jump directly to layer3; production clamps configured distances to
camera far, so strict ordering cannot be presumed from configuration alone.
The old count theorem merely proves a weak disjunction, not production clamping.

Positive boundary and blend scale imply positive smoothstep width. Exact
normalized smoothstep has endpoint values 0/1 and stays in [0,1]; mixing bounded
samples preserves their range. A nonoverlap condition puts the next boundary
past the previous fade end. If that previous fade has finished, its output and
the next band's weight-zero output agree for the same previous sample. A
counterexample shows overlapping fades need not agree. These are conditional
endpoint identities, **not** universal continuity of texture samples, moving
geometry or GPU evaluation. Zero/negative scale uses the production abrupt
weight-one branch and carries no smooth-transition claim.

For positive homogeneous w, the exact `(coord/w+1)/2` bounds are equivalent to
`-w<=coord<=w`. Receiver/sample normalized-depth error perturbs the shadow
comparison by at most `er+es`, unlike radial depth's `er+far*es`; bias is a
matched normalized-depth input. Actual matrix accuracy, bias computation and
texture/filter errors remain supplied. Production center guards omit a lower
depth bound and explicit w safety; native negative-depth witnesses record this
limitation rather than assuming complete clip rejection. Off-center PCF taps
can leave the face despite an accepted center; texture wrap behavior is external.

`make csm-contract-check` checks 36 CPU selector/fade transcriptions at/around
three splits for counts1..4, plus three native orthographic projection witnesses.
Five tests include equal/disabled splits and overlap negatives. Textual guards
pin surface, terrain and fog selector branches; the scalar CPU fixtures do not
execute those shaders. Surface/terrain select on reconstructed view depth,
whereas fog selects on ray distance: identical branch algebra does not prove
the same world point chooses the same cascade. View-depth reconstruction,
frustum coverage, full PCSS/PCF behavior and GPU rounding remain uncertified.

<!-- //ai: point-shadow radial encoding and comparison contracts; supplied precision limits. -->
## Point-shadow radial depth and comparison

`GfxPointShadowDepth` models `gfx_pointdepth.frag`'s radial `length(worldPos-lightPos)/far`
write and the surface/fog receiver's `receiver-bias > sample*far` comparison.
It proves decode identity for nonzero far, normalized range for `0<=radius<=far`,
the matched-far shadow criterion `caster+bias<receiver`, strict equality being
lit, bias monotonicity, conditional clear-depth behavior, and bounds for the
center-plus-four-tap average. Bias is in world-distance units, not normalized
depth units. Radius is a supplied scalar: Euclidean sqrt execution, raster
coverage and camera/frustum clipping do not follow from the scalar identities.

With supplied receiver error `er` and sampled normalized-depth error `es`, the
comparison margin deviates by at most `er+far*es` for positive far. Strictly
positive ideal margin beyond that bound guarantees shadow; margin at most the
negative bound guarantees lit. Bias and far are matched exact inputs here;
their own uncertainty must be included separately. No depth-format assumption
or universal GPU accuracy bound is inferred. The five-tap bounds do not prove
physically correct PCF coverage or filtering.

A kernel counterexample and native negative show **clear depth 1 can shadow a
receiver beyond far**. This batch records that range obligation, not a runtime
fix or a claim that all production callers satisfy it. `standard.frag` gates
sampling by positive far and optional shadow range; those guards alone are not
a general proof that every biased receiver is at most far. Zero-distance face
selection likewise remains outside the preceding cubemap contract.

Admission's `make point-shadow-depth-check` checks six CPU witnesses: self,
blocked, bias equality, clear within/beyond far and non-exact radial arithmetic.
Fraction analysis separates measured encoding/decoding error from CPU comparison
error, relative to supplied native radii. Existing sqrt tolerance is reused;
actual texture storage/interpolation and GPU rounding remain external. Six tests
include matched-far failure controls and PCF bounds; textual source guards pin
radial write, strict comparisons, five taps, shader activation and capture/receiver
far uploads. They are drift guards, not formal C/GLSL semantic equivalence.

<!-- //ai: shared cubemap geometry contracts and numerical/source correspondence limits. -->
## Shared cubemap face geometry

`GfxCubemapGeometry` models the six GL face mappings, including −X `(sc,tc)=(z,-y)`.
It proves face-local coordinate recovery, direction recovery up to nonzero
major-axis scale, strict-interior face selection, positive-scale coordinate
invariance, UV bounds, two shared-edge identities and an asymmetric −X witness.
Canonical target/up frames produce right/up/back dot products equal to
`sc/tc/-major`; exact axis-aligned normalization is assumed by this frame model.
The shader selector explicitly gives ties to X, then Y, then Z. This is not a
claim about hardware cubemap tie selection or seamless filtering. Zero direction
is excluded from the native oracle; shader division at zero remains undefined
for this contract. The six-face model does not certify arbitrary shadow FOVs.

`make cubemap-geometry-check` checks 24 native LookAt/perspective witnesses using
a translated camera: asymmetric interiors, exact edges and taps outside each
face. Its fraction reference reroutes taps and checks direction recovery and
UV bounds; native projection correspondence uses existing tolerances. Source
guards pin point-shadow targets/ups, IBL capture arrays, inverse-face formulas
and the −X formula in surface/fog sampling. They are textual guards, not formal
C/GLSL semantic correspondence. Volumetric fog's duplicated −X mapping had
retained `-z`; it now uses `+z`, matching GL and the surface shader. GPU effect
gates provide regression evidence, not an exhaustive cubemap sampler certificate.

<!-- //ai: exact fixture witness separates a visible in-box hit from virtual box exit. -->
## Probe-box exit counterexample

`GfxProbeBoxExit` is an exact-rational witness using the owned fixture's probe
center, emitter AABB and `(50,12,50)` half-extents. Its selected rational ray
is a geometric witness, not a direction reconstructed from a captured pixel.
It hits the emitter at parameter 3, before its first probe-box exit at parameter
`67/5`. The direction from the probe center to that hit intersects the emitter;
both the virtual box-exit direction and the shader's component-scaled
expression miss it for this witness. This pins why box-exit projection can fail
for geometry inside the probe volume; it does not define a general visibility rule.
The rational model does not certify shader floats, GPU cubemap sampling,
occlusion by arbitrary geometry, or moving-scene behavior. Local probe
contributions remain unavailable unless those runtime obligations are met.

## Positive-w projection and clipping before division

<!-- //ai: exact homogeneous near-plane theory, not a replacement or certification of GPU clipping. -->
`GfxProjectionClipping` extends the corpus to 82 modules. For three nonzero-w
clip vertices it proves projected NDC signed area equals the X/Y/W determinant
divided by `wa*wb*wc`. With all w positive, signed orientation is preserved;
the `(ndc+1)/2` UV mapping scales area by 1/4 without changing its sign. A
kernel-checked mixed-w counterexample has positive homogeneous orientation but
negative divided area. Face classification before clipping is therefore unsafe
when a triangle crosses the camera plane.

The module models the **canonical near half-space z+w >= 0**, not an invented
w-epsilon GPU clip plane. A crossing edge's exact interpolation parameter lands
on the boundary and lies strictly between endpoints when the inside endpoint
is strictly inside. Standard ideal perspective with `0 < near < far` makes
near-plane membership imply `w >= near > 0`. Perspective depth is affine along
homogeneous edges, so this safe-w result also applies to the new intersection
vertices. A separate affine error-margin theorem keeps clip-z perturbation
allowance explicit instead of applying the exact projection formula to floats.

For one outside vertex A and inside B/C, the oriented clipped quad can be split
into two fan triangles. Their homogeneous determinants are `(1-s)*D` and
`s*(1-t)*D`; strict crossing parameters preserve orientation. Combined with
positive intersection/inside w, the divided fan orientation is preserved. This
does not formally verify every clipping topology, six-plane implementation,
GPU intersection precision, degenerate-face removal, raster rules or viewport
orientation. Division at zero in Lean's total rational field is not a model of
valid C/GLSL execution; the projection identity explicitly excludes zero w.

`make projection-clipping-check` runs in admission. Native CPU projection exports
positive, near-crossing and camera-crossing triangle clips. A fraction oracle
checks projection correspondence with unchanged math-pass tolerance, the exact
area identity, near-edge/fan identities, positive w margins including measured
clip-z arithmetic error, and oriented polygons from a six-plane reference
clipper. Four tests include mixed-w and zero-w negatives. The six-plane oracle
is checked on fixtures, **not** formally certified or claimed to execute GPU
clipping. In the camera-crossing fixture, divided original area is
`-0.6857144420430841`, while near-clipped fan areas are `9.87428796542041` and
`2.605714879763719` with minimum safe w .1. No culling/runtime/threshold changes.

Keep the C unit tests, SymPy math oracle, and native GPU probes as independent
implementation evidence. Do not replace them with a theorem count or successful
Lean build.
