/**********************************************************************************************
*
*   [gX: src/renderer/gfx/light.h]
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


#ifndef RENDERER_GFX_LIGHT_H
#define RENDERER_GFX_LIGHT_H

#include "../../common/types.h"

#include "camera.h"

//opencode: fixed waypoint capacity for authored light motion paths.
#define gX_GFX_LIGHT_WAYPOINTS 8

typedef struct gX_RendererGFX_Light {
    gX_I1   sun;
    gX_I32  type;
    
    gX_F32      fov;
    gX_F32v3    pos;
    gX_F32v3    dir;
    //opencode: authored circular path for point lights; plane 0=XY, 1=XZ, 2=YZ.
    gX_I1       orbitEnabled;
    gX_I1       orbitPaused;
    gX_I1       orbitLoop;
    gX_I32      orbitPlane;
    gX_F32v3    orbitCenter;
    gX_F32      orbitRadius;
    gX_F32      orbitSpeed;
    gX_F32      orbitPhase;
    gX_F64      orbitElapsed;
    //opencode: waypoint motion path — flat xyz triples for the F32 schema array.
    gX_I32      pathMode;          //opencode: 0=circle, 1=waypoints
    gX_I32      pathWaypointCount; //opencode: 2..gX_GFX_LIGHT_WAYPOINTS
    gX_F32      pathWaypoints[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_I1       pathPingPong;      //opencode: waypoint loop direction reversal
    gX_I1       pathSmooth;        //opencode: Catmull-Rom segments instead of lines
    //opencode: per-waypoint Bezier handles: pathTangents are the out-handles,
    //   pathTangentsIn the in-handles. With pathHandlesIndependent = 0 the in
    //   side mirrors the out side (the original behavior); zero handles
    //   reproduce the straight polyline.
    gX_I1       pathBezier;
    gX_F32      pathTangents[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_F32      pathTangentsIn[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_I1       pathHandlesIndependent;
    gX_RendererGFX_Camera camera;
    gX_F32      brightness;
    
    gX_Color    diffuseTint;
    gX_Color    specularTint;
    gX_F32      diffuseFactor;
    gX_F32      specularFactor;
    gX_Color    color;
    gX_F32      attenConst;
    gX_F32      attenLinear;
    gX_F32      attenQuad;
    
    //opencode: light cookie (projected texture for spot lights)
    gX_Texture  cookieTex;    //opencode: cookie texture (id=0 = no cookie)
    gX_I8       cookiePath[128]; //opencode: source path — cookie atlas rebuilds from CPU data
    gX_I1       cookieEnabled;

    //opencode: point light shadows
    gX_I1       castShadows;
    gX_U32      shadowSize;
    gX_F32      shadowFov;
    gX_U32      shadowFboID;
    gX_Texture  shadowDepth;
    gX_Matrix   shadowViews[6];
    gX_F32      shadowFar;
    gX_F32      shadowRange; //opencode: max distance for shadow casting, 0=unlimited
} gX_RendererGFX_Light;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I32 gX_RendererGFX_Light_Init(  gX_RendererGFX_Light *light);
gX_I0  gX_RendererGFX_Light_Update(gX_RendererGFX *gfx, gX_RendererGFX_Light *light);
//opencode: advance a configured circular path by a frame delta; paused or
//   disabled paths preserve the current position.
gX_I0 gX_RendererGFX_Light_Orbit_Update(gX_RendererGFX_Light *light,
    gX_F64 deltaTime);
gX_I0 gX_RendererGFX_PointShadow_InitArray(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_PointShadow_InitPlaceholder(gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Light_SetCookie(gX_RendererGFX *gfx, gX_RendererGFX_Light *light, const gX_I8 *path);

#endif //RENDERER_GFX_LIGHT_H
