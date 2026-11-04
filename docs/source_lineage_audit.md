# Source-lineage and citation audit

gX is distilled from the author's private `gameX` archive, so "self-plagiarism"
is expected: reused author-owned code is legitimate, but a port should say so.
Third-party code additionally needs its license and notice entry.

`scripts/source_lineage_audit.py` is a review aid for both cases. It strips
comments, tokenises the source, builds 10-token shingles and reports files whose
shingle containment against a corpus is at least a threshold (default 25%). A
match is **credited** when the gX file carries:

- a `[gX: /path]` porting banner;
- a known lineage/source name (`raylib`, `raymath`, `three.js`, `gameX`,
  `game3`, `EMMIR`, `EMMIR`, `ImGui`, ...); or
- an explicit phrase (`ported from`, `adapted from`, `borrowed code`,
  `derived from`, ...).

The tool is deliberately heuristic. It is not a legal originality proof, a
license-compatibility check or a block-level plagiarism detector; it finds
whole-file reuse above the threshold and leaves partial/patch-level review to
humans.

## Third-party corpus (public, gateable)

```bash
make lineage-audit
# = python3 scripts/source_lineage_audit.py \
#     --compare external/raylib/src --strict
```

This checks gX against the vendored raylib source. Any high-similarity file
must carry a raylib/raymath credit or a `[gX:` banner. The existing
`attribution-check` still verifies the notice and license files.

## Private archive (report-only)

```bash
python3 scripts/source_lineage_audit.py \
  --archive /path/to/gameX/master \
  --compare external/raylib/src \
  --strict --json /path/outside/the/repo/lineage.json
```

Keep the archive path and JSON report outside the public tree. The self-audit
does not gate CI because the archive is private; run it when porting a large
module or before a release.

## Baseline (2026-11-04)

- Private archive: 1,265 comparable files, 387 matches at >= 0.25, **0
  uncited**.
- raylib corpus: 116 files, 4 matches, **0 uncited** (raymath/raylib or
  game3 lineage).
- ImGui/AngelScript corpora: no matches outside their vendored trees.

Matched paths and the private corpus name stay in the private report; the
public documentation only states the counts.
