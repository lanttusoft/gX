# gX documentation index

Start here:

- [gX readme](../readme.txt) — what gX is, build, license and controls.
- [Contributing](../CONTRIBUTING.md), [support](../SUPPORT.md), [security policy](../SECURITY.md), [code of conduct](../CODE_OF_CONDUCT.md).
- [Changelog](../CHANGELOG.md) — released versions and current changes.
- [gX proofs](https://github.com/lanttusoft/gx-proofs) — Lean 4 proof sources,
  audit contract and provenance.
- [AI policy](ai_policy.md) — contribution marking, asset provenance and the
  license stance for AI-assisted work.
- [AI usage and limitations](ai_disclosure.md) — what AI assisted with,
  review limits and the as-is disclaimer.
- [License](../LICENSE), [third-party notices](../license/NOTICE.md), [generated attributions](../license/ATTRIBUTIONS.md), [provenance source](../license/provenance.json).

## Engine and platform

- [Active-ragdoll lab — body-first contract](active_ragdoll_lab.md)
- [gX — Architecture](architecture.md)
- [gX — audio subsystem](audio.md)
- [gX — Backend Study (L0: what raylib-only means, and what a second target costs)](backends.md)
- [Battle client/server proposal — 2026-10-05](battle_multiplayer_plan.md)
- [gX — Dynamic modules (.so)](dynamic_modules.md)
- [Engine frontend contract — module-owned scene](engine_frontend_contract.md)
- [Engine/game split and the no-assets terminal frontend](engine_game_split.md)
- [Engine-core archive boundary](engine_library.md)
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
- [gfx shader inventory](gfx_shader_inventory.md)
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
- [gX debug colors — magic-color registry](debug_colors.md)
- [gX — editors and tooling surfaces](editors.md)
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
- [gX error handling](error_handling.md)
- [gX error hints — every error carries its recipe](error_hints.md)
- [Hardware requirements](hardware.md)
- [gX — Minimum Hardware Study (measured floor on a reference class)](minimum_hardware.md)

The gate matrix is defined in `scripts/audit_summary.py`; each gate documents its own contract in its script header.
