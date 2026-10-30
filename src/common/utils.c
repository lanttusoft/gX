/**********************************************************************************************
*
*   [gX: src/common/utils.c]
*
*   Copyright (c) 2025 lanttu
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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "utils.h"
#include "random.h"

#include "enum.h"
#include "types.h"
#include "log.h"
#include "heap.h"

#include "../wrappers/wrappers.h"
#include "../main.h"

#define GETSIZE(dt,dt2) \
    case gX_DATATYPE_##dt: { return sizeof(gX_##dt2); break; } \
        
#define GETSIZE_VECTORIZE(dt) \
    GETSIZE(dt,dt) \
    GETSIZE(dt##V2,dt##v2) \
    GETSIZE(dt##V3,dt##v3) \
    GETSIZE(dt##V4,dt##v4) \

size_t gX_Struct_GetSize(gX_I32 id) {
    switch (id) {
        case gX_DATATYPE_I0: { return 1; break; }
        case gX_DATATYPE_I1: { return 1; break; }
        
        GETSIZE(I8,I8)
        GETSIZE(I16,I16)
        GETSIZE(I32,I32)
        GETSIZE(I64,I64)
        GETSIZE(U8,U8)
        GETSIZE(U16,U16)
        GETSIZE(U32,U32)
        GETSIZE(U64,U64)
        GETSIZE_VECTORIZE(F32)
        GETSIZE_VECTORIZE(F64)
        GETSIZE(STRUCT_FONT,Font)
        GETSIZE(STRUCT_HEAP,Heap)
        GETSIZE(STRUCT_IMAGE,Image)
        GETSIZE(STRUCT_MESH,Mesh)
        GETSIZE(STRUCT_MODEL,Model)
        GETSIZE(STRUCT_MUSIC,Music)
        GETSIZE(STRUCT_RENDERTEXTURE,RenderTexture)
        GETSIZE(STRUCT_SHADER,Shader)
        GETSIZE(STRUCT_SOUND,Sound)
        GETSIZE(STRUCT_TEXTURE,Texture)
        
        default: { return 1; break; }
    }
}

gX_I0 gX_Rectangle_MultiGradient(gX_Rectangle rect, gX_ColorGradientArray array) {
    
    gX_Rectangle rectPart = rect;
    rectPart.height /= (gX_F32)array.amount;
    
    gX_Rectangle_Parameters params = {0};
    params.gradientVertical = true;
    
    for (gX_U32 i = 0; i < array.amount; i++) {
        params.colors[0] = array.gradients[i].start;
        params.colors[1] = array.gradients[i].end;
        gX_Rectangle_Draw_Parameters(rectPart, params);
        rectPart.y += rectPart.height;
    }
    
}

gX_I0 gX_RawString_Draw_ShadowSoft(const gX_I8 *string, gX_Font font,
    gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize,
    gX_F32 spacing, gX_Color tint, gX_F32v2 shadowOffset,
    gX_Color shadowColor, gX_F32 softness)
{
    //opencode: SDF fonts blur through the distance field (one draw) instead
    //   of the 5-tap ring: spread grows the silhouette, blur softens the edge.
    if (font.shader.id != 0) {
        gX_RawString_SDF_SetSoft(softness * 0.06f, 2.2f, 1.0f);
        gX_RawString_Draw(string, font,
            (gX_F32v2){position.x + shadowOffset.x, position.y + shadowOffset.y},
            origin, rotation, fontsize, spacing, shadowColor);
        gX_RawString_SDF_SetSoft(0.0f, 1.0f, 1.0f);
        gX_RawString_Draw(string, font, position, origin, rotation, fontsize,
            spacing, tint);
        return;
    }
    gX_Color tap = shadowColor;
    tap.a = (gX_U8)((gX_U32)shadowColor.a / 5u);
    const gX_F32v2 ring[4] = {{-1.0f, 0.0f}, {1.0f, 0.0f},
                              {0.0f, -1.0f}, {0.0f, 1.0f}};
    gX_F32v2 base = {position.x + shadowOffset.x, position.y + shadowOffset.y};
    gX_RawString_Draw(string, font, base, origin, rotation, fontsize, spacing,
        tap);
    for (gX_I32 i = 0; i < 4; i++) {
        gX_F32v2 offset = {
            base.x + ring[i].x * softness, base.y + ring[i].y * softness};
        gX_RawString_Draw(string, font, offset, origin, rotation, fontsize,
            spacing, tap);
    }
    gX_RawString_Draw(string, font, position, origin, rotation, fontsize,
        spacing, tint);
}

gX_I0 gX_RawString_Draw_Outline(const gX_I8 *string, gX_Font font,
    gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize,
    gX_F32 spacing, gX_F32 outlineWidth, gX_Color outlineColor)
{
    static const gX_F32v2 directions[8] = {
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f},
        {1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    for (gX_I32 i = 0; i < 8; i++) {
        gX_F32v2 offset = {
            position.x + directions[i].x * outlineWidth,
            position.y + directions[i].y * outlineWidth};
        gX_RawString_Draw(string, font, offset, origin, rotation, fontsize,
            spacing, outlineColor);
    }
}

gX_I0 gX_RawString_Draw_Shadow(const gX_I8 *string, gX_Font font, gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize, gX_F32 spacing, gX_Color tint, gX_F32v2 shadowOffset, gX_Color shadowColor) {
    gX_F32v2 shadowPosition = position;
    shadowPosition.x += shadowOffset.x;
    shadowPosition.y += shadowOffset.y;
    gX_RawString_Draw(string,font,shadowPosition,origin,rotation,fontsize,spacing,shadowColor);
    gX_RawString_Draw(string,font,position,origin,rotation,fontsize,spacing,tint);
}

gX_I0 gX_Text_Draw_Shadow(gX_Text text, gX_F32v2 shadowOffset, gX_Color shadowColor) {
    gX_Text textShadow = text;
    textShadow.offset = shadowOffset;
    textShadow.color = shadowColor;
    gX_Text_Draw(textShadow);
    gX_Text_Draw(text);
}

gX_I0 gX_Model_AttachShader(gX_Model *model, gX_Shader shader) {
    for (int i = 0; i < model->materialCount; i++)
        model->materials[i].shader = shader;
}

gX_Texture gX_Texture_Load_Parameters(const gX_I8 *path, gX_I32 filter, gX_I32 wrap) {
    gX_Texture tex = (gX_Texture){0};
    tex = gX_Texture_Load(path);
    gX_Texture_SetFilter(tex,filter);
    gX_Texture_SetWrap(tex,wrap);
    return tex;
}

gX_RenderTexture gX_RenderTexture_Load_Parameters(gX_U32 width, gX_U32 height, gX_I32 filter, gX_I32 wrap) {
    gX_RenderTexture rt = (gX_RenderTexture){0};
    rt = gX_RenderTexture_Load(width,height);
    gX_Texture_SetFilter(rt.texture,filter);
    gX_Texture_SetWrap(rt.texture,wrap);
    return rt;
}

gX_I0 gX_PathGen(gX_I8 *target, gX_I8 *filename, gX_U32 filetype) {
    
    const gX_I8 *resourcesFolder = "resources";
    strcpy(target,resourcesFolder);
    
    switch (filetype) {
        case gX_DATATYPE_STRUCT_TEXTURE: {
            strcat(target,"/texture/");
            break;
        }
        default: break;
    }
    
    strcat(target,filename);
    
}

gX_Color gX_Color_Random(gX_I0) {
    
    gX_Color result = (gX_Color){0,0,0,0};
    
    //opencode: four raw bytes from the deterministic PCG stream
    result.r = (gX_U8)gX_Random32_Next(&rng.rng32);
    result.g = (gX_U8)gX_Random32_Next(&rng.rng32);
    result.b = (gX_U8)gX_Random32_Next(&rng.rng32);
    result.a = (gX_U8)gX_Random32_Next(&rng.rng32);
    
    return result;
}

gX_I32 gX_LoadingState_Handler(gX_LoadingState *state, gX_I0 (*loadf)(), gX_I0 (*unloadf)(), gX_I0 *wra) {
    gX_I1 loadasapstate = (maindata.backendLoaded && state->loadAsap);
    gX_I1 loadbeforestate = false;
    gX_I1 unloadafterstate = false;
    if ((state->load || loadasapstate || loadbeforestate) && !(state->loaded)) {
        loadf(wra);
        return 1;
    }
    if ((state->unload || unloadafterstate) && state->loaded) {
        unloadf(wra);
        return 2;
    }
    if (state->reload && state->loaded) {
        unloadf(wra);
        loadf(wra);
        return 3;
    }
    return 0;
}

gX_I8 *gX_Text_Split_GetLast(const gX_I8 *string, gX_I8 delimiter) {
    gX_I32 count = 0;
    
    gX_I8 **strings = gX_RawString_Split(string, delimiter, &count);
    gX_I8 *last = strings[count-1];
    
    return last;
}

gX_I0 gX_Text_Screenshot(gX_I8 *text) {
    time_t t = time(NULL);
    struct tm *timedata = localtime(&t);
    int year = timedata->tm_year;
    int month = timedata->tm_mon;
    int day = timedata->tm_mday;
    int hour = timedata->tm_hour;
    int min = timedata->tm_min;
    int sec = timedata->tm_sec;
    
    const gX_I8 *timeFormatStr = "%02i%02i%02i_%02i%02i%02i";
    const gX_I8 *timeFormat = gX_Text_Format(timeFormatStr,(1900+year) % 100,month+1,day,hour,min,sec);
    
    //opencode: capture harnesses can isolate generated screenshots without
    //   changing the default user-facing screenshot directory.
    const gX_I8 *screenshotDir = getenv("GX_SCREENSHOT_DIR");
    if (!screenshotDir || !screenshotDir[0]) screenshotDir = "screenshot";
    const gX_I8 *separator = screenshotDir[strlen((const char *)screenshotDir) - 1] == '/'
        ? "" : "/";
    snprintf(text, sizeof(gX_TextLine), "%s%sgXss_%s.png",
             screenshotDir, separator, timeFormat);
    
}

gX_I32 gX_String_JSON_Object(gX_I8 *bfr, gX_JSON_Object *obj) {
    strcpy(bfr,"[JSON_Object] \n");
    
    strcat(bfr,gX_Text_Format("loaded: %i \n",obj->loaded));
    strcat(bfr,gX_Text_Format("hasUpper: %i \n",obj->hasUpper));
    strcat(bfr,gX_Text_Format("datatype: %i [%s] \n",obj->datatype,gX_Datatypes_String(obj->datatype)));
    strcat(bfr,gX_Text_Format("key: %s \n",obj->key));
    strcat(bfr,gX_Text_Format("object: 0x%lx \n",(gX_U64)obj->object));
    strcat(bfr,gX_Text_Format("upper: 0x%lx \n",(gX_U64)obj->upper));
    strcat(bfr,gX_Text_Format("dataPtr: 0x%lx \n",(gX_U64)obj->dataPtr));
    strcat(bfr,gX_Text_Format("dataRemark: %s \n",obj->dataRemark));
    
    return 0;
}
