"""Offline checks for exact regional biome masks; never imports Unreal."""

import math
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "Content" / "Python"))
import prk_biome_region_geometry as geometry


class RegionGeometryChecks(unittest.TestCase):
    def test_native_uint8_array_export_is_complete_and_byte_exact(self):
        self.assertEqual(geometry.uint8_array_export(" (5, 25,0,255) \n", 4), bytes([5, 25, 0, 255]))
        self.assertEqual(geometry.uint8_array_export("(000,001,254,255)", 4), bytes([0, 1, 254, 255]))

    def test_native_uint8_array_export_refuses_partial_or_malformed_data(self):
        for text in (None, "5,25,0,255", "(5,25,0)", "(5,25,0,255,7)",
                     "(5,25,0,256)", "(5,25,0,-1)", "(5,25,0,2.5)",
                     "(5,,25,0,255)", "(5,25,0,255) trailing"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                geometry.uint8_array_export(text, 4)

    def test_detached_piece_is_planned_and_painted(self):
        res = 12
        channel = bytearray([3] * (res * res))
        for start in (1, 9):
            for y in range(start, start + 2):
                for x in range(start, start + 2):
                    channel[y * res + x] = 7
        args = (bytes(channel), res, 7, [0, 0], [12, 12], [0, 0], [4, 4])
        self.assertEqual(geometry.candidate_components(*args), [[0, 0], [2, 2]])
        for component in ([0, 0], [2, 2]):
            self.assertEqual(geometry.selector(*args, component, 5)[2 * 5 + 2], 255)

    def test_tiny_detached_texel_is_never_dropped_from_candidates(self):
        channel = bytearray([3] * 144)
        channel[1 * 12 + 1] = 7
        channel[11 * 12 + 11] = 7
        self.assertEqual(geometry.candidate_components(
            channel, 12, 7, [0, 0], [12, 12], [0, 0], [4, 4]), [[0, 0], [2, 2]])

    def test_hole_inside_footprint_defeats_center_and_corner_checks(self):
        channel = bytearray([7] * 256)
        args = (channel, 16, 7, [0, 0], [16, 16], [0, 0], [16, 16], [0, 0], 3)
        self.assertEqual(geometry.selector(*args)[4], 255)
        # Middle sample has footprint [4,12] x [4,12]. This hole touches neither
        # the center nor a corner, but belongs to another zone.
        channel[6 * 16 + 6] = 3
        self.assertEqual(geometry.selector(*args)[4], 0)

    def test_boundary_and_entire_painted_domains_preserve_neighbors(self):
        res = 8
        channel = bytes(7 if x < 4 else 3 for y in range(res) for x in range(res))
        pixels = geometry.selector(channel, res, 7, [0, 0], [8, 8], [0, 0], [8, 8], [0, 0], 9)
        self.assertEqual(pixels[4 * 9 + 3], 255)
        self.assertEqual(pixels[4 * 9 + 4], 0)
        self.assertEqual(pixels[4 * 9], 0)  # footprint extends outside the canonical map
        # Independently intersect every canonical texel with every painted domain.
        for y in range(9):
            for x in range(9):
                if not pixels[y * 9 + x]:
                    continue
                low_x, high_x, low_y, high_y = x - 0.5, x + 0.5, y - 0.5, y + 0.5
                self.assertTrue(0 <= low_x <= high_x <= 8 and 0 <= low_y <= high_y <= 8)
                for row in range(res):
                    for col in range(res):
                        if max(low_x, col) < min(high_x, col + 1) and max(low_y, row) < min(high_y, row + 1):
                            self.assertEqual(channel[row * res + col], 7)

    def test_negative_origin_and_component_indices(self):
        channel = bytes(7 if x < 4 else 3 for y in range(8) for x in range(8))
        positive = geometry.selector(channel, 8, 7, [0, 0], [8, 8], [0, 0], [8, 8], [0, 0], 9)
        translated = geometry.selector(channel, 8, 7, [-1200, -350], [8, 8],
                                       [-1192, -342], [8, 8], [-1, -1], 9)
        self.assertEqual(positive, translated)
        self.assertEqual(geometry.candidate_components(channel, 8, 7, [-1200, -350], [8, 8],
                                                       [-1196, -346], [4, 4]), [[-1, -1], [-1, 0]])

    def test_shared_seam_selectors_agree_even_beside_hole(self):
        channel = bytearray([7] * 24 * 24)
        channel[7 * 24 + 8] = 3
        args = (channel, 24, 7, [0, 0], [24, 24], [0, 0], [8, 8])
        left = geometry.selector(*args, [0, 0], 9)
        right = geometry.selector(*args, [1, 0], 9)
        self.assertEqual(left[8::9], right[0::9])
        self.assertIn(255, left[8::9])
        self.assertEqual(left[7 * 9 + 8], 0)

    def test_offset_anisotropic_grid_covers_zone_and_never_spills(self):
        res, size = 7, 5
        origin, extent, grid, component = [-11.5, -30.25], [14, 21], [-12, -32], [5, 8]
        channel = bytearray([3] * res * res)
        for row in range(1, 6):
            for col in range(1, 6):
                channel[row * res + col] = 7
        channel[3 * res + 3] = 3
        channel[6 * res + 6] = 7
        zone_rects = [(origin[0] + col * 2, origin[1] + row * 3,
                       origin[0] + (col + 1) * 2, origin[1] + (row + 1) * 3)
                      for row in range(res) for col in range(res) if channel[row * res + col] == 7]

        def overlaps(a, b):
            return max(a[0], b[0]) < min(a[2], b[2]) and max(a[1], b[1]) < min(a[3], b[3])

        # Independently intersect complete tile rectangles with every zone texel.
        expected = set()
        for cy in range(-1, 4):
            for cx in range(-1, 4):
                tile = (grid[0] + cx * 5, grid[1] + cy * 8,
                        grid[0] + (cx + 1) * 5, grid[1] + (cy + 1) * 8)
                if any(overlaps(tile, rect) for rect in zone_rects):
                    expected.add((cx, cy))
        candidates = geometry.candidate_components(channel, res, 7, origin, extent, grid, component)
        self.assertEqual({tuple(c) for c in candidates}, expected)
        painted = 0
        for cx, cy in candidates:
            pixels = geometry.selector(channel, res, 7, origin, extent, grid, component, [cx, cy], size)
            for y in range(size):
                for x in range(size):
                    if pixels[y * size + x] != 255:
                        continue
                    painted += 1
                    center_x, center_y = grid[0] + cx * 5 + x * 1.25, grid[1] + cy * 8 + y * 2
                    footprint = (center_x - 0.625, center_y - 1, center_x + 0.625, center_y + 1)
                    self.assertTrue(origin[0] <= footprint[0] <= footprint[2] <= origin[0] + extent[0])
                    self.assertTrue(origin[1] <= footprint[1] <= footprint[3] <= origin[1] + extent[1])
                    for row in range(res):
                        for col in range(res):
                            texel = (origin[0] + col * 2, origin[1] + row * 3,
                                     origin[0] + (col + 1) * 2, origin[1] + (row + 1) * 3)
                            if overlaps(footprint, texel):
                                self.assertEqual(channel[row * res + col], 7)
        self.assertGreater(painted, 0)

    def test_png_is_valid_grayscale_and_round_trips_rows(self):
        pixels = bytes((0, 255, 0, 255, 0, 255, 0, 255, 0))
        png = geometry.selector_png(pixels, 3)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset, kinds, compressed = 8, [], bytearray()
        while offset < len(png):
            length = struct.unpack_from(">I", png, offset)[0]
            kind = png[offset + 4:offset + 8]
            payload = png[offset + 8:offset + 8 + length]
            crc = struct.unpack_from(">I", png, offset + 8 + length)[0]
            self.assertEqual(crc, zlib.crc32(kind + payload) & 0xFFFFFFFF)
            kinds.append(kind)
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">IIBBBBB", payload), (3, 3, 8, 0, 0, 0, 0))
            elif kind == b"IDAT":
                compressed.extend(payload)
            offset += length + 12
        self.assertEqual(kinds, [b"IHDR", b"IDAT", b"IEND"])
        self.assertEqual(offset, len(png))
        decoded = zlib.decompress(compressed)
        self.assertEqual(decoded, b"\0" + pixels[:3] + b"\0" + pixels[3:6] + b"\0" + pixels[6:])

    def test_rejects_invalid_geometry_and_png(self):
        valid = [bytes([7] * 4), 2, 7, [0, 0], [2, 2], [0, 0], [2, 2], [0, 0], 3]
        invalid = {0: b"bad", 1: 0, 2: 256, 3: [math.nan, 0], 4: [2, math.inf],
                   5: [0], 6: [0, 2], 7: [0.5, 0], 8: 1}
        for index, value in invalid.items():
            with self.subTest(argument=index):
                args = list(valid)
                args[index] = value
                with self.assertRaises(ValueError):
                    geometry.selector(*args)
        with self.assertRaises(ValueError):
            geometry.candidate_components(b"bad", 2, 7, [0, 0], [2, 2], [0, 0], [2, 2])
        with self.assertRaises(ValueError):
            geometry.selector_png(bytes([0, 1, 0, 255]), 2)
        with self.assertRaises(ValueError):
            geometry.selector_png(b"", 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
