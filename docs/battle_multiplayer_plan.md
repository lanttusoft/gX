# Battle client/server proposal — 2026-10-05

<!-- //ai: future architecture plan only; networking is not implemented or release-admitted. -->

## Status and first scope

The author requested a plan for online Battle. This opens **design discussion**
for networking, not implementation approval, a release promise or a change to
the lean budget. Stabilization and the current source-grounded proof batch
remain active priorities. No sockets, dependencies, services or network exposure
are added by this document.

Recommended first slice: **cooperative control of the existing three-member
party**, one human-controlled actor per connected player, against server-run
enemies. Unassigned companions retain autofight. Preserve free movement/facing,
independent realtime ATB, positional combat, actor-owned HP/MP/cooldowns and
control ownership. Shared consumables remain server-owned shared resources.
PvP, matchmaking, accounts, persistent progression, host migration, spectator
services and public deployment are separate scope decisions.

## Existing foundation and gaps

`src/scene/battle_runtime.{c,h}` already separates simulation from rendering:
three party actors, up to two enemies, a 9×9 occupancy grid, bounded impacts,
two waves and independent actor gauges. `Runtime_Update` accepts elapsed time
and delta; `MoveActor`, `TurnActor`, `ConfirmActorAction` and controller helpers
offer candidate authority entry points. The CPU-only suite already exercises
combat, movement/facing, occupancy, cooldowns, AI, waves and outcomes.

That foundation is **not a network/security boundary**:

- Actor command functions check non-AI control, but do not authenticate a caller
  or prove that the connected player owns that actor.
- `controllerId` is a small local value, not a connection credential. The setter
  can assign non-AI IDs, but no session/ownership protocol exists.
- `targetIndex` is global, and `SelectTarget` is local-controller-oriented.
  Multiple clients cannot safely share a mutable local HUD selection.
- `EnsureLocalController` restores local control after defeat, potentially
  replacing an existing remote owner. Dedicated-server ownership needs an
  explicit policy, not this automatic local fallback.
- Live polisher/rule changes must become server-approved encounter state;
  clients must not modify combat tuning independently.
- Delta-driven floating-point state is not certified cross-platform deterministic.
  “Headless runtime” does not yet mean a standalone server executable without
  renderer/audio/window initialization; entry point and dependency roots need work.

## Authority and process boundaries

```text
Client input/UI → intent commands → server validation/ownership → Battle runtime
Client view/UI  ← snapshots/events ← server tick and authoritative resolution
```

**Server owns:** simulation clock/tick, rules, roster/entity identity, ownership,
occupancy, actor HP/MP, ATB and cooldowns, shared item consumption, damage/healing,
enemy/companion AI, cached telegraphs, waves and outcomes.

**Client owns:** camera, rendering/effects, UI focus/target preview, audio,
interpolation and command submission. A target highlight is local presentation;
the action command supplies a validated target identity. Clients never submit
HP, damage, ATB completion, item counts or a claimed authoritative timestamp.

Listen-host mode should use the same command/validation path as remote clients;
hosting must not create a privileged bypass. Offline play can eventually use
an in-process authority adapter, preserving the familiar local demo without
requiring a socket or duplicating combat logic.

## Command and ownership contract

Proposed command envelope: protocol/session epoch, bounded sequence number,
client's last observed server tick, actor ID/generation, command kind and bounded
payload. Requests include move direction, quarter-turn, action+target, and actor
control-transfer requests. Sequence/tick fields are hints for ordering/dedup,
not permission or permission to backdate execution.

1. Resolve the authenticated connection to a server-controlled player/session.
2. Reject incompatible protocol/rules identity, stale epoch/generation, malformed
   payloads, invalid enums/indices, oversized messages and excessive rate/queue use.
3. Verify current ownership and actor liveness on the server.
4. Apply server timing and the existing action/range/facing/occupancy/resource
   rules. Movement remains ATB-free, but packet floods must not grant faster
   movement than the intended input cadence; define that cadence before coding.
5. Deduplicate commands within a bounded session window. An acknowledgement
   distinguishes accepted, rejected and already-handled commands. Resource
   mutations occur at most once; UI retries do not consume another potion.

Actor ownership changes are atomic server transactions. Tab requests a transfer
to a living, available actor; it cannot silently steal another player's actor.
Competing requests are resolved in the server's recorded order. One player owns
at most one actor in the first slice; one actor has at most one human owner.
No player can impersonate another by sending their `controllerId`.

Action and target validation/resolution must be atomic. Refactor the simulation
boundary to carry actor/action/target explicitly instead of setting global
`targetIndex` and hoping later commands do not change it. Keep actor resources,
enemy intents and grid mutations in the existing simulation, not a cloned
“network battle” implementation.

## Tick, ordering and replication

- Proposed starting point: **60 Hz server simulation**, independent of client
  render FPS. Measure before freezing the rate; convert server ticks to the
  runtime's elapsed/delta inputs in one place. Clients do not control that clock.
- Define and test tick phases: bounded validated command queue → sequential
  command resolution → ATB/cooldown/AI advance → outcome/ownership handling →
  snapshot/event publication. The exact ATB threshold/command boundary is a
  gameplay contract to settle, not an implementation accident.
- Cap catch-up work without silently granting arbitrary elapsed time or
  freezing cooldowns. Record overload behavior and avoid starvation/floods.
- Start with full snapshots of this small roster, perhaps **20 Hz** plus critical
  acknowledgements/events. Include server tick, session/encounter epoch,
  actor IDs/generations, ownership, combat/grid state, enemy telegraphs and
  outcome. A newly joined client needs enough state to render without old events.
- Impact/event IDs prevent duplicate floaters/audio. The existing eight-entry
  impact ring is not a reliable network event queue; snapshots must not depend
  on receiving every past impact.
- Use explicit bounded field serialization, specified units/endian/version and
  validated finite numeric ranges. Never transmit raw C structs, pointers,
  padding, arbitrary resource paths or renderer/GPU objects.

Use authoritative snapshots rather than deterministic lockstep initially.
Render interpolation can smooth grid transitions without changing occupancy.
Start without prediction/rollback; add local movement prediction only if measured
latency warrants it, with reconciliation against server occupancy/ownership.
Fixed ticks do not prove IEEE determinism, especially across platforms/builds.
Replays record authoritative command order/rules/clock and checkpoints; bit-exact
cross-platform replay remains a separate validation problem.

## Join, disconnect and reconnect

First slice: fixed encounter roster; joining occupies an available living actor
under server policy. Reject excess players clearly. Specify whether mid-encounter
joining is allowed before implementation; it is not an accidental side effect.

On disconnect, the server either freezes that ownership during a short bounded
grace period or releases it to companion AI according to a documented policy.
Battle time continues; a missing client must not stall everyone indefinitely.
On death, ownership may transfer only to an unowned/AI survivor, not overwrite a
remote owner. Shared potion use and simultaneous action claims remain serialized.

Reconnect authenticates a fresh connection with a bounded-expiry rejoin token,
rotates connection credentials, and sends a full current snapshot. Old-epoch
commands are rejected. Client state never overwrites authoritative progress.
The initial server is authoritative and nonmigrating: server loss ends the
session with an explicit error rather than inventing host migration.

## Transport/security decisions before Internet release

Keep simulation independent of transport through a bounded command/snapshot
adapter. Begin with an in-memory fake transport, then local two-process tests.
A reliable framed stream is a candidate for the small cooperative demo, not a
final selection: head-of-line blocking/backpressure versus authenticated datagram
transport must be measured. Define framing, size limits, timeouts, malformed-data
handling and reconnect behavior before selecting a dependency.

Loopback/LAN experimentation is **not Internet readiness**. Public online use
needs authenticated encrypted transport, credential/rejoin-token handling,
authorization, version/rules negotiation, bounded parser/connection/queue costs,
rate limits, abuse/fuzz testing and a reviewed deployment/update plan. Do not
invent cryptography or ship unauthenticated public listeners by default.
Bind locally by default during development; opening ports or hosting is a
separate explicit operation. NAT traversal/relay discovery is deferred and no
hosted service is assumed. Minimize personal data and never log credentials.

## Implementation milestones (future authorization required)

| Slice | Deliverable | Admission evidence |
| --- | --- | --- |
| A. Authority seam | Explicit actor/action/target commands; owner table; clock/tick wrapper; offline compatibility | Existing Battle tests plus unauthorized actor, atomic transfer/target, concurrent occupancy/item and ATB-boundary tests. |
| B. Wire model | Bounded codec, sequence/epoch/generation rules, snapshots and event identity; fake transport | Round trips, reject malformed/oversize/nonfinite values, duplicate/reordered/stale messages, queue overflow and resync. |
| C. Local client/server | Headless server entry point; two local clients; AI fallback, joins and disconnects | No GPU/window in server; one authority for shared resources; loopback reconnect and offline-vs-authority behavioral parity. |
| D. Impaired network | Loss/latency/jitter/duplication/backpressure simulation; client interpolation | At-most-once resource mutations, bounded queues, no ownership theft/stalls; eventual resync under stated delivery assumptions. |
| E. Online readiness | Chosen authenticated transport and reviewed Internet deployment | Security/fuzz/resource-limit gates and explicit user approval; not implied by LAN success. |

Potential math contracts: ownership uniqueness, rejection preserving state,
at-most-once command application per epoch, occupied-cell exclusivity, atomic
control transfer and tick/ATB bounds. Prove the actual implemented model with
source/native witnesses; algebra does not prove authentication, network liveness
or floating-point determinism. Loss/reconnect guarantees need explicit delivery,
timeout and process-failure assumptions.

## Size, licensing and unresolved choices

Networking is opt-in in any future build design; evaluate separate server/client/
offline profiles with explicit budgets. Keep the default Release's recovered
headroom visible; do not quietly add a network stack or lift its cap. Measure
dependency/code/BSS/queue costs before admission and preserve GPLv3 compatibility.
No gameX network code, copyrighted assets or experiment data are imported.

Before slice A: confirm cooperative-only scope, join timing, disconnect/AI policy,
input cadence, command/tick phase ordering and desired server platform. Before
slice C: select transport, protocol limits, authentication/reconnect approach and
deployment boundaries. Larger rosters/PvP/persistence remain separate proposals.
