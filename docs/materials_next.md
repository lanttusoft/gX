# Materials/textures/models - open threads (2026-09-10)

## M9. Double tonemap: linearize the G-buffer (PROPOSED, not started)
- standard.frag ACES-tonemaps into hdrScene (measured max exactly 1.0 on
  sunlit scenes); the bloom composite (which documents linear-HDR input)
  tonemaps AGAIN (ENABLE_ACES_TONEMAP default on). Consequences: bloom
  extract starved to the 0.85-1.0 sliver (weak/flaky rows), highlights
  over-compressed. Fix = emit linear HDR from all model paths (extend
  the mirrorFill skip pattern) + tonemap once in composite. Blast radius:
  entire look, all gate values recalibrate, bloom retune. Milestone-scale;
  needs explicit go-ahead.

## M9. Double tonemap (DONE 2026-09-10)
- Cutover (one shader line + one gate): standard.frag emits exposure-
  scaled linear HDR (ACES call removed); composite keeps the only tonemap.
  Water/sky/particles never tonemapped - now consistent with models.
  Exposure semantics unchanged (0.8 linear pre-scale + 1.0 composite).
- Proof: hdrScene TEX_STATS max 1.000000 -> 6.281250, finite, no NaN.
  Bloom extract sees real HDR: bloomemis row 0.0037 -> 0.0313 (8.5x),
  re-gated 0.015-0.06 (dead-bloom margin now 50x). Intensity 6 and
  threshold 0.85 KEPT (healthy on linear: +4% lift, no blowout).
- Blast radius smaller than feared: full matrix PASS on existing gates
  (frac-based, encoding-robust), suite bit-stable (40.34/43.04/46.53 -
  ratio metric), VALIDATE 0, kernel OK. Look change: single-ACES grade
  (correct by construction; old double-tonemap lifted mids).
- BLOOM=0 raw bypass now presents linear (dark) - documented, accepted;
  rows recalibrated by their own gates (all held).

## PBR lighting and material controls (2026-10-31)

<!-- //ai: PBR source/IBL correction; user-facing shadow controls and gates. -->

- **PBR requires MEDIUM shader quality.** LOW compiles the direct-only PBR
  path (no IBL), which read as black metal or white porcelain. Loading a
  preset with any `usePBR` model raises `qualityPreset`/`shaderQuality` to
  MEDIUM, and enabling **use PBR** in the inspector raises it on the spot.
- **IBL source is the procedural sky.** The old split-sum maps were generated
  from the static image skybox, which is usually much darker than the visible
  procedural sky. `gX_RendererGFX_IBL_PrepareSkyCube` renders a one-time,
  sky-only cubemap from the procedural sky shader; the irradiance and
  prefilter passes use it. The IBL cube-face rotation now uses the inverse
  view (`transpose(mat3(captureView))`).
- **IBL uniform locations are cached.** `irradianceMap`/`prefilterMap` were
  missing from `gX_RendererGFX_Shader_Load_Locations`; the struct default `0`
  bound the cubemaps to uniform 0 instead of the actual samplers.
- **Live IBL refresh is double-buffered.** A sky/sun edit builds the new
  irradiance/prefilter pair before releasing the old one, so the fallback
  dummy/dynamic cubemap is no longer visible for a frame. `make pbr-check`
  pins the hold window from a 12-frame burst around the swap.
- **Shadow controls:** `shadowDiffuseBias` (0.85), `shadowSpecularBias`
  (1.0) and `pbrShadowAmbient` (0.35) are schema fields with inspector
  sliders. The PBR path applies the sun shadow to direct light and to the
  diffuse IBL term, so cast shadows remain readable under a bright sky.
- **Planar terrain mirror skips SSAO.** The mirror pass has its own screen
  mapping; sampling the main-view SSAO map was the horizontally flipped AO
  seen inside terrain reflections.
- **God rays use scene depth.** The generation pass masks foreground pixels
  and stops rays at geometry, so PBR specular highlights are not replayed as
  light shafts. `make godray-check` now pins the depth mask.
- **Gate:** `make pbr-check` renders a public PBR fixture, requires the
  LOW→MEDIUM raise, checks procedural-sky IBL, material tint spread and a
  measurable cast-shadow change.
