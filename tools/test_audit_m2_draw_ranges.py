"""Synthetic vertex-decoder regressions; no game assets required."""
import math
import struct
import unittest
from audit_m2_draw_ranges import vertex_sanity


def model_vertex():
    data = bytearray(80 + 48)
    struct.pack_into("<I", data, 44, 1)
    struct.pack_into("<3f4B4B3f4f", data, 80,
                     1.0, 2.0, 3.0, 255, 0, 0, 0, 0, 0, 0, 0,
                     0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0)
    return data


class VertexAuditTests(unittest.TestCase):
    def test_valid_vertex(self):
        report = vertex_sanity(model_vertex(), 80, 1)
        self.assertTrue(report["passes"])
        self.assertEqual(report["weight_sum_histogram"], {255: 1})
        self.assertAlmostEqual(report["position"]["finite_max_magnitude"], math.sqrt(14))

    def test_nonfinite_fields_fail_without_nonstandard_json_numbers(self):
        for offset, field, value in [(0, "position", float("nan")),
                                     (20, "normal", float("inf")),
                                     (32, "uv", float("-inf"))]:
            with self.subTest(field=field):
                data = model_vertex()
                struct.pack_into("<f", data, 80 + offset, value)
                report = vertex_sanity(data, 80, 1)
                self.assertFalse(report["passes"])
                self.assertEqual(report[field]["nonfinite_components"], 1)
                self.assertEqual(report[field]["nonfinite_vertices"], 1)

    def test_weighted_bone_past_raw_count_is_reported(self):
        data = model_vertex()
        data[80 + 16] = 240
        report = vertex_sanity(data, 80, 1)
        self.assertFalse(report["passes"])
        self.assertEqual(report["weighted_bone_index_oob_components"], 1)
        self.assertEqual(report["shader_clamped_index_components"], 1)


if __name__ == "__main__":
    unittest.main()
