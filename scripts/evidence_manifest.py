#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I036/GX-I039 claim-level evidence manifest. Confidence and volatility
#   are derived from weighted for/against evidence; they never promote a claim
#   or replace gates/proofs. Null (not assessed) and passive 0 (assessed, no
#   evidence) are distinct; active 0 is balanced high-weight evidence.
import argparse
import json
from pathlib import Path
import re
import sys

LEVEL_CAPS = {
    "claimed": 10,
    "implemented": 25,
    "compiles": 35,
    "unit": 55,
    "external": 60,
    "gate": 70,
    "visual": 75,
    "reproducible": 85,
    "formal": 100,
}
DIRECTIONS = ("for", "against", "neutral")
DEPENDENT_FACTOR = 0.5
STALE_FACTOR = 0.5
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def fail(message):
    raise ValueError(message)


def validate_manifest(data):
    if not isinstance(data, dict) or data.get("schema") != 1:
        fail("manifest schema must be 1")
    claims = data.get("claims")
    if not isinstance(claims, list) or not claims:
        fail("manifest needs a non-empty claims list")
    seen = set()
    for claim in claims:
        if not isinstance(claim, dict):
            fail("claim must be an object")
        for key in ("id", "claim", "assessed", "evidence"):
            if key not in claim:
                fail("claim missing %s" % key)
        if not isinstance(claim["id"], str) or not ID_RE.match(claim["id"]):
            fail("invalid claim id: %r" % claim["id"])
        if claim["id"] in seen:
            fail("duplicate claim id: %s" % claim["id"])
        seen.add(claim["id"])
        if not isinstance(claim["claim"], str) or not claim["claim"].strip():
            fail("%s: empty claim text" % claim["id"])
        if not isinstance(claim["assessed"], bool):
            fail("%s: assessed must be a boolean" % claim["id"])
        if "confidence" in claim or "volatility" in claim:
            fail("%s: confidence/volatility are derived, do not store them"
                 % claim["id"])
        if "significance" in claim:
            value = claim["significance"]
            if (not isinstance(value, (int, float))
                    or isinstance(value, bool) or not 0 <= value <= 100):
                fail("%s: significance must be 0..100" % claim["id"])
        evidence = claim["evidence"]
        if not isinstance(evidence, list):
            fail("%s: evidence must be a list" % claim["id"])
        if not claim["assessed"] and evidence:
            fail("%s: unassessed claims cannot carry evidence" % claim["id"])
        for index, item in enumerate(evidence):
            prefix = "%s evidence[%d]" % (claim["id"], index)
            if not isinstance(item, dict):
                fail("%s: item must be an object" % prefix)
            for key in ("level", "direction", "weight", "artifact",
                        "independent", "fresh"):
                if key not in item:
                    fail("%s: missing %s" % (prefix, key))
            if item["level"] not in LEVEL_CAPS:
                fail("%s: unknown level %r" % (prefix, item["level"]))
            if item["direction"] not in DIRECTIONS:
                fail("%s: unknown direction %r" % (prefix, item["direction"]))
            weight = item["weight"]
            if (not isinstance(weight, (int, float)) or isinstance(weight, bool)
                    or not 0 <= weight <= 100):
                fail("%s: weight must be 0..100" % prefix)
            if weight > LEVEL_CAPS[item["level"]]:
                fail("%s: weight %.1f exceeds %s cap %.0f"
                     % (prefix, weight, item["level"],
                        LEVEL_CAPS[item["level"]]))
            if not isinstance(item["artifact"], str) or not item["artifact"]:
                fail("%s: artifact must be a non-empty string" % prefix)
            if not isinstance(item["independent"], bool):
                fail("%s: independent must be a boolean" % prefix)
            if not isinstance(item["fresh"], bool):
                fail("%s: fresh must be a boolean" % prefix)
    return data


def effective_weight(item):
    value = float(item["weight"])
    if not item["independent"]:
        value *= DEPENDENT_FACTOR
    if not item["fresh"]:
        value *= STALE_FACTOR
    return value


def compute_claim(claim):
    assessed = claim["assessed"]
    for_count = sum(1 for item in claim["evidence"]
                    if item["direction"] == "for")
    against_count = sum(1 for item in claim["evidence"]
                        if item["direction"] == "against")
    neutral_count = sum(1 for item in claim["evidence"]
                        if item["direction"] == "neutral")
    raw_for = sum(float(item["weight"]) for item in claim["evidence"]
                  if item["direction"] == "for")
    raw_against = sum(float(item["weight"]) for item in claim["evidence"]
                      if item["direction"] == "against")
    for_weight = sum(effective_weight(item) for item in claim["evidence"]
                     if item["direction"] == "for")
    against_weight = sum(effective_weight(item) for item in claim["evidence"]
                         if item["direction"] == "against")
    total = for_weight + against_weight
    if not assessed:
        confidence = None
        volatility = None
        status = "not-assessed"
    elif total <= 0.0:
        confidence = 0.0
        volatility = 0.0
        status = "passive-zero"
    else:
        confidence = 100.0 * (for_weight - against_weight) / total
        volatility = 100.0 * 2.0 * min(for_weight, against_weight) / total
        confidence = round(confidence, 1)
        volatility = round(volatility, 1)
        if abs(confidence) < 0.05 and volatility > 0.0:
            status = "active-zero"
        elif confidence > 0.0:
            status = "positive"
        elif confidence < 0.0:
            status = "negative"
        else:
            status = "zero"
    items = []
    for item in claim["evidence"]:
        copy = dict(item)
        copy["effective_weight"] = round(effective_weight(item), 1)
        items.append(copy)
    return {
        "id": claim["id"],
        "claim": claim["claim"],
        "assessed": assessed,
        "status": status,
        "significance": claim.get("significance"),
        "for_count": for_count,
        "against_count": against_count,
        "neutral_count": neutral_count,
        "raw_for_weight": round(raw_for, 1),
        "raw_against_weight": round(raw_against, 1),
        "for_weight": round(for_weight, 1),
        "against_weight": round(against_weight, 1),
        "total_weight": round(total, 1),
        "confidence": confidence,
        "volatility": volatility,
        "evidence": items,
    }


def value_text(value):
    return "null" if value is None else "%.1f" % value


def render_markdown(report):
    lines = ["# Evidence manifest", "",
             "Confidence is derived from weighted for/against evidence. "
             "It is not a gate, proof, or release decision.", "",
             "| Claim | Status | Confidence | Volatility | For | Against | "
             "For weight | Against weight | Significance |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for claim in report["claims"]:
        lines.append("| `%s` | %s | %s | %s | %d | %d | %.1f | %.1f | %s |"
                     % (claim["id"], claim["status"],
                        value_text(claim["confidence"]),
                        value_text(claim["volatility"]),
                        claim["for_count"], claim["against_count"],
                        claim["for_weight"], claim["against_weight"],
                        "null" if claim["significance"] is None
                        else "%.0f" % claim["significance"]))
    for claim in report["claims"]:
        lines += ["", "## %s" % claim["id"], "",
                  claim["claim"], "",
                  "Status: **%s**; confidence %s; volatility %s; "
                  "significance %s." % (
                      claim["status"], value_text(claim["confidence"]),
                      value_text(claim["volatility"]),
                      "null" if claim["significance"] is None
                      else "%.0f" % claim["significance"]), ""]
        if claim["evidence"]:
            lines += ["| Direction | Level | Weight | Effective | "
                      "Independent | Fresh | Artifact | Note |",
                      "| --- | --- | ---: | ---: | --- | --- | --- | --- |"]
            for item in claim["evidence"]:
                lines.append("| %s | %s | %.1f | %.1f | %s | %s | `%s` | %s |"
                             % (item["direction"], item["level"],
                                item["weight"], item["effective_weight"],
                                "yes" if item["independent"] else "no",
                                "yes" if item["fresh"] else "no",
                                item["artifact"], item.get("note", "")))
        else:
            lines.append("No evidence items.")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(
        description="Validate and report a claim-level evidence manifest.")
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--check", action="store_true",
                        help="validate only")
    parser.add_argument("--json", action="store_true",
                        help="print the derived JSON report")
    arguments = parser.parse_args()
    try:
        data = json.loads(Path(arguments.manifest).read_text(encoding="utf-8"))
        validate_manifest(data)
        report = {
            "schema": 1,
            "claims": [compute_claim(claim) for claim in data["claims"]],
        }
        if arguments.check:
            print("EVIDENCE_MANIFEST: OK — %d claims" % len(report["claims"]))
            return 0
        if arguments.json:
            print(json.dumps(report, indent=2))
        else:
            print(render_markdown(report), end="")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print("EVIDENCE_MANIFEST: FAIL — %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
