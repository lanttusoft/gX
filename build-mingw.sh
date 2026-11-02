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
echo "build-mingw: build/mingw/gX_build.exe"
