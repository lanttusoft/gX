# Speculative ideas — not a roadmap

<!-- //ai: speculative idea register; entries are not planned or promised. -->

Status: **idea register only**. Entries here are deliberately speculative or
very hard. They are not planned, approved, scheduled or promised, and they are
not authorization to implement them. A normal idea row in
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

## Register

| ID | Idea | Status | Notes |
| --- | --- | --- | --- |
| S-001 | Embodied NPC (ENPC) | not implemented; no plan | An original gX term for an embodied character that explores the world, interacts with it, or joins the player as a party member. Design shorthand: **"Euphoria-style active body + Nextbot-style simple agency"** — behavioral references only; no third-party middleware, code, data, constants or assets, and no affiliation or endorsement. It does **not** imply a neural network, sentience or general intelligence; it is a game entity with authored behavior. The body-first contract is in [`active_ragdoll_lab.md`](active_ragdoll_lab.md); the visual lab, hybrid locomotion and ENPC behavior are not implemented, and the contract keeps ENPC behavior out of the lab. Revisit only after the hybrid controller, get-up state machine and performance budget exist, and the owner explicitly promotes it. |
| S-002 | Distributed role split for multiplayer | not implemented; no plan | Split one game across machines — for example a render node, a physics node, an AI node and a simulation authority — instead of running everything on one host. The realistic first form is an authoritative simulation plus one or more render clients, not fine-grained per-subsystem ownership: physics, AI and gameplay state are tightly coupled, so splitting them adds consistency, ordering and rollback problems before it adds throughput. Hard problems: frame pacing under LAN latency; one owner for mutable truth; cross-platform floating-point determinism (already flagged in [`battle_multiplayer_plan.md`](battle_multiplayer_plan.md)); GPU contexts cannot be shared across machines; join/leave/failure handling and host migration; serialization and time synchronization; and a network security boundary that does not exist today. LAN-first and opt-in; no public deployment, accounts, matchmaking or security promise. Revisit only with a headless authority, a fixed-role LAN prototype, a latency/failure budget and explicit owner promotion. |
| S-003 | Wavelet / wave-based graphics renderer | not implemented; no plan | Two distinct ideas share the name. **(a) Wavelet representation:** use wavelet transforms for radiance/lightmap compression, hierarchical GI, denoising or screen-space filtering. This is plausible as an acceleration/compression layer, not a replacement for the raster renderer. **(b) Wave-optics rendering:** propagate scalar fields (diffraction, interference, holography) instead of rays. This is not a candidate for the 60 Hz PBR path: cost scales with resolution and scene extent, needs FFT/propagation plus boundary and material models, and is best suited to offline or stylized diffraction captures. The existing PRISM-1 aperture diffraction in [`console_240p.md`](console_240p.md) is an analytic approximation, not a wave solver. First promotion candidate: a deterministic offline lab (wavelet GI/denoise or a scalar-diffraction still of a tiny scene) with energy/variance/NaN checks and a measured quality-per-cost win. Clean-room; own or clearly licensed transform code only. Revisit only with a concrete win over the existing path and owner approval. |
| S-004 | Complete path tracer (Cycles-style) | not implemented; no plan | A production path tracer replacing the raster/raymarch path: BVH over scene geometry, physically based materials and lights, next-event estimation/MIS, BSDF sampling, denoising, progressive accumulation, CPU/GPU scheduling and a deterministic capture contract. This is renderer-scale work, not a feature: Cycles-class parity implies a large material/lighting surface, a denoiser and long tuning, and it conflicts with the lean/raylib-only beta and current frame/VRAM budgets. The realistic first slice is an **opt-in progressive path-trace lab**, not a replacement: raster primary visibility, 1 spp/frame accumulation, a tiny analytic/GLB scene, fixed seed, 320x180 or 640x360, variance/NaN gates and raw-versus-patterned presentation. Full-scene BVH, production materials, denoising and real-time budgets are later gates. The name is descriptive; no Blender/Cycles affiliation or code. Keep the default renderer unchanged. Revisit only with a measured quality win, a hard frame/VRAM budget and owner approval. |

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
