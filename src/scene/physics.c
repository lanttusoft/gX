/**********************************************************************************************
*
*   [gX: src/scene/physics.c]
*   Physics sandbox scene — rigid-body props in the shared simulation world
*
*   Copyright (c) 2026 lanttu, opencode
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


//ai: feature-first port of gameX's scenephysics sandbox (src/scene/scenephysics.c).
//   gameX drew its objects through the RHI immediate path; gX lets the GFX
//   renderer draw them — each prop owns a generated mesh in the renderer's
//   model heap and this scene only spawns bodies and syncs transforms.
//   Physics stepping stays in the module (gX_Physics_Update, main loop).
#include "physics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../camera.h"
#include "../common/heap.h"
#include "../common/log.h"
#include "../common/path.h"
#include "../common/marshal/json_impl.h"
#include "../gxmath/constants.h"
#include "../gxmath/quaternion.h"
#include "../gxmath/vector3.h"
#include "../main.h"
#include "../physics/physics.h"
#include "../physics/elements.h"
#include "../physics/em.h"
#include "../physics/fluid.h"
#include "../physics/thermal.h"
#include "../renderer/renderer.h"
#include "../renderer/gfx/particle.h"
#include "../renderer/gfx/scene.h"
#include "../rhi/batch.h"
#include "../wrappers/input.h"
#include "../wrappers/wrappers.h"

//ai: prop palette shared by the generated meshes.
static const gX_Color gX_ScenePhysics_Palette[] = {
    {232,  93,  90, 255},
    { 94, 186, 125, 255},
    { 92, 143, 232, 255},
    {236, 183,  92, 255},
    {181, 106, 214, 255},
    { 86, 199, 196, 255},
};

static gX_Color gX_ScenePhysics_Tint(gX_I32 index) {
    gX_U32 count = (gX_U32)(sizeof(gX_ScenePhysics_Palette) / sizeof(gX_ScenePhysics_Palette[0]));
    return gX_ScenePhysics_Palette[(gX_U32)index % count];
}

//ai: GFX heap accessor — the renderer owns drawing; this scene owns content.
static gX_RendererGFX *gX_ScenePhysics_GFX(gX_I0) {
    if (!renderer.gfx.data || renderer.gfx.amount == 0) return NULL;
    return (gX_RendererGFX *)renderer.gfx.data;
}

//ai: shape volume — sphere r³, cylinder πr²len (size=(r,r,len)), else box.
static gX_F32 gX_ScenePhysics_PropVolume(gX_ScenePhysics_PropKind kind,
    gX_F32v3 size) {
    gX_F32 pi = 3.14159265f;
    if (kind == gX_SCENE_PHYSICS_PROP_SPHERE) {
        return (4.0f / 3.0f) * pi * size.x * size.x * size.x;
    }
    if (kind == gX_SCENE_PHYSICS_PROP_CYLINDER) {
        return pi * size.x * size.x * size.z;
    }
    return size.x * size.y * size.z;
}

//ai: push one generated prop mesh into the GFX model heap with PBR params.
static gX_I32 gX_ScenePhysics_AddPropModel(gX_RendererGFX *gfx,
    gX_ScenePhysics_PropKind kind, gX_F32v3 size, gX_Color tint) {
    gX_Mesh mesh;
    if (kind == gX_SCENE_PHYSICS_PROP_SPHERE) {
        mesh = gX_Mesh_GenSphere(size.x, 8, 12);
    } else if (kind == gX_SCENE_PHYSICS_PROP_CYLINDER) {
        //ai: Z-aligned so the mesh matches ODE's cylinder axis.
        mesh = gX_Mesh_GenCylinderZ(size.x, size.z, 20);
    } else {
        mesh = gX_Mesh_GenCube(size.x, size.y, size.z);
    }
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) {
        gX_Mesh_Unload(mesh);
        return -1;
    }
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.usePBR = true;
    model->params.albedoTint = tint;
    model->params.metalness = (kind == gX_SCENE_PHYSICS_PROP_SPHERE) ? 0.75f
        : ((kind == gX_SCENE_PHYSICS_PROP_CYLINDER) ? 0.45f : 0.18f);
    model->params.roughness = (kind == gX_SCENE_PHYSICS_PROP_SPHERE) ? 0.25f
        : ((kind == gX_SCENE_PHYSICS_PROP_CYLINDER) ? 0.45f : 0.62f);
    model->params.emissiveColor = tint;
    model->params.emissiveFactor = 0.05f;
    return index;
}

//ai: compact point lights so the PBR props read against the sky (mirrors
//   battle's light recipe).
static gX_I0 gX_ScenePhysics_AddLight(gX_RendererGFX *gfx,
    gX_F32v3 position, gX_Color color) {
    gX_RendererGFX_Light *light = gX_Heap_Add_Push_NoPtr(&gfx->lights);
    if (!light) return;
    gX_RendererGFX_Light_Init(light);
    light->type = 1;
    light->pos = position;
    light->color = color;
    light->diffuseTint = color;
    light->specularTint = color;
    light->brightness = 3.2f;
    light->diffuseFactor = 1.0f;
    light->specularFactor = 1.0f;
    light->attenConst = 1.0f;
    light->attenLinear = 0.08f;
    light->attenQuad = 0.025f;
}

//ai: rig bodies use category 0x4 with a self-excluding mask (ragdoll
//   convention) so chain links never contact each other; props and the
//   ground (category 0x1) still collide with every rig part.
#define GX_SCENE_PHYSICS_RIG_CATEGORY_BITS 0x00000004u
#define GX_SCENE_PHYSICS_RIG_MASK_BITS     0xFFFFFFFBu

//ai: contact materials — the ground is a rough dead surface (the wrapper's
//   min-combining only takes grip away, so a prop keeps its own friction),
//   rig steel is stiff and slightly springy, ragdoll tissue soft and grippy.
#define GX_SCENE_PHYSICS_GROUND_FRICTION    0.90f
#define GX_SCENE_PHYSICS_GROUND_RESTITUTION 0.00f
#define GX_SCENE_PHYSICS_STEEL_FRICTION     0.60f
#define GX_SCENE_PHYSICS_STEEL_RESTITUTION  0.35f
#define GX_SCENE_PHYSICS_TISSUE_FRICTION    0.90f
#define GX_SCENE_PHYSICS_TISSUE_RESTITUTION 0.05f

//ai: map an element to its contact material (the wrapper combines a pair as
//   min friction / max restitution, so the softest/deadest side wins);
//   unknown ids fall back to the wrapper default so callers never read
//   uninitialized data.
gX_PhysicsMaterial gX_ScenePhysics_MaterialForElement(gX_I32 element) {
    const gX_Element_Info *info = gX_Elements_Get(element);
    if (!info) return gX_PhysicsMaterial_Default();
    return (gX_PhysicsMaterial){info->friction, info->restitution, 0.0f, 0.0f};
}

//ai: push one generated rig box (arbitrary extents) into the GFX model heap
//   with steel PBR params; used by every joint rig part.
static gX_I32 gX_ScenePhysics_AddRigModel(gX_RendererGFX *gfx, gX_F32v3 size,
    gX_Color tint) {
    gX_Mesh mesh = gX_Mesh_GenCube(size.x, size.y, size.z);
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) {
        gX_Mesh_Unload(mesh);
        return -1;
    }
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.usePBR = true;
    model->params.albedoTint = tint;
    model->params.metalness = 0.65f;
    model->params.roughness = 0.35f;
    model->params.emissiveColor = tint;
    model->params.emissiveFactor = 0.03f;
    return index;
}

//ai: rig part mesh — the cylinder variant is Z-aligned (ODE's axis), so the
//   wheel's physics and pixels line up without a render-side base rotation.
static gX_I32 gX_ScenePhysics_AddRigCylinderModel(gX_RendererGFX *gfx,
    gX_F32v3 size, gX_Color tint) {
    gX_Mesh mesh = gX_Mesh_GenCylinderZ(size.x, size.z, 20);
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) {
        gX_Mesh_Unload(mesh);
        return -1;
    }
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.usePBR = true;
    model->params.albedoTint = tint;
    model->params.metalness = 0.15f;
    model->params.roughness = 0.7f;
    model->params.emissiveColor = tint;
    model->params.emissiveFactor = 0.05f;
    return index;
}

//ai: one rig body plus its render mesh. gfx may be NULL in headless tests
//   (bodies only, no model). Returns the link slot or -1. The shared part
//   builder under AddRigLink keeps the box call sites unchanged.
static gX_I32 gX_ScenePhysics_AddRigPart(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_ScenePhysics_PropKind shape, gX_F32v3 pos,
    gX_F32v3 size, gX_F32 density, gX_Color tint) {
    if (scene->linkCount >= GX_SCENE_PHYSICS_MAX_LINKS) return -1;
    gX_PhysicsBody body;
    if (shape == gX_SCENE_PHYSICS_PROP_CYLINDER) {
        //ai: size = (radius, radius, length), direction 3 = body Z.
        body = gX_PhysicsBody_AddCylinder(physics.world, size.x, size.z, 3,
            density, pos);
    } else {
        body = gX_PhysicsBody_AddBox(physics.world, size, density, pos);
    }
    if (body.internal == 0) return -1;
    gX_I32 modelIndex = -1;
    if (gfx) {
        modelIndex = (shape == gX_SCENE_PHYSICS_PROP_CYLINDER)
            ? gX_ScenePhysics_AddRigCylinderModel(gfx, size, tint)
            : gX_ScenePhysics_AddRigModel(gfx, size, tint);
        if (modelIndex < 0) {
            gX_PhysicsBody_Destroy(physics.world, body);
            return -1;
        }
    }
    gX_PhysicsBody_SetCollisionFilter(body, GX_SCENE_PHYSICS_RIG_CATEGORY_BITS,
        GX_SCENE_PHYSICS_RIG_MASK_BITS);
    if (scene->materialsEnabled) {
        gX_PhysicsBody_SetMaterial(body, (gX_PhysicsMaterial){
            GX_SCENE_PHYSICS_STEEL_FRICTION,
            GX_SCENE_PHYSICS_STEEL_RESTITUTION, 0.0f, 0.0f});
    }
    gX_PhysicsBody_SetDamping(body, 0.25f, 0.35f);
    gX_ScenePhysics_Link *link = &scene->links[scene->linkCount];
    link->body = body;
    link->modelIndex = modelIndex;
    link->extraModel = -1;
    link->size = size;
    link->shape = shape;
    link->spawnPos = pos;
    link->spawnQuat = gX_F32Qua_Identity();
    link->prevPos = pos;
    link->prevQuat = gX_F32Qua_Identity();
    scene->linkCount++;
    return scene->linkCount - 1;
}
static gX_I32 gX_ScenePhysics_AddRigLink(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32v3 size, gX_F32 density,
    gX_Color tint) {
    return gX_ScenePhysics_AddRigPart(scene, gfx, gX_SCENE_PHYSICS_PROP_BOX,
        pos, size, density, tint);
}

//ai: record a joint for the debug view and the park sweep; a joint that
//   cannot be tracked is destroyed here so it cannot leak.
static gX_I0 gX_ScenePhysics_TrackJoint(gX_ScenePhysics *scene,
    gX_PhysicsJoint joint, gX_ScenePhysics_JointKind kind,
    gX_F32v3 anchor, gX_F32v3 axis) {
    if (joint.internal == 0
        || scene->jointCount >= GX_SCENE_PHYSICS_MAX_JOINTS) {
        if (joint.internal != 0 && physics.world.internal != 0) {
            gX_PhysicsJoint_Destroy(physics.world, joint);
        }
        return;
    }
    gX_ScenePhysics_JointInfo *info = &scene->joints[scene->jointCount++];
    info->joint = joint;
    info->kind = kind;
    info->anchor = anchor;
    info->axis = axis;
}

//ai: static rig mount (post/arm/mast/beam) — a positioned visual with no
//   body, re-added with the rest of the scene content.
static gX_I0 gX_ScenePhysics_AddRigStatic(gX_RendererGFX *gfx, gX_F32v3 pos,
    gX_F32v3 size, gX_Color tint) {
    if (!gfx) return;
    gX_I32 index = gX_ScenePhysics_AddRigModel(gfx, size, tint);
    if (index < 0) return;
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.pos = pos;
}

//ai: the constraint rigs — a hanging chain (ball joints), a hinged door, a
//   motorized windmill and a slider rail. Deterministic placement, built once
//   at scene entry; GX_PHYSICS_JOINTS=0 skips them. Models are skipped when
//   the renderer is unavailable (headless tests).
gX_I0 gX_ScenePhysics_BuildJointRigs(gX_ScenePhysics *scene) {
    if (!scene->jointsEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_Color steel = {152, 160, 172, 255};
    gX_Color brass = {214, 178, 108, 255};
    gX_Color dark = {96, 104, 116, 255};

    // chain: the first link hangs from a fixed anchor, each next one pivots
    // at the centre of the link above.
    {
        gX_F32v3 anchor = {-5.8f, 6.4f, -5.8f};
        gX_I32 previous = -1;
        for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_ROPE_LINKS; i++) {
            gX_F32v3 pos = {anchor.x,
                anchor.y - (gX_F32)(i + 1) * GX_SCENE_PHYSICS_LINK_SPACING,
                anchor.z};
            gX_I32 index = gX_ScenePhysics_AddRigLink(scene, gfx, pos,
                (gX_F32v3){GX_SCENE_PHYSICS_LINK_SIZE,
                    GX_SCENE_PHYSICS_LINK_SIZE, GX_SCENE_PHYSICS_LINK_SIZE},
                1.2f, steel);
            if (index < 0) break;
            gX_PhysicsBody parent = (previous < 0)
                ? scene->links[index].body : scene->links[previous].body;
            gX_PhysicsBody child = (previous < 0)
                ? gX_PHYSICS_BODY_NULL : scene->links[index].body;
            gX_F32v3 pivot = (previous < 0)
                ? anchor
                : gX_PhysicsBody_GetPosition(scene->links[previous].body);
            gX_PhysicsJoint joint = gX_PhysicsJoint_CreateBall(physics.world,
                parent, child, pivot);
            gX_ScenePhysics_TrackJoint(scene, joint,
                gX_SCENE_PHYSICS_JOINT_BALL, pivot,
                (gX_F32v3){0.0f, 0.0f, 0.0f});
            previous = index;
        }
        gX_ScenePhysics_AddRigStatic(gfx,
            (gX_F32v3){anchor.x - 1.3f, 3.5f, anchor.z},
            (gX_F32v3){0.25f, 7.0f, 0.25f}, dark);
        gX_ScenePhysics_AddRigStatic(gfx,
            (gX_F32v3){anchor.x - 0.65f, anchor.y, anchor.z},
            (gX_F32v3){1.3f, 0.25f, 0.25f}, dark);
    }

    // door: plank on a vertical hinge at its edge (drag it or throw props).
    {
        gX_F32v3 pos = {-4.4f, 1.35f, 4.8f};
        gX_F32v3 size = {0.16f, 2.2f, 1.5f};
        gX_I32 index = gX_ScenePhysics_AddRigLink(scene, gfx, pos, size, 0.25f,
            brass);
        if (index >= 0) {
            gX_F32v3 anchor = {pos.x, pos.y, pos.z - size.z * 0.5f};
            gX_F32v3 axis = {0.0f, 1.0f, 0.0f};
            gX_PhysicsJoint joint = gX_PhysicsJoint_CreateHinge(physics.world,
                scene->links[index].body, gX_PHYSICS_BODY_NULL, anchor, axis);
            gX_PhysicsJoint_SetLimits(joint, -1.4f, 1.4f);
            gX_ScenePhysics_TrackJoint(scene, joint,
                gX_SCENE_PHYSICS_JOINT_HINGE, anchor, axis);
            gX_ScenePhysics_AddRigStatic(gfx,
                (gX_F32v3){pos.x, 1.3f, anchor.z},
                (gX_F32v3){0.22f, 2.6f, 0.22f}, dark);
        }
    }

    // windmill: free hinge driven by a motor; whatever strays into the blade
    // gets batted away.
    {
        gX_F32v3 pos = {-6.6f, 4.8f, 0.0f};
        gX_F32v3 size = {2.4f, 0.3f, 0.3f};
        gX_I32 index = gX_ScenePhysics_AddRigLink(scene, gfx, pos, size, 0.6f,
            steel);
        if (index >= 0) {
            gX_F32v3 axis = {0.0f, 0.0f, 1.0f};
            gX_PhysicsJoint joint = gX_PhysicsJoint_CreateHinge(physics.world,
                scene->links[index].body, gX_PHYSICS_BODY_NULL, pos, axis);
            gX_PhysicsJoint_SetMotor(joint, 2.8f, 40.0f);
            gX_ScenePhysics_TrackJoint(scene, joint,
                gX_SCENE_PHYSICS_JOINT_HINGE_MOTOR, pos, axis);
            gX_ScenePhysics_AddRigStatic(gfx,
                (gX_F32v3){pos.x, pos.y * 0.5f, pos.z},
                (gX_F32v3){0.22f, pos.y, 0.22f}, dark);
        }
    }

    // rail: beam-mounted carriage on a slider with symmetric limits (push it
    // with a thrown prop).
    {
        gX_F32v3 pos = {-6.6f, 1.2f, 5.6f};
        gX_F32v3 size = {0.9f, 0.9f, 0.9f};
        gX_I32 index = gX_ScenePhysics_AddRigLink(scene, gfx, pos, size, 0.5f,
            dark);
        if (index >= 0) {
            gX_F32v3 axis = {1.0f, 0.0f, 0.0f};
            gX_PhysicsJoint joint = gX_PhysicsJoint_CreateSlider(physics.world,
                scene->links[index].body, gX_PHYSICS_BODY_NULL, axis);
            gX_PhysicsJoint_SetLimits(joint, -2.4f, 2.4f);
            gX_ScenePhysics_TrackJoint(scene, joint,
                gX_SCENE_PHYSICS_JOINT_SLIDER, pos, axis);
            gX_Color mount = {70, 76, 86, 255};
            gX_F32 beamY = pos.y - 0.55f;
            gX_ScenePhysics_AddRigStatic(gfx,
                (gX_F32v3){pos.x, beamY, pos.z},
                (gX_F32v3){5.6f, 0.12f, 0.2f}, mount);
            gX_ScenePhysics_AddRigStatic(gfx,
                (gX_F32v3){pos.x - 2.7f, beamY * 0.5f, pos.z},
                (gX_F32v3){0.18f, beamY, 0.18f}, mount);
            gX_ScenePhysics_AddRigStatic(gfx,
                (gX_F32v3){pos.x + 2.7f, beamY * 0.5f, pos.z},
                (gX_F32v3){0.18f, beamY, 0.18f}, mount);
        }
    }
}

//ai: default spawn grid — 4x4 footprint, alternating heights, every fourth a sphere.
static gX_F32v3 gX_ScenePhysics_GridPos(gX_I32 slot) {
    gX_F32 x = ((gX_F32)(slot % 4) - 1.5f) * 2.0f;
    gX_F32 z = ((gX_F32)((slot / 4) % 4) - 1.5f) * 2.0f;
    gX_F32 y = 6.0f + (gX_F32)(slot % 5) * 1.6f;
    return (gX_F32v3){x, y, z};
}

//ai: prop material roster — solids only (the gases stay in the table but
//   are not sandbox props).
static const gX_I32 gX_ScenePhysics_PropElements[] = {
    gX_ELEMENT_CARBON, gX_ELEMENT_ALUMINUM, gX_ELEMENT_TITANIUM,
    gX_ELEMENT_IRON, gX_ELEMENT_COPPER, gX_ELEMENT_ZINC,
    gX_ELEMENT_SILVER, gX_ELEMENT_TIN, gX_ELEMENT_GOLD, gX_ELEMENT_LEAD,
};
#define GX_SCENE_PHYSICS_PROP_ELEMENT_COUNT \
    ((gX_I32)(sizeof(gX_ScenePhysics_PropElements) / sizeof(gX_ScenePhysics_PropElements[0])))

//ai: alternating charge signs — the electrostatic pair is the visible part.
static gX_F32 gX_ScenePhysics_PropCharge(gX_I32 slot) {
    return (slot % 2) ? -1.0f : 1.0f;
}

//ai: EM mode colors props by charge sign.
static gX_Color gX_ScenePhysics_ChargeTint(gX_I32 slot) {
    return (gX_ScenePhysics_PropCharge(slot) > 0.0f)
        ? (gX_Color){92, 143, 232, 255}
        : (gX_Color){232, 93, 90, 255};
}

//ai: existing-prop look tint — charge color under EM, otherwise the prop's
//   base tint (element albedo, or the spawner's explicit color for
//   buoys/magnets).
static gX_Color gX_ScenePhysics_PropTint(gX_ScenePhysics *scene, gX_I32 index) {
    if (scene->emEnabled) return gX_ScenePhysics_ChargeTint(index);
    return scene->props[index].tint;
}

//ai: spawn-time look tint — EM colors by charge sign; otherwise the
//   element's albedo (unknown elements fall back to the palette).
static gX_Color gX_ScenePhysics_SpawnTint(gX_ScenePhysics *scene, gX_I32 slot,
    gX_I32 element) {
    if (scene->emEnabled) return gX_ScenePhysics_ChargeTint(slot);
    const gX_Element_Info *info = gX_Elements_Get(element);
    return info ? info->albedo : gX_ScenePhysics_Tint(slot);
}

//ai: deterministic ragdoll drop spots for the entry plan.
static gX_F32v3 gX_ScenePhysics_RagdollSpawnPos(gX_I32 index);

//ai: register one prop with the fluid applier as its volume-equivalent
//   sphere (boxes register their displaced volume; buoyancy and drag are
//   shape-averaged). No-op while the pool is drained.
static gX_I0 gX_ScenePhysics_FluidRegister(gX_ScenePhysics *scene,
    gX_ScenePhysics_Prop *prop) {
    if (!scene->waterEnabled || prop->body.internal == 0) return;
    gX_F32 pi = 3.14159265f;
    gX_F32 volume = gX_ScenePhysics_PropVolume(prop->kind, prop->size);
    gX_PhysicsFluid_AddBody(prop->body,
        cbrtf(3.0f * volume / (4.0f * pi)));
}

//ai: register one ragdoll bone with the fluid applier as its
//   volume-equivalent sphere (capsule = cylinder + caps); bones are
//   human-dense after the mass rescale, so dummies float and take drag.
static gX_I0 gX_ScenePhysics_FluidRegisterBone(gX_Ragdoll_Bone *bone) {
    if (bone->body.internal == 0) return;
    gX_F32 pi = 3.14159265f;
    gX_F32 volume = (4.0f / 3.0f) * pi * bone->radius * bone->radius
        * bone->radius;
    if (bone->length > 0.0f) {
        volume += pi * bone->radius * bone->radius * bone->length;
    }
    gX_PhysicsFluid_AddBody(bone->body,
        cbrtf(3.0f * volume / (4.0f * pi)));
}

//ai: register one prop's induced-magnetic response when its element has a
//   meaningful susceptibility at sim scale (iron; the weak
//   paramagnets/diamagnets stay inert). alpha = chi * scale * volume rides
//   the SI table value, so m_ind = alpha * B pulls ferrous props toward
//   field sources.
static gX_I0 gX_ScenePhysics_RegisterPropEM(gX_ScenePhysics *scene,
    gX_ScenePhysics_Prop *prop) {
    (void)scene;
    if (prop->body.internal == 0) return;
    const gX_Element_Info *info = gX_Elements_Get(prop->element);
    if (!info) return;
    if (fabsf(info->susceptibility) < GX_SCENE_PHYSICS_CHI_MIN) return;
    gX_F32 volume = gX_ScenePhysics_PropVolume(prop->kind, prop->size);
    gX_PhysicsEM_SetSusceptibility(prop->body,
        info->susceptibility * GX_SCENE_PHYSICS_CHI_SCALE * volume);
}

//ai: shared spawn core — body, material, model and appliers; the element
//   plan (SpawnProp), the buoy plan (SpawnBuoy) and the bar magnets
//   (SpawnMagnet) only differ in inputs. element NONE skips EM/thermal and
//   the element material, so buoys/magnets stay applier-free at spawn.
static gX_I32 gX_ScenePhysics_SpawnPropEx(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos, gX_ScenePhysics_PropKind kind,
    gX_F32v3 size, gX_F32 density, gX_I32 element, gX_Color tint) {
    gX_I32 slot = scene->propCount;
    if (slot >= GX_SCENE_PHYSICS_MAX_PROPS) return -1;
    gX_ScenePhysics_Prop *prop = &scene->props[slot];

    if (kind == gX_SCENE_PHYSICS_PROP_SPHERE) {
        prop->body = gX_PhysicsBody_AddSphere(physics.world, size.x, density,
            pos);
    } else if (kind == gX_SCENE_PHYSICS_PROP_CYLINDER) {
        //ai: direction 3 = the body's Z (ODE's cylinder axis; the mesh
        //   generator matches it).
        prop->body = gX_PhysicsBody_AddCylinder(physics.world, size.x,
            size.z, 3, density, pos);
    } else {
        prop->body = gX_PhysicsBody_AddBox(physics.world, size, density, pos);
    }
    if (prop->body.internal == 0) return -1;

    //ai: light per-prop damping — the world default is the ported heavy
    //   per-step damping; friction stops slides, rolling resistance slows
    //   rolls, and this is the air-drag stand-in.
    gX_PhysicsBody_SetDamping(prop->body,
        GX_SCENE_PHYSICS_PROP_DAMPING_LINEAR,
        GX_SCENE_PHYSICS_PROP_DAMPING_ANGULAR);

    if (scene->materialsEnabled && element != gX_ELEMENT_NONE) {
        gX_PhysicsBody_SetMaterial(prop->body,
            gX_ScenePhysics_MaterialForElement(element));
    }

    gX_I32 modelIndex = -1;
    if (gfx) {
        modelIndex = gX_ScenePhysics_AddPropModel(gfx, kind, size, tint);
        if (modelIndex < 0) {
            gX_PhysicsBody_Destroy(physics.world, prop->body);
            prop->body = gX_PHYSICS_BODY_NULL;
            return -1;
        }
    }

    prop->modelIndex = modelIndex;
    prop->element = element;
    prop->size = size;
    prop->dipole = (gX_F32v3){0.0f, 0.0f, 0.0f};
    prop->tint = tint;
    prop->spawnPos = pos;
    prop->kind = kind;
    prop->molten = 0;
    prop->prevPos = pos;
    prop->prevQuat = gX_F32Qua_Identity();
    scene->propCount = slot + 1;
    scene->liveCount++;

    const gX_Element_Info *info = gX_Elements_Get(element);
    if (gfx && modelIndex >= 0 && info) {
        //ai: the element's real look (polished-metal ballparks).
        gX_RendererGFX_Model *model =
            &((gX_RendererGFX_Model *)gfx->models.data)[modelIndex];
        model->params.metalness = info->metalness;
        model->params.roughness = info->roughness;
    }
    if (scene->emEnabled && info) {
        gX_PhysicsEM_AddBody(prop->body, gX_ScenePhysics_PropCharge(slot));
    }
    if (gX_PhysicsThermal_Enabled() && info) {
        //ai: sim mass = density * shape volume, for the thermal model.
        gX_F32 volume = gX_ScenePhysics_PropVolume(kind, size);
        gX_PhysicsThermal_AddBody(prop->body, gX_PhysicsThermal_GetSettings()
                .ambientTemperature + scene->heatInitial,
            density * volume, info->specificHeat, info->emissivity,
            info->meltingPoint, info->latentHeat);
    }
    if (scene->relativityEnabled) {
        //ai: the light-speed layer tracks every prop's rest mass — the
        //   applier re-masses it by gamma while it moves fast.
        gX_PhysicsRelativity_AddBody(prop->body,
            gX_PhysicsBody_GetMass(prop->body));
    }
    gX_ScenePhysics_FluidRegister(scene, prop);
    gX_ScenePhysics_RegisterPropEM(scene, prop);
    return 0;
}

//ai: spawn one element prop into a slot. Returns 0 on success.
static gX_I32 gX_ScenePhysics_SpawnProp(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos) {
    gX_I32 slot = scene->propCount;
    if (slot >= GX_SCENE_PHYSICS_MAX_PROPS) return -1;
    //ai: the grid mixes the body types — boxes, standing cylinders and
    //   spheres cycle so the shape set stays visible in the default field.
    gX_I32 cycle = slot % 4;
    gX_ScenePhysics_PropKind kind = (cycle == 3)
        ? gX_SCENE_PHYSICS_PROP_SPHERE
        : ((cycle == 2) ? gX_SCENE_PHYSICS_PROP_CYLINDER
            : gX_SCENE_PHYSICS_PROP_BOX);
    gX_F32v3 size;
    if (kind == gX_SCENE_PHYSICS_PROP_SPHERE) {
        size = (gX_F32v3){0.5f, 0.5f, 0.5f};
    } else if (kind == gX_SCENE_PHYSICS_PROP_CYLINDER) {
        size = (gX_F32v3){0.32f, 0.32f, 0.9f};
    } else {
        size = (gX_F32v3){1.0f, 1.0f, 1.0f};
    }
    //ai: the wrapper's mass parameter is a density; element table supplies it.
    gX_I32 element = gX_ScenePhysics_PropElements[
        slot % GX_SCENE_PHYSICS_PROP_ELEMENT_COUNT];
    const gX_Element_Info *info = gX_Elements_Get(element);
    gX_F32 density = info ? info->density * GX_SCENE_PHYSICS_DENSITY_SCALE : 1.0f;
    gX_I32 result = gX_ScenePhysics_SpawnPropEx(scene, gfx, pos, kind, size,
        density, element, gX_ScenePhysics_SpawnTint(scene, slot, element));
    if (result == 0 && kind == gX_SCENE_PHYSICS_PROP_CYLINDER
        && scene->propCount > 0) {
        //ai: cylinders stand (drums) — the physics axis is the body's Z, so
        //   the spawn pose tilts it upright.
        gX_I32 index = scene->propCount - 1;
        gX_F32Qua upright = gX_F32Qua_FromAxisAngle(
            (gX_F32v3){1.0f, 0.0f, 0.0f}, -3.14159265f * 0.5f);
        gX_PhysicsBody_SetRotation(scene->props[index].body, upright);
        scene->props[index].prevQuat = upright;
    }
    return result;
}

//ai: spawn one buoy — a light prop sphere meant to float the waterline. It
//   uses a prop slot (drag/delete/reset work) with the NONE element; the
//   model gets a non-metal, slightly self-lit look so it reads on the water.
static gX_I32 gX_ScenePhysics_SpawnBuoy(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos) {
    gX_I32 result = gX_ScenePhysics_SpawnPropEx(scene, gfx, pos,
        gX_SCENE_PHYSICS_PROP_SPHERE,
        (gX_F32v3){GX_SCENE_PHYSICS_BUOY_RADIUS, GX_SCENE_PHYSICS_BUOY_RADIUS,
            GX_SCENE_PHYSICS_BUOY_RADIUS},
        GX_SCENE_PHYSICS_BUOY_DENSITY, gX_ELEMENT_NONE,
        (gX_Color){238, 126, 62, 255});
    if (result != 0 || scene->propCount == 0) return result;
    gX_I32 modelIndex = scene->props[scene->propCount - 1].modelIndex;
    if (gfx && modelIndex >= 0) {
        gX_RendererGFX_Model *model =
            &((gX_RendererGFX_Model *)gfx->models.data)[modelIndex];
        model->params.metalness = 0.05f;
        model->params.roughness = 0.45f;
        model->params.emissiveFactor = 0.22f;
    }
    return result;
}

//ai: spawn one bar magnet — an elongated box whose local Z carries the
//   dipole moment. The body starts at `yaw` so the demo shows magnets
//   rotating into alignment instead of starting pre-aligned; the magnet
//   model is tinted warm so the bars read against the steel rigs.
static gX_I32 gX_ScenePhysics_SpawnMagnet(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32 yaw) {
    gX_I32 result = gX_ScenePhysics_SpawnPropEx(scene, gfx, pos,
        gX_SCENE_PHYSICS_PROP_BOX,
        (gX_F32v3){GX_SCENE_PHYSICS_MAGNET_SIZE,
            GX_SCENE_PHYSICS_MAGNET_SIZE, GX_SCENE_PHYSICS_MAGNET_LENGTH},
        GX_SCENE_PHYSICS_MAGNET_DENSITY, gX_ELEMENT_NONE,
        (gX_Color){198, 84, 66, 255});
    if (result != 0 || scene->propCount == 0) return result;
    gX_ScenePhysics_Prop *prop = &scene->props[scene->propCount - 1];
    gX_PhysicsBody_SetRotation(prop->body,
        gX_F32Qua_FromAxisAngle((gX_F32v3){0.0f, 1.0f, 0.0f}, yaw));
    prop->prevQuat = gX_F32Qua_FromAxisAngle((gX_F32v3){0.0f, 1.0f, 0.0f},
        yaw);
    //ai: the local moment rides the long axis; the applier rotates it with
    //   the body each step.
    prop->dipole = (gX_F32v3){0.0f, 0.0f, GX_SCENE_PHYSICS_MAGNET_MOMENT};
    return result;
}

//ai: a loose iron test piece — a small element-prop sphere that the magnet
//   row visibly pulls in when N is on (its susceptibility rides the element
//   table).
static gX_I32 gX_ScenePhysics_SpawnIronPiece(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos) {
    const gX_Element_Info *info = gX_Elements_Get(gX_ELEMENT_IRON);
    gX_F32 density = info
        ? info->density * GX_SCENE_PHYSICS_DENSITY_SCALE : 7.874f;
    gX_Color tint = info ? info->albedo : (gX_Color){176, 180, 187, 255};
    return gX_ScenePhysics_SpawnPropEx(scene, gfx, pos,
        gX_SCENE_PHYSICS_PROP_SPHERE,
        (gX_F32v3){GX_SCENE_PHYSICS_IRON_RADIUS, GX_SCENE_PHYSICS_IRON_RADIUS,
            GX_SCENE_PHYSICS_IRON_RADIUS},
        density, gX_ELEMENT_IRON, tint);
}

//ai: fill/drain the pool — fluid registry, translucent volume and buoys.
//   The basin statics persist either way.
static gX_I0 gX_ScenePhysics_SetWater(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_I1 enabled) {
    scene->waterEnabled = enabled ? 1 : 0;
    if (!enabled) {
        gX_PhysicsFluid_Reset();
        if (gfx && scene->waterModel >= 0) {
            gX_RendererGFX_Model_Remove(gfx, (gX_U32)scene->waterModel);
        }
        scene->waterModel = -1;
        gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: pool drained");
        return;
    }

    gX_F32 half = GX_SCENE_PHYSICS_POOL_SIZE * 0.5f;
    gX_PhysicsFluid_Settings settings = gX_PhysicsFluid_Defaults();
    settings.surfaceY = GX_SCENE_PHYSICS_POOL_SURFACE;
    settings.minX = GX_SCENE_PHYSICS_POOL_CENTER_X - half;
    settings.maxX = GX_SCENE_PHYSICS_POOL_CENTER_X + half;
    settings.minZ = GX_SCENE_PHYSICS_POOL_CENTER_Z - half;
    settings.maxZ = GX_SCENE_PHYSICS_POOL_CENTER_Z + half;
    settings.floorY = 0.0f;
    gX_PhysicsFluid_SetSettings(settings);
    gX_PhysicsFluid_SetEnabled(1);
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal != 0) {
            gX_ScenePhysics_FluidRegister(scene, &scene->props[i]);
        }
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            gX_ScenePhysics_FluidRegisterBone(&slot->ragdoll.bones[b]);
        }
    }

    if (gfx && scene->waterModel < 0) {
        gX_F32 size = GX_SCENE_PHYSICS_POOL_SIZE - 0.06f;
        gX_Mesh mesh = gX_Mesh_GenCube(size, GX_SCENE_PHYSICS_POOL_SURFACE,
            size);
        gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
        if (index >= 0) {
            gX_RendererGFX_Model *model =
                &((gX_RendererGFX_Model *)gfx->models.data)[index];
            model->params.pos = (gX_F32v3){GX_SCENE_PHYSICS_POOL_CENTER_X,
                GX_SCENE_PHYSICS_POOL_SURFACE * 0.5f,
                GX_SCENE_PHYSICS_POOL_CENTER_Z};
            model->params.usePBR = true;
            model->params.albedoTint = (gX_Color){64, 140, 190, 255};
            model->params.metalness = 0.0f;
            model->params.roughness = 0.08f;
            model->params.emissiveColor = model->params.albedoTint;
            model->params.emissiveFactor = 0.06f;
            //ai: blended volume — the committed transparent pass draws it
            //   back-to-front, so props read through the water tint.
            model->params.enableBlending = 1;
            model->params.blendOpacity = 0.42f;
            scene->waterModel = index;
        } else {
            gX_Mesh_Unload(mesh);
            gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: water mesh unavailable");
        }
    }
    if (gfx && !scene->buoysSpawned) {
        scene->buoysSpawned = 1;
        for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_BUOYS; i++) {
            gX_F32v3 pos = {
                GX_SCENE_PHYSICS_POOL_CENTER_X - 1.6f + 1.6f * (gX_F32)i,
                GX_SCENE_PHYSICS_POOL_SURFACE + 0.8f,
                GX_SCENE_PHYSICS_POOL_CENTER_Z + ((i % 2) ? 1.6f : -1.6f)
            };
            gX_ScenePhysics_SpawnBuoy(scene, gfx, pos);
        }
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: pool filled (%d bodies)",
        gX_PhysicsFluid_Count());
}

//ai: the pool basin — four static walls (persistent, like the ground) plus
//   their visual boxes, then the filled state. Headless tests get the walls
//   and the fluid state without models.
gX_I0 gX_ScenePhysics_BuildWater(gX_ScenePhysics *scene) {
    if (physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_F32 wall = GX_SCENE_PHYSICS_POOL_WALL;
    gX_F32 half = GX_SCENE_PHYSICS_POOL_SIZE * 0.5f;
    gX_F32 height = GX_SCENE_PHYSICS_POOL_SURFACE + 0.4f;
    gX_Color basin = {58, 74, 92, 255};
    for (gX_I32 i = 0; i < 4; i++) {
        gX_F32v3 size = (i < 2)
            ? (gX_F32v3){GX_SCENE_PHYSICS_POOL_SIZE + wall * 2.0f, height, wall}
            : (gX_F32v3){wall, height, GX_SCENE_PHYSICS_POOL_SIZE};
        gX_F32v3 pos = {
            GX_SCENE_PHYSICS_POOL_CENTER_X,
            height * 0.5f,
            GX_SCENE_PHYSICS_POOL_CENTER_Z
        };
        if (i == 0) pos.z -= half + wall * 0.5f;
        else if (i == 1) pos.z += half + wall * 0.5f;
        else if (i == 2) pos.x -= half + wall * 0.5f;
        else pos.x += half + wall * 0.5f;
        if (scene->poolWall[i].internal == 0) {
            scene->poolWall[i] = gX_PhysicsWorld_AddStaticBox(physics.world,
                size, pos);
        }
        gX_ScenePhysics_AddRigStatic(gfx, pos, size, basin);
    }
    //ai: a teal basin floor so the water volume reads against the dark slab.
    gX_ScenePhysics_AddRigStatic(gfx,
        (gX_F32v3){GX_SCENE_PHYSICS_POOL_CENTER_X, -0.02f,
            GX_SCENE_PHYSICS_POOL_CENTER_Z},
        (gX_F32v3){GX_SCENE_PHYSICS_POOL_SIZE - 0.1f, 0.06f,
            GX_SCENE_PHYSICS_POOL_SIZE - 0.1f},
        (gX_Color){46, 96, 106, 255});
    if (scene->waterEnabled) gX_ScenePhysics_SetWater(scene, gfx, 1);
}

//ai: (re)register the applier extras the prop data carries — magnet moments
//   and element susceptibilities. A registry reset (M toggling) wipes both,
//   while the props keep their data (local moment, element).
static gX_I0 gX_ScenePhysics_RegisterEMExtras(gX_ScenePhysics *scene) {
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0) continue;
        if (prop->dipole.x != 0.0f || prop->dipole.y != 0.0f
            || prop->dipole.z != 0.0f) {
            gX_PhysicsEM_SetDipole(prop->body, prop->dipole);
        }
        gX_ScenePhysics_RegisterPropEM(scene, prop);
    }
}

//ai: toggle the magnetic forces — the moments stay registered and the
//   shared scale gates them; the module serves charges and magnets, so it
//   stays enabled while either mode is active.
gX_I0 gX_ScenePhysics_SetMagnets(gX_ScenePhysics *scene, gX_I1 enabled) {
    scene->magnetsOn = enabled ? 1 : 0;
    gX_PhysicsEM_Settings settings = gX_PhysicsEM_GetSettings();
    settings.dipoleScale = scene->magnetsOn ? scene->magnetScale : 0.0f;
    gX_PhysicsEM_SetSettings(settings);
    if (physics.world.internal != 0) {
        gX_PhysicsEM_SetEnabled(scene->emEnabled || scene->magnetsOn);
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: magnets %s",
        scene->magnetsOn ? "on" : "off");
}

//ai: magnet content — a row of bar magnets with varied yaw, each registered
//   with the EM applier (local moment along the bar). Headless-safe.
gX_I0 gX_ScenePhysics_BuildMagnets(gX_ScenePhysics *scene) {
    if (!scene->magnetsEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_MAGNET_COUNT; i++) {
        gX_F32v3 pos = {
            -2.5f + 1.0f * (gX_F32)i,
            GX_SCENE_PHYSICS_MAGNET_SIZE * 0.5f + 0.05f,
            -5.5f
        };
        gX_ScenePhysics_SpawnMagnet(scene, gfx, pos, 0.4f + 0.9f * (gX_F32)i);
    }
    //ai: loose iron pieces in front of the row — pressing N pulls them in.
    for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_IRON_COUNT; i++) {
        gX_F32v3 pos = {
            -1.6f + 1.6f * (gX_F32)i,
            0.6f,
            -4.2f
        };
        gX_ScenePhysics_SpawnIronPiece(scene, gfx, pos);
    }
    gX_ScenePhysics_RegisterEMExtras(scene);
    gX_ScenePhysics_SetMagnets(scene, scene->magnetsOn);
}

//ai: conveyor belt — one static strip whose top surface carries bodies along
//   +X at the belt material's surface velocity; the statics persist like
//   the ground and the visual rebuilds per entry. Headless-safe.
gX_I0 gX_ScenePhysics_BuildBelt(gX_ScenePhysics *scene) {
    if (physics.world.internal == 0) return;
    const gX_I8 *envBelt = (const gX_I8 *)getenv("GX_PHYSICS_BELT");
    if (envBelt && envBelt[0] == '0') return;
    gX_F32v3 size = {GX_SCENE_PHYSICS_BELT_LENGTH,
        GX_SCENE_PHYSICS_BELT_HEIGHT, GX_SCENE_PHYSICS_BELT_WIDTH};
    gX_F32v3 pos = {GX_SCENE_PHYSICS_BELT_CENTER_X,
        GX_SCENE_PHYSICS_BELT_HEIGHT * 0.5f, GX_SCENE_PHYSICS_BELT_CENTER_Z};
    if (scene->beltCollider.internal == 0) {
        scene->beltCollider = gX_PhysicsWorld_AddStaticBox(physics.world,
            size, pos);
        if (scene->beltCollider.internal != 0) {
            gX_PhysicsMaterial belt = gX_PhysicsMaterial_Default();
            belt.surfaceVelocity = GX_SCENE_PHYSICS_BELT_SPEED;
            gX_PhysicsCollider_SetMaterial(scene->beltCollider, belt);
        }
    }
    gX_ScenePhysics_AddRigStatic(gX_ScenePhysics_GFX(), pos, size,
        (gX_Color){54, 60, 70, 255});
}

//ai: push one mover's generated cube mesh into the GFX heap.
static gX_I32 gX_ScenePhysics_AddMoverModel(gX_RendererGFX *gfx, gX_F32v3 size,
    gX_Color tint) {
    if (!gfx) return -1;
    gX_Mesh mesh = gX_Mesh_GenCube(size.x, size.y, size.z);
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) {
        gX_Mesh_Unload(mesh);
        return -1;
    }
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.usePBR = true;
    model->params.albedoTint = tint;
    model->params.metalness = 0.35f;
    model->params.roughness = 0.5f;
    model->params.emissiveColor = tint;
    model->params.emissiveFactor = 0.10f;
    return index;
}

//ai: kinematic movers — a driven lift platform (vertical sine) and a spinning
//   turntable; GX_PHYSICS_MOVERS=0 skips them. The bodies are kinematic: the
//   drive steers their velocities from the step hook, ODE integrates them
//   (forces/gravity ignored), and dynamic bodies ride the surfaces by
//   contact. Headless-safe (models skipped without a renderer).
gX_I0 gX_ScenePhysics_BuildMovers(gX_ScenePhysics *scene) {
    if (!scene->moversEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_PhysicsMaterial grip = gX_PhysicsMaterial_Default();
    grip.friction = 0.9f;
    grip.restitution = 0.0f;

    // lift: rises and lowers under a payload.
    gX_ScenePhysics_Mover *lift = &scene->movers[0];
    gX_F32v3 liftSize = {GX_SCENE_PHYSICS_LIFT_SIZE,
        GX_SCENE_PHYSICS_LIFT_THICK, GX_SCENE_PHYSICS_LIFT_SIZE};
    gX_F32v3 liftPos = {GX_SCENE_PHYSICS_LIFT_X,
        GX_SCENE_PHYSICS_LIFT_THICK * 0.5f, GX_SCENE_PHYSICS_LIFT_Z};
    lift->body = gX_PhysicsBody_AddBox(physics.world, liftSize, 1.0f,
        liftPos);
    if (lift->body.internal == 0) return;
    gX_PhysicsBody_SetType(lift->body, gX_PHYSICS_BODY_KINEMATIC);
    gX_PhysicsBody_SetDamping(lift->body, 0.0f, 0.0f);
    gX_PhysicsBody_SetMaterial(lift->body, grip);
    lift->isTable = 0;
    lift->phase = 0.0f;
    lift->baseY = liftPos.y;
    lift->rate = 0.0f;
    lift->spawnPos = liftPos;
    lift->prevPos = liftPos;
    lift->prevQuat = gX_F32Qua_Identity();
    lift->modelIndex = gX_ScenePhysics_AddMoverModel(gfx, liftSize,
        (gX_Color){206, 178, 64, 255});
    scene->moverCount = 1;

    // turntable: spins about Y and carries its payload around.
    gX_ScenePhysics_Mover *table = &scene->movers[1];
    gX_F32v3 tableSize = {GX_SCENE_PHYSICS_TABLE_SIZE,
        GX_SCENE_PHYSICS_TABLE_THICK, GX_SCENE_PHYSICS_TABLE_SIZE};
    gX_F32v3 tablePos = {GX_SCENE_PHYSICS_TABLE_X,
        GX_SCENE_PHYSICS_TABLE_THICK * 0.5f, GX_SCENE_PHYSICS_TABLE_Z};
    table->body = gX_PhysicsBody_AddBox(physics.world, tableSize, 1.0f,
        tablePos);
    if (table->body.internal == 0) return;
    gX_PhysicsBody_SetType(table->body, gX_PHYSICS_BODY_KINEMATIC);
    gX_PhysicsBody_SetDamping(table->body, 0.0f, 0.0f);
    gX_PhysicsBody_SetMaterial(table->body, grip);
    table->isTable = 1;
    table->phase = 0.0f;
    table->baseY = tablePos.y;
    table->rate = GX_SCENE_PHYSICS_TABLE_RATE;
    table->spawnPos = tablePos;
    table->prevPos = tablePos;
    table->prevQuat = gX_F32Qua_Identity();
    table->modelIndex = gX_ScenePhysics_AddMoverModel(gfx, tableSize,
        (gX_Color){96, 142, 190, 255});
    scene->moverCount = 2;
}

//ai: per-step mover drive — velocity steering only (ODE integrates kinematic
//   bodies); called from the step hook and by the headless mover test.
gX_I0 gX_ScenePhysics_DriveMovers(gX_ScenePhysics *scene, gX_F32 dt) {
    if (!scene->moversEnabled || physics.world.internal == 0) return;
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal == 0) continue;
        if (mover->isTable) {
            gX_PhysicsBody_SetAngularVel(mover->body,
                (gX_F32v3){0.0f, mover->rate, 0.0f});
        } else {
            gX_F32 omega = 2.0f * 3.14159265f
                / GX_SCENE_PHYSICS_LIFT_PERIOD;
            mover->phase += omega * dt;
            gX_F32 vel = GX_SCENE_PHYSICS_LIFT_AMP * 0.5f
                * sinf(mover->phase) * omega;
            gX_PhysicsBody_SetLinearVel(mover->body,
                (gX_F32v3){0.0f, vel, 0.0f});
        }
    }
}

//ai: live rig toggle — remove the current joint rigs (models first, then
//   joints before their bodies) and rebuild when enabled; the editor's
//   joints switch.
static gX_I0 gX_ScenePhysics_SetJoints(gX_ScenePhysics *scene, gX_I1 enabled) {
    scene->jointsEnabled = enabled ? 1 : 0;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    for (gX_I32 i = 0; i < scene->jointCount; i++) {
        if (scene->joints[i].joint.internal != 0
            && physics.world.internal != 0) {
            gX_PhysicsJoint_Destroy(physics.world, scene->joints[i].joint);
        }
        scene->joints[i].joint = gX_PHYSICS_JOINT_NULL;
    }
    scene->jointCount = 0;
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        if (scene->links[i].modelIndex >= 0 && gfx) {
            gX_RendererGFX_Model_Remove(gfx,
                (gX_U32)scene->links[i].modelIndex);
        }
        if (scene->links[i].body.internal != 0 && physics.world.internal != 0) {
            gX_PhysicsBody_Destroy(physics.world, scene->links[i].body);
        }
        scene->links[i].body = gX_PHYSICS_BODY_NULL;
        scene->links[i].modelIndex = -1;
        scene->links[i].extraModel = -1;
    }
    scene->linkCount = 0;
    scene->targetLink = -1;
    scene->targetJoint = -1;
    scene->targetBroken = 0;
    scene->pumpLink = -1;
    scene->pumpJoint = -1;
    if (scene->jointsEnabled) gX_ScenePhysics_BuildJointRigs(scene);
    //ai: the rover, the break target and the pump also live in the
    //   link/joint pools — rebuild them with the rigs so the editor's
    //   joints toggle cannot remove them permanently.
    if (scene->jointsEnabled && scene->roverEnabled) {
        gX_ScenePhysics_BuildRover(scene);
    }
    if (scene->jointsEnabled && scene->targetEnabled) {
        gX_ScenePhysics_BuildBreakTarget(scene);
    }
    if (scene->jointsEnabled && scene->pumpEnabled) {
        gX_ScenePhysics_BuildPump(scene);
    }
}

//ai: ragdoll appliers — bones join the EM registry with the slot's charge
//   sign (scaled for the rescaled bone masses so joint-scale separations
//   splay like before without slamming the force cap), the thermal registry
//   with a generic tissue material and the nominal per-bone mass, and the
//   fluid applier (when the pool is filled) so dummies float and take drag.
static gX_I0 gX_ScenePhysics_RagdollCharge(gX_ScenePhysics *scene,
    gX_ScenePhysics_RagdollSlot *slot) {
    (void)scene;
    for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
        if (slot->ragdoll.bones[b].body.internal == 0) continue;
        gX_PhysicsEM_AddBody(slot->ragdoll.bones[b].body,
            slot->charge * GX_SCENE_PHYSICS_RAGDOLL_CHARGE);
    }
}

gX_I0 gX_ScenePhysics_RagdollRegister(gX_ScenePhysics *scene,
    gX_ScenePhysics_RagdollSlot *slot) {
    if (gX_PhysicsEM_Enabled()) {
        gX_ScenePhysics_RagdollCharge(scene, slot);
    }
    for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
        gX_Ragdoll_Bone *bone = &slot->ragdoll.bones[b];
        if (bone->body.internal == 0) continue;
        if (scene->waterEnabled) gX_ScenePhysics_FluidRegisterBone(bone);
        if (!gX_PhysicsThermal_Enabled()) continue;
        gX_F32 temperature = gX_PhysicsThermal_GetSettings().ambientTemperature
            + scene->heatInitial;
        //ai: the thermal mass stays the nominal kg figure — heat capacity
        //   and impact heating are unchanged by the sim-mass rescale.
        gX_PhysicsThermal_AddBody(bone->body, temperature, bone->mass,
            GX_SCENE_PHYSICS_RAGDOLL_CP, GX_SCENE_PHYSICS_RAGDOLL_EMISSIVITY,
            0.0f, 0.0f);
    }
}

gX_I0 gX_ScenePhysics_RagdollUnregister(gX_ScenePhysics *scene,
    gX_ScenePhysics_RagdollSlot *slot) {
    (void)scene;
    for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
        gX_PhysicsBody body = slot->ragdoll.bones[b].body;
        if (body.internal == 0) continue;
        gX_PhysicsEM_RemoveBody(body);
        gX_PhysicsThermal_RemoveBody(body);
        gX_PhysicsFluid_RemoveBody(body);
    }
}

//ai: destroy every prop/ragdoll body of the previous entry and reset slot
//   accounting. Model indices die with the renderer's scene clear.
static gX_I0 gX_ScenePhysics_ClearContent(gX_ScenePhysics *scene) {
    //ai: pins first, then the content joints — a constraint must not outlive
    //   its bodies, and the pins hold content bodies.
    gX_ScenePhysics_UnpinAll(scene);
    for (gX_I32 i = 0; i < scene->jointCount; i++) {
        if (scene->joints[i].joint.internal != 0
            && physics.world.internal != 0) {
            gX_PhysicsJoint_Destroy(physics.world, scene->joints[i].joint);
        }
        scene->joints[i].joint = gX_PHYSICS_JOINT_NULL;
    }
    scene->jointCount = 0;
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        if (scene->links[i].body.internal != 0 && physics.world.internal != 0) {
            gX_PhysicsBody_Destroy(physics.world, scene->links[i].body);
        }
        scene->links[i].body = gX_PHYSICS_BODY_NULL;
        scene->links[i].modelIndex = -1;
        scene->links[i].extraModel = -1;
    }
    scene->linkCount = 0;
    //ai: kinematic movers — bodies only (the models die with the GFX heap).
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal != 0 && physics.world.internal != 0) {
            gX_PhysicsBody_Destroy(physics.world, mover->body);
        }
        mover->body = gX_PHYSICS_BODY_NULL;
        mover->modelIndex = -1;
    }
    scene->moverCount = 0;
    scene->moverPayloads = 0;
    //ai: balance posts are props (they die with the pool); drop the index
    //   bookkeeping so B and the debug markers skip stale slots.
    scene->balanceCount = 0;
    //ai: the rover lives in the link pool (destroyed above).
    scene->roverLinkFirst = 0;
    scene->roverLinkCount = 0;
    scene->roverJointFirst = 0;
    //ai: the break target's board is a link and its weld a joint (both
    //   destroyed above); drop the tracking so nothing points at freed
    //   slots.
    scene->targetLink = -1;
    scene->targetJoint = -1;
    scene->targetBroken = 0;
    //ai: the pump's plunger is a link and its piston joint a tracked joint.
    scene->pumpLink = -1;
    scene->pumpJoint = -1;
    scene->pumpPhase = 0.0f;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal != 0 && physics.world.internal != 0) {
            gX_PhysicsBody_Destroy(physics.world, scene->props[i].body);
        }
        scene->props[i].body = gX_PHYSICS_BODY_NULL;
        scene->props[i].modelIndex = -1;
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        if (physics.world.internal != 0) {
            gX_ScenePhysics_RagdollUnregister(scene, slot);
            gX_Ragdoll_Destroy(&slot->ragdoll, physics.world);
        }
        for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
            slot->boneModel[b] = -1;
        }
    }
    scene->propCount = 0;
    scene->liveCount = 0;
    scene->spawnTotal = 0;
    scene->dragProp = -1;
    scene->dragLink = -1;
    scene->dragActive = 0;
    scene->hoverProp = -1;
    scene->hoverLink = -1;
    scene->ragdollCount = 0;
    scene->ragdollSpawned = 0;
    scene->slugProp = -1;
    scene->contentEpoch++;
    gX_PhysicsEM_Reset();
    gX_PhysicsRelativity_Reset();
    gX_PhysicsThermal_Reset();
    //ai: water content (volume model + buoys) rebuilds per entry; the basin
    //   statics persist like the ground.
    gX_PhysicsFluid_Reset();
    scene->waterModel = -1;
    scene->buoysSpawned = 0;
}

//ai: spring drag — pull the held prop toward the mouse target with a
//   velocity controller so it still collides while dragged (a teleport
//   shoves stacks apart). Runs per fixed sub-step; the release keeps the
//   last velocity, which reads as a throw.
static gX_I0 gX_ScenePhysics_DragApply(gX_ScenePhysics *scene) {
    if (!scene->dragActive) return;
    gX_PhysicsBody body = gX_PHYSICS_BODY_NULL;
    if (scene->dragProp >= 0 && scene->dragProp < scene->propCount) {
        body = scene->props[scene->dragProp].body;
    } else if (scene->dragLink >= 0 && scene->dragLink < scene->linkCount) {
        body = scene->links[scene->dragLink].body;
    }
    if (body.internal == 0) {
        scene->dragActive = 0;
        return;
    }
    gX_F32v3 pos = gX_PhysicsBody_GetPosition(body);
    gX_F32v3 vel = gX_F32v3_MulScalar(
        gX_F32v3_Sub(scene->dragTarget, pos), GX_SCENE_PHYSICS_DRAG_GAIN);
    gX_F32 speedSqr = gX_F32v3_LengthSqr(vel);
    gX_F32 maxSqr = GX_SCENE_PHYSICS_DRAG_MAX_SPEED * GX_SCENE_PHYSICS_DRAG_MAX_SPEED;
    if (speedSqr > maxSqr) {
        vel = gX_F32v3_MulScalar(vel,
            GX_SCENE_PHYSICS_DRAG_MAX_SPEED / sqrtf(speedSqr));
    }
    gX_PhysicsBody_SetLinearVel(body, vel);
    gX_F32v3 spin = gX_PhysicsBody_GetAngularVel(body);
    gX_PhysicsBody_SetAngularVel(body,
        gX_F32v3_MulScalar(spin, GX_SCENE_PHYSICS_DRAG_SPIN_DAMP));
    gX_PhysicsBody_Enable(body);
}

//ai: capture pre-step poses for render interpolation. The hook runs before
//   every fixed step, so prev/current bracket exactly one step and the
//   module's alpha (accumulator fraction) blends between them per frame.
static gX_I0 gX_ScenePhysics_Snapshot(gX_ScenePhysics *scene) {
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0) continue;
        prop->prevPos = gX_PhysicsBody_GetPosition(prop->body);
        gX_PhysicsBody_GetRotation(prop->body, &prop->prevQuat);
    }
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        gX_ScenePhysics_Link *link = &scene->links[i];
        if (link->body.internal == 0) continue;
        link->prevPos = gX_PhysicsBody_GetPosition(link->body);
        gX_PhysicsBody_GetRotation(link->body, &link->prevQuat);
    }
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal == 0) continue;
        mover->prevPos = gX_PhysicsBody_GetPosition(mover->body);
        gX_PhysicsBody_GetRotation(mover->body, &mover->prevQuat);
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            gX_PhysicsBody body = slot->ragdoll.bones[b].body;
            if (body.internal == 0) continue;
            slot->bonePrevPos[b] = gX_PhysicsBody_GetPosition(body);
            gX_PhysicsBody_GetRotation(body, &slot->bonePrevQuat[b]);
        }
    }
}

//ai: EM forces must land once per fixed sub-step, so they run from the
//   physics module's step hook rather than the per-frame scene update;
//   thermal cooling integrates over the same step dt.
static gX_I0 gX_ScenePhysics_StepHook(gX_I0 *userData, gX_F32 dt) {
    gX_ScenePhysics *scene = (gX_ScenePhysics *)userData;
    if (scene) gX_ScenePhysics_DragApply(scene);
    if (scene) gX_ScenePhysics_DriveMovers(scene, dt);
    if (scene) gX_ScenePhysics_DrivePump(scene, dt);
    if (scene) gX_ScenePhysics_Snapshot(scene);
    gX_PhysicsEM_Apply();
    gX_PhysicsThermal_Apply(dt);
    gX_PhysicsFluid_Apply();
    gX_PhysicsRelativity_Apply();
    if (scene) gX_ScenePhysics_UpdateBreakTarget(scene);
}

//ai: spark tint — a scene prop's current tint, else the owning ragdoll
//   limb's palette color, else a warm default (static colliders).
static gX_I1 gX_ScenePhysics_FindTint(gX_ScenePhysics *scene,
    gX_PhysicsBody body, gX_Color *out) {
    if (body.internal == 0) return 0;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal == body.internal) {
            *out = gX_ScenePhysics_PropTint(scene, i);
            return 1;
        }
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            if (slot->ragdoll.bones[b].body.internal == body.internal) {
                *out = gX_ScenePhysics_Tint(
                    (gX_I32)slot->ragdoll.bones[b].colorIndex);
                return 1;
            }
        }
    }
    return 0;
}

static gX_Color gX_ScenePhysics_ImpactTint(gX_ScenePhysics *scene,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB) {
    gX_Color tint = {255, 214, 150, 255};
    if (gX_ScenePhysics_FindTint(scene, bodyA, &tint)) return tint;
    if (gX_ScenePhysics_FindTint(scene, bodyB, &tint)) return tint;
    return tint;
}

//ai: the EM charge a body carries under the current mode (props alternate
//   signs by slot; ragdoll bones share their slot sign), scaled for bones.
//   Zero while EM is off, so sparks only read as arcs when charges are live.
static gX_F32 gX_ScenePhysics_BodyCharge(gX_ScenePhysics *scene,
    gX_PhysicsBody body) {
    if (!scene->emEnabled || body.internal == 0) return 0.0f;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal == body.internal) {
            return gX_ScenePhysics_PropCharge(i);
        }
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            if (slot->ragdoll.bones[b].body.internal == body.internal) {
                return slot->charge * GX_SCENE_PHYSICS_RAGDOLL_CHARGE;
            }
        }
    }
    return 0.0f;
}

//ai: contact-event capture — one ENTER event per pair transition, so a
//   bounce re-arms and a resting stack does not re-fire. Impacts above the
//   closing-speed floor are queued with their averaged contact point,
//   reduced-mass energy, thermal state and tint; the queue drops the oldest
//   entry when full. Runs during the step: records only (no GL/heap work).
//ai: impact-energy mass — ragdoll bones report their nominal mass so the
//   spark/impact FX keep the pre-rescale energies while the rigid body runs
//   at human weight.
static gX_F32 gX_ScenePhysics_BodyImpactMass(gX_ScenePhysics *scene,
    gX_PhysicsBody body) {
    if (body.internal == 0) return 0.0f;
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            if (slot->ragdoll.bones[b].body.internal == body.internal) {
                return slot->ragdoll.bones[b].mass;
            }
        }
    }
    return gX_PhysicsBody_GetMass(body);
}

static gX_I0 gX_ScenePhysics_ImpactEventCB_(gX_I0 *userData,
    const gX_PhysicsContactEvent *event) {
    gX_ScenePhysics *scene = (gX_ScenePhysics *)userData;
    if (!scene || !event) return;

    //ai: debug view — tag the first contact of every event with the physics
    //   frame so the overlay can draw the current frame's contact points.
    if (scene->debugDraw && event->contactCount > 0 && event->contacts) {
        gX_ScenePhysics_Contact *contact = &scene->debugContacts[
            scene->debugContactWrite % GX_SCENE_PHYSICS_DEBUG_CONTACTS];
        scene->debugContactWrite++;
        contact->pos = event->contacts[0].position;
        contact->normal = event->contacts[0].normal;
        contact->frame = physics.frameCount;
    }

    if (event->phase != gX_PHYSICS_PHASE_ENTER) return;
    if (event->contactCount == 0 || !event->contacts) return;

    gX_F32v3 velA = {0.0f, 0.0f, 0.0f};
    gX_F32v3 velB = {0.0f, 0.0f, 0.0f};
    if (event->bodyA.internal != 0) {
        velA = gX_PhysicsBody_GetLinearVel(event->bodyA);
    }
    if (event->bodyB.internal != 0) {
        velB = gX_PhysicsBody_GetLinearVel(event->bodyB);
    }
    gX_F32v3 normal = event->contacts[0].normal;
    gX_F32 relX = velA.x - velB.x;
    gX_F32 relY = velA.y - velB.y;
    gX_F32 relZ = velA.z - velB.z;
    gX_F32 speed = relX * normal.x + relY * normal.y + relZ * normal.z;
    if (speed < 0.0f) speed = -speed;
    if (speed < GX_SCENE_PHYSICS_SPARK_MIN_SPEED) return;

    gX_F32 massA = gX_ScenePhysics_BodyImpactMass(scene, event->bodyA);
    gX_F32 massB = gX_ScenePhysics_BodyImpactMass(scene, event->bodyB);
    gX_F32 reduced;
    if (massA > 0.0f && massB > 0.0f) reduced = (massA * massB) / (massA + massB);
    else if (massA > 0.0f) reduced = massA;
    else reduced = massB;

    if (scene->impactCount >= GX_SCENE_PHYSICS_MAX_IMPACTS) {
        scene->impactHead = (scene->impactHead + 1) % GX_SCENE_PHYSICS_MAX_IMPACTS;
        scene->impactCount--;
    }
    gX_ScenePhysics_Impact *impact = &scene->impacts[
        (scene->impactHead + scene->impactCount) % GX_SCENE_PHYSICS_MAX_IMPACTS];
    scene->impactCount++;

    //ai: center the burst on up to four contact points.
    gX_F32v3 position = {0.0f, 0.0f, 0.0f};
    gX_U32 used = event->contactCount < 4 ? event->contactCount : 4;
    for (gX_U32 i = 0; i < used; i++) {
        position.x += event->contacts[i].position.x;
        position.y += event->contacts[i].position.y;
        position.z += event->contacts[i].position.z;
    }
    position = gX_F32v3_MulScalar(position, 1.0f / (gX_F32)used);

    impact->position = position;
    impact->normal = normal;
    impact->energy = 0.5f * reduced * speed * speed;
    gX_F32 tempA = gX_PhysicsThermal_GetTemperature(event->bodyA);
    gX_F32 tempB = gX_PhysicsThermal_GetTemperature(event->bodyB);
    impact->temperature = tempA > tempB ? tempA : tempB;
    //ai: any charged side flags the arc; the magnitudes cancel for opposite
    //   pairs, so keep the larger-|.| charge rather than a sum.
    gX_F32 chargeA = gX_ScenePhysics_BodyCharge(scene, event->bodyA);
    gX_F32 chargeB = gX_ScenePhysics_BodyCharge(scene, event->bodyB);
    impact->charge = (fabsf(chargeA) >= fabsf(chargeB)) ? chargeA : chargeB;
    impact->tint = gX_ScenePhysics_ImpactTint(scene, event->bodyA, event->bodyB);
}

gX_I0 gX_ScenePhysics_EnableImpactCapture(gX_ScenePhysics *scene) {
    scene->impactHead = 0;
    scene->impactCount = 0;
    scene->sparksEnabled = 1;
    scene->impactCapture = 1;
    if (physics.world.internal != 0) {
        gX_PhysicsWorld_SetEventCallback(physics.world,
            gX_ScenePhysics_ImpactEventCB_, scene);
    }
}

gX_I0 gX_ScenePhysics_DisableImpactCapture(gX_ScenePhysics *scene) {
    if (physics.world.internal != 0) {
        gX_PhysicsWorld_SetEventCallback(physics.world, NULL, NULL);
    }
    scene->impactHead = 0;
    scene->impactCount = 0;
    scene->sparksEnabled = 0;
    scene->impactCapture = 0;
}

gX_I32 gX_ScenePhysics_ImpactCount(gX_ScenePhysics *scene) {
    return scene->impactCount;
}

gX_I1 gX_ScenePhysics_ImpactGet(gX_ScenePhysics *scene, gX_I32 index,
    gX_ScenePhysics_Impact *out) {
    if (!scene || !out || index < 0 || index >= scene->impactCount) return 0;
    *out = scene->impacts[(scene->impactHead + index) % GX_SCENE_PHYSICS_MAX_IMPACTS];
    return 1;
}

gX_I0 gX_ScenePhysics_SetSFXEnabled(gX_ScenePhysics *scene, gX_I1 enabled) {
    if (!scene) return;
    scene->sfxEnabled = enabled ? 1 : 0;
    //ai: sound shares the impact capture with sparks/decals/the debug view —
    //   install or remove the callback only when the combined demand flips.
    gX_I1 wantCapture = (scene->sfxEnabled || scene->sparksEnabled
        || scene->decalsEnabled || scene->debugDraw) ? 1 : 0;
    if (wantCapture && !scene->impactCapture) {
        scene->impactCapture = 1;
        if (physics.world.internal != 0) {
            gX_PhysicsWorld_SetEventCallback(physics.world,
                gX_ScenePhysics_ImpactEventCB_, scene);
        }
    } else if (!wantCapture && scene->impactCapture) {
        gX_ScenePhysics_DisableImpactCapture(scene);
    }
}

gX_I1 gX_ScenePhysics_IsSFXEnabled(gX_ScenePhysics *scene) {
    return (scene && scene->sfxEnabled) ? 1 : 0;
}

//ai: impact → SFX palette/level mapping (see physics.h). Pure: the tests pin
//   the decisions without an audio device. Pitch jitter is derived from the
//   contact position, so a capture replay repeats the same mix.
gX_I0 gX_ScenePhysics_ImpactSoundMap(const gX_ScenePhysics_Impact *impact,
    gX_I1 underwater, gX_ScenePhysics_ImpactSound *out) {
    if (!out) return;
    out->variant = GX_AUDIO_SFX_THUD;
    out->volume = 0.0f;
    out->pitch = 1.0f;
    if (!impact) return;

    //ai: heavy hits read low and loud — kick is the spark pass's own
    //   energy->intensity curve, reused so FX and audio agree.
    gX_F32 kick = impact->energy / (impact->energy + 90.0f);
    if (kick < 0.0f) kick = 0.0f;
    if (kick > 1.0f) kick = 1.0f;

    gX_F32 volume = 0.22f + 0.55f * kick;
    gX_F32 pitch = 1.12f - 0.30f * kick;

    //ai: position-derived jitter keeps repeated contacts from sounding
    //   identical without a nondeterministic RNG.
    gX_I32 jitter = (gX_I32)(impact->position.x * 73.1f
        + impact->position.y * 131.7f + impact->position.z * 197.3f);
    jitter = jitter % 9;
    if (jitter < 0) jitter += 9;
    pitch += ((gX_F32)jitter - 4.0f) * 0.008f;

    if (impact->charge != 0.0f) {
        out->variant = GX_AUDIO_SFX_ARC;
        pitch = 1.0f + 0.20f * kick;
    } else if (underwater) {
        out->variant = GX_AUDIO_SFX_THUD;
        pitch *= 0.72f;
        volume *= 0.75f;
    } else if (impact->energy < 45.0f) {
        out->variant = GX_AUDIO_SFX_THUD;
    } else {
        //ai: metal-ish read — a desaturated bright tint (the default metal
        //   albedo ballpark) or a hot body rings; everything else clacks.
        gX_I32 r = impact->tint.r, g = impact->tint.g, b = impact->tint.b;
        gX_I32 lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        gX_I32 hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
        gX_I32 lum = (r * 30 + g * 59 + b * 11) / 100;
        gX_I1 metalTint = (hi - lo) < 26 && lum > 110 && lum < 235;
        if (metalTint || impact->temperature > 700.0f) {
            out->variant = GX_AUDIO_SFX_METAL;
            pitch = 1.0f + 0.12f * (1.0f - kick);
        } else {
            out->variant = GX_AUDIO_SFX_CLACK;
        }
    }

    if (pitch < 0.35f) pitch = 0.35f;
    if (pitch > 2.2f) pitch = 2.2f;
    if (volume < 0.05f) volume = 0.05f;
    if (volume > 0.9f) volume = 0.9f;
    out->volume = volume;
    out->pitch = pitch;
}

//ai: reserve spark emitters in the GFX particle heap (dormant until a burst
//   activates them) and switch the particle pass on for the sandbox. Any
//   pre-existing emitter (the renderer's default fountain) is silenced —
//   its active flag is saved for SparksRemove to restore.
static gX_I0 gX_ScenePhysics_SparksSetup(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx) {
    scene->sparkEmitterCount = 0;
    scene->sparkNext = 0;
    gfx->state.ENABLE_PARTICLES = 1;

    scene->sparkSilencedCount = 0;
    gX_I32 existing = (gX_I32)gfx->particle.emitters.amount;
    for (gX_I32 i = 0;
        i < existing && scene->sparkSilencedCount < GX_SCENE_PHYSICS_SILENCED_MAX;
        i++) {
        gX_RendererGFX_Emitter *emitter =
            gX_Heap_SeekPtr_Index(&gfx->particle.emitters, (gX_U32)i);
        if (!emitter) continue;
        scene->sparkSilenced[scene->sparkSilencedCount] = emitter->active;
        scene->sparkSilencedCount++;
        emitter->active = 0;
    }
    for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_SPARK_EMITTERS; i++) {
        scene->sparkEmitter[i] = -1;
        scene->sparkTimer[i] = 0.0f;
        gX_I32 index = gX_RendererGFX_Emitter_Add(gfx);
        if (index < 0) break;
        gX_RendererGFX_Emitter *emitter =
            gX_Heap_SeekPtr_Index(&gfx->particle.emitters, (gX_U32)index);
        if (!emitter) break;
        emitter->active = 0;
        emitter->emitRate = 0.0f;
        emitter->life = GX_SCENE_PHYSICS_SPARK_LIFE;
        emitter->speed = 2.0f;
        emitter->spread = 70.0f;
        emitter->color = (gX_F32v4){1.0f, 0.8f, 0.4f, 1.0f};
        scene->sparkEmitter[scene->sparkEmitterCount] = index;
        scene->sparkEmitterCount++;
    }
    if (scene->sparkEmitterCount < GX_SCENE_PHYSICS_SPARK_EMITTERS) {
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: %d/%d spark emitters",
            scene->sparkEmitterCount, GX_SCENE_PHYSICS_SPARK_EMITTERS);
    }
}

//ai: release the reserved emitters — highest index first, because heap
//   removal shifts later entries — and hand the particle pass back off.
static gX_I0 gX_ScenePhysics_SparksRemove(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx) {
    for (gX_I32 i = scene->sparkEmitterCount - 1; i >= 0; i--) {
        if (scene->sparkEmitter[i] >= 0) {
            gX_RendererGFX_Emitter_Remove(gfx, (gX_U32)scene->sparkEmitter[i]);
        }
        scene->sparkEmitter[i] = -1;
        scene->sparkTimer[i] = 0.0f;
    }
    scene->sparkEmitterCount = 0;
    //ai: restore the silenced pre-existing emitters (our removals happened
    //   above them in the heap, so their indices are unchanged).
    for (gX_I32 i = 0; i < scene->sparkSilencedCount; i++) {
        gX_RendererGFX_Emitter *emitter =
            gX_Heap_SeekPtr_Index(&gfx->particle.emitters, (gX_U32)i);
        if (!emitter) continue;
        emitter->active = scene->sparkSilenced[i];
        scene->sparkSilenced[i] = 0;
    }
    scene->sparkSilencedCount = 0;
    gfx->state.ENABLE_PARTICLES = 0;
}

//ai: one reserved-emitter burst (round-robin); impacts and water splashes
//   share the emitter pool.
static gX_I0 gX_ScenePhysics_EmitBurst(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32v3 baseVel, gX_F32v3 color,
    gX_F32 spread, gX_F32 life, gX_F32 speed, gX_F32 emitRate) {
    if (scene->sparkEmitterCount == 0) return;
    gX_I32 slot = scene->sparkNext % scene->sparkEmitterCount;
    scene->sparkNext = (scene->sparkNext + 1) % scene->sparkEmitterCount;
    gX_RendererGFX_Emitter *emitter = gX_Heap_SeekPtr_Index(
        &gfx->particle.emitters, (gX_U32)scene->sparkEmitter[slot]);
    if (!emitter) return;
    emitter->active = 1;
    emitter->pos = pos;
    emitter->baseVel = baseVel;
    emitter->spread = spread;
    emitter->color = (gX_F32v4){color.x, color.y, color.z, 1.0f};
    emitter->life = life;
    emitter->speed = speed;
    emitter->emitRate = emitRate;
    scene->sparkTimer[slot] = GX_SCENE_PHYSICS_SPARK_LIFE;
}

//ai: is a point inside the pool water? Underwater contacts neither spark nor
//   scorch — the basin stays clean and the entry events feed the splashes.
static gX_I1 gX_ScenePhysics_Underwater(gX_ScenePhysics *scene, gX_F32v3 pos) {
    if (!scene->waterEnabled) return 0;
    gX_F32 half = GX_SCENE_PHYSICS_POOL_SIZE * 0.5f;
    return pos.y < GX_SCENE_PHYSICS_POOL_SURFACE
        && pos.x > GX_SCENE_PHYSICS_POOL_CENTER_X - half
        && pos.x < GX_SCENE_PHYSICS_POOL_CENTER_X + half
        && pos.z > GX_SCENE_PHYSICS_POOL_CENTER_Z - half
        && pos.z < GX_SCENE_PHYSICS_POOL_CENTER_Z + half;
}

//ai: per-frame impact FX — expire spark bursts, then drain the queued
//   impacts into the spark emitters (when sparks are live) and scorch decals
//   (ground impacts at/above the energy floor). Sparks map energy to burst
//   rate/speed/life and blend tint/blackbody/charge into the color; decals
//   size with energy, warm with heat and fade with the texture's alpha.
//   Fluid entry events drain into white-blue splash bursts alongside.
static gX_I0 gX_ScenePhysics_ImpactUpdate(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx) {
    if (!scene->sparksEnabled && !scene->decalsEnabled && !scene->sfxEnabled) {
        scene->impactCount = 0;
        scene->impactHead = 0;
        gX_PhysicsFluid_EventClear();
        return;
    }
    gX_F32 dt = (gX_F32)gX_FrameTime_Get();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    if (scene->sparksEnabled) {
        for (gX_I32 i = 0; i < scene->sparkEmitterCount; i++) {
            if (scene->sparkTimer[i] <= 0.0f) continue;
            scene->sparkTimer[i] -= dt;
            if (scene->sparkTimer[i] <= 0.0f) {
                gX_RendererGFX_Emitter *emitter = gX_Heap_SeekPtr_Index(
                    &gfx->particle.emitters, (gX_U32)scene->sparkEmitter[i]);
                if (emitter) emitter->active = 0;
            }
        }
    }

    while (scene->impactCount > 0) {
        gX_ScenePhysics_Impact impact = scene->impacts[scene->impactHead];
        scene->impactHead = (scene->impactHead + 1) % GX_SCENE_PHYSICS_MAX_IMPACTS;
        scene->impactCount--;

        //ai: impact audio — drain through the procedural SFX pool before the
        //   visual branches, so water entries and no-spark runs still sound;
        //   the engine pools/rate-limits the voices.
        gX_I1 underwater = gX_ScenePhysics_Underwater(scene, impact.position);
        if (scene->sfxEnabled) {
            gX_ScenePhysics_ImpactSound sound;
            gX_ScenePhysics_ImpactSoundMap(&impact, underwater, &sound);
            gX_AudioSFX_Play(sound.variant, impact.position, sound.volume,
                sound.pitch, GX_SCENE_PHYSICS_SFX_MAX_DIST);
        }

        //ai: underwater contacts neither spark nor scorch — the entries
        //   above turned into splash bursts instead.
        if (underwater) continue;

        gX_F32 kick = impact.energy / (impact.energy + 90.0f);
        gX_F32 heat = (impact.temperature - 400.0f) / 1600.0f;
        if (heat < 0.0f) heat = 0.0f;
        if (heat > 1.0f) heat = 1.0f;

        //ai: scorch decal — ground-facing hits above the energy floor.
        if (scene->decalsEnabled && scene->scorchTexture >= 0
            && impact.normal.y > 0.7f
            && impact.energy >= GX_SCENE_PHYSICS_DECAL_MIN_ENERGY) {
            gX_F32v3 at = {
                impact.position.x + impact.normal.x * 0.05f,
                impact.position.y + impact.normal.y * 0.05f,
                impact.position.z + impact.normal.z * 0.05f
            };
            //ai: contacts can sit slightly below the surface (penetration);
            //   keep a ground mark above the floor so it is not buried.
            if (impact.normal.y > 0.7f && at.y < 0.03f) at.y = 0.03f;
            gX_I32 di = gX_RendererGFX_Decal_Add(gfx, at, impact.normal,
                0.55f + 1.0f * kick, scene->scorchTexture,
                GX_SCENE_PHYSICS_DECAL_LIFE);
            if (di >= 0) {
                gX_RendererGFX_Decal *decal = gX_Heap_SeekPtr_Index(
                    &gfx->decals, (gX_U32)di);
                if (decal) {
                    //ai: hotter impacts tint the char warm; alpha keeps the
                    //   floor readable through the mark.
                    if (heat > 0.05f) {
                        gX_F32v3 ember = {1.0f, 0.72f, 0.5f};
                        gX_F32v3 tint = gX_F32v3_Lerp(
                            (gX_F32v3){1.0f, 1.0f, 1.0f}, ember, heat);
                        decal->color = (gX_Color){
                            (gX_U8)(tint.x * 255.0f),
                            (gX_U8)(tint.y * 255.0f),
                            (gX_U8)(tint.z * 255.0f), 255
                        };
                    }
                    decal->alpha = 0.65f + 0.25f * kick;
                }
            }
        }

        if (!scene->sparksEnabled || scene->sparkEmitterCount == 0) continue;

        gX_F32v3 color;
        gX_F32 gain;
        if (impact.charge != 0.0f) {
            //ai: charged impacts read as electric arcs — saturated cyan,
            //   whitening only with thermal state; no ember spike/gain so
            //   the hue survives.
            gX_F32v3 arc = {0.25f, 0.65f, 1.0f};
            gX_F32v3 arcHot = {0.55f, 0.85f, 1.0f};
            color = gX_F32v3_Lerp(arc, arcHot, heat);
            gain = 1.0f + 0.25f * kick;
        } else {
            gX_F32v3 warm = {1.0f, 0.78f, 0.38f};
            gX_F32v3 hot = gX_F32v3_Physics_Temp_BlackbodyRGB(
                impact.temperature > 900.0f ? impact.temperature : 900.0f);
            color = gX_F32v3_Lerp(warm, hot, heat);
            gX_F32v3 tint = {
                impact.tint.r / 255.0f,
                impact.tint.g / 255.0f,
                impact.tint.b / 255.0f
            };
            color = gX_F32v3_Lerp(color, tint, 0.25f * (1.0f - heat));
            //ai: heavy fresh hits flash white; energy scales the ember gain.
            gX_F32 spike = kick > 0.5f ? (kick - 0.5f) * 2.0f : 0.0f;
            color = gX_F32v3_Lerp(color, (gX_F32v3){1.0f, 1.0f, 1.0f},
                0.45f * spike);
            gain = 0.85f + 0.5f * kick;
        }
        color = gX_F32v3_MulScalar(color, gain);
        if (color.x > 1.0f) color.x = 1.0f;
        if (color.y > 1.0f) color.y = 1.0f;
        if (color.z > 1.0f) color.z = 1.0f;

        gX_ScenePhysics_EmitBurst(scene, gfx, impact.position,
            (gX_F32v3){impact.normal.x, impact.normal.y + 0.8f,
                impact.normal.z},
            color, (impact.charge != 0.0f) ? 45.0f : 70.0f,
            0.25f + 0.3f * kick,
            (impact.charge != 0.0f ? 3.0f : 2.0f) + 5.0f * kick,
            80.0f + 220.0f * kick);
    }

    //ai: splash bursts — one white-blue spike per surface entry, at the
    //   crossing point; the queue drains every frame.
    gX_I32 splashCount = gX_PhysicsFluid_EventCount();
    for (gX_I32 i = 0; i < splashCount; i++) {
        gX_PhysicsFluid_Event event;
        if (!gX_PhysicsFluid_EventGet(i, &event)) continue;
        if (!scene->sparksEnabled) continue;
        gX_F32 kick = event.speed / (event.speed + 4.0f);
        gX_F32 gain = 0.9f + 0.5f * kick;
        gX_F32v3 color = gX_F32v3_MulScalar(
            (gX_F32v3){0.62f, 0.82f, 1.0f}, gain);
        if (color.x > 1.0f) color.x = 1.0f;
        if (color.y > 1.0f) color.y = 1.0f;
        if (color.z > 1.0f) color.z = 1.0f;
        gX_ScenePhysics_EmitBurst(scene, gfx, event.position,
            (gX_F32v3){0.0f, 0.9f, 0.0f}, color, 85.0f,
            0.25f + 0.3f * kick, 2.0f + 4.0f * kick,
            60.0f + 160.0f * kick);
    }
    if (splashCount > 0) gX_PhysicsFluid_EventClear();
}

//ai: repaint live props for the current mode (element palette vs charge sign).
//   Emissive is owned by the heat sync, so only the albedo tint changes here.
static gX_I0 gX_ScenePhysics_RecolorProps(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx) {
    if (!gfx->models.data || !gfx->models.meta) return;
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model *)gfx->models.data;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0 || prop->modelIndex < 0) continue;
        if ((gX_U32)prop->modelIndex >= gfx->models.amount
            || !gfx->models.meta[prop->modelIndex].USED) continue;
        models[prop->modelIndex].params.albedoTint = gX_ScenePhysics_PropTint(scene, i);
    }
}

//ai: temperature -> emissive glow and molten material state. Cold props keep
//   the small base emissive (mode tint); heated ones ramp blackbody color
//   and intensity into the renderer's emissive path, where bloom picks the
//   glow up. Molten props get a dark, low-roughness surface and a glow
//   floor (molten metal glows below the ramp's white-hot band).
static gX_I0 gX_ScenePhysics_SyncGlow(gX_ScenePhysics *scene,
    gX_RendererGFX_Model *models) {
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0 || prop->modelIndex < 0) continue;
        gX_RendererGFX_Model *model = &models[prop->modelIndex];
        gX_F32 temperature = gX_PhysicsThermal_GetTemperature(prop->body);
        gX_I1 molten = gX_PhysicsThermal_IsMolten(prop->body);
        gX_F32 intensity = (temperature - GX_SCENE_PHYSICS_GLOW_START)
            / (GX_SCENE_PHYSICS_GLOW_FULL - GX_SCENE_PHYSICS_GLOW_START);
        if (intensity < 0.0f) intensity = 0.0f;
        if (intensity > 1.0f) intensity = 1.0f;

        if (molten != prop->molten) {
            prop->molten = molten;
            if (molten) {
                model->params.metalness = 0.6f;
                model->params.roughness = 0.12f;
                gX_Color tint = gX_ScenePhysics_PropTint(scene, i);
                model->params.albedoTint = (gX_Color){
                    (gX_U8)(tint.r / 2), (gX_U8)(tint.g / 2),
                    (gX_U8)(tint.b / 2), 255
                };
            } else {
                const gX_Element_Info *info = gX_Elements_Get(prop->element);
                if (info) {
                    model->params.metalness = info->metalness;
                    model->params.roughness = info->roughness;
                } else {
                    model->params.metalness =
                        (prop->kind == gX_SCENE_PHYSICS_PROP_SPHERE) ? 0.75f
                        : ((prop->kind == gX_SCENE_PHYSICS_PROP_CYLINDER)
                            ? 0.45f : 0.18f);
                    model->params.roughness =
                        (prop->kind == gX_SCENE_PHYSICS_PROP_SPHERE) ? 0.25f
                        : ((prop->kind == gX_SCENE_PHYSICS_PROP_CYLINDER)
                            ? 0.45f : 0.62f);
                }
                model->params.albedoTint = gX_ScenePhysics_PropTint(scene, i);
            }
        }
        if (molten && intensity < 0.35f) intensity = 0.35f;

        if (intensity > 0.0f) {
            gX_F32v3 glow = gX_F32v3_Physics_Temp_BlackbodyRGB(temperature);
            model->params.emissiveColor = (gX_Color){
                (gX_U8)(glow.x * 255.0f),
                (gX_U8)(glow.y * 255.0f),
                (gX_U8)(glow.z * 255.0f), 255
            };
            model->params.emissiveFactor = 0.05f + 0.95f * intensity;
        } else {
            model->params.emissiveColor = gX_ScenePhysics_PropTint(scene, i);
            model->params.emissiveFactor = 0.05f;
        }
    }
}

//ai: toggle EM — rebuild the charge registry from the live props, keep the
//   B field, recolor, and let the step hook do the per-step work.
static gX_I0 gX_ScenePhysics_SetEM(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_I1 enabled) {
    scene->emEnabled = enabled ? 1 : 0;
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetUniformB(scene->emB);
    //ai: the reset wipes the registry — re-register the magnet moments and
    //   element susceptibilities, then let the shared toggle set the
    //   scale/enabled state (charges + magnets).
    gX_ScenePhysics_RegisterEMExtras(scene);
    gX_ScenePhysics_SetMagnets(scene, scene->magnetsOn);
    if (scene->emEnabled) {
        for (gX_I32 i = 0; i < scene->propCount; i++) {
            //ai: buoys and bar magnets are not charge carriers.
            if (scene->props[i].body.internal == 0) continue;
            if (scene->props[i].element == gX_ELEMENT_NONE) continue;
            gX_PhysicsEM_AddBody(scene->props[i].body, gX_ScenePhysics_PropCharge(i));
        }
        for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
            gX_ScenePhysics_RagdollCharge(scene, &scene->ragdolls[i]);
        }
    }
    gX_ScenePhysics_RecolorProps(scene, gfx);
    gX_TraceLog(gX_LOG_NOTICE,
        "PHYSICS_SCENE: EM %s (%d charges, %d magnets)",
        scene->emEnabled ? "on" : "off", gX_PhysicsEM_Count(),
        scene->magnetsOn ? GX_SCENE_PHYSICS_MAGNET_COUNT : 0);
}

//ai: one mover payload — a crate spawned resting on a mover top (the lift
//   for even indices, the turntable for odd); it rides the surface and R
//   resets it back onto the mover (spawnPos). Grippy like the mover top
//   (min-rule contact) and heavier than the grid crates so the oscillating
//   ride does not walk it off the edge.
gX_I32 gX_ScenePhysics_SpawnPayload(gX_ScenePhysics *scene, gX_I32 index) {
    if (!scene->moversEnabled || scene->moverCount == 0) return -1;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_I1 onLift = (index % 2) == 0;
    gX_F32 size = GX_SCENE_PHYSICS_PAYLOAD_SIZE;
    gX_F32v3 pos = {
        onLift ? GX_SCENE_PHYSICS_LIFT_X : GX_SCENE_PHYSICS_TABLE_X,
        (onLift ? GX_SCENE_PHYSICS_LIFT_THICK : GX_SCENE_PHYSICS_TABLE_THICK)
            + size * 0.5f + 0.002f,
        onLift ? GX_SCENE_PHYSICS_LIFT_Z : GX_SCENE_PHYSICS_TABLE_Z
    };
    gX_I32 result = gX_ScenePhysics_SpawnPropEx(scene, gfx, pos,
        gX_SCENE_PHYSICS_PROP_BOX, (gX_F32v3){size, size, size},
        2.0f, gX_ELEMENT_NONE, (gX_Color){216, 138, 78, 255});
    if (result != 0 || scene->propCount == 0) return result;
    gX_PhysicsBody_SetMaterial(scene->props[scene->propCount - 1].body,
        (gX_PhysicsMaterial){0.9f, 0.0f, 0.0f, 0.0f});
    gX_I32 modelIndex = scene->props[scene->propCount - 1].modelIndex;
    if (gfx && modelIndex >= 0) {
        gX_RendererGFX_Model *model =
            &((gX_RendererGFX_Model *)gfx->models.data)[modelIndex];
        model->params.metalness = 0.05f;
        model->params.roughness = 0.5f;
        model->params.emissiveFactor = 0.22f;
    }
    return result;
}

//ai: balance posts — three identical planks whose only difference is the
//   mass center (same shape, same total mass), so the outcome reads as a
//   pure center-of-mass effect. They use prop slots (drag/delete/R work);
//   B kicks all three and the mass center decides.
gX_I0 gX_ScenePhysics_BuildBalancePosts(gX_ScenePhysics *scene) {
    if (!scene->balanceEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_Color tints[GX_SCENE_PHYSICS_BALANCE_COUNT] = {
        {216, 96, 80, 255},
        {206, 208, 204, 255},
        {96, 168, 216, 255}
    };
    gX_F32v3 size = {GX_SCENE_PHYSICS_BALANCE_WIDTH,
        GX_SCENE_PHYSICS_BALANCE_HEIGHT, GX_SCENE_PHYSICS_BALANCE_WIDTH};
    for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_BALANCE_COUNT; i++) {
        gX_F32v3 pos = {
            GX_SCENE_PHYSICS_BALANCE_X
                + GX_SCENE_PHYSICS_BALANCE_GAP * (gX_F32)i,
            GX_SCENE_PHYSICS_BALANCE_HEIGHT * 0.5f + 0.002f,
            GX_SCENE_PHYSICS_BALANCE_Z
        };
        if (gX_ScenePhysics_SpawnPropEx(scene, gfx, pos,
                gX_SCENE_PHYSICS_PROP_BOX, size, 1.0f, gX_ELEMENT_NONE,
                tints[i]) != 0) {
            break;
        }
        gX_I32 slot = scene->propCount - 1;
        gX_PhysicsBody body = scene->props[slot].body;
        gX_PhysicsBody_SetMass(body, GX_SCENE_PHYSICS_BALANCE_MASS);
        gX_F32 offset = (i == 0) ? GX_SCENE_PHYSICS_BALANCE_OFFSET
            : ((i == 2) ? -GX_SCENE_PHYSICS_BALANCE_OFFSET : 0.0f);
        gX_PhysicsBody_SetMassCenter(body, (gX_F32v3){0.0f, offset, 0.0f});
        //ai: SetMassCenter moves the body origin onto the balance point
        //   (the shape keeps its world pose) — resnapshot so R and the
        //   interpolation address the new origin.
        scene->props[slot].spawnPos = gX_PhysicsBody_GetPosition(body);
        scene->props[slot].prevPos = scene->props[slot].spawnPos;
        //ai: non-metal, self-lit look so the distant posts read while
        //   standing and while fallen.
        gX_I32 modelIndex = scene->props[slot].modelIndex;
        if (gfx && modelIndex >= 0) {
            gX_RendererGFX_Model *model =
                &((gX_RendererGFX_Model *)gfx->models.data)[modelIndex];
            model->params.metalness = 0.05f;
            model->params.roughness = 0.5f;
            model->params.emissiveFactor = 0.22f;
        }
        scene->balance[scene->balanceCount++] = slot;
    }
}

//ai: B — the balance test: re-pose all three posts at the same tilted
//   release (pivot edge on the ground, zero velocity); the mass center
//   decides who falls, who stands and who rocks back. Re-pressing B
//   re-poses them, so the test repeats after a fall.
gX_I0 gX_ScenePhysics_NudgeBalance(gX_ScenePhysics *scene) {
    if (physics.world.internal == 0) return;
    gX_F32 halfH = GX_SCENE_PHYSICS_BALANCE_HEIGHT * 0.5f;
    gX_F32 halfW = GX_SCENE_PHYSICS_BALANCE_WIDTH * 0.5f;
    gX_F32 tilt = GX_SCENE_PHYSICS_BALANCE_TILT;
    gX_F32 restY = halfH * cosf(tilt) + halfW * sinf(tilt);
    gX_F32Qua quat = gX_F32Qua_FromAxisAngle((gX_F32v3){-1.0f, 0.0f, 0.0f},
        tilt);
    for (gX_I32 i = 0; i < scene->balanceCount; i++) {
        gX_I32 slot = scene->balance[i];
        if (slot < 0 || slot >= scene->propCount) continue;
        gX_PhysicsBody body = scene->props[slot].body;
        if (body.internal == 0) continue;
        gX_F32 offset = (i == 0) ? GX_SCENE_PHYSICS_BALANCE_OFFSET
            : ((i == 2) ? -GX_SCENE_PHYSICS_BALANCE_OFFSET : 0.0f);
        //ai: the body origin is the balance point: the shape center stays at
        //   the spawn column and the origin rides the rotated offset.
        gX_F32v3 pos = scene->props[slot].spawnPos;
        gX_PhysicsBody_SetRotation(body, quat);
        pos.y = restY + offset * cosf(tilt);
        pos.z -= offset * sinf(tilt);
        gX_PhysicsBody_SetPosition(body, pos);
        gX_PhysicsBody_SetLinearVel(body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetAngularVel(body, (gX_F32v3){0.0f, 0.0f, 0.0f});
    }
}

//ai: rover — a hinge2 four-wheeler: the front wheels carry their steering
//   angle in the spawn pose (the group-1 motor holds it), the drive motors
//   spin all four wheels and the soft suspension leans the chassis through
//   the turns. The rear axle spawns tangent to the drive circle so the lap
//   stays on the open floor; R re-poses it with the other rig parts.
gX_I0 gX_ScenePhysics_BuildRover(gX_ScenePhysics *scene) {
    if (!scene->roverEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_F32 r = GX_SCENE_PHYSICS_ROVER_WHEEL_R;
    gX_F32 halfBase = GX_SCENE_PHYSICS_ROVER_WHEELBASE * 0.5f;
    gX_F32 halfTrack = GX_SCENE_PHYSICS_ROVER_TRACK * 0.5f;
    gX_F32 steer = GX_SCENE_PHYSICS_ROVER_STEER;
    gX_F32v3 rearAxle = {GX_SCENE_PHYSICS_ROVER_CENTER_X, r,
        GX_SCENE_PHYSICS_ROVER_CENTER_Z + GX_SCENE_PHYSICS_ROVER_SPAWN_R};
    gX_F32v3 chassisPos = {rearAxle.x + halfBase,
        r + GX_SCENE_PHYSICS_ROVER_CHASSIS_LIFT, rearAxle.z};
    gX_Color shell = {96, 148, 196, 255};
    gX_Color tyre = {58, 62, 70, 255};

    scene->roverLinkFirst = scene->linkCount;
    gX_I32 chassisLink = gX_ScenePhysics_AddRigLink(scene, gfx, chassisPos,
        (gX_F32v3){GX_SCENE_PHYSICS_ROVER_CHASSIS_X,
            GX_SCENE_PHYSICS_ROVER_CHASSIS_Y,
            GX_SCENE_PHYSICS_ROVER_CHASSIS_Z}, 1.2f, shell);
    if (chassisLink < 0) return;
    gX_PhysicsBody chassis = scene->links[chassisLink].body;
    gX_PhysicsBody_SetDamping(chassis, 0.01f, 0.02f);
    gX_PhysicsBody_SetAutoDisable(chassis, 0);

    scene->roverJointFirst = scene->jointCount;
    for (gX_I32 i = 0; i < 4; i++) {
        gX_I1 front = (i >= 2);
        gX_F32 side = (i % 2 == 0) ? -1.0f : 1.0f;
        gX_F32v3 wheelPos = {rearAxle.x + (front ? halfBase * 2.0f : 0.0f),
            r, rearAxle.z + side * halfTrack};
        gX_I32 wheelLink = gX_ScenePhysics_AddRigPart(scene, gfx,
            gX_SCENE_PHYSICS_PROP_CYLINDER, wheelPos,
            (gX_F32v3){r, r, GX_SCENE_PHYSICS_ROVER_WHEEL_W}, 0.9f, tyre);
        if (wheelLink < 0) break;
        gX_PhysicsBody wheel = scene->links[wheelLink].body;
        //ai: tyre grip over the steel rig default — the drive is thrust-
        //   limited by friction, so a slick wheel would just spin out.
        gX_PhysicsBody_SetMaterial(wheel,
            (gX_PhysicsMaterial){0.95f, 0.0f, 0.0f, 0.0f});
        //ai: a spoke bar rides the wheel's transform so the spin reads
        //   (a smooth cylinder is rotationally symmetric and looks parked).
        if (gfx) {
            gX_Mesh spokeMesh = gX_Mesh_GenCube(r * 2.2f, 0.07f,
                GX_SCENE_PHYSICS_ROVER_WHEEL_W * 0.9f);
            gX_I32 spokeIndex =
                gX_RendererGFX_Model_AddGeneratedMesh(gfx, spokeMesh);
            if (spokeIndex >= 0) {
                gX_RendererGFX_Model *spoke =
                    &((gX_RendererGFX_Model *)gfx->models.data)[spokeIndex];
                spoke->params.usePBR = true;
                spoke->params.albedoTint = (gX_Color){226, 222, 208, 255};
                spoke->params.metalness = 0.1f;
                spoke->params.roughness = 0.6f;
                spoke->params.emissiveColor = (gX_Color){226, 222, 208, 255};
                spoke->params.emissiveFactor = 0.12f;
                scene->links[wheelLink].extraModel = spokeIndex;
            } else {
                gX_Mesh_Unload(spokeMesh);
            }
        }
        gX_F32 yaw = front ? steer : 0.0f;
        gX_F32Qua pose = gX_F32Qua_FromAxisAngle((gX_F32v3){0.0f, 1.0f, 0.0f},
            yaw);
        if (yaw != 0.0f) gX_PhysicsBody_SetRotation(wheel, pose);
        scene->links[wheelLink].spawnQuat = pose;
        scene->links[wheelLink].prevQuat = pose;
        //ai: near-zero damping — the rig default is a damper, and a vehicle
        //   cannot reach its drive speed through it.
        gX_PhysicsBody_SetDamping(wheel, 0.0f, 0.0f);
        gX_PhysicsBody_SetAutoDisable(wheel, 0);

        //ai: axis1 = the steering/suspension up, axis2 = the wheel's own
        //   axle (its yawed body Z). The rear wheels carry the drive; the
        //   steered fronts roll freely so the turn follows the geometry.
        gX_F32v3 up = {0.0f, 1.0f, 0.0f};
        gX_F32v3 axle = {sinf(yaw), 0.0f, cosf(yaw)};
        gX_PhysicsJoint joint = gX_PhysicsJoint_CreateHinge2(physics.world,
            wheel, chassis, wheelPos, up, axle);
        if (joint.internal == 0) break;
        gX_PhysicsJoint_SetSuspension(joint,
            GX_SCENE_PHYSICS_ROVER_SUSP_ERP, GX_SCENE_PHYSICS_ROVER_SUSP_CFM);
        gX_PhysicsJoint_SetMotor2(joint,
            front ? GX_SCENE_PHYSICS_ROVER_SPIN : 0.0f,
            front ? GX_SCENE_PHYSICS_ROVER_TORQUE : 0.0f);
        //ai: front-wheel drive — the driven steered wheels pull the cart
        //   along their heading while the free rear wheels keep their full
        //   lateral grip, so the tight lap stays stable (driving the rear
        //   spins it out and the cart stops cornering).
        gX_PhysicsJoint_SetMotor(joint, 0.0f,
            GX_SCENE_PHYSICS_ROVER_STEER_HOLD);
        gX_PhysicsJoint_SetLimits(joint, -0.12f, 0.12f);
        gX_ScenePhysics_TrackJoint(scene, joint,
            gX_SCENE_PHYSICS_JOINT_HINGE_MOTOR, wheelPos, up);
    }
    scene->roverLinkCount = scene->linkCount - scene->roverLinkFirst;
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: rover %d links / %d joints",
        scene->roverLinkCount, scene->jointCount - scene->roverJointFirst);
}

//ai: (re)weld the target board to the static frame with a fixed joint whose
//   feedback the break check polls — used at build and by R after a break.
static gX_I32 gX_ScenePhysics_WeldTarget(gX_ScenePhysics *scene) {
    if (physics.world.internal == 0) return -1;
    if (scene->targetLink < 0 || scene->targetLink >= scene->linkCount) {
        return -1;
    }
    gX_PhysicsBody board = scene->links[scene->targetLink].body;
    if (board.internal == 0) return -1;
    gX_PhysicsJoint joint = gX_PhysicsJoint_CreateFixed(physics.world, board,
        gX_PHYSICS_BODY_NULL);
    if (joint.internal == 0) return -1;
    gX_PhysicsJoint_EnableFeedback(joint, 1);
    if (scene->targetJoint >= 0 && scene->targetJoint < scene->jointCount) {
        scene->joints[scene->targetJoint].joint = joint;
    } else {
        gX_I32 before = scene->jointCount;
        gX_ScenePhysics_TrackJoint(scene, joint, gX_SCENE_PHYSICS_JOINT_HINGE,
            gX_PhysicsBody_GetPosition(board), (gX_F32v3){0.0f, 1.0f, 0.0f});
        if (scene->jointCount == before) {
            gX_PhysicsJoint_Destroy(physics.world, joint);
            return -1;
        }
        scene->targetJoint = before;
    }
    scene->targetBroken = 0;
    return 0;
}

//ai: break target — a gantry (static post + arm) and a board welded to the
//   frame; a heavy throw at the board snaps the weld (the joint is
//   destroyed) and the board drops. GX_PHYSICS_TARGET=0 skips the rig.
gX_I0 gX_ScenePhysics_BuildBreakTarget(gX_ScenePhysics *scene) {
    if (!scene->targetEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_Color steel = {70, 76, 86, 255};
    gX_Color boardTint = {196, 150, 96, 255};
    gX_ScenePhysics_AddRigStatic(gfx,
        (gX_F32v3){GX_SCENE_PHYSICS_TARGET_POST_X, 1.30f,
            GX_SCENE_PHYSICS_TARGET_Z},
        (gX_F32v3){0.18f, 2.6f, 0.18f}, steel);
    gX_ScenePhysics_AddRigStatic(gfx,
        (gX_F32v3){GX_SCENE_PHYSICS_TARGET_POST_X + 0.45f,
            GX_SCENE_PHYSICS_TARGET_ARM_Y, GX_SCENE_PHYSICS_TARGET_Z},
        (gX_F32v3){0.9f, 0.12f, 0.12f}, steel);

    gX_F32v3 boardPos = {GX_SCENE_PHYSICS_TARGET_POST_X + 0.65f,
        GX_SCENE_PHYSICS_TARGET_BOARD_CY, GX_SCENE_PHYSICS_TARGET_Z};
    gX_I32 link = gX_ScenePhysics_AddRigPart(scene, gfx,
        gX_SCENE_PHYSICS_PROP_BOX, boardPos,
        (gX_F32v3){GX_SCENE_PHYSICS_TARGET_BOARD_SX,
            GX_SCENE_PHYSICS_TARGET_BOARD_SY,
            GX_SCENE_PHYSICS_TARGET_BOARD_SZ},
        GX_SCENE_PHYSICS_TARGET_DENSITY, boardTint);
    if (link < 0) return;
    //ai: the board's broad face turns toward the yard (and the default
    //   camera) so thrown props land on it.
    gX_F32Qua yaw = gX_F32Qua_FromAxisAngle((gX_F32v3){0.0f, 1.0f, 0.0f},
        GX_SCENE_PHYSICS_TARGET_YAW);
    gX_PhysicsBody_SetRotation(scene->links[link].body, yaw);
    scene->links[link].spawnQuat = yaw;
    scene->links[link].prevQuat = yaw;
    scene->targetLink = link;
    if (gX_ScenePhysics_WeldTarget(scene) != 0) {
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: target weld failed");
    }
}

//ai: per-step break check — the weld's feedback carries the board's weight;
//   an impact above the threshold destroys the joint and marks the break.
gX_I0 gX_ScenePhysics_UpdateBreakTarget(gX_ScenePhysics *scene) {
    if (!scene->targetEnabled || scene->targetBroken) return;
    if (scene->targetJoint < 0 || scene->targetJoint >= scene->jointCount) {
        return;
    }
    gX_PhysicsJoint joint = scene->joints[scene->targetJoint].joint;
    if (joint.internal == 0) {
        scene->targetBroken = 1;
        return;
    }
    gX_F32v3 force;
    if (!gX_PhysicsJoint_GetFeedback(joint, &force, NULL)) return;
    gX_F32 mag = sqrtf(force.x * force.x + force.y * force.y
        + force.z * force.z);
    if (mag > GX_SCENE_PHYSICS_TARGET_BREAK_N) {
        gX_PhysicsJoint_Destroy(physics.world, joint);
        scene->joints[scene->targetJoint].joint = gX_PHYSICS_JOINT_NULL;
        scene->targetBroken = 1;
        gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: target broke (%.1f N)",
            mag);
    }
}

//ai: piston pump — a plunger on a piston joint (a slide along Y plus a free
//   rotation about it, the screw/drill DOF) guided by two static plates. The
//   step hook square-wave drives the slide motor into the limits while a
//   small rotation motor keeps the plunger turning.
gX_I0 gX_ScenePhysics_BuildPump(gX_ScenePhysics *scene) {
    if (!scene->pumpEnabled || physics.world.internal == 0) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    gX_Color steel = {70, 76, 86, 255};
    gX_Color plunger = {178, 130, 74, 255};
    gX_F32 x = GX_SCENE_PHYSICS_PUMP_X;
    gX_F32 z = GX_SCENE_PHYSICS_PUMP_Z;
    gX_ScenePhysics_AddRigStatic(gfx, (gX_F32v3){x, 0.14f, z},
        (gX_F32v3){0.9f, 0.28f, 0.9f}, steel);
    gX_ScenePhysics_AddRigStatic(gfx, (gX_F32v3){x - 0.32f, 0.95f, z},
        (gX_F32v3){0.1f, 1.3f, 0.72f}, steel);
    gX_ScenePhysics_AddRigStatic(gfx, (gX_F32v3){x + 0.32f, 0.95f, z},
        (gX_F32v3){0.1f, 1.3f, 0.72f}, steel);

    gX_I32 link = gX_ScenePhysics_AddRigPart(scene, gfx,
        gX_SCENE_PHYSICS_PROP_BOX, (gX_F32v3){x, 0.85f, z},
        (gX_F32v3){0.44f, 0.8f, 0.44f}, 1.1f, plunger);
    if (link < 0) return;
    scene->pumpLink = link;
    gX_PhysicsJoint joint = gX_PhysicsJoint_CreatePiston(physics.world,
        scene->links[link].body, gX_PHYSICS_BODY_NULL,
        (gX_F32v3){x, 0.85f, z}, (gX_F32v3){0.0f, 1.0f, 0.0f});
    if (joint.internal == 0) {
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: pump joint failed");
        return;
    }
    gX_PhysicsJoint_SetLimits(joint, -GX_SCENE_PHYSICS_PUMP_STROKE,
        GX_SCENE_PHYSICS_PUMP_STROKE);
    gX_PhysicsJoint_SetMotor2(joint, GX_SCENE_PHYSICS_PUMP_SPIN, 0.6f);
    scene->pumpJoint = scene->jointCount;
    gX_ScenePhysics_TrackJoint(scene, joint,
        gX_SCENE_PHYSICS_JOINT_HINGE_MOTOR, (gX_F32v3){x, 0.85f, z},
        (gX_F32v3){0.0f, 1.0f, 0.0f});
}

//ai: the pump's square-wave drive — flips the slide motor direction every
//   half period so the plunger pumps instead of pinning at one stop.
gX_I0 gX_ScenePhysics_DrivePump(gX_ScenePhysics *scene, gX_F32 dt) {
    if (!scene->pumpEnabled || physics.world.internal == 0) return;
    if (scene->pumpJoint < 0 || scene->pumpJoint >= scene->jointCount) return;
    gX_PhysicsJoint joint = scene->joints[scene->pumpJoint].joint;
    if (joint.internal == 0) return;
    scene->pumpPhase += dt;
    gX_F32 half = GX_SCENE_PHYSICS_PUMP_LEG;
    gX_F32 phase = fmodf(scene->pumpPhase, 2.0f * half);
    gX_F32 vel = (phase < half) ? GX_SCENE_PHYSICS_PUMP_RATE
        : -GX_SCENE_PHYSICS_PUMP_RATE;
    gX_PhysicsJoint_SetMotor(joint, vel, 25.0f);
}

//ai: terrain mound — a heightfield ridge just past the belt's far end. The
//   wrapped tile is 30 m so its repeated mounds land outside the yard; the
//   mesh draws only the window around the mound (the rest of the tile is
//   flat and coincides with the ground).
gX_I0 gX_ScenePhysics_BuildMound(gX_ScenePhysics *scene) {
    if (!scene->moundEnabled || physics.world.internal == 0) return;
    static gX_F32 heights[GX_SCENE_PHYSICS_MOUND_SAMPLES
        * GX_SCENE_PHYSICS_MOUND_SAMPLES];
    gX_I32 n = GX_SCENE_PHYSICS_MOUND_SAMPLES;
    for (gX_I32 i = 0; i < n; i++) {
        for (gX_I32 j = 0; j < n; j++) {
            //ai: tile-local metres (the tile is centred on the mound).
            gX_F32 x = ((gX_F32)i / (gX_F32)(n - 1) - 0.5f)
                * GX_SCENE_PHYSICS_MOUND_TILE;
            gX_F32 z = ((gX_F32)j / (gX_F32)(n - 1) - 0.5f)
                * GX_SCENE_PHYSICS_MOUND_TILE;
            gX_F32 r2 = (x * x + z * z)
                / (GX_SCENE_PHYSICS_MOUND_RADIUS
                    * GX_SCENE_PHYSICS_MOUND_RADIUS);
            gX_F32 h = GX_SCENE_PHYSICS_MOUND_HEIGHT * (1.0f - r2);
            heights[i * n + j] = (h > 0.0f) ? h : 0.0f;
        }
    }
    scene->moundCollider = gX_PhysicsWorld_AddHeightfield(physics.world,
        heights, n, n, GX_SCENE_PHYSICS_MOUND_TILE,
        GX_SCENE_PHYSICS_MOUND_TILE, 1.0f,
        (gX_F32v3){GX_SCENE_PHYSICS_MOUND_X, 0.0f,
            GX_SCENE_PHYSICS_MOUND_Z});
    if (scene->moundCollider.internal == 0) {
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: mound collider failed");
        return;
    }
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (!gfx) return;
    //ai: mesh window around the mound (the tile is far too big to draw).
    gX_I32 m = GX_SCENE_PHYSICS_MOUND_MESH;
    gX_I32 base = n / 2 - m / 2;
    gX_F32 window[GX_SCENE_PHYSICS_MOUND_MESH
        * GX_SCENE_PHYSICS_MOUND_MESH];
    for (gX_I32 i = 0; i < m; i++) {
        for (gX_I32 j = 0; j < m; j++) {
            window[i * m + j] = heights[(base + i) * n + (base + j)];
        }
    }
    gX_F32 span = GX_SCENE_PHYSICS_MOUND_TILE * (gX_F32)(m - 1)
        / (gX_F32)(n - 1);
    gX_Mesh mesh = gX_Mesh_GenHeightGrid(window, m, m,
        (gX_F32v3){span, 1.0f, span});
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) {
        gX_Mesh_Unload(mesh);
        return;
    }
    gX_RendererGFX_Model *model =
        &((gX_RendererGFX_Model *)gfx->models.data)[index];
    model->params.usePBR = true;
    model->params.albedoTint = (gX_Color){118, 112, 96, 255};
    model->params.metalness = 0.02f;
    model->params.roughness = 0.9f;
    model->params.emissiveColor = model->params.albedoTint;
    model->params.emissiveFactor = 0.06f;
    model->params.pos = (gX_F32v3){GX_SCENE_PHYSICS_MOUND_X, 0.02f,
        GX_SCENE_PHYSICS_MOUND_Z};
}

//ai: the light-speed slug — a slick iron ball on the lane start, tinted pale
//   so it reads against the prop field; it registers with the relativity
//   applier through the normal prop spawn (scene->relativityEnabled).
gX_I32 gX_ScenePhysics_BuildSlug(gX_ScenePhysics *scene) {
    if (!scene->relativityEnabled) return -1;
    if (scene->slugProp >= 0 && scene->slugProp < scene->propCount
        && scene->props[scene->slugProp].body.internal != 0) {
        return 0;
    }
    if (physics.world.internal == 0) return -1;
    gX_F32 radius = GX_SCENE_PHYSICS_SLUG_RADIUS;
    const gX_Element_Info *info = gX_Elements_Get(gX_ELEMENT_IRON);
    gX_F32 density = info
        ? info->density * GX_SCENE_PHYSICS_DENSITY_SCALE : 1.0f;
    gX_F32v3 pos = {GX_SCENE_PHYSICS_SLUG_X, radius + 0.02f,
        GX_SCENE_PHYSICS_SLUG_Z};
    if (gX_ScenePhysics_SpawnPropEx(scene, gX_ScenePhysics_GFX(), pos,
            gX_SCENE_PHYSICS_PROP_SPHERE, (gX_F32v3){radius, radius, radius},
            density, gX_ELEMENT_IRON, (gX_Color){214, 234, 255, 255}) != 0) {
        return -1;
    }
    gX_I32 slot = scene->propCount - 1;
    scene->slugProp = slot;
    //ai: the slug is the slick one — low friction, a lively bounce; the
    //   element's own contact values would grind it to a stop.
    gX_PhysicsBody_SetMaterial(scene->props[slot].body,
        (gX_PhysicsMaterial){0.03f, 0.45f, 0.0f, 0.0f});
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: slug built (slot %d)", slot);
    return 0;
}

//ai: relaunch the slug — re-pose it on the lane start and fire it at 0.92c
//   along +x (the applier's clamp and gamma mass take over from there).
gX_I0 gX_ScenePhysics_LaunchSlug(gX_ScenePhysics *scene) {
    if (scene->slugProp < 0 || scene->slugProp >= scene->propCount) return;
    gX_ScenePhysics_Prop *prop = &scene->props[scene->slugProp];
    if (prop->body.internal == 0) return;
    gX_PhysicsRelativity_Settings settings =
        gX_PhysicsRelativity_GetSettings();
    if (settings.lightSpeed <= 0.0f) return;
    gX_F32 speed = settings.lightSpeed * GX_SCENE_PHYSICS_SLUG_LAUNCH;
    gX_PhysicsBody_SetPosition(prop->body, (gX_F32v3){GX_SCENE_PHYSICS_SLUG_X,
        GX_SCENE_PHYSICS_SLUG_RADIUS + 0.02f, GX_SCENE_PHYSICS_SLUG_Z});
    gX_PhysicsBody_SetRotation(prop->body, gX_F32Qua_Identity());
    gX_PhysicsBody_SetLinearVel(prop->body, (gX_F32v3){speed, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(prop->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_Enable(prop->body);
    gX_TraceLog(gX_LOG_NOTICE,
        "PHYSICS_SCENE: slug launched at %.2f m/s (%.2f c)", speed,
        GX_SCENE_PHYSICS_SLUG_LAUNCH);
}

//ai: spawn a humanoid ragdoll (feet at `pos`) and give each limb a scaled
//   unit-cube model in the GFX heap.
static gX_I32 gX_ScenePhysics_SpawnRagdoll(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_F32v3 pos) {
    if (scene->ragdollCount >= GX_SCENE_PHYSICS_MAX_RAGDOLLS) return -1;
    gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[scene->ragdollCount];
    for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
        slot->boneModel[b] = -1;
    }
    if (gX_Ragdoll_CreateHumanoid(&slot->ragdoll, physics.world, pos, 1.0f) != 0) {
        return -1;
    }
    for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
        gX_Ragdoll_Bone *bone = &slot->ragdoll.bones[b];
        if (scene->materialsEnabled) {
            gX_PhysicsBody_SetMaterial(bone->body, (gX_PhysicsMaterial){
                GX_SCENE_PHYSICS_TISSUE_FRICTION,
                GX_SCENE_PHYSICS_TISSUE_RESTITUTION, 0.0f, 0.0f});
        }
        gX_Mesh mesh = gX_Mesh_GenCube(1.0f, 1.0f, 1.0f);
        gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
        if (index < 0) {
            gX_Mesh_Unload(mesh);
            slot->boneModel[b] = -1;
            gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: ragdoll bone mesh unavailable");
            continue;
        }
        gX_RendererGFX_Model *model =
            &((gX_RendererGFX_Model *)gfx->models.data)[index];
        model->params.usePBR = true;
        model->params.albedoTint = gX_ScenePhysics_Tint((gX_I32)bone->colorIndex);
        model->params.metalness = 0.12f;
        model->params.roughness = 0.55f;
        model->params.emissiveColor = model->params.albedoTint;
        model->params.emissiveFactor = 0.10f;  //ai: lift limbs off the dark ground
        model->params.scale = bone->renderScale;
        slot->boneModel[b] = index;
    }
    //ai: all ragdolls share one sign so a dummy's own limbs repel outward
    //   (spread-eagle) and dummies push each other apart instead of
    //   clumping; opposite-sign dummies would just tangle.
    slot->charge = 1.0f;
    gX_ScenePhysics_RagdollRegister(scene, slot);
    scene->ragdollCount++;
    scene->ragdollSpawned++;
    return 0;
}

//ai: destroy one ragdoll and swap-remove its slot.
static gX_I0 gX_ScenePhysics_DestroyRagdoll(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_I32 index) {
    if (index < 0 || index >= scene->ragdollCount) return;
    gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[index];
    if (physics.world.internal != 0) {
        gX_ScenePhysics_RagdollUnregister(scene, slot);
        gX_Ragdoll_Destroy(&slot->ragdoll, physics.world);
    }
    for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
        if (slot->boneModel[b] >= 0) {
            gX_RendererGFX_Model_Remove(gfx, (gX_U32)slot->boneModel[b]);
            slot->boneModel[b] = -1;
        }
    }
    gX_I32 last = scene->ragdollCount - 1;
    if (index != last) {
        scene->ragdolls[index] = scene->ragdolls[last];
    }
    for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
        scene->ragdolls[last].boneModel[b] = -1;
    }
    scene->ragdollCount = last;
}

//ai: ground-plane intersection of a mouse ray (y = 0 plane).
static gX_I1 gX_ScenePhysics_GroundHit(gX_Ray ray, gX_F32v3 *outPos) {
    if (fabsf(ray.direction.y) < 0.0001f) return 0;
    gX_F32 t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return 0;
    outPos->x = ray.position.x + t * ray.direction.x;
    outPos->y = 0.0f;
    outPos->z = ray.position.z + t * ray.direction.z;
    return 1;
}

//ai: map a picked GFX model back to a prop slot (-1 for ground/none).
static gX_I32 gX_ScenePhysics_PropForModel(gX_ScenePhysics *scene,
    gX_I32 modelIndex) {
    if (modelIndex < 0) return -1;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].modelIndex == modelIndex) return i;
    }
    return -1;
}

//ai: map a picked GFX model back to a joint rig part.
static gX_I32 gX_ScenePhysics_LinkForModel(gX_ScenePhysics *scene,
    gX_I32 modelIndex) {
    if (modelIndex < 0) return -1;
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        if (scene->links[i].modelIndex == modelIndex) return i;
    }
    return -1;
}

//ai: map a picked GFX model back to the ragdoll that owns the limb.
static gX_I32 gX_ScenePhysics_PickRagdoll(gX_ScenePhysics *scene,
    gX_I32 modelIndex) {
    if (modelIndex < 0) return -1;
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            if (slot->boneModel[b] == modelIndex) return i;
        }
    }
    return -1;
}

//ai: build the entry content once the renderer is ready. Re-entry after a
//   scene switch rebuilds a fresh prop set (the renderer cleared the heap).
static gX_I0 gX_ScenePhysics_Ensure(gX_ScenePhysics *scene) {
    if (!maindata.backendLoaded) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (!gfx) return;
    if (gfx->common.physicsScene) return;

    //ai: another owner's content is still in the heap — the renderer's
    //   scene-switch clear runs first, then the build happens next frame.
    if (gfx->common.demoSceneAdded || gfx->common.battleScene) return;

    gX_ScenePhysics_ClearContent(scene);

    //ai: contact materials — per-element friction/restitution plus the
    //   ground/steel/tissue conventions; GX_PHYSICS_MATERIALS=0 keeps the
    //   wrapper defaults for A/B captures.
    const gX_I8 *envMaterials = (const gX_I8 *)getenv("GX_PHYSICS_MATERIALS");
    scene->materialsEnabled = (envMaterials && envMaterials[0] == '0') ? 0 : 1;

    //ai: ground collider persists across entries (destroyed at unload only).
    if (scene->groundCollider.internal == 0) {
        scene->groundCollider = gX_PhysicsWorld_AddGroundPlane(physics.world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    if (scene->materialsEnabled && scene->groundCollider.internal != 0) {
        gX_PhysicsCollider_SetMaterial(scene->groundCollider,
            (gX_PhysicsMaterial){GX_SCENE_PHYSICS_GROUND_FRICTION,
                GX_SCENE_PHYSICS_GROUND_RESTITUTION, 0.0f, 0.0f});
    }

    gX_Mesh groundMesh = gX_Mesh_GenCube(GX_SCENE_PHYSICS_GROUND_SIZE, 0.5f,
        GX_SCENE_PHYSICS_GROUND_SIZE);
    scene->groundModelIndex = gX_RendererGFX_Model_AddGeneratedMesh(gfx, groundMesh);
    if (scene->groundModelIndex >= 0) {
        gX_RendererGFX_Model *ground =
            &((gX_RendererGFX_Model *)gfx->models.data)[scene->groundModelIndex];
        ground->params.pos = (gX_F32v3){0.0f, -0.25f, 0.0f};
        ground->params.usePBR = true;
        ground->params.albedoTint = (gX_Color){38, 42, 50, 255};
        ground->params.metalness = 0.05f;
        ground->params.roughness = 0.9f;
    } else {
        gX_Mesh_Unload(groundMesh);
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS_SCENE: ground mesh unavailable");
    }

    //ai: key + fill so the PBR props are lit even against the night sky.
    gX_ScenePhysics_AddLight(gfx, (gX_F32v3){-6.5f, 5.5f, 5.0f},
        (gX_Color){255, 226, 190, 255});
    gX_ScenePhysics_AddLight(gfx, (gX_F32v3){6.0f, 4.0f, -5.0f},
        (gX_Color){150, 190, 255, 255});

    //ai: prop plan — env override for capture/audit runs.
    gX_I32 total = GX_SCENE_PHYSICS_DEFAULT_PROPS;
    const gX_I8 *env = (const gX_I8 *)getenv("GX_PHYSICS_PROPS");
    if (env && env[0]) total = atoi((const char *)env);
    if (total < 1) total = 1;
    if (total > GX_SCENE_PHYSICS_MAX_PROPS) total = GX_SCENE_PHYSICS_MAX_PROPS;
    scene->spawnTotal = total;

    //ai: EM preset hooks — captures/audits start charged (M toggles live).
    scene->emB = (gX_F32v3){0.0f, 0.0f, 0.0f};
    const gX_I8 *envB = (const gX_I8 *)getenv("GX_PHYSICS_B");
    if (envB && envB[0]) {
        sscanf((const char *)envB, "%f,%f,%f",
            &scene->emB.x, &scene->emB.y, &scene->emB.z);
    }
    const gX_I8 *envEm = (const gX_I8 *)getenv("GX_PHYSICS_EM");
    scene->emEnabled = (envEm && envEm[0] && envEm[0] != '0') ? 1 : 0;
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetUniformB(scene->emB);
    gX_PhysicsEM_SetEnabled(scene->emEnabled);

    //ai: thermal preset hooks — impacts heat by default, H heats manually.
    scene->heatInitial = 0.0f;
    scene->heatEnergy = GX_SCENE_PHYSICS_HEAT_DEFAULT;
    const gX_I8 *envTemp = (const gX_I8 *)getenv("GX_PHYSICS_TEMP");
    if (envTemp && envTemp[0]) {
        gX_F32 absolute = (gX_F32)atof((const char *)envTemp);
        gX_F32 ambient = gX_PhysicsThermal_Defaults().ambientTemperature;
        scene->heatInitial = (absolute > ambient) ? absolute - ambient : 0.0f;
    }
    const gX_I8 *envHeat = (const gX_I8 *)getenv("GX_PHYSICS_HEAT");
    if (envHeat && envHeat[0]) {
        scene->heatEnergy = (gX_F32)atof((const char *)envHeat);
    }
    const gX_I8 *envThermal = (const gX_I8 *)getenv("GX_PHYSICS_THERMAL");
    gX_I1 thermalOn = (envThermal && envThermal[0] == '0') ? 0 : 1;
    gX_PhysicsThermal_Reset();
    gX_PhysicsThermal_SetEnabled(thermalOn);
    if (thermalOn && physics.world.internal != 0) {
        gX_PhysicsThermal_InstallCollisionCallback(physics.world);
    }
    gX_Physics_SetStepHook(gX_ScenePhysics_StepHook, scene);

    //ai: impact FX — contact events from the shared world queue spark
    //   bursts; GX_PHYSICS_SPARKS=0 disables the capture and the emitters.
    //   The debug view shares the same event callback, so it can run with
    //   sparks off and still record contacts.
    const gX_I8 *envSparks = (const gX_I8 *)getenv("GX_PHYSICS_SPARKS");
    gX_I1 sparksOn = !(envSparks && envSparks[0] == '0');
    const gX_I8 *envDebug = (const gX_I8 *)getenv("GX_PHYSICS_DEBUG");
    scene->debugDraw = (envDebug && envDebug[0] == '1') ? 1 : 0;
    //ai: scorch decals — GX_PHYSICS_DECALS=0 disables; the pass flag is
    //   saved so park can hand the renderer back its state.
    const gX_I8 *envDecals = (const gX_I8 *)getenv("GX_PHYSICS_DECALS");
    scene->decalsEnabled = (envDecals && envDecals[0] == '0') ? 0 : 1;
    scene->scorchTexture = -1;
    //ai: impact audio — on by default; GX_PHYSICS_SFX=0 (or the editor's
    //   toggle) disables the layer without touching sparks/decals.
    const gX_I8 *envSfx = (const gX_I8 *)getenv("GX_PHYSICS_SFX");
    scene->sfxEnabled = (envSfx && envSfx[0] == '0') ? 0 : 1;
    if (scene->decalsEnabled) {
        scene->scorchTexture = gX_RendererGFX_Texture_Add(gfx,
            GX_RESOURCE_DIR "/texture/misc/scorch.png");
        scene->decalsPrev = gfx->state.ENABLE_DECALS;
        gfx->state.ENABLE_DECALS = 1;
    }
    if (sparksOn || scene->debugDraw || scene->decalsEnabled || scene->sfxEnabled) {
        gX_ScenePhysics_EnableImpactCapture(scene);
    }
    //ai: EnableImpactCapture force-sets sparksEnabled; the env decision is
    //   authoritative for the emitter pass.
    scene->sparksEnabled = sparksOn ? 1 : 0;
    if (sparksOn) {
        gX_ScenePhysics_SparksSetup(scene, gfx);
    }

    //ai: first batch spawns immediately so the scene is never empty.
    gX_I32 immediate = (total > 8) ? 8 : total;
    for (gX_I32 i = 0; i < immediate; i++) {
        gX_ScenePhysics_SpawnProp(scene, gfx, gX_ScenePhysics_GridPos(i));
    }

    //ai: ragdoll plan — env override for capture/audit runs.
    gX_I32 ragdollTotal = 0;
    const gX_I8 *envRagdolls = (const gX_I8 *)getenv("GX_PHYSICS_RAGDOLLS");
    if (envRagdolls && envRagdolls[0]) ragdollTotal = atoi((const char *)envRagdolls);
    if (ragdollTotal < 0) ragdollTotal = 0;
    if (ragdollTotal > GX_SCENE_PHYSICS_MAX_RAGDOLLS) {
        ragdollTotal = GX_SCENE_PHYSICS_MAX_RAGDOLLS;
    }
    for (gX_I32 i = 0; i < ragdollTotal; i++) {
        gX_ScenePhysics_SpawnRagdoll(scene, gfx, gX_ScenePhysics_RagdollSpawnPos(i));
    }

    //ai: joint rigs — chain/door/windmill/rail built after the prop plan;
    //   GX_PHYSICS_JOINTS=0 skips them.
    const gX_I8 *envJoints = (const gX_I8 *)getenv("GX_PHYSICS_JOINTS");
    scene->jointsEnabled = (envJoints && envJoints[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildJointRigs(scene);

    //ai: water — the basin plus the filled pool; GX_PHYSICS_WATER=0 skips
    //   the water and keeps the empty basin (P fills/drains at runtime).
    const gX_I8 *envWater = (const gX_I8 *)getenv("GX_PHYSICS_WATER");
    scene->waterEnabled = (envWater && envWater[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildWater(scene);

    //ai: magnets — the bar-magnet row builds with the scene
    //   (GX_PHYSICS_MAGNETS=0 skips it); =1 starts the forces on too
    //   (N toggles at runtime).
    const gX_I8 *envMagnets = (const gX_I8 *)getenv("GX_PHYSICS_MAGNETS");
    scene->magnetsEnabled = (envMagnets && envMagnets[0] == '0') ? 0 : 1;
    scene->magnetsOn = (envMagnets && envMagnets[0] == '1') ? 1 : 0;
    gX_ScenePhysics_BuildMagnets(scene);

    //ai: conveyor belt — GX_PHYSICS_BELT=0 skips the strip.
    gX_ScenePhysics_BuildBelt(scene);

    //ai: kinematic movers — GX_PHYSICS_MOVERS=0 skips the lift/turntable.
    const gX_I8 *envMovers = (const gX_I8 *)getenv("GX_PHYSICS_MOVERS");
    scene->moversEnabled = (envMovers && envMovers[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildMovers(scene);

    //ai: balance posts — GX_PHYSICS_COM=0 skips the mass-center demo.
    const gX_I8 *envCom = (const gX_I8 *)getenv("GX_PHYSICS_COM");
    scene->balanceEnabled = (envCom && envCom[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildBalancePosts(scene);

    //ai: rover — GX_PHYSICS_ROVER=0 skips the hinge2 vehicle.
    const gX_I8 *envRover = (const gX_I8 *)getenv("GX_PHYSICS_ROVER");
    scene->roverEnabled = (envRover && envRover[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildRover(scene);

    //ai: break target — GX_PHYSICS_TARGET=0 skips the welded board.
    const gX_I8 *envTarget = (const gX_I8 *)getenv("GX_PHYSICS_TARGET");
    scene->targetEnabled = (envTarget && envTarget[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildBreakTarget(scene);

    //ai: piston pump — GX_PHYSICS_PUMP=0 skips the plunger.
    const gX_I8 *envPump = (const gX_I8 *)getenv("GX_PHYSICS_PUMP");
    scene->pumpEnabled = (envPump && envPump[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildPump(scene);

    //ai: terrain mound — GX_PHYSICS_MOUND=0 skips the heightfield.
    const gX_I8 *envMound = (const gX_I8 *)getenv("GX_PHYSICS_MOUND");
    scene->moundEnabled = (envMound && envMound[0] == '0') ? 0 : 1;
    gX_ScenePhysics_BuildMound(scene);

    //ai: relativity — GX_PHYSICS_RELATIVITY=0 skips the light-speed lane and
    //   disables the applier; GX_PHYSICS_LIGHTSPEED=<m/s> sets c.
    const gX_I8 *envRelativity = (const gX_I8 *)getenv("GX_PHYSICS_RELATIVITY");
    scene->relativityEnabled =
        (envRelativity && envRelativity[0] == '0') ? 0 : 1;
    gX_PhysicsRelativity_Settings relativity =
        gX_PhysicsRelativity_Defaults();
    const gX_I8 *envLightSpeed =
        (const gX_I8 *)getenv("GX_PHYSICS_LIGHTSPEED");
    if (envLightSpeed && envLightSpeed[0]) {
        gX_F32 lightSpeed = (gX_F32)atof((const char *)envLightSpeed);
        if (lightSpeed > 0.0f) relativity.lightSpeed = lightSpeed;
    }
    gX_PhysicsRelativity_SetSettings(relativity);
    gX_PhysicsRelativity_SetEnabled(scene->relativityEnabled);
    scene->slugProp = -1;

    //ai: editor preset seeding — GX_PHYSICS_PRESET=<json> loads a tuning
    //   over the env-seeded state (captures/audits), applied live at the
    //   end of the entry plan.
    const gX_I8 *envPreset = (const gX_I8 *)getenv("GX_PHYSICS_PRESET");
    if (envPreset && envPreset[0]) {
        gX_ScenePhysics_Tuning preset;
        if (gX_ScenePhysics_Tuning_Load(envPreset, &preset) == 0) {
            gX_ScenePhysics_Tuning_Apply(scene, &preset);
            gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: preset applied (%s)",
                envPreset);
        }
    }

    if (!scene->cameraSet) {
        scene->cameraSet = true;
        if (scene->waterEnabled) {
            camera.camera3D.position = (gX_F32v3){10.5f, 6.0f, 8.5f};
            camera.camera3D.target = (gX_F32v3){2.0f, 1.0f, -1.5f};
        } else {
            camera.camera3D.position = (gX_F32v3){7.5f, 5.0f, 7.5f};
            camera.camera3D.target = (gX_F32v3){0.0f, 1.2f, 0.0f};
        }
        camera.camera3D.fovy = 60.0f;
    }

    gfx->common.physicsScene = true;
    gfx->common.demoSceneAdded = true;  //ai: suppress the default demo content
    gX_Physics_ResetFrameHistory();
    gX_TraceLog(gX_LOG_NOTICE,
        "PHYSICS_SCENE: %d props / %d ragdolls / %d rig links planned (world %s, em %s, thermal %s)",
        total, ragdollTotal, scene->linkCount, physics.enabled ? "live" : "stub",
        scene->emEnabled ? "on" : "off",
        gX_PhysicsThermal_Enabled() ? "on" : "off");
}

//ai: deterministic ragdoll drop spots for the entry plan.
static gX_F32v3 gX_ScenePhysics_RagdollSpawnPos(gX_I32 index) {
    gX_F32 x = -3.0f + (gX_F32)(index % 4) * 2.0f;
    gX_F32 z = 3.5f + (gX_F32)((index / 4) % 2) * 2.0f;
    gX_F32 y = 1.0f + (gX_F32)(index % 3) * 1.5f;
    return (gX_F32v3){x, y, z};
}

//ai: spawn the stagger batch one entry at a time; the mover payloads ride
//   the tail of the plan (one per frame) so the grid slots stay dense.
static gX_I0 gX_ScenePhysics_SpawnNext(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx) {
    gX_I32 spawned = 0;
    while (scene->propCount < scene->spawnTotal
        && spawned < GX_SCENE_PHYSICS_SPAWN_PER_FRAME) {
        gX_I32 slot = scene->propCount;
        if (gX_ScenePhysics_SpawnProp(scene, gfx, gX_ScenePhysics_GridPos(slot)) != 0) break;
        spawned++;
    }
    gX_I32 payloadTarget = scene->moversEnabled
        ? GX_SCENE_PHYSICS_MOVER_PAYLOADS : 0;
    if (scene->propCount >= scene->spawnTotal
        && scene->moverPayloads < payloadTarget) {
        if (gX_ScenePhysics_SpawnPayload(scene, scene->moverPayloads) == 0) {
            scene->moverPayloads++;
        }
    }
    //ai: the light-speed slug rides the tail like the payloads (one per
    //   frame) so it never crowds the grid slots; it launches once as it
    //   appears (L re-fires) so the lane demonstrates itself.
    if (scene->spawnTotal > 0 && scene->propCount >= scene->spawnTotal
        && scene->moverPayloads >= payloadTarget
        && scene->relativityEnabled && scene->slugProp < 0) {
        if (gX_ScenePhysics_BuildSlug(scene) == 0) {
            gX_ScenePhysics_LaunchSlug(scene);
        }
    }
}

//ai: drop every pin that holds the given body (used by remove and unpin).
static gX_I32 gX_ScenePhysics_UnpinBody(gX_ScenePhysics *scene,
    gX_PhysicsBody body) {
    gX_I32 removed = 0;
    for (gX_I32 i = 0; i < scene->pinCount; i++) {
        if (scene->pins[i].body.internal != body.internal) continue;
        if (scene->pins[i].joint.internal != 0
            && physics.world.internal != 0) {
            gX_PhysicsJoint_Destroy(physics.world, scene->pins[i].joint);
        }
        scene->pins[i] = scene->pins[scene->pinCount - 1];
        scene->pinCount--;
        removed++;
        i--;
    }
    return removed;
}

//ai: release every toolgun pin (content rebuilds, park and the R reset).
gX_I0 gX_ScenePhysics_UnpinAll(gX_ScenePhysics *scene) {
    for (gX_I32 i = 0; i < scene->pinCount; i++) {
        if (scene->pins[i].joint.internal != 0
            && physics.world.internal != 0) {
            gX_PhysicsJoint_Destroy(physics.world, scene->pins[i].joint);
        }
        scene->pins[i].joint = gX_PHYSICS_JOINT_NULL;
    }
    scene->pinCount = 0;
}

gX_I32 gX_ScenePhysics_PinCount(gX_ScenePhysics *scene) {
    return scene->pinCount;
}

//ai: remove one prop slot — appliers first, pins before the body, then the
//   model; R-spawned replacements just fill the free slot again.
static gX_I0 gX_ScenePhysics_RemoveProp(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_I32 index) {
    if (index < 0 || index >= scene->propCount) return;
    gX_ScenePhysics_Prop *prop = &scene->props[index];
    if (prop->body.internal != 0) {
        gX_ScenePhysics_UnpinBody(scene, prop->body);
        gX_PhysicsEM_RemoveBody(prop->body);
        gX_PhysicsRelativity_RemoveBody(prop->body);
        gX_PhysicsThermal_RemoveBody(prop->body);
        gX_PhysicsFluid_RemoveBody(prop->body);
        gX_PhysicsBody_Destroy(physics.world, prop->body);
        prop->body = gX_PHYSICS_BODY_NULL;
    }
    if (scene->slugProp == index) {
        scene->slugProp = -1;
    }
    if (prop->modelIndex >= 0) {
        if (gfx) {
            gX_RendererGFX_Model_Remove(gfx, (gX_U32)prop->modelIndex);
        }
        prop->modelIndex = -1;
    }
    scene->liveCount--;
    if (scene->dragProp == index) {
        scene->dragProp = -1;
        scene->dragActive = 0;
    }
}

//ai: resolve a GFX model pick to the sandbox object behind it — the toolgun's
//   aim. Also feeds the HUD's hover lines (the mouse half of the interaction
//   stands down in tool mode); a ragdoll hit resolves its aimed bone.
gX_ScenePhysics_Aim gX_ScenePhysics_AimAtModel(gX_ScenePhysics *scene,
    gX_I32 modelIndex) {
    gX_ScenePhysics_Aim aim;
    aim.kind = gX_SCENE_PHYSICS_AIM_NONE;
    aim.index = -1;
    aim.bone = -1;
    aim.body = gX_PHYSICS_BODY_NULL;
    aim.position = (gX_F32v3){0.0f, 0.0f, 0.0f};
    scene->hoverProp = -1;
    scene->hoverLink = -1;
    if (!scene->loaded || modelIndex < 0) return aim;
    gX_I32 prop = gX_ScenePhysics_PropForModel(scene, modelIndex);
    if (prop >= 0 && scene->props[prop].body.internal != 0) {
        aim.kind = gX_SCENE_PHYSICS_AIM_PROP;
        aim.index = prop;
        aim.body = scene->props[prop].body;
        aim.position = gX_PhysicsBody_GetPosition(aim.body);
        scene->hoverProp = prop;
        return aim;
    }
    gX_I32 link = gX_ScenePhysics_LinkForModel(scene, modelIndex);
    if (link >= 0 && scene->links[link].body.internal != 0) {
        aim.kind = gX_SCENE_PHYSICS_AIM_LINK;
        aim.index = link;
        aim.body = scene->links[link].body;
        aim.position = gX_PhysicsBody_GetPosition(aim.body);
        scene->hoverLink = link;
        return aim;
    }
    gX_I32 ragdollIndex = gX_ScenePhysics_PickRagdoll(scene, modelIndex);
    if (ragdollIndex >= 0) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[ragdollIndex];
        aim.kind = gX_SCENE_PHYSICS_AIM_RAGDOLL;
        aim.index = ragdollIndex;
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            if (slot->boneModel[b] != modelIndex) continue;
            aim.bone = (gX_I32)b;
            aim.body = slot->ragdoll.bones[b].body;
            break;
        }
        if (aim.body.internal == 0 && slot->ragdoll.boneCount > 0) {
            aim.bone = 0;
            aim.body = slot->ragdoll.bones[0].body;
        }
        if (aim.body.internal != 0) {
            aim.position = gX_PhysicsBody_GetPosition(aim.body);
        }
        return aim;
    }
    return aim;
}

//ai: tool mode hand-off — the sandbox's own mouse interaction stands down and
//   any held drag is dropped so nothing keeps simulating after the handover.
gX_I0 gX_ScenePhysics_SetToolMode(gX_ScenePhysics *scene, gX_I1 on) {
    gX_I1 active = on ? 1 : 0;
    if (scene->toolMode == active) return;
    scene->toolMode = active;
    scene->dragProp = -1;
    scene->dragLink = -1;
    scene->dragActive = 0;
    scene->hoverProp = -1;
    scene->hoverLink = -1;
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: tool mode %s",
        active ? "on" : "off");
}

//ai: spring grab — press locks the aimed prop/link, hold chases the point
//   along the crosshair ray at the target's current distance (the step
//   hook's drag apply does the pulling), release throws with the last
//   velocity.
gX_I0 gX_ScenePhysics_ToolGrab(gX_ScenePhysics *scene, gX_ScenePhysics_Aim aim,
    gX_F32v3 rayOrigin, gX_F32v3 rayDirection, gX_I1 pressed, gX_I1 held) {
    if (pressed && (aim.kind == gX_SCENE_PHYSICS_AIM_PROP
            || aim.kind == gX_SCENE_PHYSICS_AIM_LINK)) {
        scene->dragProp = (aim.kind == gX_SCENE_PHYSICS_AIM_PROP)
            ? aim.index : -1;
        scene->dragLink = (aim.kind == gX_SCENE_PHYSICS_AIM_LINK)
            ? aim.index : -1;
    }
    if (!held) {
        scene->dragProp = -1;
        scene->dragLink = -1;
        scene->dragActive = 0;
        return;
    }
    gX_PhysicsBody body = gX_PHYSICS_BODY_NULL;
    if (scene->dragProp >= 0 && scene->dragProp < scene->propCount) {
        body = scene->props[scene->dragProp].body;
    } else if (scene->dragLink >= 0 && scene->dragLink < scene->linkCount) {
        body = scene->links[scene->dragLink].body;
    }
    if (body.internal == 0) {
        scene->dragProp = -1;
        scene->dragLink = -1;
        scene->dragActive = 0;
        return;
    }
    gX_F32v3 bodyPos = gX_PhysicsBody_GetPosition(body);
    gX_F32 distance = gX_F32v3_Distance(bodyPos, rayOrigin);
    if (distance < 0.6f) distance = 0.6f;
    gX_F32v3 target = gX_F32v3_Add(rayOrigin,
        gX_F32v3_MulScalar(rayDirection, distance));
    if (target.y < 0.5f) target.y = 0.5f;
    scene->dragTarget = target;
    scene->dragActive = 1;
}

//ai: delete the aimed prop or the aimed ragdoll (rig links stay intact —
//   they carry joints); mirrors the right-click path under the crosshair.
gX_I0 gX_ScenePhysics_ToolRemove(gX_ScenePhysics *scene,
    gX_ScenePhysics_Aim aim) {
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (aim.kind == gX_SCENE_PHYSICS_AIM_PROP) {
        gX_ScenePhysics_RemoveProp(scene, gfx, aim.index);
    } else if (aim.kind == gX_SCENE_PHYSICS_AIM_RAGDOLL) {
        gX_ScenePhysics_DestroyRagdoll(scene, gfx, aim.index);
    }
}

//ai: pin the aimed body to the static frame at the aim point (a ball joint:
//   the body swings around the pin); the pool is small and fixed.
gX_I0 gX_ScenePhysics_ToolPin(gX_ScenePhysics *scene, gX_ScenePhysics_Aim aim,
    gX_F32v3 anchor) {
    if (aim.body.internal == 0) return;
    if (scene->pinCount >= GX_SCENE_PHYSICS_MAX_PINS) return;
    if (physics.world.internal == 0) return;
    gX_PhysicsJoint joint = gX_PhysicsJoint_CreateBall(physics.world, aim.body,
        gX_PHYSICS_BODY_NULL, anchor);
    if (joint.internal == 0) return;
    scene->pins[scene->pinCount].body = aim.body;
    scene->pins[scene->pinCount].joint = joint;
    scene->pins[scene->pinCount].anchor = anchor;
    scene->pinCount++;
    gX_PhysicsBody_Enable(aim.body);
}

gX_I0 gX_ScenePhysics_ToolUnpin(gX_ScenePhysics *scene,
    gX_ScenePhysics_Aim aim) {
    if (aim.body.internal == 0) return;
    gX_ScenePhysics_UnpinBody(scene, aim.body);
}

//ai: mouse interaction — drag a prop, click the ground to drop a box,
//   T drops a ragdoll, right-click removes a prop or ragdoll. An editor's
//   toolgun owns the buttons while `scene->toolMode` is set: the mouse half
//   stands down and only the keyboard shortcuts run.
static gX_I0 gX_ScenePhysics_Interact(gX_ScenePhysics *scene,
    gX_RendererGFX *gfx, gX_I1 mouseEnabled) {
    gX_Ray ray = gX_Mouse_GetRay(gX_Mouse_GetPos(), camera.camera3D);
    gX_I32 picked = gX_RendererGFX_Model_Pick(gfx, gX_Mouse_GetPos(), NULL);
    gX_I32 hovered = gX_ScenePhysics_PropForModel(scene, picked);
    gX_I32 hoveredLink = gX_ScenePhysics_LinkForModel(scene, picked);
    gX_I32 hoveredRagdoll = gX_ScenePhysics_PickRagdoll(scene, picked);
    if (mouseEnabled) {
        scene->hoverProp = (hovered >= 0
            && scene->props[hovered].body.internal != 0) ? hovered : -1;
        scene->hoverLink = (hoveredLink >= 0
            && scene->links[hoveredLink].body.internal != 0) ? hoveredLink : -1;
    }

    if (mouseEnabled && gX_Mouse_IsPressed(gX_MOUSE_BUTTON_LEFT)) {
        if (hovered >= 0) {
            gX_F32v3 bodyPos = gX_PhysicsBody_GetPosition(scene->props[hovered].body);
            gX_F32v3 ground;
            scene->dragProp = hovered;
            scene->dragLink = -1;
            scene->dragActive = 0;
            scene->dragOffset = (gX_F32v3){0.0f, 0.0f, 0.0f};
            if (gX_ScenePhysics_GroundHit(ray, &ground)) {
                scene->dragOffset.x = bodyPos.x - ground.x;
                scene->dragOffset.y = bodyPos.y - ground.y;
                scene->dragOffset.z = bodyPos.z - ground.z;
            }
        } else if (hoveredLink >= 0) {
            gX_F32v3 bodyPos = gX_PhysicsBody_GetPosition(
                scene->links[hoveredLink].body);
            gX_F32v3 ground;
            scene->dragProp = -1;
            scene->dragLink = hoveredLink;
            scene->dragActive = 0;
            scene->dragOffset = (gX_F32v3){0.0f, 0.0f, 0.0f};
            if (gX_ScenePhysics_GroundHit(ray, &ground)) {
                scene->dragOffset.x = bodyPos.x - ground.x;
                scene->dragOffset.y = bodyPos.y - ground.y;
                scene->dragOffset.z = bodyPos.z - ground.z;
            }
        } else {
            gX_F32v3 ground;
            if (gX_ScenePhysics_GroundHit(ray, &ground)
                && scene->propCount < GX_SCENE_PHYSICS_MAX_PROPS) {
                gX_F32v3 pos = ground;
                pos.y = 6.0f;
                if (gX_ScenePhysics_SpawnProp(scene, gfx, pos) == 0
                    && scene->spawnTotal < scene->propCount) {
                    scene->spawnTotal = scene->propCount;
                }
            }
        }
    }

    //ai: hold either a prop or a rig part (chain link, door, windmill, rail
    //   carriage); release keeps the velocity, so both can be thrown.
    gX_PhysicsBody dragBody = gX_PHYSICS_BODY_NULL;
    if (scene->dragProp >= 0 && scene->dragProp < scene->propCount) {
        dragBody = scene->props[scene->dragProp].body;
    } else if (scene->dragLink >= 0 && scene->dragLink < scene->linkCount) {
        dragBody = scene->links[scene->dragLink].body;
    }
    if (mouseEnabled && (scene->dragProp >= 0 || scene->dragLink >= 0)) {
        if (!gX_Mouse_IsDown(gX_MOUSE_BUTTON_LEFT)
            || dragBody.internal == 0) {
            scene->dragProp = -1;
            scene->dragLink = -1;
            scene->dragActive = 0;
        } else {
            gX_F32v3 ground;
            if (gX_ScenePhysics_GroundHit(ray, &ground)) {
                gX_F32v3 target = {
                    ground.x + scene->dragOffset.x,
                    ground.y + scene->dragOffset.y,
                    ground.z + scene->dragOffset.z
                };
                if (target.y < 0.5f) target.y = 0.5f;
                scene->dragTarget = target;
                scene->dragActive = 1;
            } else {
                scene->dragActive = 0;
            }
        }
    } else if (mouseEnabled) {
        scene->dragActive = 0;
    }

    if (mouseEnabled && gX_Mouse_IsPressed(gX_MOUSE_BUTTON_RIGHT)) {
        if (hovered >= 0) {
            gX_ScenePhysics_RemoveProp(scene, gfx, hovered);
        } else if (hoveredRagdoll >= 0) {
            gX_ScenePhysics_DestroyRagdoll(scene, gfx, hoveredRagdoll);
        }
    }

    //ai: T — drop a ragdoll at the mouse ground point (fallback: a fixed row).
    if (gX_Key_IsPressed(gX_KEY_T)) {
        gX_F32v3 spawn;
        if (!gX_ScenePhysics_GroundHit(ray, &spawn)) {
            spawn = gX_ScenePhysics_RagdollSpawnPos(scene->ragdollSpawned);
        }
        spawn.y = 2.5f;
        gX_ScenePhysics_SpawnRagdoll(scene, gfx, spawn);
    }

    //ai: B — kick the balance posts (the mass-center demo).
    if (gX_Key_IsPressed(gX_KEY_B)) {
        gX_ScenePhysics_NudgeBalance(scene);
    }

    //ai: M — toggle electromagnetism (charged props + optional B field).
    if (gX_Key_IsPressed(gX_KEY_M)) {
        gX_ScenePhysics_SetEM(scene, gfx, !scene->emEnabled);
    }

    //ai: P — fill/drain the pool (buoyancy/drag + the visible volume).
    if (gX_Key_IsPressed(gX_KEY_P)) {
        gX_ScenePhysics_SetWater(scene, gfx, scene->waterEnabled ? 0 : 1);
    }

    //ai: N — toggle the magnetic dipole forces (the bar magnets stay).
    if (gX_Key_IsPressed(gX_KEY_N)) {
        gX_ScenePhysics_SetMagnets(scene, scene->magnetsOn ? 0 : 1);
    }

    //ai: K — toggle the debug view (colliders, contacts, ground grid);
    //   enabling installs the shared event capture when nothing else did.
    if (gX_Key_IsPressed(gX_KEY_K)) {
        scene->debugDraw = scene->debugDraw ? 0 : 1;
        if (scene->debugDraw) {
            gX_ScenePhysics_EnableImpactCapture(scene);
            scene->sparksEnabled = scene->sparkEmitterCount > 0;
        }
        gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: debug view %s",
            scene->debugDraw ? "on" : "off");
    }

    //ai: H — heat the hovered prop or ragdoll, or everything when hovering
    //   nothing (bones share the heat energy; light limbs heat fastest).
    if (gX_Key_IsPressed(gX_KEY_H)) {
        if (hovered >= 0 && scene->props[hovered].body.internal != 0) {
            gX_PhysicsThermal_HeatBody(scene->props[hovered].body,
                scene->heatEnergy);
        } else if (hoveredRagdoll >= 0) {
            gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[hoveredRagdoll];
            for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
                gX_PhysicsThermal_HeatBody(slot->ragdoll.bones[b].body,
                    scene->heatEnergy);
            }
        } else {
            for (gX_I32 i = 0; i < scene->propCount; i++) {
                if (scene->props[i].body.internal == 0) continue;
                gX_PhysicsThermal_HeatBody(scene->props[i].body,
                    scene->heatEnergy);
            }
            for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
                gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
                for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
                    gX_PhysicsThermal_HeatBody(slot->ragdoll.bones[b].body,
                        scene->heatEnergy);
                }
            }
        }
    }

    //ai: L — launch the light-speed slug down the lane (the relativity
    //   demo; the applier's clamp and gamma mass take over from there).
    if (gX_Key_IsPressed(gX_KEY_L)) {
        gX_ScenePhysics_LaunchSlug(scene);
    }
}

//ai: R — teleport props and ragdolls back to their spawn points/poses and
//   return temperatures to the entry state.
gX_I0 gX_ScenePhysics_ResetContent(gX_ScenePhysics *scene) {
    gX_F32 restTemperature = gX_PhysicsThermal_GetSettings().ambientTemperature
        + scene->heatInitial;
    gX_ScenePhysics_UnpinAll(scene);
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0) continue;
        gX_PhysicsBody_SetPosition(prop->body, prop->spawnPos);
        gX_PhysicsBody_SetLinearVel(prop->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetAngularVel(prop->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_Enable(prop->body);
        gX_PhysicsThermal_SetTemperature(prop->body, restTemperature);
    }
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        gX_ScenePhysics_Link *link = &scene->links[i];
        if (link->body.internal == 0) continue;
        gX_PhysicsBody_SetPosition(link->body, link->spawnPos);
        //ai: links reset to their spawn orientation too (steered rover
        //   wheels carry their steering angle in the pose).
        gX_PhysicsBody_SetRotation(link->body, link->spawnQuat);
        gX_PhysicsBody_SetLinearVel(link->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetAngularVel(link->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_Enable(link->body);
    }
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal == 0) continue;
        mover->phase = 0.0f;
        gX_PhysicsBody_SetPosition(mover->body, mover->spawnPos);
        gX_PhysicsBody_SetRotation(mover->body, gX_F32Qua_Identity());
        gX_PhysicsBody_SetLinearVel(mover->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetAngularVel(mover->body, (gX_F32v3){0.0f, 0.0f, 0.0f});
    }
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_Ragdoll_ResetPose(&scene->ragdolls[i].ragdoll, physics.world);
        gX_Ragdoll_SetEnabled(&scene->ragdolls[i].ragdoll, physics.world, 1);
    }
    //ai: a broken break-target is rewelded after the pose reset (the fixed
    //   joint locks the reset pose); the pump drive restarts its cycle.
    if (scene->targetEnabled && scene->targetBroken) {
        if (gX_ScenePhysics_WeldTarget(scene) != 0) {
            gX_TraceLog(gX_LOG_WARNING,
                "PHYSICS_SCENE: target reweld failed");
        }
    }
    scene->pumpPhase = 0.0f;
    //ai: reset is a teleport — resnapshot so interpolation does not glide
    //   from the old pose.
    gX_ScenePhysics_Snapshot(scene);
}

//ai: body transforms → GFX model params. The renderer draws what we write.
//   Positions/quaternions blend from the pre-step snapshot by the module's
//   accumulator alpha so motion stays smooth when the display rate is not
//   the simulation rate (the module's GetAlpha finally has a consumer).
static gX_I0 gX_ScenePhysics_Sync(gX_ScenePhysics *scene, gX_RendererGFX *gfx) {
    if (!gfx->models.data || !gfx->models.meta) return;
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model *)gfx->models.data;
    gX_F32 alpha = gX_Physics_GetAlpha();
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0 || prop->modelIndex < 0) continue;
        if ((gX_U32)prop->modelIndex >= gfx->models.amount
            || !gfx->models.meta[prop->modelIndex].USED) {
            prop->modelIndex = -1;
            continue;
        }
        gX_RendererGFX_Model *model = &models[prop->modelIndex];
        gX_F32v3 pos = gX_F32v3_Lerp(prop->prevPos,
            gX_PhysicsBody_GetPosition(prop->body), alpha);
        gX_F32Qua quatNow;
        gX_PhysicsBody_GetRotation(prop->body, &quatNow);
        gX_F32Qua quat = gX_F32Qua_Slerp(prop->prevQuat, quatNow, alpha);
        gX_F32v3 euler = gX_F32Qua_ToEuler(quat);
        //ai: the model draws the collision shape, so a prop with a moved
        //   mass center (the balance posts) is placed back by the rotated
        //   offset — zero for everything else.
        gX_F32v3 com = gX_PhysicsBody_GetMassCenter(prop->body);
        if (com.x != 0.0f || com.y != 0.0f || com.z != 0.0f) {
            gX_F32v3 shapeShift = gX_F32v3_RotateByQuaternion(com, quat);
            pos.x -= shapeShift.x;
            pos.y -= shapeShift.y;
            pos.z -= shapeShift.z;
        }
        model->params.pos = pos;
        model->params.rotation = (gX_F32v3){
            euler.x * gX_RAD2DEG,
            euler.y * gX_RAD2DEG,
            euler.z * gX_RAD2DEG
        };
    }

    //ai: joint rig parts — same interpolation as props (chain links, door,
    //   spinner bar, rail carriage).
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        gX_ScenePhysics_Link *link = &scene->links[i];
        if (link->body.internal == 0 || link->modelIndex < 0) continue;
        if ((gX_U32)link->modelIndex >= gfx->models.amount
            || !gfx->models.meta[link->modelIndex].USED) {
            link->modelIndex = -1;
            continue;
        }
        gX_RendererGFX_Model *model = &models[link->modelIndex];
        gX_F32v3 pos = gX_F32v3_Lerp(link->prevPos,
            gX_PhysicsBody_GetPosition(link->body), alpha);
        gX_F32Qua quatNow;
        gX_PhysicsBody_GetRotation(link->body, &quatNow);
        gX_F32Qua quat = gX_F32Qua_Slerp(link->prevQuat, quatNow, alpha);
        gX_F32v3 euler = gX_F32Qua_ToEuler(quat);
        model->params.pos = pos;
        model->params.rotation = (gX_F32v3){
            euler.x * gX_RAD2DEG,
            euler.y * gX_RAD2DEG,
            euler.z * gX_RAD2DEG
        };
        //ai: the link's second model (wheel spokes) shares the transform.
        if (link->extraModel >= 0) {
            if ((gX_U32)link->extraModel >= gfx->models.amount
                || !gfx->models.meta[link->extraModel].USED) {
                link->extraModel = -1;
            } else {
                gX_RendererGFX_Model *extra = &models[link->extraModel];
                extra->params.pos = pos;
                extra->params.rotation = model->params.rotation;
            }
        }
    }

    //ai: kinematic movers — same interpolation as props.
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal == 0 || mover->modelIndex < 0) continue;
        if ((gX_U32)mover->modelIndex >= gfx->models.amount
            || !gfx->models.meta[mover->modelIndex].USED) {
            mover->modelIndex = -1;
            continue;
        }
        gX_RendererGFX_Model *model = &models[mover->modelIndex];
        gX_F32v3 pos = gX_F32v3_Lerp(mover->prevPos,
            gX_PhysicsBody_GetPosition(mover->body), alpha);
        gX_F32Qua quatNow;
        gX_PhysicsBody_GetRotation(mover->body, &quatNow);
        gX_F32Qua quat = gX_F32Qua_Slerp(mover->prevQuat, quatNow, alpha);
        gX_F32v3 euler = gX_F32Qua_ToEuler(quat);
        model->params.pos = pos;
        model->params.rotation = (gX_F32v3){
            euler.x * gX_RAD2DEG,
            euler.y * gX_RAD2DEG,
            euler.z * gX_RAD2DEG
        };
    }

    //ai: thermal glow — emissive ramp for heated props (per frame).
    gX_ScenePhysics_SyncGlow(scene, models);

    //ai: ragdoll limbs (scale is set once at spawn; transforms sync here).
    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            gX_I32 modelIndex = slot->boneModel[b];
            if (modelIndex < 0) continue;
            if ((gX_U32)modelIndex >= gfx->models.amount
                || !gfx->models.meta[modelIndex].USED) {
                slot->boneModel[b] = -1;
                continue;
            }
            gX_Ragdoll_Bone *bone = &slot->ragdoll.bones[b];
            if (bone->body.internal == 0) continue;
            gX_RendererGFX_Model *model = &models[modelIndex];
            gX_F32v3 pos = gX_F32v3_Lerp(slot->bonePrevPos[b],
                gX_PhysicsBody_GetPosition(bone->body), alpha);
            gX_F32Qua quatNow;
            gX_PhysicsBody_GetRotation(bone->body, &quatNow);
            gX_F32Qua quat = gX_F32Qua_Slerp(slot->bonePrevQuat[b], quatNow, alpha);
            gX_F32v3 euler = gX_F32Qua_ToEuler(quat);
            model->params.pos = pos;
            model->params.rotation = (gX_F32v3){
                euler.x * gX_RAD2DEG,
                euler.y * gX_RAD2DEG,
                euler.z * gX_RAD2DEG
            };

            //ai: bone thermal glow — the prop ramp without the molten state;
            //   cold bones keep their palette-tint base emissive.
            gX_F32 temperature = gX_PhysicsThermal_GetTemperature(bone->body);
            gX_F32 heat01 = (temperature - GX_SCENE_PHYSICS_GLOW_START)
                / (GX_SCENE_PHYSICS_GLOW_FULL - GX_SCENE_PHYSICS_GLOW_START);
            if (heat01 < 0.0f) heat01 = 0.0f;
            if (heat01 > 1.0f) heat01 = 1.0f;
            if (heat01 > 0.0f) {
                gX_F32v3 glow = gX_F32v3_Physics_Temp_BlackbodyRGB(temperature);
                model->params.emissiveColor = (gX_Color){
                    (gX_U8)(glow.x * 255.0f),
                    (gX_U8)(glow.y * 255.0f),
                    (gX_U8)(glow.z * 255.0f), 255
                };
                model->params.emissiveFactor = 0.10f + 0.9f * heat01;
            } else {
                model->params.emissiveColor =
                    gX_ScenePhysics_Tint((gX_I32)bone->colorIndex);
                model->params.emissiveFactor = 0.10f;
            }
        }
    }
}

gX_I32 gX_ScenePhysics_Load(gX_ScenePhysics *scene) {
    memset(scene, 0, sizeof(*scene));
    strcpy(scene->name, "[gX] Physics");
    strcpy(scene->description, "Rigid-body props sandbox (shared physics world)");
    scene->dragProp = -1;
    scene->dragLink = -1;
    scene->hoverProp = -1;
    scene->hoverLink = -1;
    scene->groundModelIndex = -1;
    scene->jointsEnabled = 1;
    scene->materialsEnabled = 1;
    scene->magnetScale = GX_SCENE_PHYSICS_MAGNET_SCALE;
    scene->loaded = true;
    gX_TraceLog(gX_LOG_INFO, "PHYSICS_SCENE: Loaded");
    return 0;
}

//ai: scene-switch exit. Destroying the content is what stops the shared
//   world from simulating the sandbox while another scene runs; the step
//   hook and the collision callback must go too. The ground collider is
//   static and persists for the next entry.
gX_I0 gX_ScenePhysics_Park(gX_ScenePhysics *scene) {
    if (!scene->loaded) return;
    gX_ScenePhysics_ClearContent(scene);
    gX_ScenePhysics_DisableImpactCapture(scene);
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (gfx) {
        gX_ScenePhysics_SparksRemove(scene, gfx);
        //ai: hand the decal pass back the way we found it.
        if (scene->decalsEnabled) {
            gfx->state.ENABLE_DECALS = scene->decalsPrev;
            scene->decalsEnabled = 0;
            scene->scorchTexture = -1;
        }
    }
    gX_Physics_SetStepHook(NULL, NULL);
    if (physics.world.internal != 0) {
        gX_PhysicsThermal_RemoveCollisionCallback(physics.world);
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: parked (content released)");
}

gX_I32 gX_ScenePhysics_Unload(gX_ScenePhysics *scene) {
    gX_ScenePhysics_Park(scene);
    if (scene->groundCollider.internal != 0 && physics.world.internal != 0) {
        gX_PhysicsCollider_Destroy(physics.world, scene->groundCollider);
    }
    scene->groundCollider = gX_PHYSICS_COLLIDER_NULL;
    scene->groundModelIndex = -1;
    //ai: the pool basin statics go with the scene (like the ground).
    for (gX_I32 i = 0; i < 4; i++) {
        if (scene->poolWall[i].internal != 0 && physics.world.internal != 0) {
            gX_PhysicsCollider_Destroy(physics.world, scene->poolWall[i]);
        }
        scene->poolWall[i] = gX_PHYSICS_COLLIDER_NULL;
    }
    if (scene->beltCollider.internal != 0 && physics.world.internal != 0) {
        gX_PhysicsCollider_Destroy(physics.world, scene->beltCollider);
    }
    scene->beltCollider = gX_PHYSICS_COLLIDER_NULL;
    //ai: the terrain mound goes with the scene (like the belt).
    if (scene->moundCollider.internal != 0 && physics.world.internal != 0) {
        gX_PhysicsCollider_Destroy(physics.world, scene->moundCollider);
    }
    scene->moundCollider = gX_PHYSICS_COLLIDER_NULL;
    scene->loaded = false;
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: Unloaded");
    return 0;
}

//ai: physics editor surface — defaults mirror the sandbox/applier defaults
//   so a fresh tuning is behaviorally identical to the env-seeded scene.
gX_ScenePhysics_Tuning gX_ScenePhysics_Tuning_Defaults(gX_I0) {
    gX_ScenePhysics_Tuning tuning;
    memset(&tuning, 0, sizeof(tuning));
    tuning.gravity = 9.81f;
    tuning.tuneLevel = -1;
    tuning.props = GX_SCENE_PHYSICS_DEFAULT_PROPS;
    tuning.ragdolls = 0;
    tuning.materials = 1;
    tuning.joints = 1;
    tuning.water = 1;
    tuning.magnetsOn = 0;
    tuning.sparks = 1;
    tuning.decals = 1;
    tuning.sfx = 1;
    tuning.relativity = 1;
    tuning.lightSpeed = gX_PhysicsRelativity_Defaults().lightSpeed;
    tuning.em = gX_PhysicsEM_Defaults();
    //ai: the sandbox's magnet strength rides em.dipoleScale (the scene's
    //   resting scale); magnetsOn only gates it.
    tuning.em.dipoleScale = GX_SCENE_PHYSICS_MAGNET_SCALE;
    tuning.thermal = gX_PhysicsThermal_Defaults();
    gX_PhysicsFluid_Settings fluid = gX_PhysicsFluid_Defaults();
    tuning.fluidDensity = fluid.density;
    tuning.fluidDrag = fluid.dragCoefficient;
    tuning.fluidLinearDrag = fluid.linearDrag;
    tuning.fluidAngularDrag = fluid.angularDrag;
    return tuning;
}

//ai: push a tuning into the live scene + shared appliers. Content counts
//   fill (props stagger to the target; ragdolls spawn the missing ones —
//   nothing despawns), the toggles rebuild/clear their content, and the
//   applier settings replace the module values. Pool geometry is scene
//   content and stays untouched.
gX_I0 gX_ScenePhysics_Tuning_Apply(gX_ScenePhysics *scene,
    const gX_ScenePhysics_Tuning *tuning) {
    if (!scene || !tuning) return;
    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (physics.world.internal != 0) {
        gX_PhysicsWorld_SetGravity(physics.world,
            (gX_F32v3){0.0f, -tuning->gravity, 0.0f});
    }
    physics.tuningPin = tuning->tuneLevel;
    if (tuning->tuneLevel >= 0) {
        physics.tuningLevel = tuning->tuneLevel;
        gX_Physics_ApplyTuning(tuning->tuneLevel);
    }
    if (tuning->props >= 1 && tuning->props <= GX_SCENE_PHYSICS_MAX_PROPS
        && scene->spawnTotal < tuning->props) {
        scene->spawnTotal = tuning->props;
    }
    gX_I32 ragdollTarget = tuning->ragdolls;
    if (ragdollTarget < 0) ragdollTarget = 0;
    if (ragdollTarget > GX_SCENE_PHYSICS_MAX_RAGDOLLS) {
        ragdollTarget = GX_SCENE_PHYSICS_MAX_RAGDOLLS;
    }
    if (gfx) {
        while (scene->ragdollCount < ragdollTarget) {
            if (gX_ScenePhysics_SpawnRagdoll(scene, gfx,
                    gX_ScenePhysics_RagdollSpawnPos(scene->ragdollSpawned))
                != 0) {
                break;
            }
        }
    }
    scene->materialsEnabled = tuning->materials ? 1 : 0;
    if ((tuning->joints ? 1 : 0) != scene->jointsEnabled) {
        gX_ScenePhysics_SetJoints(scene, tuning->joints);
    }
    if ((tuning->water ? 1 : 0) != scene->waterEnabled) {
        gX_ScenePhysics_SetWater(scene, gfx, tuning->water);
    }
    //ai: raw applier settings first, then the magnet gate — magnets-off must
    //   zero dipoleScale even while charges are on; the scale re-asserts
    //   after the raw load either way.
    scene->magnetScale = tuning->em.dipoleScale;
    gX_PhysicsEM_SetSettings(tuning->em);
    if ((tuning->magnetsOn ? 1 : 0) != scene->magnetsOn) {
        gX_ScenePhysics_SetMagnets(scene, tuning->magnetsOn);
    }
    gX_PhysicsEM_Settings emLive = gX_PhysicsEM_GetSettings();
    emLive.dipoleScale = scene->magnetsOn ? scene->magnetScale : 0.0f;
    gX_PhysicsEM_SetSettings(emLive);
    if (physics.world.internal != 0) {
        gX_PhysicsEM_SetEnabled(scene->emEnabled || scene->magnetsOn);
    }

    gX_PhysicsThermal_SetEnabled(tuning->thermal.enabled);
    gX_PhysicsThermal_SetSettings(tuning->thermal);
    //ai: relativity — the light-speed knob applies live; disabling the
    //   applier restores every registered body's rest mass.
    gX_PhysicsRelativity_Settings relativity =
        gX_PhysicsRelativity_GetSettings();
    relativity.lightSpeed = tuning->lightSpeed;
    gX_PhysicsRelativity_SetSettings(relativity);
    gX_PhysicsRelativity_SetEnabled(tuning->relativity);
    scene->relativityEnabled = tuning->relativity ? 1 : 0;
    gX_PhysicsFluid_Settings fluid = gX_PhysicsFluid_GetSettings();
    fluid.density = tuning->fluidDensity;
    fluid.dragCoefficient = tuning->fluidDrag;
    fluid.linearDrag = tuning->fluidLinearDrag;
    fluid.angularDrag = tuning->fluidAngularDrag;
    fluid.gravity = tuning->gravity;
    gX_PhysicsFluid_SetSettings(fluid);

    //ai: sparks/decals track the impact capture (shared with the debug
    //   view); textures/emitters only exist with a renderer, and the
    //   capture re-installs on transitions only (sparksEnabled keeps the
    //   burst pass semantics).
    gX_I1 wantCapture = (tuning->sparks || scene->debugDraw
        || tuning->decals || tuning->sfx) ? 1 : 0;
    if (wantCapture && !scene->impactCapture) {
        gX_ScenePhysics_EnableImpactCapture(scene);
    } else if (!wantCapture && scene->impactCapture) {
        gX_ScenePhysics_DisableImpactCapture(scene);
    }
    scene->sfxEnabled = tuning->sfx ? 1 : 0;
    scene->sparksEnabled = tuning->sparks ? 1 : 0;
    if (gfx) {
        if (tuning->sparks && scene->sparkEmitterCount == 0) {
            gX_ScenePhysics_SparksSetup(scene, gfx);
        } else if (!tuning->sparks && scene->sparkEmitterCount > 0) {
            gX_ScenePhysics_SparksRemove(scene, gfx);
        }
        if (tuning->decals && !scene->decalsEnabled) {
            if (scene->scorchTexture < 0) {
                scene->scorchTexture = gX_RendererGFX_Texture_Add(gfx,
                    GX_RESOURCE_DIR "/texture/misc/scorch.png");
            }
            scene->decalsPrev = gfx->state.ENABLE_DECALS;
            gfx->state.ENABLE_DECALS = 1;
            scene->decalsEnabled = 1;
        } else if (!tuning->decals && scene->decalsEnabled) {
            gfx->state.ENABLE_DECALS = scene->decalsPrev;
            scene->decalsEnabled = 0;
        }
    } else {
        scene->decalsEnabled = tuning->decals ? 1 : 0;
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_SCENE: tuning applied");
}

//ai: tuning JSON — flat, versioned, snake-free keys; vectors as components.
static struct json_object *gX_ScenePhysics_Tuning_ToObj(
    const gX_ScenePhysics_Tuning *tuning) {
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "gravity",
        json_object_new_double(tuning->gravity));
    json_object_object_add(root, "tuneLevel",
        json_object_new_int(tuning->tuneLevel));
    json_object_object_add(root, "props", json_object_new_int(tuning->props));
    json_object_object_add(root, "ragdolls",
        json_object_new_int(tuning->ragdolls));
    json_object_object_add(root, "materials",
        json_object_new_boolean(tuning->materials));
    json_object_object_add(root, "joints",
        json_object_new_boolean(tuning->joints));
    json_object_object_add(root, "water", json_object_new_boolean(tuning->water));
    json_object_object_add(root, "magnetsOn",
        json_object_new_boolean(tuning->magnetsOn));
    json_object_object_add(root, "sparks",
        json_object_new_boolean(tuning->sparks));
    json_object_object_add(root, "decals",
        json_object_new_boolean(tuning->decals));
    json_object_object_add(root, "sfx",
        json_object_new_boolean(tuning->sfx));
    json_object_object_add(root, "relativity",
        json_object_new_boolean(tuning->relativity));
    json_object_object_add(root, "lightSpeed",
        json_object_new_double(tuning->lightSpeed));
    struct json_object *em = json_object_new_object();
    json_object_object_add(em, "coulombScale",
        json_object_new_double(tuning->em.coulombScale));
    json_object_object_add(em, "softening",
        json_object_new_double(tuning->em.softening));
    json_object_object_add(em, "maxForce",
        json_object_new_double(tuning->em.maxForce));
    json_object_object_add(em, "bX",
        json_object_new_double(tuning->em.uniformB.x));
    json_object_object_add(em, "bY",
        json_object_new_double(tuning->em.uniformB.y));
    json_object_object_add(em, "bZ",
        json_object_new_double(tuning->em.uniformB.z));
    json_object_object_add(em, "dipoleScale",
        json_object_new_double(tuning->em.dipoleScale));
    json_object_object_add(em, "susceptibilityScale",
        json_object_new_double(tuning->em.susceptibilityScale));
    json_object_object_add(root, "em", em);
    struct json_object *thermal = json_object_new_object();
    json_object_object_add(thermal, "ambientTemperature",
        json_object_new_double(tuning->thermal.ambientTemperature));
    json_object_object_add(thermal, "heatingScale",
        json_object_new_double(tuning->thermal.heatingScale));
    json_object_object_add(thermal, "coolingScale",
        json_object_new_double(tuning->thermal.coolingScale));
    json_object_object_add(thermal, "contactConductance",
        json_object_new_double(tuning->thermal.contactConductance));
    json_object_object_add(thermal, "maxTemperature",
        json_object_new_double(tuning->thermal.maxTemperature));
    json_object_object_add(thermal, "minImpactSpeed",
        json_object_new_double(tuning->thermal.minImpactSpeed));
    json_object_object_add(thermal, "enabled",
        json_object_new_boolean(tuning->thermal.enabled));
    json_object_object_add(root, "thermal", thermal);
    json_object_object_add(root, "fluidDensity",
        json_object_new_double(tuning->fluidDensity));
    json_object_object_add(root, "fluidDrag",
        json_object_new_double(tuning->fluidDrag));
    json_object_object_add(root, "fluidLinearDrag",
        json_object_new_double(tuning->fluidLinearDrag));
    json_object_object_add(root, "fluidAngularDrag",
        json_object_new_double(tuning->fluidAngularDrag));
    return root;
}

//ai: read a numeric/boolean key when present (missing/typed keys keep the
//   default), so older or hand-edited presets degrade gracefully.
static gX_F32 gX_ScenePhysics_Tuning_F32(struct json_object *obj,
    const char *key, gX_F32 fallback) {
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(obj, key, &value) || !value) return fallback;
    enum json_type type = json_object_get_type(value);
    if (type == json_type_double || type == json_type_int) {
        return (gX_F32)json_object_get_double(value);
    }
    return fallback;
}

static gX_I32 gX_ScenePhysics_Tuning_I32(struct json_object *obj,
    const char *key, gX_I32 fallback) {
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(obj, key, &value) || !value) return fallback;
    enum json_type type = json_object_get_type(value);
    if (type == json_type_int) return json_object_get_int(value);
    if (type == json_type_double) {
        return (gX_I32)json_object_get_double(value);
    }
    return fallback;
}

static gX_I1 gX_ScenePhysics_Tuning_Bool(struct json_object *obj,
    const char *key, gX_I1 fallback) {
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(obj, key, &value) || !value) return fallback;
    if (json_object_get_type(value) == json_type_boolean) {
        return json_object_get_boolean(value) ? 1 : 0;
    }
    return fallback;
}

static gX_I0 gX_ScenePhysics_Tuning_FromObj(struct json_object *root,
    gX_ScenePhysics_Tuning *tuning) {
    tuning->gravity = gX_ScenePhysics_Tuning_F32(root, "gravity",
        tuning->gravity);
    tuning->tuneLevel = gX_ScenePhysics_Tuning_I32(root, "tuneLevel",
        tuning->tuneLevel);
    tuning->props = gX_ScenePhysics_Tuning_I32(root, "props", tuning->props);
    tuning->ragdolls = gX_ScenePhysics_Tuning_I32(root, "ragdolls",
        tuning->ragdolls);
    tuning->materials = gX_ScenePhysics_Tuning_Bool(root, "materials",
        tuning->materials);
    tuning->joints = gX_ScenePhysics_Tuning_Bool(root, "joints",
        tuning->joints);
    tuning->water = gX_ScenePhysics_Tuning_Bool(root, "water", tuning->water);
    tuning->magnetsOn = gX_ScenePhysics_Tuning_Bool(root, "magnetsOn",
        tuning->magnetsOn);
    tuning->sparks = gX_ScenePhysics_Tuning_Bool(root, "sparks",
        tuning->sparks);
    tuning->decals = gX_ScenePhysics_Tuning_Bool(root, "decals",
        tuning->decals);
    tuning->sfx = gX_ScenePhysics_Tuning_Bool(root, "sfx", tuning->sfx);
    tuning->relativity = gX_ScenePhysics_Tuning_Bool(root, "relativity",
        tuning->relativity);
    tuning->lightSpeed = gX_ScenePhysics_Tuning_F32(root, "lightSpeed",
        tuning->lightSpeed);
    struct json_object *em = NULL;
    if (json_object_object_get_ex(root, "em", &em) && em
        && json_object_get_type(em) == json_type_object) {
        tuning->em.coulombScale = gX_ScenePhysics_Tuning_F32(em,
            "coulombScale", tuning->em.coulombScale);
        tuning->em.softening = gX_ScenePhysics_Tuning_F32(em, "softening",
            tuning->em.softening);
        tuning->em.maxForce = gX_ScenePhysics_Tuning_F32(em, "maxForce",
            tuning->em.maxForce);
        tuning->em.uniformB.x = gX_ScenePhysics_Tuning_F32(em, "bX",
            tuning->em.uniformB.x);
        tuning->em.uniformB.y = gX_ScenePhysics_Tuning_F32(em, "bY",
            tuning->em.uniformB.y);
        tuning->em.uniformB.z = gX_ScenePhysics_Tuning_F32(em, "bZ",
            tuning->em.uniformB.z);
        tuning->em.dipoleScale = gX_ScenePhysics_Tuning_F32(em,
            "dipoleScale", tuning->em.dipoleScale);
        tuning->em.susceptibilityScale = gX_ScenePhysics_Tuning_F32(em,
            "susceptibilityScale", tuning->em.susceptibilityScale);
    }
    struct json_object *thermal = NULL;
    if (json_object_object_get_ex(root, "thermal", &thermal) && thermal
        && json_object_get_type(thermal) == json_type_object) {
        tuning->thermal.ambientTemperature = gX_ScenePhysics_Tuning_F32(
            thermal, "ambientTemperature", tuning->thermal.ambientTemperature);
        tuning->thermal.heatingScale = gX_ScenePhysics_Tuning_F32(thermal,
            "heatingScale", tuning->thermal.heatingScale);
        tuning->thermal.coolingScale = gX_ScenePhysics_Tuning_F32(thermal,
            "coolingScale", tuning->thermal.coolingScale);
        tuning->thermal.contactConductance = gX_ScenePhysics_Tuning_F32(
            thermal, "contactConductance", tuning->thermal.contactConductance);
        tuning->thermal.maxTemperature = gX_ScenePhysics_Tuning_F32(thermal,
            "maxTemperature", tuning->thermal.maxTemperature);
        tuning->thermal.minImpactSpeed = gX_ScenePhysics_Tuning_F32(thermal,
            "minImpactSpeed", tuning->thermal.minImpactSpeed);
        tuning->thermal.enabled = gX_ScenePhysics_Tuning_Bool(thermal,
            "enabled", tuning->thermal.enabled);
    }
    tuning->fluidDensity = gX_ScenePhysics_Tuning_F32(root, "fluidDensity",
        tuning->fluidDensity);
    tuning->fluidDrag = gX_ScenePhysics_Tuning_F32(root, "fluidDrag",
        tuning->fluidDrag);
    tuning->fluidLinearDrag = gX_ScenePhysics_Tuning_F32(root,
        "fluidLinearDrag", tuning->fluidLinearDrag);
    tuning->fluidAngularDrag = gX_ScenePhysics_Tuning_F32(root,
        "fluidAngularDrag", tuning->fluidAngularDrag);
}

gX_I32 gX_ScenePhysics_Tuning_Save(const gX_I8 *path,
    const gX_ScenePhysics_Tuning *tuning) {
    if (!path || !tuning || !path[0]) return -1;
    if (getenv("GX_NO_SETTINGS_SAVE")
        && strstr((const char *)path, "resources/tools/")) {
        gX_TraceLog(gX_LOG_ERROR,
            "PHYSICS_TUNING: refusing shipped preset save: %s", path);
        return -1;
    }
    struct json_object *root = gX_ScenePhysics_Tuning_ToObj(tuning);
    if (!root) return -1;
    gX_I32 flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY
        | JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext((const char *)path, root, flags);
    json_object_put(root);
    return ret == 0 ? 0 : -1;
}

gX_I32 gX_ScenePhysics_Tuning_Load(const gX_I8 *path,
    gX_ScenePhysics_Tuning *out) {
    if (!path || !out || !path[0]) return -1;
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root
        || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        return -1;
    }
    *out = gX_ScenePhysics_Tuning_Defaults();
    gX_ScenePhysics_Tuning_FromObj(root, out);
    json_object_put(root);
    return 0;
}

gX_I0 gX_ScenePhysics_Update(gX_ScenePhysics *scene) {
    if (!scene->loaded) return;

    gX_ScenePhysics_Ensure(scene);

    gX_RendererGFX *gfx = gX_ScenePhysics_GFX();
    if (!gfx || !gfx->common.physicsScene) return;

    gX_ScenePhysics_Interact(scene, gfx, scene->toolMode ? 0 : 1);
    gX_ScenePhysics_SpawnNext(scene, gfx);
    gX_ScenePhysics_Sync(scene, gfx);
    gX_ScenePhysics_ImpactUpdate(scene, gfx);

    if (gX_Key_IsPressed(gX_KEY_R)) {
        gX_ScenePhysics_ResetContent(scene);
    }
}

//ai: HUD — a compact status panel drawn in the scene overlay after the
//   renderer composites. Shows world/step state, the hovered prop's
//   element + thermal state, the hottest prop, and the key legend.
static gX_F32 gX_ScenePhysics_HudScale(gX_I0) {
    gX_F32 widthScale = (gX_F32)maindata.screenWidth / 1024.0f;
    gX_F32 heightScale = (gX_F32)maindata.screenHeight / 768.0f;
    return fminf(widthScale, heightScale);
}

static gX_I0 gX_ScenePhysics_HudText(const gX_I8 *text, gX_F32 x, gX_F32 y,
    gX_F32 size, gX_Color color) {
    gX_Batch_Flush();
    gX_RawString_Draw_Lesser(text, (gX_F32v2){x, y}, size, color);
}

//ai: one prop slot as a line — element symbol, temperature, phase, charge
//   when EM is on. A dead/absent slot renders "-".
static gX_I0 gX_ScenePhysics_PropLine(gX_ScenePhysics *scene, gX_I32 index,
    gX_I8 *out, gX_U32 size) {
    if (index < 0 || index >= scene->propCount
        || scene->props[index].body.internal == 0) {
        snprintf((char *)out, size, "-");
        return;
    }
    gX_ScenePhysics_Prop *prop = &scene->props[index];
    const gX_Element_Info *info = gX_Elements_Get(prop->element);
    gX_F32 temperature = gX_PhysicsThermal_GetTemperature(prop->body);
    gX_F32 melt = gX_PhysicsThermal_MeltFraction(prop->body);
    const gX_I8 *phase = "SOLID";
    if (gX_PhysicsThermal_IsMolten(prop->body)) phase = "MOLTEN";
    else if (melt > 0.001f) phase = "MELTING";
    //ai: nominal contact material readout (element table values).
    gX_I8 material[32] = "";
    if (scene->materialsEnabled && info) {
        snprintf((char *)material, sizeof(material), " mu %.2f e %.2f",
            info->friction, info->restitution);
    }
    if (scene->emEnabled) {
        snprintf((char *)out, size, "%s %.0f K %s %.0f%% Q %+.0f%s",
            info ? info->symbol : "?", temperature, phase, melt * 100.0f,
            gX_ScenePhysics_PropCharge(index), material);
    } else {
        snprintf((char *)out, size, "%s %.0f K %s %.0f%%%s",
            info ? info->symbol : "?", temperature, phase, melt * 100.0f,
            material);
    }
}

//ai: debug view — wireframe colliders (green props, red while dragged,
//   orange bones), the current frame's contact points (cyan cubes) and
//   normals (yellow lines), plus a ground grid. Drawn in the scene overlay
//   after the renderer with its own 3D state block.
static gX_I0 gX_ScenePhysics_DrawDebug(gX_ScenePhysics *scene) {
    gX_Drawing_Begin3D(camera.camera3D);
    gX_Grid_Draw(24, 1.0f);

    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0) continue;
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(prop->body);
        gX_F32v3 com = gX_PhysicsBody_GetMassCenter(prop->body);
        if (com.x != 0.0f || com.y != 0.0f || com.z != 0.0f) {
            gX_F32Qua quat;
            gX_PhysicsBody_GetRotation(prop->body, &quat);
            gX_F32v3 shift = gX_F32v3_RotateByQuaternion(com, quat);
            pos.x -= shift.x;
            pos.y -= shift.y;
            pos.z -= shift.z;
        }
        gX_Color color = (scene->dragProp == i)
            ? (gX_Color){255, 96, 64, 220}
            : (gX_Color){96, 220, 128, 200};
        if (prop->kind == gX_SCENE_PHYSICS_PROP_SPHERE) {
            gX_Sphere_Draw((gX_Sphere){pos, prop->size.x}, color);
        } else {
            //ai: cylinders draw as their bounding box (a debug approximation).
            gX_Cube_Draw((gX_Cube){pos, prop->size}, color);
        }
    }

    //ai: joint rig parts (steel wireframes) and joint anchors/axes — anchors
    //   read like contact cubes, hinge/slider axes like contact normals.
    for (gX_I32 i = 0; i < scene->linkCount; i++) {
        gX_ScenePhysics_Link *link = &scene->links[i];
        if (link->body.internal == 0) continue;
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(link->body);
        gX_Color color = (scene->dragLink == i)
            ? (gX_Color){255, 96, 64, 220}
            : (gX_Color){192, 202, 220, 190};
        gX_Cube_Draw((gX_Cube){pos, link->size}, color);
    }
    for (gX_I32 i = 0; i < scene->jointCount; i++) {
        gX_ScenePhysics_JointInfo *joint = &scene->joints[i];
        if (joint->joint.internal == 0) continue;
        //ai: the tracked anchor is a build-time snapshot; moving rigs (the
        //   rover, the pump) need the live one.
        gX_F32v3 anchor = gX_PhysicsJoint_GetAnchor(joint->joint);
        if (anchor.x == 0.0f && anchor.y == 0.0f && anchor.z == 0.0f) {
            anchor = joint->anchor;
        }
        gX_Cube_Draw((gX_Cube){anchor, {0.1f, 0.1f, 0.1f}},
            (gX_Color){96, 224, 255, 255});
        if (joint->kind != gX_SCENE_PHYSICS_JOINT_BALL) {
            gX_F32v3 tip = {
                anchor.x + joint->axis.x * 0.9f,
                anchor.y + joint->axis.y * 0.9f,
                anchor.z + joint->axis.z * 0.9f
            };
            gX_Line3D_Draw((gX_Line3D){anchor, tip, 0.03f},
                (gX_Color){255, 224, 96, 220});
        }
    }

    //ai: toolgun pins — the gun's only joints, otherwise invisible.
    for (gX_I32 i = 0; i < scene->pinCount; i++) {
        gX_Cube_Draw((gX_Cube){scene->pins[i].anchor, {0.12f, 0.12f, 0.12f}},
            (gX_Color){255, 220, 127, 255});
    }

    //ai: kinematic movers — driven platforms (orange wireframes).
    for (gX_I32 i = 0; i < scene->moverCount; i++) {
        gX_ScenePhysics_Mover *mover = &scene->movers[i];
        if (mover->body.internal == 0) continue;
        gX_F32v3 size = mover->isTable
            ? (gX_F32v3){GX_SCENE_PHYSICS_TABLE_SIZE,
                GX_SCENE_PHYSICS_TABLE_THICK, GX_SCENE_PHYSICS_TABLE_SIZE}
            : (gX_F32v3){GX_SCENE_PHYSICS_LIFT_SIZE,
                GX_SCENE_PHYSICS_LIFT_THICK, GX_SCENE_PHYSICS_LIFT_SIZE};
        gX_Cube_Draw((gX_Cube){gX_PhysicsBody_GetPosition(mover->body), size},
            (gX_Color){255, 176, 64, 210});
    }

    //ai: balance posts — the mass-center marker (yellow dot at the body
    //   origin, the balance point) plus the offset line from the shape
    //   center, so the top-heavy/centered/bottom-heavy difference reads.
    for (gX_I32 i = 0; i < scene->balanceCount; i++) {
        gX_I32 slot = scene->balance[i];
        if (slot < 0 || slot >= scene->propCount) continue;
        gX_ScenePhysics_Prop *prop = &scene->props[slot];
        if (prop->body.internal == 0) continue;
        gX_F32v3 com = gX_PhysicsBody_GetPosition(prop->body);
        gX_F32Qua quat;
        gX_PhysicsBody_GetRotation(prop->body, &quat);
        gX_F32v3 localCom = gX_PhysicsBody_GetMassCenter(prop->body);
        gX_F32v3 shift = gX_F32v3_RotateByQuaternion(localCom, quat);
        gX_F32v3 shapeCenter = {com.x - shift.x, com.y - shift.y,
            com.z - shift.z};
        gX_Cube_Draw((gX_Cube){com, {0.09f, 0.09f, 0.09f}},
            (gX_Color){255, 224, 96, 255});
        gX_Line3D_Draw((gX_Line3D){shapeCenter, com, 0.02f},
            (gX_Color){255, 224, 96, 160});
    }

    for (gX_I32 i = 0; i < scene->ragdollCount; i++) {
        gX_ScenePhysics_RagdollSlot *slot = &scene->ragdolls[i];
        for (gX_U32 b = 0; b < slot->ragdoll.boneCount; b++) {
            gX_Ragdoll_Bone *bone = &slot->ragdoll.bones[b];
            if (bone->body.internal == 0) continue;
            gX_F32v3 pos = gX_PhysicsBody_GetPosition(bone->body);
            gX_Cube_Draw((gX_Cube){pos, bone->renderScale},
                (gX_Color){255, 168, 72, 180});
        }
    }

    //ai: bar magnets draw their world dipole axis (red = moment direction,
    //   blue = opposite) so alignment reads in the debug view.
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        gX_ScenePhysics_Prop *prop = &scene->props[i];
        if (prop->body.internal == 0) continue;
        if (prop->dipole.x == 0.0f && prop->dipole.y == 0.0f
            && prop->dipole.z == 0.0f) continue;
        gX_F32v3 moment;
        if (!gX_PhysicsEM_GetDipole(prop->body, &moment)) continue;
        gX_F32 length = sqrtf(moment.x * moment.x + moment.y * moment.y
            + moment.z * moment.z);
        if (length <= 1e-6f) continue;
        gX_F32 scale = 0.5f / length;
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(prop->body);
        gX_F32v3 tip = { pos.x + moment.x * scale, pos.y + moment.y * scale,
            pos.z + moment.z * scale };
        gX_F32v3 back = { pos.x - moment.x * scale, pos.y - moment.y * scale,
            pos.z - moment.z * scale };
        gX_Line3D_Draw((gX_Line3D){pos, tip, 0.03f},
            (gX_Color){255, 96, 96, 255});
        gX_Line3D_Draw((gX_Line3D){back, pos, 0.03f},
            (gX_Color){96, 140, 255, 255});
    }

    for (gX_I32 i = 0; i < GX_SCENE_PHYSICS_DEBUG_CONTACTS; i++) {
        gX_ScenePhysics_Contact *contact = &scene->debugContacts[i];
        if (contact->frame != physics.frameCount) continue;
        gX_Cube_Draw((gX_Cube){contact->pos, {0.08f, 0.08f, 0.08f}},
            (gX_Color){96, 224, 255, 255});
        gX_F32v3 tip = {
            contact->pos.x + contact->normal.x * 0.4f,
            contact->pos.y + contact->normal.y * 0.4f,
            contact->pos.z + contact->normal.z * 0.4f
        };
        gX_Line3D_Draw((gX_Line3D){contact->pos, tip, 0.02f},
            (gX_Color){255, 224, 96, 255});
    }

    //ai: water volume wireframe (cyan) — the fluid region bounds.
    if (scene->waterEnabled) {
        gX_Cube_Draw((gX_Cube){
            {GX_SCENE_PHYSICS_POOL_CENTER_X,
                GX_SCENE_PHYSICS_POOL_SURFACE * 0.5f,
                GX_SCENE_PHYSICS_POOL_CENTER_Z},
            {GX_SCENE_PHYSICS_POOL_SIZE, GX_SCENE_PHYSICS_POOL_SURFACE,
                GX_SCENE_PHYSICS_POOL_SIZE}},
            (gX_Color){96, 200, 255, 120});
    }

    //ai: belt direction arrow (yellow) along the surface velocity.
    if (scene->beltCollider.internal != 0) {
        gX_F32v3 tail = {GX_SCENE_PHYSICS_BELT_CENTER_X - 1.6f,
            GX_SCENE_PHYSICS_BELT_HEIGHT + 0.12f,
            GX_SCENE_PHYSICS_BELT_CENTER_Z};
        gX_F32v3 head = {GX_SCENE_PHYSICS_BELT_CENTER_X + 1.6f,
            GX_SCENE_PHYSICS_BELT_HEIGHT + 0.12f,
            GX_SCENE_PHYSICS_BELT_CENTER_Z};
        gX_Line3D_Draw((gX_Line3D){tail, head, 0.05f},
            (gX_Color){255, 224, 96, 220});
        gX_Cube_Draw((gX_Cube){head, {0.12f, 0.12f, 0.12f}},
            (gX_Color){255, 224, 96, 220});
    }

    gX_Drawing_End3D();
}

//ai: live charge carriers (element props) — buoys and bar magnets do not
//   count as charges.
static gX_I32 gX_ScenePhysics_ChargeCount(gX_ScenePhysics *scene) {
    if (!scene->emEnabled) return 0;
    gX_I32 count = 0;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal == 0) continue;
        if (scene->props[i].element == gX_ELEMENT_NONE) continue;
        count++;
    }
    return count;
}

gX_I0 gX_ScenePhysics_Draw(gX_ScenePhysics *scene) {
    if (!scene->loaded) return;
    if (scene->debugDraw) gX_ScenePhysics_DrawDebug(scene);
    gX_F32 scale = gX_ScenePhysics_HudScale();
    //ai: the key legend cannot shrink below the pixel font's floor, so the
    //   HUD stops scaling once the panel would fall under it (probes at
    //   800x600 through 1920x1080 keep the legend inside from 0.95 up).
    scale = fmaxf(scale, 0.95f);
    gX_F32 x = (gX_F32)maindata.screenWidth * 0.02f;
    gX_F32 y = (gX_F32)maindata.screenHeight * 0.02f;
    gX_Rectangle panel = {x, y, 660.0f * scale,
        (scene->relativityEnabled ? 118.0f : 96.0f) * scale};
    gX_Rectangle_Draw(panel, (gX_Color){4, 10, 24, 216});
    gX_Rectangle_Draw_Border(panel, (gX_Color){86, 199, 196, 190});

    gX_I8 line[192];
    gX_I8 hoverLine[64];
    gX_I8 hotLine[64];

    //ai: hottest prop + sleeper count — walked every frame while the HUD is
    //   visible.
    gX_I32 hottest = -1;
    gX_F32 hottestTemperature = -1.0f;
    gX_I32 asleep = 0;
    for (gX_I32 i = 0; i < scene->propCount; i++) {
        if (scene->props[i].body.internal == 0) continue;
        if (!gX_PhysicsBody_IsEnabled(scene->props[i].body)) asleep++;
        gX_F32 temperature = gX_PhysicsThermal_GetTemperature(scene->props[i].body);
        if (temperature > hottestTemperature) {
            hottestTemperature = temperature;
            hottest = i;
        }
    }
    gX_ScenePhysics_PropLine(scene, scene->hoverProp, hoverLine, sizeof(hoverLine));
    if (scene->hoverProp < 0 && scene->hoverLink >= 0) {
        snprintf((char *)hoverLine, sizeof(hoverLine), "RIG");
    }
    gX_ScenePhysics_PropLine(scene, hottest, hotLine, sizeof(hotLine));

    snprintf((char *)line, sizeof(line),
        "PHYSICS // %d PROPS // %d RAGDOLLS // %d JOINTS // %d CHARGES // %d ASLEEP",
        scene->liveCount, scene->ragdollCount, scene->jointCount,
        gX_ScenePhysics_ChargeCount(scene), asleep);
    gX_ScenePhysics_HudText(line, x + 10.0f * scale, y + 8.0f * scale,
        17.0f * scale, (gX_Color){133, 224, 238, 255});

    snprintf((char *)line, sizeof(line),
        "%d STEP%s @ 60 HZ // TUNE %d // WORLD %s // EM %s // WATER %s // MAG %s%s",
        physics.stepsLastFrame, physics.stepsLastFrame == 1 ? "" : "S",
        (gX_I32)physics.tuningLevel,
        physics.enabled ? "LIVE" : "STUB", scene->emEnabled ? "ON" : "OFF",
        scene->waterEnabled ? "ON" : "OFF", scene->magnetsOn ? "ON" : "OFF",
        scene->debugDraw ? " // DEBUG" : "");
    gX_ScenePhysics_HudText(line, x + 10.0f * scale, y + 32.0f * scale,
        13.0f * scale, (gX_Color){190, 207, 219, 255});

    snprintf((char *)line, sizeof(line), "HOVER %s // HOTTEST %s",
        hoverLine, hotLine);
    gX_ScenePhysics_HudText(line, x + 10.0f * scale, y + 52.0f * scale,
        13.0f * scale, (gX_Color){255, 220, 127, 255});

    gX_ScenePhysics_HudText(
        "[LMB] DRAG [RMB] REMOVE [T] RAGDOLL [B] BALANCE [M] EM [H] HEAT [P] WATER [N] MAGNETS [K] DEBUG [R] RESET",
        x + 10.0f * scale, y + 72.0f * scale,
        12.0f * scale, (gX_Color){155, 185, 203, 255});

    if (scene->relativityEnabled) {
        //ai: the light-speed readout — the slug's beta/gamma/mass straight
        //   from the applier (the mass is the dilated solver value).
        gX_PhysicsRelativity_Settings relativity =
            gX_PhysicsRelativity_GetSettings();
        gX_PhysicsBody slug = gX_PHYSICS_BODY_NULL;
        if (scene->slugProp >= 0 && scene->slugProp < scene->propCount) {
            slug = scene->props[scene->slugProp].body;
        }
        gX_F32 beta = 0.0f;
        gX_F32 gamma = 1.0f;
        gX_F32 mass = 0.0f;
        if (slug.internal != 0
            && gX_PhysicsRelativity_GetBodyState(slug, &beta, &gamma, &mass)) {
            snprintf((char *)line, sizeof(line),
                "LIGHT c %.1f M/S // SLUG %.2f C GAMMA %.2f M %.2f T // [L] LAUNCH",
                relativity.lightSpeed, beta, gamma, mass);
        } else {
            snprintf((char *)line, sizeof(line),
                "LIGHT c %.1f M/S // [L] LAUNCH", relativity.lightSpeed);
        }
        gX_ScenePhysics_HudText(line, x + 10.0f * scale, y + 92.0f * scale,
            12.0f * scale, (gX_Color){150, 216, 255, 255});
    }
}
