#!/usr/bin/env python3
#ai: battle scene gate — the BATTLE backdrop is composited under transparent
#   3D GFX, postprocessed, then topped with the native battle HUD.

import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit

SCENE = "--battle"
FRAMES = 90
SHOT_FRAME = 60
PRESET_MARKER = "BG_PRESET: BATTLE"
WAVE_PRESET_MARKER = "BG_PRESET: TITLE palette mode 3"
CLI_MARKER = "BG_PALETTE: mode=3"
GFX_MARKER = "BATTLE_GFX: models="
PP_MARKER = "BATTLE_PP: preset=VIGNETTE"
MIN_MEAN = 5.0
MIN_STD = 8.0

#ai: the header panel is opaque (4,10,26,255): y 0.018..0.086, x 0.045..0.955
#   at 1024x768 -> the band below. The arena sample excludes the HUD panels.
HEADER_BAND = (20, 60, 60, 960)
ARENA_BAND = (100, 450, 60, 960)
MAX_HEADER_LUMA = 0.12
HEADER_ARENA_RATIO = 0.85
MIN_MODE_DELTA = 8.0
MIN_ANIM_DELTA = 1.5
MIN_WAVE_DELTA = 1.5
MIN_HISTORY_DELTA = 0.5
MIN_IMPACT_DELTA = 0.25
MIN_GFX_DELTA = 1.0
MIN_PP_DELTA = 0.5
MIN_EDGE_DELTA = 2.0
MIN_GRID_CONTRAST = 0.005
GFX_BAND = (100, 460, 80, 945)
PP_BAND = (90, 645, 40, 984)
INTENT_HUD_BAND = (510, 536, 358, 666)
INTENT_ARENA_BAND = (190, 430, 480, 660)
#ai: isolate the queued-intent target-cell ring below the player model.
INTENT_CELL_BAND = (400, 455, 530, 650)
REACTION_ARENA_BAND = (150, 450, 480, 690)
#ai: camera-projected glyphs occupy the exposed sides above the deck.
SPACE_PROJECTION_BAND = (80, 280, 55, 345)
SPACE_PROJECTION_RIGHT_BAND = (80, 280, 680, 970)
#ai: only the lower outside corners should reveal the lava well.
LAVA_LEFT_BAND = (350, 530, 0, 92)
LAVA_RIGHT_BAND = (350, 530, 932, 1024)
#ai: sample the lower target-ring arc below the actor and above the HUD.
RIPPLE_ARENA_BAND = (390, 455, 525, 660)
#ai: exposed side spans track the octagon footprint; the grid probe avoids actors.
OCTAGON_LEFT_BAND = (300, 400, 40, 190)
OCTAGON_RIGHT_BAND = (300, 400, 830, 980)
GRID_LINE_BAND = (404, 408, 180, 310)
GRID_GAP_ABOVE_BAND = (400, 404, 180, 310)
GRID_GAP_BELOW_BAND = (408, 412, 180, 310)
MIN_INTENT_HUD_DELTA = 1.0
MIN_INTENT_ARENA_DELTA = 0.35
MIN_INTENT_CELL_DELTA = 1.0
MIN_REACTION_ARENA_DELTA = 0.25
MIN_RIPPLE_ARENA_DELTA = 0.5
MIN_SPACE_PROJECTION_COVERAGE = 0.015
MIN_LAVA_COVERAGE = 0.04


def capture(temp_root, tag, args=(), frames=FRAMES, shot=SHOT_FRAME,
            overrides=None):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
    }, clean=True)
    if overrides:
        environment.update(overrides)
    with audit.settings_preserved():
        result = audit.run_scene(SCENE, environment, width=None, height=None,
                                 fps=None, frames=frames, shot_frame=shot,
                                 screenshot_dir=directory, timeout=180,
                                 check=True, extra_args=args)
    log = audit.strip_ansi(result.stdout or "")
    shot_path = audit.newest_shot(directory)
    if shot_path is None:
        raise RuntimeError(f"{tag}: capture missing")
    pixels = np.asarray(Image.open(shot_path).convert("RGB"), dtype=np.float32)
    return pixels, log


def band_luma(pixels, band):
    y0, y1, x0, x1 = band
    return float(pixels[y0:y1, x0:x1].mean() / 255.0)


#ai: assert the procedural sphere and lava remain visibly distinct by color.
def band_color_coverage(pixels, band, kind):
    y0, y1, x0, x1 = band
    sample = pixels[y0:y1, x0:x1].astype(np.float32)
    red, green, blue = sample[:, :, 0], sample[:, :, 1], sample[:, :, 2]
    if kind == "cyan":
        mask = (blue > red * 1.25) & (green > red * 1.1) & (blue > 35.0)
    else:
        mask = (red > green * 1.25) & (green > blue * 1.15) & (red > 28.0)
    return float(mask.mean())


#ai: compare a named viewport region without allocating a full-frame diff.
def band_delta(first, second, band):
    y0, y1, x0, x1 = band
    return float(np.abs(first[y0:y1, x0:x1] - second[y0:y1, x0:x1]).mean())


def check_scene(temp_root):
    base, log = capture(temp_root, "battle_base")
    if PRESET_MARKER not in log:
        raise RuntimeError("battle scene did not load the BATTLE preset")
    if GFX_MARKER not in log:
        raise RuntimeError("battle scene did not build its 3D GFX arena")
    if PP_MARKER not in log:
        raise RuntimeError("battle scene did not configure its postprocess chain")
    if float(base.mean()) < MIN_MEAN or float(base.std()) < MIN_STD:
        raise RuntimeError(
            f"battle frame is blank (mean {base.mean():.1f}, "
            f"std {base.std():.1f})")
    header = band_luma(base, HEADER_BAND)
    arena = band_luma(base, ARENA_BAND)
    if header > MAX_HEADER_LUMA or header > arena * HEADER_ARENA_RATIO:
        raise RuntimeError(
            f"HUD header missing or not dark (header {header:.3f}, "
            f"arena {arena:.3f})")

    projection_coverage = band_color_coverage(
        base, SPACE_PROJECTION_BAND, "cyan")
    projection_right_coverage = band_color_coverage(
        base, SPACE_PROJECTION_RIGHT_BAND, "warm")
    if (projection_coverage < MIN_SPACE_PROJECTION_COVERAGE
            or projection_right_coverage < 0.001):
        raise RuntimeError(
            f"spherical background projections are missing "
            f"(cyan {projection_coverage:.3f}, "
            f"warm {projection_right_coverage:.3f})")
    lava_coverage = (
        band_color_coverage(base, LAVA_LEFT_BAND, "warm")
        + band_color_coverage(base, LAVA_RIGHT_BAND, "warm")) * 0.5
    if lava_coverage < MIN_LAVA_COVERAGE:
        raise RuntimeError(
            f"lava is not visible below the octagonal edge "
            f"(warm coverage {lava_coverage:.3f})")

    no_gfx, _ = capture(temp_root, "battle_no_gfx",
                        overrides={"GX_BATTLE_TEST_NO_GFX": "1"})
    edge_delta = (band_delta(base, no_gfx, OCTAGON_LEFT_BAND)
                  + band_delta(base, no_gfx, OCTAGON_RIGHT_BAND)) * 0.5
    if edge_delta < MIN_EDGE_DELTA:
        raise RuntimeError(
            f"octagonal arena edge is not visible (delta {edge_delta:.2f})")
    grid_line = band_luma(base, GRID_LINE_BAND)
    grid_gap = (band_luma(base, GRID_GAP_ABOVE_BAND)
                + band_luma(base, GRID_GAP_BELOW_BAND)) * 0.5
    grid_contrast = grid_line - grid_gap
    if grid_contrast < MIN_GRID_CONTRAST:
        raise RuntimeError(
            f"nine-by-nine board grid is not visible "
            f"(line contrast {grid_contrast:.3f})")
    gfx_delta = float(np.abs(base[GFX_BAND[0]:GFX_BAND[1],
                                 GFX_BAND[2]:GFX_BAND[3]] -
                             no_gfx[GFX_BAND[0]:GFX_BAND[1],
                                    GFX_BAND[2]:GFX_BAND[3]]).mean())
    if gfx_delta < MIN_GFX_DELTA:
        raise RuntimeError(
            f"3D GFX layer did not affect the arena image (delta {gfx_delta:.2f})")

    no_pp, _ = capture(temp_root, "battle_no_pp",
                       overrides={"GX_BATTLE_TEST_NO_PP": "1"})
    pp_delta = float(np.abs(base[PP_BAND[0]:PP_BAND[1],
                                PP_BAND[2]:PP_BAND[3]] -
                            no_pp[PP_BAND[0]:PP_BAND[1],
                                  PP_BAND[2]:PP_BAND[3]]).mean())
    if pp_delta < MIN_PP_DELTA:
        raise RuntimeError(
            f"postprocess chain did not affect the scene image (delta {pp_delta:.2f})")

    intent_base, _ = capture(temp_root, "battle_intent_base", frames=24, shot=8)
    intent, _ = capture(temp_root, "battle_intent", frames=24, shot=8,
                        overrides={"GX_BATTLE_TEST_INTENT": "1"})
    intent_hud_delta = float(np.abs(
        intent[INTENT_HUD_BAND[0]:INTENT_HUD_BAND[1],
               INTENT_HUD_BAND[2]:INTENT_HUD_BAND[3]] -
        intent_base[INTENT_HUD_BAND[0]:INTENT_HUD_BAND[1],
                    INTENT_HUD_BAND[2]:INTENT_HUD_BAND[3]]).mean())
    if intent_hud_delta < MIN_INTENT_HUD_DELTA:
        raise RuntimeError(
            f"queued enemy intent is not visible in the HUD "
            f"(delta {intent_hud_delta:.2f})")
    intent_arena_delta = float(np.abs(
        intent[INTENT_ARENA_BAND[0]:INTENT_ARENA_BAND[1],
               INTENT_ARENA_BAND[2]:INTENT_ARENA_BAND[3]] -
        intent_base[INTENT_ARENA_BAND[0]:INTENT_ARENA_BAND[1],
                    INTENT_ARENA_BAND[2]:INTENT_ARENA_BAND[3]]).mean())
    if intent_arena_delta < MIN_INTENT_ARENA_DELTA:
        raise RuntimeError(
            f"queued enemy intent has no visible GFX cue "
            f"(delta {intent_arena_delta:.2f})")
    intent_cell_delta = float(np.abs(
        intent[INTENT_CELL_BAND[0]:INTENT_CELL_BAND[1],
               INTENT_CELL_BAND[2]:INTENT_CELL_BAND[3]] -
        intent_base[INTENT_CELL_BAND[0]:INTENT_CELL_BAND[1],
                    INTENT_CELL_BAND[2]:INTENT_CELL_BAND[3]]).mean())
    if intent_cell_delta < MIN_INTENT_CELL_DELTA:
        raise RuntimeError(
            f"queued enemy intent has no visible target-cell telegraph "
            f"(delta {intent_cell_delta:.2f})")

    reaction_base, _ = capture(temp_root, "battle_reaction_base",
                               frames=24, shot=8)
    magic_reaction, _ = capture(temp_root, "battle_magic_reaction",
                                frames=24, shot=8,
                                overrides={"GX_BATTLE_TEST_IMPACT": "magic"})
    reaction_delta = float(np.abs(
        magic_reaction[REACTION_ARENA_BAND[0]:REACTION_ARENA_BAND[1],
                       REACTION_ARENA_BAND[2]:REACTION_ARENA_BAND[3]] -
        reaction_base[REACTION_ARENA_BAND[0]:REACTION_ARENA_BAND[1],
                      REACTION_ARENA_BAND[2]:REACTION_ARENA_BAND[3]]).mean())
    if reaction_delta < MIN_REACTION_ARENA_DELTA:
        raise RuntimeError(
            f"confirmed elemental impact has no visible arena reaction "
            f"(delta {reaction_delta:.2f})")
    ripple_delta = float(np.abs(
        magic_reaction[RIPPLE_ARENA_BAND[0]:RIPPLE_ARENA_BAND[1],
                       RIPPLE_ARENA_BAND[2]:RIPPLE_ARENA_BAND[3]] -
        reaction_base[RIPPLE_ARENA_BAND[0]:RIPPLE_ARENA_BAND[1],
                      RIPPLE_ARENA_BAND[2]:RIPPLE_ARENA_BAND[3]]).mean())
    if ripple_delta < MIN_RIPPLE_ARENA_DELTA:
        raise RuntimeError(
            f"confirmed elemental impact has no visible floor ripple "
            f"(delta {ripple_delta:.2f})")

    alt, alt_log = capture(temp_root, "battle_mode3",
                           args=("--bg-palette-mode", "3"))
    if CLI_MARKER not in alt_log:
        raise RuntimeError("--bg-palette-mode did not reach the battle scene")
    mode_base = np.concatenate((base[105:215, 60:350].reshape(-1, 3),
                                base[105:215, 670:960].reshape(-1, 3)))
    mode_alt = np.concatenate((alt[105:215, 60:350].reshape(-1, 3),
                               alt[105:215, 670:960].reshape(-1, 3)))
    mode_delta = float(np.abs(mode_base - mode_alt).mean())
    if mode_delta < MIN_MODE_DELTA:
        raise RuntimeError(
            f"battle palette override did not change the backdrop "
            f"(delta {mode_delta:.2f})")

    wave2, wave_log = capture(temp_root, "battle_wave2",
                              overrides={"GX_BATTLE_TEST_ADVANCE_WAVE": "1"})
    if WAVE_PRESET_MARKER not in wave_log:
        raise RuntimeError("second battle wave did not select the TITLE backdrop")
    wave_delta = float(np.abs(base[90:300, 60:960] -
                              wave2[90:300, 60:960]).mean())
    if wave_delta < MIN_WAVE_DELTA:
        raise RuntimeError(
            f"wave backdrop did not visibly change (delta {wave_delta:.2f})")

    impacts, _ = capture(temp_root, "battle_impacts",
                         overrides={"GX_BATTLE_TEST_IMPACT": "1"})
    history_delta = float(np.abs(base[90:225, 370:640] -
                                 impacts[90:225, 370:640]).mean())
    if history_delta < MIN_HISTORY_DELTA:
        raise RuntimeError(
            f"recent-impact history did not render (delta {history_delta:.2f})")
    impact_delta = float(np.abs(base[165:240, 650:800] -
                                impacts[165:240, 650:800]).mean())
    if impact_delta < MIN_IMPACT_DELTA:
        raise RuntimeError(
            f"floating damage effect did not render (delta {impact_delta:.2f})")

    later, _ = capture(temp_root, "battle_later", frames=240, shot=210)
    anim_base = np.concatenate((base[105:215, 60:350].reshape(-1, 3),
                                base[105:215, 670:960].reshape(-1, 3)))
    anim_later = np.concatenate((later[105:215, 60:350].reshape(-1, 3),
                                 later[105:215, 670:960].reshape(-1, 3)))
    anim_delta = float(np.abs(anim_base - anim_later).mean())
    if anim_delta < MIN_ANIM_DELTA:
        raise RuntimeError(
            f"battle backdrop does not animate (delta {anim_delta:.2f})")
    return (header, arena, edge_delta, grid_contrast, gfx_delta, pp_delta,
            intent_hud_delta,
            intent_arena_delta, intent_cell_delta, reaction_delta,
            ripple_delta, projection_coverage, projection_right_coverage,
            lava_coverage, mode_delta, wave_delta, history_delta,
            impact_delta, anim_delta)


def main():
    if not audit.BIN.is_file():
        print("GFX_BATTLE_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_battle_",
                                         dir=str(audit.SCRATCH.parent)) as temp:
            (header, arena, edge_delta, grid_contrast, gfx_delta, pp_delta,
             intent_hud_delta,
             intent_arena_delta, intent_cell_delta, reaction_delta,
             ripple_delta, projection_coverage, projection_right_coverage,
             lava_coverage, mode_delta, wave_delta, history_delta,
             impact_delta, anim_delta) = check_scene(Path(temp))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_BATTLE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_BATTLE_CHECK: PASS BATTLE/GFX/PP + HUD header "
          f"{header:.3f} vs arena {arena:.3f} + palette mode delta "
          f"{mode_delta:.1f} + octagon {edge_delta:.1f} + "
          f"grid contrast {grid_contrast:.3f} + 3D GFX {gfx_delta:.1f} + "
          f"PP {pp_delta:.1f}"
          f" + intent HUD {intent_hud_delta:.1f} + intent GFX {intent_arena_delta:.1f}"
          f" + intent cell {intent_cell_delta:.1f}"
          f" + impact reaction {reaction_delta:.2f} + floor ripple {ripple_delta:.2f}"
          f" + sphere {projection_coverage:.3f}/{projection_right_coverage:.3f}"
          f" + lava {lava_coverage:.3f}"
          f" + wave {wave_delta:.1f}"
          f" + history {history_delta:.1f}"
          f" + float {impact_delta:.1f} + anim {anim_delta:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
