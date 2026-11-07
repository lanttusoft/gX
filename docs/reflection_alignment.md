# Reflection alignment contract

<!-- //ai: reflection ownership/frame/pipeline contract; gate first, pre-bloom resolve later. -->

## Purpose

gX has four reflection paths that can contribute to the same frame:

- exact planar reflections from a mirrored camera,
- screen-space reflections (SSR),
- environment cubemap / IBL reflections,
- the planar aux fallback used by SSR miss pixels.

Each path is useful in isolation. A frame is correct only when they agree on
the mirror plane, camera, frame, UV convention, ownership and color space.

This contract is intentionally narrow: the first slice is documentation plus
the numeric alignment gate. The pre-bloom SSR resolve is a separate review
slice (see “Implementation slices”).

## Paths and current code

### Planar

`src/renderer/gfx/draw.c` builds the mirror camera and stores
`gfx->reflection.mvp`, renders the planar RT, and excludes the capturing slab
from the fill. `resources/shader/standard.frag` samples the RT projectively
through `reflMVP` when `planarReflection == 1`.

### SSR

`resources/shader/gfx_ssr.frag` marches the depth buffer and outputs linear
HDR color with a confidence alpha. `Pass_Run_SsrComp` currently composites the
map into `bloom.scene` after the bloom/tonemap pass.

### Environment / IBL

`standard.frag` samples the probe cubemap (box-corrected) and uses it directly
or as the planar fallback. The probe must exclude the reflective slab and be
captured before the main scene draw.

### Planar aux fallback

When `ENABLE_SSR_AUXVIEW` is on and the planar RT is valid, SSR miss pixels can
sample `planarVP` through `planarSampleUV()` instead of the env cubemap.

The Y2K cartridge now uses the exact planar path only: the floor has
`planarReflection = 1`, `receiveSSR = 0`, and the cartridge disables SSR. This
is the current single-owner example, not the general engine resolution.

## Invariants

### 1. One owner per surface

Every reflective material has exactly one primary source:

| Mode | Owner | Fallback |
| --- | --- | --- |
| `PLANAR` | exact planar RT | env only off-plane / at RT edges |
| `SSR` | SSR march | env on a miss |
| `ENV` | cubemap | none |
| `PLANAR + ENV_FALLBACK` | planar on-plane | env at edges |

A planar receiver must not also receive SSR. A `reflectionSource` slab only
captures the mirror view; it is not automatically a planar receiver. Do not
infer ownership from three independent booleans without checking the
combination.

### 2. One reflection frame per frame

Build once and share:

```text
ReflectionFrame
  view, proj, vp
  planeNormal, planeDist
  jitter
  probePosition, probeBounds
  exposure
  valid
```

The fill, direct sampling, SSR aux and debug overlays must consume the same
frame. A path that recomputes its own matrix or plane is a bug.

### 3. One plane convention

`planeDist` is the signed distance to the reflective plane in world space
(`dot(n, x) = d`). The fill clip, the mirrored camera placement, the direct
`pconf` gate and the SSR `originOnPlane` test must all use the same value. For
a slab, use the top surface, not the model origin.

### 4. Same camera and jitter

The mirror VP must use the same viewport, aspect, fovy, near and far planes as
the main camera. If TAA jitter moves the main projection, either apply the same
jitter to the mirror VP or disable TAA on planar frames. Do not mix a jittered
main pass with an unjittered mirror pass. The Y2K console profile disables TAA.

### 5. Pre-bloom composition and color space

All reflection sources should resolve in linear HDR before bloom and tonemap.
The current SSR composite runs after bloom and writes linear SSR values into
the tonemapped `bloom.scene`, which can make a geometrically aligned reflection
look like a second image and clips its glow. The planned order is:

```text
scene -> reflection capture -> opaque/transparent
      -> SSR -> reflection resolve (linear HDR)
      -> bloom/tonemap -> post -> present
```

Do not claim this order until the pre-bloom slice lands with its own evidence.

### 6. Env is a fallback

For on-plane fragments, planar confidence should be effectively 1. Mix the
cubemap only off-plane or at RT edges, with a narrow fade. Exclude reflective
slabs from the probe capture, update the probe before the main pass, and use
the same exposure.

## Numeric gate

`make reflection-alignment-check` runs two focused checks:

1. **Planar mirror chain** — `scripts/oracle_planar.py` on
   `--showcase-planar`: CPU-replicated main VP vs the direct magenta centroid,
   ghost centroid vs the mirrored-ray intersection, and echo UV vs the logged
   mirror projection. This catches plane, mirror-camera, `planarVP`, y-flip and
   RT-orientation errors.
2. **Y2K ownership** — a `--cartridge-y2k` boot with `GX_GFX_PLAN=1` must
   report `SHOWCASE-Y2K: mirror planar=1 receiveSSR=0`, include
   `feature=planar-reflections`, and exclude `feature=ssr`.

Run the planar oracle with TAA off (the product Y2K setting). A TAA-on run is
advisory: it records the jitter mismatch that the contract forbids; it does
not fail the product gate until the jitter-consistent mirror VP lands.

The gate is not a substitute for the full reflection suite (`make reflect-check`)
or the visual review; it is the fast numeric ownership/geometry check.

## Debug

Existing hooks:

- `GX_T_SSR=0` — disable SSR.
- `GX_T_SSRAUX=0` — disable the planar aux fallback.
- `GX_PLANAR_AUX=0` — disable the aux bind in the composite.
- `GX_T_DEFER=0|1` — force the planar-defer switch.
- `GX_PLANAR_VIEW=1` — present the planar RT.
- `GX_SSR_DEBUG=23` — echo `planarSampleUV(worldPos)` for the oracle.

Add a future `GX_REFL_ALIGN=1` overlay/dump for the direct UV, aux UV, plane and
matrix if a new mismatch class appears.

## Implementation slices

1. **This slice:** contract + `make reflection-alignment-check` (planar oracle
   and Y2K ownership).
2. **Pre-bloom SSR resolve:** move/duplicate the composite into linear HDR
   before bloom, or tonemap the SSR color consistently; add a color-match and
   glow-retention gate.
3. **Per-surface ownership:** make `planarReflection` imply no SSR/defer and
   make the mode explicit; keep `reflectionSource` capture-only.
4. **Jitter-consistent mirror VP:** apply the main jitter to the mirror
   projection or reject planar frames from TAA; gate TAA on/off.
5. **Probe exclusion and narrow env fallback:** exclude reflective slabs from
   the probe capture and prove the fallback only owns edges/off-plane pixels.
