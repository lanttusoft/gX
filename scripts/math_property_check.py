#!/usr/bin/env python3
#ai: invariant checks over the seeded math dump; complements the differential oracle.
#   These tests do not reuse the formula oracle, so a shared C/oracle convention
#   mistake is more likely to surface here.
import argparse
import math
import sys
from pathlib import Path

try:
    from sympy import Matrix
except ImportError:
    print("math-property-check: sympy not found (pip install sympy)", file=sys.stderr)
    sys.exit(1)

import math_random_check

ROOT = Path(__file__).resolve().parent.parent
PROP_TOL = 1e-4


class PropertyError(ValueError):
    """Raised when an invariant does not hold."""


def _finite(value):
    return math.isfinite(float(value))


def _assert_close(label, got, want, tolerance=PROP_TOL):
    got = float(got)
    want = float(want)
    if not _finite(got) or not _finite(want):
        raise PropertyError(f"{label}: non-finite value ({got}, {want})")
    error = abs(got - want) / max(1.0, abs(got), abs(want))
    if error > tolerance:
        raise PropertyError(f"{label}: {got} != {want} (rel {error:.3e})")


def _vector_close(got, want, tolerance=PROP_TOL):
    return all(abs(a - b) <= tolerance * max(1.0, abs(a), abs(b))
               for a, b in zip(got, want))


def _assert_vec_close(label, got, want, tolerance=PROP_TOL):
    if len(got) != len(want):
        raise PropertyError(f"{label}: length {len(got)} != {len(want)}")
    for index, (a, b) in enumerate(zip(got, want)):
        _assert_close(f"{label}[{index}]", a, b, tolerance)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]]


def _norm_sq(a):
    return sum(value * value for value in a)


def _norm(a):
    return math.sqrt(_norm_sq(a))


def _qmul(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return [x1 * w2 + w1 * x2 + y1 * z2 - z1 * y2,
            y1 * w2 + w1 * y2 + z1 * x2 - x1 * z2,
            z1 * w2 + w1 * z2 + x1 * y2 - y1 * x2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2]


def _assert_perp(label, a, b, tolerance=PROP_TOL):
    scale = max(1.0, _norm(a) * _norm(b))
    if abs(_dot(a, b)) > tolerance * scale:
        raise PropertyError(f"{label}: dot {_dot(a, b):.6e} is not perpendicular")


def _assert_parallel(label, a, b, tolerance=PROP_TOL):
    scale = max(1.0, _norm(a) * _norm(b))
    if _norm(_cross(a, b)) > tolerance * scale:
        raise PropertyError(f"{label}: cross is not parallel")


def _matrix_close(label, got, want):
    for row in range(4):
        for column in range(4):
            _assert_close(f"{label}[{row},{column}]", got[row, column], want[row, column])


def check_matrix(probe, inputs, outputs):
    a = Matrix(4, 4, inputs[0:16])
    b = Matrix(4, 4, inputs[16:32])
    product = Matrix(4, 4, outputs[0:16])
    transpose = Matrix(4, 4, outputs[16:32])
    determinant = outputs[32]
    inverse = Matrix(4, 4, outputs[33:49])
    identity = Matrix.eye(4)
    _matrix_close("A*inv", a * inverse, identity)
    _matrix_close("inv*A", inverse * a, identity)
    _assert_close("det(A*B)", product.det(), a.det() * b.det())
    _matrix_close("transpose(A).T", transpose.T, a)
    _assert_close("det(A)*det(inv)", a.det() * inverse.det(), 1.0)


def check_vectors(probe, inputs, outputs):
    a = inputs[0:3]
    b = inputs[3:6]
    cross = outputs[0:3]
    dot = outputs[3]
    project = outputs[4:7]
    norm_a = outputs[10:13]
    norm_b = outputs[13:16]
    _assert_perp("cross.a", cross, a)
    _assert_perp("cross.b", cross, b)
    _assert_close("Lagrange", _norm_sq(cross) + dot * dot,
                  _norm_sq(a) * _norm_sq(b))
    _assert_parallel("project//b", project, b)
    _assert_close("|normalize(a)|", _norm(norm_a), 1.0)
    _assert_close("|normalize(b)|", _norm(norm_b), 1.0)


def check_quaternion(probe, inputs, outputs):
    q1 = inputs[0:4]
    q2 = inputs[4:8]
    vector = inputs[9:12]
    multiply = outputs[0:4]
    normalized = outputs[4:8]
    nlerp = outputs[8:12]
    inverse = outputs[16:20]
    rotated = outputs[20:23]
    _assert_close("|normalize(q1)|", _norm(normalized), 1.0)
    _assert_close("|nlerp|", _norm(nlerp), 1.0)
    _assert_close("|q1*q2|", _norm(multiply), _norm(q1) * _norm(q2))
    product = _qmul(q1, inverse)
    if not (_vector_close(product, [0.0, 0.0, 0.0, 1.0])
            or _vector_close(product, [0.0, 0.0, 0.0, -1.0])):
        raise PropertyError(f"q1*inverse is not +/- identity: {product}")
    _assert_close("rotation norm", _norm(rotated), _norm(vector))


def check_fft(probe, inputs, outputs):
    signal = [complex(inputs[2 * index], inputs[2 * index + 1]) for index in range(8)]
    spectrum = [complex(outputs[2 * index], outputs[2 * index + 1]) for index in range(8)]
    _assert_close("Parseval", sum(abs(value) ** 2 for value in spectrum) / 8.0,
                  sum(abs(value) ** 2 for value in signal))
    _assert_close("DC.real", spectrum[0].real, sum(value.real for value in signal))
    _assert_close("DC.imag", spectrum[0].imag, sum(value.imag for value in signal))
    #ai: reconstruct native forward output with a positive-sign, 1/N inverse specification.
    for sample in range(8):
        recovered = sum(
            spectrum[frequency] * complex(
                math.cos(2.0 * math.pi * sample * frequency / 8.0),
                math.sin(2.0 * math.pi * sample * frequency / 8.0))
            for frequency in range(8)) / 8.0
        _assert_close(f"IDFT.real[{sample}]", recovered.real, signal[sample].real)
        _assert_close(f"IDFT.imag[{sample}]", recovered.imag, signal[sample].imag)


def check_convert_coords(probe, inputs, outputs):
    point = inputs[0:3]
    #ai: retain the native nonzero-point domain; real totalized division does not certify the C origin.
    cylinder = outputs[0:3]
    spherical = outputs[6:9]
    if cylinder[0] < 0.0 or spherical[0] <= 0.0 or _norm_sq(point) == 0.0:
        raise PropertyError("coordinate radius/domain: spherical origin is excluded")
    for label, azimuth in (("cylinder", cylinder[1]), ("spherical", spherical[1])):
        if not (-math.pi - PROP_TOL <= azimuth <= math.pi + PROP_TOL):
            raise PropertyError(f"{label}: azimuth outside atan2 principal branch")
    if not (-PROP_TOL <= spherical[2] <= math.pi + PROP_TOL):
        raise PropertyError("spherical inclination outside [0,pi]")
    _assert_close("cylinder height", cylinder[2], point[2])
    _assert_close("cylinder squared radius", cylinder[0] ** 2 + cylinder[2] ** 2, _norm_sq(point))
    _assert_close("spherical radius", spherical[0], _norm(point))
    _assert_close("shared azimuth", cylinder[1], spherical[1])
    _assert_vec_close("cylinder roundtrip", outputs[3:6], point)
    _assert_vec_close("spherical roundtrip", outputs[9:12], point)
    _assert_vec_close("cylinder to spherical", outputs[12:15], spherical)
    _assert_vec_close("spherical to cylinder", outputs[15:18], cylinder)


def check_convert_sky(probe, inputs, outputs):
    eq = inputs[0:3]
    ecliptic = outputs[0:3]
    #ai: check each native direction as well as the existing composition witnesses.
    eq_from_ecliptic = outputs[3:6]
    eq_back = outputs[6:9]
    ecliptic_back = outputs[9:12]
    horizontal = outputs[12:15]
    eq_from_horizontal = outputs[15:18]
    horizontal_back = outputs[18:21]
    horizontal_again = outputs[21:24]
    _assert_close("|ecliptic|", _norm(ecliptic), _norm(eq))
    _assert_close("|ecliptic inverse|", _norm(eq_from_ecliptic), _norm(eq))
    _assert_close("|horizontal|", _norm(horizontal), _norm(eq))
    _assert_close("|horizontal inverse|", _norm(eq_from_horizontal), _norm(eq))
    _assert_vec_close("equatorial roundtrip", eq_back, eq)
    _assert_vec_close("ecliptic roundtrip", ecliptic_back, ecliptic)
    _assert_vec_close("horizontal roundtrip", horizontal_back, eq)
    _assert_vec_close("horizontal repeat", horizontal_again, horizontal)


def check_convert_proj(probe, inputs, outputs):
    point = inputs[0:3]
    _assert_vec_close("horizontal roundtrip", outputs[2:5], point)
    _assert_vec_close("equatorial roundtrip", outputs[7:10], point)


def check_convert_ball(probe, inputs, outputs):
    point = inputs[0:3]
    _assert_vec_close("ball roundtrip", outputs[2:5], point)


def check_sdf(probe, inputs, outputs):
    point = inputs[0:3]
    other = inputs[28:31]
    distance = _norm([point[index] - other[index] for index in range(3)])
    #ai: cone (5) is an approximate SDF and Menger (10) is outside this bound.
    for index in (0, 1, 2, 3, 4, 6, 7, 8, 9):
        first = outputs[index]
        second = outputs[11 + index]
        tolerance = PROP_TOL * max(1.0, abs(first), abs(second))
        if abs(first - second) > distance + tolerance:
            raise PropertyError(
                f"sdf[{index}]: Lipschitz {abs(first - second):.6e} > {distance:.6e}")


def check_convert_color(probe, inputs, outputs):
    rgb = inputs[0:3]
    hsv = inputs[3:6]
    _assert_vec_close("color RGB roundtrip", outputs[6:9], rgb)
    _assert_vec_close("color HSV roundtrip", outputs[9:12], hsv)


def check_convert_new(probe, inputs, outputs):
    source = inputs[0:8]
    roundtrips = outputs[8:16]
    _assert_vec_close("convert_new roundtrips", roundtrips, source)


def check_convert_units(probe, inputs, outputs):
    meters, feet, inches, kelvin, celsius = inputs[0:5]
    expected = [feet, meters, inches, meters, kelvin, celsius]
    _assert_vec_close("convert_units roundtrips", outputs[7:13], expected)


def check_color(probe, inputs, outputs):
    source = inputs[0:4]
    for index, channel in enumerate(outputs[0:12]):
        if not _finite(channel) or channel < 0.0 or channel > 255.0:
            raise PropertyError(f"color channel {index} out of range: {channel}")
    for index in range(4):
        _assert_close(f"ToV4[{index}]", outputs[12 + index], source[index] / 255.0)


def check_scalar(probe, inputs, outputs):
    _, low, high, _, _, _, _, lerp_a, lerp_b, _ = inputs
    clamp, lerp, _, _, wrap = outputs[0:5]
    if not (low - PROP_TOL <= clamp <= high + PROP_TOL):
        raise PropertyError(f"clamp outside [{low}, {high}]: {clamp}")
    if not (min(lerp_a, lerp_b) - PROP_TOL <= lerp <= max(lerp_a, lerp_b) + PROP_TOL):
        raise PropertyError(f"lerp outside endpoints: {lerp}")
    if not (low - PROP_TOL <= wrap <= high + PROP_TOL):
        raise PropertyError(f"wrap outside [{low}, {high}]: {wrap}")


def check_easing(probe, inputs, outputs):
    start, end, _, _, _, _, _ = inputs
    step, linear = outputs[0:2]
    low, high = min(start, end), max(start, end)
    if not (_equal(step, start) or _equal(step, end)):
        raise PropertyError(f"step is neither endpoint: {step}")
    if not (low - PROP_TOL <= linear <= high + PROP_TOL):
        raise PropertyError(f"linear outside endpoints: {linear}")


def _equal(a, b):
    return abs(a - b) <= PROP_TOL * max(1.0, abs(a), abs(b))


def check_sdf_ext(probe, inputs, outputs):
    cell = inputs[14:17]
    repeated = outputs[2:5]
    for index in range(3):
        rem = repeated[index] + 0.5 * cell[index]
        if abs(rem) > cell[index] + PROP_TOL:
            raise PropertyError(f"repeat[{index}] outside fmod range: {rem}")
    point = inputs[0:3]
    other = inputs[23:26]
    distance = _norm([point[index] - other[index] for index in range(3)])
    tolerance = PROP_TOL * max(1.0, abs(outputs[0]), abs(outputs[8]))
    if abs(outputs[0] - outputs[8]) > distance + tolerance:
        raise PropertyError("box-frame Lipschitz bound failed")


def check_em(probe, inputs, outputs):
    offset = inputs[4:7]
    velocity = inputs[8:11]
    magnetic = inputs[11:14]
    field = outputs[2:5]
    lorentz = outputs[5:8]
    _assert_parallel("E//offset", field, offset)
    _assert_perp("Lorentz.velocity", lorentz, velocity)
    _assert_perp("Lorentz.field", lorentz, magnetic)


def check_dipole(probe, inputs, outputs):
    moment = inputs[3:6]
    field = outputs[0:3]
    torque = outputs[3:6]
    _assert_perp("torque.moment", torque, moment)
    _assert_perp("torque.field", torque, field)


CHECKS = {
    "matrix": check_matrix,
    "vectors": check_vectors,
    "quaternion": check_quaternion,
    "fft": check_fft,
    "convert_coords": check_convert_coords,
    "convert_sky": check_convert_sky,
    "convert_proj": check_convert_proj,
    "convert_ball": check_convert_ball,
    "sdf": check_sdf,
    "convert_color": check_convert_color,
    "convert_new": check_convert_new,
    "convert_units": check_convert_units,
    "color": check_color,
    "scalar": check_scalar,
    "easing": check_easing,
    "sdf_ext": check_sdf_ext,
    "em": check_em,
    "dipole": check_dipole,
}


def main():
    parser = argparse.ArgumentParser(description="gX math invariant check")
    parser.add_argument("--binary", default=str(ROOT / "build" / "gX_build"))
    parser.add_argument("--probes", type=int, default=math_random_check.PROBES,
                        help=f"probes per family (default {math_random_check.PROBES}, "
                             f"max {math_random_check.MAX_PROBES})")
    args = parser.parse_args()
    if args.probes < 1 or args.probes > math_random_check.MAX_PROBES:
        print(f"math-property-check: probes must be 1..{math_random_check.MAX_PROBES}",
              file=sys.stderr)
        return 1

    binary = Path(args.binary)
    if not binary.exists():
        print(f"math-property-check: binary not found: {binary}", file=sys.stderr)
        return 1
    try:
        probes = math_random_check.run_dump(binary, args.probes)
    except math_random_check.RandomCheckError as exc:
        print(f"math-property-check: {exc}", file=sys.stderr)
        return 1

    failures = 0
    total = 0
    for family, check in CHECKS.items():
        family_failures = 0
        for probe, (inputs, outputs) in enumerate(probes[family]):
            total += 1
            try:
                check(probe, inputs, outputs)
            except PropertyError as exc:
                print(f"  {family}[{probe}]: {exc}")
                family_failures += 1
                failures += 1
        print(f"  {family:<14} {len(probes[family]):>3} probes  "
              f"{'ok' if family_failures == 0 else 'FAIL'}")
    if failures:
        print(f"MATH_PROPERTY: FAIL ({failures} probe(s))")
        return 1
    print(f"MATH_PROPERTY: PASS — {total} invariant probes across {len(CHECKS)} families")
    return 0


if __name__ == "__main__":
    sys.exit(main())
