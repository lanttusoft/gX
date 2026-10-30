#!/usr/bin/env python3
#ai: tone-map and gamma controls must disclose their composite-pass dependency.
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GfxTonemapControlsContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/tools/gfx/gfx_editor.cpp").read_text()
        start = source.index("static gX_I0 gfx_look_panel(")
        end = source.index("static gX_I0 gfx_quality_panel(", start)
        cls.panel = source[start:end]

    def test_tone_controls_are_disabled_without_bloom_composite(self):
        disabled = self.panel.index(
            "ImGui::BeginDisabled(!gfx->state.ENABLE_BLOOM);"
        )
        tone_mapper = self.panel.index('ImGui::Combo("tone mapper"')
        gamma = self.panel.index('ImGui::Checkbox("gamma correction"')
        enabled = self.panel.index("ImGui::EndDisabled();", gamma)
        self.assertLess(disabled, tone_mapper)
        self.assertLess(tone_mapper, gamma)
        self.assertLess(gamma, enabled)

    def test_raw_blit_path_explains_why_controls_are_inactive(self):
        self.assertRegex(
            self.panel,
            r'if\s*\(!gfx->state\.ENABLE_BLOOM\)\s*'
            r'ImGui::TextDisabled\("Tone mapping and gamma require the '
            r'HDR composite; enable it in Bloom\."\);',
        )


if __name__ == "__main__":
    unittest.main()
