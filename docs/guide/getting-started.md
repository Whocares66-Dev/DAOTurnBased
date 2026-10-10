---
title: Getting started
permalink: /getting-started/
nav_order: 1
---

# Getting started

## Requirements

Dragon Age: Origins on PC, version 1.05. The mod is developed and tested on the GOG version.

The Steam and EA versions need a large-address-aware (LAA) `DAOrigins.exe`. As installed, theirs can use 2 GB of memory, and the game crashes when it runs out. GOG's is large-address-aware already.

## Installation

The mod needs [DAOLib](https://github.com/Whocares66-Dev/DAOLib), which loads it into the game.

1. Close the game.
2. Download `DAOLib-x.y.z.zip` from [DAOLib's releases](https://github.com/Whocares66-Dev/DAOLib/releases) and extract it into the game's folder, the one that contains `bin_ship`. For GOG this is usually `C:\Program Files (x86)\GOG Galaxy\Games\Dragon Age Origins`.
3. Download `DAOTurnBased-x.y.z.zip` from the [releases page](https://github.com/Whocares66-Dev/DAOTurnBased/releases) and extract it into the same folder.

`bin_ship` then has `version.dll` next to `DAOrigins.exe`, and `daolib\plugins\turnbased\turnbased.dll`.

Saves are not modified, and nothing is added to the game's override folder.

## First fight

1. Walk into a fight. Everyone rolls initiative, and the [portrait bar]({{ '/portrait-bar/' | relative_url }}) appears at the top of the screen.
2. On a party member's turn, the fight waits for you. Attack, use an ability or move as usual. Each costs time from the [time bar]({{ '/portrait-bar/' | relative_url }}#time-bar).
3. Press <kbd>Enter</kbd> or click **End** to end the turn. Unused time [rolls over]({{ '/turns/' | relative_url }}#rollover).

![A party member's turn]({{ "/assets/img/your-turn.jpg" | relative_url }}){: .screenshot loading="lazy"}

The keys can be changed under [Key bindings]({{ '/options/' | relative_url }}#key-bindings).

## Turning it on and off

In **Options**, on the **Game** tab, tick or untick **Turn-based combat**. It can only be changed outside a fight.

## Uninstalling

Close the game and delete `bin_ship\daolib\plugins\turnbased`. To remove DAOLib too, delete `bin_ship\version.dll`, `bin_ship\version.pdb` and `bin_ship\daolib`. Saves load without the mod.

## Reporting problems

[Open an issue on GitHub](https://github.com/Whocares66-Dev/DAOTurnBased/issues). Describe what happened and where in the game, and attach these files from `Documents\BioWare\Dragon Age\daolib`:

- `turnbased.log`: the last session.
- `turnbased.previous.log`: the session before, if you have restarted the game since.
- Any `crash-...log` or `hang-...log`, if the game crashed or froze.
