# Peer-review gate

<!-- //ai: reusable review gate for major UX/editor work; scores are evidence, not a single aggregate. -->

Status: process contract (2026-10-28). Applies to major editor/UX changes,
release-facing surfaces and test changes; small fixes can use the ordinary code
review.

## Profiles

Reviews use a profile so unrelated surfaces are not forced into the same
lenses. Every lens is scored independently; the gate is always 7.0/10 minimum
and 8.0/10 target.

### UX/editor (default)

- **Workflow/UX** — task flow, hierarchy, discoverability, keyboard/mouse
  parity, error recovery and whether the common path is obvious.
- **Product/credibility** — whether the surface looks and behaves like the
  shipped product, matches the visual identity, has a deliberate default
  state, and would survive a public screenshot.
- **Accessibility** — text size/contrast, focus visibility, scaling,
  reduced motion, non-colour cues and reachable controls at supported sizes.

### Test

- **Contract** — does the test assert the intended behavior, and does it fail
  when that behavior is broken? A test that only proves "no crash" or mocks
  away the contract scores low.
- **Coverage** — positive, negative, boundary and error paths; the real
  failure is replayed, not replaced by a synthetic shortcut. Redundant cases
  and untested claims are blockers.
- **Determinism** — repeatable across runs, order, timing and machines; no
  sleeps as synchronization, no leaked global state, no unbounded loops.
- **Diagnostics & cost** — a failure names the contract and shows inputs,
  expected/actual; the fixture and runtime are proportionate.

Scores use a 0–10 scale. A 5 is "only usable with prior knowledge"; a 7 is
"adequate for merge, with visible debt"; an 8 is "release-grade and defensible
in public"; a 9–10 reserves room for exceptional quality. The same scale is
used for UX and test profiles.

## Grade mapping

Scores map to S/A–F with plus/minus bands:

| Score | Grade | Reading |
|---:|---|---|
| 9.5–10.0 | S+ | exceptional, reference quality |
| 9.0–9.4 | S | exceptional |
| 8.7–8.9 | A+ | release-grade, ahead of the bar |
| 8.3–8.6 | A | release-grade |
| 8.0–8.2 | A- | release-grade, minor debt |
| 7.7–7.9 | B+ | solid, near release-grade |
| 7.3–7.6 | B | solid, polish needed |
| 7.0–7.2 | B- | minimum merge gate |
| 6.7–6.9 | C+ | usable, visible debt |
| 6.3–6.6 | C | usable with guidance |
| 6.0–6.2 | C- | rough but tractable |
| 5.7–5.9 | D+ | below the merge bar |
| 5.3–5.6 | D | below the merge bar |
| 5.0–5.2 | D- | below the merge bar |
| 0.0–4.9 | F | not acceptable |

`scripts/peer_review_grade.py 7.4 8.1 7.8` prints the default UX grades and
checks the gate. The test profile takes four scores:

```bash
python3 scripts/peer_review_grade.py --profile test 7.5 8.0 7.0 7.8
python3 scripts/peer_review_grade.py --names --profile test
```

The mapping is a presentation layer over the raw scores; it does not replace
the evidence or the blocker list.

## Merge gate

For a major editor/UX change:

- each lens must score **at least 7.0/10 (B-)**;
- there must be **no open high-severity blocker**;
- the target for a release-line merge is **8.0/10 (A-)** in every lens.

Record the three raw scores, their grades, the evidence and every blocker in
the change's review note. Do not collapse them into a single average; a 9/9/5
is not an 8.

## Evidence

UX/editor:

- Captures at 1024x768, 1600x900 and 1920x1080.
- The interaction matrix for the changed surface (open, select, edit, undo,
  save/load, cancel/back).
- Accessibility checks: contrast, focus, text minimum and scale.
- Code review for state ownership, escaped input, allocation lifetime and
  destructive-action guards.

Test:

- The failing case before the fix and the passing case after it.
- A mutation/fault-injection result proving the test can fail.
- Repeated runs (for example 20), shuffled order, and parallel execution where
  relevant.
- A failure-output excerpt that shows inputs, expected/actual and the contract.
- Test runtime and fixture size.

## Re-review

Re-run the same lenses after the fixes. A score only counts when the evidence
is from the current revision; stale captures from a previous layout do not
carry forward.
