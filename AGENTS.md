# gX — agent guide

<!-- //ai: agent-drafted; maintainer review follows docs/ai_policy.md. -->

This is the entry point for AI coding agents working on gX. It is
harness-neutral: Codex, Claude Code, OpenCode and plain scripts read the same
contract. Read [`docs/ai_policy.md`](docs/ai_policy.md) and
[`CONTRIBUTING.md`](CONTRIBUTING.md) first; this file adds the practical
workflow.

## What this repository is

- gX is a raylib-only game engine in C99, licensed GPL-3.0-or-later. It is a
  public beta: engine core, demo scenes and engine-native tooling.
- It is not a game, an asset library or a modeller/paint application. New
  feature work stays inside the engine and tooling scope
  ([`docs/tooling_scope.md`](docs/tooling_scope.md)).
- The renderer, editor and gates are the product surface. A change that only
  looks right in a screenshot is not done until its gate is green.

## Build and verify

```bash
./build.sh                       # Debug build (build/gX_build)
./build/gX_build --test-only     # unit tests, no window
make check-fast                  # fast admission (build + focused gates)
make check                       # full CI: build, tests, scene smokes, audits
make gates-check                 # gate matrix manifest consistency
make version-check               # version format/consistency
```

- `make check` is the admission gate for a change; `make check-fast` is the
  useful inner loop. The full gate includes the Lean proof audit, which
  expects the public `gx-proofs` repo as a sibling `../proofs` checkout at the
  tag recorded in `proofs.lock.json`. Without it, use `check-fast` plus the
  focused gates.
- Run the focused gate for the surface you touched (each script documents its
  own contract; the matrix is `gates.toml`).
- Local gates are authoritative. GitHub Actions is intentionally disabled;
  do not enable it or add workflows as part of a change.
- If a gate fails, fix the cause. A known cold-start flake may be retried once
  and recorded as a retry, never hidden.

## Repository map

- `src/` — engine (renderer, RHI, tools, GUI, scene, audio, math)
- `resources/` — shaders, demo assets, editor presets; every new resource
  needs a provenance entry
- `docs/` — contracts; start at [`docs/README.md`](docs/README.md)
- `scripts/` — gates, audits and evidence tools
- `../proofs/` — optional sibling Lean proof checkout (`make proofs`); the
  pinned corpus is external, see [`docs/lean_proofs.md`](docs/lean_proofs.md)

## Code conventions

- C99/POSIX-style C. Use gX types (`gX_I8`..`gX_I64`, `gX_F32`, `gX_I0`,
  `gX_I1`); do not add `stdbool.h` or raw `int` outside wrappers and the
  backend.
- Variables are `pascalCase`; functions are `gX_Struct_SubStruct_Method()`.
  Allman braces. No comments inside function bodies. Include guards on
  headers. Structs start with `name[128]` and `description[256]`.
- Public `gX_*` families need an entry in
  [`docs/api_reference.md`](docs/api_reference.md); new enums need their
  `String()` implementation.
- gxmath is row-major. Upload matrices with the transpose flag the renderer
  expects; never mix in raylib `MatrixMultiply` on gxmath data. Read
  [`docs/porting.md`](docs/porting.md) before touching transforms.
- GPU skinning, texture-unit and cubemap-face conventions have hard limits
  documented there and in [`docs/architecture.md`](docs/architecture.md).
  Check them before changing renderer state.

## AI attribution and review

- Mark AI-authored changes with `//ai:` above the affected code (`#ai:` in
  shell/Makefile, `<!-- //ai: -->` in Markdown). One marker per logical
  change is enough; do not decorate every line.
- AI output is a draft until a human reviews it. The author of record owns
  the change, its license and its claims.
- Do not add AI-generated assets without a `license/provenance.json` entry
  (`"origin": "ai-generated"`) and the review described in
  [`docs/ai_policy.md`](docs/ai_policy.md).

## Contribution rules

- GPL-3.0-or-later only. Submit only code and assets you can license.
- No third-party code without a compatible license and a
  `license/NOTICE.md` attribution.
- No private or unrelated material: no credentials, personal data, private
  paths, proprietary assets or content from other projects.
- Keep diffs scoped. Do not mix a fix, a refactor and a feature in one
  change; do not reformat untouched code.
- Do not commit build output, user settings, editor layouts, shader binary
  caches or screenshots unless the change is specifically their gate
  evidence.

## Agent workflow

1. Read this file, [`docs/README.md`](docs/README.md) and the contract for the
   surface you will touch.
2. Make the smallest change that satisfies the contract, on a branch.
3. Build, run `--test-only`, then `make check-fast` and the focused gate.
4. Update the API reference, docs and provenance when the change adds a
   public surface or a resource.
5. Report the exact commands and results. Do not claim a gate you did not
   run, and do not describe a beta as certified or release-grade.
6. Leave tags, releases and publishing to the maintainer.

## Where to start

- [`docs/architecture.md`](docs/architecture.md) — system layout
- [`docs/porting.md`](docs/porting.md) — matrix/renderer traps
- [`docs/api_reference.md`](docs/api_reference.md) — public API index
- [`gates.toml`](gates.toml) — the audit matrix
- [`docs/ai_policy.md`](docs/ai_policy.md) — AI contribution policy
- [`AI_CONTRIBUTING.md`](AI_CONTRIBUTING.md) — the short reviewer checklist
