/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_camera.cpp]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: tour camera API (gX-native remake of gameX's Camera_* surface). The
//   keyframe rail lives here in the shell — gameX had it in camera.c, gX's
//   camera module was left without one — and drives camera.camera3D (the
//   renderer renders through it) from a scripting update hook, wall-clock
//   timed instead of gameX's per-frame 1/targetFPS step.
#include "tour.h"

#include <angelscript.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

#include "../../common/log.h"
#include "../../common/marshal/json_impl.h"
#include "../../main.h"
#include "../../camera.h"
#include "../../gxmath/easing.h"
#include "../../gxmath/vector3.h"

#include "tour_camera.h"

extern "C" gX_F64 gX_FrameTime_Get(gX_I0);

typedef gX_TourCamera_Keyframe TourCamera_Keyframe;

static TourCamera_Keyframe s_keyframes[GX_TOUR_CAMERA_MAX_KEYFRAMES];
static gX_I32 s_keyframeCount = 0;
static gX_I1  s_railRunning = 0;
static gX_I32 s_segment = 0;
static gX_F64 s_segmentT = 0.0;
static gX_F64 s_lastElapsed = 0.0;

// ===========================================================================
// Rail tick — advances the current segment and writes camera.camera3D
// ===========================================================================

static gX_I0 TourCamera_ApplyPose(gX_F32v3 pos, gX_F32v3 target, gX_F32 fov) {
    camera.camera3D.position = pos;
    camera.camera3D.target = target;
    camera.camera3D.fovy = fov;
}

static gX_I0 TourCamera_Tick(gX_I0) {
    if (!s_railRunning || s_keyframeCount < 2) return;

    gX_F64 now = maindata.timeElapsed;
    gX_F64 delta = now - s_lastElapsed;
    s_lastElapsed = now;
    if (delta < 0.0) delta = 0.0;
    s_segmentT += delta;

    while (s_segment < s_keyframeCount - 1) {
        const TourCamera_Keyframe *from = &s_keyframes[s_segment];
        const TourCamera_Keyframe *to = &s_keyframes[s_segment + 1];
        gX_F64 duration = (to->t > 0.0f) ? (gX_F64)to->t : 0.0;
        if (duration <= 0.0) {
            s_segment++;
            s_segmentT = 0.0;
            continue;
        }
        gX_F32 tNorm = (s_segmentT >= duration) ? 1.0f : (gX_F32)(s_segmentT / duration);

        gX_F32v3 pos;
        if (from->bezier) {
            pos = gX_F32v3_EasingBezier(from->pos, from->ctrl1, from->ctrl2, to->pos, tNorm);
        } else {
            pos = gX_F32v3_EasingLinear(from->pos, to->pos, tNorm);
        }
        gX_F32v3 target = gX_F32v3_EasingLinear(from->target, to->target, tNorm);
        gX_F32 fov = gX_F32_EasingLinear(from->fov, to->fov, tNorm);
        TourCamera_ApplyPose(pos, target, fov);

        if (tNorm >= 1.0f) {
            s_segmentT -= duration;
            s_segment++;
            continue;
        }
        return;
    }

    TourCamera_ApplyPose(s_keyframes[s_keyframeCount - 1].pos,
        s_keyframes[s_keyframeCount - 1].target,
        s_keyframes[s_keyframeCount - 1].fov);
    s_railRunning = 0;
}

// ===========================================================================
// Bindings
// ===========================================================================

static gX_F32v3 TourCamera_GetPos(void) {
    return camera.camera3D.position;
}

static void TourCamera_SetPos(const gX_F32v3 &pos) {
    camera.camera3D.position = pos;
}

static gX_F32v3 TourCamera_GetTarget(void) {
    return camera.camera3D.target;
}

static void TourCamera_SetTarget(const gX_F32v3 &target) {
    camera.camera3D.target = target;
}

static gX_F32 TourCamera_GetFov(void) {
    return camera.camera3D.fovy;
}

static void TourCamera_SetFov(gX_F32 fov) {
    if (fov < 1.0f) fov = 1.0f;
    if (fov > 179.0f) fov = 179.0f;
    camera.camera3D.fovy = fov;
}

static void TourCamera_KeyframeClear(void) {
    s_keyframeCount = 0;
    s_railRunning = 0;
    s_segment = 0;
    s_segmentT = 0.0;
    memset(s_keyframes, 0, sizeof(s_keyframes));
}

//keyframe times follow gameX's rail semantics: t is the duration of the
//   segment arriving at this keyframe (the first keyframe's t is unused).
//   AddBezier sets the outgoing position handles for the segment that leaves
//   the added keyframe; the destination's handles are ignored.
static void TourCamera_KeyframeAdd(gX_F32 t, const gX_F32v3 &pos,
                                   const gX_F32v3 &target, gX_F32 fov) {
    if (s_keyframeCount >= GX_TOUR_CAMERA_MAX_KEYFRAMES) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "camera rail full (%d keyframes)",
            GX_TOUR_CAMERA_MAX_KEYFRAMES);
        return;
    }
    TourCamera_Keyframe *kf = &s_keyframes[s_keyframeCount++];
    kf->t = (t > 0.0f) ? t : 0.001f;
    kf->pos = pos;
    kf->target = target;
    kf->fov = fov;
    kf->bezier = 0;
}

static void TourCamera_KeyframeAddBezier(gX_F32 t, const gX_F32v3 &pos,
                                         const gX_F32v3 &target, gX_F32 fov,
                                         const gX_F32v3 &ctrl1, const gX_F32v3 &ctrl2) {
    TourCamera_KeyframeAdd(t, pos, target, fov);
    if (s_keyframeCount == 0) return;
    TourCamera_Keyframe *kf = &s_keyframes[s_keyframeCount - 1];
    kf->bezier = 1;
    kf->ctrl1 = ctrl1;
    kf->ctrl2 = ctrl2;
}

static void TourCamera_KeyframeStart(void) {
    if (s_keyframeCount == 0) return;
    TourCamera_ApplyPose(s_keyframes[0].pos, s_keyframes[0].target, s_keyframes[0].fov);
    s_segment = 0;
    s_segmentT = 0.0;
    s_lastElapsed = maindata.timeElapsed;
    s_railRunning = (s_keyframeCount > 1) ? 1 : 0;
}

static void TourCamera_KeyframeStop(void) {
    s_railRunning = 0;
}

static gX_I1 TourCamera_KeyframeIsRunning(void) {
    return s_railRunning;
}

// ===========================================================================
// Rail access (module-internal: editor panel + persistence + script bindings)
// ===========================================================================

gX_I32 gX_TourCamera_RailMax(gX_I0) { return GX_TOUR_CAMERA_MAX_KEYFRAMES; }
gX_I32 gX_TourCamera_RailCount(gX_I0) { return s_keyframeCount; }

gX_TourCamera_Keyframe *gX_TourCamera_RailKey(gX_I32 index) {
    if (index < 0 || index >= s_keyframeCount) return NULL;
    return &s_keyframes[index];
}

gX_I32 gX_TourCamera_RailAddGrab(gX_F32 t) {
    if (s_keyframeCount >= GX_TOUR_CAMERA_MAX_KEYFRAMES) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "camera rail full (%d keyframes)",
            GX_TOUR_CAMERA_MAX_KEYFRAMES);
        return -1;
    }
    TourCamera_KeyframeAdd(t, camera.camera3D.position, camera.camera3D.target,
                           camera.camera3D.fovy);
    return s_keyframeCount - 1;
}

gX_I32 gX_TourCamera_RailRemove(gX_I32 index) {
    if (index < 0 || index >= s_keyframeCount) return 1;
    for (gX_I32 i = index; i + 1 < s_keyframeCount; i++) {
        s_keyframes[i] = s_keyframes[i + 1];
    }
    s_keyframeCount--;
    memset(&s_keyframes[s_keyframeCount], 0, sizeof(TourCamera_Keyframe));
    if (s_keyframeCount < 2) s_railRunning = 0;
    return 0;
}

gX_I32 gX_TourCamera_RailMove(gX_I32 index, gX_I32 delta) {
    if (index < 0 || index >= s_keyframeCount) return 1;
    gX_I32 target = index + delta;
    if (target < 0 || target >= s_keyframeCount) return 1;
    TourCamera_Keyframe tmp = s_keyframes[index];
    s_keyframes[index] = s_keyframes[target];
    s_keyframes[target] = tmp;
    return 0;
}

gX_I32 gX_TourCamera_RailApply(gX_I32 index) {
    if (index < 0 || index >= s_keyframeCount) return 1;
    TourCamera_ApplyPose(s_keyframes[index].pos, s_keyframes[index].target,
                         s_keyframes[index].fov);
    return 0;
}

gX_I0 gX_TourCamera_RailClear(gX_I0) {
    TourCamera_KeyframeClear();
}

gX_I0 gX_TourCamera_RailPlay(gX_I0) {
    TourCamera_KeyframeStart();
}

gX_I0 gX_TourCamera_RailStop(gX_I0) {
    TourCamera_KeyframeStop();
}

gX_I1 gX_TourCamera_RailIsRunning(gX_I0) {
    return TourCamera_KeyframeIsRunning();
}

static gX_I0 TourCamera_JsonAddVec(struct json_object *obj, const char *key, gX_F32v3 v) {
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double((double)v.x));
    json_object_array_add(arr, json_object_new_double((double)v.y));
    json_object_array_add(arr, json_object_new_double((double)v.z));
    json_object_object_add(obj, key, arr);
}

static gX_I32 TourCamera_JsonGetVec(struct json_object *obj, const char *key, gX_F32v3 *out) {
    struct json_object *arr = NULL;
    if (!json_object_object_get_ex(obj, key, &arr)) return 1;
    if (json_object_array_length(arr) < 3) return 1;
    out->x = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 0));
    out->y = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 1));
    out->z = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 2));
    return 0;
}

gX_I32 gX_TourCamera_RailSave(const gX_I8 *path) {
    //opencode: shipped preset paths are refused under the harness guard,
    //   mirroring gX_RendererGFX_Preset_Save_New (gates never rewrite what
    //   they verify).
    if (getenv("GX_NO_SETTINGS_SAVE") && path
        && strstr((const char *)path, "resources/tools/")) {
        gX_TraceLog_SCRIPTING(gX_LOG_ERROR,
            "CameraRail: refusing to overwrite a shipped preset under the harness guard: %s",
            path);
        return 1;
    }
    struct json_object *root = json_object_new_object();
    if (!root) return 1;
    json_object_object_add(root, "type", json_object_new_string("camera_rail"));
    json_object_object_add(root, "version", json_object_new_int(1));
    struct json_object *keys = json_object_new_array();
    for (gX_I32 i = 0; i < s_keyframeCount; i++) {
        const TourCamera_Keyframe *kf = &s_keyframes[i];
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "t", json_object_new_double((double)kf->t));
        TourCamera_JsonAddVec(entry, "pos", kf->pos);
        TourCamera_JsonAddVec(entry, "target", kf->target);
        json_object_object_add(entry, "fov", json_object_new_double((double)kf->fov));
        json_object_object_add(entry, "bezier", json_object_new_int(kf->bezier ? 1 : 0));
        TourCamera_JsonAddVec(entry, "ctrl1", kf->ctrl1);
        TourCamera_JsonAddVec(entry, "ctrl2", kf->ctrl2);
        json_object_array_add(keys, entry);
    }
    json_object_object_add(root, "keyframes", keys);
    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);
    if (ret == 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_NOTICE, "CameraRail: saved %d keyframes to %s",
            s_keyframeCount, path);
        return 0;
    }
    gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "CameraRail: failed to save to %s", path);
    return 1;
}

gX_I32 gX_TourCamera_RailLoad(const gX_I8 *path) {
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "CameraRail: failed to load from %s", path);
        return 1;
    }
    struct json_object *keys = NULL;
    if (!json_object_object_get_ex(root, "keyframes", &keys)) {
        json_object_put(root);
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "CameraRail: %s has no keyframes", path);
        return 1;
    }
    TourCamera_KeyframeClear();
    gX_U32 count = (gX_U32)json_object_array_length(keys);
    if (count > GX_TOUR_CAMERA_MAX_KEYFRAMES) count = GX_TOUR_CAMERA_MAX_KEYFRAMES;
    for (gX_U32 i = 0; i < count; i++) {
        struct json_object *entry = json_object_array_get_idx(keys, i);
        if (!entry) continue;
        TourCamera_Keyframe *kf = &s_keyframes[s_keyframeCount];
        memset(kf, 0, sizeof(*kf));
        struct json_object *v = NULL;
        if (json_object_object_get_ex(entry, "t", &v)) kf->t = (gX_F32)json_object_get_double(v);
        if (kf->t <= 0.0f) kf->t = 0.001f;
        TourCamera_JsonGetVec(entry, "pos", &kf->pos);
        TourCamera_JsonGetVec(entry, "target", &kf->target);
        if (json_object_object_get_ex(entry, "fov", &v)) kf->fov = (gX_F32)json_object_get_double(v);
        if (json_object_object_get_ex(entry, "bezier", &v)) kf->bezier = (gX_I1)(json_object_get_int(v) != 0);
        TourCamera_JsonGetVec(entry, "ctrl1", &kf->ctrl1);
        TourCamera_JsonGetVec(entry, "ctrl2", &kf->ctrl2);
        s_keyframeCount++;
    }
    json_object_put(root);
    if (s_keyframeCount == 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "CameraRail: %s has no usable keyframes", path);
        return 1;
    }
    gX_TraceLog_SCRIPTING(gX_LOG_NOTICE, "CameraRail: loaded %d keyframes from %s",
        s_keyframeCount, path);
    return 0;
}

static gX_F32 TourCamera_GetDeltaTime(void) {
    return (gX_F32)gX_FrameTime_Get();
}

//opencode: script-facing rail persistence/editing (the panel uses the
//   module-internal functions directly).
static gX_I32 TourCamera_RailCountScript(void) {
    return gX_TourCamera_RailCount();
}

static void TourCamera_RailAddGrabScript(gX_F32 t) {
    gX_TourCamera_RailAddGrab(t);
}

static gX_I32 TourCamera_RailSaveScript(const std::string &path) {
    return gX_TourCamera_RailSave((const gX_I8 *)path.c_str());
}

static gX_I32 TourCamera_RailLoadScript(const std::string &path) {
    return gX_TourCamera_RailLoad((const gX_I8 *)path.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-camera registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourCamera_Register(asIScriptEngine *engine) {
    static gX_I1 hooked = 0;
    if (!hooked) {
        if (gX_Scripting_RegisterUpdateHook("tour_camera", TourCamera_Tick) == 0) {
            hooked = 1;
        }
    }

    int r = 0;
    r = engine->RegisterGlobalFunction("Vec3 Camera_GetPos()",
        asFUNCTION(TourCamera_GetPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_SetPos(const Vec3 &in)",
        asFUNCTION(TourCamera_SetPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("Vec3 Camera_GetTarget()",
        asFUNCTION(TourCamera_GetTarget), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_SetTarget(const Vec3 &in)",
        asFUNCTION(TourCamera_SetTarget), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Camera_GetFov()",
        asFUNCTION(TourCamera_GetFov), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_SetFov(float)",
        asFUNCTION(TourCamera_SetFov), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("void Camera_Keyframe_Clear()",
        asFUNCTION(TourCamera_KeyframeClear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_Keyframe_Add(float, const Vec3 &in, const Vec3 &in, float)",
        asFUNCTION(TourCamera_KeyframeAdd), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_Keyframe_AddBezier(float, const Vec3 &in, const Vec3 &in, float, const Vec3 &in, const Vec3 &in)",
        asFUNCTION(TourCamera_KeyframeAddBezier), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_Keyframe_Start()",
        asFUNCTION(TourCamera_KeyframeStart), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_Keyframe_Stop()",
        asFUNCTION(TourCamera_KeyframeStop), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Camera_Keyframe_IsRunning()",
        asFUNCTION(TourCamera_KeyframeIsRunning), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Camera_Rail_Count()",
        asFUNCTION(TourCamera_RailCountScript), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Camera_Rail_AddGrab(float)",
        asFUNCTION(TourCamera_RailAddGrabScript), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Camera_Rail_Save(const string &in)",
        asFUNCTION(TourCamera_RailSaveScript), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Camera_Rail_Load(const string &in)",
        asFUNCTION(TourCamera_RailLoadScript), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("float Time_GetDeltaTime()",
        asFUNCTION(TourCamera_GetDeltaTime), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour camera API (pos/target/fov, keyframe rail)");
    return 0;
}
