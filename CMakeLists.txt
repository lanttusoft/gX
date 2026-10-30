cmake_minimum_required(VERSION 3.10)
project(gX_build LANGUAGES C CXX ASM)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CompilerFlags)

#opencode: v0.5.0 — compute/SSBO light culling. raylib must build with
#   GRAPHICS_API_OPENGL_43 (OPENGL_VERSION=4.3) or compute+SSBO compile out
#   of rlgl with only log warnings. Default build stays GL 3.3 (uniform
#   lights, 32-slot ceiling); the 3.3 binary ignores the SSBO path.
option(GX_COMPUTE "Build raylib with OpenGL 4.3 + enable SSBO light path" OFF)
if(GX_COMPUTE)
    add_definitions(-DGX_COMPUTE)
    set(OPENGL_VERSION "4.3" CACHE STRING "raylib OpenGL version" FORCE)
else()
    set(OPENGL_VERSION "OFF" CACHE STRING "raylib OpenGL version" FORCE)
endif()

#opencode: kernel cluster — renderer/scene form an atomic unit
#   (scene uses renderer). Setting GX_ENABLE_RENDERER or
#   GX_ENABLE_SCENE=OFF drops both source dirs (kernel-only build:
#   lifecycle + gui/tools, masterIndex stays NULL).
#   NOTE: gX_ENABLE_GAME keeps its config.h default (0 = game-content
#   headers absent) in BOTH configs — it is not a cmake option yet.
#   cmake -B build/kernel -DGX_ENABLE_RENDERER=OFF && cmake --build build/kernel
option(GX_ENABLE_RENDERER "Enable renderer module (atomic with scene+game sources)" ON)
option(GX_ENABLE_SCENE "Enable scene module (atomic with renderer+game sources)" ON)
if(NOT GX_ENABLE_RENDERER OR NOT GX_ENABLE_SCENE)
    set(GX_ENABLE_RENDERER OFF CACHE BOOL "Enable renderer module (atomic with scene+game sources)" FORCE)
    set(GX_ENABLE_SCENE OFF CACHE BOOL "Enable scene module (atomic with renderer+game sources)" FORCE)
endif()
if(GX_ENABLE_RENDERER)
    add_definitions(-DgX_ENABLE_RENDERER=1)
else()
    add_definitions(-DgX_ENABLE_RENDERER=0)
endif()
if(GX_ENABLE_SCENE)
    add_definitions(-DgX_ENABLE_SCENE=1)
else()
    add_definitions(-DgX_ENABLE_SCENE=0)
endif()

#ai: optional ODE rigid-body backend for the physics wrapper
#   (wrappers/physics.c). When found, the wrapper compiles its ODE
#   implementation; otherwise no-op stubs keep the engine building without
#   the dependency. Disable explicitly with -DGX_USE_ODE=OFF.
option(GX_USE_ODE "Enable the ODE rigid-body physics backend (stubs when off/missing)" ON)
if(GX_USE_ODE)
    find_path(GX_ODE_INCLUDE_DIR ode/ode.h)
    find_library(GX_ODE_LIBRARY ode)
    if(GX_ODE_INCLUDE_DIR AND GX_ODE_LIBRARY)
        add_definitions(-DGX_USE_ODE)
        message(STATUS "ODE: found (${GX_ODE_LIBRARY}) — physics enabled")
    else()
        set(GX_USE_ODE OFF)
        message(STATUS "ODE: not found — physics stubs active")
    endif()
else()
    message(STATUS "ODE: disabled — physics stubs active")
endif()

#ai: opt-in AngelScript scripting shell (host + distilled core/state API).
#   OFF by default — the default Release payload and the lean gate stay
#   untouched. Its own config + gate: cmake -B build/scripting
#   -DGX_USE_ANGELSCRIPT=ON (see Makefile check-scripting).
option(GX_USE_ANGELSCRIPT "Enable the AngelScript scripting shell (opt-in)" OFF)
if(GX_USE_ANGELSCRIPT AND NOT GX_ENABLE_SCENE)
    set(GX_USE_ANGELSCRIPT OFF CACHE BOOL "Enable the AngelScript scripting shell (opt-in)" FORCE)
    message(STATUS "AngelScript: disabled (kernel-only build — scripting needs the scene module)")
endif()

set(GX_SOURCES
    src/main.c
    src/main_load.c
    src/main_loop.c
    src/main_unload.c
    src/main_update.c
    src/main_parse.c
    src/main_tests.c
    #opencode: dynamic module host (dlopen loader + hook registry)
    src/module/module.c
    src/audio.c
    src/audio/filter.c
    src/audio/filter_tests.c
    src/audio/sfx.c
    src/audio/sfx_tests.c
    src/audio/opx.c
    src/audio/opx_schema.c
    src/audio/opx_tests.c
    src/audio/pad.c
    src/audio/pad_schema.c
    src/audio/pad_tests.c
    src/audio/synth.c
    src/audio/synth_schema.c
    src/audio/synth_tests.c
    src/camera.c
    src/input.c
    src/common/heap.c
    src/common/log.c
    src/common/error.c
    src/common/debug.c
    src/common/enum.c
    src/common/alloc.c
    src/common/random.c
    src/common/profiler.c
    src/common/utils.c
    src/common/shader_compiler.c
    src/common/path.c
    src/common/msg.c
    src/common/hardware.c
    src/common/hashmap.c
    src/common/pool.c
    src/common/polisher.c
    src/common/data.c
    src/common/threading.c
    src/common/utils/billboard.c
    src/common/utils/shader.c
    src/common/utils/pointercast.c
    src/common/marshal/json.c
    src/common/marshal/marshal.c
    src/common/marshal/schema_json.c
    src/common/marshal/json_impl.c
    src/rhi/batch.c
    src/rhi/gl_draw.c
    src/rhi/renderdoc_hook.c
    src/rhi/gl_state.c
    src/rhi/rhi_buf.c
    src/rhi/rhi_caps.c
    src/rhi/rhi_bindless.c
    #opencode: opt-in runtime render-target lifecycle tracing.
    src/rhi/rhi_buffer_audit.c
    src/rhi/rhi_debug.c
    src/rhi/rhi_event_log.c
    src/rhi/rhi_fbo.c
    src/rhi/rhi_matrix.c
    src/rhi/rhi_query.c
    src/rhi/rhi_state.c
    src/rhi/rhi_tex.c
    #ai: opt-in depth storage precision diagnostics, compiled out of lean Release.
    src/rhi/rhi_depth_probe.c
    src/rhi/rhi_validate.c
    src/rhi/rhi_mesh.c
    src/game/ephemeris.c
    src/backend/raylib/raylib_wrappers.c
    src/backend/raylib/raylib_wrappers_texture.c
    src/backend/raylib/raylib_wrappers_model.c
    src/backend/raylib/raylib_wrappers_load.c
    src/backend/raylib/raylib_wrappers_control.c
    src/backend/raylib/raylib_wrappers_query.c
    src/wrappers/camera.c
    src/wrappers/drawing.c
    src/wrappers/model.c
    src/wrappers/shader.c
    src/wrappers/texture.c
    src/wrappers/wrappers.c
    src/wrappers/convert.c
    src/wrappers/color.c
    src/wrappers/image.c
    src/wrappers/file.c
    src/wrappers/collision.c
    src/wrappers/physics.c

    #ai: physics module — shared simulation world (fixed-step accumulator)
    src/physics/physics.c
    src/physics/physics_tests.c
    src/physics/ragdoll.c
    #ai: element table + EM applier (charged-body fields) + thermal applier
    src/physics/elements.c
    src/physics/em.c
    src/physics/relativity.c
    src/physics/thermal.c
    src/physics/fluid.c

    src/wrappers/wrappers_gl.c
    src/wrappers/input.c
    src/wrappers/audio.c
    src/wrappers/window.c
    src/wrappers/misc.c
    src/wrappers/text.c
    src/wrappers/shape.c
    src/gui/enum.c
    src/gui/utils.c
    src/gui/canvas.c
    src/gui/builder.c
    src/gui/gui.c
    src/gui/respond.c
    src/gui/make.c
    src/gui/update.c
    src/gui/schema.c
    src/gui/builder_widgets.c
    src/gui/widgetsdf.c
    src/gui/css.c
    src/gui/markup.c
    #opencode: v0.4.0 — gX Tools suite: vendored Dear ImGui runtime (imgui
    #   core + rlImGui raylib backend). Tools = in-engine editors whose chrome
    #   does NOT depend on the gX GUI itself (breaks the gui-creates-gui loop).
    src/tools/imgui/imgui.cpp
    src/tools/imgui/imgui_draw.cpp
    src/tools/imgui/imgui_tables.cpp
    src/tools/imgui/imgui_widgets.cpp
    src/tools/imgui/rlImGui.cpp
    src/tools/imgui/imgui_gx.cpp
    src/tools/editor/editor.cpp
    src/tools/theme/theme_editor.cpp
    src/tools/gfx/gfx_editor.cpp
    #opencode: synth wavetable authoring panel (draw/generate + .f32 fixtures).
    src/tools/synth/wavetable_editor.cpp
    #opencode: textwriter authoring panel (lines/tags/choices + preview).
    src/tools/textwriter/textwriter_editor.cpp
    #opencode: background tuning body (shared by the GFX + background editors).
    src/tools/background/bg_panel.cpp
    #opencode: standalone background editor (presets/polisher/react).
    src/tools/background/background_editor.cpp
    #opencode: sky editor (look params + ephemeris clock + presets).
    src/tools/sky/sky_editor.cpp
    #ai: physics sandbox/applier tuning panel with JSON presets.
    src/tools/physics/physics_editor.cpp
    #ai: private leaf panels keep their asset/history ownership in the editor.
    src/tools/gfx/water_panel.cpp
    src/tools/gfx/glass_panel.cpp
    src/tools/tools.c
    #ai: shared crosshair/tool mode (GMod-style toolgun) for editors.
    src/tools/toolgun.c
    src/gui/style.c
    src/gui/transition.c
    src/gui/colorpicker.c
    src/gui/element/element.c
    src/gui/element/animation.c
    src/gui/element/attribute.c
    src/gui/element/color.c
    src/gui/element/data.c
    src/gui/element/layout.c
    src/gui/element/make.c
    src/gui/element/model.c
    src/gui/element/shader.c
    src/gui/element/shape.c
    src/gui/element/sound.c
    src/gui/element/text.c
    src/gui/element/textinput.c
    src/gui/element/texture.c
    src/gui/element/timer.c
    src/gui/templates/template_registry.c
    src/gui/templates/element.c
    src/gui/templates/element_template_registry.c
    src/ui/layout_debug.c
    src/gui/templates/templates.c
    src/gui/templates/settings.c
    src/gui/templates/titlegx.c
    src/gui/templates/guitester.c
    src/gui/templates/guieditor.c
    src/gui/templates/themlab.c
    src/gui/templates/materialslab.c
    src/gui/templates/shaderlab.c
    src/gui/binding.c

    # Resource manager (kernel: always on)
    src/resource/rmheap.c
    src/resource/resource.c
    src/game/game.c
    src/settings.c
    src/resource/loaddefaults.c

    # gxmath — pure math library (no external dependencies)
    src/gxmath/calculus.c
    src/gxmath/color.c
    src/gxmath/convert.c
    src/gxmath/easing.c
    src/gxmath/fourier.c
    src/gxmath/gxmath.c
    src/gxmath/gxmath_tests.c
    src/tools/toolgun_tests.c
    src/gxmath/matrix.c
    src/gxmath/physics.c
    src/gxmath/quaternion.c
    src/gxmath/random.c
    src/gxmath/scalar.c
    src/gxmath/sdf.c
    src/gxmath/vector2.c
    src/gxmath/vector3.c
    src/gxmath/vector4.c
    src/gxmath/vectormath.s
)

#opencode: kernel cluster sources — renderer/scene compile only in
#   full builds (GX_ENABLE_RENDERER=ON, the default). Kernel-only builds
#   drop the cluster atomically; call sites are #if-guarded.
#   NOTE: game/ (ephemeris math + shim, dependency-free like gxmath)
#   stays always-on; gX_ENABLE_GAME keeps its config.h default (0) in
#   both configs and gates only the absent game-content headers.
if(GX_ENABLE_RENDERER)
    set(GX_SOURCES ${GX_SOURCES}
        src/scene/scene.c
        src/scene/battle.c
        src/scene/battle_ai.c
        src/scene/battle_actions.c
        src/scene/battle_logic.c
        src/scene/battle_runtime.c
        src/scene/battle_tests.c
        src/scene/title.c
        #ai: physics sandbox scene — props in the shared physics world
        src/scene/physics.c
        src/scene/physics_tests.c
        #opencode: synth visualizer scene — playable keyboard + visualizer composite
        src/scene/synthvis.c
        src/scene/synthvis_tests.c
        #opencode: the synthvis keyboard template reads scene.synthvis — keep it
        #   in the renderer/scene cluster (kernel builds have no scene).
        src/gui/templates/synthvis.c
        src/renderer/renderer.c
        src/renderer/renderer_tests.c
        src/renderer/compositor.c
        src/renderer/analog.c
        src/renderer/analog/analog_shader.c
        src/renderer/analog/ntsc_pal.c
        src/renderer/analog/vhs/vhs.c
        src/external/crt_core.c
        src/external/crt_ntsc.c
        src/renderer/clock.c
        src/renderer/fractal.c
        src/renderer/fractal/fractal_preset.c
        src/renderer/sky.c
        src/renderer/sky_preset.c
        src/renderer/teletext.c
        src/renderer/water.c
        src/renderer/wireframe.c
        src/renderer/generate.c
        src/renderer/gfx.c
        src/renderer/cubemap.c
        src/renderer/oscillo.c
        src/renderer/lissajous.c
        src/renderer/spectrum.c
        src/renderer/ws2d.c
        src/renderer/gfx/bloom.c
        src/renderer/gfx/camera.c
        src/renderer/gfx/decal.c
        src/renderer/gfx/dof.c
        src/renderer/gfx/draw.c
        src/renderer/gfx/fog_volume.c
        src/renderer/gfx/clusters.c
        src/renderer/gfx/contact.c
        src/renderer/gfx/fullscreen_pass.c
        src/renderer/gfx/gfx_pipeline.c
        #ai: explicit --gfx-profile owns frame-matched query/file lifetime.
        src/renderer/gfx/profile.c
        src/renderer/gfx/showcase.c
        src/renderer/gfx/gfx_render.c
        src/renderer/gfx/glb_loader.c
        src/renderer/gfx/glb_writer.c
        src/renderer/gfx/gl_level.c
        src/renderer/gfx/godray.c
        src/renderer/gfx/rt_helpers.c
        src/renderer/gfx/terrain.c
        src/renderer/gfx/lensflare.c
        src/renderer/gfx/layout.c
        src/renderer/gfx/light.c
        src/renderer/gfx/load.c
        src/renderer/gfx/model.c
        src/renderer/gfx/motion_path.c
        src/renderer/gfx/animation.c
        src/renderer/gfx/motionblur.c
        src/renderer/gfx/particle.c
        src/renderer/gfx/splat.c
        src/renderer/gfx/mirror.c
        src/renderer/gfx/weather.c
        src/renderer/gfx/oracle_gpu.c
        src/renderer/gfx/scene.c
        src/renderer/gfx/schema.c
        src/renderer/gfx/shader.c
        src/renderer/gfx/sun_uv.c
        src/renderer/gfx/taa.c
        src/renderer/gfx/temporal_history.c
        src/renderer/gfx/texture.c
        src/renderer/gfx/update.c
        src/renderer/gfx/velocity.c
        src/renderer/gfx/viewport_grid.c
        src/renderer/background.c
        src/renderer/background/param.c
        src/renderer/background/schema.c
        src/renderer/background/imgpro.c
        src/renderer/background/transition.c
        src/renderer/background/tests.c
        #opencode: sky preset tests — headless pins (test-only code)
        src/renderer/sky_tests.c
        src/renderer/background/polisher.c
        src/renderer/postprocess.c
        src/renderer/postprocess/shader.c
        src/renderer/postprocess/schema.c
        src/renderer/raymarch.c
        src/renderer/raymarch/schema.c
        src/renderer/raymarch/schema2.c
        src/renderer/raymarch/tests.c
        src/renderer/postprocess/tests.c
        src/renderer/textwriter.c
        #opencode: textwriter parser tests — headless pins (test-only code)
        src/renderer/textwriter_tests.c
    )
endif()

#ai: engine-core archive boundary (docs/engine_library.md). The archive holds
#   the reusable engine sources; the executable keeps main/lifecycle, demo
#   scenes and authoring tools. This is a build boundary only: the engine still
#   owns the global maindata contract, so a game frontend must supply its host
#   state before linking the archive.
set(GX_APP_SOURCES ${GX_SOURCES})
list(FILTER GX_APP_SOURCES INCLUDE REGEX "^src/(main|tools|scene)")
set(GX_CORE_SOURCES ${GX_SOURCES})
list(FILTER GX_CORE_SOURCES EXCLUDE REGEX "^src/(main|tools|scene)")
add_library(gx_engine_core STATIC ${GX_CORE_SOURCES})

#ai: AngelScript scripting shell — built as the gx_shell dynamic module
#   (docs/dynamic_modules.md): the default exe stays lean; the shell links
#   against the exe's curated exports at dlopen (Route A). The SDK is the
#   vendored source subset in external/angelscript, compiled in-tree (the
#   upstream base source list; per-architecture files are preprocessor-guarded).
if(GX_USE_ANGELSCRIPT)
    set(GX_AS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/angelscript)
    set(GX_AS_SOURCES
        ${GX_AS_DIR}/source/as_atomic.cpp
        ${GX_AS_DIR}/source/as_builder.cpp
        ${GX_AS_DIR}/source/as_bytecode.cpp
        ${GX_AS_DIR}/source/as_callfunc.cpp
        ${GX_AS_DIR}/source/as_callfunc_mips.cpp
        ${GX_AS_DIR}/source/as_callfunc_x86.cpp
        ${GX_AS_DIR}/source/as_callfunc_x64_gcc.cpp
        ${GX_AS_DIR}/source/as_callfunc_x64_msvc.cpp
        ${GX_AS_DIR}/source/as_callfunc_x64_mingw.cpp
        ${GX_AS_DIR}/source/as_compiler.cpp
        ${GX_AS_DIR}/source/as_configgroup.cpp
        ${GX_AS_DIR}/source/as_context.cpp
        ${GX_AS_DIR}/source/as_datatype.cpp
        ${GX_AS_DIR}/source/as_gc.cpp
        ${GX_AS_DIR}/source/as_generic.cpp
        ${GX_AS_DIR}/source/as_globalproperty.cpp
        ${GX_AS_DIR}/source/as_memory.cpp
        ${GX_AS_DIR}/source/as_module.cpp
        ${GX_AS_DIR}/source/as_objecttype.cpp
        ${GX_AS_DIR}/source/as_outputbuffer.cpp
        ${GX_AS_DIR}/source/as_parser.cpp
        ${GX_AS_DIR}/source/as_restore.cpp
        ${GX_AS_DIR}/source/as_scriptcode.cpp
        ${GX_AS_DIR}/source/as_scriptengine.cpp
        ${GX_AS_DIR}/source/as_scriptfunction.cpp
        ${GX_AS_DIR}/source/as_scriptnode.cpp
        ${GX_AS_DIR}/source/as_scriptobject.cpp
        ${GX_AS_DIR}/source/as_string.cpp
        ${GX_AS_DIR}/source/as_string_util.cpp
        ${GX_AS_DIR}/source/as_thread.cpp
        ${GX_AS_DIR}/source/as_tokenizer.cpp
        ${GX_AS_DIR}/source/as_typeinfo.cpp
        ${GX_AS_DIR}/source/as_variablescope.cpp
        ${GX_AS_DIR}/add_on/scriptstdstring/scriptstdstring.cpp
        ${GX_AS_DIR}/add_on/scriptstdstring/scriptstdstring_utils.cpp
        ${GX_AS_DIR}/add_on/scriptarray/scriptarray.cpp
    )
    add_library(gx_shell SHARED
        ${GX_AS_SOURCES}
        src/module/gx_shell.c
        src/scripting.cpp
        src/scripting/script.cpp
        src/scripting/hooks.cpp
        src/scripting/api_core.cpp
        src/scripting/api_state.cpp
        #ai: debugger panel — chrome over the scripting debug API (tools/ImGui).
        src/tools/script/script_panel.cpp
        #opencode: camera rail editor — module-side panel over the shell rail
        #   (docs/editors.md placement: the rail is shell-owned).
        src/tools/tour/rail_editor.cpp
        #ai: console front-end — core + one-liner bridge + ImGui panel.
        src/console/console.c
        src/console/console_exec.cpp
        src/console/console_panel.cpp
        #ai: tour APIs — all landed (camera/gfx/audio/background/synth/
        #   opx/pad/textwriter/overlay/raymarch/pp); gX-native remakes of the
        #   reel surface.
        src/scripting/tour/tour_camera.cpp
        src/scripting/tour/tour_gfx.cpp
        src/scripting/tour/tour_audio.cpp
        src/scripting/tour/tour_background.cpp
        src/scripting/tour/tour_synth.cpp
        src/scripting/tour/tour_opx.cpp
        src/scripting/tour/tour_pad.cpp
        src/scripting/tour/tour_textwriter.cpp
        src/scripting/tour/tour_overlay.cpp
        src/scripting/tour/tour_raymarch.cpp
        src/scripting/tour/tour_pp.cpp
    )
    set_target_properties(gx_shell PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_compile_definitions(gx_shell PRIVATE GX_USE_ANGELSCRIPT)
    target_include_directories(gx_shell PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GX_AS_DIR}/include
        ${GX_AS_DIR}/source
        ${GX_AS_DIR}/add_on
        ${CMAKE_CURRENT_SOURCE_DIR}/external/raylib/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/raylib/src/external
        ${CMAKE_CURRENT_SOURCE_DIR}/external)
    #opencode: modules resolve engine symbols from the exe at dlopen
    #   (Route A) — no engine link, only libc/libm/libstdc++.
    target_link_libraries(gx_shell m pthread)

    #ai: the vendored AngelScript SDK is third-party source — keep gX's
    #   warning budget clean by compiling it without project warnings.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(${GX_AS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    message(STATUS "AngelScript: gx_shell module enabled (${GX_AS_DIR})")
endif()

#ai: devcube lab module — the engine avatar. Output stays under build/labs so
#   the host never auto-loads it; --module/GX_MODULE opt in explicitly.
if(GX_ENABLE_RENDERER)
    add_library(gx_devcube SHARED src/module/devcube.c)
    set_target_properties(gx_devcube PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/labs)
    target_include_directories(gx_devcube PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gx_devcube m)
endif()

add_executable(${PROJECT_NAME} ${GX_APP_SOURCES})
target_link_libraries(${PROJECT_NAME} gx_engine_core)

#opencode: the exe exports the curated module-facing symbol set (Route A); the
#   list is generated from the module undefined symbols (make module-exports)
#   and is the ABI — append-only within GX_MODULE_ABI_VERSION. Applies to the
#   default profile too: devcube is a lab module loaded without the shell.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/modules/module_exports.txt)
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/modules/module_exports.txt")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/module_exports.txt)
else()
    message(WARNING "modules/module_exports.txt missing — build the module-less exe only, or run 'make module-exports'")
endif()

#ai: party simulation, CPU raymarch, authoring, schema and shader parsing favor size;
# shader tracing stays on the GPU and the main GFX renderer remains O3.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        src/scene/battle_runtime.c src/renderer/raymarch.c src/gui/style.c
        src/tools/gfx/gfx_editor.cpp src/common/shader_compiler.c
        src/tools/gfx/water_panel.cpp src/tools/gfx/glass_panel.cpp
        src/renderer/gfx/schema.c
        #ai: physics demo cold paths — sandbox/ragdoll/EM/element code is not
        #   frame-critical math (the shared world module in src/physics/physics.c
        #   stays O3); this keeps the Release lean gate honest.
        src/scene/physics.c src/physics/ragdoll.c src/physics/em.c
        src/physics/relativity.c
        src/physics/elements.c src/physics/thermal.c src/physics/fluid.c
        #ai: battle presentation is a self-contained demo scene whose CPU
        #   runtime (battle_runtime.c above) is already size-favored; its HUD
        #   is 2D rects/text, so -Os costs no meaningful frame time.
        src/scene/battle.c
        #ai: cold UI/dispatch — the title menu builds per view change (not per
        #   frame), the scene dispatcher is a few branch flags, GUI event
        #   callbacks fire on input, the title scene is a fixed menu backdrop,
        #   the settings screen is a form rebuild, and the GUI-editor screen
        #   is a tool UI (like gfx_editor.cpp above); all favor size over
        #   speed.
        src/gui/templates/titlegx.c src/gui/templates/settings.c
        src/gui/templates/guieditor.c src/scene/scene.c
        src/gui/binding.c src/scene/title.c
        #ai: synth engine — note/event-rate parameter plumbing plus the audio
        #   mix loop, which only runs while a note sounds; size-favored keeps
        #   the lean gate honest without touching the hot renderer paths.
        src/audio/synth.c src/audio/synth_schema.c
        #ai: the procedural SFX pool (wave synthesis + voice pick) only runs
        #   on impact events; size-favored along with the rest of the audio
        #   off-frame paths. The preset schemas are cold too (file IO).
        src/audio/sfx.c
        src/audio/opx_schema.c src/audio/pad_schema.c
        #opencode: synthvis demo scene — keyboard polling + canvas panel layout
        #   (per-key note plumbing, no frame-critical math), and its keyboard
        #   template (24 rect elements rebuilt per frame); size-favored.
        src/scene/synthvis.c src/gui/templates/synthvis.c
        #opencode: synthvis scene tests — test-only code, size-favored.
        src/scene/synthvis_tests.c
        #opencode: textwriter parser tests — test-only code, size-favored.
        src/renderer/textwriter_tests.c
        #opencode: sky preset tests — test-only code, size-favored.
        src/renderer/sky_tests.c
        #opencode: dynamic module host — a few pointer checks + registration
        #   lookups per frame, off the frame-critical path; -Oz keeps the
        #   default profile's module capability cheap.
        src/module/module.c PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Release>:-Os>")

    #ai: the physics demo cold paths go further (-Oz): the sandbox scene plus
    #   ragdoll/EM/elements/thermal/fluid are all off the frame-critical path
    #   (the shared world module stays O3) and the lean gate is payload-bound;
    #   the fluid/water slice needed the extra size tier rather than a budget
    #   bump. Codegen size only — no behavior change.
    set_source_files_properties(
        src/scene/physics.c src/physics/ragdoll.c src/physics/em.c
        src/physics/relativity.c
        src/physics/elements.c src/physics/thermal.c src/physics/fluid.c
        #ai: the SI physics math (Coulomb/Lorentz/dipoles/Planck) is applier
        #   cold path too — only the sim hook calls it, never per-frame
        #   render code — so it joins the size tier.
        src/gxmath/physics.c
        #opencode: the synthvis demo scene is also off the frame-critical path
        #   (key polling + panel layout); the lean gate is payload-bound, so it
        #   joins the extra size tier rather than a budget bump.
        src/scene/synthvis.c src/gui/templates/synthvis.c
        #opencode: the wavetable editor is a tool UI (pointer-driven, opens on
        #   demand) — it takes the extra size tier like the physics cold set.
        src/tools/synth/wavetable_editor.cpp
        #opencode: the textwriter editor is the same class of tool UI.
        src/tools/textwriter/textwriter_editor.cpp
        #ai: the physics editor is the same class of tool UI.
        src/tools/physics/physics_editor.cpp
        #opencode: the shared background tuning body is the same class of UI.
        src/tools/background/bg_panel.cpp
        #opencode: the standalone background editor is the same class of UI.
        src/tools/background/background_editor.cpp
        #opencode: the sky editor is the same class of UI.
        src/tools/sky/sky_editor.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-Oz>")
endif()
#opencode: Debug builds export symbols so crash traces name functions.
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-rdynamic>)

#opencode: v0.4.0 — vendored rlImGui: skip the FontAwesome icon font (not
#   used by the tools; avoids shipping the icon glyph header + font data)
set_source_files_properties(src/tools/imgui/rlImGui.cpp PROPERTIES COMPILE_DEFINITIONS NO_FONT_AWESOME)

target_link_libraries(${PROJECT_NAME} m)

#ai: ODE link/include for the physics wrapper (see the GX_USE_ODE option).
if(GX_USE_ODE)
    target_include_directories(${PROJECT_NAME} PRIVATE ${GX_ODE_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} ${GX_ODE_LIBRARY})
endif()

# Define raylib backend (also keeps GAME_BACKEND_GX active to block gameX infra)
add_definitions(-DGAME_BACKEND_RAYLIB -DGAME_BACKEND_GX)
#opencode: enable gX's GPU-skinning plumbing (bone attribute upload/binding and
#   the per-model bone matrix upload in the raylib backend).
add_definitions(-DGX_GPU_SKINNING=1)
#opencode: release-lean — unit tests and harness-only code are development
#   features (make check runs them on the Debug build); tagging Release keeps
#   them out of the shipped payload.
if(CMAKE_BUILD_TYPE MATCHES "Release")
    add_definitions(-DGX_LEAN_RELEASE=1)
endif()

# raylib: build from bundled submodule (external/raylib)
set(GX_RAYLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/raylib)
add_subdirectory(${GX_RAYLIB_DIR} raylib_build EXCLUDE_FROM_ALL)
#opencode: raylib's UploadMesh gates the boneIds/boneWeights VBO upload on
#   SUPPORT_GPU_SKINNING (config.h default 0); without it skinned meshes have
#   no bone buffers and the skinned shaders read the generic defaults.
target_compile_definitions(raylib PRIVATE SUPPORT_GPU_SKINNING=1)
include_directories(${GX_RAYLIB_DIR}/src)
include_directories(${GX_RAYLIB_DIR}/src/external)  # for "glad.h" (RHI) + par_shapes.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external)  # for "raylib/src/external/par_shapes.h"
target_link_libraries(${PROJECT_NAME} raylib)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/raylib)
target_link_libraries(${PROJECT_NAME} m pthread X11 Xrandr Xinerama Xcursor Xi Xxf86vm GL dl)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

#ai: the archive compiles the same engine headers/definitions as the exe.
target_include_directories(gx_engine_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/raylib
    ${GX_RAYLIB_DIR}/src
    ${GX_RAYLIB_DIR}/src/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external)
target_compile_definitions(gx_engine_core PRIVATE
    GAME_BACKEND_RAYLIB
    GAME_BACKEND_GX
    GX_GPU_SKINNING=1
    "gX_ENABLE_RENDERER=$<BOOL:${GX_ENABLE_RENDERER}>"
    "gX_ENABLE_SCENE=$<BOOL:${GX_ENABLE_SCENE}>")
if(GX_USE_ODE)
    target_include_directories(gx_engine_core PRIVATE ${GX_ODE_INCLUDE_DIR})
    target_compile_definitions(gx_engine_core PRIVATE GX_USE_ODE)
endif()
if(CMAKE_BUILD_TYPE MATCHES "Release")
    target_compile_definitions(gx_engine_core PRIVATE GX_LEAN_RELEASE=1)
endif()

#ai: scripting shell include roots — the AngelScript public header, the
#   library's internal source headers (as_config.h etc.) and the add-on
#   headers (scriptstdstring/scriptarray).
if(GX_USE_ANGELSCRIPT)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${GX_AS_DIR}/include
        ${GX_AS_DIR}/source
        ${GX_AS_DIR}/add_on)
endif()
