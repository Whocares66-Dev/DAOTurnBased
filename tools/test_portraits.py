"""tools/portraits.py without the game: where the raw render is in a frame, which template a
row is spawned from, what a cell says, and the sweep's loop against a stand-in for the game."""

import argparse
import re
import sys
import tempfile
import unittest
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import portraits

ROOT = Path(__file__).resolve().parent.parent
MOVIE = ROOT / "ui" / "turnorder" / "frame_1" / "DoAction.as"
TABLE = ROOT / "src" / "core" / "PortraitTable.h"


def options(**given: object) -> argparse.Namespace:
    """The command line's options as parsed, with those given."""
    base = {
        "rows": None,
        "first": 0,
        "last": 10**9,
        "plot": False,
        "fights": False,
        "awakening": False,
        "from_table": False,
        "row": None,
        "distance": None,
        "z": None,
        "x": None,
        "pitch": None,
        "rotation": None,
        "kind": None,
    }
    return argparse.Namespace(**{**base, **given})


class Frame(unittest.TestCase):
    def test_the_raw_render_is_where_it_was_measured_in_play(self) -> None:
        # 2026-10-04, a 1600 x 1000 frame: the square from about (529, 292) to (709, 470).
        box = portraits.raw_box(1600, 1000)
        for got, measured in zip(box, (529, 292, 709, 470)):
            self.assertLessEqual(abs(got - measured), 2)

    def test_it_follows_the_frame_as_the_stage_is_laid_over_it(self) -> None:
        # The stage's own size: the movie's points as they are.
        self.assertEqual(portraits.raw_box(1280, 720, inset=0), (422, 192, 568, 338))
        # Half as big again, and a wider frame: the stage is centred in it.
        self.assertEqual(portraits.raw_box(1920, 1080, inset=0), (633, 288, 852, 507))
        self.assertEqual(portraits.raw_box(2560, 1080, inset=0), (953, 288, 1172, 507))
        # A square, to a pixel, whatever the frame.
        left, top, right, bottom = portraits.raw_box(1600, 1000)
        self.assertLessEqual(abs((right - left) - (bottom - top)), 1)
        self.assertGreater(right - left, 170)

    def test_the_movies_own_numbers_are_the_tools(self) -> None:
        source = MOVIE.read_text(encoding="utf-8")
        for name in (
            "STAGE_W",
            "STAGE_H",
            "SHOW_X",
            "SHOW_Y",
            "SHOW_PAD",
            "SHOW_BIG",
            "SHOW_RAW",
        ):
            with self.subTest(name=name):
                found = re.search(rf"^var {name} = (\d+);", source, re.MULTILINE)
                self.assertIsNotNone(found, f"the movie has no {name}")
                self.assertEqual(int(found.group(1)), getattr(portraits, name))


class Templates(unittest.TestCase):
    def test_each_row_has_one_template_a_resource_name(self) -> None:
        seen = set()
        for row, (template, label, source) in portraits.TEMPLATES.items():
            with self.subTest(row=row):
                self.assertRegex(template, r"^[a-z0-9_]{1,32}$")
                self.assertNotIn(template, seen)
                seen.add(template)
                self.assertTrue(label)
                self.assertIn(source, ("core", "campaign", "plot", "fights"))
                self.assertTrue(1 < row < 90)
        self.assertEqual(list(portraits.TEMPLATES), sorted(portraits.TEMPLATES))

    def test_the_plain_core_ones_are_the_rows(self) -> None:
        for row, template in (
            (64, "dragonling"),
            (80, "drake"),
            (66, "high_dragon"),
            (49, "wolf"),
            (47, "ogre_default"),
        ):
            self.assertEqual(portraits.TEMPLATES[row][0], template)
            self.assertEqual(portraits.TEMPLATES[row][2], "core")

    def test_rows_nothing_in_the_game_wears_have_none(self) -> None:
        for row in (1, 19, 21, 43, 48, 56):
            self.assertIsNone(portraits.template_of(row))
        self.assertIsNone(portraits.template_of(0))

    def test_awakenings_rows_are_apart(self) -> None:
        self.assertFalse(set(portraits.AWAKENING) & set(portraits.TEMPLATES))
        self.assertTrue(all(row >= 20000 for row in portraits.AWAKENING))
        self.assertEqual(
            portraits.template_of(20014),
            ("armored_ogre", "(GXA) Armored Ogre", "awakening"),
        )

    def test_every_row_of_the_cores_framing_table_can_be_swept(self) -> None:
        # src/core/PortraitTable.h: a row found by eye is seen again by spawning its creature,
        # and its label is the row's number and the appearance table's own label for it.
        source = TABLE.read_text(encoding="utf-8")
        table = source[source.index("kPortraitTable{") :]
        found = re.findall(
            r'^    (?:FromRoot\()?Row\((\d+), "(\d+) ([^"]+)",', table, re.MULTILINE
        )
        rows = [int(row) for row, _, _ in found]
        self.assertGreaterEqual(len(rows), 4)
        self.assertIn(66, rows)
        for row, number, label in found:
            with self.subTest(row=row):
                self.assertEqual(row, number)
                self.assertEqual(portraits.template_of(int(row))[1], label)
        # Awakening's own are among them since issue 84.
        self.assertIn(20019, rows)

    def test_the_listing_names_every_row(self) -> None:
        text = portraits.listing()
        self.assertIn("   64  dragonling", text)
        self.assertIn("20014  armored_ogre", text)
        self.assertIn(
            "amgarrak    dao_prc_gib    The Golems of Amgarrak: apr_gib", text
        )
        self.assertEqual(
            len(text.splitlines()),
            len(portraits.TEMPLATES)
            + len(portraits.AWAKENING)
            + 1
            + 1
            + len(portraits.ADDONS),
        )


def archive(path: Path, magic: str, names: list[str]) -> None:
    """An archive of the game's with those entries and no contents: ERF V2.0, or V2.2
    with its flags saying its contents are enciphered, as the add-ins' are."""
    head = magic.encode("utf-16-le") + len(names).to_bytes(4, "little")
    if magic == "ERF V2.0":
        head += bytes(12)
        tail = bytes(8)  # an entry's place and size
    else:
        head += bytes(12) + (0x20).to_bytes(4, "little") + bytes(4) + bytes(16)
        tail = bytes(12)  # its place, its size packed and unpacked
    body = b"".join(name.encode("utf-16-le").ljust(64, b"\0") + tail for name in names)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + body)


class Addons(unittest.TestCase):
    """The other add-ins: swept by their templates, whose names their archives list in plain."""

    def test_an_archives_list_is_read_whichever_version_it_is(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            old = Path(folder) / "old.erf"
            new = Path(folder) / "new.erf"
            archive(old, "ERF V2.0", ["Dragonling.utc", "apr_base.gda"])
            archive(
                new,
                "ERF V2.2",
                ["gib000cr_harvester.utc", "gib000cr_golem_runic.utc", "x.dlg"],
            )
            self.assertEqual(
                portraits.erf_names(old), ["dragonling.utc", "apr_base.gda"]
            )
            self.assertEqual(
                portraits.erf_names(new),
                ["gib000cr_harvester.utc", "gib000cr_golem_runic.utc", "x.dlg"],
            )
            other = Path(folder) / "other.erf"
            other.write_bytes(b"not an archive of the game's at all")
            self.assertEqual(portraits.erf_names(other), [])

    def test_an_addins_templates_are_its_creature_archives_names(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            addin = Path(folder) / "Addins" / "dao_prc_gib"
            archive(
                addin / "module" / "data" / "designercreatures.erf",
                "ERF V2.2",
                ["gib000cr_harvester.utc", "gib000cr_adventurer_a.utc"],
            )
            archive(
                addin / "module" / "data" / "coreep1" / "designercreatures.erf",
                "ERF V2.2",
                ["armored_ogre.utc", "gib000cr_harvester.utc"],
            )
            archive(
                addin / "core" / "data" / "2da.erf",
                "ERF V2.2",
                ["apr_gib.gda", "stray.utc"],
            )
            self.assertEqual(
                portraits.addon_templates(Path(folder), "amgarrak"),
                ["armored_ogre", "gib000cr_adventurer_a", "gib000cr_harvester"],
            )
            self.assertEqual(portraits.addon_templates(Path(folder), "witch"), [])

    def test_every_addin_has_its_folder_and_what_it_adds(self) -> None:
        folders = [folder for folder, _, _ in portraits.ADDONS.values()]
        self.assertEqual(len(set(folders)), len(folders))
        for name, (folder, title, tables) in portraits.ADDONS.items():
            with self.subTest(addon=name):
                self.assertTrue(folder.startswith("dao_prc_"))
                self.assertTrue(title)
                self.assertTrue(tables.startswith(("apr_", "no table")))

    def test_an_addin_is_swept_by_template_and_each_appearance_pictured_once(
        self,
    ) -> None:
        game = Game()
        harvester = {**ANSWER, "appearance": 30001, "label": "Harvester"}
        game.portraits = [
            harvester,
            {**ANSWER, "appearance": 15, "label": "Human"},
            harvester,
        ]
        sweep = sweep_of(game)
        sweep.run(
            [], ["gib_harvester", "gib_adventurer", "gib_harvester_b"], "amgarrak"
        )
        self.assertEqual([r["row"] for r in sweep.records], [30001, 15, 30001])
        self.assertEqual(sweep.records[0]["template_from"], "amgarrak")
        self.assertEqual(sweep.records[0]["label"], "Harvester")
        # The third is of an appearance already pictured: said, with no cell and no picture.
        self.assertEqual(sweep.records[2]["same_as"], "gib_harvester")
        self.assertEqual(len(sweep.cells), 2)
        self.assertEqual(game.named().count("screenshot"), 2)
        self.assertEqual(game.named().count("kill"), 3)
        self.assertEqual(sweep.pictured, {30001: "gib_harvester", 15: "gib_adventurer"})
        spawned = [
            call[1]["template"] for call in game.calls if call[0] == "stage_spawn"
        ]
        self.assertEqual(
            spawned, ["gib_harvester", "gib_adventurer", "gib_harvester_b"]
        )


class Options(unittest.TestCase):
    def test_a_sweep_is_every_row_but_the_plots_and_awakenings(self) -> None:
        rows = portraits.chosen_rows(options())
        self.assertIn(64, rows)
        self.assertNotIn(83, rows)  # the follower Shale's template
        self.assertNotIn(20014, rows)
        self.assertEqual(rows, sorted(rows))
        self.assertIn(83, portraits.chosen_rows(options(plot=True)))
        self.assertIn(20014, portraits.chosen_rows(options(awakening=True)))
        # Nor the one whose creature stays hostile and begins a fight, unless asked for.
        self.assertNotIn(65, rows)
        self.assertIn(65, portraits.chosen_rows(options(fights=True)))
        self.assertNotIn(83, portraits.chosen_rows(options(fights=True)))
        self.assertEqual(portraits.chosen_rows(options(rows=[65])), [65])
        self.assertEqual(portraits.chosen_rows(options(row=65)), [65])

    def test_the_trees_table_is_read_as_cameras(self) -> None:
        # src/core/PortraitTable.h, for a sweep by the table as the tree has it (--from-table).
        rows = portraits.table_rows()
        self.assertEqual(
            rows[64],
            {
                "distance": 0.9,
                "rotation": 35.0,
                "pitch": 0.0,
                "xAdjust": -0.5,
                "zAdjust": 0.45,
            },
        )
        self.assertEqual(
            rows[47],
            {
                "distance": 2.47,
                "rotation": 23.5,
                "pitch": -7.5,
                "xAdjust": 0.25,
                "zAdjust": -0.15,
            },
        )
        # Every appearance's row, and no default or the game's own Default row among them.
        source = TABLE.read_text(encoding="utf-8")
        table = source[source.index("kPortraitTable{") :]
        self.assertEqual(
            len(rows),
            len(re.findall(r"^    (?:FromRoot\()?Row\(", table, re.MULTILINE)),
        )
        self.assertNotIn(0, rows)

    def test_rows_and_ranges(self) -> None:
        self.assertEqual(
            portraits.chosen_rows(options(rows=[80, 64, 83])), [80, 64, 83]
        )
        self.assertEqual(
            portraits.chosen_rows(options(first=76, last=80)), [76, 77, 78, 79, 80]
        )
        self.assertEqual(portraits.chosen_rows(options(rows=[64, 80], first=70)), [80])
        self.assertEqual(portraits.chosen_rows(options(row=83, rows=[1, 2])), [83])

    def test_a_camera_to_try_is_the_tools_arguments(self) -> None:
        self.assertEqual(portraits.camera_arguments(options()), {})
        self.assertEqual(
            portraits.camera_arguments(options(distance=1.4, z=0.9)),
            {"distance": 1.4, "zAdjust": 0.9},
        )
        self.assertEqual(
            portraits.camera_arguments(
                options(x=0.0, pitch=-7.5, rotation=23.5, kind="upright")
            ),
            {"xAdjust": 0.0, "pitch": -7.5, "rotation": 23.5, "kind": "upright"},
        )

    def test_a_trials_name(self) -> None:
        self.assertEqual(
            portraits.trial_name(80, {"distance": 1.4, "zAdjust": 0.9}),
            "row-80-d1.4-z0.9",
        )
        self.assertEqual(portraits.trial_name(64, {}), "row-64")
        self.assertEqual(
            portraits.trial_name(64, {"kind": "on_all_fours", "distance": 2.0}),
            "row-64-d2-on_all_fours",
        )

    def test_the_place_to_spawn_is_ahead_of_the_party(self) -> None:
        hero = {"position": {"x": 10.0, "y": 20.0, "z": 3.0}, "facing": 90.0}
        point = portraits.spawn_point(hero, 4.0)
        self.assertAlmostEqual(point["x"], 10.0, places=4)
        self.assertAlmostEqual(point["y"], 24.0, places=4)
        self.assertEqual(point["z"], 3.0)
        still = portraits.spawn_point({"position": {"x": 1.0, "y": 2.0, "z": 0.0}}, 4.0)
        self.assertAlmostEqual(still["x"], 5.0)


ANSWER = {
    "id": "0x5001",
    "appearance": 80,
    "label": "Dragon, Drake",
    "kind": "on_all_fours",
    "framed_by": "row",
    "row": "80 Dragon, Drake",
    "source": "rendered",
    "lack": "",
    "shown": True,
    "extent": 2.38,
    "camera": {
        "given": True,
        "pitch": 0.0,
        "rotation": 0.0,
        "distance": 1.4,
        "zAdjust": 0.9,
        "xAdjust": 0.06,
    },
    "problem": "",
}


class Words(unittest.TestCase):
    def test_a_cell_says_its_row_what_framed_it_and_the_camera(self) -> None:
        record = portraits.record_of(80, "drake", "core", ANSWER)
        self.assertEqual(record["answer"], ANSWER)
        self.assertEqual(
            portraits.caption(record),
            ["80 Dragon, Drake", "row, 2.38 m", "d1.40 z0.90 x0.06"],
        )
        turned = portraits.record_of(
            47,
            "ogre_default",
            "core",
            {
                **ANSWER,
                "camera": {
                    "distance": 2.47,
                    "zAdjust": -0.15,
                    "xAdjust": 0.25,
                    "pitch": -7.5,
                    "rotation": 23.5,
                },
            },
        )
        self.assertEqual(portraits.caption(turned)[2], "d2.47 z-0.15 x0.25 p-8 r24")
        self.assertEqual(record["table_row"], "80 Dragon, Drake")
        # A kind's default says whose.
        by_default = portraits.record_of(
            34,
            "dog_mabari_1",
            "core",
            {**ANSWER, "framed_by": "default", "row": "a body on all fours"},
        )
        self.assertEqual(
            portraits.caption(by_default)[1], "default on all fours, 2.38 m"
        )

    def test_a_cell_with_no_render_says_why(self) -> None:
        headless = portraits.record_of(
            14,
            "wisp",
            "core",
            {"label": "Wisp", "lack": "no_head_node", "source": "none"},
        )
        self.assertEqual(portraits.caption(headless), ["14 Wisp", "no_head_node", ""])
        made = portraits.record_of(
            49,
            "wolf",
            "core",
            {
                "label": "Wolf",
                "source": "the portraits table's",
                "extent": 1.02,
                "camera": {"distance": 0.0},
            },
        )
        self.assertEqual(
            portraits.caption(made), ["49 Wolf", "the portraits table's, 1.02 m", ""]
        )
        self.assertEqual(
            portraits.caption(
                {
                    "row": 60,
                    "template": "ntb210cr_hermit",
                    "error": "not spawned: no creature",
                }
            ),
            ["60 ntb210cr_hermit", "error", "not spawned: no creature"],
        )

    def test_a_camera_is_printed_as_a_row_of_the_table(self) -> None:
        # src/core/PortraitTable.h's own columns: the row and its label, then
        # distance, rotation, pitch, aside, up, and where it came from.
        record = {
            "row": 80,
            "label": "Dragon, Drake",
            "pitch": 0.0,
            "rotation": 35.0,
            "distance": 1.5,
            "zAdjust": 0.8,
            "xAdjust": -0.85,
        }
        self.assertEqual(
            portraits.table_line(record, "at the camp", today=date(2026, 10, 4)),
            '    Row(80, "80 Dragon, Drake", 1.5f, 35.0f, 0.0f, -0.85f, 0.8f,\n'
            '        "by eye at the camp, 2026-10-04: seen as it is"),',
        )
        # From the command line's numbers alone (--print-row): the label is the tool's own for the row.
        printed = portraits.printed_record(
            options(row=66, distance=6.0, rotation=35.0, x=-1.7, z=-0.8)
        )
        self.assertEqual(
            portraits.table_line(
                printed,
                "above Haven",
                "the aside interpolated between -1.5 and -1.9",
                today=date(2026, 10, 4),
            ),
            '    Row(66, "66 Dragon, High", 6.0f, 35.0f, 0.0f, -1.7f, -0.8f,\n'
            '        "by eye above Haven, 2026-10-04: the aside interpolated between -1.5 and -1.9"),',
        )
        # The table's own rows are written this way.
        source = TABLE.read_text(encoding="utf-8")
        self.assertIn(
            '    Row(80, "80 Dragon, Drake", 1.5f, 35.0f, 0.0f, -0.85f, 0.8f,\n', source
        )
        self.assertIn(
            '    Row(66, "66 Dragon, High", 6.0f, 35.0f, 0.0f, -1.7f, -0.8f,\n', source
        )
        self.assertEqual(portraits.cpp_float(6), "6.0f")
        self.assertEqual(portraits.cpp_float(-7.5), "-7.5f")
        self.assertEqual(portraits.cpp_float(None), "0.0f")
        self.assertEqual(portraits.cpp_float(0.02), "0.02f")

    def test_a_sheet_is_six_across(self) -> None:
        self.assertEqual(portraits.sheet_size(30), (6 * 186, 5 * 224))
        self.assertEqual(portraits.sheet_size(7), (6 * 186, 2 * 224))
        self.assertEqual(portraits.sheet_size(1), (6 * 186, 224))
        # A search's is as many across as its grid has a side.
        self.assertEqual(portraits.sheet_size(9, 3), (3 * 186, 3 * 224))

    def test_a_grid_is_round_its_start(self) -> None:
        self.assertEqual(portraits.grid(-1.7, 0.3, 3), [-2.0, -1.7, -1.4])
        self.assertEqual(portraits.grid(0.0, 0.1, 5), [-0.2, -0.1, 0.0, 0.1, 0.2])
        self.assertEqual(portraits.grid(0.5, 0.2, 1), [0.5])

    def test_a_searchs_cell_says_its_aside_and_up_first(self) -> None:
        record = portraits.record_of(
            66,
            "high_dragon",
            "core",
            {
                "label": "Dragon, High",
                "source": "rendered",
                "camera": {
                    "distance": 6.0,
                    "zAdjust": -0.8,
                    "xAdjust": -1.7,
                    "pitch": 0.0,
                    "rotation": 35.0,
                },
            },
        )
        self.assertEqual(
            portraits.search_caption(record),
            ["x -1.7  z -0.8", "d6.00 p0 r35", "66 Dragon, High"],
        )


class Game:
    """A stand-in for the game behind Eluvian: what each tool is asked, and scripted answers."""

    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []
        self.spawn: dict = {"creature": {"id": "0x5001"}, "moved": 0.2}
        self.modes: list[str] = []  # what health answers, in turn; explore after them
        self.portraits: list[dict] = [ANSWER]
        self.shot: dict = {"file": "frame.jpg"}

    def tool(
        self, name: str, arguments: dict | None = None, timeout: float = 60
    ) -> dict:
        arguments = arguments or {}
        self.calls.append((name, arguments))
        if name == "stage_spawn":
            return self.spawn
        if name == "turnbased_portrait" and arguments.get("show"):
            return (
                self.portraits.pop(0) if len(self.portraits) > 1 else self.portraits[0]
            )
        if name == "screenshot":
            return self.shot
        if name == "health":
            return {"mode": self.modes.pop(0) if self.modes else "explore"}
        return {}

    def named(self) -> list[str]:
        """The calls in order, each as a word or two."""
        out = []
        for name, arguments in self.calls:
            if name.startswith("stage_"):
                out.append(name.removeprefix("stage_"))
            elif name == "turnbased_portrait":
                out.append("show" if arguments.get("show") else "take down")
            else:
                out.append(name)
        return out


def sweep_of(game: Game, **given: object) -> portraits.Sweep:
    waits: list[float] = []
    sweep = portraits.Sweep(
        game.tool,
        {"x": 1.0, "y": 2.0, "z": 3.0},
        sleep=waits.append,
        picture=lambda file: (f"crop of {file}", f"cell of {file}"),
        say=lambda line: None,
        **given,
    )
    sweep.waits = waits
    return sweep


class Loop(unittest.TestCase):
    def test_a_row_is_spawned_made_friendly_shown_pictured_taken_down_and_killed(
        self,
    ) -> None:
        game = Game()
        sweep = sweep_of(game)
        record = sweep.row(80)
        self.assertEqual(
            game.named(), ["spawn", "group", "show", "screenshot", "take down", "kill"]
        )
        self.assertEqual(
            game.calls[0],
            (
                "stage_spawn",
                {"template": "drake", "point": {"x": 1.0, "y": 2.0, "z": 3.0}},
            ),
        )
        self.assertEqual(game.calls[1], ("stage_group", {"id": "0x5001", "group": 2}))
        self.assertEqual(
            game.calls[2][1], {"id": "0x5001", "show": True, "seconds": 30}
        )
        self.assertEqual(game.calls[3][1], {"source": "game", "image": False})
        self.assertEqual(game.calls[4][1], {"id": "0x5001", "show": False})
        self.assertEqual(game.calls[5], ("stage_kill", {"id": "0x5001"}))
        self.assertEqual(record["row"], 80)
        self.assertEqual(record["template"], "drake")
        self.assertEqual(record["frame"], "frame.jpg")
        self.assertEqual(record["moved"], 0.2)
        self.assertEqual(sweep.crops[80], "crop of frame.jpg")
        self.assertEqual(sweep.cells, [(record, "cell of frame.jpg")])
        # It waits for the creature to be set up, for the movie to draw, and for the picture to be let go.
        self.assertGreaterEqual(sum(sweep.waits), 3.0)

    def test_a_creature_not_set_up_yet_is_asked_again(self) -> None:
        game = Game()
        game.portraits = [{"lack": "no_head_node"}, {"lack": "no_head_node"}, ANSWER]
        sweep_of(game).row(80)
        self.assertEqual(game.named().count("show"), 3)
        # One that never has a head is given up after a few tries, and said.
        game = Game()
        game.portraits = [{"lack": "no_head_node", "label": "Wisp", "source": "none"}]
        sweep = sweep_of(game)
        record = sweep.row(14)
        self.assertEqual(game.named().count("show"), 1 + portraits.Sweep.HEAD_TRIES)
        self.assertEqual(record["lack"], "no_head_node")
        # A quarter of a minute: a werewolf's model was once later than 4.5 s. And
        # said, since the tool cannot tell a late model from one with no such node.
        self.assertGreaterEqual(record["waited"], 15.0)
        self.assertIn("not a row known to have none", record["problem"])
        self.assertEqual(
            portraits.caption(record)[1], f"no_head_node, {record['waited']:.0f} s"
        )

    def test_a_row_with_no_head_node_is_done_once_it_is_rendered_from_its_root(
        self,
    ) -> None:
        # The plugin renders one about its root where a camera is given or the
        # table has a row from the root: its lack stays, and it is a picture.
        rooted = {
            **ANSWER,
            "lack": "no_head_node",
            "rendered": True,
            "aimed_at": "scene_origin",
            "extent": 0.0,
        }
        game = Game()
        game.portraits = [rooted]
        sweep = sweep_of(game, camera={"distance": 1.0, "zAdjust": 0.6})
        record = sweep.row(33)
        self.assertEqual(game.named().count("show"), 1)
        self.assertEqual(game.named().count("screenshot"), 1)
        self.assertNotIn("waited", record)
        self.assertEqual(
            portraits.caption(record)[1], f"{ANSWER['framed_by']}, from its root"
        )
        self.assertTrue(
            portraits.table_line(
                record, "at the camp", today=date(2026, 10, 4)
            ).startswith('    FromRoot(Row(33, "33 ')
        )
        self.assertTrue(
            portraits.table_line(
                record, "at the camp", today=date(2026, 10, 4)
            ).endswith('")),')
        )
        # Another row's creature rendered so has a head that is late: asked on.
        game = Game()
        game.portraits = [rooted, rooted, ANSWER]
        record = sweep_of(game, camera={"distance": 1.0}).row(22)
        self.assertEqual(game.named().count("show"), 3)
        self.assertEqual(record["lack"], "")

    def test_a_row_known_to_have_no_head_node_is_not_waited_for(self) -> None:
        game = Game()
        game.portraits = [
            {"lack": "no_head_node", "label": "Deepstalker", "source": "none"}
        ]
        record = sweep_of(game).row(33)
        self.assertEqual(game.named().count("show"), 1 + portraits.Sweep.HEADLESS_TRIES)
        self.assertNotIn("waited", record)
        self.assertEqual(portraits.caption(record)[1], "no_head_node")
        self.assertLess(portraits.Sweep.HEADLESS_TRIES, portraits.Sweep.HEAD_TRIES)
        # The eleven of the sweeps of 2026-10-04 and Awakening's one of 2026-10-09,
        # each with a template to spawn.
        self.assertEqual(len(portraits.HEADLESS), 12)
        self.assertLessEqual(
            portraits.HEADLESS, set(portraits.TEMPLATES) | set(portraits.AWAKENING)
        )
        self.assertNotIn("screenshot", game.named())  # nothing was put up
        self.assertEqual(game.named()[-1], "kill")

    def test_a_camera_to_try_goes_with_the_ask(self) -> None:
        game = Game()
        sweep_of(game, camera={"distance": 1.4, "zAdjust": 0.9}).row(80)
        self.assertEqual(
            game.calls[2][1],
            {
                "id": "0x5001",
                "show": True,
                "seconds": 30,
                "distance": 1.4,
                "zAdjust": 0.9,
            },
        )

    def test_a_template_the_game_does_not_have_is_said_and_the_sweep_goes_on(
        self,
    ) -> None:
        game = Game()
        game.spawn = {
            "error": 'stage_spawn: no creature from template "ntb210cr_hermit"'
        }
        sweep = sweep_of(game)
        sweep.run([60, 1])
        # Nothing to make friendly or kill; row 1 has no template. After each
        # row the game's mode is asked, for a fight the row may have begun.
        self.assertEqual(game.named(), ["spawn", "health", "health"])
        self.assertIn("not spawned", sweep.records[0]["error"])
        self.assertIn("no template", sweep.records[1]["error"])
        self.assertEqual(len(sweep.cells), 2)
        self.assertFalse(sweep.stopped)

    def test_a_portrait_the_tool_refuses_still_has_its_creature_killed(self) -> None:
        game = Game()
        game.portraits = [{"error": "There is no creature with the id 0x5001"}]
        sweep = sweep_of(game)
        record = sweep.row(80)
        self.assertIn("no creature", record["error"])
        self.assertEqual(game.named(), ["spawn", "group", "show", "kill"])

    def test_a_game_that_stops_answering_stops_the_sweep(self) -> None:
        game = Game()
        game.spawn = {"error": "no answer from the game: timed out", "gone": True}
        sweep = sweep_of(game)
        sweep.run([64, 80, 66])
        self.assertEqual(len(sweep.records), 1)
        self.assertIn("timed out", sweep.stopped)

    def test_keep_leaves_the_creature_alive(self) -> None:
        game = Game()
        sweep_of(game, keep=True).row(80)
        self.assertNotIn("kill", game.named())

    def test_the_trees_table_gives_a_rows_camera_under_the_command_lines(self) -> None:
        # --from-table: the row's camera as the tree's table has it, given by hand.
        table = {
            80: {
                "distance": 1.5,
                "rotation": 35.0,
                "pitch": 0.0,
                "xAdjust": -0.85,
                "zAdjust": 0.8,
            }
        }
        game = Game()
        sweep = sweep_of(game, row_cameras=table)
        record = sweep.row(80)
        self.assertEqual(
            game.calls[2][1], {"id": "0x5001", "show": True, "seconds": 30, **table[80]}
        )
        self.assertEqual(record["camera_from"], "the tree's table")
        self.assertEqual(record["framed_by"], "row")
        # A row the table has none for is asked as it is; what the command line gives goes over the table's.
        game = Game()
        record = sweep_of(game, row_cameras=table).row(64)
        self.assertEqual(
            game.calls[2][1], {"id": "0x5001", "show": True, "seconds": 30}
        )
        self.assertNotIn("camera_from", record)
        game = Game()
        sweep_of(game, row_cameras=table, camera={"distance": 2.0}).row(80)
        self.assertEqual(game.calls[2][1]["distance"], 2.0)
        self.assertEqual(game.calls[2][1]["xAdjust"], -0.85)

    def test_a_fight_a_row_began_is_waited_out_and_said(self) -> None:
        game = Game()
        game.modes = ["combat", "combat", "combat", "explore"]
        sweep = sweep_of(game)
        sweep.run([65, 80])
        self.assertTrue(sweep.records[0]["fight"])
        self.assertNotIn("fight", sweep.records[1])
        self.assertEqual(len(sweep.records), 2)
        self.assertFalse(sweep.stopped)
        # One that does not end stops the sweep.
        game = Game()
        game.modes = ["combat"] * 40
        sweep = sweep_of(game)
        sweep.run([65, 80])
        self.assertEqual(len(sweep.records), 1)
        self.assertIn("does not end", sweep.stopped)

    def test_a_template_of_another_appearance_is_said(self) -> None:
        game = Game()
        sweep = sweep_of(game)
        record = sweep.row(64)  # the stand-in answers a drake's appearance, 80
        self.assertIn("has appearance 80, not 64", record["problem"])

    def test_a_picture_not_put_up_is_not_looked_for(self) -> None:
        game = Game()
        game.portraits = [
            {
                **ANSWER,
                "shown": False,
                "problem": "not put up: the turn bar's movie is not there",
            }
        ]
        sweep = sweep_of(game)
        record = sweep.row(80)
        self.assertNotIn("screenshot", game.named())
        self.assertEqual(sweep.cells, [(record, None)])


class Search(unittest.TestCase):
    """--search: one creature, a grid of asides and ups round a start, each pictured."""

    @staticmethod
    def game() -> Game:
        game = Game()
        turned = {
            **ANSWER,
            "appearance": 66,
            "label": "Dragon, High",
            "camera": {
                "distance": 6.0,
                "zAdjust": -0.8,
                "xAdjust": -1.7,
                "pitch": 0.0,
                "rotation": 35.0,
            },
        }
        game.portraits = [turned]
        return game

    def test_the_grid_is_round_the_camera_given_and_the_creature_is_made_once(
        self,
    ) -> None:
        game = self.game()
        sweep = sweep_of(
            game,
            camera={
                "distance": 6.0,
                "rotation": 35.0,
                "xAdjust": -1.7,
                "zAdjust": -0.8,
            },
        )
        cells = sweep.search(66, 3, 0.3)
        self.assertEqual(len(cells), 9)
        self.assertEqual(game.named().count("spawn"), 1)
        self.assertEqual(game.named().count("kill"), 1)
        self.assertEqual(game.named().count("screenshot"), 9)
        self.assertEqual(game.named()[-2:], ["take down", "kill"])
        asked = [
            a
            for name, a in game.calls
            if name == "turnbased_portrait" and a.get("show")
        ]
        # The first ask is the start; then the grid, the highest up first, the aside growing.
        self.assertEqual(len(asked), 10)
        self.assertEqual(
            [(a["xAdjust"], a["zAdjust"]) for a in asked[1:]],
            [(x, z) for z in (-0.5, -0.8, -1.1) for x in (-2.0, -1.7, -1.4)],
        )
        for ask in asked:
            self.assertEqual(ask["distance"], 6.0)
            self.assertEqual(ask["rotation"], 35.0)

    def test_what_is_not_given_starts_from_the_rules_own(self) -> None:
        game = self.game()
        sweep = sweep_of(game, camera={"rotation": 35.0})
        cells = sweep.search(66, 3, None)
        self.assertEqual(len(cells), 9)
        asked = [
            a
            for name, a in game.calls
            if name == "turnbased_portrait" and a.get("show")
        ]
        self.assertNotIn("xAdjust", asked[0])
        # A sixth of the picture's width between cells: 0.525 m for 6 m.
        self.assertEqual(
            sorted({a["xAdjust"] for a in asked[1:]}),
            [-2.225, -1.7, -1.175],
        )
        self.assertEqual(asked[5]["zAdjust"], -0.8)

    def test_a_picture_that_is_not_a_render_is_not_searched(self) -> None:
        game = Game()
        game.portraits = [
            {
                "label": "Wolf",
                "source": "the portraits table's",
                "problem": "not rendered",
            }
        ]
        sweep = sweep_of(game)
        self.assertEqual(sweep.search(49, 3, 0.1), [])
        self.assertIn("nothing to search", sweep.records[0]["error"])
        self.assertEqual(game.named()[-1], "kill")
        self.assertEqual(game.named().count("show"), 1)


@unittest.skipUnless(portraits.Image is not None, "Pillow is not installed")
class Pictures(unittest.TestCase):
    def test_the_cell_is_the_raw_renders_square_of_the_frame(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            frame = portraits.Image.new("RGB", (1600, 1000), (10, 10, 10))
            box = portraits.raw_box(1600, 1000)
            frame.paste((200, 40, 40), box)
            file = Path(folder) / "frame.png"
            frame.save(file)
            crop, small = portraits.cell_of(file)
            self.assertEqual(crop.size, (box[2] - box[0], box[3] - box[1]))
            self.assertEqual(small.size, (portraits.CELL, portraits.CELL))
            for picture in (crop, small):
                self.assertEqual(picture.getpixel((0, 0)), (200, 40, 40))
                self.assertEqual(
                    picture.getpixel((picture.width - 1, picture.height - 1)),
                    (200, 40, 40),
                )

    def test_the_sheets_are_thirty_cells_each(self) -> None:
        cell = portraits.Image.new(
            "RGB", (portraits.CELL, portraits.CELL), (90, 90, 90)
        )
        record = portraits.record_of(80, "drake", "core", ANSWER)
        cells = [(record, cell)] * 31 + [
            ({"row": 14, "template": "wisp", "lack": "no_head_node"}, None)
        ]
        with tempfile.TemporaryDirectory() as folder:
            written = portraits.contact_sheets(cells, Path(folder))
            self.assertEqual(
                [file.name for file in written], ["sweep-1.jpg", "sweep-2.jpg"]
            )
            with portraits.Image.open(written[0]) as first:
                self.assertEqual(first.size, portraits.sheet_size(30))
                # A cell's own grey, as near as a JPEG keeps it.
                for channel in first.getpixel((50, 50)):
                    self.assertAlmostEqual(channel, 90, delta=4)
            with portraits.Image.open(written[1]) as second:
                self.assertEqual(second.size, portraits.sheet_size(2))


if __name__ == "__main__":
    unittest.main()
