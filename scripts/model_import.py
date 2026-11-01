#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: offline COLLADA model importer. gX is raylib-only, so .dae files are
#   converted to GLB with assimp outside the engine; external textures are
#   copied beside the output and the result is validated before it reaches
#   the asset browser.

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import urllib.parse


PROJECT = Path(__file__).resolve().parent.parent
CONSTANTS = PROJECT / "src/common/types/3d.h"
GLB_MAGIC = 0x46546C67
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942
FORMATS = ("glb2", "glb")
DEFAULT_FORMAT = "glb2"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def skin_bone_cap():
    try:
        text = CONSTANTS.read_text(encoding="utf-8")
    except OSError:
        return 128
    match = re.search(r"#define\s+gX_SKIN_MAX_BONES\s+(\d+)", text)
    return int(match.group(1)) if match else 128


def find_assimp(explicit=None):
    candidate = explicit or os.environ.get("ASSIMP") or shutil.which("assimp")
    if not candidate:
        raise RuntimeError(
            "assimp was not found; install assimp-utils or pass --assimp PATH")
    if os.sep in candidate and not Path(candidate).is_file():
        raise RuntimeError(f"assimp executable not found: {candidate}")
    return candidate


def assimp_command(assimp, source, output, model_format):
    return [assimp, "export", str(source), str(output), f"-f{model_format}"]


def read_glb(path):
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError("file is shorter than a GLB header")
    magic, version, declared = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError("not a binary glTF file")
    if version != 2:
        raise ValueError(f"unsupported GLB version {version}")
    if declared != len(data):
        raise ValueError(
            f"GLB length mismatch (header {declared}, file {len(data)})")
    offset = 12
    document = None
    binary_bytes = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("truncated GLB chunk header")
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if offset + length > len(data):
            raise ValueError("truncated GLB chunk payload")
        payload = data[offset:offset + length]
        if kind == GLB_JSON and document is None:
            text = payload.decode("utf-8").rstrip(" \t\r\n\x00")
            document = json.loads(text)
        elif kind == GLB_BIN:
            binary_bytes = length
        offset += length
    if document is None:
        raise ValueError("GLB has no JSON chunk")
    return document, binary_bytes


def external_image_uris(document):
    uris = []
    for image in document.get("images", []):
        uri = image.get("uri")
        if uri and not uri.startswith("data:"):
            uris.append(uri)
    return uris


def safe_relative_uri(uri):
    text = urllib.parse.unquote(uri.replace("\\", "/"))
    if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", text):
        return None
    relative = Path(text)
    if relative.is_absolute() or any(part == ".." for part in relative.parts):
        return None
    return relative


def resolve_texture(source_dir, relative):
    candidate = source_dir / relative
    if candidate.is_file():
        return candidate, False
    matches = [path for path in source_dir.rglob(relative.name) if path.is_file()]
    if matches:
        return matches[0], True
    return None, False


def copy_textures(document, source_dir, output_dir, force=False, enabled=True):
    result = {"copied": [], "present": [], "missing": [], "conflicts": [],
              "unsafe": [], "warnings": []}
    if not enabled:
        return result
    for uri in external_image_uris(document):
        relative = safe_relative_uri(uri)
        if relative is None:
            result["unsafe"].append(uri)
            result["warnings"].append(
                f"texture URI is not a safe relative path: {uri}")
            continue
        source, found_elsewhere = resolve_texture(source_dir, relative)
        if source is None:
            result["missing"].append(uri)
            result["warnings"].append(f"texture not found: {uri}")
            continue
        if found_elsewhere:
            result["warnings"].append(
                f"texture resolved by basename search: {uri} -> {source}")
        target = output_dir / relative
        if source.resolve() == target.resolve():
            result["present"].append(uri)
            continue
        if target.exists():
            if sha256(source) == sha256(target):
                result["present"].append(uri)
            elif force:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                result["copied"].append(uri)
            else:
                result["conflicts"].append(uri)
                result["warnings"].append(
                    f"texture exists with different content: {target}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        result["copied"].append(uri)
    return result


def summarize(document, bone_cap):
    meshes = document.get("meshes", [])
    primitives = sum(len(mesh.get("primitives", [])) for mesh in meshes)
    skins = document.get("skins", [])
    joints = [len(skin.get("joints", [])) for skin in skins]
    max_joints = max(joints) if joints else 0
    warnings = []
    if not meshes or primitives == 0:
        raise ValueError("converted GLB contains no mesh primitives")
    if max_joints > bone_cap:
        warnings.append(
            f"skin has {max_joints} joints; gX uploads at most {bone_cap} "
            "per draw (the mesh renders in bind pose until retargeted)")
    modes = {primitive.get("mode", 4)
             for mesh in meshes for primitive in mesh.get("primitives", [])}
    if "FB_ngon_encoding" in document.get("extensionsUsed", []) \
            and any(mode != 4 for mode in modes):
        warnings.append(
            "FB_ngon_encoding is present with non-triangle primitives; "
            "gX/raylib ignores the extension, so inspect the result")
    for index, image in enumerate(document.get("images", [])):
        if not image.get("uri") and "bufferView" not in image:
            warnings.append(f"image {index} has neither uri nor bufferView")
    version = document.get("asset", {}).get("version", "")
    if not str(version).startswith("2."):
        warnings.append(f"unexpected glTF asset version: {version!r}")
    return {
        "meshes": len(meshes),
        "primitives": primitives,
        "materials": len(document.get("materials", [])),
        "nodes": len(document.get("nodes", [])),
        "skins": len(skins),
        "joints": max_joints,
        "animations": len(document.get("animations", [])),
        "images": len(document.get("images", [])),
        "warnings": warnings,
    }


def import_model(source, output, assimp, model_format=DEFAULT_FORMAT,
                 force=False, textures=True, strict=False, dry_run=False):
    source = Path(source).resolve()
    output = Path(output).resolve()
    if source.suffix.lower() != ".dae":
        raise ValueError(f"input must be a .dae file: {source}")
    if not source.is_file():
        raise FileNotFoundError(f"input file not found: {source}")
    if output.suffix.lower() != ".glb":
        raise ValueError(f"output must be a .glb file: {output}")
    if output == source:
        raise ValueError("output path would overwrite the source file")
    command = assimp_command(assimp, source, output, model_format)
    result = {
        "source": str(source),
        "output": str(output),
        "format": model_format,
        "command": command,
        "warnings": [],
    }
    if dry_run:
        return result
    if output.exists() and not force:
        raise FileExistsError(
            f"output already exists (use --force): {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.stem + ".importing" + output.suffix)
    if temporary.exists():
        temporary.unlink()
    try:
        completed = subprocess.run(
            assimp_command(assimp, source, temporary, model_format),
            capture_output=True, text=True)
        if completed.returncode != 0:
            detail = (completed.stderr or completed.stdout).strip().splitlines()
            raise RuntimeError(
                f"assimp failed ({completed.returncode}): "
                + (" | ".join(detail[-3:]) if detail else "no output"))
        if not temporary.is_file() or temporary.stat().st_size == 0:
            raise RuntimeError("assimp did not write a GLB")
        document, binary_bytes = read_glb(temporary)
        stats = summarize(document, skin_bone_cap())
        texture_stats = copy_textures(document, source.parent, output.parent,
                                      force=force, enabled=textures)
        if strict and stats["warnings"] + texture_stats["warnings"]:
            raise RuntimeError("strict conversion rejected warnings")
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()
    result.update(stats)
    result.update({
        "bytes": output.stat().st_size,
        "binary_bytes": binary_bytes,
        "sha256": sha256(output),
        "textures": texture_stats,
    })
    result["warnings"] = stats["warnings"] + texture_stats["warnings"]
    return result


def format_summary(result):
    lines = [
        f"MODEL_IMPORT: {Path(result['source']).name} -> {result['output']}",
    ]
    if "meshes" not in result:
        lines.append("  command: " + " ".join(result["command"]))
    if "meshes" in result:
        lines.append(
            f"  meshes={result['meshes']} primitives={result['primitives']} "
            f"materials={result['materials']} nodes={result['nodes']} "
            f"skins={result['skins']} joints={result['joints']} "
            f"animations={result['animations']} images={result['images']}")
        textures = result["textures"]
        lines.append(
            f"  textures: copied={len(textures['copied'])} "
            f"present={len(textures['present'])} "
            f"missing={len(textures['missing'])} "
            f"conflicts={len(textures['conflicts'])} "
            f"unsafe={len(textures['unsafe'])}")
        lines.append(f"  sha256={result['sha256']} bytes={result['bytes']}")
    for warning in result["warnings"]:
        lines.append(f"  warning: {warning}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Convert COLLADA .dae models to GLB for gX.")
    parser.add_argument("inputs", nargs="+", help="COLLADA .dae source files")
    parser.add_argument("--output", help="output GLB (single input only)")
    parser.add_argument("--out-dir", help="output directory for converted GLBs")
    parser.add_argument("--format", choices=FORMATS, default=DEFAULT_FORMAT,
                        help="assimp GLB flavor (default: glb2)")
    parser.add_argument("--assimp", help="assimp executable (or set ASSIMP)")
    parser.add_argument("--force", action="store_true",
                        help="overwrite existing outputs and textures")
    parser.add_argument("--no-textures", action="store_true",
                        help="do not copy external textures")
    parser.add_argument("--strict", action="store_true",
                        help="treat conversion warnings as failure")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the planned command without writing")
    parser.add_argument("--json", action="store_true",
                        help="emit a machine-readable summary")
    args = parser.parse_args(argv)
    if args.output and len(args.inputs) != 1:
        parser.error("--output requires exactly one input")
    try:
        assimp = find_assimp(args.assimp)
    except RuntimeError as error:
        print(f"MODEL_IMPORT: FAIL — {error}", file=sys.stderr)
        return 1
    results = []
    failures = 0
    for text in args.inputs:
        source = Path(text)
        if args.output:
            output = Path(args.output)
        elif args.out_dir:
            output = Path(args.out_dir) / (source.stem + ".glb")
        else:
            output = source.with_suffix(".glb")
        try:
            result = import_model(
                source, output, assimp, model_format=args.format,
                force=args.force, textures=not args.no_textures,
                strict=args.strict, dry_run=args.dry_run)
            results.append(result)
            if not args.json:
                print(format_summary(result))
        except (OSError, ValueError, RuntimeError) as error:
            failures += 1
            if args.json:
                results.append({"source": str(source), "output": str(output),
                                "error": str(error)})
            else:
                print(f"MODEL_IMPORT: FAIL — {source}: {error}", file=sys.stderr)
    if args.json:
        print(json.dumps(results, indent=2))
    if failures:
        print(f"MODEL_IMPORT: FAIL — {failures} of {len(args.inputs)} failed",
              file=sys.stderr)
        return 1
    if not args.dry_run and not args.json:
        print(f"MODEL_IMPORT: PASS — {len(results)} model(s) converted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
