# Visual accessibility audit (low vision focus)

<!-- //ai: 2026-10-22 visual accessibility inventory and pass; low-vision first. -->

Status: audit + landed A1/A3/A4/A5/A6/A8 (2026-10-22), with A2/A7 open. Scope:
visual accessibility for people without perfect eyesight — contrast, text
size, scaling, colour-independent state, focus visibility and
motion/photosensitivity. Screen-reader output and full keyboard-only workflows
are related but separate audits.

## What already exists

| Capability | Evidence |
|---|---|
| High-contrast, colour-vision and Aero themes | `resources/style/theme_high_contrast.json`, `theme_color_vision.json`, `theme_aero.json` |
| WCAG AA surface pairs | `a11y.surface*` / `textOn*` tokens; `STYLE: a11y pair ... OK` |
| Theme gate | `scripts/gui_theme_check.py` loads every shipped theme, checks AA pairs and proves high contrast changes pixels |
| Glass/effect contrast | `scripts/gui_effect_contrast_check.py` (4.5:1 text over the pause/glass surface) |
| Text fit / line height | `scripts/gui_layout_check.py`, `a11y.typeLineHeight`, text shadows |
| Tool UI scale | `settings.toolUiScale` (100–175%), ImGui tool menu and native settings |
| Reduced-motion token | `a11y.motionReduced`; title/pause/transition/element paths snap instead of animating |
| Reduce Motion | persisted native Settings + tool menu setting; drives `a11y.motionReduced` |
| Terminal plain mode | `gx_book --plain` |

## Findings

| ID | Finding | Evidence | Severity | Proposed pass |
|---|---|---|---|---|
| A1 | **Resolved (`5eba9c7`).** Screen Flash and Screen Shake were not consumed anywhere; the dead controls/fields were removed and a real persisted **Reduce Motion** setting now drives `a11y.motionReduced` in native Settings and the tool menu. | `src/settings.c`, `src/gui/style.c`, `src/gui/templates/settings.c`, `src/tools/imgui/imgui_gx.cpp` | **done** | — |
| A2 | **Native text/UI scaling is not a simple setting.** A 1.15× text multiplier produced 112 text-fit failures across all 22 templates; 1.25× produced 254. Fixed-grid native rects need reflow, not only a font multiplier. | text-fit probe `GX_GUI_TEXT_SCALE=1.15/1.25`; `gui_layout_check.py` | **high** | template reflow/flexible boxes before exposing a scale |
| A3 | **Matrix + HUD probe added.** 174 theme checks across the four shipped themes found 27 below-threshold pairs: default 12, high-contrast 4, color-vision 3, aero 8. Examples: button hover 1.37:1, module unavailable 1.35:1, scroll arrow 1.56:1, selected 3.69:1. The HUD-over-scene probe adds 10 fixed ROIs across title/battle. Palette fixes remain. | `scripts/gui_state_contrast_check.py`, `scripts/gui_hud_contrast_check.py`, both baseline JSONs | **partial** | fix baselined pairs; extend HUD ROIs/states |
| A4 | **Audited (`colour_independence_audit.md`).** Battle/title states retain labels, numbers, signs, bar extent or position; battle's own palette is not theme-driven and is reported as a warning, not hidden. | `scripts/colour_independence_check.py`, `docs/colour_independence_audit.md` | medium | keep cue guard + simulation review; tokenize battle palette later |
| A5 | **Landed.** Transitions/tweens/pause and the opt-in analog CRT/VHS/static path honor Reduce Motion; the analog path shows the clean input and the capture gate proves early/late reduced frames are identical. | `src/renderer/analog.c`; `analog-reduced-check` | **done** | — |
| A6 | **Focus-ring contrast matrix landed.** `gui-state-contrast-check` now checks the focus ring against canvas/raised/overlay/sunken, panel/button/selected, title menu/save and battle/escape surfaces in every shipped theme. The inverse surface has no focusable consumer yet; add its pair when one exists. | `scripts/gui_state_contrast_check.py` | **done** | `gui-state-contrast-check` (226 checks) |
| A7 | **High-DPI / small-window text sizing is not pinned.** Native text scales with `screenWidth/1024`; 800×600 gives ~0.78×, while tool scale is separate. | `src/gui/element/text.c:242–246`; Packet D sweep pending | medium | layout/resolution sweep + minimum-text rule |
| A8 | **User-facing page landed.** `docs/accessibility.md` collects Reduce Motion, themes/contrast, tool sizing, keyboard/terminal controls and known limits. | `docs/accessibility.md`, docs index, readme | low | keep in sync with new controls |

## Proposed pass

### A0 — audit (this document)

Inventory, evidence and non-goals. No behavior change.

### A1 — dead controls and motion preference (done)

- Screen Flash/Screen Shake had no effect consumers; the dead controls and
  fields were removed.
- Added a persisted `reduceMotion` setting, exposed in native Settings and the
  tool accessibility menu, applied live to `gui.style.a11y.motionReduced`.
- Gate evidence: `make check-fast`, SETTINGS layout check and title smoke pass.
- Reduced-effects coverage is tracked in A5 (analog CRT/glitch paths).

### A2 — native UI scale / reflow (blocked on layout work)

- A naive text multiplier is not shippable: 1.15× caused 112 text-fit failures
  across all 22 templates; 1.25× caused 254. Fixed-grid rects do not reflow.
- Required: template-by-template flexible text boxes / wrapping / reflow, or a
  deliberately scoped subset (Settings/title/dialog) with a minimum readable
  size. Gate every supported scale through `gui_layout_check.py`.
- Do not expose a native scale slider until the reflow slice lands; the
  existing window/UI text scaling at larger resolutions remains the safe path.

### A3 — state and surface contrast (matrix + HUD probe landed)

- `make gui-state-contrast-check` merges each shipped theme over the default,
  composites alpha, and checks text ≥4.5:1 and non-text ≥3:1 across base,
  hover, selected, focus, scrollbar, status and title/battle/escape pairs.
- After the P3 mapping review, module-token pairs were removed (unused draw
  path): **162 checks, 0 failures**; HUD 10/10. Both baselines are empty.
  `--strict` is clean. Do not remove the unused tokens; re-add a pair only when
  a real component/status draw path consumes them.
- `scripts/gui_hud_contrast_check.py` adds scene-pixel evidence: at
  1024×768/frame 24 it captures `--title` and `--battle`, then samples 10
  explicit text ROIs. The 2/98 luminance percentiles pick the dark/light
  clusters inside each ROI; the contrast helper supports alpha compositing,
  while rendered captures are already final-composited. `--baseline
  scripts/gui_hud_contrast_baseline.json` fails only new ROIs, `--strict`
  fails every below-threshold ROI, and no baseline is report-only.
- ROI coverage is deliberately narrow: fixed 1024×768 bands for the default
  theme and two scene states, not the whole HUD, dynamic menus, hover/
  disabled states or OCR. The title/battle probes are stable capture
  evidence, not a claim that every HUD string was tested.
- Remaining: fix the baselined theme/HUD pairs, add hover/pressed/disabled
  tokens where they are missing, and extend fixed ROIs when new HUD states
  ship.

### A4 — colour independence (audited)

- `make colour-independence-check` captures title/battle in the default and
  colour-vision themes, writes protan/deutan/tritan simulations, and guards
  the named battle cues (`HP`, `ATB`, `STATUS`, `RECENT IMPACTS`, `YOU`, `AI`).
- The cue matrix is in [`colour_independence_audit.md`](colour_independence_audit.md).
  Battle HP/MP/status/team/impact states have labels, numbers, signs or bar
  extent; the battle scene's own palette is not theme-driven and is reported as
  a warning.
- Simulation is diagnostic; it is not a substitute for the redundant cues or a
  full colour-blind certification.

### A5 — motion/photosensitivity (landed)

- Reduced motion snaps transitions, title effects, element tweens and pause
  fades.
- The opt-in analog CRT/VHS/static renderer now bypasses its time-driven path
  and draws the clean input under Reduce Motion. `make analog-reduced-check`
  proves normal early/late frames differ while reduced early/late frames are
  byte-identical; see [`analog_reduced_motion.md`](analog_reduced_motion.md).
- Remaining decorative effects outside the analog path must be assessed when
  added; this audit does not claim third-party/custom shader coverage.

## Gates and non-goals

Existing gates to keep: `gui-theme-check`, `gui-effect-contrast-check`,
`gui-layout-check`, `gui-shadow-check`, game transcript tests. New gates:
state-contrast, HUD-over-scene contrast, colour-independence review and
reduced-motion/flash behavior.

Not in this pass: complete WCAG certification, screen-reader semantics for the
native GUI, terminal screen-reader certification, or accessibility of every
third-party/editor surface. Those need separate scopes.

## Recommended order

1. ~~A1 — fix dead flash/shake and add reduce-motion.~~ Done (`5eba9c7`).
1. ~~P1/P2/A3 — core and secondary contrast + HUD.~~ Done.
2. ~~A5 — reduced-effects/photosensitivity coverage.~~ Done.
3. ~~A4 — colour independence; A8 — user docs.~~ Audited/landed.
4. **A2 — native reflow/minimum text** as a template rework, not a naive scale.
5. A7 high-DPI/minimum-text follow; A6 is done.
