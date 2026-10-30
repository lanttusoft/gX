#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: scoped authoring regression: production widget IDs/drags, label offsets and authored water height.
import argparse
import json
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image
import audit_common as audit

OUT = Path('/tmp/opencode/gfx_authoring_check')


def native_widgets():
    source = (audit.ROOT/'src/tools/gfx/gfx_editor.cpp').read_text()
    #ai: exercise the extracted leaf panel's production height widget too.
    source += (audit.ROOT/'src/tools/gfx/water_panel.cpp').read_text()
    widgets = []
    for field in ('bloom.intensity', 'lensflare.intensity', 'water.planeHeight'):
        match = re.search(r'ImGui::DragFloat\("([^"]+)", &gfx->'+re.escape(field)+r'[^;]+;', source)
        if not match:
            raise RuntimeError('Missing production widget: '+field)
        widgets.append((match[1], match[0]))
    label = re.search(r'ImGui::DragFloat3\("(label offset)",[^;]+;', source)
    if not label:
        raise RuntimeError('Missing label placement widget')
    widgets.append((label[1], label[0]))
    statements = []
    for index, (name, statement) in enumerate(widgets):
        statements.append(statement+f'\nIds[{index}]=ImGui::GetID({json.dumps(name)});'
            f'\nRects[{index}]=ImGui::GetItemRectMin();'
            f'\nRects[{index}].x+=10; Rects[{index}].y+=8;')
    harness = r'''
//ai: extracted production widget statements execute in the actual bundled ImGui context.
#include "imgui.h"
#include <cassert>
#include <cstdio>
struct Gfx { struct {float intensity=1;} bloom, lensflare; struct {float planeHeight=0;} water; } Data;
struct Model {struct {float textOffset[3]={0,0,0};} params;} ModelData;
Gfx *gfx=&Data;
Model *model=&ModelData;
ImGuiID Ids[4]; ImVec2 Rects[4];
void frame()
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(600,400));
    ImGui::Begin("Production widget probe");
'''+'\n'.join(statements)+r'''
    assert(ImGui::GetID("intensity")==ImGui::GetID("intensity"));
    ImGui::End();
    ImGui::Render();
}
int main()
{
    ImGui::CreateContext();
    ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(800,600); io.DeltaTime=1.0f/60;
    unsigned char *pixels; int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    frame(); frame();
    for(int i=0;i<4;i++) for(int j=i+1;j<4;j++) assert(Ids[i]!=Ids[j]);
    for(int target=0;target<4;target++)
    {
        Data.bloom.intensity=1; Data.lensflare.intensity=1;
        Data.water.planeHeight=0; ModelData.params.textOffset[0]=0;
        ImVec2 point=Rects[target];
        io.AddMousePosEvent(point.x,point.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(point.x+60,point.y); frame(); frame();
        io.AddMouseButtonEvent(0,false); frame();
        float values[4]={Data.bloom.intensity,Data.lensflare.intensity,
            Data.water.planeHeight,ModelData.params.textOffset[0]};
        for(int i=0;i<4;i++)
        {
            float initial=i<2 ? 1.0f : 0.0f;
            if(i==target) assert(values[i]>initial);
            else assert(values[i]==initial);
        }
        assert(ModelData.params.textOffset[1]==0 && ModelData.params.textOffset[2]==0);
        printf("AUTHORING_WIDGET: target=%d isolated=1\n",target);
    }
    ImGui::DestroyContext();
}
'''
    path = OUT/'widgets.cpp'
    path.write_text(harness)
    imgui = audit.ROOT/'src/tools/imgui'
    command = ['c++', '-std=c++17', '-O0', '-I'+str(imgui), str(path)]
    command += [str(imgui/name) for name in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')]
    binary = OUT/'widgets'
    command += ['-o',str(binary)]
    build = subprocess.run(command,capture_output=True,text=True,timeout=120)
    (OUT/'widget_build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        raise RuntimeError('Widget harness build failed')
    run = subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
    (OUT/'widgets.log').write_text(run.stdout+run.stderr)
    if run.returncode:
        raise RuntimeError('Production widget ID/isolated edit regression')
    print(run.stdout,end='')


def capture(tag, preset):
    directory=OUT/tag
    directory.mkdir(exist_ok=True)
    path=directory/'input.json'
    saved=directory/'saved.json'
    path.write_text(json.dumps(preset))
    env=audit.scene_env({'GX_FIXED_DT':'0.0166666667','GX_GFX_PRESET_LOAD':str(path),
        'GX_GFX_PRESET_SAVE':str(saved),'GX_DOCK_TRACE':'1','GX_GFX_SNAPSHOT_SMOKE':'1'},clean=True)
    run=audit.run_scene('--gfx',env,frames=30,shot_frame=30,fps=None,
        screenshot_dir=directory,timeout=120,check=True,extra_args=('--gfx-editor',))
    (directory/'renderer.log').write_text(run.stdout or '')
    if 'GFX_LABEL_HISTORY: PASS' not in (run.stdout or ''):
        raise RuntimeError('Label offset undo/redo did not pass in editor')
    pixels=np.asarray(Image.open(audit.newest_shot(directory)).convert('RGB'),dtype=np.float32)
    x,y,w,h=audit.dock_viewport(audit.strip_ansi(run.stdout or ''),pixels.shape[1],pixels.shape[0])
    return pixels[y:y+h,x:x+w],json.loads(saved.read_text())


#ai: sparse owned-primitive scene avoids author presets and quarantined model dependencies.
def label_fixture():
    return {
        'name': 'Owned label regression',
        'state': {'ENABLE_REFLECTIONS': False, 'ENABLE_PLANAR_REFLECTIONS': False,
                  'ENABLE_SSR': False, 'ENABLE_SSAO': False, 'ENABLE_VOLUME_FOG': False,
                  'ENABLE_BLOOM': False, 'ENABLE_TAA': False, 'ENABLE_WATER': False,
                  'ENABLE_PROCEDURAL_SKY': False},
        'sky': {'color': [20, 20, 20, 255]},
        'models': [{'path': 'resources/model/primitive/cube.glb',
                    'params': {'pos': [0, 0, 0], 'rotation': [0, 180, 0], 'scale': [1, 1, 1]}}],
        'lights': [], 'textures': [],
    }


def main(labels_only=False):
    OUT.mkdir(parents=True,exist_ok=True)
    native_widgets()
    with audit.settings_preserved():
        probe=label_fixture()
        probe['camera']={'position':[0,2,-10],'target':[0,0,0],'up':[0,1,0],
            'fovy':45,'projection':0}
        params=probe['models'][0]['params']
        #ai: the fixed quad must face this fixture's camera; retain identical geometry in all A/B shots.
        params.update(textVisible=False,textString='OFFSET',textFontPath='',textFontSize=32.0,
            pos=[0,0,0],rotation=[0,180,0],textWorldSize=[4.0,1.0])
        original_position=params['pos']
        off,legacy=capture('label_off',probe)
        if legacy['models'][0]['params']['textOffset']!=[0,0,0]:
            raise RuntimeError('Legacy preset lacks zero-default label offset')
        for billboard in (True,False):
            params.update(textVisible=True,textBillboard=billboard,textOffset=[0,0,0])
            origin,_=capture('label_origin_'+str(billboard),probe)
            params['textOffset']=[2,1,0]
            moved,saved=capture('label_moved_'+str(billboard),probe)
            stored=saved['models'][0]['params']
            if stored['textOffset']!=[2,1,0] or stored['pos']!=original_position:
                raise RuntimeError('Label offset/model anchor round-trip changed')
            masks=[np.abs(image-off).mean(axis=2)>40 for image in (origin,moved)]
            if any(mask.sum()<30 for mask in masks):
                raise RuntimeError('Label missing from origin/offset scene')
            centers=[np.array(np.nonzero(mask)).mean(axis=1) for mask in masks]
            distance=float(np.linalg.norm(centers[1]-centers[0]))
            if distance<10:
                raise RuntimeError('Label offset did not move scene glyphs')
            print('AUTHORING_LABEL: billboard=',billboard,'centroid_shift=',distance)
            #ai: equal world anchors reached via model translation or label offset must coincide.
            params.update(pos=[2,1,0],textOffset=[0,0,0],textVisible=False)
            translated_off,_=capture('label_translated_off_'+str(billboard),probe)
            params['textVisible']=True
            translated,_=capture('label_translated_'+str(billboard),probe)
            follow_mask=np.abs(translated-translated_off).mean(axis=2)>40
            if follow_mask.sum()<30:
                raise RuntimeError('Translated-model label missing')
            follow_center=np.array(np.nonzero(follow_mask)).mean(axis=1)
            residual=float(np.linalg.norm(follow_center-centers[1]))
            if residual>3:
                raise RuntimeError('Label did not follow its translated model anchor')
            print('AUTHORING_LABEL_FOLLOW: billboard=',billboard,'centroid_residual=',residual)
            params['pos']=original_position
        if not labels_only:
            params['textVisible']=False
            probe['state']['ENABLE_WATER']=True
            water=probe.setdefault('water',{})
            images=[]
            for height in (0,1.5):
                water['planeHeight']=height
                image,saved=capture('water_'+str(height),probe)
                if saved['water']['planeHeight']!=height:
                    raise RuntimeError('Enabled-water updates erased authored height')
                images.append(image)
            delta=float(np.abs(images[1]-images[0]).mean())
            if delta<0.5:
                raise RuntimeError('Authored water height did not change scene pixels')
            print('AUTHORING_WATER: height retained; pixel mean_delta=',delta)
    print('GFX_AUTHORING_CHECK: PASS'+(' (labels-only)' if labels_only else ''))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--labels-only',action='store_true',
                        help='Check labels independently of the pending water-height repair')
    main(parser.parse_args().labels_only)
