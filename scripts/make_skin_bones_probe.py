#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: generate resources/model/probe/skinprobe80.glb — an 80-joint chain
#   driving a quad strip, sized just past the historic 64-bone upload cap. The
#   file is self-authored and deterministic (no third-party content), and the
#   "wave" clip rotates the top joint so the pose visibly deforms.
#   Run from the repo root:
#   python3 scripts/make_skin_bones_probe.py

import argparse
import json
import math
import os
import struct
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, "resources", "model", "probe", "skinprobe80.glb")
JOINTS = 80
SPAN = 2.0
STEP = SPAN / JOINTS

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
    global JOINTS, STEP
    parser = argparse.ArgumentParser()
    parser.add_argument("--joints", type=int, default=JOINTS)
    parser.add_argument("--output", default=DST)
    options = parser.parse_args()
    JOINTS = options.joints
    STEP = SPAN / JOINTS
    destination = options.output
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    positions = []
    joints = []
    weights = []
    for index in range(JOINTS):
        y = index * STEP
        for x in (-0.125, 0.125):
            positions.append((x, y, 0.0))
            joints.append((index, 0, 0, 0))
            weights.append((1.0, 0.0, 0.0, 0.0))
    indices = []
    for index in range(JOINTS - 1):
        a, b = 2 * index, 2 * index + 1
        c, d = 2 * (index + 1), 2 * (index + 1) + 1
        indices += [a, b, c, c, b, d]
    ibm = []
    for index in range(JOINTS):
        matrix = list(identity)
        matrix[13] = -(index * STEP)
        ibm.append(matrix)
    #opencode: a cumulative curl — every joint rotates 1 degree at t=1, so the
    #   chain bends smoothly and the top of the ribbon swings visibly (a single
    #   top-joint twist moves its own vertices by only a fraction of a step).
    half = math.radians(1.0) * 0.5
    wave = [0.0, 0.0, math.sin(half), math.cos(half)]
    times = [0.0, 1.0, 2.0]
    rotations = [[0.0, 0.0, 0.0, 1.0], wave, [0.0, 0.0, 0.0, 1.0]]

    blob = bytearray()
    gltf_views = []

    def add_view(payload, target=None):
        offset = len(blob)
        blob.extend(align4(payload))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        gltf_views.append(view)
        return len(gltf_views) - 1

    normals = [(0.0, 0.0, 1.0)] * len(positions)
    position_view = add_view(b"".join(struct.pack("<3f", *value) for value in positions), 34962)
    normal_view = add_view(b"".join(struct.pack("<3f", *value) for value in normals), 34962)
    joints_view = add_view(b"".join(struct.pack("<4H", *value) for value in joints), 34962)
    weights_view = add_view(b"".join(struct.pack("<4f", *value) for value in weights), 34962)
    index_view = add_view(b"".join(struct.pack("<H", value) for value in indices), 34963)
    ibm_view = add_view(b"".join(struct.pack("<16f", *value) for value in ibm))
    time_view = add_view(b"".join(struct.pack("<f", value) for value in times))
    rot_view = add_view(b"".join(struct.pack("<4f", *value) for value in rotations))

    nodes = []
    for index in range(JOINTS):
        node = {"name": "j%02d" % index,
                "translation": [0.0, STEP if index else 0.0, 0.0]}
        if index + 1 < JOINTS:
            node["children"] = [index + 1]
        nodes.append(node)
    nodes.append({"name": "body", "mesh": 0, "skin": 0})

    gltf = {
        "asset": {"version": "2.0", "generator": "gX make_skin_bones_probe"},
        "scene": 0,
        "scenes": [{"nodes": [0, JOINTS]}],
        "nodes": nodes,
        "skins": [{"name": "chain", "joints": list(range(JOINTS)),
                   "inverseBindMatrices": ibm_view, "skeleton": 0}],
        "meshes": [{
            "name": "ribbon",
            "primitives": [{
                "attributes": {"POSITION": position_view, "NORMAL": normal_view,
                               "JOINTS_0": joints_view, "WEIGHTS_0": weights_view},
                "indices": index_view,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "probe",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.25, 0.75, 0.35, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.55,
            },
            "doubleSided": True,
        }],
        "animations": [{
            "name": "wave",
            "samplers": [{"input": time_view, "output": rot_view,
                          "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": index,
                                                   "path": "rotation"}}
                         for index in range(JOINTS)],
        }],
        "accessors": [
            {"bufferView": position_view, "componentType": 5126,
             "count": len(positions), "type": "VEC3",
             "min": [-0.125, 0.0, 0.0], "max": [0.125, SPAN, 0.0]},
            {"bufferView": normal_view, "componentType": 5126,
             "count": len(normals), "type": "VEC3"},
            {"bufferView": joints_view, "componentType": 5123,
             "count": len(joints), "type": "VEC4"},
            {"bufferView": weights_view, "componentType": 5126,
             "count": len(weights), "type": "VEC4"},
            {"bufferView": index_view, "componentType": 5123,
             "count": len(indices), "type": "SCALAR"},
            {"bufferView": ibm_view, "componentType": 5126,
             "count": len(ibm), "type": "MAT4"},
            {"bufferView": time_view, "componentType": 5126,
             "count": len(times), "type": "SCALAR", "min": [0.0], "max": [2.0]},
            {"bufferView": rot_view, "componentType": 5126,
             "count": len(rotations), "type": "VEC4"},
        ],
        "bufferViews": gltf_views,
        "buffers": [{"byteLength": len(blob)}],
    }
    write_glb(gltf, bytes(blob), destination)
    print("wrote %s (%d bytes)" % (destination, os.path.getsize(destination)))


if __name__ == "__main__":
    sys.exit(main())
