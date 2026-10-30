/**********************************************************************************************
*
*   [gX: src/gui/colorpicker.c]
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


//opencode: standalone self-contained color picker widget
#include "colorpicker.h"
#include "builder.h"
#include "../input.h"
#include "../main.h"
#include "../common/macros.h"
#include "../wrappers/shape.h"
#include "../wrappers/text.h"

gX_I0 gX_ColorPicker_Init(gX_ColorPicker *picker, gX_U8 *r, gX_U8 *g, gX_U8 *b) {
    picker->active = 0;
    picker->dragChannel = -1;
    picker->leftWasDown = 0;
    picker->targetR = r;
    picker->targetG = g;
    picker->targetB = b;
    picker->gridX = 0.0f;
    picker->gridY = 0.0f;
    for (gX_I32 i = 0; i < 3; i++) {
        picker->barRects[i] = (gX_Rectangle){0, 0, 0, 0};
    }
    picker->swatchRect = (gX_Rectangle){0, 0, 0, 0};
}

gX_I0 gX_ColorPicker_Create(gX_ColorPicker *picker, gX_GUI_Builder *b, gX_F32 gridX, gX_F32 gridY) {
    picker->active = 1;
    picker->gridX = gridX;
    picker->gridY = gridY;

    gX_F32 wd = (gX_F32)maindata.screenWidth / 8.0f;
    gX_F32 hd = (gX_F32)maindata.screenHeight / 8.0f;

    // Store screen rects for input hit-testing and overlay drawing
    picker->barRects[0] = (gX_Rectangle){gridX * wd, (gridY + 0.4f) * hd, 3.5f * wd, 0.25f * hd};
    picker->barRects[1] = (gX_Rectangle){gridX * wd, (gridY + 0.7f) * hd, 3.5f * wd, 0.25f * hd};
    picker->barRects[2] = (gX_Rectangle){gridX * wd, (gridY + 1.0f) * hd, 3.5f * wd, 0.25f * hd};
    picker->swatchRect = (gX_Rectangle){(gridX + 3.5f) * wd, (gridY + 0.4f) * hd, 0.3f * wd, 0.55f * hd};

    // Label
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX, gridY, 3.5f, 0.3f);
        gX_GUI_Builder_Color(b, 48, 48, 48, 255);
        b->currentElement->state.IGNORE_OCCLUSION = true;
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, "Colors (R G B)");
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);

    // R bar background (visual only — no clickable)
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX, gridY + 0.4f, 3.5f, 0.25f);
        gX_GUI_Builder_Color(b, 32, 0, 0, 255);
        b->currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(b);

    // G bar background
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX, gridY + 0.7f, 3.5f, 0.25f);
        gX_GUI_Builder_Color(b, 0, 32, 0, 255);
        b->currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(b);

    // B bar background
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX, gridY + 1.0f, 3.5f, 0.25f);
        gX_GUI_Builder_Color(b, 0, 0, 32, 255);
        b->currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(b);

    // Color swatch background — thin strip to the right of bars
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX + 3.5f, gridY + 0.4f, 0.3f, 0.55f);
        gX_GUI_Builder_Color(b, 64, 64, 64, 255);
        b->currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(b);
}

gX_I1 gX_ColorPicker_Update(gX_ColorPicker *picker) {
    if (!picker->active) return 0;
    if (!picker->targetR || !picker->targetG || !picker->targetB) return 0;

    gX_F32 mx = input.mouse.coord.x;
    gX_F32 my = input.mouse.coord.y;
    gX_I1 nowDown = input.mouse.leftButton.DOWN;

    // Detect press edge (transition from UP to DOWN)
    gX_I1 justPressed = nowDown && !picker->leftWasDown;

    // Start drag on press
    if (justPressed && picker->dragChannel < 0) {
        for (gX_I32 ci = 0; ci < 3; ci++) {
            gX_Rectangle *bar = &picker->barRects[ci];
            if (mx >= bar->x && mx <= bar->x + bar->width &&
                my >= bar->y && my <= bar->y + bar->height) {
                picker->dragChannel = ci;
                // Set value immediately on click
                gX_F32 t = (mx - bar->x) / bar->width;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                gX_U8 val = (gX_U8)(t * 255.0f);
                switch (ci) {
                    case 0: *picker->targetR = val; break;
                    case 1: *picker->targetG = val; break;
                    case 2: *picker->targetB = val; break;
                    default: break;
                }
                break;
            }
        }
    }

    // Continue drag
    if (nowDown && picker->dragChannel >= 0) {
        gX_Rectangle *bar = &picker->barRects[picker->dragChannel];
        gX_F32 t = (mx - bar->x) / bar->width;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        gX_U8 val = (gX_U8)(t * 255.0f);
        switch (picker->dragChannel) {
            case 0: *picker->targetR = val; break;
            case 1: *picker->targetG = val; break;
            case 2: *picker->targetB = val; break;
            default: break;
        }
    }

    // Release
    if (!nowDown && picker->dragChannel >= 0) {
        picker->dragChannel = -1;
    }

    picker->leftWasDown = nowDown;

    return (picker->dragChannel >= 0 || justPressed) ? 1 : 0;
}

gX_I0 gX_ColorPicker_Draw(gX_ColorPicker *picker) {
    if (!picker->active) return;
    if (!picker->targetR || !picker->targetG || !picker->targetB) return;

    gX_U8 chVal[3] = {*picker->targetR, *picker->targetG, *picker->targetB};
    gX_Color chCol[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    const gX_I8 *chLabel[3] = {"R", "G", "B"};

    for (gX_I32 ci = 0; ci < 3; ci++) {
        gX_Rectangle *bar = &picker->barRects[ci];
        gX_F32 fillW = (gX_F32)chVal[ci] / 255.0f * bar->width;

        // Bar fill
        if (fillW > 0.0f) {
            gX_Rectangle_Draw_Parameters(
                (gX_Rectangle){bar->x, bar->y, fillW, bar->height},
                (gX_Rectangle_Parameters){.colors[0] = chCol[ci]});
        }

        // Knob marker
        gX_F32 knobX = bar->x + fillW - 3.0f;
        if (knobX < bar->x) knobX = bar->x;
        gX_Rectangle_Draw_Parameters(
            (gX_Rectangle){knobX, bar->y - 1.0f, 6.0f, bar->height + 2.0f},
            (gX_Rectangle_Parameters){.colors[0] = gX_WHITE});

        // Value text
        gX_RawString_Draw_Lesser(
            gX_Text_Format("%s: %i", chLabel[ci], chVal[ci]),
            (gX_F32v2){bar->x + 4.0f, bar->y + 2.0f}, 20, gX_WHITE);
    }

    // Color swatch — draw final combined color
    gX_Rectangle_Draw_Parameters(picker->swatchRect,
        (gX_Rectangle_Parameters){.colors[0] = {*picker->targetR, *picker->targetG, *picker->targetB, 255}});
    gX_Rectangle_Draw_Parameters(picker->swatchRect,
        (gX_Rectangle_Parameters){.colors[0] = gX_WHITE, .lines = true});
}
