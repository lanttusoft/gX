#!/usr/bin/env bash
#opencode: gx-clean — recreate the disposable verification worktree
#   (the private lane guide): detached at a commit, raylib as a pristine clone
#   at the pinned gitlink, the quarantined assets + settings seeded from the
#   main worktree, then an optional Debug build and check-fast gate.
#
# Usage:
#   scripts/gx_clean.sh [commit] [--path DIR] [--no-build] [--check-fast]
#
#   commit        a commit or tag (default HEAD)
#   --path DIR    worktree location (default /tmp/opencode/gx_clean)
#   --no-build    create the tree only (skip the build)
#   --check-fast  run `make check-fast` after the build, under the GPU lock
#
# The script refuses to touch a DIR that exists but is not a worktree of this
# repository; a registered worktree at DIR is recreated.
set -euo pipefail

commit="HEAD"
path="/tmp/opencode/gx_clean"
do_build=1
do_check_fast=0

while [ $# -gt 0 ]; do
    case "$1" in
        --path) path="$2"; shift 2 ;;
        --no-build) do_build=0; shift ;;
        --check-fast) do_check_fast=1; shift ;;
        -*) echo "gx-clean: unknown option $1" >&2; exit 2 ;;
        *) commit="$1"; shift ;;
    esac
done

common="$(git rev-parse --path-format=absolute --git-common-dir)"
top="$(git rev-parse --show-toplevel)"
main_root="$(dirname "$common")"
main_proj="$main_root/master"
module_dir="$common/modules/master/external/raylib"

if [ ! -d "$module_dir" ]; then
    echo "gx-clean: raylib module missing at $module_dir" >&2
    echo "  (external/raylib must be an initialized submodule of the main tree)" >&2
    exit 1
fi

sha="$(git -C "$top" ls-tree "$commit" master/external/raylib | awk '{print $3}')"
if [ -z "$sha" ]; then
    echo "gx-clean: no raylib gitlink at $commit" >&2
    exit 1
fi

if git worktree list --porcelain | grep -qxF "worktree $path"; then
    git worktree remove --force "$path"
    echo "gx-clean: removed the previous worktree at $path"
elif [ -e "$path" ]; then
    echo "gx-clean: $path exists and is not a worktree of this repo — refusing" >&2
    exit 1
fi

git worktree add --detach "$path" "$commit"
resolved="$(git -C "$path" rev-parse --short HEAD)"
echo "gx-clean: worktree at $path (${commit} -> ${resolved})"

git clone -q "$module_dir" "$path/master/external/raylib"
git -C "$path/master/external/raylib" checkout -q "$sha"
echo "gx-clean: raylib pinned at $(git -C "$path/master/external/raylib" rev-parse --short HEAD)"

seeded=0
missing=0
for asset in \
    "resources/settings.json"
do
    if [ -f "$main_proj/$asset" ]; then
        mkdir -p "$path/master/$(dirname "$asset")"
        cp "$main_proj/$asset" "$path/master/$asset"
        seeded=$((seeded + 1))
    else
        echo "gx-clean: note — $asset not present in the main tree (skipped)"
        missing=$((missing + 1))
    fi
done
echo "gx-clean: seeded $seeded asset(s), $missing skipped"

if [ "$do_build" -eq 1 ]; then
    (cd "$path/master" && bash build.sh Debug >/dev/null)
    echo "gx-clean: Debug build complete ($path/master/build/gX_build)"
fi

if [ "$do_check_fast" -eq 1 ]; then
    (cd "$path/master" && DISPLAY="${DISPLAY:-:1}" \
        flock "$common/gx-gpu.lock" make check-fast)
fi

echo "gx-clean: done — $path is ready"
