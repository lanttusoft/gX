#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: audit result cache — fingerprints the build INPUTS, not the linked binary.
#   A normal relink no longer invalidates unchanged gate work, while a source,
#   resource, helper, flag or build-profile change still does. Only passing
#   results are stored; failures always re-run. Bump CACHE_VERSION on any
#   key-format change (old entries are ignored by load()).
#
#   Usage:
#     python3 scripts/audit_cache.py --show
#     python3 scripts/audit_cache.py --clear

import argparse
import contextlib
import hashlib
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CACHE_VERSION = 4
CACHE_PATH = ROOT / "build" / ".audit_cache.json"

#ai: process-local memo — audit runs hash hundreds of requests but the inputs
#   do not change during one run. Tests pass use_cache=False.
_INPUT_CACHE = {}
_HARDWARE_DIGEST = None

PROFILE_ENV_KEYS = (
    "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS", "CPPFLAGS",
    "CMAKE_BUILD_TYPE", "GX_BUILD_TYPE", "GX_USE_ANGELSCRIPT",
)

#ai: gate results may depend on these environment knobs; hash them into the
#   key without hashing unrelated or secret-bearing process state. Scheduler
#   controls (AUDIT_CACHE/AUDIT_JOBS) do not change a gate's verdict.
GATE_ENV_KEYS = (
    "DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE", "TZ", "LANG",
    "LC_ALL", "LC_CTYPE", "PYTHONHASHSEED", "PYTHONPATH", "VIRTUAL_ENV",
)
GATE_ENV_PREFIXES = (
    "GX_", "AUDIT_", "LIBGL_", "MESA_", "__GLX", "__EGL", "NVIDIA_", "AMD_",
)
GATE_ENV_IGNORE = (
    "AUDIT_CACHE", "AUDIT_CACHE_DIR", "AUDIT_JOBS", "GX_SCREENSHOT_DIR",
    "GX_SHOT_FRAME",
)


def cache_path():
    """Return the cache file; AUDIT_CACHE_DIR opts into a shared directory."""
    directory = os.environ.get("AUDIT_CACHE_DIR")
    if directory:
        return Path(directory).expanduser().resolve() / ".audit_cache.json"
    return CACHE_PATH


def _hash_file(digest, path):
    try:
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                digest.update(chunk)
    except OSError:
        digest.update(b"<missing>")
    digest.update(b"\0")


def _hash_tree(digest, root, relative):
    """Hash every regular file under root/relative by content and path.

    Content hashing keeps the key stable across checkouts/relinks; the path is
    included so renames still invalidate. Generated Python/compiler caches are
    excluded because they are not build inputs.
    """
    base = root / relative
    if not base.exists():
        digest.update(f"<missing:{relative}>\0".encode())
        return
    if base.is_file():
        digest.update(relative.encode())
        digest.update(b"\0")
        _hash_file(digest, base)
        return
    for path in sorted(base.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        if "__pycache__" in rel.parts or rel.name.endswith(".pyc"):
            continue
        digest.update(str(rel).encode())
        digest.update(b"\0")
        _hash_file(digest, path)


def _profile_paths(root, binary=None):
    paths = [root / "build" / "CMakeCache.txt"]
    if binary:
        binary_path = Path(binary).resolve()
        for parent in (binary_path.parent, binary_path.parent.parent):
            candidate = parent / "CMakeCache.txt"
            if candidate.is_file() and candidate not in paths:
                paths.append(candidate)
    return paths


def _hash_profile(digest, root, binary=None):
    """Build type/compiler/flags live in CMakeCache.txt (generated, stable)."""
    for profile in _profile_paths(root, binary):
        try:
            rel = profile.relative_to(root)
        except ValueError:
            rel = profile
        digest.update(f"profile:{rel}\0".encode())
        _hash_file(digest, profile)
    for key in PROFILE_ENV_KEYS:
        digest.update(f"env:{key}={os.environ.get(key, '')}\0".encode())
    digest.update(
        f"platform:{platform.system()}:{platform.machine()}\0".encode())


def build_input_digest(root=None, binary=None, use_cache=True):
    """Deterministic digest of everything that determines a gate's build.

    Inputs: src/, resources/, scripts/, cmake/, the top-level build files,
    the frozen raylib headers, the selected CMake profile (plus a custom
    binary's sibling profile), and toolchain/build environment identity.
    """
    root = Path(root or ROOT).resolve()
    binary_path = str(Path(binary).resolve()) if binary else ""
    cache_key = (str(root), binary_path)
    if use_cache and cache_key in _INPUT_CACHE:
        return _INPUT_CACHE[cache_key]

    digest = hashlib.sha256()
    digest.update(f"gx-audit-inputs-v{CACHE_VERSION}\0".encode())
    for relative in ("src", "resources", "scripts", "cmake"):
        _hash_tree(digest, root, relative)
    for relative in ("CMakeLists.txt", "build.sh", "Makefile", ".gitmodules"):
        digest.update(relative.encode())
        digest.update(b"\0")
        _hash_file(digest, root / relative)
    #opencode: the frozen raylib API is a build input even though it lives in a
    #   submodule; hashing the public headers catches a version/API change.
    for relative in ("external/raylib/src/raylib.h", "external/raylib/src/rlgl.h"):
        digest.update(relative.encode())
        digest.update(b"\0")
        _hash_file(digest, root / relative)
    _hash_profile(digest, root, binary if binary else None)

    value = digest.hexdigest()
    if use_cache:
        _INPUT_CACHE[cache_key] = value
    return value


def base_digest():
    """Backward-compatible name used by audit_summary.py."""
    return build_input_digest()


def _file_digest(path):
    digest = hashlib.sha256()
    _hash_file(digest, Path(path))
    return digest.hexdigest()


def _probe(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=5)
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout.strip()


def environment_digest(environment=None):
    """Hash only gate-relevant environment state, never the whole process env."""
    env = os.environ if environment is None else environment
    digest = hashlib.sha256()
    for key in sorted(env):
        if key in GATE_ENV_IGNORE:
            continue
        if key in GATE_ENV_KEYS or key.startswith(GATE_ENV_PREFIXES):
            digest.update(f"{key}={env[key]}\0".encode())
    return digest.hexdigest()


def _gpu_identity():
    """Stable GPU/driver identity; live memory counters must not enter the key."""
    probe = _probe(("nvidia-smi", "--query-gpu=name,driver_version",
                    "--format=csv,noheader"))
    if probe:
        return probe
    probe = _probe(("glxinfo", "-B"))
    keep = []
    for line in probe.splitlines():
        if line.startswith(("OpenGL vendor string:", "OpenGL renderer string:",
                            "OpenGL version string:",
                            "OpenGL core profile version string:",
                            "OpenGL core profile shading language version string:")):
            keep.append(line.strip())
    return "\n".join(keep)


def hardware_digest():
    """Best-effort GPU/driver/runtime identity for rendering gate results."""
    global _HARDWARE_DIGEST
    if _HARDWARE_DIGEST is not None:
        return _HARDWARE_DIGEST
    digest = hashlib.sha256()
    digest.update(f"platform:{platform.system()}:{platform.release()}:"
                  f"{platform.machine()}\0".encode())
    digest.update(f"python:{sys.version}\0".encode())
    try:
        import numpy
        digest.update(f"numpy:{numpy.__version__}\0".encode())
    except Exception:
        pass
    probe = _gpu_identity()
    if probe:
        digest.update(b"gpu:\0" + probe.encode() + b"\0")
    _HARDWARE_DIGEST = digest.hexdigest()
    return _HARDWARE_DIGEST


def gate_parts(base, script_path, args=(), environment=None):
    """Return the independently comparable inputs behind one gate key."""
    return {
        "base": str(base),
        "script": _file_digest(script_path),
        "args": hashlib.sha256(
            json.dumps(list(args), sort_keys=True).encode()).hexdigest(),
        "env": environment_digest(environment),
        "hardware": hardware_digest(),
    }


def key_from_parts(parts):
    digest = hashlib.sha256()
    for name in ("base", "script", "args", "env", "hardware"):
        digest.update(f"{name}:{parts[name]}\0".encode())
    return digest.hexdigest()


def gate_key(base, script_path, args=(), environment=None):
    return key_from_parts(gate_parts(base, script_path, args, environment))


def _gate_entries(cache, name):
    entries = cache.get("gates", {}).get(name, {})
    if isinstance(entries, dict) and "key" in entries and "ok" in entries:
        return {entries.get("key"): entries}
    return entries if isinstance(entries, dict) else {}


def explain(cache, name, parts):
    """Return a short reason a gate will run or reuse its cached pass."""
    key = key_from_parts(parts)
    entries = _gate_entries(cache, name)
    entry = entries.get(key)
    if not entry:
        if not entries:
            return "no cached pass"
        entry = max(entries.values(), key=lambda item: item.get("when", ""))
        stored = entry.get("parts") or {}
        for part in ("base", "script", "args", "env", "hardware"):
            if stored.get(part) != parts.get(part):
                return f"{part} changed"
        return "no cached pass for this key"
    if not entry.get("ok"):
        return "cached entry is not a pass"
    stored = entry.get("parts") or {}
    for part in ("base", "script", "args", "env", "hardware"):
        if stored.get(part) != parts.get(part):
            return f"{part} changed"
    return "fresh"


def _empty_cache():
    return {"version": CACHE_VERSION, "gates": {}}


def _lock_path(path):
    return path.with_suffix(path.suffix + ".lock")


@contextlib.contextmanager
def _cache_lock(path):
    """Best-effort process lock for shared-cache merge/write."""
    lock_path = _lock_path(path)
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        import fcntl
        with open(lock_path, "w") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            yield
    except (ImportError, OSError):
        yield


def _merge_cache(target, source):
    for name, entries in source.get("gates", {}).items():
        if not isinstance(entries, dict):
            continue
        bucket = target.setdefault("gates", {}).setdefault(name, {})
        for key, entry in entries.items():
            if isinstance(entry, dict):
                bucket[key] = entry


def load():
    path = cache_path()
    try:
        data = json.loads(path.read_text())
        if data.get("version") != CACHE_VERSION:
            return _empty_cache()
        if not isinstance(data.get("gates"), dict):
            return _empty_cache()
        return data
    except (OSError, ValueError):
        return _empty_cache()


def save(data):
    path = cache_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    with _cache_lock(path):
        disk = load()
        _merge_cache(disk, data)
        disk["version"] = CACHE_VERSION
        temp = path.with_name(path.name + f".tmp.{os.getpid()}")
        temp.write_text(json.dumps(disk, indent=2, sort_keys=True))
        os.replace(temp, path)


def clear():
    path = cache_path()
    for candidate in (path, _lock_path(path)):
        try:
            candidate.unlink()
        except OSError:
            pass
    if path.parent.is_dir():
        for temp in path.parent.glob(path.name + ".tmp.*"):
            try:
                temp.unlink()
            except OSError:
                pass


def lookup(cache, name, key):
    entry = _gate_entries(cache, name).get(key)
    if not entry or not entry.get("ok"):
        return None
    return entry


def store(cache, name, key, seconds, verdict, script, parts=None):
    entry = {
        "key": key,
        "ok": True,
        "seconds": round(seconds, 2),
        "verdict": verdict,
        "script": script,
        "when": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    if parts is not None:
        entry["parts"] = parts
    cache.setdefault("gates", {}).setdefault(name, {})[key] = entry


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--show", action="store_true")
    parser.add_argument("--clear", action="store_true")
    arguments = parser.parse_args()
    if arguments.clear:
        clear()
        print("audit cache cleared")
        return 0
    if arguments.show:
        cache = load()
        base = base_digest()
        env = environment_digest()
        hardware = hardware_digest()
        gates = cache.get("gates", {})
        print(f"cache: {cache_path()}")
        total = 0
        current = 0
        for name in sorted(gates):
            for key, entry in sorted(_gate_entries(cache, name).items()):
                total += 1
                script = ROOT / "scripts" / entry.get("script", "")
                parts = entry.get("parts") or {}
                reason = ""
                if not parts:
                    reason = "no parts"
                elif parts.get("base") != base:
                    reason = "base changed"
                elif (not script.is_file()
                      or parts.get("script") != _file_digest(script)):
                    reason = "script changed"
                elif parts.get("env") != env:
                    reason = "env changed"
                elif parts.get("hardware") != hardware:
                    reason = "hardware changed"
                fresh = not reason
                current += 1 if fresh else 0
                status = "fresh" if fresh else f"stale ({reason})"
                print(f"  {name:20s} {key[:10]} {status:20s} "
                      f"{entry.get('seconds', 0):6.1f}s  {entry.get('when', '')}")
        print(f"audit cache: {current}/{total} fresh")
        return 0
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
