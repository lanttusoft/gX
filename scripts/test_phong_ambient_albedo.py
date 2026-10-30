#!/usr/bin/env python3
#ai: guard albedo-map contribution in the live raylib Phong ambient path.
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PhongAmbientAlbedoContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shader = (ROOT / "resources/shader/standard.frag").read_text()

    def test_ambient_helper_samples_enabled_albedo_map(self):
        start = self.shader.index("vec3 phong_ambient(vec2 tex)")
        end = self.shader.index("\n}", start) + 2
        helper = self.shader[start:end]
        self.assertIn("enableMapAlbedo == 1 && hasAlbedoMap == 1", helper)
        self.assertIn("albedoMapScale * tex - albedoMapOffset", helper)
        self.assertIn(
            "ambient *= albedoMapFactor * gX_MaterialAlbedo(coord);",
            helper,
        )
        #ai: follow the shared sampler rather than weakening the enabled-albedo contract.
        start = self.shader.index("vec3 gX_MaterialAlbedo(vec2 uv)")
        end = self.shader.index("\n}", start) + 2
        material = self.shader[start:end]
        self.assertIn("vec3 base = texture(albedoMap, uv).rgb;", material)
        self.assertIn("if (terrainBlend == 0) return base;", material)

    def test_live_phong_branch_uses_textured_ambient_helper(self):
        marker = "//opencode: full PBR/Phong path - used on raylib backend"
        live_main = self.shader.index("void main()", self.shader.index(marker))
        phong_branch = self.shader.index(
            "} else if (enablePhong == 1) {", live_main
        )
        branch_end = self.shader.index("float shadowAttn", phong_branch)
        branch = self.shader[phong_branch:branch_end]
        self.assertIn("phong_ambient(texcoord)", branch)
        self.assertNotIn(
            "vec3 ambient = ambientFactor * ambientColor.rgb;", branch
        )

    def test_live_phong_branch_respects_ambient_toggle(self):
        marker = "//opencode: full PBR/Phong path - used on raylib backend"
        live_main = self.shader.index("void main()", self.shader.index(marker))
        phong_branch = self.shader.index(
            "} else if (enablePhong == 1) {", live_main
        )
        branch_end = self.shader.index("float shadowAttn", phong_branch)
        branch = self.shader[phong_branch:branch_end]
        self.assertIn(
            "vec3 ambient = (enableLightingAmbient == 1)\n"
            "            ? phong_ambient(texcoord) : vec3(0.0);",
            branch,
        )


if __name__ == "__main__":
    unittest.main()
