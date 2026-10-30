# gX Visual Direction

Authored look for the opt-in GFX style studies. Defaults stay GL33
production; this file only pins the art target so captures stay comparable.

## Positive prompt

Late-1990s/early-2000s Bryce 3D, Y2K futurism, Frutiger Aero optimism, old
rave-poster graphics, tactical minimalism, abstract terrain, glass, brushed
metal and neon materials, crisp low-poly forms, dark atmospheric space,
cyan/lime/magenta accents, luminous grids, controlled bloom, planar
reflections, hard-edged silhouettes, clean OpenGL-era realtime rendering,
utilitarian geometry, dreamlike technical landscapes, restrained detail,
deliberate composition, high readability.

## Negative prompt

No hyperrealism, no AAA cinematic polish, no Unreal Engine 5 look, no
Nanite/Lumen aesthetic, no temporal microstutter, no TAA ghosting, no
shimmering edges, no faceted SSR, no broken reflections, no flickering
shadows, no bloom washout, no excessive lens flare, no chromatic aberration,
no plastic materials, no noisy microdetail, no generic asset-store aesthetic,
no UI clutter, no logos, no watermarks. Keep it stylized, deliberate,
readable, technically clean, and visibly authored.

## Y2K showcase mapping

- Scene: `--showcase-y2k` (`src/renderer/gfx/scene.c` Y2K branch,
  `src/renderer/gfx/showcase.c` post/rim/env tuning). Compact stepped
  terrain, one violet spire, cyan ring, one glass sphere, twilight sky,
  restrained bloom + fog + rim. Existing primitive GLBs only.
- Preset: `resources/tools/gfx_y2k.json` — saved from the polished live
  state via `GX_GFX_PRESET_SAVE`; load with `GX_GFX_PRESET_LOAD` over the
  live scene (see the preset bar in `--gfx-editor`).
- Workflow: experiment live in `--gfx-editor`; only an explicit preset save
  persists anything. Production scenes and reflection fixtures are untouched.
