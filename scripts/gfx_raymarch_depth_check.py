#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: opaque hybrid depth oracle: analytic sphere vs axis-aligned raster cube.
import json
import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit
import gfx_raymarch_check as artifacts
from gfx_feature_matrix import BASE_OFF


def oracle(width, height, case, asymmetric, depth=False, normals=False):
    origin = np.array([4.,2.,8.] if asymmetric else [0.,0.,8.])
    forward = -origin/np.linalg.norm(origin)
    right = np.cross(forward,[0.,1.,0.])
    right /= np.linalg.norm(right)
    up = np.cross(right,forward)
    y,x = np.mgrid[:height,:width]
    rays = ((x+.5-width/2)[...,None]*right
            + (height/2-y-.5)[...,None]*up
            + height/(2*np.tan(np.radians(25)))*forward)
    rays /= np.linalg.norm(rays,axis=2)[...,None]
    center = np.array([100. if case == "miss" else .7,0.,
                       2.5 if case == "front" else -2.5 if case == "back" else 0.])
    offset = origin-center
    b = (rays*offset).sum(axis=2)
    discriminant = b*b - np.dot(offset,offset) + 1.3**2
    sphere = -b-np.sqrt(np.maximum(discriminant,0))
    sphere = np.where((discriminant>0)&(sphere>0),sphere,np.inf)
    with np.errstate(divide="ignore",invalid="ignore"):
        a, c = (-1-origin)/rays, (1-origin)/rays
    enter = np.minimum(a,c).max(axis=2)
    leave = np.maximum(a,c).min(axis=2)
    cube = np.where(leave>=np.maximum(enter,0),enter,np.inf)
    visible = sphere<cube
    #ai: exclude silhouettes and depth ties from the strict interior oracle.
    interior = visible.copy()
    exterior = ~visible
    for dy in range(-2,3):
        for dx in range(-2,3):
            shifted = np.roll(visible,(dy,dx),(0,1))
            interior &= shifted
            exterior &= ~shifted
    interior[:3] = interior[-3:] = False
    interior[:,:3] = interior[:,-3:] = False
    exterior[:3] = exterior[-3:] = False
    exterior[:,:3] = exterior[:,-3:] = False
    if normals:
        hit = origin + np.where(np.isfinite(sphere),sphere,0)[...,None]*rays
        normal = (hit-center)/1.3
        return interior, exterior, normal
    if depth:
        view = np.minimum(sphere,cube)*(rays*forward).sum(axis=2)
        expected = np.where(np.isfinite(view),100/99.9-10/(99.9*view),1.)
        valid = interior|exterior
        cube_hit = np.isfinite(cube)
        for dy in range(-2,3):
            for dx in range(-2,3):
                valid &= cube_hit == np.roll(cube_hit,(dy,dx),(0,1))
        return interior, exterior, expected, valid
    return interior, exterior


#ai: native RT dumps are bottom-up float32, with dimensions in their binary header.
def read_depth(directory):
    paths = sorted(directory.glob("hybrid_depth_*.gxrt"))
    if not paths:
        raise RuntimeError(f"missing combined depth dump: {directory}")
    data = paths[-1].read_bytes()
    magic,width,height,channels = struct.unpack_from("<8sIII",data)
    if magic != b"GXRTF32\0" or channels != 1 or len(data) != 20+width*height*4:
        raise RuntimeError(f"invalid depth dump: {paths[-1]}")
    return np.frombuffer(data,dtype="<f4",offset=20).reshape(height,width)[::-1]


def capture(root, case, asymmetric, enabled, suffix="", overrides=None):
    directory = root/f"{case}_{'oblique' if asymmetric else 'axial'}_{enabled}{suffix}"
    directory.mkdir()
    hooks = dict(BASE_OFF)
    hooks.update({"GX_RAYMARCH_DEPTH_TEST":case,"GX_GFX_RAYMARCH_DEPTH":str(enabled),
                  "GX_FIXED_DT":"0.0166666667","GX_FRAMES":"30","GX_T_MSAA":"0"})
    hooks.update(overrides or {})
    if asymmetric:
        hooks["GX_RAYMARCH_DEPTH_CAMERA"] = "1"
    if enabled == 2:
        hooks.update(GX_RT_DUMP_DIR=str(directory),GX_RT_DUMP_FRAME="20",GX_RT_DUMP_COUNT="1")
    environment = audit.scene_env(hooks,clean=True)
    result = artifacts.capture_scene("--showcase-minimal",environment,width=640,height=480,
        fps=60,frames=30,shot_frame=20,screenshot_dir=directory,timeout=120,check=True)
    shot = audit.newest_shot(directory)
    if not shot:
        raise RuntimeError(f"missing capture: {directory}")
    log = audit.strip_ansi(result.stdout)
    if "SHADER" in log and ("Failed to compile" in log or "Failed to link" in log):
        raise RuntimeError(f"shader failure: {directory}")
    pixels = np.asarray(Image.open(shot).convert("RGB"),dtype=float)
    return (pixels,read_depth(directory)) if enabled == 2 else pixels


#ai: resolved raster depth belongs to a multisample location, not the pixel-center ray.
def msaa_metrics(image, baseline, depth, raster_depth):
    interior,exterior,expected,valid = oracle(image.shape[1],image.shape[0],"cross",True,depth=True)
    changed = np.abs(image-baseline).mean(axis=2)>10
    return {"msaa":4,"case":"cross","camera":"oblique",
            "holes":float((~changed)[interior].mean()),
            "leaks":float(changed[exterior].mean()),
            "sdf_depth_max_error":float(np.abs(depth-expected)[interior&valid].max()),
            "raster_depth_preservation_max_error":float(np.abs(depth-raster_depth)[exterior].max()),
            "hole_cap":.01,"leak_cap":.001,"depth_error_cap":2e-6}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--shared",action="store_true",help="validate HDR scene color and exported depth")
    args = parser.parse_args()
    mode = 2 if args.shared else 1
    rows = []
    shared_front = None
    try:
        with artifacts.artifact_run() as directory, audit.settings_preserved():
            root = Path(directory)
            for asymmetric in (False,True):
                #ai: keep the compositor identical; an off-path canvas has different edge alpha.
                baseline = capture(root,"miss",asymmetric,mode,"_baseline")
                if args.shared:
                    baseline = baseline[0]
                for case in ("front","back","cross","miss"):
                    image = capture(root,case,asymmetric,mode)
                    if args.shared:
                        image,depth_image = image
                        if not asymmetric and case == "front":
                            shared_front = image
                    interior, exterior = oracle(image.shape[1],image.shape[0],case,asymmetric)
                    changed = np.abs(image-baseline).mean(axis=2)>10
                    holes = float((~changed)[interior].mean()) if interior.any() else 0.
                    leaks = float(changed[exterior].mean())
                    row = {"case":case,"camera":"oblique" if asymmetric else "axial",
                           "expected_visible_pixels":int(interior.sum()),"holes":holes,"leaks":leaks,
                           "hole_cap":.01,"leak_cap":.001}
                    rows.append(row)
                    if args.shared:
                        _,_,expected,valid = oracle(image.shape[1],image.shape[0],case,asymmetric,depth=True)
                        if depth_image.shape != expected.shape or not np.isfinite(depth_image).all():
                            raise RuntimeError("combined depth dimensions/finiteness mismatch")
                        error = np.abs(depth_image-expected)[valid]
                        maximum = float(error.max())
                        row.update(depth_max_error=maximum,depth_error_cap=2e-6,
                                   depth_test_pixels=int(valid.sum()))
                        print(f"  combined depth: max error={maximum:.9f} <= 0.000002",flush=True)
                        if maximum>2e-6:
                            (root/"depth_report.json").write_text(json.dumps(rows,indent=2)+"\n")
                            raise RuntimeError(f"combined depth oracle failed: {row}")
                    (root/"depth_report.json").write_text(json.dumps(rows,indent=2)+"\n")
                    print(f"{case}/{row['camera']}: visible={interior.sum()} holes={holes:.6f} <= .01 "
                          f"leaks={leaks:.6f} <= .001",flush=True)
                    if holes>.01 or leaks>.001:
                        raise RuntimeError(f"visibility oracle failed: {row}")
            (root/"depth_report.json").write_text(json.dumps(rows,indent=2)+"\n")
            if args.shared:
                baseline_msaa,raster_depth = capture(root,"miss",True,2,"_msaa_baseline",{"GX_T_MSAA":"4"})
                image_msaa,depth_msaa = capture(root,"cross",True,2,"_msaa",{"GX_T_MSAA":"4"})
                msaa = msaa_metrics(image_msaa,baseline_msaa,depth_msaa,raster_depth)
                (root/"msaa_report.json").write_text(json.dumps(msaa,indent=2)+"\n")
                print(f"4x MSAA: holes={msaa['holes']:.6f} <= .01, leaks={msaa['leaks']:.6f} <= .001, "
                      f"SDF depth error={msaa['sdf_depth_max_error']:.9f} <= .000002, "
                      f"raster preservation error={msaa['raster_depth_preservation_max_error']:.9f} <= .000002",flush=True)
                if (msaa["holes"]>.01 or msaa["leaks"]>.001 or msaa["sdf_depth_max_error"]>2e-6
                        or msaa["raster_depth_preservation_max_error"]>2e-6):
                    raise RuntimeError(f"MSAA depth oracle failed: {msaa}")
                #ai: DOF is a depth-only consumer; the historical post-effects overlay is its control.
                dof = {"GX_T_DOF":"1","GX_T_DOFFOCAL":"1","GX_T_DOFRANGE":".1"}
                shared_on,_ = capture(root,"front",False,2,"_dof",dof)
                legacy_off = capture(root,"front",False,1,"_dof_control_off")
                legacy_on = capture(root,"front",False,1,"_dof_control_on",dof)
                support,_ = oracle(640,480,"front",False)
                delta = float(np.abs(shared_on-shared_front).mean(axis=2)[support].mean())
                control = float(np.abs(legacy_on-legacy_off).mean(axis=2)[support].mean())
                consumer = {"effect":"dof","sdf_mean_delta":delta,"delta_floor":.1,
                            "legacy_overlay_mean_delta":control,"control_cap":.01}
                (root/"consumer_report.json").write_text(json.dumps(consumer,indent=2)+"\n")
                print(f"DOF on SDF: mean delta={delta:.6f} >= .1; post-effects control={control:.6f} <= .01",flush=True)
                if delta<.1 or control>.01:
                    raise RuntimeError(f"depth consumer failed: {consumer}")
            #ai: editor evidence must refer to a visible, populated checkbox rectangle.
            ui = root/"editor"
            ui.mkdir()
            environment = audit.scene_env({"GX_GFX_RAYMARCH_DEPTH":str(mode),
                "GX_RAYMARCH_EDITOR_INSPECT":"3","GX_FIXED_DT":"0.0166666667",
                "GX_RAYMARCH_DEPTH_TEST":"front","GX_FRAMES":"30"},clean=True)
            result = artifacts.capture_scene("--showcase-minimal",environment,
                width=1024,height=768,fps=60,frames=30,shot_frame=20,
                screenshot_dir=ui,timeout=120,check=True,extra_args=("--gfx-editor",))
            shot = audit.newest_shot(ui)
            if not shot:
                raise RuntimeError("hybrid editor capture missing")
            pixels = np.asarray(Image.open(shot).convert("RGB"),dtype=float)
            evidence = artifacts.check_item_evidence(audit.strip_ansi(result.stdout),
                pixels,required=("shared_depth" if args.shared else "hybrid_depth",))
            (ui/"visible_controls.json").write_text(json.dumps(evidence,indent=2)+"\n")
            print("hybrid editor checkbox: visible, nonempty",flush=True)
        print("GFX_RAYMARCH_DEPTH: PASS")
        return 0
    except Exception as error:
        print(f"GFX_RAYMARCH_DEPTH: FAIL — {error}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
