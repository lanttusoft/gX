# Worktree and branch tracking

<!-- //ai: shared local inventory and explicit cleanup-review workflow. -->

The [idea ledger](ideas.md) records what was considered and why. A workstream
record connects that idea to its branches, worktrees, verification and eventual
disposition. Neither record authorizes implementation, publishing or deletion.

## Shared local inventory

```bash
python3 scripts/worktree_inventory.py --base main
python3 scripts/worktree_inventory.py --base main --write
```

The first command only reports. The second writes reports under
`<git-common-dir>/gx-workstreams/`, shared by every worktree of this repository:

- `inventory.json` and `inventory.md`: refreshable observations of **all**
  registered worktrees and local branches, including branches without a tree;
- `notes.md`: human/agent-maintained purpose, related idea IDs, lifecycle,
  integration evidence, remaining work and cleanup decision. Refresh never
  overwrites this file. Keep closed/removed entries as history.

Find the common directory with `git rev-parse --git-common-dir`. It is not a
per-worktree `.ai` handoff. Machine paths and branch metadata remain local;
do not copy this inventory into public documentation or release assets.
Separate clones have separate common directories and need separate inventories.

The base is explicit: `main` is an example, not an assumption that every branch
targets it. Reports use local refs, do not fetch, and count commits reachable
only from each side of that base. A directory name is not its branch identity;
always use the branch reported by Git. The script only runs read-only Git
commands; `--write` changes these local report files, not refs, indexes or trees.

## Safe collection and prune planning

`worktree_collection.py` extends the read-only inventory with an explicit
archive and a plan-only prune report. Neither command removes a worktree,
deletes a branch, fetches, resets or cleans:

```bash
# grouped integrated/unmerged/dirty/missing/locked/active/detached view
python3 scripts/worktree_collection.py status --base main

# deterministic archive in a new or empty directory
python3 scripts/worktree_collection.py archive --base main \
  --output /tmp/gx-worktree-archive-$(date -u +%Y%m%dT%H%M%SZ)

# exact commands for clean, integrated candidates; this only prints them
python3 scripts/worktree_collection.py prune-plan --base main
```

The archive contains a full local-branch/tag Git bundle, one bundle per
detached worktree HEAD, tracked/staged binary-safe patches, untracked/ignored
path-and-size listings, worktree status snapshots, the inventory and notes,
active advisory-bus claims, a JSON/Markdown manifest, and a sorted
`SHA256SUMS`. Every bundle is verified with `git bundle verify` before the
archive succeeds. Bundle, patch and listing bytes are deterministic for the
same repository state; the manifest observation time is not.

`prune-plan` emits `git worktree remove` and `git branch -d` only for branches
integrated into the explicit base whose worktrees are clean, unlocked, not
current and free of untracked **and ignored** files, with no active bus claim.
It never emits `--force`, `-D`, `clean` or `reset --hard`. Everything else is a
`REVIEW` line. Commands are quoted for copy/paste but the script does not
execute them; removal and branch deletion remain separate, explicit maintainer
actions after reviewing the archive and notes.

Regression tests use disposable repositories:

```bash
python3 -m unittest discover -s scripts -p test_worktree_collection.py
```

## Parallel sessions

The inventory is the durable record. For live, short-lived coordination between
sessions, use the advisory local bus in
[`agent_coordination.md`](agent_coordination.md): check active claims before
editing, post intent, claim paths, release when done. It is not a lock or a
cleanup authorization, and it does not replace these notes. See
`python3 scripts/agent_bus.py list`.

## Local ecosystem

This inventory covers one Git repository. For separate clones, proof
repositories and lineage/reference trees under the local Git root, use
`scripts/repo_ecosystem.py` (GX-I023, contract in
[`repo_ecosystem.md`](repo_ecosystem.md)):

```bash
# read-only observation of the local root
python3 scripts/repo_ecosystem.py report --root /path/to/local/git-root

# refresh the local snapshot and create ledger.md once
python3 scripts/repo_ecosystem.py report --root /path/to/local/git-root --write

# compare against the previous snapshot without writing
python3 scripts/repo_ecosystem.py drift --root /path/to/local/git-root

# print per-repository GX-I022 archive/prune-plan commands; never executes
python3 scripts/repo_ecosystem.py archive-plan --root /path/to/local/git-root
```

The default local state directory is `<git-common-dir>/gx-ecosystem`, not the
public source tree. `snapshot.json`/`snapshot.md` are refreshable observations;
`ledger.md` is human/agent-maintained and refresh never overwrites it. Remotes
are sanitized, discovery is bounded, unrelated repositories are excluded from
the ecosystem groups, and unknown purpose stays unreviewed. No command fetches,
archives, prunes or deletes. Disposable-root tests:

```bash
python3 -m unittest discover -s scripts -p test_repo_ecosystem.py
```

## Keeping the notes useful

- Before creating a branch/worktree, search the idea ledger and inventory for
  existing work. Reuse it when appropriate instead of starting an unnoticed
  duplicate. Do not assume another tree is inactive.
- When creating, switching, handing off or finishing work, refresh the inventory
  and update that workstream's notes. Link `GX-I###`, the actual branch, relevant
  commits/gates and the intended integration target.
- Use lifecycle labels such as active, paused, integrated or abandoned; leave
  unfamiliar work **unknown / unreviewed**. Do not infer its purpose or completion
  from its age, directory name or commit subject alone.
- Keep main/release/archive trees and branches until their retention purpose is
  explicitly reviewed. A branch with no worktree is still part of the inventory.
- The common Git directory is local, not a backup service. Preserve important
  handoffs elsewhere before removing its repository; export only intentionally,
  after reviewing private paths and metadata.

## Before any cleanup

1. Refresh against the correct integration target; identify the exact tree/ref.
2. Inspect tracked changes, untracked **and ignored** local files, active
   sessions, locks, missing/prunable paths and detached HEAD work. A clean
   `git status` does not mean the directory has nothing valuable.
3. Verify integration or an intentional archive. Zero unique commits is an
   ancestry observation, not proof of equivalent cherry-picks, publication,
   backups or permission to discard local work. Unique commits require review.
4. Record the proposed disposition and get explicit user approval for the exact
   worktree and/or branch. Removing a worktree and deleting its branch are
   separate operations; neither implies the other. Never force away changes.
5. After approved cleanup, refresh and retain a dated note of what happened.

There is deliberately no delete, prune, force or automatic-cleanup option.
Validate the inventory tool without touching real worktrees:

```bash
python3 -m unittest discover -s scripts -p test_worktree_inventory.py
```

These ten display-free tests also run in `make check-fast` and `make check`.
