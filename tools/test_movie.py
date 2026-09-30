"""tools/movie.py's SWF shell, read back: what the game's GFx and FFDec will parse."""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import movie


def read_rect(data: bytes, at: int) -> tuple[list[int], int]:
    """A SWF RECT's four values in twips, and the offset after it."""
    bits = "".join(f"{b:08b}" for b in data[at : at + 20])
    nbits = int(bits[:5], 2)
    values = [int(bits[5 + i * nbits : 5 + (i + 1) * nbits], 2) for i in range(4)]
    used = 5 + 4 * nbits
    return values, at + (used + 7) // 8


def read_tags(data: bytes, at: int) -> list[tuple[int, bytes]]:
    tags = []
    while at < len(data):
        (header,) = struct.unpack_from("<H", data, at)
        code, length = header >> 6, header & 0x3F
        at += 2
        if length == 0x3F:
            (length,) = struct.unpack_from("<I", data, at)
            at += 4
        tags.append((code, data[at : at + length]))
        at += length
        if code == 0:
            break
    return tags


class Shell(unittest.TestCase):
    def setUp(self) -> None:
        self.data = movie.shell()

    def test_header_is_an_uncompressed_swf_8_of_its_own_length(self) -> None:
        self.assertEqual(self.data[:3], b"FWS")
        self.assertEqual(self.data[3], 8)
        self.assertEqual(struct.unpack_from("<I", self.data, 4)[0], len(self.data))

    def test_stage_frame_rate_and_one_frame(self) -> None:
        rect, at = read_rect(self.data, 8)
        self.assertEqual(rect, [0, 1280 * 20, 0, 720 * 20])
        rate, frames = struct.unpack_from("<HH", self.data, at)
        self.assertEqual(rate, 30 << 8)
        self.assertEqual(frames, 1)

    def test_tags_are_the_fonts_then_a_script_on_the_one_frame(self) -> None:
        _, at = read_rect(self.data, 8)
        codes = [code for code, _ in read_tags(self.data, at + 4)]
        # FileAttributes, two ImportAssets2, DoAction, ShowFrame, End.
        self.assertEqual(codes, [69, 71, 71, 12, 1, 0])

    def test_fonts_are_imported_as_the_games_movies_import_them(self) -> None:
        _, at = read_rect(self.data, 8)
        imports = [body for code, body in read_tags(self.data, at + 4) if code == 71]
        # portraits.gfx's own import of $BodyFont, byte for byte but for its
        # character id (19 there, 2 here).
        game = bytes.fromhex(
            "67667866 6f6e746c 69622e73 776600 01 00 0100 1300 24426f64 79466f6e 7400".replace(
                " ", ""
            )
        )
        ours = imports[1]
        self.assertEqual(ours[: len(ours) - 13], game[: len(game) - 13])
        self.assertEqual(
            ours[-11:], game[-11:]
        )  # "$BodyFont\0" and what precedes it by one
        self.assertIn(b"$TitleFont\0", imports[0])

    def test_the_script_is_empty_until_ffdec_fills_it(self) -> None:
        _, at = read_rect(self.data, 8)
        scripts = [body for code, body in read_tags(self.data, at + 4) if code == 12]
        self.assertEqual(scripts, [b"\0"])


class EmptyMovie(unittest.TestCase):
    def test_a_one_frame_swf_with_nothing_in_it(self) -> None:
        data = movie.empty_movie()
        self.assertEqual(data[:3], b"FWS")
        self.assertEqual(struct.unpack_from("<I", data, 4)[0], len(data))
        _, at = read_rect(data, 8)
        self.assertEqual([code for code, _ in read_tags(data, at + 4)], [69, 1, 0])


class Missing(unittest.TestCase):
    SOURCE = """function ask()
{
    ExternalInterface.call("TB_GetView");
}
function draw(view)
"""

    def test_a_whole_script_lacks_nothing(self) -> None:
        compiled = (
            'function ask(){ExternalInterface.call("TB_GetView");}function draw(view){}'
        )
        self.assertEqual(movie.missing(self.SOURCE, compiled), [])

    def test_a_short_script_names_what_it_lost(self) -> None:
        self.assertEqual(
            movie.missing(self.SOURCE, "function ask(){}"), ["TB_GetView", "draw"]
        )

    def test_the_source_itself_is_whole(self) -> None:
        source = (movie.SOURCE / "frame_1" / "DoAction.as").read_text(encoding="utf-8")
        self.assertEqual(movie.missing(source, source), [])
        self.assertIn("TB_GetView", source)


class Rect(unittest.TestCase):
    def test_bits_are_enough_for_the_largest_signed_value(self) -> None:
        for w, h in ((1, 1), (1280, 720), (1920, 1080), (4096, 4096)):
            values, _ = read_rect(movie.rect(w, h), 0)
            self.assertEqual(values, [0, w * 20, 0, h * 20])


if __name__ == "__main__":
    unittest.main()
