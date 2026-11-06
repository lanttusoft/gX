/**********************************************************************************************
*
*   [gX: src/gui/console_playback.c]
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

//ai: gX-240 playback semantics — the console pause menu freezes simulation,
//   a step advances one 1/60 s tick, and fast-forward cycles 2x/4x/8x/off.
//   UI, OSD and audio intentionally keep real time. Save-state loads and scene
//   changes reset playback so a transient speed never leaks across cartridges.

#include "console_playback.h"
#include "console_state.h"
#include "gui.h"

#include "../main.h"
#include "../input.h"
#include "../wrappers/wrappers.h"
#include "../common/enum.h"
#include "../common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern gX_I1 gX_Tools_RuntimeActive(gX_I0);

static gX_I1 s_active;
static gX_I1 s_paused;
static gX_I1 s_stepPending;
static gX_F32 s_stepFeedback;
static gX_I32 s_fastForward;
static gX_I32 s_scene = -1;
static gX_F64 s_delta;
static gX_I8 s_label[24] = { 0 };

static gX_I1 console_playback_scene(gX_I0)
{
    if (arguments.cartridgeY2K) return 1;
    if (arguments.cartridgeRay1 && maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH) return 1;
    if (arguments.cartridgeParticle && maindata.masterIndex == gX_SCENE_PHYSICS) return 1;
    if (arguments.consoleShell && maindata.masterIndex != gX_SCENE_TITLE) return 1;
    return 0;
}

static gX_I0 console_playback_refresh_menu(gX_I0)
{
    if (gui.menuCanvas)
    {
        gui.menuCanvas->refresh = true;
        gui.focusFirstPending = true;
    }
}

gX_I0 gX_ConsolePlayback_Reset(gX_I0)
{
    s_stepPending = 0;
    s_stepFeedback = 0.0f;
    s_fastForward = 0;
    s_delta = 0.0;
    s_paused = 0;
    s_active = 0;
    s_scene = -1;
    s_label[0] = 0;
    gX_FrameTime_ClearSimulationOverride();
}

gX_I1 gX_ConsolePlayback_Active(gX_I0)
{
    return console_playback_scene();
}

gX_I1 gX_ConsolePlayback_Paused(gX_I0)
{
    return (console_playback_scene() && gui.state.SHOW_MENU) ? 1 : 0;
}

gX_I1 gX_ConsolePlayback_InputBlocked(gX_I0)
{
    return gX_ConsolePlayback_Paused();
}

gX_I1 gX_ConsolePlayback_Simulating(gX_I0)
{
    if (!s_active) return 1;
    if (s_delta > 0.0) return 1;
    if (maindata.refresh || maindata.refreshOngoing) return 1;
    return 0;
}

gX_F64 gX_ConsolePlayback_Delta(gX_I0)
{
    return s_delta;
}

gX_I32 gX_ConsolePlayback_FastForward(gX_I0)
{
    return s_fastForward;
}

gX_I0 gX_ConsolePlayback_RequestStep(gX_I0)
{
    if (!gX_ConsolePlayback_Paused()) return;
    s_stepPending = 1;
    s_stepFeedback = 0.75f;
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_PLAYBACK: step requested");
}

gX_I0 gX_ConsolePlayback_CycleFastForward(gX_I0)
{
    if (!gX_ConsolePlayback_Paused()) return;
    switch (s_fastForward)
    {
        case 0:  s_fastForward = 2; break;
        case 2:  s_fastForward = 4; break;
        case 4:  s_fastForward = 8; break;
        default: s_fastForward = 0; break;
    }
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_PLAYBACK: fast-forward=%d", s_fastForward);
    console_playback_refresh_menu();
}

const gX_I8 *gX_ConsolePlayback_Label(gX_I0)
{
    if (!gX_ConsolePlayback_Active()) return "";
    if (s_paused)
    {
        if (s_stepFeedback > 0.0f) return "STEP";
        if (s_fastForward > 0)
        {
            snprintf(s_label, sizeof(s_label), "PAUSED %dx", s_fastForward);
            return s_label;
        }
        return "PAUSED";
    }
    if (s_fastForward > 0)
    {
        snprintf(s_label, sizeof(s_label), "FF %dx", s_fastForward);
        return s_label;
    }
    return "";
}

gX_I0 gX_ConsolePlayback_Update(gX_I0)
{
    //ai: cartridge save hotkeys share the console input path so Y2K and RAY-1
    //   behave the same; the pause menu owns its own explicit save/load rows.
    if (!gX_ConsolePlayback_InputBlocked()
        && (arguments.cartridgeY2K || arguments.cartridgeRay1
            || arguments.cartridgeParticle)) {
        if (input.keyboard.keys[gX_KEY_F5].state.PRESSED)
            gX_ConsoleState_Save(1);
        if (input.keyboard.keys[gX_KEY_F8].state.PRESSED)
            gX_ConsoleState_Load(1);
    }

    gX_I1 Active = console_playback_scene();
    if (Active != s_active || maindata.masterIndex != s_scene)
    {
        s_stepPending = 0;
        s_stepFeedback = 0.0f;
        s_fastForward = 0;
        s_delta = 0.0;
    }
    s_active = Active;
    s_scene = maindata.masterIndex;

    gX_F64 RealDt = gX_FrameTime_Get();
    if (RealDt < 0.0) RealDt = 0.0;

    gX_I1 Paused = (Active && gui.state.SHOW_MENU) ? 1 : 0;
    if (Paused && !s_paused)
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_PLAYBACK: pause");
    if (!Paused && s_paused && Active)
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_PLAYBACK: resume");
    s_paused = Paused;

    if (Paused && !gX_Tools_RuntimeActive())
    {
        if (input.keyboard.keys[gX_KEY_F6].state.PRESSED)
        {
            input.keyboard.keys[gX_KEY_F6].state.PRESSED = false;
            gX_ConsolePlayback_RequestStep();
        }
        if (input.keyboard.keys[gX_KEY_F7].state.PRESSED)
        {
            input.keyboard.keys[gX_KEY_F7].state.PRESSED = false;
            gX_ConsolePlayback_CycleFastForward();
        }
    }

    gX_I1 StepApplied = 0;
    if (!Active)
    {
        s_delta = RealDt;
        s_stepPending = 0;
    }
    else if (Paused)
    {
        if (s_stepPending)
        {
            s_delta = GX_CONSOLE_PLAYBACK_STEP_DT;
            s_stepPending = 0;
            s_stepFeedback = 0.75f;
            StepApplied = 1;
            gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_PLAYBACK: step");
        }
        else
        {
            s_delta = 0.0;
        }
    }
    else if (s_fastForward > 0)
    {
        s_delta = RealDt * (gX_F64)s_fastForward;
        if (s_delta > GX_CONSOLE_PLAYBACK_MAX_DELTA)
            s_delta = GX_CONSOLE_PLAYBACK_MAX_DELTA;
    }
    else
    {
        s_delta = RealDt;
    }

    if (s_stepFeedback > 0.0f)
    {
        s_stepFeedback -= (gX_F32)RealDt;
        if (s_stepFeedback < 0.0f) s_stepFeedback = 0.0f;
    }

    if (gX_GUI_Trace_Enabled() && getenv("GX_CONSOLE_PLAYBACK_TRACE"))
    {
        gX_TraceLog(gX_LOG_NOTICE,
            "CONSOLE_PLAYBACK: frame=%llu active=%d paused=%d ff=%d step=%d delta=%.9f sim=%.9f",
            (unsigned long long)maindata.frameCount, (gX_I32)Active, (gX_I32)Paused,
            s_fastForward, (gX_I32)StepApplied, s_delta,
            maindata.simulationTime + s_delta);
    }
}
