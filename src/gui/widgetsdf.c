/**********************************************************************************************
*
*   [gX: src/gui/widgetsdf.c]
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

//opencode: v0.2.0 — SDF widget surface pass (guisurface.frag + guivert.vert).
//   Elements flagged RECT_SDF_SURFACE skip their flat batch rect and enqueue;
//   Canvas_Draw flushes the queue inside the buffer RT where batch rects and
//   text already render — same FBO, same state, proven context.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "widgetsdf.h"
#include "style.h"
#include "canvas.h"

#include "../wrappers/shader.h"
#include "../wrappers/drawing.h"
#include "../rhi/batch.h"
#include "../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "../../rhi/rhi_matrix.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/renderdoc_hook.h"
#include "../rhi/rhi_gl.h"

#include "../common/log.h"
#include "../common/utils/shader.h"
#include "../../main.h"

//opencode: gX_WHITE + gX_Color_Mul for the hue-multiplied gradient colors
#include "../common/macros.h"
#include "../gxmath/color.h"
#include "../input.h"

static gX_Shader s_widgetShader;
static gX_I1     s_loaded = false;

static gX_I32 s_colLoc = -1;
static gX_I32 s_resLoc = -1;
static gX_I32 s_boxLoc = -1;
static gX_I32 s_uniformsLoc = -1;
static gX_I32 s_gradientTopLoc = -1;
static gX_I32 s_gradientBottomLoc = -1;
static gX_I32 s_gradientCornerALoc = -1;
static gX_I32 s_gradientCornerBLoc = -1;
static gX_I32 s_gradientModeLoc = -1;
static gX_I32 s_surfaceDepthLoc = -1;
static gX_I32 s_metalParamsLoc = -1;
static gX_I32 s_cursorLoc = -1;
static gX_I32 s_cursorColorLoc = -1;

//opencode: hover/pressed glint — the cursor light tightens and brightens on
//   interactive SDF surfaces (specular read). GX_GUI_CURSOR_GLINT=0 forces
//   the plain light (audit comparison).
static gX_I1 s_glintChecked = 0;
static gX_I1 s_glintEnabled = 1;

//opencode: per-element depth override trace (audit hook), cached env.
static gX_I1 s_depthTraceChecked = 0;
static gX_I1 s_depthTraceOn = 0;
static gX_I1 s_depthTraceEnabled(gX_I0) {
    if (!s_depthTraceChecked) {
        s_depthTraceChecked = 1;
        const gX_I8 *hook = getenv("GX_GUI_DEPTH_TRACE");
        s_depthTraceOn = (hook && hook[0] == '1') ? 1 : 0;
    }
    return s_depthTraceOn;
}

static gX_F32 gfx_widgetsdf_glint(const gX_GUI_Element *element,
    gX_F32 strength, gX_F32 *outRadiusScale)
{
    if (outRadiusScale) *outRadiusScale = 1.0f;
    if (!s_glintChecked) {
        s_glintChecked = 1;
        const gX_I8 *hook = getenv("GX_GUI_CURSOR_GLINT");
        if (hook && hook[0] == '0') s_glintEnabled = 0;
    }
    if (!s_glintEnabled || !element || strength <= 0.0f) return 1.0f;
    gX_F32 glint = gui.style.cursorLight.glint;
    if (glint <= 0.0f) return 1.0f;
    if (element->state.PRESSED_LEFT) {
        if (outRadiusScale) *outRadiusScale = 0.40f;
        return 1.0f + glint * 2.2f;
    }
    if (element->state.HOVERED) {
        if (outRadiusScale) *outRadiusScale = 0.55f;
        return 1.0f + glint * 1.2f;
    }
    return 1.0f;
}

typedef struct gX_GUI_WidgetSDF_Item {
    gX_GUI_Element *element;
    gX_Color        fill;
    gX_Color        hue;              //opencode: canvasHue at enqueue time
    gX_F32          sx, sy, sw, sh;   //opencode: screen-space rect (buffer->screen mapped)
    gX_F32          cw, ch;           //opencode: canvas mapping basis (canvasBox
                                      //   dims at enqueue — the batch maps content
                                      //   through these, NOT screen dims)
} gX_GUI_WidgetSDF_Item;

static gX_GUI_WidgetSDF_Item s_queue[512];
static gX_U32                s_queueCount = 0;
static gX_GUI_Canvas        *s_canvasCtx = NULL;

//opencode: resolve the SDF surface style — authored gradient modes
//   (1 vertical, 2 horizontal, 3 four-point), the derived button ramp, and
//   the themed depth profiles (mode 1/2 -> horizon/metallic over the resolved
//   start/end colors). Colors follow the flat path: [0]=start, [1]=end,
//   [2]=BR, [3]=TR, hue-multiplied like every canvas color.
static gX_I0 gX_GUI_WidgetSDF_GetSurfaceStyle(const gX_GUI_Element *element,
    gX_Color hue, gX_Color fill,
    gX_Color *top, gX_Color *bottom, gX_Color *cornerA, gX_Color *cornerB,
    gX_I32 *mode)
{
    *top = fill;
    *bottom = fill;
    *cornerA = fill;
    *cornerB = fill;
    *mode = 0;
    if (!element) return;

    if (element->stateRendering.GRADIENT_ENABLE
        && element->colors.amount > 0 && element->colors.data) {
        gX_GUI_Element_Color *colors = element->colors.data;
        for (gX_U32 i = 0; i < element->colors.amount && i <= 3; i++) {
            if (!colors[i].gradientColor) continue;
            gX_Color color = gX_Color_Mul(colors[i].color, hue);
            if (i == 0)      *top = color;
            else if (i == 1) *bottom = color;
            else if (i == 2) *cornerA = color;
            else             *cornerB = color;
        }
        if (element->stateRendering.GRADIENT_STATE == 1)      *mode = 2;
        else if (element->stateRendering.GRADIENT_STATE >= 2) *mode = 3;
        else                                                  *mode = 1;
    } else if (element->type == gX_GUI_ELEMENT_TYPE_BUTTON) {
        gX_F32 strength = gui.style.gradientStrength;
        if (strength < 0.0f) strength = 0.0f;
        if (strength > 1.0f) strength = 1.0f;
        top->r = (gX_U8)((gX_F32)fill.r * (1.0f - 0.04f * strength));
        top->g = (gX_U8)((gX_F32)fill.g * (1.0f - 0.04f * strength));
        top->b = (gX_U8)((gX_F32)fill.b * (1.0f - 0.04f * strength));
        bottom->r = (gX_U8)((gX_F32)fill.r * (1.0f - 0.60f * strength));
        bottom->g = (gX_U8)((gX_F32)fill.g * (1.0f - 0.60f * strength));
        bottom->b = (gX_U8)((gX_F32)fill.b * (1.0f - 0.60f * strength));
        *mode = (strength > 0.001f) ? 1 : 0;
    }

    //opencode: theme depth profile — applies to authored gradients too so a
    //   horizon/metallic theme restyles every surface consistently. Plain
    //   surfaces derive a usable contrasting pair from their fill.
    gX_I32 depth = gui.style.surfaceDepth.mode;
    if (element->stateRendering.SURFACE_DEPTH != 0) {
        depth = (gX_I32)element->stateRendering.SURFACE_DEPTH - 1;
        if (s_depthTraceEnabled() && maindata.frameCount % 30 == 0)
            gX_TraceLog(gX_LOG_NOTICE,
                "GX_DEPTH_TRACE: elem_depth=%d chosen=%d theme=%d",
                (gX_I32)element->stateRendering.SURFACE_DEPTH, depth,
                gui.style.surfaceDepth.mode);
    }
    if (depth == 1 || depth == 2) {
        //opencode: the authored orientation survives the depth restyle — a
        //   horizontal gradient keeps its metallic band/rim along X.
        gX_I32 authoredMode = *mode;
        if (*mode == 0) {
            gX_F32 lift = (depth == 1) ? 0.22f : 0.10f;
            gX_F32 sink = (depth == 1) ? 0.35f : 0.55f;
            top->r = (gX_U8)((gX_F32)fill.r + (255.0f - (gX_F32)fill.r) * lift);
            top->g = (gX_U8)((gX_F32)fill.g + (255.0f - (gX_F32)fill.g) * lift);
            top->b = (gX_U8)((gX_F32)fill.b + (255.0f - (gX_F32)fill.b) * lift);
            bottom->r = (gX_U8)((gX_F32)fill.r * (1.0f - sink));
            bottom->g = (gX_U8)((gX_F32)fill.g * (1.0f - sink));
            bottom->b = (gX_U8)((gX_F32)fill.b * (1.0f - sink));
        }
        if (depth == 2 && authoredMode == 2) *mode = 6;
        else *mode = (depth == 1) ? 4 : 5;
    }
}

gX_I0 gX_GUI_WidgetSDF_Init(gX_I0)
{
    if (s_loaded) return;
    gX_RenderDoc_Init();
    //opencode: explicit-layout vertex stage pairs with DrawScreenQuad's VAO
    s_widgetShader = gX_Shader_Load_Ex(
        "resources/shader/guivert.vert", "resources/shader/guisurface.frag", 0);
    if (!s_widgetShader.id) {
        gX_TraceLog(gX_LOG_WARNING,
            "WidgetSDF: shader failed to compile — rects fall back to flat");
        return;
    }
    s_colLoc       = gX_Shader_GetLocation(s_widgetShader, "col");
    s_resLoc       = gX_Shader_GetLocation(s_widgetShader, "res");
    s_boxLoc       = gX_Shader_GetLocation(s_widgetShader, "box");
    s_uniformsLoc  = gX_Shader_GetLocation(s_widgetShader, "uniforms");
    s_gradientTopLoc = gX_Shader_GetLocation(s_widgetShader, "gradientTop");
    s_gradientBottomLoc = gX_Shader_GetLocation(s_widgetShader, "gradientBottom");
    s_gradientCornerALoc = gX_Shader_GetLocation(s_widgetShader, "gradientCornerA");
    s_gradientCornerBLoc = gX_Shader_GetLocation(s_widgetShader, "gradientCornerB");
    s_gradientModeLoc = gX_Shader_GetLocation(s_widgetShader, "gradientMode");
    s_surfaceDepthLoc = gX_Shader_GetLocation(s_widgetShader, "surfaceDepth");
    s_metalParamsLoc = gX_Shader_GetLocation(s_widgetShader, "metalParams");
    s_cursorLoc = gX_Shader_GetLocation(s_widgetShader, "cursor");
    s_cursorColorLoc = gX_Shader_GetLocation(s_widgetShader, "cursorColor");
    s_loaded = true;
}

gX_I0 gX_GUI_WidgetSDF_Unload(gX_I0)
{
    if (!s_loaded) return;
    gX_Shader_Unload(s_widgetShader);
    s_loaded = false;
}

//opencode: triple gate — env opt-in (GX_GUI_SDF=1), theme token, program
gX_I1 gX_GUI_WidgetSDF_Ready(gX_I0)
{
    //opencode: GUI heap/unit tests construct elements before the graphics
    //   backend exists. Do not lazily compile the widget shader without a GL
    //   context; the element simply uses the regular rectangle path instead.
    if (!maindata.backendLoaded) return false;
    //opencode: v0.2.0 — SHIP GATE: the theme token decides; the env var is a
    //   force-OFF switch only ("0" disables even when theme enables).
    //   Pre-10 latched on env=1 which made the token meaningless and
    //   GX_GUI_SDF=0 unable to disable after early init.
    {
        const gX_I8 *e = getenv("GX_GUI_SDF");
        if (e && e[0] == '0') return false;
    }
    if (!gui.style.widgetSDF) return false;
    if (!s_loaded) gX_GUI_WidgetSDF_Init();
    return s_loaded;
}

gX_I0 gX_GUI_WidgetSDF_Attach(gX_GUI_Element *element)
{
    int rdy = gX_GUI_WidgetSDF_Ready();
    if (!element) return;
    if (rdy) element->stateRendering.RECT_SDF_SURFACE = 1;
}

//opencode: text-bearing buttons render their rounded surface in the canvas
//   before text, preventing the post-composite SDF pass from covering labels.
gX_I0 gX_GUI_WidgetSDF_DrawNow(gX_GUI_Element *element, gX_GUI_Canvas *canvas, gX_Color fill)
{
    if (!s_loaded || !element || !canvas) return;
    gX_Color gradientTop, gradientBottom, gradientCornerA, gradientCornerB;
    gX_I32 gradientMode = 0;
    gX_GUI_WidgetSDF_GetSurfaceStyle(element, canvas->canvasHue, fill,
        &gradientTop, &gradientBottom, &gradientCornerA, &gradientCornerB,
        &gradientMode);
    gX_F32 resW = canvas->buffer.loadingState.loaded
        ? (gX_F32)canvas->buffer.width : (gX_F32)maindata.windowWidth;
    gX_F32 resH = canvas->buffer.loadingState.loaded
        ? (gX_F32)canvas->buffer.height : (gX_F32)maindata.windowHeight;
    gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
    gX_I1 depthWas = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    gX_I1 scissorWas = gX_RHI_IsEnabled(gX_RHI_SCISSOR_TEST);
    gX_Drawing_BeginShader(s_widgetShader);
        const GLfloat c[4] = { fill.r/255.0f, fill.g/255.0f, fill.b/255.0f, fill.a/255.0f };
        const GLfloat rv[2] = { resW, resH };
        const GLfloat bv[4] = { element->rect.x, element->rect.y,
                                 element->rect.width, element->rect.height };
        GLfloat u[16] = { 0 };
        u[0] = (element->cornerRadius >= 0.0f) ? element->cornerRadius : gui.style.cornerRadius;
        u[1] = gui.style.borderWidth;
        u[4] = 0.92f;
        u[5] = 0.92f;
        u[6] = 0.92f;
        u[7] = 1.0f;
        glUniform4fv(s_colLoc, 1, c);
        glUniform2fv(s_resLoc, 1, rv);
        glUniform4fv(s_boxLoc, 1, bv);
        glUniform4fv(s_uniformsLoc, 4, u);
        const GLfloat gt[4] = { gradientTop.r/255.0f, gradientTop.g/255.0f,
                                 gradientTop.b/255.0f, gradientTop.a/255.0f };
        const GLfloat gb[4] = { gradientBottom.r/255.0f, gradientBottom.g/255.0f,
                                 gradientBottom.b/255.0f, gradientBottom.a/255.0f };
        const GLfloat gca[4] = { gradientCornerA.r/255.0f, gradientCornerA.g/255.0f,
                                  gradientCornerA.b/255.0f, gradientCornerA.a/255.0f };
        const GLfloat gcb[4] = { gradientCornerB.r/255.0f, gradientCornerB.g/255.0f,
                                  gradientCornerB.b/255.0f, gradientCornerB.a/255.0f };
        const GLfloat sd[4] = { (gX_F32)gui.style.surfaceDepth.mode,
                                 gui.style.surfaceDepth.horizonPos,
                                 gui.style.surfaceDepth.horizonSoft,
                                 gui.style.surfaceDepth.horizonGlow };
        const GLfloat mp[4] = { gui.style.surfaceDepth.metalBand,
                                 gui.style.surfaceDepth.metalWidth,
                                 gui.style.surfaceDepth.metalStrength,
                                 gui.style.surfaceDepth.metalRim };
        glUniform4fv(s_gradientTopLoc, 1, gt);
        glUniform4fv(s_gradientBottomLoc, 1, gb);
        glUniform4fv(s_gradientCornerALoc, 1, gca);
        glUniform4fv(s_gradientCornerBLoc, 1, gcb);
        glUniform1f(s_gradientModeLoc, (gX_F32)gradientMode);
        glUniform4fv(s_surfaceDepthLoc, 1, sd);
        glUniform4fv(s_metalParamsLoc, 1, mp);
        //opencode: cursor light in the element's own draw space — buffer
        //   coords for buffered canvases (inverse canvas transform), else the
        //   game/screen coords the elements already use.
        gX_F32v2 cursorPos = input.mouse.coord;
        gX_F32 cursorSpan = resW;
        if (canvas->buffer.loadingState.loaded) {
            gX_F32v2 bufferPos;
            if (gX_GUI_Canvas_ToBufferPoint(canvas, input.mouse.coord, &bufferPos))
                cursorPos = bufferPos;
        }
        const gX_F32 cursorStrength = gui.style.cursorLight.strength;
        gX_F32 radiusScale = 1.0f;
        const gX_F32 glintScale = gfx_widgetsdf_glint(element, cursorStrength,
            &radiusScale);
        const GLfloat cv[4] = { cursorPos.x, cursorPos.y,
            gui.style.cursorLight.radius * (cursorSpan / 1024.0f) * radiusScale,
            cursorStrength * glintScale };
        const GLfloat cc[4] = { gui.style.cursorLight.color.r/255.0f,
            gui.style.cursorLight.color.g/255.0f,
            gui.style.cursorLight.color.b/255.0f, 1.0f };
        glUniform4fv(s_cursorLoc, 1, cv);
        glUniform4fv(s_cursorColorLoc, 1, cc);
        gX_RHI_Disable(gX_RHI_CULL_FACE);
        gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
        gX_Drawing_DrawScreenQuad(element->rect.x, element->rect.y,
            element->rect.width, element->rect.height);
        if (scissorWas) gX_RHI_Enable(gX_RHI_SCISSOR_TEST);
        if (depthWas) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        if (cullWas) gX_RHI_Enable(gX_RHI_CULL_FACE);
    gX_Drawing_EndShader();
}

gX_I0 gX_GUI_WidgetSDF_Enqueue(gX_GUI_Element *element, gX_GUI_Canvas *canvas, gX_Color fill)
{
    if (s_queueCount >= 512) return;
    s_canvasCtx = canvas;
    //opencode: map buffer-space rect -> screen space NOW (composite mapping),
    //   so the end-of-frame pass can draw straight to the default framebuffer
    gX_F32 bx = (gX_F32)(canvas ? canvas->buffer.width  : 0);
    gX_F32 by = (gX_F32)(canvas ? canvas->buffer.height : 0);
    gX_F32 kx = (bx > 0 && canvas) ? (canvas->canvasBox.width  / bx) : 1.0f;
    gX_F32 ky = (by > 0 && canvas) ? (canvas->canvasBox.height / by) : 1.0f;
    gX_GUI_WidgetSDF_Item *it = &s_queue[s_queueCount];
    it->element = element;
    it->fill    = fill;
    it->hue     = canvas ? canvas->canvasHue : gX_WHITE;
    //opencode: v0.3.8 — hover-reactive scale pulse: apply the element scale
    //   around the rect center (SDF surfaces skip Draw_Rect's flat-rect path
    //   where the scale would otherwise be applied)
    gX_F32 scx = element->params.scale.x, scy = element->params.scale.y;
    if (scx == 0.0f) scx = 1.0f;
    if (scy == 0.0f) scy = 1.0f;
    gX_F32 rw = element->rect.width  * scx;
    gX_F32 rh = element->rect.height * scy;
    gX_F32 rx = element->rect.x + (element->rect.width  - rw) * 0.5f;
    gX_F32 ry = element->rect.y + (element->rect.height - rh) * 0.5f;
    it->sx = canvas->canvasBox.x + canvas->canvasOffset.x + rx * kx;
    it->sy = canvas->canvasBox.y + canvas->canvasOffset.y + ry * ky;
    it->sw = rw * kx;
    it->sh = rh * ky;
    it->cw = (canvas && canvas->canvasBox.width  > 0) ? canvas->canvasBox.width  : 1536.0f;
    it->ch = (canvas && canvas->canvasBox.height > 0) ? canvas->canvasBox.height : 1016.0f;
    s_queueCount++;
}

gX_I0 gX_GUI_WidgetSDF_Flush(gX_I0)
{
    if (!s_loaded || s_queueCount == 0) { s_queueCount = 0; return; }

    //opencode: v0.2.0-pre10 — END-OF-FRAME pass straight to the default
    //   framebuffer, AFTER every canvas composite: nothing can bury the quads.
    gX_RHI_BindFBO(0);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
    gX_RHI_Disable(gX_RHI_CULL_FACE);
    //opencode: SDF silhouette is evaluated against gl_FragCoord (WINDOW px),
    //   so box/res MUST live in window space too. The enqueue stores game
    //   coords; map them through the live game viewport here. Using the
    //   inherited viewport + game-space box (pre-fix) clipped every rect to
    //   the box's raw numbers as window px whenever game != window mapping
    //   (docked tools) — buttons narrowed, labels buried under the overlay.
    //   Explicit full-window viewport + window ortho: mapping-independent.
    GLfloat winW = (GLfloat)maindata.windowWidth, winH = (GLfloat)maindata.windowHeight;
    GLfloat vpx = gui.viewportBox.x, vpy = gui.viewportBox.y;
    GLfloat vpw = gui.viewportBox.width, vph = gui.viewportBox.height;
    if (vpw <= 0 || vph <= 0) { vpx = 0; vpy = 0; vpw = winW; vph = winH; }
    gX_RHI_Viewport(0, 0, (gX_I32)winW, (gX_I32)winH);
    //opencode: ONE shader bind for the whole pass — interleaved
    //   BeginShader/EndShader per quad left rlgl state such that only the
    //   first quad rasterized (probes: quads 1..N sampled bare backdrop).
    gX_Batch_Flush();
    gX_Drawing_BeginShader(s_widgetShader);
    GLint mvpLoc = glGetUniformLocation(s_widgetShader.id, "mvp");
    GLint colLoc2 = glGetUniformLocation(s_widgetShader.id, "col");
    GLint resLoc2 = glGetUniformLocation(s_widgetShader.id, "res");
    GLint boxLoc2 = glGetUniformLocation(s_widgetShader.id, "box");
    GLint uniLoc2 = glGetUniformLocation(s_widgetShader.id, "uniforms");

    for (gX_U32 qi = 0; qi < s_queueCount; qi++) {
        gX_GUI_WidgetSDF_Item *it = &s_queue[qi];
        gX_GUI_Element *element = it->element;
        gX_Color gradientTop, gradientBottom, gradientCornerA, gradientCornerB;
        gX_I32 gradientMode = 0;
        gX_GUI_WidgetSDF_GetSurfaceStyle(element, it->hue, it->fill,
            &gradientTop, &gradientBottom, &gradientCornerA, &gradientCornerB,
            &gradientMode);

            {
                const GLfloat c[4] = { it->fill.r/255.0f, it->fill.g/255.0f,
                                       it->fill.b/255.0f, it->fill.a/255.0f };
                glUniform4fv(colLoc2, 1, c);
                const GLfloat rv[2] = { winW, winH };
                glUniform2fv(resLoc2, 1, rv);
                //opencode: box in window px (same space as gl_FragCoord).
                //   Basis = the enqueue canvas dims (what the batch mapped
                //   through — verified pixel-exact docked AND undocked).
                GLfloat gameW = (it->cw > 0) ? it->cw : 1536.0f;
                GLfloat gameH = (it->ch > 0) ? it->ch : 1016.0f;
                GLfloat bx = vpx + it->sx * (vpw / gameW);
                GLfloat by = vpy + it->sy * (vph / gameH);
                GLfloat bw = it->sw * (vpw / gameW);
                GLfloat bh = it->sh * (vph / gameH);
                const GLfloat bv[4] = { bx, by, bw, bh };
                glUniform4fv(boxLoc2, 1, bv);
                GLfloat u[16] = { 0 };
                u[0] = (element->cornerRadius >= 0.0f) ? element->cornerRadius
                                                       : gui.style.cornerRadius;
                u[1] = gui.style.borderWidth;
                u[2] = 0.0f;
                u[3] = 0.0f;
                u[4] = 0.92f;
                u[5] = 0.92f;
                u[6] = 0.92f;
                u[7] = 1.0f;
                u[8] = gui.style.blueprint.latticeColor.r / 255.0f;
                u[9] = gui.style.blueprint.latticeColor.g / 255.0f;
                u[10] = gui.style.blueprint.latticeColor.b / 255.0f;
                u[11] = 1.0f;
                u[12] = gui.style.blueprint.latticeCell;
                glUniform4fv(uniLoc2, 4, u);
                const GLfloat gt[4] = { gradientTop.r/255.0f, gradientTop.g/255.0f,
                                         gradientTop.b/255.0f, gradientTop.a/255.0f };
                const GLfloat gb[4] = { gradientBottom.r/255.0f, gradientBottom.g/255.0f,
                                         gradientBottom.b/255.0f, gradientBottom.a/255.0f };
                const GLfloat gca[4] = { gradientCornerA.r/255.0f, gradientCornerA.g/255.0f,
                                          gradientCornerA.b/255.0f, gradientCornerA.a/255.0f };
                const GLfloat gcb[4] = { gradientCornerB.r/255.0f, gradientCornerB.g/255.0f,
                                          gradientCornerB.b/255.0f, gradientCornerB.a/255.0f };
                const GLfloat sd[4] = { (gX_F32)gui.style.surfaceDepth.mode,
                                         gui.style.surfaceDepth.horizonPos,
                                         gui.style.surfaceDepth.horizonSoft,
                                         gui.style.surfaceDepth.horizonGlow };
                const GLfloat mp[4] = { gui.style.surfaceDepth.metalBand,
                                         gui.style.surfaceDepth.metalWidth,
                                         gui.style.surfaceDepth.metalStrength,
                                         gui.style.surfaceDepth.metalRim };
                glUniform4fv(s_gradientTopLoc, 1, gt);
                glUniform4fv(s_gradientBottomLoc, 1, gb);
                glUniform4fv(s_gradientCornerALoc, 1, gca);
                glUniform4fv(s_gradientCornerBLoc, 1, gcb);
                glUniform1f(s_gradientModeLoc, (gX_F32)gradientMode);
                glUniform4fv(s_surfaceDepthLoc, 1, sd);
                glUniform4fv(s_metalParamsLoc, 1, mp);
                //opencode: cursor light in window px (same space as the box
                //   below) mapped through the item's canvas basis.
                const gX_F32 cursorStrength = gui.style.cursorLight.strength;
                gX_F32 radiusScale = 1.0f;
                const gX_F32 glintScale = gfx_widgetsdf_glint(element,
                    cursorStrength, &radiusScale);
                const GLfloat cv[4] = {
                    vpx + input.mouse.coord.x * (vpw / gameW),
                    vpy + input.mouse.coord.y * (vph / gameH),
                    gui.style.cursorLight.radius * (winW / 1024.0f) * radiusScale,
                    cursorStrength * glintScale };
                const GLfloat cc[4] = { gui.style.cursorLight.color.r/255.0f,
                    gui.style.cursorLight.color.g/255.0f,
                    gui.style.cursorLight.color.b/255.0f, 1.0f };
                glUniform4fv(s_cursorLoc, 1, cv);
                glUniform4fv(s_cursorColorLoc, 1, cc);
            }

            {
                GLfloat gameW = (it->cw > 0) ? it->cw : 1536.0f;
                GLfloat gameH = (it->ch > 0) ? it->ch : 1016.0f;
                GLfloat qx0 = vpx + it->sx * (vpw / gameW);
                GLfloat qy0 = vpy + it->sy * (vph / gameH);
                GLfloat qx1 = qx0 + it->sw * (vpw / gameW);
                GLfloat qy1 = qy0 + it->sh * (vph / gameH);
                const GLfloat ortho[16] = {
                    2.0f/winW, 0, 0, 0,
                    0, -2.0f/winH, 0, 0,
                    0, 0, -1.0f, 0,
                    -1.0f, 1.0f, 0, 1.0f };
                const GLfloat verts[6][5] = {
                    {qx0,qy0, 0,0}, {qx1,qy0, 1,0}, {qx1,qy1, 1,1},
                    {qx0,qy0, 0,0}, {qx1,qy1, 1,1}, {qx0,qy1, 0,1},
                };
                GLuint vao=0, vbo=0;
                glGenVertexArrays(1,&vao);
                glGenBuffers(1,&vbo);
                glBindVertexArray(vao);
                glBindBuffer(GL_ARRAY_BUFFER,vbo);
                glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STREAM_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,(void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);
                glUniformMatrix4fv(mvpLoc,1,GL_FALSE,ortho);
                glDrawArrays(GL_TRIANGLES,0,6);
                glBindVertexArray(0);
                glDeleteBuffers(1,&vbo);
                glDeleteVertexArrays(1,&vao);
            }
    }
    gX_Drawing_EndShader();

    //opencode: mark this frame for RenderDoc capture (injected runs only)
    gX_RenderDoc_Trigger();

    s_queueCount = 0;
}
