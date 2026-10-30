/**********************************************************************************************
*
*   [gX: src/game/ephemeris.h]
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


#ifndef GAME_EPHEMERIS_H
#define GAME_EPHEMERIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: maximum celestial bodies tracked by the ephemeris system
#define GX_EPHEMERIS_MAX_BODIES 8

//opencode: Keplerian orbital elements defining a body's orbit around its parent
typedef struct gX_Ephemeris_Orbit {
    gX_F32 semiMajorAxis;       // orbit scale (AU or game units)
    gX_F32 eccentricity;        // 0=circle, 0..1=elliptical
    gX_F32 inclination;         // tilt from reference plane (radians)
    gX_F32 ascendingNode;       // Ω: longitude of ascending node (radians)
    gX_F32 periapsis;           // ω: argument of periapsis (radians)
    gX_F32 meanAnomaly0;        // M₀: mean anomaly at epoch (radians)
    gX_F64 orbitalPeriodSec;    // orbital period in game-seconds
} gX_Ephemeris_Orbit;

//opencode: body type classification
typedef enum {
    GX_EPHEMERIS_SUN = 0,
    GX_EPHEMERIS_MOON,
    GX_EPHEMERIS_PLANET,
} gX_Ephemeris_BodyType;

//opencode: a celestial body in the ephemeris model
typedef struct gX_Ephemeris_Body {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_Ephemeris_BodyType type;

    gX_I32 parentIndex;         // index of parent body (-1 = no parent, heliocentric)
    gX_Ephemeris_Orbit orbit;   // Keplerian orbital elements

    // Visual parameters
    gX_F32 radius;              // physical radius (for shadow/eclipse geometry)
    gX_F32 visualSize;          // angular size multiplier for rendering
    gX_F32v3 color;             // RGB tint
    gX_F32 brightness;          // brightness multiplier

    // Computed state (filled by gX_Ephemeris_Update)
    gX_F32v3 position;          // absolute world position (heliocentric coordinates)
    gX_F32v3 direction;         // normalized direction FROM observer TO body
    gX_F32   angularRadius;     // apparent angular radius (radians)
    gX_F32   phase;             // illuminated fraction visible from observer [0..1]
    gX_I1    inEclipse;         // 1 if currently in an eclipse
    gX_F32   eclipseDepth;      // 0=no eclipse, 1=total

} gX_Ephemeris_Body;

//opencode: the complete ephemeris system — orbits and eclipse prediction
typedef struct gX_Ephemeris {
    gX_U32 bodyCount;
    gX_Ephemeris_Body bodies[GX_EPHEMERIS_MAX_BODIES];

    gX_F32v3 observerPos;       // observer location in world coordinates
    gX_F64   timeSec;           // current in-game epoch time (seconds)

    // Eclipse configuration: the "primary" body casts a shadow
    gX_I32   primaryIndex;      // index of primary body (e.g. Earth)
    gX_F32   primaryRadius;     // radius of the primary body

    gX_I1    eclipsesEnabled;   // toggle eclipse prediction

} gX_Ephemeris;

// Lifecycle
gX_I32 gX_Ephemeris_Init(gX_Ephemeris *eph);

//opencode: populate with Sol/Earth/Moon/Venus/Mars defaults (real-ish data, game-scalable)
gX_I32 gX_Ephemeris_SetupDefaults(gX_Ephemeris *eph);

// Per-frame update: compute all body positions and check eclipses
gX_I0 gX_Ephemeris_Update(gX_Ephemeris *eph, gX_F64 inGameTimeSec);

//opencode: solve Kepler's equation M = E - e*sin(E) for eccentric anomaly E
//   Uses Newton's method, converges in ~5 iterations for e < 0.9
gX_F32 gX_Ephemeris_SolveKepler(gX_F32 meanAnomaly, gX_F32 eccentricity);

//opencode: compute 3D position in orbital frame from eccentric anomaly
//   Returns position in the orbital plane (before Ω/i/ω rotation)
gX_F32v3 gX_Ephemeris_PositionInPlane(gX_F32 semiMajorAxis, gX_F32 eccentricity, gX_F32 eccentricAnomaly);

//opencode: rotate orbital-plane position into 3D using orbital elements
gX_F32v3 gX_Ephemeris_RotateToOrbit(gX_F32v3 posInPlane, gX_F32 ascendingNode, gX_F32 inclination, gX_F32 periapsis);

//opencode: compute illuminated fraction of a body from observer's viewpoint
//   0 = dark (body between observer and sun, e.g. new moon)
//   1 = fully lit (observer between body and sun, e.g. full moon)
gX_F32 gX_Ephemeris_ComputePhase(gX_F32v3 observerPos, gX_F32v3 bodyPos, gX_F32v3 sunPos);

// Queries
gX_Ephemeris_Body* gX_Ephemeris_GetBody(gX_Ephemeris *eph, const gX_I8 *name);
gX_F32v3 gX_Ephemeris_GetBodyDirection(gX_Ephemeris *eph, const gX_I8 *name);
gX_I1 gX_Ephemeris_IsEclipsed(gX_Ephemeris *eph, const gX_I8 *name);

//opencode: compute absolute heliocentric position of a body at a given time
gX_F32v3 gX_Ephemeris_GetAbsolutePosition(gX_Ephemeris *eph, gX_I32 bodyIdx, gX_F64 timeSec);

#ifdef __cplusplus
}
#endif

#endif //GAME_EPHEMERIS_H
