/**********************************************************************************************
*
*   [gX: src/renderer/gfx/sun_uv.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


//opencode: shared sun-to-screen UV projection — replaces duplicated code in godray.c and lensflare.c
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "sun_uv.h"

#include "../../camera.h"

#include "../../main.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/vector3.h"
#include "../../gxmath/constants.h"

gX_F32v2 gX_RendererGFX_GetSunScreenUV(gX_RendererGFX *gfx, gX_I1 *outBehind)
{
    gX_F32v3 sunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);

    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);

    gX_F32v4 sunView;
    sunView.x = view.m0 * sunDir.x + view.m4 * sunDir.y + view.m8  * sunDir.z + view.m12 * 0.0f;
    sunView.y = view.m1 * sunDir.x + view.m5 * sunDir.y + view.m9  * sunDir.z + view.m13 * 0.0f;
    sunView.z = view.m2 * sunDir.x + view.m6 * sunDir.y + view.m10 * sunDir.z + view.m14 * 0.0f;
    sunView.w = view.m3 * sunDir.x + view.m7 * sunDir.y + view.m11 * sunDir.z + view.m15 * 0.0f;

    gX_F32v4 sunClip;
    sunClip.x = proj.m0 * sunView.x + proj.m4 * sunView.y + proj.m8  * sunView.z + proj.m12 * sunView.w;
    sunClip.y = proj.m1 * sunView.x + proj.m5 * sunView.y + proj.m9  * sunView.z + proj.m13 * sunView.w;
    sunClip.z = proj.m2 * sunView.x + proj.m6 * sunView.y + proj.m10 * sunView.z + proj.m14 * sunView.w;
    sunClip.w = proj.m3 * sunView.x + proj.m7 * sunView.y + proj.m11 * sunView.z + proj.m15 * sunView.w;

    gX_I1 behind = (sunClip.w <= 0.0f);
    if (outBehind) *outBehind = behind;

    if (fabsf(sunClip.w) < 0.0001f) sunClip.w = 0.0001f;

    gX_F32v2 ndc = {sunClip.x / sunClip.w, sunClip.y / sunClip.w};

    gX_F32v2 uv = {(ndc.x + 1.0f) * 0.5f, (ndc.y + 1.0f) * 0.5f};

    if (uv.x < 0.0f) uv.x = 0.0f;
    if (uv.x > 1.0f) uv.x = 1.0f;
    if (uv.y < 0.0f) uv.y = 0.0f;
    if (uv.y > 1.0f) uv.y = 1.0f;

    return uv;
}
