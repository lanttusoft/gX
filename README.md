# gX — public beta

gX is a distilled, raylib-only game engine: engine core, renderer, GUI and
authoring tools, with demo scenes. This public beta ships the engine source and
demo resources; story/game content and private development history are not part
of this repository. The beta does not include the cast/broadcast demo; Battle remains a
generic engine demo.

- Version: see `readme.txt` (`v0.18.0` beta).
- License: GPLv3; third-party notices in `license/NOTICE.md`.
- Build: `./build.sh`; unit tests: `./build/gX_build --test-only`;
  fast checks: `make check-fast`.
- Docs index: `docs/README.md`.
- Proof sources: https://github.com/lanttusoft/gx-proofs
- AI usage, limitations and disclaimer: `docs/ai_disclosure.md`.
- Security reports: see `SECURITY.md`.
- Contributing: see `CONTRIBUTING.md`; support: `SUPPORT.md`; conduct: `CODE_OF_CONDUCT.md`.

This is a beta: APIs and scene formats may still change. The **GFX editor is
work in progress** and ships as a technical preview, not yet the release-grade
primary editor. Its layout, hierarchy, showcase scene, accessibility and UI
scaling are planned for a dedicated rework; expect significant improvements in
future betas. Report issues with the version/tag, OS, GPU/driver and
reproduction steps.

## Credits

- **lanttu / lanttusoft** — artist and engine author
- **opencode (AI)** — contributions marked with `//opencode:` or `//ai:` in the source
