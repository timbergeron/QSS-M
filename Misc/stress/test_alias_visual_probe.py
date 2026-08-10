#!/usr/bin/env python3
"""Unit tests for the deterministic alias visual comparison helpers."""

import argparse
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alias_visual_probe as V


class SampleTimeTests(unittest.TestCase):
    def test_fractional_range_has_stable_endpoint(self):
        self.assertEqual(V.sample_times(1.0, 2.0, 0.25),
                         [1.0, 1.25, 1.5, 1.75, 2.0])

    def test_invalid_range_is_rejected(self):
        with self.assertRaises(ValueError):
            V.sample_times(2.0, 1.0, 0.25)


class ImageTests(unittest.TestCase):
    def setUp(self):
        # Four columns make the normalized middle-half crop unambiguous.
        self.image = {
            "width": 4,
            "height": 2,
            "pixels": bytes([
                1, 2, 3, 10, 20, 30, 40, 50, 60, 70, 80, 90,
                4, 5, 6, 11, 21, 31, 41, 51, 61, 71, 81, 91,
            ]),
        }

    def test_tga_round_trip_preserves_top_down_rgb(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "image.tga"
            V.write_tga(path, self.image)
            self.assertEqual(V.read_tga(path), self.image)

    def test_normalized_crop_selects_expected_pixels(self):
        cropped, pixels = V.crop_image(self.image, (0.25, 0.0, 0.5, 1.0))
        self.assertEqual(pixels, (1, 0, 2, 2))
        self.assertEqual((cropped["width"], cropped["height"]), (2, 2))
        self.assertEqual(cropped["pixels"], bytes([
            10, 20, 30, 40, 50, 60,
            11, 21, 31, 41, 51, 61,
        ]))

    def test_comparison_counts_pixels_above_tolerance(self):
        candidate = dict(self.image)
        changed = bytearray(candidate["pixels"])
        changed[0] += 10
        candidate["pixels"] = bytes(changed)
        metrics, diff = V.compare_images(
            self.image, candidate, pixel_tolerance=2, diff_gain=4)
        self.assertEqual(metrics["max_abs"], 10)
        self.assertEqual(metrics["changed_pixels"], 1)
        self.assertEqual(metrics["changed_fraction"], 1 / 8)
        self.assertAlmostEqual(metrics["mean_abs"], 10 / 24)
        self.assertEqual(diff["pixels"][:3], bytes([40, 0, 0]))


class ArgumentTests(unittest.TestCase):
    def test_crop_validation(self):
        self.assertIsNone(V.parse_crop("full"))
        self.assertEqual(V.parse_crop(".7,.3,.2,.25"),
                         (0.7, 0.3, 0.2, 0.25))
        with self.assertRaises(argparse.ArgumentTypeError):
            V.parse_crop(".9,.3,.2,.25")


class ArtifactPolicyTests(unittest.TestCase):
    def test_visual_report_and_crops_stay_inside_allowed_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / "base").mkdir()
            (work / "visuals").mkdir()
            (work / "visual-report.json").write_text("{}\n")
            (work / "visuals" / "crop.tga").write_bytes(b"fixture")
            self.assertEqual(V.S.sandbox_escapes(work), [])


if __name__ == "__main__":
    unittest.main()
