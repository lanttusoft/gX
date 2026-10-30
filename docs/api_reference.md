# gX — API Reference

<!-- //ai: opt-in Debug diagnostic; not an engine precision guarantee. -->
`gX_RHI_Depth_Probe` queries live depth allocations and measures isolated
format-matched raster-write, shader-sample and MSAA depth-resolve fixtures.
It is excluded from lean Release and does not alter production texture contents.

Naming conventions: `gX_Struct_SubStruct_Method()`. Structs carry
`name[128]` + `description[256]` first. No `stdbool` — use `gX_I1`.
Types: `gX_I8..gX_I64`, `gX_U8..gX_U64`, `gX_F32`, `gX_F64`.

## Definition audit

`make api-definition-check` compares the `gX_*` declarations in `src/**/*.h`
with `T`/`W` symbols in every normal-build object under
`build/CMakeFiles/gX_build.dir` (linker-dropped declarations remain visible).
`scripts/api_definition_check.py` holds the checked baseline of known
default-engine declarations with no definition; the default gate fails only on
new names, while `--strict` asks for the full known list to be clean.
Module-only `gx_shell` families (`gX_Console_*`, `gX_Script*`,
`gX_Scripting_*`, `gX_Tour*`) are excluded from the default-engine list. The
dated audit report is retained in the release audit record; it is not part of
this source beta.

## gxmath (pure C, no deps)

- `gX_Math_*` — constants (PI, DEG2RAD, ...), random, clamp
- `gX_Random32_*` — deterministic PCG32 stream (Seed, Next, Bounded, F32,
  Range); per-consumer streams with platform-independent sequences
- `gX_F32_Convert_*` — unit/color/coordinate/astronomy conversions plus
  sRGB↔linear (exact IEC 61966-2-1), amplitude↔dB, MIDI note↔frequency and
  Celsius↔Fahrenheit
- `gX_F32v2/v3/v4` — vector ops (Add, Sub, Scale, Dot, Cross, Normalize,
  Lerp) via vector_macros instantiations
- `gX_Matrix_*` — multiply (row-major A×B), translate/scale/rotate,
  perspective/ortho, inverse
- `gX_Quaternion_*` — ops, slerp, to/from matrix
- `gX_Easing_*` (`gX_Easing_Apply(ease, t)`) — easing curves
- `gX_SDF_*` — signed distance field helpers
- `gX_Math_Calculus_*` — derivatives/integrals
- `gX_F32_*` — float helpers (clamp/lerp/random/root/rsqrt/equals) — `gX_F64_*`
- `gX_I32_*` / `gX_I64_*`, `gX_I32v2_*`, `gX_I32v3_*`, `gX_I32v4_*` — int +
  int-vector ops
- `gX_F32v2_*`, `gX_F32v3_*`, `gX_F32v4_*`, `gX_F64v4_*`, `gX_F32m2x2_*`,
  `gX_F32m3x3_*`, `gX_F32m4x4_*`, `gX_F32Qua_*` — instantiated
  vector/matrix/quaternion ops (the vector_macros instantiations)
- `gX_Color_*` — color math (Mul, Lerp, HSV/RGB)
- `gX_FormatCast_*` — numeric format/cast helpers
- `gX_FrameTime_*` — per-frame timing
- `gX_F32*_Physics_*` — SI physics formulas (Newton, energies, Lorentz
  contraction, Planck/Rayleigh-Jeans blackbody) plus the EM laws added for
  the sandbox: Coulomb force/potential, point-charge electric field, Lorentz
  force, cyclotron radius, photon energy, the magnetic-dipole set (field
  vector, torque, dipole-dipole force/potential and the on-axis/equatorial/
  coaxial scalars), the special-relativity set (beta, gamma, contracted
  length, dilated time, Doppler factor, velocity addition with a caller
  light speed, rapidity — `gX_F32_Physics_Relativity*`), and the thermal set
  (normalized
  blackbody RGB, Stefan-Boltzmann power, Wien peak); constants in
  `gxmath/config.h` (c, h/ħ, k_B, e, N_A, ε₀, k_e, μ₀, μ₀/4π, σ, Wien b,
  the relativity beta clamp)
- Shape/geometry helpers: `gX_Bezier_*`, `gX_Circle_*`, `gX_Circle3D_*`,
  `gX_Cube_*`, `gX_Capsule_*`, `gX_Cylinder_*`, `gX_Ellipse_*`, `gX_Grid_*`,
  `gX_Line_*`, `gX_Line3D_*`, `gX_Plane_*`, `gX_Point_*`, `gX_Polygon_*`,
  `gX_Ring_*`, `gX_Sphere_*`, `gX_Triangle_*`, `gX_Triangle3D_*`,
  `gX_BoundingBox_*`, `gX_Ray_*`, `gX_RayCollision_*`

## common

- `gX_Assert_*` (`GX_ASSERT`) — development-invariant asserts with
  expression/file/line/function context and an optional fatal abort;
  boundary failures stay on `GX_ERROR_SET`

- `gX_Heap_*` — dynamic array (Init/Add/SeekPtr_Index/Remove/Foreach),
  `GX_HEAP_FOREACH` macro.
  <!-- //ai: allocation failure contract from native rollback regressions. -->
  checked transactional resizing; failed growth preserves old storage/count and
  add/seek returns NULL. Successful resize invalidates pointers and may hold old
  plus new buffers temporarily.
- `gX_TraceLog(level, fmt, ...)` — logging with module macros
  (gX_TraceLog_GFX/GUI/MAIN/RENDER...)
- `gX_LogState.globalLevel` — log threshold
- `gX_Text_Format/Replace/...` — string utils
- `gX_JSON_Schema*` / `gX_JSON_Load/Save` / `gX_JSON_*` / `gX_Marshal_*` —
  marshal (custom json_impl, drop-in json-c subset).
  <!-- //ai: file input limits do not constrain programmatically built objects. -->
  File parsing is limited to 16 MiB and 64 nested containers; non-finite numeric
  overflow, malformed numbers and trailing data reject the document.
- `gX_Enum*` — X-macro enums (scenes, datatypes, keys, binds, texture
  formats) with `gX_*_String()`
- `gX_Shader_Compiler_*` — GLSL preprocessor/compiler (hot-reload, ES/VK
  dialect transforms)
- `gX_Path/GX_PATH(type, name)` — resource path resolution
- `gX_Profiler` / `GX_PROFILER_CHECK` — frame profiling
- `gX_Polisher_*` — runtime tuning of registered magic-number defaults
  (RegisterFloat/RegisterInt/Find/ResetAll/GenerateReport/ApplyEnv; the
  `GX_POLISHER="NAME=VALUE,..."` scripted hook). See the background
  module's preset/polisher documentation in its header.
- `gX_Hardware_Tier_*` / `gX_Hardware_Report` — hardware tier classifier
  (below-minimum/minimum/standard/recommended) over the RHI caps and the
  `--hw-report` capability report; see `docs/hardware.md`
- `gX_Heap (alloc)` / `gX_Random_*` / `gX_HashMap_*` / `gX_Pool_*` /
  `gX_Alloc_*` — allocation helpers
- `gX_Thread_*` / `gX_Mutex_*` / `gX_WorkQueue_*` / `gX_ThreadPool_*` —
  threading
- `gX_Msg_*` — message/notification queue; `gX_Schema_*` — schema helpers
- `gX_SchemaField` / `GX_SCHEMA_*` — offset-based JSON descriptors. Partial
  objects retain initialized values; missing keys warn by default, required
  fields can report errors without aborting the load, and sparse fields can
  suppress omission diagnostics (`gX_SchemaMissingSeverity`). Use
  `gX_Schema_JSON_LoadFromObjPath` to prefix diagnostics for nested/external
  structs.
- `gX_String_*`, `gX_Log_*` (log config) / `gX_TraceLog_*` (module log
  macros: gX_TraceLog_GFX/GUI/MAIN/...), `gX_LoadingState_*`,
  `gX_Datatype_*`, `gX_Raw_*`, `gX_Profiler_*` (frame phases/zones; see
  `docs/debugging.md`), `gX_Error_*` (boundary codes + last-error slot; see
  `docs/error_handling.md`), `gX_Debug_*` (crash traces + variable watches;
  see `docs/debugging.md`), `gX_Path_*`
  (search-dir resolution; guest-root fallback via `gX_Path_GuestFallback`,
  see `docs/guest_assets.md`),
  `gX_PathGen_*`, `gX_PointerCast_*`, `gX_PointerReceive_*`,
  `gX_RMHeap_*` (load-or-404), `gX_Free_*`

## wrappers (backend-agnostic; only layer touching raylib)

- `gX_Texture_*` — Load, Draw (center-origin params: src/dst/origin/offset/
  scale/tint; backend auto-flips RT sources), IsRT, RT load
- `gX_Shader_*` — Load, LoadFromMemory, Load_Geo, SetValue_* (Int/Float/
  V2/V3/V4/Matrix/TextureSlot), GetLocation, Unload; hot-reload via
  `gX_Shader_Compiler_CheckReload`
- `gX_Model_*` — Load, AsyncLoad (Start/IsDone/Finish), Draw, Mesh access,
  Unload; `gX_Mesh_Gen*` (plane/cube/sphere...)
- `gX_Font_*` — GetDefault, Load, Measure (gX_RawString_Measure)
- `gX_Sound_*` / `gX_Music_*` — Play/Stop/SetVolume/SetPan, GetLength
- `gX_Window_*` — Init, ShouldClose, resize, fullscreen
- `gX_Input_*` — keyboard/mouse/gamepad state, `gX_Bind_*` bind system
- `gX_Camera_*` (wrappers) — conversion helpers
- `gX_Drawing_*` — Begin/End, ClearBG, BeginTexture, blend modes,
  scissor, viewport
- `gX_Rectangle_Draw_Parameters` — colored/gradient rects
- `gX_RawString_Draw` — text rendering
- `gX_Image_*`, `gX_File_*` / `gX_FileData_*`, `gX_Physics_*` (ODE),
  `gX_PhysicsBody_*` (incl. enable/auto-disable sleeping controls and
  mass/center-of-mass controls),
  `gX_PhysicsWorld_*`, `gX_PhysicsCollider_*`,
  `gX_PhysicsJoint_*` (ball/hinge/slider/universal/fixed/hinge2/AMotor/piston —
  hinge2 adds a suspension plus steering/spin motors and binds a NULL
  partner to a hidden kinematic anchor; the AMotor adds per-axis angular
  limits and motors, used for the ragdoll swing bands; the piston adds a
  slide with a free rotation about its axis; per-joint feedback
  reports the constraint force/torque for breaking), `gX_PhysicsMaterial_*`
  (friction, restitution,
  rolling/spin resistance and belt surface velocity), `gX_Ragdoll_*`,
  `gX_PhysicsEM_*` (charges + magnetic dipoles: `SetDipole`/`GetDipole`, and
  induced magnetism: `SetSusceptibility`/`GetSusceptibility`),
  `gX_PhysicsRelativity_*` (the tunable light-speed layer: a per-step
  speed clamp plus the gamma mass-dilation trick; `SetSettings`
  lightSpeed/clampMargin, `AddBody`/`RemoveBody` with rest masses,
  `GetBodyState` for the readout),
  `gX_PhysicsThermal_*`, `gX_PhysicsFluid_*` (still-water
  region: buoyancy/drag + surface entry events),
  `gX_Physics_ApplyTuning` (quality pin for the editor),
  `gX_Element_*` / `gX_Elements_*`
  (element table: densities/molar masses/thermal data, appearance and
  magnetic susceptibility), `gX_Collision_*`
- `gX_Backend_*` — backend lifecycle (Load/Unload/RerouteLogtype)
- `gX_Batch_*` — draw-batch control; `gX_Billboard_*`,
  `gX_DrawBillboard_*`; `gX_Cubemap_*`; `gX_Bindless_*`
  (RHI bindless handles)
- `gX_AudioStream_*`; `gX_Wave_*` — raw audio buffers
- `gX_Mesh_*` / `gX_MeshPtr_*` / `gX_ModelSkeleton_*`; `gX_Material_*` /
  `gX_MaterialMap_*`; `gX_RenderTexture_*`
- `gX_Skin_*` — shared per-draw bone-matrix upload cap
  (`gX_SKIN_MAX_BONES`) and `gX_Skin_UploadBoneCount` clamp
- Input/window: `gX_Key_*`, `gX_Mouse_*`, `gX_Gamepad_*`, `gX_Cursor_*`,
  `gX_Monitor_*`, `gX_Clipboard_*`, `gX_ExitKey_*`, `gX_TargetFPS_*`,
  `gX_MasterVolume_*`, `gX_Time_*`, `gX_URL_*`; `gX_Camera2D_*`,
  `gX_Camera3D_*` — camera conversion helpers
- `gX_Mouse_GetRay` takes coordinates in logical game-screen pixels
  (`maindata.screenWidth × maindata.screenHeight`), after viewport mapping.
- Text: `gX_RawString_*`, `gX_Rectangle_*`, `gX_Text_*`, `gX_Text3D_*`,
  `gX_TextGetter_*`, `gX_TextHandler_*`
- `gX_LoadTextureFromImage_*`, `gX_UpdateTexture_*`,
  `gX_UpdateTextureRec_*` — texture upload helpers
- `gX_GetScreenWidth_*`/`gX_GetScreenHeight_*`,
  `gX_GetRenderWidth_*`/`gX_GetRenderHeight_*`, `gX_GetCharPressed_*`,
  `gX_TakeScreenShot_*` — window/screen helpers

## rhi (GL surface, raylib's glad)

- `gX_RHI_Tex_*` — create/bind/param/mipmap/delete cubemaps+2D
- `gX_RHI_BufferAudit_*` — opt-in render-target bind, clear, blit, draw,
  texture-bind, explicit full-write, feedback, and per-pass lifecycle tracing
  (`GX_GFX_BUFFER_AUDIT`)
- `gX_RHI_FBO_*` — FBO create/bind/attach/blit (deferred deletes)
- `gX_RHI_Buf_*` — VBO/UBO create/upload
- `gX_RHI_State_*` / `gl_state` — enable/disable/blend/depth/cull
- `gX_RHI_Caps_*` — capability queries
- `gX_RHI_Shader_SetLoading` / `gX_RHI_Shader_HasLoading` — scoped compile
  presentation callback; KHR/ARB completion polling keeps GL ownership on the
  context thread, callback zero cancels. Used by raymarch's RGB-gas loader.
- `gX_RHI_GetProcAddress` — extension lookup through the registered backend resolver.
- `gX_RHI_Matrix_*` — projection/modelview stack
- `gX_RHI_Debug_Label` — object labels (GL 4.3)
- `gX_Batch_Flush` — force draw batch flush
- `gX_RHI_Bindless_*` — bindless texture handles
- `gX_RHI_Mesh_Upload` — raw mesh VAO upload
- `gX_GL_Detect` — GL version/feature detection
- `gX_GL_*` — raw GL layer (gl_detect/gl_state/gl_draw + shader-compiler
  raw compile paths, error capture)
- `gX_RenderDoc_*` — in-app RenderDoc capture trigger (`Init`/`Trigger`;
  active only when the process runs under RenderDoc injection, no-ops
  otherwise)

## renderer

<!-- //ai: alpha routing and emission resolution are independent of the rich-material cache. -->
GLB loading retains compact full-source alpha mode/cutoff/opacity arrays.
`gX_RendererGFX_GLB_Free` releases these with model metadata. MASK uploads
reset per primitive; authored model alpha-test settings retain precedence.
BLEND routing includes materials beyond the rich metadata cache. Cached
legacy water/transmission opacity remains unchanged. Uncached file emission
uses native material RGB; uncached emissive-strength extensions remain limited.

<!-- //ai: independent opt-in visible cubemap background. -->
Sky parameter `visibleImageSky` defaults false. With `ENABLE_SKYBOX` and a
loaded `skyboxPath`, it draws that cubemap as the visible scene background.
Missing images fall back to the legacy procedural/clear background. It does
not select the source for dynamic reflection captures or procedural sky LUTs;
existing IBL configuration remains independent. Orientation and color follow
the existing cubemap loader, without additional shader gamma conversion.
<!-- //ai: distinguish Lean routing guarantees from GPU and loader obligations. -->
`GxProofs.VisibleImageSky` models default-OFF selection, unavailable-image
fallback, shader readiness, and capture/LUT selector reset. These proofs do
not establish C translation equivalence, successful loading, orientation,
color conversion, or driver behavior; those require native/GPU validation.

<!-- //ai: backward-compatible bounded environment-reflection experiment. -->
Model parameter `balancedReflection` defaults false. When enabled on an
environment-reflective model, it mixes the already-lit surface and cubemap
using clamped `reflectionBias × Fresnel` instead of adding the environment
term. HDR radiance is not clamped; planar reflection behavior is unchanged.
This bounded mix is not a full energy-conserving BRDF.

<!-- //ai: per-model surface controls for imported one-sided and flipped assets. -->
Model parameters `doubleSided` and `flipNormals` default false; an old preset
that omits them loads unchanged. `doubleSided` disables backface culling for
that model's color, depth/shadow, instanced and planar-fill draws.
`flipNormals` inverts the standard vertex-path shading normal without changing
triangle winding or culling. Both values round-trip through the GFX preset
schema.

<!-- //ai: cubemap inspection reads actual face HDR without the replacement-color audit. -->
`GX_CUBEMAP_HDR_PREFIX=/tmp/.../capture` enables diagnostic float RGBA
readback of the six actual cubemap faces on the 30th `RenderEx` capture,
not necessarily frame 30. `GX_CUBEMAP_HDR_CAPTURE=N` selects a different
positive capture ordinal. It writes `capture.face0.bin` through
`capture.face5.bin` in the existing `GXHDR1` format (bottom-left rows).
It is inactive by default; paths outside `/tmp/` and faces larger than
4096 are refused. Do not combine it with the content-replacing
`GX_CUBEMAP_FACE_DEBUG` control.

- `gX_Renderer_Load/Unload/Update/Draw` — module dispatch via the
  GX_RENDERER_MODULES X-macro table (heaps, scene configs, compositor layers)
- `gX_RendererGFX_*` — 3D forward PBR: Model_Add/AddGeneratedMesh,
  Light_Add and Light_Orbit_Update, Animation_Update,
  Animation_ClipRange, Animation_PoseKey, and the
  Animation_SidecarLoad/SidecarSave pair (GLB clip playback, trimmed
  playback ranges, pose overrides, `<model>.anim.json` authored-key
  persistence, and GPU skinning matrices), shaders (standard/depth/procedural sky),
  render targets,
  reflections (planar + non-planar), CSM, IBL, SSR, SSAO, bloom, TAA,
  decals, particles, fog volume, god rays, lens flare, HDR pipeline
  <!-- //ai: explicit terrain material sidecars preserve separate vertex color semantics. -->
  Material overrides optionally accept `terrainBlend` (default false),
  `terrainAlbedoMap` (second albedo), `terrainDetailMap` (optional non-color
  macro), and `terrainDetailScale` (default 0.1). Paths are texture-resource
  subpaths or absolute external paths. `GLB_TerrainMasksLoad` preserves the red
  channel of normalized/float `COLOR_1` in VAO attribute 9 without modifying
  `COLOR_0` tint or UV2/lightmaps. It mirrors the native all-node triangle
  traversal, including mesh instances. Missing/incompatible masks or unresolved
  second albedo disable blending for that mesh with a warning. Matching uses the
  existing bounded GLB material-name cache. Opted-in materials linearly blend
  decoded sRGB albedos, optionally multiply by macro RGB × 2, clamp, and re-encode
  for the existing color pipeline. Blend state resets per primitive in Phong,
  Gouraud and PBR; base texture alpha remains authoritative. Runtime buffers and
  externally loaded terrain textures are released with the model. The owned
  opt-in probe is `python3 scripts/terrain_material_blend_check.py`.
  <!-- //ai: backward-compatible label placement and authored water height. -->
  Model label parameters include `textOffset` (world-axis offset added to model
  position; omitted/zero preserves the legacy anchor), saved with the model and
  used by billboard/flat text. GFX water initialization supplies height zero;
  updates preserve the authored `water.planeHeight` used by rendering and SSR.
  <!-- //ai: periodic ripple sources are independently authored and backward-compatible. -->
  `water.automaticRipples` is saved by the GFX editor and defaults to enabled
  when omitted by legacy presets. Disabling it stops new periodic ripples;
  active ripples decay normally and `gX_RendererWater_SpawnRipple`/buoy splashes
  remain available. Lake calm-water mode still suppresses periodic sources.
  <!-- //ai: normal detail animation is independent of geometry and ripple sources. -->
  `water.normalMapScroll` stores signed U/V UV-units-per-second rates in the Water
  tab. Zero stops scrolling without disabling normals; legacy presets default to
  `(0.01, 0.01)`, including when loaded after an authored rate in the same process.
  <!-- //ai: requested label styling retains old preset anchors. -->
  Labels also save `textAnchorTop`, `textGap`, `textWorldSize`, `textFit`,
  `textColor`, `textShadowColor` and `textShadowPixels`. New editor labels default
  above transformed mesh bounds; existing labels retain their placement. Bounds
  are the model's existing mesh AABB, not a certified animated/skinned envelope.
<!-- //ai: frustum culling is opt-in and preserves unsupported geometry. -->
  GFX `ENABLE_FRUSTUM_CULLING` uses conservative per-mesh bounds for static file
  geometry; no bounding-box corner must lie inside the view. Bounds belong to
  each model and reset on reload/unload. Skinned, mutable/generated, invalid,
  wave-displaced, orthographic, TAA and reflection-enabled cases remain unculled.
  Shadow, cubemap/reflection and velocity submission paths are unchanged.
  `make gfx-mesh-culling-check` runs owned cache/boundary and exact HDR controls.
- `gX_Billboard_Text_Draw_Styled` / `gX_Text3D_Draw_Styled` — fitted label raster,
  RGBA color and optional 0–4 pixel shadow; original text APIs remain available.
  World-size aspect fitting uses the shared 512×64 texture ratio, and raster
  sizing automatically fits measured glyphs to that texture instead of clipping.
- `gX_MotionPath_*` — shared authored motion-path evaluator: waypoint-polyline
  sampling with wrap, ping-pong, one-shot pause, Catmull-Rom smoothing, and
  per-point Bezier in/out handles (mirrored by default, independent when
  authored) via `gX_MotionPath_Bezier_Point` for models and point lights
- `gX_RendererGFX_Fullscreen_*` — explicit fullscreen pass begin/draw/end
  helper; preserves wrapper RT, shader, and screen-quad behavior
<!-- //ai: explicit profiling owns complete source-frame query sets, not cached per-frame CSV rows. -->
- `gX_RendererGFX_Profile_*` (`renderer/gfx/profile.h`) — opt-in
  `--gfx-profile=path` collector: Init/Begin/StageBegin/StageEnd/End/Deinit.
  Complete nonblocking interval sets retain native `frame`, per-renderer `draw`
  and `readback_frame`; pending/full-ring/tail samples are never fabricated.
  Renderer unload releases queries and closes the CSV before context teardown.
- `gX_RendererGFX_Preset_ToObj` / `gX_RendererGFX_Preset_CameraToObj` — owned
  in-memory JSON objects; caller releases with `json_object_put`. Diagnostic
  mode retains only renderer state/tuning, active camera and compositor flags;
  full preset mode retains the existing scene schema. No disk writes.
- `gX_RendererGFX_ViewportGrid_*` — opt-in `GX_GFX_GRID=1` shared-showcase
  2×2 output-target dashboard; phase 1 scopes cameras only and owns no
  per-tile scene or temporal history
- `gX_RendererGFX_RT_*` — sized render-target initialization and resize reload;
  deferred loading remains with `gX_RendererGFX_RenderTexture_Update`
- `gX_RendererGFX_TemporalHistory_*` — explicit temporal reset, clear,
  frame-index, ping-pong target selection, and post-pass commit state
- `gX_RendererBackground_*`, `gX_RendererPostProcess_*`,
  `gX_RendererTextWriter_*`, `gX_RendererSky_*`, `gX_RendererWater_*`,
  `gX_RendererRaymarch_*`, `gX_RendererAnalog_*` (CRT/VHS), fractal,
  teletext, wireframe, generate, oscillo/lissajous/spectrum, ws2d
- `gX_RendererRaymarch_Quality_Set/Get` — pure Draft/Balanced/High/Full tuning;
  Get returns -1 for Custom. Scene flags/geometry/materials are untouched.
- `gX_RendererRaymarch_DrawHybrid` — opt-in full-resolution opaque SDF overlay
  against current GFX raster depth, using the external perspective camera.
- `gX_RendererRaymarch_DrawSharedDepth` — opaque HDR color plus projected depth
  through the depth test, without sampling the attached scene depth.
- `gX_RendererRaymarch_DrawSharedNormals` — primary world normals and binary
  coverage into the opt-in SSR normal target, with private depth ownership.
- `gX_RendererRaymarch_DrawSharedTemporal` — opt-in SDF ownership markers for
  static camera-only TAA fallback, visibility-checked against separate shared depth.
- `gX_RendererRaymarch_Temporal_Signature` — authored-input fingerprint for
  invalidating static history without treating camera movement as a scene edit.
- `gX_RendererRaymarch_Graph_Compile` — validates a reachable 64-slot CSG DAG
  into at most 32 topological GPU nodes; root -1 bypasses. Cycles, malformed
  references, unsupported repetition and oversized programs return false,
  clear the runtime count, and expose `graph.error` (legacy objects still render).
- `gX_RendererRaymarch_Graph_Combine` — CPU scalar CSG reference, opcodes 2..7;
  difference is A minus B. Zero smoothing is the hard operator; invalid or
  nonfinite inputs return NaN. See [Raymarch](raymarch.md) for node encoding.
- `gX_RendererRaymarch_Object_Type_Set`, `Object_Scale_Get/Set`, `Object_Rotate`,
  `Object_Remove`, `Object_ToWorld` — pure SDF authoring commands. Type changes
  reset dimensions only; scale preserves basis directions; rotations are XYZ
  world-axis degree deltas. Removal compensates slot-dependent yaw and ToWorld
  inverts the stored SDF basis for matching proxy positions. Invalid commands
  fail without mutation; invalid/singular ToWorld returns zero/object center.
- `gX_Renderer_*` — module dispatch internals
- `gX_Renderer_RaymarchDepth_Set` — transient mode 0=off, 1=post-effects opaque
  visibility, 2=shared HDR depth before effects; restores layer
  routing and module flags when disabled, resets on scene changes.
- Sample-scene renderers: `gX_RendererFractal_*`, `gX_RendererGenerate_*`,
  `gX_RendererLissajous_*`, `gX_RendererOscillo_*`, `gX_RendererSpectrum_*`,
  `gX_RendererTeletext_*`, `gX_RendererWireframe_*`, `gX_RendererWS2D_*`,
  `gX_RendererClock_*`, `gX_RendererBG_*` / `gX_RenBG_*`, `gX_WS2D_*`
- `gX_Background_*` — background polisher helpers
- `gX_Analog_*` — analog renderer internals (NTSCPAL, VHS, legacy shaders);
  `gX_TextWriter_*` — textwriter internals (tags/effects/ruby, choice
  collection: `<option line='N'>` entries fill the writer list on
  load/next/jump, `SelectChoice` jumps; TXT load/save with the shipped-text
  harness guard)
- `gX_Showcase_*` — interactive demo controller (--showcase): orbit camera,
  effect hotkeys, model focus, HUD legend
- `gX_Compositor_*` — layer compositing (blend modes per layer)

## gui

- `gX_GUI_Load/Unload/Update/Draw`, `gX_GUI_Handle_Back` — lifecycle,
  canvas system, and keyboard/gamepad Back routing
- `gX_GUI_Builder_*` — immediate widgets (Rect, Color, Text, Button,
  Slider, CheckBox, DropDown, Radio, Scroll, Toggle...)
- `gX_GUI_Element_*` — 15 element sub-modules (rect, color, text, texture,
  model, shader, sound, animation, attribute, data, layout, shape,
  textinput, timer)
- `gX_GUI_Canvas_*` — canvas boxes, buffers, composites
- `gX_GUI_Style/CSS/Markup` — theme.json + css + markup templates
- `gX_GUI_Template_*` — registry (Example/Demo/Demo2/Scene/EscapeMenu/
  Message/DebugDisplay)
- `gX_GUI_OnClick` / `gX_Widget_*` — click callbacks
- `gX_GUI_AddMake(templateID, canvasIndex)` — rebuild a canvas
- `gX_GUI_Trace_*` — opt-in `GX_GUI_TRACE=1` audit readout (interaction
  events + per-build element maps) for the scripted GUI gates
- `gX_ColorPicker_*` — color-picker widget internals; `gX_UI_*` — layout
  debug panels (rulers/state panels)

## audio

- `gX_Audio_Init/Close` — device lifecycle; volume/pan
- `gX_Synth_*` — procedural FM/ADSR synthesizer (per-voice filters ride the
  shared TPT core from `src/audio/filter.h`; full-range cutoff): voices,
  patches,
  presets (Load/Save — full global state incl. oscillator/LFO/FM
  defaults), wavetable gen/file-load/file-save (`gX_Synth_WT_Generate`,
  `gX_Synth_WT_LoadFile` / `gX_Synth_WT_SaveFile` — raw float32
  1..2048 samples, 0/1 status; saves refuse shipped fixture paths under the
  harness guard), arpeggiator, delay, portamento; scope buffer
  for lissajous/oscillo/spectrum renderers. `gX_Synth_Update` (per-frame
  driver: arp/sequencer ticking + lazy mixer attach/detach),
  `gX_Synth_Render` (offline stereo core shared by the raylib mixed
  processor and the unit tests), `gX_Synth_IsAttached`. Output is muted by
  default (`GX_SYNTH_MUTE=0` or `gX_Synth_SetMuted(0)` opens it); the
  engine and scope keep running while muted.
- `gX_Music_GetLength/GetTimePlayed` — streamed music queries
- `gX_OPX_*` — the 8-operator phase-modulation FM engine
  (`src/audio/opx.{h,c}`), separate from the demo synth: 8 sine operators
  per voice (own ratio, level, detune, self-feedback and ADSR), a fixed
  data-driven algorithm table (8 routings — `gX_OPX_AlgorithmCount/Name` +
  the `gX_OPX_AlgorithmRouting` readout; every modulator has a lower index
  than its target, so one forward pass evaluates the graph),
  `gX_OPX_NoteOn/NoteOff/NoteOff_ID/AllNotesOff`, default/operator surface
  (`gX_OPX_SetDefault*` for new notes, `gX_OPX_SetOperator*` for sounding
  notes), `gX_OPX_SetPan/MasterVolume`, offline core `gX_OPX_Render`
  (shared by the raylib mixed-processor adapter and the unit tests) and
  `gX_OPX_Update` (lazy attach/detach) and the JSON preset round trip
  (`gX_OPX_Preset_Save/Load` in `opx_schema.c`; resource paths refused under
  the harness guard). Output rides the synth bus and starts muted
  (`GX_OPX_MUTE=0` or `gX_OPX_SetMuted(0)` opens it).
- `gX_Pad_*` — the layered ensemble pad engine (`src/audio/pad.{h,c}`;
  per-voice filter on the shared TPT core):
  three detuned saw/tri layers per voice (`gX_Pad_Layer`: waveform,
  detuneCents, level, pan, driftRate/driftDepth), long ADSR, a per-voice
  resonant filter (`gX_Pad_SetVoiceFilter`, LP/HP/BP) with a slow filter
  LFO, an engine-level stereo `gX_Pad_SetChorus` and `gX_Pad_SetWidth`
  over the layer pans. Defaults→note and sounding-voice edit surfaces,
  `gX_PAD_MUTE` (default muted, `gX_Pad_SetMuted`), offline core
  `gX_Pad_Render` + `gX_Pad_Update` (lazy attach/detach) and the JSON preset
  round trip (`gX_Pad_Preset_Save/Load` in `pad_schema.c`; resource paths
  refused under the harness guard) — see `docs/audio.md`.
- `gX_Audio_Reverb_*` — heap-lazy reverb (allocates on first enable);
  `gX_Audio_Reverb_Process` is the device-free stereo core (damping is a
  one-pole absorbtion on the recirculating tap — the parameter used to be
  stored but ignored), `gX_Audio_Reverb_Clear` drops the tail
- `gX_AudioSFX_*` — procedural one-shot SFX pool
  (`src/audio/sfx.c`): `gX_AudioSFX_BuildWave` (pure, deterministic
  variant synthesis: thud/clack/metal/arc/click) + `gX_AudioSFX_WaveFrames`,
  `gX_AudioSFX_Play` (3D position/volume/pitch, bus-scaled, rate-limited,
  copies per variant so bursts overlap) and the pool lifetime; used by the
  sandbox's impact/tool audio
- `gX_Audio_Reverb_*` / `gX_Audio_Bus*` — mixer buses
  (`GX_AUDIO_BUS_MASTER/MUSIC/SFX/SYNTH`): `gX_Audio_BusSetVolume`/
  `BusGetVolume`/`BusGain` scale every gX playback path (registry sounds,
  one-shots, 3D/SFX, music, synth). Master is raylib's device master
  (0..2); the rest are 0..1 gains. `gX_Audio_Sound_PlayBus` plays a
  registry sound through the SFX bus; `gX_Audio_SetMuted`/`IsMuted` is the
  global playback mute (`GX_AUDIO_MUTE=1` for quiet audits)
- `gX_Audio_Filter_*` / `gX_Audio_MasterFilter_*` — the shared TPT/ZDF
  state-variable filter (`gX_Audio_Filter_Set/Prepare/Step/Process/Reset`;
  cutoff normalized to Nyquist so the whole 0..1 range is usable) and the
  engine-level master filter on the final mix (`MasterFilter_Set/Init/
  Unload/Update` — the Update keeps the processor at the mixed-chain tail
  as the engines attach lazily; persisted with the settings and exposed to
  scripts as `Audio_SetMasterFilter`/`Audio_GetMasterFilter*`)
- `gX_Audio_*` — full family: Init/Close, SoundGet/SoundRegister/SoundPlay,
  Update, Gen_Play (procedural SFX), Play3D (pitch-aware `Play3DEx`),
  Music; `gX_BackendAudio_*` — audio backend shim (`--disableAudio`
  actually skips the device)
- `gX_AudioStream_*` — custom audio streams (wrappers)

## resource

- `gX_ResourceManager_*` — typed heaps (font/model/music/shader/sound/
  texture), `gX_ResourceSlot_*` registry
- `gX_RMHeap_Load/CheckElseLoadFileName` — load-or-404 fallback chain
- `gX_Resource_Load*` — on-demand loading (defaults preloaded in
  `loaddefaults.c`; broken refs pruned at compression P2)

## camera / input (standalone modules)

- `gX_Camera_*` — camera modes (FREE/FIRSTPERSON/THIRDPERSON/TOP/ORTHO),
  orbit, shake (`gX_Camera_ShakeStart`, settings-togglable)
- `gX_Bind_*` — per-scene input binds; `gX_Input_*` — raw
  keyboard/mouse/gamepad state (input.c); F4 exit + ESC-cancel handling;
  `gX_AI_Input_Synth` feeds scripted keyboard/mouse events to ImGui
- `gX_Key_IsPressed/IsDown` — edge/held query via `gX_Keys` enum

## headless input (GX_AI_INPUT)

- Syntax: `"t click x y; t drag x1 y1 x2 y2 ms [R|X|Y|Z|RX|RY|RZ|CTRL+R|CTRL+RX|CTRL+RY|CTRL+RZ|CTRL+X|CTRL+Y|CTRL+Z]; t move x y ms; t key code [shift] [ctrl]"`
  (up to 16 actions; `t` = wall-clock seconds, coords = window px, key uses
  the gX_Keys enum value; shift/ctrl are optional 0/1 modifiers).
  `move` parks button-up (pre-position: clicks route by last-frame hover).
- Engine mouse owns game coords (`Update_Mouse` maps once via the live
  viewport); scripts author window px (mapped on write). While a script
  runs it owns the cursor (idle frames park at last script pos).
- ImGui bridge: synth pos via events + left button edges, timed key events,
  and optional drag chord events (posted after `rlImGuiBegin`); the backend mouse
  poll is suppressed while scripted. Headless has no OS focus: mouse scripts
  may need an initial focus click; key-only actions also reach ImGui navigation
  and editor shortcuts.
- Related hooks: `GX_TOOLS_SELECT=<idx>`, `GX_TOOLS_CLOSE=<frame>`,
  `--ai-screenshot-after N`.

## scene

- `gX_Scene_Load/Unload/Update/Draw` — dispatcher
- `gX_SceneTitle_*` — title demo (gradient + gX text)
  classification and the 32×48 teletext page builder
- `gX_SceneBattle_*` — native tactical battle arena + HUD; bounded grid,
  actor-owned three-member party/controller IDs, independent real-time ATB and
  cooldowns, optional control transfer, movement/facing, and a GL-free two-wave
  runtime (`Runtime_Init/Update/ConfirmAction/ConfirmActorAction/IsActorReady/
  GetControlledActor/SetControlledActor/SetActorController/SelectTarget/
  MoveActor/TurnActor/MovePlayer/TurnPlayer`). Party AI companions autofight;
  deterministic hostile profiles expose cached per-enemy telegraphs through
  `Runtime_GetEnemyIntent`. Target routing, spell selectors, guard/item rules,
  wave outcomes, bounded actor-indexed impact history, transient Polisher
  tuning, and pure grid/queue helpers remain headless-testable.
- `gX_Scene_*` — dispatcher sub-API (Load/Unload/Update/Draw helpers)
- `gX_ScenePhysics_*` — rigid-body props + ragdoll sandbox (`--physics`): shared-world
  bodies, staggered spawn, mouse drag/spawn/delete, `T` ragdoll / `R` reset keys;
  props and limbs render through the GFX renderer (`GX_PHYSICS_PROPS`,
  `GX_PHYSICS_RAGDOLLS` set the counts), impact sparks drive the GPU particle
  emitters from contact events, ragdoll bones join the EM/thermal appliers
  (`gX_ScenePhysics_RagdollRegister`/`Unregister`), `K`/`GX_PHYSICS_DEBUG`
  draws the collider/contact debug overlay, joint rigs exercise the
  constraint family (hanging chain, hinged door, motorized windmill, slider
  rail — all mouse-draggable; `GX_PHYSICS_JOINTS=0` skips;
  `gX_ScenePhysics_BuildJointRigs` is the headless-test entry), contact
  materials (element friction/restitution via
  `gX_ScenePhysics_MaterialForElement`, steel/tissue/ground conventions,
  `GX_PHYSICS_MATERIALS=0` opts out), a walled water pool driven by
  `gX_PhysicsFluid_*` (buoyancy, drag, splash entries; `P` toggles,
  `GX_PHYSICS_WATER=0` skips; `gX_ScenePhysics_BuildWater` is the headless
  entry), a bar-magnet row with `gX_PhysicsEM_SetDipole` moments (`N`
  toggles the forces, `GX_PHYSICS_MAGNETS=0`/`=1`;
  `gX_ScenePhysics_BuildMagnets`/`SetMagnets` are the headless entries),
  iron test pieces registering their element susceptibility so the forces
  pull them in, a conveyor belt strip carried by the wrapper's surface
  velocity (`GX_PHYSICS_BELT=0` skips; `gX_ScenePhysics_BuildBelt` is the
  headless entry), kinematic movers — a driven lift platform (vertical
  sine) and a spinning turntable carrying payload crates
  (`GX_PHYSICS_MOVERS=0` skips; `gX_ScenePhysics_BuildMovers`/
  `DriveMovers`/`SpawnPayload` are the headless entries), balance posts —
  three identical planks whose top-heavy / centered / bottom-heavy mass
  centers decide the same tilt test (`B` re-poses and releases them,
  `GX_PHYSICS_COM=0` skips; `gX_ScenePhysics_BuildBalancePosts`/
  `NudgeBalance` are the headless entries), a hinge2 rover — a four-wheel
  cart on cylinder wheels (steered front wheels locked in the spawn pose,
  front-wheel drive, suspension lean) that laps the open floor
  (`GX_PHYSICS_ROVER=0` skips; `gX_ScenePhysics_BuildRover` is the
  headless entry) with standing cylinder props in the grid mix, a break
  target — a board welded to a gantry by a fixed joint whose feedback the
  scene polls; hits above the break force snap it and `R` rewelds
  (`GX_PHYSICS_TARGET=0` skips; `gX_ScenePhysics_BuildBreakTarget`/
  `UpdateBreakTarget` are the headless entries), a piston pump (a vertical
  plunger on a piston joint, square-wave driven with a rotation motor;
  `GX_PHYSICS_PUMP=0` skips) and a heightfield terrain mound past the
  belt's far end (`GX_PHYSICS_MOUND=0` skips; `BuildPump`/`DrivePump`/
  `BuildMound` are the headless entries), a tuning
  surface (`gX_ScenePhysics_Tuning_*`:
  defaults/live apply/JSON presets under `resources/tools/physics`;
  `--physics-editor`, `GX_TOOLS_OPEN=physics` or an EXTRAS card opens
  `src/tools/physics/physics_editor.cpp`; `GX_PHYSICS_PRESET=<json>` seeds a
  run) with a toolgun mode (its Toolgun button or `GX_TOOLS_TOOLGUN=1`)
  whose aims resolve through `gX_ScenePhysics_AimAtModel` and whose tools
  are the `ToolGrab`/`ToolRemove`/`ToolPin`/`ToolUnpin`/`PinCount`/
  `SetToolMode` surface (trigger pulls play a procedural tool tick), an
  impact-audio layer (queued contacts play the procedural SFX pool at the
  contact point — energy drives level/pitch, charge selects the arc,
  desaturated bright tints and hot bodies ring metal; `GX_PHYSICS_SFX=0`
  or the tuning's `sfx` toggle disables;
  `gX_ScenePhysics_ImpactSoundMap` is the pure mapping and
  `SetSFXEnabled`/`IsSFXEnabled` the live toggle), and
  `gX_Ragdoll_*` is the
  humanoid rig
  (create/destroy, T-pose reset, enable/disable, bone/joint queries; bones
  run at tonne-convention sim mass with the nominal kg kept for the thermal
  and impact-FX registries)
- `masterIndex` switch via `gX_MainData_Switch`, subsystem
  `masterIndexSwitchResponse`
- `gX_SceneSynthVis_*` — playable synth visualizer scene (`--synthvis`,
  gameX port): the physical key rows (white `Z X C V B N M , . / Q W E R`,
  black `S D G H J 2 3 5 6 7`) drive `gX_Synth_*` notes; the scene composites
  the renderer's oscillo/lissajous/spectrum module RTs into `renderer.canvas`
  and the GUI keyboard canvas lights the held keys. Park silences notes on
  scene exit; `GX_SYNTHVIS_AUTOPLAY=1` holds a demo arpeggio for captures.

## main

- `gX_Parse` — CLI flags (--scene, --test-only, --logLevel, --module, ...)
- `gX_Module_*` — dynamic module host (Route A; `docs/dynamic_modules.md`):
  `Load` (dlopen `gX_Module_Entry`, ABI handshake, registration),
  `LoadConfigured` (`--module <path.so>` → `GX_MODULE=a:b` → `modules/*.so`
  auto-scan), `Update` / `Draw` / `SceneEnter` / `SceneExit` (registry
  iterators called unconditionally by the loop), `PreUnload` / `Unload`
  (shutdown callbacks; handles never `dlclose`d), `ExitCode` (max of module
  getters drives the process exit), `AnyLoaded` / `ToolsAvailable`, and the
  kernel interop `MarkupClick` / `MarkupEnsure` / `ToolsDraw` / `OverlayOpen`
  / `OverlayDraw` (module-provided handlers for markup onclick, companion
  scripts, the debugger panel and the console overlay). The export list
  (`modules/module_exports.txt`, `make module-exports`) is the ABI —
  append-only within `GX_MODULE_ABI_VERSION`.
- `gX_DevCube_*` — module-local 60 Hz phase helper (`PhaseTicks`,
  `PhaseSeconds`) shared by `gx_devcube` and its headless test; see
  `docs/dynamic_modules.md`.

## scripting (gx_shell dynamic module)

Build with `-DGX_USE_ANGELSCRIPT=ON` (`make check-scripting`), which now
builds the `gx_shell.so` dynamic module instead of a second exe; the default
build compiles no scripting code and the lean payload is unchanged, and the
shell args warn without a module. The shell contract, behavior guarantees and
the not-in-slice list live in `docs/scripting.md`.

- `gX_Scripting_*` — host lifecycle + script heap: `Load` / `Unload` /
  `Update` (engine, heap, per-frame execution, hot-reload watch,
  virtual-input merge), `AddScript` / `AddScriptWithLibs` / `GetScript` /
  `RemoveScript` / `RemoveAllScripts`, `RegisterHook` / `TriggerHook`
  (named callbacks; `Hook("scene_enter", Func)` scene wiring),
  `GetActiveVirt`, `CheckReload`, `RunFunctionByName` / `IsLoaded` /
  `EnsureLoaded` / `FireEvent` (companion-script dispatch on fresh contexts;
  markup `onclick`) and the `State_*` KV store surviving hot-reload
- `gX_Script_*` — script file/module/context lifecycle: `Load` / `Unload` /
  `Compile` / `Execute` / `Reload` (hot-reload preserves the path across the
  struct reset; the gameX original silently lost it)
- Shell surface (script-visible): math/Print/Sleep, `Assert*`/`Fail`/`Quit`,
  key/mouse state + `KEY_*`, virtual input (`GUIClick`/`GUIClickElement`/
  `GUIKey`/`GUIDeactivateVirtualInput`), GUI/DOM queries (`GUIFindElement`,
  element name/text/rect/state getters, `GUIAssertTextContains`), `Vec3`,
  `Hook`/`TriggerHook`, `SwitchScene`, render scale, screenshot, state KV
  (`ScriptSaveState`/`ScriptLoadState` file persistence), script list +
  debugger (`ScriptCount`, `ScriptGetPath/Name`, `DebugToggleBreakpoint`,
  `DebugContinue/Step/StepOver/StepOut`, `DebugIsPaused`, `DebugGetLine`);
  boot scan (`resources/script/*.as`, `GX_SCRIPT_SCAN`/`GX_SCRIPT_DIR`) and
  mod scan (`custom/*/script/*.as`, `GX_SCRIPT_MOD_DIR`)
- `gX_Tools_OpenScript_Runtime` — scripting debugger panel
  (`--script-editor`, `GX_TOOLS_OPEN=script`; opt-in build)
- `gX_TourCamera_*` / `gX_TourGfx_*` / `gX_TourAudio_*` /
  `gX_TourBackground_*` / `gX_TourSynth_*` / `gX_TourOPX_*` /
  `gX_TourPad_*` / `gX_TourTextWriter_*` /
  `gX_TourOverlay_*` / `gX_TourRaymarch_*` / `gX_TourPostProcess_*` — tour API
  registration (`*_Register`; camera + keyframe rail, gfx flags/settings/
  models/lights, music + beep, the background param surface with
  presets/transition, synth notes/patches/arp/sequencer, textwriter files/
  lines/reveal/fonts/tagged draws, the overlay paint list, raymarch params/
  objects/presets, postprocess chain/feedback/inject); called by the
  shell's `RegisterAll`, and the
  module exposes the script-facing `Camera_*`, `Gfx_*`, `Model_*`, `Light_*`,
  `Music_*`, `Sound_Beep`, `Background_*`, `Synth_*`, `TextWriter_*`,
  `Overlay_*`, `Raymarch_*`, `PostProcess_*`, `Time_GetDeltaTime` names

## console (opt-in shell build)

- `gX_Console_*` — developer console: `Init`/`Uninit`, `Update` (F10 toggle +
  one-shot `GX_CONSOLE_EXEC` batch), `Draw` (ImGui panel), `SetOpen`/`IsOpen`,
  `AddLog`/`Clear`, `Execute` (built-in dispatch + AngelScript one-liner
  fall-through), log/history accessors. Opened by `--console`, toggled by F10
  (`input.consoleKey`); built-ins: `/clear /help /history /scene /scenes
  /msg /echo /time /stats /ls /reload /exec /run /scripts /state`.
  One-liners run as throwaway modules via `gX_Console_ExecScript`
  (`src/console/console_exec.cpp`). Compiled into `gx_shell.so`
  (`GX_USE_ANGELSCRIPT=ON`).
- `gX_Scripting_Shell_*` — assert failure count + requested exit code
  (`gX_build --script gate.as` returns non-zero on failed asserts)

## tools (ImGui suite, deterministic editor chrome)

- `gX_Tools_*` — runtime open/close + CLI inits (`--tools`, `--theme-editor`)
- `gX_Tools_RuntimeActive` — any in-app tool open (draw gate)
- `gX_Tools_Toolgun_*` — the shared crosshair/tool mode for in-engine
  editors (open a tool table, step the input each frame, draw the crosshair
  + viewmodel overlay; the physics sandbox adopts it via
  `GX_TOOLS_TOOLGUN=1`/its Toolgun button)
- `gX_Tools_Open*_Runtime` — open the in-app tools from their buttons (the
  GUI editor, theme editor, theme lab, GFX editor, wavetable editor,
  textwriter editor, physics editor, camera rail editor, background editor
  and sky editor; the script debugger opens through its runtime flag only).
  Openers: `Editor`, `Theme`, `ThemLab`, `Materials`, `ShaderLab`, `Gfx`,
  `Script`, `Wavetable`, `Textwriter`, `Physics`, `Rail`, `Background`,
  `Sky`. CLI equivalents: `--tools`, `--theme-editor`, `--themlab`,
  `--materialslab`, `--shaderlab`, `--gfx-editor`, `--script-editor`,
  `--wavetable-editor`, `--textwriter-editor`, `--physics-editor`,
  `--rail-editor`, `--background-editor`, `--sky-editor`. `GX_TOOLS_OPEN` =
  editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics|rail|background|sky
  is the headless hook; the rail panel is drawn by the gx_shell module.
- `gX_Tools_Close_Runtime` — close all runtime tools (window X closes one)
- `gX_Tests` — unit tests (math + heap + JSON), 0 errors = green
- `gX_Load` → `gX_Loop` → `gX_Unload` — lifecycle
- `gX_Settings_*` — window/audio/accessibility persistence
  (the locally generated `settings.json` under `resources/`), including tool UI scale, keyboard navigation,
  and live-previewable Default, High Contrast, and Color Vision Friendly themes;
  dirty-flag + save-on-exit; CLI overrides window size/FOV
- `gX_MainData_*` — global state, scene switching, exit
- `gX_asm_*` — register/hello debug helpers (src/asm)
- `gX_AI_*` / `gX_AiScript_*` — AI probe (kept uncompiled behind
  `gX_ENABLE_GFXAI`; headers remain for resurrection)
