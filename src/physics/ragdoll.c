/**********************************************************************************************
*
*   [gX: src/physics/ragdoll.c]
*   Ragdoll subsystem — humanoid articulation on the physics wrapper
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


//ai: ported from gameX src/physics/ragdoll.c — 11-bone humanoid on the
//   wrapper's capsule/sphere bodies and ball/hinge joints. Bone layout and
//   joint anchors are the gameX reference values (units: metres, y up).
#include "ragdoll.h"

#include <math.h>
#include <string.h>

#include "../common/log.h"
#include "../gxmath/quaternion.h"

//ai: category bits for collision filtering. Ragdoll bodies use
//   category 0x2 and a mask excluding 0x2, so ragdoll-ragdoll pairs never
//   generate contacts while the ground/boxes (category 0x1, mask ~0) still
//   collide with ragdolls. ODE's collideAABBs accepts a pair if EITHER
//   direction matches, hence the exclusive self-mask.
//ai: the bones carry their own category bit and collide with each other;
//   the wrapper skips jointed pairs, the AMotor swing bands keep the limbs
//   out of the extreme folds and the wider leg spacing keeps limb contacts
//   shallow — together they make self-collision stable (see ragdoll.c).
#define RAGDOLL_CATEGORY_BITS  0x00000002u
#define RAGDOLL_MASK_BITS      0xFFFFFFFFu

typedef struct gX_RagdollBoneDef_ {
    const gX_I8 *name;
    gX_F32v3 pos;
    gX_F32 radius;
    gX_F32 length;       //ai: 0 = sphere
    gX_I32 axis;         //ai: 1=X, 2=Y, 3=Z (capsule only)
    gX_F32 mass;
    gX_U32 colorIndex;
} gX_RagdollBoneDef_;

typedef struct gX_RagdollJointDef_ {
    gX_U32 parent;
    gX_U32 child;
    gX_I32 type;         //ai: 0=ball, 1=hinge
    gX_F32v3 anchor;
    gX_I32 hingeAxis;    //ai: 1=X, 2=Y, 3=Z
    gX_F32 lo;
    gX_F32 hi;
} gX_RagdollJointDef_;

static const gX_RagdollBoneDef_ boneDefs_[] = {
    { "pelvis",     {  0.00f, 0.95f, 0.00f }, 0.16f, 0.18f, 2,  8.0f, 0 },
    { "torso",      {  0.00f, 1.38f, 0.00f }, 0.19f, 0.40f, 2, 12.0f, 1 },
    { "head",       {  0.00f, 1.78f, 0.00f }, 0.13f, 0.00f, 0,  4.0f, 2 },
    { "armU_L",     { -0.40f, 1.45f, 0.00f }, 0.06f, 0.26f, 1,  2.0f, 3 },
    { "armL_L",     { -0.68f, 1.45f, 0.00f }, 0.05f, 0.24f, 1,  1.5f, 3 },
    { "armU_R",     {  0.40f, 1.45f, 0.00f }, 0.06f, 0.26f, 1,  2.0f, 4 },
    { "armL_R",     {  0.68f, 1.45f, 0.00f }, 0.05f, 0.24f, 1,  1.5f, 4 },
    { "legU_L",     { -0.20f, 0.55f, 0.00f }, 0.10f, 0.40f, 2,  6.0f, 3 },
    { "legL_L",     { -0.20f, 0.15f, 0.00f }, 0.08f, 0.42f, 2,  4.0f, 3 },
    { "legU_R",     {  0.20f, 0.55f, 0.00f }, 0.10f, 0.40f, 2,  6.0f, 4 },
    { "legL_R",     {  0.20f, 0.15f, 0.00f }, 0.08f, 0.42f, 2,  4.0f, 4 },
};

static const gX_RagdollJointDef_ jointDefs_[] = {
    { GX_RAGDOLL_BONE_TORSO,       GX_RAGDOLL_BONE_PELVIS,        0, {  0.00f, 1.18f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_HEAD,        GX_RAGDOLL_BONE_TORSO,         0, {  0.00f, 1.60f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_ARM_UPPER_L, GX_RAGDOLL_BONE_TORSO,         0, { -0.27f, 1.45f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_ARM_LOWER_L, GX_RAGDOLL_BONE_ARM_UPPER_L,   1, { -0.53f, 1.45f, 0.00f }, 3, -2.6f, 0.1f },
    { GX_RAGDOLL_BONE_ARM_UPPER_R, GX_RAGDOLL_BONE_TORSO,         0, {  0.27f, 1.45f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_ARM_LOWER_R, GX_RAGDOLL_BONE_ARM_UPPER_R,   1, {  0.53f, 1.45f, 0.00f }, 3, -2.6f, 0.1f },
    { GX_RAGDOLL_BONE_LEG_UPPER_L, GX_RAGDOLL_BONE_PELVIS,        0, { -0.20f, 0.95f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_LEG_LOWER_L, GX_RAGDOLL_BONE_LEG_UPPER_L,   1, { -0.20f, 0.55f, 0.00f }, 1, 0.0f, 2.6f },
    { GX_RAGDOLL_BONE_LEG_UPPER_R, GX_RAGDOLL_BONE_PELVIS,        0, {  0.20f, 0.95f, 0.00f }, 0, 0.0f, 0.0f },
    { GX_RAGDOLL_BONE_LEG_LOWER_R, GX_RAGDOLL_BONE_LEG_UPPER_R,   1, {  0.20f, 0.55f, 0.00f }, 1, 0.0f, 2.6f },
};

//ai: swing/twist bands for the ball joints (swing = the AMotor's middle
//   angle band around the rest reading, twist = the two outer axes). The
//   bands keep the limbs out of the extreme folds that drove the earlier
//   self-collision blow-ups. 0 = the joint keeps no AMotor.
static const gX_F32 ballSwing_[] = { 0.40f, 0.45f, 0.45f, 0.40f };
static const gX_F32 ballTwist_[] = { 0.60f, 0.80f, 0.60f, 0.60f };

//ai: the ball-joint limit band for a child bone (by the child's limb axis).
static gX_I32 ballLimitIndex_(gX_U32 child) {
    switch (child) {
        case GX_RAGDOLL_BONE_PELVIS:      return 0;   // spine
        case GX_RAGDOLL_BONE_HEAD:        return 1;   // neck
        case GX_RAGDOLL_BONE_ARM_UPPER_L:
        case GX_RAGDOLL_BONE_ARM_UPPER_R: return 2;   // shoulders
        case GX_RAGDOLL_BONE_LEG_UPPER_L:
        case GX_RAGDOLL_BONE_LEG_UPPER_R: return 3;   // hips
        default: return -1;
    }
}

static gX_F32v3 v3Mul_(gX_F32v3 v, gX_F32 s) {
    return (gX_F32v3){ v.x * s, v.y * s, v.z * s };
}

static gX_F32v3 v3Add_(gX_F32v3 a, gX_F32v3 b) {
    return (gX_F32v3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static gX_F32v3 v3Sub_(gX_F32v3 a, gX_F32v3 b) {
    return (gX_F32v3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static gX_F32v3 v3Cross_(gX_F32v3 a, gX_F32v3 b) {
    return (gX_F32v3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
}

static gX_F32v3 v3Norm_(gX_F32v3 v) {
    gX_F32 len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-6f) return (gX_F32v3){ 0.0f, 1.0f, 0.0f };
    return v3Mul_(v, 1.0f / len);
}

//ai: any unit vector perpendicular to v — the second Euler reference axis
//   (ODE requires the two to be perpendicular).
static gX_F32v3 v3Perp_(gX_F32v3 v) {
    if (fabsf(v.x) < 0.9f) {
        return v3Norm_(v3Cross_(v, (gX_F32v3){ 1.0f, 0.0f, 0.0f }));
    }
    return v3Norm_(v3Cross_(v, (gX_F32v3){ 0.0f, 1.0f, 0.0f }));
}

static gX_F32v3 axisToV3_(gX_I32 axis) {
    if (axis == 1) return (gX_F32v3){ 1.0f, 0.0f, 0.0f };
    if (axis == 3) return (gX_F32v3){ 0.0f, 0.0f, 1.0f };
    return (gX_F32v3){ 0.0f, 1.0f, 0.0f };
}

//ai: the wrapper's AddSphere/AddCapsule interpret the mass parameter as
//   DENSITY (dMassSet*), so convert the bone's nominal kg mass into the
//   sandbox's tonne convention (1 sim mass unit = 1 tonne): the analytic
//   shape volume yields a density in tonne/m3, which keeps articulation
//   dynamics (mass ratios) while making ragdolls human-weight against the
//   element props and buoyant in water.
static gX_F32 densityFromTotal_(gX_F32 totalMass, gX_F32 radius,
    gX_F32 length) {
    gX_F32 pi = 3.14159265f;
    gX_F32 sphereVol = (4.0f / 3.0f) * pi * radius * radius * radius;
    gX_F32 vol;
    if (length <= 0.0f) {
        vol = sphereVol;
    } else {
        vol = pi * radius * radius * length + sphereVol;
    }
    if (vol <= 0.0f) return 1.0f;
    return (totalMass / 1000.0f) / vol;
}

gX_I32 gX_Ragdoll_CreateHumanoid(gX_Ragdoll *rd, gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32 scale) {
    gX_U32 i;

    if (!rd || world.internal == 0) return -1;
    if (scale <= 0.0f) scale = 1.0f;

    memset(rd, 0, sizeof(*rd));
    strncpy((char *)rd->name, "humanoidRagdoll", 127);
    strncpy((char *)rd->description,
        "11-bone humanoid articulation with ball and hinge joints", 255);
    rd->world = world;
    rd->origin = origin;
    rd->scale = scale;
    rd->active = 1;

    //ai: create bodies at the upright T-pose.
    for (i = 0; i < GX_RAGDOLL_BONE_COUNT; i++) {
        const gX_RagdollBoneDef_ *def = &boneDefs_[i];
        gX_Ragdoll_Bone *bone = &rd->bones[i];
        gX_F32v3 scaled = v3Mul_(def->pos, scale);
        gX_F32v3 worldPos = v3Add_(origin, scaled);

        strncpy((char *)bone->name, (const char *)def->name, 31);
        bone->colorIndex = def->colorIndex;
        bone->restPosition = def->pos;
        bone->mass = def->mass;

        if (def->length <= 0.0f) {
            gX_F32 r = def->radius * scale;
            gX_F32 density = densityFromTotal_(def->mass, r, 0.0f);
            bone->body = gX_PhysicsBody_AddSphere(world, r, density, worldPos);
            bone->renderScale = (gX_F32v3){ 2.0f * r, 2.0f * r, 2.0f * r };
            bone->radius = r;
            bone->length = 0.0f;
        } else {
            gX_F32 r = def->radius * scale;
            gX_F32 l = def->length * scale;
            gX_F32 density = densityFromTotal_(def->mass, r, l);
            bone->body = gX_PhysicsBody_AddCapsule(world, r, l, def->axis,
                density, worldPos);
            if (def->axis == 1) bone->renderScale = (gX_F32v3){ l + 2.0f * r, 2.0f * r, 2.0f * r };
            else if (def->axis == 3) bone->renderScale = (gX_F32v3){ 2.0f * r, 2.0f * r, l + 2.0f * r };
            else bone->renderScale = (gX_F32v3){ 2.0f * r, l + 2.0f * r, 2.0f * r };
            bone->radius = r;
            bone->length = l;
        }
        if (bone->body.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "ragdoll: failed to create bone %s", def->name);
            gX_Ragdoll_Destroy(rd, world);
            return -2;
        }

        //ai: self-collision filtering (see the filter notes above).
        gX_PhysicsBody_SetCollisionFilter(bone->body,
            RAGDOLL_CATEGORY_BITS, RAGDOLL_MASK_BITS);
    }
    rd->boneCount = GX_RAGDOLL_BONE_COUNT;

    //ai: create joints.
    for (i = 0; i < GX_RAGDOLL_MAX_JOINTS; i++) {
        const gX_RagdollJointDef_ *def = &jointDefs_[i];
        gX_Ragdoll_Joint *j = &rd->joints[i];
        gX_PhysicsBody parent = rd->bones[def->parent].body;
        gX_PhysicsBody child = rd->bones[def->child].body;
        gX_F32v3 anchor = v3Add_(origin, v3Mul_(def->anchor, scale));

        j->parentBone = def->parent;
        j->childBone = def->child;
        j->type = def->type;

        if (def->type == 0) {
            j->joint = gX_PhysicsJoint_CreateBall(world, parent, child, anchor);
            //ai: ball range limits — an Euler AMotor around the ball. The
            //   reference axes are perpendicular (ODE asserts otherwise on
            //   the derived middle axis) and the swing band centers on the
            //   rest reading taken here, so the chain bookkeeping does not
            //   matter. The bands keep the limbs out of the extreme folds
            //   (an arm through the torso, the legs scissoring), which is
            //   what self-collision needs to stay stable.
            gX_I32 band = ballLimitIndex_(def->child);
            if (j->joint.internal != 0 && band >= 0) {
                gX_F32v3 limb = v3Sub_(boneDefs_[def->child].pos,
                    boneDefs_[def->parent].pos);
                gX_F32v3 axisA = v3Norm_(limb);
                gX_F32v3 axisB = v3Perp_(axisA);
                gX_PhysicsJoint am = gX_PhysicsJoint_CreateAMotor(world,
                    parent, child);
                if (am.internal != 0) {
                    gX_PhysicsJoint_AMotorSetEuler(am, axisA, axisB);
                    gX_F32 rest = gX_PhysicsJoint_AMotorGetAngle(am, 1);
                    gX_PhysicsJoint_AMotorSetLimits(am, 1,
                        rest - ballSwing_[band], rest + ballSwing_[band]);
                    gX_PhysicsJoint_AMotorSetLimits(am, 0, -3.0f, 3.0f);
                    gX_PhysicsJoint_AMotorSetLimits(am, 2, -3.0f, 3.0f);
                    j->limit = am;
                }
            }
        } else {
            j->joint = gX_PhysicsJoint_CreateHinge(world, parent, child, anchor,
                axisToV3_(def->hingeAxis));
            if (j->joint.internal != 0) {
                gX_PhysicsJoint_SetLimits(j->joint, def->lo, def->hi);
            }
        }
        if (j->joint.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "ragdoll: failed to create joint %u", i);
            gX_Ragdoll_Destroy(rd, world);
            return -3;
        }
    }
    rd->jointCount = GX_RAGDOLL_MAX_JOINTS;

    return 0;
}

gX_I32 gX_Ragdoll_Destroy(gX_Ragdoll *rd, gX_PhysicsWorld world) {
    gX_U32 i;

    if (!rd) return -1;

    for (i = 0; i < GX_RAGDOLL_MAX_JOINTS; i++) {
        if (rd->joints[i].joint.internal != 0) {
            gX_PhysicsJoint_Destroy(world, rd->joints[i].joint);
            rd->joints[i].joint = gX_PHYSICS_JOINT_NULL;
        }
        //ai: the ball limit AMotors are separate handles.
        if (rd->joints[i].limit.internal != 0) {
            gX_PhysicsJoint_Destroy(world, rd->joints[i].limit);
            rd->joints[i].limit = gX_PHYSICS_JOINT_NULL;
        }
    }
    for (i = 0; i < GX_RAGDOLL_MAX_BONES; i++) {
        if (rd->bones[i].body.internal != 0) {
            gX_PhysicsBody_Destroy(world, rd->bones[i].body);
            rd->bones[i].body = gX_PHYSICS_BODY_NULL;
        }
    }
    rd->jointCount = 0;
    rd->boneCount = 0;
    rd->active = 0;
    return 0;
}

gX_I32 gX_Ragdoll_ResetPose(gX_Ragdoll *rd, gX_PhysicsWorld world) {
    gX_U32 i;

    if (!rd) return -1;

    for (i = 0; i < rd->boneCount; i++) {
        gX_Ragdoll_Bone *bone = &rd->bones[i];
        gX_F32v3 scaled = v3Mul_(bone->restPosition, rd->scale);
        gX_F32v3 worldPos = v3Add_(rd->origin, scaled);

        gX_PhysicsBody_SetPosition(bone->body, worldPos);
        gX_PhysicsBody_SetRotation(bone->body, gX_F32Qua_Identity());
        gX_PhysicsBody_SetLinearVel(bone->body, (gX_F32v3){ 0.0f, 0.0f, 0.0f });
        gX_PhysicsBody_SetAngularVel(bone->body, (gX_F32v3){ 0.0f, 0.0f, 0.0f });
    }
    return 0;
}

gX_I32 gX_Ragdoll_SetEnabled(gX_Ragdoll *rd, gX_PhysicsWorld world, gX_I1 enabled) {
    gX_U32 i;
    (void)world;

    if (!rd) return -1;

    for (i = 0; i < rd->boneCount; i++) {
        if (enabled) {
            gX_PhysicsBody_Enable(rd->bones[i].body);
        } else {
            gX_PhysicsBody_Disable(rd->bones[i].body);
        }
    }
    rd->active = enabled ? 1 : 0;
    return 0;
}

static gX_F32v3 gX_Ragdoll_Control_Clamp(gX_F32v3 v, gX_F32 limit) {
    gX_F32 length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length > limit && length > 1e-8f) {
        gX_F32 scale = limit / length;
        v.x *= scale;
        v.y *= scale;
        v.z *= scale;
    }
    return v;
}

static gX_F32v3 gX_Ragdoll_Control_Rotate(gX_F32Qua q, gX_F32v3 v) {
    gX_F32v3 qv = {q.x, q.y, q.z};
    gX_F32v3 t = v3Cross_(qv, v);
    t = v3Add_(t, v3Mul_(v, q.w));
    t = v3Cross_(qv, t);
    return v3Add_(v, v3Mul_(t, 2.0f));
}

gX_F32 gX_Ragdoll_Control_UpDot(const gX_Ragdoll *ragdoll) {
    gX_F32Qua q = gX_F32Qua_Identity();
    gX_F32v3 up;

    if (!ragdoll || ragdoll->boneCount <= GX_RAGDOLL_BONE_TORSO)
        return 0.0f;
    if (ragdoll->bones[GX_RAGDOLL_BONE_TORSO].body.internal == 0)
        return 0.0f;
    gX_PhysicsBody_GetRotation(
        ragdoll->bones[GX_RAGDOLL_BONE_TORSO].body, &q);
    q = gX_F32Qua_Normalize(q);
    up = gX_Ragdoll_Control_Rotate(q, (gX_F32v3){0.0f, 1.0f, 0.0f});
    return up.y;
}

gX_I32 gX_Ragdoll_Control_Init(gX_Ragdoll_Control *control,
    gX_Ragdoll *ragdoll, gX_PhysicsWorld world) {
    gX_U32 i;

    if (control == NULL || ragdoll == NULL || world.internal == 0)
        return 1;
    memset(control, 0, sizeof(*control));
    control->ragdoll = ragdoll;
    control->world = world;
    control->stiffness = 12.0f;
    control->damping = 6.0f;
    control->supportStiffness = 20.0f;
    control->supportDamping = 4.0f;
    control->mode = gX_RAGDOLL_CONTROL_PASSIVE;
    control->targetPelvisY = ragdoll->origin.y;
    if (ragdoll->boneCount > GX_RAGDOLL_BONE_PELVIS) {
        control->targetPelvisY +=
            ragdoll->bones[GX_RAGDOLL_BONE_PELVIS].restPosition.y;
    }
    for (i = 0; i < ragdoll->boneCount
            && i < GX_RAGDOLL_MAX_BONES; i++) {
        if (ragdoll->bones[i].body.internal == 0) continue;
        gX_PhysicsBody_GetRotation(ragdoll->bones[i].body,
            &control->restQuat[i]);
        control->restQuat[i] =
            gX_F32Qua_Normalize(control->restQuat[i]);
        control->totalMass += gX_PhysicsBody_GetMass(
            ragdoll->bones[i].body);
    }
    if (!(control->totalMass > 0.0f)) control->totalMass = 1.0f;
    control->maxForce = control->totalMass * 9.81f * 1.8f;
    control->maxTorque = control->totalMass * 0.8f;
    control->initialized = 1;
    return 0;
}

gX_I0 gX_Ragdoll_Control_SetMode(gX_Ragdoll_Control *control, gX_I32 mode) {
    if (control == NULL) return;
    if (mode < gX_RAGDOLL_CONTROL_PASSIVE
        || mode > gX_RAGDOLL_CONTROL_GETTING_UP) return;
    control->mode = mode;
    control->modeTimer = 0;
}

gX_I32 gX_Ragdoll_Control_GetMode(const gX_Ragdoll_Control *control) {
    return control ? control->mode : gX_RAGDOLL_CONTROL_PASSIVE;
}

gX_I0 gX_Ragdoll_Control_Step(gX_Ragdoll_Control *control, gX_F32 dt) {
    gX_Ragdoll *rd;
    gX_F32 up;
    gX_F32 torqueScale = 1.0f;
    gX_F32 supportScale = 1.0f;

    if (control == NULL || !control->initialized || control->ragdoll == NULL)
        return;
    if (control->mode == gX_RAGDOLL_CONTROL_PASSIVE) return;
    rd = control->ragdoll;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    control->modeTimer++;

    if (control->mode == gX_RAGDOLL_CONTROL_FALLING) {
        if (control->modeTimer >= 90) {
            control->mode = gX_RAGDOLL_CONTROL_GETTING_UP;
            control->modeTimer = 0;
        }
        return;
    }
    if (control->mode == gX_RAGDOLL_CONTROL_GETTING_UP) {
        torqueScale = 4.0f;
        supportScale = 2.0f;
    }

    up = gX_Ragdoll_Control_UpDot(rd);
    if (control->mode == gX_RAGDOLL_CONTROL_ACTIVE) {
        if (up < 0.40f && control->modeTimer > 30) {
            control->mode = gX_RAGDOLL_CONTROL_FALLING;
            control->modeTimer = 0;
            return;
        }
    }
    else if (control->mode == gX_RAGDOLL_CONTROL_GETTING_UP) {
        if (up > 0.85f && control->modeTimer > 60) {
            control->mode = gX_RAGDOLL_CONTROL_ACTIVE;
            control->modeTimer = 0;
        }
    }

    for (gX_U32 i = 0; i < rd->boneCount && i < GX_RAGDOLL_MAX_BONES; i++) {
        gX_Ragdoll_Bone *bone = &rd->bones[i];
        gX_F32Qua current;
        gX_F32Qua error;
        gX_F32v3 axis;
        gX_F32 angle;
        gX_F32v3 angular;
        gX_F32v3 desired;
        gX_F32v3 torque;
        gX_F32 inertia;
        gX_F32 weight = 1.0f;

        if (bone->body.internal == 0) continue;
        if (i == GX_RAGDOLL_BONE_ARM_UPPER_L
            || i == GX_RAGDOLL_BONE_ARM_LOWER_L
            || i == GX_RAGDOLL_BONE_ARM_UPPER_R
            || i == GX_RAGDOLL_BONE_ARM_LOWER_R) {
            weight = 0.55f;
        }
        gX_PhysicsBody_GetRotation(bone->body, &current);
        current = gX_F32Qua_Normalize(current);
        error = gX_F32Qua_Multiply(control->restQuat[i],
            gX_F32Qua_Invert(current));
        if (error.w < 0.0f) error = gX_F32Qua_Scale(error, -1.0f);
        gX_F32Qua_ToAxisAngle(error, &axis, &angle);
        if (!isfinite(angle)) continue;
        angular = gX_PhysicsBody_GetAngularVel(bone->body);
        desired = v3Mul_(axis, angle * control->stiffness * weight
            * torqueScale);
        torque = v3Sub_(desired, v3Mul_(angular,
            control->damping * weight));
        inertia = gX_PhysicsBody_GetInertia(bone->body).x;
        if (!(inertia > 0.0f) || !isfinite(inertia)) inertia = 0.001f;
        torque = v3Mul_(torque, inertia * 60.0f);
        torque = gX_Ragdoll_Control_Clamp(torque,
            control->maxTorque * weight * torqueScale);
        gX_PhysicsBody_ApplyTorque(bone->body, torque);
    }

    if (rd->boneCount > GX_RAGDOLL_BONE_PELVIS) {
        gX_PhysicsBody pelvis = rd->bones[GX_RAGDOLL_BONE_PELVIS].body;
        if (pelvis.internal != 0) {
            gX_F32v3 position = gX_PhysicsBody_GetPosition(pelvis);
            gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(pelvis);
            gX_F32 forceY = control->totalMass * 9.81f
                + control->supportStiffness * supportScale
                    * (control->targetPelvisY - position.y)
                - control->supportDamping * supportScale * velocity.y;
            if (forceY > control->maxForce * supportScale)
                forceY = control->maxForce * supportScale;
            if (forceY < -control->maxForce * supportScale)
                forceY = -control->maxForce * supportScale;
            gX_PhysicsBody_ApplyForce(pelvis,
                (gX_F32v3){0.0f, forceY, 0.0f});
        }
    }
}

gX_PhysicsBody gX_Ragdoll_GetBone(gX_Ragdoll *rd, gX_U32 boneIndex) {
    if (!rd || boneIndex >= rd->boneCount) return gX_PHYSICS_BODY_NULL;
    return rd->bones[boneIndex].body;
}

gX_U32 gX_Ragdoll_GetBoneCount(gX_Ragdoll *rd) {
    return rd ? rd->boneCount : 0;
}

gX_U32 gX_Ragdoll_GetJointCount(gX_Ragdoll *rd) {
    return rd ? rd->jointCount : 0;
}
