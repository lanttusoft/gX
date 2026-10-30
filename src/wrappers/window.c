/**********************************************************************************************
* 
*   [wrappers/window.c]
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
*     2. Altered source versions must be plainly marked as follows: and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib window/monitor functions
#include "wrappers_header.h"
#include "window.h"

//opencode: GX no-op init/close (real init lives in Backend_Load)
gX_I0 gX_Window_Init(gX_U32 width, gX_U32 height, const gX_I8 *name) {
    (void)width; (void)height; (void)name;
}

gX_I0 gX_Window_Close(gX_I0) {}

//opencode: live window queries (dock hardening resize sync reads these;
//   GetScreenWidth/Height track user/WM resizes, unlike cached maindata).
gX_I32 gX_GetScreenWidth(gX_I0) { return GetScreenWidth(); }

gX_I32 gX_GetScreenHeight(gX_I0) { return GetScreenHeight(); }

gX_I32 gX_GetRenderWidth(gX_I0) { return GetRenderWidth(); }

gX_I32 gX_GetRenderHeight(gX_I0) { return GetRenderHeight(); }

//opencode: N64 display queries via libdragon

gX_I1 gX_Window_ShouldClose(gX_I0) {
    return WindowShouldClose();
}

gX_I1 gX_Window_IsReady(gX_I0) {
    return IsWindowReady();
}

gX_I1 gX_Window_IsFullScreen(gX_I0) {
    return IsWindowFullscreen();
}

gX_I1 gX_Window_IsHidden(gX_I0) {
    return IsWindowHidden();
}

gX_I1 gX_Window_IsMinimized(gX_I0) {
    return IsWindowMinimized();
}

gX_I1 gX_Window_IsMaximized(gX_I0) {
    return IsWindowMaximized();
}

gX_I1 gX_Window_IsFocused(gX_I0) {
    return IsWindowFocused();
}

gX_I1 gX_Window_IsResized(gX_I0) {
    return IsWindowResized();
}

gX_I1 gX_Window_IsState(gX_U32 flag) {
    return IsWindowState(flag);
}

gX_I0 gX_Window_SetState(gX_U32 flags) {
    SetWindowState(flags);
}

gX_I0 gX_Window_ClearState(gX_U32 flags) {
    ClearWindowState(flags);
}

gX_I0 gX_Window_ToggleFullScreen(gX_I0) {
    ToggleFullscreen();
}

gX_I0 gX_Window_ToggleBorderless(gX_I0) {
    ToggleBorderlessWindowed();
}

gX_I0 gX_Window_Minimize(gX_I0) {
    MinimizeWindow();
}

gX_I0 gX_Window_Maximize(gX_I0) {
    MaximizeWindow();
}

gX_I0 gX_Window_Restore(gX_I0) {
    RestoreWindow();
}

gX_I0 gX_Window_SetTitle(const gX_I8 *title) {
    SetWindowTitle(title);
}

gX_I0 gX_Window_SetPosition(gX_I32 x, gX_I32 y) {
    SetWindowPosition(x,y);
}


gX_I0 gX_Window_SetMonitor(gX_I32 monitor) {
    SetWindowMonitor(monitor);
}

//opencode: remaining window stubs for GX (functions declared in window.h but not in gameX window.c)
gX_I0 gX_Window_SetIcon(gX_Image image) { (void)image; }
gX_I0 gX_Window_SetIcons(gX_Image *image, gX_U32 count) { (void)image; (void)count; }
gX_I0 gX_Window_SetMinSize(gX_U32 width, gX_U32 height) { (void)width; (void)height; }
gX_I0 gX_Window_SetMaxSize(gX_U32 width, gX_U32 height) { (void)width; (void)height; }
//opencode: v0.6.6 — real impls (were no-op stubs) for monitor fit.
gX_I0 gX_Window_SetSize(gX_U32 width, gX_U32 height) { SetWindowSize((int)width, (int)height); }
gX_I0 gX_Window_SetOpacity(gX_F32 opacity) { (void)opacity; }
gX_I0 gX_Window_SetFocuced(gX_I0) {}
gX_I0 *gX_Window_GetHandle(gX_I0) { return 0; }
gX_F32v2 gX_Window_GetPosition(gX_I0) { return (gX_F32v2){0,0}; }
gX_F32v2 gX_Window_GetScaleDPI(gX_I0) { return (gX_F32v2){0,0}; }
gX_I32 gX_Monitor_GetCount(gX_I0) { return 1; }
gX_I32 gX_Monitor_GetCurrent(gX_I0) { return (gX_I32)GetCurrentMonitor(); }
gX_F32v2 gX_Monitor_GetPosition(gX_I32 monitor) { (void)monitor; return (gX_F32v2){0,0}; }
gX_I32 gX_Monitor_GetWidth(gX_I32 monitor) { return (gX_I32)GetMonitorWidth((int)monitor); }
gX_I32 gX_Monitor_GetHeight(gX_I32 monitor) { return (gX_I32)GetMonitorHeight((int)monitor); }
gX_I32 gX_Monitor_GetWidthPhysical(gX_I32 monitor) { (void)monitor; return 0; }
gX_I32 gX_Monitor_GetHeightPhysical(gX_I32 monitor) { (void)monitor; return 0; }
const gX_I8 *gX_Monitor_Name(gX_I32 monitor) { (void)monitor; return "GX Backend"; }
