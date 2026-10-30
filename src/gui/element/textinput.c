/**********************************************************************************************
*
*   [gX: src/gui/element/textinput.c]
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


#include <string.h>

#include "element.h"
#include "../canvas.h"
#include "../gui.h"
#include "modules.h"

#include "../../input.h"
#include "../../common/enum/keys.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../main.h"
#include "../../wrappers/wrappers.h"
#include "../../wrappers/misc.h"

//opencode: textinput widget module — editing, selection, clipboard, cursor.
//   Keyboard input is consumed only when the element is FOCUSED.

inline gX_I0 gX_GUI_Element_TextInput_Init(gX_GUI_Element_TextInput *ti) {
    ti->text[0] = '\0';
    ti->length = 0;
    ti->cursorPos = 0;
    ti->selectionStart = 0;
    ti->hasSelection = false;
    ti->passwordMode = false;
    ti->placeholder[0] = '\0';
    ti->maxLength = gX_GUI_ELEMENT_TEXT_MAX - 1;
    ti->committed = false;
    ti->undoCount = 0;
    ti->redoCount = 0;
    ti->onCommit = NULL;
    ti->onCommitUserData = NULL;
    ti->externalBuffer = NULL;
    ti->externalBufferCapacity = 0;
    ti->blinkTimer = 0.0f;
    ti->cursorVisible = true;
}

//opencode: mechanical build unblock for the in-progress UTF-8/buffer WIP —
//   these helpers are defined later in this file. Not an audit repair.
static gX_U32 gX_TI_Utf8Cut(const gX_I8 *s, gX_U32 n);
static gX_U32 gX_TI_SeqLenAt(const gX_I8 *s, gX_U32 i, gX_U32 len);
static gX_U32 gX_TI_PrevSeqStart(const gX_I8 *s, gX_U32 pos);
static gX_I0 gX_TI_DeleteSelection(gX_GUI_Element_TextInput *ti);

//opencode: public setters used by the builder and scripts
gX_I0 gX_GUI_Element_TextInput_SetText(gX_GUI_Element_TextInput *ti, const gX_I8 *text) {
    if (!ti || !text) return;
    gX_U32 n = (gX_U32)strlen(text);
    if (n > ti->maxLength) n = ti->maxLength;
    //opencode: never leave a truncated UTF-8 sequence in the buffer
    n = gX_TI_Utf8Cut(text, n);
    memcpy(ti->text, text, n);
    ti->text[n] = '\0';
    ti->length = n;
    ti->cursorPos = n;
    ti->selectionStart = n;
    ti->hasSelection = false;
    //opencode: programmatic set is build-time init, not a user edit — no undo.
    ti->undoCount = 0;
    ti->redoCount = 0;
}

gX_I0 gX_GUI_Element_TextInput_SetPlaceholder(gX_GUI_Element_TextInput *ti, const gX_I8 *text) {
    if (!ti || !text) return;
    gX_U32 n = (gX_U32)strlen(text);
    if (n >= sizeof(ti->placeholder)) n = sizeof(ti->placeholder) - 1;
    n = gX_TI_Utf8Cut(text, n);
    memcpy(ti->placeholder, text, n);
    ti->placeholder[n] = '\0';
}

gX_I0 gX_GUI_Element_TextInput_SetPassword(gX_GUI_Element_TextInput *ti, gX_I1 enabled) {
    if (ti) ti->passwordMode = enabled;
}

gX_I0 gX_GUI_Element_TextInput_SetMaxLength(gX_GUI_Element_TextInput *ti, gX_U32 maxLength) {
    if (!ti) return;
    if (maxLength > gX_GUI_ELEMENT_TEXT_MAX - 1) maxLength = gX_GUI_ELEMENT_TEXT_MAX - 1;
    ti->maxLength = maxLength;
    if (ti->length > maxLength) {
        //opencode: shrink on a UTF-8 boundary — never split a codepoint
        gX_U32 n = gX_TI_Utf8Cut(ti->text, maxLength);
        ti->text[n] = '\0';
        ti->length = n;
        if (ti->cursorPos > n) ti->cursorPos = n;
        if (ti->selectionStart > n) ti->selectionStart = n;
    }
}

gX_I0 gX_GUI_Element_TextInput_SetOnCommit(gX_GUI_Element_TextInput *ti,
    gX_I0 (*onCommit)(gX_I0 *userData), gX_I0 *userData) {
    if (!ti) return;
    ti->onCommit = onCommit;
    ti->onCommitUserData = userData;
}

//opencode: attach a caller-owned buffer that the module keeps in sync while
//   focused. capacity must be the full byte size of buffer; the sync never
//   writes past capacity-1. Pass NULL or 0 to disable syncing.
gX_I0 gX_GUI_Element_TextInput_SetExternalBuffer(gX_GUI_Element_TextInput *ti,
    gX_I8 *buffer, gX_U32 capacity) {
    if (!ti) return;
    if (!buffer || capacity == 0) {
        ti->externalBuffer = NULL;
        ti->externalBufferCapacity = 0;
        return;
    }
    ti->externalBuffer = buffer;
    ti->externalBufferCapacity = capacity;
}

const gX_I8 *gX_GUI_Element_TextInput_GetText(const gX_GUI_Element_TextInput *ti) {
    return ti ? ti->text : "";
}

//=============================================================================
// UTF-8 helpers (audit finding #9 — keep edits on codepoint boundaries)
//=============================================================================

//opencode: clamp a byte count to the nearest valid UTF-8 sequence boundary so
//   truncation never leaves a split multi-byte codepoint in the buffer. s is a
//   NUL-terminated string; n must be <= strlen(s).
static gX_U32 gX_TI_Utf8Cut(const gX_I8 *s, gX_U32 n) {
    if (n == 0) return 0;
    gX_U32 lead = n;
    gX_U32 back = 0;
    while (back < 4 && lead > 0 && ((gX_U8)s[lead - 1] & 0xC0) == 0x80) { lead--; back++; }
    if (lead == 0) return 0; // only continuation bytes — malformed tail
    gX_U8 b = (gX_U8)s[lead - 1];
    gX_U32 expect = 0;
    if      ((b & 0xE0) == 0xC0) expect = 2;
    else if ((b & 0xF0) == 0xE0) expect = 3;
    else if ((b & 0xF8) == 0xF0) expect = 4;
    if (expect == 0) return n; // ASCII byte — clean cut
    if (lead - 1 + expect <= n) return n; // sequence completes within n
    return lead - 1; // incomplete tail — cut before the sequence
}

//opencode: byte length of the UTF-8 sequence starting at s[i], clamped to the
//   buffer end so truncated/malformed input is removed without OOB reads.
static gX_U32 gX_TI_SeqLenAt(const gX_I8 *s, gX_U32 i, gX_U32 len) {
    if (i >= len) return 0;
    gX_U8 b = (gX_U8)s[i];
    if ((b & 0x80) == 0) return 1;
    gX_U32 expect = 0;
    if      ((b & 0xE0) == 0xC0) expect = 2;
    else if ((b & 0xF0) == 0xE0) expect = 3;
    else if ((b & 0xF8) == 0xF0) expect = 4;
    else return 1; // stray continuation byte
    while (expect > 1 && i + expect > len) expect--;
    return expect;
}

//opencode: byte index of the start of the UTF-8 sequence whose last byte is at
//   pos-1 (the character just before the cursor).
static gX_U32 gX_TI_PrevSeqStart(const gX_I8 *s, gX_U32 pos) {
    if (pos == 0) return 0;
    gX_U32 i = pos;
    gX_U32 back = 0;
    while (back < 3 && i > 0 && ((gX_U8)s[i - 1] & 0xC0) == 0x80) { i--; back++; }
    if (i == pos) return pos - 1; // single byte (ASCII or stray lead)
    return i; // lead byte index
}

//=============================================================================
// undo + word navigation (phase 3 — codepoint-aware, headless-testable)
//=============================================================================

//opencode: snapshot current state before a user mutation; drops the oldest
//   entry when full and clears the redo tail (new edit forks history).
static gX_I0 gX_TI_PushUndo(gX_GUI_Element_TextInput *ti) {
    if (!ti) return;
    if (ti->undoCount >= GX_TI_UNDO_MAX) {
        memmove(&ti->undoStack[0], &ti->undoStack[1],
            sizeof(ti->undoStack[0]) * (GX_TI_UNDO_MAX - 1));
        ti->undoCount = GX_TI_UNDO_MAX - 1;
    }
    gX_GUI_TextInput_Snapshot *s = &ti->undoStack[ti->undoCount++];
    memcpy(s->text, ti->text, ti->length + 1);
    s->length = ti->length;
    s->cursorPos = ti->cursorPos;
    s->selectionStart = ti->selectionStart;
    ti->redoCount = 0;
}

//opencode: restore a snapshot (shared by undo and redo).
static gX_I0 gX_TI_Restore(gX_GUI_Element_TextInput *ti, const gX_GUI_TextInput_Snapshot *s) {
    if (!ti || !s) return;
    gX_U32 n = s->length;
    if (n >= sizeof(ti->text)) n = sizeof(ti->text) - 1;
    n = gX_TI_Utf8Cut(s->text, n);
    memcpy(ti->text, s->text, n);
    ti->text[n] = '\0';
    ti->length = n;
    ti->cursorPos = s->cursorPos > n ? n : s->cursorPos;
    ti->selectionStart = s->selectionStart > n ? n : s->selectionStart;
    ti->hasSelection = (ti->selectionStart != ti->cursorPos);
}

//opencode: undo the last user mutation; redo the last undone mutation.
gX_I0 gX_GUI_Element_TextInput_Undo(gX_GUI_Element_TextInput *ti) {
    if (!ti || ti->undoCount == 0) return;
    if (ti->redoCount >= GX_TI_UNDO_MAX) {
        memmove(&ti->redoStack[0], &ti->redoStack[1],
            sizeof(ti->redoStack[0]) * (GX_TI_UNDO_MAX - 1));
        ti->redoCount = GX_TI_UNDO_MAX - 1;
    }
    gX_GUI_TextInput_Snapshot *r = &ti->redoStack[ti->redoCount++];
    memcpy(r->text, ti->text, ti->length + 1);
    r->length = ti->length;
    r->cursorPos = ti->cursorPos;
    r->selectionStart = ti->selectionStart;
    gX_TI_Restore(ti, &ti->undoStack[--ti->undoCount]);
}

//opencode: redo the last undone mutation.
gX_I0 gX_GUI_Element_TextInput_Redo(gX_GUI_Element_TextInput *ti) {
    if (!ti || ti->redoCount == 0) return;
    if (ti->undoCount >= GX_TI_UNDO_MAX) {
        memmove(&ti->undoStack[0], &ti->undoStack[1],
            sizeof(ti->undoStack[0]) * (GX_TI_UNDO_MAX - 1));
        ti->undoCount = GX_TI_UNDO_MAX - 1;
    }
    gX_GUI_TextInput_Snapshot *u = &ti->undoStack[ti->undoCount++];
    memcpy(u->text, ti->text, ti->length + 1);
    u->length = ti->length;
    u->cursorPos = ti->cursorPos;
    u->selectionStart = ti->selectionStart;
    gX_TI_Restore(ti, &ti->redoStack[--ti->redoCount]);
}

//opencode: word character — ASCII alnum/underscore or any non-ASCII codepoint
//   (CJK runs count as words); spaces and punctuation delimit.
static gX_I1 gX_TI_IsWordLead(gX_U8 b) {
    if (b >= 0x80) return true;
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z')
        || (b >= '0' && b <= '9') || b == '_';
}

//opencode: start-of-word to the left of pos (Ctrl+Left target).
gX_U32 gX_GUI_Element_TextInput_WordLeft(const gX_GUI_Element_TextInput *ti, gX_U32 pos) {
    gX_U32 i = 0;
    if (!ti) return 0;
    if (pos > ti->length) pos = ti->length;
    i = pos;
    while (i > 0) {
        gX_U32 s = gX_TI_PrevSeqStart(ti->text, i);
        if (gX_TI_IsWordLead((gX_U8)ti->text[s])) break;
        i = s;
    }
    while (i > 0) {
        gX_U32 s = gX_TI_PrevSeqStart(ti->text, i);
        if (!gX_TI_IsWordLead((gX_U8)ti->text[s])) break;
        i = s;
    }
    return i;
}

//opencode: start of the next word at or after pos (Ctrl+Right target).
gX_U32 gX_GUI_Element_TextInput_WordRight(const gX_GUI_Element_TextInput *ti, gX_U32 pos) {
    gX_U32 i = 0;
    if (!ti) return 0;
    if (pos > ti->length) pos = ti->length;
    i = pos;
    while (i < ti->length) {
        if (!gX_TI_IsWordLead((gX_U8)ti->text[i])) break;
        i += gX_TI_SeqLenAt(ti->text, i, ti->length);
    }
    while (i < ti->length) {
        if (gX_TI_IsWordLead((gX_U8)ti->text[i])) break;
        i += gX_TI_SeqLenAt(ti->text, i, ti->length);
    }
    return i;
}

//opencode: delete the word to the left of the cursor (Ctrl+Backspace).
gX_I0 gX_GUI_Element_TextInput_DeleteWordLeft(gX_GUI_Element_TextInput *ti) {
    gX_U32 w = 0;
    if (!ti) return;
    if (ti->hasSelection) {
        gX_TI_PushUndo(ti);
        gX_TI_DeleteSelection(ti);
        return;
    }
    w = gX_GUI_Element_TextInput_WordLeft(ti, ti->cursorPos);
    if (w == ti->cursorPos) return;
    gX_TI_PushUndo(ti);
    memmove(&ti->text[w], &ti->text[ti->cursorPos], ti->length - ti->cursorPos + 1);
    ti->length -= (ti->cursorPos - w);
    ti->cursorPos = w;
    ti->selectionStart = w;
}

//opencode: delete the word at or after the cursor (Ctrl+Delete).
gX_I0 gX_GUI_Element_TextInput_DeleteWordRight(gX_GUI_Element_TextInput *ti) {
    gX_U32 w = 0;
    if (!ti) return;
    if (ti->hasSelection) {
        gX_TI_PushUndo(ti);
        gX_TI_DeleteSelection(ti);
        return;
    }
    w = gX_GUI_Element_TextInput_WordRight(ti, ti->cursorPos);
    if (w == ti->cursorPos) return;
    gX_TI_PushUndo(ti);
    memmove(&ti->text[ti->cursorPos], &ti->text[w], ti->length - w + 1);
    ti->length -= (w - ti->cursorPos);
    ti->selectionStart = ti->cursorPos;
}

//=============================================================================
// editing helpers
//=============================================================================

static gX_I0 gX_TI_DeleteSelection(gX_GUI_Element_TextInput *ti) {
    if (!ti->hasSelection) return;
    gX_U32 start = ti->selectionStart < ti->cursorPos ? ti->selectionStart : ti->cursorPos;
    gX_U32 end   = ti->selectionStart > ti->cursorPos ? ti->selectionStart : ti->cursorPos;
    if (end > ti->length) end = ti->length;
    if (end > start) {
        memmove(&ti->text[start], &ti->text[end], ti->length - end + 1);
        ti->length -= (end - start);
        ti->cursorPos = start;
        ti->selectionStart = start;
        ti->hasSelection = false;
    }
}

//opencode: insert a printable byte at the cursor. The selection is deleted
//   BEFORE the maxLength check so replacing selected text works even when the
//   field is full (audit finding #9). Exposed (not static) for unit tests.
gX_I0 gX_GUI_Element_TextInput_InsertChar(gX_GUI_Element_TextInput *ti, gX_I8 c) {
    if (!ti) return;
    if (c < 32 || c == 127) return; // non-printable
    if (!ti->hasSelection && ti->length >= ti->maxLength) return;
    gX_TI_PushUndo(ti);
    gX_TI_DeleteSelection(ti);
    if (ti->length >= ti->maxLength) return;
    // shift tail right to make room
    memmove(&ti->text[ti->cursorPos + 1], &ti->text[ti->cursorPos], ti->length - ti->cursorPos + 1);
    ti->text[ti->cursorPos] = c;
    ti->length++;
    ti->cursorPos++;
    ti->selectionStart = ti->cursorPos;
}

static gX_I0 gX_TI_Backspace(gX_GUI_Element_TextInput *ti) {
    if (!ti) return;
    if (ti->hasSelection) { gX_TI_PushUndo(ti); gX_TI_DeleteSelection(ti); return; }
    if (ti->cursorPos == 0) return;
    gX_TI_PushUndo(ti);
    //opencode: delete the whole UTF-8 sequence before the cursor, never a
    //   single continuation byte (keeps the buffer well-formed)
    gX_U32 start = gX_TI_PrevSeqStart(ti->text, ti->cursorPos);
    gX_U32 end = start + gX_TI_SeqLenAt(ti->text, start, ti->length);
    if (end < ti->cursorPos) end = ti->cursorPos; // cursor inside the sequence
    memmove(&ti->text[start], &ti->text[end], ti->length - end + 1);
    ti->length -= (end - start);
    ti->cursorPos = start;
    ti->selectionStart = start;
}

static gX_I0 gX_TI_Delete(gX_GUI_Element_TextInput *ti) {
    if (!ti) return;
    if (ti->hasSelection) { gX_TI_PushUndo(ti); gX_TI_DeleteSelection(ti); return; }
    if (ti->cursorPos >= ti->length) return;
    gX_TI_PushUndo(ti);
    //opencode: delete the whole UTF-8 sequence at the cursor (was one byte)
    gX_U32 seqLen = gX_TI_SeqLenAt(ti->text, ti->cursorPos, ti->length);
    memmove(&ti->text[ti->cursorPos], &ti->text[ti->cursorPos + seqLen], ti->length - ti->cursorPos - seqLen + 1);
    ti->length -= seqLen;
    ti->selectionStart = ti->cursorPos;
}

//opencode: clipboard — wrapped in #if so console backends with no OS clipboard
//   still compile (gX_Clipboard_Get returns "" there).
static gX_I0 gX_TI_Paste(gX_GUI_Element_TextInput *ti) {
    const gX_I8 *clip = gX_Clipboard_Get();
    if (!clip || !clip[0]) return;
    gX_TI_PushUndo(ti);
    gX_TI_DeleteSelection(ti);
    gX_U32 n = (gX_U32)strlen(clip);
    if (ti->length + n > ti->maxLength) {
        n = ti->maxLength - ti->length;
        //opencode: never paste a split codepoint
        n = gX_TI_Utf8Cut(clip, n);
    }
    for (gX_U32 i = 0; i < n; i++) {
        if (clip[i] < 32 || clip[i] == 127) continue;
        memmove(&ti->text[ti->cursorPos + 1], &ti->text[ti->cursorPos], ti->length - ti->cursorPos + 1);
        ti->text[ti->cursorPos] = clip[i];
        ti->length++;
        ti->cursorPos++;
    }
    ti->selectionStart = ti->cursorPos;
}

static gX_I0 gX_TI_CopySelection(gX_GUI_Element_TextInput *ti) {
    if (!ti->hasSelection) return;
    gX_U32 start = ti->selectionStart < ti->cursorPos ? ti->selectionStart : ti->cursorPos;
    gX_U32 end   = ti->selectionStart > ti->cursorPos ? ti->selectionStart : ti->cursorPos;
    if (end > ti->length) end = ti->length;
    gX_I8 buf[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32 n = end - start;
    memcpy(buf, &ti->text[start], n);
    buf[n] = '\0';
    gX_Clipboard_Set(buf);
}

//=============================================================================
// update — keyboard editing when focused
//=============================================================================

static gX_I0 gX_GUI_Element_TextInput_Update(gX_GUI_Element_TextInput *ti,
    gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    (void)canvas;
    if (!ti || !element) return;
    if (!element->state.FOCUSED) { ti->cursorVisible = true; ti->blinkTimer = 0; return; }

    //opencode: cursor blink — ~0.5s half-period
    ti->blinkTimer += (gX_F32)gX_FrameTime_Get();
    if (ti->blinkTimer >= 0.5f) {
        ti->blinkTimer = 0.0f;
        ti->cursorVisible = !ti->cursorVisible;
    }

    gX_I1 ctrl = input.keyboard.keys[gX_KEY_LEFT_CONTROL].state.DOWN
              || input.keyboard.keys[gX_KEY_RIGHT_CONTROL].state.DOWN;
    gX_I1 shift = input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN
               || input.keyboard.keys[gX_KEY_RIGHT_SHIFT].state.DOWN;

    // Ctrl+C / Ctrl+V / Ctrl+X / Ctrl+A / Ctrl+Z / Ctrl+Y
    if (ctrl) {
        if (input.keyboard.keys[gX_KEY_C].state.PRESSED) gX_TI_CopySelection(ti);
        if (input.keyboard.keys[gX_KEY_V].state.PRESSED) gX_TI_Paste(ti);
        if (input.keyboard.keys[gX_KEY_X].state.PRESSED) {
            gX_TI_CopySelection(ti);
            gX_TI_PushUndo(ti);
            gX_TI_DeleteSelection(ti);
        }
        if (input.keyboard.keys[gX_KEY_A].state.PRESSED) {
            ti->selectionStart = 0;
            ti->cursorPos = ti->length;
            ti->hasSelection = (ti->length > 0);
        }
        if (input.keyboard.keys[gX_KEY_Z].state.PRESSED && !shift) gX_GUI_Element_TextInput_Undo(ti);
        if (input.keyboard.keys[gX_KEY_Y].state.PRESSED) gX_GUI_Element_TextInput_Redo(ti);
        if (input.keyboard.keys[gX_KEY_Z].state.PRESSED && shift) gX_GUI_Element_TextInput_Redo(ti);
    }

    // navigation (Ctrl+arrows jump whole words)
    if (input.keyboard.keys[gX_KEY_LEFT].state.PRESSED) {
        if (shift && !ti->hasSelection) { ti->selectionStart = ti->cursorPos; ti->hasSelection = true; }
        if (ctrl) ti->cursorPos = gX_GUI_Element_TextInput_WordLeft(ti, ti->cursorPos);
        else ti->cursorPos = gX_TI_PrevSeqStart(ti->text, ti->cursorPos);
        if (!shift) { ti->selectionStart = ti->cursorPos; ti->hasSelection = false; }
    }
    if (input.keyboard.keys[gX_KEY_RIGHT].state.PRESSED) {
        if (shift && !ti->hasSelection) { ti->selectionStart = ti->cursorPos; ti->hasSelection = true; }
        if (ctrl) ti->cursorPos = gX_GUI_Element_TextInput_WordRight(ti, ti->cursorPos);
        else ti->cursorPos += gX_TI_SeqLenAt(ti->text, ti->cursorPos, ti->length);
        if (!shift) { ti->selectionStart = ti->cursorPos; ti->hasSelection = false; }
    }
    if (input.keyboard.keys[gX_KEY_HOME].state.PRESSED) {
        ti->cursorPos = 0;
        if (!shift) { ti->selectionStart = 0; ti->hasSelection = false; }
    }
    if (input.keyboard.keys[gX_KEY_END].state.PRESSED) {
        ti->cursorPos = ti->length;
        if (!shift) { ti->selectionStart = ti->length; ti->hasSelection = false; }
    }

    // delete / backspace / enter (Ctrl+Backspace/Delete removes whole words)
    if (input.keyboard.keys[gX_KEY_BACKSPACE].state.PRESSED) {
        if (ctrl) gX_GUI_Element_TextInput_DeleteWordLeft(ti);
        else gX_TI_Backspace(ti);
    }
    if (input.keyboard.keys[gX_KEY_DELETE].state.PRESSED) {
        if (ctrl) gX_GUI_Element_TextInput_DeleteWordRight(ti);
        else gX_TI_Delete(ti);
    }
    if (input.keyboard.keys[gX_KEY_ENTER].state.PRESSED ||
        input.keyboard.keys[gX_KEY_KP_ENTER].state.PRESSED) {
        ti->committed = true;
        if (ti->onCommit) ti->onCommit(ti->onCommitUserData);
    }

    // printable characters
    gX_I32 c;
    while ((c = gX_GetCharPressed()) != 0) {
        gX_GUI_Element_TextInput_InsertChar(ti, (gX_I8)c);
        ti->cursorVisible = true;
        ti->blinkTimer = 0.0f;
    }

    //opencode: sync to external buffer every frame (cheap, keeps caller fresh)
    gX_GUI_Element_TextInput_SyncExternalBuffer(ti);
}

//opencode: sync module text into the caller's external buffer, bounded by the
//   capacity given at SetExternalBuffer time — never writes past it (audit
//   CRITICAL #1 fix). Exposed (not static) for unit tests.
gX_I0 gX_GUI_Element_TextInput_SyncExternalBuffer(gX_GUI_Element_TextInput *ti) {
    if (!ti || !ti->externalBuffer || ti->externalBufferCapacity == 0) return;
    gX_U32 n = ti->length;
    if (n >= ti->externalBufferCapacity) n = ti->externalBufferCapacity - 1;
    memcpy(ti->externalBuffer, ti->text, n);
    ti->externalBuffer[n] = '\0';
}

//=============================================================================
// draw
//=============================================================================

static gX_I0 gX_GUI_Element_TextInput_Draw(gX_GUI_Element_TextInput *ti,
    gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    (void)canvas;
    if (!ti || !element) return;

    //opencode: engine UI font like every other text — the hardcoded default
    //   clashed with the UI face (mono swap made it obvious).
    gX_Font font = gui.style.uiFontLoaded ? gui.style.uiFont : gX_Font_GetDefault();
    gX_Rectangle rect = element->rect;

    //opencode: explicit legacy math — identical bits to the old bare-macro
    //   division ((SW/1024)*mult), but context-free.
    gX_F32 resScale = (gX_F32)maindata.screenWidth / 1024.0f * gX_scale_mult;
    if (resScale < 0.5f) resScale = 0.5f;
    if (resScale > 4.0f) resScale = 4.0f;
    gX_F32 fontsize = gX_GUI_FONT_SIZE_DEFAULT * resScale;
    //opencode: fit the field — 32px type in a 0.34-unit field dwarfs the box
    //   and spills over neighbors (theme-lab audit). Width overflow while
    //   typing is inherent (no scissor); height fit is not.
    gX_F32 maxF = rect.height * resScale * 0.5f;
    if (maxF > 8.0f && fontsize > maxF) fontsize = maxF;
    gX_F32 spacing = 1.0f * resScale;
    gX_F32 padding = 6.0f;

    gX_I8 display[gX_GUI_ELEMENT_TEXT_MAX];
    if (ti->passwordMode && ti->length > 0) {
        gX_U32 out = 0;
        for (gX_U32 i = 0; i < ti->length; i += gX_TI_SeqLenAt(ti->text, i, ti->length)) display[out++] = '*';
        display[out] = '\0';
    } else {
        strncpy(display, ti->text, sizeof(display) - 1);
        display[sizeof(display) - 1] = '\0';
    }

    gX_F32v2 pos = { rect.x + padding, rect.y + (rect.height - fontsize) * 0.5f };

    // selection highlight
    if (ti->hasSelection && !ti->passwordMode) {
        gX_U32 start = ti->selectionStart < ti->cursorPos ? ti->selectionStart : ti->cursorPos;
        gX_U32 end   = ti->selectionStart > ti->cursorPos ? ti->selectionStart : ti->cursorPos;
        gX_I8 pre[gX_GUI_ELEMENT_TEXT_MAX], sel[gX_GUI_ELEMENT_TEXT_MAX];
        gX_U32 npre = start > ti->length ? ti->length : start;
        gX_U32 nsel = (end > ti->length ? ti->length : end) - npre;
        memcpy(pre, ti->text, npre); pre[npre] = '\0';
        memcpy(sel, &ti->text[npre], nsel); sel[nsel] = '\0';
        gX_F32v2 preLen = gX_RawString_Measure(pre, font, fontsize, spacing);
        gX_F32v2 selLen = gX_RawString_Measure(sel, font, fontsize, spacing);
        gX_Rectangle selRect = { pos.x + preLen.x, rect.y, selLen.x + 2, rect.height };
        gX_Rectangle_Draw(selRect, (gX_Color){60, 120, 255, 120});
    }

    // text / placeholder
    if (ti->length > 0) {
        gX_RawString_Draw(display, font, pos, (gX_F32v2){0, 0}, 0.0f, fontsize, spacing,
            ti->passwordMode ? (gX_Color){200, 200, 200, 255} : gX_WHITE);
    } else if (element->state.FOCUSED || ti->placeholder[0]) {
        gX_I8 ph[gX_GUI_ELEMENT_TEXT_MAX];
        if (ti->passwordMode && ti->placeholder[0]) {
            gX_U32 n = (gX_U32)strlen(ti->placeholder);
            if (n > sizeof(ph) - 1) n = sizeof(ph) - 1;
            for (gX_U32 i = 0; i < n; i++) ph[i] = '*';
            ph[n] = '\0';
        } else {
            strncpy(ph, ti->placeholder, sizeof(ph) - 1);
            ph[sizeof(ph) - 1] = '\0';
        }
        gX_RawString_Draw(ph, font, pos, (gX_F32v2){0, 0}, 0.0f, fontsize, spacing,
            (gX_Color){140, 140, 140, 255});
    }

    // cursor bar (when focused)
    if (element->state.FOCUSED && ti->cursorVisible) {
        gX_I8 pre[gX_GUI_ELEMENT_TEXT_MAX];
        gX_U32 n = ti->cursorPos > ti->length ? ti->length : ti->cursorPos;
        if (ti->passwordMode) {
            gX_U32 out = 0;
            for (gX_U32 i = 0; i < n; i += gX_TI_SeqLenAt(ti->text, i, n)) pre[out++] = '*';
            pre[out] = '\0';
        } else {
            memcpy(pre, ti->text, n); pre[n] = '\0';
        }
        gX_F32v2 preLen = gX_RawString_Measure(pre, font, fontsize, spacing);
        gX_Rectangle cur = { pos.x + preLen.x, rect.y + 2, 2.0f, rect.height - 4 };
        gX_Rectangle_Draw(cur, (gX_Color){255, 255, 255, 255});
    }
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( TextInput,textinput)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   TextInput,textinput)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   TextInput,textinput,TEXTINPUT)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(TextInput,textinput)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( TextInput,textinput)

gX_I0 gX_GUI_Element_Update_TextInputs(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->textinputs;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_TextInput *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_TextInput *ti = &heapPtr[i];
            gX_GUI_Element_TextInput_Update(ti, element, NULL);
        }
    }
}

gX_I0 gX_GUI_Element_Draw_TextInputs(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    gX_Heap heapStruct = element->textinputs;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_TextInput *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_TextInput_Draw(&heapPtr[i], element, canvas);
        }
    }
}
