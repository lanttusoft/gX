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
