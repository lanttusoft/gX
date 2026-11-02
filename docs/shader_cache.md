# Shader program cache

<!-- //ai: agent-drafted contract for the GL program-binary cache. -->

The renderer compiles GLSL at load time. On a cold start the driver's
compile/link step dominates shader loading; the cache stores the linked
program binary and skips that step on later runs.

## Key

A cache entry is valid only for the exact inputs that produced it:

- the **processed** vertex and fragment sources (includes and defines are
  already expanded, so a shader edit or a quality/feature-define change
  produces a different key),
- the GL renderer and version strings (driver identity),
- the compiler mode (desktop GLSL, GLES, legacy GLSL level, Vulkan),
- the cache format version.

The key is a 64-bit FNV-1a hash. Editing a shader file, changing an include,
changing a define, updating the driver or bumping the cache version all
invalidate the entry; a stale binary cannot shadow new source.

## Storage

- Directory: `gx_shader_cache/` under the engine working directory.
- File: `<key>.bin`, with a small header (magic, format version, driver hash,
  key, program-binary format, length) before the driver blob.
- The directory is local scratch state, gitignored and never shipped.
- Entries are capped at 8 MiB. Corrupt, truncated, mismatched or
  link-failing entries are deleted and the shader is compiled normally.

## Fallback

The cache is an optimization, not a dependency. When the driver does not
advertise `GL_ARB_get_program_binary` (or the GLES equivalent), or the load
fails, the normal compile path runs and the frame is unchanged.

## Hooks

| Variable | Effect |
| --- | --- |
| `GX_NO_SHADER_CACHE=1` | Disable cache reads and writes for the run |
| `GX_SHADER_TIME=1` | Log `SHADER_TIME: <fragment> <ms> (compiled\|binary cache)` per shader |

The timing hook is diagnostic only; it is not part of the gate contract.

## Verification

`scripts/shader_cache_check.py` runs the same scene twice — once with the
cache disabled and once warm — and requires:

- at least one `binary cache` hit on the warm run,
- the cold and warm captures to match within the usual image tolerance,
- the warm total shader time to be lower than the cold total.

The check is advisory on the speed ratio (machine load varies) and strict on
the image and hit requirements. Run it with `make shader-cache-check`.

Reference measurement (GTX 1060 6 GB, editor preset, 46 shaders):

| Run | Shader time |
| --- | ---: |
| Cold (cache removed) | 7,328 ms |
| Warm (binary cache) | 139 ms |

The cold and warm captures matched exactly (mean absolute difference 0.00).
Most of the win is the driver's GLSL compile/link, which the cached program
binary skips entirely.
