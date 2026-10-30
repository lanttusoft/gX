#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: generated proof inventory; declaration counts, roots, namespaces and math coverage.
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROOFS = ROOT.parent / "proofs"
DOC = ROOT / "docs" / "proof_inventory.md"
IMPORTANCE = PROOFS / "IMPORTANCE.json"
TARGET_LEVEL = {"K": "guarded", "A": "grounded", "B": "checked", "C": "registered"}
LEVEL_RANK = {"registered": 0, "checked": 1, "grounded": 2, "guarded": 3, "complete": 4}

FAMILIES = ("GxProofs", "GxProofsGfx", "GxMath")
DECL_KINDS = (
    "axiom", "opaque", "def", "abbrev", "structure", "inductive",
    "class", "instance", "lemma", "theorem", "example",
)
DECL_RE = re.compile(
    r"^\s*(?:@\[[^\n]*\]\s*)?"
    r"(?:(?:private|protected|noncomputable|partial|unsafe|scoped|local)\s+)*"
    r"(" + "|".join(DECL_KINDS) + r")\b")
FORBIDDEN_RE = re.compile(r"\b(sorry|admit|native_decide|implemented_by|extern)\b")
MATH_COVERAGE = (
    ("F32v2", "missing", "No vector2 module; only F32v3 algebra is modeled."),
    ("F32v3", "GxMath.Vector", "Partial: algebra, dot/cross, Cauchy-Schwarz, triangle/reverse triangle, lerp, nonzero normalize."),
    ("F32v4 / F64v4", "GxMath.Vector64", "F64v4 lerp only; F32v4 and other v4 operations missing."),
    ("4x4 matrix", "GxMath.Matrix/CofactorInverse/ClipMatrix", "Logical algebra, ideal inverse and clip model; camera-history bridge remains in GFX."),
    ("2x2/3x3 matrix", "GxMath.MatrixSmall", "Determinant, trace, transpose, scaling, diagonal identities."),
    ("quaternion", "GxMath.Quaternion", "Partial: algebra, sandwich/vector rotation, unit norms, inverse/composition, antipodal sign, unit-only matrix agreement and exact-real branch-aware Lerp/Nlerp/Slerp contracts; IEEE execution accuracy missing."),
    ("SDF", "GxMath.Sdf", "Partial: primitive values/signs, sphere/unit-normal plane Lipschitz and clearance; other primitive Lipschitz and IEEE execution missing."),
    ("Fourier/FFT", "GxMath.Fourier", "Partial: explicit Fin/ZMod bridges, DFT linearity/impulse, inverse round trips and Parseval for nonzero length; radix-2 implementation, sine-bin phase and IEEE error missing."),
    ("color", "GxMath.Color", "Partial: signed-delta truncation/clamp, channel bounds, ideal normalization/multiply; IEEE error, full RGBA assembly and raw casts missing."),
    ("convert", "GxMath.Units/Sky/Coordinates", "Partial: basic units, Cartesian sky frames, branch-aware cylindrical/spherical round trips and pole/axis limits; SI-prefix, color and other angular conversions missing."),
    ("easing", "GxMath.Easing", "Partial: selected scalar curves; vector curves and full dispatch missing."),
    ("scalar/int math", "GxMath.Scalar", "Partial: clamp/lerp/normalize/remap; wrap, factorial, GCD, combinatorics and tolerance equality missing."),
    ("random", "GxProofs.Random32", "Partial: increment/bounds/mantissa bits; full PCG state and output sequence missing."),
    ("calculus", "GxMath.Calculus", "Partial: finite-difference identities, affine Newton step, positive-even-step Simpson cubic integral and branch counterexamples; general convergence, tolerance guards and IEEE error missing."),
    ("physics", "GxMath.Physics", "EM, dipole and thermal/radiation contracts present."),
)

MATRIX_MODELS = (
    ("GxMath.Matrix", "GxMath.Matrix", "Logical row-major engine semantics"),
    ("MatrixField.Matrix", "GxProofsGfx.Matrix.MatrixField", "Flat numeric-field order"),
    ("MatrixTuple", "GxProofsGfx.Matrix.MatrixTuple", "Abstract Mathlib tuple upload interpretation"),
    ("ClipMatrix.Mat4", "GxMath.ClipMatrix", "Row-vector clip/NDC model over ℚ"),
    ("TemporalMatrix", "GxProofsGfx.Temporal.TemporalMatrix", "Camera snapshot/history reconstruction"),
    ("CofactorInverse", "GxMath.CofactorInverse", "Ideal cofactor/determinant inverse"),
    ("Camera inverse bridge", "GxProofsGfx.Temporal.CofactorInverse", "Temporal reconstruction using the generic inverse"),
    ("MatrixSmall", "GxMath.MatrixSmall", "2×2/3×3 determinant and trace"),
)

KERNEL_COVERAGE = (
    ("GxProofs.MainDataSwitch", "Scene-switch flags and five-responder contract"),
    ("GxProofs.ModuleInfoConsistency", "moduleInfo mirror and kernel_only case"),
    ("GxProofsGfx.Kernel.MainDataRefresh", "Resolution ladder and refresh bounds"),
    ("GxProofsGfx.Kernel.MainDataReset", "Window/render defaults and reset consistency"),
)

KERNEL_MODULES = {name for name, _ in KERNEL_COVERAGE}


class InventoryError(ValueError):
    """Raised when the importance manifest or catalog cannot be verified."""


def load_importance():
    if not IMPORTANCE.exists():
        raise InventoryError(f"missing importance manifest: {IMPORTANCE}")
    try:
        data = json.loads(IMPORTANCE.read_text())
    except json.JSONDecodeError as exc:
        raise InventoryError(f"invalid importance manifest: {exc}") from exc
    if data.get("version") != 1 or not isinstance(data.get("defaults"), dict):
        raise InventoryError("importance manifest must be version 1 with defaults")
    if not isinstance(data.get("modules"), dict):
        raise InventoryError("importance manifest must contain a modules map")
    return data


def _repo_path(relative):
    return ROOT / relative


def _gate_exists(gate):
    if not gate:
        return False
    if gate.startswith("make "):
        target = gate.split()[1]
        makefile = (ROOT / "Makefile").read_text()
        return re.search(rf"^{re.escape(target)}:", makefile, re.M) is not None
    return _repo_path(gate).exists()


def evidence_level(module, entry):
    counts = module["counts"]
    if (counts["axiom"] or counts["opaque"] or module["forbidden"]):
        return "registered"
    level = "checked"
    source = entry.get("source", [])
    source_ok = bool(source) and all(_repo_path(path).exists() for path in source)
    guard = entry.get("guard")
    guard_ok = bool(guard) and _repo_path(guard).exists()
    if source_ok and guard_ok:
        level = "grounded"
    if level == "grounded" and _gate_exists(entry.get("gate")):
        level = "guarded"
    if level == "guarded" and entry.get("complete", False):
        level = "complete"
    return level


def annotate_importance(families, manifest):
    defaults = manifest["defaults"]
    overrides = manifest["modules"]
    all_names = {name for family in families.values() for name in family["modules"]}
    unknown = sorted(set(overrides) - all_names)
    if unknown:
        raise InventoryError("importance manifest names unknown modules: " + ", ".join(unknown))
    for family_name, family in families.items():
        default = defaults.get(family_name)
        if not default:
            raise InventoryError(f"importance defaults missing for {family_name}")
        for name, module in family["modules"].items():
            entry = overrides.get(name, {})
            importance = entry.get("importance", default["importance"])
            if importance not in TARGET_LEVEL:
                raise InventoryError(f"{name}: invalid importance {importance}")
            significance = entry.get("significance", default["significance"])
            if not isinstance(significance, int) or not 0 <= significance <= 100:
                raise InventoryError(f"{name}: significance must be an integer 0..100")
            target = TARGET_LEVEL[importance]
            achieved = evidence_level(module, entry)
            module.update({
                "importance": importance,
                "significance": significance,
                "target": target,
                "achieved": achieved,
                "reviewed": bool(entry),
                "justification": entry.get("justification", "family default"),
                "gaps": entry.get("gaps", []),
                "missing": LEVEL_RANK[achieved] < LEVEL_RANK[target],
            })


def domain_for(family, name):
    if name in KERNEL_MODULES:
        return "kernel"
    if family == "GxMath":
        return "math"
    if family == "GxProofs":
        return "core"
    short = name.split(".", 1)[1] if "." in name else name
    if short.startswith("Audio."):
        return "audio"
    if short.startswith("Kernel."):
        return "kernel"
    if short.startswith("Matrix."):
        return "matrix"
    if short.startswith("Temporal."):
        return "temporal"
    if short.startswith("Postprocess."):
        return "postprocess"
    if short.startswith("Weather."):
        return "weather"
    if short.startswith("Ssr."):
        return "ssr"
    if short.startswith("Renderer."):
        return "renderer"
    return "renderer"


def clean(text):
    out = []
    index = 0
    depth = 0
    quoted = False
    while index < len(text):
        pair = text[index:index + 2]
        if quoted:
            if text[index] == "\\" and index + 1 < len(text):
                index += 2
                continue
            if text[index] == '"':
                quoted = False
            out.append("\n" if text[index] == "\n" else " ")
            index += 1
            continue
        if pair == "/-":
            depth += 1
            out.append(" ")
            index += 2
        elif depth and pair == "-/":
            depth -= 1
            out.append(" ")
            index += 2
        elif not depth and pair == "--":
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
        else:
            if not depth:
                if text[index] == '"':
                    quoted = True
                    out.append(" ")
                else:
                    out.append(text[index])
            elif text[index] == "\n":
                out.append("\n")
            index += 1
    return "".join(out)


def imports(text):
    clean_text = clean(text)
    return [name for line in re.findall(r"^\s*import\s+([^\n]+)", clean_text, re.M)
            for name in line.split()]


def module_name(path):
    return ".".join(path.relative_to(PROOFS).with_suffix("").parts)


def scan_family(family):
    root_path = PROOFS / f"{family}.lean"
    root_imports = imports(root_path.read_text())
    modules = {}
    for path in sorted((PROOFS / family).rglob("*.lean")):
        text = path.read_text()
        clean_text = clean(text)
        counts = {kind: 0 for kind in DECL_KINDS}
        namespace = None
        forbidden = []
        for line_number, line in enumerate(clean_text.splitlines(), 1):
            match = DECL_RE.match(line)
            if match:
                counts[match.group(1)] += 1
            ns = re.match(r"^\s*namespace\s+(\S+)", line)
            if ns and namespace is None:
                namespace = ns.group(1)
            if FORBIDDEN_RE.search(line):
                forbidden.append((line_number, line.strip()))
        name = module_name(path)
        modules[name] = {
            "name": name,
            "domain": domain_for(family, name),
            "namespace": namespace or "(none)",
            "counts": counts,
            "forbidden": forbidden,
        }
    expected = set(modules)
    imported = {name for name in root_imports if name.split(".")[0] == family}
    return {
        "root": root_imports,
        "modules": modules,
        "missing_from_root": sorted(expected - imported),
        "imports_without_file": sorted(imported - expected),
    }


def totals(family_data):
    result = {kind: 0 for kind in DECL_KINDS}
    for data in family_data.values():
        for kind, count in data["counts"].items():
            result[kind] += count
    return result


def markdown():
    manifest = load_importance()
    families = {family: scan_family(family) for family in FAMILIES}
    annotate_importance(families, manifest)
    lines = [
        "# Proof inventory",
        "",
        "<!-- //ai: generated by scripts/proof_inventory.py; regenerate with make proof-inventory. -->",
        "",
        "This is a static source inventory, not a substitute for the kernel/axiom",
        "audit in `make proofs`/`make proofs-full`. It counts authored declarations",
        "in project Lean files; the audit also counts generated equality/helper",
        "theorems. Lean has no `corollary` keyword, so corollaries are `theorem`",
        "or `lemma` declarations.",
        "",
        "## Tier totals",
        "",
        "| Tier | Modules | Theorems | Lemmas | Defs | Structures | Inductives | Instances | Abbrevs | Axioms | Opaque |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    grand = {kind: 0 for kind in DECL_KINDS}
    for family, data in families.items():
        total = totals(data["modules"])
        for kind in DECL_KINDS:
            grand[kind] += total[kind]
        lines.append(
            f"| `{family}` | {len(data['modules'])} | {total['theorem']} | "
            f"{total['lemma']} | {total['def']} | {total['structure']} | "
            f"{total['inductive']} | {total['instance']} | {total['abbrev']} | "
            f"{total['axiom']} | {total['opaque']} |")
    lines.extend([
        f"| **All** | **{sum(len(data['modules']) for data in families.values())}** | "
        f"**{grand['theorem']}** | **{grand['lemma']}** | **{grand['def']}** | "
        f"**{grand['structure']}** | **{grand['inductive']}** | "
        f"**{grand['instance']}** | **{grand['abbrev']}** | "
        f"**{grand['axiom']}** | **{grand['opaque']}** |",
        "",
        "## Root coverage",
        "",
    ])
    for family, data in families.items():
        status = "complete"
        if data["missing_from_root"] or data["imports_without_file"]:
            status = "FAIL"
        lines.append(f"- `{family}`: {status} ({len(data['modules'])} modules)")
        if data["missing_from_root"]:
            lines.append("  - not imported by root: " + ", ".join(data["missing_from_root"]))
        if data["imports_without_file"]:
            lines.append("  - imports without files: " + ", ".join(data["imports_without_file"]))
    domains = {}
    for family in FAMILIES:
        for data in families[family]["modules"].values():
            domains.setdefault(data["domain"], []).append(data["name"])
    lines.extend([
        "",
        "## Domain map",
        "",
        "Tier is the dependency set; domain is the subject area.",
        "",
        "| Domain | Modules |",
        "|---|---:|",
    ])
    for domain in sorted(domains):
        lines.append(f"| `{domain}` | {len(domains[domain])} |")
    lines.extend([
        "",
        "## Matrix models",
        "",
        "| Model | Module | Role |",
        "|---|---|---|",
    ])
    for model, module, role in MATRIX_MODELS:
        lines.append(f"| `{model}` | `{module}` | {role} |")
    lines.extend([
        "",
        "## Kernel coverage",
        "",
        "These contracts cover maindata behavior used by the kernel-only build.",
        "The cluster-off build, link boundary and NULL boot are enforced natively by",
        "`make check-kernel`; they are not Lean theorems.",
        "",
        "| Module | Contract |",
        "|---|---|",
    ])
    for module, role in KERNEL_COVERAGE:
        lines.append(f"| `{module}` | {role} |")
    reviewed = [(name, data) for family in FAMILIES
                for name, data in families[family]["modules"].items()
                if data["reviewed"]]
    reviewed.sort(key=lambda item: (-item[1]["significance"], item[0]))
    lines.extend([
        "",
        "## Significance review",
        "",
        "Scores follow the [gX proofs significance scale](https://github.com/lanttusoft/gx-proofs/blob/main/SIGNIFICANCE.md): 0–100 measures how much gX",
        "depends on the contract, not proof fame or verification level.",
        "Unreviewed modules use the family default score and do not appear",
        "in this reviewed table.",
        "",
        "| Module | Class | Sig | Target | Achieved | Justification |",
        "|---|---|---:|---|---|---|",
    ])
    for name, data in reviewed:
        lines.append(
            f"| `{name}` | {data['importance']} | {data['significance']} | "
            f"{data['target']} | {data['achieved']} | {data['justification']} |")
    gaps = [(name, data) for family in FAMILIES
            for name, data in families[family]["modules"].items()
            if data["missing"]]
    gaps.sort(key=lambda item: (-item[1]["significance"], item[0]))
    lines.extend([
        "",
        "## Importance gaps",
        "",
        "| Module | Class | Sig | Achieved → Target | Missing evidence / limit |",
        "|---|---|---:|---|---|",
    ])
    if not gaps:
        lines.append("| — | — | — | — | none |")
    for name, data in gaps:
        missing = data["gaps"] or ["declared evidence target not met"]
        lines.append(
            f"| `{name}` | {data['importance']} | {data['significance']} | "
            f"{data['achieved']} → {data['target']} | {'; '.join(missing)} |")
    lines.extend([
        "",
        "## Modules",
        "",
        "| Module | Domain | Class | Sig | Level | Namespace | Thm | Lem | Def | Struct | Ind | Inst | Ax | Opaque |",
        "|---|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for family in FAMILIES:
        for name in sorted(families[family]["modules"]):
            data = families[family]["modules"][name]
            count = data["counts"]
            lines.append(
                f"| `{name}` | `{data['domain']}` | {data['importance']} | "
                f"{data['significance']} | {data['achieved']} | `{data['namespace']}` | "
                f"{count['theorem']} | {count['lemma']} | {count['def']} | "
                f"{count['structure']} | {count['inductive']} | "
                f"{count['instance']} | {count['axiom']} | {count['opaque']} |")
    lines.extend([
        "",
        "## Static trust markers",
        "",
        "- Project `axiom` declarations: " + str(grand["axiom"]),
        "- Project `opaque` declarations: " + str(grand["opaque"]),
        "- `sorry`/`admit`/`native_decide`/`implemented_by`/`extern` matches: "
        + str(sum(len(data["forbidden"]) for family in families.values()
                  for data in family["modules"].values())),
        "",
        "## Math source coverage",
        "",
        "| Source family | Proof | Status |",
        "|---|---|---|",
    ])
    for source, proof, note in MATH_COVERAGE:
        lines.append(f"| {source} | `{proof}` | {note} |")
    lines.extend([
        "",
        "## Namespace notes",
        "",
    ])
    for family in FAMILIES:
        for name in sorted(families[family]["modules"]):
            data = families[family]["modules"][name]
            if not data["namespace"].startswith(family):
                lines.append(
                    f"- `{name}` uses namespace `{data['namespace']}` "
                    f"(outside `{family}`).")
    lines.extend(["", "## Shared namespaces", ""])
    users = {}
    for family in FAMILIES:
        for name, data in families[family]["modules"].items():
            users.setdefault(data["namespace"], []).append(name)
    shared = {namespace: names for namespace, names in users.items() if len(names) > 1}
    if not shared:
        lines.append("- None.")
    else:
        for namespace in sorted(shared):
            lines.append(f"- `{namespace}`: " + ", ".join(f"`{name}`" for name in sorted(shared[namespace])))
    lines.append("")
    return "\n".join(lines).rstrip("\n") + "\n"


def main():
    parser = argparse.ArgumentParser(description="Generate the proof inventory")
    parser.add_argument("--check", action="store_true",
                        help="fail if docs/proof_inventory.md is stale")
    parser.add_argument("--stdout", action="store_true", help="print instead of writing")
    args = parser.parse_args()
    text = markdown()
    if args.stdout:
        print(text, end="")
        return 0
    if args.check:
        current = DOC.read_text() if DOC.exists() else ""
        if current != text:
            print("PROOF_INVENTORY: FAIL — docs/proof_inventory.md is stale; "
                  "run `make proof-inventory`", file=sys.stderr)
            return 1
        print("PROOF_INVENTORY: PASS — catalog is current")
        return 0
    DOC.write_text(text)
    print(f"PROOF_INVENTORY: wrote {DOC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
