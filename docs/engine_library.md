# Engine-core archive boundary

<!-- //ai: 2026-10-22 stage-1 engine core extraction; build boundary only. -->

Status: stage 1 (2026-10-22). Companion to
[`engine_game_split.md`](engine_game_split.md), which defines the long-term
engine/game separation.

## What landed

CMake now builds a static `gx_engine_core` archive and links it into
`gX_build`:

- **`gx_engine_core`** contains the reusable engine sources under `src/`,
  excluding the executable lifecycle (`src/main.c` plus its load, loop, update,
  unload, parse and test siblings), `src/tools/**` and `src/scene/**`.
- **`gX_build`** contains the program lifecycle, the authoring
  tools and the demo scene dispatcher, and links the archive.
- `gx_shell` and the lab modules are unchanged.
- The game's `gx_book` target is unchanged; it remains a separate story-only
  frontend and does not link the engine archive.

The split is path-based, not a public API. It gives the build a real archive
boundary and removes duplicate source compilation from the main target without
changing runtime behavior.

## Why these exclusions

| Excluded | Reason |
|---|---|
| `src/main.c` and lifecycle siblings | Program lifecycle, argument parsing and the `maindata` definition. |
| `src/tools/**` | In-engine authoring UI and vendored ImGui; not needed by a game frontend. |
| `src/scene/**` | Demo scenes and their dispatcher; engine showcases, not engine core. |

The engine demo template `src/gui/templates/synthvis.c` still sits in the
archive because it lives under `src/gui`. It references the excluded scene
module. A frontend that pulls the template registry must provide those scene
symbols (or use a later stage that splits demo templates out). This is a known
stage-1 impurity, recorded here rather than hidden.

## What this does not solve

The archive is **not** a standalone no-raylib engine library yet:

- `maindata` is still a global contract defined by the executable. A separate
  game frontend must supply a host/main split before linking the archive.
- The renderer, GUI, wrappers and backend are still raylib-based; the archive
  is compiled with the same engine definitions as `gX_build`.
- The game visual frontend is still unbuilt. `gx_book` is the only playable
  game target and is intentionally story-core-only.

Do not put game content, lore, assets or a game scene into the archive. The
boundary rules in `engine_game_split.md` still apply.

## Verification

- Configure/build: the archive compiles as its own target.
- `./build/gX_build --test-only` passes; the executable links the archive.
- Release/lean: the link still runs `--gc-sections`, so only referenced archive
  objects enter the payload. Run `make lean` after a change to this boundary.
- Kernel-only configure/build must still work with
  `-DGX_ENABLE_RENDERER=OFF`; the path filters do not depend on the renderer
  option.

## Next stages

The first frontend does not wait for the host split: the generic
`gX_SCENE_MODULE` boundary and the game-side `gx_level` probe are described in
[`engine_frontend_contract.md`](engine_frontend_contract.md). They keep the
archive internal and exercise the same level runtime without linking it.

1. **Host split:** replace the global `maindata` ownership with an explicit
   host context/accessor, or add a development-only game frontend profile that
   supplies it. Do not start the visual frontend before this decision.
2. **Template split:** move demo-only GUI templates out of the archive when a
   game frontend starts pulling the template registry.
3. **Public surface:** only then define the headers a game may include and the
   symbols it may call; the archive is internal until that contract exists.

The 3D level adapter and room/portal editor depend on stage 1. They should not
add a second engine copy or compile engine sources ad hoc into the game target.
