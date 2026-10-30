/**********************************************************************************************
* 
*   [wrappers/window.h]
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
//lanttu: original wrapper for raylib window/monitor functions
#ifndef GX_WRAPPERS_WINDOW_H
#define GX_WRAPPERS_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif
   
#include "../common/config.h"
#include "../common/types.h"
    
gX_I0 gX_Window_Init(gX_U32 width, gX_U32 height, const gX_I8 *name);
gX_I0 gX_Window_Close(          gX_I0);
gX_I1 gX_Window_ShouldClose(    gX_I0);

gX_I1 gX_Window_IsReady(        gX_I0);
gX_I1 gX_Window_IsFullScreen(   gX_I0);
gX_I1 gX_Window_IsHidden(       gX_I0);
gX_I1 gX_Window_IsMinimized(    gX_I0);
gX_I1 gX_Window_IsMaximized(    gX_I0);
gX_I1 gX_Window_IsFocused(      gX_I0);
gX_I1 gX_Window_IsResized(      gX_I0);

gX_I1 gX_Window_IsState(        gX_U32 flag);
gX_I0 gX_Window_SetState(       gX_U32 flags);
gX_I0 gX_Window_ClearState(     gX_U32 flags);

gX_I0 gX_Window_ToggleFullScreen(gX_I0);
gX_I0 gX_Window_ToggleBorderless(gX_I0);
gX_I0 gX_Window_Minimize(       gX_I0);
gX_I0 gX_Window_Maximize(       gX_I0);
gX_I0 gX_Window_Restore(        gX_I0);

gX_I0 gX_Window_SetIcon(        gX_Image image);
gX_I0 gX_Window_SetIcons(       gX_Image *image, gX_U32 count);
gX_I0 gX_Window_SetTitle(       const gX_I8 *title);
gX_I0 gX_Window_SetPosition(    gX_I32 x, gX_I32 y);
gX_I0 gX_Window_SetMonitor(     gX_I32 monitor);
gX_I0 gX_Window_SetMinSize(     gX_U32 width, gX_U32 height);
gX_I0 gX_Window_SetMaxSize(     gX_U32 width, gX_U32 height);
gX_I0 gX_Window_SetSize(        gX_U32 width, gX_U32 height);
gX_I0 gX_Window_SetOpacity(     gX_F32 opacity);
gX_I0 gX_Window_SetFocuced(     gX_I0);

gX_I0 *gX_Window_GetHandle(     gX_I0);
gX_F32v2 gX_Window_GetPosition( gX_I0);
gX_F32v2 gX_Window_GetScaleDPI( gX_I0);

gX_I32 gX_GetScreenWidth(       gX_I0);
gX_I32 gX_GetScreenHeight(      gX_I0);
gX_I32 gX_GetRenderWidth(       gX_I0);
gX_I32 gX_GetRenderHeight(      gX_I0);

gX_I32 gX_Monitor_GetCount(     gX_I0);
gX_I32 gX_Monitor_GetCurrent(   gX_I0);
gX_F32v2 gX_Monitor_GetPosition(gX_I32 monitor);
gX_I32 gX_Monitor_GetWidth(     gX_I32 monitor);
gX_I32 gX_Monitor_GetHeight(    gX_I32 monitor);
gX_I32 gX_Monitor_GetWidthPhysical(     gX_I32 monitor);
gX_I32 gX_Monitor_GetHeightPhysical(    gX_I32 monitor);
const gX_I8 *gX_Monitor_Name(     gX_I32 monitor);
    
#ifdef __cplusplus
}
#endif

#endif //GX_WRAPPERS_WINDOW_H
