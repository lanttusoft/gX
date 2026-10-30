# gX error hints — every error carries its recipe

Errors log through three sites, all served by one table
(`gX_RHI_Hint_For` in `src/rhi/rhi_debug.c`): the GL debug callback,
the `RHI_SHADER` compile/link logs, and the `RHI_VALIDATE` warnings
(`GX_VALIDATE=1`). A matching error line ends with `| HINT: ...`.
First substring match wins; matching is case-sensitive.

//opencode: error-hint table (idea 2, last piece).

## How to use

1. Copy the `HINT:` text — it names the check, not just the symptom.
2. Reproduce headless: same flags + `--ai-screenshot-final`, read the log
   (no vision needed — hints are text).
3. Isolate with the hook/view in the last column, then fix.

## Table

| Error text (substring) | Meaning | HINT (recipe) | Isolate with |
|---|---|---|---|
| `feedback loop` | Texture sampled while attached to DRAW FBO | Unbind the unit (planar unit 19?) or blit instead | `GX_VALIDATE=1` draw labels |
| `incomplete` + `framebuffer` / `INCOMPLETE` | FBO missing/mismatched attachments | Depth-exclusive rule; layered vs 2D; equal sizes | `GX_VALIDATE=1` (reports color/depth ids) |
| `location` + `-1` | Bad uniform location | Hook-gated CacheLocs or optimized-out uniform; SetValues need `rlEnableShader` first | Shader error log (is the uniform active?) |
| `no program` / `program=0` | Draw with no program | Shader failed to load (NULL vertex path?) or BeginShader skipped | `--debug-view` (what pass is this?) |
| `not a valid GL program` | Deleted program still bound | Check hot-reload swap order | `GX_VALIDATE=1` |
| `out of memory` | GPU OOM | `nvidia-smi`; guest loads share nothing — batch/instance | `GX_FRAMES=30` + VRAM watch |
| `viewport` mismatch | Viewport vs FBO size | BeginTexture missed after an RT resize? | `--debug-view` the RT |
| `degenerate` viewport | 0-size viewport | Window minimized or RT sized 0? | log backtrace (which pass?) |
| `deleted` + `texture` / `invalid tex` | Use-after-delete on a unit | EvictUnits missed it? | `GX_VALIDATE=1` (names the unit) |
| `attrib` | VAO/attrib mismatch | Depth shader must declare all 6 standard attributes (raylib 6.0 layout) | `compile FAILED` grep |
| `nan` / `NaN` | NaN in pipeline | InjectNaN test left on? degenerate matrix (zero scale)? | `GX_COMPOSITE_TEX_STATS` (find the spike) |
| `pending GL error` | Earlier call left an error | Bisect with `GX_VALIDATE=1` labels (last label = nearest) | draw-label bisection |
| `undeclared identifier` (GLSL) | Unknown name in shader | Hot-reload `#include` search path? hook-gated uniform? vs/fs varying mismatch? | `GX_DUMP_SHADER=<substr>` |
| `undefined variable` / C1503 (NVIDIA) | Same as above, NVIDIA wording | Same as above | `GX_DUMP_SHADER=<substr>` |
| `no matching overloaded function` + `texture` | Sampler/kind mismatch | 2D vs 2DArray vs shadow sampler | `GX_DUMP_SHADER=<substr>` |
| `syntax error` (GLSL) | Parse failure | Dump preprocessed source; errors follow `#line` directives to originals | `GX_DUMP_SHADER=<substr>` |
| `varyings` / `link error` | vs/fs interface mismatch | Names AND types must match; dump it | `GX_DUMP_SHADER=<substr>` |
| `'main'` | Missing entry point | Emptied TEMP region or bad revert? | `git diff` the shader |
| `cannot open` / `preprocessing failed` | Shader file not found | Check `resources/shader` + GX search dirs (falls back to direct load) | log shows the path tried |
| `MODEL: Failed to load` | Model file missing/bad | Check path + format (pre-existing hint, kept) | `GX_ASSETS` for guest roots |

## Rules for adding entries

1. One line, actionable (name the check, not the symptom).
2. First match wins — put specific pairs before single words.
3. Only ERROR/WARNING lines get hints (perf chatter stays clean).
4. No magic colors here — that's `docs/debug_colors.md`.
