#!/usr/bin/env python3
#ai: native perspective reconstruction and exact scalar GTAO contracts; not GPU normalization/pow proofs.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import struct
import subprocess
from planar_geometry_check import ROOT,close
from reprojection_residual_check import require,row,normalize,check_case


def horizon(h,s,k):
    return h+max(Q(0),s-h)*k


def step_distance(radius,fraction,step):
    return radius*(step+fraction)/6


def texel_center(uv,size):
    require(size>0,'invalid texture size')
    index=min(max(math.floor(uv*size),0),size-1)
    return (Q(index)+Q(1,2))/size


#ai: velocity B packs previous projected depth above the legacy valid marker.
def encode_previous_depth(depth):
    if depth is None or depth < 0 or depth > 1:
        return Q(7,2)
    return Q(2) + depth


def history_depth_matches(payload,history_depth,tolerance=Q(5,1000)):
    if payload == 1:
        return None
    if payload < 2 or payload > 3:
        return False
    return abs(payload - 2 - history_depth) <= tolerance


def half_float(value):
    return struct.unpack('>e',struct.pack('>e',float(value)))[0]


#ai: pin strict GTAO temporal outlier rejection without changing its blend policy.
def source_check():
    shader=(ROOT/'resources/shader/gfx_gtao.frag').read_text()
    for phrase in ['floor(screenPosition * depthSize)','invProjection * clipSpacePosition',
                   'viewSpacePosition.xyz / viewSpacePosition.w','1.0 - fragTexCoord.y',
                   'if (depth >= 0.999) return','if (sd >= 0.999) continue',
                   '(float(j) + stepFraction) / float(STEPS)',
                   'dot(viewDelta, viewDelta) == 0.0','dot(viewNormal, viewDelta) <= max(aoBias, 0.0)',
                   '2.0 / float(j + 2)']:
        require(phrase in shader,'GTAO source drift: '+phrase)
    require('suv.y = 1.0 - suv.y' not in shader,'sample UV double-flip')
    draw=(ROOT/'src/renderer/gfx/draw.c').read_text()
    require('gtaoBiasLoc, gfx->ssao.bias' in draw,'authored bias upload missing')
    require('gX_Matrix invProj = gX_Matrix_Invert(proj);' in draw,'projection-only inverse source changed')
    texture=(ROOT/'src/renderer/gfx/texture.c').read_text()
    require('gX_Texture_SetFilter(map->wrapper.rt.depth, gX_TEXTURE_FILTER_POINT)' in texture,'nearest-depth premise changed')
    accum=(ROOT/'resources/shader/gfx_gtao_accum.frag').read_text()
    require('float pad = 0.03;' in accum,'GTAO temporal AO pad changed')
    require('bool historyInNeighborhood = surfaceMatches' in accum
            and '&& hist >= mn - pad && hist <= mx + pad;' in accum,
            'GTAO temporal history outlier rejection missing')
    require('float acceptedHistory = historyInNeighborhood ? hist : cur;' in accum,
            'GTAO temporal reject fallback changed')
    require('mix(0.85, 0.30, smoothstep(0.05, 0.25, velLen))' in accum,
            'GTAO temporal blend weights or velocity thresholds changed')
    require('uniform int   cameraMoving = 0;' in accum
            and 'cameraMoving != 1 || abs(hist - cur) <= pad' in accum,
            'GTAO camera-motion reactive history gate missing or changed')
    require('acc = mix(cur, acceptedHistory, blend);' in accum,
            'GTAO temporal history rejection is not applied')
    require('clamp(hist, mn - pad, mx + pad)' not in accum,
            'GTAO temporal history reverted to clamp-only behavior')
    require('sampleDepthMatchedHistory(histUv, velocityTap.b - 2.0, hist)' in accum,
            'GTAO temporal history is not surface-depth matched')
    require('abs(tap.g - expectedDepth) <= DEPTH_HISTORY_TOLERANCE' in accum,
            'GTAO history depth taps are not filtered by surface correspondence')
    require('finalColor = vec4(acc, currentDepth, 0.0, 1.0);' in accum,
            'GTAO accumulated history no longer stores current depth')
    require('vec2 currentPixelUv = (vec2(depthCoord) + 0.5) / vec2(depthSize);' in accum
            and 'histUv = currentPixelUv - velocityTap.rg;' in accum,
            'GTAO motion must reproject from the exact depth/velocity texel center')
    velocity=(ROOT/'resources/shader/gfx_velocity.frag').read_text()
    require('2.0 + prevDepth : 3.5' in velocity,
            'raster velocity no longer carries previous projected depth')
    terrainVelocity=(ROOT/'resources/shader/gfx_terrain_velocity.frag').read_text()
    require('2.0 + prevDepth : 3.5' in terrainVelocity,
            'terrain velocity no longer carries previous projected depth')
    load=(ROOT/'src/renderer/gfx/load.c').read_text()
    require('GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.gtaoAccumA);' in load
            and 'GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.gtaoAccumB);' in load,
            'GTAO history targets must retain depth precision')
    require('gfx->velocity.map.filter = gX_TEXTURE_FILTER_POINT;' in load,
            'velocity payload must not be interpolated across surfaces')
    draw=(ROOT/'src/renderer/gfx/draw.c').read_text()
    require('gfx->ssao.gtaoAccDepthMapLoc' in draw
            and 'gfx->bloom.hdrScene.wrapper.rt.depth' in draw,
            'GTAO depth input is not bound to the accumulation pass')
    update=(ROOT/'src/renderer/gfx/update.c').read_text()
    require('gX_Shader_GetLocation(ga, "depthMap")' in update,
            'GTAO depth sampler location is not cached')
    velocityPass=(ROOT/'src/renderer/gfx/velocity.c').read_text()
    require('gtaoTemporalActive = gfx->state.ENABLE_SSAO && gfx->ssao.useGTAO' in velocityPass
            and '&& !gtaoTemporalActive' in velocityPass,
            'velocity pass skips temporal GTAO when other vector consumers are disabled')
    terrainVelocityIndex=velocityPass.index('gX_RendererGFX_Terrain_VelocityQuad(gfx);')
    depthClearIndex=velocityPass.index('gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);',terrainVelocityIndex)
    depthEnableIndex=velocityPass.index('gX_RHI_Enable(gX_RHI_DEPTH_TEST);',depthClearIndex)
    modelDrawIndex=velocityPass.index('GX_HEAP_FOREACH(gfx->models',depthEnableIndex)
    require('gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &velPrevDepthWrite);' in velocityPass
            and 'gX_RHI_Disable(gX_RHI_DEPTH_TEST);' in velocityPass
            and terrainVelocityIndex < depthClearIndex < depthEnableIndex < modelDrawIndex
            and 'gX_RHI_Depth_Write(velPrevDepthWrite);' in velocityPass,
            'velocity pass must seed fallback motion without depth, then depth-resolve models and restore state')
    require('velocity_matrix_changed(currVP, gfx->velocity.prevVP)' in velocityPass
            and 'gfx->velocity.cameraMoved = gfx->velocity.hasPrevVP' in velocityPass
            and 'gtaoAccCameraMovedLoc' in draw
            and 'gX_Shader_GetLocation(ga, "cameraMoving")' in update,
            'GTAO camera-motion state is not propagated from the velocity pass')
    require('|| (gfx->state.ENABLE_SSAO && gfx->ssao.useGTAO)' in update,
            'velocity shader/target stay unloaded for GTAO-only temporal use')


def native_check(text):
    for line in text.splitlines():
        if line.startswith('REPROJECTION perspective '):
            fields=line.split()[2:]
            residual=check_case('perspective',fields)
            q=list(map(Q.from_float,map(float.fromhex,fields)))
            p,inv=q[:16],q[16:32]
            view=q[48:52]
            clip=row(view,p)
            require(clip[3]>0,'perspective fixture has nonpositive clip w')
            uvdepth=[(x/clip[3]+1)/2 for x in clip[:3]]
            converted=[2*x-1 for x in uvdepth]+[Q(1)]
            require(converted==normalize(clip),'UV/depth conversion identity failed')
            recovered=normalize(row(converted,inv))
            close(recovered,view)
            close(q[52:56],view)
            flipped=converted.copy()
            flipped[1]=-flipped[1]
            wrong=normalize(row(flipped,inv))
            require(abs(wrong[1]-view[1])>Q(1,10),'asymmetric Y-flip negative not exercised')
            return {'input_view':list(map(float,view)),'native_view':list(map(float,q[52:56])),
                    'exact_stored_inverse_error':list(map(float,[a-b for a,b in zip(recovered,view)])),
                    'double_flip_y_error':float(abs(wrong[1]-view[1])),
                    'residual_report':residual}
    raise ValueError('missing native perspective fixture')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build/gX_build')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    source_check()
    run=subprocess.run([str(args.binary.resolve()),'--test-only'],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP='1',GX_REPROJECTION_DUMP='1',GX_NO_SETTINGS_SAVE='1'),
        capture_output=True,text=True,check=True,timeout=60)
    report={'result':'pass','native':native_check(run.stdout),'limits':
        'One native perspective witness; exact inverse is conditional in Lean. Texel selection/ties, normalizations, acos/sqrt/pow, GPU arithmetic, temporal range and physical AO completeness unproved.'}
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('GTAO_CONTRACTS: PASS — native reconstruction, asymmetric flip and source guards')


if __name__=='__main__':
    main()
