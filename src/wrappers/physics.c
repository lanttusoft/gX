/**********************************************************************************************
*
*   [gX: src/wrappers/physics.c]
*   Physics wrapper — ODE-backed
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

//opencode: backend-agnostic physics wrapper — delegates to ODE when available,
//   provides no-op stubs when GX_USE_ODE is off.
#include "wrappers_header.h"
#include "physics.h"
#include "../common/log.h"

#if defined(GX_USE_ODE)

//opencode: undef I before ODE to avoid conflict with complex.h (I=_Complex_I)
#undef I
#include <ode/ode.h>
#include <math.h>
#include <string.h>

//opencode: public default contact material for ODE callers.
gX_PhysicsMaterial gX_PhysicsMaterial_Default(gX_I0) {
    return (gX_PhysicsMaterial){0.8f, 0.1f, 0.0f, 0.0f};
}
#include <stdlib.h>

// ========================================================================
// Internal world and geometry state
// ========================================================================

typedef struct gX_PhysicsGeomData_ {
    gX_PhysicsMaterial material;
    gX_U32 categoryBits;
    gX_U32 maskBits;
    gX_U64 userTag;
    gX_I1 sensor;
    gX_I32 dataClass;
    gX_I0 *shapeData;
    //ai: shape-space mass (COM at the shape center) — kept so the mass
    //   center can be re-derived exactly when it is moved.
    dMass baseMass;
} gX_PhysicsGeomData_;

//opencode: a colliding pair tracked across steps for enter/stay/exit events.
typedef struct gX_PhysicsPair_ {
    dGeomID gA;   // canonicalized so gA < gB
    dGeomID gB;
} gX_PhysicsPair_;

typedef struct gX_PhysicsWorldData_ {
    dWorldID world;
    dSpaceID space;
    gX_PhysicsCollisionCB cb;
    gX_I0 *userData;
    gX_PhysicsEventCB eventCB;
    gX_I0 *eventUserData;
    gX_PhysicsPair_ *pairsPrev;   // pairs colliding last step
    gX_PhysicsPair_ *pairsNow;    // pairs colliding this step
    gX_U32 pairCountPrev;
    gX_U32 pairCountNow;
    gX_U32 pairCapacity;
    //ai: joint feedback registry — ODE writes into caller-owned structures,
    //   so the world owns one per enabled joint (freed at world destroy,
    //   pruned when a joint is destroyed).
    dJointFeedback *fb;
    dJointID *fbJoints;
    gX_U32 fbCount;
    gX_U32 fbCapacity;
    dJointGroupID jointGroup;
    gX_I32 maxContacts;
    dThreadingImplementationID threading;
    dThreadingThreadPoolID pool;
    gX_I1 useThreading;
    //ai: hidden kinematic body standing in for the static environment on
    //   joints that need two real bodies (ODE 0.16 hinge2).
    dBodyID anchorBody;
} gX_PhysicsWorldData_;

static gX_I32 g_odeRefCount = 0;

static dWorldID unwrapWorld(gX_PhysicsWorld w) {
    return (dWorldID)(uintptr_t)w.internal;
}
static dBodyID unwrapBody(gX_PhysicsBody b) {
    return (dBodyID)(uintptr_t)b.internal;
}
static gX_PhysicsWorld wrapWorld(dWorldID w) {
    gX_PhysicsWorld r; r.internal = (gX_U64)(uintptr_t)w; return r;
}
static gX_PhysicsBody wrapBody(dBodyID b) {
    gX_PhysicsBody r; r.internal = (gX_U64)(uintptr_t)b; return r;
}
static dJointID unwrapJoint(gX_PhysicsJoint j) {
    return (dJointID)(uintptr_t)j.internal;
}
static gX_PhysicsJoint wrapJoint(dJointID j) {
    gX_PhysicsJoint r; r.internal = (gX_U64)(uintptr_t)j; return r;
}
static gX_PhysicsCollider wrapCollider(dGeomID g) {
    gX_PhysicsCollider r; r.internal = (gX_U64)(uintptr_t)g; return r;
}
static dGeomID unwrapCollider(gX_PhysicsCollider c) {
    return (dGeomID)(uintptr_t)c.internal;
}
static gX_PhysicsWorldData_ *getWorldData(dWorldID w) {
    return w ? (gX_PhysicsWorldData_ *)dWorldGetData(w) : NULL;
}
static dSpaceID getWorldSpace(dWorldID w) {
    gX_PhysicsWorldData_ *data = getWorldData(w);
    return data ? data->space : NULL;
}

//ai: a joint's world via its first attached body (ODE has no
//   dJointGetWorld); a limbo joint returns NULL and feedback is a no-op.
static dWorldID jointWorld_(dJointID j) {
    if (!j) return NULL;
    for (gX_I32 i = 0; i < 2; i++) {
        dBodyID b = dJointGetBody(j, i);
        if (b) return dBodyGetWorld(b);
    }
    return NULL;
}
static gX_PhysicsGeomData_ *getGeomData(dGeomID geom) {
    return geom ? (gX_PhysicsGeomData_ *)dGeomGetData(geom) : NULL;
}
static gX_PhysicsMaterial defaultMaterial_(gX_I0) {
    return (gX_PhysicsMaterial){0.8f, 0.1f, 0.0f, 0.0f};
}
static gX_PhysicsGeomData_ *initGeomData_(dGeomID geom) {
    gX_PhysicsGeomData_ *data = (gX_PhysicsGeomData_ *)calloc(1, sizeof(*data));
    if (!data) return NULL;
    data->material = defaultMaterial_();
    data->categoryBits = 1u;
    data->maskBits = 0xffffffffu;
    dGeomSetData(geom, data);
    return data;
}
static void destroyGeomData_(dGeomID geom) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (!data) return;
    if (data->dataClass == dTriMeshClass && data->shapeData) {
        dGeomTriMeshDataDestroy((dTriMeshDataID)data->shapeData);
    } else if (data->dataClass == dHeightfieldClass && data->shapeData) {
        //ai: heightfield data is caller-visible to ODE only through its
        //   owner — the wrapper owns it through the geom data.
        dGeomHeightfieldDataDestroy((dHeightfieldDataID)data->shapeData);
    } else if (data->shapeData) {
        free(data->shapeData);
    }
    dGeomSetData(geom, NULL);
    free(data);
}
static void applyGeomDefaults_(dGeomID geom) {
    gX_PhysicsGeomData_ *data = initGeomData_(geom);
    if (!data) return;
    dGeomSetCategoryBits(geom, data->categoryBits);
    dGeomSetCollideBits(geom, data->maskBits);
}

//ai: remember the shape-space mass so SetMassCenter can rebuild the mass
//   about a moved center from a known base.
static void rememberShapeMass_(dGeomID geom, const dMass *dm) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (data && dm) data->baseMass = *dm;
}

//opencode: canonicalize a pair (sort by geom pointer) for stable tracking.
static void pairCanonical_(dGeomID o1, dGeomID o2, dGeomID *outA, dGeomID *outB) {
    if (o1 < o2) { *outA = o1; *outB = o2; }
    else         { *outA = o2; *outB = o1; }
}

static gX_PhysicsPair_ *pairFind_(gX_PhysicsPair_ *pairs, gX_U32 count,
    dGeomID a, dGeomID b) {
    for (gX_U32 i = 0; i < count; i++) {
        if (pairs[i].gA == a && pairs[i].gB == b) return &pairs[i];
    }
    return NULL;
}

//opencode: remove a geom from the tracked pair sets (used on destroy so a
//   destroyed body never produces a dangling EXIT referencing freed memory).
static gX_I0 pairPurgeGeom_(gX_PhysicsPair_ *pairs, gX_U32 *count, dGeomID geom) {
    gX_U32 write = 0;
    for (gX_U32 i = 0; i < *count; i++) {
        if (pairs[i].gA == geom || pairs[i].gB == geom) continue;
        if (write != i) pairs[write] = pairs[i];
        write++;
    }
    *count = write;
}

static gX_PhysicsPair_ *pairGrow_(gX_PhysicsPair_ **pairs, gX_U32 *capacity,
    gX_U32 needed) {
    if (needed <= *capacity) return *pairs;
    gX_U32 newCap = *capacity ? *capacity * 2 : 16;
    if (newCap < needed) newCap = needed;
    gX_PhysicsPair_ *grown = (gX_PhysicsPair_ *)realloc(*pairs,
        sizeof(gX_PhysicsPair_) * newCap);
    if (!grown) return NULL;
    *pairs = grown;
    *capacity = newCap;
    return *pairs;
}

//ai: articulated self-contact filtering — two bodies connected by a
//   constraint joint never collide, so the overlapping segments at a joint
//   do not fight the solver while non-adjacent parts still collide (ragdoll
//   self-collision). Contact joints from earlier pairs in the same pass are
//   ignored.
static gX_I1 bodiesJointed_(dBodyID b1, dBodyID b2) {
    if (!b1 || !b2) return false;
    gX_I32 count = dBodyGetNumJoints(b1);
    for (gX_I32 i = 0; i < count; i++) {
        dJointID j = dBodyGetJoint(b1, i);
        if (!j || dJointGetType(j) == dJointTypeContact) continue;
        if (dJointGetBody(j, 0) == b2 || dJointGetBody(j, 1) == b2) return true;
    }
    return false;
}

//opencode: ODE near callback uses per-world contact state and material data.
static void nearCallback(gX_I0 *data, dGeomID o1, dGeomID o2) {
    gX_PhysicsWorldData_ *state = (gX_PhysicsWorldData_ *)data;
    if (!state) return;
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);
    if (b1 == b2) return;
    if (bodiesJointed_(b1, b2)) return;

    //ai: an enabled body touching a sleeping one wakes it, so sleeping is a
    //   pure optimization — resting piles settle and sleep together, while a
    //   fresh impact reacts against them instead of an immovable obstacle.
    if (b1 && b2) {
        if (dBodyIsEnabled(b1) && !dBodyIsEnabled(b2)) dBodyEnable(b2);
        else if (dBodyIsEnabled(b2) && !dBodyIsEnabled(b1)) dBodyEnable(b1);
    }

    gX_PhysicsBody pb1 = wrapBody(b1);
    gX_PhysicsBody pb2 = wrapBody(b2);

    gX_I32 limit = state->maxContacts;
    if (limit < 1) limit = 1;
    if (limit > 8) limit = 8;
    #define MAXC 8
    dContact contacts[MAXC];
    gX_I32 numc = dCollide(o1, o2, limit, &contacts[0].geom, sizeof(dContact));

    gX_PhysicsContact gxc[MAXC];
    for (gX_I32 i = 0; i < numc && i < limit; i++) {
        gxc[i].position = (gX_F32v3){
            (gX_F32)contacts[i].geom.pos[0],
            (gX_F32)contacts[i].geom.pos[1],
            (gX_F32)contacts[i].geom.pos[2]
        };
        gxc[i].normal = (gX_F32v3){
            (gX_F32)contacts[i].geom.normal[0],
            (gX_F32)contacts[i].geom.normal[1],
            (gX_F32)contacts[i].geom.normal[2]
        };
        gxc[i].depth = (gX_F32)contacts[i].geom.depth;
    }

    gX_I32 handled = 0;
    if (state->cb) {
        handled = state->cb(state->userData, pb1, pb2, (gX_U32)numc, gxc);
    }

    //opencode: sensor geometry reports through the callback but never creates contacts.
    gX_PhysicsGeomData_ *data1 = getGeomData(o1);
    gX_PhysicsGeomData_ *data2 = getGeomData(o2);

    //opencode: contact events — fire on real contacts (numc > 0), including
    //   sensor/trigger pairs. ENTER when the pair was not colliding last step,
    //   STAY otherwise. EXIT is swept in Step after dSpaceCollide.
    if (state->eventCB && numc > 0) {
        dGeomID cA, cB;
        pairCanonical_(o1, o2, &cA, &cB);
        if (!pairFind_(state->pairsNow, state->pairCountNow, cA, cB)) {
            if (pairGrow_(&state->pairsNow, &state->pairCapacity,
                    state->pairCountNow + 1)) {
                state->pairsNow[state->pairCountNow].gA = cA;
                state->pairsNow[state->pairCountNow].gB = cB;
                state->pairCountNow++;
            }
            gX_PhysicsContactEvent event;
            event.phase = pairFind_(state->pairsPrev, state->pairCountPrev,
                cA, cB) ? gX_PHYSICS_PHASE_STAY : gX_PHYSICS_PHASE_ENTER;
            event.bodyA = pb1;
            event.bodyB = pb2;
            event.tagA = data1 ? data1->userTag : 0u;
            event.tagB = data2 ? data2->userTag : 0u;
            event.contactCount = (gX_U32)numc;
            event.contacts = gxc;
            state->eventCB(state->eventUserData, &event);
        }
    }

    if ((data1 && data1->sensor) || (data2 && data2->sensor)) return;

    //opencode: default contact response uses the least permissive material values.
    if (!handled && numc > 0) {
        dJointGroupID jg = state->jointGroup;
        if (jg) {
            gX_PhysicsMaterial material1 = data1 ? data1->material : defaultMaterial_();
            gX_PhysicsMaterial material2 = data2 ? data2->material : defaultMaterial_();
            gX_F32 friction = material1.friction < material2.friction
                ? material1.friction : material2.friction;
            gX_F32 restitution = material1.restitution > material2.restitution
                ? material1.restitution : material2.restitution;
            //ai: rolling/spin resistances add (each surface contributes), and
            //   the tangential surface velocities add into one relative belt
            //   motion along the first friction direction.
            gX_F32 rolling = material1.rollingFriction
                + material2.rollingFriction;
            gX_F32 motion = material1.surfaceVelocity
                + material2.surfaceVelocity;
            for (gX_I32 i = 0; i < numc; i++) {
                contacts[i].surface.mode = dContactBounce | dContactSoftCFM;
                contacts[i].surface.mu = (dReal)friction;
                contacts[i].surface.mu2 = 0;
                contacts[i].surface.bounce = (dReal)restitution;
                contacts[i].surface.bounce_vel = 0.1f;
                //ai: soft contacts keep resting stacks stable, but the ported
                //   0.01 swallowed most of the bounce (a nominal 0.85
                //   restitution rebounded at ~0.33 of the impact speed, so
                //   per-material restitution was effectively dead); 1e-4
                //   still settles and lets the material pins rebound.
                contacts[i].surface.soft_cfm = 0.0001f;
                if (rolling > 0.0f) {
                    //ai: rolling (rho1/rho2) plus spin (rhoN) resistance, so
                    //   balls wind down by surface instead of rolling forever;
                    //   dContactApprox1 includes the rolling-friction mode
                    //   (approx1_N) and the per-direction friction terms.
                    contacts[i].surface.mode |= dContactRolling
                        | dContactApprox1;
                    contacts[i].surface.rho = (dReal)rolling;
                    contacts[i].surface.rho2 = (dReal)rolling;
                    contacts[i].surface.rhoN = (dReal)rolling;
                }
                if (motion != 0.0f) {
                    //ai: belt surface — fdir1 is world +X projected onto the
                    //   contact plane (fallback +Y when X is the normal), and
                    //   motion1 is the combined surface speed along it.
                    dReal nx = contacts[i].geom.normal[0];
                    dReal ny = contacts[i].geom.normal[1];
                    dReal nz = contacts[i].geom.normal[2];
                    dReal fx = 1.0 - nx * nx;
                    dReal fy = -nx * ny;
                    dReal fz = -nx * nz;
                    dReal len = sqrt((double)(fx * fx + fy * fy + fz * fz));
                    if (len < 1e-4) {
                        fx = -nx * ny;
                        fy = 1.0 - ny * ny;
                        fz = -ny * nz;
                        len = sqrt((double)(fx * fx + fy * fy + fz * fz));
                    }
                    if (len > 1e-4) {
                        contacts[i].surface.mode |= dContactMotion1
                            | dContactFDir1;
                        contacts[i].fdir1[0] = fx / len;
                        contacts[i].fdir1[1] = fy / len;
                        contacts[i].fdir1[2] = fz / len;
                        contacts[i].surface.motion1 = (dReal)motion;
                    }
                }
                dJointID cj = dJointCreateContact(state->world, jg, &contacts[i]);
                dJointAttach(cj, b1, b2);
            }
        }
    }
    #undef MAXC
}

// ========================================================================
// Public API — ODE implementation
// ========================================================================

//opencode: create an independently-owned ODE world state.
gX_PhysicsWorld gX_PhysicsWorld_Create(gX_I0) {
    //opencode: initialize ODE once so independent worlds can coexist safely.
    if (g_odeRefCount == 0) dInitODE2(0);
    g_odeRefCount++;
    dAllocateODEDataForThread(dAllocateMaskAll);

    dWorldID w = dWorldCreate();
    dSpaceID space = dHashSpaceCreate(0);
    gX_PhysicsWorldData_ *state = (gX_PhysicsWorldData_ *)calloc(1, sizeof(*state));
    if (!w || !space || !state) {
        if (space) dSpaceDestroy(space);
        if (w) dWorldDestroy(w);
        free(state);
        g_odeRefCount--;
        if (g_odeRefCount == 0) dCloseODE();
        return gX_PHYSICS_WORLD_NULL;
    }
    state->world = w;
    state->space = space;
    state->maxContacts = 8;
    dWorldSetData(w, state);

    //opencode: default solver parameters
    dWorldSetCFM(w, 1e-5);
    dWorldSetERP(w, 0.2);
    dWorldSetAutoDisableFlag(w, 1);
    dWorldSetAutoDisableLinearThreshold(w, 0.5);
    dWorldSetAutoDisableAngularThreshold(w, 0.5);
    dWorldSetAutoDisableSteps(w, 10);
    dWorldSetAutoDisableTime(w, 1.0);
    dWorldSetLinearDamping(w, 0.01f);
    dWorldSetAngularDamping(w, 0.05f);
    dWorldSetLinearDampingThreshold(w, 0.01f);
    dWorldSetAngularDampingThreshold(w, 0.01f);
    dWorldSetMaxAngularSpeed(w, 200);
    dWorldSetContactMaxCorrectingVel(w, 100.0);
    dWorldSetContactSurfaceLayer(w, 0.01);

    //opencode: create contact and solver state owned by this world.
    state->jointGroup = dJointGroupCreate(0);

    //ai: the static-environment anchor for joints that need two bodies.
    state->anchorBody = dBodyCreate(w);
    if (state->anchorBody) {
        dBodySetKinematic(state->anchorBody);
        dBodySetAutoDisableFlag(state->anchorBody, 0);
    }

    return wrapWorld(w);
}

//opencode: destroy world-owned geometry metadata and solver resources.
gX_I0 gX_PhysicsWorld_Destroy(gX_PhysicsWorld world) {
    dWorldID w = unwrapWorld(world);
    if (!w) return;
    gX_PhysicsWorldData_ *state = getWorldData(w);
    dSpaceID space = getWorldSpace(w);

    //opencode: free wrapper metadata before ODE destroys the geoms.
    if (space) {
        gX_I32 total = dSpaceGetNumGeoms(space);
        for (gX_I32 i = 0; i < total; i++) {
            destroyGeomData_(dSpaceGetGeom(space, i));
        }
    }

    //opencode: clean up this world's contact and threaded solver state.
    if (state && state->jointGroup) {
        dJointGroupDestroy(state->jointGroup);
    }
    if (state && state->threading) {
        dThreadingImplementationShutdownProcessing(state->threading);
        if (state->pool) {
            dThreadingThreadPoolServeMultiThreadedImplementation(
                state->pool, state->threading);
        }
        dWorldSetStepThreadingImplementation(w, NULL, NULL);
        dThreadingFreeImplementation(state->threading);
    }
    if (state && state->pool) {
        dThreadingFreeThreadPool(state->pool);
    }

    //opencode: free the contact event pair tracking arrays.
    if (state) {
        free(state->pairsPrev);
        free(state->pairsNow);
        free(state->fb);
        free(state->fbJoints);
        if (state->anchorBody) dBodyDestroy(state->anchorBody);
    }

    if (space) dSpaceDestroy(space);
    dWorldDestroy(w);
    free(state);
    g_odeRefCount--;
    if (g_odeRefCount == 0) dCloseODE();
}

gX_I0 gX_PhysicsWorld_SetGravity(gX_PhysicsWorld world, gX_F32v3 gravity) {
    dWorldSetGravity(unwrapWorld(world), (dReal)gravity.x, (dReal)gravity.y, (dReal)gravity.z);
}
gX_I0 gX_PhysicsWorld_SetSolverParams(gX_PhysicsWorld world, gX_F32 cfm, gX_F32 erp) {
    dWorldSetCFM(unwrapWorld(world), (dReal)cfm);
    dWorldSetERP(unwrapWorld(world), (dReal)erp);
}

//opencode: forward declaration — Step/QuickStep call the EXIT sweep after collide.
static gX_I0 sweepEvents_(gX_PhysicsWorldData_ *state);

gX_I0 gX_PhysicsWorld_Step(gX_PhysicsWorld world, gX_F32 dt) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);

    if (space) {
        dSpaceCollide(space, getWorldData(w), &nearCallback);
    }
    dWorldStep(w, (dReal)dt);
    sweepEvents_(getWorldData(w));
}

//opencode: after collision detection, fire EXIT for pairs that stopped
//   colliding this step, then roll the pair sets for the next step.
static gX_I0 sweepEvents_(gX_PhysicsWorldData_ *state) {
    if (state->eventCB) {
        for (gX_U32 i = 0; i < state->pairCountPrev; i++) {
            dGeomID gA = state->pairsPrev[i].gA;
            dGeomID gB = state->pairsPrev[i].gB;
            if (pairFind_(state->pairsNow, state->pairCountNow, gA, gB)) continue;
            dBodyID bA = dGeomGetBody(gA);
            dBodyID bB = dGeomGetBody(gB);
            gX_PhysicsGeomData_ *dataA = getGeomData(gA);
            gX_PhysicsGeomData_ *dataB = getGeomData(gB);
            gX_PhysicsContactEvent event;
            event.phase = gX_PHYSICS_PHASE_EXIT;
            event.bodyA = wrapBody(bA);
            event.bodyB = wrapBody(bB);
            event.tagA = dataA ? dataA->userTag : 0u;
            event.tagB = dataB ? dataB->userTag : 0u;
            event.contactCount = 0;
            event.contacts = NULL;
            state->eventCB(state->eventUserData, &event);
        }
    }

    //opencode: roll forward — this step's pairs become next step's "previous".
    gX_PhysicsPair_ *tmp = state->pairsPrev;
    state->pairsPrev = state->pairsNow;
    state->pairsNow = tmp;
    state->pairCountPrev = state->pairCountNow;
    state->pairCountNow = 0;
}

gX_I0 gX_PhysicsWorld_QuickStep(gX_PhysicsWorld world, gX_F32 dt) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);

    if (space) {
        dSpaceCollide(space, getWorldData(w), &nearCallback);
    }
    dWorldQuickStep(w, (dReal)dt);
    sweepEvents_(getWorldData(w));
}

gX_I0 gX_PhysicsWorld_SetCollisionCallback(gX_PhysicsWorld world,
    gX_PhysicsCollisionCB cb, gX_I0 *userData) {
    dWorldID w = unwrapWorld(world);
    gX_PhysicsWorldData_ *state = getWorldData(w);
    if (!state) return;
    state->cb = cb;
    state->userData = userData;
}

gX_I0 gX_PhysicsWorld_SetEventCallback(gX_PhysicsWorld world,
    gX_PhysicsEventCB cb, gX_I0 *userData) {
    dWorldID w = unwrapWorld(world);
    gX_PhysicsWorldData_ *state = getWorldData(w);
    if (!state) return;
    state->eventCB = cb;
    state->eventUserData = userData;
    if (!cb) {
        //opencode: disabling the callback also clears tracked pairs.
        state->pairCountPrev = 0;
        state->pairCountNow = 0;
    }
}

// ========================================================================
// Bodies
// ========================================================================

//opencode: create static geometry without allocating an ODE dynamic body.
static gX_PhysicsCollider createStatic_(dGeomID geom, gX_F32v3 position) {
    if (!geom) return gX_PHYSICS_COLLIDER_NULL;
    applyGeomDefaults_(geom);
    //opencode: plane geoms encode position in the plane equation and are not placeable.
    if (dGeomGetClass(geom) != dPlaneClass) {
        dGeomSetPosition(geom, (dReal)position.x, (dReal)position.y, (dReal)position.z);
    }
    return wrapCollider(geom);
}

gX_PhysicsBody gX_PhysicsBody_AddBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    dGeomID geom = dCreateBox(space, (dReal)size.x, (dReal)size.y, (dReal)size.z);
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetBox(&dm, (dReal)mass, (dReal)size.x, (dReal)size.y, (dReal)size.z);
    if (body) {
        dBodySetMass(body, &dm);
        dGeomSetBody(geom, body);
        dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
        applyGeomDefaults_(geom);
        rememberShapeMass_(geom, &dm);
    }
    return wrapBody(body);
}

gX_PhysicsBody gX_PhysicsBody_AddSphere(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    dGeomID geom = dCreateSphere(space, (dReal)radius);
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetSphere(&dm, (dReal)mass, (dReal)radius);
    if (body) {
        dBodySetMass(body, &dm);
        dGeomSetBody(geom, body);
        dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
        applyGeomDefaults_(geom);
        rememberShapeMass_(geom, &dm);
    }
    return wrapBody(body);
}

//opencode: capsule body — direction 1=X, 2=Y, 3=Z (alignment axis)
gX_PhysicsBody gX_PhysicsBody_AddCapsule(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    dGeomID geom = dCreateCapsule(space, (dReal)radius, (dReal)length);
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetCapsule(&dm, (dReal)mass, direction, (dReal)radius, (dReal)length);
    if (body) {
        dBodySetMass(body, &dm);
        dGeomSetBody(geom, body);
        dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
        applyGeomDefaults_(geom);
        rememberShapeMass_(geom, &dm);
    }
    return wrapBody(body);
}

//opencode: cylinder body — direction 1=X, 2=Y, 3=Z (alignment axis)
gX_PhysicsBody gX_PhysicsBody_AddCylinder(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    dGeomID geom = dCreateCylinder(space, (dReal)radius, (dReal)length);
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetCylinder(&dm, (dReal)mass, direction, (dReal)radius, (dReal)length);
    if (body) {
        dBodySetMass(body, &dm);
        dGeomSetBody(geom, body);
        dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
        applyGeomDefaults_(geom);
        rememberShapeMass_(geom, &dm);
    }
    return wrapBody(body);
}

//opencode: convex hull — copies planes/points/polygons into heap memory
//   and stores pointer in geom user data for cleanup on destroy.
gX_PhysicsBody gX_PhysicsBody_AddConvex(gX_PhysicsWorld world,
    const gX_F32 *planes, gX_U32 planeCount,
    const gX_F32 *points, gX_U32 pointCount,
    const gX_U32 *polygons,
    gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);

    //opencode: copy all variable-length data into a single allocation
    //   layout: [planes(4*planeCount)][points(3*pointCount)][polygons(var)]
    //   stored as geom data for later free.
    gX_U32 polyCount = 0;
    gX_U32 polyTotal = 0;
    {
        const gX_U32 *p = polygons;
        while (polyTotal < 65536) {  // sanity limit
            gX_U32 verts = *p;
            if (verts == 0 || verts > 64) break;
            polyTotal += 1 + verts;
            p += 1 + verts;
            polyCount++;
            if (polyCount >= planeCount) break;
        }
    }
    gX_I0 *mem = (gX_I0*)malloc(
        (size_t)planeCount * 4 * sizeof(gX_F32) +
        (size_t)pointCount * 3 * sizeof(gX_F32) +
        (size_t)polyTotal * sizeof(gX_U32));
    if (!mem) return gX_PHYSICS_BODY_NULL;

    gX_F32 *memPlanes = (gX_F32*)mem;
    memcpy(memPlanes, planes, planeCount * 4 * sizeof(gX_F32));

    gX_F32 *memPoints = (gX_F32*)(memPlanes + planeCount * 4);
    memcpy(memPoints, points, pointCount * 3 * sizeof(gX_F32));

    gX_U32 *memPoly = (gX_U32*)(memPoints + pointCount * 3);
    memcpy(memPoly, polygons, polyTotal * sizeof(gX_U32));

    dGeomID geom = dCreateConvex(space,
        (const dReal*)memPlanes, (unsigned int)planeCount,
        (const dReal*)memPoints, (unsigned int)pointCount,
        (const unsigned int*)memPoly);
    if (!geom) {
        free(mem);
        return gX_PHYSICS_BODY_NULL;
    }
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetSphereTotal(&dm, (dReal)mass, 1.0f);  //opencode: sphere approximation
    if (!body) {
        dGeomDestroy(geom);
        free(mem);
        return gX_PHYSICS_BODY_NULL;
    }
    dBodySetMass(body, &dm);
    dGeomSetBody(geom, body);
    gX_PhysicsGeomData_ *geomData = initGeomData_(geom);
    if (geomData) geomData->shapeData = mem;
    else free(mem);
    rememberShapeMass_(geom, &dm);
    dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
    return wrapBody(body);
}

//opencode: triangle mesh — creates data object internally, stored in
//   geom user data for lifecycle management.
gX_PhysicsBody gX_PhysicsBody_AddTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32 mass, gX_F32v3 position) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);

    dTriMeshDataID data = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(data,
        vertices, 3 * (int)sizeof(gX_F32), (int)vertexCount,
        indices, (int)indexCount, 3 * (int)sizeof(gX_U32));

    dGeomID geom = dCreateTriMesh(space, data, NULL, NULL, NULL);
    if (!geom) {
        dGeomTriMeshDataDestroy(data);
        return gX_PHYSICS_BODY_NULL;
    }
    dBodyID body = dBodyCreate(w);
    dMass dm;
    dMassSetSphereTotal(&dm, (dReal)mass, 1.0f);  //opencode: sphere approximation
    if (!body) {
        dGeomDestroy(geom);
        dGeomTriMeshDataDestroy(data);
        return gX_PHYSICS_BODY_NULL;
    }
    dBodySetMass(body, &dm);
    dGeomSetBody(geom, body);
    gX_PhysicsGeomData_ *geomData = initGeomData_(geom);
    if (geomData) {
        geomData->dataClass = dTriMeshClass;
        geomData->shapeData = (gX_I0 *)data;
    } else {
        dGeomTriMeshDataDestroy(data);
    }
    rememberShapeMass_(geom, &dm);
    dBodySetPosition(body, (dReal)position.x, (dReal)position.y, (dReal)position.z);
    return wrapBody(body);
}

//opencode: static collider constructors for non-dynamic scene geometry.
gX_PhysicsCollider gX_PhysicsWorld_AddStaticBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32v3 position) {
    dSpaceID space = getWorldSpace(unwrapWorld(world));
    if (!space) return gX_PHYSICS_COLLIDER_NULL;
    return createStatic_(dCreateBox(space, (dReal)size.x, (dReal)size.y, (dReal)size.z), position);
}

gX_PhysicsCollider gX_PhysicsWorld_AddStaticTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32v3 position) {
    dSpaceID space = getWorldSpace(unwrapWorld(world));
    if (!space) return gX_PHYSICS_COLLIDER_NULL;
    dTriMeshDataID data = dGeomTriMeshDataCreate();
    if (!data) return gX_PHYSICS_COLLIDER_NULL;
    dGeomTriMeshDataBuildSingle(data, vertices, 3 * (int)sizeof(gX_F32),
        (int)vertexCount, indices, (int)indexCount, 3 * (int)sizeof(gX_U32));
    dGeomID geom = dCreateTriMesh(space, data, NULL, NULL, NULL);
    if (!geom) {
        dGeomTriMeshDataDestroy(data);
        return gX_PHYSICS_COLLIDER_NULL;
    }
    gX_PhysicsCollider collider = createStatic_(geom, position);
    gX_PhysicsGeomData_ *geomData = getGeomData(geom);
    if (geomData) {
        geomData->dataClass = dTriMeshClass;
        geomData->shapeData = (gX_I0 *)data;
    } else {
        dGeomTriMeshDataDestroy(data);
    }
    return collider;
}

gX_PhysicsCollider gX_PhysicsWorld_AddGroundPlane(gX_PhysicsWorld world,
    gX_F32v3 normal, gX_F32 dist) {
    dSpaceID space = getWorldSpace(unwrapWorld(world));
    if (!space) return gX_PHYSICS_COLLIDER_NULL;
    dGeomID geom = dCreatePlane(space, (dReal)normal.x, (dReal)normal.y,
        (dReal)normal.z, (dReal)dist);
    return createStatic_(geom, (gX_F32v3){0.0f, 0.0f, 0.0f});
}

//ai: static heightfield terrain — a collider sampling a caller height grid
//   (widthSamples x depthSamples, row-major with x varying fastest along a
//   row). The field is built WRAPPED: ODE tiles it infinitely in its local
//   x/z, and this build's bounded mode drops bodies through erratically
//   (probed), so the caller passes one tile centred on `position` and keeps
//   the tile borders flat (0 heights) — the repeated tiles are then flat
//   and harmless. Each height is multiplied by heightScale and the min/max
//   derive the AABB bounds; the heights are copied, so the caller's array
//   need not outlive the call.
gX_PhysicsCollider gX_PhysicsWorld_AddHeightfield(gX_PhysicsWorld world,
    const gX_F32 *heights, gX_I32 widthSamples, gX_I32 depthSamples,
    gX_F32 width, gX_F32 depth, gX_F32 heightScale, gX_F32v3 position) {
    dSpaceID space = getWorldSpace(unwrapWorld(world));
    if (!space || !heights || widthSamples < 2 || depthSamples < 2) {
        return gX_PHYSICS_COLLIDER_NULL;
    }
    if (width < 1e-4f) width = 1.0f;
    if (depth < 1e-4f) depth = 1.0f;
    if (heightScale == 0.0f) heightScale = 1.0f;
    gX_I32 count = widthSamples * depthSamples;
    gX_F32 minHeight = heights[0];
    gX_F32 maxHeight = heights[0];
    for (gX_I32 i = 1; i < count; i++) {
        if (heights[i] < minHeight) minHeight = heights[i];
        if (heights[i] > maxHeight) maxHeight = heights[i];
    }
    dHeightfieldDataID data = dGeomHeightfieldDataCreate();
    if (!data) return gX_PHYSICS_COLLIDER_NULL;
    dGeomHeightfieldDataBuildSingle(data, heights, 1,
        (dReal)width, (dReal)depth, widthSamples, depthSamples,
        (dReal)heightScale, 0.0, 0.0, 1);
    dGeomHeightfieldDataSetBounds(data, (dReal)(minHeight * heightScale),
        (dReal)(maxHeight * heightScale));
    dGeomID geom = dCreateHeightfield(space, data, 1);
    if (!geom) {
        dGeomHeightfieldDataDestroy(data);
        return gX_PHYSICS_COLLIDER_NULL;
    }
    gX_PhysicsCollider collider = createStatic_(geom, (gX_F32v3){
        position.x - width * 0.5f, position.y, position.z - depth * 0.5f});
    gX_PhysicsGeomData_ *geomData = getGeomData(geom);
    if (geomData) {
        geomData->dataClass = dHeightfieldClass;
        geomData->shapeData = (gX_I0 *)data;
    } else {
        dGeomHeightfieldDataDestroy(data);
    }
    return collider;
}

//opencode: body and static collider destruction with metadata cleanup.
gX_I0 gX_PhysicsBody_Destroy(gX_PhysicsWorld world, gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (!b) return;
    dGeomID g = dBodyGetFirstGeom(b);
    if (g) {
        gX_PhysicsWorldData_ *state = getWorldData(unwrapWorld(world));
        if (state) {
            pairPurgeGeom_(state->pairsPrev, &state->pairCountPrev, g);
            pairPurgeGeom_(state->pairsNow, &state->pairCountNow, g);
        }
        destroyGeomData_(g);
        //ai: the wrapper creates exactly one geom per body; ODE's
        //   dBodyDestroy only *detaches* attached geoms, so the geom must be
        //   destroyed first or it stays in the space as a ghost collider at
        //   its last pose (found by the sandbox landing test).
        dGeomDestroy(g);
    }
    dBodyDestroy(b);
}

gX_I0 gX_PhysicsCollider_Destroy(gX_PhysicsWorld world,
    gX_PhysicsCollider collider) {
    dGeomID geom = unwrapCollider(collider);
    if (!geom) return;
    gX_PhysicsWorldData_ *state = getWorldData(unwrapWorld(world));
    if (state) {
        pairPurgeGeom_(state->pairsPrev, &state->pairCountPrev, geom);
        pairPurgeGeom_(state->pairsNow, &state->pairCountNow, geom);
    }
    destroyGeomData_(geom);
    dGeomDestroy(geom);
}

gX_F32v3 gX_PhysicsBody_GetPosition(gX_PhysicsBody body) {
    const dReal *p = dBodyGetPosition(unwrapBody(body));
    return (gX_F32v3){ (gX_F32)p[0], (gX_F32)p[1], (gX_F32)p[2] };
}

//ai: explicit mutations wake auto-disabled bodies — ODE ignores velocity
//   and force changes on a disabled body, while the sandbox drag, reset and
//   the field appliers all rely on the wrapper enabling it here. Contacts
//   still wake sleeping bodies on their own.
static dBodyID awakeBody_(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (b) dBodyEnable(b);
    return b;
}

//ai: a motor command is an explicit mutation — wake both joint bodies so a
//   sleeping rig responds (ODE ignores motor input on disabled bodies).
static void awakeJoint_(dJointID j) {
    if (!j) return;
    for (gX_I32 i = 0; i < 2; i++) {
        dBodyID b = dJointGetBody(j, i);
        if (b) dBodyEnable(b);
    }
}

//ai: joint feedback registry helpers (see gX_PhysicsWorldData_).
static gX_I32 feedbackFind_(gX_PhysicsWorldData_ *state, dJointID j) {
    if (!state) return -1;
    for (gX_U32 i = 0; i < state->fbCount; i++) {
        if (state->fbJoints[i] == j) return (gX_I32)i;
    }
    return -1;
}

static gX_I1 feedbackAdd_(gX_PhysicsWorldData_ *state, dJointID j) {
    if (!state || !j || feedbackFind_(state, j) >= 0) return state != NULL;
    if (state->fbCount >= state->fbCapacity) {
        gX_U32 cap = state->fbCapacity ? state->fbCapacity * 2 : 8;
        dJointFeedback *fb = (dJointFeedback *)realloc(state->fb,
            sizeof(*fb) * cap);
        if (!fb) return false;
        state->fb = fb;
        dJointID *ids = (dJointID *)realloc(state->fbJoints,
            sizeof(*ids) * cap);
        if (!ids) return false;
        state->fbJoints = ids;
        state->fbCapacity = cap;
    }
    gX_U32 i = state->fbCount++;
    state->fbJoints[i] = j;
    memset(&state->fb[i], 0, sizeof(state->fb[i]));
    dJointSetFeedback(j, &state->fb[i]);
    return true;
}

static void feedbackRemove_(gX_PhysicsWorldData_ *state, dJointID j) {
    gX_I32 i = feedbackFind_(state, j);
    if (i < 0) return;
    dJointSetFeedback(j, NULL);
    gX_U32 last = state->fbCount - 1;
    if ((gX_U32)i != last) {
        state->fbJoints[i] = state->fbJoints[last];
        state->fb[i] = state->fb[last];
        dJointSetFeedback(state->fbJoints[i], &state->fb[i]);
    }
    state->fbCount = last;
}

gX_I0 gX_PhysicsBody_SetPosition(gX_PhysicsBody body, gX_F32v3 pos) {
    dBodySetPosition(awakeBody_(body), (dReal)pos.x, (dReal)pos.y, (dReal)pos.z);
}

gX_I0 gX_PhysicsBody_GetRotation(gX_PhysicsBody body, gX_F32Qua *outQuat) {
    if (!outQuat) return;
    const dReal *dq = dBodyGetQuaternion(unwrapBody(body));
    //opencode: ODE quat is [w,x,y,z], gX uses [x,y,z,w]
    outQuat->x = (gX_F32)dq[1];
    outQuat->y = (gX_F32)dq[2];
    outQuat->z = (gX_F32)dq[3];
    outQuat->w = (gX_F32)dq[0];
}

//opencode: body type, transform, sleep, and physical property accessors.
gX_I0 gX_PhysicsBody_SetRotation(gX_PhysicsBody body, gX_F32Qua quat) {
    dBodyID b = unwrapBody(body);
    if (!b) return;
    gX_F32Qua q = quat;
    gX_F32 length = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (length <= 0.000001f) return;
    q.x /= length;
    q.y /= length;
    q.z /= length;
    q.w /= length;
    dQuaternion dq = {(dReal)q.w, (dReal)q.x, (dReal)q.y, (dReal)q.z};
    dBodyEnable(b);
    dBodySetQuaternion(b, dq);
}

gX_I0 gX_PhysicsBody_SetType(gX_PhysicsBody body, gX_PhysicsBodyType type) {
    dBodyID b = unwrapBody(body);
    if (!b) return;
    if (type == gX_PHYSICS_BODY_KINEMATIC) dBodySetKinematic(b);
    else dBodySetDynamic(b);
    dBodyEnable(b);
}

gX_PhysicsBodyType gX_PhysicsBody_GetType(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (!b) return gX_PHYSICS_BODY_DYNAMIC;
    return dBodyIsKinematic(b) ? gX_PHYSICS_BODY_KINEMATIC : gX_PHYSICS_BODY_DYNAMIC;
}

gX_I1 gX_PhysicsBody_IsEnabled(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    return b ? (gX_I1)dBodyIsEnabled(b) : 0;
}

gX_I0 gX_PhysicsBody_Enable(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (b) dBodyEnable(b);
}

//ai: per-body auto-disable override (the world default is on; disabling it
//   keeps a body simulating after it settles).
gX_I0 gX_PhysicsBody_SetAutoDisable(gX_PhysicsBody body, gX_I1 enabled) {
    dBodyID b = unwrapBody(body);
    if (b) dBodySetAutoDisableFlag(b, enabled ? 1 : 0);
}

gX_I1 gX_PhysicsBody_GetAutoDisable(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    return b ? (gX_I1)dBodyGetAutoDisableFlag(b) : 0;
}

gX_I0 gX_PhysicsBody_Disable(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (b) dBodyDisable(b);
}

gX_F32v3 gX_PhysicsBody_GetLinearVel(gX_PhysicsBody body) {
    const dReal *v = dBodyGetLinearVel(unwrapBody(body));
    return (gX_F32v3){ (gX_F32)v[0], (gX_F32)v[1], (gX_F32)v[2] };
}

gX_F32v3 gX_PhysicsBody_GetAngularVel(gX_PhysicsBody body) {
    const dReal *v = dBodyGetAngularVel(unwrapBody(body));
    return (gX_F32v3){ (gX_F32)v[0], (gX_F32)v[1], (gX_F32)v[2] };
}

gX_I0 gX_PhysicsBody_SetLinearVel(gX_PhysicsBody body, gX_F32v3 vel) {
    dBodySetLinearVel(awakeBody_(body), (dReal)vel.x, (dReal)vel.y, (dReal)vel.z);
}

gX_I0 gX_PhysicsBody_SetAngularVel(gX_PhysicsBody body, gX_F32v3 vel) {
    dBodySetAngularVel(awakeBody_(body), (dReal)vel.x, (dReal)vel.y, (dReal)vel.z);
}

gX_F32 gX_PhysicsBody_GetMass(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (!b) return 0.0f;
    dMass mass;
    dBodyGetMass(b, &mass);
    return (gX_F32)mass.mass;
}

//ai: mass properties — SetMass rescales mass and inertia together (the
//   stored shape mass scales too, so a later SetMassCenter stays
//   consistent). ODE 0.16 requires the center of mass at the body origin,
//   so SetMassCenter puts the center at `offset` from the shape center by
//   offsetting the shape the other way and moving the body origin onto the
//   balance point (the shape keeps its world pose); the inertia about the
//   center is rebuilt from the shape mass with the parallel axis theorem.
gX_I0 gX_PhysicsBody_SetMass(gX_PhysicsBody body, gX_F32 mass) {
    dBodyID b = awakeBody_(body);
    if (!b || !(mass > 0.0f)) return;
    dMass dm;
    dBodyGetMass(b, &dm);
    dMassAdjust(&dm, (dReal)mass);
    dBodySetMass(b, &dm);
    gX_PhysicsGeomData_ *data = getGeomData(dBodyGetFirstGeom(b));
    if (data) dMassAdjust(&data->baseMass, (dReal)mass);
}

gX_I0 gX_PhysicsBody_SetMassCenter(gX_PhysicsBody body, gX_F32v3 offset) {
    dBodyID b = awakeBody_(body);
    dGeomID geom = b ? dBodyGetFirstGeom(b) : NULL;
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (!b || !geom || !data) return;
    const dReal *g = dGeomGetOffsetPosition(geom);
    dReal d[3] = {(dReal)offset.x + g[0], (dReal)offset.y + g[1],
        (dReal)offset.z + g[2]};
    dGeomSetOffsetPosition(geom, (dReal)-offset.x, (dReal)-offset.y,
        (dReal)-offset.z);
    dMass dm = data->baseMass;
    dMassTranslate(&dm, (dReal)offset.x, (dReal)offset.y, (dReal)offset.z);
    dm.c[0] = 0.0;
    dm.c[1] = 0.0;
    dm.c[2] = 0.0;
    dBodySetMass(b, &dm);
    //ai: the body origin follows the balance point; the shape's world pose
    //   is unchanged.
    dVector3 world;
    dBodyVectorToWorld(b, d[0], d[1], d[2], world);
    const dReal *p = dBodyGetPosition(b);
    dBodySetPosition(b, p[0] + world[0], p[1] + world[1], p[2] + world[2]);
}

gX_F32v3 gX_PhysicsBody_GetMassCenter(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    dGeomID geom = b ? dBodyGetFirstGeom(b) : NULL;
    if (!geom) return (gX_F32v3){0.0f, 0.0f, 0.0f};
    const dReal *g = dGeomGetOffsetPosition(geom);
    return (gX_F32v3){-(gX_F32)g[0], -(gX_F32)g[1], -(gX_F32)g[2]};
}

gX_F32v3 gX_PhysicsBody_GetInertia(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    if (!b) return (gX_F32v3){0.0f, 0.0f, 0.0f};
    dMass mass;
    dBodyGetMass(b, &mass);
    return (gX_F32v3){(gX_F32)mass.I[0], (gX_F32)mass.I[5], (gX_F32)mass.I[10]};
}

gX_I0 gX_PhysicsBody_SetDamping(gX_PhysicsBody body,
    gX_F32 linear, gX_F32 angular) {
    dBodyID b = unwrapBody(body);
    if (!b) return;
    dBodySetLinearDamping(b, (dReal)linear);
    dBodySetAngularDamping(b, (dReal)angular);
}

//opencode: return the single geometry currently attached by the primitive API.
static dGeomID bodyGeom_(gX_PhysicsBody body) {
    dBodyID b = unwrapBody(body);
    return b ? dBodyGetFirstGeom(b) : NULL;
}
static void setGeomMaterial_(dGeomID geom, gX_PhysicsMaterial material) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (data) data->material = material;
}
static void setGeomFilter_(dGeomID geom, gX_U32 categoryBits, gX_U32 maskBits) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (!data) return;
    data->categoryBits = categoryBits;
    data->maskBits = maskBits;
    dGeomSetCategoryBits(geom, categoryBits);
    dGeomSetCollideBits(geom, maskBits);
}
static void setGeomSensor_(dGeomID geom, gX_I1 sensor) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (data) data->sensor = sensor ? 1 : 0;
}
static void setGeomTag_(dGeomID geom, gX_U64 tag) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    if (data) data->userTag = tag;
}
static gX_U64 getGeomTag_(dGeomID geom) {
    gX_PhysicsGeomData_ *data = getGeomData(geom);
    return data ? data->userTag : 0;
}

//opencode: material, filtering, sensor, and user-tag accessors.
gX_I0 gX_PhysicsBody_SetMaterial(gX_PhysicsBody body,
    gX_PhysicsMaterial material) {
    setGeomMaterial_(bodyGeom_(body), material);
}

gX_I0 gX_PhysicsBody_SetCollisionFilter(gX_PhysicsBody body,
    gX_U32 categoryBits, gX_U32 maskBits) {
    setGeomFilter_(bodyGeom_(body), categoryBits, maskBits);
}

gX_I0 gX_PhysicsBody_SetSensor(gX_PhysicsBody body, gX_I1 sensor) {
    setGeomSensor_(bodyGeom_(body), sensor);
}

gX_I0 gX_PhysicsBody_SetUserTag(gX_PhysicsBody body, gX_U64 tag) {
    setGeomTag_(bodyGeom_(body), tag);
}

gX_U64 gX_PhysicsBody_GetUserTag(gX_PhysicsBody body) {
    return getGeomTag_(bodyGeom_(body));
}

gX_I0 gX_PhysicsCollider_SetMaterial(gX_PhysicsCollider collider,
    gX_PhysicsMaterial material) {
    setGeomMaterial_(unwrapCollider(collider), material);
}

gX_I0 gX_PhysicsCollider_SetCollisionFilter(gX_PhysicsCollider collider,
    gX_U32 categoryBits, gX_U32 maskBits) {
    setGeomFilter_(unwrapCollider(collider), categoryBits, maskBits);
}

gX_I0 gX_PhysicsCollider_SetSensor(gX_PhysicsCollider collider, gX_I1 sensor) {
    setGeomSensor_(unwrapCollider(collider), sensor);
}

gX_I0 gX_PhysicsCollider_SetUserTag(gX_PhysicsCollider collider, gX_U64 tag) {
    setGeomTag_(unwrapCollider(collider), tag);
}

gX_U64 gX_PhysicsCollider_GetUserTag(gX_PhysicsCollider collider) {
    return getGeomTag_(unwrapCollider(collider));
}

gX_I0 gX_PhysicsBody_ApplyForce(gX_PhysicsBody body, gX_F32v3 force) {
    dBodyAddForce(awakeBody_(body), (dReal)force.x, (dReal)force.y, (dReal)force.z);
}

gX_I0 gX_PhysicsBody_ApplyTorque(gX_PhysicsBody body, gX_F32v3 torque) {
    dBodyAddTorque(awakeBody_(body), (dReal)torque.x, (dReal)torque.y, (dReal)torque.z);
}

gX_I0 gX_PhysicsBody_ApplyForceAtPos(gX_PhysicsBody body,
    gX_F32v3 force, gX_F32v3 pos) {
    dBodyAddForceAtPos(awakeBody_(body),
        (dReal)force.x, (dReal)force.y, (dReal)force.z,
        (dReal)pos.x, (dReal)pos.y, (dReal)pos.z);
}

gX_I0 gX_PhysicsWorld_EmptyContactGroup(gX_PhysicsWorld world) {
    gX_PhysicsWorldData_ *state = getWorldData(unwrapWorld(world));
    if (state && state->jointGroup) dJointGroupEmpty(state->jointGroup);
}

gX_I0 gX_PhysicsWorld_SetMaxContacts(gX_PhysicsWorld world, gX_I32 count) {
    gX_PhysicsWorldData_ *state = getWorldData(unwrapWorld(world));
    if (!state) return;
    state->maxContacts = count;
    if (state->maxContacts < 1) state->maxContacts = 1;
    if (state->maxContacts > 8) state->maxContacts = 8;
}

gX_I0 gX_PhysicsWorld_SetThreading(gX_PhysicsWorld world, gX_I1 enable) {
    dWorldID w = unwrapWorld(world);
    gX_PhysicsWorldData_ *state = getWorldData(w);
    if (!w || !state) return;

    if (enable && !state->useThreading) {
        //opencode: initialize multi-threaded solver
        if (!state->threading) {
            state->threading =
                dThreadingAllocateMultiThreadedImplementation();
        }
        if (!state->pool) {
            state->pool =
                dThreadingAllocateThreadPool(4, 0, dAllocateFlagBasicData, NULL);
        }
        if (state->pool && state->threading) {
            dThreadingThreadPoolServeMultiThreadedImplementation(
                state->pool, state->threading);
            dWorldSetStepThreadingImplementation(w,
                dThreadingImplementationGetFunctions(state->threading),
                state->threading);
            gX_TraceLog(gX_LOG_NOTICE, "PHYSICS: threaded solver enabled (4 threads)");
            state->useThreading = 1;
        }
    } else if (!enable && state->useThreading) {
        dWorldSetStepThreadingImplementation(w, NULL, NULL);
        state->useThreading = 0;
        gX_TraceLog(gX_LOG_NOTICE, "PHYSICS: threaded solver disabled");
    }
}

// ========================================================================
// Spatial queries
// ========================================================================

gX_I32 gX_PhysicsWorld_GetBodyCount(gX_PhysicsWorld world) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    if (!space) return 0;
    gX_I32 count = 0;
    gX_I32 total = dSpaceGetNumGeoms(space);
    for (gX_I32 i = 0; i < total; i++) {
        dGeomID g = dSpaceGetGeom(space, i);
        if (dGeomGetBody(g) != NULL) count++;
    }
    return count;
}

gX_PhysicsBody gX_PhysicsWorld_GetBodyByIndex(gX_PhysicsWorld world,
    gX_I32 index) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    if (!space || index < 0) return gX_PHYSICS_BODY_NULL;
    gX_I32 found = 0;
    gX_I32 total = dSpaceGetNumGeoms(space);
    for (gX_I32 i = 0; i < total; i++) {
        dGeomID g = dSpaceGetGeom(space, i);
        dBodyID b = dGeomGetBody(g);
        if (b != NULL) {
            if (found == index) return wrapBody(b);
            found++;
        }
    }
    return gX_PHYSICS_BODY_NULL;
}

// ========================================================================
// Raycasting
// ========================================================================

//opencode: per-raycast callback data
typedef struct gX_RaycastData_ {
    dGeomID  ray;
    gX_F32v3 origin;
    gX_F32   closestDist;
    gX_I1    hit;
    gX_F32v3 hitPos;
    gX_F32v3 hitNormal;
    dBodyID  hitBody;
    gX_U32   maskBits;
    gX_I1    includeSensors;
} gX_RaycastData_;

static void raycastCallback_(gX_I0 *data, dGeomID o1, dGeomID o2) {
    gX_RaycastData_ *rd = (gX_RaycastData_*)data;
    dGeomID target = (o1 == rd->ray) ? o2 : o1;
    if (target == rd->ray) return;  //opencode: shouldn't happen, but guard
    gX_PhysicsGeomData_ *targetData = getGeomData(target);
    if (targetData && !(targetData->categoryBits & rd->maskBits)) return;
    if (targetData && targetData->sensor && !rd->includeSensors) return;

    dContactGeom contact;
    gX_I32 numc = dCollide(rd->ray, target, 1, &contact, sizeof(dContactGeom));
    if (numc > 0) {
        gX_F32 dx = (gX_F32)contact.pos[0] - rd->origin.x;
        gX_F32 dy = (gX_F32)contact.pos[1] - rd->origin.y;
        gX_F32 dz = (gX_F32)contact.pos[2] - rd->origin.z;
        gX_F32 dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist < rd->closestDist) {
            rd->closestDist = dist;
            rd->hitPos = (gX_F32v3){
                (gX_F32)contact.pos[0],
                (gX_F32)contact.pos[1],
                (gX_F32)contact.pos[2]
            };
            rd->hitNormal = (gX_F32v3){
                (gX_F32)contact.normal[0],
                (gX_F32)contact.normal[1],
                (gX_F32)contact.normal[2]
            };
            rd->hitBody = dGeomGetBody(target);
            rd->hit = 1;
        }
    }
}

//opencode: filtered raycast preserves the original unfiltered API as a wrapper.
gX_I1 gX_PhysicsWorld_Raycast(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_PhysicsRaycastHit *outHit) {
    return gX_PhysicsWorld_RaycastFiltered(world, origin, direction,
        maxDistance, 0xffffffffu, 1, outHit);
}

gX_I1 gX_PhysicsWorld_RaycastFiltered(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_U32 maskBits, gX_I1 includeSensors,
    gX_PhysicsRaycastHit *outHit) {
    dWorldID w = unwrapWorld(world);
    dSpaceID space = getWorldSpace(w);
    if (!space) return 0;

    //opencode: create temporary ray
    dGeomID ray = dCreateRay(0, (dReal)maxDistance);
    dGeomRaySet(ray,
        (dReal)origin.x, (dReal)origin.y, (dReal)origin.z,
        (dReal)direction.x, (dReal)direction.y, (dReal)direction.z);
    dGeomRaySetClosestHit(ray, 1);

    gX_RaycastData_ rd;
    rd.ray = ray;
    rd.origin = origin;
    rd.closestDist = maxDistance;
    rd.hit = 0;
    rd.hitBody = NULL;
    rd.maskBits = maskBits;
    rd.includeSensors = includeSensors;

    //opencode: collide ray against all geoms in the space
    dSpaceCollide2((dGeomID)space, ray, &rd, &raycastCallback_);

    dGeomDestroy(ray);

    if (outHit) {
        outHit->hit = rd.hit;
        outHit->position = rd.hitPos;
        outHit->normal = rd.hitNormal;
        outHit->distance = rd.closestDist;
        outHit->body = rd.hitBody ? wrapBody(rd.hitBody) : gX_PHYSICS_BODY_NULL;
    }
    return rd.hit ? 1 : 0;
}

// ========================================================================
// Joints
// ========================================================================

gX_PhysicsJoint gX_PhysicsJoint_CreateBall(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateBall(w, 0);
    //opencode: attach BEFORE setting the anchor — ODE's dJointAttach calls
    //   setRelativeValues() which recomputes joint state from the bodies, so
    //   setting the anchor first would leave it overwritten. With the anchor
    //   applied after attach the stored anchor matches the requested world pos.
    dJointAttach(j, unwrapBody(bodyA),
        (bodyB.internal == 0) ? NULL : unwrapBody(bodyB));
    dJointSetBallAnchor(j, (dReal)anchor.x, (dReal)anchor.y, (dReal)anchor.z);
    return wrapJoint(j);
}

gX_PhysicsJoint gX_PhysicsJoint_CreateFixed(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateFixed(w, 0);
    dJointAttach(j, unwrapBody(bodyA),
        (bodyB.internal == 0) ? NULL : unwrapBody(bodyB));
    dJointSetFixed(j);  //opencode: required — locks current relative transform
    return wrapJoint(j);
}

//ai: hinge2 — a hinge with a suspension: axis1 carries the suspension
//   slide (a soft constraint, no travel stops in ODE 0.16) and the
//   steering rotation, axis2 is the wheel spin axis. The group-1 params
//   (SetLimits/SetMotor/SetStops/GetPosition) act on the steering, the *2
//   variants on the spin. ODE needs two real bodies for hinge2, so a NULL
//   bodyB binds to the world's hidden kinematic anchor (the static
//   environment).
gX_PhysicsJoint gX_PhysicsJoint_CreateHinge2(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2) {
    dWorldID w = unwrapWorld(world);
    gX_PhysicsWorldData_ *state = getWorldData(w);
    dJointID j = dJointCreateHinge2(w, 0);
    if (!j) return gX_PHYSICS_JOINT_NULL;
    dBodyID partner = (bodyB.internal == 0)
        ? (state ? state->anchorBody : NULL) : unwrapBody(bodyB);
    //opencode: attach BEFORE anchor/axes — see CreateHinge note.
    dJointAttach(j, unwrapBody(bodyA), partner);
    dJointSetHinge2Anchor(j, (dReal)anchor.x, (dReal)anchor.y, (dReal)anchor.z);
    dReal a1[4] = {(dReal)axis1.x, (dReal)axis1.y, (dReal)axis1.z, 0.0};
    dReal a2[4] = {(dReal)axis2.x, (dReal)axis2.y, (dReal)axis2.z, 0.0};
    dJointSetHinge2Axes(j, a1, a2);
    return wrapJoint(j);
}

//ai: hinge2 specific params — the suspension softness along axis1 and the
//   second-axis (spin) motor/limits.
gX_I0 gX_PhysicsJoint_SetSuspension(gX_PhysicsJoint joint,
    gX_F32 erp, gX_F32 cfm) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeHinge2) return;
    dJointSetHinge2Param(j, dParamSuspensionERP, (dReal)erp);
    dJointSetHinge2Param(j, dParamSuspensionCFM, (dReal)cfm);
}

gX_I0 gX_PhysicsJoint_SetMotor2(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax) {
    dJointID j = unwrapJoint(joint);
    if (!j) return;
    //ai: hinge2's second axis is the wheel spin; the piston's is its
    //   rotation about the slide axis.
    if (dJointGetType(j) == dJointTypeHinge2) {
        dJointSetHinge2Param(j, dParamVel2, (dReal)vel);
        dJointSetHinge2Param(j, dParamFMax2, (dReal)fMax);
    } else if (dJointGetType(j) == dJointTypePiston) {
        dJointSetPistonParam(j, dParamVel2, (dReal)vel);
        dJointSetPistonParam(j, dParamFMax2, (dReal)fMax);
    } else {
        return;
    }
    awakeJoint_(j);
}

gX_I0 gX_PhysicsJoint_SetLimits2(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop) {
    dJointID j = unwrapJoint(joint);
    if (!j) return;
    if (dJointGetType(j) == dJointTypeHinge2) {
        dJointSetHinge2Param(j, dParamLoStop2, (dReal)loStop);
        dJointSetHinge2Param(j, dParamHiStop2, (dReal)hiStop);
    } else if (dJointGetType(j) == dJointTypePiston) {
        dJointSetPistonParam(j, dParamLoStop2, (dReal)loStop);
        dJointSetPistonParam(j, dParamHiStop2, (dReal)hiStop);
    } else {
        return;
    }
    awakeJoint_(j);
}

//ai: joint feedback — once enabled, ODE records the constraint force and
//   torque the solver applied to the joint's first body (bodyA) each step.
//   The wrapper owns the storage; GetFeedback returns false when the joint
//   has no feedback registered.
gX_I0 gX_PhysicsJoint_EnableFeedback(gX_PhysicsJoint joint, gX_I1 enabled) {
    dJointID j = unwrapJoint(joint);
    if (!j) return;
    gX_PhysicsWorldData_ *state = getWorldData(jointWorld_(j));
    if (enabled) {
        feedbackAdd_(state, j);
    } else {
        feedbackRemove_(state, j);
    }
}

gX_I1 gX_PhysicsJoint_GetFeedback(gX_PhysicsJoint joint,
    gX_F32v3 *force, gX_F32v3 *torque) {
    dJointID j = unwrapJoint(joint);
    if (!j) return false;
    gX_PhysicsWorldData_ *state = getWorldData(jointWorld_(j));
    gX_I32 i = feedbackFind_(state, j);
    if (i < 0) return false;
    if (force) {
        *force = (gX_F32v3){(gX_F32)state->fb[i].f1[0],
            (gX_F32)state->fb[i].f1[1], (gX_F32)state->fb[i].f1[2]};
    }
    if (torque) {
        *torque = (gX_F32v3){(gX_F32)state->fb[i].t1[0],
            (gX_F32)state->fb[i].t1[1], (gX_F32)state->fb[i].t1[2]};
    }
    return true;
}

//ai: AMotor — up to three angular axes between two bodies with per-axis
//   limits and motors. `AMotorSetEuler` selects ODE's swing-twist chain
//   (axis A fixed in body A, axis B fixed in body B, the middle axis
//   derived by ODE): the middle angle is the swing away from the reference
//   direction, so it is what ball joints use for cone limits.
//   `AMotorSetAxes` selects user mode (all axes fixed in body A) — what
//   axis motors (spin a body about an axis) use; user-mode stops
//   additionally need AMotorSetAngle, ODE has no angle source there.
gX_PhysicsJoint gX_PhysicsJoint_CreateAMotor(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateAMotor(w, 0);
    if (!j) return gX_PHYSICS_JOINT_NULL;
    gX_PhysicsWorldData_ *state = getWorldData(w);
    dBodyID partner = (bodyB.internal == 0)
        ? (state ? state->anchorBody : NULL) : unwrapBody(bodyB);
    dJointAttach(j, unwrapBody(bodyA), partner);
    dJointSetAMotorMode(j, dAMotorUser);
    dJointSetAMotorNumAxes(j, 1);
    return wrapJoint(j);
}

gX_I0 gX_PhysicsJoint_AMotorSetEuler(gX_PhysicsJoint joint,
    gX_F32v3 axisA, gX_F32v3 axisB) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor) return;
    dJointSetAMotorMode(j, dAMotorEuler);
    dJointSetAMotorNumAxes(j, 3);
    dJointSetAMotorAxis(j, 0, 1, (dReal)axisA.x, (dReal)axisA.y,
        (dReal)axisA.z);
    dJointSetAMotorAxis(j, 2, 2, (dReal)axisB.x, (dReal)axisB.y,
        (dReal)axisB.z);
    awakeJoint_(j);
}

gX_I0 gX_PhysicsJoint_AMotorSetAxes(gX_PhysicsJoint joint, gX_I32 count,
    const gX_F32v3 *axes) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || !axes) return;
    if (count < 1) count = 1;
    if (count > 3) count = 3;
    dJointSetAMotorMode(j, dAMotorUser);
    dJointSetAMotorNumAxes(j, count);
    for (gX_I32 i = 0; i < count; i++) {
        dJointSetAMotorAxis(j, i, 1, (dReal)axes[i].x, (dReal)axes[i].y,
            (dReal)axes[i].z);
    }
    awakeJoint_(j);
}

//ai: per-axis AMotor params (axis 0..2); the parameter offsets are the
//   ODE parameter groups (dParamGroup per axis).
gX_I0 gX_PhysicsJoint_AMotorSetLimits(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 loStop, gX_F32 hiStop) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || axis < 0 || axis > 2) {
        return;
    }
    gX_I32 off = axis * dParamGroup;
    dJointSetAMotorParam(j, dParamLoStop + off, (dReal)loStop);
    dJointSetAMotorParam(j, dParamHiStop + off, (dReal)hiStop);
    awakeJoint_(j);
}

gX_I0 gX_PhysicsJoint_AMotorSetMotor(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 vel, gX_F32 fMax) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || axis < 0 || axis > 2) {
        return;
    }
    gX_I32 off = axis * dParamGroup;
    dJointSetAMotorParam(j, dParamVel + off, (dReal)vel);
    dJointSetAMotorParam(j, dParamFMax + off, (dReal)fMax);
    awakeJoint_(j);
}

gX_I0 gX_PhysicsJoint_AMotorSetAngle(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 angle) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || axis < 0 || axis > 2) {
        return;
    }
    dJointSetAMotorAngle(j, axis, (dReal)angle);
}

gX_F32 gX_PhysicsJoint_AMotorGetAngle(gX_PhysicsJoint joint, gX_I32 axis) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || axis < 0 || axis > 2) {
        return 0.0f;
    }
    return (gX_F32)dJointGetAMotorAngle(j, axis);
}

gX_F32 gX_PhysicsJoint_AMotorGetAngleRate(gX_PhysicsJoint joint,
    gX_I32 axis) {
    dJointID j = unwrapJoint(joint);
    if (!j || dJointGetType(j) != dJointTypeAMotor || axis < 0 || axis > 2) {
        return 0.0f;
    }
    return (gX_F32)dJointGetAMotorAngleRate(j, axis);
}

//ai: piston — a slide along its axis plus a free rotation about it (the
//   screw/drill DOF). The group-1 params drive the slide (SetLimits/
//   SetMotor/GetPosition/GetPositionRate), the *2 variants the rotation
//   (SetMotor2/SetLimits2, GetPosition2/GetPosition2Rate; ODE measures the
//   angle from an arbitrary reference, so use rates/deltas). A NULL bodyB
//   binds to the world's hidden kinematic anchor.
gX_PhysicsJoint gX_PhysicsJoint_CreatePiston(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreatePiston(w, 0);
    if (!j) return gX_PHYSICS_JOINT_NULL;
    gX_PhysicsWorldData_ *state = getWorldData(w);
    dBodyID partner = (bodyB.internal == 0)
        ? (state ? state->anchorBody : NULL) : unwrapBody(bodyB);
    dJointAttach(j, unwrapBody(bodyA), partner);
    dJointSetPistonAnchor(j, (dReal)anchor.x, (dReal)anchor.y,
        (dReal)anchor.z);
    dJointSetPistonAxis(j, (dReal)axis.x, (dReal)axis.y, (dReal)axis.z);
    return wrapJoint(j);
}

gX_PhysicsJoint gX_PhysicsJoint_CreateHinge(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateHinge(w, 0);
    //opencode: attach BEFORE anchor/axis — dJointAttach recomputes joint state
    //   (setRelativeValues), so anchor/axis set earlier would be overwritten.
    //   Also ODE's setAxes only converts world→body-local once the body is
    //   attached, so the axis must be set after attach to be stored correctly.
    dJointAttach(j, unwrapBody(bodyA),
        (bodyB.internal == 0) ? NULL : unwrapBody(bodyB));
    dJointSetHingeAnchor(j, (dReal)anchor.x, (dReal)anchor.y, (dReal)anchor.z);
    dJointSetHingeAxis(j, (dReal)axis.x, (dReal)axis.y, (dReal)axis.z);
    return wrapJoint(j);
}

gX_PhysicsJoint gX_PhysicsJoint_CreateSlider(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 axis) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateSlider(w, 0);
    //opencode: attach BEFORE setting the axis — see CreateHinge note.
    dJointAttach(j, unwrapBody(bodyA),
        (bodyB.internal == 0) ? NULL : unwrapBody(bodyB));
    dJointSetSliderAxis(j, (dReal)axis.x, (dReal)axis.y, (dReal)axis.z);
    return wrapJoint(j);
}

gX_PhysicsJoint gX_PhysicsJoint_CreateUniversal(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2) {
    dWorldID w = unwrapWorld(world);
    dJointID j = dJointCreateUniversal(w, 0);
    //opencode: attach BEFORE anchor/axes — see CreateHinge note.
    dJointAttach(j, unwrapBody(bodyA),
        (bodyB.internal == 0) ? NULL : unwrapBody(bodyB));
    dJointSetUniversalAnchor(j, (dReal)anchor.x, (dReal)anchor.y, (dReal)anchor.z);
    dJointSetUniversalAxis1(j, (dReal)axis1.x, (dReal)axis1.y, (dReal)axis1.z);
    dJointSetUniversalAxis2(j, (dReal)axis2.x, (dReal)axis2.y, (dReal)axis2.z);
    return wrapJoint(j);
}

gX_I0 gX_PhysicsJoint_SetLimits(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop) {
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeHinge:
            dJointSetHingeParam(j, dParamLoStop, (dReal)loStop);
            dJointSetHingeParam(j, dParamHiStop, (dReal)hiStop);
            break;
        case dJointTypeSlider:
            dJointSetSliderParam(j, dParamLoStop, (dReal)loStop);
            dJointSetSliderParam(j, dParamHiStop, (dReal)hiStop);
            break;
        case dJointTypeUniversal:
            dJointSetUniversalParam(j, dParamLoStop, (dReal)loStop);
            dJointSetUniversalParam(j, dParamHiStop, (dReal)hiStop);
            break;
        case dJointTypeHinge2:
            dJointSetHinge2Param(j, dParamLoStop, (dReal)loStop);
            dJointSetHinge2Param(j, dParamHiStop, (dReal)hiStop);
            break;
        case dJointTypeAMotor:
            dJointSetAMotorParam(j, dParamLoStop, (dReal)loStop);
            dJointSetAMotorParam(j, dParamHiStop, (dReal)hiStop);
            break;
        case dJointTypePiston:
            //ai: group 1 = the slide.
            dJointSetPistonParam(j, dParamLoStop, (dReal)loStop);
            dJointSetPistonParam(j, dParamHiStop, (dReal)hiStop);
            break;
        default: break;
    }
}

gX_I0 gX_PhysicsJoint_SetMotor(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax) {
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {        case dJointTypeHinge:
            dJointSetHingeParam(j, dParamVel, (dReal)vel);
            dJointSetHingeParam(j, dParamFMax, (dReal)fMax);
            break;
        case dJointTypeSlider:
            dJointSetSliderParam(j, dParamVel, (dReal)vel);
            dJointSetSliderParam(j, dParamFMax, (dReal)fMax);
            break;
        case dJointTypeUniversal:
            dJointSetUniversalParam(j, dParamVel, (dReal)vel);
            dJointSetUniversalParam(j, dParamFMax, (dReal)fMax);
            break;
        case dJointTypeHinge2:
            dJointSetHinge2Param(j, dParamVel, (dReal)vel);
            dJointSetHinge2Param(j, dParamFMax, (dReal)fMax);
            break;
        case dJointTypeAMotor:
            dJointSetAMotorParam(j, dParamVel, (dReal)vel);
            dJointSetAMotorParam(j, dParamFMax, (dReal)fMax);
            break;
        case dJointTypePiston:
            //ai: group 1 = the slide motor.
            dJointSetPistonParam(j, dParamVel, (dReal)vel);
            dJointSetPistonParam(j, dParamFMax, (dReal)fMax);
            break;
        default: break;
    }
    awakeJoint_(j);
}

gX_I0 gX_PhysicsJoint_SetStops(gX_PhysicsJoint joint,
    gX_F32 bounce, gX_F32 cfm, gX_F32 erp) {
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeHinge:
            dJointSetHingeParam(j, dParamBounce, (dReal)bounce);
            dJointSetHingeParam(j, dParamStopCFM, (dReal)cfm);
            dJointSetHingeParam(j, dParamStopERP, (dReal)erp);
            break;
        case dJointTypeSlider:
            dJointSetSliderParam(j, dParamBounce, (dReal)bounce);
            dJointSetSliderParam(j, dParamStopCFM, (dReal)cfm);
            dJointSetSliderParam(j, dParamStopERP, (dReal)erp);
            break;
        case dJointTypeUniversal:
            dJointSetUniversalParam(j, dParamBounce, (dReal)bounce);
            dJointSetUniversalParam(j, dParamStopCFM, (dReal)cfm);
            dJointSetUniversalParam(j, dParamStopERP, (dReal)erp);
            break;
        case dJointTypeHinge2:
            dJointSetHinge2Param(j, dParamBounce, (dReal)bounce);
            dJointSetHinge2Param(j, dParamStopCFM, (dReal)cfm);
            dJointSetHinge2Param(j, dParamStopERP, (dReal)erp);
            break;
        default: break;
    }
}

gX_I0 gX_PhysicsJoint_Destroy(gX_PhysicsWorld world, gX_PhysicsJoint joint) {
    dJointID j = unwrapJoint(joint);
    if (!j) return;
    feedbackRemove_(getWorldData(unwrapWorld(world)), j);
    dJointDestroy(j);
}

gX_F32 gX_PhysicsJoint_GetPosition(gX_PhysicsJoint joint) {
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeBall:       return 0.0f;
        case dJointTypeFixed:      return 0.0f;
        case dJointTypeHinge:      return (gX_F32)dJointGetHingeAngle(j);
        case dJointTypeSlider:     return (gX_F32)dJointGetSliderPosition(j);
        case dJointTypeUniversal:  return (gX_F32)dJointGetUniversalAngle1(j);
        case dJointTypeHinge2:     return (gX_F32)dJointGetHinge2Angle1(j);
        case dJointTypePiston:     return (gX_F32)dJointGetPistonPosition(j);
        default: return 0.0f;
    }
}

//ai: hinge2 second-axis (spin) and piston rotation queries; 0 for other
//   joint types.
gX_F32   gX_PhysicsJoint_GetPosition2(gX_PhysicsJoint joint) {
    dJointID j = unwrapJoint(joint);
    if (!j) return 0.0f;
    if (dJointGetType(j) == dJointTypeHinge2) {
        return (gX_F32)dJointGetHinge2Angle2(j);
    }
    if (dJointGetType(j) == dJointTypePiston) {
        return (gX_F32)dJointGetPistonAngle(j);
    }
    return 0.0f;
}

gX_F32 gX_PhysicsJoint_GetPosition2Rate(gX_PhysicsJoint joint) {
    dJointID j = unwrapJoint(joint);
    if (!j) return 0.0f;
    if (dJointGetType(j) == dJointTypeHinge2) {
        return (gX_F32)dJointGetHinge2Angle2Rate(j);
    }
    if (dJointGetType(j) == dJointTypePiston) {
        return (gX_F32)dJointGetPistonAngleRate(j);
    }
    return 0.0f;
}

gX_F32 gX_PhysicsJoint_GetPositionRate(gX_PhysicsJoint joint) {
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeHinge:      return (gX_F32)dJointGetHingeAngleRate(j);
        case dJointTypeSlider:     return (gX_F32)dJointGetSliderPositionRate(j);
        case dJointTypeUniversal:  return (gX_F32)dJointGetUniversalAngle1Rate(j);
        case dJointTypeHinge2:     return (gX_F32)dJointGetHinge2Angle1Rate(j);
        case dJointTypePiston:     return (gX_F32)dJointGetPistonPositionRate(j);
        default: return 0.0f;
    }
}

gX_F32v3 gX_PhysicsJoint_GetAnchor(gX_PhysicsJoint joint) {
    dVector3 anchor;
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeBall:
            dJointGetBallAnchor(j, anchor);
            break;
        case dJointTypeHinge:
            dJointGetHingeAnchor(j, anchor);
            break;
        case dJointTypeUniversal:
            dJointGetUniversalAnchor(j, anchor);
            break;
        case dJointTypeHinge2:
            dJointGetHinge2Anchor(j, anchor);
            break;
        case dJointTypePiston:
            dJointGetPistonAnchor(j, anchor);
            break;
        default:
            anchor[0] = 0; anchor[1] = 0; anchor[2] = 0;
            break;
    }
    return (gX_F32v3){ (gX_F32)anchor[0], (gX_F32)anchor[1], (gX_F32)anchor[2] };
}

gX_F32v3 gX_PhysicsJoint_GetAxis(gX_PhysicsJoint joint) {
    dVector3 axis;
    dJointID j = unwrapJoint(joint);
    gX_I32 type = (gX_I32)dJointGetType(j);
    switch (type) {
        case dJointTypeHinge:
            dJointGetHingeAxis(j, axis);
            break;
        case dJointTypeSlider:
            dJointGetSliderAxis(j, axis);
            break;
        case dJointTypeUniversal:
            dJointGetUniversalAxis1(j, axis);
            break;
        case dJointTypeHinge2:
            dJointGetHinge2Axis1(j, axis);
            break;
        case dJointTypePiston:
            dJointGetPistonAxis(j, axis);
            break;
        default:
            axis[0] = 0; axis[1] = 0; axis[2] = 0;
            break;
    }
    return (gX_F32v3){ (gX_F32)axis[0], (gX_F32)axis[1], (gX_F32)axis[2] };
}

#else
// ========================================================================
// Stub implementations when ODE is not available
// ========================================================================

#include <string.h>

//opencode: default material stub when ODE is disabled.
gX_PhysicsMaterial gX_PhysicsMaterial_Default(gX_I0) {
    return (gX_PhysicsMaterial){0.8f, 0.1f, 0.0f, 0.0f};
}

gX_PhysicsWorld gX_PhysicsWorld_Create(gX_I0) {
    return gX_PHYSICS_WORLD_NULL;
}
gX_I0 gX_PhysicsWorld_Destroy(gX_PhysicsWorld world) { (void)world; }
gX_I0 gX_PhysicsWorld_SetGravity(gX_PhysicsWorld world, gX_F32v3 gravity) {
    (void)world; (void)gravity;
}
gX_I0 gX_PhysicsWorld_SetSolverParams(gX_PhysicsWorld world, gX_F32 cfm, gX_F32 erp) {
    (void)world; (void)cfm; (void)erp;
}
gX_I0 gX_PhysicsWorld_Step(gX_PhysicsWorld world, gX_F32 dt) { (void)world; (void)dt; }
gX_I0 gX_PhysicsWorld_QuickStep(gX_PhysicsWorld world, gX_F32 dt) { (void)world; (void)dt; }
gX_I0 gX_PhysicsWorld_SetCollisionCallback(gX_PhysicsWorld world,
    gX_PhysicsCollisionCB cb, gX_I0 *userData) {
    (void)world; (void)cb; (void)userData;
}
gX_I0 gX_PhysicsWorld_SetEventCallback(gX_PhysicsWorld world,
    gX_PhysicsEventCB cb, gX_I0 *userData) {
    (void)world; (void)cb; (void)userData;
}

gX_PhysicsBody gX_PhysicsBody_AddBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)size; (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsBody gX_PhysicsBody_AddSphere(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)radius; (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsBody gX_PhysicsBody_AddCapsule(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)radius; (void)length; (void)direction; (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsBody gX_PhysicsBody_AddCylinder(gX_PhysicsWorld world,
    gX_F32 radius, gX_F32 length, gX_I32 direction,
    gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)radius; (void)length; (void)direction; (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsBody gX_PhysicsBody_AddConvex(gX_PhysicsWorld world,
    const gX_F32 *planes, gX_U32 planeCount,
    const gX_F32 *points, gX_U32 pointCount,
    const gX_U32 *polygons,
    gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)planes; (void)planeCount;
    (void)points; (void)pointCount; (void)polygons;
    (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsBody gX_PhysicsBody_AddTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32 mass, gX_F32v3 position) {
    (void)world; (void)vertices; (void)vertexCount;
    (void)indices; (void)indexCount;
    (void)mass; (void)position;
    return gX_PHYSICS_BODY_NULL;
}
gX_PhysicsCollider gX_PhysicsWorld_AddStaticBox(gX_PhysicsWorld world,
    gX_F32v3 size, gX_F32v3 position) {
    (void)world; (void)size; (void)position;
    return gX_PHYSICS_COLLIDER_NULL;
}
gX_PhysicsCollider gX_PhysicsWorld_AddStaticTriMesh(gX_PhysicsWorld world,
    const gX_F32 *vertices, gX_U32 vertexCount,
    const gX_U32 *indices, gX_U32 indexCount,
    gX_F32v3 position) {
    (void)world; (void)vertices; (void)vertexCount;
    (void)indices; (void)indexCount; (void)position;
    return gX_PHYSICS_COLLIDER_NULL;
}
gX_PhysicsCollider gX_PhysicsWorld_AddGroundPlane(gX_PhysicsWorld world,
    gX_F32v3 normal, gX_F32 dist) {
    (void)world; (void)normal; (void)dist;
    return gX_PHYSICS_COLLIDER_NULL;
}
gX_PhysicsCollider gX_PhysicsWorld_AddHeightfield(gX_PhysicsWorld world,
    const gX_F32 *heights, gX_I32 widthSamples, gX_I32 depthSamples,
    gX_F32 width, gX_F32 depth, gX_F32 heightScale, gX_F32v3 position) {
    (void)world; (void)heights; (void)widthSamples; (void)depthSamples;
    (void)width; (void)depth; (void)heightScale; (void)position;
    return gX_PHYSICS_COLLIDER_NULL;
}
gX_I0 gX_PhysicsBody_Destroy(gX_PhysicsWorld world, gX_PhysicsBody body) {
    (void)world; (void)body;
}
gX_I0 gX_PhysicsCollider_Destroy(gX_PhysicsWorld world,
    gX_PhysicsCollider collider) { (void)world; (void)collider; }
gX_F32v3 gX_PhysicsBody_GetPosition(gX_PhysicsBody body) { (void)body; return (gX_F32v3){0,0,0}; }
gX_I0 gX_PhysicsBody_SetPosition(gX_PhysicsBody body, gX_F32v3 pos) {
    (void)body; (void)pos;
}
gX_I0 gX_PhysicsBody_GetRotation(gX_PhysicsBody body, gX_F32Qua *outQuat) {
    (void)body; if (outQuat) memset(outQuat, 0, sizeof(*outQuat));
}
gX_I0 gX_PhysicsBody_SetRotation(gX_PhysicsBody body, gX_F32Qua quat) {
    (void)body; (void)quat;
}
gX_I0 gX_PhysicsBody_SetType(gX_PhysicsBody body, gX_PhysicsBodyType type) {
    (void)body; (void)type;
}
gX_PhysicsBodyType gX_PhysicsBody_GetType(gX_PhysicsBody body) {
    (void)body; return gX_PHYSICS_BODY_DYNAMIC;
}
gX_I1 gX_PhysicsBody_IsEnabled(gX_PhysicsBody body) { (void)body; return 0; }
gX_I0 gX_PhysicsBody_Enable(gX_PhysicsBody body) { (void)body; }
gX_I0 gX_PhysicsBody_SetAutoDisable(gX_PhysicsBody body, gX_I1 enabled) {
    (void)body; (void)enabled;
}
gX_I1 gX_PhysicsBody_GetAutoDisable(gX_PhysicsBody body) { (void)body; return 0; }
gX_I0 gX_PhysicsBody_Disable(gX_PhysicsBody body) { (void)body; }
gX_F32v3 gX_PhysicsBody_GetLinearVel(gX_PhysicsBody body) {
    (void)body; return (gX_F32v3){0,0,0};
}
gX_F32v3 gX_PhysicsBody_GetAngularVel(gX_PhysicsBody body) {
    (void)body; return (gX_F32v3){0,0,0};
}
gX_I0 gX_PhysicsBody_SetLinearVel(gX_PhysicsBody body, gX_F32v3 vel) {
    (void)body; (void)vel;
}
gX_I0 gX_PhysicsBody_SetAngularVel(gX_PhysicsBody body, gX_F32v3 vel) {
    (void)body; (void)vel;
}
gX_F32 gX_PhysicsBody_GetMass(gX_PhysicsBody body) { (void)body; return 0.0f; }
gX_I0 gX_PhysicsBody_SetMass(gX_PhysicsBody body, gX_F32 mass) {
    (void)body; (void)mass;
}
gX_I0 gX_PhysicsBody_SetMassCenter(gX_PhysicsBody body, gX_F32v3 offset) {
    (void)body; (void)offset;
}
gX_F32v3 gX_PhysicsBody_GetMassCenter(gX_PhysicsBody body) {
    (void)body; return (gX_F32v3){0.0f, 0.0f, 0.0f};
}
gX_F32v3 gX_PhysicsBody_GetInertia(gX_PhysicsBody body) {
    (void)body; return (gX_F32v3){0.0f, 0.0f, 0.0f};
}
gX_I0 gX_PhysicsBody_SetDamping(gX_PhysicsBody body,
    gX_F32 linear, gX_F32 angular) { (void)body; (void)linear; (void)angular; }
gX_I0 gX_PhysicsBody_SetMaterial(gX_PhysicsBody body,
    gX_PhysicsMaterial material) { (void)body; (void)material; }
gX_I0 gX_PhysicsBody_SetCollisionFilter(gX_PhysicsBody body,
    gX_U32 categoryBits, gX_U32 maskBits) {
    (void)body; (void)categoryBits; (void)maskBits;
}
gX_I0 gX_PhysicsBody_SetSensor(gX_PhysicsBody body, gX_I1 sensor) {
    (void)body; (void)sensor;
}
gX_I0 gX_PhysicsBody_SetUserTag(gX_PhysicsBody body, gX_U64 tag) {
    (void)body; (void)tag;
}
gX_U64 gX_PhysicsBody_GetUserTag(gX_PhysicsBody body) { (void)body; return 0; }
gX_I0 gX_PhysicsCollider_SetMaterial(gX_PhysicsCollider collider,
    gX_PhysicsMaterial material) { (void)collider; (void)material; }
gX_I0 gX_PhysicsCollider_SetCollisionFilter(gX_PhysicsCollider collider,
    gX_U32 categoryBits, gX_U32 maskBits) {
    (void)collider; (void)categoryBits; (void)maskBits;
}
gX_I0 gX_PhysicsCollider_SetSensor(gX_PhysicsCollider collider, gX_I1 sensor) {
    (void)collider; (void)sensor;
}
gX_I0 gX_PhysicsCollider_SetUserTag(gX_PhysicsCollider collider, gX_U64 tag) {
    (void)collider; (void)tag;
}
gX_U64 gX_PhysicsCollider_GetUserTag(gX_PhysicsCollider collider) {
    (void)collider; return 0;
}
gX_I0 gX_PhysicsBody_ApplyForce(gX_PhysicsBody body, gX_F32v3 force) {
    (void)body; (void)force;
}
gX_I0 gX_PhysicsBody_ApplyTorque(gX_PhysicsBody body, gX_F32v3 torque) {
    (void)body; (void)torque;
}
gX_I0 gX_PhysicsBody_ApplyForceAtPos(gX_PhysicsBody body,
    gX_F32v3 force, gX_F32v3 pos) {
    (void)body; (void)force; (void)pos;
}

gX_I0 gX_PhysicsWorld_EmptyContactGroup(gX_PhysicsWorld world) { (void)world; }
gX_I0 gX_PhysicsWorld_SetMaxContacts(gX_PhysicsWorld world, gX_I32 count) {
    (void)world; (void)count;
}
gX_I0 gX_PhysicsWorld_SetThreading(gX_PhysicsWorld world, gX_I1 enable) { (void)world; (void)enable; }

//opencode: spatial query stubs
gX_I32 gX_PhysicsWorld_GetBodyCount(gX_PhysicsWorld world) { (void)world; return 0; }
gX_PhysicsBody gX_PhysicsWorld_GetBodyByIndex(gX_PhysicsWorld world, gX_I32 index) {
    (void)world; (void)index; return gX_PHYSICS_BODY_NULL;
}

//opencode: raycast stub
gX_I1 gX_PhysicsWorld_Raycast(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_PhysicsRaycastHit *outHit) {
    (void)world; (void)origin; (void)direction; (void)maxDistance;
    if (outHit) { outHit->hit = 0; outHit->body = gX_PHYSICS_BODY_NULL; }
    return 0;
}
gX_I1 gX_PhysicsWorld_RaycastFiltered(gX_PhysicsWorld world,
    gX_F32v3 origin, gX_F32v3 direction, gX_F32 maxDistance,
    gX_U32 maskBits, gX_I1 includeSensors,
    gX_PhysicsRaycastHit *outHit) {
    (void)world; (void)origin; (void)direction; (void)maxDistance;
    (void)maskBits; (void)includeSensors;
    if (outHit) { outHit->hit = 0; outHit->body = gX_PHYSICS_BODY_NULL; }
    return 0;
}

//opencode: joint stubs
gX_PhysicsJoint gX_PhysicsJoint_CreateBall(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB, gX_F32v3 anchor) {
    (void)world; (void)bodyA; (void)bodyB; (void)anchor;
    return gX_PHYSICS_JOINT_NULL;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateFixed(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB) {
    (void)world; (void)bodyA; (void)bodyB;
    return gX_PHYSICS_JOINT_NULL;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateHinge2(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2) {
    (void)world; (void)bodyA; (void)bodyB; (void)anchor;
    (void)axis1; (void)axis2;
    return gX_PHYSICS_JOINT_NULL;
}
gX_I0 gX_PhysicsJoint_SetSuspension(gX_PhysicsJoint joint,
    gX_F32 erp, gX_F32 cfm) {
    (void)joint; (void)erp; (void)cfm;
}
gX_I0 gX_PhysicsJoint_SetMotor2(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax) {
    (void)joint; (void)vel; (void)fMax;
}
gX_I0 gX_PhysicsJoint_SetLimits2(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop) {
    (void)joint; (void)loStop; (void)hiStop;
}
gX_I0 gX_PhysicsJoint_EnableFeedback(gX_PhysicsJoint joint, gX_I1 enabled) {
    (void)joint; (void)enabled;
}
gX_I1 gX_PhysicsJoint_GetFeedback(gX_PhysicsJoint joint,
    gX_F32v3 *force, gX_F32v3 *torque) {
    (void)joint; (void)force; (void)torque;
    return false;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateAMotor(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB) {
    (void)world; (void)bodyA; (void)bodyB;
    return gX_PHYSICS_JOINT_NULL;
}
gX_I0 gX_PhysicsJoint_AMotorSetEuler(gX_PhysicsJoint joint,
    gX_F32v3 axisA, gX_F32v3 axisB) {
    (void)joint; (void)axisA; (void)axisB;
}
gX_I0 gX_PhysicsJoint_AMotorSetAxes(gX_PhysicsJoint joint, gX_I32 count,
    const gX_F32v3 *axes) {
    (void)joint; (void)count; (void)axes;
}
gX_I0 gX_PhysicsJoint_AMotorSetLimits(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 loStop, gX_F32 hiStop) {
    (void)joint; (void)axis; (void)loStop; (void)hiStop;
}
gX_I0 gX_PhysicsJoint_AMotorSetMotor(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 vel, gX_F32 fMax) {
    (void)joint; (void)axis; (void)vel; (void)fMax;
}
gX_I0 gX_PhysicsJoint_AMotorSetAngle(gX_PhysicsJoint joint, gX_I32 axis,
    gX_F32 angle) {
    (void)joint; (void)axis; (void)angle;
}
gX_F32 gX_PhysicsJoint_AMotorGetAngle(gX_PhysicsJoint joint, gX_I32 axis) {
    (void)joint; (void)axis;
    return 0.0f;
}
gX_F32 gX_PhysicsJoint_AMotorGetAngleRate(gX_PhysicsJoint joint,
    gX_I32 axis) {
    (void)joint; (void)axis;
    return 0.0f;
}
gX_PhysicsJoint gX_PhysicsJoint_CreatePiston(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis) {
    (void)world; (void)bodyA; (void)bodyB; (void)anchor; (void)axis;
    return gX_PHYSICS_JOINT_NULL;
}
gX_F32 gX_PhysicsJoint_GetPosition2(gX_PhysicsJoint joint) {
    (void)joint; return 0.0f;
}
gX_F32 gX_PhysicsJoint_GetPosition2Rate(gX_PhysicsJoint joint) {
    (void)joint; return 0.0f;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateHinge(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis) {
    (void)world; (void)bodyA; (void)bodyB; (void)anchor; (void)axis;
    return gX_PHYSICS_JOINT_NULL;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateSlider(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 axis) {
    (void)world; (void)bodyA; (void)bodyB; (void)axis;
    return gX_PHYSICS_JOINT_NULL;
}
gX_PhysicsJoint gX_PhysicsJoint_CreateUniversal(gX_PhysicsWorld world,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_F32v3 anchor, gX_F32v3 axis1, gX_F32v3 axis2) {
    (void)world; (void)bodyA; (void)bodyB; (void)anchor; (void)axis1; (void)axis2;
    return gX_PHYSICS_JOINT_NULL;
}
gX_I0 gX_PhysicsJoint_SetLimits(gX_PhysicsJoint joint,
    gX_F32 loStop, gX_F32 hiStop) {
    (void)joint; (void)loStop; (void)hiStop;
}
gX_I0 gX_PhysicsJoint_SetMotor(gX_PhysicsJoint joint,
    gX_F32 vel, gX_F32 fMax) {
    (void)joint; (void)vel; (void)fMax;
}
gX_I0 gX_PhysicsJoint_SetStops(gX_PhysicsJoint joint,
    gX_F32 bounce, gX_F32 cfm, gX_F32 erp) {
    (void)joint; (void)bounce; (void)cfm; (void)erp;
}
gX_I0 gX_PhysicsJoint_Destroy(gX_PhysicsWorld world, gX_PhysicsJoint joint) {
    (void)world; (void)joint;
}
gX_F32 gX_PhysicsJoint_GetPosition(gX_PhysicsJoint joint) { (void)joint; return 0.0f; }
gX_F32 gX_PhysicsJoint_GetPositionRate(gX_PhysicsJoint joint) { (void)joint; return 0.0f; }
gX_F32v3 gX_PhysicsJoint_GetAnchor(gX_PhysicsJoint joint) { (void)joint; return (gX_F32v3){0,0,0}; }
gX_F32v3 gX_PhysicsJoint_GetAxis(gX_PhysicsJoint joint) { (void)joint; return (gX_F32v3){0,0,0}; }

#endif // GX_USE_ODE
