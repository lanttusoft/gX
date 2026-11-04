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
- Still open: allocation derived from the plan (lazy `hdrScene`/`luminanceRT`,
  bloom-owned tonemap), the editor feature checklist/templates, and the three
  lazy-target holdouts (`gui-pause-blur`, `raymarch-features`, `battle`).
  Those remain deliberately gated until their HDR assumptions are audited.
