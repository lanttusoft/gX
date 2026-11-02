# AI-assisted contributions

<!-- //ai: agent-drafted; maintainer review follows docs/ai_policy.md. -->

gX uses AI as a tool. AI-assisted work is welcome, and it passes the same
review and the same gates as any other change. This page is the short entry
point; the policy is [`docs/ai_policy.md`](docs/ai_policy.md), the agent
workflow is [`AGENTS.md`](AGENTS.md), and the limitations are in
[`docs/ai_disclosure.md`](docs/ai_disclosure.md).

## The rules

1. **A human owns it.** AI output is a draft. The author of record reviews
   it, can explain it, and owns the license and the claims. Scope,
   architecture, visual direction and publishing decisions are human.
2. **Mark it.** Put `//ai:` above AI-authored code, `#ai:` in
   shell/Makefile, or `<!-- //ai: -->` in Markdown. The historical
   `//opencode:` alias stays valid. Mark the change, not every line.
3. **Gate it.** `make check` is admission. Run `--test-only`, `make
   check-fast` and the focused gate for the surface you touched. A green
   local gate beats a confident paragraph.
4. **Record assets.** AI-generated or AI-assisted resources need a
   `license/provenance.json` entry with `"origin": "ai-generated"` and the
   review described in [`docs/ai_policy.md`](docs/ai_policy.md).
5. **Stay in the repository.** No private paths, credentials, personal data,
   proprietary assets or material from other projects. The engine is
   GPL-3.0-or-later and the source tree is public.
6. **Do not publish.** Tags, releases and repository settings belong to the
   maintainer. Do not enable GitHub Actions; local gates are authoritative.
7. **Be honest.** Report the commands you ran and what they returned. Do not
   claim a gate you skipped, and do not present the beta as certified.

## What to expect in review

- The diff should be the smallest change that meets the contract, with no
  unrelated reformatting.
- New public `gX_*` families need a [`docs/api_reference.md`](docs/api_reference.md)
  entry; new enums need their `String()` implementation.
- New resources need provenance and a compatible license.
- Renderer and editor changes need their gate evidence, not only a
  screenshot.
- Generated prose should read like the project wrote it: plain, specific,
  and free of filler.

## License stance

Contributions are accepted under GPL-3.0-or-later. Because some
jurisdictions do not grant copyright for purely AI-generated works, gX claims
no exclusivity over AI-generated assets that ship; to the extent copyright
does not apply, they may be used as if they were public domain. This does not
change the license of the engine or of any other contribution.

The complete asset stance, including the shipped AI-generated resources, is
in [`docs/ai_policy.md`](docs/ai_policy.md) and
[`license/NOTICE.md`](license/NOTICE.md).

## Disclaimer

AI tools make mistakes. Review and the gates reduce the risk; they do not
guarantee correctness, security or legal compliance. gX is provided as-is
under GPLv3; nothing here is legal advice.
