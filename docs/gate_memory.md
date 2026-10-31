# Gate memory and audit/test unification

Status: research + first cache-correctness slice (2026-10-31). The existing
`audit_summary.py` result cache is retained; this document records what it
already does, what it misses, and how to unify `make check`, `audit-summary`,
`version-gate` and targeted gates around one gate manifest.

## Why this exists

Most gate work is deterministic. Re-running an unchanged gate costs time
without adding evidence, but reusing a stale result costs trust. The goal is a
**content-addressed gate memory**: reuse a pass only when every input that can
change its verdict is identical; always re-run failures; make the reason for a
reuse or a re-run inspectable.

## Current state (measured 2026-10-31)

- `audit_summary.py` caches **passing** gate results in
  `build/.audit_cache.json`, keyed by a source-input digest plus the gate
  script. A failed gate is never stored.
- Quick matrix: 15 gates, **240.7 s** first run, **1 s** cached repeat
  (`AUDIT_JOBS=4`).
- `make check` repeat: **61 s**. It does not use the gate cache; only its
  sub-tools (CMake, Lake/Lean) have their own incremental state.
- `make cache-check` pins the contract: clean run executes, repeat is cached,
  `--no-cache` re-runs.
- `AUDIT_CACHE=0`, `audit_summary.py --no-cache`, `--cache-clear` and
  `make audit-cache-clear` are the escape hatches.
- `--explain` and `make audit-cache-show` expose why a gate is fresh or stale.

## Cache key contract

A gate result is reusable only when all of these match:

1. **Build inputs** — `src/`, `resources/`, `scripts/`, `cmake/`, top-level
   build files, frozen raylib API headers, selected CMake profile, toolchain
   identity.
2. **Gate identity** — the gate script content and its arguments
   (`--quick`, `--full`, `--only`, fixture selectors).
3. **Gate environment** — `GX_*`/`AUDIT_*` knobs, display/session, Mesa/NVIDIA
   overrides, locale/timezone, Python import path. Scheduler-only controls
   (`AUDIT_CACHE`, `AUDIT_JOBS`) are excluded.
4. **Hardware/runtime identity** — OS/kernel/arch, Python and NumPy versions,
   stable GPU vendor/renderer/driver strings. Live GPU memory counters are
   deliberately excluded because they change between processes.
5. **Verdict state** — only passes are stored; failures always run again.

`audit_cache.py --show` compares the stored parts and reports
`base/script/env/hardware changed`. `audit_summary.py --explain` also compares
the gate arguments, which it knows from the gate list.

## What should be audited again?

| Situation | Re-run? | Why |
| --- | --- | --- |
| Same inputs, args, environment, hardware; previous pass | No | Nothing that can change the verdict moved. |
| Any source/resource/script/build-profile change | Yes | The gate's subject or harness changed. |
| Gate args changed (`--quick` -> `--full`, different fixture) | Yes | Different assertions or coverage. |
| Display size, `GX_AUDIT_FULLSIZE`, GPU/driver or Mesa/NVIDIA override changed | Yes | Rendering gates can legitimately change verdicts. |
| Previous failure | Yes | Failures are not cached; flakiness stays visible. |
| Timing/performance gate, network fetch, mutating fixture, or nondeterministic seed | Never cache | A pass is not reproducible evidence. |
| Release/version gate | Full clean run | Release evidence must not depend on a local cache. |
| Periodic clean run (nightly/weekly) | Full run | Detects cache drift and environment regressions. |

The last two rows are the safety valve: a cache is an optimization, not a
release certificate.

## Gaps found in the current system

- Gate arguments and gate-relevant environment were not in the key. A change
  from `--quick` to `--full`, a different display size, or a `GX_*` switch
  could reuse a stale pass. Fixed in the first slice.
- The hardware probe originally included live GPU memory, so the key changed
  between processes. Fixed to stable vendor/renderer/driver identity.
- `make check` does not use the gate cache at all. Its deterministic steps
  (unit tests, math pass, scene smokes, static audits) still run every time.
- The gate list lives in at least three places: the Makefile target list and
  inline recipes, `audit_summary.GATES`, and `version_check.TIERS`. They can
  drift; a gate can exist in one path and be absent from another.
- The base digest hashes every file under `scripts/`, so editing one gate
  script invalidates every gate. It is conservative but over-invalidates.
- Some build inputs are outside the hashed set: `patches/raylib-*.patch`,
  `modules/`, most of `external/`, `license/`, and the `proofs/` tree. A
  change there can affect a build or a proof gate without invalidating the
  cache.
- There is no per-gate dependency declaration, no `--changed` planner, and no
  shared cache across worktrees or CI. The cache is local to `build/`.
- There is no explicit cache policy per gate; the runner assumes every gate in
  the list is safe to cache.

## Proposed unified gate manifest

One manifest is the source of truth for every gate:

```toml
[gate.water]
command = ["python3", "scripts/gfx_water_check.py"]
tier = ["full"]
inputs = ["src/renderer/water.c", "resources/shader/water.frag"]
shared = ["scripts/audit_common.py", "scripts/audit_cache.py"]
env = ["GX_AUDIT_FULLSIZE", "DISPLAY"]
cache = "content"        # content | never | ttl
timeout = 1200
retries = 1
description = "Water surface, fog and planar reflection contracts."
```

`cache = "content"` is the default for deterministic gates. `never` is for
timing, network, mutating or nondeterministic gates. `ttl` is for
environment-sensitive gates that should be refreshed periodically.

A single runner exposes:

```bash
gates list
gates plan --tier check --changed HEAD~1
gates run --tier quick
gates explain water
gates cache --show
```

`make check`, `make check-fast`, `make audit-summary`, `make audit-summary-full`
and `version-gate` become thin wrappers over the same manifest and runner.
`--changed` maps a Git diff to affected gates through declared input globs;
shared harness files invalidate every dependent gate, not every gate in the
repository.

## Rollout

1. **Cache correctness** (this slice): args, gate environment, stable hardware
   identity, `--explain`, public `cache-check` fixture.
2. **Manifest for audit gates**: move `audit_summary.GATES` into the manifest;
   add `list`, `plan`, `explain`; keep the existing gate commands unchanged.
3. **Per-gate dependencies**: replace the all-scripts base hash with declared
   shared helpers and input globs; add the missing `patches/`, `modules/`,
   `external/` and proof inputs.
4. **`make check` memory**: cache the deterministic, non-GPU steps first
   (unit tests, math, static audits). Keep Lean/Lake's own build cache and
   never cache release packaging or save/overwrite probes.
5. **Changed-only and shared cache**: add `--changed` for local iteration and
   an opt-in shared cache directory for worktrees/CI. Key the shared cache by
   the same parts, not by machine path.

## Risks and mitigations

- **Stale pass** — pass-only storage, explicit environment/hardware parts,
  `--explain`, `CACHE_VERSION` bumps, periodic clean runs.
- **False confidence** — a cached pass is evidence for the same inputs, not a
  release certificate; release/version gates always run clean.
- **Cache poisoning** — the cache lives under `build/` and is never part of a
  release archive; shared caches should be opt-in and content-addressed.
- **Maintenance burden** — the manifest must stay small and generated Make
  targets should remain stable for humans and CI.
- **Environment drift** — hardware identity is best-effort; if no stable GPU
  probe is available, rendering gates should fall back to `cache = "never"` or
  a short TTL rather than claiming a portable pass.

## Non-goals

- Caching failures or hiding flaky gates.
- Replacing CMake/Ninja or Lake's incremental builds.
- Treating a local cache as a substitute for the release gate matrix.
- Hashing secrets or the entire process environment.

## References

- `scripts/audit_cache.py` — key, cache file, `--show`.
- `scripts/audit_summary.py` — gate list, scheduler, `--explain`.
- `scripts/audit_cache_check.py` — clean/repeat/no-cache contract.
- `scripts/test_audit_cache.py` — input, args, environment and explain tests.
- `docs/architecture.md` — audit runner context.
