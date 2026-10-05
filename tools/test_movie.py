"""tools/movie.py's SWF shell, read back: what the game's GFx and FFDec will parse.

And the two movies it builds: each from its own folder, and the tactics
page's box's script against the plugin's side of it.
"""

from __future__ import annotations

import re
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

    def test_each_movie_has_the_shell_with_its_own_stage(self) -> None:
        # The tactics page's box lives on the tactics screen's stage, 1024 by
        # 768 (tactics.gfx), and its shell is otherwise the turn bar's.
        self.assertEqual(self.data, movie.shell(movie.TURNORDER.stage))
        data = movie.shell(movie.TACTICS.stage)
        self.assertEqual(struct.unpack_from("<I", data, 4)[0], len(data))
        rect, at = read_rect(data, 8)
        self.assertEqual(rect, [0, 1024 * 20, 0, 768 * 20])
        _, ours = read_rect(self.data, 8)
        self.assertEqual(data[at:], self.data[ours:])


class Movies(unittest.TestCase):
    def test_two_movies_each_built_by_its_name_from_its_own_folder(self) -> None:
        self.assertEqual(
            [m.name for m in movie.MOVIES], ["turnorder", "turnbased_tactics"]
        )
        for m in movie.MOVIES:
            with self.subTest(m.name):
                self.assertEqual(m.out, movie.OUT_DIR / f"{m.name}.swf")
                self.assertEqual(m.script, m.source / "frame_1" / "DoAction.as")
                self.assertTrue(m.script.exists(), m.script)
        self.assertNotEqual(movie.TURNORDER.source, movie.TACTICS.source)


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
        source = movie.TURNORDER.script.read_text(encoding="utf-8")
        self.assertEqual(movie.missing(source, source), [])
        self.assertIn("TB_GetView", source)

    def test_the_script_knows_the_stage_the_shell_is_made_with(self) -> None:
        # The movement preview takes the view's pixels to stage points by the
        # stage's size, which in the game Stage.height does not give.
        source = movie.TURNORDER.script.read_text(encoding="utf-8")
        width, height = movie.TURNORDER.stage
        self.assertIn(f"var STAGE_W = {width};", source)
        self.assertIn(f"var STAGE_H = {height};", source)
        self.assertIn("viewH / STAGE_H", source)

    def test_the_tactics_boxs_names_are_held_to_the_compiled_movie(self) -> None:
        # Its calls to the plugin and the clips it makes are TurnBased_...,
        # not TB_...: a compiled movie without one of them is not shipped.
        source = """function Refresh()
{
    ExternalInterface.call("TurnBased_TacticsBox", id);
    fscommand("TurnBased_UseWhenTurnBased", id + " 1");
}
var BOX = "TurnBased_mc";
"""
        self.assertEqual(
            movie.missing(source, "function Refresh(){}"),
            ["TurnBased_TacticsBox", "TurnBased_UseWhenTurnBased", "TurnBased_mc"],
        )
        self.assertEqual(movie.missing(source, source), [])


class TacticsScript(unittest.TestCase):
    """ui/tactics against what the plugin answers (src/game/TacticsBox.cpp):
    the two are a pair, and nothing but the game runs them together."""

    def setUp(self) -> None:
        self.source = movie.TACTICS.script.read_text(encoding="utf-8")
        plugin = movie.REPO / "src" / "game" / "TacticsBox.cpp"
        self.plugin = plugin.read_text(encoding="utf-8")

    def test_the_source_itself_is_whole(self) -> None:
        self.assertEqual(movie.missing(self.source, self.source), [])

    def test_every_call_of_the_movies_is_one_the_plugin_answers(self) -> None:
        calls = set(re.findall(r'(?:call|fscommand)\("(TurnBased_\w+)"', self.source))
        self.assertEqual(
            calls,
            {
                "TurnBased_TacticsBox",
                "TurnBased_UseWhenTurnBased",
                "TurnBased_TacticsSays",
            },
        )
        answered = set(
            re.findall(
                r'On(?:ExternalInterface|FSCommand)\("(TurnBased_\w+)"', self.plugin
            )
        )
        self.assertEqual(calls, answered)

    def test_the_plugin_calls_the_function_the_movie_has_in_the_clip_it_loads(
        self,
    ) -> None:
        clip = re.search(r'kClip = "(\w+)"', self.plugin)
        self.assertIsNotNone(clip)
        self.assertIn(f'kRefresh = "_root.{clip.group(1)}.Refresh"', self.plugin)
        self.assertIn("\nfunction Refresh()\n", self.source)
        # And loads the movie by the name it is built as.
        self.assertIn(f'kMovie = L"{movie.TACTICS.name}"', self.plugin)

    def test_the_box_and_its_caption_are_not_the_clip_the_movie_is_loaded_into(
        self,
    ) -> None:
        # The movie's own clip is the plugin's to name, and the box is made
        # at the screen's root beside it: one name for both would have the
        # box attached over the script that attaches it.
        clip = re.search(r'kClip = "(\w+)"', self.plugin).group(1)
        box = re.search(r'var BOX = "(\w+)"', self.source).group(1)
        caption = re.search(r'var CAPTION = "(\w+)"', self.source).group(1)
        self.assertEqual(len({clip, box, caption}), 3)

    def test_the_layouts_numbers_are_in_one_place(self) -> None:
        # Each once, at the top: the place is to be found by eye in the game,
        # and whoever moves the box changes a number there and nothing else.
        for name in (
            "ROW_END",
            "ROW_DY",
            "CAPTION_GAP",
            "CAPTION_DY",
            "CAPTION_SIZE",
            "CAPTION_COLOUR",
        ):
            with self.subTest(name):
                self.assertEqual(
                    len(re.findall(rf"^var {name} = ", self.source, re.MULTILINE)), 1
                )
        head = self.source[: self.source.index("\nfunction ")]
        self.assertIn("var ROW_END = ", head)

    def test_the_screens_root_keeps_its_own_handlers(self) -> None:
        # The movie's _root is the tactics screen's: a handler set on it
        # would take the place of the game's.
        self.assertNotRegex(self.source, r"_root\.on[A-Z]\w* *=")
        self.assertIn("this.onEnterFrame = function()", self.source)


class Rect(unittest.TestCase):
    def test_bits_are_enough_for_the_largest_signed_value(self) -> None:
        for w, h in ((1, 1), (1280, 720), (1920, 1080), (4096, 4096)):
            values, _ = read_rect(movie.rect(w, h), 0)
            self.assertEqual(values, [0, w * 20, 0, h * 20])


if __name__ == "__main__":
    unittest.main()
