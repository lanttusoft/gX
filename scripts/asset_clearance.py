#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: create a hash-bound, author-declared provenance intake report for gX assets.
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys


PROJECT = Path(__file__).resolve().parent.parent
RESOURCES = (PROJECT / "resources").resolve()
MANIFEST = PROJECT / "license" / "provenance.json"
ORIGINS = ("author", "ai-generated", "public-domain", "third-party")
PUBLISH_ACTIONS = ("keep", "review-ai-stance", "verify-license", "quarantine")


class ClearanceError(ValueError):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_asset(value):
    supplied = Path(value).expanduser()
    candidate = supplied if supplied.is_absolute() else PROJECT / supplied
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise ClearanceError(f"{value}: cannot resolve asset: {error}") from error
    try:
        relative = resolved.relative_to(RESOURCES).as_posix()
    except ValueError as error:
        raise ClearanceError(
            f"{value}: assets must already be inside this gX checkout's resources/; "
            "export original work directly into gX rather than importing from another checkout"
        ) from error
    if not resolved.is_file():
        raise ClearanceError(f"{value}: expected a regular file")
    return resolved, f"resources/{relative}"


def check_manifest_exclusions(relative, digest, manifest):
    if relative in manifest.get("quarantine", []):
        raise ClearanceError(f"{relative}: quarantined resources cannot be cleared by this tool")
    basename = Path(relative).name.lower()
    for rule in manifest.get("denied", []):
        if rule.get("name") and rule["name"].lower() in basename:
            raise ClearanceError(
                f"{relative}: denied name '{rule['name']}' cannot be cleared by this tool"
            )
        if rule.get("sha256") and digest == rule["sha256"]:
            raise ClearanceError(f"{relative}: denied content hash cannot be cleared by this tool")


def build_report(asset_values, *, origin, creator, source, license_text,
                 publish, attribution=None, evidence=None, generated_at=None):
    if origin not in ORIGINS:
        raise ClearanceError(f"origin must be one of: {', '.join(ORIGINS)}")
    if publish not in PUBLISH_ACTIONS:
        raise ClearanceError(f"publish must be one of: {', '.join(PUBLISH_ACTIONS)}")
    for label, value in (("creator", creator), ("source", source), ("license", license_text)):
        if not value or not value.strip():
            raise ClearanceError(f"{label} must not be empty")
    if origin == "third-party" and not attribution:
        raise ClearanceError("third-party assets require an attribution statement")

    try:
        manifest = json.loads(MANIFEST.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ClearanceError(f"cannot read provenance manifest: {error}") from error

    assets = []
    seen = set()
    for value in asset_values:
        path, relative = resolve_asset(value)
        if relative in seen:
            raise ClearanceError(f"duplicate asset path: {relative}")
        seen.add(relative)
        digest = sha256(path)
        check_manifest_exclusions(relative, digest, manifest)
        assets.append({
            "path": relative,
            "sha256": digest,
            "bytes": path.stat().st_size,
        })

    record = {
        "origin": origin,
        "creator": creator.strip(),
        "source": source.strip(),
        "license": license_text.strip(),
        "publish": publish,
    }
    if attribution:
        record["attribution"] = attribution.strip()
    if evidence:
        record["evidence"] = evidence.strip()
    manifest_entries = []
    for asset in assets:
        entry = {
            "match": asset["path"],
            "origin": record["origin"],
            "license": record["license"],
            "publish": record["publish"],
        }
        if record.get("attribution"):
            entry["attribution"] = record["attribution"]
        manifest_entries.append(entry)

    return {
        "schema": 1,
        "report_type": "author-declared asset provenance intake",
        "generated_at_utc": generated_at or datetime.now(timezone.utc).isoformat(),
        "declaration": record,
        "assets": assets,
        "suggested_provenance_entries": manifest_entries,
        "limitations": [
            "This report records supplied claims; it does not independently verify authorship, source, or license.",
            "A report is not legal clearance and does not grant rights to distribute an asset.",
            "Review each claim and resolve any overlapping family-glob entry before editing license/provenance.json.",
            "The tool reads only files already inside this gX checkout's resources/ and never copies or imports them.",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Report provenance claims for asset files already exported into this gX checkout."
    )
    parser.add_argument("assets", nargs="+", help="one or more files under resources/")
    parser.add_argument("--origin", required=True, choices=ORIGINS)
    parser.add_argument("--creator", required=True, help="person or organization claiming authorship/source")
    parser.add_argument("--source", required=True, help="how the asset was created or obtained")
    parser.add_argument("--license", required=True, dest="license_text",
                        help="license or intended project-use terms")
    parser.add_argument("--publish", required=True, choices=PUBLISH_ACTIONS,
                        help="proposed distribution action; this is not an approval")
    parser.add_argument("--attribution", help="required attribution statement for third-party assets")
    parser.add_argument("--evidence", help="optional evidence reference, such as a source project path or URL")
    arguments = parser.parse_args(argv)
    try:
        report = build_report(
            arguments.assets,
            origin=arguments.origin,
            creator=arguments.creator,
            source=arguments.source,
            license_text=arguments.license_text,
            publish=arguments.publish,
            attribution=arguments.attribution,
            evidence=arguments.evidence,
        )
    except ClearanceError as error:
        print(f"asset clearance report: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
