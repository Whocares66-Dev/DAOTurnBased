# Rebalancing around turns

An investigation for [issue 1](https://github.com/Whocares66-Dev/DAOTurnBased/issues/1), "Rebalance mechanics around turns", 2026-10-01: what the game's talents, spells, skills, durations and regeneration are made of, what taking turns does to them, and a set of rules that converts them. **Nothing here is built.** `dev/DESIGN.md` is what the mod does today; this page is what it could do next. "Current specification", next, is the rules in force; the rest is how they were reached.

How to read the evidence. "Read" is from the game's own scripts (the toolset's 2,100 `.nss` files, `..\DAOLib\toolset\extract\scripts\`) or its data tables (`..\DAOLib\tools\gda.py --dump`), with the file and function named. "Measured" is from the play tests' logs. "Wiki" is dragonage.fandom.com, read through its API since the site refuses fetches. "Inferred" and "not established" mean what they say; the last section lists them. The scripts are the toolset's, with Awakening's hooks in them, and were not compared with the shipped compiled scripts.

## Current specification

The rules in force as of 2026-10-02, and only those: what to build from. Everything below this section is how they were reached, kept as their reasons; where it disagrees with this section, this section holds. **Built so far, as `[turns] clock = time`, the default since 2026-10-02 (seen in play, play tests 25 to 28; `clock = actions` is the rule before it)**: the turn's bar with Combat Tactics and rollover, actions at their own time (stand-ins where the engine's lengths are not read), walking at a pace with a free metre, an action that does not fit kept and landing on the next turn, an AI choosing while it has time, and Survival's initiative; `dev/REBALANCE_IMPLEMENTATION.md` has what and what not. Since (the fourth and fifth slices): holds by their own time, cooldowns on their user's clock, the engine's own durations for swings, shots and casts, interruption of a pending spell by a blow, a grab charged the time it runs, the time on the game's experience bar, and the fight's speed. Then (the sixth slice): the cost drawn on the experience bar, Delay, and a pending action said on hovering over its creature. Not built: effects on their owner's own clock (they age a round at a time), areas' clocks, waiting a set time. Each rule is marked by where it stands: *decided*, the user's own decision in the talk ("Decided: a turn is four seconds of time" and the section after it); *proposed*, an answer to a review that the user has had pushed but not confirmed in words ("Answers to the review concerns" and the two answer sections after it). Times are in seconds as the game has them; the core keeps them in milliseconds.

### The turn

- **A turn is a bar of 4 s** (*decided*), one setting, `seconds_per_turn`, which replaces `cooldown_seconds_per_turn`, `effect_seconds_per_round`, the casting rule's turn and, with the hold rule below, `condition_turns` (kept only as the fallback for a hold whose duration cannot be read).
- **A turn's bar** is 4 s, plus the creature's time from Combat Tactics, plus what it rolled over (*decided*). A turn begun with an action pending has that action's remaining cost as the first part of its bar, spent before anything else (*decided*).
- **A turn ends** when its bar is spent or its owner ends it (*decided*); not when an action is spent, so `end_turn_when_action_used` goes. A creature with too little left for anything it could do may still start an action that pends (below).
- **Leftover** at the turn's end rolls over: the smaller of what is left and the creature's cap (*decided*). The cap is 1 s, plus 0.5 s at Combat Tactics rank 1 and again at rank 3. A turn lost whole to a hold drops it (*decided*).
- **Combat Tactics adds time**: 0.5 s a turn at rank 2 and again at rank 4 (*decided*). **Enemies have Combat Tactics by their rank** (*decided*), as a setting: normal, weak and critter none; a lieutenant rank 2 (+0.5 s); a boss rank 4 (+1 s); an elite boss rank 4, or 1.5 s (open, below).

### The creature's own clock

- **Every creature has its own clock, which moves exactly 4 s a turn** (*proposed*), whatever its bar holds: spending, banking, passing, waiting and losing the turn all move it 4 s, and no second moves it twice. Combat Tactics' time and rollover are capacity, not a faster clock.
- **Within a turn it moves in proportion to the bar spent** (*proposed*): 4 s times what has been spent over the turn's whole bar. The ratio is fixed when the turn begins, from the whole bar, spill and rollover included; nothing spent or given back afterwards changes it. For a creature with a bar of exactly 4 s, the clock is the bar, second for second. Ending a turn early moves the clock the rest of the way to 4 s.
- **What runs on it** (*decided*): its cooldowns, which lift at the moment they run out, mid-turn included; the effects on it (buffs, debuffs, poisons, damage over time, holds), which end mid-turn as it passes; its regeneration and its modes' drains. A burn ticks every 1.5 s of its victim's clock.
- **The worked example** is in "Answers to the review concerns": a rank-four character over two turns, spending, passing and stunned.

### Actions

- **Every action costs its own time, unrounded** (*decided*): an attack its swing by the game's formula (a dagger 1.5 s, a longsword 1.9, a mace 2.0, a greatsword 2.5), times the attack speed modifier (Haste, Momentum, Swift Salve); a talent its animation; a spell its conjuring (the engine's: the start animation and its loops, "Conjuring time") and its cast; a potion or poultice its drink; a bomb its throw; a trap its crouch. A creature with a bigger bar pays the same seconds of bar; its clock moves by its ratio.
- **Until the engine gives animations' lengths**, the defaults by kind of "What no rule reaches" stand in (a talent 2 s, a knockdown 4 s, a conjuring start 0.6 s and loop 3 s).
- **An action that does not fit is begun now and lands partway into the next turn** (*decided*), whose bar starts that much spent; one needing several whole turns takes each of them whole. The command is kept, not queued, and given to its creature when its time is paid.
- **While an action is pending its creature cannot move** (*decided*). A hold, a fall or forced movement interrupts it; physical damage over the game's interruption threshold (`rules_core.nss`) interrupts a spell (*decided*).
- **No limit on actions of a kind**: different talents and spells may share a turn, and cooldowns stop the same one twice (*decided*). Poultices have no limit beside their cooldowns, and every tier costs its drink (*decided*).
- **Sustained abilities**: turning one on costs its animation's time, which follows from every action costing its own (not discussed: today it costs nothing); off is free, and a mode turned off waits for its user's next turn before it can go on again, as built.

**An action's sequence** (*proposed*):

1. **Check the order**: target, reach, what it needs. A refusal here costs nothing.
2. **Start the command**, and wait the few frames until the engine has taken it or refused it. A refusal costs nothing; the built rule for an action that failed without acting stands.
3. **Once taken, pay its time**: the creature's clock moves by its share, and the timers in that span fire one by one in the order they fall, the creature looked at after each. Only its own timers move: every other creature's clock stands.
4. **If one kills or holds the creature, its command is cleared before it lands**, only the time up to the interruption is charged, and the hold then takes its own time. What the command creates (an effect, a cooldown) starts from the moment paid, so its own time does not age it.

A pending action follows the same sequence when its time is paid, after a check at the start of its creature's turn, **before** the remainder is paid: a target gone or out of reach (an archer stepping away from a pending swing) releases the unpaid remainder; the time already spent stays spent (*proposed*).

**What is committed when** (*proposed*): time already spent stays spent and only the unpaid remainder is given back. A spell's mana is taken as it lands, as the charge rule is built, so an interrupted spell costs only its time. Its cooldown starts as it lands.

### Movement

- **Movement costs time out of the same bar** at the creature's own pace: its appearance's combat speed, times its movement rate effects (*decided*). 9 m is 1.9 s for a person. **About a metre a turn is free** (*decided*).
- **It is paid as it goes** (*proposed*): measured frame by frame, as walking is today, the clock moving as the creature walks. An attack that walks to its target pays its walk so and its swing by the sequence above on arriving.
- **A move must fit in the time left or is refused**, "Too far", by the path's length; a move never pends (*decided*). The separate budget in meters, its clamps and the dash go.
- **Leaving melee costs nothing beyond its time** (*decided*); kiting is for play tests.

### Holds

- **A hold uses up its victim's own time** (*decided*): the effect's duration as the engine holds it when it lands, after the game's shortening for rank, difficulty and a recent stun.
- **It takes that share of the victim's turn by the turn's fixed ratio** (*proposed*): a 2 s stun is half of a 4 s clock, so half the bar, 3.5 s of a 7 s one. A hold of 4 s or more is a lost turn; what is over 4 s takes the next turn's share by the same rule. A knockdown's time is its fall and getting up (rule 5).
- **A hold landing in the victim's own turn** interrupts what it was doing (the sequence above), and takes its time from that point.
- **Cured or dispelled** before it is spent, it gives back what it had not yet taken, as built.

### Areas on the ground

- **An area keeps its own script and its own clock** (*proposed*): 4 s a round, spread through the round's turns, independent of its caster. Its intervals (2.05 s for storms, 4.05 s for Inferno and Earthquake, as read), its first pulse and its later-pulse effects (Blizzard's freeze on its fourth) are the game's. Whoever is inside when a pulse falls is struck, acting or not. A creature walking through in one short turn may meet no pulse.
- **Traps and glyphs** keep the game's own trigger on entering.

### Initiative

- **Rolled at the fight's start, as built** (*decided*): 1 to 20, plus 0.25 a point of Dexterity above 10, **plus 2 a rank of Survival** (proposed in the talk and not objected to).

### What the player sees

- **The bar of time, the mod's own under the turn order, made of the experience bar's art**, a tick a second, the rollover past the 4 s mark (*decided*, 2026-10-02: it was built on the game's experience bar first, and the user, having seen it, wanted it separate: "the action bar seems to replace the experience bar? that's confusing").
- **The hovered action's cost shaded on it** (built: pale for what it would spend, red if it spills), with its numbers ("move 1.2 s + attack 1.9 s, 0.9 s left"; "lands next turn, 2.4 s left"), and where a cooldown will lift (*proposed*).
- **A pending action on hovering over its creature or its card**: "Inferno lands 2.4 s into its next turn", and the talent's or spell's own icon on the card (*decided*; built).
- **The quickbar's sweep** moving as its user spends (*decided*).
- **Waiting a set time**, as a way to spend the bar on nothing until a cooldown lifts (*proposed*).

### Open

- **Doors, chests and pickups**: free as built, or a cost in time (decision 8).
- **Dain's Fixes**: whether the mod assumes it (decision 10).
- **An elite boss's time**: 1 s or 1.5 s.
- **A stealthed opening attack**: whether the mod's clearing of commands at the fight's start cancels it, to see in play ("After the decisions"); the fix if so is written there.
- **The AI on the bar**: its own issue ("After the decisions").
- **Dragons and creatures that fight unlike people**: grabs, flight, attacks timed by their animations.
- **Delay**, the player's only: **built 2026-10-02 as Deadfire's, to judge in play** (`[turns] delay`, on; the bar's Delay beside End). A party member puts its turn off to the round's end, once a round and only before it has moved or acted; the turn it then takes is whole, the round after is in initiative's order again, and nothing counts the turn twice (the scripts' turn start, its cooldowns, its own clock). The AI does not use it. *Decided*: the user, 2026-10-02, having asked what it does (it does not skip the turn; the turn is taken whole at the round's end): "okay, we can keep delay".

### What it rests on that is not yet had

From DAOLib ("Asked of DAOLib"): animations' lengths and conjuring times (item 0); script timers taken and fired on the right clock, a creature's for damage over time and an area's for its pulses (item 1); a creature's effects listed (item 2) and aged on its own clock apart from the rest, which `dao::clock` does not do; the damage a creature takes, for interruption (item 3); the object under the cursor, for the pending action on hover. To see in play: that a command can be cleared before it lands; which clock `DelayEvent` counts on; the lengths read from files against the game's own (the list in "How it would be built").

## The short of it

**Since decided** (2026-10-02, "Decided: a turn is four seconds of time" below): a turn is a 4 s bar of time, not 2 s or points, and the items below that recommend otherwise (3, 5, 6, 11) are kept for their reasons.

1. **Almost everything the game times is in seconds, and one number converts it: how many seconds a turn is worth.** Cooldowns, effect durations, damage over time, area pulses, regeneration and sustained drains all take it. So the conversion can be rules over what every ability already carries (its row in the ability table, the durations of the effects it applies, its timers), not a list of abilities. A talent a mod adds is converted with the rest.
2. **The mod uses three numbers for it today**, and they disagree: 4 s a turn for cooldowns and casting, 6 s a round for effects and regeneration, and a flat one turn for a stun whatever its length. With one action a turn, which is about 2 s of fighting, the clocks run two to three times faster than the fighting does. This is the first thing to fix, and needs no action points.
3. **One attack or talent a turn is the right shape, and such a turn is worth about 2 s, not 4.** Both were the user's ideas, floated for an opinion; the opinion here is yes to both, for a reason beyond feel: the less a turn holds, the more the turns interleave, and the nearer the fight stays to real time's everyone-at-once. A swing takes 1.5 to 2.6 s by the game's own formula and 1.7 s as measured; an ability about 2.6 s. 2 s is also the game's own beat: regeneration ticks every 2 s in combat, storms pulse every 2 s and fires every 4. The number of rounds in a fight does not depend on the choice: with one action a turn a fight is as many rounds as it has actions.
4. **Damage over time and area pulses are outside the clock the mod holds.** They are script timers (`DelayEvent`), 1.5 s for every damage over time in the game and 1.5 to 5 s for areas, while the effect's lifetime is on the engine's effect clock. As built, a burn ticks whenever the world runs (through everyone's actions, and while the player thinks with the world idling) and ends by rounds. Read, not yet seen in play. It is the largest gap in "everything in turns".
5. **Action points fit as the user sketched them once the main action is capped at one a turn**: points are for what goes beside the attack or talent (a potion, a poison, a mode turned on, the off hand's strike). Costs come from the weapon's timing and the length of the animation the ability names, both the game's own data, so nothing is listed by hand but the few things the data does not tell apart.
6. **Speed is what one action a turn costs.** Pillars of Eternity II made every character one action a round and its speed stats lost their meaning, by its players' and its director's account. Here the same loss is weapon speed (a dagger's swing is 1.5 s, a greatsword's 2.5) and Haste, Momentum and Swift Salve. Three things give it back without two attacks a turn as the norm: a faster weapon's attack costs fewer points, a dual wielder's off hand strikes as a lesser action, and a speed effect earns a second attack every few turns at exactly its real-time worth (Swift Salve one in four, Haste one in three).
7. **A stun measured in turns should come from its own duration**, which the engine now gives (`CGameEffect::Duration`), rounded to the nearest turn. The game already shortens holds on bosses (to a quarter on an elite boss, on normal) and on a creature stunned in the last 15 s (to a quarter), and both come through the duration for free; a flat turn per stun throws both away.
8. **The order of work is not the order of interest.** One rate, script timers on the clock and holds from their durations (2, 4 and 7) correct what is built and need no decision about points. Points come after. And the thing most likely to need tuning in play is the length of fights: those fought out in the play tests ran 5 to 17 rounds at level 1 to 3.
9. **Interruption is a rule the game already has**: physical damage over 10 a rank of Combat Training plus a level term stops a spell being conjured. Applied to a cast that is charging across turns, it gives the issue's "long actions span turns and can be interrupted" and gives the mage's Combat Training its meaning back.
10. **The conjuring table counts loops, not seconds, and every animation's length is in the game's files.** A "fast" spell conjures for 3.5 to 3.9 s before its cast (Inferno, Blizzard, Tempest, Death Cloud, Chain Lightning, Mass Paralysis), not the 1 s the built charge rule takes it for; every other spell does not conjure at all. At 2 s a turn those six charge two turns by the rule as it stands. The same files give each talent's length (1.3 to 3.5 s), a potion's (1.2 s) and a knockdown's (3.5 to 4.5 s), so what looked like the part no rule could reach is data too.
11. **The user's second sketch, points as time, is the better shape for action points** and would replace items 5 and 6's main and lesser actions: half a second a point, four a turn, everything at its own time. One attack a turn and a speed effect's occasional second attack both come out of the arithmetic. It needs an overrun owed from the next turn, unspent points carried, and a speed effect's fraction kept rather than rounded; it should not gain points by level ("Points as time").

## What the user has said

The issue: a turn is `N` seconds, preliminarily 4; action points, converted from real time automatically so custom talents are supported; more points from the Combat Tactics skill, whose tactics do nothing in turn-based play; long actions such as spellcasting span turns and can be interrupted.

During this investigation, 2026-10-01, ideas of the user's, not decisions ("just shooting off some ideas. i want to know what you think, given all the context you've gathered"); "Thoughts on the ideas floated" below answers them one by one:

- "ideally we'd be able to come up w/ a set of rules to automatically convert rtwp mechanics to turn-based. this means we could support new talents etc. on the fly instead having to override a closed set."
- "maybe each turn should represent 2s instead of 4s? 4s was just a preliminary value we picked."
- "i don't want a turn to be too many actions. letting a char attack 2x in a turn might be too imbalanced. normally we'd want 1 attack, talent, etc. additional action points might be spent on e.g. applying poisons, drinking potions, etc.; or maybe toggle a sustained ability on (off should be free, probably). only in rare cases should attacking 2x be supported, e.g. after taking haste potion." The potion meant is Swift Salve; the Haste spell "does similar".

And from `dev/DESIGN.md`, standing: everything in turns, nothing on the game's clock; 4 points a turn, a point a second of casting time; one talent or spell a turn; items cost points, more for the greater; movement its own budget.

## Decided, 2026-10-02: a turn is four seconds of time

The user's decisions of 2026-10-02, talked through with a session that had this page. They replace the points of "Points as time" and the main and lesser actions of the section after it, and answer decisions 0 to 6 and 12 below. **Nothing here is built.**

The user's turn: "instead of converting 0.5s = 1 AP, maybe we just have a bar 4s per turn. so things that take time can take precise time away from bar, instead of being rounded to nearest 0.5s for discrete point." And on the length, against this page's 2 s: "2s seems like a lot of things would overflow. maybe 4s is better. 2x attack w/ some weapons, perhaps, but not w/ others."

**The bar.**

- **A turn is a bar of 4 s**, kept in milliseconds as every other time in the core, and shown on the game's experience bar, a segment a second (the user's idea; whether that bar is drawn in segments and can be driven by the mod is not established).
- **Everything costs its own time**, unrounded: an attack its swing (a longsword 1.9 s, a dagger 1.5, a greatsword 2.5), a talent its animation, a spell its conjuring and cast, a potion its drink. Until the engine gives animations' lengths ("Asked of DAOLib", item 0), the defaults by kind of "What no rule reaches" stand in, so the precision is on paper until then.
- **An action that does not fit is begun now and lands partway into the next turn**, which starts that much short: "if a spell costs 6s, you cast it this turn, wait, possibly interrupted, then next turn cast it, and still have 2s left." Time is never borrowed, only taken from the next turn by an action already begun. One that needs several whole turns (a mod's ritual) takes each of them whole. It is the charge rule of `dev/DESIGN.md` made general and exact: the command is kept, not queued, and given to its creature when its time is paid; it lands then, its cooldown starts then, and one whose target is gone is given back.
- **While an action is pending its creature cannot move**, and a hold, a fall or forced movement interrupts it; physical damage over the game's threshold interrupts a spell, as rule 7. A turn that begins with an action pending finishes it first, then has the rest of its 4 s and its movement.
- **Weapon speed comes out of the arithmetic.** A dagger strikes about 2.7 times a turn, a longsword 2.1, a mace 2, a greatsword 1.6, each its real-time rate, and a late start lands next turn rather than adding a blow to this one. Haste and the other speed effects multiply the cost.

**Leftover time.**

- **What is left at a turn's end rolls over, up to 1 s**: the smaller of the leftover and the cap, so there is no edge at which wasting time pays. A turn can begin with at most 5 s, which adds a third blow only to a dagger.
- **Combat Tactics raises it, and adds time**, alternating by rank (the user's): rank 1 half a second more rollover, rank 2 half a second more a turn, rank 3 half a second more rollover, rank 4 half a second more a turn. At rank 4 a turn is 5 s and up to 2 s roll over. Rollover adds nothing over a fight; the added time is a quarter faster, about Haste.
- **Enemies get Combat Tactics by their rank**, so the added time makes bosses more dangerous rather than the party stronger ("i think 2's more interesting b/c it makes bosses more dangerous"): a normal, weak or critter creature none, a lieutenant rank 2 (0.5 s), a boss rank 4 (1 s), an elite boss rank 4 or beyond the player's cap (1 s or 1.5 s; the game goes past the player's ranks for elite bosses in its interruption rule). A setting, by rank, to tune difficulty by.
- **A lost turn drops what was rolled over.**

**Movement costs time** at the creature's own pace (its appearance's combat speed; a person's 4.66 m/s, so 9 m is 1.9 s), out of the same bar, **with about a metre a turn free** for shuffling in a melee ring. A move must fit in the time left or is refused, "Too far", as now; a move never pends. The separate budget in meters, its clamps and the dash proposed in `dev/DESIGN.md` go; so does ending a party member's turn when its action is spent: a turn ends when its time is gone or its owner ends it. Opening turns spent closing in are the thing to watch; the free step, or a multiplier on movement's cost, is the knob.

**Clocks.**

- **Cooldowns run on their user's own time**, the seconds its bar spends, and lift at the moment they run out, mid-turn included: "it'd have to be when the game's numbers actually apply, including in middle of turn. so char may need to do something else, then talent comes out of cooldown." A creature's own time counts what it spends, what it gives up by ending a turn early (or passing would hold its cooldowns), a lost turn's 4 s, and rolled-over time when it is spent. (Replaced, if taken, by "Answers to the review concerns": a creature's clock moves exactly 4 s a turn, whatever its bar holds.) The quickbar's sweep then moves as the character spends.
- **Effects run on their target's own time**, the same clock, and end mid-turn as it passes: a 20 s buff on a creature lasts five of its turns, a burn ticks every 1.5 s of its victim's time, and a hold uses up its victim's time, which makes holds exact with no rule of their own. Effects on the ground (Inferno) need an owner, presumably their caster. It needs DAOLib to age one creature's effects apart from the rest, which `dao::clock` does not do (to add to "Asked of DAOLib").
- **No limit on talents and spells a turn**: "can't spam same talent, but can use different ones. seems fair to me." Cooldowns stop the same one twice; at 4 s two short ones fit (Shield Bash and Dirty Fighting, 3.4 s). The game's own guard against stun-locks (a stun within 15 s of another lasts a quarter) still applies through the durations. Combinations landing in one turn (Cone of Cold, then Stonefist) are the thing to watch; a setting for abilities a turn can bring a limit back.

**Initiative** is rolled at the fight's start as built, **plus 2 a rank of Survival** (proposed in the talk, against the user's "+1 per rank enough if we're rolling d20? maybe +2", and not objected to) (+8 at rank 4, about what 40 Dexterity gives): against an otherwise equal enemy, acting first goes from about half to about four in five. The order is fixed for the fight, so it decides first strikes and who acts between whom, not how much anyone does. It is also the answer to a 4 s turn's cost: a whole side acting before the other answers grows with how much a turn holds.

**Not yet looked at**: dragons and the creatures that fight unlike people (their attacks' length is their animation's, grabs hold two creatures, flight is classed an action). "we'll have to cross that bridge when we get there."

### After the decisions, the same day

Ideas raised once the above was decided, and what the user made of them.

- **Poultices: no shared limit** (decision 7). Each tier has its own 5 s cooldown, so three can be drunk in a turn, 3.7 s of its 4, chosen after seeing the blows; real time allows the same, and choosing after seeing is true of every action in turns. "doesn't the time / cooldown already balance this? poultices cost money; if char wants to blow through them, that's on them." Each costs its drink, the same for every tier.
- **Script timers onto the target's clock: agreed.** Damage over time and area pulses (`DelayEvent` chains, "What runs on a script's own timer") must move to the clock of the creature they tick on, or burns stay wrong. Needs DAOLib ("Asked of DAOLib", item 1).
- **Leaving melee costs nothing more than its time.** Opportunity attacks stand in, in other games, for an enemy swinging while one walks away in real time; but with movement out of the bar, kiting at equal speed is a wash (the archer pays 1.9 s to step 9 m back, the swordsman 1.9 s to close), and pays only with a speed advantage, as in real time. Nothing added; watched in play.
- **The AI on the bar is its own piece of work**, worth its own issue ("changing enemy AI is a big project. might deserve its own issue"). Today an AI's turn is one action, a move, at most `max_commands_per_turn` (6) commands and a short think. On the bar it is told to choose again after each command until its time is gone; it should not begin an action it cannot sensibly pay for (a 4.4 s spell with 0.1 s left), and the turn's end is its time running out, not its action spent. How fair enemies feel will come from this more than from any number.
- **A pending action is shown on hovering over its creature or its card**: "Inferno, lands in 2.4 s", so that interrupting it is a choice the player can see to make. The card is the turn bar's own movie; the creature in the world needs the object under the cursor (`dev/DAOLIB_NEEDS.md`, "The world under the cursor", item 1).
- **Surprise: the game's own, if the mod lets it through.** Combat does not begin until a stealthed character attacks, so its attack is a free one (the user). But as combat mode begins the mod pauses and clears every combatant's command, the attacker's included, so the first turn starts from a still world (`TurnMachine::StartFight`), and then rolls everyone's initiative, the attacker's with the rest. Whether the blow has landed by then depends on when the game enters combat mode: at the hit, it has; at the swing's start or when the enemy notices, the mod cancels it. Not seen in play: a stealthed attack, `combat began`, and whether its damage came before the commands were cleared. If it is cancelled, the fix is to leave the command of the one whose attack began the fight and clear the rest. An ambush by enemies is the same question.
- **Delay, the player's only**: a party member moving later in the order (a healer after the tank), its time kept. The AI need not use it. Low in the order of work.

## What the game's rules are made of

### The ability table

`abi_base.gda`, 551 rows: 281 talents, 105 spells, 70 skills, 95 item abilities (`AbilityType` 1 to 4). By `UseType`: 315 activated, 60 sustained, 172 passive. Awakening's 162 rows are in a second archive (`packages\core_ep1\data\2da.erf`, `abi_gxa`), which `gda.py` does not open; in the game the two are one table, so a rule over columns covers them and a mod's rows alike.

| Column | What it gives a rule |
|---|---|
| `AbilityType`, `UseType` | talent, spell, skill or item; activated, sustained or passive |
| `Cooldown` | seconds; set at impact, or at a sustained ability's turning off (`ability_core.nss`, `effect_upkeep_h.nss`) |
| `Cost`, `CostUpkeep`, `Fatigue` | mana or stamina to use; reserved while sustained; percent added to every cost while sustained |
| `Speed` | a row of `spellconjuringspeeds.gda`, whose own `Speed` is **a count of loops of the conjure animation, not seconds**: 1 instant (0), 2 fast (1), 3 normal (2), 4 slow (4), 5 ritual (6). See "Conjuring time" below |
| `ConjureAnim`, `CastAnim` | rows of the animation tables (`anim_base`, `anim_combat`), whose files have lengths: how long the ability takes |
| `Flags` | bit 1: an archery talent, given a 1.5 s aim (`ability_core.nss`); 0x40: a mode that turns off at no mana or stamina |
| `TargetType`, `Projectile`, `AOE_Type` | self, friend, foe, ground; thrown; an area |

Cooldowns of the 315 activated abilities fall on a few values. For the 102 talents and spells with a place in the talent screen:

| Cooldown | How many | Turns at 2 s | at 2.5 s | at 4 s |
|---|---|---|---|---|
| 5 to 6 s | 5 | 3 | 2 to 3 | 2 |
| 10 s | 14 | 5 | 4 | 3 |
| 15 s | 9 | 8 | 6 | 4 |
| 20 s | 21 | 10 | 8 | 5 |
| 30 s | 17 | 15 | 12 | 8 |
| 40 s | 14 | 20 | 16 | 10 |
| 60 s | 13 | 30 | 24 | 15 |

About thirty abilities, nearly all monsters' attacks, sit at 1 to 2.5 s, which is once a turn at any rate. A player's talents cost 20 to 60 for the most part, and spells 20 to 70.

**Conjuring time** (read from the executable, 0x00744542 on, and the game's animation files, 2026-10-01, at the user's prompting: "you have access to the gdas ... you can check how long the conjuring speeds are"). The engine reads the ability's `ConjureAnim`, `Speed` and `ConjureVFX`; with no conjure animation there is no conjuring at all, whatever `Speed` says. With one, the time is

> the start animation's length + the loop animation's length × the conjuring table's `Speed` for the ability's `Speed`

the loop being the start's `TransitionTo` in the animation table, and the whole thing nothing when the count is 0. A script's own conjure time (`CommandUseAbility`'s fourth argument, positive) replaces the loops: the start's length plus that many seconds. The animations' lengths are in the game's `anims.erf`: each animation file has a `.gad` beside it with its length in seconds, and a blend tree (`.anb`) says which file plays for an animation's name in a stance (`staff`, `combat_humanoid`).

| | Start | Loop | Conjuring at "fast" (1 loop) | "slow" (4) | "ritual" (6) | Then the cast |
|---|---|---|---|---|---|---|
| The spells that build up (`ConjureAnim` 245), with a staff | 0.77 s | 2.70 s | **3.47 s** | 11.6 s | 17.0 s | 1.03 s |
| the same, without a staff | 0.67 | 2.27 | 2.93 | 9.7 | 14.3 | 1.27 |
| The spells that draw in (244), staff or not | 0.47 | 3.40 | **3.87 s** | 14.1 | 20.9 | 1.0 to 1.33 |

So the wiki's "Ability mechanics" page is right (a speed of 2 is about 3.5 s of channelling, the longest 21 s), and `dev/DESIGN.md`'s reading of the table as seconds (fast 1 s, normal 2 s) is not: **the built charge rule works from numbers a third of the real ones.**

- **Fast, about three and a half seconds of conjuring and a second of casting**: Inferno, Blizzard, Tempest and Death Cloud (3.47 s), Chain Lightning and Mass Paralysis (3.87 s); among the sustained, the three shapeshifts and the ranger's three summons.
- **Instant, no conjuring**: every other spell, 71 of the player's 80. The cast animation alone, 1.0 to 1.3 s, and a second or less of recovery.
- **Talents do not conjure**, six aside: Holy Smite, Cleanse Area and Mark of Death have a conjure animation at instant, and the ranger's summons at fast. The other seventeen talents with a `Speed` over instant (Shield Bash and Arrow of Slaying are "normal") have no conjure animation, so it does nothing in the engine.
- Nothing in the game as shipped is slow or ritual.

### How long an action takes

**A melee swing is a formula in script** (`core_h.nss`, `CalculateAttackTiming`, set through `SetAttackDuration` in `combat_h.nss`): a base by style plus the weapon's `DSpeed` (`itemstats.gda`), times the creature's attack speed modifier.

| | Seconds a swing | Swings in 2 s |
|---|---|---|
| Dual wield, a dagger's hand | 1.0 | 2.0 |
| Dual wield, a longsword's hand | 1.4 | 1.43 |
| A dagger alone | 1.5 | 1.33 |
| Longsword or waraxe, with a shield or without | 1.9 | 1.05 |
| Mace | 2.0 | 1.0 |
| Greatsword | 2.5 | 0.8 |
| Maul, battleaxe | 2.55, 2.6 | 0.78, 0.77 |

The bases are 1.5 s dual, 2.0 s single, 2.5 s two-handed; `DSpeed` is -0.5 for a dagger, -0.1 for a longsword or waraxe, +0.05 for a maul, +0.1 for a battleaxe. A dual wielder's hands take turns, one to an attack command. A creature that is not a humanoid, or is shapeshifted, has no figure: its animation decides.

**The attack speed modifier** (`PROPERTY_ATTRIBUTE_ATTACK_SPEED_MODIFIER`, 31) rests at 1.0, and effects add to it: Haste -0.25, Momentum -0.3, Swift Salve -0.2, Precise Striking +0.1. It is clamped to 0.5 to 1.5, and a sum under 0.5 is thrown away: Haste with Momentum (0.45) gives no speed at all, which the wiki lists as a bug that Dain's Fixes mends. It touches plain attacks only, not talents.

**A shot** is a draw (0.8 s for a party member, 2.0 s in heavy armour; 1.5 and 2.5 s for others), an aim (the bow's `BaseAimDelay`: shortbow 0.2 s, longbow 0.3, crossbow 0.8, staff 0.1; plus Aim's 1.5 s or Defensive Fire's 1.2, less Rapid Shot's) and the release. The wiki's archery page puts the best rate at 1.6 s a shot. Haste and Swift Salve add 0.8 and 0.9 s to the aim: they slow archers, also listed as a bug.

**A talent's time is its animation's, and that is in the game's files too.** A talent is a command: the engine plays the animation the table names (`CastAnim`) and sends the script an impact event for each blow. The lengths, by the same reading as the conjure animations':

| Seconds | Talents |
|---|---|
| 1.3 to 1.8 | Below the Belt 1.30; Riposte 1.50; Shield Bash 1.57; Threaten 1.63; Dirty Fighting, Upset Balance, Cripple, Dual-Weapon Sweep 1.80 |
| 2.1 to 2.7 | Pommel Strike 2.13; Flurry 2.27; Whirlwind 2.30; War Cry, Taunt, Distraction, Disengage and the songs and auras turned on 2.47; Deadly Strike, Final Blow, Berserk 2.50; Two-Handed Sweep 2.60; Mighty Blow 2.63; Critical Strike 2.70 |
| 3.0 to 3.5 | Punisher 2.97; Overpower 3.17; Sunder Arms, Sunder Armor 3.30; Assault 3.43; Shield Pummel 3.47 |

The long ones are the ones of several blows: Shield Pummel and Overpower three, Punisher four, the Sunders two, Assault four at 60% damage (the wiki). Plain attacks' animations are as long as the script's timing says: 1.47 s dual, 1.97 to 2.0 s with one weapon, 2.3 to 2.5 s two-handed. A potion's drink is 1.23 s, a bomb's throw 1.0 s, and setting a trap is a crouch of 3.5 s in a fight. A knockdown is 1.37 s falling and 2.13 s getting up around the effect's own 0 to 1 s, 3.5 to 4.5 s in all for a person, and a slip 3.77 s; creatures' getting up runs 1.5 to 3.8 s.

A length is the whole clip; whether a command ends with it or a little before was not established. The archery talents have no animation of their own in the table: they are a shot with a 1.5 s aim.

**Measured** (41 play test logs, the time from an order passing the gate to its creature's AI choosing again, at the game's own speed): an attack already in reach, 1.74 s at the median over 442; an ability, 2.6 s over 44, from 1.5 to 4.0. The attacks are mostly low-level longswords, maces and creatures' weapons, so the formula, the animations and the game agree.

### Regeneration, fatigue and upkeep

**Regeneration ticks every 2 s in combat and every 3 s out of it** (the engine's `UpdateRegeneration`, read by DAOLib and again here), adding what a property holds: in combat, health's is 0 and mana and stamina's is 1.0 by default (`core_h.nss`).

| | A tick (2 s) | A second |
|---|---|---|
| A party member above 50 mana or stamina | 1.5 | 0.75 |
| at 26 to 50 | 2.0 | 1.0 |
| at 25 or under | 4.5 | 2.25 |
| Anyone else | 1.0 | 0.5 |
| Health, in combat | 0 | 0 |

The party's bands are set by a heartbeat a second (`player_core.nss`, `EVENT_TYPE_HEARTBEAT2`). Nothing scales combat regeneration by an attribute. Out of combat it is 17.5 plus twice the level, each 3 s: a pool refills in seconds, so the economy of a fight is the pool it began with.

What adds to it, a tick: Rejuvenate +8 for 10 s, Mass Rejuvenation +5 for 15 s, the Regeneration spell (100 + spellpower) / 5 health for 10 s, Spellbloom +3 inside it, Song of Valor +0.5 and a little for Cunning, Improved Combat Training +0.5, an item's 0.25 a point.

**A sustained ability is paid by a reserve, not a drain** (`effect_upkeep_h.nss`): its `CostUpkeep` comes off the top of the pool while it is on, and its `Fatigue` (5% for most, Haste 10%, Combat Magic 50%) is added to the cost of everything else. A few also take from regeneration, a tick: Berserk, Rally and Momentum -2, Haste -3 on its caster, Spell Might -4, Shimmering Shield and Cleansing Aura -10. Against a party member's 1.5 to 4.5 the -2 modes stall the pool rather than empty it. At nothing, the modes flagged 0x40 turn themselves off (`Ability_HandleEventOutOfManaStamina`).

So a sustained ability has no time in it but its turning on, its cooldown after turning off, and a regeneration penalty. All three convert with the rest.

### Durations

About 150 player abilities and the monsters' holds were read (`talent_constants_h.nss`, `spell_constants_h.nss`, `monster_constants_h.nss` and the scripts that use them). They cluster:

| | Seconds | What |
|---|---|---|
| Knockdowns and slips | 3.5 to 4.5 | Shield Bash, Pommel Strike, Overpower, Fireball, Stonefist, Grease, about 16 in all. The effect is given 0 to 1 s; the rest is the fall and the getting up, animations (above) |
| Short stuns | 2 to 5 | Scattershot 2, Mind Blast 3, Dirty Fighting, Shield Pummel and Riposte 4, the mabari's howl 5 |
| A mage's holds | 8 to 20 | Cone of Cold 8, Pinning Shot 8.5, Crushing Prison 9, Holy Smite and Mass Paralysis 10, Sleep 12, Paralyze 15, Force Field 18, Petrify 20 |
| Debuffs | 10 or 20 | nearly all of them |
| Timed buffs | 10 to 20 | the Heroic spells 20, Perfect Striking 15; Lifeward 60 |
| Damage over time | 4 to 20 | Fireball's burn 5, Stinging Swarm 10, Walking Bomb 20 |
| Areas on the ground | 20 or 30 | Grease, Earthquake and the glyphs 20; Inferno, Blizzard, Tempest, Death Cloud 30 |
| Items | 60 to 180 | a poison or a coating 60, Swift Salve 60, Rock Salve 120 |

Three rules of the game change a duration before it is applied, and all three arrive in the effect's own duration, so a rule over durations inherits them:

- **By the target's rank and the difficulty** (`sys_resistances_h.nss`, `GetRankAdjustedEffectDuration`; the table is `autoscale.gda`'s `fEffectDur` columns). On normal: a lieutenant 0.75, a boss 0.6, an elite boss 0.25, a weak creature 1.2, a critter 1.5, the party 0.8. On nightmare a boss is 0.35 and an elite boss 0.2. About 60 scripts call it: the holds, mostly.
- **A second at a time for a short party**: a hostile effect lasts a second longer for each empty place in a party of four, before the rank's multiplier.
- **A stun soon after a stun is quartered**: the first leaves a hidden 15 s mark (`EFFECT_TYPE_RECENTLY_STUNNED`, in `core_h.nss`'s `ApplyEffectOnObject`), and a stun landing on it lasts a quarter. A second stun on a creature still stunned is refused.

Sleep and a root end at any damage (`Ability_HandleOnDamageAbilities`). Spellpower changes how much, not how long.

### What runs on a script's own timer

The engine's effect clock, which the mod holds through a fight and spends a round's worth at a time, decides only when an effect ends. What an effect does meanwhile is often a script's `DelayEvent` chain, which that clock does not reach:

| Timer | Seconds | Where |
|---|---|---|
| **Every damage over time in the game** | 1.5 | `effect_dot2_h.nss`: one chain a creature (`EVENT_TYPE_DOT_TICK`), every such effect on it ticking together; the total is split into duration / 1.5 + 1 ticks |
| Storms: Tempest, Blizzard, Death Cloud, Storm of the Century | 2.05 | `spell_aoe_duration.nss`, `spell_blizzard.nss`: each heartbeat arms the next |
| Inferno, Earthquake; Captivating Song, Aura of Pain | 4.05 | the same; `talent_aoe_duration.nss` |
| Cleansing Aura | 5.05 | |
| Grease, Flying Swarm | 1.5 | |
| Death Syphon, Death Magic; caltrops | 2.0, 2.05 | a corpse a pulse |
| Glyph of Paralysis, its 60 s life; traps arming and resetting | 60; various | `sys_traps_h.nss` |
| A fallen party member's revival; the end of combat | 6; 3 | `player_core.nss`, `module_core.nss` |

Blizzard freezes on its fourth heartbeat, so its freeze too is this timer's. Which clock `DelayEvent` counts on is not established; the reading here is the world's, which stops with the pause.

Also on the world's time and not the effect clock's: cooldowns (held by the mod already), the AI's own timers (a creature's minimum time before it changes target grows from 5 to 25 s; `ai_threat_h.nss`), and threat falling by half a point a second.

### Interruption

`rules_core.nss`, at `EVENT_TYPE_DAMAGED`, for physical damage in combat:

- **A spell being conjured is interrupted** when the damage is over a threshold: for a party member 10 for each rank of Combat Training, plus 5, plus half the level; for anyone else 10 for each of the ranks its kind is given (`autoscale.gda`: a normal creature 2, a lieutenant 3, a boss 4, an elite boss 6), plus its level. "Interrupted" floats and its commands are cleared; the mana is already spent. Master Combat Training never counts: the script's constant for it (100104) is not the ability's id (100103).
- **An archer struck in melee loses the shot** if the damage is over a third of its Dexterity above 10, unless it has Melee Archer.
- The Mage Slayer property on a weapon clears a conjuring target's commands, elite bosses aside.

It can only happen while the caster is conjuring, so in the game as shipped only to the spells with a conjuring time.

### Items

Each item's use is an ability of its own (`AbilityType` 4), with its cooldown in the table and no cost. No script or table shares a cooldown between items: each tier of poultice has its own 5 s, so four can be drunk one after another.

| | Cooldown | Targets | Animation |
|---|---|---|---|
| Health poultices, lyrium potions (four tiers each) | 5 s | the user | a drink, about a second (the wiki) |
| Salves, balms, incense, injury kits, lyrium dust | 5 s | the user | none |
| Poisons and the elemental coatings | 60 s (a few 5 s) | the user: its weapons, for 60 s | none |
| Traps and lures | 5 s | the ground | placing it |
| Bombs | 20 s | the ground, thrown, an area of 2.5 m | a throw |

A poultice heals (50 + spellpower) times 1, 2, 3 or 4 by tier at once (`item_singletarget.nss`). The data tells the tiers apart by nothing else: same cooldown, same animation.

### Skills

| Skill | What a rank gives | In a turn-based fight |
|---|---|---|
| **Combat Training** | the weapon talents' tiers; +10 to the damage a mage takes without losing a spell; rank 2: +0.5 mana or stamina a tick in combat; rank 3: +2 attack; rank 4: +1 armour, +4 attack | all of it carries over; the mage's part needs casts that can be interrupted |
| **Combat Tactics** | tactic slots: +1, +2, +4, +6 by rank, on 2 to 7 by level (`Chargen_SetNumTactics`). Needs Cunning 10, 12, 14, 16. Nothing else | nothing, when every party member is the player's; its slots, under `party = tactics` |
| **Poison-Making** | rank 1 is needed to use any poison or throw any bomb; ranks make better ones | the items are actions (below) |
| **Trap-Making** | sets traps; ranks make better ones | the same |
| **Herbalism** | makes poultices, potions and salves | the same |
| **Survival** | sees creatures on the map by level; +5, +5, +5, +10 nature resistance | carries over |
| **Stealing** | rank 4 steals in a fight; a 10 s cooldown | an action |
| **Coercion** | conversation | none |

### Spells that work together

The wiki lists ten combinations, and most need two effects alive at once: a target still frozen or petrified when Stonefist, Crushing Prison or a critical hit lands (Shattering); Blizzard and Tempest overlapping with Spell Might on (Storm of the Century); a sleeper still asleep for Horror (Nightmare, whose 10 s ignores rank); Death Hex's 10 s inside a Death Cloud (Entropic Death); a hex's 20 s for Drain Life (Improved Drain); Force Field and Crushing Prison on one target (Shockwave); two glyphs in one place (Paralysis Explosion); fire in Grease's 20 s. Each is one script checking for the other's effect at impact. In turns they need no timing skill, only the order of play, so every one of them is easier than in real time.

## What turns do to them

**One at a time.** In real time everyone acts at once. In turns the party's blows all land before the enemy answers, so focus fire, opening bursts and initiative are worth more than any real-time rule gives them, and a killed enemy loses actions it would have had. Holds gain the most: everyone who acts between a stun and its victim's turn has the whole of their own turn against a helpless target. Combinations (above) and area spells gain too: nobody walks out of a Fireball or an Inferno being cast. This is no conversion's fault, and it is why a rule that damps bursts (one talent a turn, a big spell that charges) is worth having even where real time has none.

**One action a turn is about two seconds.** A swing is 1.5 to 2.6 s and an ability about 2.6. If the clocks run at any other rate, everything on a clock changes its worth against plain attacks. Today cooldowns count 4 s a turn and effects and regeneration 6 s a round:

| Against real time, for each action taken | Today |
|---|---|
| A cooldown comes back | twice as fast |
| Mana and stamina come back | three times as fast |
| A buff or a poison lasts | a third as many of its owner's attacks |
| A stun costs | one turn, whether it was 2 s or 20 s, on a critter or on an elite boss |

The first three favour abilities over plain attacks, and so mages over warriors and rogues, in a game where mages are already the strongest class (the wiki's notes on Mana Clash, Cone of Cold, Force Field, Crushing Prison; patch 1.02 shortened several of them because "combatants can no longer stun-lock each other").

**Rounding.** A duration becomes a whole number of turns. The longer the turn, the more is lost: at 4 s a turn, Cone of Cold on a boss on nightmare (2.8 s) and Paralyze on an elite boss (3 s) are both less than a turn, and Scattershot's 2 s stun and Dirty Fighting's 4 s are both exactly one.

**Speed.** One attack a turn for everyone makes a greatsword strike as often as a dagger. Against real time at 2 s a turn: a greatsword gains a quarter, a mace or a longsword is even, a single dagger loses a quarter, a bow a fifth, and a dual wielder striking with one hand a turn loses 30% (longswords) to 50% (daggers). Haste, Momentum and Swift Salve do nothing at all.

**Script timers.** As built, damage over time and area pulses tick while the world runs and their effects end by rounds (above). A burn meant for four ticks gets as many as the rest of the round's actions give it, in a large fight many more, and with the world idling on the player's turn it goes on ticking while the player thinks. Read, not seen: the first check in play.

**Movement.** Nine metres is about 2 s of running at a person's 4.66 m/s. A turn's movement is its own budget, as in most turn-based games, so a creature that walks and strikes does 4 s of real time's work in a turn and one that stands does 2. It favours closing and kiting over real time, where every step is time not spent attacking. Opportunity attacks (`dev/DESIGN.md`, "Also needed") are the usual counterweight.

**What is lost outright.** An archer's shot interrupted by a blow, since a shot is over within its own turn. Walking out of an area while it is being cast. Seeing a spell begin and stunning its caster, unless a cast spans turns.

## How long a turn is

The number has to be the real time a turn's worth of fighting takes, or the clocks drift against the actions. With one attack or talent a turn that is about 2 s. It would be 4 s only if a turn held two attacks, which the user would rather it did not ("Thoughts on the ideas floated" agrees, and says why).

| | 2 s | 2.5 s | 4 s (today's cooldowns) |
|---|---|---|---|
| Against a turn's real content (2 to 2.6 s) | even, a little fast for two-handers and casters | even | clocks twice as fast as actions |
| The 10, 20, 30 and 60 s the game is built on | 5, 10, 15, 30 turns | 4, 8, 12, 24 | 3, 5, 8, 15 |
| Regeneration (a tick each 2 s) | a tick a round | 1.25 | 2 |
| Storms (2 s) and fires (4 s) | 1 a round, 1 in 2 | 1.2, 0.6 | 2, 1 |
| A 4 s stun; a 2 s one; a 3 s one | 2 turns; 1; 2 | 2; 1; 1 | 1; 1; 1 |
| The 15 s in which a second stun is quartered | 8 turns | 6 | 4 |
| A potion's 5 s | 3 turns | 2 | 2 |

**Recommended: 2 s, as one setting (`seconds_per_turn`) that everything below reads**, so that 2 against 2.5 or 4 is a play test and not a rewrite. Two reasons past the arithmetic. It is the game's own beat: its combat regeneration, its storms and half its timers count in twos, as Pathfinder's hidden six-second round is why that game converts cleanly. And it rounds least: every hold of the game is a whole turn or more.

**The cost is the look of the numbers**: a 60 s cooldown reads "30 turns". It is the same cooldown: in real time it comes back once a fight, and so it does here.

**The length of a fight does not come from this number.** With one action a turn a fight lasts as many rounds as it has actions, about the real fight's seconds over two: a 40 s fight is some twenty rounds at any setting. The play tests say the same: the fights at Castle Cousland that were fought out, at level 1 to 3, ran 5 to 17 rounds (6, 7, 8, 8, 8, 11, 11, 12, 13, 17 among them; the staged crowds 24 and 38; most of the many under 5 were staged or ended through the test tool). That is long by the genre's habits. If it is too long, the knob is how hard blows land, not the turn: Pillars of Eternity's second try at a turn-based mode raised all damage by a third for that reason. `ai_turn_speed` shortens the wait, not the fight.

## The rules of conversion

Written against `seconds_per_turn`, here `T`. Each says what it reads, so it can be seen that a new talent needs nothing added.

**1. One clock for the fight, moved `T` a round.** Effects' time and regeneration are held through a fight and advanced as now (`dao::clock`), by `T` for each round rather than 6 s. Spread through the round, a share after each turn (`T` over the number of turns in the round), not all at the round's start: as built, an effect put on by the last to act loses a round's worth before anyone has had a turn under it, and everything that ticks does so in one burst. Reads: nothing of the ability. Changes `effect_seconds_per_round` into `T`.

**2. Script timers on the same clock.** A `DelayEvent` asked for during a fight for a second or more is a duration: it is held and fired when the fight's clock reaches it. One under a second is sequencing (a bomb's spread after 0.1 s, a summon's 0.5 s) and runs with the world as now. Damage over time then ticks `T` / 1.5 times a round and an Inferno once in two rounds at 2 s, whoever is thinking for how long, and a mod's own chain is covered without knowing it. A short list by event type can name exceptions either way. Reads: the delay asked for. Needs DAOLib (below).

**3. A cooldown is its seconds over `T`, rounded up, in its user's own turns.** As built; `cooldown_seconds_per_turn` becomes `T`. Reads: `Cooldown`.

**4. A hold costs its own duration over `T`, to the nearest turn and never less than one.** For a stun, paralysis, sleep, freeze, root, fear or grab: the effect's duration as the engine holds it when it lands (`CGameEffect::Duration`), which is after the game's own shortening for rank, difficulty and a recent stun. At 2 s: Scattershot 1 turn, Mind Blast 2, Dirty Fighting 2, Cone of Cold 4, Holy Smite 5, Paralyze 8; Paralyze on an elite boss on normal (3.75 s) 2. The rest is as built: the turns are the victim's own, taken at their start, the effect removed when they are spent, and given back if something cures it. `condition_turns` becomes the fallback for an effect whose duration cannot be read. Reads: the effect's flags and duration.

**5. A knockdown costs its animations' time over `T`.** The fall, the effect's own second or less, and the getting up: 3.5 to 4.5 s for a person, so two turns at 2 s and one at 4, and for a creature by its own getting up (a mabari's 1.5 s, an ogre's 3.8). Reads: the lengths of the knockdown's animations for the creature, which the engine has (below, "Asked of DAOLib"); until then a setting by the kind of effect (`knockdown_seconds`, 4), not by ability.

**6. What is neither hold nor timer needs no rule.** A buff, a debuff, a summon's life, a poison on a blade, the mark that quarters a second stun: each is an effect with a duration, on the clock of rule 1. Sustained abilities' reserves and fatigue have no time in them. Regeneration and the modes' drains are properties the clock ticks.

**7. A cast charges for its conjuring time over `T`, to the nearest turn, and what interrupts it is the game's own rule.** As built for the charge: it is begun on one turn and cast at the start of a later one. Two changes. The conjuring time is the engine's, the start animation and its loops ("Conjuring time" above), not the table's number read as seconds: 3.47 s for Inferno, Blizzard, Tempest and Death Cloud, 3.87 s for Chain Lightning and Mass Paralysis, the same for the shapeshifts and the ranger's summons, and nothing for the rest. And to what interrupts it (a hold, a fall) add physical damage over the game's threshold, by the game's formula. At 2 s a turn those spells charge two turns and are cast on the third, by the user's own rule for a two-turn charge; at 4 s, one turn. That is the issue's third point exactly, it lands on the six spells that gain most from turns, it is the only place the game itself allows an interruption, and a mod's slow or ritual spell (twelve to twenty seconds) charges six to ten turns with nothing added. Reads: `ConjureAnim`, `Speed`, the animations' lengths; the caster's Combat Training ranks or its rank's; the damage event.

**8. Initiative, reach and movement** have no real-time counterpart and stay as designed.

## Points as time (the user's second sketch, 2026-10-01)

Superseded on 2026-10-02 by a bar of 4 s ("Decided: a turn is four seconds of time"); kept for its arithmetic.

After reading the section below, the user: "you suggested points but then talk about major / minor actions. perhaps 1 points should be 0.5s. i think most things take at last that time. so player starts w/ 4 points, 2s. an attack is 4 pts. then at levels 5, 10, 15, 20 gain a point each. combat tactics can give a point per level. time should be converted to points, rounding. a spell that takes 10s costs 20 points. this means 5 turns at 4 points per caste (cast on the 5th). speed buffs and cast time reductions decrease points needed." And asked whether half a second is fine enough, or a quarter.

**The view here: the one currency is better than the main and lesser actions below, and should replace them.** A point is half a second, a turn is four, everything costs its own time, and nothing needs a category. Three things fall out that the section below needed rules of its own for:

- **One attack a turn, for the common case, by arithmetic**: a sword or a mace costs the whole turn.
- **Speed**: a dagger at 3 points strikes four times in three turns, two daggers twice a turn, and a hasted sword at 3 strikes a second time every third turn, which is the "banked" second attack below with no banking rule.
- **A potion is paid for**: 2 points is half a turn not spent attacking, as its 1.23 s is in real time. Healing beside an attack, the thing turns make too strong, is gone.

| Costs at half a second a point | Points |
|---|---|
| A dual wielder's hand: a dagger's, a longsword's | 2, 3 |
| Dagger; longsword, waraxe, mace; greatsword, maul, battleaxe | 3; 4; 5 |
| Potion; bomb; trap | 2; 2; 7 |
| Below the Belt, Shield Bash; Dirty Fighting, Cripple | 3; 4 |
| Flurry, War Cry, Taunt, Mighty Blow, Critical Strike | 5 |
| Punisher, Overpower; the Sunders, Shield Pummel | 6; 7 |
| An instant spell: its cast alone, or with its recovery | 2, or 4 |
| Inferno, Blizzard, Tempest, Death Cloud; Chain Lightning, Mass Paralysis (conjuring and cast) | 9; 10: cast on the third turn |
| A mod's slow spell | 25: cast on the seventh |

Against real time the plain attacks come out within 7% (a longsword 5% slow, a dual wielder's longsword 7%), the rest exact.

**It needs three things said that the sketch leaves open.**

1. **What happens when a cost is more than the turn.** This is not the rare case: a two-hander's swing is 5, and over half the talents are 5 to 7. For a spell that conjures, the sketch's answer is right and is the charge rule: paid over turns, cast when paid, open to interruption meanwhile. For a swing or a talent, waiting would mean a greatsword never lands on the turn it is ordered. Better: **it is done now and the rest is owed from the next turn**, to at most a turn's worth. A greatsword then swings four turns in five and loses the fifth, which is its real rate, and Shield Pummel costs its turn and most of the next.
2. **Points not spent carry over**, to a cap of two or so. Without it a dagger's spare point is lost each turn and it strikes as often as a sword; with it the rates above hold.
3. **An attack is its weapon's time, not a flat 4.** A flat 4 is Pillars of Eternity II's mistake in small: the dagger and the greatsword strike as often.

**Half a second or a quarter.** Half is enough for what an action costs, as the table shows. It is too coarse for speed effects if the cost is rounded after they are applied:

| Attacks gained | Really | At half a second, by weapon | At a quarter |
|---|---|---|---|
| Swift Salve | +25% | nothing (a dual dagger) to +50% (a dagger) | +20% to +50% |
| Haste | +33% | nothing to +50% | +20% to +50% |
| Momentum | +43% | +25% (a greatsword) to +100% (a dual dagger) | +33% to +60% |
| Precise Striking | -9% | nothing, or -17% (a greatsword) | nothing to -14% |

A quarter second narrows it and does not cure it: a tenth more or less of a two-second action will never round well. **Recommended: half a second, and keep the fraction instead of rounding it.** An action at rest costs whole points; a speed effect multiplies the cost and the fraction is kept (a hasted sword 3, a mace under Swift Salve 3.2, a dual dagger under Haste 1.5), with what is left over carried. Every effect is then worth exactly what the game gives it, the numbers on the bar stay small, and the bar shows whole pips and one partly filled. If whole numbers everywhere matter more than that, a quarter second (8 points a turn, a sword 8, a potion 5) is the second choice. The seconds in a point should be a setting either way.

**"Speed buffs and cast time reductions decrease points needed"**: yes, and for nothing, since a cost is a time. The game's own speed effects touch plain attacks only (the attack speed modifier and the archer's aim), not talents or spells, and the points would follow the game in that.

**Points from level: not recommended.** Four more by level 20 and four from Combat Tactics is 12 points, six seconds: three sword attacks a turn, where the user's own line was that two "might be too imbalanced". And it breaks the one thing the conversion stands on, that a turn is the same seconds for everyone and every clock. Cooldowns, durations and regeneration are sized against 2 s of fighting; a character doing 6 s of it a turn has them at a third of their weight, and if enemies gain the same by level the turn has simply become 6 s, with the bursts that come with it. In real time a character at level 20 acts no faster than at level 1: the game's growth is in what a blow does, not how many there are.

**Combat Tactics: the bank, not the budget.** A point a rank is a quarter more of everything a rank, twice the turn at rank 4, for a skill that needs only Cunning: every character would take it first. What suits the skill in this model is how many unspent points carry over: 1 for anyone, one more a rank. It gives no more time, only the freedom to save it for a turn that matters, which is what tactics are. If it must give points, one at rank 4.

**One talent or spell a turn** needs no rule at 4 points, since two do not fit; it is worth keeping as a rule only if the budget ever grows.

**Movement stays its own budget** (below), outside the points.

## Action points: the earlier shape, a main action and lesser ones

Superseded by the section above if the user takes it; kept for its tables and its reasons. The user's first sketch and ideas, made into rules over data. A turn has **one main action, lesser actions bought with points, and movement**.

**The budget.** 4 points a turn. A point is about a second of the action's own time. A turn's clock is `T` whatever was spent: the points cap how much fits in a turn, and most turns will not use them all.

**The main action: one a turn.** A plain attack, or an activated talent or spell (`UseType` 4, `AbilityType` 1 or 2), or an item aimed away from its user (a bomb thrown, a trap set), or Stealing. Using one locks the others out for the turn, which is "one talent or spell a turn" and "one attack a turn" as one rule. Its cost in points is its own time in seconds, rounded, a half going up:

| Main action | Points | From |
|---|---|---|
| An attack | 1 for a dual wielder's hand, 2 for a dagger, sword, mace, bow or staff, 3 for a two-hander | `CalculateAttackTiming`'s terms: the style, `DSpeed`, the modifier |
| A talent | its animation's: 1 for Below the Belt; 2 for Shield Bash, Riposte, Dirty Fighting, Cripple, Flurry, Whirlwind, War Cry, Taunt; 3 for Mighty Blow, Critical Strike, Two-Handed Sweep, Deadly Strike, the Sunders, Punisher, Overpower, Assault, Shield Pummel; an archery talent a shot and its 1.5 s aim, 3 | `CastAnim`'s length; `Flags` |
| An instant spell | 2: the cast and its recovery, 1.5 to 2.3 s | `CastAnim` |
| A spell that conjures | all of them: it charges (rule 7) | `ConjureAnim`, `Speed` |
| A bomb; a trap | 1; 4 | their animations: a throw of 1.0 s, a crouch of 3.5 s |

The cost of the main action decides only what is left beside it, since there is one: a quick talent leaves room for a potion and a mode, a three-blow one for a single lesser action. All of it is the game's own data, so a mod's talent is costed by the animation it names.

**Lesser actions: with the points left.**

| Lesser action | Points | A turn | What makes it one |
|---|---|---|---|
| An item used on oneself: a poultice, a potion (a drink of 1.23 s), a salve, a kit | 1 | one | `AbilityType` 4, `TargetType` the user |
| A poison or a coating | 1 | one | the same; told from the row above by its 60 s enchantment, or counted with it |
| A sustained ability turned on | its animation's seconds, at least 1: 1 for the weapon modes with none (Shield Wall, Powerful Swings, Aim) and for a mage's (a cast), 2 for Threaten, 2 or 3 for Berserk, the songs and the auras (2.5 s). A shapeshift or a ranger's summons conjures, and charges | as the points allow | `UseType` 2; `CastAnim`, `ConjureAnim` |
| A sustained ability turned off | free | any; it waits for the next turn before it can go on again, as built | |
| The off hand's strike, for a dual wielder who has attacked | the hand's swing: 1 | one | the weapon style |
| A door, a chest, a lever, a thing picked up | free, as the user decided, or 1: asked before and still open | | |
| Switching weapon sets | free | one | |

"One of a kind a turn" is from Pillars of Eternity: its first turn-based mode let free actions be taken without limit and they were abused, and its second limits each kind to one a turn. Here it also stops four poultices in a turn, one of each tier, which their separate cooldowns would otherwise allow. Modes are limited by points alone: a warrior who starts a fight with nothing on spends a turn or two putting on three, as the three animations cost in real time, and modes carried into the fight cost nothing.

In real time with pause, turning a mode off, switching weapon sets and changing equipment all happen at once while paused, as often as the player likes (the wiki's "Pausing Combat Action"). The limits above are where that stops.

**The larger poultice costing more**, as the user sketched, is the one thing here the data cannot give: the tiers differ only by a number in a script. It would be a short table of costs in `turnbased.ini` by item, the override the rest avoids. The other way is to leave them equal: the greater poultice is already scarcer and dearer.

**Speed, without two attacks as the norm.** Three things carry it:

- *A faster weapon leaves more points.* A sword and shield attack for 2 and keep 2; a greatsword attacks for 3 and keeps 1.
- *A dual wielder's off hand is a lesser action.* The engine already alternates hands from one attack command to the next, so the off hand's strike is a second attack command the gate lets through for a point. Two daggers then strike twice in a 2 s turn, which is what they do in 2 s of real time; two longswords gain 40% on real time, the price of whole numbers. It is Baldur's Gate 3's off-hand attack as a bonus action, and Pillars of Eternity II's "dual wield attacks with both weapons".
- *A speed effect earns a second attack, as often as it is worth.* A creature whose attack speed modifier `m` is under 1 banks `1/m - 1` of an attack each turn, and with a whole one banked may attack twice that turn, the second at its points. Swift Salve (0.8) is a second attack every fourth turn, Haste (0.75) every third, Momentum (0.7) three in seven, Swift Salve with Momentum (0.5) every turn. That is each one's real-time worth, a quarter to a half more attacks, and it reads one property, so any mod's speed effect is covered. The plainer rule, "hasted: two attacks every turn", doubles the attack rate for an effect worth a quarter more, and Haste is a sustained spell on the whole party: it would be the first thing every party took. If the banked attack reads as too slow in play, a multiplier on the banking (`speed_bonus_scale`) is the knob. Both Swift Salve and Haste also quicken movement (1.2 and 1.3), which the movement budget already takes. For archers the game adds to the aim instead, slowing them; the rule here goes by the one property and so gives archers the bonus the game's bug denies. With Dain's Fixes in, the game agrees.

**The archer's modes are the same question, and the sharpest case of it.** Aim adds 1.5 s to each shot for attack and damage, Defensive Fire 1.2 s for defence, and Rapid Shot takes up to 2 s off (3 with Master Archer) and gives up critical hits. Their whole trade is the rate of fire, through the aim's own property (`PROPERTY_ATTRIBUTE_RANGED_AIM_SPEED`, 24), not the attack speed modifier. With one shot a turn and nothing more, Aim is all gain and Rapid Shot all loss. So the banking goes by the whole attack's time against the same attack unmodified, aim included: Rapid Shot banks toward a second shot as Haste does, and what makes an attack slower is paid in points rather than in attacks lost, a shot under Aim costing 3 or 4 points where it cost 2. Precise Striking's tenth is under a point and costs nothing.

**A slower weapon is not owed anything**, and gains a quarter on real time at 2 s. If play shows two-handers ahead, the same banking can run on the weapon's own speed against the slowest (a longsword a second attack one turn in three), at the cost of two attacks being less rare. Not proposed to begin with.

**Enemies** are on the same budget. An AI's turn is as now: told to choose, its first main action let through, a second refused and the turn ended. With a banked attack it is told to choose once more. The AI turns its modes on by itself and pays their point.

**Combat Tactics.** With the main action capped, more points buy only more lesser actions, so the issue's idea is safe in a way it would not be if points bought attacks: a point more at rank 2 and another at rank 4, for 6. A turn that spends 6 is a spell (3), a potion, a poison and a mode. Two things to weigh. The skill needs Cunning 10 to 16, so warriors who leave Cunning alone are shut out of it. And under `party = tactics` it still gives its slots, so there it gives both. Enemies have no such skill; a rank's worth could come from the creature's rank as the interruption threshold does (a boss 4, an elite boss 6 "ranks" of Combat Training in `autoscale.gda`), or they keep 4.

**Considered and set aside: a turn as a budget of seconds that attacks fill.** A 4 s turn in which a sword strikes twice and a dagger nearly three times, each action costing its real time and the remainder carried over, is the exact conversion: weapon speed, Haste and potions all come out at their real worth with no special rule, and a fight takes half the rounds. It was worked through here first, and the arithmetic of whole points without carrying is worth keeping: rounded to whole seconds, two longswords gain 40% and a greatsword loses 38% against real time. Against it: several actions to a turn, which the user does not want, and which makes worse the one distortion no rule can remove, that a side's blows all land before the other answers. The shape above keeps most of what it gave.

**A simpler shape that does nearly the same: one main action and one lesser, no points.** It is Baldur's Gate 3's action and bonus action. Points earn their keep only where costs differ enough to matter: a fast weapon or a quick talent leaving room for two lesser actions against a two-hander's one, Combat Tactics giving more. If play shows nobody counting them, this is what to fall back to, with Combat Tactics giving a second lesser action.

## What no rule reaches

The escape hatch, kept short on purpose. Each is a setting by kind or a small table, with a default:

- **Times that are animations** turned out to be data after all (a talent's length, a conjuring, a knockdown, a potion), but in the game's animation files, which the mod does not read: until the engine is asked for them ("Asked of DAOLib"), defaults by kind stand in (a talent 2 points, a knockdown 4 s, a conjuring start 0.6 s and loop 3 s). A creature's attack that is not a humanoid's is its own animation's length, the same way.
- **Things the data does not tell apart**: the tiers of a poultice.
- **Script timers that are not durations though they are long**, or are though they are short: a list by event type for rule 2. The end of combat's 3 s and a fallen companion's 6 s are the known ones to leave alone.
- **An action that is being held**: a spider's Overwhelm, an ogre's grab (10 s), a dragon's (4 s). The attacker is at an effect for that long and its victim is held by it. As built, effects' time runs with the world while the attacker is at it, and the victim loses its turn if still held. By rule 4 the grab's time would cost both their turns: the ogre's next four at 2 s. Left as built until seen: a grab that plays out in one turn and costs the ogre nothing more may be the better game.
- **A per-ability override** of anything above, in `turnbased.ini` by ability id, as `charge` is today. For a talent whose script does what no rule can see. None is known to need it yet.
- **The AI's timers** (when it may change target, threat falling off by the second) run on the world's time, which in turns is a different amount each round. Small, and not reached.

## What gets stronger and weaker

After the rules, at 2 s a turn. Against real time:

- **Holds** stay about where they were against their cooldowns, which is what the play tests had to tune by hand (at 10 s a turn the archers' stun came back every other turn, and 4 was chosen to stop it). A boss shrugging off a stun in a turn rather than standing through it is the game's own rule arriving. The order of play still makes a hold worth more than its seconds.
- **Talents of several blows** (Punisher, Overpower, Shield Pummel, Assault, the Sunders) are one main action each though their animations run 3 to 3.5 s, a swing and a half or more. They gain. Their 3 points take a little of it back; if it is not enough, the same rounding that makes a long stun two turns could make a talent of 3 s or more take its user's next lesser actions, which is not proposed to begin with.
- **Knockdowns** are worth two turns at 2 s, not the one they cost today, and the game has sixteen of them on cooldowns of 10 to 20 s (Pommel Strike's is 10 s with no roll to hit). They were strong in real time and stay so: worth watching first among the holds.
- **Mages** lose the clock's favour they have today, and keep the favour of acting in order: areas that cannot be walked out of, combinations with no timing in them. Rule 7 is aimed at the first. One spell a turn is about their real rate, since a cast is about a turn long.
- **Two-handers** gain a quarter in attack rate and have the least to spend beside it. **Dual wielders** hold their rate through the off hand and pay their points for it. **Archers** lose a fifth in rate, lose the risk of being interrupted in melee, and gain free movement to keep away: opportunity attacks would matter most against them.
- **Healing** is stronger: a potion beside an attack rather than instead of one, and a heal chosen after seeing every enemy's blow. A point for it and one potion a turn are the limits.
- **Regeneration** is a tick a round, 1.5 to 4.5 for the party. A 40-point talent is ten to twenty-five rounds of it, as in real time: the pool is the fight's budget, and the Rejuvenate spells and lyrium are what add to it.
- **Poisons and timed buffs** last their real count of attacks again: a coating's 60 s is thirty turns.

## What other games did

**Pillars of Eternity II** (turn-based mode, 2019; the official wiki's "Turn-based mode"): a round is 6 s and everyone has one action. Recovery became initiative and weapon speed is ignored. A duration is its seconds over six, rounded down, never less than a round. Casts begin at once and finish later in the round, or in the next. Players' complaints (the Steam discussions): speed stats lost their worth ("+15% is a good bonus in real time but in 'we all get one turn each round'... not so much"), short effects all became one round and whether one did anything was decided by turn order, and fights with real time's numbers of enemies were long. Its director said in 2025 that the criticisms were reasonable and that free actions "felt abusable".

**Pillars of Eternity** (turn-based beta, November 2025; Obsidian's announcement): speed buys turns ("the number of turns you get to take per round is no longer tied to the round, but to your speed and recovery"), a weapon swap, a potion and a mode are free but one of each kind a turn, and damage is raised across the board to shorten fights. The director: "It's not a 1:1 translation of time because we did try that. But it does match the spirit of what dexterity should do." The lesson taken here: keep what speed meant, not its arithmetic.

**Pathfinder: Kingmaker**, by a mod (Hsinyu's Turn-Based Combat) and then officially: the rules already had six-second rounds under the real time, so the work was elsewhere, and its change log is a list of what to expect: effects lasting a round too long or too short at a turn's edge, a spell whose animation was a whole round, areas that moved in real time, the speed of others' turns, a surprise round. The mod's "max delay between iterative attacks" is the pacing of several blows within one action.

**Point systems**: Divinity: Original Sin 2 gives 4 points a turn, carries unspent ones up to 6, and makes Haste a point more; Fallout gives 5 plus half Agility and turns unspent points into armour; Wasteland 3 carries two over; Baldur's Gate 3 has an action, a bonus action (a potion, the off hand's attack) and movement. The shape proposed above is the last, with points for the bonus actions.

**No turn-based mod for Dragon Age: Origins was found** on Nexus or in forum searches; players approximate one by pausing. Nexus's own search could not be reached, so that is likely, not proven.

### Pillars as the reference: what its two modes teach (2026-10-02)

The user, 2026-10-02: "the turn-based mode should be smooth, balanced, challenging, and most of all, fun. it should be a real viable alternative to rtwp, like it was always meant to be there, like the first-party turn-based modes of pillars of eternity. perhaps pillars can be a good reference for design choices and features we'd want." So both modes were looked at again, with their reception: Deadfire's (January 2019; the official wiki's "Turn-based mode", which quotes the in-game help) and the first game's (beta November 2025, released April 2026 as patch 3.9.3; Obsidian's release notes as gogdb.org carries them, the Steam beta notes, PC Gamer's and RPGFan's reviews, the Steam discussions). What was praised and what was not, each against what this mod does. These are readings and proposals, not decisions.

| What Pillars found | Here |
|---|---|
| **Speed must buy something.** Deadfire gave everyone one action a round and ignored weapon speed: Dexterity became "the worst stat", heavy armour was free. The first game's mode made speed buy turns and was called "the way it's meant to be played". | The bar of time is this, taken further: a dagger swings 2.7 times in a turn and a greatsword 1.6, Haste and Momentum shorten every swing. Built, and seen in play (play test 25). |
| **Free actions capped.** Deadfire's unlimited free actions were exploited; the first game allows one weapon swap, one potion and one mode a turn. | A potion costs its drink (1.2 s) and a mode its animation; a weapon swap is free and uncapped: to cap at one a turn, or cost a second. Proposed: one free swap a turn. |
| **Fights must be shorter than a real-time fight feels.** Deadfire: a dungeon "took me 3 hours"; the first game raised all damage 35% to shorten fights, and its "max speed" was still called slow. | Unmeasured. To measure: the same fight in real time, in one-action turns and on the bar, in rounds, damage and minutes on the clock (the balance sweep). The levers, in order: enemies' turns faster (`ai_turn_speed`, 1 today), no waits between turns, then lethality if still slow. |
| **Short effects must not be wasted.** In both, a one-round effect that ends at its victim's turn start does nothing for a slow debuffer. | A hold takes its own seconds of its victim's bar, whenever it lands (the hold rule): to build next. |
| **Damage over time and pulses that land a round at once** leave no chance to react (the first game). | Effects age a round at a time at the round's start, the same fault. A creature's own clock mends it; needs DAOLib. Until then, to watch in play. |
| **Interrupts that only delay become a stunlock** (the first game, uncapped). | A hold is the game's own, with its own shortening for a recent stun (rule in "Durations"); nothing added. |
| **A pending cast is an entry in the tracker** (Deadfire), so interrupting it is a visible choice. | A pending action shown on its creature's card: to build. |
| **The tracker must show what is coming**: the first game's did not show enough of a fast enemy's turns. | Everyone has one turn a round here, and the bar shows the round and the next. An enemy's bar (how much it will do) is drawn on its turn. |
| **Delay Turn** (both; Deadfire's only before moving or acting). | Built as Deadfire's (2026-10-02, `[turns] delay`): before anything is spent, to the round's end, once a round. Kept (the user, the same day). |
| **Enemy turns: slow, and an AI that stalls or skips** (both). | An AI that chooses nothing passes after `ai_think_ms`; with time it chooses again until its bar is spent. Its turns at `ai_turn_speed`. To be judged in long fights with several kinds of enemy. |
| **The mode switched freely** between fights (the first game: options, a key, a HUD button); Deadfire's lock at a new game was resented. | Built: the options' toggle and a key, outside a fight. |
| **Path preview and a readout of what is left** (both); the cursor reddens when a move would provoke (the first game). | Built: the path on the ground, gold and red, with its length; the bar of time with the hovered order's cost on it is being built. Nothing provokes here: leaving melee costs only its time, as decided. |
| **Encounters tuned for real time become a slog** (both; Sawyer: "fewer encounters, smaller encounters, but much more tactical"). | Not the mod's to change. The switch between fights answers it: a trash fight in real time, a hard one in turns. |

## Thoughts on the ideas floated

The user asked for an opinion on each, with everything above in hand. Plainly:

**"A set of rules to automatically convert."** Yes, and it is more reachable than it sounds, because the game is already written in one unit. Rules 1 to 7 name no ability. What they cannot see is short and of two kinds: lengths that live in animations, and the few places a script hides a duration from the engine. The first turned out to be data like the rest, once looked for: every animation's length is in the game's files and the engine works conjuring time out from them itself, so the mod can ask the engine. The second is rule 2. A per-ability table should exist as a way out and start empty.

**"Maybe each turn should represent 2 s."** Yes, if a turn is one action, and no if it is several. The number is not a free choice: it is whatever a turn's worth of fighting takes in real time. The trouble today is not that 4 is wrong but that the mod has one action a turn beside clocks at 4 and 6, so it is neither. 2 with one action is consistent; 4 with attacks filling the time would be too.

**"I don't want a turn to be too many actions ... normally 1 attack, talent."** Agreed, and the case is stronger than taste. The worst thing turns do to this game is let a whole side act before the other answers, and that grows with how much a turn holds. Short turns are nearer to real time, not further. The price is that speed stops being free, and Pillars of Eternity II shows what happens when that price is not paid: with one action each, its speed stats became the ones to dump. So one action a turn stands or falls on what is done for speed, which is the next idea.

**"Only in rare cases should attacking 2x be supported, e.g. after taking haste potion."** The right instinct and the wrong size, if it means twice every turn while the effect lasts. Swift Salve makes attacks a quarter more frequent for a minute; two attacks a turn is twice as frequent. Haste is on the whole party for as long as the mage holds it. Banked, a second attack comes every fourth turn under the salve and every third under Haste: rare, as asked, and worth what the game priced it at. The same rule has to cover the archer's Rapid Shot and Aim, or those modes stop being a choice. And a dual wielder needs the off hand's strike, or the style built on attacking twice as often attacks as often as a maul.

**"Additional action points might be spent on applying poisons, drinking potions; toggle a sustained ability on (off free)."** Agreed on what the points are for. Two cautions. Healing is the thing turns make strongest, since a potion is chosen after seeing every blow and is taken beside an attack rather than in place of one: a point and one a turn is the least it should cost, and whether it should instead be the turn's main action is worth a play test. And the points must not grow into a second economy. Four, one main action, a handful of lesser ones: if the bar needs more than a row of four pips and the main action's mark to show it, it has become the thing the user said they did not want.

**"APs can be increased via tactics skill rank."** Safe once the main action is capped, since more points then buy only lesser actions; it would not be if points bought attacks. It is also a little thin: a fifth and sixth point matter in few turns. Another thing for the same skill, alone or with the points, is the order of play: a rank's worth of initiative, or the right to delay a turn, which is what tactics are in a game of turns. Not costed here.

**What matters most is none of the above.** In order: one rate for every clock (the mod's three disagree today); script timers on it (burns and storms are outside it); holds from their own durations (bosses and repeat stuns are the game's balance against stun-locking, and a flat turn discards it). Those three are corrections to what is built, need no decision about points, and change how every fight plays. Action points are the more interesting design and the smaller change.

**What is most likely to be wrong in play is the length of fights**, not any cost. Ten to seventeen rounds at level 3 is already long by the genre's habits, and the game's later fights are larger. The first thought here was that blows would have to land harder on both sides; the user's answer is the better one (decision 11): run the fight faster and leave the numbers alone, since any change to damage is a rebalance of everything this page works to keep.

## Decisions for the user

As of 2026-10-02 ("Decided: a turn is four seconds of time"): 0, a turn of seconds that actions fill; 1, 4 s; 2, a hold uses up its target's own time; 3, 4 and 12, answered by the bar, every action at its own time; 5, a spell costs its real conjuring and cast, begun and landing later when it does not fit; 6, rollover and time by rank, alternating. 9 and 11 were decided before. 7, the tiers cost the same, a drink each, with no limit beside their cooldowns. Still open: 8 and 10.

0. **One main action a turn** (recommended), with the next three as what it costs; or a turn of seconds that attacks fill ("Considered and set aside").
1. **A turn's seconds**: 2 (recommended), 2.5 or 4, as one setting replacing `cooldown_seconds_per_turn`, `effect_seconds_per_round` and the casting rule's turn. Everything else follows from it.
2. **Holds from their own durations** (rule 4), in place of one turn each.
3. **The off hand's strike as a lesser action** for dual wielders. Without it they attack at half their real rate.
4. **A speed effect's second attack**: banked at its real worth (recommended), or every turn while it lasts.
5. **The spells that conjure charge by their real conjuring time** (rule 7): at 2 s a turn Inferno, Blizzard, Tempest, Death Cloud, Chain Lightning, Mass Paralysis, the shapeshifts and the ranger's summons take two turns and land on the third, and damage can interrupt them. Recommended; the other way is to leave the charge rule on the table's numbers, where nothing charges.
6. **Combat Tactics**: a point at ranks 2 and 4 (proposed), a point a rank, or something other than points.
7. **Poultice tiers**: equal cost, or a table.
8. **Doors, chests and pickups**: free, or a point. Still open from `dev/DESIGN.md`.
9. **Movement at 2 s a turn: decided, 9 m** for a person, as now ("4.7m per turn is too little. 9m default (for normal human) is fine", the user, 2026-10-01).
10. **Dain's Fixes**: whether the mod assumes it. Several things above are bugs it mends (Haste with Momentum, speed effects slowing archers, Master Combat Training not counting for interruption).
11. **Fights too long: decided, by the game's speed, not by damage** (the user, 2026-10-01: "fights too long should probably be addressed by speeding up combat time. we already know how to do that. that can be a setting, 0.5x, 1x, 2x speed. otherwise we'd have to rebalance all the numbers. besides, turn-based players are willing to take their time"). `ai_turn_speed` does it for AI turns today (0.25 to 4); a setting for the whole fight, the player's own actions included, is the addition.
12. **Points as time** ("Points as time" above): half a second a point and four a turn (recommended, with the three additions there); whether a speed effect's fraction is kept or the point is a quarter second; no points from level and Combat Tactics as the bank (recommended), or the sketch's point a rank and four by level 20. If taken, it replaces decisions 3, 4, 6 and 7 and the main and lesser actions.

## How it would be built

In stages, each playable.

**First, to see in play**, since rule 2 rests on them and the lengths read from files should be seen once:

- a burn's ticks against the rounds it lasts, in a fight of two and of ten, with the world idling on the player's turn and paused;
- which clock `DelayEvent` counts on, and whether it stops with the pause;
- Inferno from the order to the impact with a staff (read: 3.47 s of conjuring, then the cast), and an instant spell's;
- a knockdown from the blow to the victim's feet (read: 3.5 to 4.5 s);
- whether a talent's command ends with its animation (Shield Pummel, read: 3.47 s) or before it;
- regeneration given by `dao::clock::Advance`, which has not been watched.

**A correction to what is built**, apart from any decision: the charge rule takes an ability's conjuring time as the conjuring table's number in seconds (`dev/DESIGN.md`, "Casting time"), and that number is a count of loops. Nothing shipped charges either way at 4 s a turn (3.47 and 3.87 s are just under it), so nothing plays wrong today; a mod's slow spell would charge one turn where the engine conjures it for twelve seconds. It wants the engine's figure, the first item asked of DAOLib below.

**Stage 1, one rate.** `seconds_per_turn`; cooldowns, the clock (spread through the round) and casting read it; holds take their turns from the effect's duration. Core: the conversions as functions in `src/core/Rules` with tests, a mutant for each. No action points yet; the game it makes is today's with honest clocks.

**Stage 2, script timers on the clock** (rule 2).

**Stage 3, the points.** `TurnMachine::Budget` gains points; `Classify` answers main or lesser and a cost, from what the gate reads of the table and the weapon; one of each kind; the lockout; the caption on the turn bar and refusals that give the numbers ("needs 2, 1 left").

**Stage 4, speed.** The off hand's strike; the banked attack.

**Stage 5.** Interruption by damage (rule 7); Combat Tactics' points; the engine's animation lengths in place of the defaults by kind.

**Asked of DAOLib** (to be copied into `dev/DAOLIB_NEEDS.md` when the rules are decided, not before):

0. **An animation's length for a creature, and an ability's conjuring time.** The engine's routine that sets a cast up (0x00744542 on; the ability table read through the object at 0x00C1D090) asks the creature's animation object for an animation's length in milliseconds (0x00694E50, an animation id), finds the loop the start goes on to (0x005CA6B0), asks whether the creature has it (0x00695390), and makes the conjuring time the start's length plus the loop's times the conjuring table's count (table 135). Wanted: `AnimationLength(creature, animation)` and, over it, `ConjureTime(creature, ability)`. With the first alone the mod has a talent's length (`CastAnim`), a potion's, a knockdown's fall and getting up, and a creature's own attack, for whatever creature and whatever mod's animations, with no file read. What the three functions are called and what object they are on was not read: only how the cast's setup uses them.
1. **Script timers**: what `DelayEvent` counts on; a way to take a delayed event at its asking and signal it later, or the delayed events' queue held and advanced with `dao::clock`. Intercepting the script command is the pattern `ScriptHooks.h` has; whether an event taken that way can be kept and signalled later is the question.
2. **A creature's effects, listed**: type, duration, time left, the ability that put it there. For holds' durations (rule 4) without waiting on a status read. `CGameEffect` has the figures; the list of a creature's is what is missing.
3. **The damage a creature takes**, as an event with its amount and kind, for interruption.
4. **Advancing the clock by a fraction of a round many times a round**: whether many small `Advance` calls cost anything, and whether regeneration's 2 s accumulator carries across them as it should.

## Sources, and what is not established

Scripts read: `core_h.nss` (attack timing, regeneration's defaults, the stun mark), `combat_h.nss`, `rules_core.nss` (interruption), `ability_core.nss` and `ability_h.nss` (costs, cooldowns, the aim loop), `effect_dot2_h.nss`, `effect_upkeep_h.nss`, `sys_resistances_h.nss` and `core_difficulty_h.nss` (durations by rank), `player_core.nss` (the party's regeneration), `talent_constants_h.nss`, `spell_constants_h.nss`, `monster_constants_h.nss`, `item_constants_h.nss`, the `spell_*`, `talent_*`, `monster_*` and `item_*` scripts, `sys_traps_h.nss`, `ai_threat_h.nss`. Tables: `abi_base`, `itemstats`, `bitm_base`, `autoscale`, `properties`, `spellconjuringspeeds`, `persistent`, `exptable`, `anim_base`, `anim_combat`. The engine's regeneration tick, the two attack-timing commands and the cast's setup (how conjuring time is made) were read from the executable by disassembly (`..\DAOLib\tools\daoexe.py`; the code that reads a column is found by the column name's hash as an immediate). Animation lengths: `packages\core\data\anims.erf`, 4,519 animations each with a `.gad` (a GFF4 file: `AnimationGeneralAnimName`, `AnimationAnimLength`) and 1,113 blend trees (`.anb`: `AnimationTreeNodeName`, `AnimationTreeNodeFile`), read with DAOLib's `tools/gff4.py`; the humanoid trees are `combat_humanoid`, `staff`, `ws`, `tw`, `dw`, `bow`, `cbow`. The scripts used for the reading are scratch, in `build\rebalance\`, not in the repository.

Wiki pages, through the API: Skills, Talents (Origins), Spells (Origins) and the pages they link, Combat mechanics (Origins), Ability mechanics, Regeneration, Fatigue, Rank, Spell combinations, Swift Salve, Haste (Origins), Patches (Origins). Other games: pillarsofeternity.fandom.com/wiki/Turn-based_mode; obsidian.net/news/eternity/public-beta-for-turn-based-mode; github.com/hsinyuhcan/KingmakerTurnBasedMod (its `ChangeLog.txt`); divinity.fandom.com, fallout.fandom.com, bg3.wiki and wasteland.fandom.com on their points.

Not established:

- That a command lasts as long as its animation's file says: the lengths are the clips', read from files and not timed in play. The one check there is agrees: plain attacks' clips match the script's timing and the play tests' 1.74 s.
- What the engine does with a conjuring time of nothing (an instant spell): taken here as no conjure phase at all.
- Which stance's tree a caster with a sword uses, and whether sheathing is added to the cast (the wiki says an Arcane Warrior sheathes first for many spells).
- Creatures' own lengths were read only for knockdowns; their attacks and abilities are in the same files and were not tabulated.
- Which clock `DelayEvent` counts on. The reading of damage over time under today's build follows from the code and has not been seen.
- A whole shot's seconds: how the engine joins the draw, the aim and the release.
- Whether the shipped compiled scripts match the toolset's sources read here.
- Awakening's durations: its scripts are not in the extract, though its rows are in the table.
- The other games' details where a source could not be opened: the Obsidian forum's threads on Deadfire, the PC Gamer piece on the Pillars of Eternity beta and the figures of its patch notes (a secondary site only), Owlcat's own rules for a full-round cast.
- That no turn-based mod for the game exists.

## Review concerns after the four-second-bar decisions

Review of the proposal at `a7e1b66`, recorded at the user's request. These are design deductions from this document, not independently verified engine findings, new decisions, or implemented behavior. The four-second bar remains a reasonable direction: weapon speed matters naturally, movement competes with attacking, and new abilities can use the same conversion rules. The concerns are chiefly about what advancing time means.

### Combat Tactics changes more than action capacity

If effects and cooldowns follow time spent from the bar, a character with 5 s per turn recovers cooldowns faster per round, consumes buffs and debuffs faster, takes periodic damage and receives regeneration faster if both use that clock, and recovers from a fixed-duration stun in fewer turns. This can be coherent, but it makes Combat Tactics an acceleration of the character's internal clock, not only extra actions.

The rule that a lost turn advances 4 s needs clarification: does a stunned rank-four character advance 4 s or 5 s? The review preference is that extra action capacity should not automatically produce faster poison ticks or shorter buffs, though separating action capacity from elapsed effect time adds complexity. Decide this explicitly before implementation.

### Rollover needs exact accounting

Suppose a character spends 3 s and banks 1 s. Does its cooldown advance by 3 s or 4 s? Advancing by 4 s now and again when the banked second is spent counts that second twice. Advancing by 3 s makes saving time postpone cooldowns and harmful effects. Either interpretation needs an explicit rule; the statement that rollover adds nothing over a fight does not settle timer accounting.

Write out a two-turn example covering spending, banking, passing, and being stunned. Account separately for time spent, time forfeited, time banked, and time advanced on each timer; no second should advance the same clock twice.

### Impact time is not total action duration

Animation length, command duration, and time to impact may differ. A sword can hit before its animation finishes, and a multi-hit talent has several impact times. Paying the entire cost and then executing the command is a possible approximation, but puts every hit after the logical cost has been paid. Executing the command normally and charging afterward can instead resolve a burn, death, or interruption too late.

Eventually distinguish wind-up, impact, and recovery. For the first version, define the approximation explicitly and test an ordinary attack, a multi-hit talent, and a long spell crossing a turn boundary. Animation playback must not advance timers a second time for time already charged logically.

### Ground effects need an explicit clock

"Presumably their caster" leaves important cases open. Does Combat Tactics shorten Inferno's lifetime? What happens when its caster dies or leaves the fight? If pulses follow each victim's clock, do faster characters take more pulses before the area expires?

The review preference is a round-based lifetime for independent ground effects, with explicit exposure and pulse rules. Test a creature entering and leaving an area within one turn, as well as victims with different time budgets. This is a proposal to evaluate, not a replacement decision.

### Movement cost does not establish that equal-speed kiting is balanced

Equal movement costs cancel in the budget, but not necessarily tactically. An archer can shoot and retreat; a pursuer must close before attacking and may arrive with too little time to finish a swing. If the swing becomes pending, the archer may move again before it lands. Target movement, reach checks, and pending-action cancellation determine the outcome.

Try the agreed rules without opportunity attacks first, but treat equal-speed kiting as a play-test question rather than a settled consequence of the arithmetic.

### Practical additions and rules to clarify

- **Deliberate waiting:** allow "wait X seconds" so a player can let a cooldown expire mid-turn without shuffling around to spend time. Waiting must use the same timer accounting as other elapsed time.
- **Time preview:** show the expected result on the bar, such as "move 1.2 s + attack 1.9 s; 0.9 s left" or "finishes next turn; 2.4 s remaining". Precise costs should not require the player to do arithmetic.
- **Interruption and invalid-target refunds:** proposed default: elapsed time stays spent and unspent time is released; mana and items follow their actual commitment point. A target dying should not refund seconds already spent casting. Clarify what "given back" means for a pending command whose target is gone.
- **Survival's initiative bonus:** treat it as a substantial combat benefit. It does not add actions directly, but with several attacks or spells per turn it can decide whether an opponent survives to act at all.

### First scenarios to test

Keep the four-second bar as the prototype's direction, settle clock accounting, then test:

1. A swordsman pursuing an equal-speed archer, including an attack pending across turns and its target moving away.
2. A caster interrupted across turns, checking elapsed time, remaining time, impact, cooldown start, and resource consumption.
3. A poisoned or stunned character with Combat Tactics bonus time and rollover, comparing spending, waiting, passing, and losing a turn.

These scenarios exercise interactions between the rules before broader balance tuning. Independent ground-effect lifetime and exposure need their own small test as described above.

## Answers to the review concerns

From the session that took the decisions above, 2026-10-02, at the user's request. Proposals, not decisions, until the user takes them.

### One rule for the clock: four seconds a turn, whatever the bar holds

The first two concerns come from a rule the decisions never stated: how far a creature's own clock moves in a turn. Proposed: **exactly 4 s a turn, always.** The bar is how much a creature can do in its turn, not how much time passes for it. Within the turn the clock moves in proportion to the bar spent: four seconds times what has been spent over the turn's whole bar.

- **Combat Tactics is capacity, not a faster clock.** A rank-four character's 5 s bar holds more, and its poisons, buffs, cooldowns and regeneration still move 4 s a turn. The same for a boss's time by rank. A stunned creature's lost turn moves its clock 4 s, whatever its rank.
- **Rollover is capacity too.** Banked time moves no clock when it is spent, so nothing is counted twice (wrongly put: corrected in "Answers to the follow-up review"). Spending, banking, passing, waiting and losing the turn all move the clock exactly 4 s.
- **A pending action's spill** is the first part of the next turn's bar, so the clock is that share into the turn when the action lands.
- **For a creature with 4 s and nothing banked, the clock is the bar**, second for second, as the decisions put it. The rule changes nothing in the ordinary case.
- **The cost**: for a creature with a bigger bar, a cooldown lifts at a scaled point on the bar rather than at whole seconds of it. The bar shows where (below, "Time preview"), so the player does no arithmetic.

This replaces the decisions' list of what a creature's own time counts (what it spends, what it gives up, a lost turn's 4 s, rolled-over time when spent).

The two-turn example the review asked for: a rank-four character (5 s a turn, up to 2 s rolled over), with a longsword (1.9 s), a talent of 1.8 s with a 10 s cooldown used first in its first turn, and a poison on it with 6 s left. The cooldown starts as the talent lands, 1.8 s into the bar, 1.44 s into the clock.

| | Bar | Spent | Banked | The clock moves | Cooldown left | Poison left |
|---|---|---|---|---|---|---|
| Turn 1: the talent (1.8 s), then a swing (1.9 s), then ends | 5 s | 3.7 s | 1.3 s | 4 s (3.7 / 5 of it, 2.96 s, as it acts; the rest at the end) | 7.44 s | 2 s |
| Turn 2, spending: three swings (5.7 s) of 6.3 s | 6.3 s | 5.7 s | 0.6 s | 4 s | 3.44 s | gone 2 s into the clock, as 3.15 s of the bar is spent |
| Turn 2, passing at once | 6.3 s | 0 | 2 s (the cap) | 4 s | 3.44 s | gone |
| Turn 2, stunned | none | 0 | 0 (dropped) | 4 s | 3.44 s | gone |

Every row moves the clock 4 s and no second twice; what differs is only what the creature did with its bar.

### Impact time is not total time

(Reordered in "Answers to the review of the latest timing responses": the command starts, then its time is paid.) Agreed. For the first version: **an action's whole cost is charged when it is ordered, and timers move then.** One that fits plays as the game plays it, with effects' time held, so its animation ages nothing again. One that does not fit is kept and given to its creature when its time is paid, and plays out then. So every hit lands at or after the moment its cost is paid, which is the approximation the review names; it is the simple one and errs late, never twice. Wind-up, impact and recovery apart is for later. The review's three tests (an ordinary attack, a talent of several blows, a long spell across a turn's end) are the right ones.

### Ground effects

(Pulses corrected in "Answers to the follow-up review".) **Take the review's preference: a lifetime by rounds**, not by its caster's clock. An area ages 4 s a round, spread through the round as the round's turns are taken, so its caster's death, departure or Combat Tactics changes nothing. **Pulses go by the usual turn-based rule**: an area acts on a creature that begins its turn in it or enters it during a turn, once each time. A fast creature does not take more pulses for being fast, and one that walks through takes one. The review's test (into and out of an area in one turn; victims with different bars) stands.

### Kiting

Agreed, it is for play tests; the arithmetic shows only that the time is even. (The refund below is corrected in "Answers to the follow-up review".) One rule for it: **a pending attack whose target has gone out of reach by the time it lands is given back** under the target-gone rule (below), so an archer stepping away from a pending swing costs the swordsman the swing's unspent time, not the whole of it.

### The practical additions

- **Waiting a set time**: yes. Under the clock rule it is spending the bar on nothing, so it needs no accounting of its own.
- **Time preview**: yes, as already meant for the bar on the experience bar: the hovered action's cost shaded on it, gold if it fits and red if it spills, with the numbers ("move 1.2 s + attack 1.9 s, 0.9 s left"; "lands next turn, 2.4 s left"), and where a cooldown will lift.
- **Refunds**: agreed. Time already spent stays spent and only what was not yet spent is given back; for a pending action "given back" means the part of its cost the next turn would have paid. Mana and items follow the game: mana is spent when conjuring begins, so an interrupted spell's mana is lost, as the game's own interruption does. (Withdrawn for the first version in "Answers to the follow-up review".)
- **Survival**: agreed that it is substantial, for the reason given. It stays at +2 a rank until play shows otherwise.

## Follow-up review of the responses

Review of the responses at `6427f7d`, recorded at the user's request. These are review notes and proposals, not new decisions or independently verified engine findings.

**The fixed four-second clock resolves the main accounting concern.** Combat Tactics and rollover buy more actions without accelerating poison, regeneration, or cooldowns. The revised example checks out: the talent lands at clock time 1.44 s, leaving 7.44 s of its 10 s cooldown after turn one. The remaining concerns are about applying that rule consistently and about action execution.

### Describe rollover consistently with the proportional clock

"Banked time moves no clock when it is spent" is misleading. Under the proposed proportional rule, spending banked capacity does advance the clock; it simply does not add to the turn's total four seconds. State that distinction explicitly.

### Convert partial stuns through the same clock

With a 7 s bar, a 2 s stun consumes half the turn: 3.5 s of capacity, leaving 3.5 s. Subtracting 2 s directly from the bar would let bonus capacity weaken stuns. Fix the proportional denominator at the turn's starting capacity; spending or refunding capacity should not change it afterward.

### Preserve or deliberately rebalance ground-effect pulses

The document lists storms pulsing roughly every 2 s and Inferno roughly every 4 s. Giving each one pulse per 4 s turn changes their relative damage unless pulse strength or count is converted too. This is a new balance change, not just a choice of clock ownership.

"On entering, once each time" also needs a limit: can repeatedly crossing the boundary trigger repeated damage? Suggested approaches are to preserve each area's existing pulse interval through an exposure accumulator, or explicitly define a per-turn damage allowance. Entry behavior must distinguish damage areas from traps and glyphs.

### Define event ordering when charging before playback

Charging time before playback is an acceptable prototype approximation, provided timer events can prevent the ordered action from executing. For example, a character with little health orders a potion; paying its time advances a burn to a lethal tick. Under this approximation the character dies before drinking, and the command must be cancelled before execution. Incapacitation during payment needs the same consideration.

An effect created by an action must not be aged by the time just spent performing that action. Define the sequence of time payment, timer events, action validation, and execution, including what happens when a timer event interrupts that sequence.

### Pending spells need a resource commitment mechanism

The response says mana is spent when conjuring begins, but also says the command is withheld until its time is paid. If the engine does not receive the command at the beginning, it cannot perform that initial mana deduction by itself. The implementation needs either an actual casting-start phase or explicit resource accounting, with protection against charging twice at completion. This remains an implementation requirement to investigate, not an established engine capability.

### Correct the kiting refund wording

The kiting response says stepping away costs the swordsman the swing's "unspent time". Under the agreed refund rule it should cost the **already-spent time**, while the unpaid remainder is released. Check reach before paying the next installment, or the refundable remainder may be consumed before the invalid target is detected.

The review recommendation is to proceed with the fixed-clock proposal after tightening partial-stun conversion, area pulse accounting, and action event ordering. The earlier play-test scenarios still apply.

## Answers to the follow-up review

From the session that wrote "Answers to the review concerns", 2026-10-02, at the user's request. All six points are right; two correct mistakes in those answers and three name rules they left undefined. Proposals, not decisions, until the user takes them.

**1. Rollover's wording.** Corrected: spending banked time does move the clock, as any spending does under the proportional rule; what it does not do is add to the turn's 4 s. Banking makes a turn's bar longer, so each second of it moves the clock less.

**2. Partial holds go through the same ratio.** A hold is time on its victim's own clock, so it takes that share of the victim's turn: a 2 s stun is half of 4 s, so half the bar, 3.5 s of a 7 s one, not 2 s. The ratio (4 s over the bar) is fixed when the turn begins, from its whole bar, spill and bank included; nothing spent or given back afterwards changes it. A hold of 4 s or more is a lost turn, as decided, and what is over 4 s takes the next turn's share by the same rule.

**3. Pulses on their victim's own clock, by exposure.** (Withdrawn in "Answers to the review of the latest timing responses": areas pulse on their own clock, by rounds.) One pulse a turn would halve the storms (a pulse each 2 s) and leave Inferno (each 4 s) as it was: a change of balance nobody chose. Instead, as the review suggests, an area keeps its own interval and counts it on the clock of the creature standing in it, the same rule as damage over time:

- a creature that spends its turn in a storm takes two pulses, in Inferno one; one that walks out a quarter into its turn has a quarter of that turn's exposure;
- exposure accumulates across turns, so crossing the edge again and again earns no pulse it has not stood out;
- the area's own lifetime stays by rounds, as answered before;
- traps and glyphs keep the game's own trigger on entering, through their scripts; the accumulator is for areas that pulse.

This replaces "an area acts on a creature that begins its turn in it or enters it". It needs the same thing from DAOLib as damage over time on the target's clock: the area's heartbeat taken from the script timer and counted per creature.

**4. The order of an action.** (Reordered in "Answers to the review of the latest timing responses": the command starts before its time is paid.) Defined for the first version:

1. **Check the order**: its target, reach and the resources it needs.
2. **Pay its time**: the creature's clock moves by its share, and the timers of that span fire in order (a burn's tick, a cooldown lifting, an effect ending).
3. **If a timer has killed or held the creature, the action is cancelled**: the time paid stays paid, nothing else is spent, nothing goes on cooldown.
4. **Otherwise the command runs**, and what it creates (an effect, a cooldown) starts from the moment paid, so the action's own time does not age it.

The review's case, a character drinking at low health whose burn ticks lethally during the drink, comes out as real time has it: a poultice heals when the drink ends, so a tick during it can kill. The approximation is only that the tick shows before the drink is seen, not during it.

**5. A pending spell's mana: at landing, for the first version.** The review is right that a command kept back cannot have the engine take its mana at the start, and the answer before contradicted what is built: the charge rule gives the cast at the end, and `dev/DESIGN.md` has an interrupted cast cost nothing ("nothing spent and nothing on cooldown"). So: mana at landing, an interrupted spell costing only the time it spent. Mana at the start is a later refinement, and it has a trap: the mod would take the mana as casting begins and restore it just after the engine takes it again at landing. Restoring before landing fails, since regeneration in the wait can have filled the pool and the restored mana would be lost to its cap. A spell whose mana is drained below its cost meanwhile (Mana Clash) is refused by the engine at landing and counts as interrupted.

**6. The kiting refund, the right way round.** A pending attack whose target has left its reach costs the swordsman **the time already spent** on it; the unpaid remainder is released. Reach is checked at the start of the swordsman's turn, **before** the remainder is paid, so the released part is not spent first. The same check serves a target that has died.

The review's recommendation stands: the fixed clock, with these. The play-test scenarios of "Review concerns" still apply, with the area test now one of exposure: a creature walking into and out of a storm within its turn, and two victims with different bars.

## Review of the latest timing responses

Review of the responses at `7a49dad`, recorded at the user's request. These are review notes, not new decisions or independently verified engine findings. The fixed clock, proportional partial holds, corrected refunds, and mana-at-completion approximation now form a workable prototype design. Three implementation issues remain.

### Movement must account for exposure along the path

The exposure rule cannot use "pay the whole action's time, then execute" for movement. If a creature starts outside a storm and walks through it, advancing all its time before moving records no exposure. Starting inside and walking out would charge too much.

Movement needs incremental accounting as the creature travels, or equivalent accounting over the path's entry and exit points. This is the main remaining gap between the action-order rule and the exposure rule.

### Timer processing must react at each interruption

The response says timers fire in order, then checks whether the creature was killed or held. That check must happen after each event, and action execution must stop at the event that interrupts it. For example, a 3 s action encounters a stun at 1 s that expires at 2 s. Checking only the final state would miss the interruption.

The refund rule also needs an explicit choice here. The review preference is to charge the 1 s elapsed before interruption, then process the remaining turn under the stun rules. Charging all 3 s upfront would impose an additional cancellation penalty. Either can be implemented, but the choice should be intentional. The example's seconds are elapsed clock time; capacity is charged through the turn's fixed ratio.

### Revalidate after advancing timers

Checking resources and the target before payment is necessary, but timers can change them. Between initial validation and execution, mana can drain, a required form can expire, or the target can become invalid.

Suggested sequence:

1. Validate the order.
2. Advance time chronologically, reacting immediately to interruptions.
3. Revalidate the conditions needed to execute.
4. Execute once, with resource consumption and cooldown handled at the defined commitment point.

The engine may already perform much of the final validation; the mod still needs to handle rejection consistently.

### Smaller clarifications and readiness

- "Two storm pulses per turn" is approximate: the script intervals reported earlier in this document are 2.05 and 4.05 s. Preserve the actual intervals unless deliberately changing them, including the initial pulse and effects triggered after a particular heartbeat.
- Mana at completion is a reasonable first-version simplification. Leave the proposed future deduction-and-restoration scheme unresolved until the engine's resource hooks are investigated.
- Consolidate the accepted rules into one current specification before implementation. The successive answers now supersede enough earlier text that following an obsolete rule is a practical risk. Preserve the discussion as rationale, with the current specification clearly distinguished from proposals.

No broad design rethink is recommended. Movement exposure and chronological interruption handling are the last substantial gaps identified by this review before prototyping; the earlier targeted play tests still apply.

## Answers to the review of the latest timing responses

From the session that wrote the answers before, 2026-10-02, at the user's request. The review's points are right; working through them withdraws one of those answers (area pulses) and reorders another (an action's sequence). Proposals, not decisions, until the user takes them.

**Movement is paid as it goes.** The mod already measures a walk frame by frame, so a walk's time is charged the same way: the creature's clock moves as it walks, and whatever it passes through on the way is met at the moment it is there. Paying a whole cost at once is for actions that are not movement. An attack that walks to its target pays for the walk as it goes and for the swing at once on arriving.

**An action's sequence: start the command, then pay.** "Pay, then execute" cannot give back what the engine refuses after the payment: today an attack the engine ends at once (no room round its target, no path) is given back and the AI tries again, and once timers have fired on the payment that cannot be undone. So, replacing the sequence of the answers before:

1. **Check the order**: its target, reach and what it needs, before anything happens.
2. **Start the command**, and wait the few frames until the engine has taken it or refused it. A refusal costs nothing, since nothing was paid; the existing rule for an action that failed without acting stands.
3. **Once the engine has taken it, pay its time**: the creature's clock moves by its share, and the timers of that span fire one by one in the order they fall, the creature looked at after each.
4. **If one kills or holds the creature, its command is cleared before it lands**, and only the time up to the interruption is charged (the review's preference): in the review's case, a 3 s action meeting a stun at 1 s is charged 1 s, and the stun's own second is then taken by the hold rule. What is left of the action's time is released.

An action lands about a second or more after it starts (a swing's impact, a drink's end), so clearing it in time is within reach; that it is in every case is for the first play test. What the command creates still starts after the payment, so its own time does not age it. Two narrowings: during a creature's payment only its own timers move, everyone else's clock standing, so an interruption mid-payment is mostly a lethal tick of its own damage over time, or a glyph met while walking; and the engine checks mana and target again as it runs the command, so the review's "revalidate" is the engine's refusal handled at step 2 or, for a pending action given when its time is paid, the target check of the kiting answer.

**Areas pulse on their own clock, by rounds: the exposure answer is withdrawn.** The game's area scripts pulse once for everyone inside (`spell_aoe_duration.nss`'s heartbeat), and some act on a particular pulse (Blizzard's freeze on its fourth). An exposure kept per victim means the mod dealing each victim's pulses itself, rewriting every area's script, which goes against converting by rules over data and would break another mod's areas. So an area keeps its script and its intervals, 2.05 s and 4.05 s as read, its first pulse and its later-pulse effects, all counted on the area's own clock: 4 s a round, spread through the round's turns, independent of its caster. Whoever is inside when a pulse falls is struck, acting or not. The cost: a creature walking through a storm in one short turn may meet no pulse, where in real time it would meet one or two. Burns and poisons stay on their victim's own clock, since the game already ticks those per creature (`effect_dot2_h.nss`, one chain a creature). The area test of the scenarios becomes: a creature standing in a storm through a round of eight, against one walking through it in its turn.

**Mana at landing**: agreed; the deduction and restoring at the start stays open until DAOLib has looked at the engine's resources.

**One current specification: agreed, and now the main risk.** Five rounds of answers have each overridden earlier text, and building from this page risks following a rule since replaced. Next is a "Current specification" at the head of this page, the rules in force only, with the rounds below it as their reasons.

Implementation has started with a pure-core time-accounting component; see [REBALANCE_IMPLEMENTATION.md](REBALANCE_IMPLEMENTATION.md) for what is built, verification, and the engine integration gaps. The live turn machine still uses the existing rules until those gaps are resolved.
