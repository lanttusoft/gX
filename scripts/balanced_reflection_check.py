#!/usr/bin/env python3
#ai: opt-in environment mixing has an owned rendered control and legacy default equivalence.
import argparse
import json
from pathlib import Path
import numpy as np
import audit_common as audit
from glb_material_maps_check import checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT / 'build/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/balanced_refl'))
    args = parser.parse_args()
    out = args.out.resolve()
    captures = {}
    for name, flags in [('default', {}), ('legacy', {'balanced_reflection': False}),
                        ('balanced', {'balanced_reflection': True}),
                        ('disabled', {'balanced_reflection': True, 'reflections': False})]:
        options = dict(surface_case='factor', reflections=True)
        options.update(flags)
        captures[name] = checks(args.binary.resolve(), out / name, **options)
    assert np.array_equal(captures['default'], captures['legacy']), 'legacy default changed'
    roi = (slice(280, 320), slice(200, 600))
    legacy = captures['legacy'][roi]
    balanced = captures['balanced'][roi]
    delta = float(np.abs(legacy - balanced).mean())
    assert delta > 0.0001, ('balanced reflection did not render', delta)
    base = checks(args.binary.resolve(), out / 'plain', surface_case='factor')
    assert np.array_equal(base, captures['disabled']), 'opt-in changed non-reflective surfaces'
    report = {'default_equals_legacy': True, 'nonreflective_unchanged': True,
              'roi_mean_abs_delta': delta,
              'legacy_center_rgb': captures['legacy'][300, 400].tolist(),
              'balanced_center_rgb': captures['balanced'][300, 400].tolist()}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('BALANCED_REFLECTION_CHECK: PASS', report)


if __name__ == '__main__':
    main()
