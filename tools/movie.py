"""Build the mod's movies from their ActionScript 2 sources.

Two of them: the turn bar's, turnorder.swf (ui/turnorder), a screen of the
mod's own; and the tactics page's box's, turnbased_tactics.swf (ui/tactics),
loaded into the game's tactics screen (dev/UI.md has both).

The game's UI is Scaleform GFx 3 (dev/UI.md), which loads a plain SWF: for a
screen "turnorder" the game asks its resource system for turnorder.swf, then
turnorder.gfx (DAOLib's dev/GUI.md). So a movie is an ordinary SWF 8, like
the game's own, built from text with no Flash authoring:

  1. this script writes a shell: the stage, the game's two fonts imported by
     name from its font library as its own movies import them ($TitleFont and
     $BodyFont from gfxfontlib.swf), and an empty script on frame 1;
  2. JPEXS FFDec compiles the movie's frame_1/DoAction.as into that script
     (its -importScript, from the command line);
  3. FFDec decompiles the result again, and the build fails unless the
     script came back, so a source the compiler dropped is not shipped.

The turn bar draws itself with ActionScript's drawing API, and the tactics
page's box is a symbol of the game's own screen, attached as the movie runs:
neither needs symbols of its own, so nothing has to be drawn by hand in a
Flash editor.

    python tools\\movie.py                      both, into build\\ui
    python tools\\movie.py turnbased_tactics    that one alone
    python tools\\movie.py --ffdec C:\\...\\ffdec  FFDec elsewhere than C:\\Modding

FFDec is https://github.com/jindrapetrik/jpexs-decompiler, run on Java; the
default place is where it was installed on this machine (2026-09-30).
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "build" / "ui"
FFDEC = Path(r"C:\Modding\ffdec_26.3.0")

SWF_VERSION = 8  # the game's movies are SWF 8 (their CFX header's version byte)
FPS = 30
FONT_LIBRARY = "gfxfontlib.swf"
FONTS = ["$TitleFont", "$BodyFont"]


@dataclass(frozen=True)
class Movie:
    """One of the mod's movies: its name, its script's folder, its stage."""

    name: str  # built as build/ui/<name>.swf, and deployed beside the plugin so
    source: Path  # holds frame_1/DoAction.as
    stage: tuple[int, int]  # the shell's, in pixels

    @property
    def out(self) -> Path:
        return OUT_DIR / f"{self.name}.swf"

    @property
    def script(self) -> Path:
        return self.source / "frame_1" / "DoAction.as"


# The turn bar is a screen of its own and places itself by its stage, which
# its script knows (tools/test_movie.py holds the two together).
TURNORDER = Movie("turnorder", REPO / "ui" / "turnorder", (1280, 720))
# The tactics page's box is loaded into the game's tactics screen and lives
# on that movie's stage, not its own: the shell's is the tactics movie's
# (tactics.gfx, read with FFDec 2026-10-05), which matters only where the
# movie is played by itself, for a look at its stand-in (tools/snapshot.ps1).
TACTICS = Movie("turnbased_tactics", REPO / "ui" / "tactics", (1024, 768))
MOVIES = (TURNORDER, TACTICS)


class Bits:
    """Big-endian bit packing, as a SWF RECT wants it."""

    def __init__(self) -> None:
        self.bits: list[int] = []

    def put(self, value: int, count: int) -> None:
        for i in range(count - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def bytes(self) -> bytes:
        padded = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(
            int("".join(map(str, padded[i : i + 8])), 2)
            for i in range(0, len(padded), 8)
        )


def rect(width_px: int, height_px: int) -> bytes:
    xmax, ymax = width_px * 20, height_px * 20  # twips
    nbits = max(xmax.bit_length(), ymax.bit_length()) + 1  # signed
    b = Bits()
    b.put(nbits, 5)
    for v in (0, xmax, 0, ymax):
        b.put(v, nbits)
    return b.bytes()


def tag(code: int, body: bytes) -> bytes:
    if len(body) < 0x3F:
        return struct.pack("<H", (code << 6) | len(body)) + body
    return struct.pack("<HI", (code << 6) | 0x3F, len(body)) + body


def shell(stage: tuple[int, int] = TURNORDER.stage) -> bytes:
    """A one-frame SWF with the fonts imported and an empty frame script."""
    tags = bytearray()
    tags += tag(69, struct.pack("<I", 0))  # FileAttributes: AS1/2, no network
    for index, font in enumerate(FONTS):
        # ImportAssets2, as portraits.gfx imports $BodyFont: the library's
        # url, two reserved bytes (1, 0), one asset, its id and name.
        body = (
            FONT_LIBRARY.encode()
            + b"\0"
            + bytes([1, 0])
            + struct.pack("<HH", 1, 1 + index)
        )
        tags += tag(71, body + font.encode() + b"\0")
    tags += tag(12, b"\0")  # DoAction: just ActionEnd, for FFDec to fill
    tags += tag(1, b"")  # ShowFrame
    tags += tag(0, b"")  # End
    rest = rect(*stage) + struct.pack("<HH", FPS << 8, 1) + bytes(tags)
    return b"FWS" + bytes([SWF_VERSION]) + struct.pack("<I", 8 + len(rest)) + rest


def empty_movie() -> bytes:
    """A one-frame SWF with nothing in it."""
    tags = tag(69, struct.pack("<I", 0)) + tag(1, b"") + tag(0, b"")
    rest = rect(1, 1) + struct.pack("<HH", FPS << 8, 1) + tags
    return b"FWS" + bytes([SWF_VERSION]) + struct.pack("<I", 8 + len(rest)) + rest


def missing(source: str, compiled: str) -> list[str]:
    """What of the source's functions and names for the plugin a decompiled
    script lacks: the turn bar's calls are TB_..., the tactics page's box's
    calls and its clips TurnBased_..."""
    wanted = re.findall(r"^function (\w+)", source, re.MULTILINE)
    wanted += re.findall(r'"((?:TB|TurnBased)_\w+)"', source)
    return sorted({name for name in wanted if name not in compiled})


def ffdec(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["java", "-jar", str(root / "ffdec-cli.jar"), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def build(root: Path, movie: Movie = TURNORDER) -> int:
    if not movie.script.exists():
        print(f"no {movie.script}")
        return 1
    out = movie.out
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        work_dir = Path(work)
        base = work_dir / "shell.swf"
        base.write_bytes(shell(movie.stage))
        scripts = work_dir / "scripts"
        shutil.copytree(movie.source, scripts)
        # Into the work folder first: a compile that fails must not leave the
        # movie of the build before in build\ui looking like this one's.
        made = work_dir / out.name
        result = ffdec(root, "-importScript", str(base), str(made), str(scripts))
        if result.returncode != 0 or not made.exists():
            print(result.stdout, result.stderr)
            print(f"FFDec could not compile {movie.name}")
            return 1
        # The script must come back out, whole: a compile error can leave
        # the frame's script empty, or short, and the build otherwise quiet.
        back = work_dir / "back"
        ffdec(root, "-export", "script", str(back), str(made))
        exported = list(back.rglob("DoAction.as"))
        text = (
            exported[0].read_text(encoding="utf-8", errors="replace")
            if exported
            else ""
        )
        lost = missing(movie.script.read_text(encoding="utf-8"), text)
        if not text or lost:
            print(result.stdout, result.stderr)
            print(
                f"the compiled {movie.name}'s script is not the source's: "
                + (", ".join(lost) if text else "it has none")
                + "; see FFDec's output above"
            )
            return 1
        shutil.copyfile(made, out)
    # Outside the game the font library is not there, and a Flash player
    # holds frame 1, script and all, until an import resolves. An empty one
    # beside the movie lets it play for a look (tools/snapshot.ps1); the game
    # has its own, and this one is never deployed.
    (out.parent / FONT_LIBRARY).write_bytes(empty_movie())
    print(f"built {out} ({out.stat().st_size} bytes)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ffdec", type=Path, default=FFDEC, help="FFDec's folder")
    parser.add_argument(
        "movies",
        nargs="*",
        metavar="movie",
        help="which to build: " + ", ".join(m.name for m in MOVIES) + " (all)",
    )
    args = parser.parse_args()
    known = {movie.name: movie for movie in MOVIES}
    unknown = [name for name in args.movies if name not in known]
    if unknown:
        print(f"no movie named {', '.join(unknown)}: there are {', '.join(known)}")
        return 1
    if not (args.ffdec / "ffdec-cli.jar").exists():
        print(f"no FFDec at {args.ffdec}; see this script's head")
        return 1
    # Every one asked for is tried, so that one's failure is not all that is
    # said of two.
    failed = 0
    for movie in MOVIES:
        if not args.movies or movie.name in args.movies:
            failed |= build(args.ffdec, movie)
    return failed


if __name__ == "__main__":
    sys.exit(main())
