# gX — Scripting (AngelScript shell)

<!-- //ai: shell contract — gx_shell.so dynamic module, gate in check-scripting. -->
Status: **a dynamic module** (`gx_shell.so`) — `GX_USE_ANGELSCRIPT` (default
`OFF`) now builds the module instead of a second exe. The default exe compiles
no scripting code and stays module-capable through the kernel host
(`src/module/`, `docs/dynamic_modules.md`); the default Release payload and
the lean gate are untouched.

```bash
cmake -B build/scripting -DGX_USE_ANGELSCRIPT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/scripting -j$(nproc)
make check-scripting          # the same build + the module/host behavior probes
./build/scripting/gX_build --module build/scripting/gx_shell.so \
    --script path/to/script.as      # or GX_MODULE=<path>, or modules/*.so auto-load
```

The exe and the module must come from the same build profile (the module
resolves engine symbols from the exe's curated export list,
`modules/module_exports.txt`; `make module-exports` regenerates it and relinks
— `make check-scripting` fails on drift). `--script`, `--console` and
`--script-editor` stay in the default CLI and warn when no module is loaded.

## Role

A shell for gX: script-authored demo/art tours and engine-internal test/audit
drivers. The 2026-10-12 port (from gameX's `src/scripting*`) distilled the
host and a core API subset; the game-content, GUI-test, AI-probe and
demoscript bindings did **not** come along. See `docs/porting.md` for the
feature-first porting contract and the drop-list decision note.

## Architecture

- `src/scripting.h` / `src/scripting.cpp` — host: engine lifecycle, script
  heap, per-frame execution, mtime hot-reload, runaway watchdog, per-script
  virtual-input merge, companion-script events, shell counters.
- `src/scripting/script.{h,cpp}` — `.as` file loading (single or with library
  files prepended), module compilation, per-script context + line callback.
- `src/scripting/hooks.{h,cpp}` — named callback registry (`Hook(name, func)`
  from scripts).
- `src/scripting/api_core.{h,cpp}` — math/easing, `Print`, `Yield`/`Sleep`,
  key state, mouse state, time, render scale, screenshot, `SwitchScene` +
  `SCENE_*`, `Vec3`, virtual input (`GUIClick`/`GUIKey`/`GUIClickElement`),
  GUI canvas/element queries (`GUIFindElement`, text/rect/state getters,
  `GUIAssertTextContains`), the Hook bridge, and the shell assert/quit
  harness.
- `src/scripting/api_state.{h,cpp}` — `ScriptSet*/ScriptGet*` KV store that
  survives hot-reload, plus file persistence (JSON; default
  `save/script_state.json`, `GX_SCRIPT_STATE` overrides, `=0` disables;
  loaded at shell load, saved at unload, explicit via
  `ScriptSaveState`/`ScriptLoadState`).
- `src/tools/script/script_panel.cpp` — the ImGui debugger panel (script
  list, pause state, breakpoint toggles, Continue/Step/Over/Out); opened by
  `--script-editor` or `GX_TOOLS_OPEN=script`.
- `src/console/` — the developer console: `console.c` (ring log, history,
  built-ins, dispatch), `console_exec.cpp` (one-liner AngelScript bridge) and
  `console_panel.cpp` (ImGui panel). `--console` opens it at load, F10
  toggles, `GX_CONSOLE_EXEC` runs a newline-separated batch headlessly.
- `external/angelscript/` — vendored SDK 2.38.0 source subset (zlib/libpng),
  compiled in-tree. See `license/NOTICE.md`.

Wiring: `src/module/gx_shell.c` (the module entry registers the frame/scene/
lifecycle hooks and the kernel interop: markup onclick + companion scripts,
the ImGui debugger panel, the console overlay), `main_load` (module load
before the tools/markup builds), `main_loop` (scene → **modules** →
renderer/gui; draw hooks after the GUI), `main_unload` (module shutdown before
engine teardown), `main_update` (`scene_exit`/`scene_enter` around the module
responses), `src/gui/markup.c` (host interop seam).

## Behavior contract

- `void main()` is the entry point. It runs when the script is added; once it
  returns, the script stops, unless it suspends via `Yield()`/`Sleep(ms)` —
  then it resumes on a later frame (and `Sleep` waits on `maindata.timeElapsed`).
- Runaway watchdog: a line callback counts executed lines per frame; a script
  over the budget is suspended + killed (the frame loop never hangs).
- Hot-reload: `.as` files are mtime-watched (rate-limited, CSS-style). A
  suspended `main()` restarts from the top; the script keeps running.
- Hooks: `Hook("name", Func)` registers a `void()` callback; `TriggerHook`
  fires callbacks on fresh contexts. Built-in hooks: `scene_enter` (after the
  new scene's flags are set) and `scene_exit` (before the old scene's flags
  are cleared).
- State: `ScriptSetInt/Float/String` + getters survive hot-reload; the module
  rebuild resets script globals, the C-side store does not. It also persists
  across processes: loaded at shell load, saved at unload to
  `save/script_state.json` (JSON array of typed entries; `GX_SCRIPT_STATE`
  overrides the path, `=0` disables).
- Debugger: breakpoints (16 per script) pause the line callback; Continue
  passes the paused line once, Step/StepOver/StepOut move the context.
  `DebugIsPaused`/`DebugGetLine` read state; `ScriptCount`/`ScriptGetPath`/
  `ScriptGetName` enumerate. The ImGui panel (`--script-editor`,
  `GX_TOOLS_OPEN=script`) wraps the same API.
- Shell harness: `AssertTrue`, `AssertEqual`, `AssertFloat`, `Fail`,
  `FailCount`, `Quit(code)`. Any failed assert (or explicit `Quit`) drives the
  process exit code — `gX_build --script gate.as` returns non-zero on failure.
- Markup bridge: a markup's `script` file lazy-loads at build time; an
  element's `onclick` runs the named handler via
  `gX_Scripting_RunFunctionByName` (fresh context; safe while `main()` sleeps).
- Load paths: `--script <path>` (one-shot), the boot scan of
  `resources/script/*.as` (`GX_SCRIPT_SCAN=0` opts out, `GX_SCRIPT_DIR`
  overrides the directory), the mod scan of `<mods>/<name>/script/*.as`
  (`GX_SCRIPT_MOD_DIR` overrides the root, default `custom`), and markup
  companion scripts. Scans load in sorted order for deterministic boots.
- Headless markup: `GX_GUI_MARKUP=<path>` registers a markup template and
  builds it on the audit canvas (2) *without* the editor, so scripts can
  query and click it (`--tools` remains the editor path; its edit lock keeps
  element actions inert by design).
- Per-script virtual input: each script owns a `gX_Input_Virtual` slot,
  folded into `input.virtInput` at the end of the update. `GUIClick`/`GUIKey`
  write to the executing script's slot; `GUIClickElement` converts an
  element's buffer-space rect center to screen pixels via
  `gX_GUI_Canvas_ToScreenPoint` (offset/scale/angle aware).

## Tour APIs

gX-native remakes of gameX's reel-scripting surface (`src/scripting/tour/`,
all in the shell build). Landed: camera, gfx, audio, background, synth,
textwriter, overlay, raymarch, pp — the reel surface is complete.

- Camera (`tour_camera.cpp`): `Camera_GetPos/SetPos`, `GetTarget/SetTarget`
  (Vec3), `GetFov/SetFov`, `Camera_Keyframe_Clear/Add/AddBezier/Start/Stop/
  IsRunning`, `Camera_Rail_Count/AddGrab/Save/Load` (JSON persistence; saves
  refuse shipped preset paths under the harness guard) and `Time_GetDeltaTime`.
  The rail lives in the shell (gameX had it in `camera.c`; gX's module was
  left without one) and ticks from a scripting update hook, wall-clock timed,
  driving `camera.camera3D` (the renderer's scene camera). Keyframe `t` =
  duration of the segment arriving at that keyframe; `AddBezier` sets the
  outgoing position handles for the segment leaving the added keyframe;
  target/fov interpolate linearly. The rail editor (`src/tools/tour/
  rail_editor.cpp`) is a module-drawn panel over the same data — grabs from
  the live camera, per-keyframe t/pos/target/fov/bezier editing, reorder/
  remove, play, and JSON presets — opened by `--rail-editor`,
  `GX_TOOLS_OPEN=rail` or its EXTRAS card; its gate lives in
  `make check-scripting`. The shipped example `resources/script/tour/
  tour_demo.as` flies the rail sample (`resources/tools/camera/
  orbit_sample.json`) over the GFX scene with tagged captions and exits when
  the rail finishes — run it explicitly with `--script` (the flat boot scan
  skips subdirectories); the gate runs it headless alongside the rail
  round-trip probe.
- Gfx (`tour_gfx.cpp`): `Gfx_Flag_Get/Set` over a readable flag table
  (`"bloom"`, `"ssr"`, `"shadows"`, `"procedural_sky"`, ... — 50+ names over
  `renderer.gfx.state`), typed setters (`Gfx_SetFog*`, `Gfx_SetSky*`,
  `Gfx_SetSun*`, `Gfx_SetBloom*`, `Gfx_SetSSAO*`, `Gfx_SetCascadeCount`),
  and the dynamic scene: `Model_Add/SetPos/SetRotate/SetScale/Remove/Clear`,
  `Light_Add/SetPos/SetColor/SetBrightness/Remove/Clear` (Vec3-based).
- Audio (`tour_audio.cpp`): `Music_Load/Play/Stop/SetVolume/GetPosition/
  GetLength/IsPlaying`, `Sound_Beep(freq, ms, vol)`, and the master filter
  (`Audio_SetMasterFilter(enabled, type, cutoff, resonance)` +
  `Audio_GetMasterFilterEnabled/Mode/Cutoff/Resonance`, `AUDIO_FILTER_LP/
  HP/BP`) — the shared TPT core on the final mix, off by default.
  `Sound_PlayFile` is not bound yet (the sound pool lives behind the
  raylib-typed header the shell avoids).
- Background (`tour_background.cpp`): the live background heap
  (`renderer.background.data`; present in the title, battle and `--background`
  scenes) driven through the full param struct. Transform
  (`Background_SetScale/Rotate/Translate/Stretch/Perspective/Affine` +
  `*Vel/*Accel`, origins, `SetMatrixBlend`, `SetMatrixA/B`,
  `SetUseTransformMatrix/UseDirectMatrix`), special (`SetWarp*`,
  `SetWave*`, HDMA channel setters, noise mode/scale/speed/octaves),
  the 8 sinusoids (13 fields each, `SetSinusoid*`), palette
  (`SetPaletteMode/RowCount/Size/Amount/Speed/Frequency/Color1/Color2/
  Color/bg+fg boundaries/bg+fg colors`, blend/color-math), misc
  (`SetBoundaryXY/SetBoundaryZ/SetModulo`), state toggles,
  `Background_React(type, intensity, duration)` (`BG_REACT_*`), texture
  (`SetTexture/IsTextureReady`), presets (`LoadDefault/LoadBuiltIn/LoadPreset/
  SavePreset`, `BG_PRESET_*`) and the preset morph (`Transition(path, ms,
  ease)` + `BG_TRANS_*`, `TransitionStop/IsTransitionActive`). gX specifics
  the bindings respect: setters mark the module's dirty gates
  (`paramsDirty/colorsDirty/sinusoidDirty`) because the per-frame updates are
  skipped when nothing changed; read-backs (`GetScale/GetRotate/
  GetMatrixBlend/GetPaletteMode`) return the script-set `constant`; transition
  duration is milliseconds so it pairs directly with `Sleep`.
- Synth (`tour_synth.cpp`): `Synth_NoteOn/NoteOnPatch/NoteOff/AllNotesOff`
  (the note returns its voice slot for per-voice edits), per-voice
  `Synth_SetADSR/SetPan/SetFilter/SetLFO/SetFM/SetPulseWidth/SetPortamento`
  and the default-setting twins (`Synth_SetDefault*` applied to new notes,
  `Synth_ApplyDefaultsToActive`, `Synth_SetMasterVolume`), delay
  (`Synth_SetDelay/SetDelayEnable`), the arpeggiator (`Synth_Arp_*`) and the
  16-step sequencer (`Synth_Seq_*`) — the engine ticks both per frame now
  (`gX_Synth_Update`), so scripts only play notes — patches
  (`Synth_Patch_SaveToBank` returning 0/1, `Synth_Patch_LoadFromBank`,
  `Synth_NoteOnPatch`, `Synth_PatchCount`, `Synth_VoiceFilterCutoff`),
  full-engine presets (`Synth_Preset_Save/Load` return 0/1 so demos can show
  a status; the `Synth_GetDefault*` getters read the state back for UI sync),
  wavetables (`Synth_WavetableGenerate/Load` — the file loader reads raw
  float32 tables, 1..2048 samples, and returns 0/1; `Synth_SetWavetableIndex`
  plus `Synth_WavetableCount/Size/Sample` for script-side previews), and
  state/scope read-outs (`Synth_IsAttached`, `Synth_ScopeCount/ScopeAt`,
  `Synth_VoiceActive`).
  The mixer attaches lazily on the first sounding voice and detaches after
  ~1 s of silence (kept while delay/arp/sequencer are enabled), so a silent
  engine costs no audio-thread work. Output is **muted by default**
  (`Synth_IsMuted`; `GX_SYNTH_MUTE=0` at launch or `Synth_SetMuted(false)`
  opens it, and the check gate runs muted): voices, arp/sequencer and the
  scope keep running, so the visualizers stay live while nothing is
  audible. `SYNTH_*` constants cover waveforms, filters, LFO targets, FM
  algorithms and arp modes.
- OPX (`tour_opx.cpp`): the 8-operator FM engine — `Opx_NoteOn(freq, vel,
  algorithm)` returns the voice slot (or use the `OPX_ALG_*` constants;
  `Opx_AlgorithmCount/Name` list the table), `Opx_NoteOff/AllNotesOff`,
  per-voice operator edits (`Opx_SetOperator/SetOperatorEnabled/
  SetOperatorADSR/SetOperatorDetune/SetOperatorFeedback`, `Opx_SetPan`) and
  the defaults twins (`Opx_SetDefaultOperator*` plus
  `Opx_SetDefaultAlgorithm`/`Opx_SetMasterVolume`), readbacks for UI sync
  (`Opx_GetDefaultAlgorithm`, `Opx_GetOperatorRatio/Level/Enabled`,
  `Opx_GetMasterVolume`), state (`Opx_VoiceActive`, `Opx_IsAttached`,
  `Opx_IsMuted`/`Opx_SetMuted` — muted by default,
  `GX_OPX_MUTE=0` opens) and the JSON preset round trip
  (`Opx_Preset_Save/Load` return 0/1; resource-tree paths are refused under
  the harness guard).
- PAD (`tour_pad.cpp`): the layered ensemble engine — `Pad_NoteOn(freq,
  vel)` returns the voice slot, `Pad_NoteOff/AllNotesOff`,
  `Pad_VoiceActive`; layers via `Pad_SetLayer(voice, layer, waveform,
  detune, level, pan, driftRate, driftDepth)` and `Pad_SetDefaultLayer`
  (`PAD_WAVE_SAW/TRI`, three layers), the long envelope
  (`Pad_SetADSR`/`Pad_SetDefaultADSR`), the moving filter
  (`Pad_SetFilter`/`Pad_SetDefaultFilter` with `PAD_FILTER_LP/HP/BP`,
  `Pad_SetFilterLFO`/`Pad_SetDefaultFilterLFO`), pan and the engine-level
  `Pad_SetChorus(enabled, rate, depth, mix, stereoPhase)` /
  `Pad_SetWidth`, `Pad_SetMasterVolume`, readbacks
  (`Pad_GetLayerWaveform/Level/Pan`, `Pad_GetChorusEnabled`, `Pad_GetWidth`,
  `Pad_GetMasterVolume`, `Pad_IsAttached`, `Pad_IsMuted`/`Pad_SetMuted` —
  muted by default, `GX_PAD_MUTE=0` opens) and the preset round trip
  (`Pad_Preset_Save/Load`). The demo `resources/script/audio/opx_pad_demo.as`
  plays a PAD chord progression under an OPX bell melody + bass (A S D B V X Z R pick
  the OPX algorithm, C chorus, W width, M sound); the scripting
  gate runs its self-test and an interactive boot smoke.
- TextWriter (`tour_textwriter.cpp`): the textwriter module
  (`renderer.textwriter.data`; it draws itself in the
  `SCENE_RENDERER_TEXTWRITER` scene and auto-loads
  `resources/text/textwriter.txt` on first update) via
  `TextWriter_LoadFile/LoadFileTXT/LoadFileYAML/UnloadFile/IsLoaded`, line
  navigation (`GetLine/GetLineCount/GetLineText/NextLine/JumpLine`), the
  reveal cursor (`SetReveal/GetReveal/SetRevealSpeed/GetRevealSpeed/
  IsRevealComplete`), fonts (`SetFont/ResetFont/HasFont`), colour
  (`SetColor`) and choices: a line carrying
  `<choice><option line='N'>text</option>...</choice>` entries exposes
  `HasChoices/GetChoiceCount/GetChoiceText`, and `SelectChoice(i)` jumps to
  option *i*'s target line and clears the list (options are collected at
  load/next/jump; scripts draw the option list themselves).
  `TextWriter_DrawTagged(text, x, y, size)` queues tagged strings that a
  shell draw hook replays after the GUI layer (topmost) (immediate draws
  during update would be erased by the renderer's canvas clear); colour/
  size/`<font path>` tags apply, while layout offsets, effects and matrices
  belong to the module's own draw path. `MeasureText`/`MeasureTagged`
  return widths and `ClearDraws/GetDrawCount` manage the overlay list. YAML
  loading needs `GX_USE_YAML`, which gX does not enable — the shipped demo
  is TXT. The choice collection completes the subsystem gameX left
  unfinished (the arrays existed there but nothing ever filled them).
- Overlay (`tour_overlay.cpp`): a paint list replayed from the draw hook
  (topmost). Ops persist across `Sleep`/`Yield` and draw in call order:
  `Overlay_Text`, `Overlay_Rect`, `Overlay_RectBorder`, `Overlay_Bar`
  (progress), `Overlay_Line`, `Overlay_Circle`, `Overlay_Triangle` — all use
  the colour set by `Overlay_SetColor` (the bar derives its bg/border from
  it). The replay flushes pending raylib-native ops before the batch
  primitives (line/circle/triangle), so mixing fills with shapes keeps the
  declared order. `Overlay_TextWidth` measures with the default font (rich
  text stays with `TextWriter_*`), `Overlay_Width/Height` return screen
  pixels, and `Overlay_Clear/Count` manage the list. This is the gX successor
  of gameX's demo paint list (`PaintClear/DrawRect/DrawText/SetColorRGB`).
- Raymarch (`tour_raymarch.cpp`): the CPU raymarcher
  (`renderer.raymarch.data`; `--raymarch` scene) — terrain/water/fog/sun
  (gameX's `RmSet*` contract; byte colours 0..255), march parameters
  (bounces/steps/near/far/epsilon/stepMax plus gX's stepsPerBounce/
  distTotal/renderScale), the full flag set (terrain/water/fog/sky/AO/
  objects/lights plus glow/shadow/proxy/volumetric/sample debug), SDF
  objects (`SetObjectPos/Color/Type/Params/Scale/Rotate/Amount`,
  `RemoveObject`; set `SetObjectAmount` before `SetObjectType` — the safe
  setter needs the slot live) and lights, ambient/null colours, the internal
  time/camera switches, and presets (`LoadDefault/LoadRandom/LoadBuiltIn`
  with `RM_PRESET_DEFAULT/ISLAND/WATER/FLATLANDS/DESERT/ALIEN`,
  `LoadPreset/SavePreset`). The SDF graph and the clouds/volFog parameter
  blocks are not bound — presets carry those looks.
- PostProcess (`tour_pp.cpp`): the embedded chain (`renderer.pp`) —
  `PostProcess_SetEnabled/IsEnabled`, `PostProcess_Stop` (disable + clear
  chain), built-in presets (`PP_PRESET_*`, incl. the battle transitions),
  the chain editor (`PushShader/PopShader/ClearShaders/RemoveShader/
  MoveShader/ReloadShader/GetShaderCount`, with `PP_<TYPE>` constants for
  every chain shader), composite flags (`SetFeedback/SetInject/
  SetInjectCanvas`), feedback decay/blend (`PP_BLEND_*` modes), inject slots
  (`SetInjectAmount`, `SetInjectTexture`/`ClearInjectTexture` — resource
  paths, accepted as `resources/texture/...` or the resource-root subpath)
  and the canvas blend/tint. Enabling from the very first script frame races
  the initial scene response (the scene config re-asserts `usePP`); start
  with a `Sleep` or enable from a later frame, as tours do.
- The playable instrument built on this surface: `resources/script/synth/
  synth.as` (run with `--script`; the boot scan is flat, so it stays inert
  otherwise) — a two-octave on-screen keyboard plus envelope/filter/LFO/FM/
  delay/arp/volume controls, played with the mouse (click/drag, glissando)
  or the physical row `Z S X D C V G B H N J M` / `Q 2 W 3 E R 5 T 6 Y 7 U`
  (`,`/`.` shift the octave); SOUND toggles the default mute, and the scope
  visuals stay live behind the panel on the synthvis scene. A wavetable strip
  cycles slot 0 through four generated shapes and three shipped additive
  tables (`resources/audio/wavetable/*.f32`, regenerate with
  `scripts/gen_wavetables.py`), previews the loaded table via
  `Synth_WavetableSample` and switches the oscillator to WAVETABLE; SAVE/
  LOAD PRESET write the full engine preset to `save/synth_demo_preset.json`
  (the gate seeds `ScriptSetString("synth_preset_path", ...)`), and SAVE/ARM
  PATCH capture the current defaults into the in-memory bank (ARM routes new
  notes through `Synth_NoteOnPatch`). The gate runs its self-test (state key
  `synth_selftest`, including the wavetable/patch/preset round trips) and an
  interactive boot smoke.
- Host infrastructure: `gX_Scripting_RegisterUpdateHook` (after scripts each
  frame) and `gX_Scripting_RegisterDrawHook` (draw phase, after the GUI —
  topmost; used by the textwriter tagged draws and the overlay paint list).

## Console front-end

Port of gameX's `common/console.c` + ImGui panel, distilled (demoscript and
stringtable couplings dropped, `/scripts` and `/state` added):

- Built-ins: `/clear /cls /help /? /history /scene /scenes /msg /echo /time
  /stats /ls /reload /exec /run /scripts /state [save|load|clear]`.
- Anything else runs as AngelScript wrapped in `void main() { ... }` — a
  missing trailing `;` is appended for REPL ergonomics. Each one-liner is a
  throwaway module that is discarded after execution (gameX leaked them).
- Script errors and compile failures are reported in the console log and are
  not fatal.
- Surfaces: `--console` at load, F10 toggle at runtime, `GX_CONSOLE_EXEC`
  (newline-separated commands executed once at first update) for headless
  gates. The panel draws independently of the exclusive tool set.
- `/state save|load|clear` is the manual twin of the automatic state
  persistence; `/scripts` shows pause state next to the debugger API.

## Registered API (shell subset)

- math: `Sin Cos Tan Sqrt Abs Min Max Clamp EaseBezier EasePow`
- shell: `Print Yield Sleep`, `GetTotalTime GetFrameCount`,
  `AssertTrue AssertEqual AssertFloat Fail FailCount Quit`
- input: `IsKeyPressed IsKeyDown IsKeyReleased`, `Mouse_GetX/Y/Scroll/
  DeltaX/DeltaY`, `Mouse_IsPressed/IsDown`, `KEY_*` constants
- virtual input: `GUIClick GUIClickElement GUIKey GUIDeactivateVirtualInput`
- GUI/DOM: `GUIGetCanvasCount GUIGetCanvasElementCount GUIFindElement`,
  `GUIIsHovered/Clicked/Focused/Disabled`, `GUIGetElementName/Text/X/Y/W/H`,
  `GUIFocusElement`, `GUIAssertTextContains`
- view/scene: `SetRenderScale TakeScreenshot SwitchScene`, `SCENE_*`
- types: `Vec3` (value type with operators, `Vec3_Create`)
- hooks: `Hook`, `TriggerHook`
- script list + debugger: `ScriptCount`, `ScriptGetPath`, `ScriptGetName`,
  `DebugToggleBreakpoint`, `DebugClearBreakpoints`, `DebugHasBreakpoint`,
  `DebugContinue`, `DebugStep`, `DebugStepOver`, `DebugStepOut`,
  `DebugIsPaused`, `DebugGetLine`
- state: `ScriptSetInt ScriptGetInt ScriptSetFloat ScriptGetFloat
  ScriptSetString ScriptGetString ScriptHas ScriptClearState
  ScriptSaveState ScriptLoadState`

## Build note

The gX `extern "C"` include block in `scripting.h` must not pull raylib
headers through C++ (`raymath` operator overloads give a C-linkage clash);
`api_core.cpp` declares the one wrapper symbol it needs instead of including
`wrappers/wrappers.h` (same trap as `src/tools/gfx/gfx_editor.cpp` documents).

## Gate

`make check-scripting` builds `build/scripting` and runs
`scripts/scripting_check.py`: assert pack, failure exit code, cross-frame
sleep, scene hooks order, hot-reload resume, boot + mod scans, DOM query +
click through a real markup onclick, breakpoints/continue, state persistence
across processes, the debugger panel boot, console batch/one-liner/panel
behavior, the tour APIs (camera rail, gfx flags/settings, models/lights,
audio, the background param surface incl. presets/transition, the synth
surface incl. note/arp/sequencer/patch/preset, the textwriter file/reveal/
font surface incl. tagged draws, the overlay paint list, raymarch params/
objects/presets, postprocess chain/feedback/inject), and markup companion
lazy-load.

## Not in this slice

The tour API surface is complete (camera/gfx/audio/background/synth/
textwriter/overlay/raymarch/pp — see the Tour APIs section). Demoscript
`.gxdemo` remains a separate, still-dropped subsystem.
