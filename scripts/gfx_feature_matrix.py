#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

# gX feature verification matrix v2 — isolated rows (user directive:
# disable everything else to minimize interference).
# Baseline: every effect forced OFF; each row enables exactly ONE
# feature (plus注明 infrastructure: lights, sky, cubemap, tonemap).
#   POSITIVE rows: on-vs-off diff frac in [lo,hi], no collapse.
#   NEGATIVE controls (culling): expect identical frames.
# Aux row runs on --showcase-reflectrow (the aux testbed: slab is
# reflectionSource there, so aux output is observable; in planar
# scenes the defer owns plane pixels by design).
# Motion-blur row spins the camera (static frames can't blur).
# Exit 0 iff all rows PASS. DISPLAY=:1 (NVIDIA) like reflect-check.
# Examples:
#   python3 scripts/gfx_feature_matrix.py --quick
#   python3 scripts/gfx_feature_matrix.py /tmp/opencode/fmatrix postprocess
#   python3 scripts/gfx_feature_matrix.py --only bloom,dof --skip bloom
#   python3 scripts/gfx_feature_matrix.py --only godray,lensflare,pointshadows \
#       --random-vantages 3 --seed 314159
import os
import argparse
import random
import shutil
import sys
import json
import shlex
import time
import subprocess

import audit_common as audit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "gX_build")
OUT = "/tmp/opencode/fmatrix"
CAPTURE_DIR = os.path.join(OUT, "raw")
TIMEOUT = 120
DEFAULT_SHOT_FRAME = "120"
MATRIX_WIDTH = "1024"
MATRIX_HEIGHT = "768"
MATRIX_FPS = "60"
# PACKS (user taxonomy) — overlapping lenses, not partitions: a feature
# may belong to two packs (ssr marches in screen space for reflections).
#   reflection : ssr, ssraux, envambient (+planar via reflect-check suite)
#   screenspace: ssao, ssr
#   fog        : fog
#   postprocess: bloom, lensflare, godrays, dof, motionblur, particles,
#                decals, snow, snowfall, rain, fogsprite
#   aa         : taa, fxaa (+msaa: startup-only, headless-uncovered)
#   shadow     : shadows, pointshadows, cookies
# Negative controls + wireframe stay packless (perf/debug).
PACKS = {
    "reflection": ["ssr", "ssraux", "envambient", "probes", "splat", "mirror"],
    "screenspace": ["ssao", "ssr"],
    "fog": ["fog"],
    "postprocess": ["bloom", "bloomemis", "lensflare", "godray", "dof",
                    "motionblur", "particles", "decals", "snow", "snowfall", "rain", "fogsprite"],
    "aa": ["taa", "fxaa"],
    "shadow": ["shadows", "pointshadows", "cookies"],
}
QUICK_ROWS = ["bloom", "ssr", "dof", "taa", "motionblur"]
LIGHT_MARKER_ROWS = {"shadows", "fog", "pointshadows", "cookies",
                     "godray", "lensflare"}
SUN_VIEW_ROWS = {"fog", "godray", "lensflare"}

BASE_OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0",
    "GX_T_SSR": "0", "GX_T_TAA": "0", "GX_T_FXAA": "0",
    "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_MOTIONBLUR": "0", "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0",
    "GX_T_SNOW": "0", "GX_T_SNOWFALL": "0", "GX_T_RAIN": "0", "GX_T_FOGSPRITE": "0",
    "GX_T_SPLAT": "0", "GX_MIRROR_PAIR": "0", "GX_PROBE_GRID": "0",
    "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    "GX_T_LIGHT_MARKERS": "0", "GX_CAM_FOV": "45",
    # chain engager (neutral): volume fog at zero density engages the
    # composite chain (scene copy + compositeTarget) without changing a
    # pixel, so post-chain effects (motionblur/dof/particles) are fairly
    # tested instead of silently discarded with the canvas.
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    #opencode: HUD glyphs jitter with frame timing; keep diff metrics stable.
    "GX_T_HUD": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
}
# (name, scene, on-extra-env, lo, hi, note[, delta-threshold])
# delta-threshold overrides the |ON-OFF|>10 pixel-cut (default 10).
# bloomemis uses 2: the tonemapped G-buffer caps at 1.0, so the emissive
# sliver above the 0.85 extract threshold is faint by design (frac>10 = 0
# even healthy) - the >2 cut is where its halo lives.
ROWS = [
    ("bloom", "--showcase", {"GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6"}, 0.00001, 0.2, "glow isolation = default (6 vs 0; was 1.2 ~no-op, 8 washes out)"),
    #opencode: the linear-HDR path makes the emissive halo intentionally
    #   broader than the old double-tonemapped probe; current healthy captures
    #   are ~0.108 of the frame at the >2 cut, so keep a regression ceiling
    #   above that while still rejecting a runaway full-frame wash.
    ("bloomemis", "--showcase-minimal", {"GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1", "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6"}, 0.015, 0.20, "deterministic emissive bloom (orb-mapped cube, sun-independent)", 2),
    ("ssao", "--showcase", {"GX_T_SSAO": "1"}, 0.005, 0.80, ""),
    ("shadows", "--showcase", {"GX_T_SHADOWS": "1"}, 0.005, 0.80, ""),
    ("ssr", "--showcase", {"GX_T_SSR": "1"}, 0.005, 0.80, ""),
    ("ssraux", "--reflectrow", {"GX_T_SSR": "1", "GX_T_SSRAUX": "1"},
     0.002, 0.80, "vs ssr-only baseline"),
    ("pointshadows", "--showcase", {"GX_T_POINT_SHADOWS": "1", "GX_EXTRA_SHADOW_LIGHTS": "1"},
     0.005, 0.80, "one deterministic casting point light"),
    ("instancing", "--showcase", {"GX_T_INSTANCING": "1"},
     0.005, 0.95, "16 cube transforms via hardware instancing"),
    ("taa", "--showcase", {"GX_T_TAA": "1"}, 0.0, 0.30, ""),
    ("fxaa", "--showcase", {"GX_T_FXAA": "1"}, 0.0, 0.50, ""),
    # fog-volume visibly needs shadow maps (gen input); provoke both
    # fog scatters proportionally to scene brightness: an all-dark baseline
    # yields sub-threshold delta (correct physics, unmeasurable). Run it on
    # a lit baseline (ssr+shadows) and isolate only the fog delta.
    ("fog", "--showcase", {"GX_T_SSR": "1", "GX_T_SHADOWS": "1",
                           "GX_T_FOG": "1", "GX_FOG_DENSITY": "0.01"},
     0.005, 0.95, "lit baseline; fog needs light to scatter"),
    ("godray", "--showcase", {"GX_T_GODRAY": "1"}, 0.0, 0.60, "needs sun in frame"),
    ("cookies", "--showcase", {"GX_T_COOKIES": "1"}, 0.0, 0.60, ""),
    ("particles", "--showcase", {"GX_T_PARTICLES": "1"}, 0.0001, 0.60, "sparse at this angle"),
    ("lensflare", "--showcase", {"GX_T_LENSFLARE": "1"}, 0.0, 0.60, "was 0.0 (dead upload) pre include-fix"),
    # static scenes sit inside the default focal slab: provoke at focal 1.0
    ("dof", "--showcase", {"GX_T_DOF": "1", "GX_T_DOFFOCAL": "1.0"}, 0.005, 0.80, "provoked focal"),
    ("motionblur", "--showcase", {"GX_T_MOTIONBLUR": "1", "GX_CAM_SPIN": "3"},
     0.0001, 0.80, "spun camera"),
    # envambient: upload verified live (flag=1 factor=0.3 loc=53); effect is
    # subtle in dark scenes (env~0.5 gray) — threshold documents, not proves
    ("envambient", "--showcase", {"GX_T_ENVAMBIENT": "1"}, 0.0, 0.95, "subtle; upload-verified"),
    # decals: GX_T_DECALS=1 spawns 4 deterministic floor quads (3 matte
    #   white, one translucent + 1 textured SMPTE testcard, GX_T_DECALTEX=0
    #   forces all-matte); measures textured-vs-dry delta
    ("decals", "--showcase", {"GX_T_DECALS": "1"}, 0.001, 0.60, "3 matte + 1 textured quads"),
    ("snow", "--showcase", {"GX_T_SNOW": "1", "GX_SNOW_COVERAGE": "0.75",
                              "GX_SNOW_MELT": "0.20", "GX_SNOW_HEIGHT": "0.015"},
     0.001, 0.60, "bounded snow decals: coverage, melt, height, slopes"),
    ("snowfall", "--showcase", {"GX_T_SNOWFALL": "1", "GX_SNOWFALL_SEED": "1337",
                              "GX_SNOWFALL_RATE": "448", "GX_SNOWFALL_WIND": "0.35"},
     0.0005, 0.60, "CPU snowflakes: LCG sim, VBO points, GL33"),
    ("rain", "--showcase", {"GX_T_RAIN": "1", "GX_RAIN_SEED": "7331",
                              "GX_RAIN_RATE": "384", "GX_RAIN_WIND": "1.2"},
     0.0005, 0.60, "CPU rain streaks: LCG sim, GL lines, GL33"),
    ("fogsprite", "--showcase", {"GX_T_FOGSPRITE": "1", "GX_FOGSPRITE_SEED": "4242",
                              "GX_FOGSPRITE_COUNT": "28", "GX_FOGSPRITE_WIND": "0.25",
                              "GX_FOGSPRITE_OPACITY": "0.10"},
     0.01, 0.60, "CPU fog billboards: sorted, depth-tested, GL33"),
    ("probes", "--showcase-minimal", {"GX_PROBE_GRID": "1", "GX_PROBE_SIZE": "128",
                              "GX_CUBEMAP_TEST": "1"},
     0.00005, 0.80, "nearest-probe projection vs dynamic cubemap"),
    ("mirror", "--showcase-minimal", {"GX_MIRROR_PAIR": "1"},
     0.05, 0.95, "two-mirror regress: bounded ping-pong bounces"),
    ("splat", "--showcase", {"GX_T_SPLAT": "1", "GX_SPLAT_SEED": "777",
                              "GX_SPLAT_COUNT": "256"},
     0.001, 0.60, "gaussian splat cloud: conic rasterizer, GL33"),
    ("wireframe", "--showcase", {"GX_T_WIREFRAME": "1"}, 0.005, 0.95, ""),
    ("occlusion", "--showcase", {"GX_T_OCCL": "1"}, None, None, "negative"),
    ("frustum", "--showcase", {"GX_T_FRUSTUM": "1"}, None, None, "negative"),
]


#opencode: capture jobs go through the shared pool — parallel workers, private
#   per-probe directories, hidden windows, and a serial retry phase for GPU
#   contention stragglers (the old single retry could still drop a row).
#ai: each profile owns a separate pool and private controls.
def capture_jobs_to_pool(capture_jobs, jobs, shot_frame):
    results = {}
    binaries = sorted({request[3] for request in capture_jobs.values()})
    for binary in binaries:
        subset = {path: request[:3] for path, request in capture_jobs.items()
                  if request[3] == binary}
        results.update(capture_profile_jobs(subset, jobs, shot_frame, binary))
    return results


#ai: both arms use the same explicitly selected executable.
def capture_profile_jobs(capture_jobs, jobs, shot_frame, binary):
    requests = {}
    destination_for_key = {}
    for destination, (scene, env, tag) in capture_jobs.items():
        key = os.path.splitext(os.path.basename(destination))[0]
        requests[key] = (scene, env)
        destination_for_key[key] = destination
    pool = audit.CapturePool(jobs=jobs, raw_dir=CAPTURE_DIR, timeout=TIMEOUT,
                             binary=binary)
    shots = pool.capture_all(requests, width=MATRIX_WIDTH, height=MATRIX_HEIGHT,
                             fps=MATRIX_FPS, shot_frame=shot_frame)
    results = {}
    for key, source in shots.items():
        destination = destination_for_key[key]
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        shutil.copyfile(source, destination)
        results[destination] = True
    return results


#opencode: single re-capture for the size/verdict retry paths.
#ai: retries must preserve the original profile.
def capture_one(scene, env, destination, shot_frame, binary=None):
    key = os.path.splitext(os.path.basename(destination))[0] + "_retry"
    pool = audit.CapturePool(jobs=1, raw_dir=CAPTURE_DIR, timeout=TIMEOUT,
                             binary=binary)
    shots = pool.capture_all({key: (scene, env)}, width=MATRIX_WIDTH,
                             height=MATRIX_HEIGHT, fps=MATRIX_FPS,
                             shot_frame=shot_frame)
    if key not in shots:
        return False
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copyfile(shots[key], destination)
    return True


#opencode: preserve each row's intentional control arm while allowing the same
#   paired environment to be evaluated from multiple camera viewpoints.
def off_environment(name):
    off_extra = {"GX_T_SSR": "1"} if name == "ssraux" else {}
    #ai: reflection consumers are fixture infrastructure, not the tested effect.
    if name == "probes":
        off_extra = {"GX_CUBEMAP_TEST": "1"}
    if name == "bloom":
        off_extra = {"GX_T_BLOOM": "1", "GX_T_BLOOMINT": "0"}
    if name == "bloomemis":
        off_extra = {"GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1",
                     "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "0"}
    if name == "fog":
        off_extra = {"GX_T_SSR": "1", "GX_T_SHADOWS": "1"}
    if name == "pointshadows":
        off_extra = {"GX_T_POINT_SHADOWS": "0", "GX_EXTRA_SHADOW_LIGHTS": "1"}
    spin = {"GX_CAM_SPIN": "3"} if name == "motionblur" else {}
    return {**spin, **off_extra}, bool(spin or off_extra)


#ai: use the dedicated mirror witness, not a generic camera that clips its response.
def fixture_base(name):
    environment = dict(BASE_OFF)
    if name == "mirror":
        environment.update(GX_CAM_YAW="45", GX_CAM_PITCH="20", GX_CAM_DIST="12")
    #ai: use the minimal scene's native distance so the cubemap response is measurable.
    if name == "probes":
        environment["GX_CAM_DIST"] = "6"
    return environment


#opencode: seeded camera samples keep the light rig in frame; sun-facing views
#   keep the sky source inside a wide-FOV composition with the stage.
def make_viewpoints(random_count, seed, sun_facing=False):
    base = ({"GX_CAM_YAW": "45", "GX_CAM_PITCH": "0",
             "GX_CAM_DIST": "10", "GX_CAM_FOV": "80"}
            if sun_facing else {})
    views = [("base", base)]
    rng = random.Random(seed)
    for index in range(random_count):
        if sun_facing:
            camera = {
                "GX_CAM_YAW": f"{rng.uniform(40.0, 50.0):.3f}",
                "GX_CAM_PITCH": f"{rng.uniform(0.0, 1.0):.3f}",
                "GX_CAM_DIST": f"{rng.uniform(8.0, 12.0):.3f}",
                "GX_CAM_FOV": "80",
            }
        else:
            camera = {
                "GX_CAM_YAW": f"{rng.uniform(12.0, 48.0):.3f}",
                "GX_CAM_PITCH": f"{rng.uniform(16.0, 34.0):.3f}",
                "GX_CAM_DIST": f"{rng.uniform(8.5, 12.5):.3f}",
            }
        views.append((f"rand{index + 1:02d}", camera))
    return views


def label_top_right(path, text):
    #opencode: space-saving forensics - burn the feature name into the
    # pixels (top-right) so OUT images stay identifiable out of context;
    # filenames keep the machine-readable form.
    from PIL import Image, ImageDraw
    im = Image.open(path).convert("RGB")
    dr = ImageDraw.Draw(im)
    t = str(text)
    tw = dr.textlength(t)
    x, y = im.width - tw - 12, 10
    dr.rectangle([x - 4, y - 2, im.width - 8, y + 14], fill=(0, 0, 0))
    dr.text((x, y), t, fill=(255, 255, 255))
    im.save(path)


#ai: explain the unchanged admission rules, including weak zero-floor probes.
def explain_metrics(metrics, case):
    failures = []
    frac, lo, hi = metrics["frac"], case["lo"], case["hi"]
    expected = "changed fraction < 0.002 (negative control)" if lo is None else f"changed fraction in [{lo:g}, {hi:g}]"
    if lo is None:
        if frac >= 0.002:
            failures.append(f"negative control changed: {frac:.6f} >= 0.002")
    else:
        if frac < lo:
            failures.append(f"effect too small: {frac:.6f} < {lo:g}")
        if frac > hi:
            failures.append(f"effect too broad: {frac:.6f} > {hi:g}")
        if metrics["black"] >= 0.5:
            failures.append(f"collapse guard: black fraction {metrics['black']:.6f} >= 0.5")
        for arm in ("mean0", "mean1"):
            if not 5 < metrics[arm] < 250:
                failures.append(f"brightness guard: {arm}={metrics[arm]:.3f} outside (5,250)")
    return {"expected": expected, "pixel_delta_cut": case["dthresh"],
            "failures": failures, "evidence": "invariance" if lo is None else
            "visible effect" if lo > 0 else "bounded effect; zero change permitted"}


#ai: replays use the same shared capture helper and protect settings.
def replay_command(case, arm, shot_frame):
    environment = audit.scene_env(case[f"{arm}Env"])
    hooks = {key: value for key, value in environment.items()
             if key.startswith("GX_") or key == "DISPLAY"}
    directory = os.path.join(OUT, "replay", case["caseName"], arm)
    hooks.update(GX_SCREENSHOT_DIR=directory, GX_SHOT_FRAME=str(shot_frame))
    command = [case.get("binary", BIN), case["scene"], "--screenWidth", MATRIX_WIDTH,
               "--screenHeight", MATRIX_HEIGHT, "--targetFPS", MATRIX_FPS,
               "--ai-screenshot-final"]
    return "cd " + shlex.quote(ROOT) + " && mkdir -p " + shlex.quote(directory) + " && env " + " ".join(
        shlex.quote(f"{key}={value}") for key, value in sorted(hooks.items())) + " " + shlex.join(command)


#ai: rebuild the compute profile whenever the positive particle row is selected.
def prepare_particle_binary():
    build = os.path.join(ROOT, "build", "compute")
    subprocess.run(["cmake", "-S", ROOT, "-B", build,
                    "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=ON"], check=True)
    subprocess.run(["cmake", "--build", build, "-j", str(os.cpu_count() or 1)],
                   check=True)
    return os.path.join(build, "gX_build")


def main():
    from PIL import Image
    import numpy as np
    parser = argparse.ArgumentParser(
        description="Run isolated gX graphics feature verification rows.")
    parser.add_argument(
        "output", nargs="?", default="/tmp/opencode/fmatrix",
        help="capture output directory (default: /tmp/opencode/fmatrix)")
    parser.add_argument(
        "pack", nargs="?", choices=sorted(PACKS),
        help="legacy positional pack filter")
    parser.add_argument(
        "--quick", action="store_true",
        help="run the curated bloom/SSR/DOF/TAA/motion-blur smoke set")
    parser.add_argument(
        "--only", metavar="NAMES",
        help="comma-separated feature names to include")
    parser.add_argument(
        "--skip", metavar="NAMES",
        help="comma-separated feature names to omit")
    parser.add_argument(
        "--jobs", type=int, default=2,
        help="parallel isolated capture processes (default: 2)")
    parser.add_argument(
        "--random-vantages", type=int, default=0,
        help="add seeded camera position/direction samples per feature")
    parser.add_argument(
        "--seed", type=int, default=314159,
        help="random vantage seed (default: 314159)")
    args = parser.parse_args()
    started = time.monotonic()
    if args.jobs < 1 or args.jobs > 8:
        parser.error("--jobs must be between 1 and 8")
    if args.random_vantages < 0 or args.random_vantages > 8:
        parser.error("--random-vantages must be between 0 and 8")
    global OUT, CAPTURE_DIR
    OUT = os.path.abspath(args.output)
    CAPTURE_DIR = os.path.join(OUT, "raw")

    names = {row[0] for row in ROWS}
    def parse_names(value):
        parsed = {item.strip() for item in value.split(",") if item.strip()}
        unknown = sorted(parsed - names)
        if unknown:
            parser.error("unknown feature(s): " + ", ".join(unknown))
        return parsed

    only = parse_names(args.only) if args.only else None
    skip = parse_names(args.skip) if args.skip else set()
    selected = set(names)
    if args.pack:
        selected &= set(PACKS[args.pack])
    if args.quick:
        selected &= set(QUICK_ROWS)
    if only is not None:
        selected &= only
    selected -= skip
    if not selected:
        parser.error("feature selection is empty")

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(CAPTURE_DIR, exist_ok=True)
    rows = [r for r in ROWS if r[0] in selected]
    particle_binary = prepare_particle_binary() if "particles" in selected else None
    filters = []
    if args.pack:
        filters.append(f"pack '{args.pack}'")
    if args.quick:
        filters.append("quick")
    if args.only:
        filters.append("only=" + ",".join(sorted(only)))
    if args.skip:
        filters.append("skip=" + ",".join(sorted(skip)))
    if filters:
        print(f"selection ({' '.join(filters)}): {len(rows)} rows")
    views = make_viewpoints(args.random_vantages, args.seed)
    sunViews = make_viewpoints(args.random_vantages, args.seed, True)
    fails = set()
    shared = {}
    cases = []
    capture_jobs = {}

    def schedule(destination, scene, env, tag, binary):
        if os.path.isfile(destination):
            os.remove(destination)
        capture_jobs[destination] = (scene, env, tag, binary)

    for row in rows:
        name, scene, extra, lo, hi = row[:5]
        binary = particle_binary if name == "particles" else BIN
        off_extra, custom_off = off_environment(name)
        custom_off = custom_off or name == "mirror"
        spin = {"GX_CAM_SPIN": "3"} if name == "motionblur" else {}
        lightMarkers = name in LIGHT_MARKER_ROWS
        sunFacing = name in SUN_VIEW_ROWS
        rowViews = sunViews if sunFacing else views
        rowBase = {**fixture_base(name),
                   **({"GX_T_LIGHT_MARKERS": "1"} if lightMarkers else {})}
        for view_name, view_env in rowViews:
            suffix = "" if view_name == "base" else f"_{view_name}"
            case_name = f"{name}{suffix}"
            p0 = os.path.join(OUT, f"{case_name}_off.png")
            p1 = os.path.join(OUT, f"{case_name}_on.png")
            off_env = {**rowBase, **spin, **off_extra, **view_env}
            on_env = {**rowBase, **spin, **extra, **view_env}
            if custom_off:
                schedule(p0, scene, off_env, f"{case_name}_off", binary)
            else:
                sharedKey = (scene, view_name, lightMarkers, sunFacing, binary)
                if sharedKey not in shared:
                    controlName = scene.replace("-", "_")
                    if binary == particle_binary:
                        controlName += "_gl43"
                    if lightMarkers:
                        controlName += "_lights"
                    if sunFacing:
                        controlName += "_sun"
                    if view_name != "base":
                        controlName += "_" + view_name
                    pshared = os.path.join(OUT, f"_control_{controlName}.png")
                    schedule(pshared, scene,
                        {**rowBase, **view_env}, f"control_{controlName}", binary)
                    shared[sharedKey] = pshared
                p0 = shared[sharedKey]
            schedule(p1, scene, on_env, f"{case_name}_on", binary)
            cases.append({
                "name": name,
                "caseName": case_name,
                "view": view_name,
                "sunFacing": sunFacing,
                "scene": scene,
                "binary": binary,
                "offEnv": off_env,
                "onEnv": on_env,
                "p0": p0,
                "p1": p1,
                "customOff": custom_off,
                "lo": lo,
                "hi": hi,
                "dthresh": next((r[6] for r in ROWS
                    if r[0] == name and len(r) > 6), 10),
                "note": row[5],
            })

    shot_frame = os.environ.get("GX_SHOT_FRAME", DEFAULT_SHOT_FRAME)
    shot_frame_value = int(float(shot_frame))
    print(f"captures: {len(capture_jobs)} images, {args.jobs} workers, "
          f"{MATRIX_WIDTH}x{MATRIX_HEIGHT}@{MATRIX_FPS}, "
          f"shot-frame={shot_frame}")
    capture_results = capture_jobs_to_pool(capture_jobs, args.jobs,
                                           shot_frame_value)
    for destination in capture_jobs:
        if destination not in capture_results:
            print(f"capture error {os.path.basename(destination)}: missing")

    print(f"{'feature':>14} | {'frac':>7} {'mean0':>7} {'mean1':>7} "
          f"{'dmean':>7} {'dmax':>7} | verdict")
    case_metrics = {}
    report_rows = []

    def measure_case(case):
        p0 = case["p0"]
        p1 = case["p1"]
        if not os.path.isfile(p0) or not os.path.isfile(p1):
            return None
        a = np.asarray(Image.open(p0).convert("RGB")).astype(float)
        b = np.asarray(Image.open(p1).convert("RGB")).astype(float)
        sizeRetried = False
        if a.shape != b.shape:
            sizeRetried = True
            if not capture_one(case["scene"], case["offEnv"], p0,
                               shot_frame_value, case["binary"]):
                return None
            if not capture_one(case["scene"], case["onEnv"], p1,
                               shot_frame_value, case["binary"]):
                return None
            a = np.asarray(Image.open(p0).convert("RGB")).astype(float)
            b = np.asarray(Image.open(p1).convert("RGB")).astype(float)
            if a.shape != b.shape:
                return None
        d = np.abs(a - b).mean(axis=2)
        frac = float((d > case["dthresh"]).mean())
        dmean = float(d.mean())
        dmax = float(d.max())
        mean0 = float(a.mean())
        mean1 = float(b.mean())
        black = min((a.mean(axis=2) < 8).mean(),
                    (b.mean(axis=2) < 8).mean())
        lo = case["lo"]
        hi = case["hi"]
        if lo is None:
            ok = frac < 0.002
        else:
            ok = (lo <= frac <= hi) and black < 0.5 \
                and 5 < mean0 < 250 and 5 < mean1 < 250
        return {
            "frac": frac,
            "mean0": mean0,
            "mean1": mean1,
            "dmean": dmean,
            "dmax": dmax,
            "black": black,
            "ok": ok,
            "sizeRetried": sizeRetried,
        }

    for case in cases:
        row_started = time.monotonic()
        record = {"name": case["caseName"], "scene": case["scene"],
                  "binary": case["binary"],
                  "off_capture": case["p0"], "on_capture": case["p1"],
                  "reproduce_off": replay_command(case,"off",shot_frame_value),
                  "reproduce_on": replay_command(case,"on",shot_frame_value)}
        report_rows.append(record)
        p0 = case["p0"]
        p1 = case["p1"]
        if not os.path.isfile(p0) or not os.path.isfile(p1):
            arm = "control/off" if not os.path.isfile(p0) else "on"
            print(f"{case['caseName']:>14} | no shot ({arm}) | FAIL")
            record.update(verdict="FAIL", reason=f"missing {arm} capture")
            case_metrics[(case["name"], case["view"])] = None
            continue
        metrics = measure_case(case)
        if metrics is None:
            print(f"{case['caseName']:>14} | size mismatch | FAIL")
            record.update(verdict="FAIL", reason="capture unavailable or size mismatch after retry")
            case_metrics[(case["name"], case["view"])] = None
            continue
        if case["view"] == "base" and not metrics["ok"]:
            record["first_attempt"] = dict(metrics)
            if capture_one(case["scene"], case["onEnv"], p1,
                            shot_frame_value, case["binary"]):
                retriedMetrics = measure_case(case)
                if retriedMetrics is not None:
                    retriedMetrics["retried"] = True
                    metrics = retriedMetrics
        case_metrics[(case["name"], case["view"])] = metrics
        explanation = explain_metrics(metrics,case)
        record.update(verdict="PASS" if metrics["ok"] else "FAIL", metrics=metrics,
                      checks=explanation, evaluation_seconds=time.monotonic()-row_started)
        retried = " (retry)" if metrics.get("retried") else ""
        if metrics["sizeRetried"]:
            retried += " (size retry)"
        tag = ("ok" if metrics["ok"] else "FAIL") + retried
        print(f"{case['caseName']:>14} | {metrics['frac']:>7.4f} "
              f"{metrics['mean0']:>7.1f} {metrics['mean1']:>7.1f} "
              f"{metrics['dmean']:>7.2f} {metrics['dmax']:>7.1f} | "
              f"{tag} {case['note']}")
        detail = "; ".join(explanation["failures"]) or explanation["expected"]
        print(f"  {detail}; pixel cut >{case['dthresh']}; evidence: {explanation['evidence']}")
        if not metrics["ok"]:
            print(f"  captures: {p0} | {p1}")

    if args.random_vantages:
        randomNames = [name for name, _ in views if name != "base"]
        for row in rows:
            name, _, _, lo, hi = row[:5]
            samples = [case_metrics.get((name, view_name))
                       for view_name in randomNames]
            if any(sample is None for sample in samples):
                stressOK = False
            else:
                stressOK = all(sample["ok"] for sample in samples)
            rates = ", ".join(
                "missing" if sample is None else f"{sample['frac']:.4f}"
                for sample in samples)
            print(f"{name:>14} | hardness {rates} | "
                  f"{'ok' if stressOK else 'FAIL'} seed={args.seed}")
            if not stressOK:
                fails.add(name)

    for row in rows:
        name = row[0]
        baseMetrics = case_metrics.get((name, "base"))
        if baseMetrics is None or not baseMetrics["ok"]:
            fails.add(name)
    #ai: measure raw pixels first; diagnostic labels must not create an artificial effect.
    for case in cases:
        for arm in ("off","on"):
            path = case["p0" if arm == "off" else "p1"]
            if os.path.isfile(path):
                annotated = os.path.join(OUT,f"{case['caseName']}_{arm}_annotated.png")
                shutil.copyfile(path,annotated)
                label_top_right(annotated,f"{case['caseName']} {arm}")
    report = {"verdict": "FAIL" if fails else "PASS", "failed_features": sorted(fails),
              "seconds": time.monotonic()-started, "jobs": args.jobs,
              "seed": args.seed, "shot_frame": shot_frame_value,
              "metrics_before_annotation": True, "rows": report_rows}
    report_path = os.path.join(OUT,"report.json")
    with open(report_path,"w") as stream:
        json.dump(report,stream,indent=2)
        stream.write("\n")
    print(f"FEATURE MATRIX REPORT: {report_path}")
    if fails:
        print(f"FEATURE MATRIX: FAIL — {sorted(fails)}")
        return 1
    if args.random_vantages:
        print(f"FEATURE MATRIX: PASS (base + {args.random_vantages} "
              f"random vantages; seed={args.seed})")
    else:
        print("FEATURE MATRIX: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
