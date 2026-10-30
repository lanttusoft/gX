/**********************************************************************************************
*
*   [gX: src/renderer/gfx/temporal_history.c]
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


//opencode: renderer-layer temporal-history lifecycle and ping-pong state.
#include "temporal_history.h"

gX_I0 gX_RendererGFX_TemporalHistory_Init(
    gX_RendererGFX_TemporalHistory *history)
{
    if (!history) return;
    history->frameIndex = 0;
    history->historyValid = false;
    history->clearPending = true;
    history->passActive = false;
    history->parity = false;
}

gX_I0 gX_RendererGFX_TemporalHistory_Reset(
    gX_RendererGFX_TemporalHistory *history)
{
    if (!history) return;
    history->frameIndex = 0;
    history->historyValid = false;
    history->clearPending = true;
    history->passActive = false;
    history->parity = false;
}

gX_I1 gX_RendererGFX_TemporalHistory_NeedsClear(
    const gX_RendererGFX_TemporalHistory *history)
{
    return history && history->clearPending;
}

gX_I0 gX_RendererGFX_TemporalHistory_MarkCleared(
    gX_RendererGFX_TemporalHistory *history)
{
    if (!history) return;
    history->clearPending = false;
    history->historyValid = false;
}

gX_I32 gX_RendererGFX_TemporalHistory_FrameIndex(
    const gX_RendererGFX_TemporalHistory *history)
{
    return history ? history->frameIndex : 0;
}

//ai: require distinct live wrapper metadata to prevent history color feedback.
gX_I1 gX_RendererGFX_TemporalHistory_BeginPass(
    gX_RendererGFX_TemporalHistory *history,
    gX_RendererGFX_RenderTexture *targetA,
    gX_RendererGFX_RenderTexture *targetB,
    gX_RendererGFX_TemporalHistoryTargets *targets)
{
    if (!history || !targetA || !targetB || !targets
        || history->clearPending || history->passActive
        || !targetA->wrapper.loadingState.loaded
        || !targetB->wrapper.loadingState.loaded
        || targetA == targetB
        || !targetA->wrapper.rt.id || !targetB->wrapper.rt.id
        || !targetA->wrapper.rt.texture.id || !targetB->wrapper.rt.texture.id
        || targetA->wrapper.rt.id == targetB->wrapper.rt.id
        || targetA->wrapper.rt.texture.id == targetB->wrapper.rt.texture.id)
        return false;
    if (history->parity) {
        targets->read = targetB;
        targets->write = targetA;
    } else {
        targets->read = targetA;
        targets->write = targetB;
    }
    history->passActive = true;
    return true;
}

//ai: bounded sample counter wraps without discarding the completed history or parity.
gX_I0 gX_RendererGFX_TemporalHistory_CommitPass(
    gX_RendererGFX_TemporalHistory *history)
{
    if (!history || !history->passActive) return;
    history->historyValid = true;
    history->parity = !history->parity;
    history->frameIndex = history->frameIndex >= 2147483647
        ? 0 : history->frameIndex + 1;
    history->passActive = false;
}

gX_I0 gX_RendererGFX_TemporalHistory_AbortPass(
    gX_RendererGFX_TemporalHistory *history)
{
    if (!history) return;
    history->passActive = false;
}

gX_RendererGFX_RenderTexture *gX_RendererGFX_TemporalHistory_CurrentTarget(
    const gX_RendererGFX_TemporalHistory *history,
    gX_RendererGFX_RenderTexture *targetA,
    gX_RendererGFX_RenderTexture *targetB)
{
    if (!history) return targetA;
    return history->parity ? targetB : targetA;
}
