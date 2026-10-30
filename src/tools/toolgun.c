/**********************************************************************************************
*
*   [gX: src/tools/toolgun.c]
*   Toolgun — shared crosshair/tool mode for in-engine editors
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

#include "toolgun.h"

#include "raylib.h"
#include "../common/enum.h"
#include "../common/log.h"
#include "../gxmath/gxmath.h"
#include "../rhi/batch.h"
#include "../wrappers/input.h"
#include "../wrappers/shape.h"
#include "../wrappers/text.h"

#include <math.h>
#include <stdio.h>

//ai: the viewmodel is authored in view space (x right, y up, z forward) and
//   projected with the camera's own fov, so it stays glued to the view at any
//   resolution; the overlay has no depth buffer, so the handful of box faces
//   are painted back to front.
#define GX_TOOLGUN_BOXES_MAX 5
#define GX_TOOLGUN_FACES_MAX 30
#define GX_TOOLGUN_FACE_NEAR 0.05f
#define GX_TOOLGUN_FONT_SCALE 0.75f
#define GX_TOOLGUN_CROSS_GAP 8.0f
#define GX_TOOLGUN_CROSS_LEN 12.0f

//ai: one viewmodel box in view space — pitch first rotates the box itself
//   (the grip tilt), the recoil kick rotates the whole gun about the pivot.
//   `shade` darkens lighter parts (the rail) or the grip, `accent` paints the
//   muzzle cap in the current tool's colour.
typedef struct gX_ToolgunBox {
    gX_F32v3 center;
    gX_F32v3 half;
    gX_F32 pitch;
    gX_F32 shade;
    gX_I1 accent;
} gX_ToolgunBox;

typedef struct gX_ToolgunFace {
    gX_F32 depth;
    gX_F32v2 p[4];
    gX_Color fill;
    gX_Color edge;
} gX_ToolgunFace;

static gX_Tools_ToolgunTool s_tgTools[GX_TOOLS_TOOLGUN_TOOLS_MAX];
static gX_I32 s_tgCount = 0;
static gX_I1 s_tgActive = 0;
static gX_I32 s_tgTool = 0;
static gX_I1 s_tgAimed = 0;
static gX_I1 s_tgFireDown[2] = {0, 0};
static gX_I1 s_tgFirePrev[2] = {0, 0};
static gX_I1 s_tgFirePressed[2] = {0, 0};
static gX_F32 s_tgBob = 0.0f;
static gX_F32 s_tgRecoil = 0.0f;
static gX_F32 s_tgSwayX = 0.0f;
static gX_F32 s_tgSwayY = 0.0f;
static gX_F32v3 s_tgLastForward = {0.0f, 0.0f, 1.0f};
static gX_F32v3 s_tgLastPos = {0.0f, 0.0f, 0.0f};
static gX_I1 s_tgHaveLast = 0;

static const gX_I32 gX_ToolgunFaceCorners[6][4] = {
    {1, 5, 7, 3},
    {0, 2, 6, 4},
    {2, 3, 7, 6},
    {0, 4, 5, 1},
    {4, 6, 7, 5},
    {0, 1, 3, 2}
};

static const gX_F32v3 gX_ToolgunFaceNormals[6] = {
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f}
};

//ai: rotate a view-space vector around the view's x axis (pitch) — the
//   viewmodel's only rotation axis.
static gX_F32v3 gX_ToolgunPitch(gX_F32v3 v, gX_F32 angle)
{
    gX_F32 c = cosf(angle);
    gX_F32 s = sinf(angle);
    gX_F32v3 result = {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    return result;
}

//ai: perspective-project a view-space point to screen pixels (raylib's own
//   convention: y grows down the screen).
static gX_I1 gX_ToolgunProject(gX_F32v3 view, gX_F32 focal, gX_F32v2 center,
    gX_F32v2 *out)
{
    if (view.z < GX_TOOLGUN_FACE_NEAR) return 0;
    out->x = center.x + view.x * focal / view.z;
    out->y = center.y - view.y * focal / view.z;
    return 1;
}

static gX_Color gX_ToolgunScaleColor(gX_Color base, gX_F32 factor)
{
    gX_F32 r = (gX_F32)base.r * factor;
    gX_F32 g = (gX_F32)base.g * factor;
    gX_F32 b = (gX_F32)base.b * factor;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    return (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, base.a};
}

//ai: collect the visible faces of the viewmodel boxes — back-facing quads
//   dropped, each face shaded against a fixed view-space key light.
static gX_I32 gX_ToolgunBuildFaces(const gX_ToolgunBox *boxes, gX_I32 boxCount,
    gX_F32v3 pivot, gX_F32 recoilPitch, gX_F32v3 offset, gX_Color body,
    gX_Color grip, gX_Color accentColor, gX_F32 focal, gX_F32v2 center,
    gX_ToolgunFace *faces, gX_I32 facesMax)
{
    gX_F32v3 light = gX_F32v3_Normalize((gX_F32v3){-0.28f, 0.52f, -0.81f});
    gX_I32 faceCount = 0;
    for (gX_I32 b = 0; b < boxCount; b++)
    {
        const gX_ToolgunBox *box = &boxes[b];
        gX_F32 pitch = box->pitch + recoilPitch;
        gX_F32v3 local = gX_F32v3_Sub(box->center, pivot);
        gX_F32v3 origin = gX_F32v3_Add(
            gX_F32v3_Add(pivot, gX_ToolgunPitch(local, recoilPitch)), offset);
        gX_F32v3 corners[8];
        for (gX_I32 c = 0; c < 8; c++)
        {
            gX_F32v3 sign = {
                (c & 1) ? 1.0f : -1.0f,
                (c & 2) ? 1.0f : -1.0f,
                (c & 4) ? 1.0f : -1.0f
            };
            gX_F32v3 corner = {
                sign.x * box->half.x,
                sign.y * box->half.y,
                sign.z * box->half.z
            };
            corners[c] = gX_F32v3_Add(origin, gX_ToolgunPitch(corner, pitch));
        }
        gX_Color base = body;
        if (box->accent) base = accentColor;
        else if (box->shade < 0.9f) base = grip;
        for (gX_I32 f = 0; f < 6 && faceCount < facesMax; f++)
        {
            gX_F32v3 normal = gX_ToolgunPitch(gX_ToolgunFaceNormals[f], pitch);
            if (gX_F32v3_Dot(normal, origin) >= 0.0f) continue;
            gX_ToolgunFace *face = &faces[faceCount];
            gX_F32v3 depthSum = {0.0f, 0.0f, 0.0f};
            gX_I1 visible = 1;
            for (gX_I32 p = 0; p < 4; p++)
            {
                gX_F32v3 corner = corners[gX_ToolgunFaceCorners[f][p]];
                depthSum = gX_F32v3_Add(depthSum, corner);
                if (!gX_ToolgunProject(corner, focal, center, &face->p[p]))
                {
                    visible = 0;
                }
            }
            if (!visible) continue;
            gX_F32 lambert = gX_F32v3_Dot(normal, light);
            if (lambert < 0.0f) lambert = 0.0f;
            gX_F32 shade = (0.52f + 0.48f * lambert) * box->shade;
            face->depth = gX_F32v3_Length(depthSum) * 0.25f;
            face->fill = gX_ToolgunScaleColor(base, shade);
            face->edge = gX_ToolgunScaleColor(base, shade * 0.45f);
            faceCount++;
        }
    }
    return faceCount;
}

static gX_I0 gX_ToolgunSortFaces(gX_ToolgunFace *faces, gX_I32 count)
{
    for (gX_I32 i = 1; i < count; i++)
    {
        gX_ToolgunFace key = faces[i];
        gX_I32 j = i - 1;
        while (j >= 0 && faces[j].depth < key.depth)
        {
            faces[j + 1] = faces[j];
            j--;
        }
        faces[j + 1] = key;
    }
}

static gX_I0 gX_ToolgunText(const gX_I8 *text, gX_F32v2 position, gX_F32 size,
    gX_Color color)
{
    gX_Batch_Flush();
    gX_RawString_Draw_Lesser(text, position, size, color);
}

static gX_I0 gX_ToolgunTextCentered(const gX_I8 *text, gX_F32 centerX, gX_F32 y,
    gX_F32 size, gX_Color color)
{
    gX_F32 width = (gX_F32)MeasureText((const char *)text,
        (gX_I32)(size * GX_TOOLGUN_FONT_SCALE));
    gX_ToolgunText(text, (gX_F32v2){centerX - width * 0.5f, y}, size, color);
}

//ai: enter the mode with the host's tool table (copied; names/hints may be
//   static) — presentation state starts clean.
gX_I0 gX_Tools_Toolgun_Open(const gX_Tools_ToolgunTool *tools, gX_I32 count)
{
    if (tools == 0 || count <= 0) return;
    if (count > GX_TOOLS_TOOLGUN_TOOLS_MAX) count = GX_TOOLS_TOOLGUN_TOOLS_MAX;
    for (gX_I32 i = 0; i < count; i++) s_tgTools[i] = tools[i];
    s_tgCount = count;
    s_tgTool = 0;
    s_tgAimed = 0;
    s_tgFireDown[0] = 0;
    s_tgFireDown[1] = 0;
    s_tgFirePrev[0] = 0;
    s_tgFirePrev[1] = 0;
    s_tgFirePressed[0] = 0;
    s_tgFirePressed[1] = 0;
    s_tgBob = 0.0f;
    s_tgRecoil = 0.0f;
    s_tgSwayX = 0.0f;
    s_tgSwayY = 0.0f;
    s_tgHaveLast = 0;
    s_tgActive = 1;
    gX_TraceLog(gX_LOG_NOTICE, "TOOLGUN: on (%d tools)", count);
}

gX_I0 gX_Tools_Toolgun_Close(gX_I0)
{
    if (!s_tgActive) return;
    s_tgActive = 0;
    s_tgCount = 0;
    s_tgTool = 0;
    s_tgAimed = 0;
    s_tgFireDown[0] = 0;
    s_tgFireDown[1] = 0;
    s_tgFirePressed[0] = 0;
    s_tgFirePressed[1] = 0;
    gX_TraceLog(gX_LOG_NOTICE, "TOOLGUN: off");
}

gX_I1 gX_Tools_Toolgun_Active(gX_I0)
{
    return s_tgActive;
}

gX_I32 gX_Tools_Toolgun_ToolCount(gX_I0)
{
    return s_tgCount;
}

gX_I32 gX_Tools_Toolgun_Tool(gX_I0)
{
    return s_tgTool;
}

const gX_I8 *gX_Tools_Toolgun_ToolName(gX_I32 index)
{
    if (index < 0 || index >= s_tgCount) return "";
    return s_tgTools[index].name;
}

gX_I0 gX_Tools_Toolgun_Select(gX_I32 index)
{
    if (!s_tgActive) return;
    if (index < 0 || index >= s_tgCount) return;
    s_tgTool = index;
    s_tgRecoil = 1.0f;
}

gX_I0 gX_Tools_Toolgun_Cycle(gX_I32 delta)
{
    if (!s_tgActive) return;
    if (s_tgCount <= 0 || delta == 0) return;
    gX_I32 next = s_tgTool + (delta > 0 ? 1 : -1);
    if (next < 0) next = s_tgCount - 1;
    if (next >= s_tgCount) next = 0;
    gX_Tools_Toolgun_Select(next);
}

gX_I0 gX_Tools_Toolgun_SetAim(gX_I1 aimed)
{
    s_tgAimed = aimed ? 1 : 0;
}

//ai: per-frame input poll — the wheel cycles tools, the left button is
//   primary fire and the right button secondary; both expose down and
//   pressed edges for the host's per-tool effects.
gX_I0 gX_Tools_Toolgun_Step(gX_I0)
{
    if (!s_tgActive) return;
    gX_F32 wheel = gX_Mouse_GetWheelMove();
    if (wheel > 0.05f) gX_Tools_Toolgun_Cycle(1);
    else if (wheel < -0.05f) gX_Tools_Toolgun_Cycle(-1);
    s_tgFireDown[0] = gX_Mouse_IsDown(gX_MOUSE_BUTTON_LEFT);
    s_tgFireDown[1] = gX_Mouse_IsDown(gX_MOUSE_BUTTON_RIGHT);
    for (gX_I32 i = 0; i < 2; i++)
    {
        s_tgFirePressed[i] = (s_tgFireDown[i] && !s_tgFirePrev[i]) ? 1 : 0;
        s_tgFirePrev[i] = s_tgFireDown[i];
    }
    if (s_tgFirePressed[0] || s_tgFirePressed[1]) s_tgRecoil = 1.0f;
}

gX_I1 gX_Tools_Toolgun_Fire(gX_I1 secondary)
{
    return s_tgFireDown[secondary ? 1 : 0];
}

gX_I1 gX_Tools_Toolgun_FirePressed(gX_I1 secondary)
{
    return s_tgFirePressed[secondary ? 1 : 0];
}

//ai: presentation — the viewmodel, the muzzle flash, the crosshair and the
//   tool readout, all in the 2D overlay after the world render.
gX_I0 gX_Tools_Toolgun_Draw(gX_Camera3D camera3D)
{
    if (!s_tgActive) return;
    gX_F32v3 forward = gX_F32v3_Sub(camera3D.target, camera3D.position);
    if (gX_F32v3_LengthSqr(forward) < 0.0001f) return;
    forward = gX_F32v3_Normalize(forward);
    gX_F32v3 right = gX_F32v3_Cross(forward, camera3D.up);
    if (gX_F32v3_LengthSqr(right) < 0.0001f) return;
    right = gX_F32v3_Normalize(right);
    gX_F32v3 up = gX_F32v3_Cross(right, forward);

    gX_F32 dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    gX_F32 speed = 0.0f;
    if (s_tgHaveLast) speed = gX_F32v3_Distance(camera3D.position, s_tgLastPos);
    gX_F32v3 deltaForward = gX_F32v3_Sub(forward, s_tgLastForward);
    gX_F32 swayTargetX = gX_F32v3_Dot(deltaForward, right) * -6.0f;
    gX_F32 swayTargetY = gX_F32v3_Dot(deltaForward, up) * -6.0f;
    if (swayTargetX > 0.030f) swayTargetX = 0.030f;
    if (swayTargetX < -0.030f) swayTargetX = -0.030f;
    if (swayTargetY > 0.030f) swayTargetY = 0.030f;
    if (swayTargetY < -0.030f) swayTargetY = -0.030f;
    gX_F32 swayBlend = dt * 9.0f;
    if (swayBlend > 1.0f) swayBlend = 1.0f;
    s_tgSwayX += (swayTargetX - s_tgSwayX) * swayBlend;
    s_tgSwayY += (swayTargetY - s_tgSwayY) * swayBlend;
    gX_F32 stride = 0.0f;
    if (dt > 0.0001f) stride = speed / dt;
    if (stride > 6.0f) stride = 6.0f;
    s_tgBob += dt * (1.30f + stride * 0.85f);
    s_tgRecoil -= dt * 4.2f;
    if (s_tgRecoil < 0.0f) s_tgRecoil = 0.0f;
    s_tgLastForward = forward;
    s_tgLastPos = camera3D.position;
    s_tgHaveLast = 1;

    gX_F32 screenW = (gX_F32)GetScreenWidth();
    gX_F32 screenH = (gX_F32)GetScreenHeight();
    gX_F32 fovy = camera3D.fovy;
    if (fovy < 5.0f) fovy = 5.0f;
    if (fovy > 160.0f) fovy = 160.0f;
    gX_F32 focal = (screenH * 0.5f) / tanf(fovy * 0.5f * DEG2RAD);
    gX_F32v2 center = {screenW * 0.5f, screenH * 0.5f};

    gX_Color accent = (gX_Color){133, 224, 238, 255};
    if (s_tgCount > 0) accent = s_tgTools[s_tgTool].accent;
    gX_Color body = {66, 74, 90, 255};
    gX_Color grip = {44, 48, 58, 255};

    gX_F32 recoilPitch = s_tgRecoil * 0.38f;
    gX_F32v3 pivot = {0.340f, -0.240f, 0.420f};
    gX_F32v3 offset = {
        sinf(s_tgBob) * 0.0045f + s_tgSwayX,
        fabsf(cosf(s_tgBob)) * 0.0040f + s_tgSwayY,
        s_tgRecoil * -0.018f
    };
    const gX_ToolgunBox boxes[GX_TOOLGUN_BOXES_MAX] = {
        {{0.340f, -0.200f, 0.420f}, {0.090f, 0.075f, 0.280f}, 0.0f, 1.00f, 0},
        {{0.345f, -0.180f, 0.800f}, {0.030f, 0.030f, 0.150f}, 0.0f, 1.15f, 0},
        {{0.335f, -0.345f, 0.360f}, {0.048f, 0.090f, 0.055f}, 0.42f, 0.85f, 0},
        {{0.340f, -0.115f, 0.500f}, {0.024f, 0.016f, 0.140f}, 0.0f, 1.25f, 0},
        {{0.345f, -0.180f, 0.965f}, {0.045f, 0.045f, 0.024f}, 0.0f, 1.00f, 1}
    };
    gX_ToolgunFace faces[GX_TOOLGUN_FACES_MAX];
    gX_I32 faceCount = gX_ToolgunBuildFaces(boxes, GX_TOOLGUN_BOXES_MAX, pivot,
        recoilPitch, offset, body, grip, accent, focal, center, faces,
        GX_TOOLGUN_FACES_MAX);
    gX_ToolgunSortFaces(faces, faceCount);
    for (gX_I32 i = 0; i < faceCount; i++)
    {
        gX_ToolgunFace *face = &faces[i];
        //ai: the overlay phase leaves face culling on (probed), so the quads
        //   emit in the front-facing winding (the software back-face pass has
        //   already dropped the rest).
        gX_Triangle_Draw((gX_Triangle){face->p[0], face->p[1], face->p[2], 1.0f},
            face->fill);
        gX_Triangle_Draw((gX_Triangle){face->p[0], face->p[2], face->p[3], 1.0f},
            face->fill);
        for (gX_I32 e = 0; e < 4; e++)
        {
            gX_Line_Draw((gX_Line){face->p[e], face->p[(e + 1) % 4], 1.4f},
                face->edge);
        }
    }

    gX_F32v3 muzzle = gX_F32v3_Add(
        gX_F32v3_Add(pivot,
            gX_ToolgunPitch(
                gX_F32v3_Sub((gX_F32v3){0.345f, -0.180f, 0.995f}, pivot),
                recoilPitch)),
        offset);
    gX_F32v2 muzzlePoint = {0.0f, 0.0f};
    if (s_tgRecoil > 0.30f
        && gX_ToolgunProject(muzzle, focal, center, &muzzlePoint))
    {
        gX_F32 flash = (s_tgRecoil - 0.30f) / 0.70f;
        gX_Circle_Draw((gX_Circle){muzzlePoint, 5.0f + 13.0f * flash},
            (gX_Color){accent.r, accent.g, accent.b, (gX_U8)(120.0f + 120.0f * flash)});
        gX_Circle_Draw((gX_Circle){muzzlePoint, 2.5f + 5.0f * flash},
            (gX_Color){255, 252, 236, (gX_U8)(200.0f + 55.0f * flash)});
    }

    gX_Color crossTint = {228, 238, 246, 216};
    gX_F32 gap = screenH * 0.0084f;
    gX_F32 length = screenH * 0.0126f;
    if (s_tgAimed)
    {
        crossTint = accent;
        crossTint.a = 235;
        gap *= 0.62f;
        length *= 0.72f;
    }
    gX_Line_Draw((gX_Line){{center.x - gap - length, center.y},
        {center.x - gap, center.y}, 1.6f}, crossTint);
    gX_Line_Draw((gX_Line){{center.x + gap, center.y},
        {center.x + gap + length, center.y}, 1.6f}, crossTint);
    gX_Line_Draw((gX_Line){{center.x, center.y - gap - length},
        {center.x, center.y - gap}, 1.6f}, crossTint);
    gX_Line_Draw((gX_Line){{center.x, center.y + gap},
        {center.x, center.y + gap + length}, 1.6f}, crossTint);
    gX_Circle_Draw((gX_Circle){center, 1.6f}, crossTint);

    if (s_tgCount > 0)
    {
        gX_I8 line[128];
        gX_Color hint = {168, 186, 200, 235};
        gX_Color label = {120, 138, 154, 235};
        gX_ToolgunTextCentered(s_tgTools[s_tgTool].name, center.x,
            screenH - 76.0f, 20.0f, accent);
        gX_ToolgunTextCentered(s_tgTools[s_tgTool].hint, center.x,
            screenH - 50.0f, 13.0f, hint);
        snprintf((char *)line, sizeof(line), "TOOL %d/%d  //  WHEEL CYCLES",
            s_tgTool + 1, s_tgCount);
        gX_ToolgunTextCentered(line, center.x, screenH - 30.0f, 11.0f, label);
    }
}
