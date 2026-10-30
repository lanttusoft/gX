#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: proof certificates record static evidence + executed gates; they do not prove C/Lean translation.
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

import proof_inventory

ROOT = Path(__file__).resolve().parent.parent
CERTS = ROOT / "certificates" / "proofs"
TIERS = ("catalog", "kernel", "guarded")


def gate_commands(tier):
    gates = [("proof-inventory", ["python3", "scripts/proof_inventory.py", "--check"])]
    if tier in ("kernel", "guarded"):
        gates.append(("proofs-full", ["make", "proofs-full"]))
    if tier == "guarded":
        manifest = proof_inventory.load_importance()
        seen = {tuple(command) for _, command in gates}
        for entry in manifest["modules"].values():
            gate = entry.get("gate")
            if not gate:
                continue
            command = gate.split() if gate.startswith("make ") else ["python3", gate]
            key = tuple(command)
            if key not in seen:
                seen.add(key)
                gates.append((gate, command))
    return gates


def run_gate(name, command, timeout=3600):
    started = time.perf_counter()
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    seconds = time.perf_counter() - started
    lines = result.stdout.splitlines()
    failed = result.returncode != 0
    return {
        "name": name,
        "command": " ".join(command),
        "status": "fail" if failed else "pass",
        "seconds": round(seconds, 1),
        "tail": "\n".join(lines[-30:] if failed else lines[-4:]),
    }


def certificate_verdict(gates, audit, reviewed_gaps):
    if any(gate["status"] != "pass" for gate in gates):
        return "NOT CERTIFIED"
    if audit and audit.get("verdict") != "PASS":
        return "NOT CERTIFIED"
    if reviewed_gaps:
        return "GATED"
    return "CERTIFIED"


def certificate_hash(certificate):
    payload = json.dumps(certificate, indent=1, sort_keys=True)
    return hashlib.sha256(payload.encode()).hexdigest()


def module_rows():
    manifest = proof_inventory.load_importance()
    families = {family: proof_inventory.scan_family(family)
                for family in proof_inventory.FAMILIES}
    proof_inventory.annotate_importance(families, manifest)
    reviewed = []
    unreviewed = 0
    for family in proof_inventory.FAMILIES:
        for name, data in families[family]["modules"].items():
            if data["reviewed"]:
                reviewed.append({
                    "module": name,
                    "importance": data["importance"],
                    "significance": data["significance"],
                    "target": data["target"],
                    "achieved": data["achieved"],
                    "justification": data["justification"],
                    "gaps": data["gaps"],
                })
            else:
                unreviewed += 1
    reviewed.sort(key=lambda row: (-row["significance"], row["module"]))
    gaps = [row for row in reviewed
            if proof_inventory.LEVEL_RANK[row["achieved"]]
            < proof_inventory.LEVEL_RANK[row["target"]]]
    return reviewed, gaps, unreviewed


def main():
    parser = argparse.ArgumentParser(description="gX proof certificate")
    parser.add_argument("--tier", choices=TIERS, default="kernel")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=CERTS)
    args = parser.parse_args()

    gates = gate_commands(args.tier)
    reviewed, reviewed_gaps, unreviewed = module_rows()
    if args.dry_run:
        print(f"proof-certify tier={args.tier}")
        print(f"  reviewed modules: {len(reviewed)}")
        print(f"  reviewed gaps: {len(reviewed_gaps)}")
        print(f"  unreviewed modules: {unreviewed}")
        for name, command in gates:
            print(f"  gate {name}: {' '.join(command)}")
        return 0

    certificate = {
        "kind": "proof",
        "tier": args.tier,
        "date": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "policy": "proofs/SIGNIFICANCE.md",
        "manifest_sha256": hashlib.sha256(
            (proof_inventory.IMPORTANCE).read_bytes()).hexdigest(),
        "catalog_sha256": hashlib.sha256(
            (proof_inventory.DOC).read_bytes()).hexdigest(),
        "gates": [],
        "reviewed_modules": reviewed,
        "reviewed_gaps": [row["module"] for row in reviewed_gaps],
        "unreviewed_modules": unreviewed,
    }

    failed = 0
    for name, command in gates:
        result = run_gate(name, command)
        certificate["gates"].append(result)
        marker = "ok  " if result["status"] == "pass" else "FAIL"
        print(f"  {marker} {name:<28} {result['seconds']:>7.1f}s")
        if result["status"] != "pass":
            failed += 1

    audit = None
    if args.tier in ("kernel", "guarded"):
        with tempfile.TemporaryDirectory(prefix="gx_proof_cert_") as scratch:
            report_path = Path(scratch) / "audit.json"
            audit_result = run_gate(
                "proof-audit-report",
                ["python3", "scripts/proof_audit.py", "--full", "--no-build",
                 "--report", str(report_path)])
            certificate["gates"].append(audit_result)
            if audit_result["status"] == "pass" and report_path.exists():
                report = json.loads(report_path.read_text())
                audit = {
                    "verdict": report.get("verdict"),
                    "modules": report.get("modules"),
                    "theorem_declarations": report.get(
                        "theorem_declarations_including_generated"),
                    "allowed_axioms": report.get("allowed_axioms"),
                    "project_axioms": report.get("project_axioms"),
                    "report_sha256": hashlib.sha256(
                        report_path.read_bytes()).hexdigest(),
                }
            else:
                audit = {"verdict": "FAIL", "detail": "audit report failed"}
            certificate["audit"] = audit
    else:
        certificate["audit"] = {"verdict": "not-run", "detail": "catalog tier"}

    verdict = certificate_verdict(certificate["gates"], audit, reviewed_gaps)
    certificate["verdict"] = verdict
    certificate["commit"] = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
        stdout=subprocess.PIPE).stdout.strip()
    certificate["certificate_hash"] = certificate_hash(certificate)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    path = args.output_dir / f"{stamp}-{certificate['commit'][:7]}-{args.tier}.json"
    path.write_text(json.dumps(certificate, indent=1, sort_keys=True) + "\n")
    print(f"PROOF_CERTIFY: {verdict} {args.tier} — "
          f"{len(certificate['gates'])} gates, {len(reviewed_gaps)} reviewed gaps, "
          f"certificate {path.relative_to(ROOT)}")
    return 0 if verdict == "CERTIFIED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
