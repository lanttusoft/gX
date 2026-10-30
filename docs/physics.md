# gX — Physics

Rigid-body physics, resurrected from gameX (feature-first per
[`porting.md`](porting.md)). Three layers, each with one job:

| Layer | Files | Job |
|---|---|---|
| Wrapper | `src/wrappers/physics.{h,c}` | Backend-agnostic rigid-body API over opaque handles; ODE implementation + no-op stubs |
| Module | `src/physics/physics.{h,c}` | Shared simulation world; fixed timestep, sub-step cap, auto-tuning |
| Ragdoll | `src/physics/ragdoll.{h,c}` | 11-bone humanoid articulation (capsules/spheres + ball/hinge joints) on the wrapper |
| EM | `src/physics/em.{h,c}` | Charged-body fields + magnetic dipoles: softened Coulomb, uniform-B Lorentz, dipole pair forces/torques, per fixed step |
| Thermal | `src/physics/thermal.{h,c}` | Body temperature: impact heating + radiative T⁴ cooling + glow |
| Fluid | `src/physics/fluid.{h,c}` | Still-water region: Archimedes buoyancy + drag per fixed step, surface entry events |
| Elements | `src/physics/elements.{h,c}` | 12-element table (symbol, Z, molar mass, density, c_p, melting point, emissivity, contact friction/restitution/rolling) |
| Scene | `src/scene/physics.{h,c}` | `--physics` props/ragdoll sandbox: spawns bodies, syncs transforms to GFX models |

## Build

`GX_USE_ODE` (CMake, default ON) links the system ODE when found:

- found — wrapper compiles its ODE implementation (`-DGX_USE_ODE`);
- missing or `-DGX_USE_ODE=OFF` — the wrapper's stub branch keeps every other
  subsystem building; physics tests skip with a NOTICE and the sandbox scene
  renders static props only.

The wrapper and module are always compiled; the sandbox scene rides the
renderer/scene cluster (dropped in kernel-only builds) and the integration
tests are Development-gated like the rest of the suite. The physics world is
created in `main_load` (`gX_Physics_Init`), stepped in `main_loop` before
scene updates, and torn down in `main_unload`. The physics demo cold paths
(sandbox/ragdoll/EM/elements/thermal/fluid) compile at `-Oz` in Release to
keep the lean gate honest; the shared world module stays O3.

## Stepping contract

- Fixed 60 Hz steps from a frame-time accumulator; the accumulator is capped
  (0.05 s) and at most `maxSubSteps` steps run per frame, so a hitch cannot
  spiral the solver.
- Contact joints are emptied after every step.
- Auto-tuning watches smoothed frame time and steps through four quality
  levels (contacts 8→4→2, sub-step cap 4→2). Escalation is prompt above
  1.2× the 55 fps target; recovery steps one level down after three
  consecutive evaluations at/under target, with a 1.0–1.2 dead band, so a
  60 Hz vsync run returns to level 0 instead of staying pinned (the old
  recover >~92 fps threshold was unreachable at vsync). Hitches over 100 ms
  are clamped out of the EMA. The threaded solver is deliberately not on the
  auto path — ODE's threading overhead outweighs its win below ~1k bodies —
  and `gX_PhysicsWorld_SetThreading` stays available explicitly.
- `GX_PHYSICS_TUNE=<0..3>` pins the level (captures/audits) and survives
  `gX_Physics_ResetFrameHistory`; heavy scene switches call the reset so one
  spike cannot lock the low tier.
- `gX_Physics_GetAlpha` exposes the sub-step fraction; the sandbox snapshots
  pre-step poses in its step hook and blends positions (`gX_F32v3_Lerp`) and
  orientations (`gX_F32Qua_Slerp`) at render time, so motion stays smooth
  when the display rate differs from 60 Hz.

## Wrapper contract

`gX_PhysicsWorld_Create` returns an independently-owned world (multiple
worlds coexist; ODE refcounting handles global init/teardown). Each world owns
its space, contact joint group, event pair tables and threaded solver state —
no process-global collision state. The API covers box/sphere/capsule/cylinder/
convex/tri-mesh bodies, static colliders, ground planes, heightfield terrain,
materials, collision
filters, sensors, user tags, forces/torques, raycasts, joints (ball/hinge/
slider/universal/fixed/hinge2/AMotor/piston), and ENTER/STAY/EXIT contact
events. The piston is a slide along its axis plus a free rotation about it
(the drill/screw DOF): the group-1 params drive the slide, the *2 variants
the rotation (ODE measures the rotation from an arbitrary reference — use
rates). The heightfield builder samples a caller grid row-major (x fastest),
copies the heights, and builds the field WRAPPED: ODE tiles it in local x/z
and this build's bounded mode drops bodies through (probed), so the caller
passes one tile centred on the position and keeps its borders at zero — the
repeated tiles are then flat. `gX_Mesh_GenHeightGrid` renders the same grid.
The AMotor is the angular tier: up to three axes with per-axis limits and
motors. `AMotorSetEuler` selects ODE's swing-twist chain (axis A fixed in
body A, axis B fixed in body B and the middle axis derived — the two
references must be perpendicular, ODE asserts otherwise) and is what ball
joints use for swing bands; `AMotorSetAxes` selects user mode (axes fixed in
body A) and is what axis motors use; the middle angle reads the swing, and
user-mode stops additionally need `AMotorSetAngle`. The
cylinder body runs along the body's Z (ODE's cylinder axis) — the mesh
wrapper offers `gX_Mesh_GenCylinderZ` so physics and pixels line up without
a render-side base rotation. Hinge2 is a hinge with a suspension: axis1
carries the suspension slide (a soft constraint, ERP/CFM via
`SetSuspension`; ODE 0.16 hinge2 has no travel stops) and the steering
rotation, axis2 is the spin axis; the group-1 params (SetLimits/SetMotor/
SetStops, GetPosition/GetPositionRate) drive the steering and the *2
variants (SetMotor2/SetLimits2, GetPosition2/GetPosition2Rate) the spin.
ODE needs two real bodies for hinge2, so a NULL partner binds to a hidden
per-world kinematic anchor (motor commands wake both joint bodies, so a
settled rig still responds). Bodies connected by a constraint joint never
collide: the near callback skips the pair before the narrow phase, so the
overlapping segments a joint is built with (ragdoll limbs, rig links) never
reach the solver while unconnected bodies still collide normally. Joint
feedback is available per joint:
`EnableFeedback` registers ODE's per-step constraint report and
`GetFeedback` returns the force/torque the solver applied to the joint's
first body (bodyA) in world coordinates — the storage is owned by the world
and pruned when a joint is destroyed, so callers can poll a joint's load
and implement breaking themselves. ODE quaternions
are converted at the boundary (`[w,x,y,z]` ↔ gX `[x,y,z,w]`). Contact
response combines a pair as min friction / max restitution, adds the
rolling/spin resistances, and turns summed tangential surface velocities
into a belt motion along the first friction direction (ODE's rolling and
motion contact modes); the ported soft-CFM stabilizer is tuned (1e-4) so
resting stacks stay stable while material restitution actually rebounds —
pinned by the wrapper suite. Bodies auto-disable after settling (world
defaults: on, 0.5 linear/angular thresholds, 10 steps, 1 s); the wrapper
wakes them on explicit position/rotation/velocity/force mutations and when
an enabled body touches them, so sleeping is a pure optimization —
`IsEnabled` / `SetAutoDisable` expose the state and the sandbox HUD counts
the sleepers. Mass properties are editable: `SetMass` rescales the total
mass (the inertia follows, the balance point stays) and
`SetMassCenter` / `GetMassCenter` move the center of mass in body-local
space (an offset from the shape center) with the inertia rebuilt by the
parallel axis theorem. ODE 0.16 keeps the center of mass at the body
origin, so a non-zero center offsets the shape the other way and moves the
body origin onto the balance point while the shape keeps its world pose;
renderers place the shape at `position - RotateByQuaternion(center,
rotation)` (the sandbox sync does this for every prop).

## Scene and rendering

`--physics` (or `--masterIndex` to `PHYSICS`) runs the props sandbox:
`GX_PHYSICS_PROPS=<n>` sets the prop count (default 16, cap 64) and
`GX_PHYSICS_RAGDOLLS=<n>` pre-spawns up to 8 humanoid ragdolls; the first
prop batch spawns immediately and the rest stagger across frames. The grid
mixes the body types — boxes, standing cylinders (drums) and spheres cycle
per spawn slot. The scene
owns the bodies; the GFX renderer draws them — each prop and ragdoll limb is
a generated mesh in the renderer's model heap, and the scene writes body
transforms into model params every frame. The GFX heap transition contract
treats the sandbox as a third content owner (demo / battle / physics), so
switching scenes clears and rebuilds content exactly like battle's; the
sandbox is also excluded from the GFX auto-seat pass (its ground slab is
authored with the top at y=0 and its objects are physics-driven). Leaving the
sandbox calls `gX_ScenePhysics_Park` (scene responder): props and ragdolls are
destroyed and the step hook plus collision callback are detached, so nothing
simulates in the shared world while another scene runs; the static ground
collider persists for the next entry. The title DEMOS hub routes here as
"PHYSICS SANDBOX".

The sandbox draws a HUD in the scene overlay (post-renderer): prop/ragdoll
counts, charge count, the sleeper count, step and tuning state, the hovered
prop's element,
temperature, phase, melt percent and nominal contact material (`mu`/`e`),
the hottest prop, the water state, and the key legend.

### Contact materials

Props carry the element table's nominal friction and restitution
(`gX_ScenePhysics_MaterialForElement`; the HUD shows the hovered prop's
`mu`/`e`). The wrapper combines a contact pair as min friction / max
restitution, so a prop keeps its own surface even against the grippy ground:
iron lands springy (`e 0.55`), lead is the dead soft outlier (`e 0.10`), and
gold's low friction slides farthest. Rig parts use a steel material
(0.6/0.35), ragdoll limbs a soft grippy tissue (0.9/0.05) and the ground a
rough dead surface (0.9/0.0). `GX_PHYSICS_MATERIALS=0` keeps the wrapper
defaults for A/B captures. Props also carry the element's rolling/spin
resistance (lead and graphite roll deader than polished steel/gold) and
override the world's ported per-step damping with a light air-drag
stand-in, so slides end at friction and rolls end at rolling resistance.
The sandbox's conveyor belt strip (near the pool) carries bodies along +X
at its surface velocity — flip the sign in the material to reverse; the
debug view draws the direction arrow and `GX_PHYSICS_BELT=0` skips the
strip. The ported contact softness (soft CFM 0.01)
absorbed most of a bounce; the wrapper now uses 1e-4 so per-element
restitution is meaningful while stacks still settle — the sandbox suite
pins the element→material mapping, a slippery/grippy slide, a bouncy/dead
drop, the rolling/spin decay and the belt carry.

### Impact FX (sparks)

The world's contact-event callback (`gX_PhysicsWorld_SetEventCallback`, a slot
independent of thermal's collision callback) feeds a per-scene impact queue:
ENTER transitions only (a bounce re-arms, a resting stack does not re-fire)
with a closing-speed floor. Each entry records the averaged contact point,
the reduced-mass energy, the hotter side's thermal state and the prop/bone
tint. The queue is a fixed 16-entry ring — purely visual, never fed back into
the step, so the simulation stays deterministic.

Per frame the scene drains the queue into four reserved GPU particle emitters
(`gX_ScenePhysics_SparksSetup`/`SparksRemove`; `GX_PHYSICS_SPARKS=0` disables
both the capture and the emitters). Bursts are round-robin and short-lived
(`GX_SCENE_PHYSICS_SPARK_LIFE`); energy maps to emit rate/speed/life, and the
color blends the tint (cold) toward the blackbody color (hot) with a
white-hot spike on heavy fresh hits. **Charged impacts read as saturated
cyan arcs** (any charged side flags the entry — a sum would cancel opposite
signs) with a tighter spread. The renderer's default emitter is silenced
while the sandbox owns the particle pass and restored at park.

The GPU particle path needs the opt-in compute build (`GX_COMPUTE=ON` → GL
4.3, `build/compute`); the default GL 3.3 build logs the unavailable path and
runs the physics without visual sparks.

### Scorch decals

Ground-facing impacts at/above `GX_SCENE_PHYSICS_DECAL_MIN_ENERGY` leave a
soft ash/scorch mark (`resources/texture/misc/scorch.png`, procedurally
generated): the scene sizes it with impact energy, tints it warm as the
prop heats and fades it via the texture's alpha (the decal shader
multiplies texture alpha into the blend opacity). Marks last
`GX_SCENE_PHYSICS_DECAL_LIFE` seconds and the sandbox saves/restores the
renderer's decal-pass flag at park; `GX_PHYSICS_DECALS=0` disables them.
The decal draw takes a per-decal albedo tint (`gX_RendererGFX_Decal.color`);
marks stay subtle by design on the sandbox's near-black floor.

### Debug view

`K` toggles a collider/contact overlay (`GX_PHYSICS_DEBUG=1` starts it on for
captures/audits): wireframe boxes/spheres on every prop (red while dragged),
wireframe boxes on ragdoll bones, cyan cubes and yellow normal lines for the
current frame's contact points, plus a 1-unit ground grid. Joint rig parts
draw as steel wireframes with cyan anchor cubes and yellow hinge/slider
axes. Contact points are recorded from the same contact-event stream as the
impact FX (frame-tagged, so stale entries are skipped). The overlay draws in
the scene layer after the renderer with its own 3D state block
(`gX_Drawing_Begin3D`), so no renderer coupling is needed.

| Input | Action |
|---|---|
| Left click on a prop | Grab it — spring-pulled along the ground plane; release throws with the last velocity |
| Left click on a rig part | Grab a chain link, the door, the windmill or the rail carriage — same spring drag |
| Left click on the ground | Spawn a box above the cursor |
| Right click on a prop | Delete it |
| Right click on a ragdoll limb | Delete that ragdoll |
| `T` | Drop a ragdoll at the cursor (fallback: a fixed row) |
| `M` | Toggle electromagnetism (charge-colored props + optional B field) |
| `H` | Heat the prop/ragdoll under the cursor (everything when hovering none) |
| `K` | Toggle the debug view (colliders, contacts, ground grid) |
| `P` | Fill/drain the pool (buoyancy/drag plus the visible water volume) |
| `N` | Toggle the magnetic dipole forces (the bar magnets react and align) |
| `B` | Balance test — re-pose the three mass-center posts at the same tilt and release |
| `R` | Reset all props, ragdolls and rig parts to their spawn poses |

The engine free camera drives the view (the scene sets a default framing on
first entry only).

### Joint rigs

<!-- //ai: constraint rigs exercise the wrapper joint family; no new assets. -->
The sandbox exercises the wrapper's joint family with four fixed rigs: a
ten-link hanging chain (ball joints under a gantry), a plank door on a
limited vertical hinge, a motorized windmill, and a slider rail carriage.
Rig parts share a link pool and render through the GFX heap with the same
sub-step interpolation as props; they are mouse-draggable (swing the door,
throw the rail carriage) and reset to their spawn poses with `R`.
`GX_PHYSICS_JOINTS=0` skips the rigs. Rig bodies use an exclusive collision
category (the ragdoll convention) so chain links never contact each other,
and joints are destroyed before their bodies on clear/park. Headless tests
pin the chain span, the driven hinge rate, each rig's constraint height and
the park sweep.

### Kinematic movers

<!-- //ai: driven platforms; no new assets. -->
A lift platform (vertical sine, 1.8 m amplitude over a 6 s period) and a
square turntable (0.9 rad/s about Y) are scene-driven kinematic bodies: the
step hook sets their velocities analytically, ODE integrates them while
ignoring forces and gravity, and dynamic bodies ride or get pushed by the
surfaces through contacts. A payload crate spawns resting on each mover top
at entry (payloads use prop slots, so drag/delete work and `R` places them
back on the movers when the platforms reset to their base poses). The
wrapper's per-body damping is zeroed on the movers so the drive integrates
exactly. `GX_PHYSICS_MOVERS=0` skips the pair. Headless tests pin the
kinematic type, the drive (lift rise, table spin), the payload spawn on
each top and a 20-second ride without a crate walking off the platform.

### Mass properties (balance posts)

<!-- //ai: mass-center content; no new assets. -->
Three identical planks (same shape, same total mass — normalized through
`gX_PhysicsBody_SetMass`) stand behind the magnets and differ only in where
their mass sits: `gX_PhysicsBody_SetMassCenter` puts the balance point
+0.6 (top-heavy, red), 0 (centered, pale) and -0.6 (bottom-heavy, blue)
above/below the shape center. `B` re-poses all three at the same tilted
release (0.2 rad with the pivot edge on the ground, at rest): the top-heavy
post tips over while the centered one settles back and the bottom-heavy one
rocks like a weeble — the same test the wrapper suite pins. The debug view
marks each balance point (yellow dot and the offset line from the shape
center). `GX_PHYSICS_COM=0` skips the posts. Payload/rig/mover bodies keep
centered mass; only the posts carry an offset, and the render sync undoes
it so the shapes draw exactly where they collide.

### Rover (hinge2 vehicle)

<!-- //ai: hinge2 cart; no new assets. -->
A four-wheel cart exercises the wrapper's hinge2 joints: a box chassis on
four Z-aligned cylinder wheels, each wheel held by a hinge2 whose axis1 is
the steering/suspension up and axis2 its own axle. The front wheels carry a
1.1 rad steering angle in their spawn pose (the group-1 motor locks it),
the drive motors spin the steered front wheels and the free rear wheels
roll — front-wheel drive keeps the tight lap stable (driving the rear
spins the cart out and it stops cornering). Each wheel carries a light
spoke bar sharing the wheel's transform (a second model in the GFX heap,
synced by the link pass), so the spin reads — a smooth cylinder is
rotationally symmetric and otherwise looks parked; the 1.1 rad steering is
what keeps the lap inside the free floor (smaller angles understeer into a
multi-metre circle). The soft suspension
(ERP/CFM) leans and bobs the chassis through the turns; the wheels and
chassis use near-zero damping (the rig default is a damper a vehicle
cannot reach its drive speed through) and auto-disable off. The spawn
places the rear axle the *measured* turn radius (not wheelbase/tan(steer)
— the tyres slip) from the drive-circle center, so the lap stays on the
open floor between the prop grid and the belt; `R` re-poses it with the
other rig parts and it is mouse-draggable. `GX_PHYSICS_ROVER=0` skips it.
Headless tests pin the build (five links, four hinge2s, box + cylinder
shapes), the drive (front-wheel spin at the motor rate, a steady yaw, a
closed lap inside the yard).

### Break target (joint feedback)

<!-- //ai: feedback-welded board; no new assets. -->
A plank hangs from a gantry post (static collider) welded to the frame with
a fixed joint whose feedback the scene polls every step: the reported
constraint force carries the board's weight (~1.3 N), and an impact above
the break force (10 N) destroys the joint — the weld snaps and the board
falls. Thrown props, the rover or a dragged link all register through the
same reading; the impact also fires the usual spark/decal impact FX. `R`
rewelds the board at its spawn pose (the weld is recreated after the pose
reset, so the fixed joint locks the right transform). `GX_PHYSICS_TARGET=0`
skips the rig. Headless tests pin the build, the static-load reading, the
no-break-under-own-weight case, the break under a heavy pull, the fall and
R's reweld.

### Piston pump and terrain mound

<!-- //ai: piston joint + heightfield content; no new assets. -->
A vertical plunger runs on a **piston joint** (a slide along Y plus a free
rotation about it — the screw/drill DOF) between two static guide plates:
the step hook square-wave drives the slide motor into the ±0.40 limits while
a small rotation motor keeps the plunger turning, so it pumps instead of
pinning at a stop (`GX_PHYSICS_PUMP=0` skips; `R` restarts the cycle). It
doubles as a trampoline for props dropped on it.

Just past the belt's far end a **heightfield mound** (the wrapper's static
terrain collider) catches what the belt carries and rolls it down the slope.
The collider's tile is 30 m so the wrapped field's repeated mounds land
outside the yard, and the mesh renders only the window around the mound
(`gX_Mesh_GenHeightGrid`, the same heights as the collider, lifted 2 cm off
the ground plane); `GX_PHYSICS_MOUND=0` skips it and it is released on
unload like the belt. Headless tests pin the pump's stroke/limits/spin and
the mound's downhill roll.

### Light-speed lane (relativity)

<!-- //ai: relativity slice content; no new assets. -->
A slick iron **slug** parks on the lane start at (-6, -2.4) and launches
along +x through the prop field into the pool the frame it spawns; `L`
re-fires it. Its speed is clamped below the tuned light speed and its mass
dilates by gamma as it runs (see **Relativity (light speed)** below for the
layer contract); headless tests pin the build/register, the launch velocity
(0.92c), the gamma growth over the run and the never-exceeds-c invariant.

## Electromagnetism

`M` toggles the charged-prop mode; `GX_PHYSICS_EM=1` starts with it on and
`GX_PHYSICS_B=x,y,z` sets a uniform magnetic field (default zero:
electrostatic only). The same applier serves the magnetic dipoles: the
module stays enabled while either mode is active, and `N` toggles the
dipole forces.

- **Laws**: pairwise Coulomb between charged bodies (props and, when the
  sandbox registers them, ragdoll bones) —
  `k·q₁q₂/(r²+ε²)`, softened by `settings.softening` and clamped at
  `settings.maxForce` per pair — plus the uniform-field Lorentz force
  `F = q(v×B)` from body velocities. Like charges repel, opposite charges
  attract, and a B field curves moving charges; the applier adds no torque
  (forces act along the separation line / on the body center).
- **Magnetic dipoles**: `gX_PhysicsEM_SetDipole` gives a body a magnetic
  moment in its local frame — the applier rotates it with the body each
  step, so a magnet's field and its response follow its pose. Moments feel
  the dipole pair force (the field-gradient law
  `F = 3μ₀/4πr⁴·[(m₁·r̂)m₂ + (m₂·r̂)m₁ + (m₁·m₂)r̂ − 5(m₁·r̂)(m₂·r̂)r̂]`),
  each other's torques `m × B`, and the uniform-field torque `m × B`; the
  same softening and force cap bound them. Coaxial moments attract when
  the moment product is positive and repel when it is negative; moments
  side by side repel.
- **Induced magnetism**: `gX_PhysicsEM_SetSusceptibility` registers a
  body's induced-moment polarizability `α` (`m_ind = α·B`); the applier
  folds the induced moment into the same pair laws, which is the classic
  iron-to-magnet pull — **ferrous props are attracted toward field
  sources** (both sides feel it) while non-magnetic ones stay put. The
  sandbox registers element props whose table χ is meaningful at sim scale
  (iron; `GX_SCENE_PHYSICS_CHI_MIN`), so Au/Cu/Pb/Ag props are inert and
  iron reacts.
- **Sim-scaled, not SI**: `coulombScale` (default 60) replaces `k_e` and
  `dipoleScale` (default 0.05; the sandbox uses 0.03) replaces `μ₀/4π`; real
  SI EM at human scale is violent. The gxmath formulas stay SI-exact and are
  oracle-checked (`math_pass` `em` and `dipole` lines) and mirrored over ℝ
  in `GxMath.Physics` (`make proofs-full`); the applier is the tunable
  layer.
- **Per fixed step**: the applier runs from `gX_Physics_SetStepHook`, once
  before every substep. A per-frame call would be consumed by the first
  substep only, and force magnitude would drift with the sub-step count.
- **Deterministic**: a fixed registry walked in registration order; two
  identical charged worlds step bit-identically (tested, 300 steps), and
  dipole runs are pinned the same way.
- **Coincident bodies**: separation direction is undefined, so the pair is
  skipped (no NaN, no explosion); keep `softening` above zero for
  contact-range interactions.
- **Sandbox magnets**: a row of six bar magnets (element NONE, density 4)
  lies near the -Z wall; each bar's local Z carries the moment
  (`GX_SCENE_PHYSICS_MAGNET_*`). `N` toggles the forces via the shared
  `dipoleScale` (the bars stay), `GX_PHYSICS_MAGNETS=0` skips the content
  and `=1` starts the forces on. Three loose iron test pieces sit in front
  of the row — with the forces on they are pulled onto the magnets
  (susceptibility from the element table; the other elements stay put).
  The debug view (`K`) draws each magnet's world moment axis (red = moment,
  blue = opposite), and the bars rotate on the ground as they align.

## Elements

`src/physics/elements.{h,c}` — a 12-entry X-macro table (H, He, C, Al, Ti,
Fe, Cu, Zn, Ag, Sn, Au, Pb) with symbol, name, atomic number, molar mass
(g/mol), density (kg/m³), specific heat (J/kg·K), melting point (K),
nominal emissivity, latent heat of fusion (J/kg), the nominal contact
friction/restitution the sandbox props carry, a polished-metal appearance
(albedo, metalness, roughness — graphite for carbon) and the magnetic
susceptibility (μ_r−1: iron carries the effective ferromagnetic value, the
rest are weak paramagnets/diamagnets). The sandbox's props
carry **specific gravity**
(density/1000, i.e. tonne units) so a unit cube spans 0.09 (hydrogen) to
19.3 (gold) mass units; gases stay in the table but are not prop material.
Density drives mass, so identical props differ in inertia and — because EM
acceleration is `qE/m` — in how hard the fields throw them. The appearance
fields drive the props' PBR look (polished gold/silver/copper, matte
graphite, dull lead) and the susceptibility field drives the induced
magnetism: props with a meaningful χ at sim scale register an
induced-moment polarizability, so **iron snaps toward the bar magnets**
while gold, copper and lead stay inert. `gX_Elements_*`
provides the lookup, count and `String()` for logs.

## Temperature (thermal)

Every prop carries a temperature; impacts heat it, radiation cools it, and
the renderer glows it. `H` heats the hovered prop (or all props), and
`GX_PHYSICS_TEMP=<K>` pre-heats for captures (`GX_PHYSICS_HEAT=<J>` sets the
H-key energy, `GX_PHYSICS_THERMAL=0` disables the model):

- **State** (`src/physics/thermal.{h,c}`): a fixed registry of
  `{body, enthalpy, mass, specificHeat, emissivity, meltingPoint,
  latentHeat}` updated once per fixed step from the same physics step hook
  as EM (thermal integrates over the step dt). Temperature is *derived* from
  enthalpy, which makes the melting plateau exact.
- **Impact heating**: the thermal collision callback (installed on the
  world while enabled) converts closing speed along the contact normal into
  `½μv²`, amplified by `heatingScale` and split by mass share; a
  `minImpactSpeed` floor keeps resting-contact jitter from heating anything.
- **Contact conduction**: every touching pair also exchanges heat down the
  gradient (`contactConductance·ΔT·dt`); a static contact (the ground) sinks
  toward ambient, so a heated prop cools faster while it rests on the floor.
- **Radiative cooling**: `dH/dt = −coolingScale·ε·(T⁴ − T_ambient⁴)/mass`,
  clamped to `[ambient, maxTemperature]` (the T⁴ stiff end is capped). The
  gxmath Stefan-Boltzmann/Wien functions stay SI-exact.
- **Melting**: when enthalpy reaches `c_p·meltingPoint` the body enters the
  latent plateau — temperature holds at the melting point while
  `latentHeat` is absorbed — then it is *molten* and heats past the point.
  Cooling runs the plateau in reverse (re-solidification). Molten props get
  a dark, low-roughness surface and a glow floor so molten metal reads even
  below the white-hot ramp.
- **Glow**: per frame the scene maps temperature through
  `gX_F32v3_Physics_Temp_BlackbodyRGB` (650/520/460 nm samples, normalized)
  and ramps `emissiveFactor` from the glow threshold to white-hot; bloom and
  the AgX tonemap shape the result. Cold props keep the base tint emissive.
- **Element spread**: specific heat, emissivity and latent heat from the
  element table make melting and cooling element-specific — lead and tin
  melt under a heat-gun press, iron and gold only glow; precious metals
  hold a glow far longer than carbon, which sheds it in seconds.

## Ragdolls

`src/physics/ragdoll.{h,c}` — an 11-bone humanoid (pelvis/torso/head, upper
and lower arms/legs) built from wrapper capsules and spheres, joined by ball
and hinge joints with hinge limits (gameX reference layout). Torso/head/hip/
shoulder ball joints also carry an **Euler AMotor swing band** (the middle
angle, centered on the rest reading; ±0.40 rad for the spine/hips, ±0.45 for
the neck/shoulders) — ODE's Euler AMotor needs perpendicular reference axes
and its outer axes must stay free (clamping the twist axes destabilises the
articulation), so the band is the whole limit. Bones collide with each other
(one shared category bit, no self-exclusion): jointed pairs are skipped by
the wrapper, the swing bands keep the limbs out of the extreme folds and the
legs are spaced wider (±0.20) so limb contacts stay shallow — together that
keeps ragdoll self-collision stable, so an arm no longer passes through the
torso. The API covers creation at a feet position,
destroy, T-pose reset, enable/disable, and bone/joint queries. Bone mass is
authored in kg but the sim runs in the sandbox's tonne convention
(nominal/1000), so a dummy is human-weight: props shove it, it does not
bulldoze them, and it floats in the pool. Each bone keeps its nominal kg
figure for the thermal heat-capacity unit and the impact-FX energy mass, so
heating and spark energies are unchanged by the rescale.

When EM and/or thermal are active, the sandbox registers **every bone** on
spawn (and rebuilds on the `M` toggle):

- **EM**: charge magnitude `GX_SCENE_PHYSICS_RAGDOLL_CHARGE` (0.022, the
  pre-rescale 0.7 scaled by `sqrt(1000)`) with one shared sign across
  dummies — chosen so the limb-splay acceleration matches the pre-rescale
  behavior. A charged dummy's limbs repel into a spread-eagle and charged
  dummies push each other apart; opposite signs would tangle instead. Since
  the dummies are light now, charged props move them more than they used to.
- **Thermal**: the bone's nominal mass and a generic tissue material
  (cp 3300 J/kgK, emissivity 0.95, no melting plateau), so `H` heats a
  hovered dummy (or all of them when hovering nothing), impact heating
  applies to ragdoll landings, and the limbs glow through the same
  blackbody ramp as props.

Both registries hold 256 bodies (64 props + 8 ragdolls x 11 bones = 152).
Destroy and scene park unregister the bones before the bodies go.

### Active body controller (Phase 1)

`gX_Ragdoll_Control` is an opt-in stabilizer layered on the passive ragdoll;
it does not change the passive scene path. `Init` records the creation pose,
mass and support height. `Step` runs before a fixed physics step and applies:

- bounded PD torque toward each bone's creation orientation;
- a bounded center-of-mass support force at the pelvis;
- `PASSIVE`, `ACTIVE`, `FALLING` and `GETTING_UP` modes; the last uses stronger
  torque/support and returns to active after an upright check.

The controller never teleports bodies, never runs from render dt, and
allocates nothing. The headless `gX_Tests_Physics_RagdollControl` covers
upright hold, a bounded impulse, fall/get-up, null initialization, and a
same-state repeat comparison. A dev-only visual lab and animation/ragdoll
handoff remain future slices; see
[`active_ragdoll_lab.md`](active_ragdoll_lab.md).

## Water (buoyancy)

`src/physics/fluid.{h,c}` — a still-water region in the shared world. Bodies
registered with the applier get Archimedes buoyancy (`density · g · V_sub`)
applied at the centre of buoyancy, plus linear/quadratic drag and angular
drag while submerged, once per fixed step from the same step hook as EM and
thermal (a per-frame call would be consumed by the first sub-step only and
would scale drag with the sub-step count). The water is a static
axis-aligned region: a flat surface at `surfaceY`, an XZ extent and a
density in the sandbox's tonne convention — 1.0 is water, so element props
use their specific gravity directly. Bodies are modelled as
volume-equivalent spheres: the submerged fraction uses the exact
spherical-cap volume, so a body floats with the displaced volume matching
its density ratio and sinks past it; boxes register the radius of their
displaced volume (a small shape error, no orientation dependence). The
registry is a fixed array walked in registration order, so two identical
fluid worlds step bit-identically (pinned like EM/thermal).

The sandbox hosts a walled pool (`P` toggles it; `GX_PHYSICS_WATER=0`
skips the water while keeping the empty basin, and the basin statics persist
across entries like the ground): a translucent blended water volume over a
teal basin floor, three light buoys (density 0.35) bobbing at the
waterline, and every prop registered at spawn — element props sink at
density-ordered rates (lead plunges, carbon drifts) and drag slows the wet
fall versus a dry drop. Surface entries queue splash events the scene
drains into white-blue emitter bursts; underwater contacts skip the
spark/scorch path so the basin stays clean. Ragdoll bones register with the
applier too — human-density after the mass rescale — so a dummy dropped in
the pool floats, bobs and takes water drag. The default camera framing
includes the pool on first entry only.

## Tuning (physics editor)

`gX_ScenePhysics_Tuning` (`src/scene/physics.{h,c}`) is the sandbox's engine
parameter surface — the runtime knobs the scene kept behind env hooks,
promoted to a struct the editor edits and presets round-trip:

- **world**: gravity and the quality-level pin (adaptive or 0..3 via
  `gX_Physics_ApplyTuning`);
- **content**: prop/ragdoll targets (fill up; nothing despawns) and the
  materials/joints/water/magnets/sparks/decals toggles — the content
  toggles rebuild their content live (rigs re-build through the joint
  builder, the pool drains/fills, the magnet gate re-runs), and the impact
  capture re-installs only on transitions;
- **appliers**: the EM settings (Coulomb scale/softening/cap, uniform B,
  dipole + susceptibility scales), thermal settings (ambient, heating,
  cooling, conduction, cap, impact floor, enabled) and the fluid
  density/drag scalars (pool geometry stays scene content);
- **magnets**: `em.dipoleScale` is the magnet strength; `magnetsOn` gates
  it, and magnets-off zeroes the scale even while charges are on.

`gX_ScenePhysics_Tuning_Apply` pushes a tuning into the live scene;
`_Save`/`_Load` are JSON presets under `resources/tools/physics/` (json-c
conventions; `GX_NO_SETTINGS_SAVE` refuses writes into shipped paths).
`GX_PHYSICS_PRESET=<json>` applies a preset at the end of the entry plan for
captures/audits. The editor panel
(`src/tools/physics/physics_editor.cpp`) edits a pulled copy — "Pull live"
re-reads the scene/appliers, and a scene rebuild re-pulls automatically
(`contentEpoch`) — and is opened by `--physics-editor`,
`GX_TOOLS_OPEN=physics` or the title EXTRAS card. The panel carries a
**Free camera** button: the engine freezes the scene camera binds while a
tool is open, so the button hands the keyboard and cursor to the sandbox's
free camera (WASD move, E/Q rise and fall, mouse to look) and Esc returns
to the panel — the same modal pattern as the GFX editor. `R` still resets
the scene from the free camera, and clicks drag props at the crosshair.
`GX_PHYSICS_FREECAM=1` opens straight into the mode (capture hook).

The panel's **Toolgun** button (or `GX_TOOLS_TOOLGUN=1`) enters the shared
toolgun mode over the same free camera: a crosshair at the screen centre, a
blockout viewmodel drawn from the camera (no assets; bob/sway/recoil and a
muzzle flash), primary/secondary fire on the crosshair ray and the wheel
cycling tools. The sandbox's tools are grab (spring-pull the aimed prop or
rig part), freeze (dynamic/kinematic), heat, impulse, pin (a ball joint to
the static frame; RMB releases the aimed part's pins, `R` releases all, `K`
draws them) and remove. While the mode owns the buttons the sandbox's own
mouse interaction stands down (its keys keep working) via
`gX_ScenePhysics_SetToolMode`, and Esc leaves the toolgun and the camera
together. Aims resolve through `gX_ScenePhysics_AimAtModel`, so the HUD's
hover lines follow the crosshair; the tool effects are the
`gX_ScenePhysics_Tool*` surface plus the pin pool (`PinCount`), all pinned
by the headless scene tests.

## Relativity (light speed)

The sandbox runs a **tunable light speed**: `gX_PhysicsRelativity_*`
(`src/physics/relativity.{h,c}`) applies two layer effects on top of ODE's
Newtonian solver, per fixed step (the scene's step hook, after EM/thermal/
fluid):

- a **speed clamp** — no registered body's speed reaches `lightSpeed` (the
  cap bites at `c*(1 - clampMargin)`, default 1e-3);
- the **mass-dilation trick** — a body's mass re-scales to `gamma*restMass`
  as it speeds up (`SetMass`, throttled to a 1% rest-mass delta so the
  solver's mass rebuild stays cheap), so a constant force accelerates it
  less and less as v → c. Disabling the layer (or removing a body, or the R
  reset's content rebuild) restores the rest mass.

The gxmath laws behind it are the textbook ones with the light speed passed
in (`gX_F32_Physics_RelativityBeta/Gamma/ContractedLength/DilatedTime/
DopplerFactor/VelocityAdd/Rapidity`), pinned by the math-pass `relativity`
corpus case against the SymPy oracle; `|beta|` is clamped to 0.9999
(`gxmath/config.h`) so gamma stays finite. The SI-exact
`gX_F32_Physics_LorentzLength` keeps the real constant and is untouched.

The demo is the **light-speed lane**: a slick iron slug (radius 0.16 m,
pale tinted) parks on the lane start at (-6, -2.4) and launches along +x
through the prop field into the pool the moment it spawns (frame one of the
tail plan); `L` re-fires it. The HUD grows a fifth line — `LIGHT c ... //
SLUG <beta> C GAMMA <gamma> M <mass> T // [L] LAUNCH` — straight from
`gX_PhysicsRelativity_GetBodyState`. The editor panel toggles the layer and
slides `c` (2..60 m/s) live, the tuning round-trips `relativity`/`lightSpeed`
in presets, and the env hooks are `GX_PHYSICS_RELATIVITY=0` (skip the lane +
disable the applier) and `GX_PHYSICS_LIGHTSPEED=<m/s>`.

Deliberately out of this slice (documented follow-ups): the render flavours
(length contraction along the velocity, time-dilated clocks, Doppler
colouring for fast movers) and the Mathlib-tier Lean twins of the gxmath
laws (the identities `gamma^2 (1 - beta^2) = 1`, `contraction . gamma =
length`, `dilute = gamma . t`, `doppler^2 = (1+b)/(1-b)`, and the velocity
addition as `tanh (atanh u + atanh v)` — in
[`GxMath/Physics.lean`](https://github.com/lanttusoft/gx-proofs/blob/main/GxMath/Physics.lean)
(Mathlib gate, where the cache lives)).

## Tests and gates

- `./build/gX_build --test-only` — wrapper suites: world isolation/settling,
  joints (anchor/axis round-trips, limits), forces, raycast, sensors, contact
  events (ENTER/STAY/EXIT, sensor pairs, destroy-mid-contact), the ragdoll
  articulation (T-pose layout, settle bounds, disable/destroy, the rescaled
  tonne-convention bone masses), the element
  table invariants (density/molar mass plus thermal, contact, look and
  susceptibility data), electromagnetism
  (repel/attract signs, Lorentz direction, coincident-charge clamp, EM
  determinism), thermal behavior (heat capacity, monotonic cooling toward
  ambient, emissivity spread, impact heating, contact conduction, the
  melting plateau plus re-solidification, thermal determinism), and
  single-threaded step determinism (two identical worlds must step
  bit-identically for 600 steps, with a stability bound). Suite entry:
  `gX_Tests_Physics` (`src/physics/physics_tests.c`) — the relativity suite
  pins the light-speed layer: the clamp's cap/direction, the gamma mass
  growth at 0.92c/half-c, the rest-mass restore at rest, a driven body
  saturating below c, and the registry/disable paths.
- Sandbox lifecycle (`gX_Tests_ScenePhysics`, `src/scene/physics_tests.c`):
  parking the scene destroys its bodies, clears the EM/thermal registries and
  leaves the shared world steppable; the same suite drops a prop and requires
  the impact queue plus the debug contact capture to record the landing, and
  registers a ragdoll to verify every bone joins both appliers and heat lands
  on a bone. It also pins the contact materials (the element→material
  mapping plus the wrapper default fallback, a slippery-vs-grippy box slide
  and a bouncy-vs-dead sphere drop) and the pool build: the basin statics,
  the fluid settings matching the pool bounds, a dummy floated in the filled
  pool (bones join the fluid applier, unregister clears them), the
  park/unload reset and the drained-basin path.
- Fluid behavior (`gX_Tests_Physics_Fluid`, `src/physics/physics_tests.c`):
  the float waterline at density ratios 0.5/0.25 (Archimedes), a dense
  sphere sinking to the floor, a wet-vs-dry drop (drag), the surface-entry
  event, registry removal/reset state and two-world fluid determinism.
- Dipole behavior (`gX_Tests_Physics_Dipoles`, `src/physics/physics_tests.c`):
  the world moment follows the body rotation; coaxial aligned moments
  attract and anti-aligned repel; side-by-side parallel moments repel; a
  perpendicular moment in a uniform field gains the aligning torque and
  rotates onto the field; a magnetizable body is pulled toward a dipole
  (induced magnetism) while a control stays put; registry removal/reset and
  serial-run determinism.
  The sandbox suite pins the bar-magnet and iron-piece build (moments,
  susceptibilities, registry) and the `N` toggle scale/enabled
  transitions.
- Tuning surface (`gX_Tests_ScenePhysics`): the defaults mirror the
  appliers, a preset round-trips every field, the shipped-path guard
  refuses under `GX_NO_SETTINGS_SAVE`, and `gX_ScenePhysics_Tuning_Apply`
  drives the live toggles/settings headlessly (rigs rebuild, the pool
  drains/fills, EM/thermal/fluid settings replace).
- Contact surfaces (`gX_Tests_Physics_Surfaces`, `src/physics/physics_tests.c`):
  a rolling sphere with rolling resistance slows while a slick one keeps
  rolling; spin about the contact normal winds down with rolling resistance
  and persists without it; a belt surface carries a resting box at the
  surface speed and the sign reverses it. The sandbox suite pins the belt
  build (statics + unload) and a box drifting along +X.
- Sleeping (`gX_Tests_Physics_Sleeping`, `src/physics/physics_tests.c`):
  a settled box auto-disables; velocity/teleport/force mutations wake it;
  `SetAutoDisable(0)` keeps it simulating; explicit disable/enable round
  trips; and a side impact wakes a sleeper and shoves it (contacts wake).
- Wrapper destroy cleanup (`gX_Tests_Physics_Destroy`): a destroyed body must
  not leave a ghost geom holding up later bodies (ODE's `dBodyDestroy` only
  detaches attached geoms — the wrapper destroys them first).
- `make math-check` — the gxmath corpus includes the `em` line (Coulomb,
  field, Lorentz, cyclotron, photon energy), the `dipole` line (dipole
  field, torque, pair force, on-axis/equatorial/coaxial scalars) and the
  `thermal` line (blackbody RGB, Stefan-Boltzmann, Wien peak)
  oracle-checked against SymPy-SI constants.
- `make proofs-full` (Mathlib tier) builds `GxMath.Physics`, which mirrors
  the dipole laws over ℝ — the axial/equatorial ratio, fall-off and sign
  contracts, the torque algebra and the textbook magnitude bounds.
- `make gfx-scene-smoke` — boots `--physics` and checks a non-empty frame.
- `make physics-editor-check` — both editor open paths
  (`GX_TOOLS_OPEN=physics`, `--physics-editor`) route to the sandbox, render
  and exit clean; the tuning data path is pinned in `--test-only`.
- Renderer scene-transition unit tests cover the physics content-owner matrix.

## Deferred (gameX side, not ported yet)

- soft-body / deformable props;
- gameplay hooks on top of the sandbox (character controller, interactions).

### Queued ideas (author-proposed, unscheduled)

<!-- //ai: relativity queue note; no implementation yet. -->
- **Relativistic kinematics** ("set the speed of light to some value and
  relativistic effects follow", aimed at battle scenes). ODE's solver is
  Newtonian and fixed-step, so true SR dynamics are out; the tractable
  shape is a `c`-driven layer:
  - gxmath laws: β = v/c, γ = 1/sqrt(1-β²), length contraction / time
    dilation factors — exactly the math-pass corpus + Lean contract style
    (the identities γ²(1-β²) = 1, contraction(dilation(v)) round trips);
  - a per-step velocity clamp at c (the wrapper already exposes velocities
    and `SetMass`: the per-step mass-dilation trick, `SetMass(γ·m)` on
    chosen bodies, makes acceleration diverge as v → c without touching
    the solver);
  - render-side flavour for fast movers: length contraction along the
    velocity axis (model scale in the motion-oriented frame), time
    dilation on their spinning/animated parts (a fast spinner visibly
    slows), and a Doppler emissive tint;
  - `c` is a scene tuning value (env hook + physics editor knob) set low
    enough to read on screen; "light speed" is then a real in-scene
    speed limit for projectiles and movers.
