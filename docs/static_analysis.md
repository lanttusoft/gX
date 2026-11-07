# Static analysis and type gates

gX layers three checks so a type mistake is caught by the cheapest tool that
can see it:

| Layer | Tool | Gate | Dependency |
| --- | --- | --- | --- |
| Compiler capability + alias contract | CMake probes, GCC/Clang warnings, `_Static_assert` | `make compiler-check` (inside `make check`) | configured build |
| AST type audit | `scripts/type_audit.py` (libclang) | `make type-audit-check` (tooling tier) | `python3-clang` |
| Lint gate | `scripts/lint_check.py` (clang-tidy diff) | `make lint-check` (tooling tier) | `clang-tidy` + `clang-tidy-diff.py` |

## Compiler capability probes

`cmake/CompilerFeatures.cmake` checks the compiler instead of assuming it:

- warning flags are probed and only then enabled:
  `-Werror=bool-compare`, `-Werror=bool-operation`,
  `-Werror=pointer-to-int-cast`, `-Werror=int-to-pointer-cast`, and
  Clang's `-Werror=tautological-constant-out-of-range-compare`;
- quality warnings are probed and enabled on gX targets only (vendored code
  keeps its own warning budget): `-Wimplicit-fallthrough`, `-Wformat=2`,
  `-Wundef`, `-Wvla`, `-Wpointer-arith`, `-Wwrite-strings`,
  `-Wold-style-definition`, plus GCC's `-Wduplicated-cond` and
  `-Wlogical-op`, with matching `-Werror=` forms; null-deref coverage stays
  in the clang-analyzer lint gate (GCC's optimizer oracle is too noisy here);
- attribute/builtin support is probed (`format`, `visibility`, `noinline`,
  `aligned`, `popcount`, `return_address`), as are `_Noreturn`, `_Alignas`
  and `__has_include`;
- sanitizer (`address`, `undefined`), coverage, LTO, stack-protector and
  `-fno-common` support are recorded; `-fno-common` is enabled by default;
- C11 features are probed (`_Static_assert`, `_Thread_local`, `_Generic`,
  `<stdatomic.h>`, `__atomic`, `__builtin_expect`);
- the results are written to `build/generated/compiler_features.txt`;
- `src/common/types/primary.h` asserts the alias sizes and that `gX_I1` is
  `_Bool`, so an ABI or typedef change fails to compile.

`make compiler-check` fails when the compiler is unsupported, a required
capability or quality warning is missing, the gX target flags do not carry the
required errors, or the alias probe fails. It also compiles a small
`primary.h` probe with `CC` so the check is independent of the configured
build. Optional capabilities (sanitizers, LTO, coverage, stack protector) are
printed but not required; they are available for opt-in build configurations.

## AST type audit

`scripts/type_audit.py` uses the compile database
(`build/lint/compile_commands.json`) and libclang's typed AST. It catches what
the compiler does not:

- `gX_I1`/`bool` initialized or assigned a sentinel (`gX_I1 flag = -1`);
- `gX_I1` counters, compound assignments and out-of-range comparisons;
- `gX_I1` functions returning `-1`/other non-boolean values;
- `gX_I1` casts of negative literals;
- `stdbool.h` outside `src/common/types*`;
- raw host-type declarations growing in engine core.

Raw types are ratcheted by `scripts/fixtures/type_audit_baseline.json`
(generated from the release tree). Existing files are grandfathered; a new
file or a higher count fails. Regenerate deliberately with:

```bash
python3 scripts/type_audit.py --update-baseline
```

The default scope is the source changes since the latest tag, so a release
tree is clean by construction. `--all` scans every database entry (slow,
manual use).

## Lint gate

`scripts/lint_check.py` runs `clang-tidy` over the diff against the latest
tag. The check set is deliberately narrow: the clang static analyzer
(`core`, `deadcode`, `unix`) plus high-signal bugprone checks
(`signed-char-misuse`, `misplaced-widening-cast`, `sizeof-expression`,
`suspicious-memset-usage`, `incorrect-roundings`, `too-small-loop-variable`,
`narrowing-conversions`) and a few `misc`/`portability` checks. Only changed
lines are graded; the compile database is configured automatically in
`build/lint` when missing.

Manual use:

```bash
make lint-check                 # gate
python3 scripts/lint_check.py --all --advisory
```

## Runtime instrumentation (opt-in)

The compiler probes also gate two separate build configurations:

```bash
make sanitize-check   # build/sanitize: ASan + UBSan, then --test-only
make tsan-check       # build/tsan: ThreadSanitizer, then --test-only
make coverage-check   # build/coverage: gcov, then --test-only + line summary
```

`GX_SANITIZE=ON` requires the probed ASan/UBSan support and adds
`-fsanitize=address,undefined -fno-sanitize-recover=all
-fno-omit-frame-pointer` to gX targets. `GX_TSAN=ON` adds
`-fsanitize=thread` and halts on the first race. `GX_COVERAGE=ON` adds
`--coverage`.
Both keep the default `build/` configuration untouched. The sanitizer run
disables leak detection (`detect_leaks=0`) because the test harness keeps
process-lifetime allocations; UBSan halts on the first error. Coverage writes
`build/coverage/coverage.json` and prints the lowest-covered sources.

<!-- //ai: M11-01 native capacity regression also runs without a display. -->
`sanitize-check` additionally runs `scripts/test_raylib_mesh_upload.py` with
ASan/UBSan and leak detection enabled. It compiles the complete mesh backend,
load wrapper and conversion units, and uses the bundled raylib destructor's
actual capacity/loop with instrumented allocation and GL boundaries. Rigid,
skinned, all-attribute, repeated-lifetime and already-uploaded controls must
match raylib's nine-slot destruction contract, even without wrapper skinning.
The ordinary guarded-allocation variant also runs in `check` and `check-fast`.
This is a CPU ownership/bounds test, not a replacement for sanitized water
and physics scene runs with a real GL context.

<!-- //ai: GX-I024 GUI shader relocation regression runs headless. -->
The sanitizer suite also includes a headless resource-lifetime regression: a
GUI shader element keeps a stable fragment path, a forced shader-heap growth
frees the old slice, and resolve/clear must use the live slot. No GL context
is needed; the sanitized 24-frame water run covers the real relocation path.

## Presets, clangd and fuzzing

`CMakePresets.json` standardizes the build trees: `default` (build/), `debug`,
`release`, `lint` (compile database for clang-tidy/clangd), `sanitize`, `tsan`,
`coverage`, `kernel` and `gl43`. Every preset exports
`compile_commands.json`; `.clangd` points at `build/lint`:

```bash
cmake --preset lint
cmake --build --preset lint
```

The JSON parser has a libFuzzer + ASan harness:

```bash
make fuzz-json                 # 60 s default
python3 scripts/fuzz_json.py --seconds 120 --runs 0
```

It compiles `fuzz/fuzz_json.c` with `-fsanitize=fuzzer,address`, seeds from
`resources/tools/*.json`, and writes crash artifacts under
`build/fuzz-artifacts/`. It is a manual/tooling target, not a gate.

## External tool checks

The installed host tools are wired as manual/tooling targets:

```bash
make cppcheck-check   # cppcheck project audit; errors block, warnings advisory
make size-report      # bloaty section attribution of build/release/gX_build
make perf-stat        # perf software counters for --test-only
make coverage-check   # gcov report + gcovr HTML when installed
```

Current cppcheck baseline: **0 errors, 0 warnings**. The remaining
`dangerousTypeCast` class is filtered for `src/tools/**` C++ files because
Dear ImGui's API takes `float*`/`bool*` and the old-style casts are
intentional; clang-tidy still owns C++ cast style there. Allocation guards in
billboard/water/wireframe/raylib wrappers, printf format fixes and the
remaining null/array guards are real source fixes. `bloaty` on the
Release payload reports 3.26 MiB `.text`, 2.57 MiB `.bss`, 547 KiB `.rodata`.
`perf record` is blocked by the host's `perf_event_paranoid`; `perf stat`
works. `semgrep` is installed but currently fails to start (missing
`opentelemetry.exporter` Python dependency), so it is not wired in.

## Why this split

A `gX_I1` assignment from `-1` compiles silently (`bool` becomes `true`);
the AST audit catches it. A bool comparison or pointer-to-int cast is a
compiler warning, so the build makes it an error instead of duplicating the
pattern in an audit. The November 2026 render-target diet regression
(`gX_I1 trace = -1`, always-on logging) is the worked example for the first
rule.
