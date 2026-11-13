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
| S-001 | Embodied NPC (ENPC) | not implemented; no plan | An original gX term for an embodied character that explores a world and interacts with it. The body-first contract is in [`active_ragdoll_lab.md`](active_ragdoll_lab.md); the visual lab, hybrid locomotion and ENPC behavior are not implemented, and the contract keeps ENPC behavior out of the lab. Clean-room, no third-party animation/physics material. Revisit only after the hybrid controller, get-up state machine and performance budget exist, and the owner explicitly promotes it. |

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
