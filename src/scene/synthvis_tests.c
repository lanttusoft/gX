/**********************************************************************************************
*
*   [gX: src/scene/synthvis_tests.c]
*   Synth visualizer scene tests — key mapping, note lifecycle, key-bed geometry
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


//opencode: headless scene tests (no window/GPU): the key rows drive gX_Synth_*
//   voices at the gameX frequencies, release drops them, Park/Unload silence the
//   scene, Update rebuilds both GUI canvases, and the key-bed geometry the GUI
//   template shares stays in bounds.
#include "synthvis.h"

#include <math.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../input.h"
#include "../gui/gui.h"
#include "../audio/synth.h"

//opencode: set one key row down/up and run one scene update.
static gX_I0 gX_Tests_SynthVis_SetKey(gX_I32 keyCode, gX_I1 down) {
    input.keyboard.keys[keyCode].state.DOWN = down;
}

gX_I32 gX_Tests_SceneSynthVis(gX_I0);
gX_I32 gX_Tests_SceneSynthVis(gX_I0) {
    gX_I32 errors = 0;
    gX_Input savedInput = input;
    input = (gX_Input){0};
    gX_I32 savedMakeAmount = gui.makeAmount;
    gX_I1 savedMake = gui.make;

    gX_SceneSynthVis vis;
    gX_SceneSynthVis_Load(&vis);
    if (!vis.loaded || vis.firstFrame != 1 || strcmp(vis.name, "SynthVis") != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis load state");
        errors++;
    }
    for (gX_I32 i = 0; i < 14; i++) {
        if (vis.whiteVoiceID[i] != -1 || vis.keysPressed[i]) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis white key %i not idle", i);
            errors++;
        }
    }
    for (gX_I32 i = 0; i < 10; i++) {
        if (vis.sharpVoiceID[i] != -1 || vis.keysSharpPressed[i]) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis sharp key %i not idle", i);
            errors++;
        }
    }

    //opencode: key-bed geometry — the GUI template and the scene composite share
    //   these rects, so pin edges on a known surface.
    gX_Rectangle wr = gX_SceneSynthVis_WhiteKeyRect(0, 800.0f, 600.0f);
    gX_Rectangle wrLast = gX_SceneSynthVis_WhiteKeyRect(13, 800.0f, 600.0f);
    if (fabsf(wr.y - 510.0f) > 0.01f || fabsf(wr.height - 90.0f) > 0.01f
        || fabsf(wr.x) > 0.01f || wrLast.x + wrLast.width > 800.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis white key geometry");
        errors++;
    }
    gX_Rectangle br = gX_SceneSynthVis_BlackKeyRect(0, 800.0f, 600.0f);
    gX_Rectangle brLast = gX_SceneSynthVis_BlackKeyRect(9, 800.0f, 600.0f);
    if (fabsf(br.x - 40.0f) > 0.01f || fabsf(br.height - 55.8f) > 0.01f
        || fabsf(br.y - 510.0f) > 0.01f
        || brLast.x + brLast.width > 800.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis black key geometry");
        errors++;
    }

    //opencode: press Z (white 0) — note on, active voice at C4.
    gX_Tests_SynthVis_SetKey(gX_KEY_Z, 1);
    gX_SceneSynthVis_Update(&vis);
    if (!vis.keysPressed[0] || vis.whiteVoiceID[0] < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis Z did not start a note");
        errors++;
    } else {
        gX_Synth_Voice *voice = gX_Synth_GetVoice((gX_U32)vis.whiteVoiceID[0]);
        if (!voice || !voice->active || fabsf(voice->frequency - 261.63f) > 0.6f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis Z voice state/frequency");
            errors++;
        }
    }

    //opencode: hold a three-note chord across the rows (Z + X whites, S sharp).
    gX_Tests_SynthVis_SetKey(gX_KEY_X, 1);
    gX_Tests_SynthVis_SetKey(gX_KEY_S, 1);
    gX_SceneSynthVis_Update(&vis);
    gX_I32 chordVoices = 0;
    for (gX_I32 i = 0; i < 14; i++) if (vis.whiteVoiceID[i] >= 0) chordVoices++;
    for (gX_I32 i = 0; i < 10; i++) if (vis.sharpVoiceID[i] >= 0) chordVoices++;
    if (chordVoices != 3 || vis.sharpVoiceID[0] < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis chord voice count (%i)", chordVoices);
        errors++;
    }
    gX_Synth_Voice *sharp = gX_Synth_GetVoice((gX_U32)vis.sharpVoiceID[0]);
    if (!sharp || fabsf(sharp->frequency - 277.18f) > 0.6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis S sharp frequency");
        errors++;
    }

    //opencode: release — Z drops to release, key row clears.
    gX_I32 zVoice = vis.whiteVoiceID[0];
    gX_Tests_SynthVis_SetKey(gX_KEY_Z, 0);
    gX_SceneSynthVis_Update(&vis);
    if (vis.keysPressed[0] || vis.whiteVoiceID[0] != -1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis Z release did not clear");
        errors++;
    }
    gX_Synth_Voice *released = gX_Synth_GetVoice((gX_U32)zVoice);
    if (!released || !released->release) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis Z voice did not enter release");
        errors++;
    }

    //opencode: canvas requests — canvas 0 (scene) + canvas 1 (keyboard).
    gui.makeAmount = 0;
    gui.make = false;
    gX_SceneSynthVis_Update(&vis);
    if (gui.makeAmount != 2) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis canvas requests (%i)", gui.makeAmount);
        errors++;
    }

    //opencode: upper-row mapping — Q is white key 10 (F5; the gameX row runs
    //   Z..M / , . / = C4..E5, then Q W E R = F5..B5).
    gX_Tests_SynthVis_SetKey(gX_KEY_Q, 1);
    gX_SceneSynthVis_Update(&vis);
    if (vis.whiteVoiceID[10] < 0
        || fabsf(gX_Synth_GetVoice((gX_U32)vis.whiteVoiceID[10])->frequency - 698.46f) > 0.6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis Q upper octave mapping");
        errors++;
    }

    //opencode: Park is the scene-exit hook: key state cleared, voices released.
    gX_SceneSynthVis_Park(&vis);
    for (gX_I32 i = 0; i < 14; i++) {
        if (vis.whiteVoiceID[i] != -1 || vis.keysPressed[i]) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis park kept white key %i", i);
            errors++;
        }
    }
    for (gX_I32 i = 0; i < 10; i++) {
        if (vis.sharpVoiceID[i] != -1 || vis.keysSharpPressed[i]) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis park kept sharp key %i", i);
            errors++;
        }
    }
    if (g_synth.arp.heldCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis park kept arp notes");
        errors++;
    }

    gX_SceneSynthVis_Unload(&vis);
    if (vis.loaded) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: synthvis unload kept loaded");
        errors++;
    }

    //opencode: restore the globals the test borrowed.
    input = savedInput;
    gui.makeAmount = savedMakeAmount;
    gui.make = savedMake;

    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GX_TESTS: synthvis scene lifecycle — 0 errors");
    }
    return errors;
}
