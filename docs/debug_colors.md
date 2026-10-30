# gX debug colors — magic-color registry

Sentinel colors mark shader/feature failures where log text cannot reach
(headless fills, fullscreen passes). All entries are exact 8-bit sRGB.

//opencode: debug-vis registry (voted scope: views + labels + this doc).

## Rule 0 — sentinels live pre-tonemap

ACES tonemap + gamma remap every value, so an exact shade on the FINAL
frame proves nothing. Judge sentinels ONLY in raw paths:

- `--debug-view <0-12>` raw-RT views (D cycles all; see `--help`),
- `GX_COMPOSITE_TEX_STATS` / `CONTACT_STATS`-style numeric dumps,
- `GX_PLANAR_VIEW=1` (raw mirror RT).

A sentinel seen on the tonemapped composite is a hint, never a verdict —
re-capture the raw view before acting on it.

## Registry

| Color (8-bit) | Emitter | Meaning | Verify with |
|---|---|---|---|
| Magenta `255,0,255` | `gfx_bloom_blur_h/v.frag` | `finalColor` never assigned — blur pass fell through | `--debug-view 7` (bloom.scene) |
| Magenta `255,0,255` ×1.0, albedo 0.02 | `GX_DIAG_CUBE=1` harness | DELIBERATE calibration aid, not an error | colorimetric gate (g-ratio) |
| Cyan `40,225,255` ×1.45 | reflectrow probe cube | Attribution marker: "this pixel came from the probe" | `--debug-view 8` (planar RT) |
| `0xFA17ED` (250,23,237) | RESERVED, no emitter yet | Feature-failed fallback (invalid light index, missing map, dead branch in standard.frag) | raw view of the failing pass |
| White atlas cell | cookie atlas rebuild | Empty slot (no cookie bound), not an error | `--debug-view 10` |
| White (contact map) | `gfx_contact.frag` | 1.0 = lit, not an error; dark = occluded | `--debug-view 11` + `CONTACT_STATS` |
| Black (velocity map) | velocity pass | 0 = still, not an error; read edges not shades | `--debug-view 12` + `GX_CAM_SPIN` |

## Adding a new sentinel

1. Pick an unlisted garish color; reserve it HERE first (never reuse).
2. Emit it pre-tonemap (HDR ok — it only needs exactness in raw views).
3. Add the row above: emitter file, meaning, verifying view/hook.
4. Keep grayscale-adjacent values OUT — bloom/tonemap hunts already
   fight over every gray; sentinels must be unmistakable (pink/cyan/lime).
