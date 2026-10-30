#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: analytic hybrid SSR oracle distinguishes visible screen-space targets from unavailable geometry.
import numpy as np
import json
import sys
from pathlib import Path

import audit_common as audit
import gfx_raymarch_check as artifacts
from gfx_feature_matrix import BASE_OFF
from gfx_raymarch_surface_check import read_rgba


def sphere_distance(origin, direction, center, radius=1.3):
    offset = origin-center
    b = (direction*offset).sum(axis=-1)
    d = b*b-(offset*offset).sum(axis=-1)+radius*radius
    t = -b-np.sqrt(np.maximum(d,0))
    return np.where((d>0)&(t>1e-5),t,np.inf)


def cube_distance(origin, direction, center=None, half=None):
    center = np.zeros(3) if center is None else center
    half = np.ones(3) if half is None else half
    origin = origin-center
    with np.errstate(divide="ignore",invalid="ignore"):
        a,c = (-half-origin)/direction,(half-origin)/direction
    enter = np.minimum(a,c).max(axis=-1)
    leave = np.maximum(a,c).min(axis=-1)
    return np.where((leave>=np.maximum(enter,0))&(enter>1e-5),enter,np.inf)


def reflection_oracle(width, height, case="front", oblique=False):
    origin = np.array([4.,2.,8.] if oblique else [0.,0.,8.])
    forward = -origin/np.linalg.norm(origin)
    right = np.cross(forward,[0.,1.,0.])
    right /= np.linalg.norm(right)
    up = np.cross(right,forward)
    y,x = np.mgrid[:height,:width]
    focal = height/(2*np.tan(np.radians(25)))
    ray = ((x+.5-width/2)[...,None]*right+(height/2-y-.5)[...,None]*up+focal*forward)
    ray /= np.linalg.norm(ray,axis=-1)[...,None]
    reflection_fixture = case.startswith("reflect")
    cube_center = np.array([100. if case == "reflect-empty" else -1.8,0.,4.]) if reflection_fixture else np.zeros(3)
    half = np.array([.45,.7,.45]) if reflection_fixture else np.ones(3)
    center = np.array([.7,0.,2.5 if case == "front" or reflection_fixture else -2.5 if case == "back" else 0.])
    sphere = sphere_distance(origin,ray,center)
    primary = sphere<cube_distance(origin,ray,cube_center,half)
    point = origin+np.where(np.isfinite(sphere),sphere,0)[...,None]*ray
    normal = (point-center)/1.3
    reflected = ray-2*(ray*normal).sum(axis=-1)[...,None]*normal
    distance = cube_distance(point,reflected,cube_center,half)
    finite = np.isfinite(distance)
    target = point+np.where(finite,distance,0)[...,None]*reflected
    view = target-origin
    length = np.linalg.norm(view,axis=-1)
    target_dir = view/np.maximum(length[...,None],1e-8)
    occluder = sphere_distance(origin,target_dir,center)
    #ai: the raster target can occlude its own reflected back faces too.
    target_front = cube_distance(origin,target_dir,cube_center,half)
    z = (view*forward).sum(axis=-1)
    target_x = width/2+focal*(view*right).sum(axis=-1)/np.maximum(z,1e-8)
    target_y = height/2-focal*(view*up).sum(axis=-1)/np.maximum(z,1e-8)
    available = (finite & (occluder>length+.02) & (np.abs(target_front-length)<.02) & (z>0)
                 & (target_x>width*.08)&(target_x<width*.92)
                 & (target_y>height*.08)&(target_y<height*.92))
    hit = primary&available
    miss = primary&~finite
    #ai: erode classification boundaries so subpixel quantization is not treated as a reflection error.
    for mask in (hit,miss):
        source = mask.copy()
        for dy in range(-2,3):
            for dx in range(-2,3):
                mask &= np.roll(source,(dy,dx),(0,1))
        mask[:3] = mask[-3:] = False
        mask[:,:3] = mask[:,-3:] = False
    return hit,miss,reflected,target_x,target_y


def capture(root, case, oblique, debug, overrides=None):
    directory = root/f"{case}_{int(oblique)}_debug{debug}"
    directory.mkdir()
    hooks = dict(BASE_OFF)
    hooks.update(GX_GFX_RAYMARCH_DEPTH="2",GX_RAYMARCH_DEPTH_TEST=case,
        GX_RAYMARCH_ROUGHNESS=".05",GX_SSR_NORMALS="1",GX_T_SSR="1",
        GX_SSR_DEBUG=str(debug),GX_RT_VIEW="ssr",GX_RT_DUMP_DIR=str(directory),
        GX_RT_DUMP_FRAME="20",GX_RT_DUMP_COUNT="1",GX_FIXED_DT="0.0166666667",
        GX_FRAMES="30",GX_T_MSAA="0")
    if oblique:
        hooks["GX_RAYMARCH_DEPTH_CAMERA"] = "1"
    hooks.update(overrides or {})
    artifacts.capture_scene("--showcase-minimal",audit.scene_env(hooks,clean=True),
        width=640,height=480,fps=60,frames=30,shot_frame=20,screenshot_dir=directory,
        timeout=120,check=True)
    return read_rgba(directory,"ssr")


def write_report(root, rows, failures, complete=False):
    (root/"reflection_report.json").write_text(json.dumps(
        {"rows":rows,"failures":failures,"complete":complete,
         "fixture_passed":complete and not failures,
         "general_quality_certified":False},indent=2)+"\n")


def main():
    rows = []
    failures = []
    root = None
    try:
        with artifacts.artifact_run() as directory, audit.settings_preserved():
            root = Path(directory)
            for oblique in (False,True):
                for case in ("reflect","reflect-empty"):
                    image = capture(root,case,oblique,4)
                    hit,miss,*_ = reflection_oracle(image.shape[1],image.shape[0],case,oblique)
                    accepted = image[...,2]>.99
                    row = {"case":case,"oblique":oblique,"expected_hits":int(hit.sum()),
                           "expected_misses":int(miss.sum()),
                           "hit_recall":float(accepted[hit].mean()) if hit.any() else None,
                           "false_hits":float(accepted[miss].mean()),"recall_floor":.9,"false_hit_cap":.01}
                    if case == "reflect" and hit.sum()<20:
                        failures.append(f"{case}/{oblique}: insufficient positive oracle support")
                    if row["hit_recall"] is not None and row["hit_recall"]<.9:
                        failures.append(f"{case}/{oblique}: hit recall {row['hit_recall']:.6f} < .9")
                    if row["false_hits"]>.01:
                        failures.append(f"{case}/{oblique}: false hits {row['false_hits']:.6f} > .01")
                    if miss.sum()<1000:
                        failures.append(f"{case}/{oblique}: insufficient negative oracle support")
                    np.savez_compressed(root/f"{case}_{int(oblique)}_oracle.npz",hit=hit,miss=miss)
                    rows.append(row)
                    print(row,flush=True)
                    if case == "reflect":
                        color = capture(root,case,oblique,18)
                        attributed = (color[...,1]>.5)&(color[...,1]>color[...,0]+.2)&(color[...,1]>color[...,2]+.2)
                        row["green_target_recall"] = float(attributed[hit].mean())
                        row["color_recall_floor"] = .9
                        if row["green_target_recall"]<.9:
                            failures.append(f"{case}/{oblique}: target color recall {row['green_target_recall']:.6f} < .9")
                        verdict = "PASS" if row["green_target_recall"]>=.9 else "FAIL"
                        print(f"  green target recall={row['green_target_recall']:.6f}, floor=.9: {verdict}",flush=True)
                        rough = capture(root,case,oblique,15,{"GX_RAYMARCH_ROUGHNESS":".95"})
                        gated = (rough[...,1]>.99)&(rough[...,0]<.01)&(rough[...,2]<.01)
                        row["roughness_gate_coverage"] = float(gated[hit|miss].mean())
                        row["roughness_gate_floor"] = .99
                        if row["roughness_gate_coverage"]<.99:
                            failures.append(f"{case}/{oblique}: roughness gate coverage < .99")
                    route = capture(root,case,oblique,14)
                    fallback = (route[...,2]>.99)&(route[...,0]<.01)&(route[...,1]<.01)
                    row["miss_environment_route"] = float(fallback[miss].mean())
                    row["environment_route_floor"] = .99
                    if row["miss_environment_route"]<.99:
                        failures.append(f"{case}/{oblique}: environment miss routing < .99")
                    print(f"  controls: {row}",flush=True)
                    write_report(root,rows,failures)
            write_report(root,rows,failures,complete=True)
            if failures:
                raise RuntimeError("; ".join(failures))
        print("GFX_RAYMARCH_REFLECTION: PASS")
        return 0
    except Exception as error:
        if root is not None and not failures:
            failures.append(str(error))
            write_report(root,rows,failures)
        print(f"GFX_RAYMARCH_REFLECTION: FAIL — {error}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
