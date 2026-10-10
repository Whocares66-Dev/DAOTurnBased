---
title: Options
permalink: /options/
nav_order: 5
---

# Options

## Game options

In **Options**, on the **Game** tab, under **Turn-based**:

![Options, Game tab]({{ "/assets/img/options.jpg" | relative_url }}){: .screenshot loading="lazy"}

- **Turn-based combat**: turns the mod on or off. It can only be changed outside a fight.
- **Fight speed**: how fast turn-based fights play: 0.5x, 1x, 2x or 3x. It changes animation speed only, not the rules.

## Key bindings

In **Options**, on the **Controls** tab, at the bottom of the list under **Turn-based**:

![Options, Controls tab]({{ "/assets/img/controls.jpg" | relative_url }}){: .screenshot loading="lazy"}

| Action | Default key |
|---|---|
| End turn | <kbd>Enter</kbd> |
| Delay turn | none |
| Toggle turn-based combat | none |

## Settings file

`Documents\BioWare\Dragon Age\daolib\turnbased.ini` is created the first time the game runs with the mod. Edit it with the game closed.

| Section | Setting | Default | Description |
|---|---|---|---|
| `[turnbased]` | `enabled` | `true` | Turn-based combat on or off. Same as the box in the game's options |
| `[turns]` | `party` | `manual` | `manual`: you play every party member. `tactics`: companions play by their tactics unless set otherwise on their Tactics page |
| `[turns]` | `speed` | `1` | Fight speed, 0.25 to 4. Same as the slider in the game's options |
| `[feedback]` | `camera_follow` | `true` | Moves the camera to the combatant whose turn it is |
| `[keys]` | `end_turn` | `Enter` | Key for End turn, until one is bound in the game's controls |
| `[keys]` | `delay` | `None` | Key for Delay turn, until one is bound in the game's controls |
| `[keys]` | `toggle` | `None` | Key for turning turn-based combat on or off, until one is bound in the game's controls |

An unknown setting or an invalid value is reported in `turnbased.log`, and the default is used.
