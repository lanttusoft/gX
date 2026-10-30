#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: authored periodic-ripple lifecycle, native checkbox and same-process legacy-preset controls.
import copy
import json
from pathlib import Path
import re
import subprocess
import tempfile

import audit_common as audit


def compile_run(out, name, source, cpp=False):
    path = out / (name + ('.cpp' if cpp else '.c'))
    path.write_text(source)
    binary = out / name
    command = ['c++' if cpp else 'cc', '-O0', str(path), '-o', str(binary), '-lm']
    if cpp:
        imgui = audit.ROOT / 'src/tools/imgui'
        command += ['-std=c++17', '-I' + str(imgui)]
        command += [str(imgui / file) for file in
                    ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')]
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    (out / (name + '_build.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(name + ' compilation failed')
    result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=30)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(name + ' failed')


#ai: execute the actual production ripple-manager statements and explicit-spawn function.
def lifecycle(out):
    source = (audit.ROOT / 'src/renderer/water.c').read_text()
    start = source.index('    water->rippleTimer += (gX_F32)dt;')
    end = source.index('    water->rippleCount = writeIdx;', start)
    manager = source[start:end + len('    water->rippleCount = writeIdx;')]
    spawn = re.search(r'gX_I0 gX_RendererWater_SpawnRipple\(.*?\n\}', source, re.S).group()
    header = (audit.ROOT / 'src/renderer/water.h').read_text()
    constants = '\n'.join(re.findall(r'^#define GX_WATER_(?:RIPPLE_\w+|MAX_RIPPLES)\s+.*', header, re.M))
    harness = r'''
//ai: wrapper types isolate production CPU statements from the window/resource loader.
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef void gX_I0;
typedef int gX_I32;
typedef float gX_F32;
typedef double gX_F64;
typedef struct {float x,z;} gX_F32v2;
'''+constants+r'''
typedef struct {
    int automaticRipples, calmWater, rippleCount;
    float rippleTimer;
    gX_F32v2 ripplePos[GX_WATER_MAX_RIPPLES];
    float rippleAmp[GX_WATER_MAX_RIPPLES], rippleRadius[GX_WATER_MAX_RIPPLES];
    float ripplePhase[GX_WATER_MAX_RIPPLES], rippleLifetime[GX_WATER_MAX_RIPPLES];
} gX_RendererWater;
int RandomCalls;
float gX_F32_RandomRange(float a, float b) {RandomCalls++;return (a+b)*0.5f;}
void tick(gX_RendererWater *water, double dt) {
'''+manager+r'''
}
'''+spawn+r'''
static float ripple_at(float dt, int steps, int *count) {
    gX_RendererWater water={0};
    water.rippleCount=1;
    water.rippleAmp[0]=0.5f;
    water.rippleRadius[0]=0.5f;
    for(int i=0;i<steps;i++) tick(&water,dt);
    *count=water.rippleCount;
    return water.rippleAmp[0];
}
int main(void) {
    gX_RendererWater water={0};
    water.automaticRipples=1;
    tick(&water, 1.6);
    assert(water.rippleCount==1 && RandomCalls==3);
    float amplitude=water.rippleAmp[0], lifetime=water.rippleLifetime[0];
    water.automaticRipples=0;
    tick(&water, 0.5);
    assert(water.rippleCount==1 && RandomCalls==3);
    assert(water.rippleAmp[0]<amplitude && water.rippleLifetime[0]>lifetime);
    for(int i=0;i<20;i++) tick(&water, 0.5);
    assert(water.rippleCount==0 && RandomCalls==3);
    gX_RendererWater_SpawnRipple(&water, 1, 2, 0.5f);
    assert(water.rippleCount==1 && water.rippleAmp[0]==0.5f);
    tick(&water, 0.1);
    assert(water.rippleCount==1 && RandomCalls==3);
    water.automaticRipples=1;
    tick(&water, 0.1);
    assert(water.rippleCount==2 && RandomCalls==6);
    water.calmWater=1;
    tick(&water, 2);
    assert(RandomCalls==6);
    int count60=0,count30=0,countAtExpiry=0;
    float fade60=ripple_at(1.0/60.0,450,&count60);
    float fade30=ripple_at(1.0/30.0,225,&count30);
    float expired=ripple_at(1.0/60.0,480,&countAtExpiry);
    assert(count60==1 && count30==1 && fade60>0.0f && fade60<0.05f);
    assert(fabsf(fade60-fade30)<0.001f);
    assert(countAtExpiry==0 && expired<GX_WATER_RIPPLE_AMP_THRESHOLD);
    puts("WATER_RIPPLE_LIFECYCLE: PASS spawn/stop/explicit/resume, smooth rate-independent fade");
}
'''
    compile_run(out, 'lifecycle', harness)


#ai: the checkbox statement is extracted verbatim and exercised with native ImGui input.
def widget(out):
    source = (audit.ROOT / 'src/tools/gfx/water_panel.cpp').read_text()
    statement = re.search(r'gX_I1 automaticRipples =.*?gfx->water.automaticRipples = automaticRipples;',
                          source, re.S).group()
    harness = r'''
//ai: native checkbox input preserves unrelated geometry-wave controls.
#include "imgui.h"
#include <cassert>
typedef bool gX_I1;
struct Gfx {struct {bool automaticRipples=true;int waveCount=3;} water;} Data;
Gfx *gfx=&Data;
ImVec2 Point;
void frame() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(500,200));
    ImGui::Begin("Water widget");
'''+statement+r'''
    Point=ImGui::GetItemRectMin();Point.x+=8;Point.y+=8;
    ImGui::End();ImGui::Render();
}
int main() {
    ImGui::CreateContext();ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.0f/60;
    unsigned char *pixels;int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    frame();frame();
    for(int i=0;i<2;i++) {
        io.AddMousePosEvent(Point.x,Point.y);frame();
        io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(0,false);frame();
        assert(Data.water.automaticRipples==(i==1));
        assert(Data.water.waveCount==3);
    }
    ImGui::DestroyContext();
}
'''
    compile_run(out, 'widget', harness, cpp=True)


#ai: loading disabled then legacy presets in one renderer prevents inherited false defaults.
def persistence(out, binary):
    from gfx_water_authoring_check import fixture
    legacy = fixture()
    legacy['water'].pop('automaticRipples', None)
    off = copy.deepcopy(legacy)
    off['water']['automaticRipples'] = False
    on = copy.deepcopy(off)
    on['water']['automaticRipples'] = True
    absent = copy.deepcopy(legacy)
    absent.pop('water')
    jobs = [('off', off, False), ('legacy', legacy, True), ('on', on, True),
            ('off_again', off, False), ('absent_water', absent, True)]
    manifest = out / 'batch.tsv'
    entries = []
    for name, preset, _ in jobs:
        source = out / (name + '.json')
        source.write_text(json.dumps(preset))
        entries.append(str(source) + '\t' + str(out / (name + '_saved.json')))
    manifest.write_text('\n'.join(entries) + '\n')
    env = audit.scene_env({'GX_GFX_PRESET_BATCH': str(manifest)}, clean=True)
    result = audit.run_scene('--gfx', env, binary=binary, frames=3, fps=None,
                             extra_args=('--gfx-editor',), check=True)
    (out / 'persistence.log').write_text(audit.strip_ansi(result.stdout or ''))
    for name, _, expected in jobs:
        saved = json.loads((out / (name + '_saved.json')).read_text())
        if saved['water']['automaticRipples'] is not expected:
            raise RuntimeError('Periodic ripple persistence mismatch: ' + name)


def checks(binary=None, out=None):
    if out is None:
        out = Path(tempfile.mkdtemp(prefix='water_ripple_', dir='/tmp/opencode'))
    out.mkdir(parents=True, exist_ok=True)
    lifecycle(out)
    widget(out)
    persistence(out, binary or audit.BIN)
    print('WATER_RIPPLE_CHECK: PASS native toggle, lifecycle and same-process legacy defaults')


if __name__ == '__main__':
    with audit.settings_preserved():
        checks()
