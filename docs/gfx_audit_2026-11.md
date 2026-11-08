# Monthly GFX audit — November 2026

<!-- //ai: report-only monthly snapshot; documentation placement is subsequent to the audited run, not a renderer repair or gate change. -->

**Outcome at the frozen snapshot: not clean.** Admission and the uncached full
matrix pass, but a native AddressSanitizer witness confirms a
heap-buffer-overflow, and the separate full reflection suite fails twice.

> **Resolution update (2026-11-08):** this page records the frozen `89927f5`
> snapshot. The sphere-upload overflow was repaired in `48ec24a` (merged in
> `4e7c386`), and the reflection suite was calibrated in `38685a3` with
> deterministic settings isolation and a yaw-only non-planar fixture. The
> historical findings below remain the audit record; see
> [`ideas.md`](ideas.md) GX-I024 for the current repair status. "Not clean" and
> "fails" describe the audited snapshot, not current `main`.

## Audited identity and scope

- Actual branch: `feature/console-240p`.
- Frozen commit: `89927f5290031cc34643ae40dfea6cb63b681560`.
- The checkout advanced from `ecbaffeb7ed4` during preflight. The audit began
  only after a stable, clean snapshot of `89927f5` was verified.
- Proof pin: `v0.19.0-proofs`, `ff1a54676c87c3efd328e22491ceedc535492522`.
- Host: GTX 1060 6GB, NVIDIA 580.196.07, Linux 6.18.60-1-lts,
  Python 3.14.9, GCC 16.2.1, display `:1`. Default renderer builds request
  GL 3.3; selected gates also build their existing GL 4.3 profiles. Driver
  GL 4.6 capability is not certification of every context or platform.
- Admission started 2026-11-07 10:29:35 UTC. Final preservation check:
  11:31:57 UTC. No repository files, thresholds, fixtures, presets, budgets,
  source code, display modes or production defaults were edited by the audit
  run. This document and its index entry were added afterward for reporting.
- An earlier audit of a superseded checkout is excluded.

The engine snapshot, builds, generated fixtures and shader caches ran in a
copy-on-write sandbox at the original engine path. Proof and Elan writes were
also copy-on-write. `GX_NO_SETTINGS_SAVE=1`, hidden audit windows and
`AUDIT_CACHE=0` were retained.

Raw evidence remains outside the repository in the retained bundle
`console-tree_2gg3i4uv`. Evidence paths below are relative to that bundle,
**not** files beneath `docs/`. It contains the original sealed report,
commands, logs, captures, manifests and source snapshot. This repository-safe
copy omits machine-specific checkout locations and updates the storage
explanation; the findings and verdicts are unchanged. No raw evidence or
private machine paths are installed into the repository.

## Independent results

| Check | Result | Retained evidence |
| --- | --- | --- |
| `make check` | PASS | `tmp/opencode/monthly-admission/check.log` |
| `make gates-check` | PASS | `tmp/opencode/monthly-admission/gates-check.log` |
| `make version-check` | PASS | `tmp/opencode/monthly-admission/version-check.log` |
| `audit_summary.py --full --jobs 2 --no-cache` | PASS, **100/100** | `tmp/opencode/monthly-full/summary.json` |
| Full feature matrix within that summary | PASS, **29/29** | `tmp/monthly-first-full-matrix/report.json` |
| Feature-matrix report tests | PASS, 11 tests | `tmp/opencode/monthly-core/matrix-report-tests.log` |
| `make reflect-check` | **FAIL, F** | `tmp/opencode/monthly-core/reflect.log` |
| Unchanged `make reflect-check` repeat | **FAIL, F** | `tmp/opencode/monthly-core/reflect-repeat.log` |
| `make bloom-audit` | PASS, **22 checks / 10 families** | `tmp/opencode/monthly-core/bloom.log` |
| `python3 scripts/gl_state_audit.py` | PASS exit; 0 errors / **15 warnings** | `tmp/opencode/monthly-admission/gl-state.log` |
| `make gfx-obvious-audit` | PASS; 24 tests, 0 errors / 0 warnings | `tmp/opencode/monthly-admission/gfx-obvious.log` |
| External unchanged-backend ASan witness | **Heap-buffer-overflow reproduced** | `diagnostics/build/vbo-bounds/` |
| Review artifact generation | PASS; human grades remain UNREVIEWED | `tmp/opencode/visual-review/` |

The full summary ran in 2,141.91 seconds wall time. All 100 scheduled gate
attempts exited zero: no cached passes or gate-level retries. The 29-row
matrix also records no metric retries. Existing per-script capture/retry
behavior was not modified; this is not a claim that every GL launch needed
only one attempt. Full matrix and full reflection are different suites: the
100/100 result does not override the reflection failure.

## M11-01 — High: procedural-sphere VBO heap overflow

`src/backend/raylib/raylib_wrappers_model.c` defaults
`MAX_MESH_VERTEX_BUFFERS` to **7** at lines 51–53 and allocates that many
32-bit entries at line 126: **28 bytes**. With production
`GX_GPU_SKINNING=1`, lines **138–139** unconditionally initialize entries
**7 and 8**, at byte offsets **28 and 32**, outside the allocation.

The fresh Release build reported both `-Warray-bounds` diagnostics. The
external witness compiled the **unchanged complete backend translation unit**
with the production feature definitions and AddressSanitizer, linked against
the audit's private raylib build, and called the upload on a minimum triangle.
It produced:

```text
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 4
raylib_Mesh_Upload .../raylib_wrappers_model.c:138
0 bytes after 28-byte region
allocated .../raylib_wrappers_model.c:126
```

The positive arm exits 1; the already-uploaded early-return control exits 0.
The write precedes the first GL call, so a window/context is unnecessary to
demonstrate this initialization defect. This is not a full-engine ASan run,
an exploitability assessment or proof of a particular scene crash.

Production reachability is visible through `gX_Mesh_GenSphere()` at
`src/wrappers/model.c:404–407`, the buoy sphere at
`src/renderer/water.c:351`, and sphere props at `src/scene/physics.c:99`.
The separate RHI uploader uses a different capacity; this finding is about
the backend wrapper, not every mesh-upload implementation.

Exact commands, stdin fixture, source hash, control output and ASan trace:
`diagnostics/build/vbo-bounds/commands.json`, `results.json` and
`new-mesh-positive.log`. **No repair was made.**

## M11-04 — Reproduced full-reflection gate failure

The fixed curved-sphere fixture's cubemap-component fraction is **0.418** on
the first full suite and **0.417** on the unchanged repeat, below the existing
**0.50** floor in `scripts/reflect_suite.py:438`. The planar component is
0.708 on both runs and passes its 0.30 floor. Both suite runs grade **F** and
`make` exits 2.

The six planar/SSR angle cases, receiver mask, standalone-water smoke,
direct-cubemap response and face/orientation checks pass. Thus this is a
specific, reproduced fixture failure—not evidence that all reflections fail.
No native attribution to the heap defect, geometric ray error, radiance
policy or oracle mismatch has been established. No threshold, camera or
fallback was changed.

First captures: `tmp/monthly-reflection-first/`.
Independent repeat: `tmp/monthly-reflection-repeat/`.
First and repeat stdout remain separate in `tmp/opencode/monthly-core/`.
The Debug and Release binaries are identical across admission, matrix and
both reflection runs.

## Capacity and residual notices

- Release payload: **4,187,739 / 4,194,304 bytes**, only **6,565 bytes**
  headroom. BSS: **2,701,344 / 2,705,000**, only **3,656 bytes** headroom.
  The existing lean gate passes. This is the existing **GX-I017** reserve
  concern, not permission to raise caps or trim capabilities.
- Core proof audit: **60 modules / 1,347 theorem declarations**, standard
  axioms only, against the matching published pin. This is not a universal
  C-memory-safety or GPU-radiance certificate.
- All **15** GL-state warnings are preserved verbatim. Ten concern raw
  oracle GL mutations; four concern local scope counts; one is the global
  push/pop count, 28/30. Source review finds mutually exclusive early-return
  and normal-tail pops in the point-shadow and postprocess functions. Lexical
  counting sees both; this review is not a dynamic GL-state proof.
- Admission retains **103** informational shader-uniform heuristics and
  compiler/Lean notices. Exact localized warning text and locations remain
  in the raw log and `reports/admission-warning-lines.txt`. The bounds
  warnings above were investigated, not dismissed because admission passed.
- The cookies feature row passes its existing zero-floor rule with changed
  fraction 0 and mean delta about `4.24e-7`. It does **not** demonstrate a
  visible cookie contribution. This is a coverage limitation, not a newly
  proven broken-cookie diagnosis.

## Advisory image review

All 29 OFF/ON pairs were reviewed as report-bound contact sheets, including
shared OFF controls. Native-size SSR, sun-facing control/godray, point-shadow,
water, underwater-editor and curved-sphere captures were also inspected.

Material/shape separation, decals, shadows, instancing, weather and mirror
contributions are readable. Native SSR shows conspicuous stepped/banded
reflection edges; water has angular/block-like detail. These are still-image
polish observations, not independently established wrong ray hits or temporal
instability. The bright sun-facing sky is already bright in the OFF control;
it is not by itself proof of a godray runaway. The curved-sphere native image
retains a magenta response but still fails the automated component floor.

Human grades remain **UNREVIEWED**. `reports/capture-index.json` verifies all
107 image references, including the 58 paired-arm references; shared controls
mean these are not 107 distinct images. Reflection references bind to the
first failed run, not silently to overwritten repeat files.

No isolated general-purpose performance benchmark, physical CRT/controller
test, cross-driver certification or arbitrary-scene radiance proof was added.
Console budget gates pass, but their timings are not a month-over-month
performance claim. October's differently scoped repository snapshot is not
a compatible pixel baseline for this release worktree.

## Preservation and newer live work

The frozen source snapshot is unchanged and its final sandbox Git status is
clean. Protected settings/art2 hashes and art.json's initial absence are
unchanged both inside the sandbox and in the original checkout. The original
proof source files also remain unchanged.

The **live checkout was edited outside the sandbox during the audit**. At the
final preservation check, HEAD was still `89927f5`, but there were newer
changes to `CHANGELOG.md`, `docs/console_240p.md`,
`scripts/cartridge_check.py`, `scripts/console_shell_check.py`,
`src/gui/update.c`, `src/renderer/gfx/scene.c` and
`src/renderer/gfx/showcase.c`, plus its ignored session/binary state. They
were recorded and **not restored or discarded**. The results above do not
certify that newer live WIP or subsequent changes.

Identity, original and sandbox hashes, source snapshot, exact gate attempts,
final live diff and preservation checks are retained in the evidence bundle's
`identity/` directory. The audit run performed no commits, tags, pushes, source
repairs, worktree cleanup or inventory-note writes. Adding this document and
its index link afterward does not regrade any recorded result.
