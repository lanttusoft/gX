/**********************************************************************************************
* 
*   [common/types_3d.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025 lanttu
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

#ifndef TYPES_3D_H
#define TYPES_3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "color.h"
#include "matrix.h"
#include "primary.h"
#include "shader.h"
#include "state.h"
#include "text.h"
#include "texture.h"
#include "vectors.h"

// [raylib] borrowed
// Quaternion, 4 components (Vector4 alias)
typedef gX_F32v4 gX_F32Qua;
typedef gX_F32Qua gX_Quaternion;

// [raylib] borrowed
// Camera, defines position/orientation in 3d space
typedef struct gX_Camera3D {
    gX_F32v3    position;       // Camera position
    gX_F32v3    target;         // Camera target it looks-at
    gX_F32v3    up;             // Camera up vector (rotation over its axis)
    gX_F32      fovy;             // Camera field-of-view aperture in Y (degrees) in perspective, used as near plane width in orthographic
    gX_I32      projection;         // Camera projection: CAMERA_PERSPECTIVE or CAMERA_ORTHOGRAPHIC
} gX_Camera3D;

// [raylib] borrowed
// Updated to raylib 6.x
// Mesh, vertex data and vao/vbo
typedef struct gX_Mesh {
    gX_I32 vertexCount;        // Number of vertices stored in arrays
    gX_I32 triangleCount;      // Number of triangles stored (indexed or not)

    // Vertex attributes data
    gX_F32 *vertices;        // Vertex position (XYZ - 3 components per vertex) (shader-location = 0)
    gX_F32 *texcoords;       // Vertex texture coordinates (UV - 2 components per vertex) (shader-location = 1)
    gX_F32 *texcoords2;      // Vertex texture second coordinates (UV - 2 components per vertex) (shader-location = 5)
    gX_F32 *normals;         // Vertex normals (XYZ - 3 components per vertex) (shader-location = 2)
    gX_F32 *tangents;        // Vertex tangents (XYZW - 4 components per vertex) (shader-location = 4)
    gX_U8 *colors;      // Vertex colors (RGBA - 4 components per vertex) (shader-location = 3)
    gX_U16 *indices;    // Vertex indices (in case vertex data comes indexed)

    // Skin data for animation
    gX_I32 boneCount;          // Number of bones (MAX: 256 bones)
    gX_U8 *boneIndices; // Vertex bone indices, up to 4 bones influence by vertex (skinning) (shader-location = 6)
    gX_F32 *boneWeights;     // Vertex bone weight, up to 4 bones influence by vertex (skinning) (shader-location = 7)
    
    // Runtime animation vertex data (CPU skinning)
    gX_F32 *animVertices;    // Animated vertex positions (after bones transformations)
    gX_F32 *animNormals;     // Animated normals (after bones transformations)

    // OpenGL identifiers
    gX_U32 vaoId;     // OpenGL Vertex Array Object id
    gX_U32 *vboId;    // OpenGL Vertex Buffer Objects id (default vertex data)
} gX_Mesh;

// [raylib] borrowed
// MaterialMap
typedef struct gX_MaterialMap {
    gX_Texture texture;      // Material map texture
    gX_Color color;            // Material map color
    gX_F32 value;            // Material map value
} gX_MaterialMap;

// [raylib] borrowed
// Material, includes shader and maps
typedef struct gX_Material {
    gX_Shader shader;       // Material shader
    gX_MaterialMap *maps;   // Material maps array (MAX_MATERIAL_MAPS)
    gX_F32 params[4];       // Material generic parameters (if required)
} gX_Material;

// [raylib] borrowed
// Transform, vertex transformation data
typedef struct gX_Transform {
    gX_F32v3 translation;   // Translation
    gX_F32Qua rotation;     // Rotation
    gX_F32v3 scale;         // Scale
} gX_Transform;

// [raylib] borrowed
// Bone, skeletal animation bone
typedef struct gX_BoneInfo {
    gX_I8 name[32];          // Bone name
    gX_I32 parent;          // Bone parent
} gX_BoneInfo;

// [raylib] borrowed
// Skeleton, animation bones hierarchy
typedef struct gX_ModelSkeleton {
    gX_I32 boneCount;       // Number of bones
    gX_BoneInfo *bones;     // Bones information (skeleton)
    gX_Transform *bindPose; // Bones base transformation (Transform[])
} gX_ModelSkeleton;

//ai: shared per-draw bone-matrix upload cap. The skinned vertex shaders
//    declare boneMatrices[gX_SKIN_MAX_BONES]; the evaluator may carry more
//    bones than one draw can upload (gX_GFX_SKELETON_MAX_BONES is 256).
#define gX_SKIN_MAX_BONES 128

//ai: clamp a skeleton's bone count to the per-draw upload cap.
static inline gX_I32 gX_Skin_UploadBoneCount(gX_I32 boneCount)
{
    if (boneCount <= 0) return 0;
    return (boneCount > gX_SKIN_MAX_BONES) ? gX_SKIN_MAX_BONES : boneCount;
}

// [raylib] borrowed
// Model, meshes, materials and animation data
typedef struct gX_Model {
    gX_Matrix transform;       // Local transform matrix

    gX_I32 meshCount;          // Number of meshes
    gX_I32 materialCount;      // Number of materials
    gX_Mesh *meshes;           // Meshes array
    gX_Material *materials;    // Materials array
    gX_I32 *meshMaterial;      // Mesh material number
    
    // Animation data
    gX_ModelSkeleton skeleton; // Skeleton for animation

    // Runtime animation data (CPU/GPU skinning)
    gX_Transform *currentPose; // Current animation pose (Transform[])
    gX_Matrix *boneMatrices;   // Bones animated transformation matrices
} gX_Model;

typedef struct gX_Model_Parameters {
    gX_I1    drawMeshCountEnable;
    gX_F32v3 position;
    gX_F32   scale;
    gX_Color color;
    gX_U32   drawMeshCount;
} gX_Model_Parameters;

// [raylib] borrowed
// ModelAnimation
typedef struct gX_ModelAnimation {
    gX_I32 boneCount;          // Number of bones
    gX_I32 frameCount;         // Number of animation frames
    gX_BoneInfo *bones;        // Bones information (skeleton)
    gX_Transform **framePoses; // Poses array by frame
    gX_I8 name[32];          // Animation name
} gX_ModelAnimation;

// [raylib] borrowed
// Ray, ray for raycasting
typedef struct gX_Ray {
    gX_F32v3 position;       // Ray position (origin)
    gX_F32v3 direction;      // Ray direction
} gX_Ray;

// [raylib] borrowed
// RayCollision, ray hit information
typedef struct gX_RayCollision {
    gX_I1 hit;               // Did the ray hit something?
    gX_F32 distance;         // Distance to the nearest hit
    gX_F32v3 point;          // Point of the nearest hit
    gX_F32v3 normal;         // Surface normal of hit
} gX_RayCollision;

// [raylib] borrowed
// BoundingBox
typedef struct gX_BoundingBox {
    gX_F32v3 min;            // Minimum vertex box-corner
    gX_F32v3 max;            // Maximum vertex box-corner
} gX_BoundingBox;

#ifdef __cplusplus
}
#endif

#endif //TYPES_3D_H
