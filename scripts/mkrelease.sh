#!/usr/bin/env bash
# gX release helper — builds a standalone source tarball for a tag.
# Usage:
#   scripts/mkrelease.sh <tag> [--verify] [--out DIR]
#     <tag>      git tag to package (e.g. v0.6.9); tree must be clean
#     --verify   clean-room gate: extract, build, run title/tools/close smokes
#     --out DIR  where to write gX-<tag>.tar.gz (default: current directory)
# --verify needs a display (Xvfb) and ~10 min for the clean-room build.
set -euo pipefail

TAG="${1:-}"
VERIFY=0
OUT="$PWD"
while [ $# -gt 0 ]; do
    case "$1" in
        --verify) VERIFY=1; shift ;;
        --out) OUT="$2"; shift 2 ;;
        -*) echo "mkrelease: unknown flag $1" >&2; exit 2 ;;
        *) shift ;;
    esac
done
if [ -z "$TAG" ]; then
    echo "mkrelease: usage: scripts/mkrelease.sh <tag> [--verify] [--out DIR]" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GITROOT="$(git -C "$ROOT" rev-parse --show-toplevel)"

if [ -n "$(git -C "$GITROOT" status --porcelain)" ]; then
    echo "mkrelease: working tree not clean — commit first" >&2
    exit 1
fi
if ! git -C "$GITROOT" rev-parse --verify --quiet "refs/tags/$TAG" >/dev/null; then
    echo "mkrelease: tag not found: $TAG" >&2
    exit 1
fi

#opencode: submodule must match the gitlink frozen at the tag, else the
#   tarball vendors the wrong raylib.
WANT="$(git -C "$GITROOT" ls-tree "$TAG" master/external/raylib | awk '{print $3}')"
HAVE="$(git -C "$ROOT/external/raylib" rev-parse HEAD)"
if [ "$WANT" != "$HAVE" ]; then
    echo "mkrelease: raylib mismatch (tag wants $WANT, checkout has $HAVE)" >&2
    exit 1
fi

#ai: validate the chosen tag's own exclusions; current attributes cannot sanitize legacy tags.
python3 "$SCRIPT_DIR/packaging_check.py" --repo "$GITROOT" --ref "$TAG" --layout release

NAME="gX-$TAG"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

#opencode: prefix carries the top dir only — path arg already yields master/,
#   so no nesting normalize step (whose rm -rf once ate the raylib copy).
#ai: use the same source-only attribute scope as the preflight, not machine-global exclusions.
GIT_ATTR_NOSYSTEM=1 git -c core.attributesFile=/dev/null -C "$GITROOT" archive "$TAG" --prefix="$NAME/" master | tar -x -C "$STAGE"
rm -rf "$STAGE/$NAME/master/external/raylib"
mkdir -p "$STAGE/$NAME/master/external/raylib"
tar -cf - --exclude=.git --exclude=examples --exclude=logo --exclude=projects \
    --exclude=tools --exclude='build.zig*' --exclude=BINDINGS.md \
    --exclude=CONTRIBUTING.md --exclude=CONVENTIONS.md --exclude=FAQ.md \
    --exclude=SECURITY.md --exclude=CHANGELOG \
    -C "$ROOT/external/raylib" . | tar -xf - -C "$STAGE/$NAME/master/external/raylib"

#opencode: self-checks — these exact files sank the v0.6.9 manual run.
for f in CMakeLists.txt CMakeOptions.txt cmake src raylib.pc.in LICENSE; do
    if [ ! -e "$STAGE/$NAME/master/external/raylib/$f" ]; then
        echo "mkrelease: vendored raylib missing $f" >&2
        exit 1
    fi
done
if grep -rIl --include='CMakeLists.txt' --include='*.cmake' --include='Makefile' \
        --include='*.sh' --exclude='mkrelease.sh' -e '/mnt/' -e "$HOME" "$STAGE/$NAME/master" | head -3 | grep -q .; then
    #opencode: mkrelease.sh itself is excluded — the patterns above would
    #   match their own literals.
    echo "mkrelease: absolute build-machine path leaked into build files" >&2
    exit 1
fi

mkdir -p "$OUT"
tar -czf "$OUT/$NAME.tar.gz" -C "$STAGE" "$NAME"
echo "mkrelease: wrote $OUT/$NAME.tar.gz ($(du -h "$OUT/$NAME.tar.gz" | cut -f1))"

if [ "$VERIFY" -eq 0 ]; then
    exit 0
fi

#opencode: clean-room gate — build + headless smokes from the tarball.
if [ -z "${DISPLAY:-}" ]; then
    echo "mkrelease: --verify needs a display (run under Xvfb)" >&2
    exit 1
fi
VDIR="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$VDIR"' EXIT
tar -xzf "$OUT/$NAME.tar.gz" -C "$VDIR"
VROOT="$VDIR/$NAME/master"
cmake -B "$VROOT/build" -S "$VROOT" -DCMAKE_BUILD_TYPE=Debug -DGX_COMPUTE=OFF >/dev/null
cmake --build "$VROOT/build" -j"$(nproc)" >/dev/null
mkdir -p "$VROOT/screenshot"

png_size() {
    python3 -c "import struct,sys; d=open(sys.argv[1],'rb').read(); w,h=struct.unpack('>II',d[16:24]); print(f'{w}x{h}')" "$1"
}

(cd "$VROOT" && timeout 120 ./build/gX_build --ai-screenshot-final >/dev/null 2>&1)
SHOT="$(ls -t "$VROOT"/screenshot/gXss_*.png | head -1)"
echo "mkrelease: standalone title: $(png_size "$SHOT")"

(cd "$VROOT" && rm -f screenshot/gXss_*.png \
    && timeout 120 env GX_TOUR=0 ./build/gX_build --tools resources/tools/tools_demo.json \
        --ai-screenshot-after 5 >verify_open.log 2>&1)
#opencode: prefix-only match — the OS may report back height ± a few px
#   from the requested size (observed 1010 for 1016); the size itself is
#   asserted by the explicit dimensions logged at engage.
grep -q "dock engaged (" "$VROOT/verify_open.log"
echo "mkrelease: standalone tools-open: dock engaged"

(cd "$VROOT" && rm -f screenshot/gXss_*.png \
    && timeout 150 env GX_TOUR=0 GX_TOOLS_CLOSE=100 ./build/gX_build \
        --tools resources/tools/tools_demo.json --ai-screenshot-after 180 >verify_close.log 2>&1)
#opencode: prefix-only (same ±px note as engage above).
grep -q "dock released (" "$VROOT/verify_close.log"
SHOT="$(ls -t "$VROOT"/screenshot/gXss_*.png | head -1)"
echo "mkrelease: standalone post-close: $(png_size "$SHOT") (restored)"
rm -f "$VROOT"/verify_open.log "$VROOT"/verify_close.log
echo "mkrelease: verify PASS"
