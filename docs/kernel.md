# gX Kernel

## What is the kernel

The **kernel** is the set of engine systems that cannot be removed: the
bootstrap/lifecycle and its supporting foundations. Everything else builds on
top of it.

<!-- //ai: include the always-on shared physics module in the kernel inventory. -->
| Module | Directory | Role |
|--------|-----------|------|
| maindata | `src/main*.c`, `src/main.h` | global state, Reset/Refresh, scene-switch handshake, load/loop/unload lifecycle, CLI parse |
| common | `src/common/` | heap, alloc, log, msg, enum (X-macros), marshal (JSON), schema, profiler, random, hashmap, pool, path, shader_compiler, threading, utils |
| wrappers | `src/wrappers/` | backend-agnostic Texture/Shader/Model/Font/Window/drawing/shape/text/input/audio/camera/collision/physics types |
| rhi | `src/rhi/` | raw-GL rendering hardware interface: batch, GL state, FBO, texture, buffer, matrix, mesh, bindless, debug, validate, event log, caps, query |
| backend | `src/backend/` | concrete backend: raylib (only) |
| game shim | `src/game/` | ephemeris math + load/update shim — dependency-free like gxmath, so kernel (see below) |
| physics | `src/physics/` | shared world, rigid-body helpers, EM/thermal appliers and tests; the sandbox scene is cluster-only |

Dependency order (bottom-up): `gxmath → common → wrappers → {rhi, backend} →
maindata`.

Support modules that stay always-on: `audio`, `resource`, `settings`, `input`,
`camera`, `gui`, `tools`, `ui`, `gxmath` (+ the game shim above).

## What is NOT kernel (the disableable cluster)

| Module | Directory |
|--------|-----------|
| renderer | `src/renderer/` (gfx, background, sky, water, raymarch, postprocess, textwriter, …) |
| scene | `src/scene/` (dispatcher + title) |

Plus the layers that depend on the cluster (kept, guarded per-site): GUI
renderer-canvas compositing paths, the scene-switch responders, background CPU
tests.

## gX deltas vs gameX (`docs/kernel.md` there)

Three deliberate divergences, all verified against source:

1. **Atomic pair, not trio.** gameX's `game/` is content (character/items/
   magic…); gX's `game/` is 490 lines of dependency-free ephemeris math +
   shim (include-clean, verified). It stays kernel like gxmath. The gated
   unit is renderer+scene: `GX_ENABLE_RENDERER=OFF` (or SCENE) drops both.
2. **`gX_ENABLE_GAME` is not a cmake option.** It keeps its config.h default
   (`0`) in both configs and gates only the absent game-content headers
   (`game/character.h`, `game/save.h`). Making it an option would break full
   builds.
3. **tools/ stays on, unguarded.** The tools (editor/theme/imgui bridge) only
   touch `gui/` — zero cluster includes, and the kernel link proves zero
   cluster symbols. Their value (editing GUI canvases) survives without
   scenes.

## Disabling the cluster (kernel-only build)

```bash
# kernel-only build (cluster sources out, call sites guarded)
cmake -B build/kernel -DGX_ENABLE_RENDERER=OFF   # SCENE=OFF works too (atomic)
cmake --build build/kernel -j$(nproc)
./build/kernel/gX_build --test-only              # unit tests, no GPU
GX_FRAMES=30 ./build/kernel/gX_build --ai-screenshot-final   # NULL boot
```

Or the CI wrapper (needs a display — Xvfb headless):

```bash
make check-kernel   # cluster-off build → tests → 30-frame NULL boot
```

A kernel-only boot runs the normal loop with `masterIndex = gX_SCENE_NULL`
(no scene switch — the startup switch is compiled out; CLI `--scene` still
honored but nothing serves it). GUI draws over the clear color; the switch
handshake requires only the kernel responders (gui/input/camera).

## Boundary rule

Kernel code must never `#include` or link against `renderer/` or `scene/`.
Audit state (every site verified):

| Kernel file | Cluster reach | Status |
|-------------|---------------|--------|
| `main_load.c` | renderer/scene includes + Load calls + startup Switch | guarded; moduleinfo populated from flags |
| `main_loop.c` | renderer/scene includes + Update/Draw calls | guarded |
| `main_unload.c` | renderer/scene includes + Unload calls | guarded |
| `main_update.c` | renderer/scene includes, Responses checks, RefreshResponses, shot-gate GFX block | guarded (renderer/scene checks conditional) |
| `main_tests.c` | background CPU tests (cluster object) | call guarded |
| `gui/gui.c` | scene/renderer includes, canvas compositing, GPU timers | guarded (composites pre-date this work) |
| `gui/canvas.c` | renderer include + SetCanvasDrawnByGUI | guarded |
| `gui/binding.c`, `builder_widgets.c` | scene include (callback wiring) | includes guarded; no symbol uses |
| `gui/templates/settings.c` | scene include, include-only | guarded |
| `common/utils.h` | game/character.h (absent header) | pre-existing GAME guard |
| `wrappers/drawing.c` | renderer frame hooks | pre-existing RENDERER guards |
| `main.c` | empty RENDERER guard stub | leftover, harmless |
| `tools/*` | gui only | clean, no guards needed |
| `game/*` | none (kernel-clean includes) | clean by construction |

The link is the enforcer: any new reference to an absent cluster symbol
fails `build/kernel` at link time. `make check-kernel` keeps it green.

## Focused allocator regression

<!-- //ai: actual native tracker case, not a blanket allocator correctness claim. -->
<!-- //ai: file-backed input and heap failure policy from the local defensive audit. -->
Heap resizing prepares new data, metadata and optional filename buffers before
replacing any old storage. Failure returns a nonzero status (or NULL from add/seek)
and preserves the old count, pointers, allocation flags and contents. Counts and
32-bit allocator byte products are checked before allocation; pointer offsets use
wide unsigned arithmetic. Successful resize may temporarily hold old plus new
storage and invalidates data pointers, reflected by `refreshState`.

File-backed JSON accepts at most 16 MiB and 64 nested object/array containers.
It rejects non-finite numeric overflow, malformed numbers and trailing
non-whitespace. Finite underflow to zero remains accepted; this is not a universal
numerical-accuracy guarantee. Parser allocation failure rejects the document
instead of returning truncated strings or partially populated containers.
`make runtime-input-check` checks the real sources without an engine window;
`python3 scripts/runtime_input_check.py --sanitize` adds ASan/UBSan.

Null reallocations use the established tracked malloc/calloc paths rather than
matching an unused zero-address tracker slot. Existing moved-address tracking is
preserved. `gX_Alloc_Tests` covers both null branches, live-byte/count accounting,
growth, prefix preservation, clear-tail bytes and cleanup; its errors contribute
to the full and kernel test verdicts. These tests do not certify allocation
failure/zero-size semantics, tracker saturation or thread safety.

## Verification matrix

| Config | Build | `--test-only` | Boot (loop, clean unload, exit 0) |
|--------|-------|---------------|-----------------------------------|
| full | ✅ | 0 errors | ✅ (title switch) |
| kernel-only (RENDERER=OFF) | ✅ | 0 errors (no background tests) | ✅ (NULL, 30 frames, GUI draws) |

## Lean 4 kernel proofs

the [gX proofs repository](https://github.com/lanttusoft/gx-proofs) (see its
[README](https://github.com/lanttusoft/gx-proofs/blob/main/README.md)):
`MainDataRefresh`
(resolution ladder), `MainDataReset` (defaults 1536×1152), `MainDataSwitch`
(flag machine + five-responder contract), `ModuleInfoConsistency` (flag
mirror + kernel_only case, re-enabled by the moduleinfo population in
`main_load.c`). The generated proof catalog currently counts 41 authored
theorem declarations across these four modules; the full audit count includes
generated helpers. The cluster-off build, link boundary and NULL boot are
enforced by `make check-kernel`, not by these Lean contracts.

## Key files

- `CMakeLists.txt` — `GX_ENABLE_RENDERER/SCENE` options, atomic enforcement, source gating
- `src/common/config.h` — `gX_ENABLE_*` defaults (`#ifndef`-guarded trio so cmake `-D` wins)
- `src/main_load.c` — Init (moduleinfo population), Reset, Refresh, Load
- `src/main_update.c` — scene-switch handshake (conditional responders)
- `src/main.h` — `gX_MainData` / `gX_MainData_ModuleInfo`
- `Makefile` — `check-kernel` target
