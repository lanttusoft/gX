#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: Windows scripting-shell cross-build (MinGW + AngelScript). The shell is a
#   DLL beside the exe; `make mingw-scripting-check` runs the script smoke.
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "build-mingw-scripting: x86_64-w64-mingw32-gcc not found" >&2
    exit 1
fi

cmake -B build/mingw-scripting -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DGX_USE_ANGELSCRIPT=ON
cmake --build build/mingw-scripting -j"${JOBS:-$(nproc)}"

copy_runtime() {
    local name="$1" source
    source="$(x86_64-w64-mingw32-g++ -print-file-name="$name")"
    [ -f "$source" ] || source="/usr/x86_64-w64-mingw32/bin/$name"
    if [ -f "$source" ]; then
        cp -f "$source" build/mingw-scripting/
    else
        echo "build-mingw-scripting: $name not found" >&2
    fi
}
copy_runtime libstdc++-6.dll
copy_runtime libgcc_s_seh-1.dll
copy_runtime libwinpthread-1.dll
echo "build-mingw-scripting: build/mingw-scripting/gX_build.exe + gx_shell.dll"
