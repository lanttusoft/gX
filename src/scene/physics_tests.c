/**********************************************************************************************
*
*   [gX: src/scene/physics_tests.c]
*   Physics sandbox scene tests — park/exit lifecycle
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


//ai: sandbox lifecycle test. Leaving the scene must destroy the sandbox
//   bodies and clear the EM/thermal appliers so nothing simulates while
//   another scene runs; the shared world itself must stay alive.
#include "physics.h"

#include "../common/log.h"
#include "../gxmath/quaternion.h"
#include "../gxmath/vector3.h"
#include "../physics/elements.h"
#include "../physics/fluid.h"
#include "../physics/physics.h"
#include "../physics/em.h"
#include "../physics/thermal.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <math.h>

gX_I32 gX_Tests_ScenePhysics(gX_I0);
//ai: impact-audio palette mapping (pure; no world/renderer/audio device)
gX_I32 gX_Tests_SceneImpactSound(gX_I0);
//ai: save-guard tests preserve the complete inherited environment value and leave it untouched on snapshot failure.
gX_I32 gX_Tests_ScenePhysics(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;  //ai: ODE unavailable — nothing to verify

    //ai: Park/ClearContent sweep the module's shared world; swap in an
    //   isolated one for the test and restore it afterwards (tests run
    //   before the module is loaded, so the saved world is empty).
    gX_PhysicsWorld savedWorld = physics.world;
    physics.world = world;

    gX_ScenePhysics sceneLocal;
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsBody propBody = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.0f, 5.0f, 0.0f});
    if (propBody.internal == 0) errors++;
    sceneLocal.props[0].body = propBody;
    sceneLocal.props[0].modelIndex = -1;
    sceneLocal.propCount = 1;
    sceneLocal.liveCount = 1;
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetEnabled(1);
    gX_PhysicsEM_AddBody(propBody, 1.0f);
    gX_PhysicsThermal_Reset();
    gX_PhysicsThermal_SetEnabled(1);
    gX_PhysicsThermal_AddBody(propBody, 300.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f);

    gX_ScenePhysics_Park(&sceneLocal);
    if (sceneLocal.propCount != 0 || sceneLocal.liveCount != 0
        || sceneLocal.ragdollCount != 0 || sceneLocal.dragActive) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics park kept scene slots");
        errors++;
    }
    if (gX_PhysicsEM_Count() != 0 || gX_PhysicsThermal_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics park kept appliers");
        errors++;
    }
    //ai: the shared world must stay alive and steppable after the sweep.
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: impact capture — a prop dropped onto the ground must queue one
    //   spark impact with positive energy at the contact point.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_ScenePhysics_EnableImpactCapture(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsBody drop = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 2.0f, (gX_F32v3){0.0f, 6.0f, 0.0f});
    sceneLocal.props[0].body = drop;
    sceneLocal.props[0].modelIndex = -1;
    sceneLocal.propCount = 1;
    sceneLocal.liveCount = 1;
    //ai: charge the drop so the impact entry carries the arc flag (slot 0
    //   props are +1 under the alternation).
    sceneLocal.emEnabled = 1;
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetEnabled(1);
    gX_PhysicsEM_AddBody(drop, 1.0f);
    //ai: the debug view records contacts from the same event stream.
    sceneLocal.debugDraw = 1;
    for (gX_I32 i = 0; i < 120; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    if (sceneLocal.debugContactWrite == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: debug contact capture stayed empty");
        errors++;
    }
    sceneLocal.debugDraw = 0;
    if (gX_ScenePhysics_ImpactCount(&sceneLocal) < 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact capture saw no landing");
        errors++;
    } else {
        gX_ScenePhysics_Impact impact;
        if (!gX_ScenePhysics_ImpactGet(&sceneLocal, 0, &impact)
            || !(impact.energy > 0.0f) || !(impact.position.y < 1.5f)
            || impact.charge == 0.0f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: impact entry energy=%.1f y=%.2f charge=%.2f",
                impact.energy, impact.position.y, impact.charge);
            errors++;
        }
    }
    gX_ScenePhysics_DisableImpactCapture(&sceneLocal);
    if (gX_ScenePhysics_ImpactCount(&sceneLocal) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact capture kept its queue");
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: ragdoll appliers — a ragdoll's bones register with the active
    //   EM/thermal registries (slot charge sign, tissue material, per-bone
    //   mass) and unregister on teardown.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetEnabled(1);
    gX_PhysicsThermal_Reset();
    gX_PhysicsThermal_SetEnabled(1);
    gX_ScenePhysics_RagdollSlot ragdollSlot;
    memset(&ragdollSlot, 0, sizeof(ragdollSlot));
    for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
        ragdollSlot.boneModel[b] = -1;
    }
    if (gX_Ragdoll_CreateHumanoid(&ragdollSlot.ragdoll, world,
            (gX_F32v3){4.0f, 0.5f, 0.0f}, 1.0f) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll create for appliers");
        errors++;
    } else {
        ragdollSlot.charge = 1.0f;
        gX_ScenePhysics_RagdollRegister(&sceneLocal, &ragdollSlot);
        gX_I32 bones = (gX_I32)ragdollSlot.ragdoll.boneCount;
        if (gX_PhysicsEM_Count() != bones || gX_PhysicsThermal_Count() != bones) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: ragdoll appliers em=%d thermal=%d bones=%d",
                gX_PhysicsEM_Count(), gX_PhysicsThermal_Count(), bones);
            errors++;
        }
        //ai: heating one bone must raise it above ambient.
        gX_PhysicsBody hand =
            ragdollSlot.ragdoll.bones[GX_RAGDOLL_BONE_ARM_LOWER_L].body;
        gX_F32 before = gX_PhysicsThermal_GetTemperature(hand);
        gX_PhysicsThermal_HeatBody(hand, 5.0e5f);
        if (!(gX_PhysicsThermal_GetTemperature(hand) > before)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll bone heat");
            errors++;
        }
        gX_ScenePhysics_RagdollUnregister(&sceneLocal, &ragdollSlot);
        if (gX_PhysicsEM_Count() != 0 || gX_PhysicsThermal_Count() != 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll unregister left entries");
            errors++;
        }
        gX_Ragdoll_Destroy(&ragdollSlot.ragdoll, world);
    }
    gX_PhysicsEM_Reset();
    gX_PhysicsThermal_Reset();

    //ai: joint rigs — the builder must create the chain + door + windmill +
    //   rail; the chain holds under gravity, the windmill motor drives its
    //   hinge, and park destroys every joint before its body.
    gX_ScenePhysics_Load(&sceneLocal);
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_ScenePhysics_BuildJointRigs(&sceneLocal);
    if (sceneLocal.linkCount != GX_SCENE_PHYSICS_ROPE_LINKS + 3
        || sceneLocal.jointCount != GX_SCENE_PHYSICS_ROPE_LINKS + 3) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: rig build links=%d joints=%d",
            sceneLocal.linkCount, sceneLocal.jointCount);
        errors++;
    }
    for (gX_I32 i = 0; i < 240; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    //ai: chain endpoints stay ROPE_LINKS-1 spacings apart (sag tolerance);
    //   the motor keeps its hinge at the driven rate.
    gX_F32v3 top = gX_PhysicsBody_GetPosition(sceneLocal.links[0].body);
    gX_F32v3 bottom = gX_PhysicsBody_GetPosition(
        sceneLocal.links[GX_SCENE_PHYSICS_ROPE_LINKS - 1].body);
    gX_F32 chainLength = gX_F32v3_Length(gX_F32v3_Sub(top, bottom));
    gX_F32 expected = (gX_F32)(GX_SCENE_PHYSICS_ROPE_LINKS - 1)
        * GX_SCENE_PHYSICS_LINK_SPACING;
    if (chainLength < expected - 0.8f || chainLength > expected + 1.2f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: chain length %.2f (expected ~%.2f)",
            chainLength, expected);
        errors++;
    }
    gX_F32 motorRate = -1.0f;
    for (gX_I32 i = 0; i < sceneLocal.jointCount; i++) {
        if (sceneLocal.joints[i].kind == gX_SCENE_PHYSICS_JOINT_HINGE_MOTOR) {
            motorRate = gX_PhysicsJoint_GetPositionRate(
                sceneLocal.joints[i].joint);
        }
    }
    if (!(motorRate > 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: windmill motor rate %.2f", motorRate);
        errors++;
    }
    //ai: the constrained rigs keep their orientation/lines — a vertical
    //   hinge keeps the door upright, and hinge/slider pivots hold height.
    gX_F32 doorY = gX_PhysicsBody_GetPosition(
        sceneLocal.links[GX_SCENE_PHYSICS_ROPE_LINKS].body).y;
    gX_F32 spinnerY = gX_PhysicsBody_GetPosition(
        sceneLocal.links[GX_SCENE_PHYSICS_ROPE_LINKS + 1].body).y;
    gX_F32 railY = gX_PhysicsBody_GetPosition(
        sceneLocal.links[GX_SCENE_PHYSICS_ROPE_LINKS + 2].body).y;
    if (!(doorY > 0.9f && doorY < 1.8f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: door tipped (y=%.2f)", doorY);
        errors++;
    }
    if (!(spinnerY > 4.6f && spinnerY < 5.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: spinner hub drifted (y=%.2f)", spinnerY);
        errors++;
    }
    if (!(railY > 1.0f && railY < 1.4f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: rail carriage left its line (y=%.2f)",
            railY);
        errors++;
    }
    gX_ScenePhysics_Park(&sceneLocal);
    if (sceneLocal.linkCount != 0 || sceneLocal.jointCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: park kept rigs links=%d joints=%d",
            sceneLocal.linkCount, sceneLocal.jointCount);
        errors++;
    }
    if (gX_PhysicsWorld_GetBodyCount(world) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: park left %d bodies",
            gX_PhysicsWorld_GetBodyCount(world));
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: contact materials — the element rows map to wrapper materials
    //   (unknown ids fall back to the wrapper default), and the contact path
    //   honors them: a slippery box slides farther than a grippy one, and a
    //   bouncy sphere rebounds where a dead one stays down.
    gX_ScenePhysics_Load(&sceneLocal);
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    gX_PhysicsCollider_SetMaterial(sceneLocal.groundCollider,
        (gX_PhysicsMaterial){0.9f, 0.0f, 0.0f, 0.0f});
    const gX_Element_Info *ironRow = gX_Elements_Get(gX_ELEMENT_IRON);
    gX_PhysicsMaterial ironMaterial =
        gX_ScenePhysics_MaterialForElement(gX_ELEMENT_IRON);
    gX_PhysicsMaterial fallback = gX_PhysicsMaterial_Default();
    gX_PhysicsMaterial unknown =
        gX_ScenePhysics_MaterialForElement(gX_ELEMENT_NONE);
    if (!ironRow || ironMaterial.friction != ironRow->friction
        || ironMaterial.restitution != ironRow->restitution
        || unknown.friction != fallback.friction
        || unknown.restitution != fallback.restitution) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: element material mapping");
        errors++;
    }

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsBody slick = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){-8.0f, 0.5f, -2.0f});
    gX_PhysicsBody grippy = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){-8.0f, 0.5f, 2.0f});
    if (slick.internal == 0 || grippy.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: material slide bodies");
        errors++;
    } else {
        gX_PhysicsBody_SetMaterial(slick,
            (gX_PhysicsMaterial){0.02f, 0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetMaterial(grippy,
            (gX_PhysicsMaterial){0.95f, 0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetLinearVel(slick, (gX_F32v3){4.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetLinearVel(grippy, (gX_F32v3){4.0f, 0.0f, 0.0f});
        for (gX_I32 i = 0; i < 120; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        gX_F32 slickDx = gX_PhysicsBody_GetPosition(slick).x + 8.0f;
        gX_F32 grippyDx = gX_PhysicsBody_GetPosition(grippy).x + 8.0f;
        if (!(slickDx > grippyDx + 1.5f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: friction slide slick=%.2f grippy=%.2f",
                slickDx, grippyDx);
            errors++;
        }
        gX_PhysicsBody_Destroy(world, slick);
        gX_PhysicsBody_Destroy(world, grippy);
    }

    gX_PhysicsBody bouncy = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){2.0f, 4.0f, -2.0f});
    gX_PhysicsBody dead = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){2.0f, 4.0f, 2.0f});
    if (bouncy.internal == 0 || dead.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: material bounce bodies");
        errors++;
    } else {
        gX_PhysicsBody_SetMaterial(bouncy,
            (gX_PhysicsMaterial){0.3f, 0.85f, 0.0f, 0.0f});
        gX_PhysicsBody_SetMaterial(dead,
            (gX_PhysicsMaterial){0.3f, 0.0f, 0.0f, 0.0f});
        //ai: post-impact rebound speed — the bouncy sphere must visibly
        //   reverse (nominal 0.85 of an 8+ m/s drop), the dead one only
        //   shows solver noise.
        gX_F32 bouncyUp = 0.0f;
        gX_F32 deadUp = 0.0f;
        for (gX_I32 i = 0; i < 240; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            gX_F32 up = gX_PhysicsBody_GetLinearVel(bouncy).y;
            if (up > bouncyUp) bouncyUp = up;
            up = gX_PhysicsBody_GetLinearVel(dead).y;
            if (up > deadUp) deadUp = up;
        }
        if (!(bouncyUp > 3.0f) || !(deadUp < 0.8f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: restitution rebound bouncy=%.2f dead=%.2f",
                bouncyUp, deadUp);
            errors++;
        }
        gX_PhysicsBody_Destroy(world, bouncy);
        gX_PhysicsBody_Destroy(world, dead);
    }
    if (gX_PhysicsWorld_GetBodyCount(world) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: material test left %d bodies",
            gX_PhysicsWorld_GetBodyCount(world));
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: water — the basin builds headless (static walls + the fluid region
    //   matching the pool settings); park resets the fluid and unload
    //   releases the basin statics. The empty-basin path stays dry.
    gX_ScenePhysics_Load(&sceneLocal);
    sceneLocal.waterEnabled = 1;
    gX_ScenePhysics_BuildWater(&sceneLocal);
    gX_F32 poolHalf = GX_SCENE_PHYSICS_POOL_SIZE * 0.5f;
    gX_PhysicsFluid_Settings fluid = gX_PhysicsFluid_GetSettings();
    if (!gX_PhysicsFluid_Enabled()
        || sceneLocal.poolWall[0].internal == 0
        || sceneLocal.poolWall[3].internal == 0
        || fluid.surfaceY != GX_SCENE_PHYSICS_POOL_SURFACE
        || fluid.minX != GX_SCENE_PHYSICS_POOL_CENTER_X - poolHalf
        || fluid.maxZ != GX_SCENE_PHYSICS_POOL_CENTER_Z + poolHalf) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: water build state");
        errors++;
    }

    //ai: a dummy dropped into the filled pool floats — the bones register
    //   with the fluid applier (human-density after the mass rescale) and
    //   buoyancy holds the body off the basin floor; unregister clears them.
    gX_ScenePhysics_RagdollSlot waterSlot;
    memset(&waterSlot, 0, sizeof(waterSlot));
    for (gX_U32 b = 0; b < GX_RAGDOLL_MAX_BONES; b++) {
        waterSlot.boneModel[b] = -1;
    }
    if (gX_Ragdoll_CreateHumanoid(&waterSlot.ragdoll, world,
            (gX_F32v3){GX_SCENE_PHYSICS_POOL_CENTER_X, 1.0f,
                GX_SCENE_PHYSICS_POOL_CENTER_Z}, 1.0f) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: water ragdoll create");
        errors++;
    } else {
        waterSlot.charge = 1.0f;
        gX_ScenePhysics_RagdollRegister(&sceneLocal, &waterSlot);
        gX_I32 bones = (gX_I32)waterSlot.ragdoll.boneCount;
        if (gX_PhysicsFluid_Count() != bones) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: ragdoll fluid registration count=%d bones=%d",
                gX_PhysicsFluid_Count(), bones);
            errors++;
        }
        for (gX_I32 i = 0; i < 300; i++) {
            gX_PhysicsFluid_Apply();
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        gX_F32 pelvisY = gX_PhysicsBody_GetPosition(
            waterSlot.ragdoll.bones[GX_RAGDOLL_BONE_PELVIS].body).y;
        if (!(pelvisY > 0.8f)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: dummy sank (pelvis y=%.2f)",
                pelvisY);
            errors++;
        }
        gX_ScenePhysics_RagdollUnregister(&sceneLocal, &waterSlot);
        if (gX_PhysicsFluid_Count() != 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll unregister left fluid entries");
            errors++;
        }
        gX_Ragdoll_Destroy(&waterSlot.ragdoll, world);
    }
    gX_ScenePhysics_Park(&sceneLocal);
    if (gX_PhysicsFluid_Enabled() || gX_PhysicsFluid_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: park left the fluid active");
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.poolWall[0].internal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the basin statics");
        errors++;
    }
    gX_ScenePhysics_Load(&sceneLocal);
    sceneLocal.waterEnabled = 0;
    gX_ScenePhysics_BuildWater(&sceneLocal);
    if (gX_PhysicsFluid_Enabled() || sceneLocal.poolWall[0].internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: drained basin state");
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: magnets — the bar-magnet content registers each local moment with
    //   the EM applier; N toggles the forces via the shared scale without
    //   touching the prop-side moments.
    gX_PhysicsEM_Reset();
    gX_ScenePhysics_Load(&sceneLocal);
    sceneLocal.magnetsEnabled = 1;
    sceneLocal.magnetsOn = 0;
    gX_ScenePhysics_BuildMagnets(&sceneLocal);
    gX_I32 magnetProps = 0;
    gX_I32 ironPieces = 0;
    for (gX_I32 i = 0; i < sceneLocal.propCount; i++) {
        gX_ScenePhysics_Prop *prop = &sceneLocal.props[i];
        if (prop->body.internal == 0) continue;
        if (prop->dipole.x != 0.0f || prop->dipole.y != 0.0f
            || prop->dipole.z != 0.0f) {
            magnetProps++;
        }
        if (prop->element == gX_ELEMENT_IRON) {
            gX_F32 chi = 0.0f;
            if (gX_PhysicsEM_GetSusceptibility(prop->body, &chi)
                && chi > 1.0f) {
                ironPieces++;
            }
        }
    }
    if (sceneLocal.propCount != GX_SCENE_PHYSICS_MAGNET_COUNT
            + GX_SCENE_PHYSICS_IRON_COUNT
        || magnetProps != GX_SCENE_PHYSICS_MAGNET_COUNT
        || ironPieces != GX_SCENE_PHYSICS_IRON_COUNT
        || gX_PhysicsEM_Count() != GX_SCENE_PHYSICS_MAGNET_COUNT
            + GX_SCENE_PHYSICS_IRON_COUNT) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: magnet build props=%d magnets=%d iron=%d registry=%d",
            sceneLocal.propCount, magnetProps, ironPieces,
            gX_PhysicsEM_Count());
        errors++;
    }
    gX_ScenePhysics_SetMagnets(&sceneLocal, 1);
    gX_PhysicsEM_Settings magnetSettings = gX_PhysicsEM_GetSettings();
    if (!gX_PhysicsEM_Enabled()
        || magnetSettings.dipoleScale != GX_SCENE_PHYSICS_MAGNET_SCALE) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: magnet toggle on (scale %.3f)",
            magnetSettings.dipoleScale);
        errors++;
    }
    gX_ScenePhysics_SetMagnets(&sceneLocal, 0);
    magnetSettings = gX_PhysicsEM_GetSettings();
    if (gX_PhysicsEM_Enabled() || magnetSettings.dipoleScale != 0.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: magnet toggle off state");
        errors++;
    }
    gX_ScenePhysics_Park(&sceneLocal);
    if (gX_PhysicsEM_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: magnet park left %d entries",
            gX_PhysicsEM_Count());
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    gX_PhysicsEM_Reset();

    //ai: tuning surface — defaults mirror the appliers, a preset JSON round
    //   trip preserves every field, the shipped-path guard refuses saves
    //   under GX_NO_SETTINGS_SAVE, and Apply drives the live scene state.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_ScenePhysics_Tuning tuning = gX_ScenePhysics_Tuning_Defaults();
    gX_PhysicsFluid_Settings fluidDefaults = gX_PhysicsFluid_Defaults();
    if (tuning.props != GX_SCENE_PHYSICS_DEFAULT_PROPS
        || fabsf(tuning.gravity - 9.81f) > 1e-4f
        || tuning.em.dipoleScale != GX_SCENE_PHYSICS_MAGNET_SCALE
        || tuning.thermal.ambientTemperature != gX_PhysicsThermal_Defaults().ambientTemperature
        || tuning.fluidDensity != fluidDefaults.density) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning defaults");
        errors++;
    }
    tuning.gravity = 3.5f;
    tuning.props = 24;
    tuning.ragdolls = 2;
    tuning.joints = 0;
    tuning.water = 0;
    tuning.magnetsOn = 1;
    //ai: the impact-audio layer round-trips like the other toggles.
    tuning.sfx = 0;
    //ai: the relativity knob (c) round-trips too.
    tuning.relativity = 0;
    tuning.lightSpeed = 7.5f;
    tuning.em.coulombScale = 12.5f;
    tuning.em.uniformB = (gX_F32v3){0.1f, -0.2f, 0.3f};
    tuning.em.dipoleScale = 0.123f;
    tuning.thermal.heatingScale = 1234.0f;
    tuning.fluidDensity = 1.4f;
    tuning.fluidDrag = 2.2f;
    gX_I8 tuningPath[128];
    snprintf((char *)tuningPath, sizeof(tuningPath),
        "/tmp/gx_test_%d_physics_tuning.json", (gX_I32)getpid());
    if (gX_ScenePhysics_Tuning_Save(tuningPath, &tuning) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning save");
        errors++;
    }
    gX_ScenePhysics_Tuning loaded;
    if (gX_ScenePhysics_Tuning_Load(tuningPath, &loaded) != 0
        || fabsf(loaded.gravity - 3.5f) > 1e-3f || loaded.props != 24
        || loaded.ragdolls != 2 || loaded.joints != 0 || loaded.water != 0
        || loaded.magnetsOn != 1 || loaded.sfx != 0
        || loaded.relativity != 0
        || fabsf(loaded.lightSpeed - 7.5f) > 1e-3f
        || loaded.em.coulombScale != 12.5f
        || fabsf(loaded.em.uniformB.y + 0.2f) > 1e-3f
        || loaded.em.dipoleScale != 0.123f
        || loaded.thermal.heatingScale != 1234.0f
        || loaded.fluidDensity != 1.4f || loaded.fluidDrag != 2.2f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning round trip");
        errors++;
    }
    remove((const char *)tuningPath);
    if (gX_ScenePhysics_Tuning_Load("/tmp/gx_missing_tuning.json",
            &loaded) == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning load accepted a missing file");
        errors++;
    }
    //ai: the shipped-path guard must be observable without leaking into a
    //   harness-set value for the rest of the run.
    const gX_I8 *guardPrev = (const gX_I8 *)getenv("GX_NO_SETTINGS_SAVE");
    gX_I8 *guardSaved = guardPrev ? (gX_I8 *)strdup((const char *)guardPrev) : NULL;
    if (guardPrev && !guardSaved)
    {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning harness guard snapshot allocation");
        errors++;
    }
    else
    {
        gX_I32 guardSet = setenv("GX_NO_SETTINGS_SAVE", "1", 1);
        if (guardSet != 0)
        {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning harness guard set");
            errors++;
        }
        else if (gX_ScenePhysics_Tuning_Save(
                "resources/tools/physics/tuning_edit.json", &tuning) == 0)
        {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning guard let a shipped save");
            errors++;
        }
        gX_I32 guardRestored = guardSaved
            ? setenv("GX_NO_SETTINGS_SAVE", (const char *)guardSaved, 1)
            : unsetenv("GX_NO_SETTINGS_SAVE");
        if (guardRestored != 0)
        {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning inherited harness guard restoration");
            errors++;
        }
        free(guardSaved);
    }

    //ai: Apply drives the live scene: toggles rebuild/clear, applier
    //   settings replace, and the plan fields land (headless: no ragdolls).
    gX_ScenePhysics_Tuning_Apply(&sceneLocal, &tuning);
    gX_PhysicsEM_Settings emNow = gX_PhysicsEM_GetSettings();
    if (sceneLocal.jointsEnabled || sceneLocal.linkCount != 0
        || sceneLocal.waterEnabled || gX_PhysicsFluid_Enabled()
        || !sceneLocal.magnetsOn || !gX_PhysicsEM_Enabled()
        || sceneLocal.spawnTotal != 24
        || emNow.coulombScale != 12.5f || emNow.uniformB.y != -0.2f
        || fabsf(emNow.dipoleScale - 0.123f) > 1e-6f
        || gX_PhysicsThermal_GetSettings().heatingScale != 1234.0f
        || gX_PhysicsFluid_GetSettings().density != 1.4f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning apply state");
        errors++;
    }
    tuning.joints = 1;
    tuning.water = 1;
    tuning.magnetsOn = 0;
    gX_ScenePhysics_Tuning_Apply(&sceneLocal, &tuning);
    if (!sceneLocal.jointsEnabled
        || sceneLocal.linkCount != GX_SCENE_PHYSICS_ROPE_LINKS + 3
        || !gX_PhysicsFluid_Enabled()
        || sceneLocal.magnetsOn || gX_PhysicsEM_Enabled()) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning re-apply state");
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    gX_PhysicsEM_Reset();
    gX_PhysicsThermal_Reset();
    gX_PhysicsFluid_Reset();

    //ai: conveyor belt — the strip builds headless (static + belt material)
    //   and carries a box along +X at the surface velocity; unload releases
    //   the statics.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_ScenePhysics_BuildBelt(&sceneLocal);
    if (sceneLocal.beltCollider.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: belt statics missing");
        errors++;
    } else {
        gX_PhysicsBody beltBox = gX_PhysicsBody_AddBox(world,
            (gX_F32v3){0.8f, 0.8f, 0.8f}, 1.0f,
            (gX_F32v3){GX_SCENE_PHYSICS_BELT_CENTER_X,
                GX_SCENE_PHYSICS_BELT_HEIGHT + 0.4f,
                GX_SCENE_PHYSICS_BELT_CENTER_Z});
        if (beltBox.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: belt box creation");
            errors++;
        } else {
            for (gX_I32 i = 0; i < 240; i++) {
                gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
                gX_PhysicsWorld_EmptyContactGroup(world);
            }
            gX_F32 beltDx = gX_PhysicsBody_GetPosition(beltBox).x
                - GX_SCENE_PHYSICS_BELT_CENTER_X;
            if (!(beltDx > 0.5f)) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: belt drift dx=%.2f", beltDx);
                errors++;
            }
            gX_PhysicsBody_Destroy(world, beltBox);
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.beltCollider.internal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the belt statics");
        errors++;
    }

    //ai: kinematic movers — the lift/turntable build headless (kinematic
    //   types), the scene drive animates them (the lift rises over its
    //   first half-period, the turntable spins), a rider on the lift is
    //   carried, and unload releases the bodies.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    sceneLocal.moversEnabled = 1;
    gX_ScenePhysics_BuildMovers(&sceneLocal);
    if (sceneLocal.moverCount != GX_SCENE_PHYSICS_MOVERS
        || gX_PhysicsBody_GetType(sceneLocal.movers[0].body)
            != gX_PHYSICS_BODY_KINEMATIC
        || gX_PhysicsBody_GetType(sceneLocal.movers[1].body)
            != gX_PHYSICS_BODY_KINEMATIC) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: movers missing/not kinematic");
        errors++;
    } else {
        //ai: the shipped payloads spawn resting on the mover tops.
        gX_I32 payloadBase = sceneLocal.propCount;
        if (gX_ScenePhysics_SpawnPayload(&sceneLocal, 0) != 0
            || gX_ScenePhysics_SpawnPayload(&sceneLocal, 1) != 0
            || sceneLocal.propCount
                != payloadBase + GX_SCENE_PHYSICS_MOVER_PAYLOADS) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: mover payload spawn");
            errors++;
        } else {
            gX_PhysicsBody rider = sceneLocal.props[payloadBase].body;
            gX_PhysicsBody tablePayload =
                sceneLocal.props[payloadBase + 1].body;
            gX_F32 baseY = sceneLocal.movers[0].spawnPos.y;
            gX_F32 restY = GX_SCENE_PHYSICS_LIFT_THICK * 0.5f
                + GX_SCENE_PHYSICS_PAYLOAD_SIZE * 0.5f + 0.002f;
            gX_F32v3 riderStart = gX_PhysicsBody_GetPosition(rider);
            if (fabsf(riderStart.x - GX_SCENE_PHYSICS_LIFT_X) > 0.01f
                || fabsf(riderStart.z - GX_SCENE_PHYSICS_LIFT_Z) > 0.01f
                || fabsf(riderStart.y - (baseY + restY)) > 0.02f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: payload not on the lift top");
                errors++;
            }
            for (gX_I32 i = 0; i < 180; i++) {
                gX_ScenePhysics_DriveMovers(&sceneLocal, 1.0f / 60.0f);
                gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
                gX_PhysicsWorld_EmptyContactGroup(world);
            }
            gX_F32 liftRise = gX_PhysicsBody_GetPosition(
                sceneLocal.movers[0].body).y - baseY;
            gX_F32 riderRise = gX_PhysicsBody_GetPosition(rider).y
                - (baseY + restY);
            gX_F32 rest = gX_PhysicsBody_GetPosition(rider).y
                - gX_PhysicsBody_GetPosition(sceneLocal.movers[0].body).y;
            if (!(liftRise > 1.0f) || !(riderRise > 0.8f)
                || fabsf(rest - restY) > 0.15f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: mover ride lift=%.2f rider=%.2f rest=%.2f",
                    liftRise, riderRise, rest);
                errors++;
            }
            //ai: long ride — the payload must stay on the platform through
            //   several lift cycles (contact jitter must not walk it off).
            for (gX_I32 i = 0; i < 1020; i++) {
                gX_ScenePhysics_DriveMovers(&sceneLocal, 1.0f / 60.0f);
                gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
                gX_PhysicsWorld_EmptyContactGroup(world);
            }
            gX_F32v3 riderPos = gX_PhysicsBody_GetPosition(rider);
            gX_F32v3 liftPos = gX_PhysicsBody_GetPosition(
                sceneLocal.movers[0].body);
            gX_F32 driftX = riderPos.x - liftPos.x;
            gX_F32 driftZ = riderPos.z - liftPos.z;
            gX_F32 restEnd = riderPos.y - liftPos.y;
            if (sqrtf(driftX * driftX + driftZ * driftZ) > 0.8f
                || fabsf(restEnd - restY) > 0.2f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: payload walked off (dx=%.2f dz=%.2f rest=%.2f)",
                    driftX, driftZ, restEnd);
                errors++;
            }
            //ai: the turntable payload rides the spin — on the platform but
            //   carried around, not flung off.
            gX_F32v3 tablePos = gX_PhysicsBody_GetPosition(
                sceneLocal.movers[1].body);
            gX_F32v3 tableRide = gX_PhysicsBody_GetPosition(tablePayload);
            gX_F32 tableDx = tableRide.x - tablePos.x;
            gX_F32 tableDz = tableRide.z - tablePos.z;
            gX_F32 tableRestY = GX_SCENE_PHYSICS_TABLE_THICK * 0.5f
                + GX_SCENE_PHYSICS_PAYLOAD_SIZE * 0.5f + 0.002f;
            if (sqrtf(tableDx * tableDx + tableDz * tableDz) > 0.9f
                || fabsf((tableRide.y - tablePos.y) - tableRestY) > 0.2f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: turntable payload off (dx=%.2f dz=%.2f rest=%.2f)",
                    tableDx, tableDz, tableRide.y - tablePos.y);
                errors++;
            }
        }
        gX_F32Qua spin;
        gX_PhysicsBody_GetRotation(sceneLocal.movers[1].body, &spin);
        if (fabsf(spin.y) < 0.2f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: turntable did not spin");
            errors++;
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.moverCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the movers");
        errors++;
    }

    //ai: balance posts — the mass-center content builds headless (equal
    //   masses, distinct centers), the R snapshot addresses the moved body
    //   origin, and B re-poses all three at the same tilted release: only
    //   the top-heavy post tips over.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
        gX_PhysicsCollider_SetMaterial(sceneLocal.groundCollider,
            (gX_PhysicsMaterial){0.9f, 0.0f, 0.0f, 0.0f});
    }
    sceneLocal.balanceEnabled = 1;
    gX_ScenePhysics_BuildBalancePosts(&sceneLocal);
    if (sceneLocal.balanceCount != GX_SCENE_PHYSICS_BALANCE_COUNT) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: balance posts missing");
        errors++;
    } else {
        gX_I1 setupOk = true;
        for (gX_I32 i = 0; i < sceneLocal.balanceCount; i++) {
            gX_ScenePhysics_Prop *prop =
                &sceneLocal.props[sceneLocal.balance[i]];
            gX_F32 want = (i == 0) ? GX_SCENE_PHYSICS_BALANCE_OFFSET
                : ((i == 2) ? -GX_SCENE_PHYSICS_BALANCE_OFFSET : 0.0f);
            gX_F32v3 com = gX_PhysicsBody_GetMassCenter(prop->body);
            gX_F32 spawnY = GX_SCENE_PHYSICS_BALANCE_HEIGHT * 0.5f + 0.002f
                + want;
            if (prop->body.internal == 0
                || fabsf(gX_PhysicsBody_GetMass(prop->body)
                    - GX_SCENE_PHYSICS_BALANCE_MASS) > 1e-3f
                || fabsf(com.y - want) > 1e-3f
                || fabsf(prop->spawnPos.y - spawnY) > 1e-2f) {
                setupOk = false;
            }
        }
        if (!setupOk) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: balance post mass/center/spawn");
            errors++;
        }
        gX_ScenePhysics_NudgeBalance(&sceneLocal);
        gX_I1 poseOk = true;
        for (gX_I32 i = 0; i < sceneLocal.balanceCount; i++) {
            gX_ScenePhysics_Prop *prop =
                &sceneLocal.props[sceneLocal.balance[i]];
            gX_F32Qua quat;
            gX_PhysicsBody_GetRotation(prop->body, &quat);
            gX_F32v3 up = gX_F32v3_RotateByQuaternion(
                (gX_F32v3){0.0f, 1.0f, 0.0f}, quat);
            gX_F32 lean = acosf(up.y > 1.0f ? 1.0f : up.y);
            if (fabsf(lean - GX_SCENE_PHYSICS_BALANCE_TILT) > 1e-2f
                || fabsf(gX_PhysicsBody_GetLinearVel(prop->body).y) > 1e-3f) {
                poseOk = false;
            }
        }
        if (!poseOk) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: balance release pose");
            errors++;
        }
        for (gX_I32 i = 0; i < 300; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        gX_F32 tilts[GX_SCENE_PHYSICS_BALANCE_COUNT] = {0.0f, 0.0f, 0.0f};
        for (gX_I32 i = 0; i < sceneLocal.balanceCount; i++) {
            gX_ScenePhysics_Prop *prop =
                &sceneLocal.props[sceneLocal.balance[i]];
            gX_F32Qua quat;
            gX_PhysicsBody_GetRotation(prop->body, &quat);
            gX_F32v3 up = gX_F32v3_RotateByQuaternion(
                (gX_F32v3){0.0f, 1.0f, 0.0f}, quat);
            if (up.y > 1.0f) up.y = 1.0f;
            if (up.y < -1.0f) up.y = -1.0f;
            tilts[i] = acosf(up.y);
        }
        if (!(tilts[0] > 1.0f) || !(tilts[1] < 0.15f) || !(tilts[2] < 0.15f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: balance kick top=%.2f mid=%.2f low=%.2f",
                tilts[0], tilts[1], tilts[2]);
            errors++;
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.balanceCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the balance posts");
        errors++;
    }

    //ai: rover — the hinge2 cart builds (box chassis + four cylinder wheels
    //   and four hinge2 joints), the drive motors spin the wheels and the
    //   steered front wheels turn the rover; unload clears it with the rigs.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
        gX_PhysicsCollider_SetMaterial(sceneLocal.groundCollider,
            (gX_PhysicsMaterial){0.9f, 0.0f, 0.0f, 0.0f});
    }
    sceneLocal.roverEnabled = 1;
    gX_ScenePhysics_BuildRover(&sceneLocal);
    gX_I32 roverLinks = sceneLocal.roverLinkCount;
    gX_I32 roverJoints = sceneLocal.jointCount - sceneLocal.roverJointFirst;
    if (roverLinks != 5 || roverJoints != 4) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: rover build %d links / %d joints",
            roverLinks, roverJoints);
        errors++;
    } else {
        gX_I32 first = sceneLocal.roverLinkFirst;
        gX_I1 shapesOk = (sceneLocal.links[first].shape
            == gX_SCENE_PHYSICS_PROP_BOX);
        for (gX_I32 i = 1; i < roverLinks; i++) {
            if (sceneLocal.links[first + i].shape
                != gX_SCENE_PHYSICS_PROP_CYLINDER) {
                shapesOk = false;
            }
        }
        if (!shapesOk) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: rover shapes");
            errors++;
        }
        gX_PhysicsBody chassis = sceneLocal.links[first].body;
        gX_F32v3 start = gX_PhysicsBody_GetPosition(chassis);
        //ai: integrate the chassis's world yaw rate — the euler extraction
        //   wraps and flips near ±pi, the angular velocity does not.
        gX_F32 turned = 0.0f;
        gX_F32 minX = 99.0f, maxX = -99.0f, minZ = 99.0f, maxZ = -99.0f;
        for (gX_I32 i = 0; i < 300; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            gX_F32v3 cpos = gX_PhysicsBody_GetPosition(chassis);
            if (cpos.x < minX) minX = cpos.x;
            if (cpos.x > maxX) maxX = cpos.x;
            if (cpos.z < minZ) minZ = cpos.z;
            if (cpos.z > maxZ) maxZ = cpos.z;
            turned += gX_PhysicsBody_GetAngularVel(chassis).y / 60.0f;
        }
        gX_F32v3 end = gX_PhysicsBody_GetPosition(chassis);
        gX_F32 travel = sqrtf((end.x - start.x) * (end.x - start.x)
            + (end.z - start.z) * (end.z - start.z));
        //ai: the driven front wheels hold the motor rate; the free rear
        //   wheels just roll.
        gX_F32 spin = fabsf(gX_PhysicsJoint_GetPosition2Rate(
            sceneLocal.joints[sceneLocal.roverJointFirst + 2].joint));
        if (!(fabsf(turned) > 1.5f) || !(travel > 0.8f) || !(spin > 1.5f)
            || !(minZ > 3.6f) || !(maxX < 2.0f) || !(minX > -2.0f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: rover drive turn=%.2f travel=%.2f spin=%.2f "
                "bounds x[%.2f,%.2f] z[%.2f,%.2f]",
                turned, travel, spin, minX, maxX, minZ, maxZ);
            errors++;
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.linkCount != 0 || sceneLocal.jointCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the rover");
        errors++;
    }

    //ai: break target — the feedback-welded board builds, reports its
    //   weight, survives its own load, breaks above the threshold (the weld
    //   is destroyed and the board falls) and R rewelds it.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    sceneLocal.targetEnabled = 1;
    gX_ScenePhysics_BuildBreakTarget(&sceneLocal);
    if (sceneLocal.targetLink < 0 || sceneLocal.targetJoint < 0
        || sceneLocal.links[sceneLocal.targetLink].body.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: break target build");
        errors++;
    } else {
        gX_PhysicsBody board = sceneLocal.links[sceneLocal.targetLink].body;
        gX_F32 weight = gX_PhysicsBody_GetMass(board) * 9.81f;
        for (gX_I32 i = 0; i < 120; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            gX_ScenePhysics_UpdateBreakTarget(&sceneLocal);
        }
        gX_PhysicsJoint weld = sceneLocal.joints[sceneLocal.targetJoint].joint;
        gX_F32v3 force = {0.0f, 0.0f, 0.0f};
        if (weld.internal == 0
            || !gX_PhysicsJoint_GetFeedback(weld, &force, NULL)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: break target feedback");
            errors++;
        } else {
            gX_F32 mag = sqrtf(force.x * force.x + force.y * force.y
                + force.z * force.z);
            if (fabsf(mag - weight) > weight * 0.25f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: target static load |F|=%.2f weight=%.2f",
                    mag, weight);
                errors++;
            }
        }
        if (sceneLocal.targetBroken) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: target broke under its own weight");
            errors++;
        }
        //ai: a heavy pull snaps the weld and the board falls.
        gX_F32 yBefore = gX_PhysicsBody_GetPosition(board).y;
        for (gX_I32 i = 0; i < 12; i++) {
            gX_PhysicsBody_ApplyForce(board, (gX_F32v3){0.0f, -80.0f, 0.0f});
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            gX_ScenePhysics_UpdateBreakTarget(&sceneLocal);
        }
        if (!sceneLocal.targetBroken) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: target did not break");
            errors++;
        }
        for (gX_I32 i = 0; i < 30; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        if (!(gX_PhysicsBody_GetPosition(board).y < yBefore - 0.2f)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: broken board did not fall");
            errors++;
        }
        //ai: R rewelds the board at its reset pose.
        gX_ScenePhysics_ResetContent(&sceneLocal);
        if (sceneLocal.targetBroken
            || sceneLocal.joints[sceneLocal.targetJoint].joint.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: target reweld");
            errors++;
        } else {
            gX_F32 back = gX_PhysicsBody_GetPosition(board).y
                - sceneLocal.links[sceneLocal.targetLink].spawnPos.y;
            if (fabsf(back) > 0.05f) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: reweld pose (dy=%.3f)", back);
                errors++;
            }
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: piston pump — the plunger builds on a piston joint, the square-wave
    //   drive pumps it through its stroke inside the limits and the rotation
    //   motor keeps it turning; unload releases it with the rigs.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    sceneLocal.pumpEnabled = 1;
    gX_ScenePhysics_BuildPump(&sceneLocal);
    if (sceneLocal.pumpLink < 0 || sceneLocal.pumpJoint < 0
        || sceneLocal.links[sceneLocal.pumpLink].body.internal == 0
        || sceneLocal.joints[sceneLocal.pumpJoint].joint.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: pump build");
        errors++;
    } else {
        gX_PhysicsBody plunger = sceneLocal.links[sceneLocal.pumpLink].body;
        gX_PhysicsJoint joint = sceneLocal.joints[sceneLocal.pumpJoint].joint;
        gX_F32 minY = 99.0f, maxY = -99.0f;
        for (gX_I32 i = 0; i < 360; i++) {
            gX_ScenePhysics_DrivePump(&sceneLocal, 1.0f / 60.0f);
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            gX_F32 y = gX_PhysicsBody_GetPosition(plunger).y;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        gX_F32 slide = gX_PhysicsJoint_GetPosition(joint);
        gX_F32 spin = fabsf(gX_PhysicsBody_GetAngularVel(plunger).y);
        if (!(maxY - minY > 0.5f)
            || !(fabsf(slide) <= GX_SCENE_PHYSICS_PUMP_STROKE + 0.06f)
            || !(spin > 0.3f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: pump drive stroke=%.2f slide=%.2f spin=%.2f",
                maxY - minY, slide, spin);
            errors++;
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: terrain mound — the heightfield builds, a ball dropped off the
    //   crest rolls away downhill without falling through, and unload
    //   releases the collider.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    sceneLocal.moundEnabled = 1;
    gX_ScenePhysics_BuildMound(&sceneLocal);
    if (sceneLocal.moundCollider.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: mound build");
        errors++;
    } else {
        gX_F32 startDx = 0.8f;
        gX_PhysicsBody ball = gX_PhysicsBody_AddSphere(world, 0.22f, 1.0f,
            (gX_F32v3){GX_SCENE_PHYSICS_MOUND_X + startDx, 1.0f,
                GX_SCENE_PHYSICS_MOUND_Z});
        for (gX_I32 i = 0; i < 240; i++) {
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        gX_F32v3 end = gX_PhysicsBody_GetPosition(ball);
        gX_F32 dx = end.x - GX_SCENE_PHYSICS_MOUND_X;
        gX_F32 dz = end.z - GX_SCENE_PHYSICS_MOUND_Z;
        gX_F32 dist = sqrtf(dx * dx + dz * dz);
        if (!(dist > startDx - 0.05f) || !(end.y > 0.1f)
            || !(end.y < 1.3f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: mound ball (dx=%.2f dz=%.2f y=%.2f)", dx, dz, end.y);
            errors++;
        }
        gX_PhysicsBody_Destroy(world, ball);
    }
    gX_ScenePhysics_Unload(&sceneLocal);
    if (sceneLocal.moundCollider.internal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unload kept the mound");
        errors++;
    }

    //ai: relativity lane — the tuning defaults keep the layer on (c = 12),
    //   the slug builds on the lane start and registers with the applier,
    //   launching fires it at 0.92c, the run shows gamma mass growth, and
    //   the speed never crosses c.
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    gX_ScenePhysics_Tuning relativityDefaults =
        gX_ScenePhysics_Tuning_Defaults();
    if (!relativityDefaults.relativity
        || fabsf(relativityDefaults.lightSpeed - 12.0f) > 1e-4f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity tuning defaults");
        errors++;
    }
    sceneLocal.relativityEnabled = 1;
    gX_PhysicsRelativity_SetEnabled(1);
    gX_PhysicsRelativity_Settings relativity =
        gX_PhysicsRelativity_Defaults();
    relativity.lightSpeed = 8.0f;
    gX_PhysicsRelativity_SetSettings(relativity);
    if (gX_ScenePhysics_BuildSlug(&sceneLocal) != 0
        || sceneLocal.slugProp < 0
        || sceneLocal.slugProp >= sceneLocal.propCount) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: slug build");
        errors++;
    } else {
        gX_PhysicsBody slug = sceneLocal.props[sceneLocal.slugProp].body;
        if (gX_PhysicsRelativity_Count() < 1) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: slug not registered");
            errors++;
        }
        gX_F32v3 start = gX_PhysicsBody_GetPosition(slug);
        gX_ScenePhysics_LaunchSlug(&sceneLocal);
        gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(slug);
        if (fabsf(velocity.x - 8.0f * GX_SCENE_PHYSICS_SLUG_LAUNCH) > 0.05f
            || fabsf(velocity.y) > 0.01f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: slug launch v=(%.2f,%.2f,%.2f)",
                velocity.x, velocity.y, velocity.z);
            errors++;
        }
        gX_I32 overspeed = 0;
        for (gX_I32 i = 0; i < 30; i++) {
            gX_PhysicsRelativity_Apply();
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
            if (gX_F32v3_Length(gX_PhysicsBody_GetLinearVel(slug)) > 8.0001f) {
                overspeed++;
            }
        }
        gX_F32 beta = 0.0f;
        gX_F32 gamma = 1.0f;
        gX_F32 mass = 0.0f;
        gX_F32 travel = gX_PhysicsBody_GetPosition(slug).x - start.x;
        if (overspeed != 0
            || !gX_PhysicsRelativity_GetBodyState(slug, &beta, &gamma, &mass)
            || !(gamma > 1.5f) || !(beta > 0.7f) || !(travel > 1.5f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: slug run overspeed=%d beta=%.2f gamma=%.2f travel=%.2f",
                overspeed, beta, gamma, travel);
            errors++;
        }
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    //ai: toolgun surface — the aim resolve, the tool-mode hand-off, the
    //   spring grab's ray target and the pin pool's lifecycle (unpin, prop
    //   removal and the R reset).
    gX_ScenePhysics_Load(&sceneLocal);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    if (sceneLocal.groundCollider.internal == 0) {
        sceneLocal.groundCollider = gX_PhysicsWorld_AddGroundPlane(world,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    }
    gX_PhysicsBody toolBox = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.5f, (gX_F32v3){0.0f, 3.0f, 0.0f});
    sceneLocal.props[0].body = toolBox;
    sceneLocal.props[0].modelIndex = 7;
    sceneLocal.props[0].spawnPos = (gX_F32v3){0.0f, 3.0f, 0.0f};
    sceneLocal.propCount = 1;
    sceneLocal.liveCount = 1;

    gX_ScenePhysics_SetToolMode(&sceneLocal, 1);
    if (!sceneLocal.toolMode) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tool mode on");
        errors++;
    }
    gX_ScenePhysics_SetToolMode(&sceneLocal, 0);
    if (sceneLocal.toolMode) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tool mode off");
        errors++;
    }

    gX_ScenePhysics_Aim emptyAim = gX_ScenePhysics_AimAtModel(&sceneLocal, -1);
    if (emptyAim.kind != gX_SCENE_PHYSICS_AIM_NONE
        || emptyAim.index != -1 || emptyAim.bone != -1
        || emptyAim.body.internal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: empty aim resolve");
        errors++;
    }
    gX_ScenePhysics_Aim aim = gX_ScenePhysics_AimAtModel(&sceneLocal, 7);
    if (aim.kind != gX_SCENE_PHYSICS_AIM_PROP || aim.index != 0
        || aim.body.internal != toolBox.internal
        || sceneLocal.hoverProp != 0
        || fabsf(aim.position.y - 3.0f) > 0.05f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: prop aim resolve");
        errors++;
    }

    gX_Ray ray = {{0.0f, 3.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    gX_ScenePhysics_ToolGrab(&sceneLocal, aim, ray.position, ray.direction,
        1, 1);
    if (!sceneLocal.dragActive || sceneLocal.dragProp != 0
        || fabsf(sceneLocal.dragTarget.y - 3.0f) > 0.2f
        || fabsf(sceneLocal.dragTarget.z) > 0.6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: toolgun grab target (y=%.2f z=%.2f)",
            sceneLocal.dragTarget.y, sceneLocal.dragTarget.z);
        errors++;
    }
    gX_ScenePhysics_ToolGrab(&sceneLocal, aim, ray.position, ray.direction,
        0, 0);
    if (sceneLocal.dragActive || sceneLocal.dragProp != -1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: toolgun grab release");
        errors++;
    }

    gX_F32v3 pinAnchor = {0.0f, 3.5f, 0.0f};
    gX_ScenePhysics_ToolPin(&sceneLocal, aim, pinAnchor);
    if (gX_ScenePhysics_PinCount(&sceneLocal) != 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: pin create");
        errors++;
    }
    for (gX_I32 i = 0; i < 60; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    gX_F32v3 pinned = gX_PhysicsBody_GetPosition(toolBox);
    gX_F32 pinDist = gX_F32v3_Distance(pinned, pinAnchor);
    if (!(pinDist > 0.2f) || !(pinDist < 1.2f) || !(pinned.y > 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: pin hold dist=%.2f y=%.2f",
            pinDist, pinned.y);
        errors++;
    }
    gX_ScenePhysics_ToolUnpin(&sceneLocal, aim);
    if (gX_ScenePhysics_PinCount(&sceneLocal) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: unpin");
        errors++;
    }
    gX_ScenePhysics_ToolPin(&sceneLocal, aim, pinAnchor);
    gX_ScenePhysics_ToolPin(&sceneLocal, aim, (gX_F32v3){0.3f, 3.5f, 0.0f});
    if (gX_ScenePhysics_PinCount(&sceneLocal) != 2) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: double pin");
        errors++;
    }
    gX_ScenePhysics_ResetContent(&sceneLocal);
    if (gX_ScenePhysics_PinCount(&sceneLocal) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: reset kept pins");
        errors++;
    }
    gX_ScenePhysics_ToolPin(&sceneLocal, aim, pinAnchor);
    gX_ScenePhysics_ToolRemove(&sceneLocal, aim);
    if (gX_ScenePhysics_PinCount(&sceneLocal) != 0
        || sceneLocal.props[0].body.internal != 0
        || sceneLocal.props[0].modelIndex != -1
        || sceneLocal.liveCount != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: remove with pins");
        errors++;
    }
    gX_ScenePhysics_Unload(&sceneLocal);

    gX_PhysicsWorld_Destroy(world);
    physics.world = savedWorld;
    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics scene lifecycle — 0 errors");
    }
    return errors;
}

//ai: impact-audio palette mapping — pure, so the whole decision table is
//   pinned here without a world, a renderer or an audio device.
gX_I32 gX_Tests_SceneImpactSound(gX_I0) {
    gX_I32 errors = 0;
    gX_ScenePhysics_Impact impact;
    memset(&impact, 0, sizeof(impact));
    impact.position = (gX_F32v3){1.0f, 0.5f, -2.0f};
    impact.normal = (gX_F32v3){0.0f, 1.0f, 0.0f};
    impact.tint = (gX_Color){176, 180, 187, 255};  // sandbox metal default
    impact.energy = 10.0f;
    impact.temperature = 300.0f;
    gX_ScenePhysics_ImpactSound sound;

    gX_ScenePhysics_ImpactSoundMap(&impact, 0, &sound);
    if (sound.variant != GX_AUDIO_SFX_THUD
        || sound.volume < 0.05f || sound.volume > 0.9f
        || sound.pitch < 0.8f || sound.pitch > 1.3f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: soft impact palette v=%d vol=%.2f p=%.2f",
            sound.variant, sound.volume, sound.pitch);
        errors++;
    }
    gX_F32 softVolume = sound.volume;
    gX_F32 softPitch = sound.pitch;

    //ai: a hard metal hit rings (desaturated bright tint) and reads lower
    //   and louder than the soft bump.
    impact.energy = 400.0f;
    gX_ScenePhysics_ImpactSoundMap(&impact, 0, &sound);
    if (sound.variant != GX_AUDIO_SFX_METAL
        || !(sound.volume > softVolume) || !(sound.pitch < softPitch)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: metal impact v=%d vol=%.2f p=%.2f (soft %.2f/%.2f)",
            sound.variant, sound.volume, sound.pitch, softVolume, softPitch);
        errors++;
    }

    //ai: a saturated (wood/plastic/paint) hard hit clacks instead.
    impact.tint = (gX_Color){186, 72, 40, 255};
    gX_ScenePhysics_ImpactSoundMap(&impact, 0, &sound);
    if (sound.variant != GX_AUDIO_SFX_CLACK) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: colored impact v=%d", sound.variant);
        errors++;
    }

    //ai: charge wins over material; hot bodies ring even when colorful.
    impact.charge = -2.0f;
    gX_ScenePhysics_ImpactSoundMap(&impact, 0, &sound);
    if (sound.variant != GX_AUDIO_SFX_ARC) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: charged impact v=%d", sound.variant);
        errors++;
    }
    impact.charge = 0.0f;
    impact.temperature = 1200.0f;
    gX_ScenePhysics_ImpactSoundMap(&impact, 0, &sound);
    if (sound.variant != GX_AUDIO_SFX_METAL) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: hot impact v=%d", sound.variant);
        errors++;
    }

    //ai: water entries read as a dull, quieter thud.
    impact.temperature = 300.0f;
    impact.energy = 400.0f;
    gX_ScenePhysics_ImpactSoundMap(&impact, 1, &sound);
    if (sound.variant != GX_AUDIO_SFX_THUD
        || !(sound.pitch < softPitch) || !(sound.volume < 0.9f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: water impact v=%d vol=%.2f p=%.2f",
            sound.variant, sound.volume, sound.pitch);
        errors++;
    }

    //ai: the position-derived jitter must stay bounded and repeatable.
    gX_ScenePhysics_ImpactSound again;
    gX_ScenePhysics_ImpactSoundMap(&impact, 1, &again);
    if (again.pitch != sound.pitch || again.volume != sound.volume
        || again.variant != sound.variant) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact mapping is not repeatable");
        errors++;
    }

    //ai: NULL guards must not crash.
    gX_ScenePhysics_ImpactSoundMap(NULL, 0, &sound);
    gX_ScenePhysics_ImpactSoundMap(&impact, 0, NULL);

    //ai: the scene toggle drives the layer state (a zeroed scene has no
    //   world to install capture on, so this stays side-effect free).
    gX_ScenePhysics sceneLocal;
    memset(&sceneLocal, 0, sizeof(sceneLocal));
    if (gX_ScenePhysics_IsSFXEnabled(&sceneLocal) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sfx layer defaults off on a zeroed scene");
        errors++;
    }
    gX_ScenePhysics_SetSFXEnabled(&sceneLocal, 1);
    if (!gX_ScenePhysics_IsSFXEnabled(&sceneLocal)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sfx layer did not enable");
        errors++;
    }
    gX_ScenePhysics_SetSFXEnabled(&sceneLocal, 0);
    if (gX_ScenePhysics_IsSFXEnabled(&sceneLocal)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sfx layer did not disable");
        errors++;
    }

    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GX_TESTS: impact sound mapping — 0 errors");
    }
    return errors;
}
