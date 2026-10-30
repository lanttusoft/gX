/**********************************************************************************************
*
*   [gX: src/camera.h]
*   Camera header — modes, binds, respond table
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


#ifndef CAMERA_H
#define CAMERA_H

#include "common/types.h"
#include "common/enum.h"

#define gX_CAMERA_MODES \
    X(FREE) \
    X(FIRSTPERSON) \
    X(THIRDPERSON) \
    X(RAIL) \
    X(ORBIT) \
    X(STATIC) \
    X(2D) \
    X(CUSTOM) \
    
typedef enum gX_Camera_Modes {
    gX_CAMERAMODE_BACKEND = -1,
    gX_CAMERAMODE_NULL = 0,
#define X(mode) gX_CAMERAMODE_##mode,
    gX_CAMERA_MODES
#undef X
} gX_Camera_Modes;

typedef struct gX_Camera_State {
    
    gX_I32  MODE                        ; //4
    
    gX_I1   ENABLE_MODE_EXTERNAL        ; //5
    gX_I32  MODE_EXTERNAL               ;
    
    gX_I1   DO_BINDS                    ; //4
    gX_I1   DO_BINDS_MOVE               ;
    gX_I1   DO_BINDS_ROTATE             ;
    gX_I1   HAS_TARGET                  ;
    
    gX_I1   CAM2D_SET_INITIAL           ; //2
    gX_I1   CAM3D_SET_INITIAL           ;
    
    gX_I1   FLAGS_OVERRIDE                  ; //9
    gX_I1   FLAG_SMOOTHMOVE                 ;
    gX_I1   FLAG_AUTOROTATECAM              ;
    gX_I1   FLAG_MOVEINWORLDPLANE_FORWARD   ;
    gX_I1   FLAG_MOVEINWORLDPLANE_RIGHT     ;
    gX_I1   FLAG_LOCKVIEW                   ;
    gX_I1   FLAG_ROTATEAROUNDTARGET_YAW     ;
    gX_I1   FLAG_ROTATEAROUNDTARGET_PITCH   ;
    gX_I1   FLAG_ROTATEUP                   ;
    
    gX_U32  CAM3D_DIRECTION_CARDINAL    ; //5
    gX_I1   CAM3D_DIRECTION_UP          ;
    gX_I1   CAM3D_DIRECTION_DOWN        ;
    
    gX_I1   TILT_NORMAL                 ; //1
    
} gX_Camera_State;

typedef struct gX_Camera_Movement {
    gX_I1 forward;
    gX_I1 up;
    gX_I1 right;
    gX_I1 toward;
    gX_F32 forwardAmount;
    gX_F32 upAmount;
    gX_F32 rightAmount;
    gX_F32 towardAmount;
} gX_Camera_Movement;

typedef struct gX_Camera_Rotation {
    gX_I1 yaw;
    gX_I1 pitch;
    gX_I1 roll;
    gX_F32 yawAmount;
    gX_F32 pitchAmount;
    gX_F32 rollAmount;
} gX_Camera_Rotation;

typedef struct gX_Frustum {
    gX_F64 near;
    gX_F64 far;
    gX_F64 left;
    gX_F64 right;
    gX_F64 top;
    gX_F64 bottom;
} gX_Frustum;

typedef struct gX_Camera_Limits {
    gX_I1  useFovLimits;
    gX_F32 fovMin;
    gX_F32 fovMax;
} gX_Camera_Limits;

typedef struct gX_Camera_Speed {
    gX_I1     useExpSpeed;
    
    gX_F32    moveSpeed;
    gX_F32    moveSpeedExp; //exp version
    
    gX_F32    lookSpeed;
    
    gX_F32    fovSpeed;
    gX_F32    fovDefault;
    
    gX_F32v3  speedPrevPos;
    gX_F32    speedPrevTime;
    gX_F32    speedCalculated;
} gX_Camera_Speed;

typedef struct gX_Camera_Target {
    gX_I1       moveWithTarget;
    gX_F32v3   *ptrTargetPos;
    gX_U32      datatype;
    gX_I0      *ptr;
} gX_Camera_Target;

//typedef struct gX_Camera_Keyframe

typedef struct gX_Camera {
    gX_I8 name[128];
    gX_I8 description[128];
    
    gX_Camera_State state;
    gX_I1 masterIndexSwitchResponse;
    gX_F32v2 camera3D_direction;
    gX_F32v3 camera3D_target_pos;
    
    gX_Camera_Movement movement;
    gX_Camera_Rotation rotation;
    
    gX_Camera_Limits limits;
    
    gX_Camera2D camera2D;
    gX_Camera2D camera2DInitial;
    gX_Camera2D camera2Dvalues[8];
    
    gX_Camera3D camera3D;
    gX_Camera3D camera3DInitial;
    gX_Camera3D camera3Dvalues[8];
    
    gX_Frustum frustum;
    gX_Camera_Speed speed;
    gX_Camera_Target target;
    
    //keyframes here
    
} gX_Camera;

extern gX_Camera camera;

gX_I32 gX_Camera_Init(gX_I0);
gX_I32 gX_Camera_Reset(gX_I0);
gX_I32 gX_Camera_GiveTarget(gX_F32v3 *target);
gX_I32 gX_Camera_RemoveTarget(gX_I0);

gX_I0 gX_Camera_Update(gX_I0);
const gX_I8 *gX_Camera_Modes_String(gX_I32 cameramode);

// Camera math operations (implemented in stubs.c)
gX_I0 gX_Camera_MoveForward(gX_Camera3D *cam, gX_F32 distance, gX_I1 moveInWorldPlane);
gX_I0 gX_Camera_MoveUp(gX_Camera3D *cam, gX_F32 distance);
gX_I0 gX_Camera_MoveRight(gX_Camera3D *cam, gX_F32 distance, gX_I1 moveInWorldPlane);
gX_I0 gX_Camera_MoveToTarget(gX_Camera3D *cam, gX_F32 distance);
gX_I0 gX_Camera_Yaw(gX_Camera3D *cam, gX_F32 angle, gX_I1 rotateAroundTarget);
gX_I0 gX_Camera_Pitch(gX_Camera3D *cam, gX_F32 angle, gX_I1 lockView, gX_I1 rotateAroundTarget, gX_I1 rotateUp);
gX_I0 gX_Camera_Roll(gX_Camera3D *cam, gX_F32 angle);

#endif //CAMERA_H
