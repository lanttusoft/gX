# AngelScript (vendored subset)

Vendored source of the AngelCode Scripting Library, used by gX's opt-in
scripting shell (`GX_USE_ANGELSCRIPT`, default OFF).

- Version: 2.38.0 (`include/angelscript.h` — `ANGELSCRIPT_VERSION_STRING`)
- Upstream: https://www.angelcode.com/angelscript/
- License: zlib/libpng — `license/angelscript/LICENSE` (`license/NOTICE.md`)
- Copyright (c) 2003-2023 Andreas Jönsson

## Contents

- `include/angelscript.h` — public API header.
- `source/` — the library implementation (all platforms; per-architecture
  files are preprocessor-guarded by `as_config.h`).
- `add_on/scriptstdstring/` — `string` type for scripts.
- `add_on/scriptarray/` — array type for scripts.

## Provenance

Vendored 2026-10-12 from the anglecode.com 2.38.0 SDK distribution
(gameX's workspace copy, `external/angelscript_2380/sdk`), source only — no
prebuilt library is committed. The gX build compiles this subset in-tree:

```
cmake -B build/scripting -DGX_USE_ANGELSCRIPT=ON
```

<!-- //ai: vendored third-party source subset, provenance tracked in license/NOTICE.md -->
