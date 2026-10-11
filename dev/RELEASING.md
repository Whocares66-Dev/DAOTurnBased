# Releasing

A release is a tag, `v<version>`, and on GitHub the zip the mod is installed from: `DAOTurnBased-<version>.zip`, the plugin's folder laid out as the game's (`tools\package.ps1`). The guide's site builds that version's pages from the tag. The process is DAOLib's (`extern\DAOLib\dev\RELEASING.md`) and Eluvian's. Written 2026-10-10, for 0.1.0 (issue 106).

## The version

`project(DAOTurnBased VERSION x.y.z)` in `CMakeLists.txt` is the one place it is written; `turnbased.log`'s first line and the zip's name take it from there. It is set on the work branch, with the rest of a release's preparation; `release.ps1` does not bump it.

What a number says is what a player can count on:

- **patch**: fixes, with the rules, the settings and the saves' file as they were.
- **minor**: something a player would call new: a rule, a part of the interface, a setting.
- **major**: something a player must do something for: a setting renamed or gone, or the mod's file in a save no longer read. Below 1.0 such a change may come in a minor, and its notes say so first.

What loader a release needs is not the version's to say but DAOLib's `kPluginApiVersion`, below.

## DAOLib

A release holds `extern\DAOLib` at one of DAOLib's own release tags, and `release.ps1` refuses at any other commit. A game has a DAOLib release's loader and address files, and nothing newer: a plugin built on a later commit may ask the address library for an ID those files lack, or be of a plugin API that loader refuses. Held at the tag, the mod needs "DAOLib x.y.z or later", which the changelog's section and the zip's `VERSION.txt` name. Later holds while `kPluginApiVersion` is the same (`extern\DAOLib\src\dao\Plugin.h`), since a plugin carries its own copy of the library and the address files only gain IDs.

So a release that needs something DAOLib's master has and its last release lacks waits for DAOLib's next release (lib-agent's, asked for in `dev/DAOLIB_NEEDS.md`).

## What the zip holds, and what it must not

All of it is the plugin's folder, `bin_ship\daolib\plugins\turnbased\`, which is the same in a build tree, in the game after `deploy.ps1` and in the zip:

- `turnbased.dll` and `turnbased.pdb`, the release build's: what the game is played on here (issue 54). The PDB is 21 MB, most of the zip's 7 MB, and is what lets a crash log name the mod's frames with file and line. The link is not incremental, with `/OPT:REF` and `/OPT:ICF`, and starts a PDB of its own each time (`CMakeLists.txt`): incremental, the DLL was 3.9 MB and its PDB 72.
- `turnorder.swf` and `turnbased_tactics.swf`, the two movies (`python tools\movie.py`).
- `override\`: the twenty files of the movement preview's rings and red marker (`ui\reticles`, the game's own art made again; in the repository and the zip at the user's word, 2026-10-10: "include them in the repo and zip") and the two of a Veil Tear's portrait (`ui\portraits`).
- `README.md`, the repository's own, with `LICENSE.txt` and `VERSION.txt`: the release, and the DAOLib it was built on with its plugin API.
- **No loader and no address files.** Those are DAOLib's zip, and a second copy here would put older address files over newer ones.
- **Nothing that says who built it.** A PDB holds the paths of its sources (`C:\project\DAOTurnBased`, `C:\vcpkg`, Visual Studio's), and the linker adds the folder of its temporary files, which is under the user's name. `tools\build.ps1` gives a build a temporary folder in its own tree, and `package.ps1` refuses a zip in which any file holds the path of the user's folder. A build made any other way (the IDE's, `cmake --build` by hand) is not one to package.

`package.ps1` also refuses a folder that is not whole: a movie missing or built since the build, or the override subfolder short of a file `CMakeLists.txt` names.

## Cutting one

1. On a work branch (`gh-<N>-...`): set the version if it changes, and add `## x.y.z` at the top of `CHANGELOG.md`: about five entries, the most important first, in a player's words. The section is the GitHub release's notes, word for word, so no link in it is relative. Move the submodule to DAOLib's tag if it is not at one.
2. Run "Before a merge to master" (`CLAUDE.md`), then `python tools\movie.py`, `.\tools\build.ps1 -Preset release -Test` and `.\tools\package.ps1`, and read the zip: its files and its `VERSION.txt`.
3. See the release build in the game, from the zip: its `turnbased` folder extracted over the game's, with the loader and address files of the DAOLib release the zip names, out of that release's own zip from GitHub (the loader that was in the game is put back after). A start, a load of a save and a fight: `loader.log` says "loaded", `turnbased.log` the version, the bar's movie and the rings' files found, and no warning. Under a claim, with no game running, as any deploy.
4. Publishing waits for the user's word: a request to prepare a release is not one to publish it.
5. Squash into `master` and do not push it: from the main tree `.\tools\release.ps1 -DryRun` and `.\tools\release.ps1` (`-Jobs 4` while someone plays). It builds the release preset and runs its tests, writes the zip, makes the annotated tag, pushes `master` and the tag in one push, and creates the release with the zip attached, a prerelease below 1.0. The one push is for the guide: GitHub Pages deploys a commit's site once, and a second deployment of the same commit is reported done and changes nothing. At 0.1.0 `master` was pushed first, its site was deployed without the tag, and the tag's own run built the 0.1.0 pages and deployed nothing; they came with the next push that built the site. Pushed together, whichever run deploys first has the tag.
6. Check the release's tag, notes, prerelease flag and one zip, and the zip's hash against `dist\`. Check that the tag's run of the Documentation workflow passed and the site's version selector has the version. A tag is never moved: a correction is the next patch.

The DLL in the zip is `release.ps1`'s own link, made in the main tree, and the one seen in step 3 was the work branch's where that was another tree: the same sources, two links.

If the build, the tests or the zip fail, nothing was tagged or pushed. If `gh release create` fails after the push, run it alone on the tag that is there: `gh release create vX.Y.Z --repo Whocares66-Dev/DAOTurnBased --verify-tag --title "DAOTurnBased X.Y.Z" --notes-file build\release-notes-X.Y.Z.md dist\DAOTurnBased-X.Y.Z.zip` (`--prerelease` below 1.0).
