#!/usr/bin/env python3
#opencode: deterministic opt-in palette check. It verifies the fixed 16-color
#   and RGB332 256-color quantizers, plus independent palette dithering.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/palette_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")
FIXTURE_WIDTH = 256
FIXTURE_HEIGHT = 192
PALETTE16 = np.asarray([
    [0.0, 0.0, 0.0], [0.0, 0.0, 0.667], [0.0, 0.667, 0.0],
    [0.0, 0.667, 0.667], [0.667, 0.0, 0.0], [0.667, 0.0, 0.667],
    [0.667, 0.333, 0.0], [0.667, 0.667, 0.667], [0.333, 0.333, 0.333],
    [0.0, 0.0, 1.0], [0.0, 1.0, 0.0], [0.0, 1.0, 1.0],
    [1.0, 0.0, 0.0], [1.0, 0.0, 1.0], [1.0, 1.0, 0.0],
    [1.0, 1.0, 1.0],
], dtype=np.float32)
ADVISORY_BUDGETS = {
    "palette16": {
        "max_color_error": 0.16,
        "max_plateau_fraction": 0.997,
        "min_shadow_silhouette_iou": 0.80,
    },
    "palette256": {
        "max_color_error": 0.07,
        "max_plateau_fraction": 0.99,
        "min_shadow_silhouette_iou": 0.95,
    },
}


def request(palette_mode, dither_mode):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_T_PALETTE_MODE": palette_mode,
        "GX_T_DITHER_MODE": dither_mode,
        "GX_T_DITHER_SEED": "17",
        "GX_T_DITHER_ANIMATE": "0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
    }, clean=True)


def image(path):
    pixels = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    if not np.isfinite(pixels).all():
        raise RuntimeError("non-finite pixels in %s" % path)
    return pixels


def difference(first, second):
    delta = np.abs(first - second)
    return {
        "mean_abs_channel": float(delta.mean()),
        "changed_fraction": float((delta.max(axis=2) > 0.5).mean()),
        "max_channel": float(delta.max()),
    }


def quantize16(pixels):
    distances = ((pixels[..., None, :] - PALETTE16) ** 2).sum(axis=3)
    return PALETTE16[distances.argmin(axis=2)]


def quantize256(pixels):
    levels = np.asarray([7.0, 7.0, 3.0], dtype=np.float32)
    return np.floor(np.clip(pixels, 0.0, 1.0) * levels + 0.5) / levels


def luma(pixels):
    return np.dot(pixels, np.asarray([0.299, 0.587, 0.114], dtype=np.float32))


def plateau_fraction(pixels):
    values = luma(pixels)
    adjacent = np.concatenate((
        np.abs(values[:, 1:] - values[:, :-1]).reshape(-1),
        np.abs(values[1:, :] - values[:-1, :]).reshape(-1),
    ))
    return float((adjacent <= 0.0001).mean())


def fixture_set():
    y, x = np.mgrid[0:FIXTURE_HEIGHT, 0:FIXTURE_WIDTH]
    horizontal = x.astype(np.float32) / float(FIXTURE_WIDTH - 1)
    vertical = y.astype(np.float32) / float(FIXTURE_HEIGHT - 1)
    gradient = np.stack((
        horizontal,
        0.25 + 0.65 * vertical,
        0.15 + 0.85 * (1.0 - horizontal) * vertical,
    ), axis=2)
    sky = np.stack((
        0.03 + 0.45 * vertical,
        0.06 + 0.65 * vertical,
        0.14 + 0.80 * vertical,
    ), axis=2)
    background = np.stack((
        0.25 + 0.45 * vertical,
        0.30 + 0.50 * vertical,
        0.35 + 0.55 * vertical,
    ), axis=2)
    shadow_shape = ((horizontal - 0.5) ** 2 / 0.22 ** 2
                    + (vertical - 0.58) ** 2 / 0.12 ** 2) < 1.0
    shadow = background.copy()
    shadow[shadow_shape] *= 0.80
    return {
        "gradient": gradient.astype(np.float32),
        "sky": sky.astype(np.float32),
        "shadow": shadow.astype(np.float32),
    }, background.astype(np.float32), shadow.astype(np.float32)


def bayer_fixture(shape, amplitude):
    matrix = np.asarray([
        [0.0, 8.0, 2.0, 10.0], [12.0, 4.0, 14.0, 6.0],
        [3.0, 11.0, 1.0, 9.0], [15.0, 7.0, 13.0, 5.0],
    ], dtype=np.float32)
    matrix = (matrix + 0.5) / 16.0 - 0.5
    tiled = np.tile(matrix, (shape[0] // 4, shape[1] // 4))
    return tiled[..., None] * amplitude


def fixture_metrics():
    fixtures, background, shadow = fixture_set()
    result = {}
    for label, quantizer, amplitude in (
        ("palette16", quantize16, 1.5 / 8.0),
        ("palette256", quantize256, 1.5 / 16.0),
    ):
        result[label] = {}
        for name, reference in fixtures.items():
            quantized = quantizer(reference)
            dithered = quantizer(np.clip(
                reference + bayer_fixture(reference.shape[:2], amplitude), 0.0, 1.0))
            result[label][name] = {
                "color_error": float(np.abs(quantized - reference).mean()),
                "plateau_fraction": plateau_fraction(quantized),
                "bayer_plateau_fraction": plateau_fraction(dithered),
            }
        quantized_background = quantizer(background)
        quantized_shadow = quantizer(shadow)
        reference_mask = (luma(background) - luma(shadow)) > 0.03
        quantized_mask = (luma(quantized_background)
                          - luma(quantized_shadow)) > 0.03
        intersection = np.logical_and(reference_mask, quantized_mask).sum()
        union = np.logical_or(reference_mask, quantized_mask).sum()
        result[label]["shadow"]["silhouette_iou"] = float(intersection / union)
    return result


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    OUT.mkdir(parents=True, exist_ok=True)

    requests = {
        "off": ("--showcase-minimal", request("0", "0"), SEED_ARGS),
        "palette16": ("--showcase-minimal", request("1", "0"), SEED_ARGS),
        "palette16_bayer": ("--showcase-minimal", request("1", "2"), SEED_ARGS),
        "palette256": ("--showcase-minimal", request("2", "0"), SEED_ARGS),
        "palette256_repeat": ("--showcase-minimal", request("2", "0"), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing palette captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    pixels = {label: image(path) for label, path in shots.items()}
    fixtures = fixture_metrics()
    unique = {
        label: int(np.unique(value.reshape(-1, 3), axis=0).shape[0])
        for label, value in pixels.items()
    }
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-sky",
        "resolution": [1024, 768],
        "seed": 424242,
        "advisory_budgets": ADVISORY_BUDGETS,
        "fixture_metrics": fixtures,
        "unique_rgb_colors": unique,
        "comparisons": {
            "off_to_palette16": difference(pixels["off"], pixels["palette16"]),
            "off_to_palette256": difference(pixels["off"], pixels["palette256"]),
            "palette16_dither_toggle": difference(pixels["palette16"], pixels["palette16_bayer"]),
            "palette256_repeat": difference(pixels["palette256"], pixels["palette256_repeat"]),
        },
        "pass": True,
    }
    if unique["palette16"] > 16 or unique["palette16_bayer"] > 16:
        report["pass"] = False
    if unique["palette256"] > 256 or unique["palette256_repeat"] > 256:
        report["pass"] = False
    if report["comparisons"]["off_to_palette16"]["mean_abs_channel"] <= 1.0:
        report["pass"] = False
    if report["comparisons"]["off_to_palette256"]["mean_abs_channel"] <= 0.1:
        report["pass"] = False
    if report["comparisons"]["palette16_dither_toggle"]["mean_abs_channel"] <= 0.01:
        report["pass"] = False
    if report["comparisons"]["palette256_repeat"]["max_channel"] > 0.0:
        report["pass"] = False
    for mode, fixture_rows in fixtures.items():
        budget = ADVISORY_BUDGETS[mode]
        for name in ("gradient", "sky", "shadow"):
            metrics = fixture_rows[name]
            if metrics["color_error"] > budget["max_color_error"]:
                report["pass"] = False
            if metrics["plateau_fraction"] > budget["max_plateau_fraction"]:
                report["pass"] = False
            if metrics["bayer_plateau_fraction"] >= metrics["plateau_fraction"]:
                report["pass"] = False
        if (fixture_rows["shadow"]["silhouette_iou"]
                < budget["min_shadow_silhouette_iou"]):
            report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("PALETTE-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    print("  unique RGB colors: 16=%d, 16+Bayer=%d, 256=%d" % (
        unique["palette16"], unique["palette16_bayer"], unique["palette256"]))
    print("  16-color dither toggle mean %.4f" %
          report["comparisons"]["palette16_dither_toggle"]["mean_abs_channel"])
    print("  deterministic 256 repeat max %.1f" %
          report["comparisons"]["palette256_repeat"]["max_channel"])
    for mode in ("palette16", "palette256"):
        rows = fixtures[mode]
        print("  %-9s gradient error %.4f, sky error %.4f, shadow IoU %.3f" % (
            mode, rows["gradient"]["color_error"], rows["sky"]["color_error"],
            rows["shadow"]["silhouette_iou"]))
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("PALETTE-CHECK: FAIL — %s" % error)
        sys.exit(1)
