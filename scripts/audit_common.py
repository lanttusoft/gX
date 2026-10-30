#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: shared helpers for the gX GPU audit scripts — settings safety,
#   deterministic scene runs, capture loading, image metrics, and a parallel
#   capture pool with private per-probe directories and optional caching.
#
#   Usage (typical):
#       from audit_common import (BIN, CapturePool, diff_frac, mean_luma,
#                                 run_scene, settings_preserved, strip_ansi)
#
#   Everything here is GPU-display dependent (DISPLAY, default :1) and must be
#   called under `make <gate>` or with DISPLAY exported, exactly like the
#   existing per-effect checks.

import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import numpy as np

import audit_cache

#opencode: gates whose assertions depend on the engine's own window geometry
#   (absolute pixel bands, picked targets, camera crops) call
#   keep_engine_size() once so the smaller default audit window does not
#   apply to them.
_keep_engine_size = False


def keep_engine_size():
    global _keep_engine_size
    _keep_engine_size = True
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
#ai: shared scratch root — any harness may override with GX_SCRATCH; the
#   legacy default stays for compatibility.
SCRATCH = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
LUMA_WEIGHTS = np.array([0.299, 0.587, 0.114], dtype=np.float32)

DEFAULT_TIMEOUT = 120
MATRIX_WIDTH = 1024
MATRIX_HEIGHT = 768
MATRIX_FPS = 60
DEFAULT_SHOT_FRAME = 120

#opencode: ambient test hooks a probe must be able to override authoritatively.
HOOK_PREFIXES = ("GX_T_", "GX_CAM_", "GX_FOG_", "GX_SNOW_", "GX_SNOWFALL_",
                 "GX_RAIN_", "GX_FOGSPRITE_", "GX_SPLAT_", "GX_MIRROR_",
                 "GX_PROBE_", "GX_EXTRA_", "GX_LAYOUT_", "GX_RAYMARCH_")
HOOK_KEYS = ("GX_SCREENSHOT_DIR", "GX_SHOT_FRAME", "GX_SHOT_FORCE", "GX_FRAMES",
              "GX_FIXED_DT", "GX_SKY_CHECK", "GX_GFX_RAYMARCH_DEPTH",
              "GX_REDUCE_MOTION")


def display():
    return os.environ.get("DISPLAY", ":1")


def strip_hooks(environment):
    """Remove ambient test hooks so a probe's own overrides are authoritative."""
    for key in list(environment):
        if key.startswith(HOOK_PREFIXES) or key in HOOK_KEYS:
            environment.pop(key, None)
    return environment


def strip_ansi(text):
    return ANSI_RE.sub("", text)


#opencode: scene viewport from the editor's GX_DOCK_TRACE line, clamped to the
#   capture — gates compare scene pixels without the panels (whose text varies
#   with the run).
DOCK_RE = re.compile(r"GFX_DOCK:.*viewport=\((\d+),(\d+),(\d+),(\d+)\)")


def dock_viewport(log, width, height):
    """Last traced dock viewport, or the full capture when untraced."""
    match = None
    for candidate in DOCK_RE.findall(log or ""):
        match = candidate
    if not match:
        return (0, 0, width, height)
    x, y, w, h = (int(value) for value in match)
    x = max(0, min(x, width - 1))
    y = max(0, min(y, height - 1))
    w = max(1, min(w, width - x))
    h = max(1, min(h, height - y))
    return (x, y, w, h)


@contextlib.contextmanager
def settings_preserved():
    """Restore resources/settings.json when a probe rewrites it anyway."""
    settings = ROOT / "resources" / "settings.json"
    previous = settings.read_bytes() if settings.exists() else None
    try:
        yield
    finally:
        if previous is not None:
            if not settings.exists() or settings.read_bytes() != previous:
                settings.write_bytes(previous)
        elif settings.exists():
            settings.unlink()


def with_settings_preserved(callback):
    """Callback form kept for the existing per-effect checks."""
    with settings_preserved():
        callback()


def scene_env(env=None, clean=False, **extra):
    """Environment for one deterministic scene run (user settings protected).

    Audit runs default to an unmapped window (GX_AUDIT_WINDOW=hidden) so
    nothing appears on the desktop and the pointer is never grabbed; set
    GX_AUDIT_ONSCREEN=1 to opt back into a normal window for debugging.
    clean=True strips ambient test hooks first so the probe's overrides win.
    """
    result = dict(os.environ)
    if clean:
        strip_hooks(result)
    result["GX_NO_SETTINGS_SAVE"] = "1"
    #ai: audits keep the deterministic demo scene; the composed editor
    #    showcase is a manual/authoring default, not a gate fixture.
    result.setdefault("GX_EDITOR_DEFAULT_SCENE", "0")
    result["DISPLAY"] = display()
    if os.environ.get("GX_AUDIT_ONSCREEN") != "1":
        result.setdefault("GX_AUDIT_WINDOW", "hidden")
    for source in (env, extra):
        for key, value in (source or {}).items():
            if value is not None:
                result[key] = str(value)
    return result


def run_scene(scene, env=None, *, binary=None, width=MATRIX_WIDTH,
              height=MATRIX_HEIGHT,
              fps=MATRIX_FPS, frames=None, shot_frame=DEFAULT_SHOT_FRAME,
              screenshot_dir=None, timeout=DEFAULT_TIMEOUT, check=True,
              extra_args=(), screenshot_after=False):
    """Run the engine once; capture a screenshot when screenshot_dir is given.

    binary overrides the default build for checks that use an isolated
    configuration (e.g. build/gl-level33/gX_build).
    screenshot_after=True switches to --ai-screenshot-after (required by the
    GX_BURST multi-frame capture path) instead of --ai-screenshot-final.
    """
    environment = scene_env(env)
    if frames is not None:
        environment["GX_FRAMES"] = str(frames)
    command = [str(binary or BIN), scene]
    #opencode: None sizes keep the engine's own window defaults unless the
    #   audit-size override is active. Audits default to a smaller hidden
    #   window (much less desktop lag, faster frames); checks that need
    #   absolute pixel bands pass explicit sizes or set GX_AUDIT_FULLSIZE=1.
    if width is not None and height is not None:
        command += ["--screenWidth", str(width), "--screenHeight", str(height)]
    elif not _keep_engine_size and environment.get("GX_AUDIT_FULLSIZE") != "1":
        command += ["--screenWidth",
                    environment.get("GX_AUDIT_WIDTH", "1024"),
                    "--screenHeight",
                    environment.get("GX_AUDIT_HEIGHT", "768")]
    if fps:
        command += ["--targetFPS", str(fps)]
    if screenshot_dir is not None:
        environment["GX_SCREENSHOT_DIR"] = str(screenshot_dir)
        #opencode: None keeps whatever GX_SHOT_FRAME the probe env (or the
        #   engine default) already specifies.
        if shot_frame is not None:
            environment["GX_SHOT_FRAME"] = str(shot_frame)
        if screenshot_after:
            command += ["--ai-screenshot-after", str(shot_frame)]
        else:
            command.append("--ai-screenshot-final")
    command += list(extra_args)
    result = subprocess.run(
        command, cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(f"{scene} exited {result.returncode}\n"
                           f"{strip_ansi(result.stdout)[-2000:]}")
    return result


def newest_shot(directory):
    shots = list(Path(directory).glob("gXss_*.png"))
    return max(shots, key=lambda path: path.stat().st_mtime) if shots else None


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float32)


def luma(image):
    return (image * LUMA_WEIGHTS).sum(axis=2)


def diff_frac(a, b, cut=10.0):
    """Fraction of pixels whose mean channel delta exceeds cut (0..255)."""
    return float((np.abs(a - b).mean(axis=2) > cut).mean())


def mean_luma(image):
    return float(luma(image).mean())


def mean_abs_diff(a, b):
    """Mean absolute RGB delta in 0..255 units (pairwise, same shape)."""
    return float(np.abs(a.astype(np.float32) - b.astype(np.float32)).mean())


def blown_fraction(image, cut=250.0):
    """Fraction of pixels above the blow-out cut on any channel."""
    return float((image > cut).mean())


def black_fraction(image, cut=8.0):
    return float((image.mean(axis=2) < cut).mean())


def magenta_fraction(image):
    #opencode: bloom/composite shaders write a magenta error sentinel when a
    #   pass fails to assign finalColor; any surviving sentinel is a defect.
    red, green, blue = image[..., 0], image[..., 1], image[..., 2]
    return float(((red > 200) & (green < 60) & (blue > 200)).mean())


def chroma_spread(image):
    """Mean (max channel - min channel); 0 = grayscale, high = saturated."""
    return float((image.max(axis=2) - image.min(axis=2)).mean())


def luma_std(image):
    return float(luma(image).std())


def edge_center_ratio(image, border=0.12):
    """Border-ring luma over center luma; vignette darkens the ring."""
    height, width = image.shape[:2]
    values = luma(image)
    bx, by = int(width * border), int(height * border)
    ring = np.concatenate([values[:by, :].ravel(), values[-by:, :].ravel(),
                           values[:, :bx].ravel(), values[:, -bx:].ravel()])
    center = values[by:-by, bx:-bx]
    center_mean = float(center.mean()) if center.size else 1.0
    return float(ring.mean()) / max(center_mean, 1e-4)


#ai: fingerprint the selected binary, including its identity, for isolated profiles.
def _fingerprint(scene, env, width, height, fps, shot_frame, extra_args=(), binary=None):
    selected_binary = Path(binary or BIN).resolve()
    #ai: source-input identity, not the relinked binary's mtime/size. The
    #    sibling CMakeCache is included for isolated-build binaries.
    inputs = audit_cache.build_input_digest(binary=selected_binary)
    payload = {
        "scene": scene,
        "env": sorted((key, str(value)) for key, value in (env or {}).items()
                      if key not in ("GX_SCREENSHOT_DIR", "GX_SHOT_FRAME")),
        "width": width, "height": height, "fps": fps,
        "shot_frame": shot_frame,
        "args": list(extra_args),
        "inputs": inputs,
        "binary_path": str(selected_binary),
    }
    digest = hashlib.sha1(json.dumps(payload, sort_keys=True).encode()).hexdigest()
    return digest[:16]


class CapturePool:
    """Deterministic scene captures in parallel, one private dir per probe.

    cache=True reuses a capture only when the binary fingerprint and the full
    request (scene + env + geometry) match — safe for local iteration, and
    opt-in because it can hide driver-side regressions in CI.
    """

    #ai: keep profile selection local to the pool rather than changing module globals.
    def __init__(self, jobs=4, raw_dir=None, timeout=DEFAULT_TIMEOUT, cache=False,
                 binary=None):
        self.jobs = max(1, int(jobs))
        self.raw_dir = Path(raw_dir or (SCRATCH / "audit_raw"))
        self.raw_dir.mkdir(parents=True, exist_ok=True)
        self.timeout = timeout
        self.cache = cache
        self.binary = Path(binary) if binary is not None else None

    def _cached(self, key, fingerprint):
        sidecar = self.raw_dir / f"{key}.json"
        shot = self.raw_dir / f"{key}.png"
        if not (self.cache and shot.is_file() and sidecar.is_file()):
            return None
        try:
            if json.loads(sidecar.read_text())["fingerprint"] == fingerprint:
                return shot
        except (OSError, ValueError, KeyError):
            pass
        return None

    def _capture(self, key, scene, env, width, height, fps, shot_frame, retries,
                 extra_args=()):
        fingerprint = _fingerprint(scene, env, width, height, fps, shot_frame,
                                   extra_args, self.binary)
        cached = self._cached(key, fingerprint)
        if cached is not None:
            return cached
        destination = self.raw_dir / f"{key}.png"
        for _ in range(retries + 1):
            private = Path(tempfile.mkdtemp(prefix=f"{key}_", dir=self.raw_dir))
            try:
                run_scene(scene, env, binary=self.binary, width=width, height=height, fps=fps,
                          shot_frame=shot_frame, screenshot_dir=private,
                          timeout=self.timeout, extra_args=extra_args)
                shot = newest_shot(private)
                if shot:
                    shutil.copyfile(shot, destination)
                    (self.raw_dir / f"{key}.json").write_text(
                        json.dumps({"fingerprint": fingerprint}))
                    return destination
            except (subprocess.SubprocessError, OSError):
                continue
            finally:
                shutil.rmtree(private, ignore_errors=True)
        return None

    def capture_all(self, requests, *, width=MATRIX_WIDTH, height=MATRIX_HEIGHT,
                    fps=MATRIX_FPS, shot_frame=DEFAULT_SHOT_FRAME, retries=1):
        """requests: {key: (scene, env[, extra_args])} -> {key: Path}.

        Captures run in parallel first; any stragglers are retried one at a
        time, because GPU context contention makes a few parallel launches
        fail intermittently while serial runs are reliable.
        """
        safe = {}
        for key, request in requests.items():
            scene, env = request[0], request[1]
            extra_args = request[2] if len(request) > 2 else ()
            safe[re.sub(r"[^A-Za-z0-9_.-]", "_", key)] = (scene, env, extra_args)
        results = {}
        with ThreadPoolExecutor(max_workers=self.jobs) as executor:
            futures = {
                executor.submit(self._capture, key, scene, env, width, height,
                                fps, shot_frame, retries, extra_args): key
                for key, (scene, env, extra_args) in safe.items()
            }
            for future in as_completed(futures):
                key = futures[future]
                try:
                    path = future.result()
                except Exception:
                    path = None
                if path is not None:
                    results[key] = path
        for key, (scene, env, extra_args) in safe.items():
            if key in results:
                continue
            path = self._capture(key, scene, env, width, height, fps,
                                 shot_frame, retries + 1, extra_args)
            if path is not None:
                results[key] = path
        return results
