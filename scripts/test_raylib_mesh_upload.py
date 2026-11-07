#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compile complete backend units; the vendored destructor supplies its own capacity and loop, with only allocation/GL boundaries instrumented.
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))


class MeshUploadContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="mesh_upload_", dir=SCRATCH)
        cls.directory = Path(cls.temporary.name)
        cls.sanitize = os.environ.get("GX_MESH_UPLOAD_SANITIZE") == "1"
        native = (ROOT / "external/raylib/src/rmodels.c").read_text()
        capacity = native[native.index("#ifndef MAX_MESH_VERTEX_BUFFERS"):
                          native.index("#ifndef MAX_FILEPATH_LENGTH")]
        start = native.index("void UnloadMesh(Mesh mesh)\n{")
        unload = native[start:native.index("// Export mesh data to file", start)]
        header = cls.directory / "allocation.h"
        header.write_text("#include <stddef.h>\n"
                          "void *MeshTest_Calloc(size_t Count, size_t Size);\n"
                          "void MeshTest_Free(void *Data);\n")
        probe = cls.directory / "probe.c"
        probe.write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "src/backend/raylib/raylib_wrappers.h"
#include "src/backend/raylib/raylib_wrappers_model.h"
''' + capacity + r'''
static unsigned int *LiveTable;
static size_t AllocatedSlots;
static unsigned int Uploaded[9];
static int UploadCount, DeleteCount, DeleteCalls, VaoDeletes, Calls, Allocations;
static bool ExpectedDynamic;

void *MeshTest_Calloc(size_t Count, size_t Size)
{
    assert(!LiveTable && Size == sizeof(*LiveTable));
    AllocatedSlots = Count;
    Allocations++;
#ifdef MESH_TEST_SANITIZE
    LiveTable = calloc(Count, Size);
#else
    LiveTable = calloc(Count + 2, Size);
    assert(LiveTable);
    LiveTable[Count] = 0x51a7cafeu;
    LiveTable[Count + 1] = 0x51a7cafeu;
#endif
    assert(LiveTable);
    return LiveTable;
}

void MeshTest_Free(void *Data)
{
    if (Data && Data == LiveTable)
    {
        assert(AllocatedSlots == MAX_MESH_VERTEX_BUFFERS);
        assert(DeleteCalls == MAX_MESH_VERTEX_BUFFERS);
        assert(DeleteCount == UploadCount && VaoDeletes == 1);
#ifndef MESH_TEST_SANITIZE
        assert(LiveTable[AllocatedSlots] == 0x51a7cafeu);
        assert(LiveTable[AllocatedSlots + 1] == 0x51a7cafeu);
#endif
        LiveTable = NULL;
    }
    free(Data);
}

unsigned int rlLoadVertexArray(void)
{
    Calls++;
    return 42;
}
bool rlEnableVertexArray(unsigned int Id)
{
    Calls++;
    assert(Id == 42);
    return true;
}
void rlDisableVertexArray(void)
{
    Calls++;
}
void rlEnableVertexAttribute(unsigned int Index)
{
    Calls++;
    assert(Index < 9);
}
void rlDisableVertexAttribute(unsigned int Index)
{
    Calls++;
    assert(Index < 9);
}
void rlSetVertexAttribute(unsigned int Index, int Components, int Type, bool Normalized, int Stride, int Offset)
{
    Calls++;
    assert(Index < 9 && Components > 0 && Type && !Stride && !Offset);
    (void)Normalized;
}
void rlSetVertexAttributeDefault(int Index, const void *Value, int Type, int Count)
{
    Calls++;
    assert(Index >= 0 && Index < 9 && Value && Type && Count > 0);
}
unsigned int rlLoadVertexBuffer(const void *Data, int Bytes, bool Dynamic)
{
    Calls++;
    assert(Data && Bytes > 0 && Dynamic == ExpectedDynamic && UploadCount < 9);
    unsigned int Id = 100u + (unsigned int)UploadCount;
    Uploaded[UploadCount++] = Id;
    return Id;
}
unsigned int rlLoadVertexBufferElement(const void *Data, int Bytes, bool Dynamic)
{
    return rlLoadVertexBuffer(Data, Bytes, Dynamic);
}
void rlUnloadVertexArray(unsigned int Id)
{
    assert(Id == 42);
    VaoDeletes++;
}
void rlUnloadVertexBuffer(unsigned int Id)
{
    DeleteCalls++;
    if (!Id) return;
    int Found = 0;
    for (int Index = 0; Index < UploadCount; Index++)
    {
        if (Uploaded[Index] == Id)
        {
            Uploaded[Index] = 0;
            DeleteCount++;
            Found = 1;
            break;
        }
    }
    assert(Found);
}
void TraceLog(int Level, const char *Format, ...)
{
    (void)Level;
    (void)Format;
}
''' + unload + r'''
int main(int Argc, char **Argv)
{
    assert(Argc == 5);
    int Mode = atoi(Argv[1]);
    int Cycles = atoi(Argv[2]);
    int Reload = atoi(Argv[3]);
    ExpectedDynamic = atoi(Argv[4]) != 0;
    assert(MAX_MESH_VERTEX_BUFFERS == RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS + 1);
    for (int Cycle = 0; Cycle < Cycles; Cycle++)
    {
        gX_Mesh Mesh = {0};
        Mesh.vertexCount = 3;
        Mesh.triangleCount = 1;
        Mesh.vertices = calloc(9, sizeof(float));
        Mesh.texcoords = calloc(6, sizeof(float));
        Mesh.normals = calloc(9, sizeof(float));
        if (Mode & 1)
        {
            Mesh.boneIndices = calloc(12, sizeof(unsigned char));
            Mesh.boneWeights = calloc(12, sizeof(float));
        }
        if (Mode & 2)
        {
            Mesh.colors = calloc(12, sizeof(unsigned char));
            Mesh.tangents = calloc(12, sizeof(float));
            Mesh.texcoords2 = calloc(6, sizeof(float));
            Mesh.indices = calloc(3, sizeof(unsigned short));
        }
        UploadCount = DeleteCount = DeleteCalls = VaoDeletes = Calls = Allocations = 0;
        raylib_Mesh_Upload(&Mesh, ExpectedDynamic);
        assert(AllocatedSlots == MAX_MESH_VERTEX_BUFFERS && Allocations == 1);
        assert(Mesh.vaoId == 42 && Mesh.vboId == LiveTable);
#ifdef GX_GPU_SKINNING
        assert(UploadCount == 3 + ((Mode & 2) ? 4 : 0) + ((Mode & 1) ? 2 : 0));
        assert((Mesh.vboId[7] != 0) == ((Mode & 1) != 0));
        assert((Mesh.vboId[8] != 0) == ((Mode & 1) != 0));
#else
        assert(UploadCount == 3 + ((Mode & 2) ? 4 : 0));
        assert(!Mesh.vboId[7] && !Mesh.vboId[8]);
#endif
        if (Reload)
        {
            int Before = Calls;
            unsigned int *Table = Mesh.vboId;
            raylib_Mesh_Upload(&Mesh, !ExpectedDynamic);
            assert(Calls == Before && Allocations == 1 && Mesh.vboId == Table);
        }
        raylib_Mesh_Unload(Mesh);
        assert(!LiveTable && DeleteCount == UploadCount);
    }
    return 0;
}
''')
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-std=gnu11", "-O0", "-g", "-ffunction-sections", "-fdata-sections",
                 "-DGAME_BACKEND_GX", "-DGAME_BACKEND_RAYLIB",
                 "-DGRAPHICS_API_OPENGL_33", "-DSUPPORT_GPU_SKINNING=1",
                 "-DRL_CALLOC=MeshTest_Calloc", "-DRL_FREE=MeshTest_Free",
                 "-include", str(header)]
        for relative in (".", "src", "src/backend/raylib", "external",
                         "external/raylib/src", "external/raylib/src/external", "build/generated"):
            flags += ["-I" + str(ROOT / relative)]
        if cls.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer", "-DMESH_TEST_SANITIZE"]
        cls.executables = {}
        sources = [ROOT / "src/backend/raylib/raylib_wrappers_model.c",
                   ROOT / "src/backend/raylib/raylib_wrappers_load.c",
                   ROOT / "src/wrappers/convert.c", probe]
        for skinning in (False, True):
            variant = flags + (["-DGX_GPU_SKINNING=1"] if skinning else [])
            objects = []
            for index, source in enumerate(sources):
                target = cls.directory / f"{int(skinning)}_{index}.o"
                result = subprocess.run(compiler + variant + ["-c", str(source), "-o", str(target)],
                                        capture_output=True, text=True)
                if result.returncode:
                    raise AssertionError(result.stdout + result.stderr)
                objects.append(str(target))
            executable = cls.directory / f"probe_{int(skinning)}"
            result = subprocess.run(compiler + variant + objects +
                                    ["-Wl,--gc-sections", "-lm", "-o", str(executable)],
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.executables[skinning] = executable

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, mode=0, cycles=1, reload=False, dynamic=False, skinning=True):
        environment = dict(os.environ)
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
        result = subprocess.run([str(self.executables[skinning]), str(mode), str(cycles),
                                 str(int(reload)), str(int(dynamic))],
                                env=environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rigid_mesh_retains_zero_bone_slots_for_native_unload(self):
        self.run_case()

    def test_skinned_mesh_owns_both_bone_buffers(self):
        self.run_case(mode=1)

    def test_all_nine_slots_upload_and_unload(self):
        self.run_case(mode=3, dynamic=True)

    def test_already_uploaded_mesh_is_not_reallocated(self):
        self.run_case(mode=3, reload=True)

    def test_repeated_rigid_and_skinned_lifetimes(self):
        for mode in (0, 3):
            with self.subTest(mode=mode):
                self.run_case(mode=mode, cycles=100, reload=True)

    def test_wrapper_without_skinning_still_matches_skinned_raylib_unload(self):
        self.run_case(mode=2, reload=True, skinning=False)


if __name__ == "__main__":
    unittest.main()
