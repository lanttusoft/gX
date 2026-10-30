/**********************************************************************************************
*
*   [gX: src/gui/css.c]
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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "css.h"
#include "canvas.h"
#include "element/element.h"

#include "gui.h"      // gui.style (default sheet) + gX_GUI_Style

#include "../common/log.h"
#include "../main.h"

//opencode: gameCSS engine (Phase 2). Hand-written tokenizer + parser +
//   cascade resolver. See css.h for the language summary.

//=============================================================================
// registry
//=============================================================================

static gX_CSS_StyleSheet g_cssSheets[gX_GUI_CSS_MAX_SHEETS];
static gX_U32 g_cssSheetCount = 0;
static gX_U32 g_cssReloadCounter = 0;

//opencode: built-in origin-0 sheet derived from gui.style
static gX_CSS_StyleSheet g_cssDefaultSheet;
static gX_U32 g_cssDefaultSheetBuilt = 0;

//opencode: forward declaration — ParseCompound lives in the parser section
static gX_I0 gX_GUI_CSS_ParseCompound(gX_CSS_Selector *sel, const gX_I8 *text);

//opencode: rule construction helpers for the built-in sheet
static gX_CSS_Rule *gX_GUI_CSS_DefaultRule(gX_CSS_StyleSheet *sheet, const gX_I8 *selector, gX_I32 origin) {
    gX_CSS_Rule *r = &sheet->rules[sheet->ruleCount];
    gX_GUI_CSS_ParseCompound(&r->selector, selector);
    r->order = sheet->ruleCount;
    r->origin = origin;
    r->declCount = 0;
    sheet->ruleCount++;
    return r;
}

static gX_I0 gX_GUI_CSS_DefaultColorDecl(gX_CSS_Rule *r, gX_I32 prop, gX_Color c) {
    gX_CSS_Decl *d = &r->decls[r->declCount++];
    d->prop = prop;
    d->value.type = gX_CSS_VALUE_COLOR;
    d->value.color = c;
    d->important = false;
}

static gX_I0 gX_GUI_CSS_DefaultNumDecl(gX_CSS_Rule *r, gX_I32 prop, gX_F32 n) {
    gX_CSS_Decl *d = &r->decls[r->declCount++];
    d->prop = prop;
    d->value.type = gX_CSS_VALUE_NUMBER;
    d->value.number = n;
    d->important = false;
}

gX_I0 gX_GUI_CSS_BuildDefaultSheet(gX_I0) {
    memset(&g_cssDefaultSheet, 0, sizeof(g_cssDefaultSheet));
    strcpy(g_cssDefaultSheet.path, "<builtin:gui.style>");
    gX_GUI_Style *s = &gui.style;
    
    //opencode: buttons (background + text color)
    gX_CSS_Rule *r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".btn", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->buttonBase);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->buttonText);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".btn:hover", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->buttonHover);
    
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".back", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->backBase);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->backText);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".back:hover", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->backHover);
    
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".play", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->playBase);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".play:hover", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->playHover);
    
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".extra", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->extraBase);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".extra:hover", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->extraHover);
    
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".quit", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->quitBase);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".quit:hover", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->quitHover);
    
    //opencode: panels / overlays
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".panel", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->panelBg);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".panel-overlay", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->panelOverlay);
    
    //opencode: text variants — font-size in text-module logical units
    //   (same unit as curText->fontSize; scaled by resScale at draw time)
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".title", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    gX_GUI_CSS_DefaultNumDecl(r, gX_CSS_PROP_FONT_SIZE, s->titleSize);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".body", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    gX_GUI_CSS_DefaultNumDecl(r, gX_CSS_PROP_FONT_SIZE, s->bodySize);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".small", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    gX_GUI_CSS_DefaultNumDecl(r, gX_CSS_PROP_FONT_SIZE, s->smallSize);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".credits", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    gX_GUI_CSS_DefaultNumDecl(r, gX_CSS_PROP_FONT_SIZE, s->creditsSize);
    
    //opencode: escape/pause menu classes — driven by gui.style.escape tokens,
    //   the reference pattern: theme.json → style → CSS classes → elements.
    //   A canvas-attached stylesheet can override these (sheet > builder).
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-dim", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->escape.dimOverlay);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-panel", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->escape.panelBg);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-title", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->escape.titleBarBg);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->escape.titleText);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-btn", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->escape.buttonBg);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-close", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_BACKGROUND_COLOR, s->escape.closeBg);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->textColor);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-hint", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->escape.hintText);
    r = gX_GUI_CSS_DefaultRule(&g_cssDefaultSheet, ".pause-saved", 0);
    gX_GUI_CSS_DefaultColorDecl(r, gX_CSS_PROP_COLOR, s->escape.savedText);
    
    g_cssDefaultSheetBuilt = 1;
    gX_TraceLog(gX_LOG_INFO, "CSS: built default style sheet (%d rules)", (gX_I32)g_cssDefaultSheet.ruleCount);
}

static gX_I64 gX_GUI_CSS_GetFileMtime(const gX_I8 *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (gX_I64)st.st_mtime;
}

static gX_I8 *gX_GUI_CSS_ReadFile(const gX_I8 *path, gX_I32 *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0 || len > 256 * 1024) { fclose(f); return NULL; }
    rewind(f);
    gX_I8 *buf = (gX_I8*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    if (outLen) *outLen = (gX_I32)got;
    return buf;
}

gX_I32 gX_GUI_CSS_LoadSheet(const gX_I8 *path) {
    if (!path || !path[0]) return -1;
    for (gX_U32 i = 0; i < g_cssSheetCount; i++) {
        if (strcmp(g_cssSheets[i].path, path) == 0) return (gX_I32)i;
    }
    if (g_cssSheetCount >= gX_GUI_CSS_MAX_SHEETS) {
        gX_TraceLog(gX_LOG_WARNING, "CSS: sheet registry full (max %d) — '%s' not loaded",
            gX_GUI_CSS_MAX_SHEETS, path);
        return -1;
    }
    gX_I32 len = 0;
    gX_I8 *text = gX_GUI_CSS_ReadFile(path, &len);
    if (!text) {
        gX_TraceLog(gX_LOG_WARNING, "CSS: cannot read stylesheet '%s'", path);
        return -1;
    }
    gX_CSS_StyleSheet *sheet = &g_cssSheets[g_cssSheetCount];
    memset(sheet, 0, sizeof(*sheet));
    strncpy(sheet->path, path, sizeof(sheet->path) - 1);
    gX_I32 rules = gX_GUI_CSS_ParseSheet(text, sheet);
    free(text);
    sheet->mtime = gX_GUI_CSS_GetFileMtime(path);
    gX_TraceLog(gX_LOG_INFO, "CSS: loaded '%s' (%d rules)", path, rules);
    g_cssSheetCount++;
    return (gX_I32)(g_cssSheetCount - 1);
}

gX_I0 gX_GUI_CSS_UnloadSheet(const gX_I8 *path) {
    for (gX_U32 i = 0; i < g_cssSheetCount; i++) {
        if (strcmp(g_cssSheets[i].path, path) == 0) {
            memmove(&g_cssSheets[i], &g_cssSheets[i + 1],
                (g_cssSheetCount - i - 1) * sizeof(gX_CSS_StyleSheet));
            g_cssSheetCount--;
            return;
        }
    }
}

//opencode: hot-reload — mtime watch, rate-limited to every ~30 frames
//   (shader-reload pattern). Reparses changed sheets in place.
gX_I0 gX_GUI_CSS_CheckReload(gX_I0) {
    g_cssReloadCounter++;
    if (g_cssReloadCounter % 30 != 0) return;
    for (gX_U32 i = 0; i < g_cssSheetCount; i++) {
        gX_CSS_StyleSheet *sheet = &g_cssSheets[i];
        gX_I64 mt = gX_GUI_CSS_GetFileMtime(sheet->path);
        if (mt != sheet->mtime) {
            gX_I32 len = 0;
            gX_I8 *text = gX_GUI_CSS_ReadFile(sheet->path, &len);
            if (text) {
                gX_I32 old = (gX_I32)sheet->ruleCount;
                gX_I32 fresh = gX_GUI_CSS_ParseSheet(text, sheet);
                free(text);
                sheet->mtime = mt;
                gX_TraceLog(gX_LOG_INFO, "CSS: hot-reloaded '%s' (%d rules -> %d)",
                    sheet->path, old, fresh);
            }
        }
    }
}

//=============================================================================
// parser
//=============================================================================

static gX_I0 gX_GUI_CSS_SkipWs(gX_I8 **p) {
    while (**p) {
        if (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r' || **p == '\f') { (*p)++; continue; }
        if ((*p)[0] == '/' && (*p)[1] == '*') {
            (*p) += 2;
            while (**p && !((*p)[0] == '*' && (*p)[1] == '/')) (*p)++;
            if (**p) (*p) += 2;
            continue;
        }
        break;
    }
}

static gX_I32 gX_GUI_CSS_PropFromName(const gX_I8 *name) {
    struct { const gX_I8 *n; gX_I32 p; } table[] = {
        {"display", gX_CSS_PROP_DISPLAY}, {"position", gX_CSS_PROP_POSITION},
        {"flex-direction", gX_CSS_PROP_FLEX_DIRECTION},
        {"justify-content", gX_CSS_PROP_JUSTIFY_CONTENT},
        {"align-items", gX_CSS_PROP_ALIGN_ITEMS},
        {"gap", gX_CSS_PROP_GAP}, {"flex-grow", gX_CSS_PROP_FLEX_GROW},
        {"order", gX_CSS_PROP_ORDER}, {"width", gX_CSS_PROP_WIDTH},
        {"height", gX_CSS_PROP_HEIGHT}, {"min-width", gX_CSS_PROP_MIN_WIDTH},
        {"min-height", gX_CSS_PROP_MIN_HEIGHT}, {"max-width", gX_CSS_PROP_MAX_WIDTH},
        {"max-height", gX_CSS_PROP_MAX_HEIGHT}, {"margin", gX_CSS_PROP_MARGIN},
        {"margin-top", gX_CSS_PROP_MARGIN_TOP}, {"margin-right", gX_CSS_PROP_MARGIN_RIGHT},
        {"margin-bottom", gX_CSS_PROP_MARGIN_BOTTOM}, {"margin-left", gX_CSS_PROP_MARGIN_LEFT},
        {"padding", gX_CSS_PROP_PADDING}, {"padding-top", gX_CSS_PROP_PADDING_TOP},
        {"padding-right", gX_CSS_PROP_PADDING_RIGHT}, {"padding-bottom", gX_CSS_PROP_PADDING_BOTTOM},
        {"padding-left", gX_CSS_PROP_PADDING_LEFT}, {"border-width", gX_CSS_PROP_BORDER_WIDTH},
        {"border-color", gX_CSS_PROP_BORDER_COLOR}, {"border-radius", gX_CSS_PROP_BORDER_RADIUS},
        {"box-sizing", gX_CSS_PROP_BOX_SIZING}, {"font-size", gX_CSS_PROP_FONT_SIZE},
        {"color", gX_CSS_PROP_COLOR}, {"text-align", gX_CSS_PROP_TEXT_ALIGN},
        {"line-height", gX_CSS_PROP_LINE_HEIGHT}, {"background-color", gX_CSS_PROP_BACKGROUND_COLOR},
        {"opacity", gX_CSS_PROP_OPACITY}, {"layer", gX_CSS_PROP_LAYER},
        {"z-index", gX_CSS_PROP_Z_INDEX}, {"visible", gX_CSS_PROP_VISIBLE},
        {"angle", gX_CSS_PROP_ANGLE}, {"shader-index", gX_CSS_PROP_SHADER_INDEX},
    };
    for (gX_U32 i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].n) == 0) return table[i].p;
    }
    return gX_CSS_PROP_NULL;
}

//opencode: named colors — raylib palette + common CSS names
static gX_I1 gX_GUI_CSS_ColorFromName(const gX_I8 *name, gX_Color *out) {
    struct { const gX_I8 *n; gX_U8 r, g, b, a; } table[] = {
        {"white", 255,255,255,255}, {"black", 0,0,0,255}, {"red", 255,0,0,255},
        {"green", 0,255,0,255}, {"lime", 0,255,0,255}, {"blue", 0,0,255,255},
        {"yellow", 255,255,0,255}, {"magenta", 255,0,255,255}, {"cyan", 0,255,255,255},
        {"orange", 255,161,0,255}, {"purple", 200,122,255,255},
        {"darkgray", 80,80,80,255}, {"gray", 130,130,130,255},
        {"lightgray", 200,200,200,255}, {"darkblue", 0,82,172,255},
        {"darkgreen", 0,117,44,255}, {"darkpurple", 112,31,126,255},
        {"darkbrown", 76,63,47,255}, {"beige", 211,176,131,255},
        {"brown", 127,106,79,255}, {"maroon", 190,33,55,255},
        {"pink", 255,109,194,255}, {"gold", 255,203,0,255},
        {"darkred", 139,0,0,255}, {"skyblue", 102,191,255,255},
        {"transparent", 0,0,0,0},
    };
    for (gX_U32 i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].n) == 0) { *out = (gX_Color){table[i].r, table[i].g, table[i].b, table[i].a}; return true; }
    }
    return false;
}

static gX_I32 gX_GUI_CSS_HexDigit(gX_I8 c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static gX_I1 gX_GUI_CSS_ParseColor(const gX_I8 *tok, gX_Color *out) {
    if (!tok || !tok[0]) return false;
    if (tok[0] == '#') {
        gX_U32 n = (gX_U32)strlen(tok) - 1;
        if (n == 3 || n == 6 || n == 4 || n == 8) {
            gX_I32 v[8]; gX_I32 cnt = 0;
            for (gX_U32 i = 1; tok[i]; i++) {
                gX_I32 d = gX_GUI_CSS_HexDigit(tok[i]);
                if (d < 0) return false;
                v[cnt++] = d;
            }
            if (n == 3 || n == 4) {
                out->r = (gX_U8)(v[0] * 17); out->g = (gX_U8)(v[1] * 17);
                out->b = (gX_U8)(v[2] * 17); out->a = (n == 4) ? (gX_U8)(v[3] * 17) : 255;
            } else {
                out->r = (gX_U8)(v[0] * 16 + v[1]); out->g = (gX_U8)(v[2] * 16 + v[3]);
                out->b = (gX_U8)(v[4] * 16 + v[5]); out->a = (n == 8) ? (gX_U8)(v[6] * 16 + v[7]) : 255;
            }
            return true;
        }
        return false;
    }
    if (strncmp(tok, "rgb(", 4) == 0 || strncmp(tok, "rgba(", 5) == 0) {
        gX_I32 r = -1, g = -1, b = -1, a = 255;
        if (sscanf(tok, "rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            sscanf(tok, "rgba(%d,%d,%d,%d)", &r, &g, &b, &a) == 4) {
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255 && a >= 0 && a <= 255) {
                *out = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
                return true;
            }
        }
        return false;
    }
    return gX_GUI_CSS_ColorFromName(tok, out);
}

//opencode: parse one value token (may span up to the terminating ';' or '}')
static gX_I1 gX_GUI_CSS_ParseValue(const gX_I8 *tok, gX_CSS_Value *out) {
    memset(out, 0, sizeof(*out));
    if (!tok || !tok[0]) return false;
    //opencode: number — optional trailing unit (px = grid unit alias, % unsupported)
    gX_I8 *end = NULL;
    gX_F32 num = strtof(tok, &end);
    if (end != tok) {
        while (*end == ' ') end++;
        if (end[0] == '%') return false; // v1: percentages unsupported
        out->type = gX_CSS_VALUE_NUMBER;
        out->number = num;
        return true;
    }
    if (gX_GUI_CSS_ParseColor(tok, &out->color)) {
        out->type = gX_CSS_VALUE_COLOR;
        return true;
    }
    out->type = gX_CSS_VALUE_KEYWORD;
    snprintf(out->str, sizeof(out->str), "%s", tok);
    return true;
}

//opencode: parse one comma-separated selector compound into the selector.
//   Character-based so combined tokens work (.btn:hover). A type token at the
//   START separated by whitespace is a descendant ancestor (panel .label);
//   without whitespace it's a compound on the element itself (rect.btn).
static gX_I0 gX_GUI_CSS_ParseCompound(gX_CSS_Selector *sel, const gX_I8 *text) {
    memset(sel, 0, sizeof(*sel));
    enum { TOK_TYPE, TOK_CLASS, TOK_ID, TOK_PSEUDO };
    gX_I32 kinds[8];
    gX_I8  texts[8][gX_GUI_CSS_STR_MAX];
    gX_I1  spaced[8];
    gX_U32 count = 0;
    const gX_I8 *p = text;
    gX_I1 hadSpace = false;
    while (1) {
        gX_I1 anySpace = false;
        while (*p == ' ' || *p == '\t') { p++; anySpace = true; }
        if (!*p || count >= 8) break;
        gX_I32 kind = TOK_TYPE;
        if (*p == '#') { kind = TOK_ID; p++; }
        else if (*p == '.') { kind = TOK_CLASS; p++; }
        else if (*p == ':') { kind = TOK_PSEUDO; p++; }
        else if (*p == '*') { while (*p && *p != ' ' && *p != '\t') p++; continue; }
        gX_U32 n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '#' && *p != '.' && *p != ':' && n < gX_GUI_CSS_STR_MAX - 1) {
            texts[count][n++] = *p++;
        }
        texts[count][n] = '\0';
        kinds[count] = kind;
        spaced[count] = hadSpace || anySpace;
        count++;
        hadSpace = false;
    }
    //opencode: assign — a FIRST type token separated from the REST by
    //   whitespace is a descendant ancestor (panel .label); without the
    //   whitespace it's a compound on the element itself (rect.btn)
    gX_I32 lastType = -1;
    for (gX_U32 i = 0; i < count; i++) if (kinds[i] == TOK_TYPE) lastType = (gX_I32)i;
    if (count > 1 && kinds[0] == TOK_TYPE && spaced[1]) {
        snprintf(sel->ancestorType, sizeof(sel->ancestorType), "%s", texts[0]);
        sel->specificity += 1;
        if (lastType > 0) {
            snprintf(sel->type, sizeof(sel->type), "%s", texts[(gX_U32)lastType]);
            sel->specificity += 1;
        }
    } else if (lastType >= 0) {
        snprintf(sel->type, sizeof(sel->type), "%s", texts[(gX_U32)lastType]);
        sel->specificity += 1;
    }
    for (gX_U32 i = 0; i < count; i++) {
        if (kinds[i] == TOK_ID) {
            snprintf(sel->id, sizeof(sel->id), "%.63s", texts[i]);
            sel->specificity += 100;
        } else if (kinds[i] == TOK_CLASS) {
            snprintf(sel->cls, sizeof(sel->cls), "%.63s", texts[i]);
            sel->specificity += 10;
        } else if (kinds[i] == TOK_PSEUDO) {
            if (strcmp(texts[i], "hover") == 0) sel->pseudo |= gX_GUI_CSS_PSEUDO_HOVER;
            else if (strcmp(texts[i], "focus") == 0) sel->pseudo |= gX_GUI_CSS_PSEUDO_FOCUS;
            else if (strcmp(texts[i], "active") == 0) sel->pseudo |= gX_GUI_CSS_PSEUDO_ACTIVE;
            else if (strcmp(texts[i], "disabled") == 0) sel->pseudo |= gX_GUI_CSS_PSEUDO_DISABLED;
        }
    }
}

static gX_I1 gX_GUI_CSS_ParseDecls(gX_I8 **pp, gX_CSS_Rule *rule) {
    gX_I8 *p = *pp;
    while (1) {
        gX_GUI_CSS_SkipWs(&p);
        if (!*p) break; //opencode: EOF after decls is valid (brace-less inline)
        if (*p == '}') { p++; break; }
        gX_I8 propName[64];
        gX_U32 pn = 0;
        while (*p && *p != ':' && *p != '}' && *p != ';') {
            if (*p != ' ' && *p != '\t' && pn < sizeof(propName) - 1) propName[pn++] = *p;
            p++;
        }
        propName[pn] = '\0';
        if (*p == ':') {
            p++;
            gX_I8 valueStr[256];
            gX_U32 vn = 0;
            while (*p && *p != ';' && *p != '}') {
                if (vn < sizeof(valueStr) - 1) valueStr[vn++] = *p;
                p++;
            }
            valueStr[vn] = '\0';
            //opencode: trim leading + trailing whitespace (strtof skips
            //   leading ws but leaves end==tok on no-conversion — the color/
            //   keyword paths then see a bogus leading space)
            gX_U32 lead = 0;
            while (valueStr[lead] == ' ' || valueStr[lead] == '\t') lead++;
            if (lead > 0) {
                gX_U32 li = 0;
                for (; valueStr[lead + li]; li++) valueStr[li] = valueStr[lead + li];
                valueStr[li] = '\0';
                vn = li;
            }
            while (vn > 0 && (valueStr[vn-1] == ' ' || valueStr[vn-1] == '\t')) valueStr[--vn] = '\0';
            
            gX_I32 prop = gX_GUI_CSS_PropFromName(propName);
            if (prop != gX_CSS_PROP_NULL && rule->declCount < gX_GUI_CSS_MAX_DECLS_PER_RULE) {
                gX_I1 important = false;
                //opencode: strip !important suffix (may be preceded by a
                //   space: "0.5 !important")
                gX_I8 *imp = strstr(valueStr, "!important");
                if (imp && imp[10] == '\0' && (imp == valueStr || imp[-1] == ' ' || imp[-1] == '\t')) {
                    important = true;
                    *imp = '\0';
                    gX_U32 tl = (gX_U32)strlen(valueStr);
                    while (tl > 0 && (valueStr[tl-1] == ' ' || valueStr[tl-1] == '\t')) valueStr[--tl] = '\0';
                }
                gX_CSS_Value value;
                if (gX_GUI_CSS_ParseValue(valueStr, &value)) {
                    gX_CSS_Decl *decl = &rule->decls[rule->declCount];
                    decl->prop = prop;
                    decl->value = value;
                    decl->important = important;
                    rule->declCount++;
                }
            }
        }
        if (*p == ';') { p++; continue; }
        if (*p == '}') { p++; break; }
        if (!*p) return false;
    }
    *pp = p;
    return rule->declCount > 0;
}

gX_I32 gX_GUI_CSS_ParseSheet(const gX_I8 *text, gX_CSS_StyleSheet *sheet) {
    if (!text || !sheet) return 0;
    sheet->ruleCount = 0;
    gX_I8 *p = (gX_I8*)text;
    //opencode: brace-less text (inline `style=` strings) is one decl block
    if (strchr(text, '{') == NULL) {
        gX_CSS_Rule rule;
        memset(&rule, 0, sizeof(rule));
        gX_I8 *q = p;
        if (gX_GUI_CSS_ParseDecls(&q, &rule) && sheet->ruleCount < gX_GUI_CSS_MAX_RULES_PER_SHEET) {
            rule.order = sheet->ruleCount;
            rule.origin = 2; //opencode: brace-less = inline style origin
            sheet->rules[sheet->ruleCount++] = rule;
        }
        return (gX_I32)sheet->ruleCount;
    }
    while (1) {
        gX_GUI_CSS_SkipWs(&p);
        if (!*p) break;
        //opencode: selectors up to '{'
        gX_I8 selBuf[512];
        gX_U32 sn = 0;
        while (*p && *p != '{' && *p != '}') {
            if (sn < sizeof(selBuf) - 1) selBuf[sn++] = *p;
            p++;
        }
        selBuf[sn] = '\0';
        if (*p != '{') break;
        p++; // consume '{'
        
        //opencode: parse the shared decl block ONCE, then clone into each
        //   comma-group rule (`.a, .b { ... }` — all groups share the block)
        gX_CSS_Rule declBlock;
        memset(&declBlock, 0, sizeof(declBlock));
        gX_I1 declOk = gX_GUI_CSS_ParseDecls(&p, &declBlock);
        
        //opencode: split comma groups
        gX_I8 *groups[16];
        gX_I32 gCount = 0;
        gX_I8 *gStart = selBuf;
        for (gX_U32 i = 0; i < sn; i++) {
            if (selBuf[i] == ',') {
                selBuf[i] = '\0';
                if (gCount < 16) groups[gCount++] = gStart;
                gStart = &selBuf[i + 1];
            }
        }
        if (gCount < 16) groups[gCount++] = gStart;
        
        for (gX_I32 gi = 0; gi < gCount && sheet->ruleCount < gX_GUI_CSS_MAX_RULES_PER_SHEET; gi++) {
            gX_GUI_CSS_SkipWs((gX_I8**)&groups[gi]);
            if (!groups[gi][0]) continue;
            gX_CSS_Rule *rule = &sheet->rules[sheet->ruleCount];
            gX_GUI_CSS_ParseCompound(&rule->selector, groups[gi]);
            rule->order = sheet->ruleCount;
            rule->origin = 1;
            if (declOk) {
                rule->declCount = declBlock.declCount;
                for (gX_U32 d = 0; d < declBlock.declCount; d++) rule->decls[d] = declBlock.decls[d];
                sheet->ruleCount++;
            }
        }
        if (!declOk) {
            while (*p && *p != '}') p++;
            if (*p) p++;
        }
    }
    return (gX_I32)sheet->ruleCount;
}

//=============================================================================
// selector matching + declaration application
//=============================================================================

//opencode: element type name lowercased (CSS type selectors are case-insensitive)
static const gX_I8 *gX_GUI_CSS_ElementTypeName(const gX_GUI_Element *element) {
    return gX_GUI_Element_Type_String((gX_I32)element->type);
}

static gX_I1 gX_GUI_CSS_TypeMatches(const gX_I8 *selType, const gX_I8 *elemType) {
    if (selType[0] == '\0') return true;
    for (gX_U32 i = 0; ; i++) {
        gX_I8 a = selType[i], b = elemType[i];
        if (a >= 'A' && a <= 'Z') a = (gX_I8)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (gX_I8)(b - 'A' + 'a');
        if (a == '\0' && b == '\0') return true;
        if (a != b) return false;
    }
}

gX_I1 gX_GUI_CSS_MatchSelector(const gX_CSS_Selector *sel, const gX_GUI_Element *element, const gX_GUI_Canvas *canvas) {
    if (!sel || !element) return false;
    if (!gX_GUI_CSS_TypeMatches(sel->type, gX_GUI_CSS_ElementTypeName(element))) return false;
    if (sel->id[0] && strcmp(sel->id, element->name) != 0) return false;
    if (sel->cls[0]) {
        //opencode: class list is space-separated on the element
        gX_I8 cls[gX_GUI_CSS_STR_MAX];
        snprintf(cls, sizeof(cls), "%s", element->cssClass);
        gX_I8 *tok = strtok(cls, " ");
        gX_I1 found = false;
        while (tok) {
            if (strcmp(tok, sel->cls) == 0) { found = true; break; }
            tok = strtok(NULL, " ");
        }
        if (!found) return false;
    }
    if (sel->pseudo & gX_GUI_CSS_PSEUDO_HOVER)    { if (!element->state.HOVERED) return false; }
    if (sel->pseudo & gX_GUI_CSS_PSEUDO_FOCUS)    { if (!element->state.FOCUSED) return false; }
    if (sel->pseudo & gX_GUI_CSS_PSEUDO_ACTIVE)   { if (!element->state.PRESSED_LEFT) return false; }
    if (sel->pseudo & gX_GUI_CSS_PSEUDO_DISABLED) { if (!element->state.DISABLED) return false; }
    //opencode: descendant — the element must be a layout child of an element
    //   of the ancestor type (v1: scan the canvas for a matching container)
    if (sel->ancestorType[0]) {
        if (!canvas || !canvas->elements.data) return false;
        gX_GUI_Element *elems = canvas->elements.data;
        gX_I1 found = false;
        for (gX_U32 i = 0; i < canvas->elements.amount && !found; i++) {
            gX_GUI_Element *parent = &elems[i];
            if (parent->layouts.amount == 0 || !parent->layouts.data) continue;
            if (!gX_GUI_CSS_TypeMatches(sel->ancestorType, gX_GUI_CSS_ElementTypeName(parent))) continue;
            gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)parent->layouts.data)[0];
            for (gX_U32 c = 0; c < layout->childCount; c++) {
                if ((gX_U32)layout->children[c] == element->id) { found = true; break; }
            }
        }
        if (!found) return false;
    }
    return true;
}

//opencode: apply a declaration to element fields. Grid-unit numbers. CSS
//   `color` = text color (web semantics); `background-color` = element rect.
gX_I0 gX_GUI_CSS_ApplyDecl(gX_GUI_Element *element, gX_GUI_Canvas *canvas, const gX_CSS_Decl *decl) {
    if (!element || !decl || decl->prop == gX_CSS_PROP_NULL) return;
    const gX_CSS_Value *v = &decl->value;
    gX_I32 p = decl->prop;
    
    switch (p) {
    case gX_CSS_PROP_BACKGROUND_COLOR:
        if (v->type == gX_CSS_VALUE_COLOR) element->color = v->color;
        break;
    case gX_CSS_PROP_COLOR:
        if (v->type == gX_CSS_VALUE_COLOR && element->texts.amount > 0 && element->texts.data) {
            gX_GUI_Element_Text *t = &((gX_GUI_Element_Text*)element->texts.data)[0];
            t->color = v->color;
            t->colorOriginal = v->color;
        }
        break;
    case gX_CSS_PROP_FONT_SIZE:
        if (v->type == gX_CSS_VALUE_NUMBER && element->texts.amount > 0 && element->texts.data) {
            ((gX_GUI_Element_Text*)element->texts.data)[0].fontSize = v->number;
        }
        break;
    case gX_CSS_PROP_LINE_HEIGHT:
        if (v->type == gX_CSS_VALUE_NUMBER && element->texts.amount > 0 && element->texts.data) {
            //opencode: lineSpacing is an EXTRA multiplier over fontSize
            ((gX_GUI_Element_Text*)element->texts.data)[0].lineSpacing = v->number - 1.0f;
        }
        break;
    case gX_CSS_PROP_TEXT_ALIGN:
        if (v->type == gX_CSS_VALUE_KEYWORD) {
            if (strcmp(v->str, "center") == 0) {
                element->stateRendering.TEXT_CENTER_X = true;
                element->stateRendering.TEXT_CENTER_Y = true;
            } else if (strcmp(v->str, "left") == 0 || strcmp(v->str, "start") == 0) {
                element->stateRendering.TEXT_CENTER_X = false;
                element->stateRendering.TEXT_CENTER_Y = false;
            }
        }
        break;
    case gX_CSS_PROP_WIDTH:
    case gX_CSS_PROP_HEIGHT:
        if (v->type == gX_CSS_VALUE_NUMBER && canvas && canvas->canvasBox.width > 0) {
            //opencode: CSS numbers are canvas GRID units → pixels
            gX_F32 gu = canvas->canvasBox.width / (gX_F32)gX_GUI_BUILDER_DIVISIONS;
            gX_F32 gv = canvas->canvasBox.height / (gX_F32)gX_GUI_BUILDER_DIVISIONS;
            if (p == gX_CSS_PROP_WIDTH) element->rect.width = v->number * gu;
            else element->rect.height = v->number * gv;
        }
        break;
    case gX_CSS_PROP_MARGIN:
        if (v->type == gX_CSS_VALUE_NUMBER) {
            element->margin[0] = element->margin[1] = element->margin[2] = element->margin[3] = v->number;
        }
        break;
    case gX_CSS_PROP_MARGIN_TOP:    if (v->type == gX_CSS_VALUE_NUMBER) element->margin[0] = v->number; break;
    case gX_CSS_PROP_MARGIN_RIGHT:  if (v->type == gX_CSS_VALUE_NUMBER) element->margin[1] = v->number; break;
    case gX_CSS_PROP_MARGIN_BOTTOM: if (v->type == gX_CSS_VALUE_NUMBER) element->margin[2] = v->number; break;
    case gX_CSS_PROP_MARGIN_LEFT:   if (v->type == gX_CSS_VALUE_NUMBER) element->margin[3] = v->number; break;
    case gX_CSS_PROP_PADDING:
    case gX_CSS_PROP_PADDING_TOP:
    case gX_CSS_PROP_PADDING_RIGHT:
    case gX_CSS_PROP_PADDING_BOTTOM:
    case gX_CSS_PROP_PADDING_LEFT:
        if (v->type == gX_CSS_VALUE_NUMBER && element->layouts.amount > 0 && element->layouts.data) {
            gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
            if (p == gX_CSS_PROP_PADDING) { layout->padding[0] = layout->padding[1] = layout->padding[2] = layout->padding[3] = v->number; }
            else if (p == gX_CSS_PROP_PADDING_TOP) layout->padding[0] = v->number;
            else if (p == gX_CSS_PROP_PADDING_RIGHT) layout->padding[1] = v->number;
            else if (p == gX_CSS_PROP_PADDING_BOTTOM) layout->padding[2] = v->number;
            else if (p == gX_CSS_PROP_PADDING_LEFT) layout->padding[3] = v->number;
        }
        break;
    case gX_CSS_PROP_DISPLAY:
        if (v->type == gX_CSS_VALUE_KEYWORD) {
            if (strcmp(v->str, "none") == 0) {
                element->visible = 0;
            } else if (element->layouts.amount > 0 && element->layouts.data) {
                gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
                if (strcmp(v->str, "block") == 0) layout->display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_BLOCK;
                else if (strcmp(v->str, "flex-row") == 0) layout->display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW;
                else if (strcmp(v->str, "flex-col") == 0 || strcmp(v->str, "flex-column") == 0) layout->display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_COL;
            }
        }
        break;
    case gX_CSS_PROP_FLEX_DIRECTION:
        if (v->type == gX_CSS_VALUE_KEYWORD && element->layouts.amount > 0 && element->layouts.data) {
            gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
            if (strcmp(v->str, "row") == 0) layout->display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW;
            else if (strcmp(v->str, "column") == 0) layout->display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_COL;
        }
        break;
    case gX_CSS_PROP_JUSTIFY_CONTENT:
        if (v->type == gX_CSS_VALUE_KEYWORD && element->layouts.amount > 0 && element->layouts.data) {
            gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
            if (strcmp(v->str, "center") == 0) layout->justify = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_CENTER;
            else if (strcmp(v->str, "end") == 0 || strcmp(v->str, "flex-end") == 0) layout->justify = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_END;
            else if (strcmp(v->str, "space-between") == 0) layout->justify = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_SPACE_BETWEEN;
            else layout->justify = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_START;
        }
        break;
    case gX_CSS_PROP_ALIGN_ITEMS:
        if (v->type == gX_CSS_VALUE_KEYWORD && element->layouts.amount > 0 && element->layouts.data) {
            gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
            if (strcmp(v->str, "center") == 0) layout->align = gX_GUI_ELEMENT_LAYOUT_ALIGN_CENTER;
            else if (strcmp(v->str, "end") == 0 || strcmp(v->str, "flex-end") == 0) layout->align = gX_GUI_ELEMENT_LAYOUT_ALIGN_END;
            else if (strcmp(v->str, "stretch") == 0) layout->align = gX_GUI_ELEMENT_LAYOUT_ALIGN_STRETCH;
            else layout->align = gX_GUI_ELEMENT_LAYOUT_ALIGN_START;
        }
        break;
    case gX_CSS_PROP_GAP:
        if (v->type == gX_CSS_VALUE_NUMBER && element->layouts.amount > 0 && element->layouts.data) {
            ((gX_GUI_Element_Layout*)element->layouts.data)[0].gap = v->number;
        }
        break;
    case gX_CSS_PROP_OPACITY:
        if (v->type == gX_CSS_VALUE_NUMBER) {
            gX_F32 o = v->number;
            if (o < 0.0f) o = 0.0f;
            if (o > 1.0f) o = 1.0f;
            element->color.a = (gX_U8)(255.0f * o);
        }
        break;
    case gX_CSS_PROP_LAYER:
    case gX_CSS_PROP_Z_INDEX:
        if (v->type == gX_CSS_VALUE_NUMBER) element->layer = (gX_U8)v->number;
        break;
    case gX_CSS_PROP_VISIBLE:
        if (v->type == gX_CSS_VALUE_KEYWORD) {
            element->visible = (strcmp(v->str, "visible") == 0 || strcmp(v->str, "true") == 0) ? 1 : 0;
        }
        break;
    case gX_CSS_PROP_ANGLE:
        if (v->type == gX_CSS_VALUE_NUMBER) element->params.angle = v->number;
        break;
    case gX_CSS_PROP_SHADER_INDEX:
        if (v->type == gX_CSS_VALUE_NUMBER) {
            gX_U32 idx = (gX_U32)v->number;
            if (element->textures.amount > 0 && element->textures.data) {
                gX_GUI_Element_Texture *t = &((gX_GUI_Element_Texture*)element->textures.data)[0];
                t->useShader = true; t->shaderIndex = idx;
            }
            if (element->shapes.amount > 0 && element->shapes.data) {
                gX_GUI_Element_Shape *s = &((gX_GUI_Element_Shape*)element->shapes.data)[0];
                s->useShader = true; s->shaderIndex = idx;
            }
        }
        break;
    default:
        //opencode: accepted-but-inert v1 properties (position, flex-grow,
        //   order, min/max sizes, border-*, box-sizing) — parsed, not applied
        break;
    }
    (void)canvas;
}

//=============================================================================
// canvas resolve
//=============================================================================

//opencode: cascade sort — later wins. Lexicographic key over the cascade
//   fields in precedence order: origin, importance, specificity, source
//   order. The old weighted key (origin*64 + important*2 + specificity*3)
//   let a single specificity point outweigh !important and let a
//   default-origin ID rule outrank a later-origin rule. Field-by-field
//   comparison keeps each dimension dominant. Equal keys keep collection
//   order (stable insertion sort in the resolver).
gX_I1 gX_GUI_CSS_KeyLess(const gX_CSS_Decl *a, const gX_CSS_Rule *ra,
                         const gX_CSS_Decl *b, const gX_CSS_Rule *rb) {
    if (ra->origin != rb->origin) return ra->origin < rb->origin;
    gX_I1 ia = (a->important != 0);
    gX_I1 ib = (b->important != 0);
    if (ia != ib) return ia < ib;
    if (ra->selector.specificity != rb->selector.specificity)
        return ra->selector.specificity < rb->selector.specificity;
    return ra->order < rb->order;
}

//opencode: append every declaration whose selector matches the element —
//   built-in gui.style sheet first (origin 0, sorts lowest), then the sheets
//   attached to the canvas. When outDecls is NULL the pass only counts, so
//   the resolver can size its allocation exactly. The cascade is
//   non-truncating: the fixed 64-candidate stack array silently dropped
//   matching declarations past the cap, so the highest-key (winning) rule
//   could vanish. Returns the number of matching declarations.
static gX_U32 gX_GUI_CSS_CollectCandidates(const gX_GUI_Canvas *canvas, const gX_GUI_Element *el,
                                           gX_CSS_Decl *outDecls, gX_CSS_Rule **outRules) {
    gX_U32 count = 0;
    if (g_cssDefaultSheetBuilt) {
        for (gX_U32 r = 0; r < g_cssDefaultSheet.ruleCount; r++) {
            gX_CSS_Rule *rule = &g_cssDefaultSheet.rules[r];
            if (!gX_GUI_CSS_MatchSelector(&rule->selector, el, canvas)) continue;
            for (gX_U32 d = 0; d < rule->declCount; d++) {
                if (outDecls) { outDecls[count] = rule->decls[d]; outRules[count] = rule; }
                count++;
            }
        }
    }
    for (gX_U32 s = 0; s < canvas->cssSheetCount; s++) {
        gX_I32 si = canvas->cssSheets[s];
        if (si < 0 || (gX_U32)si >= gX_GUI_CSS_MAX_SHEETS) continue;
        if (si >= (gX_I32)g_cssSheetCount) continue;
        gX_CSS_StyleSheet *sheet = &g_cssSheets[si];
        for (gX_U32 r = 0; r < sheet->ruleCount; r++) {
            gX_CSS_Rule *rule = &sheet->rules[r];
            if (!gX_GUI_CSS_MatchSelector(&rule->selector, el, canvas)) continue;
            for (gX_U32 d = 0; d < rule->declCount; d++) {
                if (outDecls) { outDecls[count] = rule->decls[d]; outRules[count] = rule; }
                count++;
            }
        }
    }
    return count;
}

gX_I0 gX_GUI_CSS_ResolveCanvas(gX_GUI_Canvas *canvas) {
    //opencode: rate-limited mtime watch — runs before the early return so
    //   reloads are detected even when this canvas has no sheets attached
    gX_GUI_CSS_CheckReload();
    if (!canvas || canvas->elements.data == NULL || canvas->cssSheetCount == 0) {
        //opencode: still apply inline styles when no sheets are attached
        if (canvas && canvas->elements.data) {
            gX_GUI_Element *elems = canvas->elements.data;
            for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
                if (!canvas->elements.meta || !canvas->elements.meta[i].USED) continue;
                gX_GUI_Element *el = &elems[i];
                if (el->cssStyle[0]) {
                    gX_CSS_StyleSheet tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    gX_GUI_CSS_ParseSheet(el->cssStyle, &tmp);
                    for (gX_U32 d = 0; d < tmp.ruleCount; d++) {
                        for (gX_U32 di = 0; di < tmp.rules[d].declCount; di++) {
                            gX_GUI_CSS_ApplyDecl(el, canvas, &tmp.rules[d].decls[di]);
                        }
                    }
                }
            }
        }
        return;
    }
    
    gX_GUI_Element *elems = canvas->elements.data;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        gX_GUI_Element *el = &elems[i];
        if (!canvas->elements.meta[i].USED) continue;
        
        //opencode: collect matching declarations — built-in gui.style sheet
        //   first (origin 0, sorts lowest), then sheets attached to the canvas.
        //   Count first, then allocate exactly: no fixed candidate cap, so the
        //   winning (highest-key) declaration is never silently dropped.
        gX_U32 candCount = gX_GUI_CSS_CollectCandidates(canvas, el, NULL, NULL);
        
        if (candCount > 0) {
            gX_CSS_Decl *candidates = (gX_CSS_Decl*)malloc(candCount * sizeof(gX_CSS_Decl));
            gX_CSS_Rule **candRules = (gX_CSS_Rule**)malloc(candCount * sizeof(gX_CSS_Rule*));
            if (!candidates || !candRules) {
                free(candidates);
                free(candRules);
                gX_TraceLog(gX_LOG_ERROR, "CSS: candidate allocation failed (%u decls) for element '%s'",
                    candCount, el->name);
                continue;
            }
            gX_GUI_CSS_CollectCandidates(canvas, el, candidates, candRules);
            //opencode: stable insertion sort by cascade key
            for (gX_U32 a = 1; a < candCount; a++) {
                gX_CSS_Decl keyD = candidates[a];
                gX_CSS_Rule *keyR = candRules[a];
                gX_U32 b = a;
                while (b > 0 && gX_GUI_CSS_KeyLess(&keyD, keyR, &candidates[b - 1], candRules[b - 1])) {
                    candidates[b] = candidates[b - 1];
                    candRules[b] = candRules[b - 1];
                    b--;
                }
                candidates[b] = keyD;
                candRules[b] = keyR;
            }
            for (gX_U32 d = 0; d < candCount; d++) {
                gX_GUI_CSS_ApplyDecl(el, canvas, &candidates[d]);
            }
            free(candidates);
            free(candRules);
        }
        
        //opencode: inline style — origin 2, always beats stylesheet rules
        if (el->cssStyle[0]) {
            gX_CSS_StyleSheet tmp;
            memset(&tmp, 0, sizeof(tmp));
            gX_GUI_CSS_ParseSheet(el->cssStyle, &tmp);
            for (gX_U32 r = 0; r < tmp.ruleCount; r++) {
                for (gX_U32 d = 0; d < tmp.rules[r].declCount; d++) {
                    gX_GUI_CSS_ApplyDecl(el, canvas, &tmp.rules[r].decls[d]);
                }
            }
        }
    }
}
