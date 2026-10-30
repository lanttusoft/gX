/**********************************************************************************************
*
*   [gX: src/scene/physics.h]
*   Physics sandbox scene — rigid-body props in the shared simulation world
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


#ifndef GX_SCENE_PHYSICS_H
#define GX_SCENE_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../wrappers/physics.h"
#include "../audio/sfx.h"
#include "../physics/em.h"
#include "../physics/relativity.h"
#include "../physics/thermal.h"
#include "../physics/ragdoll.h"

//ai: props demo scope — a deterministic grid of boxes/spheres falls onto a
//   ground plane. Bodies live in the shared physics world; the props are
//   drawn by the GFX renderer (generated meshes pushed into its model heap)
//   and this scene only spawns bodies and syncs transforms each frame.

#define GX_SCENE_PHYSICS_MAX_PROPS 64
#define GX_SCENE_PHYSICS_DEFAULT_PROPS 16
#define GX_SCENE_PHYSICS_SPAWN_PER_FRAME 4
#define GX_SCENE_PHYSICS_GROUND_SIZE 24.0f
#define GX_SCENE_PHYSICS_MAX_RAGDOLLS 8

//ai: element densities are kg/m3; the sandbox's mass unit is the tonne (a
//   unit cube of water = 1), so props carry specific gravity (0.09..19.3)
//   instead of the raw kg figure — the relative differences are the point.
#define GX_SCENE_PHYSICS_DENSITY_SCALE (1.0f / 1000.0f)

//ai: thermal glow ramp (K) and the H-key heat energy (sim joules).
#define GX_SCENE_PHYSICS_GLOW_START   800.0f
#define GX_SCENE_PHYSICS_GLOW_FULL    2000.0f
#define GX_SCENE_PHYSICS_HEAT_DEFAULT 2.5e6f

//ai: drag spring — velocity toward the mouse target per second and its
//   clamp; releasing keeps the velocity (drag-and-throw).
#define GX_SCENE_PHYSICS_DRAG_GAIN       6.0f
#define GX_SCENE_PHYSICS_DRAG_MAX_SPEED 12.0f
//ai: per-step angular damping while dragged (stops windmilling).
#define GX_SCENE_PHYSICS_DRAG_SPIN_DAMP  0.85f

//ai: ragdoll magic — bones register with the EM/thermal appliers while
//   those are active. All dummies share one charge sign (set at spawn) so a
//   dummy's own limbs repel outward and dummies push each other apart;
//   magnitude is scaled for the rescaled bone masses (nominal/1000) so the
//   limb-splay acceleration matches the pre-rescale behavior
//   (~0.7/sqrt(1000)); the per-pair force cap is far away at this scale, so
//   charged props do move light dummies more than they used to. The thermal
//   material is a generic tissue constant (cp J/kgK, emissivity).
#define GX_SCENE_PHYSICS_RAGDOLL_CHARGE 0.022f
#define GX_SCENE_PHYSICS_RAGDOLL_CP     3300.0f
#define GX_SCENE_PHYSICS_RAGDOLL_EMISSIVITY 0.95f

//ai: debug view — K toggles wireframe colliders, the current frame's
//   contact points/normals and a ground grid (scene overlay, no renderer
//   coupling). GX_PHYSICS_DEBUG=1 starts it on for captures/audits.
#define GX_SCENE_PHYSICS_DEBUG_CONTACTS 32
//ai: the particle heap caps at 16 emitters (gfx/particle.h); the sandbox
//   needs at most that many pre-existing emitters silenced while it owns
//   the pass.
#define GX_SCENE_PHYSICS_SILENCED_MAX 16

typedef struct gX_ScenePhysics_Contact {
    gX_F32v3 pos;
    gX_F32v3 normal;
    gX_U32 frame;    //ai: physics.frameCount tag; stale entries are skipped
} gX_ScenePhysics_Contact;

//ai: impact FX — the contact-event callback queues impacts above a closing
//   speed floor; the scene drains the queue per frame into reserved GFX
//   particle emitters (spark bursts scaled by energy; colored by thermal
//   state, element tint and charge — charged impacts read as cyan arcs, and
//   heavy fresh hits spike white). Purely visual: the queue never feeds back
//   into the simulation.
#define GX_SCENE_PHYSICS_MAX_IMPACTS     16
#define GX_SCENE_PHYSICS_SPARK_EMITTERS   4
#define GX_SCENE_PHYSICS_SPARK_MIN_SPEED  2.0f
#define GX_SCENE_PHYSICS_SPARK_LIFE       0.35f

//ai: scorch decals — heavy ground impacts (energy >= floor) leave a soft
//   char mark from the procedurally generated texture
//   (resources/texture/misc/scorch.png). The decal albedo tint and alpha
//   are per-decal; texture alpha multiplies into the blend opacity.
#define GX_SCENE_PHYSICS_DECAL_MIN_ENERGY 45.0f
#define GX_SCENE_PHYSICS_DECAL_LIFE       30.0

typedef struct gX_ScenePhysics_Impact {
    gX_F32v3 position;     //ai: averaged contact point
    gX_F32v3 normal;       //ai: first contact normal
    gX_F32   energy;       //ai: 1/2 * reduced mass * closing speed^2 (sim J)
    gX_F32   temperature;  //ai: hotter side's thermal state (K)
    gX_F32   charge;       //ai: charged-side sum (0 = uncharged impact)
    gX_Color tint;         //ai: prop/bone tint (element hint)
} gX_ScenePhysics_Impact;

//ai: impact audio — the drain maps each queued impact onto the engine's
//   procedural SFX palette (audio/sfx.h) and plays it at the contact point.
//   The map is pure (no audio device), so tests pin the palette/level
//   decisions; GX_PHYSICS_SFX=0 or the tuning's sfx toggle mutes the layer.
#define GX_SCENE_PHYSICS_SFX_MAX_DIST 14.0f

typedef struct gX_ScenePhysics_ImpactSound {
    gX_I32 variant;   //ai: GX_AUDIO_SFX_*
    gX_F32 volume;    //ai: 0..1 pre-bus level
    gX_F32 pitch;     //ai: 1.0 = natural
} gX_ScenePhysics_ImpactSound;

//ai: pure mapping — energy drives level/pitch (heavy hits read low and
//   loud), charge selects the arc palette, water selects the dull thud,
//   and a desaturated bright tint (metal-ish albedo) or a hot body selects
//   the metal clang.
gX_I0 gX_ScenePhysics_ImpactSoundMap(const gX_ScenePhysics_Impact *impact,
    gX_I1 underwater, gX_ScenePhysics_ImpactSound *out);

typedef enum gX_ScenePhysics_PropKind {
    gX_SCENE_PHYSICS_PROP_BOX = 0,
    gX_SCENE_PHYSICS_PROP_SPHERE = 1,
    //ai: cylinder — size = (radius, radius, length); the length runs along
    //   the body's Z (ODE's cylinder axis, matching gX_Mesh_GenCylinderZ).
    gX_SCENE_PHYSICS_PROP_CYLINDER = 2
} gX_ScenePhysics_PropKind;

typedef struct gX_ScenePhysics_Prop {
    gX_PhysicsBody body;
    //ai: index into the GFX model heap; -1 for a destroyed prop.
    gX_I32 modelIndex;
    gX_I32 element;          //ai: gX_Elements id — density (mass) source
    gX_F32v3 size;           //ai: box extents / sphere radius in x
    gX_F32v3 dipole;         //ai: local magnetic moment (zero = not a magnet)
    gX_Color tint;           //ai: base look tint (element albedo / explicit)
    gX_F32v3 spawnPos;       // deterministic spawn point (reset key)
    //ai: shape kind — spheres/cylinders use different base material values
    //   and meshes, and the molten state must restore the solid look.
    gX_ScenePhysics_PropKind kind;
    gX_I1 molten;
    //ai: pre-step pose for render interpolation (alpha from the module).
    gX_F32v3 prevPos;
    gX_F32Qua prevQuat;
} gX_ScenePhysics_Prop;

//ai: one humanoid ragdoll plus the GFX model indices of its limb meshes.
typedef struct gX_ScenePhysics_RagdollSlot {
    gX_Ragdoll ragdoll;
    gX_I32 boneModel[GX_RAGDOLL_MAX_BONES];  // GFX model heap indices, -1 = none
    //ai: charge sign (+/-1) — set at spawn, stable across registry rebuilds.
    gX_F32 charge;
    //ai: pre-step bone poses for render interpolation.
    gX_F32v3 bonePrevPos[GX_RAGDOLL_MAX_BONES];
    gX_F32Qua bonePrevQuat[GX_RAGDOLL_MAX_BONES];
} gX_ScenePhysics_RagdollSlot;

//ai: joints — a small fixed set of constraint rigs (hanging chain, hinged
//   door, motorized windmill, slider rail) gives the wrapper's joint family a
//   sandbox home; GX_PHYSICS_JOINTS=0 skips them. Rig bodies share the
//   link/interpolation pool below; joints are tracked for the debug view.
//   The pools hold the 13 rig parts with generous headroom.
#define GX_SCENE_PHYSICS_MAX_LINKS  24
#define GX_SCENE_PHYSICS_MAX_JOINTS 24
#define GX_SCENE_PHYSICS_ROPE_LINKS 10
#define GX_SCENE_PHYSICS_LINK_SIZE  0.34f
#define GX_SCENE_PHYSICS_LINK_SPACING 0.56f

//ai: water — a walled pool over the ground slab, fed by the fluid applier
//   (buoyancy + drag). The basin is a persistent fixture; only the water
//   volume, the buoy props and the fluid registry toggle with P
//   (GX_PHYSICS_WATER=0 skips the water but keeps the empty basin).
#define GX_SCENE_PHYSICS_POOL_CENTER_X 6.4f
#define GX_SCENE_PHYSICS_POOL_CENTER_Z -2.4f
#define GX_SCENE_PHYSICS_POOL_SIZE     6.0f
#define GX_SCENE_PHYSICS_POOL_SURFACE  2.4f
#define GX_SCENE_PHYSICS_POOL_WALL     0.2f
#define GX_SCENE_PHYSICS_BUOYS         3
#define GX_SCENE_PHYSICS_BUOY_DENSITY  0.35f
#define GX_SCENE_PHYSICS_BUOY_RADIUS   0.45f

//ai: magnets — bar magnets whose local Z carries the moment. The content
//   builds with the scene (GX_PHYSICS_MAGNETS=0 skips it) and the N key
//   toggles the magnetic forces (GX_PHYSICS_MAGNETS=1 starts them on).
#define GX_SCENE_PHYSICS_MAGNET_COUNT   6
#define GX_SCENE_PHYSICS_MAGNET_SIZE    0.22f
#define GX_SCENE_PHYSICS_MAGNET_LENGTH  0.8f
#define GX_SCENE_PHYSICS_MAGNET_DENSITY 4.0f
#define GX_SCENE_PHYSICS_MAGNET_MOMENT  1.2f
//ai: sim-scaled mu0/4pi for the applier (SI is 1e-7; sandbox moments are
//   order-one so the field/force/torque magnitudes land in playable ranges).
#define GX_SCENE_PHYSICS_MAGNET_SCALE   0.03f
//ai: loose iron test pieces near the magnet row — N visibly pulls them in.
#define GX_SCENE_PHYSICS_IRON_COUNT     3
#define GX_SCENE_PHYSICS_IRON_RADIUS    0.32f
//ai: the element table's susceptibility is used as-is at sim scale (SI iron
//   is ~1e3, weak paramagnets/diamagnets ~1e-5); only values above this
//   floor register, so the sandbox shows iron reacting and the rest inert.
#define GX_SCENE_PHYSICS_CHI_MIN        0.01f
#define GX_SCENE_PHYSICS_CHI_SCALE      1.0f

//ai: props opt out of the world's ported per-step damping (0.01 linear /
//   0.05 angular — ~45%/s and ~95%/s at 60 Hz) with a light air-drag
//   stand-in, so slides are stopped by friction and rolls by the element
//   rolling resistance instead of global damping.
#define GX_SCENE_PHYSICS_PROP_DAMPING_LINEAR  0.002f
#define GX_SCENE_PHYSICS_PROP_DAMPING_ANGULAR 0.002f

//ai: conveyor belt — a static strip whose top surface carries bodies along
//   +X at the material surface velocity; GX_PHYSICS_BELT=0 skips it. The
//   statics persist like the ground; the visual rebuilds per entry.
#define GX_SCENE_PHYSICS_BELT_CENTER_X 5.0f
#define GX_SCENE_PHYSICS_BELT_CENTER_Z 3.6f
#define GX_SCENE_PHYSICS_BELT_LENGTH   5.2f
#define GX_SCENE_PHYSICS_BELT_WIDTH    1.2f
#define GX_SCENE_PHYSICS_BELT_HEIGHT   0.25f
#define GX_SCENE_PHYSICS_BELT_SPEED    1.5f

//ai: kinematic movers — a driven lift platform (vertical sine) and a spinning
//   turntable; GX_PHYSICS_MOVERS=0 skips them. The scene steers their
//   velocities from the step hook: ODE integrates kinematic bodies (forces and
//   gravity ignored) and they carry/push dynamic ones by contact.
#define GX_SCENE_PHYSICS_MOVERS        2
#define GX_SCENE_PHYSICS_LIFT_SIZE     2.6f
#define GX_SCENE_PHYSICS_LIFT_THICK    0.3f
#define GX_SCENE_PHYSICS_LIFT_AMP      1.8f
#define GX_SCENE_PHYSICS_LIFT_PERIOD   6.0f
#define GX_SCENE_PHYSICS_LIFT_X       -5.0f
#define GX_SCENE_PHYSICS_LIFT_Z        1.8f
#define GX_SCENE_PHYSICS_TABLE_SIZE    2.6f
#define GX_SCENE_PHYSICS_TABLE_THICK   0.25f
#define GX_SCENE_PHYSICS_TABLE_RATE    0.9f
#define GX_SCENE_PHYSICS_TABLE_X      -5.0f
#define GX_SCENE_PHYSICS_TABLE_Z      -2.0f

//ai: mover payloads — one crate spawns resting on each mover top (lift,
//   turntable) after the grid plan so the ride reads on entry; they use prop
//   slots and R resets them back onto the movers.
#define GX_SCENE_PHYSICS_MOVER_PAYLOADS 2
#define GX_SCENE_PHYSICS_PAYLOAD_SIZE   0.6f

//ai: balance posts — three identical planks (same shape, same total mass)
//   whose only difference is the mass center: top-heavy, centered,
//   bottom-heavy. They spawn standing; B re-poses all three at the same
//   tilted release (the pivot edge on the ground, at rest) and the mass
//   center decides who falls, who stands and who rocks back like a weeble.
//   GX_PHYSICS_COM=0 skips them.
#define GX_SCENE_PHYSICS_BALANCE_COUNT    3
#define GX_SCENE_PHYSICS_BALANCE_WIDTH    0.5f
#define GX_SCENE_PHYSICS_BALANCE_HEIGHT   2.0f
#define GX_SCENE_PHYSICS_BALANCE_MASS     0.5f
#define GX_SCENE_PHYSICS_BALANCE_X       -1.6f
#define GX_SCENE_PHYSICS_BALANCE_Z       -7.4f
#define GX_SCENE_PHYSICS_BALANCE_GAP      1.6f
#define GX_SCENE_PHYSICS_BALANCE_OFFSET   0.6f
#define GX_SCENE_PHYSICS_BALANCE_TILT     0.20f

//ai: rover — a four-wheel cart on hinge2 joints (suspension + steering +
//   spin): cylinder wheels, the front wheels locked at a fixed steering
//   angle so the drive motors trace a circle, and a soft suspension that
//   leans the chassis through the turns. The wheels sit on the ground at
//   spawn; GX_PHYSICS_ROVER=0 skips it.
#define GX_SCENE_PHYSICS_ROVER_WHEEL_R      0.30f
#define GX_SCENE_PHYSICS_ROVER_WHEEL_W      0.24f
#define GX_SCENE_PHYSICS_ROVER_WHEELBASE    1.00f
#define GX_SCENE_PHYSICS_ROVER_TRACK        1.00f
#define GX_SCENE_PHYSICS_ROVER_CHASSIS_X    1.15f
#define GX_SCENE_PHYSICS_ROVER_CHASSIS_Y    0.30f
#define GX_SCENE_PHYSICS_ROVER_CHASSIS_Z    0.60f
#define GX_SCENE_PHYSICS_ROVER_CHASSIS_LIFT 0.45f
#define GX_SCENE_PHYSICS_ROVER_STEER        1.10f
#define GX_SCENE_PHYSICS_ROVER_SPIN        -2.50f
//ai: the spawn places the rear axle this far from the drive-circle center —
//   the tyres slip in the tight turn, so the real radius runs larger than
//   wheelbase/tan(steer); the spawn uses the measured radius so the lap
//   stays in the open floor.
#define GX_SCENE_PHYSICS_ROVER_SPAWN_R      0.85f
#define GX_SCENE_PHYSICS_ROVER_TORQUE       2.00f
#define GX_SCENE_PHYSICS_ROVER_STEER_HOLD   12.0f
#define GX_SCENE_PHYSICS_ROVER_SUSP_ERP     0.50f
#define GX_SCENE_PHYSICS_ROVER_SUSP_CFM     0.01f
#define GX_SCENE_PHYSICS_ROVER_CENTER_X     0.0f
#define GX_SCENE_PHYSICS_ROVER_CENTER_Z     5.35f

//ai: break target — a board welded to a gantry post with a fixed joint whose
//   feedback the scene polls; an impact above the break force snaps the weld
//   (the joint is destroyed and the board falls) and R rewelds it.
//   GX_PHYSICS_TARGET=0 skips the rig.
#define GX_SCENE_PHYSICS_TARGET_POST_X     -6.60f
#define GX_SCENE_PHYSICS_TARGET_Z          -3.60f
#define GX_SCENE_PHYSICS_TARGET_ARM_Y       2.50f
#define GX_SCENE_PHYSICS_TARGET_BOARD_SX    0.12f
#define GX_SCENE_PHYSICS_TARGET_BOARD_SY    1.00f
#define GX_SCENE_PHYSICS_TARGET_BOARD_SZ    0.90f
#define GX_SCENE_PHYSICS_TARGET_BOARD_CY    1.85f
#define GX_SCENE_PHYSICS_TARGET_YAW         0.785f
#define GX_SCENE_PHYSICS_TARGET_DENSITY     1.2f
#define GX_SCENE_PHYSICS_TARGET_BREAK_N     10.0f

//ai: piston pump — a vertical plunger on a piston joint (slide + spin): the
//   step hook drives a square-wave slide motor into its limits while a small
//   rotation motor keeps it turning. GX_PHYSICS_PUMP=0 skips it; R resets
//   the plunger pose and the drive phase.
#define GX_SCENE_PHYSICS_PUMP_X        -6.60f
#define GX_SCENE_PHYSICS_PUMP_Z        -2.20f
#define GX_SCENE_PHYSICS_PUMP_STROKE    0.40f
#define GX_SCENE_PHYSICS_PUMP_RATE      1.30f
#define GX_SCENE_PHYSICS_PUMP_SPIN      1.10f
#define GX_SCENE_PHYSICS_PUMP_LEG       2.40f

//ai: terrain mound — a static heightfield ridge just past the belt's far
//   end (the belt feeds it; items tumble down the slope). The wrapped tile
//   is large so its repeats land outside the yard; the mesh renders a window
//   around the mound only. GX_PHYSICS_MOUND=0 skips it.
#define GX_SCENE_PHYSICS_MOUND_X        9.20f
#define GX_SCENE_PHYSICS_MOUND_Z        3.60f
#define GX_SCENE_PHYSICS_MOUND_TILE     30.0f
#define GX_SCENE_PHYSICS_MOUND_SAMPLES  40
#define GX_SCENE_PHYSICS_MOUND_RADIUS   2.30f
#define GX_SCENE_PHYSICS_MOUND_HEIGHT   0.60f
#define GX_SCENE_PHYSICS_MOUND_MESH     13

//ai: light-speed lane (the relativity demo) — a slick iron slug parked on
//   the yard's near side and launched along +x through the prop field into
//   the pool at 0.92c; the relativity applier clamps its speed below c and
//   scales its mass by gamma. GX_PHYSICS_RELATIVITY=0 skips the slug and
//   disables the applier; GX_PHYSICS_LIGHTSPEED=<m/s> sets c.
#define GX_SCENE_PHYSICS_SLUG_X        -6.00f
#define GX_SCENE_PHYSICS_SLUG_Z        -2.40f
#define GX_SCENE_PHYSICS_SLUG_RADIUS    0.16f
#define GX_SCENE_PHYSICS_SLUG_LAUNCH    0.92f

typedef enum gX_ScenePhysics_JointKind {
    gX_SCENE_PHYSICS_JOINT_BALL = 0,
    gX_SCENE_PHYSICS_JOINT_HINGE = 1,
    gX_SCENE_PHYSICS_JOINT_HINGE_MOTOR = 2,
    gX_SCENE_PHYSICS_JOINT_SLIDER = 3
} gX_ScenePhysics_JointKind;

//ai: one constrained rig body (chain link, door, spinner bar, rail carriage,
//   rover part) plus the GFX model that draws it. Boxes and Z-cylinders;
//   size feeds the debug wireframe (cylinders draw as their bounding box).
typedef struct gX_ScenePhysics_Link {
    gX_PhysicsBody body;
    gX_I32 modelIndex;        // GFX model heap index, -1 = none
    //ai: optional second model sharing the link's transform (the rover's
    //   wheel spokes, so the spin reads on symmetric cylinders).
    gX_I32 extraModel;
    gX_F32v3 size;            // full extents (body + mesh + debug)
    gX_ScenePhysics_PropKind shape;
    gX_F32v3 spawnPos;
    gX_F32Qua spawnQuat;      // spawn orientation (steered wheels)
    gX_F32v3 prevPos;
    gX_F32Qua prevQuat;
} gX_ScenePhysics_Link;

//ai: joint bookkeeping — kind drives the debug drawing (anchor dot, axis
//   line); the handle is needed to destroy rigs on clear/park.
typedef struct gX_ScenePhysics_JointInfo {
    gX_PhysicsJoint joint;
    gX_ScenePhysics_JointKind kind;
    gX_F32v3 anchor;
    gX_F32v3 axis;
} gX_ScenePhysics_JointInfo;

//ai: toolgun aims — a GFX model pick resolved to the sandbox object behind
//   it: the slot, the body the tool acts on (a ragdoll's aimed bone) and the
//   body's position for the tool's aim point math.
typedef enum gX_ScenePhysics_AimKind {
    gX_SCENE_PHYSICS_AIM_NONE = 0,
    gX_SCENE_PHYSICS_AIM_PROP,
    gX_SCENE_PHYSICS_AIM_LINK,
    gX_SCENE_PHYSICS_AIM_RAGDOLL
} gX_ScenePhysics_AimKind;

typedef struct gX_ScenePhysics_Aim {
    gX_ScenePhysics_AimKind kind;
    gX_I32 index;            //ai: prop/link/ragdoll slot
    gX_I32 bone;             //ai: ragdoll bone index, -1 otherwise
    gX_PhysicsBody body;
    gX_F32v3 position;       //ai: body world position at resolve time
} gX_ScenePhysics_Aim;

#define GX_SCENE_PHYSICS_MAX_PINS 8

//ai: a toolgun pin — a ball joint from a body to the static frame at the aim
//   point. Pins live in their own pool (the rig joints are content) and die
//   with the content, the R reset or an explicit unpin.
typedef struct gX_ScenePhysics_Pin {
    gX_PhysicsBody body;
    gX_PhysicsJoint joint;
    gX_F32v3 anchor;
} gX_ScenePhysics_Pin;

//ai: kinematic mover — a driven platform (lift) or spinning turntable. The
//   body is kinematic (velocity-steered by the scene, never by forces), the
//   model syncs with the same sub-step interpolation as props.
typedef struct gX_ScenePhysics_Mover {
    gX_PhysicsBody body;
    gX_I32 modelIndex;       // GFX model heap index, -1 = none
    gX_I1 isTable;           // lift (0) or turntable (1)
    gX_F32 phase;            // lift phase (rad)
    gX_F32 baseY;            // lift base height
    gX_F32 rate;             // turntable angular rate (rad/s)
    gX_F32v3 spawnPos;       // spawn pose (reset)
    gX_F32v3 prevPos;
    gX_F32Qua prevQuat;
} gX_ScenePhysics_Mover;

typedef struct gX_ScenePhysics {
    gX_I8 name[128];
    gX_I8 description[256];
    gX_I1 loaded;
    //ai: contact materials — props take per-element friction/restitution,
    //   rigs steel, ragdolls tissue and the ground a rough dead surface;
    //   GX_PHYSICS_MATERIALS=0 keeps the wrapper defaults.
    gX_I1 materialsEnabled;

    gX_ScenePhysics_Prop props[GX_SCENE_PHYSICS_MAX_PROPS];
    gX_I32 propCount;        // slots used (dead slots stay, modelIndex = -1)
    gX_I32 liveCount;
    gX_I32 spawnTotal;       // staggered-entry target count

    //ai: ground geometry outlives scene entries (the visual slab is rebuilt
    //   with the rest of the GFX content).
    gX_PhysicsCollider groundCollider;
    gX_I32 groundModelIndex;
    //ai: conveyor belt statics (persist like the ground).
    gX_PhysicsCollider beltCollider;

    //ai: kinematic movers — driven platforms built per entry (no statics).
    gX_I1 moversEnabled;
    gX_I32 moverPayloads;    // payload crates spawned (plan tail)
    gX_ScenePhysics_Mover movers[GX_SCENE_PHYSICS_MOVERS];
    gX_I32 moverCount;

    //ai: balance posts (mass-center demo) — prop indices; GX_PHYSICS_COM=0
    //   skips them and B kicks whichever are still live.
    gX_I1 balanceEnabled;
    gX_I32 balanceCount;
    gX_I32 balance[GX_SCENE_PHYSICS_BALANCE_COUNT];

    //ai: rover (hinge2 vehicle) — link indices; GX_PHYSICS_ROVER=0 skips.
    gX_I1 roverEnabled;
    gX_I32 roverLinkFirst;
    gX_I32 roverLinkCount;
    gX_I32 roverJointFirst;

    //ai: break target — the welded board's link and tracked-joint indices
    //   plus its broken flag; GX_PHYSICS_TARGET=0 skips. R rewelds it.
    gX_I1 targetEnabled;
    gX_I1 targetBroken;
    gX_I32 targetLink;
    gX_I32 targetJoint;

    //ai: piston pump — the plunger link/joint and the drive phase;
    //   GX_PHYSICS_PUMP=0 skips. The mound collider persists like the belt.
    gX_I1 pumpEnabled;
    gX_I32 pumpLink;
    gX_I32 pumpJoint;
    gX_F32 pumpPhase;
    gX_I1 moundEnabled;
    gX_PhysicsCollider moundCollider;
    //ai: relativity — the light-speed lane's slug (prop slot, -1 = none)
    //   plus the applier gate; the knob lives in the tuning.
    gX_I1 relativityEnabled;
    gX_I32 slugProp;
    //ai: bumps on every content (re)build/park — the physics editor re-pulls
    //   its tuning copy when the epoch changes so a stale copy cannot
    //   override a fresh entry's env defaults.
    gX_U32 contentEpoch;

    //ai: mouse drag state — the per-frame hover pick feeds the HUD, the
    //   spring target feeds the step hook (a per-frame force at 60 Hz would
    //   be consumed by the first sub-step).
    gX_I32 dragProp;
    gX_I32 dragLink;         //ai: dragged joint rig part (link pool index)
    gX_F32v3 dragOffset;
    gX_I1 dragActive;
    gX_F32v3 dragTarget;
    gX_I32 hoverProp;
    gX_I32 hoverLink;        //ai: hovered joint rig part

    //ai: tool mode — while an editor's toolgun owns the buttons the sandbox's
    //   own mouse interaction stands down (its keys keep working); the gun's
    //   pins are the only joints the scene did not build itself.
    gX_I1 toolMode;
    gX_ScenePhysics_Pin pins[GX_SCENE_PHYSICS_MAX_PINS];
    gX_I32 pinCount;

    //ai: electromagnetism — charged props (alternating signs) plus an
    //   optional uniform B field. M toggles it; GX_PHYSICS_EM / _B presets it.
    gX_I1 emEnabled;
    gX_F32v3 emB;

    //ai: thermal — initial prop temperature offset above ambient
    //   (GX_PHYSICS_TEMP, K) and the H-key heat energy (GX_PHYSICS_HEAT).
    gX_F32 heatInitial;
    gX_F32 heatEnergy;

    //ai: ragdoll pool — slots hold one humanoid each; boneModel[] mirrors
    //   the ragdoll's limbs in the GFX heap.
    gX_ScenePhysics_RagdollSlot ragdolls[GX_SCENE_PHYSICS_MAX_RAGDOLLS];
    gX_I32 ragdollCount;
    gX_I32 ragdollSpawned;   // deterministic placement counter (T key fallback)

    //ai: joint rigs — constrained bodies (GX_PHYSICS_JOINTS=0 skips) and
    //   their joints; joints are destroyed before the bodies on clear/park.
    gX_I1 jointsEnabled;
    gX_ScenePhysics_Link links[GX_SCENE_PHYSICS_MAX_LINKS];
    gX_I32 linkCount;
    gX_ScenePhysics_JointInfo joints[GX_SCENE_PHYSICS_MAX_JOINTS];
    gX_I32 jointCount;

    //ai: water — the basin statics persist across entries (like the ground);
    //   the water volume model and the buoy props rebuild per entry. Buoys
    //   occupy prop slots (element NONE), so drag/delete/reset just work.
    gX_I1 waterEnabled;
    gX_I1 buoysSpawned;
    gX_I32 waterModel;               // translucent volume, -1 = none
    gX_PhysicsCollider poolWall[4];  // basin statics (persist)

    //ai: magnets — bar-magnet props (element NONE) whose local moment is
    //   registered with the EM applier; N toggles the magnetic forces.
    gX_I1 magnetsEnabled;
    gX_I1 magnetsOn;
    gX_F32 magnetScale;      //ai: sim-scaled mu0/4pi (editor-tunable)
    //ai: impact capture installed (shared by sparks/debug/decals) — the
    //   editor toggles reinstall/remove it only on transitions.
    gX_I1 impactCapture;

    //ai: impact FX — contact-event queue (callback fills, Update drains),
    //   the reserved spark emitters and their burst timers.
    gX_I1 sparksEnabled;
    gX_ScenePhysics_Impact impacts[GX_SCENE_PHYSICS_MAX_IMPACTS];
    gX_I32 impactHead;
    gX_I32 impactCount;
    gX_I32 sparkEmitter[GX_SCENE_PHYSICS_SPARK_EMITTERS];
    gX_I32 sparkEmitterCount;
    gX_F32 sparkTimer[GX_SCENE_PHYSICS_SPARK_EMITTERS];
    gX_I32 sparkNext;

    //ai: scorch decals — the scorch texture index and the renderer's decal
    //   pass flag saved while the sandbox owns it.
    gX_I1 decalsEnabled;
    gX_I1 decalsPrev;
    gX_I32 scorchTexture;
    //ai: impact audio — plays the drained impact queue through the engine's
    //   procedural SFX pool (GX_PHYSICS_SFX=0 disables; the tuning has the
    //   live toggle).
    gX_I1 sfxEnabled;
    //ai: pre-existing (renderer default) emitters are silenced while the
    //   sandbox owns the particle pass; their active flags are restored at
    //   park so other scenes get their emitters back untouched.
    gX_I1 sparkSilenced[GX_SCENE_PHYSICS_SILENCED_MAX];
    gX_I32 sparkSilencedCount;

    //ai: debug view — K toggles; the callback records current-frame contacts.
    gX_I1 debugDraw;
    gX_ScenePhysics_Contact debugContacts[GX_SCENE_PHYSICS_DEBUG_CONTACTS];
    gX_I32 debugContactWrite;

    //ai: entry camera set once — keeps user navigation across re-entries
    gX_I1 cameraSet;
} gX_ScenePhysics;

gX_I32 gX_ScenePhysics_Load(gX_ScenePhysics *scene);
gX_I32 gX_ScenePhysics_Unload(gX_ScenePhysics *scene);
//ai: scene-switch exit — destroy props/ragdolls and detach the step hook
//   and collision callback so nothing simulates while another scene runs.
gX_I0  gX_ScenePhysics_Park(gX_ScenePhysics *scene);
//ai: impact capture — install/remove the contact-event callback on the
//   shared world (the queue lives in the scene; Enable resets it). The
//   getter peeks a pending entry, 0 = oldest.
gX_I0  gX_ScenePhysics_EnableImpactCapture(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_DisableImpactCapture(gX_ScenePhysics *scene);
gX_I32 gX_ScenePhysics_ImpactCount(gX_ScenePhysics *scene);
gX_I1  gX_ScenePhysics_ImpactGet(gX_ScenePhysics *scene, gX_I32 index,
    gX_ScenePhysics_Impact *out);
//ai: ragdoll applier registration — bones join the active EM (charge sign
//   from the slot) and thermal (tissue constants + per-bone mass) registries;
//   call before destroying the ragdoll's bodies.
gX_I0  gX_ScenePhysics_RagdollRegister(gX_ScenePhysics *scene,
    gX_ScenePhysics_RagdollSlot *slot);
gX_I0  gX_ScenePhysics_RagdollUnregister(gX_ScenePhysics *scene,
    gX_ScenePhysics_RagdollSlot *slot);
//ai: joint rig builder — chain/door/windmill/rail into the scene pools;
//   exported for the headless rig tests (no renderer needed).
gX_I0  gX_ScenePhysics_BuildJointRigs(gX_ScenePhysics *scene);
//ai: water builder — pool basin statics plus the filled state (water volume,
//   buoys, fluid registry); exported for the headless water tests.
gX_I0  gX_ScenePhysics_BuildWater(gX_ScenePhysics *scene);
//ai: magnet builder/toggle — the bar magnets are scene content; the toggle
//   enables the applier's dipole forces (and the charges' Coulomb mode stays
//   independent). Both are exported for the headless magnet tests.
gX_I0  gX_ScenePhysics_BuildMagnets(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_SetMagnets(gX_ScenePhysics *scene, gX_I1 enabled);
//ai: impact audio layer toggle (env GX_PHYSICS_SFX / editor tuning; the
//   impact-sound mapping above is pure). Installing the layer also installs
//   the shared impact capture when nothing else needs it.
gX_I0  gX_ScenePhysics_SetSFXEnabled(gX_ScenePhysics *scene, gX_I1 enabled);
gX_I1  gX_ScenePhysics_IsSFXEnabled(gX_ScenePhysics *scene);
//ai: conveyor belt builder — the static strip with a belt surface velocity;
//   exported for the headless belt test.
gX_I0  gX_ScenePhysics_BuildBelt(gX_ScenePhysics *scene);
//ai: kinematic movers — the lift/turntable content plus the per-step drive
//   (the step hook calls it; exported for the headless mover test).
gX_I0  gX_ScenePhysics_BuildMovers(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_DriveMovers(gX_ScenePhysics *scene, gX_F32 dt);
//ai: one mover payload crate (index 0 = lift, 1 = turntable); exported for
//   the headless ride test — SpawnNext calls it at the tail of the plan.
gX_I32 gX_ScenePhysics_SpawnPayload(gX_ScenePhysics *scene, gX_I32 index);
//ai: balance posts — the mass-center content and the B nudge (exported for
//   the headless mass-center test).
gX_I0  gX_ScenePhysics_BuildBalancePosts(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_NudgeBalance(gX_ScenePhysics *scene);
//ai: rover — the hinge2 cart (exported for the headless hinge2 test).
gX_I0  gX_ScenePhysics_BuildRover(gX_ScenePhysics *scene);
//ai: break target — the feedback-welded board plus the per-step break check
//   and R's content reset; all exported for the headless break test.
gX_I0  gX_ScenePhysics_BuildBreakTarget(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_UpdateBreakTarget(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_ResetContent(gX_ScenePhysics *scene);
//ai: piston pump and terrain mound (exported for the headless tests).
gX_I0  gX_ScenePhysics_BuildPump(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_DrivePump(gX_ScenePhysics *scene, gX_F32 dt);
gX_I0  gX_ScenePhysics_BuildMound(gX_ScenePhysics *scene);

//ai: relativity lane — the slug builds once the prop plan is full (a slick
//   iron ball on the lane start); LaunchSlug re-poses it and fires it at
//   0.92c. Both are exported for the headless pins.
gX_I32 gX_ScenePhysics_BuildSlug(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_LaunchSlug(gX_ScenePhysics *scene);

//ai: toolgun surface — the editor's tools act on aims (a model pick resolved
//   to a prop/link/ragdoll bone) instead of a mouse position, so the same
//   actions work under the crosshair; the aim resolve also feeds the HUD's
//   hover lines. Actions are exported for the headless tool pins.
gX_ScenePhysics_Aim gX_ScenePhysics_AimAtModel(gX_ScenePhysics *scene,
    gX_I32 modelIndex);
//ai: tool mode stands the sandbox's own mouse interaction down and drops any
//   held drag when it ends.
gX_I0  gX_ScenePhysics_SetToolMode(gX_ScenePhysics *scene, gX_I1 on);
//ai: spring drag from the aim ray — the held body chases the point along the
//   crosshair at its current distance (release keeps the velocity).
gX_I0  gX_ScenePhysics_ToolGrab(gX_ScenePhysics *scene,
    gX_ScenePhysics_Aim aim, gX_F32v3 rayOrigin, gX_F32v3 rayDirection,
    gX_I1 pressed, gX_I1 held);
gX_I0  gX_ScenePhysics_ToolRemove(gX_ScenePhysics *scene,
    gX_ScenePhysics_Aim aim);
gX_I0  gX_ScenePhysics_ToolPin(gX_ScenePhysics *scene, gX_ScenePhysics_Aim aim,
    gX_F32v3 anchor);
gX_I0  gX_ScenePhysics_ToolUnpin(gX_ScenePhysics *scene,
    gX_ScenePhysics_Aim aim);
gX_I32 gX_ScenePhysics_PinCount(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_UnpinAll(gX_ScenePhysics *scene);

//ai: physics editor surface — the sandbox's engine-tunable parameters. The
//   editor edits a live copy; presets are JSON under resources/tools/physics
//   (save refuses shipped paths while GX_NO_SETTINGS_SAVE is set). Every
//   field applies live except prop/ragdoll counts, which fill the stagger
//   plan / spawn the missing dummies; pool geometry stays scene content.
typedef struct gX_ScenePhysics_Tuning {
    gX_F32 gravity;          //ai: downward acceleration (m/s^2)
    gX_I32 tuneLevel;        //ai: pinned quality level, -1 = adaptive
    gX_I32 props;            //ai: target prop count (spawns the remainder)
    gX_I32 ragdolls;         //ai: target ragdoll count (spawns the missing)
    gX_I1 materials;
    gX_I1 joints;            //ai: live rig rebuild
    gX_I1 water;             //ai: live pool fill/drain
    gX_I1 magnetsOn;         //ai: live magnetic forces
    gX_I1 sparks;            //ai: live emitter capture
    gX_I1 decals;            //ai: live scorch pass
    gX_I1 sfx;               //ai: live impact sound layer
    gX_I1 relativity;        //ai: live light-speed clamp + mass dilation
    gX_F32 lightSpeed;       //ai: the sim's c (m/s)
    gX_PhysicsEM_Settings em;
    gX_PhysicsThermal_Settings thermal;
    gX_F32 fluidDensity;     //ai: fluid region density (tonne units)
    gX_F32 fluidDrag;        //ai: quadratic drag coefficient
    gX_F32 fluidLinearDrag;
    gX_F32 fluidAngularDrag;
} gX_ScenePhysics_Tuning;

gX_ScenePhysics_Tuning gX_ScenePhysics_Tuning_Defaults(gX_I0);
//ai: push a tuning into the live scene + shared appliers.
gX_I0 gX_ScenePhysics_Tuning_Apply(gX_ScenePhysics *scene,
    const gX_ScenePhysics_Tuning *tuning);
//ai: JSON preset round-trip (json-c; the GFX preset file conventions).
gX_I32 gX_ScenePhysics_Tuning_Save(const gX_I8 *path,
    const gX_ScenePhysics_Tuning *tuning);
gX_I32 gX_ScenePhysics_Tuning_Load(const gX_I8 *path,
    gX_ScenePhysics_Tuning *out);
//ai: contact material mapping — the element table's nominal friction and
//   restitution as a wrapper material (exported for the material pins).
gX_PhysicsMaterial gX_ScenePhysics_MaterialForElement(gX_I32 element);
gX_I0  gX_ScenePhysics_Update(gX_ScenePhysics *scene);
gX_I0  gX_ScenePhysics_Draw(gX_ScenePhysics *scene);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_PHYSICS_H
