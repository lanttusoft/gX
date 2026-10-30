/**********************************************************************************************
*
*   [gX: src/gui/element/sound.c]
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


#include <string.h>

#include "element.h"
#include "modules.h"

#include "../../wrappers/wrappers.h"
#include "../../audio.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../main.h"

inline gX_I0 gX_GUI_Element_Sound_Init(gX_GUI_Element_Sound *sound) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing sound values");
    
    sound->sound = 0x0;
    
    sound->params.play = false;
    sound->params.pause = false;
    sound->params.stop = false;
    sound->params.resume = false;
    sound->params.playing = false;
    
    sound->params.volume = 1.0f;
    sound->params.pitch = 1.0f;
    sound->params.pan = 0.5f;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Sound_Update(gX_GUI_Element_Sound *sound, gX_GUI_Element *element) {
    
    gX_I1 execute = false;
    
    //opencode: independent ifs (was else-if) so multi-condition triggers all fire
    if (sound->execute.hovered      && element->state.HOVERED)         { execute = true; }
    if (sound->execute.toggled      && element->state.TOGGLED)         { execute = true; } 
    if (sound->execute.clickedLeft  && element->state.CLICKED_LEFT)    { execute = true; }
    if (sound->execute.clickedRight && element->state.CLICKED_RIGHT)   { execute = true; }
    if (sound->execute.pressedLeft  && element->state.PRESSED_LEFT)    { execute = true; }
    if (sound->execute.pressedRight && element->state.PRESSED_RIGHT)   { execute = true; }
    if (!execute) sound->execute.done = false;
    
    if (sound->execute.once && sound->execute.done) { execute = false; }
    
    if (execute) {
        gX_Sound *snd = sound->sound;
        //ai: route through the SFX bus so the mixer's effects gain and the
        //   global mute apply to GUI feedback too.
        if (snd) gX_Audio_Sound_PlayBus(snd, sound->params);
        sound->execute.done = true;
    }
    
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Sound,sound)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Sound,sound)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Sound,sound,SOUND)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Sound,sound)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Sound,sound)
//opencode: inlined from LOOP macro for navigability
gX_I0 gX_GUI_Element_Update_Sounds(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Update_Sounds(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->sounds;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Sound *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Sound *elementmodule = &heapPtr[i];
            gX_GUI_Element_Sound_Update(elementmodule,element);
        }
    }
}
