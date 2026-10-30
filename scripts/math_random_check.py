#!/usr/bin/env python3
#ai: seeded randomized differential math corpus; C exports inputs/outputs as hex floats.
#   The oracle recomputes each probe independently, so fixed-corpus slips that
#   happen to agree with one input are more likely to surface across many.
import argparse
import math
import subprocess
import sys
from pathlib import Path

try:
    from sympy import Matrix
except ImportError:
    print("math-random-check: sympy not found (pip install sympy)", file=sys.stderr)
    sys.exit(1)

import math_pass

ROOT = Path(__file__).resolve().parent.parent
TOL = 1e-4
FAMILY_TOL = {
    "calculus": 2e-3,
    "color": 5e-2,
}
PROBES = 64
MAX_PROBES = 4096
EXPECTED = {
    "vectors": (7, 16),
    "matrix": (35, 52),
    "matrix_small": (22, 8),
    "quaternion": (12, 23),
    "scalar": (10, 5),
    "easing": (7, 5),
    "sdf": (31, 22),
    "em": (19, 10),
    "dipole": (16, 13),
    "convert_coords": (3, 18),
    "convert_sky": (5, 24),
    "convert_new": (8, 16),
    "sdf_ext": (26, 9),
    "convert_units": (6, 13),
    "convert_color": (6, 12),
    "convert_proj": (3, 10),
    "convert_ball": (3, 5),
    "fft": (16, 16),
    "calculus": (8, 10),
    "color": (9, 16),
}


class RandomCheckError(ValueError):
    """Raised when the randomized dump or an oracle result is invalid."""


def parse_float(token):
    if token.lower().startswith(("0x", "+0x", "-0x")):
        return float.fromhex(token)
    try:
        return float(token)
    except ValueError as exc:
        raise RandomCheckError(f"invalid float {token!r}") from exc


def parse_dump(text, probe_count=PROBES):
    probes = {family: [] for family in EXPECTED}
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("MATHRANDOM"):
            continue
        parts = line.split()
        if parts[0] != "MATHRANDOM":
            continue
        if len(parts) < 5:
            raise RandomCheckError(f"malformed MATHRANDOM line: {line!r}")
        family = parts[1]
        if family not in EXPECTED:
            raise RandomCheckError(f"unknown family: {family}")
        try:
            probe = int(parts[2])
            input_count = int(parts[3])
        except ValueError as exc:
            raise RandomCheckError(f"{family}: invalid probe/count") from exc
        expected_inputs, expected_outputs = EXPECTED[family]
        if input_count != expected_inputs:
            raise RandomCheckError(
                f"{family}: expected {expected_inputs} inputs, got {input_count}")
        value_start = 4
        output_count_at = value_start + input_count
        if len(parts) <= output_count_at:
            raise RandomCheckError(f"{family}: truncated input block")
        inputs = [parse_float(token) for token in parts[value_start:output_count_at]]
        try:
            output_count = int(parts[output_count_at])
        except ValueError as exc:
            raise RandomCheckError(f"{family}: invalid output count") from exc
        if output_count != expected_outputs:
            raise RandomCheckError(
                f"{family}: expected {expected_outputs} outputs, got {output_count}")
        output_start = output_count_at + 1
        if len(parts) != output_start + output_count:
            raise RandomCheckError(f"{family}: output count does not match values")
        outputs = [parse_float(token) for token in parts[output_start:]]
        if probe != len(probes[family]):
            raise RandomCheckError(
                f"{family}: probe {probe}, expected {len(probes[family])}")
        probes[family].append((inputs, outputs))
    for family, family_probes in probes.items():
        if len(family_probes) != probe_count:
            raise RandomCheckError(
                f"{family}: expected {probe_count} probes, got {len(family_probes)}")
    return probes


def run_dump(binary, probes=PROBES, runner=None):
    runner = runner or subprocess.run
    env = {
        "GX_MATH_DUMP": "1",
        "GX_MATH_RANDOM_DUMP": "1",
        "GX_MATH_RANDOM_PROBES": str(probes),
        "PATH": "/usr/bin:/bin",
    }
    try:
        run = runner([str(binary)], env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as exc:
        raise RandomCheckError("random dump timed out") from exc
    if run.returncode != 0:
        raise RandomCheckError(f"random dump exited with status {run.returncode}")
    return parse_dump(run.stdout or "", probes)


def _linear2srgb(value):
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055


def _srgb2linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _amplitude2db(value):
    return 20.0 * math.log10(value)


def _db2amplitude(value):
    return 10.0 ** (value / 20.0)


def _midi2frequency(value):
    return 440.0 * 2.0 ** ((value - 69.0) / 12.0)


def _frequency2midi(value):
    return 69.0 + 12.0 * math.log2(value / 440.0)


def _celsius2fahrenheit(value):
    return value * 9.0 / 5.0 + 32.0


def _fahrenheit2celsius(value):
    return (value - 32.0) * 5.0 / 9.0


def _rgb2hsv(rgb):
    r, g, b = rgb
    high, low = max(rgb), min(rgb)
    delta = high - low
    hue = 0.0
    if delta > 0.0:
        if high == r:
            hue = 60.0 * math.fmod((g - b) / delta, 6.0)
        elif high == g:
            hue = 60.0 * ((b - r) / delta + 2.0)
        else:
            hue = 60.0 * ((r - g) / delta + 4.0)
        if hue < 0.0:
            hue += 360.0
    saturation = delta / high if high > 0.0 else 0.0
    return [hue, saturation, high]


def _hsv2rgb(hsv):
    hue, saturation, value = hsv
    h = hue / 60.0
    sector = int(h) % 6
    if sector < 0:
        sector += 6
    fraction = h - sector
    p = value * (1.0 - saturation)
    q = value * (1.0 - saturation * fraction)
    t = value * (1.0 - saturation * (1.0 - fraction))
    return list([
        (value, t, p), (q, value, p), (p, value, t),
        (p, q, value), (t, p, value), (value, p, q),
    ][sector])


def _rotate_by_quaternion(vector, q):
    x, y, z, w = q
    vx, vy, vz = vector
    return [
        vx * (x * x + w * w - y * y - z * z)
        + vy * (2 * x * y - 2 * w * z)
        + vz * (2 * x * z + 2 * w * y),
        vx * (2 * w * z + 2 * x * y)
        + vy * (w * w - x * x + y * y - z * z)
        + vz * (-2 * w * x + 2 * y * z),
        vx * (-2 * w * y + 2 * x * z)
        + vy * (2 * w * x + 2 * y * z)
        + vz * (w * w - x * x - y * y + z * z),
    ]


def expected_probe(family, inputs):
    if family == "vectors":
        a = math_pass.v3(inputs[0:3], math_pass.f64)
        b = math_pass.v3(inputs[3:6], math_pass.f64)
        t = math_pass.f64(inputs[6])
        cross = a.cross(b)
        dot = a.dot(b)
        proj = b * (dot / b.dot(b))
        lerp = a + (b - a) * t
        norm_a = a / a.norm()
        norm_b = b / b.norm()
        return ([cross[0], cross[1], cross[2], dot,
                 proj[0], proj[1], proj[2],
                 lerp[0], lerp[1], lerp[2],
                 norm_a[0], norm_a[1], norm_a[2],
                 norm_b[0], norm_b[1], norm_b[2]])
    if family == "matrix":
        a = math_pass.mat(inputs[0:16], math_pass.f64)
        b = math_pass.mat(inputs[16:32], math_pass.f64)
        point = Matrix(4, 1, [math_pass.f64(value) for value in inputs[32:35]] + [1])
        transformed = a.T * point
        return (list(a * b) + list(a.T) + [a.det()] + list(a.inv())
                + [transformed[0], transformed[1], transformed[2]])
    if family == "matrix_small":
        base = math_pass.case_matrix_small(inputs[0:13], math_pass.f64)
        first = [float(value) for value in inputs[13:17]]
        second = [float(value) for value in inputs[17:21]]
        amount = float(inputs[21])
        return base + [first[index] + amount * (second[index] - first[index])
                       for index in range(4)]
    if family == "quaternion":
        q1 = [float(value) for value in inputs[0:4]]
        q2 = [float(value) for value in inputs[4:8]]
        amount = float(inputs[8])
        vector = [float(value) for value in inputs[9:12]]
        normalized = math_pass._q_normalize(q1)
        return (math_pass._q_multiply(q1, q2) + normalized
                + math_pass._q_normalize(math_pass._q_lerp(q1, q2, amount))
                + math_pass._q_slerp(q1, q2, amount)
                + math_pass._q_invert(q1)
                + _rotate_by_quaternion(vector, normalized))
    if family == "scalar":
        return math_pass.case_scalar(inputs, math_pass.f64)
    if family == "easing":
        return math_pass.case_easing(inputs, math_pass.f64)
    if family == "sdf":
        q_inputs = inputs[28:31] + inputs[3:28]
        return (math_pass.case_sdf(inputs, math_pass.f64)
                + math_pass.case_sdf(q_inputs, math_pass.f64))
    if family == "em":
        return math_pass.case_em(inputs, math_pass.f64)
    if family == "dipole":
        return math_pass.case_dipole(inputs, math_pass.f64)
    if family == "convert_coords":
        return math_pass.case_convert_coords(inputs, math_pass.f64)
    if family == "convert_sky":
        return math_pass.case_convert_sky(inputs, math_pass.f64)
    if family == "convert_new":
        base = math_pass.case_convert_new(inputs, math_pass.f64)
        linear, srgb, amp, db, note, freq, celsius, fahrenheit = (
            float(value) for value in inputs[0:8])
        return base + [
            _srgb2linear(_linear2srgb(linear)),
            _linear2srgb(_srgb2linear(srgb)),
            _db2amplitude(_amplitude2db(amp)),
            _amplitude2db(_db2amplitude(db)),
            _frequency2midi(_midi2frequency(note)),
            _midi2frequency(_frequency2midi(freq)),
            _fahrenheit2celsius(_celsius2fahrenheit(celsius)),
            _celsius2fahrenheit(_fahrenheit2celsius(fahrenheit)),
        ]
    if family == "sdf_ext":
        base = math_pass.case_sdf_ext(inputs[0:23], math_pass.f64)
        other = math_pass._sdf_box_frame(inputs[23:26], inputs[3:6], inputs[6])
        return base + [other]
    if family == "convert_units":
        base = math_pass.case_convert_units(inputs, math_pass.f64)
        meters, feet, inches, kelvin, celsius = (float(value) for value in inputs[0:5])
        return base + [
            feet,
            meters,
            inches,
            meters,
            kelvin,
            celsius,
        ]
    if family == "convert_color":
        rgb = [float(value) for value in inputs[0:3]]
        hsv = [float(value) for value in inputs[3:6]]
        return (math_pass.case_convert_color(inputs, math_pass.f64)
                + _hsv2rgb(_rgb2hsv(rgb))
                + _rgb2hsv(_hsv2rgb(hsv)))
    if family == "convert_proj":
        return math_pass.case_convert_proj(inputs, math_pass.f64)
    if family == "convert_ball":
        return math_pass.case_convert_ball(inputs, math_pass.f64)
    if family == "fft":
        return math_pass.case_fft(inputs, math_pass.f64)
    if family == "calculus":
        return math_pass.case_calculus(inputs, math_pass.f64)
    if family == "color":
        return math_pass.case_color(inputs, math_pass.f64)
    raise RandomCheckError(f"unknown family: {family}")


def check_probe(family, probe, inputs, outputs):
    expected = expected_probe(family, inputs)
    if len(expected) != len(outputs):
        raise RandomCheckError(
            f"{family}[{probe}]: oracle returned {len(expected)}, got {len(outputs)}")
    tolerance = FAMILY_TOL.get(family, TOL)
    worst = 0.0
    for index, (want, got) in enumerate(zip(expected, outputs)):
        want_float = math_pass.expected_float(want)
        if not math.isfinite(want_float) or not math.isfinite(got):
            raise RandomCheckError(f"{family}[{probe}]: non-finite value at entry {index}")
        error = abs(want_float - got) / max(1.0, abs(want_float))
        if not math.isfinite(error):
            raise RandomCheckError(f"{family}[{probe}]: non-finite error at entry {index}")
        if error > tolerance:
            raise RandomCheckError(
                f"{family}[{probe}]: entry {index} error {error:.3e} > {tolerance:.0e}")
        worst = max(worst, error)
    return worst


def main():
    parser = argparse.ArgumentParser(description="gX randomized math differential check")
    parser.add_argument("--binary", default=str(ROOT / "build" / "gX_build"))
    parser.add_argument("--probes", type=int, default=PROBES,
                        help=f"probes per family (default {PROBES}, max {MAX_PROBES})")
    args = parser.parse_args()
    if args.probes < 1 or args.probes > MAX_PROBES:
        print(f"math-random-check: probes must be 1..{MAX_PROBES}", file=sys.stderr)
        return 1

    binary = Path(args.binary)
    if not binary.exists():
        print(f"math-random-check: binary not found: {binary}", file=sys.stderr)
        return 1
    try:
        probes = run_dump(binary, args.probes)
    except RandomCheckError as exc:
        print(f"math-random-check: {exc}", file=sys.stderr)
        return 1

    failures = 0
    total = 0
    for family in EXPECTED:
        family_worst = 0.0
        family_failures = 0
        for probe, (inputs, outputs) in enumerate(probes[family]):
            total += 1
            try:
                family_worst = max(family_worst, check_probe(family, probe, inputs, outputs))
            except RandomCheckError as exc:
                print(f"  {exc}")
                family_failures += 1
                failures += 1
        tolerance = FAMILY_TOL.get(family, TOL)
        print(f"  {family:<14} {len(probes[family]):>3} probes  "
              f"max-rel-err {family_worst:.2e}  tol {tolerance:.0e}  "
              f"{'ok' if family_failures == 0 else 'FAIL'}")
    if failures:
        print(f"MATH_RANDOM: FAIL ({failures} probe(s))")
        return 1
    print(f"MATH_RANDOM: PASS — {total} seeded differential probes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
