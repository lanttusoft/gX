/**********************************************************************************************
*
*   [gX: src/scene/battle_logic.c]
*   Headless battle primitives — turn queue and tactical grid
*
*   Copyright (c) 2026 lanttu, gX contributors
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

//ai: implement battle queue and grid rules without scene, renderer, or game-state dependencies.
#include "battle_logic.h"

#include <math.h>
#include <string.h>

//ai: reject malformed public ring state before indexing its fixed buffers.
static gX_I1 gX_SceneBattle_TurnQueue_Valid(const gX_SceneBattle_TurnQueue *queue) {
    if (!queue || queue->head >= GX_BATTLE_TURN_QUEUE_MAX
        || queue->tail >= GX_BATTLE_TURN_QUEUE_MAX
        || queue->count > GX_BATTLE_TURN_QUEUE_MAX) return false;
    return queue->tail == (gX_U8)((queue->head + queue->count) % GX_BATTLE_TURN_QUEUE_MAX);
}

//ai: validate finite grid geometry before any cell indexing or conversion.
static gX_I1 gX_SceneBattle_Grid_Valid(const gX_SceneBattle_Grid *grid) {
    if (!grid || grid->cols <= 0 || grid->cols > GX_BATTLE_GRID_COLS_MAX
        || grid->rows <= 0 || grid->rows > GX_BATTLE_GRID_ROWS_MAX
        || !isfinite(grid->cellSize) || grid->cellSize <= 0.0f
        || !isfinite(grid->origin.x) || !isfinite(grid->origin.y)
        || !isfinite(grid->origin.z)) return false;
    return isfinite(grid->origin.x + (gX_F32)grid->cols * grid->cellSize)
        && isfinite(grid->origin.z + (gX_F32)grid->rows * grid->cellSize);
}

//ai: centralize row/column bounds checks for all grid cell operations.
static gX_I1 gX_SceneBattle_Grid_InBounds(const gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row) {
    return gX_SceneBattle_Grid_Valid(grid) && col >= 0 && col < grid->cols
        && row >= 0 && row < grid->rows;
}

//ai: initialize to the same zeroed ring state used by the reference battle.
gX_I0 gX_SceneBattle_TurnQueue_Init(gX_SceneBattle_TurnQueue *queue) {
    if (!queue) return;
    memset(queue, 0, sizeof(*queue));
}

//ai: enqueue FIFO actor references, allowing duplicates as in the reference contract.
gX_I1 gX_SceneBattle_TurnQueue_Enqueue(gX_SceneBattle_TurnQueue *queue,
    gX_I8 type, gX_U8 index) {
    if (!gX_SceneBattle_TurnQueue_Valid(queue) || queue->count == GX_BATTLE_TURN_QUEUE_MAX
        || (type != GX_BATTLE_ACTOR_PLAYER && type != GX_BATTLE_ACTOR_ENEMY)) return false;
    queue->type[queue->tail] = type;
    queue->index[queue->tail] = index;
    queue->tail = (gX_U8)((queue->tail + 1) % GX_BATTLE_TURN_QUEUE_MAX);
    queue->count++;
    return true;
}

//ai: dequeue only when an entry exists; preserve the ring invariant after wraparound.
gX_I1 gX_SceneBattle_TurnQueue_Dequeue(gX_SceneBattle_TurnQueue *queue) {
    if (!gX_SceneBattle_TurnQueue_Valid(queue) || queue->count == 0) return false;
    queue->head = (gX_U8)((queue->head + 1) % GX_BATTLE_TURN_QUEUE_MAX);
    queue->count--;
    if (queue->count == 0) queue->tail = queue->head;
    return true;
}

//ai: return the oldest actor without consuming it and clear outputs on failure.
gX_I1 gX_SceneBattle_TurnQueue_Peek(const gX_SceneBattle_TurnQueue *queue,
    gX_I8 *outType, gX_U8 *outIndex) {
    if (outType) *outType = -1;
    if (outIndex) *outIndex = 0;
    if (!outType || !outIndex || !gX_SceneBattle_TurnQueue_Valid(queue)
        || queue->count == 0) return false;
    *outType = queue->type[queue->head];
    *outIndex = queue->index[queue->head];
    return true;
}

//ai: scan live ring entries only, excluding stale slots beyond the queue count.
gX_I1 gX_SceneBattle_TurnQueue_IsInQueue(const gX_SceneBattle_TurnQueue *queue,
    gX_I8 type, gX_U8 index) {
    if (!gX_SceneBattle_TurnQueue_Valid(queue)) return false;
    for (gX_U32 i = 0; i < queue->count; i++) {
        gX_U32 pos = (queue->head + i) % GX_BATTLE_TURN_QUEUE_MAX;
        if (queue->type[pos] == type && queue->index[pos] == index) return true;
    }
    return false;
}

//ai: build row-major cell metadata around a bottom-left world-space origin.
gX_I1 gX_SceneBattle_Grid_Init(gX_SceneBattle_Grid *grid, gX_I32 cols,
    gX_I32 rows, gX_F32 cellSize, gX_F32v3 origin) {
    if (!grid || cols <= 0 || cols > GX_BATTLE_GRID_COLS_MAX
        || rows <= 0 || rows > GX_BATTLE_GRID_ROWS_MAX
        || !isfinite(cellSize) || cellSize <= 0.0f
        || !isfinite(origin.x) || !isfinite(origin.y) || !isfinite(origin.z)
        || !isfinite(origin.x + (gX_F32)cols * cellSize)
        || !isfinite(origin.z + (gX_F32)rows * cellSize)) return false;
    memset(grid, 0, sizeof(*grid));
    grid->cols = cols;
    grid->rows = rows;
    grid->cellSize = cellSize;
    grid->origin = origin;
    for (gX_I32 row = 0; row < rows; row++) {
        for (gX_I32 col = 0; col < cols; col++) {
            gX_U32 idx = (gX_U32)(row * cols + col);
            grid->cells[idx].col = col;
            grid->cells[idx].row = row;
            grid->cells[idx].worldPos = (gX_F32v3){
                origin.x + ((gX_F32)col + 0.5f) * cellSize,
                origin.y,
                origin.z + ((gX_F32)row + 0.5f) * cellSize};
        }
    }
    return true;
}

//ai: resolve a cell index to the center position precomputed during initialization.
gX_I1 gX_SceneBattle_Grid_CellToWorld(const gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row, gX_F32v3 *outWorldPos) {
    if (!outWorldPos || !gX_SceneBattle_Grid_InBounds(grid, col, row)) return false;
    *outWorldPos = grid->cells[row * grid->cols + col].worldPos;
    return true;
}

//ai: map the half-open X/Z footprint to cells using floor semantics at negative coordinates.
gX_I1 gX_SceneBattle_Grid_WorldToCell(const gX_SceneBattle_Grid *grid,
    gX_F32v3 worldPos, gX_I32 *outCol, gX_I32 *outRow) {
    if (outCol) *outCol = -1;
    if (outRow) *outRow = -1;
    if (!outCol || !outRow || !gX_SceneBattle_Grid_Valid(grid)
        || !isfinite(worldPos.x) || !isfinite(worldPos.z)) return false;
    gX_F32 localX = (worldPos.x - grid->origin.x) / grid->cellSize;
    gX_F32 localZ = (worldPos.z - grid->origin.z) / grid->cellSize;
    if (!isfinite(localX) || !isfinite(localZ) || localX < 0.0f
        || localX >= (gX_F32)grid->cols || localZ < 0.0f
        || localZ >= (gX_F32)grid->rows) return false;
    *outCol = (gX_I32)floorf(localX);
    *outRow = (gX_I32)floorf(localZ);
    return true;
}

//ai: update occupancy only for cells represented by the initialized grid.
gX_I1 gX_SceneBattle_Grid_CellSetOccupied(gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row, gX_I1 occupied) {
    if (!gX_SceneBattle_Grid_InBounds(grid, col, row)) return false;
    grid->cells[row * grid->cols + col].occupied = occupied;
    return true;
}

//ai: invalid cell coordinates are not occupied.
gX_I1 gX_SceneBattle_Grid_CellIsOccupied(const gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row) {
    if (!gX_SceneBattle_Grid_InBounds(grid, col, row)) return false;
    return grid->cells[row * grid->cols + col].occupied;
}
