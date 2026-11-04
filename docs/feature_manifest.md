# Scene feature manifest

`renderer/gfx/features.c` is the single table for the scene feature set. Each
entry records:

- id and label;
- the existing `ENABLE_*` state predicate;
- dependencies (for example SSGI → SSAO, volume fog → sun shadows);
- whether it needs the HDR scene target;
- whether it belongs to the pre-present effect closure;
- cost class (cheap / medium / expensive);
- passes and render targets it owns.

Run with `GX_GFX_PLAN=1` to print the resolved plan once per process:

```bash
GX_GFX_PLAN=1 ./build/gX_build --showcase
```

```
GFX_PLAN mask=0x0001e47f count=12 needHdr=1 effects=1 missing=0x00000000
GFX_PLAN feature=shadows cost=1 passes=shadow targets=sun.map,csm
GFX_PLAN feature=point-shadows cost=2 passes=shadow targets=pointarray
...
```

`missing` is the dependency mask that is enabled but not satisfied; the
planner does not silently enable dependencies. `GFX_FEATURES` still reports
the closure/loaded target state for the existing gate.

## Current migration state

- Predicates (`HasEffects`, `NeedHdr`, `HasSsgi`) now read the table, so the
  pass table and composite-target lifecycle cannot drift onto separate lists.
- `scripts/gfx_feature_trace_check.py` pins:
  - all-off scene: no effect/HDR closure;
  - DOF-only: HDR + effect closure and `bloom.scene` loaded;
  - volume fog without shadows: missing dependency reported.
- `hdrScene`, `hdrMSAA` and `luminanceRT` are now allocated from the plan:
  an all-off scene stays on LDR, DOF/effects load HDR, and bloom owns the
  luminance target. Scene-level dependencies keep the historical holdouts
  correct without making them LDR: `gfx-scene` (the GFX demo), `battle`,
  `gui-menu` (pause backdrop) and `raymarch` all declare `needsHdr`.
- The GFX editor **Features** panel renders the same table: checkbox per user
  feature, cost/dependency hint, and templates `Unlit`, `Lit`, `Lit + shadows`,
  `HDR + bloom`, `Full`. Context-driven features (battle, GFX scene, pause
  menu, raymarch) are read-only and marked `(scene)`.
- Enabling a feature auto-enables its dependencies; disabling a dependency
  disables its dependents (`SSGI ↔ SSAO`, `volume fog → shadows`). A C unit
  test pins the template masks and dependency closure.
- Remaining: bloom-owned tonemap cleanup and the per-feature target/VRAM delta
  row.
