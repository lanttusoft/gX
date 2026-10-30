/**********************************************************************************************
*
*   [gX: src/gui/config.h]
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


#ifndef GUI_CONFIG_H
#define GUI_CONFIG_H

//opencode: kernel hardening — the ImGui debug toolbar depends on
//   scene/renderer/game. Options.cmake passes -DgX_GUI_USE_IMGUI=0 when
//   any of those modules is disabled, so gui core compiles without the
//   imgui subsystem (gX_GUI_Load_ImGui/Draw_ImGui become dead calls).
//opencode: gX — ImGui lives under src/tools/ (gX Tools suite, v0.4.0):
//   the vendored runtime + editor chrome are always compiled; the game GUI
//   itself never depends on ImGui. This flag stays 0 as a no-op guard for
//   any game-coupled debug panels ported from gameX (they remain excluded).
#ifndef gX_GUI_USE_IMGUI
    #define gX_GUI_USE_IMGUI 0
#endif

#define gX_GUI_TRACELOG_LEVEL gX_LOG_TRACE2

//gui
#define gX_GUI_DEFAULT_MAX_CANVASES 16

//animation defaults
#define gX_GUI_ANIM_DEFAULT_DURATION     1.0f
#define gX_GUI_ANIM_HACK_THRESHOLD       0.001f

//transition defaults
//opencode: faster out transition (0.15s) for snappier scene switches
#define gX_GUI_TRANSITION_DEFAULT_OUT    0.15f
#define gX_GUI_TRANSITION_DEFAULT_MIDDLE 1.0f
#define gX_GUI_TRANSITION_DEFAULT_IN     0.5f
#define gX_GUI_TRANSITION_SUB_DONE_T     1.0f
#define gX_GUI_TRANSITION_SUB_RESET_T    0.0f

//canvas defaults
#define gX_GUI_CANVAS_DEFAULT_FOVY       90.0f
#define gX_GUI_CANVAS_DEFAULT_CAM_Z      -2.0f
#define gX_GUI_CANVAS_DEFAULT_CAM_Y      0.0f
#define gX_GUI_CANVAS_CAM_UP_Y           1.0f

//panel defaults
#define gX_GUI_PANEL_PADDING             8

//font defaults
#define gX_GUI_FONT_SIZE_DEFAULT         32.0f
#define gX_GUI_FONT_SPACING_DEFAULT      3
#define gX_GUI_FONT_TITLE_SIZE           48.0f
#define gX_GUI_FONT_BODY_SIZE            18.0f
#define gX_GUI_FONT_SMALL_SIZE           14.0f
#define gX_GUI_FONT_CREDITS_SIZE         28.0f
#define gX_GUI_FONT_CREDITS_SPACING      (-3)

//section defaults
#define gX_GUI_SECTION_DIVIDER_H         0.04f

//hover defaults
#define gX_GUI_HOVER_SCALE               1.1f

//element animation defaults (start/mid/end values are inline in caller)

#define gX_GUI_NAMEBFR_SIZE 128
#define gX_GUI_DESCBFR_SIZE 512

//canvas
#define gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS 128

//make queue — max template rebuilds queued per frame
#define gX_GUI_MAKE_QUEUE_MAX 16

//escape menu blur — default radius for Gaussian blur shader
#define gX_GUI_BLUR_RADIUS_DEFAULT 3.0f

#define gX_GUI_CANVAS_NAMEBFR_SIZE 128
#define gX_GUI_CANVAS_DESCBFR_SIZE 512

//element
#define gX_GUI_ELEMENT_DEFAULT_MAX_ANIMATIONS 8
#define gX_GUI_ELEMENT_DEFAULT_MAX_ATTRIBUTES 8
#define gX_GUI_ELEMENT_DEFAULT_MAX_COLORS 8
#define gX_GUI_ELEMENT_DEFAULT_MAX_DATAS 8
//opencode: layout container module (box model) — 1 per element, but the heap
//   pattern is used for uniformity (Add_FirstEmpty); max 2 leaves headroom.
#define gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUTS 2
#define gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUT_CHILDREN 32
#define gX_GUI_ELEMENT_DEFAULT_MAX_MODELS 2
#define gX_GUI_ELEMENT_DEFAULT_MAX_SHADERS 2
#define gX_GUI_ELEMENT_DEFAULT_MAX_SHAPES 8
#define gX_GUI_ELEMENT_DEFAULT_MAX_SOUNDS 4
#define gX_GUI_ELEMENT_DEFAULT_MAX_TEXTS 8
#define gX_GUI_ELEMENT_DEFAULT_MAX_TEXTURES 4
#define gX_GUI_ELEMENT_DEFAULT_MAX_TIMERS   4
#define gX_GUI_ELEMENT_DEFAULT_MAX_TEXTINPUTS 2

//opencode: v0.6.x — owner-scoped click callback context. Widget callbacks copy
//   their arguments into the owning element's data module (see
//   gX_GUI_OnClick_Context) instead of a global fixed pool, so partial canvas
//   refreshes cannot alias them. The inline buffer matches the largest widget
//   context; markup stores its script path in the owner's existing format field.
#define gX_GUI_ELEMENT_CALLBACK_CTX_MAX 64

#define gX_GUI_ELEMENT_REMARK_MAX 128
#define gX_GUI_ELEMENT_FORMAT_MAX 128

#define gX_GUI_ELEMENT_TEXT_MAX 256

#define gX_GUI_ELEMENT_NAMEBFR_SIZE 128
#define gX_GUI_ELEMENT_DESCBFR_SIZE 256

//builder defaults
#define gX_GUI_BUILDER_DIVISIONS         8
#define gX_GUI_BUILDER_WIDGET_BUTTON_R   127
#define gX_GUI_BUILDER_WIDGET_BUTTON_G   127
#define gX_GUI_BUILDER_WIDGET_BUTTON_B   63
#define gX_GUI_BUILDER_WIDGET_BUTTON_A   255
#define gX_GUI_BUILDER_WIDGET_BACK_R     255
#define gX_GUI_BUILDER_WIDGET_BACK_G     127
#define gX_GUI_BUILDER_WIDGET_BACK_B     63
#define gX_GUI_BUILDER_WIDGET_BACK_A     255
#define gX_GUI_BUILDER_WIDGET_SLIDER_R   127
#define gX_GUI_BUILDER_WIDGET_SLIDER_G   127
#define gX_GUI_BUILDER_WIDGET_SLIDER_B   127
#define gX_GUI_BUILDER_WIDGET_SLIDER_A   255
#define gX_GUI_BUILDER_WIDGET_KNOB_RADIUS 24.0f
#define gX_GUI_BUILDER_WIDGET_SLIDER_FORMAT "%.3f"

//interaction defaults
#define gX_GUI_HOVER_COLOR_MULTIPLIER    2.0f
#define gX_GUI_HOVER_TRANSITION_SECONDS  0.1f
#define gX_GUI_SOUND_DEFAULT_VOLUME      0.5f
#define gX_GUI_SOUND_DEFAULT_PITCH       1.0f
#define gX_GUI_SOUND_DEFAULT_PAN         0.0f

//widget sizing (grid units)
#define gX_GUI_WIDGET_BUTTON_WIDTH       2.0f
#define gX_GUI_WIDGET_BUTTON_HEIGHT      0.5f
#define gX_GUI_WIDGET_BACK_WIDTH         1.0f
#define gX_GUI_WIDGET_BACK_HEIGHT        0.5f
#define gX_GUI_WIDGET_STEPPER_BTN_WIDTH  0.6f
#define gX_GUI_WIDGET_SCROLLLIST_ITEM_H  0.6f
#define gX_GUI_WIDGET_SCROLLBAR_WIDTH    0.5f
#define gX_GUI_WIDGET_TABBAR_HEIGHT      0.6f
#define gX_GUI_WIDGET_TAB_GUTTER         0.1f
#define gX_GUI_WIDGET_PROGRESS_MIN_SLIVER 0.05f

//text defaults
#define gX_GUI_TEXT_DEFAULT_FONTSIZE     32.0f
#define gX_GUI_TEXT_DEFAULT_SPACING      3
//opencode: floor for the opt-in shrink-to-fit path — below this the label is
//   too small to read, so truncation/wrapping remains the honest signal.
#define gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE 0.6f

//focus ring
#define gX_GUI_FOCUS_RING_OFFSET         3.0f
#define gX_GUI_FOCUS_RING_R              255
#define gX_GUI_FOCUS_RING_G              255
#define gX_GUI_FOCUS_RING_B              255
#define gX_GUI_FOCUS_RING_A              255

//drag threshold — minimum pixel movement to trigger drag (prevents jitter)
#define gX_GUI_DRAG_THRESHOLD            2.0f

//shadow
//opencode: text-shadow offset/color are theme tokens now
//   (gui.style.textShadow) so they scale with the resolution; the old
//   fixed 4/4 constants lived here.

//opencode: transition/animation easing — references centralized enum in gxmath/easing.h
#include "../gxmath/easing.h"
typedef gX_Easing gX_GUI_Easing;

#endif //GUI_CONFIG_H
