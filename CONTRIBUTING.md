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
- AI-assisted contributions must be marked with `//ai:` above the affected
  code. The historical `//opencode:` alias is also accepted.

## Licensing, provenance, and scope

- gX is GPLv3. By submitting a contribution you confirm that you can license it
  under GPLv3 and that it contains no incompatible third-party code.
- Do not add gameX assets, story content, private scenes, screenshots, or
  proprietary material. This repository is the engine-only beta.
- Every new model, texture, font, audio file, or other resource needs a
  `license/provenance.json` entry and a compatible license.
- Do not copy tutorial or third-party code without a clear license and an
  attribution entry in `license/NOTICE.md`.

## Security and conduct

Do not open a public issue for a suspected vulnerability; use the repository's
private security reporting channel described in `SECURITY.md`.

Participation is covered by `CODE_OF_CONDUCT.md`. Keep reports reproducible and
free of credentials, personal data, and unrelated private files.
