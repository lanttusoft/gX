/**********************************************************************************************
*
*   [gX: src/common/marshal/json.c]
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
#include <stdlib.h>
#include <string.h>

#include "json.h"

#include "../enum.h"
#include "../log.h"

gX_JSONschemaContext jsoncontext;

gX_JSON_Object *gX_JSON_Schema_SearchObject(gX_JSON_Schema *schema, const gX_I8 *key) {
    gX_JSON_Object *obj = 0;
    for (gX_U32 i = 0; i < schema->objectsCount; i++) {
        gX_JSON_Object *o = &schema->objects[i];
        if (!strcmp(o->key,key)) {
            obj = o;
            break;
        }
    }
    return obj;
}

//opencode: arena is heap-allocated once per schema (never freed — program
//   lifetime, same as the old static embedding). The bounds guard in
//   GX_SCHEMAJSON_ADDOBJECT_TEMPLATE prevents past-the-end writes.
gX_I32 gX_JSON_Schema_EnsureArena(gX_JSON_Schema *schema, gX_U32 capacity) {
    if (schema->objects) return 0;
    schema->objects = (gX_JSON_Object *)malloc(sizeof(gX_JSON_Object) * capacity);
    if (!schema->objects) {
        gX_TraceLog(gX_LOG_ERROR, "JSON_SCHEMA: arena alloc failed (%u objects)", capacity);
        return 1;
    }
    memset(schema->objects, 0, sizeof(gX_JSON_Object) * capacity);
    return 0;
}

gX_JSON json;

gX_I32 gX_JSON_Init(gX_I0) {
    return 0;
}

gX_I32 gX_JSON_Deinit(gX_I0) {
    
    
    json_object_put(json.root);
    return 0;
}

gX_I32 gX_JSON_Load(const gX_I8 *path) {
    gX_I32 errors = 0;
    gX_TraceLog_COMMON(gX_LOG_DEBUG,"Loading from path %s",path);
    
    json.root = json_object_from_file(path);
    if (!json.root) {
        gX_TraceLog(gX_LOG_WARNING,"file loading failed");
        return 1;
    }
    
    //gX_JSON_Root_Print();
    
    for (gX_U32 i = 0; i < json.schema->loadedCount; i++) {
        //gX_TraceLog(gX_LOG_DEBUG,"i: %i",i);
        gX_JSON_Object *obj = &(json.schema->objects[i]);
        //gX_TraceLog(gX_LOG_DEBUG,"obj->key: %s",obj->key);
        
        if (obj->hasUpper) {
            obj->object = json_object_object_get(obj->upper->object, obj->key);
        }
        else {
            obj->object = json_object_object_get(json.root, obj->key); //asan
        }
        
        if (!obj->object) {
            obj->type = json_type_null;
        } else {
            obj->type = json_object_get_type(obj->object);
        }
        
        gX_TextLine objectstack;
        strcpy(objectstack,obj->key);
        
        switch (obj->type) {
            case json_type_null: {
                //gX_TraceLog(gX_LOG_INFO,"JSON: %s [null]: %s", objectstack, json_object_get_string(obj->object));
                break;
            }
            case json_type_array: {
                size_t arraylen = json_object_array_length(obj->object);
                gX_I8 arraytext[128];
                memset(arraytext,'\0',128);
                gX_I8 bfr[128];
                memset(bfr,'\0',128);
                for (size_t k = 0; k < arraylen; k++) {
                    struct json_object *ind = json_object_array_get_idx(obj->object, k);
                    gX_F64 value = json_object_get_double(ind);
                    sprintf(bfr,"%f ",value);
                    strcat(arraytext,bfr);
                }
                gX_TraceLog(gX_LOG_INFO,"JSON: %s [array(%i)]: %s <0x%lx>", objectstack, arraylen, arraytext, obj->dataPtr);
                //opencode: dispatch by datatype instead of arraylen (handle all vector types + Rect + Color)
                switch (obj->datatype) {
                    case gX_DATATYPE_F32V2: {
                        if (arraylen >= 2) {
                            gX_F32v2 *ptr = obj->dataPtr;
                            ptr->x = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 1));
                        }
                        break;
                    }
                    case gX_DATATYPE_F32V3: {
                        if (arraylen >= 3) {
                            gX_F32v3 *ptr = obj->dataPtr;
                            ptr->x = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 1));
                            ptr->z = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 2));
                        }
                        break;
                    }
                    case gX_DATATYPE_F32V4: {
                        if (arraylen >= 4) {
                            gX_F32v4 *ptr = obj->dataPtr;
                            ptr->x = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 1));
                            ptr->z = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 2));
                            ptr->w = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 3));
                        }
                        break;
                    }
                    case gX_DATATYPE_F64V2: {
                        if (arraylen >= 2) {
                            gX_F64v2 *ptr = obj->dataPtr;
                            ptr->x = json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = json_object_get_double(json_object_array_get_idx(obj->object, 1));
                        }
                        break;
                    }
                    case gX_DATATYPE_F64V3: {
                        if (arraylen >= 3) {
                            gX_F64v3 *ptr = obj->dataPtr;
                            ptr->x = json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = json_object_get_double(json_object_array_get_idx(obj->object, 1));
                            ptr->z = json_object_get_double(json_object_array_get_idx(obj->object, 2));
                        }
                        break;
                    }
                    case gX_DATATYPE_F64V4: {
                        if (arraylen >= 4) {
                            gX_F64v4 *ptr = obj->dataPtr;
                            ptr->x = json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            ptr->y = json_object_get_double(json_object_array_get_idx(obj->object, 1));
                            ptr->z = json_object_get_double(json_object_array_get_idx(obj->object, 2));
                            ptr->w = json_object_get_double(json_object_array_get_idx(obj->object, 3));
                        }
                        break;
                    }
                    case gX_DATATYPE_RECTANGLE: {
                        if (arraylen >= 4) {
                            gX_Rectangle *r = obj->dataPtr;
                            r->x = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 0));
                            r->y = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 1));
                            r->width = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 2));
                            r->height = (gX_F32)json_object_get_double(json_object_array_get_idx(obj->object, 3));
                        }
                        break;
                    }
                    case gX_DATATYPE_COLOR: {
                        if (arraylen >= 4) {
                            gX_Color *c = obj->dataPtr;
                            c->r = (gX_U8)json_object_get_int(json_object_array_get_idx(obj->object, 0));
                            c->g = (gX_U8)json_object_get_int(json_object_array_get_idx(obj->object, 1));
                            c->b = (gX_U8)json_object_get_int(json_object_array_get_idx(obj->object, 2));
                            c->a = (gX_U8)json_object_get_int(json_object_array_get_idx(obj->object, 3));
                        }
                        break;
                    }
                    default: break;
                }
                break;
            }
            case json_type_boolean: {
                gX_I1 value = json_object_get_boolean(obj->object);
                gX_TraceLog(gX_LOG_INFO,"JSON: %s [boolean]: %i <0x%lx>", objectstack, value, obj->dataPtr);
                //opencode: handle boolean -> I1
                if (obj->datatype == gX_DATATYPE_I1) {
                    gX_I1 *ptr = obj->dataPtr;
                    *ptr = value;
                }
                break;
            }
            case json_type_double: {
                gX_F64 value = json_object_get_double(obj->object);
                gX_TraceLog(gX_LOG_INFO,"JSON: %s [double]: %f <0x%lx>", objectstack, value, obj->dataPtr);
                //opencode: handle F32 and F64
                if (obj->datatype == gX_DATATYPE_F32) {
                    gX_F32 *ptr = obj->dataPtr;
                    *ptr = (gX_F32)value;
                } else if (obj->datatype == gX_DATATYPE_F64) {
                    gX_F64 *ptr = obj->dataPtr;
                    *ptr = value;
                }
                break;
            }
            case json_type_int: {
                gX_I64 value = json_object_get_int64(obj->object);
                gX_TraceLog(gX_LOG_INFO,"JSON: %s [int]: %li <0x%lx>", objectstack, value, obj->dataPtr);
                //opencode: handle all int datatypes
                switch (obj->datatype) {
                    case gX_DATATYPE_I1:  { gX_I1  *p = obj->dataPtr; *p = (gX_I1)value;  break; }
                    case gX_DATATYPE_I8:  { gX_I8  *p = obj->dataPtr; *p = (gX_I8)value;  break; }
                    case gX_DATATYPE_I16: { gX_I16 *p = obj->dataPtr; *p = (gX_I16)value; break; }
                    case gX_DATATYPE_I32: { gX_I32 *p = obj->dataPtr; *p = (gX_I32)value; break; }
                    case gX_DATATYPE_I64: { gX_I64 *p = obj->dataPtr; *p = (gX_I64)value; break; }
                    case gX_DATATYPE_U8:  { gX_U8  *p = obj->dataPtr; *p = (gX_U8)value;  break; }
                    case gX_DATATYPE_U16: { gX_U16 *p = obj->dataPtr; *p = (gX_U16)value; break; }
                    case gX_DATATYPE_U32: { gX_U32 *p = obj->dataPtr; *p = (gX_U32)value; break; }
                    case gX_DATATYPE_U64: { gX_U64 *p = obj->dataPtr; *p = (gX_U64)value; break; }
                    default: break;
                }
                break;
            }
            case json_type_object: {
                //gX_TraceLog(gX_LOG_INFO,"JSON: %s [object]: %s", objectstack, json_object_get_string(obj->object));
                break;
            }
            case json_type_string: {
                const gX_I8 *value = json_object_get_string(obj->object);
                gX_TraceLog(gX_LOG_INFO,"JSON: %s [string]: %s <0x%lx>", objectstack, value, obj->dataPtr);
                //opencode: handle string -> STRING (bounded copy — dataSize
                //   comes from GX_SCHEMAJSON_ADDOBJECT_STRING; fall back to
                //   255 for legacy STRING registrations without a size).
                if (obj->datatype == gX_DATATYPE_STRING) {
                    gX_U32 cap = (obj->dataSize > 0) ? obj->dataSize : 255;
                    strncpy((gX_I8*)obj->dataPtr, value, cap - 1);
                    ((gX_I8*)obj->dataPtr)[cap - 1] = '\0';
                }
                break;
            }
            default: break;
        }
    }
    
    json_object_put(json.root);
    
    return errors;
}

//opencode: use json_object_new_int for 32-bit-safe types
#define GX_JSON_SAVE_INT32(type) \
    case gX_DATATYPE_##type: { \
        gX_##type *val = (gX_##type*)obj->dataPtr; \
        gX_TraceLog(gX_LOG_DEBUG,#type": %i",(gX_I32)(*val)); \
        obj->object = json_object_new_int((gX_I32)(*val)); \
        break; \
    }

//opencode: use json_object_new_int64 for 64-bit types
#define GX_JSON_SAVE_INT64(type) \
    case gX_DATATYPE_##type: { \
        gX_##type *val = (gX_##type*)obj->dataPtr; \
        gX_TraceLog(gX_LOG_TRACE,#type": %li",(gX_I64)(*val)); \
        obj->object = json_object_new_int64((gX_I64)(*val)); \
        break; \
    }

gX_I32 gX_JSON_Save(const gX_I8 *path) {
    gX_I32 errors = 0;
    
    json.root = json_object_new_object();
    
    for (gX_U32 i = 0; i < json.schema->loadedCount; i++) {
        gX_TraceLog(gX_LOG_TRACE,"i: %i",i);
        gX_JSON_Object *obj = &(json.schema->objects[i]);
        gX_TraceLog(gX_LOG_TRACE,"obj->key: %s",obj->key);
        
        switch (obj->datatype) {
            case gX_DATATYPE_I1: {
                gX_I1 *val = (gX_I1*)obj->dataPtr;
                gX_TraceLog(gX_LOG_TRACE,"I1: %i",*val);
                obj->object = json_object_new_boolean((*val));
                break;
            }
            GX_JSON_SAVE_INT32(I8)
            GX_JSON_SAVE_INT32(I16)
            GX_JSON_SAVE_INT32(I32)
            GX_JSON_SAVE_INT64(I64)
            GX_JSON_SAVE_INT32(U8)
            GX_JSON_SAVE_INT32(U16)
            GX_JSON_SAVE_INT64(U32)
            GX_JSON_SAVE_INT64(U64)
            case gX_DATATYPE_F32: {
                gX_F32 *val = (gX_F32*)obj->dataPtr;
                gX_TraceLog(gX_LOG_TRACE,"F32: %.3f", (gX_F64)(*val));
                obj->object = json_object_new_double((gX_F64)(*val));
                break;
            }
            case gX_DATATYPE_F64: {
                gX_F64 *val = (gX_F64*)obj->dataPtr;
                gX_TraceLog(gX_LOG_TRACE,"F64: %.3f",*val);
                obj->object = json_object_new_double((gX_F64)(*val));
                break;
            }
            case gX_DATATYPE_STRING: {
                gX_I8 *val = (gX_I8*)obj->dataPtr;
                gX_TraceLog(gX_LOG_TRACE,"STRING: %s",val);
                obj->object = json_object_new_string(val);
                break;
            }
            //opencode: save 2/3/4-component float vectors as JSON arrays
            case gX_DATATYPE_F32V2: {
                gX_F32v2 *vec = (gX_F32v2*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                break;
            }
            case gX_DATATYPE_F32V3: {
                gX_F32v3 *vec = (gX_F32v3*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                json_object_array_put_idx(obj->object, 2, json_object_new_double(vec->z));
                break;
            }
            case gX_DATATYPE_F32V4: {
                gX_F32v4 *vec = (gX_F32v4*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                json_object_array_put_idx(obj->object, 2, json_object_new_double(vec->z));
                json_object_array_put_idx(obj->object, 3, json_object_new_double(vec->w));
                break;
            }
            case gX_DATATYPE_F64V2: {
                gX_F64v2 *vec = (gX_F64v2*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                break;
            }
            case gX_DATATYPE_F64V3: {
                gX_F64v3 *vec = (gX_F64v3*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                json_object_array_put_idx(obj->object, 2, json_object_new_double(vec->z));
                break;
            }
            case gX_DATATYPE_F64V4: {
                gX_F64v4 *vec = (gX_F64v4*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(vec->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(vec->y));
                json_object_array_put_idx(obj->object, 2, json_object_new_double(vec->z));
                json_object_array_put_idx(obj->object, 3, json_object_new_double(vec->w));
                break;
            }
            //opencode: save Rectangle as float array [x, y, width, height]
            case gX_DATATYPE_RECTANGLE: {
                gX_Rectangle *r = (gX_Rectangle*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_double(r->x));
                json_object_array_put_idx(obj->object, 1, json_object_new_double(r->y));
                json_object_array_put_idx(obj->object, 2, json_object_new_double(r->width));
                json_object_array_put_idx(obj->object, 3, json_object_new_double(r->height));
                break;
            }
            //opencode: save Color as int array [r, g, b, a]
            case gX_DATATYPE_COLOR: {
                gX_Color *c = (gX_Color*)obj->dataPtr;
                obj->object = json_object_new_array();
                json_object_array_put_idx(obj->object, 0, json_object_new_int(c->r));
                json_object_array_put_idx(obj->object, 1, json_object_new_int(c->g));
                json_object_array_put_idx(obj->object, 2, json_object_new_int(c->b));
                json_object_array_put_idx(obj->object, 3, json_object_new_int(c->a));
                break;
            }
            default: {
                obj->object = json_object_new_object();
                break;
            }
        }
        
        gX_I8 bfr[512];
        gX_String_JSON_Object(bfr,obj);
        gX_TraceLog(gX_LOG_TRACE,"obj->object: 0x%lx %s",(gX_U64)obj,bfr);
        if (obj->upper) {
            errors = json_object_object_add(obj->upper->object, obj->key, obj->object);
        }
        else {
            errors = json_object_object_add(json.root, obj->key, obj->object);
        }
        
    }
    
    gX_TraceLog_COMMON(gX_LOG_DEBUG,"json.root: %s", json_object_get_string(json.root));
    int flags = 0;
    flags |= JSON_C_TO_STRING_SPACED;
    flags |= JSON_C_TO_STRING_PRETTY;
    flags |= JSON_C_TO_STRING_PRETTY_TAB;
    errors = json_object_to_file_ext(path, json.root, flags);
    
    return errors;
}

gX_I32 gX_JSON_Root_Print(gX_I0) {
    gX_TraceLog_COMMON(gX_LOG_INFO,"root: %s",json_object_to_json_string(json.root));
    return 0;
}
