/**********************************************************************************************
*
*   [gX: src/audio/pad_schema.h]
*   gX — PAD preset save/load
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


//ai: PAD preset — the engine's default patch: the three layer templates,
//   the long ADSR, the filter + its LFO, the chorus and the stereo width.
//   JSON, versioned. Saves refuse repo resource paths while
//   GX_NO_SETTINGS_SAVE is set (the shipped-preset harness guard precedent);
//   a failed load leaves the engine untouched.
#ifndef AUDIO_PAD_SCHEMA_H
#define AUDIO_PAD_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

gX_I32 gX_Pad_Preset_Save(const gX_I8 *path);
gX_I32 gX_Pad_Preset_Load(const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_PAD_SCHEMA_H
