/**********************************************************************************************
*
*   [gX: src/renderer/gfx/profile.h]
*
*   Copyright (c) 2026 lanttu, AI contributors
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


//ai: opt-in frame-matched GPU profiling; query ownership is private to each renderer instance.
#ifndef GX_GFX_PROFILE_H
#define GX_GFX_PROFILE_H

#include "../../common/types.h"

#define GX_GFX_PROFILE_SHADOW 0
#define GX_GFX_PROFILE_REFLECTIONS 1
#define GX_GFX_PROFILE_OPAQUE 2
#define GX_GFX_PROFILE_PRE_SSR 3
#define GX_GFX_PROFILE_SSR_INPUTS 4
#define GX_GFX_PROFILE_SSR 5
#define GX_GFX_PROFILE_PRE_BLOOM 6
#define GX_GFX_PROFILE_BLOOM 7
#define GX_GFX_PROFILE_POST 8
#define GX_GFX_PROFILE_TOTAL 9
#define GX_GFX_PROFILE_STAGES 10
#define GX_GFX_PROFILE_RING 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gX_RendererGFX_Profile gX_RendererGFX_Profile;

gX_RendererGFX_Profile *gX_RendererGFX_Profile_Init(const gX_I8 *Path);
gX_I0 gX_RendererGFX_Profile_Begin(gX_RendererGFX_Profile *Profile, gX_U64 Frame);
gX_I0 gX_RendererGFX_Profile_StageBegin(gX_RendererGFX_Profile *Profile, gX_U32 Stage);
gX_I0 gX_RendererGFX_Profile_StageEnd(gX_RendererGFX_Profile *Profile, gX_U32 Stage);
gX_I0 gX_RendererGFX_Profile_End(gX_RendererGFX_Profile *Profile);
gX_I0 gX_RendererGFX_Profile_Deinit(gX_RendererGFX_Profile *Profile, gX_U64 ReadbackFrame);

#ifdef __cplusplus
}
#endif

#endif
