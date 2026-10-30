# gX — Porting Guide

How to move work from gameX (the experimental lab) into gX (the release line).

## Feature-first, not code-first (when architectures diverge)

gX is distilled from gameX — as gX simplifies and refines, its architecture
drifts from the gameX original (different module boundaries, structs, APIs,
pass chains). When a port spans that seam, **port the feature, not the code**:

1. **Extract the contract from gameX** — observable behavior, inputs/outputs,
   edge cases, tuning values. gameX code answers "how does this behave in
   corner case X" — it is the reference spec, not the import source.
2. **Reimplement natively in gX** — new gX-idiomatic code against gX's
   architecture (existing pass chain, gX structs/APIs). Never `#ifdef`
   gameX/gX variants into a copied file — that re-imports the tech debt gX
   was created to remove.
3. **Verify behavioral parity** — screenshots, HUD state, logs, A/B effect
   compares. Code diffing is meaningless across architectures; behavior is
   the contract.
4. **Pass the filters on arrival** (scope / distill / debt) — feature-first
   doesn't bypass the gates; it makes them the admission test.

**When to use code-first instead:** the strata that survived distillation
unchanged — math, algorithms, shaders identical in both — port cheapest as
code. Rule of thumb: *start from the code when the architecture matches;
start from the feature contract when it doesn't.* Divergence is the norm as
gX refines, so feature-first becomes the default over time.

## The pipeline

1. **Prove in gameX** — experiments land in gameX first (no quality gate).
2. **Port to gX** — on demand, or at session end for work that matters
   (milestone-sync cadence). Ports land on the `experimental` branch first if
   they aren't milestone-gated; they graduate to `master` at milestone gates.
   Code-first (file copy) when the architecture matches; feature-first
   (contract → native reimplementation) when it diverges.
3. **Parity audit** — code-first: diff the ported file against the gameX
   original and resolve every API drift. Feature-first: behavioral parity
   (screenshots/state/logs) instead of a file diff.
4. **%-commit** — commit with a parity percentage note when full parity
   isn't reached (e.g. "audio 35% → ~95%").

## Filters (scope / distill / debt)

A port must pass:

- **Scope** — is it engine core? game content (game/, game templates,
  battle/world scenes, save systems) stays in gameX.
- **Distill** — does it earn its lines? The renderer subset is
  background/postprocess/textwriter + gfx core; soft/raymarch/analog-style
  demos are sample scenes, not core. Exception (2026-09-28): raymarch's preset
  schema was restored by decision, making it the one authoring surface carved
  out of that rule — gated by `make gfx-raymarch-check`.
- **Debt** — every subsystem has a tech-debt ledger entry; pay it at port
  time (fix headers, remove dead code, close warnings) rather than carrying
  it over.

## Known drop list (do not port)

| Module | Reason |
|---|---|
| video/FFmpeg | external dependency, out of scope |
| globe | drags the weather system |
| demoscript (`.gxdemo`) | the renderer demo-sequence system; out of scope |
| AI probe (ai.c/ai_script.c) | game-coupled harness |
| ImGui debug panels | debug-only, game-coupled |
| game templates (MENU/QUESTLOG/...) | game content |
| full gamedata | game content — the shim keeps only the ephemeris |

**Decision (2026-10-12, revised):** AngelScript scripting returned as the
**gx_shell dynamic module** (`GX_USE_ANGELSCRIPT`, default OFF; contract in
`docs/scripting.md`, host in `docs/dynamic_modules.md`, gate
`make check-scripting`) — host + distilled core/state API; the game-content
API modules (character, inventory, world, battle, ai, game, entity) stay out.
The drop-list row previously read "demoscript | AngelScript scripting",
conflating two different subsystems; demoscript remains dropped.
Rationale: the author's demand was a gX shell for demo/art tours and
internal test/audit drivers, and the removal left the integration seams in
place (markup onclick, SCRIPTING log module, virtual-input merge point).

## Port checklist

Code-first path (architecture matches — see "Feature-first" above for when
to take the feature-first path instead):

1. Copy the file(s) from gameX.
2. Fix includes (gX has no `src/external/glad.h` — the RHI uses raylib's
   bundled glad; gX has no `game/`, `scene/battle.h`, `ui/imgui`, etc).
3. Add the gX header banner (`[gX: /path]` + Copyright) if missing.
4. Compile: `cmake --build build -j$(nproc)` — expect API-drift errors;
   sync the dependent headers from gameX when the drift is legit.
5. Gate game-coupled calls behind the config flags
   (`gX_ENABLE_GAME`, `gX_ENABLE_GFXAI`, `gX_GUI_USE_IMGUI`).
6. Verify: `--test-only`, the affected scene(s), zero new warnings.
7. Commit to `experimental` (or `master` at a gate) with the %-commit note.

## Gotchas (learned the hard way)

- **Load order**: `gX_Backend_Init()` must run before renderer/gui loads —
  GPU resource creation needs the GL context. Check main_load.c.
- **`-std=gnu11` + `-Wno-implicit-function-declaration`** — gameX compiles
  with these; strict c11 + implicit-decl errors will mislead you into
  "invalid initializer" cascades that are actually missing includes.
- **include-order dependencies**: gameX's renderer.h pulls gxmath via
  background.h; gX's renderer.h must keep that order or vector ops vanish.
- **The NVIDIA DCE quirk** (non-planar reflections): reassigning a GLSL
  uniform-dependent value inside a branch can make the driver DCE the whole
  block — compute into a local, assign once.
- **Texture units ≤ 31** — GL_MAX_TEXTURE_IMAGE_UNITS; the unit map lives in
  `src/renderer/gfx/constants.h` (SSR=1 SCENE=2 DEPTH=3 ... CSM_ARRAY=10
  COOKIE=11 SHADOW=8).
- **Matrix convention (REAFFIRMED 2026-09-07 — do not flip to GL_FALSE)**:
  gxmath is row-major; upload with glUniformMatrix4fv GL_TRUE. Every
  shader uses `M * vec` column form, and ~100 3D call sites pass
  transpose=1 (`gX_RHI_UniformMatrix(..., 1, ...)`; the
  `gX_Shader_SetValue_Matrix` wrapper hardcodes 1). Never raylib's
  MatrixMultiply on gxmath data.
  - **Readback trap**: `glGetUniformfv` on a GL_TRUE upload returns the
    authored float order (round-trip identity) — a readback "match" says
    nothing about the flag. Only a render (or an asymmetric-matrix probe)
    settles transpose disputes. This exact trap produced a false
    "GL_TRUE is backwards" claim during the worldFromDepth hunt; the
    oracle/reflect gates (which render) confirm GL_TRUE is correct.
  - **transpose=0 exceptions (3, all intentional, do not "fix")**:
    `gui/transition.c` uploads identity (flag-irrelevant);
    `gui/widgetsdf.c` uploads a pure scale+translate ortho (a transpose
    maps a row-major translation to the correct column-major one — safe
    by structure; flipping it to 1 would DROP the translation).
  - **RESOLVED 2026-09-18, no code change beyond the one-line fix**:
    `renderer/wireframe.c` custom-VAO path uploaded its row-major
    proj·view with transpose=0, rendering (PV)ᵀ garbage (squashed triangle
    pancake + stray off-frame lines, captured on `--wireframe`). Flipped
    to transpose=1 per the gxmath convention: the render is now a coherent
    button.glb disc (model 0) at the correct orbit placement. The matrix
    wireframe row passes either way (it measures on-vs-off delta, not
    placement) — visual verify was the only honest gate here.
  - Multiply ORDER (proj·view·model vs model·view·proj) is per-path;
    verify empirically per scene (the Lean proofs don't settle it).
- **Raylib Y-flip**: render textures have UV(0,0) = bottom of visual content
  (rlOrtho Y-down). File textures don't flip. The backend auto-inverts
  src.height for RT sources.
