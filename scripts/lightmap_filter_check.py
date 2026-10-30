#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: opt-in owned GPU fixture checks heap lightmap point/bilinear sampling without game assets.
import argparse
import copy
import json
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/lightmap_filter_check'))
    parser.add_argument('--binary', type=Path, default=audit.BIN)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    image = np.array([[[25, 45, 100], [220, 100, 30]],
                      [[80, 180, 60], [180, 220, 240]]], dtype=np.uint8)
    texture = out / 'owned_lightmap.png'
    Image.fromarray(image).save(texture)
    positions = np.array([[-1,-1,0],[1,-1,0],[1,1,0],[-1,-1,0],[1,1,0],[-1,1,0]], dtype='<f4')
    uvs = np.array([[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]], dtype='<f4')
    normals = np.tile([0,0,1], (6,1)).astype('<f4')
    binary = bytearray()
    views, accessors = [], []
    for array, kind in ((positions,'VEC3'), (normals,'VEC3'), (uvs,'VEC2'), (uvs,'VEC2')):
        payload = array.tobytes()
        views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(payload)})
        accessors.append({'bufferView':len(views)-1,'componentType':5126,
                          'count':len(array),'type':kind,'min':array.min(0).tolist(),
                          'max':array.max(0).tolist()})
        binary.extend(payload)
    doc = {'asset':{'version':'2.0'},'buffers':[{'byteLength':len(binary)}],
           'bufferViews':views,'accessors':accessors,
           'materials':[{'name':'OwnedWhiteSurface','doubleSided':True,
                         'pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1],
                                                'metallicFactor':0,'roughnessFactor':1}}],
           'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,
                                                  'TEXCOORD_0':2,'TEXCOORD_1':3},'material':0}]}],
           'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
    encoded = json.dumps(doc,separators=(',',':')).encode()
    encoded += b' '*((-len(encoded))%4)
    model = out / 'owned_quad.glb'
    model.write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))
                      +struct.pack('<II',len(encoded),0x4e4f534a)+encoded
                      +struct.pack('<II',len(binary),0x004e4942)+binary)
    base = json.loads((audit.ROOT/'resources/tools/gfx_lightmap.json').read_text())
    base['models'][0]['path'] = str(model)
    base['models'][0]['params'].update(emissiveFactor=0,emissiveColor=[0,0,0,255])
    base['textures'] = [{'path':str(texture)}]
    base['sky'] = {'color':[40,40,40,255],'strength':0}
    base['state'].update(ENABLE_PROCEDURAL_SKY=False,ENABLE_LIGHTING_AMBIENT=False,
                         ENABLE_LIGHTING_DIFFUSE=False,ENABLE_LIGHTING_SPECULAR=False,
                         ENABLE_FXAA=False)
    view = {'position':[0,0,4],'target':[0,0,0],'up':[0,1,0],'fovy':45,'projection':0}
    base['camera'] = view
    base['cameras'] = [{'name':'Owned quad','view':view}]
    captures = {}
    for label, filtering in (('point',0),('bilinear',1)):
        preset = copy.deepcopy(base)
        preset['state']['textureFilter'] = filtering
        path = out / (label+'.json')
        path.write_text(json.dumps(preset,indent=2)+'\n')
        capture = out / (label+'.png')
        env = audit.scene_env(clean=True,GX_GFX_PRESET_LOAD=str(path),
                              GX_GFX_RENDER_CAMERA=str(capture),GX_GFX_RENDER_CAMERA_INDEX='0',
                              GX_FIXED_DT='0.0166666667',GX_FRAMES='90',GX_NO_AE='1')
        result = subprocess.run([str(args.binary.resolve()),'--screenWidth','1024',
                                 '--screenHeight','768','--targetFPS','60','--gfx','--gfx-editor'],
                                cwd=audit.ROOT,env=env,stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,text=True,timeout=300)
        (out/(label+'.log')).write_text(result.stdout)
        if result.returncode or not capture.exists():
            raise RuntimeError('Capture failed: '+label)
        captures[label] = np.array(Image.open(capture).convert('RGB'))
    h,w = captures['point'].shape[:2]
    roi = (slice(h*3//8,h*5//8),slice(w*3//8,w*5//8))
    counts = {label:len(np.unique(array[roi].reshape(-1,3),axis=0))
              for label,array in captures.items()}
    if any(count < 4 for count in counts.values()):
        raise RuntimeError('Fixture coverage missing: fewer than four sampled colors')
    delta = float(np.abs(captures['point'][roi].astype(float)
                        -captures['bilinear'][roi].astype(float)).mean())
    report = {'roi_unique_colors':counts,'roi_mean_abs_delta':delta,
              'scope':'owned GPU lightmap sampling; no BSP brightness/UV/atlas parity claim'}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    if delta <= 1 or counts['bilinear'] <= counts['point']*3:
        raise RuntimeError('Bilinear interpolation not distinguished from point sampling')


if __name__ == '__main__':
    main()
