#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: capture native GFX render-target floats and matching temporal
#   screenshot bursts for SSR, SSAO/SSGI, temporal history, and fog review.

import argparse
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
BURST_LEN = 12
DEFAULT_FRAME = 120

VIEWS = {
    "ssr": (0.5, 0.0, False),
    "ssrNoTAA": (0.5, 0.0, False),
    "velocity": (20.0, 0.5, 0.1),
    "velocityStatic": (20.0, 0.5, 0.0),
    "depth": (1.0, 0.0, False),
    "taaA": (1.0, 0.0, False),
    "taaB": (1.0, 0.0, False),
    "ssao": (1.0, 0.0, False),
    "ssaoBlur": (1.0, 0.0, False),
    "gtao": (1.0, 0.0, False),
    "gtaoA": (1.0, 0.0, False),
    "gtaoB": (1.0, 0.0, False),
    "ssgiRaw": (0.5, 0.0, False),
    "ssgiBlur": (0.5, 0.0, False),
    "ssgi": (0.5, 0.0, False),
    "ssgiA": (0.5, 0.0, False),
    "ssgiB": (0.5, 0.0, False),
    "fog": (3.0, 0.5, False),
    "fogNoTAA": (3.0, 0.5, False),
    "skyLut": (1.0, 0.0, False),
    "fogLut": (3.0, 0.5, False),
}

QUICK_VIEWS = {"ssr", "velocity", "velocityStatic", "taaA", "ssao",
               "gtao", "ssgi", "fog"}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_dump(path):
    with open(path, "rb") as stream:
        header = stream.read(20)
        if len(header) != 20 or header[:8] != b"GXRTF32\0":
            raise RuntimeError(f"invalid RT dump header: {path}")
        width, height, channels = struct.unpack("<III", header[8:])
        payload = stream.read()
    values = np.frombuffer(payload, dtype="<f4")
    expected = width * height * channels
    if width == 0 or height == 0 or channels not in (1, 4) or values.size != expected:
        raise RuntimeError(f"invalid RT dump dimensions/data: {path}")
    return values.reshape((height, width, channels)).copy()


def value_stats(values):
    labels = ["R", "G", "B", "A"] if values.shape[2] == 4 else ["Z"]
    finite = np.isfinite(values)
    result = {
        "finite_fraction": float(finite.mean()),
        "channels": {},
    }
    for channel, label in enumerate(labels):
        plane = values[:, :, channel]
        valid = plane[np.isfinite(plane)]
        if valid.size == 0:
            result["channels"][label] = None
            continue
        result["channels"][label] = {
            "min": float(valid.min()),
            "max": float(valid.max()),
            "mean": float(valid.mean()),
            "std": float(valid.std()),
            "p01": float(np.percentile(valid, 1)),
            "p50": float(np.percentile(valid, 50)),
            "p99": float(np.percentile(valid, 99)),
            "near_zero_fraction": float((np.abs(valid) < 1e-6).mean()),
        }
    return result


def semantic_stats(name, values):
    result = {}
    if name in ("velocity", "velocityStatic") and values.shape[2] == 4:
        valid = values[:, :, 2] > 0.5
        vectors = values[:, :, :2][valid]
        magnitude = np.linalg.norm(vectors, axis=1) if vectors.size else np.array([])
        result["valid_pixel_fraction"] = float(valid.mean())
        result["glass_pixel_fraction"] = float((values[:, :, 3] < 0.5).mean())
        result["valid_motion_fraction_over_1e-4"] = (
            float((magnitude > 1e-4).mean()) if magnitude.size else 0.0)
        if magnitude.size:
            result["valid_motion_uv_magnitude"] = {
                "p50": float(np.percentile(magnitude, 50)),
                "p95": float(np.percentile(magnitude, 95)),
                "p99": float(np.percentile(magnitude, 99)),
                "max": float(magnitude.max()),
            }
    if name in ("ssao", "ssaoBlur", "gtao", "gtaoHist", "gtaoA", "gtaoB"):
        visibility = values[:, :, 0]
        result["visibility_mean"] = float(visibility.mean())
        result["occluded_fraction_below_0.99"] = float((visibility < 0.99).mean())
        result["occluded_fraction_below_0.90"] = float((visibility < 0.90).mean())
    if name in ("ssgiRaw", "ssgiBlur", "ssgi", "ssgiHist", "ssgiA", "ssgiB"):
        radiance = values[:, :, :3]
        luminance = np.mean(radiance, axis=2)
        result["irradiance_mean_rgb"] = [float(radiance[:, :, channel].mean())
                                          for channel in range(3)]
        result["irradiance_pixel_fraction_over_1e-4"] = float(
            (np.max(np.abs(radiance), axis=2) > 1e-4).mean())
        result["irradiance_luminance_p99"] = float(np.percentile(luminance, 99))
    if name in ("fog", "fogNoTAA", "fogLut"):
        delta = values[:, :, :3]
        result["rgb_delta_mean"] = [float(delta[:, :, channel].mean())
                                    for channel in range(3)]
        result["negative_rgb_component_fraction"] = float((delta < -1e-5).mean())
        result["positive_rgb_component_fraction"] = float((delta > 1e-5).mean())
    if name in ("ssr", "ssrNoTAA") and values.shape[2] == 4:
        alpha = values[:, :, 3]
        result["reflection_alpha_coverage_over_1e-3"] = float((alpha > 1e-3).mean())
        result["reflection_rgb_max"] = float(np.max(np.abs(values[:, :, :3])))
    return result


def drift_stats(frames, threshold):
    rows = []
    for previous, current in zip(frames, frames[1:]):
        if previous.shape != current.shape:
            raise RuntimeError("temporal frame dimensions changed within burst")
        diff = np.abs(current - previous)
        pixel_diff = diff.max(axis=2)
        rows.append({
            "mean_abs": float(diff.mean()),
            "p95_abs": float(np.percentile(diff, 95)),
            "fraction_pixels_over_threshold": float((pixel_diff > threshold).mean()),
        })
    means = [row["mean_abs"] for row in rows]
    fractions = [row["fraction_pixels_over_threshold"] for row in rows]
    return {
        "pair_count": len(rows),
        "median_pair_mean_abs": float(np.median(means)),
        "max_pair_mean_abs": float(np.max(means)),
        "median_pair_fraction_over_threshold": float(np.median(fractions)),
        "threshold": threshold,
        "pairs": rows,
    }


def image_drift(paths):
    images = [np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
              for path in paths]
    return drift_stats(images, 2.0)


def capture_view(name, frame, output_dir, velocity_spin):
    case_dir = os.path.join(output_dir, name)
    if os.path.isdir(case_dir):
        shutil.rmtree(case_dir)
    os.makedirs(case_dir, exist_ok=True)
    gain, bias, spin = VIEWS[name]
    if name in ("velocity", "depth"):
        spin = velocity_spin
    env = audit.scene_env({}, clean=True)
    env["DISPLAY"] = env.get("DISPLAY", ":1")
    env["GX_FIXED_DT"] = "0.016666667"
    env["GX_SCREENSHOT_DIR"] = case_dir
    env["GX_BURST"] = "1"
    env["GX_RT_VIEW"] = f"{name}:{gain:g}:{bias:g}"
    env["GX_RT_DUMP_DIR"] = case_dir
    env["GX_RT_DUMP_FRAME"] = str(frame)
    env["GX_RT_DUMP_COUNT"] = str(BURST_LEN)
    taa_enabled = name not in ("fogNoTAA", "ssrNoTAA")
    env["GX_T_TAA"] = "1" if taa_enabled else "0"
    ssr_enabled = name not in ("fog", "fogNoTAA")
    env["GX_T_SSR"] = "1" if ssr_enabled else "0"
    env["GX_T_SSAO"] = "1"
    env["GX_T_FOG"] = "1"
    env["GX_T_BLOOM"] = "1"
    env["GX_T_REFL"] = "1"
    env["GX_T_SSRAUX"] = "0"
    env["GX_T_PARTICLES"] = "0"
    env["GX_T_GODRAY"] = "0"
    env["GX_T_LENSFLARE"] = "0"
    env["GX_T_MOTIONBLUR"] = "0"
    env["GX_T_DOF"] = "0"
    env["GX_T_FXAA"] = "0"
    env["GX_T_PALETTE_MODE"] = "0"
    env["GX_T_FOGPOINTS"] = "1"
    ssgi_enabled = name not in ("fog", "fogNoTAA")
    env["GX_SSGI"] = "1" if ssgi_enabled else "0"
    env["GX_GTAO"] = "1"
    env.pop("GX_T_ORACLE", None)
    env.pop("GX_T_TAASTATS", None)
    env.pop("GX_T_ORACLESEED", None)
    env.pop("GX_SHOT_FRAME", None)
    env.pop("GX_SHOT_FORCE", None)
    if spin:
        env["GX_CAM_SPIN"] = f"{spin:g}"
    else:
        env.pop("GX_CAM_SPIN", None)
    command = [BIN, "--showcase-y2k", "--ai-screenshot-after", str(frame)]
    log_path = os.path.join(case_dir, "renderer.log")
    print(f"capture {name:9s} at frame {frame} -> {case_dir}", flush=True)
    with open(log_path, "wb") as log:
        subprocess.run(command, cwd=ROOT, env=env, check=True, timeout=360,
                       stdout=log, stderr=subprocess.STDOUT)

    shot_paths = [os.path.join(case_dir, f"burst_{index:03d}.png")
                  for index in range(BURST_LEN)]
    dump_paths = [os.path.join(case_dir, f"{name}_{frame + index:06d}.gxrt")
                  for index in range(BURST_LEN)]
    missing = [path for path in shot_paths + dump_paths if not os.path.isfile(path)]
    if missing:
        log_text = open(log_path, "r", encoding="utf-8", errors="replace").read()
        tail = "\n".join(log_text.splitlines()[-35:])
        raise RuntimeError(f"{name}: missing capture artifacts {missing}\n{tail}")
    return (case_dir, shot_paths, dump_paths, log_path, ssgi_enabled,
            ssr_enabled, taa_enabled, spin)


def compare_targets(rows, left, right):
    left_path = rows[left]["dump_paths"][0]
    right_path = rows[right]["dump_paths"][0]
    a = read_dump(left_path)
    b = read_dump(right_path)
    if a.shape != b.shape:
        raise RuntimeError(f"{left}/{right}: ping-pong target size mismatch")
    diff = np.abs(a - b)
    pixel_diff = diff.max(axis=2)
    return {
        "left": left,
        "right": right,
        "shape": list(a.shape),
        "mean_abs": float(diff.mean()),
        "p95_abs": float(np.percentile(diff, 95)),
        "max_abs": float(diff.max()),
        "fraction_pixels_over_1e-4": float((pixel_diff > 1e-4).mean()),
    }


def make_contact_sheet(rows, path):
    columns = 3
    cell_width = 480
    cell_height = 300
    names = list(rows)
    sheet = Image.new("RGB", (columns * cell_width,
                               ((len(names) + columns - 1) // columns) * cell_height),
                      (24, 27, 34))
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(names):
        x = (index % columns) * cell_width
        y = (index // columns) * cell_height
        image = Image.open(rows[name]["shot_paths"][0]).convert("RGB")
        image.thumbnail((cell_width - 16, cell_height - 48), Image.Resampling.LANCZOS)
        sheet.paste(image, (x + (cell_width - image.width) // 2, y + 32))
        native = rows[name]["native_stats"]
        finite = native["finite_fraction"]
        draw.text((x + 10, y + 8),
                  f"{name}   RT {rows[name]['dimensions'][0]}x{rows[name]['dimensions'][1]}   finite {finite:.4f}",
                  fill=(245, 245, 245))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(
        description="Capture and measure SSR, AO/SSGI, temporal, and fog RTs.")
    parser.add_argument("--out", default="/tmp/opencode/gfx-buffer-audit",
                        help="output directory (default: %(default)s)")
    parser.add_argument("--frame", type=int, default=DEFAULT_FRAME,
                        help="first frame to capture (default: %(default)s)")
    parser.add_argument("--velocity-spin", type=float, default=0.1,
                        help="camera degrees/frame for the moving velocity view")
    parser.add_argument("--views", metavar="NAMES",
                        help="comma-separated view names (default: all)")
    parser.add_argument("--quick", action="store_true",
                        help="capture representative buffers only")
    args = parser.parse_args()
    if args.frame < 1:
        parser.error("--frame must be >= 1")
    selected = set(VIEWS)
    if args.quick:
        selected &= QUICK_VIEWS
    if args.views:
        requested = {name.strip() for name in args.views.split(",") if name.strip()}
        unknown = sorted(requested - set(VIEWS))
        if unknown:
            parser.error("unknown view(s): " + ", ".join(unknown))
        selected &= requested
    if not selected:
        parser.error("view selection is empty")
    ordered = [name for name in VIEWS if name in selected]
    os.makedirs(args.out, exist_ok=True)

    rows = {}
    for name in ordered:
        case_dir, shot_paths, dump_paths, log_path, ssgi_enabled, ssr_enabled, taa_enabled, spin = capture_view(
            name, args.frame, args.out, args.velocity_spin)
        dumps = [read_dump(path) for path in dump_paths]
        rows[name] = {
            "case_dir": case_dir,
            "shot_paths": shot_paths,
            "dump_paths": dump_paths,
            "log_path": log_path,
            "dimensions": [int(dumps[0].shape[1]), int(dumps[0].shape[0])],
            "channels": int(dumps[0].shape[2]),
            "case_config": {
                "ssgi_enabled": ssgi_enabled,
                "ssr_enabled": ssr_enabled,
                "taa_enabled": taa_enabled,
                "camera_spin_degrees_per_frame": spin,
            },
            "native_stats": value_stats(dumps[0]),
            "semantic_stats": semantic_stats(name, dumps[0]),
            "native_temporal_drift": drift_stats(dumps, 1e-4),
            "screenshot_temporal_drift": image_drift(shot_paths),
            "first_dump_sha256": sha256(dump_paths[0]),
            "first_screenshot_sha256": sha256(shot_paths[0]),
        }

    comparisons = []
    for left, right in (("taaA", "taaB"), ("gtaoA", "gtaoB"),
                        ("ssgiA", "ssgiB")):
        if left in rows and right in rows:
            comparisons.append(compare_targets(rows, left, right))

    def clean_row(row):
        return {key: value for key, value in row.items()
                if key not in ("shot_paths", "dump_paths", "log_path")}

    report = {
        "schema": "gx-gfx-buffer-audit-v1",
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"],
                                         cwd=ROOT, text=True).strip(),
        "capture": {
            "scene": "--showcase-y2k",
            "first_frame": args.frame,
            "burst_frames": BURST_LEN,
            "fixed_dt_seconds": 0.016666667,
            "features": {
                "taa": "on except fogNoTAA", "ssr": "on except isolated fog targets",
                "ssao": True, "gtao": True,
                "ssgi": "on except isolated fog target", "volume_fog": True, "bloom": True,
                "presentation_dither": False,
            },
            "camera_spin_degrees_per_frame": args.velocity_spin,
            "native_format": "GXRTF32: 8-byte magic, uint32 width/height/channels, float32 payload",
            "interpretation": "Native RT stats are linear source-domain values; screenshot metrics are display-domain and advisory.",
        },
        "targets": {name: clean_row(row) for name, row in rows.items()},
        "ping_pong_comparisons": comparisons,
    }
    report_path = os.path.join(args.out, "report.json")
    with open(report_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    contact_path = os.path.join(args.out, "contact_sheet.png")
    make_contact_sheet(rows, contact_path)

    print("view       size        finite   native mean |delta|   screenshot median |delta|")
    for name in ordered:
        row = rows[name]
        channels = row["native_stats"]["channels"]
        rgb = [channels[key]["mean"] for key in ("R", "G", "B")
               if key in channels and channels[key] is not None]
        mean = float(np.mean(rgb)) if rgb else channels["Z"]["mean"]
        print(f"{name:10s} {row['dimensions'][0]:4d}x{row['dimensions'][1]:<4d} "
              f"{row['native_stats']['finite_fraction']:7.4f} "
              f"{row['native_temporal_drift']['median_pair_mean_abs']:18.7g} "
              f"{row['screenshot_temporal_drift']['median_pair_mean_abs']:24.4f}")
    print(f"report: {report_path}")
    print(f"contact sheet: {contact_path}")
    print("GFX BUFFER AUDIT: CAPTURED (advisory measurements; no quality thresholds applied)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"GFX BUFFER AUDIT: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
