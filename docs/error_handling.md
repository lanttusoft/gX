# gX error handling

Policy and tools for failure paths. GL/RHI error *recipes* live in
[`error_hints.md`](error_hints.md); this file covers return codes, logging
and the no-silent-failure contract.

## Contract

1. **Status functions return 0 = success** (or `gX_I1` truthy = success).
   Data queries return data — document them as such so the audit can tell
   them apart.
2. **Never return success from a no-op.** A function that cannot do its job
   must log and return failure. An intentional no-op is allowlisted in
   `scripts/error_handling_audit.py` with a reason (and normally carries an
   `//opencode:` comment explaining why).
3. **Every failed operation logs** at `gX_LOG_ERROR` through the module
   macro (`gX_TraceLog_RENDERER`, `gX_TraceLog_BACKGROUND`, ...). Recoverable
   failures log NOTICE and the caller keeps running.
4. **Check what can fail**: allocations (`gX_Malloc`/`gX_Calloc`/`malloc`),
   file loads (`*_Load`, `LoadImage`, `LoadDirectoryFilesEx`), shader
   compiles, JSON parses, GL object creation.
5. **Legacy void wrappers** delegate to a status `_New` entry point; the
   `_New` call owns the logging, so a discarded wrapper result is
   informational in the audit (not a failure).

## Codes

`src/common/error.h` defines the boundary codes. Failures set the last-error
slot (`gX_Error_Set` / `GX_ERROR_SET`), which stores code, message and source
position and traces one machine-readable line:

```
GX_ERROR: preset not found: resources/tools/nope.json code=1 name=not-found at src/tools/gfx/gfx_editor.cpp:1838
```

| Code | Name | Typical cause |
|---|---|---|
| 0 | none | cleared |
| 1 | not-found | missing file or path |
| 2 | io | read/write failure |
| 3 | parse | malformed input (JSON/GLB) |
| 4 | schema | schema or field mismatch |
| 5 | oom | allocation failure |
| 6 | invalid | bad argument |
| 7 | unsupported | capability/format not supported |
| 8 | gl | GL/RHI failure |
| 9 | state | operation invalid in the current state |

Status functions keep their 0/1 returns and set the slot on failure; boundary
APIs may return `gX_Error` directly where callers branch on the reason. Gates
assert `name=` (stable) rather than prose — `error_check` pins
`name=not-found` and `name=parse` today.

## Tools

- `make error-handling-check` — static scan: silent stubs (strict, must be
  allowlisted with a reason) plus discarded status results (informational).
  It also runs inside `make check`.
- [`error_hints.md`](error_hints.md) — runtime GL/RHI error → recipe table
  (`| HINT: ...` lines).
- Headless error paths: per-gate failure assertions (e.g.
  `gfx-raymarch-check` fails loudly when the preset schema cannot save or
  load) plus the bad-input runs in `scripts/error_check.py`.

## History

- The raymarch preset schema shipped as no-op stubs returning success
  ("saved" while doing nothing) until the revival replaced them — that class
  of bug is what the silent-stub scan exists for.
- The material-assets gate printed FAIL but exited 0 (`main()` without
  `sys.exit`), so a real failure could pass the summary — a runtime analogue
  of the same class.
- `gX_Audio_Music_SetVolume` ignored its argument and returned success; it
  now clamps and applies the volume.
