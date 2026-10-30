#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""Small-resolution variant patchwork for graphics debugging and preference tests.

Renders one scene with several environment variants, crops the same region,
upscales each crop with nearest-neighbour, and tiles them into a labeled
patchwork. An auto-crop mode focuses on the region that differs most between
variants.

Examples:
    python3 scripts/gfx_patchwork_check.py --scene gfx \
        --size 320x320 --auto-crop --crop-size 64 --scale 4 --cols 4 \
        --variant baseline: \
        --variant no-ssr:GX_T_SSR=0 \
        --variant no-shadow:GX_T_SHADOWS=0 \
        --variant no-bloom:GX_T_BLOOM=0

    python3 scripts/gfx_patchwork_check.py --scene gfx \
        --size 256x256,512x512 --crop 80,80,64,64 --scale 4 --cols 3 \
        --variant warm:GX_T_BLOOM=1 \
        --variant cool:GX_T_BLOOM=0
"""

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import audit_common as audit


OUT = Path("/tmp/opencode/gfx_patchwork")
DEFAULT_SIZE = "320x320"
DEFAULT_CROP = 64
DEFAULT_FRAMES = 30
DEFAULT_SHOT_FRAME = 24
BG = (18, 16, 26)
TILE_BG = (10, 9, 15)
TEXT = (238, 232, 255)
MUTED = (166, 156, 190)
ACCENT = (232, 174, 82)
BORDER = (74, 58, 104)


def parse_size(value):
    try:
        width, height = value.lower().split("x", 1)
        width, height = int(width), int(height)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid size: {value}") from error
    if width < 8 or height < 8:
        raise argparse.ArgumentTypeError("size must be at least 8x8")
    return width, height


def parse_crop(value):
    try:
        x, y, width, height = (int(part) for part in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid crop: {value}") from error
    if width < 1 or height < 1:
        raise argparse.ArgumentTypeError("crop width/height must be positive")
    return x, y, width, height


def parse_variant(value):
    name, _, encoded = value.partition(":")
    name = name.strip()
    if not name:
        raise argparse.ArgumentTypeError("variant needs a name: name:KEY=VALUE;...")
    env = {}
    if encoded.strip():
        for item in encoded.split(";"):
            item = item.strip()
            if not item:
                continue
            key, separator, raw = item.partition("=")
            if not separator or not key.strip():
                raise argparse.ArgumentTypeError(
                    f"variant {name}: expected KEY=VALUE, got {item!r}")
            env[key.strip()] = raw
    return name, env


def grid_variants(x_env, x_values, y_env, y_values, cell_env):
    """Build row-major (label, env) cells for a two-axis value grid."""
    values_x = [value.strip() for value in x_values.split(",") if value.strip()]
    values_y = [value.strip() for value in y_values.split(",") if value.strip()]
    if not values_x or not values_y:
        raise ValueError("grid axes need at least one value each")
    variants = []
    for y in values_y:
        for x in values_x:
            env = dict(cell_env)
            env[x_env] = x
            env[y_env] = y
            variants.append((f"x={x} y={y}", env))
    return variants, len(values_x)


def bit_variants(x_env, y_env, bits, cell_env):
    """Build an 8x8 (for bits=3) bitmask grid with 0b labels."""
    if bits < 1 or bits > 8:
        raise ValueError("bits must be 1..8")
    count = 1 << bits
    variants = []
    for y in range(count):
        for x in range(count):
            env = dict(cell_env)
            env[x_env] = str(x)
            env[y_env] = str(y)
            variants.append((f"x=0b{x:0{bits}b} y=0b{y:0{bits}b}", env))
    return variants, count


def parse_global_env(values):
    env = {}
    for value in values or ():
        key, separator, raw = value.partition("=")
        if not separator or not key:
            raise argparse.ArgumentTypeError(f"expected KEY=VALUE, got {value!r}")
        env[key] = raw
    return env


def capture(variant, env, scene_args, size, frames, shot_frame, output):
    directory = output / variant
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob("gXss_*.png"):
        stale.unlink()
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_SKIP_TESTS": "1",
    }
    overrides.update(env)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene(
        scene_args[0], environment, width=size[0], height=size[1],
        fps=None, frames=frames, shot_frame=shot_frame,
        screenshot_dir=directory, timeout=180, check=False,
        extra_args=tuple(scene_args[1:]))
    shot = audit.newest_shot(directory)
    if result.returncode != 0 or shot is None:
        raise RuntimeError(
            f"{variant}: capture failed (exit {result.returncode})\n"
            + audit.strip_ansi(result.stdout or "")[-2500:])
    return Image.open(shot).convert("RGB"), shot


def difference(frames):
    baseline = frames[0]
    deltas = []
    for frame in frames:
        delta = np.abs(frame.astype(np.int16) - baseline.astype(np.int16))
        deltas.append(float(delta.mean()))
    return deltas


def auto_crop(frames, crop_size):
    baseline = frames[0].astype(np.int16)
    delta = np.zeros((baseline.shape[0], baseline.shape[1]), dtype=np.float32)
    for frame in frames[1:]:
        delta += np.abs(frame.astype(np.int16) - baseline).mean(axis=2)
    if delta.max() <= 0.0:
        height, width = delta.shape
        x = max(0, (width - crop_size) // 2)
        y = max(0, (height - crop_size) // 2)
        return x, y, min(crop_size, width), min(crop_size, height), 0.0
    threshold = np.percentile(delta, 99.0)
    ys, xs = np.where(delta >= threshold)
    if len(xs) == 0:
        ys, xs = np.where(delta > 0.0)
    if len(xs) == 0:
        height, width = delta.shape
        return max(0, (width - crop_size) // 2), max(0, (height - crop_size) // 2), \
            min(crop_size, width), min(crop_size, height), 0.0
    center_x = int((xs.min() + xs.max()) * 0.5)
    center_y = int((ys.min() + ys.max()) * 0.5)
    height, width = delta.shape
    half = crop_size // 2
    x = max(0, min(width - min(crop_size, width), center_x - half))
    y = max(0, min(height - min(crop_size, height), center_y - half))
    return x, y, min(crop_size, width), min(crop_size, height), float(delta.max())


def label_for(name, delta):
    return f"{name}  delta {delta:.1f}"


def make_patchwork(tiles, labels, flags, crop, scale, cols, size, title, notes, output):
    tile_width = crop[2] * scale
    tile_height = crop[3] * scale
    label_height = 22
    gap = 10
    rows = (len(tiles) + cols - 1) // cols
    width = cols * tile_width + (cols + 1) * gap
    height = (52 + (14 if notes else 0)) + rows * (tile_height + label_height) \
        + (rows + 1) * gap
    canvas = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    draw.text((gap, 10), title, fill=TEXT, font=font)
    draw.text((gap, 28), f"crop={crop[0]},{crop[1]},{crop[2]},{crop[3]}  "
              f"scale={scale}x  grid={cols}", fill=MUTED, font=font)
    if notes:
        draw.text((gap, 42), notes, fill=ACCENT, font=font)
    top = 52 + (14 if notes else 0)
    for index, (tile, label) in enumerate(zip(tiles, labels)):
        column = index % cols
        row = index // cols
        x = gap + column * (tile_width + gap)
        y = top + gap + row * (tile_height + label_height + gap)
        color = (224, 84, 84) if flags and flags[index] else BORDER
        draw.rectangle((x - 1, y - 1, x + tile_width, y + tile_height),
                       outline=color)
        canvas.paste(tile.resize((tile_width, tile_height),
                                 Image.Resampling.NEAREST), (x, y))
        draw.text((x, y + tile_height + 5), label,
                  fill=(255, 180, 180) if flags and flags[index] else TEXT,
                  font=font)
    canvas.save(output)
    return output


def run_size(size, variants, scene_args, global_env, args, output, grid, notes):
    size_dir = output / f"{size[0]}x{size[1]}"
    size_dir.mkdir(parents=True, exist_ok=True)
    frames = []
    shots = []
    for name, variant_env in variants:
        env = dict(global_env)
        env.update(variant_env)
        try:
            frame, shot = capture(name, env, scene_args, size, args.frames,
                args.shot_frame, size_dir)
        except RuntimeError as error:
            print(f"GFX_PATCHWORK: {error}", file=sys.stderr)
            return None
        frames.append(np.asarray(frame, dtype=np.int16))
        shots.append(shot)
    deltas = difference(frames)
    if args.crop:
        crop = args.crop
    else:
        crop = auto_crop(frames, args.crop_size)
    crop = (
        min(crop[0], frames[0].shape[1] - 1),
        min(crop[1], frames[0].shape[0] - 1),
        min(crop[2], frames[0].shape[1] - crop[0]),
        min(crop[3], frames[0].shape[0] - crop[1]),
    )
    tiles = []
    labels = []
    flags = []
    metrics = []
    for name, frame, delta in zip([v[0] for v in variants], frames, deltas):
        image = Image.fromarray(frame.astype(np.uint8), "RGB")
        tile = image.crop((crop[0], crop[1],
                           crop[0] + crop[2], crop[1] + crop[3]))
        tile_array = np.asarray(tile, dtype=np.float32)
        std = float(tile_array.std())
        mean = float(tile_array.mean())
        flat = bool(args.flag_flat and std < args.flat_std)
        tiles.append(tile)
        label = label_for(name, delta)
        if flat:
            label += "  FLAT"
        labels.append(label)
        flags.append(flat)
        metrics.append({"mean": mean, "std": std, "flat": flat})
    title = f"gX patchwork - {' '.join(scene_args)} - {size[0]}x{size[1]}"
    patchwork = size_dir / "patchwork.png"
    make_patchwork(tiles, labels, flags, crop, args.scale, args.cols, size,
        title, notes, patchwork)
    manifest = {
        "scene": scene_args,
        "size": list(size),
        "crop": list(crop),
        "scale": args.scale,
        "cols": args.cols,
        "variants": [
            {"name": name, "env": env, "delta": delta, "shot": str(shot),
             "mean": metrics[index]["mean"], "std": metrics[index]["std"],
             "flat": metrics[index]["flat"]}
            for index, ((name, env), delta, shot)
            in enumerate(zip(variants, deltas, shots))
        ],
        "patchwork": str(patchwork),
    }
    (size_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{size[0]}x{size[1]}: {patchwork}")
    for (name, _), delta in zip(variants, deltas):
        print(f"  {name:<20} delta {delta:6.2f}")
    if args.require_change and len(variants) > 1:
        if grid:
            weak = [] if max(deltas[1:]) >= args.min_delta else ["grid"]
        else:
            weak = [name for (name, _), delta in zip(variants[1:], deltas[1:])
                    if delta < args.min_delta]
        if weak:
            print(f"GFX_PATCHWORK: FAIL — no visible change for {', '.join(weak)}",
                file=sys.stderr)
            return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Small-resolution variant patchwork for gfx review.")
    parser.add_argument("--scene", default="gfx",
        help="scene name (gfx, title, battle, showcase, ...) or 'custom:flag'")
    parser.add_argument("--scene-arg", action="append", default=[],
        help="extra scene argument (repeatable)")
    parser.add_argument("--size", action="append", default=[],
        help="render size WxH (repeatable; default 320x320)")
    parser.add_argument("--crop", type=parse_crop,
        help="absolute crop x,y,w,h; default is auto-crop")
    parser.add_argument("--crop-size", type=int, default=DEFAULT_CROP,
        help="auto-crop square size in pixels (default 64)")
    parser.add_argument("--scale", type=int, default=4,
        help="nearest-neighbour upscale factor (default 4)")
    parser.add_argument("--cols", type=int, default=4,
        help="patchwork columns (default 4)")
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES,
        help="frames to render per variant (default 30)")
    parser.add_argument("--shot-frame", type=int, default=DEFAULT_SHOT_FRAME,
        help="capture frame (default 24)")
    parser.add_argument("--variant", action="append", default=[],
        type=parse_variant, metavar="NAME:ENV=VALUE;...",
        help="variant name plus optional semicolon-separated env overrides")
    parser.add_argument("--x-env", help="env key varied across grid columns")
    parser.add_argument("--x-values", help="comma-separated column values")
    parser.add_argument("--y-env", help="env key varied across grid rows")
    parser.add_argument("--y-values", help="comma-separated row values")
    parser.add_argument("--x-bits", metavar="ENV",
        help="3-bit x mask env key; generates 0..7 columns")
    parser.add_argument("--y-bits", metavar="ENV",
        help="3-bit y mask env key; generates 0..7 rows")
    parser.add_argument("--bits", type=int, default=3,
        help="bit width for --x-bits/--y-bits (default 3)")
    parser.add_argument("--cell-env", action="append", default=[],
        metavar="KEY=VALUE", help="constant env override for every grid cell")
    parser.add_argument("--flag-flat", action="store_true",
        help="mark cells whose crop is nearly flat")
    parser.add_argument("--flat-std", type=float, default=0.5,
        help="flat-cell standard-deviation threshold (default 0.5)")
    parser.add_argument("--env", action="append", default=[],
        metavar="KEY=VALUE", help="global env override for all variants")
    parser.add_argument("--out", type=Path, default=OUT,
        help="output directory (default /tmp/opencode/gfx_patchwork)")
    parser.add_argument("--require-change", action="store_true",
        help="fail when a non-baseline variant is below --min-delta")
    parser.add_argument("--min-delta", type=float, default=1.0,
        help="minimum mean absolute crop delta for --require-change")
    args = parser.parse_args()
    if args.scale < 1 or args.cols < 1:
        parser.error("--scale and --cols must be positive")
    scene_args = [f"--{args.scene}" if not args.scene.startswith("--")
                  else args.scene]
    scene_args.extend(args.scene_arg)
    try:
        global_env = parse_global_env(args.env)
        cell_env = parse_global_env(args.cell_env)
    except argparse.ArgumentTypeError as error:
        parser.error(str(error))
    bit_grid = bool(args.x_bits or args.y_bits)
    grid = bool(args.x_env or args.y_env or bit_grid)
    notes = ""
    if grid:
        if bit_grid:
            if not args.x_bits or not args.y_bits:
                parser.error("--x-bits and --y-bits are required together")
            if args.bits < 1 or args.bits > 8:
                parser.error("--bits must be 1..8")
            args.x_env = args.x_bits
            args.y_env = args.y_bits
            variants, args.cols = bit_variants(
                args.x_env, args.y_env, args.bits, cell_env)
            notes = (f"x: {args.x_env} bits={args.bits}  |  "
                     f"y: {args.y_env} bits={args.bits}  |  "
                     f"{args.cols}x{args.cols}")
        else:
            if not args.x_env or not args.y_env or not args.x_values or not args.y_values:
                parser.error("--x-env/--x-values and --y-env/--y-values are required together")
            try:
                variants, args.cols = grid_variants(
                    args.x_env, args.x_values, args.y_env, args.y_values, cell_env)
            except ValueError as error:
                parser.error(str(error))
            notes = (f"x: {args.x_env} [{args.x_values}]  |  "
                     f"y: {args.y_env} [{args.y_values}]")
    else:
        if not args.variant:
            parser.error("--variant or --x-env/--y-env is required")
        variants = args.variant
    sizes = [parse_size(value) for value in args.size] or [parse_size(DEFAULT_SIZE)]
    output = args.out.resolve()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    ok = True
    try:
        with audit.settings_preserved():
            for size in sizes:
                if not run_size(size, variants, scene_args, global_env,
                        args, output, grid, notes):
                    ok = False
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_PATCHWORK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_PATCHWORK: PASS" if ok else "GFX_PATCHWORK: FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
