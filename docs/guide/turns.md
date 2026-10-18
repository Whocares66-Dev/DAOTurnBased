---
title: Turns
permalink: /turns/
nav_order: 2
---

# Turns

A turn is 4 seconds. Every action costs the time it takes.

## Turn order

When a fight starts, everyone rolls initiative:

- a d20,
- plus 1 for every 4 points of Dexterity above 10,
- plus 2 for each rank of the Survival skill.

The highest roll acts first. The order stays the same for the whole fight; anyone who joins later is inserted into it.

A round is one turn for everyone.

## Turn time

Each turn has 4 seconds to spend, in any order, on:

- **Actions**: an attack, a talent, a spell or an item. Each costs the time it takes to perform.
- **Movement**: a person moves 4.66 m per second, so a full turn of movement is about 18.6 m. The first meter of each turn is free.

Switching weapon sets and toggling a sustained ability are free. Opening a door or a chest costs 0.5 seconds.

| Attack with | Cost |
|---|---|
| Dagger | 1.5 s |
| Longsword | 1.9 s |
| Mace | 2.0 s |
| Greatsword | 2.5 s |
| Staff | 1.6 s |
| Shortbow | 1.8 s |
| Longbow | 1.9 s |
| Crossbow | 2.7 s |

Anything that changes attack speed changes the cost. Haste and Momentum lower a melee attack's cost. For a bow, Rapid Shot lowers the cost, Aim raises it by 1.5 seconds, and heavy or massive armor raises it by 1.2 seconds.

A spell costs its casting time.

## Actions that do not fit

An action that costs more than the time left starts now and completes at the start of the character's next turn. The rest of its cost is taken from that turn.

Example: a greatsword attack (2.5 s) started with 1 second left completes next turn, which then has 1.5 seconds less.

While an action is pending, the character cannot move. A stun or knockdown cancels it. A pending action is marked by an icon on the character's [portrait]({{ '/portrait-bar/' | relative_url }}#portraits).

## Rollover

If you end a turn with time left, up to 1 second is added to that character's next turn. [Combat Tactics](#combat-tactics) raises this limit to 1.5 and then 2 seconds.

Time rolls over only from a turn you ended yourself, not from a turn lost to a stun or skipped.

## Combat Tactics

Each rank of the Combat Tactics skill adds time:

| Rank | Effect |
|---|---|
| 1 | Rollover limit 1.5 s |
| 2 | Turn length 4.5 s |
| 3 | Rollover limit 2 s |
| 4 | Turn length 5 s |

Higher-ranked enemies have longer turns: lieutenants 4.5 s, bosses 5 s, elite bosses 5.5 s.

## Cooldowns and effects

Cooldowns and effects keep their normal lengths. Each combatant's cooldowns and effects advance by 4 seconds on each of that combatant's turns.

- A 20-second cooldown is ready after five of its user's turns.
- A 20-second effect lasts five of its target's turns. Damage over time is applied on the target's turns.
- A stun or knockdown costs the target its duration. One that lasts 4 seconds or more costs a whole turn.
- A grab or Overwhelm (an ogre, a bear, a spider, a high dragon) runs on its attacker's turn. If it lasts longer, attacker and victim freeze until the attacker's next turn, and then it goes on. The victim's turns in between are skipped. A stun, knockdown, paralysis or slip on the attacker breaks it, and so does either one dying.
- Health, mana and stamina regenerate each turn at their normal rates.

No timers run while a turn is waiting for your order.

## Unaware enemies

An enemy in the fight that has not noticed the party is marked **Unaware** and skips its turn until it does.

## End and Delay

- **End** (<kbd>Enter</kbd>) ends the turn. If an action is still playing, it is stopped.
- **Delay** moves the character's turn to the end of the round. It can be used once per round, before the character has moved or acted.

## End of a fight

A fight ends when no enemy is left standing, or when the party escapes and neither side can see the other.
