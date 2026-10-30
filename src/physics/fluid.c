/**********************************************************************************************
*
*   [gX: src/physics/fluid.c]
*   Fluid applier — a still-water region in the shared physics world
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


#include "fluid.h"

#include <math.h>

//ai: fixed registry — no allocation, walked in registration order so the
//   force application (and the step results) stay deterministic.
typedef struct gX_PhysicsFluid_Body_ {
    gX_PhysicsBody body;
    gX_F32 radius;      //ai: equivalent-volume sphere radius
    gX_F32 volume;      //ai: full volume (buoyancy scale)
    gX_F32 area;        //ai: drag reference cross-section
    gX_I1 submerged;    //ai: last apply's submerged state (entry detection)
} gX_PhysicsFluid_Body_;

static gX_PhysicsFluid_Body_ g_fluidBodies[GX_PHYSICS_FLUID_MAX_BODIES];
static gX_I32 g_fluidCount = 0;
static gX_I1 g_fluidEnabled = 0;
static gX_PhysicsFluid_Settings g_fluidSettings = {
    0.0f, 1.0f, 9.81f, 1.1f, 1.5f, 0.8f,
    -1000.0f, 1000.0f, -1000.0f, 1000.0f, -1000.0f
};
static gX_PhysicsFluid_Event g_fluidEvents[GX_PHYSICS_FLUID_MAX_EVENTS];
static gX_I32 g_fluidEventHead = 0;
static gX_I32 g_fluidEventCount = 0;

gX_PhysicsFluid_Settings gX_PhysicsFluid_Defaults(gX_I0) {
    gX_PhysicsFluid_Settings defaults = {
        0.0f, 1.0f, 9.81f, 1.1f, 1.5f, 0.8f,
        -1000.0f, 1000.0f, -1000.0f, 1000.0f, -1000.0f
    };
    return defaults;
}

gX_I0 gX_PhysicsFluid_Reset(gX_I0) {
    for (gX_I32 i = 0; i < g_fluidCount; i++) {
        g_fluidBodies[i].body = gX_PHYSICS_BODY_NULL;
        g_fluidBodies[i].submerged = 0;
    }
    g_fluidCount = 0;
    g_fluidEnabled = 0;
    g_fluidSettings = gX_PhysicsFluid_Defaults();
    g_fluidEventHead = 0;
    g_fluidEventCount = 0;
}

gX_I0 gX_PhysicsFluid_SetEnabled(gX_I1 enabled) {
    g_fluidEnabled = enabled ? 1 : 0;
}

gX_I1 gX_PhysicsFluid_Enabled(gX_I0) {
    return g_fluidEnabled;
}

gX_I0 gX_PhysicsFluid_SetSettings(gX_PhysicsFluid_Settings settings) {
    if (settings.density < 0.0f) settings.density = 0.0f;
    if (settings.gravity < 0.0f) settings.gravity = 0.0f;
    if (settings.dragCoefficient < 0.0f) settings.dragCoefficient = 0.0f;
    if (settings.linearDrag < 0.0f) settings.linearDrag = 0.0f;
    if (settings.angularDrag < 0.0f) settings.angularDrag = 0.0f;
    g_fluidSettings = settings;
}

gX_PhysicsFluid_Settings gX_PhysicsFluid_GetSettings(gX_I0) {
    return g_fluidSettings;
}

gX_I32 gX_PhysicsFluid_AddBody(gX_PhysicsBody body, gX_F32 radius) {
    if (body.internal == 0 || radius <= 0.0f) return -1;
    gX_F32 pi = 3.14159265f;
    gX_F32 volume = (4.0f / 3.0f) * pi * radius * radius * radius;
    gX_F32 area = pi * radius * radius;
    for (gX_I32 i = 0; i < g_fluidCount; i++) {
        if (g_fluidBodies[i].body.internal != body.internal) continue;
        g_fluidBodies[i].radius = radius;
        g_fluidBodies[i].volume = volume;
        g_fluidBodies[i].area = area;
        return i;
    }
    if (g_fluidCount >= GX_PHYSICS_FLUID_MAX_BODIES) return -1;
    g_fluidBodies[g_fluidCount].body = body;
    g_fluidBodies[g_fluidCount].radius = radius;
    g_fluidBodies[g_fluidCount].volume = volume;
    g_fluidBodies[g_fluidCount].area = area;
    g_fluidBodies[g_fluidCount].submerged = 0;
    g_fluidCount++;
    return g_fluidCount - 1;
}

gX_I0 gX_PhysicsFluid_RemoveBody(gX_PhysicsBody body) {
    for (gX_I32 i = 0; i < g_fluidCount; i++) {
        if (g_fluidBodies[i].body.internal != body.internal) continue;
        gX_I32 last = g_fluidCount - 1;
        if (i != last) g_fluidBodies[i] = g_fluidBodies[last];
        g_fluidBodies[last].body = gX_PHYSICS_BODY_NULL;
        g_fluidBodies[last].submerged = 0;
        g_fluidCount = last;
        return;
    }
}

gX_I32 gX_PhysicsFluid_Count(gX_I0) {
    return g_fluidCount;
}

//ai: spherical cap volume and its centroid offset below the sphere centre
//   (toward the bottom pole); h is the submerged cap height.
static gX_F32 fluidSphereCap_(gX_F32 radius, gX_F32 h, gX_F32 *offset) {
    gX_F32 pi = 3.14159265f;
    if (h <= 0.0f) {
        if (offset) *offset = 0.0f;
        return 0.0f;
    }
    if (h >= 2.0f * radius) {
        if (offset) *offset = 0.0f;
        return (4.0f / 3.0f) * pi * radius * radius * radius;
    }
    gX_F32 cap = pi * h * h * (3.0f * radius - h) / 3.0f;
    if (offset) {
        gX_F32 base = 2.0f * radius - h;
        *offset = 3.0f * base * base / (4.0f * (3.0f * radius - h));
    }
    return cap;
}

static gX_I0 fluidEventPush_(gX_F32v3 position, gX_F32 speed) {
    if (g_fluidEventCount >= GX_PHYSICS_FLUID_MAX_EVENTS) return;
    gX_I32 slot = (g_fluidEventHead + g_fluidEventCount)
        % GX_PHYSICS_FLUID_MAX_EVENTS;
    g_fluidEvents[slot].position = position;
    g_fluidEvents[slot].speed = speed;
    g_fluidEventCount++;
}

gX_I0 gX_PhysicsFluid_Apply(gX_I0) {
    if (!g_fluidEnabled || g_fluidCount == 0) return;
    gX_PhysicsFluid_Settings settings = g_fluidSettings;
    for (gX_I32 i = 0; i < g_fluidCount; i++) {
        gX_PhysicsFluid_Body_ *entry = &g_fluidBodies[i];
        if (entry->body.internal == 0) continue;
        gX_F32v3 center = gX_PhysicsBody_GetPosition(entry->body);
        gX_I1 inside = center.x >= settings.minX && center.x <= settings.maxX
            && center.z >= settings.minZ && center.z <= settings.maxZ;
        gX_F32 submerged = 0.0f;
        gX_F32v3 centroid = center;
        if (inside) {
            gX_F32 radius = entry->radius;
            gX_F32 offset = 0.0f;
            submerged = fluidSphereCap_(radius,
                (settings.surfaceY - center.y) + radius, &offset);
            centroid.y = center.y - offset;
        }
        gX_I1 wet = submerged > 1e-6f;
        if (!entry->submerged && wet) {
            gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(entry->body);
            gX_F32 speed = -velocity.y;
            if (speed < 0.0f) speed = 0.0f;
            fluidEventPush_((gX_F32v3){center.x, settings.surfaceY, center.z},
                speed);
        }
        entry->submerged = wet;
        if (!wet) continue;

        //ai: buoyancy at the centre of buoyancy (the submerged cap centroid).
        gX_F32 buoyancy = settings.density * settings.gravity * submerged;
        gX_PhysicsBody_ApplyForceAtPos(entry->body,
            (gX_F32v3){0.0f, buoyancy, 0.0f}, centroid);

        //ai: drag scales with the submerged fraction; the linear term damps
        //   slow bobbing, the quadratic term sets the sink terminal speed.
        gX_F32 fraction = submerged / entry->volume;
        gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(entry->body);
        gX_F32 speed2 = velocity.x * velocity.x + velocity.y * velocity.y
            + velocity.z * velocity.z;
        gX_F32 speed = sqrtf(speed2);
        gX_F32 scale = settings.linearDrag * settings.density * submerged
            + 0.5f * settings.dragCoefficient * settings.density
                * entry->area * fraction * speed;
        gX_PhysicsBody_ApplyForce(entry->body, (gX_F32v3){
            -scale * velocity.x, -scale * velocity.y, -scale * velocity.z });

        gX_F32v3 spin = gX_PhysicsBody_GetAngularVel(entry->body);
        if (spin.x != 0.0f || spin.y != 0.0f || spin.z != 0.0f) {
            gX_F32 torque = settings.angularDrag * settings.density * submerged;
            gX_PhysicsBody_ApplyTorque(entry->body, (gX_F32v3){
                -torque * spin.x, -torque * spin.y, -torque * spin.z });
        }
    }
}

gX_I32 gX_PhysicsFluid_EventCount(gX_I0) {
    return g_fluidEventCount;
}

gX_I1 gX_PhysicsFluid_EventGet(gX_I32 index, gX_PhysicsFluid_Event *out) {
    if (index < 0 || index >= g_fluidEventCount || !out) return 0;
    *out = g_fluidEvents[(g_fluidEventHead + index) % GX_PHYSICS_FLUID_MAX_EVENTS];
    return 1;
}

gX_I0 gX_PhysicsFluid_EventClear(gX_I0) {
    g_fluidEventHead = 0;
    g_fluidEventCount = 0;
}
