# Speculative ideas — not a roadmap

<!-- //ai: speculative idea register; entries are not planned or promised. -->

Status: **speculative idea ledger**. An entry means only that the idea is
theoretically possible within gX's clean-room, technical and budget
constraints. It does **not** mean it will ever be implemented; most entries may
stay here forever. Nothing here is planned, approved, scheduled or promised,
and nothing is authorization to implement. A normal idea row in
[`ideas.md`](ideas.md) plus explicit owner approval is required before code or
content work starts.

Keeping them has value: the rationale and constraints survive sessions, the
same proposal is not re-litigated from scratch, and contributors can explore a
concept without implying a release commitment.

## Rules

- Clean-room only. No proprietary middleware, assets, animations, rigs, data
  or extracted material may enter gX. Public high-level ideas are references,
  not code to copy.
- Original names and terms stay original. A speculative entry must not imply
  affiliation with, or endorsement by, a third party.
- An entry is not a gate, a budget exception or a promise of support. If a
  concept is promoted, it gets a normal `GX-I###` row with scope, owner
  approval, gates and its own lean/performance budget.
- A declined or parked entry keeps its reason and revisit condition. Do not
  reopen it merely because a session is new.

## Sourcing

Entries may be seeded by a high-level review of the author's private gameX
design and engineering notes. Only paraphrased, engine-level concepts are
recorded here: no private paths, story content, assets, code or third-party
material. A gameX-side idea still needs its own public rationale, clean-room
implementation and owner promotion before it becomes work.

## Register

| ID | Idea | Status | Notes |
| --- | --- | --- | --- |
| S-001 | Embodied NPC (ENPC) | not implemented; no plan | An original gX term for an embodied character that explores the world, interacts with it, or joins the player as a party member. Design shorthand: **"Euphoria-style active body + Nextbot-style simple agency"** — behavioral references only; no third-party middleware, code, data, constants or assets, and no affiliation or endorsement. It does **not** imply a neural network, sentience or general intelligence; it is a game entity with authored behavior. The body-first contract is in [`active_ragdoll_lab.md`](active_ragdoll_lab.md); the visual lab, hybrid locomotion and ENPC behavior are not implemented, and the contract keeps ENPC behavior out of the lab. Revisit only after the hybrid controller, get-up state machine and performance budget exist, and the owner explicitly promotes it. |
| S-002 | Distributed role split for multiplayer | not implemented; no plan | Split one game across machines — for example a render node, a physics node, an AI node and a simulation authority — instead of running everything on one host. The realistic first form is an authoritative simulation plus one or more render clients, not fine-grained per-subsystem ownership: physics, AI and gameplay state are tightly coupled, so splitting them adds consistency, ordering and rollback problems before it adds throughput. Hard problems: frame pacing under LAN latency; one owner for mutable truth; cross-platform floating-point determinism (already flagged in [`battle_multiplayer_plan.md`](battle_multiplayer_plan.md)); GPU contexts cannot be shared across machines; join/leave/failure handling and host migration; serialization and time synchronization; and a network security boundary that does not exist today. LAN-first and opt-in; no public deployment, accounts, matchmaking or security promise. Revisit only with a headless authority, a fixed-role LAN prototype, a latency/failure budget and explicit owner promotion. |
| S-003 | Wavelet / wave-based graphics renderer | not implemented; no plan | Two distinct ideas share the name. **(a) Wavelet representation:** use wavelet transforms for radiance/lightmap compression, hierarchical GI, denoising or screen-space filtering. This is plausible as an acceleration/compression layer, not a replacement for the raster renderer. **(b) Wave-optics rendering:** propagate scalar fields (diffraction, interference, holography) instead of rays. This is not a candidate for the 60 Hz PBR path: cost scales with resolution and scene extent, needs FFT/propagation plus boundary and material models, and is best suited to offline or stylized diffraction captures. The existing PRISM-1 aperture diffraction in [`console_240p.md`](console_240p.md) is an analytic approximation, not a wave solver. First promotion candidate: a deterministic offline lab (wavelet GI/denoise or a scalar-diffraction still of a tiny scene) with energy/variance/NaN checks and a measured quality-per-cost win. Clean-room; own or clearly licensed transform code only. Revisit only with a concrete win over the existing path and owner approval. |
| S-004 | Complete path tracer (Cycles-style) | not implemented; no plan | A production path tracer replacing the raster/raymarch path: BVH over scene geometry, physically based materials and lights, next-event estimation/MIS, BSDF sampling, denoising, progressive accumulation, CPU/GPU scheduling and a deterministic capture contract. This is renderer-scale work, not a feature: Cycles-class parity implies a large material/lighting surface, a denoiser and long tuning, and it conflicts with the lean/raylib-only beta and current frame/VRAM budgets. The realistic first slice is an **opt-in progressive path-trace lab**, not a replacement: raster primary visibility, 1 spp/frame accumulation, a tiny analytic/GLB scene, fixed seed, 320x180 or 640x360, variance/NaN gates and raw-versus-patterned presentation. Full-scene BVH, production materials, denoising and real-time budgets are later gates. The name is descriptive; no Blender/Cycles affiliation or code. Keep the default renderer unchanged. Revisit only with a measured quality win, a hard frame/VRAM budget and owner approval. |
| S-005 | Vulkan backend | not implemented; no plan | An explicit Vulkan RHI path (pipelines, descriptor sets, memory, synchronization, SPIR-V) instead of the OpenGL surface. gX retains Vulkan dialect machinery and a SPIR-V corpus probe, but the [backend study](backends.md) measured only 1/153 shader files portable as-is and identified ~220 RHI entry points plus ~98 direct GL sites that would need re-routing, with deferred recording and per-draw descriptor rings as the structural cost. It conflicts with the raylib-only beta and has no demand signal. Parked; revisit only with a named product need, a maintained platform layer, a fully rewritten shader corpus and a dedicated backend budget/gate. |
| S-006 | Retro console backends | not implemented; no plan | N64/PS1/PS2-style targets with fixed memory, command-list or fixed-function rendering, palette/quantization, fixed-point math and separate asset/scene budgets. The author's gameX work explored these as engineering exercises; gX's scope is engine/tooling, not console ports, and [platform support](platforms.md) lists consoles as out of scope. A port would need a public homebrew toolchain, a self-contained demo, a hard memory/frame budget and no impact on the default build or lean caps. No proprietary SDKs, assets or extracted content. Parked; revisit only with a named homebrew target and explicit owner approval. |
| S-007 | Self-modifying code segment / runtime code generation | not planned; security-sensitive, platform-gated | A separate writable+executable `text2`-style segment, or patching already-executed code at runtime. **Scope:** only a platform layer with no W^X/NX and no process isolation (retro console, bare-metal, DOS-style) may ever use this; hosted Linux/Windows/macOS and the default desktop build must not. "Complete memory access" is not a green light: it means the platform cannot enforce W^X, so code/data aliasing remains a correctness risk, there is no privilege boundary, and untrusted input or a bad write can still corrupt or brick the program. This is still the wrong abstraction for the core: `text`, `data` and `bss` are section/segment names, and a new segment does not create a safe boundary. A retro backend should own a platform-specific overlay/code pool (linked to S-006), not a generic engine `text2`. On hosted systems, W+X is blocked or audited by kernels, hardened runtimes, CFI and antivirus, and any write primitive becomes code execution. If runtime generation is ever needed there, use an opt-in W^X JIT pool (dual RW/RX mappings or RW->RX transitions), cache flushes, strict validation, no untrusted input and a W^X audit gate. Prefer precompiled code, bytecode, GPU shaders or dynamic modules. Revisit only with a named platform target, a measured hot path, a security review and owner approval. |
| S-008 | General-relativity / spacetime lab | not implemented; no plan | A visualization/numerical lab for curved spacetime, distinct from the existing **special**-relativity applier (`gX_PhysicsRelativity_*`: a speed clamp and gamma mass scaling on top of ODE). A realistic slice is a fixed Schwarzschild metric with test-particle geodesics, gravitational time dilation/redshift and known-orbit checks (precession, photon ring), rendered as trails; weak-field approximations could later inform gameplay. Full numerical relativity, dynamical spacetime, matter coupling and tensor-field rendering are out of scope. Hard problems: 4D tensor maths, coordinate singularities, stable integrators, units and performance; it is not an ODE feature. Revisit only with a self-contained deterministic lab, analytic/oracle tolerances and owner approval. |
| S-009 | Quantum-mechanics / wavefunction lab | not implemented; no plan | A visualization/numerical lab for wave mechanics, not a replacement for the rigid-body solver: a 1D/2D wavefunction evolved with split-step Fourier or Crank-Nicolson, showing free-packet spreading, wells, tunneling, two-slit interference or harmonic modes; a small qubit/gate toy is a separate possible slice. Hard problems: complex arithmetic, FFT/grid cost, boundary conditions, normalization, measurement/decoherence and scale; GPU compute may be needed for larger grids. The classical sandbox stays unchanged. First slice would be deterministic 1D/2D cases with analytic tests (well eigenvalues, packet width, tunneling probability) and an existing-renderer view. Revisit only with a measured educational/visual win and owner approval. |
| S-010 | NP-hard optimization lab | not implemented; no plan | An offline lab for engine problems whose exact/optimal form is NP-hard or combinatorial. Candidates include render-graph/pass scheduling, draw-call batching, atlas/lightmap packing, shadow-casting light selection, LOD selection, shader-permutation selection, visibility set selection, frame-task scheduling, multi-agent/coverage navigation, gate/test selection and optimal BVH construction. Exact solving is only for tiny instances; runtime must stay heuristic and P-first per the [runtime complexity policy](architecture.md#runtime-complexity-policy-2026-11-13). Compare exact/ILP/CP-SAT/brute force against the existing greedy/list heuristic on generated instances, report the optimality gap and runtime, and add no solver dependency to the default build. See the candidate queue below. |

### NP-hard candidate queue (S-010)

These are optimization problems, not claims that the current engine is doing
them badly. Most already have practical heuristics; the lab would measure how
far those heuristics are from a small exact solution.

- **Render-graph / pass scheduling** — precedence and resource hazards plus
  target reuse; list scheduling vs exact for small graphs.
- **Draw-call batching / instance grouping** — bin packing and clustering.
- **Atlas / lightmap / tile packing** — 2D rectangle and bin packing.
- **Shadow-casting light and light-list selection** — set cover or knapsack
  under a shadow/light budget.
- **LOD and budgeted mesh selection** — multiple-choice knapsack under
  triangle/VRAM limits; simplification itself stays heuristic.
- **Shader permutation / feature selection** — set cover or knapsack under
  variant and build-time budgets.
- **Visibility / occlusion set selection** — set cover; single-ray visibility
  is not NP-hard, so state the problem precisely.
- **Frame-task scheduling** — precedence-constrained multiprocessor
  scheduling.
- **Multi-agent / coverage navigation** (ENPC) — collision-free multi-agent
  planning or coverage, not single-agent shortest path.
- **Gate/test selection under a time budget** — knapsack or set cover.
- **Optimal BVH/tree construction** — exact optimal trees are combinatorial;
  production builders use greedy SAH-style heuristics.

## Template

```text
ID: S-###
Idea / search terms:
Status: speculative / parked / declined
Why it is hard:
Clean-room constraints:
What would promote it:
Revisit only if:
```

## Promotion

Promotion is a separate decision. Create a `GX-I###` entry in
[`ideas.md`](ideas.md), name the smallest contract and gate, and record the
lean/performance cost. Until then, do not add runtime code, resources, scenes
or public claims for a speculative entry.
