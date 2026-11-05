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
| GX-I008 | Supersampled render/output split / SSAA to 240p | proposed | Not implemented by rendering at 320x240 then upscaling. Specify the higher-resolution render, 320x240 resolve, quality and performance/size budgets before implementation. | 2026-11-05 | [Follow-up](console_240p.md#follow-up) |
| GX-I009 | PRISM-1 dither / quantization / composite / CRT profile | partial | Generic deterministic dither, EGA16/RGB332 palettes and interlace hooks/gates exist. Console integration, 16-bit output semantics and composite/phosphor-mask treatment do not. Reuse the hooks rather than proposing them from scratch. | 2026-11-05 | [Rendering experiments](rendering_backlog.md#p1--dithering), [console follow-up](console_240p.md#follow-up), `dither-check`, `palette-check`, `interlace-check` |
| GX-I010 | Richer console OSD / save status / frame advance / fast-forward | partial | Boot banner and live native video/cartridge/slot status with save/load/failure feedback are implemented in this local draft commit. OSD, console, fast/full and 12/12 shared-GUI gates passed; not merged/published. Frame advance/fast-forward remain unimplemented and need a separate timing contract/approval. | 2026-11-05 | [Presets](console_240p.md#presets), [follow-up](console_240p.md#follow-up), `scripts/console_osd_check.py` |
| GX-I011 | More authored console cartridge profiles | partial | Y2K fixed profile exists; RAY-1 and particle routes launch existing scenes, but more complete authored profiles remain open. Preserve the fictional, engine-only scope and resource provenance rules. | 2026-11-05 | [Cartridges](console_240p.md#cartridges), [follow-up](console_240p.md#follow-up) |
| GX-I012 | Physical CRT and controller validation | deferred | Scripted input and window captures are not hardware evidence. Revisit with suitable hardware and explicit approval of monitor-safe, non-120 Hz modes. Do not change display modes automatically. | 2026-11-05 | [CRT notes](console_240p.md#crt-notes), [coverage limits](console_240p.md#gates) |
| GX-I013 | Film-grain presentation variant | proposed | Optional experiment only; not an accepted PRISM-1 requirement or part of the compact-UI slice. Needs a separate decision, opt-in switch and deterministic gate. | 2026-11-05 | [Follow-up](console_240p.md#follow-up) |
| GX-I014 | ROM emulation / real-console branding / BIOS | declined | Outside gX-240's standing scope: it is an original fictional presentation profile, not a compatibility emulator. Do not reintroduce these as console backlog items. | 2026-11-05 | [Non-goals](console_240p.md#non-goals) |
| GX-I015 | Cross-worktree / branch inventory for future cleanup | done | Shared local inventory and retained purpose/idea/disposition notes are implemented. Initial inventory populated; ten disposable-repository/parser tests passed. No deletion/pruning or cleanup approval inferred. Local branch work, not merged/published; machine paths stay outside the public tree. | 2026-11-05 | [Worktree tracking](worktree_tracking.md), `scripts/worktree_inventory.py`, `scripts/test_worktree_inventory.py` |
| GX-I016 | Durable idea / decision / outcome memory | done | This versioned ledger retains done/not-done distinctions, reasons and revisit conditions. Agent workflow now checks it and existing backlogs before suggesting work; it does not replace feature contracts or grant implementation approval. Local branch work, not merged/published. | 2026-11-05 | This document, `AGENTS.md`, [documentation index](README.md) |

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
