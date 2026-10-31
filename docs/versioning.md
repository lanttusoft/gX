# Versioning

Status: **official policy** (2026-10-31). This document is the contract for gX
version numbers, release classes and validation gates.

## Format

Versions use `vMAJOR.MINOR.PATCH[_W]`:

- `MAJOR.MINOR.PATCH` is the SemVer core.
- `_W` is a variant or pre-release suffix (`_rc1`, `_beta2`, `_hotfix`);
  it keeps the numeric core and uses the minimal gate.
- Tags are annotated and signed. The tag target is the release commit.
- `readme.txt` is the canonical version. `README.md`, the latest
  `CHANGELOG.md` section, `src/common/data.c`, `src/gui/templates/titlegx.c`
  and `src/gui/templates/settings.c` mirror it.

`make version-check` enforces the mirrors and the tag relationship.

## Change classes

| Class | Bump | Rule |
| --- | --- | --- |
| `major` | MAJOR | Incompatible public API/ABI, save/format, CLI or contract change; removal of a documented feature. While gX is `0.x`, a breaking change is released as a **MINOR** bump and flagged in the changelog; `1.0.0` is reserved for the first stable contract. |
| `minor` | MINOR | Backward-compatible new public API, format, mode, capacity, CLI flag or user-facing capability. |
| `patch` | PATCH | Backward-compatible fix, diagnostic, documentation, tooling, build or performance change with no new public contract. |
| `variant` | `_W` | Pre-release/variant of the same code baseline; no numeric bump. |

Over-bumping is allowed. Under-bumping is not: a release whose actual changes
are MINOR must not ship as PATCH.

### gX-specific rules

- New opt-in color-code palette/ECC/capacity modes, new public `gX_*`
  functions, new CLI flags and new save/format fields are MINOR.
- New user-facing features, including diagnostic presets and editor
  conveniences, are MINOR. Docs, scripts, tests and build changes are PATCH.
  v0.24.1 (empty scene) was released as PATCH under the pre-policy
  small-diagnostic rule; under this policy it would be MINOR.
- A bug fix that changes observable behavior is PATCH unless it adds a new
  public capability.
- When in doubt, bump higher and explain the decision in the changelog.

## Release-Class metadata

The latest `CHANGELOG.md` section declares the intended class directly below
the version header:

```markdown
## [0.26.0] - 2026-10-31

Release-Class: minor

### Added
...
```

`make version-check` validates that the declaration matches the numeric bump
relative to the previous tag. Historical releases before 0.26.0 are exempt
from the check, but the recent sections carry the metadata for reference.

## Classifier

`scripts/version_suggest.py` inspects the actual change rather than trusting
the number:

- `git diff` file additions, public header declarations and CLI flags;
- commit subjects (`feat:`, `fix:`, `docs:`, `BREAKING CHANGE:`);
- the current `CHANGELOG.md` section categories (`Added`, `Changed`, `Fixed`,
  `Removed`, `Deprecated`, `Security`).

```bash
make version-suggest                 # latest tag -> HEAD
make version-suggest FROM=v0.25.0 TO=HEAD
python3 scripts/version_suggest.py --from v0.25.0 --to HEAD --json
```

In a source archive without git history, the classifier falls back to the
current `CHANGELOG.md` section. The output is advisory. `--check` rejects an
under-bump:

```bash
python3 scripts/version_suggest.py --from v0.25.0 --to HEAD --check minor
```

`make version-gate` runs the classifier with the declared class before the
numeric tier, so a PATCH declaration cannot hide a MINOR change.

## Gates

| Class | Tier | Commands |
| --- | --- | --- |
| `major` / `minor` | full | `make check` + `make audit-summary-full` |
| `patch` | fast | `make check-fast` |
| `variant` | minimal | `./build/gX_build --test-only` + `python3 scripts/provenance_audit.py --strict` |

The public beta may have known baseline reds in the full audit matrix. A
release candidate may be held with documented targeted evidence, but it must
not claim silver or gold while the public baseline is red. The release
handoff records the exact gates that ran.

```bash
make version-check
make version-suggest FROM=v0.25.0 TO=HEAD
FROM=v0.25.0 TO=v0.26.0 make version-gate
```

## Release steps

1. Choose the class from the actual change and record it as `Release-Class`.
2. Update `readme.txt`, `README.md`, `CHANGELOG.md`, the runtime string and
   both UI labels.
3. Run `make version-check` and the class-appropriate gate.
4. Package the source archive, record its SHA-256, scan it and smoke-test a
   clean extraction.
5. The owner signs and pushes the annotated tag, publishes the prerelease and
   runs the post-publish smoke.
6. Update the private release ledger with tag object, target commit, asset
   SHA-256 and post-publish result.

## History

- `v0.24.0` — MINOR: opt-in color-code ECC and clutter detection.
- `v0.24.1` — PATCH: empty-scene diagnostic preset; historical exception,
  now MINOR under this policy.
- `v0.25.0` — MINOR: opt-in 8-colour/3-bit color-code mode.
- `v0.26.0` — MINOR: multi-frame payload cap raised to 56 KiB.
