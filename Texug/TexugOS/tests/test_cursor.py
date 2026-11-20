import sys
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
import mkcursor


class CursorAssetTests(unittest.TestCase):
    def test_png_is_reduced_to_a_small_transparent_palette_bitmap(self):
        image_path = PROJECT_ROOT / "assets" / "Mouse.png"
        width, height, rows = mkcursor.read_rgba_png(image_path)
        bitmap = mkcursor.scale_cursor(width, height, rows)

        self.assertEqual(len(bitmap), 12 * 16)
        self.assertTrue({0, 15, 0xFF}.issubset(set(bitmap)))


if __name__ == "__main__":
    unittest.main()
