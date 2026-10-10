"""tools/reticles.py on a made-up game: what it changes in a file and what it leaves.

The game's files are not here to test with, and are not needed: each kind is
made up small in the game's form (an archive, a DXT5 texture, a material
object, a model's strings, a data table), and the tool's copy of it is read
back. The table is read by DAOLib's own reader too, where the submodule is
there. And the names the tool gives its files are held to the two other
places that have them: what the build copies into the plugin's override
subfolder, and what the plugin looks for and places.
"""

from __future__ import annotations

import math
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import reticles
from reticles import Unexpected

DAOLIB_TOOLS = REPO / "extern" / "DAOLib" / "tools"


def erf(files: dict[str, bytes]) -> bytes:
    """An ERF V2.0 archive of these files."""
    out = "ERF V2.0".encode("utf-16-le")
    out += struct.pack("<IIII", len(files), 2009, 300, 0xFFFFFFFF)
    at = len(out) + 72 * len(files)
    for name, data in files.items():
        out += name.encode("utf-16-le").ljust(64, b"\0") + struct.pack(
            "<II", at, len(data)
        )
        at += len(data)
    return out + b"".join(files.values())


def colour(r: int, g: int, b: int) -> int:
    """Eight bits each, as a block has a colour: 5, 6 and 5."""
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def dds(width: int, height: int, blocks: list[bytes], levels: int = 0) -> bytes:
    head = bytearray(128)
    head[:4] = b"DDS "
    struct.pack_into("<IIII", head, 4, 124, 0x81007, height, width)
    struct.pack_into("<I", head, 28, levels)
    head[84:88] = b"DXT5"
    struct.pack_into("<I", head, 108, 0x1000)
    return bytes(head) + b"".join(blocks)


OPAQUE, CLEAR = b"\xff\xff" + bytes(6), bytes(8)


def block(first: int, second: int, alpha: bytes = OPAQUE) -> bytes:
    """A block by hand: its texels' choices of colour the four in turn."""
    return alpha + struct.pack("<HH", first, second) + bytes([0x1B, 0xE4, 0x55, 0xAA])


# The party's ring's soft edge, body and highlight, and the marker's body
# (the tool's head has them measured); and white, the marker's heart.
EDGE, BODY, HIGHLIGHT = (99, 63, 9), (140, 77, 4), (206, 162, 74)
MARKER, WHITE = (245, 204, 106), (255, 255, 255)
GOLD, PALE = colour(*BODY), colour(*HIGHLIGHT)


def art(across: int, texel_at) -> bytes:
    """A texture of one level from a texel for each place, through the
    tool's own blocks (which the cases below hold to a block by hand)."""
    blocks = [
        reticles.block([texel_at(left + i % 4, top + i // 4) for i in range(16)])
        for top in range(0, across, 4)
        for left in range(0, across, 4)
    ]
    return dds(across, across, blocks)


def ring(across: int, inner: float, outer: float) -> bytes:
    """A gold ring, opaque from `inner` to `outer` of its middle."""

    def texel_at(x: int, y: int) -> tuple[int, int, int, int]:
        out = math.hypot(x + 0.5 - across / 2, y + 0.5 - across / 2)
        return (*BODY, 255 if inner <= out < outer else 0)

    return art(across, texel_at)


MAO = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!-- Exported by Material Editor --><MaterialObject Name="{name}">
\t<Material Name="GeneralGui.mat"></Material>
\t<DefaultSemantic Name="Default"></DefaultSemantic>
\t<Texture Name="mml_tDiffuse" ResName="{texture}"></Texture>
</MaterialObject>
"""


def mmh(name: str, *materials: str) -> bytes:
    """What of a model's hierarchy the tool reads: its strings, each once."""
    out = b"GFF V4.0PC  MMH V0.1" + bytes(range(40))
    for text in (*materials, "SelectionReticules.mal", f"{name}.mmh", f"{name}.msh"):
        out += reticles.gff_string(text)
        out += b"\xff" * (-len(out) % 4)
    return out


def ours(name: str, *materials: str) -> bytes:
    """A model as the tool leaves it: our names, and the game's mesh's."""
    game_s = name.replace("tb_gld", "sr_pty").replace("tb_red", "sr_pty")
    game_s = game_s.replace("tb_mov", "sr_mov")
    return mmh(name, *materials).replace(
        reticles.gff_string(f"{name}.msh"), reticles.gff_string(f"{game_s}.msh")
    )


# A table of the game's shape, small: the cells of four bytes in the columns'
# order, and after them the cells of one, as vfx_base.gda has its own.
COLUMNS = [
    ("ID", 5),
    ("Label", 14),
    ("AlignToGround", 0),
    ("ModelName", 14),
    ("ModelName1", 14),
    ("Size1", 8),
    ("ModelName2", 14),
    ("ModelName3", 14),
    ("BlendTreeName", 14),
    ("Tint", 14),
    ("LoadGlobal", 5),
]
RING = [30005, "Party Member", 1, "SR_PTY", "SR_PTY", 0.35, "SR_PTY_M", "SR_PTY_L"]
RING += ["SR_PTY", "0", 1]
MOVE = [30000, "Movement Indicator", 1, "SR_MOV", "SR_MOV", 0.0, "", "", "SR_MOV"]
MOVE += ["0", 1]
OTHER = [30012, "Enemy Targeted", 0, "SR_EMYt", "SR_EMYt", 0.4, "", "", "SR_EMYt"]
OTHER += ["0", 1]


def gda(rows: list[list[object]]) -> bytes:
    """A data table of COLUMNS with these rows, as the game's files lie."""
    offsets, wide, narrow = [], 0, 4 * sum(kind != 0 for _, kind in COLUMNS)
    for _, kind in COLUMNS:
        if kind == 0:
            offsets.append(narrow)
            narrow += 1
        else:
            offsets.append(wide)
            wide += 4
    row_size = narrow + -narrow % 4
    fields = struct.pack("<III", 10002, 1 | 0xC000 << 16, 0)
    fields += struct.pack("<III", 10003, 2 | 0xC000 << 16, 4)
    fields += struct.pack("<III", 10001, 4, 0) + struct.pack("<III", 10999, 0, 4)
    for i, (_, kind) in enumerate(COLUMNS):
        fields += struct.pack("<III", 10005 + i, kind, offsets[i])
    start = 28 + 16 * 3 + len(fields)
    head = b"GFF V4.0PC  G2DAV0.2" + struct.pack("<II", 3, start)
    head += struct.pack("<4sIII", b"gtop", 2, 76, 8)
    head += struct.pack("<4sIII", b"colm", 2, 100, 8)
    head += struct.pack("<4sIII", b"rows", len(COLUMNS), 124, row_size)
    columns = struct.pack("<I", len(COLUMNS))
    for name, kind in COLUMNS:
        columns += struct.pack("<IB", reticles.column_hash(name), kind) + b"\xff" * 3
    top = struct.pack("<II", 8, 8 + len(columns))
    strings = b""
    strings_at = 8 + len(columns) + 4 + row_size * len(rows)
    body = struct.pack("<I", len(rows))
    for row in rows:
        made = bytearray(b"\xff" * row_size)
        for (_, kind), offset, value in zip(COLUMNS, offsets, row, strict=True):
            if kind == 14 and value:
                strings += b"\xff" * (-len(strings) % 4)
                struct.pack_into("<I", made, offset, strings_at + len(strings))
                strings += reticles.gff_string(str(value))
            elif kind != 14:
                struct.pack_into(reticles.NUMBERS[kind], made, offset, value)
        body += made
    return head + fields + top + columns + body + strings


def game(root: Path, changed: dict[str, bytes] | None = None) -> Path:
    """A game's folder with what the tool reads, each file a small one of its
    kind; `changed` puts another archive in one's place, or beside them."""
    textures = {name for name, _, _ in reticles.TEXTURES.values()}
    materials = {
        name: reticles.TEXTURES[texture][0]
        for name, texture in reticles.MATERIALS.values()
    }
    files = {
        "data/textures.erf": erf({name: ring(16, 4, 7) for name in sorted(textures)}),
        "data/materialobjects.erf": erf(
            {
                f"{name}.mao": MAO.format(name=name, texture=texture).encode()
                for name, texture in materials.items()
            }
        ),
        "data/modelhierarchies.erf": erf({"sr_pty.mmh": b"the game's before a patch"}),
        "patch/patch001.erf": erf({"sr_pty.mmh": b"an earlier patch's"}),
        "patch/patch002.erf": erf(
            {
                "sr_pty.mmh": mmh("sr_pty", "Party"),
                "sr_pty_m.mmh": mmh("sr_pty_m", "Party_Med"),
                "sr_pty_l.mmh": mmh("sr_pty_l", "Party"),
                "sr_mov.mmh": mmh("sr_mov", "Move_icon", "Move_Indicator"),
            }
        ),
        "data/2da.erf": erf({"vfx_base.gda": gda([MOVE, RING, OTHER])}),
    }
    files.update(changed or {})
    for name, data in files.items():
        path = root / "packages" / "core" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return root


class Folders(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.out = self.root / "out"


class Archives(Folders):
    def test_the_latest_patch_comes_before_an_earlier_one_and_the_data(self) -> None:
        found = reticles.Game(game(self.root))
        self.assertEqual(found.read("sr_pty.mmh"), mmh("sr_pty", "Party"))
        self.assertEqual(found.where("sr_pty.mmh")[0].name, "patch002.erf")
        self.assertEqual(found.where("party.mao")[0].name, "materialobjects.erf")

    def test_a_name_the_game_has_not_is_none_and_reading_it_is_refused(self) -> None:
        found = reticles.Game(game(self.root))
        self.assertIsNone(found.where("tb_red.mmh"))
        with self.assertRaisesRegex(Unexpected, "no tb_red.mmh"):
            found.read("tb_red.mmh")

    def test_a_name_is_found_in_whatever_case_the_archive_has_it(self) -> None:
        path = self.root / "one.erf"
        path.write_bytes(erf({"SR_Pty.MMH": b"x", "other": b"yz"}))
        self.assertEqual(
            reticles.erf_index(path), {"sr_pty.mmh": (176, 1), "other": (177, 2)}
        )

    def test_what_is_no_archive_of_the_kind_is_refused(self) -> None:
        path = self.root / "one.erf"
        path.write_bytes(
            erf({}).replace("2.0".encode("utf-16-le"), "2.2".encode("utf-16-le"))
        )
        with self.assertRaisesRegex(Unexpected, "not an ERF V2.0"):
            reticles.erf_index(path)
        with self.assertRaisesRegex(Unexpected, "no archives"):
            reticles.Game(self.root / "nowhere")


class TheRed(unittest.TestCase):
    """The gold's ramp laid on the enemy ring's."""

    def test_body_goes_to_body_and_highlight_to_highlight(self) -> None:
        # The party's ring, whose brightest is its highlight: that to the
        # enemy ring's brightest, its body into the enemy's body (54 to 176,
        # the middle half of vfx_selbad.dds), its soft edge under it.
        top = reticles.brightness(*HIGHLIGHT)
        self.assertEqual(reticles.red(HIGHLIGHT, top), (206, 0, 0))
        self.assertEqual(reticles.red(BODY, top), (109, 0, 0))
        self.assertEqual(reticles.red(EDGE, top), (84, 0, 0))

    def test_the_markers_white_heart_is_the_reds_highlight(self) -> None:
        # The enemy's art has no white: the marker's brightest is its heart.
        top = reticles.brightness(*WHITE)
        self.assertEqual(reticles.red(WHITE, top), (206, 0, 0))
        self.assertEqual(reticles.red(MARKER, top), (166, 0, 0))

    def test_no_colour_has_blue_over_green_or_more_than_the_ramps_own(self) -> None:
        top = reticles.brightness(*HIGHLIGHT)
        for gold in (EDGE, BODY, HIGHLIGHT, MARKER, WHITE, (0, 0, 0)):
            r, g, b = reticles.red(gold, top)
            self.assertLessEqual(r, reticles.RED_TOP)
            self.assertGreaterEqual(g, b)
            self.assertEqual((g, b), (0, 0))

    def test_only_each_blocks_two_colours_change(self) -> None:
        before = dds(8, 4, [block(GOLD, PALE), block(PALE, GOLD)])
        after = reticles.redden(before)
        self.assertEqual(len(after), len(before))
        differ = {i for i in range(len(before)) if before[i] != after[i]}
        # No byte but a colour's, bytes 8 to 11 of a block's 16.
        self.assertLessEqual({(i - 128) % 16 for i in differ}, {8, 9, 10, 11})
        # The highlight the enemy's brightest, and the body's red under it.
        _, _, texels = reticles.decode(after)
        self.assertEqual(max(texels), (206, 0, 0, 255))
        self.assertEqual(min(texels)[1:], (0, 0, 255))
        self.assertLess(min(texels)[0], 120)

    def test_what_is_bright_under_nothing_does_not_set_the_highlight(self) -> None:
        # The game's art has a colour spread under its transparent texels.
        lit = dds(8, 4, [block(GOLD, PALE), block(0xFFFF, 0xFFFF, alpha=CLEAR)])
        self.assertEqual(max(reticles.decode(reticles.redden(lit))[2])[0], 206)
        with self.assertRaisesRegex(Unexpected, "nothing opaque"):
            reticles.redden(dds(4, 4, [block(GOLD, PALE, alpha=CLEAR)]))

    def test_every_level_of_a_texture_with_smaller_ones_is_reddened(self) -> None:
        # 8 by 8 and its three smaller: four blocks, then one each.
        before = dds(8, 8, [block(GOLD, PALE)] * 7, levels=4)
        after = reticles.redden(before)
        for at in range(128, len(before), 16):
            self.assertEqual(after[at : at + 16], after[128:144])
            self.assertNotEqual(after[at + 8 : at + 12], before[at + 8 : at + 12])

    def test_a_texture_of_another_kind_or_length_is_refused(self) -> None:
        whole = dds(8, 4, [block(GOLD, PALE)] * 2)
        with self.assertRaisesRegex(Unexpected, "not a DXT5"):
            reticles.redden(whole.replace(b"DXT5", b"DXT1"))
        with self.assertRaisesRegex(Unexpected, "is 160 bytes, not 144"):
            reticles.redden(whole[:-16])


class Blocks(unittest.TestCase):
    """A texture read into texels, and texels made a texture again."""

    def test_a_block_by_hand_is_read_as_its_colours_and_alphas_span(self) -> None:
        # Alphas 200 and 100, each texel's choice the next of the eight.
        choices = sum((i % 8) << 3 * i for i in range(16)).to_bytes(6, "little")
        _, _, texels = reticles.decode(
            dds(4, 4, [block(GOLD, PALE, alpha=bytes([200, 100]) + choices)])
        )
        body, pale = reticles.eight(GOLD), reticles.eight(PALE)
        third = tuple((2 * a + b) // 3 for a, b in zip(body, pale, strict=True))
        # The colours' choices, 0x1B: the second, the third, the first, ...
        self.assertEqual(
            [t[:3] for t in texels[:3]],
            [(*reticles.colours(GOLD, PALE)[3],), third, pale],
        )
        self.assertEqual(texels[3][:3], body)
        self.assertEqual([t[3] for t in texels[:4]], [200, 100, 185, 171])

    def test_texels_made_a_block_come_back_as_they_were(self) -> None:
        # Two colours and two alphas a block holds exactly, bar its 5 and 6 bits.
        pale, body = reticles.eight(PALE), reticles.eight(GOLD)
        texels = [(*pale, 255) if i % 3 else (*body, 40) for i in range(16)]
        made = dds(4, 4, [reticles.block(texels)])
        self.assertEqual(reticles.decode(made)[2], texels)
        flat = [(*body, 77)] * 16
        self.assertEqual(reticles.decode(dds(4, 4, [reticles.block(flat)]))[2], flat)

    def test_a_shade_between_a_blocks_two_goes_to_the_nearest_it_spans(self) -> None:
        texels = [(v, 0, 0, v) for v in range(0, 256, 17)]
        back = reticles.decode(dds(4, 4, [reticles.block(texels)]))[2]
        for was, now in zip(texels, back, strict=True):
            self.assertLessEqual(
                abs(was[0] - now[0]), 43
            )  # a third of the way, at most half of it off
            self.assertLessEqual(abs(was[3] - now[3]), 19)  # a seventh
            self.assertEqual(now[1:3], (0, 0))

    def test_a_smaller_level_is_the_mean_a_colour_counting_as_it_is_opaque(
        self,
    ) -> None:
        four = [(200, 100, 0, 255), (0, 0, 0, 0), (100, 50, 0, 255), (9, 9, 9, 0)]
        self.assertEqual(reticles.halved(2, 2, four), (1, 1, [(150, 75, 0, 128)]))
        clear = [(8, 4, 0, 0), (0, 0, 0, 0), (4, 4, 0, 0), (0, 0, 0, 0)]
        self.assertEqual(reticles.halved(2, 2, clear), (1, 1, [(3, 2, 0, 0)]))

    def test_a_texture_made_again_has_every_smaller_level(self) -> None:
        before = ring(16, 4, 7)
        width, height, texels = reticles.decode(before)
        made = reticles.encode(before, width, height, texels)
        # 16, 8, 4, 2 and 1 across: sixteen blocks, four, and three of one.
        self.assertEqual(len(made), 128 + 16 * (16 + 4 + 3))
        self.assertEqual(reticles.size(made), (16, 16))
        self.assertEqual(struct.unpack_from("<I", made, 28)[0], 5)
        self.assertEqual(struct.unpack_from("<I", made, 8)[0], 0x81007 | 0x20000)
        self.assertEqual(struct.unpack_from("<I", made, 108)[0], 0x1000 | 0x400008)
        self.assertEqual(reticles.decode(made)[2], texels)


class TheBand(unittest.TestCase):
    """A mark's ring with its band widened: twice as wide."""

    def setUp(self) -> None:
        # Opaque from 10 to 13 texels of its middle, 32 across.
        self.game = ring(32, 10, 13)

    def opaque(self, texels: list, out: float) -> bool:
        """Whether the ring is there, `out` texels right of its middle."""
        return texels[16 * 32 + 16 + int(out)][3] > 128

    def test_the_band_widens_inward_and_the_ring_is_as_large_as_it_was(self) -> None:
        _, _, before = reticles.decode(self.game)
        _, _, after = reticles.widened(self.game)
        # Three texels of band are six, from 7 to 13.
        self.assertEqual(
            [self.opaque(before, r) for r in (6, 8, 11, 12, 14)],
            [False, False, True, True, False],
        )
        self.assertEqual(
            [self.opaque(after, r) for r in (5, 8, 11, 12, 14)],
            [False, True, True, True, False],
        )
        # And its colour is the ring's.
        self.assertEqual(after[16 * 32 + 16 + 8][:3], reticles.eight(GOLD))
        widths = [sum(t[3] > 128 for t in texels) for texels in (before, after)]
        self.assertAlmostEqual(
            widths[1] / widths[0], (13**2 - 7**2) / (13**2 - 10**2), delta=0.15
        )

    def test_what_is_no_marks_keeps_the_games_blocks_in_red(self) -> None:
        self.assertEqual(
            reticles.texture(self.game, True, False), reticles.redden(self.game)
        )

    def test_widened_it_is_made_again_with_smaller_levels_red_or_gold(self) -> None:
        gold, red = (reticles.texture(self.game, r, True) for r in (False, True))
        for made in (gold, red):
            self.assertEqual(reticles.size(made), (32, 32))
            self.assertEqual(struct.unpack_from("<I", made, 28)[0], 6)
        # The texels as they were widened, an alpha to the nearest of a
        # block's eight.
        wanted = reticles.widened(self.game)[2]
        for was, now in zip(wanted, reticles.decode(gold)[2], strict=True):
            self.assertEqual(now[:3], was[:3])
            self.assertLessEqual(abs(now[3] - was[3]), 19)
        # The ring's one gold is its brightest: the enemy's brightest red.
        self.assertEqual(
            {t[:3] for t in reticles.decode(red)[2] if t[3]}, {(206, 0, 0)}
        )

    def test_what_is_no_ring_is_refused(self) -> None:
        with self.assertRaisesRegex(Unexpected, "no ring in it"):
            reticles.widened(ring(16, 20, 21))
        with self.assertRaisesRegex(Unexpected, "not square"):
            reticles.widened(dds(8, 4, [block(GOLD, PALE)] * 2))


class Materials(unittest.TestCase):
    def test_the_games_object_under_our_name_names_our_texture(self) -> None:
        theirs = MAO.format(name="party", texture="vfx_selparty.dds").encode()
        self.assertEqual(
            reticles.material(theirs, "tbred").decode(),
            MAO.format(name="tbred", texture="tb_red_selparty.dds"),
        )
        self.assertEqual(
            reticles.material(theirs, "tbgld").decode(),
            MAO.format(name="tbgld", texture="tb_gold_selparty.dds"),
        )

    def test_one_that_names_another_texture_is_refused(self) -> None:
        theirs = MAO.format(name="party", texture="vfx_selparty2.dds")
        with self.assertRaisesRegex(Unexpected, "vfx_selparty.dds.* 0 times"):
            reticles.material(theirs.encode(), "tbred")


class Models(unittest.TestCase):
    def test_the_names_change_where_they_lie_and_nothing_else(self) -> None:
        before = mmh("sr_mov", "Move_icon", "Move_Indicator")
        after = reticles.model(before, "tb_mov")
        self.assertEqual(after, ours("tb_mov", "tbred_arw", "tbred_movemark"))
        self.assertEqual(len(after), len(before))
        ring_s = mmh("sr_pty", "Party")
        self.assertEqual(reticles.model(ring_s, "tb_red"), ours("tb_red", "tbred"))
        self.assertEqual(reticles.model(ring_s, "tb_gld"), ours("tb_gld", "tbgld"))

    def test_every_name_of_ours_is_as_long_as_the_games(self) -> None:
        for mine, (theirs, _) in (
            *reticles.MATERIALS.items(),
            *reticles.MODELS.items(),
        ):
            self.assertEqual(len(mine), len(theirs), mine)
        for _, materials in reticles.MODELS.values():
            for theirs, mine in materials.items():
                self.assertEqual(reticles.MATERIALS[mine][0], theirs)

    def test_a_name_not_there_once_as_a_whole_string_is_refused(self) -> None:
        with self.assertRaisesRegex(Unexpected, "has party 0 times"):
            reticles.model(mmh("sr_pty", "Party_Med"), "tb_red")
        twice = mmh("sr_pty", "Party") + reticles.gff_string("party")
        with self.assertRaisesRegex(Unexpected, "has party 2 times"):
            reticles.model(twice, "tb_red")

    def test_a_name_of_another_length_is_not_written(self) -> None:
        with self.assertRaises(ValueError):
            reticles.rename(mmh("sr_pty", "Party"), {"party": "longer"}, "a model")


class Tables(unittest.TestCase):
    def setUp(self) -> None:
        self.game = gda([MOVE, RING, OTHER])
        self.made = reticles.table(self.game)

    def row(self, data: bytes, number: int) -> list[object]:
        values = reticles.Table(data).values(number)
        return [values[reticles.column_hash(name)] for name, _ in COLUMNS]

    def test_the_made_up_table_reads_as_it_was_written(self) -> None:
        for row in (MOVE, RING, OTHER):
            read = self.row(self.game, row[0])
            self.assertEqual(read[:5] + read[6:], row[:5] + row[6:])
            self.assertAlmostEqual(read[5], row[5])

    def test_our_rows_are_the_games_under_new_numbers_naming_our_models(self) -> None:
        self.assertEqual(reticles.Table(self.made).count, 3)
        red, marker, gold = (self.row(self.made, 1000000 + i) for i in range(3))
        self.assertEqual(red[:5], [1000000, "Party Member", 1, "tb_red", "tb_red"])
        self.assertEqual(red[6:], ["tb_red_m", "tb_red_l", "SR_PTY", "0", 1])
        self.assertEqual(gold[:5], [1000002, "Party Member", 1, "tb_gld", "tb_gld"])
        self.assertEqual(gold[6:], ["tb_gld_m", "tb_gld_l", "SR_PTY", "0", 1])
        self.assertEqual(red[5], self.row(self.game, 30005)[5])
        # The marker's animation is the game's still, by the game's name.
        self.assertEqual(
            marker[:5], [1000001, "Movement Indicator", 1, "tb_mov", "tb_mov"]
        )
        self.assertEqual(marker[6:], ["", "", "SR_MOV", "0", 1])

    def test_all_before_the_rows_is_the_games_and_a_string_is_kept_once(self) -> None:
        rows_at = reticles.Table(self.game).rows_at
        self.assertEqual(self.made[:rows_at], self.game[:rows_at])
        for text in ("tb_red", "0", "SR_MOV", "Party Member"):
            self.assertEqual(self.made.count(reticles.gff_string(text)), 1, text)

    def test_a_row_the_game_has_not_or_a_model_unknown_is_refused(self) -> None:
        with self.assertRaisesRegex(Unexpected, "no row 30006"):
            reticles.extension(self.game, {1000000: (30006, {})})
        with self.assertRaisesRegex(Unexpected, "row 30012 names a model SR_EMYt"):
            reticles.extension(self.game, {1000000: (30012, {"sr_pty": "tb_red"})})
        with self.assertRaisesRegex(Unexpected, "not a data table"):
            reticles.Table(mmh("sr_pty", "Party"))

    @unittest.skipUnless((DAOLIB_TOOLS / "gff4.py").exists(), "needs DAOLib's tools")
    def test_daolibs_reader_reads_our_rows_the_same(self) -> None:
        sys.path.insert(0, str(DAOLIB_TOOLS))
        self.addCleanup(sys.path.remove, str(DAOLIB_TOOLS))
        import gff4

        read = gff4.Gff4(self.made)
        self.assertEqual(read.type, "G2DA")
        rows = []
        for template, at in read.list_of_structs(
            read.data_offset, read.fields(0)[10003]
        ):
            cells = read.fields(template)
            rows.append([read.value(at, cells[10005 + i]) for i in range(len(COLUMNS))])
        self.assertEqual(rows, [self.row(self.made, 1000000 + i) for i in range(3)])


class Build(Folders):
    def test_every_file_is_made_from_the_games(self) -> None:
        said = reticles.build(game(self.root), self.out)
        made = sorted(path.name for path in self.out.iterdir())
        self.assertEqual(made, sorted(reticles.outputs()))
        self.assertEqual(len(made), 20)
        self.assertIn("from sr_pty.mmh in packages", " ".join(said))
        self.assertEqual(
            (self.out / "tb_gld_m.mmh").read_bytes(), ours("tb_gld_m", "tbgld_med")
        )
        table = reticles.Table((self.out / "vfx_base_turnbased.gda").read_bytes())
        self.assertEqual(
            table.values(1000001)[reticles.column_hash("ModelName")], "tb_mov"
        )
        # The marks' rings are widened and made again, with smaller levels;
        # the marker's two are the game's blocks, as long as they were.
        before = len(ring(16, 4, 7))
        for name in reticles.outputs():
            if name.endswith(".dds"):
                longer = len((self.out / name).read_bytes()) > before
                self.assertEqual(longer, "selparty" in name, name)

    def test_with_one_file_not_as_expected_nothing_is_written(self) -> None:
        other = erf({"vfx_base.gda": gda([MOVE, OTHER])})
        with self.assertRaisesRegex(Unexpected, "no row 30005"):
            reticles.build(game(self.root, {"data/2da.erf": other}), self.out)
        self.assertFalse(self.out.exists())

    def test_a_name_of_ours_that_the_game_has_is_refused(self) -> None:
        theirs = erf({"tb_mov.mmh": b"the game's own"})
        with self.assertRaisesRegex(Unexpected, "has a tb_mov.mmh of its own"):
            reticles.build(game(self.root, {"data/anims.erf": theirs}), self.out)
        self.assertFalse(self.out.exists())


class HeldToTheRest(unittest.TestCase):
    """The tool's names, in the build and in the plugin."""

    def test_the_build_copies_every_file_the_tool_makes_where_the_game_reads(
        self,
    ) -> None:
        # By OVERRIDE, into the plugin's override subfolder, and none by
        # FILES, beside the dll, where the game does not look for them.
        cmake = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
        files, override = re.search(
            r"^    FILES(.*?)^    OVERRIDE(.*?)^\)", cmake, re.MULTILINE | re.DOTALL
        ).groups()
        made = [name for name in override.split() if name.startswith("ui/reticles/")]
        self.assertEqual(made, [f"ui/reticles/{name}" for name in reticles.outputs()])
        self.assertNotIn("ui/reticles/", files)
        # And each is in the repository (issue 106), with nothing else beside them.
        kept = sorted(path.name for path in (REPO / "ui" / "reticles").iterdir())
        self.assertEqual(kept, sorted(reticles.outputs()))
        # What else goes there is in the repository (a Veil Tear's picture
        # and its row of the portraits table, issue 103), and is there.
        for name in override.split():
            if name not in made:
                self.assertTrue((REPO / name).is_file(), name)

    def test_the_plugin_looks_for_every_file_and_places_the_three_rows(self) -> None:
        plugin = (REPO / "src" / "game" / "Preview.cpp").read_text(encoding="utf-8")
        looked_for = re.findall(r'L"([\w.]+\.(?:dds|mao|mmh|gda))"', plugin)
        self.assertEqual(sorted(looked_for), sorted(reticles.outputs()))
        for row, name, game_s in (
            (1000000, "RedRing", "sr_pty"),
            (1000001, "RedMarker", "sr_mov"),
            (1000002, "GoldRing", "sr_pty"),
        ):
            self.assertIn(f"k{name} = {row};", plugin)
            self.assertIn(f'k{name}Model = L"{reticles.ROWS[row][1][game_s]}";', plugin)


if __name__ == "__main__":
    unittest.main()
