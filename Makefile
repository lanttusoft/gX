# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# gX — Makefile (convenience wrapper around build.sh)
#   make          — build Debug
#   make check    — full CI: build → tests → scene smoke → lean gate
#   make check-kernel — kernel-only CI: cluster-off build → tests → boot
#   make proofs   — Lean specs (../proofs, blocking in check)
#   make proofs-full — Mathlib-tier gfx specs (opt-in, needs cache)
#   make reflect-check — GPU reflection fidelity gates (needs DISPLAY=:1)
#   make gfx-check — GPU feature matrix (needs DISPLAY=:1)
#   make gfx-scene-smoke — every standalone scene boots non-empty, no errors (needs DISPLAY=:1)
#   make bloom-audit — stage-by-stage bloom pipeline gate (needs DISPLAY=:1)
#   make gui-check — GUI template capture matrix (needs DISPLAY=:1)
#   make gui-interaction-check — scripted GUI interaction matrix (needs DISPLAY=:1)
#   make gui-theme-check — shipped GUI themes: load/contrast/pixels (needs DISPLAY=:1)
#   make gui-layout-check — text-fit layout pass over every template (needs DISPLAY=:1)
#   make gui-shadow-check — themed text shadows: bitmap/SDF/off/layout (needs DISPLAY=:1)
#   make gui-depth-check — SDF gradient modes + horizon/metallic profiles (needs DISPLAY=:1)
#   make gui-glass-check — Aero glass: blur/tint/rim + flat fallback (needs DISPLAY=:1)
#   make gui-cursor-light-check — cursor highlight follows/off/deterministic (needs DISPLAY=:1)
#   make gui-pause-blur-check — pause backdrop = current frame blur+dim (needs DISPLAY=:1)
#   make gui-effect-contrast-check — AA text over glass/flat panels (needs DISPLAY=:1)
#   make gui-benchmark — advisory GUI frame-cost artifact (needs DISPLAY=:1)
#   make gui-scoreboard — GUI coverage/correctness/visual/cost summary
#   make gfx-quick — five-row GPU feature smoke matrix
#   make check-fast — incremental build, unit/contracts, short scene smokes
#   make gfx-check-fast — alias for the curated fast feature matrix
#   make gfx-grid-check — opt-in GUI × GFX viewport-grid capture smoke
#   make release  — build Release

BUILD_TYPE ?= Debug
#opencode: gate parallelism for the audit summaries (2 = light on the desktop).
AUDIT_JOBS ?= 2

#ai: opt-in math soak size; admission remains at the script default (64).
MATH_SOAK_PROBES ?= 1024

#opencode: GPU audit targets run with an unmapped window (GX_AUDIT_WINDOW=hidden):
#   nothing appears on the desktop, no focus steal, no pointer grab, and the
#   captures are pixel-identical to visible runs. Override for interactive
#   debugging with `make GX_AUDIT_WINDOW=1 <gate>` (unfocused + off-screen) or
#   `make GX_AUDIT_WINDOW= <gate>` (normal window). Timing targets
#   (benchmarks, scoreboards, warmup) clear the variable themselves: skipping
#   presentation would skew measured wall cost.
export GX_AUDIT_WINDOW ?= hidden

.PHONY: all check check-fast check-kernel release clean lean reflect-check reflect-check-fast ssr-normal-check dither-check palette-check interlace-check dof-resolution-check snow-check snowfall-check rain-check fogsprite-check probe-check splat-check mirror-check materials-check gfx-check gfx-check-fast gfx-scene-smoke gui-check gui-check-fast gui-scoreboard gui-visual-review gui-interaction-check gui-theme-check gui-layout-check gui-shadow-check gui-depth-check gui-glass-check gui-cursor-light-check gui-pause-blur-check gui-effect-contrast-check gui-benchmark gui-benchmark-check gfx-benchmark gfx-benchmark-check gfx-benchmark-release gfx-benchmark-reference-check raymarch-benchmark raymarch-benchmark-check gfx-visual-review gfx-patchwork-check gfx-quick gfx-grid-check gfx-snapshot-check gfx-picker-check gfx-light-picker-check gfx-motion-path-check dock-layout-check gfx-label-font-check gfx-rig-inspector-check gfx-animation-check bloom-audit bloom-audit-quick audit-summary audit-summary-full gfx-preset-overwrite-check gfx-art-picker-check gfx-art-blending-check gfx-mirror-alignment-check gfx-reflection-ui-check gfx-material-asset-check gfx-asset-roots-check gfx-bijection-check gfx-skin-bones-check gfx-camera-render-check provenance-check gfx-gl-level-ui-check gfx-prefab-check gfx-aero-check gfx-primitive-check gfx-texture-check wavetable-editor-check textwriter-editor-check textwriter-render-check sky-render-check analog-reduced-check colour-independence-check physics-editor-check gfx-export-check gfx-battle-check gfx-background-check gfx-pp-check gfx-raymarch-check gfx-raymarch-features-check error-handling-check error-check gfx-profile-check debug-check cache-check gfx-decal-bake-check version-check version-gate gfx-planar-material-check gfx-light-check gfx-art-light-check gfx-art-reflection-check gfx-shadow-check gfx-light-shadow-check material-maps-check lightmap-check lightmap-visual-review temporal-check temporal-check-fast warmup-check water-check horizon-check scoreboard fog-light-check wireframe-check particle-velocity-check compute-feature-check gl-level-check glsl130-check gl-legacy-native-check oracle-path-check oracle-bounce-check oracle-hdr-check oracle-gpu-check oracle-gpu-multibounce-check oracle-gpu-reflection-check gl-audit math-check proofs proofs-full hw-report-check sense-check

all:
	bash build.sh $(BUILD_TYPE)

check: all
	@if [ ! -d ../proofs ]; then \
	  echo "gX public source beta: full 'make check' needs the proof tree (../proofs)."; \
	  echo "Use 'make check-fast' for the public source-beta CI."; \
	  exit 2; \
	fi
	#ai: cookie image formats must match RGBA client uploads and stay within atlas cells.
	python3 scripts/test_gfx_cookie_upload.py
	#ai: pass identities, unchanged draw order, complete GPU query sets and strict source-frame windows.
	python3 scripts/test_gfx_profile.py
	#ai: startup save-guard probes must preserve the exact inherited environment, including allocation-failure controls.
	python3 scripts/test_harness_guard_lifetime.py
	#ai: heap textures must receive initial and deferred sampler policy.
	python3 -m unittest discover -s scripts -p test_gfx_texture_filter_lifecycle.py
	#ai: owned fixed-camera reflection fixture geometry and diagnostic negative controls.
	python3 scripts/test_ssr_gap_check.py
	#ai: optional shading-normal precision must preserve zero components and occlusion controls.
	python3 scripts/test_ssr_normal_precision.py
	#ai: color-filter support must not be mistaken for point-ray visibility.
	python3 scripts/test_ssr_boundary_footprint.py
	@echo "== [gX CI] unit tests =="
#opencode: emit full regression diagnostics and require the exact zero-error marker.
	@status=0; ./build/gX_build --test-only > $${GX_SCRATCH:-/tmp/opencode}/gx_test_only.log 2>&1 || status=$$?; \
	  cat $${GX_SCRATCH:-/tmp/opencode}/gx_test_only.log; \
	  grep -q "TEST_ONLY: 0 errors" $${GX_SCRATCH:-/tmp/opencode}/gx_test_only.log && test $$status -eq 0
	#ai: native allocation rollback and bounded file-backed JSON admission controls.
	$(MAKE) -s runtime-input-check
	#ai: archive policy preview and isolated tagged-export negative controls.
	$(MAKE) -s packaging-export-check
	#ai: clean-checkout default dependencies and strict bloom-intensity metric negatives.
	$(MAKE) -s clean-defaults-check
	@echo "== [gX CI] static-lightmap contract =="
	python3 scripts/lightmap_check.py
	#ai: ambient-only Phong shading must preserve albedo texture variation.
	python3 scripts/test_phong_ambient_albedo.py
	#ai: uncached file materials must not inherit another material's emission.
	python3 scripts/test_emissive_material_fallback.py
	#ai: full-source alpha routes preserve MASK/BLEND isolation beyond the metadata cache.
	python3 scripts/test_alpha_material_fallback.py
	#ai: tone-map and gamma controls disclose their HDR-composite dependency.
	python3 scripts/test_gfx_tonemap_controls.py
	#ai: conservative mesh bounds and per-model cache lifetime (owned native controls).
	python3 scripts/gfx_mesh_culling_check.py --native-only
	@echo "== [gX CI] scene smoke (title + gfx + y2k, 60 frames each) =="
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=60 GX_NO_SETTINGS_SAVE=1 timeout 60 ./build/gX_build --screenWidth 1024 --screenHeight 768 --title >/dev/null 2>&1
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=60 GX_NO_SETTINGS_SAVE=1 timeout 60 ./build/gX_build --screenWidth 1024 --screenHeight 768 --gfx >/dev/null 2>&1
	#opencode: opt-in Y2K showcase smoke — crash-free only, defaults unchanged.
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=60 GX_NO_SETTINGS_SAVE=1 timeout 60 ./build/gX_build --screenWidth 1024 --screenHeight 768 --showcase-y2k >/dev/null 2>&1
	@echo "== [gX CI] resource + copyright audit =="
	bash scripts/audit_resources.sh
	python3 scripts/script_license_audit.py --strict
	$(MAKE) -s asset-clearance-check
	@echo "== [gX CI] hardware report =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/hw_report_check.py
	@echo "== [gX CI] error-handling audit =="
	python3 scripts/error_handling_audit.py --strict
	@echo "== [gX CI] common-sense guard =="
	python3 scripts/common_sense_check.py
	@echo "== [gX CI] lean gate (Release payload + bss) =="
	$(MAKE) -s lean
	@echo "== [gX CI] math pass (gxmath corpus vs SymPy oracle) =="
	python3 -m unittest discover -s scripts -p test_math_pass.py
	python3 scripts/math_pass.py
	#ai: seeded randomized differential probes across algebraic, SDF, physics and conversion families.
	python3 -m unittest discover -s scripts -p test_math_random_check.py
	python3 scripts/math_random_check.py
	#ai: invariant checks that do not reuse the differential formula oracle.
	python3 -m unittest discover -s scripts -p test_math_property_check.py
	python3 scripts/math_property_check.py
	#ai: exact stored-float residual bounds with separate measured CPU uncertainty.
	$(MAKE) -s reprojection-residual-check
	#ai: depth/UV/jitter input sensitivity with conditional simulated quantization.
	$(MAKE) -s depth-input-check
	#ai: source-grounded planar camera/UV geometry and asymmetric convention witnesses.
	$(MAKE) -s planar-geometry-check
	#ai: native reflected vs rebuilt camera orientation and conditional winding witnesses.
	$(MAKE) -s reflection-handedness-check
	#ai: near clipping before perspective divide and mixed-w winding negative controls.
	$(MAKE) -s projection-clipping-check
	#ai: canonical cubemap geometry and asymmetric negative-X correspondence.
	$(MAKE) -s cubemap-geometry-check
	#ai: radial point-shadow depth, strict comparisons and error-margin witnesses.
	$(MAKE) -s point-shadow-depth-check
	#ai: source-order cascade boundaries and conditional fade/projection witnesses.
	$(MAKE) -s csm-contract-check
	#ai: source-grounded GTAO native/scalar proof witnesses.
	$(MAKE) -s gtao-contract-check
	#ai: SSGI normalized fallback and tangent predicate correspondence.
	$(MAKE) -s ssgi-contract-check
	$(MAKE) -s postprocess-contract-check
	#ai: offline editor reports write only on explicit submit; preserve user files.
	$(MAKE) -s gfx-bug-report-check
	@echo "== [gX CI] proofs (Lean specs, ../proofs) =="
	$(MAKE) -s proofs
	@echo "== [gX CI] OK =="

#ai: every project script carries SPDX/copyright metadata.
.PHONY: script-license-check
script-license-check:
	python3 scripts/script_license_audit.py --strict

#ai: isolated runtime hardening regressions; no display or engine launch needed.
.PHONY: runtime-input-check
runtime-input-check:
	python3 scripts/runtime_input_check.py

#ai: source dependency contracts and headless spatial/brightness/boundedness rejection controls.
.PHONY: clean-defaults-check
clean-defaults-check:
	python3 scripts/clean_defaults_check.py
	python3 scripts/test_bloom_intensity.py

#ai: hardware capability report gate — GL strings/flags/limits + tier
#   classification (cross-checked against the same rules) + the headless
#   classifier unit tests.
hw-report-check:
	@echo "== [gX CI] hardware report =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/hw_report_check.py

#opencode: fast inner-loop counterpart to check; preserves the full check
#   target and its Release-size, resource/copyright, and Lean admission gates.
check-fast: all
	@echo "== [gX fast] unit tests =="
	./build/gX_build --test-only 2>&1 | grep -E "TEST_ONLY" || (./build/gX_build --test-only && exit 1)
	@echo "== [gX fast] attribution =="
	python3 scripts/attribution_audit.py --strict
	python3 scripts/script_license_audit.py --strict
	@echo "== [gX fast] static-lightmap contract =="
	python3 scripts/lightmap_check.py
	@echo "== [gX fast] short scene smoke (title + gfx + y2k, 24 frames each) =="
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=24 GX_NO_SETTINGS_SAVE=1 timeout 45 ./build/gX_build --screenWidth 1024 --screenHeight 768 --title >/dev/null 2>&1
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=24 GX_NO_SETTINGS_SAVE=1 timeout 45 ./build/gX_build --screenWidth 1024 --screenHeight 768 --gfx >/dev/null 2>&1
	DISPLAY=$${DISPLAY:-:1} GX_FRAMES=24 GX_NO_SETTINGS_SAVE=1 timeout 45 ./build/gX_build --screenWidth 1024 --screenHeight 768 --showcase-y2k >/dev/null 2>&1
	@echo "== [gX fast] math pass (gxmath corpus vs SymPy oracle) =="
	python3 -m unittest discover -s scripts -p test_math_pass.py
	python3 scripts/math_pass.py
	python3 -m unittest discover -s scripts -p test_math_random_check.py
	python3 scripts/math_random_check.py
	python3 -m unittest discover -s scripts -p test_math_property_check.py
	python3 scripts/math_property_check.py
	@echo "== [gX fast] devcube lab module =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_devcube_check.py
	@echo "== [gX fast] OK =="

#ai: devcube lab-module gate — 60 Hz phase plus the explicit native overlay.
.PHONY: devcube-check
devcube-check: all
	@echo "== [gX] devcube lab module =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_devcube_check.py

#ai: module-owned frontend probe — build the game-side level module, then run
#   the real engine host with the generic MODULE scene. Opt-in (the game target
#   is not part of the engine release payload).
.PHONY: level-module-check
level-module-check: all
	@echo "== [gX] module-owned level scene probe =="
	cmake -S ../game -B ../game/build
	cmake --build ../game/build --target gx_level -j$$(nproc)
	python3 scripts/level_module_check.py build/gX_build \
		build/labs/gx_level.so ../game/tests/fixtures/level.json

#ai: top-down editor model/C-validator gate (no display or Tk needed).
.PHONY: level-editor-check
level-editor-check:
	@echo "== [gX] game level editor checks =="
	cmake -S ../game -B ../game/build
	cmake --build ../game/build --target gx_book -j$$(nproc)
	ctest --test-dir ../game/build --output-on-failure \
		-R 'level-editor-model|level-check|level-invalid'
	GX_BOOK_BIN=../game/build/gx_book python3 ../game/editor/level_editor.py \
		--check ../game/tests/fixtures/level.json

#ai: dialogue graph/state editor model and start-node gate (no display/Tk).
.PHONY: dialogue-editor-check
dialogue-editor-check:
	@echo "== [gX] game dialogue editor checks =="
	cmake -S ../game -B ../game/build
	cmake --build ../game/build --target gx_book -j$$(nproc)
	ctest --test-dir ../game/build --output-on-failure \
		-R 'dialogue-editor-model|start-node|content-check|content-invalid'
	GX_BOOK_BIN=../game/build/gx_book python3 ../game/editor/dialogue_editor.py \
		--check ../game/data/dialogue/chapter1.json

#opencode: kernel-only CI — renderer/scene sources out (atomic pair),
#   then unit tests + a short NULL boot (exit 0, clean unload). Separate
#   from check (own build dir, own cost); run before tagging kernel work.
#   The boot step needs a display (run under Xvfb headless).
check-kernel:
	cmake -B build/kernel -DGX_ENABLE_RENDERER=OFF -DCMAKE_BUILD_TYPE=Debug -DGX_COMPUTE=OFF
	cmake --build build/kernel -j$$(nproc)
	@echo "== [gX kernel] unit tests =="
	./build/kernel/gX_build --test-only 2>&1 | grep -E "TEST_ONLY" || (./build/kernel/gX_build --test-only && exit 1)
	@echo "== [gX kernel] NULL boot (30 frames, clean unload) =="
	GX_FRAMES=30 GX_NO_SETTINGS_SAVE=1 ./build/kernel/gX_build --ai-screenshot-final >/dev/null 2>&1
	@echo "== [gX kernel] OK =="

#opencode: scripting shell gate — the shell builds as the gx_shell dynamic
#   module now (docs/dynamic_modules.md); probes load it through GX_MODULE and
#   cover load/execute, Sleep, asserts + exit codes, hooks, hot-reload,
#   scan modes, DOM click, debugger, state, console, the tour APIs and markup.
#   See docs/scripting.md and scripts/scripting_check.py.
.PHONY: check-scripting
check-scripting:
	cmake -B build/scripting -DGX_USE_ANGELSCRIPT=ON -DCMAKE_BUILD_TYPE=Debug
	cmake --build build/scripting -j$$(nproc)
	@echo "== [gX scripting] host + shell module probes =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/scripting_check.py
	@echo "== [gX scripting] OK =="

#ai: dynamic module exports — regenerate the Route A ABI surface from the
#   gx_shell module's undefined symbols (Release profile) and relink the exe
#   with the refreshed dynamic list (make check-scripting pins drift).
.PHONY: module-exports
module-exports:
	cmake -B build/modules -DGX_USE_ANGELSCRIPT=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build build/modules -j$$(nproc)
	python3 scripts/module_exports.py build/modules/CMakeFiles/gX_build.dir \
		build/modules/gx_shell.so build/modules/labs/gx_devcube.so \
		modules/module_exports.txt
	cmake -B build/modules -DGX_USE_ANGELSCRIPT=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build build/modules -j$$(nproc)

#opencode: v0.1.36 — reflection fidelity gates (Phase C). Requires a live
#   GPU display (DISPLAY=:1); NOT part of headless `make check`.
reflect-check: all
	@echo "== [gX CI] reflection suite (SSR + planar + cubemap gates) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/reflect_suite.py

#opencode: one-angle SSR/planar smoke; full reflection admission remains
#   `reflect-check` and still runs every angle and cubemap/consumer audit.
reflect-check-fast: all
	@echo "== [gX fast] reflection smoke (one SSR + planar angle) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/reflect_suite.py --quick

#opencode: opt-in SSR normal-buffer encoding/coverage and A/B diagnostic lane.
#   It never changes the production normals-off default or reflection grades.
ssr-normal-check: all
	@echo "== [gX review] SSR normal-buffer diagnostics =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/ssr_normal_check.py

#opencode: opt-in deterministic dither mode check. Production's legacy IGN
#   dither remains the default; this target exercises off/Bayer/noise modes.
dither-check: all
	@echo "== [gX review] deterministic dither modes =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/dither_check.py

#opencode: opt-in 16/256-color post-tonemap quantizer check. Dither is
#   explicitly toggled to verify it remains independent from palette selection.
palette-check: all
	@echo "== [gX review] deterministic palette modes =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/palette_check.py

#opencode: opt-in horizontal/vertical/dot interlace check with deterministic
#   phase, strength, cadence, and repeatability assertions.
interlace-check: all
	@echo "== [gX review] deterministic interlace modes =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/interlace_check.py

#opencode: opt-in full/half/quarter DOF focus-rack and repeatability check.
dof-resolution-check: all
	@echo "== [gX review] reduced-resolution DOF =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/dof_resolution_check.py

#opencode: opt-in bounded snow decals: coverage, melt, height, slope, distance.
snow-check: all
	@echo "== [gX review] deterministic snow fixture =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/snow_check.py

#opencode: opt-in CPU falling snow: seed, rate, wind, distance, accumulation.
snowfall-check: all
	@echo "== [gX review] deterministic CPU snowfall =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/snowfall_check.py

#opencode: opt-in CPU rain streaks: seed, rate, wind, distance.
rain-check: all
	@echo "== [gX review] deterministic CPU rain =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/rain_check.py

#opencode: opt-in CPU fog sprites: seed, count, wind, opacity, distance.
fogsprite-check: all
	@echo "== [gX review] deterministic CPU fog sprites =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/fogsprite_check.py

#opencode: opt-in reflection-map probe grid: bake, blend, refresh cadence.
probe-check: all
	@echo "== [gX review] deterministic probe grid =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/probe_check.py

#opencode: opt-in gaussian splat cloud: seed, count, distance LOD.
splat-check: all
	@echo "== [gX review] deterministic splat cloud =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/splat_check.py

#opencode: material override sidecar: response, sidecar-off A/B, repeat.
materials-check: all
	@echo "== [gX review] deterministic material overrides =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/materials_check.py

#opencode: opt-in two-mirror feedback: settle, buildup, churn, repeat.
mirror-check: all
	@echo "== [gX review] deterministic mirror pair =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/mirror_check.py

#opencode: GFX effect verification matrix. Requires a live GPU display and is
#   intentionally separate from headless `make check` because it captures each
#   isolated effect at the showcase resolution.
gfx-check: all
	python3 scripts/test_gfx_feature_matrix.py
	@echo "== [gX CI] GFX feature matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_feature_matrix.py

#opencode: scene boot smoke — every standalone scene flag boots, renders a
#   non-empty frame and logs no ERROR!/FATAL! lines. The deep per-feature
#   gates own the pipeline; this is the boot floor for the secondary demos.
gfx-scene-smoke: all
	@echo "== [gX CI] GFX scene smoke =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_scene_smoke.py

#opencode: GUI counterpart to gfx-check. The script pairs a title baseline
#   against every GUI template and fails unexpected blank/render states.
#   GPU captures remain separate from headless make check.
gui-check: all
	@echo "== [gX CI] GUI template matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_audit.py

#opencode: fast representative GUI matrix for the common menu, settings, and
#   widget surfaces; the full gui-check still covers every registered template.
gui-check-fast: all
	@echo "== [gX fast] GUI template smoke matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_audit.py --quick

#opencode: advisory GUI visual-review contact sheet from the most recent
#   gui-check capture matrix; optional grades feed the GUI scoreboard.
gui-visual-review:
	@echo "== [gX review] GUI template contact sheet =="
	python3 scripts/gui_visual_review.py

#opencode: GUI analogue of the GFX scoreboard. Full run combines headless
#   assertions, the template capture matrix, visual grades, and median timing.
gui-scoreboard: all
	@echo "== [gX CI] GUI system scoreboard =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_scoreboard.py

#opencode: scripted GUI interaction gate — drives real canvas paths through
#   GX_AI_INPUT and asserts the GX_GUI_TRACE readout (clicks, title views,
#   modal/dialog scope, widget rebuilds).
gui-interaction-check: all
	@echo "== [gX CI] GUI interaction matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_interaction_check.py

#opencode: layout pass — every template's text must fit its element box
#   (draw-truth bounds from the GX_GUI_TRACE element map). Intentional
#   oversized demos are allowlisted inside the script with a rationale.
gui-layout-check: all
	@echo "== [gX CI] GUI layout pass (text fits its box) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_layout_check.py

#opencode: text-shadow gate — themed tokens reach bitmap + SDF drawing,
#   zeroed tokens disable cleanly, and text still fits with shadows forced.
gui-shadow-check: all
	@echo "== [gX CI] GUI text shadows =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_shadow_check.py

#opencode: gradient-depth gate — authored gradients survive the SDF path and
#   the horizon/metallic profiles land at their token positions.
gui-depth-check: all
	@echo "== [gX CI] GUI gradient depth =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_depth_check.py

#opencode: Aero-glass gate — backdrop blur, tint, rim, the GX_GUI_GLASS=0
#   flat fallback, and determinism.
gui-glass-check: all
	@echo "== [gX CI] GUI Aero glass =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_glass_check.py

#ai: layout-dump CLI contract; data source for the deferred UI diff/layout tools.
.PHONY: layout-dump-check
layout-dump-check: all
	@echo "== [gX CI] structured layout dump =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/layout_dump_check.py

#ai: textual layout-dump diff; no display or scene capture.
.PHONY: ui-diff-check
ui-diff-check:
	python3 -m unittest discover -s scripts -p test_ui_diff.py

#ai: HUD-over-scene contrast probe: unit math + live title/battle captures.
.PHONY: gui-hud-contrast-check
gui-hud-contrast-check:
	python3 -m unittest discover -s scripts -p test_gui_hud_contrast.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_hud_contrast_check.py \
		--baseline scripts/gui_hud_contrast_baseline.json

#ai: shipped-theme state/surface contrast matrix (known failures baselined).
.PHONY: gui-state-contrast-check
gui-state-contrast-check:
	python3 scripts/gui_state_contrast_check.py --baseline scripts/gui_state_contrast_baseline.json

#ai: layout report/resolution analysis tests (synthetic dumps; no scene launch).
.PHONY: layout-report-check layout-sweep-check
layout-report-check:
	python3 -m unittest discover -s scripts -p test_layout_report.py
layout-sweep-check:
	python3 -m unittest discover -s scripts -p test_layout_sweep.py

#ai: generic front-end review tools — no display/scene/AI probe required.
.PHONY: frontend-image-check frontend-static-check
frontend-image-check:
	python3 -m unittest discover -s scripts -p test_image_tools.py
frontend-static-check:
	mkdir -p /tmp/opencode
	python3 scripts/color_inventory.py --json /tmp/opencode/frontend_colors.json
	python3 scripts/style_lint.py --json /tmp/opencode/frontend_style.json
	python3 scripts/font_inventory.py --json /tmp/opencode/frontend_fonts.json
	python3 scripts/font_preview_sheet.py --out /tmp/opencode/frontend_fonts.png

#opencode: cursor-light gate — the SDF highlight follows the synthesized
#   cursor, strength 0 stays off, and repeat runs are deterministic.
gui-cursor-light-check: all
	@echo "== [gX CI] GUI cursor light =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_cursor_light_check.py

#opencode: pause-blur gate — the EscapeMenu backdrop over a renderer scene
#   must match the current frame's blur+dim model (stale ping and an unloaded
#   blur program both fail).
gui-pause-blur-check: all
	@echo "== [gX CI] GUI pause blur =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_pause_blur_check.py

#opencode: effect-contrast gate — text over the production glass panel (Aero
#   pause menu) and the default flat panel must keep WCAG 2.2 AA, with the
#   cursor light on and off.
gui-effect-contrast-check: all
	@echo "== [gX CI] GUI effect contrast =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_effect_contrast_check.py

#opencode: shipped-theme gate — every theme file must load through the
#   GX_GUI_THEME hook, keep its WCAG 2.2 AA On- pairs, and render; the
#   high-contrast theme is proven to change pixels against the default.
gui-theme-check: all
	@echo "== [gX CI] GUI shipped themes =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_theme_check.py

#opencode: advisory GUI template run-cost benchmark artifact (median of fixed samples).
gui-benchmark: all
	@echo "== [gX review] GUI benchmark =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_benchmark.py

#opencode: portable GUI benchmark artifact check — rows/samples/medians are
#   validated; host-specific timing budgets stay out of the gate.
gui-benchmark-check: all
	@echo "== [gX CI] GUI benchmark artifacts =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gui_benchmark.py --check

#opencode: deterministic 1024x768@60 CPU/GPU benchmark report. Advisory until
#   a machine/driver baseline is pinned; does not enter make check.
gfx-benchmark: all
	@echo "== [gX review] deterministic GFX benchmark =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_benchmark.py

#ai: deterministic raymarch cost report — CPU frame time (engine CSV) plus the
#   module's own GPU timers (--raymarch-profile) across render-scale quality
#   rows and one-feature-off rows. Advisory; no host thresholds.
raymarch-benchmark: all
	@echo "== [gX review] deterministic raymarch benchmark =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/raymarch_benchmark.py

#ai: raymarch benchmark artifact check — row/sample completeness, positive
#   timings, and the resolution-scaling trend (0.25 -> 1.00).
raymarch-benchmark-check: all
	@echo "== [gX CI] raymarch benchmark artifact =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/raymarch_benchmark.py --check

#opencode: benchmark artifact completeness check. It validates that both CPU
#   and GPU timing streams were captured, but does not impose a host-specific
#   performance threshold yet.
gfx-benchmark-check: all
	@echo "== [gX CI] deterministic GFX benchmark artifacts =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_benchmark.py --check

#opencode: Release-build benchmark for pinning a machine/driver baseline. This
#   remains advisory and writes outside the repository by default.
gfx-benchmark-release: release
	@echo "== [gX review] Release GFX benchmark =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_benchmark.py --binary build/release/gX_build --output /tmp/opencode/gfx_benchmark_release

#opencode: opt-in host-pinned Release benchmark gate. It is separate from the
#   portable artifact check because the budgets describe one GPU/driver pair.
gfx-benchmark-reference-check: release
	@echo "== [gX review] pinned Release GFX benchmark =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_benchmark.py --binary build/release/gX_build --output /tmp/opencode/gfx_benchmark_reference_check --reference docs/gfx_benchmark_reference.json --check

#opencode: advisory visual-review contact sheets from the latest fixed captures.
#   Run after gfx-check; this target never gates CI and does not recapture.
gfx-visual-review:
	@echo "== [gX review] GFX visual contact sheets =="
	python3 scripts/gfx_visual_review.py

#ai: small-resolution patchwork A/B; fast visual and limit scans.
.PHONY: gfx-patchwork-check
gfx-patchwork-check: all
	@echo "== [gX CI] graphics patchwork =="
	python3 -m unittest discover -s scripts -p test_gfx_patchwork_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_patchwork_check.py --scene showcase \
		--size 320x320 --crop-size 96 --scale 4 --cols 3 --env GX_T_HUD=0 \
		--variant baseline: --variant no-bloom:GX_T_BLOOM=0 \
		--variant no-ssao:GX_T_SSAO=0 --require-change --min-delta 0.1

#opencode: short graphics smoke matrix; the full gfx-check remains unchanged.
gfx-quick: all
	python3 scripts/test_gfx_feature_matrix.py
	@echo "== [gX CI] GFX quick feature matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_feature_matrix.py --quick

#opencode: explicit fast-mode name for the established five-row matrix.
gfx-check-fast: gfx-quick

#opencode: opt-in phase-1 GUI × GFX dashboard pin. Captures the seeded grid
#   twice and validates its four view tiles plus bounded frame-to-frame drift.
gfx-grid-check: all
	@echo "== [gX CI] GUI × GFX viewport-grid smoke =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_grid_check.py

#opencode: save/load/apply persistence pin for reusable material surface assets.
gfx-material-asset-check: all
	@echo "== [gX CI] reusable GFX material asset round-trip =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_material_asset_check.py

#opencode: opt-in live-only editor snapshot round-trip; it mutates and restores
#   one frame of model/camera/renderer state and never writes a preset.
gfx-snapshot-check: all
	@echo "== [gX CI] GFX editor snapshot smoke =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_snapshot_check.py

#opencode: deterministic viewport picking and Blender-style transform gestures
#   through the existing GX_AI_INPUT virtual-input + ImGui bridge.
gfx-picker-check: all
	@echo "== [gX CI] GFX editor viewport picker =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_picker_check.py

#opencode: click and drag the original-art light markers through GX_AI_INPUT.
gfx-light-picker-check: all
	@echo "== [gX CI] GFX editor light marker picking and drag =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_light_picker_check.py
#opencode: docked-editor layout pin — the scene must fit between the scene
#   panel and the inspector (letterboxed), not render behind the panel.
dock-layout-check: all
	@echo "== [gX CI] dock layout pin (scene fits the visible area) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_dock_layout_check.py
#opencode: scene-export pin (OBJ counts + baked fixture placement).
gfx-export-check: all
	@echo "== [gX CI] scene export =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_export_check.py

#opencode: decal-bake pin (top-down albedo + ORM, channels exact).
gfx-decal-bake-check: all
	@echo "== [gX CI] decal bake =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_decal_bake_check.py

#opencode: audit-cache pin (clean run, cached repeat, --no-cache re-run).
cache-check: all
	@echo "== [gX CI] audit cache =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/audit_cache_check.py

#opencode: drop the cached gate results (the next summary re-runs everything).
audit-cache-clear:
	python3 scripts/audit_cache.py --clear

#ai: declared-vs-defined public API baseline (AUD-03). Default fails only on
#    new declarations; --strict asks for the full known list to be clean.
.PHONY: api-definition-check
api-definition-check:
	python3 -m unittest discover -s scripts -p test_api_definition_check.py
	python3 scripts/api_definition_check.py

#opencode: debugger pin (crash registers/stack + variable watches).
debug-check: all
	@echo "== [gX CI] debugger traces =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/debug_check.py

#opencode: profiler pin (phases + zones, frame budget sanity).
gfx-profile-check: all
	@echo "== [gX CI] profiler =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/profile_check.py

#opencode: static silent-failure scan (allowlist lives in the script).
error-handling-check:
	python3 scripts/error_handling_audit.py --strict

#ai: common-sense regression guard — new declared-but-undefined functions,
#   unused GX_ defines, missing doc paths or parsed-but-unused arguments
#   fields fail; accepted findings live in .sense_baseline.json (--update
#   rewrites it deliberately).
sense-check:
	@echo "== [gX CI] common-sense guard =="
	python3 scripts/common_sense_check.py

#opencode: bad-input error-path pin (missing + corrupt preset).
error-check: all
	@echo "== [gX CI] error paths =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/error_check.py

#opencode: raymarch shader-feature pin (matrix + defines + gfx integration).
gfx-raymarch-features-check: all
	@echo "== [gX CI] raymarch shader features =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_features.py

#ai: battle scene pin — the native 2D presentation over the BATTLE palette
#   backdrop: preset marker, non-empty frame, HUD header band, live palette
#   override, animation.
gfx-battle-check: all
	@echo "== [gX CI] battle scene =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_battle_check.py

#ai: background polisher pin — runtime defaults drive the backdrop (A/B),
#   the explicit default renders identically, and the panel/tests pass.
gfx-background-check: all
	@echo "== [gX CI] background polisher =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_background_check.py

#ai: postprocess chain pin — --pp renders, presets change the frame (A/B),
#   bad shaders report the error code, and the editor panel draws.
#ai: asymmetric native-size routing/empty-chain GPU witnesses accompany the scene gate.
gfx-pp-check: all
	@echo "== [gX CI] postprocess chain =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_pp_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/pp_routing_check.py

#ai: label placement/history, water height preservation and production ImGui ID/edit isolation.
.PHONY: gfx-authoring-check lensflare-check

#ai: half-resolution godray source must not replay stale SSR mip levels.
.PHONY: godray-check
godray-check:
	DISPLAY=$${DISPLAY:-:1} python3 scripts/godray_freshness_check.py

#ai: native camera-follow/wrap regression before the existing weather pixel gates.
.PHONY: weather-follow-check
weather-follow-check: all
	python3 scripts/weather_godray_contract_check.py
	python3 scripts/weather_follow_check.py
	python3 scripts/weather_vertical_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/weather_follow_gpu_check.py --vertical

#ai: production GLSL shape regression on asymmetric synthetic inputs (PyOpenGL/libglfw).
lensflare-check:
	DISPLAY=$${DISPLAY:-:1} python3 scripts/lensflare_shape_check.py

gfx-authoring-check: all
	python3 scripts/label_anchor_check.py
	python3 scripts/label_top_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_authoring_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/label_style_check.py

#opencode: raymarch revival pin (preset schema round-trip + scene frame).
#ai: focused hybrid visibility oracle retains raw captures, logs and reproduction commands.
.PHONY: gfx-raymarch-depth-check
gfx-raymarch-depth-check: all
	python3 scripts/test_gfx_raymarch_depth_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_depth_check.py

#ai: exported HDR depth oracle is separate from post-effects visibility.
.PHONY: gfx-raymarch-shared-depth-check
gfx-raymarch-shared-depth-check: all
	python3 scripts/test_gfx_raymarch_depth_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_depth_check.py --shared

#ai: primary SDF normals and authored roughness in the shared GFX buffers.
.PHONY: gfx-raymarch-surface-check
gfx-raymarch-surface-check: all
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_surface_check.py

#ai: attributed hybrid SSR hits/misses, distinct from surface-buffer wiring.
.PHONY: gfx-raymarch-reflection-check
gfx-raymarch-reflection-check: all
	python3 scripts/test_gfx_raymarch_reflection_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_reflection_check.py

gfx-raymarch-check: all
	@echo "== [gX CI] raymarch preset schema + scene =="
	python3 scripts/test_gfx_raymarch_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_raymarch_check.py

#opencode: texture-utility pin (generate + bake + ORM pack, channel exact).
gfx-texture-check: all
	@echo "== [gX CI] texture utilities =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_texture_check.py

#opencode: wavetable editor — both open paths (GX_TOOLS_OPEN + CLI flag)
#   render and exit clean; the save/load + harness-guard data path runs in
#   --test-only (synth unit tests).
wavetable-editor-check: all
	@echo "== [gX CI] wavetable editor open paths =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/wavetable_editor_check.py

#opencode: textwriter editor — both open paths render and exit clean, and the
#   CLI path routes to the auto-loading textwriter scene; the save/load +
#   harness-guard data path runs in --test-only (textwriter parser tests).
textwriter-editor-check: all
	@echo "== [gX CI] textwriter editor open paths =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/textwriter_editor_check.py

#ai: renderer-level textwriter tag/reveal/choice matrix; synthetic tests plus
#   fixed GPU captures. Parser/editor gates remain the data-path authorities.
textwriter-render-check: all
	@echo "== [gX CI] textwriter render matrix =="
	python3 -m unittest discover -s scripts -p test_textwriter_render_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/textwriter_render_check.py

#opencode: background editor — both open paths with scene routing, the panel
#   draw trace, and the shipped sample preset staying loadable.
background-editor-check: all
	@echo "== [gX CI] background editor open paths + sample preset =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/background_editor_check.py

#opencode: sky editor — both open paths with scene routing and the panel
#   draw trace; the preset round trip + shipped sample run in --test-only.
sky-editor-check: all
	@echo "== [gX CI] sky editor open paths =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/sky_editor_check.py

#ai: renderer-level sky state/repeat matrix; synthetic tests plus fixed GPU
#   captures. Preset/clock, precision and horizon gates stay authoritative.
sky-render-check: all
	@echo "== [gX CI] sky render matrix =="
	python3 -m unittest discover -s scripts -p test_sky_render_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/sky_render_check.py

#ai: Reduce Motion removes the analog CRT/VHS/static glitch path; the clean
#   input must stay static while the normal path remains animated.
analog-reduced-check: all
	@echo "== [gX CI] analog reduced-motion =="
	python3 -m unittest discover -s scripts -p test_analog_reduced_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/analog_reduced_check.py

#ai: colour-independence cue guard plus diagnostic colour-vision simulations.
colour-independence-check: all
	@echo "== [gX CI] colour-independence review =="
	python3 -m unittest discover -s scripts -p test_colour_independence_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/colour_independence_check.py

#opencode: UI audit — tool wiring matrix + panel conventions (+ captures with
#   --capture DIR for the vision review recorded in docs/ui_audit.md).
ui-audit: all
	@echo "== [gX CI] UI audit (wiring + conventions) =="
	python3 scripts/ui_audit.py

#ai: physics editor — both open paths render and exit clean, and both route
#   to the physics sandbox scene; the tuning round trip + live apply pins run
#   in --test-only (scene tests).
physics-editor-check: all
	@echo "== [gX CI] physics editor open paths =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/physics_editor_check.py

#opencode: blockout primitive pin (GenMesh* traces + visible frame change).
gfx-primitive-check: all
	@echo "== [gX CI] blockout primitives =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_primitive_check.py

#opencode: Frutiger Aero showcase pin (contract, bright frame, theme applied).
gfx-aero-check: all
	@echo "== [gX CI] Frutiger Aero showcase and theme =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_aero_check.py

#opencode: prefab capture/place pin (radius bundle, offsets, file shape).
gfx-prefab-check: all
	@echo "== [gX CI] prefab capture and placement =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_prefab_check.py

#opencode: GL-level request/readout pin (330 clamps compute, 430+GL43 enables).
gfx-gl-level-ui-check: all
	@echo "== [gX CI] GL level request and readout =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_gl_level_ui_check.py

#opencode: version format/consistency (docs/versioning.md) and the tiered gate.
version-check:
	@echo "== [gX] version consistency =="
	python3 scripts/version_check.py --consistency

version-gate:
	@tier=$$(python3 scripts/version_check.py --gate-from "$${FROM:-$$(git describe --tags --abbrev=0 2>/dev/null)}" --gate-to "$${TO:-$$(python3 scripts/version_check.py --readme)}" --tier-only); \
	echo "== [gX] version gate: $$tier =="; \
	case "$$tier" in \
	  full) AUDIT_CACHE=0 $(MAKE) check && AUDIT_CACHE=0 $(MAKE) audit-summary-full ;; \
	  fast) $(MAKE) check-fast ;; \
	  minimal) ./build/gX_build --test-only >/dev/null && python3 scripts/provenance_audit.py --strict ;; \
	  *) echo "no version change; nothing to run" ;; \
	esac

#ai: code/system attribution audit — NOTICE credits, source markers, dep licenses.
.PHONY: attribution-check
attribution-check:
	python3 scripts/attribution_audit.py --strict

#opencode: resource provenance manifest audit (generates license/ATTRIBUTIONS.md).
provenance-check:
	@echo "== [gX CI] resource provenance =="
	python3 scripts/provenance_audit.py --strict

#ai: hash-bound provenance intake report safety and output-shape tests.
.PHONY: asset-clearance-check
asset-clearance-check:
	python3 -m unittest discover -s scripts -p test_asset_clearance.py

#opencode: browse-only asset root discovery pin (GX_ASSETS + settings roots).
gfx-asset-roots-check: all
	@echo "== [gX CI] asset root discovery (browse-only guest roots) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_asset_roots_check.py

#opencode: bijection gate — save/load fixed point and effect toggle residue.
gfx-bijection-check: all
	@echo "== [gX CI] bijection round trips (fixed point + toggle residue) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_bijection_check.py

#opencode: >64-bone skinning upload and deformation pin (80-joint probe).
gfx-skin-bones-check: all
	@echo "== [gX CI] skin bone cap (80-joint upload in main + depth) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_skin_bones_check.py

#opencode: scene-camera render-to-image pin (size, identity, section round trip).
gfx-camera-render-check: all
	@echo "== [gX CI] camera render (PNG size, camera identity, preset section) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_camera_render_check.py

#opencode: authored model motion paths — circle tracking and one-shot pause.
gfx-motion-path-check: all
	@echo "== [gX CI] GFX model motion path =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_motion_path_check.py

#opencode: custom label fonts — shipped font renders, bad path falls back.
gfx-label-font-check: all
	@echo "== [gX CI] GFX label fonts =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_label_font_check.py

#opencode: read-only rig inspector — skeleton bones and parsed GLB clips.
gfx-rig-inspector-check: all
	@echo "== [gX CI] GFX rig inspector =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_rig_inspector_check.py

#opencode: GLB clip playback — skinned pose bends, loop wraps, one-shot clamps.
gfx-animation-check: all
	@echo "== [gX CI] GFX animation playback =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_animation_check.py

#opencode: stage-by-stage bloom audit — extract threshold/knee, blur spread,
#   composite intensity/chain, auto-exposure, emissive probe, MSAA interaction.
bloom-audit: all
	@echo "== [gX CI] bloom pipeline audit =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/bloom_audit.py

#opencode: curated bloom smoke (isolation + intensity ramp + emissive probe).
bloom-audit-quick: all
	@echo "== [gX fast] bloom pipeline smoke =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/bloom_audit.py --quick

#opencode: aggregate GPU audit summary — quick effect gates, one table.
audit-summary: all
	@echo "== [gX CI] GPU audit summary (quick) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/audit_summary.py --jobs $(AUDIT_JOBS)

#opencode: full GPU audit summary — adds the editor/preset gates.
audit-summary-full: all
	@echo "== [gX CI] GPU audit summary (full) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/audit_summary.py --full --jobs $(AUDIT_JOBS)

#opencode: existing scene presets require an explicit Cancel/Overwrite choice.
gfx-preset-overwrite-check: all
	@echo "== [gX CI] GFX preset overwrite guard =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_preset_overwrite_check.py

#opencode: nearest-hit overlap regression against the preserved original-art scene.
gfx-art-picker-check: all
	@echo "== [gX CI] GFX original-art overlapping-model picker =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_art_picker_check.py

#opencode: full-opacity blend regression using temporary original-art presets.
gfx-art-blending-check: all
	@echo "== [gX CI] GFX original-art full-opacity blending =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_art_blending_check.py

#opencode: virtual-input regression for stale planar-reflection warning and alignment.
gfx-mirror-alignment-check: all
	@echo "== [gX CI] GFX mirror-plane alignment warning and action =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_mirror_alignment_check.py

#opencode: drive planar-only and model-follow reflection authoring through the editor UI.
gfx-reflection-ui-check: all
	@echo "== [gX CI] GFX planar-only and model-follow editor round-trip =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_reflection_ui_check.py

#opencode: PBR/Phong direct-light accumulation, back-face isolation, and heap-slot reuse.
gfx-light-check: all
	@echo "== [gX CI] GFX multi-light and heap-slot behavior =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_light_check.py

#opencode: original-art editor regression for point-shadow ownership on light
#   removal and reuse; interactions are driven through GX_AI_INPUT.
gfx-art-light-check: all
	@echo "== [gX CI] GFX original-art light removal and slot reuse =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_art_light_check.py

#opencode: original-art reflection-mode independence using temporary presets.
gfx-art-reflection-check: all
	@echo "== [gX CI] GFX original-art environment, SSR, and planar independence =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_art_reflection_check.py

#opencode: textured material-map fidelity through the raw planar-reflection capture path.
gfx-planar-material-check: all
	@echo "== [gX CI] GFX textured model materials in planar captures =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_planar_material_check.py

#opencode: four-axis point-shadow placement and bias/acne sensitivity on private fixtures.
gfx-shadow-check: all
	@echo "== [gX CI] GFX point-shadow direction and acne behavior =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_shadow_check.py

#opencode: combined direct-light slot-reuse and point-shadow regression suite.
gfx-light-shadow-check: all
	@echo "== [gX CI] GFX light and shadow suites =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_light_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_shadow_check.py

#opencode: opt-in normal/height/parallax material-map A/B gate. This is live
#   GPU-only because it validates raster shading and UV displacement visually.
material-maps-check: all
	@echo "== [gX CI] material-map normal/parallax gate =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/material_maps_check.py

#ai: pixel-level ambient Phong albedo regression uses a temporary owned checker GLB.
.PHONY: phong-ambient-check
phong-ambient-check: all
	@echo "== [gX CI] ambient-only Phong albedo regression =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/phong_ambient_check.py

#opencode: offline static-lightmap contract and baker smoke check. It does not
#   require a display and deliberately rejects assets without TEXCOORD_1.
lightmap-check:
	@echo "== [gX CI] static-lightmap contract =="
	python3 scripts/lightmap_check.py

#opencode: advisory live A/B review of the static-lightmap fixture. It captures
#   the additive RGBA8 path on/off and never gates visual quality or make check.
lightmap-visual-review: all
	@echo "== [gX review] static-lightmap A/B capture =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/lightmap_visual_review.py

#opencode: temporal stability gate — consecutive-frame drift within one
#   GX_BURST run (shimmer-class regressions). Requires a live GPU display.
temporal-check: all
	@echo "== [gX CI] temporal stability gate =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_temporal_check.py

#opencode: temporal subset retains deterministic 12-frame bursts but runs
#   representative static and forced-fallback motion cases only.
temporal-check-fast: all
	@echo "== [gX fast] temporal stability subset =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_temporal_check.py --quick

#opencode: first-frame warmup pin (startup envelope: content by frame 7,
#   one shape, settled by frame 12). Requires a live GPU display.
warmup-check: all
	@echo "== [gX CI] first-frame warmup pin =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_warmup_check.py

#opencode: water object-reflection pin (SSR on/off over --showcase-water,
#   exposure locked). Requires a live GPU display.
#ai: standalone water CI retains the reflection gate and adds owned authoring fixtures.
#ai: extracted surface panels retain native input and model-local field isolation.
.PHONY: gfx-mesh-culling-check
gfx-mesh-culling-check:
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_mesh_culling_check.py

.PHONY: gfx-surface-panels-check
gfx-surface-panels-check:
	python3 scripts/gfx_surface_panels_check.py

.PHONY: water-ci-check
water-ci-check: all
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_water_authoring_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/water_scroll_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_water_check.py

water-check: all
	@echo "== [gX CI] water object-reflection pin =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_water_check.py

#opencode: feature scoreboard (completeness/correctness/hardness).
#   Full run includes median timing plus the matrix + bloom audit + reflect
#   suite (~30 min); use
#   scripts/gfx_scoreboard.py --only <rows> for a fast subset.
scoreboard: all
	@echo "== [gX CI] feature scoreboard =="
	GX_AUDIT_WINDOW= DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_scoreboard.py

#opencode: fog/sky horizon pin (mirror seam + night murk, exposure
#   locked). Requires a live GPU display.
horizon-check: all
	@echo "== [gX CI] fog/sky horizon pin =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_horizon_check.py

#opencode: fog+light joint verification matrix (fog/light coupling, not
#   isolated effects). Requires a live GPU display like gfx-check.
fog-light-check: all
	@echo "== [gX CI] fog+light joint matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/fog_light_check.py

#opencode: wireframe placement pin — difference gates pass garbage, only
#   a bbox gate catches misplacement. Requires a live GPU display.
wireframe-check: all
	@echo "== [gX CI] wireframe placement pin =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/wireframe_check.py

#opencode: optional GL4.3 compute particle velocity regression pin; leaves the
#   normal GL33 build untouched and requires a live compute-capable display.
particle-velocity-check:
	@echo "== [gX CI] GL4.3 particle velocity pin =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/particle_velocity_check.py

#opencode: optional GL4.3 Forward+ GPU/CPU parity + 64-light ceiling pin;
#   builds an isolated GL33 uniform reference and requires a live display.
compute-feature-check:
	@echo "== [gX CI] Forward+ parity + 64-light ceiling pin =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/compute_feature_check.py

#opencode: opt-in GL compatibility policy matrix. It builds isolated GL33 and
#   GL43 binaries, checks selected-level flags, and visually isolates the
#   GL3.1 instancing and GL3.2 geometry/MSAA capability fixtures.
gl-level-check:
	@echo "== [gX CI] GL-level policy matrix =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gl_level_check.py

#opencode: hosted GLSL 1.30 dialect probe. This preserves GL33 production and
# deliberately does not claim native GL3.0 context certification.
glsl130-check: all
	@echo "== [gX CI] hosted GLSL 1.30 compatibility probe =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/glsl130_check.py

#opencode: native desktop GL 3.0/3.1/3.2 certification. Requires the opt-in
# raylib context hook (patches/raylib-gx-gl30-context.patch applied + rebuild)
# and a Mesa GLX vendor; fails loudly with the recipe when either is missing.
gl-legacy-native-check: all
	@echo "== [gX CI] native GL 3.x certification =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gl_legacy_native_check.py

#opencode: opt-in Paper 1 M1 cosine-hemisphere/direct-light transport oracle.
#   Uses the deterministic reflectrow roughness ladder; not part of make check.
oracle-path-check: all
	@echo "== [gX CI] Paper 1 M1 path oracle =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/oracle_path_check.py

#opencode: opt-in Paper 1 M2 CPU two-bounce reference transport gate.
#   This is offline and does not require DISPLAY or a renderer build.
oracle-bounce-check:
	@echo "== [gX CI] Paper 1 M2 CPU bounce oracle =="
	python3 scripts/oracle_bounce_check.py

#opencode: opt-in Paper 1 M2/M3/M4 raster/CPU linear-HDR parity gate. The
#   matte, direct-GGX, environment-bounce, and pre-tonemap fixtures are
#   isolated from production.
oracle-hdr-check: all
	@echo "== [gX CI] Paper 1 M2/M3/M4 raster HDR parity oracle =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/oracle_hdr_check.py

#opencode: opt-in Paper 1 M5 GL4.3 compute surface-bounce parity gate.
#   Requires a live GL4.3+ display; GL33 production contexts are rejected.
oracle-gpu-check: all
	@echo "== [gX CI] Paper 1 M5 GPU surface-bounce parity oracle =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/oracle_gpu_check.py

#opencode: opt-in Paper 1 M6 GL4.3 two-secondary parity gate.
#   Requires a live GL4.3+ display; M5 one-secondary parity must pass first.
oracle-gpu-multibounce-check: all
	@echo "== [gX CI] Paper 1 M6 GPU two-secondary parity oracle =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/oracle_gpu_multibounce_check.py

#opencode: opt-in Paper 1 M8 GL4.3 perfect-reflection parity gate.
#   Requires a live GL4.3+ display; M5/M6 transport remains unchanged.
oracle-gpu-reflection-check: all
	@echo "== [gX CI] Paper 1 M8 GPU reflection parity oracle =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/oracle_gpu_reflection_check.py

#opencode: static GL/RHI state audit; --strict is intentionally opt-in until
#   legacy terrain raw-state paths are migrated to the RHI.
gl-audit:
	python3 scripts/gl_state_audit.py

#ai: advisory GFX audit for texture, uniform, and skinning integration contracts.
.PHONY: gfx-obvious-audit
gfx-obvious-audit:
	python3 -m unittest discover -s scripts -p test_gfx_obvious_audit.py
	python3 scripts/gfx_obvious_audit.py

#opencode: layout gate — auto-seat CPU verdict + scene capture triptych.
#   Requires a live GPU display (DISPLAY=:1); NOT part of headless check.
layout-check: all
	@echo "== [gX CI] layout gate (auto-seat + captures) =="
	DISPLAY=$${DISPLAY:-:1} python3 scripts/layout_check.py

#opencode: Release build lives under build/ per repo policy (was build-release).
lean:
	cmake -B build/release . -DCMAKE_BUILD_TYPE=Release
	cmake --build build/release -j$$(nproc)
	bash scripts/lean_gate.sh build/release/gX_build

#ai: repeatable section/symbol attribution for a built Release payload.
.PHONY: payload-audit
payload-audit: release
	python3 -m unittest discover -s scripts -p test_payload_audit.py
	python3 scripts/payload_audit.py --binary build/release/gX_build

#opencode: math pass — the numeric sibling of the proofs: the engine's
#   gxmath corpus (GX_MATH_DUMP=1) is recomputed by scripts/math_pass.py
#   with an independent SymPy oracle. Blocking in check.
math-check: all
	@echo "== [gX CI] math pass (gxmath corpus vs SymPy oracle) =="
	#ai: pin color quantization order and basic unit formulas to the Lean models.
	python3 -m unittest discover -s scripts -p test_color_units_contract.py
	#ai: preserve sky-frame signs, observer parameters and the native handedness convention.
	python3 -m unittest discover -s scripts -p test_sky_contract.py
	#ai: pin cylindrical/spherical axis order and polar-inclination semantics.
	python3 -m unittest discover -s scripts -p test_coordinates_contract.py
	#ai: Simpson source guards and bounded native cubic/edge-branch probes.
	python3 -m unittest discover -s scripts -p 'test_simpson_*.py'
	#ai: quaternion/vector/matrix convention witnesses and bounded native rotation probes.
	python3 -m unittest discover -s scripts -p 'test_quaternion_rotation_*.py'
	python3 -m unittest discover -s scripts -p test_math_pass.py
	python3 scripts/math_pass.py
	python3 -m unittest discover -s scripts -p test_math_random_check.py
	python3 scripts/math_random_check.py
	python3 -m unittest discover -s scripts -p test_math_property_check.py
	python3 scripts/math_property_check.py

#ai: seeded randomized differential probes across algebraic, SDF, physics and conversion families.
.PHONY: math-random-check
math-random-check: all
	@echo "== [gX CI] randomized math differential =="
	python3 -m unittest discover -s scripts -p test_math_random_check.py
	python3 scripts/math_random_check.py

#ai: invariant identities over the seeded dump, independent of the formula oracle.
.PHONY: math-property-check
math-property-check: all
	@echo "== [gX CI] math invariant check =="
	python3 -m unittest discover -s scripts -p test_math_property_check.py
	python3 scripts/math_property_check.py

#ai: opt-in high-count soak; admission keeps the 64-probe default.
.PHONY: math-soak-check
math-soak-check: all
	@echo "== [gX CI] math soak ($(MATH_SOAK_PROBES) probes/family) =="
	python3 -m unittest discover -s scripts -p test_math_random_check.py
	python3 scripts/math_random_check.py --probes $(MATH_SOAK_PROBES)
	python3 scripts/math_property_check.py --probes $(MATH_SOAK_PROBES)

#ai: opt-in gate self-test; disposable copy + single-line mutations must turn math gates red.
.PHONY: math-mutation-check
math-mutation-check:
	python3 scripts/math_mutation_check.py

#ai: headless native fixtures vs exact residual/reprojection oracle; GPU accuracy remains outside this gate.
.PHONY: reprojection-residual-check
reprojection-residual-check: all
	python3 -m unittest discover -s scripts -p test_reprojection_residual_check.py
	python3 scripts/reprojection_residual_check.py

#ai: native perspective near/mid/far fixtures; not a GPU texture quantization certificate.
.PHONY: depth-input-check
depth-input-check: all
	python3 -m unittest discover -s scripts -p test_depth_input_check.py
	python3 scripts/depth_input_check.py

#ai: exact planar theory plus native IEEE witnesses, not a universal GPU guarantee.
.PHONY: planar-geometry-check
planar-geometry-check: all
	python3 -m unittest discover -s scripts -p test_planar_geometry_check.py
	python3 scripts/planar_geometry_check.py

#ai: conditional reflected-camera frame checks; do not imply an unconditional culling change.
.PHONY: reflection-handedness-check
reflection-handedness-check: all
	python3 -m unittest discover -s scripts -p test_reflection_handedness_check.py
	python3 scripts/reflection_handedness_check.py

#ai: exact near-plane theory/native projection witnesses; not a replacement GPU clipper.
.PHONY: projection-clipping-check
projection-clipping-check: all
	python3 -m unittest discover -s scripts -p test_projection_clipping_check.py
	python3 scripts/projection_clipping_check.py

#ai: native six-face capture and reference tap rerouting witnesses.
.PHONY: cubemap-geometry-check
cubemap-geometry-check: all
	python3 -m unittest discover -s scripts -p test_cubemap_geometry_check.py
	python3 scripts/cubemap_geometry_check.py

#ai: scalar native witnesses; texture/GPU precision remains an explicit obligation.
.PHONY: point-shadow-depth-check
point-shadow-depth-check: all
	python3 -m unittest discover -s scripts -p test_point_shadow_depth_check.py
	python3 scripts/point_shadow_depth_check.py

#ai: native/reference fixtures do not GPU cascade coverage.
.PHONY: csm-contract-check
csm-contract-check: all
	python3 -m unittest discover -s scripts -p test_csm_contract_check.py
	python3 scripts/csm_contract_check.py

#ai: shader-visible flat-slab artifact regression; separate GPU gate, not headless admission.
.PHONY: gtao-stripe-check
gtao-stripe-check: all
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gtao_stripe_check.py

#ai: partial-orbit GTAO temporal freshness gate on the gX-owned reflectrow proxy.
.PHONY: gtao-orbit-check
gtao-orbit-check: all
	python3 -m unittest discover -s scripts -p test_gtao_orbit_check.py
	python3 -m unittest discover -s scripts -p test_gtao_contract_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gtao_orbit_check.py

#ai: headless native reconstruction and scalar/source correspondence guards.
.PHONY: gtao-contract-check
gtao-contract-check: all
	python3 -m unittest discover -s scripts -p test_gtao_contract_check.py
	python3 scripts/gtao_contract_check.py

#ai: scalar/native checks admitted; isolated stripe/bounce GPU gate remains separate.
.PHONY: ssgi-contract-check ssgi-stripe-check
ssgi-contract-check: all
	python3 -m unittest discover -s scripts -p test_ssgi_contract_check.py
	python3 scripts/ssgi_contract_check.py
ssgi-stripe-check: all
	DISPLAY=$${DISPLAY:-:1} python3 scripts/ssgi_stripe_check.py

#ai: source-extracted dimension policies; exact counterexamples are not GPU certification.
.PHONY: postprocess-contract-check
postprocess-contract-check:
	python3 -m unittest discover -s scripts -p test_postprocess_contract_check.py
	python3 scripts/postprocess_contract_check.py

#ai: Debug opt-in writer/UI fixtures with explicit report submissions.
.PHONY: gfx-bug-report-check
gfx-bug-report-check: all
	python3 -m unittest discover -s scripts -p test_bug_status.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_bug_report_check.py

#ai: empirical live-allocation probes; no production format changes.
.PHONY: gpu-depth-precision-check
gpu-depth-precision-check: all
	python3 -m unittest discover -s scripts -p test_gfx_depth_precision_check.py
	DISPLAY=$${DISPLAY:-:1} python3 scripts/gfx_depth_precision_check.py

#opencode: proofs gate — Lean specs in ../proofs (core-only, no Mathlib).
#   Blocking in check. Distinct from `lean` (binary-size gate) on purpose.
proofs:
	@if [ ! -d ../proofs ]; then \
	  echo "proofs: not included in the public source beta (maintained separately)." >&2; \
	  exit 1; \
	fi
	command -v lake >/dev/null || (echo "proofs: 'lake' not found (install elan + Lean 4.27.0)" >&2 && exit 1)
	cd ../proofs && timeout 600 lake build GxProofs
	#ai: keep the generated proof catalog in sync with the source modules.
	python3 -m unittest discover -s scripts -p test_proof_inventory.py
	python3 scripts/proof_inventory.py --check
	#ai: proof-certificate gate selection/verdict/hash controls.
	python3 -m unittest discover -s scripts -p test_proof_certify.py
	#ai: source-drift guards for kernel/RHI/temporal contracts.
	python3 -m unittest discover -s scripts -p test_proof_source_contract.py
	#ai: reject orphaned modules and compiler-trust/placeholder axioms in the blocking tier.
	python3 -m unittest discover -s scripts -p test_proof_audit.py
	#ai: source-ground reflection freshness/resource/missing-mask branch contracts.
	python3 -m unittest discover -s scripts -p test_reflection_contract.py
	#ai: pin the selected SSR auxiliary-route clauses used by the coverage proofs.
	python3 -m unittest discover -s scripts -p test_ssr_coverage_contract.py
	python3 scripts/proof_audit.py --no-build

#ai: regenerate or check the generated proof catalog.
.PHONY: proof-inventory proof-inventory-check
proof-inventory:
	python3 scripts/proof_inventory.py
proof-inventory-check:
	python3 scripts/proof_inventory.py --check

#ai: proof-certificate artifact; catalog/kernel/guarded tiers, not an admission gate.
PROOF_CERT_TIER ?= kernel
.PHONY: proof-certify
proof-certify:
	python3 scripts/proof_certify.py --tier $(PROOF_CERT_TIER)

#opencode: proofs-full — Mathlib tiers (GxProofsGfx + GxMath,
#   incl. salvaged gameX specs). Opt-in, never in check: without
#   Users only pay for the cache when explicitly requesting this gate.
#ai: an explicitly requested proof gate must not report a successful skip.
proofs-full:
	@if [ ! -d ../proofs ]; then \
	  echo "proofs-full: not included in the public source beta (maintained separately)." >&2; \
	  exit 1; \
	fi
	command -v lake >/dev/null || (echo "proofs-full: 'lake' not found (install elan + Lean 4.27.0)" >&2 && exit 1)
	if [ ! -d ../proofs/.lake/packages/mathlib ]; then \
	  echo "proofs-full: failed (no Mathlib cache; run 'lake update' in proofs/)" >&2; exit 1; \
	else \
	  cd ../proofs && lake build GxProofs GxProofsGfx GxMath && \
	    python3 scripts/proof_audit.py --full --no-build; \
	fi
	#ai: keep the generated proof catalog in sync with the source modules.
	python3 scripts/proof_inventory.py --check
	#ai: pin the source-grounded cofactor contract without a SymPy dependency in the proof gate.
	python3 -m unittest discover -s scripts -p test_inverse_contract.py
	#ai: source-drift witnesses for channel truncation and exact-real unit conversions.
	python3 -m unittest discover -s scripts -p test_color_units_contract.py
	#ai: source-drift witnesses for the Cartesian sky-frame proof model.
	python3 -m unittest discover -s scripts -p test_sky_contract.py
	#ai: source-drift witnesses for branch-aware cylindrical/spherical coordinates.
	python3 -m unittest discover -s scripts -p test_coordinates_contract.py
	#ai: source-drift witnesses for exact-real Simpson endpoint and parity contracts.
	python3 -m unittest discover -s scripts -p test_simpson_contract.py
	#ai: pin the exact quaternion rotation and matrix-transform convention.
	python3 -m unittest discover -s scripts -p test_quaternion_rotation_contract.py

release:
	$(MAKE) BUILD_TYPE=Release all

clean:
	rm -rf build
