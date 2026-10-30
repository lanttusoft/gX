# Attribution audit — code and systems

<!-- //ai: 2026-10-22 source/system attribution pass from suggestions.md. -->

Status: audit + gate (2026-10-22). This is **not** the asset/provenance audit:
resources are covered by `provenance_audit.py`, fonts/media by
`copyright_audit.py`, and the compiled binary's third-party libraries by
`license/NOTICE.md`.

`make attribution-check` verifies required code/system credits, source credit
markers and dependency license files. It is a static gate; it does not prove a
license is compatible — the items marked “verify before publish” still require
a deliberate license check.

## Inventory

| System / lineage | Where used | Origin / license | Current credit | Status |
|---|---|---|---|---|
| raylib + raymath + stb/cgltf/tinyobj/miniaudio/par_shapes/glad/GLFW | engine backend, image/audio/model loading, `gxmath` base | zlib/MIT/public-domain (see raylib) | NOTICE + `license/raylib/LICENSE`; raymath source headers | credited |
| three.js GTAOPass | `gfx_gtao.frag` port; Jimenez et al. 2016 algorithm | MIT | NOTICE + `license/three/LICENSE` | credited |
| NVIDIA FXAA 3.11 / Timothy Lottes | `gfx_fxaa.frag` | 3-clause BSD | NOTICE + `license/fxaa/LICENSE` | credited |
| Inigo Quilez SDF/smooth-min/normals | shared SDF and raymarch shader building blocks | MIT | NOTICE + `license/iq/LICENSE` | credited |
| Ashima Arts / Stefan Gustavson simplex noise | `common/noise.glsl` | MIT | NOTICE + `license/ashima/LICENSE` | credited |
| ACES filmic fit (Narkowicz) / AgX (Sobotka) | tone mapping | published formulations; AgX MIT reference | NOTICE + `license/agx/LICENSE` | credited |
| SSRT3-inspired SSGI | `gfx_ssgi.frag` horizon-gather model | algorithm citation | NOTICE | credited; no copied-code claim |
| Independent SSAO/IBL shader implementation | `gfx_ssao_blur.frag`; `gfx_ibl_{brdf,irradiance,prefilter}.frag` | independently written from published split-sum/GGX (Karis 2013) and depth-masked blur; no tutorial code | NOTICE | credited as independent implementation |
| LearnOpenGL skybox image | `resources/texture/skybox/skybox1.png` | CC BY 4.0 (Joey de Vries) | NOTICE + provenance | credited |
| Dear ImGui / rlImGui | tools UI | MIT / zlib | NOTICE + license files | credited |
| AngelScript | optional shell module | zlib/libpng | NOTICE + license file | credited |
| ODE (Open Dynamics Engine) | optional system `libode` physics | LGPL-2.1 | NOTICE (added) | credited; distribution obligations apply |
| EMMIR NTSC/CRT | `src/external/crt_core.c`, `crt_ntsc.c` | EMMIR 2018–2023; LMP88959/NTSC-CRT permissive terms verified | NOTICE + `license/ntsc-crt/LICENSE` + source header | credited |
| Tangent-basis construction | `src/backend/raylib/raylib_wrappers_model.c` | independent UV-gradient implementation; no tutorial snippet | NOTICE | credited as independent implementation |
| Lean 4 / Mathlib4 | `proofs/` and the separate [gX proofs](https://github.com/lanttusoft/gx-proofs) repository | Apache-2.0 | NOTICE | credited; build/proof dependency, not a shipped binary |
| gameX / game3 / game2 | architecture, ports, gxmath/raymarch/gfx lineage | author's read-only archives, project-owned | `docs/porting.md`, NOTICE | recorded |
| AI-generated assets | wavetables, textures, fixtures | author-prompted; stance in NOTICE | NOTICE + policy | credited |
| Kenney / European Teletext fonts | UI / teletext | CC0 | NOTICE + license files | credited |
| learnopengl skybox | `resources/texture/skybox/skybox1.png` | CC BY 4.0 images (Joey de Vries); attribution recorded | NOTICE + provenance + publish-readiness | credited |
| Utah teapot | `resources/model/teapot.glb` | public-domain geometry | NOTICE | credited |

## Findings and actions

1. **ODE was missing from NOTICE.** Added; it is an optional system library,
   so release packaging must retain the LGPL notice and relinkable-object
   obligations where applicable.
2. **EMMIR’s NTSC/CRT sources carried a header credit but no NOTICE entry.**
   Added; the upstream LMP88959/NTSC-CRT terms are verified permissive, with
   the source credit retained and the upstream text shipped at
   `license/ntsc-crt/LICENSE`.
3. **The tangent-basis code was independently rewritten.** The Unity-derived
   citation is no longer present; the function now implements the standard
   UV-gradient/Gram-Schmidt construction directly.
4. **Mathlib/Lean was only implicit through `proofs/`.** Added as a proof/build
   dependency; it is not linked into the release executable.
5. **Asset attribution remains separate.** `learnopengl` skybox, fonts,
   teapot, AI-generated assets and quarantined files stay under
   `provenance_audit.py` / `copyright_audit.py` and the release audit.
6. **Renderer lineage was under-credited.** Added explicit raymath, three.js
   GTAO, FXAA, Inigo Quilez, Ashima/Gustavson, ACES/AgX and SSRT3 credits and
   license files.
7. **SSAO/IBL code was independently rewritten.** The previous tutorial-derived
   implementations were replaced with independent depth-masked SSAO and
   split-sum/GGX shader code; the LearnOpenGL **code** license is no longer a
   dependency. The LearnOpenGL skybox **image** remains CC BY 4.0 with
   attribution.
8. **Proof lineage is separate.** The separate `gx-proofs` repository carries
   the Mathlib/Lean notice and the gameX `lean/` salvage lineage; proofs are
   not part of the engine payload.

## Gate

```bash
make attribution-check
```

The gate checks the required systems, the source credit markers
(`crt_core`, `crt_ntsc`, tangent snippet and renderer headers), the vendored
and reference license files, and the Mathlib proof import. It does **not**
replace legal review of the “verify before publish” rows; the SSAO/IBL code
is now an independent implementation rather than a tutorial-code dependency.
