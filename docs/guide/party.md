---
title: Party control
permalink: /party/
nav_order: 4
---

# Party control

## Manual control

By default you play every party member's turn. On a companion's turn, the companion is selected and the camera moves to it. The fight waits until you end the turn; there is no time limit.

## Tactics control

A companion can play its own turns using its tactics. Open the companion's **Tactics** page and tick **Use when turn-based**.

![The Tactics page: Use when turn-based]({{ "/assets/img/tactics.jpg" | relative_url }}){: .screenshot loading="lazy"}

- The setting is per companion and is saved with the game.
- It can be changed during a fight, and applies from the companion's next turn.
- The hero is always under manual control.

To put all companions on tactics at once, use the `party` setting in the [settings file]({{ '/options/' | relative_url }}#settings-file).
