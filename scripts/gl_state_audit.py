#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: entire file — static GL/RHI state-scope and raw-state audit.
"""Audit GL/RHI state scopes without requiring a live graphics context."""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp"}
PAIR_GROUPS = (
    ("GL state", "gX_GL_State_Push", "gX_GL_State_Pop"),
    ("RHI matrix", "gX_RHI_Matrix_Push", "gX_RHI_Matrix_Pop"),
    ("backend matrix", "gX_GL_Matrix_Push", "gX_GL_Matrix_Pop"),
)
RAW_STATE_PATTERN = re.compile(
    r"\b(?:glEnable|glDisable|glBlendFunc(?:Separate)?|glBlendEquation(?:Separate)?|"
    r"glDepthMask|glDepthFunc|glCullFace|glFrontFace|glPolygonMode|"
    r"glBindFramebuffer|glActiveTexture|glBindTexture|"
    r"rlEnable(?:BackfaceCulling|ScissorTest|Framebuffer|DepthTest)|"
    r"rlDisable(?:BackfaceCulling|ScissorTest|Framebuffer|DepthTest)|"
    r"rlSetBlendMode|rlMatrixMode|rlPushMatrix|rlPopMatrix|rlLoadIdentity|"
    r"rlOrtho|rlSetMatrix(?:Modelview|Projection))\s*\("
)


def source_files(root):
    for path in sorted((root / "src").rglob("*")):
        if path.is_file() and path.suffix in SOURCE_EXTENSIONS:
            yield path


def mask_c_source(text):
    masked = list(text)
    state = "normal"
    index = 0
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if char == "/" and next_char == "/":
                masked[index] = " "
                masked[index + 1] = " "
                index += 2
                state = "line"
                continue
            if char == "/" and next_char == "*":
                masked[index] = " "
                masked[index + 1] = " "
                index += 2
                state = "block"
                continue
            if char == '"':
                masked[index] = " "
                index += 1
                state = "string"
                continue
            if char == "'":
                masked[index] = " "
                index += 1
                state = "char"
                continue
            index += 1
            continue
        if state == "line":
            if char == "\n":
                state = "normal"
            else:
                masked[index] = " "
            index += 1
            continue
        if state == "block":
            if char == "*" and next_char == "/":
                masked[index] = " "
                masked[index + 1] = " "
                index += 2
                state = "normal"
                continue
            if char != "\n":
                masked[index] = " "
            index += 1
            continue
        if state in {"string", "char"}:
            if char == "\\":
                masked[index] = " "
                if index + 1 < len(text) and text[index + 1] != "\n":
                    masked[index + 1] = " "
                    index += 2
                else:
                    index += 1
                continue
            if (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "normal"
            elif char != "\n":
                masked[index] = " "
            index += 1
    return "".join(masked)


def function_regions(masked):
    pattern = re.compile(
        r"(?m)^[ \t]*(?:(?:static|extern|inline|const|volatile|unsigned|signed|"
        r"struct\s+\w+|enum\s+\w+|[A-Za-z_]\w*)[ \t\n*&]+)+"
        r"([A-Za-z_]\w*)[ \t\n]*\([^;{}]*\)[ \t\n]*\{"
    )
    regions = []
    for match in pattern.finditer(masked):
        brace = masked.find("{", match.start(), match.end())
        depth = 0
        index = brace
        while index < len(masked):
            if masked[index] == "{":
                depth += 1
            elif masked[index] == "}":
                depth -= 1
                if depth == 0:
                    regions.append((match.group(1), match.start(), brace, index + 1))
                    break
            index += 1
    return regions


def call_positions(masked, name):
    return [match.start() for match in re.finditer(rf"\b{re.escape(name)}\s*\(", masked)]


def line_number(text, position):
    return text.count("\n", 0, position) + 1


def audit_pair_groups(root, findings):
    totals = {name: {push: 0, pop: 0} for name, push, pop in PAIR_GROUPS}
    for path in source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = mask_c_source(text)
        regions = function_regions(masked)
        rel = path.relative_to(root)
        for group, push, pop in PAIR_GROUPS:
            push_positions = call_positions(masked, push)
            pop_positions = call_positions(masked, pop)
            definitions = [(start, brace) for _, start, brace, _ in regions]
            push_positions = [p for p in push_positions if not any(start <= p < brace for start, brace in definitions)]
            pop_positions = [p for p in pop_positions if not any(start <= p < brace for start, brace in definitions)]
            totals[group][push] += len(push_positions)
            totals[group][pop] += len(pop_positions)
            for function, start, brace, end in regions:
                body = masked[brace:end]
                sequence = []
                for position in call_positions(body, push):
                    sequence.append((position, 1))
                for position in call_positions(body, pop):
                    sequence.append((position, -1))
                if not sequence or not any(delta == 1 for _, delta in sequence) or not any(delta == -1 for _, delta in sequence):
                    continue
                depth = 0
                for position, delta in sorted(sequence):
                    depth += delta
                    if depth < 0:
                        findings.append(("WARN", str(rel), line_number(text, brace + position),
                                         f"{group} pop precedes push in {function}()"))
                        break
                if depth != 0:
                    findings.append(("WARN", str(rel), line_number(text, brace),
                                     f"{group} scope is not locally balanced in {function}()"))
        for match in RAW_STATE_PATTERN.finditer(masked):
            if str(rel).startswith("src/rhi/") or str(rel).startswith("src/backend/"):
                continue
            if str(rel) == "src/wrappers/wrappers_gl.c" or str(rel).startswith("src/tools/imgui/"):
                continue
            findings.append(("WARN", str(rel), line_number(text, match.start()),
                             f"raw GL/rlgl state mutation: {match.group(0).strip()}"))
    for group, push, pop in PAIR_GROUPS:
        pushed = totals[group][push]
        popped = totals[group][pop]
        status = "OK" if pushed == popped else "WARN"
        print(f"{status}: {group} push={pushed} pop={popped}")
        if pushed != popped:
            findings.append(("WARN", "src", 0,
                             f"{group} calls are globally unbalanced: push={pushed} pop={popped}"))


def main():
    parser = argparse.ArgumentParser(description="Audit gX GL/RHI state scopes")
    parser.add_argument("--root", default=None, help="project root (default: repository containing this script)")
    parser.add_argument("--uniforms", action="store_true", help="also run shader_uniform_validator.py")
    parser.add_argument("--strict", action="store_true", help="treat raw-state warnings as failures")
    args = parser.parse_args()
    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent
    findings = []
    audit_pair_groups(root, findings)
    for level, path, line, message in findings:
        print(f"{level}: {path}:{line}: {message}")
    if args.uniforms:
        validator = root / "scripts" / "shader_uniform_validator.py"
        result = subprocess.run([sys.executable, str(validator)], cwd=root)
        if result.returncode != 0:
            findings.append(("ERROR", str(validator.relative_to(root)), 0, "uniform validator failed"))
    errors = sum(level == "ERROR" for level, _, _, _ in findings)
    warnings = sum(level == "WARN" for level, _, _, _ in findings)
    print(f"GL STATE AUDIT: errors={errors} warnings={warnings}")
    return 1 if errors or (args.strict and warnings) else 0


if __name__ == "__main__":
    sys.exit(main())
