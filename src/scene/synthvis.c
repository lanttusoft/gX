/**********************************************************************************************
*
*   [gX: src/scene/synthvis.c]
*   Synth visualizer scene — playable keyboard + visualizer composite
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


//opencode: gameX synthvis port (gX-native): the scene owns the keyboard input
//   (physical keys drive gX_Synth_* notes) and the canvas composite; the
//   renderer's oscillo/lissajous/spectrum modules own the visualizers — their
//   persistent RTs are drawn by the scene and composited into renderer.canvas,
//   with the GUI keyboard canvas on top.
#include "synthvis.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../input.h"
#include "../gui/gui.h"
#include "../gui/enum.h"
#include "../audio/synth.h"
#include "../renderer/renderer.h"
#include "../renderer/oscillo.h"
#include "../renderer/lissajous.h"
#include "../renderer/spectrum.h"
#include "../wrappers/wrappers.h"

//opencode: piano key tables C4–B5 — the gameX key rows: white keys
//   Z X C V B N M , . / Q W E R, sharps S D G H J 2 3 5 6 7.
static const gX_F32 g_whiteFreqs[14] = {
    261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f,
    523.25f, 587.33f, 659.25f, 698.46f, 783.99f, 880.00f, 987.77f
};
static const gX_F32 g_sharpFreqs[10] = {
    277.18f, 311.13f, 369.99f, 415.30f, 466.16f,
    554.37f, 622.25f, 739.99f, 830.61f, 932.33f
};

static const gX_I32 g_whiteKeyCodes[14] = {
    gX_KEY_Z, gX_KEY_X, gX_KEY_C, gX_KEY_V, gX_KEY_B, gX_KEY_N, gX_KEY_M,
    gX_KEY_COMMA, gX_KEY_PERIOD, gX_KEY_SLASH,
    gX_KEY_Q, gX_KEY_W, gX_KEY_E, gX_KEY_R
};
static const gX_I32 g_sharpKeyCodes[10] = {
    gX_KEY_S, gX_KEY_D, gX_KEY_G, gX_KEY_H, gX_KEY_J,
    gX_KEY_TWO, gX_KEY_THREE, gX_KEY_FIVE, gX_KEY_SIX, gX_KEY_SEVEN
};

//opencode: black key centers in white-key widths (gameX offsets: E/F and B/C
//   gaps have no sharp).
static const gX_F32 g_sharpOffsets[10] = {
    0.7f, 1.7f, 3.7f, 4.7f, 5.7f,
    7.7f, 8.7f, 10.7f, 11.7f, 12.7f
};

gX_Rectangle gX_SceneSynthVis_WhiteKeyRect(gX_I32 index, gX_F32 width, gX_F32 height) {
    gX_F32 kw = width / 14.0f;
    gX_F32 kh = height * gX_SYNTHVIS_KEYBOARD_FRACTION;
    if (index < 0) index = 0;
    if (index > 13) index = 13;
    return (gX_Rectangle){(gX_F32)index * kw, height - kh, kw - 1.0f, kh};
}

gX_Rectangle gX_SceneSynthVis_BlackKeyRect(gX_I32 index, gX_F32 width, gX_F32 height) {
    gX_F32 kw = width / 14.0f;
    gX_F32 kh = height * gX_SYNTHVIS_KEYBOARD_FRACTION;
    gX_F32 bw = kw * 0.6f;
    gX_F32 bh = kh * 0.62f;
    if (index < 0) index = 0;
    if (index > 9) index = 9;
    return (gX_Rectangle){g_sharpOffsets[index] * kw, height - kh, bw, bh};
}

//opencode: capture/demo hook — GX_SYNTHVIS_AUTOPLAY=1 holds a chord on the
//   arpeggiator so the visualizers have a live signal without input.
static gX_I0 gX_SceneSynthVis_Autoplay(gX_I0) {
    const gX_I8 *on = getenv("GX_SYNTHVIS_AUTOPLAY");
    if (!on || on[0] == '0' || on[0] == '\0') return;
    gX_Synth_Arp_SetMode(GX_SYNTH_ARP_UP);
    gX_Synth_Arp_SetRate(0.18f);
    gX_Synth_Arp_SetGate(0.5f);
    gX_Synth_Arp_NoteOn(220.00f, 0.7f);
    gX_Synth_Arp_NoteOn(261.63f, 0.7f);
    gX_Synth_Arp_NoteOn(329.63f, 0.7f);
}

//opencode: one key row — note on at the press edge, note off at the release
//   edge (voice index guarded so a stolen/refused voice is never double-freed).
//   Reads the engine input state (not the backend query) so the same path is
//   headless-testable.
static gX_I0 gX_SceneSynthVis_Key(gX_I1 *pressed, gX_I32 *voiceID,
        gX_I32 keyCode, gX_F32 freq) {
    gX_I1 nowPressed = input.keyboard.keys[keyCode].state.DOWN ? 1 : 0;
    if (nowPressed && !(*pressed)) {
        *voiceID = gX_Synth_NoteOn(freq, 0.7f, g_synth.defaultWaveform);
    } else if (!nowPressed && (*pressed)) {
        if (*voiceID >= 0) {
            gX_Synth_NoteOff((gX_U32)(*voiceID));
            *voiceID = -1;
        }
    }
    *pressed = nowPressed;
}

gX_I32 gX_SceneSynthVis_Load(gX_SceneSynthVis *vis) {
    gX_I32 errors = 0;

    strcpy(vis->name, "SynthVis");
    strcpy(vis->description,
        "playable synth keyboard with oscilloscope, spectrum and lissajous visuals");
    gX_TraceLog_SCENE(gX_LOG_GREEN, "name: %s", vis->name);

    memset(vis->keysPressed, 0, sizeof(vis->keysPressed));
    memset(vis->keysSharpPressed, 0, sizeof(vis->keysSharpPressed));
    for (gX_I32 i = 0; i < 14; i++) vis->whiteVoiceID[i] = -1;
    for (gX_I32 i = 0; i < 10; i++) vis->sharpVoiceID[i] = -1;
    vis->firstFrame = 1;
    vis->loaded = true;

    //opencode: the GUI canvases are requested per-frame from Update — a
    //   one-shot build here would die to the scene-switch clearAll.
    return errors;
}

gX_I32 gX_SceneSynthVis_Unload(gX_SceneSynthVis *vis) {
    gX_Synth_AllNotesOff();
    gX_Synth_Arp_AllNotesOff();
    vis->loaded = false;
    return 0;
}

//opencode: scene-exit hook — stop the arp and drop held-key state so leaving
//   the scene cannot leave notes running or keys stuck.
gX_I0 gX_SceneSynthVis_Park(gX_SceneSynthVis *vis) {
    gX_Synth_AllNotesOff();
    gX_Synth_Arp_AllNotesOff();
    memset(vis->keysPressed, 0, sizeof(vis->keysPressed));
    memset(vis->keysSharpPressed, 0, sizeof(vis->keysSharpPressed));
    for (gX_I32 i = 0; i < 14; i++) vis->whiteVoiceID[i] = -1;
    for (gX_I32 i = 0; i < 10; i++) vis->sharpVoiceID[i] = -1;
    vis->firstFrame = 1;
}

gX_I32 gX_SceneSynthVis_Update(gX_SceneSynthVis *vis) {
    gX_I32 errors = 0;

    if (vis->firstFrame) {
        vis->firstFrame = 0;
        gX_TraceLog(gX_LOG_INFO, "SYNTHVIS: started");
        gX_SceneSynthVis_Autoplay();
    }

    for (gX_I32 w = 0; w < 14; w++) {
        gX_SceneSynthVis_Key(&vis->keysPressed[w], &vis->whiteVoiceID[w],
            g_whiteKeyCodes[w], g_whiteFreqs[w]);
    }
    for (gX_I32 s = 0; s < 10; s++) {
        gX_SceneSynthVis_Key(&vis->keysSharpPressed[s], &vis->sharpVoiceID[s],
            g_sharpKeyCodes[s], g_sharpFreqs[s]);
    }

    //opencode: keyboard state changes every frame (pressed colors), and the
    //   scene-switch clearAll wipes one-shot builds — request both canvases
    //   each update; the make pass clears + rebuilds them.
    gX_GUI_AddMake(gX_GUI_TEMPLATE_SCENE, 0);
    gX_GUI_AddMake(gX_GUI_TEMPLATE_SYNTHVIS_KEYBOARD, 1);
    return errors;
}

gX_I32 gX_SceneSynthVis_Draw(gX_SceneSynthVis *vis) {
    gX_I32 errors = 0;
    (gX_I0)vis;
    if (!renderer.loaded || renderer.canvas.texture.id == 0) return errors;

    gX_F32 w = (gX_F32)maindata.renderWidth;
    gX_F32 h = (gX_F32)maindata.renderHeight;
    gX_F32 keyH = h * gX_SYNTHVIS_KEYBOARD_FRACTION;
    gX_F32 panH = h - keyH;

    //opencode: panel layout — oscillo top-left (60%), spectrum top-right (40%),
    //   lissajous square centered in the band above the keyboard.
    gX_F32 topH = panH * 0.62f;
    gX_Rectangle oscPanel = {0.0f, 0.0f, w * 0.6f, topH};
    gX_Rectangle specPanel = {w * 0.6f, 0.0f, w * 0.4f, topH};
    gX_F32 bandH = panH - topH;
    gX_F32 side = w * 0.45f;
    if (side > bandH) side = bandH;
    gX_Rectangle lisPanel = {
        (w - side) * 0.5f, topH + (bandH - side) * 0.5f, side, side};

    //opencode: refresh the module RTs first so the composite shows this frame;
    //   the scene-config draw flags for these modules are off (scene owns it).
    //   Each DrawTo binds/unbinds its own FBO (EndTexture unbinds), so these
    //   MUST run before the canvas pass — nesting them inside it would drop
    //   the rest of the canvas draw onto the default framebuffer.
    gX_RendererOscillo *osc = renderer.oscillo.data;
    gX_RendererLissajous *lis = renderer.lissajous.data;
    gX_RendererSpectrum *spec = renderer.spectrum.data;
    if (osc) gX_RendererOscillo_DrawTo(osc, NULL);
    if (lis) gX_RendererLissajous_DrawTo(lis, NULL);
    if (spec) gX_RendererSpectrum_DrawTo(spec, NULL);

    gX_Drawing_BeginTexture(renderer.canvas);
        gX_Drawing_ClearBG((gX_Color){10, 5, 20, 255});

        //opencode: panel beds + frames stay visible while the scope is silent
        gX_Rectangle_Draw(oscPanel, (gX_Color){5, 3, 12, 255});
        gX_Rectangle_Draw(specPanel, (gX_Color){5, 3, 12, 255});
        gX_Rectangle_Draw(lisPanel, (gX_Color){5, 3, 12, 255});
        gX_Rectangle_Draw_Border(oscPanel, (gX_Color){30, 70, 55, 255});
        gX_Rectangle_Draw_Border(specPanel, (gX_Color){30, 70, 55, 255});
        gX_Rectangle_Draw_Border(lisPanel, (gX_Color){30, 70, 110, 255});

        //opencode: raylib-native rects defer to the queue flush while the batch
        //   composites execute immediately — flush the beds/frames first or
        //   they land over the RT composites.
        gX_Drawing_Flush();

        //opencode: composite only fed panels — an unfed RT is uninitialized.
        if (osc && g_synthScopeCount >= 2) {
            gX_Texture_Draw(osc->rt.texture, (gX_Texture_Draw_Params){
                .dst = oscPanel, .tint = gX_WHITE, .scale = {1, 1}});
        }
        if (spec && g_synthScopeCount >= 8) {
            gX_Texture_Draw(spec->rt.texture, (gX_Texture_Draw_Params){
                .dst = specPanel, .tint = gX_WHITE, .scale = {1, 1}});
        }
        if (lis && g_synthScopeCount >= 4) {
            gX_Texture_Draw(lis->rt.texture, (gX_Texture_Draw_Params){
                .dst = lisPanel, .tint = gX_WHITE, .scale = {1, 1}});
        }
    gX_Drawing_EndTexture();
    return errors;
}
