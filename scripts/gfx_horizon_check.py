#!/usr/bin/env python3
#opencode: fog/sky horizon pin — mirror-horizon seam smoothness on
#   --showcase-water plus the night-murk profile on --ledrow, both with
#   exposure locked. Central-column median profiles reject stars; the
#   legitimate slab horizon edge is excluded from the smoothness zone.
#
#   Bands refreshed 2026-09-26: 85701e8 made the display transfer encode
#   unconditional (AgX output is linear, so linearToSrgb now applies after it),
#   which intentionally raised scene luminance — the old murk/low bands
#   (10..45 / 20..120) read stale. Water waves are wall-clock animated
#   (run-to-run profile spread up to ~7 in the sampled bands, ~26 worst-case),
#   so the bands carry margins and the seam ignores the horizon edge.

import subprocess
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN

#opencode: absolute geometry in this gate's assertions — keep the engine size.
audit.keep_engine_size()
OUT = Path("/tmp/opencode/horizon_check")
HALF_WIDTH = 100

WATER_SKY_ROWS = (60, 150)
WATER_ZONE_ROWS = (150, 350)
WATER_LOW_ROWS = (400, 600)
WATER_SEAM_MAX = 12.0     # after excluding the horizon edge (+/- 20 rows)
WATER_EDGE_EXCLUDE = 20
WATER_SKY_MIN = 80.0
WATER_LOW_MIN = 100.0
WATER_LOW_MAX = 170.0

#opencode: re-baselined 2026-10-14 — 291c193 (luminance-anchored sky
#   contrast) deliberately stopped clipping the night sky to black, so the
#   ledrow zenith now carries a dim atmospheric glow (measured 61.0 vs 0.0
#   before; the WIP sky work does not change it). The cap still fails a
#   lit/day zenith — the daytime band reads >150.
LED_ZENITH_ROWS = (60, 150)
LED_MURK_ROWS = (170, 330)
LED_ZENITH_MAX = 80.0
LED_MURK_MIN = 70.0
LED_MURK_MAX = 115.0
LED_MURK_STEP_MAX = 6.0


def capture(scene):
    directory = OUT / "raw" / scene.strip("-")
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob("gXss_*.png"):
        stale.unlink()
    #opencode: no explicit size — the absolute row bands assume the engine's
    #   fitted client geometry; audit_common keeps the window hidden.
    env = audit.scene_env({"GX_NO_AE": "1"}, clean=True)
    audit.run_scene(scene, env, width=None, height=None, fps=None,
                    screenshot_dir=directory, shot_frame=180, check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{scene}: no screenshot produced")
    frame = audit.load_rgb(shot)
    if not np.isfinite(frame).all():
        raise RuntimeError(f"{scene}: non-finite pixels")
    return frame


def profile(frame):
    band = frame[:, frame.shape[1] // 2 - HALF_WIDTH:
                 frame.shape[1] // 2 + HALF_WIDTH, :]
    return np.median(band.mean(axis=2), axis=1)


def main():
    water = profile(capture("--showcase-water"))
    led = profile(capture("--ledrow"))
    zone = np.abs(np.diff(water[WATER_ZONE_ROWS[0]:WATER_ZONE_ROWS[1]]))
    edge = int(zone.argmax())
    #opencode: the horizon edge is a legitimate composition boundary, not a
    #   reflection seam; measure smoothness around it.
    mask = np.ones_like(zone, dtype=bool)
    mask[max(0, edge - WATER_EDGE_EXCLUDE):edge + WATER_EDGE_EXCLUDE + 1] = False
    water_seam = float(zone[mask].max())
    water_edge_row = WATER_ZONE_ROWS[0] + edge
    water_sky = float(water[WATER_SKY_ROWS[0]:WATER_SKY_ROWS[1]].mean())
    water_low = float(water[WATER_LOW_ROWS[0]:WATER_LOW_ROWS[1]].mean())
    led_zenith = float(led[LED_ZENITH_ROWS[0]:LED_ZENITH_ROWS[1]].mean())
    led_murk = led[LED_MURK_ROWS[0]:LED_MURK_ROWS[1]]
    led_murk_mean = float(led_murk.mean())
    led_murk_step = float(np.abs(np.diff(led_murk)).max())
    print("                    value  gate")
    print(f"water seam step     {water_seam:6.2f}  < {WATER_SEAM_MAX}")
    print(f"water horizon edge  {water_edge_row:6d}  (excluded +/- {WATER_EDGE_EXCLUDE})")
    print(f"water sky           {water_sky:6.1f}  > {WATER_SKY_MIN}")
    print(f"water low           {water_low:6.1f}  in [{WATER_LOW_MIN}, {WATER_LOW_MAX}]")
    print(f"led zenith          {led_zenith:6.1f}  < {LED_ZENITH_MAX}")
    print(f"led murk            {led_murk_mean:6.1f}  in [{LED_MURK_MIN}, {LED_MURK_MAX}]")
    print(f"led murk step       {led_murk_step:6.2f}  < {LED_MURK_STEP_MAX}")
    if water_seam > WATER_SEAM_MAX:
        raise RuntimeError("water horizon seam above gate")
    if water_sky < WATER_SKY_MIN:
        raise RuntimeError("water sky missing")
    if not WATER_LOW_MIN < water_low < WATER_LOW_MAX:
        raise RuntimeError("water low band outside gate")
    if led_zenith > LED_ZENITH_MAX:
        raise RuntimeError("led zenith not night")
    if not LED_MURK_MIN < led_murk_mean < LED_MURK_MAX:
        raise RuntimeError("led murk band outside gate")
    if led_murk_step > LED_MURK_STEP_MAX:
        raise RuntimeError("led murk seam above gate")
    print("HORIZON-CHECK: PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"HORIZON-CHECK: FAIL — {exc}")
        sys.exit(1)
