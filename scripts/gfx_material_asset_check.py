#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: live editor round-trip pin for reusable per-material surface assets.

import json
import math
import os
import re
import sys
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
OUT = "/tmp/opencode/gfx_material_asset_check"
ASSET = os.path.join(OUT, "roundtrip.material.json")
MATERIALS = ROOT / "resources/tools/gfx_materials.json"
#opencode: per-material map Browse rects on the overrides panel.
MAP_RE = re.compile(
    r"GFX_MATMAP: override=(\d+) albedo=\(([^)]+)\) emissive=\(([^)]+)\)"
)


def editor_log(environment, frames):
    environment = dict(environment)
    environment.setdefault("GX_FRAMES", str(frames))
    env = audit.scene_env(environment, clean=True)
    result = audit.run_scene("--gfx", env, width=None, height=None, fps=None,
                             timeout=120, check=False,
                             extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{log[-3000:]}")
    return log


#opencode: exercise the production editor serializer and apply path in one GL session.
def main():
    os.makedirs(OUT, exist_ok=True)
    if os.path.isfile(ASSET):
        os.remove(ASSET)
    with audit.settings_preserved():
        environment = audit.scene_env({
            "GX_FRAMES": "3",
            "GX_GFX_MATERIAL_ASSET_CHECK": ASSET,
        }, clean=True)
        result = audit.run_scene("--gfx", environment, width=None, height=None,
                                 fps=None, check=False,
                                 extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{log[-4000:]}")
    if "GFX_MATERIAL_ASSET_CHECK: PASS" not in log:
        raise RuntimeError(f"material asset round-trip did not pass\n{log[-4000:]}")
    with open(ASSET, encoding="utf-8") as handle:
        asset = json.load(handle)
    if asset.get("format") != "gx-material-v1":
        raise RuntimeError("material asset format marker is missing")
    if asset.get("name") != "material_asset_probe":
        raise RuntimeError("material asset name did not round-trip")
    if asset.get("description") != "temporary serialization fixture":
        raise RuntimeError("material asset description did not round-trip")
    if "material" in asset or "albedoMapID" in asset:
        raise RuntimeError("material-local target or runtime IDs leaked into the asset")
    surface = asset.get("surface", {})
    if surface.get("emissiveColor") != [17, 34, 51, 255]:
        raise RuntimeError("emissive color did not round-trip")
    if not math.isclose(surface.get("emissive", 0.0), 2.75, abs_tol=1e-6):
        raise RuntimeError("emissive strength did not round-trip")
    if surface.get("albedo") != [91, 123, 177, 255]:
        raise RuntimeError("albedo did not round-trip")
    if not math.isclose(surface.get("roughness", 0.0), 0.27, abs_tol=1e-6):
        raise RuntimeError("roughness did not round-trip")
    if not math.isclose(surface.get("metalness", 0.0), 0.81, abs_tol=1e-6):
        raise RuntimeError("metalness did not round-trip")
    if surface.get("albedoMap") != "fixture/albedo.png":
        raise RuntimeError("albedo map path did not round-trip")
    if surface.get("emissiveMap") != "fixture/emission.png":
        raise RuntimeError("emissive map path did not round-trip")
    #opencode: per-material map browsing — the headless hook assigns a texture
    #   path to override 0 through the same helper the asset browser uses, and
    #   the preset save proves the path persists; a virtual-input click on the
    #   panel Browse button proves the browser opens on that override + kind.
    try:
        materials = json.loads(MATERIALS.read_text())
        cube = next((entry for entry in materials.get("models", [])
                     if entry.get("params", {}).get("materialOverrides")), None)
        if cube is None:
            raise RuntimeError("materials fixture has no override entry")
        fixture = Path(OUT) / "material_map_fixture.json"
        document = json.loads(json.dumps(materials))
        document["models"] = [cube]
        fixture.write_text(json.dumps(document))
        for kind, source, key in ((0, "orb.png", "albedoMap"),
                                  (1, "misc/testcard.png", "emissiveMap")):
            saved = Path(OUT) / f"material_map_{kind}.json"
            if saved.is_file():
                saved.unlink()
            log = editor_log({
                "GX_GFX_PRESET_LOAD": str(fixture),
                "GX_GFX_PRESET_SAVE": str(saved),
                "GX_GFX_MATERIAL_MAP_CHECK": source,
                "GX_GFX_MATERIAL_MAP_KIND": str(kind),
            }, frames=5)
            expected = ("GFX_MATERIAL_MAP_CHECK: PASS override=0 kind="
                        + ("emissive" if kind else "albedo"))
            if expected not in log:
                raise RuntimeError(
                    f"material map assignment failed (kind {kind})\n{log[-3000:]}")
            if not saved.is_file():
                raise RuntimeError("material map preset save missing")
            overrides = json.loads(
                saved.read_text())["models"][0]["params"]["materialOverrides"]
            if overrides[0].get(key) != source:
                raise RuntimeError(
                    f"{key} did not persist: {overrides[0].get(key)!r} != "
                    f"{source!r}")
        probe = editor_log({
            "GX_GFX_PRESET_LOAD": str(fixture),
            "GX_MATMAP_UI_TRACE": "1",
            "GX_GFX_MATMAP_FOCUS": "1",
        }, frames=120)
        rows = MAP_RE.findall(probe)
        if not rows:
            raise RuntimeError("material map Browse trace missing")
        emissive = rows[-1][2]
        bounds = [float(value) for value in emissive.split(",")]
        click_x = int((bounds[0] + bounds[2]) / 2)
        click_y = int((bounds[1] + bounds[3]) / 2)
        click_log = editor_log({
            "GX_GFX_PRESET_LOAD": str(fixture),
            "GX_MATMAP_UI_TRACE": "1",
            "GX_GFX_MATMAP_FOCUS": "1",
            "GX_AI_INPUT": (f"3.0 move {click_x} {click_y} 200;"
                            f"3.3 click {click_x} {click_y}"),
        }, frames=240)
        if "asset browser open kind=1 target=6 index=0 slot=1" not in click_log:
            raise RuntimeError(
                "emissive Browse did not open the material-map browser\n"
                + click_log[-3000:])
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX-MATERIAL-ASSET-CHECK: FAIL — {error}")
        return 1

    print("GFX-MATERIAL-ASSET-CHECK: PASS (save, load, apply, target "
          "isolation, per-material map browse/assign)")


if __name__ == "__main__":
    sys.exit(main())
