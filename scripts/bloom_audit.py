#!/usr/bin/env python3
#opencode: dedicated bloom audit — isolates each pipeline stage (extract
#   threshold/knee, blur spread, composite intensity and chain, auto-exposure,
#   emissive extraction, MSAA interaction). Trend checks are directional
#   (monotonicity), not just bands; every capture also rejects shader error
#   sentinels (magenta), black collapse, and full-frame washout.
#
#   Usage:
#     python3 scripts/bloom_audit.py                 # full gate
#     python3 scripts/bloom_audit.py --quick         # isolation + intensity + emissive
#     python3 scripts/bloom_audit.py --only intensity,threshold
#     python3 scripts/bloom_audit.py --report        # metrics only (calibration)
#     python3 scripts/bloom_audit.py --jobs 6 --cache
#
#   The run needs a live GPU display like the feature matrix (DISPLAY=:1).

import argparse
import json
import sys
from pathlib import Path

import audit_common as audit


SHOWCASE = "--showcase"
EMISSIVE = "--showcase-minimal"

#opencode: every effect except bloom off; neutral fog keeps the composite chain
#   engaged without changing pixels; fixed camera for cross-probe comparability.
BASE_OFF = {
    "GX_T_SSAO": "0", "GX_T_SHADOWS": "0", "GX_T_POINT_SHADOWS": "0",
    "GX_T_SSR": "0", "GX_T_TAA": "0", "GX_T_FXAA": "0",
    "GX_T_GODRAY": "0", "GX_T_COOKIES": "0", "GX_T_PARTICLES": "0",
    "GX_T_LENSFLARE": "0", "GX_T_DOF": "0", "GX_T_MOTIONBLUR": "0",
    "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0", "GX_T_SNOW": "0",
    "GX_T_SNOWFALL": "0", "GX_T_RAIN": "0", "GX_T_FOGSPRITE": "0",
    "GX_T_SPLAT": "0", "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0",
    "GX_T_INSTANCING": "0", "GX_T_LIGHT_MARKERS": "0", "GX_T_MSAA": "0",
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0", "GX_T_HUD": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
    "GX_CAM_FOV": "45",
}
#opencode: auto-exposure off keeps exposure deterministic for the stage probes;
#   the dedicated auto-exposure family toggles it explicitly.
BASE = {**BASE_OFF, "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6",
        "GX_T_BLOOMAE": "0"}
CONTROL = {**BASE, "GX_T_BLOOMINT": "0"}

#ai: strict spatial ramps need a shipped emissive source above the unchanged 10-step pixel cut.
INTENSITY_SOURCE = {
    "GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1",
    "GX_T_BLOOMTHRESH": "0.85", "GX_T_BLOOMKNEE": "0.3",
    "GX_T_BLOOMBLUR": "1", "GX_T_BLOOMEXPOSURE": "1",
    "GX_T_BLOOMSAT": "1", "GX_T_BLOOMCONTRAST": "1", "GX_T_VIGNETTE": "0.5",
}

QUICK_FAMILIES = ("isolation", "intensity", "emissive")


#ai: procedural intensity source and matching endpoints; selected-family runs retain real controls.
def requests_for(families):
    """Build {capture key: (scene, env)} for the requested probe families."""
    requests = {}

    def add(family, key, scene, env):
        if family in families:
            requests[key] = (scene, env)

    add("isolation", "control", SHOWCASE, CONTROL)
    add("isolation", "glow", SHOWCASE, BASE)
    add("intensity", "intensity_control", EMISSIVE, {**CONTROL, **INTENSITY_SOURCE})
    add("intensity", "intensity_glow", EMISSIVE, {**BASE, **INTENSITY_SOURCE})
    for value in ("0.75", "1.5", "3"):
        add("intensity", f"intensity_{value}", EMISSIVE,
            {**BASE, **INTENSITY_SOURCE, "GX_T_BLOOMINT": value})
        add("intensity", f"showcase_intensity_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMINT": value})
    for value in ("0.1", "0.85", "2", "10"):
        add("threshold", f"threshold_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMTHRESH": value})
    for value in ("0", "1"):
        add("knee", f"knee_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMKNEE": value})
    for value in ("0.5", "2"):
        add("blur", f"blur_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMBLUR": value})
    for value in ("0.5", "2"):
        add("composite", f"exposure_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMEXPOSURE": value})
    for value in ("0", "1"):
        add("composite", f"vignette_{value}", SHOWCASE,
            {**BASE, "GX_T_VIGNETTE": value})
    for value in ("0", "2"):
        add("composite", f"saturation_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMSAT": value})
    for value in ("0.5", "2"):
        add("composite", f"contrast_{value}", SHOWCASE,
            {**BASE, "GX_T_BLOOMCONTRAST": value})
    add("tonemap", "tonemap_off", SHOWCASE, {**BASE, "GX_T_TONEMAP": "0"})
    add("tonemap", "tonemap_aces", SHOWCASE, {**BASE, "GX_NO_AGX": "1"})
    add("autoexposure", "ae_on", SHOWCASE, {**BASE, "GX_T_BLOOMAE": "1"})
    add("emissive", "emissive_control", EMISSIVE,
        {**CONTROL, "GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1"})
    add("emissive", "emissive_glow", EMISSIVE,
        {**BASE, "GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1"})
    add("msaa", "msaa_control", SHOWCASE, {**CONTROL, "GX_T_MSAA": "4"})
    add("msaa", "msaa_glow", SHOWCASE, {**BASE, "GX_T_MSAA": "4"})
    if requests:
        requests.setdefault("control", (SHOWCASE, CONTROL))
        requests.setdefault("glow", (SHOWCASE, BASE))
    return requests


class Audit:
    def __init__(self, report_only=False):
        self.report_only = report_only
        self.rows = []
        self.failures = []

    def check(self, family, name, ok, detail):
        self.rows.append((family, name, bool(ok), detail))
        if not ok and not self.report_only:
            self.failures.append(f"{family}.{name}")

    def print_report(self):
        print(f"{'family':>12} | {'probe':>22} | {'metrics':<58} | verdict")
        for family, name, ok, detail in self.rows:
            verdict = "ok" if ok else ("report" if self.report_only else "FAIL")
            print(f"{family:>12} | {name:>22} | {detail:<58} | {verdict}")


def metrics(shots, key, control, audit_result, family):
    if key not in shots:
        audit_result.check(family, key, False, "capture missing")
        return None
    image = audit.load_rgb(shots[key])
    control_image = audit.load_rgb(shots[control]) if control in shots else None
    values = {
        "image": image,
        "mean": audit.mean_luma(image),
        "black": audit.black_fraction(image),
        "magenta": audit.magenta_fraction(image),
        "chroma": audit.chroma_spread(image),
        "std": audit.luma_std(image),
        "edge": audit.edge_center_ratio(image),
        "frac": audit.diff_frac(image, control_image) if control_image is not None else 0.0,
        "frac_low": audit.diff_frac(image, control_image, cut=2.0) if control_image is not None else 0.0,
    }
    if values["magenta"] > 0.0:
        audit_result.check(family, f"{key}.sentinel", False,
                           f"magenta sentinel {values['magenta']:.5f}")
    if values["black"] > 0.5:
        audit_result.check(family, f"{key}.collapse", False,
                           f"black fraction {values['black']:.3f}")
    return values


def check_isolation(shots, result):
    glow = metrics(shots, "glow", "control", result, "isolation")
    if glow is None:
        return
    ok = 1e-5 <= glow["frac"] <= 0.2
    result.check("isolation", "glow-vs-control", ok,
                 f"frac={glow['frac']:.5f} (band 0.00001..0.2) "
                 f"mean={glow['mean']:.1f}")


#ai: acceptance predicates/cut/bounds are unchanged; the spatial witness has an explicit HDR source.
def check_intensity(shots, result):
    ramp = []
    for value in ("0.75", "1.5", "3"):
        values = metrics(shots, f"intensity_{value}", "intensity_control", result, "intensity")
        if values is not None:
            ramp.append((float(value), values["frac"], values["mean"]))
    glow = metrics(shots, "intensity_glow", "intensity_control", result, "intensity")
    if glow is not None:
        ramp.append((6.0, glow["frac"], glow["mean"]))
    if len(ramp) < 4:
        result.check("intensity", "ramp", False, "missing captures")
        return
    fracs = [entry[1] for entry in ramp]
    means = [entry[2] for entry in ramp]
    monotonic = all(fracs[i] < fracs[i + 1] for i in range(len(fracs) - 1))
    bounded = fracs[-1] <= 0.25
    bright = all(means[i] < means[i + 1] for i in range(len(means) - 1))
    result.check("intensity", "ramp-monotonic", monotonic,
                 "frac " + " < ".join(f"{value:.4f}" for value in fracs))
    result.check("intensity", "ramp-bounded", bounded,
                 f"max frac {fracs[-1]:.4f} (ceiling 0.25)")
    result.check("intensity", "ramp-brightens", bright,
                  "mean " + " < ".join(f"{value:.1f}" for value in means))
    showcase = []
    for key in ("showcase_intensity_0.75", "showcase_intensity_1.5", "showcase_intensity_3", "glow"):
        values = metrics(shots, key, "control", result, "intensity")
        if values is not None:
            showcase.append(values)
    if len(showcase) != 4:
        result.check("intensity", "showcase-ramp", False, "missing captures")
        return
    result.check("intensity", "showcase-brightens",
                 all(showcase[i]["mean"] < showcase[i + 1]["mean"] for i in range(3)),
                 "mean " + " < ".join(f"{value['mean']:.1f}" for value in showcase))
    result.check("intensity", "showcase-bounded", showcase[-1]["frac"] <= 0.25,
                 f"max frac {showcase[-1]['frac']:.4f} (ceiling 0.25)")


def check_threshold(shots, result):
    sweep = []
    for value in ("0.1", "0.85", "2", "10"):
        values = metrics(shots, f"threshold_{value}", "control", result, "threshold")
        if values is not None:
            sweep.append((float(value), values["frac"]))
    if len(sweep) < 4:
        result.check("threshold", "sweep", False, "missing captures")
        return
    fracs = [entry[1] for entry in sweep]
    nonincreasing = all(fracs[i] >= fracs[i + 1] - 1e-9
                        for i in range(len(fracs) - 1))
    head = fracs[0] > fracs[1] > fracs[2]
    result.check("threshold", "sweep-decreasing", nonincreasing and head,
                 "frac " + " > ".join(f"{value:.5f}" for value in fracs))
    result.check("threshold", "high-cutoff", fracs[-1] < 5e-4,
                 f"threshold 10 frac {fracs[-1]:.5f} (expect ~0)")
    #opencode: threshold 0.1 is the fat-halo end by design — nearly every pixel
    #   passes the bright-pass; the ceiling only rejects a 100% wash.
    result.check("threshold", "low-bounded", fracs[0] <= 0.98,
                 f"threshold 0.1 frac {fracs[0]:.5f} (ceiling 0.98)")


def check_knee(shots, result):
    knee0 = metrics(shots, "knee_0", "control", result, "knee")
    knee1 = metrics(shots, "knee_1", "control", result, "knee")
    if knee0 is None or knee1 is None:
        return
    result.check("knee", "soft-band-wider", knee1["frac"] > knee0["frac"] + 5e-4,
                 f"frac knee1 {knee1['frac']:.5f} > knee0 {knee0['frac']:.5f}")
    #opencode: knee 1 is the widest soft band by design (threshold*(1±1)); the
    #   ceiling only rejects a fully washed frame.
    result.check("knee", "bounded", max(knee0["frac"], knee1["frac"]) <= 0.98,
                 f"max frac {max(knee0['frac'], knee1['frac']):.5f} (ceiling 0.98)")


def check_blur(shots, result):
    narrow = metrics(shots, "blur_0.5", "control", result, "blur")
    wide = metrics(shots, "blur_2", "control", result, "blur")
    if narrow is None or wide is None:
        return
    #opencode: a wider blur spreads the same energy thinner: the >10 cut can
    #   shrink while the low-delta footprint grows, so compare the >2 footprint.
    result.check("blur", "wider-spreads",
                 wide["frac_low"] > narrow["frac_low"] + 1e-4,
                 f"footprint {wide['frac_low']:.5f} > {narrow['frac_low']:.5f} (>2 cut)")
    result.check("blur", "bounded", wide["frac"] <= 0.3,
                 f"max frac {wide['frac']:.5f} (ceiling 0.3)")


def check_composite(shots, result):
    glow = metrics(shots, "glow", "control", result, "composite")
    dim = metrics(shots, "exposure_0.5", "control", result, "composite")
    bright = metrics(shots, "exposure_2", "control", result, "composite")
    if glow is not None and dim is not None and bright is not None:
        result.check("composite", "exposure-ramp",
                     dim["mean"] < glow["mean"] - 0.5 < bright["mean"] - 1.0,
                     f"mean {dim['mean']:.1f} < {glow['mean']:.1f} < {bright['mean']:.1f}")
    vig0 = metrics(shots, "vignette_0", "control", result, "composite")
    vig1 = metrics(shots, "vignette_1", "control", result, "composite")
    if vig0 is not None and vig1 is not None:
        result.check("composite", "vignette-darkens-edges",
                     vig1["edge"] < vig0["edge"] - 0.02,
                     f"edge ratio {vig1['edge']:.3f} < {vig0['edge']:.3f}")
    sat0 = metrics(shots, "saturation_0", "control", result, "composite")
    sat2 = metrics(shots, "saturation_2", "control", result, "composite")
    if sat0 is not None and sat2 is not None:
        result.check("composite", "saturation-ramp",
                     sat0["chroma"] < 4.0 and sat2["chroma"] > sat0["chroma"] + 1.0,
                     f"chroma {sat0['chroma']:.2f} (gray) -> {sat2['chroma']:.2f}")
    con05 = metrics(shots, "contrast_0.5", "control", result, "composite")
    con2 = metrics(shots, "contrast_2", "control", result, "composite")
    if con05 is not None and con2 is not None:
        result.check("composite", "contrast-ramp",
                     con2["std"] > con05["std"] + 1.0,
                     f"luma std {con05['std']:.2f} -> {con2['std']:.2f}")


def check_tonemap(shots, result):
    glow = metrics(shots, "glow", "control", result, "tonemap")
    off = metrics(shots, "tonemap_off", "control", result, "tonemap")
    aces = metrics(shots, "tonemap_aces", "control", result, "tonemap")
    if glow is None or off is None:
        return
    delta = audit.diff_frac(off["image"], glow["image"])
    result.check("tonemap", "chain-engages", delta > 0.001,
                 f"tonemap-off vs default frac {delta:.5f} (floor 0.001)")
    if aces is not None:
        aces_delta = audit.diff_frac(aces["image"], glow["image"])
        result.check("tonemap", "aces-vs-agx", aces_delta > 0.0005,
                     f"ACES vs AgX frac {aces_delta:.5f} (floor 0.0005)")


def check_autoexposure(shots, result):
    glow = metrics(shots, "glow", "control", result, "autoexposure")
    ae_on = metrics(shots, "ae_on", "control", result, "autoexposure")
    if glow is None or ae_on is None:
        return
    result.check("autoexposure", "adapts-frame",
                 abs(ae_on["mean"] - glow["mean"]) > 0.5,
                 f"mean {ae_on['mean']:.1f} vs fixed {glow['mean']:.1f}")


def check_emissive(shots, result):
    glow = metrics(shots, "emissive_glow", "emissive_control", result, "emissive")
    if glow is None:
        return
    result.check("emissive", "orb-halo", 0.015 <= glow["frac"] <= 0.20,
                 f"frac={glow['frac']:.4f} (band 0.015..0.20)")


def check_msaa(shots, result):
    glow = metrics(shots, "msaa_glow", "msaa_control", result, "msaa")
    if glow is None:
        return
    result.check("msaa", "glow-through-resolve", glow["frac"] > 1e-5,
                 f"frac={glow['frac']:.5f} (floor 0.00001)")


CHECKS = {
    "isolation": check_isolation,
    "intensity": check_intensity,
    "threshold": check_threshold,
    "knee": check_knee,
    "blur": check_blur,
    "composite": check_composite,
    "tonemap": check_tonemap,
    "autoexposure": check_autoexposure,
    "emissive": check_emissive,
    "msaa": check_msaa,
}


def main():
    parser = argparse.ArgumentParser(
        description="Audit the gX bloom pipeline stage by stage.")
    parser.add_argument("--jobs", type=int, default=4,
                        help="parallel capture processes (default: 4)")
    parser.add_argument("--quick", action="store_true",
                        help="isolation + intensity + emissive only")
    parser.add_argument("--only", metavar="FAMILIES",
                        help="comma-separated probe families")
    parser.add_argument("--report", action="store_true",
                        help="print metrics without asserting (calibration)")
    parser.add_argument("--cache", action="store_true",
                        help="reuse captures whose binary+request fingerprint matches")
    parser.add_argument("--json", metavar="PATH",
                        help="write the probe metrics as JSON")
    parser.add_argument("--output", default="/tmp/opencode/bloom_audit",
                        help="capture output directory")
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 8:
        parser.error("--jobs must be between 1 and 8")

    families = set(CHECKS)
    if args.quick:
        families &= set(QUICK_FAMILIES)
    if args.only:
        requested = {item.strip() for item in args.only.split(",") if item.strip()}
        unknown = sorted(requested - set(CHECKS))
        if unknown:
            parser.error("unknown probe families: " + ", ".join(unknown))
        families &= requested
    if not families:
        parser.error("probe selection is empty")
    if not audit.BIN.is_file():
        print(f"BLOOM_AUDIT: binary missing: {audit.BIN}", file=sys.stderr)
        return 1

    requests = requests_for(families)
    print(f"bloom audit: {len(families)} families, {len(requests)} captures, "
          f"{args.jobs} workers, 1024x768@60")
    pool = audit.CapturePool(jobs=args.jobs, raw_dir=Path(args.output) / "raw",
                             cache=args.cache)
    with audit.settings_preserved():
        shots = pool.capture_all(requests)
    if len(shots) != len(requests):
        missing = sorted(set(requests) - set(shots))
        print(f"BLOOM_AUDIT: missing captures: {missing}", file=sys.stderr)
        return 1

    result = Audit(report_only=args.report)
    for family in CHECKS:
        if family in families:
            CHECKS[family](shots, result)
    result.print_report()
    if args.json:
        payload = {
            "families": sorted(families),
            "rows": [{"family": family, "probe": name, "ok": ok, "detail": detail}
                     for family, name, ok, detail in result.rows],
        }
        json_path = Path(args.json)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(payload, indent=2))
    if result.failures:
        print(f"BLOOM_AUDIT: FAIL — {result.failures}")
        return 1
    if args.report:
        print("BLOOM_AUDIT: REPORT (no assertions)")
        return 0
    print(f"BLOOM_AUDIT: PASS ({len(result.rows)} checks, "
          f"{len(families)} families)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
