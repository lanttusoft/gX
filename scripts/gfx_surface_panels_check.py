#!/usr/bin/env python3
#ai: exercise the actual extracted glass leaf panel with native ImGui input; no preset/resource writes.
import argparse
from pathlib import Path
import tempfile

import audit_common as audit
from water_ripple_check import compile_run


#ai: model-local widgets must preserve other objects and respect the existing PBR gate.
def glass(out):
    source = (audit.ROOT / 'src/tools/gfx/glass_panel.cpp').read_text()
    start = source.index('void gfx_editor_glass_panel(')
    panel = source[start:source.rindex('#endif')]
    records = [('blending', 'model->params.enableBlending = blending ? 1 : 0;'),
               ('refraction', 'model->params.enableRefraction = refraction;'),
               ('use normal map', 'model->params.hasNormalMap = normal ? 1 : 0;'),
               ('roughness', 'ImGui::DragFloat("roughness", &model->params.roughness, 0.01f, 0.0f, 1.0f);'),
               ('refractive index', 'ImGui::DragFloat("refractive index", &model->params.refractiveIndex, 0.01f, 1.0f, 2.5f);')]
    for index, (label, statement) in enumerate(records):
        if panel.count(statement) != 1:
            raise RuntimeError('Glass widget test seam changed: ' + label)
        panel = panel.replace(statement, statement + f'\n    Ids[{index}]=ImGui::GetID("{label}");'
                              f' Rects[{index}]=ImGui::GetItemRectMin();', 1)
    harness = r'''
//ai: real panel statements, real mouse input, deliberately small authored-field fixture.
#include "imgui.h"
#include <cassert>
#include <cstring>
typedef void gX_I0;
typedef bool gX_I1;
typedef char gX_I8;
typedef int gX_I32;
struct gX_Color {unsigned char r,g,b,a;};
struct gX_RendererGFX_Model {
    struct Params {
        bool enableBlending=false,enableRefraction=false,usePBR=false,hasNormalMap=false;
        float blendOpacity=0.4f,refractiveIndex=1.5f,roughness=0.2f,normalMapFactor=0.5f;
        float normalMapScale[2]={1,1},normalMapOffset[2]={0,0};
        gX_Color albedoTint={180,220,240,255};
    } params;
} Data,Other;
ImGuiID Ids[5];ImVec2 Rects[5];gX_I32 Quality=1;
bool color(const char *label,gX_Color*) {ImGui::TextUnformatted(label);return false;}
'''+panel+r'''
void frame() {
    ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(700,700));ImGui::Begin("Glass fixture");
    gfx_editor_glass_panel(&Data,Quality,color);
    ImGui::End();ImGui::Render();
}
void click(int index) {
    ImGuiIO &io=ImGui::GetIO();io.AddMousePosEvent(Rects[index].x+8,Rects[index].y+8);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
}
void drag(int index) {
    ImGuiIO &io=ImGui::GetIO();float x=Rects[index].x+40,y=Rects[index].y+8;
    io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(x+60,y);frame();frame();io.AddMouseButtonEvent(0,false);frame();
}
int main() {
    ImGui::CreateContext();ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize=ImVec2(800,800);io.DeltaTime=1.0f/60;
    unsigned char *pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    frame();frame();
    for(int a=0;a<5;++a)for(int b=a+1;b<5;++b)assert(Ids[a]!=Ids[b]);
    click(0);assert(Data.params.enableBlending);
    click(1);assert(Data.params.enableRefraction);
    click(2);assert(Data.params.hasNormalMap);
    Quality=0;frame();click(1);drag(4);
    assert(Data.params.enableRefraction && Data.params.refractiveIndex==1.5f && Quality==0);
    Quality=1;for(int i=0;i<30;++i)frame();
    drag(4);assert(Data.params.refractiveIndex>1.5f);
    Data.params.refractiveIndex=1.5f;
    drag(3);assert(Data.params.roughness==0.2f);
    Data.params.usePBR=true;for(int i=0;i<30;++i)frame();
    drag(3);assert(Data.params.roughness>0.2f);
    click(0);click(1);click(2);
    assert(!Data.params.enableBlending && !Data.params.enableRefraction && !Data.params.hasNormalMap);
    assert(!Other.params.enableBlending && !Other.params.enableRefraction && !Other.params.hasNormalMap);
    assert(Other.params.roughness==0.2f && Data.params.refractiveIndex==1.5f && Data.params.blendOpacity==0.4f);
    ImGui::DestroyContext();
}
'''
    compile_run(out, 'glass_panel', harness, cpp=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/gfx_surface_panels'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run_', dir=args.out))
    glass(run)
    print('GFX_SURFACE_PANELS: PASS native glass input, IDs, PBR gate and model isolation')
