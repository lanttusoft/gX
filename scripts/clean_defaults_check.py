#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: shipped default dependencies cannot rely on local quarantined assets; no resource writes.
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    loader = (ROOT / "src/resource/loaddefaults.c").read_text()
    editor = (ROOT / "src/tools/gfx/gfx_editor.cpp").read_text()
    water = (ROOT / "src/renderer/water.c").read_text()
    scene = (ROOT / "src/renderer/gfx/scene.c").read_text()
    assert re.search(r's_normalTexturePath\[256\]\s*=\s*"resources/texture/texture_normal.png"', editor), "editor normal default drift"
    load = water[water.index("gX_I32 gX_RendererWater_Load("):water.index("gX_I32 gX_RendererWater_Unload(")]
    assert 'GX_PATH("texture", "texture_normal.png")' in load, "water normal default drift"
    specs = scene[scene.index("static const gX_DemoModelSpec specs[]"):scene.index("//opencode: --showcase-water — float")]
    assert '"primitive/cylinder.glb"' in specs, "project stand-in missing"
    for path in ["resources/texture/texture_normal.png", "resources/model/primitive/cylinder.glb"]:
        assert (ROOT / path).is_file(), "missing shipped default: " + path
        subprocess.run(["git", "ls-files", "--error-unmatch", "--", path], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
    print("CLEAN_DEFAULTS: PASS — shipped preload/editor/water/demo dependencies")


if __name__ == "__main__":
    main()
