# Screen color-code format v1

<!-- //ai: bounded core prototype; camera rectification and multi-frame reassembly are not implemented yet. -->

Status: **core encoder/decoder prototype** (2026-10-30). The reusable C module
is in `src/common/color_code.c` / `.h`; the synthetic camera round trip is part
of `--test-only`. It is not a QR replacement, a camera-certified reader or a
network/security protocol.

## Frame

A frame is a fixed 45×45 cell grid. Each cell is one of four palette entries and
carries two bits.

| Region | Cells | Purpose |
| --- | --- | --- |
| Quiet zone | 2-cell border | Blank capture margin |
| Control/calibration border | 3 cells | Alternating black/white calibration plus 3×3 corner markers |
| Metadata | 3 rows inside the border | 20-byte header and header CRC |
| Payload | 32 rows × 35 columns inside the metadata | SECDED-protected compressed payload |

Palette: `0` black, `1` white, `2` red, `3` blue. Cell bits are written
most-significant-bit first. The four 3×3 corner markers are distinct patterns;
the decoder validates them before reading metadata. The top border row supplies
black/white calibration samples for a per-channel gain/offset correction.

## Metadata

The 20-byte header is written into the first 80 metadata cells:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Magic `0x43` |
| 1 | 1 | Format version (`1`) |
| 2 | 1 | Flags (reserved, `0`) |
| 3 | 1 | Palette ID (`0` = 4-colour) |
| 4 | 1 | Compression ID (`0` raw, `1` RLE) |
| 5 | 1 | ECC ID (`1` = SECDED) |
| 6 | 2 | Uncompressed payload length (LE) |
| 8 | 2 | Compressed length including mode byte (LE) |
| 10 | 1 | Frame index |
| 11 | 1 | Frame total |
| 12 | 4 | Payload CRC32 (LE) |
| 16 | 4 | Header CRC32 over bytes 0–15 (LE) |

Unknown versions, palette IDs, ECC IDs and compression IDs fail closed.
`frameTotal` supports a future multi-frame payload; v1 does not reassemble a
sequence.

## Integrity and bounds

- Each compressed byte becomes a 13-bit SECDED codeword. The decoder corrects
  one bit per codeword and rejects a detected double-bit error.
- Low-confidence cells are classified as erasures. Metadata/data erasures fail
  closed with a distinct status; the first slice does not pretend to recover
  arbitrary erasure patterns.
- CRC32 covers the header and the uncompressed payload separately.
- Payload length is capped at **160 bytes**, compressed length at the cell
  budget, and every decoder output/decompression step is bounded.
- Compression is a PackBits-style RLE. If RLE is not smaller, the encoder uses
  raw mode. Compression is not encryption.

## Decoder input

The current decoder accepts an axis-aligned RGBA8 image with an explicit grid
origin and integer cell pitch:

```c
gX_ColorCodeImage image = {0};
image.pixels = rgba;
image.width = width;
image.height = height;
image.originX = originX;
image.originY = originY;
image.cellSize = cellSize;
gX_ColorCode_Decode(&image, &result);
```

Cell centres are sampled with a small aperture and corrected against the
calibration row. The caller must provide a frame that fills the declared
45×45 grid; arbitrary cropping is rejected.

Perspective rectification, rotation/mirror detection, camera-device capture,
multi-frame playback and a screen presentation tool remain backlog. The format
is deliberately a reusable module rather than an editor-only panel, so it can
serve a CLI/tool or an in-engine screen when normal stdout/stderr transport is
not available.

## Validation

`./build/gX_build --test-only` covers:

- raw and RLE round trips with the owned synthetic camera (gain/bias/noise);
- empty payloads and frame index/total round trips;
- one-bit SECDED correction;
- two-bit SECDED rejection;
- metadata CRC rejection;
- low-confidence cell erasure rejection;
- payload capacity and image geometry bounds.

The tests use only generated cells and pixels; no camera frames or private
payloads are stored.
