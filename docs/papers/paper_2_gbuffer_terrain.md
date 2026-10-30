# Paper 2 — Terrain belongs in the G-buffer

*Two-minute version: screenspace effects shouldn't know what a mountain
is. If raymarched terrain writes standard depth and color, eleven
effects support infinite terrain with no terrain-specific code.*

## Thesis

Today the raymarch renderer (`src/renderer/raymarch/`) is a separate
world: own terrain, own water, own sky, own AO — and zero interop with
the GFX screenspace stack. SSR can't reflect it, fog can't scatter it,
shadows can't fall on it, TAA can't still it, DOF can't blur it. Every
effect would need a terrain-specific integration, so none of them has
one, so the terrain lives alone.

The alternative: march the SDF as a GFX sub-pass that outputs exactly
what a rasterized mountain would output — depth in `hdrScene.depth`,
radiance in `hdrScene` color — and every downstream effect works
unchanged. Integration count drops from eleven to one.

## Design (concrete, in this tree)

- **New pass, not new renderer.** A `terrain` pass between `opaque` and
  `velocity` in `gfx_pipeline.c`: fullscreen (or half-res, matching the
  raymarch renderer's proven 0.5 default) SDF march over the terrain
  heightfield + analytic normals, writing depth via the same encoding
  the rasterizer produces and shaded color via the shared PBR path
  (same sun, same fog uniforms — fog then scatters it for free).
- **Reuse the field, not the frame.** The SDF evaluation, tileable
  noise (`raymarch.c:198`), water-height, and sky code move (mostly
  verbatim) into the pass shader. What gets deleted eventually: the
  standalone raymarch scene path, its private sky/AO copies, and every
  future "but does it work on terrain?" question.
- **Depth is the contract.** The only hard requirement: marched depth
  must match raster depth encoding exactly (near/far mapping, reversed
  or not, Hi-Z min-reduction behavior). Get this wrong and SSR tunnels
  through mountains; get it right and everything downstream is free.
  Validate with the depth-echo visualizer (mode 26) before wiring any
  consumer.

## Non-goals

- Not infinite detail (no virtual texture, no clipmaps to start —
  heightfield + noise, same as today).
- Not dynamic deformation (statics first; the SDF is a texture lookup,
  so this door stays open).
- Not a merge of the two renderers on day one. The raymarch scene keeps
  working while the pass matures behind a hook.

## Cost estimate

Medium. One pass + one shader (large but mostly moved code), depth-
encoding validation, suite rows (terrain mirror, terrain fog wedge,
terrain shadow). The delete-half (retiring the standalone path) is a
separate, later decision.

## First step (spike)

Depth-only march writing `hdrScene.depth` behind `GX_T_TERRAINDEPTH=1`,
verified with depth-echo mode 26 against a rasterized proxy mountain
(a big cone): same encoding, no tunneling. Color comes second.

## Spike 1 log — depth contract proven (DONE 2026-09-11)

New `terrain` pass between `opaque` and `velocity` (`gfx_pipeline.c`),
hook-gated `GX_T_TERRAINDEPTH` (`=1` flat proof at baseY 0.08, `=0.6`
hills visual; unset = pass absent, default path bit-unchanged).
Fullscreen analytic-plane + 64-step sphere-trace SDF march
(`gfx_terrain_depth.frag`, flat y=0 under slab footprint blending to
sine hills outside), `gl_FragDepth` from the same VP the rasterizer
uses, color writes masked (depth-only). Permanent `TERRAINSTATS`
instrument (ORACLESTATS-grade): full-frame depth readback vs F32
CPU mirror of the march, every 30th frame, hook-gated.

- Flat: 20160 probe hits, match=17914 @ maxAbs 2.4e-07 (~4 depth
  LSBs), occ=2246 (balls/cube in front, classified), bug=0, skip=4224
  (sky). Bit-identical across frames AND runs.
- Hills (march path): match=11967/13787 @ maxAbs 1.9e-06, bug=0.
- Proxy: reflectrow slab (top +0.075, cube.glb spans [-1,1] — the
  "proxy mountain"); spike plane at +0.08 overwrites raster across the
  whole slab, proving write + encoding together. Mode-26 echo: R
  constant 251 across the full ground row (zero jumps at the slab
  edge), smooth viewZ gradient. Beauty: sky identical, slab identical;
  only the horizon band responds (fog/SSR seeing new ground — the
  effect working as designed).
- Validation requires MSAA off (`--preset 1`): multisample depth is
  CPU-unreadable (0x502). Pass itself writes `second.rt`, MSAA-correct.
- Two real bugs caught by the spike: (1) screen-quad UV is top-down,
  shader flip lands back in GL space — CPU side must NOT flip again
  (mirrored rays); (2) RHI cap-cache desync (rlgl mutates GL behind
  its back) — the pass now does full raw-GL save/restore +
  InvalidateCaches (green-frame wipe before the fix).
- Open (next spikes): shaded color via shared PBR path, terrain motion
  vectors for velocity/TAA (terrain currently static), 24→64-step
  budget still misses grazing rays (<~11deg), MSAA-on validation path.
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.

## Spike 2 log — shaded color via shared uniforms (DONE 2026-09-11)

Terrain frag now shades: finite-difference normal (eps 0.06), two-tone
procedural albedo (grass/rock by slope, pale rock by height), sun
diffuse + ambient + first-4 type-1 point lights with the exact
standard.frag formulas (same uniform VALUES as Shader_SetValues),
times exposure, linear HDR (M9 — no tonemap, no specular on rough
ground, no SSAO/shadows). Alpha 0.9 (M1 roughness for SSR). Color
writes on (blend still off). Permanent TERRAINCOLORSTATS: color
readback vs F32 CPU shade mirror on depth-matched pixels + alpha
contract check + mean luminance (sun-response across runs).

- V1 absolute (fog off): 17910/17914 @ maxAbs 4.9e-04 (F16 quantum
  floor); 4 misses = ball-rim depth ties carrying ball color
  (depthAd 6-9e-6, exonerated). alphaBad = same 4.
- V2 sun x2 (GX_T_SUNBRIGHTNESS=2): 17910/17914 @ 9.8e-04 (scales
  with luminance as F16 predicts); meanLum 0.2560 -> 0.3822, CPU
  model tracks absolutely. Same 4 rim pixels (raster unchanged).
- Hills march path: 11958/11986 @ 4.9e-04 (sloped normals, varying
  albedo); 28 misses all ball-rim class.
- V3 consumers (mechanism level): (a) volume fog (density 0.06):
  far-terrain fogged to (52,49,49), nowhere near sky treatment
  (99,80,81) — no tunneling, response varies across terrain-depth
  bins; (b) SSR: slab mirror goes (25,27,42) -> per-pixel-varying
  warm/green (one pixel (114,161,133) terrain-green, B pinned) —
  spatially-varying reflection content, not haze. Beauty sky/slab
  raster otherwise identical.
- Paper-level finding: reflections pass (planar/cubemap) runs BEFORE
  terrain, so planar mirrors/IBL probes can never see marched terrain
  (SSR path proven here; planar integration is production work, as is
  terrain motion vectors for velocity/TAA, specular, SSAO, shadows).
- Method: fullscreen depthviz needed a headless switch — added TEMP
  GX_T_DEPTHVIS for V3 analysis, reverted after (30s to re-add).
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.

## Spike 3 log — terrain motion vectors (DONE 2026-09-11)

Depth-reprojection quad (`gfx_terrain_velocity.frag`: unproject with
current unjittered invVP, reproject with previous, vel = cuv-puv —
same math as the TAA depth fallback, baked into the buffer) runs
inside Velocity_Draw after the clear, before models (models overwrite
with object motion). Same hook + MSAA gate as the terrain pass (no
terrain = no quad; multisample depth unsampleable). Permanent
TERVELSTATS: velocity readback vs F32 CPU reproject on terrain-won
pixels. Also asserts depth NEAREST at first use (texel-exact contract;
ssr_inputs re-asserts later).

- Buffer proof (GX_CAM_SPIN=0.5 deterministic orbit): 18048/18048 @
  maxAbs 1.1e-05, meanAbs 2.8e-06. Static: 100% (FMA floor ~2e-6).
  Bit-stable across frames.
- TAA-level proof (TEMP checkerboard albedo + same-frame cross-run
  diff, all TEMPs reverted): zero-velocity ablation smears checker
  (ON-vs-OFF meanAbs 4.86 LDR, p99 25); gradient energy OFF 12.96M
  vs ON 16.45M (+27% sharper with velocity). Side finding: the TAA
  depth-delta compares within the CURRENT depth map, so smooth
  misalignment smears instead of rejecting (magenta census measured
  0 in both — instrument measured the wrong thing, checker fixed it).
- Production notes: quad generalizes to all raster non-model pixels
  (floor would gain correct velocity too) — kept terrain-gated by
  spike discipline; planar/cubemap reflections still predate terrain;
  velocity under MSAA falls back to status quo (zeros).
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.

## Spike 4 log — texture heightfield through the pipe (DONE 2026-09-11)

`GX_T_TERRAINNOISE=1` switches the height source from analytic sine to
a 256x256 tileable value-noise FBM texture (CPU-generated, integer
lattice, periods 8/16/32/64, [0,1]-normalized; lazy R32F/RED/FLOAT
upload, LINEAR/REPEAT, no mips; unit 7). Shader maps tex [0,1] to
[-1,1] like the sine path (same amp semantics, 8u tile). CPU mirror
implements the GL LINEAR+REPEAT formula; everything downstream
(march, shade, velocity, stats) is source-agnostic. Default OFF —
analytic path bit-unchanged.

- Noise depth: 16721 hits, match=10890 @ maxAbs 9.8e-06, occ=3350
  (slab-dip wins), bug=0, graze=2481, sil=459. Bit-stable f30=f60 and
  across runs.
- Noise color: 10267/10267 @ 1.3e-03 (F16 floor), alphaBad=164
  (z-fight/silhouette class, same metrology as spike 2's 4).
- Bilinear verdict (NEAREST A/B, TEMP, reverted): bug 12 -> 0. The 12
  far-grazing (pred>0.994) GPU-miss pockets are LINEAR filter
  precision (1-ulp heights decide hit/miss at ~100u grazing runs),
  not march logic/FMA/budget. Production keeps LINEAR (smooth
  normals need it); graze class documents the limit (noise path only
  — analytic stays fully metered at 2.4e-07).
- colMiss forensics: 95% got<pred + gray + alpha!=0.9 = slab-side
  silhouette pixels inside the depth gate, not terrain failures.
  Fixed the classifier, not the terrain: stride-8 neighbor
  discontinuity -> sil class; alpha!=0.9 identity gate before the
  shade check (flat overlap can never misclassify — 5mm offset).
- Analytic regression: 17914 match (spike-1 number), color
  17431/17431 @ 4.9e-04, alphaBad=4. Untouched.
- Beauty: natural puffy hills, no tiling/faceting, seamless junction
  in elevation; minor edge seam lines (5mm z-fight strip, known).
- Mote follow-up (2026-09-11 hunt, closed as analyzed-wontfix): the
  checkered mote is GTAO IGN-jitter (1-frame-lagged ssaoBlurMap
  multiplies ALL forward shading: +/-1.5 on dark floor (sub-visible),
  +/-13 on 255 specular highlights (visible on pixel-peep). Proven:
  SSR=0 and SUN=0 both kill the highlight pedestal (hence the
  checker); SSR-march debug (d4/d7/d11) shows clean misses (env
  fallback, correct); edge profiles show white-noise jitter, not
  MSAA staircase plateaus. Already mitigated in flight: SSGI oracle
  default-ON (0270b0f) + GGX D-cap 64 (ce9ece1) dim the pedestal.
  Full removal needs GTAO temporal accumulation (SSGI-M4-style;
  rotating taps WITHOUT an accum would flicker) — scoped project,
  not this paper.
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.

## Production gaps closed (2026-09-11): specular, shadows, SSAO

Terrain frag now shades the full shared path: sun diffuse + Phong
dielectric specular (shininess 24, Schlick F0 0.04) + CSM shadow
receive (single + array layers, split blend, diffuse/spec bias) +
SSAO (pre-exposure x mix(1,ssao,0.6), Phong:869). All with the same
uniform VALUES as Shader_SetValues. Permanent instruments extended:
sunOut split, normal extract, CPU shadow router (cascade select +
projection + bounds + 16-texel min-scan over all in-frustum layers),
sign census, interval gate 5e-3.

- Analytic: depth 17914 bug=0 (spike-1 number, bit-stable); color
  17019/17431 — 4070 lit-exact @ 2.4e-04, 13357 shadow-routed
  (556 neg / 1283 pos / 11518 flat), 412 interval-misses (bounded
  penumbra slop, all near real occluders), alphaBad=4 (deterministic
  march-miss pockets, positions match across modes).
- Noise: depth bug=0; color 9560/10267, same classes.
- Beauty: soft ball shadows on basin terrain, directionally
  consistent, no acne/leak/peter-panning. Hill-to-hill casts not
  visually exercisable (sun 42deg + gentle slopes = no geometric
  opportunity; receive code path-identical for all pixels).
- Forensics along the way (all resolved, TEMPs reverted): sampler
  binding audited correct (unit 12); blur map LINEAR/CLAMP asserted;
  4 echo-divergent pixels identified as the same 4 alphaBad pockets
  (instrument decoding non-terrain colors, not a sampling bug);
  shade-mismatch fall-through into occ/bug fixed (depth-matched now
  continues — occ/bug pure again, spike-1 occ number reproduced).
- Remaining gaps (scoped, not started): planar/cubemap reflections
  predate terrain (reflections pass runs 1st; fix = mirrored-VP
  terrain invocation inside the planar render, +1 march — design
  noted); MSAA validation path (passes run, only CPU readback gated
  — metrology-only); grazing march budget; terrain specular/SSAO
  now done.
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.

## Planar integration (DONE 2026-09-11): terrain in the mirror source

Mirrored-VP terrain march inside the planar fill (after the model
loops, before EndTexture): same depth shader + shared uploads, own
Push/ortho/FrontFace-CCW/blend save-restore (no Begin/EndTexture —
planar FBO already bound). `GX_T_TERRAINMIRROR=0` kills the mirror
march only (perf knob for the +1 march; stats then meter absence).
`TERRPLANARSTATS` (same core, mirrored VP, planar FBO, before
planar-fog): depth + color + all classifiers.

- Analytic planar: 19673 depth @ 8.3e-06 bug=0; color 18261/19474
  (1213 penumbra slop, same class as main); shNeg=1017 genuinely
  shadowed in mirror. Mirror dump shows lit terrain + balls + sky.
- Noise planar: 10407 depth bug=0; color 8919/10220; graze higher
  (6009: mirror looks up from below = more grazing geometry).
- March went double-sided (mirror looks up from below): flat
  analytic generalized (t>=0, bit-identical topside — main numbers
  unchanged), sphere-trace marches |f| + bisects on sign change,
  underside shades top-lit (no normal flip: flipping blacks out the
  mirror ground — tried, measured meanLum 0.034, reverted).
- Consumer impact (same-frame ablation, main terrain identical):
  mirrorON-vs-OFF differs in rows 400-700 (meanAbs 2-3.6, max 172);
  amplified diff lands on sphere lower hemispheres as coherent
  terrain-colored bands (SSR-miss fallback path). Floor proper
  SSR-hits (same both runs) — its direct planar mix is Fresnel/
  confidence-gated pre-existing behavior, not a terrain gap.
- Main-pass numbers bit-identical through the refactor (upload +
  stats cores extracted with proofs).
- Gates: suite/matrix PASS, VALIDATE 0, kernel OK.
