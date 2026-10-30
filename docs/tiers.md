# gX — Tier Model (L0–L3)

<!-- //ai: architectural model recorded 2026-10-12; descriptive, no runtime effect. -->
How a request flows through gX, where each subsystem sits, and which
surfaces are contractual. The point of the model is to keep boundaries
honest: each tier only talks to the one below through a membrane, and a
capability is never reachable from exactly one tier.

## The tiers

| Tier | Role | gX reality | Boundary / membrane |
|---|---|---|---|
| **L0 platform** | pseudo-hardware / firmware | OS, GL, X11, miniaudio; raylib (submodule); GLFW inside raylib | `external/raylib`; `src/backend/raylib/` (the only raylib includers); `src/wrappers/` (the only raylib-typed API); `src/rhi/` (thin GL surface on raylib's glad) |
| **L1 kernel** | engine internals | main/load/loop, input, camera, audio, physics, resource, settings, renderer, scene, gui, gxmath, marshal, heap/log | `gX_*` C API; CLI flags; `GX_*` env; log stream; settings/save/state files. Kernel modules are compile-time selectable subsystems (see below) |
| **L2 shell** | interactive + scripted control | AngelScript host (`src/scripting*`), developer console (`src/console/`), gX Tools / ImGui (`src/tools/`) | Script bindings + console commands + tool panels; every shell capability keeps a C path in L1 (the shell is never the only route) |
| **L3 orchestration** | batch drivers / gates | `scripts/*.py`, `make check`, audit summaries, capture pipelines, certificates | CLI + env + exit codes + logs + files. L3 composes L1/L2 runs; it adds no engine capability |

**Level 0 is not only "hardware"**: it is the substitution frontier. gX works
on raylib today; the wrappers/rhi/backend split is what makes a different
platform layer a bounded change. "Middleware" (ODE, ImGui, AngelScript) is
*not* L0 — it enters laterally and is isolated behind its own membrane.

## Kernel modules (L1, compile-time)

gX's "modules" are static selections, not dynamically loaded binaries:

| Module group | Selection | Absent behavior |
|---|---|---|
| Renderer + scene | `GX_ENABLE_RENDERER` / `GX_ENABLE_SCENE` (atomic cluster) | kernel-only build: lifecycle + gui/tools |
| Physics backend | `GX_USE_ODE` (CMake auto-detect) | no-op wrapper stubs; physics module idles |
| Scripting shell | `GX_USE_ANGELSCRIPT` (opt-in, default OFF) | no scripting/console code in the payload; the shell builds as `gx_shell.so` |
| Compute light path | `GX_COMPUTE` (isolated build config) | uniform-light GL 3.3 path |
| Per-renderer modules | always compiled with the cluster | each has its own gate (gfx, background, pp, raymarch, ...) |

Each optional group carries its own gate (`check-kernel`, `check-scripting`,
`compute-feature-check`) so the default admission path is never widened by an
optional module. Runtime plugin loading would require a new registration/ABI
contract; the current model is deliberately static (the measured `.so`
contract options and their costs are in
[`dynamic_modules.md`](dynamic_modules.md)).

## The CLI, precisely placed

CLI arguments are **L1 ingress, not a tier of their own**: they are the
kernel's boot-time declarative configuration, parsed in `src/main_parse.c`,
applied during `gX_Load`, and then inert. Three sibling surfaces exist:

- **env vars** — ambient/time-independent config (`GX_FRAMES`, `GX_SCRIPT_DIR`,
  `GX_CONSOLE_EXEC`, `GX_GUI_MARKUP`, ...);
- **console commands** — the interactive twin (`/scene`, `/scripts`, `/state`);
- **the C API** — the programmatic path.

The **mirror principle**: a kernel capability needs an L1 path, and an L2
binding whenever the shell (scripts or console) needs it; the same knob
should not exist on only one tier. `scripts/common_sense_check.py` enforces
one half of this today (it flags declared-but-unused CLI arguments). Two flag
classes should stay distinguishable in `--help`: **authoring** flags (mean
the same to a human and a gate) and **harness** flags (`GX_FRAMES`, audit
env) that exist for L3.

## Middleware (lateral expansion)

A middleware addition is acceptable when it passes this test:

1. it enters through a **membrane** (one directory/API family; the rest of the
   kernel never includes its headers — ODE via `wrappers/physics`, ImGui via
   `src/tools/imgui`, AngelScript via `src/scripting*` + `src/console/`);
2. it is **optional at build time**, and the kernel stays self-sufficient
   without it (the OFF builds are the standing proof);
3. it has its **own gate** and its own docs;
4. the capability it provides keeps an L1 C path so disabling it degrades,
   never strands, the engine.

## Worked example: input through the tiers

`F10` console toggle: L0 OS/GLFW key event → L1 `gX_Input_Update` key state →
L2 console update reads `input.consoleKey` and opens the panel → L3 can drive
the same surface via `GX_CONSOLE_EXEC` (headless) or assert on the log. The
same key is what gameX used, which is why the slot survived in `src/input.h`
while the console itself was re-ported.
