# SSR edge-ray classification audit — 2026-09-23

## Scope

Isolate whether lift-off-sensitive SSR hit changes are caused by the near-origin
self-plane swallow, the silhouette-jump rejection, or the final refined-hit
phantom gate. Production thresholds and the receiver path were held constant
except for the single A/B parameter under test.

Captures use `--showcase-minimal` or the curved-reflector
`--showcase-planar` fixture, `GX_DIAG_CUBE=1`, `GX_T_SSR=1`, TAA off, fixed
`1/60 s` updates, seed `1337`, and orbit distance `6`. Orbit rows are
`(yaw,pitch) = (30,25), (45,25), (60,20)`. Normal-buffer tests explicitly use
`GX_SSR_NORMALS=1`; the normals-off control uses `GX_SSR_NORMALS=0`.

## Findings

### The self-plane swallow is not the active classifier in these fixtures

At yaw 30 on the planar/curved fixture, bypassing the self-plane guard with
`GX_SSR_SELFPLANE_REJECT=0` changed **0 of 1,275,912 classified hit pixels**.
The `GX_SSR_DEBUG=11` map differed at 16 pixels, with a maximum 22-code change
in its phantom channel. This is too small to explain the lift-off-correlated
hit-class changes.

### The silhouette-jump gate is causal

The production crossing test is
`abs(diff - prevDiff) > maxDepthDelta * (1 + t * 0.5)`, with
`maxDepthDelta=0.6`. At yaw 30 on the planar fixture, changing only
`GX_T_MAXDEPTH` from `0.6` to `1.2` changed 22,619 of 1,275,707 classified
hit pixels (1.773%). The dominant transitions were 22,378 in-screen-sky to
geometry and 226 environment-fallback to geometry.

Those changes track the early rejection diagnostics: 99.4% of hit-class flips
overlap pixels whose phantom-rejection count changed, 99.9% overlap changed
crossing counts, and the LOD-confirmation map did not change. This isolates the
silhouette-jump gate from the LOD-0 confirmation and final phantom tests.

The relaxed gate improves the measured magenta cube coverage in the curved
fixture:

| Yaw | `maxDepthDelta=0.6` planar/cube | `1.2` planar/cube | `3.0` planar/cube |
|---:|---:|---:|---:|
| 30 | 0.6230 / 0.6030 | 0.6366 / 0.7487 | 0.6366 / 0.8427 |
| 45 | 0.4111 / 0.5374 | 0.5257 / 0.6571 | 0.5296 / 0.8637 |
| 60 | 0.4172 / 0.0118 | 0.4110 / 0.0118 | 0.4049 / 0.1684 |

The 30° and 45° rows improve both components. The 60° curved-reflector row
remains below its cube-zone gate even at `3.0`. The regular planar SSR trace
oracle is unchanged to two decimals at each angle (53.67%, 54.89%, 55.84%).

On the standard minimal fixture with the diagnostic cube, `maxDepthDelta=1.2`
also improves cube fidelity with raster normals enabled:

| Yaw | Normals on, `0.6` → `1.2` | Normals off, `0.6` → `1.2` |
|---:|---:|---:|
| 30 | 45.95% → 52.33% | 44.82% → 50.75% |
| 45 | 56.61% → 61.64% | 56.50% → 61.35% |
| 60 | 58.67% → 58.94% | 57.02% → 57.05% |

The gain is therefore not dependent on the optional raster-normal buffer. A
smaller absolute threshold of `0.9` produced more modest gains at yaw 30/45
and was effectively neutral at yaw 60; it did not materially improve the
curved-reflector cube zones.

### Distance-only relaxation is gentler, with a smaller gain

To retain the strict near-contact threshold, an opt-in distance-scale control
now exposes the existing `0.5` coefficient in
`maxDepthDelta * (1 + t * maxDepthDistanceScale)`. Raising only that coefficient
to `1.5` (keeping `maxDepthDelta=0.6`) improved standard minimal SSR by
1.35/2.50/0.03 percentage points at yaw 30/45/60. Curved-reflector cube-zone
coverage moved from 0.6030/0.5374/0.0118 to 0.6283/0.5560/0.0118. Scales `0.75`
and `1.0` matched the baseline oracle to the reported precision.

## Decision

Keep the production values unchanged: `maxDepthDelta=0.6` and distance scale
`0.5`. The silhouette gate is responsible for the edge-ray false negatives,
and relaxed thresholds improve two of three standard angles plus the first two
curved-reflector angles. However, broader near-contact/ghost-ring validation
is still needed before changing production tuning; the 60° curved-reflector
case remains unresolved.

Opt-in controls for further A/Bs:

- `GX_T_MAXDEPTH=N` varies the existing maximum depth delta.
- `GX_T_MAXDEPTH_SCALE=N` varies only its distance coefficient (`0.5` default).
- `GX_SSR_SELFPLANE_REJECT=0` bypasses only the near-origin self-plane swallow.

With all three variables unset, a repeated fixed-seed default capture was
byte-identical to the pre-hook capture. All captures and logs are under
`/tmp/opencode/ssr-edge-ray-2026-09-23/`.
