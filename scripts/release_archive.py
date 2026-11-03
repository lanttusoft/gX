#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic release archive helper. `git archive` already pins entry
#   order and mtimes; this wrapper adds the version-prefixed root, the sha256
#   sidecar and optional GPG detached signatures, and can self-check the
#   archive by building it twice and extracting one copy.
"""Create, hash and optionally sign the gX source release archive."""

import argparse
import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

import version_check

ROOT = Path(__file__).resolve().parents[1]

def run(command, cwd=ROOT):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise RuntimeError("%s failed (exit %d)\n%s"
                           % (" ".join(command), result.returncode,
                              result.stdout))
    return result.stdout


def version_label():
    parsed = version_check.readme_version()
    if not parsed:
        raise RuntimeError("cannot read the version from readme.txt")
    major, minor, patch, variant = parsed
    label = "%d.%d.%d" % (major, minor, patch)
    return "v" + label + ("_" + variant if variant else "")


def ensure_clean():
    status = run(["git", "status", "--porcelain", "--untracked-files=no"])
    if status.strip():
        raise RuntimeError("worktree has tracked changes; commit or stash first")


def archive_bytes(commit, label):
    prefix = "gX-%s/" % label
    result = subprocess.run(
        ["git", "-C", str(ROOT), "archive", "--format=tar.gz",
         "--prefix=" + prefix, commit],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode(errors="replace").strip()
                           or "git archive failed")
    return result.stdout, prefix


def sidecar_text(name, digest):
    return "%s  %s\n" % (digest, name)


def write_outputs(data, out, label):
    out.mkdir(parents=True, exist_ok=True)
    name = "gX-%s.tar.gz" % label
    archive = out / name
    archive.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    sidecar = out / (name + ".sha256")
    sidecar.write_text(sidecar_text(name, digest), encoding="utf-8")
    return archive, sidecar, digest


def sign(path, key):
    command = ["gpg", "--batch", "--yes", "--armor", "--detach-sign",
               "--output", str(path) + ".asc"]
    if key:
        command += ["--local-user", key]
    command.append(str(path))
    run(command)
    return Path(str(path) + ".asc")


def members(data):
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        return archive.getnames()


def extract(data, destination):
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        try:
            archive.extractall(destination, filter="data")
        except TypeError:  # pragma: no cover - Python < 3.12
            archive.extractall(destination)


def check(commit, out):
    label = version_label()
    first, prefix = archive_bytes(commit, label)
    second, _ = archive_bytes(commit, label)
    if first != second:
        raise RuntimeError("two git archive runs produced different bytes")
    names = members(first)
    if not names or not all(name.startswith(prefix) for name in names):
        raise RuntimeError("archive root prefix is inconsistent")
    if any("/.git/" in name or name.endswith("/.git") for name in names):
        raise RuntimeError("archive contains Git metadata")
    if any("/build/" in name for name in names):
        raise RuntimeError("archive contains a build tree")
    if not any(name == prefix + "readme.txt" for name in names):
        raise RuntimeError("archive is missing readme.txt")

    archive, sidecar, digest = write_outputs(first, out, label)
    temporary = Path(tempfile.mkdtemp(prefix="gx-release-check-"))
    try:
        extract(first, temporary)
        tree = temporary / ("gX-" + label)
        result = subprocess.run(
            [sys.executable, "scripts/sbom.py", "--check"],
            cwd=tree, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        if result.returncode != 0:
            raise RuntimeError("extracted SBOM check failed:\n" + result.stdout)
    finally:
        shutil.rmtree(temporary)

    print("RELEASE_ARCHIVE: %s — %d bytes, %d members"
          % (archive.name, len(first), len(names)))
    print("  sha256: %s" % digest)
    print("  sidecar: %s" % sidecar.name)
    print("RELEASE_ARCHIVE: PASS — deterministic bytes, clean extraction, "
          "SBOM current")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "release")
    parser.add_argument("--check", action="store_true",
                        help="build twice, compare and verify one extraction")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--sign", nargs="?", const="", default=None,
                        metavar="KEYID",
                        help="GPG-sign the archive and sidecar (owner only)")
    args = parser.parse_args()

    if not args.allow_dirty:
        ensure_clean()
    if args.check:
        return check(args.commit, args.out.resolve())

    label = version_label()
    data, prefix = archive_bytes(args.commit, label)
    archive, sidecar, digest = write_outputs(data, args.out.resolve(), label)
    print("RELEASE_ARCHIVE: %s — %d bytes, %d members"
          % (archive, len(data), len(members(data))))
    print("  sha256: %s" % digest)
    print("  sidecar: %s" % sidecar)
    if args.sign is not None:
        for path in (archive, sidecar):
            print("  signature: %s" % sign(path, args.sign))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("RELEASE_ARCHIVE: FAIL — %s" % error)
        sys.exit(1)
