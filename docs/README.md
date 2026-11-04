# gX documentation index

Start here:

- [gX readme](../readme.txt) — what gX is, build, license and controls.
- [Contributing](../CONTRIBUTING.md), [support](../SUPPORT.md), [security policy](../SECURITY.md), [code of conduct](../CODE_OF_CONDUCT.md).
<!-- //ai: public agent entry points. -->
- [Agent guide](../AGENTS.md) — harness-neutral workflow for AI coding
  agents; [AI-assisted contributions](../AI_CONTRIBUTING.md) — the short
  review and provenance checklist.
- [Changelog](../CHANGELOG.md) — released versions and current changes.
- [Versioning policy](versioning.md) — official MAJOR/MINOR/PATCH rules,
  `Release-Class` metadata and the change classifier.
- [Release candidate ledger](release_candidate.md) — the three gates (author
  dogfooding, RC completeness, demand signals) and the completeness rows that
  gate an RC claim.
- [Publish readiness](publish_readiness.md) — the release operations contract:
  admission evidence, packaging, signing, publish, post-publish verification
  and rollback.
- [gX proofs](https://github.com/lanttusoft/gx-proofs) — Lean 4 proof sources,
  audit contract and provenance.
- [AI policy](ai_policy.md) — contribution marking, asset provenance and the
  license stance for AI-assisted work.
- [Peer-review gate](peer_review_gate.md) — three-lens UX/editor review:
  7/10 minimum, 8/10 target, with evidence.
- [AI usage and limitations](ai_disclosure.md) — what AI assisted with,
  review limits and the as-is disclaimer.
- [License](../LICENSE), [third-party notices](../license/NOTICE.md), [generated attributions](../license/ATTRIBUTIONS.md), [provenance source](../license/provenance.json).

## Engine and platform

- [Active-ragdoll lab — body-first contract](active_ragdoll_lab.md)
- [gX — Architecture](architecture.md)
- [Platform support](platforms.md) — Linux supported; Windows beta; macOS source-only
- [gX — audio subsystem](audio.md)
- [gX — Backend Study (L0: what raylib-only means, and what a second target costs)](backends.md)
- [Battle client/server proposal — 2026-10-05](battle_multiplayer_plan.md)
- [gX — Dynamic modules (.so)](dynamic_modules.md)
- [Engine frontend contract — module-owned scene](engine_frontend_contract.md)
- [Engine/game split and the no-assets terminal frontend](engine_game_split.md)
- [Engine-core archive boundary](engine_library.md)
- [Static analysis and type gates](static_analysis.md) — compiler probes,
  libclang type audit and the clang-tidy diff gate.
- [Release hardening](release_hardening.md) — reproducible Release builds,
  SPDX SBOM, tracked-file secret scan and deterministic signed archives.
- [Layered point shadows](layered_shadows.md) — opt-in 4.3 point-shadow
  layering, its exact-image guarantee and measured trade.
- [Scene feature manifest](feature_manifest.md) — the feature table, dependency
  closure, `GX_GFX_PLAN` readout and remaining lazy-target work.
- [UI maturity levels](ui_maturity.md) — Basic/Advanced/Expert settings and
  editor surfaces, the shared metadata table, `GX_UI_LEVEL` and the coverage
  gate.
- [Compressed texture sidecars](texture_compression.md) — BC7/BC5/BC4 DDS
  sidecars, encoder tooling and the loader fallback policy.
- [Source-lineage audit](source_lineage_audit.md) — citation/self-reuse review
  against a private archive or vendored corpus.
- [gX Kernel](kernel.md)
- [Lean size gate](lean_gate.md)
- [Lean 4 proof audit](lean_proofs.md)
- [Lean reassessment — 2026-10-03](lean_reassessment.md)
- [Math pass](math_pass.md)
- [gX — Physics](physics.md)
- [gX — Porting Guide](porting.md)
- [Proof inventory](proof_inventory.md)
- [gX — Scripting (AngelScript shell)](scripting.md)
- [gX — Tier Model (L0–L3)](tiers.md)

## Rendering

- [Bloom pipeline audit](bloom_audit.md)
- [SSR march resolution](ssr_resolution.md) — Full/Half/Quarter quality option
  and its visual/performance budget.
- [Render-target diet](rt_diet.md) — optional VRAM mode for auxiliary targets.
- [gfx shader inventory](gfx_shader_inventory.md)
- [Shader program cache](shader_cache.md)
- [GFX visual review](gfx_visual_review.md)
- [“Good enough” audit — reflections](good_enough_audit.md)
- [Light Culling Upgrade — Research Note (v0.5.0 prep)](light_culling_research.md)
- [Static lightmaps](lightmap_baking.md)
- [Probe box-projection contract](probe_box_contract.md)
- [Raymarch](raymarch.md)
- [gX Reflection Pack](reflection_pack.md)
- [Rendering backlog](rendering_backlog.md)
- [Sky render tests](sky_render_tests.md)
- [SSR coverage and optional second camera](ssr_coverage_contract.md)
- [SSR edge-ray classification audit — 2026-09-23](ssr_edge_ray_2026-09-23.md)
- [Textwriter render tests](textwriter_render_tests.md)
- [gX Visual Direction](visual_direction.md)

## GUI, tools and editors

- [Offline bug reports](bug_reporting.md)
- [Color-code palette-density study](color_code_palette.md) — 4/8/16-colour
  capacity and noise-margin comparison.
- [Screen color-code backlog](color_code_backlog.md) — detection, capacity and
  camera-walk items that remain open.
- [Color-code hardware capture checklist](color_code_hardware.md) — real
  CRT/console/camera validation steps.
- [Screen color-code format](color_code_format.md) — bounded 16-colour/4-bit
  and opt-in 8-colour/3-bit and 4-colour/2-bit encoder/decoder for screen-only
  hardware diagnostics (per-palette calibration, metadata,
  SECDED plus interleaved XOR-parity or RS(18,16) erasure recovery, CRC32, RLE,
  automatic dark-background detection; tint/white-balance and synthetic camera
  tests) plus the standalone `--color-code` / simulated-CRT `--color-code-tv`
  presentation modes and bounded multi-frame reassembly.
- [gX debug colors — magic-color registry](debug_colors.md)
- [gX — editors and tooling surfaces](editors.md)
- [GFX editor rework](gfx_editor_rework.md) — hierarchy, identity, showcase and
  accessibility phases.
- [gX v0.2 Front-End Design Specification](frontend_design.md)
- [GUI Template Audit (v0.1.40)](gui_audit.md)
- [gX GUI Audit - 2026-09-12 (v0.6.32)](gui_audit_2026-09.md)
- [gX GUI Audit v2 — 2026-08-30 (v0.4.0)](gui_audit_v2.md)
- [GUI testing](gui_testing.md)
- [GUI visual backlog](gui_visual_backlog.md)
- [gX — Guest Assets](guest_assets.md)
- [Materials/textures/models - open threads (2026-09-10)](materials_next.md)
- [Theme editor tutorial](theme_editor_tutorial.md)
- [THEMLAB — live design preview](themlab.md)
- [gX — tooling scope](tooling_scope.md)
- [gX — UI audit (layout, discoverability, ease of use)](ui_audit.md)
- [gX — UI battle-scene audit (HUD review)](ui_battle_audit.md)
- [UI diff](ui_diff.md)
- [gX — UI title-scene audit (front-end flows)](ui_title_audit.md)

## Accessibility

- [Accessibility controls](accessibility.md)
- [Visual accessibility audit (low vision focus)](accessibility_visual.md)
- [Analog reduced-motion behavior](analog_reduced_motion.md)
- [Colour-independence audit](colour_independence_audit.md)

## Validation and reference

- [gX — API Reference](api_reference.md)
- [Asset provenance intake reports](asset_clearance.md)
- [Attribution audit — code and systems](attribution_audit.md)
- [gX debugging](debugging.md)
- [Gate memory and audit/test unification](gate_memory.md)
- [gX error handling](error_handling.md)
- [gX error hints — every error carries its recipe](error_hints.md)
- [Hardware requirements](hardware.md)
- [gX — Minimum Hardware Study (measured floor on a reference class)](minimum_hardware.md)

The audit gate matrix is defined in `gates.toml`; each gate documents its own contract in its script header. The cache-key contract, manifest and `--changed` planner are described in [gate_memory.md](gate_memory.md).

## Scratch paths

Audit and benchmark scripts write disposable evidence under
`${GX_SCRATCH:-/tmp/opencode}`. Documents that quote a path below that root
describe a session-local capture, not a shipped resource; set `GX_SCRATCH` to
relocate the scratch tree on another machine.
