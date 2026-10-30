# gX — Dynamic modules (.so)

<!-- //ai: measured 2026-10-12; Route A + gx_shell.so implemented the same day (see "Implemented host" below). -->
Question: can shared objects (`.so`) let gX **share code** (one binary + modules)
and/or **lessen the lean gate**? Answer: sharing yes (with a strict ABI), gate
relief only as honest *profiles* — a `.so` split changes what the gate measures,
not what runs. Numbers below are measured on this tree, not estimates.

## Measurements

Method: Release objects were relinked with modified link lines into scratch
binaries (no production objects touched; the default Release binary was
rebuilt normally afterwards), then graded with
`scripts/lean_gate.sh <binary> --json`.

| Build | Payload (text+rodata+data) | Notes |
|---|---:|---|
| Default Release | **3,908,859** | text 3,224,271 / rodata 538,044 / data 146,544; BSS 2,626,816 |
| Kernel-only Release (`GX_ENABLE_RENDERER=OFF`) | 2,586,235 | renderer+scene cluster = **1,322,624** |
| Shell build Release (`GX_USE_ANGELSCRIPT=ON`) | 5,291,835 | shell = **1,382,976**; BSS 2,861,888 (+235,072) |
| Default + `-Wl,--export-dynamic` | 5,299,279 | **+1,390,420** — exported globals become GC roots, `--gc-sections` is defeated (text alone 3.22 → 4.53 MB) |
| Default + curated `-Wl,--dynamic-list` | 3,908,859 | **+0 gated bytes**; `.dynsym` +32 B, `.dynstr` +176 B (outside the payload metric, page-absorbed) |

Other facts:

- The default Release exe defines **6** dynamic symbols; a plugin cannot bind
  against engine symbols today without one of the routes below.
- The curated list exported exactly the requested globals
  (`maindata`, `renderer`, `gui`, `input`, `camera`, `settings`) — the rest of
  the exe (7,003 symbols, ~1,824 `gX_*`) stayed hidden.
- gX already consumes shared objects at the dependency level: the physics
  wrapper links `libode.so.8` (NEEDED) and the RenderDoc hook resolves
  `RENDERDOC_GetAPI` at runtime via `dlsym(RTLD_DEFAULT)`.
- Capacity context from `docs/lean_reassessment.md` (2026-10-03, attributed
  retained bytes): raylib ~1.12 MB, renderer ~0.76 MB, ImGui+bridge ~0.63 MB,
  engine GUI ~0.26 MB, authoring tools ~0.19 MB, pooled strings ~0.45 MB.
- Current caps: payload 3,910,000 (**1,141 B headroom**), BSS 2,705,000
  (78,184 B headroom). The gate measures one ELF's `.text/.rodata/.data`.

## What modules would be

Three distinct uses; only the first two fit gX today:

1. **Optional subsystems as modules** — one default exe plus drop-in
   `gx_shell.so` (scripting + console + debugger panels), editor/lab
   experiments, etc. Replaces the "one build dir per feature config" pattern
   with runtime loading. The default distribution can stay lean *by
   construction*.
2. **Lab modules** — experiments (demoscript-style sequencing, audio/synth
   panels, gameX ports under test) built against the gX C API, never shipped.
3. **Native mods** — possible, but arbitrary code + a forever-ABI; the
   AngelScript tier remains the sanctioned mod surface. Not recommended now.

**Not a gameX↔gX bridge.** A shared `.so` would couple the frozen lab to the
release line and bypass the porting pipeline and the copyright/provenance
isolation. Sharing between the repos stays source-level (ports) or
script-level.

## ABI: the two viable routes

Any module must use the **host's** engine instances: gX is global-state
(`maindata`, `renderer`, `gui`, `input`, `camera`, `settings`), and a module
that statically links engine objects gets private copies — broken. Boundary
rules that follow:

- **C ABI at the seam.** C++ is fine *inside* a module (the AngelScript SDK
  precedent: C++ confined to the module, C entry point), but no C++ objects,
  exceptions, or names mangling across it; two C++ runtimes in one process is
  the classic libstdc++ hazard (gameX's libangelscript ABI mismatch is the
  cautionary tale).
- **Never `dlclose` in normal operation.** glibc rarely unmaps; static
  constructors/TLS make reload unsafe. Dev iteration = versioned filenames or
  process restart (gX restarts are cheap).

### Route A — curated dynamic-list export (measured ~free)

The exe links with `-Wl,--dynamic-list=module_exports.txt`; the list contains
the module-facing C API plus the shared globals. Modules keep undefined `gX_*`
symbols, `dlopen(RTLD_NOW|RTLD_LOCAL)` resolves them from the exe.

- Cost: ~0 gated bytes (measured); `.dynsym/.dynstr` grow outside the metric.
- Contract: the export list *is* the ABI — it must be versioned and treated as
  append-only within an ABI major.
- Full `--export-dynamic` is not an option: +1.39 MB and it defeats
  `--gc-sections` (measurement above).

### Route B — versioned function table (no exe exports)

The host passes `const gX_Module_API *` to `gX_Module_Entry(...)`; the table
carries `{abi_version, struct_size, build_id}` plus accessors (log, heap,
maindata/renderer/gui/input/camera/settings pointers or getters, scene switch,
frame hooks). Same shape as Vulkan/GLFW/ImGui host tables.

- Cost: a few hundred lines of host + table definitions; exe delta ~0 gated
  bytes (no exports at all).
- Contract: explicit and testable; modules never resolve host symbols; the
  table version/size handshake makes drift a startup error instead of a
  crash.
- Better fit for a growing module surface or third-party authors; Route A is
  enough for in-tree modules.

Both routes want the same loader shape: `dlopen` → resolve
`gX_Module_Entry` → handshake (version, size, build id) → register into a
kernel-side update/draw registry (mirroring the scripting frame hooks in
`gX_Scripting_RegisterUpdateHook/RegisterDrawHook`, moved down to the kernel
when the first non-scripting module appears).

## The lean gate, honestly

The gate measures one ELF. A `.so` split therefore **changes the metric**
without changing the runnable set — with modules shipped, an exe-only "lean"
claim would be accounting, not leanness. Two honest framings:

- **Profile framing** (already the doctrine in `docs/lean_reassessment.md`:
  "decide which capabilities belong in each shipping profile"): the release
  profile is the exe; dev/lab profiles are exe + modules. Modules not shipped
  in the release profile keep the gate honest *and* genuinely lean
  distribution.
- **Shipped-set framing**: if modules ship, budget them. `lean_gate.sh`
  already accepts an arbitrary binary, so per-module grading is a tooling
  convention plus a `.lean_budget.json` extension, e.g.:

  ```json
  "modules": {
    "gx_shell": {
      "image_max_bytes": 1500000,
      "bss_max_bytes": 300000,
      "rationale": "scripting + console + debugger panels, shipped as a module"
    }
  }
  ```

  and the release accounting reads "exe + shipped modules" — never the exe
  alone while modules ride along.

What modules do **not** do: reclaim capacity from the default payload. The
heavy parts are the mandatory ones (renderer+scene 1.32 MB, raylib ~1.12 MB,
ImGui/tools ~0.82 MB combined); the one optional-heavy part (the shell,
1.38 MB) is already outside the default gate. Genuine relief remains cold-path
`-Os` (−33 KB measured), scope decisions per profile, and opt-in builds.

## Implemented host (2026-10-12)

Route A + the shell as the first client is built:

- **Host** (`src/module/module.{h,c}`): `dlopen(RTLD_NOW|RTLD_LOCAL)` →
  `gX_Module_Entry(const gX_Module_Host *)` → handshake (`GX_MODULE_ABI_VERSION`,
  struct size, optional `gX_Module_ABI`) → registration into the kernel hook
  registry (update/draw/scene-enter/scene-exit/shutdown/exit-code) and the
  interop slots the engine calls unconditionally (markup onclick + companion
  scripts, ImGui tools panel, console overlay). Handles are never `dlclose`d.
  Loading: `--module <path.so>` → `GX_MODULE=a:b` → `modules/*.so` auto-scan;
  explicit failures fail the boot, auto-scan misses only warn.
- **Client** (`gx_shell.so`, `GX_USE_ANGELSCRIPT=ON`): SDK + `src/scripting*`
  + `src/console` + the tour APIs + the debugger panel + the new
  `src/module/gx_shell.c` entry. Measured Release: payload **1,262,231**,
  BSS **253,232**.
- **Export list** (`modules/module_exports.txt`, `scripts/module_exports.py`):
  the module's undefined symbols intersected with the exe's object universe
  (pre-`--gc-sections`, so dropped-but-needed APIs are re-rooted) —
  **202 symbols** (engine + raylib + ImGui), ~4 KB of text. `make
  module-exports` regenerates it and relinks; `make check-scripting` fails on
  drift (the list is append-only within `GX_MODULE_ABI_VERSION`).
- **Cost of the module-capable exe** (Release, with the list): payload
  **3,935,003** (+21,536 B over the default Release 3,913,467) — the listed
  symbols become GC roots. The default profile (no `GX_USE_ANGELSCRIPT`) is
  unchanged at 3,913,467; the module profile is exe + `gx_shell.so`.
- **Gate**: `make check-scripting` runs the 37-probe shell gate against the
  module (`GX_MODULE`), including export-list drift, explicit/missing module
  handling, and the module-less `--script` warning.
- **DevCube lab module** (`gx_devcube`, `build/labs/gx_devcube.so`): the
  engine avatar draws a native-resolution overlay (green wireframe cube,
  pink 3:2 Lissajous, three RGB orbs) after the scene/renderer/GUI frame.
  Its phase is quantized from `CLOCK_MONOTONIC` to 60 Hz, so 20/30/60 fps
  rendering shows the same wall-clock phase. It is built by default but
  never auto-loaded: only `--module`/`GX_MODULE` opt in. `module_exports.py
  --check` treats the list as append-only (extra lab symbols are allowed).
  **Gate**: `make devcube-check` runs the 60 Hz phase test and loads the
  module explicitly for a native `--gfx` capture, asserting the green cube,
  pink Lissajous and red/blue orb palette; it is also part of `check-fast`.

## Ship profile (audited 2026-10-21 — AUD-10)

The **default executable is the shipped engine**. `gx_shell` and `gx_devcube`
are opt-in lab clients: they build outside the release tree
(`build/`, `build/labs/`, isolated module builds) and are never auto-loaded.
`--module <path>` or `GX_MODULE=<path>` is the only load path; the `modules/`
auto-scan only sees files physically placed there by an operator, and the lab
output directories are deliberately elsewhere.

Packaging policy:

- Source and release exports contain no `build/` tree and no shared module
  binary (`.so`, `.dylib`, `.dll`). `build/labs/` and `build/scripting/` are
  therefore never packaged.
- `modules/module_exports.txt` **is** engine source/ABI metadata, not a module
  binary; it stays in the export and remains append-only within
  `GX_MODULE_ABI_VERSION`.
- `scripts/module_exports.py --check` verifies the export list; a lab module
  becoming an auto-loaded startup dependency is a packaging/ABI failure, not a
  lab convenience.

Open decision (main-owned): whether a future release ever ships a prebuilt
module `.so` beside the executable. Until that decision, the source package is
exe + engine source only.

The sketch below is the research record that led here.

## Sketch: the shell as the first module

- `GX_USE_ANGELSCRIPT=ON` stops being a second *binary* build; it becomes a
  `gx_shell.so` target (AngelScript SDK + `src/scripting*` + `src/console` +
  debugger panel + the tour APIs — everything already behind the same guard).
- The default exe gains: `dlfcn` loader + registry (~200–300 lines), the
  curated export list, `--shell <path>` and/or auto-load from `modules/`.
- The module links against the exe's exports (Route A) or receives the table
  (Route B); the scripting entry points (`gX_Scripting_*`) are already a thin
  C surface, which makes either route mechanical.
- Budget: the measured shell payload (1,382,976 B) + BSS (235,072 B) becomes
  the module's budget line; the default exe stays at today's 3.9 MB with no
  cap change.

## Open questions / risks

- **ABI discipline**: Route A makes every exported symbol a promise; Route B
  costs table maintenance. Either way a `GX_MODULE_ABI_VERSION` and a
  documented freeze/semver policy must exist before the second module.
- **Global-state coupling**: modules will lean on engine globals; the table
  should expose a curated set of accessors, not the kitchen sink, or the
  ABI ossifies the structs.
- **Provenance/licensing**: shipped modules need `license/NOTICE.md` entries
  and the packaging story (source tarball today; module binaries would be
  build outputs, not repo content).
- **Temptation**: modules used to shrink the measured exe while shipping the
  same bytes would hollow out the gate's meaning; the shipped-set framing is
  the guard.

## Recommendation

Treat dynamic modules as a **capability-packaging mechanism** (profiles,
lab experiments, one-binary ergonomics), not as a lean-gate shortcut. If
pursued, start with Route A + a single `gx_shell.so` client and extend the
budget tooling to per-module lines with a shipped-set total. If the goal is
only gate headroom, prefer the profile decision — it is cheaper and does not
add an ABI.
