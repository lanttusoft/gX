/**********************************************************************************************
* 
*   [wrappers/convert.c]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: backend type conversion implementations (gX ↔ raylib)
#include "convert.h"

#include <stdlib.h>
//opencode: hunt - implicit-decl sweep (calloc-use in raylib.h)
#include "../common/log.h"


gX_F32v2 gX_F32v2_ConvertFromBackend(Vector2 v2) {
    gX_F32v2 result = (gX_F32v2){0};
    result.x = v2.x;
    result.y = v2.y;
    return result;
}

gX_F32v3 gX_F32v3_ConvertFromBackend(Vector3 v3) {
    gX_F32v3 result = (gX_F32v3){0};
    result.x = v3.x;
    result.y = v3.y;
    result.z = v3.z;
    return result;
}

gX_F32v4 gX_F32v4_ConvertFromBackend(Vector4 v4) {
    gX_F32v4 result = (gX_F32v4){0};
    result.x = v4.x;
    result.y = v4.y;
    result.z = v4.z;
    result.w = v4.w;
    return result;
}

gX_Matrix gX_Matrix_ConvertFromBackend(Matrix mat) {
    gX_Matrix result = (gX_Matrix){0};
    result.m0 = mat.m0; result.m1 = mat.m1; result.m2 = mat.m2; result.m3 = mat.m3;
    result.m4 = mat.m4; result.m5 = mat.m5; result.m6 = mat.m6; result.m7 = mat.m7;
    result.m8 = mat.m8; result.m9 = mat.m9; result.m10 = mat.m10; result.m11 = mat.m11;
    result.m12 = mat.m12; result.m13 = mat.m13; result.m14 = mat.m14; result.m15 = mat.m15;
    return result;
}

gX_Color gX_Color_ConvertFromBackend(Color color) {
    gX_Color result = (gX_Color){0};
    result.r = color.r;
    result.g = color.g;
    result.b = color.b;
    result.a = color.a;
    return result;
}

gX_Rectangle gX_Rectangle_ConvertFromBackend(Rectangle rectangle) {
    gX_Rectangle result = (gX_Rectangle){0};
    result.x = rectangle.x;
    result.y = rectangle.y;
    result.width = rectangle.width;
    result.height = rectangle.height;
    return result;
}

gX_Texture gX_Texture_ConvertFromBackend(Texture texture) {
    gX_Texture result = (gX_Texture){0};
    result.id = texture.id;
    result.width = texture.width;
    result.height = texture.height;
    result.mipmaps = texture.mipmaps;
    result.format = texture.format;
    return result;
}

gX_RenderTexture gX_RenderTexture_ConvertFromBackend(RenderTexture rt) {
    gX_RenderTexture result = (gX_RenderTexture){0};
    result.id = rt.id;
    result.texture = gX_Texture_ConvertFromBackend(rt.texture);
    result.depth = gX_Texture_ConvertFromBackend(rt.depth);
    return result; 
}

gX_Shader gX_Shader_ConvertFromBackend(Shader shader) {
    gX_Shader result = (gX_Shader){0};
    result.id = shader.id;
    //opencode: binary cache path leaves locs=NULL — allocate to prevent rlSetShader crash
    if (shader.locs) {
        result.locs = shader.locs;
    } else {
        result.locs = (int*)RL_CALLOC((gX_U64)RL_MAX_SHADER_LOCATIONS, sizeof(int));
        for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) result.locs[i] = -1;
    }
    return result; 
}

gX_MaterialMap gX_MaterialMap_ConvertFromBackend(MaterialMap materialmap) {
    gX_MaterialMap result = (gX_MaterialMap){0};
    result.texture = gX_Texture_ConvertFromBackend(materialmap.texture);
    result.color = gX_Color_ConvertFromBackend(materialmap.color);
    result.value = materialmap.value;
    return result; 
}

gX_Material gX_Material_ConvertFromBackend(Material material) {
    gX_Material result = (gX_Material){0};
    result.shader = gX_Shader_ConvertFromBackend(material.shader);
    result.maps[0] =   gX_MaterialMap_ConvertFromBackend(material.maps[0]);
    result.maps[1] =   gX_MaterialMap_ConvertFromBackend(material.maps[1]);
    result.maps[2] =   gX_MaterialMap_ConvertFromBackend(material.maps[2]);
    result.maps[3] =   gX_MaterialMap_ConvertFromBackend(material.maps[3]);
    //result.maps =      (gX_MaterialMap*)material.maps;
    result.params[0] = material.params[0];
    result.params[1] = material.params[1];
    result.params[2] = material.params[2];
    result.params[3] = material.params[3];
    return result; 
}

gX_Mesh gX_Mesh_ConvertFromBackend(Mesh mesh) {
    gX_Mesh result = (gX_Mesh){ 0 };
    result.vertexCount      = (gX_I32)(mesh.vertexCount);
    result.triangleCount    = (gX_I32)(mesh.triangleCount);
    result.vertices         = (gX_F32*)(mesh.vertices);
    result.texcoords        = (gX_F32*)(mesh.texcoords);
    result.texcoords2       = (gX_F32*)(mesh.texcoords2);
    result.normals          = (gX_F32*)(mesh.normals);
    result.tangents         = (gX_F32*)(mesh.tangents);
    result.colors           = (gX_U8*)(mesh.colors);
    result.indices          = (gX_U16*)(mesh.indices);
    result.boneCount        = (gX_I32)(mesh.boneCount);
    result.boneIndices      = (gX_U8*)(mesh.boneIndices);
    result.boneWeights      = (gX_F32*)(mesh.boneWeights);
    result.animVertices     = (gX_F32*)(mesh.animVertices);
    result.animNormals      = (gX_F32*)(mesh.animNormals);
    result.vaoId            = (gX_U32)(mesh.vaoId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: mesh.vboId: 0x%lx",(gX_U64)mesh.vboId);
    result.vboId            = (gX_U32*)(mesh.vboId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: result.vboId: 0x%lx",(gX_U64)result.vboId);
    return result; 
}

gX_Mesh gX_MeshPtr_ConvertFromBackend(Mesh *mesh) {
    gX_Mesh result = (gX_Mesh){ 0 };
    result.vertexCount      = (gX_I32)(mesh->vertexCount);
    result.triangleCount    = (gX_I32)(mesh->triangleCount);
    result.vertices         = (gX_F32*)(mesh->vertices);
    result.texcoords        = (gX_F32*)(mesh->texcoords);
    result.texcoords2       = (gX_F32*)(mesh->texcoords2);
    result.normals          = (gX_F32*)(mesh->normals);
    result.tangents         = (gX_F32*)(mesh->tangents);
    result.colors           = (gX_U8*)(mesh->colors);
    result.indices          = (gX_U16*)(mesh->indices);
    result.boneCount        = (gX_I32)(mesh->boneCount);
    result.boneIndices      = (gX_U8*)(mesh->boneIndices);
    result.boneWeights      = (gX_F32*)(mesh->boneWeights);
    result.animVertices     = (gX_F32*)(mesh->animVertices);
    result.animNormals      = (gX_F32*)(mesh->animNormals);
    result.vaoId            = (gX_U32)(mesh->vaoId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: mesh->vboId: 0x%lx",(gX_U64)mesh->vboId);
    result.vboId            = (gX_U32*)(mesh->vboId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: result.vboId: 0x%lx",(gX_U64)result.vboId);
    return result;
}

gX_ModelSkeleton gX_ModelSkeleton_ConvertFromBackend(ModelSkeleton skeleton) {
    gX_ModelSkeleton result = (gX_ModelSkeleton){0};
    result.boneCount        = skeleton.boneCount;
    result.bones            = (gX_BoneInfo*)skeleton.bones;
    result.bindPose         = (gX_Transform*)skeleton.bindPose;
    return result;
}

gX_Model gX_Model_ConvertFromBackend(Model model) {
    gX_Model result = (gX_Model){0};
    result.transform =      gX_Matrix_ConvertFromBackend(model.transform);
    result.meshCount =      (gX_I32)(model.meshCount);
    result.materialCount =  (gX_I32)(model.materialCount);
    result.meshes =         (gX_Mesh*)(model.meshes);
    /*for (gX_I32 i = 0; i < result.meshCount; i++) {
        result.meshes[i] =  gX_Mesh_ConvertFromBackend(model.meshes[i]);
    }*/
    result.materials =      (gX_Material*)(model.materials);
    result.meshMaterial =   (gX_I32*)(model.meshMaterial);
    result.skeleton =       gX_ModelSkeleton_ConvertFromBackend(model.skeleton);
    result.currentPose =    (gX_Transform*)(model.currentPose);
    result.boneMatrices =   (gX_Matrix*)(model.boneMatrices);
    return result;
}

gX_Font gX_Font_ConvertFromBackend(Font font) {
    gX_Font result = (gX_Font){0};
    result.baseSize = font.baseSize;
    result.glyphCount = font.glyphCount;
    result.glyphPadding = font.glyphPadding;
    result.texture = gX_Texture_ConvertFromBackend(font.texture);
    result.recs = (gX_Rectangle*)font.recs;
    result.glyphs = (gX_GlyphInfo*)font.glyphs;
    return result;
}

gX_Wave gX_Wave_ConvertFromBackend(Wave wave) {
    gX_Wave result = (gX_Wave){0};
    result.frameCount = wave.frameCount;
    result.sampleRate = wave.sampleRate;
    result.sampleSize = wave.sampleSize;
    result.channels = wave.channels;
    result.data = wave.data;
    return result;
}

gX_AudioStream gX_AudioStream_ConvertFromBackend(AudioStream stream) {
    gX_AudioStream result = (gX_AudioStream){0};
    result.buffer = stream.buffer;
    result.processor = stream.processor;
    result.sampleRate = stream.sampleRate;
    result.sampleSize = stream.sampleSize;
    result.channels = stream.channels;
    return result;
}

gX_Sound gX_Sound_ConvertFromBackend(Sound sound) {
    gX_Sound result = (gX_Sound){0};
    result.stream = gX_AudioStream_ConvertFromBackend(sound.stream);
    result.frameCount = sound.frameCount;
    return result;
}

gX_Music gX_Music_ConvertFromBackend(Music music) {
    gX_Music result = (gX_Music){0};
    result.stream = gX_AudioStream_ConvertFromBackend(music.stream);
    result.frameCount = music.frameCount;
    result.looping = music.looping;
    result.ctxType = music.ctxType;
    result.ctxData = music.ctxData;
    return result;
}

gX_Camera3D gX_Camera3D_ConvertFromBackend(Camera3D camera) {
    gX_Camera3D result = (gX_Camera3D){0};
    result.position = gX_F32v3_ConvertFromBackend(camera.position);
    result.target = gX_F32v3_ConvertFromBackend(camera.target);
    result.up = gX_F32v3_ConvertFromBackend(camera.up);
    result.fovy = camera.fovy;
    result.projection = camera.projection;
    return result;
}

gX_Ray gX_Ray_ConvertFromBackend(Ray ray) {
    gX_Ray result = (gX_Ray){0};
    result.position = gX_F32v3_ConvertFromBackend(ray.position);
    result.direction = gX_F32v3_ConvertFromBackend(ray.direction);
    return result;
}

gX_RayCollision gX_RayCollision_ConvertFromBackend(RayCollision collision) {
    gX_RayCollision result = (gX_RayCollision){0};
    result.hit = collision.hit;
    result.distance = collision.distance;
    result.point = gX_F32v3_ConvertFromBackend(collision.point);
    result.normal = gX_F32v3_ConvertFromBackend(collision.normal);
    return result;
}

//opencode
gX_Image gX_Image_ConvertFromBackend(Image image) {
    gX_Image result = (gX_Image){0};
    result.data = image.data;
    result.width = image.width;
    result.height = image.height;
    result.mipmaps = image.mipmaps;
    result.format = image.format;
    return result;
}

Vector2 gX_F32v2_ConvertToBackend(gX_F32v2 v2) {
    Vector2 result = (Vector2){0};
    result.x = v2.x;
    result.y = v2.y;
    return result;
}

Vector3 gX_F32v3_ConvertToBackend(gX_F32v3 v3) {
    Vector3 result = (Vector3){0};
    result.x = v3.x;
    result.y = v3.y;
    result.z = v3.z;
    return result;
}

Vector4 gX_F32v4_ConvertToBackend(gX_F32v4 v4) {
    Vector4 result = (Vector4){0};
    result.x = v4.x;
    result.y = v4.y;
    result.z = v4.z;
    result.w = v4.w;
    return result;
}

Matrix gX_Matrix_ConvertToBackend(gX_Matrix mat) {
    Matrix result = (Matrix){0};
    result.m0 = mat.m0; result.m1 = mat.m1; result.m2 = mat.m2; result.m3 = mat.m3;
    result.m4 = mat.m4; result.m5 = mat.m5; result.m6 = mat.m6; result.m7 = mat.m7;
    result.m8 = mat.m8; result.m9 = mat.m9; result.m10 = mat.m10; result.m11 = mat.m11;
    result.m12 = mat.m12; result.m13 = mat.m13; result.m14 = mat.m14; result.m15 = mat.m15;
    return result;
}

Color gX_Color_ConvertToBackend(gX_Color color) {
    Color result = (Color){0};
    result.r = color.r;
    result.g = color.g;
    result.b = color.b;
    result.a = color.a;
    return result;
}

Rectangle gX_Rectangle_ConvertToBackend(gX_Rectangle rectangle) {
    Rectangle result = (Rectangle){0};
    result.x = rectangle.x;
    result.y = rectangle.y;
    result.width = rectangle.width;
    result.height = rectangle.height;
    return result;
}

Texture gX_Texture_ConvertToBackend(gX_Texture texture) {
    Texture result = (Texture){0};
    result.id = texture.id;
    result.width = texture.width;
    result.height = texture.height;
    result.mipmaps = texture.mipmaps;
    result.format = texture.format;
    return result;
}

RenderTexture gX_RenderTexture_ConvertToBackend(gX_RenderTexture rt) {
    RenderTexture result = (RenderTexture){0};
    result.id = rt.id;
    result.texture = gX_Texture_ConvertToBackend(rt.texture);
    result.depth = gX_Texture_ConvertToBackend(rt.depth);
    return result; 
}

Shader gX_Shader_ConvertToBackend(gX_Shader shader) {
    Shader result = (Shader){ 0 };
    result.id = (unsigned int)shader.id;
    result.locs = (int*)shader.locs;
    return result;
}

MaterialMap gX_MaterialMap_ConvertToBackend(gX_MaterialMap materialmap) {
    MaterialMap result = (MaterialMap){0};
    result.texture = gX_Texture_ConvertToBackend(materialmap.texture);
    result.color = gX_Color_ConvertToBackend(materialmap.color);
    result.value = materialmap.value;
    return result;
}

Material gX_Material_ConvertToBackend(gX_Material material) {
    Material result = (Material){ 0 };
    //result.shader =    gX_Shader_ConvertToBackend(material.shader);
    result.shader.id = material.shader.id;
    result.shader.locs = material.shader.locs;
    result.maps[0] =   gX_MaterialMap_ConvertToBackend(material.maps[0]);
    result.maps[1] =   gX_MaterialMap_ConvertToBackend(material.maps[1]);
    result.maps[2] =   gX_MaterialMap_ConvertToBackend(material.maps[2]);
    result.maps[3] =   gX_MaterialMap_ConvertToBackend(material.maps[3]);
    //result.maps =      (MaterialMap*)material.maps;
    result.params[0] = (float)material.params[0];
    result.params[1] = (float)material.params[1];
    result.params[2] = (float)material.params[2];
    result.params[3] = (float)material.params[3];
    return result; 
}

Mesh gX_Mesh_ConvertToBackend(gX_Mesh mesh) {
    Mesh result = (Mesh){ 0 };
    result.vertexCount      = (int)(mesh.vertexCount);
    result.triangleCount    = (int)(mesh.triangleCount);
    result.vertices         = (float*)(mesh.vertices);
    result.texcoords        = (float*)(mesh.texcoords);
    result.texcoords2       = (float*)(mesh.texcoords2);
    result.normals          = (float*)(mesh.normals);
    result.tangents         = (float*)(mesh.tangents);
    result.colors           = (unsigned char*)(mesh.colors);
    result.indices          = (unsigned short*)(mesh.indices);
    result.boneCount        = (int)(mesh.boneCount);
    result.boneIndices      = (unsigned char*)(mesh.boneIndices);
    result.boneWeights      = (float*)(mesh.boneWeights);
    result.animVertices     = (float*)(mesh.animVertices);
    result.animNormals      = (float*)(mesh.animNormals);
    result.vaoId            = (unsigned int)(mesh.vaoId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: mesh.vboId: 0x%lx",(gX_U64)mesh.vboId);
    result.vboId            = (unsigned int*)(mesh.vboId);
    //gX_TraceLog(gX_LOG_INFO,"WRAPPERS_CONVERT: result.vboId: 0x%lx",(gX_U64)result.vboId);
    return result; 
}

ModelSkeleton gX_ModelSkeleton_ConvertToBackend(gX_ModelSkeleton skeleton) {
    ModelSkeleton result = (ModelSkeleton){0};
    result.boneCount        = skeleton.boneCount;
    result.bones            = (BoneInfo*)skeleton.bones;
    result.bindPose         = (Transform*)skeleton.bindPose;
    return result;
}

Model gX_Model_ConvertToBackend(gX_Model model) {
    Model result = (Model){0};
    result.transform =      gX_Matrix_ConvertToBackend(model.transform);
    result.meshCount =      (int)(model.meshCount);
    result.materialCount =  (int)(model.materialCount);
    result.meshes =         (Mesh*)(model.meshes);
    /*for (gX_I32 i = 0; i < result.meshCount; i++) {
        result.meshes[i] =  gX_Mesh_ConvertToBackend(model.meshes[i]);
    }*/
    result.materials =      (Material*)(model.materials);
    result.meshMaterial =   (int*)(model.meshMaterial);
    result.skeleton =       gX_ModelSkeleton_ConvertToBackend(model.skeleton);
    result.currentPose =    (Transform*)(model.currentPose);
    result.boneMatrices =   (Matrix*)(model.boneMatrices);
    return result;
}

Font gX_Font_ConvertToBackend(gX_Font font) {
    Font result = (Font){0};
    result.baseSize = font.baseSize;
    result.glyphCount = font.glyphCount;
    result.glyphPadding = font.glyphPadding;
    result.texture = gX_Texture_ConvertToBackend(font.texture);
    result.recs = (Rectangle*)font.recs;
    result.glyphs = (GlyphInfo*)font.glyphs;
    return result;
}

Wave gX_Wave_ConvertToBackend(gX_Wave wave) {
    Wave result = (Wave){0};
    result.frameCount = wave.frameCount;
    result.sampleRate = wave.sampleRate;
    result.sampleSize = wave.sampleSize;
    result.channels = wave.channels;
    result.data = wave.data;
    return result;
}

AudioStream gX_AudioStream_ConvertToBackend(gX_AudioStream stream) {
    AudioStream result = (AudioStream){0};
    result.buffer = stream.buffer;
    result.processor = stream.processor;
    result.sampleRate = stream.sampleRate;
    result.sampleSize = stream.sampleSize;
    result.channels = stream.channels;
    return result;
}

Sound gX_Sound_ConvertToBackend(gX_Sound sound) {
    Sound result = (Sound){0};
    result.stream = gX_AudioStream_ConvertToBackend(sound.stream);
    result.frameCount = sound.frameCount;
    return result;
}

Music gX_Music_ConvertToBackend(gX_Music music) {
    Music result = (Music){0};
    result.stream = gX_AudioStream_ConvertToBackend(music.stream);
    result.frameCount = music.frameCount;
    result.looping = music.looping;
    result.ctxType = music.ctxType;
    result.ctxData = music.ctxData;
    return result;
}

Camera3D gX_Camera3D_ConvertToBackend(gX_Camera3D camera) {
    Camera3D result = (Camera3D){0};
    result.position = gX_F32v3_ConvertToBackend(camera.position);
    result.target = gX_F32v3_ConvertToBackend(camera.target);
    result.up = gX_F32v3_ConvertToBackend(camera.up);
    result.fovy = camera.fovy;
    result.projection = camera.projection;
    return result;
}

Ray gX_Ray_ConvertToBackend(gX_Ray ray) {
    Ray result = (Ray){0};
    result.position = gX_F32v3_ConvertToBackend(ray.position);
    result.direction = gX_F32v3_ConvertToBackend(ray.direction);
    return result;
}

RayCollision gX_RayCollision_ConvertToBackend(gX_RayCollision collision) {
    RayCollision result = (RayCollision){0};
    result.hit = collision.hit;
    result.distance = collision.distance;
    result.point = gX_F32v3_ConvertToBackend(collision.point);
    result.normal = gX_F32v3_ConvertToBackend(collision.normal);
    return result;
}

//opencode
Image gX_Image_ConvertToBackend(gX_Image image) {
    Image result = (Image){0};
    result.data = image.data;
    result.width = image.width;
    result.height = image.height;
    result.mipmaps = image.mipmaps;
    result.format = image.format;
    return result;
}

