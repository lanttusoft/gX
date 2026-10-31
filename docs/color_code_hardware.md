# Color-code hardware capture checklist

Status: operator checklist (2026-10-31). The synthetic tests cover the format
and decoder; this checklist is for the real screen-to-camera path on a target
with no usable text channel.

## Target and build

- [ ] Target and output: console/devkit/embedded board, model and video mode
      (for example 320×240, 640×480, 720p or 1080p).
- [ ] Build identity: version, commit and build hash.
- [ ] Color-code mode: `16`, `8` or `4` colours.
- [ ] ECC: default SECDED+XOR or `rs`.
- [ ] Payload: single frame, multi-frame or the 20-byte diagnostic record.
- [ ] Display path: direct framebuffer, CRT/TV, scaler or capture card.

## Capture setup

- [ ] Camera on a tripod or stable mount; lens perpendicular to the screen.
- [ ] Fill most of the frame with the code; keep the full quiet zone visible.
- [ ] Lock exposure and white balance; avoid auto-exposure pumping.
- [ ] Disable flash; avoid reflections and strong ambient colour cast.
- [ ] Capture at the native panel resolution when possible; avoid digital zoom.
- [ ] Take at least three distances/angles and one off-axis shot.
- [ ] For CRT, capture a dark scene and a bright scene; check scanline/moiré
      behaviour.

## Decode and record

- [ ] Decode the captured image with the current module/build.
- [ ] Record payload length, palette ID, ECC ID, frame index/total and CRC
      result.
- [ ] Confirm the decoded bytes match the source exactly.
- [ ] Repeat with the 8-colour and 4-colour modes for a poor capture.
- [ ] Record the first failure mode: geometry, palette, checksum, erasure,
      capacity or timeout.
- [ ] Keep captures and payloads private; do not commit camera frames or
      private target data.

## Pass criteria

- The exact payload (or diagnostic record) decodes with a valid CRC.
- 16-colour is the capacity baseline; 8-colour and 4-colour should decode
  under visibly worse exposure/angle conditions.
- No false accept: a corrupted capture must fail closed, not return different
  bytes.
- The quiet zone, corner controls and calibration strips remain visible at the
  chosen distance.

## Known limits

- The repository tests are synthetic; this checklist is the missing real-world
  evidence.
- Full arbitrary occlusion and border-touching codes are not guaranteed.
- The 4-colour mode is the accessibility end of the tradeoff; validate it
  before making it a default.
