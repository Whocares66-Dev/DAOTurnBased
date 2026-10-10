"""The cards' portraits, swept through every appearance the game has.

Issue 32: the one who writes a portrait's framing cannot see it, and which
bodies want a framing of their own is found by looking at all of them (the
user, 2026-10-04: "we can enumerate all the creatures in the game, test
portraits, see which ones need special handling beyond the standard
humanoid"). For each row of the appearance table this stands a REAL creature
of that appearance beside the party, has the mod render its portrait and put
it up large (the `turnbased_portrait` tool, `show`), keeps the raw render's
part of the game's own frame, and takes the creature away again. It writes
build\\portraits\\sweep.json (the tool's answer for every row) and contact
sheets of thirty, sweep-1.jpg and on, each cell with its row and label, what
framed it and the camera used.

A real creature, spawned from a template: the first sweep changed one
creature's appearance row by row (the scripts' SetAppearanceType), and 63 of
95 rows did not take on a dragonling's body, while many that did came out
empty or as a close-up of skin that a creature of the kind does not give
(2026-10-04). TEMPLATES below is which template to spawn for which row, read
from the game's own files that day: each creature template's (.utc, GFF V3.2)
`Appearance Data.Appearance_Type`, in packages\\core\\data\\designercreatures.erf
(the core's, which every area can spawn: "core") and the campaign's own
modules\\single player\\data\\designercreatures.erf ("campaign"). One template a
row, the plain ones of the core first.

    python tools\\portraits.py --list                 the rows and their templates; no game
    python tools\\portraits.py                        every row the core or the campaign has a creature for
    python tools\\portraits.py --rows 64 80 66        those rows
    python tools\\portraits.py --first 50 --last 60   that range
    python tools\\portraits.py --row 80 --distance 1.5 --rotation 35 --x -0.85 --z 0.8
                                                     one row with a camera to try: its cell is kept as
                                                     build\\portraits\\row-80-d1.5-z0.8-x-0.85-r35.jpg,
                                                     and its line for the framing table is printed
    python tools\\portraits.py --row 66 --rotation 35 --distance 6 --x -1.7 --z -0.8 --search
                                                     the same creature rendered nine times, the aside and
                                                     the up a step either side of those given, on one
                                                     sheet (...-search.jpg), with each cell's line for
                                                     the table: a row found in one or two looks
    python tools\\portraits.py --row 80 --distance 1.5 --rotation 35 --x -0.85 --z 0.8 --print-row
                                                     only the line, from the numbers; no game
    python tools\\portraits.py --from-table           every row, each with the camera this tree's table
                                                     has for it, given by hand: the table as it will
                                                     frame them, before the plugin in the game is rebuilt
    python tools\\portraits.py --fights               also the wild sylvan, which begins a fight
    python tools\\portraits.py --addon amgarrak --game FOLDER
                                                     an add-in's creatures, by its templates (their names
                                                     read from its archives' lists under FOLDER\\Addins),
                                                     in a game running it: the first of each appearance
                                                     pictured, the row and label as the game answers them

The framing table is src\\core\\PortraitTable.h: one row a creature type, every
number of a portrait's framing in the one place (the user, 2026-10-04: "if we
can put the portrait angles and stuff in a table somewhere by creature type,
we'd have it all in one place"). Filling it from a sweep is copying a line:
the line printed is a row of it, with --where for the place it was seen
("at the camp") and --note for what of it was seen ("seen as it is", or which
two numbers one lies between).

A side profile is what is wanted of a creature, as people have (the user:
"Like maybe a 30 to 45° angle"; 35 is what was looked at), and a turn
(--rotation) slides the head sideways in the picture, by an amount that is
each model's own: an aside (--x) of the other sign brings it back. That is
what --search is for: give the turn and a guess, look at the sheet, take the
cell with the head in the middle (--step and --grid set how far apart and
how many). What is not given starts from what the table answers for the
creature, its own row or its kind's default.

It drives a game that is ALREADY RUNNING, through Eluvian's HTTP as
tools\\sweep.py does (the development bench, ..\\DAOEluvianMCP): the party
somewhere with room (the camp), outside a fight, the turn bar on
(`[feedback] turn_bar = true`) with its movie deployed. It launches and quits
nothing. Each creature is made friendly as it appears (`stage_group`, group
2) and killed after (`stage_kill`), which leaves bodies where the party
stands; --keep leaves them alive.

Written without the game, the calls and what they answer read from Eluvian's
source and the loop tested against a stand-in (tools\\test_portraits.py); then
run in play, 2026-10-04, at the camp: the whole sweep three times (80 rows
answered the first time, as written), and thirty-two searches and some
fifty-five single rows, by which the table's rows of that day were found. What play showed that the
stand-in did not: one creature, the wild sylvan, stays hostile whatever group
it is moved to and begins a fight for as long as it stands (the party was
hurt a little, twice), so its row is left out of a sweep unless --fights
asks for it, and a sweep asks the game's mode after every row and waits a
fight out before the next. And a creature's rendered picture has the area
behind it, faintly, from a camera far back (the camp's tents behind a pride
demon): the engine's, and no fault of the row.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
import time
import urllib.error
import urllib.request
from collections.abc import Callable
from datetime import date, datetime, timezone
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # --list and the tests of the loop's order need no pictures
    Image = None
    ImageDraw = None

ROOT = Path(__file__).resolve().parent.parent
BASE = "http://127.0.0.1:8930"
OUT = ROOT / "build" / "portraits"

# Which template is spawned for which row of the appearance table: the row,
# the template, the row's label (APR_base.gda), and where the template is:
#   core      packages\core\data\designercreatures.erf: every area can spawn it
#   campaign  modules\single player\data\designercreatures.erf: the campaign's
#   plot      the campaign's, and a plot's own creature or a follower: spawned
#             only with --plot, since its scripts may do more than stand there
#   fights    the core's, and hostile whatever group it is moved to: spawned
#             only with --fights (or named by --row or --rows), since it
#             begins a fight with the party for as long as it stands. The
#             wild sylvan did, twice, in the first search of it (2026-10-04)
# Read 2026-10-04 from the game's files (GOG, 1.05). Rows 1, 19, 21, 43, 48
# and 56 have no template anywhere: nothing in the game wears them. The
# labels are the running game's own, as its appearance table answers them.
TEMPLATES: dict[int, tuple[str, str, str]] = {
    2: ("bec100cr_elf_commoner_f", "Elf", "campaign"),
    3: ("arl200cr_dwarf", "Dwarf", "campaign"),
    4: ("dragon", "Dragon, Normal", "core"),
    5: ("shale", "Golem, DEPRECATED", "core"),
    6: ("golem_stone", "Golem, Stone", "core"),
    7: ("golem_steel", "Golem, Steel", "core"),
    8: ("bear_great", "Bear, Great", "core"),
    9: ("bear_black", "Bear, Black", "core"),
    10: ("broodmother", "Broodmother", "core"),
    11: ("bronto", "Bronto", "core"),
    12: ("arcane_horror", "Arcane Horror", "core"),
    13: ("merc_qunari_heavy", "Qunari", "core"),
    14: ("wisp", "Wisp", "core"),
    15: ("bandit_human_daggers", "Human", "core"),
    16: ("hurlock_lsword", "Hurlock - Normal", "core"),
    17: ("hurlock_alpha_greatsword", "Hurlock - Alpha", "core"),
    18: ("hurlock_emissary_off", "Hurlock - Emissary", "core"),
    20: ("nug_core", "Nug", "core"),
    22: ("werewolf_core", "Werewolf A", "core"),
    23: ("shriek_normal", "Shriek A", "core"),
    24: ("succubus", "Succubus", "core"),
    25: ("abomination_rage", "Abomination", "core"),
    26: ("revenant", "Revenant A", "core"),
    27: ("rage_demon", "Rage Demon", "core"),
    28: ("corpse_devouring", "Corpse D", "core"),
    29: ("corpse_enraged", "Corpse E", "core"),
    30: ("corpse_shambling", "Corpse S", "core"),
    31: ("shade", "Shade", "core"),
    32: ("ash_wraith", "Ash Wraith", "core"),
    33: ("deep_stalker", "Deepstalker", "core"),
    34: ("dog_mabari_1", "Dog, Mabari", "core"),
    35: ("war_dog_b", "Dog, Party Member", "core"),
    36: ("squirel", "Squirrel", "core"),
    37: ("rat_giant", "Rat, Large", "core"),
    38: ("child_boy", "Human, Boy", "core"),
    39: ("bhn200cr_panicked_servant", "Human Servant Ambient", "campaign"),
    40: ("bhn100cr_library_browser", "Human Library Ambient", "campaign"),
    41: ("rat_ambient", "Rat, Small", "core"),
    42: ("pre100cr_guard_still", "Human Guard Ambient", "campaign"),
    44: ("human_fat_male", "Human Male, Fat", "core"),
    45: ("human_fat_female", "Human Female, Fat", "core"),
    46: ("pre100cr_duncan", "NPC: Duncan", "campaign"),
    47: ("ogre_default", "Ogre A", "core"),
    49: ("wolf", "Wolf", "core"),
    50: ("genlock_axe", "Genlock - Normal", "core"),
    51: ("genlock_alpha_dual_axes", "Genlock - Alpha", "core"),
    52: ("genlock_emissary_off", "Genlock - Emissary", "core"),
    53: ("witherfang", "Witherfang", "core"),
    54: ("goat", "Ambient, Goat", "core"),
    55: ("dog_ambient", "Ambient, Mutt", "core"),
    57: ("spider_corrupted", "Spider, Corrupted", "core"),
    58: ("spider_giant", "Spider, Giant", "core"),
    59: ("spider_poisonous", "Spider, Poisonous", "core"),
    60: ("ntb210cr_hermit", "Human Dying Ambient", "plot"),
    61: ("pre100cr_wizard", "Human, Prelude Wizard", "campaign"),
    62: ("torch_dude", "CUT (was torch guy)", "core"),
    63: ("house_cat", "Cat", "core"),
    64: ("dragonling", "Dragonling", "core"),
    65: ("wild_sylvan_core", "Wild Sylvan", "fights"),
    66: ("high_dragon", "Dragon, High", "core"),
    67: ("den200cr_amb_girl1", "Human, Girl", "campaign"),
    68: ("bereskarn", "Bear, Beareskan", "core"),
    69: ("skeleton_normal", "Skeleton A", "core"),
    70: ("skeleton_fanged", "Skeleton F", "core"),
    71: ("skeleton_shambling", "Skeleton S", "core"),
    72: ("demon_pride", "Pride Demon", "core"),
    73: ("broodmother_tentacle", "Broodmother Tentacle", "core"),
    74: ("wolf_blight", "Wolf, Blight", "core"),
    75: ("lady_of_the_forest", "Lady of the Forest", "core"),
    76: ("pig", "Pig", "core"),
    77: ("deer", "Deer", "core"),
    78: ("ox", "Ox", "core"),
    79: ("ram", "Ram", "core"),
    80: ("drake", "Dragon, Drake", "core"),
    81: ("orz540cr_spirit_app_head", "Spirit Apparatus Head", "campaign"),
    82: ("archdemon", "Archdemon  (Wounded)", "core"),
    83: ("gen00fl_shale", "Mr. Teapot", "plot"),
    84: ("raven", "Raven", "core"),
    85: ("halla", "Halla", "core"),
    86: ("chicken", "Chicken", "core"),
    87: ("owl", "Owl", "core"),
    88: ("ntb220cr_grand_oak", "Grand Oak", "plot"),
    89: ("cinearchdemon", "Archdemon", "core"),
}

# The rows the game's other campaign adds (Awakening: packages\core_ep1), each
# with a template of its own there. The base campaign cannot spawn them (an
# add-in's templates are loaded only where it runs: Eluvian's dev/TESTING.md);
# --awakening sweeps them, in a game that is running Awakening. Each row's label
# is the appearance table's own, as the game answered it of the creature spawned
# (the sweep of 2026-10-09 in the Silverite Mine: issue 84).
AWAKENING: dict[int, tuple[str, str]] = {
    20002: ("demon_pride_b", "(GXA) Pride Demon B"),
    20003: ("shade_b", "(GXA) Shade B"),
    20004: ("high_dragon_lit", "(GXA) Lightning Dragon Spirit"),
    20006: ("giant_golem_fir", "(GXA) Giant Golem, Fire"),
    20007: ("childer_p1", "(GXA) Childer Grub"),
    20008: ("childer_p2", "(GXA) Childer Hatchling"),
    20009: ("childer_p3", "(GXA) Childer Adult"),
    20010: ("disciple_axe_shield", "(GXA) Disciple - Normal"),
    20011: ("disciple_alpha_axe_shield", "(GXA) Disciple - Alpha"),
    20012: ("disciple_emissary_bal", "(GXA) Disciple - Emissary"),
    20013: ("blighted_werewolf", "(GXA) Blighted Werewolf"),
    20014: ("armored_ogre", "(GXA) Armored Ogre"),
    20015: ("the_architect", "(GXA) The Architect"),
    20016: ("the_mother", "(GXA) The Mother"),
    20017: ("the_mother_tentacle", "(GXA) The Mother - Tentacle"),
    20018: ("the_mother_cinematics", "(GXA) The Mother [cinematics]"),
    20019: ("dragon_b", "(GXA) Dragon B"),
    20020: ("wild_sylvan_charred", "(GXA) Wild Sylvan, Charred"),
    43000: ("str500cr_atavar", "(STR) Strider"),
    20021: ("disciple_heretic_greatsword", "(GXA) Heretic Disciple - Normal"),
    20100: ("cinem_disciple_body_top", "(GXA) Disciple Body - Top"),
    20101: ("cinem_disciple_body_bottom", "(GXA) Disciple Body - Bottom"),
    20200: ("corpse_devouring_putrid", "(GXA) Corpse, Putrid Devouring"),
    20201: ("corpse_enraged_ravenous", "(GXA) Corpse, Ravenous Enraged"),
    20202: ("corpse_shambling_desiccated", "(GXA) Corpse, Desiccated Shambling"),
    20203: ("skeleton_normal_frenzied", "(GXA) Skeleton, Frenzied Devouring"),
    20204: ("skeleton_fanged_ancient", "(GXA) Skeleton, Ancient Fanged"),
    20205: ("skeleton_shambling_mangled", "(GXA) Skeleton, Mangled Shambling"),
}

# The other add-ins the Ultimate Edition installs that have creatures of
# their own (the game's Addins folder): a name for --addon, the add-in's
# folder, what it is, and the appearance and portraits tables its archives
# name. Read 2026-10-04 from the archives' own lists of what is in them, and
# no further: but for Awakening's, every one of these archives is ERF V2.2
# with its contents enciphered (the header's flags: encryption 2), so which
# rows a table adds and which row a template wears is not read here, and the
# tool deciphers nothing. The game says both of a creature it has spawned
# (the answer's `appearance` and `label`), so an add-in is swept by its
# TEMPLATES, in a game running it: --addon NAME --game FOLDER, the names
# read from the add-in's designercreatures.erf as they are listed there.
# "With the campaign's" is a table in the add-in's core data, which the
# game merges into the campaign's own while the add-in is on; "its own
# campaign's" is one only its own module has.
ADDONS: dict[str, tuple[str, str, str]] = {
    "ostagar": (
        "dao_prc_cp_1",
        "Return to Ostagar",
        "apr_base_kcc, with the campaign's",
    ),
    "shale": ("dao_prc_cp_2", "The Stone Prisoner", "apr_shale, with the campaign's"),
    "keep": ("dao_prc_nrx_1", "Warden's Keep", "apr_base_gwb, with the campaign's"),
    "gifts": ("dao_prc_cp_3", "Feastday Gifts", "no table: the campaign's rows"),
    "pranks": ("dao_prc_cp_7", "Feastday Pranks", "no table: the campaign's rows"),
    "leliana": ("dao_prc_lel", "Leliana's Song", "no table: the campaign's rows"),
    "chronicles": (
        "dao_prc_drk",
        "The Darkspawn Chronicles",
        "apr_drk and portraits_drk, its own campaign's",
    ),
    "amgarrak": (
        "dao_prc_gib",
        "The Golems of Amgarrak",
        "apr_gib and portraits_gib, and Awakening's apr_gxa and portraits_gxa",
    ),
    "witch": (
        "dao_prc_str",
        "Witch Hunt",
        "apr_str and portraits_str, and Awakening's apr_gxa and portraits_gxa",
    ),
}

# The rows that answered no head node in every sweep of 2026-10-04 (three,
# a real creature each time): asked again twice and not waited for, and done
# once rendered, which the plugin does about the creature's root where the
# framing table has a row from the root for it (all but 5 and 62 have, since
# that day) or a camera is given. Any other row
# is waited for much longer, since a model can be late: the werewolf (22)
# had no head node for the 4.5 seconds one sweep gave it, and rendered at the
# first ask in the sweeps before and after.
# And Awakening's 20017, the Mother's tentacle, as the broodmother's (73): none
# in 16 s, 2026-10-09.
HEADLESS = frozenset({5, 33, 62, 73, 76, 77, 78, 81, 85, 86, 87, 20017})


def erf_names(path: Path) -> list[str]:
    """What an archive of the game's says is in it: its entries' names, lowercased.

    ERF V2.0 and V2.2 (a UTF-16 magic, a count, then an entry each of a
    64-byte UTF-16 name and its place): only the list is read, which is
    plain in both, and nothing of the contents, which V2.2 may encipher.
    """
    with path.open("rb") as archive:
        head = archive.read(20)
        magic = head[:16].decode("utf-16-le", errors="replace")
        # The entry's size and where the list starts: after the header (and,
        # in V2.2, its flags, module and the password's digest).
        size, start = {"ERF V2.0": (72, 32), "ERF V2.2": (76, 56)}.get(magic, (0, 0))
        if not size:
            return []
        (count,) = struct.unpack_from("<I", head, 16)
        archive.seek(start)
        table = archive.read(size * count)
    return [
        table[at : at + 64].decode("utf-16-le", errors="replace").split("\0")[0].lower()
        for at in range(0, len(table) - size + 1, size)
    ]


def addon_templates(game: Path, addon: str) -> list[str]:
    """An add-in's creature templates, by name, as its archives list them."""
    folder = game / "Addins" / ADDONS[addon][0]
    names: set[str] = set()
    for archive in sorted(folder.rglob("designercreatures.erf")):
        names.update(name[:-4] for name in erf_names(archive) if name.endswith(".utc"))
    return sorted(names)


# Where the bar's movie puts the picture it shows large, in its own stage's
# points (ui/turnorder/frame_1/DoAction.as: the stage, and drawShow's
# SHOW_*; tools/test_portraits.py holds these equal to the movie's). The raw
# render is the square beside the big medallion.
STAGE_W, STAGE_H = 1280, 720
SHOW_X, SHOW_Y = 150, 176
SHOW_PAD = 16
SHOW_BIG = 240
SHOW_RAW = 146

CELL = 180  # a contact sheet's cell, pixels
PER_SHEET = 30
COLUMNS = 6
CAPTION = 44  # under each cell, three lines


def raw_box(width: int, height: int, inset: int = 2) -> tuple[int, int, int, int]:
    """The raw render's square in a frame of the game's, as (left, top, right, bottom).

    The movie's stage is laid over the game's view whole and centred
    (Stage.scaleMode showAll), so a stage point is a pixel by one scale and an
    offset. `inset` keeps the gold line round the square out. In a 1600 x 1000
    frame this is the square from (529, 292) to (709, 470), as measured in
    play (2026-10-04).
    """
    scale = min(width / STAGE_W, height / STAGE_H)
    left = (width - STAGE_W * scale) / 2 + (SHOW_X + 2 * SHOW_PAD + SHOW_BIG) * scale
    top = (height - STAGE_H * scale) / 2 + (SHOW_Y + SHOW_PAD) * scale
    side = SHOW_RAW * scale
    return (
        round(left) + inset,
        round(top) + inset,
        round(left + side) - inset,
        round(top + side) - inset,
    )


def camera_arguments(options: argparse.Namespace) -> dict:
    """The camera a tester gives on the command line, as the tool's arguments."""
    names = {
        "distance": "distance",
        "z": "zAdjust",
        "x": "xAdjust",
        "pitch": "pitch",
        "rotation": "rotation",
        "kind": "kind",
    }
    return {
        argument: getattr(options, option)
        for option, argument in names.items()
        if getattr(options, option, None) is not None
    }


def printed_record(options: argparse.Namespace) -> dict:
    """The camera given on the command line as a record, for its line of the table (--print-row)."""
    return {
        "row": options.row,
        "label": (TEMPLATES.get(options.row) or ("", "", ""))[1],
        "distance": options.distance,
        "rotation": options.rotation,
        "pitch": options.pitch,
        "xAdjust": options.x,
        "zAdjust": options.z,
    }


def trial_name(row: int, camera: dict) -> str:
    """A trial's file name, without its ending: row-80-d1.4-z0.9 for distance 1.4, zAdjust 0.9."""
    tried = "".join(
        f"-{key[0]}{value:g}" for key, value in camera.items() if key != "kind"
    )
    return f"row-{row}{tried}" + (f"-{camera['kind']}" if "kind" in camera else "")


def chosen_rows(options: argparse.Namespace) -> list[int]:
    """The rows to sweep, in order, with what the options leave out left out."""
    table = dict.fromkeys(TEMPLATES)
    if options.awakening:
        table.update(dict.fromkeys(AWAKENING))
    if options.row is not None:
        return [options.row]
    rows = options.rows if options.rows else list(table)
    rows = [r for r in rows if options.first <= r <= options.last]
    if not options.rows:
        # What a sweep of everything leaves out unless asked for by name.
        left_out = {
            source
            for source, asked in (("plot", options.plot), ("fights", options.fights))
            if not asked
        }
        rows = [r for r in rows if TEMPLATES.get(r, ("", "", ""))[2] not in left_out]
    return rows


# A row of the framing table as it is written there (src/core/PortraitTable.h):
# Row(64, "64 Dragonling", distance, rotation, pitch, aside, up, "note").
TABLE = ROOT / "src" / "core" / "PortraitTable.h"
TABLE_ROW = re.compile(
    r'\bRow\(\s*(\d+),\s*"[^"]*",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,'
    r"\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,"
)


def table_rows(path: Path = TABLE) -> dict[int, dict]:
    """The framing table's rows, each as the tool's camera arguments.

    For a sweep by the table as the tree has it (--from-table), whatever the
    plugin in the running game was built with: a row written today is seen
    today, before a build and a deploy.
    """
    text = path.read_text(encoding="utf-8")
    text = text[text.index("kPortraitTable{") :]
    return {
        int(row): {
            "distance": float(distance),
            "rotation": float(rotation),
            "pitch": float(pitch),
            "xAdjust": float(aside),
            "zAdjust": float(up),
        }
        for row, distance, rotation, pitch, aside, up in TABLE_ROW.findall(text)
    }


def template_of(row: int) -> tuple[str, str, str] | None:
    """A row's template, label and where the template is; nothing for a row with none."""
    if row in TEMPLATES:
        return TEMPLATES[row]
    if row in AWAKENING:
        return (*AWAKENING[row], "awakening")
    return None


def spawn_point(hero: dict, ahead: float) -> dict:
    """A place `ahead` metres before the creature the player controls.

    `hero` is Eluvian's `creature`: its `position` {x, y, z} and
    its `facing` in degrees, taken here as from +x toward +y. Should that be
    another way round in the game, the place is still `ahead` metres from the
    party, to another side of it.
    """
    at = hero["position"]
    facing = math.radians(float(hero.get("facing") or 0.0))
    return {
        "x": at["x"] + ahead * math.cos(facing),
        "y": at["y"] + ahead * math.sin(facing),
        "z": at["z"],
    }


def record_of(row: int, template: str, source: str, answer: dict) -> dict:
    """One row of the sweep: what a contact sheet's cell says, and the tool's whole answer."""
    camera = answer.get("camera") or {}
    return {
        "row": row,
        "template": template,
        "template_from": source,
        "appearance": answer.get("appearance"),
        "label": answer.get("label"),
        "kind": answer.get("kind"),
        "framed_by": answer.get("framed_by"),
        "table_row": answer.get("row") or "",
        "source": answer.get("source"),
        "lack": answer.get("lack") or "",
        "extent": answer.get("extent"),
        "distance": camera.get("distance"),
        "zAdjust": camera.get("zAdjust"),
        "xAdjust": camera.get("xAdjust"),
        "pitch": camera.get("pitch"),
        "rotation": camera.get("rotation"),
        "problem": answer.get("problem") or "",
        "answer": answer,
    }


def caption(record: dict) -> list[str]:
    """A cell's three lines: the row and its label, what framed it, the camera used."""
    first = f"{record['row']} {record.get('label') or record.get('template') or ''}"[
        :30
    ]
    if record.get("error"):
        return [first, "error", str(record["error"])[:30]]
    rendered = record.get("source") == "rendered"
    if record.get("lack") and not rendered:
        # With how long it was waited for, where it was (a row not known to
        # have no head node): not ready in that time, or no such node.
        second = record["lack"] + (
            f", {record['waited']:.0f} s" if record.get("waited") else ""
        )
    elif record.get("lack"):
        # No head node, and rendered about its root all the same: by a row
        # of the table from the root, or by the camera given.
        second = f"{record.get('framed_by') or 'given'}, from its root"
    else:
        # What framed it: its own row of the table, the game's, or its kind's default.
        by = record.get("framed_by") or record.get("source") or ""
        if by == "default":
            by = f"default {(record.get('kind') or '').replace('_', ' ')}".strip()
        extent = record.get("extent")
        second = f"{by}, {extent:.2f} m" if isinstance(extent, (int, float)) else by
    if record.get("distance") is None or record.get("source") != "rendered":
        return [first, second[:30], ""]
    # Close set, to fit under a cell: d the distance, z up, x aside, and with a
    # turn of the camera p the pitch and r the rotation.
    third = (
        f"d{record['distance']:.2f} z{record.get('zAdjust') or 0:.2f}"
        f" x{record.get('xAdjust') or 0:.2f}"
    )
    if record.get("pitch") or record.get("rotation"):
        third += f" p{record.get('pitch') or 0:.0f} r{record.get('rotation') or 0:.0f}"
    return [first, second[:30], third[:30]]


def cpp_float(value: float | None) -> str:
    """A number as a C++ float: 1.4f, 0.0f, -7.5f."""
    text = f"{value or 0.0:g}"
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def table_line(
    record: dict,
    where: str,
    seen: str = "seen as it is",
    today: date | None = None,
) -> str:
    """A camera as a row of the framing table (src/core/PortraitTable.h), ready to paste.

    The table's own columns: the row and its label, then distance, rotation,
    pitch, aside, up, and the note: by eye, where, when, and what of it was
    seen (`seen`: change it for a number that lies between two that were).
    """
    day = (today or datetime.now(timezone.utc).astimezone().date()).isoformat()
    row = record["row"]
    label = record.get("label") or (TEMPLATES.get(row) or ("", "", ""))[1]
    camera = ", ".join(
        cpp_float(record.get(key))
        for key in ("distance", "rotation", "pitch", "xAdjust", "zAdjust")
    )
    # A row of a model with no head node is from its root, and marked so
    # (FromRoot: the engine's camera aims at the scene's origin for it).
    rooted = row in HEADLESS or record.get("lack") == "no_head_node"
    return (
        f"    {'FromRoot(' if rooted else ''}"
        f'Row({row}, "{f"{row} {label}".strip()}", {camera},\n'
        f'        "by eye {where}, {day}: {seen}"){")" if rooted else ""},'
    )


def sheet_size(cells: int, columns: int = COLUMNS) -> tuple[int, int]:
    """A sheet's size for that many cells: six across, or as many as given."""
    rows = (cells + columns - 1) // columns
    return (columns * (CELL + 6), rows * (CELL + CAPTION))


def sheet_of(cells: list[tuple[list[str], object]], columns: int = COLUMNS):
    """A sheet: each cell's picture with its three lines under it, `columns` across."""
    sheet = Image.new("RGB", sheet_size(len(cells), columns), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for k, (lines, picture) in enumerate(cells):
        x = (k % columns) * (CELL + 6) + 3
        y = (k // columns) * (CELL + CAPTION) + 3
        if picture is not None:
            sheet.paste(picture, (x, y))
        draw.text((x, y + CELL + 2), lines[0], fill=(230, 220, 170))
        draw.text((x, y + CELL + 15), lines[1], fill=(170, 170, 170))
        draw.text((x, y + CELL + 28), lines[2], fill=(150, 170, 190))
    return sheet


def contact_sheets(cells: list[tuple[dict, object]], out: Path) -> list[Path]:
    """The sweep's cells as sheets of thirty, each with its three lines; the files written."""
    written = []
    for n in range(0, len(cells), PER_SHEET):
        part = [
            (caption(record), picture) for record, picture in cells[n : n + PER_SHEET]
        ]
        name = out / f"sweep-{n // PER_SHEET + 1}.jpg"
        sheet_of(part).save(name, quality=88)
        written.append(name)
    return written


def grid(start: float, step: float, count: int) -> list[float]:
    """`count` values `step` apart with `start` in their middle: for three, one each side of it."""
    half = (count - 1) / 2
    return [round(start + (i - half) * step, 3) for i in range(count)]


def cell_of(file: str | Path) -> tuple[object, object]:
    """The raw render's part of a frame of the game's: as it is, and as a sheet's cell."""
    with Image.open(file) as frame:
        crop = frame.crop(raw_box(*frame.size)).convert("RGB")
    return crop, crop.resize((CELL, CELL))


def http_tool(name: str, arguments: dict | None = None, timeout: float = 60) -> dict:
    """One call of a game tool through Eluvian: the answer, or {"error": ...} as it refused."""
    request = urllib.request.Request(
        f"{BASE}/api/tool/{name}",
        data=json.dumps(arguments or {}).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        try:
            return json.load(error)
        except ValueError:
            return {"error": f"HTTP {error.code}"}
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return {"error": f"no answer from the game: {error}", "gone": True}


class Sweep:
    """The sweep's loop: a creature a row, its portrait, its picture, and away with it."""

    # How often a creature with no head node is asked again, 0.6 seconds
    # apart: one just spawned may not have its model yet. Five (4.5 seconds
    # with the wait after the spawn) was once too few for a werewolf, so a
    # quarter of a minute; and twice for the rows known to have none.
    HEAD_TRIES = 25
    HEADLESS_TRIES = 2
    SPAWN_WAIT = 1.5
    HEAD_WAIT = 0.6

    def __init__(
        self,
        tool: Callable[..., dict],
        point: dict,
        *,
        camera: dict | None = None,
        row_cameras: dict[int, dict] | None = None,
        keep: bool = False,
        sleep: Callable[[float], None] = time.sleep,
        picture: Callable[[str], tuple[object, object]] | None = cell_of,
        say: Callable[[str], None] = print,
    ) -> None:
        self.tool = tool
        self.point = point
        self.camera = camera or {}
        # A camera for a row by its number (the tree's table, --from-table):
        # given with the row's ask, under whatever the command line gave.
        self.row_cameras = row_cameras or {}
        self.keep = keep
        self.sleep = sleep
        self.picture = picture
        self.say = say
        self.records: list[dict] = []
        self.cells: list[tuple[dict, object]] = []
        self.crops: dict[int, object] = {}
        self.stopped = ""
        self.asked = 0  # how often the last creature was asked for its portrait
        self.pictured: dict[int, str] = {}  # an add-in's sweep: appearance -> template

    def stand(self, row: int) -> tuple[str, dict] | None:
        """A creature of the row beside the party, friendly and set up: its id and what is
        known of the row. Nothing, with the reason kept as the row's record, where none came."""
        found = template_of(row)
        if found is None:
            self.keep_record(
                {"row": row, "error": "no template in the game has this appearance"}
            )
            return None
        template, label, source = found
        return self.spawn(
            {"row": row, "template": template, "template_from": source, "label": label}
        )

    def spawn(self, base: dict) -> tuple[str, dict] | None:
        """A creature of `base`'s template beside the party, friendly and set up."""
        template = base["template"]
        spawned = self.tool("stage_spawn", {"template": template, "point": self.point})
        who = (spawned.get("creature") or {}).get("id")
        if "error" in spawned or not who:
            if spawned.get("gone"):
                self.stopped = str(spawned["error"])
            self.keep_record(
                {**base, "error": f"not spawned: {spawned.get('error', spawned)}"}
            )
            return None
        base["moved"] = spawned.get("moved")
        # Not hostile, before it has looked about: a hostile one beside the
        # party starts a fight.
        self.tool("stage_group", {"id": who, "group": 2})
        # A creature spawned is set up over the next frames (Eluvian: its
        # health reads 1 of 1 in the frame it is made).
        self.sleep(self.SPAWN_WAIT)
        return who, base

    def ask(
        self, who: str, camera: dict, seconds: int = 30, headless: bool = False
    ) -> dict:
        """Its portrait rendered and put up, asked again while it has no head node yet.

        `headless` is a row known to have none (HEADLESS): asked again only
        a little, and done once it is rendered, which the plugin does for it
        about its root where the table has a row from the root for it or a
        camera is given. Another row is asked on even if that rendered it:
        its head is late, and the picture about its root is not its row's.
        """
        ask = {"id": who, "show": True, "seconds": seconds, **camera}
        answer = self.tool("turnbased_portrait", ask)
        self.asked = 1
        tries = self.HEADLESS_TRIES if headless else self.HEAD_TRIES
        while (
            answer.get("lack") == "no_head_node"
            and not (headless and answer.get("rendered"))
            and self.asked <= tries
        ):
            self.sleep(self.HEAD_WAIT)
            answer = self.tool("turnbased_portrait", ask)
            self.asked += 1
        if answer.get("gone"):
            self.stopped = str(answer["error"])
        return answer

    def waited(self) -> float:
        """How long the last creature had, from its spawn to its last ask."""
        return round(self.SPAWN_WAIT + self.HEAD_WAIT * (self.asked - 1), 1)

    def shot(self, record: dict) -> tuple[object, object]:
        """The picture put up, out of the game's own frame: as it is, and as a cell."""
        # The movie asks three times a second, then loads the picture.
        self.sleep(0.8)
        shot = self.tool("screenshot", {"source": "game", "image": False})
        if not shot.get("file"):
            record["problem"] = (
                record["problem"] + "; no picture of the game's frame"
            ).lstrip("; ")
            return None, None
        record["frame"] = shot["file"]
        return self.picture(shot["file"])

    def away(self, who: str, shown: bool) -> None:
        """The picture down, and then the creature gone."""
        if shown:
            self.tool("turnbased_portrait", {"id": who, "show": False})
            # Its renderer is released once the movie has let the picture go.
            self.sleep(1.0)
        if not self.keep:
            self.tool("stage_kill", {"id": who})
            self.sleep(0.3)

    def row(self, row: int) -> dict:
        """One row: spawned, made friendly, rendered and shown, pictured, taken down, killed."""
        stood = self.stand(row)
        if stood is None:
            return self.records[-1]
        who, base = stood
        shown = False
        try:
            answer = self.ask(
                who,
                {**self.row_cameras.get(row, {}), **self.camera},
                headless=row in HEADLESS,
            )
            if "error" in answer:
                return self.keep_record(
                    {**base, "id": who, "error": str(answer["error"])[:300]}
                )
            shown = True
            record = {
                **record_of(row, base["template"], base["template_from"], answer),
                "id": who,
            }
            record["moved"] = base["moved"]
            if record["lack"] == "no_head_node" and row not in HEADLESS:
                # Not one of the rows known to have none: said with how long
                # it was given, since the tool cannot tell a model that is
                # late from one with no such node (the plugin's answer is the
                # same for both: the engine gave no head position).
                record["waited"] = self.waited()
                record["problem"] = (
                    f"no head node in {record['waited']:.0f} s, and not a row known to have none: "
                    "not set up in that time, or no such node; ask for the row again"
                )
            if row in self.row_cameras:
                # The camera is the tree's table's row, given by hand: what the
                # plugin will frame it by once it is built with that table.
                record["framed_by"] = "row"
                record["camera_from"] = "the tree's table"
            if answer.get("appearance") not in (None, row):
                record["problem"] = (
                    f"the template's creature has appearance {answer.get('appearance')}, not {row}; "
                    + record["problem"]
                ).rstrip("; ")
            small = None
            if answer.get("shown") and self.picture is not None:
                self.crops[row], small = self.shot(record)
            return self.keep_record(record, small)
        finally:
            self.away(who, shown)

    def template(self, template: str, source: str) -> dict:
        """One template of an add-in, whose row only the game can say: spawned and asked,
        and pictured if no creature of its appearance has been in this sweep. Its record
        has the appearance and the label the game answered; one of an appearance already
        pictured says which template that was, and has no cell."""
        stood = self.spawn(
            {"row": 0, "template": template, "template_from": source, "label": ""}
        )
        if stood is None:
            return self.records[-1]
        who, base = stood
        shown = False
        try:
            answer = self.ask(who, self.camera)
            if "error" in answer:
                return self.keep_record(
                    {**base, "id": who, "error": str(answer["error"])[:300]}
                )
            shown = True
            row = int(answer.get("appearance") or 0)
            record = {
                **record_of(row, template, source, answer),
                "id": who,
                "moved": base["moved"],
            }
            if row in self.pictured:
                record["same_as"] = self.pictured[row]
                self.records.append(record)
                self.say(f"{row} {template}: as {self.pictured[row]}")
                return record
            self.pictured[row] = template
            small = None
            if answer.get("shown") and self.picture is not None:
                self.crops[row], small = self.shot(record)
            return self.keep_record(record, small)
        finally:
            self.away(who, shown)

    def search(
        self, row: int, count: int, step: float | None
    ) -> list[tuple[dict, object]]:
        """One row's creature, rendered over a grid of asides and ups round a start.

        The start is the camera given, and for what of it is not given the
        one the rule (or the row's own framing) answers. `step` is metres
        between cells, a sixth of the picture's width where none is given.
        The cells, with the highest up in the first line and the aside
        growing to the right; each with its record.
        """
        stood = self.stand(row)
        if stood is None:
            return []
        who, base = stood
        cells: list[tuple[dict, object]] = []
        shown = False
        try:
            first = self.ask(who, self.camera, seconds=300, headless=row in HEADLESS)
            if "error" in first or first.get("source") != "rendered":
                problem = (
                    first.get("error") or first.get("problem") or "it is not rendered"
                )
                self.keep_record(
                    {
                        **base,
                        "id": who,
                        "error": f"nothing to search: {str(problem)[:300]}",
                    }
                )
                shown = "error" not in first
                return []
            shown = True
            start = first.get("camera") or {}
            step = step or round(0.0875 * float(start.get("distance") or 1.0), 3)
            for up in reversed(grid(float(start.get("zAdjust") or 0.0), step, count)):
                for aside in grid(float(start.get("xAdjust") or 0.0), step, count):
                    answer = self.ask(
                        who,
                        {**self.camera, "xAdjust": aside, "zAdjust": up},
                        seconds=300,
                        headless=row in HEADLESS,
                    )
                    if "error" in answer:
                        self.keep_record(
                            {**base, "id": who, "error": str(answer["error"])[:300]}
                        )
                        return cells
                    record = {
                        **record_of(
                            row, base["template"], base["template_from"], answer
                        ),
                        "id": who,
                    }
                    small = None
                    if answer.get("shown") and self.picture is not None:
                        _, small = self.shot(record)
                    self.records.append(record)
                    cells.append((record, small))
                    self.say(f"x {aside:g} z {up:g}")
            return cells
        finally:
            self.away(who, shown)

    def keep_record(self, record: dict, small: object = None) -> dict:
        self.records.append(record)
        self.cells.append((record, small))
        self.say(
            " | ".join(part for part in caption(record) if part)
            + (f" | {record['problem'][:70]}" if record.get("problem") else "")
        )
        return record

    FIGHT_WAITS = 12

    def fight_over(self) -> bool:
        """Whether the party is out of a fight, waited for a while: a creature that was
        hostile for a moment, or stays so, begins one, and its death ends it."""
        for _ in range(self.FIGHT_WAITS):
            if self.tool("health").get("mode") != "combat":
                return True
            self.sleep(1.0)
        return False

    def run(
        self, rows: list[int], templates: list[str] | None = None, source: str = ""
    ) -> None:
        """The rows, each by its template; then an add-in's templates, each by its name."""
        steps: list[tuple[str, Callable[[], dict]]] = [
            (f"row {row}", lambda row=row: self.row(row)) for row in rows
        ]
        steps += [
            (name, lambda name=name: self.template(name, source))
            for name in templates or []
        ]
        for row, step in steps:
            record = step()
            if self.stopped:
                self.say(f"stopped: {self.stopped}")
                return
            if self.tool("health").get("mode") == "combat":
                record["fight"] = True
                self.say(f"{row} began a fight: waiting for it to end")
                if not self.fight_over():
                    self.stopped = f"a fight that {row} began does not end"
                    self.say(f"stopped: {self.stopped}")
                    return


def search_caption(record: dict) -> list[str]:
    """A search's cell: the aside and the up it was rendered with, and the rest of the camera."""
    turn = f"p{record.get('pitch') or 0:.0f} r{record.get('rotation') or 0:.0f}"
    return [
        f"x {record.get('xAdjust') or 0:g}  z {record.get('zAdjust') or 0:g}",
        f"d{record.get('distance') or 0:.2f} {turn}",
        f"{record['row']} {record.get('label') or ''}"[:30],
    ]


def listing() -> str:
    """The rows and their templates, for a reader with no game."""
    lines = [
        f"{row:>5}  {template:<28} {source:<9} {label}"
        for row, (template, label, source) in TEMPLATES.items()
    ]
    lines += [
        f"{row:>5}  {template:<28} awakening {label}"
        for row, (template, label) in AWAKENING.items()
    ]
    lines.append(
        f"{len(TEMPLATES)} rows of the campaign's and {len(AWAKENING)} of Awakening's; "
        "rows 1, 19, 21, 43, 48 and 56 have no template"
    )
    lines.append(
        "the other add-ins, swept by their templates (--addon NAME --game FOLDER):"
    )
    lines += [
        f"  {name:<11} {folder:<14} {title}: {tables}"
        for name, (folder, title, tables) in ADDONS.items()
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--list", action="store_true", help="the rows and their templates, and stop"
    )
    parser.add_argument("--rows", type=int, nargs="+", help="only these rows")
    parser.add_argument("--first", type=int, default=0, help="from this row")
    parser.add_argument("--last", type=int, default=10**9, help="to this row")
    parser.add_argument(
        "--plot",
        action="store_true",
        help="also the rows whose only template is a plot's creature",
    )
    parser.add_argument(
        "--fights",
        action="store_true",
        help="also the rows whose creature stays hostile and begins a fight (the wild sylvan)",
    )
    parser.add_argument(
        "--awakening",
        action="store_true",
        help="also Awakening's rows, in a game running it",
    )
    parser.add_argument(
        "--addon",
        choices=sorted(ADDONS),
        help="an add-in's creatures, by its templates, in a game running it (with --game)",
    )
    parser.add_argument(
        "--game",
        type=Path,
        help="the game's folder, the one with Addins in it: where --addon reads the templates' names",
    )
    parser.add_argument(
        "--from-table",
        action="store_true",
        help="give each row the camera the tree's framing table has for it, whatever the running plugin was built with",
    )
    parser.add_argument(
        "--row",
        type=int,
        help="one row, with the camera given below: a trial for the framing table",
    )
    parser.add_argument(
        "--distance", type=float, help="the camera's distance, metres (with --row)"
    )
    parser.add_argument("--z", type=float, help="zAdjust, metres up (with --row)")
    parser.add_argument("--x", type=float, help="xAdjust, metres aside (with --row)")
    parser.add_argument("--pitch", type=float, help="degrees (with --row)")
    parser.add_argument(
        "--rotation",
        type=float,
        help="degrees: the turn of a side profile (with --row)",
    )
    parser.add_argument(
        "--kind",
        choices=["person", "upright", "on_all_fours"],
        help="frame by this kind's default, the framing table's rows passed over: with --row, or for a whole sweep",
    )
    parser.add_argument(
        "--print-row",
        action="store_true",
        help="with --row and a camera: print its line for the framing table and stop; no game",
    )
    parser.add_argument(
        "--note",
        default="seen as it is",
        help="the line's last words: what of the camera was seen, and what lies between two seen",
    )
    parser.add_argument(
        "--search",
        action="store_true",
        help="with --row: a grid of asides and ups round the camera given (or the rule's), on one sheet",
    )
    parser.add_argument(
        "--grid",
        type=int,
        default=3,
        help="--search: cells a side, 3 by default (9 pictures)",
    )
    parser.add_argument(
        "--step",
        type=float,
        help="--search: metres between cells; a sixth of the picture's width by default",
    )
    parser.add_argument(
        "--where",
        default="beside the party",
        help='the line\'s place: "at the camp", "above Haven"',
    )
    parser.add_argument(
        "--point",
        type=float,
        nargs=3,
        metavar=("X", "Y", "Z"),
        help="where to spawn, in the area's own coordinates",
    )
    parser.add_argument(
        "--ahead", type=float, default=4.0, help="metres before the party to spawn at"
    )
    parser.add_argument("--keep", action="store_true", help="leave each creature alive")
    parser.add_argument(
        "--out", type=Path, default=OUT, help="where the pictures and sweep.json go"
    )
    options = parser.parse_args()

    if options.list:
        print(listing())
        return 0
    if options.print_row:
        if options.row is None or options.distance is None:
            print("--print-row wants --row and at least --distance")
            return 1
        print(table_line(printed_record(options), options.where, options.note))
        return 0
    if options.search and options.row is None:
        print("--search is of one row: give --row")
        return 1
    templates: list[str] = []
    if options.addon:
        if options.game is None or not (options.game / "Addins").is_dir():
            print("--addon wants --game, the game's folder with Addins in it")
            return 1
        templates = addon_templates(options.game, options.addon)
        if not templates:
            print(f"no creature templates listed under {ADDONS[options.addon][0]}")
            return 1
    if not 1 <= options.grid <= 7:
        print("--grid is from 1 to 7 cells a side")
        return 1
    if Image is None:
        print("Pillow is not installed (pip install pillow): the pictures need it")
        return 1
    health = http_tool("health", timeout=10)
    if "error" in health or health.get("state") != "running":
        print(
            f"the game is not running with Eluvian in it, or not answering: {json.dumps(health)[:300]}"
        )
        return 1
    if health.get("mode") == "combat":
        print(
            "the party is in a fight: the sweep wants it standing somewhere with room, outside one"
        )
        return 1
    if options.point:
        point = dict(zip("xyz", options.point))
    else:
        hero = http_tool("creature")
        # The creature itself, or under its own key, as an older Eluvian answered.
        hero = hero.get("creature", hero)
        if "position" not in hero:
            print(f"nobody is controlled to spawn beside: {json.dumps(hero)[:300]}")
            return 1
        point = spawn_point(hero, options.ahead)

    options.out.mkdir(parents=True, exist_ok=True)
    camera = camera_arguments(options)
    sweep = Sweep(
        http_tool,
        point,
        camera=camera,
        row_cameras=table_rows() if options.from_table else None,
        keep=options.keep,
    )

    if options.search:
        cells = sweep.search(options.row, options.grid, options.step)
        stem = trial_name(options.row, camera) + "-search"
        (options.out / (stem + ".json")).write_text(
            json.dumps(sweep.records, indent=1), encoding="utf-8"
        )
        if not cells:
            print("nothing was rendered to search")
            return 1
        sheet_of(
            [(search_caption(record), small) for record, small in cells], options.grid
        ).save(options.out / (stem + ".jpg"), quality=92)
        print(options.out / (stem + ".jpg"))
        print("each cell's line for the framing table (src/core/PortraitTable.h):")
        for record, _ in cells:
            print(table_line(record, options.where, options.note))
        return 0

    if templates:
        # An add-in: its templates and no rows; each appearance pictured once.
        sweep.run([], templates, options.addon)
    else:
        sweep.run(chosen_rows(options))
    if options.row is not None:
        record = sweep.records[0] if sweep.records else {}
        stem = trial_name(options.row, camera)
        (options.out / (stem + ".json")).write_text(
            json.dumps(record, indent=1), encoding="utf-8"
        )
        if options.row in sweep.crops and sweep.crops[options.row] is not None:
            sweep.crops[options.row].save(options.out / (stem + ".jpg"), quality=92)
            print(options.out / (stem + ".jpg"))
        if record.get("source") == "rendered":
            print("its line for the framing table (src/core/PortraitTable.h):")
            print(table_line(record, options.where, options.note))
        return 0 if record and not record.get("error") else 1

    (options.out / "sweep.json").write_text(
        json.dumps(sweep.records, indent=1), encoding="utf-8"
    )
    for sheet in contact_sheets(sweep.cells, options.out):
        print(sheet)
    failed = [r for r in sweep.records if r.get("error")]
    headless = [
        r["row"]
        for r in sweep.records
        if r.get("lack") == "no_head_node" and r.get("source") != "rendered"
    ]
    late = [r["row"] for r in sweep.records if r.get("waited")]
    print(
        f"{len(sweep.records)} rows: {len(sweep.records) - len(failed)} answered, {len(failed)} not"
        f" ({[r.get('row') or r.get('template') for r in failed]}); no head node: {headless}"
        + (f", of which not known to have none: {late}" if late else "")
    )
    if templates:
        print(
            f"{len(sweep.pictured)} appearances among {len(templates)} templates: "
            f"{dict(sorted(sweep.pictured.items()))}"
        )
    return 0 if sweep.records and not sweep.stopped else 1


if __name__ == "__main__":
    sys.exit(main())
