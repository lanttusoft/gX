/**********************************************************************************************
*
*   [gX: src/renderer/postprocess/routing_probe.h]
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

//ai: Debug-only opt-in asymmetric GPU routing witness; exports only to an explicit scratch path.
#ifndef GX_POSTPROCESS_ROUTING_PROBE_H
#define GX_POSTPROCESS_ROUTING_PROBE_H

#ifndef GX_LEAN_RELEASE

#include "../../rhi/rhi_state.h"

//ai: actual RHI viewport/FBO witness supplements spatial evidence without entering the shipping image.
static gX_I0 gX_RendererPostProcess_Probe_Target(const gX_I8 *Stage, gX_RenderTexture Target)
{
    if (!getenv("GX_PP_ROUTING_CHECK")) return;
    gX_I32 Viewport[4] = {0};
    gX_I32 Fbo = 0;
    gX_RHI_GetIntegerv(gX_RHI_VIEWPORT, Viewport);
    gX_RHI_GetIntegerv(gX_RHI_DRAW_FRAMEBUFFER_BINDING, &Fbo);
    gX_TraceLog(gX_LOG_NOTICE,
        "PP_ROUTING_TARGET: stage=%s expected=%u actual=%d size=%dx%d viewport=%d,%d,%d,%d",
        Stage, Target.id, Fbo, Target.texture.width, Target.texture.height,
        Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
}

static gX_I0 gX_RendererPostProcess_Probe_Export(gX_Texture Texture,
    const gX_I8 *Directory, const gX_I8 *Name)
{
    gX_I8 Path[512];
    snprintf(Path, sizeof(Path), "%s/%s.png", Directory, Name);
    gX_Image Image = gX_Image_LoadFromTexture(Texture);
    gX_I1 Saved = Image.data && gX_Image_Export(Image, Path);
    gX_Image_Unload(Image);
    gX_TraceLog(Saved ? gX_LOG_NOTICE : gX_LOG_ERROR,
        "PP_ROUTING: %s export=%d", Name, Saved);
}

//ai: execute the production DrawTo on owned inputs/targets, retaining/restoring global fixtures.
static gX_I0 gX_RendererPostProcess_Probe_Routing(gX_I0)
{
    static gX_I1 Done = 0;
    const gX_I8 *Directory = getenv("GX_PP_ROUTING_CHECK");
    if (Done || !Directory || !Directory[0]) return;
    Done = 1;
    gX_U16 SavedWidth = maindata.renderWidth;
    gX_U16 SavedHeight = maindata.renderHeight;
    gX_RenderTexture SavedCanvas = renderer.canvas;
    gX_RenderTexture SavedFeedback = renderer.feedback;
    for (gX_U32 Case = 0; Case < 16; Case++)
    {
        gX_U32 Width = Case == 4 || Case == 15 ? 319 : 320;
        gX_U32 Height = Case == 4 || Case == 15 ? 239 : 240;
        maindata.renderWidth = (gX_U16)Width;
        maindata.renderHeight = (gX_U16)Height;
        gX_Image Pattern = gX_Image_GenColor(Width, Height, (gX_Color){220, 40, 60, 255});
        gX_Image_DrawRectangle(&Pattern, Width/2, 0, Width-Width/2, Height/2,
            (gX_Color){30, 210, 70, 255});
        gX_Image_DrawRectangle(&Pattern, 0, Height/2, Width/2, Height-Height/2,
            (gX_Color){40, 65, 225, 255});
        gX_Image_DrawRectangle(&Pattern, Width/2, Height/2, Width-Width/2, Height-Height/2,
            (gX_Color){235, 195, 25, 255});
        gX_Image_DrawRectangle(&Pattern, Width/8, Height/8, Width/10, Height/12, gX_WHITE);
        gX_Texture FileTexture = gX_Texture_LoadFromData(Width, Height, Pattern.data);
        gX_Image_Unload(Pattern);
        renderer.canvas = gX_RenderTexture_Load(Width, Height);
        renderer.feedback = gX_RenderTexture_Load(Width, Height);
        gX_Drawing_BeginTexture(renderer.feedback);
        gX_Drawing_ClearBG(gX_BLACK);
        if (Case == 6) gX_Texture_Draw(FileTexture, (gX_Texture_Draw_Params){
            .dst = {0, 0, (gX_F32)Width, (gX_F32)Height}, .tint = gX_WHITE, .scale = {1, 1}});
        gX_Drawing_EndTexture();
        gX_Drawing_BeginTexture(renderer.canvas);
        gX_Drawing_ClearBG(gX_BLACK);
        gX_Texture_Draw(FileTexture, (gX_Texture_Draw_Params){
            .dst = {0, 0, (gX_F32)Width, (gX_F32)Height}, .tint = gX_WHITE, .scale = {1, 1}});
        gX_Drawing_EndTexture();
        gX_RendererPostProcess Probe = {0};
        gX_RendererPostProcess_Load(&Probe);
        gX_RendererPostProcess_Update(&Probe);
        if (Case != 0)
        {
            Probe.state.feedback = Case == 6;
            Probe.state.inject = 0;
            Probe.state.injectCanvas = Case != 1 && Case != 6 && Case != 8 && Case != 9;
        }
        Probe.texture.renderScale = Case < 2 || Case == 14 ? 1.0f : 0.5f;
        Probe.texture.renderScaleChanged = 1;
        if (Case == 3 || Case == 7 || Case == 14 || Case == 15)
        {
            gX_RendererPostProcess_Shader_Push(&Probe, gX_PP_BLUR_GAUSSIAN);
            Probe.shaders[0].loops = Case == 14 ? 1 : 4;
        }
        if (Case == 4 || Case == 5 || Case == 9)
        {
            gX_RendererPostProcess_Shader_Push(&Probe, gX_PP_COLOR_INVERT);
            if (Case == 9) Probe.shaders[0].loops = 0;
            if (Case == 5)
            {
                gX_RendererPostProcess_Shader_Push(&Probe, gX_PP_BLUR_GAUSSIAN);
                Probe.shaders[1].loops = 0;
            }
        }
        if (Case == 10)
        {
            Probe.state.inject = 1;
            Probe.state.injectCanvas = 0;
            Probe.inject.useCustomTexture = 1;
            Probe.inject.texture = &FileTexture;
            Probe.inject.connectScale = 1;
        }
        if (Case == 11)
        {
            gX_RendererPostProcess_Shader_Push(&Probe, gX_PP_COLOR_INVERT);
            gX_RendererPostProcess_Shader_Push(&Probe, gX_PP_COLOR_INVERT);
        }
        if (Case == 12) Probe.state.injectCanvasTint = (gX_Color){128, 255, 255, 255};
        gX_RendererPostProcess_Update(&Probe);
        gX_RenderTexture Output = Case == 7 || Case == 8 || Case == 13
            ? renderer.canvas : gX_RenderTexture_Load(480, 272);
        gX_I8 Reference[64], Result[64];
        snprintf(Reference, sizeof(Reference), "case%u_input", Case);
        snprintf(Result, sizeof(Result), "case%u_output", Case);
        gX_RendererPostProcess_Probe_Export(renderer.canvas.texture, Directory, Reference);
        gX_U32 StartId = Probe.texture.start.texture.id;
        if (Case == 13) Probe.texture.start.texture.id = 0;
        gX_I32 BeforeDepth = gX_GL_State_GetDepth();
        gX_RendererPostProcess_DrawTo(&Probe, &Output);
        gX_TraceLog(gX_LOG_NOTICE, "PP_ROUTING_SCOPE: case=%u before=%d after=%d",
            Case, BeforeDepth, gX_GL_State_GetDepth());
        Probe.texture.start.texture.id = StartId;
        gX_RendererPostProcess_Probe_Export(Output.texture, Directory, Result);
        Probe.texture.texture.id = 0;
        gX_RendererPostProcess_Unload(&Probe);
        if (Case != 7 && Case != 8 && Case != 13) gX_RenderTexture_Unload(Output);
        gX_RenderTexture_Unload(renderer.canvas);
        gX_RenderTexture_Unload(renderer.feedback);
        gX_Texture_Unload(FileTexture);
    }
    renderer.canvas = SavedCanvas;
    renderer.feedback = SavedFeedback;
    maindata.renderWidth = SavedWidth;
    maindata.renderHeight = SavedHeight;
}

#endif
#endif
