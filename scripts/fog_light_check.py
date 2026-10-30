#!/usr/bin/env python3
#opencode: fog+light joint verification matrix — isolated fog/light coupling rows.
#   The feature matrix proves fog and sun each move pixels alone; this script
#   proves they interact correctly: inscatter scales with sun radiance, tint
#   follows sun color, density/scatter are monotonic, point lights feed the
#   march, and shadows occlude it. Chain-engaged baselines throughout
#   (fog density 0 engages composite without changing pixels — never compare
#   across chain states per docs/audit_backlog.md conventions).
#
#   Usage:
#     DISPLAY=:1 python3 scripts/fog_light_check.py
#     DISPLAY=:1 python3 scripts/fog_light_check.py /tmp/opencode/foglight
#
#   Exit 0 iff all rows PASS. GPU/display required — use `make fog-light-check`.

import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/opencode/foglight")
CAPTURE_DIR = OUT / "raw"
TIMEOUT = 120

BASE = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0",
    "GX_T_SSR": "0", "GX_T_TAA": "0", "GX_T_FXAA": "0",
    "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_MOTIONBLUR": "0", "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0",
    "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    #opencode: chain engager (neutral): volume fog at zero density engages the
    #   composite chain without changing a pixel.
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
}

LIT = {"GX_T_SSR": "1", "GX_T_SHADOWS": "1"}
FOG01 = {"GX_T_FOG": "1", "GX_FOG_DENSITY": "0.01"}

# (name, scene, off-extra, on-extra, lo, hi, note[, dthresh[, grade_base]])
# grade_base=True rows use GRADE_BASE (no fog-zero chain engager): grade
#   scenes bring their own fog, and the engager would zero it in both arms.
GRADE_BASE = {k: v for k, v in BASE.items()
              if k not in ("GX_T_FOG", "GX_FOG_DENSITY")}
ROWS = [
    ("fog_on", "--showcase",
     {**LIT, **FOG01, "GX_FOG_DENSITY": "0"},
     {**LIT, **FOG01},
     0.005, 0.95, "fog veil over lit baseline"),
    ("fog_density", "--showcase",
     {**LIT, **FOG01, "GX_FOG_DENSITY": "0.005"},
     {**LIT, **FOG01, "GX_FOG_DENSITY": "0.03"},
     0.005, 0.95, "monotonic density response"),
    ("fog_scatter", "--showcase",
     {**LIT, **FOG01, "GX_FOG_SCATTER": "0.1"},
     {**LIT, **FOG01, "GX_FOG_SCATTER": "2.0"},
     0.002, 0.95, "scattering bias moves inscatter"),
    ("sun_in_fog", "--showcase",
     {**LIT, **FOG01, "GX_T_SUNBRIGHTNESS": "0.25"},
     {**LIT, **FOG01, "GX_T_SUNBRIGHTNESS": "2.0"},
     0.005, 0.95, "fog inscatter scales with sun radiance"),
    ("suncolor_in_fog", "--showcase",
     {**LIT, **FOG01, "GX_T_SUNCOLOR": "255,255,255"},
     {**LIT, **FOG01, "GX_T_SUNCOLOR": "255,128,64"},
     0.005, 0.95, "fog tint follows sun color"),
    ("fogpoints", "--showcase",
     {**LIT, **FOG01, "GX_EXTRA_LIGHTS": "2", "GX_T_FOGPOINTS": "0"},
     {**LIT, **FOG01, "GX_EXTRA_LIGHTS": "2", "GX_T_FOGPOINTS": "1"},
     0.001, 0.60, "point-light inscatter feeds march", 2),
    ("fog_shadows", "--showcase",
     {"GX_T_SSR": "1", "GX_T_SHADOWS": "0", **FOG01},
     {"GX_T_SSR": "1", "GX_T_SHADOWS": "1", **FOG01},
     0.005, 0.95, "shadow occlusion visible inside fog"),
    ("sun_no_fog", "--showcase",
     {"GX_T_SUNBRIGHTNESS": "0.25"},
     {"GX_T_SUNBRIGHTNESS": "2.0"},
     0.02, 0.95, "light path sanity without fog"),
    #opencode: grade regression pins — env-gated mood grades have no other
    #   gate; a dead hook or clobbered default collapses these toward 0.
    #   Calibrated 2026-09-18 (sh 0.31, gta 0.63, noon 0.53); gates stay
    #   wide on purpose (pins catch collapse/wash, not look drift).
    ("sh_grade", "--forest", {}, {"GX_SILENTHILL": "1"},
     0.05, 0.95, "SH daylight whiteout pin", 10, True),
    ("gta_grade", "--ledrow", {}, {"GX_GTA3": "1"},
     0.02, 0.95, "GTA neon pin", 10, True),
    ("noon_grade", "--showcase", {}, {"GX_NOON": "1"},
     0.05, 0.95, "noon-haze pin", 10, True),
]


def label_top_right(path, text):
    from PIL import Image, ImageDraw
    im = Image.open(path).convert("RGB")
    dr = ImageDraw.Draw(im)
    t = str(text)
    tw = dr.textlength(t)
    x, y = im.width - tw - 12, 10
    dr.rectangle([x - 4, y - 2, im.width - 8, y + 14], fill=(0, 0, 0))
    dr.text((x, y), t, fill=(255, 255, 255))
    im.save(path)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    requests = {}
    row_meta = {}
    for row in ROWS:
        name, scene, off_extra, on_extra, lo, hi = row[:6]
        note = row[6] if len(row) > 6 else ""
        dthresh = row[7] if len(row) > 7 else 10
        grade = row[8] if len(row) > 8 else False
        base = GRADE_BASE if grade else BASE
        requests[f"{name}_off"] = (scene, audit.scene_env({**base, **off_extra}, clean=True))
        requests[f"{name}_on"] = (scene, audit.scene_env({**base, **on_extra}, clean=True))
        row_meta[name] = (lo, hi, note, dthresh)
    pool = audit.CapturePool(jobs=4, raw_dir=CAPTURE_DIR, timeout=TIMEOUT)
    shots = pool.capture_all(requests, shot_frame=180)
    fails = []
    print(f"{'feature':>14} | {'frac':>7} {'mean0':>7} {'mean1':>7} {'dmean':>7} {'dmax':>7} | verdict")
    for row in ROWS:
        name = row[0]
        lo, hi, note, dthresh = row_meta[name]
        p0 = shots.get(f"{name}_off")
        p1 = shots.get(f"{name}_on")
        if p0 is None or p1 is None:
            print(f"{name:>14} | no shot | FAIL")
            fails.append(name)
            continue
        label_top_right(str(p0), f"{name} off")
        label_top_right(str(p1), name)
        a = np.asarray(Image.open(p0).convert("RGB")).astype(float)
        b = np.asarray(Image.open(p1).convert("RGB")).astype(float)
        if a.shape != b.shape:
            print(f"{name:>14} | size mismatch | FAIL")
            fails.append(name)
            continue
        d = np.abs(a - b).mean(axis=2)
        frac = float((d > dthresh).mean())
        dmean = float(d.mean())
        dmax = float(d.max())
        m0, m1 = a.mean(), b.mean()
        black = min((a.mean(axis=2) < 8).mean(), (b.mean(axis=2) < 8).mean())
        ok = (lo <= frac <= hi) and black < 0.5 and 5 < m0 < 250 and 5 < m1 < 250
        retried = ""
        if not ok:
            retry = pool.capture_all({f"{name}_on_retry": requests[f"{name}_on"]},
                                     shot_frame=120)
            retry_path = retry.get(f"{name}_on_retry")
            if retry_path is not None:
                label_top_right(str(retry_path), name)
                b = np.asarray(Image.open(retry_path).convert("RGB")).astype(float)
                if a.shape == b.shape:
                    d = np.abs(a - b).mean(axis=2)
                    frac = float((d > dthresh).mean())
                    dmean = float(d.mean())
                    dmax = float(d.max())
                    m1 = b.mean()
                    black = min((a.mean(axis=2) < 8).mean(), (b.mean(axis=2) < 8).mean())
                    ok = (lo <= frac <= hi) and black < 0.5 and 5 < m0 < 250 and 5 < m1 < 250
                    retried = " (retry)" if ok else " (retry FAIL)"
        tag = ("ok" if ok else "FAIL") + retried
        print(f"{name:>14} | {frac:>7.4f} {m0:>7.1f} {m1:>7.1f} {dmean:>7.2f} {dmax:>7.1f} | {tag} {note}")
        if not ok:
            fails.append(name)
    if fails:
        print(f"FOG-LIGHT CHECK: FAIL — {fails}")
        return 1
    print("FOG-LIGHT CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
