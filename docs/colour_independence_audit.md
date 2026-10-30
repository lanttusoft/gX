# Colour-independence audit

<!-- //ai: 2026-10-22 state/cue review and colour-vision simulation evidence. -->

Status: audited (2026-10-22). This is not a WCAG certification and not a
claim that every hover/focus/disabled or third-party surface is covered.
Simulation captures are diagnostic; labels, numbers, signs, position and bar
fill remain the authoritative cues.

## Evidence

```bash
make colour-independence-check
```

`scripts/colour_independence_check.py` captures `--title` and `--battle` at
1024×768/frame 60 in the default and colour-vision themes. It writes original captures, protanopia/deuteranopia/
tritanopia simulations and a `report.json`/`report.md` under
`/tmp/opencode/colour_independence_check/`.

The gate also guards the named non-colour battle cues in `src/scene/battle.c`.
It fails if a cue disappears, a capture is blank/non-finite, or a
theme-required surface stops responding to the shipped colour-vision theme.

## State and redundant cue matrix

| State/surface | Colour cue | Non-colour cue | Status |
|---|---|---|---|
| Battle HP/MP | meter fill/colour | `HP`/`MP` labels and numeric values | pass |
| Battle ATB | blue→gold fill | `ATB` label, bar extent and `ATB READY` turn text | pass |
| Party/target | unit accent colours | party/enemy names, `YOU`/`AI`, slot labels | pass |
| Enemy status | alert accent | `STATUS GUARDING` / `STATUS DEFEATED` text | pass |
| Impact history | heal/damage accents | actor names and signed `+`/`-` values | pass |
| Title/demos/saves | theme/button colours | button labels, `EMPTY` save labels, focus/selection position | pass |
| Tool/editor status | success/warning colours | status text and action labels | audited, not exhaustively captured |

## Findings

- **Battle uses a scene palette, not the GUI theme palette.** The
  colour-vision theme therefore changes title pixels but not the battle
  HUD. This is reported as a warning in the gate, not hidden with a lower
  threshold; its current state independence relies on the labels/numbers/signs
  above. Tokenizing the battle palette is a later visual decision for the
  author.
- `moduleAvailable` / `moduleUnavailable` tokens have no active module-card
  consumer after the unused-token review; the title's current cards are
  labelled and do not depend on those two colours.
- Hover, focus, pressed and disabled states are covered by the state-contrast
  and layout gates where they exist, but not by a colour-blind simulation for
  every component. Treat this matrix as the state audit, not full component
  coverage.
- Third-party editor surfaces and scene/asset content are outside this
  engine-front-end audit.

## Limits

- The simulation matrices are diagnostic approximations (full-severity
  protan/deutan/tritan), not a certification.
- The source guard proves named cues still exist, not their rendered size,
  contrast or ordering; the existing layout/HUD captures and the author's
  visual review cover those.
- No screen-reader semantics are claimed here; see
  [`accessibility.md`](accessibility.md).
