#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: authored water-normal scrolling, native widget isolation, same-process defaults and signed GPU detail probes.
import argparse
import copy
import json
from pathlib import Path
import re
import tempfile

import numpy as np

import audit_common as audit
from water_ripple_check import compile_run
from water_refraction_check import capture, scene


def widget(out):
    source = (audit.ROOT / 'src/tools/gfx/water_panel.cpp').read_text()
    scroll_statement = re.search(r'ImGui::DragFloat2\("normal scroll U/V",.*?;', source, re.S).group()
    strength_statement = re.search(
        r'ImGui::DragFloat\("procedural normal strength",.*?;', source, re.S).group()
    scale_statement = re.search(
        r'ImGui::DragFloat\("procedural normal scale",.*?;', source, re.S).group()
    harness = r'''
//ai: production water controls, actual mouse drags; unrelated settings remain untouched.
#include "imgui.h"
#include <cassert>
struct Gfx {struct {float normalMapScroll[2]={0,0};float proceduralNormalStrength=1.0f;float proceduralNormalScale=1.0f;int waveCount=0;bool automaticRipples=false;} water;} Data;
Gfx *gfx=&Data;ImVec2 ScrollPoint,StrengthPoint,ScalePoint;
void frame() {
    ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(600,200));ImGui::Begin("Normal scrolling");
'''+scroll_statement+r'''
    ScrollPoint=ImGui::GetItemRectMin();
'''+strength_statement+r'''
    StrengthPoint=ImGui::GetItemRectMin();
'''+scale_statement+r'''
    ScalePoint=ImGui::GetItemRectMin();
    ImGui::End();ImGui::Render();
}
void drag(ImVec2 point,float dx) {
    ImGuiIO &io=ImGui::GetIO();float x=point.x+10,y=point.y+8;
    io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(x+dx,y);frame();frame();io.AddMouseButtonEvent(0,false);frame();
}
int main() {
    ImGui::CreateContext();ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.0f/60;
    unsigned char *pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    frame();frame();drag(ScrollPoint,60);assert(Data.water.normalMapScroll[0]>0 && Data.water.normalMapScroll[1]==0);
    Data.water.normalMapScroll[0]=0;for(int i=0;i<30;++i)frame();
    drag(ScrollPoint,-60);assert(Data.water.normalMapScroll[0]<0 && Data.water.normalMapScroll[1]==0);
    Data.water.proceduralNormalStrength=1.0f;Data.water.proceduralNormalScale=1.0f;frame();
    drag(StrengthPoint,60);assert(Data.water.proceduralNormalStrength>1.0f);
    assert(Data.water.proceduralNormalScale==1.0f && Data.water.normalMapScroll[0]<0);
    Data.water.proceduralNormalStrength=1.0f;frame();
    drag(ScalePoint,60);assert(Data.water.proceduralNormalScale>1.0f);
    assert(Data.water.proceduralNormalStrength==1.0f && Data.water.normalMapScroll[0]<0);
    assert(Data.water.waveCount==0 && !Data.water.automaticRipples);
    ImGui::DestroyContext();
}
'''
    compile_run(out, 'scroll_widget', harness, cpp=True)


def persistence(out, binary):
    base = scene()
    jobs = [('signed', [0.12, -0.08]), ('stopped', [0, 0]), ('legacy', None),
            ('signed_again', [-0.2, 0.1]), ('absent', None)]
    entries = []
    for tag, rates in jobs:
        fixture = copy.deepcopy(base)
        if rates is None:
            fixture['water'].pop('normalMapScroll', None)
        else:
            fixture['water']['normalMapScroll'] = rates
        if tag in ('legacy', 'absent'):
            fixture['water'].pop('proceduralNormalStrength', None)
            fixture['water'].pop('proceduralNormalScale', None)
        if tag == 'absent':
            fixture.pop('water')
        source = out / (tag + '.json')
        source.write_text(json.dumps(fixture))
        entries.append(str(source) + '\t' + str(out / (tag + '_saved.json')))
    manifest = out / 'batch.tsv'
    manifest.write_text('\n'.join(entries) + '\n')
    run = audit.run_scene('--gfx', audit.scene_env({'GX_GFX_PRESET_BATCH': str(manifest)}, clean=True),
                          binary=binary, frames=3, fps=None, extra_args=('--gfx-editor',), check=True)
    (out / 'persistence.log').write_text(audit.strip_ansi(run.stdout or ''))
    for tag, rates in jobs:
        saved_water = json.loads((out / (tag + '_saved.json')).read_text())['water']
        actual = saved_water['normalMapScroll']
        if not np.allclose(actual, rates if rates is not None else [0.01, 0.01], rtol=0, atol=1e-7):
            raise RuntimeError('Normal-scroll same-process round trip/default failed: ' + tag)
        if not np.allclose(saved_water['proceduralNormalStrength'], 1.0, rtol=0, atol=1e-7):
            raise RuntimeError('Procedural normal strength legacy default failed: ' + tag)
        if not np.allclose(saved_water['proceduralNormalScale'], 1.0, rtol=0, atol=1e-7):
            raise RuntimeError('Procedural normal scale legacy default failed: ' + tag)


#ai: fixed-time samples compare authored phase changes, not temporal animation of unrelated water systems.
def visual(out, binary):
    base = scene()
    base['water'].update(normalMapPath='resources/texture/texture_normal.png',
                         normalMapStrength=0.6, automaticRipples=False)
    base['state']['ENABLE_SHADOWS'] = False
    images = {}
    for tag, rates, strength in [('legacy', None, 0.6), ('default', [0.01, 0.01], 0.6),
                                 ('stopped', [0, 0], 0.6), ('positive', [0.12, 0.08], 0.6),
                                 ('negative', [-0.12, -0.08], 0.6),
                                 ('no_normals_stop', [0, 0], 0), ('no_normals_move', [0.12, -0.08], 0)]:
        fixture = copy.deepcopy(base)
        fixture['water']['normalMapStrength'] = strength
        if rates is None:
            fixture['water'].pop('normalMapScroll', None)
        else:
            fixture['water']['normalMapScroll'] = rates
        source = out / 'input.json'
        source.write_text(json.dumps(fixture))
        images[tag], _ = capture(out, tag, fixture, binary, {'GX_GFX_PRESET_LOAD': str(source)})
    metrics = {}
    for a, b, exact in [('legacy', 'default', True), ('no_normals_stop', 'no_normals_move', True),
                         ('stopped', 'positive', False), ('positive', 'negative', False)]:
        delta = np.abs(images[a] - images[b])
        metrics[a + '_' + b] = {'max': float(delta.max()), 'changed_pixels': int((delta.max(axis=2) > 0).sum())}
        if (exact and delta.max() != 0) or (not exact and metrics[a + '_' + b]['changed_pixels'] < 30):
            raise RuntimeError('Normal-scroll visual witness failed: ' + a + '/' + b)
    return metrics


def checks(out, binary):
    out.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run_', dir=out))
    widget(run)
    persistence(run, binary)
    metrics = visual(run, binary)
    (run / 'report.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print('WATER_SCROLL_CHECK: PASS signed widget, persistence, legacy defaults and GPU detail isolation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.BIN)
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/wsc'))
    args = parser.parse_args()
    with audit.settings_preserved():
        checks(args.out.resolve(), args.binary.resolve())
