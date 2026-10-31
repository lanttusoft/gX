# Screen color-code format

<!-- //ai: bounded core prototype; camera rectification and multi-frame reassembly are implemented, camera-device capture is not. -->

Status: **core encoder/decoder prototype** (2026-10-31). The reusable C module
is in `src/common/color_code.c` / `.h`; the synthetic camera round trip is part
of `--test-only`. It is not a QR replacement, a camera-certified reader or a
network/security protocol.

The primary use case is a target that has a screen but no usable text channel:
a real N64/PS1/PS2-class console, a devkit, a kiosk or an embedded board where
the framebuffer is the only output. The screen becomes a low-bandwidth downlink
and a phone camera reads a bounded diagnostic blob. On a desktop, logs, serial,
a file or the clipboard are cheaper; the color code is for the case where none
of those exist. The module is portable C and does not depend on raylib; gX's
own demo and presentation tool are raylib-backed, while gameX's console
backends are the reference for a hardware port.

## Frame

A frame is a fixed 45×45 cell grid. Three palette modes share the same
geometry:

| Mode | Palette ID | Cells | Calibration strip | Payload per frame |
| --- | ---: | --- | --- | ---: |
| 16-colour | 2 | 4 bits (palette index) | 16-entry cycle | 304 B (303 B RS) |
| 8-colour | 3 | 3 bits (subset symbol) | 8-entry cycle | 228 B (227 B RS) |
| 4-colour | 4 | 2 bits (subset symbol) | 4-entry cycle | 151 B (151 B RS) |

The 16-colour palette is `0` black, `1` white, `2`–`5` red at 25/50/75/100%,
`6`–`9` green at 25/50/75/100%, `10`–`13` blue at 25/50/75/100%, `14` dark
gray, `15` light gray. The opt-in 8-colour subset is `0` black, `1` white,
`13` blue-100, `5` red-100, `9` green-100, `11` blue-50, `3` red-50 and `15`
light gray. The 4-colour accessibility subset is `0` black, `1` white,
`13` blue-100 and `5` red-100; it roughly doubles the minimum colour distance
again at half the 16-colour payload. See
[color_code_palette.md](color_code_palette.md).

| Region | Cells | Purpose |
| --- | --- | --- |
| Quiet zone | 2-cell border | Blank capture margin |
| Control/calibration border | 3 cells | Per-mode calibration strip plus 3×3 corner markers |
| Metadata | 3 rows inside the border | 26-byte header and header CRC |
| Payload | 32 rows × 35 columns inside the metadata | SECDED-protected compressed payload |

Cell bits are written most-significant-bit first. The four 3×3 corner markers
are distinct patterns; the decoder validates them before reading metadata. Both
horizontal border rows cycle through the active palette (all sixteen entries in
mode 2, the eight-entry subset in mode 3, the four-entry subset in mode 4), so
the decoder calibrates every used
reference directly instead of assuming a fixed black/white response.

## Metadata

The 26-byte header is written into the first 52 metadata cells in mode 2 or the
first 70 cells in mode 3 (the metadata region has 105 cells):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Magic `0x43` |
| 1 | 1 | Format version (`5` legacy, `6` Reed–Solomon) |
| 2 | 1 | Flags (`bit0` = multi-frame) |
| 3 | 1 | Palette ID (`2` = 16-colour/4-bit, `3` = 8-colour/3-bit, `4` = 4-colour/2-bit) |
| 4 | 1 | Compression ID (`0` raw, `1` RLE) |
| 5 | 1 | ECC ID (`2` = SECDED + interleaved XOR parity; `3` = RS(18,16)) |
| 6 | 2 | Chunk length (LE) |
| 8 | 2 | Compressed length including mode byte (LE) |
| 10 | 1 | Frame index |
| 11 | 1 | Frame total |
| 12 | 2 | Payload ID (LE) |
| 14 | 4 | Chunk CRC32 (LE) |
| 18 | 4 | Whole-payload CRC32 (LE) |
| 22 | 4 | Header CRC32 over bytes 0–21 (LE) |

Unknown versions, palette IDs, ECC IDs and compression IDs fail closed. The
decoder does not need an out-of-band mode flag: it tries the 4-bit/16-colour
header first, then the 3-bit/8-colour and 2-bit/4-colour headers, and accepts
only a header whose palette ID and CRC agree.

## Multi-frame payloads

`gX_ColorCode_EncodeMulti` splits a payload of up to **57344 bytes (56 KiB)**
into sequential chunks (304 bytes in mode 2, 228 in mode 3, 151 in mode 4) and
sets the same 16-bit payload ID and whole-payload CRC on every frame.
`gX_ColorCode_Reassemble` accepts the decoded result set in any order, requires
exactly `frameTotal` unique indices with one shared payload ID, concatenates in
index order, enforces the output capacity and verifies the whole-payload CRC. A
missing, duplicate or mixed-payload set fails closed. The worst case is 255
frames in 4-colour mode (38,505 bytes), 252 in 8-colour and 189 in 16-colour;
`frameTotal` is one byte, so 255 is the hard ceiling.

`--color-code <text>` cycles multi-frame payloads once per second for small
sets; sets larger than 32 frames advance every 15 ticks, and
`GX_COLOR_CODE_FRAME_TICKS=N` overrides the interval. `P` pauses,
`LEFT`/`RIGHT` step frames and the window title reports the current index.
`--color-code-ecc rs` selects the opt-in Reed–Solomon erasure mode; the
default remains SECDED + XOR. `--color-code-palette 8` or `4` selects the
opt-in high-contrast modes; the default is `16`. `--color-code-file <path>`
reads a binary payload (including NUL bytes) up to 56 KiB.

## Diagnostic payloads

The payload is opaque bytes, so a no-text-channel target can use the shipped
20-byte diagnostic record (`gX_ColorCode_Diagnostic_Pack` /
`gX_ColorCode_Diagnostic_Unpack`). It fits in one frame:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `GXDG` |
| 4 | 1 | Diagnostic schema version |
| 5 | 1 | Flags |
| 6 | 2 | Scene ID (LE) |
| 8 | 4 | Build hash (LE) |
| 12 | 4 | Error code (LE) |
| 16 | 4 | Frame counter (LE) |

The target fills `gX_ColorCodeDiagnostic` and calls
`gX_ColorCode_Diagnostic_Pack`; the receiver decodes the payload and calls
`gX_ColorCode_Diagnostic_Unpack`. The magic is `GXDG` and the current schema
version is `1`. Keep secrets out of it, and remember that the payload is inert
data — the decoder never executes it. Larger dumps can use `EncodeMulti`, up
to 57344 bytes.

## Integrity and bounds

- Each encoded byte becomes a 13-bit SECDED codeword. The decoder corrects one
  bit per codeword and rejects a detected double-bit error.
- Compressed bytes are grouped into interleaved 8-byte blocks with one XOR
  parity byte per block. One erased data byte per block is recoverable; an
  erased parity byte or two erased data bytes in the same block fail closed.
- ECC ID 3 replaces that parity layer with systematic RS(18,16) over GF(256):
  16 data bytes plus 2 Cauchy parity bytes per interleaved block. Any two
  erased bytes per block are recoverable, including data/parity combinations;
  three erasures fail closed. The 13-bit SECDED layer still corrects one bit
  per encoded byte. RS mode caps a single frame at **303 payload bytes**
  (227 in 8-colour mode).
- Low-confidence cells are classified as erasures; a cell marks every encoded
  byte whose bits it covers.
- CRC32 covers the header, each chunk and the completed multi-frame payload
  separately.
- A single frame is capped at **304 payload bytes** in mode 2 (303 in RS mode),
  **228 bytes** in mode 3 (227 in RS mode) or **151 bytes** in mode 4 (151 in
  RS mode); a multi-frame payload is capped at **57344 bytes (56 KiB)**.
  Compressed length, reassembly and decompression outputs are bounded.
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
calibration row. The decoder calibrates both strip cycles and selects the
palette mode from the validated metadata header. With `autoOrientation`, it
tries the eight dihedral orientations (identity, 90°/180°/270°,
horizontal/vertical mirror, transpose and anti-transpose) for the 16-colour
mode first, then the 8-colour mode, and selects the first orientation whose
four corner control markers match. The result reports the chosen `orientation`.
The caller must provide a frame that fills the declared 45×45 grid; arbitrary
cropping is rejected.

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
corners are in **image order**, so they can be combined with
`autoOrientation` to find the logical rotation/mirroring.

## Automatic detection

For a code on a dark, roughly uniform background with a margin around the
quiet zone, `gX_ColorCode_Detect` samples the image border for the background
colour, segments pixels that differ from it, and returns the extreme
TL/TR/BR/BL points of the full-grid quad. The quiet zone is part of the
segmented region, so no active-area expansion is needed:

```c
gX_ColorCodeDetection detection;
gX_ColorCode_Detect(rgba, width, height, &detection);
image.perspective = 1;
image.autoOrientation = 1;
memcpy(image.corners, detection.corners, sizeof(image.corners));
gX_ColorCode_Decode(&image, &result);
```

This is the cheap fast path: it assumes the code is the dominant
background-contrasting region and does not handle arbitrary clutter or partial
occlusion. `gX_ColorCode_DetectClutter` is a bounded fallback for bright
backgrounds and clutter: it scans the four 3x3 control patterns across cell
scales, extrapolates the full-grid quad from their centers, and accepts only a
quad that the existing decoder validates. False control patterns are rejected
by decode. It still does not promise arbitrary occlusion, a code touching the
image border, or a full camera/contour model; the scan is bounded in scales,
positions and decode attempts. The flat, simulated-CRT and synthetic clutter
fixtures are covered in `--test-only`.

## Presentation

`./build/gX_build --color-code <text>` opens a standalone presentation window.
`SPACE` toggles fit/1:1, `F` toggles the window fullscreen and `ESC` closes it.
Single-frame codes are static/frozen; multi-frame payloads cycle once per
second, with `P` to pause and `LEFT`/`RIGHT` to step. The title reports the
current frame. `--color-code-palette 8` or `4` selects the high-contrast
modes, and `--color-code-file <path>` reads a binary payload.
`GX_COLOR_CODE_SHOT=/path.png` saves a deterministic capture (default frame 10;
`GX_COLOR_CODE_SHOT_FRAME=N` selects another) for the presentation gate.

Caller-supplied perspective rectification, automatic dark-background corner
detection, a deterministic simulated CRT/TV view and multi-frame playback are
implemented. Camera-device capture and arbitrary-clutter detection remain
backlog. The format is deliberately a reusable module rather than an
editor-only panel, so it can serve a CLI/tool or an in-engine screen when
normal stdout/stderr transport is not available. See
[color_code_hardware.md](color_code_hardware.md) for the real capture
checklist.

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
- automatic flat-background and simulated-CRT corner detection;
- per-channel tint/white-balance gains and biases;
- 25/50/75/100% red, green and blue levels plus calibration-strip coverage;
- one erased data byte per block, different-block recovery and same-block
  erasure rejection;
- opt-in RS(18,16) round trip, one/two/three erasures in one block, parity-only
  and data+parity recovery, 303-byte capacity, and multi-frame reassembly;
- opt-in 8-colour/3-bit round trip (raw, RLE and empty), palette-ID and
  calibration-strip invariants, one-bit correction, double-bit rejection,
  metadata corruption rejection, single erasure recovery, 228-byte capacity,
  RS capacity/erasure recovery and three-frame reassembly;
- opt-in 4-colour/2-bit round trip, calibration-strip invariants, bit errors,
  metadata corruption, erasure recovery, 151-byte capacity and the 255-frame
  multi-frame ceiling;
- diagnostic-record pack/unpack round trip plus bad-magic, bad-version and
  short-payload rejection;
- binary `--color-code-file` payload input in the presentation check;
- blur, additive noise, gamma/exposure and 2× resolution-loss perturbations;
- small occlusions recovered by parity, large occlusions rejected;
- simulated-CRT scanline/phosphor/noise/chroma/vignette decode across three seeds;
- bright-background/clutter detection with a false control pattern, plus a
  no-code negative;
- multi-frame complete/shuffled/incomplete/duplicate/mixed-ID/bad-CRC sets;
- metadata CRC rejection;
- low-confidence cell erasure rejection;
- payload capacity and image geometry bounds.

The tests use only generated cells and pixels; no camera frames or private
payloads are stored.
