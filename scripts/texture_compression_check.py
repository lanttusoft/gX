#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: E6 gate — encode the three representative textures, verify the decoded
#   PSNR/VRAM budgets, load each DDS through the engine probe, and prove the
#   sibling-sidecar preference. Requires bc7enc; SKIP when it is absent so the
#   portable matrix stays green.
"""Gate the DDS/BCn texture pipeline."""

import argparse
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

import audit_common as audit
import compress_textures


DEFAULT_OUT = audit.SCRATCH / "texture_compression_check"
FIXTURES = (
    ("albedo", "resources/texture/orb.png", 45.0),
    ("normal", "resources/texture/texture_normal.png", 38.0),
    ("mask", "resources/texture/misc/scorch.png", 45.0),
)
PROBE_RE = re.compile(
    r"TEXTURE_PROBE: path=(\S+) id=(\d+) width=(\d+) height=(\d+)")
DDS_RE = re.compile(r"DDS: loaded (\S+) \((\d+)x(\d+)")


def run_probe(path, out):
    environment = audit.scene_env({
        "GX_TEXTURE_PROBE": str(path),
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }, clean=True)
    result = audit.run_scene("--showcase", environment, width=640, height=480,
                             frames=2, shot_frame=1, timeout=120, check=True)
    log = audit.strip_ansi(result.stdout or "")
    probe = PROBE_RE.search(log)
    dds = DDS_RE.search(log)
    if not probe:
        raise RuntimeError("probe did not report " + str(path))
    if probe.group(1) != str(path) or int(probe.group(2)) == 0:
        raise RuntimeError("probe reported an invalid texture for " + str(path))
    metrics = {
        "path": str(path),
        "id": int(probe.group(2)),
        "width": int(probe.group(3)),
        "height": int(probe.group(4)),
        "dds_log": bool(dds),
    }
    (out / (Path(path).stem + ".probe.log")).write_text(log, encoding="utf-8")
    return metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    encoder = compress_textures.encoder_path()
    if not encoder:
        print("TEXTURE_COMPRESSION: SKIP — bc7enc not found "
              "(set GX_BC7ENC or PATH)")
        return 0

    report = {"encoder": encoder, "fixtures": [], "sidecar": None}
    failures = []
    for kind, relative, threshold in FIXTURES:
        source = audit.ROOT / relative
        output = out / (source.stem + ".dds")
        metrics = compress_textures.encode(source, output, kind, encoder)
        metrics["threshold"] = threshold
        if metrics["psnr"] < threshold:
            failures.append("%s PSNR %.2f < %.2f"
                            % (kind, metrics["psnr"], threshold))
        if metrics["vram_ratio"] >= 0.5:
            failures.append("%s is not at least 2x smaller than RGBA8" % kind)
        metrics["probe"] = run_probe(output, out)
        if metrics["probe"]["width"] != metrics["width"] \
                or metrics["probe"]["height"] != metrics["height"]:
            failures.append("%s probe dimensions differ from the source" % kind)
        if not metrics["probe"]["dds_log"]:
            failures.append("%s did not take the DDS load path" % kind)
        report["fixtures"].append(metrics)

    with tempfile.TemporaryDirectory(prefix="gx-tex-sidecar-") as directory:
        subtree = Path(directory)
        shutil.copy2(audit.ROOT / FIXTURES[0][1], subtree / "orb.png")
        encoded = compress_textures.encode(
            subtree / "orb.png", subtree / "orb.dds", "albedo", encoder)
        probe = run_probe(subtree / "orb.png", out)
        report["sidecar"] = {"encode": encoded, "probe": probe}
        if not probe["dds_log"] or probe["width"] != encoded["width"]:
            failures.append("sibling .dds was not preferred over the PNG")

    (out / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for item in report["fixtures"]:
        print("  %-6s %s  PSNR %.2f dB  %.2fx RGBA8  probe %dx%d"
              % (item["kind"], item["format"], item["psnr"],
                 item["vram_ratio"], item["probe"]["width"],
                 item["probe"]["height"]))
    print("  sidecar: %s" % ("preferred" if report["sidecar"]["probe"]["dds_log"]
                             else "NOT preferred"))
    if failures:
        for failure in failures:
            print("  FAIL " + failure)
        print("TEXTURE_COMPRESSION: FAIL")
        return 1
    print("TEXTURE_COMPRESSION: PASS — BC7/BC5/BC4 load, sidecar wins, "
          "PSNR and VRAM budgets hold")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("TEXTURE_COMPRESSION: FAIL — %s" % error)
        sys.exit(1)
