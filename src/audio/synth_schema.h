/**********************************************************************************************
*
*   [gX: src/audio/synth_schema.h]
*   gX — a game engine
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


//opencode: schema for Synth engine preset save/load
#ifndef SYNTH_SCHEMA_H
#define SYNTH_SCHEMA_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Full synth preset: saves global state + patch bank + sequencer patterns
gX_I32 gX_Synth_Preset_Save(const gX_I8 *path);
gX_I32 gX_Synth_Preset_Load(const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //SYNTH_SCHEMA2_H
