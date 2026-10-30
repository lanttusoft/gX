# gX debugging

Five layers, each with a gate:

1. **Recoverable failures** — boundary error codes and traces
   ([`error_handling.md`](error_handling.md)); GL/RHI error recipes in
   [`error_hints.md`](error_hints.md); sentinel colors in
   [`debug_colors.md`](debug_colors.md). `GX_ASSERT(cond)` covers states that
   must never happen: it logs the expression/file/line/function, sets the
   error slot (`name=assert`) and, with `GX_ASSERT_FATAL=1`, aborts so the
   crash handler captures the full trace. Boundary failures keep using
   `GX_ERROR_SET` with a return. `GX_DEBUG_ASSERT_TEST=<frame>` fires a
   deliberate failing assert for the gate.
2. **Crashes** — `src/common/debug.c`: SIGSEGV/SIGABRT/SIGFPE/SIGBUS/SIGILL
   dump the register file (x86_64 RIP/RSP/RBP, RAX-R15, EFLAGS), the recent
   log lines (`LOG TAIL`), a stack with frames and an
   `addr2line -e build/gX_build -f -C <addr>` recipe, then exit with
   `128 + signal`. The tail comes from the crash-context ring
   (`GX_LOG_RING=<N>`, default 32, 0 disables; heap-backed, signal-safe
   `dprintf` dump). `make debug-check` pins the trace and the tail.
3. **Variable traces** — watches sample addresses per frame and trace changes
   as `GX_WATCH: name=value changed`:
   - headless: `GX_DEBUG_WATCH="name=0xADDR:type,..."` (types
     f32/i32/u32/u64/v3/color; addresses via `nm build/gX_build | grep sym`),
     `GX_DEBUG_WATCH_FRAMES=N` samples every N frames. Append `:OP VALUE` to a
     scalar watch (e.g. `frameCount=0xADDR:u64:>30`) and it logs only the
     rising edge — `GX_WATCH: name=value crossed >30` — instead of every
     change; ops are `> >= < <= == !=`;
   - editor: Scene panel -> Watches (add frame count, live values, clear);
   - `GX_DEBUG_WATCH_TEST=1` registers `frameCount` for the gate.
4. **Frame timing** — the profiler: `GX_PROFILE_DUMP=<N>` traces one line per
   phase/zone, and the Scene panel's Profiler table shows the last frame.
   `GX_PROFILE_TRACE=<path>` records every zone instance into a heap ring and
   writes a Chrome trace JSON at shutdown (open it in Perfetto or speedscope);
   `GX_PROFILE_TRACE_EVENTS=N` overrides the ring capacity (default 65536,
   oldest events overwritten). `make profile-check` pins the dump and the
   trace export. `GX_FRAME_WATCHDOG_MS=<ms>` flags frames over the budget:
   the overrun logs with a zone dump and the log tail (rate-limited to one
   dump per 60 frames), and `GX_FRAME_WATCHDOG_ABORT=1` aborts on the first
   overrun so the crash handler captures everything.

5. **Memory** — `GX_MEM_DEBUG=1` dumps the allocator accounting after the
   unload (`GX_MEM: live=… bytes=… peak=… allocs=… frees=… leaks=…`) plus the
   still-live allocations as a leak report. Every leak carries its allocation
   site: `symbol+0xoff` via `dladdr` for exported callers, or a base-relative
   `+0xoff` (the binary is PIE) that resolves with
   `addr2line -e build/gX_build -f -C 0xOFF`. Scope: the
   `gX_Malloc/Calloc/Realloc/Free` path — raylib-internal allocations and the
   modules that still call libc `malloc` directly are not tracked. The first
   run reports ~28 live allocations (~12 KB) at exit; `make debug-check` pins
   that the totals balance (`allocs − frees == live`), the peaks dominate, and
   the leak count stays under 64.

## Logging

`src/common/log.c` carries the levels (TRACE…ERROR) with per-module overrides
(`gX_LogState.moduleLevels`), ANSI details, the backend rerouting and the
on-screen `gX_Msg` feed. Env hooks:

- `GX_LOG_FILE=<path>` mirrors every emitted line to a plain transcript (no
  ANSI escapes, line-buffered so a crash leaves the tail on disk);
  `GX_LOG_CONSOLE=0` silences stdout (audits keep it by default).
- `GX_LOG_DEDUP=1` collapses consecutive identical messages into one line
  plus a `last message repeated N times` note. Opt-in on purpose: the audit
  gates parse every line, and the crash ring always keeps the raw truth.
- `GX_LOG_RING=<N>` keeps the crash-context tail (layer 2).
- `GX_LOG_MODULE_LEVELS="gfx=debug,gui=info"` overrides per-module levels at
  startup (names match the detail tags, case-insensitive; levels
  trace/trace2/trace3/debug/info/notice/warning/error). The UI can still
  change them afterwards.
- `GX_LOG_JSON=1` emits machine-readable lines
  (`{"ts","level","module","msg"}`) instead of the ANSI format; the audit
  gates keep the default. Raw `printf` output (math dumps) stays raw.

Trace sub-levels are **real filter levels**: `--logLevel 5` (trace) hides the
TRACE2 per-item detail, `--logLevel 4` shows it (`DRAW:` per model, `SHADOW:`
per point-light pass). They used to be mapped to TRACE before filtering, so
the depth was unreachable; TRACE2/3 had no callers, so making them real was
safe.

Controlled fault for gate coverage: `GX_DEBUG_CRASH_TEST=<frame>` triggers a
null write at that frame (never set it in normal runs).
