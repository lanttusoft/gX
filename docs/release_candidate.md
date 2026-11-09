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
| Version | v0.42.5 (beta) |
| Public tree | `lanttusoft/gX`, `main` = release commit |
| License | GPL-3.0-or-later, plus the asset provenance contract |
| Audit baseline | 120 manifest entries; **107/107** on the v0.42.0 full run (`AUDIT_CACHE=0`, 1,476.2 s wall / 2,908.7 s gate time); v0.42.5 is a PATCH dogfooding fix that passed `make check`, `check-fast`, `gui-editor-check` and `title-flow-check` |
| Proof pin | `gx-proofs` `v0.19.0-proofs`, 60 core / 125 total modules, 1,347 / 3,071 declarations |
| Lean gate | payload 3,929,403 B (264,901 B headroom), BSS 2,672,544 B (32,456 B headroom) — v0.42.5 `make lean` |
| Release class | PATCH (v0.42.5: GUI editor tour scale/clipping, title EXTRAS focus tooltip collision, `Tool UI` label) |

## Completeness ledger

Status values: **holds** (evidence current), **partial** (works, gaps named),
**open** (not done), **n/a** (not a requirement for this stage).

| Area | Requirement | Status | Evidence |
| --- | --- | --- | --- |
| Source | clean clone builds with `./build.sh`; no private files | holds | clean-extract smoke per release |
| Source | archive reproduces from the release tag | holds | `release_archive.py` deterministic double-build + checksum sidecar |
| Source | Release builds reproduce across extraction paths | holds | `make reproducible-check` (two clean trees, fixed epoch) |
| Admission | `make check` (build, tests, scene smoke, audits, proofs, lean gate) | holds | release CI logs |
| Admission | `make check-fast` on PATCH lines; full on MINOR+ | holds | release CI logs |
| Admission | full audit matrix green (`make audit-summary-full`) | holds | **107/107** on the v0.42.0 release tree (2026-11-08, `AUDIT_CACHE=0`, 1,476.2 s wall / 2,908.7 s gate time) |
| Tests | unit suite + contract gates green | holds | `--test-only`, gate manifest |
| Stability | 3 consecutive full admission runs without a flake | holds | 3 x `make check` on 2026-11-08 (`b0fd5f4`, `89aa25d`, v0.42.0 release tree); no flake; the rare test-only SIGSEGV remains a watch item |
| Docs | documentation index covers public surfaces; API/enum coverage gates | holds | `docs/README.md`, `api_coverage.py`, `enum_coverage.py` |
| Docs | release notes + changelog for every release | holds | `CHANGELOG.md`, release notes |
| License | GPL-3.0-or-later; provenance/attribution/copyright audits clean | holds | `provenance_audit.py --strict`, `copyright_audit.py` |
| Assets | no private/gameX assets; no quarantined path tracked | holds | provenance audit + `license/provenance.json` |
| Packaging | no private members, screenshots or settings in the archive | holds | archive member scan per release |
| Hardware | reference-class matrix (NVIDIA/AMD/Intel + llvmpipe) attached | open | hardware walk not yet run |
| Dogfooding | 5 public-tree sessions recorded | open | gate 1 |
| Demand | 3 clear signals recorded | open | gate 3 |
| Security | no credentials; secret-scan/alert review | holds | `secret-scan` quick gate over tracked files + hosted secret scanning |
| Operations | `publish_readiness.md` executed with no open items | open | gate checklist |
| Scope | known limitations documented and accepted | holds | release notes + this ledger |

## Known limitations (accepted for the beta line)

- The global editor host and workspaces are **alpha**; the GFX renderer labs
  are separate floating panels and user layouts are runtime files.
- The GFX editor ships as **beta**; the Phase 1–5 rework, F1/Help guide and
  narrow-layout polish are complete for the standard captures, but the UI is
  not certified for every workflow. Every other editor is **alpha/WIP**: a
  usable preview with known gaps (no undo on most, barebones workflows, no
  tutorials); gate coverage is listed in `docs/editors.md`.
- gX-240 is an original presentation profile, not ROM/console emulation. The
  native GUI stays window-sized (no 320x240 GUI); 2x SSAA is the product
  ceiling and 4x remains a declined probe. Physical CRT/controller validation
  is parked, so no hardware claim is made.
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
| 2026-11-02 | v0.34.0: Windows x86_64 beta (MinGW cross-build, Wine smoke), internal JSON schemas, platform shims; Linux matrix unchanged. | owner/agent |
| 2026-11-02 | v0.34.1: full matrix exposed three lab-focus fixture regressions (83/86); fixed and re-run. | owner/agent |
| 2026-11-02 | v0.35.0: Windows devcube module DLL + `mingw-check` module smoke; Linux matrix unchanged. | owner/agent |
| 2026-11-02 | v0.36.0: Windows AngelScript shell DLL + `mingw-scripting-check`; Linux matrix unchanged. | owner/agent |
| 2026-11-02 | v0.37.0: all 11 renderer labs are panels, preset directories/guest presets, planar reflection strength + mode transitions, reflection lab, large-bell OPX demo. Matrix 87/87 on `a8a524a`. | owner/agent |
| 2026-11-03 | v0.38.0: shared GFX feature closure, LDR-safe scene targets, SSAO off-transition cleanup, threading atomics/OBJ pool, full-matrix dedup. Matrix 84/84 on `abb5bc3`. | owner/agent |
| 2026-11-03 | v0.39.0: optional RT VRAM diet (-23.8%), SSR Full/Half/Quarter, per-pass profiler, compiler/type/lint/sanitizer/TSan/fuzz tooling, cppcheck fixes. Matrix 86/86 on the release commit. | owner/agent |
| 2026-11-03 | Release hardening: reproducible two-tree Release builds, SPDX 2.3 SBOM, tracked-file secret scan, deterministic signed-archive helper. Matrix 88/88 on `c942acc`. | owner/agent |
| 2026-11-04 | v0.40.0: opt-in 4.3 layered point shadows (byte-identical), BC7 orb/testcard sidecars with asset gate, source-lineage audit. Matrix 90/90 on `a46164d`. | owner/agent |
| 2026-11-04 | v0.40.1: cppcheck 198→0 warnings (allocation/null/format/bounds guards; intentional ImGui casts filtered). Matrix 90/90 on `a7aec02`. | owner/agent |
| 2026-11-04 | v0.41.0: scene feature manifest + lazy HDR allocation, GFX Features checklist/templates, Basic/Advanced/Expert settings with expert metadata list. Matrix 90/90 on `13d67e8`. | owner/agent |
| 2026-11-08 | v0.42.0 release prep: MINOR version bump, full admission and 107/107 audit matrix. Packaging/extraction smoke and owner tag/publish pending; no RC claim. Dogfooding, demand, hardware and operations rows remain open. | owner/agent |
| 2026-11-08 | v0.42.1 PATCH prep: AI proxy dogfooding fixes for the GUI editor deferred markup open/tree labels, theme-editor CLI panel, GFX guide bounds, Y2K boot OSD scale and pause hint fit; focused editor/console gates and `check-fast`. No tag/push/publish; no RC claim. | owner/agent |
| 2026-11-09 | v0.42.2 PATCH prep: floating-tool dock gutter, `--tools` missing-value fail-closed parsing and GUI-playground warning noise; focused editor/CLI gates and `check-fast`. No tag/push/publish yet; no RC claim. | owner/agent |
| 2026-11-09 | v0.42.3 PATCH prep: digital-only D-pad free-camera fallback and axis-to-D-pad synthesis for the PS1 Classic adapter, console idle blanking fix against resting non-zero axes, gamepad connect/disconnect + D-pad/axis traces and `GX_GAMEPAD_MAPPING`; input unit suite, console gate, focused gates and `check-fast`. Physical PS1 USB adapter evidence captured locally (D-pad axes 0/1 synthesized to up/down/left/right, Cross 7, Circle 6, Start 15) as a user hardware test, not a gate claim. No tag/push/publish yet; no RC claim. | owner/agent |
| 2026-11-09 | v0.42.4 PATCH prep: GX-I033 BSS recovery by lazy-allocating and freeing the diagnostic-only `GX_UNIFORM_TRACE` cache; 29,728 B BSS reclaimed (2,672,544 B, 32,456 B headroom), +160 B payload, no behavior/cap change. `uniform-trace-check`, `--test-only`, `make lean` and `check-fast` pass. No tag/push/publish yet; no RC claim. | owner/agent |
| 2026-11-09 | v0.42.5 PATCH prep: GX-I034 dogfooding fixes for the GUI editor tour at 100/175%, the title EXTRAS focus-tooltip collision and the `Tool UI` scale label; `gui-editor-check` pins the tour geometry and the title/editor gates pass. Accepted low/medium polish remains for title pause glass, plain-launch hint, OPEN GFX/RUN DEMO duplicate and narrow GUI-editor dock text. No tag/push/publish yet; no RC claim. | owner/agent |

## Change rule

Update this ledger whenever an item changes state. If a **holds** row
regresses, the RC claim (if any) is withdrawn until it is green again. The
public beta may continue to publish prereleases while rows are open; those
releases must not use RC or certification language.
