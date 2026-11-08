# gX — public beta

gX is a distilled, raylib-only game engine: engine core, renderer, GUI and
authoring tools, with demo scenes. This public beta ships the engine source and
demo resources; story/game content and private development history are not part
of this repository. The beta does not include the cast/broadcast demo; Battle remains a
generic engine demo.

## Start here

1. `./build.sh`
2. `./build/gX_build --title` — front end (Open GFX, Run Demo, Save Slots,
   Settings, Demos, Extras, Quit; the footer Help link explains the controls).
3. Press **Esc** for the pause menu; **Back to Title** returns to the front end.
4. `./build/gX_build --help` lists every flag; see
   [`docs/getting_started.md`](docs/getting_started.md) for the first five
   minutes.

A genuinely fresh tree with no `resources/settings.json` and no save opens the
GFX renderer demo on a plain `./build/gX_build`. That is intentional; use
`--title` or `GX_SKIPTITLE=0` for the front end, or press **Esc** and choose
**Back to Title**. Any returning user with a settings file or save gets the
title.

- Version: see `readme.txt` (`v0.42.1` beta).
- Repository: https://github.com/lanttusoft/gX
- License: GPLv3; third-party notices in `license/NOTICE.md`.
- Build: `./build.sh`; unit tests: `./build/gX_build --test-only`;
  fast checks: `make check-fast`. Windows x86_64 cross-build:
  `./build-mingw.sh`, `make mingw-check` and `make mingw-scripting-check`
  (MinGW-w64; Wine optional).
- Docs index: `docs/README.md`.
- Proof sources: https://github.com/lanttusoft/gx-proofs
- AI usage, limitations and disclaimer: `docs/ai_disclosure.md`.
- Agent guide: `AGENTS.md`; AI-assisted contributions: `AI_CONTRIBUTING.md`.
- Security reports: see `SECURITY.md`.
- Contributing: see `CONTRIBUTING.md`; support: `SUPPORT.md`; conduct: `CODE_OF_CONDUCT.md`.

This is a beta: APIs and scene formats may still change. The **GFX editor is
work in progress** and ships as a technical preview, not yet the release-grade
primary editor. Its layout, hierarchy, showcase scene, accessibility and UI
scaling are planned for a dedicated rework; expect significant improvements in
future betas. Report issues with the version/tag, OS, GPU/driver and
reproduction steps.

## Independence

gX is an independent project. It is not affiliated with, endorsed by, or
sponsored by any third-party game, ARG, series, company, or trademark.
References to public works are descriptive and nominative; no association,
sponsorship, or endorsement is implied.

## Credits

- **lanttu / lanttusoft** — artist and engine author
- **opencode (AI)** — contributions marked with `//opencode:` or `//ai:` in the source
