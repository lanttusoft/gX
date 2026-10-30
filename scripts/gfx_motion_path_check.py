#!/usr/bin/env python3
#opencode: deterministic model motion-path gate through the authored preset and
#   the GX_MOTION_TRACE renderer hook.

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
MOTION_RE = re.compile(
    r"GFX_MOTION: kind=(model|light) "
    r"pos=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
    r"elapsed=([-0-9.]+) paused=(\d)"
)
#opencode: viewport handle trace — screen (window) and world positions per handle.
PATH_HANDLE_RE = re.compile(
    r"GFX_PATHHANDLE: kind=(model|light) index=(\d+) side=(out|in) "
    r"independent=(\d) sx=([-0-9.]+) sy=([-0-9.]+) rx=([-0-9.]+) ry=([-0-9.]+) "
    r"wx=([-0-9.]+) wy=([-0-9.]+) wz=([-0-9.]+)"
)
CENTER = (1.0, 0.5, -0.5)
RADIUS = 2.0
WAYPOINTS = [(0.0, 1.0, 0.0), (4.0, 1.0, 0.0), (4.0, 1.0, 4.0), (0.0, 1.0, 4.0)]


def polyline_distance(point, waypoints):
    px, py, pz = point
    best = float("inf")
    for (ax, ay, az), (bx, by, bz) in zip(waypoints, waypoints[1:]):
        dx, dy, dz = bx - ax, by - ay, bz - az
        length_sq = dx * dx + dy * dy + dz * dz
        if length_sq == 0.0:
            t = 0.0
        else:
            t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy
                + (pz - az) * dz) / length_sq))
        cx, cy, cz = ax + dx * t, ay + dy * t, az + dz * t
        best = min(best, ((px - cx) ** 2 + (py - cy) ** 2 + (pz - cz) ** 2) ** 0.5)
    return best


#opencode: scene demo lights trace before the preset applies; keep the samples
#   from the first on-path one so only the authored path is asserted.
def trim_to_path(samples, waypoints, tolerance=0.02):
    for index, sample in enumerate(samples):
        if polyline_distance((sample[0], sample[1], sample[2]), waypoints) <= tolerance:
            return samples[index:]
    return []


#opencode: settings safety and window isolation live in audit_common.
with_settings_preserved = audit.with_settings_preserved


def run_editor_log(preset_path, frames, save=None, path_trace=False,
                   ai_input=None):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_GFX_PRESET_LOAD": str(preset_path),
        "GX_MOTION_TRACE": "1",
    })
    environment.pop("GX_AI_INPUT", None)
    if save is not None:
        environment["GX_GFX_PRESET_SAVE"] = str(save)
    if path_trace:
        environment["GX_GFX_PATH_TRACE"] = "1"
    if ai_input:
        environment["GX_AI_INPUT"] = ai_input
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor"],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{result.stdout[-3000:]}")
    return audit.strip_ansi(result.stdout)


def run_editor(preset_path, frames, kind="model", save=None):
    log = run_editor_log(preset_path, frames, save=save)
    return [
        tuple(map(float, match[1:5])) + (int(match[5]),)
        for match in MOTION_RE.findall(log)
        if match[0] == kind
    ]


def path_handles(log_text, index=None, side=None):
    rows = PATH_HANDLE_RE.findall(log_text)
    if index is not None:
        rows = [row for row in rows if int(row[1]) == index]
    if side is not None:
        rows = [row for row in rows if row[2] == side]
    return rows


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_MOTION_PATH_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_motion_", dir="/tmp/opencode") as temp:
            temp = Path(temp)
            probe = json.loads(PRESET.read_text())
            params = probe["models"][0]["params"]
            params.update({
                "orbitEnabled": True,
                "orbitPaused": False,
                "orbitLoop": True,
                "orbitPlane": 1,
                "orbitCenter": list(CENTER),
                "orbitRadius": RADIUS,
                "orbitSpeed": 60.0,
                "orbitPhase": 0.0,
                "orbitElapsed": 0.0,
            })
            loop_preset = temp / "loop.json"
            loop_preset.write_text(json.dumps(probe))

            def exercise():
                samples = run_editor(loop_preset, frames=180)
                if len(samples) < 10:
                    raise RuntimeError(f"too few motion samples: {len(samples)}")
                for (x, y, z, elapsed, paused) in samples:
                    if abs(y - CENTER[1]) > 0.01:
                        raise RuntimeError(f"model left the XZ plane: {samples[:3]}")
                    if abs((x - CENTER[0]) ** 2 + (z - CENTER[2]) ** 2
                            - RADIUS ** 2) > 0.05:
                        raise RuntimeError(f"model left the circle: {samples[:3]}")
                if samples[0][3] >= samples[-1][3]:
                    raise RuntimeError("orbit elapsed did not advance")
                first, last = samples[0], samples[-1]
                travel = max(
                    abs(a[0] - b[0]) + abs(a[2] - b[2])
                    for a in samples for b in (first, last)
                )
                if travel < 0.5:
                    raise RuntimeError(f"model did not travel its path: {samples[:3]}")

                #opencode: one-shot playback pauses after one revolution.
                params.update({
                    "orbitLoop": False,
                    "orbitPaused": False,
                    "orbitSpeed": 360.0,
                    "orbitElapsed": 0.0,
                })
                once_preset = temp / "once.json"
                once_preset.write_text(json.dumps(probe))
                once = run_editor(once_preset, frames=150)
                if len(once) < 10 or once[-1][4] != 1:
                    raise RuntimeError(f"one-shot path did not pause: {once[-3:]}")
                tail = once[-3:]
                for sample in tail[1:]:
                    if abs(tail[0][0] - sample[0]) > 0.001 \
                            or abs(tail[0][2] - sample[2]) > 0.001:
                        raise RuntimeError(f"paused one-shot path kept moving: {tail}")

                #opencode: waypoint paths follow the authored polyline.
                flat_waypoints = [value for point in WAYPOINTS for value in point]
                params.update({
                    "pathMode": 1,
                    "pathWaypointCount": len(WAYPOINTS),
                    "pathWaypoints": flat_waypoints
                        + [0.0] * (3 * 8 - len(flat_waypoints)),
                    "pathPingPong": False,
                    "orbitLoop": True,
                    "orbitPaused": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                path_preset = temp / "waypoints.json"
                path_preset.write_text(json.dumps(probe))
                path_samples = run_editor(path_preset, frames=180)
                if len(path_samples) < 10:
                    raise RuntimeError(f"too few waypoint samples: {len(path_samples)}")
                for (x, y, z, elapsed, paused) in path_samples:
                    if polyline_distance((x, y, z), WAYPOINTS) > 0.02:
                        raise RuntimeError(
                            f"model left the waypoint polyline: {path_samples[:3]}")
                if path_samples[0][3] >= path_samples[-1][3]:
                    raise RuntimeError("waypoint elapsed did not advance")
                if abs(path_samples[0][0] - path_samples[-1][0]) \
                        + abs(path_samples[0][2] - path_samples[-1][2]) < 0.5:
                    raise RuntimeError(
                        f"model did not travel the waypoint path: {path_samples[:3]}")

                #opencode: one-shot waypoint playback pauses on the final point.
                params.update({
                    "orbitLoop": False,
                    "orbitPaused": False,
                    "orbitSpeed": 8.0,
                    "orbitElapsed": 0.0,
                })
                final_preset = temp / "waypoints_once.json"
                final_preset.write_text(json.dumps(probe))
                final = run_editor(final_preset, frames=180)
                if len(final) < 10 or final[-1][4] != 1:
                    raise RuntimeError(f"one-shot waypoints did not pause: {final[-3:]}")
                end = WAYPOINTS[-1]
                if abs(final[-1][0] - end[0]) > 0.01 \
                        or abs(final[-1][1] - end[1]) > 0.01 \
                        or abs(final[-1][2] - end[2]) > 0.01:
                    raise RuntimeError(
                        f"one-shot waypoints paused at {final[-1][:3]}, expected {end}")

                #opencode: smoothing follows Catmull-Rom instead of the polyline.
                params.update({
                    "pathSmooth": True,
                    "orbitLoop": True,
                    "orbitPaused": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                smooth_preset = temp / "waypoints_smooth.json"
                smooth_preset.write_text(json.dumps(probe))
                smooth = run_editor(smooth_preset, frames=180)
                if len(smooth) < 100:
                    raise RuntimeError(f"too few smooth samples: {len(smooth)}")
                margin = 2.0
                xs = [sample[0] for sample in smooth]
                ys = [sample[1] for sample in smooth]
                zs = [sample[2] for sample in smooth]
                if min(xs) < min(point[0] for point in WAYPOINTS) - margin \
                        or max(xs) > max(point[0] for point in WAYPOINTS) + margin \
                        or min(zs) < min(point[2] for point in WAYPOINTS) - margin \
                        or max(zs) > max(point[2] for point in WAYPOINTS) + margin \
                        or abs(min(ys) - WAYPOINTS[0][1]) > margin \
                        or abs(max(ys) - WAYPOINTS[0][1]) > margin:
                    raise RuntimeError(f"smooth path left the waypoint bounds: {smooth[:3]}")
                deviation = max(
                    polyline_distance((sample[0], sample[1], sample[2]), WAYPOINTS)
                    for sample in smooth)
                if deviation < 0.1:
                    raise RuntimeError(
                        f"smooth path did not leave the polyline: {deviation:.3f}")
                if smooth[0][3] >= smooth[-1][3]:
                    raise RuntimeError("smooth elapsed did not advance")

                #opencode: Bezier handles bulge the path away from the chords;
                #   zero handles must reproduce the straight polyline.
                line = [(0.0, 1.0, 0.0), (4.0, 1.0, 0.0), (8.0, 1.0, 0.0)]
                line_flat = [value for point in line for value in point]
                handles = [0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0]
                params.update({
                    "pathMode": 1,
                    "pathWaypointCount": len(line),
                    "pathWaypoints": line_flat + [0.0] * (3 * 8 - len(line_flat)),
                    "pathPingPong": False,
                    "pathSmooth": False,
                    "pathBezier": True,
                    "pathTangents": handles + [0.0] * (3 * 8 - len(handles)),
                    "orbitLoop": True,
                    "orbitPaused": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                bezier_preset = temp / "bezier.json"
                bezier_preset.write_text(json.dumps(probe))
                bezier = run_editor(bezier_preset, frames=300)
                if len(bezier) < 100:
                    raise RuntimeError(f"too few bezier samples: {len(bezier)}")
                bezier_z = [sample[2] for sample in bezier]
                bezier_x = [sample[0] for sample in bezier]
                if min(bezier_z) > -0.5 or max(bezier_z) < 0.5:
                    raise RuntimeError(
                        f"bezier handles did not bulge the path: z {min(bezier_z):.3f}..{max(bezier_z):.3f}")
                if min(bezier_x) > 0.2 or max(bezier_x) < 7.8:
                    raise RuntimeError(
                        f"bezier path did not span the waypoints: x {min(bezier_x):.3f}..{max(bezier_x):.3f}")
                for sample in bezier:
                    if abs(sample[1] - 1.0) > 0.01:
                        raise RuntimeError(f"bezier path left the authored plane: {bezier[:3]}")
                if bezier[0][3] >= bezier[-1][3]:
                    raise RuntimeError("bezier elapsed did not advance")

                params.update({"pathTangents": [0.0] * (3 * 8)})
                zero_preset = temp / "bezier_zero.json"
                zero_preset.write_text(json.dumps(probe))
                zero = run_editor(zero_preset, frames=180)
                if len(zero) < 100:
                    raise RuntimeError(f"too few zero-handle samples: {len(zero)}")
                for sample in zero:
                    if abs(sample[2]) > 0.02:
                        raise RuntimeError(
                            f"zero handles left the straight line: {zero[:3]}")

                #opencode: independent in/out handles — zero in-handles
                #   straighten the incoming side while the out side still
                #   bulges; mirroring the out side reproduces the default
                #   S-curve, and the new keys round-trip through the preset.
                params.update({
                    "pathHandlesIndependent": True,
                    "pathTangents": handles + [0.0] * (3 * 8 - len(handles)),
                    "pathTangentsIn": [0.0] * (3 * 8),
                })
                independent_preset = temp / "bezier_independent.json"
                independent_preset.write_text(json.dumps(probe))
                independent = run_editor(independent_preset, frames=300)
                if len(independent) < 100:
                    raise RuntimeError(
                        f"too few independent-handle samples: {len(independent)}")
                indep_z = [sample[2] for sample in independent]
                if min(indep_z) < -0.05:
                    raise RuntimeError(
                        f"independent zero in-handles did not straighten the "
                        f"incoming side: z {min(indep_z):.3f}..{max(indep_z):.3f}")
                if max(indep_z) < 0.5:
                    raise RuntimeError(
                        f"independent out-handles stopped bulging: z "
                        f"{min(indep_z):.3f}..{max(indep_z):.3f}")
                params.update({
                    "pathTangentsIn": handles + [0.0] * (3 * 8 - len(handles)),
                })
                mirror_preset = temp / "bezier_independent_mirror.json"
                mirror_preset.write_text(json.dumps(probe))
                mirrored = run_editor(mirror_preset, frames=300)
                if len(mirrored) < 100:
                    raise RuntimeError(
                        f"too few mirrored-independent samples: {len(mirrored)}")
                mirrored_z = [sample[2] for sample in mirrored]
                if min(mirrored_z) > -0.5 or max(mirrored_z) < 0.5:
                    raise RuntimeError(
                        f"mirrored independent handles lost the S-curve: z "
                        f"{min(mirrored_z):.3f}..{max(mirrored_z):.3f}")
                saved_path = temp / "motion_saved.json"
                run_editor(independent_preset, frames=30, save=saved_path)
                saved_params = json.loads(
                    saved_path.read_text())["models"][0]["params"]
                saved_in = saved_params.get("pathTangentsIn", [])
                if (not saved_params.get("pathHandlesIndependent")
                        or len(saved_in) < 24
                        or any(abs(float(value)) > 1e-6 for value in saved_in)):
                    raise RuntimeError(
                        f"independent handle keys did not round-trip: "
                        f"{saved_params.get('pathHandlesIndependent')}/"
                        f"{saved_in[:6]}")

                #opencode: viewport handle dragging — grab the middle out
                #   handle, drag it, and confirm the tangent, the mirror rule,
                #   the evaluated curve, in-side independence, and undo. The
                #   path sits left of the inspector so both handles project into
                #   the visible viewport.
                handle_line = [(-2.0, 1.0, 0.0), (0.0, 1.0, 0.0),
                               (2.0, 1.0, 0.0)]
                handle_flat = [value for point in handle_line
                               for value in point]
                handle_tangents = ([0.0, 0.0, 0.0, 0.0, 0.0, 1.5]
                                   + [0.0] * (3 * 8 - 6))
                params.update({
                    "pathMode": 1,
                    "pathWaypointCount": len(handle_line),
                    "pathWaypoints": handle_flat
                        + [0.0] * (3 * 8 - len(handle_flat)),
                    "pathPingPong": False,
                    "pathSmooth": False,
                    "pathBezier": True,
                    "pathTangents": handle_tangents,
                    "pathTangentsIn": [0.0] * (3 * 8),
                    "pathHandlesIndependent": False,
                    "orbitLoop": True,
                    "orbitPaused": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                handle_preset = temp / "handle_drag.json"
                handle_preset.write_text(json.dumps(probe))
                baseline_log = run_editor_log(handle_preset, frames=180,
                                              path_trace=True)
                baseline_out = path_handles(baseline_log, index=1, side="out")
                baseline_in = path_handles(baseline_log, index=1, side="in")
                if not baseline_out or not baseline_in:
                    raise RuntimeError("viewport handle trace missing")
                if (abs(float(baseline_out[-1][8])) > 1e-6
                        or abs(float(baseline_out[-1][10]) - 1.5) > 1e-6):
                    raise RuntimeError(
                        f"authored out handle wrong: {baseline_out[-1][8:11]}")
                out_x = float(baseline_out[-1][4])
                out_y = float(baseline_out[-1][5])
                drag_input = (
                    f"1.0 move {out_x:.1f} {out_y:.1f} 200;"
                    f"1.2 drag {out_x:.1f} {out_y:.1f} "
                    f"{out_x + 120.0:.1f} {out_y:.1f} 400")
                drag_log = run_editor_log(handle_preset, frames=180,
                                          path_trace=True, ai_input=drag_input)
                if "handle grab" not in drag_log:
                    raise RuntimeError("viewport did not grab the out handle")
                drag_out = path_handles(drag_log, index=1, side="out")
                drag_in = path_handles(drag_log, index=1, side="in")
                moved = [float(drag_out[-1][8 + axis]) for axis in range(3)]
                if abs(moved[0]) + abs(moved[2]) < 0.05:
                    raise RuntimeError(
                        f"handle drag did not move the tangent: {moved}")
                waypoint = handle_line[1]
                for axis in range(3):
                    expected = 2.0 * waypoint[axis] - moved[axis]
                    if abs(float(drag_in[-1][8 + axis]) - expected) > 1e-4:
                        raise RuntimeError(
                            f"mirrored in handle did not follow the out side: "
                            f"{drag_in[-1][8:11]} vs {moved}")
                if MOTION_RE.findall(baseline_log) == MOTION_RE.findall(drag_log):
                    raise RuntimeError(
                        "handle drag did not change the sampled path")

                #opencode: dragging the in side of a mirrored path switches to
                #   independent handles while the out side stays put.
                in_x = float(baseline_in[-1][4])
                in_y = float(baseline_in[-1][5])
                in_input = (
                    f"1.0 move {in_x:.1f} {in_y:.1f} 200;"
                    f"1.2 drag {in_x:.1f} {in_y:.1f} "
                    f"{in_x + 90.0:.1f} {in_y - 60.0:.1f} 400")
                in_log = run_editor_log(handle_preset, frames=180,
                                        path_trace=True, ai_input=in_input)
                in_rows = path_handles(in_log, index=1, side="in")
                in_out = path_handles(in_log, index=1, side="out")
                if int(in_rows[-1][3]) != 1:
                    raise RuntimeError(
                        "dragging the in handle did not enable independence")
                if (abs(float(in_rows[-1][8])) < 1e-4
                        and abs(float(in_rows[-1][9]) - 1.0) < 1e-4
                        and abs(float(in_rows[-1][10]) + 1.5) < 1e-4):
                    raise RuntimeError("in handle did not move")
                if (abs(float(in_out[-1][8])) > 1e-4
                        or abs(float(in_out[-1][10]) - 1.5) > 1e-4):
                    raise RuntimeError(
                        f"out handle changed while dragging the in side: "
                        f"{in_out[-1][8:11]}")

                #opencode: the drag coalesces into one history entry that
                #   Ctrl+Z restores (paths may be driving while authored).
                undo_input = (drag_input
                              + ";3.0 move 700 500 200;"
                                "3.2 drag 700 500 702 500 100 CTRL+Z")
                undo_log = run_editor_log(handle_preset, frames=260,
                                          path_trace=True, ai_input=undo_input)
                if "history undo" not in undo_log:
                    raise RuntimeError("handle drag undo did not fire")
                undo_rows = path_handles(undo_log, index=1, side="out")
                if (abs(float(undo_rows[-1][8])) > 1e-4
                        or abs(float(undo_rows[-1][10]) - 1.5) > 1e-4):
                    raise RuntimeError(
                        f"undo did not restore the tangent: "
                        f"{undo_rows[-1][8:11]}")

                #opencode: point lights share the same waypoint path contract.
                light = probe["lights"][0]
                light.update({
                    "orbitEnabled": True,
                    "orbitPaused": False,
                    "orbitLoop": True,
                    "orbitPlane": 1,
                    "pathMode": 1,
                    "pathWaypointCount": len(WAYPOINTS),
                    "pathWaypoints": flat_waypoints
                        + [0.0] * (3 * 8 - len(flat_waypoints)),
                    "pathPingPong": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                light_preset = temp / "light_waypoints.json"
                light_preset.write_text(json.dumps(probe))
                light_samples = trim_to_path(
                    run_editor(light_preset, frames=180, kind="light"), WAYPOINTS)
                if len(light_samples) < 100:
                    raise RuntimeError(
                        f"too few light waypoint samples: {len(light_samples)}")
                for (x, y, z, elapsed, paused) in light_samples:
                    if polyline_distance((x, y, z), WAYPOINTS) > 0.02:
                        raise RuntimeError(
                            f"light left the waypoint polyline: {light_samples[:3]}")
                if light_samples[0][3] >= light_samples[-1][3]:
                    raise RuntimeError("light waypoint elapsed did not advance")

                #opencode: one-shot light waypoints pause on the final point.
                light.update({
                    "orbitLoop": False,
                    "orbitPaused": False,
                    "orbitSpeed": 8.0,
                    "orbitElapsed": 0.0,
                })
                light_once = temp / "light_waypoints_once.json"
                light_once.write_text(json.dumps(probe))
                final_light = trim_to_path(
                    run_editor(light_once, frames=180, kind="light"), WAYPOINTS)
                if len(final_light) < 100 or final_light[-1][4] != 1:
                    raise RuntimeError(
                        f"one-shot light waypoints did not pause: {final_light[-3:]}")
                if abs(final_light[-1][0] - end[0]) > 0.01 \
                        or abs(final_light[-1][1] - end[1]) > 0.01 \
                        or abs(final_light[-1][2] - end[2]) > 0.01:
                    raise RuntimeError(
                        f"one-shot light paused at {final_light[-1][:3]}, expected {end}")

                #opencode: light Bezier handles share the model evaluator path.
                light.update({
                    "pathMode": 1,
                    "pathWaypointCount": len(line),
                    "pathWaypoints": line_flat + [0.0] * (3 * 8 - len(line_flat)),
                    "pathPingPong": False,
                    "pathSmooth": False,
                    "pathBezier": True,
                    "pathTangents": handles + [0.0] * (3 * 8 - len(handles)),
                    "orbitLoop": True,
                    "orbitPaused": False,
                    "orbitSpeed": 2.0,
                    "orbitElapsed": 0.0,
                })
                light_bezier_preset = temp / "light_bezier.json"
                light_bezier_preset.write_text(json.dumps(probe))
                light_bezier = trim_to_path(
                    run_editor(light_bezier_preset, frames=180, kind="light"),
                    line, tolerance=1.2)
                if len(light_bezier) < 100:
                    raise RuntimeError(
                        f"too few light bezier samples: {len(light_bezier)}")
                light_z = [sample[2] for sample in light_bezier]
                if min(light_z) > -0.5 or max(light_z) < 0.5:
                    raise RuntimeError(
                        f"light bezier handles did not bulge: z {min(light_z):.3f}..{max(light_z):.3f}")

                #opencode: point lights honor independent in/out handles too.
                light.update({
                    "pathHandlesIndependent": True,
                    "pathTangentsIn": [0.0] * (3 * 8),
                })
                light_indep_preset = temp / "light_bezier_independent.json"
                light_indep_preset.write_text(json.dumps(probe))
                light_independent = trim_to_path(
                    run_editor(light_indep_preset, frames=180, kind="light"),
                    line, tolerance=1.2)
                if len(light_independent) < 100:
                    raise RuntimeError(
                        f"too few independent light samples: "
                        f"{len(light_independent)}")
                light_indep_z = [sample[2] for sample in light_independent]
                if min(light_indep_z) < -0.05 or max(light_indep_z) < 0.5:
                    raise RuntimeError(
                        f"light independent handles wrong: z "
                        f"{min(light_indep_z):.3f}..{max(light_indep_z):.3f}")

            with_settings_preserved(exercise)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"GFX_MOTION_PATH_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_MOTION_PATH_CHECK: PASS circle tracking, waypoint polylines "
          "(models and lights), smooth splines, Bezier handles (mirrored, "
          "independent, and viewport-dragged with undo), and one-shot pauses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
