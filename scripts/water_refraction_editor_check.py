#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: authored experimental refraction persistence/native checkbox/live state controls; no asset import.
import argparse
import copy
import json
from pathlib import Path
import re

import numpy as np

import audit_common as audit
from water_ripple_check import compile_run
from water_refraction_check import capture, scene


#ai: reuse the native widget compiler while executing the actual checkbox and launch-override disable block.
def widget(out):
    source = (audit.ROOT / 'src/tools/gfx/water_panel.cpp').read_text()
    statement = re.search(r'gX_I1 sceneRefraction =.*?ImGui::EndDisabled\(\);', source, re.S).group()
    harness = r'''
//ai: extracted experimental editor control, actual ImGui mouse events and override refusal.
#include "imgui.h"
#include <cassert>
#include <cstdlib>
typedef bool gX_I1;
struct Gfx {struct {bool sceneRefractionEnabled=false;bool automaticRipples=true;} water;} Data;
Gfx *gfx=&Data;
ImVec2 Point;
void frame() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(600,200));
    ImGui::Begin("Experimental water widget");
'''+statement+r'''
    Point=ImGui::GetItemRectMin();Point.x+=8;Point.y+=8;
    ImGui::End();ImGui::Render();
}
void click() {
    ImGuiIO &io=ImGui::GetIO();
    io.AddMousePosEvent(Point.x,Point.y);frame();
    io.AddMouseButtonEvent(0,true);frame();
    io.AddMouseButtonEvent(0,false);frame();
}
int main() {
    unsetenv("GX_WATER_REFRACTION");
    ImGui::CreateContext();ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.0f/60;
    unsigned char *pixels;int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    frame();frame();click();assert(Data.water.sceneRefractionEnabled);
    click();assert(!Data.water.sceneRefractionEnabled);
    setenv("GX_WATER_REFRACTION","1",1);frame();click();
    assert(!Data.water.sceneRefractionEnabled && Data.water.automaticRipples);
    ImGui::DestroyContext();
}
'''
    compile_run(out, 'refraction_widget', harness, cpp=True)


def persistence(out, binary):
    base = scene()
    base['water']['sceneRefractionEnabled'] = True
    disabled = copy.deepcopy(base)
    disabled['water']['sceneRefractionEnabled'] = False
    legacy = copy.deepcopy(base)
    legacy['water'].pop('sceneRefractionEnabled')
    absent = copy.deepcopy(legacy)
    absent.pop('water')
    jobs = [('enabled', base, True), ('disabled', disabled, False),
            ('enabled_again', base, True), ('legacy', legacy, False),
            ('enabled_last', base, True), ('absent_water', absent, False)]
    entries = []
    for name, fixture, _ in jobs:
        source = out / (name + '.json')
        source.write_text(json.dumps(fixture))
        entries.append(str(source) + '\t' + str(out / (name + '_saved.json')))
    manifest = out / 'batch.tsv'
    manifest.write_text('\n'.join(entries) + '\n')
    result = audit.run_scene('--gfx', audit.scene_env({'GX_GFX_PRESET_BATCH': str(manifest)}, clean=True),
        binary=binary, frames=3, fps=None, extra_args=('--gfx-editor',), check=True)
    (out / 'persistence.log').write_text(audit.strip_ansi(result.stdout or ''))
    for name, _, expected in jobs:
        saved = json.loads((out / (name + '_saved.json')).read_text())
        if saved['water']['sceneRefractionEnabled'] is not expected:
            raise RuntimeError('Refraction persistence/legacy default failed: ' + name)
        for key in saved['water']:
            if key.startswith('refraction'):
                raise RuntimeError('Transient capture state leaked into water preset: ' + key)


#ai: execute the actual shader selection block with ownership witnesses for unsupported transmission.
def shader_fallback(out):
    source = (audit.ROOT / 'src/renderer/water.c').read_text()
    start = source.index('    {', source.index('static gX_I0 water_gpu_load'))
    block = source[start:source.index('    // Cache standard locations', start)]
    harness = r'''
//ai: native selection-block ownership witnesses; GPU correctness stays in real renderer controls.
#include <cassert>
struct gX_Shader {unsigned id;};
struct Water {gX_Shader shader;};
Water Data;
Water *water=&Data;
gX_Shader Legacy={17};
bool Supported=true,Available=true;
unsigned Unloads=0;
gX_Shader gX_Shader_Load_Ex(const char*,const char*,const char*) {return {23};}
void gX_Shader_GetDefaultLocations(gX_Shader*) {}
int gX_Shader_GetLocation(gX_Shader,const char*) {return Supported?1:-1;}
void gX_Shader_Unload(gX_Shader shader) {assert(shader.id==23);++Unloads;}
gX_Shader *gX_Resource_LoadShader(const char*,const char*) {
    assert(Unloads==1 && water->shader.id==0);
    return Available?&Legacy:nullptr;
}
void select() {
'''+block+r'''
}
int main() {
    select();assert(water->shader.id==23 && Unloads==0);
    Supported=false;select();assert(water->shader.id==17 && Unloads==1);
    Unloads=0;Available=false;select();assert(water->shader.id==0 && Unloads==1);
}
'''
    harness = '#define GX_WATER_REFRACTION_EXPERIMENT\n' + harness
    compile_run(out, 'refraction_shader_fallback', harness, cpp=True)


#ai: real renderer frames prove off invalidation and on/off/on without program swaps or launch forcing.
def live(out, binary):
    base = scene()
    base['water']['sceneRefractionEnabled'] = False
    off, _ = capture(out, 'authored_off', base, binary, {})
    enabled = copy.deepcopy(base)
    enabled['water']['sceneRefractionEnabled'] = True
    on, _ = capture(out, 'authored_on', enabled, binary, {})
    metrics = {}
    for mode, expected in [('on', on), ('off', off), ('cycle', on)]:
        image, log = capture(out, 'live_' + mode, base, binary,
                             {'GX_WATER_REFRACTION_TEST_TOGGLE': mode})
        rows = re.findall(r'WATER_REFRACTION_TOGGLE: frame=(\d+) authored=(\d+) valid=(\d+) shader=(\d+)', log)
        if not rows or len({row[3] for row in rows}) != 1:
            raise RuntimeError('Live transition missing or shader program changed')
        states = {int(frame): (int(authored), int(valid)) for frame, authored, valid, _ in rows}
        for frame, (authored, valid) in states.items():
            wanted = frame >= 12 and (mode == 'on' or frame < 20 or (mode == 'cycle' and frame >= 24))
            if authored != int(wanted) or valid != int(wanted):
                raise RuntimeError('Wrong live capture validity at frame ' + str(frame))
        if not {12, 20, 24}.issubset(states):
            raise RuntimeError('Live transition frame witnesses missing')
        delta = float(np.abs(image - expected).max())
        metrics[mode + '_reference_max_delta'] = delta
        if delta != 0:
            raise RuntimeError('Live state differs from fresh authored-state reference')
    if int((np.abs(on - off).max(axis=2) > 0).sum()) < 30:
        raise RuntimeError('Authored refraction did not change the owned fixture')
    override, _ = capture(out, 'launch_override_off', enabled, binary, {'GX_WATER_REFRACTION': '0'})
    if np.abs(override - off).max() != 0:
        raise RuntimeError('Explicit launch override did not force legacy fallback')
    return metrics


def checks(out, binary):
    out.mkdir(parents=True, exist_ok=False)
    widget(out)
    shader_fallback(out)
    persistence(out, binary)
    metrics = live(out, binary)
    if audit.BIN.is_file() and audit.BIN.resolve() != binary.resolve():
        fixture = scene()
        fixture['water']['sceneRefractionEnabled'] = True
        saved = out / 'production_saved.json'
        production, _ = capture(out, 'production_legacy', fixture, audit.BIN,
                                {'GX_GFX_PRESET_SAVE': str(saved)})
        if 'sceneRefractionEnabled' in json.loads(saved.read_text())['water']:
            raise RuntimeError('Experiment authoring leaked into production preset schema')
        fixture['water']['sceneRefractionEnabled'] = False
        experiment, _ = capture(out, 'experiment_legacy', fixture, binary,
                                {'GX_GFX_PRESET_SAVE': str(out / 'experiment_saved.json')})
        metrics['production_default_max_delta'] = float(np.abs(production - experiment).max())
        if metrics['production_default_max_delta'] != 0:
            raise RuntimeError('Dormant experiment shader differs from production legacy rendering')
    (out / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print('WATER_REFRACTION_EDITOR_CHECK: PASS native widget, persistence and live capture freshness')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT / 'build/water_refraction/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/water_refraction_editor_check'))
    args = parser.parse_args()
    with audit.settings_preserved():
        checks(args.out.resolve(), args.binary.resolve())
