# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compile actual GFX texture-update snippets against owned deferred/initial sampler cases.
import subprocess
import tempfile
import unittest
from pathlib import Path


class TextureFilterLifecycle(unittest.TestCase):
    def test_deferred_and_initial_filter(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/renderer/gfx/texture.c').read_text()
        start = source.index('gX_I0 gX_RendererGFX_Texture_Update(')
        end = source.index('gX_I0 gX_RendererGFX_RenderTexture_Update(', start)
        update = (root / 'src/renderer/gfx/update.c').read_text()
        policy_start = update.index('            //ai: newly loaded ordinary heap textures')
        policy_end = update.index('            gX_RendererGFX_Texture_Update(tex);', policy_start)
        prelude = r'''
#include <assert.h>
typedef void gX_I0;
#define false 0
#define true 1
typedef struct {int id;} Texture;
typedef struct {int loadAsap,loaded;} State;
typedef struct {State loadingState;int filterChanged,wrapChanged,filter,wrap,isSkybox;Texture texture;} Wrapper;
typedef struct {Wrapper wrapper;} gX_RendererGFX_Texture;
typedef struct {struct {int textureFilter;} state;} Gfx;
static struct {int backendLoaded;} maindata={1};
static int filters,wraps,lastFilter,lastWrap;
static void gX_RendererGFX_Texture_Load(gX_RendererGFX_Texture *t) {t->wrapper.loadingState.loaded=1;t->wrapper.texture.id=1;}
static void gX_Texture_SetFilter(Texture t,int f) {filters++;lastFilter=f;}
static void gX_Texture_SetWrap(Texture t,int w) {wraps++;lastWrap=w;}
'''
        tests = r'''
 gX_RendererGFX_Texture_Update(tex);
}
int main(void) {
 Gfx gfx={{1}};gX_RendererGFX_Texture t={0};
 t.wrapper.loadingState.loaded=1;t.wrapper.texture.id=1;t.wrapper.filterChanged=1;t.wrapper.filter=2;
 t.wrapper.wrapChanged=1;t.wrapper.wrap=3;
 gX_RendererGFX_Texture_Update(&t);
 assert(filters==1 && wraps==1 && lastFilter==2 && lastWrap==3);
 assert(!t.wrapper.filterChanged && !t.wrapper.wrapChanged);
 gX_RendererGFX_Texture_Update(&t);assert(filters==1 && wraps==1);
 t=(gX_RendererGFX_Texture){0};t.wrapper.loadingState.loadAsap=1;
 heapUpdate(&gfx,&t);assert(filters==2 && lastFilter==1 && !t.wrapper.filterChanged);
 heapUpdate(&gfx,&t);assert(filters==2);
 gfx.state.textureFilter=0;heapUpdate(&gfx,&t);assert(filters==3 && lastFilter==0);
 t.wrapper.isSkybox=1;gfx.state.textureFilter=2;heapUpdate(&gfx,&t);assert(filters==3);
 t=(gX_RendererGFX_Texture){0};t.wrapper.filterChanged=1;
 gX_RendererGFX_Texture_Update(&t);assert(filters==3 && t.wrapper.filterChanged);
 t.wrapper.loadingState.loaded=1;t.wrapper.texture.id=0;
 gX_RendererGFX_Texture_Update(&t);assert(filters==3 && t.wrapper.filterChanged);
 return 0;
}
'''
        code = prelude + source[start:end] + '\nstatic void heapUpdate(Gfx *gfx,gX_RendererGFX_Texture *tex) {\n' + update[policy_start:policy_end] + tests
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            path = Path(directory)
            (path / 'test.c').write_text(code)
            subprocess.run(['cc', '-std=c99', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
