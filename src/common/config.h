/**********************************************************************************************
*
*   [gX: src/common/config.h]
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

#ifndef GX_CONFIG_H
#define GX_CONFIG_H

//opencode: backend defined by cmake (GX_BACKEND=raylib, glfw, or gx)
// gX standalone: default to GX backend (no external dependencies)
// When GAME_BACKEND_RAYLIB or GAME_BACKEND_GLFW is active, also set GX
// to block gameX infrastructure code that gX doesn't have.

//#define LOG_TYPE_LEVEL gX_LOG_INFO
#define LOG_TYPE_LEVEL gX_LOG_DEBUG
//#define LOG_TYPE_LEVEL gX_LOG_TRACE
//#define LOG_TYPE_LEVEL gX_LOG_ALL

#define gX_ENABLE_ARCADE        1
#define gX_ENABLE_AUDIO         1
#define gX_ENABLE_CAMERA        1
#define gX_ENABLE_EDITOR        1
//opencode: kernel cluster — RENDERER/SCENE ifndef-guarded so cmake
//   -DGX_ENABLE_RENDERER/SCENE=0 wins (kernel-only build drops the
//   renderer+scene+game sources atomically). GAME stays default 0
//   (game-content headers absent in both configs; not a cmake option).
#ifndef gX_ENABLE_GAME
#define gX_ENABLE_GAME          0
#endif
#define gX_ENABLE_GUI           1
#define gX_ENABLE_GXMATH        1
#define gX_ENABLE_INPUT         1
#define gX_ENABLE_LANGUAGE      1
//ai: physics module — shared simulation world (wrapper stubs when ODE is off)
#ifndef gX_ENABLE_PHYSICS
#define gX_ENABLE_PHYSICS       1
#endif
#ifndef gX_ENABLE_RENDERER
#define gX_ENABLE_RENDERER      1
#endif
#define gX_ENABLE_RESOURCE      1
#ifndef gX_ENABLE_SCENE
#define gX_ENABLE_SCENE         1
#endif
#define gX_ENABLE_SCRIPTING     1
#define gX_ENABLE_SETTINGS      1

#define gX_scale_mult 1.5f
//opencode: parenthesized — the bare product evaluated context-dependently
//   (as (SW/1024)*mult inside resScale divisions); resScale sites now spell
//   that legacy math explicitly so values never hinge on macro shape again.
#define MAINDATA_DEFAULT_SCREENWIDTH  (1024*gX_scale_mult)
#define MAINDATA_DEFAULT_SCREENHEIGHT (768*gX_scale_mult)
#define MAINDATA_DEFAULT_WINDOWWIDTH  MAINDATA_DEFAULT_SCREENWIDTH
#define MAINDATA_DEFAULT_WINDOWHEIGHT MAINDATA_DEFAULT_SCREENHEIGHT

#define DEFAULT_NAMEBFR_SIZE 128
#define DEFAULT_DESCBFR_SIZE 256

//opencode: v0.2.0 — shared save path (binding.c uses the same under
//   gX_ENABLE_GAME==0; the game-content save.h defines its own when enabled)
#ifndef GX_SAVE_PATH
    #define GX_SAVE_PATH "save/gx.sav"
#endif

#endif //GX_CONFIG_H
