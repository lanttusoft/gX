# gX — AI policy

<!-- //ai: drafted from the author conversation (2026-10-08); the author owns this policy. -->

gX uses AI as a tool. Everything that ships has a human author of record:
AI output is a draft until the author reviews it and owns it. This document
is the policy; the marker mechanics live in the harness marker conventions
and the engineering contract stays in the repository contribution guide.

> **Limitations:** AI tools can make mistakes. Review and the gates reduce
> risk, but do not guarantee correctness, security or legal compliance. See
> [`ai_disclosure.md`](ai_disclosure.md); gX is provided as-is under GPLv3.

## Principle

- The author decides. Scope, architecture, visual direction, and licensing
  and publishing decisions are not delegated; AI may propose, implement, and
  argue for an approach, but the decision is human.
- AI-assisted work is not a separate class of contribution. It passes the
  same review and the same gates as anything else — admission is `make check`,
  never the fact that a tool wrote the change.
- No task-level allow/deny lists. As AI capabilities change, the constants
  are review, provenance, and the gates — not the tooling of the day.

## Working rules

- **Mark** AI-authored changes: `//ai:` (legacy alias `//opencode:`), `#ai:`
  in shell/Makefile, `<!-- //ai: -->` in markdown. Marking keeps the project
  auditable for the public release; details in the harness marker conventions.
- **Review** before landing. AI output is read by a human and passes the
  applicable gate; no unreviewed change lands.
- **Write like a person.** AI-drafted prose drops the telltale generated
  patterns — filler, stacked rhetorical constructions, false profundity —
  and says only what the project needs.
- **Record assets.** An AI-generated or AI-assisted resource gets an entry
  in `license/provenance.json` with `"origin": "ai-generated"`; the intake
  flow is [`asset_clearance.md`](asset_clearance.md).
- **Respect the isolation rule.** Nothing from gameX's experiments enters
  gX; resource files from outside the repo go through the clearance flow.

## AI-generated content that ships

AI-generated content is not banned, but it is never anonymous: it earns its
place like any other asset and is identified in `license/NOTICE.md` and
`license/provenance.json`. The current assets are author-prompted
generations (the author directed the output):

| Resource | What it is |
| --- | --- |
| `resources/model/props/tubetech_vt.glb` | tube-tech prop for the raymarch/geo demos |
| `resources/model/primitive/lightmap_fixture.glb` | secondary-UV static-lightmap fixture (opt-in GFX validation) |
| `resources/texture/lightmap_fixture.png` | lightmap fixture texture |
| `resources/texture/cookie_window.png` | window-grid spotlight light cookie |
| `resources/texture/misc/scorch.png` | impact-scorch decal |
| `resources/audio/wavetable/*.f32` | additive wavetable fixtures for the synth's file loader (`scripts/gen_wavetables.py`) |

**License stance (decided 2026-10-08).** These files ship under gX's GPLv3
like the rest of the project. Because some jurisdictions do not grant
copyright for purely AI-generated works, gX claims no exclusivity over them:
to the extent copyright does not apply, they may be used as if they were
public domain. The fallback removes licensing ambiguity for downstream users;
it does not change the license of anything else here.

Applied 2026-10-08: the statement is in `license/NOTICE.md` and the
provenance entries carry `"publish": "keep"`.

## Changing this policy

The author revises this document deliberately. It stays principle-based: if a
specific AI failure mode shows up repeatedly, it becomes a rule here;
otherwise the review and gate process owns it.
