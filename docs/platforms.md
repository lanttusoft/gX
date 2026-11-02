# Platform support

<!-- //ai: agent-drafted platform tiers; the maintainer owns support claims. -->

gX is **Linux-first**. The engine is raylib + OpenGL 3.3 and the codebase
keeps POSIX assumptions in a small shim layer, so additional desktop targets
are feasible. This page is the support contract: what is tested, what is
best-effort, and what is not supported yet.

## Tiers

| Tier | Target | Build | Runtime | Gates |
| --- | --- | --- | --- | --- |
| **Supported** | Linux x86_64 (X11, OpenGL 3.3+) | `./build.sh` | native | full `make check` and audit matrix |
| **Beta** | Windows x86_64 (MinGW-w64) | `./build-mingw.sh` | native, or Wine for the cross-build smoke | cross-build + `--test-only`; GFX smoke under Wine |
| **Source-only** | macOS | untested | untested | none |
| **Out of scope** | mobile, consoles, WebGPU/Vulkan-only | — | — | — |

"Supported" means the release gates run on that target. "Beta" means the
build and runtime path are exercised, but not every audit gate; report bugs
with the version, OS build and GPU/driver.

## Windows (MinGW-w64)

Requirements: a MinGW-w64 cross toolchain and CMake/Ninja on the Linux host;
Wine is optional for the smoke.

```bash
./build-mingw.sh          # configure + build build/mingw/gX_build.exe
make mingw-check          # cross-build + Wine --test-only
```

The executable is statically linked against the MinGW runtime (`-static`),
so it only imports Windows system DLLs. Ship `resources/` next to it; the
engine resolves the resource root from the executable location.

What works today: the full unit suite under Wine, the GFX demo scene with
OpenGL rendering, and JSON presets (audio/GFX). What is not ported yet:

- **Dynamic modules**: both the devcube lab module and the AngelScript shell
  build as Win32 DLLs and load under Wine (the exe exports the module ABI and
  emits an import library). `make mingw-scripting-check` builds and runs the
  shell. The runtime ships the MinGW libstdc++/pthread DLLs next to the
  build.
- **RenderDoc hook**: the in-process `RENDERDOC_GetAPI` probe is POSIX-only;
  use the RenderDoc UI/launcher on Windows.
- **Crash dumps**: `execinfo`/`ucontext` register+stack dumps are Linux-only;
  Windows logs the signal and the log ring, then exits.
- **Audit scripts**: the matrix assumes X11, `DISPLAY`, POSIX paths and
  Python tooling. The Windows gate is the cross-build plus `--test-only`.

## Linux

The reference target. X11/GLX libraries are linked explicitly; Wayland runs
through XWayland. Headless gates use a hidden or Xvfb display and the
`DISPLAY` environment variable.

## macOS

Not tested. The CMake link set has a Cocoa/OpenGL branch, but raylib's
OpenGL 3.3 path is deprecated on macOS and no runner exists. Treat any macOS
build as source-only until a hardware walk is recorded. A future Metal
backend is a separate renderer track, not a portability fix.

## Portability rules

- Platform-specific code belongs in `src/common/platform.h` or a guarded
  block next to the subsystem; do not spread `#ifdef _WIN32` through engine
  code.
- Win32 headers must not leak into translation units that include raylib
  (`Rectangle`, `CloseWindow` and `ShowCursor` collide). Use the shim or
  local guards.
- Keep the Linux build green and the MinGW cross-build green for every
  platform-shim change. `make mingw-check` is the focused gate; the full
  matrix stays Linux.
- New public `gX_Platform_*` families need an
  [`api_reference.md`](api_reference.md) entry.
