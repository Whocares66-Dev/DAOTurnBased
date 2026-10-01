# DAOTurnBased -- project context

Turn-based combat for Dragon Age: Origins, as a native plugin (`turnbased.dll`) on DAOLib, the loader and library in `extern/DAOLib`. `dev/DESIGN.md` is what the mod does and why. `dev/UI.md` is the turn bar and movement preview, made of the game's own Scaleform UI once DAOLib reaches it. `dev/DAOLIB_NEEDS.md` is what the mod needs from DAOLib, kept current as the mod grows: it is DAOLib's work list, and the DAOLib session reads it. `dev/TESTING.md` is how to test in play and what the log says.

Markdown here is not hard-wrapped: one line per paragraph or list item.

Licensed GPLv3 (`LICENSE`, the user's choice, 2026-09-30). `src/plugin/EluvianAPI.h` is Eluvian's header, MIT, as its own notice says.

## The target

DAOLib's `CLAUDE.md` has the game, the one exe build every address is from, the toolchain and where logs go. Everything is 32-bit x86; `CMakeLists.txt` refuses a 64-bit configure.

## DAOLib is an extern

`extern/DAOLib` is a git submodule (DAOLib's own repository), built with this one through `add_subdirectory(... EXCLUDE_FROM_ALL)`, which gives `DAOLib::daolib` and `add_daolib_plugin`: the way Skyrim plugins build CommonLibSSE. DAOLib's dependencies come through this repository's `vcpkg.json`.

- After cloning: `git submodule update --init`.
- A change DAOLib needs is made in DAOLib's own repository (the sibling checkout `..\DAOLib`, with its own session and its own rules in its `CLAUDE.md`), committed and pushed there; then the submodule is moved to that commit here. To build against the sibling's working tree meanwhile, configure with `-DDAOLIB_DIR=../DAOLib`; without a submodule, CMake falls back to it.
- What the mod wants from DAOLib goes into `dev/DAOLIB_NEEDS.md`, not straight into DAOLib's tree, unless the user asks for it.

## Layout

```
src/core/     the rules and the turn state machine: no <Windows.h>, no DAOLib, no dao:: type. Tested.
src/game/     every call into the engine, through DAOLib: Bridge (observe, apply), Gate (the hooks),
              Bar (the turn bar's calls, screen and portraits), Camera, SettingsFile. Verified by playing.
src/plugin/   DAOLib_Load and the frame callback that runs the machine
ui/turnorder/ the turn bar's movie, ActionScript 2 (frame_1/DoAction.as)
tests/        Catch2 over src/core: scripted cases in a stand-in for the game (World.h), and random
              fights checked against the design's promises every frame (test_fights.cpp)
tools/        build.ps1 (DAOLib's, for this tree), deploy.ps1, mutants.py (break the core on purpose),
              movie.py (build ui/ into build\ui\turnorder.swf), snapshot.ps1 (look at it in Ruffle),
              test_movie.py, console.ps1, check-powershell.ps1
dev/          design, UI, DAOLib's needs, testing; images/ for the docs
extern/DAOLib the submodule
```

**The movie** is built outside CMake, since it needs Java and FFDec: `python tools\movie.py` writes a SWF shell and has JPEXS FFDec (`C:\Modding\ffdec_26.3.0`, runs on the installed JDK) compile the ActionScript into it. `.\tools\snapshot.ps1` plays the result in Ruffle (`C:\Modding\ruffle_2026_09_30`), which shows the movie's sample fight, and saves a PNG. Both tools were installed 2026-09-30 at the user's go-ahead ("install it as needed"). `dev/UI.md` has how the movie talks to the plugin and to the game.

**The one architectural rule: `src/core` never includes `<Windows.h>`, links DAOLib or names a `dao::` type.** The machine takes what `src/game` observed and answers with effects for `src/game` to apply; `src/game/Bridge.cpp`'s `static_assert`s keep core's numbers (game modes, command types, the invalid object) equal to DAOLib's. Anything that can be decided without the game belongs in core, with a test.

The machine is not thread-safe by itself: `src/plugin/Plugin.cpp` holds a mutex over every call into it, and never holds it while calling the engine, since applying an effect (an AI told to choose) can queue a command that comes back through the gate.

## Building

```powershell
.\tools\build.ps1 -Preset core -Test            # src/core and its tests only: the fast loop, no DAOLib
.\tools\build.ps1 -Preset debug -Test           # turnbased.dll, DAOLib's loader, the tests
.\tools\build.ps1 -Preset debug -Format -Lint   # formatters, then clang-tidy, ruff and PSScriptAnalyzer
.\tools\deploy.ps1                              # into the game (closed); -Remove takes the plugin out
```

`build\debug\` is a game folder's worth of files: `version.dll` (DAOLib's loader) and `daolib\plugins\turnbased.dll`. The format and lint targets are DAOLib's `cmake/Quality.cmake` run over this tree, with this tree's `.clang-format` and `.clang-tidy` (DAOLib's, copied); they are the plugin presets', since they read its compile database. The core presets (`core`, `core-asan`, `core-cov`) build `src/core` and `tests` alone, as SkyrimFollowerTactics' do. `core-cov` is 64-bit with clang-cl, since LLVM ships the coverage runtime for x64 only, and fetches Catch2 rather than use vcpkg's x86 one.

## Before every commit

```powershell
.\tools\build.ps1 -Preset debug -Test -Format -Lint
.\tools\build.ps1 -Preset debug-asan -Test
.\tools\build.ps1 -Preset debug-analyze
```

A change to `src/core`'s logic also runs `build\core\tests\tb_tests.exe "[fights-long]"` (a thousand random fights, half a minute) and `python tools\mutants.py` (each mutant must be caught; a new promise gets a mutant). `-Preset core-cov -Coverage` shows what the tests never run. `dev/TESTING.md` has what each found.

Last green 2026-10-01, all of it, against DAOLib `cb08d00`: 186 tests (the Python tools' among them), 1,000 fights, 131 of 131 mutants caught, ASan clean, clang-tidy clean, /analyze clean; the movie built and its sample seen in Ruffle. Run the mutants after a change to the random fights' arena too: a change to its draws can stop the default seeds reaching a rare case (`dev/TESTING.md`).

## Writing files from the shell

The Bash tool turns `\\` into `\` and `\t`, `\b` into control characters inside a heredoc, so a path like `.\tools\build.ps1` written through one comes out broken (it happened twice, 2026-09-30). Write anything with a backslash through the Edit or Write tools.

## Style

As DAOLib's: clang-format's stock Microsoft style, lint for correctness only, `/W4 /WX`. Comments say why and what was read, with dates and where, not what the code does. A file's head says what it is for and how it fits. Aggregates are initialised whole or through a maker (`Effect::Of`), since clang-tidy fails a partial one.
