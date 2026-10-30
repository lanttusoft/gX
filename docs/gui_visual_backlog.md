# GUI visual backlog

Queued looks for the canvas GUI, from the author's 2026-09-28 notes. None of
these are committed behavior yet; every slice needs a gate (or an extension
of one) and a fallback path.

## Gradient depth — shipped 2026-09-28

The SDF surface program renders authored vertical/horizontal/four-point
gradients (restoring them on the SDF path) and `gui.style.surfaceDepth`
selects horizon (reflected band) or metallic (highlight + rim) profiles over
surfaces. `make gui-depth-check` pins the ramps (vertical, horizontal and the
four-point corners since 2026-09-29), the band positions (the metallic band
follows the authored orientation — along X for horizontal gradients since
2026-09-29), the flat fallback and determinism. Remaining ideas:

- Per-element depth override (a profile per gradient element) if themes want
  mixed surfaces; today `surfaceDepth.mode` is theme-wide.
- Keep the contrast/layout gates authoritative: depth profiles must not drop
  text contrast below WCAG AA (sample the text band, not the surface
  average) — a depth-aware contrast probe is the natural follow-up.
  **Done 2026-10-15** (the contrast gate's new `depth-profile` row): the
  title/button bands are SDF surfaces, so a mode-1 horizon variant against
  mode 0 pins the AA floor per band and proves the profile rendered (ROI
  delta 15.97). Measured: horizon is the *tighter* profile — title 6.98
  (metallic) -> 5.05 and button 6.14 -> 5.14, still above 4.5; the textured
  glass hint band is not SDF-shaped (4.74 in both rows).

## Text shadows — shipped 2026-09-28

Theme tokens `gui.style.textShadow` (offset in base-1024 px scaled with the
font, color + alpha), bitmap and SDF paths share the prepared text, the title
wordmark/subtitle opt in, and `make gui-shadow-check` pins determinism,
offset direction, the SDF path, and the layout pass with shadows forced.
**Text outline (2026-09-29)**: `gui.style.textOutline` (width in base-1024
px scaled with the font, color; width 0 disables) draws an 8-way 1px copy
under the glyphs on both the bitmap and SDF paths; the high-contrast theme
opts in. The gate gains outline rows (bitmap band, SDF demo, token-off
pixel-identity), and `gui-effect-contrast-check` proves the outline lifts
glass text contrast (hint 4.74->5.12, title 6.98->8.18, button 6.14->6.84).

**Per-size offsets (2026-09-29)**: `gui.style.textShadow.offsetXSmall`/
`offsetYSmall` (defaults mirror the base pair, so the default theme stays
pixel-identical) apply below `core.bodySize`; the Aero theme tightens them to
1.5/1.5. The gate asserts the small-text pair lands (direction (0,6) for a
6/0 small offset).

**Soft shadows (2026-09-29)**: `gui.style.textShadow.softness` (base-1024 px,
0 = off) draws a 5-tap ring (center + 4 cardinal taps at alpha/5) instead of
the single copy; the Aero theme softness is 2.0. The gate asserts the ring
widens the shadow (8.6k ring pixels) and that explicit and schema-default
softness 0 agree pixel-identically; the GUI benchmark gains a
`guitester-shadow` row for the advisory cost delta.

**Per-element depth override (2026-09-29)**: `gX_GUI_Builder_SurfaceDepth`
sets a 2-bit element state (0 = inherit the theme, 1 = none, 2 = horizon,
3 = metallic), so a theme can mix surfaces without a second theme; the depth
gate asserts the override beats the theme mode in both directions.

**SDF-blur shadows (2026-09-29)**: SDF fonts blur through the distance
field — one extra draw instead of the 5-tap ring. `sdf_text.fs` carries a
`soft` uniform (spread in distance units, edge-band multiplier, alpha
multiplier; defaults `0/1/1` reproduce the plain edge exactly) and the
soft-shadow helper sets it for the shadow copy. This also fixed the SDF
text path itself: the shader read `.r` (white in the GRAY_ALPHA atlas) so
the SDF demo rendered as a solid white block; it now reads `.a`, matching
raylib's own `sdf.fs` and the real distance data.

Remaining ideas:

- (none — the visual backlog is empty; new ideas land here first.)

## Aero glass — shipped 2026-09-28; production adoption 2026-09-29

`glass.frag` + `gX_GUI_Builder_Shader_Glass` + `gui.style.glass` tokens
(blur/tint/rim/refraction/gloss) sample the accumulated backdrop; the
GuiTester fixture carries a demo panel and `gui-glass-check` pins the blur,
rim, fallback and determinism. **Adoption (2026-09-29)**: glass is opt-in per
theme (the default theme zeroes the tokens and stays flat); the escape-menu
panel is a production glass surface; `resources/style/theme_aero.json` ships
the aqua palette with glass/metallic depth/cursor light; the GUI benchmark
gains `guitester-glass`/`guitester-cursor` rows and
`gui-effect-contrast-check` pins WCAG AA text over the glass and flat panels.
Remaining ideas:

- Parallax/refraction with depth: the current refraction is a fixed pixel
  push; a depth- or cursor-driven variant is future work.
- Title credits/saves panels: tried 2026-09-29 and reverted — their glass
  backdrop is the bright title sky, and `mix(backdrop, col, tint)` at the
  theme tint cannot reach AA for the light header text without becoming
  near-opaque (the escape panel works because its backdrop is the darker
  scene). Enabling them needs a per-element tint/darkening pass or a
  dedicated dark backdrop behind the glass; the title already gets the
  metallic depth profile on its slot plates.

## Cursor lighting — shipped 2026-09-28; enabled in the Aero theme 2026-09-29

`gui.style.cursorLight` (strength/radius/color/glint) adds a pointer-following
radial highlight to SDF surfaces, mapped per draw path so docking/scaling
stay aligned; `gui-cursor-light-check` pins follow/off/determinism. The
default theme keeps strength 0; `theme_aero.json` enables it (0.3/180/cool).
**Adoption (2026-09-29)**: a `guitester-cursor` benchmark row measures the
cost delta, and `gui-effect-contrast-check` parks the cursor on the pause
hint and asserts AA holds with the light on. **Hover glint (2026-09-29)**:
the light tightens (radius x0.55) and brightens (x1.54) over hovered SDF
surfaces, and more when pressed (x0.40/x1.99); `gui-cursor-light-check`
gains a `hover-glint` row comparing the hovered button's ROI with
`GX_GUI_CURSOR_GLINT=0` (delta 4.8, far field identical).

## Constraints for all of the above

- Hidden-window audit determinism: no wall-clock or backbuffer sampling.
- Lean gate: prefer uniforms/theme tokens over new per-element fields; a new
  field must justify its payload bytes.
- Fallbacks: SDF-less / GL3.0 paths keep readable flat surfaces.
- Themes: every new token needs a `resources/style/theme.json` default and a
  contrast assertion whenever it backs text.
- The layout pass stays green (no label may start clipping because a surface
  got a new inner padding or band).
