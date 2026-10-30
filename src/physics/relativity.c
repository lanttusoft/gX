/**********************************************************************************************
*
*   [gX: src/physics/relativity.c]
*   Special-relativity applier — a tunable light speed for the sim
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


#include "relativity.h"

#include <math.h>

#include "../gxmath/physics.h"
#include "../gxmath/vector3.h"

//ai: fixed registry — no allocation, walked in registration order; the
//   applier is per-body, so the order never changes the results.
typedef struct gX_PhysicsRelativity_Body_ {
    gX_PhysicsBody body;
    gX_F32 restMass;
    gX_F32 appliedMass;   //ai: the mass last pushed into the solver
    gX_F32 beta;          //ai: last measured |v|/c
    gX_F32 gamma;         //ai: last measured gamma
} gX_PhysicsRelativity_Body_;

static gX_PhysicsRelativity_Body_ g_relBodies[GX_PHYSICS_RELATIVITY_MAX_BODIES];
static gX_I32 g_relCount = 0;
static gX_I1 g_relEnabled = 0;
static gX_PhysicsRelativity_Settings g_relSettings = { 12.0f, 1.0e-3f,
    0.01f };

gX_PhysicsRelativity_Settings gX_PhysicsRelativity_Defaults(gX_I0) {
    gX_PhysicsRelativity_Settings defaults = { 12.0f, 1.0e-3f, 0.01f };
    return defaults;
}

gX_I0 gX_PhysicsRelativity_Reset(gX_I0) {
    for (gX_I32 i = 0; i < g_relCount; i++) {
        g_relBodies[i].body = gX_PHYSICS_BODY_NULL;
        g_relBodies[i].restMass = 0.0f;
        g_relBodies[i].appliedMass = 0.0f;
        g_relBodies[i].beta = 0.0f;
        g_relBodies[i].gamma = 1.0f;
    }
    g_relCount = 0;
    g_relEnabled = 0;
    g_relSettings = gX_PhysicsRelativity_Defaults();
}

gX_I0 gX_PhysicsRelativity_SetEnabled(gX_I1 enabled) {
    gX_I1 next = enabled ? 1 : 0;
    if (g_relEnabled && !next) {
        //ai: leaving the layer restores every registered body's rest mass —
        //   a stale dilated mass would otherwise keep affecting the world.
        for (gX_I32 i = 0; i < g_relCount; i++) {
            gX_PhysicsRelativity_Body_ *entry = &g_relBodies[i];
            if (entry->body.internal == 0) continue;
            if (entry->appliedMass == entry->restMass) continue;
            if (gX_PhysicsBody_GetType(entry->body)
                != gX_PHYSICS_BODY_DYNAMIC) {
                continue;
            }
            gX_PhysicsBody_SetMass(entry->body, entry->restMass);
            entry->appliedMass = entry->restMass;
            entry->beta = 0.0f;
            entry->gamma = 1.0f;
        }
    }
    g_relEnabled = next;
}

gX_I1 gX_PhysicsRelativity_Enabled(gX_I0) {
    return g_relEnabled;
}

gX_I0 gX_PhysicsRelativity_SetSettings(
    gX_PhysicsRelativity_Settings settings) {
    if (settings.lightSpeed < 0.1f) settings.lightSpeed = 0.1f;
    if (settings.clampMargin < 1.0e-6f) settings.clampMargin = 1.0e-6f;
    if (settings.clampMargin > 0.5f) settings.clampMargin = 0.5f;
    if (settings.massUpdateStep < 0.0001f) settings.massUpdateStep = 0.0001f;
    if (settings.massUpdateStep > 0.5f) settings.massUpdateStep = 0.5f;
    g_relSettings = settings;
}

gX_PhysicsRelativity_Settings gX_PhysicsRelativity_GetSettings(gX_I0) {
    return g_relSettings;
}

gX_I32 gX_PhysicsRelativity_AddBody(gX_PhysicsBody body, gX_F32 restMass) {
    if (body.internal == 0) return -1;
    if (restMass <= 0.0f) restMass = gX_PhysicsBody_GetMass(body);
    if (restMass <= 0.0f) return -1;
    for (gX_I32 i = 0; i < g_relCount; i++) {
        if (g_relBodies[i].body.internal != body.internal) continue;
        g_relBodies[i].restMass = restMass;
        g_relBodies[i].appliedMass = restMass;
        gX_PhysicsBody_SetMass(body, restMass);
        return i;
    }
    if (g_relCount >= GX_PHYSICS_RELATIVITY_MAX_BODIES) return -1;
    gX_PhysicsRelativity_Body_ *entry = &g_relBodies[g_relCount];
    entry->body = body;
    entry->restMass = restMass;
    entry->appliedMass = restMass;
    entry->beta = 0.0f;
    entry->gamma = 1.0f;
    return g_relCount++;
}

gX_I0 gX_PhysicsRelativity_RemoveBody(gX_PhysicsBody body) {
    if (body.internal == 0) return;
    for (gX_I32 i = 0; i < g_relCount; i++) {
        if (g_relBodies[i].body.internal != body.internal) continue;
        //ai: the body leaves at its rest mass — a lingering dilated mass
        //   would surprise whoever owns it next.
        if (g_relBodies[i].appliedMass != g_relBodies[i].restMass
            && gX_PhysicsBody_GetType(g_relBodies[i].body)
                == gX_PHYSICS_BODY_DYNAMIC) {
            gX_PhysicsBody_SetMass(g_relBodies[i].body,
                g_relBodies[i].restMass);
        }
        g_relBodies[i] = g_relBodies[g_relCount - 1];
        g_relCount--;
        return;
    }
}

gX_I32 gX_PhysicsRelativity_Count(gX_I0) {
    return g_relCount;
}

gX_I1 gX_PhysicsRelativity_GetBodyState(gX_PhysicsBody body, gX_F32 *outBeta,
    gX_F32 *outGamma, gX_F32 *outMass) {
    if (outBeta) *outBeta = 0.0f;
    if (outGamma) *outGamma = 1.0f;
    if (outMass) *outMass = 0.0f;
    if (body.internal == 0) return 0;
    for (gX_I32 i = 0; i < g_relCount; i++) {
        if (g_relBodies[i].body.internal != body.internal) continue;
        if (outBeta) *outBeta = g_relBodies[i].beta;
        if (outGamma) *outGamma = g_relBodies[i].gamma;
        if (outMass) *outMass = g_relBodies[i].appliedMass;
        return 1;
    }
    return 0;
}

//ai: one step's relativity pass — speed clamp first (so the solver never
//   integrates a superluminal velocity), then the gamma mass update on the
//   dynamic bodies.
gX_I0 gX_PhysicsRelativity_Apply(gX_I0) {
    if (!g_relEnabled) return;
    gX_F32 lightSpeed = g_relSettings.lightSpeed;
    if (lightSpeed <= 0.0f) return;
    gX_F32 cap = lightSpeed * (1.0f - g_relSettings.clampMargin);
    for (gX_I32 i = 0; i < g_relCount; i++) {
        gX_PhysicsRelativity_Body_ *entry = &g_relBodies[i];
        if (entry->body.internal == 0) continue;
        gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(entry->body);
        gX_F32 speed = gX_F32v3_Length(velocity);
        if (speed > cap) {
            velocity = gX_F32v3_MulScalar(velocity, cap / speed);
            gX_PhysicsBody_SetLinearVel(entry->body, velocity);
            speed = cap;
        }
        entry->beta = speed / lightSpeed;
        entry->gamma = gX_F32_Physics_RelativityGamma(entry->beta);
        if (gX_PhysicsBody_GetType(entry->body) != gX_PHYSICS_BODY_DYNAMIC) {
            continue;
        }
        gX_F32 targetMass = entry->restMass * entry->gamma;
        gX_F32 delta = fabsf(targetMass - entry->appliedMass);
        if (delta > entry->restMass * g_relSettings.massUpdateStep) {
            gX_PhysicsBody_SetMass(entry->body, targetMass);
            entry->appliedMass = targetMass;
        }
    }
}
