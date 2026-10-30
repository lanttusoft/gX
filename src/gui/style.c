/**********************************************************************************************
*
*   [gX: src/gui/style.c]
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

//opencode: design-token system — theme.json overrides gui.style at load time.
//   The marshal schema mirrors the gX_GUI_Style struct, so the JSON file is
//   the single source of truth for the aesthetic; missing keys fall back to
//   the C defaults (gui.c). Hot-reload watches the file mtime (rate-limited,
//   same pattern as gX_GUI_CSS_CheckReload) and flags canvases for rebuild.
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "style.h"
#include "css.h"
#include "canvas.h"
#include "../common/marshal/json.h"
#include "../common/log.h"
#include "../common/utils.h"
#include "../common/macros.h"
#include <math.h>

static gX_I8 s_themePath[128];
static gX_I64 s_themeMtime = 0;
static gX_U64 s_lastReloadMs = 0;

static gX_JSON_Schema s_styleSchema;

#define GX_SCHEMAJSON_ADDOBJECT(name) \
    GX_SCHEMAJSON_ADDOBJECT_TEMPLATE(s_styleSchema,name) \

//ai: share per-token registration logic instead of expanding it at every field.
static gX_I0 gX_GUI_Style_Schema_AddTerm(const gX_I8 *name, gX_I0 *dataPtr,
    gX_I32 datatype, const gX_I8 *remark)
{
    GX_SCHEMAJSON_ADDOBJECT_TEMPLATE(s_styleSchema,name)
    if (jsoncontext.currentObject) {
        if (jsoncontext.currentContextStack >= 0) {
            jsoncontext.currentObject->hasUpper = true;
            jsoncontext.currentObject->upper =
                jsoncontext.currentContext[jsoncontext.currentContextStack];
        }
        if (jsoncontext.JSONschemaDebug) {
            gX_TraceLog(gX_LOG_DEBUG,"JSON_SCHEMA: <%s> currentContext[%i]: %s",
                name,jsoncontext.currentContextStack,
                jsoncontext.currentContext[jsoncontext.currentContextStack]->key);
        }
        jsoncontext.currentObject->dataPtr = dataPtr;
        jsoncontext.currentObject->datatype = datatype;
        strcpy(jsoncontext.currentObject->dataRemark,remark);
    }
}

#undef GX_SCHEMAJSON_ADDOBJECT_TERM
#define GX_SCHEMAJSON_ADDOBJECT_TERM(name,ptr,type) \
    gX_GUI_Style_Schema_AddTerm(#name,&ptr,gX_DATATYPE_##type,#ptr);

static gX_I0 gX_GUI_Style_Schema_Init(gX_I0) {
    //opencode: gX — compact arena (was 8192 static = 2.5 MB bss for 101 fields);
    //   enlarged for the text-shadow + surface-depth token groups. The arena
    //   has no bounds check while registering, so keep headroom here.
    gX_JSON_Schema_EnsureArena(&s_styleSchema, 192);
    s_styleSchema.loadedCount = 0;
    GX_SCHEMAJSON_INIT
    
    // --- core (global) tokens ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(core)
        GX_SCHEMAJSON_ADDOBJECT_TERM(panelBg,(gui.style.panelBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textColor,(gui.style.textColor),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(focusRing,(gui.style.focusRing),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(focusRingOffset,(gui.style.focusRingOffset),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(fontSize,(gui.style.fontSize),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(fontSpacing,(gui.style.fontSpacing),I32)

        //opencode: v0.2.0 — SDF widget surface tokens (guiwidget.frag)
        GX_SCHEMAJSON_ADDOBJECT_TERM(widgetSDF,(gui.style.widgetSDF),I1)
        GX_SCHEMAJSON_ADDOBJECT_TERM(cornerRadius,(gui.style.cornerRadius),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(shadowOpacity,(gui.style.shadowOpacity),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(gradientStrength,(gui.style.gradientStrength),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(borderWidth,(gui.style.borderWidth),F32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonBase,(gui.style.buttonBase),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonHover,(gui.style.buttonHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonText,(gui.style.buttonText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonW,(gui.style.buttonW),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonH,(gui.style.buttonH),F32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(backBase,(gui.style.backBase),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backHover,(gui.style.backHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backText,(gui.style.backText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backW,(gui.style.backW),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backH,(gui.style.backH),F32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(playBase,(gui.style.playBase),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(playHover,(gui.style.playHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(extraBase,(gui.style.extraBase),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(extraHover,(gui.style.extraHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(quitBase,(gui.style.quitBase),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(quitHover,(gui.style.quitHover),COLOR)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(sectionHeader,(gui.style.sectionHeader),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(sectionDividerH,(gui.style.sectionDividerH),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(panelOverlay,(gui.style.panelOverlay),COLOR)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(titleSize,(gui.style.titleSize),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(bodySize,(gui.style.bodySize),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(smallSize,(gui.style.smallSize),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(creditsSize,(gui.style.creditsSize),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(creditsSpacing,(gui.style.creditsSpacing),I32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(hoverSeconds,(gui.style.hoverSeconds),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(hoverMultiplier,(gui.style.hoverMultiplier),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(hoverScale,(gui.style.hoverScale),F32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(scrollbarW,(gui.style.scrollbarW),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(scrolllistItemH,(gui.style.scrolllistItemH),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(stepperBtnW,(gui.style.stepperBtnW),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(tabBarH,(gui.style.tabBarH),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(tabGutter,(gui.style.tabGutter),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(sliderH,(gui.style.sliderH),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(knobRadius,(gui.style.knobRadius),F32)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(scrollbarTrack,(gui.style.scrollbarTrack),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(scrollbarThumb,(gui.style.scrollbarThumb),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(scrollbarArrow,(gui.style.scrollbarArrow),COLOR)
        
        GX_SCHEMAJSON_ADDOBJECT_TERM(selectedBg,(gui.style.selectedBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(unselectedBg,(gui.style.unselectedBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(selectedText,(gui.style.selectedText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(unselectedText,(gui.style.unselectedText),COLOR)
    GX_SCHEMAJSON_POPCONTEXT
    
    // --- v0.6.0 a11y: surface stack + On- pairs + motion + type ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(a11y)
        GX_SCHEMAJSON_ADDOBJECT_TERM(surfaceCanvas,(gui.style.a11y.surfaceCanvas),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(surfaceRaised,(gui.style.a11y.surfaceRaised),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(surfaceOverlay,(gui.style.a11y.surfaceOverlay),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(surfaceSunken,(gui.style.a11y.surfaceSunken),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(surfaceInverse,(gui.style.a11y.surfaceInverse),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnCanvas,(gui.style.a11y.textOnCanvas),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnRaised,(gui.style.a11y.textOnRaised),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnOverlay,(gui.style.a11y.textOnOverlay),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnSunken,(gui.style.a11y.textOnSunken),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnInverse,(gui.style.a11y.textOnInverse),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(motionShort,(gui.style.a11y.motionShort),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(motionBase,(gui.style.a11y.motionBase),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(motionLong,(gui.style.a11y.motionLong),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(motionReduced,(gui.style.a11y.motionReduced),I1)
        GX_SCHEMAJSON_ADDOBJECT_TERM(typeLineHeight,(gui.style.a11y.typeLineHeight),F32)
    GX_SCHEMAJSON_POPCONTEXT

    // --- text shadow (per-element TEXT_SHADOW flag) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(textShadow)
        GX_SCHEMAJSON_ADDOBJECT_TERM(offsetX,(gui.style.textShadow.offsetX),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(offsetY,(gui.style.textShadow.offsetY),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(offsetXSmall,(gui.style.textShadow.offsetXSmall),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(offsetYSmall,(gui.style.textShadow.offsetYSmall),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(softness,(gui.style.textShadow.softness),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(color,(gui.style.textShadow.color),COLOR)
    GX_SCHEMAJSON_POPCONTEXT

    // --- text outline (contrast themes) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(textOutline)
        GX_SCHEMAJSON_ADDOBJECT_TERM(width,(gui.style.textOutline.width),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(color,(gui.style.textOutline.color),COLOR)
    GX_SCHEMAJSON_POPCONTEXT

    // --- surface depth profiles (SDF gradient look) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(surfaceDepth)
        GX_SCHEMAJSON_ADDOBJECT_TERM(mode,(gui.style.surfaceDepth.mode),I32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(horizonPos,(gui.style.surfaceDepth.horizonPos),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(horizonSoft,(gui.style.surfaceDepth.horizonSoft),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(horizonGlow,(gui.style.surfaceDepth.horizonGlow),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(metalBand,(gui.style.surfaceDepth.metalBand),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(metalWidth,(gui.style.surfaceDepth.metalWidth),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(metalStrength,(gui.style.surfaceDepth.metalStrength),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(metalRim,(gui.style.surfaceDepth.metalRim),F32)
    GX_SCHEMAJSON_POPCONTEXT

    // --- Aero-glass surface (glass.frag over gui.ping) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(glass)
        GX_SCHEMAJSON_ADDOBJECT_TERM(blur,(gui.style.glass.blur),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(tint,(gui.style.glass.tint),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(rim,(gui.style.glass.rim),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(refraction,(gui.style.glass.refraction),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(gloss,(gui.style.glass.gloss),F32)
    GX_SCHEMAJSON_POPCONTEXT

    // --- cursor light (SDF surfaces) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(cursorLight)
        GX_SCHEMAJSON_ADDOBJECT_TERM(strength,(gui.style.cursorLight.strength),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(radius,(gui.style.cursorLight.radius),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(glint,(gui.style.cursorLight.glint),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(color,(gui.style.cursorLight.color),COLOR)
    GX_SCHEMAJSON_POPCONTEXT

    // --- shared accent tokens (migration targets) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(shared)
        GX_SCHEMAJSON_ADDOBJECT_TERM(accentGold,(gui.style.shared.accentGold),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(accentCyan,(gui.style.shared.accentCyan),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(panelDark,(gui.style.shared.panelDark),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textDim,(gui.style.shared.textDim),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textLightBlue,(gui.style.shared.textLightBlue),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(translucentWhite,(gui.style.shared.translucentWhite),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(hpGreen,(gui.style.shared.hpGreen),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(alertRed,(gui.style.shared.alertRed),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(goldDim,(gui.style.shared.goldDim),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(borderLight,(gui.style.shared.borderLight),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(textOnCard,(gui.style.shared.textOnCard),COLOR)
    GX_SCHEMAJSON_POPCONTEXT
    
    // --- escape/pause menu ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(escape)
        GX_SCHEMAJSON_ADDOBJECT_TERM(dimOverlay,(gui.style.escape.dimOverlay),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(panelBg,(gui.style.escape.panelBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(titleBarBg,(gui.style.escape.titleBarBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(buttonBg,(gui.style.escape.buttonBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(closeBg,(gui.style.escape.closeBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(titleText,(gui.style.escape.titleText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(hintText,(gui.style.escape.hintText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(savedText,(gui.style.escape.savedText),COLOR)
    GX_SCHEMAJSON_POPCONTEXT
    
    // --- battle HUD ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(battle)
        GX_SCHEMAJSON_ADDOBJECT_TERM(panelBg,(gui.style.battle.panelBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(nameBg,(gui.style.battle.nameBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(hpBg,(gui.style.battle.hpBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(mpBg,(gui.style.battle.mpBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(enBg,(gui.style.battle.enBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(enemyPanelBg,(gui.style.battle.enemyPanelBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(enemyNameBg,(gui.style.battle.enemyNameBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(enemyHpBg,(gui.style.battle.enemyHpBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg,(gui.style.battle.menuBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuItemBg,(gui.style.battle.menuItemBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(magicBg,(gui.style.battle.magicBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(magicItemBg,(gui.style.battle.magicItemBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(skillsBg,(gui.style.battle.skillsBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(skillsItemBg,(gui.style.battle.skillsItemBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(itemsBg,(gui.style.battle.itemsBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(itemsItemBg,(gui.style.battle.itemsItemBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(targetBg,(gui.style.battle.targetBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(overlayBg,(gui.style.battle.overlayBg),COLOR)
    GX_SCHEMAJSON_POPCONTEXT
    
    // --- title2 (title screen) ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(title2)
        GX_SCHEMAJSON_ADDOBJECT_TERM(bgGradientTop,(gui.style.title2.bgGradientTop),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(bgGradientBottom,(gui.style.title2.bgGradientBottom),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(titleCardBg,(gui.style.title2.titleCardBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(moduleAvailable,(gui.style.title2.moduleAvailable),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(moduleUnavailable,(gui.style.title2.moduleUnavailable),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(moduleHover,(gui.style.title2.moduleHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(subButtonBg,(gui.style.title2.subButtonBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(actionBg,(gui.style.title2.actionBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(actionHover,(gui.style.title2.actionHover),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(modelBg,(gui.style.title2.modelBg),COLOR)
        //opencode: v0.3.8 — titlegx token migration (aesthetic presets)
        GX_SCHEMAJSON_ADDOBJECT_TERM(heroBand,(gui.style.title2.heroBand),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(wordmarkText,(gui.style.title2.wordmarkText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(subtitleText,(gui.style.title2.subtitleText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg1,(gui.style.title2.menuBg1),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg2,(gui.style.title2.menuBg2),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg3,(gui.style.title2.menuBg3),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg4,(gui.style.title2.menuBg4),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg5,(gui.style.title2.menuBg5),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuBg6,(gui.style.title2.menuBg6),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText1,(gui.style.title2.menuText1),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText2,(gui.style.title2.menuText2),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText3,(gui.style.title2.menuText3),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText4,(gui.style.title2.menuText4),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText5,(gui.style.title2.menuText5),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(menuText6,(gui.style.title2.menuText6),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(quitBg,(gui.style.title2.quitBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(quitText,(gui.style.title2.quitText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(footerText,(gui.style.title2.footerText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(versionText,(gui.style.title2.versionText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(savePanelBg,(gui.style.title2.savePanelBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(saveHeaderText,(gui.style.title2.saveHeaderText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(saveSlotBg,(gui.style.title2.saveSlotBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(saveSlotText,(gui.style.title2.saveSlotText),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backBg,(gui.style.title2.backBg),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(backText,(gui.style.title2.backText),COLOR)
    GX_SCHEMAJSON_POPCONTEXT

    // --- blueprint (line-art design language) tokens ---
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(blueprint)
        GX_SCHEMAJSON_ADDOBJECT_TERM(inkColor,(gui.style.blueprint.inkColor),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(accentColor,(gui.style.blueprint.accentColor),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(latticeColor,(gui.style.blueprint.latticeColor),COLOR)
        GX_SCHEMAJSON_ADDOBJECT_TERM(latticeAlpha,(gui.style.blueprint.latticeAlpha),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(latticeCell,(gui.style.blueprint.latticeCell),F32)
    GX_SCHEMAJSON_POPCONTEXT
    
    s_styleSchema.loadedCount = (gX_U32)(jsoncontext.currentObjectIndex + 1);
    //opencode: v0.6.1 — derive the arena count from the term count (was a
    //   hand-edited constant that drifted with every token group addition)
    s_styleSchema.objectsCount = s_styleSchema.loadedCount;
}

static gX_I64 gX_GUI_Style_GetMtime(const gX_I8 *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (gX_I64)st.st_mtime;
}

static gX_U64 gX_GUI_Style_NowMs(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_U64)ts.tv_sec * 1000 + (gX_U64)(ts.tv_nsec / 1000000);
}

static gX_I0 gX_GUI_Style_AssertContrast(gX_I0);
static gX_F32 gX_GUI_Style_Contrast(gX_Color a, gX_Color b);

//opencode: C defaults for every theme token — the fallback when a key is
//   missing from theme.json. Previously assigned inline in gX_GUI_Load and
//   never re-applied, so a hot reload that REMOVED a key retained the stale
//   loaded value instead of falling back. The reload path now resets to these
//   defaults before applying JSON (missing keys → C default). Runtime
//   resource flags (uiFont/uiFontSDF/loaded) are deliberately NOT reset —
//   they describe loaded assets, not theme values.
gX_I0 gX_GUI_Style_ResetDefaults(gX_I0) {
    gX_GUI_Style *s = &gui.style;

    s->panelBg        = (gX_Color){63,63,127,255};   // muted purple-blue — title2 headers
    s->textColor      = gX_WHITE;
    s->widgetSDF         = true;
    s->cornerRadius      = 10.0f;
    s->shadowOpacity     = 0.35f;
    s->gradientStrength  = 1.0f;
    s->borderWidth       = 1.0f;

    //opencode: main-menu polish — warm opaque focus ring and stronger footer
    //   tokens keep keyboard selection and utility text legible on the violet bg.
    s->focusRing      = (gX_Color){255,224,96,255};
    s->focusRingOffset = 4.0f;
    s->fontSize       = gX_GUI_FONT_SIZE_DEFAULT;
    s->fontSpacing    = gX_GUI_FONT_SPACING_DEFAULT;

    s->buttonBase     = (gX_Color){127,127,63,255};  // olive — general button
    s->buttonHover    = gX_GREEN;
    s->buttonText     = gX_WHITE;
    s->buttonW        = gX_GUI_WIDGET_BUTTON_WIDTH;
    s->buttonH        = gX_GUI_WIDGET_BUTTON_HEIGHT;

    s->backBase       = (gX_Color){255,127,63,255};  // orange — back/cancel
    s->backHover      = (gX_Color){220,60,60,255};   // muted red (was gX_RED)
    s->backText       = gX_WHITE;
    s->backW          = gX_GUI_WIDGET_BACK_WIDTH;
    s->backH          = gX_GUI_WIDGET_BACK_HEIGHT;

    s->playBase       = (gX_Color){200,255,200,255};   // light green — primary action
    s->playHover      = gX_GREEN;
    s->extraBase      = (gX_Color){255,255,200,255};   // light yellow — secondary action
    s->extraHover     = gX_YELLOW;
    s->quitBase       = (gX_Color){255,200,200,255};   // light red — destructive
    s->quitHover      = (gX_Color){220,50,50,255};     // brighter red (was gX_RED)

    //opencode: readable lavender — (100,80,150,200) was dim against dark
    //   surfaces (theme-lab audit); same hue family, full alpha.
    s->sectionHeader   = (gX_Color){170,150,220,255}; // bright lavender
    s->sectionDividerH = gX_GUI_SECTION_DIVIDER_H;
    s->panelOverlay    = (gX_Color){127,127,127,127};// semi-transparent gray

    s->titleSize       = gX_GUI_FONT_TITLE_SIZE;
    s->bodySize        = gX_GUI_FONT_BODY_SIZE;
    s->smallSize       = gX_GUI_FONT_SMALL_SIZE;
    s->creditsSize     = gX_GUI_FONT_CREDITS_SIZE;
    s->creditsSpacing  = gX_GUI_FONT_CREDITS_SPACING;

    s->hoverSeconds     = gX_GUI_HOVER_TRANSITION_SECONDS;
    s->hoverMultiplier  = gX_GUI_HOVER_COLOR_MULTIPLIER;
    s->hoverScale       = gX_GUI_HOVER_SCALE;

    s->scrollbarW       = gX_GUI_WIDGET_SCROLLBAR_WIDTH;
    s->scrolllistItemH  = gX_GUI_WIDGET_SCROLLLIST_ITEM_H;
    s->stepperBtnW      = gX_GUI_WIDGET_STEPPER_BTN_WIDTH;
    s->tabBarH          = gX_GUI_WIDGET_TABBAR_HEIGHT;
    s->tabGutter        = gX_GUI_WIDGET_TAB_GUTTER;
    s->sliderH          = gX_GUI_WIDGET_PROGRESS_MIN_SLIVER;
    s->knobRadius       = gX_GUI_BUILDER_WIDGET_KNOB_RADIUS;

    s->scrollbarTrack   = (gX_Color){31,31,31,255};
    s->scrollbarThumb   = (gX_Color){95,95,127,255};
    s->scrollbarArrow   = (gX_Color){63,63,63,255};

    s->selectedBg       = (gX_Color){63,127,255,255};
    s->unselectedBg     = (gX_Color){47,47,63,255};
    s->selectedText     = gX_WHITE;
    s->unselectedText   = gX_LIGHTGRAY;

    //opencode: battle HUD colors — matching original scene/battle.c hardcoded values
    s->battle.panelBg      = (gX_Color){8,18,42,224};
    s->battle.nameBg       = (gX_Color){30,58,104,255};
    s->battle.hpBg         = (gX_Color){46,8,20,255};
    s->battle.mpBg         = (gX_Color){8,20,58,255};
    s->battle.enBg         = (gX_Color){54,42,8,255};
    s->battle.enemyPanelBg = (gX_Color){42,10,18,224};
    s->battle.enemyNameBg  = (gX_Color){92,24,30,255};
    s->battle.enemyHpBg    = (gX_Color){62,8,12,255};
    s->battle.menuBg       = (gX_Color){8,12,32,242};
    s->battle.menuItemBg   = (gX_Color){18,30,64,255};
    s->battle.magicBg      = (gX_Color){26,12,58,242};
    s->battle.magicItemBg  = (gX_Color){48,24,92,255};
    s->battle.skillsBg     = (gX_Color){8,42,34,242};
    s->battle.skillsItemBg = (gX_Color){12,70,52,255};
    s->battle.itemsBg      = (gX_Color){54,38,8,242};
    s->battle.itemsItemBg  = (gX_Color){88,60,12,255};
    s->battle.targetBg     = (gX_Color){64,18,24,242};
    s->battle.overlayBg    = (gX_Color){4,8,20,228};

    //opencode: title2 (title screen) colors
    s->title2.bgGradientTop      = (gX_Color){40,0,60,255};
    s->title2.bgGradientBottom   = (gX_Color){100,20,120,255};
    s->title2.titleCardBg        = (gX_Color){255,255,255,255};
    s->title2.moduleAvailable    = (gX_Color){63,127,63,255};
    s->title2.moduleUnavailable  = (gX_Color){63,63,63,255};
    s->title2.moduleHover        = gX_GREEN;
    s->title2.subButtonBg        = (gX_Color){127,127,63,255};
    s->title2.actionBg           = (gX_Color){63,63,127,255};
    s->title2.actionHover        = gX_BLUE;
    s->title2.modelBg            = (gX_Color){30,30,60,255};
    s->title2.heroBand           = (gX_Color){205,192,245,120};
    s->title2.wordmarkText       = (gX_Color){245,242,255,255};
    s->title2.subtitleText       = (gX_Color){245,236,255,245};
    s->title2.menuBg1            = (gX_Color){50,40,80,180};
    s->title2.menuBg2            = (gX_Color){45,35,75,180};
    s->title2.menuBg3            = (gX_Color){40,30,70,180};
    s->title2.menuBg4            = (gX_Color){35,28,65,180};
    s->title2.menuBg5            = (gX_Color){30,25,55,170};
    s->title2.menuBg6            = (gX_Color){28,22,50,160};
    s->title2.menuText1          = (gX_Color){244,240,255,245};
    s->title2.menuText2          = (gX_Color){240,234,254,240};
    s->title2.menuText3          = (gX_Color){236,228,252,235};
    s->title2.menuText4          = (gX_Color){232,224,250,235};
    s->title2.menuText5          = (gX_Color){225,216,246,225};
    s->title2.menuText6          = (gX_Color){220,210,242,220};
    s->title2.quitBg             = (gX_Color){60,20,40,200};
    s->title2.quitText           = (gX_Color){248,190,208,230};
    s->title2.footerText         = (gX_Color){224,216,246,235};
    s->title2.versionText        = (gX_Color){206,198,232,225};
    //opencode: modal sub-screens must isolate live controls from the title
    //   menu underneath; 240 alpha left ghost labels behind settings values.
    s->title2.savePanelBg        = (gX_Color){25,18,45,255};
    s->title2.saveHeaderText     = (gX_Color){245,240,255,240};
    s->title2.saveSlotBg         = (gX_Color){50,40,80,220};
    s->title2.saveSlotText       = (gX_Color){235,228,252,235};
    s->title2.backBg             = (gX_Color){60,30,55,220};
    s->title2.backText           = (gX_Color){250,236,248,240};

    //opencode: blueprint design-language defaults (flat line-art per
    //   frontend_design.md). Zero visual change on the default theme.
    s->blueprint.inkColor        = (gX_Color){40,45,60,255};
    s->blueprint.accentColor     = (gX_Color){120,180,240,255};
    s->blueprint.latticeColor    = (gX_Color){90,120,180,255};
    s->blueprint.latticeAlpha    = 0.22f;
    s->blueprint.latticeCell     = 48.0f;

    //opencode: escape/pause menu colors
    s->escape.dimOverlay   = (gX_Color){20,15,35,220};
    s->escape.panelBg      = (gX_Color){20,15,35,255};
    s->escape.titleBarBg   = (gX_Color){40,30,60,220};
    s->escape.buttonBg     = (gX_Color){50,40,80,225};
    s->escape.closeBg      = (gX_Color){60,50,80,200};
    s->escape.titleText    = (gX_Color){200,200,255,255};
    s->escape.hintText     = (gX_Color){255,255,255,255};
    s->escape.savedText    = (gX_Color){120,120,150,160};

    //opencode: shared accent tokens
    s->shared.accentGold        = (gX_Color){255,220,100,255};
    s->shared.accentCyan        = (gX_Color){100,200,255,255};
    s->shared.panelDark         = (gX_Color){20,20,40,200};
    s->shared.textDim           = (gX_Color){150,150,150,200};
    s->shared.textLightBlue     = (gX_Color){200,200,255,255};
    s->shared.translucentWhite  = (gX_Color){255,255,255,127};
    s->shared.hpGreen           = (gX_Color){100,255,100,200};
    s->shared.alertRed          = (gX_Color){255,100,100,200};
    s->shared.goldDim           = (gX_Color){200,200,100,200};
    s->shared.borderLight       = (gX_Color){200,200,200,200};
    s->shared.textOnCard        = (gX_Color){255,255,255,255};

    //opencode: a11y tokens — surface stack + On- pairs mirror theme.json's
    //   shipped values; motion durations in ms; motionReduced default false;
    //   typeLineHeight default 1.0 (the documented text line-height
    //   multiplier — theme.json ships 1.5 for WCAG 1.4.12)
    s->a11y.surfaceCanvas  = (gX_Color){48,44,78,255};
    s->a11y.surfaceRaised  = (gX_Color){63,63,100,255};
    s->a11y.surfaceOverlay = (gX_Color){35,32,60,255};
    s->a11y.surfaceSunken  = (gX_Color){28,26,48,255};
    s->a11y.surfaceInverse = (gX_Color){240,235,255,255};
    s->a11y.textOnCanvas   = (gX_Color){235,232,255,255};
    s->a11y.textOnRaised   = (gX_Color){240,237,255,255};
    s->a11y.textOnOverlay  = (gX_Color){235,232,255,255};
    s->a11y.textOnSunken   = (gX_Color){200,197,230,255};
    s->a11y.textOnInverse  = (gX_Color){40,36,70,255};
    s->a11y.motionShort    = 150.0f;
    s->a11y.motionBase     = 250.0f;
    s->a11y.motionLong     = 400.0f;
    s->a11y.motionReduced  = false;
    s->a11y.typeLineHeight = 1.0f;

    //opencode: soft default — down-right offset with partial alpha reads as a
    //   shadow without a blur pass; alpha 0 disables it per theme.
    s->textShadow.offsetX  = 3.0f;
    s->textShadow.offsetY  = 3.0f;
    s->textShadow.offsetXSmall = 3.0f;
    s->textShadow.offsetYSmall = 3.0f;
    s->textShadow.softness = 0.0f;
    s->textShadow.color    = (gX_Color){0,0,0,200};

    //opencode: outline off by default; contrast themes opt in.
    s->textOutline.width   = 0.0f;
    s->textOutline.color   = (gX_Color){0,0,0,255};

    //opencode: depth profiles are opt-in — mode 0 keeps authored gradients so
    //   a default theme renders byte-identically to the pre-depth build.
    s->surfaceDepth.mode          = 0;
    s->surfaceDepth.horizonPos    = 0.45f;
    s->surfaceDepth.horizonSoft   = 0.16f;
    s->surfaceDepth.horizonGlow   = 0.28f;
    s->surfaceDepth.metalBand     = 0.38f;
    s->surfaceDepth.metalWidth    = 0.10f;
    s->surfaceDepth.metalStrength = 0.35f;
    s->surfaceDepth.metalRim      = 0.22f;

    //opencode: Aero-glass defaults — readable tint with a soft blur and a
    //   restrained rim/gloss. Nothing renders through glass until a template
    //   opts in (GuiTester demo + any future theme surface).
    //opencode: glass ships off — the flat blueprint default is the design
    //   language; themes (e.g. theme_aero.json) opt in with non-zero tokens.
    s->glass.blur       = 0.0f;
    s->glass.tint       = 0.0f;
    s->glass.rim        = 0.0f;
    s->glass.refraction = 0.0f;
    s->glass.gloss      = 0.0f;

    //opencode: cursor light ships OFF — opt-in via the strength token once
    //   the cost and the accessibility review are done.
    s->cursorLight.strength = 0.0f;
    s->cursorLight.radius   = 160.0f;
    s->cursorLight.glint    = 0.35f;
    s->cursorLight.color    = (gX_Color){255,235,205,255};

    return;
}

gX_I32 gX_GUI_Style_LoadDefault(gX_I0) {
    //opencode: audit hook — GX_GUI_THEME forces a theme file for the duration
    //   of the run without touching the persisted settings (theme gates load
    //   the shipped themes and assert tokens/contrast/pixels).
    const gX_I8 *themeEnv = getenv("GX_GUI_THEME");
    if (themeEnv && themeEnv[0]) {
        strncpy(s_themePath, themeEnv, sizeof(s_themePath) - 1);
        s_themePath[sizeof(s_themePath) - 1] = '\0';
    }
    if (!s_themePath[0]) {
        strncpy(s_themePath, GX_GUI_STYLE_DEFAULT_PATH, sizeof(s_themePath) - 1);
    }
    //opencode: reset to C defaults BEFORE applying JSON so a hot reload that
    //   removed a key falls back to the documented default instead of
    //   retaining its previously loaded value.
    gX_GUI_Style_ResetDefaults();
    if (!s_styleSchema.loadedCount) {
        gX_GUI_Style_Schema_Init();
    }
    json.schema = &s_styleSchema;
    if (gX_JSON_Load(s_themePath) != 0) {
        gX_TraceLog(gX_LOG_NOTICE, "Style: theme file '%s' missing — using C defaults", s_themePath);
        s_themeMtime = 0;
        return 1;
    }
    s_themeMtime = gX_GUI_Style_GetMtime(s_themePath);
    gX_TraceLog(gX_LOG_INFO, "Style: theme loaded from '%s'", s_themePath);
    //opencode: v0.6.0 — WCAG 2.2 AA contrast assertion for the On- pairs
    //   (>= 4.5:1). Warnings only — a theme override that breaks a pair is
    //   reported at load so the author fixes the theme, not the code.
    gX_GUI_Style_AssertContrast();
    return 0;
}

//opencode: v0.6.0 — WCAG relative-luminance contrast check (AA threshold 4.5)
static gX_F32 gX_GUI_Style_Luminance(gX_Color c) {
    gX_F32 r = (gX_F32)c.r / 255.0f, g = (gX_F32)c.g / 255.0f, b = (gX_F32)c.b / 255.0f;
    gX_F32 lr = (r <= 0.03928f) ? r/12.92f : powf((r + 0.055f)/1.055f, 2.4f);
    gX_F32 lg = (g <= 0.03928f) ? g/12.92f : powf((g + 0.055f)/1.055f, 2.4f);
    gX_F32 lb = (b <= 0.03928f) ? b/12.92f : powf((b + 0.055f)/1.055f, 2.4f);
    return 0.2126f*lr + 0.7152f*lg + 0.0722f*lb;
}

static gX_F32 gX_GUI_Style_Contrast(gX_Color a, gX_Color b) {
    gX_F32 la = gX_GUI_Style_Luminance(a), lb = gX_GUI_Style_Luminance(b);
    if (la < lb) { gX_F32 t = la; la = lb; lb = t; }
    return (la + 0.05f) / (lb + 0.05f);
}

static gX_I0 gX_GUI_Style_AssertContrast(gX_I0) {
    struct gX_GUI_Style_Pair { const gX_I8 *name; gX_Color bg; gX_Color fg; };
    const struct gX_GUI_Style_Pair pairs[] = {
        { "canvas",   gui.style.a11y.surfaceCanvas,  gui.style.a11y.textOnCanvas  },
        { "raised",   gui.style.a11y.surfaceRaised,  gui.style.a11y.textOnRaised  },
        { "overlay",  gui.style.a11y.surfaceOverlay, gui.style.a11y.textOnOverlay },
        { "sunken",   gui.style.a11y.surfaceSunken,  gui.style.a11y.textOnSunken  },
        { "inverse",  gui.style.a11y.surfaceInverse, gui.style.a11y.textOnInverse },
    };
    for (gX_U32 i = 0; i < sizeof(pairs)/sizeof(pairs[0]); i++) {
        gX_F32 ratio = gX_GUI_Style_Contrast(pairs[i].bg, pairs[i].fg);
        if (ratio < 4.5f)
            gX_TraceLog(gX_LOG_WARNING, "STYLE: a11y pair '%s' contrast %.2f:1 < 4.5:1 (WCAG AA) — fix theme.json",
                pairs[i].name, ratio);
        else
            gX_TraceLog(gX_LOG_DEBUG, "STYLE: a11y pair '%s' contrast %.2f:1 OK", pairs[i].name, ratio);
    }
    {
        gX_F32 fr = gX_GUI_Style_Contrast(gui.style.focusRing, gui.style.a11y.surfaceCanvas);
        if (fr < 3.0f)
            gX_TraceLog(gX_LOG_WARNING, "STYLE: focus ring vs canvas contrast %.2f:1 < 3:1 (WCAG 2.4.11) — fix theme.json", fr);
    }
}

gX_I0 gX_GUI_Style_SetReducedMotion(gX_I1 reduced) {
    gui.style.a11y.motionReduced = reduced != 0;
}

gX_I0 gX_GUI_Style_SetThemePath(const gX_I8 *path) {
    if (path && path[0]) {
        strncpy(s_themePath, path, sizeof(s_themePath) - 1);
    } else {
        strncpy(s_themePath, GX_GUI_STYLE_DEFAULT_PATH, sizeof(s_themePath) - 1);
    }
    s_themeMtime = 0;
}

const gX_I8 *gX_GUI_Style_GetThemePath(gX_I0) {
    return s_themePath;
}

//opencode: flag every canvas + the global refresh so templates rebuild
//   with the new style values (mirrors the markup hot-reload refresh path)
gX_I0 gX_GUI_Style_MarkRefresh(gX_I0) {
    //opencode: the CSS default sheet derives from gui.style — rebuild it
    //   before the canvases so both token reads and css-class colors agree
    gX_GUI_CSS_BuildDefaultSheet();
    gX_GUI_Canvas *canvases = gui.canvases.data;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        canvases[i].refresh = true;
    }
    gui.refresh = true;
}

//opencode: v0.4.0 — theme editor support (gX Tools suite)

gX_I32 gX_GUI_Style_TokenCount(gX_I0) {
    if (!s_styleSchema.loadedCount) gX_GUI_Style_Schema_Init();
    return (gX_I32)s_styleSchema.loadedCount;
}

const gX_I8 *gX_GUI_Style_TokenKey(gX_I32 index) {
    if (index < 0 || (gX_U32)index >= s_styleSchema.loadedCount) return NULL;
    return s_styleSchema.objects[(gX_U32)index].key;
}

const gX_I8 *gX_GUI_Style_TokenGroup(gX_I32 index) {
    if (index < 0 || (gX_U32)index >= s_styleSchema.loadedCount) return NULL;
    const gX_JSON_Object *obj = &s_styleSchema.objects[(gX_U32)index];
    if (obj->upper) return obj->upper->key;
    return NULL;
}

gX_U32 gX_GUI_Style_TokenDatatype(gX_I32 index) {
    if (index < 0 || (gX_U32)index >= s_styleSchema.loadedCount) return 0;
    return (gX_U32)s_styleSchema.objects[(gX_U32)index].datatype;
}

gX_I0 *gX_GUI_Style_TokenPtr(gX_I32 index) {
    if (index < 0 || (gX_U32)index >= s_styleSchema.loadedCount) return NULL;
    return s_styleSchema.objects[(gX_U32)index].dataPtr;
}

gX_I32 gX_GUI_Style_Save(const gX_I8 *path) {
    if (!path) return 1;
    if (!s_styleSchema.loadedCount) gX_GUI_Style_Schema_Init();
    json.schema = &s_styleSchema;
    gX_I32 err = gX_JSON_Save(path);
    if (err != 0) {
        gX_TraceLog(gX_LOG_ERROR, "Style: failed to save theme '%s'", path);
        return err;
    }
    gX_TraceLog(gX_LOG_INFO, "Style: theme saved to '%s'", path);
    return 0;
}

gX_I0 gX_GUI_Style_CheckReload(gX_I0) {
    if (!s_themePath[0]) return;
    if (gX_GUI_Style_NowMs() - s_lastReloadMs < GX_GUI_STYLE_RELOAD_MS) return;
    s_lastReloadMs = gX_GUI_Style_NowMs();
    gX_I64 mt = gX_GUI_Style_GetMtime(s_themePath);
    if (mt == s_themeMtime) return;
    if (gX_GUI_Style_LoadDefault() != 0) return;
    //opencode: theme changed — rebuild the CSS default sheet (it derives from
    //   gui.style) and rebuild every canvas with the new colors.
    gX_GUI_Style_MarkRefresh();
    gX_TraceLog(gX_LOG_INFO, "Style: theme hot-reloaded");
}
