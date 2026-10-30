#!/usr/bin/env python3
#ai: isolated sky/local resource and explicit-unavailable diagnostics; no renderer admission.
"""Validate diagnostic resource selection without certifying sampled local RGB."""
import argparse
import json
from pathlib import Path

import numpy as np
from probe_proxy_gpu_check import diagnose, read_hdr


def expected_route(geometry, visibility, distance, sky_ready=True):
    source = np.zeros(len(geometry), dtype=int)
    reason = np.full(len(geometry), 3, dtype=int)
    sky = geometry == 0
    local = geometry == 1
    visible = local & (visibility == 1)
    source[sky] = 1 if sky_ready else 0
    reason[sky] = 0 if sky_ready else 1
    source[visible & (distance > 0)] = 2
    reason[visible & (distance > 0)] = 0
    reason[visible & (distance == 0)] = 4
    reason[local & ~visible] = 2
    return source, reason


def diagnose_split(directory):
    directory = Path(directory)
    base = diagnose(directory)
    mask = read_hdr(directory / "dynamic_m1.hdr")[:, :, 3] == 7
    rows = []
    baseline_sky = None
    for label in ("dynamic", "probe0", "probe1", "probe2", "probe3"):
        images = {mode: read_hdr(directory / f"{label}_m{mode}.hdr") for mode in (3, 8, 9, 10, 11, 12)}
        if any(im.shape[:2] != mask.shape or not np.array_equal(im[:, :, 3] == 7, mask) for im in images.values()):
            raise ValueError("unaligned split diagnostic")
        geometry, visibility, _ = images[3][mask, :3].T
        distance = images[9][mask, 0]
        source, reason = expected_route(geometry, visibility, distance)
        routes = images[12][mask, :3]
        sky = images[10][mask, :3]
        routed = images[11][mask, :3]
        local = images[8][mask, :3]
        expected = np.where((source == 1)[:, None], sky,
                            np.where((source == 2)[:, None], local, 0))
        if baseline_sky is None:
            baseline_sky = sky.copy()
        rows.append({"capture": label, "sky_resource_pixels": int((source == 1).sum()),
                     "local_candidate_pixels": int((source == 2).sum()),
                     "unavailable_hidden_pixels": int((reason == 2).sum()),
                     "unavailable_geometry_pixels": int((reason == 3).sum()),
                     "unavailable_no_response_pixels": int((reason == 4).sum()),
                     "source_code_conflicts": int((routes[:, 0] != source).sum()),
                     "reason_code_conflicts": int((routes[:, 1] != reason).sum()),
                     "geometry_code_conflicts": int((routes[:, 2] != geometry).sum()),
                     "routed_rgb_max_delta": float(np.abs(routed - expected).max()),
                     "sky_cross_capture_max_delta": float(np.abs(sky - baseline_sky).max()),
                     "hidden_routed_rgb_max_abs": float(np.abs(routed[reason == 2]).max()) if (reason == 2).any() else 0})
    return {"base_diagnostic_pass": base["pass"], "readback_geometry_uncertain_pixels": base["readback_geometry_uncertain_pixels"],
            "rows": rows, "pass": base["pass"] and all(not row[key] for row in rows for key in (
                "source_code_conflicts", "reason_code_conflicts", "geometry_code_conflicts", "routed_rgb_max_delta",
                "sky_cross_capture_max_delta", "hidden_routed_rgb_max_abs")),
            "limits": "Diagnostic-only routing, sky LUT and local-only cubemap. Black unavailable is an explicit marker, NOT a production material fallback. Local candidates may have black-clear filtered coverage, not certified RGB or roughness. No promotion of reflection sampling."}


def direction_report(directory, width=1024, height=512):
    directory = Path(directory)
    expected = read_hdr(directory / "dynamic_m13.hdr")
    actual = read_hdr(directory / "dynamic_m14.hdr")
    mask = expected[:, :, 3] == 7
    if not mask.any() or actual.shape != expected.shape or not np.array_equal(actual[:, :, 3] == 7, mask):
        raise ValueError("unaligned sky-direction control")
    a, b = expected[mask, :3].astype(float), actual[mask, :3].astype(float)
    if (np.linalg.norm(b, axis=-1) == 0).any():
        raise ValueError("missing sky-direction resource")
    a /= np.linalg.norm(a, axis=-1)[:, None]
    b /= np.linalg.norm(b, axis=-1)[:, None]
    angles = np.arccos(np.clip((a * b).sum(axis=-1), -1, 1))
    readback_error = np.sqrt(3) * 2 ** -10
    bound = 2 * np.pi / width + np.pi / height + 3 * np.arctan2(readback_error, 1 - readback_error)
    columns = np.broadcast_to(np.arange(mask.shape[1]), mask.shape)[mask]
    rows = [{"case": case, "pixels": int((columns % 8 == case).sum()),
             "max_angle_degrees": float(np.degrees(angles[columns % 8 == case].max()))}
            for case in range(8)]
    return {"rows": rows, "derived_bound_degrees": float(np.degrees(bound)),
            "direction_conflicts": int((angles > bound).sum()), "pass": not (angles > bound).any(),
            "limits": "Cardinal/asymmetric encoded direction correspondence only; bound derives from LUT angular spacing plus three half-value quantization steps, not a reflection-quality threshold."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--direction", action="store_true")
    args = parser.parse_args()
    report = direction_report(args.directory) if args.direction else diagnose_split(args.directory)
    print(json.dumps(report, indent=2))
    return int(not report["pass"])


if __name__ == "__main__":
    raise SystemExit(main())
