# Color-code palette density study

Status: implemented (2026-10-31). This note compares 4-, 8- and 16-colour
palettes for the 45×45 screen code. The 8-colour recommendation is now the
opt-in palette ID 3; the default remains the 16-colour, 4-bit palette.

## Method

`scripts/color_code_palette_experiment.py` selects the 4/8/16 colours with a
greedy maximum-minimum-distance subset of the shipped palette, then computes:

- the real data capacity at 2/3/4 bits per cell over the 32×35 data region,
  including the 13-bit SECDED layer and one XOR parity byte per 8 encoded
  bytes;
- nearest-colour symbol error under deterministic gain `0.95`, bias `+5` and
  independent Gaussian RGB noise at σ 0, 5, 10, 15, 20 and 30.

The simulation measures **pre-ECC symbol errors**. Blur, JPEG artifacts,
perspective and occlusion are covered by other tests, not this table.

Run:

```bash
python3 scripts/color_code_palette_experiment.py --check
python3 scripts/color_code_palette_experiment.py --json
```

## Results (200,000 symbols, seed 7)

| Colours | Bits/cell | Min RGB distance | Payload | σ5 | σ10 | σ15 | σ20 | σ30 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 4 | 46.1 | 304 B | 0.0005% | 1.0775% | 7.8485% | 17.9865% | 35.8945% |
| 8 | 3 | 109.1 | 228 B | 0% | 0% | 0.0520% | 0.6265% | 5.5495% |
| 4 | 2 | 227.2 | 151 B | 0% | 0% | 0% | 0% | 0.0145% |

Greedy subsets (indices into the shipped 16-colour table):

- 16: all colours.
- 8: black, white, blue-100, red-100, green-100, blue-50, red-50, light gray.
- 4: black, white, blue-100, red-100.

## Interpretation

- **16 colours** maximize payload (304 B) but the smallest colour margin makes
  nearest-colour errors appear early; at σ10 about 1.1% of symbols are wrong
  before ECC, and at σ20 about 18%.
- **8 colours** cost 25% of payload (228 B) but roughly double the minimum
  colour distance and keep errors negligible through σ10, with 0.63% at σ20.
- **4 colours** cost 50% of payload (151 B) and remain essentially error-free
  through σ20; this is the poor-camera/accessibility end of the tradeoff.

ECC can correct some symbol errors, but it cannot recover a symbol that was
mapped to the wrong colour. The 8-colour mode is therefore the interesting
middle: a visible robustness gain for a quarter of the payload.

## Recommendation

1. Keep the 16-colour, 4-bit format as the default; it is the capacity mode.
2. The **opt-in 8-colour, 3-bit-per-cell palette** is implemented as palette
   ID 3: it has its own 8-entry calibration strip, uses 3 bits per cell for
   metadata and payload, and reuses the existing SECDED/RS layers.
3. Treat 4 colours as a poor-camera/accessibility mode, not a general default;
   it remains unimplemented.
4. Do not change the default based on this table alone: the simulation is
   pre-ECC and excludes blur/JPEG/occlusion. The 8-colour mode is covered by
   the synthetic round-trip/erasure tests and the presentation check; a camera
   walk is still backlog.

## Implementation

- Palette ID 3; subset indices `0, 1, 13, 5, 9, 11, 3, 15` (black, white,
  blue-100, red-100, green-100, blue-50, red-50, light gray).
- 3 bits per cell for both metadata and payload; the 26-byte header uses 70
  cells instead of 52.
- The calibration strips cycle the eight-entry subset, and the decoder tries
  the 4-bit/16-colour header first, then the 3-bit/8-colour header.
- Single-frame caps: 228 payload bytes with SECDED+XOR, 227 with RS(18,16).
- API: `gX_ColorCode_SetPaletteMode` / `GetPaletteMode`; CLI:
  `--color-code-palette 8` (default `16`).

## Backlog

- Re-run the experiment with blur/JPEG/occlusion and a real camera capture.
- Consider the 4-colour/2-bit accessibility mode only if the camera walk shows
  it is needed.
