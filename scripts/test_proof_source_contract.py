#ai: source-drift guards for the kernel, RHI and temporal proof models.
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


class ProofSourceContractTests(unittest.TestCase):
    def test_maindata_switch_flags_and_responders(self):
        header = read("src/main.h")
        self.assertIn("gX_I1       masterIndexSwitch;", header)
        self.assertIn("gX_I1       masterIndexSwitched;", header)
        self.assertIn("gX_I1       masterIndexSuccessful;", header)
        update = read("src/main_update.c")
        for contract in (
            "maindata.masterIndexSwitch = true;",
            "maindata.masterIndexSwitched = false;",
            "gui.masterIndexSwitchResponse",
            "input.masterIndexSwitchResponse",
            "renderer.masterIndexSwitchResponse",
            "scene.masterIndexSwitchResponse",
            "camera.masterIndexSwitchResponse",
            "maindata.masterIndexSuccessful = true;",
        ):
            self.assertIn(contract, update)

    def test_moduleinfo_mirror_and_kernel_only_branch(self):
        load = read("src/main_load.c")
        for contract in (
            "maindata.moduleinfo.rendererExists = (gX_ENABLE_RENDERER == 1);",
            "maindata.moduleinfo.gamedataExists = (gX_ENABLE_GAME == 1);",
            "maindata.moduleinfo.sceneExists    = (gX_ENABLE_SCENE == 1);",
            "kernel-only — no scene switch",
        ):
            self.assertIn(contract, load)

    def test_refresh_ladder_and_reset_defaults(self):
        load = read("src/main_load.c")
        config = read("src/common/config.h")
        self.assertIn("maindata.renderWidth = (gX_U16)(maindata.renderScale*maindata.screenWidth);", load)
        self.assertIn("maindata.renderHeight = (gX_U16)(maindata.renderScale*maindata.screenHeight);", load)
        self.assertIn("maindata.renderScale = 1.0f;", load)
        self.assertIn("#define MAINDATA_DEFAULT_SCREENWIDTH", config)
        self.assertIn("#define MAINDATA_DEFAULT_SCREENHEIGHT", config)

    def test_fbo_cache_and_delete_lifecycle(self):
        fbo = read("src/rhi/rhi_fbo.c")
        for contract in (
            "static gX_RHI_FBO g_drawFbo = 0;",
            "static gX_RHI_FBO g_readFbo = 0;",
            "if (fbo == g_drawFbo && fbo == g_readFbo)",
            "glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);",
            "glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);",
            "if (g_drawFbo == fbo) g_drawFbo = 0;",
            "if (g_readFbo == fbo) g_readFbo = 0;",
        ):
            self.assertIn(contract, fbo)

    def test_rhi_blend_mode_mapping(self):
        state = read("src/rhi/rhi_state.c")
        for contract in (
            "case gX_RHI_BLEND_ALPHA:",
            "case gX_RHI_BLEND_ADDITIVE:",
            "case gX_RHI_BLEND_MULTIPLIED:",
            "case gX_RHI_BLEND_ADD_COLORS:",
            "case gX_RHI_BLEND_SUBTRACT_COLORS:",
            "case gX_RHI_BLEND_ALPHA_PREMULTIPLY:",
            "gX_RHI_Blend_Func_Separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,",
            "gX_RHI_Blend_Equation(GL_FUNC_SUBTRACT);",
        ):
            self.assertIn(contract, state)

    def test_temporal_history_lifecycle_and_snapshots(self):
        history = read("src/renderer/gfx/temporal_history.c")
        for contract in (
            "history->historyValid = false;",
            "history->clearPending = true;",
            "history->passActive = false;",
            "history->parity = !history->parity;",
            "history->frameIndex = history->frameIndex >= 2147483647",
            "return history->parity ? targetB : targetA;",
        ):
            self.assertIn(contract, history)
        taa = read("src/renderer/gfx/taa.h")
        taa_c = read("src/renderer/gfx/taa.c")
        self.assertIn("gX_RendererGFX_RenderTexture historyMap;", taa)
        self.assertIn("hybridHistoryActive;", taa)
        self.assertIn("gfx->taa.temporal.historyValid && maindata.frameCount != gfx->taa.lastResolveFrame + 1", taa_c)


if __name__ == "__main__":
    unittest.main()
