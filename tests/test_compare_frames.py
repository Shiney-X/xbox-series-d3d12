# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.compare_frames import compare, read_rgb_png, write_rgb_png  # noqa: E402


class CompareFramesTest(unittest.TestCase):
    def test_exact_and_tolerant_comparisons(self):
        reference = (2, 1, bytes([0, 10, 20, 100, 110, 120]))
        same, difference = compare(reference, reference, 0, 0.0, 0.0)
        self.assertTrue(same["passed"])
        self.assertEqual(difference, bytes(6))

        candidate = (2, 1, bytes([0, 10, 20, 103, 110, 120]))
        strict, difference = compare(reference, candidate, 0, 0.0, 0.0)
        self.assertFalse(strict["passed"])
        self.assertEqual(strict["changed_ratio"], 0.5)
        self.assertEqual(strict["mean_absolute_error"], 0.5)
        self.assertEqual(strict["max_channel_error"], 3)
        self.assertEqual(difference, bytes([0, 0, 0, 3, 0, 0]))

        tolerant, _ = compare(reference, candidate, 3, 0.0, 0.5)
        self.assertTrue(tolerant["passed"])

    def test_png_round_trip_and_crc_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.png"
            pixels = bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])
            write_rgb_png(path, 2, 2, pixels)
            self.assertEqual(read_rgb_png(path), (2, 2, pixels))
            damaged = bytearray(path.read_bytes())
            damaged[20] ^= 1
            path.write_bytes(damaged)
            with self.assertRaisesRegex(ValueError, "CRC mismatch"):
                read_rgb_png(path)

    def test_dimension_mismatch_is_not_a_visual_pass(self):
        with self.assertRaisesRegex(ValueError, "dimension mismatch"):
            compare((1, 1, bytes(3)), (2, 1, bytes(6)), 0, 0.0, 0.0)

    def test_cli_reports_failure_and_writes_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference.png"
            candidate = root / "candidate.png"
            difference = root / "difference.png"
            write_rgb_png(reference, 1, 1, bytes([1, 2, 3]))
            write_rgb_png(candidate, 1, 1, bytes([2, 2, 3]))
            command = [sys.executable, str(Path(__file__).resolve().parents[1]
                                         / "scripts/compare_frames.py"),
                       str(reference), str(candidate), "--diff", str(difference)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 1)
            self.assertIn('"passed": false', result.stdout)
            self.assertEqual(read_rgb_png(difference), (1, 1, bytes([1, 0, 0])))


if __name__ == "__main__":
    unittest.main()
