# gX — Minimum Hardware Study (measured floor on a reference class)

Date: 2026-10-14. Status: **study only — no implementation, no build or
runtime change.** Goal: replace the guidance-only hardware bands in
[`docs/hardware.md`](hardware.md) with a measured answer to "what is the
actual minimum hardware for gX?", using a single reference machine as the
only anchor: a 6-core/12-thread Zen1-class CPU, 32 GiB RAM, and an NVIDIA
GeForce GTX 1060 6 GB.

Sources: this tree's benchmark hooks (`GX_BENCHMARK_CPU_CSV`,
`--gfx-profile`, `GX_BENCHMARK_UNCAPPED`, `GX_FIXED_DT`, `GX_FRAMES`),
`scripts/gfx_benchmark.py` + the pinned reference
`docs/gfx_benchmark_reference.json`, `scripts/audit_common.py`, a
source-side render-target inventory, and external measurements kept outside
the repository (driver scripts and per-run CSV/JSON artifacts).

**Verdict up front**: the shipped default showcase (Ultra preset: 4x MSAA,
SSR, SSAO/SSGI, TAA, bloom, CSM) needs roughly a **GTX 1060 6 GB class**
GPU for 60 fps at 1024x768 — the reference card itself sits *at* that line
(75 fps in a quiet state, ~57 fps in this session's contended desktop).
The docs' old "minimum" band (GTX 460 / HD 4000) is a *feature* floor, not
a 60 fps floor; with the showcase effects disabled those cards become
plausible (model). Details and the model below.

## 0. The reference class

| | |
|---|---|
| CPU | 6-core/12-thread Zen1-class, 3.2-3.6 GHz |
| RAM | 32 GiB DDR4 |
| GPU | NVIDIA GeForce GTX 1060 6 GB (reference class) |
| OS/desktop | Linux, KDE/Wayland with Xwayland (`DISPLAY=:1`) |
| Binary | Release build, pinned benchmark harness |
| Scene under test | `--showcase-minimal` (the benchmark contract scene) with its showcase defaults; `--showcase`, `--gfx`, `--title`, `--raymarch`, `--physics` additionally |

Contention caveat: the engine ran alongside a desktop compositor. The pinned
reference from 2026-09-20 (`docs/gfx_benchmark_reference.json`: default GPU
p95 13.34 ms) is treated as the quiet anchor; this session reproduces the
harness gate (PASS) but measures the default row 8-39% slower per row
(isolated +8%, SSR +36%, TAA +26%, default +39%). Both the desktop state and
post-pin feature work are candidate causes; the study carries a
contended/quiet range wherever it matters.

## 1. Method

- Ladder runs use the engine's own hooks: `GX_FIXED_DT=1/60` (deterministic
  sim), `GX_BENCHMARK_UNCAPPED=1`, `GX_FRAMES`, per-frame CPU ms CSV and
  the `--gfx-profile` GPU ms CSV; hidden 1024x768 window
  (`GX_AUDIT_WINDOW=hidden`), seed 424242, medians over the measured tail
  (2 repeats x 360 frames, after 150 warmup frames).
- While each run executes, the driver samples process RSS (psutil) and the
  GPU (SM/mem clocks, util, memory, temp, power).
- `cpu_ms` is wall time around engine update + draw per frame; at the
  resolution ladder it tracks GPU time 1:1 once the GPU is saturated
  (frame-pipeline backpressure), so it is an upper bound on engine CPU
  work, and its p95 exposes periodic stalls.
- GPU per-pass columns (`shadow/reflections/opaque/bloom/ssr`) cover only
  ~25% of the frame at defaults; feature attribution below therefore uses
  isolated single-feature rows (`GX_T_*` overrides) instead.
- `scripts/gfx_benchmark.py --reference` was run as the drift check against
  the pinned baseline (PASS, budgets unchanged).

Limits (falsification plan in section 4): single rig; no clock-locked or
weaker-GPU validation (no root); one desktop-session state; static camera;
software-GL timer values unreliable, so llvmpipe is cited qualitatively.

## 2. Measured results

### 2.1 Resolution ladder (`--showcase-minimal` defaults)

Medians, avg of 2 repeats; CPU and GPU in ms:

| Resolution | CPU ms | GPU ms | pixels |
|---|---|---|---|
| 320x240 | 3.93 | 4.33 | 76.8k |
| 512x384 | 6.45 | 6.69 | 196.6k |
| 640x480 | 8.42 | 8.54 | 307.2k |
| 800x600 | 11.44 | 11.72 | 480.0k |
| 1024x768 | 17.30 | 18.22 | 786.4k |
| 1280x720 | 18.78 | 19.75 | 921.6k |
| 1600x900 | 28.71 | 28.94 | 1.440M |
| 1920x1080 | 37.03 | 36.80 | 2.074M |

Least-squares fits (`ms = intercept + slope x pixels`, R2 >= 0.99):

- **defaults**: GPU `3.84 ms + 16.60 ns/px`; CPU `3.33 ms + 16.78 ns/px`
  (i.e. CPU is saturated by the GPU at every rung — visible work is at
  most ~3.3-4 ms on this CPU, the rest is frame-pipeline backpressure).
- **isolated** (all showcase effects off): GPU `1.75 ms + 2.54 ns/px`;
  CPU `1.82 ms + 2.62 ns/px`.

So the default effect stack multiplies the per-pixel cost ~6.5x over the
plain forward path, and the fixed (resolution-independent) part is small
(~2-4 ms: shadow maps, cubemap captures, planar capture).

### 2.2 Scene suite (1024x768, defaults, medians)

| Scene | CPU ms | GPU ms | RSS MB |
|---|---|---|---|
| `--showcase-minimal` | 17.3 | 18.0 | 184 |
| `--showcase` (full) | 23.1 | 25.5 | 187 |
| `--gfx` | 3.7 | 3.5 | 165 |
| `--title` (GUI only) | 0.8 | n/a | 149 |
| `--physics` | 4.3 | 2.2 | 165 |
| `--raymarch` | 0.3 (deep queue) | ~1.1 (module timer, renderScale 0.35; 4.4 at 1.0 per hardware.md) | 162 |

The full showcase is the heaviest shipped scene (~1.4x the minimal one);
GUI/title and the physics sandbox are trivial at this resolution. The CPU
column is the showcase's pipeline backpressure again; physics is the one
scene whose CPU cost (~4.3 ms) exceeds its GPU cost.

### 2.3 Feature attribution (isolated + one feature, 1024x768 GPU medians)

| Feature increment | GPU ms |
|---|---|
| SSR | **+10.78** |
| SSAO/GTAO (+SSGI chain) | +1.16 |
| TAA | +0.28 |
| Bloom (intensity 6) | +0.07 |
| Motion blur / rain / snowfall / probes | <= +0.1 (noise band) |

SSR alone is ~60% of the default frame on this class of GPU: it is the
single most valuable knob for weak hardware.

### 2.4 CPU and frame pacing

- Engine CPU work is bounded by the isolated row: **<= 2.6 ms** per frame
  at 640x480 and <= 3.9 ms at 320x240 with defaults, on a Zen1 core at
  3.2-3.6 GHz. A CPU around 4x slower single-thread would still fit the
  16.7 ms budget of the minimal showcase; the GPU binds long before the
  CPU does.
- **Auto-exposure readback stall**: CPU p95 is consistently ~2x the median
  (34.8 vs 17.0 ms at 1024x768 defaults) with a ~10-frame period. With
  `GX_NO_AE=1` the p95 drops to 18.7 ms (max 20.1) at the same settings.
  Every 10th frame waits for the luminance readback and roughly doubles
  that frame; this is engine-side pacing, independent of the GPU class.
- Uncapped runs are genuinely uncapped (no vsync floor: isolated medians
  fall to 3.9 ms), and hidden windows mean no compositor throttling of the
  driver loop; the 1:1 CPU/GPU medians above are driver/queue backpressure.

### 2.5 Memory

- Process RSS (Release, whole run): 149 MB (title) to 192 MB (1080p
  showcase). Development builds are larger; 2 GB RAM is enough, 4 GB
  comfortable.
- VRAM, structural inventory at 1024x768 defaults (`--showcase-minimal`,
  4x MSAA on): **~209 MiB** of color+depth render targets (largest single
  item: the 4x MSAA scene pair at 36 MiB; the HDR scene + LDR/refraction
  copies + cube captures + CSM array + SSR/TAA/SSAO chains account for the
  rest). About 150 MiB of that is resolution-scaled, so 1080p is ~455 MiB
  structural.
- VRAM, observed `nvidia-smi` deltas (system-wide, includes driver
  overhead and desktop): +349..506 MiB across the 1024x768 scenes, ~970
  MiB at 1080p defaults. A 1 GB card runs the 1024x768 showcase; 2 GB is
  the safe recommendation once 1080p or the full opt-in set is in play.
- One allocation can break a 1 GB card on its own: the point-shadow array
  is all-or-nothing — `gX_GFX_POINT_SHADOW_ARRAY_SIZE 512`,
  `gX_GFX_MAX_POINT_SHADOW_LIGHTS 64`, D24 x 6 x 64 layers = **384 MiB**
  the moment any shadow-casting point light is enabled
  (`src/renderer/gfx/constants.h:119-120`, `light.c`). No shipped default
  scene triggers it (deltas above confirm), but an edited preset/light can.
- Software GL (Mesa llvmpipe, forced and verified by renderer string): even
  with every effect off, 320x240 settles at ~50 ms/frame after multiple
  seconds of per-shader LLVM compilation (frames 2-3 took 8.8 s / 6.1 s).
  llvmpipe timers are not trustworthy, but the qualitative verdict is
  solid: software rendering cannot host the showcase.

## 3. The floor (model)

Method: measured 1060 frame cost `T(px)` (defaults fit above) is scaled by
a card factor `k = sqrt(FP32 ratio x texel-rate ratio)` against nominal
best-boost specs; the two anchors are this session (contended: 16.9 ms at
1024x768) and the pinned quiet state (12.4 ms, i.e. x0.73). `k` values are
nominal-spec estimates, not measurements — treat them as +/- one class.

| Card (nominal) | k (x1060 cost) | defaults @1024x768 | SSR off | all effects off |
|---|---|---|---|---|
| GTX 1660 SUPER | 0.87 | 15 / 11 ms (68 / 93 fps) | 5 / 4 ms | 3 / 2 ms |
| GTX 950 | 2.4 | 41 / 30 ms (25 / 34 fps) | 15 / 11 ms (68 / 93) | 9 / 7 ms |
| GTX 750 Ti | 3.2 | 53 / 39 ms (19 / 26) | 19 / 14 ms (52 / 71) | 12 / 9 ms (85 / 116) |
| RX 550 | 3.6 | 61 / 45 ms | 22 / 16 ms (45 / 62) | 13 / 10 ms |
| GTX 460 1 GB | 4.2 | 70 / 52 ms (14 / 19) | 25 / 19 ms (39 / 53) | 16 / 11 ms (64 / 87) |
| HD 4000 (iGPU) | 15 | 253 / 185 ms (4 / 5) | 91 / 67 ms | 56 / 41 ms (18 / 24) |

Required capacity at 60 fps (= measured 1060 cost / 16.67 ms), as a
multiple of a GTX 1060:

| Configuration | Required x1060 (contended / quiet) |
|---|---|
| defaults @1024x768 | **1.0x / 0.74x** — a 1060 exactly, no margin |
| SSR off @1024x768 | 0.37x / 0.27x — GTX 950/750 Ti class |
| all effects off @1024x768 | 0.22x / 0.16x — GTX 460 class borderline |
| defaults @1600x900 | 1.7x / 1.3x — GTX 1070/1660 Ti class |
| defaults @1920x1080 | 2.3x / 1.7x — GTX 1080/RTX-class |

**Answers**:

- **Feature floor (boots and runs the demos at any frame rate)**: desktop
  GL 3.3 + FBOs (the certified floor), ~1 GB VRAM, 4 GB RAM, any
  2010-era dual-core. HD 4000-class iGPUs qualify and will render the
  showcase slowly (model: ~5 fps at defaults, ~18-24 fps with effects off).
- **60 fps floor, shipped defaults @1024x768**: GTX 1060 6 GB class —
  measured, not modeled (75 fps quiet / ~57 fps contended on the reference
  card). For a margin, one class up (GTX 1660 SUPER / GTX 1070).
- **60 fps floor with SSR off**: GTX 950 / GTX 750 Ti class (model;
  quiet-side GTX 460 possible but not comfortable).
- **30 fps floor, defaults @1024x768**: ~GTX 950 class (model).
- **CPU**: not the constraint for the demo scenes; anything within ~4x of
  a Zen1 3.6 GHz core. Physics/multi-body scenes raise the CPU floor.
- **RAM/VRAM**: 4 GB RAM; 1 GB VRAM at 1024x768, 2 GB for 1080p or
  point-shadow/cookie-heavy presets.

## 4. Open probes (not run; would raise confidence)

1. **Clock-locked validation** (needs root; exact recipe):
   baseline `nvidia-smi -q -d CLOCK`; lock `sudo nvidia-smi -lgc 400,800`
   (and optionally `-pl 60`), rerun the measurement driver on the
   `res-defaults` matrix and compare predicted vs observed scaling; restore with
   `sudo nvidia-smi -rgc`. This validates the k-factor model on one axis
   (clocks), not across architectures.
2. **Real weak-hardware validation** (GTX 750 Ti / GTX 460 / HD 4000 or
   equivalent): required before any published minimum-spec claim.
3. **Contention isolation**: rerun the pinned reference with the desktop
   quiet (no Firefox, compositor idle) to re-anchor the quiet column.
4. **Raymarch profile** at renderScale 0.35/1.0 via `--raymarch-profile`
   if the module scenes enter the support statement.

## 5. Follow-ups found while measuring (no code changed)

- The auto-exposure readback hitch (section 2.4) is a genuine pacing
  defect at the 60 fps boundary on *any* hardware; candidates: split the
  64x64 luminance reduction across frames, or move the readback off the
  loop with a fence. Deserves a backlog entry before it is called a bug.
- `docs/hardware.md` tiers should be labeled as *feature* tiers with a
  pointer to this study for performance bands.
- The current tree is 8-39% slower than the 2026-09-20 pinned reference on
  this reference class for GPU-heavy rows; attribute (desktop state vs
  post-pin feature cost) before touching the reference budgets or the lean
  story.
- The point-shadow array could allocate lazily per light or per face
  instead of 384 MiB up front; noted, not actioned.

## See also

- [`hardware.md`](hardware.md) — runtime feature tiers, `--hw-report` and
  its gate; this study supplies the measured performance bands.
- [`architecture.md`](architecture.md) — renderer feature list and the
  effect presets; [`raymarch.md`](raymarch.md) — the raymarch cost ladder.
- [`docs/gfx_benchmark_reference.json`](gfx_benchmark_reference.json) —
  the pinned same-machine baseline used as the quiet anchor.
- Measurement artifacts were kept outside the repository.
