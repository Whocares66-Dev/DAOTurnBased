# DAOTurnBased -- project context

Turn-based combat for Dragon Age: Origins, as a native plugin (`turnbased.dll`) on DAOLib, the loader and library in `extern/DAOLib`. `dev/DESIGN.md` is what the mod does and why. `dev/UI.md` is the turn bar and movement preview, made of the game's own Scaleform UI once DAOLib reaches it. `dev/DAOLIB_NEEDS.md` is what the mod needs from DAOLib, kept current as the mod grows: it is DAOLib's work list, and the DAOLib session reads it. `dev/TESTING.md` is how to test in play and what the log says.

Markdown here is not hard-wrapped: one line per paragraph or list item.

Licensed GPLv3 (`LICENSE`, the user's choice, 2026-09-30). `src/plugin/EluvianAPI.h` is Eluvian's header, MIT, as its own notice says.

## The target

DAOLib's `CLAUDE.md` has the game, the one exe build every address is from, the toolchain and where logs go. Everything is 32-bit x86; `CMakeLists.txt` refuses a 64-bit configure.

## DAOLib is an extern

`extern/DAOLib` is a git submodule (DAOLib's own repository), built with this one through `add_subdirectory(... EXCLUDE_FROM_ALL)`, which gives `DAOLib::daolib` and `add_daolib_plugin`: the way Skyrim plugins build CommonLibSSE. DAOLib's dependencies come through this repository's `vcpkg.json`.

- After cloning: `git submodule update --init --recursive` (DAOLib has a submodule of its own, `extern/DAOAddressLibrary`, since `8b027c6`).
- Since DAOLib `8b027c6` (2026-10-01) the game's addresses are IDs, looked up at run time in a file for the running build of the game: the same dll runs in GOG's game and EA's. The build packs those files into `build\<preset>\daolib\addresses` (daolib depends on them itself) and `tools/deploy.ps1` copies them to `bin_ship\daolib\addresses`; without the one for the running exe the loader loads nothing. The mod has no address of its own: what it hooks and whom it takes a command's caller for are DAOLib's IDs (`src/game/Gate.cpp`). It has run in GOG's game only. The plugin API is 2: the loader and every plugin in the game's folder must be of the same API, so a deploy across that line is agreed with the other plugins' sessions (Eluvian's) first.
- Since DAOLib's issue 26 (on its master as `b8bbe63`; the mod was built on its branch for it, `f442300` and then `d892f71` for what the mod met, commits no branch of DAOLib's has now; issue 47 here, 2026-10-05) a plugin is a folder of its own: the loader loads `bin_ship\daolib\plugins\turnbased\turnbased.dll` and no dll loose in `plugins\` (its log says where one belongs), and the turn bar's movie is served to the game from beside the dll (`turnorder.swf` there, by `add_daolib_plugin`'s `FILES`), so nothing of the mod's goes into `packages\core\override\`. `tools/deploy.ps1` copies the folder and takes out what earlier deploys left: the loose dll and pdb, the override's `turnorder.swf`, which the game's resource system would find before the plugin's own, and the two material objects of 2026-10-01 there. The API is still 2, but the loader and every plugin in the game's folder change layout in the same restart, so a deploy across this line too is agreed with the other plugins' sessions first. Seen in play 2026-10-05, the loader and both plugins changing layout in one restart: `loader.log` said `eluvian.dll: loaded`, `turnbased.dll: loaded` and `2 plugin(s)`, the deploy had taken out the loose dll, its pdb and the override's movie, `turnbased.log` said `the turn bar's screen is added, and its movie found as turnorder.swf beside the plugin`, and the turn bar was in a fight as before, every card with its portrait. The submodule is at DAOLib's master, `7a36c03` (its issues 26, 27 and 23). Its issue 27 is `dao::gui::OnFullScreen` and `LoadInto`, by which a second movie of the mod's, `turnbased_tactics.swf` beside the dll, is loaded into the game's tactics screen (`dev/UI.md`, "The tactics page's box": seen in play 2026-10-05; built on DAOLib's branch for it at `a12afc4`, which no branch of DAOLib's has now either).
- A change DAOLib needs is made in DAOLib's own repository (the sibling checkout `..\DAOLib`, with its own session and its own rules in its `CLAUDE.md`), committed and pushed there; then the submodule is moved to that commit here. To build against the sibling's working tree meanwhile, configure with `-DDAOLIB_DIR=../DAOLib`; without a submodule, CMake falls back to it.
- What the mod wants from DAOLib goes into `dev/DAOLIB_NEEDS.md`, not straight into DAOLib's tree, unless the user asks for it.

## Layout

```
src/core/     the rules and the turn state machine: no <Windows.h>, no DAOLib, no dao:: type. Tested.
src/game/     every call into the engine, through DAOLib: Bridge (observe, apply), Gate (the hooks),
              Bar (the turn bar's calls, screen and portraits), Preview (the path to the cursor), Camera,
              SettingsFile, CoSave (the mod's file in each save), TacticsBox (the box on the game's
              tactics page). Verified by playing.
src/plugin/   DAOLib_Load and the frame callback that runs the machine
ui/turnorder/ the turn bar's movie, ActionScript 2 (frame_1/DoAction.as)
ui/tactics/   the tactics page's box's movie, the same, loaded into the game's tactics screen
tests/        Catch2 over src/core: scripted cases in a stand-in for the game (World.h), and random
              fights checked against the design's promises every frame (test_fights.cpp)
tools/        build.ps1 (DAOLib's, for this tree), deploy.ps1, mutants.py (break the core on purpose),
              movie.py (build ui/ into build\ui: turnorder.swf and turnbased_tactics.swf), snapshot.ps1
              (look at one in Ruffle),
              sweep.py (who is in a fight, swept through an area by Eluvian: it launches the game),
              portraits.py (every appearance's portrait on contact sheets, each a real creature in the
              running game: how a row of src/core/PortraitTable.h is found and checked by eye),
              test_*.py, console.ps1, check-powershell.ps1
dev/          design, UI, DAOLib's needs, testing; images/ for the docs
extern/DAOLib the submodule
```

**The movies** (two since issue 46: the turn bar's and the tactics page's box's) are built outside CMake, since it needs Java and FFDec: `python tools\movie.py` writes each a SWF shell and has JPEXS FFDec (`C:\Modding\ffdec_26.3.0`, runs on the installed JDK) compile its ActionScript into it. `.\tools\snapshot.ps1` plays one in Ruffle (`C:\Modding\ruffle_2026_09_30`), which shows the turn bar's sample fight (or, with `-Movie build\ui\turnbased_tactics.swf -Width 1024 -Height 768`, the tactics box on a stand-in of its screen's bottom row), and saves a PNG. Both tools were installed 2026-09-30 at the user's go-ahead ("install it as needed"). `dev/UI.md` has how the movie talks to the plugin and to the game.

**The one architectural rule: `src/core` never includes `<Windows.h>`, links DAOLib or names a `dao::` type.** The machine takes what `src/game` observed and answers with effects for `src/game` to apply; `src/game/Bridge.cpp`'s `static_assert`s keep core's numbers (game modes, command types, the invalid object) equal to DAOLib's. Anything that can be decided without the game belongs in core, with a test.

The machine is not thread-safe by itself: `src/plugin/Plugin.cpp` holds a mutex over every call into it, and never holds it while calling the engine, since applying an effect (an AI told to choose) can queue a command that comes back through the gate.

## Building

```powershell
.\tools\build.ps1 -Preset core -Test            # src/core and its tests only: the fast loop, no DAOLib
.\tools\build.ps1 -Preset debug -Test           # turnbased.dll, DAOLib's loader, the tests
.\tools\build.ps1 -Preset debug -Format -Lint   # formatters, then clang-tidy, ruff and PSScriptAnalyzer
.\tools\deploy.ps1                              # into the game (closed); -Remove takes the plugin out
```

`-Test` runs the tests as many at a time as the build's jobs (`ctest --parallel`, since 2026-10-03: each test is a process of its own with nothing shared; 20 s became 8), as the mutants run side by side.

`build\debug\` is a game folder's worth of files: `version.dll` (DAOLib's loader), `daolib\addresses` and the plugin's folder `daolib\plugins\turnbased\` (`turnbased.dll`, its pdb and the movies, `turnorder.swf` and `turnbased_tactics.swf`; a loose `daolib\plugins\turnbased.dll` until 2026-10-05). A movie is copied there from `build\ui` on every build, whether the dll links or not (the target `turnbased_files`, DAOLib's `FILES`), so the order is `python tools\movie.py`, a build, the deploy: `tools\deploy.ps1` copies the folder as the build left it, and stops at a movie built since the last build. The format and lint targets are DAOLib's `cmake/Quality.cmake` run over this tree, with this tree's `.clang-format` and `.clang-tidy` (DAOLib's, copied); they are the plugin presets', since they read its compile database. The core presets (`core`, `core-asan`, `core-cov`) build `src/core` and `tests` alone, as SkyrimFollowerTactics' do. `core-cov` is 64-bit with clang-cl, since LLVM ships the coverage runtime for x64 only, and fetches Catch2 rather than use vcpkg's x86 one.

## Before every commit

```powershell
.\tools\build.ps1 -Preset debug -Test -Format -Lint
.\tools\build.ps1 -Preset debug-asan -Test
.\tools\build.ps1 -Preset debug-analyze
```

A change to `src/core`'s logic also runs `build\core\tests\tb_tests.exe "[fights-long]"` (three thousand five hundred random fights: a thousand as built, a thousand with a turn a bar of time, five hundred a clock with hostiles unaware and fighters out of reach, and five hundred with the party's tactics chosen and changed in them; about six minutes) and `python tools\mutants.py` (each mutant must be caught; a new promise gets a mutant). The mutants run side by side, each worker breaking its own copy of the core under `build\mutants\wN` at below normal priority: the working tree is only read, so it can be edited while they run and a stopped run leaves nothing broken (`-j N` sets how many at a time). Each mutant is tried on the scripted cases first and on the random fights only if those let it pass (since 2026-10-04: the fights were 22 s of a mutant's 30, run to their end whatever had failed; a mutant is now about 9 s), and the run says how many only the fights caught. `-Preset core-cov -Coverage` shows what the tests never run. `dev/TESTING.md` has what each found.

Last green 2026-10-05, all of it, against DAOLib `7a36c03` (its master: a plugin a folder of its own, issue 47 and DAOLib's 26; told when a full-screen interface opens and closes, and a movie of the plugin's loaded into a game screen, issue 46's box and DAOLib's 27; a blow's own reach test for `strikesAtOnce`, DAOLib's 23; since `8e09a93`): 596 tests (the Python tools' among them), 3,500 fights (a thousand as built, a thousand with a turn a bar of time, five hundred a clock with strays, and five hundred with the party's tactics chosen and changed), 774 of 774 mutants caught, ASan clean, clang-tidy clean, /analyze clean, on the tree of issue 47's plugin folder, issue 46's box on the game's tactics page and issue 49's pace together (the folder and the box seen in play 2026-10-05; issue 49's, a creature's Haste and slows in its turn's meters and its bar's pace, not yet); both movies built, the turn bar's samples seen in Ruffle and in the game (2026-10-03), the tactics box's stand-in in Ruffle and the box itself in the game (2026-10-05). Run the mutants after a change to the random fights' arena too: a change to its draws can stop the default seeds reaching a rare case (`dev/TESTING.md`).

## Writing files from the shell

The Bash tool turns `\\` into `\` and `\t`, `\b` into control characters inside a heredoc, so a path like `.\tools\build.ps1` written through one comes out broken (it happened twice, 2026-09-30). Write anything with a backslash through the Edit or Write tools.

## Style

As DAOLib's: clang-format's stock Microsoft style, lint for correctness only, `/W4 /WX`. Comments say why and what was read, with dates and where, not what the code does. A file's head says what it is for and how it fits. Aggregates are initialised whole or through a maker (`Effect::Of`), since clang-tidy fails a partial one.
