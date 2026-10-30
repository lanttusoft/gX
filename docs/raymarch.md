# Raymarch

## Experimental hybrid visibility

**Shared-depth prototype:** `GX_GFX_RAYMARCH_DEPTH=2` (or the editor's
**shared scene depth (experimental)** checkbox) merges SDF color and
projected primary-hit depth into the raster HDR scene after MSAA resolve,
before water and screen-space inputs. The color fill uses
`parameters.renderScale`; a separate full-res depth-only march writes the
primary hit into the same depth attachment (hardware LESS). Misses and clipped
hits write nothing. Mode 1 below remains the independent post-effects
visibility path; production default is off.

## Compositing order

The two renderers keep separate scene representations; the merge order is
deliberate:

1. GFX rasterizes opaque geometry and terrain into the HDR scene (color +
   depth), then MSAA resolves.
2. **Shared depth**: the SDF color fill is drawn at `renderScale` and
   upscaled; a full-res depth-only march writes primary SDF depth into the
   raster depth attachment. SDF hits behind raster depth fail the hardware
   test and write nothing. Water, SSR, SSAO, TAA and bloom then consume the
   merged frame.
3. **Hybrid visibility** (`GX_GFX_RAYMARCH_DEPTH=1`): the SDF color is
   composited after raster effects as an opaque visibility overlay; it does
   not feed depth, normals or materials to those effects.
4. **Backdrop composite** (`GX_GFX_RAYMARCH_COMPOSITE=2`): raymarch runs
   first and GFX is composited over it. This is the only order where
   raymarch is the background, and it does not share depth.

So "raymarch on top" means depth-tested color drawn after the raster pass, not
a naive overlay: raster geometry in front still wins, and shared depth lets
later effects treat SDF hits as scene depth.

`make gfx-raymarch-shared-depth-check` checks color visibility and native float
combined depth against the analytic sphere/cube oracle for both camera views,
including clear-depth misses. A DOF on/off probe checks a depth-only consumer
against the post-effects visibility control. Raw depth dumps use the existing GXRTF32 format.
A 4× MSAA probe checks SDF depth against the pixel-center analytic ray and
raster depth against the matching MSAA baseline: resolved raster samples need
not lie at pixel centers. Both depth-error limits remain `2e-6`.
Shared color participates in raster effects, and depth-only consumers can see
SDF hits. Shared HDR alpha now carries explicit per-object `roughness` (0..1),
independent of legacy reflectivity/refraction/IOR/emission. Historical presets
default to matte (1); imports clamp bounds and nonfinite values fall back to 1.
Terrain, water and light markers retain matte fallback in this phase.

<!-- //ai: shared optional normals use floating-point storage to preserve encoded zero components. -->
With `GX_SSR_NORMALS=1`, the opt-in raster normal pass also renders SDF primary
world normals into its RGBA16F target (RGB = normal*0.5+0.5, alpha = binary coverage).
Its private depth attachment preserves foreground raster normals and replaces
occluded raster normals with SDF normals. Primary normals are saved before any
secondary bounce. The normals-off production default remains unchanged.
`make gfx-raymarch-surface-check` verifies normal direction, unit length,
coverage, raster preservation and authored HDR roughness using native readbacks.
Its SSR debug-28 on/off control verifies agreement versus missing coverage in
the actual SSR consumer; this proves input wiring, not reflection quality.

The separate `make gfx-raymarch-reflection-check` exercises a visible green
raster target reflected by the SDF sphere, plus a removed-target control from
axial and oblique views. Its CPU oracle computes specular reflection rays and
excludes off-screen, sphere-occluded, and target-self-occluded faces.
Native SSR debug 4 tests accepted
screen hits (not environment/aux fallbacks), and debug 18 checks target-color
attribution. The fixture requires at least 90% target-hit/color recall and at
most 1% false hits. Debug 15 tests authored high-roughness early-outs, and
debug 14 checks that analytic misses route to environment rather than geometry.
Oracle masks, native buffers, captures, logs, metrics, and replay commands are
retained. Passing this
focused fixture does not certify general reflection quality or promote normals
to the production default.

Shared-mode SSR traverses bounded empty-depth gaps instead of stopping at the
first background sample. It refines the full candidate bracket and requires
tight world-position coincidence; locally planar depth neighborhoods interpolate
between texel centers to avoid nearest-depth stair steps on sloped faces.
Silhouettes and discontinuities retain nearest sampling. Debug 32 exposes native
candidate residuals (R=world gap, G=signed view-depth residual, B=candidate present).
Legacy mode keeps its original traversal/refinement/tolerance; rejected candidates
no longer retain accepted-hit state in either mode.

### Camera-only temporal prototype (validation in progress)

`GX_RAYMARCH_TEMPORAL=1` opts shared mode into raster-compatible Halton sampling
and a post-depth SDF ownership overlay in the existing velocity target. Covered
SDF pixels use camera-only depth reprojection instead of hidden raster vectors;
uncovered raster vectors are preserved. History depth is compared against the
point's projected **previous-frame** depth, with out-of-viewport history rejected.
Hybrid history color/depth use an explicit bilinear base-level footprint; the
legacy raster render-texture sampler remains unchanged.
Clock-playing SDF pixels reject history rather than pretending animated geometry
has static motion. Authored geometry/material/paused-time or routing changes
invalidate history; camera movement does not. Object animation vectors and general temporal quality are not
certified. Standalone, visibility-only, and hook-off behavior stays unchanged.

`GX_TAA_REPROJECTION_DEBUG=1/2/3` exposes native camera-motion/previous-depth,
history-coordinate/eligibility, or sampled-history-depth/residual diagnostics.
Modes 4/5/6 expose direct history-depth/UV, constant coverage, or current-input
color/depth witnesses. Set
`GX_TAA_REPROJECTION_DEBUG_FRAME=N` for a single-frame probe so earlier history
remains real color/depth. `scripts/gfx_raymarch_temporal_check.py` currently
checks analytic coordinates, strong previous-visibility classes, and production
current-color rejection. Those checks and the 12-frame static fixture burst pass
the unchanged budgets; five native authored/time/routing invalidation checks and
`make check` also pass. The hook remains opt-in, not general temporal certification.
Use `--only reprojection`, `--only stability`, or `--only invalidation` for focused
runs (default: all). This evidence is fixture-specific. Do not treat this prototype
as temporal admission.

Validation residual: the full feature matrix still fails particles (changed
fraction 0), probes (0), and mirror (0.049859 versus the 0.05 floor). All three
failures reproduce on pre-temporal HEAD `e1bad85` with unchanged thresholds.
Reflection and bloom suites pass; the temporal fixture does not certify or hide
these pre-existing matrix failures.

This is not full deferred integration: full material buffers and motion vectors
are absent. SSR quality and temporal effects are
not certified for SDF surfaces; cross-renderer shadows/secondary hits and
transparent interleaving remain unsupported.

`GX_GFX_RAYMARCH_DEPTH=1` enables a single GFX compositor layer with an SDF
overlay at scene entry. The GFX editor's Raymarch panel exposes **hybrid depth
(opaque)** as a transient checkbox; legacy overlay/backdrop controls are disabled
while it is active. Disabling restores previous compositor routing and module
flags; scene changes reset it. No preset/settings save is performed.

The mode uses an SDF
overlay tested against the current HDR raster depth. The shared external
perspective camera overrides the module camera. Primary hit distance is
converted to view-axis depth and compared with reconstructed raster depth;
misses preserve GFX sky and raster hits in front remain untouched. The
hybrid color layer honors `parameters.renderScale` (default 0.35) and upscales
from the private HDR buffer; 0.5 or higher is recommended when overlay
sharpness matters. Shared-depth mode uses the same scaled color fill plus a
full-res depth-only march, so the depth attachment stays exact while the
shaded pixels stay cheap. Proxy mode is bypassed. Standalone and historical
color compositing are unchanged.

Mode 1 is **opaque visibility only**, after raster effects: no shared SDF depth,
normals or materials are exported to SSAO/SSR/TAA/bloom, no cross-renderer
shadows or secondary-ray raster hits, no transparent interleaving, and no
orthographic support. `make gfx-raymarch-depth-check` compares front, behind,
intersecting and missing sphere/cube fixtures against an analytic oracle from
axial and oblique cameras. It retains metrics, captures, logs and replay commands.

The raymarch module is gX's signed-distance-field renderer: it marches the
camera ray through an SDF scene (terrain, water, primitives, lights), shades
with Blinn-Phong + optional glow, and composites over/with the raster
renderer. Scenes are authorable in the GFX editor and persist as JSON presets.

## Quick start

```bash
./build/gX_build --raymarch                  # built-in SDF scene
./build/gX_build --gfx-editor --raymarch     # author it (parse order matters)
```

In the raymarch scene the camera is free: **W/A/S/D** move, **Q/E** down/up,
mouse looks. Press **F1** or the editor hotkey to open the GFX editor.

Useful CLI flags:

- `--raymarch-profile[=path.csv]` — per-frame module GPU timers
  (`frame,draw_ms,total_ms,render_scale`).
- `--hw-report` — capability report + hardware tier (see `docs/hardware.md`).

## The editor panel

The **Raymarch** section lives in the GFX Scene window (it draws when the
raymarch scene is active; in a gfx scene enable *view raymarch module* or
*composite raymarch backdrop*):

- **Render** — steps, bounces, render scale, near/far/epsilon; **proxy
  wireframe** (stop-render mode); feature checkboxes (terrain, grid, water,
  fog, sky, AO, objects, lights, …).
- **Sun and fog** — sun brightness/size/direction/color, distance-fog
  near/far/color.
- **Clouds** — opt-in volumetric deck: enable, bottom/top, coverage, density,
  scale, speed, absorb, powder, color. `speed` animates only when the preset
  enables `state.useInternalTime` (keeps captures deterministic otherwise).
- **Volumetric fog** — opt-in height fog with sun shafts: enable, density,
  height falloff, scattering, HG asymmetry, color. The height band is anchored
  1.5 below the camera (the gfx `fog_volume` rule); a 3-tap SDF sun visibility
  carves the godrays, and a sky-ambient fill keeps the haze readable away from
  the sun. Marched over the shaded image (surface distance, or the far plane
  for sky rays) after the clouds.
- **Terrain / Water** — noise seed, offset/scale/rotation, color bands
  (ground/steep/beach), tex2 surface detail, water surface offset,
  ripple/whirlpool/wave speed/gradient clamp.
- **Objects** — up to 8 SDF primitives with per-object type, position,
  selection, named dimensions, axis scale, world-axis rotation deltas, color,
  named material/lighting controls and texturing. Add creates a selected sphere
  six units ahead of the camera; removal preserves neighbors' world poses.
- **Lights** — up to 8 point lights (brightness/position/color).
- **Debug** — view modes (position, direction, ray, gradient, normal, bounce
  count, path steps/length/distance).
- **Shader features** — live `CF_*` compile toggles (hot-reload).
- **Preset** — built-ins, a browser over `resources/tools/raymarch/`, and
  save/load; the schema round-trips every section below.

Built-ins: `default`, `island`, `water`, `flatlands`, `desert`, `alien`,
`canyon`, `primitives` (all SDF types in a row), `cloudscape` (cloud deck on).

The default now enables water at Y=-3.5, tiled terrain detail, and tex2 stone
albedo on the torus. Saved presets keep their own enable flags, colors and
mapping settings. Water uses angle-dependent dielectric Fresnel (about 2% at
normal incidence), with reflected SDF geometry, sky and enabled clouds. Use at
least two bounces; Draft's single bounce deliberately omits secondary tracing.
Crossing swells complement the localized ripple, and conservative terrain
distance controls shallow tint/shore foam (an approximation, not traced water
refraction). Play the Time clock to animate, or scrub it deterministically.

Raymarch's sky now transitions from blue zenith to a brighter horizon, with
sun-elevation-driven twilight/night and a subdued lower hemisphere. The standalone
`--sky` renderer also has finite horizon airmass, stable polar celestial bases,
radial moon/planet phase coordinates, continuous ground blending, and
luminance-anchored contrast. HDR sky targets preserve sun radiance above one;
LDR targets still clamp when storing pixels.

### Quality and object workflow

The Render quality picker applies these profiles without changing scene features:

| quality | render scale | total steps | bounces |
| --- | --- | --- | --- |
| Draft | 0.25 | 128 | 1 |
| Balanced | 0.35 | 128 | 2 |
| High | 0.50 | 192 | 3 |
| Full | 1.00 | 256 | 4 |

All clear a per-bounce step override. Manual edits to scale/step/bounce settings
show **Custom**; spatial/epsilon settings are independent. The ordinary
step/scale/epsilon controls remain under **Advanced tracing**. These profiles are not shader
feature presets and do not trigger compilation. Full is deliberately expensive;
benchmark resolution rows keep their baseline trace budgets, not these profiles.

Choose a slot in **Objects → selected** to edit one inspector; **Focus selected**
centers the camera on it. Type changes reset
only the primitive's dimensions to sensible defaults. **axis scale** changes local
axis lengths; **rotate by (degrees) → Apply rotation** applies XYZ world-axis
deltas. **Reset scale/orientation** leaves position intact. The **Advanced matrix**
section preserves access to imported shear/mirroring and raw inverse SDF columns;
merely opening the inspector never decomposes or rewrites them. Proxy wireframes
use that same inverse transform, including the legacy per-slot yaw.

### Check diagnostics

`make gfx-raymarch-check` first runs GPU-free negative tests for the checker,
then captures the real engine. The selected inspector must provide visible-item
bounds for selection, type, position and scale, with nonempty pixels inside each
control; a panel log marker alone no longer passes this probe.

Each run retains a unique `/tmp/opencode/gfx_raymarch_*` directory (printed as
`GFX_RAYMARCH_ARTIFACTS`), on success or failure. `report.json` records verdict,
timings, measurements and key thresholds. Capture directories contain images,
`engine.log`, `request.json` and `reproduce.sh` (run with `sh`; preserves the
probe's hooks and settings-save protection). Timed-out captures retain partial
logs and requests. Failed analytic hits emit missing/false-hit masks; default
burst failures emit a temporal-difference image. Artifacts are outside the repo
and may be removed after review. These checks do not yet simulate author clicks;
CPU command tests and the analytic authoring fixture cover command behavior.
The same gate also captures four standalone sky modes (day/twilight/night/polar
conjunction). The feature gate independently checks reflected red geometry,
wave motion and terrain texture detail, not merely whether water changes color.

## Presets

Presets are JSON: `state`, `parameters`, `fog`, `clouds`, `sun`, `terrain`,
`water`, `flags`, `misc`, `debug`, the `objects[8]`/`lights[8]` arrays, and a
`textures` section (skybox + tex0..2 paths/filters, skybox layout). Save/load
from the panel; authored files live under `resources/tools/raymarch/` and can
be deleted freely. The headless gate reuses the same schema
(`GX_GFX_RAYMARCH_CHECK=<dir>` writes/mutates/reloads a preset).

## Objects

### SDF operation/node matrix (first phase)

**SDF node matrix** is an 8×8 workspace of 64 slots, not a spatial object grid.
Select a cell, choose its operation and inputs, then **Use selected as output**.
`output = -1` bypasses the graph and preserves the ordinary object union. Slots
may link forward or backward, but the reachable graph must be cycle-free and
fit a 32-node runtime program. Unreachable slots do not consume GPU instructions.
The CPU compacts/topologically orders the program; shader compilation is not
needed when editing links within an active graph. Activating the first graph
or returning to bypass selects an optional `CF_CSG_GRAPH` shader permutation;
legacy scenes do not compile the graph evaluator. Leaf objects retain their existing transforms,
textures and materials. Terrain, water and light markers remain separate fields.

| opcode | node | inputs/parameters |
| --- | --- | --- |
| 0 | empty | cannot be referenced |
| 1 | object | A = existing object slot 0..7 |
| 2 | union | A/B = node slots |
| 3 | difference | **A minus B**, not reversed; cut faces use A's material |
| 4 | intersection | A/B = node slots |
| 5 | smooth union | A/B and nonnegative smoothing distance |
| 6 | smooth difference | A minus B and smoothing distance |
| 7 | smooth intersection | A/B and smoothing distance |
| 8 | infinite repeat | A = object node; local-axis periods, 0 disables an axis |
| 9 | finite repeat | as above, plus 0..255 copies on each side per axis |

Smoothness zero is the ordinary operator. Union/intersection choose the winning
leaf material; smooth operators currently smooth geometry, not material blending.
Repeat nodes in this first phase accept **centered sphere/box object nodes only**,
not CSG groups. Enabled periods must contain the full local primitive extent;
neighbor cells are evaluated to avoid unsafe nearest-cell jumps. Finite limits
are inclusive, so an axis limit of 1 produces three centers (-1,0,+1). Position
and orientation come from the leaf object's transform, including nonuniform
scale. Graph proxy mode still shows source-object bounds, not a CSG result mesh.

Invalid edits report an error and render independent objects instead. Saving an
invalid graph is refused; loading one reports failure and leaves that fallback
visible. Deleting an object remaps surviving leaf indices; references to the
deleted object become invalid, rather than silently changing their target.

The optional preset `graph` section holds `root` and flattened numeric arrays
`nodes`/`domains` (64 four-component entries each). Node entries are
`(opcode,A,B,smoothness)`; domain entries are `(periodX,periodY,periodZ,limits)`.
`limits = X + 256*Y + 65536*Z` packs three integer counts exactly in float32.
Compiled programs/error messages are not serialized. Loading a historical
graph-less preset clears the previous graph. `GX_RAYMARCH_GRAPH_TEST=2..9`
selects isolated operator fixtures; the raymarch gate compares all eight GPU
silhouettes with a dense CPU sign-field oracle, alongside CPU DAG/schema tests.
`GX_RAYMARCH_EDITOR_INSPECT=2` exposes the matrix for UI capture; visible-item
checks require its output selector, last grid cell and operation picker to be
on-screen. This is visibility evidence, not simulated click coverage.

Types (attribute = the object's four parameters):

| # | type | attribute semantics |
| --- | --- | --- |
| 0 | sphere | x = radius |
| 1 | cube | xyz = half extents |
| 2 | torus | x = radius, y = tube |
| 3 | box frame | xyz = half extents, w = edge |
| 4 | cylinder | x = radius, y = half-height |
| 5 | cone | xy = sin/cos half-angle, z = height; editor uses degrees |
| 6 | tree | — |
| 7 | mandelbulb | x = dr, y = power, z = limit, w = breakaway |
| 8 | conifer | — |
| 9 | menger | xyz = half extents |
| 10 | capsule | x = half-length, y = radius |
| 11 | octahedron | x = size |
| 12 | rounded box | xyz = half extents, w = corner radius |
| 13 | hex prism | x = radius, y = half-height |

### Surface tracing

Object distances are converted from transformed local space to a conservative
world-space bound, including nonuniform scaling. A ray stops only at a surface
tolerance or a refined sign-crossing bracket, never just because the step budget
ran out. Passing near one object does not terminate a ray aimed at another.
Camera-inside rays march forward to the exit surface, and secondary misses keep
the first surface opaque while sampling the reflected/refracted environment.

The default and built-in presets use 128 total steps. `stepsPerBounce` can limit
each bounce within that shared budget. Too few steps may still miss grazing or
procedural detail; raise the budget rather than enabling last-step false hits.
The legacy `enableSampleLastStep` field still round-trips but no longer invents
surface hits. Terrain/water correction polynomials can slow a march (correction
above one), but values below one cannot enlarge its distance steps.

Terrain distance/normal sampling uses explicit texture LOD zero so divergent rays
evaluate the same surface. The baked height field uses a conservative slope
bound; normal probes use a wider terrain stencil to avoid quantization rings.

Lighting queries start outside the hit surface along its normal, with a bias
matched to the hit tolerance. Lighting and AO change RGB only, not coverage;
light markers are emissive and do not self-shadow. This avoids white/black
speckles as near-surface samples move across a hit threshold.

Emissive glow changes RGB only. Coverage is clamped before writing the HDR cache:
alpha above one otherwise subtracts the previous canvas during upscaling and
causes stationary white/black flicker at the default light orb. The gate records
12 consecutive default-scene frames to exercise this actual feedback path.

The **Time** panel plays/pauses the preset clock, scrubs seconds, and resets it;
pause preserves the current time. **Terrain scale** works with baked noise as
well as procedural noise: X/Z multiply horizontal frequency, Y multiplies height.

### Compile loading screen

Initial raymarch compiles and shader reloads present `loading` with animated
ellipsis and three orbiting RGB cores. Their gas field is private to the loader:
existing PP gaussian blur, geometry translation/expansion, and HSV shaders diffuse
and advect the emission without touching the scene's feedback textures.
KHR/ARB parallel-shader-compile drivers animate while compilation/linking runs;
drivers without that extension show the loader but still block during compilation.
Closing the window cancels a completion wait. Loader resources are freed afterward.

The shader shares its symmetric normal-probe loop and disables NVIDIA's aggressive
loop unrolling with `optionNV(unroll none)` (other drivers ignore this pragma).
This reduces cold compile/link work without removing SDF types or trace steps.

Per-object **texturing** is `(texture slot 0..2, mode, scale, blend)` with
mode `off / triplanar / planar XZ / XY / YZ` and blend `0 = full texture,
1 = base color` — set it with the panel's texture/mapping/scale controls.

## Textures

The module owns a skybox cubemap and three 2D textures (`tex0..2`). Paths,
filters (point → aniso 16x) and the skybox layout are editable in the
**Textures** panel section with thumbnails and reload buttons, picked from the
shared GFX asset browser (**Raymarch** targets), and persisted in the preset.
Terrain uses tex2 red-channel detail at four world units per tile, controlled by
`terrain.parameters.w` (0=off, 1=full). This field was previously unused; old
presets with zero remain untextured. The shipped grayscale displacement texture
provides neutral detail without introducing any new asset.

## Compositing with the raster scene

- `GX_GFX_RAYMARCH_VIEW=1` forces the module into any scene (draws over it).
- `GX_GFX_RAYMARCH_COMPOSITE=1` makes the compositor own the frame: raymarch
  backdrop + gfx layer screen-blended over it.
- `GX_GFX_TRANSPARENT_BG=1` switches the gfx layer to a depth-masked alpha
  blend, so the raymarch sky shows through raster geometry (true compositing).
- `GX_GFX_RAYMARCH_COMPOSITE=2` renders the backdrop layer alone (gate
  reference).

## Proxy wireframe mode (stop rendering)

Enable **proxy wireframe** in the Render section (or
`GX_GFX_RAYMARCH_FEATURE=proxy` + `_STATE=1`) to stop the SDF march entirely
and draw the scene as raster line proxies: per-object wireframes at their
transforms (sphere rings, box edges, cylinder/capsule/cone outlines, torus
rings, octahedron/hex-prism edges, bounds for the procedural shapes), terrain
and water grids, light markers (radius tracks brightness) and a sun-direction
ray. The camera and panel stay fully live while the frame costs a few hundred
lines instead of a full march — useful for layout work, low-power sessions,
and debugging a scene's geometry. Presets persist the flag
(`flags.enableProxy`).

## Performance, profiling, hardware

- The color fill renders at `parameters.renderScale` (default 0.35) and
  upscales; that is the main quality/cost knob. Shared depth uses the same
  scaled fill plus a full-res depth-only march. The depth stage caps its
  primary budget at 64 steps (128 when the authored steps exceed 128); the
  color pass keeps the authored steps.
- Deterministic probe: `--showcase-minimal` plus
  `GX_RAYMARCH_DEPTH_TEST=front` puts one SDF sphere against the raster
  floor/cube with no terrain or water, isolating pass cost from scene content.
  `make gfx-raymarch-shared-bench-check` runs that probe and checks that the
  color scale separates 0.35 from 1.00 and that the depth overhead stays
  within a bounded multiple of hybrid color.
- `--raymarch-profile=<csv>` writes the module's own GPU timers;
  `make raymarch-benchmark` measures quality and feature rows
  (`make raymarch-benchmark-check` validates the artifact and the
  resolution-scaling trend). See `docs/rendering_backlog.md` for measured
  numbers.
- `make hw-report-check` and `docs/hardware.md` cover the supported GL
  feature tiers.

Release builds optimize the CPU-side raymarch module for size (`-Os`); its GLSL
tracing and the main GFX renderer are unchanged. Loader uniform locations are
cached per compile session. Preset resets release the previous module's shader,
textures/cache, and GPU timestamp queries before rebuilding defaults.

## Headless hooks (gates / CI)

All are environment variables; see `scripts/gfx_raymarch_check.py` and
`scripts/gfx_raymarch_features.py` for the contracts.

| hook | effect |
| --- | --- |
| `GX_GFX_RAYMARCH_VIEW=1` | force the module into the current scene |
| `GX_GFX_RAYMARCH_COMPOSITE=1\|2` | composite (both layers / backdrop only) |
| `GX_GFX_TRANSPARENT_BG=1` | true-alpha gfx layer for the composite |
| `GX_GFX_RAYMARCH_FEATURE=<name>` + `_STATE=0\|1` | one-shot flag toggle (terrain, water, fog, clouds, volfog, objects, lights, proxy, …; `objecttexture` sets a textured fixture) |
| `GX_GFX_RAYMARCH_DEFINES="…"` | full `CF_*` define override |
| `GX_GFX_RAYMARCH_RENDERSCALE=0.25` | pin the module render scale |
| `GX_RAYMARCH_STEPS=<n>` | pin march steps (benchmark/quality tuning) |
| `GX_RAYMARCH_BOUNCES=<n>` | pin march bounces (benchmark/quality tuning) |
| `GX_GFX_RAYMARCH_GALLERY=1\|2\|3` | built-in primitive row / empty / new-types row |
| `GX_GFX_RAYMARCH_GALLERY=4\|5\|6\|7` | analytic scaled solid / camera inside / near miss then far target / exhausted-budget fixtures |
| `GX_GFX_RAYMARCH_GALLERY=8` | lit reflective white-sphere stability fixture |
| `GX_GFX_RAYMARCH_GALLERY=9` | authoring type/scale/rotation/removal + Full-quality analytic solid fixture |
| `GX_RAYMARCH_EDITOR_INSPECT=1` | UI capture fixture: expose the selected-object inspector, collapse Time/Render |
| `GX_GFX_RAYMARCH_HIT_JITTER` | mode 8 only: tiny horizontal offset for subpixel stability comparisons |
| `GX_RAYMARCH_LOADING_CAPTURE=<dir>` | opt-in presented loading frames (every fifth frame, first 90); suitable for GIF encoding |
| `GX_RAYMARCH_PRESET_CYCLE=1` | visit built-ins 0..8 in one live process, exercising loaded-resource resets |
| `GX_GFX_RAYMARCH_CHECK=<dir>` | preset save-mutate-reload round trip + trace |
| `GX_HW_REPORT=1` | capability report |

Deterministic captures use `GX_FIXED_DT`, `GX_FRAMES`, `GX_SHOT_FRAME`,
`GX_SCREENSHOT_DIR`, `GX_NO_SETTINGS_SAVE=1` (the audit helpers set these).

## Code API

The module is a standard renderer module (`renderer.raymarch` heap) and draws
through the compositor or directly:

```c
gX_RendererRaymarch *rm = (gX_RendererRaymarch *)renderer.raymarch.data;
gX_RendererRaymarch_Preset_BuiltIn(rm, 8);          // cloudscape
gX_RendererRaymarch_Preset_Save_New(rm, "scene.json");
gX_RendererRaymarch_Preset_Load_New(rm, "scene.json");
gX_RendererRaymarch_Texture_SetPath(rm, 0, "resources/texture/texture.png");
gX_RendererRaymarch_Defines_Set(rm, "CF_WATER", 0); // hot-reloads the shader
gX_Renderer_RaymarchComposite_Set(1);               // compositor owns the frame
```

Scene data lives in `gX_Raymarch_*` structs (`raymarch.h`); the schema
functions live in `renderer/raymarch/schema2.c`.

## Gates

- `make gfx-raymarch-check` — preset round-trip, scene frame, editor panel,
  primitive gallery, analytic solid-hit masks (holes/false hits), and white-sphere
  time/subpixel-motion stability.
- `make gfx-raymarch-features-check` — per-feature frame effects, the `CF_*`
  define matrix, gfx integration, compositing, true alpha.
- `make raymarch-benchmark` / `-check` — cost rows and the scaling trend.
- All of the above plus `make check` (unit tests, math corpus, Lean, lean
  gate) must pass before a change is committed.
