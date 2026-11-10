# Agent roles and independence

<!-- //ai: GX-I037 harness-neutral role charters; advisory, not a security boundary. -->

Status: process contract (2026-11-10). Roles are lenses for review and
evidence, not separate legal, security or release authorities. A human author
of record still owns every change; `make check` and the focused gates remain
the admission path.

## Principles

- **Producer ≠ verifier.** The person or agent that writes a change does not
  provide its only verification.
- **Claims need artifacts.** A role output links to a diff/commit, command,
  log, screenshot, proof or evidence-manifest claim.
- **Independence is recorded.** State whether the evidence is independent of
  the producer, fresh for the revision, and free of a conflict of interest.
- **Confidence is not authority.** A role may report confidence/volatility
  (see `evidence_manifest.md`); it cannot waive a failing gate, promote an RC
  claim, tag, push or publish.
- **Quiet scope.** Roles report findings; they do not silently broaden the
  task, rewrite unrelated code or clean up another session's work.
- **AI output is a draft.** The human author of record reviews and owns it,
  as required by `ai_policy.md`.

## Roles

| Role | Question | Required output |
| --- | --- | --- |
| **Implementer** | What is the smallest change that satisfies the contract? | Branch/commit, focused commands and results, evidence links, unresolved risks, bus claim |
| **Reviewer** | Is the change correct, scoped, maintainable and consistent? | Findings by severity, file/line evidence, explicit accept/reject recommendation (not the decision) |
| **Tester** | Does the test fail when the behavior is broken? | Positive/negative/boundary/determinism cases, kill evidence, test-gap notes |
| **Verifier** | Can the claimed evidence be reproduced? | Command/log/artifact verification, freshness and independence check, claim-by-claim verdict |
| **Mathematician** | Is the model, invariant or proof obligation true and faithfully stated? | Definitions, assumptions, proof/counterexample, Lean-kernel vs statement-fidelity distinction |
| **Counter-example finder** | Where does the claim break? | Minimal counterexample, triggering command, expected vs actual, retained evidence |
| **Peer reviewer** | Does the surface meet the independent UX/test lenses? | Scores per `peer_review_gate.md`, blockers, evidence |
| **White-hat** | How can this repo-local change be made to misbehave safely? | Reproduction, impact, containment notes, proposed regression test; no exploit development |

## Independence rules

- The implementer cannot be the only reviewer, tester or verifier of the same
  change.
- A verifier must use the recorded command/artifact, not a paraphrase of the
  implementer's summary.
- Correlated evidence (same author, same test, same fixture) is weighted down
  in the evidence manifest **only when the manifest marks it
  `independent: false`**. The tool does not detect correlation automatically;
  the reviewer/verifier must set that flag and explain it.
- A role with a conflict of interest (author, commissioning agent, dependent
  session) discloses it.
- Peer-review lenses are scored independently; do not average them into one
  hidden score.
- The agent bus is advisory: claim paths, post status, release when done, but
  never treat a claim as a lock or cleanup permission.

## Handoff

Every role handoff records:

```text
Role:
Change / claim:
Commit or worktree:
Commands run (exact):
Results / artifact paths:
Evidence-manifest claim id (if any):
Confidence / volatility (if reported):
Independence / freshness / conflicts:
Unresolved risks:
Recommendation (accept / fix / reject / needs evidence):
```

Keep review evidence outside the public tree unless it is intentionally part
of a public report. Do not put credentials, private paths or personal data in
a handoff.

## Suggested flow

1. **Implementer** makes the smallest change on a branch and runs focused
   gates.
2. **Tester** tries to falsify the behavior, including a historical mutation
   or boundary case where practical.
3. **Verifier** reproduces the evidence on a fresh or disposable tree and
   checks freshness/independence.
4. **Reviewer** reviews the diff and the evidence; fixes return to step 1.
5. **Peer reviewer** applies the UX/test lenses for release-facing surfaces.
6. **Counter-example finder / white-hat** attacks the claim within the
   safety contract; any surviving bug becomes a regression test.
7. **Owner** makes the merge/tag/publish decision.

## White-hat scope

White-hat work is repo-local adversarial testing. It may use the disposable
bug-injection harness (`bug_injection.md`) and trusted historical defects. It
must not target external systems, develop exploits, persist, replicate,
exfiltrate data, escalate privileges or write outside the harness scratch tree
and `build/bug-injection/` reports. It is not a security certification; a clean
run means only that the tested claims survived these attempts.

## Evidence and confidence

A role may attach an `evidence_manifest.py` claim with for/against counts,
weights, confidence and volatility. Counts show breadth; weights show strength;
`null`, passive 0 and active 0 stay distinct. A high confidence with low total
weight is still weak evidence and should be reported that way.

## Limits

- These charters do not create independent human review; they structure AI and
  human work.
- Test-tooling and test-behavior changes should record the test-profile scores
  from `peer_review_gate.md` or an explicit exemption with a reason.
- RC gate 1 still requires five recorded **author** sessions on the public
  tree.
- A role recommendation is not a merge, release or security approval.
