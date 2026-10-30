#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: readback-aware validation of isolated proxy diagnostics, not reflection admission.
"""Check geometry, ownership and visibility separately, enclosing half-float readbacks."""
import argparse
import gzip
import json
from pathlib import Path
import struct

import numpy as np


def read_hdr(path):
    path = Path(path)
    if not path.exists() and path.with_suffix(path.suffix + ".gz").exists():
        path = path.with_suffix(path.suffix + ".gz")
    data = path.read_bytes()
    if path.suffix == ".gz":
        data = gzip.decompress(data)
    if len(data) < 20 or data[:8] != b"GXHDR1\0\0":
        raise ValueError(f"invalid HDR header: {path}")
    width, height, channels = struct.unpack_from("<III", data, 8)
    if not width or not height or channels != 4 or len(data) != 20 + width * height * channels * 4:
        raise ValueError(f"invalid HDR payload: {path}")
    image = np.frombuffer(data, dtype="<f4", offset=20).reshape(height, width, channels)
    if not np.isfinite(image).all():
        raise ValueError(f"nonfinite HDR values: {path}")
    return image


def half_bounds(values):
    half = np.asarray(values, dtype=np.float16)
    if not np.isfinite(half).all() or (half.astype(float) != values).any():
        raise ValueError("finite half-representable readbacks required")
    return (np.nextafter(half, np.float16(-np.inf), dtype=np.float16).astype(float),
            np.nextafter(half, np.float16(np.inf), dtype=np.float16).astype(float))


def normalized_bounds(lower, upper):
    minimum = np.where((lower <= 0) & (upper >= 0), 0, np.minimum(np.abs(lower), np.abs(upper)))
    maximum = np.maximum(np.abs(lower), np.abs(upper))
    length_lower = np.linalg.norm(minimum, axis=-1)
    length_upper = np.linalg.norm(maximum, axis=-1)
    valid = length_lower > 0
    lo = np.where(valid, length_lower, 1)[:, None]
    hi = np.where(valid, length_upper, 1)[:, None]
    candidates = np.stack((lower / lo, lower / hi, upper / lo, upper / hi))
    return candidates.min(axis=0), candidates.max(axis=0), valid


def geometry_bounds(point_lower, point_upper, ray_lower, ray_upper):
    center = np.array([8., 3., -3.])
    extents = np.full(3, float(np.float32(1.2)))
    stable_axis = (ray_lower > 0) | (ray_upper < 0)
    plane_intervals = []
    for sign in (-1, 1):
        numerator_lower = center + sign * extents - point_upper
        numerator_upper = center + sign * extents - point_lower
        a = np.where(stable_axis, ray_lower, 1)
        b = np.where(stable_axis, ray_upper, 1)
        candidates = np.stack((numerator_lower / a, numerator_lower / b,
                               numerator_upper / a, numerator_upper / b))
        plane_intervals.append((candidates.min(axis=0), candidates.max(axis=0)))
    near_lower_axis = np.where(stable_axis, np.minimum(plane_intervals[0][0], plane_intervals[1][0]), -np.inf)
    near_upper_axis = np.where(stable_axis, np.minimum(plane_intervals[0][1], plane_intervals[1][1]), np.inf)
    far_lower_axis = np.where(stable_axis, np.maximum(plane_intervals[0][0], plane_intervals[1][0]), -np.inf)
    far_upper_axis = np.where(stable_axis, np.maximum(plane_intervals[0][1], plane_intervals[1][1]), np.inf)
    near_lower = near_lower_axis.max(axis=-1)
    near_upper = near_upper_axis.max(axis=-1)
    far_lower = far_lower_axis.min(axis=-1)
    far_upper = far_upper_axis.min(axis=-1)
    possible = (far_upper >= np.maximum(near_lower, 0)) & (far_upper > 0)
    certain = (far_lower >= np.maximum(near_upper, 0)) & (far_lower > 0)
    status = np.full(len(point_lower), 2)
    status[~possible] = 0
    status[certain] = 1
    return status, np.maximum(near_lower, 0), np.maximum(near_upper, 0), near_lower_axis, near_upper_axis


def overlapping(a_lower, a_upper, b_lower, b_upper):
    return ((a_lower <= b_upper) & (b_lower <= a_upper)).all(axis=-1)


def selftest_report(image):
    mask = image[:, :, 3] == 7
    columns = np.broadcast_to(np.arange(image.shape[1]), mask.shape)
    expected_status = (1, 0, 0, 1, 2, 0, 2, 2, 1)
    expected_time = (1.8, 0, 0, 1.2, 0, 0, 0, 0, .9)
    rows = []
    for index in range(9):
        selected = image[mask & (columns % 9 == index)]
        if not len(selected):
            raise ValueError("missing GPU selftest case")
        lower, upper = half_bounds(selected[:, 2])
        rows.append({"case": index, "pixels": len(selected),
                     "status_conflicts": int((selected[:, 0] != expected_status[index]).sum()),
                     "time_interval_conflicts": int(((lower > expected_time[index]) | (upper < expected_time[index])).sum())})
    return {"rows": rows, "pass": not any(row["status_conflicts"] or row["time_interval_conflicts"] for row in rows)}


def validate_fixture(preset):
    fixture = json.loads(Path(preset).read_text())
    models = fixture.get("models", [])
    if len(models) != 2 or any(model.get("path") != "resources/model/primitive/cube.glb" for model in models):
        raise ValueError("exact fixture requires only two owned cube primitives")
    for model, position, scale, reflective in zip(models, ([0, 0, 0], [8, 3, -3]),
                                                  ([8, .1, 8], [1.2] * 3), (True, False)):
        params = model.get("params", {})
        if (params.get("pos") != position or params.get("scale") != scale
                or params.get("rotation") != [0, 0, 0]
                or params.get("enableReflections") is not reflective
                or params.get("planarReflection") is not False):
            raise ValueError("fixture transform/reflection contract mismatch")
    camera = fixture.get("camera", {})
    if camera.get("position") != [4, 4, 6] or camera.get("target") != [5.8, .1, .6]:
        raise ValueError("framed diagnostic camera required")
    state = fixture.get("state", {})
    if (state.get("ENABLE_REFLECTIONS") is not True or state.get("ENABLE_MAP_NORMAL") is not False
            or state.get("ENABLE_SSR") is not False):
        raise ValueError("fixture reflection/world-normal contract mismatch")


#ai: native captures use shared hidden-window/settings-preserving audit helpers.
def capture_cases(directory, preset, binary, split=False):
    import audit_common as audit
    validate_fixture(preset)
    directory = Path(directory).resolve()
    preset, binary = Path(preset).resolve(), Path(binary).resolve()
    requests = [(label, debug, mode) for label, debug in (("dynamic", None), ("probe0", "0"),
                 ("probe1", "1"), ("probe2", "2"), ("probe3", "3"))
                for mode in (range(1, 13 if split else 10) if debug is None
                             else ((3, 4, 5, 8, 9, 10, 11, 12) if split else (3, 4, 5, 8, 9)))]
    if any((directory / f"{label}_m{mode}{suffix}").exists()
           for label, _, mode in requests for suffix in (".hdr", ".hdr.gz", ".log")):
        raise ValueError("capture directory already contains diagnostic files; use a fresh directory")
    directory.mkdir(parents=True, exist_ok=True)
    for label, debug, mode in requests:
        hdr = directory / f"{label}_m{mode}.hdr"
        hooks = {"GX_GFX_PRESET_LOAD": str(preset), "GX_PROBE_GRID": "0" if debug is None else "1",
                 "GX_PROBE_SIZE": "512", "GX_PROBE_BOX_CORRECT": "1", "GX_PROBE_BLEND": "0",
                 "GX_PROBE_PROXY_FIXTURE": "1", "GX_PROBE_PROXY_DIAGNOSTIC": str(mode),
                 "GX_FIXED_DT": "0.0166666667", "GX_LAYOUT_NORESEAT": "1",
                 "GX_T_HDRDUMP": "1", "GX_HDRDUMP_FRAME": "35", "GX_HDRDUMP_PATH": str(hdr)}
        if split or mode in (8, 9):
            hooks["GX_PROBE_COVERAGE"] = "2"
        if split:
            hooks["GX_PROBE_PROXY_SPLIT"] = "1"
        if debug is not None:
            hooks["GX_PROBE_DEBUG_INDEX"] = debug
        environment = audit.scene_env(hooks, clean=True)
        for key in ("GX_AI_INPUT", "GX_GFX_PRESET_SAVE", "GX_GFX_PRESET_BATCH"):
            environment.pop(key, None)
        result = audit.run_scene("--gfx", environment, binary=binary,
                                 frames=40, shot_frame=None, extra_args=("--gfx-editor", "--seed", "424242"))
        if result.returncode or not hdr.exists():
            (directory / f"{label}_m{mode}.log").write_text(audit.strip_ansi(result.stdout))
            raise RuntimeError(f"proxy capture failed: {label} mode {mode}")
        data = hdr.read_bytes()
        archive = hdr.with_suffix(hdr.suffix + ".gz")
        archive.write_bytes(gzip.compress(data, compresslevel=6))
        if gzip.decompress(archive.read_bytes()) != data:
            raise RuntimeError("lossless HDR archive verification failed")
        hdr.unlink()
        (directory / f"{label}_m{mode}.log").write_text(audit.strip_ansi(result.stdout))


def color_availability(sampled_distance, geometry, visibility):
    if not np.isfinite(sampled_distance).all() or (sampled_distance < 0).any():
        raise ValueError("finite nonnegative distance response required")
    sky = geometry == 0
    visible = (geometry == 1) & (visibility == 1)
    hidden = (geometry == 1) & (visibility == 2)
    present = sampled_distance > 0
    return {"sky_nonzero_distance_response": int((sky & present).sum()),
            "sky_zero_distance_response": int((sky & ~present).sum()),
            "visible_local_nonzero_distance_response": int((visible & present).sum()),
            "visible_local_zero_distance_response": int((visible & ~present).sum()),
            "hidden_local_nonzero_distance_response": int((hidden & present).sum()),
            "hidden_local_zero_distance_response": int((hidden & ~present).sum()),
            "limits": "Base-level filtered distance response indicates some local capture contribution, not a certified local RGB sample or unmixed coverage."}


def diagnose(directory):
    directory = Path(directory)
    baseline = {mode: read_hdr(directory / f"dynamic_m{mode}.hdr") for mode in range(1, 7)}
    mask = baseline[1][:, :, 3] == 7
    if not mask.any():
        raise ValueError("no proxy diagnostic pixels")
    if any(im.shape != baseline[1].shape or not np.array_equal(im[:, :, 3] == 7, mask)
           for im in baseline.values()):
        raise ValueError("unaligned diagnostic marker masks")
    points = baseline[1][mask, :3]
    rays = baseline[2][mask, :3]
    normals = baseline[6][mask, :3]
    point_lower, point_upper = half_bounds(points)
    ray_lower, ray_upper = half_bounds(rays)
    normal_lower, normal_upper = half_bounds(normals)
    top_normal = np.tile([0., 1., 0.], (len(points), 1))
    normal_conflicts = int((~overlapping(normal_lower, normal_upper, top_normal, top_normal)).sum())
    incident_lower = point_lower - np.array([4., 4., 6.])
    incident_upper = point_upper - np.array([4., 4., 6.])
    incident_lower[:, 1], incident_upper[:, 1] = -incident_upper[:, 1].copy(), -incident_lower[:, 1].copy()
    expected_ray_lower, expected_ray_upper, valid_ray = normalized_bounds(incident_lower, incident_upper)
    outgoing_conflicts = int((valid_ray & ~overlapping(ray_lower, ray_upper, expected_ray_lower, expected_ray_upper)).sum())
    reference, time_lower, time_upper, axis_lower, axis_upper = geometry_bounds(
        point_lower, point_upper, ray_lower, ray_upper)
    entry_axis = axis_lower.argmax(axis=-1)
    other_upper = np.where(np.arange(3)[None, :] == entry_axis[:, None], -np.inf, axis_upper).max(axis=-1)
    unique_face = np.take_along_axis(axis_lower, entry_axis[:, None], axis=-1)[:, 0] > other_upper
    expected_origins = {"dynamic": [4., -4., 6.], "probe0": [-3., 1.5, -3.],
                        "probe1": [3., 1.5, -3.], "probe2": [-3., 1.5, 3.], "probe3": [3., 1.5, 3.]}
    rows = []
    for label, expected_origin in expected_origins.items():
        images = {mode: read_hdr(directory / f"{label}_m{mode}.hdr") for mode in (3, 4, 5)}
        if any(im.shape != baseline[1].shape or not np.array_equal(im[:, :, 3] == 7, mask)
               for im in images.values()):
            raise ValueError(f"unaligned capture diagnostic: {label}")
        geometry, visibility, time = images[3][mask, :3].T
        if not np.isin(geometry, [0, 1, 2]).all() or not np.isin(visibility, [0, 1, 2, 3]).all():
            raise ValueError("invalid geometry/visibility codes")
        capture = images[4][mask, :3]
        capture_lower, capture_upper = half_bounds(capture)
        ownership_conflicts = int((capture != np.asarray(expected_origin)).any(axis=-1).sum())
        local = geometry == 1
        sky = geometry == 0
        unsafe_local = local & (reference == 0)
        unsafe_sky = sky & (reference == 1)
        encoded_time_lower, encoded_time_upper = half_bounds(time)
        time_conflicts = local & (reference == 1) & ((encoded_time_lower > time_upper) | (encoded_time_upper < time_lower))
        row_index = np.arange(len(points))
        positive = ray_lower[row_index, entry_axis] > 0
        plane = np.array([8., 3., -3.])[entry_axis] + np.where(positive, -1, 1) * float(np.float32(1.2))
        side_lower = capture_lower[row_index, entry_axis]
        side_upper = capture_upper[row_index, entry_axis]
        definitely_visible = np.where(positive, side_upper <= plane, side_lower >= plane)
        definitely_hidden = np.where(positive, side_lower > plane, side_upper < plane)
        visibility_resolved = (reference == 1) & unique_face & (definitely_visible | definitely_hidden)
        visibility_conflicts = (local & visibility_resolved
                                & (((visibility == 1) & definitely_hidden)
                                   | ((visibility == 2) & definitely_visible)))
        products = np.stack((encoded_time_lower[:, None] * ray_lower,
                             encoded_time_lower[:, None] * ray_upper,
                             encoded_time_upper[:, None] * ray_lower,
                             encoded_time_upper[:, None] * ray_upper))
        vector_lower = point_lower + products.min(axis=0) - capture_upper
        vector_upper = point_upper + products.max(axis=0) - capture_lower
        lookup_lower, lookup_upper, valid_lookup = normalized_bounds(vector_lower, vector_upper)
        lookup_lower[sky], lookup_upper[sky] = ray_lower[sky], ray_upper[sky]
        actual_lower, actual_upper = half_bounds(images[5][mask, :3])
        lookup_conflicts = ((local & valid_lookup) | sky) & ~overlapping(actual_lower, actual_upper, lookup_lower, lookup_upper)
        availability = None
        color_paths = [directory / f"{label}_m{mode}.hdr" for mode in (8, 9)]
        color_exists = [path.exists() or path.with_suffix(path.suffix + ".gz").exists() for path in color_paths]
        if any(color_exists):
            if not all(color_exists):
                raise ValueError("incomplete color availability captures")
            colors, distance_response = [read_hdr(path) for path in color_paths]
            if any(im.shape != baseline[1].shape or not np.array_equal(im[:, :, 3] == 7, mask)
                   for im in (colors, distance_response)):
                raise ValueError("unaligned color availability captures")
            encoded = distance_response[mask, :3]
            if not np.array_equal(encoded[:, 1], geometry) or not np.array_equal(encoded[:, 2], visibility):
                raise ValueError("color availability geometry/visibility drift")
            availability = color_availability(encoded[:, 0], geometry, visibility)
            availability["robust_sky_nonzero_response"] = int(((geometry == 0) & (reference == 0) & (encoded[:, 0] > 0)).sum())
        rows.append({"capture": label, "expected_origin": expected_origin,
                     "geometry_counts": {str(code): int((geometry == code).sum()) for code in (0, 1, 2)},
                     "visibility_counts": {str(code): int((visibility[local] == code).sum()) for code in (0, 1, 2, 3)},
                     "ownership_conflicts": ownership_conflicts,
                     "resolved_false_local_labels": int(unsafe_local.sum()),
                     "resolved_false_sky_labels": int(unsafe_sky.sum()),
                     "resolved_hit_time_conflicts": int(time_conflicts.sum()),
                     "resolved_visibility_conflicts": int(visibility_conflicts.sum()),
                     "readback_visibility_uncertain_pixels": int((local & ~visibility_resolved).sum()),
                     "lookup_interval_conflicts": int(lookup_conflicts.sum()),
                     "color_availability": availability})
    fields = ("ownership_conflicts", "resolved_false_local_labels", "resolved_false_sky_labels",
              "resolved_hit_time_conflicts", "resolved_visibility_conflicts", "lookup_interval_conflicts")
    selftest = selftest_report(read_hdr(directory / "dynamic_m7.hdr"))
    return {"diagnostic_pixels": int(mask.sum()), "world_normal_conflicts": normal_conflicts,
            "outgoing_interval_conflicts": outgoing_conflicts,
            "readback_geometry_uncertain_pixels": int((reference == 2).sum()),
            "rows": rows, "gpu_selftest": selftest,
            "pass": selftest["pass"] and not (normal_conflicts or outgoing_conflicts or any(row[key] for row in rows for key in fields)),
            "limits": "Fixed exact-solid fixture, world position/outgoing/hit/lookup comparisons enclose one neighboring half value in each direction. Quantization-ambiguous reference cases are reported separately. No reflected RGB, sampling, filtering, roughness or admission certification."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--split", action="store_true")
    parser.add_argument("--preset", type=Path)
    parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[1] / "build/probe-coverage/gX_build")
    args = parser.parse_args()
    if args.capture:
        if args.preset is None:
            parser.error("--capture requires the explicit framed --preset")
        capture_cases(args.directory, args.preset, args.binary, args.split)
    report = diagnose(args.directory)
    print(json.dumps(report, indent=2))
    return int(not report["pass"])


if __name__ == "__main__":
    raise SystemExit(main())
