/**********************************************************************************************
*
*   [gX: src/gui/transition.c]
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


//opencode: unified transition system — single state machine with callback + easing
// Merges the old struct-based System A and the callback-based System B (g_fade)
#include "gui.h"

#include "../main.h"

#include "../common/log.h"
#include "../common/macros.h"

#include "../gxmath/gxmath.h"

#include "../wrappers/wrappers.h"

#include "../rhi/rhi.h"
#include "../rhi/rhi_buf.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi_state.h"
#include "../rhi/rhi_gl.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)

//opencode: apply easing — delegates to centralized gxmath/easing.h dispatch

//opencode: full reset of the transition struct — clears ALL fields, not just a subset.
//   Prevents stale state from leaking when a transition is interrupted mid-phase and
//   a new one starts before the previous one completes.
gX_I32 gX_GUI_Transition_Clear(gX_I0) {
    gui.transition.enabled = false;
    gui.transition.overrideRefresh = false;
    gui.transition.doRefreshStartMiddle = false;
    gui.transition.doRefreshEndMiddle = false;
    gui.transition.refreshNow = false;
    gui.transition.execute = false;
    gui.transition.executing = false;
    gui.transition.setup = false;
    gui.transition.done = false;
    gui.transition.doOut = false;
    gui.transition.doMiddle = false;
    gui.transition.doIn = false;
    gui.transition.targetRect = false;
    gui.transition.targetColor = false;
    gui.transition.targetPP = false;
    gui.transition.targetPosition = false;
    gui.transition.targetScale = false;
    gui.transition.startPosOffset = (gX_F32v2){0,0};
    gui.transition.endPosOffset = (gX_F32v2){0,0};
    gui.transition.startScaleVal = (gX_F32v2){1,1};
    gui.transition.endScaleVal = (gX_F32v2){1,1};
    gui.transition.result = (gX_F32v4){0,0,0,0};
    gui.transition.start = (gX_F32v4){0,0,0,0};
    gui.transition.end = (gX_F32v4){0,0,0,0};
    gui.transition.out = (gX_GUI_Canvas_Transition_Sub){0};
    gui.transition.middle = (gX_GUI_Canvas_Transition_Sub){0};
    gui.transition.in = (gX_GUI_Canvas_Transition_Sub){0};
    gui.transition.callback = 0;
    gui.transition.userData = 0;
    gui.transition.callbackRan = false;
    gui.transition.easeOut = 0;
    gui.transition.easeMiddle = 0;
    gui.transition.easeIn = 0;
    gui.transition.elapsed = 0.0f;
    gui.transition.style = GX_GUI_TRANSITION_STYLE_FADE;
    return 0;
}

gX_I32 gX_GUI_Transition_Setup(gX_I0) {
    //opencode: skip transition animation on initial program startup — the title
    //   scene should appear immediately rather than fading in from black.
    if (gui.state.INITIAL_LOAD || gui.directdraw.fadeActive) {
        gui.state.INITIAL_LOAD = false;
        return 0;
    }
    gui.transition.enabled = true;
    gui.transition.execute = true;
    gui.transition.setup = false;
    gui.transition.done = false;
    gui.transition.doOut = true;
    gui.transition.doMiddle = true;
    gui.transition.doIn = true;
    //opencode: default durations follow the a11y motion tokens so themes can
    //   retime scene switches without touching call sites.
    gui.transition.out.Tseconds = gui.style.a11y.motionBase > 0.0f
        ? gui.style.a11y.motionBase / 1000.0f : gX_GUI_TRANSITION_DEFAULT_OUT;
    gui.transition.middle.Tseconds = gui.style.a11y.motionLong > 0.0f
        ? gui.style.a11y.motionLong / 1000.0f : gX_GUI_TRANSITION_DEFAULT_MIDDLE;
    gui.transition.in.Tseconds = gui.style.a11y.motionBase > 0.0f
        ? gui.style.a11y.motionBase / 1000.0f : gX_GUI_TRANSITION_DEFAULT_IN;
    gui.transition.elapsed = 0.0f;
    gui.transition.start = gX_Color_ToV4Raw(gX_WHITE);
    gui.transition.end = gX_Color_ToV4Raw(gX_BLACK);
    gui.transition.targetColor = true;
    gui.transition.overrideRefresh = true;
    gui.transition.doRefreshStartMiddle = true;
    //opencode: Quad easing for smoother title transitions
    gui.transition.easeOut    = gX_EASE_QUAD_OUT;
    gui.transition.easeMiddle = gX_EASE_LINEAR;
    gui.transition.easeIn     = gX_EASE_QUAD_IN;
    //opencode: position/scale targets default to off (opt-in)
    gui.transition.targetPosition = false;
    gui.transition.targetScale = false;
    gui.transition.startPosOffset = (gX_F32v2){0,0};
    gui.transition.endPosOffset = (gX_F32v2){0,0};
    gui.transition.startScaleVal = (gX_F32v2){1,1};
    gui.transition.endScaleVal = (gX_F32v2){1,1};
    return 0;
}

//opencode: total active phase time — the shared clock length without stagger.
gX_F32 gX_GUI_Transition_TotalSeconds(const gX_GUI_Canvas_Transition *tra)
{
    gX_F32 total = 0.0f;
    if (!tra) return 0.0f;
    if (tra->doOut && tra->out.Tseconds > 0.0f) total += tra->out.Tseconds;
    if (tra->doMiddle && tra->middle.Tseconds > 0.0f) total += tra->middle.Tseconds;
    if (tra->doIn && tra->in.Tseconds > 0.0f) total += tra->in.Tseconds;
    return total;
}

//opencode: pure stagger evaluation — maps a delay-offset local clock onto
//   the out/middle/in curve so each canvas can run the same transition late
//   without sharing mutable phase timers. Returns true when the local
//   timeline finished (caller holds the steady end state).
gX_I1 gX_GUI_Transition_EvalLocal(const gX_GUI_Canvas_Transition *tra,
    gX_F32 localElapsed, gX_F32v4 *outColor, gX_F32v2 *outPos,
    gX_F32v2 *outScale, gX_F32 *outAngle)
{
    gX_F32v4 colorResult = gX_Color_ToV4Raw(gX_WHITE);
    gX_F32v2 posResult = {0, 0};
    gX_F32v2 scaleResult = {1, 1};
    gX_F32 angleResult = 0.0f;
    gX_F32 skewMaxAngle = 3.0f;
    gX_F32 cursor = 0.0f;
    gX_F32 outDur = 0.0f;
    gX_F32 midDur = 0.0f;
    gX_F32 inDur = 0.0f;
    if (!tra) return true;
    if (tra->doOut && tra->out.Tseconds > 0.0f) outDur = tra->out.Tseconds;
    if (tra->doMiddle && tra->middle.Tseconds > 0.0f) midDur = tra->middle.Tseconds;
    if (tra->doIn && tra->in.Tseconds > 0.0f) inDur = tra->in.Tseconds;
    if (localElapsed < 0.0f)
    {
        localElapsed = 0.0f;
    }
    if (localElapsed < outDur && (tra->doOut || outDur > 0.0f))
    {
        gX_F32 t = outDur > 0.0f ? localElapsed / outDur : 1.0f;
        gX_F32 te = gX_Easing_Apply(tra->easeOut, gX_F32_Clamp(t, 0.0f, 1.0f));
        colorResult = gX_F32v4_Lerp(tra->start, tra->end, te);
        posResult = gX_F32v2_Lerp(tra->startPosOffset, tra->endPosOffset, te);
        scaleResult = gX_F32v2_Lerp(tra->startScaleVal, tra->endScaleVal, te);
        if (tra->style == GX_GUI_TRANSITION_STYLE_SKEW_SLIDE)
        {
            angleResult = gX_F32_Lerp(0.0f, skewMaxAngle, te);
        }
        if (outColor) *outColor = colorResult;
        if (outPos) *outPos = posResult;
        if (outScale) *outScale = scaleResult;
        if (outAngle) *outAngle = angleResult;
        return false;
    }
    cursor += outDur;
    if (localElapsed < cursor + midDur && (tra->doMiddle || midDur > 0.0f))
    {
        colorResult = tra->end;
        posResult = tra->endPosOffset;
        scaleResult = tra->endScaleVal;
        if (tra->style == GX_GUI_TRANSITION_STYLE_SKEW_SLIDE)
        {
            angleResult = skewMaxAngle;
        }
        if (outColor) *outColor = colorResult;
        if (outPos) *outPos = posResult;
        if (outScale) *outScale = scaleResult;
        if (outAngle) *outAngle = angleResult;
        return false;
    }
    cursor += midDur;
    if (localElapsed < cursor + inDur && (tra->doIn || inDur > 0.0f))
    {
        gX_F32 t = inDur > 0.0f ? (localElapsed - cursor) / inDur : 1.0f;
        gX_F32 te = gX_Easing_Apply(tra->easeIn, gX_F32_Clamp(t, 0.0f, 1.0f));
        colorResult = gX_F32v4_Lerp(tra->end, tra->start, te);
        posResult = gX_F32v2_Lerp(tra->endPosOffset, tra->startPosOffset, te);
        scaleResult = gX_F32v2_Lerp(tra->endScaleVal, tra->startScaleVal, te);
        if (tra->style == GX_GUI_TRANSITION_STYLE_SKEW_SLIDE)
        {
            angleResult = gX_F32_Lerp(skewMaxAngle, 0.0f, te);
        }
        if (outColor) *outColor = colorResult;
        if (outPos) *outPos = posResult;
        if (outScale) *outScale = scaleResult;
        if (outAngle) *outAngle = angleResult;
        return false;
    }
    if (outColor) *outColor = gX_Color_ToV4Raw(gX_WHITE);
    if (outPos)
    {
        *outPos = tra->targetPosition ? tra->startPosOffset : (gX_F32v2){0, 0};
    }
    if (outScale)
    {
        *outScale = tra->targetScale ? tra->startScaleVal : (gX_F32v2){1, 1};
    }
    if (outAngle) *outAngle = 0.0f;
    return true;
}

static gX_I0 gX_GUI_Canvas_Transition_Sub_Do(gX_GUI_Canvas_Transition_Sub *sub, gX_I1 doSub) {
    if (sub->execute && doSub && !(sub->done)) {
        //opencode: guard against uninitialized Tseconds (0 or NaN) — if not set,
        //   treat the sub-phase as complete immediately. This prevents a divide-
        //   by-zero producing NaN Tactual, which would silently skip the phase
        //   and leave the transition stuck in an incomplete state.
        if (sub->Tseconds <= 0.0f) {
            sub->Tactual = gX_GUI_TRANSITION_SUB_DONE_T;
            sub->done = true;
            return;
        }
        sub->Tactual = sub->T / sub->Tseconds;
        if (sub->Tactual < gX_GUI_TRANSITION_SUB_DONE_T) {
            sub->T += (gX_F32)gX_FrameTime_Get();
        }
        else {
            sub->Tactual = gX_GUI_TRANSITION_SUB_DONE_T;
            sub->T = gX_GUI_TRANSITION_SUB_RESET_T;
            sub->done = true;
        }
    }
}

gX_I0 gX_GUI_Canvas_Transition_Update(gX_GUI_Canvas_Transition *tra);
gX_I0 gX_GUI_Canvas_Transition_Update(gX_GUI_Canvas_Transition *tra) {
    gX_GUI_Canvas *canvases = gui.canvases.data;
    static gX_I1 prevOutDone = false, prevMiddleDone = false, prevInDone = false;
    //opencode: guard against uninitialized canvases (can happen at startup)
    if (!canvases || gui.canvases.amount == 0) { return; }
    //opencode: guard against unconfigured transitions — if execute was set to true
    //   without calling Transition_Setup or Transition_Fade first (e.g. via a button
    //   callback that directly writes gui.transition.execute), no transition targets
    //   are configured. Attempting to run the state machine would produce NaN timers
    //   (Tseconds=0) and apply {0,0,0,0} canvasHue, making all elements invisible.
    if (tra->execute && !tra->done
        && !tra->targetColor && !tra->targetPosition && !tra->targetScale
        && !tra->targetRect && !tra->targetPP)
    {
        tra->done = true;
        tra->execute = false;
        tra->executing = false;
        tra->overrideRefresh = false;
        return;
    }
    //opencode: skip transition if bypass flag is set (AI/testing)
    if (gui.state.SKIP_TRANSITION) {
        tra->execute = false;
        tra->setup = false;
        tra->done = true;
        tra->executing = false;
        tra->overrideRefresh = false;
        return;
    }

    //opencode: reduced motion — snap the transition to its end state instead
    //   of tweening (a11y motionReduced token). The scene switch (callback +
    //   owned refresh) still runs; canvases land at full visibility, which is
    //   the steady-state the normal state machine restores on completion.
    if (gui.style.a11y.motionReduced && tra->execute && !tra->done) {
        tra->done = true;
        tra->execute = false;
        tra->executing = false;
        tra->setup = false;
        tra->overrideRefresh = false;
        if (tra->doRefreshStartMiddle || tra->doRefreshEndMiddle) tra->refreshNow = true;
        if (!tra->callbackRan) {
            tra->callbackRan = true;
            if (tra->callback) tra->callback(tra->userData);
        }
        for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
            gX_GUI_Canvas *c = &canvases[i];
            if (!c->doTransition) continue;
            c->canvasHue = gX_WHITE;
            c->canvasOffset = (gX_F32v2){0,0};
            c->canvasScale = (gX_F32v2){1,1};
            c->canvasAngle = 0.0f;
        }
        return;
    }
    
    //opencode: initialize a new transition before phase advancement. The old
    //   order advanced Sub_Do first and then zeroed its elapsed time, losing
    //   the first frame of every transition.
    if (!tra->setup) {
        tra->done = false;
        tra->out.done = false;
        tra->middle.done = false;
        tra->in.done = false;
        tra->out.T = 0.0f;
        tra->middle.T = 0.0f;
        tra->in.T = 0.0f;
        tra->elapsed = 0.0f;
        tra->executing = false;
        tra->refreshNow = false;
        tra->callbackRan = false;
        tra->setup = true;
        prevOutDone = false;
        prevMiddleDone = false;
        prevInDone = false;
    }

    //opencode: transition phase feedback logging (static, shared across all transition instances)
    if (tra->execute && !tra->done) {
        if (tra->out.done && !prevOutDone) {
            prevOutDone = true;
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_TRANSITION] out complete (%.2fs) — entering middle",
                (gX_F64)tra->out.Tseconds);
        }
        if (tra->middle.done && !prevMiddleDone) {
            prevMiddleDone = true;
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_TRANSITION] middle complete — entering in (%.2fs)",
                (gX_F64)tra->in.Tseconds);
        }
        if (tra->done && !prevInDone) {
            prevInDone = true;
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_TRANSITION] transition complete");
        }
    } else {
        prevOutDone = false;
        prevMiddleDone = false;
        prevInDone = false;
    }
    
    tra->executing = true;
    tra->out.execute = true;
    
    if (tra->out.done) {
        tra->out.execute = false;
        
        if (tra->doMiddle) {tra->middle.execute = true;}
        else               {tra->middle.done = true;}
        
        //opencode: fire callback at start of middle phase (out→middle transition)
        if (!tra->callbackRan) {
            tra->callbackRan = true;
            if (tra->callback) tra->callback(tra->userData);
        }
        
        if (tra->doRefreshStartMiddle) tra->refreshNow = true;
    }
    if (tra->middle.done) {
        tra->middle.execute = false;
        
        if (tra->doIn) {tra->in.execute = true;}
        else           {tra->in.done = true;}
        
        if (tra->doRefreshEndMiddle) tra->refreshNow = true;
    }
    if (tra->in.done) {
        tra->in.execute = false;

        tra->done = true;

        //opencode: v0.4.0 — the completion log lived in the phase-feedback
        //   block above, which checked tra->done BEFORE it was set here and
        //   then died (execute=false) the next frame — it could never fire.
        //   Log at the moment done becomes true instead.
        if (!prevInDone) {
            prevInDone = true;
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_TRANSITION] transition complete");
        }
    }

    gX_GUI_Canvas_Transition_Sub_Do(&tra->out,      tra->doOut);
    gX_GUI_Canvas_Transition_Sub_Do(&tra->middle,   tra->doMiddle);
    gX_GUI_Canvas_Transition_Sub_Do(&tra->in,       tra->doIn);
    tra->elapsed += (gX_F32)gX_FrameTime_Get();

    //opencode: per-canvas stagger — each canvas runs the same out/middle/in
    //   curve on its own delay-offset clock (EvalLocal is pure and unit
    //   tested). Delayed canvases hold the start state until their delay
    //   elapses instead of sharing the global sub-phase timers.
    gX_I1 allVisualsDone = true;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        if (!canvases[i].doTransition) continue;
        gX_F32 local = tra->elapsed - canvases[i].transitionDelay;
        gX_F32v4 colorResult = gX_Color_ToV4Raw(gX_WHITE);
        gX_F32v2 posResult = {0, 0};
        gX_F32v2 scaleResult = {1, 1};
        gX_F32 angleResult = 0.0f;
        gX_I1 visualDone = gX_GUI_Transition_EvalLocal(tra, local,
            &colorResult, &posResult, &scaleResult, &angleResult);
        if (!visualDone) allVisualsDone = false;

        if (tra->targetColor) {
            canvases[i].canvasHue.r = (gX_U8)gX_F32_Clamp(colorResult.x, 0.0f, 255.0f);
            canvases[i].canvasHue.g = (gX_U8)gX_F32_Clamp(colorResult.y, 0.0f, 255.0f);
            canvases[i].canvasHue.b = (gX_U8)gX_F32_Clamp(colorResult.z, 0.0f, 255.0f);
            canvases[i].canvasHue.a = (gX_U8)gX_F32_Clamp(colorResult.w, 0.0f, 255.0f);
        }
        if (tra->targetPosition) {
            canvases[i].canvasOffset = posResult;
        }
        if (tra->targetScale) {
            canvases[i].canvasScale = scaleResult;
        }
        if (tra->style == GX_GUI_TRANSITION_STYLE_SKEW_SLIDE) {
            canvases[i].canvasAngle = angleResult;
        }
    }
    //opencode: staggered visuals outlive the shared phase timers — hold the
    //   transition open until the slowest canvas finishes so delayed layers
    //   never snap to WHITE mid-curve. Callback/refresh already fired on the
    //   shared clock; only the steady-state reset waits.
    if (tra->done && !allVisualsDone) {
        tra->done = false;
        tra->execute = true;
        tra->executing = true;
    }
    if (tra->done) {
        tra->execute = false;
        tra->setup = false;
        //opencode: allow normal refreshes to proceed after transition completes
        gui.transition.overrideRefresh = false;
        //opencode: clear executing flag — was left TRUE after completion, which
        //   force-cleared HOVERED/CLICKED_LEFT/PRESSED_LEFT/DRAGGED_LEFT on all
        //   elements every frame (element.c) until the next scene switch/transition.
        tra->executing = false;
        //opencode: reset all canvases to full visibility
        for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
            canvases[i].canvasHue = gX_WHITE;
            canvases[i].canvasScale = (gX_F32v2){1,1};
            canvases[i].canvasOffset = (gX_F32v2){0,0};
            canvases[i].canvasAngle = 0.0f;
        }
    }
    
}

//=============================================================================
// Unified callback-based fade transition — fills gui.transition struct
// (replaces the old separate static g_fade system)
//=============================================================================

gX_I0 gX_GUI_Transition_Fade(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData)
{
    //opencode: clear all state first so every field is in a known state.
    //   Without this, fields like overrideRefresh or doRefreshStartMiddle
    //   may retain stale values from a previous or interrupted transition.
    gX_GUI_Transition_Clear();
    gui.transition.enabled      = true;
    gui.transition.execute      = true;
    gui.transition.doOut        = true;
    gui.transition.doMiddle     = true;
    gui.transition.doIn         = true;
    gui.transition.out.Tseconds = outSec;
    gui.transition.middle.Tseconds = midSec;
    gui.transition.in.Tseconds  = inSec;
    gui.transition.elapsed = 0.0f;
    gui.transition.start        = gX_Color_ToV4Raw(gX_WHITE);
    gui.transition.end          = gX_Color_ToV4Raw(gX_BLACK);
    gui.transition.targetColor  = true;
    gui.transition.overrideRefresh = false;
    gui.transition.doRefreshStartMiddle = false;
    gui.transition.doRefreshEndMiddle   = false;
    gui.transition.callback     = onMidpoint;
    gui.transition.userData     = userData;
    gui.transition.callbackRan  = false;
    gui.transition.easeOut      = gX_EASE_LINEAR;
    gui.transition.easeMiddle   = gX_EASE_LINEAR;
    gui.transition.easeIn       = gX_EASE_LINEAR;
    //opencode: reset position/scale targets (fade only affects color)
    gui.transition.targetPosition = false;
    gui.transition.targetScale = false;
    gui.transition.startPosOffset = (gX_F32v2){0,0};
    gui.transition.endPosOffset = (gX_F32v2){0,0};
    gui.transition.startScaleVal = (gX_F32v2){1,1};
    gui.transition.endScaleVal = (gX_F32v2){1,1};
    //opencode: reset sub-phase state so the state machine starts fresh
    gui.transition.setup        = false;
    gui.transition.done         = false;
    gui.transition.out.done     = false;
    gui.transition.middle.done  = false;
    gui.transition.in.done      = false;
    gui.transition.out.T        = 0.0f;
    gui.transition.middle.T     = 0.0f;
    gui.transition.in.T         = 0.0f;
}

gX_I1 gX_GUI_Transition_Fade_IsActive(gX_I0) {
    return gui.transition.enabled && gui.transition.execute && !gui.transition.done;
}

//opencode: v0.4.0 — fade-from-black entrance: the canvas starts fully black
//   (start color) and brightens to full (end color) over secs. Single out
//   phase — middle/in skipped so the machine completes right after the reveal
//   (the state machine requires doMiddle/doIn false to shortcut their done
//   flags). Used for the startup title entrance instead of the swing.
gX_I0 gX_GUI_Transition_FadeFromBlack(gX_F32 secs) {
    gX_GUI_Transition_Clear();
    gui.transition.enabled      = true;
    gui.transition.execute      = true;
    gui.transition.doOut        = true;
    gui.transition.doMiddle     = false;
    gui.transition.doIn         = false;
    gui.transition.out.Tseconds = secs;
    gui.transition.elapsed = 0.0f;
    gui.transition.start        = gX_Color_ToV4Raw(gX_BLACK);
    gui.transition.end          = gX_Color_ToV4Raw(gX_WHITE);
    gui.transition.targetColor  = true;
    gui.transition.overrideRefresh = false;
    gui.transition.doRefreshStartMiddle = false;
    gui.transition.doRefreshEndMiddle   = false;
    gui.transition.easeOut      = gX_EASE_QUAD_OUT;
    gui.transition.easeMiddle   = gX_EASE_LINEAR;
    gui.transition.easeIn       = gX_EASE_LINEAR;
    gui.transition.targetPosition = false;
    gui.transition.targetScale  = false;
    gui.transition.targetRect   = false;
    gui.transition.targetPP     = false;
    gui.transition.startPosOffset = (gX_F32v2){0,0};
    gui.transition.endPosOffset = (gX_F32v2){0,0};
    gui.transition.startScaleVal = (gX_F32v2){1,1};
    gui.transition.endScaleVal   = (gX_F32v2){1,1};
    gui.transition.setup        = false;
    gui.transition.done         = false;
    gui.transition.out.done     = false;
    gui.transition.middle.done  = false;
    gui.transition.in.done      = false;
    gui.transition.out.T        = 0.0f;
    gui.transition.middle.T     = 0.0f;
    gui.transition.in.T         = 0.0f;
}

//opencode: slide transition — animates canvasOffset for all canvases that have doTransition=true
gX_I0 gX_GUI_Transition_Slide(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_F32v2 startOffset, gX_F32v2 endOffset,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData)
{
    gX_GUI_Transition_Fade(outSec, midSec, inSec, onMidpoint, userData);
    gui.transition.targetColor = false;
    gui.transition.targetPosition = true;
    gui.transition.startPosOffset = startOffset;
    gui.transition.endPosOffset = endOffset;
}

//opencode: zoom transition — animates canvasScale for all canvases that have doTransition=true
gX_I0 gX_GUI_Transition_Zoom(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_F32v2 startScale, gX_F32v2 endScale,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData)
{
    gX_GUI_Transition_Fade(outSec, midSec, inSec, onMidpoint, userData);
    gui.transition.targetColor = false;
    gui.transition.targetScale = true;
    gui.transition.startScaleVal = startScale;
    gui.transition.endScaleVal = endScale;
}

//opencode: v0.3.8 — style transition ("menu personality" pack). Same phase
//   machine as Fade; the style adds motion:
//   - SKEW_SLIDE: canvasOffset slide + canvasAngle swing (Blades-style)
//   - DIAGONAL_WIPE / SCANLINE_CURTAIN / IRIS: screen-space overlay sweep
//   FADE/SLIDE/ZOOM behave exactly like their dedicated functions.
gX_I0 gX_GUI_Transition_Style(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_GUI_TransitionStyle style,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData)
{
    gX_GUI_Transition_Fade(outSec, midSec, inSec, onMidpoint, userData);
    gui.transition.style = style;
    if (style == GX_GUI_TRANSITION_STYLE_SKEW_SLIDE) {
        //opencode: slide + swing — canvas sweeps out to the left, rotates,
        //   and the new canvas swings back in from the same direction
        gui.transition.targetColor = false;
        gui.transition.targetPosition = true;
        gui.transition.startPosOffset = (gX_F32v2){0, 0};
        //opencode: v0.4.0 — tamed: -60px (was -120px), gentler swing
        gui.transition.endPosOffset = (gX_F32v2){-60.0f, 0};
    } else if (style == GX_GUI_TRANSITION_STYLE_SLIDE) {
        gui.transition.targetColor = false;
        gui.transition.targetPosition = true;
        gui.transition.startPosOffset = (gX_F32v2){0, 0};
        //opencode: v0.4.0 — tamed: -60px (was -120px), gentler swing
        gui.transition.endPosOffset = (gX_F32v2){-60.0f, 0};
    } else if (style == GX_GUI_TRANSITION_STYLE_ZOOM) {
        gui.transition.targetColor = false;
        gui.transition.targetScale = true;
        gui.transition.startScaleVal = (gX_F32v2){1, 1};
        gui.transition.endScaleVal = (gX_F32v2){0.92f, 0.92f};
    }
}

//=============================================================================
//opencode: v0.3.8 — wipe overlay draw. Screen-space shader quad rendered after
//   all canvas composites so the sweep covers the whole GUI. Progress follows
//   the phase machine: out sweeps in (0→1), middle holds (1), in sweeps out
//   (1→0) revealing the new scene. The shader masks a flat color quad; the
//   masked region shows the transition end color (black by default).
//=============================================================================

gX_I0 gX_GUI_Transition_DrawOverlay(gX_I0) {
    if (!gui.transition.enabled || !gui.transition.execute || gui.transition.done) return;
    gX_GUI_TransitionStyle style = gui.transition.style;
    if (style != GX_GUI_TRANSITION_STYLE_DIAGONAL_WIPE
        && style != GX_GUI_TRANSITION_STYLE_SCANLINE_CURTAIN
        && style != GX_GUI_TRANSITION_STYLE_IRIS) return;
    //opencode: no phase executing yet (transition fired this frame, the
    //   state machine starts next update) — skip, avoids a full-cover flash
    if (!gui.transition.out.execute && !gui.transition.middle.execute
        && !gui.transition.in.execute) return;

    //opencode: lazy shader load — gui_wipe.frag is tiny and only used during
    //   wipe transitions; hot-reload handled by the shared shader compiler
    static gX_Shader s_wipeShader = { 0 };
    static gX_I1 s_wipeLoaded = false;
    if (!s_wipeLoaded) {
        s_wipeShader = gX_Shader_Load(0, "resources/shader/gui_wipe.frag");
        s_wipeLoaded = true;
        if (s_wipeShader.id <= 0) {
            gX_TraceLog(gX_LOG_ERROR, "TRANSITION_WIPE: gui_wipe.frag failed to load — wipe disabled");
            return;
        }
    }
    if (s_wipeShader.id <= 0) return;
    //opencode: query locations every frame — the shader compiler can
    //   recompile the program on GL-state changes, invalidating cached
    //   locations (stale locs silently wrote to wrong uniforms)
    gX_I32 s_styleLoc = gX_Shader_GetLocation(s_wipeShader, "style");
    gX_I32 s_progressLoc = gX_Shader_GetLocation(s_wipeShader, "progress");
    gX_I32 s_resLoc = gX_Shader_GetLocation(s_wipeShader, "res");
    gX_I32 s_colorLoc = gX_Shader_GetLocation(s_wipeShader, "color");
    gX_I32 s_mvpLoc = gX_Shader_GetLocation(s_wipeShader, "mvp");

    gX_F32 progress;
    if (gui.transition.out.execute) {
        progress = gX_F32_Clamp(gui.transition.out.Tactual, 0.0f, 1.0f);
    } else if (gui.transition.middle.execute) {
        progress = 1.0f;
    } else {
        progress = 1.0f - gX_F32_Clamp(gui.transition.in.Tactual, 0.0f, 1.0f);
    }

    //opencode: RAW fullscreen quad — the rlgl batch ALWAYS re-binds
    //   RLGL.State.currentShaderId (the default program) at flush, so
    //   BeginShader + Rectangle_Draw silently drops any custom program and
    //   the wipe never appears (a plain white rect covered the screen
    //   instead). Raw VAO + gX_RHI_DrawArrays bypasses the batch entirely.
    static gX_U32 s_wipeVao = 0, s_wipeVbo = 0;
    static gX_I1 s_wipeGeoInit = false;
    if (!s_wipeGeoInit) {
        //opencode: one oversized triangle covers the whole NDC viewport
        //opencode: vec3 positions — raylib's default vertex shader reads
        //   vertexPosition as vec3
        const gX_F32 verts[9] = { -1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f };
        s_wipeVao = gX_RHI_VAO_Create("gui_wipe");
        gX_RHI_VAO_Bind(s_wipeVao);
        s_wipeVbo = gX_RHI_Buffer_Create("gui_wipe");
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, s_wipeVbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, sizeof(verts), verts, gX_RHI_STATIC_DRAW);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, 0, NULL);
        gX_RHI_VAO_Bind(0);
        s_wipeGeoInit = true;
    }

    gX_RHI_BindFBO(0);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
    gX_RHI_Disable(gX_RHI_CULL_FACE);
    gX_RHI_Viewport(drawinfo.vpX, drawinfo.vpY, drawinfo.vpW, drawinfo.vpH);
    gX_Drawing_BeginBlendCustomSeparate(
        gX_BLEND_SRC_ALPHA, gX_BLEND_ONE_MINUS_SRC_ALPHA,
        gX_BLEND_ONE, gX_BLEND_ONE_MINUS_SRC_ALPHA);

    gX_RHI_Shader_Use(s_wipeShader.id);
    //opencode: RHI uniform uploads — the gX SetValue wrapper routes through
    //   raylib's format-switch and failed to deliver progress reliably
    //   (res/color arrived, progress silently dropped)
    gX_RHI_UniformInt(s_styleLoc, (gX_I32)style);
    gX_RHI_Uniform_Float(s_progressLoc, progress);
    gX_RHI_Uniform_Vec2(s_resLoc, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight);
    gX_RHI_UniformVec4(s_colorLoc, 1, &gui.transition.end.x);
    //opencode: identity MVP — the default vertex shader transforms
    //   vertexPosition by matMVP; positions are pre-NDC
    if (s_mvpLoc >= 0)
        glUniformMatrix4fv((gX_I32)s_mvpLoc, 1, 0, (const gX_F32[]){
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
    gX_RHI_VAO_Bind(s_wipeVao);
    gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, 3);
    gX_RHI_VAO_Bind(0);

    gX_RHI_Shader_Use(0);
    gX_Drawing_EndBlend();
}
