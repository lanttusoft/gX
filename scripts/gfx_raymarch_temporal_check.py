#!/usr/bin/env python3
#ai: camera-only reprojection is checked against analytic static sphere points in the actual TAA consumer.
import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit
import gfx_raymarch_check as artifacts
from gfx_feature_matrix import BASE_OFF
from gfx_raymarch_reflection_check import sphere_distance, cube_distance
from gfx_raymarch_surface_check import read_rgba
import gfx_temporal_check as temporal


def camera_basis(frame, speed):
    phase = frame*speed
    origin = np.array([8*np.sin(phase),0.,8*np.cos(phase)])
    forward = -origin/8
    right = np.cross(forward,[0.,1.,0.])
    right /= np.linalg.norm(right)
    return origin,forward,right,np.cross(right,forward)


def project(point, width, height, frame, speed):
    origin,forward,right,up = camera_basis(frame,speed)
    delta = point-origin
    z = (delta*forward).sum(-1)
    #ai: placeholder points for non-hit rays can lie on the camera plane; they are outside support.
    z = np.where(np.abs(z)>1e-10,z,1.)
    focal = height/(2*np.tan(np.radians(25)))
    uv = np.stack((.5+focal*(delta*right).sum(-1)/(width*z),
                   .5+focal*(delta*up).sum(-1)/(height*z)),axis=-1)
    depth = 100/99.9-10/(99.9*z)
    return uv,depth


def oracle(width, height, frame, speed, jitter, prev_jitter):
    origin,forward,right,up = camera_basis(frame,speed)
    y,x = np.mgrid[:height,:width]
    focal = height/(2*np.tan(np.radians(25)))
    ray = ((x+.5-width/2-jitter[0]*width/2)[...,None]*right
           +(height/2-y-.5-jitter[1]*height/2)[...,None]*up+focal*forward)
    ray /= np.linalg.norm(ray,axis=-1)[...,None]
    distance = sphere_distance(origin,ray,np.array([.7,0.,2.5]))
    support = distance<cube_distance(origin,ray)
    point = origin+np.where(np.isfinite(distance),distance,0)[...,None]*ray
    prev_uv,prev_depth = project(point,width,height,frame-1,speed)
    uv = np.stack(((x+.5)/width,1-(y+.5)/height),axis=-1)
    motion = uv-np.array(jitter)/2-prev_uv
    history_uv = prev_uv+np.array(prev_jitter)/2
    source = support.copy()
    for dy in range(-2,3):
        for dx in range(-2,3):
            support &= np.roll(source,(dy,dx),(0,1))
    support[:3] = support[-3:] = False
    support[:,:3] = support[:,-3:] = False
    return support,motion,prev_depth,history_uv


def analytic_scene_depth(width, height, frame, speed, jitter):
    origin,forward,right,up = camera_basis(frame,speed)
    y,x = np.mgrid[:height,:width]
    focal = height/(2*np.tan(np.radians(25)))
    ray = ((x+.5-width/2-jitter[0]*width/2)[...,None]*right
           +(height/2-y-.5-jitter[1]*height/2)[...,None]*up+focal*forward)
    ray /= np.linalg.norm(ray,axis=-1)[...,None]
    distance = np.minimum(sphere_distance(origin,ray,np.array([.7,0.,2.5])),cube_distance(origin,ray))
    finite = np.isfinite(distance)
    z = np.where(finite,distance,1)*(ray*forward).sum(-1)
    return np.where(finite,100/99.9-10/(99.9*z),1.)


def bilinear(image, uv):
    height,width = image.shape
    x,y = uv[...,0]*width-.5,(1-uv[...,1])*height-.5
    ix,iy = np.floor(x).astype(int),np.floor(y).astype(int)
    fx,fy = x-ix,y-iy
    x0,x1 = np.clip(ix,0,width-1),np.clip(ix+1,0,width-1)
    y0,y1 = np.clip(iy,0,height-1),np.clip(iy+1,0,height-1)
    return ((1-fy)*((1-fx)*image[y0,x0]+fx*image[y0,x1])
            +fy*((1-fx)*image[y1,x0]+fx*image[y1,x1]))


def eligibility_masks(width, height, frame, speed, jitter, previous):
    support,_,depth,uv = oracle(width,height,frame,speed,jitter,previous)
    scene = analytic_scene_depth(width,height,frame-1,speed,previous)
    difference = np.abs(bilinear(scene,uv)-depth)
    inside = ((uv>0)&(uv<1)).all(-1)
    #ai: strong classifications leave more than one half-float ULP of margin around the .001 gate.
    stable = support&inside&(difference<.0002)
    disoccluded = support&(~inside|(difference>.0017))
    return stable,disoccluded,uv,depth


#ai: selectable frames exercise consecutive snapshot chronology without changing existing default probes.
def capture(root, tag, speed, debug=1, enabled=True, no_velocity=False, previous_dump=False, edit=None,
            frame=20, motion_blur=False, unsafe=None, gap=None):
    directory = root/tag
    directory.mkdir()
    hooks = dict(BASE_OFF)
    hooks.update(GX_GFX_RAYMARCH_DEPTH="2",GX_RAYMARCH_DEPTH_TEST="front",
        GX_RAYMARCH_TEMPORAL=str(int(enabled)),GX_T_TAA="1",GX_TAA_REPROJECTION_DEBUG=str(debug),
        GX_TAA_REPROJECTION_DEBUG_FRAME=str(frame),GX_RAYMARCH_CAMERA_MOTION=str(speed),
        GX_RT_VIEW="history",GX_RT_DUMP_DIR=str(directory),GX_RT_DUMP_FRAME=str(frame),
        GX_RT_DUMP_COUNT="1",GX_T_VELOCITY_TRACE=str(frame),GX_FIXED_DT="0.0166666667",GX_FRAMES=str(frame+10))
    if motion_blur:
        hooks.update(GX_T_MOTIONBLUR="1",GX_T_TAA_SNAPSHOT_TRACE=str(frame))
    if unsafe:
        hooks["GX_TAA_UNSAFE_REPROJECTION"] = unsafe
    if gap:
        hooks["GX_TAA_HISTORY_GAP"] = str(gap)
    if no_velocity:
        hooks["GX_T_NOVELOCITY"] = "1"
    if previous_dump:
        hooks.update(GX_RT_DUMP_FRAME=str(frame-1),GX_RT_DUMP_COUNT="2")
    if edit:
        hooks["GX_RAYMARCH_TEMPORAL_EDIT"] = edit
    artifacts.capture_scene("--showcase-minimal",audit.scene_env(hooks,clean=True),
        width=640,height=480,fps=60,frames=frame+10,shot_frame=frame,screenshot_dir=directory,timeout=120,check=True)
    log = (directory/"engine.log").read_text()
    match = re.search(r"TAA_REPROJECTION_TRACE jitter=\(([^)]+)\) prevJitter=\(([^)]+)\)",log)
    if not match:
        raise RuntimeError(f"missing jitter witness: {directory}")
    jitter,previous = ([float(value) for value in match.group(i).split(",")] for i in (1,2))
    return read_rgba(directory,"history"),jitter,previous


#ai: actual TAA fallback must retain the previous camera even after motion blur saves the current VP.
def snapshot_checks(root):
    rows = []
    for frame in (10,11,12):
        tag = f"snapshot_{frame}"
        image,jitter,previous = capture(root,tag,.005,no_velocity=True,frame=frame,motion_blur=True)
        log = (root/tag/"engine.log").read_text()
        stamp = f"TAA_SNAPSHOT_TRACE frame={frame} motionBlurCommitted=1"
        if stamp not in log or log.index(stamp) > log.index("TAA_REPROJECTION_TRACE jitter="):
            raise RuntimeError(f"motion blur did not commit before snapshot probe: {tag}")
        mask,motion,depth,_ = oracle(640,480,frame,.005,jitter,previous)
        row = {"case":"snapshot","frame":frame,"motion_blur_committed":True,"support":int(mask.sum()),
               "coverage":float((image[...,3]>.99)[mask].mean()),
               "motion_max_pixels":float(np.abs((image[...,:2]-motion)*[640,480])[mask].max()),
               "current_camera_separation_pixels":float(np.abs(motion*[640,480])[mask].max()),
               "previous_depth_max_error":float(np.abs(image[...,2]-depth)[mask].max()),
               "motion_cap_pixels":.05,"depth_cap":.0005}
        rows.append(row)
        print(row,flush=True)
        if (row["support"]<1000 or row["coverage"]<.99 or row["motion_max_pixels"]>.05
            or row["current_camera_separation_pixels"]<=.10
            or row["previous_depth_max_error"]>.0005):
            raise RuntimeError(f"previous snapshot lost to current-frame motion blur: {tag}")
    return rows


#ai: invalid homogeneous candidates must match the actual fresh-input witness everywhere.
def safety_checks(root):
    current,_,_ = capture(root,"safety_current",.005,debug=6)
    baseline,_,_ = capture(root,"safety_baseline",.005,debug=0)
    contrast = float(np.abs(baseline[...,:3]-current[...,:3]).max())
    if contrast == 0:
        raise RuntimeError("unsafe-reprojection fixture has no history/current contrast")
    rows = []
    for unsafe in ("singular","worldzero","worldtiny","prevzero","prevnegative","nan","inf",
                   "worldoverflow","prevoverflow"):
        output,_,_ = capture(root,f"safety_{unsafe}",.005,debug=0,unsafe=unsafe)
        log = (root/f"safety_{unsafe}"/"engine.log").read_text()
        gl_errors = [int(value,16) for value in re.findall(
            r"TAA_REPROJECTION_TRACE (?:before|after) draw error=0x([0-9a-fA-F]+)",log)]
        row = {"case":"reprojection_safety","unsafe":unsafe,"baseline_color_contrast":contrast,
               "gl_errors":gl_errors,
               "finite":bool(np.isfinite(output).all()),
               "current_color_max_error":float(np.abs(output[...,:3]-current[...,:3]).max()),
               "current_depth_max_error":float(np.abs(output[...,3]-current[...,3]).max())}
        rows.append(row)
        print(row,flush=True)
        if (len(gl_errors) != 2 or any(gl_errors) or not row["finite"]
            or row["current_color_max_error"] != 0 or row["current_depth_max_error"] != 0):
            raise RuntimeError(f"unsafe reprojection retained history: {unsafe}")
    return rows


#ai: successful-history chronology must survive skipped entry and explicit begin/abort paths.
def gap_checks(root):
    rows = []
    for hybrid in (False,True):
        for kind in (1,2):
            tag = f"gap_{int(hybrid)}_{kind}"
            output,_,_ = capture(root,tag,.005,debug=0,enabled=hybrid,gap=kind)
            current,_,_ = capture(root,tag+"_input",.005,debug=6,enabled=hybrid,gap=kind)
            resumed,jitter,previous = capture(root,tag+"_next",.005,debug=1 if hybrid else 0,
                                             enabled=hybrid,gap=kind,frame=21)
            mask,motion,depth,_ = oracle(640,480,21,.005,jitter,previous)
            next_log = (root/(tag+"_next")/"engine.log").read_text()
            log = (root/tag/"engine.log").read_text()
            row = {"case":"history_gap","hybrid":hybrid,"kind":kind,
                   "uncommitted_stamp_preserved":f"TAA_GAP_TRACE kind={kind} last=18" in log,
                   "reset":bool(re.search(r"TAA_REPROJECTION_TRACE history .* reset=1",log)),
                   "next_reset":bool(re.search(r"TAA_REPROJECTION_TRACE history .* reset=1",next_log)),
                   "next_consecutive":bool(re.search(r"TAA_REPROJECTION_TRACE history .* reset=0",next_log)),
                   "next_motion_max_pixels":float(np.abs((resumed[...,:2]-motion)*[640,480])[mask].max()) if hybrid else None,
                   "next_previous_depth_max_error":float(np.abs(resumed[...,2]-depth)[mask].max()) if hybrid else None,
                   "current_color_max_error":float(np.abs(output[...,:3]-current[...,:3]).max()),
                   "current_depth_max_error":float(np.abs(output[...,3]-current[...,3]).max())}
            rows.append(row)
            print(row,flush=True)
            if (not row["uncommitted_stamp_preserved"] or not row["reset"]
                or not row["next_consecutive"] or row["next_reset"]
                or (hybrid and (row["next_motion_max_pixels"]>.05 or row["next_previous_depth_max_error"]>.0005))
                or row["current_color_max_error"] != 0 or row["current_depth_max_error"] != 0):
                raise RuntimeError(f"stale history on skip/abort resume: {tag}")
    return rows


#ai: exercise native lifecycle resets on authored/time/routing transitions, not just signature math.
def invalidation_checks(root):
    rows = []
    for edit in ("geometry","material","time","clock","routing"):
        production,_,_ = capture(root,f"invalidate_{edit}",0.,debug=0,edit=edit)
        current,_,_ = capture(root,f"invalidate_{edit}_input",0.,debug=6,edit=edit)
        log = (root/f"invalidate_{edit}"/"engine.log").read_text()
        reset = bool(re.search(r"TAA_REPROJECTION_TRACE history .* reset=1",log))
        row = {"case":"invalidation","edit":edit,"reset":reset,
               "current_color_max_error":float(np.abs(production[...,:3]-current[...,:3]).max()),
               "current_depth_max_error":float(np.abs(production[...,3]-current[...,3]).max())}
        rows.append(row)
        print(row,flush=True)
        (root/"invalidation_report.json").write_text(json.dumps(rows,indent=2)+"\n")
        if not reset or row["current_color_max_error"] != 0 or row["current_depth_max_error"] != 0:
            raise RuntimeError(f"stale history after {edit} transition")
    return rows


#ai: reuse the raster temporal gate's burst metrics and unchanged static budgets.
def static_burst(root):
    directory = root/"static_burst"
    directory.mkdir()
    hooks = dict(BASE_OFF)
    hooks.update(GX_GFX_RAYMARCH_DEPTH="2",GX_RAYMARCH_DEPTH_TEST="front",
        GX_RAYMARCH_TEMPORAL="1",GX_T_TAA="1",GX_RAYMARCH_CAMERA_MOTION="0",
        GX_BURST="1",GX_FIXED_DT=temporal.FIXED_DT)
    artifacts.capture_scene("--showcase-minimal",audit.scene_env(hooks,clean=True),
        width=640,height=480,fps=60,frames=140,shot_frame=temporal.TARGET_FRAME,
        screenshot_dir=directory,timeout=300,check=True,screenshot_after=True,
        extra_args=("--seed",temporal.RNG_SEED))
    frames = [np.asarray(Image.open(directory/f"burst_{i:03d}.png").convert("RGB"),dtype=np.float32)
              for i in range(temporal.BURST_LEN)]
    mask,_,_,_ = oracle(640,480,temporal.TARGET_FRAME,0.,[0,0],[0,0])
    rows = []
    for name,images in (("full",frames),("sdf_interior",[frame[mask][:,None,:] for frame in frames])):
        mean,fraction,steps = temporal.summarize(temporal.pairs(images))
        row = {"case":"static_burst","region":name,"median_mean":mean,
               "median_changed_fraction":fraction,"exposure_steps":steps,
               "mean_cap":temporal.STATIC_MEDIAN_MEAN_MAX,
               "fraction_cap":temporal.STATIC_MEDIAN_FRAC_MAX,"step_cap":temporal.STATIC_STEP_COUNT_MAX}
        rows.append(row)
        print(row,flush=True)
    (directory/"stability_report.json").write_text(json.dumps(rows,indent=2)+"\n")
    if any(row["median_mean"]>row["mean_cap"] or row["median_changed_fraction"]>row["fraction_cap"]
           or row["exposure_steps"]>row["step_cap"] for row in rows):
        raise RuntimeError("static SDF burst exceeded existing temporal budgets")
    return rows


def main():
    parser = argparse.ArgumentParser(description="Validate camera-only static SDF temporal history.")
    parser.add_argument("--only",choices=("all","reprojection","stability","invalidation","snapshots","safety","gaps"),default="all")
    args = parser.parse_args()
    rows = []
    try:
        with artifacts.artifact_run() as directory,audit.settings_preserved():
            root = Path(directory)
            if args.only in ("stability","invalidation","snapshots","safety","gaps"):
                fixtures = {"stability":static_burst,"invalidation":invalidation_checks,
                            "snapshots":snapshot_checks,"safety":safety_checks,"gaps":gap_checks}
                rows = fixtures[args.only](root)
                (root/"temporal_report.json").write_text(json.dumps({"rows":rows,"quality_certified":False},indent=2)+"\n")
                print(f"GFX_RAYMARCH_TEMPORAL: {args.only.upper()} PASS (fixture only)")
                return 0
            for speed,no_velocity in ((0.,False),(.005,False),(.005,True)):
                image,jitter,previous = capture(root,f"motion_{speed}_{int(no_velocity)}",speed,no_velocity=no_velocity)
                mask,motion,depth,_ = oracle(640,480,20,speed,jitter,previous)
                row = {"speed":speed,"forced_fallback":no_velocity,"support":int(mask.sum()),
                       "coverage":float((image[...,3]>.99)[mask].mean()),
                       "motion_max_pixels":float(np.abs((image[...,:2]-motion)*[640,480])[mask].max()),
                       "previous_depth_max_error":float(np.abs(image[...,2]-depth)[mask].max()),
                       "motion_cap_pixels":.05,"depth_cap":.0005}
                rows.append(row)
                (root/"temporal_report.json").write_text(json.dumps({"rows":rows,"quality_certified":False},indent=2)+"\n")
                print(row,flush=True)
                if (row["support"]<1000 or row["coverage"]<.99
                    or row["motion_max_pixels"]>.05 or row["previous_depth_max_error"]>.0005):
                    raise RuntimeError("analytic camera reprojection failed")
            off,_,_ = capture(root,"temporal_off",.005,enabled=False)
            if np.any(off[...,3]>.99):
                raise RuntimeError("disabled path still exposes hybrid ownership")
            image,jitter,previous = capture(root,"disocclusion",.3,debug=2)
            stable,disoccluded,uv,depth = eligibility_masks(640,480,20,.3,jitter,previous)
            mask = stable|disoccluded
            row = {"case":"disocclusion","stable_support":int(stable.sum()),
                   "disoccluded_support":int(disoccluded.sum()),
                   "stable_history_kept":float((image[...,3]>.99)[stable].mean()),
                   "disoccluded_history_rejected":float((image[...,3]<.01)[disoccluded].mean()),
                   "history_uv_max_pixels":float(np.abs((image[...,:2]-uv)*[640,480])[mask].max()),
                   "previous_depth_max_error":float(np.abs(image[...,2]-depth)[mask].max()),
                   "classification_floor":.99,"uv_cap_pixels":.35,"depth_cap":.0005}
            rows.append(row)
            np.savez_compressed(root/"disocclusion_oracle.npz",stable=stable,disoccluded=disoccluded)
            (root/"temporal_report.json").write_text(json.dumps({"rows":rows,"quality_certified":False},indent=2)+"\n")
            print(row,flush=True)
            if (row["stable_support"]<1000 or row["disoccluded_support"]<100
                or row["stable_history_kept"]<.99 or row["disoccluded_history_rejected"]<.99
                or row["history_uv_max_pixels"]>.35 or row["previous_depth_max_error"]>.0005):
                raise RuntimeError("analytic disocclusion eligibility failed")
            current,_,_ = capture(root,"current_input",.3,debug=6)
            production,_,_ = capture(root,"production_rejection",.3,debug=0)
            row = {"case":"production_rejection","support":int(disoccluded.sum()),
                   "current_color_max_error":float(np.abs(production[...,:3]-current[...,:3])[disoccluded].max()),
                   "current_depth_max_error":float(np.abs(production[...,3]-current[...,3])[disoccluded].max())}
            rows.append(row)
            (root/"temporal_report.json").write_text(json.dumps({"rows":rows,"quality_certified":False},indent=2)+"\n")
            print(row,flush=True)
            if row["current_color_max_error"] != 0 or row["current_depth_max_error"] != 0:
                raise RuntimeError("production rejected pixels retained history")
            if args.only == "all":
                rows.extend(gap_checks(root))
                rows.extend(safety_checks(root))
                rows.extend(snapshot_checks(root))
                rows.extend(static_burst(root))
                rows.extend(invalidation_checks(root))
                (root/"temporal_report.json").write_text(json.dumps({"rows":rows,"quality_certified":False},indent=2)+"\n")
        print(f"GFX_RAYMARCH_TEMPORAL: {args.only.upper()} PASS (fixture only; general quality not certified)")
        return 0
    except Exception as error:
        print(f"GFX_RAYMARCH_TEMPORAL: FAIL — {error}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
