# DAOTurnBased

Turn-based combat for **Dragon Age: Origins** (GOG Ultimate Edition, `DAOrigins.exe` 1.05.13263.0): a native plugin on [DAOLib](https://github.com/Whocares66-Dev/DAOLib), the game's plugin loader and reverse-engineered library.

When a fight starts, the world freezes and every combatant rolls initiative. Then each takes a turn in order, while everyone else waits. A turn is one action (attack, ability, spell, potion) and up to 9 m of movement, scaled by the creature's speed, in either order. On the party's turns the game stays paused until you give an order, as you always would. It plays out, then waits for the next. The pause key ends your turn. Enemies and allies take their turns through the game's own AI, one at a time. When the last enemy falls, real time resumes. Turn-based combat is switched on or off outside fights in the game's Options: the Game tab, under "Turn-based". A key can be given to the switch, to Delay and to End turn (Enter) in the Controls tab, under "Turn-based".

`dev/DESIGN.md` has the rules and why, and `dev/UI.md` the turn order bar, built from the game's own UI, and the movement preview still to come. `dev/TESTING.md` is how to try it and what to report. It has been played through Castle Cousland's fights (`dev/PLAYTEST-*.md`, 2026-09-30) and is still rough: expect bugs.

## Building

What DAOLib needs: Visual Studio 2022 with the C++ workload, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set, Python 3, and for formatting and linting LLVM 22+ and PSScriptAnalyzer.

```powershell
git submodule update --init                     # extern\DAOLib
.\tools\build.ps1 -Preset debug -Test           # the plugin, DAOLib's loader, the tests
.\tools\deploy.ps1                              # into the game's bin_ship (game closed)
.\tools\deploy.ps1 -Remove                      # the plugin out again
```

Settings are in `Documents\BioWare\Dragon Age\daolib\turnbased.ini`, written with comments on first run and re-read when changed. The log is `turnbased.log` beside it.

## Layout

- `src/core`: the rules and the turn state machine, with no game and no Windows, tested in `tests/`.
- `src/game`: everything that touches the engine, through DAOLib. The gate on the command queue, and the pause key.
- `src/plugin`: the entry point and the frame callback.
- `extern/DAOLib`: the library, as a submodule, the way Skyrim plugins use CommonLibSSE.
- `dev/DAOLIB_NEEDS.md`: what this mod needs from DAOLib next.
