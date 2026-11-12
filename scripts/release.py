#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I040 — dry-run-first release orchestration. It validates and prepares a
#   release, runs the class admission tier, packages/verifies the archive and
#   assembles a retained evidence bundle. Tag, push and publication remain
#   explicit owner commands; this tool never publishes on its own.
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "release"
ARTIFACTS = ROOT / "build" / "artifacts"
VERSION_RE = re.compile(r"v?(\d+)\.(\d+)\.(\d+)(?:_([0-9A-Za-z]+))?")
SECTION_RE = re.compile(r"^## \[([^\]]+)\][^\n]*\n", re.M)
CLASS_RE = re.compile(r"^Release-Class:\s*(major|minor|patch|variant)\s*$", re.M)
MIRRORS = (
    ("readme.txt", re.compile(r"(?m)^v\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)? \(beta\)"),
     "v{version} (beta)"),
    ("README.md", re.compile(r"\(`v\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)?` beta\)"),
     "(`v{version}` beta)"),
    ("src/common/data.c", re.compile(r'"\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)? beta"'),
     '"{version} beta"'),
    ("src/gui/templates/titlegx.c",
     re.compile(r"v\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)? BETA"),
     "v{version} BETA"),
    ("src/gui/templates/settings.c",
     re.compile(r"v\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)? BETA"),
     "v{version} BETA"),
)


def run(command, *, check=True, cwd=ROOT):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if check and result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed ({result.returncode})")
    return result


def parse_version(value):
    match = VERSION_RE.search(str(value))
    if not match:
        raise ValueError(f"invalid version: {value!r}")
    major, minor, patch, variant = match.groups()
    return (int(major), int(minor), int(patch), variant or "")


def format_version(version):
    major, minor, patch, variant = version
    text = f"{major}.{minor}.{patch}"
    return f"{text}_{variant}" if variant else text


def readme_version():
    return parse_version((ROOT / "readme.txt").read_text().splitlines()[3])


def git_clean():
    result = run(["git", "status", "--porcelain"])
    return not result.stdout.strip()


def previous_tag(version):
    result = run(["git", "tag", "--list", "v*", "--sort=-v:refname"])
    candidates = []
    for line in result.stdout.splitlines():
        try:
            parsed = parse_version(line)
        except ValueError:
            continue
        if parsed < version:
            candidates.append((parsed, line))
    return max(candidates)[1] if candidates else None


def changelog_section(version):
    text = (ROOT / "CHANGELOG.md").read_text()
    target = format_version(version)
    matches = list(SECTION_RE.finditer(text))
    for index, match in enumerate(matches):
        if match.group(1) != target:
            continue
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        section = text[start:end]
        declared = CLASS_RE.search(section)
        return section, (declared.group(1) if declared else None)
    return None, None


def mirror_plan(version):
    text_version = format_version(version)
    plan = []
    for relative, pattern, template in MIRRORS:
        path = ROOT / relative
        text = path.read_text()
        replacement = template.format(version=text_version)
        updated, count = pattern.subn(replacement, text, count=1)
        if count != 1:
            raise RuntimeError(f"{relative}: version mirror not found exactly once")
        plan.append((path, text, updated))
    return plan


def state_path(version):
    return BUILD / f"v{format_version(version)}" / "state.json"


def load_state(version):
    path = state_path(version)
    if not path.is_file():
        return {"schema": 1, "version": format_version(version), "stages": {}}
    return json.loads(path.read_text())


def save_state(version, state):
    path = state_path(version)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def status(version, requested_class=None):
    section, declared = changelog_section(version)
    previous = previous_tag(version)
    state = load_state(version)
    archive = ARTIFACTS / f"gX-v{format_version(version)}.tar.gz"
    digest = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    return {
        "version": format_version(version),
        "class": requested_class or declared,
        "declared_class": declared,
        "changelog": section is not None,
        "previous_tag": previous,
        "commit": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "clean": git_clean(),
        "tag_exists": run(["git", "tag", "--list",
                           f"v{format_version(version)}"]).stdout.strip() != "",
        "archive": str(archive.relative_to(ROOT)) if archive.is_file() else None,
        "archive_sha256": digest,
        "stages": sorted(state.get("stages", {})),
    }


def command_status(args):
    version = parse_version(args.version) if args.version else readme_version()
    data = status(version, args.release_class)
    if args.json:
        print(json.dumps(data, indent=2, sort_keys=True))
        return 0
    for key in ("version", "class", "declared_class", "previous_tag", "commit",
                "clean", "tag_exists", "archive", "stages"):
        print(f"{key:12s} {data[key]}")
    return 0


def command_plan(args):
    version = parse_version(args.version) if args.version else readme_version()
    data = status(version, args.release_class)
    previous = data["previous_tag"]
    if not previous:
        print("RELEASE: no previous tag found", file=sys.stderr)
        return 1
    commands = [
        ["python3", "scripts/version_suggest.py", "--from", previous,
         "--to", "HEAD", "--check", data["class"] or "patch"],
        ["python3", "scripts/version_check.py", "--readme"],
        ["python3", "scripts/version_check.py", "--class"],
        ["make", "version-gate"],
    ]
    if args.full:
        commands += [["make", "check"], ["make", "audit-summary-full"]]
    commands += [
        ["make", "reproducible-check"],
        ["make", "release-archive-check"],
    ]
    print(f"release:    v{data['version']} ({data['class']})")
    print(f"previous:   {previous}")
    print(f"commit:     {data['commit']}")
    print(f"clean:      {data['clean']}")
    print(f"changelog:  {data['changelog']} (declared {data['declared_class']})")
    print("commands:")
    for command in commands:
        print("  " + " ".join(command))
    print("owner-only: tag, push, archive signing and publication")
    return 0


def command_prepare(args):
    version = parse_version(args.version)
    if args.release_class is None:
        print("RELEASE: --class is required", file=sys.stderr)
        return 2
    section, declared = changelog_section(version)
    if section is None:
        print(f"RELEASE: CHANGELOG.md has no [{format_version(version)}] section",
              file=sys.stderr)
        return 1
    if declared != args.release_class:
        print(f"RELEASE: CHANGELOG declares {declared}, expected "
              f"{args.release_class}", file=sys.stderr)
        return 1
    if args.apply and not git_clean() and not args.force:
        print("RELEASE: worktree is not clean; commit or use --force",
              file=sys.stderr)
        return 1
    try:
        plan = mirror_plan(version)
    except RuntimeError as error:
        print(f"RELEASE: {error}", file=sys.stderr)
        return 1
    for path, _old, _new in plan:
        print(f"update {path.relative_to(ROOT)}")
    print("update sbom/gX.spdx.json")
    if not args.apply:
        print("RELEASE: dry run; pass --apply to write")
        return 0
    for path, _old, new in plan:
        path.write_text(new)
    result = run(["python3", "scripts/sbom.py", "--write"], check=False)
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        return result.returncode
    state = load_state(version)
    state["class"] = args.release_class
    state["date"] = args.date or datetime.date.today().isoformat()
    state["previous_tag"] = previous_tag(version)
    state["stages"]["prepared"] = {
        "commit": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "date": state["date"],
    }
    save_state(version, state)
    print(f"RELEASE: prepared v{format_version(version)}; add/commit the release "
          f"changes, then run admit")
    return 0


def run_logged(name, command, log_dir):
    log_dir.mkdir(parents=True, exist_ok=True)
    path = log_dir / f"{name}.log"
    print("+ " + " ".join(command), flush=True)
    with path.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=ROOT, text=True,
                                stdout=stream, stderr=subprocess.STDOUT)
    output = path.read_text(encoding="utf-8", errors="replace")
    print(output[-2000:])
    if result.returncode != 0:
        raise RuntimeError(f"{name} failed ({result.returncode}); log {path}")
    return path, output


def command_admit(args):
    version = parse_version(args.version or readme_version())
    state = load_state(version)
    release_class = state.get("class") or args.release_class
    if not release_class:
        section, release_class = changelog_section(version)
        if section is None or not release_class:
            print("RELEASE: no release class; run prepare first", file=sys.stderr)
            return 1
    previous = state.get("previous_tag") or previous_tag(version)
    if not previous:
        print("RELEASE: no previous tag; cannot run admission", file=sys.stderr)
        return 1
    commands = [
        ("version-suggest", ["python3", "scripts/version_suggest.py",
                             "--from", previous, "--to", "HEAD",
                             "--check", release_class]),
        ("version-readme", ["python3", "scripts/version_check.py", "--readme"]),
        ("version-class", ["python3", "scripts/version_check.py", "--class"]),
        ("version-gate", ["make", "version-gate"]),
    ]
    if args.full:
        commands += [("check", ["make", "check"]),
                     ("matrix", ["make", "audit-summary-full"])]
    if not args.run:
        for _name, command in commands:
            print("  " + " ".join(command))
        print("RELEASE: dry run; pass --run to execute admission")
        return 0
    log_dir = BUILD / f"v{format_version(version)}" / "logs"
    retries = []
    for name, command in commands:
        try:
            _path, output = run_logged(name, command, log_dir)
        except RuntimeError as error:
            print(f"RELEASE: {error}", file=sys.stderr)
            return 1
        if "retry-sequential" in output:
            retries.append(name)
    state["stages"]["admitted"] = {
        "commit": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "retries": retries,
    }
    save_state(version, state)
    if retries:
        print(f"RELEASE: admission retries recorded: {', '.join(retries)}")
    print("RELEASE: admission PASS")
    return 0


def command_package(args):
    version = parse_version(args.version or readme_version())
    state = load_state(version)
    commands = [("reproducible", ["make", "reproducible-check"]),
                ("release-archive", ["make", "release-archive-check"])]
    if args.sign:
        commands.append(("sign-archive",
                         ["python3", "scripts/release_archive.py", "--sign"]))
    if not args.run:
        for _name, command in commands:
            print("  " + " ".join(command))
        print("RELEASE: dry run; pass --run to package")
        return 0
    log_dir = BUILD / f"v{format_version(version)}" / "logs"
    for name, command in commands:
        try:
            _path, _output = run_logged(name, command, log_dir)
        except RuntimeError as error:
            print(f"RELEASE: {error}", file=sys.stderr)
            return 1
    archive = ARTIFACTS / f"gX-v{format_version(version)}.tar.gz"
    digest = None
    if archive.is_file():
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    state["stages"]["packaged"] = {
        "commit": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "archive": str(archive.relative_to(ROOT)) if archive.is_file() else None,
        "archive_sha256": digest,
        "signed": bool(args.sign),
    }
    save_state(version, state)
    print(f"RELEASE: package PASS" + (f" sha256={digest}" if digest else ""))
    return 0


def command_bundle(args):
    version = parse_version(args.version or readme_version())
    state = load_state(version)
    evidence = Path(args.evidence_dir).resolve()
    if evidence == ROOT or ROOT in evidence.parents:
        print("RELEASE: evidence directory must be outside the repository",
              file=sys.stderr)
        return 1
    if not args.write:
        print(f"would write release bundle to {evidence}")
        return 0
    if evidence.exists() and any(evidence.iterdir()):
        print(f"RELEASE: evidence directory is not empty: {evidence}",
              file=sys.stderr)
        return 1
    evidence.mkdir(parents=True, exist_ok=True)
    logs = BUILD / f"v{format_version(version)}" / "logs"
    if logs.is_dir():
        shutil.copytree(logs, evidence / "logs")
    _section, declared = changelog_section(version)
    manifest = {
        "schema": 1,
        "version": format_version(version),
        "class": state.get("class") or declared,
        "commit": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "branch": run(["git", "rev-parse", "--abbrev-ref", "HEAD"]).stdout.strip(),
        "stages": state.get("stages", {}),
    }
    (evidence / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    patch = run(["git", "show", "--format=", "--binary", "HEAD"])
    (evidence / "release.patch").write_text(patch.stdout, encoding="utf-8")
    commit = run(["git", "log", "-1", "--show-signature", "--format=%H%n%G?%n%s"])
    (evidence / "commit.txt").write_text(commit.stdout, encoding="utf-8")
    archive = ARTIFACTS / f"gX-v{format_version(version)}.tar.gz"
    if archive.is_file():
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        manifest["archive"] = {"file": archive.name,
                               "size": archive.stat().st_size,
                               "sha256": digest}
        (evidence / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        (evidence / "archive.txt").write_text(
            f"file={archive.name}\nsize_bytes={archive.stat().st_size}\n"
            f"sha256={digest}\n", encoding="utf-8")
    files = sorted(path for path in evidence.rglob("*") if path.is_file()
                    and path.name != "SHA256SUMS")
    lines = []
    for path in files:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{digest}  {path.relative_to(evidence)}")
    (evidence / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"RELEASE: bundle written to {evidence}")
    return 0


def owner_commands(version, notes):
    text = format_version(version)
    print(f"git tag -s v{text} -m \"gX v{text}\"")
    print(f"git push --atomic origin main refs/tags/v{text}")
    print("python3 scripts/release_archive.py --sign")
    print(f"gh release create v{text} build/artifacts/gX-v{text}.tar.gz \\")
    print(f"  build/artifacts/gX-v{text}.tar.gz.sha256 \\")
    print(f"  build/artifacts/gX-v{text}.tar.gz.asc \\")
    print(f"  build/artifacts/gX-v{text}.tar.gz.sha256.asc \\")
    print(f"  --repo lanttusoft/gX --title \"gX v{text} — public beta\" \\")
    print(f"  --notes-file {notes} --prerelease --verify-tag")


def main():
    parser = argparse.ArgumentParser(
        description="Dry-run-first gX release orchestration.")
    sub = parser.add_subparsers(dest="command", required=True)

    status_parser = sub.add_parser("status", help="show release state")
    status_parser.add_argument("--version")
    status_parser.add_argument("--class", dest="release_class")
    status_parser.add_argument("--json", action="store_true")
    status_parser.set_defaults(function=command_status)

    plan_parser = sub.add_parser("plan", help="print the release command plan")
    plan_parser.add_argument("--version")
    plan_parser.add_argument("--class", dest="release_class")
    plan_parser.add_argument("--full", action="store_true")
    plan_parser.set_defaults(function=command_plan)

    prepare_parser = sub.add_parser("prepare", help="update version mirrors")
    prepare_parser.add_argument("--version", required=True)
    prepare_parser.add_argument("--class", dest="release_class",
                                choices=("major", "minor", "patch", "variant"),
                                required=True)
    prepare_parser.add_argument("--date")
    prepare_parser.add_argument("--apply", action="store_true")
    prepare_parser.add_argument("--force", action="store_true")
    prepare_parser.set_defaults(function=command_prepare)

    admit_parser = sub.add_parser("admit", help="run admission gates")
    admit_parser.add_argument("--version")
    admit_parser.add_argument("--class", dest="release_class")
    admit_parser.add_argument("--full", action="store_true")
    admit_parser.add_argument("--run", action="store_true")
    admit_parser.set_defaults(function=command_admit)

    package_parser = sub.add_parser("package", help="build/verify the archive")
    package_parser.add_argument("--version")
    package_parser.add_argument("--run", action="store_true")
    package_parser.add_argument("--sign", action="store_true",
                                help="owner-only archive signing")
    package_parser.set_defaults(function=command_package)

    bundle_parser = sub.add_parser("bundle", help="assemble evidence bundle")
    bundle_parser.add_argument("--version")
    bundle_parser.add_argument("--evidence-dir", required=True)
    bundle_parser.add_argument("--write", action="store_true")
    bundle_parser.set_defaults(function=command_bundle)

    owner_parser = sub.add_parser("owner", help="print owner-only commands")
    owner_parser.add_argument("--version", required=True)
    owner_parser.add_argument("--notes", required=True)
    owner_parser.set_defaults(
        function=lambda args: (owner_commands(parse_version(args.version),
                                              args.notes), 0)[1])

    args = parser.parse_args()
    try:
        return args.function(args)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"RELEASE: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
