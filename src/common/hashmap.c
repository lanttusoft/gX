/**********************************************************************************************
*
*   [gX: src/common/hashmap.c]
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


#include <string.h>

#include "hashmap.h"
#include "alloc.h"
#include "log.h"

#define GX_HASHMAP_MIN_CAPACITY 16
#define GX_HASHMAP_DEFAULT_LOAD 0.75f

//opencode: gX_HashMap_ProbeDistance — calculates Robin Hood displacement
static gX_U32 gX_HashMap_ProbeDistance(gX_HashMap *map, gX_U32 index, gX_U32 desired) {
    if (index >= desired)
        return index - desired;
    return (map->capacity + index) - desired;
}

//opencode: gX_HashMap_Resize — rehashes all entries into new capacity
static gX_I0 gX_HashMap_Resize(gX_HashMap *map, gX_U32 newCapacity) {
    gX_HashMap_Entry *oldEntries = map->entries;
    gX_U32 oldCapacity = map->capacity;

    map->entries = (gX_HashMap_Entry*)gX_Calloc(newCapacity * sizeof(gX_HashMap_Entry));
    if (!(map->entries)) {
        map->entries = oldEntries;
        return;
    }
    map->capacity = newCapacity;
    map->count = 0;
    map->state.RESIZING = true;

    for (gX_U32 i = 0; i < oldCapacity; i++) {
        gX_HashMap_Entry *entry = &oldEntries[i];
        if (!entry->key || entry->key == (const gX_I0*)GX_HASHMAP_TOMBSTONE)
            continue;
        gX_HashMap_Set(map, entry->key, entry->keyLen, entry->value);
    }

    gX_Free((gX_I64*)oldEntries);
    map->state.RESIZING = false;
}

//opencode: gX_HashMap_Init — power-of-two capacity, FNV-1a ready
gX_I0 gX_HashMap_Init(gX_HashMap *map, gX_U32 initialCapacity) {
    if (initialCapacity < GX_HASHMAP_MIN_CAPACITY)
        initialCapacity = GX_HASHMAP_MIN_CAPACITY;

    gX_U32 cap = 1;
    while (cap < initialCapacity) cap <<= 1;

    map->capacity = cap;
    map->count = 0;
    map->loadFactor = GX_HASHMAP_DEFAULT_LOAD;
    map->state.INITIALIZED = true;
    map->state.RESIZING = false;

    map->entries = (gX_HashMap_Entry*)gX_Calloc(cap * sizeof(gX_HashMap_Entry));
    if (!(map->entries)) {
        map->state.INITIALIZED = false;
        return;
    }
}

//opencode: gX_HashMap_DeInit — frees entries array
gX_I0 gX_HashMap_DeInit(gX_HashMap *map) {
    if (map->entries) {
        gX_Free((gX_I64*)map->entries);
        map->entries = 0;
    }
    map->capacity = 0;
    map->count = 0;
    map->state.INITIALIZED = false;
}

//opencode: gX_HashMap_Hash — FNV-1a 64-bit hash
gX_U64 gX_HashMap_Hash(const gX_I0 *key, gX_U32 keyLen) {
    gX_U64 hash = 0xcbf29ce484222325ULL;
    const gX_U8 *bytes = (const gX_U8*)key;
    for (gX_U32 i = 0; i < keyLen; i++) {
        hash ^= (gX_U64)bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

//opencode: gX_HashMap_Set — Robin Hood probing, auto-resize at load factor 0.75
gX_I0 *gX_HashMap_Set(gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen, gX_I0 *value) {
    if (!map->state.INITIALIZED) return 0;

    if ((gX_F32)(map->count + 1) >= (gX_F32)map->capacity * map->loadFactor) {
        gX_U32 newCap = map->capacity * 2;
        if (newCap < map->capacity) newCap = map->capacity;
        gX_HashMap_Resize(map, newCap);
    }

    gX_U64 hash = gX_HashMap_Hash(key, keyLen);
    gX_U32 mask = map->capacity - 1;
    gX_U32 index = (gX_U32)(hash & (gX_U64)mask);

    // Robin Hood hashing: track the entry with the best probe distance
    gX_HashMap_Entry newEntry;
    newEntry.hash = hash;
    newEntry.key = key;
    newEntry.keyLen = keyLen;
    newEntry.value = value;
    gX_U32 dist = 0;

    while (true) {
        gX_HashMap_Entry *entry = &map->entries[index];

        if (!entry->key) {
            *entry = newEntry;
            map->count++;
            return entry->value;
        }

        if (entry->key == (const gX_I0*)GX_HASHMAP_TOMBSTONE) {
            *entry = newEntry;
            map->count++;
            return entry->value;
        }

        if (entry->hash == hash && entry->keyLen == keyLen &&
            memcmp(entry->key, key, keyLen) == 0) {
            entry->value = value;
            return entry->value;
        }

        gX_U32 existingDist = gX_HashMap_ProbeDistance(map, index,
            (gX_U32)(entry->hash & (gX_U64)mask));

        if (existingDist < dist) {
            gX_HashMap_Entry swap = *entry;
            *entry = newEntry;
            newEntry = swap;
            dist = existingDist;
        }

        index = (index + 1) & mask;
        dist++;
    }
}

//opencode: gX_HashMap_Get — Robin Hood lookup, O(1) average
gX_I0 *gX_HashMap_Get(gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen) {
    if (!map->state.INITIALIZED) return 0;
    if (map->count == 0) return 0;

    gX_U64 hash = gX_HashMap_Hash(key, keyLen);
    gX_U32 mask = map->capacity - 1;
    gX_U32 index = (gX_U32)(hash & (gX_U64)mask);

    for (gX_U32 dist = 0; dist <= map->capacity; dist++) {
        gX_HashMap_Entry *entry = &map->entries[index];

        if (!entry->key) return 0;

        if (entry->key != (const gX_I0*)GX_HASHMAP_TOMBSTONE &&
            entry->hash == hash &&
            entry->keyLen == keyLen &&
            memcmp(entry->key, key, keyLen) == 0) {
            return entry->value;
        }

        index = (index + 1) & mask;
    }

    return 0;
}

//opencode: gX_HashMap_Remove — tombstone deletion
gX_I0 *gX_HashMap_Remove(gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen) {
    if (!map->state.INITIALIZED) return 0;
    if (map->count == 0) return 0;

    gX_U64 hash = gX_HashMap_Hash(key, keyLen);
    gX_U32 mask = map->capacity - 1;
    gX_U32 index = (gX_U32)(hash & (gX_U64)mask);

    for (gX_U32 dist = 0; dist <= map->capacity; dist++) {
        gX_HashMap_Entry *entry = &map->entries[index];

        if (!entry->key) return 0;

        if (entry->key != (const gX_I0*)GX_HASHMAP_TOMBSTONE &&
            entry->hash == hash &&
            entry->keyLen == keyLen &&
            memcmp(entry->key, key, keyLen) == 0) {
            gX_I0 *value = entry->value;
            entry->key = (const gX_I0*)GX_HASHMAP_TOMBSTONE;
            entry->value = 0;
            map->count--;
            return value;
        }

        index = (index + 1) & mask;
    }

    return 0;
}
