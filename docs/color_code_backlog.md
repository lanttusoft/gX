# Screen color-code transfer — backlog

Status: **core encoder/decoder prototype implemented** (2026-10-30). The
bounded C module, format layout, synthetic camera round trip and eight-way
rotation/mirror detection live in [color_code_format.md](color_code_format.md).
Perspective rectification, multi-frame reassembly and a camera-device walk
remain backlog.

## Goal

Transfer a small bounded payload (for example a scene preset, a short text
blob or a few KiB of configuration) from gX to a camera/phone without a
network. The screen is the sender; the camera is the receiver. This is not a
replacement for QR, a general file-transfer protocol or a security boundary.

## Frame layout

- **Calibration border**: a known black/white (or black/white/gray) pattern
  around the frame for exposure, white balance, gamma and channel response.
- **Control points**: fixed corner/registration cells plus optional mid-edge
  markers, so the decoder can recover perspective, rotation, mirroring and
  cell pitch.
- **Metadata at the edges**: format version, payload length, compression ID,
  ECC parameters, palette ID, flags, checksum and a frame index/total for
  multi-frame payloads.
- **Payload in the middle**: data cells surrounded by the metadata/ECC/control
  region. Keep the payload region rectangular and independent of the border
  geometry.
- **Timing/quiet zone**: a blank quiet zone outside the control points so
  capture cropping cannot clip the frame.

## Calibration and sampling

- Per-channel black/white references; derive gain/offset and a coarse gamma
  correction from the calibration cells.
- Cell sampling uses the control-point homography, not screen-space
  assumptions; sample cell centers with a small aperture.
- Prefer robust color classification (nearest palette entry with confidence)
  over exact RGB equality; reject low-confidence cells as erasures.
- Nearest-neighbor cell rendering with integer scaling; no bilinear filtering,
  bloom, tonemapping or dithering on the code surface. Pin sRGB/linear
  conversion so sender and receiver agree.
- Support a monochrome/high-contrast fallback palette for poor cameras or
  accessibility; palette ID lives in the metadata.

## Error correction and integrity

- **Erasures + ECC**: classify bad cells as erasures and run Reed–Solomon (or
  an equivalent bounded block code) over the payload blocks. Interleave blocks
  so a scratch/smudge is spread across codewords.
- **Checksums**: a strong checksum/hash over the compressed payload and a
  separate checksum over metadata; reject on any mismatch.
- **Version/format ID**: unknown versions fail closed with a clear message.
- **Bounded recovery**: cap ECC parity, block count and retry budget; a
  corrupt frame must fail, not allocate or loop unbounded.
- Multi-frame payloads carry frame index/total and a payload ID; the decoder
  reassembles only a complete, checksum-valid set.

## Compression

- Compress before ECC (for example deflate/zlib or LZ4), with a format byte in
  the metadata.
- Hard cap the decompressed size and reject malformed streams; never trust the
  length field.
- Keep a stored/uncompressed mode for already-small or incompressible data and
  for debugging.
- Compression is an encoding detail, not encryption; do not put secrets in a
  screen code.

## Decoder contract

1. Locate the quiet zone/control points and rectify perspective.
2. Read calibration cells and build the channel transform.
3. Sample metadata at the edges; validate version/length/ECC parameters.
4. Sample payload cells; mark low-confidence cells as erasures.
5. Run ECC, verify checksums, decompress within the cap.
6. Return the payload plus diagnostics: per-cell confidence, corrected/erased
   counts, calibration residuals and a human-readable failure reason.

## Rendering and presentation

- Integer cell size, no filtering, no post effects; draw the code in a dedicated
  pass or tool window.
- Keep the code static while capturing; if the payload changes, show a new
  frame ID rather than mutating a frame mid-read.
- Provide a fullscreen/1:1 view and a "freeze for capture" action.
- Multi-frame playback uses a bounded frame rate and a visible progress/index;
  the receiver may need several captures.

## Testing and gate ideas

- Synthetic round-trip: render a known payload, sample it with a CPU camera
  model, decode and compare.
- Perturbation matrix: perspective, rotation, blur, noise, JPEG-like artifacts,
  exposure/gamma/white-balance shifts, partial occlusion and cell erasures.
- Error injection: flip cells/bytes and assert ECC correction or clean failure.
- Fuzz the metadata/length/compression fields; enforce allocation and time caps.
- Multi-frame loss/reorder/duplicate tests.
- A deterministic decode gate for regression, plus a manual phone-camera walk.

## Non-goals

- General-purpose file transfer, networking, encryption or authentication.
- Executing decoded content; the payload is inert data.
- Replacing QR where a standard reader is available.
- Shipping captured camera frames or private payloads in the repository.

## Open questions

- Palette size vs. cell density on an N64-era look (4/8/16 colors?).
- Single-frame practical payload budget at 1080p/4K and typical phone cameras.
- Whether the format belongs in the GFX editor, a standalone tool, or a module.
- Interaction with the screenshot/capture path and the copyright/provenance
  rules for generated codes.
