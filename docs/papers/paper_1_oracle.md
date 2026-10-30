# Paper 1 — The Rasterizer's Oracle: progressive pathtracing as an audit instrument

*Two-minute version: a game engine that can't check its own lighting is
flying blind. One sample per pixel, a new seed every frame, accumulated
through the TAA history buffer you already own — and every lighting audit
becomes a number instead of a judgment call.*

## Thesis

Every effect audit in this engine ends the same way: a human stares at two
frames and declares the delta "physical" or "wrong". That worked for 21
items because the scenes were simple and the auditors were stubborn. It
does not scale to PBR response curves, fog inscatter integrals, or SSR
fidelity percentages — all quantities with analytically known answers that
we currently verify by squinting.

A 1-sample-per-pixel Monte Carlo mode with per-frame seeds, accumulated
over frames, converges to ground truth. It does not need to ship, run
fast, or look good at frame 1. It needs to be *right* at frame 1000, so
the rasterizer can be wrong in measurable units.

## Design (concrete, in this tree)

- **Reuse, don't build.** `taa.c` already owns: a Halton(2,3) jitter
  sequence (`update.c` inline block), a history RT
  (`taa.historyMap`), and an EMA resolve (`gfx_taa_resolve.frag`,
  `blendFactor`). Oracle mode = resolve with `blendFactor` driven to
  `1 - 1/N` (running average, not exponential), jitter replaced by a
  per-frame RNG seed uniform, accumulation count in frames.
- **Swap the lighting, keep the transport.** The oracle replaces
  `pbr_direct_light` / `phong_*` calls with a 1-bounce Monte Carlo
  estimator (cosine-weighted hemisphere sample + explicit sun/point
  sampling, MIS optional later), reading the SAME albedo, roughness,
  metalness, lights, and visibility the rasterizer uses. Same inputs,
  two integrators — the diff is pure integration error.
- **Three first gates** (each becomes a suite row with a threshold):
  1. **PBR response**: still life (sphere grid × roughness ladder) vs
     oracle — mean abs diff per roughness band. (Would have caught the
     GGX low-roughness Inf class (M6) mechanically.)
  2. **Fog inscatter**: dense-fog wedge vs oracle single-scatter
     integral — validates phase, transmittance, and sun normalization
     (would have caught the double-pi class (item 8) and the xPI
     double-count).
  3. **SSR fidelity**: mirror-still-life vs oracle mirror — the current
     colorimetry oracle, but with exact expected values instead of
     hand-set gates.

## Non-goals

- Not a shipping renderer. No denoiser, no realtime budget, no
  MIS-to-start, no spectral anything. Ugly at frame 1 is fine.
- Not a replacement for the analytic audits. The oracle settles
  *numbers*; humans still settle taste (defaults, look, feel).

## Cost estimate

Small-to-medium. One shader variant (`standard_oracle.frag`, forked from
`standard.frag`, lighting functions swapped), one accumulation mode in
`taa.c` (weight override + seed uniform + frame counter), three suite
rows. No pipeline changes — it renders through the existing G-buffer
path and reads back like any other capture.

## First step (spike)

Accumulation-mode resolve behind `GX_T_ORACLE=1`: force
`blendFactor = 1 - 1/frameIndex`, freeze jitter, verify a static scene
converges pixel-stable (variance → 0 over N frames). That spike alone
proves the machinery; the Monte Carlo estimator follows.

## M1 log — transport slice (IN PROGRESS, deterministic gate added)

- `GX_T_ORACLEPATH=1` now replaces the diffuse IBL term with a cosine-weighted
  hemisphere estimator over the same sky cubemap. It also enables the existing
  unbiased uniform direct-light estimator; the sun remains explicit and exact.
- `GX_T_ORACLERAYS=1..4` averages with-replacement samples. The hook is off by
  default, and all GL33 production paths retain the pre-existing irradiance,
  direct-light, and TAA behavior.
- `make oracle-path-check` uses `--reflectrow` as the fixed roughness/metalness
  ladder, disables post effects, and checks activation, variance reduction
  (K=2 versus K=4), and repeat stability. This certifies the estimator
  machinery, not yet the full PBR band-error oracle promised above. Current
  NVIDIA run: activation MAE 1.4037, K=1/K=4 MAE 1.0576, K=2/K=4 MAE
  0.5298, repeat MAE 0.000000.

## M2 log — CPU multiple-bounce reference (IN PROGRESS, first gate added)

- `scripts/oracle_cpu_path.py` parses the shipped primitive GLBs, instantiates
  the reflectrow layout, builds a triangle BVH, and evaluates cosine diffuse
  transport with explicit sun/point next-event samples and an analytic sky
  miss. `--bounces 0/1/2` means direct-only, one secondary bounce, or two
  secondary bounces.
- `make oracle-bounce-check` uses a small matte fixture so indirect energy is
  visible independent of the production mirror materials. It gates finite
  radiance, observable first/second-bounce deltas, sample convergence, and
  bit-exact repeat determinism.
- `scripts/oracle_hdr_check.py` adds the opt-in raster parity row. The renderer
  uses `GX_T_ORACLE_MATTE=1` to evaluate a Lambert direct-light fixture and
  `GX_T_HDRDUMP=1` to read the linear RGBA16F scene target before tonemapping.
  The CPU reference now matches the actual raylib material defaults, the
  row-pinned camera, gX's transposed S*R*T transform, and the final sun default
  from `Light_Init`. `make oracle-hdr-check` gates geometry-pixel MAE, max
  error, and repeated HDR determinism; all hooks remain off in production.

## M3 log — direct GGX/specular parity (DONE, direct gate passing)

- `GX_T_ORACLE_GGX=1` enables a direct-only Cook-Torrance fixture using the
  existing reflectrow metalness/roughness ladder. IBL, shadows, emissive, and
  post effects remain disabled so the raster branch and CPU reference share
  the same sun/point-light contract.
- `oracle_cpu_path.py` now carries per-triangle roughness and evaluates the
  same clamped GGX distribution, Schlick-GGX geometry, and Schlick Fresnel
  terms as `standard.frag`.
- `make oracle-hdr-check` runs the M2 Lambert, M3 GGX, and M4 bounce rows. GGX is gated by
  linear-HDR MAE plus roughness-band MAE/P95 thresholds; the low-roughness
  band uses P95 because one resolved subpixel lobe can legitimately exceed a
  broad-image maximum without indicating lobe drift.

## M4 log — opt-in GPU environment-bounce parity (environment slice passing)

- `GX_T_ORACLE_BOUNCE=1` enables a fixed one-secondary cosine sample at each
  rasterized primary surface. The secondary miss evaluates the same analytic
  sky/sun environment as the CPU reference; primary surface-to-surface GPU
  rays remain deferred until a GPU acceleration structure exists.
- `oracle_cpu_path.py --transport bounce` mirrors the GPU hash and the
  bottom-left `gl_FragCoord` row convention. `make oracle-hdr-check` compares
  the linear HDR result against the matte direct baseline, gates secondary
  energy activation, and requires exact repeat captures.
- The hook is isolated from production defaults. This is a transport-parity
  slice, not yet the deferred GPU multi-bounce or surface-ray milestone.

## M5 log — opt-in GPU surface-bounce path (GL4.3 gate)

- `GX_T_ORACLE_GPU=1` enables an isolated GL4.3 compute/SSBO pass. It uploads
  the loaded scene triangles, transformed vertex normals, and resolved matte
  albedo values, traces the primary camera ray and one deterministic cosine
  secondary ray, and adds either secondary direct lighting at a surface hit or
  the analytic environment on a miss.
- The compute pass runs after raster transparent content and before SSR inputs,
  writes the existing RGBA16F HDR target through an image binding, and uses
  explicit shader-image/texture-fetch barriers. Unsupported contexts and
  lowered GL policy levels leave the hook inert; GL33 production behavior is
  unchanged.
- `scripts/oracle_gpu_check.py` compares the GPU result with the CPU triangle
  reference (`render_gpu_surface_bounce`) and requires an exact repeated HDR
  capture. Run it with `make oracle-gpu-check` on a GL4.3+ display; the current
  GL33 display intentionally reports the capability prerequisite instead of
  claiming a GPU parity result.

## M6 log — opt-in GPU two-secondary transport

- After M5 one-secondary parity passed on the opt-in `GX_COMPUTE=1` GL4.3
  build, the compute kernel gained a bounded two-secondary mode selected by
  `GX_T_ORACLE_GPU_BOUNCES=2`. Each surface hit contributes direct lighting,
  multiplies throughput by the resolved matte albedo, and launches the next
  deterministic cosine sample; a miss contributes analytic environment light
  and terminates the path.
- The default remains one secondary. `scripts/oracle_gpu_multibounce_check.py`
  compares the two-secondary result with the CPU reference and checks exact
  repeated HDR capture through `make oracle-gpu-multibounce-check`.

## M7 log — GPU BVH traversal

- The compute path now builds a deterministic binary triangle BVH on the CPU
  upload boundary and sends nodes plus leaf indices through SSBO bindings 3
  and 4. The shader traverses that structure with a fixed 64-entry stack,
  preserving the existing M5/M6 hit contract while avoiding a full-scene
  triangle loop for every ray.
- M5 and M6 parity were rerun after the traversal change and remain within
  their existing HDR gates. The path stays opt-in and the default GL33 build
  does not allocate or dispatch the BVH resources.
- On the validation NVIDIA display, the profiled one-secondary path measured
  0.89 ms at 48x32, 2.61 ms at 320x180, and 7.44 ms at 640x360; two
  secondaries measured 0.92 ms, 3.45 ms, and 9.34 ms respectively. These are
  workload observations, not portability thresholds.

## M8 log — opt-in GPU perfect reflection transport

- `GX_T_ORACLE_GPU_REFLECTION=1` selects a deterministic perfect-specular
  secondary ray from each rasterized primary surface. A hit evaluates direct
  GGX lighting with the uploaded secondary material; a miss evaluates the
  analytic environment. Primary Fresnel throughput uses the uploaded
  albedo/metalness pair, while the raster fixture supplies the primary direct
  Lambert baseline.
- `scripts/oracle_gpu_reflection_check.py` compares this result with the CPU
  `render_gpu_reflection` contract through `make oracle-gpu-reflection-check`.
  The initial GL4.3 run passed with MAE 0.001147, P95 0.001861, maximum
  0.004933, and exact repeated capture. Rough glossy sampling and deeper
  reflective bounces remain later slices.

## Spike 1 log — accumulation machinery (DONE 2026-09-10)
- GX_T_ORACLE=1 override live in taa.c (running-average weight 1-1/N,
  one-shot log proof). Jitter keeps running (stronger test than frozen).
- MAJOR incidental find: headless captures were NOT hermetic - the real
  mouse (wheel/drag over the window) moved the orbit camera mid-run.
  Repeat-vs-repeat measured 16.6%/4.4 UNTIL the hermetic gate
  (showcase.c ignores camera mouse input under --ai-screenshot*), then
  0px exactly. All pre-gate "divergence/drift" readings from this spike
  (65% oracle-vs-EMA, trio creep) are RETRACTED as mouse contamination.
- Measurement floor found: on converged static content the override is
  8-bit-invisible (running-average and EMA share the fixed point to
  sub-LSB; f8/f60/f300 cross-mode all 0px). ORACLE300-vs-600 still evolves
  1.2%/0.21 (max-211 spikes under study). The real discriminator needs
  varying signal (the seeded Monte Carlo stage) or float readback - NOT
  more PNG A/Bs. Documented so the estimator stage instruments first.
- Suite PASS (hook off by default, zero behavior change).

## Spike 2 log — float instrument + weight-contrast falsified (DONE 2026-09-10)
- Instrument: per-10th-frame resolveMap float readback (mean abs update
  magnitude vs previous snapshot), gated GX_T_ORACLE (override on) or
  GX_T_TAASTATS (stats only). Pipeline stall accepted; default runs pay
  nothing (getenv + dead branch).
- Result: oracle and EMA series are near-IDENTICAL (f10 0.50 baseline,
  ripple floor ~1e-3, f80 bump, f290 15x jump in both). The update
  magnitude is scene-event-driven; blend weights do not move it. The
  1/N-vs-plateau contrast hypothesis is FALSIFIED for this metric - both
  modes share the fixed point and the clamp/jitter floor dominates.
- The real discriminator remains varying per-frame SIGNAL (the seeded
  Monte Carlo stage: 1/N averaging provably beats EMA windowing on
  variance), now measurable with this instrument instead of 8-bit PNGs.
- Open observation (not chased): deterministic ~15x updatemag upset at
  f~285-290 in both modes, no log companion. Scene-content churn, origin
  unidentified - revisit with the estimator running.
- Suite PASS (hooks off by default).

## Spike 3 log — seed + PCF rotation (DONE 2026-09-10)
- Per-frame seed (frameCount, deterministic - hermetic repeats 0px)
  uploaded as oracleSeed/oracleSeedActive (model.c per-draw, hook
  GX_T_ORACLESEED, default off); rigid rotation of PCF + Poisson taps
  in shared shadow.glsl (sole includer: standard.frag). Blocker search
  deliberately unrotated (stable penumbra; filter taps carry the variance).
- Response is WEAK: seed on/off single-frame 0.19%/max-16 (tight taps,
  small penumbrae at this angle); ORACLESTATS series unchanged (floor).
  Deterministic (0px repeats). Proves seed flow end-to-end; NOT yet a
  sufficient variance source - the 1-bounce Monte Carlo estimator remains
  the real variance stage (this rotation becomes its shadow-sampling half).
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK (hooks off by default).

## Spike 4 log — estimator + two real TAA bugs (DONE 2026-09-10)
- Estimator: uniform single-light pick (PBR + Phong loops, sun exact,
  N-scaling unbiased incl. deterministic skips), PCG-style integer hash
  of pixel + frameCount seed, hook GX_T_ORACLEEST. Deterministic
  (hermetic repeats 0px). Raw response violent (TAA-off f60-vs-f61 47% /
  18.8) - real per-frame variance at last.
- CORRECTIONS to spikes 1-2 (read them with this): the "8-bit floor" and
  "weight-contrast falsified" conclusions were drawn from a DEAD OUTPUT.
  Pass order runs present BEFORE taa; with no compositeTarget (TAA-only
  configs) the resolve wrote hdrScene post-present, which next frame's
  opaque repainted - TAA never reached the screen. The matrix taa row
  measured jitter-only. Fix: extra canvas blit when taaSrc != canvas.
- Second bug in the same hunt: velocityMap/hasVelocityBuffer locs were
  never queried (stayed struct-zero 0, hitting location 0 = blendFactor:
  pre-fix TAA ran blend=1.0 with no velocity path, always). Two-line
  query fix in update.c. (blendFactorLoc=0 is REAL - alphabetical first -
  verified by fresh-query match; the zero that matters was the
  never-queried pair.)
- Proof the chain is live now: NOVELOCITY-vs-buffer under spin DIVERGES
  2%/0.31 (was 0px through three sessions of trying); matrix taa row
  moved 0.0050->0.0035 in-gate (accumulation, not just jitter); suite
  bit-identical.
- Method note: this whole find chain (locs -> green-absence -> present
  order -> routing) took six TEMP probes, two voided by stale binaries
  and one by a mid-probe timeout. Kept: only the two fixes + estimator.
- Gates: suite PASS, matrix PASS, VALIDATE 0, kernel OK.

## Spike 5 log — convergence (DONE 2026-09-10, estimator UNBIASED, loop bias OPEN)
- HDR verdict (linear space, the only space unbiasedness is defined in):
  REF hdrScene mean 0.403179 vs EST single-frame 0.403195 (ratio 1.00004,
  1.5M px, TAA=0, BLOOM=1 for the stats gate). Estimator unbiased. The LDR
  2% shortfall (83.64 vs 85.40, brights-only quantile) is presentation
  clipping of N x spikes at the RGBA8 backbuffer - a capture artifact.
- Accumulation converges: updatemag 0.49 -> 0.0009 (590f), smooth, no
  resets, both GL implementations (NVIDIA + llvmpipe agree).
- BUT NOT TO THE REFERENCE. Accumulated mean drifts below input:
  -11% @590f (clamp active) -> -7% (clamp bypassed, see below).
- Clamp ratchet FOUND + FIXED: TAA variance-clipping clamps history into
  the CURRENT frame's 3x3 box; for skewed single-samples the box min is
  ~dark, ratcheting the mean down every frame. Bypass: `oracleAccum`
  uniform (hook GX_T_ORACLE, default 0 = production path byte-identical),
  loc queried at the LIVE site (update.c - the first attempt went into
  TAA_CacheLocs, a DEAD TWIN never called; caught by bit-identical
  rerun numbers). Bias -11% -> -7%, slope -0.70 -> -0.84.
- RESIDUAL X (OPEN): uniform x0.9997-0.9998/frame drift, stochastic-input
  only, saturating ~~ -8%, edges spared (0.957 vs interiors 0.940 -
  depth-reject resets protect edges), rate implementation-modulated
  (NVIDIA ~0.8e-4, llvmpipe ~2.6e-4 rel/frame - precision-scented).
  Isolated by differentials, mechanism unidentified after exhausting the
  list (shader steps proven unbiased by replacement-independence,
  copies/blits exact per NOVELOCITY-off tracking + control, formats all
  HDR16F, blend Konrad Adenauer (alpha 1 everywhere), input witnessed FLAT
  0.3346 in-loop while resolve drifts 0.331 -> 0.307, fallback/NOVELOCITY
  path flat 0.334 (fast-EMA witness: input stable), short-memory EMA
  sits -2.3% (biased fixed point, kills slow-leak-only models)).
- Instruments landed: ORACLESTATS rgbmean (resolve RGB) + inputmean
  (taaSrc BEFORE resolve draws - post-draw reads witness our own output
  through the routing copy), TEX_STATS HDR mean, ETA-decay slope reader.
- Interlude (NOT engine): Xwayland :1 wedged for new GL clients ~19:56
  (all configs hung at context setup, incl. known-good + llvmpipe);
  diagnosed via ps (Xwayland :1), recovered spontaneously ~20:30.
  Combo BLOOM+TAA "hang" scare fully retracted - runs fine under xvfb.
- Gates: VALIDATE 0 errors, kernel OK (current binary). Suite/matrix
  stand from spike 4 (default TAA path provably unchanged: ternary with
  accum=0 IS the original clamp; else-branch writes the GLSL default).

## Spike 6 log — more rays: multi-ray direct + temporal SSGI/GTAO (DONE 2026-09-10)
- Direct estimator goes multi-ray (GX_T_ORACLERAYS=K 1..4, default 1):
  K with-replacement salted picks averaged, x N/K (collisions count
  multiply - correct). salt=0 reproduces the single-ray sequence:
  K-default vs K=1 bit-identical (0px). Variance: K=1 updatemag 0.0204
  vs K=4 0.0094 (ratio 0.46 ~~ 1/2, theory: sigma/2). Unbiased at K=4:
  HDR mean 0.403164 vs ref 0.403179 (0.99996).
- Indirect + AO go temporal (same GX_T_ORACLESEED hook as PCF rotation):
  SSGI gather (4x2x6 taps) and GTAO slices rotate per frame via
  golden-ratio seed hash (angle + tap-pixel offset). Default off =
  static taps exactly. Seed on/off under frozen camera: 8.5% pixels
  differ (rays move), means 84.11 vs 84.16 (rotation preserves mean).
  The SSGI accum / TAA running averages converge the temporal rays -
  rays distributed over TIME, not per-frame cost.
- Reflections: SSR marcher left deterministic (1058-line crossing
  solver with LOD-0 confirmation + phantom validation - temporal
  jitter there risks the validation invariants; its own project).
- Process scar: the Phong K-loop scaled AFTER the loop (out of scope
  of the per-iteration hit counter) - standard.frag failed compile and
  the engine silently fell back to the default shader (both captures
  34.8, identical). Caught by RUN-log grep, fixed by scaling inside.
  RULE: shader gate = RUN logs (build only compiles C). Bonus tool:
  live hot-reload error capture (revert shader, run, re-apply, read
  `hot-reload failed (fragment compile: ...)` with #line info).
- Gates: suite PASS (bit-identical), matrix PASS, VALIDATE 0, kernel OK.

## Spike 7 log — temporal SSR rays (DONE 2026-09-10)
- SSR marcher goes temporal under the same GX_T_ORACLESEED hook: (1)
  march start-phase jitter (t0 = stepSz * seed-hash, same ray, shifted
  sampling - crossing/bisection/validation are phase-invariant by the
  audit notes); (2) glossy-lobe direction jitter (seed-rotated cone,
  tilt ~ pxRough * 0.35, vanishing at mirror roughness so exact-mirror
  proofs hold; lift-off keeps the unjittered ray). Default off = the
  audited deterministic march exactly.
- Seed on/off, frozen camera: 48% pixels differ (rays move - mostly
  binary hit/escape flips on marginal rays), means 97.16 vs 95.85
  single-frame; 12-frame burst average 97.51 vs seed-off 97.16
  (ratio 1.0036) - rotation is mean-preserving, downstream
  accumulators converge it. Non-black, no compile fails.
- Formal proofs (gX/proofs/, outside master/): all three tiers build
  green (GxProofs 33 jobs, GxProofsGfx 3079, GxMath 3069). No spec
  updates needed - ReflectFlags pins flag lifecycle only,
  PipelineOrder the six modeled stages, EffectSources flag
  disjunctions/blits; none pin per-pixel ray behavior (that's what
  the reflect suite + matrix prove empirically, both PASS).
- Gates: suite PASS (bit-identical 40.34/43.04/46.53), matrix PASS,
  VALIDATE 0, kernel OK.

## Spike 8 log — X-hunt part 2 (OPEN, cornered; DONE 2026-09-10)
X = global x(1-1.06e-4)/frame drift of the TAA running average with
stochastic input, linear through f1190, no saturation, deterministic +
reproducible (f10 bit-matches across runs), both GL implementations.
- K-INSENSITIVE: K=1/K=4/frame-global-pick drift identically (~-3e-5/f
  abs). Not variance magnitude; needs temporal variation above a
  threshold (jitter-only control ~flat).
- INPUT EXONERATED over the full range: in-loop witness flat
  (-1e-6/f over 1190f) + R3 HDR unbiased + fallback/NOVELOCITY flat.
- SAMPLER EXONERATED early AND late: output-current-verbatim tracks
  the witness exactly f10-f400 (diff 0.00e+00 every snapshot).
- WEIGHTS CORRECT: in-shader weight dump reads 0.948 at f60 (blend
  upload lands; shortfall from 0.983 = ~0.04px systematic velocity,
  harmless for reprojection). frameIndex sane (29@f30).
- SIZES/BINDINGS SANE: res=resolve=history=taaSrc=1536x1016;
  blend/color/hist/accum locs 0/1/4/8 distinct+valid.
- BYPASS+UPLOAD ENGAGED: accum-dump 96.7% white (rest = early-out
  pixels, correctly showing scene).
- EARLY-OUTS QUANTIFIED (~1.5-3.3% black-selected void/edge pixels):
  constant offset, not drift. Production TAA (EST off) tracks input
  to 0.01% - the loop is exact for deterministic input.
- NOHIST/CONSTHIST/CONSTCUR/WEIGHT/ACCUMDUMP/HISTDUMP/NOREJECT probes:
  mix path exact (E3a mix 0.3347 vs theory 0.3345 after early-offset
  correction); feedback roundtrip exact for constant content.
- REMAINING: feedback-roundtrip loss on *varying* content only,
  ~-4%@590f after early-offset correction, with every step verified
  exact/neutral. Next instrument: apitrace the loop (program-lint
  skill) - observe uniform values, bindings, blend state per frame
  instead of inferring.
- Method scars: (1) shader gate = RUN logs only (build never compiles
  GLSL); (2) hot-reload error capture via revert-run-reapply;
  (3) trajectory single-point means have +-0.02 sampling noise -
  only spatial endpoints (SE ~2e-4) and determinism (f10 match)
  carry weight; (4) compare same-frame captures (scene events move
  means ~1%); (5) post-draw witness reads see our own output -
  witness BEFORE draws.
- Gates: VALIDATE 0, kernel OK. Suite/matrix stand (all changes since
  green are hook-gated or debug-gated; default path provably
  unchanged - ternary-with-0 IS the original clamp).

## Spike 9 log — apitrace verdict: plumbing perfect (OPEN; DONE 2026-09-10)
21MB apitrace (EST+ORACLE, 60f) fully parsed. The TAA loop matches the
model at the GL-call level, every frame:
- Resolve program gifts: blendFactor ends 1-1/n AFTER a 0.85 default
  (override last, no inversions/extras in 59 frames); oracleAccum=1.
- Textures/FBOs distinct, no aliasing: color=hdrScene, depth, history,
  velocity on units 4/3/5/21; resolve->tex64, history->tex62, all
  1536x1016, all RGBA16F/NEAREST/CLAMP.
- Copies (resolve->hdrScene, resolve->history): blend OFF, 1x/frame,
  source tex64 verified. Sampler params benign everywhere.
- control-long (EST off, ORACLE, 1190f): slope -6.5e-7/f (50x below
  EST) - drift strictly EST-gated. Input witness flat full-range.
- Reject fraction directly dumped: 0.08% steady-state (the 3.3% was
  bright-scene contamination of the counting method + burn-in).
  Early-out offset therefore ~0.0003, negligible - E3a's 0.3237 stands
  as mix(c=0.3332, h=0.3346-const, w~0.97) = 0.3345 expected vs 0.3237
  observed, still unexplained at the ALU-execution level.
- Refined decomposition: constant early offset (~-1%) + feedback drift
  (~-4%@590f, linear, no saturation thru f1190) + clamp ratchet when
  active (-4pp). Updatemag trajectory points carry +-0.02 sampling
  noise - only spatial endpoints (SE ~2e-4) and determinism (f10
  bit-match) are load-bearing.
- NEXT: RenderDoc pixel-debug (watch one pixel's history/content
  evolve - answers whether h_exec matches the model where apitrace
  cannot see). All TEMP probes reverted; tree clean.
- Gates: VALIDATE 0, kernel OK (final binary).

## Spike 10 log — history==resolve + E3a standoff (OPEN; DONE 2026-09-10)
- historyMap readback (new permanent ORACLESTATS column): history mean
  == resolve mean EXACTLY, 6 decimals x15 snapshots. The history copy
  is exonerated; roundtrip content verified.
- E3a (const-history) vs normal trajectory compared head-to-head:
  different burn-in + flat-vs-falling => const patch took effect.
  Paradox stands: mix(c=0.3332 sampler-verified, h=0.3346 const,
  w~0.97 weight-dumped) = 0.3237 observed vs 0.3345 theory.
- control-long (1190f, EST off): slope -6.5e-7/f, 50x below EST.
  EST-gating strict. Production TAA tracks deterministic input 0.01%.
- Standing decomposition: early-out offset (~-1%, reject fraction
  0.08% steady-state) + clamp ratchet when active (-4pp) + feedback
  drift on varying content (~-4%@590f post-offset, linear thru f1190,
  deterministic, reproducible, K-/spatial-insensitive, uniform sign).
- NEXT: RenderDoc pixel-debug (per-pixel h_exec vs model - the one
  quantity no readback reaches). All TEMP reverted; tree clean.
- Gates: VALIDATE 0, kernel OK.

## Spike 11 log — pixel-hull: mix exact per-pixel (OPEN; DONE 2026-09-10)
RenderDoc pixel-debug superseded by a better instrument: full-frame
CPU-side mix verification (entry input + entry history + end resolve
snapshots, same-frame causal triple).
- Convex-hull membership per pixel (output in [min(c,h),max(c,h)] +
  F16 slack; weight-free, reject-immune, orientation-agnostic):
  violations 0.034% -> 0.0074% (edge-reprojection artifacts of the
  model, not the shader). Mix executes correctly on 99.993% of pixels.
- histmean FIXED: end-of-pass reads alias the just-written copy
  (tautological). Now averaged from the ENTRY snapshot (true
  history-as-read). Entry-vs-prior-resolve matches to the drift
  quantum - roundtrip genuinely exact.
- Hull + entry-roundtrip + apitrace + control-long + E3a + sizes +
  weights + accumdump + Nohist-late + production + K/spatial +
  reject-dump + 1190f asymptote: every link verified >=2x by
  independent methods. Drift (-4%@590f post-offset, linear, no
  saturation, deterministic, EST-gated, K-/spatial-insensitive,
  uniform sign) has no API/readback-visible mechanism. Named next:
  RenderDoc pixel-history (per-pixel h_exec over frames) or accept
  OPEN and move to M1/Paper2.
- Gates: VALIDATE 0.

## Spike 12 log — HEXEC: per-pixel execution witness (CLOSED the loop question; X ACCEPTED OPEN 2026-09-10)
RenderDoc pixel-history unreachable (no python module in distro builds;
GL pixel-history exists upstream but needs a source build), so built the
better instrument in-engine: TEMP shader witness rows + CPU causal join
(all hook-gated GX_T_HEXEC, fully reverted after).
- h_exec == history texel BIT-EXACT: 8 probe px x 15 frames, 7/8 direct +
  1/8 via 4-neighborhood (neib=0.000e+00). The 8th sits on a texel
  boundary; F16 echo print rounds its address (0.50065 prints 0.50049),
  true texel verified exact. F16 witnesses carry +-0.75texel addressing
  uncertainty near boundaries - methodology note.
- Copy resolve->history 1:1 exact (magenta blink lands x=[764,771]
  y=[508,508]); no RT realloc (ids constant); history RGBA16F (0x881A);
  velocity buffer path steady (hasVel=1), camera static, jitter
  deterministic (A/B runs bit-identical incl. frameIndex phase).
- Ghost chases killed along the way: row-509 vs row-508 huv "variance"
  was two different pixel rows, not run nondeterminism; frozen-got was
  same-pixel parity bookkeeping, not stale sampler.
- VERDICT: the TAA feedback loop (sample + mix + copy) is EXACT per
  pixel per frame. Combined with hull/mix exact (spike 11),
  history==resolve (spike 10), apitrace plumbing (spike 9), flat input
  witness + 50x control-long separation (spike 8): NO loop element can
  produce the drift. X (-4%@590f linear EST-gated) ACCEPTED OPEN with
  boundary fully mapped; named next: input-side trend hunt (witness
  methodology deep-audit) or weight time-variance re-dump. NOT the
  sampler, NOT the copy, NOT precision (F16 quantum accounted), NOT
  jitter, NOT velocity path.
- Gates on reverted tree: VALIDATE 0, kernel OK, suite/matrix PASS.
