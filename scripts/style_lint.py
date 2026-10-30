#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: token-aware hardcoded-colour report for gX front-end sources. Report-only
#   by default; --fail opts into a non-zero exit for non-allowlisted literals.
#   The token source is resources/style/theme*.json + src/common/macros.h.
"""Report raw colour literals outside gX's theme/token policy.

This is a report tool, not a blocking style gate. Raw (gX_Color){...} literals
and CSS colours in GUI templates / UI-producing scenes are findings; named
gX_* colours and gui.style.* uses are not. A small built-in allowlist covers
deliberate palettes (GuiTester, material/lighting labs, scene art, CSS demo).
Exit 0 by default; --fail exits 1 when a non-allowlisted literal remains.
"""

import argparse
import json
import sys
from pathlib import Path

import color_inventory


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JSON = "/tmp/opencode/frontend_style.json"
ALLOWLIST = {
    "src/gui/templates/guitester.c":
        "deliberate palette/gradient/CSS cascade demo",
    "src/gui/templates/shaderlab.c":
        "lighting/material swatches, not theme chrome",
    "src/gui/templates/materialslab.c":
        "material swatches, not theme chrome",
    "src/gui/templates/themlab.c":
        "theme lab intentionally displays raw ramps",
    "src/scene/battle.c":
        "battle scene palette, not GUI theme chrome",
    "src/scene/synthvis.c":
        "oscilloscope palette",
    "resources/style/guitester.css":
        "CSS cascade demo",
}


def collect_findings():
    named = color_inventory.load_named_colors()
    named_by_hex = {}
    for name, rgba in named.items():
        named_by_hex.setdefault(color_inventory.rgba_hex(rgba), []).append(name)
    theme_tokens = color_inventory.load_theme_colors()
    findings = []
    allowlisted = 0
    for path in color_inventory.iter_ui_files():
        relative = str(path.relative_to(ROOT))
        for occurrence in color_inventory.scan_file(path, named):
            if occurrence["kind"] not in {"literal", "css"}:
                continue
            allowed = relative in ALLOWLIST
            allowlisted += 1 if allowed else 0
            findings.append({
                "file": relative,
                "screen": occurrence["screen"],
                "line": occurrence["line"],
                "hex": occurrence["hex"],
                "rgba": occurrence["rgba"],
                "kind": occurrence["kind"],
                "text": occurrence["text"],
                "theme": theme_tokens.get(occurrence["hex"], []),
                "named": sorted(named_by_hex.get(occurrence["hex"], [])),
                "allowlisted": allowed,
                "reason": ALLOWLIST.get(relative, ""),
            })
    return findings, allowlisted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", nargs="?", const=DEFAULT_JSON,
                        help=f"write JSON (default: {DEFAULT_JSON})")
    parser.add_argument("--fail", action="store_true",
                        help="exit 1 if a non-allowlisted literal remains")
    args = parser.parse_args()
    findings, allowlisted = collect_findings()
    violations = [item for item in findings if not item["allowlisted"]]
    print(f"STYLE_LINT: {len(findings)} raw colour literals, "
          f"{allowlisted} allowlisted, {len(violations)} findings")
    for item in violations[:50]:
        tokens = item["theme"] + item["named"]
        if len(tokens) > 3:
            hint = f" (matches {', '.join(tokens[:3])} +{len(tokens) - 3})"
        elif tokens:
            hint = f" (matches {', '.join(tokens)})"
        else:
            hint = ""
        print(f"  {item['file']}:{item['line']} {item['hex']}{hint}")
    if len(violations) > 50:
        print(f"  ... {len(violations) - 50} more in JSON")
    if args.json:
        path = Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "summary": {
                "raw_literals": len(findings),
                "allowlisted": allowlisted,
                "findings": len(violations),
            },
            "allowlist": ALLOWLIST,
            "findings": findings,
        }
        path.write_text(json.dumps(payload, indent=2, sort_keys=True),
                        encoding="utf-8")
        print(f"  json={path}")
    if args.fail and violations:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
