# Engine/game split and the no-assets terminal frontend

<!-- //ai: 2026-10-19 engine/game boundary and text-frontend proposal. -->

Status: proposal (2026-10-19).

## Recommendation

Keep the engine and the actual game separate in **code and build**, even if
they share a repository for a while.

- **gX** is the engine: kernel, RHI/backends, renderers, scene dispatcher,
  tools, demos, tests, Lean/audit gates.
- **The game** is separate: story/state, content data, assets, and one or
  more frontends. It depends on engine APIs; the engine never references the
  game's names, lore, assets, or chapters.
- Engine demos (`title`, `battle`, `physics`, `synthvis`) remain showcases,
  not game content.
- A monorepo with a game module and separate build target is acceptable at
  first; split to a separate repository once the API boundary is stable.

This is the Source/Half-Life pattern at the project level: the engine carries
reusable machinery; the game carries the world. gX's `src/game/` is already a
deliberate 490-line ephemeris shim after the game content was dropped
(`docs/kernel.md`); do not quietly turn it back into the RPG layer.

## The Gmod/HL2 analogy

Source is the engine; Half-Life 2 is a game; Garry's Mod is a separate
sandbox game on Source that mounts HL2 content. That is three layers:

| Layer | Source example | gX |
|---|---|---|
| Engine | Source | gX core: kernel, RHI/backends, renderers, tools, scene runtime |
| Sandbox/platform | Garry's Mod | optional gX sandbox: demos, editors, content mounting, mod API |
| Game | Half-Life 2 | the game: story, chapters, assets, game frontends |

- **Engine vs game** is Source vs HL2.
- **Gmod vs HL2** is two games on one engine — not the engine/game split.
- The current gX repo is engine plus sandbox-ish demos/tools. That is why it
  feels Gmod-like. If a Gmod-style product is wanted, make it a **third app**
  with its own release, content policy, and API; do not fold it into either
  the engine or the story game.
- Gmod's content-mounting model depends on HL2 assets. gX cannot mount gameX
  assets that way; every content pack needs provenance/licensing, and gameX
  stays a reference archive.
- The text frontend belongs to the game (the HL2 side), not the sandbox.

The clean target is therefore: **gX engine → optional gX sandbox → the game
game**. The first slice can skip the sandbox entirely.

## Where a task lives

Use this before writing a task brief:

| The work is... | Placement |
|---|---|
| reusable engine capability with no game identity | engine core |
| optional engine capability or easter egg, default off | lab module through the module host; build to `build/labs/`, load with `--module`/`GX_MODULE`, never auto-load from `modules/` |
| a visual demo you enter and leave | renderer scene + flag |
| game rules, content, UI, assets, awareness, moderation | game module/repository |
| story, dialogue, flags, save | story core; no renderer/scene/GUI/backend includes |

A scene is for a demo you enter. A **module** is for something that extends or
overlays the running engine. Anything that must survive the world degrading
(frame-rate/resolution/lag) belongs in the post-GUI module draw hook, not a
scene.

## A text CLI is a frontend, not a backend

A real gX backend implements the wrapper/RHI contract: window, textures,
shaders, models, fonts, input, audio. A “text backend” would have to stub
almost all of that while the 3D scene code still calls it; you would get a
fake renderer and no game.

Instead:

```text
             story core (no renderer/scene/GUI/backend)
                   /                         \
      terminal frontend                 raylib frontend
   ANSI text, choices, ASCII      3D scenes, battle,
   map, status, save/load         Y2K lighting, audio
```

- The **terminal frontend** reads/writes the story state and dialogue data,
  prints text/ANSI, and runs in any SSH session or CI job.
- The **raylib frontend** is the flagship visual game and consumes the same
  story core.
- A **null/headless backend** is a separate engine test feature (no window,
  no GPU), not the book mode.

## Story core boundary

The pure story layer must be renderer-independent:

- May include: common types, alloc, log, JSON/marshal, math, path helpers.
- May not include: wrappers, backend, RHI, renderer, scene, GUI, audio.
- Owns: phase/state machine, dialogue graph/runtime, flags, loop/alert state,
  minimal save/load, chapter data loading.
- Exposes events/commands; frontends decide how to present them.

If a story module needs a visual concept (a room, an item icon), it refers to
a data id, not a texture/model or GUI type.

## Build targets

- `gX_build` — engine demos, as today.
- `gx_book` / game text — story core + terminal frontend + common
  modules only; no raylib, no display, no assets. This is the no-assets build.
- game visual — later: engine renderer/scene + game content + raylib
  frontend. Scene registration is a separate design decision; do not add a
  game scene to the engine enum just for the first slice.

The current CMake now builds a stage-1 `gx_engine_core` static archive
(`src/` minus `main*`, `tools/**` and `scene/**`) and links it into
`gX_build`; see [`engine_library.md`](engine_library.md). It is still an
internal archive: the global `maindata` host contract and the raylib-backed
renderer are not a standalone game library yet. A game target still needs a
host split or explicit source list before it can link the engine. That is a
prerequisite, not a reason to put game content in the engine.

## No-assets mode

The terminal frontend is the answer to “the game has no shippable assets yet”:

- Playable narrative/systems with code + JSON text only; no models, textures,
  shaders, fonts, or audio files.
- Works over SSH, in CI, on low-end machines, and as an accessibility path.
- Same save/data schema as the visual frontend, so progress and content stay
  portable.
- Useful as the first vertical slice and as a test harness for story data.
- Does **not** solve 3D asset licensing or replace the Y2K visual identity; it
  is a companion/fallback, not the flagship.

Basic visuals: ANSI colors, box drawing, a status line, dialogue text, and an
ASCII room/map sketch. Optional terminal image protocols (Sixel/Kitty) are
assets and can come later; the no-assets build does not require them.

## Terminal UX

Turn-based; no real-time loop or GPU:

```text
> look
> go north
> talk guide
> choose 2
> status
> save
> load
> quit
```

- `--plain` disables ANSI for screen readers/logs.
- Scripted stdin produces golden transcripts for tests.
- The same command/event names map to visual-frontend actions later.
- Deterministic; no wall-clock transitions.

## Boundary rules

- Engine code never includes game headers, content, or assets.
- Game code includes engine public headers only; no engine internals.
- No game-specific `#ifdef` in engine sources.
- Engine demos stay demos; lore/characters do not leak into `src/scene`.
- Game pins an engine version/tag; engine and game release independently.
- GPLv3 covers code; game assets keep their own provenance/licenses.
- Engine `make check` stays engine-only; game adds its own story/transcript
  gates.

## Impact on the opening slice

Recommended order:

1. **Story core + terminal frontend** for the opening arc: boot, loop, break,
   travel/escape represented as choices/checks, an interior location; dialogue
   and save. No engine scene, renderer, or asset work.
2. **Visual adapter** later: the same story core drives 3D scenes; the engine
   gains a generic scene-registration hook only when that need is concrete.

The existing story-slice scene wiring is therefore the **3D adapter
contract**, not the first playable. Pure state/dialogue/save move
to the game story core, not `src/scene`.

## Risks

- Two frontends means two UX surfaces; keep the terminal one intentionally
  small.
- Terminal portability: UTF-8, ANSI, Windows consoles, screen readers.
- Story core purity is easy to erode; enforce the include boundary in review.
- Save/data compatibility across frontends needs versioning from the start.
- The text mode must not dilute the visual/Y2K identity; it is a companion.
- In-tree game module vs separate repo: start modular, split when the API is
  stable.

## Recommendation summary

- **Yes:** keep engine and game separated; the engine must not know the game.
- **No:** do not build a text RHI backend. Build a terminal **frontend** over a
  renderer-independent story core; add a null backend separately only if CI
  needs one.
- **First playable:** the text frontend for the opening arc — it is the no-assets
  build and the fastest way to validate the story systems before 3D work.
