#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic labeled contact sheets for PNG/PPM review batches. No
#   display, scene launch or AI probe; PIL/numpy only.
"""Build one labeled PNG contact sheet from PNG/PPM images.

All inputs must have identical pixel dimensions (mixed sizes are rejected, not
silently resized). Labels are the sorted input index and file name; no
timestamps are embedded. Use --dir to collect *.png/*.ppm deterministically.
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from snapshot_compare import load_rgb


LABEL_HEIGHT = 24
BACKGROUND = (24, 24, 28)
LABEL_COLOR = (230, 230, 235)
IMAGE_TYPES = {".png", ".ppm"}


def collect_inputs(paths, directory):
    if directory is not None and paths:
        raise ValueError("use either positional images or --dir, not both")
    if directory is not None:
        directory = Path(directory)
        if not directory.is_dir():
            raise ValueError(f"{directory}: directory not found")
        paths = sorted(path for path in directory.iterdir()
                       if path.is_file() and path.suffix.lower() in IMAGE_TYPES)
    else:
        paths = [Path(path) for path in paths]
    if not paths:
        raise ValueError("no input images (use PNG/PPM paths or --dir)")
    return paths


def load_inputs(paths):
    images = []
    size = None
    for path in paths:
        array = load_rgb(path)
        dimensions = (array.shape[1], array.shape[0])
        if size is None:
            size = dimensions
        elif dimensions != size:
            raise ValueError(
                f"mixed image sizes: {paths[0]}={size[0]}x{size[1]}, "
                f"{path}={dimensions[0]}x{dimensions[1]}")
        images.append(array)
    return images, size


def fit_label(draw, text, font, width):
    if draw.textlength(text, font=font) <= width:
        return text
    suffix = "..."
    while text and draw.textlength(text + suffix, font=font) > width:
        text = text[:-1]
    return text + suffix


def build_sheet(images, size, columns, cell_width, labels):
    if columns <= 0:
        raise ValueError("--columns must be >= 1")
    if cell_width < 16:
        raise ValueError("--cell-width must be >= 16")
    source_width, source_height = size
    image_height = max(1, round(cell_width * source_height / source_width))
    cell_height = image_height + LABEL_HEIGHT
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height),
                      BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index, image in enumerate(images):
        source = Image.fromarray(image.astype(np.uint8))
        scaled = source.resize((cell_width, image_height),
                               Image.Resampling.LANCZOS)
        column = index % columns
        row = index // columns
        x = column * cell_width
        y = row * cell_height
        sheet.paste(scaled, (x, y))
        draw.text((x + 4, y + image_height + 5),
                  fit_label(draw, labels[index], font, cell_width - 8),
                  fill=LABEL_COLOR, font=font)
    return sheet, cell_height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="*", type=Path,
                        help="PNG/PPM inputs")
    parser.add_argument("--dir", type=Path,
                        help="collect *.png/*.ppm from this directory")
    parser.add_argument("--out", type=Path, required=True,
                        help="output PNG path")
    parser.add_argument("--columns", type=int, default=4,
                        help="grid columns (default 4)")
    parser.add_argument("--cell-width", type=int, default=320,
                        help="cell width in pixels (default 320)")
    parser.add_argument("--json", action="store_true",
                        help="print sheet metadata as JSON")
    args = parser.parse_args()

    try:
        paths = collect_inputs(args.images, args.dir)
        output = args.out.resolve()
        paths = [path for path in paths if path.resolve() != output]
        if not paths:
            raise ValueError("no input images after excluding the output path")
        images, size = load_inputs(paths)
        labels = [f"{index:02d} {path.name}"
                  for index, path in enumerate(paths)]
        sheet, cell_height = build_sheet(
            images, size, args.columns, args.cell_width, labels)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.out)
    except (OSError, ValueError) as error:
        print(f"CONTACT_SHEET: ERROR — {error}", file=sys.stderr)
        return 2

    metadata = {
        "out": str(args.out),
        "images": [str(path) for path in paths],
        "labels": labels,
        "source_width": size[0],
        "source_height": size[1],
        "columns": args.columns,
        "rows": (len(images) + args.columns - 1) // args.columns,
        "cell_width": args.cell_width,
        "cell_height": cell_height,
        "sheet_width": sheet.width,
        "sheet_height": sheet.height,
    }
    if args.json:
        print(json.dumps(metadata, indent=2, sort_keys=True))
    else:
        print(f"CONTACT_SHEET: {len(images)} images -> {args.out} "
              f"({sheet.width}x{sheet.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
