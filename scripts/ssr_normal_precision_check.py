#!/usr/bin/env python3
#ai: owned optional-normal precision/occlusion regression with explicit before/after disabled-path identity.
"""Check native normal storage, reflected rays and the fixed SSR occluder."""
import argparse
import json
from pathlib import Path
import tempfile

import numpy as np

import audit_common as audit
from gfx_buffer_audit import read_dump
import ssr_gap_check as gap


def expected_directions():
    _, rays, _ = gap.camera_rays()
    rays[..., 1] *= -1
    return rays / np.linalg.norm(rays, axis=-1)[..., None]


#ai: native float readback is required; the legacy RGBA8 dump cannot certify target precision.
def precision_metrics(normals, encoded_directions, reflection, prediction):
    ground = prediction["ground"]
    direction_error = np.abs(encoded_directions[..., :3] * 2 - 1 - expected_directions())
    desired = np.array([.5, 1, .5, 1])
    normal_error = np.abs(normals[ground] - desired)
    metrics = gap.measure(reflection, prediction)
    metrics.update(native_normal_max_abs_error=float(normal_error.max()),
                   encoded_ray_max_abs_error=float(direction_error[ground].max()))
    checks = {"flat_normal_components_preserved": bool(normal_error.max() <= 1e-6),
              "ray_encoding_allowance": bool(direction_error[ground].max() <= .0015),
              "no_red_inside_occluded_interior": metrics["red_on_occluded_interior_pixels"] == 0,
              "unoccluded_red_interior_preserved": metrics["red_interior_recall"] >= .99}
    return metrics, checks


def capture_case(directory, binary, enabled, view, debug):
    directory.mkdir()
    preset = gap.make_preset(directory.parent.parent, "ssr_env", True)
    preset["ssr"]["normalBufferEnabled"] = enabled
    preset["state"]["ssrDebugMode"] = debug
    path = directory / "input.json"
    path.write_text(json.dumps(preset, indent=2) + "\n")
    hooks = {"GX_GFX_PRESET_LOAD": str(path), "GX_FIXED_DT": "0.0166666667",
             "GX_RT_VIEW": view, "GX_RT_DUMP_DIR": str(directory),
             "GX_RT_DUMP_FRAME": gap.FRAME, "GX_RT_DUMP_COUNT": "1",
             "GX_T_HDRDUMP": "1", "GX_HDRDUMP_FRAME": gap.FRAME,
             "GX_HDRDUMP_PATH": str(directory / "main.hdr"),
             "GX_T_SSRENV": "1", "GX_PROBE_GRID": "0", "GX_PROBE_BOX_CORRECT": "0",
             "GX_LAYOUT_NORESEAT": "1", "GX_VALIDATE": "1"}
    result = audit.run_scene("--gfx", audit.scene_env(hooks, clean=True),
        binary=binary, width=gap.WIDTH, height=gap.HEIGHT, fps=None, frames=gap.FRAME + 2,
        extra_args=("--gfx-editor", "--seed", "424242"), timeout=180, check=False)
    (directory / "renderer.log").write_text(audit.strip_ansi(result.stdout))
    if result.returncode:
        raise RuntimeError(f"precision capture exited {result.returncode}: {directory}")
    raw = read_dump(directory / f"{view}_{gap.FRAME:06d}.gxrt")[::-1]
    if raw.shape != (gap.HEIGHT, gap.WIDTH, 4) or not np.isfinite(raw).all():
        raise ValueError("matching finite native RGBA target required")
    return raw, gap.read_hdr(directory / "main.hdr")


#ai: an explicit old binary may retain the counterexample; no source rewriting or implicit promotion.
def capture(output, binary, before_binary=None):
    output = Path(output).resolve()
    if output == audit.ROOT or audit.ROOT in output.parents:
        raise ValueError("precision fixture output must stay outside the repository")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError("precision fixture output must be empty")
    protected = gap.protected_hashes()
    gap.write_glb(output / "floor.glb", [[-5, 0, -5], [-5, 0, 5], [5, 0, 5],
                  [-5, 0, -5], [5, 0, 5], [5, 0, -5]], [[0, 1, 0]] * 6, [0, 0, 0])
    gap.write_glb(output / "emitter.glb", *gap.box_mesh(gap.SOURCE_LO, gap.SOURCE_HI), [1, 0, 0])
    gap.write_glb(output / "occluder.glb", *gap.box_mesh(gap.BLOCKER_LO, gap.BLOCKER_HI), [0, 1, 0])
    prediction = gap.oracle(True)
    results, disabled = {}, {}
    binaries = {"after": Path(binary).resolve()}
    if before_binary:
        binaries = {"before": Path(before_binary).resolve(), **binaries}
    hashes = {name: gap.file_hash(path) for name, path in binaries.items()}
    print(f"SSR_NORMAL_PRECISION output={output}", flush=True)
    try:
        for name, selected in binaries.items():
            profile = output / name
            profile.mkdir()
            images = {}
            for label, enabled, view, debug in (("normals", True, "normal", 0),
                ("direction", True, "ssr", 2), ("reflection", True, "ssr", 0),
                ("disabled", False, "ssr", 0)):
                images[label], hdr = capture_case(profile / label, selected, enabled, view, debug)
                if label == "disabled":
                    disabled[name] = images[label], hdr
            metrics, checks = precision_metrics(images["normals"], images["direction"],
                                                images["reflection"], prediction)
            results[name] = {"metrics": metrics, "checks": checks}
            print(f"SSR_NORMAL_PRECISION {name} {json.dumps(metrics)}", flush=True)
    finally:
        unchanged = gap.protected_hashes() == protected
        (output / "protected_settings.json").write_text(json.dumps({"unchanged": unchanged,
            "before": protected}, indent=2) + "\n")
    identity = (all(np.array_equal(a, b) for a, b in zip(disabled["before"], disabled["after"]))
                if "before" in disabled else None)
    stable = hashes == {name: gap.file_hash(path) for name, path in binaries.items()}
    passed = all(results["after"]["checks"].values()) and unchanged and stable and identity is not False
    report = {"profiles": results, "pass": passed, "disabled_ssr_and_material_bit_identical": identity,
              "protected_settings_unchanged": unchanged, "binaries_stable": stable,
              "binary_sha256": hashes, "ray_encoding_allowance": .0015,
              "native_flat_normal_allowance": 1e-6, "second_camera": False,
              "scope": "optional shading-normal precision; arbitrary cube visibility and border filtering not certified"}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"SSR_NORMAL_PRECISION {'PASS' if passed else 'FAIL'} report={output / 'report.json'}", flush=True)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=audit.BIN)
    parser.add_argument("--before-binary", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output
    if output is None:
        parent = Path("/tmp/opencode/ssr_normal_precision_check")
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="run_", dir=parent))
    return 0 if capture(output, args.binary, args.before_binary) else 1


if __name__ == "__main__":
    raise SystemExit(main())
