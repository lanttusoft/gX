#!/usr/bin/env python3
#ai: validate actual Git source exports without extraction, publication or history changes.
"""Archive privacy/shape gate; this does not certify repository history."""
import argparse
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_DIRS = {"bugs", "screenshot", "certificates", ".opencode", ".claude"}
PRIVATE_FILES = {
    ".ai/session.md", "resources/settings.json",
}
#ai: module/lab binaries are build outputs, never engine source or release
#   artifacts; the export list is source/ABI metadata and must remain.
SHARED_BINARY_SUFFIXES = {".so", ".dylib", ".dll"}
EXPORT_LIST = "modules/module_exports.txt"


def check_names(names, layout="subtree"):
    problems = []
    engine = []
    export_list_seen = False
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts:
            problems.append(f"unsafe archive path: {name}")
            continue
        parts = path.parts
        if not parts:
            continue
        if layout == "release":
            if parts[0] != "master":
                problems.append(f"outside engine subtree: {name}")
                continue
            parts = parts[1:]
        if not parts:
            continue
        relative = "/".join(parts)
        engine.append(relative)
        if relative == EXPORT_LIST:
            export_list_seen = True
        if parts[0] in {"build", "proofs"}:
            problems.append(f"non-source tree: {relative}")
        if path.suffix.lower() in SHARED_BINARY_SUFFIXES:
            problems.append(f"module/shared binary in export: {relative}")
        if parts[0] in PRIVATE_DIRS or relative in PRIVATE_FILES:
            problems.append(f"excluded private/quarantined path: {relative}")
        if len(parts) == 1 and relative.startswith("screenshot") and relative.endswith(".png"):
            problems.append(f"root author capture: {relative}")
    if "CMakeLists.txt" not in engine:
        problems.append("CMakeLists.txt missing")
    if not any(name.startswith("src/") for name in engine):
        problems.append("src/ missing")
    if not export_list_seen:
        problems.append(f"{EXPORT_LIST} missing (engine ABI metadata)")
    return problems


def archive_names(repo, ref="HEAD", layout="subtree", worktree_attributes=False):
    resolved = subprocess.run(["git", "-C", str(repo), "rev-parse", "--verify",
                               "--end-of-options", f"{ref}^{{commit}}"],
                              check=True, capture_output=True, text=True).stdout.strip()
    info = subprocess.run(["git", "-C", str(repo), "rev-parse", "--git-path", "info/attributes"],
                          check=True, capture_output=True, text=True).stdout.strip()
    info_path = Path(info)
    if not info_path.is_absolute():
        info_path = Path(repo) / info_path
    if not worktree_attributes and info_path.exists():
        if any(line.strip() and not line.lstrip().startswith("#")
               for line in info_path.read_text().splitlines()):
            raise RuntimeError("local info/attributes overrides active; committed export policy cannot be certified")
    command = ["git", "-C", str(repo), "-c", f"core.attributesFile={os.devnull}"]
    if worktree_attributes and layout == "subtree":
        command += ["-c", f"core.attributesFile={Path(repo).resolve() / 'master/.gitattributes'}"]
    command += ["archive", "--format=tar"]
    if worktree_attributes:
        command.append("--worktree-attributes")
    command += [f"{resolved}:master"] if layout == "subtree" else [resolved, "master"]
    env = dict(os.environ, GIT_ATTR_NOSYSTEM="1")
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env) as process:
        try:
            with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
                names = [member.name for member in archive]
        except tarfile.TarError as error:
            process.stdout.close()
            stderr = process.stderr.read().decode(errors="replace").strip()
            process.wait()
            raise RuntimeError(f"archive failed: {stderr or error}") from error
        process.stdout.read()
        process.stdout.close()
        stderr = process.stderr.read().decode(errors="replace").strip()
        if process.wait() != 0:
            raise RuntimeError(f"archive failed: {stderr}")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--layout", choices=["subtree", "release"], default="subtree")
    parser.add_argument("--worktree-attributes", action="store_true",
                        help="developer preview only; release/certification uses committed attributes")
    args = parser.parse_args()
    try:
        repo = args.repo or Path(subprocess.run(["git", "rev-parse", "--show-toplevel"],
                                               cwd=ROOT, check=True, capture_output=True,
                                               text=True).stdout.strip())
        names = archive_names(repo, args.ref, args.layout, args.worktree_attributes)
        problems = check_names(names, args.layout)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"PACKAGING_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    mode = "worktree-policy preview" if args.worktree_attributes else "committed-policy export"
    for problem in problems:
        print(f"  {problem}")
    print(f"PACKAGING_CHECK: {'FAIL' if problems else 'PASS'} — {args.layout}, {mode}, {len(names)} entries; history not certified")
    return bool(problems)


if __name__ == "__main__":
    sys.exit(main())
