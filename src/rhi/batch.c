/**********************************************************************************************
*
*   [gX: src/rhi/batch.c]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: shared immediate-mode vertex batch — replaces rlBegin/rlEnd + rlSetTexture.
//   Supports QUADS (→ indexed triangles) and LINES (→ direct arrays).
//   Has its own internal shader (compiled at init) — no rlgl dependency.
//   Vertex format: pos3 + uv2 + color4 + normal3 = 36 bytes.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "rhi_buf.h"
#include "rhi_state.h"
#include "rhi_tex.h"
#include "rhi_matrix.h"
#include "batch.h"
#include "../gxmath/matrix.h"
#include "rhi.h"

#define MAX_VERTS 8192

//opencode: batch internal shader — standard raylib-compatible uniforms
//   Version header is prepended at compile time (ES vs desktop).
//   Attribute names MUST match the RHI bind table in gl_draw.c
//   (0=vertexPosition 1=vertexTexCoord 2=vertexNormal 3=vertexColor):
//   single-letter names relied on driver-assigned locations, which mismatched
//   on strict drivers (zink bound color wrong → R/G dropped in batch draws
//   like the LDR seed copy and debug views).
static const char *s_vs =
    "layout(location=0)in vec3 vertexPosition;"
    "layout(location=1)in vec2 vertexTexCoord;"
    "layout(location=2)in vec3 vertexNormal;"
    "layout(location=3)in vec4 vertexColor;"
    "out vec2 fu;out vec4 fc;"
    "uniform mat4 mvp;"
    "void main(){fu=vertexTexCoord;fc=vertexColor;gl_Position=mvp*vec4(vertexPosition,1);}";
static const char *s_vs130 =
    "in vec3 vertexPosition;"
    "in vec2 vertexTexCoord;"
    "in vec3 vertexNormal;"
    "in vec4 vertexColor;"
    "out vec2 fu;out vec4 fc;"
    "uniform mat4 mvp;"
    "void main(){fu=vertexTexCoord;fc=vertexColor;gl_Position=mvp*vec4(vertexPosition,1);}"; //opencode: no-layout
    // variant for every desktop legacy rung (130/140/150) — `in`/`out`
    // without locations is valid on all three.
static const char *s_fs =
    "in vec2 fu;in vec4 fc;out vec4 o;"
    "uniform sampler2D texture0;uniform vec4 colDiffuse;"
    "void main(){o=texture(texture0,fu)*colDiffuse*fc;}";

typedef struct { gX_F32 x, y, z; gX_F32 u, v; gX_U8 r, g, b, a; gX_F32 nx, ny, nz; } Vertex;

static Vertex s_verts[MAX_VERTS];
static gX_I32 s_count = 0;
static gX_BatchMode s_mode = GX_BATCH_QUADS;
static gX_U32 s_vao = 0, s_vbo = 0, s_ibo = 0;
static gX_U32 s_defaultTex = 0;
static gX_U32 s_currentTex = 0;
static gX_I1 s_active = 0;
static gX_U32 s_shader = 0;
static gX_I32 s_mvpLoc = -1, s_colLoc = -1, s_texLoc = -1;

static gX_F32 s_cu = 0, s_cv = 0;
static gX_U8 s_cr = 255, s_cg = 255, s_cb = 255, s_ca = 255;
static gX_F32 s_cnx = 0, s_cny = 0, s_cnz = 1;

gX_I0 gX_Batch_Init(gX_I0) {
    if (s_vao) return;
    //opencode: clear any accumulated GL errors from rlglInit
    { gX_U32 e; gX_I32 ec = 0; while ((e = gX_RHI_GetError()) != 0) ec++;
      if (ec) fprintf(stderr, "BATCH: cleared %d pre-existing GL errors before init\n", ec); }
    //opencode: compile batch's own shader (no rlgl dependency)
    gX_I8 err[1024];
    gX_I8 vsBuf[1024], fsBuf[1024];
    snprintf(vsBuf, sizeof(vsBuf), "%s%s", gX_RHI_Shader_VersionHeader(0),
        gX_RHI_Shader_LegacyGLSL() ? s_vs130 : s_vs);
    snprintf(fsBuf, sizeof(fsBuf), "%s%s", gX_RHI_Shader_VersionHeader(1), s_fs);
    s_shader = gX_RHI_Shader_Compile(vsBuf, NULL, fsBuf, "batch", err, sizeof(err));
    if (!s_shader) {
        fprintf(stderr, "BATCH: shader compile failed: %s\n", err);
    } else {
        s_mvpLoc = gX_RHI_GetUniformLocation(s_shader, "mvp");
        s_colLoc = gX_RHI_GetUniformLocation(s_shader, "colDiffuse");
        s_texLoc = gX_RHI_GetUniformLocation(s_shader, "texture0");
    }
    //opencode: 1x1 white default texture via the immutable-storage RHI path
    //   (glTexImage2D mutable storage produces zero-pixel data on NVIDIA).
    { gX_U8 w[] = {255,255,255,255};
      s_defaultTex = gX_RHI_Tex_Create2D_Immutable(1, 1, gX_RHI_RGBA8,
          gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, w, "batch_default"); }
    gX_RHI_Tex_Param(s_defaultTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
    gX_RHI_Tex_Param(s_defaultTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
    s_vao = gX_RHI_VAO_Create("batch_vao");
    s_vbo = gX_RHI_Buffer_Create("batch_vbo");
    s_ibo = gX_RHI_Buffer_Create("batch_ibo");
    gX_RHI_VAO_Bind(s_vao);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, s_vbo);
    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, sizeof(s_verts), NULL, gX_RHI_DYNAMIC_DRAW);
    gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, gX_RHI_FALSE, sizeof(Vertex), (void*)0); gX_RHI_Attrib_Enable(0);
    gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, gX_RHI_FALSE, sizeof(Vertex), (void*)12); gX_RHI_Attrib_Enable(1);
    gX_RHI_Attrib_Pointer(2, 3, gX_RHI_FLOAT, gX_RHI_FALSE, sizeof(Vertex), (void*)24); gX_RHI_Attrib_Enable(2);
    gX_RHI_Attrib_Pointer(3, 4, gX_RHI_UNSIGNED_BYTE, gX_RHI_TRUE, sizeof(Vertex), (void*)20); gX_RHI_Attrib_Enable(3);
    gX_RHI_Buffer_Bind(gX_RHI_ELEMENT_ARRAY_BUFFER, s_ibo);
    { gX_U16 idx[MAX_VERTS/4*6];
      for (gX_I32 i=0; i<MAX_VERTS/4; i++) { gX_U16 b=(gX_U16)(i*4); idx[i*6+0]=b; idx[i*6+1]=(gX_U16)(b+1); idx[i*6+2]=(gX_U16)(b+2); idx[i*6+3]=b; idx[i*6+4]=(gX_U16)(b+2); idx[i*6+5]=(gX_U16)(b+3); }
      gX_RHI_Buffer_Data(gX_RHI_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, gX_RHI_STATIC_DRAW); }
    gX_RHI_VAO_Bind(0);
    s_currentTex = 0;
}

gX_I0 gX_Batch_Shutdown(gX_I0) {
    if (s_vao) { gX_RHI_VAO_Delete(s_vao); s_vao = 0; }
    if (s_vbo) { gX_RHI_Buffer_Delete(s_vbo); s_vbo = 0; }
    if (s_ibo) { gX_RHI_Buffer_Delete(s_ibo); s_ibo = 0; }
    if (s_defaultTex) { gX_RHI_Tex_Delete(s_defaultTex); s_defaultTex = 0; }
    if (s_shader) { gX_RHI_Shader_Delete(s_shader); s_shader = 0; }
}

gX_I0 gX_Batch_Begin(gX_BatchMode mode) {
    s_mode = mode;
    s_count = 0;
    s_active = 1;
    s_cu = 0; s_cv = 0;
}

gX_I0 gX_Batch_End(gX_I0) {
    gX_Batch_Flush();
    s_active = 0;
    s_currentTex = 0;
}

gX_U32 gX_Batch_GetShader(gX_I0) { return s_shader; }
gX_U32 gX_Batch_GetDefaultTex(gX_I0) { return s_defaultTex; }

gX_I0 gX_Batch_Flush(gX_I0) {
    if (s_count == 0 || !s_active) return;
    //opencode: no rlgl dependency — batch has its own shader compiled at init.
    //   Always use the batch's shader for drawing, then restore the caller's.
    //   DrawScreenQuad in drawing.c bypasses the batch (separate direct draw)
    //   so custom shader passes are never affected.
    gX_I32 prevProg = 0;
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &prevProg);
    gX_U32 prog = s_shader;
    if (!prog) { static gX_I1 warned = 0; if (!warned) { warned = 1; fprintf(stderr, "BATCH: no internal shader — discarding %d vertices\n", s_count); } s_count = 0; return; }
    gX_RHI_Shader_Use(prog);
    if (s_mvpLoc >= 0) {
        gX_Matrix proj = gX_RHI_Matrix_GetProjection();
        gX_Matrix view = gX_RHI_Matrix_GetModelview();
        //opencode: gxmath is row-vector convention (p' = p·M) — the correct
        //   composition is view·proj (world → view → proj). The old proj·view
        //   order worked for 2D (view = identity) but rendered every 3D batch
        //   draw (cubes, rain lines) with a transposed matrix — trees/houses
        //   were invisible in the world scene.
        gX_Matrix mvp = gX_Matrix_Multiply(view, proj);
        gX_RHI_UniformMatrix(s_mvpLoc, 1, 1, &mvp.m0);
    }
    if (s_colLoc >= 0) gX_RHI_UniformVec4(s_colLoc, 1, (gX_F32[4]){1, 1, 1, 1});
    if (s_texLoc >= 0) gX_RHI_UniformInt(s_texLoc, 0);
    gX_RHI_ActiveTexture(gX_RHI_TEXTURE0);
    {
        gX_U32 tex = s_currentTex ? s_currentTex : s_defaultTex;
        gX_RHI_Tex_Bind(tex, gX_RHI_TEXTURE_2D);
        //opencode: do NOT re-set MIN/MAG filter here — textures configure their
        //   own filters at creation, and re-setting them per-flush on immutable
        //   textures generates GL_INVALID_OPERATION ("Texture is immutable") on
        //   this NVIDIA driver every frame.
    }
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, s_vbo);
    gX_RHI_Buffer_SubData(gX_RHI_ARRAY_BUFFER, 0, (gX_U32)(s_count * (gX_I32)sizeof(Vertex)), s_verts);
    gX_RHI_VAO_Bind(s_vao);
    if (s_mode == GX_BATCH_QUADS) {
        gX_RHI_DrawElements(gX_RHI_TRIANGLES, (gX_U32)(s_count / 4) * 6, gX_RHI_UNSIGNED_SHORT, 0);
    } else if (s_mode == GX_BATCH_TRIANGLES) {
        gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, (gX_U32)s_count);
    } else {
        gX_RHI_DrawArrays(gX_RHI_LINES, 0, (gX_U32)s_count);
    }
    gX_RHI_VAO_Bind(0);
    if (prevProg) gX_RHI_Shader_Use((gX_U32)prevProg);
    //opencode: the flush consumed s_verts[0..s_count) — reset the counter.
    //   Without this, a mid-batch flush (texture switch or s_count >= MAX_VERTS)
    //   left s_count >= MAX_VERTS and the next gX_Batch_Vertex3f wrote
    //   s_verts[MAX_VERTS] out of bounds, corrupting adjacent statics (the
    //   rhi_matrix.c matrix stack) → random SIGSEGVs when drawing huge text.
    s_count = 0;
}

gX_I0 gX_Batch_Vertex3f(gX_F32 x, gX_F32 y, gX_F32 z) {
    if (s_count >= MAX_VERTS) gX_Batch_Flush();
    Vertex *v = &s_verts[s_count++];
    v->x = x; v->y = y; v->z = z;
    v->u = s_cu; v->v = s_cv;
    v->r = s_cr; v->g = s_cg; v->b = s_cb; v->a = s_ca;
    v->nx = s_cnx; v->ny = s_cny; v->nz = s_cnz;
}

gX_I0 gX_Batch_Vertex2f(gX_F32 x, gX_F32 y) {
    return gX_Batch_Vertex3f(x, y, 0.0f);
}

gX_I0 gX_Batch_TexCoord2f(gX_F32 u, gX_F32 v) {
    s_cu = u; s_cv = v;
}

gX_I0 gX_Batch_Color4ub(gX_U8 r, gX_U8 g, gX_U8 b, gX_U8 a) {
    s_cr = r; s_cg = g; s_cb = b; s_ca = a;
}

gX_I0 gX_Batch_Normal3f(gX_F32 x, gX_F32 y, gX_F32 z) {
    s_cnx = x; s_cny = y; s_cnz = z;
}

gX_I0 gX_Batch_SetTexture(gX_U32 texId) {
    if (texId != s_currentTex && s_count > 0 && s_active) {
        gX_Batch_Flush();
    }
    s_currentTex = texId;
}
