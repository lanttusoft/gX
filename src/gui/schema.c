/**********************************************************************************************
*
*   [gX: src/gui/schema.c]
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

//opencode: expanded all element schema functions with missing fields
#include <string.h>
#include <stdio.h>

#include "schema.h"
#include "../common/enum.h"
#include "../common/log.h"

gX_JSON_Schema schemaJSON_GUI;

//opencode: schema version load target — pre-set to the current version so
//   files written before versioning load as v1 (no guiVersion key = default)
gX_U32 gX_GUI_SchemaVersionLoaded = gX_GUI_SCHEMA_VERSION;

gX_I0 gX_GUI_Schema_CheckVersion(gX_I0) {
    if (gX_GUI_SchemaVersionLoaded != gX_GUI_SCHEMA_VERSION) {
        gX_TraceLog(gX_LOG_WARNING, "GUI: schema version mismatch — file v%u, current v%u (fields may mis-read)",
            gX_GUI_SchemaVersionLoaded, (gX_U32)gX_GUI_SCHEMA_VERSION);
    }
}

#define GX_SCHEMAJSON_ADDOBJECT(name) \
    GX_SCHEMAJSON_ADDOBJECT_TEMPLATE(schemaJSON_GUI,name) \
    
//opencode: added done, pressed*, dragged*, scrollWheel
static gX_I0 gX_GUI_Element_ExecuteState_SchemaJSON_Create(gX_GUI_Element_ExecuteState *execute) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(done,(execute->done),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(once,(execute->once),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(hovered,(execute->hovered),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(toggled,(execute->toggled),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(clickedLeft,(execute->clickedLeft),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(clickedRight,(execute->clickedRight),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(clickedMiddle,(execute->clickedMiddle),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(pressedLeft,(execute->pressedLeft),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(pressedRight,(execute->pressedRight),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(pressedMiddle,(execute->pressedMiddle),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(draggedLeft,(execute->draggedLeft),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(draggedRight,(execute->draggedRight),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(draggedMiddle,(execute->draggedMiddle),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(scrollWheel,(execute->scrollWheel),I1)
    
}
    
//opencode: added readTransitionCanvas/Out/In/Condition, cond, doAny, datatype, do*, model*, text*, texture*, easing, durationWait
static gX_I0 gX_GUI_Element_Animation_SchemaJSON_Create(gX_GUI_Element_Animation *animation) {
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(execute)
        gX_GUI_Element_ExecuteState_SchemaJSON_Create(&animation->execute);
    GX_SCHEMAJSON_POPCONTEXT
    GX_SCHEMAJSON_ADDOBJECT_TERM(executeAnyway,(animation->executeAnyway),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(readTransitionGUI,(animation->readTransitionGUI),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readTransitionCanvas,(animation->readTransitionCanvas),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readTransitionOut,(animation->readTransitionOut),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readTransitionIn,(animation->readTransitionIn),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readCondition,(animation->readCondition),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(cond,(animation->cond),I32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(oscillate,(animation->oscillate),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(reverse,(animation->reverse),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(reverseWhenDone,(animation->reverseWhenDone),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(reverseWhenNotExecute,(animation->reverseWhenNotExecute),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(infinite,(animation->infinite),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(offset,(animation->offset),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(modeAdd,(animation->modeAdd),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(doAny,(animation->doAny),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(datatype,(animation->datatype),I32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(doRect,(animation->doRect),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doColor,(animation->doColor),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(doScale,(animation->doScale),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doAngle,(animation->doAngle),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doMatrixRow0,(animation->doMatrixRow0),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doMatrixRow1,(animation->doMatrixRow1),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doMatrixRow2,(animation->doMatrixRow2),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doMatrixRow3,(animation->doMatrixRow3),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(modelIndex,(animation->modelIndex),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doModelParamExtraColorMultiplier,(animation->doModelParamExtraColorMultiplier),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doModelParamRotate,(animation->doModelParamRotate),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doModelParamTranslate,(animation->doModelParamTranslate),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doModelParamScale,(animation->doModelParamScale),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(textIndex,(animation->textIndex),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextFontSize,(animation->doTextFontSize),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextOffset,(animation->doTextOffset),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextSpacing,(animation->doTextSpacing),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextColor,(animation->doTextColor),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(textureIndex,(animation->textureIndex),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextureParamRect,(animation->doTextureParamRect),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextureParamScale,(animation->doTextureParamScale),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextureParamOrigin,(animation->doTextureParamOrigin),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextureParamRotation,(animation->doTextureParamRotation),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTextureParamTint,(animation->doTextureParamTint),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(easing,(animation->easing),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(durationWait,(animation->durationWait),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(duration,(animation->duration),F32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(start,(animation->start),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(end,(animation->end),F32V4)
    
}

static gX_I0 gX_GUI_Element_Attribute_SchemaJSON_Create(gX_GUI_Element_Attribute *attribute) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(value,(attribute->value),F32V4)
    
}

//opencode: added modeMultiply, rainbow, smoothRandomColor, execute, factor, color, colorOriginal
static gX_I0 gX_GUI_Element_Color_SchemaJSON_Create(gX_GUI_Element_Color *color) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(rectColor,(color->rectColor),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(textColor,(color->textColor),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(shapeColor,(color->shapeColor),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(gradientColor,(color->gradientColor),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(textColorIndex,(color->textColorIndex),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(shapeColorIndex,(color->shapeColorIndex),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(modeMultiply,(color->modeMultiply),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(rainbow,(color->rainbow),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(smoothRandomColor,(color->smoothRandomColor),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(execute)
        gX_GUI_Element_ExecuteState_SchemaJSON_Create(&color->execute);
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(factor,(color->factor),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(color,(color->color),COLOR)
    GX_SCHEMAJSON_ADDOBJECT_TERM(colorOriginal,(color->colorOriginal),COLOR)
    
}

//opencode: added reg, textArraySize, textAppend
static gX_I0 gX_GUI_Element_Data_Agent_SchemaJSON_Create(gX_GUI_Element_Data_Agent *agent) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(read,(agent->read),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(write,(agent->write),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(writePotentio,(agent->writePotentio),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readPotentio,(agent->readPotentio),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(reg,(agent->reg),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(datatype,(agent->datatype),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_STRING(remark,(agent->remark))
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeIndex,(agent->attributeIndex),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeReadBeforeOp,(agent->attributeReadBeforeOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeWriteBeforeOp,(agent->attributeWriteBeforeOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeReadAfterOp,(agent->attributeReadAfterOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeWriteAfterOp,(agent->attributeWriteAfterOp),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(textIndex,(agent->textIndex),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(textUse,(agent->textUse),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(textArraySize,(agent->textArraySize),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(textAppend,(agent->textAppend),I1)
    
}

//opencode: added limit, copyCursor, readBefore/After, writeBefore/After, reg, use*, attributeIndex, shapeIndex, min, max
static gX_I0 gX_GUI_Element_Data_Potentio_SchemaJSON_Create(gX_GUI_Element_Data_Potentio *potentio) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(smooth,(potentio->smooth),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(radial,(potentio->radial),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(enableX,(potentio->enableX),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(enableY,(potentio->enableY),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(limit,(potentio->limit),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(copyCursor,(potentio->copyCursor),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readBefore,(potentio->readBefore),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(readAfter,(potentio->readAfter),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(writeBefore,(potentio->writeBefore),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(writeAfter,(potentio->writeAfter),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(reg,(potentio->reg),F32V2)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(useMain,(potentio->useMain),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(useAttribute,(potentio->useAttribute),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(useShape,(potentio->useShape),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(attributeIndex,(potentio->attributeIndex),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(shapeIndex,(potentio->shapeIndex),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(min,(potentio->min),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(max,(potentio->max),F32V2)
    
}

//opencode: added useDataOperator, op, executeAnyway, executeOneTime, potentio, copy*/swap* ops
static gX_I0 gX_GUI_Element_Data_SchemaJSON_Create(gX_GUI_Element_Data *data) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(isFunctionPtr,(data->isFunctionPtr),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(src)
        gX_GUI_Element_Data_Agent_SchemaJSON_Create(&data->src);
    GX_SCHEMAJSON_POPCONTEXT
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(dst)
        gX_GUI_Element_Data_Agent_SchemaJSON_Create(&data->dst);
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_STRING(format,(data->format))
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(init,(data->init),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(useDataOperator,(data->useDataOperator),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(op,(data->op),I32)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(execute)
        gX_GUI_Element_ExecuteState_SchemaJSON_Create(&data->execute);
    GX_SCHEMAJSON_POPCONTEXT
    GX_SCHEMAJSON_ADDOBJECT_TERM(executeAnyway,(data->executeAnyway),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(executeOneTime,(data->executeOneTime),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(usePotentio,(data->usePotentio),I1)
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(potentio)
        gX_GUI_Element_Data_Potentio_SchemaJSON_Create(&data->potentio);
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(copySrcToDstBeforeOp,(data->copySrcToDstBeforeOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(copyDstToSrcBeforeOp,(data->copyDstToSrcBeforeOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(swapSrcAndDstBeforeOp,(data->swapSrcAndDstBeforeOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(copySrcToDstAfterOp,(data->copySrcToDstAfterOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(copyDstToSrcAfterOp,(data->copyDstToSrcAfterOp),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(swapSrcAndDstAfterOp,(data->swapSrcAndDstAfterOp),I1)
}

//opencode: added extraparams (doGlass, doBackside, colorMultiplier, refIndex)
static gX_I0 gX_GUI_Element_Model_SchemaJSON_Create(gX_GUI_Element_Model *model) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(useShader,(model->useShader),I1)
    //opencode: shaderIndex (into element->shaders) — the raw shader ptr is
    //   runtime-only and no longer exists on the module
    GX_SCHEMAJSON_ADDOBJECT_TERM(shaderIndex,(model->shaderIndex),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(useParameters,(model->useParameters),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(extraparams)
        GX_SCHEMAJSON_ADDOBJECT_TERM(doGlass,(model->extraparams.doGlass),I1)
        GX_SCHEMAJSON_ADDOBJECT_TERM(doBackside,(model->extraparams.doBackside),I1)
        GX_SCHEMAJSON_ADDOBJECT_TERM(colorMultiplier,(model->extraparams.colorMultiplier),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(refIndex,(model->extraparams.refIndex),F32)
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(paramRotate,(model->paramRotate),F32V3)
    GX_SCHEMAJSON_ADDOBJECT_TERM(paramTranslate,(model->paramTranslate),F32V3)
    GX_SCHEMAJSON_ADDOBJECT_TERM(paramScale,(model->paramScale),F32V3)
    
}

//opencode: added value
static gX_I0 gX_GUI_Element_Shader_Uniform_SchemaJSON_Create(gX_GUI_Element_Shader_Uniform *unif) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(value,(unif->value),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(initial,(unif->initial),F32V4)
    GX_SCHEMAJSON_ADDOBJECT_TERM(delta,(unif->delta),F32V4)
    
}

//opencode: added uniforms[1-3]
static gX_I0 gX_GUI_Element_Shader_SchemaJSON_Create(gX_GUI_Element_Shader *shader) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(loaded,(shader->loaded),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(loops,(shader->loops),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(drawShader,(shader->drawShader),I1)
    
    //opencode: fragPath — the pool-migration key; shaders otherwise lose
    //   their source reference on load
    GX_SCHEMAJSON_ADDOBJECT_STRING(fragPath,(shader->fragPath))
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(uniforms)
        GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(unif[0])
            gX_GUI_Element_Shader_Uniform_SchemaJSON_Create(&shader->uniforms[0]);
        GX_SCHEMAJSON_POPCONTEXT
        GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(unif[1])
            gX_GUI_Element_Shader_Uniform_SchemaJSON_Create(&shader->uniforms[1]);
        GX_SCHEMAJSON_POPCONTEXT
        GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(unif[2])
            gX_GUI_Element_Shader_Uniform_SchemaJSON_Create(&shader->uniforms[2]);
        GX_SCHEMAJSON_POPCONTEXT
        GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(unif[3])
            gX_GUI_Element_Shader_Uniform_SchemaJSON_Create(&shader->uniforms[3]);
        GX_SCHEMAJSON_POPCONTEXT
    GX_SCHEMAJSON_POPCONTEXT
    
}

//opencode: added state
static gX_I0 gX_GUI_Element_Shape_SchemaJSON_Create(gX_GUI_Element_Shape *shape) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(state,(shape->state),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(type,(shape->type),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(integer[0],(shape->integer[0]),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(integer[1],(shape->integer[1]),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(scalar[0],(shape->scalar[0]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(scalar[1],(shape->scalar[1]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(scalar[2],(shape->scalar[2]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(scalar[3],(shape->scalar[3]),F32)
    //opencode: fix v2 field mapping (was scalar — never serialized v2 data)
    GX_SCHEMAJSON_ADDOBJECT_TERM(v2[0],(shape->v2[0]),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(v2[1],(shape->v2[1]),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(v2[2],(shape->v2[2]),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(v2[3],(shape->v2[3]),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(color,(shape->color),COLOR)
    //opencode: shader reference (index into element->shaders)
    GX_SCHEMAJSON_ADDOBJECT_TERM(useShader,(shape->useShader),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(shaderIndex,(shape->shaderIndex),U32)
    
}

//opencode: box-model layout container module
static gX_I0 gX_GUI_Element_Layout_SchemaJSON_Create(gX_GUI_Element_Layout *layout) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(state,(layout->state),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(display,(layout->display),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(justify,(layout->justify),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(align,(layout->align),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(gap,(layout->gap),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(wrap,(layout->wrap),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(padding[0],(layout->padding[0]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(padding[1],(layout->padding[1]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(padding[2],(layout->padding[2]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(padding[3],(layout->padding[3]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(childCount,(layout->childCount),U32)
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(children)
        for (gX_U32 i = 0; i < layout->childCount; i++) {
            gX_TextLine bfr;
            snprintf(bfr,sizeof(bfr),"child[%i]",i);
            GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT_STRING(bfr)
                GX_SCHEMAJSON_ADDOBJECT_TERM(id,(layout->children[i]),I32)
            GX_SCHEMAJSON_POPCONTEXT
        }
    GX_SCHEMAJSON_POPCONTEXT
    
}

//opencode: added state + params (volume/pitch/pan) — sound vanished on load
static gX_I0 gX_GUI_Element_Sound_SchemaJSON_Create(gX_GUI_Element_Sound *sound) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(state,(sound->state),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(execute)
        gX_GUI_Element_ExecuteState_SchemaJSON_Create(&sound->execute);
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(params)
        GX_SCHEMAJSON_ADDOBJECT_TERM(volume,(sound->params.volume),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(pitch,(sound->params.pitch),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(pan,(sound->params.pan),F32)
    GX_SCHEMAJSON_POPCONTEXT
    
}

//opencode: added state, customFontIndex, lineSpacing
static gX_I0 gX_GUI_Element_Text_SchemaJSON_Create(gX_GUI_Element_Text *text) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(draw,(text->draw),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(state,(text->state),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_STRING(text,(text->text))
    GX_SCHEMAJSON_ADDOBJECT_TERM(length,(text->length),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(fontSize,(text->fontSize),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(offset,(text->offset),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(spacing,(text->spacing),I32)
    //opencode: lineSpacing — multi-line height multiplier used by title2
    GX_SCHEMAJSON_ADDOBJECT_TERM(lineSpacing,(text->lineSpacing),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(wrap,(text->wrap),I1) //opencode: v0.6.0 word-wrap
    GX_SCHEMAJSON_ADDOBJECT_TERM(sdf,(text->sdf),I1)   //opencode: v0.6.0 SDF text
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(color,(text->color),COLOR)
    GX_SCHEMAJSON_ADDOBJECT_TERM(colorOriginal,(text->colorOriginal),COLOR)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(hasCustomFont,(text->hasCustomFont),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(customFontIndex,(text->customFontIndex),U32)
    
}

//opencode: added state + params (src/dst/rotation/origin/offset/scale/tint)
static gX_I0 gX_GUI_Element_Texture_SchemaJSON_Create(gX_GUI_Element_Texture *texture) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(state,(texture->state),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(useShader,(texture->useShader),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(shaderIndex,(texture->shaderIndex),U32)
    
    //opencode: texture draw params — Widget_TextureButton sets params.dst;
    //   without this the texture silently disappeared on load
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(params)
        GX_SCHEMAJSON_ADDOBJECT_TERM(src,(texture->params.src),RECTANGLE)
        GX_SCHEMAJSON_ADDOBJECT_TERM(dst,(texture->params.dst),RECTANGLE)
        GX_SCHEMAJSON_ADDOBJECT_TERM(rotation,(texture->params.rotation),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(origin,(texture->params.origin),F32V2)
        GX_SCHEMAJSON_ADDOBJECT_TERM(offset,(texture->params.offset),F32V2)
        GX_SCHEMAJSON_ADDOBJECT_TERM(scale,(texture->params.scale),F32V2)
        GX_SCHEMAJSON_ADDOBJECT_TERM(tint,(texture->params.tint),COLOR)
    GX_SCHEMAJSON_POPCONTEXT
    
}

//opencode: added angle
static gX_I0 gX_GUI_Element_Parameters_SchemaJSON_Create(gX_GUI_Element_Parameters *params) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(scale,(params->scale),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(angle,(params->angle),F32)
    
}

static gX_I0 gX_GUI_Element_State_SchemaJSON_Create(gX_GUI_Element_State *state) {
    //opencode: U32 not U64 — gX_GUI_Element_State is exactly 4 bytes; the
    //   previous U64 term read 8 bytes (state + adjacent stateRendering).
    GX_SCHEMAJSON_ADDOBJECT_TERM(STATE,(state),U32)
    
}

static gX_I0 gX_GUI_Element_Rendering_SchemaJSON_Create(gX_GUI_Element_Rendering *rendering) {
    //opencode: U32 not U64 — gX_GUI_Element_Rendering is exactly 4 bytes.
    GX_SCHEMAJSON_ADDOBJECT_TERM(RENDERING,(rendering),U32)
    
}

//opencode: timer module — onFire/onFireUserData are runtime callbacks, not persisted
static gX_I0 gX_GUI_Element_Timer_SchemaJSON_Create(gX_GUI_Element_Timer *timer) {
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(mode,(timer->mode),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(duration,(timer->duration),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(elapsed,(timer->elapsed),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(paused,(timer->paused),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(done,(timer->done),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(execute)
        gX_GUI_Element_ExecuteState_SchemaJSON_Create(&timer->execute);
    GX_SCHEMAJSON_POPCONTEXT
    
}

#define SCHEMAJSON_GEN_IND(name,ind) \
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(name[ind]) \

//opencode: snprintf for buffer overflow safety
#define SCHEMAJSON_INDEX_ELEMENT(ind) \
    gX_TextLine bfr; \
    snprintf(bfr,sizeof(bfr),"element[%i]",ind); \
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT_STRING(bfr) \
        gX_GUI_Element *element = &elements[ind]; \
        gX_GUI_Element_SchemaJSON_Create(element); \
    GX_SCHEMAJSON_POPCONTEXT \

#define SCHEMAJSON_INDEX_MODULE(ind,namelc,nameuc) \
    gX_TextLine bfr; \
    snprintf(bfr,sizeof(bfr),#namelc"[%i]",ind); \
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT_STRING(bfr) \
        gX_GUI_Element_##nameuc *namelc = &namelc##s[ind]; \
        gX_GUI_Element_##nameuc##_SchemaJSON_Create(namelc); \
    GX_SCHEMAJSON_POPCONTEXT \

#define SCHEMAJSON_GUIELEMENT_MODULE(namelc,nameuc) \
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(namelc##s) \
        GX_SCHEMAJSON_ADDOBJECT_TERM(amount,(element->namelc##s.amount),U32) \
        gX_GUI_Element_##nameuc *namelc##s = element->namelc##s.data; \
        for (gX_U32 i = 0; i < element->namelc##s.amount; i++) { \
            SCHEMAJSON_INDEX_MODULE(i,namelc,nameuc) \
        } \
    GX_SCHEMAJSON_POPCONTEXT \
    
//opencode: added name, description, stateFunction (commented - bitfield)
static gX_I0 gX_GUI_Element_SchemaJSON_Create(gX_GUI_Element *element) {
    
    GX_SCHEMAJSON_ADDOBJECT_STRING(name,(element->name))
    GX_SCHEMAJSON_ADDOBJECT_STRING(description,(element->description))
    //opencode: gameCSS — class list + inline style (name doubles as #id)
    GX_SCHEMAJSON_ADDOBJECT_STRING(cssClass,(element->cssClass))
    GX_SCHEMAJSON_ADDOBJECT_STRING(cssStyle,(element->cssStyle))
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(id,(element->id),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(rect,(element->rect),RECTANGLE)
    GX_SCHEMAJSON_ADDOBJECT_TERM(color,(element->color),COLOR)
    //opencode: box-model margins (grid units, used by layout containers)
    GX_SCHEMAJSON_ADDOBJECT_TERM(margin[0],(element->margin[0]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(margin[1],(element->margin[1]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(margin[2],(element->margin[2]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(margin[3],(element->margin[3]),F32)
    //opencode: flex sizing — grow weight plus min/max clamps (grid units)
    GX_SCHEMAJSON_ADDOBJECT_TERM(flexGrow,(element->flexGrow),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(flexMin[0],(element->flexMin[0]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(flexMin[1],(element->flexMin[1]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(flexMax[0],(element->flexMax[0]),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(flexMax[1],(element->flexMax[1]),F32)
    
    //opencode: widget classification fields — without these, a loaded canvas
    //   loses its widget identity (type/layer/tag/visible). userData is a
    //   runtime pointer and is deliberately NOT serialized.
    GX_SCHEMAJSON_ADDOBJECT_TERM(type,(element->type),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(layer,(element->layer),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(tag,(element->tag),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(visible,(element->visible),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(params)
        gX_GUI_Element_Parameters_SchemaJSON_Create(&element->params);
    GX_SCHEMAJSON_POPCONTEXT
    
    //stateFunction removed — operator is now on each data module (data->op)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(state)
        gX_GUI_Element_State_SchemaJSON_Create(&element->state);
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(stateRendering)
        gX_GUI_Element_Rendering_SchemaJSON_Create(&element->stateRendering);
    GX_SCHEMAJSON_POPCONTEXT
    
    SCHEMAJSON_GUIELEMENT_MODULE(animation,Animation)
    SCHEMAJSON_GUIELEMENT_MODULE(attribute,Attribute)
    SCHEMAJSON_GUIELEMENT_MODULE(color,Color)
    //opencode: funcPtr/funcPtrArg (gX_GUI_OnClick bindings) are runtime
    //   callback pointers and are NOT persisted — on load the element keeps
    //   its freshly-built bindings. Use isFunctionPtr=false data modules for
    //   data-driven behavior that must survive save/load.
    SCHEMAJSON_GUIELEMENT_MODULE(data,Data)
    SCHEMAJSON_GUIELEMENT_MODULE(layout,Layout)
    SCHEMAJSON_GUIELEMENT_MODULE(model,Model)
    SCHEMAJSON_GUIELEMENT_MODULE(shader,Shader)
    SCHEMAJSON_GUIELEMENT_MODULE(shape,Shape)
    SCHEMAJSON_GUIELEMENT_MODULE(sound,Sound)
    SCHEMAJSON_GUIELEMENT_MODULE(text,Text)
    SCHEMAJSON_GUIELEMENT_MODULE(texture,Texture)
    SCHEMAJSON_GUIELEMENT_MODULE(timer,Timer)
    
}

gX_I0 gX_GUI_SchemaJSON_Create(gX_I32 id) {
    
    //opencode: gX — heap arena allocated on first marshal (was 2.5 MB static bss;
    //   gX never marshals canvas JSON at runtime, so the arena stays unmallocated)
    gX_JSON_Schema_EnsureArena(&schemaJSON_GUI, gX_JSON_SCHEMA_MAXOBJECTS);

    strcpy(schemaJSON_GUI.name,"test name");
    strcpy(schemaJSON_GUI.description,"test description");
    strcpy(schemaJSON_GUI.author,"lanttu");
    strcpy(schemaJSON_GUI.comments,"json works, good");
    
    schemaJSON_GUI.loadedCount = 0;
    schemaJSON_GUI.objectsCount = gX_JSON_SCHEMA_MAXOBJECTS;
    GX_SCHEMAJSON_INIT
    
    gX_GUI_Canvas *canvases = gui.canvases.data;
    gX_GUI_Canvas *canvas = &canvases[id];
    
    //opencode: schema version — bump when the element/module layout changes;
    //   loaders can reject or migrate old files instead of mis-reading them.
    //   gX_GUI_SchemaVersionLoaded is reset to current at Create (so repeated
    //   saves without loads don't accumulate stale versions).
    gX_GUI_SchemaVersionLoaded = gX_GUI_SCHEMA_VERSION;
    GX_SCHEMAJSON_ADDOBJECT_TERM(guiVersion,(gX_GUI_SchemaVersionLoaded),U32)
    
    //making gui template schema
    GX_SCHEMAJSON_ADDOBJECT_STRING(name,(canvas->name))
    GX_SCHEMAJSON_ADDOBJECT_STRING(description,(canvas->description))
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(canvasID,(canvas->canvasID),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(templateID,(canvas->templateID),I32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(canvasBox,(canvas->canvasBox),RECTANGLE)
    GX_SCHEMAJSON_ADDOBJECT_TERM(canvasHue,(canvas->canvasHue),COLOR)
    GX_SCHEMAJSON_ADDOBJECT_TERM(canvasOffset,(canvas->canvasOffset),F32V2)
    GX_SCHEMAJSON_ADDOBJECT_TERM(canvasScale,(canvas->canvasScale),F32V2)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(refresh,(canvas->refresh),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(doTransition,(canvas->doTransition),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(state)
        GX_SCHEMAJSON_ADDOBJECT_TERM(doNotLoadBuffer,(canvas->state.doNotLoadBuffer),I1)
        GX_SCHEMAJSON_ADDOBJECT_TERM(cursorX,(canvas->state.cursorX),I32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(cursorY,(canvas->state.cursorY),I32)
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(camera)
        GX_SCHEMAJSON_ADDOBJECT_TERM(camera2D,(canvas->camera.camera2D),STRUCT_CAMERA2D)
        GX_SCHEMAJSON_ADDOBJECT_TERM(camera3D,(canvas->camera.camera3D),STRUCT_CAMERA3D)
        GX_SCHEMAJSON_ADDOBJECT_TERM(ambientFactor,(canvas->camera.ambientFactor),F32)
        GX_SCHEMAJSON_ADDOBJECT_TERM(lightDir,(canvas->camera.lightDir),F32V3)
    GX_SCHEMAJSON_POPCONTEXT
    
    GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(elements)
        //opencode: serialize live element slots only. Heap removal leaves the
        //   storage extent intact so amount includes holes; persisting those
        //   zeroed/inert slots produced ghost elements on a later load.
        gX_U32 elementCount = 0;
        for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
            if (canvas->elements.meta && canvas->elements.meta[i].USED) elementCount++;
        }
        GX_SCHEMAJSON_ADDOBJECT_TERM(amount,(elementCount),U32)
        gX_GUI_Element *elements = canvas->elements.data;
        gX_U32 savedIndex = 0;
        for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
            if (!canvas->elements.meta || !canvas->elements.meta[i].USED) continue;
            gX_TextLine bfr;
            snprintf(bfr,sizeof(bfr),"element[%i]",savedIndex);
            GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT_STRING(bfr)
                gX_GUI_Element *element = &elements[i];
                gX_GUI_Element_SchemaJSON_Create(element);
            GX_SCHEMAJSON_POPCONTEXT
            savedIndex++;
        }
    GX_SCHEMAJSON_POPCONTEXT
    
    schemaJSON_GUI.loadedCount = (gX_U32)(jsoncontext.currentObjectIndex)+1;
}

gX_I0 gX_GUI_SchemaJSON_Delete(gX_I0) {
    return;
}
