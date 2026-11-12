# gX release candidate ledger

<!-- //ai: RC readiness refresh 2026-11-12: v0.45.0 audit/lean/publish outcomes. -->
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
| Version | v0.45.0 (beta) |
| Public tree | `lanttusoft/gX`; signed `v0.45.0` tag targets `1358c7b`; `main` carries the post-release editor slices through `f57b6bd` plus docs closeouts |
| License | GPL-3.0-or-later, plus the asset provenance contract |
| Audit baseline | 123 manifest entries; PASS — 110/110 gates, 1,623.2 s wall / 3,205.7 s gate time (AUDIT_CACHE=0, no retries) at `f57b6bd`; the v0.45.0 admission was 109/109 at `1358c7b` |
| Proof pin | `gx-proofs` `v0.19.0-proofs`, 60 core / 125 total modules, 1,347 / 3,071 declarations |
| Lean gate | payload 3,961,147 B (233,157 B headroom), BSS 2,692,160 B (12,840 B headroom), assessment D — post-input-owner `make lean` at `f57b6bd` |
| Release class | MINOR (v0.45.0: global Shift+F10 offline report, locked/zoomable GUI editor canvas, lab navigation, preview parity gates; no save/resource format change) |

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
| Admission | full audit matrix green (`make audit-summary-full`) | holds | PASS — 109/109 gates, 1,523.6 s wall / 3,005.1 s gate time (AUDIT_CACHE=0) on the v0.45.0 tree at `1358c7b` (2026-11-12); no retries |
| Tests | unit suite + contract gates green | holds | `--test-only`, gate manifest |
| Stability | 3 consecutive full admission runs without a flake | partial | 3 x `make check` on 2026-11-08 (`b0fd5f4`, `89aa25d`, v0.42.0 release tree); the `7fda31e` matrix needed one shader-cache warm-load retry (146.8 ms vs 143.4 ms; sequential retry PASS 138.3/126.6 ms); `4eadcf9`, the palette matrix at `b1f2fc8`, the panel-registry matrix at `8607a66` and the input-owner matrix at `f57b6bd` were clean. The rare test-only SIGSEGV remains a watch item. |
| Docs | documentation index covers public surfaces; API/enum coverage gates | holds | `docs/README.md`, `api_coverage.py`, `enum_coverage.py` |
| Docs | release notes + changelog for every release | holds | `CHANGELOG.md`, release notes |
| License | GPL-3.0-or-later; provenance/attribution/copyright audits clean | holds | `provenance_audit.py --strict`, `copyright_audit.py` |
| Assets | no private/gameX assets; no quarantined path tracked | holds | provenance audit + `license/provenance.json` |
| Packaging | no private members, screenshots or settings in the archive | holds | archive member scan per release |
| Hardware | reference-class matrix (NVIDIA/AMD/Intel + llvmpipe) attached | open | NVIDIA GTX 1060 6GB re-verified at `81191f6`; llvmpipe verified at `b3cbabd`; AMD/Intel pending |
| Dogfooding | 5 public-tree sessions recorded | open | gate 1 |
| Demand | 3 clear signals recorded | open | gate 3 |
| Security | no credentials; secret-scan/alert review | holds | `secret-scan` quick gate over tracked files + hosted secret scanning |
| Operations | `publish_readiness.md` executed with no open items | open | v0.45.0 runbook executed as a worked example through `scripts/release.py`; RC sign-off pending (operations record) |
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
| 2026-11-10 | v0.42.6 PATCH prep: GX-I035 baseline-controlled mutation pilot (5/5 intended, console candidate blocked), GX-I036/GX-I039 evidence/confidence manifest (38 contract tests), GX-I037 agent role charters + independent reviewer pilot. Full `make check`, `check-fast`, `evidence-manifest-check` and focused tests pass. Opt-in tooling/docs only; no runtime/resource behavior change; not RC evidence. No tag/push/publish yet; no RC claim. | owner/agent |
| 2026-11-10 | v0.42.6 published: signed tag `v0.42.6` (object `cd34928`, target `32b915a`); `main` + tag pushed atomically; GitHub pre-release with archive/checksum, hosted digest and fresh-clone smoke PASS (https://github.com/lanttusoft/gX/releases/tag/v0.42.6). | owner/agent |
| 2026-11-10 | RC readiness refresh at `32b915a`: full audit matrix **107/107** (`AUDIT_CACHE=0`, 1,490.2 s wall / 2,938.2 s gate time), NVIDIA GTX 1060 + llvmpipe hardware re-run PASS, v0.42.6 evidence manifest recorded. Blockers: five author sessions, three demand signals, AMD/Intel hardware (or accepted gap), operations sign-off. No RC/silver/gold claim. | owner/agent |
| 2026-11-10 | v0.42.7 PATCH prep: GX-I034 residual polish fixed at `32cdb03` (opaque title pause panel, timed plain-launch Esc/Back hint, duplicate RUN DEMO removed from the full menu, GUI editor inspector/tree/help at 175%). `title-flow-check`, `gui-interaction-check`, `gui-editor-check`, `gui-pause-blur-check`, `gui-glass-check` and `check-fast` pass. No tag/push/publish yet; no RC claim. | owner/agent |
| 2026-11-10 | v0.42.7 published: signed tag `v0.42.7` (object `371acfd`, target `b3cbabd`); `main` + tag pushed atomically; GitHub pre-release with archive/checksum, hosted digest and fresh-clone smoke PASS (https://github.com/lanttusoft/gX/releases/tag/v0.42.7). | owner/agent |
| 2026-11-10 | RC refresh at `b3cbabd` (v0.42.7): PASS — 107/107 gates, 1,539.0 s wall / 3,034.6 s gate time (AUDIT_CACHE=0), NVIDIA GTX 1060 + llvmpipe hardware re-run PASS, v0.42.7 evidence manifest and owner packet recorded. Blockers: five author sessions, three demand signals, AMD/Intel hardware or accepted gap, operations sign-off. No RC/silver/gold claim. | owner/agent |
| 2026-11-11 | v0.43.0 MINOR prep: RHI/common audit repairs (message-history overflow, storage/FBO/MSAA diagnostics, hashmap, registry, ordered event log) plus clean-checkout console fixtures. Version mirrors/SBOM updated; `version-gate` PASS — 107/107 gates, 1,481.0 s wall / 2,919.0 s gate time (AUDIT_CACHE=0, no retries); reproducible build and deterministic archive PASS. No tag/push yet; no RC claim. | owner/agent |
| 2026-11-11 | v0.43.0 published: signed tag `v0.43.0` (object `476b305`, target `81191f6`); `main` + tag pushed atomically; GitHub pre-release with archive/checksum/signatures; hosted digest `6bb96a8e...343d` verified, fresh-clone and hosted-archive smoke PASS (https://github.com/lanttusoft/gX/releases/tag/v0.43.0). | owner/agent |
| 2026-11-11 | RC refresh at `81191f6` (v0.43.0): PASS — 107/107 gates, 1,481.0 s wall / 2,919.0 s gate time (AUDIT_CACHE=0), NVIDIA GTX 1060 re-verified, hosted archive/signatures verified. Blockers unchanged: five author sessions, three demand signals, AMD/Intel hardware or accepted gap, operations sign-off. No RC/silver/gold claim. | owner/agent |
| 2026-11-11 | v0.44.0 MINOR published: signed tag `v0.44.0` (object `ba9d946`, target `2ebe375`); audit **108/108** gates, 1,536.0 s wall / 3,029.8 s gate time, Lean payload 3,935,707 B (258,597 B headroom), BSS 2,672,576 B (32,424 B headroom); archive `2d6e7921...cbde4`; fresh-clone smoke PASS. The pre-release page appeared after the post-push check; its four assets are present, but no hosted extraction smoke was recorded for this version (https://github.com/lanttusoft/gX/releases/tag/v0.44.0). | owner/agent |
| 2026-11-12 | v0.44.1 PATCH published: signed tag `v0.44.1` (object `e608d6e`, target `462fc40`); cross-editor lifecycle/readability repairs; audit **109/109** gates, 1,519.8 s wall / 2,991.7 s gate time; archive `aa87a3a1...8c9e5`; hosted and fresh-clone smoke PASS (https://github.com/lanttusoft/gX/releases/tag/v0.44.1). | owner/agent |
| 2026-11-12 | v0.44.2 PATCH published: signed tag `v0.44.2` (object `80f6a4f`, target `a97d1cd`); standardized release orchestrator, cumulative evidence bundles and notes; PATCH admission (`check-fast` + focused release tests); archive `8c1b067a...dc21d`; hosted and fresh-clone smoke PASS (https://github.com/lanttusoft/gX/releases/tag/v0.44.2). The tag is immutable; post-tag test fix `6766886` shipped in v0.45.0 rather than rewriting it. | owner/agent |
| 2026-11-12 | v0.45.0 MINOR prep: global Shift+F10 offline report (plain F10 stays console), locked-by-default GUI canvas with wheel zoom/middle pan/Unlock/Fit and restored runtime transform, Theme Lab Back, editor/runtime center-crop parity, cross-editor gate coverage. `release.py` prepared mirrors/SBOM; `version-gate` PASS — **109/109** gates, 1,523.6 s wall / 3,005.1 s gate time (AUDIT_CACHE=0, no retries); reproducible build and deterministic archive PASS. | owner/agent |
| 2026-11-12 | v0.45.0 published: signed tag `v0.45.0` (object `8d2b8c6`, target `1358c7b`); archive `86648ec4...024b9` (22,868,291 B, 1,918 members); hosted digest/signatures and fresh-clone smoke PASS; release body corrected to published (https://github.com/lanttusoft/gX/releases/tag/v0.45.0). | owner/agent |
| 2026-11-12 | RC refresh at `1358c7b` (v0.45.0): PASS — 109/109 gates, 1,523.6 s wall / 3,005.1 s gate time (AUDIT_CACHE=0), Lean payload 3,940,699 B / BSS 2,672,608 B, hosted archive and fresh-clone smoke verified. Blockers unchanged: five author dogfooding sessions, three demand signals, AMD/Intel hardware or accepted gap, operations sign-off. No RC/silver/gold claim. | owner/agent |
| 2026-11-12 | Post-v0.45.0 editor dogfooding: native GUI input now yields to ImGui capture (clicks and arrow keys), canvas wheel/pan stays in the free viewport, the editor toolbar wraps and the status strip scales, the theme panel is inset/width-capped with a panel Back route, the host hides with the last editor, and the report form scrolls with Reset. New quick-tier `editor-input` gate plus `editor-system` overlap/state coverage. `--test-only`, focused gates, `check-fast`, `make check` and the full matrix PASS — **110/110** gates, 1,556.7 s wall / 3,071.7 s gate time (AUDIT_CACHE=0, no retries); Lean payload 3,944,155 B / BSS 2,672,672 B. Commit not tagged/pushed. Remaining `bugs/`: back-to-title fade and GUI text/highlight alignment. No RC claim. | owner/agent |
| 2026-11-12 | GFX asset-browser long-path dogfooding fix at `35164b3`: model paths ellipsize on one line with full-path tooltips instead of clipping at the cell edge; headless `GX_GFX_ASSET_BROWSER` hook and `gfx-editor-layout-check` cell width/height coverage. Full matrix PASS — **110/110** gates, 1,540.0 s wall / 3,036.8 s gate time (AUDIT_CACHE=0, no retries); `make check` PASS, Lean payload 3,944,987 B / BSS 2,672,672 B. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | GX-I043 editor framework slices at `3f26a47`: bounded command registry, shared preset picker (Load opens picker, Reload current, Enter/double-click, ellipsis/tooltip, 175% geometry) and document dirty/load-guard state (modified marker, load warning, save/load clear). Full matrix PASS — **110/110** gates, 1,604.1 s wall / 3,165.5 s gate time (AUDIT_CACHE=0, no retries); `make check` PASS, Lean payload 3,949,947 B / BSS 2,685,504 B. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | Editor close input handoff at `7fda31e`: active-editor-aware host capture, deferred host hide through the invoking menu, live-frame ImGui capture guard and panel-X host close. `editor-input-check` now pins X/host-menu close followed by a title OPEN GFX click. Full matrix PASS — **110/110** gates, 1,541.3 s wall / 3,035.5 s gate time (AUDIT_CACHE=0) with one recorded `shader-cache` warm-load retry (retry PASS); `make check` PASS, Lean payload 3,950,683 B / BSS 2,685,536 B. Stability row now partial. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | Dirty GFX preset close guard at `4eadcf9`: host/GFX close menus veto teardown and open Save and close / Discard and close / Cancel; Save/Discard clear the document, Cancel keeps the editor. `GX_GFX_PRESET_CLOSE` and `gfx-editor-layout-check` pin clean close and dirty prompt. Full matrix PASS — **110/110** gates, 1,592.2 s wall / 3,141.9 s gate time (AUDIT_CACHE=0, no retries); `make check` PASS, Lean payload 3,951,323 B / BSS 2,685,568 B. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | Ctrl+P command palette at `b1f2fc8` (GX-I043): a thin view over the command registry and shared picker, available from any scene and skipping active text input; the host contributes toolbar/report/open-editor commands and an active GFX editor adds its preset commands. The picker accept path now runs callbacks after the popup/child closes and pops its IDs, fixing a crash when a command opened another editor. `editor-input-check` adds palette open/list and double-click run rows. Full matrix PASS — **110/110** gates, 1,574.2 s wall / 3,108.6 s gate time (AUDIT_CACHE=0, no retries); `--test-only`, `check-fast`, api coverage, gates/version checks and Lean (payload 3,954,171 B / BSS 2,685,856 B) PASS. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | Workspace panel registry at `c8e2bd6` (GX-I043): one bounded C table now declares the GFX scene, inspector, asset, footer and eleven renderer labs with get/set callbacks; the active editor menu is generated from it, suites persist a single panel mask as `#v2` lines (legacy lines still parse), **View > Reset Layout** restores the saved mask and clears the per-suite ImGui geometry, and `editor.layout.reset` joins the palette. `GX_EDITOR_STATE_DIR`/`GX_EDITOR_PANEL_TOGGLE`/`GX_EDITOR_LAYOUT_RESET` expose the path headlessly; `editor-system-check` adds registry, round-trip, reset and legacy-parse rows. Follow-up `8607a66` fixed two clang-tidy diff findings in the picker row loop and the GUI refresh path. Full matrix PASS — **110/110** gates, 1,624.5 s wall / 3,205.1 s gate time (AUDIT_CACHE=0, no retries); `make check`, `--test-only`, `check-fast`, `lint-check`, api coverage, gates/version checks and Lean (payload 3,960,379 B / BSS 2,692,128 B; the registry's 24 slots account for +6,272 B BSS, a possible lean follow-up) PASS. No tag/push; no RC claim. | owner/agent |
| 2026-11-12 | Input owner at `f57b6bd` (GX-I043): `gX_EditorInput_*` combines report/picker/host/tool/ImGui probes once per frame with **modal > ImGui > native GUI > game** precedence. Native GUI mouse/keyboard capture, gameplay binds, the character queue and the Ctrl+P text guard consume `CapturesMouse`/`CapturesKeyboard`/`CapturesText`; `GX_EDITOR_INPUT_TRACE=1` logs transitions and `editor-input-check` pins title=game, GFX=imgui/tools, report=modal and picker=modal. The remaining per-tool probes and canvas wheel/pan capture migrate incrementally. Full matrix PASS — **110/110** gates, 1,623.2 s wall / 3,205.7 s gate time (AUDIT_CACHE=0, no retries); `make check`, `--test-only`, `editor-input-check`, `editor-system-check`, `check-fast`, `lint-check`, api coverage and Lean (payload 3,961,147 B / BSS 2,692,160 B) PASS. No tag/push; no RC claim. | owner/agent |

## Change rule

Update this ledger whenever an item changes state. If a **holds** row
regresses, the RC claim (if any) is withdrawn until it is green again. The
public beta may continue to publish prereleases while rows are open; those
releases must not use RC or certification language.
