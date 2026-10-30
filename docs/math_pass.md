# Math pass

The math pass is the numeric sibling of the Lean proofs: the proofs pin
the *semantics* (what the formulas mean), the math pass pins the *code*
(what the C actually computes) against an independent oracle.

## Tiers

- **Lean proofs** (`proofs/`, sibling of `master/`) — coordinate mappings,
  RHI state machines, draw protocols, the point-shadow face table, and the
  containment algebra. Core tier is blocking in `make check`
  (`make proofs`); the Mathlib tiers are opt-in (`make proofs-full`).
  See `../proofs/README.md`.
  Both tiers audit root coverage and transitive theorem axioms; see
  [`lean_proofs.md`](lean_proofs.md) for correctness-audit findings and limits.
- **Math pass** (`make math-check`) — the engine's gxmath corpus
  (`GX_MATH_DUMP=1`, implemented in `src/gxmath/gxmath_tests.c`) is
  recomputed by `scripts/math_pass.py` with SymPy: exact rationals for the
  algebraic cases, float64 formulas for the trig/sqrt cases. A slip in the
  C formulas fails here even when the engine's own unit tests agree with it,
  because the oracle comes from a different toolchain.

## Corpus

| Case | What it pins |
|---|---|
| `multiply` | the row-major matrix product (`gX_Matrix_Multiply`) |
| `invert` | `gX_Matrix_Invert` on a TRS-like matrix (the point-shadow guard's local-space step) |
| `transform` | `gX_F32v3_Transform` = `(M^T * (v,1))[0..2]` |
| `determinant` | `gX_F32m4x4_Determinant` |
| `perspective` | `gX_Matrix_Perspective` (standard GL form) |
| `rotate_xyz` | `gX_Matrix_RotateXYZ` = column-convention `Rz*Ry*Rx` (X, then Y, then Z) |
| `lookat` | `gX_Matrix_LookAt` = standard gluLookAt view, transposed |
| `vectors` | `gX_F32v3_Cross` / `Dot` / `Project` |
| `random` | `gX_Random32_*` raw sequence (anchored by the published pcg32-demo vectors for seed 42 / stream 54) and the unbiased bounded draw |
| `sdf` | `gX_F32_SDF_*` sphere/plane/cube/torus/cylinder/cone/capsule/octahedron/rounded-box/hex-prism/Menger distances |
| `sdf_ext` | box-frame and Mandelbulb distances plus the infinite/finite repeat helpers |
| `quaternion` | `gX_F32Qua_Multiply/Invert/Normalize/Lerp/Nlerp/Slerp` |
| `quaternion_rot` | axis-angle and Euler construction/decomposition, plus quaternion-to-matrix and matrix-to-quaternion |
| `matrix_aux` | transpose, add/subtract, elementwise multiply/divide, cofactor/adjugate, general frustum/ortho, serialization, and the four-row constructor |
| `matrix_small` | 2×2/3×3 determinant and trace helpers |
| `f64` | double-precision `gX_F64v4_Lerp`, emitted as `MATHDUMP64` |
| `edge_cases` | classified singular, zero-length and degenerate probes; each scalar exports `(kind, value)` with finite/+inf/-inf/NaN kinds |
| `easing` | `gX_F32_Easing*` Step/Linear/Quad/Pow/Bezier |
| `scalar` | `gX_F32_Clamp/Lerp/Normalize/Remap/Wrap` |
| `int_math` | `gX_I32_Factorial/GCD/LCM/nCr/nPr` (exact) |
| `color` | `gX_Color_Lerp/LerpRGB/Mul/ToV4` (u8 truncation + clamp semantics) |
| `physics` | Newton2GetAcceleration, Potential/KineticEnergy, LorentzLength |
| `relativity` | `gX_F32_Physics_Relativity*` — beta, gamma, contraction/dilation, Doppler, velocity addition, rapidity against a tunable `c` |
| `dipole` | `gX_F32v3_Physics_DipoleField`/`DipoleTorque`/`DipoleForce` plus the on-axis/equatorial/coaxial scalars, against the SI-exact `mu0/4pi` laws |
| `planck` | Rayleigh-Jeans + Planck radiance and the 650/520/460 nm RGB triple, checked against CODATA/SI constants (independent of `config.h`) |
| `fft_delta`, `fft_sine` | the forward DFT of a delta and a sine bin (unnormalized `e^{-2πikn/N}` convention) |
| `calculus` | Differentiate/Nabla/Diverge/Curl/Simpson/Newton against analytic values (fixed probes in the dump; the O(d) forward-difference bias sets the 1e-3 tolerance) |
| `convert_units` | foot/inch/meter, Kelvin/Celsius, SI prefix |
| `convert_color` | RGB↔HSV (the engine's 0..360 hue convention) |
| `convert_coords` | cartesian↔cylindrical↔spherical, including the round trips |
| `convert_sky` | equatorial↔ecliptic (23.4° obliquity) and equatorial↔horizontal at a parameterized observer (latitude/longitude in radians), with round-trip pins in both orders |
| `convert_new` | sRGB↔linear (exact IEC 61966-2-1), amplitude↔dB, MIDI note↔frequency, Celsius↔Fahrenheit |
| `convert_proj` | Vector2Horizontal/Equatorial and their inverses, with round-trip pins |
| `convert_ball` | space2ball/ball2space dome projection, with a round-trip pin |

<!-- //ai: quaternion vector action is homogeneous, while its native matrix requires unit agreement. -->
`GxMath.Quaternion` now models `gX_F32v3_RotateByQuaternion` and
`gX_F32Qua_ToMatrix` using the existing `GxMath.Matrix` numeric-field transform
convention. Twenty new contracts prove sandwich action `q * pure(v) * conj(q)`,
composition (apply b then a for q-product a*b), inverse restoration for
nonzero norm, antipodal sign invariance and exact unit-length preservation.
The homogeneous vector helper scales length by `normSq(q)`; it does **not**
implicitly normalize. The literal-one matrix diagonal gives the exact relation
`Transform(ToMatrix(q),v) = Rotate(q,v) + (1 - normSq(q))*v`. Matrix/vector
agreement therefore has an explicit unit-quaternion hypothesis. A scalar
quaternion `(0,0,0,2)` maps +X to 4X through the vector helper but leaves +X
unchanged through the matrix helper; this counterexample is proved.

`test_quaternion_rotation_contract.py` pins the C rotation, product, inverse,
matrix and vector-transform formulas plus the Lean matrix/vector convention
in `math-check`/`proofs-full`. Negative controls reject cross-term signs,
transpose mappings and an incorrect implicit normalization. Native probes
compile actual gxmath translation units under `build/`, using Hamilton
sandwich expectations, antipodal/unit/nonunit/inverse/composition witnesses,
and a +Z quarter turn taking +X to +Y. These bounded CPU witnesses retain the
`1e-4` math-property tolerance. They do not certify IEEE normalizer accuracy,
inverse conditioning or GPU upload. No C behavior or matrix convention is
changed.

<!-- //ai: interpolation branches are exact-real contracts plus bounded native float witnesses. -->
The same quaternion module now defines branch-aware `Lerp`, `Nlerp` and
`Slerp` models. Nlerp preserves unit norm when its linear blend is nonzero and
has exact endpoints for unit inputs. The native normalizer's zero-input
fallback is modeled explicitly: antipodal unit endpoints at `t=1/2` make
Nlerp return zero, not a unit quaternion. Slerp models sign correction,
the `|cos| >= 1` endpoint branch, the strict near-parallel `cos > 19/20`
Nlerp branch, the explicit epsilon/midpoint fallback, and the regular sine
weights. The regular formula proves unit norm for unit endpoints whose dot is
the cosine parameter; the branch theorem states the nonnegative cosine and
epsilon conditions needed to reach it. The exact-real `19/20` represents the
source's `0.95f` threshold, not bit-exact float comparison semantics. The
defensive small-sine fallback is modeled for arbitrary epsilon, and its
midpoint behavior is proved; on the regular interval the exact half-sine is
at least `sqrt(39/400)`, so it is unreachable for the source epsilon
idealization `1/1,000,000`.

Native actual-TU witnesses test Lerp/Nlerp/Slerp endpoints, identical and
antipodal inputs, zero normalization, negative-dot sign correction, both
sides and the exact boundary of the near-parallel threshold, regular trig
interpolation and extrapolation. Source-drift negative controls pin the C
branch order, strict comparisons, epsilon and Lean model. These tests are
bounded CPU evidence only; they do not certify IEEE rounding at the branch
boundary, universal trig/normalization accuracy or a universal C-to-real
execution correspondence. In particular, the epsilon-unreachable result is
an exact-real bound, not a claim about every rounded float comparison.

`GxMath.Sdf` mirrors the real-valued sphere, plane, cube, torus and capsule
formulas, including sphere sign/radius correspondence, plane linearity, the
torus center value, cube corner zero and the zero-length capsule reduction.
<!-- //ai: exact-real metric proofs are separate from native sampled bounds. -->
It also proves sphere and unit-normal plane distances are 1-Lipschitz, using
Cauchy-Schwarz and triangle/reverse-triangle inequalities in `GxMath.Vector`.
The general plane bound is scaled by the normal length; unit length is an
explicit hypothesis, not a proof that C's normalization is exact. Clearance
corollaries show displacements no longer than the current positive distance
cannot cross the surface (strictly shorter steps preserve strict positivity).
These are exact-real results, not float/GPU raymarcher safety certificates.
Other primitive Lipschitz bounds and cone/Menger behavior remain outside the
formal model. The numeric `sdf`/`sdf_ext` families remain the code-level oracle.

`GxMath.Scalar` mirrors clamp bounds/idempotence/saturation, lerp endpoints,
normalize/lerp inverse round trips and remap endpoint/lerp-normalize identities.
The numeric `scalar` family remains the code-level oracle; wrap, integer
factorial/GCD/combinatorics and tolerance equality are not yet modeled.

`GxMath.Fourier` models the same unnormalized forward DFT convention as the
numeric `fft_delta`/`fft_sine` corpus: linearity, zero response and the
delta/impulse exponential.
<!-- //ai: inversion is transferred through explicit formula/index bridges, not assumed. -->
It now defines the positive-sign, `1/n` inverse, proves its equivalence with
Mathlib's finite inverse through explicit Fin/ZMod index/character bridges,
and proves both inverse round trips and Parseval for every nonzero length.
Parseval is `Σ |DFT(x)|² = n Σ |x|²`, including both complex channels.
The iterative radix-2 implementation, sine-bin phase and IEEE error remain
outside these proofs. There is no native inverse FFT: `idft` is a specification.

`GxMath.Calculus` proves exact forward-difference constants/identity/square/
affine/linearity identities and one affine Newton step.
<!-- //ai: cubic integral exactness follows only after a bridge from the native parity loop. -->
Its Simpson model now mirrors the endpoint setup, interior indices 1..steps-1,
odd/even weights 4/2 and final `h/3` scale. The positive-even-step weighted
loop is proved equal to adjacent panels; polynomial primitive differences
telescope and are linked to Mathlib's signed interval integral. Thus every
degree-at-most-three polynomial is integrated exactly over the reals for a
positive, even step count, including reversed and degenerate intervals.

Zero steps return zero, not necessarily the integral. One step uses the
native trapezoidal fallback, exact for affine functions but not quadratics in
general (`x²` on `[0,1]` returns `1/2`, not `1/3`). C accepts odd steps; they
are not silently rounded or rejected by these changes. Three steps on the
constant one over `[0,1]` return `8/9`, not one. These counterexamples are
proved. The mathematical function assumes a pure nonnull callback and natural
indices; IEEE rounding, float/machine-index casts, null-callback behavior and
general convergence are not part of the integral-exactness theorem.

`test_simpson_contract.py` pins C/Lean branches, endpoint inclusion, parity,
sample indexing and scale in `math-check`/`proofs-full`, with mutation and
comment-masking negative controls. `test_simpson_native.py`, run by
`math-check`, compiles the actual `calculus.c` and `scalar.c` with
`scripts/simpson_probe.c` under a temporary `build/` subdirectory. It checks
80 bounded degree-0..3 even-step fixtures plus zero/one/odd steps and the
native null callback. Expected integrals use exact rational arithmetic;
native comparisons use the existing math-property `1e-4` relative scale.
These are finite CPU witnesses, not universal IEEE error bounds. The numeric
`calculus` corpus remains unchanged; the tolerance-based differentiation zero
guard and convergence remain outside the formal model.

<!-- //ai: channel quantization order and exact-real units have separate source witnesses. -->
`GxMath.Color` proves per-channel bounds, lerp endpoints with signed-delta
truncation before addition, normalization bounds/ideal round trips, and
normalized-multiply bounds, black/white identities and commutativity.
`GxMath.Units` proves both round-trip directions for foot/inch/metre,
Kelvin/Celsius and Celsius/Fahrenheit, plus the twelve-inch foot and freezing/
boiling reference values. Decimal literals are exact real constants in Lean.
These contracts do not bound IEEE rounding, validate physical temperatures,
or certify out-of-range float-to-integer casts. C lerp correspondence assumes
finite, representable intermediate casts and I32 addition; ideal byte round
trips do not imply exact float byte round trips. Full RGBA assembly, raw
casts, SI-prefix dispatch, color-space and coordinate/sky conversions remain
outside these modules.

`scripts/test_color_units_contract.py` runs in `math-check` and `proofs-full`.
It pins the relevant C and Lean formulas (including channel mappings and cast
order), rejects missing/duplicate witnesses, and mutation-tests the guard.
This is a narrow source-drift check, not a verified translation. The numeric
`color`, `convert_units` and `convert_new` probes remain independent evidence
for the actual C execution.

<!-- //ai: preserve the native Cartesian sky convention, including its negative horizontal orientation. -->
`GxMath.Sky` proves both equatorial/ecliptic and equatorial/horizontal inverse
round trips, squared-length/length preservation for all four helpers, and
orientation. The ecliptic pair uses arbitrary real obliquity; C supplies
`23.4f * gX_DEG2RAD`. Horizontal latitude/longitude stay explicit in radians.
The ecliptic frame preserves handedness (+1); the horizontal frame reverses
it (-1) while preserving lengths. The C formulas are unchanged. These are
Cartesian identities for arbitrary vectors, including zero/nonunit inputs;
they do not prove IEEE trigonometry, degree-constant accuracy, physical
observer validity or angular-coordinate branch/radius conventions.

`scripts/test_sky_contract.py` pins the four C/Lean formulas in `math-check`
and `proofs-full`, including signs, observer parameters and the shipped
obliquity setting. Negative controls reject constant/axis changes, hardcoded
observers and a wrong-obliquity pair that could still round-trip. This is a
narrow source-drift witness, not a verified C-to-Lean translation. The native
`convert_sky` property checks now cover each direct helper's length and every
existing output triple; synthetic zero/asymmetric, corrupted-slot and
reciprocal-scaling controls exercise the checker without changing tolerances.

<!-- //ai: coordinate singularities and angle branches are theorem hypotheses, not hidden C fixes. -->
`GxMath.Coordinates` models the six cylindrical/spherical helpers. Fields are
`(radius, azimuth, height)` for cylindrical and `(radius, azimuth, inclination)`
for spherical; inclination is measured from +z, not altitude. Angles are
radians. Ideal `atan2(y,x)` uses `Complex.arg (x + iy)`, with principal range
`(-π, π]` and `arg(0)=0`. The two azimuth-wrapping theorems use `toIocMod` with
period `2π`; raw angles outside the principal range are not claimed to return
unchanged.

| Exact-real round trip | Domain |
|---|---|
| Cartesian → cylinder → Cartesian | All vectors in the ideal model, including the axis |
| Cartesian → spherical → Cartesian | Positive total radius; nonzero poles are allowed |
| Cylinder → Cartesian → cylinder | Positive cylindrical radius, azimuth in `(-π, π]` |
| Spherical → Cartesian → spherical | Positive radius, azimuth in `(-π, π]`, inclination in `(0, π)` |
| Spherical → cylinder → spherical | Positive radius, inclination in `[0, π]`; direct helpers copy azimuth even at poles |
| Cylinder → spherical → cylinder | Nonnegative cylindrical radius and positive total radius |

Zero spherical radius erases both angles, and either pole erases azimuth
after a Cartesian detour. These ambiguities have explicit reconstruction
theorems, not invented inverses. The C spherical origin is excluded: real
division is totalized at zero, whereas native division/acos may yield NaN.
Signed-zero `atan2` branch endpoints, IEEE errors, underflow/overflow and
exceptional native behavior remain outside the claims. No C code is changed.

`scripts/test_coordinates_contract.py` pins the six C/Lean formulas and xy
bridge in `math-check`/`proofs-full`; axis-order, asin-vs-acos, field and joint
C/Lean drift controls fail closed. This is a narrow source witness, not a
verified translation. Native `convert_coords` checks now cover all six output
triples, radial/height invariants and angular ranges. Synthetic quadrant,
nonzero-pole, corrupted-slot and invalid-domain controls exercise the checker;
native probe counts and tolerances are unchanged.

Tolerances: `1e-5` relative for the exact cases (float32 round-off),
`1e-4` for the trig/sqrt cases (float32 vs float64 evaluation). The
quaternion, SDF-extension and matrix-auxiliary cases are fixed probes over
ordinary inputs; they do not certify singular, degenerate or non-finite
behavior. `edge_cases` records the named degenerate probes explicitly, but it
is not an exhaustive edge-case proof.

## Running

<!-- //ai: the gate must fail closed on malformed dumps and oracle/native arity mismatches. -->
`scripts/test_math_pass.py` pins the gate's failure behavior: a non-zero dump
status, malformed or duplicate cases, count mismatches, non-finite values and
oracle/native output-arity mismatches all fail instead of being skipped.

<!-- //ai: seeded randomized probes complement the fixed corpus without replacing edge contracts. -->
`make math-random-check` runs 64 seeded probes across 20 families (1,280
probes total) for vectors, full and small matrices, quaternions, scalars,
easing, FFT, calculus, color helpers, SDF primitives and extensions,
electromagnetism, dipoles, and all conversion families. The randomized calculus family uses
`2e-3` because float cancellation in the forward difference can exceed the
fixed corpus's `1e-3` on unlucky points; color uses its `5e-2` truncation
tolerance, and the other families stay at `1e-4`. The native dump
(`GX_MATH_DUMP=1 GX_MATH_RANDOM_DUMP=1 GX_MATH_RANDOM_PROBES=<n>`) exports
exact inputs and outputs as hex floats; `scripts/math_random_check.py`
recomputes each probe independently. Admission uses 64 probes per family;
`make math-soak-check` runs both differential and invariant checks at 1,024
probes per family (`MATH_SOAK_PROBES=<n>` overrides). The generator keeps
matrices triangular/invertible and vectors nonzero, so singular and degenerate
behavior remains covered by `edge_cases` rather than this differential pass.
This is a fixed-seed black-box check, not a proof.

<!-- //ai: invariant checks avoid reusing the differential formula oracle. -->
`make math-property-check` reuses the seeded dump but checks identities rather
than recomputing the same formulas: matrix inverse round trips and determinant
multiplicativity; vector/cross/projection orthogonality; quaternion norms,
inverse identity and rotation norm preservation; FFT Parseval and DC sums;
coordinate/sky/projection/ball and RGB/HSV, unit and physical-unit round
trips; exact-SDF and box-frame Lipschitz bounds (cone/Menger excluded); color
channel bounds; scalar/easing
bounds; repeat/SDF half-cell behavior; and field/torque orthogonality. At the
default count it covers 1,152 probes across 18 families and is independent of
the formula oracle; `math-soak-check` reuses the same checks with the larger
probe count.

<!-- //ai: numeric inverse reconstruction checks phase/order that energy and DC alone miss. -->
The FFT property check additionally reconstructs each native eight-sample
complex signal with the positive-sign, `1/8` inverse specification. This is
sampled native-forward evidence, not a test of a shipped inverse function.
Shifted-complex and imaginary impulse controls exercise both channels; wrong
normalization and reversed frequency order must fail. The reversal preserves
energy and DC, so the negative control specifically requires inverse
reconstruction to reject it. Tolerances and probe counts are unchanged.

<!-- //ai: synthetic negative controls exercise the existing SDF bound checker. -->
`test_math_property_check.py` includes sharp sphere/plane bound fixtures,
radius-offset and normal-sign invariance, all exact-primitive output-slot
violations, a nonunit-plane negative control, and explicit cone/Menger
exclusions. These test the checker itself; the native seeded dump supplies
the sampled C evidence. Existing tolerances and excluded families are unchanged.

<!-- //ai: the mutation gate validates the gate itself; it is too expensive for admission. -->
`make math-mutation-check` is an opt-in gate self-test. It copies the source to
`GX_SCRATCH` (default `/tmp/opencode`), builds a disposable tree, verifies a
green math baseline, then applies five one-line formula mutations separately
(matrix transpose, quaternion product, sphere distance, foot conversion, vector
cross). Each mutation must turn at least one of `math-pass`, `math-random` or
`math-property` red. It is not part of `make check` because it performs a full
disposable build; use `--keep` on the script to inspect the tree.

<!-- //ai: empirical rounding witnesses are separate from exact stored-float algebra. -->
`make reprojection-residual-check` builds and checks a separate headless native
dump (`GX_MATH_DUMP=1 GX_REPROJECTION_DUMP=1`), also run by `make check`.
`scripts/reprojection_residual_check.py` reads exact hexadecimal float exports
into Python fractions and checks the `GfxReprojectionError` residual, normalized
world, previous clip and U/V/depth bounds. Five oracle unit tests exercise row
conventions, missing/unknown fixtures, malformed/non-finite data, and a false
singular label. A JSON report is available with `--report PATH`.

The finite fixtures use one ordinary perspective camera and three synthetic
conditioning stress matrices (diagonal, nearly dependent, Hilbert); the latter
are algebra probes, not certified camera configurations. A fifth fixture rounds
a perturbation away, producing the expected singular/non-finite output. Native
CPU float row actions mirror the mathematical path but do not execute GLSL.
Residual measurement uncertainty is the exact difference between the stored
float residual and its double evaluation. CPU projection uncertainty is measured
against exact stored-float arithmetic for **this fixture**, not predicted for
unseen inputs. Observed-error containment checks therefore test the algebra and
triangle decomposition, not a universal rounding bound. Accuracy comparisons
with .05 axis pixels/.0005 depth are advisory, with no tolerance changes.

Initial perspective witness: exact residual `2.3507141122536268e-7`, observed
X error `1.708666483561198e-5` pixels, X bound including measured CPU uncertainty
`0.009392110537002675` pixels; observed depth error `3.9736429850260414e-8`.
The relevant **right-product** Hilbert residual is `0.0010058805637527257`
(distinct from the earlier maximum of both products). Its observed Y error is
`0.27278900146484375` pixels, exceeding .05, while its conservative Y bound is
`28.675144274901967` pixels. The checker passes containment and reports that
accuracy miss explicitly; it does not claim the Hilbert fixture meets the budget.
The weighted companion `GfxWeightedReprojection` preserves componentwise
absolute residual sums, zero normalized-W error, and previous-camera column
weights. The oracle checks intermediate and projected containment and refinement
against the uniform bound; three additional tests pin column indexing, sparse
components and normalized W. With the same empirical CPU deviations, Hilbert's
Y bound improves to `0.5666920080280106` pixels and perspective's X bound to
`0.00006782470344206597` pixels. Hilbert still misses .05; no thresholds change.
GPU execution, input texture quantization, and inverse accuracy policy remain
outside this gate.

<!-- //ai: depth input decomposition uses a simulated grid, not an assumed driver format. -->
`make depth-input-check` (also in `make check`) exercises native perspective
near/mid/far samples via `DEPTH_INPUT` hex exports in the same opt-in dump.
`scripts/depth_input_check.py --report PATH` compares exact fractions against
`GfxDepthInputError` clip/inverse/normalization bounds, combines the baseline
inverse-residual budget, and records CPU arithmetic deviation separately.
Four unit tests pin factors of two, current jitter subtraction, denominator
contributions and invalid/missing input handling. The grid is a **simulated**
nearest UNORM24 grid: depth-forward error and float conversion of the grid value
are additional terms, not silently included in the half-grid-step guarantee.
Resolved depth allocation is unsized on raylib GL3.3, so these samples do not
certify actual texture precision or GPU sampling. Depth/jitter perturbation
and inverse residual remain separate report fields; accuracy budgets stay
advisory and unchanged. See [`lean_proofs.md`](lean_proofs.md) for the contracts.

<!-- //ai: empirical precision is tested on live driver allocations without changing them. -->
`make gpu-depth-precision-check` is a separate GPU gate: live allocation queries
plus disposable format-matched constant-depth raster/sampler/4x-resolve fixtures.
Five parser negative controls reject missing/duplicate data, GL errors, restore
failures, missing distance samples and unequal MS samples. Reports compare each
resolved depth against the exact perspective oracle, separating submitted float
error, storage/sampling error, and resolve error. The nearest-grid comparison is
conditional/advisory, not a guarantee of driver format or universal rounding.

<!-- //ai: exact unit geometry is separate from native normal and LookAt approximation. -->
`make planar-geometry-check`, included in admission, exports headless
`GX_MATH_DUMP=1 GX_PLANAR_GEOMETRY_DUMP=1` camera witnesses. Axis and oblique
planes pin point-vs-direction reflection, native LookAt, view/projection order,
row clip coordinates and UV mapping. Four tests also check source formulas and
negative controls. The checker reuses existing math-pass tolerances; reports
normal length/plane-membership defects and involution/distance/UV errors rather
than claiming exact rational theorems certify float execution. See
[`lean_proofs.md`](lean_proofs.md) for exact geometry contracts and limitations.

<!-- //ai: rebuilt LookAt is not the raw orientation-reversing reflected frame. -->
`make reflection-handedness-check`, also in admission, reuses the asymmetric
native dump with its appended original LookAt matrix. It checks column bases,
negative orientation of the raw reflected frame, positive orientation of native
LookAt, mirrored right/up/back axis identities and one positive-w projected
triangle's conditional winding relation. Four unit/source tests reject basis
row/column confusion and wrong winding assumptions. Existing math-pass tolerances
are unchanged; actual GPU culling and float normalization are not certified.

<!-- //ai: camera-crossing triangles must be clipped before perspective face classification. -->
`make projection-clipping-check`, included in admission, exports native clips
with `GX_MATH_DUMP=1 GX_PROJECTION_CLIP_DUMP=1`. A fraction oracle checks the
positive-w determinant/area relation and canonical near intersections/fans,
including explicit native clip-z error margins. Positive, near-crossing and
camera-crossing fixtures also exercise an exact six-plane reference clipper.
Four tests pin fan identities, mixed-w sign reversal and zero-w/missing-data
rejection. Only near-plane identities are formally proved; the six-plane oracle
is fixture evidence, not a GPU clipping/raster or universal IEEE certificate.

<!-- //ai: canonical cubemap frame math; hardware seams/filtering remain external. -->
`make cubemap-geometry-check` exports 24 native capture witnesses through
`GX_MATH_DUMP=1 GX_CUBEMAP_GEOMETRY_DUMP=1`. A fraction oracle checks asymmetric
six-face interiors, ties and out-of-face tap rerouting, with unchanged numeric
tolerances. Source guards cover point-shadow and IBL frames and the corrected
fog −X formula. Canonical 90-degree projection only; hardware filtering, custom
shadow FOV and GPU arithmetic are not certified by these fixtures.

<!-- //ai: scalar depth comparison witnesses, with native errors separate from GPU precision. -->
`make point-shadow-depth-check` uses `GX_MATH_DUMP=1 GX_POINT_SHADOW_DEPTH_DUMP=1`
to export six radial encoding/comparison cases. Exact fractions separate
encoding/decoding and CPU subtraction/multiplication errors relative to native
supplied radii. Existing sqrt tolerance is unchanged; texture quantization,
filtering and GPU arithmetic are not certified. Equality is lit; clear depth
one is not automatically lit beyond far. Six tests cover bias, far mismatch,
range, error containment, PCF bounds and source drift.

<!-- //ai: CSM scalar branch transcriptions and native ortho bounds, not GPU cascade coverage. -->
`make csm-contract-check` runs 36 selector/fade CPU transcriptions and three native
orthographic projections via `GX_MATH_DUMP=1 GX_CSM_CONTRACT_DUMP=1`. Existing
tolerances are unchanged. Exact boundaries, counts and negative-depth center
acceptance are pinned; tests retain equal-split jumps and overlapping-fade
counterexamples. Hardware depth/filtering, view-depth reconstruction and fog's
distinct ray-distance selection are not certified by these fixtures.

<!-- //ai: GTAO scalar/native proof witnesses and separate GPU stripe/contact regression. -->
`make gtao-contract-check` checks source formulas and seven scalar tests plus the
existing native asymmetric perspective inverse fixture, including a double-flip
negative. Exact proofs retain inverse/normalization and sampled-error assumptions.
`make gtao-stripe-check` uses disposable presets and raw/blur dumps to pin flat
slab stripes and contact shadows with the unchanged authored .025 bias, static
and rotating taps, and a zero-bias self-occlusion negative. It is a separate GPU
gate; source guards/native fixtures do not prove GPU sampler or pow execution.

<!-- //ai: SSGI fallback-order/tangent controls and isolated flat/bounce GPU evidence. -->
`make ssgi-contract-check` reuses the native asymmetric perspective witness with
six scalar/source tests for tangent sign, 48-candidate confidence and
normalize-before-fallback behavior. `make ssgi-stripe-check` checks raw/final
flat-only slab profiles with static/rotating taps plus nonzero localized bounce;
it does not certify physical GI completeness, HDR bounds or feedback convergence.

```bash
make math-check            # gate (builds first, then the SymPy oracle)
make math-random-check     # seeded differential probes (also in make check)
make math-property-check   # invariant identities (also in make check)
make math-soak-check       # opt-in high-count differential + invariant soak
make math-mutation-check   # opt-in gate self-test (disposable build)
python3 scripts/math_pass.py --binary build/gX_build   # directly
python3 scripts/math_random_check.py --binary build/gX_build
python3 scripts/math_property_check.py --binary build/gX_build
GX_MATH_DUMP=1 ./build/gX_build | grep MATHDUMP        # raw corpus
GX_MATH_DUMP=1 GX_MATH_RANDOM_DUMP=1 ./build/gX_build | grep MATHRANDOM
```

The gate needs `python3` + `sympy` (the same pattern as the proofs gate
needing `lake`). It is part of `make check`.

## Extending

Add a case to `gX_Tests_MathDump` in `src/gxmath/gxmath_tests.c` (document
the float layout in its header comment), mirror the layout in
`scripts/math_pass.py` (`CASES` + a `case_*` oracle), and re-run
`make math-check`. Prefer exact rational inputs for algebraic cases — the
oracle then verifies to zero error — and reserve float64 oracles for the
trig/sqrt surfaces.
