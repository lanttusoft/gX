/**********************************************************************************************
*
*   [gX: src/common/polisher.c]
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


//ai: polisher system — native gX port of gameX's common/polisher (the contract
//   is: register a module-owned runtime variable, drag it live, report the
//   changed values as source lines, reset on demand).

#include "polisher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

gX_Polisher polisher = {0};

static gX_Polisher_Entry *find_entry(const gX_I8 *name) {
    for (gX_U32 i = 0; i < polisher.count; i++) {
        if (strcmp((const char *)polisher.entries[i].name,
                (const char *)name) == 0)
            return &polisher.entries[i];
    }
    return NULL;
}

static gX_Polisher_Entry *entry_for(const gX_I8 *name,
    const gX_I8 *category) {
    gX_Polisher_Entry *existing = find_entry(name);
    if (existing) return existing;
    if (polisher.count >= GX_POLISHER_MAX_ENTRIES) {
        gX_TraceLog(gX_LOG_WARNING,
            "POLISHER: entry table full, dropping '%s'", name);
        return NULL;
    }
    gX_Polisher_Entry *entry = &polisher.entries[polisher.count++];
    memset(entry, 0, sizeof(*entry));
    snprintf((char *)entry->name, sizeof(entry->name), "%s", name);
    snprintf((char *)entry->category, sizeof(entry->category), "%s",
        category ? category : "Other");
    return entry;
}

gX_I0 gX_Polisher_RegisterFloat(const gX_I8 *name, const gX_I8 *category,
    gX_F32 *value, gX_F32 min, gX_F32 max, gX_F32 step) {
    if (!name || !value) return;
    gX_Polisher_Entry *entry = entry_for(name, category);
    if (!entry) return;
    entry->type = GX_POLISHER_TYPE_F32;
    entry->f32Value = value;
    entry->f32Original = *value;
    entry->f32Min = min;
    entry->f32Max = max;
    entry->step = step != 0.0f ? step : 0.01f;
    entry->modified = 0;
}

gX_I0 gX_Polisher_RegisterInt(const gX_I8 *name, const gX_I8 *category,
    gX_I32 *value, gX_I32 min, gX_I32 max, gX_I32 step) {
    if (!name || !value) return;
    gX_Polisher_Entry *entry = entry_for(name, category);
    if (!entry) return;
    entry->type = GX_POLISHER_TYPE_I32;
    entry->i32Value = value;
    entry->i32Original = *value;
    entry->i32Min = min;
    entry->i32Max = max;
    entry->step = step != 0 ? (gX_F32)step : 1.0f;
    entry->modified = 0;
}

gX_Polisher_Entry *gX_Polisher_Find(const gX_I8 *name) {
    return name ? find_entry(name) : NULL;
}

gX_I0 gX_Polisher_ResetAll(gX_I0) {
    for (gX_U32 i = 0; i < polisher.count; i++) {
        gX_Polisher_Entry *entry = &polisher.entries[i];
        if (entry->type == GX_POLISHER_TYPE_F32) {
            if (entry->f32Value) *entry->f32Value = entry->f32Original;
        } else {
            if (entry->i32Value) *entry->i32Value = entry->i32Original;
        }
        entry->modified = 0;
    }
}

gX_I0 gX_Polisher_GenerateReport(gX_I8 *buf, gX_U32 bufsize) {
    if (!buf || bufsize == 0) return;
    buf[0] = '\0';
    gX_U32 used = 0;
    for (gX_U32 i = 0; i < polisher.count; i++) {
        gX_Polisher_Entry *entry = &polisher.entries[i];
        if (!entry->modified) continue;
        gX_I8 line[192];
        if (entry->type == GX_POLISHER_TYPE_F32) {
            snprintf((char *)line, sizeof(line), "%.4ff   // was %.4ff  %s\n",
                entry->f32Value ? (gX_F64)*entry->f32Value : 0.0,
                (gX_F64)entry->f32Original, entry->name);
        } else {
            snprintf((char *)line, sizeof(line), "%d   // was %d  %s\n",
                entry->i32Value ? (gX_I32)*entry->i32Value : 0,
                (gX_I32)entry->i32Original, entry->name);
        }
        size_t length = strlen((const char *)line);
        if (used + length + 1 > bufsize) break;
        memcpy(buf + used, line, length);
        used += (gX_U32)length;
    }
    buf[used] = '\0';
}

gX_I0 gX_Polisher_ApplyEnv(gX_I0) {
    const char *hook = getenv("GX_POLISHER");
    if (!hook || !hook[0]) return;
    gX_I8 work[512];
    snprintf((char *)work, sizeof(work), "%s", hook);
    char *save = NULL;
    for (char *pair = strtok_r((char *)work, ",", &save); pair;
         pair = strtok_r(NULL, ",", &save)) {
        char *equals = strchr(pair, '=');
        if (!equals) continue;
        *equals = '\0';
        const char *name = pair;
        const char *value = equals + 1;
        gX_Polisher_Entry *entry = gX_Polisher_Find((const gX_I8 *)name);
        if (!entry) {
            gX_TraceLog(gX_LOG_WARNING,
                "POLISHER: GX_POLISHER names unknown entry '%s'", name);
            continue;
        }
        if (entry->type == GX_POLISHER_TYPE_F32) {
            if (entry->f32Value) *entry->f32Value = (gX_F32)atof(value);
        } else {
            if (entry->i32Value) *entry->i32Value = (gX_I32)atoi(value);
        }
        entry->modified = 1;
        gX_TraceLog(gX_LOG_NOTICE, "POLISHER: %s=%s applied", name, value);
    }
}
