/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/sound.h]
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


#ifndef GUI_TEMPLATES_MACROS_SOUND_H
#define GUI_TEMPLATES_MACROS_SOUND_H

#define GUI_TEMPLATE_ELEMENT_SOUND_ADD(snd) \
    if (soundOK) {  \
        currentSound->sound = (snd); \
    } \

#define GUI_TEMPLATE_ELEMENT_SOUND_ADDR(addr) \
    if (soundOK) {  \
        currentSound->sound = &(addr); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_SOUND_VOLPITPAN(vol,pit,pa) \
    if (soundOK) {  \
        currentSound->params.volume = (vol); \
        currentSound->params.pitch = (pit); \
        currentSound->params.pan = (pa); \
    } \

#endif //GUI_TEMPLATES_MACROS_SOUND_H
