# SSR march resolution

`gX_RendererGFX_SSR.resolutionDivisor` selects the ray-march target scale:

| Value | Preset label | Target | Default |
| --- | --- | --- | --- |
| `1` | Full | scene resolution | yes |
| `2` | Half | half width/height | no |
| `4` | Quarter | quarter width/height | no |

The editor exposes it in the **Refl** lab panel. Presets serialize it as
`ssr.resolutionDivisor`; the audit hook `GX_T_SSR_RES=2|4` overrides the preset
for A/B runs. The render target is reconciled before the lazy RT load, so a
preset change takes effect without a restart.

## Why it is safe

- The march runs at a smaller target, but ray hits are still computed against
  the full-resolution scene depth/Hi-Z: only the output density drops, so
  reflections soften instead of shifting.
- Reduced targets sample depth at full-resolution texel centers
  (`ssrReduced`), which removes NEAREST row-boundary striping.
- Full resolution takes no shader branch and remains byte-identical to the
  pre-option captures.
- The map also shrinks: 9.44 MB -> 2.36 MB (half) -> 0.59 MB (quarter) at
  1024x768 RGBA16F + depth.

## Measured trade (GTX 1060, 1024x768, p95, 3 repeats)

| Row | Full | Half | Quarter |
| --- | --- | --- | --- |
| SSR pass (ssr row) | 7.233 ms | 2.799 ms (-61.3%) | 0.900 ms (-87.6%) |
| ssr row total | 13.726 ms | 8.458 ms (-38.4%) | 6.362 ms (-53.6%) |
| default row total | 13.735 ms | 11.711 ms (-14.7%) | 11.537 ms (-16.0%) |

Visual budget on the deterministic SSR fixture (frame 180):

| Variant | PSNR | mean abs | max | pixels >64 |
| --- | --- | --- | --- | --- |
| Half | 31.33 dB | 0.442 | 239 | 0.26% |
| Quarter | 28.28 dB | 0.881 | 238 | 0.60% |

The residual is the sharp analytic sun disc and reflection silhouettes; the
reflections themselves stay in place. Quarter is a low-quality option.

## Gate

`make ssr-resolution-check` / `python3 scripts/ssr_resolution_check.py`:

- captures full twice and requires byte identity (determinism),
- requires half PSNR >= 30 dB, mean <= 0.60, >64 pixels <= 0.5%,
- requires quarter PSNR >= 27 dB, mean <= 1.10, >64 pixels <= 1.2%,
- requires both reduced captures to differ from full (the option engaged).

Timing is not gated: it is host/driver-specific and stays in the lab report.
