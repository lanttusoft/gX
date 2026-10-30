#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic showcase GIF generator. Captures N fixed
#   scene/camera frames (one headless run per frame, yaw-swept for orbits)
#   and assembles them with PIL (GIF or animated WebP, no extra deps).
#   Reuses ssr_lab.capture so framing/metrics stay comparable with the lab.

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssr_lab import COMMON_ENV, capture

import audit_common as audit

from PIL import Image

#opencode: engine burst primitive (main_update.c): GX_BURST=1 with
#   --ai-screenshot-after N captures 12 consecutive frames
#   (burst_000.png .. burst_011.png) from one live run, then exits.
BURST_FRAMES = 12

SCENES = {
    "showcase": "--showcase",
    "minimal": "--showcase-minimal",
    "reflectrow": "--reflectrow",
    "planar": "--showcase-planar",
}


def parse_env(spec):
    if "=" not in spec:
        raise ValueError("env must be KEY=VALUE")
    key, value = spec.split("=", 1)
    if not key:
        raise ValueError("env must be KEY=VALUE")
    return key, value


def main():
    parser = argparse.ArgumentParser(description="gX deterministic GIF generator")
    parser.add_argument("--scene", choices=tuple(SCENES), default="showcase")
    parser.add_argument("--mode", choices=("orbit", "temporal"),
                        default="orbit",
                        help="orbit: one run per frame with yaw sweep; "
                             "temporal: single live run, 12 consecutive "
                             "frames via GX_BURST (fixed camera)")
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--yaw-start", type=float, default=0.0)
    parser.add_argument("--yaw-end", type=float, default=360.0)
    parser.add_argument("--pitch", type=float, default=25.0)
    parser.add_argument("--distance", type=float, default=None)
    parser.add_argument("--shot-frame", type=int, default=60)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--fps", type=float, default=10.0)
    parser.add_argument("--width", type=int, default=768,
                        help="output width, aspect kept (0 keeps native)")
    parser.add_argument("--loop", type=int, default=0,
                        help="GIF loop count, 0=infinite")
    parser.add_argument("--format", choices=("gif", "webp"), default="gif")
    parser.add_argument("--quality", type=int, default=85,
                        help="webp quality (ignored for gif)")
    parser.add_argument("--strip", action="store_true",
                        help="apply ssr_lab isolation env (no bloom/TAA/fog/...)")
    parser.add_argument("--env", action="append", default=[],
                        metavar="KEY=VALUE", help="extra env override, repeatable")
    parser.add_argument("--binary", default="./build/gX_build")
    parser.add_argument("--output", default="/tmp/gfx_gif/showcase.gif")
    args = parser.parse_args()

    if args.frames < 1:
        parser.error("--frames must be >= 1")
    if args.fps <= 0:
        parser.error("--fps must be > 0")

    try:
        extra_env = dict(parse_env(spec) for spec in args.env)
    except ValueError as error:
        parser.error(str(error))

    distance = args.distance
    if distance is None:
        distance = 6.0 if args.scene in ("reflectrow", "minimal") else 9.0

    output = args.output
    stem, _ = os.path.splitext(output)
    frames_dir = stem + "_frames"
    raw_dir = os.path.join(frames_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)

    scene_flag = SCENES[args.scene]
    base_env = audit.scene_env(clean=True)
    if args.strip:
        base_env.update(COMMON_ENV)
    base_env.update(extra_env)
    base_env["GX_SCREENSHOT_DIR"] = raw_dir
    base_env["GX_SHOT_FRAME"] = str(args.shot_frame)
    base_env["GX_CAM_PITCH"] = str(args.pitch)
    base_env["GX_CAM_DIST"] = str(distance)

    paths = []
    if args.mode == "temporal":
        #opencode: single live run; the engine writes burst_000..011 into
        #   the screenshot dir and exits. Fixed camera at --yaw-start.
        if args.frames != BURST_FRAMES:
            print(f"note: temporal mode always yields {BURST_FRAMES} "
                  f"frames (ignoring --frames {args.frames})")
        env = dict(base_env)
        env["GX_CAM_YAW"] = str(args.yaw_start)
        env["GX_BURST"] = "1"
        before = {name for name in os.listdir(raw_dir)}
        result = audit.run_scene(
            scene_flag, env, binary=args.binary, width=None, height=None,
            fps=None, screenshot_dir=raw_dir, shot_frame=args.shot_frame,
            screenshot_after=True, timeout=args.timeout, check=False)
        if result.returncode != 0:
            raise RuntimeError(
                f"capture exited with status {result.returncode}")
        fresh = sorted(name for name in os.listdir(raw_dir)
                       if name.startswith("burst_") and name not in before)
        if len(fresh) != BURST_FRAMES:
            raise RuntimeError(
                f"expected {BURST_FRAMES} burst frames, got {len(fresh)}")
        for index, name in enumerate(fresh):
            output_path = os.path.join(frames_dir, f"frame_{index:03d}.png")
            shutil.copyfile(os.path.join(raw_dir, name), output_path)
            paths.append(output_path)
        print(f"captured {len(paths)} burst frames from one live run",
              flush=True)
    else:
        for index in range(args.frames):
            frac = index / (args.frames - 1) if args.frames > 1 else 0.0
            yaw = args.yaw_start + (args.yaw_end - args.yaw_start) * frac
            env = dict(base_env)
            env["GX_CAM_YAW"] = str(yaw)
            tag = f"frame_{index:03d}_yaw{yaw:07.2f}.png"
            output_path = os.path.join(frames_dir, tag)
            print(f"[{index + 1}/{args.frames}] yaw={yaw:.1f} -> {tag}",
                  flush=True)
            capture(args.binary, scene_flag, env, output_path, args.timeout)
            paths.append(output_path)

    frames = [Image.open(path).convert("RGB") for path in paths]
    if args.width > 0 and frames[0].width != args.width:
        height = round(frames[0].height * args.width / frames[0].width)
        frames = [frame.resize((args.width, height), Image.LANCZOS)
                  for frame in frames]
    duration_ms = int(round(1000.0 / args.fps))
    save_kwargs = {"save_all": True, "append_images": frames[1:],
                   "duration": duration_ms, "loop": args.loop}
    if args.format == "webp":
        save_kwargs["quality"] = args.quality
    frames[0].save(output, **save_kwargs)

    check = Image.open(output)
    print(f"wrote {output} ({check.width}x{check.height}, "
          f"frames={getattr(check, 'n_frames', 1)}, "
          f"{os.path.getsize(output) // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
