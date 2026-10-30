# gX — UI battle-scene audit (HUD review)

<!-- //opencode: first pass 2026-10-15 — committed-state captures of `--battle`
     at t=3/12/35/70s at 1024x768 plus a 1600x900 spot check; findings and the
     fixed item below. -->
Status: **first pass landed** — the battle HUD reviewed across the opening,
mid-fight and terminal victory states from the committed tree. The mechanical
layer is `gfx-battle-check` (backdrop/GFX/PP setup, HUD header, intent tell,
impact history, floaters, animation); this doc is the judgment layer on top.

## Scope

- The battle scene (`--battle`, the DEMOS → BATTLE ARENA route) at 1024x768:
  opening (t=3s), mid-fight (t=12s, wave 1), wave 2 on the title-palette
  backdrop and the terminal victory state (t=35s, t=70s); 1600x900 spot check.
- Panels reviewed: header (`BATTLE // ASTRAL RIFT`, wave, target/range),
  RECENT IMPACTS, PARTY (HP/ATB bars, MP, controller), COMMAND (five actions
  plus the two hint lines), enemy details (HP bar + value chip, status, slot),
  the turn banner and the footer message.

## Findings

### Fixed in this pass

- **Enemy HP bar hidden under the value chip.** The bar was sized from the
  party panel (`playerPanel.width * 0.63f` = 174 px) while the opaque value
  chip is drawn at `enemyPanel.width - 88 * scale`, so the bar's right end ran
  under the chip and the last ~13% of the track was invisible: at 525/1000 the
  fill read ~60% of the visible track, at 201/1000 ~23%. The bar now reserves
  the chip gutter (`enemyPanel.width - 138 * scale`, leaving an 8 px gap) and
  reads true — 52.1% measured against 52.5% shown — at 1024x768 and 1600x900;
  `gfx-battle-check` stays green with no re-baseline. The gate's bands do not
  cover the bar/chip junction, so this is not machine-pinned yet (the battle
  gate file is mid-flight in the author's WIP).

### Notes (not blocking)

- **Victory keeps the command prompts live.** After `VICTORY // ALL RIFT WAVES
  CLEARED` the COMMAND row still reads `ARROWS: COMMAND / TARGET // ENTER:
  ACT` with ATTACK pulsing selected; dimming the row on terminal outcomes
  would read cleaner (author's call).
- **Party HP has no numeric readout.** Party rows show HP/ATB bars, MP and the
  controller while the enemy shows exact HP (bar + chip); fine for the demo, a
  per-member number would close the asymmetry if the HUD grows.
- **`TARGET 2/0`** appears once the wave-2 enemy is defeated (the target
  ordinal over a zero living count); `TARGET -/0` or hiding the range would
  read better.
- The `HP` / `ATB` row labels are 8 px at the lean floor — the smallest text in
  the HUD; legible in the captures, leave unless the bars change.
- **Small-window overflow (800x600 and under).** The HUD *scale* shrinks but
  the pixel font floors at its ~12 px size while the panels are
  width-percentages, so at 800x600 the two COMMAND hint lines exit the panel's
  right edge (~16 px, clearing the enemy panel by ~4 px) and the `ATTACK`
  label nearly fills its button. The canvas GUI scales cleanly at the same
  size (title verified), so this is scoped to the scene-level HUDs; fixing it
  wants shorter hint copy at small scales or scale-based panels (author's
  call — the physics sandbox took the scale-floor route for its legend).

## Follow-ups (not blocking)

- Machine-pin the bar/chip separation (a band assertion on the junction) once
  `scripts/gfx_battle_check.py` settles after the author's WIP lands.
