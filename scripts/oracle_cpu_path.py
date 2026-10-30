#!/usr/bin/env python3
#opencode: Paper 1 M2/M4 — small deterministic CPU reference path tracer.
#   It consumes the shipped primitive GLBs, builds a triangle BVH for the
#   reflectrow layout, and renders diffuse direct + environment transport.
#   The optional matte fixture removes metalness so bounce energy is visible;
#   exact material values remain available for later raster parity work.
#   This is an offline oracle fixture, not a replacement for the renderer.

import argparse
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PI = math.pi


def load_glb(path):
    data = open(path, "rb").read()
    _, _, total = struct.unpack_from("<4sII", data, 0)
    offset = 12
    json_chunk = None
    binary_chunk = None
    while offset < total:
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if kind == 0x4E4F534A:
            json_chunk = data[offset:offset + length]
        elif kind == 0x004E4942:
            binary_chunk = data[offset:offset + length]
        offset += length
    document = json.loads(json_chunk)
    component_types = {
        5121: (np.uint8, 1), 5123: (np.uint16, 2),
        5125: (np.uint32, 4), 5126: (np.float32, 4),
    }
    type_width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

    def accessor(index):
        desc = document["accessors"][index]
        view = document["bufferViews"][desc["bufferView"]]
        dtype, component_size = component_types[desc["componentType"]]
        width = type_width[desc["type"]]
        stride = view.get("byteStride", component_size * width)
        start = view.get("byteOffset", 0) + desc.get("byteOffset", 0)
        count = desc["count"]
        if stride == component_size * width:
            return np.frombuffer(
                binary_chunk, dtype=dtype, count=count * width, offset=start
            ).reshape(count, width).astype(np.float64)
        result = np.empty((count, width), dtype=np.float64)
        for row in range(count):
            begin = start + row * stride
            result[row] = np.frombuffer(
                binary_chunk, dtype=dtype, count=width, offset=begin
            )
        return result

    meshes = []
    for mesh in document.get("meshes", []):
        primitives = []
        for primitive in mesh["primitives"]:
            positions = accessor(primitive["attributes"]["POSITION"])
            normals = None
            if "NORMAL" in primitive["attributes"]:
                normals = accessor(primitive["attributes"]["NORMAL"])
            indices = accessor(primitive["indices"]).reshape(-1).astype(np.int64)
            primitives.append((positions, normals, indices))
        meshes.append(primitives)
    return document, meshes


def rotation_xyz(degrees):
    x, y, z = [math.radians(value) for value in degrees]
    cx, sx = math.cos(x), math.sin(x)
    cy, sy = math.cos(y), math.sin(y)
    cz, sz = math.cos(z), math.sin(z)
    return np.array([
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
        [-sy, cy * sx, cy * cx],
    ], dtype=np.float64)


def model_matrix(position, scale, rotation=(0.0, 0.0, 0.0)):
    matrix = np.eye(4, dtype=np.float64)
    #opencode: gX uploads its row-major S*R*T matrix with GL_TRUE. The
    #   effective GLSL column-major transform is therefore R^T*S.
    matrix[:3, :3] = rotation_xyz(rotation).T @ np.diag(scale)
    matrix[:3, 3] = position
    return matrix


def glb_triangles(document, meshes, matrix):
    triangles = []
    normals_out = []
    normal_matrix = np.linalg.inv(matrix[:3, :3]).T
    for positions, normals, indices in meshes[0]:
        points = np.concatenate(
            [positions, np.ones((len(positions), 1), dtype=np.float64)], axis=1
        )
        world = (matrix @ points.T).T[:, :3]
        triangles.append(world[indices].reshape(-1, 3, 3))
        if normals is None:
            face_normals = np.cross(world[indices][..., 1, :] - world[indices][..., 0, :],
                                    world[indices][..., 2, :] - world[indices][..., 0, :])
            face_normals /= np.maximum(np.linalg.norm(face_normals, axis=1, keepdims=True),
                                       1.0e-12)
            normals_out.append(np.repeat(face_normals[:, None, :], 3, axis=1))
        else:
            transformed = (normal_matrix @ normals.T).T
            transformed /= np.maximum(np.linalg.norm(transformed, axis=1, keepdims=True),
                                      1.0e-12)
            normals_out.append(transformed[indices].reshape(-1, 3, 3))
    return np.concatenate(triangles, axis=0), np.concatenate(normals_out, axis=0)


def glb_base_color(document):
    materials = document.get("materials", [])
    if not materials:
        #opencode: raylib supplies a white default material for the shipped
        #   sphere GLB, whose JSON has no material table.
        return np.ones(3, dtype=np.float64)
    pbr = materials[0].get("pbrMetallicRoughness", {})
    return np.asarray(pbr.get("baseColorFactor", (1.0, 1.0, 1.0, 1.0))[:3],
                      dtype=np.float64)


class TriangleBVH:
    def __init__(self, triangles, vertex_normals):
        self.triangles = triangles
        self.vertex_normals = vertex_normals
        self.edges_a = triangles[:, 1] - triangles[:, 0]
        self.edges_b = triangles[:, 2] - triangles[:, 0]
        self.normals = np.cross(self.edges_a, self.edges_b)
        lengths = np.linalg.norm(self.normals, axis=1)
        self.normals /= np.maximum(lengths[:, None], 1.0e-12)
        self.bounds_min = triangles.min(axis=1)
        self.bounds_max = triangles.max(axis=1)
        indices = np.arange(len(triangles), dtype=np.int64)
        self.nodes = []
        self._build(indices)

    def _build(self, indices):
        node_index = len(self.nodes)
        low = self.bounds_min[indices].min(axis=0)
        high = self.bounds_max[indices].max(axis=0)
        self.nodes.append((low, high, -1, -1, None))
        if len(indices) <= 8:
            self.nodes[node_index] = (low, high, -1, -1, indices)
            return node_index
        centers = (self.bounds_min[indices] + self.bounds_max[indices]) * 0.5
        axis = int(np.argmax(high - low))
        order = np.argsort(centers[:, axis])
        middle = len(indices) // 2
        left = self._build(indices[order[:middle]])
        right = self._build(indices[order[middle:]])
        self.nodes[node_index] = (low, high, left, right, None)
        return node_index

    @staticmethod
    def _box_hit(origin, direction, low, high, limit):
        near = -math.inf
        far = limit
        for axis in range(3):
            value = direction[axis]
            if abs(value) < 1.0e-12:
                if origin[axis] < low[axis] or origin[axis] > high[axis]:
                    return False
                continue
            inv = 1.0 / value
            first = (low[axis] - origin[axis]) * inv
            last = (high[axis] - origin[axis]) * inv
            if first > last:
                first, last = last, first
            near = max(near, first)
            far = min(far, last)
            if near > far:
                return False
        return far >= max(near, 0.0)

    def hit(self, origin, direction, limit=math.inf):
        best = limit
        triangle_index = -1
        best_u = 0.0
        best_v = 0.0
        stack = [0]
        while stack:
            node = self.nodes[stack.pop()]
            if not self._box_hit(origin, direction, node[0], node[1], best):
                continue
            if node[4] is not None:
                for index in node[4]:
                    edge_a = self.edges_a[index]
                    edge_b = self.edges_b[index]
                    pvec = np.cross(direction, edge_b)
                    determinant = float(np.dot(edge_a, pvec))
                    if abs(determinant) < 1.0e-10:
                        continue
                    inverse = 1.0 / determinant
                    tvec = origin - self.triangles[index, 0]
                    u = float(np.dot(tvec, pvec)) * inverse
                    if u < 0.0 or u > 1.0:
                        continue
                    qvec = np.cross(tvec, edge_a)
                    v = float(np.dot(direction, qvec)) * inverse
                    if v < 0.0 or u + v > 1.0:
                        continue
                    distance = float(np.dot(edge_b, qvec)) * inverse
                    if distance > 1.0e-5 and distance < best:
                        best = distance
                        triangle_index = int(index)
                        best_u = u
                        best_v = v
            else:
                stack.append(node[2])
                stack.append(node[3])
        if triangle_index < 0:
            return None
        barycentric = (1.0 - best_u - best_v, best_u, best_v)
        normal = (self.vertex_normals[triangle_index, 0] * barycentric[0]
                  + self.vertex_normals[triangle_index, 1] * barycentric[1]
                  + self.vertex_normals[triangle_index, 2] * barycentric[2])
        normal /= max(float(np.linalg.norm(normal)), 1.0e-12)
        if float(np.dot(normal, direction)) > 0.0:
            normal = -normal
        return best, triangle_index, normal


def make_scene(matte_fixture=False):
    sphere_doc, sphere_meshes = load_glb(
        os.path.join(ROOT, "resources/model/primitive/sphere.glb")
    )
    cube_doc, cube_meshes = load_glb(
        os.path.join(ROOT, "resources/model/primitive/cube.glb")
    )
    triangles = []
    materials = []

    def add(document, meshes, position, scale, rotation, albedo, metalness, roughness):
        geometry, normals = glb_triangles(document, meshes, model_matrix(position, scale, rotation))
        triangles.append(geometry)
        triangle_normals.append(normals)
        material_color = np.asarray(albedo, dtype=np.float64) * glb_base_color(document)
        materials.extend([(material_color, metalness, roughness)] * len(geometry))

    triangle_normals = []
    floor_metalness = 0.0 if matte_fixture else 1.0
    add(cube_doc, cube_meshes, (0.0, -0.075, 0.0), (14.0, 0.15, 8.0),
        (0.0, 0.0, 0.0), (0.9, 0.9, 0.9), floor_metalness, 0.02)
    balls = ((1.0, 0.02), (1.0, 0.15), (1.0, 0.40),
             (0.0, 0.05), (0.0, 0.80), (0.30, 0.08))
    for index, (metalness, roughness) in enumerate(balls):
        if matte_fixture:
            metalness = 0.0
        add(sphere_doc, sphere_meshes, (-5.0 + 2.0 * index, 0.42, 0.0),
            (0.7, 0.7, 0.7), (0.0, 0.0, 0.0), (0.9, 0.9, 0.9),
            metalness, roughness)
    add(cube_doc, cube_meshes, (3.1, 1.65, -3.4), (0.8, 0.8, 0.8),
        (22.0, 25.0, 8.0), (0.9, 0.9, 0.9), 0.0, 0.5)
    packed = np.concatenate(triangles, axis=0)
    packed_normals = np.concatenate(triangle_normals, axis=0)
    return TriangleBVH(packed, packed_normals), materials


def hash_float(pixel, sample, bounce, dimension):
    value = (pixel + 1) * 0x9E3779B1
    value ^= (sample + 17) * 0x85EBCA6B
    value ^= (bounce + 31) * 0xC2B2AE35
    value ^= (dimension + 7) * 0x27D4EB2D
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    return ((value & 0x00FFFFFF) + 0.5) / 16777216.0


def gpu_oracle_random(pixel_x, pixel_y, salt):
    #opencode: M4 — mirror gX_OracleHash in shadow.glsl for a fixed-frame
    #   GPU/CPU transport sample. The bounce fixture intentionally leaves the
    #   frame seed at zero so a repeat is bit-exact without temporal resolve.
    value = (pixel_x * 1973 + pixel_y * 9277 + salt * 40503 + 1) & 0xFFFFFFFF
    value = ((value ^ (value >> 16)) * 0x45D9F3B) & 0xFFFFFFFF
    value = ((value ^ (value >> 16)) * 0x45D9F3B) & 0xFFFFFFFF
    value ^= value >> 16
    return ((value & 0x00FFFFFF) + 0.5) / 16777216.0


def cosine_direction(normal, pixel, sample, bounce):
    u1 = hash_float(pixel, sample, bounce, 0)
    u2 = hash_float(pixel, sample, bounce, 1)
    radius = math.sqrt(u1)
    phi = 2.0 * PI * u2
    local = np.array((radius * math.cos(phi), radius * math.sin(phi),
                      math.sqrt(max(0.0, 1.0 - u1))), dtype=np.float64)
    helper = np.array((0.0, 1.0, 0.0))
    if abs(normal[1]) > 0.999:
        helper = np.array((1.0, 0.0, 0.0))
    tangent = np.cross(helper, normal)
    tangent /= max(np.linalg.norm(tangent), 1.0e-12)
    bitangent = np.cross(normal, tangent)
    result = tangent * local[0] + bitangent * local[1] + normal * local[2]
    return result / max(np.linalg.norm(result), 1.0e-12)


def gpu_cosine_direction(normal, pixel_x, pixel_y, bounce=0):
    salt = 17 + bounce * 2
    u1 = gpu_oracle_random(pixel_x, pixel_y, salt)
    u2 = gpu_oracle_random(pixel_x, pixel_y, salt + 1)
    radius = math.sqrt(u1)
    phi = 2.0 * PI * u2
    local = np.array((radius * math.cos(phi), radius * math.sin(phi),
                      math.sqrt(max(0.0, 1.0 - u1))), dtype=np.float64)
    helper = np.array((0.0, 1.0, 0.0))
    if abs(normal[1]) > 0.999:
        helper = np.array((1.0, 0.0, 0.0))
    tangent = np.cross(helper, normal)
    tangent /= max(np.linalg.norm(tangent), 1.0e-12)
    bitangent = np.cross(normal, tangent)
    result = tangent * local[0] + bitangent * local[1] + normal * local[2]
    return result / max(np.linalg.norm(result), 1.0e-12)


def environment(direction):
    height = max(0.0, min(1.0, direction[1] * 0.5 + 0.5))
    sky = np.array((0.025, 0.035, 0.060)) * (1.0 - height)
    sky += np.array((0.18, 0.24, 0.38)) * height
    sun = np.array((0.55, 0.38, 0.20))
    sun_direction = np.array((1.0, 2.0, 1.0))
    sun_direction /= np.linalg.norm(sun_direction)
    highlight = max(0.0, float(np.dot(direction, sun_direction))) ** 180.0
    return sky + sun * (5.0 * highlight)


LIGHTS = (
    (np.array((3.2, 2.6, 2.2)),
     np.array((1.0, 130.0 / 255.0, 90.0 / 255.0)) * 4.0),
    (np.array((-3.2, 2.2, -1.8)),
     np.array((110.0 / 255.0, 190.0 / 255.0, 1.0)) * 4.0),
)
#opencode: match gX_RendererGFX_Light_Init, which owns the final sun default
#   after Load_SetDefaults: sun.dir=(1,-1,1) is light travel direction, so
#   the surface-to-sun vector is (-1,1,-1).
SUN_DIRECTION = np.array((-1.0, 1.0, -1.0), dtype=np.float64)
SUN_DIRECTION /= np.linalg.norm(SUN_DIRECTION)


def direct_lighting(bvh, origin, normal, albedo, shadows=True):
    result = np.zeros(3, dtype=np.float64)
    diffuse = albedo / PI
    sun_cos = max(0.0, float(np.dot(normal, SUN_DIRECTION)))
    if sun_cos > 0.0 and (not shadows or
                          bvh.hit(origin + normal * 1.0e-4, SUN_DIRECTION) is None):
        result += diffuse * sun_cos * np.array((1.0, 1.0, 1.0))
    for position, radiance in LIGHTS:
        delta = position - origin
        distance = float(np.linalg.norm(delta))
        direction = delta / max(distance, 1.0e-12)
        light_cos = max(0.0, float(np.dot(normal, direction)))
        if light_cos <= 0.0:
            continue
        if not shadows or bvh.hit(origin + normal * 1.0e-4, direction,
                                  distance - 1.0e-4) is None:
            attenuation = 1.0 / (1.0 + 0.08 * distance * distance)
            result += diffuse * radiance * attenuation * light_cos
    return result


def distribution_ggx(normal, halfway, roughness):
    alpha = roughness * roughness
    alpha2 = alpha * alpha
    normal_halfway = max(float(np.dot(normal, halfway)), 0.0)
    denominator = normal_halfway * normal_halfway * (alpha2 - 1.0) + 1.0
    return min(alpha2 / (PI * denominator * denominator), 64.0)


def geometry_schlick_ggx(normal_direction, roughness):
    r = roughness + 1.0
    k = (r * r) / 8.0
    return normal_direction / (normal_direction * (1.0 - k) + k)


def fresnel_schlick(cosine, f0):
    return f0 + (1.0 - f0) * max(0.0, min(1.0, 1.0 - cosine)) ** 5.0


def pbr_direct_light(normal, view, light, albedo, metalness, roughness, radiance):
    normal_light = max(float(np.dot(normal, light)), 0.0)
    if normal_light <= 0.0:
        return np.zeros(3, dtype=np.float64)
    halfway = view + light
    halfway /= max(float(np.linalg.norm(halfway)), 1.0e-12)
    f0 = np.asarray((0.04, 0.04, 0.04), dtype=np.float64)
    f0 = f0 * (1.0 - metalness) + albedo * metalness
    fresnel = fresnel_schlick(max(float(np.dot(halfway, view)), 0.0), f0)
    distribution = distribution_ggx(normal, halfway, roughness)
    geometry = geometry_schlick_ggx(
        max(float(np.dot(normal, view)), 0.001), roughness)
    geometry *= geometry_schlick_ggx(
        max(float(np.dot(normal, light)), 0.001), roughness)
    specular = (distribution * geometry * fresnel) / max(
        4.0 * max(float(np.dot(normal, view)), 0.001) * normal_light, 0.001)
    diffuse = (1.0 - fresnel) * (1.0 - metalness) * albedo / PI
    return (diffuse + specular) * radiance * normal_light


def direct_ggx_lighting(bvh, origin, normal, view, albedo, metalness,
                        roughness, shadows=True):
    result = np.zeros(3, dtype=np.float64)
    sun_light = SUN_DIRECTION
    if not shadows or bvh.hit(origin + normal * 1.0e-4, sun_light) is None:
        result += pbr_direct_light(
            normal, view, sun_light, albedo, metalness, roughness,
            np.ones(3, dtype=np.float64))
    for position, radiance in LIGHTS:
        delta = position - origin
        distance = float(np.linalg.norm(delta))
        light = delta / max(distance, 1.0e-12)
        if not shadows or bvh.hit(origin + normal * 1.0e-4, light,
                                  distance - 1.0e-4) is None:
            attenuation = 1.0 / (1.0 + 0.08 * distance * distance)
            result += pbr_direct_light(
                normal, view, light, albedo, metalness, roughness,
                radiance * attenuation)
    return result


def camera_ray(x, y, width, height):
    #opencode: match gX_Showcase_Update's pinned --reflectrow view.
    target = np.array((0.0, 0.35, 0.0), dtype=np.float64)
    yaw = math.radians(-35.0)
    pitch = math.radians(16.0)
    distance = 11.0
    camera = target + distance * np.array((math.cos(pitch) * math.sin(yaw),
                                           math.sin(pitch),
                                           math.cos(pitch) * math.cos(yaw)))
    forward = target - camera
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array((0.0, 1.0, 0.0)))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    aspect = width / float(height)
    fov = math.tan(math.radians(45.0) * 0.5)
    sx = ((x + 0.5) / width * 2.0 - 1.0) * aspect * fov
    sy = (1.0 - (y + 0.5) / height * 2.0) * fov
    direction = forward + right * sx + up * sy
    direction /= np.linalg.norm(direction)
    return camera, direction


def render(bvh, materials, width, height, samples, bounces, shadows=True):
    image = np.zeros((height, width, 3), dtype=np.float64)
    for y in range(height):
        for x in range(width):
            pixel = y * width + x
            total = np.zeros(3, dtype=np.float64)
            for sample in range(samples):
                origin, direction = camera_ray(x, y, width, height)
                throughput = np.ones(3, dtype=np.float64)
                radiance = np.zeros(3, dtype=np.float64)
                for bounce in range(bounces + 1):
                    hit = bvh.hit(origin, direction)
                    if hit is None:
                        radiance += throughput * environment(direction)
                        break
                    distance, triangle, normal = hit
                    origin = origin + direction * distance
                    albedo, metalness, _ = materials[triangle]
                    diffuse = albedo * (1.0 - metalness)
                    radiance += throughput * direct_lighting(
                        bvh, origin, normal, diffuse, shadows=shadows)
                    if bounce >= bounces or np.max(throughput * diffuse) < 1.0e-4:
                        break
                    throughput *= diffuse
                    direction = cosine_direction(normal, pixel, sample, bounce)
                    origin += normal * 1.0e-4
                total += radiance
            image[y, x] = total / float(samples)
    return image


def render_environment_bounce(bvh, materials, width, height, shadows=False):
    #opencode: M4 one-secondary environment path. This is the first GPU path
    #   slice: primary direct sun/point transport plus one cosine-sampled
    #   secondary miss. Surface-to-surface GPU rays remain a later milestone.
    image = np.zeros((height, width, 3), dtype=np.float64)
    for y in range(height):
        for x in range(width):
            origin, direction = camera_ray(x, y, width, height)
            hit = bvh.hit(origin, direction)
            if hit is None:
                continue
            distance, triangle, normal = hit
            point = origin + direction * distance
            albedo, metalness, _ = materials[triangle]
            diffuse = albedo * (1.0 - metalness)
            image[y, x] = direct_lighting(
                bvh, point, normal, diffuse, shadows=shadows)
            #opencode: gl_FragCoord.y is bottom-up while this reference image
            #   is top-down, so mirror the captured framebuffer row before
            #   reproducing the GPU's deterministic pixel hash.
            secondary = gpu_cosine_direction(normal, x, height - 1 - y)
            image[y, x] += diffuse * environment(secondary)
    return image


def render_gpu_surface_bounce(bvh, materials, width, height, shadows=False):
    return render_gpu_surface_bounces(bvh, materials, width, height, 1, shadows)


def render_gpu_surface_bounces(bvh, materials, width, height, bounces, shadows=False):
    #opencode: M5/M6 — CPU contract for the opt-in GPU surface transport path.
    image = np.zeros((height, width, 3), dtype=np.float64)
    for y in range(height):
        for x in range(width):
            origin, direction = camera_ray(x, y, width, height)
            hit = bvh.hit(origin, direction)
            if hit is None:
                continue
            distance, triangle, normal = hit
            point = origin + direction * distance
            albedo, metalness, _ = materials[triangle]
            diffuse = albedo * (1.0 - metalness)
            image[y, x] = direct_lighting(
                bvh, point, normal, diffuse, shadows=shadows)
            throughput = diffuse.copy()
            secondary = gpu_cosine_direction(normal, x, height - 1 - y)
            secondary_origin = point + normal * 1.0e-4
            for bounce in range(bounces):
                secondary_hit = bvh.hit(secondary_origin, secondary)
                if secondary_hit is None:
                    image[y, x] += throughput * environment(secondary)
                    break
                secondary_distance, secondary_triangle, secondary_normal = secondary_hit
                secondary_point = secondary_origin + secondary * secondary_distance
                secondary_albedo, secondary_metalness, _ = materials[secondary_triangle]
                secondary_diffuse = secondary_albedo * (1.0 - secondary_metalness)
                image[y, x] += throughput * direct_lighting(
                    bvh, secondary_point, secondary_normal, secondary_diffuse,
                    shadows=shadows)
                if bounce + 1 >= bounces:
                    break
                throughput *= secondary_diffuse
                secondary = gpu_cosine_direction(
                    secondary_normal, x, height - 1 - y, bounce + 1)
                secondary_origin = secondary_point + secondary_normal * 1.0e-4
    return image


def render_gpu_reflection(bvh, materials, width, height, shadows=False):
    #opencode: Paper 1 M8 — CPU contract for the opt-in perfect-specular
    #   reflection transport. The raster fixture supplies primary diffuse
    #   light; this function supplies only the reflected secondary path.
    image = np.zeros((height, width, 3), dtype=np.float64)
    for y in range(height):
        for x in range(width):
            origin, direction = camera_ray(x, y, width, height)
            hit = bvh.hit(origin, direction)
            if hit is None:
                continue
            distance, triangle, normal = hit
            point = origin + direction * distance
            albedo, metalness, _ = materials[triangle]
            image[y, x] = direct_lighting(
                bvh, point, normal, albedo, shadows=shadows)
            view = -direction
            f0 = np.full(3, 0.04, dtype=np.float64)
            f0 = f0 * (1.0 - metalness) + albedo * metalness
            throughput = fresnel_schlick(
                max(float(np.dot(normal, view)), 0.0), f0)
            reflected = direction - 2.0 * float(np.dot(direction, normal)) * normal
            reflected /= max(float(np.linalg.norm(reflected)), 1.0e-12)
            secondary = bvh.hit(point + normal * 1.0e-4, reflected)
            if secondary is None:
                image[y, x] += throughput * environment(reflected)
                continue
            secondary_distance, secondary_triangle, secondary_normal = secondary
            secondary_point = point + normal * 1.0e-4 + reflected * secondary_distance
            secondary_albedo, secondary_metalness, secondary_roughness = materials[secondary_triangle]
            image[y, x] += throughput * direct_ggx_lighting(
                bvh, secondary_point, secondary_normal, -reflected,
                secondary_albedo, secondary_metalness, secondary_roughness,
                shadows=shadows)
    return image


def render_ggx(bvh, materials, width, height, shadows=False):
    #opencode: M3 direct-only Cook-Torrance reference. The raster fixture
    #   disables IBL, shadows, and post effects, so this row isolates the
    #   same GGX direct-light contract for roughness/metalness parity.
    image = np.zeros((height, width, 3), dtype=np.float64)
    for y in range(height):
        for x in range(width):
            origin, direction = camera_ray(x, y, width, height)
            hit = bvh.hit(origin, direction)
            if hit is None:
                continue
            distance, triangle, normal = hit
            point = origin + direction * distance
            albedo, metalness, roughness = materials[triangle]
            image[y, x] = direct_ggx_lighting(
                bvh, point, normal, -direction, albedo, metalness,
                roughness, shadows=shadows)
    return image


def save_image(path, image):
    mapped = image / (1.0 + image)
    mapped = np.power(np.clip(mapped, 0.0, 1.0), 1.0 / 2.2)
    Image.fromarray(np.asarray(mapped * 255.0, dtype=np.uint8), "RGB").save(path)


def main():
    parser = argparse.ArgumentParser(description="Render the CPU path oracle")
    parser.add_argument("--width", type=int, default=48)
    parser.add_argument("--height", type=int, default=32)
    parser.add_argument("--samples", type=int, default=16)
    parser.add_argument("--bounces", type=int, default=2)
    parser.add_argument("--matte-fixture", action="store_true")
    parser.add_argument("--transport", choices=("diffuse", "ggx", "bounce", "reflection"), default="diffuse")
    parser.add_argument("--output", default="/tmp/opencode/oracle_cpu")
    args = parser.parse_args()
    if min(args.width, args.height, args.samples) < 1 or args.bounces < 0:
        parser.error("width, height, samples must be positive and bounces non-negative")
    os.makedirs(args.output, exist_ok=True)
    bvh, materials = make_scene(args.matte_fixture)
    if args.transport == "ggx":
        image = render_ggx(bvh, materials, args.width, args.height)
    elif args.transport == "bounce":
        image = render_environment_bounce(bvh, materials, args.width, args.height)
    elif args.transport == "reflection":
        image = render_gpu_reflection(bvh, materials, args.width, args.height)
    else:
        image = render(bvh, materials, args.width, args.height, args.samples, args.bounces)
    if not np.isfinite(image).all():
        raise RuntimeError("CPU oracle produced non-finite radiance")
    np.savez_compressed(os.path.join(args.output, "oracle.npz"), image=image,
                        bounces=args.bounces, samples=args.samples)
    save_image(os.path.join(args.output, "oracle.png"), image)
    print("CPU oracle: %dx%d transport=%s samples=%d bounces=%d mean=%.6f max=%.6f" %
          (args.width, args.height, args.transport, args.samples, args.bounces,
           image.mean(), image.max()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
