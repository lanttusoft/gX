#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: same-frame source and fixed-frame presentation A/B for the
#   procedural sky's LDR seed versus direct HDR rendering.

import argparse
import glob
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN

CASES = (
    ("seed_msaa0", False, 0, True),
    ("direct_msaa0", True, 0, True),
    ("seed_msaa4", False, 4, True),
    ("direct_msaa4", True, 4, True),
    ("no_sky_seed_msaa0", False, 0, False),
    ("no_sky_direct_request_msaa0", True, 0, False),
    ("no_sky_seed_msaa4", False, 4, False),
    ("no_sky_direct_request_msaa4", True, 4, False),
)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_rt(path):
    with open(path, "rb") as stream:
        header = stream.read(20)
        if len(header) != 20 or header[:8] != b"GXRTF32\0":
            raise RuntimeError(f"invalid sky RT header: {path}")
        width, height, channels = struct.unpack("<III", header[8:])
        payload = stream.read()
    values = np.frombuffer(payload, dtype="<f4")
    if channels != 4 or values.size != width * height * channels:
        raise RuntimeError(f"invalid sky RT dimensions/payload: {path}")
    image = values.reshape((height, width, channels)).copy()
    if not np.isfinite(image).all():
        raise RuntimeError(f"non-finite sky RT values: {path}")
    return image


def image_metrics(left, right, roi):
    if left.shape != right.shape:
        raise RuntimeError(f"screenshot dimensions differ: {left.shape} vs {right.shape}")
    delta = np.abs(left.astype(np.int16) - right.astype(np.int16))
    pixel_delta = delta.max(axis=2)
    x0, x1, y0, y1 = roi
    region = delta[y0:y1, x0:x1]
    region_pixels = region.max(axis=2)
    return {
        "mean_abs_rgb": float(delta.mean()),
        "p95_pixel_max_abs": float(np.percentile(pixel_delta, 95)),
        "p99_pixel_max_abs": float(np.percentile(pixel_delta, 99)),
        "max_pixel_max_abs": int(pixel_delta.max()),
        "fraction_pixels_over_1_code": float((pixel_delta > 1).mean()),
        "sky_roi": {
            "bounds_xyxy": [x0, y0, x1, y1],
            "mean_abs_rgb": float(region.mean()),
            "p95_pixel_max_abs": float(np.percentile(region_pixels, 95)),
            "p99_pixel_max_abs": float(np.percentile(region_pixels, 99)),
            "max_pixel_max_abs": int(region_pixels.max()),
            "fraction_pixels_over_1_code": float((region_pixels > 1).mean()),
        },
    }


def pair_metrics(direct, ldr):
    if direct.shape != ldr.shape:
        raise RuntimeError(f"same-frame sky source dimensions differ: {direct.shape} vs {ldr.shape}")
    diff = np.abs(direct[:, :, :3] - ldr[:, :, :3])
    ldr8 = np.rint(np.clip(ldr[:, :, :3], 0.0, 1.0) * 255.0) / 255.0
    quantization_residual = np.abs(ldr[:, :, :3] - ldr8)
    source_max = np.max(diff, axis=2)
    return {
        "dimensions": [int(direct.shape[1]), int(direct.shape[0])],
        "direct_hdr_rgb": {
            "min": [float(direct[:, :, c].min()) for c in range(3)],
            "max": [float(direct[:, :, c].max()) for c in range(3)],
            "mean": [float(direct[:, :, c].mean()) for c in range(3)],
        },
        "ldr_rgb": {
            "min": [float(ldr[:, :, c].min()) for c in range(3)],
            "max": [float(ldr[:, :, c].max()) for c in range(3)],
            "mean": [float(ldr[:, :, c].mean()) for c in range(3)],
        },
        "ldr_quantization_residual_max": float(quantization_residual.max()),
        "direct_vs_ldr_rgb_mean_abs": float(diff.mean()),
        "direct_vs_ldr_rgb_p95_abs": float(np.percentile(diff, 95)),
        "direct_minus_ldr_rgb_mean": [
            float((direct[:, :, c] - ldr[:, :, c]).mean()) for c in range(3)],
        "direct_vs_ldr_max_channel_mean_abs": float(source_max.mean()),
        "direct_vs_ldr_fraction_pixels_over_1e-3": float((source_max > 1e-3).mean()),
        "direct_vs_ldr_fraction_pixels_over_half_lsb":
            float((source_max > (0.5 / 255.0)).mean()),
        "direct_vs_ldr_fraction_pixels_over_1_lsb": float((source_max > 1.0 / 255.0).mean()),
        "direct_vs_ldr_max_abs": float(diff.max()),
    }


def capture_case(name, direct, msaa, sky_enabled, frame, output_dir):
    case_dir = os.path.join(output_dir, name)
    if os.path.isdir(case_dir):
        shutil.rmtree(case_dir)
    os.makedirs(case_dir, exist_ok=True)
    pair_dir = os.path.join(case_dir, "same_frame_sources")
    os.makedirs(pair_dir, exist_ok=True)
    pair_frame = frame - 1
    env = audit.scene_env({
        "GX_SCREENSHOT_DIR": case_dir,
        "GX_FIXED_DT": "0.016666667",
    }, clean=True)
    env["GX_NO_AE"] = "1"
    env["GX_T_TAA"] = "1"
    env["GX_T_DITHER_MODE"] = "0"
    env["GX_T_DITHER_SEED"] = "0"
    env["GX_T_DITHER_STRENGTH"] = "1.5"
    env["GX_T_PALETTE_MODE"] = "0"
    env["GX_T_MSAA"] = str(msaa)
    env["GX_T_SKY"] = "1" if sky_enabled else "0"
    env["GX_T_SKY_DIRECT_HDR"] = "1" if direct else "0"
    env["GX_T_TAASTATS"] = "0"
    env["GX_T_SSR"] = "0"
    env["GX_T_SSAO"] = "0"
    env["GX_SSGI"] = "0"
    env["GX_T_REFL"] = "0"
    env["GX_T_FOG"] = "0"
    env["GX_T_GODRAY"] = "0"
    env["GX_T_PARTICLES"] = "0"
    env["GX_T_SNOWFALL"] = "0"
    env["GX_T_RAIN"] = "0"
    env["GX_T_FOGSPRITE"] = "0"
    env["GX_T_LENSFLARE"] = "0"
    env["GX_T_MOTIONBLUR"] = "0"
    env["GX_T_DOF"] = "0"
    env["GX_T_FXAA"] = "0"
    env["GX_T_BLOOM"] = "1"
    env["GX_T_BLOOMINT"] = "0"
    env.pop("GX_CAM_SPIN", None)
    env.pop("GX_T_ORACLE", None)
    env.pop("GX_SHOT_FRAME", None)
    env.pop("GX_SHOT_FORCE", None)
    if sky_enabled:
        env["GX_SKY_PRECISION_PAIR_DIR"] = pair_dir
        env["GX_SKY_PRECISION_PAIR_FRAME"] = str(pair_frame)
    else:
        env.pop("GX_SKY_PRECISION_PAIR_DIR", None)
        env.pop("GX_SKY_PRECISION_PAIR_FRAME", None)
    command = [BIN, "--showcase-y2k", "--ai-screenshot-after", str(frame)]
    log_path = os.path.join(case_dir, "renderer.log")
    print(f"capture {name:30s} frame={frame} TAA=on dither=off", flush=True)
    with open(log_path, "wb") as log:
        subprocess.run(command, cwd=ROOT, env=env, check=True, timeout=360,
                       stdout=log, stderr=subprocess.STDOUT)
    screenshots = glob.glob(os.path.join(case_dir, "gXss_*.png"))
    if len(screenshots) != 1:
        raise RuntimeError(f"{name}: expected one final screenshot, found {screenshots}")
    pair_paths = {}
    if sky_enabled:
        for key in ("skyDirectHDR", "skyLdrRGBA8"):
            path = os.path.join(pair_dir, f"{key}_{pair_frame:06d}.gxrt")
            if not os.path.isfile(path):
                log_text = open(log_path, "r", encoding="utf-8", errors="replace").read()
                tail = "\n".join(log_text.splitlines()[-35:])
                raise RuntimeError(f"{name}: missing same-frame sky source {path}\n{tail}")
            pair_paths[key] = path
    return {
        "case_dir": case_dir,
        "screenshot": screenshots[0],
        "pair_paths": pair_paths,
        "log": log_path,
        "settings": {
            "direct_hdr": direct,
            "msaa_samples": msaa,
            "procedural_sky_enabled": sky_enabled,
            "taa_enabled": True,
            "dither_mode": 0,
            "fixed_dt_seconds": 0.016666667,
            "auto_exposure_enabled": False,
            "source_pair_frame": pair_frame if sky_enabled else None,
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="Compare direct-HDR and RGBA8-seeded procedural sky paths.")
    parser.add_argument("--out", default="/tmp/opencode/gfx-sky-precision-2026-09-23",
                        help="output directory (default: %(default)s)")
    parser.add_argument("--frame", type=int, default=120,
                        help="screenshot trigger frame; paired sources use frame-1")
    parser.add_argument("--cases", metavar="NAMES",
                        help="comma-separated subset of the named capture cases")
    parser.add_argument("--quick", action="store_true",
                        help="capture only seed_msaa0 and direct_msaa0")
    args = parser.parse_args()
    if args.frame < 2:
        parser.error("--frame must be >= 2")
    selected_cases = CASES
    if args.quick:
        selected_cases = CASES[:2]
    if args.cases:
        requested = {name.strip() for name in args.cases.split(",") if name.strip()}
        known = {case[0] for case in CASES}
        unknown = sorted(requested - known)
        if unknown:
            parser.error("unknown case(s): " + ", ".join(unknown))
        selected_cases = tuple(case for case in CASES if case[0] in requested)
    if not selected_cases:
        parser.error("case selection is empty")
    os.makedirs(args.out, exist_ok=True)

    rows = {}
    for name, direct, msaa, sky_enabled in selected_cases:
        rows[name] = capture_case(name, direct, msaa, sky_enabled,
                                  args.frame, args.out)

    screenshots = {}
    for name, row in rows.items():
        screenshots[name] = np.asarray(Image.open(row["screenshot"]).convert("RGB"),
                                       dtype=np.uint8)
        row["screenshot_hash"] = sha256(row["screenshot"])
    first_screenshot = next(iter(screenshots.values()))
    h, w = first_screenshot.shape[:2]
    #opencode: this left-edge strip avoids the Y2K props and HUD, giving a
    #   clean sample of the sky's full vertical gradient including dark values.
    roi = (max(0, int(w * 0.01)), min(w, int(w * 0.08)),
           max(0, int(h * 0.06)), min(h, int(h * 0.98)))

    mode_comparisons = []
    for left, right in (("seed_msaa0", "direct_msaa0"),
                        ("seed_msaa4", "direct_msaa4")):
        if left not in rows or right not in rows:
            continue
        mode_comparisons.append({
            "seed_case": left,
            "direct_case": right,
            "screenshots_identical": rows[left]["screenshot_hash"]
                == rows[right]["screenshot_hash"],
            "screenshot_metrics": image_metrics(screenshots[left], screenshots[right], roi),
        })
    fallback_comparisons = []
    for left, right in (("no_sky_seed_msaa0", "no_sky_direct_request_msaa0"),
                        ("no_sky_seed_msaa4", "no_sky_direct_request_msaa4")):
        if left not in rows or right not in rows:
            continue
        fallback_comparisons.append({
            "baseline_case": left,
            "direct_requested_case": right,
            "screenshots_identical": rows[left]["screenshot_hash"]
                == rows[right]["screenshot_hash"],
            "screenshot_metrics": image_metrics(screenshots[left], screenshots[right], roi),
        })

    paired_sources = {}
    for name in ("seed_msaa0", "direct_msaa0", "seed_msaa4", "direct_msaa4"):
        if name not in rows:
            continue
        paths = rows[name]["pair_paths"]
        direct = read_rt(paths["skyDirectHDR"])
        ldr = read_rt(paths["skyLdrRGBA8"])
        paired_sources[name] = {
            "source_frame": rows[name]["settings"]["source_pair_frame"],
            "direct_hdr_sha256": sha256(paths["skyDirectHDR"]),
            "ldr_rgba8_sha256": sha256(paths["skyLdrRGBA8"]),
            "metrics": pair_metrics(direct, ldr),
        }

    sheet_height = ((len(rows) + 1) // 2) * 330
    sheet = Image.new("RGB", (1440, sheet_height), (24, 27, 34))
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(rows):
        x = (index % 2) * 720
        y = (index // 2) * 330
        image = Image.open(rows[name]["screenshot"]).convert("RGB")
        image.thumbnail((700, 290), Image.Resampling.LANCZOS)
        sheet.paste(image, (x + (720 - image.width) // 2, y + 30))
        draw.text((x + 10, y + 8), name, fill=(245, 245, 245))
    sheet_path = os.path.join(args.out, "contact_sheet.png")
    sheet.save(sheet_path)

    for left, right, label in (("seed_msaa0", "direct_msaa0", "msaa0"),
                               ("seed_msaa4", "direct_msaa4", "msaa4")):
        if left not in rows or right not in rows:
            continue
        diff = np.abs(screenshots[left].astype(np.int16)
                      - screenshots[right].astype(np.int16)).max(axis=2)
        diff_image = np.clip(diff.astype(np.float32) * 8.0, 0, 255).astype(np.uint8)
        Image.fromarray(np.repeat(diff_image[:, :, None], 3, axis=2)).save(
            os.path.join(args.out, f"diff_{label}_x8.png"))

    report = {
        "schema": "gx-sky-precision-check-v1",
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"],
                                         cwd=ROOT, text=True).strip(),
        "frame_trigger": args.frame,
        "screenshot_pair_capture_note": "The screenshot is the previously presented frame at frame-1; native direct-HDR and LDR sky sources are both captured in that same frame before scene seeding.",
        "sky_roi_note": "Pixel ROI is the left 1%-8% of the screen, from y=6%-98%; it avoids the Y2K props and HUD and samples the full clean sky gradient.",
        "sky_source_pair_captures": paired_sources,
        "presentation_comparisons": mode_comparisons,
        "no_sky_fallback_comparisons": fallback_comparisons,
        "cases": {name: {key: value for key, value in row.items()
                         if key not in ("pair_paths", "log")}
                  for name, row in rows.items()},
        "interpretation": "Source pairs are direct same-frame GPU RT readbacks. Presentation A/B runs use the same fixed timestep, frame trigger, TAA and dither settings, but are separate runs.",
        "contact_sheet": sheet_path,
    }
    report_path = os.path.join(args.out, "report.json")
    with open(report_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"same-frame source pairs: {len(paired_sources)}")
    for comparison in mode_comparisons:
        sky = comparison["screenshot_metrics"]["sky_roi"]
        print(f"{comparison['seed_case']} vs {comparison['direct_case']}: "
              f"sky ROI mean={sky['mean_abs_rgb']:.4f} code, "
              f">1-code pixels={sky['fraction_pixels_over_1_code']:.4%}")
    for comparison in fallback_comparisons:
        print(f"{comparison['baseline_case']} vs {comparison['direct_requested_case']}: "
              f"identical={comparison['screenshots_identical']}")
    print(f"report: {report_path}")
    print(f"contact sheet: {sheet_path}")
    print("SKY PRECISION CHECK: CAPTURED (perceptual disposition is advisory)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"SKY PRECISION CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
