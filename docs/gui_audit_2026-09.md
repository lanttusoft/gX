# gX GUI Audit - 2026-09-12 (v0.6.32)

Scope: current GUI architecture, lifecycle, heap ownership, rendering,
transitions, input/focus, widgets, text input, gameCSS, themes, accessibility,
tooling, and automated coverage. Method: static source review, four independent
read-only audit passes, the existing template capture matrix, scripted mouse
input, screenshot comparison, and the repository verification gate.

This document records the initial findings and their repair status. Earlier
resolved findings remain in `docs/gui_audit_v2.md` and were used only as
historical context.

## Repair status

Repairs applied 2026-09-12 and verified by the debug build plus
`./build/gX_build --test-only`:

2026-09-28: finding 1 gained canary-guarded unit cases — sentinel bytes on
both sides of the caller buffer plus tail checks across capacity 1/4/8/16,
short-text no zero-padding, the maxLength pattern and repeated syncs
(`gX_Tests_TextInput`). An injected past-capacity write fails three rows.
Finding 11 gained slot-recycling cases (`gX_Tests_GUI_EditorIdentity`):
stale selection and slider-field handles are refused after an external
target rebuild; a dropped generation check fails two rows. Coverage gap 1
gained partial-rebuild cases (`gX_Tests_GUI_CallbackRebuild`): refreshing one
canvas repeatedly leaves another canvas's callback contexts untouched; an
injected shared-pool alias fails every round. Coverage gap 3 gained the
pause-blur pixel gate (`make gui-pause-blur-check`) and the fix it found: the
GUI shader preload used the `blur.frag` basename while the file lives at
`resources/shader/postprocess/blur.frag`, so the EscapeMenu blur program was
silently NULL (no blur at all); the preload now uses the full heap name.

| Finding | Status | Repair |
|---:|---|---|
| 1 | Resolved | External buffers carry capacity and bounded sync is tested. |
| 2 | Resolved | Rendering uses a sorted index list; element heap slots are never reordered. |
| 3 | Resolved | Remove, update, draw, focus, picking, CSS, and schema paths require live slots. |
| 4 | Resolved | Widget callback context is stored on each element's data module (`gX_GUI_OnClick_Context`); the global arg/widget pools were removed. |
| 5 | Resolved | Resource watches own compilation; GUI modules use lifetime-managed observers. |
| 6 | Resolved | Focus transfer and interaction eligibility are centralized. |
| 7 | Resolved | Rendering and picking share canvas scale, offset, and angle transforms. |
| 8 | Resolved | Cascade order is lexicographic and matching declarations are not capped at 64. |
| 9 | Resolved | UTF-8 edits use codepoint boundaries; the keyboard queue is explicitly ASCII. |
| 10 | Resolved | Renderer-direct frames seed `gui.ping` before pause blur. |
| 11 | Resolved | Selection and slider bindings are generation-checked handles; `gX_Tests_GUI_EditorIdentity` pins the slot-recycling contract. |
| 12 | Resolved | Theme reload restores C defaults before applying JSON. |
| 13 | Resolved | Reduced motion snaps transitions; line-height feeds default text spacing. |
| 14 | Resolved | Toast dismissals consume the pointer click. |
| 15 | Resolved | Transition initialization precedes phase advancement. |

The earlier follow-ups are complete: editor selections and slider bindings
now use generation-checked handles (`f56aed0`; recycling coverage added
2026-09-28 in `gX_Tests_GUI_EditorIdentity`). Remaining admission-gate gaps
are coverage-only, listed below.

## Executive summary

| Severity | Count | Main risk |
|---|---:|---|
| Critical | 1 | Live text-input memory corruption |
| High | 5 | Heap identity, stale slots, callback aliasing, shader reload, focus |
| Medium | 5 | Transforms, CSS, UTF-8, pause blur, editor identity |
| Low | 4 | Theme reload, accessibility integration, click-through, transition timing |

The first fix should be the text-input external-buffer copy. It writes beyond
both live caller buffers every frame in the affected templates. The next
architectural priority is to stop treating a mutable, sorted element array as
both render order and stable heap identity.

## Findings

### CRITICAL

1. **Text inputs overwrite caller-owned buffers every update.**

   `gX_GUI_Element_TextInput_Update()` copies with a fixed count of
   `gX_GUI_ELEMENT_TEXT_MAX - 1` and then writes a terminator at that fixed
   index (`src/gui/element/textinput.c:248-251`). `SetExternalBuffer()` accepts
   only a pointer, so the implementation has no destination capacity
   (`src/gui/element/textinput.c:86-88`). This is not limited to long input:
   `strncpy` zero-pads through its full count for short strings.

   Two current call sites are undersized: ThemLab passes `g_inputBuf[64]`
   (`src/gui/templates/themlab.c:34,153-154`) and GuiTester passes
   `g_textInputBuf[128]` (`src/gui/templates/guitester.c:11,327-329`). Both
   configure `maxLength=32`, but the sync ignores that bound and writes 512
   bytes including the explicit terminator. Adjacent static widget state can
   therefore be cleared or corrupted during an ordinary frame.

   Required direction: make destination capacity part of the external-buffer
   contract, or copy at most the configured `maxLength` when the builder
   guarantees a `maxLength + 1` destination.

### HIGH

2. **Layer sorting breaks heap-slot identity and metadata ownership.**

   `gX_GUI_Canvas_Draw()` sorts `gX_GUI_Element` structs in place by `layer`
   (`src/gui/canvas.c:348-357`) but leaves the parallel `gX_Heap_Meta` array
   untouched. Element `id` values also remain their original heap slots. After
   a permutation, `USED`, `UPDATE`, and `DRAW` can describe a different
   element than `elements[i]`; focus indexes and editor selections can target
   a different control; and a later first-empty insertion can overwrite an
   active element moved into a metadata hole.

   Layout lookup illustrates the conflicting models: it searches moved
   elements by stable `element->id`, but filters them through slot metadata at
   the current array index (`src/gui/element/layout.c:223-230`).

   Required direction: keep heap storage stable and sort an index/pointer list
   for rendering. Do not reorder owning element structs.

3. **Removed element slots remain active in core loops.**

   `gX_Heap_Remove_Index_NoPtr()` only clears `meta[index].USED`
   (`src/common/heap.c:424-428`). Element unload then restores defaults,
   including a visible 1x1 black rectangle (`src/gui/element/element.c:57-82,
   121-169`). `Canvas_Clear()` does not shrink `elements.amount`
   (`src/gui/canvas.c:162-198`).

   Canvas update and draw use `UPDATE`/`DRAW` without `USED`
   (`src/gui/canvas.c:307-321,385-415`), occlusion ignores all element metadata
   (`src/gui/update.c:171-197`), focus enumeration checks `UPDATE` but not
   `USED` (`src/gui/update.c:537-548`), and schema creation serializes every
   index through `amount` (`src/gui/schema.c:596-601`). A rebuild that creates
   fewer elements can therefore retain ghost draw, occlusion, and serialized
   entries. A trailing removed slot can also become the topmost 1x1 occluder
   at the canvas origin.

   Required direction: every live-element traversal must require `USED`, and
   removal must clear the full operational metadata or compact safely.

4. **Per-canvas refresh invalidates callback state on untouched canvases.**

   The refresh pass resets every global widget and argument pool, then rebuilds
   only canvases whose `refresh` flag is set (`src/gui/update.c:115-149`). The
   direct click callback remains attached to existing elements, but its
   `userData` often points into a global fixed pool such as `g_argsPool`
   (`src/gui/binding.c:93-110,269-275`) or a widget-specific pool such as the
   scroll-list pool (`src/gui/builder_widgets.c:666-683,724-761`).

   The refreshed canvas starts allocating again at pool index zero. Existing
   canvases retain old indexes that now alias the new entries, so a click can
   mutate another widget's pointers or state. Debug-display and widget-driven
   per-canvas refreshes make this a normal runtime path rather than a teardown-
   only concern.

   Required direction: store callback context with the owning element, use
   stable per-canvas storage, or rebuild every owner whenever a shared pool is
   reset.

   Resolved (2026-09-13): `gX_GUI_OnClick_Context()` copies the packed widget
   arguments into the owning element's `gX_GUI_Element_Data` module, and the
   callback receives that inline buffer at execute time. Back, checkbox,
   dropdown, radio, toggle, stepper, tab panel, scroll list and the
   text/texture buttons all migrated; `gX_Widget_PackArgs`, `g_argsPool`, the
   per-widget fixed pools and their `*_ResetPool`/`Pools_Reset` APIs were
   removed. The only retained global is the scroll-list geometry registry used
   by the non-callback wheel/drag input paths, which is reset only on a full
   canvas rebuild. Plain `gX_GUI_OnClick` keeps arbitrary external `userData`.

5. **GUI shader registration clobbers the resource shader's hot-reload watch.**

   Shader loading registers source paths with the compiler watcher
   (`src/wrappers/shader.c:772-792`). GUI element loading then calls
   `gX_Shader_Compiler_Watch_Ex()` for the same program with all paths `NULL`
   and a pointer to the transient GUI shader module
   (`src/gui/element/shader.c:127-139`). `Watch_Ex()` reuses the program's
   existing slot and overwrites its paths, mtimes, callback, and user data
   (`src/common/shader_compiler.c:1085-1114`).

   This removes normal file-change detection, permits only the last GUI
   element using a shared program to receive reload notification, and leaves
   callback data owned by an element heap that has no matching unwatch step.
   A pending or subsequently re-armed watch can dereference stale module
   memory.

   Required direction: make reload observers additive and lifetime-aware.
   GUI elements must not replace the resource watch that owns compilation.

6. **Focus, visibility, and disabled state do not form one interaction model.**

   Click-to-focus sets the new element's `FOCUSED` bit but does not clear the
   previous element; old focus is cleared only inside the keyboard-event block
   (`src/gui/update.c:459-480,603-620`). A scripted runtime test confirmed two
   simultaneous focus rings in `screenshot/gXss_260912_130043.png`.

   Occlusion does not check `visible`, `USED`, `DRAW`, or whether an element is
   interactive (`src/gui/update.c:177-195`). Keyboard focus collection does
   not check `visible` or `DISABLED` (`src/gui/update.c:537-548`). Global
   keyboard/gamepad activation can set `CLICKED_LEFT` on such an element before
   its data modules update (`src/gui/update.c:554-570`). The disabled early
   return in element state update does not clear prior state bits
   (`src/gui/element/element.c:190-197`).

   Required direction: centralize focus transfer and use one eligibility
   predicate for pointer picking, focus traversal, and activation.

### MEDIUM

7. **Canvas transition transforms do not match rendering and hit testing.**

   ZOOM computes and writes `canvasScale`
   (`src/gui/transition.c:304-349,463-472`), but the field is never read by a
   rendering path. It is only initialized, reset, and serialized. A DEMO2
   baseline and active-ZOOM capture confirmed pixel-identical bounds for the
   blue rectangle (`x=69..321, y=64..189`) and green rectangle
   (`x=69..130, y=255..316`). Captures:
   `screenshot/gXss_260912_130354.png` and
   `screenshot/gXss_260912_130410.png`.

   Buffered canvas compositing applies `canvasBox`, `canvasOffset`, and
   `canvasAngle` (`src/gui/canvas.c:423-436`), while occlusion compares the
   game-space mouse directly with the untransformed local element rectangle
   (`src/gui/update.c:157-186`). SLIDE/SKEW visuals can therefore move away
   from their interaction targets. Enter/Space activation is also evaluated
   after the transition-out mouse gate and can restore a blocked click
   (`src/gui/element/element.c:264-304`).

8. **The gameCSS cascade key is not lexicographic.**

   The documented order is builder/default origin, attached stylesheet,
   inline style (`src/gui/css.h:41-48`). Candidate sorting instead uses
   `origin * 64 + important * 2 + specificity * 3`
   (`src/gui/css.c:802-809`). ID specificity is 100 and class specificity is
   10 (`src/gui/css.c:422-437`), so a default-origin ID rule can outrank a
   later-origin rule. Within one origin, one specificity point outweighs
   `!important`. Matching declarations are also silently truncated to 64
   candidates (`src/gui/css.c:842-874`).

   Required direction: compare origin, importance, specificity, and source
   order as separate fields, in the intended precedence order.

9. **Text handling advertises partial UTF-8 behavior but editing remains byte based.**

   The wrap decoder reads two, three, or four bytes without validating the
   remaining string length or continuation bytes
   (`src/gui/element/text.c:72-80`), allowing an out-of-bounds read for malformed
   truncated input. The character queue only scans keycodes 32 through 96
   (`src/input.c:390-412`). Cursor movement, selection, deletion, insertion,
   and password masking operate on bytes
   (`src/gui/element/textinput.c:98-159,211-243,274-277`).

   In addition, insertion checks maximum length before deleting an active
   selection (`src/gui/element/textinput.c:112-121`), so replacing selected
   text fails when the field is currently full.

10. **Renderer-scene pause blur reads stale GUI background state.**

    Renderer scenes blit directly and return before the normal GUI background
    capture/compositing pipeline (`src/gui/gui.c:1049-1104`). The escape-menu
    blur shader explicitly samples `gui.ping`
    (`src/gui/templates/templates.c:149-170`). Opening the pause overlay over a
    GFX, sky, water, wireframe, or raymarch scene can therefore blur an old
    frame or unrelated prior scene rather than the displayed 3D frame.

11. **GUI editor selection is slot-based across target rebuilds.**

    The editor stores a global selected array index and binds slider callbacks
    directly to `&sel->rect.*` and `&sel->params.scale.x`
    (`src/gui/templates/guieditor.c:49-75,124-134,301-310`). Its REBUILD
    callback refreshes the target but not the editor. In-place layer sorting or
    a target rebuild that changes element order can make the same slot refer to
    a different element; target canvas unload can make the bound pointers
    invalid. The editor needs stable element handles/generations, not raw array
    indexes and field pointers.

### LOW

12. **Theme hot reload does not restore defaults before applying JSON.**

    C defaults are assigned during `gX_GUI_Load()` (`src/gui/gui.c:293-456`).
    Subsequent reloads call only `gX_GUI_Style_LoadDefault()` over the existing
    struct (`src/gui/style.c:245-265,374-384`). Removing a key from the theme
    therefore retains its previous loaded value instead of falling back to the
    documented C default.

13. **Accessibility tokens are only partially connected.**

    `motionReduced` changes only hover interpolation
    (`src/gui/element/element.c:326-340`). Canvas transitions, the escape-menu
    fade, and element animation modules ignore it. `motionBase`, `motionLong`,
    and `typeLineHeight` are loaded but have no consumers outside the schema
    (`src/gui/style.c:107-124`). Text defaults to a 1.0 line-height multiplier
    (`src/gui/element/text.c:51-56,303-310`). Tooltips appear only for
    `HOVERED`, not keyboard `FOCUSED` (`src/gui/element/element.c:465-483`).

14. **Toast dismissal does not consume the click.**

    Any left click hides the toast before occlusion and element dispatch
    (`src/gui/update.c:320-337`). The same click remains available to a control
    behind the toast. This is currently low impact because the message canvas
    itself is mostly non-interactive, but the behavior is surprising for an
    overlay and unsafe if the template gains actions.

15. **Transition setup loses its first time step.**

    The source comment says setup reset runs before phase advancement, but
    `Sub_Do()` executes first and the reset follows
    (`src/gui/transition.c:259-284`). The first frame's accumulated time is
    therefore discarded. Delayed canvases also use only `out.T` as their
    clock (`src/gui/transition.c:330-336`), which resets after the out phase;
    the dormant `transitionDelay` feature cannot stagger the complete
    out/middle/in sequence correctly.

## Verification

| Check | Result |
|---|---|
| Debug build | PASS |
| `./build/gX_build --test-only` | PASS, `TEST_ONLY: 0 errors - exiting` |
| Resource audit | PASS |
| Shader/model validation | PASS, 135/135 shaders and 12/12 models |
| `scripts/gui_audit.py` | 12/13 classified RENDERED; SCENE is intentionally transparent |
| Scripted focus test | Historical failure; fixed by exclusive focus transfer |
| Scripted ZOOM test | Historical failure; fixed by shared canvas transform |
| Full `make check` | FAIL at the pre-existing lean-size baseline |

The title/GFX smoke recipes in `Makefile` use `|| true`, so their exit status
does not currently enforce stability. The functional test stage of `make check`
passes; its lean gate reports binary `5442888` vs baseline `5287792` and BSS
`2782512` vs baseline `2514352`.

## Coverage gaps

Automated GUI coverage includes word-wrap, text-input capacity and replacement,
shader observers, CSS cascade/overflow, canvas transforms, style reload, and
core element-lifecycle tests. All four 2026-09 admission-gate gaps are closed:

1. Repeated partial canvas rebuilds with independent callback contexts on
   different canvases — `gX_Tests_GUI_CallbackContext` plus the partial-rebuild
   cases in `gX_Tests_GUI_CallbackRebuild` (2026-09-28); an injected
   shared-pool alias fails all rounds.
2. GUI editor selection after an externally initiated target rebuild —
   `gX_Tests_GUI_EditorIdentity` (2026-09-28).
3. Renderer-scene pause blur pixel accuracy — `make gui-pause-blur-check`
   (2026-09-28): the backdrop must match the current frame's blur+dim model
   over `--gfx`; a disabled seed and a programless blur both fail.
4. Transition delay sequencing across the complete out/middle/in cycle —
   the stagger cases in `gX_Tests_GUI_Transition`.

## Recommended repair order

1. Bound external text-buffer writes and add canary-backed tests.
2. Replace in-place element sorting with a render-order index list.
3. Require `USED` in all element traversals and clear stale metadata on remove.
4. Centralize interaction eligibility and exclusive focus transfer.
5. Replace global widget context pools with stable owning storage. *(Done:
   owner-scoped callback contexts stored on each element's data module.)*
6. Separate shader watch ownership from reload observers.
7. Make transition render and input transforms use the same matrix.
8. Correct the CSS comparator and add deterministic cascade tests.
9. Define either a complete UTF-8 text-input contract or an explicit ASCII one.
10. Connect reduced-motion and line-height tokens to all relevant paths.
