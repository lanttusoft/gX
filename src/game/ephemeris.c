/**********************************************************************************************
*
*   [gX: src/game/ephemeris.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


//opencode: Keplerian orbital mechanics + eclipse prediction system
//   All bodies use Keplerian orbital elements. The system computes heliocentric
//   3D positions, then derives direction vectors, angular sizes, phases, and
//   eclipse status from the observer's viewpoint.
//
//   Reference plane: ecliptic (XZ plane, Y = ecliptic north)
//   NOTE: code uses XZ as the orbit plane (PositionInPlane returns (x,0,z)),
//   then RotateToOrbit tilts around X (pushing Z→Y) then rotates around Z.
//   Final axes: X=vernal equinox, Y=ecliptic north, Z=complement.
//   Coordinate system: right-handed, X toward vernal equinox, Z up

#include <math.h>
#include <string.h>

#include "ephemeris.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"
#include "../gxmath/constants.h"

//opencode: solve Kepler's equation M = E - e*sin(E) for eccentric anomaly E
//   Uses Newton-Raphson, typically converges in 4-6 iterations for e < 0.9
gX_F32 gX_Ephemeris_SolveKepler(gX_F32 M, gX_F32 e) {
    // Normalize mean anomaly to [0, 2π)
    M = fmodf(M, 2.0f * gX_PI);
    if (M < 0.0f) M += 2.0f * gX_PI;

    // Initial guess: E = M for nearly circular, improved for elliptical
    gX_F32 E = M;
    if (e > 0.1f) {
        E = M + e * sinf(M);
    }

    // Newton-Raphson iteration
    for (gX_I32 i = 0; i < 10; i++) {
        gX_F32 dE = (M - E + e * sinf(E)) / (1.0f - e * cosf(E));
        E += dE;
        if (fabsf(dE) < 1.0e-7f) break;
    }
    return E;
}

//opencode: compute position in orbital plane from eccentric anomaly
gX_F32v3 gX_Ephemeris_PositionInPlane(gX_F32 a, gX_F32 e, gX_F32 E) {
    gX_F32 x = a * (cosf(E) - e);
    gX_F32 y = a * sqrtf(1.0f - e * e) * sinf(E);
    // In orbital plane: x along periapsis direction, y perpendicular, z=0
    return (gX_F32v3){x, 0.0f, y};
}

//opencode: rotate orbital-plane position into 3D using Ω, i, ω
//   Applies R_z(Ω) * R_x(i) * R_z(ω) to the position vector
gX_F32v3 gX_Ephemeris_RotateToOrbit(gX_F32v3 p, gX_F32 Omega, gX_F32 inc, gX_F32 omega) {
    // Step 1: Rotate by argument of periapsis (ω) in orbital plane
    gX_F32 cosW = cosf(omega), sinW = sinf(omega);
    gX_F32 x1 = p.x * cosW - p.z * sinW;
    gX_F32 z1 = p.x * sinW + p.z * cosW;
    // y1 = p.y = 0 (position is in orbital plane)

    // Step 2: Rotate by inclination (i) around X axis
    // R_x(i): (x, y, z) -> (x, y*cos(i) - z*sin(i), y*sin(i) + z*cos(i))
    // With p = (x1, 0, z1): y2 = -z1*sinI, z2 = z1*cosI
    gX_F32 cosI = cosf(inc), sinI = sinf(inc);
    gX_F32 y2 = -z1 * sinI;
    gX_F32 z2 = z1 * cosI;
    gX_F32 x2 = x1;

    // Step 3: Rotate by longitude of ascending node (Ω) around Z axis
    gX_F32 cosO = cosf(Omega), sinO = sinf(Omega);
    gX_F32 x3 = x2 * cosO - y2 * sinO;
    gX_F32 y3 = x2 * sinO + y2 * cosO;
    gX_F32 z3 = z2;

    return (gX_F32v3){x3, y3, z3};
}

//opencode: compute position of a body in heliocentric coordinates at a given time
//   Returns absolute world position (heliocentric).
static gX_F32v3 gX_Ephemeris_ComputePosition(gX_Ephemeris_Orbit *orbit, gX_F64 timeSec) {
    // Time since epoch in seconds
    gX_F64 dt = timeSec;

    // Mean anomaly at time: M = M₀ + 2π * t / T
    gX_F32 twoPi = 2.0f * gX_PI;
    gX_F32 M = orbit->meanAnomaly0 + (gX_F32)((gX_F64)twoPi * dt / (gX_F64)orbit->orbitalPeriodSec);

    // Solve Kepler's equation for eccentric anomaly
    gX_F32 E = gX_Ephemeris_SolveKepler(M, orbit->eccentricity);

    // Position in orbital plane
    gX_F32v3 posPlane = gX_Ephemeris_PositionInPlane(orbit->semiMajorAxis, orbit->eccentricity, E);

    // Rotate to 3D
    return gX_Ephemeris_RotateToOrbit(posPlane, orbit->ascendingNode, orbit->inclination, orbit->periapsis);
}

//opencode: compute illuminated fraction (phase) of a body as seen from observer
//   Uses angle between body-observer direction and body-sun direction.
//   0 = dark (body between observer and sun / new phase)
//   1 = full illumination (observer between body and sun / full phase)
gX_F32 gX_Ephemeris_ComputePhase(gX_F32v3 observerPos, gX_F32v3 bodyPos, gX_F32v3 sunPos) {
    gX_F32v3 toSun = gX_F32v3_Normalize(gX_F32v3_Sub(sunPos, bodyPos));
    gX_F32v3 toObs = gX_F32v3_Normalize(gX_F32v3_Sub(observerPos, bodyPos));
    gX_F32 cosPhase = gX_F32v3_Dot(toSun, toObs);
    // phase = (1 + cosPhase) / 2 → 0 when body between observer and sun, 1 when observer between body and sun
    return (1.0f + cosPhase) * 0.5f;
}

//opencode: build a single body in the system
static gX_I32 gX_Ephemeris_AddBody(gX_Ephemeris *eph, const gX_I8 *name, gX_Ephemeris_BodyType type,
    gX_I32 parentIndex, gX_F32 a, gX_F32 e, gX_F32 inc, gX_F32 Omega, gX_F32 omega,
    gX_F32 M0, gX_F64 periodSec, gX_F32 radius, gX_F32 visSize, gX_F32v3 color, gX_F32 brightness)
{
    if (eph->bodyCount >= GX_EPHEMERIS_MAX_BODIES) return -1;
    gX_I32 idx = (gX_I32)eph->bodyCount;
    gX_Ephemeris_Body *body = &eph->bodies[idx];

    strcpy(body->name, name);
    strcpy(body->description, "");
    body->type = type;
    body->parentIndex = parentIndex;
    body->orbit.semiMajorAxis = a;
    body->orbit.eccentricity = e;
    body->orbit.inclination = inc;
    body->orbit.ascendingNode = Omega;
    body->orbit.periapsis = omega;
    body->orbit.meanAnomaly0 = M0;
    body->orbit.orbitalPeriodSec = periodSec;
    body->radius = radius;
    body->visualSize = visSize;
    body->color = color;
    body->brightness = brightness;

    eph->bodyCount++;
    return idx;
}

//opencode: populate with Sol/Earth/Moon + Venus + Mars defaults
//   Orbital data approximates real solar system values. Periods and scales
//   are in game-seconds and game-distance-units. The game can tune these
//   after loading.
//
//   Bodies:
//     0 = Sun (at origin, not orbiting)
//     1 = Earth (orbits Sun, 1 AU)
//     2 = Moon (orbits Earth)
//     3 = Venus (orbits Sun, inner planet)
//     4 = Mars (orbits Sun, outer planet)
gX_I32 gX_Ephemeris_SetupDefaults(gX_Ephemeris *eph) {
    // Clear any existing bodies
    eph->bodyCount = 0;

    // Reference orbital period: Earth period = 36000 game-seconds (10 hours = 1 year)
    // This gives a visible planetary motion rate. Moon period is 27.3/365.25 of Earth's.
    gX_F64 earthPeriod = 36000.0;

    // Start all bodies at mean anomaly = 0 (aligned for easy testing)
    gX_F32 M0 = 0.0f;

    //--- Sun (index 0) — parked at origin, not orbiting ---
    gX_Ephemeris_AddBody(eph,
        "Sun", GX_EPHEMERIS_SUN,
        -1,                    // no parent
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0,                   // period placeholder (unused for stationary)
        696340.0f,             // physical radius (km, real)
        800.0f,                // visual angular size factor
        (gX_F32v3){1.0f, 0.85f, 0.5f},  // warm yellow-white
        3.0f);

    //--- Earth (index 1) — orbits Sun ---
    gX_Ephemeris_AddBody(eph,
        "Earth", GX_EPHEMERIS_PLANET,
        0,                     // parent = Sun
        1.0f,                  // semi-major axis = 1 AU
        0.0167f,               // eccentricity (real Earth)
        0.0f,                  // inclination (reference plane = ecliptic)
        0.0f,                  // Ω (vernal equinox reference)
        1.796f,                // ω ≈ 103° (perihelion ~Jan 4)
        M0,
        earthPeriod,           // orbital period = 1 year
        6371.0f,               // radius (km, real)
        0.0f,                  // Earth itself has no visible disk from its surface
        (gX_F32v3){0.2f, 0.4f, 0.8f},
        0.0f);

    //--- Moon (index 2) — orbits Earth ---
    gX_Ephemeris_AddBody(eph,
        "Moon", GX_EPHEMERIS_MOON,
        1,                     // parent = Earth
        0.00257f,              // semi-major axis ≈ 384,400 km / 149,597,870 km per AU
        0.0549f,               // eccentricity (real Moon)
        0.0898f,               // inclination ≈ 5.145° to ecliptic
        0.0f,                  // Ω (simplified; precesses slowly in reality)
        0.0f,                  // ω
        M0 + 0.5f,             // offset: start moon at different position
        earthPeriod * 27.321661 / 365.25,  // ~27.3 day period
        1737.0f,               // radius (km, real)
        700.0f,                // visual angular size factor
        (gX_F32v3){0.85f, 0.85f, 0.92f},  // pale white-blue
        0.5f);                 // brightness (much dimmer than sun intensity=3.0)

    //--- Venus (index 3) — orbits Sun, inner planet ---
    gX_Ephemeris_AddBody(eph,
        "Venus", GX_EPHEMERIS_PLANET,
        0,                     // parent = Sun
        0.723f,                // semi-major axis (AU)
        0.0068f,               // eccentricity (lowest in solar system)
        0.0592f,               // inclination ≈ 3.39°
        0.0f,                  // Ω
        1.338f,                // ω ≈ 76.7°
        M0 + 2.0f,             // offset position
        earthPeriod * 0.615,   // ~225 Earth days
        6052.0f,               // radius (km)
        300.0f,                // visual angular size
        (gX_F32v3){0.9f, 0.8f, 0.6f},  // pale yellowish
        1.5f);

    //--- Mars (index 4) — orbits Sun, outer planet ---
    gX_Ephemeris_AddBody(eph,
        "Mars", GX_EPHEMERIS_PLANET,
        0,                     // parent = Sun
        1.524f,                // semi-major axis (AU)
        0.0934f,               // eccentricity (moderate)
        0.0323f,               // inclination ≈ 1.85°
        0.0f,                  // Ω
        0.865f,                // ω ≈ 49.6°
        M0 + 4.0f,             // offset position
        earthPeriod * 1.881,   // ~687 Earth days
        3390.0f,               // radius (km)
        200.0f,                // visual angular size
        (gX_F32v3){0.9f, 0.4f, 0.2f},  // reddish
        1.2f);

    gX_TraceLog(gX_LOG_INFO, "EPHEMERIS: default setup done (%u bodies)", eph->bodyCount);
    return 0;
}

//opencode: initialize the ephemeris system
gX_I32 gX_Ephemeris_Init(gX_Ephemeris *eph) {
    memset(eph, 0, sizeof(gX_Ephemeris));
    eph->observerPos = (gX_F32v3){0.0f, 0.0f, 0.0f};
    eph->timeSec = 0.0;
    eph->primaryIndex = 1;    // Earth is the primary (shadow-casting) body
    eph->primaryRadius = 6371.0f;
    eph->eclipsesEnabled = true;
    return 0;
}

//opencode: compute the absolute world position of a body in heliocentric coordinates.
//   Handles orbital chains: Moon orbits Earth, Earth orbits Sun.
//   Uses recursion: body position = parent position + orbital offset around parent.
gX_F32v3 gX_Ephemeris_GetAbsolutePosition(gX_Ephemeris *eph, gX_I32 bodyIdx, gX_F64 timeSec) {
    if (bodyIdx < 0 || bodyIdx >= (gX_I32)eph->bodyCount) {
        return (gX_F32v3){0.0f, 0.0f, 0.0f};
    }

    gX_Ephemeris_Body *body = &eph->bodies[bodyIdx];

    // Sun (or body with no parent) — at origin if sun, or compute orbit
    if (body->parentIndex < 0) {
        // No parent: this is the central body (Sun) — park at origin
        return (gX_F32v3){0.0f, 0.0f, 0.0f};
    }

    // Compute orbital position around parent
    gX_F32v3 localPos = gX_Ephemeris_ComputePosition(&body->orbit, timeSec);

    // Add parent's absolute position (recursive)
    gX_F32v3 parentPos = gX_Ephemeris_GetAbsolutePosition(eph, body->parentIndex, timeSec);

    return gX_F32v3_Add(parentPos, localPos);
}

//opencode: compute the angular radius of a body as seen from observer
static gX_F32 gX_Ephemeris_ComputeAngularRadius(gX_F32 bodyRadius, gX_F32v3 observerPos, gX_F32v3 bodyPos) {
    gX_F32 dist = gX_F32v3_Distance(observerPos, bodyPos);
    if (dist < 0.0001f) return 0.0f;
    return atan2f(bodyRadius, dist);
}

//opencode: check if a body is in the shadow of the primary (Earth) relative to the Sun.
//   Solar eclipse: body is Moon, it passes between observer and Sun.
//   Lunar eclipse: body is Moon, it passes through Earth's umbra.
//   Returns: 0 = no eclipse, 0..1 = partial, 1 = total
static gX_F32 gX_Ephemeris_CheckEclipseBody(gX_Ephemeris *eph, gX_Ephemeris_Body *body, gX_Ephemeris_Body *sun) {
    if (!eph->eclipsesEnabled) return 0.0f;
    if (eph->primaryIndex < 0 || eph->primaryIndex >= (gX_I32)eph->bodyCount) return 0.0f;

    gX_Ephemeris_Body *primary = &eph->bodies[eph->primaryIndex];
    if (!primary) return 0.0f;

    // Vector from observer to body and to sun
    gX_F32v3 toBody = gX_F32v3_Normalize(gX_F32v3_Sub(body->position, eph->observerPos));
    gX_F32v3 toSun = gX_F32v3_Normalize(gX_F32v3_Sub(sun->position, eph->observerPos));

    // Angular separation between body and sun
    gX_F32 separation = acosf(gX_F32v3_Dot(toBody, toSun));

    // --- Solar eclipse check (Moon between observer and Sun) ---
    // The moon is eclipsing the sun when their angular separation is small enough
    if (body->type == GX_EPHEMERIS_MOON && separation < (sun->angularRadius + body->angularRadius) * 1.5f) {
        // Check distance: moon must be closer than sun
        gX_F32 distToBody = gX_F32v3_Distance(eph->observerPos, body->position);
        gX_F32 distToSun = gX_F32v3_Distance(eph->observerPos, sun->position);
        if (distToBody < distToSun) {
            gX_F32 overlap = (sun->angularRadius + body->angularRadius) - separation;
            return (overlap > 0.0f) ? fminf(overlap / (body->angularRadius * 2.0f), 1.0f) : 0.0f;
        }
    }

    // --- Lunar eclipse check (Moon passes through Earth's shadow) ---
    // Moon must be roughly opposite the Sun from observer
    // Check if Moon passes through Earth's umbra/penumbra cone
    if (body->type == GX_EPHEMERIS_MOON && gX_F32v3_Dot(toBody, toSun) < -0.99f) {
        // Moon is near opposition to the Sun
        // Compute Moon's position relative to Earth's shadow axis
        gX_F32v3 earthPos = primary->position;
        gX_F32v3 shadowAxis = gX_F32v3_Normalize(gX_F32v3_Sub(sun->position, earthPos));

        // Vector from Earth to Moon
        gX_F32v3 earthToMoon = gX_F32v3_Sub(body->position, earthPos);
        gX_F32v3 projAlong = gX_F32v3_Scale(shadowAxis, gX_F32v3_Dot(earthToMoon, shadowAxis));
        gX_F32v3 perp = gX_F32v3_Sub(earthToMoon, projAlong);
        gX_F32 perpDist = gX_F32v3_Length(perp);

        // Distance from Earth to Moon along shadow axis
        gX_F32 alongDist = gX_F32v3_Dot(earthToMoon, shadowAxis);

        // Umbra radius at Moon's distance:
        // Earth's radius * (1 - alongDist / sunDistance)
        // Simplified: using geometric shadow cone
        gX_F32 distToSun = gX_F32v3_Distance(earthPos, sun->position);
        gX_F32 umbraRadius = eph->primaryRadius * (1.0f - alongDist / distToSun);
        if (umbraRadius < 0.0f) umbraRadius = 0.0f;

        gX_F32 penumbraRadius = umbraRadius + eph->primaryRadius * 0.02f; // approximated

        if (perpDist < penumbraRadius + body->radius) {
            // In shadow — compute eclipse depth
            if (perpDist + body->radius < umbraRadius) {
                return 1.0f; // total lunar eclipse
            }
            // Partial
            gX_F32 overlap = (penumbraRadius + body->radius) - perpDist;
            return fminf(overlap / (body->radius * 2.0f), 1.0f);
        }
    }

    return 0.0f;
}

//opencode: update all body positions and compute derived values (direction, phase, eclipse)
gX_I0 gX_Ephemeris_Update(gX_Ephemeris *eph, gX_F64 inGameTimeSec) {
    eph->timeSec = inGameTimeSec;

    // Step 1: Compute absolute positions for all bodies
    for (gX_U32 i = 0; i < eph->bodyCount; i++) {
        eph->bodies[i].position = gX_Ephemeris_GetAbsolutePosition(eph, (gX_I32)i, eph->timeSec);
    }

    // Find the Sun body (index 0 expected, but search to be safe)
    gX_Ephemeris_Body *sun = NULL;
    gX_I32 sunIdx = -1;
    for (gX_U32 i = 0; i < eph->bodyCount; i++) {
        if (eph->bodies[i].type == GX_EPHEMERIS_SUN) {
            sun = &eph->bodies[i];
            sunIdx = (gX_I32)i;
            break;
        }
    }

    // Step 2: Compute direction from observer and angular radius
    for (gX_U32 i = 0; i < eph->bodyCount; i++) {
        gX_Ephemeris_Body *body = &eph->bodies[i];

        // Direction: normalized vector FROM observer TO body
        gX_F32v3 dir = gX_F32v3_Sub(body->position, eph->observerPos);
        gX_F32 dirLen = gX_F32v3_Length(dir);
        if (dirLen > 0.0001f) {
            body->direction = gX_F32v3_Scale(dir, 1.0f / dirLen);
        } else {
            // Observer is at body position — default to up to avoid NaN
            body->direction = (gX_F32v3){0.0f, 1.0f, 0.0f};
        }

        // Angular radius (apparent size from observer)
        body->angularRadius = gX_Ephemeris_ComputeAngularRadius(body->radius, eph->observerPos, body->position);

        // Phase (illuminated fraction)
        if (sun && i != (gX_U32)sunIdx && body->radius > 0.0f) {
            body->phase = gX_Ephemeris_ComputePhase(eph->observerPos, body->position, sun->position);
        } else {
            body->phase = 1.0f;
        }

        // Eclipse check
        if (sun && i != (gX_U32)sunIdx && body->type == GX_EPHEMERIS_MOON) {
            body->eclipseDepth = gX_Ephemeris_CheckEclipseBody(eph, body, sun);
            body->inEclipse = (body->eclipseDepth > 0.01f) ? 1 : 0;
        } else {
            body->inEclipse = 0;
            body->eclipseDepth = 0.0f;
        }
    }
}

//opencode: find a body by name
gX_Ephemeris_Body* gX_Ephemeris_GetBody(gX_Ephemeris *eph, const gX_I8 *name) {
    for (gX_U32 i = 0; i < eph->bodyCount; i++) {
        if (strcmp(eph->bodies[i].name, name) == 0) {
            return &eph->bodies[i];
        }
    }
    return NULL;
}

//opencode: get the direction vector FROM observer TO a named body
gX_F32v3 gX_Ephemeris_GetBodyDirection(gX_Ephemeris *eph, const gX_I8 *name) {
    gX_Ephemeris_Body *body = gX_Ephemeris_GetBody(eph, name);
    if (!body) return (gX_F32v3){0.0f, 1.0f, 0.0f}; // default up if not found
    return body->direction;
}

//opencode: check if a named body is currently in eclipse
gX_I1 gX_Ephemeris_IsEclipsed(gX_Ephemeris *eph, const gX_I8 *name) {
    gX_Ephemeris_Body *body = gX_Ephemeris_GetBody(eph, name);
    if (!body) return 0;
    return body->inEclipse;
}
