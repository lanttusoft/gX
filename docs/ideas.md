# Ideas and decisions

<!-- //ai: durable idea/outcome index, seeded from the documented console discussion. -->

Keep ideas after they are implemented, postponed or declined. This is project
memory, not a promise to build everything here. Feature contracts and detailed
work lists stay in the linked subsystem documents; Git keeps the history.

## How to use it

1. Before proposing work, search this file **and the linked backlog** for the
   problem, synonyms and existing solutions. Reuse the existing ID when the
   idea is the same, even if its name changes.
2. Give a genuinely new idea the next unused `GX-I###` ID. Record its scope,
   status, date, reason and source. A proposal is not implementation approval.
3. Update the same row when a decision or implementation changes. State what
   remains undone; link the contract, implementation/commit and applicable
   verification. Distinguish local/branch work from merged or released work.
4. Do not delete closed ideas or recycle IDs. For a duplicate, link to the
   canonical ID; for a replacement, keep the old row as `superseded` and name
   its replacement. Preserve the old reason in a dated note when it changes.
5. Reopen `deferred` or `declined` work only with new evidence or a changed
   requirement: record what changed and get the maintainer's decision. A new
   session or a different title is not a reason to propose it again.

### Statuses

| Status | Meaning |
| --- | --- |
| proposed | Captured for consideration; not approved or scheduled. |
| accepted | Maintainer agreed the scope; implementation has not started. |
| in-progress | Active work, including implementation awaiting its gates. |
| partial | Some named parts exist; the remaining parts are explicit. |
| done | The stated scope is implemented and its applicable gates passed; not necessarily merged or released. |
| deferred | Intentionally parked; record why and when to revisit. |
| declined | Not proceeding under the current requirements; keep the reason. |
| superseded | Replaced or duplicated; point to the current approach/ID. |

Dates below are **last reviewed**, not invented dates of the original ideas.
The initial index covers the recent gX-240 discussion, not the whole project's
history. Do not infer acceptance from an old brainstorming list.

## Idea index

| ID | Idea / search terms | Status | Outcome, remaining work or revisit condition | Last reviewed | Source / evidence |
| --- | --- | --- | --- | --- | --- |
| GX-I001 | Independent 240p scene render / native GUI | done | Scene render target and present filter are independent of the native GUI/window. Do not propose this existing split as new work; it is not supersampling (GX-I008). | 2026-11-05 | [gX mapping](console_240p.md#gx-mapping), `editor-host-check` |
| GX-I002 | Console Max quality without forcing all effects | done | Max preserves authored feature masks and raises quality. Full remains experimental. Do not silently make Max force SSR/TAA/DOF again; see GX-I007. | 2026-11-05 | [Current slice](console_240p.md#current-slice), `editor-host-check` |
| GX-I003 | Cartridge ESC / system / pause menu | done | Resume, slot-1 save/load, video cycle and Exit to Shell reuse the pause overlay. Local commit `73e272a`; not a publishing claim. | 2026-11-05 | [Presets](console_240p.md#presets), `console-check` |
| GX-I004 | Explicit console controller action map | done | Slot 0: D-pad navigation, south confirm, east back, Start menu. Local commit `c93c120`; focused and full checks passed. Not merged/published; physical controllers are not validated. | 2026-11-05 | [Controller/menu contract](console_240p.md#presets), `src/input.c`, `src/gui/element/element.c`, `console-check` |
| GX-I005 | Compact native console shell / saves / pause layout | done | Native-coordinate, aspect-fitted panels are implemented in local commit `c93c120`. Three-size console, fast/full, size and shared-GUI interaction gates passed. Not merged/published or a 320x240 GUI (GX-I006). | 2026-11-05 | [Compact native UI](console_240p.md#presets), `src/gui/templates/titlegx.c`, `src/gui/templates/templates.c`, `console-check` |
| GX-I006 | Move console GUI coordinates to 320x240 | deferred | The compact native layout (GX-I005) was chosen for this slice. A true 240p GUI remains separate work, not a missing part of that layout. Revisit only for an explicit low-resolution-GUI requirement with readable text and input-mapping criteria. | 2026-11-05 | [Native/scene boundary](console_240p.md#current-slice), GX-I001, GX-I005 |
| GX-I007 | Force Full / every effect through Console Max | superseded | Replaced by quality-only Max (GX-I002): forcing SSR/TAA/DOF caused scene artifacts. Full is still an opt-in experimental template, not the default console quality path. | 2026-11-05 | [Max review and measurements](console_240p.md#current-slice), GX-I002 |
| GX-I008 | Supersampled render/output split / SSAA to 240p | partial | User approved and implemented the 2x slice in local commit `a83126d`: 640x480 scene render, 320x240 resolve, native GUI/OSD, opt-in shell/pause video mode, save-state persistence, Y2K + RAY-1 direct-blit coverage, particle GUI-composite coverage in local commit `92e921c` and a <=3x GPU-cost gate (1.52x measured). A 4x factor remains a follow-up. Not merged/published. | 2026-11-06 | [Presets](console_240p.md#presets), [gates](console_240p.md#gates), `scripts/console_ssaa_check.py` |
| GX-I009 | PRISM-1 dither / quantization / composite / CRT profile | done | User approved RGB565 (5:6:5) + fixed 4x4 Bayer + horizontal scanlines + a subtle RGB triad, layered after the 320x240 resolve. Implemented in local commit `458dfb3` as an independent shell/pause toggle with settings and save-state persistence; Y2K/RAY-1/particle cover standard 240p and 2x SSAA through the direct-blit and GUI-composite routes; particle coverage landed in local commit `92e921c`. Deterministic gate checks level counts 26/47/27, scanline 1.386x, triad phases and a full-frame CPU guard about 1.0x. No NTSC composite smear by contract. Not merged/published. | 2026-11-06 | [Presets](console_240p.md#presets), [gates](console_240p.md#gates), [PRISM-1](rendering_backlog.md#p1--console-prism-1-output), `scripts/console_prism_check.py` |
| GX-I018 | Timestamp/provenance markers for Lean proof vs C contract evidence | proposed | Add timestamped evidence (or a freshness check) to Lean proof audits and C contract checks so cross-language results can be distinguished from stale artifacts and their ordering can be audited. No implementation yet; a concrete contract is needed for where stamps live (logs, artifacts, lock/manifest) without making reproducible builds non-deterministic. | 2026-11-06 | [Lean proofs](lean_proofs.md), [release hardening](release_hardening.md), `gates.toml` |
| GX-I010 | Richer console OSD / save status / frame advance / fast-forward | done | Boot banner, live native video/cartridge/slot status, save/load/failure feedback (local `d9eb892`) and real pause/step/2x/4x/8x playback (local `af0af32`) are implemented. Playback uses a separate simulation clock; UI/OSD/audio stay real-time; F6/F7 work only while the menu is open; loads, scene changes and Exit to Shell reset playback. OSD/playback/console, fast/full, 12/12 shared-GUI and proof gates passed. Not merged/published. | 2026-11-06 | [Presets](console_240p.md#presets), [follow-up](console_240p.md#follow-up), `scripts/console_osd_check.py`, `scripts/console_playback_check.py` |
| GX-I011 | More authored console cartridge profiles | done | Y2K's fixed profile, RAY-1's Full-quality profile (local commit `8fe6c51`) and the particle sandbox profile are implemented. All three boot with authored scene/camera defaults, console pause/save support and save-state restore; RAY-1 and particle also restore `camera3d`. Further cartridge content is optional expansion, not a missing console integration. Preserve the fictional, engine-only scope and resource provenance rules. | 2026-11-06 | [Cartridges](console_240p.md#cartridges), [gates](console_240p.md#gates), `scripts/console_ray1_check.py`, `scripts/console_particle_check.py` |
| GX-I012 | Physical CRT and controller validation | deferred | Scripted input and window captures are not hardware evidence. Revisit with suitable hardware and explicit approval of monitor-safe, non-120 Hz modes. Do not change display modes automatically. | 2026-11-05 | [CRT notes](console_240p.md#crt-notes), [coverage limits](console_240p.md#gates) |
| GX-I013 | Film-grain presentation variant | proposed | Optional experiment only; not an accepted PRISM-1 requirement or part of the compact-UI slice. Needs a separate decision, opt-in switch and deterministic gate. | 2026-11-05 | [Follow-up](console_240p.md#follow-up) |
| GX-I014 | ROM emulation / real-console branding / BIOS | declined | Outside gX-240's standing scope: it is an original fictional presentation profile, not a compatibility emulator. Do not reintroduce these as console backlog items. | 2026-11-05 | [Non-goals](console_240p.md#non-goals) |
| GX-I015 | Cross-worktree / branch inventory for future cleanup | done | Shared local inventory and retained purpose/idea/disposition notes are implemented. Initial inventory populated; ten disposable-repository/parser tests passed. No deletion/pruning or cleanup approval inferred. Local branch work, not merged/published; machine paths stay outside the public tree. | 2026-11-05 | [Worktree tracking](worktree_tracking.md), `scripts/worktree_inventory.py`, `scripts/test_worktree_inventory.py` |
| GX-I016 | Durable idea / decision / outcome memory | done | This versioned ledger retains done/not-done distinctions, reasons and revisit conditions. Agent workflow now checks it and existing backlogs before suggesting work; it does not replace feature contracts or grant implementation approval. Local branch work, not merged/published. | 2026-11-05 | This document, `AGENTS.md`, [documentation index](README.md) |
| GX-I017 | Release payload capacity recovery / lean headroom | partial | Earlier shader-compiler/serializer `-Os` recovery is already implemented; do not re-propose it as new work. Recovered 17,728 B in local commit `0e803ab` by placing the GUI editor, theme editor and startup CLI parser in the existing Release `-Os` tier; the later GX-I010 playback slice added 3,552 B, GX-I008 2x SSAA added 2,368 B, GX-I009 PRISM-1 added 2,016 B, the particle composite follow-up added 224 B, the RAY-1 profile added 2,560 B and the particle cartridge profile added 1,024 B. Current production: 4,186,971 B payload, 7,333 B headroom; BSS 2,701,344 B, 3,656 B headroom. Caps/baseline, capabilities and visuals unchanged. Settings/templates/editor-host savings were measured but not selected because of live/always-on paths. Remaining: further reclamation only if a larger reviewed reserve is needed. | 2026-11-06 | [Lean reassessment](lean_reassessment.md#gx-i017-capacity-recovery-2026-11-06), `make payload-audit`, `make lean` |

## Other existing idea lists

Search the relevant list before adding or recommending work here. Add an index
row when an idea is discussed or its outcome changes; do not bulk-mark older
suggestions accepted or complete.

- [Rendering backlog](rendering_backlog.md)
- [GUI visual backlog](gui_visual_backlog.md)
- [Audio queued ideas](audio.md#queued-ideas-author-proposed-unscheduled)
- [Physics queued ideas](physics.md#queued-ideas-author-proposed-unscheduled)
- [Screen color-code backlog](color_code_backlog.md)
- [Lean reassessment](lean_reassessment.md) — distinguish proposals from adopted policy.
- [Engine/game split proposal](engine_game_split.md)

## New-entry template

```text
ID: GX-I### (next unused number)
Idea / search terms:
Status: proposed
Last reviewed: YYYY-MM-DD
Outcome / reason:
Remaining work / revisit condition:
Source / contract / related ID:
Implementation / verification / branch or merge state (when applicable):
```

Keep the summary in the index. Put lengthy design and evidence in the linked
subsystem document instead of maintaining a second competing backlog here.
