#!/usr/bin/env python3
#opencode: scene-export gate. GX_GFX_EXPORT_CHECK exports every loaded model
#   as OBJ + MTL (render-side material values) and one GLB (all meshes as
#   primitives, world transform baked, override maps embedded) into
#   GX_GFX_EXPORT_DIR; the gate re-reads the fixture transforms, pins the
#   baked placements, the OBJ -> MTL linkage, and decodes every GLB
#   (chunk/JSON/accessor structure, decoded AABB vs accessor min/max, index
#   bounds, material refs) plus the embedded-map pass on gfx_materials.json.

import json
import math
import re
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
MATERIALS_PRESET = ROOT / "resources" / "tools" / "gfx_materials.json"
HOOK_RE = re.compile(
    r"GFX_EXPORT_CHECK: (PASS|FAIL) files=(\d+) mtls=(\d+) glbs=(\d+)")
MESH_RE = re.compile(
    r"GfxEd: export mesh model=(\d+) mesh=(\d+) verts=(\d+) tris=(\d+) "
    r"file=(.+)")
MTL_RE = re.compile(r"GfxEd: export mtl model=(\d+) mesh=(\d+) file=(.+)")
GLB_RE = re.compile(r"GfxEd: export glb model=(\d+) file=(.+)")
#opencode: cube.glb is authored at +-1; the fixture only rotates about Y, so
#   the baked AABB is exact: spread = |cos| + |sin| per scaled x/z axis.
CUBE_LOCAL_HALF = 1.0
GLB_MAGIC = 0x46546C67
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942
PNG_SIG = b"\x89PNG\r\n\x1a\n"


def expected_half_extents(params):
    scale = params.get("scale", [1.0, 1.0, 1.0])
    rotation = params.get("rotation", [0.0, 0.0, 0.0])
    if abs(rotation[0]) > 1e-6 or abs(rotation[2]) > 1e-6:
        raise RuntimeError(f"gate supports Y-only rotations: {rotation}")
    theta = math.radians(rotation[1])
    spread = abs(math.cos(theta)) + abs(math.sin(theta))
    return np.asarray([
        CUBE_LOCAL_HALF * scale[0] * spread,
        CUBE_LOCAL_HALF * scale[1],
        CUBE_LOCAL_HALF * scale[2] * spread,
    ], dtype=np.float64)


def parse_obj(path):
    verts = []
    faces = 0
    for line in Path(path).read_text().splitlines():
        if line.startswith("v "):
            verts.append([float(value) for value in line.split()[1:4]])
        elif line.startswith("f "):
            faces += 1
            for token in line.split()[1:]:
                index = int(token.split("/")[0])
                if index < 1 or index > len(verts):
                    raise RuntimeError(
                        f"{Path(path).name}: face index {index} out of range")
    return np.asarray(verts, dtype=np.float64), faces


def parse_mtl(path):
    values = {}
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key == "newmtl":
            values[key] = " ".join(parts[1:])
        elif key in ("Kd", "Ke", "Pr", "Pm", "Ns", "d"):
            values[key] = [float(value) for value in parts[1:]]
        elif key in ("map_Kd", "map_Ke"):
            values[key] = " ".join(parts[1:])
    return values


def parse_glb(path):
    data = Path(path).read_bytes()
    if len(data) < 20:
        raise RuntimeError(f"{path.name}: truncated GLB")
    magic, version, length = struct.unpack("<III", data[:12])
    if magic != GLB_MAGIC or version != 2 or length != len(data):
        raise RuntimeError(
            f"{path.name}: bad header {hex(magic)}/v{version}/{length}")
    offset = 12
    chunks = []
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack("<II", data[offset:offset + 8])
        if offset + 8 + chunk_len > len(data):
            raise RuntimeError(f"{path.name}: chunk overruns the file")
        chunks.append((chunk_type, offset + 8, chunk_len))
        offset += 8 + chunk_len
    if len(chunks) < 2 or chunks[0][0] != GLB_JSON or chunks[1][0] != GLB_BIN:
        raise RuntimeError(f"{path.name}: missing JSON/BIN chunks")
    document = json.loads(data[chunks[0][1]:chunks[0][1] + chunks[0][2]])
    return document, data, chunks[1][1]


def accessor_array(document, data, bin_offset, index, dtype):
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    start = bin_offset + view["byteOffset"] + accessor.get("byteOffset", 0)
    count = accessor["count"] * components
    values = np.frombuffer(data, dtype=dtype, count=count, offset=start)
    return values.reshape(accessor["count"], components)


def check_glb(path):
    document, data, bin_offset = parse_glb(path)
    if document.get("asset", {}).get("version") != "2.0":
        raise RuntimeError(f"{path.name}: asset version missing")
    nodes = document.get("nodes", [])
    meshes = document.get("meshes", [])
    materials = document.get("materials", [])
    if not nodes or not meshes or not materials:
        raise RuntimeError(f"{path.name}: empty scene")
    for mesh in meshes:
        primitives = mesh.get("primitives", [])
        if not primitives:
            raise RuntimeError(f"{path.name}: mesh without primitives")
        for primitive in primitives:
            attributes = primitive.get("attributes", {})
            if "POSITION" not in attributes:
                raise RuntimeError(f"{path.name}: primitive without POSITION")
            positions = accessor_array(document, data, bin_offset,
                                       attributes["POSITION"], "<f4")
            if not np.isfinite(positions).all():
                raise RuntimeError(f"{path.name}: non-finite positions")
            accessor = document["accessors"][attributes["POSITION"]]
            low = positions.min(axis=0)
            high = positions.max(axis=0)
            if np.abs(np.asarray(accessor["min"]) - low).max() > 1e-3 \
                    or np.abs(np.asarray(accessor["max"]) - high).max() > 1e-3:
                raise RuntimeError(f"{path.name}: accessor min/max mismatch")
            if "indices" in primitive:
                indices = accessor_array(document, data, bin_offset,
                                         primitive["indices"], "<u2").ravel()
                if indices.size % 3 != 0:
                    raise RuntimeError(f"{path.name}: index count % 3 != 0")
                if indices.max(initial=0) >= positions.shape[0]:
                    raise RuntimeError(f"{path.name}: index out of range")
            material = primitive.get("material")
            if material is None or material >= len(materials):
                raise RuntimeError(f"{path.name}: primitive material invalid")
    for material in materials:
        factor = material.get("pbrMetallicRoughness", {}).get("baseColorFactor")
        if not isinstance(factor, list) or len(factor) != 4 \
                or not all(0.0 <= value <= 1.0 for value in factor):
            raise RuntimeError(f"{path.name}: bad baseColorFactor {factor}")
    return document, data, bin_offset


def run_export(preset, folder):
    folder.mkdir(parents=True, exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "40",
        "GX_GFX_PRESET_LOAD": str(preset),
        "GX_GFX_EXPORT_CHECK": "1",
        "GX_GFX_EXPORT_DIR": str(folder),
    }
    environment = audit.scene_env(overrides, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None,
            height=None, fps=None, frames=40, shot_frame=None,
            screenshot_dir=None, timeout=120, check=True,
            extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    hook = HOOK_RE.findall(log)
    if not hook or hook[-1][0] != "PASS":
        raise RuntimeError(f"export hook did not pass: {hook}")
    return (int(hook[-1][1]), int(hook[-1][2]), int(hook[-1][3]), log)


def main():
    if not BIN.is_file() or not PRESET.is_file() or not MATERIALS_PRESET.is_file():
        print("GFX_EXPORT_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_export_",
                                         dir="/tmp/opencode") as temporary:
            base = Path(temporary)
            files_expected, mtls_expected, glbs_expected, log = run_export(
                PRESET, base / "exports")
            if files_expected <= 0 or mtls_expected != files_expected \
                    or glbs_expected <= 0:
                raise RuntimeError(
                    f"hook counts: files={files_expected} "
                    f"mtls={mtls_expected} glbs={glbs_expected}")
            traces = MESH_RE.findall(log)
            if len(traces) != files_expected:
                raise RuntimeError(
                    f"trace/file mismatch: {len(traces)} vs {files_expected}")
            if len(MTL_RE.findall(log)) != mtls_expected:
                raise RuntimeError("MTL trace count mismatch")
            if len(GLB_RE.findall(log)) != glbs_expected:
                raise RuntimeError("GLB trace count mismatch")
            preset = json.loads(PRESET.read_text())
            models = preset.get("models", [])
            cubes = 0
            glb_cubes = 0
            worst_center = 0.0
            worst_half = 0.0
            worst_glb = 0.0
            phong = 0
            pbr = 0
            for model_index, mesh_index, verts, tris, file_path in traces:
                path = Path(file_path)
                if not path.is_file():
                    raise RuntimeError(f"missing export {path}")
                data, faces = parse_obj(path)
                if data.shape[0] != int(verts):
                    raise RuntimeError(
                        f"{path.name}: {data.shape[0]} verts, expected {verts}")
                if faces != int(tris):
                    raise RuntimeError(
                        f"{path.name}: {faces} faces, expected {tris}")
                if not np.isfinite(data).all():
                    raise RuntimeError(f"{path.name}: non-finite vertices")
                mtl_path = path.with_suffix(".mtl")
                if not mtl_path.is_file():
                    raise RuntimeError(f"{path.name}: missing MTL sidecar")
                mtl = parse_mtl(mtl_path)
                base_name = path.stem
                if not path.read_text().startswith(
                        f"mtllib {mtl_path.name}\nusemtl {base_name}\n"):
                    raise RuntimeError(f"{path.name}: missing mtllib/usemtl")
                if mtl.get("newmtl") != base_name:
                    raise RuntimeError(
                        f"{mtl_path.name}: newmtl {mtl.get('newmtl')}")
                for key in ("Kd", "Ke"):
                    value = mtl.get(key)
                    if not isinstance(value, list) or len(value) != 3 \
                            or not all(0.0 <= component <= 1.0
                                       for component in value):
                        raise RuntimeError(f"{mtl_path.name}: bad {key} {value}")
                if "Ns" in mtl:
                    phong += 1
                elif "Pr" in mtl and "Pm" in mtl:
                    pbr += 1
                else:
                    raise RuntimeError(f"{mtl_path.name}: no Ns or Pr/Pm")
                model_index = int(model_index)
                entry = models[model_index] if model_index < len(models) else {}
                if not str(entry.get("path", "")).endswith("primitive/cube.glb"):
                    continue
                params = entry.get("params", {})
                pos = np.asarray(params.get("pos", [0.0, 0.0, 0.0]),
                                 dtype=np.float64)
                center = (data.min(axis=0) + data.max(axis=0)) / 2.0
                center_delta = float(np.abs(center - pos).max())
                if center_delta > 0.01:
                    raise RuntimeError(
                        f"{path.name}: center {center} vs preset pos {pos} "
                        f"(delta {center_delta:.3f})")
                half = (data.max(axis=0) - data.min(axis=0)) / 2.0
                expected = expected_half_extents(params)
                half_delta = float(np.abs(half - expected).max())
                if half_delta > 0.01:
                    raise RuntimeError(
                        f"{path.name}: half extents {half} vs expected "
                        f"{expected} (delta {half_delta:.3f})")
                cubes += 1
                worst_center = max(worst_center, center_delta)
                worst_half = max(worst_half, half_delta)
            if cubes < 1:
                raise RuntimeError("fixture cube exports not found")
            if phong == 0 or pbr == 0:
                raise RuntimeError(
                    f"fixture lost a shading mode (phong={phong} pbr={pbr})")

            #opencode: decode every GLB; cube models must carry the same baked
            #   transform as their OBJ siblings.
            for glb_path in sorted((base / "exports").glob("*.glb")):
                match = re.search(r"_(\d+)_", glb_path.name)
                if not match:
                    raise RuntimeError(f"{glb_path.name}: no model index")
                model_index = int(match.group(1))
                document, glb_data, bin_offset = check_glb(glb_path)
                entry = models[model_index] if model_index < len(models) else {}
                if not str(entry.get("path", "")).endswith("primitive/cube.glb"):
                    continue
                positions = []
                for mesh in document["meshes"]:
                    for primitive in mesh["primitives"]:
                        positions.append(accessor_array(document, glb_data,
                            bin_offset, primitive["attributes"]["POSITION"],
                            "<f4"))
                all_positions = np.concatenate(positions)
                params = entry.get("params", {})
                pos = np.asarray(params.get("pos", [0.0, 0.0, 0.0]),
                                 dtype=np.float64)
                center = (all_positions.min(axis=0)
                          + all_positions.max(axis=0)) / 2.0
                center_delta = float(np.abs(center - pos).max())
                half = (all_positions.max(axis=0)
                        - all_positions.min(axis=0)) / 2.0
                expected = expected_half_extents(params)
                half_delta = float(np.abs(half - expected).max())
                if center_delta > 0.01 or half_delta > 0.01:
                    raise RuntimeError(
                        f"{glb_path.name}: baked AABB center delta "
                        f"{center_delta:.3f}, extents delta {half_delta:.3f}")
                glb_cubes += 1
                worst_glb = max(worst_glb, center_delta, half_delta)
            if glb_cubes < 1:
                raise RuntimeError("fixture cube GLBs not found")

            #opencode: second pass — override maps embedded in the GLB and
            #   referenced by the material textures; the emissive override
            #   (white x glow 2.0) clamps the factor to 1 with the strength
            #   extension carrying the glow.
            files2, mtls2, glbs2, _ = run_export(MATERIALS_PRESET,
                base / "materials")
            if files2 <= 0 or mtls2 != files2 or glbs2 <= 0:
                raise RuntimeError(f"materials hook counts: {files2}/{mtls2}/{glbs2}")
            embedded = 0
            override_found = 0
            for glb_path in sorted((base / "materials").glob("*.glb")):
                document, glb_data, bin_offset = check_glb(glb_path)
                images = document.get("images", [])
                textures = document.get("textures", [])
                for image in images:
                    if image.get("mimeType") != "image/png":
                        raise RuntimeError(
                            f"{glb_path.name}: image mime {image.get('mimeType')}")
                    view = document["bufferViews"][image["bufferView"]]
                    start = bin_offset + view["byteOffset"]
                    if glb_data[start:start + 8] != PNG_SIG:
                        raise RuntimeError(f"{glb_path.name}: bad PNG payload")
                    embedded += 1
                for texture in textures:
                    if texture.get("source", -1) >= len(images):
                        raise RuntimeError(f"{glb_path.name}: bad texture source")
                for material in document.get("materials", []):
                    pbr_factors = material.get("pbrMetallicRoughness", {})
                    if "baseColorTexture" not in pbr_factors \
                            or "emissiveTexture" not in material:
                        continue
                    if np.abs(np.asarray(material["emissiveFactor"]) - 1.0).max() > 1e-3:
                        raise RuntimeError(
                            f"{glb_path.name}: emissiveFactor "
                            f"{material['emissiveFactor']} != 1.0")
                    strength = material.get("extensions", {}).get(
                        "KHR_materials_emissive_strength", {}).get(
                        "emissiveStrength", 0.0)
                    if abs(strength - 2.0) > 1e-3:
                        raise RuntimeError(
                            f"{glb_path.name}: emissiveStrength {strength} != 2.0")
                    override_found += 1
            if embedded < 2:
                raise RuntimeError(f"GLB images not embedded (found {embedded})")
            if override_found != 1:
                raise RuntimeError(
                    f"override material not found (found {override_found})")
    except (OSError, RuntimeError, ValueError, KeyError, struct.error) as error:
        print(f"GFX_EXPORT_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_EXPORT_CHECK: PASS {files_expected} OBJ + {mtls_expected} MTL "
          f"+ {glbs_expected} GLB files, counts exact, {cubes} cube placements "
          f"baked (center {worst_center:.4f}, extents {worst_half:.4f}), "
          f"GLB cubes {glb_cubes} (delta {worst_glb:.4f}), phong {phong}/pbr "
          f"{pbr}, {embedded} embedded maps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
