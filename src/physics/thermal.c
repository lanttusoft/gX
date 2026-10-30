/**********************************************************************************************
*
*   [gX: src/physics/thermal.c]
*   Thermal applier — body temperature, impacts, conduction and melting
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


#include "thermal.h"

#include <math.h>
#include <stddef.h>

//ai: fixed registry — no allocation, walked in registration order so the
//   thermal state evolves deterministically. `enthalpy` is J/kg above 0 K;
//   temperature is derived so the melting plateau is exact.
typedef struct gX_PhysicsThermal_Body_ {
    gX_PhysicsBody body;
    gX_F32 enthalpy;
    gX_F32 mass;
    gX_F32 specificHeat;
    gX_F32 emissivity;
    gX_F32 meltingPoint;
    gX_F32 latentHeat;
} gX_PhysicsThermal_Body_;

static gX_PhysicsThermal_Body_ g_thermalBodies[GX_PHYSICS_THERMAL_MAX_BODIES];
static gX_I32 g_thermalCount = 0;
static gX_I1 g_thermalEnabled = 0;
static gX_F32 g_thermalLastDt = 0.0f;
static gX_PhysicsThermal_Settings g_thermalSettings = {
    288.0f, 2000.0f, 1e-7f, 300.0f, 4000.0f, 2.0f, 0
};

gX_PhysicsThermal_Settings gX_PhysicsThermal_Defaults(gX_I0) {
    gX_PhysicsThermal_Settings defaults = {
        288.0f, 2000.0f, 1e-7f, 300.0f, 4000.0f, 2.0f, 0
    };
    return defaults;
}

static gX_PhysicsThermal_Body_ *thermalFind_(gX_PhysicsBody body) {
    if (body.internal == 0) return NULL;
    for (gX_I32 i = 0; i < g_thermalCount; i++) {
        if (g_thermalBodies[i].body.internal == body.internal) {
            return &g_thermalBodies[i];
        }
    }
    return NULL;
}

//ai: enthalpy thresholds of the melting plateau. A zero melting point or
//   latent heat collapses the plateau (plain sensible-heat model).
static gX_F32 thermalHMeltStart_(const gX_PhysicsThermal_Body_ *entry) {
    if (entry->meltingPoint <= 0.0f) return 1e30f;
    return entry->specificHeat * entry->meltingPoint;
}

static gX_F32 thermalHMeltEnd_(const gX_PhysicsThermal_Body_ *entry) {
    if (entry->latentHeat <= 0.0f) return thermalHMeltStart_(entry);
    return thermalHMeltStart_(entry) + entry->latentHeat;
}

static gX_F32 thermalTemperatureForH_(const gX_PhysicsThermal_Body_ *entry,
    gX_F32 enthalpy) {
    gX_F32 meltStart = thermalHMeltStart_(entry);
    gX_F32 meltEnd = thermalHMeltEnd_(entry);
    if (enthalpy <= meltStart) {
        return enthalpy / entry->specificHeat;
    }
    if (enthalpy < meltEnd) {
        return entry->meltingPoint;
    }
    return entry->meltingPoint
        + (enthalpy - meltEnd) / entry->specificHeat;
}

static gX_F32 thermalHForTemperature_(const gX_PhysicsThermal_Body_ *entry,
    gX_F32 temperature) {
    if (entry->meltingPoint <= 0.0f || temperature <= entry->meltingPoint) {
        return entry->specificHeat * temperature;
    }
    return thermalHMeltEnd_(entry)
        + entry->specificHeat * (temperature - entry->meltingPoint);
}

static gX_F32 thermalHClamp_(const gX_PhysicsThermal_Body_ *entry,
    gX_F32 enthalpy) {
    gX_F32 low = entry->specificHeat * g_thermalSettings.ambientTemperature;
    gX_F32 high = thermalHForTemperature_(entry,
        g_thermalSettings.maxTemperature);
    if (enthalpy < low) return low;
    if (enthalpy > high) return high;
    return enthalpy;
}

gX_I0 gX_PhysicsThermal_Reset(gX_I0) {
    for (gX_I32 i = 0; i < g_thermalCount; i++) {
        g_thermalBodies[i].body = gX_PHYSICS_BODY_NULL;
        g_thermalBodies[i].enthalpy = 0.0f;
        g_thermalBodies[i].mass = 0.0f;
        g_thermalBodies[i].specificHeat = 0.0f;
        g_thermalBodies[i].emissivity = 0.0f;
        g_thermalBodies[i].meltingPoint = 0.0f;
        g_thermalBodies[i].latentHeat = 0.0f;
    }
    g_thermalCount = 0;
    g_thermalEnabled = 0;
    g_thermalLastDt = 0.0f;
    g_thermalSettings = gX_PhysicsThermal_Defaults();
}

gX_I0 gX_PhysicsThermal_SetEnabled(gX_I1 enabled) {
    g_thermalEnabled = enabled ? 1 : 0;
}

gX_I1 gX_PhysicsThermal_Enabled(gX_I0) {
    return g_thermalEnabled;
}

gX_I0 gX_PhysicsThermal_SetSettings(gX_PhysicsThermal_Settings settings) {
    if (settings.ambientTemperature < 0.0f) settings.ambientTemperature = 0.0f;
    if (settings.maxTemperature < settings.ambientTemperature) {
        settings.maxTemperature = settings.ambientTemperature;
    }
    if (settings.minImpactSpeed < 0.0f) settings.minImpactSpeed = 0.0f;
    if (settings.contactConductance < 0.0f) settings.contactConductance = 0.0f;
    g_thermalSettings = settings;
}

gX_PhysicsThermal_Settings gX_PhysicsThermal_GetSettings(gX_I0) {
    return g_thermalSettings;
}

gX_I32 gX_PhysicsThermal_AddBody(gX_PhysicsBody body, gX_F32 temperature,
    gX_F32 mass, gX_F32 specificHeat, gX_F32 emissivity,
    gX_F32 meltingPoint, gX_F32 latentHeat) {
    if (body.internal == 0 || mass <= 0.0f || specificHeat <= 0.0f) return -1;
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    gX_I1 isNew = 0;
    if (!entry) {
        if (g_thermalCount >= GX_PHYSICS_THERMAL_MAX_BODIES) return -1;
        entry = &g_thermalBodies[g_thermalCount];
        g_thermalCount++;
        isNew = 1;
    }
    entry->body = body;
    entry->mass = mass;
    entry->specificHeat = specificHeat;
    entry->emissivity = emissivity;
    entry->meltingPoint = (meltingPoint > 0.0f) ? meltingPoint : 0.0f;
    entry->latentHeat = (latentHeat > 0.0f) ? latentHeat : 0.0f;
    entry->enthalpy = thermalHClamp_(entry,
        thermalHForTemperature_(entry, temperature));
    return isNew ? g_thermalCount - 1 : 0;
}

gX_I0 gX_PhysicsThermal_RemoveBody(gX_PhysicsBody body) {
    for (gX_I32 i = 0; i < g_thermalCount; i++) {
        if (g_thermalBodies[i].body.internal != body.internal) continue;
        gX_I32 last = g_thermalCount - 1;
        if (i != last) g_thermalBodies[i] = g_thermalBodies[last];
        g_thermalBodies[last].body = gX_PHYSICS_BODY_NULL;
        g_thermalBodies[last].enthalpy = 0.0f;
        g_thermalCount = last;
        return;
    }
}

gX_I32 gX_PhysicsThermal_Count(gX_I0) {
    return g_thermalCount;
}

gX_F32 gX_PhysicsThermal_GetTemperature(gX_PhysicsBody body) {
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    if (!entry) return g_thermalSettings.ambientTemperature;
    return thermalTemperatureForH_(entry, entry->enthalpy);
}

gX_I0 gX_PhysicsThermal_SetTemperature(gX_PhysicsBody body, gX_F32 temperature) {
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    if (!entry) return;
    entry->enthalpy = thermalHClamp_(entry,
        thermalHForTemperature_(entry, temperature));
}

gX_I0 gX_PhysicsThermal_HeatBody(gX_PhysicsBody body, gX_F32 energy) {
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    if (!entry) return;
    entry->enthalpy = thermalHClamp_(entry, entry->enthalpy + energy / entry->mass);
}

gX_I1 gX_PhysicsThermal_IsMolten(gX_PhysicsBody body) {
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    if (!entry || entry->latentHeat <= 0.0f) return 0;
    return entry->enthalpy >= thermalHMeltEnd_(entry) ? 1 : 0;
}

gX_F32 gX_PhysicsThermal_MeltFraction(gX_PhysicsBody body) {
    gX_PhysicsThermal_Body_ *entry = thermalFind_(body);
    if (!entry || entry->latentHeat <= 0.0f) return 0.0f;
    gX_F32 meltStart = thermalHMeltStart_(entry);
    gX_F32 fraction = (entry->enthalpy - meltStart) / entry->latentHeat;
    if (fraction < 0.0f) return 0.0f;
    if (fraction > 1.0f) return 1.0f;
    return fraction;
}

//ai: contact heat exchange — the callback runs once per touching pair per
//   step; `g_thermalLastDt` carries the step dt from Apply. Two registered
//   bodies exchange heat down the gradient; a static contact (no body)
//   sinks toward ambient. Coincident with the impact heating below.
static gX_I0 gX_PhysicsThermal_Conduct_(gX_PhysicsThermal_Body_ *entryA,
    gX_PhysicsThermal_Body_ *entryB) {
    gX_F32 dt = g_thermalLastDt;
    if (dt <= 0.0f) return;
    gX_F32 k = g_thermalSettings.contactConductance;
    if (k <= 0.0f) return;
    if (entryA && entryB) {
        gX_F32 tempA = thermalTemperatureForH_(entryA, entryA->enthalpy);
        gX_F32 tempB = thermalTemperatureForH_(entryB, entryB->enthalpy);
        gX_F32 heat = k * (tempA - tempB) * dt;
        entryA->enthalpy = thermalHClamp_(entryA,
            entryA->enthalpy - heat / entryA->mass);
        entryB->enthalpy = thermalHClamp_(entryB,
            entryB->enthalpy + heat / entryB->mass);
        return;
    }
    gX_PhysicsThermal_Body_ *entry = entryA ? entryA : entryB;
    gX_F32 temperature = thermalTemperatureForH_(entry, entry->enthalpy);
    gX_F32 heat = k * (temperature - g_thermalSettings.ambientTemperature) * dt;
    entry->enthalpy = thermalHClamp_(entry, entry->enthalpy - heat / entry->mass);
}

//ai: impact heating — the closing speed along the first contact normal feeds
//   1/2 mu v^2, amplified by heatingScale and split by mass share.
static gX_I32 gX_PhysicsThermal_CollisionCB_(gX_I0 *userData,
    gX_PhysicsBody bodyA, gX_PhysicsBody bodyB,
    gX_U32 contactCount, const gX_PhysicsContact *contacts) {
    (void)userData;
    if (!g_thermalEnabled || contactCount == 0 || !contacts) return 0;

    gX_PhysicsThermal_Body_ *entryA = thermalFind_(bodyA);
    gX_PhysicsThermal_Body_ *entryB = thermalFind_(bodyB);
    if (!entryA && !entryB) return 0;

    //ai: conduction first — resting contact must conduct regardless of the
    //   impact-speed floor.
    gX_PhysicsThermal_Conduct_(entryA, entryB);

    gX_F32v3 velA = {0.0f, 0.0f, 0.0f};
    gX_F32v3 velB = {0.0f, 0.0f, 0.0f};
    if (bodyA.internal != 0) velA = gX_PhysicsBody_GetLinearVel(bodyA);
    if (bodyB.internal != 0) velB = gX_PhysicsBody_GetLinearVel(bodyB);

    gX_F32v3 normal = contacts[0].normal;
    gX_F32 relX = velA.x - velB.x;
    gX_F32 relY = velA.y - velB.y;
    gX_F32 relZ = velA.z - velB.z;
    gX_F32 normalSpeed = relX * normal.x + relY * normal.y + relZ * normal.z;
    if (normalSpeed < 0.0f) normalSpeed = -normalSpeed;
    if (normalSpeed < g_thermalSettings.minImpactSpeed) return 0;

    //ai: reduced mass — a static contact (no body) behaves as infinite mass,
    //   so the dynamic body absorbs the whole impact.
    gX_F32 massA = entryA ? entryA->mass : 0.0f;
    gX_F32 massB = entryB ? entryB->mass : 0.0f;
    gX_F32 reducedMass;
    if (entryA && entryB) reducedMass = (massA * massB) / (massA + massB);
    else if (entryA) reducedMass = massA;
    else reducedMass = massB;

    gX_F32 energy = 0.5f * reducedMass * normalSpeed * normalSpeed
        * g_thermalSettings.heatingScale;

    if (entryA) {
        gX_F32 share = entryB ? (massB / (massA + massB)) : 1.0f;
        gX_PhysicsThermal_HeatBody(bodyA, energy * share);
    }
    if (entryB) {
        gX_F32 share = entryA ? (massA / (massA + massB)) : 1.0f;
        gX_PhysicsThermal_HeatBody(bodyB, energy * share);
    }
    return 0;
}

gX_I0 gX_PhysicsThermal_InstallCollisionCallback(gX_PhysicsWorld world) {
    gX_PhysicsWorld_SetCollisionCallback(world, gX_PhysicsThermal_CollisionCB_, NULL);
}

gX_I0 gX_PhysicsThermal_RemoveCollisionCallback(gX_PhysicsWorld world) {
    gX_PhysicsWorld_SetCollisionCallback(world, NULL, NULL);
}

gX_I0 gX_PhysicsThermal_Apply(gX_F32 deltaTime) {
    g_thermalLastDt = (deltaTime > 0.0f) ? deltaTime : 0.0f;
    if (!g_thermalEnabled || deltaTime <= 0.0f) return;
    gX_F32 ambient = g_thermalSettings.ambientTemperature;
    gX_F32 ambient4 = ambient * ambient * ambient * ambient;
    for (gX_I32 i = 0; i < g_thermalCount; i++) {
        gX_PhysicsThermal_Body_ *entry = &g_thermalBodies[i];
        if (entry->body.internal == 0) continue;
        gX_F32 t = thermalTemperatureForH_(entry, entry->enthalpy);
        gX_F32 t2 = t * t;
        gX_F32 t4 = t2 * t2;
        //ai: radiative exchange — enthalpy falls over ambient and rises
        //   under it; the ambient floor clamp keeps cold starts stable.
        gX_F32 scale = g_thermalSettings.coolingScale * entry->emissivity
            / entry->mass;
        entry->enthalpy = thermalHClamp_(entry,
            entry->enthalpy - scale * (t4 - ambient4) * deltaTime);
    }
}
