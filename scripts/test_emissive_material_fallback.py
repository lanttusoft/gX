# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compile the exact mesh-emission resolver against owned cached/uncached native materials.
import subprocess
import tempfile
import unittest
from pathlib import Path


class EmissiveFallback(unittest.TestCase):
    def test_uncached_material_on_first_mesh(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/renderer/gfx/model.c').read_text()
        start = source.index('gX_I0 gX_RendererGFX_Model_MeshEmissive(')
        end = source.index('\n//ai: resolve the file material', start)
        prelude = r'''
#include <assert.h>
#include <string.h>
typedef struct { float x,y,z; } Vec;
typedef struct { Vec emissiveColorFactor; int hasEmissiveStrength; float emissiveStrength; } gX_RendererGFX_GLB_Material;
typedef struct { int sourceMaterialCount,materialCount; gX_RendererGFX_GLB_Material materials[32]; } GLB;
typedef struct { gX_Color color; } Map;
typedef struct { Map *maps; } Mat;
typedef struct { int meshCount,materialCount; int *meshMaterial; Mat *materials; } Model;
typedef struct { int emissiveMeshActive,emissiveFromFile; int materialMeshMask[128]; float effectiveEmissiveGlow[128],emissiveFactor; gX_Color effectiveEmissiveColor[128],emissiveColor; } Params;
typedef struct { Params params; Model model; GLB *glbData; } gX_RendererGFX_Model;
#define gX_GFX_MODEL_MESH_MASK 128
#define gX_GFX_MMASK_EMISSIVE 1
#define gX_MATERIAL_MAP_EMISSION 0
static float factor=1;
static gX_Color color={255,255,255,255};
static void gX_RHI_GetUniformfv(unsigned id,int loc,int count,float *v) { if(count==1)*v=factor;else {v[0]=color.r/255.f;v[1]=color.g/255.f;v[2]=color.b/255.f;v[3]=color.a/255.f;} }
static void gX_Shader_SetValue_Float(gX_Shader s,int l,float v) { factor=v; }
static void gX_Shader_SetValue_Color(gX_Shader s,int l,gX_Color v) { color=v; }
'''
        tests = r'''
int main(void) {
 GLB glb={.sourceMaterialCount=176,.materialCount=32};
 Map maps[177]={0}; Mat materials[177]={0};
 for(int i=0;i<177;i++)materials[i].maps=&maps[i];
 int assignment=95;
 gX_RendererGFX_Model m={0};
 m.glbData=&glb;m.model=(Model){1,177,&assignment,materials};
 m.params.emissiveFromFile=1;m.params.emissiveFactor=1;m.params.emissiveColor=(gX_Color){255,255,255,255};
 gX_RendererGFX_Model_MeshEmissive(&m,(gX_Shader){1},0,1,0);
 assert(factor==0 && color.r==0 && color.g==0 && color.b==0);
 maps[95].color=(gX_Color){0,128,0,255};
 gX_RendererGFX_Model_MeshEmissive(&m,(gX_Shader){1},0,1,0);
 assert(factor==1 && color.g==128 && color.r==0);
 assignment=12;glb.materials[11]=(gX_RendererGFX_GLB_Material){{1,0,0},1,3};
 gX_RendererGFX_Model_MeshEmissive(&m,(gX_Shader){1},0,1,0);
 assert(factor==3 && color.r==255 && color.g==0);
 m.params.materialMeshMask[0]=1;m.params.effectiveEmissiveGlow[0]=2;m.params.effectiveEmissiveColor[0]=(gX_Color){0,0,255,255};
 gX_RendererGFX_Model_MeshEmissive(&m,(gX_Shader){1},0,1,0);
 assert(factor==2 && color.b==255);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            path = Path(directory)
            (path / 'test.c').write_text('#include "' + str(root / 'src/renderer/gfx/material_contract.h') + '"\n' + prelude + source[start:end] + tests)
            subprocess.run(['cc', '-std=c99', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
