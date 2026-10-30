#!/usr/bin/env python3
#opencode: aggregate GPU audit summary — runs the curated quick gate set and
#   prints one pass/fail table with per-gate wall time. The gates own their own
#   assertions; this runner only schedules and reports, so it never
#   reimplements a metric. Use --full to include the editor/preset gates,
#   --only to pick gates, --json for a machine-readable report, and --jobs N
#   to overlap gates (each gate is a self-contained process with its own temp
#   dirs; the default stays sequential).

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import audit_common as audit
import audit_cache


# (name, script, args, tier): "quick" runs by default, "full" joins with --full,
#   "compute" is only selected explicitly (its opt-in build gate is separate).
GATES = [
    ("bloom", "bloom_audit.py", ["--quick"], "quick"),
    ("scene-smoke", "gfx_scene_smoke.py", [], "quick"),
    ("matrix-quick", "gfx_feature_matrix.py", ["--quick"], "quick"),
    #opencode: GUI counterpart gates — the quick matrix keeps the menu/widget
    #   surfaces in the quick tier; interaction/theme join the full matrix.
    ("gui-quick", "gui_audit.py", ["--quick"], "quick"),
    ("gui-layout-quick", "gui_layout_check.py", ["--quick"], "quick"),
    ("dither", "dither_check.py", [], "quick"),
    ("wireframe", "wireframe_check.py", [], "quick"),
    ("snow", "snow_check.py", [], "quick"),
    ("snowfall", "snowfall_check.py", [], "quick"),
    ("rain", "rain_check.py", [], "quick"),
    ("fogsprite", "fogsprite_check.py", [], "quick"),
    ("palette", "palette_check.py", [], "quick"),
    ("interlace", "interlace_check.py", [], "quick"),
    ("fog-light", "fog_light_check.py", [], "quick"),
    ("horizon", "gfx_horizon_check.py", [], "quick"),
    ("matrix", "gfx_feature_matrix.py", [], "full"),
    ("gui-matrix", "gui_audit.py", [], "full"),
    #opencode: the title's advertised sub-views must open without crashing
    #   (the EXTRAS card loop once indexed past its array).
    ("title-flow", "title_flow_check.py", [], "full"),
    ("gui-interaction", "gui_interaction_check.py", [], "full"),
    ("gui-theme", "gui_theme_check.py", [], "full"),
    ("gui-layout", "gui_layout_check.py", [], "full"),
    ("gui-shadow", "gui_shadow_check.py", [], "full"),
    ("gui-depth", "gui_depth_check.py", [], "full"),
    ("gui-glass", "gui_glass_check.py", [], "full"),
    ("gui-cursor-light", "gui_cursor_light_check.py", [], "full"),
    ("gui-pause-blur", "gui_pause_blur_check.py", [], "full"),
    ("gui-effect-contrast", "gui_effect_contrast_check.py", [], "full"),
    ("water", "gfx_water_check.py", [], "full"),
    #ai: independent owned-fixture water authoring/render regression coverage.
    ("water-authoring", "gfx_water_authoring_check.py", [], "full"),
    ("dof-resolution", "dof_resolution_check.py", [], "full"),
    ("probe", "probe_check.py", [], "full"),
    ("splat", "splat_check.py", [], "full"),
    ("mirror", "mirror_check.py", [], "full"),
    ("light", "gfx_light_check.py", [], "full"),
    ("snapshot", "gfx_snapshot_check.py", [], "full"),
    ("planar-material", "gfx_planar_material_check.py", [], "full"),
    ("mirror-alignment", "gfx_mirror_alignment_check.py", [], "full"),
    ("grid", "gfx_grid_check.py", [], "full"),
    ("temporal", "gfx_temporal_check.py", ["--quick"], "full"),
    ("materials", "materials_check.py", [], "full"),
    ("particle-velocity", "particle_velocity_check.py", [], "compute"),
    ("rig-inspector", "gfx_rig_inspector_check.py", [], "full"),
    ("animation", "gfx_animation_check.py", [], "full"),
    ("label-font", "gfx_label_font_check.py", [], "full"),
    ("motion-path", "gfx_motion_path_check.py", [], "full"),
    ("picker", "gfx_picker_check.py", [], "full"),
    ("light-picker", "gfx_light_picker_check.py", [], "full"),
    ("dock-layout", "gfx_dock_layout_check.py", [], "full"),
    ("preset-overwrite", "gfx_preset_overwrite_check.py", [], "full"),
    ("material-assets", "gfx_material_asset_check.py", [], "full"),
    ("asset-roots", "gfx_asset_roots_check.py", [], "full"),
    ("wavetable-editor", "wavetable_editor_check.py", [], "full"),
    ("textwriter-editor", "textwriter_editor_check.py", [], "full"),
    ("background-editor", "background_editor_check.py", [], "full"),
    ("sky-editor", "sky_editor_check.py", [], "full"),
    ("ui-audit", "ui_audit.py", [], "full"),
    ("physics-editor", "physics_editor_check.py", [], "full"),
    ("decal-bake", "gfx_decal_bake_check.py", [], "full"),
    ("cache", "audit_cache_check.py", [], "full"),
    ("debug", "debug_check.py", [], "full"),
    ("profile", "profile_check.py", [], "full"),
    ("error", "error_check.py", [], "full"),
    ("raymarch-features", "gfx_raymarch_features.py", [], "full"),
    ("raymarch", "gfx_raymarch_check.py", [], "full"),
    ("postprocess", "gfx_pp_check.py", [], "full"),
    ("background", "gfx_background_check.py", [], "full"),
    ("battle", "gfx_battle_check.py", [], "full"),
    ("export", "gfx_export_check.py", [], "full"),
    ("texture", "gfx_texture_check.py", [], "full"),
    ("primitive", "gfx_primitive_check.py", [], "full"),
    ("aero", "gfx_aero_check.py", [], "full"),
    ("prefab", "gfx_prefab_check.py", [], "full"),
    ("gl-level-ui", "gfx_gl_level_ui_check.py", [], "full"),
    ("camera-render", "gfx_camera_render_check.py", [], "full"),
    ("bijection", "gfx_bijection_check.py", [], "full"),
    ("skin-bones", "gfx_skin_bones_check.py", [], "full"),
    ("reflection-ui", "gfx_reflection_ui_check.py", [], "full"),
]
PASS_MARKERS = ("PASS", "PASSED")


def verdict_line(output):
    for line in reversed(output.splitlines()):
        if any(marker in line for marker in PASS_MARKERS):
            return line.strip()
        if "FAIL" in line:
            return line.strip()
    return output.strip().splitlines()[-1].strip() if output.strip() else "(no output)"


def main():
    parser = argparse.ArgumentParser(
        description="Run the curated GPU audit gates and report a summary.")
    parser.add_argument("--full", action="store_true",
                        help="include the editor/preset gates (slower)")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated gate names")
    parser.add_argument("--list", action="store_true",
                        help="list gates and exit")
    parser.add_argument("--json", metavar="PATH",
                        help="write the summary as JSON")
    parser.add_argument("--timeout", type=float, default=1200.0,
                        help="per-gate timeout in seconds (default: 1200)")
    parser.add_argument("--jobs", type=int, default=1, metavar="N",
                        help="run gates concurrently (default 1 = sequential)")
    parser.add_argument("--no-cache", action="store_true",
                        help="ignore cached gate results")
    parser.add_argument("--cache-clear", action="store_true",
                        help="clear the gate cache before running")
    args = parser.parse_args()

    names = {gate[0] for gate in GATES}
    if args.list:
        for name, script, script_args, tier in GATES:
            print(f"{name:20s} {tier:5s} {script} {' '.join(script_args)}")
        return 0
    selected = set(names)
    if args.only:
        requested = {item.strip() for item in args.only.split(",") if item.strip()}
        unknown = sorted(requested - names)
        if unknown:
            parser.error("unknown gate(s): " + ", ".join(unknown))
        selected &= requested
    elif not args.full:
        selected = {gate[0] for gate in GATES if gate[3] == "quick"}
    else:
        selected = {gate[0] for gate in GATES if gate[3] in ("quick", "full")}
    if not audit.BIN.is_file():
        print(f"AUDIT_SUMMARY: binary missing: {audit.BIN}", file=sys.stderr)
        return 1

    environment = dict(os.environ)
    environment["DISPLAY"] = audit.display()
    ordered = [gate for gate in GATES if gate[0] in selected]
    use_cache = (not args.no_cache
                 and os.environ.get("AUDIT_CACHE", "1") != "0")
    if args.cache_clear:
        audit_cache.clear()
    cache = audit_cache.load() if use_cache else {
        "version": audit_cache.CACHE_VERSION, "gates": {}}
    cache_base = audit_cache.base_digest() if use_cache else None

    def run_gate(gate):
        name, script, script_args, tier = gate
        script_path = Path(audit.ROOT) / "scripts" / script
        if not script_path.is_file():
            return {"gate": name, "ok": False, "seconds": 0.0,
                    "verdict": "script missing"}
        key = audit_cache.gate_key(cache_base, script_path) if use_cache \
            else None
        if use_cache:
            entry = audit_cache.lookup(cache, name, key)
            if entry:
                return {"gate": name, "ok": True, "seconds": 0.0,
                        "verdict": f"{entry['verdict']} (cached)",
                        "cached": True}
        start = time.perf_counter()
        try:
            result = subprocess.run(
                ["python3", str(script_path)] + script_args,
                cwd=audit.ROOT, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=args.timeout)
            output = audit.strip_ansi(result.stdout)
            ok = result.returncode == 0
            verdict = verdict_line(output)
        except subprocess.TimeoutExpired:
            ok = False
            verdict = f"timeout after {args.timeout:.0f}s"
        seconds = round(time.perf_counter() - start, 2)
        if ok and use_cache:
            audit_cache.store(cache, name, key, seconds, verdict, script)
        return {"gate": name, "ok": ok, "seconds": seconds,
                "verdict": verdict}

    #opencode: parallel runs launch several GL contexts at once; startup or
    #   timing contention can flake a gate. Retry any failure once and keep
    #   the first verdict on record, so flakiness stays visible in the report
    #   instead of being silently absorbed.
    def run_gate_with_retry(gate):
        record = run_gate(gate)
        if not record["ok"]:
            print(f"{record['gate']:20s} failed, retrying once "
                  f"({record['verdict']})", flush=True)
            first_verdict = record["verdict"]
            record = run_gate(gate)
            record["retried"] = True
            record["first_verdict"] = first_verdict
        return record

    summary_start = time.perf_counter()
    print(f"audit summary: {len(ordered)} gates"
          + (" (full)" if args.full else " (quick)")
          + (f", {args.jobs} jobs" if args.jobs > 1 else ""))
    results = []
    if args.jobs > 1:
        with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.jobs) as pool:
            futures = [pool.submit(run_gate_with_retry, gate)
                       for gate in ordered]
            for future in concurrent.futures.as_completed(futures):
                record = future.result()
                results.append(record)
                print(f"{record['gate']:20s} "
                      f"{'ok' if record['ok'] else 'FAIL':4s} "
                      f"{record['seconds']:6.1f}s  {record['verdict']}",
                      flush=True)
        order = {gate[0]: index for index, gate in enumerate(ordered)}
        results.sort(key=lambda record: order[record["gate"]])
    else:
        for gate in ordered:
            record = run_gate(gate)
            results.append(record)
            print(f"{record['gate']:20s} "
                  f"{'ok' if record['ok'] else 'FAIL':4s} "
                  f"{record['seconds']:6.1f}s  {record['verdict']}",
                  flush=True)
    wall = time.perf_counter() - summary_start
    if use_cache:
        audit_cache.save(cache)
    failures = [record["gate"] for record in results if not record["ok"]]
    cached_count = sum(1 for record in results if record.get("cached"))

    total = sum(item["seconds"] for item in results)
    cached_note = f", {cached_count} cached" if cached_count else ""
    if args.jobs > 1:
        print(f"audit summary: {len(results) - len(failures)}/{len(results)} "
              f"gates passed in {wall:.1f}s wall ({total:.1f}s gate time)"
              f"{cached_note}")
    else:
        print(f"audit summary: {len(results) - len(failures)}/{len(results)} "
              f"gates passed in {total:.1f}s{cached_note}")
    if args.json:
        json_path = Path(args.json)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(
            {"full": args.full, "gates": results, "failures": failures,
             "total_seconds": round(total, 2), "wall_seconds": round(wall, 2)},
            indent=2))
    if failures:
        print(f"AUDIT_SUMMARY: FAIL — {failures}")
        return 1
    print("AUDIT_SUMMARY: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
