#ai: resource-choice and direction convention controls, not local-color certification.
from pathlib import Path
import unittest

import numpy as np
from probe_split_check import expected_route


class SplitResourceContract(unittest.TestCase):
    def test_hidden_local_is_unavailable_not_sky(self):
        source, reason = expected_route(np.array([1, 1, 0, 2]), np.array([2, 3, 0, 0]), np.array([3, 3, 0, 0]))
        np.testing.assert_equal(source, [0, 0, 1, 0])
        np.testing.assert_equal(reason, [2, 2, 0, 3])

    def test_visible_without_response_stays_unavailable(self):
        source, reason = expected_route(np.array([1, 1]), np.array([1, 1]), np.array([0., 3.]))
        np.testing.assert_equal(source, [0, 2])
        np.testing.assert_equal(reason, [4, 0])

    def test_missing_sky_resource_is_explicit_unavailable(self):
        source, reason = expected_route(np.array([0, 1]), np.array([0, 1]), np.array([0., 3.]), sky_ready=False)
        np.testing.assert_equal(source, [0, 2])
        np.testing.assert_equal(reason, [1, 0])

    def test_asymmetric_equirect_inverse(self):
        directions = np.array([[1., 0, 0], [-1., 0, 0], [0, 1., 0], [0, -1., 0],
                               [0, 0, 1.], [0, 0, -1.], [1., 2., 3.], [1., 2., -3.]])
        directions /= np.linalg.norm(directions, axis=-1)[:, None]
        u = np.mod(np.arctan2(directions[:, 2], directions[:, 0]) / (2 * np.pi), 1)
        pitch = np.arccos(directions[:, 1])
        reconstructed = np.stack((np.cos(u * 2 * np.pi) * np.sin(pitch), np.cos(pitch),
                                   np.sin(u * 2 * np.pi) * np.sin(pitch)), axis=-1)
        np.testing.assert_allclose(reconstructed, directions, atol=1e-15)
        np.testing.assert_equal(u[[0, 1, 4, 5]], [0, .5, .25, .75])

    def test_isolated_lifecycle_and_unit_source_contract(self):
        root = Path(__file__).resolve().parents[1]
        draw = (root / "src/renderer/gfx/draw.c").read_text()
        model = (root / "src/renderer/gfx/model.c").read_text()
        shader = (root / "resources/shader/standard.frag").read_text()
        self.assertIn("gfx_probe_proxy_split_enabled() && !gfx->state.ENABLE_SSR", draw)
        self.assertIn("gX_RendererGFX_Sky_DrawTo(gfx, gfx->ssr.skyLut.wrapper.rt, 1);", draw)
        self.assertIn("gX_RHI_Tex_BindOnUnit(gfx->ssr.skyLut.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SKY_LUT);", model)
        self.assertIn("(const gX_I8 *)\"gXProbeSkyDirectionCapture\"), 0);", draw)
        self.assertIn("else if (gXProbeProxyDiagnostic >= 10)", shader)


if __name__ == "__main__":
    unittest.main()
