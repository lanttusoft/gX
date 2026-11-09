# Historical bug injection / mutation pilot

<!-- //ai: GX-I035 mutation and deliberate-bug corpus contract. -->

Status: opt-in pilot (2026-11-10). This is a **test-sensitivity** tool, not a
release gate yet.

## Contract

- The working tree is never mutated. Each entry is applied to a disposable
  `git archive HEAD` tree under `/tmp/opencode` and removed afterwards.
- An entry is **killed** only when the named command exits non-zero **and**
  matches the entry's `expect_regex`. A passing command is a **survivor**; a
  failure without the expected marker is **unexpected** (build/runtime issue).
- Injected code never merges, publishes, or supplies release evidence. A
  persistent `experiments/bugs` worktree may be used for interactive
  observation with explicit owner approval, but it is not a release branch.
- `--keep` leaves the disposable tree and prints its path for inspection. Do
  not commit it.
- The base is `HEAD`; run from a clean tracked tree (or pass
  `--allow-dirty`, which still tests `HEAD`, not the dirty files).

## Usage

```bash
make bug-injection-check
python3 scripts/bug_injection_check.py --list
python3 scripts/bug_injection_check.py --only dpad-wrong-index
python3 scripts/bug_injection_check.py --keep --only tour-next-row
```

Reports are written to `build/bug-injection/report.json` and
`report.md`; per-entry build/gate logs sit beside them. The script exits 0 only
when every selected entry is killed.

## Manifest

`scripts/bug_injection_manifest.json` is the corpus. Entries are either:

```json
{
  "id": "historical-example",
  "source": "gX",
  "type": "reverse_commit",
  "fix_commit": "abcdef1",
  "paths": ["src/file.c"],
  "command": ["python3", "scripts/check.py"],
  "expect_regex": "CHECK: FAIL",
  "timeout": 900
}
```

or a targeted mutation:

```json
{
  "id": "targeted-example",
  "source": "gX",
  "type": "replace",
  "path": "src/file.c",
  "old": "exact text that appears once",
  "new": "the buggy replacement",
  "command": ["./build/gX_build", "--test-only"],
  "expect_regex": "TEST_ONLY: [1-9][0-9]* errors",
  "timeout": 600
}
```

`reverse_commit` applies the reverse of a fix diff reachable from the base. It
is best for fixes whose old code still links against the current tree; if later
dependencies changed, use a targeted `replace` that preserves the current API.

## gameX-era corpus

Historical entries may come from the gameX lineage as well as gX. Record
`"source": "gameX"` and a note explaining the mapping. gameX paths/APIs often
differ; prefer a targeted mutation that reproduces the same failure mode in the
current source, or a patch that still applies cleanly. Do not import private
paths, assets or unrelated history into the public manifest.

## Pilot entries

- `dpad-wrong-index` — the old D-pad fallback indexes; `--test-only`.
- `console-idle-zero-baseline` — the old absolute-axis idle check;
  `console-shell-check`.
- `tour-scale-fixed` — fixed-size GUI editor tour at 175%;
  `gui-editor-check`.
- `tour-next-row` — unconditional `SameLine()` after a skipped Back button;
  `gui-editor-check`.
- `tangent-lineage-reverse` — reverse the gX tangent-boundary fix (gameX-era
  lineage); `--test-only` catches the old read past an indexed quad. This is a
  contained **crash kill** (`expect_crash`), not a clean assertion.
- `float-uniform-as-int` — gameX R-2 class: float uniforms declared as int.
  `uniform-trace-check` emits named `UNIFORM_MISMATCH` lines; match those
  rather than a final FAIL summary.

## Pilot result (2026-11-10)

All six entries were killed, none survived. `INPUT 10 FAIL`,
`tour width 420 did not scale` and `Skip button clipped` hit the intended
assertions. The console-idle entry was an **indirect kill**:
`console-shell-check` failed earlier on a focus-target mismatch rather than the
idle timeout; record that as gate-ordering debt. The tangent-lineage entry
aborts with `free(): invalid size`; the harness records it as an expected crash
kill, and the test should ideally assert cleanly instead of aborting. The gfx
float/int entry produced **8,010 named `UNIFORM_MISMATCH` lines**; the trace
gate has no final FAIL summary, so the manifest matches the named mismatch
line.

## Safety

This is a containment harness for trusted, curated defects, **not a security
sandbox**.

- Each entry runs in a disposable `git archive` tree with no `.git`; the live
  worktree is never mutated.
- Commands are argv lists, never a shell. Shells and dangerous host binaries
  (`rm`, `dd`, `mkfs`, `sudo`, `kill`, network clients, mount/reboot tools) are
  rejected; command paths must be relative.
- Child processes start in a new session; timeouts kill the whole process
  group; core dumps are disabled. Writes stay in the temp tree and the
  harness's scratch/report directories.
- No network access, privilege escalation, persistence, replication or
  exfiltration is part of the contract. Injections model accidental defects,
  not malware.
- A `--keep` tree is untrusted mutated source; inspect it, but do not merge or
  execute it outside the harness.

## Limits

- Survivors mean the gate/test did not detect that mutation; they are findings,
  not proof that the code is correct or incorrect.
- A mutation can be killed for the wrong reason (for example, a missing trace
  rather than the intended assertion). Read the gate log.
- The pilot does not measure mutation score over the whole codebase; entries
  are curated. Expand only after the pilot findings are reviewed.
