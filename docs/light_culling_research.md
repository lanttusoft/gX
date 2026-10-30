# Light Culling Upgrade — Research Note (v0.5.0 prep)

Date: 2026-09-01. Goal: lift the Forward+ 32-slot uniform ceiling
(architecture.md roadmap frontier) with compute shader + SSBO light
culling. Sources: in-tree raylib 6.0-58 (rlgl.h/rlgl.c,
examples/shaders/shaders_rlgl_compute.c), raylib CMake (CMakeOptions.txt,
LibraryConfigurations.cmake), odin-raylib compute example (4.3 build
requirement + failure warnings), Divide Framework lightCull.glsl, Morsy
"Tiled Light Culling in Vulkan" (2026-04), raylib-rs mirror of the same
example, Harada et al. (Eurographics 2012, "A 2.5D Culling for Forward+").

## 1. Compute loading — rlgl HAS it, raylib.h does not

raylib 6.0 exposes compute only at the rlgl level (no
`LoadComputeShaderProgram` in raylib.h — confirmed absent):

- `rlLoadShader(code, RL_COMPUTE_SHADER)` — compile a compute shader
  (rlgl.h:775, RL_COMPUTE_SHADER = 0x91B9)
- `rlLoadShaderProgramCompute(csId)` — link the compute program
  (rlgl.h:778; rlgl.c:4420, gated on `GRAPHICS_API_OPENGL_43`)
- `rlEnableShader(program)` → `rlBindShaderBuffer(ssbo, index)` →
  `rlComputeShaderDispatch(gx, gy, gz)` → `rlDisableShader()`
  (rlgl.h:790; example uses exactly this sequence)
- SSBOs: `rlLoadShaderBuffer(size, data, usageHint)` /
  `rlUpdateShaderBuffer(id, data, size, offset)` /
  `rlBindShaderBuffer(id, index)` / `rlReadShaderBuffer` /
  `rlCopyShaderBuffer` / `rlGetShaderBufferSize` (rlgl.h:793-799).
  Example uses `RL_DYNAMIC_COPY` + `nullptr` initial data.

Key consequences for gX:

- **Build option is `OPENGL_VERSION=4.3`** (cmake enum option, CMakeOptions.txt;
  maps to GRAPHICS_API_OPENGL_43 in LibraryConfigurations.cmake). Makefile
  variant: `make PLATFORM=PLATFORM_DESKTOP GRAPHICS=GRAPHICS_API_OPENGL_43`.
  When raylib is NOT built 4.3 the failure mode is explicit log warnings, not
  a crash: `SHADER: Compute shaders not enabled. Define GRAPHICS_API_OPENGL_43`
  and `SSBO: SSBO not enabled. Define GRAPHICS_API_OPENGL_43` (confirmed by the
  odin-raylib compute example against default-built raylib 6).

- **No custom compute loader needed** — gX already calls rlgl directly
  (e.g. rlEnableShader in the SetValue wrappers). The official
  `shaders_rlgl_compute` example (Game of Life) is the canonical
  pattern; the C source ships in external/raylib/examples.
- **GL 4.3 is a BUILD-TIME choice**: raylib's CMake selects
  GRAPHICS_API_OPENGL_43 only when `OPENGL_VERSION=4.3` is passed
  (LibraryConfigurations.cmake:200); gX currently builds the default
  3.3 (fresh-extract configure prints
  `GRAPHICS=GRAPHICS_API_OPENGL_33`). Without 4.3, compute + SSBO
  calls compile OUT of rlgl (the #if guard around
  rlLoadShaderProgramCompute). The milestone needs a build flag
  (e.g. `-DOPENGL_VERSION=4.3`), with the 3.3 build remaining the
  default fallback (32-slot uniform path + GX_NO_CLUSTER intact).
- **GLSL 430 for everything touching SSBOs**: SSBO access in
  fragment shaders also needs #version 430/GL 4.3 — so even the
  "lights in an SSBO, CPU-culled grid" half-step is 4.3-only. The
  shader compiler's version-header injection (currently 330) must
  become per-program; hot-reload Watch_Ex currently handles V+F only
  — compute programs need a separate load path (raw
  rlLoadShaderProgramCompute, no hot-reload, or extend the watch).

## 2. Forward+ design (Harada 2012 + gX constraints)

Canonical pipeline: depth prepass → light culling (compute) → final
shading. Culling approaches:

- **Gather**: one thread group per tile; build tile frustum from
  screen-space tile range + min/max depth; cooperative load of lights
  into shared memory, overlap tests, TLS accumulation via local
  atomics, flush to global. Simple + effective for moderate light
  counts — the right fit here.
- **Scatter**: per-light threads write (light, tile) pairs, radix-sort,
  per-tile offsets. Overkill for gX light counts (the demo was 3072
  lights; gX scenes use dozens).

Paper's memory-traffic analysis:
Forward+ beats deferred whenever average lights/tile < ~135 (point lights).
Harada's own demo used **8×8 tiles** (tile size left open in the paper; gX
already uses 16px tiles — keep 16×16 so the grid layout and the findLSB walk
are unchanged).

Per-tile depth bounds: loose full-frustum planes cause many false-positive
light intersections (long frusta at model silhouettes — Harada notes this).
Canonical fix (Morsy 2026, Vulkan tiled culling): a **tile min/max depth
reduction kernel** — one workgroup per tile, local array of the tile's
texelFetched depths, min/max reduce, write per-tile bounds; the cull pass
then tests the light sphere against the 4 side planes + min/max depth slabs.
Reference gather kernels to crib from:
- Divide Framework `lightCull.glsl` (cluster culling): shared-memory light
  batches, `testSphereAABB`/`sqDistPointAABB`, `atomicAdd` global index-list
  offsets, light grid write.
- Morsy 2026: sphere-vs-frustum test = 4 plane dots + depth-slab compare;
  depth slicing fills a per-tile bitmask via `atomicOr(tilesBitMask[tile],
  1u << cell)` — the same bitmask idiom as gX's grid.
- Light packing: store `vec4(position.xyz, radius)` — avoids std430 vec3
  padding (16-byte alignment) entirely; radius rides in `.w`. Forward+ beats deferred whenever
average lights/tile < ~135 (point lights); tile size was left
open (16×16 is the community default; gX already uses 16px tiles).

gX current state (architecture.md v0.3.2): CPU-culled clustering with
`grid[tile]` = R32UI bitmask of upload-slot indices (≤ 32 slots = the
uniform cap), radius from attenuation quadratic root, manual
row-major world→view→clip projection, standard.frag `applyLight(i,…)`
walking findLSB over the mask. **The bitmask-grid interface survives**
— the upgrade replaces the slot space, not the grid:

- Lights move into an SSBO (std430 vec4/struct array — no 32-vec4
  uniform cap). Uniform-capped path stays as the 3.3 fallback.
- Compute culling writes the same per-tile bitmask grid (imageStore
  into the R32UI texture, or a grid SSBO); tile frustum from the
  depth prepass min/max (raylib has no depth-downsample helper —
  small manual reduce kernel or CPU min/max from a 1×1 depth
  texture; CPU readback of a 1px depth is one glReadPixels, fine at
  this scale — keep the CPU-frustum option as the non-compute 4.3
  half-step).
- DebugDisplay can surface `rlReadShaderBuffer` stats (light count /
  per-tile occupancy) once the grid is GPU-side.

## 3. Binding & environment gotchas

- `rlBindShaderBuffer(id, index)` binds to GL_SHADER_STORAGE_BUFFER
  binding points (glBindBufferBase) — a namespace separate from
  texture units, so **no collision with the texture-slot table**
  (units 8/10/11, all ≤ 31). Keep SSBO binding points low (0-3) and
  document them in the constants table.
- **`rlBindImageTexture(id, index, format, readonly)` EXISTS in rlgl**
  (rlgl.h:802, glBindImageTexture) — the compute kernel can imageStore /
  imageAtomicOr straight into the EXISTING R32UI bitmask grid texture; the
  grid does not have to move to an SSBO. SSBO alternative (atomicOr on a
  u32 buffer) also works — pick by whether the frag path prefers texelFetch.
- Depth readback: `rlReadTexturePixels` (rlgl.h:762) can pull a 1×1 depth
  texture per frame for the CPU-frustum debug fallback.
- Order matters: `rlEnableShader(computeProgram)` BEFORE
  binds/dispatch (same rlgl trap as SetValueTextureSlot).
- lavapipe (GL 4.5) supports GL_ARB_compute_shader → headless
  verification works; llvmpipe SSBO/compute is slow but correct.
- ExtSupported.computeShader (rlgl.h:1130) is queried at init — gate
  the compute path on it at runtime as well as at build time.
- raylib's `--graphics=4.3` GLAD profile also changes the default
  shader version assumptions; the engine's own shaders declare their
  own `#version` via the compiler injection — must stay 330 for the
  fallback path.

## 4. Recommended implementation plan (v0.5.0)

1. Build: raylib `-DOPENGL_VERSION=4.3` via an opt-in flag
   (`GX_COMPUTE=1`); default build unchanged (3.3).
2. Light data → SSBO (std430), upload path in clusters.c; standard.frag
   reads it with `#version 430` when the macro is on (uniform path
   otherwise — one shader source, #ifdef GLSL).
3. Compute culling kernel (gather, 16×16, tile frustum) writing the
   existing bitmask grid via imageStore; non-compute 4.3 half-step =
   keep CPU culling but mask now indexes SSBO slots (bitmask-grid
   interface survives).
4. Runtime gate on ExtSupported.computeShader; GX_NO_CLUSTER fallback
   stays. DebugDisplay occupancy stats via rlReadShaderBuffer.
5. Parity: bitmask ordering must stay ascending (findLSB order) to
   keep legacy lighting parity (0 px diff vs the 32-slot path).

## 5. Open questions (resolved)

- Depth min/max: **GPU tile-min/max reduction kernel** (Morsy pattern) is the
  canonical choice; 1×1 CPU readback via rlReadTexturePixels stays as the
  debug/fallback path. Verify both on lavapipe.
- Workgroup size: **16×16** — matches the existing 16px tile grid, so the
  bitmask-grid interface and the frag-side findLSB walk are byte-identical;
  no layout churn for the parity check.
- SSBO + hot-reload: compute programs bypass the Watch_Ex V+F path —
  accept load-once for v0.5.0 (note in gfx_pipeline docs).
## 10. UBO probe — dead end (v0.5.1, 2026-09-01)

Attempted to restore full GPU projection by passing view/proj to the
compute kernel as a std140 uniform block (binding 2, uploaded transposed
row-major->column-major). Result: **the Cg-based GLSL translator drops
ANY kernel that references a uniform block** — verified by grid readback
(glGetTexImage RG32UI):

- UBO matrix x SSBO-derived vector: 0 tiles written
- UBO matrix x constant vector: 0 tiles written
- scalar UBO element read (`m.view[0][0]`): 0 tiles written

The kernel compiles ("compute compile OK") and dispatches execute (the
shipped no-UBO kernel writes and passes parity), but any uniform-block
reference makes the dispatch a silent no-op. Conclusion: uniform
buffers in compute shaders are unsupported by this translator. The
shipped design (CPU projection + GPU mask assembly, no matrices in the
kernel) is the only working path on this hardware.

## 11. Compute validation pin (v0.5.2, 2026-09-18)

`make compute-feature-check` is an opt-in GL4.3 gate. It builds the compute
variant, runs the showcase with 64 total lights (60 injected), and enables
`GX_T_CLUSTER_PARITY=1`. After dispatch, the GPU RG32UI grid is read back and
compared bit-for-bit with the CPU reference; a nonzero tile or word mismatch
fails the target. The projected-light SSBO is heap-slot indexed, matching the
fragment light SSBO and preserving holes as inactive entries.
