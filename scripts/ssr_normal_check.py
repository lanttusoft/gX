#!/usr/bin/env python3
#ai: opt-in SSR normal-buffer diagnostic lane. It validates the GXNRM1
#   RGBA8 compatibility readback (not native storage precision), captures
#   normals-off/on A/Bs for minimal and planar
#   scenes, and reports advisory reflection deltas without changing gates.

import glob
import json
import os
import struct
import subprocess
import sys
from datetime import datetime, timezone

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = str(audit.ROOT)
BIN = str(audit.BIN)
OUT = "/tmp/opencode/ssr_normal_check"
#opencode: allow advisory threshold sweeps without changing the production hook.
FALLBACK_DEGREES = os.environ.get("GX_SSR_NORMAL_FALLBACK_TEST_DEG", "10")
HYBRID_DEGREES = os.environ.get("GX_SSR_NORMAL_LIFTOFF_TEST_DEG", "1")
LIFT_VARIANTS = {"depth": "1", "none": "2", "half": "3", "hybrid": "4"}

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_reflect_verify as P  # noqa: E402
import probe_nonplanar_verify as NP  # noqa: E402


def capture(tag, mode, enabled, extra):
    directory = os.path.join(OUT, tag)
    os.makedirs(directory, exist_ok=True)
    dump = os.path.join(directory, "normals.rgba")
    environment = audit.scene_env({
        "GX_SSR_NORMALS": "1" if enabled else "0",
        "GX_T_SSR": "1",
        "GX_T_TAA": "0",
        "GX_FIXED_DT": "0.0166666667",
        "GX_CAM_YAW": "30",
        "GX_CAM_PITCH": "25",
        "GX_CAM_DIST": "6",
    }, clean=True)
    environment.pop("GX_RT_VIEW", None)
    environment.pop("GX_SSR_NORMAL_DUMP", None)
    environment.pop("GX_SSR_NORMAL_FALLBACK_DEG", None)
    environment.pop("GX_SSR_NORMAL_LIFTOFF_MODE", None)
    environment.pop("GX_SSR_NORMAL_LIFTOFF_DEG", None)
    environment.pop("GX_SSR_NORMAL_ACCEPT_MODE", None)
    if enabled:
        environment["GX_SSR_NORMAL_DUMP"] = dump
    environment.update(extra)
    before = set(glob.glob(os.path.join(directory, "gXss_*.png")))
    audit.run_scene(mode, environment, width=None, height=None, fps=None,
                    screenshot_dir=directory, shot_frame=None, timeout=120,
                    check=True)
    fresh = [path for path in glob.glob(os.path.join(directory, "gXss_*.png"))
             if path not in before]
    if not fresh:
        raise RuntimeError("%s: no screenshot produced" % tag)
    screenshot = max(fresh, key=os.path.getmtime)
    return screenshot, dump if enabled else None


def read_normal_dump(path, tag):
    with open(path, "rb") as stream:
        payload = stream.read()
    if len(payload) < 20:
        raise RuntimeError("%s: normal dump is truncated" % tag)
    magic, width, height, channels = struct.unpack_from("<8sIII", payload, 0)
    if magic != b"GXNRM1\x00\x00" or channels != 4:
        raise RuntimeError("%s: unexpected normal dump header" % tag)
    expected = width * height * channels
    if len(payload) != 20 + expected:
        raise RuntimeError("%s: normal dump size mismatch" % tag)
    pixels = np.frombuffer(payload, dtype=np.uint8, offset=20).reshape(
        (height, width, channels)).copy()
    valid = pixels[:, :, 3] > 127
    coverage = float(valid.mean())
    if coverage <= 0.01 or coverage >= 0.995:
        raise RuntimeError("%s: suspicious normal coverage %.2f%%" % (tag, coverage * 100.0))
    decoded = pixels[:, :, :3].astype(np.float32) / 127.5 - 1.0
    lengths = np.linalg.norm(decoded, axis=2)
    if float(np.max(np.abs(lengths[valid] - 1.0))) > 0.04:
        raise RuntimeError("%s: encoded normals are not unit length" % tag)
    Image.fromarray(np.flipud(pixels[:, :, :3]), mode="RGB").save(
        os.path.join(os.path.dirname(path), "normal_decode.png"))
    return {
        "width": int(width),
        "height": int(height),
        "coverage": coverage,
        "alpha_values": [int(value) for value in np.unique(pixels[:, :, 3])],
        "mean_normal": [float(value) for value in decoded[valid].mean(axis=0)],
        "max_unit_length_error": float(np.max(np.abs(lengths[valid] - 1.0))),
        "dump": os.path.abspath(path),
        "decoded_view": os.path.abspath(os.path.join(os.path.dirname(path), "normal_decode.png")),
    }


def image_metrics(off_path, on_path):
    off = np.asarray(Image.open(off_path).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(on_path).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape or not np.isfinite(off).all() or not np.isfinite(on).all():
        raise RuntimeError("SSR normal A/B image mismatch or non-finite pixels")
    height, width = off.shape[:2]
    rows = (int(height * 0.20), int(height * 0.75))
    cols = (int(width * 0.25), int(width * 0.75))
    delta = np.abs(on - off)
    region = delta[rows[0]:rows[1], cols[0]:cols[1]]
    return {
        "whole_mean_abs_channel_delta": float(delta.mean()),
        "roi_mean_abs_channel_delta": float(region.mean()),
        "roi_fraction_pixels_changed": float((region.max(axis=2) > 2.0).mean()),
        "roi": {"rows": list(rows), "cols": list(cols)},
    }


def heatmap_metrics(path):
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    height, width = image.shape[:2]
    rows = (int(height * 0.20), int(height * 0.75))
    cols = (int(width * 0.25), int(width * 0.75))
    def classify(region):
        aligned = ((region[:, :, 1] > region[:, :, 0] + 20.0)
                   & (region[:, :, 1] > region[:, :, 2] + 20.0))
        disagreement = ((region[:, :, 0] > region[:, :, 1] + 20.0)
                        & (region[:, :, 0] > region[:, :, 2] + 20.0))
        uncovered = ((region[:, :, 2] > region[:, :, 0] + 20.0)
                     & (region[:, :, 2] > region[:, :, 1] + 20.0))
        active = aligned | disagreement | uncovered
        return aligned, disagreement, active

    region = image[rows[0]:rows[1], cols[0]:cols[1]]
    aligned, disagreement, active = classify(region)
    result = {
        "aligned_fraction": float(aligned[active].mean()) if active.any() else 0.0,
        "disagreement_fraction": float(disagreement[active].mean()) if active.any() else 0.0,
        "uncovered_fraction": float((~(aligned | disagreement))[active].mean()) if active.any() else 0.0,
    }
    for label, bounds in {
        "cube": (0.14, 0.50, 0.43, 0.70),
        "sphere": (0.30, 0.55, 0.24, 0.40),
    }.items():
        r0, r1, c0, c1 = bounds
        sub = image[int(height * r0):int(height * r1),
                    int(width * c0):int(width * c1)]
        sub_aligned, sub_disagreement, sub_active = classify(sub)
        result["%s_disagreement_fraction" % label] = (
            float(sub_disagreement[sub_active].mean()) if sub_active.any() else 0.0)

    result["mean_blue_angle_proxy"] = float(region[:, :, 2][active].mean() / 255.0) if active.any() else 0.0
    result["heatmap"] = os.path.abspath(path)
    result["roi"] = {"rows": list(rows), "cols": list(cols)}
    return result


def edge_hit_metrics(heat_path, hit_path):
    heat = np.asarray(Image.open(heat_path).convert("RGB"), dtype=np.float32)
    hit = np.asarray(Image.open(hit_path).convert("RGB"), dtype=np.float32)
    if heat.shape != hit.shape:
        raise RuntimeError("heatmap/hit-map size mismatch")
    edge = ((heat[:, :, 0] > heat[:, :, 1] + 20.0)
            & (heat[:, :, 0] > heat[:, :, 2] + 20.0))
    geometry = ((hit[:, :, 0] > hit[:, :, 1] + 20.0)
                & (hit[:, :, 0] > hit[:, :, 2] + 20.0))
    sky = ((hit[:, :, 1] > hit[:, :, 0] + 20.0)
           & (hit[:, :, 1] > hit[:, :, 2] + 20.0))
    environment = ((hit[:, :, 2] > hit[:, :, 0] + 20.0)
                   & (hit[:, :, 2] > hit[:, :, 1] + 20.0))
    count = int(edge.sum())
    return {
        "edge_pixels": count,
        "edge_geometry_hit_fraction": float(geometry[edge].mean()) if count else 0.0,
        "edge_sky_hit_fraction": float(sky[edge].mean()) if count else 0.0,
        "edge_environment_fraction": float(environment[edge].mean()) if count else 0.0,
        "hit_map": os.path.abspath(hit_path),
    }


def decode_probe(image):
    decoded = image.astype(np.float32) / 127.5 - 1.0
    lengths = np.linalg.norm(decoded, axis=2, keepdims=True)
    return decoded / np.maximum(lengths, 1.0e-6)


def hit_classes(path):
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    geometry = ((image[:, :, 0] > image[:, :, 1] + 20.0)
                & (image[:, :, 0] > image[:, :, 2] + 20.0))
    sky = ((image[:, :, 1] > image[:, :, 0] + 20.0)
           & (image[:, :, 1] > image[:, :, 2] + 20.0))
    environment = ((image[:, :, 2] > image[:, :, 0] + 20.0)
                   & (image[:, :, 2] > image[:, :, 1] + 20.0))
    classes = np.full(image.shape[:2], -1, dtype=np.int8)
    classes[geometry] = 0
    classes[sky] = 1
    classes[environment] = 2
    return classes


def ray_direction_metrics(ray_off_path, ray_on_path, hit_off_path, hit_on_path):
    ray_off = np.asarray(Image.open(ray_off_path).convert("RGB"), dtype=np.float32)
    ray_on = np.asarray(Image.open(ray_on_path).convert("RGB"), dtype=np.float32)
    if ray_off.shape != ray_on.shape:
        raise RuntimeError("ray probe size mismatch")
    active = ((ray_off.max(axis=2) > 32.0)
              & (ray_on.max(axis=2) > 32.0))
    off_dir = decode_probe(ray_off)
    on_dir = decode_probe(ray_on)
    angle = np.degrees(np.arccos(np.clip(np.sum(off_dir * on_dir, axis=2), -1.0, 1.0)))
    hit_off = hit_classes(hit_off_path)
    hit_on = hit_classes(hit_on_path)
    if hit_off.shape != angle.shape or hit_on.shape != angle.shape:
        raise RuntimeError("ray probe/hit map size mismatch")
    classified = active & (hit_off >= 0) & (hit_on >= 0)
    changed_hit = classified & (hit_off != hit_on)
    stable_hit = classified & ~changed_hit
    return {
        "active_fraction": float(active.mean()),
        "mean_angle_degrees": float(angle[active].mean()) if active.any() else 0.0,
        "p95_angle_degrees": float(np.percentile(angle[active], 95.0)) if active.any() else 0.0,
        "fraction_over_1_degree": float((angle[active] > 1.0).mean()) if active.any() else 0.0,
        "fraction_over_5_degrees": float((angle[active] > 5.0).mean()) if active.any() else 0.0,
        "classified_hit_fraction": float(classified[active].mean()) if active.any() else 0.0,
        "hit_class_change_fraction": float(changed_hit[classified].mean()) if classified.any() else 0.0,
        "mean_angle_changed_hit": float(angle[changed_hit].mean()) if changed_hit.any() else 0.0,
        "mean_angle_stable_hit": float(angle[stable_hit].mean()) if stable_hit.any() else 0.0,
        "ray_off": os.path.abspath(ray_off_path),
        "ray_on": os.path.abspath(ray_on_path),
        "hit_off": os.path.abspath(hit_off_path),
        "hit_on": os.path.abspath(hit_on_path),
    }


def ray_start_metrics(start_off_path, start_on_path, hit_off_path, hit_on_path):
    start_off = np.asarray(Image.open(start_off_path).convert("RGB"), dtype=np.float32)
    start_on = np.asarray(Image.open(start_on_path).convert("RGB"), dtype=np.float32)
    if start_off.shape != start_on.shape:
        raise RuntimeError("ray-start probe size mismatch")
    active = ((start_off.max(axis=2) > 32.0)
              & (start_on.max(axis=2) > 32.0))
    delta = np.linalg.norm((start_on - start_off) / 255.0 * 2.0, axis=2)
    hit_off = hit_classes(hit_off_path)
    hit_on = hit_classes(hit_on_path)
    classified = active & (hit_off >= 0) & (hit_on >= 0)
    changed_hit = classified & (hit_off != hit_on)
    stable_hit = classified & ~changed_hit
    return {
        "active_fraction": float(active.mean()),
        "mean_delta": float(delta[active].mean()) if active.any() else 0.0,
        "p95_delta": float(np.percentile(delta[active], 95.0)) if active.any() else 0.0,
        "fraction_over_0_01": float((delta[active] > 0.01).mean()) if active.any() else 0.0,
        "mean_delta_changed_hit": float(delta[changed_hit].mean()) if changed_hit.any() else 0.0,
        "mean_delta_stable_hit": float(delta[stable_hit].mean()) if stable_hit.any() else 0.0,
        "start_off": os.path.abspath(start_off_path),
        "start_on": os.path.abspath(start_on_path),
    }


def march_metrics(reject_off_path, reject_on_path, hit_off_path, hit_on_path):
    reject_off = np.asarray(Image.open(reject_off_path).convert("RGB"), dtype=np.float32)
    reject_on = np.asarray(Image.open(reject_on_path).convert("RGB"), dtype=np.float32)
    if reject_off.shape != reject_on.shape:
        raise RuntimeError("march diagnostic size mismatch")
    hit_off = hit_classes(hit_off_path)
    hit_on = hit_classes(hit_on_path)
    classified = (hit_off >= 0) & (hit_on >= 0)
    changed_hit = classified & (hit_off != hit_on)
    stable_hit = classified & ~changed_hit
    delta = np.abs(reject_on - reject_off) / 255.0
    return {
        "classified_fraction": float(classified.mean()),
        "hit_class_change_fraction": float(changed_hit[classified].mean()) if classified.any() else 0.0,
        "mean_confirm_delta": float(delta[:, :, 0][classified].mean()) if classified.any() else 0.0,
        "mean_phantom_delta": float(delta[:, :, 1][classified].mean()) if classified.any() else 0.0,
        "mean_cross_delta": float(delta[:, :, 2][classified].mean()) if classified.any() else 0.0,
        "changed_hit_confirm_delta": float(delta[:, :, 0][changed_hit].mean()) if changed_hit.any() else 0.0,
        "changed_hit_phantom_delta": float(delta[:, :, 1][changed_hit].mean()) if changed_hit.any() else 0.0,
        "changed_hit_cross_delta": float(delta[:, :, 2][changed_hit].mean()) if changed_hit.any() else 0.0,
        "stable_hit_confirm_delta": float(delta[:, :, 0][stable_hit].mean()) if stable_hit.any() else 0.0,
        "stable_hit_phantom_delta": float(delta[:, :, 1][stable_hit].mean()) if stable_hit.any() else 0.0,
        "stable_hit_cross_delta": float(delta[:, :, 2][stable_hit].mean()) if stable_hit.any() else 0.0,
        "reject_off": os.path.abspath(reject_off_path),
        "reject_on": os.path.abspath(reject_on_path),
    }


def score(path):
    try:
        report, _ = P.trace(path, 30.0, 25.0, 6.0,
                            os.path.join(OUT, "trace"), plane_y=None,
                            cube_rot=P.parse_cube_rot("default"))
        return {"cube_fidelity_pct": report["cube_fidelity_pct"],
                "cube_zone_pixels": report["zones"]["CUBE"]["pixels"]}
    except Exception as error:
        return {"error": str(error)}


def run_case(name, mode, extra):
    off_path, _ = capture(name + "_off", mode, False, extra)
    on_path, dump_path = capture(name + "_on", mode, True, extra)
    heat_path, _ = capture(name + "_heat", mode, True,
                           {**extra, "GX_SSR_DEBUG": "28"})
    hit_off_path, _ = capture(name + "_hit_off", mode, False,
                              {**extra, "GX_SSR_DEBUG": "14"})
    hit_on_path, _ = capture(name + "_hit_on", mode, True,
                              {**extra, "GX_SSR_DEBUG": "14"})
    ray_off_path, _ = capture(name + "_ray_off", mode, False,
                              {**extra, "GX_SSR_DEBUG": "2"})
    ray_on_path, _ = capture(name + "_ray_on", mode, True,
                             {**extra, "GX_SSR_DEBUG": "2"})
    start_off_path, _ = capture(name + "_start_off", mode, False,
                                {**extra, "GX_SSR_DEBUG": "30"})
    start_on_path, _ = capture(name + "_start_on", mode, True,
                               {**extra, "GX_SSR_DEBUG": "30"})
    reject_off_path, _ = capture(name + "_reject_off", mode, False,
                                 {**extra, "GX_SSR_DEBUG": "11"})
    reject_on_path, _ = capture(name + "_reject_on", mode, True,
                                {**extra, "GX_SSR_DEBUG": "11"})
    fallback_path, _ = capture(name + "_fallback", mode, True,
                               {**extra, "GX_SSR_NORMAL_FALLBACK_DEG": FALLBACK_DEGREES})
    accept_path, _ = capture(name + "_accept", mode, True,
                             {**extra, "GX_SSR_NORMAL_ACCEPT_MODE": "1"})
    lift_paths = {}
    for label, lift_mode in LIFT_VARIANTS.items():
        lift_extra = {**extra, "GX_SSR_NORMAL_LIFTOFF_MODE": lift_mode}
        if label == "hybrid":
            lift_extra["GX_SSR_NORMAL_LIFTOFF_DEG"] = HYBRID_DEGREES
        lift_paths[label], _ = capture(
            name + "_lift_" + label, mode, True,
            lift_extra)
    normal = read_normal_dump(dump_path, name)
    result = image_metrics(off_path, on_path)
    result["normals"] = normal
    result["heatmap"] = heatmap_metrics(heat_path)
    result["edge_hits"] = edge_hit_metrics(heat_path, hit_on_path)
    result["ray_directions"] = ray_direction_metrics(
        ray_off_path, ray_on_path, hit_off_path, hit_on_path)
    result["ray_starts"] = ray_start_metrics(
        start_off_path, start_on_path, hit_off_path, hit_on_path)
    result["march_diagnostics"] = march_metrics(
        reject_off_path, reject_on_path, hit_off_path, hit_on_path)
    result["off_score"] = score(off_path)
    result["on_score"] = score(on_path)
    result["fallback_score"] = score(fallback_path)
    result["fallback_image"] = image_metrics(off_path, fallback_path)
    result["accept_score"] = score(accept_path)
    result["accept_image"] = image_metrics(off_path, accept_path)
    result["lift_variants"] = {
        label: {
            "score": score(path),
            "image": image_metrics(off_path, path),
            "capture": os.path.abspath(path),
        }
        for label, path in lift_paths.items()
    }
    if "GX_NONPLANAR_TEST" in extra:
        try:
            off_report, _ = NP.verify(off_path, 30.0, 25.0, 6.0)
            on_report, _ = NP.verify(on_path, 30.0, 25.0, 6.0)
            result["off_nonplanar"] = {
                "planar_zone_frac": off_report["planar_zone_frac"],
                "cube_zone_frac": off_report["cube_zone_frac"],
            }
            result["on_nonplanar"] = {
                "planar_zone_frac": on_report["planar_zone_frac"],
                "cube_zone_frac": on_report["cube_zone_frac"],
            }
            fallback_report, _ = NP.verify(fallback_path, 30.0, 25.0, 6.0)
            result["fallback_nonplanar"] = {
                "planar_zone_frac": fallback_report["planar_zone_frac"],
                "cube_zone_frac": fallback_report["cube_zone_frac"],
            }
            accept_report, _ = NP.verify(accept_path, 30.0, 25.0, 6.0)
            result["accept_nonplanar"] = {
                "planar_zone_frac": accept_report["planar_zone_frac"],
                "cube_zone_frac": accept_report["cube_zone_frac"],
            }
            result["lift_nonplanar"] = {}
            for label, path in lift_paths.items():
                lift_report, _ = NP.verify(path, 30.0, 25.0, 6.0)
                result["lift_nonplanar"][label] = {
                    "planar_zone_frac": lift_report["planar_zone_frac"],
                    "cube_zone_frac": lift_report["cube_zone_frac"],
                }
        except Exception as error:
            result["nonplanar_error"] = str(error)
    result["off_capture"] = os.path.abspath(off_path)
    result["on_capture"] = os.path.abspath(on_path)
    return result


def main():
    if not os.path.isfile(BIN):
        raise RuntimeError("missing build binary: %s" % BIN)
    os.makedirs(OUT, exist_ok=True)
    with audit.settings_preserved():
        return run_report()


def run_report():
    result = {
        "schema": 1,
        "advisory": True,
        "fallback_threshold_degrees": FALLBACK_DEGREES,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "minimal": run_case("minimal", "--showcase-minimal", {}),
        "planar": run_case("planar", "--showcase-planar", {
            "GX_NONPLANAR_TEST": "1",
            "GX_DIAG_CUBE": "1",
        }),
    }
    minimal_regression = result["minimal"]["on_score"].get("cube_fidelity_pct", 0.0) < result["minimal"]["off_score"].get("cube_fidelity_pct", 0.0)
    planar_regression = result["planar"]["on_score"].get("cube_fidelity_pct", 0.0) < result["planar"]["off_score"].get("cube_fidelity_pct", 0.0)
    nonplanar_regression = (
        "off_nonplanar" in result["planar"]
        and (result["planar"]["on_nonplanar"]["planar_zone_frac"] < result["planar"]["off_nonplanar"]["planar_zone_frac"]
             or result["planar"]["on_nonplanar"]["cube_zone_frac"] < result["planar"]["off_nonplanar"]["cube_zone_frac"]))
    result["graduation_verdict"] = "HOLD" if (minimal_regression or planar_regression or nonplanar_regression) else "CANDIDATE"
    path = os.path.join(OUT, "report.json")
    with open(path, "w", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print("SSR-NORMAL-CHECK: ADVISORY")
    for name in ("minimal", "planar"):
        case = result[name]
        normal = case["normals"]
        print("  %-7s coverage %.1f%% unit-error %.4f ROI delta %.3f changed %.1f%%" % (
            name, normal["coverage"] * 100.0, normal["max_unit_length_error"],
            case["roi_mean_abs_channel_delta"],
            case["roi_fraction_pixels_changed"] * 100.0))
        print("           off/on score: %s / %s" % (
            case["off_score"].get("cube_fidelity_pct", "n/a"),
            case["on_score"].get("cube_fidelity_pct", "n/a")))
        print("           %s° fallback score: %s ROI delta %.3f" % (
            FALLBACK_DEGREES,
            case["fallback_score"].get("cube_fidelity_pct", "n/a"),
            case["fallback_image"]["roi_mean_abs_channel_delta"]))
        print("           relaxed phantom score: %s ROI delta %.3f" % (
            case["accept_score"].get("cube_fidelity_pct", "n/a"),
            case["accept_image"]["roi_mean_abs_channel_delta"]))
        print("           lift variants depth/none/half/hybrid: %s/%s/%s/%s" % (
            case["lift_variants"]["depth"]["score"].get("cube_fidelity_pct", "n/a"),
            case["lift_variants"]["none"]["score"].get("cube_fidelity_pct", "n/a"),
            case["lift_variants"]["half"]["score"].get("cube_fidelity_pct", "n/a"),
            case["lift_variants"]["hybrid"]["score"].get("cube_fidelity_pct", "n/a")))
        print("           lift ROI depth/none/half/hybrid: %.3f/%.3f/%.3f/%.3f" % (
            case["lift_variants"]["depth"]["image"]["roi_mean_abs_channel_delta"],
            case["lift_variants"]["none"]["image"]["roi_mean_abs_channel_delta"],
            case["lift_variants"]["half"]["image"]["roi_mean_abs_channel_delta"],
            case["lift_variants"]["hybrid"]["image"]["roi_mean_abs_channel_delta"]))
        print("           heat aligned/disagree: %.1f%%/%.1f%% angle %.4f" % (
            case["heatmap"]["aligned_fraction"] * 100.0,
            case["heatmap"]["disagreement_fraction"] * 100.0,
            case["heatmap"]["mean_blue_angle_proxy"]))
        print("           heat cube/sphere disagreement: %.1f%%/%.1f%%" % (
            case["heatmap"]["cube_disagreement_fraction"] * 100.0,
            case["heatmap"]["sphere_disagreement_fraction"] * 100.0))
        print("           edge hit geom/sky/env: %.1f%%/%.1f%%/%.1f%% (%d px)" % (
            case["edge_hits"]["edge_geometry_hit_fraction"] * 100.0,
            case["edge_hits"]["edge_sky_hit_fraction"] * 100.0,
            case["edge_hits"]["edge_environment_fraction"] * 100.0,
            case["edge_hits"]["edge_pixels"]))
        print("           ray delta mean/p95: %.3f°/%.3f° >1° %.1f%% >5° %.1f%%" % (
            case["ray_directions"]["mean_angle_degrees"],
            case["ray_directions"]["p95_angle_degrees"],
            case["ray_directions"]["fraction_over_1_degree"] * 100.0,
            case["ray_directions"]["fraction_over_5_degrees"] * 100.0))
        print("           ray delta hit-changed/stable: %.3f°/%.3f°; classified %.1f%%, class changes %.1f%%" % (
            case["ray_directions"]["mean_angle_changed_hit"],
            case["ray_directions"]["mean_angle_stable_hit"],
            case["ray_directions"]["classified_hit_fraction"] * 100.0,
            case["ray_directions"]["hit_class_change_fraction"] * 100.0))
        print("           start delta mean/p95: %.5f/%.5f >.01 %.1f%%; changed/stable %.5f/%.5f" % (
            case["ray_starts"]["mean_delta"],
            case["ray_starts"]["p95_delta"],
            case["ray_starts"]["fraction_over_0_01"] * 100.0,
            case["ray_starts"]["mean_delta_changed_hit"],
            case["ray_starts"]["mean_delta_stable_hit"]))
        print("           march Δ confirm/phantom/cross: %.4f/%.4f/%.4f changed; %.4f/%.4f/%.4f stable" % (
            case["march_diagnostics"]["changed_hit_confirm_delta"],
            case["march_diagnostics"]["changed_hit_phantom_delta"],
            case["march_diagnostics"]["changed_hit_cross_delta"],
            case["march_diagnostics"]["stable_hit_confirm_delta"],
            case["march_diagnostics"]["stable_hit_phantom_delta"],
            case["march_diagnostics"]["stable_hit_cross_delta"]))
        if "off_nonplanar" in case:
            print("           nonplanar planar/cube: %.3f/%.3f -> %.3f/%.3f" % (
                case["off_nonplanar"]["planar_zone_frac"],
                case["off_nonplanar"]["cube_zone_frac"],
                case["on_nonplanar"]["planar_zone_frac"],
                case["on_nonplanar"]["cube_zone_frac"]))
            print("           fallback planar/cube: %.3f/%.3f" % (
                case["fallback_nonplanar"]["planar_zone_frac"],
                case["fallback_nonplanar"]["cube_zone_frac"]))
            print("           relaxed phantom planar/cube: %.3f/%.3f" % (
                case["accept_nonplanar"]["planar_zone_frac"],
                case["accept_nonplanar"]["cube_zone_frac"]))
            print("           lift depth/none/half/hybrid planar: %.3f/%.3f/%.3f/%.3f" % (
                case["lift_nonplanar"]["depth"]["planar_zone_frac"],
                case["lift_nonplanar"]["none"]["planar_zone_frac"],
                case["lift_nonplanar"]["half"]["planar_zone_frac"],
                case["lift_nonplanar"]["hybrid"]["planar_zone_frac"]))
            print("           lift depth/none/half/hybrid cube: %.3f/%.3f/%.3f/%.3f" % (
                case["lift_nonplanar"]["depth"]["cube_zone_frac"],
                case["lift_nonplanar"]["none"]["cube_zone_frac"],
                case["lift_nonplanar"]["half"]["cube_zone_frac"],
                case["lift_nonplanar"]["hybrid"]["cube_zone_frac"]))
    print("  Graduation verdict: %s (advisory; production default unchanged)" % result["graduation_verdict"])
    print("  Report: %s" % path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print("SSR-NORMAL-CHECK: FAIL — %s" % error)
        sys.exit(1)
