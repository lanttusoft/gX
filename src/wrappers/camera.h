/**********************************************************************************************
* 
*   [wrappers/camera.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib rcamera functions
#ifndef WRAPPERS_CAMERA_H
#define WRAPPERS_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//rcamera.h
gX_F32v3    gX_Camera_GetForward(   gX_Camera3D *cam);
gX_F32v3    gX_Camera_GetUp(        gX_Camera3D *cam);
gX_F32v3    gX_Camera_GetRight(     gX_Camera3D *cam);
gX_I0       gX_Camera_MoveForward(  gX_Camera3D *cam, gX_F32 dist, gX_I1 worldplane);
gX_I0       gX_Camera_MoveUp(       gX_Camera3D *cam, gX_F32 dist);
gX_I0       gX_Camera_MoveRight(    gX_Camera3D *cam, gX_F32 dist, gX_I1 worldplane);
gX_I0       gX_Camera_MoveToTarget( gX_Camera3D *cam, gX_F32 dist);
gX_I0       gX_Camera_Yaw(          gX_Camera3D *cam, gX_F32 angle, gX_I1 rotatearound);
gX_I0       gX_Camera_Pitch(        gX_Camera3D *cam, gX_F32 angle, gX_I1 lock, gX_I1 rotatearound, gX_I1 rotateup);
gX_I0       gX_Camera_Roll(         gX_Camera3D *cam, gX_F32 angle);
gX_Matrix   gX_Camera_GetView(           gX_Camera3D *cam);
gX_Matrix   gX_Camera_GetViewNoTranslate(gX_Camera3D *cam);
gX_Matrix   gX_Camera_GetProjection(gX_Camera3D *cam, gX_F32 aspect);

gX_F32v2    gX_Camera_GetWorldToScreen(     gX_F32v3 pos, gX_Camera3D cam);
gX_F32v2    gX_Camera_GetScreenToWorld2D(   gX_F32v2 pos, gX_Camera2D cam);
gX_F32v2    gX_Camera_GetWorldToScreen2D(   gX_F32v2 pos, gX_Camera2D cam);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_CAMERA_H
