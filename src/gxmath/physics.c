/**********************************************************************************************
*
*   gxmath/physics.c
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

//opencode: ported physics functions from gamemath to gxmath

#include <math.h>

#include "physics.h"
#include "config.h"

//opencode: gX_F32v4_Physics_Newton2GetAcceleration
gX_F32v4 gX_F32v4_Physics_Newton2GetAcceleration(gX_F32v4 force, gX_F32 mass) {
    gX_F32 invmass = 1.0f / mass;
    gX_F32v4 result;
    result.x = force.x * invmass;
    result.y = force.y * invmass;
    result.z = force.z * invmass;
    result.w = force.w * invmass;
    return result;
}

//opencode: gX_F32_Physics_PotentialEnergy
gX_F32 gX_F32_Physics_PotentialEnergy(gX_F32 mass, gX_F32 gfield_acc, gX_F32 displ_from_surface) {
    return mass * gfield_acc * displ_from_surface;
}

//opencode: gX_F32_Physics_KineticEnergy
gX_F32 gX_F32_Physics_KineticEnergy(gX_F32v4 velocity, gX_F32 mass) {
    gX_F32 vmag = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y +
                        velocity.z * velocity.z + velocity.w * velocity.w);
    return 0.5f * mass * vmag * vmag;
}

//opencode: gX_F32_Physics_LorentzLength
gX_F32 gX_F32_Physics_LorentzLength(gX_F32 length, gX_F32 speed) {
    gX_F32 c = (gX_F32)gX_PHYSICS_C;
    if (speed > c) return NAN;
    gX_F32 c2 = c * c;
    return length * sqrtf(1.0f - (speed * speed / c2));
}

//opencode: gX_F32_Physics_Temp_RayleighJeans
gX_F32 gX_F32_Physics_Temp_RayleighJeans(gX_F32 temperature, gX_F32 wavelength) {
    gX_F32 k = (gX_F32)gX_PHYSICS_BOLTZMANN;
    gX_F32 c = (gX_F32)gX_PHYSICS_C;
    return (2.0f * c * k * temperature) / powf(wavelength, 4.0f);
}

//opencode: gX_F32_Physics_Temp_Planck
gX_F32 gX_F32_Physics_Temp_Planck(gX_F32 temperature, gX_F32 wavelength) {
    gX_F32 k = (gX_F32)gX_PHYSICS_BOLTZMANN;
    gX_F32 c = (gX_F32)gX_PHYSICS_C;
    gX_F32 h = (gX_F32)gX_PHYSICS_H;
    gX_F32 upper = 2.0f * h * c * c;
    gX_F32 inside = (h * c) / (wavelength * k * temperature);
    gX_F32 lower = powf(wavelength, 5.0f) * (expf(inside) - 1.0f);
    return upper / lower;
}

//opencode: gX_F32v3_Physics_Temp_Planck
gX_F32v3 gX_F32v3_Physics_Temp_Planck(gX_F32 temperature) {
    gX_F32v3 result;
    result.x = gX_F32_Physics_Temp_Planck(temperature, 650.0f / 1.0e9f);
    result.y = gX_F32_Physics_Temp_Planck(temperature, 520.0f / 1.0e9f);
    result.z = gX_F32_Physics_Temp_Planck(temperature, 460.0f / 1.0e9f);
    return result;
}

//opencode: gX_F32v4_Physics_Temp_Planck
gX_F32v4 gX_F32v4_Physics_Temp_Planck(gX_F32 temperature) {
    gX_F32v4 result;
    result.x = gX_F32_Physics_Temp_Planck(temperature, 650.0f / 1.0e9f);
    result.y = gX_F32_Physics_Temp_Planck(temperature, 520.0f / 1.0e9f);
    result.z = gX_F32_Physics_Temp_Planck(temperature, 460.0f / 1.0e9f);
    result.w = 1.0f;
    return result;
}

//ai: gX_F32_Physics_CoulombForce
gX_F32 gX_F32_Physics_CoulombForce(gX_F32 q1, gX_F32 q2, gX_F32 distance) {
    if (distance <= 0.0f) return NAN;
    gX_F32 k = (gX_F32)gX_PHYSICS_COULOMB;
    return k * q1 * q2 / (distance * distance);
}

//ai: gX_F32_Physics_CoulombPotential
gX_F32 gX_F32_Physics_CoulombPotential(gX_F32 q1, gX_F32 q2, gX_F32 distance) {
    if (distance <= 0.0f) return NAN;
    gX_F32 k = (gX_F32)gX_PHYSICS_COULOMB;
    return k * q1 * q2 / distance;
}

//ai: gX_F32v3_Physics_ElectricField
gX_F32v3 gX_F32v3_Physics_ElectricField(gX_F32 charge, gX_F32v3 offset) {
    gX_F32v3 result = {0.0f, 0.0f, 0.0f};
    gX_F32 r2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (r2 <= 0.0f) return result;
    gX_F32 k = (gX_F32)gX_PHYSICS_COULOMB;
    gX_F32 invR3 = 1.0f / (r2 * sqrtf(r2));
    gX_F32 scale = k * charge * invR3;
    result.x = offset.x * scale;
    result.y = offset.y * scale;
    result.z = offset.z * scale;
    return result;
}

//ai: gX_F32v3_Physics_LorentzForce
gX_F32v3 gX_F32v3_Physics_LorentzForce(gX_F32 charge, gX_F32v3 velocity,
    gX_F32v3 magneticField) {
    gX_F32v3 result;
    result.x = charge * (velocity.y * magneticField.z - velocity.z * magneticField.y);
    result.y = charge * (velocity.z * magneticField.x - velocity.x * magneticField.z);
    result.z = charge * (velocity.x * magneticField.y - velocity.y * magneticField.x);
    return result;
}

//ai: gX_F32_Physics_CyclotronRadius
gX_F32 gX_F32_Physics_CyclotronRadius(gX_F32 mass, gX_F32 speed,
    gX_F32 charge, gX_F32 magneticField) {
    gX_F32 q = (charge < 0.0f) ? -charge : charge;
    if (q <= 0.0f || magneticField == 0.0f || speed < 0.0f) return NAN;
    gX_F32 denominator = q * magneticField;
    return mass * speed / denominator;
}

//ai: gX_F32_Physics_PhotonEnergy
gX_F32 gX_F32_Physics_PhotonEnergy(gX_F32 wavelength) {
    if (wavelength <= 0.0f) return NAN;
    return (gX_F32)(gX_PHYSICS_H * gX_PHYSICS_C) / wavelength;
}

//ai: gX_F32v3_Physics_DipoleField
gX_F32v3 gX_F32v3_Physics_DipoleField(gX_F32v3 moment, gX_F32v3 offset) {
    gX_F32v3 result = {0.0f, 0.0f, 0.0f};
    gX_F32 r2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (r2 <= 0.0f) return result;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    gX_F32 r = sqrtf(r2);
    gX_F32 invR = 1.0f / r;
    gX_F32 rx = offset.x * invR;
    gX_F32 ry = offset.y * invR;
    gX_F32 rz = offset.z * invR;
    gX_F32 mDotR = moment.x * rx + moment.y * ry + moment.z * rz;
    gX_F32 scale = c / (r2 * r);
    result.x = scale * (3.0f * mDotR * rx - moment.x);
    result.y = scale * (3.0f * mDotR * ry - moment.y);
    result.z = scale * (3.0f * mDotR * rz - moment.z);
    return result;
}

//ai: gX_F32_Physics_DipoleFieldOnAxis
gX_F32 gX_F32_Physics_DipoleFieldOnAxis(gX_F32 moment, gX_F32 distance) {
    if (distance <= 0.0f) return NAN;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    return c * 2.0f * moment / (distance * distance * distance);
}

//ai: gX_F32_Physics_DipoleFieldEquatorial
gX_F32 gX_F32_Physics_DipoleFieldEquatorial(gX_F32 moment, gX_F32 distance) {
    if (distance <= 0.0f) return NAN;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    return c * moment / (distance * distance * distance);
}

//ai: gX_F32v3_Physics_DipoleTorque
gX_F32v3 gX_F32v3_Physics_DipoleTorque(gX_F32v3 moment,
    gX_F32v3 magneticField) {
    gX_F32v3 result;
    result.x = moment.y * magneticField.z - moment.z * magneticField.y;
    result.y = moment.z * magneticField.x - moment.x * magneticField.z;
    result.z = moment.x * magneticField.y - moment.y * magneticField.x;
    return result;
}

//ai: gX_F32v3_Physics_DipoleForce
gX_F32v3 gX_F32v3_Physics_DipoleForce(gX_F32v3 m1, gX_F32v3 m2,
    gX_F32v3 offset) {
    gX_F32v3 result = {0.0f, 0.0f, 0.0f};
    gX_F32 r2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (r2 <= 0.0f) return result;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    gX_F32 r = sqrtf(r2);
    gX_F32 invR = 1.0f / r;
    gX_F32 rx = offset.x * invR;
    gX_F32 ry = offset.y * invR;
    gX_F32 rz = offset.z * invR;
    gX_F32 m1DotR = m1.x * rx + m1.y * ry + m1.z * rz;
    gX_F32 m2DotR = m2.x * rx + m2.y * ry + m2.z * rz;
    gX_F32 m1DotM2 = m1.x * m2.x + m1.y * m2.y + m1.z * m2.z;
    gX_F32 scale = 3.0f * c / (r2 * r2);
    result.x = scale * (m1DotR * m2.x + m2DotR * m1.x + m1DotM2 * rx
        - 5.0f * m1DotR * m2DotR * rx);
    result.y = scale * (m1DotR * m2.y + m2DotR * m1.y + m1DotM2 * ry
        - 5.0f * m1DotR * m2DotR * ry);
    result.z = scale * (m1DotR * m2.z + m2DotR * m1.z + m1DotM2 * rz
        - 5.0f * m1DotR * m2DotR * rz);
    return result;
}

//ai: gX_F32_Physics_DipoleForceOnAxis
gX_F32 gX_F32_Physics_DipoleForceOnAxis(gX_F32 m1, gX_F32 m2,
    gX_F32 distance) {
    if (distance <= 0.0f) return NAN;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    return -6.0f * c * m1 * m2 / (distance * distance * distance * distance);
}

//ai: gX_F32_Physics_DipolePotential
gX_F32 gX_F32_Physics_DipolePotential(gX_F32v3 m1, gX_F32v3 m2,
    gX_F32v3 offset) {
    gX_F32 r2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (r2 <= 0.0f) return NAN;
    gX_F32 c = (gX_F32)gX_PHYSICS_MAGNETIC;
    gX_F32 r = sqrtf(r2);
    gX_F32 invR = 1.0f / r;
    gX_F32 rx = offset.x * invR;
    gX_F32 ry = offset.y * invR;
    gX_F32 rz = offset.z * invR;
    gX_F32 m1DotR = m1.x * rx + m1.y * ry + m1.z * rz;
    gX_F32 m2DotR = m2.x * rx + m2.y * ry + m2.z * rz;
    gX_F32 m1DotM2 = m1.x * m2.x + m1.y * m2.y + m1.z * m2.z;
    return -c * (3.0f * m1DotR * m2DotR - m1DotM2) / (r2 * r);
}

//ai: gX_F32_Physics_Temp_StefanBoltzmann
gX_F32 gX_F32_Physics_Temp_StefanBoltzmann(gX_F32 temperature, gX_F32 area,
    gX_F32 emissivity) {
    if (temperature < 0.0f) return NAN;
    gX_F32 sigma = (gX_F32)gX_PHYSICS_STEFAN_BOLTZMANN;
    gX_F32 t2 = temperature * temperature;
    return sigma * emissivity * area * t2 * t2;
}

//ai: gX_F32_Physics_Temp_WienPeak
gX_F32 gX_F32_Physics_Temp_WienPeak(gX_F32 temperature) {
    if (temperature <= 0.0f) return NAN;
    return (gX_F32)gX_PHYSICS_WIEN / temperature;
}

//ai: gX_F32v3_Physics_Temp_BlackbodyRGB
gX_F32v3 gX_F32v3_Physics_Temp_BlackbodyRGB(gX_F32 temperature) {
    gX_F32v3 result = {0.0f, 0.0f, 0.0f};
    if (temperature <= 0.0f) return result;
    gX_F32v3 sample = gX_F32v3_Physics_Temp_Planck(temperature);
    gX_F32 maxChannel = sample.x;
    if (sample.y > maxChannel) maxChannel = sample.y;
    if (sample.z > maxChannel) maxChannel = sample.z;
    if (maxChannel <= 0.0f) return result;
    gX_F32 inv = 1.0f / maxChannel;
    result.x = sample.x * inv;
    result.y = sample.y * inv;
    result.z = sample.z * inv;
    return result;
}

//ai: gX_F32_Physics_RelativityBeta
gX_F32 gX_F32_Physics_RelativityBeta(gX_F32 velocity, gX_F32 lightSpeed) {
    if (lightSpeed <= 0.0f) return NAN;
    return velocity / lightSpeed;
}

//ai: gX_F32_Physics_RelativityGamma — the clamp keeps gamma finite when a
//   solver slip pushes beta past 1.
gX_F32 gX_F32_Physics_RelativityGamma(gX_F32 beta) {
    gX_F32 limit = (gX_F32)gX_PHYSICS_RELATIVITY_BETA_MAX;
    if (beta > limit) beta = limit;
    if (beta < -limit) beta = -limit;
    return 1.0f / sqrtf(1.0f - beta * beta);
}

//ai: gX_F32_Physics_RelativityGammaForVelocity
gX_F32 gX_F32_Physics_RelativityGammaForVelocity(gX_F32 velocity,
    gX_F32 lightSpeed) {
    return gX_F32_Physics_RelativityGamma(
        gX_F32_Physics_RelativityBeta(velocity, lightSpeed));
}

//ai: gX_F32_Physics_RelativityContractedLength
gX_F32 gX_F32_Physics_RelativityContractedLength(gX_F32 length, gX_F32 beta) {
    gX_F32 limit = (gX_F32)gX_PHYSICS_RELATIVITY_BETA_MAX;
    if (beta > limit) beta = limit;
    if (beta < -limit) beta = -limit;
    return length * sqrtf(1.0f - beta * beta);
}

//ai: gX_F32_Physics_RelativityDilatedTime
gX_F32 gX_F32_Physics_RelativityDilatedTime(gX_F32 time, gX_F32 beta) {
    return time * gX_F32_Physics_RelativityGamma(beta);
}

//ai: gX_F32_Physics_RelativityDopplerFactor
gX_F32 gX_F32_Physics_RelativityDopplerFactor(gX_F32 beta) {
    gX_F32 limit = (gX_F32)gX_PHYSICS_RELATIVITY_BETA_MAX;
    if (beta > limit) beta = limit;
    if (beta < -limit) beta = -limit;
    return sqrtf((1.0f + beta) / (1.0f - beta));
}

//ai: gX_F32_Physics_RelativityVelocityAdd
gX_F32 gX_F32_Physics_RelativityVelocityAdd(gX_F32 u, gX_F32 v,
    gX_F32 lightSpeed) {
    if (lightSpeed <= 0.0f) return NAN;
    return (u + v) / (1.0f + (u * v) / (lightSpeed * lightSpeed));
}

//ai: gX_F32_Physics_RelativityRapidity
gX_F32 gX_F32_Physics_RelativityRapidity(gX_F32 beta) {
    gX_F32 limit = (gX_F32)gX_PHYSICS_RELATIVITY_BETA_MAX;
    if (beta > limit) beta = limit;
    if (beta < -limit) beta = -limit;
    return atanhf(beta);
}
