<!-- //ai: contribution checklist aligned with the public gates and release classes. -->
## Summary

<!-- What changed and why? Keep it focused. -->

## Related issue

<!-- Closes #... or link the discussion. Write "none" for a small self-contained fix. -->

## Scope

- [ ] Engine core
- [ ] Renderer / shaders
- [ ] GFX editor / authoring tools
- [ ] Asset pipeline / importers
- [ ] Build, tests, or audits
- [ ] Documentation only

## Verification

- [ ] `./build.sh`
- [ ] `./build/gX_build --test-only`
- [ ] `make check-fast`
- [ ] Focused renderer/scene/feature gate: <!-- name it -->
- [ ] `python3 scripts/api_coverage.py --strict` and `python3 scripts/enum_coverage.py --strict` if the public surface changed
- [ ] `make provenance-check` if a resource was added or changed

## Compatibility

- [ ] No scene/preset format change.
- [ ] Scene/preset change is backward-compatible (old files still load).
- [ ] Public API or enum change is documented in `docs/api_reference.md`.
- [ ] `CHANGELOG.md` has an entry and the correct `Release-Class` for user-visible changes.

## Contribution checks

- [ ] The change is GPLv3-compatible and has no incompatible third-party code.
- [ ] New assets have a provenance entry and a compatible license.
- [ ] No gameX assets, story content, private scenes, screenshots, or proprietary material.
- [ ] No credentials, private paths, or personal data.
- [ ] AI-assisted code is marked with `//ai:` (or the historical `//opencode:`), and the change follows `docs/ai_policy.md`.
- [ ] The change keeps the beta's engine-only scope (`docs/tooling_scope.md`).

## Evidence

<!-- Gate output, logs, before/after captures, or a short reproduction note. Remove private content. -->
