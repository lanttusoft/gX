#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: generate resources/model/probe/animprobe.glb — a self-authored,
#   deterministic skinned fixture for the animation gate. Two joints ("root",
#   "tip") drive a 0.25 x 2.0 quad strip; the "swing" clip rotates the tip +30
#   degrees around Z at t=1 and back at t=2, and the shorter "twist" clip
#   rotates it +60 degrees around X at t=0.5 and back at t=1, so clip blending
#   (and secondary wrap/clamp behavior) are observable without touching the
#   shipped hero assets.
#   Run from the repo root:
#   python3 scripts/make_animation_probe.py

import json
import math
import os
import struct
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, "resources", "model", "probe", "animprobe.glb")

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942


def write_glb(gltf, blob, path):
    json_payload = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_payload += b" " * ((4 - len(json_payload) % 4) % 4)
    bin_payload = blob + b"\x00" * ((4 - len(blob) % 4) % 4)
    body = struct.pack("<II", len(json_payload), CHUNK_JSON) + json_payload
    body += struct.pack("<II", len(bin_payload), CHUNK_BIN) + bin_payload
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as stream:
        stream.write(struct.pack("<III", GLB_MAGIC, 2, 12 + len(body)))
        stream.write(body)


def align4(blob):
    return blob + b"\x00" * ((4 - len(blob) % 4) % 4)


def main():
    #opencode: geometry — quad strip, bottom edge on "root", top edge on "tip".
    positions = [(-0.125, 0.0, 0.0), (0.125, 0.0, 0.0),
                 (-0.125, 2.0, 0.0), (0.125, 2.0, 0.0)]
    normals = [(0.0, 0.0, 1.0)] * 4
    joints = [(0, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
    weights = [(1.0, 0.0, 0.0, 0.0)] * 4
    indices = [0, 1, 2, 2, 1, 3]

    #opencode: inverse bind matrices — root at origin, tip at (0,1,0).
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    tip_inverse = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
    ibm = [identity, tip_inverse]

    #opencode: swing clip — identity -> rotZ(30deg) -> identity (x,y,z,w).
    half = math.radians(30.0) * 0.5
    swing = [0.0, 0.0, math.sin(half), math.cos(half)]
    times = [0.0, 1.0, 2.0]
    rotations = [[0.0, 0.0, 0.0, 1.0], swing, [0.0, 0.0, 0.0, 1.0]]
    #opencode: twist clip — identity -> rotX(60deg) -> identity on a shorter
    #   1.0s timeline for blend wrap/clamp coverage.
    twist_half = math.radians(60.0) * 0.5
    twist = [math.sin(twist_half), 0.0, 0.0, math.cos(twist_half)]
    twist_times = [0.0, 0.5, 1.0]
    twist_rotations = [[0.0, 0.0, 0.0, 1.0], twist, [0.0, 0.0, 0.0, 1.0]]

    blob = bytearray()
    views = {}

    def add_view(name, payload, target=None):
        offset = len(blob)
        blob.extend(align4(payload))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        views[name] = len(views)
        gltf_views.append(view)
        return views[name], offset

    gltf_views = []
    position_view, _ = add_view("positions",
        b"".join(struct.pack("<3f", *value) for value in positions), 34962)
    normal_view, _ = add_view("normals",
        b"".join(struct.pack("<3f", *value) for value in normals), 34962)
    joints_view, _ = add_view("joints",
        b"".join(struct.pack("<4H", *value) for value in joints), 34962)
    weights_view, _ = add_view("weights",
        b"".join(struct.pack("<4f", *value) for value in weights), 34962)
    index_view, _ = add_view("indices",
        b"".join(struct.pack("<H", value) for value in indices), 34963)
    ibm_view, _ = add_view("ibm",
        b"".join(struct.pack("<16f", *value) for value in ibm))
    time_view, _ = add_view("times",
        b"".join(struct.pack("<f", value) for value in times))
    rot_view, _ = add_view("rotations",
        b"".join(struct.pack("<4f", *value) for value in rotations))
    twist_time_view, _ = add_view("twist_times",
        b"".join(struct.pack("<f", value) for value in twist_times))
    twist_rot_view, _ = add_view("twist_rotations",
        b"".join(struct.pack("<4f", *value) for value in twist_rotations))

    gltf = {
        "asset": {"version": "2.0", "generator": "gX make_animation_probe"},
        "scene": 0,
        "scenes": [{"nodes": [0, 2]}],
        "nodes": [
            {"name": "root", "translation": [0.0, 0.0, 0.0], "children": [1]},
            {"name": "tip", "translation": [0.0, 1.0, 0.0]},
            {"name": "arm", "mesh": 0, "skin": 0},
        ],
        "skins": [{"name": "armature", "joints": [0, 1],
                   "inverseBindMatrices": 5, "skeleton": 0}],
        "meshes": [{
            "name": "ribbon",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1,
                               "JOINTS_0": 2, "WEIGHTS_0": 3},
                "indices": 4,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "probe",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.25, 0.25, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.55,
            },
            "doubleSided": True,
        }],
        "animations": [
            {
                "name": "swing",
                "samplers": [{"input": 6, "output": 7,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0, "target": {"node": 1,
                                                       "path": "rotation"}}],
            },
            {
                "name": "twist",
                "samplers": [{"input": 8, "output": 9,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0, "target": {"node": 1,
                                                       "path": "rotation"}}],
            },
        ],
        "accessors": [
            {"bufferView": position_view, "componentType": 5126, "count": 4,
             "type": "VEC3",
             "min": [-0.125, 0.0, 0.0], "max": [0.125, 2.0, 0.0]},
            {"bufferView": normal_view, "componentType": 5126, "count": 4,
             "type": "VEC3"},
            {"bufferView": joints_view, "componentType": 5123, "count": 4,
             "type": "VEC4"},
            {"bufferView": weights_view, "componentType": 5126, "count": 4,
             "type": "VEC4"},
            {"bufferView": index_view, "componentType": 5123, "count": 6,
             "type": "SCALAR"},
            {"bufferView": ibm_view, "componentType": 5126, "count": 2,
             "type": "MAT4"},
            {"bufferView": time_view, "componentType": 5126, "count": 3,
             "type": "SCALAR", "min": [0.0], "max": [2.0]},
            {"bufferView": rot_view, "componentType": 5126, "count": 3,
             "type": "VEC4"},
            {"bufferView": twist_time_view, "componentType": 5126, "count": 3,
             "type": "SCALAR", "min": [0.0], "max": [1.0]},
            {"bufferView": twist_rot_view, "componentType": 5126, "count": 3,
             "type": "VEC4"},
        ],
        "bufferViews": gltf_views,
        "buffers": [{"byteLength": len(blob)}],
    }
    write_glb(gltf, bytes(blob), DST)
    print("wrote %s (%d bytes)" % (DST, os.path.getsize(DST)))


if __name__ == "__main__":
    sys.exit(main())
