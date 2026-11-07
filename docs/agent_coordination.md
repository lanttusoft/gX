# Cross-session agent coordination

<!-- //ai: advisory local bus for parallel sessions; not a lock or security boundary. -->

More than one agent session can use the same Git repository at the same time,
often in different worktrees. They can share the Git index, refs, resources,
scratch directories and GPU/display. This document defines a small, local,
file-based coordination layer so sessions notice each other before they race.

It is **advisory**: it reduces accidental overwrites and makes intent visible.
It is not a hard lock, a sandbox or a security boundary. A process that does
not use the bus can still write. Keep the durable workstream record in
[`worktree_tracking.md`](worktree_tracking.md) and its `notes.md`; the bus is
ephemeral.

## Where the bus lives

By default:

```text
<git-common-dir>/gx-agent-bus/
  bus.jsonl      append-only intent/status messages
  claims.json    active advisory path claims with expiry
  lock           flock target for atomic reads/writes
```

`git rev-parse --git-common-dir` is shared by every worktree of one clone.
Separate clones have separate buses; point `GX_AGENT_BUS` (or `--root`) at a
shared directory if they need to coordinate. The directory is local state:
never commit it and never put credentials, personal data or private paths in a
message.

## Commands

```bash
# Read the current claims/messages before editing.
python3 scripts/agent_bus.py list

# Announce intent (paths are informational; quote globs).
python3 scripts/agent_bus.py post \
  --intent "Y2K reflection review" \
  --path 'src/renderer/gfx/**' --path docs/console_240p.md

# Claim the paths you will edit (advisory, default 240 minutes).
python3 scripts/agent_bus.py claim \
  --path 'src/renderer/gfx/**' --note "single-owner reflection fix"

# Check a path before touching it; exit 1 means another session holds it.
python3 scripts/agent_bus.py check --path src/renderer/gfx/scene.c

# Release when done, or prune expired claims deliberately.
python3 scripts/agent_bus.py release --all
python3 scripts/agent_bus.py prune

# Machine-readable state for a harness or subagent prompt.
python3 scripts/agent_bus.py list --json
```

The `--session` value defaults to `GX_AGENT_SESSION`,
`OPENCODE_SESSION_ID`, or `unknown-<pid>`. The worktree and branch default to
the current Git checkout; `--worktree` and `--branch` override them.
`GX_AGENT_BUS` selects a non-default bus root.

Claims support exact paths and subtree globs ending in `/**`. Two claims
overlap when they cover the same path. A conflicting `claim` exits 1 and
prints the owner; `--allow-overlap` is an explicit override. Expired claims
are ignored by `check` and shown as inactive by `list`; `prune` removes them.
Messages are append-only and tolerant of an interrupted writer (malformed
lines are skipped); writes use `flock` and atomic temp/rename for
`claims.json`.

## Session rules

1. **Before editing:** run `git status`, `agent_bus.py list`, and the
   workstream inventory. Do not assume another worktree is inactive.
2. **Unknown files are not yours.** A modified, untracked or ignored file may
   belong to another session. Never `git add -A`, `git add .`, commit, delete,
   clean, reformat or stage it without explicit approval.
3. **Claim and post intent** for the paths you will touch; release when done.
   Keep claims short-lived and specific.
4. **Do not run destructive Git operations in a shared worktree.** No
   `reset --hard`, `clean`, branch deletion, worktree prune or forced checkout
   without the maintainer's explicit approval for the exact tree/ref.
5. **Use unique scratch paths** (`${GX_SCRATCH:-/tmp/opencode}/<session>/`) and
   coordinate GPU/display-heavy gates; concurrent captures can invalidate
   timing and pixel evidence.
6. **Record durable outcomes** in `docs/worktree_tracking.md` / `notes.md`,
   the idea ledger and the changelog. The bus is not a substitute for review
   or release notes.
7. **Subagents** should receive the bus root and their session ID; the parent
   posts the child's intent and records the claim. A subagent working in a
   disposable copy does not need a claim on the source tree, but it must not
   write the source tree.

## What it does not solve

- It is not a chat server or a daemon; there is no live notification.
- It cannot stop a process that ignores it.
- It does not make independent clones share state unless `GX_AGENT_BUS` points
  at the same directory.
- It does not authorize cleanup, deletion or publishing.
- It does not replace human review, tests or the worktree inventory.

## Tests

```bash
python3 -m unittest discover -s scripts -p test_agent_bus.py
```

The display-free tests cover atomic concurrent posts, claims/conflicts,
expiry/prune, release and scope matching. They also run in `make check-fast`
and `make check`.
