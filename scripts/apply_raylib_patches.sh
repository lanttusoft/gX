#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

# gX — apply vendor patches to the frozen raylib submodule (idempotent).
# The submodule pointer never moves; patches/* overlay opt-in behavior that
# is zero-effect unless its env hook is set. Safe to run on every build:
# already-applied patches are detected and skipped, so incremental builds
# stay fast. Fails loudly on conflict (refresh the patch against the pinned
# raylib commit instead of force-applying).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RL="$ROOT/external/raylib"
if [ ! -e "$RL/.git" ] && [ ! -f "$RL/CMakeLists.txt" ]; then
    echo "apply_raylib_patches: external/raylib missing (run: git submodule update --init)" >&2
    exit 1
fi
shopt -s nullglob
count=0
for patch in "$ROOT"/patches/raylib-*.patch; do
    name="$(basename "$patch")"
    if git -C "$RL" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "apply_raylib_patches: already applied: $name"
    else
        git -C "$RL" apply --check "$patch" >/dev/null 2>&1 || {
            echo "apply_raylib_patches: CONFLICT: $name does not apply cleanly" >&2
            echo "apply_raylib_patches: refresh it against $(git -C "$RL" rev-parse --short HEAD 2>/dev/null)" >&2
            exit 1
        }
        git -C "$RL" apply "$patch"
        echo "apply_raylib_patches: applied: $name"
    fi
    count=$((count + 1))
done
echo "apply_raylib_patches: $count patch(es) ensured"
