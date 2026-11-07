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

## Parallel sessions

The inventory is the durable record. For live, short-lived coordination between
sessions, use the advisory local bus in
[`agent_coordination.md`](agent_coordination.md): check active claims before
editing, post intent, claim paths, release when done. It is not a lock or a
cleanup authorization, and it does not replace these notes. See
`python3 scripts/agent_bus.py list`.

## Local ecosystem

This inventory covers one Git repository. For separate clones, proof
repositories and lineage/reference trees under the local Git root, see
[`repo_ecosystem.md`](repo_ecosystem.md) (proposed GX-I023). It is a read-only
discovery/ledger contract and does not authorize cleanup.

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
