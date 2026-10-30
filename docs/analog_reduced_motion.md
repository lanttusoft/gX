# Analog reduced-motion behavior

<!-- //ai: 2026-10-22 A5 reduced-effects evidence for the analog CRT/VHS path. -->

The opt-in analog renderer (`--analog`) has a time-driven CRT/VHS/glitch
path. With **Reduce Motion** enabled it bypasses
that path and draws the clean input instead, so the effect is removed rather
than slowed. The normal path and the static testcard input are unchanged.

## Implementation

- `settings.reduceMotion` is the persisted source of truth;
  `GX_REDUCE_MOTION=1` is a test-only capture override.
- `gX_RendererAnalog_Update` copies the live setting into the renderer.
- `gX_RendererAnalog_DrawTo` returns the clean input when reduced motion is
  active, before any CRT/VHS/CPU processing.
- `gui.style.a11y.motionReduced` continues to drive title, transition,
  element-tween and pause behavior.

## Gate

```bash
make analog-reduced-check
```

`scripts/analog_reduced_check.py` captures `--analog` at fixed frames 8 and 68:

- normal early/late frames must differ (the existing effect animates);
- reduced early/late frames must be byte-identical (the clean input is static);
- reduced and normal captures must differ (the reduction actually changes the
  rendered path);
- all captures must be finite and non-empty.

The report is written to `/tmp/opencode/analog_reduced_check/`. This is a
renderer regression gate, not an aesthetic sign-off.

## Limits

- The bypass intentionally removes the analog look under Reduce Motion; it
  does not provide a second static CRT shader.
- The standalone analog scene and VHS/legacy/CPU modes are covered. Third-party shaders, editor previews and arbitrary game
  content are outside this gate.
- `GX_REDUCE_MOTION` is test-only; unset, the native Settings value is
  authoritative.
