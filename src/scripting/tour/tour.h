/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: tour API modules — gX-native remakes of gameX's reel-scripting surface
//   (all landed: camera/gfx/audio/background/synth/textwriter/overlay/
//   raymarch/pp). Each module registers its script bindings and any
//   per-frame hooks it needs.
#ifndef GX_SCRIPTING_TOUR_H
#define GX_SCRIPTING_TOUR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../scripting.h"

gX_I32 gX_TourCamera_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourGfx_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourAudio_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourBackground_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourSynth_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourOPX_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourPad_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourTextWriter_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourOverlay_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourRaymarch_Register(struct asIScriptEngine *engine);
gX_I32 gX_TourPostProcess_Register(struct asIScriptEngine *engine);

#ifdef __cplusplus
}
#endif

#endif //GX_SCRIPTING_TOUR_H
