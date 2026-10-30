/**********************************************************************************************
*
*   [gX: src/camera.c]
*   Camera — modes, input, scene respond
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


#include <string.h>
#include <math.h>

#include "camera.h"
#include "input.h"
#include "main.h"

#include "common/enum.h"
#include "gxmath/gxmath.h"

#include "wrappers/wrappers.h"

gX_Camera camera;

//ai: camera mode names — the declaration had drifted to a void return and no
//   definition existed; the enum coverage gate now scans the whole tree.
const gX_I8 *gX_Camera_Modes_String(gX_I32 cameramode) {
    switch (cameramode) {
        case gX_CAMERAMODE_BACKEND: return "BACKEND";
        case gX_CAMERAMODE_NULL: return "NULL";
#define X(mode) case gX_CAMERAMODE_##mode: return #mode;
        gX_CAMERA_MODES
#undef X
        default: return "undefined";
    }
}

gX_I32 gX_Camera_Init(gX_I0) {
    camera = (gX_Camera){0};
    
    strcpy(camera.name,"Camera");
    strcpy(camera.description,"camera system");
    
    camera.state.MODE = gX_CAMERAMODE_FREE;
    
    camera.speed.useExpSpeed = true;
    
    gX_Camera_Reset();
    
    return 0;
}

gX_I32 gX_Camera_Reset(gX_I0) {
    
    camera.state.FLAGS_OVERRIDE = false;
    camera.state.FLAG_SMOOTHMOVE = false;
    camera.state.DO_BINDS = true;
    camera.state.DO_BINDS_MOVE = true;
    camera.state.DO_BINDS_ROTATE = true;
    camera.state.HAS_TARGET = false;
    
    camera.camera2D.offset = gX_F32v2_Zero();
    camera.camera2D.target = gX_F32v2_Zero();
    camera.camera2D.rotation = 0.0f;
    camera.camera2D.zoom = 1.0f;
    
    camera.camera3D.position = gX_F32v3_Zero();
    camera.camera3D.target = (gX_F32v3){0.0f,0.0f,1.0f};
    camera.camera3D.up = (gX_F32v3){0.0f,1.0f,0.0f};
    camera.camera3D.fovy = 90.0f;
    camera.camera3D.projection = gX_CAMERA_PERSPECTIVE;
    
    camera.limits.fovMin = 0.1f;
    camera.limits.fovMax = 179.9f;
    
    camera.speed.moveSpeed = 0.01f;
    camera.speed.lookSpeed = 0.01f;
    
    camera.frustum.near = 0.1;
    camera.frustum.far = 1000.0;
    
    return 0;
}

static gX_I0 gX_Camera_UpdateSpeed(gX_I0) {
    if (input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN) {
        camera.movement.forwardAmount *= 5.0f;
        camera.movement.upAmount *= 5.0f;
        camera.movement.rightAmount *= 5.0f;
    }
    if (input.keyboard.keys[gX_KEY_LEFT_CONTROL].state.DOWN) {
        camera.movement.forwardAmount /= 5.0f;
        camera.movement.upAmount /= 5.0f;
        camera.movement.rightAmount /= 5.0f;
    }
    camera.speed.moveSpeedExp = expf(camera.speed.moveSpeed);
    if ((int)input.mouse.scroll != 0) camera.speed.moveSpeed += 0.1f*input.mouse.scroll;
}

static gX_I0 gX_Camera_FOV(gX_I0) {
    if (input.keyboard.keys[gX_KEY_PAGE_UP].state.DOWN) {
        camera.camera3D.fovy += 1.0f;
    }
    if (input.keyboard.keys[gX_KEY_PAGE_DOWN].state.DOWN) {
        camera.camera3D.fovy -= 1.0f;
    }
    if (input.keyboard.keys[gX_KEY_HOME].state.DOWN) {
        camera.camera3D.fovy = 90.0f;
    }
    if (camera.camera3D.fovy < camera.limits.fovMin) camera.camera3D.fovy = camera.limits.fovMin;
    if (camera.camera3D.fovy > camera.limits.fovMax) camera.camera3D.fovy = camera.limits.fovMax;
}

static gX_I0 gX_Camera_Move(gX_I0) {
    gX_Camera_State state = camera.state;
    gX_Camera_Movement move = camera.movement;
    
    gX_I1 movePlaneFwd = false;
    gX_I1 movePlaneRgt = true;
    if (state.FLAGS_OVERRIDE) {
        movePlaneFwd = state.FLAG_MOVEINWORLDPLANE_FORWARD;
        movePlaneRgt = state.FLAG_MOVEINWORLDPLANE_RIGHT;
    }
    
    gX_Camera_MoveForward( &camera.camera3D, move.forwardAmount, movePlaneFwd);
    gX_Camera_MoveUp(      &camera.camera3D, move.upAmount);
    gX_Camera_MoveRight(   &camera.camera3D, move.rightAmount,   movePlaneRgt);
    gX_Camera_MoveToTarget(&camera.camera3D, move.towardAmount);
}

static gX_I0 gX_Camera_Look(gX_I0) {
    gX_Camera_State state = camera.state;
    gX_Camera_Rotation rotate = camera.rotation;
    
    gX_I1 rotateTargetYaw = false;
    gX_I1 rotateTargetPitch = false;
    gX_I1 lockView = true;
    gX_I1 rotateUp = false;
    if (state.FLAGS_OVERRIDE) {
        rotateTargetYaw = state.FLAG_ROTATEAROUNDTARGET_YAW;
        rotateTargetPitch = state.FLAG_ROTATEAROUNDTARGET_PITCH;
        lockView = state.FLAG_LOCKVIEW;
        rotateUp = state.FLAG_ROTATEUP;
    }
    
    gX_Camera_Yaw(  &camera.camera3D, rotate.yawAmount,   rotateTargetYaw);
    gX_Camera_Pitch(&camera.camera3D, rotate.pitchAmount, lockView, rotateTargetPitch, rotateUp);
    gX_Camera_Roll( &camera.camera3D, rotate.rollAmount);
}

static gX_I0 gX_Camera_Update_Free(gX_I0) {
    
    if (input.cursorEnabled) return;
    
    gX_Camera_UpdateSpeed();
    
    gX_Camera_FOV();
    
    gX_F32v2 look = input.mouse.coordDelta;
    float lookspd = camera.speed.lookSpeed*(camera.camera3D.fovy/90.0f);
    camera.rotation.yawAmount = -look.x*lookspd;
    camera.rotation.pitchAmount = -look.y*lookspd;
    
    gX_Camera_Move();
    gX_Camera_Look();
    
}

static gX_I0 gX_Camera_Update_FirstPerson(gX_I0) {
    
    if (input.cursorEnabled) return;
    
    gX_F32v2 look = input.mouse.coordDelta;
    float lookspd = camera.speed.lookSpeed*(camera.camera3D.fovy/90.0f);
    camera.rotation.yawAmount = -look.x*lookspd;
    camera.rotation.pitchAmount = +look.y*lookspd;
    
    gX_Camera_Move();
    gX_Camera_Look();
}

static gX_I0 gX_Camera_Update_ThirdPerson(gX_I0) {
    
    if (input.cursorEnabled) return;
    
    gX_F32v2 look = input.mouse.coordDelta;
    float lookspd = camera.speed.lookSpeed*(camera.camera3D.fovy/90.0f);
    camera.rotation.yawAmount = -look.x*lookspd;
    camera.rotation.pitchAmount = +look.y*lookspd;
    
    camera.movement.towardAmount = camera.speed.moveSpeed*input.mouse.scroll;
    
    gX_Camera_Move();
    gX_Camera_Look();
    
}

static gX_I0 gX_Camera_Respond(gX_I0) {
    camera.masterIndexSwitchResponse = false;
    gX_Camera_Reset();
    switch (maindata.masterIndex) {
        case gX_SCENE_TITLE: {
            camera.masterIndexSwitchResponse = true;
            break;
        }
        //ai: module-owned scene — the module drives its own camera state.
        case gX_SCENE_MODULE: {
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_BATTLE: {
            camera.state.MODE = gX_CAMERAMODE_THIRDPERSON;
            camera.speed.lookSpeed = 0.001f;
            camera.speed.moveSpeed = 0.1f;
            camera.camera3D.position = (gX_F32v3){0.0f, 5.8f, 13.5f};
            camera.camera3D.target = (gX_F32v3){0.0f, 0.5f, 0.0f};
            camera.camera3D.fovy = 40.0f;
            camera.state.FLAGS_OVERRIDE = true;
            camera.state.FLAG_ROTATEAROUNDTARGET_YAW = true;
            camera.state.FLAG_ROTATEAROUNDTARGET_PITCH = true;
            camera.state.FLAG_LOCKVIEW = true;
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_MAP: {
            camera.state.MODE = gX_CAMERAMODE_THIRDPERSON;
            camera.state.DO_BINDS = false;
            camera.state.DO_BINDS_MOVE = false;
            camera.state.DO_BINDS_ROTATE = false;
            camera.state.HAS_TARGET = true;
            camera.state.FLAGS_OVERRIDE = true;
            camera.state.FLAG_SMOOTHMOVE = true;
            camera.state.FLAG_ROTATEAROUNDTARGET_YAW = true;
            camera.state.FLAG_ROTATEAROUNDTARGET_PITCH = true;
            camera.target.moveWithTarget = true;
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_BACKGROUND: {
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_GFX: {
            camera.state.MODE = gX_CAMERAMODE_FREE;
            camera.camera3D.position = (gX_F32v3){6.0f,2.5f,6.0f};
            camera.camera3D.target = (gX_F32v3){0.0f,0.0f,0.0f};
            camera.camera3D.fovy = 45.0f;
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_PP: {
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_WIREFRAME: {
            camera.state.MODE = gX_CAMERAMODE_FREE;
            camera.state.DO_BINDS = true;
            camera.state.DO_BINDS_MOVE = true;
            camera.state.DO_BINDS_ROTATE = true;
            camera.camera3D.position = (gX_F32v3){0.0f,0.0f,3.0f};
            camera.camera3D.target = (gX_F32v3){0.0f,0.0f,0.0f};
            camera.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_TEXTWRITER:
        case gX_SCENE_RENDERER_FRACTAL:
        case gX_SCENE_RENDERER_GLOBE:
        case gX_SCENE_RENDERER_ANALOG:
        case gX_SCENE_RENDERER_CLOCK:
        case gX_SCENE_RENDERER_SKY:
        case gX_SCENE_RENDERER_TELETEXT:
        case gX_SCENE_RENDERER_WATER: {
            camera.masterIndexSwitchResponse = true;
            break;
        }
        default: break;
    }
}

#define GX_CAMERA_BIND(bindplus,bindminus,where,what,speed) \
    if (input.binds[gX_BIND_##bindplus].state.DOWN) { \
        camera.where.what = true; \
        camera.where.what##Amount = +speed; \
    } \
    else if (input.binds[gX_BIND_##bindminus].state.DOWN) { \
        camera.where.what = true; \
        camera.where.what##Amount = -speed; \
    } \
    else { \
        camera.where.what = false; \
        camera.where.what##Amount = 0.0f; \
    } \

static gX_I0 gX_Camera_Binds(gX_I0) {
    gX_F32 movespeed = camera.speed.moveSpeed;
    if (camera.speed.useExpSpeed) {
        movespeed = camera.speed.moveSpeedExp;
    }
    movespeed /= maindata.targetFPS;
    
    if (camera.state.DO_BINDS_MOVE) {
        if (camera.state.FLAG_SMOOTHMOVE) {
            camera.movement.forwardAmount *= 0.95f;
            camera.movement.rightAmount *= 0.95f;
            camera.movement.upAmount *= 0.95f;
            if (input.binds[gX_BIND_MOVE_FORWARD].state.DOWN)  camera.movement.forwardAmount += 0.2f*movespeed;
            if (input.binds[gX_BIND_MOVE_BACKWARD].state.DOWN) camera.movement.forwardAmount -= 0.2f*movespeed;
            if (input.binds[gX_BIND_MOVE_RIGHT].state.DOWN)    camera.movement.rightAmount += 0.2f*movespeed;
            if (input.binds[gX_BIND_MOVE_LEFT].state.DOWN)     camera.movement.rightAmount -= 0.2f*movespeed;
            if (input.binds[gX_BIND_MOVE_UP].state.DOWN)       camera.movement.upAmount += 0.2f*movespeed;
            if (input.binds[gX_BIND_MOVE_DOWN].state.DOWN)     camera.movement.upAmount -= 0.2f*movespeed;
        }
        else {
            GX_CAMERA_BIND(MOVE_FORWARD,MOVE_BACKWARD,movement,forward,movespeed)
            GX_CAMERA_BIND(MOVE_RIGHT,MOVE_LEFT,movement,right,movespeed)
            GX_CAMERA_BIND(MOVE_UP,MOVE_DOWN,movement,up,movespeed)
        }
    }
    
}

static gX_I0 gX_Camera_UpdateModes(gX_I0) {
    switch (camera.state.MODE) {
        case gX_CAMERAMODE_FREE: {
            gX_Camera_Update_Free();
            break;
        }
        case gX_CAMERAMODE_FIRSTPERSON: {
            gX_Camera_Update_FirstPerson();
            break;
        }
        case gX_CAMERAMODE_THIRDPERSON: {
            gX_Camera_Update_ThirdPerson();
            break;
        }
        default: break;
    }
}

gX_I0 gX_Camera_Update(gX_I0) {
    
    if (maindata.masterIndexSwitched) {
        gX_Camera_Respond();
    }
    
    if (camera.state.DO_BINDS) {
        gX_Camera_Binds();
    }
    
    gX_Camera_UpdateModes();
    
    //opencode: map can enter before gameplay registers an external target.
    if (camera.state.HAS_TARGET && camera.target.ptrTargetPos) {
        camera.camera3D.target = *camera.target.ptrTargetPos;
    }
    
}
