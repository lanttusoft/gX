/**********************************************************************************************
*
*   [gX: src/renderer/gfx/temporal_history.h]
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


#ifndef GX_RENDERER_GFX_TEMPORAL_HISTORY_H
#define GX_RENDERER_GFX_TEMPORAL_HISTORY_H

#include "texture.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: explicit temporal-history state for ping-pong accumulation passes.
typedef struct gX_RendererGFX_TemporalHistory {
    gX_I32 frameIndex;
    gX_I1  historyValid;
    gX_I1  clearPending;
    gX_I1  passActive;
    gX_I1  parity;
} gX_RendererGFX_TemporalHistory;

typedef struct gX_RendererGFX_TemporalHistoryTargets {
    gX_RendererGFX_RenderTexture *read;
    gX_RendererGFX_RenderTexture *write;
} gX_RendererGFX_TemporalHistoryTargets;

//opencode: initialize temporal state; the first pass reads target A and writes B.
gX_I0 gX_RendererGFX_TemporalHistory_Init(
    gX_RendererGFX_TemporalHistory *history);

//opencode: invalidate history, reset ping-pong parity, and restart the sample index.
gX_I0 gX_RendererGFX_TemporalHistory_Reset(
    gX_RendererGFX_TemporalHistory *history);

//opencode: report whether both targets need a clear before the next pass.
gX_I1 gX_RendererGFX_TemporalHistory_NeedsClear(
    const gX_RendererGFX_TemporalHistory *history);

//opencode: mark both targets initialized after the caller clears them.
gX_I0 gX_RendererGFX_TemporalHistory_MarkCleared(
    gX_RendererGFX_TemporalHistory *history);

//opencode: expose the current sample index without advancing it.
gX_I32 gX_RendererGFX_TemporalHistory_FrameIndex(
    const gX_RendererGFX_TemporalHistory *history);

//ai: select loaded targets with distinct nonzero FBO/color IDs; wrapper metadata must be coherent.
// Rejection leaves both state and output targets unchanged; parity changes only on commit.
gX_I1 gX_RendererGFX_TemporalHistory_BeginPass(
    gX_RendererGFX_TemporalHistory *history,
    gX_RendererGFX_RenderTexture *targetA,
    gX_RendererGFX_RenderTexture *targetB,
    gX_RendererGFX_TemporalHistoryTargets *targets);

//ai: commit a completed pass; index wraps from signed maximum to zero, history becomes valid and parity flips once.
gX_I0 gX_RendererGFX_TemporalHistory_CommitPass(
    gX_RendererGFX_TemporalHistory *history);

//opencode: cancel an in-flight pass without changing parity or frame index.
gX_I0 gX_RendererGFX_TemporalHistory_AbortPass(
    gX_RendererGFX_TemporalHistory *history);

//opencode: return the target containing the latest committed history.
gX_RendererGFX_RenderTexture *gX_RendererGFX_TemporalHistory_CurrentTarget(
    const gX_RendererGFX_TemporalHistory *history,
    gX_RendererGFX_RenderTexture *targetA,
    gX_RendererGFX_RenderTexture *targetB);

#ifdef __cplusplus
}
#endif

#endif //GX_RENDERER_GFX_TEMPORAL_HISTORY_H
