#!/usr/bin/env python3
#opencode: offline contract check for the opt-in static-lightmap slice.
"""Validate the shader/model contract and a deterministic baker smoke case."""

import os
import json
import struct
import sys
import tempfile


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from bake_lightmap import (  # noqa: E402
    bake,
    collect_triangles,
    load_glb,
    parse_manifest,
    transform_direction,
    inverse_transpose_3x3,
)


def require_text(path, needle):
    with open(os.path.join(ROOT, path), "r", encoding="utf-8") as stream:
        content = stream.read()
    if needle not in content:
        raise AssertionError("%s is missing %r" % (path, needle))


def make_fixture_glb():
    #opencode: tiny indexed-free GLB exercises the same TEXCOORD_1 parser path
    #   used by the offline baker without adding a generated binary asset.
    binary = b"".join((
        struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
        struct.pack("<9f", 0.0, -1.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, 0.0),
        struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0),
    ))
    views = [(0, 36), (36, 36), (72, 24)]
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offset, "byteLength": length}
            for offset, length in views
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
        ],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_1": 2},
        }]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    chunks += struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def main():
    #opencode: the vertex/shader contract must remain paired with the C fields.
    require_text("resources/shader/standard.vert", "vertexTexCoord2")
    require_text("resources/shader/standard.vert", "fragTexCoord2")
    require_text("resources/shader/standard.frag", "uniform sampler2D lightmapMap")
    require_text("resources/shader/standard.frag", "gX_LightmapContribution")
    require_text("src/renderer/gfx/model.h", "lightmapID")
    require_text("src/renderer/gfx/schema.c", '"hasLightmap"')

    #opencode: parser + raster unit case catches TEXCOORD_1 extraction, UV
    #   rasterization, output packing, and the positive-light path without a
    #   display or a generated repository asset.
    with tempfile.NamedTemporaryFile(suffix=".glb") as fixture:
        fixture.write(make_fixture_glb())
        fixture.flush()
        document, binary = load_glb(fixture.name)
        triangles = collect_triangles(document, binary, 1)
    pixels, covered, overlaps = bake(
        triangles, 32, (0.0, -1.0, 0.0), (1.0, 1.0, 1.0),
        0.8, 0.15, 0.001, 2)
    if covered == 0 or max(pixels) == 0:
        raise AssertionError("baker produced no lit texels")
    if overlaps != 0:
        raise AssertionError("unit chart unexpectedly overlapped itself")

    #opencode: non-uniform-transform normal check prevents a future inverse
    #   versus inverse-transpose regression in the offline baker.
    matrix = (0.0, -2.0, 0.0, 0.0,
              1.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 3.0, 0.0,
              0.0, 0.0, 0.0, 1.0)
    normal_matrix = inverse_transpose_3x3(matrix)
    transformed = transform_direction(normal_matrix, (0.0, 1.0, 0.0))
    if abs(transformed[0] + 0.5) > 1.0e-6 or abs(transformed[1]) > 1.0e-6:
        raise AssertionError("normal inverse-transpose check failed: %r" % (transformed,))

    #opencode: the checked-in opt-in fixture closes the parser-to-preset
    #   contract: real secondary UVs, a declared PNG, and the matching heap ID.
    fixture_model = os.path.join(ROOT, "resources/model/primitive/lightmap_fixture.glb")
    fixture_document, fixture_binary = load_glb(fixture_model)
    fixture_triangles = collect_triangles(fixture_document, fixture_binary, 1)
    if len(fixture_triangles) != 12:
        raise AssertionError("fixture triangle count changed: %d" % len(fixture_triangles))
    fixture_manifest_path = os.path.join(ROOT, "resources/tools/lightmap_fixture_bake.json")
    fixture_manifest = parse_manifest(fixture_manifest_path)
    if len(fixture_manifest["lights"]) != 2:
        raise AssertionError("fixture manifest must contain directional and point lights")
    if {light["type"] for light in fixture_manifest["lights"]} != {"directional", "point"}:
        raise AssertionError("fixture manifest light types changed")
    fixture_pixels, fixture_covered, _, fixture_stats = bake(
        fixture_triangles, 32, ambient=fixture_manifest["ambient"],
        bias=fixture_manifest["bias"], dilation=1,
        lights=fixture_manifest["lights"], diagnostics=True)
    if fixture_covered == 0 or max(fixture_pixels) == 0:
        raise AssertionError("fixture bake produced no lit texels")
    for field in ("degenerate_triangles", "uv_out_of_bounds_triangles", "overlap_texels"):
        if fixture_stats[field] != 0:
            raise AssertionError("fixture UV diagnostic %s changed: %s" % (field, fixture_stats[field]))
    if fixture_stats["dilated_gutter_texels"] == 0:
        raise AssertionError("fixture dilation did not cover any chart gutter texels")

    #opencode: duplicate charts must be reported as overlap diagnostics rather
    #   than silently becoming a last-writer-wins lightmap.
    _, _, _, overlap_stats = bake(
        fixture_triangles[:1] + fixture_triangles[:1], 32,
        ambient=fixture_manifest["ambient"], bias=fixture_manifest["bias"],
        dilation=0, lights=fixture_manifest["lights"], diagnostics=True)
    if overlap_stats["overlap_texels"] == 0:
        raise AssertionError("overlapping UV charts were not diagnosed")
    fixture_png = os.path.join(ROOT, "resources/texture/lightmap_fixture.png")
    with open(fixture_png, "rb") as stream:
        png_header = stream.read(26)
    if png_header[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("fixture lightmap is not a PNG")
    width, height = struct.unpack(">II", png_header[16:24])
    if (width, height) != (128, 128):
        raise AssertionError("fixture lightmap dimensions changed: %dx%d" % (width, height))
    if png_header[24:26] != b"\x08\x06":
        raise AssertionError("fixture lightmap is not RGBA8")
    with open(os.path.join(ROOT, "resources/tools/gfx_lightmap.json"), encoding="utf-8") as stream:
        fixture_preset = json.load(stream)
    fixture_params = fixture_preset["models"][0]["params"]
    if not fixture_params.get("hasLightmap") or fixture_params.get("lightmapID") != 0:
        raise AssertionError("fixture preset does not enable texture-heap lightmap 0")
    if fixture_preset["textures"][0]["path"] != "resources/texture/lightmap_fixture.png":
        raise AssertionError("fixture preset lightmap path changed")

    #opencode: shipped primitives intentionally have no secondary UV yet; the
    #   baker must reject them instead of silently baking primary UVs.
    sphere = os.path.join(ROOT, "resources/model/primitive/sphere.glb")
    document, binary = load_glb(sphere)
    try:
        collect_triangles(document, binary, 1)
    except ValueError as error:
        if "TEXCOORD_1" not in str(error):
            raise AssertionError("unexpected missing-UV diagnostic: %s" % error)
    else:
        raise AssertionError("baker accepted a shipped model without TEXCOORD_1")

    print("LIGHTMAP-CHECK: PASS (contract, raster smoke, transform, missing-UV rejection)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
