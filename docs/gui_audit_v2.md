# gX GUI Audit v2 — 2026-08-30 (v0.4.0)

Scope: low-level GUI systems (transition, input gating, occlusion, click
dispatch, refresh, focus) + compression pass. Method: static sweep +
headless AI-input forensics (`GX_AI_INPUT` click/drag scripts with
event-log/switch assertions) + capture matrix + vision grading.

## Capture matrix

| Scene | Result |
|---|---|
| title (first launch) | fade-from-black 0.25s, buttons live after; frame-5 capture = black, frame-180 = full purple |
| title (re-entry) | tamed skew-slide (3°, -60px, 0.35/0.15/0.35); click mid-out blocked, click post-in works |
| title LOAD GAME | save-slot sub-view opens (SAVE 1-3 + BACK); BACK returns to menu |
| gfx / showcase / background | byte-stable (see regression) |
| tools (--tools) | editor over live canvas (unchanged from v0.4.0 tools commit) |

## Findings (ranked)

### CRITICAL — fixed (v0.4.0)
1. **Title entrance swing on every launch** — `INITIAL_LOAD` was never set
   anywhere, so the SKEW_SLIDE entrance (canvas -120px + 6° over 1.25s)
   always played on startup AND blocked ALL input during it
   (`transition.executing` force-cleared CLICKED_LEFT/HOVERED on every
   element, element.c). Fix: `INITIAL_LOAD = true` set in gX_Load before
   gX_GUI_Load; startup entrance = `gX_GUI_Transition_FadeFromBlack(0.25)`
   (single out phase, black→white); skew-slide tamed (3°, -60px,
   0.35/0.15/0.35) for re-entries; input gate now `out.execute` only
   (arriving scene's buttons live during `in`). Forensics: clicks at
   t=0.1/0.8/1.5s dead pre-fix; t=0.1 dead + t=1.0 live post-fix.

2. **FadeFromBlack leaked BLACK hue forever** — `colorResult = tra->start`
   default (transition.c) assumed start=WHITE; FadeFromBlack sets
   start=BLACK, so after completion every canvas was multiplied by BLACK
   → all elements rendered dark gray permanently (probe: buttons
   [73,73,77] vs baseline [93,80,117]). Fix: default colorResult =
   WHITE. Also fixed the never-firing "transition complete" log (checked
   tra->done before it was set, then died next frame).

3. **Refresh gate swallowed canvas->refresh** — `gX_GUI_Update_Refresh`
   only ran when `gui.refresh` was true (update.c), but
   TitleSaves/TitleBack (binding.c) set only `canvas->refresh` → the
   LOAD GAME sub-view swap NEVER rebuilt (verified: view flag set, menu
   unchanged). Widget pools set both flags so they worked. Fix: refresh
   pass runs when ANY canvas requests it.

### HIGH — fixed (v0.4.0)
4. **Occlusion canvasID-vs-heap-index drift** — occlusion stored the heap
   INDEX in `canvasID`; the element match compared `canvas->canvasID` (a
   creation-order id). They align only while no canvas is removed (heap
   holes drift the mapping → silent dead clicks). Fix: `canvasPtr` in the
   occlusion struct, pointer match (same pattern as elementPtr).

5. **OnClick slot-pool indirection** — 256-slot pool + dispatch stub +
   slot index in the data module; pool reset (refresh) could invalidate
   indices; the slot cap silently dropped bindings. Fix: direct callback
   pointer in the data module (`funcPtr`/`funcPtrArg`, type changed from
   the legacy `gX_I32(*)()` shader-wire signature — clicks were its only
   caller); `ResetPool` now touches only the widget args pool. Verified:
   LOAD GAME → SAVES → BACK → MENU → CONTINUE → scene switch all fire.

### MEDIUM — verified acceptable
6. **Focus activation (B3)** — click-to-focus sets FOCUSED; Enter/Space
   on FOCUSED+CLICKABLE_LEFT sets CLICKED_LEFT (same dispatch as mouse).
   Chain sound; no headless key simulation available (GX_AI_INPUT is
   click/drag only).
7. **Toast click-dismiss (B4)** — clears SHOW_MSG/toastMessage only; does
   not consume the element click; messages canvas is empty and its
   UPDATE is off when no toast → non-blocking.
8. **Mouse scaling (B5)** — coord = rawPos × (screen/window); rects are
   screen-space; consistent fullscreen and windowed.
9. **SDF hover-scale edge (B7)** — hover pulse scales a LOCAL draw rect;
   `element->rect` (occlusion target) stays unscaled. The 3% visual
   overhang of a grown button is non-interactive — cosmetic, acceptable.
10. **Occlusion pointer across refresh (B2)** — refresh runs before
    occlusion in the same frame; occlusion recomputes fresh pointers;
    no stale-element window.
11. **GX_GUI_USE_POOLS** — dormant (0, never compiled); kept gated as a
    future pool-draw option; no code paths read it at 0.

## Compression pass (gameX→gX distill)

- Removed 6 leftover TEMP probes: canvas.c (CANVAS draw idx), update.c
  (CLEARALL wiping), gui.c (SDFT readback), element/shader.c (TEMP-NOBEGIN
  label + WSDF probe), templates.c (demo shader probe).
- Removed the empty "Old canvas buffer draw" comment block (gui.c).
- Dead `gX_GUI_Builder_Widget_ActionButton` (no callers) signature
  updated to the click-callback type (kept — 1 caller possible later).
- `gX_GUI_USE_IMGUI` (0) re-documented: ImGui lives in src/tools/ (the
  suite); the flag remains a no-op guard for game-coupled debug panels.

## Lean proof divergence (gameX lean/)

The gameX repo formalizes GUI invariants in Lean 4
(`lean/GameXProofs/`). gX v0.4.0 diverges from two of them — both were
synced into the ledger on 2026-08-30 (`lake build` green):

- `OnClickPool.lean` (32-slot pool, register/reset theorems): marked
  **DEPRECATED** — gX moved to direct callback pointers in the data
  module (B6). No pool to bound; the "register increments / reset
  clears" proofs have no counterpart. File kept as the historical spec
  of the removed binding.c pool.
- `Transition.lean`: the general out→middle→in→done ordering theorems
  still hold; gX's `FadeFromBlack` (single out-phase variant,
  doMiddle/doIn false) is now pinned by three new lemmas —
  `fade_from_black_out_phase`, `fade_from_black_done` (two advances
  reach done), `fade_outdone_terminal` (all three sub-phases complete →
  one advance to done). 7 → 9 theorems.

## Regression

- `--test-only`: 0 errors (BG_TESTS included).
- gfx capture byte-identical; showcase/background clean; title rest-state
  purple baseline restored after the hue fix.
- `make check`: only the 2 pre-existing audit failures (deps path.h:45
  x.glb, api RenderDoc).