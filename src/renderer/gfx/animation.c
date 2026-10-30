/**********************************************************************************************
*
*   [gX: src/renderer/gfx/animation.c]
*   gX — a game engine
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


//opencode: GLB clip playback (first slice). The custom GLB parser stores
//   clips as node-targeted channels (times + values); this evaluator samples
//   them into the skeleton's bone transforms, accumulates world matrices, and
//   writes the skinning matrices the backend uploads per model draw. Bone
//   names map channels to bones (raylib names bones from the skin joints).
//opencode: clip evaluation is a light per-frame pass; size-optimize the TU so
//   the playback feature stays inside the release lean budget (the editor TU
//   uses the same treatment).
#if defined(__GNUC__)
#pragma GCC optimize("Os")
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "animation.h"
#include "constants.h"
#include "model.h"
#include "glb_loader.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/quaternion.h"
#include "../../gxmath/constants.h"
#include "../../common/log.h"
#include "../../common/marshal/json_impl.h"

//opencode: mirrors the wrapper's bone cap (gX_BoneInfo comment: MAX 256).
#define gX_GFX_ANIMATION_MAX_BONES 256

static gX_I32 Animation_FindNode(const gX_RendererGFX_GLB_Data *glb,
    const gX_I8 *name) {
    if (!glb || !name || !name[0]) return -1;
    for (gX_I32 i = 0; i < glb->nodeCount; i++) {
        if (strcmp((const char *)glb->nodes[i].name, (const char *)name) == 0)
            return i;
    }
    return -1;
}

//opencode: sample one channel at t (linear; rotations are normalized after the
//   blend so nlerp stays a unit quaternion).
static gX_I0 Animation_SampleChannel(
    const gX_RendererGFX_GLB_AnimChannel *channel, gX_F32 t, gX_F32 *out) {
    if (!channel || !channel->times || !channel->values
        || channel->keyframeCount <= 0) return;
    gX_I32 stride = channel->valueStride;
    if (stride < 1) stride = 1;
    gX_I32 last = channel->keyframeCount - 1;
    if (last == 0 || t <= channel->times[0]) {
        for (gX_I32 c = 0; c < stride; c++) out[c] = channel->values[c];
    } else if (t >= channel->times[last]) {
        for (gX_I32 c = 0; c < stride; c++)
            out[c] = channel->values[last * stride + c];
    } else {
        gX_I32 index = 0;
        while (index + 1 < channel->keyframeCount
            && channel->times[index + 1] < t) index++;
        gX_F32 t0 = channel->times[index];
        gX_F32 t1 = channel->times[index + 1];
        gX_F32 k = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        for (gX_I32 c = 0; c < stride; c++) {
            gX_F32 a = channel->values[index * stride + c];
            gX_F32 b = channel->values[(index + 1) * stride + c];
            out[c] = a + (b - a) * k;
        }
    }
    if (stride == 4) {
        gX_F32 length = sqrtf(out[0] * out[0] + out[1] * out[1]
            + out[2] * out[2] + out[3] * out[3]);
        if (length > 0.0001f) {
            out[0] /= length;
            out[1] /= length;
            out[2] /= length;
            out[3] /= length;
        } else {
            out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
        }
    }
}

static gX_F32 Animation_ClipDuration(
    const gX_RendererGFX_GLB_Animation *clip) {
    gX_F32 duration = 0.0f;
    if (!clip) return duration;
    for (gX_I32 ci = 0; ci < clip->channelCount; ci++) {
        const gX_RendererGFX_GLB_AnimChannel *channel = &clip->channels[ci];
        if (!channel->times || channel->keyframeCount <= 0) continue;
        gX_F32 end = channel->times[channel->keyframeCount - 1];
        if (end > duration) duration = end;
    }
    return duration;
}

static gX_Matrix Animation_LocalMatrix(const gX_Transform *transform) {
    gX_Matrix scale = gX_Matrix_Scale(transform->scale.x,
        transform->scale.y, transform->scale.z);
    gX_Matrix rotation = gX_F32Qua_ToMatrix(transform->rotation);
    gX_Matrix translation = gX_Matrix_Translate(transform->translation.x,
        transform->translation.y, transform->translation.z);
    return gX_Matrix_Multiply(gX_Matrix_Multiply(scale, rotation),
        translation);
}

//opencode: accumulate world matrices for a bone array whose parents may come
//   in any order; a depth guard bounds malformed cycles.
static gX_I0 Animation_AccumulateWorld(gX_ModelSkeleton *skeleton,
    const gX_Transform *poses, gX_I32 count, gX_Matrix *world) {
    gX_I1 resolved[gX_GFX_ANIMATION_MAX_BONES];
    for (gX_I32 i = 0; i < count; i++) resolved[i] = 0;
    for (gX_I32 bi = 0; bi < count; bi++) {
        gX_I32 chain[gX_GFX_ANIMATION_MAX_BONES];
        gX_I32 depth = 0;
        gX_I32 index = bi;
        while (index >= 0 && index < count && !resolved[index]
            && depth < count) {
            chain[depth++] = index;
            index = skeleton->bones[index].parent;
        }
        for (gX_I32 ci = depth - 1; ci >= 0; ci--) {
            gX_I32 bone = chain[ci];
            gX_Matrix local = Animation_LocalMatrix(&poses[bone]);
            gX_Matrix parent = gX_Matrix_Identity();
            gX_I32 parentIndex = skeleton->bones[bone].parent;
            if (parentIndex >= 0 && parentIndex < count)
                parent = world[parentIndex];
            world[bone] = gX_Matrix_Multiply(local, parent);
            resolved[bone] = 1;
        }
    }
}

gX_I0 gX_RendererGFX_Animation_ClipRange(gX_F32 animStart, gX_F32 animEnd,
    gX_F32 duration, gX_F32 *outStart, gX_F32 *outEnd) {
    gX_F32 start = animStart;
    gX_F32 end = animEnd;
    if (!isfinite(start) || start < 0.0f) start = 0.0f;
    if (!isfinite(end) || end <= 0.0f) end = duration;
    if (end > duration) end = duration;
    if (start >= end) {
        start = 0.0f;
        end = duration;
    }
    if (outStart) *outStart = start;
    if (outEnd) *outEnd = end;
}

//opencode: apply the authored per-bone overrides on top of the sampled pose
//   (rotation is normalized so a hand-edited quaternion stays valid).
static gX_I0 Animation_ApplyOverrides(
    const gX_RendererGFX_Model_Parameters *params, gX_Transform *poses,
    gX_I32 count) {
    gX_I32 overrideCount = params->poseOverrideCount;
    if (overrideCount > gX_GFX_MODEL_POSE_OVERRIDES)
        overrideCount = gX_GFX_MODEL_POSE_OVERRIDES;
    for (gX_I32 oi = 0; oi < overrideCount; oi++) {
        const gX_F32 *entry = &params->poseOverrides[oi * 11];
        gX_I32 bone = (gX_I32)entry[0];
        if (bone < 0 || bone >= count) continue;
        gX_Transform *pose = &poses[bone];
        pose->translation = (gX_F32v3){entry[1], entry[2], entry[3]};
        pose->rotation = (gX_F32Qua){entry[4], entry[5], entry[6], entry[7]};
        gX_F32 length = sqrtf(pose->rotation.x * pose->rotation.x
            + pose->rotation.y * pose->rotation.y
            + pose->rotation.z * pose->rotation.z
            + pose->rotation.w * pose->rotation.w);
        if (length > 0.0001f) {
            pose->rotation.x /= length;
            pose->rotation.y /= length;
            pose->rotation.z /= length;
            pose->rotation.w /= length;
        } else {
            pose->rotation = (gX_F32Qua){0.0f, 0.0f, 0.0f, 1.0f};
        }
        pose->scale = (gX_F32v3){entry[8], entry[9], entry[10]};
    }
}

//opencode: sample every channel targeting a node into a local transform
//   (translation/rotation/scale; other paths stay untouched).
static gX_I0 Animation_SampleInto(const gX_RendererGFX_GLB_Animation *clip,
    gX_I32 node, gX_F32 time, gX_Transform *local) {
    if (!clip || node < 0) return;
    for (gX_I32 ci = 0; ci < clip->channelCount; ci++) {
        const gX_RendererGFX_GLB_AnimChannel *channel = &clip->channels[ci];
        if (channel->targetNode != node) continue;
        if (channel->targetPath == 0) {
            gX_F32 value[4] = {0};
            Animation_SampleChannel(channel, time, value);
            local->translation = (gX_F32v3){value[0], value[1], value[2]};
        } else if (channel->targetPath == 1) {
            gX_F32 value[4] = {0, 0, 0, 1};
            Animation_SampleChannel(channel, time, value);
            local->rotation = (gX_F32Qua){value[0], value[1], value[2],
                value[3]};
        } else if (channel->targetPath == 2) {
            gX_F32 value[4] = {1, 1, 1, 0};
            Animation_SampleChannel(channel, time, value);
            local->scale = (gX_F32v3){value[0], value[1], value[2]};
        }
    }
}

//opencode: cross-fade two local transforms — lerp translation/scale and nlerp
//   the rotation along the shortest arc, normalizing the result.
static gX_Transform Animation_BlendPose(const gX_Transform *first,
    const gX_Transform *second, gX_F32 weight) {
    gX_Transform out;
    out.translation = (gX_F32v3){
        first->translation.x
            + (second->translation.x - first->translation.x) * weight,
        first->translation.y
            + (second->translation.y - first->translation.y) * weight,
        first->translation.z
            + (second->translation.z - first->translation.z) * weight,
    };
    out.scale = (gX_F32v3){
        first->scale.x + (second->scale.x - first->scale.x) * weight,
        first->scale.y + (second->scale.y - first->scale.y) * weight,
        first->scale.z + (second->scale.z - first->scale.z) * weight,
    };
    gX_F32 dot = first->rotation.x * second->rotation.x
        + first->rotation.y * second->rotation.y
        + first->rotation.z * second->rotation.z
        + first->rotation.w * second->rotation.w;
    gX_F32 sign = (dot < 0.0f) ? -1.0f : 1.0f;
    out.rotation = (gX_F32Qua){
        first->rotation.x
            + (sign * second->rotation.x - first->rotation.x) * weight,
        first->rotation.y
            + (sign * second->rotation.y - first->rotation.y) * weight,
        first->rotation.z
            + (sign * second->rotation.z - first->rotation.z) * weight,
        first->rotation.w
            + (sign * second->rotation.w - first->rotation.w) * weight,
    };
    gX_F32 length = sqrtf(out.rotation.x * out.rotation.x
        + out.rotation.y * out.rotation.y
        + out.rotation.z * out.rotation.z
        + out.rotation.w * out.rotation.w);
    if (length > 0.0001f) {
        out.rotation.x /= length;
        out.rotation.y /= length;
        out.rotation.z /= length;
        out.rotation.w /= length;
    } else {
        out.rotation = (gX_F32Qua){0.0f, 0.0f, 0.0f, 1.0f};
    }
    return out;
}

gX_I0 gX_RendererGFX_Animation_Update(gX_RendererGFX_Model *model,
    gX_F64 deltaTime) {
    if (!model || !model->loaded || !model->glbData) return;
    gX_RendererGFX_Model_Parameters *params = &model->params;
    gX_ModelSkeleton *skeleton = &model->model.skeleton;
    gX_RendererGFX_GLB_Data *glb = model->glbData;
    //opencode: pose overrides must evaluate even without a selected clip, so
    //   the early out only applies when neither source drives the pose.
    gX_I1 hasClip = (params->animClip >= 0
        && params->animClip < glb->animCount) ? 1 : 0;
    if (!hasClip && params->poseOverrideCount <= 0) return;
    if (skeleton->boneCount <= 0 || !skeleton->bones || !skeleton->bindPose
        || !model->model.currentPose || !model->model.boneMatrices) return;
    gX_I32 count = skeleton->boneCount;
    if (count > gX_GFX_ANIMATION_MAX_BONES)
        count = gX_GFX_ANIMATION_MAX_BONES;
    const gX_RendererGFX_GLB_Animation *clip = NULL;
    gX_F32 rangeStart = 0.0f;
    gX_F32 rangeEnd = 0.0f;
    gX_F32 time = 0.0f;
    if (hasClip) {
        clip = &glb->animations[params->animClip];
        gX_F32 duration = Animation_ClipDuration(clip);
        rangeEnd = duration;
        gX_RendererGFX_Animation_ClipRange(params->animStart, params->animEnd,
            duration, &rangeStart, &rangeEnd);

        if (params->animPlaying && isfinite(deltaTime) && deltaTime > 0.0) {
            if (deltaTime > 0.25) deltaTime = 0.25;
            gX_F64 speed = isfinite(params->animSpeed) ? params->animSpeed : 1.0;
            params->animTime += deltaTime * speed;
        }
        //opencode: trimmed playback — looping wraps inside the range, one-shot
        //   clamps at the range end and stops, and static scrubbing clamps into
        //   the range so the pose never leaves the authored trim.
        if (rangeEnd > rangeStart) {
            if (params->animPlaying && params->animLoop) {
                gX_F64 span = (gX_F64)(rangeEnd - rangeStart);
                gX_F64 relative = fmod(params->animTime - rangeStart, span);
                if (relative < 0.0) relative += span;
                params->animTime = rangeStart + relative;
            } else if (params->animTime >= rangeEnd) {
                params->animTime = rangeEnd;
                if (params->animPlaying) params->animPlaying = 0;
            } else if (params->animTime < rangeStart) {
                params->animTime = rangeStart;
            }
        } else {
            params->animTime = rangeStart;
        }
        time = (gX_F32)params->animTime;
    }
    //opencode: cross-fade — the primary clip drives the playhead; the
    //   secondary shares it (wrapping within its own duration when looping,
    //   clamping to its end otherwise). Blend weight 0 or a missing clip
    //   leaves the secondary out entirely.
    gX_I32 clip2Index = params->animClip2;
    gX_F32 blend = params->animBlend;
    if (!isfinite(blend)) blend = 0.0f;
    if (blend < 0.0f) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;
    const gX_RendererGFX_GLB_Animation *clip2 = NULL;
    gX_F32 time2 = time;
    if (hasClip && blend > 0.0f && clip2Index >= 0
        && clip2Index < glb->animCount) {
        clip2 = &glb->animations[clip2Index];
        gX_F32 duration2 = Animation_ClipDuration(clip2);
        if (duration2 > 0.0f) {
            if (params->animLoop) {
                time2 = fmodf(time2, duration2);
                if (time2 < 0.0f) time2 += duration2;
            } else if (time2 > duration2) {
                time2 = duration2;
            }
        } else {
            time2 = 0.0f;
        }
    }

    for (gX_I32 bi = 0; bi < count; bi++) {
        gX_Transform local = skeleton->bindPose[bi];
        gX_I32 node = clip ? Animation_FindNode(glb, skeleton->bones[bi].name)
                           : -1;
        Animation_SampleInto(clip, node, time, &local);
        if (clip2) {
            gX_Transform secondary = skeleton->bindPose[bi];
            Animation_SampleInto(clip2, node, time2, &secondary);
            local = Animation_BlendPose(&local, &secondary, blend);
        }
        model->model.currentPose[bi] = local;
    }
    Animation_ApplyOverrides(params, model->model.currentPose, count);

    gX_Matrix current[gX_GFX_ANIMATION_MAX_BONES];
    gX_Matrix bind[gX_GFX_ANIMATION_MAX_BONES];
    Animation_AccumulateWorld(skeleton, model->model.currentPose, count,
        current);
    Animation_AccumulateWorld(skeleton, skeleton->bindPose, count, bind);
    for (gX_I32 bi = 0; bi < count; bi++) {
        gX_Matrix inverseBind = gX_Matrix_Invert(bind[bi]);
        model->model.boneMatrices[bi] = gX_Matrix_Multiply(inverseBind,
            current[bi]);
    }

    if (getenv("GX_ANIM_TRACE")) {
        gX_Matrix *tip = &model->model.boneMatrices[count - 1];
        gX_Matrix *world = &current[count - 1];
        gX_TraceLog(gX_LOG_INFO,
            "GFX_ANIM: clip=%d time=%.3f playing=%d "
            "pose=(%.4f,%.4f,%.4f) world=(%.4f,%.4f,%.4f) range=(%.3f,%.3f) "
            "blend=%.3f clip2=%d",
            params->animClip, params->animTime, params->animPlaying,
            tip->m0, tip->m1, tip->m2, world->m12, world->m13, world->m14,
            rangeStart, rangeEnd, blend, clip2 ? clip2Index : -1);
    }
}

//opencode: find the clip channel targeting (node, path); -1 = none.
static gX_I32 Animation_ChannelIndexForPath(
    const gX_RendererGFX_GLB_Animation *clip, gX_I32 node, gX_I32 path) {
    for (gX_I32 ci = 0; ci < clip->channelCount; ci++) {
        if (clip->channels[ci].targetNode == node
            && clip->channels[ci].targetPath == path)
            return ci;
    }
    return -1;
}

//opencode: insert or replace a key (sorted by time). Returns 1 inserted,
//   0 replaced, -1 on allocation failure.
static gX_I32 Animation_ChannelAddKey(gX_RendererGFX_GLB_AnimChannel *channel,
    gX_F32 time, const gX_F32 *value) {
    gX_I32 stride = channel->valueStride;
    if (stride < 1) stride = 1;
    for (gX_I32 ki = 0; ki < channel->keyframeCount; ki++) {
        if (fabsf(channel->times[ki] - time) <= 0.0005f) {
            for (gX_I32 c = 0; c < stride; c++)
                channel->values[ki * stride + c] = value[c];
            return 0;
        }
    }
    gX_I32 at = 0;
    while (at < channel->keyframeCount && channel->times[at] < time) at++;
    gX_F32 *times = (gX_F32 *)realloc(channel->times,
        (size_t)(channel->keyframeCount + 1) * sizeof(gX_F32));
    if (!times) return -1;
    channel->times = times;
    gX_F32 *values = (gX_F32 *)realloc(channel->values,
        (size_t)(channel->keyframeCount + 1) * (size_t)stride
            * sizeof(gX_F32));
    if (!values) return -1;
    channel->values = values;
    memmove(&channel->times[at + 1], &channel->times[at],
        (size_t)(channel->keyframeCount - at) * sizeof(gX_F32));
    memmove(&channel->values[(at + 1) * stride], &channel->values[at * stride],
        (size_t)(channel->keyframeCount - at) * (size_t)stride
            * sizeof(gX_F32));
    channel->times[at] = time;
    for (gX_I32 c = 0; c < stride; c++)
        channel->values[at * stride + c] = value[c];
    channel->keyframeCount++;
    return 1;
}

//opencode: create an empty channel for (node, path); -1 = full.
static gX_I32 Animation_ChannelCreate(gX_RendererGFX_GLB_Animation *clip,
    gX_I32 node, gX_I32 path, gX_I32 stride) {
    if (clip->channelCount >= 8) return -1;
    gX_I32 index = clip->channelCount;
    gX_RendererGFX_GLB_AnimChannel *channel = &clip->channels[index];
    memset(channel, 0, sizeof(*channel));
    channel->targetNode = node;
    channel->targetMaterial = -1;
    channel->targetPath = path;
    channel->valueStride = stride;
    clip->channelCount++;
    return index;
}

gX_I0 gX_RendererGFX_Animation_PoseKey(gX_RendererGFX_Model *model,
    gX_I32 bone) {
    if (!model || !model->loaded || !model->glbData) return;
    gX_RendererGFX_Model_Parameters *params = &model->params;
    gX_ModelSkeleton *skeleton = &model->model.skeleton;
    gX_RendererGFX_GLB_Data *glb = model->glbData;
    if (params->animClip < 0 || params->animClip >= glb->animCount) return;
    if (!skeleton->bones || !model->model.currentPose) return;
    if (bone < 0 || bone >= skeleton->boneCount) return;
    gX_RendererGFX_GLB_Animation *clip = &glb->animations[params->animClip];
    gX_I32 node = Animation_FindNode(glb, skeleton->bones[bone].name);
    if (node < 0) return;
    //opencode: key the effective pose (clip + override) at the playhead.
    gX_Transform pose = model->model.currentPose[bone];
    gX_F32 length = sqrtf(pose.rotation.x * pose.rotation.x
        + pose.rotation.y * pose.rotation.y
        + pose.rotation.z * pose.rotation.z
        + pose.rotation.w * pose.rotation.w);
    if (length > 0.0001f) {
        pose.rotation.x /= length;
        pose.rotation.y /= length;
        pose.rotation.z /= length;
        pose.rotation.w /= length;
    } else {
        pose.rotation = (gX_F32Qua){0.0f, 0.0f, 0.0f, 1.0f};
    }
    gX_F32 time = (gX_F32)params->animTime;
    struct {
        gX_I32 path;
        gX_I32 stride;
        gX_F32 value[4];
    } keys[3] = {
        { 0, 3, { pose.translation.x, pose.translation.y, pose.translation.z,
                  0.0f } },
        { 1, 4, { pose.rotation.x, pose.rotation.y, pose.rotation.z,
                  pose.rotation.w } },
        { 2, 3, { pose.scale.x, pose.scale.y, pose.scale.z, 0.0f } },
    };
    for (gX_I32 ki = 0; ki < 3; ki++) {
        gX_I32 ci = Animation_ChannelIndexForPath(clip, node, keys[ki].path);
        if (ci < 0) {
            ci = Animation_ChannelCreate(clip, node, keys[ki].path,
                keys[ki].stride);
            if (ci < 0) continue;
        }
        Animation_ChannelAddKey(&clip->channels[ci], time, keys[ki].value);
    }
}

//opencode: sidecar helpers — the file sits next to the model, keyed by stem
//   ("...fixture.glb" -> "...fixture.anim.json"), matching the materials sidecar.
static gX_I1 Animation_SidecarPath(const gX_RendererGFX_Model *model,
    gX_I8 *out, size_t capacity) {
    if (!model || !model->path[0] || !out || capacity == 0)
        return 0;
    gX_I8 stem[gX_GFX_PATH_BUFFER];
    snprintf(stem, sizeof(stem), "%s", model->path);
    gX_I8 *dot = strrchr((char *)stem, '.');
    gX_I8 *slash = strrchr((char *)stem, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    int written = snprintf(out, capacity, "%s.anim.json", stem);
    return written > 0 && (size_t)written < capacity;
}

static const gX_I8 *Animation_PathName(gX_I32 path) {
    if (path == 0) return "translation";
    if (path == 1) return "rotation";
    if (path == 2) return "scale";
    return NULL;
}

static gX_I32 Animation_PathIndex(const gX_I8 *name) {
    if (!name) return -1;
    if (strcmp((const char *)name, "translation") == 0) return 0;
    if (strcmp((const char *)name, "rotation") == 0) return 1;
    if (strcmp((const char *)name, "scale") == 0) return 2;
    return -1;
}

gX_I0 gX_RendererGFX_Animation_SidecarLoad(gX_RendererGFX_Model *model) {
    if (!model || !model->glbData) return;
    gX_I8 path[gX_GFX_PATH_BUFFER + 64];
    if (!Animation_SidecarPath(model, path, sizeof(path))) return;
    struct json_object *root = json_object_from_file(path);
    if (!root) return;
    struct json_object *format = NULL;
    struct json_object *clips = NULL;
    gX_I32 loaded = 0;
    if (json_object_object_get_ex(root, "format", &format) && format
        && json_object_get_type(format) == json_type_string
        && strcmp(json_object_get_string(format), "gx-anim-v1") == 0
        && json_object_object_get_ex(root, "clips", &clips) && clips
        && json_object_get_type(clips) == json_type_array) {
        size_t clipCount = json_object_array_length(clips);
        for (size_t ci = 0; ci < clipCount; ci++) {
            struct json_object *clip = json_object_array_get_idx(clips, ci);
            struct json_object *name = NULL;
            struct json_object *keys = NULL;
            if (!clip || json_object_get_type(clip) != json_type_object
                || !json_object_object_get_ex(clip, "name", &name) || !name
                || json_object_get_type(name) != json_type_string
                || !json_object_object_get_ex(clip, "keys", &keys) || !keys
                || json_object_get_type(keys) != json_type_array)
                continue;
            const gX_I8 *clipName = json_object_get_string(name);
            gX_I32 clipIndex = -1;
            for (gX_I32 ai = 0; ai < model->glbData->animCount; ai++) {
                if (strcmp((const char *)model->glbData->animations[ai].name,
                        (const char *)clipName) == 0) {
                    clipIndex = ai;
                    break;
                }
            }
            if (clipIndex < 0) continue;
            gX_RendererGFX_GLB_Animation *animation =
                &model->glbData->animations[clipIndex];
            size_t keyCount = json_object_array_length(keys);
            for (size_t ki = 0; ki < keyCount; ki++) {
                struct json_object *key = json_object_array_get_idx(keys, ki);
                struct json_object *node = NULL;
                struct json_object *pathName = NULL;
                struct json_object *time = NULL;
                struct json_object *value = NULL;
                if (!key || json_object_get_type(key) != json_type_object
                    || !json_object_object_get_ex(key, "node", &node) || !node
                    || json_object_get_type(node) != json_type_string
                    || !json_object_object_get_ex(key, "path", &pathName)
                    || !pathName
                    || json_object_get_type(pathName) != json_type_string
                    || !json_object_object_get_ex(key, "time", &time) || !time
                    || (json_object_get_type(time) != json_type_double
                        && json_object_get_type(time) != json_type_int)
                    || !json_object_object_get_ex(key, "value", &value)
                    || !value
                    || json_object_get_type(value) != json_type_array)
                    continue;
                gX_I32 pathIndex = Animation_PathIndex(
                    json_object_get_string(pathName));
                if (pathIndex < 0) continue;
                gX_I32 stride = (pathIndex == 1) ? 4 : 3;
                if ((gX_I32)json_object_array_length(value) != stride)
                    continue;
                gX_I32 nodeIndex = Animation_FindNode(model->glbData,
                    json_object_get_string(node));
                if (nodeIndex < 0) continue;
                gX_F32 values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                gX_I1 valid = 1;
                for (gX_I32 vi = 0; vi < stride; vi++) {
                    struct json_object *component =
                        json_object_array_get_idx(value, (size_t)vi);
                    if (!component
                        || (json_object_get_type(component) != json_type_int
                            && json_object_get_type(component)
                                != json_type_double)) {
                        valid = 0;
                        break;
                    }
                    values[vi] = (gX_F32)json_object_get_double(component);
                }
                if (!valid) continue;
                gX_I32 channelIndex = Animation_ChannelIndexForPath(animation,
                    nodeIndex, pathIndex);
                if (channelIndex < 0)
                    channelIndex = Animation_ChannelCreate(animation,
                        nodeIndex, pathIndex, stride);
                if (channelIndex < 0) continue;
                if (Animation_ChannelAddKey(&animation->channels[channelIndex],
                        (gX_F32)json_object_get_double(time), values) >= 0)
                    loaded++;
            }
        }
    }
    json_object_put(root);
    if (loaded > 0)
        gX_TraceLog(gX_LOG_NOTICE, "ANIM: %d authored key(s) loaded from %s",
            loaded, path);
}

gX_I32 gX_RendererGFX_Animation_SidecarSave(gX_RendererGFX_Model *model) {
    if (!model || !model->glbData) return 0;
    gX_I8 path[gX_GFX_PATH_BUFFER + 64];
    if (!Animation_SidecarPath(model, path, sizeof(path))) return 0;
    struct json_object *root = json_object_new_object();
    struct json_object *clips = json_object_new_array();
    if (!root || !clips) {
        if (root) json_object_put(root);
        if (clips) json_object_put(clips);
        return 0;
    }
    json_object_object_add(root, "format",
        json_object_new_string("gx-anim-v1"));
    gX_I32 written = 0;
    for (gX_I32 ai = 0; ai < model->glbData->animCount; ai++) {
        gX_RendererGFX_GLB_Animation *animation =
            &model->glbData->animations[ai];
        struct json_object *clip = json_object_new_object();
        struct json_object *keys = json_object_new_array();
        if (!clip || !keys) {
            if (clip) json_object_put(clip);
            if (keys) json_object_put(keys);
            continue;
        }
        json_object_object_add(clip, "name",
            json_object_new_string(animation->name));
        for (gX_I32 ci = 0; ci < animation->channelCount; ci++) {
            gX_RendererGFX_GLB_AnimChannel *channel = &animation->channels[ci];
            const gX_I8 *pathName = Animation_PathName(channel->targetPath);
            if (!pathName || !channel->times || !channel->values
                || channel->keyframeCount <= 0)
                continue;
            const gX_I8 *nodeName = "?";
            if (channel->targetNode >= 0
                && channel->targetNode < model->glbData->nodeCount)
                nodeName = model->glbData->nodes[channel->targetNode].name;
            gX_I32 stride = channel->valueStride;
            if (stride < 1) stride = 1;
            for (gX_I32 ki = 0; ki < channel->keyframeCount; ki++) {
                struct json_object *key = json_object_new_object();
                struct json_object *value = json_object_new_array();
                if (!key || !value) {
                    if (key) json_object_put(key);
                    if (value) json_object_put(value);
                    continue;
                }
                json_object_object_add(key, "node",
                    json_object_new_string(nodeName));
                json_object_object_add(key, "path",
                    json_object_new_string(pathName));
                json_object_object_add(key, "time",
                    json_object_new_double(channel->times[ki]));
                for (gX_I32 vi = 0; vi < stride; vi++)
                    json_object_array_add(value, json_object_new_double(
                        channel->values[ki * stride + vi]));
                json_object_object_add(key, "value", value);
                json_object_array_add(keys, key);
                written++;
            }
        }
        json_object_object_add(clip, "keys", keys);
        json_object_array_add(clips, clip);
    }
    json_object_object_add(root, "clips", clips);
    gX_I1 saved = json_object_to_file_ext(path, root,
        JSON_C_TO_STRING_PRETTY_TAB) == 0;
    json_object_put(root);
    if (saved)
        gX_TraceLog(gX_LOG_NOTICE, "ANIM: %d key(s) saved to %s", written, path);
    return saved ? 1 : 0;
}
