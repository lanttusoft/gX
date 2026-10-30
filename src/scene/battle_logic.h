/**********************************************************************************************
*
*   [gX: src/scene/battle_logic.h]
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

#ifndef GX_SCENE_BATTLE_LOGIC_H
#define GX_SCENE_BATTLE_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//ai: bound the independent turn and grid state to the same reference capacities.
#define GX_BATTLE_TURN_QUEUE_MAX 16
#define GX_BATTLE_GRID_COLS_MAX 9
#define GX_BATTLE_GRID_ROWS_MAX 9
#define GX_BATTLE_ACTOR_PLAYER 0
#define GX_BATTLE_ACTOR_ENEMY 1

typedef struct gX_SceneBattle_TurnQueue {
    gX_I8 type[GX_BATTLE_TURN_QUEUE_MAX];
    gX_U8 index[GX_BATTLE_TURN_QUEUE_MAX];
    gX_U8 head;
    gX_U8 tail;
    gX_U8 count;
} gX_SceneBattle_TurnQueue;

typedef struct gX_SceneBattle_GridCell {
    gX_I32 col;
    gX_I32 row;
    gX_F32v3 worldPos;
    gX_I1 occupied;
} gX_SceneBattle_GridCell;

typedef struct gX_SceneBattle_Grid {
    gX_I32 cols;
    gX_I32 rows;
    gX_F32 cellSize;
    gX_F32v3 origin;
    gX_SceneBattle_GridCell cells[GX_BATTLE_GRID_COLS_MAX * GX_BATTLE_GRID_ROWS_MAX];
} gX_SceneBattle_Grid;

//ai: expose the small, bounded battle data types as pure CPU APIs.
gX_I0 gX_SceneBattle_TurnQueue_Init(gX_SceneBattle_TurnQueue *queue);
gX_I1 gX_SceneBattle_TurnQueue_Enqueue(gX_SceneBattle_TurnQueue *queue,
    gX_I8 type, gX_U8 index);
gX_I1 gX_SceneBattle_TurnQueue_Dequeue(gX_SceneBattle_TurnQueue *queue);
gX_I1 gX_SceneBattle_TurnQueue_Peek(const gX_SceneBattle_TurnQueue *queue,
    gX_I8 *outType, gX_U8 *outIndex);
gX_I1 gX_SceneBattle_TurnQueue_IsInQueue(const gX_SceneBattle_TurnQueue *queue,
    gX_I8 type, gX_U8 index);

gX_I1 gX_SceneBattle_Grid_Init(gX_SceneBattle_Grid *grid, gX_I32 cols,
    gX_I32 rows, gX_F32 cellSize, gX_F32v3 origin);
gX_I1 gX_SceneBattle_Grid_CellToWorld(const gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row, gX_F32v3 *outWorldPos);
gX_I1 gX_SceneBattle_Grid_WorldToCell(const gX_SceneBattle_Grid *grid,
    gX_F32v3 worldPos, gX_I32 *outCol, gX_I32 *outRow);
gX_I1 gX_SceneBattle_Grid_CellSetOccupied(gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row, gX_I1 occupied);
gX_I1 gX_SceneBattle_Grid_CellIsOccupied(const gX_SceneBattle_Grid *grid,
    gX_I32 col, gX_I32 row);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_BATTLE_LOGIC_H
