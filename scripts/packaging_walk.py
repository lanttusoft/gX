#!/usr/bin/env python3
#opencode: packaging walk — fresh clone + submodule + build + tests + smoke.
#   The raylib submodule is sourced from the local worktree so the walk works
#   offline; the point is that the clone shape (engine under master/, proofs
#   beside it) actually builds and runs, which `git archive` alone cannot
#   prove. This is the RC ledger's packaging sanity item.

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent      # engine dir (master/)
REPO = ROOT.parent                                  # repo root
RAYLIB = ROOT / "external" / "raylib"               # local submodule worktree
SUBMODULE = "master/external/raylib"


def run(command, cwd, env=None, timeout=1800):
    started = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, text=True, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return result, time.perf_counter() - started


def main():
    if not (REPO / ".git").exists() or not (RAYLIB / ".git").exists():
        print("PACKAGING_WALK: missing repo or raylib worktree", file=sys.stderr)
        return 1
    started = time.perf_counter()
    try:
        with tempfile.TemporaryDirectory(prefix="packaging_walk_",
                                         dir="/tmp/opencode") as temporary:
            clone = Path(temporary) / "gx"
            result, _ = run(["git", "clone", "--no-local", "--quiet",
                             str(REPO), str(clone)], REPO)
            if result.returncode != 0:
                raise RuntimeError(f"clone failed: {result.stdout[-300:]}")
            #opencode: protocol.file.allow — git's hardening blocks file
            #   transports for submodule clones; ours is a local path we own
            result, _ = run(["git", "-c", "protocol.file.allow=always", "-c",
                    f"submodule.{SUBMODULE}.url={RAYLIB}",
                    "submodule", "update", "--init", "--quiet", SUBMODULE],
                clone)
            if result.returncode != 0:
                raise RuntimeError(
                    f"submodule update failed: {result.stdout[-300:]}")
            engine = clone / "master"
            result, build_seconds = run(["bash", "build.sh"], engine)
            if result.returncode != 0:
                raise RuntimeError(f"build failed: {result.stdout[-400:]}")
            binary = engine / "build" / "gX_build"
            if not binary.is_file():
                raise RuntimeError("build produced no binary")
            result, _ = run([str(binary), "--test-only"], engine, timeout=600)
            if "TEST_ONLY: 0 errors" not in result.stdout:
                raise RuntimeError(
                    f"unit tests failed: {result.stdout[-300:]}")
            env = dict(os.environ)
            env.update({"GX_FIXED_DT": "0.0166666667", "GX_FRAMES": "30",
                        "GX_NO_SETTINGS_SAVE": "1"})
            env.setdefault("DISPLAY", ":1")
            result, _ = run([str(binary), "--gfx"], engine, env=env, timeout=180)
            if result.returncode != 0:
                raise RuntimeError(f"smoke failed: {result.stdout[-300:]}")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"PACKAGING_WALK: FAIL — {error}", file=sys.stderr)
        return 1
    elapsed = time.perf_counter() - started
    print(f"PACKAGING_WALK: PASS fresh clone built and ran "
          f"(build {build_seconds:.0f}s, total {elapsed:.0f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
