/**********************************************************************************************
*
*   [gX: src/gui/element/textinput.h]
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


#ifndef GUI_ELEMENT_TEXTINPUT_H
#define GUI_ELEMENT_TEXTINPUT_H

#include "../../common/types.h"
#include "../config.h"
#include "struct_executestate.h"

//opencode: textinput widget module — editable single-line text with cursor,
//   selection, clipboard (Ctrl+C/V/X), placeholder, password mode, and an
//   optional commit callback (Enter). The buffer is a sized array stored in
//   the flat module heap (same pattern as Text).

//opencode: single undo snapshot — full buffer plus cursor state.
typedef struct gX_GUI_TextInput_Snapshot {
    gX_I8   text[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32  length;
    gX_U32  cursorPos;
    gX_U32  selectionStart;
} gX_GUI_TextInput_Snapshot;
#define GX_TI_UNDO_MAX 16

typedef struct gX_GUI_Element_TextInput {
    gX_I8   text[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32  length;
    gX_U32  cursorPos;        // insertion point (UTF-8 byte index)
    gX_U32  selectionStart;   // selection anchor (UTF-8 byte index)
    gX_I1   hasSelection;
    gX_I1   passwordMode;     // mask with '*' when drawing
    gX_I8   placeholder[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32  maxLength;
    gX_I1   committed;        // one-shot flag: Enter fired since last clear
    //opencode: undo/redo — snapshots pushed before each user mutation;
    //   programmatic SetText clears both stacks (build-time init, no undo).
    gX_GUI_TextInput_Snapshot undoStack[GX_TI_UNDO_MAX];
    gX_U32  undoCount;
    gX_GUI_TextInput_Snapshot redoStack[GX_TI_UNDO_MAX];
    gX_U32  redoCount;

    gX_I0 (*onCommit)(gX_I0 *userData);
    gX_I0 *onCommitUserData;

    //opencode: optional external buffer — written every frame while focused so
    //   the caller always sees the current text (runtime-only, not serialized).
    //   externalBufferCapacity is the full byte size of the caller's buffer;
    //   the sync never writes past capacity-1 (audit CRITICAL #1 fix).
    gX_I8 *externalBuffer;
    gX_U32 externalBufferCapacity;

    gX_F32  blinkTimer;       // cursor blink accumulator
    gX_I1   cursorVisible;
} gX_GUI_Element_TextInput;

//opencode: public setters — used by the builder, DOM API, and scripts. Keyboard
//   events are ASCII (the engine input queue contract); programmatic and
//   clipboard text is retained and edited as UTF-8.
gX_I0 gX_GUI_Element_TextInput_SetText(gX_GUI_Element_TextInput *ti, const gX_I8 *text);
gX_I0 gX_GUI_Element_TextInput_SetPlaceholder(gX_GUI_Element_TextInput *ti, const gX_I8 *text);
gX_I0 gX_GUI_Element_TextInput_SetPassword(gX_GUI_Element_TextInput *ti, gX_I1 enabled);
gX_I0 gX_GUI_Element_TextInput_SetMaxLength(gX_GUI_Element_TextInput *ti, gX_U32 maxLength);
gX_I0 gX_GUI_Element_TextInput_SetOnCommit(gX_GUI_Element_TextInput *ti,
    gX_I0 (*onCommit)(gX_I0 *userData), gX_I0 *userData);
gX_I0 gX_GUI_Element_TextInput_SetExternalBuffer(gX_GUI_Element_TextInput *ti,
    gX_I8 *buffer, gX_U32 capacity);
const gX_I8 *gX_GUI_Element_TextInput_GetText(const gX_GUI_Element_TextInput *ti);
//opencode: exposed for unit tests (used by Update before its definition) —
//   mechanical decl for the in-progress buffer-sync WIP.
gX_I0 gX_GUI_Element_TextInput_SyncExternalBuffer(gX_GUI_Element_TextInput *ti);
//opencode: word navigation and undo — codepoint-aware, headless-testable.
gX_U32 gX_GUI_Element_TextInput_WordLeft(const gX_GUI_Element_TextInput *ti, gX_U32 pos);
gX_U32 gX_GUI_Element_TextInput_WordRight(const gX_GUI_Element_TextInput *ti, gX_U32 pos);
gX_I0 gX_GUI_Element_TextInput_Undo(gX_GUI_Element_TextInput *ti);
gX_I0 gX_GUI_Element_TextInput_Redo(gX_GUI_Element_TextInput *ti);
gX_I0 gX_GUI_Element_TextInput_DeleteWordLeft(gX_GUI_Element_TextInput *ti);
gX_I0 gX_GUI_Element_TextInput_DeleteWordRight(gX_GUI_Element_TextInput *ti);

#endif
