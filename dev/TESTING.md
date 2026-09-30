# Testing

Two kinds. The rules and the state machine are tested without the game, on every build. Everything that touches the engine is tested by playing and reading `turnbased.log` after. The second half of this page is a session's checklist. Nothing here has been seen in play yet (2026-09-30).

## Without the game

`src/core` has no engine in it, so it is tested whole, in `tests/`, by `tb_tests` (Catch2):

- **Scripted cases**, one behaviour each: the rules (`test_rules.cpp`), the order of turns (`test_turnorder.cpp`), the settings file (`test_settings.cpp`), keys, text, the bar's string (`test_bar.cpp`), whose portrait to get next (`test_portraits.cpp`), and the machine through short fights in a stand-in for the game (`test_machine.cpp` over `World.h`: creatures, a pause flag, a mode, and the machine's effects applied the way `src/game/Bridge.cpp` applies them).
- **Random fights** (`test_fights.cpp`), SkyrimFollowerTactics' "random sequences, with something that knows the answer". An arena makes the creatures act: AIs choose when told to and whenever a command ends; commands walk and then work; attacks kill, knock down; creatures bleed out, vanish, try to act out of turn; the player orders, ends turns, gives orders nothing comes of, opens menus; conversations interrupt; reinforcements join. Every frame and every command, eight promises are checked: only the current combatant may start or run anything of its own; the pause follows the phase; movement holds, give or take a bounded creep; one action a turn plus one refund; one turn a round each; AI turns end, fights end when enemies are down; effects are well-formed; the pause key is taken exactly while a fight holds the world. Eighty fights run with every build (about four seconds, three and a half hours of play); `tb_tests "[fights-long]"` runs a thousand. A failure names its seed and the machine's last forty lines. Floors on what they reach (turns, cuts, refunds, dispatches, conversations, joiners, deaths) fail the run if a change to the arena stops reaching a state.

What the random fights found while being written (2026-09-30), each now fixed and held by them:

- after a conversation mid-fight, creatures a cutscene had set walking went on through others' turns, the current one included;
- a creature joining the fight kept what it was doing as it joined;
- an action given back after a movement cut could be given back again, letting a creature creep on a step at a time;
- a resumed turn started afresh, with its action back and a second turn-start event.

What the first session in play found (2026-09-30), now fixed and held by scripted tests:

- a click the gate refused ("No action left", "Too far") left its order waiting, and a tenth of a second later the world ran for half a second looking for the command; every click after the action was spent did it;
- a party member's turn stayed open after its action while it had movement left, which read as a pause before the next character; ending it there is now the default (`end_turn_when_action_used = true`);
- every creature's "movement speed" read 60: the scripts' property id is the flanking angle in the shipped table; the pace now comes from the appearance table (`SpeedFactor`);
- every enemy chose nothing on its turn once camera follow was on: centring the camera on an enemy just before its AI was poked left the poke unanswered (with it off, the next enemy attacked at once); the camera now moves after the AI has chosen, and an AI that chooses nothing has what it saw logged (`chose nothing: combat state ...`);
- a stunned or pinned enemy's turn waited the condition out and then acted (the machine took the stun for an action under way): conditions now cost turns;
- cooldowns ran in real time while the game was paused for a turn: they are held for turns;
- a character the player controls carried on attacking by itself (its own AI), spending the player's turn before they chose; on the player's turns only the player's orders spend it now (the user: "turn off party AI in turn-based");
- the turn bar's portraits drew grey, since the movie asked for `img://portrait_player0` where the game's loader wants `portrait_player0.dds` (`TextureName`; `dev/images/turnorder-ingame-grey.jpg` is the bar before the fix).

```powershell
.\tools\build.ps1 -Preset core -Test          # src/core and the tests only: no DAOLib, no plugin
.\tools\build.ps1 -Preset core-cov -Coverage  # 64-bit clang-cl; per-file coverage, HTML in build\core-cov\coverage\html
python tools\mutants.py                        # break the core 51 ways, one at a time; each must fail a test
```

**Coverage** (2026-09-30): 90% of `src/core`'s regions, 89% of its lines, before the tests coverage prompted; what it showed unrun led to the arena's knockdowns, deaths mid-turn and vanishing creatures, and to a key-formatting bug (a key with no name was written `0x` and its decimal code). **Mutants** (`tools/mutants.py`): each breaks one promise (the gate letting anyone act, an action never spent, the pause never taken, the turn never passing on, a joiner's place ignored, ...), builds, and runs the suite; a survivor is a promise no test holds. 51 of 51 caught (2026-09-30). A new promise gets a mutant of its own.

A lesson from the second pass: the reach check changed the arena's random draws, and two mutants that the fights had caught survived. The refund limit and a joiner being stopped had been held only by rare sequences, which the new streams no longer reached in seeds 1-80. So a promise the random fights happen to hold gets a scripted test as well (`test_machine.cpp`: "an action stopped short is given back once a turn", "a creature that joins the fight stops what it was doing"), and the mutants are run after any change to the arena, not only to the core.

**The turn bar's movie** is checked two ways without the game. `tools/test_movie.py` (run by ctest) reads back the SWF shell `tools/movie.py` writes: the header, the stage, the tags, and the font imports byte for byte against `portraits.gfx`'s. `.\tools\snapshot.ps1` plays the built movie in Ruffle, which draws its sample fight outside the game, and saves a PNG to look at (`dev/images/turnorder-sample.png`); it needs a desktop session.

What these do not establish is the engine side: that `src/game` reads and does what the stand-in assumes. That is the checklist below.

# Testing in play

## Setting up

```powershell
.\tools\build.ps1 -Preset debug -Test
.\tools\deploy.ps1          # the game closed: turnbased.dll and DAOLib's loader into bin_ship
```

Logs and settings are in `Documents\BioWare\Dragon Age\daolib\` (on this machine under OneDrive): `turnbased.log`, `turnbased.ini`, `loader.log`, and `crash-<time>.log` if the game crashes. Set `DAOLIB_LOG_LEVEL=debug` in the environment before starting the game for the gate's refusals and every effect applied; info shows the turns.

DAOLib's probe can stay deployed beside the mod: both hook `QueueCommand` and the pause key, and chain.

## A session, step by step

Each step names what to look for; a line quoted is the log's.

1. **Start the game.** `loader.log` shows `turnbased.dll` loaded. `turnbased.log` has, in order: `turnbased 0.1.0 on DAOLib 0.1.0`, `wrote the default settings to ...` (first run only), `settings from ...`, `detoured CCObject::QueueCommand at 0x0073cd10`, `detoured GameModeController::HandleMessage(UserTogglePauseMessage) at ...`, `ready; ...`. An `error` line here means a hook failed, and the mod stays off.
2. **The toggle, outside combat.** Load a save and press `Ctrl+T`: "Turn-based combat off" floats over the hero, and `turnbased.ini` says `enabled = false`. Again: on. In a fight it must refuse: "... changed only out of combat".
3. **A fight starts.** The world freezes when combat begins; "Turn-based combat" floats over the hero. The log has `combat began: turn-based`, a line per creature (`... rank 2, dexterity 14, combat speed 4.66 m/s, 1.00 a person's`), and `the fight: N combatants; *Name 17 (party), ...`: the order, the current first.
    - Check the **pace** values: 1.00 for people, genlocks and hurlocks, more for a mabari (1.07) or a shriek (1.58), less for an ogre (0.84). 0 m/s for a creature means its appearance has no speed in the table, and it moves at a person's pace.
    - With the game's auto-pause on, `a script's ToggleGamePause during a turn-based fight: ignored` about half a second of running time in. If the game unpauses by itself early in the first fight, this is where to look.
4. **Your turn.** "Name's turn" floats in gold and that character should be selected. **Check:** is it selected like a portrait click (check 4 in `dev/DAOLIB_NEEDS.md`)? Order an attack. The log says `Name: attack (action, from player)`: the player's orders carry the game's own player-issued flag. `from engine` for a click would mean the flag is not what DAOLib read. A warning `QueueCommand called from 0x..., not one of the ten callers DAOLib knows` names a caller DAOLib's reading missed; send it. With `DAOLIB_LOG_LEVEL=debug`, each `order: ...` line (the click as explore mode took it) is followed by the command it queued, which ties orders to callers. The game runs while the swing plays, then pauses again.
    - `an order but no command while paused: running a moment for it` on every order means the engine queues orders only once the game runs. The mod copes by running it for a moment, but say so: it is worth knowing. `the order queued no command` after an order means it produced nothing at all.
    - The first order also logs `the player's orders reach the queue on the scripts' thread` (or `off`). Send which: off, the reach check below cannot run.
    - **Too far.** On a fresh turn, click an enemy well beyond 9 m and a bit: "Too far" floats, nothing moves, and the log has `Name: attack refused, 14.2 m to walk with 9.0 m left`. An enemy a step or two inside the movement is attacked. At debug, `attack on 0x...: 16.1 m away, reach 3.5 m` gives the distance and the reach: an ogre or a dragon should show more reach, a bow 45 m.
5. **Out of turn.** Select another party member and order it anything: "Not Name's turn", and nothing happens.
6. **Moving.** After the attack, click to move well past 9 m. The character stops and "Out of movement" floats; the log has `Name is out of movement (9.3 of 9.0 m)`. **Check:** it stops at once, not after its walk. Attacking something far away first should stop the walk the same way, keep the action (`the action it was moving for is kept`), and leave an attack on something in reach possible.
7. **End turn.** The pause key ends a party member's turn: `Name's turn: end turn pressed`, then `Name's turn ends: ended by the player`. It must not pause or unpause the game.
8. **An enemy's turn.** Its name floats in red; it acts once, and nobody else moves. **Check:**
    - `Genlock: attack (action, from script)` soon after its turn starts. If the log says `Genlock's turn ends: chose nothing` for every enemy, the synthetic `COMMAND_COMPLETE` is not reaching its AI (check 2 in `dev/DAOLIB_NEEDS.md`): set `ai_poke = kickstart` and try again.
    - Enemies that are not acting stand still, including right after being hit.
9. **Rounds.** `round 2: ...` after everyone's turn. A creature that joins mid-fight (reinforcements) gets `Name joins the fight (enemy, initiative 12)`.
10. **Falling.** A party member at 0 health is skipped (`...'s turn ends: fell`, or never begins). A dead enemy leaves the order at the round's end.
11. **The end.** With the last enemy down, `the turn-based fight is over after N rounds: no enemy left standing`; the game runs in real time again and leaves combat on its own.

## The turn bar

Once a fight plays as above. Build the movie (`python tools\movie.py`), deploy (`tools\deploy.ps1` puts `turnorder.swf` in `Documents\BioWare\Dragon Age\packages\core\override\`), and set `turn_bar = true` under `[feedback]` in `turnbased.ini` (read again when it changes).

1. At the main menu or on loading a save: `the turn bar's calls are answered: TB_GetView, TB_EndTurn, TB_Focus`, then `the turn bar's screen is added, and its movie found as turnorder.swf`. If it says the movie is not where the game looks, the override folder is not searched for movies: say so, and try the game's own `packages\core\override\`.
2. A fight starts: `the turn bar is shown`, and the bar is at the top of the screen, looking like `dev/images/turnorder-sample.png`. **Check:**
    - the names' text: in the game's font, or missing (the font import);
    - the party's portraits on their cards, or their initials (`GetValue` and `img://` from our movie);
    - **the enemies' portraits.** As the fight starts, a line per combatant, a tenth of a second apart, current turn first: `Hero's portrait: img://portrait_3 (the game's)` for the party, `Genlock's portrait: img://portrait_12 (rendered)` for the rest. The cards fill in the same order. Look at each: framed like the party's, or black, blank, or the wrong creature. `has no portrait` for everyone means the engine made none. A crash here names `UpdatePortrait`; set `portraits = false` under `[feedback]`, which keeps the party's, and send the crash log. Note a hitch as they render, if any (check 16 in `dev/DAOLIB_NEEDS.md`);
    - the portraits stay put while a character walks (the status line's meters change, the cards are not rebuilt);
    - it follows the turns, the current card first;
    - it hides in a conversation or a menu and comes back after, and hides when the fight ends.
3. Click a card: the camera centres on that combatant (`TB_Focus`). Click End turn on a party member's turn: the turn ends, as the pause key ends it.

## Harder cases, once the above holds

- **Inventory mid-fight**: on your turn, open the inventory and drink a potion. The menu pauses the game itself; after closing it, the potion counts as the action, and the fight carries on where it was.
- **A conversation mid-fight** (a surrender): `dialogue in the middle of the fight: standing aside until it ends`, then `back to the fight: the current turn starts again`.
- **Knockdowns and stuns** on the creature whose turn it is: it gets up (at debug, `busy with a execute effect the gate did not see; letting it run`), then acts.
- **Long fights, memory**: every refused command is freed by the mod, whoever queued it (`src/game/Gate.cpp`). Enemies' portrait renderers are freed with their creatures when the area unloads (`dev/DAOLIB_NEEDS.md`). Over a long fight the game's memory in Task Manager should stay flat. A crash soon after a refused command points at the freeing (check 1 in `dev/DAOLIB_NEEDS.md`).
- **`party = tactics`**: companions play their own turns; the hero stays selected on theirs.
- **`camera_follow = true`** (experimental): the camera should centre on whoever's turn it is. If the game crashes on the first turn with this on, turn it off and send the crash log.
- **`round_min_seconds = 6`**: the world runs on briefly at the end of short rounds, nobody acting.

## What to send back

`turnbased.log`, any `crash-*.log`, and a line on each step above that did not do what it says. The unknown `QueueCommand` callers (step 4) and the pace values (step 3) are wanted whatever else happens. They go to DAOLib's reading.
