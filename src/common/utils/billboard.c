/**********************************************************************************************
*
*   [gX: src/common/utils/billboard.c]
*   //opencode: billboard rendering — builds a unit quad once, reorients via model.transform
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

//opencode: billboard rendering — always faces the camera
#include "billboard.h"

#include "../../rhi/rhi_mesh.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../wrappers/wrappers.h"
#include "../../wrappers/wrappers_gl.h"
#include "../../gxmath/vector3.h"
#include "../../gxmath/vector2.h"
#include "../../gxmath/matrix.h"
#include "../../common/macros.h"
#include "../../common/enum.h"
#include "../../main.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/rhi_enums.h"
#include "../../rhi/rhi_state.h"
#include "../../wrappers/drawing.h"

//opencode: text labels render into a shared RT before the billboard quad is
//   drawn. That nested target render ends with EndTexture, which unbinds to the
//   default framebuffer, so save and restore the caller's target, viewport, and
//   RHI matrices around the text pass. Without it the quad landed on the
//   default framebuffer and the present pass overwrote it (labels never
//   reached scene captures).
typedef struct {
    gX_RHI_FBO fbo;
    gX_I32 vpX, vpY, vpW, vpH;
    gX_Matrix projection;
    gX_Matrix modelview;
    //opencode: raylib mesh draws (the label quad) read rlgl's matrices, which
    //   the text RT sync overwrites — carry them too.
    gX_Matrix glProjection;
    gX_Matrix glModelview;
} gX_TextTargetState;

static gX_TextTargetState text_target_push(gX_I0) {
    gX_TextTargetState state;
    //opencode: the cache can be the -1 sentinel (BeginTexture invalidates it
    //   after binding), so resolve the real binding instead of trusting the
    //   plain getter's 0-for-unknown.
    state.fbo = gX_RHI_GetDrawFBOResolved();
    state.vpX = drawinfo.vpX;
    state.vpY = drawinfo.vpY;
    state.vpW = drawinfo.vpW;
    state.vpH = drawinfo.vpH;
    state.projection = gX_RHI_Matrix_GetProjection();
    state.modelview = gX_RHI_Matrix_GetModelview();
    state.glProjection = gX_GL_Matrix_GetProjection();
    state.glModelview = gX_GL_Matrix_GetModelView();
    return state;
}

static gX_I0 text_target_pop(gX_TextTargetState state) {
    gX_RHI_InvalidateFBO();
    if (state.fbo != 0) gX_RHI_BindFBO(state.fbo);
    else gX_RHI_UnbindFBO();
    drawinfo.vpX = state.vpX;
    drawinfo.vpY = state.vpY;
    drawinfo.vpW = state.vpW;
    drawinfo.vpH = state.vpH;
    gX_RHI_Viewport(state.vpX, state.vpY, state.vpW, state.vpH);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_SetProjection(state.projection);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_SetModelview(state.modelview);
    gX_GL_Matrix_SetProjection(state.glProjection);
    gX_GL_Matrix_SetModelView(state.glModelview);
}

// Module-level cached billboard model (created once, reused every frame)
static gX_Model billboardModel;
static gX_I1 billboardReady = 0;

//opencode: shared render texture for text-to-billboard rendering (512x64, enough for ~30 chars)
#define TEXT_RT_WIDTH 512
#define TEXT_RT_HEIGHT 64
static gX_RenderTexture textRT;
static gX_I1 textRTReady = 0;

//opencode: create a unit vertical quad mesh (XY plane, facing +Z, centered at origin)
static gX_Mesh billboard_create_quad_mesh(gX_I0) {
    gX_Mesh mesh;
    memset(&mesh, 0, sizeof(mesh));

    mesh.vertexCount   = 4;
    mesh.triangleCount = 2;

    // Vertex positions (TL, TR, BR, BL in CCW order)
    gX_F32 *verts = (gX_F32 *)malloc(4 * 3 * sizeof(gX_F32));
    verts[0] = -0.5f;  verts[1] =  0.5f;  verts[2] = 0.0f; // TL
    verts[3] =  0.5f;  verts[4] =  0.5f;  verts[5] = 0.0f; // TR
    verts[6] =  0.5f;  verts[7] = -0.5f;  verts[8] = 0.0f; // BR
    verts[9] = -0.5f;  verts[10]= -0.5f;  verts[11]= 0.0f; // BL
    mesh.vertices = verts;

    // UV coordinates
    gX_F32 *uvs = (gX_F32 *)malloc(4 * 2 * sizeof(gX_F32));
    uvs[0] = 0.0f; uvs[1] = 0.0f; // TL
    uvs[2] = 1.0f; uvs[3] = 0.0f; // TR
    uvs[4] = 1.0f; uvs[5] = 1.0f; // BR
    uvs[6] = 0.0f; uvs[7] = 1.0f; // BL
    mesh.texcoords = uvs;

    // Normals (face +Z)
    gX_F32 *norms = (gX_F32 *)malloc(4 * 3 * sizeof(gX_F32));
    for (gX_I32 i = 0; i < 4; i++) {
        norms[i * 3 + 0] = 0.0f;
        norms[i * 3 + 1] = 0.0f;
        norms[i * 3 + 2] = 1.0f;
    }
    mesh.normals = norms;

    // Vertex colors (white)
    gX_U8 *cols = (gX_U8 *)malloc(4 * 4 * sizeof(gX_U8));
    for (gX_I32 i = 0; i < 4; i++) {
        cols[i * 4 + 0] = 255;
        cols[i * 4 + 1] = 255;
        cols[i * 4 + 2] = 255;
        cols[i * 4 + 3] = 255;
    }
    mesh.colors = cols;

    // Indices (CCW: TL->BR->TR, TL->BL->BR)
    gX_U16 *idxs = (gX_U16 *)malloc(6 * sizeof(gX_U16));
    idxs[0] = 0; idxs[1] = 2; idxs[2] = 1; // TL, BR, TR
    idxs[3] = 0; idxs[4] = 3; idxs[5] = 2; // TL, BL, BR
    mesh.indices = idxs;

    return mesh;
}

//opencode: ensure the cached billboard model is loaded once
static gX_I0 billboard_ensure_init(gX_I0) {
    if (billboardReady) return;
    {
        gX_Mesh mesh = billboard_create_quad_mesh();
        //opencode: gX_Model_LoadMesh does NOT upload (LoadModelFromMesh keeps
        //   vaoId=0) — without this the billboard draw is skipped (vao==0)
        //   and every NPC/player billboard is invisible.
        gX_RHI_Mesh_Upload(&mesh, 0);
        billboardModel = gX_Model_LoadMesh(mesh);
    }
    billboardReady = 1;
}

//opencode: compute Y-axis rotation angle so the quad faces the camera
static gX_F32 billboard_angle_to_camera(gX_F32v3 position, gX_F32v3 cameraPos) {
    gX_F32v3 dir = gX_F32v3_Sub(cameraPos, position);
    dir.y = 0.0f;
    gX_F32 len = gX_F32v3_Length(dir);
    if (len < 0.001f) return 0.0f;
    return atan2f(dir.x, dir.z);
}

gX_I0 gX_Billboard_Draw(gX_F32v3 position, gX_F32v2 size, gX_Texture *texture, gX_F32v3 cameraPos) {
    gX_Billboard_Draw_Tinted(position, size, texture, cameraPos, gX_WHITE);
}

//ai: gxmath Multiply composes right-after-left; translation must be applied last to preserve the anchor.
static gX_Matrix billboard_transform(gX_F32v3 Position, gX_F32v2 Size, gX_F32 Angle)
{
    gX_Matrix Transform = gX_Matrix_Scale(Size.x, Size.y, 1.0f);
    Transform = gX_Matrix_Multiply(Transform, gX_Matrix_RotateY(Angle));
    return gX_Matrix_Multiply(Transform, gX_Matrix_Translate(Position.x, Position.y, Position.z));
}

//ai: raw mesh draws bypass Texture_Draw's RT Y-flip; update only the cached quad's UV rows.
static gX_I0 billboard_uv(gX_I1 InvertY)
{
    static gX_I1 Inverted = false;
    if (Inverted == InvertY || !billboardReady) return;
    gX_Mesh *Mesh = &billboardModel.meshes[0];
    if (!Mesh->texcoords || !Mesh->vboId || !Mesh->vboId[1]) return;
    for (gX_U32 Vertex = 0; Vertex < 4; Vertex++)
        Mesh->texcoords[Vertex * 2 + 1] = 1.0f - Mesh->texcoords[Vertex * 2 + 1];
    gX_I32 PreviousBuffer = 0;
    gX_RHI_GetIntegerv(gX_RHI_ARRAY_BUFFER_BINDING, &PreviousBuffer);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, Mesh->vboId[1]);
    gX_RHI_Buffer_SubData(gX_RHI_ARRAY_BUFFER, 0, 8 * sizeof(gX_F32), Mesh->texcoords);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, (gX_U32)PreviousBuffer);
    Inverted = InvertY;
}

//ai: share the anchor-preserving composition with fixed text; facing changes orientation, not position.
gX_I0 gX_Billboard_Draw_Tinted(gX_F32v3 position, gX_F32v2 size, gX_Texture *texture, gX_F32v3 cameraPos, gX_Color tint) {
    billboard_ensure_init();
    billboard_uv(texture && gX_Texture_IsRT(texture->id));

    // Compute facing rotation
    gX_F32 angle = billboard_angle_to_camera(position, cameraPos);

    gX_Matrix mat = billboard_transform(position, size, angle);

    // Apply transform to model
    billboardModel.transform = mat;

    // Set texture on the material albedo map (or zero-clear if NULL)
    if (billboardModel.materialCount > 0) {
        if (texture != NULL) {
            billboardModel.materials[0].maps[gX_MATERIAL_MAP_ALBEDO].texture = *texture;
        } else {
            gX_Texture zeroTex = {0};
            billboardModel.materials[0].maps[gX_MATERIAL_MAP_ALBEDO].texture = zeroTex;
        }
    }

    // Draw using model with identity position/scale so model.transform is used
    gX_Model_Parameters params = {0};
    params.position = (gX_F32v3){0.0f, 0.0f, 0.0f};
    params.scale    = 1.0f;
    params.color    = tint;
    gX_Model_Draw_Parameters(billboardModel, params);
}

//opencode: lazy-init the shared text render texture
static gX_I0 text_rt_ensure_init(gX_I0) {
    if (textRTReady) return;
    textRT = gX_RenderTexture_Load(TEXT_RT_WIDTH, TEXT_RT_HEIGHT);
    textRTReady = (textRT.id > 0);
}

//opencode: raylib's text batch draws through rlgl's transform, which gX's RHI
//   matrix stack never updates; sync it to the shared text RT so glyphs land
//   in the render texture instead of the stale GUI transform.
static gX_I0 text_rt_sync_transform(gX_I0) {
    gX_GL_Matrix_SetProjection(gX_Matrix_Ortho(0.0f, (gX_F32)TEXT_RT_WIDTH,
        (gX_F32)TEXT_RT_HEIGHT, 0.0f, 0.0f, 1.0f));
    gX_GL_Matrix_SetModelView(gX_Matrix_Identity());
}

//opencode: opt-in label RT dump for the font gate; dumps on a settled frame
//   so the first-frame state cannot capture an empty atlas (GX_LABEL_DUMP=<path>).
static gX_I0 text_rt_dump_once(gX_I0) {
    const gX_I8 *path = getenv("GX_LABEL_DUMP");
    if (!path || !path[0] || path[0] == '0') return;
    if (maindata.frameCount < 30) return;
    static gX_I1 dumped = 0;
    if (dumped) return;
    dumped = 1;
    gX_Image image = gX_Image_LoadFromTexture(textRT.texture);
    if (!image.data) return;
    FILE *file = fopen(path, "wb");
    if (!file) {
        gX_Image_Unload(image);
        return;
    }
    gX_U32 width = (gX_U32)image.width;
    gX_U32 height = (gX_U32)image.height;
    gX_U32 channels = 4;
    fwrite("GXLBL1\0\0", 1, 8, file);
    fwrite(&width, sizeof(width), 1, file);
    fwrite(&height, sizeof(height), 1, file);
    fwrite(&channels, sizeof(channels), 1, file);
    fwrite(image.data, 1, (size_t)width * (size_t)height * 4, file);
    fclose(file);
    gX_Image_Unload(image);
}

//ai: fit measured glyph extents inside the fixed RT; oversized UI font values must not clip letters.
static gX_F32 text_rt_fit_font(const gX_I8 *Text, gX_Font Font, gX_F32 FontSize, gX_F32v2 *Position)
{
    if (!isfinite(FontSize) || FontSize < 1.0f) FontSize = 1.0f;
    gX_F32v2 Extent = gX_RawString_Measure(Text, Font, FontSize, 1.0f);
    for (gX_U32 Pass = 0; Pass < 8; Pass++)
    {
        gX_F32 Ratio = 1.0f;
        if (Extent.x > TEXT_RT_WIDTH - 8.0f) Ratio = (TEXT_RT_WIDTH - 8.0f) / Extent.x;
        if (Extent.y * Ratio > TEXT_RT_HEIGHT - 8.0f) Ratio = (TEXT_RT_HEIGHT - 8.0f) / Extent.y;
        if (Ratio >= 1.0f) break;
        FontSize *= Ratio;
        Extent = gX_RawString_Measure(Text, Font, FontSize, 1.0f);
    }
    *Position = (gX_F32v2){(TEXT_RT_WIDTH - Extent.x) * 0.5f, (TEXT_RT_HEIGHT - Extent.y) * 0.5f};
    return FontSize;
}

//ai: normalize the requested raster font size to the available label texture before drawing.
//opencode: render text string into the shared RT, then draw as camera-facing billboard
gX_I0 gX_Billboard_Text_Draw_Styled(gX_F32v3 position, gX_F32v2 size, const gX_I8 *text, gX_F32 fontSize, gX_Color color, gX_F32v3 cameraPos, gX_Font font, gX_Color shadowColor, gX_F32 shadowPixels) {
    shadowPixels = fminf(4.0f, fmaxf(0.0f, shadowPixels));
    if (!text || text[0] == '\0') return;
    text_rt_ensure_init();
    if (!textRTReady) return;

    if (!gX_Font_IsReady(font)) font = gX_Font_GetDefault();

    // Render text into the shared RT
    gX_TextTargetState outer = text_target_push();
    gX_Drawing_BeginTexture(textRT);
    gX_Drawing_ClearBG((gX_Color){0,0,0,0});
    {
        text_rt_sync_transform();
        gX_F32v2 textPos;
        fontSize = text_rt_fit_font(text, font, fontSize, &textPos);
        if (shadowColor.a && shadowPixels > 0.0f)
            gX_RawString_Draw(text, font, gX_F32v2_Add(textPos, (gX_F32v2){shadowPixels, shadowPixels}),
                (gX_F32v2){0,0}, 0.0f, fontSize, 1.0f, shadowColor);
        gX_RawString_Draw(text, font, textPos, (gX_F32v2){0,0}, 0.0f, fontSize, 1.0f, color);
    }
    gX_Drawing_EndTexture();
    text_target_pop(outer);
    text_rt_dump_once();

    // Draw billboard with the RT's texture
    gX_Billboard_Draw_Tinted(position, size, &textRT.texture, cameraPos, gX_WHITE);
}

//opencode: render text into shared RT, then draw as fixed-orientation 3D quad
//ai: the same composition preserves the authored position independently of fixed rotation and size.
gX_I0 gX_Text3D_Draw_Styled(gX_F32v3 position, gX_F32v2 size, gX_F32v3 rotation, const gX_I8 *text, gX_F32 fontSize, gX_Color color, gX_Font font, gX_Color shadowColor, gX_F32 shadowPixels) {
    shadowPixels = fminf(4.0f, fmaxf(0.0f, shadowPixels));
    if (!text || text[0] == '\0') return;
    text_rt_ensure_init();
    if (!textRTReady) return;

    // Render text into shared RT (same as billboard path)
    gX_TextTargetState outer = text_target_push();
    gX_Drawing_BeginTexture(textRT);
    gX_Drawing_ClearBG((gX_Color){0,0,0,0});
    {
        if (!gX_Font_IsReady(font)) font = gX_Font_GetDefault();
        text_rt_sync_transform();
        gX_F32v2 textPos;
        fontSize = text_rt_fit_font(text, font, fontSize, &textPos);
        if (shadowColor.a && shadowPixels > 0.0f)
            gX_RawString_Draw(text, font, gX_F32v2_Add(textPos, (gX_F32v2){shadowPixels, shadowPixels}),
                (gX_F32v2){0,0}, 0.0f, fontSize, 1.0f, shadowColor);
        gX_RawString_Draw(text, font, textPos, (gX_F32v2){0,0}, 0.0f, fontSize, 1.0f, color);
    }
    gX_Drawing_EndTexture();
    text_target_pop(outer);
    text_rt_dump_once();

    // Build fixed-orientation quad
    billboard_ensure_init();
    billboard_uv(true);

    gX_F32 rad = rotation.y * 0.0174533f; //opencode: deg2rad for Y
    gX_Matrix mat = billboard_transform(position, size, rad);
    billboardModel.transform = mat;

    if (billboardModel.materialCount > 0) {
        billboardModel.materials[0].maps[gX_MATERIAL_MAP_ALBEDO].texture = textRT.texture;
    }

    gX_Model_Parameters params = {0};
    params.position = (gX_F32v3){0,0,0};
    params.scale = 1.0f;
    params.color = gX_WHITE;
    gX_Model_Draw_Parameters(billboardModel, params);
}

//ai: preserve the existing text API with neutral shadow defaults.
gX_I0 gX_Billboard_Text_Draw(gX_F32v3 Position, gX_F32v2 Size, const gX_I8 *Text,
    gX_F32 FontSize, gX_Color Color, gX_F32v3 CameraPos, gX_Font Font)
{
    gX_Billboard_Text_Draw_Styled(Position, Size, Text, FontSize, Color, CameraPos, Font,
        (gX_Color){0,0,0,0}, 0.0f);
}

//ai: preserve the existing fixed-text API with neutral shadow defaults.
gX_I0 gX_Text3D_Draw(gX_F32v3 Position, gX_F32v2 Size, gX_F32v3 Rotation,
    const gX_I8 *Text, gX_F32 FontSize, gX_Color Color, gX_Font Font)
{
    gX_Text3D_Draw_Styled(Position, Size, Rotation, Text, FontSize, Color, Font,
        (gX_Color){0,0,0,0}, 0.0f);
}
