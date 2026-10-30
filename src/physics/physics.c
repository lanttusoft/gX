/**********************************************************************************************
*
*   [gX: src/physics/physics.c]
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


//ai: physics engine module — shared simulation world managed by a
//   fixed-timestep accumulator. Scenes can spawn bodies via the wrapper API.
#include "physics.h"

#include <stdlib.h>
#include <string.h>

#include "../common/log.h"
#include "../gxmath/matrix.h"
#include "../gxmath/quaternion.h"
#include "../wrappers/wrappers.h"

gX_Physics physics;

//ai: step hook — see gX_Physics_SetStepHook. Cleared with the module.
static gX_PhysicsStepHook g_physicsStepHook = NULL;
static gX_I0 *g_physicsStepHookUser = NULL;

gX_I0 gX_Physics_SetStepHook(gX_PhysicsStepHook hook, gX_I0 *userData) {
    g_physicsStepHook = hook;
    g_physicsStepHookUser = userData;
}

//ai: apply a tuning level — quality levers only. The contact count is the
//   solver-cost lever that matters at gX's body counts; the threaded solver
//   is deliberately not part of the auto path (ODE's threading overhead
//   outweighs its win below ~1k bodies) and stays available explicitly via
//   gX_PhysicsWorld_SetThreading. Sub-step count is capped harder under
//   load so a catch-up frame cannot multiply solver work.
//ai: exported for the physics editor's quality pin (the auto path calls it
//   on level changes; a pinned level applies it directly).
gX_I0 gX_Physics_ApplyTuning(gX_I32 level) {
    switch (level) {
        case 0:
            physics.maxSubSteps = 4;
            gX_PhysicsWorld_SetMaxContacts(physics.world, 8);
            break;
        case 1:
            physics.maxSubSteps = 4;
            gX_PhysicsWorld_SetMaxContacts(physics.world, 4);
            break;
        default:
            physics.maxSubSteps = 2;
            gX_PhysicsWorld_SetMaxContacts(physics.world, 2);
            break;
    }
}

gX_I32 gX_Physics_Init(gX_I0) {
    gX_TRACELOG_PROLOGUE("Physics_Init", "PHYSICS: Initializing physics engine")

    memset(&physics, 0, sizeof(physics));
    strcpy(physics.name, "Physics");
    strcpy(physics.description, "Shared physics simulation world");

    physics.fixedDt = 1.0f / 60.0f;  //ai: 60 Hz fixed simulation rate
    physics.accumulator = 0.0;
    physics.maxSubSteps = 4;
    physics.stepsLastFrame = 0;
    physics.pause = false;

    //ai: GX_PHYSICS_TUNE=<0..3> pins the quality level for captures/audits;
    //   unset (or invalid) leaves the adaptive tuner in charge.
    physics.tuningPin = -1;
    const gX_I8 *tune = (const gX_I8 *)getenv("GX_PHYSICS_TUNE");
    if (tune && tune[0]) {
        gX_I32 pinned = atoi((const char *)tune);
        if (pinned < 0) pinned = 0;
        if (pinned > 3) pinned = 3;
        physics.tuningPin = pinned;
        physics.tuningLevel = pinned;
    }
    physics.tuningGoodTicks = 0;

    //ai: create the shared simulation world
    physics.world = gX_PhysicsWorld_Create();
    if (physics.world.internal == 0) {
        gX_TraceLog(gX_LOG_WARNING, "PHYSICS: World creation returned null (ODE not available)");
        physics.enabled = false;
        gX_TRACELOG_EPILOGUE("PHYSICS: Init done (disabled)");
        return 0;
    }
    gX_Physics_ApplyTuning(physics.tuningLevel);

    //ai: default gravity (Earth)
    gX_PhysicsWorld_SetGravity(physics.world, (gX_F32v3){0.0f, -9.81f, 0.0f});

    physics.enabled = true;

    gX_TraceLog(gX_LOG_GREEN, "PHYSICS: world created, 60 Hz fixed timestep");
    gX_TRACELOG_EPILOGUE("PHYSICS: Init done");
    return 0;
}

gX_I32 gX_Physics_Shutdown(gX_I0) {
    gX_TRACELOG_PROLOGUE("Physics_Shutdown", "PHYSICS: Shutting down")

    if (physics.world.internal != 0) {
        gX_PhysicsWorld_EmptyContactGroup(physics.world);
        gX_PhysicsWorld_Destroy(physics.world);
        physics.world.internal = 0;
    }
    physics.enabled = false;
    g_physicsStepHook = NULL;
    g_physicsStepHookUser = NULL;

    gX_TRACELOG_EPILOGUE("PHYSICS: Shutdown done");
    return 0;
}

gX_I0 gX_Physics_Update(gX_I0) {
    if (!physics.enabled || physics.pause) return;
    if (physics.world.internal == 0) return;

    //ai: smoothed frame time for auto-tuning; hitches (>100 ms) are clamped
    //   so one stall cannot dominate the EMA.
    gX_F64 rawFt = gX_FrameTime_Get();
    if (rawFt > 0.1) rawFt = 0.1;
    if (physics.frameCount == 0) {
        physics.frameTimeHistory = rawFt;
    } else {
        //ai: exponential moving average (alpha = 0.05)
        physics.frameTimeHistory = physics.frameTimeHistory * 0.95 + rawFt * 0.05;
    }
    physics.frameCount++;

    //ai: auto-tuning — evaluate every 10 frames. Escalation is prompt when
    //   the smoothed frame time is over the target; recovery steps one level
    //   down after three consecutive evaluations at/under target, because a
    //   single good sample after a hitch is not evidence the load is gone.
    //   The old recovery threshold (>~92 fps) was unreachable at a 60 Hz
    //   vsync, so one startup spike pinned the world at level 3 forever; the
    //   1.0..1.2 dead band also absorbs vsync jitter. GX_PHYSICS_TUNE pins
    //   the level and skips all of this.
    if (physics.tuningPin < 0 && physics.frameCount % 10 == 0) {
        gX_F64 targetFt = 1.0 / 55.0;  // ~0.0182s
        gX_F64 ratio = physics.frameTimeHistory / targetFt;
        gX_I32 newLevel = physics.tuningLevel;

        if (ratio > 1.2) {
            physics.tuningGoodTicks = 0;
            if (ratio > 3.0) {
                newLevel = 3;  //ai: severely overloaded → max tuning
            } else if (ratio > 2.0) {
                newLevel = 2;  //ai: heavily overloaded
            } else if (newLevel < 3) {
                newLevel++;    //ai: mildly overloaded → step up
            }
        } else if (ratio <= 1.0) {
            physics.tuningGoodTicks++;
            if (physics.tuningGoodTicks >= 3 && newLevel > 0) {
                newLevel--;
                physics.tuningGoodTicks = 0;
            }
        } else {
            physics.tuningGoodTicks = 0;  //ai: dead band — hold
        }

        if (newLevel != physics.tuningLevel) {
            physics.tuningLevel = newLevel;
            gX_Physics_ApplyTuning(newLevel);
            gX_TraceLog(gX_LOG_NOTICE,
                "PHYSICS: tuning level %d (ft=%.4fs)",
                physics.tuningLevel,
                physics.frameTimeHistory);
        }
    }

    //ai: use the frame's delta time directly (no accumulator) for
    //   single-step-per-frame behavior matching the original scene.
    //   Sub-steps only happen when the display rate exceeds the physics rate.
    //ai: simulation time follows real frame time while tuning only changes solver quality.
    gX_F64 frameDt = gX_FrameTime_Get();
    if (frameDt < 0.0) frameDt = 0.0;
    if (frameDt > 0.05) frameDt = 0.05;
    physics.accumulator += frameDt;

    gX_I32 maxSteps = physics.maxSubSteps;
    gX_I32 steps = 0;
    while (physics.accumulator >= (gX_F64)physics.fixedDt && steps < maxSteps) {
        if (g_physicsStepHook) g_physicsStepHook(g_physicsStepHookUser, physics.fixedDt);
        gX_PhysicsWorld_Step(physics.world, physics.fixedDt);
        //ai: empty contact joints after EACH step — critical.
        gX_PhysicsWorld_EmptyContactGroup(physics.world);
        physics.accumulator -= (gX_F64)physics.fixedDt;
        steps++;
    }
    physics.stepsLastFrame = steps;

    //ai: drop excess accumulator
    if (physics.accumulator >= (gX_F64)physics.fixedDt) {
        physics.accumulator = 0.0;
    }
}

gX_I0 gX_Physics_StepOnce(gX_I0) {
    if (!physics.enabled || physics.world.internal == 0) return;
    if (g_physicsStepHook) g_physicsStepHook(g_physicsStepHookUser, physics.fixedDt);
    gX_PhysicsWorld_Step(physics.world, physics.fixedDt);
    gX_PhysicsWorld_EmptyContactGroup(physics.world);
    physics.stepsLastFrame = 1;
}

gX_F32 gX_Physics_GetAlpha(gX_I0) {
    //ai: fraction of the current physics step completed.
    if (physics.fixedDt <= 0.0f) return 1.0f;
    gX_F64 alpha = physics.accumulator / (gX_F64)physics.fixedDt;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    return (gX_F32)alpha;
}

gX_I0 gX_Physics_ResetFrameHistory(gX_I0) {
    //ai: clear the EMA history and reset tuning so a single
    //   heavy scene-switch frame doesn't lock physics to low quality. A
    //   pinned level (GX_PHYSICS_TUNE) survives the reset.
    physics.frameTimeHistory = 0.0;
    physics.frameCount = 0;
    physics.tuningGoodTicks = 0;
    physics.tuningLevel = (physics.tuningPin >= 0) ? physics.tuningPin : 0;
    physics.fixedDt = 1.0f / 60.0f;
    physics.maxSubSteps = 4;
    physics.stepsLastFrame = 0;
    gX_Physics_ApplyTuning(physics.tuningLevel);
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS: frame history reset — tuning %s (%d)",
        physics.tuningPin >= 0 ? "pinned" : "restored", physics.tuningLevel);
}

gX_Matrix gX_Physics_MatrixFromBody(gX_PhysicsBody body) {
    gX_F32v3 pos = gX_PhysicsBody_GetPosition(body);
    gX_F32Qua quat;
    gX_PhysicsBody_GetRotation(body, &quat);
    gX_Matrix rotMat = gX_F32Qua_ToMatrix(quat);
    gX_Matrix transMat = gX_Matrix_Translate(pos.x, pos.y, pos.z);
    //ai: model transform = rotation * translation (SRT without scale)
    return gX_Matrix_Multiply(rotMat, transMat);
}
