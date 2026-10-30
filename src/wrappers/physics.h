/**********************************************************************************************
*
*   [gX: src/wrappers/physics.h]
*   Backend-agnostic physics wrapper — hides ODE/Bullet/Jolt types behind opaque handles.
*   Implementation: wrappers/physics.c
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

#ifndef WRAPPERS_PHYSICS_H
#define WRAPPERS_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: opaque handle types — implementation casts internally to backend types
//   (dBodyID, dWorldID, etc.). Layout must be POD for heap/array storage.
typedef struct gX_PhysicsWorld { gX_U64 internal; } gX_PhysicsWorld;
typedef struct gX_PhysicsBody  { gX_U64 internal; } gX_PhysicsBody;
typedef struct gX_PhysicsJoint { gX_U64 internal; } gX_PhysicsJoint;
//opencode: static geometry handle kept separate from dynamic body handles.
typedef struct gX_PhysicsCollider { gX_U64 internal; } gX_PhysicsCollider;

#define gX_PHYSICS_WORLD_NULL  ((gX_PhysicsWorld){0})
#define gX_PHYSICS_BODY_NULL   ((gX_PhysicsBody){0})
#define gX_PHYSICS_JOINT_NULL  ((gX_PhysicsJoint){0})
#define gX_PHYSICS_COLLIDER_NULL ((gX_PhysicsCollider){0})

//opencode: rigid-body mode supported by the ODE wrapper.
typedef enum gX_PhysicsBodyType {
    gX_PHYSICS_BODY_DYNAMIC = 0,
    gX_PHYSICS_BODY_KINEMATIC = 1
} gX_PhysicsBodyType;

//opencode: contact surface parameters shared by bodies and static colliders.
typedef struct gX_PhysicsMaterial {
    gX_F32 friction;
    gX_F32 restitution;
    gX_F32 rollingFriction;
    gX_F32 surfaceVelocity;
} gX_PhysicsMaterial;

gX_PhysicsMaterial gX_PhysicsMaterial_Default(gX_I0);

//opencode: collision contact point
typedef struct gX_PhysicsContact {
    gX_F32v3 position;
    gX_F32v3 normal;
    gX_F32   depth;
} gX_PhysicsContact;

//opencode: collision callback — called once per colliding pair per step.
//   Return non-zero to skip default contact creation (if you want to handle
//   contacts manually). Implementations must not call back into the step API.
typedef gX_I32 (*gX_PhysicsCollisionCB)(gX_I0 *userData,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_U32 contactCount, const gX_PhysicsContact *contacts);

//opencode: contact event phase — ENTER when a pair starts colliding,
//   STAY for every step it remains in contact, EXIT when it separates.
typedef enum gX_PhysicsContactPhase {
    gX_PHYSICS_PHASE_ENTER = 0,
    gX_PHYSICS_PHASE_STAY  = 1,
    gX_PHYSICS_PHASE_EXIT  = 2
} gX_PhysicsContactPhase;

//opencode: contact event — delivered to the event callback registered via
//   gX_PhysicsWorld_SetEventCallback. EXIT events carry no contact data
//   (contactCount=0, contacts=NULL). bodyA/bodyB may be gX_PHYSICS_BODY_NULL
//   for static colliders — use tagA/tagB (the user tags) to identify them.
typedef struct gX_PhysicsContactEvent {
    gX_PhysicsContactPhase phase;
    gX_PhysicsBody bodyA;
    gX_PhysicsBody bodyB;
    gX_U64 tagA;
    gX_U64 tagB;
    gX_U32 contactCount;
    const gX_PhysicsContact *contacts;
} gX_PhysicsContactEvent;

//opencode: contact event callback — fires on ENTER/STAY/EXIT transitions.
//   Does not affect the collision response; that is controlled by the
//   gX_PhysicsCollisionCB return value. Implementations must not call back
//   into the step API.
typedef gX_I0 (*gX_PhysicsEventCB)(gX_I0 *userData,
    const gX_PhysicsContactEvent *event);

// ========================================================================
// World lifecycle
// ========================================================================

gX_PhysicsWorld gX_PhysicsWorld_Create(gX_I0);
gX_I0           gX_PhysicsWorld_Destroy(gX_PhysicsWorld world);

gX_I0 gX_PhysicsWorld_SetGravity(gX_PhysicsWorld world, gX_F32v3 gravity);
gX_I0 gX_PhysicsWorld_SetSolverParams(gX_PhysicsWorld world,
    gX_F32 cfm, gX_F32 erp);

gX_I0 gX_PhysicsWorld_Step(gX_PhysicsWorld world, gX_F32 dt);
gX_I0 gX_PhysicsWorld_QuickStep(gX_PhysicsWorld world, gX_F32 dt);

//opencode: register collision callback. userData is passed to the callback.
//   Pass NULL for cb to disable.
gX_I0 gX_PhysicsWorld_SetCollisionCallback(gX_PhysicsWorld world,
    gX_PhysicsCollisionCB cb, gX_I0 *userData);

//opencode: register contact event callback (ENTER/STAY/EXIT). userData is
//   passed to the callback. Pass NULL for cb to disable. Independent of the
//   collision callback — both can be set at the same time.
gX_I0 gX_PhysicsWorld_SetEventCallback(gX_PhysicsWorld world,
    gX_PhysicsEventCB cb, gX_I0 *userData);

// ========================================================================
// Bodies
// ========================================================================

gX_PhysicsBody gX_PhysicsBody_AddBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32 mass, gX_F32v3 position);

gX_PhysicsBody gX_PhysicsBody_AddSphere(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 mass, gX_F32v3 position);

//opencode: capsule — direction is 1=X, 2=Y, 3=Z (alignment axis).
//   length is the cylindrical section (total = length + 2*radius).
gX_PhysicsBody gX_PhysicsBody_AddCapsule(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position);

//opencode: cylinder — direction is 1=X, 2=Y, 3=Z (alignment axis).
gX_PhysicsBody gX_PhysicsBody_AddCylinder(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position);

//opencode: convex hull — planes/points/polygons data is copied internally.
//   Polygons array format: for each polygon, first entry = vertex count,
//   followed by that many vertex indices, all flattened into one array.
//   Mass inertia is approximated as a sphere of equal mass.
gX_PhysicsBody gX_PhysicsBody_AddConvex(gX_PhysicsWorld world,
    const gX_F32 *planes, gX_U32 planeCount,
    const gX_F32 *points, gX_U32 pointCount,
    const gX_U32 *polygons,
    gX_F32 mass, gX_F32v3 position);

//opencode: triangle mesh — vertices as packed float[3], indices as uint32[3*triCount].
//   Mass inertia is approximated as a sphere of equal mass.
gX_PhysicsBody gX_PhysicsBody_AddTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32 mass, gX_F32v3 position);

gX_I0 gX_PhysicsBody_Destroy(gX_PhysicsWorld world, gX_PhysicsBody body);

//opencode: static colliders have no dynamic body and never fall under gravity.
gX_PhysicsCollider gX_PhysicsWorld_AddStaticBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32v3 position);
gX_PhysicsCollider gX_PhysicsWorld_AddStaticTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32v3 position);
gX_PhysicsCollider gX_PhysicsWorld_AddGroundPlane(gX_PhysicsWorld world,
    gX_F32v3 normal, gX_F32 dist);
//ai: static heightfield terrain — a collider sampling a caller height grid
//   (widthSamples x depthSamples, row-major with x varying fastest). The
//   field is built wrapped (ODE tiles it in x/z; this build's bounded mode
//   drops bodies through — probed), so the heights are one tile centred on
//   `position` and its borders should stay flat; each height scales by
//   heightScale and the heights are copied internally.
gX_PhysicsCollider gX_PhysicsWorld_AddHeightfield(gX_PhysicsWorld world,
    const gX_F32 *heights, gX_I32 widthSamples, gX_I32 depthSamples,
    gX_F32 width, gX_F32 depth, gX_F32 heightScale, gX_F32v3 position);
gX_I0 gX_PhysicsCollider_Destroy(gX_PhysicsWorld world,
    gX_PhysicsCollider collider);

gX_F32v3 gX_PhysicsBody_GetPosition(gX_PhysicsBody body);
gX_I0    gX_PhysicsBody_SetPosition(gX_PhysicsBody body, gX_F32v3 pos);

gX_I0 gX_PhysicsBody_GetRotation(gX_PhysicsBody body, gX_F32Qua *outQuat);
gX_I0 gX_PhysicsBody_SetRotation(gX_PhysicsBody body, gX_F32Qua quat);
gX_I0 gX_PhysicsBody_SetType(gX_PhysicsBody body, gX_PhysicsBodyType type);
gX_PhysicsBodyType gX_PhysicsBody_GetType(gX_PhysicsBody body);
//ai: auto-disable — the world sleeps bodies that settle below the speed
//   thresholds (defaults: on, 0.5 linear/angular, 10 steps, 1 s). A sleeping
//   body ignores forces and velocity changes until woken; the wrapper wakes
//   it on explicit position/rotation/velocity/force mutations (drag could
//   otherwise be a silent no-op), and contacts wake it too.
gX_I1    gX_PhysicsBody_IsEnabled(gX_PhysicsBody body);
gX_I0    gX_PhysicsBody_Disable(gX_PhysicsBody body);
gX_I0    gX_PhysicsBody_SetAutoDisable(gX_PhysicsBody body, gX_I1 enabled);
gX_I1    gX_PhysicsBody_GetAutoDisable(gX_PhysicsBody body);

gX_I0    gX_PhysicsBody_Enable(gX_PhysicsBody body);

gX_F32v3 gX_PhysicsBody_GetLinearVel(gX_PhysicsBody body);
gX_F32v3 gX_PhysicsBody_GetAngularVel(gX_PhysicsBody body);

//opencode: set body velocities
gX_I0    gX_PhysicsBody_SetLinearVel(gX_PhysicsBody body, gX_F32v3 vel);
gX_I0    gX_PhysicsBody_SetAngularVel(gX_PhysicsBody body, gX_F32v3 vel);
gX_F32   gX_PhysicsBody_GetMass(gX_PhysicsBody body);
gX_F32v3 gX_PhysicsBody_GetInertia(gX_PhysicsBody body);
//ai: mass properties — SetMass rescales the body to the given total mass
//   (the inertia tensor scales with it, the balance point stays).
//   SetMassCenter moves the center of mass in body-local space (an offset
//   from the shape center) with the inertia rebuilt by the parallel axis
//   theorem, so an identical body can be made top- or bottom-heavy. ODE
//   keeps the center of mass at the body origin: the origin moves onto the
//   balance point and the shape takes the opposite offset while keeping
//   its world pose — renderers place the shape at `position -
//   RotateByQuaternion(GetMassCenter, rotation)`.
gX_I0    gX_PhysicsBody_SetMass(gX_PhysicsBody body, gX_F32 mass);
gX_I0    gX_PhysicsBody_SetMassCenter(gX_PhysicsBody body,
    gX_F32v3 offset);
gX_F32v3 gX_PhysicsBody_GetMassCenter(gX_PhysicsBody body);
gX_I0    gX_PhysicsBody_SetDamping(gX_PhysicsBody body,
    gX_F32 linear, gX_F32 angular);

//opencode: configure contact behavior and broad-phase filtering.
gX_I0 gX_PhysicsBody_SetMaterial(gX_PhysicsBody body,
    gX_PhysicsMaterial material);
gX_I0 gX_PhysicsBody_SetCollisionFilter(gX_PhysicsBody body,
    gX_U32 categoryBits, gX_U32 maskBits);
gX_I0 gX_PhysicsBody_SetSensor(gX_PhysicsBody body, gX_I1 sensor);
gX_I0 gX_PhysicsBody_SetUserTag(gX_PhysicsBody body, gX_U64 tag);
gX_U64 gX_PhysicsBody_GetUserTag(gX_PhysicsBody body);

gX_I0 gX_PhysicsCollider_SetMaterial(gX_PhysicsCollider collider,
    gX_PhysicsMaterial material);
gX_I0 gX_PhysicsCollider_SetCollisionFilter(gX_PhysicsCollider collider,
    gX_U32 categoryBits, gX_U32 maskBits);
gX_I0 gX_PhysicsCollider_SetSensor(gX_PhysicsCollider collider, gX_I1 sensor);
gX_I0 gX_PhysicsCollider_SetUserTag(gX_PhysicsCollider collider, gX_U64 tag);
gX_U64 gX_PhysicsCollider_GetUserTag(gX_PhysicsCollider collider);

//opencode: apply forces/torques (accumulated, cleared each step)
gX_I0    gX_PhysicsBody_ApplyForce(gX_PhysicsBody body, gX_F32v3 force);
gX_I0    gX_PhysicsBody_ApplyTorque(gX_PhysicsBody body, gX_F32v3 torque);
gX_I0    gX_PhysicsBody_ApplyForceAtPos(gX_PhysicsBody body,
              gX_F32v3 force, gX_F32v3 pos);

// ========================================================================
// Static collision geometry
// ========================================================================

// ========================================================================
// Contact joint cleanup
// ========================================================================

//opencode: must be called after each step to release temporary contact
//   joints created by collision response.
gX_I0 gX_PhysicsWorld_EmptyContactGroup(gX_PhysicsWorld world);
gX_I0 gX_PhysicsWorld_SetMaxContacts(gX_PhysicsWorld world, gX_I32 count);
gX_I0 gX_PhysicsWorld_SetThreading(gX_PhysicsWorld world, gX_I1 enable);

// ========================================================================
// Spatial queries
// ========================================================================

//opencode: count dynamic bodies (geoms with attached dBody) in the world space.
gX_I32 gX_PhysicsWorld_GetBodyCount(gX_PhysicsWorld world);

//opencode: get body handle by index in [0, GetBodyCount-1]. Index corresponds
//   to the order of geoms in the space with attached bodies. Returns
//   gX_PHYSICS_BODY_NULL if index is out of range or geom has no body.
gX_PhysicsBody gX_PhysicsWorld_GetBodyByIndex(gX_PhysicsWorld world,
    gX_I32 index);

// ========================================================================
// Raycasting
// ========================================================================

typedef struct gX_PhysicsRaycastHit {
    gX_I1       hit;
    gX_F32v3    position;
    gX_F32v3    normal;
    gX_F32      distance;
    gX_PhysicsBody body;    // hit body (gX_PHYSICS_BODY_NULL for static geom)
} gX_PhysicsRaycastHit;

//opencode: cast a ray against all geoms in the world. direction should be
//   normalized. Returns 1 if anything was hit, 0 otherwise. outHit is filled
//   with the closest hit (if any). Uses temporary ray geom internally.
gX_I1 gX_PhysicsWorld_Raycast(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_PhysicsRaycastHit *outHit);
gX_I1 gX_PhysicsWorld_RaycastFiltered(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_U32 maskBits, gX_I1 includeSensors,
    gX_PhysicsRaycastHit *outHit);

// ========================================================================
// Joints — constraints between two bodies
// ========================================================================

//opencode: ball joint — 3 rotational DOF, keeps bodies at fixed anchor point.
//   Pass gX_PHYSICS_BODY_NULL for bodyB to attach to static environment.
gX_PhysicsJoint gX_PhysicsJoint_CreateBall(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor);

//opencode: hinge joint — 1 rotational DOF around axis. Supports limits and motor.
gX_PhysicsJoint gX_PhysicsJoint_CreateHinge(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis);

//opencode: slider joint — 1 translational DOF along axis. Supports limits and motor.
gX_PhysicsJoint gX_PhysicsJoint_CreateSlider(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 axis);

//opencode: universal joint — 2 rotational DOF, two perpendicular axes.
gX_PhysicsJoint gX_PhysicsJoint_CreateUniversal(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2);

//opencode: fixed joint — 0 DOF, locks relative transform of two bodies.
//   NOTE: the current relative transform is locked at creation time.
//   Pass gX_PHYSICS_BODY_NULL for bodyB to pin to static environment.
gX_PhysicsJoint gX_PhysicsJoint_CreateFixed(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB);

//ai: hinge2 — a hinge with a suspension: axis1 carries the suspension
//   slide (a soft constraint; softness via SetSuspension) and the steering
//   rotation, axis2 is the wheel spin axis. Group-1 params
//   (SetLimits/SetMotor/SetStops/GetPosition/GetPositionRate) act on the
//   steering, the *2 variants on the spin. ODE needs two real bodies for
//   hinge2, so a NULL bodyB binds to the world's hidden kinematic anchor
//   (the static environment).
gX_PhysicsJoint gX_PhysicsJoint_CreateHinge2(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2);
gX_I0 gX_PhysicsJoint_SetSuspension(gX_PhysicsJoint joint,
    gX_F32 erp, gX_F32 cfm);
//ai: second-axis motor/limits — the hinge2 spin or the piston rotation.
gX_I0 gX_PhysicsJoint_SetMotor2(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax);
gX_I0 gX_PhysicsJoint_SetLimits2(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop);

//ai: AMotor — up to three angular axes between two bodies with per-axis
//   limits and motors. `AMotorSetEuler` selects ODE's swing-twist chain
//   (axis A fixed in body A, axis B fixed in body B, the middle axis
//   derived by ODE): the middle angle is the swing away from the reference
//   direction, so it is what ball joints use for cone limits.
//   `AMotorSetAxes` selects user mode (all axes fixed in body A) — what
//   axis motors (spin a body about an axis) use; user-mode stops
//   additionally need AMotorSetAngle, ODE has no angle source there.
//   Axes are 0..2; the generic SetLimits/SetMotor/SetsStops drive axis 0.
gX_PhysicsJoint gX_PhysicsJoint_CreateAMotor(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB);
gX_I0 gX_PhysicsJoint_AMotorSetEuler(gX_PhysicsJoint joint,
    gX_F32v3 axisA, gX_F32v3 axisB);
gX_I0 gX_PhysicsJoint_AMotorSetAxes(gX_PhysicsJoint joint, gX_I32 count,
    const gX_F32v3 *axes);
gX_I0 gX_PhysicsJoint_AMotorSetLimits(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 loStop, gX_F32 hiStop);
gX_I0 gX_PhysicsJoint_AMotorSetMotor(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 vel, gX_F32 fMax);
gX_I0 gX_PhysicsJoint_AMotorSetAngle(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 angle);
gX_F32 gX_PhysicsJoint_AMotorGetAngle(gX_PhysicsJoint joint, gX_I32 axis);
gX_F32 gX_PhysicsJoint_AMotorGetAngleRate(gX_PhysicsJoint joint,
    gX_I32 axis);

//ai: piston — a slide along its axis plus a free rotation about it (the
//   screw/drill DOF). The group-1 params drive the slide
//   (SetLimits/SetMotor, GetPosition/GetPositionRate), the *2 variants the
//   rotation (SetMotor2/SetLimits2, GetPosition2/GetPosition2Rate; ODE
//   measures the angle from an arbitrary reference, so use rates/deltas).
//   A NULL bodyB binds to the world's hidden kinematic anchor.
gX_PhysicsJoint gX_PhysicsJoint_CreatePiston(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis);

//opencode: common joint parameters
gX_I0 gX_PhysicsJoint_SetLimits(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop);gX_I0 gX_PhysicsJoint_SetMotor(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax);
gX_I0 gX_PhysicsJoint_SetStops(gX_PhysicsJoint joint,
    gX_F32 bounce, gX_F32 cfm, gX_F32 erp);

//opencode: destroy a joint. joint handle becomes invalid after call.
gX_I0 gX_PhysicsJoint_Destroy(gX_PhysicsWorld world,
    gX_PhysicsJoint joint);

//opencode: joint state queries
gX_F32   gX_PhysicsJoint_GetPosition(gX_PhysicsJoint joint);gX_F32   gX_PhysicsJoint_GetPositionRate(gX_PhysicsJoint joint);
//ai: hinge2 second-axis (spin) queries; 0 for other joint types.
gX_F32   gX_PhysicsJoint_GetPosition2(gX_PhysicsJoint joint);
gX_F32   gX_PhysicsJoint_GetPosition2Rate(gX_PhysicsJoint joint);
//ai: joint feedback — EnableFeedback registers ODE's per-step constraint
//   force/torque report; GetFeedback returns the force/torque applied to
//   the joint's first body (bodyA) in world coordinates and 0 when the
//   joint has no feedback registered. The world owns the storage.
gX_I0    gX_PhysicsJoint_EnableFeedback(gX_PhysicsJoint joint, gX_I1 enabled);
gX_I1    gX_PhysicsJoint_GetFeedback(gX_PhysicsJoint joint,
    gX_F32v3 *force, gX_F32v3 *torque);
gX_F32v3 gX_PhysicsJoint_GetAnchor(gX_PhysicsJoint joint);
gX_F32v3 gX_PhysicsJoint_GetAxis(gX_PhysicsJoint joint);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_PHYSICS_H
