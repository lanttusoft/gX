#!/usr/bin/env bash
# gX build helper — mirrors gameX build.sh interface
set -e
BUILD_TYPE="${1:-Debug}"
BUILD_DIR="build"
GX_COMPUTE="${GX_COMPUTE:-0}"

#opencode: vendor patches for the frozen raylib submodule (idempotent,
# zero-effect unless their env hooks are set). Must run before the build so
# a fresh clone gets the same raylib behavior as an incremental tree.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
bash "$ROOT_DIR/scripts/apply_raylib_patches.sh"

cmake -B "$BUILD_DIR" . -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DGX_COMPUTE="$GX_COMPUTE"
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "Build complete: $BUILD_DIR/gX_build"
