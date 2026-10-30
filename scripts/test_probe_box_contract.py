#ai: exact-rational inside-box projection fixtures; no renderer or GPU certification.
from fractions import Fraction as Q
from itertools import product
import unittest
from pathlib import Path


def exit_hit(origin, direction, center, half_extents):
    """Return the first forward exit of a nonzero ray from a closed AABB."""
    if any(h <= 0 for h in half_extents):
        raise ValueError("positive half-extents required")
    local = tuple(p - c for p, c in zip(origin, center))
    if any(abs(p) > h for p, h in zip(local, half_extents)):
        raise ValueError("origin must be inside the closed box")
    times = [((h if d > 0 else -h) - p) / d
             for p, d, h in zip(local, direction, half_extents) if d != 0]
    if not times:
        raise ValueError("nonzero direction required")
    time = min(times)
    return time, tuple(p + time * d for p, d in zip(origin, direction))


def face_oracle(origin, direction, center, half_extents):
    """Independent oracle: enumerate face intersections and keep in-box hits."""
    candidates = []
    for axis in range(3):
        if direction[axis] == 0:
            continue
        for sign in (-1, 1):
            time = (center[axis] + sign * half_extents[axis]
                    - origin[axis]) / direction[axis]
            if time < 0:
                continue
            hit = tuple(p + time * d for p, d in zip(origin, direction))
            if all(abs(p - c) <= h for p, c, h in zip(hit, center, half_extents)):
                candidates.append((time, hit))
    return min(candidates)


def parallel(a, b):
    return all(a[i] * b[j] == a[j] * b[i]
               for i, j in ((0, 1), (0, 2), (1, 2)))


#ai: reference for proposed outside-box fallback and separately owned capture origin.
def lookup_direction(origin, direction, box_center, half_extents, capture_center):
    if not any(direction):
        raise ValueError("nonzero direction required")
    if any(h <= 0 for h in half_extents):
        raise ValueError("positive half-extents required")
    if any(abs(p - c) > h for p, c, h in zip(origin, box_center, half_extents)):
        return direction
    _, hit = exit_hit(origin, direction, box_center, half_extents)
    lookup = tuple(p - c for p, c in zip(hit, capture_center))
    return lookup if any(lookup) else direction


#ai: unit-normal reflection reference matching GLSL reflect(I,N).
def reflect(incident, normal):
    if sum(n * n for n in normal) != 1:
        raise ValueError("unit normal required")
    dot = sum(i * n for i, n in zip(incident, normal))
    return tuple(i - 2 * dot * n for i, n in zip(incident, normal))


#ai: capture-origin witness for distinct normalized world reflection planes.
def mirrored_camera(camera, normal, plane_distance):
    if sum(n * n for n in normal) != 1:
        raise ValueError("unit normal required")
    distance = sum(p * n for p, n in zip(camera, normal)) - plane_distance
    return tuple(p - 2 * distance * n for p, n in zip(camera, normal))


#ai: standard GL face order; tie policy is irrelevant to the non-seam witnesses.
def cubemap_face(direction):
    if not any(direction):
        raise ValueError("nonzero direction required")
    axis = max(range(3), key=lambda i: abs(direction[i]))
    return 2 * axis + (direction[axis] < 0)


class ProbeBoxContract(unittest.TestCase):
    def test_face_oracle_and_exit_invariants(self):
        center = (Q(3), Q(-2), Q(5))
        half_extents = (Q(50), Q(12), Q(50))
        for fractions in product((Q(-3, 4), Q(0), Q(3, 4)), repeat=3):
            origin = tuple(c + f * h for c, f, h
                           in zip(center, fractions, half_extents))
            for direction in product((Q(-2), Q(0), Q(1)), repeat=3):
                if not any(direction):
                    continue
                time, hit = exit_hit(origin, direction, center, half_extents)
                self.assertEqual((time, hit), face_oracle(
                    origin, direction, center, half_extents))
                self.assertGreater(time, 0)
                self.assertTrue(all(abs(p - c) <= h for p, c, h
                                    in zip(hit, center, half_extents)))
                self.assertTrue(any(abs(p - c) == h for p, c, h
                                    in zip(hit, center, half_extents)))
                for sample_time in (Q(0), time / 2):
                    self.assertTrue(all(abs(p + sample_time * d - c) < h
                                        for p, d, c, h in zip(
                                            origin, direction, center, half_extents)))

    def test_axis_corner_and_near_parallel(self):
        zero = (Q(0),) * 3
        unit = (Q(1),) * 3
        for axis in range(3):
            for sign in (-1, 1):
                direction = tuple(Q(sign if i == axis else 0) for i in range(3))
                self.assertEqual(exit_hit(zero, direction, zero, unit),
                                 (Q(1), direction))
        self.assertEqual(exit_hit(zero, unit, zero, unit), (Q(1), unit))
        tiny = Q(1, 10**9)
        self.assertEqual(exit_hit(zero, (tiny, Q(0), Q(1)), zero, unit),
                         (Q(1), (tiny, Q(0), Q(1))))

    def test_boundary_outward_inward_and_tangent(self):
        center = (Q(0),) * 3
        half_extents = (Q(1),) * 3
        origin = (Q(1), Q(0), Q(0))
        self.assertEqual(exit_hit(origin, (Q(1), Q(0), Q(0)), center,
                                  half_extents), (Q(0), origin))
        self.assertEqual(exit_hit(origin, (Q(-1), Q(0), Q(0)), center,
                                  half_extents), (Q(2), (Q(-1), Q(0), Q(0))))
        self.assertEqual(exit_hit(origin, (Q(0), Q(1), Q(0)), center,
                                  half_extents), (Q(1), (Q(1), Q(1), Q(0))))

    def test_translation_and_direction_scale(self):
        origin = (Q(1, 4), Q(-1, 3), Q(0))
        direction = (Q(2), Q(-1), Q(3))
        center = (Q(0),) * 3
        half_extents = (Q(2), Q(1), Q(4))
        time, hit = exit_hit(origin, direction, center, half_extents)
        shift = (Q(5), Q(-8), Q(11))
        translated = exit_hit(tuple(p + s for p, s in zip(origin, shift)),
                              direction, shift, half_extents)
        self.assertEqual(translated, (time, tuple(p + s for p, s in zip(hit, shift))))
        self.assertEqual(exit_hit(origin, tuple(7 * d for d in direction),
                                  center, half_extents), (time / 7, hit))

    def test_current_projection_counterexample(self):
        origin = center = (Q(0),) * 3
        direction = (Q(1), Q(2), Q(0))
        half_extents = (Q(50), Q(12), Q(50))
        time, hit = exit_hit(origin, direction, center, half_extents)
        self.assertEqual((time, hit), (Q(6), (Q(6), Q(12), Q(0))))
        lookup = tuple(p - c for p, c in zip(hit, center))
        current = tuple((p - c + d * h / max(abs(d), Q(1, 1000))) / h
                        for p, c, d, h in zip(origin, center, direction, half_extents))
        self.assertEqual(current, (Q(1), Q(1), Q(0)))
        self.assertFalse(parallel(current, lookup))
        self.assertTrue(parallel(lookup, direction))

    def test_reject_undefined_inputs(self):
        zero = (Q(0),) * 3
        unit = (Q(1),) * 3
        for args, message in (
            ((zero, zero, zero, unit), "nonzero"),
            ((zero, unit, zero, zero), "positive"),
            (((Q(2), Q(0), Q(0)), unit, zero, unit), "inside"),
        ):
            with self.assertRaisesRegex(ValueError, message):
                exit_hit(*args)


    def test_outgoing_ray_sign_and_normal_orientation(self):
        view = (Q(1), Q(2), Q(3))
        normal = (Q(0), Q(1), Q(0))
        outgoing = reflect(tuple(-v for v in view), normal)
        current = reflect(view, normal)
        self.assertEqual(outgoing, (Q(-1), Q(2), Q(-3)))
        self.assertEqual(current, tuple(-d for d in outgoing))
        self.assertGreater(sum(d * n for d, n in zip(outgoing, normal)), 0)
        self.assertLess(sum(d * n for d, n in zip(current, normal)), 0)
        self.assertEqual(outgoing, reflect(tuple(-v for v in view),
                                           tuple(-n for n in normal)))

    def test_distinct_capture_and_box_centers(self):
        zero = (Q(0),) * 3
        extents = (Q(2),) * 3
        ray = (Q(1), Q(1), Q(0))
        capture_center = (Q(1), Q(0), Q(0))
        lookup = lookup_direction(zero, ray, zero, extents, capture_center)
        self.assertEqual(lookup, (Q(1), Q(2), Q(0)))
        self.assertNotEqual(lookup, lookup_direction(zero, ray, zero, extents, zero))

    def test_outside_box_does_not_project_even_when_ray_enters(self):
        zero = (Q(0),) * 3
        unit = (Q(1),) * 3
        origin = (Q(2), Q(0), Q(0))
        for ray in ((Q(-1), Q(0), Q(0)), (Q(1), Q(0), Q(0)),
                    (Q(0), Q(1), Q(0))):
            self.assertEqual(lookup_direction(origin, ray, zero, unit, zero), ray)

    def test_zero_lookup_uses_outgoing_ray(self):
        zero = (Q(0),) * 3
        unit = (Q(1),) * 3
        ray = (Q(1), Q(0), Q(0))
        self.assertEqual(lookup_direction(zero, ray, zero, unit, ray), ray)

    def test_shared_dynamic_capture_cannot_use_consumer_plane(self):
        camera = (Q(2), Q(4), Q(5))
        normal = (Q(0), Q(1), Q(0))
        captured = mirrored_camera(camera, normal, Q(0))
        other_consumer = mirrored_camera(camera, normal, Q(1))
        self.assertEqual(captured, (Q(2), Q(-4), Q(5)))
        self.assertEqual(other_consumer, (Q(2), Q(-2), Q(5)))
        self.assertNotEqual(captured, other_consumer)
        box_center = (Q(0),) * 3
        extents = (Q(10),) * 3
        ray = (Q(1), Q(1), Q(0))
        self.assertFalse(parallel(
            lookup_direction(box_center, ray, box_center, extents, captured),
            lookup_direction(box_center, ray, box_center, extents, other_consumer)))

    def test_direction_only_sky_must_not_gain_capture_position_parallax(self):
        origin = (Q(0),) * 3
        direction = (Q(1), Q(1), Q(2))
        extents = (Q(50), Q(12), Q(50))
        center_a = (Q(3), Q(3, 2), Q(-3))
        center_b = (Q(3), Q(3, 2), Q(3))
        lookup_a = lookup_direction(origin, direction, center_a, extents, center_a)
        lookup_b = lookup_direction(origin, direction, center_b, extents, center_b)
        self.assertFalse(parallel(lookup_a, lookup_b))
        self.assertFalse(parallel(lookup_a, direction))
        self.assertFalse(parallel(lookup_b, direction))
        #ai: an asymmetric direction-only environment reveals the artificial shift.
        sky = lambda d: d[2] / d[1]
        self.assertEqual(sky(direction), Q(2))
        self.assertNotEqual(sky(lookup_a), sky(lookup_b))

    def test_face_metric_rejects_two_channel_palette_colors(self):
        palette = ((255, 0, 0), (0, 255, 0), (0, 0, 255),
                   (255, 255, 0), (255, 0, 255), (0, 255, 255))
        for index, color in enumerate(palette):
            ordered = sorted(color)
            current_metric = ordered[-1] - ordered[-2] > 12
            self.assertEqual(current_metric, index < 3)
            self.assertGreater(ordered[-1] - ordered[0], 12)

    def test_candidate_sky_classification_source_contract(self):
        root = Path(__file__).resolve().parent.parent
        draw = (root / "src/renderer/gfx/draw.c").read_text()
        callback = draw.split("static gX_I0 gfx_cubemap_draw_face", 1)[1]
        callback = callback.split("//opencode: point light shadow", 1)[0]
        self.assertLess(callback.index("if (!m->loaded) continue;"),
                        callback.index("g_cubemap_hasLocal = 1;"))
        self.assertLess(callback.index("if (m->params.enableReflections"),
                        callback.index("g_cubemap_hasLocal = 1;"))
        self.assertLess(callback.index("g_cubemap_hasLocal = 1;"),
                        callback.index("gX_RendererGFX_Model_Draw(m"))
        self.assertIn("dynamicSkyOnly = !g_cubemap_hasLocal;", draw)
        self.assertIn("probe->skyOnly = !g_cubemap_hasLocal;", draw)
        shader = (root / "resources/shader/standard.frag").read_text()
        helper = shader.split("vec3 gX_Probe_BoxDirection", 1)[1]
        helper = helper.split("void reflection", 1)[0]
        self.assertLess(helper.index("if (gXProbeBoxCorrect == 0)"),
                        helper.index("if (skyOnly == 1) return direction;"))
        self.assertEqual(shader.count("cubeSize, gXProbeSkyOnlyA)"), 2)
        self.assertEqual(shader.count("cubeSize, gXProbeSkyOnlyB)"), 2)

    def test_asymmetric_face_correspondence(self):
        witnesses = ((Q(3), Q(1), Q(-2)), (Q(-3), Q(1), Q(2)),
                     (Q(1), Q(3), Q(-2)), (Q(1), Q(-3), Q(2)),
                     (Q(-2), Q(1), Q(3)), (Q(2), Q(1), Q(-3)))
        for face, direction in enumerate(witnesses):
            self.assertEqual(cubemap_face(direction), face)
            self.assertEqual(cubemap_face(tuple(-v for v in direction)), face ^ 1)
        self.assertEqual(cubemap_face((Q(-402, 1000), Q(179, 1000),
                                      Q(-898, 1000))), 5)
        with self.assertRaisesRegex(ValueError, "nonzero"):
            cubemap_face((Q(0),) * 3)


if __name__ == "__main__":
    unittest.main()
