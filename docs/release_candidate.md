# gX release candidate ledger

Status: **not a release candidate yet** — ledger opened 2026-11-01 on the
v0.30.0 line.

The public beta publishes PATCH/MINOR prereleases on a regular cadence. The
**release candidate** claim is a separate, deliberate state: it is what
unblocks the parked publish plan. It is granted by three independent gates,
not by a green `make check`.

## The three gates

| # | Gate | Contract | Evidence |
| --- | --- | --- | --- |
| 1 | **Author dogfooding** | 5 recorded sessions using only the public tree (no private assets, no private tooling), each with friction notes and a pass/fail | private dogfood log, one entry per session |
| 2 | **RC completeness** | every row in the completeness ledger below holds with current evidence | this document + linked reports |
| 3 | **Demand signals** | at least 3 recorded, clear signals (issue, external request, or documented evaluation) with a source, a scope and an owner decision | private signal ledger |

A release candidate also requires the operations contract in
[`publish_readiness.md`](publish_readiness.md) to be executed without open
items. Until all three gates and the operations contract hold, gX stays a
**public beta** and makes no RC, silver or gold claim.

## Current snapshot

| Item | Value |
| --- | --- |
| Version | v0.33.0 (beta) |
| Public tree | `lanttusoft/gX`, `main` = release commit |
| License | GPL-3.0-or-later, plus the asset provenance contract |
| Audit baseline | 86 gates (latest: `shader-cache`); last full run 83/83 on `bf873f5` (2026-11-02); the incremental gates are verified individually |
| Proof pin | `gx-proofs` `v0.19.0-proofs`, 60 core / 125 total modules, 1,347 / 3,071 declarations |
| Lean gate | payload 4,118,331 B (75,973 B headroom), BSS 2,698,976 B (6,024 B headroom) |
| Release class | MINOR (v0.33.0: renderer lab panels, shader program cache, public agent docs) |

## Completeness ledger

Status values: **holds** (evidence current), **partial** (works, gaps named),
**open** (not done), **n/a** (not a requirement for this stage).

| Area | Requirement | Status | Evidence |
| --- | --- | --- | --- |
| Source | clean clone builds with `./build.sh`; no private files | holds | clean-extract smoke per release |
| Source | archive reproduces from the release tag | holds | `git archive` + checksum sidecar |
| Admission | `make check` (build, tests, scene smoke, audits, proofs, lean gate) | holds | release CI logs |
| Admission | `make check-fast` on PATCH lines; full on MINOR+ | holds | release CI logs |
| Admission | full audit matrix green (`make audit-summary-full`) | partial | last full run 83/83 on `bf873f5`; the matrix now has 86 gates and the incremental gates pass; a fresh full run on the final commit is the RC execution step |
| Tests | unit suite + contract gates green | holds | `--test-only`, gate manifest |
| Stability | 3 consecutive full admission runs without a flake | holds | 3 x `make check` on 2026-11-01 (v0.30.0 + readiness docs), logs archived privately; the rare test-only SIGSEGV remains a watch item |
| Docs | documentation index covers public surfaces; API/enum coverage gates | holds | `docs/README.md`, `api_coverage.py`, `enum_coverage.py` |
| Docs | release notes + changelog for every release | holds | `CHANGELOG.md`, release notes |
| License | GPL-3.0-or-later; provenance/attribution/copyright audits clean | holds | `provenance_audit.py --strict`, `copyright_audit.py` |
| Assets | no private/gameX assets; no quarantined path tracked | holds | provenance audit + `license/provenance.json` |
| Packaging | no private members, screenshots or settings in the archive | holds | archive member scan per release |
| Hardware | reference-class matrix (NVIDIA/AMD/Intel + llvmpipe) attached | open | hardware walk not yet run |
| Dogfooding | 5 public-tree sessions recorded | open | gate 1 |
| Demand | 3 clear signals recorded | open | gate 3 |
| Security | no credentials; secret-scan/alert review | partial | no known findings; a fresh scan is an RC item |
| Operations | `publish_readiness.md` executed with no open items | open | gate checklist |
| Scope | known limitations documented and accepted | holds | release notes + this ledger |

## Known limitations (accepted for the beta line)

- The global editor host and workspaces are **alpha**; the GFX editor is
  hosted, but its renderer tabs are not split into separate panels yet, and
  user layouts are runtime files.
- The GFX editor ships as **beta**; the rework is complete but the UI is not
  release-grade for every workflow. Every other editor is **alpha/WIP**: a
  usable preview with known gaps (no undo on most, barebones workflows, no
  tutorials); gate coverage is listed in `docs/editors.md`.
- The raymarch renderer is **alpha**: no deferred material buffers or motion
  vectors; cross-renderer shadows/secondary hits and transparent interleaving
  are unsupported.
- The remaining renderer modules are alpha demos except the beta set listed in
  [`architecture.md`](architecture.md) / [`editors.md`](editors.md).
- No silver or gold certification claim is made while the ledger has open
  rows.
- Watch item: a one-off unit-test SIGSEGV that has not reproduced (ASan,
  Valgrind and repeated boots clean).

## Decision log

| Date | Decision | By |
| --- | --- | --- |
| 2026-11-01 | Ledger opened; RC not claimed. Open: full matrix, stability soak, hardware walk, dogfooding, demand signals, secret scan, operations contract. | owner/agent |
| 2026-11-01 | Stability soak: 3 consecutive full `make check` runs green; row moves to holds. | owner/agent |
| 2026-11-02 | Full audit matrix 83/83 on `bf873f5` (2,926.8 s wall). | owner/agent |
| 2026-11-02 | v0.30.1: unit suite opt-in, portable test scratch, non-GFX editors alpha/WIP; affected gates rerun. | owner/agent |
| 2026-11-02 | v0.31.0: editor host, workspaces and cursor state machine; matrix gains `editor-host` and `cursor` (85 gates), both green. | owner/agent |
| 2026-11-02 | v0.32.0: GFX hosted below the bars; scene/inspector/asset/footer panels and per-suite masks; editor-host gate extended. | owner/agent |
| 2026-11-02 | v0.32.1: proof pin bumped to `v0.19.0-proofs` (60/125, 1,347/3,071); no engine changes. | owner/agent |
| 2026-11-02 | v0.33.0: renderer lab panels, shader program cache and public agent docs; matrix 86 gates. | owner/agent |

## Change rule

Update this ledger whenever an item changes state. If a **holds** row
regresses, the RC claim (if any) is withdrawn until it is green again. The
public beta may continue to publish prereleases while rows are open; those
releases must not use RC or certification language.
