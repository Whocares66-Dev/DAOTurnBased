# Turn-based combat: the design

Dragon Age: Origins fights in real time with pause. This mod makes a fight a sequence of turns: everyone in it acts one at a time, in an order rolled when the fight begins, and the world waits between actions. Outside fights nothing changes. This page is what the mod does and why, for the code (`src/core/TurnMachine.cpp` implements it phase by phase) and for someone who has not played many turn-based games.

## The words

- **Combatant**: a creature in the fight. Party members, allies that are not in the party (soldiers at Ostagar), enemies.
- **Initiative**: a number rolled for each combatant when the fight starts. Highest acts first. The order is fixed for the fight; newcomers are slotted in.
- **Turn**: one combatant acting. It has one **action** and some **movement** to spend, then passes the turn on.
- **Round**: everyone's turn, once. Round 2 starts when the last combatant in the order has had its turn.
- **Action**: the one thing a turn is for: an attack, an ability or spell, drinking a potion. One per turn for now (see "Action points"). Opening a door or a chest, picking something up and the like are free ("only combat actions ... should" end it, the user, 2026-10-01).
- **Movement**: meters a combatant may walk in its turn, spent as it goes, before or after its action or both.
- **Free**: costs nothing (switching weapon sets).
- **End turn**: giving up what is left. The game's pause key does it on a party member's turn.

## What the player sees

1. A fight starts (the game switches to combat mode). The world freezes, everyone's initiative is rolled, and the order is logged. "Turn-based combat" floats over the hero.
2. **A party member's turn.** The game stays paused and that character is selected ("Alistair's turn" in gold over them). The player gives an order exactly as in the base game: click an enemy, pick an ability, click the ground. The game unpauses while it plays out, then pauses again. The player can move and act in either order until both are spent, or press the pause key to end the turn early. Ordering anyone else gets "Not Morrigan's turn".
3. **An AI combatant's turn** (enemies, allies, and companions under the `tactics` setting). Its name floats over it (red for enemies), the game runs, its AI chooses and acts, and the turn passes. Nothing else moves meanwhile.
4. A combatant who falls loses its turns. Fallen enemies leave the order at the end of the round; fallen party members stay in it, marked down, since they can be revived. A creature that joins mid-fight (reinforcements) stops whatever it was doing and waits for its first turn: this round if its initiative place is still to come, next round if it has passed.
5. When no enemy is left standing, the fight ends, the game unpauses, and real time resumes, the hero selected again (the last turn's companion stayed selected in play, 2026-09-30). The game then leaves combat mode on its own, as it always does.

## Movement: how far

This was an open question: a constant, or stats? The answer here is **9 meters (BG3's default) times how fast the creature moves against a person**, clamped to 3-20 m, so what the game already treats as speed matters. The pace comes from the creature's appearance: its combat speed in the appearance table (`Game_CbtSpeed`) over a person's 4.66 m/s. People, genlocks and hurlocks get 9 m, a mabari 9.6, a shriek or werewolf 14.2, a dragon 13.2, an ogre 7.6. Haste and slows are effects (`EFFECT_TYPE_MOVEMENT_RATE`, a potency of 1.5 or 0.5) and will multiply it once DAOLib reads a creature's effects. The scripts' movement speed property is no use: it is the flanking angle in the shipped table (the first session in play, 2026-09-30). Dexterity can add meters too (`movement.per_dexterity`), off by default because DAO's Dexterity already carries attack and defence. `movement.rule = constant` gives everyone the same.

Movement is measured, not predicted: the distance walked while the creature is moving under its own command, frame by frame, teleports ignored. When the budget runs out mid-walk, the creature is stopped and "Out of movement" floats over it. If it was walking to an attack that it never reached, the action is given back, once a turn: it may still attack something in reach. An AI creature that runs out ends its turn.

**A player's attack that cannot reach its target this turn is refused before a step is taken**: "Too far" floats over the attacker, and the action is not spent, as BG3 refuses a red target. The reach is the game's AI's own rule (`_AI_IsTargetInMeleeRange`, `ai_main_h_2.nss`): 3.5 m, plus half the target's personal space for a big one (the appearance table's `PerSpace`, 1 to 2 for bears, drakes and the broodmother), 12 m for a huge one (a high dragon's 3), or a ranged weapon's range (the items table's `Range`: 45 m for bows, crossbows and staves, 0 for every melee weapon). The walk is the distance to the target less the reach, with half a meter's slack for the approach stopping inside it (`src/core/Rules.h`, `Reach`; `src/game/Gate.cpp`, `WalkToTarget`). It is worked out only when the player's order reaches the queue on the scripts' thread, where script commands may be called; which thread that is has not been seen, and the log says so the first time.

An AI's attack at something far away is not refused: it walks what it can and is stopped, which is how an AI closes in over a turn or two. Each later try of an AI to reach something out of reach still takes one step before it is stopped, and so may a player's where the walk could not be worked out. The once-a-turn refund bounds that: a creature can end at most the stopping distance plus a few frames' walk (a fraction of a meter) past its budget, and the random fights (`tests/test_fights.cpp`) check the bound.

Abilities and spells are not checked for reach yet: an ability's range is in its own table (`abi_base`), not read so far.

The engine's pathfinder is reachable since DAOLib `8819b7c` (`world::FindPath`, for an idle creature; built 2026-09-30, not yet seen in play): a click on the ground whose path is longer than the meters left is refused before a step ("Too far"), as an attack out of reach is, and an attack's reach is measured along the path to its target rather than a straight line. `[movement] path_check` turns it off, back to a straight line and a move that walks until its meters run out. The gold/red arc preview still needs a way to draw in the world (`dev/UI.md`, `dev/DAOLIB_NEEDS.md`).

## Time

The world runs only while someone acts. Everything that ticks in seconds ticks only then: effect durations, damage over time, cooldowns, stamina and mana regeneration. So a round of eight combatants, each acting for about a second and a half, is about twelve seconds of game time, and a 30-second cooldown comes back in two or three rounds. That keeps the base game's numbers meaning roughly what they meant.

One distortion follows: a round in a big fight lasts longer than a round in a small one, so cooldowns recover faster, per round, in big fights. `turns.round_min_seconds` evens it out by giving every round at least that much game time: if the turns ran short, the world runs on, nobody acting, before the next round. Off by default. Converting durations and cooldowns to rounds outright needs DAOLib (`dev/DAOLIB_NEEDS.md`, "Rules").

### Conditions in turns (built 2026-09-30)

The user's request after the first fights in play: a stun, measured in seconds, lasted more turns than it read as, and should be measured in turns, derived from its time, with a saving throw at the start of each turn. As BG3 and D&D do it:

- **A condition's length in turns** is its duration over a round's worth of seconds, rounded up: 6 s a round (`turns.seconds_per_round`), so the mabari howl's 5 s stun is one turn, Pinning Shot's 8.5 s paralysis two, Holy Smite's 10 s two.
- **It costs the creature its turns.** At the start of each of its turns while it lasts, a disabled creature (stunned, paralysed, asleep: `GetEffectsFlags` has `EFFECT_FLAG_DISABLE_COMBAT`) loses the turn at once, with "Stunned" over it and on its card, and the world does not run for it.
- **A save at the start of the turn** can end it early: a roll against the creature's physical or mental resistance (the attributes the game rolls its own resistances with; which one by the effect's kind), as BG3 rolls a save at the end of each turn.
- **It ends on the creature's own turn.** When its turns are spent, or the save succeeds, the mod removes the effect (`RemoveEffect`, wrapped) at the start of the creature's turn, so it acts that turn. The engine's own timer no longer matters.
- **Knockdowns and pins** are conditions like the rest (below).

Built with `condition_turns` turns per condition (1), the fallback while each disabling effect's full duration is out of reach (`dev/DAOLIB_NEEDS.md`, "Rules", item 2); saves are still to build. As built:

- a stun, paralysis, sleep, pin (Pinning Shot's root) or knockdown is seen at the next status read, every half second and at every turn start, and the creature owes that many turns; each is taken at its start, "Name loses the turn" floating, the world still;
- a creature stunned in its own turn loses the rest of it at once, and whatever was queued for it since is cleared; it still owes its turns, so a hold landing mid-turn costs the rest of that turn and the next, where one landing just before costs one. Intended (the user, 2026-09-30, through the fourth play test: "if you fuck up on your own turn, you are extra punished");
- a hold that ends before its victim's turn: run out on the clock (effects' time moves only as a round begins, so a stun shorter than a round ends there) it keeps the turns it owes, or a short stun would cost nothing; taken off by something else (a cure, a dispel; with effects' time held, any time but just after a round's aging), it gives them back (the fourth play test's addendum: a lifted stun still cost the turn);
- a pin was first built to take only the movement, the action kept, as the root's script reads (`effect_root_h`: the AI's stationary flag). In play (2026-09-30, 13:25) the engine held the pinned creature in the effect's command (`ExecuteEffect`, queued at the front as the pin landed: DAOLib's probe log); its attack queued behind and never ran, and with effects' time held the command outlasted the turn, which ended on the 12 s limit. So a pin costs the turn, as the user had put it ("turns of no action"); a knockdown too, which the engine handles the same way (not yet seen). Daze is only a penalty (`effect_daze_h`) and costs nothing; a grab (`EFFECT_TYPE_GRABBED`, ogres) is not handled yet;
- End pressed while the engine holds the creature in something the gate did not see (an effect's command) ends the turn at once; its own action it still finishes first;
- a creature found in an effect's command as its turn comes, with no condition the statuses name (an ogre's grab, or an effect not met yet), gets a grace of 2 s (`kEffectGraceMs`) and then loses the turn, the effect left to the engine; meanwhile its orders are refused ("Held"), since they would queue behind the effect and run in someone else's turn, and an AI is not told its turn has come until the effect lets go (the machine looks at the creature in the turn-start frame);
- at `condition_turns` 0 the condition keeps its own seconds, spent a round at a time while effects count rounds, and every turn that comes while it lasts is lost: the creature could not act, and its command would not end within the turn;
- when the turns are spent and the engine still holds the condition, the mod removes it at the start of the creature's turn (`EndConditions`), before its AI is told or the player handed the turn.

### Talents ask for their target (built 2026-09-30)

The user: with a target selected, a talent chosen on the quickbar went to it at once, "sometimes it allows targeting and sometimes it doesn't". The quickbar's handler uses a talent on the selected target or, for one marked `AutoTarget`, on the caster's attack target, and asks only when there is neither (DAOLib read it); a character keeps its attack target from turn to turn. In a fight the mod takes the current creature's talent and asks for its target (`GameModeExplore::RequestTarget`), as BG3 does, unless it can target only its user or is a mode (`AsksForTarget`: the ability table's `TargetType` mask and `UseType`), which the game uses on the caster. The click uses it, through the gate as any order. `pick_targets` turns it off. Not yet seen in play.

### Cooldowns in turns (built 2026-09-30)

DAO's cooldowns run in real time, paused or not: passing turns quickly left a talent cooling, and waiting brought it back (the user). An ability used is held on cooldown in the engine (3600 s, where waiting cannot reach) once its action has run, and released at the start of its user's turn that many turns later: its cooldown in the ability table over `cooldown_seconds_per_turn` (4), rounded up. At 4: Shield Bash and Pinning Shot 4 (seen in play, play test 5), the mabari howl about 10, Overwhelm about 15. An action taken with the movement spent is at something in reach, and its creature may shift its feet up to a metre to strike before "out of movement" stops it: Eleanor's Shield Bash at a soldier 2 m away was cleared 9 ms after it was let through, the action gone (the seventh play test). An ability its user was walking to use when the movement ran out never ran, so it is not held (the user, 2026-09-30: chosen, not reached, and wasted on cooldown). It was 10 until the fourth play test, where the archers' stun came back every other turn and the dog acted every other round; a stun or a pin costs its victim a whole turn however short it is, so at 4 it keeps about the share of the time it has in real time (the howl's 5 s stun on a 40 s cooldown, an eighth; the user's call, 2026-09-30). A lost turn counts. At the fight's end what still waits goes back to seconds. **The quickbar's sweep shows the turns** (the user, 2026-09-30: with 2 of 3 turns left to wait, a third recovered): at each of its user's turns a held ability is set to its turns left over the turns it was held for, on the hold's 3600 s scale, through DAOLib's `dao::abilities::SetCooldown(creature, ability, remaining, total)`; the scripts' `SetCooldown` sets both numbers as one, which showed a full sweep that never moved. That the slot draws that ratio is lib-agent's reading of the cooldown record, not yet seen on screen.

**Sustained abilities** (the ability table's `UseType` 2: Shield Wall, Powerful Swings, Flaming Weapons, Rock Armor, Stealth) cost nothing to turn on or off, and do not end the turn; turned off, one waits for its user's next turn, so it cannot be turned on, off and on again in one turn (the user's rule). Action points will put a price on them.


### Everything in turns: the overhaul still to do

The user's rule (2026-09-30): in turn-based mode everything runs on turns (or, later, action points), nothing on time. Built so far: conditions (stun, paralysis, sleep, pins) and cooldowns. Still on the game's clock, running whenever the world runs:

- **Effect durations**: buffs, debuffs, damage and healing over time (burning, poison, bleeding, regeneration potions, auras), each a duration in seconds on a `CGameEffect`.
- **Regeneration**: stamina, mana and health coming back each second, in and out of combat.
- **Sustained abilities' upkeep**: their drain, if any, per second, and their stamina or mana reserved.
- **Areas that last**: spell areas on the ground (Grease, Blizzard, Inferno), traps' areas and lures, a duration each.
- **Summons and transformations** with a duration.
- **Casting and attack timing**: an action's own animation time, which is fine as it is (the world runs while an action plays).

**Built for effects (2026-09-30, DAOLib `4fee0fa`):** every temporary effect ages through one engine function, which DAOLib hooks (`dao/Effects.h`). A fight holds that clock from its start (`HoldTime`), spends `effect_seconds_per_round` on every effect at each new round (`AgeTime`), and lets it go when the fight ends or is dropped: buffs, debuffs, damage and healing over time, timed summons and, it is thought, lasting areas count rounds. Regeneration too, since DAOLib `a42be67`: each creature's stats tick it every 2 s in combat (`UpdateRegeneration`), and `dao/Clock.h` holds and moves on effects and regeneration together; a 6 s round is three ticks. Still on the clock: timers scripts set themselves with `DelayEvent`. Not yet seen in play.

**An action's own effect gets its time** (built 2026-10-01). Effects' time is held through a fight and spent a round's worth at each round. But some actions are themselves an effect that must run out: a giant spider's Overwhelm is one effect on the spider and one on its prey, each ended by its time, and held, the pounce never ended: the world stood twelve seconds until `max_action_ms` stopped it and the turn ended "took too long" (the eighth play test). So while the acting creature is at an effect's command after its own action, effects' time runs with the world, and what it took comes off that round's worth. An effect that held a creature before its turn began still costs the turn, and a stun in one's own turn still ends it.

The direction: the world's clock stops being the fight's clock. A round (or each creature's turn) advances what belongs to its creatures by a fixed amount, as conditions and cooldowns do now: durations counted in turns and ended at the start of the owner's turn; regeneration given per turn; areas lasting so many rounds. Two ways to get there, which the DAOLib work decides between:

- **Hold the clock and step it**: the game's time scale, or its effect timers, frozen outside actions, and advanced by a round's worth at each round (`dev/DAOLIB_NEEDS.md`, "Time and input": the time scale, advancing game time without running the world).
- **Own each timer**: read and set an effect's remaining duration (`dev/DAOLIB_NEEDS.md`, "Rules": an effect's duration and remaining time), hold it as cooldowns are held, and count it down per turn; set regeneration to nothing in fights and give it per turn.

Until then, `pause_on_player_turn = true` keeps what ticks from ticking while the player chooses; off (the default, the user's choice), the world idles on and those timers run.

## Who is in the fight (built 2026-09-30)

The game says who is in combat, not with whom. In Castle Cousland Howe's men and the castle's guards fight in rooms the party has not reached, all "in combat" and within `combat_radius`: a fight took in nine enemies, the turn order filled with creatures nobody could see, and the camera went to rooms that are not drawn (the user's picture, 2026-09-30). The user did not want a rule of distance alone: it cuts out enemies who should be in the fight and takes in ones who should not.

So an enemy or ally in combat within the radius takes turns only once it is **fighting where the player can see it** (`join_by_sight`, on): it stands in a room the game shows, or it or a party member is the other's attack target. The room is the engine's own answer and the user's own rule ("suspend the ones in areas that aren't loaded"): an area is made of rooms, and the game hides every room no party member has yet seen, drawing it black and leaving its creatures inactive, even through an open door, until one of the party walks in (lib-agent's reading and play test, DAOLib `1d99f54`, `dao::world::RoomOf(creature)->Shown()`). What is seen goes by the party, not the camera, so the mod's camera going to such a room shows nothing. The first rule built here was a party member's line of sight, which is clear through that open door; it remains only for an area with no rooms, or one where the party leader's own room does not read as shown (the rooms cannot be gone by there). Until then it **waits outside the fight**, where it stands: its commands are held like a joiner's, it is not in the turn order, and nothing of it is shown. When the party sees its room, or one attacks the other, it joins as any creature joining mid-way does, with a roll of initiative. **Once in, it stays in**: stepping behind a pillar does not take a creature out of the fight. The fight is over when the last enemy in it falls; one still waiting outside is the game's again, in real time, and begins turns again when it finds the party.

**Leaving the fight.** A combatant the scan no longer finds (out of combat, out of range) has left only after four seconds of it: one scan without it is not that. One that left alive stays out, down in the order, until a scan finds it fighting again; its health alone does not bring it back. Before, the status read every frame said "alive" and brought it up again until the next scan put it down: in the user's eight fights a guard and a soldier were "up again" every three seconds for ten minutes, 311 times against 39 falls (read off Eluvian's bus), losing the turns that came while they were down.

## A shut door in the way (built 2026-10-01)

Told to attack something behind a shut door, the game ends the attack "path action required" and queues the door's use and the attack again. From close by that works: the door opens, the creature goes through and strikes. From 12 m it does not: the creature walks a few metres and stands at "use object" for good, the door shut. That is the game's own, seen the same with the mod's path check off, with turn-based off, and with `turnbased.dll` out of the game's folder (the ninth play test).

- **The player's attack with a door in the way is refused**, "A door is in the way", before a step is taken and with the action kept: the game's pathfinder is asked why there is no path (DAOLib's `Pathfind`, answer 6). Opening the door costs no action, so the player opens it and attacks. The close case the game would have managed is refused too: one rule the player can read.
- **A creature standing at a walk it does not take is stopped** after two and a half seconds, where `max_action_ms` is twelve of the world standing and the turn lost: at a move, or at the use of a door or the like, whose walk is a step of the command. On the player's turn the turn goes on, and an action that waited behind the walk and never began is given back; an AI's turn ends. An attack or a talent stands still while it swings and is not judged so.

## The AI's turn

**An action that failed without acting is tried again, once.** A command that ends in a failure that did nothing (no path, no line of sight, no room at the target: `COMMAND_FAILED_NO_SPACE_IN_MELEE_RING`, the engine's limit on how many may stand round one creature) gives its action back, and an AI is then told its turn has come once more, where its turn used to end 48 ms later with the action unused: an ally guard stood through a whole fight that way (the user, 2026-09-30, read off Eluvian's bus). Seen in play (the seventh play test, 2026-10-01, a staged crowd of seventeen): with its action back the game's own AI carries on by itself. A soldier with no room at its target at once tries to move round to it (`move to object`, which ends `COMMAND_FAILED_INVALID_PATH`, the crowd standing in the way), waits about a second, attacks again, fails again, and chooses to wait, which ends its turn. So the mod's telling it again is the fallback for an AI that queues nothing, and did not fire once in twenty such failures. That a creature with no room stands and waits is the engine's own rule, in real time too.

The game's AI is event-driven (DAOLib's `dev/TURN_BASED.md`). When a creature's command completes, the engine sends it `EVENT_TYPE_COMMAND_COMPLETE`; `rules_core.nss` runs `AI_DetermineCombatRound`, which picks the next command and queues it with `AddCommand`. There is no round. The mod turns that loop on and off:

- **Off-turn**, every command a combatant chooses is refused at the queue (`src/game/Gate.cpp`). A creature with nothing queued gets no `COMMAND_COMPLETE`, so its AI stops choosing, and nothing it does costs anything.
- **Its turn** starts with the mod sending it a `COMMAND_COMPLETE` of its own (a wait that succeeded), processed at once. The creature's own script sees it first, then `rules_core`, then its AI: custom boss AI included, which is why this is the default rather than the scripts' `EVENT_TYPE_DEBUG_KICKSTART_AI` (`turns.ai_poke`). Its command comes back through the queue, is allowed, and runs.
- When that command completes, the AI chooses again. A move is allowed while movement is left; a second action is refused, and a wait is the AI passing. After `turns.ai_follow_up_ms` with nothing more, the turn ends.
- An AI that chooses nothing within `turns.ai_think_ms` passes. More than `turns.max_commands_per_turn` commands in a turn ends it, which guards against a loop.

## The party's turn

Only the player's orders spend a party member's turn. A controlled character's own AI carries on attacking by itself in real time (`AI_DetermineCombatRound_Partial`); on the player's turns its attacks and moves are refused, so the party's AI is off in turn-based fights except under `party = tactics` (the user, 2026-09-30).

The mod selects the party member (`SetPrimaryControlled`) and waits, paused. An order arrives at the queue as the base game gives it, is checked against the turn's action and movement, and runs. Whether the engine queues an order given while paused at once, or only once the game runs, has not been seen. So the mod also watches explore mode's order handlers: if an order comes and no command follows within a tenth of a second, it runs the world for up to half a second, everyone else still held, until the command arrives. When the character is idle again, the world settles for `turns.settle_ms` (arrows land, bodies fall) and pauses for the next order. A controlled character's automatic "keep attacking" is a second action and is refused.

With `turns.party = tactics`, the companions play their turns through their tactics like the AI, and only the hero waits for the player. The hero is kept selected on the companions' turns, since the game gives a controlled character only its "keep attacking" AI (`AI_DetermineCombatRound_Partial`).

## What stays the engine's

Each of the game's 43 command types (its `commands.gda`, as DAOLib names them) has a cost (`src/core/Rules.cpp`, `Classify`):

| Cost | Types |
|---|---|
| The action, a combat action | attack, use ability (a talent, a spell), use item (a potion), fly |
| Movement | move to a point or an object, move through points, move away, follow, random walk, the player steering |
| Free, on one's own turn | use object (a door, a chest, a lever), interact, lock, unlock, give, take, pick up; switch or draw weapons, equip, unequip, drop, turn to face |
| A pass, from a script | wait: an AI choosing to do nothing ends its turn |
| Always let through | an effect executing, a death blow, jumps, barks, animations, sounds, conversations, appearing and vanishing, shops, and a wait nobody chose (the scripts' `SetActive` queues one) |

The last row is what happens to a creature, not what it chooses, and goes through at any time. A creature knocked down on its turn gets up first, then acts.

Whose a command is comes from the command itself: the player's orders carry a flag (`Command::PlayerIssued`), a script's come through `AddCommand`, and the rest are the engine's (DAOLib's reading of the queue's ten callers). A refused command is freed by the mod, since the queue always takes what it is given and no caller frees it after.

**Conversations and cutscenes** mid-fight (a surrender, a boss's speech) stand the mod aside. Their scripts move people about, so nothing is refused while they play. When combat resumes, whatever anyone was left doing stops, and the current turn goes on with what it had left: its action spent or not, its movement, and no second turn-start event.

**Menus** (inventory, character sheet) over the fight hold everything as it is. The menu pauses the game itself, and the mod picks up where it was after.

**The game's auto-pause** on combat is a script (`module_core.nss`, `EVENT_TYPE_AUTOPAUSE`) that toggles pause half a second after combat starts. It would unpause a turn-based fight, so the script command is ignored during one.

**Loading** a save or an area drops any fight without touching its objects, since they are gone.

## Turning it on and off

`Ctrl+T` turns turn-based combat on or off, only outside combat ("can't change during battle"). The answer floats over the hero and is written to `turnbased.ini`. The key is one of the game's own since 2026-10-01 (DAOLib's `dao/Input.h`): listed in the options' Controls tab under "Turn-based" as "Toggle turn-based combat", bound there like any key, and kept in the game's `KeyBindings.ini` (`TurnBasedToggle_0`). `[keys] toggle` is the key it has until the player binds another. A key the game's bindings are not known to take (the number pad, Pause, a key by its code) is polled as before and is not in the controls. The rest of the settings are in that file, which is re-read when it changes. The switch itself is in the game's Options too: a "Turn-based" section on the Game tab, headed by the game's own gold bar, with a box, "Turn-based combat", the user's call of 2026-10-01 ("not controls, gameplay tab i think, its own section w/ a toggle"), through DAOLib's `dao/Options.h`. The box is the game's own checkbox, ticked by what the setting is when the screen opens; a click changes the setting at once (not at the screen's OK) and writes `turnbased.ini`, as the key does; in a fight the click is refused, the box goes back, and its tooltip says "Changes outside a fight only".

## Settings

`Documents\BioWare\Dragon Age\daolib\turnbased.ini`, written with comments the first time the game runs with the mod; `src/core/Settings.cpp` has the text.

| Setting | Default | What |
|---|---|---|
| `[turnbased] enabled` | true | Fights are turn-based |
| `[initiative] die` | 20 | A roll of 1..die |
| `[initiative] per_dexterity` | 0.25 | Added per point of Dexterity above 10 (40 Dexterity: +7.5) |
| `[movement] rule` | speed | `speed`: meters times the creature's pace against a person's; `constant`: meters |
| `[movement] meters` | 9 | |
| `[movement] per_dexterity` | 0 | Meters per point of Dexterity above 10 |
| `[movement] min`, `max` | 3, 20 | |
| `[turns] party` | manual | `manual`: every party member is yours; `tactics`: companions use their tactics |
| `[turns] end_turn_when_action_used` | true | End a party member's turn once its action is spent. The user's choice after the first session in play (2026-09-30): a turn left open to walk felt like a pause before the next character |
| `[turns] round_min_seconds` | 0 | Pad short rounds with running time |
| `[turns] settle_ms` | 400 | The world runs on this long after an action |
| `[turns] ai_think_ms` | 1500 | An AI that has chosen nothing by then passes |
| `[turns] ai_follow_up_ms` | 500 | After its action, how long an AI has to choose a move |
| `[turns] max_action_ms` | 12000 | An action running longer is stopped |
| `[turns] max_commands_per_turn` | 6 | |
| `[turns] order_run_ms` | 0 | An order that queued nothing while paused runs the world this long. Off: in play the engine queued orders at once while paused |
| `[turns] combat_radius` | 50 | Creatures fighting within this of the party leader take turns |
| `[turns] join_by_sight` | true | Of those, only who is fighting the party: see "Who is in the fight" |
| `[turns] ai_poke` | command_complete | Or `kickstart` |
| `[feedback] floaties` | true | Whose turn, out of movement, not your turn |
| `[feedback] script_events` | true | The events below |
| `[feedback] camera_follow` | false | Experimental: centre on whoever's turn it is |
| `[feedback] turn_bar` | false | The turn order bar; off until seen in play |
| `[feedback] portraits` | true | With the bar: render portraits for combatants the game has none for |
| `[keys] toggle` | Ctrl+T | the key until bound in the game's controls |

## For script mods: events

With `script_events` on, the module receives these events (`src/core/TurnMachine.h`, `tb::events`), in a range the game's scripts do not use:

| Event | Object 0 | Int 0 | Int 1 |
|---|---|---|---|
| 77000 combat start | | combatants | |
| 77001 round start | | round | |
| 77002 turn start | whose turn | round | side: 0 party, 1 ally, 2 enemy |
| 77003 turn end | whose turn | round | |
| 77004 combat end | | rounds | |

A module script can handle them like any other event. A new script command (`IsTurnBased`, `GetTurnCreature`) is a small addition, since DAOLib can fill a free command slot. Scripts would also need a `script.ldf` declaration to call it.

## What the requested features need, and where they stand

| Request | State |
|---|---|
| Cycle through party and enemies a turn at a time | Built: `TurnMachine`, the gate |
| Whose turn and who is next, BG3's portrait bar | A prototype is built: the movie (`ui/turnorder`, `dev/images/turnorder-sample.png`), the plugin's side and the screen. Everyone's portraits come from the engine: the party's as the game has them, everyone else's rendered as the party's are (`dev/UI.md`, "Portraits"). Not yet seen in the game |
| One attack or ability per turn | Built |
| Movement per turn | Built: speed-based, measured, enforced |
| The movement arc, gold in reach, red too far | Waits on DAOLib: the cursor's world point, the pathfinder, a way to draw in the world (`dev/UI.md`) |
| On/off outside combat, not in it | Built as a key and the settings file; the Options screen waits on DAOLib |

## Also needed, and what turn-based players will expect

Filling in what a turn-based mode usually has, by how much it matters:

- **An end-turn control.** Built as the pause key, and as a button on the turn bar.
- **Knowing it is your turn.** Built as floating text and selection. The bar and a sound would be better; the game's GUI sounds are reachable from a movie (`FSCommand:PlayGUISound`).
- **Seeing what is left.** Action and movement remaining, under the bar or on the portrait. The data is in `TurnView`.
- **The camera going to whoever acts.** `camera_follow`, through DAOLib's camera (`src/game/Camera.cpp`, `src/core/Easing.h`). In the tactical view it pans to whoever acts, eased, and never turns: only the player turns it. Close in, it stays with the player's character through others' turns (following an enemy put the camera 3.8 m behind it) and comes round behind each party member as their turn begins. Not yet seen in play.
- **Fast enemy turns** (built 2026-09-30, off by default). A big fight is long when every enemy animates in full; BG3 lets you speed them up. `[turns] ai_turn_speed` (1; 0.25 to 4) is the game's speed through an AI's turn, through DAOLib's `dao::clock::SetSpeed` (`23abbc1`: the world's timer and the frame's, so logic, animation and sound together; seen at 2 in play by DAOLib, the fight twice as fast, the sound in step; the Flash UI keeps its own clock). The player's turns, conversations and everything outside a fight run at 1. The mod writes the speed only when it changes, so at 1 never, and another plugin setting it is left alone. Round padding is not sped up yet.
- **Delay.** Moving one's turn later in the round, to act after an ally. Easy in `TurnOrder`; needs a key and a place in the bar.
- **Surprise.** A party that starts a fight from stealth might get a free round, as in D&D; enemies who ambush likewise. Stealth is readable from the scripts.
- **Opportunity attacks.** Walking away from an adjacent enemy provokes a free attack in most turn-based games. Without them, melee creatures cannot hold anyone and archers kite freely. DAO has melee rings (`GetCreaturesInMeleeRing`) to detect leaving; the free attack is an `AddCommand` out of turn the gate lets through. A good second step.
- **Sustained abilities and potions.** DAO's modal abilities (toggled on, draining mana or stamina) cost the action as built; most turn-based designs make a toggle or a potion a bonus action. That wants the ability's type from its data table (`abi_base`, readable through `dao::gda`) and a second, lesser action in the budget.
- **Cooldowns and durations in rounds.** See Time above.
- **Balance.** DAO is tuned for real time: some fights throw many weak enemies, and regeneration assumes seconds pass. Expect `movement.meters`, `round_min_seconds` and initiative to want tuning in play.

## Proposed, not decided (2026-10-01)

Two questions put to the user and not yet answered. Nothing here is built unless it says so.

### Casting time (decided and built 2026-10-01)

The user's rule: "the ritual spells which take 6 s should charge for 2 turns, since 1 turn is 4 s. cast on the 3rd turn. instant & fast & normal can cast on the turn of. slow spells take 1 turn to charge."

So an ability whose conjuring time is a turn's worth or more (`cooldown_seconds_per_turn`, 4 s) charges that many of its caster's turns, rounded up, the turn it is ordered on the first, and is cast at the start of the turn after: slow (4 s) one turn and cast on the second, ritual (6 s) two and on the third. Less than a turn's worth (instant, fast 1 s, normal 2 s) is cast on the turn, as before.

- **Begun:** the action is spent and the turn ends; "Casting" over the caster. The command is not queued: the game layer keeps what it was (the ability, its target, its place).
- **A charging turn** (a ritual's second): spent on the charge, nothing to order.
- **Cast:** as its caster's turn begins the kept cast is given to it again, with a short conjuring (the turns were its time), for nothing: the caster then has its turn, its action its own. It goes on cooldown as any ability used.
- **Interrupted:** a caster held (a stun, a knockdown, paralysis, sleep), fallen or gone before then loses the charge, "Interrupted", nothing spent and nothing on cooldown; the fight's end loses it too. Damage alone does not: what the engine does in real time has not been read. This and the caster keeping its turn were not the user's words, and are theirs to change.
- **Enemies too:** an enemy's slow spell is said a turn ahead, and stunning the caster stops it.
- **Its target gone:** a cast whose target fell while it charged is not given: "Its target is gone", nothing spent, nothing on cooldown, and the turn goes on. In play it was cast at nothing and put on cooldown (the twelfth play test).

What the game's data says (`abi_base.gda`, Awakening's `abi_gxa.gda` and `spellconjuringspeeds.gda`, read with DAOLib's `tools/gda.py`): the ability table's `Speed` is a row of the conjuring speeds: 1 instant (0 s), 2 fast (1 s), 3 normal (2 s), 4 slow (4 s), 5 ritual (6 s); that it is used so is read from the names and values, not from a script. No ability of the base game or of Awakening is slow or ritual (one plot monster's summon aside), and the downloadable contents' tables are in archives the tool does not read. So by the rule nothing charges in the game as it ships; `[turns] charge = 10002:1, 13000:1` makes an ability charge so many turns whatever its conjuring time, which is also how it is tested. With action points (below) this is the same thing seen from the other side: a cost of more than a turn's points.

### A move farther than its movement (decided 2026-10-01)

Refused: "refusing too far is right i think" (the user). A click on ground whose path is longer than the movement left is refused, "Too far", before a step, as an attack out of reach is; the walk is the game's own path's length (`path_check`, DAOLib's `FindPath`), found for a creature at rest. Where the game finds no path, or with `path_check = false`, a move walks until its meters run out, as before.

## Action points (being designed, 2026-10-01; not built)

The one-action rule is a budget of one. Action points make it a budget of several, spent on things of different cost. The user's sketch, 2026-10-01, in their words: "each turn an actor should get N base action points. not sure what N should be. maybe it should represent 1s, so 4? this doesn't mean they get to attack 4x or use 4 talents. time to use ability should affects action point consumption. longer cast time = more action points. 1s cast = 1 action point, 4 s = 4 action points. i think action points should allow you to do different things. using a less health poulstice takes an action point. larger ones take more. so you can use health poulstice, drink potion, set trap, etc. and all that takes action points. attacking takes an action point; can't attack again same turn (unless maybe we allow multi-attack w/ a talent or something). talents take action points and lock out other skills so you can't spam multiple skills in a turn. don't want to force end turn after attack / talent, so things like drink potion can come after, though that would be easiest. still needs further thinking an refinement."

**What is settled by that** (the user's, unless marked):

- **4 points a turn, a point a second.** It is the number the rest already stands on: a turn is 4 s for cooldowns (`cooldown_seconds_per_turn`) and for casting time, so a slow spell (4 s) is a whole turn's points and a ritual (6 s) more than a turn's. Charging (above) is then not a rule of its own but a cost greater than a turn.
- **A cost by time.** An ability costs its conjuring time in seconds, at least 1.
- **One talent or spell a turn**: using one locks the others out for the turn, so cooldowns in turns keep their meaning. Proposed with it: sustained modes are outside the lockout, a point to turn on and nothing to turn off (they cost nothing either way today).
- **Items cost points, more for the greater**: a lesser poultice 1, larger ones more; traps, bombs and the like their own.
- **The turn is not ended by an attack or a talent**: what points are left may still be spent (a potion after the blow). The turn ends when its owner ends it, or when neither points nor movement are left.
- **Movement stays its own budget**, in meters, as now (proposed; the sketch does not fold it into the points).

**What the game's data gives for costs** (read with DAOLib's `tools/gda.py`):

- Abilities: the conjuring speeds in use are instant (0 s), fast (1 s) and normal (2 s): `abi_base`'s `Speed` is 0 for 177 rows, 1 for 330, 2 for 37, 3 for 7. So by time alone nearly every talent and spell is 1 point and a few are 2: casting time gives little to choose between them.
- Weapons: the item table (`bitm_base`) has a `Speed` class: dagger 3; longsword, mace, waraxe 2; greatsword, maul, battleaxe, staff 1; bows 0. It is readable in play as the weapon's range already is (`Table::Items`, read in the gate for an attack's reach), so a cost by weapon needs nothing new of DAOLib.
- Items: the game has no "time" for a poultice or a trap, so their costs would be a small table in `turnbased.ini` with defaults.

**Proposed to the user, and theirs to decide:**

1. **An attack's cost from its weapon's speed, and attacks repeatable while points last**: say a dagger 1, a one-handed weapon 2, a two-handed weapon, staff or bow 3. A flat "one attack, one point" has a greatsword strike as often as a dagger for far more damage, which takes away the game's balance of fast against heavy, and leaves a fighter whose talents are on cooldown spending one point of four. By weapon, a turn of plain attacks is four dagger blows, two sword blows or one of a greatsword: near what four seconds give in real time.
2. **If attacks stay one a turn, something to spend the rest on**: a dash, 1 point for a quarter more movement.
3. **Doors, chests and pickups 1 point.** They are free today only because the other choice was the whole turn.
4. **Haste and slow as a point more or fewer**: they are effects on time.
5. **No banking**: points not spent are lost at the turn's end, to begin with.
6. **Enemies on the same budget**: an AI is told to choose again after each command until its points are gone or it passes. Enemy turns get longer in real time; `ai_turn_speed` is the offset.
7. **Shown**: the points on the turn bar's caption beside the movement, and refusals that give the numbers ("needs 2, 1 left").

**Asked of the user, 2026-10-01, and not yet answered:** attacks one a turn at 1 point, or by weapon speed and repeatable; doors and chests 1 point or free; a dash or not.

**How it would be built**, in two stages: first the budget (`TurnMachine::Budget`'s `actions` becomes points), the costs (`Classify` answers a cost with its kind; the gate gives the machine the weapon's speed and the ability's conjuring time, as it gives the latter now), the one-talent lockout, the AI told again, and the caption; then the items' table, the dash and haste. The gate's hooks and the rest stay as they are.
