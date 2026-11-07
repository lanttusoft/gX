# Local gX repository ecosystem

<!-- //ai: implemented multi-repo discovery/ledger; no cleanup authorization. -->

Status: **implemented (GX-I023)** in `scripts/repo_ecosystem.py`. The tool is
read-only by default and writes only an explicit local snapshot/ledger under
the configured ecosystem directory. It never archives, prunes or deletes.

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

The helper scans a configurable root (`GX_REPO_ROOT`, defaulting to the parent
of the primary checkout) with a bounded depth (`--max-depth`, default 4) and
skips `.git`, build/output, vendor and package directories. It records only
directories that Git identifies as a repository or worktree; a directory name
containing “gX” is a hint, not proof. Registered worktrees of a discovered
repository are added through `git worktree list`, so discovery does not need to
walk into every checkout.

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

Repository and branch lifecycle are independent. A repository can be retained
as lineage/reference while its default branch is deprecated; a stale `master`
does not make newer release or feature branches disposable. Record lifecycle
per branch/worktree (“active”, “paused”, “deprecated”, “archived”, “unknown”)
and do not infer it from the default branch alone. Deprecated or archived means
“preserve and label”, not “delete”: unique commits, tags, worktree dirt and
unmerged patches remain evidence until an explicit maintainer decision.

For each repository snapshot:

```text
identity: common dir, sanitized remote(s), repo kind, known aliases
state:    current branch/HEAD, worktree list, local branches/tags,
          dirty/untracked/ignored state, unique commits vs chosen base,
          last commit time, proof pin and release/archive state
activity: active agent-bus claims/messages, recent session notes
lifecycle: per-branch/worktree active/paused/deprecated/archived/unknown
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

## Commands

```bash
# read-only report over the local root
python3 scripts/repo_ecosystem.py report --root <local-git-root> [--json]

# write snapshot.md/snapshot.json and create ledger.md if absent
python3 scripts/repo_ecosystem.py report --root <local-git-root> --write

# also retain an immutable timestamped observation for drift history
python3 scripts/repo_ecosystem.py report --root <local-git-root> \
  --write --history

# compare a new observation with the previous snapshot
python3 scripts/repo_ecosystem.py drift --root <local-git-root>

# print per-repository archive/prune-plan commands (GX-I022); plan only
python3 scripts/repo_ecosystem.py archive-plan --root <local-git-root>
```

The local directory defaults to `<git-common-dir>/gx-ecosystem` and can be
overridden with `GX_ECOSYSTEM_DIR` or `--ecosystem-dir`. `ledger.md` is created
once and never overwritten by a refresh. `drift` and `archive-plan` do not
write a snapshot or create archive directories; `archive-plan` prints exact
commands for explicit review and never executes them.

## Tests

`scripts/test_repo_ecosystem.py` uses disposable roots containing:

- one repository with two worktrees;
- a separate clone of the same remote;
- a proof-style repository;
- a candidate repository with partial engine markers;
- a lineage-style repository with no remote;
- an unrelated repository whose name merely contains “gX”.

The tests assert grouping, worktree-vs-clone identity, remote sanitization,
exclusion of unrelated trees, preservation of `ledger.md`, drift reporting,
archive-plan linkage, depth/vendor/symlink skips, active-claim marking, and
that no ref, index, worktree or file is modified by a report, drift or
archive-plan run.

```bash
python3 -m unittest discover -s scripts -p test_repo_ecosystem.py
```

## Non-goals

- No network-wide discovery, GitHub API calls or automatic fetching.
- No automatic cleanup, pruning, archiving or deletion.
- No public machine-path inventory.
- No replacement for human review, `worktree_inventory.py` or the idea ledger.
