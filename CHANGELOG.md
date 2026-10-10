# Changelog

## 0.1.0

The first release: turn-based combat for Dragon Age: Origins, Awakening and Witch Hunt, version 1.05.13263.0, with [DAOLib 0.1.0](https://github.com/Whocares66-Dev/DAOLib/releases/tag/v0.1.0) or later. The [guide](https://whocares66-dev.github.io/DAOTurnBased/) has how to install it and how it plays.

- **Fights in turns:** when a fight starts everyone rolls initiative and acts one at a time. A turn is 4 seconds to spend on attacks, talents, spells and movement, each at the time the game takes for it, and what is left rolls over. Talents, spells, enemies and difficulty are the game's own, unchanged. Cooldowns, effects and conditions count in turns.
- **A portrait bar:** everyone in the fight in the order they act, with health and stamina or mana, made of the game's own interface. Under it the turn's time, and what an attack or a move will cost before you order it: the path to the cursor is drawn on the ground, gold in reach and red beyond.
- **Your party, or its tactics:** play every companion's turn yourself, or tick "Use when turn-based" on a companion's Tactics page and its tactics play its turns. The choice is kept with each save.
- **In the game's own options:** a box to turn it on or off and a fight speed slider on the Game tab, and End turn (Enter), Delay and the on/off switch among the Controls. `turnbased.ini` beside the logs has the same seven settings.
- **Played through:** the main campaign, Awakening and Witch Hunt, on the GOG version. The Steam and EA versions need a large-address-aware `DAOrigins.exe`: as installed theirs can use 2 GB of memory, where GOG's has 4, and the game runs out of it with or without the mod.

To install: extract DAOLib's zip into the game's folder, then this one. Saves are not modified, and deleting `bin_ship\daolib\plugins\turnbased` removes the mod.
