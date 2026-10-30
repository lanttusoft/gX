# Asset provenance intake reports

`scripts/asset_clearance.py` creates a hash-bound JSON report for one or more
assets that have already been exported into this gX checkout's `resources/`
tree. It records the supplied creator, origin, source, license or intended use,
proposed publish action, optional attribution/evidence, file size, and SHA-256.
It prints to stdout and does not change the asset, `license/provenance.json`, or
`license/ATTRIBUTIONS.md`.

Example for an original Blender model:

```bash
python3 scripts/asset_clearance.py "$ASSET" \
  --origin author \
  --creator "Your chosen name" \
  --source "Created from scratch in Blender; no imported third-party assets" \
  --license "project (GPLv3)" \
  --publish keep \
  --evidence "Original Blender project retained by creator" \
  > /tmp/opencode/asset-clearance.json
```

Set `ASSET` to the path of the exported file under `resources/` before running
the example.

Review the report and the source project, then deliberately update
`license/provenance.json` and run `make provenance-check`. The report includes a
suggested exact entry, but existing family globs may overlap it; resolve that
overlap rather than adding ambiguous duplicate coverage. `make
asset-clearance-check` runs the helper's safety and report-format tests.

This is an intake record, **not independent verification, legal advice, or a
license grant**. A declaration that an asset is self-made does not prove that
all embedded material is original. The tool rejects paths outside the current
gX `resources/` tree, quarantined paths, and known denied content; it does not
copy files from another checkout. For work currently stored in gameX, export
the original Blender project directly into gX only after confirming that no
gameX-only experiments or third-party material are included. Never use this
report to bypass the repo's gameX asset-isolation rule.
