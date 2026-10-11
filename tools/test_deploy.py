"""tools/deploy.ps1 run on a throwaway game: what goes where now a plugin is a folder.

Since DAOLib's issue 26 the loader loads plugins/turnbased/turnbased.dll and
serves the turn bar's movie from beside it; a dll loose among the plugins is
not loaded, and a movie left in the override folder would be found before the
plugin's own (issue 47). A deploy that leaves any of that as it was shows in
the game as a mod that is not there, or as a bar a version behind, so what the
script copies, what it takes out and what it leaves is checked here. And since
issue 46 there are two movies beside the dll, the second the tactics page's
box's, which goes and is held to its own sources as the first is; and since
issue 72 the movement preview's red ring and marker, which go in the folder's
override subfolder, are missed aloud, and are left loose nowhere.

Nothing of the real game is touched: the script takes its repository from
where it stands, so a copy of it runs in a made-up one (a built plugin's
folder with the movie beside the dll as a build leaves it, a loader, address
files, the movie and its sources, each a few bytes), with -Game and -Documents
naming a made-up game beside it, in the layout a deploy before 2026-10-05
left.

Each run starts a PowerShell, so a case has as few as tell what it is for,
nineteen in all, and they are Windows PowerShell's where it is there: it
starts in half the time of PowerShell 7 (0.18 s against 0.30 here), and is the
stricter of the two to write for. The script ran the same under both,
2026-10-05.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SHELL = shutil.which("powershell") or shutil.which("pwsh")
ADDRESSES = "daoaddresses-4eb19d53-00a49000-009ed2ee-00400000.bin"
# The mod's movies, as build/ui and the plugin's folder name them, each with
# the folder of its script under ui/.
BAR = "turnorder.swf"
BOX = "turnbased_tactics.swf"
SOURCES = {BAR: "turnorder", BOX: "tactics"}
# What is not the mod's and stays, whatever is deployed or removed: the game,
# another plugin in each layout, the settings and another mod's override.
LEFT = [
    "DAOrigins.exe",
    "daolib/plugins/eluvian/eluvian.dll",
    "daolib/plugins/probe.dll",
]
KEPT = ["daolib/turnbased.ini", "packages/core/override/another.swf"]
# The plugin's own folder after a deploy, the red ring's subfolder apart.
PLUGIN = ["turnbased.dll", "turnbased.pdb", BOX, BAR]
# A material object as the deploys of 2026-10-01 wrote the mod's two into the
# override folder (dda42c9, ui/override): the game's, with a comment at its
# head that says whose it is.
MATERIAL = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!-- DAOTurnBased: the game's {name} material object, with the semantic of GeneralGui.mat that takes a colour. -->
<MaterialObject Name="{name}">
\t<Material Name="GeneralGui.mat"></Material>
\t<DefaultSemantic Name="ReticalTint"></DefaultSemantic>
</MaterialObject>
"""


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def files(root: Path) -> list[str]:
    return sorted(
        p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()
    )


def said(done: subprocess.CompletedProcess[str]) -> str:
    """What a run printed, on one line: PowerShell breaks an error's long one."""
    return " ".join((done.stdout + done.stderr).replace("|", " ").split())


@unittest.skipUnless(os.name == "nt" and SHELL, "needs Windows and a PowerShell")
class Deploy(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        root = Path(folder.name)
        self.repo = root / "repo"
        self.game = root / "game" / "bin_ship"
        self.documents = root / "documents"
        self.elsewhere = root / "elsewhere"

        # The repository as the script reads it: itself, what it prints
        # with, the movies' sources, the movies, and a debug build.
        (self.repo / "tools").mkdir(parents=True)
        for name in ("deploy.ps1", "console.ps1"):
            shutil.copy(TOOLS / name, self.repo / "tools" / name)
        write(self.repo / "tools" / "movie.py", "")
        for folder in SOURCES.values():
            write(self.repo / "ui" / folder / "frame_1" / "DoAction.as", "")
        build = self.repo / "build" / "debug"
        self.built = build / "daolib" / "plugins" / "turnbased"
        write(build / "version.dll", "the loader")
        write(build / "version.pdb", "the loader's symbols")
        write(build / "daolib" / "addresses" / ADDRESSES, "addresses")
        write(self.built / "turnbased.dll", "the plugin")
        write(self.built / "turnbased.pdb", "the plugin's symbols")
        write(self.built / "turnbased.ilk", "the incremental link's")
        self.movie = self.repo / "build" / "ui" / BAR
        self.beside = self.built / BAR
        self.make("the movie")
        self.make("the box's movie", name=BOX)
        self.build()

        # The game as a deploy before 2026-10-05 left it, with another
        # plugin in each layout and another mod's override beside ours.
        self.plugins = self.game / "daolib" / "plugins"
        self.there = self.plugins / "turnbased"
        self.override = self.documents / "packages" / "core" / "override"
        write(self.game / "DAOrigins.exe", "")
        write(self.plugins / "turnbased.dll", "the plugin before")
        write(self.plugins / "turnbased.pdb", "its symbols before")
        write(self.plugins / "probe.dll", "another plugin, loose")
        write(self.plugins / "eluvian" / "eluvian.dll", "another plugin")
        write(self.override / "turnorder.swf", "the movie before")
        for name in ("move_indicator", "move_icon"):
            write(self.override / f"{name}.mao", MATERIAL.format(name=name))
        write(self.override / "another.swf", "another mod's")
        write(self.documents / "daolib" / "turnbased.ini", "settings")
        write(self.elsewhere / "precious.txt", "not the mod's")

    def make(self, text: str, older: float = -60, name: str = BAR) -> None:
        """A movie built: that many seconds older than its newest source.

        Newer by default; never of the same time, which a file time's
        rounding would make either.
        """
        movie = self.repo / "build" / "ui" / name
        write(movie, text)
        sources = [
            self.repo / "tools" / "movie.py",
            self.repo / "ui" / SOURCES[name] / "frame_1" / "DoAction.as",
        ]
        at = max(source.stat().st_mtime for source in sources) - older
        os.utime(movie, (at, at))

    def build(self) -> None:
        """What a build does with the movies: beside the dll, their times kept."""
        for name in SOURCES:
            movie = self.repo / "build" / "ui" / name
            if movie.exists():
                shutil.copy2(movie, self.built / name)

    def everything(self) -> list[str]:
        return files(self.game) + files(self.documents) + files(self.elsewhere)

    def deploy(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        done = subprocess.run(
            [
                SHELL,
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(self.repo / "tools" / "deploy.ps1"),
                "-Game",
                str(self.game),
                "-Documents",
                str(self.documents),
                *arguments,
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        # A game the script cannot place (started by another user, say) is
        # taken for this folder's, as it must be for the real one.
        if "a path that cannot be read" in said(done):
            self.skipTest("a game is running whose path cannot be read")
        return done

    def done(self, *arguments: str) -> str:
        """A run that went through, and what it said."""
        done = self.deploy(*arguments)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        return said(done)

    def refused(self, *arguments: str) -> str:
        """A run that stopped with nothing changed, and what it said."""
        before = self.everything()
        done = self.deploy(*arguments)
        self.assertNotEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.everything(), before)
        return said(done)

    def junction(self, link: Path) -> None:
        """A junction to the folder elsewhere, taken out again as a link."""
        made = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(self.elsewhere)],
            capture_output=True,
            text=True,
            check=False,
        )
        if made.returncode != 0:
            self.skipTest(f"no junction could be made: {made.stdout}{made.stderr}")
        # Before the folder it is in is cleaned up: rmdir takes the link alone.
        self.addCleanup(lambda: os.path.lexists(link) and os.rmdir(link))

    def test_a_deploy_makes_the_folder_and_takes_the_old_layout_out(self) -> None:
        # With no red ring and marker built (tools/reticles.py), which it says.
        self.assertIn("reticles.py", self.done())
        self.assertEqual(
            files(self.game),
            [
                "DAOrigins.exe",
                f"daolib/addresses/{ADDRESSES}",
                "daolib/plugins/eluvian/eluvian.dll",
                "daolib/plugins/probe.dll",
                "daolib/plugins/turnbased/turnbased.dll",
                "daolib/plugins/turnbased/turnbased.pdb",
                "daolib/plugins/turnbased/turnbased_tactics.swf",
                "daolib/plugins/turnbased/turnorder.swf",
                "version.dll",
                "version.pdb",
            ],
        )
        self.assertEqual((self.there / "turnbased.dll").read_text(), "the plugin")
        self.assertEqual((self.there / BAR).read_text(), "the movie")
        self.assertEqual((self.there / BOX).read_text(), "the box's movie")
        # The override's old movie and the mod's two material objects gone.
        self.assertEqual(files(self.documents), KEPT)

    def test_a_movie_older_than_its_source_stops_the_deploy(self) -> None:
        self.make("the movie", older=60)
        self.build()
        told = self.refused()
        self.assertIn("movie.py", told)
        self.assertIn("nothing deployed", told)
        self.done("-StaleMovie")
        self.assertEqual((self.there / "turnorder.swf").read_text(), "the movie")

    def test_a_movie_built_since_the_last_build_stops_the_deploy(self) -> None:
        # python tools\movie.py and then the deploy, with no build between:
        # the one beside the dll is the one before it.
        self.make("the movie built since")
        told = self.refused()
        self.assertIn("build.ps1", told)
        self.assertIn("nothing deployed", told)
        # The build's folder is the build's to fill: the script wrote nothing.
        self.assertEqual(self.beside.read_text(), "the movie")

    def test_with_no_movie_built_the_one_beside_the_dll_goes(self) -> None:
        # An earlier build's, which a build with no movie warns and leaves.
        self.movie.unlink()
        self.done()
        self.assertEqual((self.there / "turnorder.swf").read_text(), "the movie")

    def test_with_no_movie_at_all_the_plugin_goes_and_the_old_movie_too(self) -> None:
        self.movie.unlink()
        self.beside.unlink()
        self.assertIn("no movie to show", self.done())
        self.assertEqual(files(self.there), ["turnbased.dll", "turnbased.pdb", BOX])
        self.assertFalse((self.override / "turnorder.swf").exists())

    def test_the_tactics_boxs_movie_is_held_to_its_own_source_as_the_bars_is(
        self,
    ) -> None:
        # Older than its script, with the turn bar's as fresh as ever.
        self.make("the box's movie", older=60, name=BOX)
        self.build()
        told = self.refused()
        self.assertIn(f"{BOX} is older than", told)
        self.assertIn("nothing deployed", told)
        # Built again, and no build after it: the one beside the dll is the
        # one before.
        self.make("the box's movie built since", name=BOX)
        told = self.refused()
        self.assertIn(f"{BOX}, which was built after the last build", told)
        self.assertEqual((self.built / BOX).read_text(), "the box's movie")
        # A build, and both go.
        self.build()
        self.done()
        self.assertEqual((self.there / BOX).read_text(), "the box's movie built since")
        self.assertEqual((self.there / BAR).read_text(), "the movie")

    def test_a_stale_tactics_movie_goes_when_asked_for_as_the_folder_has_it(
        self,
    ) -> None:
        self.make("the box's movie", older=60, name=BOX)
        self.build()
        self.done("-StaleMovie")
        self.assertEqual((self.there / BOX).read_text(), "the box's movie")

    def test_with_no_tactics_movie_the_plugin_and_the_bars_movie_go(self) -> None:
        # The page then has no box, which the deploy says; the plugin says
        # it too, each time the tactics screen opens.
        (self.repo / "build" / "ui" / BOX).unlink()
        (self.built / BOX).unlink()
        told = self.done()
        self.assertIn("the tactics page has no box", told)
        self.assertNotIn("no movie to show", told)
        self.assertEqual(files(self.there), ["turnbased.dll", "turnbased.pdb", BAR])

    def test_the_previews_rings_and_marker_go_in_the_override_subfolder(
        self,
    ) -> None:
        # As a build leaves them (issue 72), in the subfolder the game reads:
        # the table of their rows, and a texture for the rest. And loose
        # beside the dll, as the issue's first build had them: one a deploy
        # of it left in the game, one its build left in the build's folder.
        # And in the game's subfolder, one under a name the tool has given up.
        art = ["tb_red_selparty.dds", "vfx_base_turnbased.gda"]
        for name in art:
            write(self.built / "override" / name, "the game's art in red")
        write(self.there / "tb_pty.mmh", "loose, of a deploy before")
        write(self.built / "tbred.mao", "loose, of a build before")
        write(self.there / "override" / "tb_pty.mmh", "of a deploy before")
        told = self.done()
        self.assertNotIn("reticles.py", told)
        self.assertIn("the layout before override", told)
        self.assertIn("the build has no such file", told)
        self.assertEqual(
            files(self.there),
            sorted([*(f"override/{name}" for name in art), *PLUGIN]),
        )

    def test_with_no_plugin_built_nothing_is_taken_out(self) -> None:
        (self.built / "turnbased.dll").unlink()
        self.assertIn("build.ps1", self.refused())

    def test_remove_takes_the_old_layout_out_and_leaves_the_rest(self) -> None:
        # A material object by one of the two names that is not the mod's:
        # another mod's override of the game's own, which stays.
        write(self.override / "move_icon.mao", "<MaterialObject/>")
        self.assertIn("does not say it is this mod's", self.done("-Remove"))
        self.assertEqual(files(self.game), LEFT)
        self.assertEqual(
            files(self.documents),
            sorted([*KEPT, "packages/core/override/move_icon.mao"]),
        )

    def test_remove_takes_the_folder_out_but_not_through_a_link_in_it(self) -> None:
        self.done()
        self.junction(self.there / "linked")
        told = self.refused("-Remove")
        self.assertIn("is a link", told)
        self.assertIn("nothing removed", told)
        # The link taken out by hand, as the refusal says: the folder goes,
        # and the loader and the address files stay for the other plugins.
        os.rmdir(self.there / "linked")
        self.done("-Remove")
        self.assertFalse(self.there.exists())
        stay = [f"daolib/addresses/{ADDRESSES}", "version.dll", "version.pdb"]
        self.assertEqual(files(self.game), sorted(LEFT + stay))
        self.assertEqual(files(self.documents), KEPT)
        self.assertEqual(files(self.elsewhere), ["precious.txt"])

    def test_remove_refuses_a_folder_that_is_a_link(self) -> None:
        self.junction(self.there)
        self.assertIn("is a link", self.refused("-Remove"))
        # Nothing removed: what the link leads to, the old layout's files.
        self.assertEqual(files(self.elsewhere), ["precious.txt"])
        self.assertTrue((self.plugins / "turnbased.dll").exists())
        self.assertTrue((self.override / "turnorder.swf").exists())


if __name__ == "__main__":
    unittest.main()
