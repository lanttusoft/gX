/**********************************************************************************************
*
*   [gX: src/physics/ragdoll.h]
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


#ifndef GX_PHYSICS_RAGDOLL_H
#define GX_PHYSICS_RAGDOLL_H

//ai: ragdoll subsystem — a humanoid articulation built on the wrapper
//   API. Owns a fixed set of capsule/sphere bodies joined by ball and hinge
//   joints. All ragdoll bodies share a category bit that is masked out of
//   their own collision masks, so ragdoll parts do not collide with each
//   other but still collide with world geometry (ground, boxes). Self-
//   collision needs joint range limits first — see the filter notes in
//   ragdoll.c.

#include "../common/types.h"
#include "../wrappers/physics.h"

#define GX_RAGDOLL_MAX_BONES   12
#define GX_RAGDOLL_MAX_JOINTS  10

typedef enum gX_RagdollBoneType {
    GX_RAGDOLL_BONE_PELVIS = 0,
    GX_RAGDOLL_BONE_TORSO,
    GX_RAGDOLL_BONE_HEAD,
    GX_RAGDOLL_BONE_ARM_UPPER_L,
    GX_RAGDOLL_BONE_ARM_LOWER_L,
    GX_RAGDOLL_BONE_ARM_UPPER_R,
    GX_RAGDOLL_BONE_ARM_LOWER_R,
    GX_RAGDOLL_BONE_LEG_UPPER_L,
    GX_RAGDOLL_BONE_LEG_LOWER_L,
    GX_RAGDOLL_BONE_LEG_UPPER_R,
    GX_RAGDOLL_BONE_LEG_LOWER_R,
    GX_RAGDOLL_BONE_COUNT
} gX_RagdollBoneType;

typedef struct gX_Ragdoll_Bone {
    gX_I8 name[32];
    gX_PhysicsBody body;
    gX_F32v3 restPosition;   //ai: local position relative to ragdoll origin (scale=1)
    gX_F32v3 renderScale;    //ai: unit-cube scale matching the bone shape
    gX_U32 colorIndex;
    //ai: nominal mass (kg) — the thermal registry's heat-capacity unit and
    //   the impact-FX energy mass; the sim body runs at mass/1000 (tonne
    //   convention) so ragdolls are human-weight for contacts and water.
    gX_F32 mass;
    //ai: scaled shape extents for fluid-volume registration (length 0 =
    //   sphere).
    gX_F32 radius;
    gX_F32 length;
} gX_Ragdoll_Bone;

typedef struct gX_Ragdoll_Joint {
    gX_PhysicsJoint joint;
    //ai: optional AMotor carrying the ball's swing/twist limits.
    gX_PhysicsJoint limit;
    gX_U32 parentBone;
    gX_U32 childBone;
    gX_I32 type;             //ai: 0=ball, 1=hinge
} gX_Ragdoll_Joint;

typedef struct gX_Ragdoll {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_PhysicsWorld world;
    gX_F32v3 origin;         //ai: world-space feet position
    gX_F32 scale;

    gX_U32 boneCount;
    gX_Ragdoll_Bone bones[GX_RAGDOLL_MAX_BONES];
    gX_U32 jointCount;
    gX_Ragdoll_Joint joints[GX_RAGDOLL_MAX_JOINTS];

    gX_I1 active;
} gX_Ragdoll;

//ai: opt-in active stabilizer — bounded torques toward the creation pose plus
//   a center-of-mass support force. It does not teleport bodies and does not
//   change the passive ragdoll unless a caller explicitly steps it.
typedef enum gX_Ragdoll_ControlMode {
    gX_RAGDOLL_CONTROL_PASSIVE = 0,
    gX_RAGDOLL_CONTROL_ACTIVE,
    gX_RAGDOLL_CONTROL_FALLING,
    gX_RAGDOLL_CONTROL_GETTING_UP
} gX_Ragdoll_ControlMode;

typedef struct gX_Ragdoll_Control {
    gX_Ragdoll *ragdoll;
    gX_PhysicsWorld world;
    gX_F32Qua restQuat[GX_RAGDOLL_MAX_BONES];
    gX_F32 targetPelvisY;
    gX_F32 totalMass;
    gX_F32 stiffness;
    gX_F32 damping;
    gX_F32 supportStiffness;
    gX_F32 supportDamping;
    gX_F32 maxTorque;
    gX_F32 maxForce;
    gX_I32 mode;
    gX_I32 modeTimer;
    gX_I1 initialized;
} gX_Ragdoll_Control;

gX_I32 gX_Ragdoll_Control_Init(gX_Ragdoll_Control *control,
    gX_Ragdoll *ragdoll, gX_PhysicsWorld world);
gX_I0  gX_Ragdoll_Control_SetMode(gX_Ragdoll_Control *control, gX_I32 mode);
gX_I32 gX_Ragdoll_Control_GetMode(const gX_Ragdoll_Control *control);
gX_I0  gX_Ragdoll_Control_Step(gX_Ragdoll_Control *control, gX_F32 dt);
gX_F32 gX_Ragdoll_Control_UpDot(const gX_Ragdoll *ragdoll);

//ai: build a standing humanoid ragdoll (feet at `origin`, scaled by
//   `scale`). Returns 0 on success, non-zero on failure (world null or a
//   body/joint failed to create). On failure all created objects are cleaned
//   up. The ragdoll is spawned upright with zero velocity.
gX_I32 gX_Ragdoll_CreateHumanoid(gX_Ragdoll *rd, gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32 scale);

//ai: destroy all bodies and joints owned by the ragdoll.
gX_I32 gX_Ragdoll_Destroy(gX_Ragdoll *rd, gX_PhysicsWorld world);

//ai: teleport all bones back to the upright T-pose at the ragdoll
//   origin, zeroing velocities.
gX_I32 gX_Ragdoll_ResetPose(gX_Ragdoll *rd, gX_PhysicsWorld world);

//ai: enable/disable all ragdoll bodies (disabled bodies fall asleep).
gX_I32 gX_Ragdoll_SetEnabled(gX_Ragdoll *rd, gX_PhysicsWorld world,
    gX_I1 enabled);

gX_PhysicsBody gX_Ragdoll_GetBone(gX_Ragdoll *rd, gX_U32 boneIndex);
gX_U32 gX_Ragdoll_GetBoneCount(gX_Ragdoll *rd);
gX_U32 gX_Ragdoll_GetJointCount(gX_Ragdoll *rd);

#endif //GX_PHYSICS_RAGDOLL_H
