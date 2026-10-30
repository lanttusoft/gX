# GFX visual review

`gfx_visual_review.py` is an advisory companion to the automated feature
matrix and reflection suite. It does not change a gate result and leaves a
new capture as `UNREVIEWED` until a person grades it.

## Workflow

```bash
make gfx-check
python3 scripts/gfx_visual_review.py
```

The script reads the fixed captures in `/tmp/opencode/fmatrix` and
`/tmp/reflect_suite`, then writes:

- `matrix_contact_sheet.png`
- `reflection_contact_sheet.png`
- `visual_review.json`

The manifest records SHA-256 hashes, dimensions, source paths, visual grades,
and reviewer notes. Update a card without changing the automated gates:

```bash
python3 scripts/gfx_visual_review.py \
  --grade 'reflect:ssr_default_30_25=A-' \
  --note 'reflect:ssr_default_30_25=Readable reflection; mild edge noise'
```

## Adjacent-grade rubric

- **A+** — polished composition, materials, lighting, and no visible artifacts.
- **A** — strong authored result with only minor non-blocking roughness.
- **A-** — readable and correct, with noticeable but contained roughness.
- **B+ / B** — feature reads, but presentation or material separation needs work.
- **C or lower** — difficult to read, unstable, or visibly broken.

Review composition, material separation, lighting continuity, edge/horizon
artifacts, and temporal-looking noise. Visual grades are human evidence, not
substitutes for correctness or performance gates.
