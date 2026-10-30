/**********************************************************************************************
*
*   [gX: src/physics/relativity.h]
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


#ifndef GX_PHYSICS_RELATIVITY_H
#define GX_PHYSICS_RELATIVITY_H

#ifdef __cplusplus
extern "C" {
#endif

//ai: special-relativity applier. ODE's solver is Newtonian and fixed-step, so
//   this is two layer effects on top of it, not SR dynamics:
//   - a per-step speed clamp: no registered body's speed reaches `lightSpeed`
//     (the clamp bites at c*(1 - clampMargin));
//   - the mass-dilation trick: a body's mass scales with gamma*restMass as it
//     speeds up, so a constant force accelerates it less and less as v -> c
//     without the solver knowing anything about relativity.
//   `gX_PhysicsRelativity_Apply` must run once per fixed step (the physics
//   module step hook), after the appliers that add forces: a per-frame call
//   would be consumed by the first sub-step only. The registry is a fixed
//   array walked in registration order, and the applier is per-body, so the
//   step results stay deterministic. Mass updates are throttled
//   (`massUpdateStep`): ODE rebuilds the mass matrix on every SetMass, so
//   the applier only re-masses when gamma has moved by more than that
//   fraction of the rest mass.

#include "../common/types.h"
#include "../wrappers/physics.h"

//ai: registry capacity — the sandbox registers its props, the mover
//   payloads and the light-speed slug.
#define GX_PHYSICS_RELATIVITY_MAX_BODIES 128

typedef struct gX_PhysicsRelativity_Settings {
    gX_F32 lightSpeed;      //ai: c in sim units/s (default 12)
    gX_F32 clampMargin;     //ai: speeds cap at c*(1 - margin) (default 1e-3)
    gX_F32 massUpdateStep;  //ai: re-mass when gamma moved by this fraction
                            //   of the rest mass (default 0.01)
} gX_PhysicsRelativity_Settings;

gX_PhysicsRelativity_Settings gX_PhysicsRelativity_Defaults(gX_I0);

//ai: clear the registry, restore defaults, disable. Disabling an active
//   layer restores every registered body's rest mass.
gX_I0 gX_PhysicsRelativity_Reset(gX_I0);
gX_I0 gX_PhysicsRelativity_SetEnabled(gX_I1 enabled);
gX_I1 gX_PhysicsRelativity_Enabled(gX_I0);
gX_I0 gX_PhysicsRelativity_SetSettings(gX_PhysicsRelativity_Settings settings);
gX_PhysicsRelativity_Settings gX_PhysicsRelativity_GetSettings(gX_I0);

//ai: register a body with its rest mass (slot index, or -1 when
//   full/invalid). A body is registered once; re-registering updates the
//   rest mass and restores the body's mass to it.
gX_I32 gX_PhysicsRelativity_AddBody(gX_PhysicsBody body, gX_F32 restMass);
//ai: drop a body from the registry (call before destroying it). A dilated
//   body is restored to its rest mass first, so nothing lingers.
gX_I0 gX_PhysicsRelativity_RemoveBody(gX_PhysicsBody body);
gX_I32 gX_PhysicsRelativity_Count(gX_I0);

//ai: the body's last measured beta/gamma/applied mass (0/1/restMass when the
//   body is not registered) — the HUD readout.
gX_I1 gX_PhysicsRelativity_GetBodyState(gX_PhysicsBody body, gX_F32 *outBeta,
    gX_F32 *outGamma, gX_F32 *outMass);

//ai: apply one step: clamp every registered body's speed below c and re-mass
//   the dynamic ones by their gamma. The slowest possible light speed is
//   clamped to 0.1 so a bad tune cannot produce infinities.
gX_I0 gX_PhysicsRelativity_Apply(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_PHYSICS_RELATIVITY_H
