# Evidence and confidence manifest

<!-- //ai: GX-I036/GX-I039 claim-level evidence and confidence contract. -->

Status: opt-in pilot (2026-11-10). This is a reporting layer over existing
evidence, not a new gate, proof source or release authority.

## Purpose

A claim is useful only when its evidence is named. The manifest records:

- the claim and its scope;
- each evidence item, its verification level, direction, weight, source,
  independence and freshness;
- derived **confidence** and **volatility**;
- an optional **significance** score, kept separate from confidence.

It indexes existing evidence such as gate logs, proof inventory entries,
`gates.toml` families, evidence stamps, visual captures and external sources.
It does not replace them, and a high confidence never promotes a claim past a
failing gate or proof.

## Verification levels

The level caps the weight an item may claim:

| Level | Cap | Meaning |
| --- | ---: | --- |
| `claimed` | 10 | assertion only |
| `implemented` | 25 | code exists but is not yet verified |
| `compiles` | 35 | build/compile evidence |
| `unit` | 55 | unit or contract test |
| `external` | 60 | primary external source, DOI/arXiv/URL |
| `gate` | 70 | focused gate or scene smoke |
| `visual` | 75 | reviewed screenshot or capture |
| `reproducible` | 85 | deterministic/reproducible artifact |
| `formal` | 100 | kernel-checked proof plus statement-fidelity audit |

A weight must not exceed its level cap. Lower weights are allowed.

## Direction and weight

Each item is `for`, `against` or `neutral`.

- Raw weights are summed per direction for transparency.
- Effective weight discounts dependent/correlated sources (`×0.5`) and stale
  inputs (`×0.5`). The report shows both raw and effective weights.
- **Aggregate cap:** effective weights are summed per level and direction, then
  capped at that level's weight cap. Ten `claimed` items cannot exceed the
  `claimed` cap of 10, so they cannot outweigh one `formal` item of 100.
- Counts show breadth; weights show strength.
- Neutral items are counted and their effective weight is reported, but they
  do not enter the formula.

## Derived confidence and volatility

Let `F` and `A` be the effective for/against weights.

```text
confidence = 100 × (F − A) / (F + A)     # signed −100…+100
volatility = 100 × 2 × min(F, A) / (F + A)  # 0…100
```

Special states:

| State | Meaning |
| --- | --- |
| `not-assessed` | confidence `null`, volatility `null`; no assessment made |
| `passive-zero` | assessed, no effective for/against **or neutral** weight; confidence 0, volatility 0 |
| `neutral-only` | assessed, only neutral evidence; confidence 0, volatility 0, neutral weight reported |
| `balanced-low` | `F ≈ A` but total weight below the 50-point active-zero floor |
| `active-zero` | `F ≈ A` and total weight at least 50; confidence 0, high volatility |
| `positive` / `negative` | net weighted support / opposition |

`null` is not passive 0. A claim explicitly assessed as having no evidence is
passive 0; a claim nobody has assessed is null. Neutral evidence is not the
same as no evidence. Active zero requires a minimum total weight, so two tiny
opposing items do not masquerade as substantial contention. The summary table
reports total and neutral weight so confidence can be read with its mass.

## Schematic

```json
{
  "schema": 1,
  "claims": [
    {
      "id": "example",
      "claim": "What is being claimed",
      "assessed": true,
      "significance": 40,
      "evidence": [
        {
          "level": "gate",
          "direction": "for",
          "weight": 60,
          "artifact": "build/evidence/gate.log",
          "independent": true,
          "fresh": true,
          "note": "named gate result"
        }
      ]
    }
  ]
}
```

Derived `confidence` and `volatility` must **not** be stored in the manifest;
the tool computes them. `assessed: false` requires an empty evidence list.

## Usage

```bash
python3 scripts/evidence_manifest.py --check --manifest path.json
python3 scripts/evidence_manifest.py --manifest path.json          # Markdown
python3 scripts/evidence_manifest.py --json --manifest path.json   # JSON
make evidence-manifest-check
```

`make evidence-manifest-check` validates the example manifest and runs the
contract tests.

## Limits

- Confidence is a report for attention and review, not a merge/release
  decision or a substitute for a failing gate.
- Weights are assigned by a person/agent and need review; the tool validates
  range/caps, not honesty.
- Significance (impact/priority) is a separate 0–100 axis. Do not conflate it
  with confidence.
- Evidence links can rot; external and visual items need revalidation.
- The manifest does not prove absence of unknown counter-evidence.
