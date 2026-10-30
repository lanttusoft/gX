/**********************************************************************************************
*
*   [gX: src/physics/em.h]
*   Electromagnetism applier — charged bodies in the shared physics world
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


#ifndef GX_PHYSICS_EM_H
#define GX_PHYSICS_EM_H

#ifdef __cplusplus
extern "C" {
#endif

//ai: electromagnetism applier. Bodies registered here get a charge; each
//   fixed step the applier computes pairwise Coulomb forces (softened and
//   clamped) plus the Lorentz force from a uniform magnetic field, and adds
//   them to the bodies' force accumulators.
//   This is a sim-scaled model, not SI: `coulombScale` replaces k_e,
//   `softening` keeps 1/r^2 finite near contact, and `maxForce` caps each
//   pair contribution so a close approach cannot explode the solver.
//   `gX_PhysicsEM_Apply` must run once per fixed step (the physics module
//   step hook), never once per frame: a per-frame call would be consumed by
//   the first sub-step only.
//   The registry is a fixed array walked in registration order, so the
//   applied forces (and therefore the step results) stay deterministic.

#include "../common/types.h"
#include "../wrappers/physics.h"

//ai: registry capacity — the sandbox caps at 64 props + 8 ragdolls x 11
//   bones (152 bodies), so the registries hold 256.
#define GX_PHYSICS_EM_MAX_BODIES 256

typedef struct gX_PhysicsEM_Settings {
    gX_F32 coulombScale;   //ai: sim-scaled k (default 60)
    gX_F32 softening;      //ai: epsilon in r^2 + eps^2 (default 0.6)
    gX_F32 maxForce;       //ai: per-pair/per-lorentz force cap (default 500)
    gX_F32v3 uniformB;     //ai: uniform magnetic field (zero = electrostatics only)
    gX_F32 dipoleScale;    //ai: sim-scaled mu0/4pi for dipole fields/forces
                           //   (default 0.05; the SI value is 1e-7)
    gX_F32 susceptibilityScale; //ai: sim scale applied to a body's
                           //   susceptibility before use (default 1.0)
} gX_PhysicsEM_Settings;

gX_PhysicsEM_Settings gX_PhysicsEM_Defaults(gX_I0);

//ai: clear the body registry, restore defaults, disable.
gX_I0 gX_PhysicsEM_Reset(gX_I0);

gX_I0 gX_PhysicsEM_SetEnabled(gX_I1 enabled);
gX_I1 gX_PhysicsEM_Enabled(gX_I0);
gX_I0 gX_PhysicsEM_SetSettings(gX_PhysicsEM_Settings settings);
gX_PhysicsEM_Settings gX_PhysicsEM_GetSettings(gX_I0);
gX_I0 gX_PhysicsEM_SetUniformB(gX_F32v3 magneticField);

//ai: register a charged body (slot index, or -1 when full/invalid). A body
//   is registered once; re-registering updates the charge.
gX_I32 gX_PhysicsEM_AddBody(gX_PhysicsBody body, gX_F32 charge);
//ai: register/update a magnetic dipole moment on a body — the moment is in
//   the body's local frame, so the magnet's field rotates with it. A body
//   without a charge gets an entry with charge 0. Returns the slot or -1.
gX_I32 gX_PhysicsEM_SetDipole(gX_PhysicsBody body, gX_F32v3 moment);
//ai: read the body's dipole moment rotated into world space (0 when the
//   body is not registered).
gX_I1 gX_PhysicsEM_GetDipole(gX_PhysicsBody body, gX_F32v3 *out);
//ai: set a body's induced-magnetic response (sim-scaled polarizability
//   alpha: m_ind = alpha * B, so a positive alpha is pulled toward field
//   sources — iron near a magnet). Negative values model diamagnets.
gX_I32 gX_PhysicsEM_SetSusceptibility(gX_PhysicsBody body, gX_F32 alpha);
//ai: read the registered polarizability (0 when the body is not
//   registered).
gX_I1 gX_PhysicsEM_GetSusceptibility(gX_PhysicsBody body, gX_F32 *out);
//ai: drop a body from the registry (call before destroying the body).
gX_I0 gX_PhysicsEM_RemoveBody(gX_PhysicsBody body);
gX_I32 gX_PhysicsEM_Count(gX_I0);

//ai: apply one step's EM forces to every registered body. Call from the
//   physics step hook. Bodies with invalid handles are skipped. Charges get
//   the softened Coulomb pair force plus the uniform-field Lorentz force;
//   dipoles get the dipole pair force (gradient of the pair potential),
//   each other's torques, and the uniform-field torque m x B.
gX_I0 gX_PhysicsEM_Apply(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_PHYSICS_EM_H
