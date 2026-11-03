#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: release-build reproducibility gate. Two clean source archives of the same
#   commit are built in different paths with a fixed SOURCE_DATE_EPOCH; the
#   Release executable and the source archive bytes must match. This catches
#   __DATE__/__TIME__, absolute __FILE__ paths and build-directory leakage.
"""Build a commit twice from clean archives and compare the Release output."""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORK = ROOT / "build" / "reproducible-check"
BINARY = "build/gX_build"
ARTIFACTS = (BINARY, "build/libgx_engine_core.so")


def run(command, cwd, env, log):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.append("$ " + " ".join(command))
    log.append(result.stdout)
    if result.returncode != 0:
        raise RuntimeError("%s failed (exit %d)" % (command[0],
                                                   result.returncode))
    return result


def commit_epoch(commit):
    result = subprocess.run(["git", "-C", str(ROOT), "log", "-1",
                             "--format=%ct", commit], check=True,
                            stdout=subprocess.PIPE, text=True)
    return int(result.stdout.strip())


def extract(commit, destination):
    result = subprocess.run(["git", "-C", str(ROOT), "archive",
                             "--format=tar", commit], check=True,
                            stdout=subprocess.PIPE)
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
        try:
            archive.extractall(destination, filter="data")
        except TypeError:  # pragma: no cover - Python < 3.12
            archive.extractall(destination)
    return result.stdout


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def build_tree(tree, env, log):
    run(["./build.sh", "Release"], tree, env, log)
    artifacts = {}
    for relative in ARTIFACTS:
        path = tree / relative
        if path.is_file():
            artifacts[relative] = digest(path)
    if BINARY not in artifacts:
        raise RuntimeError("missing " + BINARY)
    return artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--keep", action="store_true",
                        help="keep the two extracted trees")
    args = parser.parse_args()

    work = args.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    epoch = commit_epoch(args.commit)
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = str(epoch)
    log = []

    archives = []
    artifacts = []
    for name in ("tree-a", "tree-b"):
        destination = work / name
        archives.append(extract(args.commit, destination))
        artifacts.append(build_tree(destination, env, log))

    archive_ok = archives[0] == archives[1]
    differences = []
    for relative in sorted(set(artifacts[0]) | set(artifacts[1])):
        left = artifacts[0].get(relative)
        right = artifacts[1].get(relative)
        if left != right:
            differences.append("%s: %s != %s" % (relative, left, right))

    (work / "reproducible-check.log").write_text("\n".join(log),
                                                 encoding="utf-8")
    print("REPRODUCIBLE_BUILD: commit=%s epoch=%d" % (args.commit, epoch))
    print("  archive: %s" % ("identical" if archive_ok
                             else "DIFFERENT bytes"))
    for relative in sorted(artifacts[0]):
        print("  %s: %s" % (relative, artifacts[0][relative]))
    if differences:
        for difference in differences:
            print("  FAIL " + difference)
        print("REPRODUCIBLE_BUILD: FAIL")
        return 1
    if not args.keep:
        shutil.rmtree(work)
    else:
        print("  trees kept under %s" % work)
    print("REPRODUCIBLE_BUILD: PASS — two clean trees produced identical "
          "Release bytes")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("REPRODUCIBLE_BUILD: FAIL — %s" % error)
        sys.exit(1)
