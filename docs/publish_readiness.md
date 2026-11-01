# gX publish readiness

Status: **operations contract — draft, not executed.** Executing it is a
deliberate owner action, taken only after
[`release_candidate.md`](release_candidate.md) holds.

This document is the runbook for publishing a gX release. It covers the
repository identity, admission evidence, packaging, tagging, publishing,
post-publish verification and rollback. It is deliberately separate from the
version policy ([`versioning.md`](versioning.md)): versioning decides *what*
number a change gets; this decides *whether and how* a release goes out.

## Preconditions

- [ ] The release-candidate ledger holds all three gates (dogfooding, RC
      completeness, demand signals).
- [ ] The operations checklist below has no open items.
- [ ] Owner go/no-go is recorded in the ledger's decision log.
- [ ] No secrets, private paths, screenshots, settings or guest assets are
      present in the tree or the package.

## Repository and identity

| Item | Contract |
| --- | --- |
| Public repo | `lanttusoft/gX` (engine); proofs in the separate `gx-proofs` repo |
| Owner identity | `lanttu` (owner-signed decisions and tags) |
| Signing | annotated, signed tags; the private key is held by the owner and never enters the tree |
| Credentials | none in the tree; releases use the owner's local credentials only |
| CI | GitHub Actions stays disabled until workflows and pinned actions are reviewed; the local gate is authoritative |
| Private material | release evidence and operator notes live outside the public tree |

The parked Gitea self-host plan does not change this contract; if it is
activated, the same checklist applies with the mirror as an additional
destination.

## Admission evidence (per release)

| Step | Command | Tier |
| --- | --- | --- |
| Format/consistency | `make version-check` on the tagged tree | all |
| Fast admission | `make check-fast` | PATCH |
| Full admission | `make check` | MINOR+ |
| Audit matrix | `make audit-summary-full` | RC / MINOR+ |
| Proofs | `make proofs` (pinned tree) | all |
| Lean gate | part of `make check` | all |
| Clean extraction | build + `--test-only` + `check-fast` + `gates-check` + key gates from the archive | all |
| Resources | resource, provenance, attribution, copyright, script-license audits | all |
| Hardware spot check | `make hw-report-check` + one showcase boot | RC |

PATCH releases may use the fast tier; MINOR and RC releases require the full
tier. A red admission gate stops the release.

## Packaging

1. Package from the release commit, never from a dirty worktree:
   `git archive --format=tar --prefix=gX-vX.Y.Z/ <commit> | gzip -n -9`.
2. Write a `.sha256` sidecar.
3. Scan the member list for private paths, screenshots, settings, `.lake`
   proof caches and anything outside the public contract.
4. Extract the archive into a scratch directory and run the clean-extraction
   smoke from it.
5. Record the archive size, member count and SHA-256 in the release notes.

## Tag and publish

1. Merge the release branch into `main` fast-forward only; never rewrite a
   pushed tag.
2. Create the annotated, owner-signed tag on the release commit; verify with
   `git tag -v`.
3. Push only `main` and the tag.
4. Create a **pre-release** with the notes file; upload the archive and the
   checksum sidecar.
5. Verify the hosted asset digest matches the local sidecar.

## Post-publish verification

From a fresh clone of the tag (no local tree):

- [ ] `./build.sh`, `--test-only`, `version-check`
- [ ] `make check-fast` (and the full tier for MINOR/RC)
- [ ] `make gates-check`
- [ ] the release's headline gates (for example `pbr`, `picker`,
      `title-flow`, `bijection`, `raymarch`)
- [ ] download the asset unauthenticated and `sha256sum -c` the sidecar
- [ ] record the result in the release ledger

## Rollback and withdrawal

- A broken release is **withdrawn**, not silently replaced: mark the release
  withdrawn in the notes and publish a superseding PATCH with the fix.
- A pushed tag is immutable; never move or force-push it.
- If credentials or private data are ever exposed, stop publishing, rotate
  the credential, audit the archive members and record the incident in the
  private operations log before resuming.

## Records

Each release keeps, outside the public tree: the release notes, the archive
and checksum, the admission logs, the clean-extraction logs, the post-publish
smoke logs and the ledger entry. This document is the public contract; the
evidence stays private.

## Sign-off

| Item | Owner | Date |
| --- | --- | --- |
| RC ledger holds | | |
| Operations checklist complete | | |
| Go/no-go recorded | | |
| Tag signed and verified | | |
| Post-publish smoke passed | | |
