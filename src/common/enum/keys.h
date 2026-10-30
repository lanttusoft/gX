/**********************************************************************************************
* 
*   [common/enum/keys.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
#ifndef ENUM_KEYS_H
#define ENUM_KEYS_H

#include "../types/primary.h"

//opencode: expanded gX_KEYS from ~58 to ~109 entries covering function keys, keypad, nav, modifier, and Android keys
#define gX_KEYS \
    X(APOSTROPHE,   39) /* // Key: ' */ \
    X(COMMA,        44) /* // Key: , */ \
    X(MINUS,        45) /* // Key: - */ \
    X(PERIOD,       46) /* // Key: . */ \
    X(SLASH,        47) /* // Key: / */ \
    X(ZERO,         48) /* // Key: 0 */ \
    X(ONE,          49) /* // Key: 1 */ \
    X(TWO,          50) /* // Key: 2 */ \
    X(THREE,        51) /* // Key: 3 */ \
    X(FOUR,         52) /* // Key: 4 */ \
    X(FIVE,         53) /* // Key: 5 */ \
    X(SIX,          54) /* // Key: 6 */ \
    X(SEVEN,        55) /* // Key: 7 */ \
    X(EIGHT,        56) /* // Key: 8 */ \
    X(NINE,         57) /* // Key: 9 */ \
    X(SEMICOLON,    59) /* // Key: ; */ \
    X(EQUAL,        61) /* // Key: = */ \
    X(A,            65) /* // Key: A | a */ \
    X(B,            66) /* // Key: B | b */ \
    X(C,            67) /* // Key: C | c */ \
    X(D,            68) /* // Key: D | d */ \
    X(E,            69) /* // Key: E | e */ \
    X(F,            70) /* // Key: F | f */ \
    X(G,            71) /* // Key: G | g */ \
    X(H,            72) /* // Key: H | h */ \
    X(I,            73) /* // Key: I | i */ \
    X(J,            74) /* // Key: J | j */ \
    X(K,            75) /* // Key: K | k */ \
    X(L,            76) /* // Key: L | l */ \
    X(M,            77) /* // Key: M | m */ \
    X(N,            78) /* // Key: N | n */ \
    X(O,            79) /* // Key: O | o */ \
    X(P,            80) /* // Key: P | p */ \
    X(Q,            81) /* // Key: Q | q */ \
    X(R,            82) /* // Key: R | r */ \
    X(S,            83) /* // Key: S | s */ \
    X(T,            84) /* // Key: T | t */ \
    X(U,            85) /* // Key: U | u */ \
    X(V,            86) /* // Key: V | v */ \
    X(W,            87) /* // Key: W | w */ \
    X(X,            88) /* // Key: X | x */ \
    X(Y,            89) /* // Key: Y | y */ \
    X(Z,            90) /* // Key: Z | z */ \
    X(LEFT_BRACKET, 91) /* // Key: [ */ \
    X(BACKSLASH,    92) /* // Key: '\' */ \
    X(RIGHT_BRACKET,93) /* // Key: ] */ \
    X(GRAVE,        96) /* // Key: ` */ \
    X(ESCAPE,       256) /* // Key: Esc */ \
    X(ENTER,        257) /* // Key: Enter */ \
    X(TAB,          258) /* // Key: Tab */ \
    X(BACKSPACE,    259) /* // Key: Backspace */ \
    X(INSERT,       260) /* // Key: Ins */ \
    X(DELETE,       261) /* // Key: Del */ \
    X(RIGHT,        262) /* // Key: Cursor right */ \
    X(LEFT,         263) /* // Key: Cursor left */ \
    X(DOWN,         264) /* // Key: Cursor down */ \
    X(UP,           265) /* // Key: Cursor up */ \
    X(SPACE,         32) /* // Key: Space */ \
    X(PAGE_UP,      266) /* // Key: Page up */ \
    X(PAGE_DOWN,    267) /* // Key: Page down */ \
    X(HOME,         268) /* // Key: Home */ \
    X(END,          269) /* // Key: End */ \
    X(CAPS_LOCK,    280) /* // Key: Caps lock */ \
    X(SCROLL_LOCK,  281) /* // Key: Scroll down */ \
    X(NUM_LOCK,     282) /* // Key: Num lock */ \
    X(PRINT_SCREEN, 283) /* // Key: Print screen */ \
    X(PAUSE,        284) /* // Key: Pause */ \
    X(F1,           290) /* // Key: F1 */ \
    X(F2,           291) /* // Key: F2 */ \
    X(F3,           292) /* // Key: F3 */ \
    X(F4,           293) /* // Key: F4 */ \
    X(F5,           294) /* // Key: F5 */ \
    X(F6,           295) /* // Key: F6 */ \
    X(F7,           296) /* // Key: F7 */ \
    X(F8,           297) /* // Key: F8 */ \
    X(F9,           298) /* // Key: F9 */ \
    X(F10,          299) /* // Key: F10 */ \
    X(F11,          300) /* // Key: F11 */ \
    X(F12,          301) /* // Key: F12 */ \
    X(LEFT_SHIFT,   340) /* // Key: Shift left */ \
    X(LEFT_CONTROL, 341) /* // Key: Control left */ \
    X(LEFT_ALT,     342) /* // Key: Alt left */ \
    X(LEFT_SUPER,   343) /* // Key: Super left */ \
    X(RIGHT_SHIFT,  344) /* // Key: Shift right */ \
    X(RIGHT_CONTROL,345) /* // Key: Control right */ \
    X(RIGHT_ALT,    346) /* // Key: Alt right */ \
    X(RIGHT_SUPER,  347) /* // Key: Super right */ \
    X(KB_MENU,      348) /* // Key: KB menu */ \
    X(KP_0,         320) /* // Key: Keypad 0 */ \
    X(KP_1,         321) /* // Key: Keypad 1 */ \
    X(KP_2,         322) /* // Key: Keypad 2 */ \
    X(KP_3,         323) /* // Key: Keypad 3 */ \
    X(KP_4,         324) /* // Key: Keypad 4 */ \
    X(KP_5,         325) /* // Key: Keypad 5 */ \
    X(KP_6,         326) /* // Key: Keypad 6 */ \
    X(KP_7,         327) /* // Key: Keypad 7 */ \
    X(KP_8,         328) /* // Key: Keypad 8 */ \
    X(KP_9,         329) /* // Key: Keypad 9 */ \
    X(KP_DECIMAL,   330) /* // Key: Keypad . */ \
    X(KP_DIVIDE,    331) /* // Key: Keypad / */ \
    X(KP_MULTIPLY,  332) /* // Key: Keypad * */ \
    X(KP_SUBTRACT,  333) /* // Key: Keypad - */ \
    X(KP_ADD,       334) /* // Key: Keypad + */ \
    X(KP_ENTER,     335) /* // Key: Keypad Enter */ \
    X(KP_EQUAL,     336) /* // Key: Keypad = */ \
    X(BACK,           4) /* // Key: Android back button */ \
    X(VOLUME_UP,     24) /* // Key: Android volume up button */ \
    X(VOLUME_DOWN,   25) /* // Key: Android volume down button */ \

//raylib
// Keyboard keys (US keyboard layout)
// NOTE: Use GetKeyPressed() to allow redefining
// required keys for alternative layouts
//opencode: typedef — now fully driven by gX_KEYS macro (MENU kept manual due to value collision with R)
typedef enum {
    gX_KEY_NULL = 0,
#define X(key,value) gX_KEY_##key = value,
    gX_KEYS
#undef X
    //opencode: MENU=82 collides with R=82; kept outside macro to avoid duplicate switch case
    gX_KEY_MENU = 82,
} gX_Keys;
const gX_I8 *gX_Keys_String(gX_I32 key);

#endif //ENUM_KEYS_H
