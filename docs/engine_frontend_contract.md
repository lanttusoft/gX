# Engine frontend contract — module-owned scene

<!-- //ai: 2026-10-22 frontend ownership decision and dev-only scene probe. -->

Status: contract + first probe (2026-10-22). Companion to
[`engine_library.md`](engine_library.md) and
[`engine_game_split.md`](engine_game_split.md).

## Decision

The first visual game frontend is a **module-owned scene**, not a separate
engine-linked executable yet.

- The engine gains a generic, empty `gX_SCENE_MODULE` scene. It owns no game
  content and draws nothing.
- A game module loaded through the existing dynamic-module host supplies the
  scene's update/draw through `RegisterUpdate`, `RegisterDraw`,
  `RegisterSceneEnter` and `RegisterSceneExit`.
- The module switches the engine to that scene with
  `gX_MainData_Switch(gX_SCENE_MODULE)` or the generic `--module-scene` flag.
- The module is a development/lab artifact: it builds under `build/labs/`,
  loads only with `--module`/`GX_MODULE`, and is not part of the engine payload.

This proves the frame-ownership boundary without putting game content in the
engine scene enum or forcing the `maindata` host split. The archive from
`engine_library.md` remains the path for a future standalone editor/frontend.

## Why this first

The engine archive is reusable, but the load/update/draw/unload lifecycle is
still executable-owned and `maindata` is a global contract. A full host split
is a larger change and would still need a scene-registration decision. The
module host already provides the registration and lifecycle hooks; adding a
blank engine scene is the smallest generic extension that lets a game module
own a frame.

The engine scene enum must not learn `LEVEL`, `INTERIOR`, or any game name.
`MODULE` is the only addition.

## Engine contract

### Scene

`gX_SCENE_MODULE` is appended to the scene enum (no existing numeric value
changes). In `gX_Scene_Respond` it only marks the switch response; no scene
state is set. Its update/draw dispatches are intentionally empty.

### Input

Entering `gX_SCENE_MODULE` uses the free-camera bind set (WASD + E/Q) and
disables the cursor. The module reads movement through the exported
`gX_Input_Get_Bind_State` accessor. Left/right rotation binds are added for
the module scene; the module owns the camera interpretation.

The module must not call backend input directly. It sees engine bind states,
so remaps and virtual input stay engine-owned.

### Draw

`RegisterDraw` runs after the engine scene and GUI pass. A module-owned scene
must:

- guard on `maindata.masterIndex == gX_SCENE_MODULE`;
- save/restore GL state (`gX_GL_State_Push/Pop`);
- set its own viewport, projection and blend state;
- flush its geometry before restoring the matrices;
- leave the final frame pipeline usable when the scene switches away.

The module may clear/cover the full viewport. It may draw its own HUD; engine
GUI is not the game HUD in this profile.

### Module ABI

The existing host table and append-only export list are used. No game module
links `gx_engine_core`; unresolved engine symbols resolve from the host at
`dlopen`. A symbol needed by the game module must be added to
`modules/module_exports.txt` and the host export list check must stay green.

## Level probe

The first client is `game/src/level_module.c`, built by the game CMake as
`gx_level.so` under `master/build/labs/`.

- It compiles the real game-side `level.c`, `level_runtime.c` and the common
  JSON implementation.
- It loads `GX_LEVEL` (or a compile-time default fixture), enters the start
  spawn, applies movement/portal containment through
  `gX_BookLevelState_Move`, and projects the AABB rooms, portals, props,
  spawns and triggers as a first/third-person wireframe view selected by
  `GX_LEVEL_CAMERA`.
- It uses only host-exported engine APIs (`maindata`, drawing, input accessor,
  RHI/GL state).
- It is not a shipped game, not an editor, and not an asset pipeline.

Run:

```bash
GX_LEVEL="$PWD/game/tests/fixtures/level.json" \
DISPLAY=:1 GX_FRAMES=60 GX_NO_SETTINGS_SAVE=1 \
./master/build/gX_build --module "$PWD/game/build/labs/gx_level.so" \
  --module-scene
```

`--module-scene` is generic: it starts the empty module scene even before a
module is loaded. A module may also switch to it itself.

## Acceptance

- `gX_SCENE_MODULE` is the only new scene and contains no game name/content.
- The module loads, binds, draws the fixture, and exits cleanly with
  `--frames`.
- Movement stays inside the current room; a portal transition reaches the
  target room and position.
- The module is absent from the engine Release/lean payload.
- The existing `check-fast`, kernel, scripting and lean gates remain green.

## Not yet

- Standalone game executable linking `gx_engine_core`.
- A module-owned input callback or mouse-look contract.
- Prop collision, textured rendering, NPCs or a real 3D editor viewport.
- S8 3D viewport and asset preview; the top-down editor with resize handles,
  undo/redo and first/third-person module playtest is
  `game/editor/level_editor.py`, a separate development app.
