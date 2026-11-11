# Gate memory and audit/test unification

Status: manifest + changed-only planner + opt-in shared cache (2026-10-31). The existing
`audit_summary.py` result cache is retained; this document records what it
already does, what it misses, and how the unified gate manifest works.

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
- Quick matrix: 20 gates, **236 s** first run at `AUDIT_JOBS=2`; the
  deduplicated full matrix is 84 gates (**1,207 s**). `AUDIT_JOBS=4` is not
  reliably faster on the GTX 1060 (capture contention; 267 s with a
  sequential retry), so 2 remains the default.
- `make check` repeat: **61 s**. It does not use the gate cache; only its
  sub-tools (CMake, Lake/Lean) have their own incremental state.
- `make cache-check` pins the contract: clean run executes, repeat is cached,
  `--no-cache` re-runs.
- `AUDIT_CACHE=0`, `audit_summary.py --no-cache`, `--cache-clear` and
  `make audit-cache-clear` are the escape hatches.
- `--explain` and `make audit-cache-show` expose why a gate is fresh or stale.
- `gates.toml` is now the source of truth for the audit gate list, tiers,
  groups, cache policy and shared harness files. `scripts/audit_summary.py`
  loads it and keeps the old hardcoded list only as a fallback. A full run
  skips a quick gate whose `superseded_by` full sibling runs the same script
  with a superset (`matrix-quick`, `gui-quick`, `gui-layout-quick`); the
  `cache` self-test is a `tooling`-tier gate, not an engine gate.
- `scripts/gates.py` adds `list`, `plan`, `run`, `explain` and `cache`;
  `make gates-list`, `gates-plan`, `gates-run` and `gates-check` wrap it.
  `--manifest PATH` applies to execution as well as selection: the runner
  receives the same manifest instead of reloading the default.
- `--changed REF` (or `--files a,b`) maps a Git diff to affected gates through
  declared group inputs and shared harness files. `docs/` changes select no
  audit gates; an unmapped build input conservatively selects all gates.
- Per-gate cache policy is honored: `cache = "never"` bypasses the cache for
  `profile` and `cache`; the rest use content-addressed passes. A failure in
  a parallel batch is retried sequentially after the batch; the first
  parallel verdict stays attached so contention remains visible.
- `AUDIT_CACHE_DIR=/path` opts into a shared cache directory. Entries are
  stored per gate and per content key, merged under a file lock and written
  atomically, so parallel worktrees do not lose passes. The directory is not
  part of a release archive; release/version gates still run clean.

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
- The planner uses group-level input globs, not per-file dependencies;
  `src/common/` and `resources/tools/` still over-select.
- `make check` still does not use the gate cache, and `version_check.TIERS`
  remains separate from the manifest.
- The shared cache is opt-in and local/CI-managed; there is no central
  service or eviction policy yet.

## Unified gate manifest

`gates.toml` is the source of truth for the audit gates:

```toml
[gate.water]
script = "gfx_water_check.py"
args = []
tier = ["full"]
group = "water"
cache = "content"        # content | never | ttl
timeout = 1200
retries = 1
description = "Water surface, fog and planar reflection contracts."
```

Group inputs live once:

```toml
[groups.water]
inputs = ["src/renderer/", "resources/shader/", "resources/tools/",
          "src/common/"]
```

`cache = "content"` is the default for deterministic gates. `never` is for
timing, network, mutating or nondeterministic gates. `ttl` is reserved for
environment-sensitive gates that should be refreshed periodically.

The runner exposes:

```bash
make gates-list
make gates-plan TIER=full CHANGED=HEAD~1
make gates-run TIER=quick CHANGED=HEAD~1
python3 scripts/gates.py explain water
python3 scripts/gates.py plan --tier full --files src/renderer/water.c
python3 scripts/gates.py cache --show
```

`audit_summary.py` loads the same manifest. `--changed REF` (or `--files`) maps
a Git diff to affected gates through group inputs and shared harness files.
Shared files invalidate every gate; an unmapped build input conservatively
selects all gates; `docs/` changes select none. The manifest test rejects a
missing gate script or unknown group.

## Shared cache

The cache is local to `build/` by default. To share passes across worktrees or
CI jobs:

```bash
AUDIT_CACHE_DIR=/path/to/gx-audit-cache make audit-summary
AUDIT_CACHE_DIR=/path/to/gx-audit-cache make audit-cache-show
AUDIT_CACHE_DIR=/path/to/gx-audit-cache make audit-cache-clear
```

The cache file is `$AUDIT_CACHE_DIR/.audit_cache.json`; entries are keyed by
gate name and the full content key, and `save()` merges the on-disk entries
before an atomic replace. `AUDIT_CACHE_DIR` is excluded from the gate
environment fingerprint. The shared cache is an optimization, not release
evidence: `AUDIT_CACHE_DIR` must not be used for the
version/release gate.

## Rollout

1. **Cache correctness** (this slice): args, gate environment, stable hardware
   identity, `--explain`, public `cache-check` fixture.
2. **Manifest for audit gates** — done: `gates.toml`, `list`/`plan`/`run`/
   `explain`, and `audit_summary.py` loads the manifest.
3. **Per-gate dependencies** — group inputs and shared harness files are in;
   per-file globs and the remaining `patches/`, `modules/`, `external/` and
   proof inputs still need refinement.
4. **`make check` memory** — pending: cache the deterministic, non-GPU steps
   first (unit tests, math, static audits). Keep Lean/Lake's own build cache
   and never cache release packaging or save/overwrite probes.
5. **Changed-only and shared cache** — `--changed`/`--files` and the opt-in
   `AUDIT_CACHE_DIR` shared cache are in; a central CI cache/eviction policy
   remains future work.

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
