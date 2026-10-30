# Active-ragdoll lab — body-first contract

<!-- //ai: 2026-10-22 clean-room hybrid active-ragdoll contract; no Euphoria code. -->

Status: contract + Phase 1 headless controller (2026-10-22). The visual lab,
hybrid locomotion and "DMNPC" behavior are not implemented. The passive/default
ragdoll path is unchanged.

## Purpose

Define the smallest reusable physical body that can later support a
"DMNPC"-like entity: an embodied character that explores a world and interacts
with it.

This is **clean-room** work. It does not reverse-engineer, decompile, extract
or copy Euphoria, RAGE, gameX or any proprietary animation/physics middleware.
No RAGE/Euphoria code, constants, data, animations or gameX assets may enter
gX. Public high-level ideas (procedural animation, active ragdolls, balance,
behavior trees) are fair references; the implementation and tuning stay
original.

The quoted term "DMNPC" is a descriptive reference to the entity archetype
associated with INTERLOPER ARG (Anomidae). The lab only aims for ragdolls that
behave like those entities — an embodied character that walks, balances, falls
and recovers — and uses no INTERLOPER/Anomidae code, assets, narrative or
character material.

## Decision

Build the body in two layers and keep them separate:

- **Engine-side body/control**: existing ragdoll articulation plus a
  deterministic controller (joint targets, balance, contact handling,
  get-up).
- **Game-side entity behavior**: perception, navigation, affordances,
  interaction, goals and "DMNPC" behavior. Not part of this lab.

The first body is **hybrid**, not a pure ragdoll walker:

- animate/kinematic locomotion is the normal path later;
- active balance and foot/contact correction run underneath;
- full ragdoll takes over on impacts, falls and loss of support;
- get-up and blend back to animated control.

Pure active-ragdoll walking is deferred. It is expensive, unstable and hard to
read at distance; it is not required to prove an embodied body.

## What already exists

- `src/physics/ragdoll.h` / `src/physics/ragdoll.c`: 11-bone humanoid
  (capsules/spheres, ball/hinge joints), self-collision filtering, joint swing
  bands, reset/enable/destroy.
- `src/physics/physics.h` / `src/physics/physics.c`: optional ODE world, fixed
  60 Hz step and accumulator, sub-step cap, per-step hook.
- `src/wrappers/physics.h` / `src/wrappers/physics.c`: opaque body/joint API,
  ball/hinge/AMotor, motors, feedback, collision callbacks and ODE stubs when
  ODE is absent.
- `gX_Tests_Physics_Ragdoll` and the broader physics suite: creation, T-pose,
  reset, settle, disable, destroy, determinism.
- `--physics` sandbox: passive ragdolls, rendering, picking, impact effects.

What is missing: active PD targets, balance/contact control, get-up state
machine, animation/ragdoll handoff, and an authored visual lab.

## Body contract

### Rig

- Start with the existing 11-bone rig; do not add a second skeleton format.
- Visuals are engine primitives (capsules/cubes) and debug overlays only.
- Target joint poses are procedural; no animation assets are required for the
  first slice.
- The controller owns joint target angles and bounded torque/velocity, not
  direct body teleportation.

### Controller

- Runs once before each fixed physics step through the existing per-step hook
  or an equivalent ODE-side callback; never from render dt.
- PD targets per articulated joint; bounded torque, angular velocity and
  correction.
- Balance input from center of mass, support/contact state and a desired
  upright direction.
- Modes: `PASSIVE`, `ACTIVE_UPRIGHT`, `FALLING`, `GETTING_UP`. Walking,
  steering and interaction are later modes.
- All state is finite and bounded; no NaN/Inf reaches ODE. Missing ODE or
  `GX_USE_ODE=OFF` must leave the rest of the engine stubbed and functional.
- No per-frame allocation; all arrays are fixed-size.

### Handoff

- Animation/kinematic pose is a target, not a hard body transform.
- Impact or loss of support switches active control to full ragdoll.
- Get-up is a timed sequence with contact checks, not a single snap.
- Returning to animated control requires a stable support/upright test.
- Do not blend by teleporting bodies; use bounded targets/weights.

### Determinism and budget

- Same seed, inputs and fixed-step sequence must produce bit-identical body
  state for the lab scenario.
- One active ragdoll in the first lab; existing physics caps remain.
- At most one active controller evaluation per fixed step per ragdoll.
- The lab is opt-in. Nothing in the default title/gfx/battle scenes changes.
- No active-ragdoll body in the shipped Release payload until a separate
  ship-profile decision.

## Lab and tooling

### Phase 1 — headless body

**Implemented:** `gX_Ragdoll_Control` in `ragdoll.h`/`ragdoll.c` records the
creation pose and mass, applies bounded PD torque plus a pelvis support force,
and runs `PASSIVE`/`ACTIVE`/`FALLING`/`GETTING_UP` modes before a fixed step.
The passive ragdoll path is unchanged.

Current tests in the existing physics suite cover:

- upright hold under gravity;
- one bounded torso impulse;
- controlled fall and get-up;
- null initialization (ODE-off builds skip the body test);
- same-state repeat comparison.

Still open: four-direction impulse matrix, per-step debug trace, joint/self-
collision stress matrix, and disable/destroy assertions inside the active
controller test. No scene, renderer, GUI or asset changes yet.

### Phase 2 — dev-only visual lab

The placement decision follows Phase 1:

- preferred: a dev-only lab scene or module that draws the existing primitive
  rig and debug trace; loaded/entered explicitly;
- not a shipped default scene;
- no game content or gameX assets;
- the author reviews body behavior, not the placeholder visuals.

A module is attractive for lean payload, but the current module ABI does not
expose the physics handles; the contract must either extend the ABI or use an
engine-only dev scene. That decision is deferred until Phase 1 proves the
controller.

### Phase 3 — hybrid locomotion

- animated target + balance correction;
- foot/contact support and simple room/portal movement;
- no navmesh or perception yet.

### Phase 4 — entity behavior (game-side)

- perception, affordances, goals, object interaction, schedules, memory;
- navigation over the room/portal graph or a later navmesh;
- narrative/authoring data stays in the game layer.

## Acceptance

The first body slice is accepted when:

- one ragdoll holds upright, recovers from bounded impulses and gets up from a
  controlled fall;
- all fixed-step traces are finite, repeatable and within joint/contact caps;
- disabling the controller returns the existing passive ragdoll behavior;
- ODE-off builds and their physics tests still pass;
- no renderer/GUI/scene default changes are required for the headless slice;
- the author signs off the visual lab when it exists.

Do not lower a test bound to hide a fall, explosion or support failure.

## Risks and stop conditions

- ODE solver instability or contact explosions: reduce scope, fix the body,
  never hide with damping/threshold changes.
- Pure ragdoll locomotion becomes the only option: stop and revisit hybrid
  animation targets.
- Controller needs a second skeleton/format: stop; extend the existing rig.
- Module ABI cannot expose physics safely: use a dev scene or extend the ABI
  deliberately.
- Performance or lean budget regresses: keep it opt-in and measure.
- Any legal/provenance concern about proprietary animation/physics material:
  stop; replace with clean-room work.

## Non-goals

- Reverse engineering Euphoria/RAGE or extracting proprietary middleware.
- Reusing gameX animations, models, rigs or code.
- A general-purpose behavior tree/navmesh/affordance system in this lab.
- "DMNPC" gameplay, combat, or narrative integration.
- A default or shipped active-ragdoll scene.
