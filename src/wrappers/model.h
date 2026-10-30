/**********************************************************************************************
* 
*   [wrappers/model.h]
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
//lanttu: original wrapper for raylib model/material/mesh functions
#ifndef WRAPPERS_MODEL_H
#define WRAPPERS_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//opencode: Model_Wrapper — deferred-loading model with path + params
typedef struct gX_Model_Wrapper {
    gX_LoadingState loadingState;
    
    gX_I1 shaderAttached;
    
    gX_TextLine path;
    
    gX_Model            model;
    gX_Model_Parameters params;
} gX_Model_Wrapper;

gX_I0 gX_Model_Wrapper_Load(gX_Model_Wrapper *mw);
gX_I0 gX_Model_Wrapper_Unload(gX_Model_Wrapper *mw);
gX_I0 gX_Model_Wrapper_Update(gX_Model_Wrapper *mw);
    
gX_Material gX_Material_LoadDefault(gX_I0);
gX_I1       gX_Material_IsReady(gX_Material material);
gX_I0       gX_Material_SetTexture(gX_Material *material, gX_I32 mapType, gX_Texture texture);
gX_I0       gX_Material_Unload( gX_Material material);

gX_Mesh  gX_Mesh_GenPoly(       gX_I32 sides,  gX_F32 radius);
gX_Mesh  gX_Mesh_GenPlane(      gX_F32 width,  gX_F32 length, gX_I32 resX, gX_I32 resZ);
gX_Mesh  gX_Mesh_GenCube(       gX_F32 width,  gX_F32 height, gX_F32 length);
gX_Mesh  gX_Mesh_GenSphere(     gX_F32 radius, gX_I32 rings,  gX_I32 slices);
gX_Mesh  gX_Mesh_GenHemiSphere( gX_F32 radius, gX_I32 rings,  gX_I32 slices);
gX_Mesh  gX_Mesh_GenCylinder(   gX_F32 radius, gX_F32 height, gX_I32 slices);
//ai: Z-aligned cylinder (ODE's cylinder axis) for physics-driven cylinders.
gX_Mesh  gX_Mesh_GenCylinderZ(  gX_F32 radius, gX_F32 height, gX_I32 slices);
//ai: height-grid mesh — the visual twin of gX_PhysicsWorld_AddHeightfield
//   (row-major heights, x fastest; centered; size scales width/height/depth).
gX_Mesh  gX_Mesh_GenHeightGrid( const gX_F32 *heights, gX_I32 widthSamples,
    gX_I32 depthSamples, gX_F32v3 size);
gX_Mesh  gX_Mesh_GenCone(       gX_F32 radius, gX_F32 height, gX_I32 slices);
gX_Mesh  gX_Mesh_GenTorus(      gX_F32 radius, gX_F32 size,   gX_I32 radSeq, gX_I32 sides);
gX_Mesh  gX_Mesh_GenKnot(       gX_F32 radius, gX_F32 size,   gX_I32 radSeq, gX_I32 sides);
gX_Mesh  gX_Mesh_GenHeightMap(  gX_Image heightMap, gX_F32v3 size);
gX_Mesh  gX_Mesh_GenCubicMap(   gX_Image cubicMap,  gX_F32v3 size);

gX_I0    gX_Mesh_Upload(gX_Mesh *mesh, gX_I1 dynamic);
gX_I0    gX_Mesh_Unload(gX_Mesh mesh);
gX_I0    gX_Mesh_Draw(  gX_Mesh mesh, gX_Material material, gX_Matrix transform);
gX_I0    gX_Mesh_GenTangents(gX_Mesh *mesh);

gX_Model gX_Model_Load(             const gX_I8 *path);
gX_Model gX_Model_LoadMesh(         gX_Mesh mesh);
gX_I1    gX_Model_IsReady(          gX_Model model);
gX_I0    gX_Model_SetMeshMaterial(  gX_Model *model, gX_I32 meshID, gX_I32 materialID);
gX_I0    gX_Model_Unload(           gX_Model model);
gX_I0    gX_Model_Draw(             gX_Model model);
gX_I0    gX_Model_Draw_Parameters(  gX_Model model, gX_Model_Parameters params);
//opencode: draw with an externally-bound program — locs = {mvp, matModel,
//   matNormal, matView, matProjection, texture0} (-1 skipped); binds the
//   diffuse map to unit 0. The caller owns the shader enable/disable.
gX_I0    gX_Model_Draw_Parameters_WithShader(gX_Model model, gX_Model_Parameters params,
    gX_U32 programId, const gX_I32 *locs);
gX_I0    gX_Model_Draw_Wires(       gX_Model model);

//opencode: draw model with full transform (position, rotation axis/angle, scale)
gX_I0    gX_Model_DrawEx(           gX_Model model, gX_F32v3 position,
                                     gX_F32v3 rotationAxis, gX_F32 rotationAngle,
                                     gX_F32v3 scale, gX_Color tint);

gX_BoundingBox gX_Model_GetBoundingBox(gX_Model model);

gX_I0 gX_BoundingBox_Draw(gX_BoundingBox bb, gX_Color color);

//opencode: async OBJ model loading — parses in a worker thread to avoid main-thread softlocks
gX_I0   gX_Model_AsyncLoadStart(const gX_I8 *path);
gX_I1   gX_Model_AsyncLoadIsDone(const gX_I8 *path);
gX_Model gX_Model_AsyncLoadFinish(const gX_I8 *path);

gX_RayCollision gX_RayCollision_Sphere(gX_Ray ray, gX_Sphere sphere);
gX_RayCollision gX_RayCollision_Box(gX_Ray ray, gX_BoundingBox box);
gX_RayCollision gX_RayCollision_Mesh(gX_Ray ray, gX_Mesh mesh, gX_Matrix matrix);
gX_RayCollision gX_RayCollision_Triangle(gX_Ray ray, gX_Triangle tri);
//gX_RayCollision gX_RayCollision_Quard(gX_Ray ray, gX_Quad quad);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_H
