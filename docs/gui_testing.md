# GUI testing

The GUI has four complementary test paths:

- `make check` runs the headless `--test-only` suite, including GUI input,
  dialogs, text input, CSS, layout, canvas transforms, dock target resize,
  the WM resize sync and cramped-dock fallback, markup
  callback/visibility/ownership/deep cleanup, and ImGui editor press capture.
- `make gui-check` captures every registered template against a title-scene
  baseline and gates on crashes, missing captures, unexpected blank templates,
  and unexpected rendering from templates expected to be transparent.
- `make gui-check-fast` captures the EscapeMenu, GuiTester, and Settings
  templates for a shorter GPU-backed smoke run.
- `make gui-interaction-check` drives the real canvas GUI through
  `GX_AI_INPUT` and asserts the `GX_GUI_TRACE` interaction readout.
- `make gui-layout-check` is the layout pass: every template's text must fit
  its element box (draw-truth bounds from the engine's prepared text path).
- `make gui-shadow-check` forces the themed text shadow on every label
  (`GX_GUI_TEXT_SHADOW=1`) and asserts the bitmap offset direction, the SDF
  path, clean disable via zeroed tokens, the per-size small-text offset pair
  (`offsetXSmall`/`offsetYSmall` below `core.bodySize`), the soft-shadow ring
  (`softness`, 5-tap) on bitmap text and the SDF distance-field blur on the
  SDF demo, and a green layout pass; it also pins the themed text
  outline (`gui.style.textOutline`, 8-way 1px copy) on the bitmap and SDF
  paths and its width-0 pixel-identity.
- `make gui-depth-check` asserts authored gradients survive the SDF surface
  path (vertical, horizontal, four-point corners), that the themed depth
  profiles land at their token positions (the metallic band following the
  authored orientation), and that the per-element depth override
  (`gX_GUI_Builder_SurfaceDepth`) beats the theme mode.
- `make gui-glass-check` asserts the Aero-glass surface blurs/tints its
  backdrop, keeps its silhouette rim, falls back pixel-identically when
  disabled, and stays deterministic.
- `make gui-cursor-light-check` drives the synthesized cursor between two
  surfaces and asserts the highlight follows, strength 0 keeps it off,
  the hovered/pressed glint tightens and brightens locally
  (`GX_GUI_CURSOR_GLINT=0` is the force-off), and repeat runs are
  deterministic.
- `make gui-pause-blur-check` opens the EscapeMenu over a renderer scene and
  asserts the backdrop is the current frame blurred with the shader's own
  kernel plus the theme dim token (a stale ping or an unloaded blur program
  fails), the panel renders, and repeat runs are deterministic.
- `make gui-effect-contrast-check` renders the Aero pause panel and the
  default flat panel and asserts the hint/title/button text bands keep WCAG
  2.2 AA (>= 4.5:1), with the cursor light parked away and on the hint, and
  that the outline token lifts the glass text contrast further.
- `make gui-theme-check` loads every shipped theme through `GX_GUI_THEME` and
  gates theme application, WCAG 2.2 AA On- pairs, and a rendered frame.
- `make gui-visual-review` creates a paired baseline/template contact sheet;
  optional grades and notes persist in its review manifest.
- `make gui-scoreboard` combines system coverage, headless correctness,
  template-matrix correctness, optional visual grades, and median runtime cost.
  Use `python3 scripts/gui_scoreboard.py --only GUITESTER SETTINGS` for a
  timing subset without rerunning the full correctness gates.
- `make gui-benchmark` / `make gui-benchmark-check` write and validate the
  advisory GUI template run-cost artifact (median of repeated wall-clock
  samples; portable shape check, no host-specific budgets).

## Snapshot and theme round-trips

Two headless regressions pin persistence:

- the GUI editor snapshot path (canvas -> markup file -> load -> rebuild) must
  preserve the named element's type, name, rect, color, layer, visibility, and
  text; and
- the theme save/load path must be a byte-identical fixed point, with a reload
  overwriting in-memory token mutations.

Both run inside `make check` (`GUI SNAPSHOT roundtrip tests`,
`STYLE ROUNDTRIP tests`) and feed the GUI scoreboard.

## Generic image review tools

Two display-free helpers cover ad-hoc visual review; both accept PNG (the
capture format) and binary PPM P6, and use only PIL/numpy.

`scripts/snapshot_compare.py` compares two images and exits 0 on pass, 1 on a
visual failure, 2 on usage/input errors. `--tolerance` sets a per-channel
absolute delta that does not count as a difference. By default any changed
pixel fails; bound it with `--max-diff-count` or `--max-diff-fraction`
(count and fraction can be combined). `--max-mean` and `--max-mse` add
optional image-wide error bounds; the summary always reports the count,
fraction, mean absolute RGB error, MSE and maximum channel delta. `--region
x0,y0,x1,y1` applies the whole verdict to a half-open window, so a review can
ignore a known-unstable corner. `--diff out.png` writes a PNG with changed
pixels highlighted red and the unchanged content dimmed; `--json` prints the
machine-readable summary.

```bash
python3 scripts/snapshot_compare.py before.png after.png
python3 scripts/snapshot_compare.py before.png after.png \
  --region 0,0,1280,720 --tolerance 2 --max-diff-count 0 \
  --diff /tmp/opencode/gui_diff.png
```

`scripts/contact_sheet.py` builds a deterministic labeled grid from explicit
PNG/PPM paths or `--dir` (sorted `*.png`/`*.ppm`). All inputs must have the
same pixel dimensions; empty and mixed-size batches fail with exit 2 rather
than being silently resized. `--columns` and `--cell-width` control the grid;
labels are input index plus file name (no timestamps), and `--json` prints the
sheet metadata.

```bash
python3 scripts/contact_sheet.py /tmp/opencode/shots/*.png \
  --out /tmp/opencode/contact.png --columns 4 --cell-width 320
```

`make frontend-image-check` runs the synthetic unit tests; neither tool needs
a display, engine launch, scene name or AI probe.

## Structured layout report

`scripts/layout_report.py` launches the scene matrix with `--layout-dump` and
reports per-scene canvases, element counts, off-screen rects, same-canvas
overlaps and text elements. It uses **scene flags only** — no AI probe,
`--guiapi` or `--debug-ui`. The default matrix is `title`, `gfx`, `battle`,
`showcase`, `showcase-y2k` and `themlab`; `--only` selects a comma-separated
subset.

```bash
python3 scripts/layout_report.py --only title,themlab \
  --out /tmp/opencode/layout-report.md
python3 scripts/layout_report.py --only themlab --format json \
  --out /tmp/opencode/themlab-layout.json --frame 8 --width 1280 --height 720
```

The report is diagnostic: off-screen rectangles and overlaps never make the
command fail. Full-screen background elements are reported as a separate
background count (and remain in JSON) so the foreground overlap table stays
useful; containment and untyped-element context is kept on every row. The
dump's `text` field is a boolean flag, not the string content. Renderer-only
scenes can legitimately dump zero GUI canvases and are reported as such.
Tool, scene-run and parse failures return nonzero; findings do not.

At 2026-10-22 on `--title` + `--themlab` at 1024x768 frame 8 the report saw
2 canvases, 83 elements, 0 off-screen, 55 foreground + 81 full-screen
background overlaps and 59 text elements. `make layout-report-check` runs the
synthetic geometry/schema tests only; an actual report needs the built binary
and a display like the other scene tools.

## Interaction gate

`scripts/gui_interaction_check.py` resolves its click targets from the
engine's own element map instead of hardcoding layout math: a discovery run
with `GX_GUI_TRACE=1` dumps every built element with its rect, the gate clicks
the discovered center through `GX_AI_INPUT`, and the assertions come from the
trace event stream. Rows cover the title menu views (LOAD GAME/BACK,
SETTINGS/BACK), the quit confirm dialog (including background clicks staying
blocked while the modal is open), scene-continue dispatch, the escape-menu
Resume action, a GuiTester checkbox toggle, and a Settings toggle switch.

`GX_GUI_TRACE=1` prints one structured line per event:

```
GUITRACE: click canvas=0 element=12 detail="LOAD GAME"
GUITRACE: title-view canvas=1 element=-1 detail=""
GUITRACE: dialog-open canvas=8 element=-1 detail="Quit"
GUITRACE: modal-close canvas=-1 element=0 detail=""
```

Canvas builds also emit the element map (`GUITRACE: canvas-build ...` plus
one `GUITRACE: element ...` line per live element with name, text, rect, and
state flags), which is what keeps the gate calibration template-owned. The
trace is inert unless the environment variable is set.

## Theme gate

`GX_GUI_THEME=<path>` overrides the active theme file for the run without
touching the persisted settings (audit runs still set `GX_NO_SETTINGS_SAVE`).
`scripts/gui_theme_check.py` uses it to load the default, high-contrast,
color-vision, and blueprint themes; each must report the requested path, keep
its contrast pairs at or above 4.5:1, and render a non-blank frame. The
high-contrast theme must additionally change pixels against the default
theme, so a loaded-but-unapplied theme fails the gate.

## Layout pass

`gX_GUI_Element_Text_Analyze` runs the *same* prepared text path as the draw
call (font selection, resolution scale, opt-in shrink, wrap, auto-size,
ellipsis) and returns the drawn bounds plus `wraps`, `truncated`, `overflow`,
and the applied `shrinkScale`. The `GX_GUI_TRACE` element map carries that
verdict per element (`textfit=W,H wraps=N truncated=N overflow=N shrink=S`),
so `scripts/gui_layout_check.py` fails a template when a label clips,
ellipsizes, or spills out of its box. Findings that are deliberate (the
GuiTester 96 px font-scale demos) are allowlisted in the script with a
rationale; every other finding is a defect to size, reflow, or shrink.

Shrinking is opt-in, not a global rule: `gX_GUI_Builder_Text_AutoShrink` (or
the text module's `autoShrink` field) reduces the font size until the natural
single-line width fits, floored at
`gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE` (0.6) so unreadably small type still
reports truncation. Compact widget labels (button, checkbox, dropdown, radio,
toggle, stepper, tab, slider row) already opt in; headings and hero text
usually should not, because a deliberate size hierarchy is part of the
design.

## Text shadows

The per-element `TEXT_SHADOW` flag (or `gX_GUI_Builder_Text_Shadow`) draws the
label twice: a themed offset/color copy first, then the text. Tokens live in
`gui.style.textShadow`: `offsetX`/`offsetY` are base-1024 pixels scaled with
the font (a high-resolution run keeps the same relative depth), and `color`
alpha softens the shadow without a blur pass — alpha 0 disables it while the
element flag stays set. The bitmap and SDF draw paths share the same prepared
text, so the shadow follows whichever font renders; the title wordmark and
subtitle opt in as the first production use.

`make gui-shadow-check` drives the gate: zeroed tokens render byte-identical
to the shadow-free baseline, the bitmap shadow's pixels correlate with the
glyph mask shifted by the themed offset (down-right beats every other
direction), the SDF demo changes in its ROI, and the layout pass stays green
with shadows forced on every text. The editor's `TEXT_SHADOW` checkbox still
toggles the flag per element; theme authors control the look through the
tokens.

## Gradient depth

The SDF surface program (`guisurface.frag`) carries the authored gradient
modes — vertical, horizontal, and four-point (TL/BL/BR/TR corner order) — so
elements with `GRADIENT_ENABLE` render their gradients instead of a flat
fill on the SDF path too. On top of that, `gui.style.surfaceDepth` selects a
themed depth profile:

- `mode` 0 keeps the authored gradient (or the derived button ramp); 1 and 2
  apply the depth profiles to every SDF surface, including plain panels for
  which a contrasting top/bottom pair is derived from the fill.
- **horizon** (1): a sky/ground ramp with a soft, hue-preserving reflected
  band at `horizonPos` (surface-height fraction), `horizonSoft` for the band
  width and `horizonGlow` for its strength.
- **metallic** (2): a compressed ramp, an anisotropic highlight band
  (`metalBand`, `metalWidth`, `metalStrength`) and a top-edge rim
  (`metalRim`).

`make gui-depth-check` captures the GuiTester fixture with generated themes:
the authored vertical/horizontal ramps must survive the SDF path, the horizon
and metallic peaks must sit within tolerance of their token positions, the
`GX_GUI_SDF=0` fallback must return to the authored ramp without a band, and
repeat runs must be pixel-identical. Defaults keep `mode` 0, so existing
themes render unchanged.

## Aero glass

`gX_GUI_Builder_Shader_Glass` attaches `glass.frag` to an element: the shader
samples the accumulated backdrop (`gui.ping` — the scene capture plus every
canvas already composited) through a small gaussian blur, tints toward the
element color, and adds an inner silhouette rim and a top gloss. Tokens live
in `gui.style.glass` (`blur`, `tint`, `rim`, `refraction`, `gloss`; lengths
are base-1024 pixels scaled with the window). The element's own translucent
rect still draws first, so the flat fill is the fallback when the shader is
unavailable.

`GX_GUI_GLASS=0` is the force-off switch (same pattern as `GX_GUI_SDF=0`),
and zeroed tokens also skip the shader entirely — both paths must render
pixel-identically to the plain translucent fill. `make gui-glass-check` pins
that fallback, the backdrop variance drop under glass, the rim lift, and
repeat determinism. The GuiTester fixture carries the demo panel; any
template or theme surface can opt in with the builder call.

## Cursor light

`gui.style.cursorLight` adds a soft radial highlight to SDF surfaces that
follows the pointer: `strength` (0 = off, the default), `radius` in base-1024
pixels, and a warm `color`. The cursor is mapped per draw path — canvas
buffer coordinates through the inverse canvas transform for immediate
(text-bearing) surfaces, and window pixels through the viewport mapping for
the end-of-frame queued pass — so the highlight tracks the pointer under
docking and scaling too.

`make gui-cursor-light-check` parks the synthesized cursor over two different
controls with `GX_AI_INPUT` and gates the follow direction, the on/off
contrast at the same position, the strength-0 no-op (only hover state may
differ), and repeat determinism. The effect ships disabled pending the cost
and accessibility review; enabling it is a one-token theme change.

## Pause blur

The EscapeMenu's full-screen blur element samples `gui.ping`. On GUI-composited
scenes `ping` accumulates the background plus earlier canvases; renderer
scenes bypass that pipeline, so the draw path seeds `ping` from the current
`renderer.canvas` before drawing the menu (the 2026-09 audit finding 10 fix).
The blur program is `resources/shader/postprocess/blur.frag` — preloaded into
the shader heap under its full relative name, since the builder's lookup is by
exact heap name (the old `blur.frag` basename silently preloaded nothing and
left the blur element programless).

`make gui-pause-blur-check` captures the same `--gfx` frame with and without
`GX_GUI_SHOW_MENU=1`, models the backdrop as the shader's 19-tap gamma-2.2
kernel (offsets/weights mirrored in the script) followed by the theme's
`escape.dimOverlay` blend, and gates the model fit over the backdrop, the fit
beating a dim-only fit (proves the kernel ran), the panel region, and repeat
determinism. Disabling the seed turns the backdrop near-black; dropping the
blur program turns it dim-only — both fail.

## Effect production adoption

The visual effects are opt-in per theme: the default theme keeps the flat
blueprint look (zeroed glass tokens, cursor-light strength 0, flat depth
mode), and `resources/style/theme_aero.json` opts in with an aqua palette,
glass (blur 10 / tint 0.5 / rim 0.5 / refraction 3.0 / gloss 0.32), the
metallic surface-depth profile and a cool cursor light. The escape-menu panel
is a production glass surface — themes with non-zero glass tokens get a glass
pause panel, everything else keeps the flat fill, and `GX_GUI_GLASS=0` forces
it off. `make gui-effect-contrast-check` pins AA contrast of the pause
panel's hint/title/button bands over glass and over the flat panel, with the
cursor light parked away and on the hint; the GUI benchmark gains
`guitester-glass` and `guitester-cursor` rows for the advisory cost delta.

## Capture matrix

The capture matrix writes screenshots and `gui_matrix.md` under
`/tmp/opencode/gui_audit`. `SCENE` is an intentionally transparent full-screen
canvas. All other registered templates are expected to change the captured
image. Add templates to an explicit expected-blank set only when their
transparent behavior is intentional.

GPU capture/timing launches set `GX_SKIP_TESTS=1` so rendering validation is
independent from the headless `--test-only` gate. Direct `--test-only` runs
always execute the full suite.

The scoreboard's completeness percentages are a curated coverage estimate,
not implementation progress telemetry. Runtime costs compare the title-scene
baseline against a template forced onto a second canvas at the default frame
cap; they are advisory, include process startup, and near-zero cost means the
template remains within that frame budget. A fresh complete `gui-check` matrix
is reused when available; pass `--refresh-matrix` to `gui_scoreboard.py` to
capture it again.

## Gate matrix

`scripts/audit_summary.py` registers the quick GUI matrix in its default tier
and adds the full GUI matrix, the interaction matrix, and the theme gate to
`--full` (so `make audit-summary-full` and the silver certificate cover them).
Results are cached by input fingerprint like every other gate; set
`AUDIT_CACHE=0` to force fresh runs.

The GPU-backed targets require a working display (by default `:1`) and remain
separate from the portable headless `make check` gate.
