# Local gX repository ecosystem

<!-- //ai: proposed multi-repo discovery/ledger; no implementation or cleanup authorization. -->

Status: **proposed (GX-I023)**. This is a contract for a future read-only
inventory, not a claim that the tool exists.

## Why

The per-repository inventory in
[`worktree_tracking.md`](worktree_tracking.md) answers “what branches and
worktrees does *this* repository have?” It does not answer:

- which local directories are separate clones of the same upstream;
- which are worktrees of one common Git directory;
- which are the proof repositories;
- which are lineage/reference repositories or unrelated projects;
- which local clones have diverged, are dirty or are safe to archive.

The local Git root can contain several repositories with the same remote, a
primary engine checkout with many worktrees, a proof checkout and older
lineage/reference trees. Tracking them by directory name alone is unsafe.

## Discovery

A read-only helper would scan a configurable root (`GX_REPO_ROOT`, defaulting
to the directory that contains the current checkout) with a bounded depth and
skip `.git`, build/output, vendor and package directories. It records only
directories that Git identifies as a repository or worktree; a directory name
containing “gX” is a hint, not proof.

A repository is classified as:

- **engine**: gX engine markers/remotes (for example `AGENTS.md`,
  `gates.toml`, `proofs.lock.json`, `src/`, `resources/style/theme.json`);
- **proofs**: the pinned proof corpus or a clone of it;
- **lineage/reference**: older gameX-family trees, retained for history;
- **candidate/unreviewed**: plausible but not confirmed;
- **unrelated**: not part of the gX ecosystem.

Unknown directories stay unknown. Do not infer purpose or cleanup status from
a path name or age.

## Identity and grouping

Use Git identity, not directory names:

- `common_dir` groups worktrees of one repository;
- sanitized remote URL groups separate clones of the same upstream;
- a clone and a worktree are different lifecycle objects even if their branch
  names match;
- branches without worktrees remain in the per-repository report.

For each repository snapshot:

```text
identity: common dir, sanitized remote(s), repo kind, known aliases
state:    current branch/HEAD, worktree list, local branches/tags,
          dirty/untracked/ignored state, unique commits vs chosen base,
          last commit time, proof pin and release/archive state
activity: active agent-bus claims/messages, recent session notes
review:   purpose, lineage, integration target, archive/prune disposition
```

## Local ledger

The ledger is local machine state, never committed and never copied into
public docs or release archives. A configurable directory
(`GX_ECOSYSTEM_DIR`) holds:

- `snapshot.json` and `snapshot.md`: refreshable observations;
- `snapshots/<UTC>.json`: optional timestamped history for drift checks;
- `ledger.md`: human/agent-maintained purpose, lineage, integration target and
  archive/prune decisions. Refresh never overwrites it.

The default location should be under the local Git root or the primary
repository’s common directory, not inside the public source tree. Machine
paths, private remotes and user data stay local.

## Safety

- Read-only Git by default; no fetch, no network, no credential-bearing URLs.
- Sanitize remotes before printing (strip userinfo/tokens).
- Never delete, prune, clean, reset or force anything; this tool only reports.
- Refuse to label a repository safe merely because it is clean or zero commits
  behind a base.
- Active agent-bus claims/sessions make a tree “active” until released.
- Skip symlink loops, huge vendor/build trees and inaccessible paths with a
  clear “skipped/unavailable” state.
- Separate clones need their own common directories; do not merge identities
  just because paths share a parent.

## Relationship to the other tools

- `worktree_inventory.py` remains the per-repository read-only inventory.
- GX-I022 (branch/worktree collection, archive and prune-plan) operates on one
  repository and produces bundles/patches/manifests; the ecosystem ledger
  links those results to the repository identity.
- GX-I021 (agent bus) supplies active-session claims; the ecosystem tool reads
  them read-only and does not create claims for directories it merely scans.
- Archive/prune remains an explicit, reviewed maintainer action; the
  ecosystem tool may produce a plan but never executes it.

## Planned commands

```text
# read-only report over the local root
repo-ecosystem report --root <local-git-root> [--json]

# write snapshot.md/snapshot.json and create ledger.md if absent
repo-ecosystem report --root <local-git-root> --write

# compare a new snapshot with the previous one
repo-ecosystem drift --root <local-git-root>

# link per-repository archive/prune plans (GX-I022)
repo-ecosystem archive-plan --root <local-git-root>
```

The exact executable name is intentionally not fixed in this contract.

## Tests

Use disposable roots containing:

- one repository with two worktrees;
- a separate clone of the same remote;
- a proof-style repository;
- a lineage-style repository with no remote;
- an unrelated repository whose name merely contains “gX”.

Assert grouping, worktree-vs-clone identity, remote sanitization, exclusion of
unrelated trees, preservation of `ledger.md`, and that no ref, index, worktree
or file is modified by a report or drift run.

## Non-goals

- No network-wide discovery, GitHub API calls or automatic fetching.
- No automatic cleanup, pruning, archiving or deletion.
- No public machine-path inventory.
- No replacement for human review, `worktree_inventory.py` or the idea ledger.
