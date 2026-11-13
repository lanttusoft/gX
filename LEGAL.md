# License and provenance concerns

<!-- //ai: public response policy for license/provenance/attribution concerns. -->

gX is licensed GPL-3.0-or-later and ships only code, assets and generated
content the project can license. This page describes how to raise a good-faith
license, provenance, attribution or trademark concern, and what the project
will try to do about it.

This is a project process, **not legal advice**, and it does not promise a
response time, a particular outcome or any admission of liability. The
maintainer decides whether and how a concern is addressed. If you need legal
advice, consult a qualified lawyer.

## Reporting a concern

Do **not** open a public issue that reproduces copyrighted, proprietary or
private material. Use GitHub's private reporting channel for this repository
when it is enabled, or contact the maintainer privately through the repository
owner account.

Include, where possible:

- the affected file, path, URL, commit/tag and release;
- the right or license you believe is implicated, and why;
- the smallest evidence needed to understand the concern;
- your contact details and whether you want the report kept confidential.

Remove credentials, personal data, private paths and unrelated private assets.
Do not send material you do not have the right to share.

- Suspected security vulnerabilities go to [`SECURITY.md`](SECURITY.md).
- Conduct reports go to [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
- General support goes to [`SUPPORT.md`](SUPPORT.md).

## Scope

This process covers source code, shaders, models, textures, fonts, audio,
data, presets, generated assets, notices and provenance records, and
trademark or endorsement concerns about project naming and references. It is
not a general legal-dispute channel.

## What happens next

1. **Acknowledge** the report privately on a best-effort basis.
2. **Preserve** the report and its evidence; avoid reposting disputed material
   in public.
3. **Triage** the provenance, license, notice and release trail. The maintainer
   may quarantine the material from `main`, builds or release artifacts while
   the review is open.
4. **Resolve** by removing, replacing, attributing, relicensing with permission
   or clean-room reimplementing the material. Update
   [`license/provenance.json`](license/provenance.json),
   [`license/NOTICE.md`](license/NOTICE.md), the SBOM and the changelog as
   applicable. AI-generated content follows [`docs/ai_policy.md`](docs/ai_policy.md);
   if its origin cannot be established, it is removed.
5. **Release handling.** Published tags are immutable; do not rewrite them.
   Ship a follow-up PATCH with the correction, state which versions were
   affected, and withdraw affected artifacts where the hosting platform
   allows. Prior downloads may persist; the release notes should say so.
6. **Close** with a short public note when appropriate, without disclosing
   confidential details or personal data.

## Good faith

Report privately and in good faith. The maintainer may decline a request that
lacks a legal basis or falls outside this project's scope. Nothing in this
policy waives any right, admits liability, or creates an obligation beyond
applicable law.
