# gX — Backend Study (L0: what raylib-only means, and what a second target costs)

<!-- //ai: distinguish read-only gameX archive citations from verified gX paths; no audit exemption. -->
Date: 2026-10-13. Status: **study only — no implementation, no build or
runtime change.** Goal: answer the gameX-archive question
(the RHI/backend studies in the read-only gameX archive) for gX: which
backends could exist under gX's constitution (raylib-only, frozen raylib
submodule, lean gate), what they would actually cost against the code that
exists, and in what order they should be considered — if demand ever
appears. Sources: this tree's `src/rhi/`, `src/backend/raylib/`,
`src/wrappers/`, `src/common/shader_compiler.c` and
`src/common/shader_compiler.h`, `src/renderer/gfx/gl_level.c` and
`src/renderer/gfx/gl_level.h`, `external/raylib` (6.0-58-gcf9f27db),
`patches/raylib-*.patch`, the shader corpus and
`scripts/shader_compile_check.py` (`--spirv`), `scripts/gl_level_check.py`,
`scripts/gl_legacy_native_check.py`, `scripts/gl_state_audit.py`,
`docs/tiers.md`, `docs/hardware.md`, `docs/lean_reassessment.md`.
External gameX archive sources are listed with their absolute paths in
section 7; they are reference documents/tools, not files shipped by gX.

## 0. Local state: what a "backend" is in gX today

gX removed multi-backend support in v0.1.0 (CHANGELOG: "Multi-backend
support (GLFW/SDL/Vulkan/N64/PS1/PS2/Windows stay in gameX as engineering
exercises)"). What remains is the L0 substitution frontier from
`docs/tiers.md`: `external/raylib` + `src/backend/raylib/` (raylib-typed
implementations), `src/wrappers/` (backend-agnostic API), `src/rhi/` (thin
GL surface on raylib's bundled glad).

Measured at study time:

| Surface | Fact |
|---|---|
| raylib | submodule `6.0-58-gcf9f27db`; GLFW window/input + miniaudio inside raylib; `GRAPHICS_API_OPENGL_33` default; `GX_COMPUTE=1` rebuilds raylib `GRAPHICS_API_OPENGL_43` (`CMakeLists.txt:12-18`) |
| RHI | `src/rhi/`: 18 `.c` + 20 headers, 6,028 lines, 222 unique `gX_RHI_*` names in the headers |
| GL leaks | ~98 direct `gl*()` call sites outside `src/rhi/` and `src/backend/`: `src/gui/widgetsdf.c` 45, `src/renderer/gfx/oracle_gpu.c` 22, `src/renderer/gfx/terrain.c` 12, `src/renderer/gfx/clusters.c` 10, remainder 9 (readbacks, TAAs, one `glUniformMatrix4fv`) |
| raylib includers | `src/backend/raylib/` (5 files), `wrappers/drawing.c`, `audio.c` + `audio/synth.c` + `audio/opx.c` + `audio/pad.c`, and the L2 tooling (`tools/imgui`, `tools/gfx\|physics\|textwriter`, `scripting/tour/*`) — the tools are the second real consumer of raylib headers |
| Shader corpus | 153 `.frag/.vert/.geom` (130/21/2) + 2 `.comp` + 8 `.glsl`; versioned: 146x `#version 330`, 1x `330 core`, 7x `430`; 16 files carry explicit `layout(location)`; no `layout(std140)` blocks |
| Shader dialects | compiler modes already shipped: production 330; desktop legacy 130/140/150 (`gX_Shader_Compiler_SetDesktopLegacyGLSL`); ES 300/310/320 es (`SetESMode`); Vulkan 450 + `gX_Shader_Compiler_ToVulkanDialect` (`src/common/shader_compiler.c:2191`) |
| Context knobs | `patches/raylib-gx-gl30-context.patch` (`GX_GL30/31/32_CONTEXT`, `GX_GL43_CONTEXT`); `patches/raylib-gx-legacy-default-shader.patch` (raylib default shader version swap for the legacy rungs); `GX_GL_LEVEL` 200..500 ladder (`gfx/gl_level.h`; "500 = Vulkan (spiritual), reserved") |
| Caps | `gX_RHI_Caps` is GL-independent and already names `isGLES`, `hasCompute`/`hasSSBO`/`hasBindless`/`hasTessellation`/...; `gX_GL_Features` mirrors them as selectable policy flags |
| Gates / lean | 4 MiB payload + 2.7 MB BSS caps (`.lean_budget.json`); configs are compile-time exclusive (kernel, compute, shell module); isolated build dirs under `build/<name>` |

The loaders are raylib's, and they are split: desktop builds include
`external/raylib/src/external/glad.h` (desktop GL 1.0-4.3; no
`GLAD_GL_ES_VERSION` symbols at all), while ES builds include
`external/raylib/src/external/glad_gles2.h` (ES 2.0 core + 170
extensions; raylib's ES3 define aliases ES2 and resolves the ES entry
points through OES/EXT names, e.g. `glGenVertexArrays` is loaded from
`glGenVertexArraysOES`, `rlgl.h:2530`). raylib's CMake offers
`OPENGL_VERSION` `OFF;4.3;3.3;2.1;1.1;ES 2.0;ES 3.0;Software` and
`PLATFORM` `Desktop;Web;WebRGFW;Android;Raspberry Pi;DRM;SDL;RGFW;Memory`.

## 1. Already certified (no work)

| Target | Evidence | Notes |
|---|---|---|
| Desktop GL 3.3 | default build; `make check`, feature matrix, reflection/bloom gates | the supported pair |
| Desktop GL 4.3 + compute/SSBO | `GX_COMPUTE=1` (`build/compute`); `make compute-feature-check`, `make particle-velocity-check` | opt-in profile (64-light ceiling) |
| Native GL 3.0/3.1/3.2 contexts | `scripts/gl_legacy_native_check.py` (Mesa + context patch; capability pins + denies-absent/visible fixtures) | the "educational ladder", not a product promise |
| Hosted GLSL 130 dialect | `scripts/glsl130_check.py` | compatibility probe of the 130 rewrite |
| GL policy ladder | `scripts/gl_level_check.py` (`build/gl-level33\|43`) | policy rows are not native-context certification |
| GPU-free raster CI | Mesa llvmpipe (e.g. `gl_legacy_native_check` runs `GALLIUM_DRIVER=llvmpipe`); hidden-window audit renders; Xvfb for release capture checks | software GL pipelines already live |

## 2. Candidate matrix

Effort figures scale from the gameX archive's measured work (its RHI candidate
matrix and GLFW staged raylib-removal study, cited in section 7), not from
estimates invented here.

| Candidate | What maps today | What breaks in gX | Cost | Value / verdict |
|---|---|---|---|---|
| Platform layer inside raylib (SDL3, RGFW, DRM, RPi, Web/Android) | raylib CMake `PLATFORM` selection; engine code keeps calling raylib | the gX context patch targets `rcore_desktop_glfw.c` only; Linux+GLFW is the only exercised combination; gates assume a display | build matrix + one patch port per platform layer | only for a named OS/platform; cheapest true backend — demand-gated |
| GLES 2 / 3.0 | raylib `OPENGL_VERSION="ES 3.0"`; ES shader mode exists; `isGLES` caps branches already in the RHI | **loader**: no core ES3 symbol set in either bundled glad (ES2 core + extensions only), so `glTexStorage2D`/`glDrawBuffers`/`glGenVertexArrays`-style calls need a new loader or an OES/EXT mapping; **no ES 3.1** (compute/SSBO impossible with raylib as-is; bindless/tessellation/cube arrays never); MS textures ≥ES 3.1; float HDR targets need `EXT_color_buffer_float`; raylib's ES default shader/batch path is untested by any gX gate | medium (loader + caps completion + gates), not the flag flip gameX had | mobile / Pi / web only under a named target; no recorded demand today |
| Null/headless RHI | `gX_RHI_Caps` already specifies a zeroed non-GL path; tests (`--test-only`) run windowless; audits render on llvmpipe | 222 RHI entry points need stubs; window/input/audio still raylib; neither path unlocks much the current llvmpipe route does not | 1-2 days (gameX estimate) | logic-only CI already exists; **no** |
| llvmpipe / EGL-surfaceless | already the CI software path | an EGL-surfaceless context would drop the X requirement, but no gate needs it (Xvfb covers headless CI) | half a day | keep current; **no new backend** |
| Vulkan | retained dialect machinery: `SetVulkanMode` + `ToVulkanDialect`, `--vk-dump-shader` handler in `src/main.c`, and the `shader_compile_check.py --spirv` corpus gate | measured corpus result (this study, 2026-10-13): **1/153 files portable as-is**; failures: location 111, uniform-block 31, binding 7, version 3. The dialect rewrite is the prerequisite, and the CLI handlers are **unreachable** (not registered in `src/main_parse.c`; recorded in `docs/lean_reassessment.md`). Structurally: rlgl is load-bearing (batch, matrix stack, default shader, wrappers/drawing/GUI), ~220 RHI entry points + ~98 GL leak sites must be re-routed; gameX's Stage 1 needed deferred recording, per-draw descriptor rings, deferred FBO deletes (5-10K lines) | high | parked; no demand signal; the only live action is the dump-shader CLI decision (below) |
| WebGPU / wgpu | — | GLSL→WGSL (naga) + a new dependency; Vulkan-class RHI work | high | only if web export becomes a goal; parked |
| Software rasterizer as oracle | gX already runs CPU-side twins/fixtures (`probe_box_contract`, oracle CPU/GPU paths, gxmath CPU twins) and llvmpipe | full CPU shader rasterizer is not justified while llvmpipe is free | high | not applicable beyond the current fixtures |
| Retro consoles | gameX mini-ports only (N64/PS1/PS2, separate scenes/renderers) | contradicts gX scope (`docs/tooling_scope.md`); no RHI mapping | weeks | gameX-only engineering exercises; **no** |

Raylib's own `Memory` platform + rlsw software rasterizer (GL 1.1-style,
no shaders/FBO) cannot host gX's pipeline; do not mistake it for a headless
renderer — llvmpipe remains the GPU-free path.

## 3. What any second target would structurally need

Independent of the candidate:

1. **Loader switch**: `rhi_gl.h` is the single GL include point; a target
   needs its loader header selected there (`glad.h` vs `glad_gles2.h` vs a
   custom loader) and calls gated by caps.
2. **GL constants**: the public RHI already speaks `gX_RHI_*` enums; the
   remaining ~98 direct `gl*()` sites (widget SDF, GPU oracle, terrain,
   cluster compute, readbacks) must move behind the RHI or gain target
   equivalents.
3. **Capability gating**: exists (`gX_RHI_Caps`, `gX_GL_Features`,
   `gX_GL_SetLevel`); the `GX_GL_LEVEL` ladder already models degraded
   contexts.
4. **Shader dialects**: source rewriting exists for legacy/ES; a per-target
   binary path (SPIR-V/WGSL) would be new.
5. **Window/input/audio membrane**: `src/wrappers/` is the seam; a
   non-raylib platform layer replaces raylib's window/input/audio. gameX's
   GLFW backend measured ~3,800 lines of raylib modules replaced
   (draw2d/matrix/batch/text) — the equivalent trade here is raylib out,
   GLFW+glad+miniaudio+stb in, plus gates.
6. **Lean policy**: a backend swap is compile-time exclusive and must pass
   the same payload/BSS caps and its own gate; multi-backend means a bigger
   gate matrix, not more runtime code.

## 4. Drift found while writing (documentation only; no code changed)

- `src/rhi/rhi_gl.h:14` says raylib's bundled `glad.h` is "GL core 4.3 +
  GLES 3.2". Actual: `glad.h` is desktop GL 1.0-4.3 with no ES symbols; ES
  uses the separate `glad_gles2.h` (ES 2.0 + extensions). The GLES half is
  wrong and directly affects any ES cost estimate.
- `src/rhi/rhi_buf.c:25` says software-VAO emulation "must be implemented
  at GLES port time". ES 3.0 has core VAOs (and `glad_gles2.h` carries the
  OES entry points); the note is over-conservative for an ES3 target.
- The ES/Vulkan shader dump handlers in `src/main.c` are retained but
  unreachable (not registered in `src/main_parse.c`) — already recorded in
  `docs/lean_reassessment.md`; it is the one live CLI decision this study
  inherits (restore with a small gate, or delete).
- `docs/hardware.md` tier notes describe ES 3.0 "best-effort"; the loader
  and compute gaps above are the concrete reason it stays profile-only.

## 5. Recommended order (if demand appears)

1. **Nothing now**: keep desktop GL 3.3 + the GL 4.3 compute profile as the
   supported pair. This study changes no behavior.
2. **Decide the retained-but-unreachable shader dump flags**
   (`--es-dump-shader`, `--vk-dump-shader`) — a `lean_reassessment.md`
   action item.
3. **Platform layer inside raylib** (SDL3/RGFW/DRM/RPi/web) only for a
   named target; port the context patch per platform and extend the gates.
4. **GLES** only for a named device class; budget loader work first; no
   compute above ES 3.0 with raylib as-is.
5. **Vulkan/WebGPU**: parked behind the release candidate and a recorded
   demand; use the gameX Stage 0/1 notes as the reference if it starts.
6. **Null RHI / consoles / soft renderer**: no gX use case while llvmpipe
   CI and the existing oracle/fixtures hold.

Lane result (2026-10-18): `backend-sdl` built gX with raylib
`PLATFORM=SDL` from the same source (no gX CMake change needed; the raylib
cache var is enough) and captured `--gfx` pixel-identically to the GLFW
build in same-batch runs. The `--title` background diverges slightly by
platform (11-23% pixels >2, bounded; attribution in progress), and cross-batch
captures have a small wall-clock noise floor. Full evidence, platform notes
(`FLAG_WINDOW_UNFOCUSED` unsupported; context ladder is GLFW-only) and the
opt-in A/B gate: `scripts/backend_sdl_check.py`.

## 6. Open probes (not run for this note)

- `python3 scripts/gl_level_check.py` / `scripts/gl_legacy_native_check.py`
  — fresh context-ladder evidence (display required).
- `make compute-feature-check` / `make particle-velocity-check` — GL 4.3
  parity pins.
- `python3 scripts/shader_compile_check.py --spirv` — the corpus gate used
  in section 2 (informational, never fails the build).
- A GLES feasibility spike would add a raylib `OPENGL_VERSION="ES 3.0"`
  build under `build/gles-probe/` and a loader decision before any engine
  edit.

## 7. See also

- `docs/tiers.md` (L0 definition), `docs/architecture.md` (dependency
  order, dropped subsystems), `docs/hardware.md` (runtime feature tiers),
  `docs/lean_gate.md` + `docs/lean_reassessment.md` (size policy, the
  retained dump flags), `docs/dynamic_modules.md` (the sibling
  measured-no-code-change study).
- External, read-only gameX archive (local reference checkout; not required
  for gX builds and not distributed with gX):
  - `<gameX-archive>/docs/rhi_backends.md` — candidate matrix and
    recommended order.
  - `<gameX-archive>/docs/backend_raylib_vs_glfw.md` — what raylib
    hides and when it stops being enough.
  - `<gameX-archive>/docs/backend_glfw.md` and
    `<gameX-archive>/docs/backend_glfw_audit.md` — staged raylib
    removal and its audit.
  - `<gameX-archive>/docs/backend_vulkan.md` — deferred-recording
    model and known issues.
  - `<gameX-archive>/scripts/cross_backend_compare.py` —
    cross-backend pixel comparison.
