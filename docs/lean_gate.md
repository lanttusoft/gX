# Lean size gate

The [2026-10-03 reassessment](lean_reassessment.md) records measured recovery
options and proposed profile/reserve policies; it does not change these caps.

`scripts/lean_gate.sh` grades the Release **payload** — the sum of `.text`,
`.rodata`, and `.data` — plus max BSS against `.lean_baseline`, with absolute
caps in `.lean_budget.json`. The report includes the remaining byte headroom
for each cap.

Payload replaced whole-file size as the gated metric because file bytes also
include symbol tables, unwind data, and linker page-alignment padding: a
section crossing a 4 KiB page boundary could add several KiB of file size for
about one KiB of code, which repeatedly looked like feature growth. The whole
file size and its overhead over the payload remain in the report as an
advisory line, alongside the code/data split, media fit, and bss footprint
heuristics.

The Release link also compiles with `-ffunction-sections -fdata-sections` and
links with `--gc-sections` (see `cmake/CompilerFlags.cmake`), so unreferenced
code and data never reach the payload. That is why the stored baseline sits
well below the pre-GC measurements; caps are anchored to the current
baseline.

`scripts/lean_gate.sh --update` refreshes measurements only; it does not move
the caps. This keeps a baseline refresh, such as one needed after a toolchain
change, from silently accepting accumulated feature growth. If a reviewed
change needs more capacity, update the relevant cap and its `rationale` in
`.lean_budget.json`. The 4 KiB jitter allowance is used for grading; the
absolute caps remain the blocking limit. Baselines written before the payload
metric lack an `image_bytes` field; the gate derives it from the stored
`text_bytes` + `rodata_bytes` + `data_bytes` until the next `--update`.

## Release-only exclusions

Release builds define `GX_LEAN_RELEASE`: `src/main_tests.c` compiles to a
stub and the other test translation units (`gxmath_tests.c`,
`renderer/background/tests.c`) become unreachable, so `--gc-sections` drops
them (≈78 KB of payload). Development builds keep the full suite for
`make check`; the Release binary reports the missing suite when `--test-only`
is requested. Keep new development-only code inside this guard.
