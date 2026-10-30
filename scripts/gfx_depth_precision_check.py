#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: live allocation queries and format-matched GPU evidence, not universal precision certification.
import argparse
from fractions import Fraction as Q
import json
import math
from pathlib import Path
import re
import struct

import audit_common as audit


def require(value, message):
    if not value:
        raise ValueError(message)


def parse_probe(text):
    text = audit.strip_ansi(text)
    driver = re.findall(r"DEPTH_PROBE_DRIVER (.+)", text)
    formats = re.findall(r"DEPTH_PROBE_FORMAT scene=(\d+) bits=(\d+) msaa=(\d+) msbits=(\d+) samples=(\d+)", text)
    verdict = re.findall(r"DEPTH_PROBE_RESULT complete=(\d+) glerror=(\d+)", text)
    restored = re.findall(r"DEPTH_PROBE_RESTORE restored=(\d+) glerror=(\d+)", text)
    require(len(driver) == len(formats) == len(verdict) == 1, "missing/duplicate probe metadata")
    require(verdict[0] == ("1", "0"), f"probe failure: {verdict}")
    require(restored == [("1", "0")] and "GL_STATE_DIFF depth_probe" not in text, "GL restore failure")
    scene, bits, msformat, msbits, count = map(int, formats[0])
    require(0 < bits <= 64 and 0 < msbits <= 64 and count > 1, "invalid depth/MSAA precision")
    cases = {}
    for name, fields in re.findall(r"DEPTH_PROBE_SAMPLE (\w+) ([^\n]+)", text):
        require(name in {"near", "mid", "far"} and name not in cases, "bad sample name")
        values = {k: float.fromhex(v) for k, v in (part.split("=", 1) for part in fields.split())}
        require(set(values) == {"distance", "ideal", "input", "direct", "readback", "msfirst", "msmin", "msmax", "msmean", "resolved"}, "bad sample fields")
        require(all(math.isfinite(v) for v in values.values()), "nonfinite GPU measurement")
        require(values["distance"] > 0 and all(0 <= values[key] <= 1 for key in values if key != "distance"), "depth out of range")
        require(values["msmin"] == values["msmax"] == values["msfirst"], "unequal constant-depth MS samples")
        distance = Q.from_float(values["distance"])
        ideal = Q(10000, 9999)-Q(1000, 9999)/distance
        submitted, direct, resolved = map(Q.from_float, (values["input"], values["direct"], values["resolved"]))
        sample = dict(values)
        sample.update({"double_oracle_deviation": float(abs(Q.from_float(values["ideal"])-ideal)),
            "submitted_float_error": float(abs(submitted-ideal)),
            "direct_storage_and_sampling_error": float(abs(direct-submitted)),
            "direct_vs_readback": float(abs(direct-Q.from_float(values["readback"]))),
            "msaa_storage_and_sampling_error": float(abs(Q.from_float(values["msfirst"])-submitted)),
            "resolve_vs_msaa_sample": float(abs(resolved-Q.from_float(values["msfirst"]))),
            "total_resolved_depth_input_error": float(abs(resolved-ideal))})
        if scene in {0x1902, 0x81A5, 0x81A6, 0x81A7}:
            levels = Q(2**bits-1)
            expected = Q(round(submitted*levels))/levels
            expected_float = struct.unpack("f", struct.pack("f", float(expected)))[0]
            conversion = abs(Q.from_float(expected_float)-expected)
            conditional_input_bound = abs(submitted-ideal)+Q(1, 2)/levels+conversion
            sample.update({"conditional_nearest_grid_bound": float(Q(1, 2)/levels),
                "hypothetical_grid_to_float_error": float(conversion),
                "sample_vs_hypothetical_nearest_float": float(abs(direct-Q.from_float(expected_float))),
                "nearest_conversion_observed_for_this_sample": direct == Q.from_float(expected_float),
                "conditional_total_depth_input_bound": float(conditional_input_bound),
                "resolved_within_conditional_depth_input_bound_advisory": abs(resolved-ideal) <= conditional_input_bound})
        cases[name] = sample
    require(set(cases) == {"near", "mid", "far"}, "missing distance fixtures")
    return {"result": "pass", "driver": driver[0], "scene_internal_format": scene, "scene_depth_bits": bits,
        "msaa_internal_format": msformat, "msaa_depth_bits": msbits, "samples": count, "cases": cases,
        "limits": "Live production allocation metadata; disposable format-matched constant-depth targets. Not presented scene, edge resolve, raster interpolation or universal GPU certification."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=audit.BIN)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    with audit.settings_preserved():
        run = audit.run_scene("--showcase", audit.scene_env(clean=True, GX_DEPTH_PRECISION_PROBE=1,
                              GX_T_MSAA=4, GX_T_BLOOM=1, GX_SKIP_TESTS=1), binary=args.binary,
                              width=640, height=480, frames=4, timeout=90)
    if args.log:
        args.log.write_text(run.stdout)
    report = parse_probe(run.stdout)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print("DEPTH_PRECISION: PASS — live format queries and three GPU sample/resolve fixtures")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
