# Textwriter render tests

<!-- //ai: renderer-level textwriter tag/reveal/choice matrix; complements parser tests. -->

`scripts/textwriter_render_check.py` captures what the textwriter renderer
actually draws. It complements `src/renderer/textwriter_tests.c` (parser,
segmentation, choice data) and `scripts/textwriter_editor_check.py` (editor
open/routing); it does not duplicate either.

## Test hooks

`src/renderer/textwriter.c` carries two test-only, default-neutral hooks:

- `GX_TEXTWRITER_FILE` overrides the auto-loaded fixture path.
- `GX_TEXTWRITER_LINE` selects a line after load/jump, clamped to the loaded
  line range.

Unset, the scene still auto-loads `resources/text/textwriter.txt` and starts at
line zero. No shipped text, parser syntax, editor or scene code is changed.

## Matrix

Fixtures are temporary; no shipped text is edited. Captures are 1024×768 at a
fixed timestep.

| Case | Fixture line | Frame | Covers |
|---|---|---:|---|
| `plain-early` | plain | 8 | first glyphs |
| `plain-mid` | plain | 70 | mid reveal |
| `plain-full` | plain | 220 | full plain line |
| `tagged-full` | tagged | 260 | color, red/green/blue, bold, italic, underline, size, spacing, matrix, wave, rainbow, gradient, shake, disintegrate |
| `tagged-repeat` | tagged | 260 | byte-identical repeat |
| `choice` | choice | 220 | `GX_TEXTWRITER_LINE=2` and both option labels |

The tagged line uses the existing inline parser syntax (`<color>`, `<b>`,
`<i>`, `<u>`, `<size>`, `<spacing>`, `<matrix>`, `<wave>`, `<rainbow>`,
`<gradient>`, `<shake>`, `<disintegrate>`). The choice fixture uses
`<choice><option line='N'>...</option></choice>`; the existing parser renders
the option labels inline (currently adjacent, without a separator), and the
suite records that current layout rather than inventing a second format.

## Commands

Agent/local closure (synthetic tests only):

```bash
python3 -m py_compile scripts/textwriter_render_check.py
python3 -m unittest scripts/test_textwriter_render_check.py
make check-fast
```

GPU captures (main, after integration):

```bash
make textwriter-render-check
make textwriter-editor-check
```

The script writes captures under `/tmp/opencode/textwriter_render_check`
(overridable with `--out`) and `report.json`/`report.md`. Use `--only
plain-full,tagged-full,choice` for a subset.

## What it checks

- All captures are finite and exactly 1024×768.
- The text ROI has non-zero ink pixels, an ink bbox and a minimum ink fraction.
- Plain reveal at frames 8/70/220 is strictly increasing.
- A tagged capture is distinct from the plain full capture.
- A choice capture (selected with the line hook) is distinct and non-empty.
- The tagged repeat is byte-identical (hash and pixel delta).
- Per-case ink extent/bbox and plain-vs-tagged/plain-vs-choice deltas are
  reported for main review.

Thresholds are regression floors, not aesthetic approval. Do not lower one to
make a capture pass; report the state instead.

## Limits / non-goals

- This is not OCR: the suite proves ink/reveal/state differences, not that a
  particular glyph string is semantically correct.
- It does not test every parser tag combination, shipped text content, the
  editor UI, dynamic choice navigation or font coverage.
- The choice labels currently render as adjacent inline text; no new choice
  widget or layout convention is introduced.
- GPU/Xvfb captures are not authoritative. Main runs the target on the real
  GPU and reviews the captures; `textwriter-editor-check` and
  `textwriter_tests.c` remain the editor/parser authorities.
