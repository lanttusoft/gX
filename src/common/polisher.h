/**********************************************************************************************
*
*   [gX: src/common/polisher.h]
*   gX — a game engine
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


#ifndef GX_COMMON_POLISHER_H
#define GX_COMMON_POLISHER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

//ai: polisher system — runtime tweaking of "magic number" defaults. Modules
//   register pointers to module-owned runtime variables (initialized from the
//   gX_BG_*_DEFVAL-style constants); the editor panel drags them, the report
//   prints copy-paste lines to fold values back into the source #define, and
//   GX_POLISHER="NAME=VALUE,..." applies scripted tunings at registration
//   time (gates/CI).
//ai: 32 entries is ample (the background registers 10); the table is a global
//   so the BSS budget stays lean.
#define GX_POLISHER_MAX_ENTRIES 32
#define GX_POLISHER_NAME_SIZE   48   // longest shipped name is 25 chars
#define GX_POLISHER_CAT_SIZE    32   // longest shipped category is 10 chars

typedef enum gX_Polisher_Type {
    GX_POLISHER_TYPE_F32,
    GX_POLISHER_TYPE_I32
} gX_Polisher_Type;

//ai: entry flags
#define GX_POLISHER_UNBOUNDED  (1u << 0)  // no min/max limits (unbounded drag)
#define GX_POLISHER_LOGSCALE   (1u << 1)  // logarithmic slider (drag log10)

typedef struct gX_Polisher_Entry {
    gX_I8  name[GX_POLISHER_NAME_SIZE];    // e.g. "gX_BG_SCALE_DEFVAL"
    gX_I8  category[GX_POLISHER_CAT_SIZE]; // e.g. "Background"
    gX_Polisher_Type type;
    gX_U32 flags;                          // GX_POLISHER_* flags

    //float variant
    gX_F32 *f32Value;
    gX_F32  f32Original;
    gX_F32  f32Min;
    gX_F32  f32Max;

    //int variant
    gX_I32 *i32Value;
    gX_I32  i32Original;
    gX_I32  i32Min;
    gX_I32  i32Max;

    gX_F32 step;      // drag speed
    gX_I1  modified;  // changed from the original since registration
} gX_Polisher_Entry;

typedef struct gX_Polisher {
    gX_Polisher_Entry entries[GX_POLISHER_MAX_ENTRIES];
    gX_U32 count;
} gX_Polisher;

extern gX_Polisher polisher;

//ai: register a float/int magic number. Idempotent per name (re-registration
//   refreshes the pointer/bounds instead of adding a duplicate entry).
gX_I0 gX_Polisher_RegisterFloat(const gX_I8 *name, const gX_I8 *category,
    gX_F32 *value, gX_F32 min, gX_F32 max, gX_F32 step);
gX_I0 gX_Polisher_RegisterInt(const gX_I8 *name, const gX_I8 *category,
    gX_I32 *value, gX_I32 min, gX_I32 max, gX_I32 step);

//ai: copy-paste report of the modified entries:
//   "NAME newValue   // was originalValue"
gX_I0 gX_Polisher_GenerateReport(gX_I8 *buf, gX_U32 bufsize);

//ai: reset every entry to its original value.
gX_I0 gX_Polisher_ResetAll(gX_I0);

//ai: look up an entry by name (NULL when missing).
gX_Polisher_Entry *gX_Polisher_Find(const gX_I8 *name);

//ai: apply GX_POLISHER="NAME=VALUE,NAME2=VALUE2" to registered entries.
gX_I0 gX_Polisher_ApplyEnv(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_COMMON_POLISHER_H
