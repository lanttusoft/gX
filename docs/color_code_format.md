# Screen color-code format

<!-- //ai: bounded core prototype; camera rectification and multi-frame reassembly are not implemented yet. -->

Status: **core encoder/decoder prototype** (2026-10-30). The reusable C module
is in `src/common/color_code.c` / `.h`; the synthetic camera round trip is part
of `--test-only`. It is not a QR replacement, a camera-certified reader or a
network/security protocol.

## Frame

A frame is a fixed 45×45 cell grid. Each cell is one of sixteen palette entries
and carries four bits.

| Region | Cells | Purpose |
| --- | --- | --- |
| Quiet zone | 2-cell border | Blank capture margin |
| Control/calibration border | 3 cells | Per-palette calibration strip plus 3×3 corner markers |
| Metadata | 3 rows inside the border | 26-byte header and header CRC |
| Payload | 32 rows × 35 columns inside the metadata | SECDED-protected compressed payload |

Palette: `0` black, `1` white, `2`–`5` red at 25/50/75/100%, `6`–`9` green at
25/50/75/100%, `10`–`13` blue at 25/50/75/100%, `14` dark gray, `15` light gray.
Cell bits are written most-significant-bit first. The four 3×3 corner markers
are distinct patterns; the decoder validates them before reading metadata. Both
horizontal border rows cycle through all sixteen palette entries, so the decoder
calibrates every reference directly instead of assuming a fixed black/white
response.

## Metadata

The 26-byte header is written into the first 104 metadata cells:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Magic `0x43` |
| 1 | 1 | Format version (`5`) |
| 2 | 1 | Flags (`bit0` = multi-frame) |
| 3 | 1 | Palette ID (`2` = 16-colour, 4-bit) |
| 4 | 1 | Compression ID (`0` raw, `1` RLE) |
| 5 | 1 | ECC ID (`2` = SECDED + interleaved XOR parity) |
| 6 | 2 | Chunk length (LE) |
| 8 | 2 | Compressed length including mode byte (LE) |
| 10 | 1 | Frame index |
| 11 | 1 | Frame total |
| 12 | 2 | Payload ID (LE) |
| 14 | 4 | Chunk CRC32 (LE) |
| 18 | 4 | Whole-payload CRC32 (LE) |
| 22 | 4 | Header CRC32 over bytes 0–21 (LE) |

Unknown versions, palette IDs, ECC IDs and compression IDs fail closed.

## Multi-frame payloads

`gX_ColorCode_EncodeMulti` splits a payload of up to **4096 bytes** into
sequential 304-byte chunks and sets the same 16-bit payload ID and
whole-payload CRC on every frame. `gX_ColorCode_Reassemble` accepts the decoded
result set in any order, requires exactly `frameTotal` unique indices with one
shared payload ID, concatenates in index order, enforces the output capacity
and verifies the whole-payload CRC. A missing, duplicate or mixed-payload set
fails closed.

`--color-code <text>` cycles multi-frame payloads once per second; `P` pauses,
`LEFT`/`RIGHT` step frames and the window title reports the current index.

## Integrity and bounds

- Each encoded byte becomes a 13-bit SECDED codeword. The decoder corrects one
  bit per codeword and rejects a detected double-bit error.
- Compressed bytes are grouped into interleaved 8-byte blocks with one XOR
  parity byte per block. One erased data byte per block is recoverable; an
  erased parity byte or two erased data bytes in the same block fail closed.
- Low-confidence cells are classified as erasures; a cell marks every encoded
  byte whose bits it covers.
- CRC32 covers the header, each chunk and the completed multi-frame payload
  separately.
- A single frame is capped at **304 payload bytes**; a multi-frame payload is
  capped at **4096 bytes**. Compressed length, reassembly and decompression
  outputs are bounded.
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
image.autoOrientation = 1;   // try all 8 rotations/mirrors
gX_ColorCode_Decode(&image, &result);
```

Cell centres are sampled with a small aperture and corrected against the
calibration row. With `autoOrientation`, the decoder tries the eight
dihedral orientations (identity, 90°/180°/270°, horizontal/vertical mirror,
transpose and anti-transpose) and selects the one whose four corner control
markers match. The result reports the chosen `orientation`. The caller must
provide a frame that fills the declared 45×45 grid; arbitrary cropping is
rejected.

For caller-supplied perspective, set `perspective = 1` and provide the four
projected full-grid corners in image pixels, ordered TL, TR, BR, BL:

```c
image.perspective = 1;
image.corners[0] = tl.x; image.corners[1] = tl.y;
image.corners[2] = tr.x; image.corners[3] = tr.y;
image.corners[4] = br.x; image.corners[5] = br.y;
image.corners[6] = bl.x; image.corners[7] = bl.y;
```

The decoder reconstructs the homography and samples through it. Perspective
mode rejects `autoOrientation` because the corner order is already caller
defined; a future slice can combine both.

## Presentation

`./build/gX_build --color-code <text>` opens a standalone presentation window.
`SPACE` toggles fit/1:1, `F` toggles the window fullscreen and `ESC` closes it.
Single-frame codes are static/frozen; multi-frame payloads cycle once per
second, with `P` to pause and `LEFT`/`RIGHT` to step. The title reports the
current frame. `GX_COLOR_CODE_SHOT=/path.png` saves a deterministic capture
(default frame 10; `GX_COLOR_CODE_SHOT_FRAME=N` selects another) for the
presentation gate.

Caller-supplied perspective rectification is implemented. Automatic corner
detection, camera-device capture and multi-frame playback remain backlog. The
format is deliberately a reusable module rather than an editor-only panel, so
it can serve a CLI/tool or an in-engine screen when normal stdout/stderr
transport is not available.

## Simulated TV

`gX_ColorCodeTv_Render` maps the code onto a tilted 4:3 CRT-style screen and
applies deterministic scanlines, an RGB phosphor mask, a small chroma shift,
grain and vignette. It returns the projected grid corners for the perspective
decoder. `./build/gX_build --color-code-tv <text>` shows the same view in the
standalone presentation window.

The TV decode test renders three fixed noise seeds and requires each to decode
back to the original payload through the caller-supplied corners. This is a
visual/robustness fixture, not a physical CRT or camera model.

## Validation

`./build/gX_build --test-only` covers:

- raw and RLE round trips with the owned synthetic camera (gain/bias/noise);
- empty payloads and frame index/total round trips;
- one-bit SECDED correction;
- two-bit SECDED rejection;
- all eight rotation/mirror orientations;
- affine and true keystone perspective warps via supplied corners;
- per-channel tint/white-balance gains and biases;
- 25/50/75/100% red, green and blue levels plus calibration-strip coverage;
- one erased data byte per block, different-block recovery and same-block
  erasure rejection;
- simulated-CRT scanline/phosphor/noise/chroma/vignette decode across three seeds;
- multi-frame complete/shuffled/incomplete/duplicate/mixed-ID/bad-CRC sets;
- metadata CRC rejection;
- low-confidence cell erasure rejection;
- payload capacity and image geometry bounds.

The tests use only generated cells and pixels; no camera frames or private
payloads are stored.
