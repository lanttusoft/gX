/**********************************************************************************************
*
*   [gX: src/common/enum.c]
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

#include "enum.h"
#include "types.h"

#define gX_ENUMNAME_CASE(name,stri) \
    case name: return stri; break;

const gX_I8 *gX_Datatypes_String(gX_I32 datatype) {
#define X(dt) gX_ENUMNAME_CASE(gX_DATATYPE_##dt,#dt);
    switch (datatype) {
        gX_DATATYPES
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_Scenes_String(gX_I32 scene) {
#define X(scene) gX_ENUMNAME_CASE(gX_SCENE_##scene,#scene);
    switch (scene) {
        gX_SCENES
        default: return "undefined";
    }
#undef X    
}

const gX_I8 *gX_Keys_String(gX_I32 key) {
#define X(key,value) gX_ENUMNAME_CASE(gX_KEY_##key,#key);
    switch (key) {
        gX_KEYS
        default: return "undefined";
    }
#undef X    
}

const gX_I8 *gX_MaterialMapIndex_String(gX_I32 mapindex) {
#define X(map) gX_ENUMNAME_CASE(gX_MATERIAL_MAP_##map,#map);
    switch (mapindex) {
        gX_MATERIALMAPINDEX
        default: return "undefined";
    }
#undef X      
}

const gX_I8 *gX_PixelFormat_String(gX_I32 format) {
#define X(format) gX_ENUMNAME_CASE(gX_PIXELFORMAT_##format,#format);
    switch (format) {
        gX_PIXELFORMATS
        default: return "undefined";
    }
#undef X      
}

const gX_I8 *gX_BlendMode_String(gX_I32 blend) {
#define X(blend) gX_ENUMNAME_CASE(gX_BLEND_##blend,#blend);
    switch (blend) {
        gX_BLENDMODES
        default: return "undefined";
    }
#undef X      
}

const gX_I8 *gX_ShaderLocationIndex_String(gX_I32 locindex) {
#define X(loc) gX_ENUMNAME_CASE(gX_SHADER_LOC_##loc,#loc);
    switch (locindex) {
        gX_SHADERLOCATIONINDEX
        default: return "undefined";
    }
#undef X      
}

const gX_I8 *gX_TextureFilter_String(gX_I32 filter) {
#define X(fil) gX_ENUMNAME_CASE(gX_TEXTURE_FILTER_##fil,#fil);
    switch (filter) {
        gX_TEXTUREFILTER
        default: return "undefined";
    }
#undef X      
}

const gX_I8 *gX_TextureWrap_String(gX_I32 wrap) {
#define X(wra) gX_ENUMNAME_CASE(gX_TEXTURE_WRAP_##wra,#wra);
    switch (wrap) {
        gX_TEXTUREWRAP
        default: return "undefined";
    }
#undef X   
}

const gX_I8 *gX_MouseButton_String(gX_I32 button) {
#define X(mousebutton) gX_ENUMNAME_CASE(gX_MOUSE_BUTTON_##mousebutton,#mousebutton);
    switch (button) {
        gX_MOUSEBUTTONS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_MouseCursor_String(gX_I32 cursor) {
#define X(cursor) gX_ENUMNAME_CASE(gX_MOUSE_CURSOR_##cursor,#cursor);
    switch (cursor) {
        gX_MOUSECURSOR
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GamepadButton_String(gX_I32 button) {
#define X(button) gX_ENUMNAME_CASE(gX_GAMEPAD_BUTTON_##button,#button);
    switch (button) {
        gX_GAMEPADBUTTON
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GamepadAxis_String(gX_I32 axis) {
#define X(axis) gX_ENUMNAME_CASE(gX_GAMEPAD_AXIS_##axis,#axis);
    switch (axis) {
        gX_GAMEPADAXIS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_FontType_String(gX_I32 type) {
#define X(type) gX_ENUMNAME_CASE(gX_FONT_##type,#type);
    switch (type) {
        gX_FONTTYPE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_CameraMode_String(gX_I32 mode) {
#define X(mode) gX_ENUMNAME_CASE(gX_CAMERA_##mode,#mode);
    switch (mode) {
        gX_CAMERAMODE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_CameraProjection_String(gX_I32 projection) {
#define X(proj) gX_ENUMNAME_CASE(gX_CAMERA_##proj,#proj);
    switch (projection) {
        gX_CAMERAPROJECTION
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_glCullMode_String(gX_I32 mode) {
#define X(mode) gX_ENUMNAME_CASE(gX_GL_CULL_FACE_##mode,#mode);
    switch (mode) {
        gX_GLCULLMODE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_ConfigFlags_String(gX_I32 flag) {
#define X(flag,value) gX_ENUMNAME_CASE(gX_FLAG_##flag,#flag);
    switch (flag) {
        gX_CONFIGFLAGS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_CubemapLayout_String(gX_I32 layout) {
#define X(lay) gX_ENUMNAME_CASE(gX_CUBEMAP_LAYOUT_##lay,#lay);
    switch (layout) {
        gX_CUBEMAP_LAYOUTS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_ShaderUniformDataType_String(gX_I32 type) {
#define X(type) gX_ENUMNAME_CASE(gX_SHADER_UNIFORM_##type,#type);
    switch (type) {
        gX_SHADERUNIFORMDATATYPE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_ShaderAttributeDataType_String(gX_I32 type) {
#define X(type) gX_ENUMNAME_CASE(gX_SHADER_ATTRIB_##type,#type);
    switch (type) {
        gX_SHADERATTRIBUTEDATATYPE
        default: return "undefined";
    }
#undef X
}
