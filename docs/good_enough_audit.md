# “Good enough” audit — reflections

<!-- //ai: report-only practical assessment; no gate waiver or renderer promotion. -->

Date: 2026-10-06. Scope: current default reflections and the isolated probe
experiment, **not** whole-engine certification or release readiness.

## Verdict

**Good enough for continued author dogfooding on the default path, with known
limitations. Stop tuning for now. Not good enough to promote the experimental
reflection candidate or claim an all-green feature matrix.**

“Good enough” means a useful bounded workflow, not perfect geometric reflection
or a replacement admission grade. The default path has passing build/smoke,
reflection and bloom evidence. The experiment has answered its immediate
sky/local separation question without replacing production sampling. Remaining
uncertainties are explicit and can stay parked while other work proceeds.

## Evidence and practical assessment

This assessment reuses the completed `split_validation/` run; it does not
pretend to be another full gate run. HEAD was
`8dd35974dfb30ef7daca7d9332942d92c9cf36e7`, with extensive concurrent
uncommitted work. HEAD alone does not identify the tested tree. The index was
empty at report time. Evidence snapshots, SHA-256 manifest and worktree status
are retained under `/tmp/opencode/good_enough_audit_2026-10-06/`.

| Area | Evidence | Practical judgment |
| --- | --- | --- |
| Default build/runtime baseline | `make check` passes, including smoke/tests/audits | Suitable for continued bounded dogfooding; not universal stability evidence |
| Default reflections | Existing suite A- | Keep current default; no justification to replace it with the candidate |
| Bloom | 22 checks pass | No additional bloom tuning required by this audit |
| Probe diagnostics | 72 CPU tests and fresh split capture/report pass | Good enough to retain as isolated research evidence |
| Separate sky resource | 517,842 sky-labelled pixels, zero sky RGB difference across five capture selections | Separation works in this fixed procedural-sky, SSR-off fixture |
| Hidden local hits | Probe 0/1 each leave 29,343 hits unavailable; routed RGB zero | Honest diagnostic outcome, not an approved black production fallback |
| GL state | 0 errors, 13 existing warnings | Known debt remains; not a warning-free or dynamic GL correctness certificate |
| Payload | 3,906,491 bytes; 85 bytes headroom | Within cap, but effectively no room for casual production additions |

### Failures are retained, not regraded

- **Mirror:** changed fraction `0.049859` versus required `0.05`. The reviewed
  still shows visible mirror content. This is a narrow gate miss, not proof
  that mirror rendering is absent, and not grounds to lower the cutoff.
- **Probes:** changed fraction `0.000000` at the matrix's pixel-change cutoff,
  required `0.00005`; mean image difference is `1.79`. This means insufficient
  demonstrated visible effect in the fixture, not proof that probes never work.
  Do not claim their visible benefit is validated.
- **Particles:** changed fraction `0.000000`, required `0.0001`, after retry.
  Outside this reflection-focused audit; keep the failure on the ledger.
- **Candidate:** reflection suite **F**, face-color metric `0.000`. Earlier
  independent face witnesses explain why a cyan result can fail the unchanged
  dominant-channel metric. That limits what the metric proves; it does not
  erase the failure or establish correct mixed-scene surface RGB.

The reviewed latest candidate stills show a visible planar cube reflection,
large cyan/green regions in the face-color fixture, and strongly blocky water
detail. The water gate establishes non-black output (`99.8%`), **not** polished
water appearance. No claim of temporal smoothness or photorealism follows
from these stills. The default A- verdict comes from its own completed log;
the shared screenshot directory was subsequently used by the candidate run.

## What to keep, park and revisit

**Keep:** default sampling, existing weights/budgets/thresholds, the accepted
contact-history decision, independent sky/local diagnostics and explicit
unavailability codes. No settings, provenance or resource imports are needed.

**Park:** further projection tuning, proof expansion and local-RGB integration.
The diagnostic still has 675 readback-ambiguous world cases and 674 local
visibility cases per capture; positive sampled distance is not correct-surface
RGB certification. Hidden surfaces and filtered silhouettes remain unresolved.
Do not put the fixed exact-box assumptions into general production scenes.

**Revisit only when:** ordinary authoring reveals a reproducible distracting
artifact/crash, a concrete scene needs probe reflections the default cannot
provide, or deliberate release/promotion work requires the failing gates to
close. A full matrix/release claim still needs those failures resolved under
unchanged gates. Any future candidate integration needs an explicit unavailable
local-color fallback and fresh default/candidate validation.

This audit changes no runtime code, admission thresholds, release caps or
production defaults. It authorizes no publish action and certifies neither
arbitrary scenes nor performance; a screenshot FPS label is not a benchmark.

Detailed experiment logs remain in the local evidence directories noted
above; this report does not promote their implementation or publish those logs.

## Dogfooding follow-up: showcase slowdown

<!-- //ai: actual reported usability defect qualifies the earlier practical verdict. -->

The user subsequently reported whole-scene slowdown in the showcase, not
reflection trailing. A repeated advisory benchmark using the existing runner,
1536x1152 resolution, 90 warmup frames and 120 measured frames per repeat
(two repeats, hidden window, fixed simulation step, uncapped timing) found:

| Showcase configuration | CPU frame p95 (ms) | GPU total p95 (ms) |
| --- | ---: | ---: |
| Normal | 90.737 | 48.483 |
| SSR off | 19.607 | 11.350 |
| Environment reflections off | 72.312 | 43.978 |
| Both toggles off | 19.626 | 11.556 |

Evidence: `/tmp/opencode/showcase_reflection_cost2/report.json` and its repeat
CSVs/logs. Protected hashes remained unchanged. The first attempted runner
failed argument parsing before launching scenes and is not timing evidence.

**The SSR-enabled workload is a real practical performance limitation on this
host.** The cheap mitigation is to press **R** to disable SSR while dogfooding.
Do not present the normal showcase as validated 60-FPS operation at this size.
These measurements are not interactive/display-latency benchmarks and CPU/GPU
timings must not be added together.

Turning SSR off also skips dependent stages: the recorded SSR draw timer is
only about 0.9ms p95 in the normal run, versus about 48ms total. Therefore the
large improvement does **not** yet identify ray marching as the culprit.
Likewise, both toggles off can retain planar/cubemap consumers, so that row is
not an all-reflection-work-eliminated control. If GFX work resumes, finer
profiling of SSR-dependent stages is the bounded next task, not projection
research. No runtime defaults or quality thresholds were changed.

## Anti-perfectionism audit: stabilization checkpoint

<!-- //ai: user-authorized report-only triage; no admission waiver or commit authorization. -->

Date: 2026-10-06. Latest completed postcommit evidence is for HEAD
`c1eb2a8866c265db1a27894e2ae09de3ba57d719` **plus the dirty worktree**, under
`/tmp/opencode/label_scope_review/postcommit/`. Earlier bloom success above is
historical: the latest run fails one threshold-sweep check.

### Decision rules

- **Block the affected workflow:** reproducible crashes, corruption, lost edits,
  or licensing/provenance violations. Repair before continuing that workflow.
- **Investigate briefly:** new failures get one repeat, capped at ten minutes,
  plus comparison with retained evidence. If attribution needs more work,
  record it as unresolved rather than starting open-ended renderer research.
- **Accept and record for bounded dogfooding:** cosmetic limitations and known
  experimental failures that do not block the chosen authoring workflow.
  This is not permission to skip required admission or release gates.
- **Stop tuning:** once the bounded workflow works reliably and its limitations
  are recorded, move to the next concrete authoring defect.
- **Reopen:** a reproducible authoring blocker, materially distracting artifact,
  or deliberate certification/promotion requirement—not a desire for perfection.

### Current triage

| Item | Evidence | Decision |
| --- | --- | --- |
| Label repair | Isolated build, anchor/top, model-follow, raster/style, orientation and history checks pass | Stop tuning; retain static-AABB/animated-envelope limitation |
| Admission | Latest combined-worktree `make check` passes | Continue bounded dogfooding; not clean-release certification |
| Default reflection / GL state | Reflection passes; GL state has 0 errors and 13 warnings | Keep defaults and recorded warnings |
| Matrix | `mirror`, `particles`, `probes` still fail | Park research; no all-green claim or promotion |
| Bloom | Initial run: 21 pass, threshold sweep fails; bounded repeat: all 22 pass | Record non-reproduced failure; park investigation, no baseline attribution |
| Water-height persistence | Existing uncommitted repair and earlier author signoff; not independently committed | Next scoped repair after bounded bloom triage |
| Showcase performance | SSR-enabled workload is slow in the retained benchmark | Dogfood with SSR off; do not expand profiling now |

Protected settings/art files were unchanged in the completed label runs.
No new crash, corruption or lost-edit finding is established by these results;
that is not a universal safety claim. The bloom failure must not be attributed
to labels without baseline isolation, or called harmless without workflow
evidence. If the repeat budget expires, park its **investigation**, not its
failure verdict. Release certification still requires the applicable gates.

The bounded repeat completed with exit 0: all 22 bloom checks pass, with
threshold fractions `0.90591 > 0.01871 > 0.00000 > 0.00000` accepted by the
unchanged gate. Evidence: `/tmp/opencode/anti_perfectionism_audit/`. Protected
hashes remain unchanged. Retain the initial failure as possible flakiness;
one passing repeat establishes neither its cause nor a clean-release verdict.
Stop this investigation for now and resume the scoped water-height review.

This checkpoint changes no runtime code, thresholds, provenance, budgets or
release caps, and authorizes neither a commit nor publication.

## Water follow-up (2026-10-07)

<!-- //ai: bounded water repair evidence and correction of the suspected tint defect. -->

Water-height persistence is committed as `e0ef9b1`. In an isolated checkout,
the old code failed the unchanged height-to-image assertion; the repaired code
passes it (mean pixel difference `11.7391`) and the existing water-render gate.
Combined-worktree admission passes after correcting a path-like phrase in the
uncommitted release guide; the initial docs-guard failure remains recorded.
Protected settings/art hashes are unchanged. Evidence:
`/tmp/opencode/water_height_review/`.

The suspected water-tint/opacity mismatch was **not established**. Source review
shows the water panel uses `gfx_color_edit`, which calls `ImGui::ColorEdit3`
and leaves alpha untouched. Rendering likewise uploads RGB tint only. The RGBA
schema preserves alpha, but it is not an exposed water-opacity setting. Surface
opacity is computed separately by the shader. Seven source-contract checks pass
at HEAD `e0ef9b1`; evidence: `/tmp/opencode/water_tint_review/report.json`.
This is source review, not a GPU transparency certificate.

**Stop here:** no opacity feature or visual tuning is justified by this finding.
Reopen water appearance work for a specific reproducible authoring problem.

### Proposed next water scope

<!-- //ai: user-proposed water CI/refraction follow-up; plan only, no feature authorization inferred. -->

Prioritize regression coverage over general visual tuning:

- height/default persistence and enabled/disabled preset round-trips;
- tint RGB, wave settings and normal-map clear/fallback behavior;
- finite, non-black above-/below-water output in controlled owned fixtures;
- retain the existing object-reflection gate and its unchanged thresholds.

Water refraction needs a separate contract. The current water fragment shader
refracts sky directions underwater; it does not sample submerged scene geometry.
The renderer's transparent-model refraction path is not automatically a water
refraction implementation. A future scene-refraction feature would need to
define capture ordering, water exclusion, depth/foreground rejection, UV
orientation, unavailable-sample fallback and performance cost. Start with an
owned submerged-object fixture and explicit on/off controls, not a broad
reflection rewrite. This proposal is not evidence that those tests or that
feature exist, nor a new release requirement.

### Focused water CI gate under review

<!-- //ai: implemented test scope, not an assertion that pending validation passed. -->

`make water-ci-check` runs the new owned-fixture authoring check and the existing
object-reflection check. The full audit summary includes the independent
`water-authoring` row. The new check passes on the combined worktree and the
isolated height-repair tree. Evidence: `/tmp/opencode/water_ci_review/`;
protected settings/art hashes are unchanged.

The authoring check compares saved/reloaded scenes, enabled/disabled presets,
height changes, RGB tint, authored waves, cleared normal detail versus a loaded
shipped normal map, and above-/below-water captures. Corrupted-field controls
exercise its serialization assertions. It retains unique run directories and
uses shared settings-preservation and hidden-window helpers. No new assets or
renderer code are required; existing gate thresholds are unchanged.

Limits: this checks enable/disable through preset loads, **not an in-process UI
toggle or undo**. Image checks establish visible differences and non-black
output, not physical water accuracy, GPU floating-point finiteness, correct
refraction, or arbitrary missing-file recovery. PNG values being finite is not
a shader numerical-safety proof. Tint alpha retention remains serialization
only; it does not imply alpha controls water opacity.

The isolated run passes 12 corrupted-field controls. Save/reload, re-enabled
presets and cleared-normal comparisons reproduce with mean image delta `0`;
height, tint, waves and normal detail have mean deltas `5.5547`, `7.0175`,
`3.5621` and `6.4098`. Underwater enabled/disabled mean delta is `21.4689`.
These are fixture results, not general visual-quality scores.
