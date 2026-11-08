# GFX memory and gate audit — November 2026 follow-up

<!-- //ai: report-only follow-up; engine source, shipped fixtures, defaults and gate thresholds were not repaired or changed. -->

**Outcome at the frozen snapshot: not clean.** The follow-up confirms a
separate async OBJ read overrun, a generated-model material leak, incomplete
allocation-failure rollback and an unsafe tangent API boundary. It also
establishes which matrix rows accept an absent feature contribution. At the
snapshot, the earlier sphere-upload and full-reflection findings remained open.

> **Resolution update (2026-11-08):** this page records the frozen `89927f5`
> snapshot. The async OBJ deindex overrun was repaired in `39e3201`, the
> generated-model map ownership and allocation rollback in `0132c4d`, the
> tangent API boundary in `4288636`, and the zero-floor matrix rows in
> `c6fa4e6`/`f1ba90e`; the sphere upload and reflection suite are covered by
> the [monthly audit](gfx_audit_2026-11.md) resolution note. The findings below
> remain the audit record; see [`ideas.md`](ideas.md) GX-I024 for current
> status.

## Identity and boundaries

- Frozen source: `89927f5290031cc34643ae40dfea6cb63b681560`, originally
  `feature/console-240p`; the same snapshot as the
  [monthly GFX audit](gfx_audit_2026-11.md).
- Runtime work used a fresh copy-on-write sandbox, not the newer live WIP.
  The existing Debug/Release binaries were retained; an additional
  `GX_SANITIZE=ON` Debug build instrumented gX with ASan and UBSan.
- Reference host: Linux, GTX 1060 6GB, NVIDIA 580.196.07, desktop GL 3.3
  request. This is not cross-driver or platform certification.
- Started 2026-11-07 11:46:03 UTC; final sandbox verification 12:00:06 UTC.
  Tracked source hashes, protected resource hashes and the original
  Debug/Release binary hashes match their retained baselines. The sanitized
  binary also remained unchanged between its runs. Live tracked/resource
  hashes were unchanged at the pre-report preservation check.
- `GX_NO_SETTINGS_SAVE=1`, hidden audit windows and `AUDIT_CACHE=0` were
  retained. No display-mode changes, source repairs, threshold adjustments,
  commits, tags or pushes were made. This document and the index/ledger
  updates were added after the audit.

Raw evidence remains outside the repository in bundle
`memory-gates_xp07cve4`. Paths below are **relative to that external bundle**,
not files under `docs/`. It retains commands, source/archive/binary hashes,
native logs, generated OBJ/preset inputs, allocator witnesses and capture
references. `reports/capture-index.json` verifies **52 references / 44 unique
paths**; shared controls are counted as references, not distinct images.
No raw fixtures, captures or machine-specific paths are installed here.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| `make sanitize-check` | PASS, unit suite reports 0 errors | `logs/sanitize-check.log` |
| Sanitized 24-frame `--title`, `--showcase-minimal`, `--showcase-y2k` | PASS | `logs/sanitize-*.log` |
| Sanitized 24-frame `--water`, `--physics` | FAIL, existing M11-01 upload overrun | `logs/sanitize-water.log`, `logs/sanitize-physics.log` |
| Native editor async OBJ, complete attributes | PASS, model loads and exits normally | `logs/native-async-complete-control.log` |
| Native editor async OBJ, shared normal / no normals / no UVs / positions only | FAIL, four ASan read-overrun reports | `logs/native-async-*.log` |
| Existing 80-bone gate through sanitizer build | PASS, main/depth upload 80 of cap 128; pose delta 0.77 | `reports/skinned.json`, `diagnostics/skinned/` |
| Tangent triangle, 1,000 cycles and three partial-allocation controls | PASS with leak detection enabled | `diagnostics/memory-slices/` |
| Tangent indexed quad at backend entry point | FAIL, ASan read overrun | `diagnostics/memory-slices/tangents-indexed-quad.log` |
| Generated-mesh add/remove, 100 cycles | FAIL, 33,600 bytes leaked in 100 allocations | `diagnostics/lifecycle-v3/generated-owner.log` |
| Same model with normal-owner destruction, 100 cycles | PASS with leak detection enabled | `diagnostics/lifecycle-v3/normal-owner-control.log` |
| Generated-mesh allocation failures | First/second failure leave a used slot; third also leaks 336 bytes | `diagnostics/lifecycle-v3/fail*.log` |
| Fresh eight-row feature baseline | PASS, 8/8 | `tmp/opencode/followup-matrix-baseline/report.json` |
| Same evaluator, null-response inputs | Six zero-floor rows PASS; bloom and SSAO correctly FAIL | `tmp/opencode/followup-matrix-null/report.json` |
| `python3 scripts/test_gfx_feature_matrix.py` | PASS, 11 tests | `logs/matrix-report-tests.log` |
| `reflect_suite.py --quick` | PASS, A- | `reports/reflection-quick.json` |
| Full `reflect_suite.py` | FAIL, F; curved-sphere cube fraction 0.418 < 0.50 | `reports/reflection-full.json` |

The null-response failures are intended experimental outcomes, not new bloom
or SSAO regressions. The source evaluator and its thresholds stayed unchanged.
The two positive-floor controls used the evaluator's existing retry behavior;
both initial and retried attempts failed and remain recorded. No failed
gate was rerun until it passed. Two preliminary lifecycle-harness link
attempts failed before execution; those logs remain in `diagnostics/lifecycle/`
and `diagnostics/lifecycle-v2/`. The successful harness links the real
sanitized engine archive and supplies its documented host globals.

## M11F-01 — High: async OBJ attribute-count mismatch

`src/wrappers/model.c:128–138` allocates destination attributes using
`attrib.num_vertices`, then copies that many normals and UVs without using
their separate counts or resolving the OBJ face attribute indices.

A legal triangle with three positions, three UVs and **one shared normal**
causes a 36-byte normal copy from a 12-byte allocation at line 137.
Missing normals also fail there; absent UVs fail at line 138. These are
ordinary supported OBJ attribute arrangements, not necessarily damaged files.

Each fixture contains ordinary short comment lines to exceed the existing
5 MiB async-selection threshold without changing its small geometry. All
four fail through the **actual sanitized editor executable**, loading an
external preset via `GX_GFX_PRESET_LOAD`. The complete-attribute control
successfully completes parsing, GPU upload and shutdown. Independent native
translation-unit witnesses reproduce the same four read failures.

Production routing is `gX_RendererGFX_Model_Load()` at
`src/renderer/gfx/model.c:1029–1036`, followed by the two-worker parser.
No source edits or substituted parser were needed. This establishes a
runtime loading defect, not a claim about exploitability or every OBJ file.

Evidence: `diagnostics/memory-slices/fixtures.json`, `commands.json`,
`source-hashes.json`, the native preset files in `diagnostics/native-async/`,
and `reports/followup-results.json`.

## M11F-02 — Medium: generated-model material maps leak

`gX_RendererGFX_Model_AddGeneratedMesh()` allocates one default material at
`src/renderer/gfx/model.c:3000–3002`. Its map array is separately allocated
at `src/backend/raylib/raylib_wrappers_model.c:59`.

The `ownsMesh` destruction branch at `src/renderer/gfx/model.c:1401–1413`
frees mesh storage, the material container and the mesh/material index, but
does not free each material's `maps`. The normal-owner branch delegates to
raylib, whose `UnloadModel()` explicitly frees those arrays.

An external harness links the unchanged sanitized engine archive and calls
the real generated-mesh add/remove APIs 100 times. LeakSanitizer reports
**33,600 bytes in 100 allocations: 336 bytes per cycle**. The control
changes only the instance's ownership flag to exercise the normal destructor
on the same compatible model representation; it reports no leaks.

This is a CPU ownership witness with unuploaded meshes, not an estimate of
GPU resource loss. The editor primitive path at
`src/tools/gfx/gfx_editor.cpp:1509` and physics-generated props at
`src/scene/physics.c:106` use the affected add API.

Evidence: `diagnostics/lifecycle-v3/`, `diagnostics/lifecycle-probe.c` and
the retained archive hashes. No destructor repair was made.

## M11F-03 — Low: generated-model allocation failure is not transactional

The same add routine reserves a used heap slot before its container
allocations. Failure of the mesh or material-container allocation returns
`-1` without releasing that slot (`src/renderer/gfx/model.c:2989–3004`).

Allocator-controlled witnesses fail the first and second explicit `malloc`
after heap initialization. Both return failure with **one used slot** still
present. Failure of the mesh/material-index allocation additionally loses
the already allocated default-material map array: LeakSanitizer reports
**336 bytes**. These are deterministic allocation-failure tests, not
observations of ordinary memory pressure on the host.

Evidence: `diagnostics/lifecycle-v3/fail1.log`, `fail2.log`, `fail3.log` and
`commands.json`. Caller-side mesh cleanup does not release the reserved slot
or the lost material allocation. No recovery change was made.

## M11F-04 — Medium, API-boundary scope: tangent generation assumes triangle soup

`raylib_Mesh_GenTangents()` walks `vertexCount` in triples and reads
`i+1`/`i+2` at `src/backend/raylib/raylib_wrappers_model.c:305–314`, ignoring
the mesh's index array. A valid indexed quad with four vertices and two
triangles produces an ASan read overrun at line 308.

The public `gX_Mesh_GenTangents()` wrapper delegates without validation
(`src/wrappers/model.c:441–443`). The principal GFX model-load caller
**already protects this count boundary** with a multiple-of-three guard at
`src/renderer/gfx/model.c:1214–1220`. Do not interpret this witness as a
newly demonstrated crash in that guarded load path.

A triangle control, 1,000 triangle cycles and failure of each of the three
tangent allocations pass with leak detection enabled. The finding is the
unprotected API boundary, not a blanket failure of tangent generation.

## M11F-05 — Coverage limitation: six rows accept no contribution

An external Python adapter imports the unchanged feature-matrix module and
sets each selected row's ON inputs equal to that row's OFF inputs. It does
not change engine/shader files, the measurement code or admission bands.
Fresh native captures still go through the ordinary evaluator.

| Row | Original floor | Null-response result |
| --- | ---: | --- |
| TAA | 0 | PASS, exact zero mean delta |
| FXAA | 0 | PASS, exact zero mean delta |
| Godray | 0 | PASS, exact zero mean delta |
| Cookies | 0 | PASS, exact zero mean delta |
| Lensflare | 0 | PASS, exact zero mean delta |
| Environment ambient | 0 | PASS, exact zero mean delta |
| Bloom control | 0.00001 | FAIL, changed fraction 0 |
| SSAO control | 0.005 | FAIL, changed fraction 0 |

The matrix already labels zero-floor results **“bounded effect; zero change
permitted.”** This experiment confirms that documented limitation; it does
not prove six renderer features are broken or that their separate focused
gates are insensitive. The normal baseline visibly changes the frame for
five of these six rows. Cookies again has changed fraction 0 and a maximum
mean-channel pixel delta of only 0.333; that row still does not establish a
visible cookie contribution.

Evidence: `drivers/null-matrix.py`, both matrix reports and the retained
null-input mapping. No positive floor was added during the audit.

## M11F-06 — Coverage gap: the full summary does not schedule the reflection suite

The frozen `gates.toml` has **no entry for `reflect_suite.py`**. The summary
selects registered quick/full gates; its `reflection-lab` entry instead runs
`gfx_reflection_lab_check.py`, a different strength/culling fixture. Thus
the monthly **100/100 curated summary** can be truthful while the separate
full reflection suite fails. This is missing suite coverage, not a cached
failure or a swallowed nonzero exit from that suite.

Quick reflection mode also omits the nonplanar, standalone-water, direct
cubemap and cubemap-face tasks (`scripts/reflect_suite.py:480–491`). In this
follow-up it grades A-, while full mode grades F because the sphere's
cubemap component is **0.418 < 0.50**; its planar component is 0.704 and
passes. This reproduces **M11-04**, without attributing it to a memory defect
or changing the threshold.

The full summary's temporal entry also explicitly passes `--quick`.
“Full summary” describes its curated selection, not every available script
or every full variant. Evidence: `reports/gate-coverage.json` and both new
reflection reports.

## Existing M11-01 — Production reachability now observed

The monthly report's backend upload witness is now reproduced in real
sanitized `--water` and `--physics` runs. Both terminate at
`raylib_Mesh_Upload():138`; native stacks identify the water buoy and
physics prop callers respectively. This strengthens the existing finding;
it is not a separate defect. The sanitizer unit suite passes because its
`--test-only` scope does not exercise those GL scene paths.

## Reproduction and limits

Use an isolated checkout of the frozen revision, not the live WIP:

```sh
make sanitize-check
GX_SKIP_TESTS=1 GX_FRAMES=24 GX_FIXED_DT=0.0166666667 \
  GX_NO_SETTINGS_SAVE=1 GX_AUDIT_WINDOW=hidden GX_EDITOR_DEFAULT_SCENE=0 \
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build/sanitize/gX_build --water --screenWidth 640 --screenHeight 480 \
  --targetFPS 60 --disableAudio
python3 scripts/gfx_feature_matrix.py "$GX_SCRATCH/followup-matrix-baseline" \
  --only bloom,ssao,taa,fxaa,godray,cookies,lensflare,envambient --jobs 1
python3 scripts/test_gfx_feature_matrix.py
python3 scripts/reflect_suite.py --quick --output "$GX_SCRATCH/followup-reflection-quick"
python3 scripts/reflect_suite.py --output "$GX_SCRATCH/followup-reflection-full"
```

The same native scene command was used for `--title`, `--showcase-minimal`,
`--physics` and `--showcase-y2k`. Native OBJ runs use `--gfx --gfx-editor`,
40 frames and the retained external preset in `GX_GFX_PRESET_LOAD`.
Exact commands/environments for every run and all external harness sources
remain in the bundle; the 80-bone adapter changes only the selected binary
and evidence retention around the existing gate.

Full-engine runs retain the repository's leak-detection-off setting;
dedicated CPU ownership/allocation witnesses explicitly enable it. Raylib
and the driver are not comprehensively sanitizer-instrumented by
`GX_SANITIZE`. No exhaustive GPU-object lifetime, long-duration resource
growth, race, cross-driver or arbitrary-scene claim follows. Performance,
cache/texture fallbacks and broader temporal transitions were not added to
this follow-up. `make check` and the 100-gate summary were not rerun here;
their earlier results remain historical, not a new admission claim.

At the frozen snapshot all findings remained unrepaired. The subsequent
resolution commits are listed in the update above; further implementation
requires a separate decision, and the audit does not authorize weaker
thresholds or larger budgets.

## Report installation checks

After writing the report, `make gates-check` passes its 11 tests and
`git diff --check` is clean. A scoped check of the report and its new
index/ledger links passes every referenced path; idea IDs are unique.

The whole-tree `python3 scripts/common_sense_check.py` **fails** on one
unrelated reference: the concurrently added new-user proposal names the
not-yet-created `new_user_pass.md` report under `docs/`. That work was
preserved, not removed or accepted into the baseline. Its GX-I020 ID was
retained; this audit uses GX-I021. This documentation-check failure is not a regrade of the frozen
engine's runtime results.
