"""Build the movement preview's rings and its red marker from the game's own.

The preview's path is marked on the ground (dev/UI.md, "The path on the
ground"): the party's selection ring made small along it, gold as far as the
movement left reaches and red beyond, and past the movement left the game's
movement marker in red at its end (the user, 2026-10-09: "replace the red walk
circles with the regular gold circle by red color instead of the enemy circle
indicator", and "the end circle with a red version of the regular gold compass
marker"). The engine colours no reticle (DAOLib's dao/Vfx.h: a tint never
showed), so each is the game's own made again: a row of the visual effects'
table names models, a model names material objects, and a material object
names a texture (lib-agent's reading, issue 72). Four kinds of file, then,
each the game's with as little changed as makes it ours:

  - the textures, DXT5. In red, every block's two colours are turned to the
    red of the enemy's ring and its alpha and its sixteen choices between
    the two left as they are, so the art is the game's to the pixel. And
    the marks' ring, gold and red, with its band widened (BAND, below): at a
    tenth of a ring's size the band is under a pixel on the screen;
  - the material objects, a few lines of XML, with their own name and their
    texture's changed;
  - the models' hierarchies (.mmh, GFF V4.0), with the material objects'
    names and their own changed where they lie: every new name is as long as
    the one it replaces, so nothing else in the file moves. The mesh and the
    animation stay the game's, named as they were;
  - the table's rows: vfx_base_turnbased.gda, read with vfx_base.gda as
    Awakening's vfx_base_gxa.gda is (every file whose name begins with the
    table's prefix, m2da_base.gda's "VFX_", is one table), holding the
    party's ring's row twice and the movement marker's once under numbers
    of our own, each naming our models.

The game finds them in the plugin's override subfolder, which DAOLib's loader
makes a source of its resources (DAOLib's issue 43). They are made here, on
the builder's machine, and are not in the repository: the art is the game's.

    python tools\\reticles.py                 into build\\art
    python tools\\reticles.py --game D:\\...   the game elsewhere (or DAO_GAME_DIR)

Then a build, which copies them there (CMakeLists.txt, OVERRIDE). A
file of the game's that is not as it was read here (2026-10-09, in GOG's
game) stops the tool with what was expected of it, and nothing is written.
"""

from __future__ import annotations

import argparse
import functools
import math
import os
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "build" / "art"
GAME = Path(
    os.environ.get(
        "DAO_GAME_DIR", r"C:\Program Files (x86)\GOG Galaxy\Games\Dragon Age Origins"
    )
)

# How many times as wide the marks' ring's band is as the game's (widened,
# below). At the art's own width a mark's band was under a pixel on the
# screen, and the owner said of the marks, 2026-10-09: "thin and faint.
# Compared to the circles under the character". Pictured that day at 1, 2
# and 3 in one scene (at 2 a line of small rings in both colours, at 3 the
# rings nearly beads), and the owner of the 2: "It looks good the way you
# have it now. No need for any configuration I think". So a constant, and
# no option or setting for it.
BAND = 2.0

# Our textures: the game's each is made from, whether it is turned red, and
# whether it is a mark's, its band widened. The marker is not widened: it is
# placed at its own size.
TEXTURES = {
    "tb_gold_selparty.dds": ("vfx_selparty.dds", False, True),
    "tb_gold_selparty_med.dds": ("vfx_selparty_med.dds", False, True),
    "tb_red_selparty.dds": ("vfx_selparty.dds", True, True),
    "tb_red_selparty_med.dds": ("vfx_selparty_med.dds", True, True),
    "tb_red_movetarget.dds": ("vfx_movetarget.dds", True, False),
    "tb_red_movearrow.dds": ("vfx_movearrow.dds", True, False),
}
# Our material objects: the game's each is, whose name is as long, and the
# texture of ours it names.
MATERIALS = {
    "tbgld": ("party", "tb_gold_selparty.dds"),
    "tbgld_med": ("party_med", "tb_gold_selparty_med.dds"),
    "tbred": ("party", "tb_red_selparty.dds"),
    "tbred_med": ("party_med", "tb_red_selparty_med.dds"),
    "tbred_movemark": ("move_indicator", "tb_red_movetarget.dds"),
    "tbred_arw": ("move_icon", "tb_red_movearrow.dds"),
}
# Our models: the game's each is, whose name is as long, and ours for each
# material object it names. The ring is three, by the size of what it is
# under (the row's Size1 to Size3): which of them an effect placed at a
# point gets is not read, so a row keeps all three.
MODELS = {
    "tb_gld": ("sr_pty", {"party": "tbgld"}),
    "tb_gld_m": ("sr_pty_m", {"party_med": "tbgld_med"}),
    "tb_gld_l": ("sr_pty_l", {"party": "tbgld"}),
    "tb_red": ("sr_pty", {"party": "tbred"}),
    "tb_red_m": ("sr_pty_m", {"party_med": "tbred_med"}),
    "tb_red_l": ("sr_pty_l", {"party": "tbred"}),
    "tb_mov": (
        "sr_mov",
        {"move_indicator": "tbred_movemark", "move_icon": "tbred_arw"},
    ),
}
# The table, ours beside it, and our rows: the game's row each is (30005
# "Party Member", 30000 "Movement Indicator") and ours for each model it
# names. The game's own rows end at 95002 and Awakening's at 111047;
# src/game/Preview.cpp places these three.
TABLE = "vfx_base.gda"
EXTENSION = "vfx_base_turnbased.gda"
ROWS = {
    1000000: (
        30005,
        {"sr_pty": "tb_red", "sr_pty_m": "tb_red_m", "sr_pty_l": "tb_red_l"},
    ),
    1000001: (30000, {"sr_mov": "tb_mov"}),
    1000002: (
        30005,
        {"sr_pty": "tb_gld", "sr_pty_m": "tb_gld_m", "sr_pty_l": "tb_gld_l"},
    ),
}
# The columns that name a model. BlendTreeName has the model's name too and
# is not one: it is the marker's animation, sr_mov.anb, which stays the
# game's.
MODEL_COLUMNS = ("ModelName", "ModelName1", "ModelName2", "ModelName3")


class Unexpected(Exception):
    """A file of the game's is not as this tool read it."""


def outputs() -> list[str]:
    """Every file the tool makes, by name."""
    return [
        *TEXTURES,
        *(f"{ours}.mao" for ours in MATERIALS),
        *(f"{ours}.mmh" for ours in MODELS),
        EXTENSION,
    ]


# -- the game's archives -----------------------------------------------------


def erf_index(path: Path) -> dict[str, tuple[int, int]]:
    """{name: (offset, length)} of an ERF V2.0 archive: a UTF-16 magic, a
    count, then for each file a name of 32 UTF-16 characters, where it begins
    and how long it is (as DAOLib's tools/script_commands.py reads one)."""
    with path.open("rb") as f:
        head = f.read(32)
        if head[:16] != "ERF V2.0".encode("utf-16-le"):
            raise Unexpected(f"{path} is not an ERF V2.0 archive")
        (count,) = struct.unpack_from("<I", head, 16)
        entries = f.read(72 * count)
    index = {}
    for at in range(0, len(entries), 72):
        name = entries[at : at + 64].decode("utf-16-le").split("\0")[0]
        index[name.lower()] = struct.unpack_from("<II", entries, at + 64)
    return index


class Game:
    """The game's files by name, looked for where the game looks: its patches
    before its data (the resource manager's PATCH: before DATA:, DAOLib's
    dao/Addresses.h), and of the patches the latest first. That last is
    taken, not read: the models are in patch002.erf alone of the patches,
    and nothing else here is in any."""

    def __init__(self, root: Path) -> None:
        core = root / "packages" / "core"
        self.archives = sorted((core / "patch").glob("*.erf"), reverse=True)
        self.archives += sorted((core / "data").glob("*.erf"))
        if not self.archives:
            raise Unexpected(f"no archives of the game's under {core}")
        self.indexes: dict[Path, dict[str, tuple[int, int]]] = {}

    def where(self, name: str) -> tuple[Path, int, int] | None:
        for archive in self.archives:
            if archive not in self.indexes:
                self.indexes[archive] = erf_index(archive)
            if name in self.indexes[archive]:
                return (archive, *self.indexes[archive][name])
        return None

    def read(self, name: str) -> bytes:
        found = self.where(name)
        if found is None:
            raise Unexpected(f"the game has no {name}")
        archive, offset, length = found
        with archive.open("rb") as f:
            f.seek(offset)
            return f.read(length)


# -- the textures ------------------------------------------------------------

# A DXT5 texture is a header of 128 bytes and then blocks of 16, each four
# texels square: two alphas and sixteen choices of three bits among the
# eight values they span, then two colours (5 bits of red, 6 of green, 5 of
# blue) and sixteen choices of two bits among the four they span. In DXT5
# the two colours' order chooses nothing. Smaller levels follow the first,
# each half the last's size; the game's reticles have none.

Texel = tuple[int, int, int, int]  # red, green, blue, alpha


def size(dds: bytes) -> tuple[int, int]:
    """A DXT5 texture's width and height, its length held to them."""
    if (
        dds[:4] != b"DDS "
        or struct.unpack_from("<I", dds, 4)[0] != 124
        or dds[84:88] != b"DXT5"
    ):
        raise Unexpected("not a DXT5 texture")
    height, width = struct.unpack_from("<II", dds, 12)
    levels = max(1, struct.unpack_from("<I", dds, 28)[0])
    blocks = sum(
        max(1, (width >> level) + 3 >> 2) * max(1, (height >> level) + 3 >> 2)
        for level in range(levels)
    )
    if len(dds) != 128 + 16 * blocks:
        raise Unexpected(
            f"a DXT5 texture of {width} by {height} with {levels} level(s) is "
            f"{128 + 16 * blocks} bytes, not {len(dds)}"
        )
    return width, height


def eight(colour: int) -> tuple[int, int, int]:
    """A block's colour, each of the three in eight bits."""
    r, g, b = colour >> 11, (colour >> 5) & 0x3F, colour & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def five_six_five(r: float, g: float, b: float) -> int:
    return round(r * 31 / 255) << 11 | round(g * 63 / 255) << 5 | round(b * 31 / 255)


def colours(first: int, second: int) -> list[tuple[int, int, int]]:
    """The four colours a block's two span, in the order its choices name."""
    one, two = eight(first), eight(second)
    return [
        one,
        two,
        *(
            tuple((a * (3 - i) + b * i) // 3 for a, b in zip(one, two, strict=True))
            for i in (1, 2)
        ),
    ]


def alphas(first: int, second: int) -> list[int]:
    """The eight alphas a block's two span, in the order its choices name."""
    if first > second:
        return [
            first,
            second,
            *((first * (7 - i) + second * i) // 7 for i in range(1, 7)),
        ]
    return [
        first,
        second,
        *((first * (5 - i) + second * i) // 5 for i in range(1, 5)),
        0,
        255,
    ]


def decode(dds: bytes) -> tuple[int, int, list[Texel]]:
    """A texture's first level: its width, its height and its texels, row by
    row."""
    width, height = size(dds)
    if width % 4 or height % 4:
        raise Unexpected(f"a texture of {width} by {height} is not whole blocks")
    texels: list[Texel] = [(0, 0, 0, 0)] * (width * height)
    at = 128
    for top in range(0, height, 4):
        for left in range(0, width, 4):
            opaque = alphas(dds[at], dds[at + 1])
            which = int.from_bytes(dds[at + 2 : at + 8], "little")
            palette = colours(*struct.unpack_from("<HH", dds, at + 8))
            (picks,) = struct.unpack_from("<I", dds, at + 12)
            for i in range(16):
                texels[(top + i // 4) * width + left + i % 4] = (
                    *palette[picks >> 2 * i & 3],
                    opaque[which >> 3 * i & 7],
                )
            at += 16
    return width, height, texels


def brightness(r: float, g: float, b: float) -> float:
    """How bright a colour is to the eye, of 255."""
    return 0.299 * r + 0.587 * g + 0.114 * b


# The red of the enemy's ring, measured 2026-10-09 on the game's own art,
# its texels more than half opaque, darkest quarter, middle and brightest:
#
#   vfx_selbad.dds, the ring under an enemy  (54, 0, 0)   (122, 0, 0)   (206, 0, 0)
#   vfx_selbad_med.dds and _large.dds        (40, 0, 0)   (100..130, 0, 0)  (206, 0, 0)
#   vfx_selbad2.dds, a targeted enemy's      (57, 23, 10) (109, 31, 25) (231, 115, 116)
#
# The ring under an enemy is red and nothing else, to 206 of 255, a sixth of
# it at that. The targeted one has a quarter of its red in green and a
# little less in blue up to a red of 190, and a pale top fifth, green and
# blue alike at half the red. This tool's first red took that pale top's
# proportion for the gold's highlights, and in the game they were pink (the
# owner: "more of a pinkish color. Is it the same red as the enemy
# circles"; its marks read (171, 101, 117) there where the ring under an
# ogre read (145, 68, 52), brightest fifth (184, 63, 56): not pale, so the
# plain ring's art). So the ramp is the plain ring's.
#
# And the gold it is laid over, the same way (soft edge, body, highlight):
#
#   vfx_selparty.dds       (99, 63, 9)    (140, 77, 4)    (206, 162, 74)
#   vfx_selparty_med.dds   (90, 62, 17)   (135, 90, 24)   (227, 191, 125)
#   vfx_movetarget.dds     (206, 146, 15) (245, 204, 106) (255, 229, 164)
#   vfx_movearrow.dds      (206, 143, 7)  (230, 168, 24)  white
#
# A gold texel goes to the red as far up the red's ramp as it is up its own
# art's, by its brightness to the eye: the art's brightest to 206, the rest
# in proportion. So the party's ring's highlight is (206, 0, 0), its body
# (109, 0, 0) where the enemy's is 122, its soft edge (84, 0, 0); and the
# marker's white heart, the enemy's art having no white, is the highlight.
RED_TOP = 206


def top_of(texels: list[Texel]) -> float:
    """The brightest of an art's texels that are more than half opaque: the
    rest are a colour spread under nothing, for the edges' sake."""
    solid = [brightness(*texel[:3]) for texel in texels if texel[3] >= 128]
    if not solid or max(solid) <= 0:
        raise Unexpected("a texture with nothing opaque in it")
    return max(solid)


def red(rgb: tuple[float, float, float], top: float) -> tuple[int, int, int]:
    """A colour of an art whose brightest is `top`, in the enemy ring's red."""
    return min(RED_TOP, round(brightness(*rgb) * RED_TOP / top)), 0, 0


def redden(dds: bytes) -> bytes:
    """A texture with its colours red: the same size, format, levels and
    alpha, and each block's choices as they were. A block's texels lie on
    the line between its two colours, and stay on it."""
    top = top_of(decode(dds)[2])
    out = bytearray(dds)
    for block in range(128, len(dds), 16):
        for at in (block + 8, block + 10):
            (colour,) = struct.unpack_from("<H", dds, at)
            struct.pack_into("<H", out, at, five_six_five(*red(eight(colour), top)))
    return bytes(out)


def block(texels: list[Texel]) -> bytes:
    """Sixteen texels as a block: its two alphas their least and greatest,
    its two colours their darkest's and brightest's, each texel's choice the
    nearest of what those span."""
    most, least = max(t[3] for t in texels), min(t[3] for t in texels)
    which = 0
    if most > least:
        for i, texel in enumerate(texels):
            # Seven steps from the greatest to the least: the two ends are
            # choices 0 and 1, and those between them 2 to 7.
            step = round((most - texel[3]) * 7 / (most - least))
            which |= (step + 1 if 0 < step < 7 else step // 7) << 3 * i
    by_brightness = sorted(texels, key=lambda t: brightness(*t[:3]))
    first, second = (
        five_six_five(*by_brightness[-1][:3]),
        five_six_five(*by_brightness[0][:3]),
    )
    one, two = eight(first), eight(second)
    along = [b - a for a, b in zip(one, two, strict=True)]
    length = sum(d * d for d in along)
    picks = 0
    if length:
        for i, texel in enumerate(texels):
            # Three steps from the first colour to the second, as the alphas.
            far = sum((c - a) * d for c, a, d in zip(texel, one, along, strict=False))
            step = min(3, max(0, round(far * 3 / length)))
            picks |= (step + 1 if 0 < step < 3 else step // 3) << 2 * i
    return (
        bytes([most, least])
        + which.to_bytes(6, "little")
        + struct.pack("<HHI", first, second, picks)
    )


def halved(
    width: int, height: int, texels: list[Texel]
) -> tuple[int, int, list[Texel]]:
    """The next smaller level: each texel the mean of the four over it, a
    colour counting for as much as it is opaque."""
    w, h = max(1, width // 2), max(1, height // 2)
    out = []
    for y in range(h):
        for x in range(w):
            four = [
                texels[min(height - 1, 2 * y + j) * width + min(width - 1, 2 * x + i)]
                for j in (0, 1)
                for i in (0, 1)
            ]
            opaque = sum(t[3] for t in four)
            weights = [t[3] for t in four] if opaque else [1] * 4
            out.append(
                (
                    *(
                        round(
                            sum(t[c] * k for t, k in zip(four, weights, strict=True))
                            / sum(weights)
                        )
                        for c in range(3)
                    ),
                    round(opaque / 4),
                )
            )
    return w, h, out


def encode(header: bytes, width: int, height: int, texels: list[Texel]) -> bytes:
    """Texels as a DXT5 texture under `header`, another texture's of this
    size, with every smaller level down to one texel made from them: the
    game's reticles have none, and a ring a few pixels across on the screen
    is drawn from the small ones."""
    out = bytearray()
    levels = 0
    while True:
        for top in range(0, height, 4):
            for left in range(0, width, 4):
                out += block(
                    [
                        texels[
                            min(height - 1, top + i // 4) * width
                            + min(width - 1, left + i % 4)
                        ]
                        for i in range(16)
                    ]
                )
        levels += 1
        if width == 1 and height == 1:
            break
        width, height, texels = halved(width, height, texels)
    head = bytearray(header[:128])
    # That it has smaller levels, and how many: the header's flags
    # (DDSD_MIPMAPCOUNT), its count, and its caps (COMPLEX and MIPMAP).
    struct.pack_into("<I", head, 8, struct.unpack_from("<I", head, 8)[0] | 0x20000)
    struct.pack_into("<I", head, 28, levels)
    struct.pack_into("<I", head, 108, struct.unpack_from("<I", head, 108)[0] | 0x400008)
    return bytes(head + out)


@functools.lru_cache(maxsize=2)
def widened(dds: bytes) -> tuple[int, int, list[Texel]]:
    """A ring's first level with its band BAND times as wide.

    The ring's art is a band about its middle, the same all the way round:
    in vfx_selparty.dds, 256 across, it begins 88 texels from the middle, is
    opaque from 97 to 107 with its pale highlight at 99 to 104, is half
    faded at 113 and gone at the texture's edge, 128. A mark is the ring at
    a tenth of its size (src/game/Preview.cpp, kMarkScale), its band a tenth
    as wide. So the band is stretched inward from where it is half faded on
    the outside, which stays where it is with the fade beyond it, the ring
    as large as it was: the texel `d` inside there is the game's at
    `d / BAND`, read between its four nearest. (At 4.5 the band would reach
    the middle, and the ring be a disc; the full-size ring's width on the
    screen would be 10.)
    """
    width, height, texels = decode(dds)
    if width != height:
        raise Unexpected(f"a ring's texture of {width} by {height} is not square")
    half = width / 2
    # Where the ring ends: the last whole texel out at which, all the way
    # round, it is still half opaque.
    rounds: dict[int, list[int]] = {}
    for y in range(height):
        for x in range(width):
            out_here = int(math.hypot(x + 0.5 - half, y + 0.5 - half))
            rounds.setdefault(out_here, []).append(texels[y * width + x][3])
    solid = [r for r, round_ in rounds.items() if sum(round_) >= 128 * len(round_)]
    if not solid:
        raise Unexpected("a ring's texture with no ring in it")
    edge = max(solid) + 1.0

    def between(x: float, y: float) -> Texel:
        # Texels' middles are at halves.
        x, y = min(width - 1.0, max(0.0, x - 0.5)), min(height - 1.0, max(0.0, y - 0.5))
        left, top = min(width - 2, int(x)), min(height - 2, int(y))
        fx, fy = x - left, y - top
        four = [texels[(top + j) * width + left + i] for j in (0, 1) for i in (0, 1)]
        share = [(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy]
        return tuple(
            round(sum(t[c] * k for t, k in zip(four, share, strict=True)))
            for c in range(4)
        )

    out = list(texels)
    for y in range(height):
        for x in range(width):
            dx, dy = x + 0.5 - half, y + 0.5 - half
            from_middle = math.hypot(dx, dy)
            if from_middle < edge:
                scale = (edge - (edge - from_middle) / BAND) / from_middle
                out[y * width + x] = between(half + dx * scale, half + dy * scale)
    return width, height, out


def texture(dds: bytes, reddened: bool, mark: bool) -> bytes:
    """One of our textures from the game's, red or as it is. The marker's
    keeps the game's blocks, choice for choice; a mark's is the ring widened
    and made into blocks again, with its smaller levels."""
    if not mark:
        return redden(dds) if reddened else dds
    width, height, texels = widened(dds)
    if reddened:
        top = top_of(decode(dds)[2])
        texels = [(*red(texel[:3], top), texel[3]) for texel in texels]
    return encode(dds, width, height, texels)


# -- the material objects ----------------------------------------------------


def swap_once(text: str, old: str, new: str, what: str) -> str:
    if text.count(old) != 1:
        raise Unexpected(f"{what} has {old} {text.count(old)} times, not once")
    return text.replace(old, new)


def material(mao: bytes, ours: str) -> bytes:
    """The game's material object under our name, naming our texture."""
    name, texture_of_ours = MATERIALS[ours]
    text = mao.decode("utf-8")
    what = f"{name}.mao"
    text = swap_once(
        text, f'<MaterialObject Name="{name}">', f'<MaterialObject Name="{ours}">', what
    )
    text = swap_once(
        text,
        f'ResName="{TEXTURES[texture_of_ours][0]}"',
        f'ResName="{texture_of_ours}"',
        what,
    )
    return text.encode("utf-8")


# -- the models --------------------------------------------------------------


def gff_string(text: str) -> bytes:
    """A string as it lies in a GFF V4.0 file: how many characters, the NUL
    that ends it among them, then UTF-16."""
    return struct.pack("<I", len(text) + 1) + (text + "\0").encode("utf-16-le")


def rename(data: bytes, names: dict[str, str], what: str) -> bytes:
    """A GFF V4.0 file with each of `names` in it replaced by a name of the
    same length, where it lies. Each must be there once as a whole string,
    in any case (sr_mov.mmh has "Move_Indicator" for move_indicator.mao): a
    file keeps a string once however many fields have it."""
    # Bytes' lower() is ASCII's: the names' letters, and in what is counted
    # with them no letter (a count under 65, and zeros).
    lowered = data.lower()
    out = bytearray(data)
    for old, new in names.items():
        if len(new) != len(old):
            raise ValueError(f"{new} is not as long as {old}")
        whole = gff_string(old.lower())
        if lowered.count(whole) != 1:
            raise Unexpected(
                f"{what} has {old} {lowered.count(whole)} times as a whole "
                "string, not once"
            )
        at = lowered.index(whole)
        out[at : at + len(whole)] = gff_string(new)
    return bytes(out)


def model(mmh: bytes, ours: str) -> bytes:
    """The game's model under our name, naming our material objects."""
    name, materials = MODELS[ours]
    return rename(mmh, {f"{name}.mmh": f"{ours}.mmh", **materials}, f"{name}.mmh")


# -- the table ---------------------------------------------------------------

# A data table is a GFF V4.0 file of type G2DA (DAOLib's tools/gda.py and
# gff4.py have the format, after xoreos): a header, three struct templates
# (the top, a column, a row) each with its fields declared, then the data:
# the top struct, two offsets; the columns, each its name's hash; the rows,
# one after another, a row's strings offsets into what follows them.
LIST_OF_STRUCTS = 0xC000  # a field's flags: a list, of structs, held in place
STRING = 14
NO_STRING = 0xFFFFFFFF
COLUMN_LIST, ROW_LIST, COLUMN_HASH, FIRST_CELL = 10002, 10003, 10001, 10005
NUMBERS = {0: "<B", 4: "<I", 5: "<i", 8: "<f"}  # the cells' types, strings apart


def column_hash(name: str) -> int:
    """What a table knows a column by (DAOLib's dao/core/GdaHash.h)."""
    return zlib.crc32(name.lower().encode("utf-16-le"))


class Table:
    """A data table's columns and rows, where they lie in its file."""

    def __init__(self, data: bytes) -> None:
        if data[:16] != b"GFF V4.0PC  G2DA":
            raise Unexpected("not a data table (a GFF V4.0 file of type G2DA)")
        self.data = data
        count, self.start = struct.unpack_from("<II", data, 20)
        templates = []
        for i in range(count):
            _, fields, at, size = struct.unpack_from("<4sIII", data, 28 + 16 * i)
            declared = {}
            for j in range(fields):
                label, kind, offset = struct.unpack_from("<III", data, at + 12 * j)
                declared[label] = (kind & 0xFFFF, kind >> 16, offset)
            templates.append((declared, size))
        top = templates[0][0]
        lists = []
        for label in (COLUMN_LIST, ROW_LIST):
            kind, flags, offset = top.get(label, (0, 0, 0))
            if flags != LIST_OF_STRUCTS:
                raise Unexpected(f"the table's field {label} is no list of structs")
            at = self.start + self.u32(self.start + offset)
            lists.append((at, *templates[kind]))
        (columns_at, column, column_size), (self.rows_at, row, self.row_size) = lists
        self.count = self.u32(self.rows_at)
        self.columns = [
            self.u32(columns_at + 4 + column_size * i + column[COLUMN_HASH][2])
            for i in range(self.u32(columns_at))
        ]
        # Each column's cell in a row: its type and where it is.
        self.cells = []
        for i in range(len(self.columns)):
            kind, flags, offset = row.get(FIRST_CELL + i, (0, 1, 0))
            if flags or (kind != STRING and kind not in NUMBERS):
                raise Unexpected(f"the table's column {i} is of no kind read here")
            self.cells.append((kind, offset))

    def u32(self, at: int) -> int:
        return struct.unpack_from("<I", self.data, at)[0]

    def string(self, at: int) -> str:
        """The string whose offset is at `at`; none is the empty one."""
        offset = self.u32(at)
        if offset == NO_STRING:
            return ""
        at = self.start + offset
        text = self.data[at + 4 : at + 4 + 2 * self.u32(at)]
        return text.decode("utf-16-le").rstrip("\0")

    def row(self, number: int) -> int:
        """Where the row of that ID lies."""
        kind, offset = self.cells[self.columns.index(column_hash("ID"))]
        for i in range(self.count):
            at = self.rows_at + 4 + self.row_size * i
            if struct.unpack_from(NUMBERS[kind], self.data, at + offset)[0] == number:
                return at
        raise Unexpected(f"the table has no row {number}")

    def values(self, number: int) -> dict[int, object]:
        """A row's cells, by their columns' hashes."""
        at = self.row(number)
        return {
            column: self.string(at + offset)
            if kind == STRING
            else struct.unpack_from(NUMBERS[kind], self.data, at + offset)[0]
            for column, (kind, offset) in zip(self.columns, self.cells, strict=True)
        }


def extension(game: bytes, rows: dict[int, tuple[int, dict[str, str]]]) -> bytes:
    """A table of the game's columns with `rows` alone: each the game's row
    it names, under its own number and naming its own models for the game's.

    All before the rows is the game's own, as it is in Awakening's
    vfx_base_gxa.gda beside vfx_base.gda: the header, the templates, the top
    struct and the columns. A row is the game's bytes with its ID and its
    strings' offsets written again; the strings follow the rows as the
    game's do, each once, each beginning on a fourth byte with 0xFF between.
    """
    table = Table(game)
    named = {column_hash(name) for name in MODEL_COLUMNS}
    identity = column_hash("ID")
    strings = bytearray()
    strings_at = table.rows_at + 4 + table.row_size * len(rows) - table.start
    placed: dict[str, int] = {}
    out = bytearray(game[: table.rows_at] + struct.pack("<I", len(rows)))
    for ours, (theirs, models) in rows.items():
        at = table.row(theirs)
        row = bytearray(game[at : at + table.row_size])
        for column, (kind, offset) in zip(table.columns, table.cells, strict=True):
            if column == identity:
                struct.pack_into(NUMBERS[kind], row, offset, ours)
            if kind != STRING:
                continue
            text = table.string(at + offset)
            if text and column in named:
                if text.lower() not in models:
                    raise Unexpected(f"row {theirs} names a model {text}")
                text = models[text.lower()]
            if text and text not in placed:
                strings += b"\xff" * (-len(strings) % 4)
                placed[text] = strings_at + len(strings)
                strings += gff_string(text)
            struct.pack_into("<I", row, offset, placed.get(text, NO_STRING))
        out += row
    return bytes(out + strings)


def table(game: bytes) -> bytes:
    """Our rows of the visual effects' table, read back and held to the
    game's: all of each as the game's but its number and its models."""
    made = extension(game, ROWS)
    named = {column_hash(name) for name in MODEL_COLUMNS}
    before, after = Table(game), Table(made)
    for ours, (theirs, models) in ROWS.items():
        want = before.values(theirs)
        want[column_hash("ID")] = ours
        for column in named:
            want[column] = models.get(str(want[column]).lower(), "")
        if after.values(ours) != want:
            raise Unexpected(f"row {ours} did not come back as row {theirs} was")
    return made


# -- all of it ---------------------------------------------------------------


def build(game_root: Path, out: Path) -> list[str]:
    """Every file made from the game at `game_root` and written to `out`:
    all of them or, with one not as expected, none. What was made, a line
    each."""
    game = Game(game_root)
    made: dict[str, tuple[bytes, str]] = {}
    for ours, (name, reddened, mark) in TEXTURES.items():
        made[ours] = (texture(game.read(name), reddened, mark), name)
    for ours, (name, _) in MATERIALS.items():
        made[f"{ours}.mao"] = (material(game.read(f"{name}.mao"), ours), f"{name}.mao")
    for ours, (name, _) in MODELS.items():
        made[f"{ours}.mmh"] = (model(game.read(f"{name}.mmh"), ours), f"{name}.mmh")
    made[EXTENSION] = (table(game.read(TABLE)), TABLE)
    for name in made:
        if game.where(name) is not None:
            raise Unexpected(f"the game has a {name} of its own")
    out.mkdir(parents=True, exist_ok=True)
    said = []
    for name, (data, source) in made.items():
        (out / name).write_bytes(data)
        archive = game.where(source)[0].relative_to(game_root)
        said.append(
            f"built {out / name} ({len(data)} bytes) from {source} in {archive}"
        )
    return said


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", type=Path, default=GAME, help="the game's folder")
    args = parser.parse_args()
    try:
        print("\n".join(build(args.game, OUT_DIR)))
    except (Unexpected, OSError) as e:
        print(f"{e}: nothing written (the game read at {args.game})")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
