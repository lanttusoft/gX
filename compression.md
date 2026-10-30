# gX Compression Log

Compression loop (pivot §9): recurring Reuse/Strip passes, Lean-gated.
Each pass records before/after here. Gate metrics: **Release binary size + max-bss**.

## Pass 1 (2026-08-15) — Static footprint + dead resources

### Baseline (before)

| Metric | Debug | Release |
|---|---|---|
| Binary | 10,010,616 B | 4,030,712 B |
| text | 4,245,541 | 3,572,239 |
| data | 89,632 | 80,176 |
| **bss (max RAM)** | **126,312,520 (120.5 MB)** | **126,312,568 (120.5 MB)** |
| src | 5.5 MB / 218 .c / 82K lines | |
| resources | 4.0 MB (shader 592K, model 1.3M, audio 304K, font 96K, texture 1.7M) | |

bss top symbols: `g_synth` 110.6 MB (recorder buffer 115 MB static — never called),
`settings` 2.5 MB, `schemaJSON_GUI` 2.5 MB, `s_styleSchema` 2.5 MB, `g_cssSheets` 0.8 MB,
`g_reverbState` 0.4 MB.

### Pass 1 actions

- [x] P1: remove synth recorder (dead API, −115 MB bss)
- [x] P1: schema blobs pointer-ized + lazy arenas (−7.4 MB bss)
- [x] P2: strip unreferenced shaders (−16, keep vhs_* trio — #include'd)
- [x] P2: strip unloaded models (−5, incl. broken shuttle.glb ref)
- [x] P2: loaddefaults.c pruned (−30 broken refs, silent 404s at boot)
- [x] P0: port codebase_map.py + dead_symbols.py + lean_gate.sh
- [x] Lean gate: `make check` runs size+bss comparator
- [x] P3: strip yaml/toml/xml marshal schemas (−4,692 lines, JSON-only dispatch)
- [x] P3: reverb delay line heap-lazy (−384 KB bss, API preserved)
- [x] P4: source strip — dead renderer files (globe, demoscript, bg_video,
      rhi_null, postprocess/schema2) −4,046 lines
- [x] P5: backend guard strip — single-backend raylib, 50 files, −5,298 lines
- [x] P6: dedupe + dead-state strip (cgltf.h −7,240, imguiState −215, editor/schema stubs −43)
- [x] P7: json-c unused-file trim (version/visit/libjson/iterator −712)

### Results (after P1+P2+P3+P4+P5+P6+P7)

| Metric | Release before | Release after | Δ |
|---|---|---|---|
| Binary | 4,030,712 B | 4,008,736 B | −21,976 B (−0.5%) |
| text | 3,572,239 | 3,552,488 | −19,751 B |
| bss | 126,312,568 | 2,863,736 | **−123.4 MB (−97.7%)** |
| resources | 4.0 MB | 3.8 MB | −0.2 MB |
| shaders | 61 + common/ | 46 + common/ | −16 files |
| models | 16 | 11 | −5 files |
| marshal sources | 8 | 4 | −4 files (−4,692 lines) |
| src tree | 113,210 | 95,643 | **−17,567 lines (−15.5%)** |

### Source tree (P4 detail)

- Deleted not-compiled dead files (zero build impact — never in CMakeLists):
  globe.c/.h (2,168 — dropped subsystem, weather dep), demoscript.c/.h
  (1,067 — AngelScript demos), background/bg_video.c/.h (342 — FFmpeg
  video), rhi/rhi_null.c (450 — null backend), postprocess/schema2.c (19 —
  stub). All headers only self-referenced (verified).
- Scene enum entries (RENDERER_GLOBE/DEMOSCRIPT/VIDEO) kept — constants
  used by scene.c switch; removing would shift masterIndex indices.
- KEPT: raymarch (compiled, M5-verified, user decision), AI probe
  (gfx/ai.c + ai_script.c — renderer-coupled blind-AI interface, stays
  uncompiled behind gX_ENABLE_GFXAI, relocation to common/ rejected:
  dependency order violation), reusable infra (threading/hashmap/
  json_pointer/json_patch — potential future use).
- Backend guard strip (674 GAME_BACKEND_* refs) deferred → P5.

### Backend guard strip (P5 detail)

- `scripts/strip_backends.py` — block-aware preprocessor pruner: evaluates
  each guard under gX's defines (RAYLIB/GX = true, GLFW/LIBDRAGON/PS1/PS2/
  SDL/VULKAN/NULL/N64 = false), removes dead branches, unwraps always-true
  guards. Unknown conditions pass through verbatim (structure preserved).
- 50 files, −5,298 lines. 0 non-raylib backend refs remain (only the
  config.h comment mentions GLFW).
- Guard forms handled: #if defined() single + || chains, #ifdef/#ifndef,
  #elif chains, #else fallbacks, !defined() forms, multi-line continuations.
- `config.h` auto-define of GAME_BACKEND_GX removed (it existed to bridge
  RAYLIB/GLFW detection) — CMake now defines both RAYLIB + GX explicitly.
- Biggest cuts: raylib_wrappers_load.c −715, image.c −611, model.c −471,
  texture.c −367, text.c −345, control.c −293, query.c −272, input.c −243,
  drawing.c −217, window.c −218, wrappers_gl.c −205 (GLFW wrapper layer).
- Binary/bss unchanged (dead branches never compiled); tests 0 errors,
  12/12 scenes clean, lean gate PASS.

P1 detail:
- Recorder removed: `gX_Synth_Record_Start/Stop/IsRecording` + `gX_Synth_Recorder`
  struct + `GX_SYNTH_RECORD_MAX_SAMPLES` (5-min stereo buffer = 115 MB static,
  zero callers). synth.c −90 lines, synth.h −18.
- Schema arenas: `gX_JSON_Schema.objects[]` (fixed 8192 = 2.5 MB each) →
  heap pointer via `gX_JSON_Schema_EnsureArena`. settings 64, style 128,
  GUI 8192 lazy (never allocated in gX runtime — marshal path has no callers).
  The ASan note in json_struct.h predated the bounds guard (json.h:60-67);
  valgrind clean on the load path.
- Verified: settings.json load/save + theme.json (style schema) parse correctly.

P2 detail:
- Shader strip was basename-grep based — caught a false positive (vhs_format/
  machine/tape are #include'd by vhs.frag, restored) via include-graph recheck.
- All remaining shader #include targets (common/*.glsl, fractal_math.glsl,
  vhs_*) verified present.
- models: cone10/cylinder_old/pyramid/tetrahedron unloaded; misc/shuttle.glb
  was a broken ref (loaded as "shuttle.glb", file at misc/ — never found).
- loaddefaults.c: dropped 6 sounds + 4 music + 19 textures + blur.frag +
  shuttle.glb (all silent boot 404s). RM loads on demand via gX_Resource_Load*;
  GUI builder heap lookups still hit preloaded entries (fonts/models/shaders).

P0 detail:
- scripts/lean_gate.sh — Release binary+bss vs .lean_baseline, wired into
  `make check` (binary fails above +3% or +4 KB; BSS remains capped at +1%
  or +4 KB).
- scripts/codebase_map.py — include-graph (gX subsystems, circulars flagged).
- scripts/dead_symbols.py — nm-based unused-symbol hints (114 candidates,
  mostly public GUI API — kept per API-surface-complete pillar).

P3 detail:
- textwriter verified LIVE (scene dispatch, --textwriter, input, compositor
  draw) — kept. Its YAML loader was already a stub on desktop (GX_USE_YAML
  never defined) — stripped the dangling gX_YAML_* helpers + yaml.h include.
- schema_yaml.c + yaml.c: compiled to 4 stub functions (GX_USE_YAML off) —
  removed with the -lyaml link dep.
- schema_toml.c (1,345 lines) + schema_xml.c (1,338 lines): fully compiled,
  real code, reachable only via gX_Marshal_Load extension dispatch — whose
  sole caller is markup.c (itself dead: zero callers). settings.c uses
  gX_JSON_Load directly (JSON-only). All removed.
- gX_Marshal_Save/Load now JSON-only (unsupported-format error for others).
- Reverb: g_reverbState.buf 384 KB static → calloc on first enable in
  gX_Audio_Reverb_Set (public API preserved, gX_Audio_Reverb_Init no longer
  allocates, gX_Audio_Unload frees). No callers exist in gX, so zero
  allocation in practice.
- dead_symbols sweep ≥200 B: all candidates are dispatch-table entries or
  public API (gX_Synth_Preset_Load, gX_F32v3_Unproject, renderer *_Update)
  — nothing internal left to strip safely.
- .lean_baseline updated to 4,008,736 / 2,863,736.

### Next pass candidates

- markup.c (GUI markup template — live via guitester scene dispatch but
  demo.json missing; verify template path before touching).
- g_cssSheets/g_cssDefaultSheet capacities (CSS live — reduce only).
- vendored json-c-master (~10 files): full JSON implementation vs gX's
  gX_JSON needs — distilling to the used subset is the last big text win.
- (done in P5)

### P6 detail — dedupe + dead-state strip (−7,513 lines)

- **cgltf.h dedupe (−7,240)**: `src/external/cgltf.h` was a byte-identical
  copy (md5 `5c6cc87…`) of raylib's bundled `external/raylib/src/external/
  cgltf.h`. Deleted the vendored copy; `glb_loader.c` now includes raylib's
  (3-level relative path). Zero behavior change — verified by checksum,
  model draws confirmed in --gfx smoke.
- **imguiState strip (−215)**: GX_GUI_USE_IMGUI is undefined in gX — all
  imgui calls compiled out, leaving dead state. Removed the 82-line
  `gX_GUI_ImguiState` struct (gui.h), init block + unload-if + two
  `#if`-guarded Draw_ImGui blocks (gui.c), Load_ImGui block (update.c),
  `GX_GUI_IMGUI_FLAG_*` enum + sceneFlags table + SetImguiFlag (respond.c,
  41 lines), toolbar/console key toggles (input.c), and two
  `hitsWindow` checks (update.c — always-false with imgui compiled out).
- **editor.c/.h stubs (−31)**: empty Update/Draw/Unload stubs, zero callers
  (editor.h only self-referenced; scene enum entry kept — shifting
  masterIndex is a separate risk). Removed from CMake.
- **common/schema.c placeholder (−12)**: empty file (schema.h is the real
  content and stays — used by postprocess/gfx schema.c).
- Verified: Debug build clean, tests 0 errors, --gfx model draws OK,
  --title/--menu smoke clean. Binary/bss unchanged (all removed code was
  never compiled or dead state).

### P7 detail — json-c unused-file trim (−712 lines)

- Deleted vendored json-c files with zero external AND zero internal refs:
  `json_c_version.c/.h` (19+55), `json_visit.c/.h` (128+101),
  `json_object_iterator.c/.h` (153+228), `libjson.c` (26).
- `json.h` (json-c master header) dropped the two `#include` lines for
  json_c_version.h + json_object_iterator.h — no types/functions from them
  used elsewhere (verified grep).
- KEPT: debug.c (96 — #included by json_object/tokener/util), json_pointer.c
  + json_patch.c (user P4 keep), random_seed.c (tokener dep).
- Verified: tests 0 errors, settings.json + theme.json parse correctly
  (json_object_from_file/to_file_ext path intact).

### P8 detail — custom JSON parser swap (−8,389 lines)

- **Replaced vendored json-c-master (9,389 lines / 11 files) with a custom
  drop-in parser** implementing the exact 22-function json-c subset gX uses.
  New `src/common/marshal/json_impl.c` (~1,000 lines) + `json_impl.h`.
- API surface preserved 1:1: `json_object_*` getters/setters, `json_object_
  new_*`, `json_object_object_add/get/foreach`, `json_object_array_*`,
  `json_object_from_file/to_file_ext`, `json_tokener_parse`, `json_object_
  get_string`, `json_object_put` (refcount), `json_object_to_json_string_ext`
  (pretty/compact), `json_object_new_double_s` (locale-safe), `json_object_
  get_double` (strtod), `json_object_object_foreach` macro, `json_type_*`
  enum + `json_type_to_name`.
- `json.h`/`json_struct.h` and the four schema consumers
  (`schema_json.c`, `fractal_preset.c`, `postprocess/schema.c`,
  `gfx/schema.c`) now include `json_impl.h`; CMake swaps the 11 json-c
  sources for `json_impl.c`.
- **Bug fixed during bring-up**: `parse_value` returns NULL for JSON
  `null`, which `parse_object`/`parse_array` treated as a parse error.
  Fixed by consuming the `null` token explicitly before `parse_value()` in
  both — verified with `{"f":{"g":null}}` and `[1,null,3]` round-trips.
- Verified: Debug build clean, tests 0 errors (8 new JSON test sections in
  main_tests.c), settings.json + theme.json parse correctly, valgrind clean
  (0 leaks; 2 NVIDIA-driver dlopen false-positives), --gfx/--title/--menu
  smoke clean.
- **Lean gate**: binary 4,008,736 → 3,979,576 (−29,160 B), bss 2,863,736 →
  2,863,344 (−392 B). PASS.
- **Infra fix**: `make lean` built into `build/` then read the stale
  `build-release/gX_build` (always PASS trivially). Now cmake-builds
  `build-release/` directly before gating.

## Compression loop — STOP (2026-08-15)

Decision: **stop compression at P8.** Current src tree 87,210 lines
(−23.0% from 113,210), Release binary 3,979,576 B, bss 2,863,344 B.
Lean gate PASS.

Remaining gap to the 85K budget is 2,210 lines (2.6%) — deliberately
kept, not stripped:

- **rhi (4,372)** — the contract's §4 disposition is "drop", but it is a
  live layer with 1,070 call sites across 25+ renderer files. It exists to
  enforce the §6 "100% boundary enforcement" pillar (no raylib outside
  wrappers/backend). Dropping it means renderers go direct rlGL (pillar
  violation) or wrappers grow a raw-GL surface first — a real refactor, not
  a strip. Deferred 3×; revisit as a dedicated session if the budget
  becomes binding.
- **gui/markup (528)** — dead in gX (no resources/markup/ assets, no scene
  loader, no tests) but kept as public API surface + hot-reload hook.
- **AI probe (3,005)** — user keep.
- **rhi aux files (rhi_validate 209, rhi_bindless 187, rhi_event_log 114,
  rhi_mesh 124)** — internal debug instrumentation; strippable in a future
  P9 if desired, but not needed to close the budget.

Rationale: 2.6% over a soft target is within tolerance; the remaining
candidates are either the boundary-discipline layer (kept by design) or
small debug instrumentation. Compression loop resumes only if the budget
becomes binding or a new dead subsystem appears.
