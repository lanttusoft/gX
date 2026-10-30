/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers.c]
*   Raylib backend — wrapper implementations
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

//opencode: suppress double-promotion warnings — frustum uses gX_F64 (double) by design
#pragma GCC diagnostic ignored "-Wdouble-promotion"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib_wrappers.h"

#include "rlgl.h"

#include "../../main.h"
#include "../../camera.h"
#include "../../wrappers/convert.h"
#include "../../gxmath/constants.h"

gX_I0 raylib_Rectangle_Draw(gX_Rectangle rect, gX_Rectangle_Parameters params) {
    //opencode: raylib backend — use native raylib shape functions
    Rectangle rectRaylib = (Rectangle){rect.x,rect.y,rect.width,rect.height};
    Color color0Raylib = (Color){params.colors[0].r,params.colors[0].g,
                                 params.colors[0].b,params.colors[0].a};
    Color color1Raylib,color2Raylib,color3Raylib;
    if (params.gradientVertical) {
        color1Raylib = (Color){params.colors[1].r,params.colors[1].g,
                               params.colors[1].b,params.colors[1].a};
        DrawRectangleGradientV((int)rectRaylib.x,    (int)rectRaylib.y,
                                (int)rectRaylib.width,(int)rectRaylib.height,
                                color0Raylib,color1Raylib);
    }
    else if (params.gradientHorizontal) {
        color1Raylib = (Color){params.colors[1].r,params.colors[1].g,
                               params.colors[1].b,params.colors[1].a};
        DrawRectangleGradientH((int)rectRaylib.x,    (int)rectRaylib.y,
                                (int)rectRaylib.width,(int)rectRaylib.height,
                                color0Raylib,color1Raylib);
    }
    else if (params.gradientFourPoint) {
        color1Raylib = (Color){params.colors[1].r,params.colors[1].g,
                               params.colors[1].b,params.colors[1].a};
        color2Raylib = (Color){params.colors[2].r,params.colors[2].g,
                               params.colors[2].b,params.colors[2].a};
        color3Raylib = (Color){params.colors[3].r,params.colors[3].g,
                               params.colors[3].b,params.colors[3].a};
        DrawRectangleGradientEx(rectRaylib,color0Raylib,color1Raylib,color2Raylib,color3Raylib);
    }
    else if (params.lines) {
        //opencode: support rounded outlines for focus rings and other UI
        //   primitives; the previous path could only draw square outlines.
        if (params.isRound) {
            float thickness = (params.lineThickness > 0.0f) ? params.lineThickness : 1.0f;
            DrawRectangleRoundedLinesEx(rectRaylib, params.roundness, 8, thickness, color0Raylib);
        } else {
            DrawRectangleLines((int)rectRaylib.x,    (int)rectRaylib.y,
                                (int)rectRaylib.width,(int)rectRaylib.height,color0Raylib);
        }
    }
    else if (params.isRound) {
        DrawRectangleRounded(rectRaylib, params.roundness, 8, color0Raylib);
    }
    else {
        DrawRectangleRec(rectRaylib,color0Raylib);
    }
}

gX_I0 raylib_Shader_SetValueCubemap(gX_Shader shader, gX_I32 location, gX_Texture texture) {
    rlEnableShader(shader.id);
    rlEnableTextureCubemap(texture.id);
    int i = MATERIAL_MAP_CUBEMAP;
    rlSetUniform(location, &i, SHADER_UNIFORM_INT, 1);
}

//opencode: bind a cubemap to a specific texture unit slot (for point light shadow maps)
gX_I0 raylib_Shader_SetValueCubemapSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot) {
    rlEnableShader(shader.id);
    rlActiveTextureSlot(slot);
    rlEnableTextureCubemap(texture.id);
    rlSetUniform(location, &slot, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
}

//opencode: reset active texture slot to 0 after binding to custom slot
//prevents raylib's 2D batch system from binding the default texture to the custom slot on flush
gX_I0 raylib_Shader_SetValueTextureSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot) {
    rlEnableShader(shader.id);
    rlActiveTextureSlot(slot);
    rlEnableTexture(texture.id);
    rlSetUniform(location, &slot, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
}

//opencode: wrap raylib's GetShaderLocationAttrib for vertex attribute location queries
gX_I32 raylib_Shader_GetLocationAttrib(gX_Shader shader, const gX_I8 *attribName) {
    return GetShaderLocationAttrib(gX_Shader_ConvertToBackend(shader), attribName);
}

// Initializes 3D mode with custom camera (3D)
// Original: void BeginMode3D(Camera camera) from rcore.c
gX_I0 raylib_Drawing_Begin3D(gX_Camera3D cam) {
    rlDrawRenderBatchActive();      // Update and draw internal render batch

    rlMatrixMode(RL_PROJECTION);    // Switch to projection matrix
    rlPushMatrix();                 // Save previous matrix, which contains the settings for the 2d ortho projection
    rlLoadIdentity();               // Reset current matrix (projection)

    //float aspect = (float)CORE.Window.currentFbo.width/(float)CORE.Window.currentFbo.height;
    float aspect = (float)maindata.renderWidth/(float)maindata.renderHeight;

    // NOTE: zNear and zFar values are important when computing depth buffer values
    if (cam.projection == CAMERA_PERSPECTIVE) {
        // Setup perspective projection — use tanf to avoid double promotion
        camera.frustum.top = camera.frustum.near*tanf(cam.fovy*0.5f*gX_DEG2RAD);
        camera.frustum.bottom = -camera.frustum.top;
        camera.frustum.right = camera.frustum.top*aspect;
        camera.frustum.left = -camera.frustum.right;

        rlFrustum(camera.frustum.left,   camera.frustum.right,
                  camera.frustum.bottom, camera.frustum.top,
                  camera.frustum.near,   camera.frustum.far);
    }
    else if (cam.projection == CAMERA_ORTHOGRAPHIC) {
        // Setup orthographic projection
        camera.frustum.top = cam.fovy/2.0f;
        camera.frustum.bottom = -camera.frustum.top;
        camera.frustum.right = camera.frustum.top*aspect;
        camera.frustum.left = -camera.frustum.right;

        rlOrtho(camera.frustum.left,    camera.frustum.right,
                camera.frustum.bottom,  camera.frustum.top,
                camera.frustum.near,    camera.frustum.far);
    }

    rlMatrixMode(RL_MODELVIEW);     // Switch back to modelview matrix
    rlLoadIdentity();               // Reset current matrix (modelview)

    // Setup Camera view
    Vector3 position =  gX_F32v3_ConvertToBackend(cam.position);
    Vector3 target =    gX_F32v3_ConvertToBackend(cam.target);
    Vector3 up =        gX_F32v3_ConvertToBackend(cam.up);
    Matrix matView = MatrixLookAt(position, target, up);
    rlMultMatrixf(MatrixToFloat(matView));      // Multiply modelview matrix by view matrix (camera)

    rlEnableDepthTest();            // Enable DEPTH_TEST for 3D
}

// Get the screen space position from a 3d world space position
// Original: Vector2 GetWorldToScreen(Vector3 position, Camera camera) from rcore.c
Vector2 raylib_Camera_GetWorldToScreen(Vector3 pos, Camera3D cam, Vector2 res) {
    
    Vector2 screenPosition = GetWorldToScreenEx(pos, cam, (int)res.x, (int)res.y);

    return screenPosition;
}

//helps to draw longitude lines
//from rmodels.c (raylib src) [original: DrawCircle3D(v3,f,v3,f,col)]
gX_I0 raylib_Circle_Draw_Matrix(gX_F32v3 center, gX_F32 radius, Matrix matrix, gX_Color color) {
    rlPushMatrix();
        rlTranslatef(center.x, center.y, center.z);
        //rlRotatef(rotationAngle, rotationAxis.x, rotationAxis.y, rotationAxis.z);
        rlMultMatrixf(MatrixToFloat(matrix));

        rlBegin(RL_LINES);
            for (int i = 0; i < 360; i += 10) {
                float k = (float)i;
                rlColor4ub(color.r, color.g, color.b, color.a);

                rlVertex3f(sinf(DEG2RAD*k)*radius, cosf(DEG2RAD*k)*radius, 0.0f);
                rlVertex3f(sinf(DEG2RAD*(k + 10.0f))*radius, cosf(DEG2RAD*(k + 10.0f))*radius, 0.0f);
            }
        rlEnd();
    rlPopMatrix();
}

#define MAX_TEXT_BUFFER_LENGTH 1024
#define MAX_TEXTFORMAT_BUFFERS 8        // Maximum number of static buffers for text formatting
// Formatting of text with variables to 'embed'
// WARNING: String returned will expire after this function is called MAX_TEXTFORMAT_BUFFERS times
// Original: void TextFormat(const char *text, ...) from rtext.c
inline const gX_I8 *raylib_Text_Format(const gX_I8 *text, va_list args) {

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = { 0 };
    static unsigned index = 0;

    char *currentBuffer = buffers[index];
    memset(currentBuffer, 0, MAX_TEXT_BUFFER_LENGTH);   // Clear buffer before using

    int requiredByteCount = vsnprintf(currentBuffer, MAX_TEXT_BUFFER_LENGTH, text, args);
    
    //debug
    //printf("[TEST] %s <index %i> <rBC %i>\n",currentBuffer,index,requiredByteCount);

    // If requiredByteCount is larger than the MAX_TEXT_BUFFER_LENGTH, then overflow occured
    if (requiredByteCount >= MAX_TEXT_BUFFER_LENGTH) {
        // Inserting "..." at the end of the string to mark as truncated
        char *truncBuffer = buffers[index] + MAX_TEXT_BUFFER_LENGTH - 4; // Adding 4 bytes = "...\0"
        sprintf(truncBuffer, "...");
    }

    index += 1;     // Move to next buffer for next function call
    if (index >= MAX_TEXTFORMAT_BUFFERS) index = 0;

    return (const gX_I8*)currentBuffer;
}

//opencode: default shader accessor for the RHI — raylib's rlgl default shader
unsigned int gx_get_default_shader(void) { return rlGetShaderIdDefault(); }
int *gx_get_default_shader_locs(void) { return rlGetShaderLocsDefault(); }

//opencode: default texture accessor for the RHI — raylib's default texture
unsigned int gx_get_default_texture(void) { return rlGetTextureIdDefault(); }
