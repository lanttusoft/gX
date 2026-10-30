/**********************************************************************************************
*
*   gxmath/physics.h
*   Copyright (c) 2025 nauris
* 
*   Uses functions from: raymath v1.5
* 
* 
*   <raymath> LICENSE: zlib/libpng
* 
*   Copyright (c) 2015-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef GXMATH_PHYSICS_H
#define GXMATH_PHYSICS_H

#include "../common/types.h"

//opencode: Ported physics functions from gamemath to gxmath

//opencode: gX_F32v4_Physics_Newton2GetAcceleration
//opencode: gX_F32_Physics_PotentialEnergy
//opencode: gX_F32_Physics_KineticEnergy
//opencode: gX_F32_Physics_LorentzLength
//opencode: gX_F32_Physics_Temp_RayleighJeans
//opencode: gX_F32_Physics_Temp_Planck
//opencode: gX_F32v3_Physics_Temp_Planck
//opencode: gX_F32v4_Physics_Temp_Planck
gX_F32v4 gX_F32v4_Physics_Newton2GetAcceleration(gX_F32v4 force, gX_F32 mass);
gX_F32 gX_F32_Physics_PotentialEnergy(gX_F32 mass, gX_F32 gfield_acc, gX_F32 displ_from_surface);
gX_F32 gX_F32_Physics_KineticEnergy(gX_F32v4 velocity, gX_F32 mass);
gX_F32 gX_F32_Physics_LorentzLength(gX_F32 length, gX_F32 speed);
gX_F32 gX_F32_Physics_Temp_RayleighJeans(gX_F32 temperature, gX_F32 wavelength);
gX_F32 gX_F32_Physics_Temp_Planck(gX_F32 temperature, gX_F32 wavelength);
gX_F32v3 gX_F32v3_Physics_Temp_Planck(gX_F32 temperature);
gX_F32v4 gX_F32v4_Physics_Temp_Planck(gX_F32 temperature);

//ai: electromagnetism — SI-exact point-charge laws. The simulation layer
//   scales/softens these; these functions keep the textbook math.
//ai: Coulomb force magnitude between two point charges `distance` apart
//   (signed: positive = repulsion, negative = attraction). Distance <= 0
//   is undefined and returns NAN.
gX_F32 gX_F32_Physics_CoulombForce(gX_F32 q1, gX_F32 q2, gX_F32 distance);
//ai: Coulomb potential energy of the pair (signed; 0 at infinity).
//   Distance <= 0 returns NAN.
gX_F32 gX_F32_Physics_CoulombPotential(gX_F32 q1, gX_F32 q2, gX_F32 distance);
//ai: electric field vector at `offset` from a point charge (E = k*q*r/|r|^3).
//   A zero offset has no defined direction and returns the zero vector.
gX_F32v3 gX_F32v3_Physics_ElectricField(gX_F32 charge, gX_F32v3 offset);
//ai: Lorentz force on a charge moving through a magnetic field
//   (F = q * (v x B)).
gX_F32v3 gX_F32v3_Physics_LorentzForce(gX_F32 charge, gX_F32v3 velocity,
    gX_F32v3 magneticField);
//ai: cyclotron radius r = m*v/(|q|*B). Zero charge or field (unbounded
//   motion) and negative speed return NAN.
gX_F32 gX_F32_Physics_CyclotronRadius(gX_F32 mass, gX_F32 speed,
    gX_F32 charge, gX_F32 magneticField);
//ai: photon energy E = h*c/lambda. Wavelength <= 0 returns NAN.
gX_F32 gX_F32_Physics_PhotonEnergy(gX_F32 wavelength);

//ai: magnetic dipoles — SI-exact magnetostatic laws. A dipole is a moment
//   vector (A*m^2); the simulation layer scales/softens these, the functions
//   keep the textbook math. The module rotates a magnet's local moment by
//   its body orientation, so all functions here are world-space.
//ai: field of a magnetic dipole at `offset` from it
//   (B = mu0/4pi * (3*(m.rhat)*rhat - m)/r^3). A zero offset has no defined
//   direction and returns the zero vector.
gX_F32v3 gX_F32v3_Physics_DipoleField(gX_F32v3 moment, gX_F32v3 offset);
//ai: on-axis dipole field magnitude (mu0/4pi * 2m/r^3). Distance <= 0
//   returns NAN.
gX_F32 gX_F32_Physics_DipoleFieldOnAxis(gX_F32 moment, gX_F32 distance);
//ai: equatorial dipole field magnitude (mu0/4pi * m/r^3). Distance <= 0
//   returns NAN.
gX_F32 gX_F32_Physics_DipoleFieldEquatorial(gX_F32 moment, gX_F32 distance);
//ai: torque on a dipole in a magnetic field (tau = m x B).
gX_F32v3 gX_F32v3_Physics_DipoleTorque(gX_F32v3 moment,
    gX_F32v3 magneticField);
//ai: force on dipole `m2` from dipole `m1`, `m2` at `offset` from `m1`:
//   F = 3*mu0/4pi/r^4 * [(m1.rhat)*m2 + (m2.rhat)*m1 + (m1.m2)*rhat
//       - 5*(m1.rhat)*(m2.rhat)*rhat]. A zero offset returns the zero vector.
gX_F32v3 gX_F32v3_Physics_DipoleForce(gX_F32v3 m1, gX_F32v3 m2,
    gX_F32v3 offset);
//ai: signed force between two coaxial dipoles, positive = repulsion
//   (F = -mu0/4pi * 6*m1*m2/r^4 along the separation; a positive moment
//   product attracts). Distance <= 0 returns NAN.
gX_F32 gX_F32_Physics_DipoleForceOnAxis(gX_F32 m1, gX_F32 m2,
    gX_F32 distance);
//ai: pair potential U = -mu0/4pi * [3*(m1.rhat)*(m2.rhat) - m1.m2]/r^3.
//   A zero offset returns NAN.
gX_F32 gX_F32_Physics_DipolePotential(gX_F32v3 m1, gX_F32v3 m2,
    gX_F32v3 offset);

//ai: thermal radiation — Stefan-Boltzmann, Wien's displacement, and a
//   normalized blackbody RGB sample (the gfx-facing glow color).
//ai: Stefan-Boltzmann radiated power sigma*eps*A*T^4 (W). Negative
//   temperature returns NAN.
gX_F32 gX_F32_Physics_Temp_StefanBoltzmann(gX_F32 temperature, gX_F32 area,
    gX_F32 emissivity);
//ai: Wien displacement law: peak wavelength b/T (metres). T <= 0 returns NAN.
gX_F32 gX_F32_Physics_Temp_WienPeak(gX_F32 temperature);
//ai: normalized blackbody RGB (linear, max channel = 1) sampled at
//   650/520/460 nm like the existing Planck helpers. Channels underflow to
//   zero at everyday temperatures; when every channel is zero the result is
//   the zero vector (no visible glow) rather than a division by zero.
gX_F32v3 gX_F32v3_Physics_Temp_BlackbodyRGB(gX_F32 temperature);

//ai: special relativity — the textbook laws with the light speed passed in,
//   so the simulation layer can run a tunable `c` (the SI-exact
//   `gX_F32_Physics_LorentzLength` keeps the real constant). Beta-taking
//   functions clamp |beta| to 0.9999 (gamma <= ~70.7) so a solver slip can
//   never divide by zero.
//ai: beta = v/c (signed). A non-positive light speed returns NAN.
gX_F32 gX_F32_Physics_RelativityBeta(gX_F32 velocity, gX_F32 lightSpeed);
//ai: gamma = 1/sqrt(1 - beta^2) with |beta| clamped below 1.
gX_F32 gX_F32_Physics_RelativityGamma(gX_F32 beta);
//ai: gamma from a velocity and a light speed (beta then gamma).
gX_F32 gX_F32_Physics_RelativityGammaForVelocity(gX_F32 velocity,
    gX_F32 lightSpeed);
//ai: contracted length L = L0*sqrt(1 - beta^2) (short along the motion).
gX_F32 gX_F32_Physics_RelativityContractedLength(gX_F32 length, gX_F32 beta);
//ai: dilated time t = t0*gamma (the moving clock runs slow).
gX_F32 gX_F32_Physics_RelativityDilatedTime(gX_F32 time, gX_F32 beta);
//ai: relativistic Doppler factor sqrt((1+beta)/(1-beta)) for an approaching
//   source (1 at rest, > 1 blueshift, < 1 receding for negative beta).
gX_F32 gX_F32_Physics_RelativityDopplerFactor(gX_F32 beta);
//ai: relativistic velocity addition (u + v)/(1 + u*v/c^2) — the result never
//   reaches c when both inputs are below it. A non-positive light speed
//   returns NAN.
gX_F32 gX_F32_Physics_RelativityVelocityAdd(gX_F32 u, gX_F32 v,
    gX_F32 lightSpeed);
//ai: rapidity atanh(beta) — boosts add linearly in rapidity (the additive
//   coordinate of a Lorentz boost).
gX_F32 gX_F32_Physics_RelativityRapidity(gX_F32 beta);

#endif //GXMATH_PHYSICS_H
