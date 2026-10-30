/**********************************************************************************************
*
*   [gX: src/gui/element/animation.c]
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

#include "../gui.h"
#include "../utils.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"

#include "../../gxmath/scalar.h"
#include "../../gxmath/vector4.h"
#include "../../gxmath/easing.h"

#include "../../main.h"

inline gX_I0 gX_GUI_Element_Animation_Init(gX_GUI_Element_Animation *anim) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing animation values");
    
    anim->duration = gX_GUI_ANIM_DEFAULT_DURATION;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Animation_Update_Float(gX_GUI_Element_Animation *anim, gX_F32 *target) {
    if (anim->modeAdd) {
        *target = anim->offset.x + anim->result.x;
    }
    else {
        *target = anim->result.x;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_V2(gX_GUI_Element_Animation *anim, gX_F32v2 *target) {
    if (anim->modeAdd) {
        target->x = anim->offset.x + anim->result.x;
        target->y = anim->offset.y + anim->result.y;
    }
    else {
        target->x = anim->result.x;
        target->y = anim->result.y;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_V3(gX_GUI_Element_Animation *anim, gX_F32v3 *target) {
    if (anim->modeAdd) {
        target->x = anim->offset.x + anim->result.x;
        target->y = anim->offset.y + anim->result.y;
        target->z = anim->offset.z + anim->result.z;
    }
    else {
        target->x = anim->result.x;
        target->y = anim->result.y;
        target->z = anim->result.z;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_V4(gX_GUI_Element_Animation *anim, gX_F32v4 *target) {
    if (anim->modeAdd) {
        target->x = anim->offset.x + anim->result.x;
        target->y = anim->offset.y + anim->result.y;
        target->z = anim->offset.z + anim->result.z;
        target->w = anim->offset.w + anim->result.w;
    }
    else {
        target->x = anim->result.x;
        target->y = anim->result.y;
        target->z = anim->result.z;
        target->w = anim->result.w;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_Rectangle(gX_GUI_Element_Animation *anim, gX_Rectangle *target) {
    if (anim->modeAdd) {
        target->x = anim->offset.x + anim->result.x;
        target->y = anim->offset.y + anim->result.y;
        target->width = anim->offset.z + anim->result.z;
        target->height = anim->offset.w + anim->result.w;
    }
    else {
        target->x = anim->result.x;
        target->y = anim->result.y;
        target->width = anim->result.z;
        target->height = anim->result.w;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_Color(gX_GUI_Element_Animation *anim, gX_Color *target) {
    if (anim->modeAdd) {
        target->r = (gX_U8)(anim->offset.x + anim->result.x);
        target->g = (gX_U8)(anim->offset.y + anim->result.y);
        target->b = (gX_U8)(anim->offset.z + anim->result.z);
        target->a = (gX_U8)(anim->offset.w + anim->result.w);
    }
    else {
        target->r = (gX_U8)anim->result.x;
        target->g = (gX_U8)anim->result.y;
        target->b = (gX_U8)anim->result.z;
        target->a = (gX_U8)anim->result.w;
    }
}

static gX_I0 gX_GUI_Element_Animation_Update_Cast(gX_GUI_Element_Animation *anim, gX_GUI_Element *element) {
    //unsafe
    if (anim->doAny) {
        gX_PointerCast(anim->target, (gX_I32)anim->datatype, &(anim->result));
    }
    
    //safe
    if (anim->doRect) {
        gX_GUI_Element_Animation_Update_Rectangle(anim,&(element->rect));
    }
    if (anim->doColor) {
        gX_GUI_Element_Animation_Update_Color(anim,&(element->color));
    }
    if (anim->doScale) {
        gX_GUI_Element_Animation_Update_V2(anim,&(element->params.scale));
    }
    //opencode: angle animation — lerps element->params.angle (single float)
    if (anim->doAngle) {
        gX_GUI_Element_Animation_Update_Float(anim,&(element->params.angle));
    }
    //opencode: bounds check modelIndex against models.amount
    if (anim->doModelParamExtraColorMultiplier) {
        gX_U32 modelIndex = (gX_U32)anim->modelIndex;
        gX_GUI_Element_Model *models = element->models.data;
        if (modelIndex < element->models.amount)
            gX_GUI_Element_Animation_Update_Float(anim,&(models[modelIndex].extraparams.colorMultiplier));
    }
    if (anim->doModelParamRotate) {
        gX_U32 modelIndex = (gX_U32)anim->modelIndex;
        gX_GUI_Element_Model *models = element->models.data;
        if (modelIndex < element->models.amount)
            gX_GUI_Element_Animation_Update_V3(anim,&(models[modelIndex].paramRotate));
    }
    if (anim->doModelParamScale) {
        gX_U32 modelIndex = (gX_U32)anim->modelIndex;
        gX_GUI_Element_Model *models = element->models.data;
        if (modelIndex < element->models.amount)
            gX_GUI_Element_Animation_Update_V3(anim,&(models[modelIndex].paramScale));
    }
    if (anim->doModelParamTranslate) {
        gX_U32 modelIndex = (gX_U32)anim->modelIndex;
        gX_GUI_Element_Model *models = element->models.data;
        if (modelIndex < element->models.amount)
            gX_GUI_Element_Animation_Update_V3(anim,&(models[modelIndex].paramTranslate));
    }
    //opencode: texture param animations — apply to element->textures[textureIndex].params
    if (anim->doTextureParamRect) {
        gX_U32 texIdx = (gX_U32)anim->textureIndex;
        gX_GUI_Element_Texture *txs = element->textures.data;
        if (texIdx < element->textures.amount)
            gX_GUI_Element_Animation_Update_Rectangle(anim,&(txs[texIdx].params.src));
    }
    if (anim->doTextureParamScale) {
        gX_U32 texIdx = (gX_U32)anim->textureIndex;
        gX_GUI_Element_Texture *txs = element->textures.data;
        if (texIdx < element->textures.amount)
            gX_GUI_Element_Animation_Update_V2(anim,&(txs[texIdx].params.scale));
    }
    if (anim->doTextureParamOrigin) {
        gX_U32 texIdx = (gX_U32)anim->textureIndex;
        gX_GUI_Element_Texture *txs = element->textures.data;
        if (texIdx < element->textures.amount)
            gX_GUI_Element_Animation_Update_V2(anim,&(txs[texIdx].params.origin));
    }
    if (anim->doTextureParamRotation) {
        gX_U32 texIdx = (gX_U32)anim->textureIndex;
        gX_GUI_Element_Texture *txs = element->textures.data;
        if (texIdx < element->textures.amount)
            gX_GUI_Element_Animation_Update_Float(anim,&(txs[texIdx].params.rotation));
    }
    if (anim->doTextureParamTint) {
        gX_U32 texIdx = (gX_U32)anim->textureIndex;
        gX_GUI_Element_Texture *txs = element->textures.data;
        if (texIdx < element->textures.amount)
            gX_GUI_Element_Animation_Update_Color(anim,&(txs[texIdx].params.tint));
    }
    //opencode: text param animations — apply to element->texts[textIndex]
    if (anim->doTextFontSize) {
        gX_U32 txtIdx = (gX_U32)anim->textIndex;
        gX_GUI_Element_Text *txts = element->texts.data;
        if (txtIdx < element->texts.amount)
            gX_GUI_Element_Animation_Update_Float(anim,&(txts[txtIdx].fontSize));
    }
    if (anim->doTextOffset) {
        gX_U32 txtIdx = (gX_U32)anim->textIndex;
        gX_GUI_Element_Text *txts = element->texts.data;
        if (txtIdx < element->texts.amount)
            gX_GUI_Element_Animation_Update_V2(anim,&(txts[txtIdx].offset));
    }
    //opencode: text spacing is gX_I32, so we read/write via a float intermediate
    if (anim->doTextSpacing) {
        gX_U32 txtIdx = (gX_U32)anim->textIndex;
        gX_GUI_Element_Text *txts = element->texts.data;
        if (txtIdx < element->texts.amount) {
            gX_F32 f = (gX_F32)txts[txtIdx].spacing;
            if (anim->modeAdd) {
                f = anim->offset.x + anim->result.x;
            } else {
                f = anim->result.x;
            }
            txts[txtIdx].spacing = (gX_I32)f;
        }
    }
    if (anim->doTextColor) {
        gX_U32 txtIdx = (gX_U32)anim->textIndex;
        gX_GUI_Element_Text *txts = element->texts.data;
        if (txtIdx < element->texts.amount)
            gX_GUI_Element_Animation_Update_Color(anim,&(txts[txtIdx].color));
    }
    
}

static gX_I0 gX_GUI_Element_Animation_Update(gX_GUI_Element_Animation *anim, gX_GUI_Element *element) {
    
    //opencode: use local var to avoid clearing executing when trigger is inactive
    gX_I1 triggerActive = false;
    gX_GUI_Element_ExecuteState_Update(&anim->execute, element, &triggerActive);
    if (triggerActive) anim->executing = true;
    
    if (anim->executeAnyway) { anim->executing = true; }
    //opencode: transition latch fires once — without the done guard a still-
    //   running transition re-latched finished anims every frame (visible
    //   replay blink; onStart/onComplete are designed as once-per-run).
    if (anim->readTransitionGUI && gui.transition.executing && !anim->execute.done) {
        anim->executing = true;
    }
    if (anim->reverseWhenNotExecute && anim->executeHint && !(anim->execute.done)) {
        anim->executing = true;
        anim->reverse = true;
        anim->executeHint = false;
    }
    
    if (anim->executing) {
        if (!anim->Tset) {
            anim->T = -anim->durationWait;
            anim->Tset = true;
            anim->started = false;
        }
        
        //opencode: fire onStart when animation first enters the active range
        if (!anim->started && anim->T >= 0.0f) {
            anim->started = true;
            if (anim->onStart) anim->onStart(anim->onStartUserData);
        }

        //move doanimation to anim
        gX_I1 doAnimation = false;
        //opencode: duration<=0 guard — T/duration with both 0 produced NaN
        //   that corrupted element rect/color permanently.
        if (anim->T >= 0.0f && anim->duration > 0.0f && anim->T <= anim->duration) doAnimation = true;
        
        if (doAnimation) {
            gX_F32 t = anim->T/anim->duration;
            if (anim->reverse) { t = 1.0f-t; }
            
            //opencode: apply easing — centralized via gxmath/easing.h
            t = gX_Easing_Apply((gX_Easing)anim->easing, t);
            anim->result = gX_F32v4_Lerp(anim->start, anim->end, t);
            
            gX_TraceLog_GUI(gX_LOG_TRACE,"anim->T %.3f, t %.3f, result.x %.3f",
                    anim->T,t,anim->result.x);
            
            gX_GUI_Element_Animation_Update_Cast(anim,element);
            
        }
        
        if (gX_F32_Equals(anim->T, 0.0f)) anim->T = -gX_GUI_ANIM_HACK_THRESHOLD;
        
        if (anim->T > anim->duration) {
            //opencode: snap to the completion boundary — a hitch frame can
            //   jump T from below 0 straight past duration, skipping every
            //   in-range write and stranding the target at the start state
            //   (title cascade buttons stayed invisible). One-shots only;
            //   oscillate/infinite keep their looping behavior.
            if (!anim->infinite && !anim->oscillate) {
                gX_F32 tSnap = anim->reverse ? 0.0f : 1.0f;
                tSnap = gX_Easing_Apply((gX_Easing)anim->easing, tSnap);
                anim->result = gX_F32v4_Lerp(anim->start, anim->end, tSnap);
                gX_GUI_Element_Animation_Update_Cast(anim, element);
            }
            //opencode: fire onComplete once when animation finishes
            if (anim->onComplete && anim->started) {
                anim->onComplete(anim->onCompleteUserData);
                anim->started = false;
            }
            if (anim->oscillate) {
                anim->Tset = false;
                anim->reverse = !anim->reverse;
            }
            if (anim->reverseWhenDone) {
                anim->reverse = !anim->reverse;
            }
            if (!(anim->infinite)) {
                anim->executing = false;
            }
            anim->execute.done = true;
            if (anim->reverseWhenNotExecute && anim->execute.done) {
                if (anim->reverse) anim->reverse = false;
                else anim->executeHint = true;
            }
            anim->Tset = false;
        }
        else {
            gX_I1 conditionCheck = false;
            if (anim->condPtr) conditionCheck = (anim->cond == *(anim->condPtr)); 
            
            //opencode: latched free-run — transition-triggered but frame-clock
            //   advanced, so durationWait staggers (previously this flag
            //   combination never advanced T: a dead mode).
            gX_I1 phaseDriven = anim->readTransitionGUI
                && (anim->readTransitionOut || anim->readTransitionIn);
            if (!phaseDriven) {
                //opencode: use actual frame delta instead of fixed 1/targetFPS
                anim->T += (gX_F32)gX_FrameTime_Get();
            } else {
                if (anim->readTransitionOut && gui.transition.out.execute) {
                    if ((anim->readCondition && conditionCheck) || !(anim->readCondition)) {
                        anim->T = gui.transition.out.Tactual;
                    }
                }
                if (anim->readTransitionIn && gui.transition.in.execute) {
                    anim->T = 1.0f-gui.transition.in.Tactual;
                    if (anim->T < 0.0f) anim->T = 0.0f;
                }
            }
        }
    }
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Animation,animation)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Animation,animation)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Animation,animation,ANIMATION)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Animation,animation)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Animation,animation)
//opencode: inlined from LOOP macro for navigability
gX_I0 gX_GUI_Element_Update_Animations(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Update_Animations(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->animations;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Animation *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Animation *elementmodule = &heapPtr[i];
            gX_GUI_Element_Animation_Update(elementmodule,element);
        }
    }
}
