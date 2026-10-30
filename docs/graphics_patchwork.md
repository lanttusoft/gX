# Graphics patchwork

<!-- //ai: small-resolution variant matrix; crop, upscale and tile for fast visual comparison. -->

Status: gate/tool (2026-10-28). `scripts/gfx_patchwork_check.py`.

## Why

A full-resolution capture is slow and hides the exact region a change affects.
The patchwork gate renders one scene with several parameter variants at low
resolution, crops the same region from each variant, upscales the crops with
nearest-neighbour sampling, and tiles them into a labeled sheet.

This is for:

- isolating a problematic region and comparing parameters around it;
- fast iteration on a small pixel area;
- preference testing without full-resolution renders;
- keeping the comparison honest: same camera, same frame, same crop.

It is not an image-quality metric by itself. The patchwork is visual evidence;
`--require-change` only proves the variant is not pixel-identical.

## Usage

```bash
python3 scripts/gfx_patchwork_check.py --scene showcase \
  --size 320x320 --crop-size 96 --scale 4 --cols 3 \
  --variant baseline: \
  --variant no-bloom:GX_T_BLOOM=0 \
  --variant no-ssao:GX_T_SSAO=0
```

- `--scene` accepts `gfx`, `showcase`, `title`, `battle`, or `custom:--flag`.
- `--size WxH` is repeatable; each size writes its own patchwork.
- `--crop x,y,w,h` fixes the region. Without it the tool auto-crops around the
  largest cross-variant difference (99th percentile of the mean channel delta).
- `--crop-size` is the auto-crop square size; `--scale` is integer
  nearest-neighbour upscaling; `--cols` is the grid width.
- `--variant name:KEY=VALUE;KEY=VALUE` adds one column; the baseline is
  `--variant baseline:`.
- `--x-env KEY --x-values ...` plus `--y-env KEY --y-values ...` builds a 2D
  parameter grid; the cell count is the product of the axes.
- `--x-bits KEY --y-bits KEY --bits 3` builds an 8x8 bitmask grid: the env
  values are 0..7 and the labels show `0b000`..`0b111`. Use this for 3-bit
  mask axes or any 3-bit limit scan.
- `--env GX_T_HUD=0` removes the showcase help overlay from the crops.
- `make gfx-patchwork-check` uses the showcase scene, 320x320, a 96px crop,
  baseline/no-bloom/no-ssao variants, `GX_T_HUD=0`, and
  `--require-change`.
- Outputs: `patchwork.png`, per-variant full captures and `manifest.json`
  under `/tmp/opencode/gfx_patchwork/<WxH>/`.

## Variant hooks

`GX_T_*` showcase hooks are the natural first set:

- `GX_T_BLOOM`, `GX_T_SSAO`, `GX_T_SSR`, `GX_T_SHADOWS`, `GX_T_TAA`.
- `GX_T_BLOOMINT`, `GX_T_BLOOMTHRESH`, `GX_T_BLOOMEXPOSURE` for small
  preference sweeps.
- `GX_GFX_STATE_SCRIPT` for editor/renderer flags.

The scene must consume the hook. `--showcase` consumes the `GX_T_*` set;
`--gfx` does not, so use it only when the desired parameters are already live.

## Gate target

```bash
make gfx-patchwork-check
```

This is a fast visual gate, not a replacement for full-resolution checks. It
fails only when a non-baseline variant is pixel-identical to the baseline
within the configured delta.

## Limits

- Auto-crop finds where variants differ, not where the image is wrong.
- A HUD/overlay inside the crop will dominate the difference; hide it
  (`GX_T_HUD=0`) or pass an explicit `--crop`.
- A crop can hide a regression outside the chosen region; keep a full-frame
  capture alongside it.
- Labels use the built-in ASCII font; no Unicode glyphs.
- The tool uses the live engine and a display, like the other capture gates.
