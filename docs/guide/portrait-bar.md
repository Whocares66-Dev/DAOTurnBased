---
title: Portrait bar
permalink: /portrait-bar/
nav_order: 3
---

# Portrait bar

The portrait bar at the top of the screen shows everyone in the fight in the order they act.

![The portrait bar]({{ "/assets/img/portrait-bar.jpg" | relative_url }}){: .screenshot loading="lazy"}

## Portraits

- **Order**: left to right. The first portrait, larger, is the current turn.
- **Glow**: gold for the party, red for enemies, blue for allies outside the party.
- **Ring**: health on the left in red, stamina or mana on the right.
- **Number between two portraits**: the start of the next round.
- **Icon at the corner**: a pending action that completes next turn, or movement that ran out of time.
- **Greyed portrait**: a fallen party member. Dead enemies are removed.

Hover over a portrait to see the name, colored by rank: white for normal, yellow for lieutenant, orange for boss. Click a portrait to move the camera to that combatant.

The bar shows up to ten portraits. With more, arrows at its ends scroll it.

## Time bar

On a party member's turn, the bar under the portraits shows the turn's time, one segment per second. The text below it shows the time left and the distance that time covers.

![The time bar: 3 seconds left, 13.8 m of movement]({{ "/assets/img/time-bar.jpg" | relative_url }}){: .screenshot loading="lazy"}

[Rollover]({{ '/turns/' | relative_url }}#rollover) and [Combat Tactics]({{ '/turns/' | relative_url }}#combat-tactics) make the bar longer.

## Cost estimation

Hover over an enemy to see what attacking it would cost. The cost is highlighted on the time bar, and the text reads, for example, **Costs 2.9 s, leaving 0.1 s**. If the enemy is out of reach, the cost includes the movement to it.

![Hovering over an enemy: the walk to it and the attack cost 2.9 seconds]({{ "/assets/img/cost-preview.jpg" | relative_url }}){: .screenshot loading="lazy"}

If the cost is more than the time left, the text says the action will complete next turn. See [Actions that do not fit]({{ '/turns/' | relative_url }}#actions-that-do-not-fit).

## Movement

Hover over the ground to see the path there, drawn as rings, with its length and cost.

![The path to the cursor: 4.6 m, 0.8 seconds]({{ "/assets/img/move-preview.jpg" | relative_url }}){: .screenshot loading="lazy"}

- **Gold rings**: within reach this turn.
- **Red rings**: out of reach. An order to move there is refused and costs nothing.

![A path longer than the movement left: 12.7 m of 6.6]({{ "/assets/img/move-preview-red.jpg" | relative_url }}){: .screenshot loading="lazy"}

## End and Delay

The buttons at the right of the bar:

- **End** ends the turn. Same as <kbd>Enter</kbd>.
- **Delay** moves the turn to the end of the round. It is available until the character moves or acts.

## Refused orders

An order that cannot be carried out is refused, with the reason shown over the character. A refused order costs nothing.

| Message | Meaning |
|---|---|
| Not Morrigan's turn | The order was for a character whose turn it is not |
| Too far | The destination or target is beyond this turn's movement |
| No movement left | No time is left to move |
| No way through | The path to the target is blocked |
| No room at the target | No free space next to the target to attack from |
| A door is in the way | A closed door blocks the path; open it first |
| No action left | The turn's time is spent |
