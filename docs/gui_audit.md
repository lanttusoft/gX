# GUI Template Audit (v0.1.40)

Backdrop: `--title` + `GX_GUI_TEMPLATE_FORCE=<name>` (headless capture).

| Template | Status |
|---|---|
| DEBUGDISPLAY | RENDERED |
| DEMO | BLANK |
| DEMO2 | RENDERED |
| ESCAPEMENU | RENDERED |
| EXAMPLE | RENDERED |
| GUITESTER | RENDERED |
| MESSAGE | BLANK |
| SCENE | BLANK |
| SETTINGS | RENDERED |

## Findings (v0.1.40)

- **Every registered builder function renders.** DEMO and SCENE report
  BLANK only because the respond scene-table already builds them onto
  canvases 0/1 during `--title` — they are part of the baseline image.
  MESSAGE's builder is intentionally empty. The old port note claiming
  "builder elements won't draw" was stale: the draw path works.
- **Pruned 39 enum entries** whose builder functions were never ported
  from gameX (BATTLE*, CAST*, TITLE*, MARKET, ARCADE, EDITOR, ROOMPLANNER,
  SBOX, GAMETITLE, LOADINGSCREEN, STARTTERMINAL, GFXDEBUG, SYNTHVIS_*,
  GUIPLAYGROUND, DIALOGUE, QUESTLOG, TRADE, BESTIARY, CAST_TELETEXT, MENU,
  LENS, BLUR). respond.c rows binding those templates to unreachable gX
  scenes were removed with them.
- **Ported from gameX**: SETTINGS (full panel: sections/toggles/sliders/
  steppers/buttons + Back) and GUITESTER (widget wall: rects/gradients/
  wireframes/shader quads/shapes/widgets/model element). Both verified
  rendering via forced-template captures.

### Known widget-style differences vs gameX

- ToggleSwitch renders as an ON/OFF labeled bar (no switch graphic yet).
- Text truncation to "..." activates on narrow elements (e.g. Invert Y).
- FPS-Target stepper value clips behind the +/- buttons at this canvas
  size (layout refinement queued).


**6/9 templates render content.**

## Scene enum cleanup (v0.1.42)

Same disease as templates: 19 of 28 scene enum entries referenced scenes
never ported to gX (WORLD/BATTLE/MAP/MENU/ROOMS/CAST/EDITOR/MARKET/
ARCADE/LOADINGSCREEN/STARTTERMINAL/PHYSICS/GFXDEBUG/SYNTHVIS/GUIPLAYGROUND/
INTERIOR/PS2_BATTLE/GUITESTER/ROOMS).

Removed (user-reachable paths): CLI flags `--cast/--battle/--map/--menu/
--editor/--rooms` — each jumped to an unimplemented scene rendering a
blank white frame (`--battle` verified: uniform 255,255,255 capture);
7 respond.c GUI binding rows for those scenes.

Kept (inert porting reserves): switch cases in input.c/camera.c, renderer
module-table rows, and the enum entries themselves — unreachable code with
zero runtime cost; removal is high-churn/zero-gain until those scenes are
actually ported.

## Audio smoke (v0.1.42)

Device init OK (miniaudio → PulseAudio). 16 procedural PCM tones registered
at load (buttonHover/buttonConfirm/buttonCancel/warning/successful/error/…)
— no external audio files needed (copyright-safe synthesized replacements).
GUI wiring live: widget hover uses SoundGet("buttonHover"); binding save/
cancel paths play successful/error/warning.
