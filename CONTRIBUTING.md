# Contributing to gX

gX is a public beta. Bug reports and small, focused fixes are welcome. For a
larger feature, renderer change, or refactor, open an issue first so the engine
contract and audit surface can be agreed before code lands.

## Build and test

```bash
./build.sh
./build/gX_build --test-only
make check-fast
```

Run the specific renderer, scene, or feature gate touched by a change. The full
release audit matrix is part of the release process; a contribution should at
minimum build, pass the unit tests, and pass its own focused gate.

## Code style

- C99/POSIX-style C, with gX custom types (`gX_I8`..`gX_I64`, `gX_F32`,
  `gX_I0`, `gX_I1`); do not add `stdbool.h` or raw `int` outside wrappers and
  the backend.
- Variables are `pascalCase`; functions follow `gX_Struct_SubStruct_Method()`.
- Allman braces. Keep comments outside function bodies.
- New public `gX_*` families need an entry in `docs/api_reference.md`; new
  enums need their `String()` implementation.
- AI-assisted contributions are welcome and follow the same review and
  gates. Mark them with `//ai:` above the affected code (`#ai:` in shell,
  `<!-- //ai: -->` in Markdown); the historical `//opencode:` alias is also
  accepted. Read [`AI_CONTRIBUTING.md`](AI_CONTRIBUTING.md) and the
  [AI policy](docs/ai_policy.md).

<!-- //ai: public agent entry points; see docs/ai_policy.md. -->
## Working with agents

Coding agents are welcome. [`AGENTS.md`](AGENTS.md) is the harness-neutral
agent guide: build/verify commands, conventions, the marker rules, the
evidence to report, and the limits of autonomous changes. The short
reviewer checklist is [`AI_CONTRIBUTING.md`](AI_CONTRIBUTING.md).

## Licensing, provenance, and scope

- gX is GPLv3. By submitting a contribution you confirm that you can license it
  under GPLv3 and that it contains no incompatible third-party code.
- Do not add gameX assets, story content, private scenes, screenshots, or
  proprietary material. This repository is the engine-only beta.
- Every new model, texture, font, audio file, or other resource needs a
  `license/provenance.json` entry and a compatible license.
- Do not copy tutorial or third-party code without a clear license and an
  attribution entry in `license/NOTICE.md`.
- To raise a license, provenance, attribution or trademark concern, follow
  [`LEGAL.md`](LEGAL.md). Do not post disputed material publicly.

## Security and conduct

Do not open a public issue for a suspected vulnerability; use the repository's
private security reporting channel described in `SECURITY.md`.

Participation is covered by `CODE_OF_CONDUCT.md`. Keep reports reproducible and
free of credentials, personal data, and unrelated private files.
