/**********************************************************************************************
*
*   [gX: src/physics/physics_tests.c]
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
//ai: physics wrapper integration tests for world isolation, rigid-body
//   properties, joints, forces, raycast, sensors, contact events, the
//   ragdoll articulation, and single-threaded step determinism.
//   Ported from gameX src/physics/physics_tests.c.
#include "../common/log.h"
#include "../gxmath/quaternion.h"
#include "../gxmath/vector3.h"
#include "../wrappers/physics.h"
#include "ragdoll.h"
#include "elements.h"
#include "em.h"
#include "relativity.h"
#include "thermal.h"
#include "fluid.h"
#include "physics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static gX_U32 g_physicsTestContacts = 0;

//ai: expose the physics test entry points to the main test runner.
gX_I32 gX_Tests_Physics(gX_I0);
gX_I32 gX_Tests_Physics_Joints(gX_I0);
gX_I32 gX_Tests_Physics_Forces(gX_I0);
gX_I32 gX_Tests_Physics_Raycast(gX_I0);
gX_I32 gX_Tests_Physics_Sensor(gX_I0);
gX_I32 gX_Tests_Physics_Ragdoll(gX_I0);
gX_I32 gX_Tests_Physics_RagdollControl(gX_I0);
gX_I32 gX_Tests_Physics_Determinism(gX_I0);
gX_I32 gX_Tests_Physics_Elements(gX_I0);
gX_I32 gX_Tests_Physics_EM(gX_I0);
gX_I32 gX_Tests_Physics_Relativity(gX_I0);
gX_I32 gX_Tests_Physics_Dipoles(gX_I0);
gX_I32 gX_Tests_Physics_Surfaces(gX_I0);
gX_I32 gX_Tests_Physics_Sleeping(gX_I0);
gX_I32 gX_Tests_Physics_Kinematic(gX_I0);
gX_I32 gX_Tests_Physics_Mass(gX_I0);
gX_I32 gX_Tests_Physics_Hinge2(gX_I0);
gX_I32 gX_Tests_Physics_Feedback(gX_I0);
gX_I32 gX_Tests_Physics_SelfContact(gX_I0);
gX_I32 gX_Tests_Physics_AMotor(gX_I0);
gX_I32 gX_Tests_Physics_Piston(gX_I0);
gX_I32 gX_Tests_Physics_Heightfield(gX_I0);
gX_I32 gX_Tests_Physics_Thermal(gX_I0);
gX_I32 gX_Tests_Physics_Fluid(gX_I0);
gX_I32 gX_Tests_Physics_Events(gX_I0);
gX_I32 gX_Tests_Physics_Destroy(gX_I0);
gX_I32 gX_Tests_Physics_Tuning(gX_I0);

//ai: record contact events (phase, tags, contact count) for assertions.
#define GX_TEST_MAX_EVENTS 64
static gX_PhysicsContactPhase g_eventPhases[GX_TEST_MAX_EVENTS];
static gX_U64 g_eventTagA[GX_TEST_MAX_EVENTS];
static gX_U64 g_eventTagB[GX_TEST_MAX_EVENTS];
static gX_U32 g_eventCounts[GX_TEST_MAX_EVENTS];
static gX_U32 g_eventTotal = 0;

static gX_I0 physicsTestEventCB_(gX_I0 *userData,
    const gX_PhysicsContactEvent *event) {
    (void)userData;
    if (g_eventTotal >= GX_TEST_MAX_EVENTS) return;
    g_eventPhases[g_eventTotal] = event->phase;
    g_eventTagA[g_eventTotal] = event->tagA;
    g_eventTagB[g_eventTotal] = event->tagB;
    g_eventCounts[g_eventTotal] = event->contactCount;
    g_eventTotal++;
}

static gX_I0 g_eventsReset_(gX_I0) {
    g_eventTotal = 0;
}

//ai: count how many recorded events match a phase + tag pair.
static gX_I32 g_eventsCount_(gX_PhysicsContactPhase phase, gX_U64 tagA, gX_U64 tagB) {
    gX_I32 n = 0;
    for (gX_U32 i = 0; i < g_eventTotal; i++) {
        if (g_eventPhases[i] != phase) continue;
        gX_U64 a = g_eventTagA[i];
        gX_U64 b = g_eventTagB[i];
        if ((a == tagA && b == tagB) || (a == tagB && b == tagA)) n++;
    }
    return n;
}

//ai: helper — step a world once and return contact count of a phase.
static gX_I0 g_step_(gX_PhysicsWorld world, gX_I32 steps) {
    for (gX_I32 i = 0; i < steps; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
}

//ai: count contacts without changing the wrapper's default response.
static gX_I32 physicsTestCollision_(gX_I0 *userData,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_U32 contactCount, const gX_PhysicsContact *contacts) {
    (void)userData;
    (void)bodyA;
    (void)bodyB;
    (void)contacts;
    g_physicsTestContacts += contactCount;
    return 0;
}

//ai: sensor-dedicated callback — flags that a sensor pair reported.
static gX_I32 g_physicsSensorFired = 0;
static gX_I32 physicsTestSensorCB_(gX_I0 *userData,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_U32 contactCount, const gX_PhysicsContact *contacts) {
    (void)userData;
    (void)bodyA;
    (void)bodyB;
    (void)contactCount;
    (void)contacts;
    g_physicsSensorFired = 1;
    return 0;
}

//ai: componentwise float3 comparison within epsilon.
static gX_I1 v3Close_(gX_F32v3 a, gX_F32v3 b, gX_F32 eps) {
    return (fabsf(a.x - b.x) <= eps) &&
           (fabsf(a.y - b.y) <= eps) &&
           (fabsf(a.z - b.z) <= eps);
}

//ai: validate world isolation, static geometry, and body properties.
gX_I32 gX_Tests_Physics(gX_I0) {
    gX_I32 errors = 0;

    //ai: if ODE is unavailable, skip the whole physics suite.
    gX_PhysicsWorld probe = gX_PhysicsWorld_Create();
    if (probe.internal == 0) {
        gX_TraceLog(gX_LOG_NOTICE, "GX_TESTS: physics unavailable — wrapper stubs active");
        return 0;
    }
    gX_PhysicsWorld_Destroy(probe);

    errors += gX_Tests_Physics_Joints();
    errors += gX_Tests_Physics_Forces();
    errors += gX_Tests_Physics_Raycast();
    errors += gX_Tests_Physics_Sensor();
    errors += gX_Tests_Physics_Ragdoll();
    errors += gX_Tests_Physics_RagdollControl();
    errors += gX_Tests_Physics_Determinism();
    errors += gX_Tests_Physics_Elements();
    errors += gX_Tests_Physics_EM();
    errors += gX_Tests_Physics_Relativity();
    errors += gX_Tests_Physics_Dipoles();
    errors += gX_Tests_Physics_Surfaces();
    errors += gX_Tests_Physics_Sleeping();
    errors += gX_Tests_Physics_Kinematic();
    errors += gX_Tests_Physics_Mass();
    errors += gX_Tests_Physics_Hinge2();
    errors += gX_Tests_Physics_Feedback();
    errors += gX_Tests_Physics_SelfContact();
    errors += gX_Tests_Physics_AMotor();
    errors += gX_Tests_Physics_Piston();
    errors += gX_Tests_Physics_Heightfield();
    errors += gX_Tests_Physics_Thermal();
    errors += gX_Tests_Physics_Fluid();
    errors += gX_Tests_Physics_Events();
    errors += gX_Tests_Physics_Destroy();
    errors += gX_Tests_Physics_Tuning();

    gX_PhysicsWorld worldA = gX_PhysicsWorld_Create();
    gX_PhysicsWorld worldB = gX_PhysicsWorld_Create();
    if (worldA.internal == 0 || worldB.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics world re-creation after probe");
        if (worldA.internal != 0) gX_PhysicsWorld_Destroy(worldA);
        if (worldB.internal != 0) gX_PhysicsWorld_Destroy(worldB);
        return errors + 1;
    }

    gX_PhysicsWorld_SetGravity(worldA, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetCollisionCallback(worldA, physicsTestCollision_, NULL);
    gX_PhysicsWorld_SetMaxContacts(worldA, 8);
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(worldA,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsBody body = gX_PhysicsBody_AddBox(worldA,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f,
        (gX_F32v3){0.0f, 4.0f, 0.0f});
    gX_PhysicsBody other = gX_PhysicsBody_AddSphere(worldB,
        0.5f, 1.0f, (gX_F32v3){0.0f, 4.0f, 0.0f});
    if (body.internal == 0 || other.internal == 0 || ground.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics primitive creation");
        errors++;
    }

    gX_PhysicsMaterial material = gX_PhysicsMaterial_Default();
    material.friction = 0.25f;
    material.restitution = 0.0f;
    gX_PhysicsBody_SetMaterial(body, material);
    gX_PhysicsBody_SetCollisionFilter(body, 2u, 1u);
    gX_PhysicsBody_SetUserTag(body, 42u);
    gX_PhysicsBody_SetType(body, gX_PHYSICS_BODY_KINEMATIC);
    if (gX_PhysicsBody_GetType(body) != gX_PHYSICS_BODY_KINEMATIC ||
        gX_PhysicsBody_GetUserTag(body) != 42u) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics body properties");
        errors++;
    }
    gX_PhysicsBody_SetType(body, gX_PHYSICS_BODY_DYNAMIC);
    gX_PhysicsBody_SetRotation(body, gX_F32Qua_FromAxisAngle(
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.5f));
    if (gX_PhysicsBody_GetMass(body) <= 0.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics body mass query");
        errors++;
    }

    for (gX_I32 i = 0; i < 120; i++) {
        gX_PhysicsWorld_Step(worldA, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(worldA);
        gX_PhysicsWorld_Step(worldB, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(worldB);
    }
    gX_F32v3 position = gX_PhysicsBody_GetPosition(body);
    if (position.y < 0.45f || position.y > 1.5f || g_physicsTestContacts == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: physics settling pos.y=%.3f contacts=%u",
            position.y, g_physicsTestContacts);
        errors++;
    }

    gX_PhysicsBody_Destroy(worldA, body);
    gX_PhysicsBody_Destroy(worldB, other);
    gX_PhysicsCollider_Destroy(worldA, ground);
    gX_PhysicsWorld_Destroy(worldA);
    gX_PhysicsWorld_Destroy(worldB);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics done — %d errors", errors);
    return errors;
}

// ========================================================================
// Joints — creation, query round-trip, and limit enforcement
// ========================================================================

gX_I32 gX_Tests_Physics_Joints(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 4);

    //ai: each joint type gets its OWN body pair so the constrained
    //   bodies don't tumble each other's axes during the step loop.
    gX_PhysicsBody a;
    gX_PhysicsBody b;

    //ai: ball joint — anchor round-trip.
    a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){1.0f, 2.0f, 0.0f});
    if (a.internal == 0 || b.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ball joint body creation");
        errors++;
    } else {
        gX_PhysicsJoint ball = gX_PhysicsJoint_CreateBall(world, a, b,
            (gX_F32v3){0.0f, 2.0f, 0.0f});
        if (ball.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: ball joint creation");
            errors++;
        } else {
            gX_F32v3 anchor = gX_PhysicsJoint_GetAnchor(ball);
            if (!v3Close_(anchor, (gX_F32v3){0.0f, 2.0f, 0.0f}, 1e-3f)) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: ball anchor=(%.3f,%.3f,%.3f)",
                    anchor.x, anchor.y, anchor.z);
                errors++;
            }
            gX_PhysicsJoint_Destroy(world, ball);
        }
        gX_PhysicsBody_Destroy(world, a);
        gX_PhysicsBody_Destroy(world, b);
    }

    //ai: fixed joint — locks relative transform, position query = 0.
    a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){1.0f, 2.0f, 0.0f});
    if (a.internal == 0 || b.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fixed joint body creation");
        errors++;
    } else {
        gX_PhysicsJoint fixed = gX_PhysicsJoint_CreateFixed(world, a, b);
        if (fixed.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: fixed joint creation");
            errors++;
        } else if (gX_PhysicsJoint_GetPosition(fixed) != 0.0f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: fixed joint position should be 0");
            errors++;
            gX_PhysicsJoint_Destroy(world, fixed);
        } else {
            gX_PhysicsJoint_Destroy(world, fixed);
        }
        gX_PhysicsBody_Destroy(world, a);
        gX_PhysicsBody_Destroy(world, b);
    }

    //ai: hinge joint — axis round-trip + limits enforce angle.
    a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){1.0f, 2.0f, 0.0f});
    if (a.internal == 0 || b.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge joint body creation");
        errors++;
    } else {
        gX_PhysicsJoint hinge = gX_PhysicsJoint_CreateHinge(world, a, b,
            (gX_F32v3){0.0f, 2.0f, 0.0f}, (gX_F32v3){0.0f, 1.0f, 0.0f});
        if (hinge.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge joint creation");
            errors++;
        } else {
            gX_PhysicsJoint_SetLimits(hinge, -0.5f, 0.5f);
            gX_F32v3 axis = gX_PhysicsJoint_GetAxis(hinge);
            if (!v3Close_(axis, (gX_F32v3){0.0f, 1.0f, 0.0f}, 1e-3f)) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge axis=(%.3f,%.3f,%.3f)",
                    axis.x, axis.y, axis.z);
                errors++;
            }
            //ai: step — hinge angle must stay inside its limits.
            for (gX_I32 i = 0; i < 60; i++) {
                gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
                gX_PhysicsWorld_EmptyContactGroup(world);
            }
            gX_F32 hingePos = gX_PhysicsJoint_GetPosition(hinge);
            if (hingePos < -0.55f || hingePos > 0.55f) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge angle %.3f outside [-0.5,0.5]", hingePos);
                errors++;
            }
            gX_PhysicsJoint_Destroy(world, hinge);
        }
        gX_PhysicsBody_Destroy(world, a);
        gX_PhysicsBody_Destroy(world, b);
    }

    //ai: slider joint — axis round-trip + translational limits.
    a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){1.0f, 2.0f, 0.0f});
    if (a.internal == 0 || b.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: slider joint body creation");
        errors++;
    } else {
        gX_PhysicsJoint slider = gX_PhysicsJoint_CreateSlider(world, a, b,
            (gX_F32v3){1.0f, 0.0f, 0.0f});
        if (slider.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: slider joint creation");
            errors++;
        } else {
            gX_PhysicsJoint_SetLimits(slider, -0.4f, 0.4f);
            gX_F32v3 axis = gX_PhysicsJoint_GetAxis(slider);
            if (!v3Close_(axis, (gX_F32v3){1.0f, 0.0f, 0.0f}, 1e-3f)) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: slider axis=(%.3f,%.3f,%.3f)",
                    axis.x, axis.y, axis.z);
                errors++;
            }
            for (gX_I32 i = 0; i < 60; i++) {
                gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
                gX_PhysicsWorld_EmptyContactGroup(world);
            }
            gX_F32 sliderPos = gX_PhysicsJoint_GetPosition(slider);
            if (sliderPos < -0.45f || sliderPos > 0.45f) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: slider pos %.3f outside [-0.4,0.4]", sliderPos);
                errors++;
            }
            gX_PhysicsJoint_Destroy(world, slider);
        }
        gX_PhysicsBody_Destroy(world, a);
        gX_PhysicsBody_Destroy(world, b);
    }

    //ai: universal joint — anchor + axis1 round-trip.
    a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){1.0f, 2.0f, 0.0f});
    if (a.internal == 0 || b.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: universal joint body creation");
        errors++;
    } else {
        gX_PhysicsJoint univ = gX_PhysicsJoint_CreateUniversal(world, a, b,
            (gX_F32v3){0.0f, 2.0f, 0.0f},
            (gX_F32v3){1.0f, 0.0f, 0.0f}, (gX_F32v3){0.0f, 0.0f, 1.0f});
        if (univ.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: universal joint creation");
            errors++;
        } else {
            gX_F32v3 anchor = gX_PhysicsJoint_GetAnchor(univ);
            if (!v3Close_(anchor, (gX_F32v3){0.0f, 2.0f, 0.0f}, 1e-3f)) {
                gX_TraceLog(gX_LOG_ERROR, "FAIL: universal anchor=(%.3f,%.3f,%.3f)",
                    anchor.x, anchor.y, anchor.z);
                errors++;
            }
            gX_PhysicsJoint_Destroy(world, univ);
        }
        gX_PhysicsBody_Destroy(world, a);
        gX_PhysicsBody_Destroy(world, b);
    }

    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics joints — %d errors", errors);
    return errors;
}

// ========================================================================
// Forces — linear/angular impulse and off-center application
// ========================================================================

gX_I32 gX_Tests_Physics_Forces(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    //ai: zero gravity so a single force cleanly changes velocity.
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, 0.0f, 0.0f});

    //ai: linear force on a sphere → velocity rises.
    gX_PhysicsBody sphere = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (sphere.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: force test sphere creation");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    gX_PhysicsBody_ApplyForce(sphere, (gX_F32v3){0.0f, 10.0f, 0.0f});
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
    gX_F32v3 vel = gX_PhysicsBody_GetLinearVel(sphere);
    if (vel.y <= 0.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ApplyForce vel.y=%.3f (expected > 0)", vel.y);
        errors++;
    }

    //ai: torque about Z → angular velocity about Z.
    gX_PhysicsBody box = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    if (box.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: force test box creation");
        gX_PhysicsBody_Destroy(world, sphere);
        gX_PhysicsWorld_Destroy(world);
        return errors + 1;
    }
    gX_PhysicsBody_ApplyTorque(box, (gX_F32v3){0.0f, 0.0f, 5.0f});
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
    gX_F32v3 avel = gX_PhysicsBody_GetAngularVel(box);
    if (fabsf(avel.z) <= 1e-6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ApplyTorque avel.z=%.6f (expected != 0)", avel.z);
        errors++;
    }

    //ai: force at an off-center point → both linear and angular velocity.
    gX_PhysicsBody_ApplyForceAtPos(box, (gX_F32v3){5.0f, 0.0f, 0.0f},
        (gX_F32v3){0.25f, 0.25f, 0.0f});
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
    gX_F32v3 vel2 = gX_PhysicsBody_GetLinearVel(box);
    gX_F32v3 avel2 = gX_PhysicsBody_GetAngularVel(box);
    if (vel2.x <= 1e-6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ApplyForceAtPos vel.x=%.6f", vel2.x);
        errors++;
    }
    if (fabsf(avel2.z) <= 1e-6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ApplyForceAtPos avel.z=%.6f (off-center)", avel2.z);
        errors++;
    }

    gX_PhysicsBody_Destroy(world, box);
    gX_PhysicsBody_Destroy(world, sphere);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics forces — %d errors", errors);
    return errors;
}

// ========================================================================
// Raycast — hit, miss, and collision-filtered queries
// ========================================================================

gX_I32 gX_Tests_Physics_Raycast(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    //ai: box centered at origin, size 2.0. Raycast down from y=5.
    gX_PhysicsBody box = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){2.0f, 2.0f, 2.0f}, 1.0f, (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (box.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast test box creation");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    gX_PhysicsBody_SetCollisionFilter(box, 1u, 0xFFFFFFFFu);

    //ai: downward ray should hit the box top at y=+1, distance 4.0.
    gX_PhysicsRaycastHit hit;
    gX_I1 found = gX_PhysicsWorld_Raycast(world,
        (gX_F32v3){0.0f, 5.0f, 0.0f}, (gX_F32v3){0.0f, -1.0f, 0.0f},
        10.0f, &hit);
    if (!found || !hit.hit) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast downward miss");
        errors++;
    } else {
        if (fabsf(hit.position.y - 1.0f) > 1e-2f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast pos.y=%.3f (expected ~1.0)",
                hit.position.y);
            errors++;
        }
        if (fabsf(hit.distance - 4.0f) > 0.1f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast dist=%.3f (expected ~4.0)",
                hit.distance);
            errors++;
        }
        if (hit.body.internal == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast did not return the hit body");
            errors++;
        }
    }

    //ai: upward ray (away from box) → miss.
    gX_PhysicsRaycastHit miss;
    gX_I1 foundUp = gX_PhysicsWorld_Raycast(world,
        (gX_F32v3){0.0f, 5.0f, 0.0f}, (gX_F32v3){0.0f, 1.0f, 0.0f},
        10.0f, &miss);
    if (foundUp || miss.hit) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: raycast upward should miss");
        errors++;
    }

    //ai: filtered raycast with a mask that excludes the box's category.
    gX_PhysicsRaycastHit filtered;
    gX_I1 foundFiltered = gX_PhysicsWorld_RaycastFiltered(world,
        (gX_F32v3){0.0f, 5.0f, 0.0f}, (gX_F32v3){0.0f, -1.0f, 0.0f},
        10.0f, 2u, 0, &filtered);
    if (foundFiltered || filtered.hit) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: filtered raycast should miss (mask excludes box)");
        errors++;
    }

    gX_PhysicsBody_Destroy(world, box);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics raycast — %d errors", errors);
    return errors;
}

// ========================================================================
// Sensor — callback fires but no contact response
// ========================================================================

gX_I32 gX_Tests_Physics_Sensor(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_SetCollisionCallback(world, physicsTestSensorCB_, NULL);
    g_physicsSensorFired = 0;

    //ai: static ground + a large sensor volume that a box falls into.
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsCollider sensor = gX_PhysicsWorld_AddStaticBox(world,
        (gX_F32v3){2.0f, 1.0f, 2.0f}, (gX_F32v3){0.0f, 3.0f, 0.0f});
    gX_PhysicsCollider_SetSensor(sensor, 1);
    gX_PhysicsCollider_SetUserTag(sensor, 0x7E);
    if (ground.internal == 0 || sensor.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor test geometry creation");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    if (gX_PhysicsCollider_GetUserTag(sensor) != 0x7Eu) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor user tag round-trip");
        errors++;
    }

    //ai: a normal dynamic box falls through the sensor volume.
    gX_PhysicsBody falling = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 6.0f, 0.0f});
    if (falling.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor test falling box creation");
        gX_PhysicsWorld_Destroy(world);
        return errors + 1;
    }

    for (gX_I32 i = 0; i < 180; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }

    //ai: sensor callback must have fired (box passed through volume).
    if (!g_physicsSensorFired) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor callback never fired");
        errors++;
    }

    //ai: the falling box should rest on the ground (~0.25 up) — it was
    //   NOT blocked by the sensor volume (sensors create no contacts).
    gX_F32v3 fallPos = gX_PhysicsBody_GetPosition(falling);
    if (fallPos.y < 0.2f || fallPos.y > 1.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: falling box pos.y=%.3f (expected ~0.25)",
            fallPos.y);
        errors++;
    }

    gX_PhysicsBody_Destroy(world, falling);
    gX_PhysicsCollider_Destroy(world, sensor);
    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics sensor — %d errors", errors);
    return errors;
}

// ========================================================================
// Contact events — enter/stay/exit transitions via SetEventCallback
// ========================================================================

gX_I32 gX_Tests_Physics_Events(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_SetEventCallback(world, physicsTestEventCB_, NULL);

    //ai: static ground + a floating box far above (no contact).
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsCollider_SetUserTag(ground, 1u);
    //ai: zero restitution on BOTH sides — contacts use the pair's max,
    //   so the ground's default 0.1 restitution would make the box bounce.
    gX_PhysicsMaterial groundMat = gX_PhysicsMaterial_Default();
    groundMat.restitution = 0.0f;
    gX_PhysicsCollider_SetMaterial(ground, groundMat);
    if (ground.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: event test ground plane creation");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }

    //ai: case 3 (zero events while nothing collides). Static so it can
    //   never fall and land — a dynamic "floating" box would drop over the
    //   long step counts and jitter on the ground, flooding the event buffer.
    gX_PhysicsCollider floating = gX_PhysicsWorld_AddStaticBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, (gX_F32v3){0.0f, 8.0f, 0.0f});
    if (floating.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: event test floating box creation");
        gX_PhysicsCollider_Destroy(world, ground);
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    g_eventsReset_();
    g_step_(world, 10);
    if (g_eventTotal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: events fired with nothing touching (%u)", g_eventTotal);
        errors++;
    }

    //ai: case 1 — drop a box onto the ground: ENTER once, STAY while
    //   resting, then teleport it up to force EXIT on the next step.
    gX_PhysicsBody box = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 4.0f, 0.0f});
    gX_PhysicsBody_SetUserTag(box, 0xABCDu);
    //ai: zero restitution so the landing box never bounces off and
    //   re-enters — ENTER must fire exactly once.
    gX_PhysicsMaterial boxMat = gX_PhysicsMaterial_Default();
    boxMat.restitution = 0.0f;
    gX_PhysicsBody_SetMaterial(box, boxMat);
    if (box.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: event test drop box creation");
        gX_PhysicsCollider_Destroy(world, floating);
        gX_PhysicsCollider_Destroy(world, ground);
        gX_PhysicsWorld_Destroy(world);
        return errors + 1;
    }

    g_eventsReset_();
    g_step_(world, 180);  //ai: falls, lands, rests
    gX_I32 enterBox = g_eventsCount_(gX_PHYSICS_PHASE_ENTER, 0xABCDu, 1u);
    gX_I32 stayBox = g_eventsCount_(gX_PHYSICS_PHASE_STAY, 0xABCDu, 1u);
    if (enterBox < 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: box ENTER count=%d (expected >=1)", enterBox);
        errors++;
    }
    if (stayBox == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: box STAY count=%d (expected >0 while resting)", stayBox);
        errors++;
    }

    //ai: teleport away → next step fires EXIT for the box/ground pair.
    g_eventsReset_();
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 8.0f, 0.0f});
    g_step_(world, 1);
    if (g_eventsCount_(gX_PHYSICS_PHASE_EXIT, 0xABCDu, 1u) != 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: box EXIT after teleport count=%d (expected 1)",
            g_eventsCount_(gX_PHYSICS_PHASE_EXIT, 0xABCDu, 1u));
        errors++;
    }

    //ai: re-enable helper — ODE auto-disables resting bodies, so a
    //   SetPosition after a long rest must be followed by Enable + zero vel.
    //   (A disabled body ignores gravity and never falls back to the ground.)
    gX_PhysicsBody_Enable(box);
    gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){0.0f, 0.0f, 0.0f});

    //ai: ordering — a fresh drop must see ENTER before any EXIT.
    g_eventsReset_();
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 4.0f, 0.0f});
    g_step_(world, 60);
    gX_I1 sawEnter = 0;
    gX_I1 exitBeforeEnter = 0;
    for (gX_U32 i = 0; i < g_eventTotal; i++) {
        if (g_eventPhases[i] == gX_PHYSICS_PHASE_ENTER) sawEnter = 1;
        if (g_eventPhases[i] == gX_PHYSICS_PHASE_EXIT && !sawEnter) exitBeforeEnter = 1;
    }
    if (!sawEnter) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: re-drop never produced ENTER");
        errors++;
    }
    if (exitBeforeEnter) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: EXIT observed before ENTER in re-drop");
        errors++;
    }

    //ai: case 2 — sensor trigger volume with tag 0x7E; the falling box
    //   passes through it and the events fire with the sensor's tag.
    gX_PhysicsCollider sensor = gX_PhysicsWorld_AddStaticBox(world,
        (gX_F32v3){2.0f, 1.0f, 2.0f}, (gX_F32v3){0.0f, 3.0f, 0.0f});
    gX_PhysicsCollider_SetSensor(sensor, 1);
    gX_PhysicsCollider_SetUserTag(sensor, 0x7Eu);
    if (sensor.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: event sensor creation");
        errors++;
    } else {
        g_eventsReset_();
        gX_PhysicsBody_Enable(box);
        gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 6.0f, 0.0f});
        g_step_(world, 120);  //ai: falls through sensor, rests on ground
        gX_I32 sensorEnter = g_eventsCount_(gX_PHYSICS_PHASE_ENTER, 0xABCDu, 0x7Eu);
        gX_I32 sensorExit = g_eventsCount_(gX_PHYSICS_PHASE_EXIT, 0xABCDu, 0x7Eu);
        if (sensorEnter < 1) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor ENTER count=%d (expected >=1)", sensorEnter);
            errors++;
        }
        if (sensorExit < 1) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: sensor EXIT count=%d (expected >=1)", sensorExit);
            errors++;
        }
        gX_F32v3 restPos = gX_PhysicsBody_GetPosition(box);
        if (restPos.y < 0.2f || restPos.y > 1.0f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: event sensor box pos.y=%.3f (expected ~0.25)",
                restPos.y);
            errors++;
        }
        gX_PhysicsCollider_Destroy(world, sensor);
    }

    //ai: case 4 — destroy mid-contact must not leave a dangling EXIT
    //   referencing the freed body, and must not crash.
    g_eventsReset_();
    gX_PhysicsBody_Enable(box);
    gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 2.0f, 0.0f});
    g_step_(world, 30);  //ai: box lands and rests
    gX_PhysicsBody_Destroy(world, box);
    g_eventsReset_();
    g_step_(world, 5);
    if (g_eventTotal != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: destroyed body produced %u events", g_eventTotal);
        errors++;
    }

    gX_PhysicsCollider_Destroy(world, floating);
    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics events — %d errors", errors);
    return errors;
}

//ai: body destruction must remove the geom too. ODE's dBodyDestroy only
//   detaches attached geoms, so a destroy that skips dGeomDestroy leaves a
//   ghost collider at the body's last pose (found by the sandbox landing
//   test: a destroyed blocker held up the next box).
gX_I32 gX_Tests_Physics_Destroy(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsBody blocker = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.0f, 0.5f, 0.0f});
    g_step_(world, 10);
    gX_PhysicsBody_Destroy(world, blocker);

    gX_PhysicsBody probe = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 2.0f, (gX_F32v3){0.0f, 6.0f, 0.0f});
    g_step_(world, 120);
    gX_F32 y = gX_PhysicsBody_GetPosition(probe).y;
    if (!(y < 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: destroyed body left a ghost geom (probe y=%.2f)", y);
        errors++;
    }

    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics destroy — %d errors", errors);
    return errors;
}

//ai: module tuning policy — GX_PHYSICS_TUNE pins the quality level and the
//   pin survives a frame-history reset; the adaptive default starts at max
//   quality. The adaptive thresholds themselves are frame-time driven and
//   are exercised by the sandbox HUD/smoke.
gX_I32 gX_Tests_Physics_Tuning(gX_I0) {
    gX_I32 errors = 0;

    //ai: the test owns GX_PHYSICS_TUNE for its run — save any outer value
    //   (audit/capture pins) and restore it afterwards so the app's scene
    //   still sees its pin after the startup test pass.
    gX_I8 savedTune[32];
    savedTune[0] = 0;
    const gX_I8 *outerTune = (const gX_I8 *)getenv("GX_PHYSICS_TUNE");
    if (outerTune && outerTune[0]) {
        strncpy((char *)savedTune, (const char *)outerTune, sizeof(savedTune) - 1);
        savedTune[sizeof(savedTune) - 1] = 0;
    }
    unsetenv("GX_PHYSICS_TUNE");

    gX_Physics_Init();
    if (physics.tuningPin != -1 || physics.tuningLevel != 0
        || physics.maxSubSteps != 4) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: tuning default pin=%d level=%d substeps=%d",
            physics.tuningPin, physics.tuningLevel, physics.maxSubSteps);
        errors++;
    }
    gX_Physics_ResetFrameHistory();
    if (physics.tuningLevel != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning reset lost level 0");
        errors++;
    }

    setenv("GX_PHYSICS_TUNE", "2", 1);
    gX_Physics_Shutdown();
    gX_Physics_Init();
    if (physics.tuningPin != 2 || physics.tuningLevel != 2
        || physics.maxSubSteps != 2) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: tuning pin not applied pin=%d level=%d substeps=%d",
            physics.tuningPin, physics.tuningLevel, physics.maxSubSteps);
        errors++;
    }
    gX_Physics_ResetFrameHistory();
    if (physics.tuningLevel != 2) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: tuning pin lost on reset (level=%d)",
            physics.tuningLevel);
        errors++;
    }
    gX_Physics_Shutdown();

    if (savedTune[0]) setenv("GX_PHYSICS_TUNE", (const char *)savedTune, 1);
    else unsetenv("GX_PHYSICS_TUNE");

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics tuning — %d errors", errors);
    return errors;
}

// ========================================================================
// Ragdoll — creation, T-pose, reset, settle, disable, destroy
// ========================================================================

gX_I32 gX_Tests_Physics_Ragdoll(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);

    //ai: ground plane the ragdoll collapses onto during the settle test.
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    if (ground.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll ground plane creation");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }

    gX_Ragdoll rd;
    gX_I32 created = gX_Ragdoll_CreateHumanoid(&rd, world,
        (gX_F32v3){0.0f, 0.0f, 0.0f}, 1.0f);
    if (created != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll create returned %d", created);
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }

    if (gX_Ragdoll_GetBoneCount(&rd) != GX_RAGDOLL_BONE_COUNT) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll bone count %u (expected %d)",
            (gX_U32)gX_Ragdoll_GetBoneCount(&rd), GX_RAGDOLL_BONE_COUNT);
        errors++;
    }
    if (gX_Ragdoll_GetJointCount(&rd) != GX_RAGDOLL_MAX_JOINTS) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll joint count %u (expected %d)",
            (gX_U32)gX_Ragdoll_GetJointCount(&rd), GX_RAGDOLL_MAX_JOINTS);
        errors++;
    }

    //ai: verify the upright T-pose layout. Origin at feet (y=0).
    //   Pelvis < torso < head in height; arms at ±x; legs below pelvis.
    gX_PhysicsBody pelvis = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_PELVIS);
    gX_PhysicsBody torso  = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_TORSO);
    gX_PhysicsBody head   = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_HEAD);
    gX_PhysicsBody armL   = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_ARM_UPPER_L);
    gX_PhysicsBody armR   = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_ARM_UPPER_R);
    gX_PhysicsBody legL   = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_LEG_UPPER_L);
    gX_PhysicsBody legR   = gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_LEG_UPPER_R);
    if (pelvis.internal == 0 || torso.internal == 0 || head.internal == 0 ||
        armL.internal == 0 || armR.internal == 0 ||
        legL.internal == 0 || legR.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll bone bodies missing");
        errors++;
    } else {
        gX_F32v3 p = gX_PhysicsBody_GetPosition(pelvis);
        gX_F32v3 t = gX_PhysicsBody_GetPosition(torso);
        gX_F32v3 h = gX_PhysicsBody_GetPosition(head);
        gX_F32v3 al = gX_PhysicsBody_GetPosition(armL);
        gX_F32v3 ar = gX_PhysicsBody_GetPosition(armR);
        gX_F32v3 ll = gX_PhysicsBody_GetPosition(legL);
        gX_F32v3 lr = gX_PhysicsBody_GetPosition(legR);
        if (!(p.y < t.y && t.y < h.y)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: T-pose heights pelvis=%.2f torso=%.2f head=%.2f",
                p.y, t.y, h.y);
            errors++;
        }
        if (!(al.x < 0.0f && ar.x > 0.0f)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: T-pose arms al.x=%.2f ar.x=%.2f",
                al.x, ar.x);
            errors++;
        }
        if (!(ll.y < p.y && lr.y < p.y)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: T-pose legs below pelvis ll.y=%.2f lr.y=%.2f p.y=%.2f",
                ll.y, lr.y, p.y);
            errors++;
        }
    }

    //ai: rescaled masses — bones run at human weight in the sandbox's tonne
    //   convention (nominal kg / 1000) so water buoyancy and prop collisions
    //   treat dummies as human-weight; the nominal mass rides along for the
    //   thermal/impact-FX registries.
    if (torso.internal != 0) {
        gX_F32 torsoSim = gX_PhysicsBody_GetMass(torso);
        gX_F32 nominal = rd.bones[GX_RAGDOLL_BONE_TORSO].mass;
        if (!(torsoSim > 0.0f && torsoSim < 0.05f)
            || fabsf(nominal - 12.0f) > 0.01f
            || fabsf(torsoSim - nominal * 0.001f) > 0.002f
            || !(rd.bones[GX_RAGDOLL_BONE_TORSO].radius > 0.0f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: ragdoll torso mass sim=%.4f nominal=%.2f",
                torsoSim, nominal);
            errors++;
        }
    }

    //ai: step a few frames, then reset to the T-pose.
    for (gX_I32 i = 0; i < 10; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    if (gX_Ragdoll_ResetPose(&rd, world) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll reset pose");
        errors++;
    } else {
        gX_F32v3 p = gX_PhysicsBody_GetPosition(pelvis);
        if (fabsf(p.y - 0.95f) > 1e-2f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: reset pelvis y=%.3f (expected ~0.95)", p.y);
            errors++;
        }
    }

    //ai: settle — ragdoll collapses onto the ground but stays finite and
    //   within a sane bounding box (no explosion / NaN).
    gX_I1 finiteOk = 1;
    gX_F32 minY = 1e9f, maxY = -1e9f;
    gX_I32 anyBelowGround = 0;
    for (gX_I32 i = 0; i < 120; i++) {
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    for (gX_U32 i = 0; i < rd.boneCount; i++) {
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(rd.bones[i].body);
        if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z)) {
            finiteOk = 0;
        }
        if (pos.y < minY) minY = pos.y;
        if (pos.y > maxY) maxY = pos.y;
        if (pos.y < -0.5f) anyBelowGround = 1;
    }
    if (!finiteOk) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll non-finite position after settle");
        errors++;
    }
    if (anyBelowGround) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll bone below ground (minY=%.3f)", minY);
        errors++;
    }
    if (maxY > 3.5f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll exploded (maxY=%.3f)", maxY);
        errors++;
    }

    //ai: disable all bones → each reports disabled.
    if (gX_Ragdoll_SetEnabled(&rd, world, 0) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll set enabled(0)");
        errors++;
    } else {
        gX_I1 allDisabled = 1;
        for (gX_U32 i = 0; i < rd.boneCount; i++) {
            if (gX_PhysicsBody_IsEnabled(rd.bones[i].body)) allDisabled = 0;
        }
        if (!allDisabled) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll disable did not disable all bones");
            errors++;
        }
    }

    //ai: destroy → counts zeroed.
    if (gX_Ragdoll_Destroy(&rd, world) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll destroy");
        errors++;
    } else if (gX_Ragdoll_GetBoneCount(&rd) != 0 ||
               gX_Ragdoll_GetJointCount(&rd) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: ragdoll destroy did not zero counts");
        errors++;
    }

    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics ragdoll — %d errors", errors);
    return errors;
}

// ========================================================================
// Active ragdoll controller — bounded upright hold and recovery
// ========================================================================

gX_I32 gX_Tests_Physics_RagdollControl(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    gX_Ragdoll rd;
    gX_Ragdoll_Control control;
    gX_PhysicsCollider ground;

    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    if (ground.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "RAGDOLL_CONTROL FAIL: ground");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    if (gX_Ragdoll_CreateHumanoid(&rd, world,
            (gX_F32v3){0.0f, 0.0f, 0.0f}, 1.0f) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "RAGDOLL_CONTROL FAIL: create");
        gX_PhysicsCollider_Destroy(world, ground);
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    if (gX_Ragdoll_Control_Init(&control, &rd, world) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "RAGDOLL_CONTROL FAIL: init");
        errors++;
    }
    if (gX_Ragdoll_Control_Init(NULL, &rd, world) == 0) {
        gX_TraceLog(gX_LOG_ERROR, "RAGDOLL_CONTROL FAIL: null init accepted");
        errors++;
    }

    gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_ACTIVE);
    for (gX_I32 i = 0; i < 300; i++) {
        gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    {
        gX_I1 finite = 1;
        gX_F32 minY = 1e9f;
        gX_F32 maxY = -1e9f;
        for (gX_U32 i = 0; i < rd.boneCount; i++) {
            gX_F32v3 pos = gX_PhysicsBody_GetPosition(rd.bones[i].body);
            if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z))
                finite = 0;
            if (pos.y < minY) minY = pos.y;
            if (pos.y > maxY) maxY = pos.y;
        }
        if (!finite || minY < -0.5f || maxY > 3.5f) {
            gX_TraceLog(gX_LOG_ERROR,
                "RAGDOLL_CONTROL FAIL: upright bounds min=%.2f max=%.2f",
                minY, maxY);
            errors++;
        }
        if (gX_Ragdoll_Control_UpDot(&rd) < 0.60f) {
            gX_TraceLog(gX_LOG_ERROR,
                "RAGDOLL_CONTROL FAIL: torso not upright");
            errors++;
        }
        if (gX_PhysicsBody_GetPosition(
                gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_PELVIS)).y < 0.60f) {
            gX_TraceLog(gX_LOG_ERROR,
                "RAGDOLL_CONTROL FAIL: pelvis dropped");
            errors++;
        }
    }

    gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_PASSIVE);
    for (gX_I32 i = 0; i < 120; i++) {
        gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_ACTIVE);
    gX_PhysicsBody_SetLinearVel(gX_Ragdoll_GetBone(&rd,
        GX_RAGDOLL_BONE_TORSO), (gX_F32v3){1.5f, 0.0f, 0.0f});
    for (gX_I32 i = 0; i < 240; i++) {
        gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    if (!isfinite(gX_PhysicsBody_GetPosition(
            gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_HEAD)).y)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RAGDOLL_CONTROL FAIL: non-finite after impulse");
        errors++;
    }

    gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_FALLING);
    for (gX_I32 i = 0; i < 120; i++) {
        gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    if (gX_Ragdoll_Control_GetMode(&control)
            == gX_RAGDOLL_CONTROL_FALLING) {
        gX_TraceLog(gX_LOG_ERROR,
            "RAGDOLL_CONTROL FAIL: falling did not leave fall state");
        errors++;
    }
    for (gX_I32 i = 0; i < 300; i++) {
        gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    if (gX_Ragdoll_Control_UpDot(&rd) < 0.60f
        || gX_PhysicsBody_GetPosition(gX_Ragdoll_GetBone(&rd,
            GX_RAGDOLL_BONE_PELVIS)).y < 0.60f) {
        gX_TraceLog(gX_LOG_ERROR,
            "RAGDOLL_CONTROL FAIL: get-up did not recover");
        errors++;
    }

    {
        gX_F32v3 firstPelvis;
        gX_F32v3 firstHead;

        gX_Ragdoll_ResetPose(&rd, world);
        gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_ACTIVE);
        for (gX_I32 i = 0; i < 120; i++) {
            gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        firstPelvis = gX_PhysicsBody_GetPosition(
            gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_PELVIS));
        firstHead = gX_PhysicsBody_GetPosition(
            gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_HEAD));
        gX_Ragdoll_ResetPose(&rd, world);
        gX_Ragdoll_Control_SetMode(&control, gX_RAGDOLL_CONTROL_ACTIVE);
        for (gX_I32 i = 0; i < 120; i++) {
            gX_Ragdoll_Control_Step(&control, 1.0f / 60.0f);
            gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(world);
        }
        if (!v3Close_(firstPelvis, gX_PhysicsBody_GetPosition(
                gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_PELVIS)), 1e-5f)
            || !v3Close_(firstHead, gX_PhysicsBody_GetPosition(
                gX_Ragdoll_GetBone(&rd, GX_RAGDOLL_BONE_HEAD)), 1e-5f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "RAGDOLL_CONTROL FAIL: repeat diverged");
            errors++;
        }
    }

    gX_Ragdoll_Destroy(&rd, world);
    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: ragdoll control — %d errors", errors);
    return errors;
}

// ========================================================================
// Determinism — identical worlds must step bit-identically
// ========================================================================

#define GX_TEST_DETERMINISM_BODIES 12
#define GX_TEST_DETERMINISM_STEPS  600

//ai: build identical content into a world (shared by the determinism pair).
static gX_I32 gX_Tests_Physics_Determinism_Build(gX_PhysicsWorld world,
    gX_PhysicsBody *out, gX_I32 count) {
    gX_I32 made = 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsMaterial mat = gX_PhysicsMaterial_Default();
    mat.friction = 0.5f;
    mat.restitution = 0.05f;
    for (gX_I32 i = 0; i < count; i++) {
        gX_F32 x = ((gX_F32)(i % 4) - 1.5f) * 1.1f;
        gX_F32 z = ((gX_F32)((i / 4) % 3) - 1.0f) * 1.1f;
        gX_F32 y = 2.5f + (gX_F32)(i % 5) * 0.9f;
        gX_PhysicsBody b;
        if (i % 3 == 2) {
            b = gX_PhysicsBody_AddSphere(world, 0.4f, 1.0f, (gX_F32v3){x, y, z});
        } else {
            b = gX_PhysicsBody_AddBox(world, (gX_F32v3){0.7f, 0.7f, 0.7f},
                1.0f, (gX_F32v3){x, y, z});
        }
        if (b.internal == 0) return made;
        gX_PhysicsBody_SetMaterial(b, mat);
        gX_PhysicsBody_SetRotation(b, gX_F32Qua_FromAxisAngle(
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.3f * (gX_F32)i));
        out[made++] = b;
    }
    return made;
}

gX_I32 gX_Tests_Physics_Determinism(gX_I0) {
    gX_I32 errors = 0;
    const gX_I32 count = GX_TEST_DETERMINISM_BODIES;

    gX_PhysicsWorld worldA = gX_PhysicsWorld_Create();
    gX_PhysicsWorld worldB = gX_PhysicsWorld_Create();
    if (worldA.internal == 0 || worldB.internal == 0) {
        if (worldA.internal != 0) gX_PhysicsWorld_Destroy(worldA);
        if (worldB.internal != 0) gX_PhysicsWorld_Destroy(worldB);
        return 0;
    }

    gX_PhysicsBody bodiesA[GX_TEST_DETERMINISM_BODIES];
    gX_PhysicsBody bodiesB[GX_TEST_DETERMINISM_BODIES];
    gX_I32 countA = gX_Tests_Physics_Determinism_Build(worldA, bodiesA, count);
    gX_I32 countB = gX_Tests_Physics_Determinism_Build(worldB, bodiesB, count);
    if (countA != count || countB != count) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: determinism build created %d/%d bodies (expected %d)",
            countA, countB, count);
        errors++;
    }

    //ai: step both worlds in lockstep — same order, same dt, same calls.
    for (gX_I32 step = 0; step < GX_TEST_DETERMINISM_STEPS; step++) {
        gX_PhysicsWorld_Step(worldA, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(worldA);
        gX_PhysicsWorld_Step(worldB, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(worldB);
    }

    //ai: bit-exact comparison of transforms and velocities.
    gX_I32 mismatches = 0;
    for (gX_I32 i = 0; i < count && errors == 0; i++) {
        gX_F32v3 posA = gX_PhysicsBody_GetPosition(bodiesA[i]);
        gX_F32v3 posB = gX_PhysicsBody_GetPosition(bodiesB[i]);
        gX_F32v3 velA = gX_PhysicsBody_GetLinearVel(bodiesA[i]);
        gX_F32v3 velB = gX_PhysicsBody_GetLinearVel(bodiesB[i]);
        gX_F32Qua rotA, rotB;
        gX_PhysicsBody_GetRotation(bodiesA[i], &rotA);
        gX_PhysicsBody_GetRotation(bodiesB[i], &rotB);
        if (memcmp(&posA, &posB, sizeof(posA)) != 0 ||
            memcmp(&velA, &velB, sizeof(velA)) != 0 ||
            memcmp(&rotA, &rotB, sizeof(rotA)) != 0) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: determinism body %d A=(%.6f,%.6f,%.6f) B=(%.6f,%.6f,%.6f)",
                i, posA.x, posA.y, posA.z, posB.x, posB.y, posB.z);
            mismatches++;
        }
    }
    if (mismatches != 0) errors++;

    //ai: stability — the shared world must not explode or go non-finite.
    for (gX_I32 i = 0; i < count; i++) {
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(bodiesA[i]);
        if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z) ||
            pos.y < -2.0f || pos.y > 20.0f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: determinism stability body %d y=%.3f", i, pos.y);
            errors++;
            break;
        }
    }

    gX_PhysicsWorld_Destroy(worldA);
    gX_PhysicsWorld_Destroy(worldB);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics determinism — %d errors", errors);
    return errors;
}

// ========================================================================
// Elements — table invariants and density-driven mass
// ========================================================================

gX_I32 gX_Tests_Physics_Elements(gX_I0) {
    gX_I32 errors = 0;

    gX_I32 count = gX_Elements_Count();
    if (count != 12) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: element count %d (expected 12)", count);
        errors++;
    }

    gX_I32 previousNumber = 0;
    for (gX_I32 i = 0; i < count; i++) {
        const gX_Element_Info *info = gX_Elements_Get(i);
        if (!info) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: element %d has no table row", i);
            errors++;
            continue;
        }
        if (info->symbol[0] == 0 || info->name[0] == 0) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: element %d has an empty symbol/name", i);
            errors++;
        }
        if (info->atomicNumber <= previousNumber) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: element %d atomic number %d not increasing (prev %d)",
                i, info->atomicNumber, previousNumber);
            errors++;
        }
        previousNumber = info->atomicNumber;
        if (info->molarMass <= 0.0f || info->density <= 0.0f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: element %d non-positive mass/density (%.3f, %.3f)",
                i, info->molarMass, info->density);
            errors++;
        }
        if (info->specificHeat <= 0.0f || info->meltingPoint <= 0.0f ||
            info->emissivity <= 0.0f || info->emissivity > 1.0f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: element %d bad thermal data (cp %.1f, melt %.1f, e %.2f)",
                i, info->specificHeat, info->meltingPoint, info->emissivity);
            errors++;
        }
        //ai: contact parameters must stay inside the physically meaningful
        //   band (friction > 0, restitution in [0,1]) — zero friction makes
        //   the slider pin meaningless, >1 restitution pumps energy.
        if (info->friction <= 0.0f || info->friction > 1.2f ||
            info->restitution < 0.0f || info->restitution > 1.0f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: element %d bad contact data (mu %.2f, e %.2f)",
                i, info->friction, info->restitution);
            errors++;
        }
        //ai: look and susceptibility data must be usable (opaque albedo,
        //   0..1 metalness/roughness, finite chi, rolling in the crr band).
        if (info->albedo.a == 0
            || info->metalness < 0.0f || info->metalness > 1.0f
            || info->roughness < 0.0f || info->roughness > 1.0f
            || !isfinite(info->susceptibility)
            || info->rollingFriction < 0.0f
            || info->rollingFriction > 0.2f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: element %d bad look/susceptibility (m %.2f r %.2f chi %.2e roll %.3f)",
                i, info->metalness, info->roughness, info->susceptibility,
                info->rollingFriction);
            errors++;
        }
    }

    //ai: spot values (CRC): iron 7874 kg/m3, gold 19300 kg/m3 / 196.97 g/mol.
    const gX_Element_Info *iron = gX_Elements_Get(gX_ELEMENT_IRON);
    const gX_Element_Info *gold = gX_Elements_Get(gX_ELEMENT_GOLD);
    const gX_Element_Info *hydrogen = gX_Elements_Get(gX_ELEMENT_HYDROGEN);
    const gX_Element_Info *lead = gX_Elements_Get(gX_ELEMENT_LEAD);
    if (!iron || fabsf(iron->density - 7874.0f) > 1.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: iron density (expected 7874)");
        errors++;
    }
    if (!gold || fabsf(gold->density - 19300.0f) > 1.0f ||
        fabsf(gold->molarMass - 196.96657f) > 0.01f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: gold density/molar mass spot check");
        errors++;
    }
    if (!hydrogen || hydrogen->density > 0.2f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: hydrogen density (expected ~0.09)");
        errors++;
    }
    //ai: thermal spot values (CRC/engineering): iron melts at 1811 K, gold
    //   specific heat 129 J/(kg*K) and a low polished emissivity.
    if (!iron || fabsf(iron->meltingPoint - 1811.0f) > 1.0f ||
        fabsf(iron->specificHeat - 449.0f) > 1.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: iron thermal spot check");
        errors++;
    }
    if (!gold || fabsf(gold->specificHeat - 129.0f) > 1.0f ||
        gold->emissivity > 0.1f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: gold thermal spot check");
        errors++;
    }
    //ai: heat of fusion spot values (CRC): gold 64 kJ/kg, lead 23 kJ/kg.
    if (!gold || fabsf(gold->latentHeat - 64000.0f) > 1.0f ||
        !lead || fabsf(lead->latentHeat - 23000.0f) > 1.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: latent heat spot check");
        errors++;
    }
    //ai: contact spot: lead is the dead soft outlier — less bouncy than the
    //   springy hard metals, and it keeps more grip than gold.
    if (!lead || !iron || !gold ||
        !(lead->restitution < iron->restitution) ||
        !(lead->restitution <= gold->restitution) ||
        !(lead->friction > gold->friction)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: lead contact spot check");
        errors++;
    }
    //ai: look/susceptibility spot: polished gold and silver, matte graphite
    //   carbon, dull lead; iron carries the effective ferromagnetic chi
    //   while the control metals stay weak (< 1).
    const gX_Element_Info *carbon = gX_Elements_Get(gX_ELEMENT_CARBON);
    const gX_Element_Info *silver = gX_Elements_Get(gX_ELEMENT_SILVER);
    const gX_Element_Info *zinc = gX_Elements_Get(gX_ELEMENT_ZINC);
    if (!gold || !(gold->metalness > 0.9f) || !(gold->roughness < 0.3f)
        || !silver || !(silver->metalness > 0.9f)
        || !carbon || !(carbon->metalness < 0.2f)
        || !(lead->roughness > gold->roughness)
        || !(iron->susceptibility > 1.0f)
        || !(fabsf(lead->susceptibility) < 1.0f)
        || !(fabsf(silver->susceptibility) < 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: element look/susceptibility spot check");
        errors++;
    }
    //ai: rolling resistance spot: soft/rough lead and carbon roll deader
    //   than the hard polished metals.
    if (!lead || !iron || !carbon ||
        !(lead->rollingFriction > iron->rollingFriction) ||
        !(carbon->rollingFriction > iron->rollingFriction) ||
        !(gold->rollingFriction < zinc->rollingFriction)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: element rolling spot check");
        errors++;
    }
    if (gX_Elements_Get(gX_ELEMENT_NONE) != NULL ||
        gX_Elements_Get(count) != NULL) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: out-of-range element returned a row");
        errors++;
    }
    if (strcmp((const char *)gX_Elements_String(gX_ELEMENT_IRON), "IRON") != 0 ||
        strcmp((const char *)gX_Elements_String(gX_ELEMENT_NONE), "NONE") != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: element String mapping");
        errors++;
    }

    //ai: density x volume is the mass a solid unit box would carry in SI.
    if (gold && fabsf(gold->density * (1.0f * 1.0f * 1.0f) - 19300.0f) > 1.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: gold unit-box mass");
        errors++;
    }

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics elements — %d errors", errors);
    return errors;
}

// ========================================================================
// Electromagnetism — Coulomb sign, Lorentz direction, clamp, determinism
// ========================================================================

//ai: one EM step (forces then step), mirroring the module step hook.
static gX_I0 g_emStep_(gX_PhysicsWorld world) {
    gX_PhysicsEM_Apply();
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
}

gX_I32 gX_Tests_Physics_EM(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    //ai: zero gravity so the checks isolate the EM forces.
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetEnabled(1);

    //ai: like charges repel — distance must grow.
    gX_PhysicsBody bodyA = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody bodyB = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){2.0f, 0.0f, 0.0f});
    gX_PhysicsEM_AddBody(bodyA, 1.0f);
    gX_PhysicsEM_AddBody(bodyB, 1.0f);
    gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.0f, 0.0f});
    if (gX_PhysicsEM_Count() != 2) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: EM registry count %d (expected 2)",
            gX_PhysicsEM_Count());
        errors++;
    }
    for (gX_I32 i = 0; i < 60; i++) g_emStep_(world);
    gX_F32v3 posA = gX_PhysicsBody_GetPosition(bodyA);
    gX_F32v3 posB = gX_PhysicsBody_GetPosition(bodyB);
    gX_F32 separation = posB.x - posA.x;
    if (separation <= 2.0f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: like charges did not repel (separation %.3f)", separation);
        errors++;
    }

    //ai: opposite charges attract — same setup with the sign flipped.
    gX_PhysicsEM_RemoveBody(bodyA);
    gX_PhysicsEM_RemoveBody(bodyB);
    gX_PhysicsBody_SetPosition(bodyA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(bodyB, (gX_F32v3){2.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(bodyA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(bodyB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_AddBody(bodyA, 1.0f);
    gX_PhysicsEM_AddBody(bodyB, -1.0f);
    for (gX_I32 i = 0; i < 60; i++) g_emStep_(world);
    posA = gX_PhysicsBody_GetPosition(bodyA);
    posB = gX_PhysicsBody_GetPosition(bodyB);
    separation = fabsf(posB.x - posA.x);
    if (separation >= 2.0f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: opposite charges did not attract (separation %.3f)", separation);
        errors++;
    }

    //ai: Lorentz direction — q*(v x B) with v=+x, B=+y gives +z.
    gX_PhysicsEM_RemoveBody(bodyA);
    gX_PhysicsEM_RemoveBody(bodyB);
    gX_PhysicsBody_SetPosition(bodyA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(bodyA, (gX_F32v3){2.0f, 0.0f, 0.0f});
    gX_PhysicsEM_AddBody(bodyA, 1.0f);
    gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 2.0f, 0.0f});
    g_emStep_(world);
    gX_F32v3 velA = gX_PhysicsBody_GetLinearVel(bodyA);
    if (!(velA.z > 0.0f) || fabsf(velA.x - 2.0f) > 0.1f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: Lorentz direction vel=(%.3f,%.3f,%.3f)", velA.x, velA.y, velA.z);
        errors++;
    }

    //ai: coincident opposite charges — no defined direction, so the pair is
    //   skipped: forces must stay finite (no NaN/explosion).
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_SetEnabled(1);
    gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(bodyA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(bodyA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(bodyB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(bodyB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_AddBody(bodyA, 1.0f);
    gX_PhysicsEM_AddBody(bodyB, -1.0f);
    for (gX_I32 i = 0; i < 30; i++) g_emStep_(world);
    gX_F32v3 coincidentA = gX_PhysicsBody_GetPosition(bodyA);
    if (!isfinite(coincidentA.x) || !isfinite(coincidentA.y) || !isfinite(coincidentA.z)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: coincident charges produced non-finite motion");
        errors++;
    }

    gX_PhysicsBody_Destroy(world, bodyA);
    gX_PhysicsBody_Destroy(world, bodyB);
    gX_PhysicsWorld_Destroy(world);
    gX_PhysicsEM_Reset();

    //ai: EM determinism — identical charged worlds step bit-identically.
    gX_PhysicsWorld worldA = gX_PhysicsWorld_Create();
    gX_PhysicsWorld worldB = gX_PhysicsWorld_Create();
    if (worldA.internal != 0 && worldB.internal != 0) {
        gX_PhysicsBody bodiesA[4];
        gX_PhysicsBody bodiesB[4];
        for (gX_I32 i = 0; i < 4; i++) {
            gX_F32 x = (gX_F32)(i % 2) * 1.5f;
            gX_F32 z = (gX_F32)(i / 2) * 1.5f;
            bodiesA[i] = gX_PhysicsBody_AddSphere(worldA, 0.4f, 1.0f,
                (gX_F32v3){x, 1.0f + (gX_F32)i * 0.2f, z});
            bodiesB[i] = gX_PhysicsBody_AddSphere(worldB, 0.4f, 1.0f,
                (gX_F32v3){x, 1.0f + (gX_F32)i * 0.2f, z});
        }
        for (gX_I32 step = 0; step < 300; step++) {
            gX_PhysicsEM_Reset();
            gX_PhysicsEM_SetEnabled(1);
            gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.5f, 0.0f});
            for (gX_I32 i = 0; i < 4; i++) {
                gX_PhysicsEM_AddBody(bodiesA[i], (i % 2) ? -1.0f : 1.0f);
            }
            g_emStep_(worldA);
            gX_PhysicsEM_Reset();
            gX_PhysicsEM_SetEnabled(1);
            gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.5f, 0.0f});
            for (gX_I32 i = 0; i < 4; i++) {
                gX_PhysicsEM_AddBody(bodiesB[i], (i % 2) ? -1.0f : 1.0f);
            }
            g_emStep_(worldB);
        }
        gX_I32 mismatches = 0;
        for (gX_I32 i = 0; i < 4; i++) {
            gX_F32v3 posA = gX_PhysicsBody_GetPosition(bodiesA[i]);
            gX_F32v3 posB = gX_PhysicsBody_GetPosition(bodiesB[i]);
            if (memcmp(&posA, &posB, sizeof(posA)) != 0) mismatches++;
        }
        if (mismatches != 0) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: EM determinism — %d mismatching bodies", mismatches);
            errors++;
        }
        gX_PhysicsWorld_Destroy(worldA);
        gX_PhysicsWorld_Destroy(worldB);
    }
    gX_PhysicsEM_Reset();

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics em — %d errors", errors);
    return errors;
}

// ========================================================================
// Thermal — heat capacity, radiative cooling, impact heating, determinism
// ========================================================================

gX_I32 gX_Tests_Physics_Thermal(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    gX_PhysicsThermal_Reset();
    gX_PhysicsThermal_SetEnabled(1);

    //ai: heat capacity — dT = E/(mass*cp).
    gX_PhysicsBody body = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (gX_PhysicsThermal_AddBody(body, 300.0f, 2.0f, 500.0f, 0.5f, 0.0f, 0.0f) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: thermal body registration");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    gX_PhysicsThermal_HeatBody(body, 2.0e5f);
    gX_F32 heated = gX_PhysicsThermal_GetTemperature(body);
    if (fabsf(heated - 500.0f) > 0.1f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: heat capacity T=%.2f (expected 500)", heated);
        errors++;
    }

    //ai: radiative cooling — monotonic decay toward ambient, never below.
    gX_PhysicsThermal_SetTemperature(body, 1500.0f);
    gX_F32 previous = 1500.0f;
    gX_I32 monotonic = 1;
    for (gX_I32 i = 0; i < 180; i++) {
        gX_PhysicsThermal_Apply(1.0f / 60.0f);
        gX_F32 current = gX_PhysicsThermal_GetTemperature(body);
        if (current > previous + 1e-4f) monotonic = 0;
        if (current < 288.0f - 1e-3f) monotonic = 0;
        previous = current;
    }
    if (!monotonic || previous >= 1500.0f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: cooling not monotonic/anchored (T=%.2f)", previous);
        errors++;
    }
    if (previous > 1490.0f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: cooling did not decay (T=%.2f)", previous);
        errors++;
    }

    //ai: emissivity spread — a high-emissivity body cools faster.
    gX_PhysicsBody lowE = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){3.0f, 0.0f, 0.0f});
    gX_PhysicsBody highE = gX_PhysicsBody_AddSphere(world, 0.5f, 1.0f,
        (gX_F32v3){6.0f, 0.0f, 0.0f});
    gX_PhysicsThermal_AddBody(lowE, 1500.0f, 2.0f, 500.0f, 0.05f, 0.0f, 0.0f);
    gX_PhysicsThermal_AddBody(highE, 1500.0f, 2.0f, 500.0f, 0.90f, 0.0f, 0.0f);
    for (gX_I32 i = 0; i < 180; i++) {
        gX_PhysicsThermal_Apply(1.0f / 60.0f);
    }
    if (!(gX_PhysicsThermal_GetTemperature(highE)
          < gX_PhysicsThermal_GetTemperature(lowE) - 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: emissivity spread low=%.1f high=%.1f",
            gX_PhysicsThermal_GetTemperature(lowE),
            gX_PhysicsThermal_GetTemperature(highE));
        errors++;
    }

    //ai: impact heating — a falling body lands and warms above ambient.
    //ai: destroy the cooling-section bodies first so the drop has clear space.
    gX_PhysicsBody_Destroy(world, body);
    gX_PhysicsBody_Destroy(world, lowE);
    gX_PhysicsBody_Destroy(world, highE);
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    gX_PhysicsBody faller = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 2.0f, (gX_F32v3){0.0f, 4.0f, 0.0f});
    gX_PhysicsThermal_AddBody(faller, 288.0f, 2.0f, 500.0f, 0.5f, 0.0f, 0.0f);
    gX_PhysicsThermal_InstallCollisionCallback(world);
    for (gX_I32 i = 0; i < 150; i++) {
        gX_PhysicsThermal_Apply(1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    gX_F32 impactTemp = gX_PhysicsThermal_GetTemperature(faller);
    if (impactTemp <= 288.0f + 10.0f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: impact did not heat (T=%.2f)", impactTemp);
        errors++;
    }

    gX_PhysicsBody_Destroy(world, faller);
    gX_PhysicsCollider_Destroy(world, ground);

    //ai: contact conduction — two touching bodies exchange heat down the
    //   gradient (emissivity 0 isolates conduction from radiation).
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody hotBox = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.0f, 5.0f, 0.0f});
    gX_PhysicsBody coldBox = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.98f, 5.0f, 0.0f});
    gX_PhysicsThermal_AddBody(hotBox, 800.0f, 2.0f, 500.0f, 0.0f, 0.0f, 0.0f);
    gX_PhysicsThermal_AddBody(coldBox, 288.0f, 2.0f, 500.0f, 0.0f, 0.0f, 0.0f);
    gX_PhysicsThermal_InstallCollisionCallback(world);
    for (gX_I32 i = 0; i < 30; i++) {
        gX_PhysicsThermal_Apply(1.0f / 60.0f);
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
    }
    gX_F32 conductedHot = gX_PhysicsThermal_GetTemperature(hotBox);
    gX_F32 conductedCold = gX_PhysicsThermal_GetTemperature(coldBox);
    if (!(conductedCold > 288.2f) || !(conductedHot < 799.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: conduction hot=%.2f cold=%.2f (expected hot down, cold up)",
            conductedHot, conductedCold);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, hotBox);
    gX_PhysicsBody_Destroy(world, coldBox);

    //ai: melting — enthalpy plateaus at the melting point until the latent
    //   heat is absorbed, then the body is molten and reheats past it.
    gX_PhysicsBody melter = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.5f, 0.5f, 0.5f}, 1.0f, (gX_F32v3){0.0f, 8.0f, 0.0f});
    const gX_F32 meltPoint = 600.61f;
    const gX_F32 meltMass = 11.34f;
    const gX_F32 meltCp = 129.0f;
    const gX_F32 meltLatent = 23000.0f;
    gX_PhysicsThermal_AddBody(melter, 288.0f, meltMass, meltCp, 0.0f,
        meltPoint, meltLatent);
    gX_PhysicsThermal_SetTemperature(melter, meltPoint);
    if (gX_PhysicsThermal_IsMolten(melter) ||
        gX_PhysicsThermal_MeltFraction(melter) > 0.001f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: melting start state");
        errors++;
    }
    gX_PhysicsThermal_HeatBody(melter, meltMass * meltLatent * 0.5f);
    gX_F32 plateauTemp = gX_PhysicsThermal_GetTemperature(melter);
    gX_F32 plateauFraction = gX_PhysicsThermal_MeltFraction(melter);
    if (fabsf(plateauTemp - meltPoint) > 0.01f ||
        fabsf(plateauFraction - 0.5f) > 0.01f ||
        gX_PhysicsThermal_IsMolten(melter)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: melting plateau T=%.2f f=%.3f (expected %.2f / 0.5)",
            plateauTemp, plateauFraction, meltPoint);
        errors++;
    }
    gX_PhysicsThermal_HeatBody(melter, meltMass * meltLatent * 0.5f + 1.0f);
    if (!gX_PhysicsThermal_IsMolten(melter) ||
        !(gX_PhysicsThermal_GetTemperature(melter) > meltPoint)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: body did not become molten (T=%.2f)",
            gX_PhysicsThermal_GetTemperature(melter));
        errors++;
    }
    //ai: cooling back through the plateau must hold the melting point.
    gX_PhysicsThermal_HeatBody(melter, -meltMass * (meltLatent * 0.5f + 1.0f));
    gX_F32 refreezeTemp = gX_PhysicsThermal_GetTemperature(melter);
    if (gX_PhysicsThermal_IsMolten(melter) ||
        fabsf(refreezeTemp - meltPoint) > 0.01f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: refreeze plateau T=%.2f (expected %.2f)",
            refreezeTemp, meltPoint);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, melter);

    gX_PhysicsWorld_Destroy(world);
    gX_PhysicsThermal_Reset();

    //ai: thermal determinism — identical impact+cooling worlds bit-match.
    gX_PhysicsWorld worldA = gX_PhysicsWorld_Create();
    gX_PhysicsWorld worldB = gX_PhysicsWorld_Create();
    if (worldA.internal != 0 && worldB.internal != 0) {
        gX_PhysicsBody boxA = gX_PhysicsBody_AddBox(worldA,
            (gX_F32v3){0.5f, 0.5f, 0.5f}, 2.0f, (gX_F32v3){0.0f, 3.0f, 0.0f});
        gX_PhysicsBody boxB = gX_PhysicsBody_AddBox(worldB,
            (gX_F32v3){0.5f, 0.5f, 0.5f}, 2.0f, (gX_F32v3){0.0f, 3.0f, 0.0f});
        gX_PhysicsCollider groundA = gX_PhysicsWorld_AddGroundPlane(worldA,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
        gX_PhysicsCollider groundB = gX_PhysicsWorld_AddGroundPlane(worldB,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
        gX_F32 lastTempA = 0.0f, lastTempB = 0.0f;
        for (gX_I32 step = 0; step < 200; step++) {
            gX_PhysicsThermal_Reset();
            gX_PhysicsThermal_SetEnabled(1);
            gX_PhysicsThermal_AddBody(boxA, 288.0f, 2.0f, 500.0f, 0.5f, 0.0f, 0.0f);
            gX_PhysicsThermal_InstallCollisionCallback(worldA);
            gX_PhysicsThermal_Apply(1.0f / 60.0f);
            gX_PhysicsWorld_Step(worldA, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(worldA);
            lastTempA = gX_PhysicsThermal_GetTemperature(boxA);

            gX_PhysicsThermal_Reset();
            gX_PhysicsThermal_SetEnabled(1);
            gX_PhysicsThermal_AddBody(boxB, 288.0f, 2.0f, 500.0f, 0.5f, 0.0f, 0.0f);
            gX_PhysicsThermal_InstallCollisionCallback(worldB);
            gX_PhysicsThermal_Apply(1.0f / 60.0f);
            gX_PhysicsWorld_Step(worldB, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(worldB);
            lastTempB = gX_PhysicsThermal_GetTemperature(boxB);
        }
        if (memcmp(&lastTempA, &lastTempB, sizeof(lastTempA)) != 0) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: thermal determinism (%.6f vs %.6f)", lastTempA, lastTempB);
            errors++;
        }
        gX_PhysicsCollider_Destroy(worldA, groundA);
        gX_PhysicsCollider_Destroy(worldB, groundB);
        gX_PhysicsWorld_Destroy(worldA);
        gX_PhysicsWorld_Destroy(worldB);
    }
    gX_PhysicsThermal_Reset();

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics thermal — %d errors", errors);
    return errors;
}

// ========================================================================
// Fluid — Archimedes waterline, sink drag, entry events, determinism
// ========================================================================

//ai: one fluid step (forces then step), mirroring the module step hook.
static gX_I0 g_fluidStep_(gX_PhysicsWorld world) {
    gX_PhysicsFluid_Apply();
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
}

//ai: a 20x20 basin (level 0, floor -2) with the sandbox defaults.
static gX_I0 g_fluidConfigure_(gX_PhysicsWorld world) {
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsFluid_Reset();
    gX_PhysicsFluid_Settings settings = gX_PhysicsFluid_Defaults();
    settings.surfaceY = 0.0f;
    settings.minX = -10.0f;
    settings.maxX = 10.0f;
    settings.minZ = -10.0f;
    settings.maxZ = 10.0f;
    settings.floorY = -2.0f;
    gX_PhysicsFluid_SetSettings(settings);
    gX_PhysicsFluid_SetEnabled(1);
}

gX_I32 gX_Tests_Physics_Fluid(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    g_fluidConfigure_(world);
    gX_PhysicsCollider floor = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, -2.0f);

    //ai: a half-density sphere settles with its centre at the waterline
    //   (Archimedes: submerged fraction = density ratio) and queues one
    //   surface-entry event.
    gX_PhysicsBody floater = gX_PhysicsBody_AddSphere(world, 0.5f, 0.5f,
        (gX_F32v3){0.0f, 2.0f, 0.0f});
    gX_I32 slot = gX_PhysicsFluid_AddBody(floater, 0.5f);
    if (slot != 0 || gX_PhysicsFluid_Count() != 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fluid registry count %d (slot %d)",
            gX_PhysicsFluid_Count(), slot);
        errors++;
    }
    for (gX_I32 i = 0; i < 360; i++) g_fluidStep_(world);
    gX_F32 floaterY = gX_PhysicsBody_GetPosition(floater).y;
    if (fabsf(floaterY) > 0.08f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: float waterline y=%.3f (expected ~0)",
            floaterY);
        errors++;
    }
    gX_PhysicsFluid_Event event;
    if (gX_PhysicsFluid_EventCount() != 1
        || !gX_PhysicsFluid_EventGet(0, &event)
        || !(event.speed > 1.0f) || fabsf(event.position.y) > 0.001f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: fluid entry event count=%d speed=%.2f",
            gX_PhysicsFluid_EventCount(), event.speed);
        errors++;
    }
    gX_PhysicsFluid_EventClear();
    if (gX_PhysicsFluid_EventCount() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fluid event queue not cleared");
        errors++;
    }

    //ai: light rides high, dense sinks to the floor — the waterline tracks
    //   the density ratio.
    gX_PhysicsFluid_RemoveBody(floater);
    gX_PhysicsBody_Destroy(world, floater);
    gX_PhysicsBody light = gX_PhysicsBody_AddSphere(world, 0.5f, 0.25f,
        (gX_F32v3){4.0f, 2.0f, 0.0f});
    gX_PhysicsBody heavy = gX_PhysicsBody_AddSphere(world, 0.5f, 1.6f,
        (gX_F32v3){-4.0f, 2.0f, 0.0f});
    gX_PhysicsFluid_AddBody(light, 0.5f);
    gX_PhysicsFluid_AddBody(heavy, 0.5f);
    for (gX_I32 i = 0; i < 400; i++) g_fluidStep_(world);
    gX_F32 lightY = gX_PhysicsBody_GetPosition(light).y;
    gX_F32 heavyY = gX_PhysicsBody_GetPosition(heavy).y;
    if (!(lightY > 0.08f) || !(lightY < 0.6f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: light sphere waterline y=%.3f", lightY);
        errors++;
    }
    if (!(heavyY < -1.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: heavy sphere did not sink (y=%.3f)",
            heavyY);
        errors++;
    }

    //ai: drag is real — the same dense sphere falls farther when it is not
    //   registered with the fluid (wet vs dry drop).
    gX_PhysicsBody dry = gX_PhysicsBody_AddSphere(world, 0.5f, 1.6f,
        (gX_F32v3){6.0f, 2.0f, 0.0f});
    gX_PhysicsBody wet = gX_PhysicsBody_AddSphere(world, 0.5f, 1.6f,
        (gX_F32v3){-6.0f, 2.0f, 0.0f});
    gX_PhysicsFluid_AddBody(wet, 0.5f);
    for (gX_I32 i = 0; i < 60; i++) g_fluidStep_(world);
    gX_F32 dryY = gX_PhysicsBody_GetPosition(dry).y;
    gX_F32 wetY = gX_PhysicsBody_GetPosition(wet).y;
    if (!(dryY < wetY - 0.3f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fluid drag dry=%.2f wet=%.2f",
            dryY, wetY);
        errors++;
    }

    //ai: registry removal and reset restore the module state.
    gX_PhysicsFluid_RemoveBody(light);
    gX_PhysicsFluid_RemoveBody(heavy);
    gX_PhysicsFluid_RemoveBody(wet);
    if (gX_PhysicsFluid_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fluid removal left %d entries",
            gX_PhysicsFluid_Count());
        errors++;
    }
    gX_PhysicsFluid_Reset();
    gX_PhysicsFluid_Settings defaults = gX_PhysicsFluid_Defaults();
    gX_PhysicsFluid_Settings now = gX_PhysicsFluid_GetSettings();
    if (gX_PhysicsFluid_Enabled() || gX_PhysicsFluid_Count() != 0
        || now.density != defaults.density || now.surfaceY != defaults.surfaceY) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: fluid reset state");
        errors++;
    }

    //ai: two identical fluid worlds step bit-identically (the registry is
    //   global; Apply feeds both worlds' bodies before each lockstep step).
    gX_PhysicsWorld worldA = gX_PhysicsWorld_Create();
    gX_PhysicsWorld worldB = gX_PhysicsWorld_Create();
    if (worldA.internal == 0 || worldB.internal == 0) {
        if (worldA.internal != 0) gX_PhysicsWorld_Destroy(worldA);
        if (worldB.internal != 0) gX_PhysicsWorld_Destroy(worldB);
    } else {
        g_fluidConfigure_(worldA);
        g_fluidConfigure_(worldB);
        gX_PhysicsBody bodyA = gX_PhysicsBody_AddSphere(worldA, 0.5f, 0.5f,
            (gX_F32v3){0.0f, 2.0f, 0.0f});
        gX_PhysicsBody bodyB = gX_PhysicsBody_AddSphere(worldB, 0.5f, 0.5f,
            (gX_F32v3){0.0f, 2.0f, 0.0f});
        gX_PhysicsFluid_AddBody(bodyA, 0.5f);
        gX_PhysicsFluid_AddBody(bodyB, 0.5f);
        for (gX_I32 i = 0; i < 240; i++) {
            gX_PhysicsFluid_Apply();
            gX_PhysicsWorld_Step(worldA, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(worldA);
            gX_PhysicsWorld_Step(worldB, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(worldB);
        }
        gX_F32v3 posA = gX_PhysicsBody_GetPosition(bodyA);
        gX_F32v3 posB = gX_PhysicsBody_GetPosition(bodyB);
        if (memcmp(&posA, &posB, sizeof(posA)) != 0) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: fluid determinism (%.6f/%.6f)", posA.y, posB.y);
            errors++;
        }
        gX_PhysicsWorld_Destroy(worldA);
        gX_PhysicsWorld_Destroy(worldB);
    }
    gX_PhysicsFluid_Reset();

    gX_PhysicsCollider_Destroy(world, floor);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics fluid — %d errors", errors);
    return errors;
}

// ========================================================================
// Magnetic dipoles — pair force signs, alignment torque, determinism
// ========================================================================

//ai: one EM step (forces then step), mirroring the module step hook.
static gX_I0 g_dipoleStep_(gX_PhysicsWorld world) {
    gX_PhysicsEM_Apply();
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
}

gX_I32 gX_Tests_Physics_Dipoles(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;

    //ai: no gravity and no charges — the checks isolate dipole forces and
    //   torques (the unipolar module blends every mode, so keep this clean).
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_Reset();
    gX_PhysicsEM_Settings settings = gX_PhysicsEM_Defaults();
    settings.dipoleScale = 1.0f;
    gX_PhysicsEM_SetSettings(settings);
    gX_PhysicsEM_SetEnabled(1);

    //ai: registration — a dipole creates an entry with zero charge; the
    //   world moment follows the body rotation.
    gX_PhysicsBody magnetA = gX_PhysicsBody_AddSphere(world, 0.3f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody magnetB = gX_PhysicsBody_AddSphere(world, 0.3f, 1.0f,
        (gX_F32v3){1.5f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetA, (gX_F32v3){0.0f, 0.0f, 1.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){0.0f, 0.0f, 1.0f});
    if (gX_PhysicsEM_Count() != 2) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole registry count %d",
            gX_PhysicsEM_Count());
        errors++;
    }
    gX_F32v3 moment;
    if (!gX_PhysicsEM_GetDipole(magnetA, &moment)
        || fabsf(moment.z - 1.0f) > 1e-5f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole world read (z=%.3f)", moment.z);
        errors++;
    }
    gX_PhysicsBody_SetRotation(magnetA, gX_F32Qua_FromAxisAngle(
        (gX_F32v3){1.0f, 0.0f, 0.0f}, 3.14159265f / 2.0f));
    gX_PhysicsEM_GetDipole(magnetA, &moment);
    if (fabsf(moment.z) > 1e-3f || fabsf(moment.y) < 0.9f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole rotation (y=%.3f z=%.3f)",
            moment.y, moment.z);
        errors++;
    }
    gX_PhysicsBody_SetRotation(magnetA, gX_F32Qua_Identity());

    //ai: coaxial aligned moments attract — the moments lie along the
    //   separation; A speeds toward +x, B toward -x.
    gX_PhysicsEM_SetDipole(magnetA, (gX_F32v3){1.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){1.0f, 0.0f, 0.0f});
    for (gX_I32 i = 0; i < 5; i++) g_dipoleStep_(world);
    gX_F32v3 velA = gX_PhysicsBody_GetLinearVel(magnetA);
    gX_F32v3 velB = gX_PhysicsBody_GetLinearVel(magnetB);
    if (!(velA.x > 0.0f && velB.x < 0.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: aligned dipoles attract (%.3f %.3f)",
            velA.x, velB.x);
        errors++;
    }

    //ai: coaxial anti-aligned moments repel — the velocities flip.
    gX_PhysicsBody_SetPosition(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(magnetB, (gX_F32v3){1.5f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){-1.0f, 0.0f, 0.0f});
    for (gX_I32 i = 0; i < 5; i++) g_dipoleStep_(world);
    velA = gX_PhysicsBody_GetLinearVel(magnetA);
    velB = gX_PhysicsBody_GetLinearVel(magnetB);
    if (!(velA.x < 0.0f && velB.x > 0.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: anti-aligned dipoles repel (%.3f %.3f)",
            velA.x, velB.x);
        errors++;
    }

    //ai: parallel moments side by side (perpendicular to the separation)
    //   repel (the classic 3C m^2/r^4 push).
    gX_PhysicsBody_SetPosition(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(magnetB, (gX_F32v3){1.5f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetA, (gX_F32v3){0.0f, 0.0f, 1.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){0.0f, 0.0f, 1.0f});
    for (gX_I32 i = 0; i < 5; i++) g_dipoleStep_(world);
    velA = gX_PhysicsBody_GetLinearVel(magnetA);
    velB = gX_PhysicsBody_GetLinearVel(magnetB);
    if (!(velA.x < 0.0f && velB.x > 0.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: side-by-side parallel dipoles repel (%.3f %.3f)",
            velA.x, velB.x);
        errors++;
    }

    //ai: a perpendicular moment in a uniform field gains the aligning torque
    //   (m x B = -y) and rotates toward the field.
    gX_PhysicsBody_SetPosition(magnetA, (gX_F32v3){5.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(magnetB, (gX_F32v3){9.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetA, (gX_F32v3){1.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.0f, 1.0f});
    g_dipoleStep_(world);
    gX_F32v3 spin = gX_PhysicsBody_GetAngularVel(magnetA);
    if (!(spin.y < 0.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole torque direction (wy=%.3f)",
            spin.y);
        errors++;
    }
    gX_F32 maxAlignment = 0.0f;
    for (gX_I32 i = 0; i < 240; i++) {
        g_dipoleStep_(world);
        gX_PhysicsEM_GetDipole(magnetA, &moment);
        if (moment.z > maxAlignment) maxAlignment = moment.z;
    }
    if (!(maxAlignment > 0.9f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole did not align (%.3f)",
            maxAlignment);
        errors++;
    }
    gX_PhysicsEM_SetUniformB((gX_F32v3){0.0f, 0.0f, 0.0f});

    //ai: induced magnetism — a magnetizable body (alpha > 0) is pulled
    //   toward a dipole on the axis, while an identical unregistered
    //   control stays put.
    gX_PhysicsBody iron = gX_PhysicsBody_AddSphere(world, 0.3f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, 1.5f});
    gX_PhysicsBody control = gX_PhysicsBody_AddSphere(world, 0.3f, 1.0f,
        (gX_F32v3){0.0f, 0.0f, -1.5f});
    gX_PhysicsEM_SetSusceptibility(iron, 1.0f);
    gX_F32 chiRead = 0.0f;
    if (!gX_PhysicsEM_GetSusceptibility(iron, &chiRead)
        || fabsf(chiRead - 1.0f) > 1e-6f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: susceptibility round trip (%.3f)",
            chiRead);
        errors++;
    }
    gX_PhysicsBody_SetPosition(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetPosition(magnetB, (gX_F32v3){9.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(magnetA, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsEM_SetDipole(magnetA, (gX_F32v3){0.0f, 0.0f, 1.0f});
    gX_PhysicsEM_SetDipole(magnetB, (gX_F32v3){0.0f, 0.0f, 0.0f});
    for (gX_I32 i = 0; i < 30; i++) g_dipoleStep_(world);
    gX_F32 ironZ = gX_PhysicsBody_GetPosition(iron).z;
    gX_F32 controlZ = gX_PhysicsBody_GetPosition(control).z;
    if (!(ironZ < 1.45f) || fabsf(controlZ + 1.5f) > 1e-3f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: induced attraction iron=%.3f control=%.3f",
            ironZ, controlZ);
        errors++;
    }
    gX_PhysicsEM_RemoveBody(iron);
    gX_PhysicsEM_RemoveBody(control);
    gX_PhysicsBody_Destroy(world, iron);
    gX_PhysicsBody_Destroy(world, control);

    //ai: registry removal clears the dipole entry.
    gX_PhysicsEM_RemoveBody(magnetA);
    gX_PhysicsEM_RemoveBody(magnetB);
    if (gX_PhysicsEM_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dipole removal left %d entries",
            gX_PhysicsEM_Count());
        errors++;
    }
    gX_PhysicsBody_Destroy(world, magnetA);
    gX_PhysicsBody_Destroy(world, magnetB);

    //ai: determinism — two identical runs of the same setup. The registry is
    //   global, so the worlds are built and stepped serially; simultaneous
    //   registries would let pairwise dipole forces leak across worlds.
    gX_F32v3 finalA1 = {0.0f, 0.0f, 0.0f};
    gX_F32v3 finalA2 = {0.0f, 0.0f, 0.0f};
    gX_F32v3 finalB1 = {0.0f, 0.0f, 0.0f};
    gX_F32v3 finalB2 = {0.0f, 0.0f, 0.0f};
    for (gX_I32 run = 0; run < 2; run++) {
        gX_PhysicsWorld testWorld = gX_PhysicsWorld_Create();
        if (testWorld.internal == 0) break;
        gX_PhysicsWorld_SetGravity(testWorld, (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsEM_Reset();
        gX_PhysicsEM_SetSettings(settings);
        gX_PhysicsEM_SetEnabled(1);
        gX_PhysicsBody body1 = gX_PhysicsBody_AddSphere(testWorld, 0.3f, 1.0f,
            (gX_F32v3){0.0f, 0.0f, 0.0f});
        gX_PhysicsBody body2 = gX_PhysicsBody_AddSphere(testWorld, 0.3f, 1.0f,
            (gX_F32v3){0.0f, 1.2f, 0.3f});
        gX_PhysicsEM_SetDipole(body1, (gX_F32v3){1.0f, 0.0f, 1.0f});
        gX_PhysicsEM_SetDipole(body2, (gX_F32v3){0.0f, 1.0f, 0.0f});
        for (gX_I32 i = 0; i < 120; i++) {
            gX_PhysicsEM_Apply();
            gX_PhysicsWorld_Step(testWorld, 1.0f / 60.0f);
            gX_PhysicsWorld_EmptyContactGroup(testWorld);
        }
        if (run == 0) {
            finalA1 = gX_PhysicsBody_GetPosition(body1);
            finalA2 = gX_PhysicsBody_GetPosition(body2);
        } else {
            finalB1 = gX_PhysicsBody_GetPosition(body1);
            finalB2 = gX_PhysicsBody_GetPosition(body2);
        }
        gX_PhysicsWorld_Destroy(testWorld);
    }
    if (memcmp(&finalA1, &finalB1, sizeof(finalA1)) != 0
        || memcmp(&finalA2, &finalB2, sizeof(finalA2)) != 0) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: dipole determinism (%.6f/%.6f)", finalA1.x, finalB1.x);
        errors++;
    }
    gX_PhysicsEM_Reset();

    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics dipoles — %d errors", errors);
    return errors;
}

// ========================================================================
// Rolling friction + conveyor surfaces — decay, ordering, belt motion
// ========================================================================

//ai: one surface step (forces then step), mirroring the module step hook.
static gX_I0 g_surfaceStep_(gX_PhysicsWorld world) {
    gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
    gX_PhysicsWorld_EmptyContactGroup(world);
}

gX_I32 gX_Tests_Physics_Surfaces(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsCollider ground = gX_PhysicsWorld_AddGroundPlane(world,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);
    if (ground.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: surfaces ground plane");
        gX_PhysicsWorld_Destroy(world);
        return 1;
    }
    gX_PhysicsMaterial groundMat = gX_PhysicsMaterial_Default();
    gX_PhysicsCollider_SetMaterial(ground, groundMat);

    //ai: two identical rolling spheres — the one with rolling resistance
    //   must slow down while the slick one keeps rolling. Body damping is
    //   zeroed so the (per-step) world default does not mask the effect.
    gX_PhysicsBody slick = gX_PhysicsBody_AddSphere(world, 0.4f, 1.0f,
        (gX_F32v3){-4.0f, 0.4f, 0.0f});
    gX_PhysicsBody grippy = gX_PhysicsBody_AddSphere(world, 0.4f, 1.0f,
        (gX_F32v3){4.0f, 0.4f, 0.0f});
    gX_PhysicsMaterial slickMat = gX_PhysicsMaterial_Default();
    slickMat.friction = 0.8f;
    slickMat.rollingFriction = 0.0f;
    gX_PhysicsMaterial grippyMat = slickMat;
    grippyMat.rollingFriction = 0.05f;
    gX_PhysicsBody_SetMaterial(slick, slickMat);
    gX_PhysicsBody_SetMaterial(grippy, grippyMat);
    gX_PhysicsBody_SetDamping(slick, 0.0f, 0.0f);
    gX_PhysicsBody_SetDamping(grippy, 0.0f, 0.0f);
    gX_PhysicsBody_SetLinearVel(slick, (gX_F32v3){6.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetLinearVel(grippy, (gX_F32v3){6.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(slick, (gX_F32v3){0.0f, 0.0f, -15.0f});
    gX_PhysicsBody_SetAngularVel(grippy, (gX_F32v3){0.0f, 0.0f, -15.0f});
    for (gX_I32 i = 0; i < 240; i++) g_surfaceStep_(world);
    gX_F32 slickVx = gX_PhysicsBody_GetLinearVel(slick).x;
    gX_F32 grippyVx = gX_PhysicsBody_GetLinearVel(grippy).x;
    gX_F32 slickX = gX_PhysicsBody_GetPosition(slick).x + 4.0f;
    gX_F32 grippyX = gX_PhysicsBody_GetPosition(grippy).x - 4.0f;
    if (!(slickVx > grippyVx + 0.5f) || !(slickX > grippyX + 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: rolling resistance slick=%.2f/%.2f grippy=%.2f/%.2f",
            slickVx, slickX, grippyVx, grippyX);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, slick);
    gX_PhysicsBody_Destroy(world, grippy);

    //ai: spin resistance — with low interfacial friction (so ordinary
    //   friction does not dominate), spin about the contact normal winds
    //   down with rolling resistance and persists without it.
    gX_PhysicsBody spinFast = gX_PhysicsBody_AddSphere(world, 0.4f, 1.0f,
        (gX_F32v3){-2.0f, 0.4f, 6.0f});
    gX_PhysicsBody spinSlow = gX_PhysicsBody_AddSphere(world, 0.4f, 1.0f,
        (gX_F32v3){2.0f, 0.4f, 6.0f});
    gX_PhysicsMaterial spinLoose = gX_PhysicsMaterial_Default();
    spinLoose.friction = 0.02f;
    spinLoose.rollingFriction = 0.0f;
    gX_PhysicsMaterial spinGrip = spinLoose;
    spinGrip.rollingFriction = 0.05f;
    gX_PhysicsBody_SetMaterial(spinFast, spinLoose);
    gX_PhysicsBody_SetMaterial(spinSlow, spinGrip);
    gX_PhysicsBody_SetDamping(spinFast, 0.0f, 0.0f);
    gX_PhysicsBody_SetDamping(spinSlow, 0.0f, 0.0f);
    gX_PhysicsBody_SetAngularVel(spinFast, (gX_F32v3){0.0f, 20.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(spinSlow, (gX_F32v3){0.0f, 20.0f, 0.0f});
    for (gX_I32 i = 0; i < 240; i++) g_surfaceStep_(world);
    gX_F32 fastSpin = gX_PhysicsBody_GetAngularVel(spinFast).y;
    gX_F32 slowSpin = gX_PhysicsBody_GetAngularVel(spinSlow).y;
    if (!(slowSpin < fastSpin - 1.0f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: spin resistance fast=%.2f slow=%.2f",
            fastSpin, slowSpin);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, spinFast);
    gX_PhysicsBody_Destroy(world, spinSlow);

    //ai: conveyor surface — a belt moving +x carries a resting box until it
    //   matches the surface speed; flipping the sign carries it back.
    gX_PhysicsMaterial beltMat = gX_PhysicsMaterial_Default();
    beltMat.surfaceVelocity = 1.5f;
    gX_PhysicsCollider_SetMaterial(ground, beltMat);
    gX_PhysicsBody rider = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.0f, 0.5f, 0.0f});
    for (gX_I32 i = 0; i < 180; i++) g_surfaceStep_(world);
    gX_F32 riderVx = gX_PhysicsBody_GetLinearVel(rider).x;
    gX_F32 riderX = gX_PhysicsBody_GetPosition(rider).x;
    if (!(riderVx > 0.8f) || !(riderX > 0.5f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: belt carried box vx=%.2f x=%.2f",
            riderVx, riderX);
        errors++;
    }
    gX_PhysicsBody_SetPosition(rider, (gX_F32v3){0.0f, 0.5f, 0.0f});
    gX_PhysicsBody_SetLinearVel(rider, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetAngularVel(rider, (gX_F32v3){0.0f, 0.0f, 0.0f});
    beltMat.surfaceVelocity = -1.5f;
    gX_PhysicsCollider_SetMaterial(ground, beltMat);
    for (gX_I32 i = 0; i < 180; i++) g_surfaceStep_(world);
    riderVx = gX_PhysicsBody_GetLinearVel(rider).x;
    riderX = gX_PhysicsBody_GetPosition(rider).x;
    if (!(riderVx < -0.8f) || !(riderX < -0.5f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: belt reverse box vx=%.2f x=%.2f",
            riderVx, riderX);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, rider);
    gX_PhysicsCollider_Destroy(world, ground);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics surfaces — %d errors", errors);
    return errors;
}

// ========================================================================
// Sleeping / auto-disable — settle, wake on mutation, opt-out
// ========================================================================

//ai: settle a body until auto-disable engages (returns the steps used, or
//   -1 when it stays awake).
static gX_I32 g_sleepSettle_(gX_PhysicsWorld world, gX_PhysicsBody body) {
    for (gX_I32 i = 0; i < 300; i++) {
        g_surfaceStep_(world);
        if (!gX_PhysicsBody_IsEnabled(body)) return i + 1;
    }
    return -1;
}

gX_I32 gX_Tests_Physics_Sleeping(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);

    //ai: a settled box auto-disables (the world default is on).
    gX_PhysicsBody box = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){0.0f, 0.5f, 0.0f});
    if (g_sleepSettle_(world, box) < 0 || gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: settled body did not sleep");
        errors++;
    }

    //ai: explicit mutations wake it — velocity, teleport and force (ODE
    //   ignores them on a disabled body; the wrapper enables it).
    gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){3.0f, 0.0f, 0.0f});
    if (!gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: velocity mutation did not wake");
        errors++;
    }
    for (gX_I32 i = 0; i < 10; i++) g_surfaceStep_(world);
    if (!(gX_PhysicsBody_GetPosition(box).x > 0.1f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: woken body did not move");
        errors++;
    }
    if (g_sleepSettle_(world, box) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: body did not re-sleep");
        errors++;
    }
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 5.0f, 0.0f});
    if (!gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: teleport did not wake");
        errors++;
    }
    for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
    if (!(gX_PhysicsBody_GetPosition(box).y < 4.5f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: teleported body hung (y=%.2f)",
            gX_PhysicsBody_GetPosition(box).y);
        errors++;
    }
    if (g_sleepSettle_(world, box) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: body did not re-sleep after drop");
        errors++;
    }
    gX_PhysicsBody_ApplyForce(box, (gX_F32v3){50.0f, 0.0f, 0.0f});
    if (!gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: force mutation did not wake");
        errors++;
    }

    //ai: auto-disable opt-out keeps the body simulating after it settles.
    gX_PhysicsBody_SetAutoDisable(box, 0);
    if (gX_PhysicsBody_GetAutoDisable(box) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: auto-disable flag round trip");
        errors++;
    }
    if (g_sleepSettle_(world, box) >= 0 || !gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: slept with auto-disable off");
        errors++;
    }
    gX_PhysicsBody_SetAutoDisable(box, 1);
    gX_PhysicsBody_Disable(box);
    if (gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: explicit disable");
        errors++;
    }
    gX_PhysicsBody_Enable(box);
    if (!gX_PhysicsBody_IsEnabled(box)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: explicit enable");
        errors++;
    }

    //ai: an impact wakes a sleeper — a dropped box shoves it sideways.
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 0.5f, 0.0f});
    gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (g_sleepSettle_(world, box) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact sleeper did not settle");
        errors++;
    }
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 0.5f, 0.0f});
    if (g_sleepSettle_(world, box) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact sleeper re-settle");
        errors++;
    }
    //ai: contacts wake sleepers (sleep is a pure optimization) — a thrown
    //   box slams the settled one sideways.
    gX_PhysicsBody_SetPosition(box, (gX_F32v3){0.0f, 0.5f, 0.0f});
    gX_PhysicsBody_SetLinearVel(box, (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (g_sleepSettle_(world, box) < 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: impact sleeper did not settle");
        errors++;
    }
    gX_PhysicsBody upper = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 1.0f, 1.0f}, 1.0f, (gX_F32v3){-2.0f, 0.5f, 0.0f});
    gX_PhysicsBody_SetLinearVel(upper, (gX_F32v3){6.0f, 0.0f, 0.0f});
    gX_I1 woke = 0;
    gX_F32 maxMove = 0.0f;
    for (gX_I32 i = 0; i < 150; i++) {
        g_surfaceStep_(world);
        if (gX_PhysicsBody_IsEnabled(box)) woke = 1;
        gX_F32v3 pos = gX_PhysicsBody_GetPosition(box);
        gX_F32 move = sqrtf(pos.x * pos.x + pos.z * pos.z);
        if (move > maxMove) maxMove = move;
    }
    if (!woke || !(maxMove > 0.2f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: side impact woke=%d move=%.2f", woke, maxMove);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, upper);
    gX_PhysicsBody_Destroy(world, box);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics sleeping — %d errors", errors);
    return errors;
}

//ai: kinematic bodies — infinite mass: gravity and forces are ignored while
//   the wrapper-set velocity still integrates; a rider on a rising platform
//   is carried by contact, and switching the body back to dynamic restores
//   gravity.
gX_I32 gX_Tests_Physics_Kinematic(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);

    gX_PhysicsBody lift = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){2.2f, 0.3f, 2.2f}, 5.0f, (gX_F32v3){0.0f, 1.0f, 0.0f});
    gX_PhysicsBody_SetDamping(lift, 0.0f, 0.0f);
    gX_PhysicsBody_SetType(lift, gX_PHYSICS_BODY_KINEMATIC);
    if (gX_PhysicsBody_GetType(lift) != gX_PHYSICS_BODY_KINEMATIC) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: kinematic type round trip");
        errors++;
    }
    //ai: no velocity — gravity must not pull it down.
    for (gX_I32 i = 0; i < 120; i++) g_surfaceStep_(world);
    gX_F32 driftY = gX_PhysicsBody_GetPosition(lift).y - 1.0f;
    if (fabsf(driftY) > 0.02f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: kinematic drifted y=%.3f", driftY);
        errors++;
    }
    //ai: forces are ignored (infinite mass).
    gX_PhysicsBody_ApplyForce(lift, (gX_F32v3){500.0f, 0.0f, 0.0f});
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    if (fabsf(gX_PhysicsBody_GetPosition(lift).x) > 0.02f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: force moved a kinematic body");
        errors++;
    }
    //ai: the wrapper-set velocity integrates.
    gX_PhysicsBody_SetLinearVel(lift, (gX_F32v3){0.0f, 1.0f, 0.0f});
    gX_F32 preRiseY = gX_PhysicsBody_GetPosition(lift).y;
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    gX_F32 rise = gX_PhysicsBody_GetPosition(lift).y - preRiseY;
    if (!(rise > 0.9f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: kinematic velocity not integrated "
            "rise=%.3f", rise);
        errors++;
    }
    gX_PhysicsBody_SetLinearVel(lift, (gX_F32v3){0.0f, 0.0f, 0.0f});

    //ai: a dynamic rider is carried by a rising platform.
    gX_F32 liftY = gX_PhysicsBody_GetPosition(lift).y;
    gX_PhysicsBody rider = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.0f,
        (gX_F32v3){0.0f, liftY + 0.15f + 0.31f, 0.0f});
    gX_PhysicsBody_SetDamping(rider, 0.002f, 0.002f);
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    gX_F32 riderY0 = gX_PhysicsBody_GetPosition(rider).y;
    gX_F32 liftY0 = gX_PhysicsBody_GetPosition(lift).y;
    gX_PhysicsBody_SetLinearVel(lift, (gX_F32v3){0.0f, 1.2f, 0.0f});
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    gX_F32 riderDy = gX_PhysicsBody_GetPosition(rider).y - riderY0;
    gX_F32 liftDy = gX_PhysicsBody_GetPosition(lift).y - liftY0;
    if (!(liftDy > 0.9f) || !(riderDy > liftDy - 0.25f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: rider not carried rider=%.2f lift=%.2f", riderDy, liftDy);
        errors++;
    }
    //ai: back to dynamic — gravity takes over again.
    gX_PhysicsBody_SetLinearVel(lift, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsBody_SetType(lift, gX_PHYSICS_BODY_DYNAMIC);
    if (gX_PhysicsBody_GetType(lift) != gX_PHYSICS_BODY_DYNAMIC) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dynamic type round trip");
        errors++;
    }
    gX_F32 beforeY = gX_PhysicsBody_GetPosition(lift).y;
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    if (!(gX_PhysicsBody_GetPosition(lift).y < beforeY - 0.2f)) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: dynamic body did not fall");
        errors++;
    }
    gX_PhysicsBody_Destroy(world, rider);
    gX_PhysicsBody_Destroy(world, lift);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics kinematic — %d errors",
        errors);
    return errors;
}

//ai: mass properties — SetMass rescales mass and inertia together,
//   SetMassCenter moves the balance point (idempotent) and identical
//   planks tip or right themselves purely by where their mass sits.
gX_I32 gX_Tests_Physics_Mass(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);

    //ai: round trips — SetMass keeps the mass center, SetMassCenter is
    //   idempotent, moves the body origin onto the balance point and the
    //   inertia scales with the mass.
    gX_PhysicsBody box = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 1.8f, 0.6f}, 1.0f, (gX_F32v3){0.0f, 0.9f, 10.0f});
    gX_PhysicsBody_SetMassCenter(box, (gX_F32v3){0.0f, 0.6f, 0.0f});
    gX_PhysicsBody_SetMassCenter(box, (gX_F32v3){0.0f, 0.6f, 0.0f});
    gX_PhysicsBody_SetMass(box, 0.5f);
    gX_F32v3 com = gX_PhysicsBody_GetMassCenter(box);
    gX_F32v3 inertia = gX_PhysicsBody_GetInertia(box);
    if (fabsf(gX_PhysicsBody_GetMass(box) - 0.5f) > 1e-3f
        || fabsf(com.x) > 1e-4f || fabsf(com.y - 0.6f) > 1e-3f
        || fabsf(com.z) > 1e-4f
        || fabsf(gX_PhysicsBody_GetPosition(box).y - 1.5f) > 1e-3f) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: mass round trip m=%.3f com=%.3f y=%.3f",
            gX_PhysicsBody_GetMass(box), com.y,
            gX_PhysicsBody_GetPosition(box).y);
        errors++;
    }
    gX_PhysicsBody_SetMass(box, 1.0f);
    gX_F32v3 inertia2 = gX_PhysicsBody_GetInertia(box);
    gX_F32v3 com2 = gX_PhysicsBody_GetMassCenter(box);
    if (fabsf(inertia2.x - inertia.x * 2.0f) > 1e-3f
        || fabsf(inertia2.y - inertia.y * 2.0f) > 1e-3f
        || fabsf(inertia2.z - inertia.z * 2.0f) > 1e-3f
        || fabsf(com2.y - 0.6f) > 1e-3f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: inertia did not scale (%.3f -> %.3f)",
            inertia.x, inertia2.x);
        errors++;
    }
    gX_PhysicsBody_Destroy(world, box);

    //ai: same shape, same mass, different mass center — tilted by the same
    //   angle on an edge, the top-heavy plank falls over while the centered
    //   and bottom-heavy ones right themselves.
    gX_F32 halfW = 0.3f, halfH = 0.9f, tilt = 0.25f;
    gX_F32 restY = halfH * cosf(tilt) + halfW * sinf(tilt);
    gX_PhysicsBody posts[3];
    gX_F32 offsets[3] = {0.7f, 0.0f, -0.7f};
    for (gX_I32 i = 0; i < 3; i++) {
        posts[i] = gX_PhysicsBody_AddBox(world,
            (gX_F32v3){halfW * 2.0f, halfH * 2.0f, halfW * 2.0f}, 1.0f,
            (gX_F32v3){-2.6f + 2.6f * (gX_F32)i, restY + 0.002f, 0.0f});
        gX_PhysicsBody_SetMass(posts[i], 0.5f);
        gX_PhysicsBody_SetRotation(posts[i],
            gX_F32Qua_FromAxisAngle((gX_F32v3){0.0f, 0.0f, 1.0f}, tilt));
        //ai: last, so the shape keeps its resting pose.
        gX_PhysicsBody_SetMassCenter(posts[i],
            (gX_F32v3){0.0f, offsets[i], 0.0f});
        gX_PhysicsBody_SetDamping(posts[i], 0.002f, 0.002f);
        gX_PhysicsBody_SetMaterial(posts[i],
            (gX_PhysicsMaterial){0.9f, 0.0f, 0.0f, 0.0f});
    }
    for (gX_I32 i = 0; i < 300; i++) g_surfaceStep_(world);
    gX_F32 tilts[3];
    for (gX_I32 i = 0; i < 3; i++) {
        gX_F32Qua quat;
        gX_PhysicsBody_GetRotation(posts[i], &quat);
        gX_F32v3 up = gX_F32v3_RotateByQuaternion((gX_F32v3){0.0f, 1.0f,
            0.0f}, quat);
        if (up.y > 1.0f) up.y = 1.0f;
        if (up.y < -1.0f) up.y = -1.0f;
        tilts[i] = acosf(up.y);
        gX_PhysicsBody_Destroy(world, posts[i]);
    }
    if (!(tilts[0] > 1.0f) || !(tilts[1] < 0.15f) || !(tilts[2] < 0.15f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: mass center tipping top=%.2f mid=%.2f low=%.2f",
            tilts[0], tilts[1], tilts[2]);
        errors++;
    }
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics mass — %d errors", errors);
    return errors;
}

//ai: hinge2 — the suspension holds a hanging wheel at its anchor, the
//   group-1 motor steers (clamped by its stops), the group-2 motor spins
//   the wheel about its own axle, and a NULL partner binds to the world's
//   hidden kinematic anchor (ODE needs two real bodies for hinge2).
gX_I32 gX_Tests_Physics_Hinge2(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);

    //ai: a kinematic chassis (a test stand) carries the wheel; the wheel
    //   hangs below it and the pair skips self-collision (rig convention).
    gX_F32v3 anchor = {0.0f, 1.2f, 0.0f};
    gX_PhysicsBody chassis = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){1.0f, 0.3f, 0.6f}, 1.2f, (gX_F32v3){0.0f, 1.75f, 0.0f});
    gX_PhysicsBody_SetType(chassis, gX_PHYSICS_BODY_KINEMATIC);
    gX_PhysicsBody_SetCollisionFilter(chassis, 0x4u, 0xFFFFFFFBu);
    gX_PhysicsBody wheel = gX_PhysicsBody_AddCylinder(world, 0.3f, 0.24f, 3,
        0.9f, anchor);
    gX_PhysicsBody_SetCollisionFilter(wheel, 0x4u, 0xFFFFFFFBu);
    gX_PhysicsJoint joint = gX_PhysicsJoint_CreateHinge2(world, wheel, chassis,
        anchor, (gX_F32v3){0.0f, 1.0f, 0.0f}, (gX_F32v3){0.0f, 0.0f, 1.0f});
    if (joint.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge2 creation");
        errors++;
    } else {
        gX_PhysicsJoint_SetSuspension(joint, 0.3f, 0.05f);
        gX_PhysicsJoint_SetMotor(joint, 0.0f, 8.0f);
        gX_PhysicsJoint_SetLimits(joint, -0.2f, 0.2f);
        for (gX_I32 i = 0; i < 120; i++) g_surfaceStep_(world);
        //ai: the suspension keeps the wheel centered on the anchor.
        gX_F32v3 wpos = gX_PhysicsBody_GetPosition(wheel);
        gX_F32v3 gap = {wpos.x - anchor.x, wpos.y - anchor.y,
            wpos.z - anchor.z};
        if (sqrtf(gap.x * gap.x + gap.y * gap.y + gap.z * gap.z) > 0.05f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: hinge2 suspension slipped (%.3f, %.3f, %.3f)",
                gap.x, gap.y, gap.z);
            errors++;
        }
        //ai: the group-1 motor steers about axis1 and the stops clamp it
        //   (the angle sign follows the axis convention).
        gX_PhysicsJoint_SetMotor(joint, 2.0f, 8.0f);
        for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
        gX_F32 steerAngle = gX_PhysicsJoint_GetPosition(joint);
        if (fabsf(fabsf(steerAngle) - 0.2f) > 0.02f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge2 steering stop (%.3f)",
                steerAngle);
            errors++;
        }
        //ai: the group-2 motor spins the wheel about its own axle.
        gX_PhysicsJoint_SetMotor(joint, 0.0f, 0.0f);
        gX_PhysicsJoint_SetMotor2(joint, 4.0f, 8.0f);
        for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
        gX_F32 spin = gX_PhysicsJoint_GetPosition2Rate(joint);
        if (fabsf(spin - 4.0f) > 0.4f) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge2 spin rate (%.2f)", spin);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, joint);
    }

    //ai: a NULL partner binds to the world's kinematic anchor.
    gX_PhysicsBody floatWheel = gX_PhysicsBody_AddCylinder(world, 0.3f, 0.24f,
        3, 0.9f, (gX_F32v3){2.0f, 1.2f, 0.0f});
    gX_PhysicsJoint freeJoint = gX_PhysicsJoint_CreateHinge2(world,
        floatWheel, gX_PHYSICS_BODY_NULL, (gX_F32v3){2.0f, 1.2f, 0.0f},
        (gX_F32v3){0.0f, 1.0f, 0.0f}, (gX_F32v3){0.0f, 0.0f, 1.0f});
    if (freeJoint.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: hinge2 anchor-body creation");
        errors++;
    } else {
        gX_PhysicsJoint_SetSuspension(freeJoint, 0.3f, 0.05f);
        gX_PhysicsJoint_SetMotor(freeJoint, 0.0f, 8.0f);
        for (gX_I32 i = 0; i < 120; i++) g_surfaceStep_(world);
        gX_F32v3 wpos = gX_PhysicsBody_GetPosition(floatWheel);
        if (fabsf(wpos.x - 2.0f) > 0.05f || fabsf(wpos.y - 1.2f) > 0.05f
            || fabsf(wpos.z) > 0.05f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: hinge2 anchor hold (%.3f, %.3f, %.3f)", wpos.x,
                wpos.y, wpos.z);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, freeJoint);
    }

    gX_PhysicsBody_Destroy(world, wheel);
    gX_PhysicsBody_Destroy(world, floatWheel);
    gX_PhysicsBody_Destroy(world, chassis);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics hinge2 — %d errors", errors);
    return errors;
}

//ai: joint feedback — a welded mass reports its weight through the joint
//   constraint, a strong pull spikes the reading, disabling removes it, and
//   the world's feedback registry survives joint create/destroy cycles.
gX_I32 gX_Tests_Physics_Feedback(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);

    gX_PhysicsBody crate = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 2.0f, (gX_F32v3){0.0f, 2.0f, 0.0f});
    gX_PhysicsJoint weld = gX_PhysicsJoint_CreateFixed(world, crate,
        gX_PHYSICS_BODY_NULL);
    gX_F32v3 force = {0.0f, 0.0f, 0.0f};
    gX_F32v3 torque = {0.0f, 0.0f, 0.0f};
    if (weld.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: feedback weld creation");
        errors++;
    } else {
        if (gX_PhysicsJoint_GetFeedback(weld, &force, &torque)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: feedback reported before it was enabled");
            errors++;
        }
        gX_PhysicsJoint_EnableFeedback(weld, 1);
        for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
        gX_F32 weight = gX_PhysicsBody_GetMass(crate) * 9.81f;
        if (!gX_PhysicsJoint_GetFeedback(weld, &force, &torque)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: feedback missing after enable");
            errors++;
        } else {
            gX_F32 mag = sqrtf(force.x * force.x + force.y * force.y
                + force.z * force.z);
            if (fabsf(mag - weight) > weight * 0.15f || !(force.y > 0.0f)) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: feedback weight (|F|=%.3f N, weight=%.3f, y=%.3f)",
                    mag, weight, force.y);
                errors++;
            }
        }
        //ai: a strong pull spikes the constraint force past the static load.
        for (gX_I32 i = 0; i < 10; i++) {
            gX_PhysicsBody_ApplyForce(crate, (gX_F32v3){0.0f, -60.0f, 0.0f});
            g_surfaceStep_(world);
        }
        if (!gX_PhysicsJoint_GetFeedback(weld, NULL, NULL)
            || !gX_PhysicsJoint_GetFeedback(weld, &force, NULL)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: feedback read during pull");
            errors++;
        } else {
            gX_F32 pullMag = sqrtf(force.x * force.x + force.y * force.y
                + force.z * force.z);
            if (!(pullMag > 10.0f)) {
                gX_TraceLog(gX_LOG_ERROR,
                    "FAIL: feedback did not spike (%.3f N)", pullMag);
                errors++;
            }
        }
        gX_PhysicsJoint_EnableFeedback(weld, 0);
        if (gX_PhysicsJoint_GetFeedback(weld, NULL, NULL)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: feedback after disable");
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, weld);
    }

    //ai: a second enable/destroy cycle must not trip the registry (the
    //   destroy path prunes the entry).
    gX_PhysicsBody second = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.4f, 0.4f, 0.4f}, 2.0f, (gX_F32v3){2.0f, 2.0f, 0.0f});
    gX_PhysicsJoint weld2 = gX_PhysicsJoint_CreateFixed(world, second,
        gX_PHYSICS_BODY_NULL);
    if (weld2.internal != 0) {
        gX_PhysicsJoint_EnableFeedback(weld2, 1);
        for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
        if (!gX_PhysicsJoint_GetFeedback(weld2, &force, NULL)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: second feedback read");
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, weld2);
    } else {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: second weld creation");
        errors++;
    }

    gX_PhysicsBody_Destroy(world, crate);
    gX_PhysicsBody_Destroy(world, second);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics feedback — %d errors",
        errors);
    return errors;
}

//ai: articulated self-contact — bodies connected by a joint never generate
//   contacts (the overlapping segments at a ragdoll's joints must not fight
//   the solver), while unconnected bodies still collide.
gX_I32 gX_Tests_Physics_SelfContact(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);

    //ai: jointed pair spawned overlapping (0.3 apart on 0.6 boxes): the
    //   solver must leave the overlap alone.
    gX_PhysicsBody a = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.0f, (gX_F32v3){0.0f, 4.0f, 0.0f});
    gX_PhysicsBody b = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.0f, (gX_F32v3){0.3f, 4.0f, 0.0f});
    gX_PhysicsJoint link = gX_PhysicsJoint_CreateBall(world, a, b,
        (gX_F32v3){0.15f, 4.0f, 0.0f});
    if (link.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: self-contact joint creation");
        errors++;
    } else {
        for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
        gX_F32v3 pa = gX_PhysicsBody_GetPosition(a);
        gX_F32v3 pb = gX_PhysicsBody_GetPosition(b);
        gX_F32 dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        gX_F32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!(dist < 0.45f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: jointed overlap fought the solver (d=%.3f)", dist);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, link);
    }

    //ai: the same overlap without a joint must separate (control case).
    gX_PhysicsBody c = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.0f, (gX_F32v3){0.0f, 4.0f, 3.0f});
    gX_PhysicsBody d = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.6f, 0.6f}, 1.0f, (gX_F32v3){0.3f, 4.0f, 3.0f});
    for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
    gX_F32 freeDist = fabsf(gX_PhysicsBody_GetPosition(d).x
        - gX_PhysicsBody_GetPosition(c).x);
    if (!(freeDist > 0.5f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: unjointed overlap did not separate (d=%.3f)", freeDist);
        errors++;
    }

    gX_PhysicsBody_Destroy(world, a);
    gX_PhysicsBody_Destroy(world, b);
    gX_PhysicsBody_Destroy(world, c);
    gX_PhysicsBody_Destroy(world, d);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics self-contact — %d errors",
        errors);
    return errors;
}

//ai: AMotor — Euler-mode swing limits hold a ball-jointed limb inside its
//   cone (and it still swings), while a user-mode axis motor spins a free
//   body at the target rate.
gX_I32 gX_Tests_Physics_AMotor(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);
    gX_PhysicsWorld_AddGroundPlane(world, (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.0f);

    //ai: a kinematic stand holds a limb on a ball joint; the AMotor adds a
    //   cone limit around the rest direction.
    gX_PhysicsBody stand = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.6f, 0.3f, 0.6f}, 1.0f, (gX_F32v3){0.0f, 2.4f, 0.0f});
    gX_PhysicsBody_SetType(stand, gX_PHYSICS_BODY_KINEMATIC);
    gX_PhysicsBody_SetCollisionFilter(stand, 0x4u, 0xFFFFFFFBu);
    gX_PhysicsBody limb = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.3f, 1.0f, 0.3f}, 1.0f, (gX_F32v3){0.0f, 1.6f, 0.0f});
    gX_PhysicsBody_SetCollisionFilter(limb, 0x4u, 0xFFFFFFFBu);
    gX_PhysicsJoint ball = gX_PhysicsJoint_CreateBall(world, limb, stand,
        (gX_F32v3){0.0f, 2.05f, 0.0f});
    gX_PhysicsJoint cone = gX_PhysicsJoint_CreateAMotor(world, limb, stand);
    if (ball.internal == 0 || cone.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: amotor creation");
        errors++;
    } else {
        //ai: ODE requires axes 0 and 2 perpendicular (an assert otherwise);
        //   the middle angle (the swing) is measured from that rest
        //   configuration, so the cone centers around it.
        gX_PhysicsJoint_AMotorSetEuler(cone, (gX_F32v3){0.0f, 1.0f, 0.0f},
            (gX_F32v3){1.0f, 0.0f, 0.0f});
        gX_PhysicsJoint_AMotorSetLimits(cone, 0, -3.0f, 3.0f);
        gX_PhysicsJoint_AMotorSetLimits(cone, 1, 1.17f, 1.97f);
        gX_PhysicsJoint_AMotorSetLimits(cone, 2, -3.0f, 3.0f);
        for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
        for (gX_I32 i = 0; i < 120; i++) {
            gX_PhysicsBody_ApplyTorque(limb, (gX_F32v3){0.0f, 0.0f, 6.0f});
            g_surfaceStep_(world);
        }
        gX_F32 swing = gX_PhysicsJoint_AMotorGetAngle(cone, 1);
        if (!(swing >= 1.17f - 0.15f && swing <= 1.97f + 0.15f)) {
            gX_TraceLog(gX_LOG_ERROR, "FAIL: amotor cone let the limb out "
                "(%.3f)", swing);
            errors++;
        }
        if (!(fabsf(swing - 1.57f) > 0.15f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: amotor cone locked the limb (%.3f)", swing);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, cone);
        gX_PhysicsJoint_Destroy(world, ball);
    }

    //ai: user mode — the axis motor spins a free body at the target rate.
    gX_PhysicsBody rotor = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.4f, 0.4f, 0.4f}, 2.0f, (gX_F32v3){3.0f, 3.0f, 0.0f});
    gX_PhysicsJoint spin = gX_PhysicsJoint_CreateAMotor(world, rotor, stand);
    if (spin.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: amotor spin creation");
        errors++;
    } else {
        gX_F32v3 axis = {0.0f, 1.0f, 0.0f};
        gX_PhysicsJoint_AMotorSetAxes(spin, 1, &axis);
        gX_PhysicsJoint_AMotorSetMotor(spin, 0, 2.0f, 5.0f);
        //ai: measure while the rotor is still airborne — the AMotor only
        //   constrains rotation, gravity still drops it.
        for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
        gX_F32 w = gX_PhysicsBody_GetAngularVel(rotor).y;
        if (fabsf(w - 2.0f) > 0.3f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: amotor axis motor rate (%.2f)", w);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, spin);
    }

    gX_PhysicsBody_Destroy(world, limb);
    gX_PhysicsBody_Destroy(world, rotor);
    gX_PhysicsBody_Destroy(world, stand);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics amotor — %d errors", errors);
    return errors;
}

//ai: piston — the slide motor drives along the axis into its limits, the
//   rotation motor spins the body about the same axis, and the queries read
//   both degrees of freedom (a NULL partner binds to the anchor body).
gX_I32 gX_Tests_Physics_Piston(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);

    gX_F32v3 anchor = {0.0f, 2.0f, 0.0f};
    gX_PhysicsBody plug = gX_PhysicsBody_AddBox(world,
        (gX_F32v3){0.3f, 0.3f, 0.3f}, 1.0f, anchor);
    gX_PhysicsJoint joint = gX_PhysicsJoint_CreatePiston(world, plug,
        gX_PHYSICS_BODY_NULL, anchor, (gX_F32v3){1.0f, 0.0f, 0.0f});
    if (joint.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: piston creation");
        errors++;
    } else {
        gX_PhysicsJoint_SetLimits(joint, -0.4f, 0.4f);
        gX_PhysicsJoint_SetMotor(joint, 1.0f, 20.0f);
        for (gX_I32 i = 0; i < 60; i++) g_surfaceStep_(world);
        gX_F32 slide = gX_PhysicsJoint_GetPosition(joint);
        if (!(slide > 0.35f && slide < 0.45f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: piston slide did not reach the limit (%.3f)", slide);
            errors++;
        }
        //ai: gravity must not move the plug off the axis line.
        gX_F32 plugY = gX_PhysicsBody_GetPosition(plug).y;
        if (fabsf(plugY - anchor.y) > 0.02f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: piston let the plug off-axis (y=%.3f)", plugY);
            errors++;
        }
        gX_PhysicsJoint_SetMotor(joint, -1.0f, 20.0f);
        for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
        gX_F32 back = gX_PhysicsJoint_GetPosition(joint);
        if (!(back < 0.2f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: piston slide did not reverse (%.3f)", back);
            errors++;
        }
        //ai: the rotation motor spins the plug about the slide axis.
        gX_PhysicsJoint_SetMotor2(joint, 2.0f, 5.0f);
        for (gX_I32 i = 0; i < 30; i++) g_surfaceStep_(world);
        gX_F32 spin = gX_PhysicsJoint_GetPosition2Rate(joint);
        gX_F32 bodySpin = gX_PhysicsBody_GetAngularVel(plug).x;
        if (fabsf(spin - 2.0f) > 0.3f || fabsf(bodySpin - 2.0f) > 0.3f) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: piston rotation rate (%.2f / %.2f)", spin, bodySpin);
            errors++;
        }
        gX_PhysicsJoint_Destroy(world, joint);
    }

    gX_PhysicsBody_Destroy(world, plug);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics piston — %d errors", errors);
    return errors;
}

//ai: heightfield — a mound (a single central peak with flat borders on a
//   wrapped tile) rolls a ball dropped off-centre further downhill; the ball
//   stays on the surface instead of falling through.
gX_I32 gX_Tests_Physics_Heightfield(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, -9.81f, 0.0f});
    gX_PhysicsWorld_SetMaxContacts(world, 8);

    //ai: a 12x12 m tile, 13x13 samples, with a smooth bump at the centre.
    gX_F32 heights[13 * 13];
    for (gX_I32 i = 0; i < 13; i++) {
        for (gX_I32 j = 0; j < 13; j++) {
            gX_F32 dx = ((gX_F32)i - 6.0f) / 6.0f;
            gX_F32 dz = ((gX_F32)j - 6.0f) / 6.0f;
            gX_F32 r2 = dx * dx + dz * dz;
            heights[i * 13 + j] = 0.7f * (1.0f - r2);
            if (heights[i * 13 + j] < 0.0f) heights[i * 13 + j] = 0.0f;
        }
    }
    gX_PhysicsCollider mound = gX_PhysicsWorld_AddHeightfield(world, heights,
        13, 13, 12.0f, 12.0f, 1.0f, (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (mound.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: heightfield creation");
        errors++;
    } else {
        //ai: drop a ball partway up the slope (x off-centre) and let it roll.
        gX_PhysicsBody ball = gX_PhysicsBody_AddSphere(world, 0.22f, 1.0f,
            (gX_F32v3){0.8f, 0.85f, 0.0f});
        gX_F32 startX = 0.8f;
        for (gX_I32 i = 0; i < 240; i++) g_surfaceStep_(world);
        gX_F32v3 end = gX_PhysicsBody_GetPosition(ball);
        //ai: downhill from the peak is any direction away from the centre —
        //   the ball must have grown its distance from the crest and stay
        //   above the base plane.
        if (!(fabsf(end.x) > fabsf(startX) - 0.05f && end.y > 0.1f
                && end.y < 1.2f)) {
            gX_TraceLog(gX_LOG_ERROR,
                "FAIL: ball did not roll off the mound (%.2f, %.2f, %.2f)",
                end.x, end.y, end.z);
            errors++;
        }
        gX_PhysicsBody_Destroy(world, ball);
    }

    //ai: the collider's data is released with the collider (no leak).
    gX_PhysicsCollider_Destroy(world, mound);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics heightfield — %d errors",
        errors);
    return errors;
}

//ai: relativity applier — the speed clamp below the tuned light speed, the
//   gamma mass growth (the applier's mass-dilation trick) and the
//   registry/disable paths. The world runs at zero gravity so the checks
//   isolate the applier.
gX_I32 gX_Tests_Physics_Relativity(gX_I0) {
    gX_I32 errors = 0;
    gX_PhysicsWorld world = gX_PhysicsWorld_Create();
    if (world.internal == 0) return 0;
    gX_PhysicsWorld_SetGravity(world, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsRelativity_Reset();
    gX_PhysicsRelativity_Settings settings = gX_PhysicsRelativity_Defaults();
    settings.lightSpeed = 10.0f;
    gX_PhysicsRelativity_SetSettings(settings);

    gX_PhysicsBody body = gX_PhysicsBody_AddSphere(world, 0.4f, 2.0f,
        (gX_F32v3){0.0f, 0.0f, 0.0f});
    if (body.internal == 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity body build");
        gX_PhysicsWorld_Destroy(world);
        return errors + 1;
    }
    if (gX_PhysicsRelativity_AddBody(body, 2.0f) < 0
        || gX_PhysicsRelativity_Count() != 1) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity registry add");
        errors++;
    }

    //ai: disabled — a superluminal velocity passes through untouched.
    gX_PhysicsBody_SetLinearVel(body, (gX_F32v3){25.0f, 0.0f, 0.0f});
    gX_PhysicsRelativity_Apply();
    if (fabsf(gX_PhysicsBody_GetLinearVel(body).x - 25.0f) > 0.01f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: disabled applier clamped a velocity");
        errors++;
    }

    //ai: enabled — the clamp caps the speed below c along its direction, and
    //   the beta near 1 drives the gamma mass up.
    gX_PhysicsRelativity_SetEnabled(1);
    gX_PhysicsBody_SetLinearVel(body, (gX_F32v3){25.0f, 0.0f, 0.0f});
    gX_PhysicsRelativity_Apply();
    gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(body);
    gX_F32 speed = gX_F32v3_Length(velocity);
    if (!(speed <= 10.0f) || !(speed > 9.5f) || !(velocity.x > 0.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: relativity clamp speed=%.3f dir=(%.2f,%.2f,%.2f)", speed,
            velocity.x, velocity.y, velocity.z);
        errors++;
    }
    gX_F32 beta = 0.0f;
    gX_F32 gamma = 0.0f;
    gX_F32 mass = 0.0f;
    if (!gX_PhysicsRelativity_GetBodyState(body, &beta, &gamma, &mass)
        || !(gamma > 20.0f) || !(mass > 40.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: relativity gamma mass beta=%.3f gamma=%.2f mass=%.2f",
            beta, gamma, mass);
        errors++;
    }
    if (fabsf(gX_PhysicsBody_GetMass(body) - mass) > 0.5f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity solver mass %.2f vs %.2f",
            gX_PhysicsBody_GetMass(body), mass);
        errors++;
    }

    //ai: half c — gamma ~1.155 and the mass moved off the rest mass.
    gX_PhysicsBody_SetLinearVel(body, (gX_F32v3){5.0f, 0.0f, 0.0f});
    gX_PhysicsRelativity_Apply();
    gX_PhysicsRelativity_GetBodyState(body, &beta, &gamma, &mass);
    if (!(beta > 0.49f) || !(beta < 0.51f) || !(gamma > 1.14f)
        || !(gamma < 1.17f) || !(mass > 2.2f) || !(mass < 2.4f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: relativity half-c state beta=%.3f gamma=%.3f mass=%.3f",
            beta, gamma, mass);
        errors++;
    }

    //ai: at rest the throttle brings the mass back to the rest mass.
    gX_PhysicsBody_SetLinearVel(body, (gX_F32v3){0.0f, 0.0f, 0.0f});
    gX_PhysicsRelativity_Apply();
    if (fabsf(gX_PhysicsBody_GetMass(body) - 2.0f) > 0.05f) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity rest mass %.3f",
            gX_PhysicsBody_GetMass(body));
        errors++;
    }

    //ai: driven by a constant force, the speed saturates below c and the
    //   mass grows until the force can barely accelerate it.
    gX_I32 overspeed = 0;
    for (gX_I32 i = 0; i < 180; i++) {
        gX_PhysicsBody_ApplyForce(body, (gX_F32v3){400.0f, 0.0f, 0.0f});
        gX_PhysicsWorld_Step(world, 1.0f / 60.0f);
        gX_PhysicsWorld_EmptyContactGroup(world);
        gX_PhysicsRelativity_Apply();
        if (gX_F32v3_Length(gX_PhysicsBody_GetLinearVel(body)) > 10.0001f) {
            overspeed++;
        }
    }
    if (overspeed != 0) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: relativity let %d steps exceed c", overspeed);
        errors++;
    }
    gX_PhysicsRelativity_GetBodyState(body, &beta, &gamma, &mass);
    gX_F32 terminal = gX_F32v3_Length(gX_PhysicsBody_GetLinearVel(body));
    if (!(mass > 20.0f) || !(terminal > 9.0f)) {
        gX_TraceLog(gX_LOG_ERROR,
            "FAIL: relativity driven state speed=%.3f mass=%.2f", terminal,
            mass);
        errors++;
    }

    gX_PhysicsRelativity_RemoveBody(body);
    if (gX_PhysicsRelativity_Count() != 0) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity registry remove");
        errors++;
    }
    gX_PhysicsRelativity_Reset();
    if (gX_PhysicsRelativity_Enabled()) {
        gX_TraceLog(gX_LOG_ERROR, "FAIL: relativity reset kept enabled");
        errors++;
    }

    gX_PhysicsBody_Destroy(world, body);
    gX_PhysicsWorld_Destroy(world);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: physics relativity — %d errors",
        errors);
    return errors;
}
