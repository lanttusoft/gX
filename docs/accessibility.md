# Accessibility controls

<!-- //ai: 2026-10-22 user-facing accessibility entry point; low-vision first. -->

gX exposes a small, deliberately bounded set of accessibility controls. This
page is the user-facing map; the engineering evidence and remaining gaps live
in [`accessibility_visual.md`](accessibility_visual.md).

## Reduce Motion

Open **Settings → Reduce Motion**. The persisted setting applies live (native
Settings and the tool menu expose the same value) and drives
`gui.style.a11y.motionReduced`.

When enabled:

- title effects, element tweens, transitions and pause fades snap instead of
  animating;
- the opt-in analog CRT/VHS/static renderer shows the clean input instead of
  its time-driven glitch path;
- `GX_REDUCE_MOTION=1` is a test-only capture hook for the analog gate; unset,
  the persisted setting remains authoritative.

This is not a complete photosensitivity guarantee for third-party content,
custom shaders or the editor tools. Report a specific remaining effect rather
than assuming the setting covers every surface.

## Themes and contrast

**Settings → UI theme** offers:

- **Default** — the standard dark theme;
- **High Contrast** — stronger surface/text pairs;
- **Color-safe** — a colour-vision-oriented palette;
- **Aero** — the Frutiger Aero tool styling.

The native and tool UIs use the same theme tokens. The state/HUD contrast
gates cover the shipped theme pairs and fixed HUD ROIs; they do not certify
every dynamic menu or third-party asset. The colour-independence review
records the non-colour cues used by battle states and provides
colour-blind simulation captures for manual review.

## Text and tool sizing

- Native text follows the window size; the current release does **not** expose
  a separate native text-scale slider while the fixed-grid templates await
  reflow.
- The ImGui tool suite has **Tool UI scale** (100–175%), persisted with
  settings. It affects tool text, spacing and hit targets, not the native
  title/pause templates.

## Keyboard and terminal

- The ImGui tools support full Tab/arrow/Enter navigation; the setting is
  persisted as **Tool keyboard navigation**.
- `gx_book --plain` is the plain-text terminal mode for story/dialogue
  content; it avoids decorative terminal effects.

## Known limits

- No screen-reader semantics or full keyboard-only path for the native GUI
  yet.
- Native UI/text reflow is blocked on the template reflow slice; a naive
  multiplier did not fit the fixed layouts.
- Focus-ring contrast over every scene/background and third-party editor
  surfaces are not yet measured.

See [`accessibility_visual.md`](accessibility_visual.md) for the audit table,
gate names and evidence.
