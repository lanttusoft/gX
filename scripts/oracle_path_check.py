#!/usr/bin/env python3
#opencode: Paper 1 M1 — deterministic opt-in transport-oracle gate.
#   The reflectrow scene is a fixed roughness/metalness ladder. The gate
#   checks that the path hook changes the diffuse environment plus direct-light
#   estimator, that K=2 reduces the residual against K=4, and that repeated
#   captures remain stable. It is deliberately separate from make check.

import os
import sys

import audit_common as audit


OUT = "/tmp/opencode/oracle_path"
TIMEOUT = 120


def capture(name, path_on, rays):
    target = os.path.join(OUT, name)
    os.makedirs(target, exist_ok=True)
    environment = audit.scene_env({
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SHADOWS": "0",
        "GX_T_POINT_SHADOWS": "0",
        "GX_T_SSR": "0",
        "GX_T_REFL": "0",
        "GX_T_TAA": "0",
        "GX_T_FOG": "0",
        "GX_T_GODRAY": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_COOKIES": "0",
        "GX_T_DOF": "0",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_FXAA": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_ENVAMBIENT": "0",
        "GX_T_INSTANCING": "0",
        "GX_T_FRUSTUM": "0",
        "GX_T_ORACLE": "0",
        "GX_T_ORACLEPATH": "1" if path_on else "0",
        "GX_T_ORACLEEST": "0",
        "GX_T_ORACLESEED": "1" if path_on else "0",
        "GX_T_ORACLERAYS": str(rays),
    }, clean=True)
    audit.run_scene("--reflectrow", environment, width=960, height=640,
                    fps=None, screenshot_dir=target, shot_frame=60,
                    screenshot_after=True, timeout=TIMEOUT, check=True,
                    extra_args=("--preset", "2", "--debug-view", "0"))
    shot = audit.newest_shot(target)
    if shot is None:
        raise RuntimeError("no screenshot produced for " + name)
    return audit.load_rgb(shot)


def mae(a, b, roi):
    x0, x1, y0, y1 = roi
    return audit.mean_abs_diff(a[y0:y1, x0:x1], b[y0:y1, x0:x1])


def main():
    global OUT
    if len(sys.argv) > 1:
        OUT = os.path.abspath(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)

    with audit.settings_preserved():
        base = capture("base", False, 1)
        path1 = capture("path1", True, 1)
        path2 = capture("path2", True, 2)
        path4 = capture("path4", True, 4)
        repeat = capture("repeat", True, 1)
    if len({image.shape for image in (base, path1, path2, path4, repeat)}) != 1:
        raise RuntimeError("capture dimensions are not stable")

    roi = (0, base.shape[1], 80, base.shape[0] - 20)
    activation = mae(base, path1, roi)
    one_vs_four = mae(path1, path4, roi)
    two_vs_four = mae(path2, path4, roi)
    repeat_delta = mae(path1, repeat, roi)
    print("oracle-path M1")
    print("  activation MAE      %.4f" % activation)
    print("  K=1 vs K=4 MAE      %.4f" % one_vs_four)
    print("  K=2 vs K=4 MAE      %.4f" % two_vs_four)
    print("  repeat K=1 MAE      %.6f" % repeat_delta)

    failures = []
    if activation <= 0.05:
        failures.append("path hook did not change the capture")
    if one_vs_four <= 0.25:
        failures.append("K=1 has no measurable estimator variance")
    if two_vs_four >= one_vs_four * 0.85:
        failures.append("K=2 does not reduce residual against K=4")
    if repeat_delta > 0.01:
        failures.append("repeat capture is not deterministic")
    if failures:
        for failure in failures:
            print("FAIL: " + failure, file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
