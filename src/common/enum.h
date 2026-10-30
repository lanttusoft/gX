/**********************************************************************************************
* 
*   [common/enum.h]
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
#ifndef ENUM_H
#define ENUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "enum/keys.h"
#include "enum/shader.h"
#include "enum/texture.h"

#define gX_DATATYPES_VECTORIZE(dt) \
    X(dt) \
    X(dt##V2) \
    X(dt##V3) \
    X(dt##V4) \
    
#define gX_DATATYPES_INT(i) \
    gX_DATATYPES_VECTORIZE(i##8) \
    gX_DATATYPES_VECTORIZE(i##16) \
    gX_DATATYPES_VECTORIZE(i##32) \
    gX_DATATYPES_VECTORIZE(i##64) \

#define gX_DATATYPES \
    X(I0) \
    X(I1) \
    gX_DATATYPES_INT(I) \
    gX_DATATYPES_INT(U) \
    gX_DATATYPES_VECTORIZE(F32) \
    gX_DATATYPES_VECTORIZE(F64) \
    X(STRING) \
    X(RECTANGLE) \
    X(COLOR) \
    X(STRUCT_HEAP) \
    X(STRUCT_AUDIOSTREAM) \
    X(STRUCT_CAMERA2D) \
    X(STRUCT_CAMERA3D) \
    X(STRUCT_FONT) \
    X(STRUCT_GUI_CANVAS) \
    X(STRUCT_GUI_ELEMENT) \
    X(STRUCT_IMAGE) \
    X(STRUCT_MATERIAL) \
    X(STRUCT_MESH) \
    X(STRUCT_MODEL) \
    X(STRUCT_MUSIC) \
    X(STRUCT_RENDERTEXTURE) \
    X(STRUCT_SHADER) \
    X(STRUCT_SOUND) \
    X(STRUCT_TEXTURE) \
    X(STRUCT_WAVE) \

typedef enum {
    gX_DATATYPE_NULL = 0,
#define X(dt) gX_DATATYPE_##dt,
    gX_DATATYPES
#undef X
} gX_Datatypes;
const gX_I8 *gX_Datatypes_String(gX_I32 datatype);

#define gX_SCENES \
    X(TITLE) \
    X(WORLD) \
    X(BATTLE) \
    X(MAP) \
    X(MENU) \
    X(ROOMS) \
    X(RENDERER_ANALOG) \
    X(RENDERER_BACKGROUND) \
    X(RENDERER_CLOCK) \
    X(RENDERER_DEMOSCRIPT) \
    X(RENDERER_FRACTAL) \
    X(RENDERER_GENERATE) \
    X(RENDERER_GFX) \
    X(RENDERER_GFX_DEMO) \
    X(RENDERER_GLOBE) \
    X(RENDERER_LED) \
    X(RENDERER_PP) \
    X(RENDERER_RAYMARCH) \
    X(RENDERER_SKY) \
    X(RENDERER_TELETEXT) \
    X(RENDERER_TEXTWRITER) \
    X(RENDERER_WATER) \
    X(RENDERER_VIDEO) \
    X(RENDERER_WIREFRAME) \
    X(EDITOR) \
    X(ROOMPLANNER) \
    X(MARKET) \
    X(ARCADE) \
    X(LOADINGSCREEN) \
    X(STARTTERMINAL) \
    X(PHYSICS) \
    X(GFXDEBUG) \
    X(SYNTHVIS) \
    X(GUIPLAYGROUND) \
    X(INTERIOR) \
    X(PS2_BATTLE) \
    X(GUITESTER) \
    X(MODULE) \
    
typedef enum {
    gX_SCENE_NULL = 0,
#define X(scene) gX_SCENE_##scene,
    gX_SCENES
#undef X
} gX_Scenes;
const gX_I8 *gX_Scenes_String(gX_I32 scene);

#define gX_MOUSEBUTTONS \
    X(LEFT) \
    X(RIGHT) \
    X(MIDDLE) \
    X(SIDE) \
    X(EXTRA) \
    X(FORWARD) \
    X(BACK) \

typedef enum {
#define X(mousebutton) gX_MOUSE_BUTTON_##mousebutton,
    gX_MOUSEBUTTONS
#undef X
} gX_MouseButton;
const gX_I8 *gX_MouseButton_String(gX_I32 button);

#define gX_MOUSECURSOR \
    X(DEFAULT) \
    X(ARROW) \
    X(IBEAM) \
    X(CROSSHAIR) \
    X(POINTING_HAND) \
    X(RESIZE_EW) \
    X(RESIZE_NS) \
    X(RESIZE_NWSE) \
    X(RESIZE_NESW) \
    X(RESIZE_ALL) \
    X(NOT_ALLOWED) \

typedef enum {
#define X(cursor) gX_MOUSE_CURSOR_##cursor,
    gX_MOUSECURSOR
#undef X
} gX_MouseCursor;
const gX_I8 *gX_MouseCursor_String(gX_I32 cursor);

#define gX_GAMEPADBUTTON \
    X(UNKNOWN) \
    X(LEFT_FACE_UP) \
    X(LEFT_FACE_RIGHT) \
    X(LEFT_FACE_DOWN) \
    X(LEFT_FACE_LEFT) \
    X(RIGHT_FACE_UP) \
    X(RIGHT_FACE_RIGHT) \
    X(RIGHT_FACE_DOWN) \
    X(RIGHT_FACE_LEFT) \
    X(LEFT_TRIGGER_1) \
    X(LEFT_TRIGGER_2) \
    X(RIGHT_TRIGGER_1) \
    X(RIGHT_TRIGGER_2) \
    X(MIDDLE_LEFT) \
    X(MIDDLE) \
    X(MIDDLE_RIGHT) \
    X(LEFT_THUMB) \
    X(RIGHT_THUMB) \

typedef enum {
#define X(button) gX_GAMEPAD_BUTTON_##button,
    gX_GAMEPADBUTTON
#undef X
} gX_GamepadButton;
const gX_I8 *gX_GamepadButton_String(gX_I32 button);

#define gX_GAMEPADAXIS \
    X(LEFT_X) \
    X(LEFT_Y) \
    X(RIGHT_X) \
    X(RIGHT_Y) \
    X(LEFT_TRIGGER) \
    X(RIGHT_TRIGGER) \

typedef enum {
#define X(axis) gX_GAMEPAD_AXIS_##axis,
    gX_GAMEPADAXIS
#undef X
} gX_GamepadAxis;
const gX_I8 *gX_GamepadAxis_String(gX_I32 axis);

#define gX_MATERIALMAPINDEX \
    X(ALBEDO) \
    X(METALNESS) \
    X(NORMAL) \
    X(ROUGHNESS) \
    X(OCCLUSION) \
    X(EMISSION) \
    X(HEIGHT) \
    X(CUBEMAP) \
    X(IRRADIANCE) \
    X(PREFILTER) \
    X(BRDF) \

typedef enum {
#define X(map) gX_MATERIAL_MAP_##map,
    gX_MATERIALMAPINDEX
#undef X
} gX_MaterialMapIndex;
const gX_I8 *gX_MaterialMapIndex_String(gX_I32 mapindex);

#define gX_PIXELFORMATS \
    X(UNCOMPRESSED_GRAYSCALE) \
    X(UNCOMPRESSED_GRAY_ALPHA) \
    X(UNCOMPRESSED_R5G6B5) \
    X(UNCOMPRESSED_R8G8B8) \
    X(UNCOMPRESSED_R5G5B5A1) \
    X(UNCOMPRESSED_R4G4B4A4) \
    X(UNCOMPRESSED_R8G8B8A8) \
    X(UNCOMPRESSED_R32) \
    X(UNCOMPRESSED_R32G32B32) \
    X(UNCOMPRESSED_R32G32B32A32) \
    X(UNCOMPRESSED_R16) \
    X(UNCOMPRESSED_R16G16B16) \
    X(UNCOMPRESSED_R16G16B16A16) \
    X(COMPRESSED_DXT1_RGB) \
    X(COMPRESSED_DXT1_RGBA) \
    X(COMPRESSED_DXT3_RGBA) \
    X(COMPRESSED_DXT5_RGBA) \
    X(COMPRESSED_ETC1_RGB) \
    X(COMPRESSED_ETC2_RGB) \
    X(COMPRESSED_ETC2_EAC_RGBA) \
    X(COMPRESSED_PVRT_RGB) \
    X(COMPRESSED_PVRT_RGBA) \
    X(COMPRESSED_ASTC_4x4_RGBA) \
    X(COMPRESSED_ASTC_8x8_RGBA) \

typedef enum {
    gX_PIXELFORMAT_NULL = 0,
#define X(pixelformat) gX_PIXELFORMAT_##pixelformat,
    gX_PIXELFORMATS
#undef X
} gX_PixelFormat;
const gX_I8 *gX_PixelFormat_String(gX_I32 format);

#define gX_FONTTYPE \
    X(DEFAULT) \
    X(BITMAP) \
    X(SDF) \

typedef enum {
#define X(type) gX_FONT_##type,
    gX_FONTTYPE
#undef X
} gX_FontType;
const gX_I8 *gX_FontType_String(gX_I32 type);

#define gX_BLENDMODES \
    X(ALPHA) \
    X(ADDITIVE) \
    X(MULTIPLIED) \
    X(ADD_COLORS) \
    X(SUBTRACT_COLORS) \
    X(ALPHA_PREMULTIPLY) \
    X(CUSTOM) \
    X(CUSTOM_SEPARATE) \

typedef enum {
#define X(blend) gX_BLEND_##blend,
    gX_BLENDMODES
#undef X
} gX_BlendMode;
const gX_I8 *gX_BlendMode_String(gX_I32 blend);

#define gX_CAMERAMODE \
    X(CUSTOM) \
    X(FREE) \
    X(ORBITAL) \
    X(FIRST_PERSON) \
    X(THIRD_PERSON) \

typedef enum {
#define X(mode) gX_CAMERA_##mode,
    gX_CAMERAMODE
#undef X
} gX_CameraMode;
const gX_I8 *gX_CameraMode_String(gX_I32 mode);

#define gX_CAMERAPROJECTION \
    X(PERSPECTIVE) \
    X(ORTHOGRAPHIC) \

typedef enum {
#define X(proj) gX_CAMERA_##proj,
    gX_CAMERAPROJECTION
#undef X
} gX_CameraProjection;
const gX_I8 *gX_CameraProjection_String(gX_I32 projection);

#define gX_GLCULLMODE \
    X(FRONT) \
    X(BACK) \

typedef enum {
#define X(mode) gX_GL_CULL_FACE_##mode,
    gX_GLCULLMODE
#undef X
} gX_glCullMode;
const gX_I8 *gX_glCullMode_String(gX_I32 mode);

#define gX_CONFIGFLAGS \
    X(VSYNC_HINT,              0x00000040) \
    X(FULLSCREEN_MODE,         0x00000002) \
    X(WINDOW_RESIZABLE,        0x00000004) \
    X(WINDOW_UNDECORATED,      0x00000008) \
    X(WINDOW_HIDDEN,           0x00000080) \
    X(WINDOW_MINIMIZED,        0x00000200) \
    X(WINDOW_MAXIMIZED,        0x00000400) \
    X(WINDOW_UNFOCUSED,        0x00000800) \
    X(WINDOW_TOPMOST,          0x00001000) \
    X(WINDOW_ALWAYS_RUN,       0x00000100) \
    X(WINDOW_TRANSPARENT,      0x00000010) \
    X(WINDOW_HIGHDPI,          0x00002000) \
    X(WINDOW_MOUSE_PASSTHROUGH,0x00004000) \
    X(BORDERLESS_WINDOWED_MODE,0x00008000) \
    X(MSAA_4X_HINT,            0x00000020) \
    X(INTERLACED_HINT,         0x00010000) \

typedef enum {
#define X(flag,value) gX_FLAG_##flag = value,
    gX_CONFIGFLAGS
#undef X
} gX_ConfigFlags;
const gX_I8 *gX_ConfigFlags_String(gX_I32 flag);

#ifdef __cplusplus
}
#endif

#endif //ENUM_H
