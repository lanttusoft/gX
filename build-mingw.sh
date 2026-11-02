#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.
#ai: Windows x86_64 cross-build (MinGW-w64). The runtime smoke lives in
#   `make mingw-check`; this script only configures and builds.
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "build-mingw: x86_64-w64-mingw32-gcc not found (install mingw-w64)" >&2
    exit 1
fi

cmake -B build/mingw -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build/mingw -j"${JOBS:-$(nproc)}"

#ai: the lab module is a DLL and needs the MinGW pthread runtime next to the
#   exe; the engine executable itself is static.
runtime="$(x86_64-w64-mingw32-gcc -print-file-name=libwinpthread-1.dll)"
if [ ! -f "$runtime" ] && [ -f /usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll ]; then
    runtime=/usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll
fi
if [ -f "$runtime" ]; then
    cp -f "$runtime" build/mingw/
else
    echo "build-mingw: libwinpthread-1.dll not found; module may not load" >&2
fi
echo "build-mingw: build/mingw/gX_build.exe (+ gx_devcube.dll)"
