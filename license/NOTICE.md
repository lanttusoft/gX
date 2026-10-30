# gX — Third-Party Notices

gX is licensed under GPLv3 (see `LICENSE`). This file lists third-party
code, assets, and content bundled with gX, per their respective licenses.

## Code

### raylib 6.0 — zlib/libpng
- Location: `external/raylib/` (vendored in the public beta); full license:
  `license/raylib/LICENSE`
- raylib bundles the following (licenses apply via raylib's own
  distribution):
  - `stb_image` — public domain / MIT (dual)
  - `cgltf` / `cgltf_write` — MIT (Johannes Kuhlmann)
  - `tinyobj_loader_c` — MIT (Syoyo Fujita)
  - `miniaudio` — public domain / MIT (dual, David Reid)
  - `par_shapes` — MIT (Philip Rideout)
  - `glad` / `glad_gles2` — MIT (David Herberth)
  - `dr_flac` / `dr_mp3` / `dr_wav` — public domain (David Reid)
  - `jar_mod` / `jar_xm` — public domain (Joshua Reisenauer)
  - GLFW (included for cross-platform builds) — zlib (Marcus Geelnard)
- **raymath v1.5** — zlib/libpng, Ramon Santamaria (@raysan5), part of the
  raylib distribution. gX's `src/gxmath/` is based on/uses raymath v1.5; the
  source headers retain the license. Credit and license reference:
  `license/raylib/LICENSE`.

### Renderer/shader references and adapted algorithms

- **three.js GTAOPass / GTAO (Jimenez et al. 2016)** — `gfx_gtao.frag` is a
  renderer port of the three.js reference implementation. three.js is MIT;
  see `license/three/LICENSE`.
- **NVIDIA FXAA 3.11 (Timothy Lottes)** —
  `resources/shader/gfx_fxaa.frag` follows the FXAA 3.11 formulation;
  see `license/fxaa/LICENSE` (3-clause BSD).
- **Inigo Quilez SDF / smooth-min / normals** — shared SDF primitives and
  operators in `resources/shader/common/` follow IQ's published formulations;
  MIT, see `license/iq/LICENSE`.
- **Ashima Arts / Stefan Gustavson simplex noise** —
  `resources/shader/common/noise.glsl` uses the Ashima simplex-noise
  formulation; MIT, see `license/ashima/LICENSE`.
- **ACES filmic fit (Krzysztof Narkowicz)** — tone-mapping fit used in
  `gfx_bloom_composite.frag` and `standard.frag`.
- **AgX (Troy Sobotka)** — display transform reference used in
  `gfx_bloom_composite.frag`; MIT implementation lineage.
- **SSRT3-inspired screen-space GI** — `gfx_ssgi.frag` cites the SSRT3
  approach for its horizon-based gather; this is an algorithm citation, not a
  claim of copied third-party code.
- **raylib wrapper surfaces** — `src/wrappers/` and the raylib backend contain
  borrowed/adapted raylib code; covered by the raylib license above.

### Directly vendored
- Dear ImGui v1.92.8 WIP (imgui core + tables/draw/widgets, imgui_internal.h,
  imconfig.h, imstb_rectpack/textedit/truetype) — MIT, Omar Cornut
  (2014-2026) — `src/tools/imgui/`, full license: `license/imgui/LICENSE`
  (imstb_* files are additionally public domain).
- rlImGui (raylib backend for Dear ImGui) — zlib, Jeffery Myers (2020-2021)
  — `src/tools/imgui/rlImGui.cpp/h`, full license:
  `license/rlimgui/LICENSE`
- AngelScript 2.38.0 (AngelCode Scripting Library) — zlib/libpng, Andreas
  Jönsson (2003-2023) — `external/angelscript/` (vendored source subset:
  `include/`, `source/`, `add_on/scriptstdstring/`, `add_on/scriptarray/`),
  full license: `license/angelscript/LICENSE`. Compiled only when
  `GX_USE_ANGELSCRIPT=ON` (see `docs/scripting.md`).

### Optional system dependency
- **Open Dynamics Engine (ODE)** — LGPL-2.1; optional system `libode` used by
  the physics sandbox (`docs/physics.md`). Not vendored. Distributions that
  link ODE must retain the LGPL notice and the applicable relinkable-object
  obligations; see `docs/attribution_audit.md`.

### Adapted / credited source
- **NTSC/CRT** — EMMIR (2018–2023), integer NTSC/CRT video-signal emulation
  adapted in `src/external/crt_core.c` and `src/external/crt_ntsc.c`; the
  upstream header credit is retained. Upstream project:
  https://github.com/LMP88959/NTSC-CRT. Full upstream terms:
  `license/ntsc-crt/LICENSE` (permissive; attribution optional but credited).
  See `docs/attribution_audit.md`.
- **Tangent-basis construction** — `raylib_Mesh_GenTangents` independently
  implements the standard UV-gradient tangent/binormal construction; no
  tutorial snippet is copied.
- **gX lineage** — gX's ports from the author's read-only gameX/game3/game2
  archives are project-owned work, not third-party code; see
  `docs/porting.md`.

### Proof/build dependencies
- **Lean 4 / Mathlib4** — Apache-2.0. Used by the separate
  [gX proofs](https://github.com/lanttusoft/gx-proofs) repository as a
  Lake/Mathlib proof-build dependency; not linked into the release executable.
  The core proof specifications salvaged from the author's gameX `lean/`
  archive are project-owned and re-verified against gX source. Attribution
  recorded here for source and proof artifacts.

## Assets

### Fonts (all CC0 — Creative Commons Zero 1.0)
- `resources/font/KenneyMiniSquareMono.ttf` — Kenney (kenney.nl), CC0,
  engine UI font (`resources/font/LICENSE-Kenney-CC0.txt`)
- `resources/font/EuropeanTeletext.ttf` — Jayvee Enaguas (HarvettFox96),
  CC0 — `resources/font/LICENSE-EuropeanTeletext-CC0.txt`

### Audio (generated, project-owned)
- All UI sounds are synthesized at runtime by `gX_Audio_GenRegisterWave`
  (`src/audio.c`) — no third-party samples are shipped.
- `resources/audio/wavetable/*.f32` — AI-generated (author-prompted,
  procedural script; deterministic additive tables for the synth's
  `gX_Synth_WT_LoadFile` path, regenerate with `scripts/gen_wavetables.py`).

### Provenance record (author-confirmed 2026-09-27)

- `resources/model/button.glb` — author-owned.
- `resources/model/teapot.glb` — Utah teapot (public-domain dataset).
- `resources/model/props/tubetech_vt.glb` — AI-generated (author-prompted).
- `resources/model/primitive/*.glb` — author-owned procedural primitives,
  except `lightmap_fixture.glb`, which is AI-generated.
- `resources/texture/texture.png`, `texture_normal.png`, and
  `texture_displace.png` — author-owned.
- `resources/texture/orb.png` — author-owned.
- `resources/texture/gx.png` — the gX logo, author-owned (pixel-art mark).
- `resources/texture/gx_title.png` — title-screen derivative of the author-owned
  logo with the black matte made transparent.
- `resources/texture/cookie_window.png` and `lightmap_fixture.png` —
  AI-generated (author-prompted).
- `resources/texture/misc/scorch.png` — AI-generated (author-prompted,
  procedural script; impact-scorch decal).
- `resources/audio/wavetable/*.f32` — AI-generated (author-prompted,
  procedural script; additive wavetables).
- `resources/model/probe/*` — project-generated fixtures (see below).
- Both fonts ship with their CC0 license files
  (`LICENSE-Kenney-CC0.txt`, `LICENSE-EuropeanTeletext-CC0.txt`).

### Newly added models (author-confirmed 2026-10-06)

- `resources/model/arena.glb`, `axis.glb`, `checkerboard.glb`, and
  `tree.glb` — author-created Blender exports; no third-party imports; licensed
  as project material under GPLv3.

### Third-party with attribution

- `resources/texture/skybox/skybox1.png` — image from **learnopengl.com**
  (Joey de Vries), licensed **CC BY 4.0**
  (https://creativecommons.org/licenses/by/4.0/), source
  https://learnopengl.com/Advanced-OpenGL/Cubemaps; exported/resized for gX.
  gX's SSAO/IBL shader code is an independent split-sum/GGX and depth-masked
  blur implementation; no LearnOpenGL tutorial **code** is shipped.
- `resources/model/teapot.glb` — the **Utah teapot** (public-domain dataset),
  project-exported; no attribution required.
- `resources/model/button.glb` — project-authored (author's own).

### Models / textures (project-original)
- `resources/model/*.glb` — Blender-exported project originals (incl.
  `props/tubetech_vt.glb`, the raymarch/geo-demo model).
- `resources/texture/texture*.png`, `resources/text/textwriter.txt` —
  project-generated raymarch textures and textwriter demo content.
- `resources/model/primitive/*.glb` — procedural primitive shapes
  (cube, sphere, cone, cylinder, icosahedron, torus).
- `resources/model/primitive/lightmap_fixture.glb` and
  `resources/texture/lightmap_fixture.png` — project-original generated
  secondary-UV static-lightmap fixture for the opt-in GFX validation path.
- `resources/model/probe/matprobe.glb` — copy of the project-original cube
  primitive used by the opt-in material-override fixture (`GX_MATPROBE`);
  its `.materials.json` sidecar is project-authored.
- `resources/model/probe/mapprobe.glb` — another cube copy used by the
  opt-in map-override fixture (`GX_MAPPROBE`); its sidecar references the
  project-generated `misc/testcard.png` and `orb.png` textures.
- `resources/model/probe/animprobe.glb` — fully project-authored two-joint
  skinned ribbon generated by `scripts/make_animation_probe.py` for the clip
  playback and GPU skinning gate (`make gfx-animation-check`).
- `resources/model/probe/skinprobe80.glb` — fully project-authored 80-joint
  skinned ribbon generated by `scripts/make_skin_bones_probe.py` for the
  skin bone-cap gate (`make gfx-skin-bones-check`); self-authored, no
  third-party content.
- `resources/texture/*.png` — project-generated textures (incl.
  `misc/testcard.png`, an SMPTE-style testcard generated for the analog
  renderer's default input, and `cookie_window.png`, a window-grid light
  cookie generated for the gfx showcase spotlight).
- The teapot geometry is the classic Utah teapot (Martin Newell, 1975;
  public domain geometry).

### AI-generated content (stance decided 2026-10-08)

AI-generated assets recorded above ship under gX's GPLv3 like the rest of
the project. Because some jurisdictions do not grant copyright for purely
AI-generated works, gX claims no exclusivity over them: to the extent
copyright does not apply, they may be used as if they were public domain.
This removes licensing ambiguity for downstream users; it does not change
the license of anything else here. Policy: `docs/ai_policy.md`. AI tools can
make mistakes; see `docs/ai_disclosure.md` for the disclosure and as-is
disclaimer.

## Notices

- Kenney fonts are free to use in personal, educational and commercial
  projects; crediting Kenney (kenney.nl) is appreciated but not mandatory.
- raylib is licensed under an unmodified zlib/libpng license permitting
  static linking with closed-source software; see `license/raylib/LICENSE`.
- Dear ImGui (MIT) and rlImGui (zlib) are vendored for the gX Tools suite
  (in-engine editors); they are compiled into the binary only when the
  tools sources are included in the build.

Maintained by the resource audit (`scripts/copyright_audit.py`) and the
attribution audit (`scripts/attribution_audit.py`) — the copyright audit fails
CI if a media file is added without a matching license entry here; the
attribution gate fails on missing code/system credits or dependency licenses.
