#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: audit gate reachability and actual theorem axioms, not just textual placeholders.
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess
import tempfile

PROOFS = Path(__file__).resolve().parents[2] / "proofs"
FAMILIES = ("GxProofs", "GxProofsGfx", "GxMath")
STANDARD_AXIOMS = {"propext", "Classical.choice", "Quot.sound"}


def imports(text):
    #ai: remove nested Lean block comments before inspecting import declarations.
    clean = []
    depth = 0
    quoted = False
    i = 0
    while i < len(text):
        pair = text[i:i+2]
        if quoted:
            if text[i] == "\\" and i+1 < len(text):
                i += 2
                continue
            if text[i] == '"':
                quoted = False
            clean.append("\n" if text[i] == "\n" else " ")
            i += 1
            continue
        if not quoted and pair == "/-":
            if not depth:
                clean.append(" ")
            depth += 1
            i += 2
        elif depth and pair == "-/":
            depth -= 1
            i += 2
        elif not depth and not quoted and pair == "--":
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
        else:
            if not depth:
                if text[i] == '"':
                    quoted = True
                    clean.append(" ")
                else:
                    clean.append(text[i])
            elif text[i] == "\n":
                clean.append("\n")
            i += 1
    return [name for line in re.findall(r"^\s*import\s+([^\n]+)", "".join(clean), re.M)
            for name in line.split()]


def coverage(root, families):
    modules = {".".join(path.relative_to(root).with_suffix("").parts)
               for family in families for path in (root / family).rglob("*.lean")}
    visited = set()

    def visit(module):
        if module in visited:
            return
        visited.add(module)
        path = root.joinpath(*module.split(".")).with_suffix(".lean")
        if path.is_file():
            for dependency in imports(path.read_text()):
                if dependency.split(".")[0] in FAMILIES:
                    visit(dependency)

    for family in families:
        visit(family)
    return len(modules), sorted(modules-visited)


def parse_axioms(output):
    rows = []
    for name, values in re.findall(r"GX_AXIOMS (\S+) \[([^\]]*)\]", output):
        rows.append({"name": name, "axioms": [value.strip() for value in values.split(",") if value.strip()]})
    return rows


def audit_source(families):
    prefix_tests = " || ".join(f'(origin == "{family}" || origin.startsWith "{family}.")' for family in families)
    return "\n".join(f"import {family}" for family in families) + f'''
import Lean.Util.CollectAxioms
import Lean.Elab.Command
set_option format.width 1000000
set_option maxHeartbeats 0
open Lean Elab Command
run_cmd do
  let env ← getEnv
  for module in env.header.moduleNames do
    let origin := module.toString
    if {prefix_tests} then
      logInfo m!"GX_MODULE {{module}}"
  for (name, info) in env.constants.toList do
    match env.getModuleIdxFor? name with
    | some index =>
      let origin := env.header.moduleNames[index]!.toString
      if {prefix_tests} then
        match info with
        | .thmInfo _ =>
          let axioms ← collectAxioms name
          logInfo m!"GX_AXIOMS {{name}} {{axioms.toList}}"
        | .axiomInfo _ => logInfo m!"GX_PROJECT_AXIOM {{name}}"
        | _ => pure ()
    | none => pure ()
'''


def main():
    parser = argparse.ArgumentParser(description="Audit Lean root coverage and theorem trust dependencies.")
    parser.add_argument("--full", action="store_true", help="Include both Mathlib tiers; cache is required.")
    parser.add_argument("--no-build", action="store_true", help="Use immediately preceding gate builds.")
    parser.add_argument("--report", type=Path, help="Retain the complete theorem/axiom ledger as JSON.")
    args = parser.parse_args()
    families = FAMILIES if args.full else FAMILIES[:1]
    count, missing = coverage(PROOFS, families)
    if missing:
        print("PROOF_AUDIT: FAIL — ungated modules: " + ", ".join(missing))
        return 1
    if not args.no_build:
        subprocess.run(["lake", "build", *families], cwd=PROOFS, check=True)
    scratch = Path("/tmp/opencode")
    with tempfile.TemporaryDirectory(prefix="gx_proof_audit_", dir=scratch if scratch.is_dir() else None) as directory:
        source = Path(directory) / "Audit.lean"
        source.write_text(audit_source(families))
        result = subprocess.run(["lake", "env", "lean", str(source)], cwd=PROOFS,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    rows = parse_axioms(result.stdout)
    imported = set(re.findall(r"GX_MODULE (\S+)", result.stdout))
    expected = {".".join(path.relative_to(PROOFS).with_suffix("").parts)
                for family in families for path in (PROOFS/family).rglob("*.lean")}
    absent = sorted(expected-imported)
    bad = [row for row in rows if set(row["axioms"])-STANDARD_AXIOMS]
    project_axioms = re.findall(r"GX_PROJECT_AXIOM (\S+)", result.stdout)
    passed = result.returncode == 0 and bool(rows) and not bad and not project_axioms and not absent
    report = {"verdict": "PASS" if passed else "FAIL", "families": families,
              "modules": count, "theorem_declarations_including_generated": len(rows),
              "allowed_axioms": sorted(STANDARD_AXIOMS), "project_axioms": project_axioms,
              "missing_from_compiled_environment": absent,
              "axiom_counts": dict(collections.Counter(axiom for row in rows for axiom in row["axioms"])),
              "unexpected_dependencies": bad, "theorems": rows,
              "implementation_correspondence_certified": False}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    if not passed:
        if result.returncode:
            print(result.stdout)
        print(f"PROOF_AUDIT: FAIL — {len(bad)} nonstandard dependency rows, {len(project_axioms)} project axioms, {len(absent)} absent modules")
        return 1
    print(f"PROOF_AUDIT: PASS — {count} modules reachable; {len(rows)} theorem declarations; standard axioms only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
