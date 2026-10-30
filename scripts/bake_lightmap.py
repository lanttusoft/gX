#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX static lightmap baker.

Rasterizes TEXCOORD_1 from a GLB into a linear RGBA8 PNG. The baked RGB is
an irradiance multiplier consumed additively by standard.frag; it is not a
replacement for a full offline renderer or a GI solver.
"""

import argparse
import json
import math
import struct
import sys
import zlib


COMPONENTS = {
    5120: ("b", 1, -128.0, 127.0),
    5121: ("B", 1, 0.0, 255.0),
    5122: ("h", 2, -32768.0, 32767.0),
    5123: ("H", 2, 0.0, 65535.0),
    5125: ("I", 4, 0.0, 4294967295.0),
    5126: ("f", 4, 0.0, 1.0),
}
TYPE_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def load_glb(path):
    data = open(path, "rb").read()
    if len(data) < 12 or data[:4] != b"glTF":
        raise ValueError("input is not a GLB file")
    _, _, total = struct.unpack_from("<4sII", data, 0)
    offset = 12
    json_chunk = None
    binary_chunk = b""
    while offset + 8 <= total:
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + length]
        if kind == 0x4E4F534A:
            json_chunk = chunk
        elif kind == 0x004E4942:
            binary_chunk = chunk
        offset += length
    if json_chunk is None:
        raise ValueError("GLB has no JSON chunk")
    return json.loads(json_chunk.rstrip(b" \t\r\n\x00")), binary_chunk


def read_accessor(document, binary, index):
    accessor = document["accessors"][index]
    view_index = accessor.get("bufferView")
    if view_index is None:
        raise ValueError("sparse/accessor without bufferView is unsupported")
    view = document["bufferViews"][view_index]
    component = COMPONENTS.get(accessor["componentType"])
    width = TYPE_WIDTH.get(accessor["type"])
    if component is None or width is None:
        raise ValueError("unsupported accessor format")
    fmt, size, low, high = component
    stride = view.get("byteStride", size * width)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    normalized = accessor.get("normalized", False)
    rows = []
    for row in range(accessor["count"]):
        base = start + row * stride
        values = list(struct.unpack_from("<" + fmt * width, binary, base))
        if normalized:
            if low < 0.0:
                values = [max(value / -low, -1.0) for value in values]
            else:
                values = [value / high for value in values]
        rows.append(tuple(values))
    return rows


def identity():
    return (1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0)


def mat_mul(a, b):
    return tuple(sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
                 for row in range(4) for col in range(4))


def transform_point(matrix, point):
    return tuple(matrix[row * 4] * point[0]
                 + matrix[row * 4 + 1] * point[1]
                 + matrix[row * 4 + 2] * point[2]
                 + matrix[row * 4 + 3]
                 for row in range(3))


def transform_direction(matrix, direction):
    return tuple(matrix[row * 3] * direction[0]
                 + matrix[row * 3 + 1] * direction[1]
                 + matrix[row * 3 + 2] * direction[2]
                 for row in range(3))


def inverse_transpose_3x3(matrix):
    a, b, c = matrix[0], matrix[1], matrix[2]
    d, e, f = matrix[4], matrix[5], matrix[6]
    g, h, i = matrix[8], matrix[9], matrix[10]
    co00, co01, co02 = e * i - f * h, f * g - d * i, d * h - e * g
    co10, co11, co12 = c * h - b * i, a * i - c * g, b * g - a * h
    co20, co21, co22 = b * f - c * e, c * d - a * f, a * e - b * d
    det = a * co00 + b * co01 + c * co02
    if abs(det) < 1.0e-12:
        return (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
    inv = 1.0 / det
    #opencode: return inverse-transpose (not inverse) for transforming
    #   glTF normals under non-uniform scale and rotation.
    return (co00 * inv, co01 * inv, co02 * inv,
            co10 * inv, co11 * inv, co12 * inv,
            co20 * inv, co21 * inv, co22 * inv)


def node_matrix(node):
    if "matrix" in node:
        values = node["matrix"]
        return tuple(values[col * 4 + row] for row in range(4) for col in range(4))
    translation = node.get("translation", (0.0, 0.0, 0.0))
    rotation = node.get("rotation", (0.0, 0.0, 0.0, 1.0))
    scale = node.get("scale", (1.0, 1.0, 1.0))
    x, y, z, w = rotation
    rotation_matrix = (
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w), translation[0],
        2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w), translation[1],
        2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y), translation[2],
        0.0, 0.0, 0.0, 1.0)
    scale_matrix = (scale[0], 0.0, 0.0, 0.0,
                    0.0, scale[1], 0.0, 0.0,
                    0.0, 0.0, scale[2], 0.0,
                    0.0, 0.0, 0.0, 1.0)
    return mat_mul(rotation_matrix, scale_matrix)


def normalize(vector):
    length = math.sqrt(sum(value * value for value in vector))
    if length < 1.0e-12:
        return (0.0, 1.0, 0.0)
    return tuple(value / length for value in vector)


def sub(a, b):
    return tuple(a[index] - b[index] for index in range(3))


def add(a, b):
    return tuple(a[index] + b[index] for index in range(3))


def mul(vector, scalar):
    return tuple(value * scalar for value in vector)


def dot(a, b):
    return sum(a[index] * b[index] for index in range(3))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def collect_triangles(document, binary, uv_set):
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    triangles = []

    def append_mesh(mesh_index, world):
        mesh = meshes[mesh_index]
        normal_matrix = inverse_transpose_3x3(world)
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            attributes = primitive.get("attributes", {})
            if "POSITION" not in attributes:
                continue
            uv_name = "TEXCOORD_%d" % uv_set
            if uv_name not in attributes:
                raise ValueError("mesh primitive is missing %s; generate a second UV unwrap first" % uv_name)
            positions = read_accessor(document, binary, attributes["POSITION"])
            uvs = read_accessor(document, binary, attributes[uv_name])
            normals = read_accessor(document, binary, attributes["NORMAL"]) if "NORMAL" in attributes else None
            if "indices" in primitive:
                indices = [int(row[0]) for row in read_accessor(document, binary, primitive["indices"])]
            else:
                indices = list(range(len(positions)))
            for offset in range(0, len(indices) - 2, 3):
                i0, i1, i2 = indices[offset:offset + 3]
                if max(i0, i1, i2) >= len(positions):
                    continue
                points = [transform_point(world, positions[index]) for index in (i0, i1, i2)]
                face_normal = normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])))
                if normals is None:
                    ns = [face_normal, face_normal, face_normal]
                else:
                    ns = [normalize(transform_direction(normal_matrix, normals[index]))
                          for index in (i0, i1, i2)]
                triangles.append((points, [uvs[i0], uvs[i1], uvs[i2]], ns))

    def walk(node_index, parent):
        node = nodes[node_index]
        world = mat_mul(parent, node_matrix(node))
        if "mesh" in node:
            append_mesh(node["mesh"], world)
        for child in node.get("children", []):
            walk(child, world)

    if nodes:
        children = {child for node in nodes for child in node.get("children", [])}
        roots = [index for index in range(len(nodes)) if index not in children]
        for root in roots:
            walk(root, identity())
    else:
        for mesh_index in range(len(meshes)):
            append_mesh(mesh_index, identity())
    if not triangles:
        raise ValueError("GLB contains no triangle primitives with the requested UV set")
    return triangles


class TriangleBVH:
    def __init__(self, triangles):
        self.triangles = triangles
        self.bounds = []
        for points, _, _ in triangles:
            low = tuple(min(point[axis] for point in points) for axis in range(3))
            high = tuple(max(point[axis] for point in points) for axis in range(3))
            self.bounds.append((low, high))
        self.nodes = []
        self.build(list(range(len(triangles))))

    def build(self, indices):
        low = tuple(min(self.bounds[index][0][axis] for index in indices) for axis in range(3))
        high = tuple(max(self.bounds[index][1][axis] for index in indices) for axis in range(3))
        node_index = len(self.nodes)
        self.nodes.append((low, high, None, None, None))
        if len(indices) <= 8:
            self.nodes[node_index] = (low, high, None, None, tuple(indices))
            return node_index
        axis = max(range(3), key=lambda item: high[item] - low[item])
        indices.sort(key=lambda index: (self.bounds[index][0][axis] + self.bounds[index][1][axis]) * 0.5)
        middle = len(indices) // 2
        left = self.build(indices[:middle])
        right = self.build(indices[middle:])
        self.nodes[node_index] = (low, high, left, right, None)
        return node_index

    @staticmethod
    def box_hit(origin, direction, low, high, limit):
        near = 0.0
        far = limit
        for axis in range(3):
            if abs(direction[axis]) < 1.0e-12:
                if origin[axis] < low[axis] or origin[axis] > high[axis]:
                    return False
                continue
            inv = 1.0 / direction[axis]
            first = (low[axis] - origin[axis]) * inv
            last = (high[axis] - origin[axis]) * inv
            if first > last:
                first, last = last, first
            near = max(near, first)
            far = min(far, last)
            if near > far:
                return False
        return True

    def hit(self, origin, direction, limit=float("inf")):
        stack = [0]
        while stack:
            node = self.nodes[stack.pop()]
            if not self.box_hit(origin, direction, node[0], node[1], limit):
                continue
            if node[4] is None:
                stack.append(node[2])
                stack.append(node[3])
                continue
            for index in node[4]:
                points = self.triangles[index][0]
                edge_a = sub(points[1], points[0])
                edge_b = sub(points[2], points[0])
                pvec = cross(direction, edge_b)
                determinant = dot(edge_a, pvec)
                if abs(determinant) < 1.0e-10:
                    continue
                inverse = 1.0 / determinant
                tvec = sub(origin, points[0])
                u = dot(tvec, pvec) * inverse
                if u < 0.0 or u > 1.0:
                    continue
                qvec = cross(tvec, edge_a)
                v = dot(direction, qvec) * inverse
                if v < 0.0 or u + v > 1.0:
                    continue
                distance = dot(edge_b, qvec) * inverse
                if distance > 1.0e-5 and distance < limit:
                    return True
        return False


def png_chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path, width, height, pixels):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        start = y * width * 4
        rows.extend(pixels[start:start + width * 4])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    payload += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(payload)


def parse_vector(value, name):
    try:
        result = tuple(float(part) for part in value.split(","))
    except ValueError as error:
        raise ValueError("%s must be comma-separated numbers" % name) from error
    if len(result) != 3:
        raise ValueError("%s needs exactly three components" % name)
    return result


def parse_manifest(path):
    with open(path, "r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict):
        raise ValueError("lightmap manifest must contain an object")
    raw_lights = manifest.get("lights")
    if not isinstance(raw_lights, list) or not raw_lights:
        raise ValueError("lightmap manifest needs a non-empty lights array")
    lights = []
    for index, raw in enumerate(raw_lights):
        if not isinstance(raw, dict):
            raise ValueError("light %d must be an object" % index)
        light_type = raw.get("type", "directional")
        if light_type not in ("directional", "point"):
            raise ValueError("light %d has unsupported type %r" % (index, light_type))
        color = tuple(float(value) for value in raw.get("color", (1.0, 1.0, 1.0)))
        if len(color) != 3 or not all(math.isfinite(value) and value >= 0.0 for value in color):
            raise ValueError("light %d color must contain three non-negative numbers" % index)
        intensity = float(raw.get("intensity", 1.0))
        if not math.isfinite(intensity) or intensity < 0.0:
            raise ValueError("light %d intensity must be non-negative" % index)
        light = {"type": light_type, "color": color, "intensity": intensity}
        field = "direction" if light_type == "directional" else "position"
        vector = raw.get(field)
        if not isinstance(vector, (list, tuple)) or len(vector) != 3:
            raise ValueError("light %d %s must contain three numbers" % (index, field))
        vector = tuple(float(value) for value in vector)
        if not all(math.isfinite(value) for value in vector):
            raise ValueError("light %d %s contains a non-finite number" % (index, field))
        if field == "direction" and dot(vector, vector) < 1.0e-12:
            raise ValueError("light %d direction must not be zero" % index)
        light[field] = vector
        if light_type == "point":
            light_range = float(raw.get("range", 0.0))
            if not math.isfinite(light_range) or light_range < 0.0:
                raise ValueError("light %d range must be non-negative" % index)
            light["range"] = light_range
        lights.append(light)
    settings = {
        "lights": lights,
        "size": int(manifest.get("size", 256)),
        "ambient": float(manifest.get("ambient", 0.15)),
        "bias": float(manifest.get("bias", 0.001)),
        "dilate": int(manifest.get("dilate", 3)),
        "strict_uv": bool(manifest.get("strict_uv", False)),
    }
    if settings["size"] < 16 or settings["size"] > 4096:
        raise ValueError("manifest size must be between 16 and 4096")
    if not math.isfinite(settings["ambient"]) or settings["ambient"] < 0.0:
        raise ValueError("manifest ambient must be non-negative")
    if not math.isfinite(settings["bias"]) or settings["bias"] <= 0.0:
        raise ValueError("manifest bias must be positive")
    if settings["dilate"] < 0:
        raise ValueError("manifest dilate must be non-negative")
    return settings


def bake(triangles, size, light_direction=None, light_color=None, intensity=0.8,
         ambient=0.15, bias=0.001, dilation=3, lights=None, diagnostics=False):
    if lights is None:
        lights = [{
            "type": "directional",
            "direction": normalize(light_direction),
            "color": light_color,
            "intensity": intensity,
        }]
    bvh = TriangleBVH(triangles)
    pixels = [(0.0, 0.0, 0.0)] * (size * size)
    covered = bytearray(size * size)
    overlap_pixels = bytearray(size * size)
    overlaps = 0
    degenerate_triangles = 0
    uv_out_of_bounds_triangles = 0
    uv_low = [float("inf"), float("inf")]
    uv_high = [float("-inf"), float("-inf")]
    for points, uvs, normals in triangles:
        for uv in uvs:
            uv_low[0] = min(uv_low[0], uv[0])
            uv_low[1] = min(uv_low[1], uv[1])
            uv_high[0] = max(uv_high[0], uv[0])
            uv_high[1] = max(uv_high[1], uv[1])
        if any(value < 0.0 or value > 1.0 for uv in uvs for value in uv):
            uv_out_of_bounds_triangles += 1
        area = ((uvs[1][0] - uvs[0][0]) * (uvs[2][1] - uvs[0][1])
                - (uvs[1][1] - uvs[0][1]) * (uvs[2][0] - uvs[0][0]))
        if abs(area) < 1.0e-12:
            degenerate_triangles += 1
            continue
        low_u = max(0, int(math.ceil(min(uv[0] for uv in uvs) * size - 0.5)))
        high_u = min(size - 1, int(math.floor(max(uv[0] for uv in uvs) * size - 0.5)))
        low_v = max(0, int(math.ceil(min(uv[1] for uv in uvs) * size - 0.5)))
        high_v = min(size - 1, int(math.floor(max(uv[1] for uv in uvs) * size - 0.5)))
        for y in range(low_v, high_v + 1):
            v_coord = (y + 0.5) / size
            for x in range(low_u, high_u + 1):
                u_coord = (x + 0.5) / size
                w0 = ((uvs[1][1] - uvs[2][1]) * (u_coord - uvs[2][0])
                      + (uvs[2][0] - uvs[1][0]) * (v_coord - uvs[2][1])) / area
                w1 = ((uvs[2][1] - uvs[0][1]) * (u_coord - uvs[0][0])
                      + (uvs[0][0] - uvs[2][0]) * (v_coord - uvs[0][1])) / area
                w2 = 1.0 - w0 - w1
                if min(w0, w1, w2) < -1.0e-6:
                    continue
                index = y * size + x
                if covered[index]:
                    overlaps += 1
                    overlap_pixels[index] = 1
                position = tuple(points[0][axis] * w0 + points[1][axis] * w1 + points[2][axis] * w2
                                  for axis in range(3))
                normal = normalize(tuple(normals[0][axis] * w0 + normals[1][axis] * w1 + normals[2][axis] * w2
                                          for axis in range(3)))
                shadow_origin = add(position, mul(normal, bias))
                color = [ambient, ambient, ambient]
                for light in lights:
                    if light["type"] == "directional":
                        direction = normalize(light["direction"])
                        limit = float("inf")
                        attenuation = 1.0
                    else:
                        to_light = sub(light["position"], position)
                        distance = math.sqrt(dot(to_light, to_light))
                        if distance < 1.0e-12:
                            continue
                        direction = mul(to_light, 1.0 / distance)
                        limit = distance
                        if light["range"] > 0.0:
                            attenuation = max(0.0, 1.0 - distance / light["range"]) ** 2
                        else:
                            attenuation = 1.0 / (1.0 + distance * distance)
                    direct = max(dot(normal, direction), 0.0)
                    blocked = direct > 0.0 and bvh.hit(shadow_origin, direction, limit)
                    if blocked:
                        continue
                    contribution = direct * light["intensity"] * attenuation
                    for axis in range(3):
                        color[axis] += contribution * light["color"][axis]
                pixels[index] = tuple(max(0.0, value) for value in color)
                covered[index] = 1
    covered_before_dilation = sum(covered)
    for _ in range(max(0, dilation)):
        source = list(pixels)
        source_covered = bytearray(covered)
        for y in range(size):
            for x in range(size):
                index = y * size + x
                if covered[index]:
                    continue
                neighbors = []
                if x > 0 and covered[index - 1]:
                    neighbors.append(source[index - 1])
                if x + 1 < size and covered[index + 1]:
                    neighbors.append(source[index + 1])
                if y > 0 and covered[index - size]:
                    neighbors.append(source[index - size])
                if y + 1 < size and covered[index + size]:
                    neighbors.append(source[index + size])
                if neighbors:
                    source_covered[index] = 1
                    pixels[index] = neighbors[0]
        covered = source_covered
    output = bytearray(size * size * 4)
    for index, color in enumerate(pixels):
        output[index * 4:index * 4 + 4] = bytes(
            min(255, max(0, int(round(value * 255.0)))) for value in color) + b"\xff"
    if not diagnostics:
        return output, sum(covered), overlaps
    bounds_low = [max(0, int(math.ceil(value * size - 0.5))) for value in uv_low]
    bounds_high = [min(size - 1, int(math.floor(value * size - 0.5))) for value in uv_high]
    bounds_texels = 0
    uncovered_in_bounds = 0
    if bounds_low[0] <= bounds_high[0] and bounds_low[1] <= bounds_high[1]:
        for y in range(bounds_low[1], bounds_high[1] + 1):
            for x in range(bounds_low[0], bounds_high[0] + 1):
                bounds_texels += 1
                if not covered[y * size + x]:
                    uncovered_in_bounds += 1
    stats = {
        "triangle_count": len(triangles),
        "degenerate_triangles": degenerate_triangles,
        "uv_out_of_bounds_triangles": uv_out_of_bounds_triangles,
        "uv_bounds": [uv_low, uv_high],
        "uv_bounds_texels": bounds_texels,
        "coverage_texels_before_dilation": covered_before_dilation,
        "coverage_ratio_before_dilation": covered_before_dilation / float(size * size),
        "overlap_samples": overlaps,
        "overlap_texels": sum(overlap_pixels),
        "dilated_gutter_texels": sum(covered) - covered_before_dilation,
        "uncovered_in_uv_bounds_after_dilation": uncovered_in_bounds,
        "remaining_uncovered_texels": size * size - sum(covered),
    }
    return output, sum(covered), overlaps, stats


def main():
    parser = argparse.ArgumentParser(description="Bake an opt-in gX lightmap from a GLB")
    parser.add_argument("input", help="source GLB with TEXCOORD_1 lightmap UVs")
    parser.add_argument("output", help="output PNG path")
    parser.add_argument("--size", type=int, default=256, help="square output size (default: 256)")
    parser.add_argument("--uv-set", type=int, default=1, help="source UV set (default: 1; gX samples TEXCOORD_1)")
    parser.add_argument("--light-direction", default="0.4,0.8,0.3", help="direction from surface to light")
    parser.add_argument("--light-color", default="1,1,1", help="linear RGB light color")
    parser.add_argument("--intensity", type=float, default=0.8, help="directional light intensity")
    parser.add_argument("--ambient", type=float, default=0.15, help="unshadowed ambient multiplier")
    parser.add_argument("--bias", type=float, default=0.001, help="shadow-ray normal bias")
    parser.add_argument("--dilate", type=int, default=3, help="chart-gutter dilation pixels")
    parser.add_argument("--manifest", help="JSON light manifest; its bake settings override matching flags")
    parser.add_argument("--strict-uv", action="store_true",
                        help="fail on UV overlaps, degenerate triangles, or out-of-bounds UVs")
    args = parser.parse_args()
    try:
        settings = {
            "size": args.size,
            "ambient": max(0.0, args.ambient),
            "bias": max(1.0e-6, args.bias),
            "dilate": args.dilate,
            "lights": [{
                "type": "directional",
                "direction": parse_vector(args.light_direction, "--light-direction"),
                "color": parse_vector(args.light_color, "--light-color"),
                "intensity": args.intensity,
            }],
            "strict_uv": args.strict_uv,
        }
        if args.manifest:
            settings.update(parse_manifest(args.manifest))
            settings["strict_uv"] = settings["strict_uv"] or args.strict_uv
        if settings["size"] < 16 or settings["size"] > 4096:
            raise ValueError("size must be between 16 and 4096")
        document, binary = load_glb(args.input)
        triangles = collect_triangles(document, binary, args.uv_set)
        pixels, covered, _, stats = bake(
            triangles, settings["size"], ambient=settings["ambient"],
            bias=settings["bias"], dilation=settings["dilate"],
            lights=settings["lights"], diagnostics=True)
        if settings["strict_uv"] and (stats["degenerate_triangles"]
                                       or stats["uv_out_of_bounds_triangles"]
                                       or stats["overlap_texels"]):
            raise ValueError("strict UV diagnostics failed: %s" % json.dumps(stats, sort_keys=True))
        write_png(args.output, settings["size"], settings["size"], pixels)
    except (OSError, ValueError, KeyError, struct.error) as error:
        print("bake_lightmap: %s" % error, file=sys.stderr)
        return 2
    print("bake_lightmap: %s triangles, %dx%d, %d covered texels (%.1f%%), "
          "%d overlap texels, %d degenerate, %d out-of-bounds, %d lights, "
          "%d gutter texels, %d uncovered-in-bounds -> %s" %
          (len(triangles), settings["size"], settings["size"], covered,
           stats["coverage_ratio_before_dilation"] * 100.0,
           stats["overlap_texels"], stats["degenerate_triangles"],
           stats["uv_out_of_bounds_triangles"], len(settings["lights"]),
           stats["dilated_gutter_texels"],
           stats["uncovered_in_uv_bounds_after_dilation"], args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
