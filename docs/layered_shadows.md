# Layered point shadows (4.3 option)

`ENABLE_LAYERED_SHADOWS` is an opt-in renderer state/preset field, exposed in
the editor under **Quality → layered point shadows (4.3)**. Default: off.

The legacy path renders six passes per casting point light (attach a layer,
clear, draw every model). On a 4.3 context the layered path instead:

- attaches the whole point-shadow depth array to one FBO;
- uploads the six light-space matrices as a uniform array;
- draws each static mesh once with six instances, writing
  `gl_Layer = layerBase + gl_InstanceID` in the vertex shader.

The result is **byte-identical** to the legacy path; only draw/state counts
change. A skinned caster falls back to the legacy six-pass path, and the
shipped default context (GL 3.3) always uses the legacy path.

## Requirement

The field only takes effect with a 4.3 context:

```bash
GX_GL43_CONTEXT=1 ./build/gX_build --showcase
```

`gX_GL.hasLayeredShadows` is set only when the selected level is 4.3 or newer
and the driver exposes `ARB_shader_viewport_layer_array`; the 3.3 path is
unchanged and the editor checkbox is disabled with a hint.

## Measured trade

Showcase with casting ring lights, 1024x768, 4.3 context:

| lights | legacy shadow p95 | layered shadow p95 | frame GPU |
| --- | --- | --- | --- |
| 4 | 1.31 ms | 0.844 ms (−35%) | neutral |
| 8 | 2.69 ms | 1.78 ms (−34%) | 25.10 → 24.23 ms (−3.5%) |

The shadow pass is a small part of the frame, so this is a shadow-heavy /
many-light mode rather than a default. The image is exact, so the decision is
purely cost.

## Gate

`make layered-shadows-check` / `python3 scripts/gfx_layered_shadows_check.py`:

- probes a 4.3 context and reports `SKIP` below it;
- captures the showcase with two casting ring lights, env off vs on;
- loads two editor presets (field off vs on) and captures both;
- requires both pairs to be byte-identical and requires the activation log
  only on the enabled arms.

The `GX_LAYERED_SHADOWS=1` environment hook overrides the preset field for
audits (`0` forces the legacy path); it is not a replacement for the state
field.
