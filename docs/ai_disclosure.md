# AI usage and limitations

<!-- //ai: public disclosure of AI assistance, review limits and the as-is disclaimer. -->

gX is developed with AI assistance. This page explains what that means, what
it does not guarantee, and what users should check before relying on the code.

## What AI is used for

AI coding assistants, including OpenCode and the models configured for it, have
been used for:

- implementation, refactoring and debugging;
- tests, audit scripts and benchmark harnesses;
- documentation, release text and issue triage;
- author-prompted asset generation (recorded in `license/provenance.json`).

AI-assisted changes are marked in the source with `//ai:` (legacy alias
`//opencode:`), `#ai:` in shell/Makefile files, and `<!-- //ai: -->` in
Markdown. The contribution policy is in [`ai_policy.md`](ai_policy.md).

## Review is risk reduction, not proof

The author owns scope, architecture, licensing, publishing and the final
decision. AI output is treated as a draft: it is reviewed and passes the same
gates as any other contribution. Those gates reduce risk, but they do not
prove that the code is correct, secure, complete or legally clean.

AI tools can make mistakes. They can produce incorrect, insecure, inconsistent
or non-original-looking output; they can miss edge cases; and they can state
things with more confidence than the evidence supports. Reviewers can miss
those problems too. Treat this beta accordingly.

## Disclaimer

gX is provided **"as is"**, without warranty of any kind, under the GNU General
Public License v3.0 or later. Nothing in this repository is legal, security,
fitness-for-purpose or professional advice. You are responsible for reviewing
the code, dependencies, assets and licenses before using gX in production or
distributing it.

AI-generated content may not attract copyright in every jurisdiction. The
project's stance is in [`ai_policy.md`](ai_policy.md) and
[`../license/NOTICE.md`](../license/NOTICE.md): where copyright does not
apply, gX claims no exclusivity. Third-party components remain under their own
licenses.

Report suspected security issues privately per [`../SECURITY.md`](../SECURITY.md).
