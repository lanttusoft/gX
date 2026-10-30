/**********************************************************************************************
* 
*   [wrappers/camera.c]
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
#include "wrappers_header.h"
#include "camera.h"

#include "../main.h"


gX_F32v3 gX_Camera_GetForward(gX_Camera3D *cam) {
    Vector3 vec = GetCameraForward((Camera3D*)cam);
    return gX_F32v3_ConvertFromBackend(vec);
}

gX_F32v3 gX_Camera_GetUp(gX_Camera3D *cam) {
    Vector3 vec = GetCameraUp((Camera3D*)cam);
    return gX_F32v3_ConvertFromBackend(vec);
}

gX_F32v3 gX_Camera_GetRight(gX_Camera3D *cam) {
    Vector3 vec = GetCameraRight((Camera3D*)cam);
    return gX_F32v3_ConvertFromBackend(vec);
}

gX_I0 gX_Camera_MoveForward(gX_Camera3D *cam, gX_F32 dist, gX_I1 worldplane) {
    CameraMoveForward((Camera3D*)cam,dist,worldplane);
}

gX_I0 gX_Camera_MoveUp(gX_Camera3D *cam, gX_F32 dist) {
    CameraMoveUp((Camera3D*)cam,dist);
}

gX_I0 gX_Camera_MoveRight(gX_Camera3D *cam, gX_F32 dist, gX_I1 worldplane) {
    CameraMoveRight((Camera3D*)cam,dist,worldplane);
}

gX_I0 gX_Camera_MoveToTarget(gX_Camera3D *cam, gX_F32 dist) {
    CameraMoveToTarget((Camera3D*)cam,dist);
}

gX_I0 gX_Camera_Yaw(gX_Camera3D *cam, gX_F32 angle, gX_I1 rotatearound) {
    CameraYaw((Camera3D*)cam,angle,rotatearound);
}

gX_I0 gX_Camera_Pitch(gX_Camera3D *cam, gX_F32 angle, gX_I1 lock, gX_I1 rotatearound, gX_I1 rotateup) {
    CameraPitch((Camera3D*)cam,angle,lock,rotatearound,rotateup);
}

gX_I0 gX_Camera_Roll(gX_Camera3D *cam, gX_F32 angle) {
    CameraRoll((Camera3D*)cam,angle);
}

gX_Matrix gX_Camera_GetView(gX_Camera3D *cam) {
    Matrix mat = GetCameraViewMatrix((Camera3D*)cam);
    return gX_Matrix_ConvertFromBackend(mat);
}

gX_Matrix gX_Camera_GetViewNoTranslate(gX_Camera3D *cam) {
    gX_Matrix mat = gX_Camera_GetView(cam);
    mat.m12 = 0.0f;
    mat.m13 = 0.0f;
    mat.m14 = 0.0f;
    return mat;
}

gX_Matrix gX_Camera_GetProjection(gX_Camera3D *cam, gX_F32 aspect) {
    Matrix mat = GetCameraProjectionMatrix((Camera3D*)cam,aspect);
    return gX_Matrix_ConvertFromBackend(mat);
}

gX_F32v2 gX_Camera_GetWorldToScreen(gX_F32v3 pos, gX_Camera3D cam) {
    Camera cam3D = gX_Camera3D_ConvertToBackend(cam);
    Vector3 pos3D = (Vector3){pos.x,pos.y,pos.z};
    Vector2 result = raylib_Camera_GetWorldToScreen(pos3D, cam3D, (Vector2){maindata.renderWidth,maindata.renderHeight});
    return (gX_F32v2){result.x,result.y};
}
