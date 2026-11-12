# gX editor framework

<!-- //ai: GX-I043 proposal. This is a design note, not an admitted API. -->

Status: **partial** — command registry, shared picker and the first
document-backed preset are implemented; the remaining kernel components are
still a proposal.

## Why

The individual editors work, but each one still owns too much of its own
framework:

- commands and shortcuts are duplicated as buttons and `IsKeyPressed` checks;
- every tool has its own file/preset/model picker and confirmation flow;
- dirty/close/save state is per-tool and sometimes only implicit;
- panel sizes, insets and wrap rules are repeated in each tool;
- headless gates click hardcoded coordinates instead of invoking a command;
- input ownership is a set of probes rather than one explicit owner.

The GFX preset flow is the current example: **Load preset** confirms the path
already in the field, while choosing another preset requires Browse → select →
Use path / Load and replace → confirm.

## Constraints

- Dear ImGui remains the tool-rendering layer. Do not add a second UI toolkit
  or a second event loop.
- Keep the C99/C++ boundary. The kernel is plain C; ImGui stays behind the
  existing C-linkage shim.
- Tools must not depend on the native GUI canvas being complete.
- Keep the current exclusive runtime-tool model and editor host.
- Commands must be callable headlessly by ID.
- No picker, modal or command may publish, quit or overwrite without an
  explicit owner action.
- Start with the two concrete slices below; do not build a generic docking
  rewrite first.

## Kernel components

### Tool registry

A tool declares its identity, open/close lifecycle, panels, commands and
default workspace:

```c
typedef struct gX_EditorTool {
    const gX_I8 *id;
    const gX_I8 *label;
    gX_I0 (*open)(gX_I0);
    gX_I0 (*close)(gX_I0);
    gX_I0 (*draw)(gX_I0);
    const gX_EditorCommand *commands;
    gX_I32 commandCount;
} gX_EditorTool;
```

The host uses this table for tabs, the active-tool label and exclusivity,
instead of maintaining parallel flags and switch chains.

### Command registry

```c
typedef struct gX_EditorCommand {
    const gX_I8 *id;         // "gfx.preset.load"
    const gX_I8 *label;      // "Load preset..."
    const gX_I8 *shortcut;   // "Ctrl+O"
    gX_I1 (*enabled)(gX_I0 *user);
    gX_I0 (*run)(gX_I0 *user);
    gX_I0 *user;
} gX_EditorCommand;

gX_I32 gX_EditorFramework_Command_Register(const gX_EditorCommand *command);
gX_I32 gX_EditorFramework_Command_Unregister(const gX_I8 *id);
gX_I1  gX_EditorFramework_Command_Run(const gX_I8 *id);
gX_I1  gX_EditorFramework_Command_Enabled(const gX_I8 *id);
gX_I32 gX_EditorFramework_Command_Count(gX_I0);
const gX_EditorCommand *
       gX_EditorFramework_Command_At(gX_I32 index);
```

Menus, toolbars, the command palette and tests all call `Command_Run` with the
same ID. Shortcut dispatch is centralized and consults the current input owner
before running.

### Input ownership

```c
typedef enum gX_EditorInputOwner {
    GX_EDITOR_INPUT_GAME,
    GX_EDITOR_INPUT_NATIVE_GUI,
    GX_EDITOR_INPUT_IMGUI,
    GX_EDITOR_INPUT_MODAL
} gX_EditorInputOwner;

gX_EditorInputOwner gX_EditorFramework_Input_Owner(gX_I0);
gX_I0 gX_EditorFramework_Input_Set(gX_EditorInputOwner owner, const gX_I8 *reason);
```

Precedence: modal > ImGui > native GUI > game. This turns the current
`WantCaptureMouse`/`WantCaptureKeyboard` probes and report/host checks into one
explicit per-frame owner. Shortcut routing, wheel ownership and arrow-key
navigation all read the same value.

### Document/session

```c
typedef struct gX_EditorDocument {
    gX_I8 id[64];
    gX_I8 path[512];
    gX_I8 title[128];
    gX_I1 dirty;
    gX_I1 readOnly;
    gX_I0 *context;
    gX_I32 (*save)(struct gX_EditorDocument *doc);
    gX_I32 (*load)(struct gX_EditorDocument *doc);
    gX_I1 (*closeGuard)(struct gX_EditorDocument *doc);
} gX_EditorDocument;
```

One active document per tool in v1. The kernel owns dirty state, recent paths,
the close guard and the save/load lifecycle. Per-tool code supplies the
callbacks. A later version can support multiple documents without changing the
tool-facing contract.

### Shared picker

```c
typedef enum gX_EditorPickerKind {
    GX_EDITOR_PICKER_FILE,
    GX_EDITOR_PICKER_PRESET,
    GX_EDITOR_PICKER_MODEL,
    GX_EDITOR_PICKER_TEXTURE,
    GX_EDITOR_PICKER_THEME
} gX_EditorPickerKind;

typedef struct gX_EditorPickerRequest {
    gX_EditorPickerKind kind;
    const gX_I8 *title;
    const gX_I8 *root;
    const gX_I8 *extensions;
    gX_I1 allowNew;
    gX_I1 showPreview;
    gX_I0 (*accept)(const gX_I8 *path, gX_I0 *user);
    gX_I0 *user;
} gX_EditorPickerRequest;

gX_I0 gX_EditorFramework_Picker_Open(const gX_EditorPickerRequest *request);
gX_I1 gX_EditorFramework_Picker_IsOpen(gX_I0);
gX_I0 gX_EditorFramework_Picker_Draw(gX_I0);
```

Picker contract:

- search + recent list
- keyboard navigation, Enter to accept, double-click to accept
- one-line ellipsis + full-path tooltip for long names
- scale-aware width/height with the display cap
- no file writes; acceptance is a callback
- one modal picker at a time

GFX preset, model, texture, theme and markup pickers migrate to this service.

### Panel registry

```c
typedef struct gX_EditorPanel {
    const gX_I8 *id;
    const gX_I8 *title;
    const gX_I8 *ownerTool;
    gX_I32 defaultDock;      // left/right/center/floating
    gX_F32 minWidth;
    gX_F32 minHeight;
    gX_I1 *visible;
    gX_I0 (*draw)(gX_I0);
} gX_EditorPanel;
```

The host owns positions, layout persistence, summon/hide and reset. This is the
single place for “panel below the host tabs”, “toolbar visible when open” and
“no panel covers another at 175%”.

### Modal/notification manager

- one modal stack, one input owner
- explicit close guard and “dirty document” prompt
- status/toast area shared by tools
- no tool writes directly into another tool’s popup

### Undo transactions

```c
gX_I0 gX_EditorFramework_Undo_Begin(const gX_I8 *label);
gX_I0 gX_EditorFramework_Undo_Commit(gX_I0);
gX_I0 gX_EditorFramework_Undo_Abort(gX_I0);
```

Tools keep their own state representation, but the kernel groups edits and
exposes `undo/redo` command IDs for menus, shortcuts and tests.

### Test/inspection API

```c
gX_I1 gX_EditorFramework_Test_RunCommand(const gX_I8 *id);
gX_I1 gX_EditorFramework_Test_PanelRect(const gX_I8 *id, gX_Rectangle *out);
gX_EditorInputOwner gX_EditorFramework_Test_InputOwner(gX_I0);
gX_I1 gX_EditorFramework_Test_DocumentDirty(const gX_I8 *toolId);
```

Headless gates use IDs and rectangles, not hardcoded mouse coordinates. The
existing `GX_TOOLS_LAYOUT_TRACE`, `GX_GUI_TRACE` and editor-system markers stay
as the low-level evidence layer.

## Implementation status

First slice landed:

- `src/tools/editor/editor_commands.{h,c}` — bounded C command registry with
  register/unregister/run/enabled/count/at.
- `src/tools/editor/editor_picker.{h,cpp}` — shared ImGui picker with search,
  Enter/double-click, ellipsis + full-path tooltip and scale-aware geometry.
- GFX preset actions registered as `gfx.preset.load`, `gfx.preset.reload` and
  `gfx.preset.save`; the inspector buttons and headless probe dispatch them.
- `GX_GFX_PRESET_PICKER=1` and `GX_EDITOR_PICKER_TRACE=1` expose the picker to
  `gfx-editor-layout-check`.
- `src/tools/editor/editor_documents.{h,c}` — caller-owned document state:
  path, dirty/read-only, save/load callbacks, `CanClose` guard. The GFX preset
  marks dirty on model/light history edits and cleans on save/load;
  `GX_GFX_PRESET_TOUCH=1` exposes the state to the layout gate.

Still proposed: wiring a close prompt into the host/tool close paths, panel
registry, input-owner stack, modal manager, undo transactions and the full
command palette.

## Pilot: GFX preset load

Target flow:

1. `gfx.preset.load` — opens the shared preset picker.
2. Picker selection via Enter or double-click — returns the path.
3. `gfx.preset.replace` — opens the replace confirmation modal.
4. Confirm — document load, camera read, status.
5. `gfx.preset.reload` — reloads the path already in the field.
6. `gfx.preset.save` / `gfx.preset.saveAs` — existing guarded save flow.

The path field, Browse button, Default/Y2K/Empty shortcuts and Load/Reload
buttons all dispatch commands instead of owning separate state.

## Slices

1. **Command registry + shortcut routing.**
   Register the GFX preset load/reload/save commands and the GUI editor
   save/load/undo commands. Menus and buttons dispatch by ID. Add a minimal
   `Ctrl+P` command palette only if it fits the slice; otherwise keep the
   registry headless-first.
2. **Shared picker.**
   Implement the preset picker first, then migrate the GFX model/texture and
   markup/theme pickers. Add double-click, Enter, recents and scale-aware
   geometry.
3. **Document/session + close guard.**
   Move the GFX preset and GUI markup save/load/dirty state behind the document
   struct.
4. **Panel registry + layout persistence.**
   Move scene/inspector/asset/footer and the tool panels onto declared panels.
5. **Schema inspector + undo transactions.**
   Start with the GFX material/model rows; unify value formatting and reset.
6. **Input-owner stack + modal manager.**
   Replace the remaining per-tool capture probes.
7. **Test kit + framework gate.**
   Add `editor-framework-check` once the command and picker surfaces are
   stable.

## Gates

- `editor-framework-check` (new, quick): command registration/run/enabled,
  picker open/accept/cancel, input-owner precedence, dirty close guard.
- `gfx-editor-layout-check`: preset picker and command rows at 100/175%,
  long-path ellipsis, action-row fit.
- `editor-system-check`: state transitions and host/toolbar visibility remain
  authoritative.
- `editor-input-check`: click/key/wheel arbitration remains authoritative.

## Non-goals

- no second UI toolkit or event loop
- no generic docking rewrite in the first slice
- no automatic publication, file writes or destructive loads from a picker
- no replacement of the existing scene/renderer architecture
- no public API freeze until the kernel survives the pilot

## Open questions

- Should the command registry own shortcut parsing, or expose commands to the
  existing ImGui key map?
- Is the document model per tool or host-owned for cross-tool recents?
- Should the picker be synchronous (v1) or request/response-based (v2)?
- Do we need multiple documents per tool before the GFX preset pilot?
- Does the panel registry replace the host suite format or serialize into it?
