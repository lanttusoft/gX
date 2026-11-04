# Compressed texture sidecars (BC7/BC5/BC4)

Compressed textures are opt-in sidecars: the source PNG stays the authoring
input, and the runtime prefers a sibling `.dds` file when one exists.

```
resources/texture/<name>.png     authoring input (committed)
resources/texture/<name>.dds     optional BC7 payload (committed or generated)

Committed sidecars: `orb.png` (BC7) and `misc/testcard.png` (BC7). The materials
fixture loads both through the primitive/emissive maps; the normal and lightmap
fixtures are deferred until their format/quality trade is settled.
```

`gX_Texture_Load(".../orb.png")` loads `orb.dds` when present and falls back to
the PNG when the file is absent, the GPU lacks the format, or
`GX_NO_TEXTURE_DDS=1` is set for an audit. `.dds` paths are loaded directly.
The loader accepts DX10 headers and the legacy ATI1/ATI2/DXT fourCC headers for

| Kind | Format | Block | Typical texture |
| --- | --- | --- | --- |
| albedo / UI | BC7 | 16 B / 4×4 | color, alpha |
| normals | BC5 | 16 B / 4×4 | two-channel XY |
| masks / detail | BC4 | 8 B / 4×4 | single channel |

BC7 needs `GL_ARB_texture_compression_bptc` (core 4.2); BC4/BC5 need RGTC
(core 3.0). The default GL 3.3 context on the reference GPU exposes both; a
context without them silently keeps the PNG path.

## Encoding

`scripts/compress_textures.py` wraps `bc7enc` (MIT, build from
`richgel999/bc7enc_rdo`). It is tooling-only and never runs in the engine:

```bash
export GX_BC7ENC=/path/to/bc7enc
python3 scripts/compress_textures.py resources/texture/orb.png --kind albedo
python3 scripts/compress_textures.py resources/texture/texture_normal.png --kind normal
python3 scripts/compress_textures.py resources/texture/misc/scorch.png --kind mask
```

The tool writes the `.dds` next to the source, decodes it back through the
encoder and fails when the PSNR or at-least-2x VRAM budget is missed. Measured
on the shipped fixtures (`--check`):

| Kind | PSNR | File vs RGBA8 |
| --- | --- | --- |
| BC7 orb | 53.61 dB | 0.25× |
| BC7 testcard (640×480) | 55.77 dB | 0.25× |
| BC5 normal | 39.18 dB | 0.25× |
| BC4 scorch mask | 57.12 dB | 0.13× |

`bc7enc` currently emits one mip level; the loader supports multiple levels if
a future encoder writes them. ASTC is not wired into this pipeline.

## Provenance

A committed `.dds` is a derived asset and needs its own
`license/provenance.json` entry (origin derived from the named source PNG).
`make provenance-check` / `provenance_audit.py --strict` enforces it. The
encoder gate generates extra sidecars under the scratch directory; only the
two shipped sidecars are tracked.

## Gate

`make texture-assets-check` / `python3 scripts/texture_assets_check.py`
(quick): loads the materials fixture with and without the sidecars; requires
PSNR >= 55 dB, mean <= 0.05, max channel <= 32, changed pixels <= 2%, both
sidecars in the DDS log, none in the PNG arm, and <=0.5× RGBA8 storage.
Measured: 71.68 dB, mean 0.0024, max 13, 0.49% changed.

`make texture-compression-check` / `python3 scripts/texture_compression_check.py`
(tooling):

- SKIPs when `GX_BC7ENC`/`bc7enc` is absent;
- encodes the three fixtures and checks decoded PSNR + ≤0.5× RGBA8;
- loads each DDS through `GX_TEXTURE_PROBE` and asserts the dimensions/id;
- copies a PNG + sibling DDS into a scratch directory and requires the
  sidecar to win.

Tooling tier: it needs the external encoder, so it is not part of the portable
quick matrix.
