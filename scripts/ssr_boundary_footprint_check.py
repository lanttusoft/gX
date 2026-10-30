#!/usr/bin/env python3
#ai: same-face two-level filter replay for the owned SSR boundary fixture, not a visibility certificate.
"""Report cubemap tap support without changing runtime sampling or point-oracle counts."""
import argparse
import json
from pathlib import Path
import struct
import tempfile

import numpy as np

import audit_common as audit
from gfx_buffer_audit import read_dump
import ssr_gap_check as gap
from ssr_normal_precision_check import expected_directions


#ai: explicit GL face conventions; a tie or crossing stencil is not certified by clamping.
def face_uv(direction):
    direction = np.asarray(direction, dtype=float)
    if direction.ndim < 1 or direction.shape[-1] != 3 or not np.isfinite(direction).all():
        raise ValueError("finite three-component directions required")
    magnitude = np.max(np.abs(direction), axis=-1)
    if np.any(magnitude == 0):
        raise ValueError("zero direction has no cubemap face")
    axis = np.argmax(np.abs(direction), axis=-1)
    major = np.take_along_axis(direction, axis[..., None], axis=-1)[..., 0]
    face = axis * 2 + (major < 0)
    x, y, z = np.moveaxis(direction, -1, 0)
    sc = np.choose(face, (-z, z, x, x, x, -x)) / magnitude
    tc = np.choose(face, (-y, -y, z, -z, -y, -y)) / magnitude
    tied = np.sum(np.abs(direction) == magnitude[..., None], axis=-1) > 1
    return face, (sc + 1) / 2, (tc + 1) / 2, tied


def face_directions(face, sc, tc):
    if face not in range(6):
        raise ValueError("GL cubemap face must be in 0..5")
    one = np.ones_like(sc)
    return np.stack(((one, -tc, -sc), (-one, -tc, sc), (sc, one, tc),
                     (sc, -one, -tc), (sc, -tc, one), (-sc, -tc, -one))[face], axis=-1)


def validate_cube(cube):
    cube = np.asarray(cube, dtype=float)
    if cube.ndim != 4 or cube.shape[0] != 6 or cube.shape[1] != cube.shape[2] or cube.shape[-1] != 4:
        raise ValueError("six square RGBA faces required")
    if cube.shape[1] < 4 or cube.shape[1] % 2 or not np.isfinite(cube).all():
        raise ValueError("finite even-sized faces of size >=4 required")
    return cube


#ai: the arithmetic box-reduction hypothesis is explicit; it is not a GL mip-generation proof.
def box_mip(cube):
    cube = validate_cube(cube)
    size = cube.shape[1]
    return cube.reshape(6, size // 2, 2, size // 2, 2, 4).mean(axis=(2, 4))


def bilinear_taps(direction, size):
    face, u, v, tie = face_uv(direction)
    x, y = u * size - .5, v * size - .5
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    taps = [(face, ix, iy, (1 - fx) * (1 - fy)),
            (face, ix + 1, iy, fx * (1 - fy)),
            (face, ix, iy + 1, (1 - fx) * fy),
            (face, ix + 1, iy + 1, fx * fy)]
    seam = tie.copy()
    for _, tx, ty, weight in taps:
        seam |= (weight > 0) & ((tx < 0) | (tx >= size) | (ty < 0) | (ty >= size))
    return taps, seam


def tap_values(cube, taps):
    size = cube.shape[1]
    return [cube[face, np.clip(y, 0, size - 1), np.clip(x, 0, size - 1)]
            for face, x, y, _ in taps]


#ai: support uses any red-bearing base texel, not coarse dominant-color classification.
def replay(cube, direction, lod):
    cube = validate_cube(cube)
    lod = np.broadcast_to(np.asarray(lod, dtype=float), np.asarray(direction).shape[:-1])
    if not np.isfinite(lod).all() or np.any((lod < 0) | (lod >= 1)):
        raise ValueError("this fixed replay supports fractional LOD in [0,1) only")
    size = cube.shape[1]
    coarse = box_mip(cube)
    low_taps, low_seam = bilinear_taps(direction, size)
    high_taps, high_seam = bilinear_taps(direction, size // 2)
    color = np.zeros(lod.shape + (4,))
    support = np.zeros(lod.shape, dtype=bool)
    for level, sampled, taps, scale, level_weight in (
        (0, cube, low_taps, 1, 1 - lod), (1, coarse, high_taps, 2, lod)):
        values = tap_values(sampled, taps)
        for (face, x, y, weight), value in zip(taps, values):
            combined = weight * level_weight
            color += value * combined[..., None]
            for dy in range(scale):
                for dx in range(scale):
                    red = cube[face, np.clip(y * scale + dy, 0, size - 1),
                               np.clip(x * scale + dx, 0, size - 1), 0]
                    support |= (combined > 0) & (red > 1e-4)
    seam = low_seam | ((lod > 0) & high_seam)
    return color, support, seam, low_taps, high_taps


def nearest(cube, direction):
    cube = validate_cube(cube)
    size = cube.shape[1]
    face, u, v, _ = face_uv(direction)
    return cube[face, np.clip((v * size).astype(int), 0, size - 1),
                np.clip((u * size).astype(int), 0, size - 1)]


def read_face(path):
    data = Path(path).read_bytes()
    if len(data) < 20 or data[:8] != b"GXHDR1\0\0":
        raise ValueError("invalid cube HDR header")
    width, height, channels = struct.unpack_from("<III", data, 8)
    if width < 4 or width != height or channels != 4 or len(data) != 20 + width * height * 16:
        raise ValueError("invalid cube HDR extent/payload")
    pixels = np.frombuffer(data, dtype="<f4", offset=20).reshape(height, width, channels)
    if not np.isfinite(pixels).all():
        raise ValueError("nonfinite cube HDR pixels")
    return pixels


def face_controls(cube):
    size = cube.shape[1]
    y, x = np.indices((size, size))
    sc, tc = (x + .5) * 2 / size - 1, (y + .5) * 2 / size - 1
    origin = np.array([0., -3., 5.])
    rows = []
    for face in range(6):
        ray = face_directions(face, sc, tc)
        source = gap.box_time(origin, ray, gap.SOURCE_LO, gap.SOURCE_HI)
        blocker = gap.box_time(origin, ray, gap.BLOCKER_LO, gap.BLOCKER_HI)
        red = np.isfinite(source) & (source < blocker)
        green = np.isfinite(blocker) & (blocker < source)
        rows.append({"face": face,
            "red_class_mismatches": int((red != gap.color_mask(cube[face], 0)).sum()),
            "green_class_mismatches": int((green != gap.color_mask(cube[face], 1)).sum())})
    return rows


#ai: nonempty fixture controls keep missing or broken diagnostic output from passing vacuously.
def diagnostic_checks(reflection, environment, prediction, support, seam, rows):
    ground = prediction["ground"]
    raw_red = gap.color_mask(reflection, 0)[ground]
    false_red = raw_red & ~prediction["red"][ground]
    red_interior = prediction["interior_red"]
    green_interior = prediction["interior_green"]
    return {"native_faces_match_first_hit_oracle": len(rows) == 6 and all(
        row["red_class_mismatches"] == row["green_class_mismatches"] == 0 for row in rows),
        "environment_diagnostic_executed": bool(np.all(environment[ground, 3] > .99)),
        "red_interior_preserved": bool(red_interior.any() and gap.color_mask(reflection, 0)[red_interior].mean() >= .99),
        "green_interior_preserved": bool(green_interior.any() and gap.color_mask(reflection, 1)[green_interior].mean() >= .99),
        "no_unexplained_red_outside_support": not np.any(false_red & ~support & ~seam),
        "boundary_seams_not_silently_certified": not np.any(false_red & seam),
        "deep_occlusion_remains_clear": not np.any(raw_red & green_interior[ground])}


#ai: point-oracle disagreements are retained; finite color support never becomes contribution-level visibility.
def analyze(output):
    output = Path(output)
    cube = np.stack([read_face(output / "reflection" / f"cube.face{face}.bin") for face in range(6)])
    reflection = read_dump(output / "reflection" / f"ssr_{gap.FRAME:06d}.gxrt")[::-1]
    environment = read_dump(output / "environment" / f"ssr_{gap.FRAME:06d}.gxrt")[::-1]
    for image in (reflection, environment):
        if image.shape != (gap.HEIGHT, gap.WIDTH, 4) or not np.isfinite(image).all():
            raise ValueError("invalid native SSR extent/pixels")
    packed = gap.read_hdr(output / "reflection/main.hdr")
    prediction = gap.oracle(True)
    ground = prediction["ground"]
    direction = expected_directions()
    lod = packed[..., 3] * 3
    filtered, support, seam, low, high = replay(cube, direction[ground], lod[ground])
    raw_red = gap.color_mask(reflection, 0)[ground]
    false_red = raw_red & ~prediction["red"][ground]
    model_red = gap.color_mask(filtered, 0)
    env = environment[ground]
    error = np.abs(env[..., :3] - filtered[..., :3])
    interior_false = false_red & ~seam
    rows = face_controls(cube)
    masks = {"point_oracle_disagreement": false_red, "red_tap_support": support,
             "face_seam_uncertain": seam}
    np.savez_compressed(output / "boundary_masks.npz", **masks)
    checks = diagnostic_checks(reflection, environment, prediction, support, seam, rows)
    report = {"checks": checks, "diagnostic_controls_pass": all(checks.values()),
        "cube_size": cube.shape[1], "capture_origin_not_fitted": [0, -3, 5],
        "lod_ground_unique": np.unique(lod[ground]).tolist(), "face_controls": rows,
        "point_oracle_disagreements_unchanged": int(false_red.sum()),
        "false_red_with_support": int((false_red & support & ~seam).sum()),
        "false_red_outside_support": int((false_red & ~support & ~seam).sum()),
        "false_red_seam_uncertain": int((false_red & seam).sum()),
        "false_red_model_class_match": int((interior_false & model_red).sum()),
        "nearest_point_oracle_disagreements": int((gap.color_mask(nearest(cube,
            direction[ground]), 0) & ~prediction["red"][ground]).sum()),
        "boundary_environment_model_max_abs": float(error[interior_false].max()) if interior_false.any() else None,
        "boundary_environment_model_mean_abs": float(error[interior_false].mean()) if interior_false.any() else None,
        "whole_ground_same_face_environment_model_max_abs": float(error[~seam].max()),
        "whole_ground_rgb_model_is_incomplete": True,
        "additional_analytic_sun_is_not_in_the_filter_model": True,
        "box_mip_is_explicit_hypothesis_not_GL_spec_certificate": True,
        "visibility_certified": False, "runtime_change": False, "second_camera": False}
    ledger = []
    screen_y, screen_x = np.where(ground)
    raw_ground = reflection[ground]
    coarse = box_mip(cube)
    low_values, high_values = tap_values(cube, low), tap_values(coarse, high)
    level_lod = lod[ground]
    for index in np.flatnonzero(false_red):
        taps = []
        for level, values, records, level_weight in ((0, low_values, low, 1 - level_lod),
                                                    (1, high_values, high, level_lod)):
            for (face, x, y, weight), value in zip(records, values):
                combined = float(weight[index] * level_weight[index])
                if combined > 0:
                    taps.append({"level": level, "face": int(face[index]),
                        "xy": [int(x[index]), int(y[index])], "weight": combined,
                        "rgba": value[index].tolist()})
        ledger.append({"xy": [int(screen_x[index]), int(screen_y[index])],
            "reflection": raw_ground[index].tolist(), "environment": env[index].tolist(),
            "model": filtered[index].tolist(), "support": bool(support[index]),
            "seam_uncertain": bool(seam[index]), "taps": taps})
    (output / "boundary_pixel_ledger.json").write_text(json.dumps(ledger, indent=2) + "\n")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def capture(output, binary):
    output = Path(output).resolve()
    if output == audit.ROOT or audit.ROOT in output.parents:
        raise ValueError("owned diagnostic artifacts must stay outside the repository")
    if Path("/tmp") not in output.parents or len(str(output / "environment/cube")) > 450:
        raise ValueError("cube HDR hook requires a /tmp/ output prefix of at most 450 characters")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError("output must be empty")
    before = gap.protected_hashes()
    binary_hash = gap.file_hash(binary)
    sources = ("resources/shader/gfx_ssr.frag", "src/renderer/gfx/draw.c",
               "src/renderer/gfx/load.c", "src/renderer/cubemap.c")
    source_hashes = {name: gap.file_hash(audit.ROOT / name) for name in sources}
    gap.write_glb(output / "floor.glb", [[-5, 0, -5], [-5, 0, 5], [5, 0, 5],
        [-5, 0, -5], [5, 0, 5], [5, 0, -5]], [[0, 1, 0]] * 6, [0, 0, 0])
    gap.write_glb(output / "emitter.glb", *gap.box_mesh(gap.SOURCE_LO, gap.SOURCE_HI), [1, 0, 0])
    gap.write_glb(output / "occluder.glb", *gap.box_mesh(gap.BLOCKER_LO, gap.BLOCKER_HI), [0, 1, 0])
    print(f"SSR_BOUNDARY output={output}", flush=True)
    (output / "contract.json").write_text(json.dumps({"camera": gap.CAMERA,
        "size": [gap.WIDTH, gap.HEIGHT], "frame": gap.FRAME,
        "capture_origin": [0, -3, 5], "mip_hypothesis": "bilinear L0 + box-reduced bilinear L1",
        "source_bounds": [gap.SOURCE_LO.tolist(), gap.SOURCE_HI.tolist()],
        "occluder_bounds": [gap.BLOCKER_LO.tolist(), gap.BLOCKER_HI.tolist()],
        "color_test": "channel>1e-4 and channel>3*other_channels",
        "point_oracle_thresholds_unchanged": True, "visibility_certificate": False}, indent=2) + "\n")
    try:
        for label, debug in (("reflection", 0), ("environment", 17)):
            case = output / label
            case.mkdir()
            preset = gap.make_preset(output, "ssr_env", True)
            preset["state"]["ssrDebugMode"] = debug
            path = case / "input.json"
            path.write_text(json.dumps(preset, indent=2) + "\n")
            hooks = {"GX_GFX_PRESET_LOAD": str(path), "GX_FIXED_DT": "0.0166666667",
                "GX_RT_VIEW": "ssr", "GX_RT_DUMP_DIR": str(case), "GX_RT_DUMP_FRAME": gap.FRAME,
                "GX_RT_DUMP_COUNT": "1", "GX_T_HDRDUMP": "1", "GX_HDRDUMP_FRAME": gap.FRAME,
                "GX_HDRDUMP_PATH": str(case / "main.hdr"), "GX_T_SSRENV": "1", "GX_PROBE_GRID": "0",
                "GX_PROBE_BOX_CORRECT": "0", "GX_LAYOUT_NORESEAT": "1", "GX_VALIDATE": "1",
                "GX_CUBEMAP_HDR_PREFIX": str(case / "cube"), "GX_CUBEMAP_HDR_CAPTURE": "30"}
            result = audit.run_scene("--gfx", audit.scene_env(hooks, clean=True), binary=binary,
                width=gap.WIDTH, height=gap.HEIGHT, fps=None, frames=gap.FRAME + 2,
                extra_args=("--gfx-editor", "--seed", "424242"), timeout=180, check=False)
            (case / "renderer.log").write_text(audit.strip_ansi(result.stdout))
            if result.returncode:
                raise RuntimeError(f"boundary capture failed: {case}")
        report = analyze(output)
        a = np.stack([read_face(output / "reflection" / f"cube.face{f}.bin") for f in range(6)])
        b = np.stack([read_face(output / "environment" / f"cube.face{f}.bin") for f in range(6)])
        report["native_faces_equal_between_runs"] = bool(np.array_equal(a, b))
    finally:
        unchanged = gap.protected_hashes() == before
        (output / "protected_settings.json").write_text(json.dumps({"unchanged": unchanged,
            "before": before}, indent=2) + "\n")
    report.update(protected_settings_unchanged=unchanged, binary_sha256=binary_hash,
                  binary_stable=gap.file_hash(binary) == binary_hash,
                  source_sha256=source_hashes, sources_stable=source_hashes == {
                      name: gap.file_hash(audit.ROOT / name) for name in sources})
    passed = (report["diagnostic_controls_pass"] and report["native_faces_equal_between_runs"]
              and unchanged and report["binary_stable"] and report["sources_stable"])
    report["diagnostic_controls_pass"] = passed
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"SSR_BOUNDARY controls={'PASS' if passed else 'FAIL'} point_disagreements={report['point_oracle_disagreements_unchanged']} report={output / 'report.json'}", flush=True)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=audit.BIN)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output
    if output is None:
        parent = Path("/tmp/opencode/ssr_boundary_footprint_check")
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="run_", dir=parent))
    return 0 if capture(output, args.binary.resolve()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
