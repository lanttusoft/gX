# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compile actual per-primitive alpha routing; full-source material 137 must bypass cache 32.
import subprocess
import tempfile
import unittest
from pathlib import Path


class AlphaFallback(unittest.TestCase):
    def test_uncached_blend_mask_and_opaque_reset(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / 'src/renderer/gfx/model.c').read_text()
        start = text.index('            //ai: alpha-test state resets per primitive')
        end = text.index('            gX_F32 Opacity =', start)
        prelude = r'''
#include <assert.h>
typedef struct { float blendOpacity; } Material;
typedef struct { int sourceMaterialCount,materialCount; int *alphaModes; float *alphaCutoffs,*blendOpacities; Material materials[32]; } Data;
typedef struct { int enableAlphaTest; float alphaCutoff; } Params;
typedef struct { Params params; Data *glbData; } Model;
typedef struct { int materialCount; } Native;
typedef struct { struct {int enableAlphaTestLoc,alphaCutoffLoc;} locations; } Shader;
static int uploadedTest;
static float uploadedCutoff;
static void gX_Shader_SetValue_Int(int p,int l,int v) {uploadedTest=v;}
static void gX_Shader_SetValue_Float(int p,int l,float v) {uploadedCutoff=v;}
static int route(Model *model, unsigned matIdx, int depthMode) {
 Native m={177}; Shader storage={{0,1}},*rshader=&storage; int shaderProg=0;
 int BlendedMaterials=!depthMode,needBlend=0;float bFac=1;
'''
        tests = r'''
 return needBlend;
}
int main(void) {
 int modes[176]={0};float cutoffs[176]={0},opacity[176];
 for(int i=0;i<176;i++)opacity[i]=1;
 Data data={.sourceMaterialCount=176,.materialCount=32,.alphaModes=modes,.alphaCutoffs=cutoffs,.blendOpacities=opacity};
 Model model={{0,.5},&data};
 modes[137]=2;opacity[137]=.65;
 assert(route(&model,138,0)==1 && uploadedTest==0);
 modes[117]=1;cutoffs[117]=.25;
 assert(route(&model,118,0)==0 && uploadedTest==1 && uploadedCutoff==.25f);
 assert(route(&model,119,0)==0 && uploadedTest==0);
 assert(route(&model,138,1)==0);
 assert(route(&model,118,1)==0 && uploadedTest==1);
 model.params.enableAlphaTest=1;model.params.alphaCutoff=.75;
 route(&model,118,0);assert(uploadedTest==1 && uploadedCutoff==.75f);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            path = Path(directory)
            (path / 'test.c').write_text('#include "' + str(root / 'src/renderer/gfx/material_contract.h') + '"\n' + prelude + text[start:end] + tests)
            subprocess.run(['cc', '-std=c99', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
