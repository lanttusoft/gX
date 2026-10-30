/**********************************************************************************************
*
*   [gX: src/physics/fluid.h]
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


#ifndef GX_PHYSICS_FLUID_H
#define GX_PHYSICS_FLUID_H

#ifdef __cplusplus
extern "C" {
#endif

//ai: fluid applier. Bodies registered here get Archimedes buoyancy
//   (density * g * V_submerged, applied at the centre of buoyancy), plus
//   quadratic + linear drag and angular drag while submerged. The water is a
//   static region — an axis-aligned box with a flat surface at `surfaceY`
//   and a density in the sandbox's tonne convention (1.0 = water; element
//   props carry specific gravity). Bodies are modelled as spheres of
//   equivalent volume (radius passed at registration), so the sphere cap
//   volume is exact per shape and boxes float/sink by the same displaced
//   volume at a small shape error.
//   `gX_PhysicsFluid_Apply` must run once per fixed step (the physics module
//   step hook), never once per frame: a per-frame call would be consumed by
//   the first sub-step only, and drag would scale with the sub-step count.
//   The registry is a fixed array walked in registration order, so the
//   applied forces (and therefore the step results) stay deterministic.
//   Surface entries queue as splash events for the scene FX.

#include "../common/types.h"
#include "../wrappers/physics.h"

//ai: registry capacity — matches the EM/thermal registries (the sandbox caps
//   at 64 props + 8 ragdolls x 11 bones = 152 bodies).
#define GX_PHYSICS_FLUID_MAX_BODIES 256
//ai: splash event ring — drained by the scene each frame.
#define GX_PHYSICS_FLUID_MAX_EVENTS 8

typedef struct gX_PhysicsFluid_Settings {
    gX_F32 surfaceY;        //ai: water surface height (world units)
    gX_F32 density;         //ai: fluid density, tonne units (1.0 = water)
    gX_F32 gravity;         //ai: downward acceleration magnitude
    gX_F32 dragCoefficient; //ai: quadratic drag Cd (0.5*rho*Cd*A folded in)
    gX_F32 linearDrag;      //ai: low-speed damping, scaled by rho*V_sub
    gX_F32 angularDrag;     //ai: angular damping, scaled by rho*V_sub
    gX_F32 minX, maxX;      //ai: region extent along x
    gX_F32 minZ, maxZ;      //ai: region extent along z
    gX_F32 floorY;          //ai: region bottom (deepest modelled water)
} gX_PhysicsFluid_Settings;

//ai: one surface entry (above -> below the waterline).
typedef struct gX_PhysicsFluid_Event {
    gX_F32v3 position;      //ai: crossing point on the surface plane
    gX_F32 speed;           //ai: downward speed at the crossing
} gX_PhysicsFluid_Event;

gX_PhysicsFluid_Settings gX_PhysicsFluid_Defaults(gX_I0);

//ai: clear the body registry and event queue, restore defaults, disable.
gX_I0 gX_PhysicsFluid_Reset(gX_I0);

gX_I0 gX_PhysicsFluid_SetEnabled(gX_I1 enabled);
gX_I1 gX_PhysicsFluid_Enabled(gX_I0);
gX_I0 gX_PhysicsFluid_SetSettings(gX_PhysicsFluid_Settings settings);
gX_PhysicsFluid_Settings gX_PhysicsFluid_GetSettings(gX_I0);

//ai: register a body (slot index, or -1 when full/invalid). A body is
//   registered once; re-registering updates its radius.
gX_I32 gX_PhysicsFluid_AddBody(gX_PhysicsBody body, gX_F32 radius);
//ai: drop a body from the registry (call before destroying the body).
gX_I0 gX_PhysicsFluid_RemoveBody(gX_PhysicsBody body);
gX_I32 gX_PhysicsFluid_Count(gX_I0);

//ai: apply one step's buoyancy/drag to every registered body. Call from the
//   physics step hook. Bodies with invalid handles are skipped.
gX_I0 gX_PhysicsFluid_Apply(gX_I0);

//ai: splash queue — entries pushed by Apply, peeked oldest-first and
//   cleared by the scene once drained.
gX_I32 gX_PhysicsFluid_EventCount(gX_I0);
gX_I1 gX_PhysicsFluid_EventGet(gX_I32 index, gX_PhysicsFluid_Event *out);
gX_I0 gX_PhysicsFluid_EventClear(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_PHYSICS_FLUID_H
