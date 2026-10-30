/**********************************************************************************************
*
*   [gX: src/wrappers/wrappers.h]
*
*   Copyright (c) 2025 lanttu
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

#ifndef GX_WRAPPERS_H
#define GX_WRAPPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/text.h"

#include "audio.h"
#include "camera.h"
#include "color.h"
#include "collision.h"
#include "convert.h"
#include "drawing.h"
#include "file.h"
#include "image.h"
#include "input.h"
#include "misc.h"
#include "../common/path.h"
#include "model.h"
#include "physics.h"
#include "shader.h"
#include "shape.h"
#include "text.h"
#include "texture.h"
#include "window.h"
#include "wrappers_stdlib.h"
#include "wrappers_gl.h"

// Backend lifecycle
gX_I0 gX_Backend_Load(gX_U32 width, gX_U32 height, const gX_I8 *title);
gX_I0 gX_Backend_Unload(gX_I0);
gX_I0 gX_BackendAudio_Load(gX_I0);
gX_I0 gX_BackendAudio_Unload(gX_I0);

// Window
gX_I1  gX_Window_ShouldClose(gX_I0);
gX_I0  gX_Window_SetTitle(const gX_I8 *title);

// Frame timing
gX_F64 gX_FrameTime_Get(gX_I0);
gX_I0  gX_TargetFPS_Set(gX_F32 fps);

// Backend log reroute
gX_I32 gX_Backend_RerouteLogtype(gX_I32 logType);

// Cursor
gX_I0 gX_Cursor_Enable(gX_I0);
gX_I0 gX_Cursor_Disable(gX_I0);

// Input wrappers
gX_I1  gX_Key_IsDown(gX_I32 key);
gX_I1  gX_Key_IsUp(gX_I32 key);
gX_I1  gX_Key_IsPressed(gX_I32 key);
gX_I1  gX_Key_IsReleased(gX_I32 key);
gX_F32v2 gX_Mouse_GetPos(gX_I0);
gX_F32v2 gX_Mouse_GetDelta(gX_I0);
gX_I1  gX_Mouse_IsDown(gX_I32 button);
gX_I1  gX_Mouse_IsPressed(gX_I32 button);
gX_F32 gX_Mouse_GetWheelMove(gX_I0);
gX_I1  gX_Gamepad_IsAvailable(gX_I32 gamepad);
gX_I8 *gX_Gamepad_GetName(gX_I32 gamepad);

// Sound/Music wrappers
gX_I0 gX_Sound_Play_Parameters(gX_Sound sound, gX_Sound_Parameters params);
gX_Sound gX_Sound_Load(const gX_I8 *path);
gX_Sound gX_Sound_LoadWave(gX_Wave wave);
gX_I0 gX_Sound_Unload(gX_Sound sound);
gX_I0 gX_Music_Update_Parameters(gX_Music music, gX_Sound_Parameters *params);
gX_I0 gX_Music_Unload(gX_Music music);
gX_Music gX_Music_Load(const gX_I8 *path);
gX_I0 gX_Music_Play(gX_Music music);
gX_I0 gX_Music_Pause(gX_Music music);
gX_I0 gX_Music_Stop(gX_Music music);

// Master volume
gX_I0 gX_MasterVolume_Set(gX_F32 volume);

// Text rendering
gX_I0 gX_RawString_Draw(const gX_I8 *string, gX_Font font,
                         gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation,
                         gX_F32 fontsize, gX_F32 spacing, gX_Color tint);
gX_I0 gX_Text_Draw(gX_Text text);

// Rectangle drawing
gX_I0 gX_Rectangle_Draw_Parameters(gX_Rectangle rect, gX_Rectangle_Parameters params);

// Exit key
extern gX_I32 exitKey;
gX_I0 gX_ExitKey_Set(gX_I32 key);

// Screenshot
gX_I0 gX_TakeScreenShot(const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //GX_WRAPPERS_H
