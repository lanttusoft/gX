/**********************************************************************************************
*
*   [gX: src/physics/em.c]
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


#include "em.h"

#include <math.h>

#include "../gxmath/quaternion.h"

//ai: fixed registry — no allocation, walked in registration order so the
//   force application (and the step results) stay deterministic.
typedef struct gX_PhysicsEM_Body_ {
    gX_PhysicsBody body;
    gX_F32 charge;
    gX_F32v3 dipole;    //ai: local-frame magnetic moment (zero = not a magnet)
    gX_F32 susceptibility; //ai: sim polarizability alpha (m_ind = alpha*B)
} gX_PhysicsEM_Body_;

static gX_PhysicsEM_Body_ g_emBodies[GX_PHYSICS_EM_MAX_BODIES];
static gX_I32 g_emCount = 0;
static gX_I1 g_emEnabled = 0;
static gX_PhysicsEM_Settings g_emSettings = {
    60.0f, 0.6f, 500.0f, { 0.0f, 0.0f, 0.0f }, 0.05f, 1.0f };

gX_PhysicsEM_Settings gX_PhysicsEM_Defaults(gX_I0) {
    gX_PhysicsEM_Settings defaults = {
        60.0f, 0.6f, 500.0f, { 0.0f, 0.0f, 0.0f }, 0.05f, 1.0f };
    return defaults;
}

gX_I0 gX_PhysicsEM_Reset(gX_I0) {
    for (gX_I32 i = 0; i < g_emCount; i++) {
        g_emBodies[i].body = gX_PHYSICS_BODY_NULL;
        g_emBodies[i].charge = 0.0f;
        g_emBodies[i].dipole = (gX_F32v3){0.0f, 0.0f, 0.0f};
        g_emBodies[i].susceptibility = 0.0f;
    }
    g_emCount = 0;
    g_emEnabled = 0;
    g_emSettings = gX_PhysicsEM_Defaults();
}

gX_I0 gX_PhysicsEM_SetEnabled(gX_I1 enabled) {
    g_emEnabled = enabled ? 1 : 0;
}

gX_I1 gX_PhysicsEM_Enabled(gX_I0) {
    return g_emEnabled;
}

gX_I0 gX_PhysicsEM_SetSettings(gX_PhysicsEM_Settings settings) {
    if (settings.softening < 0.0f) settings.softening = 0.0f;
    if (settings.maxForce < 0.0f) settings.maxForce = 0.0f;
    if (settings.dipoleScale < 0.0f) settings.dipoleScale = 0.0f;
    if (settings.susceptibilityScale < 0.0f) settings.susceptibilityScale = 0.0f;
    g_emSettings = settings;
}

gX_PhysicsEM_Settings gX_PhysicsEM_GetSettings(gX_I0) {
    return g_emSettings;
}

gX_I0 gX_PhysicsEM_SetUniformB(gX_F32v3 magneticField) {
    g_emSettings.uniformB = magneticField;
}

gX_I32 gX_PhysicsEM_AddBody(gX_PhysicsBody body, gX_F32 charge) {
    if (body.internal == 0) return -1;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal == body.internal) {
            g_emBodies[i].charge = charge;
            return i;
        }
    }
    if (g_emCount >= GX_PHYSICS_EM_MAX_BODIES) return -1;
    g_emBodies[g_emCount].body = body;
    g_emBodies[g_emCount].charge = charge;
    g_emBodies[g_emCount].dipole = (gX_F32v3){0.0f, 0.0f, 0.0f};
    g_emBodies[g_emCount].susceptibility = 0.0f;
    g_emCount++;
    return g_emCount - 1;
}

//ai: rotate a vector by a unit quaternion. gxmath has no rotate-vector
//   helper, so this is the standard sandwich in expanded form.
static gX_F32v3 emRotate_(gX_F32Qua q, gX_F32v3 v) {
    gX_F32v3 u = { q.x, q.y, q.z };
    gX_F32v3 t = {
        u.y * v.z - u.z * v.y + q.w * v.x,
        u.z * v.x - u.x * v.z + q.w * v.y,
        u.x * v.y - u.y * v.x + q.w * v.z
    };
    return (gX_F32v3){
        v.x + 2.0f * (u.y * t.z - u.z * t.y),
        v.y + 2.0f * (u.z * t.x - u.x * t.z),
        v.z + 2.0f * (u.x * t.y - u.y * t.x)
    };
}

gX_I32 gX_PhysicsEM_SetDipole(gX_PhysicsBody body, gX_F32v3 moment) {
    if (body.internal == 0) return -1;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal == body.internal) {
            g_emBodies[i].dipole = moment;
            return i;
        }
    }
    if (g_emCount >= GX_PHYSICS_EM_MAX_BODIES) return -1;
    g_emBodies[g_emCount].body = body;
    g_emBodies[g_emCount].charge = 0.0f;
    g_emBodies[g_emCount].dipole = moment;
    g_emCount++;
    return g_emCount - 1;
}

gX_I1 gX_PhysicsEM_GetDipole(gX_PhysicsBody body, gX_F32v3 *out) {
    if (body.internal == 0 || !out) return 0;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal != body.internal) continue;
        gX_F32Qua rotation = gX_F32Qua_Identity();
        gX_PhysicsBody_GetRotation(body, &rotation);
        *out = emRotate_(rotation, g_emBodies[i].dipole);
        return 1;
    }
    return 0;
}

gX_I32 gX_PhysicsEM_SetSusceptibility(gX_PhysicsBody body, gX_F32 alpha) {
    if (body.internal == 0) return -1;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal == body.internal) {
            g_emBodies[i].susceptibility = alpha;
            return i;
        }
    }
    if (g_emCount >= GX_PHYSICS_EM_MAX_BODIES) return -1;
    g_emBodies[g_emCount].body = body;
    g_emBodies[g_emCount].charge = 0.0f;
    g_emBodies[g_emCount].dipole = (gX_F32v3){0.0f, 0.0f, 0.0f};
    g_emBodies[g_emCount].susceptibility = alpha;
    g_emCount++;
    return g_emCount - 1;
}

gX_I1 gX_PhysicsEM_GetSusceptibility(gX_PhysicsBody body, gX_F32 *out) {
    if (body.internal == 0 || !out) return 0;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal != body.internal) continue;
        *out = g_emBodies[i].susceptibility;
        return 1;
    }
    return 0;
}

gX_I0 gX_PhysicsEM_RemoveBody(gX_PhysicsBody body) {
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal != body.internal) continue;
        gX_I32 last = g_emCount - 1;
        if (i != last) g_emBodies[i] = g_emBodies[last];
        g_emBodies[last].body = gX_PHYSICS_BODY_NULL;
        g_emBodies[last].charge = 0.0f;
        g_emBodies[last].dipole = (gX_F32v3){0.0f, 0.0f, 0.0f};
        g_emBodies[last].susceptibility = 0.0f;
        g_emCount = last;
        return;
    }
}

gX_I32 gX_PhysicsEM_Count(gX_I0) {
    return g_emCount;
}

//ai: clamp a force vector to the settings cap (keeps direction).
static gX_F32v3 emClamp_(gX_F32v3 force, gX_F32 maxForce) {
    gX_F32 mag2 = force.x * force.x + force.y * force.y + force.z * force.z;
    if (maxForce <= 0.0f || mag2 <= maxForce * maxForce) return force;
    gX_F32 scale = maxForce / sqrtf(mag2);
    return (gX_F32v3){ force.x * scale, force.y * scale, force.z * scale };
}

gX_I0 gX_PhysicsEM_Apply(gX_I0) {
    if (!g_emEnabled || g_emCount == 0) return;

    gX_F32 scale = g_emSettings.coulombScale;
    gX_F32 softening2 = g_emSettings.softening * g_emSettings.softening;
    gX_F32 maxForce = g_emSettings.maxForce;
    gX_F32v3 bField = g_emSettings.uniformB;
    gX_F32 dipoleScale = g_emSettings.dipoleScale;

    //ai: world-frame moments once per step — the local moment rotates with
    //   the body, so a magnet's field and its response follow its pose.
    gX_F32v3 moments[GX_PHYSICS_EM_MAX_BODIES];
    gX_I1 anyDipole = 0;
    for (gX_I32 i = 0; i < g_emCount; i++) {
        moments[i] = (gX_F32v3){0.0f, 0.0f, 0.0f};
        if (g_emBodies[i].body.internal == 0) continue;
        if (g_emBodies[i].dipole.x == 0.0f && g_emBodies[i].dipole.y == 0.0f
            && g_emBodies[i].dipole.z == 0.0f) continue;
        gX_F32Qua rotation = gX_F32Qua_Identity();
        gX_PhysicsBody_GetRotation(g_emBodies[i].body, &rotation);
        moments[i] = emRotate_(rotation, g_emBodies[i].dipole);
        anyDipole = 1;
    }

    //ai: pairwise Coulomb — each pair contributes equal and opposite forces
    //   along the separation line, so no torque is generated. Dipole pairs
    //   add the field-gradient force and each moment's torque in the other's
    //   field; softening keeps both finite at contact range.
    for (gX_I32 i = 0; i < g_emCount; i++) {
        if (g_emBodies[i].body.internal == 0) continue;
        gX_F32v3 posI = gX_PhysicsBody_GetPosition(g_emBodies[i].body);
        gX_F32 qi = g_emBodies[i].charge;
        gX_F32v3 mi = moments[i];
        for (gX_I32 j = i + 1; j < g_emCount; j++) {
            if (g_emBodies[j].body.internal == 0) continue;
            gX_F32v3 posJ = gX_PhysicsBody_GetPosition(g_emBodies[j].body);
            gX_F32 dx = posI.x - posJ.x;
            gX_F32 dy = posI.y - posJ.y;
            gX_F32 dz = posI.z - posJ.z;
            gX_F32 r2 = dx * dx + dy * dy + dz * dz;
            //ai: coincident bodies have no defined separation direction.
            if (r2 <= 1e-8f) continue;
            gX_F32 invR = 1.0f / sqrtf(r2);
            gX_F32 qj = g_emBodies[j].charge;
            if (qi != 0.0f || qj != 0.0f) {
                gX_F32 magnitude = scale * qi * qj / (r2 + softening2);
                gX_F32v3 direction = { dx * invR, dy * invR, dz * invR };
                gX_F32v3 force = emClamp_((gX_F32v3){
                    direction.x * magnitude,
                    direction.y * magnitude,
                    direction.z * magnitude
                }, maxForce);
                gX_PhysicsBody_ApplyForce(g_emBodies[i].body, force);
                gX_PhysicsBody_ApplyForce(g_emBodies[j].body,
                    (gX_F32v3){ -force.x, -force.y, -force.z });
            }
            gX_F32v3 mj = moments[j];
            gX_F32 alphaI = g_emBodies[i].susceptibility
                * g_emSettings.susceptibilityScale;
            gX_F32 alphaJ = g_emBodies[j].susceptibility
                * g_emSettings.susceptibilityScale;
            gX_I1 miZero = (mi.x == 0.0f && mi.y == 0.0f && mi.z == 0.0f);
            gX_I1 mjZero = (mj.x == 0.0f && mj.y == 0.0f && mj.z == 0.0f);
            if (miZero && mjZero && alphaI == 0.0f && alphaJ == 0.0f) {
                continue;
            }
            //ai: separation unit vector from i to j and the softened r^-3/-4.
            gX_F32 rx = -dx * invR;
            gX_F32 ry = -dy * invR;
            gX_F32 rz = -dz * invR;
            gX_F32 r2s = r2 + softening2;
            gX_F32 invR2s = 1.0f / r2s;
            gX_F32 invR3s = invR2s / sqrtf(r2s);
            gX_F32 m1r = mi.x * rx + mi.y * ry + mi.z * rz;
            gX_F32 m2r = mj.x * rx + mj.y * ry + mj.z * rz;
            //ai: induced moments — a magnetizable body polarizes along the
            //   other's field (m_ind = alpha * B); the effective moments feed
            //   the same pair laws, which is the classic iron-attraction pull.
            gX_F32v3 miEff = mi;
            gX_F32v3 mjEff = mj;
            if (alphaI != 0.0f && !mjZero) {
                miEff.x = mi.x + alphaI * dipoleScale * invR3s
                    * (3.0f * m2r * rx - mj.x);
                miEff.y = mi.y + alphaI * dipoleScale * invR3s
                    * (3.0f * m2r * ry - mj.y);
                miEff.z = mi.z + alphaI * dipoleScale * invR3s
                    * (3.0f * m2r * rz - mj.z);
            }
            if (alphaJ != 0.0f && !miZero) {
                mjEff.x = mj.x + alphaJ * dipoleScale * invR3s
                    * (3.0f * m1r * rx - mi.x);
                mjEff.y = mj.y + alphaJ * dipoleScale * invR3s
                    * (3.0f * m1r * ry - mi.y);
                mjEff.z = mj.z + alphaJ * dipoleScale * invR3s
                    * (3.0f * m1r * rz - mi.z);
            }
            gX_F32 e1r = miEff.x * rx + miEff.y * ry + miEff.z * rz;
            gX_F32 e2r = mjEff.x * rx + mjEff.y * ry + mjEff.z * rz;
            gX_F32 e12 = miEff.x * mjEff.x + miEff.y * mjEff.y
                + miEff.z * mjEff.z;
            gX_F32 k = 3.0f * dipoleScale * invR2s * invR2s;
            gX_F32v3 pairForce = emClamp_((gX_F32v3){
                k * (e1r * mjEff.x + e2r * miEff.x + e12 * rx
                    - 5.0f * e1r * e2r * rx),
                k * (e1r * mjEff.y + e2r * miEff.y + e12 * ry
                    - 5.0f * e1r * e2r * ry),
                k * (e1r * mjEff.z + e2r * miEff.z + e12 * rz
                    - 5.0f * e1r * e2r * rz)
            }, maxForce);
            gX_PhysicsBody_ApplyForce(g_emBodies[i].body,
                (gX_F32v3){ -pairForce.x, -pairForce.y, -pairForce.z });
            gX_PhysicsBody_ApplyForce(g_emBodies[j].body, pairForce);

            //ai: torque = m x B from the other dipole's field at this body.
            gX_F32v3 fieldI = {
                dipoleScale * invR3s * (3.0f * e1r * rx - miEff.x),
                dipoleScale * invR3s * (3.0f * e1r * ry - miEff.y),
                dipoleScale * invR3s * (3.0f * e1r * rz - miEff.z)
            };
            gX_F32v3 fieldJ = {
                dipoleScale * invR3s * (3.0f * e2r * rx - mjEff.x),
                dipoleScale * invR3s * (3.0f * e2r * ry - mjEff.y),
                dipoleScale * invR3s * (3.0f * e2r * rz - mjEff.z)
            };
            gX_F32v3 torqueI = emClamp_((gX_F32v3){
                miEff.y * fieldJ.z - miEff.z * fieldJ.y,
                miEff.z * fieldJ.x - miEff.x * fieldJ.z,
                miEff.x * fieldJ.y - miEff.y * fieldJ.x
            }, maxForce);
            gX_F32v3 torqueJ = emClamp_((gX_F32v3){
                mjEff.y * fieldI.z - mjEff.z * fieldI.y,
                mjEff.z * fieldI.x - mjEff.x * fieldI.z,
                mjEff.x * fieldI.y - mjEff.y * fieldI.x
            }, maxForce);
            if (!miZero || alphaI != 0.0f) {
                gX_PhysicsBody_ApplyTorque(g_emBodies[i].body, torqueI);
            }
            if (!mjZero || alphaJ != 0.0f) {
                gX_PhysicsBody_ApplyTorque(g_emBodies[j].body, torqueJ);
            }
        }
    }

    //ai: uniform-field terms — the charge sees F = q (v x B); a dipole sees
    //   the aligning torque m x B.
    gX_I1 hasB = (bField.x != 0.0f || bField.y != 0.0f || bField.z != 0.0f);
    if (hasB) {
        for (gX_I32 i = 0; i < g_emCount; i++) {
            if (g_emBodies[i].body.internal == 0) continue;
            if (g_emBodies[i].charge != 0.0f) {
                gX_F32v3 velocity = gX_PhysicsBody_GetLinearVel(g_emBodies[i].body);
                gX_F32 q = g_emBodies[i].charge;
                gX_F32v3 force = emClamp_((gX_F32v3){
                    q * (velocity.y * bField.z - velocity.z * bField.y),
                    q * (velocity.z * bField.x - velocity.x * bField.z),
                    q * (velocity.x * bField.y - velocity.y * bField.x)
                }, maxForce);
                gX_PhysicsBody_ApplyForce(g_emBodies[i].body, force);
            }
            if (!anyDipole) continue;
            gX_F32v3 m = moments[i];
            if (m.x == 0.0f && m.y == 0.0f && m.z == 0.0f) continue;
            gX_PhysicsBody_ApplyTorque(g_emBodies[i].body, emClamp_((gX_F32v3){
                m.y * bField.z - m.z * bField.y,
                m.z * bField.x - m.x * bField.z,
                m.x * bField.y - m.y * bField.x
            }, maxForce));
        }
    }
}
