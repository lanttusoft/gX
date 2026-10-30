/**********************************************************************************************
*
*   [gX: src/physics/physics.h]
*   Physics module — shared simulation world with a fixed-timestep accumulator
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


#ifndef GX_PHYSICS_H
#define GX_PHYSICS_H

//ai: physics engine module — singleton holding the shared simulation
//   world. Scenes spawn bodies into this world and read transforms for
//   rendering. The update loop drives a fixed timestep accumulator for
//   deterministic behavior.

#include "../common/types.h"
#include "../wrappers/physics.h"

typedef struct gX_Physics {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 enabled;
    gX_I1 pause;

    gX_PhysicsWorld world;

    //ai: fixed timestep duration. Default 1/60 second.
    gX_F32 fixedDt;
    gX_F64 accumulator;
    gX_I32 maxSubSteps;
    gX_I32 stepsLastFrame;

    //ai: auto-tuning — quality levels react to sustained frame time.
    //   tuningPin >= 0 (GX_PHYSICS_TUNE) disables the adaptive changes;
    //   tuningGoodTicks counts consecutive evaluations at/under target so
    //   recovery needs a sustained window, not a single good sample.
    gX_I32 tuningLevel;       // 0=max quality, 3=min quality
    gX_I32 tuningPin;         // -1 = adaptive, 0..3 = pinned
    gX_I32 tuningGoodTicks;   // consecutive evaluations at/under target
    gX_F64 frameTimeHistory;  // smoothed frame time (exponential moving avg)
    gX_U32 frameCount;

} gX_Physics;

extern gX_Physics physics;

gX_I32 gX_Physics_Init(gX_I0);
gX_I32 gX_Physics_Shutdown(gX_I0);
gX_I0  gX_Physics_Update(gX_I0);
gX_I0  gX_Physics_StepOnce(gX_I0);

//ai: helper — rotate a gX_Matrix from a body's quaternion + position
gX_Matrix gX_Physics_MatrixFromBody(gX_PhysicsBody body);

//ai: optional per-substep hook — called once before every fixed step with
//   the step dt, so force/thermal appliers run at the simulation rate and
//   not once per frame (a per-frame call would be consumed by the first
//   sub-step only). Pass NULL to clear.
typedef gX_I0 (*gX_PhysicsStepHook)(gX_I0 *userData, gX_F32 dt);
gX_I0 gX_Physics_SetStepHook(gX_PhysicsStepHook hook, gX_I0 *userData);
//ai: apply a quality level (0 = max quality .. 3) to the live world —
//   exported for the physics editor's tune pin (the auto path calls it on
//   level changes; a pinned level applies it directly).
gX_I0 gX_Physics_ApplyTuning(gX_I32 level);
//ai: interpolation alpha for smooth rendering when physics rate
//   is lower than the display rate. Returns [0..1] where 0 = previous
//   physics state, 1 = current physics state.
gX_F32 gX_Physics_GetAlpha(gX_I0);

//ai: reset the frame time history — call after a heavy scene
//   switch frame so the auto-tuning doesn't get stuck at low quality
//   from a single bad sample (unload + load spike).
gX_I0 gX_Physics_ResetFrameHistory(gX_I0);

#endif //GX_PHYSICS_H
