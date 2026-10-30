/**********************************************************************************************
*
*   [gX: src/gui/element/shape.c]
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
#include <math.h>

#include "element.h"
#include "modules.h"

#include "../enum.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../gxmath/gxmath.h"
#include "../../main.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)

inline gX_I0 gX_GUI_Element_Shape_Init(gX_GUI_Element_Shape *shape) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing shape values");
    
    //shape->value = (gX_F32v4){ 0 };
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Shape_Draw(gX_GUI_Element_Shape *shape, gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    gX_Color shapeColor = shape->color;
    if (element->stateRendering.SHAPE_PARENTCOLOR) {
        shapeColor = element->color;
    }
    shapeColor = gX_Color_Mul(shapeColor,canvas->canvasHue);
    
    //opencode: shader path — area shapes (CIRCLE/TRIANGLE/RECTANGLE) draw via
    //   the referenced shader element with direct geometry (the batch path
    //   can't use custom programs). LINE/BEZIER/POINT keep the batch path.
    gX_GUI_Element_Shader *elShader = NULL;
    if (shape->useShader && element->shaders.data && shape->shaderIndex < element->shaders.amount) {
        elShader = &((gX_GUI_Element_Shader*)element->shaders.data)[shape->shaderIndex];
        if (!elShader->shader) elShader = NULL;
    }
    gX_I1 shaderOk = (elShader != NULL) &&
        (shape->type == gX_GUI_ELEMENT_SHAPE_CIRCLE ||
         shape->type == gX_GUI_ELEMENT_SHAPE_TRIANGLE ||
         shape->type == gX_GUI_ELEMENT_SHAPE_RECTANGLE);
    
    if (shaderOk) {
        if (!elShader->loaded) {
            gX_GUI_Element_Shader_Load(elShader);
            gX_GUI_Element_Shader_Uniforms_Init(elShader);
            elShader->loaded = true;
        }
        
        //opencode: build geometry (pixel coords + uv 0..1) for the shape type
        gX_F32 verts[ (33 * 4) ] = { 0 };  // circle = center + 32 rim (max case)
        gX_U32 vcount = 0;
        gX_U32 prim = gX_RHI_TRIANGLE_FAN;
        gX_Rectangle box = element->rect;
        
        if (shape->type == gX_GUI_ELEMENT_SHAPE_CIRCLE) {
            gX_F32 cx = shape->v2[0].x, cy = shape->v2[0].y;
            gX_F32 r = shape->scalar[0];
            if (r <= 0.0f) return;
            gX_F32 minX = cx - r, minY = cy - r, w = 2.0f*r, h = 2.0f*r;
            #define SHAPE_VERT(px,py) { (gX_F32)(px), (gX_F32)(py), (w > 0.0f) ? (((gX_F32)(px)-minX)/w) : 0.0f, (h > 0.0f) ? (((gX_F32)(py)-minY)/h) : 0.0f }
            gX_F32 *v = verts;
            v[0]=cx; v[1]=cy; v[2]=0.5f; v[3]=0.5f; v += 4; vcount = 1;
            for (gX_U32 s = 0; s < 32; s++) {
                gX_F32 a = (gX_F32)s / 32.0f * 2.0f * gX_PI;
                gX_F32 px = cx + r * cosf(a);
                gX_F32 py = cy + r * sinf(a);
                v[0]=px; v[1]=py; v[2]=(px-minX)/w; v[3]=(py-minY)/h; v += 4;
                vcount++;
            }
            #undef SHAPE_VERT
        } else if (shape->type == gX_GUI_ELEMENT_SHAPE_TRIANGLE) {
            prim = gX_RHI_TRIANGLES;
            for (gX_U32 t = 0; t < 3; t++) {
                gX_F32v2 p = shape->v2[t];
                verts[t*4+0] = p.x; verts[t*4+1] = p.y;
                verts[t*4+2] = (t == 0) ? 0.0f : ((t == 1) ? 1.0f : 0.5f);
                verts[t*4+3] = (t == 0) ? 0.0f : ((t == 1) ? 0.0f : 1.0f);
            }
            vcount = 3;
        } else {  // RECTANGLE
            gX_F32v2 p0 = shape->v2[0];
            gX_F32v2 p1 = shape->v2[1];
            gX_F32 x0 = (p1.x > p0.x) ? p0.x : p1.x, x1 = (p1.x > p0.x) ? p1.x : p0.x;
            gX_F32 y0 = (p1.y > p0.y) ? p0.y : p1.y, y1 = (p1.y > p0.y) ? p1.y : p0.y;
            gX_F32 w = x1 - x0, h = y1 - y0;
            if (w <= 0.0f || h <= 0.0f) return;
            gX_F32 *v = verts;
            v[0]=x0; v[1]=y0; v[2]=0; v[3]=0; v+=4;
            v[0]=x0; v[1]=y1; v[2]=0; v[3]=1; v+=4;
            v[0]=x1; v[1]=y1; v[2]=1; v[3]=1; v+=4;
            v[0]=x1; v[1]=y0; v[2]=1; v[3]=0; v+=4;
            vcount = 4;
        }
        
        gX_F32 resW = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.width  : (gX_F32)maindata.renderWidth;
        gX_F32 resH = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.height : (gX_F32)maindata.renderHeight;
        gX_Shader *sh = elShader->shader;
        //opencode: 2D shape geometry winding is screen-space dependent — the
        //   fan/triangle winding can be back-facing in NDC (culled when the
        //   model element left cull enabled). GUI shapes always draw unculled.
        gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
        gX_RHI_Disable(gX_RHI_CULL_FACE);
        gX_Drawing_BeginShader(*sh);
            gX_GUI_Element_Shader_UploadUniforms(elShader, box,
                (gX_F32)(maindata.timeElapsed), (gX_F32v2){resW, resH},
                shapeColor, element->params.scale, canvas->camera.ambientFactor);
            gX_Drawing_DrawGeometry(verts, vcount, prim);
        gX_Drawing_EndShader();
        if (cullWas) gX_RHI_Enable(gX_RHI_CULL_FACE);
        return;
    }
    
    switch (shape->type) {
        case gX_GUI_ELEMENT_SHAPE_POINT: {
            gX_Point_Draw(shape->v2[0], shapeColor);
            break;
        }
        case gX_GUI_ELEMENT_SHAPE_LINE: {
            gX_Line line = { 0 };
            line.start = shape->v2[0];
            line.end = shape->v2[1];
            line.thickness = shape->scalar[0];
            gX_Line_Draw(line, shapeColor);
            break;
        }
        case gX_GUI_ELEMENT_SHAPE_BEZIER: {
            gX_Bezier bezier = { 0 };
            bezier.start = shape->v2[0];
            bezier.end = shape->v2[1];
            bezier.control1 = shape->v2[2];
            bezier.control2 = shape->v2[3];
            bezier.thickness = shape->scalar[0];
            gX_Bezier_Draw(bezier, shapeColor);
            break;
        }
        case gX_GUI_ELEMENT_SHAPE_CIRCLE: {
            gX_Circle circle = { 0 };
            circle.center = shape->v2[0];
            circle.radius = shape->scalar[0];
            gX_Circle_Draw(circle, shapeColor);
            break;
        }
        case gX_GUI_ELEMENT_SHAPE_TRIANGLE: {
            gX_Triangle tri = { 0 };
            tri.v1 = shape->v2[0];
            tri.v2 = shape->v2[1];
            tri.v3 = shape->v2[2];
            tri.thickness = shape->scalar[0];
            gX_Triangle_Draw(tri, shapeColor);
            break;
        }
        //opencode: RECTANGLE shape case added — was silently not drawn
        case gX_GUI_ELEMENT_SHAPE_RECTANGLE: {
            gX_F32v2 p0 = shape->v2[0];
            gX_F32v2 p1 = shape->v2[1];
            gX_F32 x0 = (p1.x > p0.x) ? p0.x : p1.x, x1 = (p1.x > p0.x) ? p1.x : p0.x;
            gX_F32 y0 = (p1.y > p0.y) ? p0.y : p1.y, y1 = (p1.y > p0.y) ? p1.y : p0.y;
            gX_Rectangle_Draw((gX_Rectangle){x0, y0, x1 - x0, y1 - y0}, shapeColor);
            break;
        }
        default: break;
    }
    
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Shape,shape)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Shape,shape)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Shape,shape,SHAPE)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Shape,shape)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Shape,shape)
//gX_GUI_ELEMENT_MODULE_FUNCDEF_LOOP(  Shape,shape,Draw)

gX_I0 gX_GUI_Element_Draw_Shapes(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw_Shapes(gX_GUI_Element *element, gX_GUI_Canvas *canvas) { \
     \
    gX_Heap heapStruct = element->shapes; \
     \
    if (heapStruct.amount > 0) { \
        gX_GUI_Element_Shape *heapPtr = heapStruct.data; \
         \
        for (gX_U32 i = 0; i < heapStruct.amount; i++) { \
            gX_GUI_Element_Shape *elementmodule = &heapPtr[i]; \
            gX_GUI_Element_Shape_Draw(elementmodule,element,canvas); \
        } \
    } \
}
