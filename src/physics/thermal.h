/**********************************************************************************************
*
*   [gX: src/physics/thermal.h]
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


#ifndef GX_PHYSICS_THERMAL_H
#define GX_PHYSICS_THERMAL_H

#ifdef __cplusplus
extern "C" {
#endif

//ai: thermal applier. Bodies registered here carry an enthalpy state from
//   which temperature is derived, so a melting element can sit on its
//   melting plateau (latent heat of fusion) instead of heating through it.
//   Each fixed step the applier cools bodies radiatively toward ambient
//   (T^4 law) and the collision callback exchanges heat with every touching
//   pair (conduction) and converts impact closing speed into heat.
//   Sim-scaled like EM: `heatingScale` amplifies impact energy,
//   `coolingScale` stands in for sigma*area and `contactConductance` for
//   the contact heat transfer. The gxmath functions stay SI-exact.
//   The wrapper's collision-callback slot is single; thermal owns it while
//   enabled. The registry is a fixed array walked in registration order, so
//   temperatures evolve deterministically.

#include "../common/types.h"
#include "../wrappers/physics.h"

//ai: registry capacity — the sandbox caps at 64 props + 8 ragdolls x 11
//   bones (152 bodies), so the registries hold 256.
#define GX_PHYSICS_THERMAL_MAX_BODIES 256

typedef struct gX_PhysicsThermal_Settings {
    gX_F32 ambientTemperature;  //ai: K (default 288)
    gX_F32 heatingScale;        //ai: impact-energy amplification (default 2000)
    gX_F32 coolingScale;        //ai: radiative tuning, absorbs sigma*area (default 1e-7)
    gX_F32 contactConductance;  //ai: contact heat transfer W/K sim (default 300)
    gX_F32 maxTemperature;      //ai: clamp against the T^4 stiff end (default 4000)
    gX_F32 minImpactSpeed;      //ai: closing speed floor for impact heat (default 2.0)
    gX_I1  enabled;
} gX_PhysicsThermal_Settings;

gX_PhysicsThermal_Settings gX_PhysicsThermal_Defaults(gX_I0);

//ai: clear the registry, restore defaults, disable.
gX_I0 gX_PhysicsThermal_Reset(gX_I0);

gX_I0 gX_PhysicsThermal_SetEnabled(gX_I1 enabled);
gX_I1 gX_PhysicsThermal_Enabled(gX_I0);
gX_I0 gX_PhysicsThermal_SetSettings(gX_PhysicsThermal_Settings settings);
gX_PhysicsThermal_Settings gX_PhysicsThermal_GetSettings(gX_I0);

//ai: register a body with its thermal material data (mass in sim units,
//   specific heat J/(kg*K), emissivity 0..1, melting point K and latent
//   heat J/kg; meltingPoint <= 0 or latentHeat <= 0 means no plateau).
//   Returns the slot or -1.
gX_I32 gX_PhysicsThermal_AddBody(gX_PhysicsBody body, gX_F32 temperature,
    gX_F32 mass, gX_F32 specificHeat, gX_F32 emissivity,
    gX_F32 meltingPoint, gX_F32 latentHeat);
gX_I0 gX_PhysicsThermal_RemoveBody(gX_PhysicsBody body);
gX_I32 gX_PhysicsThermal_Count(gX_I0);

//ai: ambient for unregistered bodies.
gX_F32 gX_PhysicsThermal_GetTemperature(gX_PhysicsBody body);
gX_I0 gX_PhysicsThermal_SetTemperature(gX_PhysicsBody body, gX_F32 temperature);
//ai: add `energy` (sim joules) as heat — dH = E/mass, with the melting
//   plateau absorbing heat at constant temperature.
gX_I0 gX_PhysicsThermal_HeatBody(gX_PhysicsBody body, gX_F32 energy);
//ai: 1 when the body's enthalpy is past the latent plateau.
gX_I1 gX_PhysicsThermal_IsMolten(gX_PhysicsBody body);
//ai: fraction of the latent heat absorbed (0 solid, 1 fully molten).
gX_F32 gX_PhysicsThermal_MeltFraction(gX_PhysicsBody body);

//ai: install/remove the conduction + impact-heating collision callback on a
//   world.
gX_I0 gX_PhysicsThermal_InstallCollisionCallback(gX_PhysicsWorld world);
gX_I0 gX_PhysicsThermal_RemoveCollisionCallback(gX_PhysicsWorld world);

//ai: per-step radiative cooling; call from the physics step hook with the
//   step dt (also stores dt for the contact callback's conduction).
gX_I0 gX_PhysicsThermal_Apply(gX_F32 deltaTime);

#ifdef __cplusplus
}
#endif

#endif //GX_PHYSICS_THERMAL_H
