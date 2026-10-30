#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compare the exact polynomial C/Lean inverse contract; no IEEE operation-order claim.
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def polynomial(expression):
    def add(left, right, sign=1):
        result = dict(left)
        for key, value in right.items():
            result[key] = result.get(key, 0) + sign * value
        return {key: value for key, value in result.items() if value}

    def visit(node):
        if isinstance(node, ast.Name):
            name = "m" + node.id[1:] if re.fullmatch(r"a[0-3][0-3]", node.id) else node.id
            return {(name,): 1}
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
            if node.value.id in ("m", "b"):
                return {(node.attr,): 1}
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return {(): node.value} if node.value else {}
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            sign = -1 if isinstance(node.op, ast.USub) else 1
            return {key: sign * value for key, value in visit(node.operand).items()}
        if isinstance(node, ast.BinOp):
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, (ast.Add, ast.Sub)):
                return add(left, right, -1 if isinstance(node.op, ast.Sub) else 1)
            if isinstance(node.op, ast.Mult):
                result = {}
                for lk, lv in left.items():
                    for rk, rv in right.items():
                        key = tuple(sorted(lk + rk))
                        result[key] = result.get(key, 0) + lv * rv
                return {key: value for key, value in result.items() if value}
        raise ValueError(f"unsupported contract expression: {ast.dump(node)}")

    return visit(ast.parse(expression.strip(), mode="eval").body)


class InverseContractTests(unittest.TestCase):
    def test_source_correspondence(self):
        c = (ROOT / "src/gxmath/matrix.c").read_text()
        c = c.split("gX_Matrix gX_Matrix_Invert(gX_Matrix mat) {", 1)[1].split("\n}", 1)[0]
        lean = (ROOT.parent / "proofs/GxMath/CofactorInverse.lean").read_text()
        bindings = re.findall(r"\b(a[0-3][0-3])\s*=\s*mat\.m(\d+)\b", c)
        self.assertEqual(len(bindings), 16)
        self.assertEqual(dict(bindings), {f"a{r}{col}": str(4*r+col) for r in range(4) for col in range(4)})
        c_minors = dict(re.findall(r"\b(b\d\d)\s*=\s*([^;]+);", c))
        lean_minors = lean.split("def minors", 1)[1].split("⟨", 1)[1].split("⟩", 1)[0].split(",")
        self.assertEqual(set(c_minors), {f"b{i:02d}" for i in range(12)})
        self.assertEqual(len(lean_minors), 12)
        for index, expression in enumerate(lean_minors):
            self.assertEqual(polynomial(c_minors[f"b{index:02d}"]), polynomial(expression))
        c_det = re.search(r"invDet\s*=\s*1\.0f/\(([^;]+)\);", c).group(1)
        lean_det = lean.split("def determinant", 1)[1].split("let b := minors m", 1)[1].split("def adjugate", 1)[0]
        self.assertEqual(polynomial(c_det), polynomial(lean_det))
        outputs = dict(re.findall(r"result\.m(\d+)\s*=\s*(.+?)\*invDet;", c))
        self.assertEqual(len(re.findall(r"result\.m\d+\s*=", c)), 16)
        adjugate = lean.split("def adjugate", 1)[1].split("def scaleMatrix", 1)[0]
        numerators = dict(re.findall(r"m([0-3][0-3])\s*:=\s*([^,}\n]+)", adjugate))
        self.assertEqual(set(outputs), {str(i) for i in range(16)})
        self.assertEqual(len(numerators), 16)
        for index in range(16):
            self.assertEqual(polynomial(outputs[str(index)]), polynomial(numerators[f"{index//4}{index%4}"]))
        self.assertIn("scaleMatrix (adjugate m) (1/determinant m)", lean)

    def test_sign_and_field_negative_controls(self):
        self.assertNotEqual(polynomial("a00*a11-a01*a10"), polynomial("a00*a11+a01*a10"))
        self.assertNotEqual(polynomial("a00*b11"), polynomial("a01*b11"))

    def test_polynomial_normalization(self):
        self.assertEqual(polynomial("a00*(a01-a02)"), polynomial("m.m01*m.m00-m.m02*m.m00"))


if __name__ == "__main__":
    unittest.main()
