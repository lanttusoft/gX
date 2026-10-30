/**********************************************************************************************
*
*   [gX: src/audio/opx_schema.h]
*   gX — OPX preset save/load
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


//ai: OPX preset — the engine's default patch: the eight operator templates
//   (the values a new note copies) plus the default algorithm and master
//   volume. JSON, versioned. Saves refuse repo resource paths while
//   GX_NO_SETTINGS_SAVE is set (the shipped-preset harness guard precedent);
//   a failed load leaves the engine untouched.
#ifndef AUDIO_OPX_SCHEMA_H
#define AUDIO_OPX_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

gX_I32 gX_OPX_Preset_Save(const gX_I8 *path);
gX_I32 gX_OPX_Preset_Load(const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_OPX_SCHEMA_H
