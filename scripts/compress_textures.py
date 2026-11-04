#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: E6 offline texture compression. The source PNG stays the authoring input;
#   this tool writes a sibling .dds payload (BC7 albedo, BC5 normal, BC4 mask)
#   and reports decoded PSNR plus the VRAM ratio. The runtime loader prefers
#   the sidecar and falls back to the source when the GPU lacks the format.
"""Encode a source PNG to a BCn DDS sidecar with bc7enc."""

import argparse
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image


KINDS = {
    "albedo": ([], 40.0, "BC7"),
    "normal": (["-5", "-f"], 35.0, "BC5"),
    "mask": (["-4", "-f"], 35.0, "BC4"),
}


def encoder_path():
    configured = os.environ.get("GX_BC7ENC")
    if configured:
        return configured if Path(configured).is_file() else None
    return shutil.which("bc7enc")


def psnr(source, decoded, kind):
    if source.shape != decoded.shape:
        raise RuntimeError("decoded image size differs from the source")
    channels = {"albedo": 3, "normal": 2, "mask": 1}[kind]
    delta = source[..., :channels].astype(np.float64) - decoded[..., :channels]
    mse = float((delta ** 2).mean())
    return float("inf") if mse == 0 else 10.0 * math.log10((255.0 ** 2) / mse)


def encode(source, output, kind, encoder):
    if kind not in KINDS:
        raise ValueError("unknown texture kind " + kind)
    flags, threshold, label = KINDS[kind]
    unpacked = output.with_suffix(".unpacked.png")
    command = [encoder, "-q", *flags, str(source), str(output), str(unpacked)]
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        raise RuntimeError("bc7enc failed:\n" + result.stdout)
    if not output.is_file() or not unpacked.is_file():
        raise RuntimeError("bc7enc produced no DDS/unpacked output")
    with Image.open(source) as image:
        source_pixels = np.asarray(image.convert("RGB"), dtype=np.uint8)
    with Image.open(unpacked) as image:
        decoded_pixels = np.asarray(image.convert("RGB"), dtype=np.uint8)
    quality = psnr(source_pixels, decoded_pixels, kind)
    payload = output.read_bytes()
    raw_bytes = source_pixels.shape[0] * source_pixels.shape[1] * 4
    metrics = {
        "kind": kind,
        "format": label,
        "source": str(source),
        "output": str(output),
        "width": source_pixels.shape[1],
        "height": source_pixels.shape[0],
        "psnr": quality,
        "threshold": threshold,
        "file_bytes": len(payload),
        "raw_rgba8_bytes": raw_bytes,
        "vram_ratio": len(payload) / raw_bytes if raw_bytes else 0.0,
    }
    unpacked.unlink()
    unpacked.with_suffix(".unpacked_alpha.png").unlink(missing_ok=True)
    if quality < threshold:
        raise RuntimeError("%s PSNR %.2f < %.2f" % (kind, quality, threshold))
    return metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--kind", choices=sorted(KINDS), default="albedo")
    parser.add_argument("--check", action="store_true",
                        help="encode to a scratch path and report only")
    args = parser.parse_args()

    encoder = encoder_path()
    if not encoder:
        print("TEXTURE_COMPRESS: bc7enc not found (set GX_BC7ENC or PATH)")
        return 2
    output = args.output or args.source.with_suffix(".dds")
    if args.check:
        output = Path("/tmp/opencode/gx_texture_check") / output.name
        output.parent.mkdir(parents=True, exist_ok=True)
    metrics = encode(args.source, output, args.kind, encoder)
    print("TEXTURE_COMPRESS: %s -> %s (%s, %dx%d)"
          % (args.source, metrics["output"], metrics["format"],
             metrics["width"], metrics["height"]))
    print("  PSNR %.2f dB (>= %.2f), file %d B vs RGBA8 %d B (%.2fx)"
          % (metrics["psnr"], metrics["threshold"], metrics["file_bytes"],
             metrics["raw_rgba8_bytes"], metrics["vram_ratio"]))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("TEXTURE_COMPRESS: FAIL — %s" % error)
        sys.exit(1)
