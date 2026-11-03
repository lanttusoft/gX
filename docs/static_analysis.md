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
- C11 features are probed (`_Static_assert`, `_Thread_local`, `_Generic`,
  `<stdatomic.h>`, `__atomic`, `__builtin_expect`);
- the results are written to `build/generated/compiler_features.txt`;
- `src/common/types/primary.h` asserts the alias sizes and that `gX_I1` is
  `_Bool`, so an ABI or typedef change fails to compile.

`make compiler-check` fails when the compiler is unsupported, a required
capability is missing, or the alias probe fails. It also compiles a small
`primary.h` probe with `CC` so the check is independent of the configured
build.

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

## Why this split

A `gX_I1` assignment from `-1` compiles silently (`bool` becomes `true`);
the AST audit catches it. A bool comparison or pointer-to-int cast is a
compiler warning, so the build makes it an error instead of duplicating the
pattern in an audit. The November 2026 render-target diet regression
(`gX_I1 trace = -1`, always-on logging) is the worked example for the first
rule.
