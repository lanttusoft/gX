#!/usr/bin/env python3
#ai: shared SDF normals/roughness are checked in native buffers, not inferred from pretty reflections.
import json
import struct
import sys
from pathlib import Path

import numpy as np

import audit_common as audit
import gfx_raymarch_check as artifacts
from gfx_raymarch_depth_check import oracle
from gfx_feature_matrix import BASE_OFF


def read_rgba(directory, name, frame=None):
    #ai: temporal probes can inspect a specific pre-diagnostic history frame.
    paths = sorted(directory.glob(f"{name}_*.gxrt" if frame is None else f"{name}_{frame:06d}.gxrt"))
    if not paths:
        raise RuntimeError(f"missing {name} float dump: {directory}")
    data = paths[-1].read_bytes()
    magic,width,height,channels = struct.unpack_from("<8sIII",data)
    if magic != b"GXRTF32\0" or channels != 4 or len(data) != 20+width*height*16:
        raise RuntimeError(f"malformed RGBA dump: {paths[-1]}")
    pixels = np.frombuffer(data,dtype="<f4",offset=20).reshape(height,width,4)[::-1]
    if not np.isfinite(pixels).all():
        raise RuntimeError("nonfinite buffer values")
    return pixels


def capture(root, case, oblique, roughness=None, diagnostic=False, normals_on=True):
    tag = f"{case}_{int(oblique)}_{roughness}_{int(diagnostic)}_{int(normals_on)}"
    directory = root/tag
    directory.mkdir()
    hooks = dict(BASE_OFF)
    hooks.update(GX_GFX_RAYMARCH_DEPTH="2",GX_RAYMARCH_DEPTH_TEST=case,
        GX_FIXED_DT="0.0166666667",GX_FRAMES="30",GX_RT_DUMP_DIR=str(directory),
        GX_RT_DUMP_FRAME="20",GX_RT_DUMP_COUNT="1",GX_T_MSAA="0")
    if oblique:
        hooks["GX_RAYMARCH_DEPTH_CAMERA"] = "1"
    name = "normal"
    if roughness is None:
        hooks.update(GX_SSR_NORMALS="1",GX_T_SSR="1",GX_RT_VIEW="normal")
    else:
        hooks.update(GX_RAYMARCH_ROUGHNESS=str(roughness),GX_RAYMARCH_SURFACE_DUMP="1")
        name = "hybrid_surface"
    if diagnostic:
        hooks.update(GX_SSR_NORMALS=str(int(normals_on)),GX_SSR_DEBUG="28",GX_RT_VIEW="ssr")
        name = "ssr"
    artifacts.capture_scene("--showcase-minimal",audit.scene_env(hooks,clean=True),
        width=640,height=480,fps=60,frames=30,shot_frame=20,screenshot_dir=directory,
        timeout=120,check=True)
    return read_rgba(directory,name)


def main():
    rows = []
    try:
        with artifacts.artifact_run() as directory, audit.settings_preserved():
            root = Path(directory)
            for oblique in (False,True):
                baseline = capture(root,"miss",oblique)
                for case in ("front","cross","back"):
                    image = capture(root,case,oblique)
                    support,exterior,expected = oracle(640,480,case,oblique,normals=True)
                    decoded = image[...,:3]*2-1
                    length = np.linalg.norm(decoded,axis=2)
                    decoded /= np.maximum(length[...,None],1e-8)
                    cosine = np.clip((decoded*expected).sum(axis=2),-1,1)
                    angle = np.degrees(np.arccos(cosine[support]))
                    row = {"case":case,"oblique":oblique,"normal_max_degrees":float(angle.max()),
                           "unit_length_max_error":float(np.abs(length[support]-1).max()),
                           "coverage_min":float(image[...,3][support].min()),
                           "raster_preservation_max_error":float(np.abs(image-baseline)[exterior].max())}
                    rows.append(row)
                    (root/"surface_report.json").write_text(json.dumps(rows,indent=2)+"\n")
                    print(row,flush=True)
                    if (row["normal_max_degrees"]>1 or row["unit_length_max_error"]>.015
                        or row["coverage_min"]!=1 or row["raster_preservation_max_error"]>1/255):
                        raise RuntimeError("normal direction/coverage/depth ownership failed")
            for roughness in (.15,.8):
                image = capture(root,"front",False,roughness)
                support,_ = oracle(640,480,"front",False)
                error = float(np.abs(image[...,3][support]-roughness).max())
                row = {"roughness":roughness,"hdr_alpha_max_error":error,"cap":.001}
                rows.append(row)
                (root/"surface_report.json").write_text(json.dumps(rows,indent=2)+"\n")
                print(row,flush=True)
                if error>.001:
                    raise RuntimeError("authored roughness missing from HDR alpha")
            #ai: debug 28 runs inside SSR: green=normal agreement, blue=missing normal coverage.
            on = capture(root,"front",False,diagnostic=True)
            off = capture(root,"front",False,diagnostic=True,normals_on=False)
            support,_ = oracle(on.shape[1],on.shape[0],"front",False)
            agreement = float(((on[...,1]>.95)&(on[...,2]<.05))[support].mean())
            missing = float(((off[...,2]>.95)&(off[...,1]<.05))[support].mean())
            consumer = {"ssr_normal_agreement_fraction":agreement,"normals_off_missing_fraction":missing,
                        "fraction_floor":.95,"quality_certified":False}
            (root/"ssr_consumer_report.json").write_text(json.dumps(consumer,indent=2)+"\n")
            print(consumer,flush=True)
            if agreement<.95 or missing<.95:
                raise RuntimeError("SSR did not consume expected SDF normals/coverage")
        print("GFX_RAYMARCH_SURFACE: PASS")
        return 0
    except Exception as error:
        print(f"GFX_RAYMARCH_SURFACE: FAIL — {error}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
