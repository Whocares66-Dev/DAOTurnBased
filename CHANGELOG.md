# Changelog

## 0.3.0

Grabs that can be broken, with [DAOLib 0.5.0](https://github.com/Whocares66-Dev/DAOLib/releases/tag/v0.5.0) or later. Settings and saves are as in 0.2.0.

- **Grabs and Overwhelms across turns:** an ogre's grab, a high dragon's or the Broodmother's, and the Overwhelm of bears, spiders, drakes, deep stalkers and werewolves used to play out whole in their attacker's turn, every blow landing before anyone could answer. Now they run on the attacker's turn like any action. If one lasts longer, attacker and victim freeze where they are until the attacker's next turn, and then it goes on. The victim's turns in between are skipped. Anyone acting meanwhile can break it as in the real-time game: a stun, knockdown, paralysis or slip on the attacker ends it at once, and so does either one dying.
- **The portrait bar on very large screens:** on a 5120x2160 monitor the bar sometimes vanished. It is now laid out from the game's own screen size.

To install: extract DAOLib's zip into the game's folder, then this one. Over 0.2.x, both zips.

## 0.2.0

A character sheet on the portrait bar, with [DAOLib 0.2.0](https://github.com/Whocares66-Dev/DAOLib/releases/tag/v0.2.0) or later. Rules, settings and saves are as in 0.1.1.

- **Character sheet:** hovering over a portrait on the portrait bar shows that combatant's sheet in the bottom right corner of the screen, in the box the game uses for a party member's details: level, class and specializations, health and stamina or mana, attributes, damage, critical chance and armor penetration for each hand, attack, armor, defense and resistances. It works for enemies and allies as well as the party. Every number and word is the game's own, read as its character record reads them, so other mods' changes show. The [guide](https://whocares66-dev.github.io/DAOTurnBased/portrait-bar/#character-sheet) has a picture.
- **DAOLib 0.2.0:** its loader is now `dinput8.dll`, where 0.1.x's was `version.dll`. DAOLib 0.2.0 also brings faster loading screens, off until ticked under Options, Game, Performance.

To install: extract DAOLib's zip into the game's folder, then this one. Over 0.1.x, first delete `bin_ship\version.dll` and `bin_ship\version.pdb`, the old loader, then extract both zips: with both loaders in the game, the mod is loaded twice and the game crashes.

## 0.1.1

Fixes to the portrait bar, with [DAOLib 0.1.0](https://github.com/Whocares66-Dev/DAOLib/releases/tag/v0.1.0) or later. Rules, settings and saves are as in 0.1.0.

- **The portrait bar at every resolution:** at 16:9 (1920x1080 and the like) the bar lay over the game's buttons along the top of the screen and was a fifth too large. It now keeps one size and one place at every resolution, as the game's own interface does, and stays clear of the minimap on a narrow screen.
- **Stamina and mana as the party portraits show them:** a card's arc now shows what sustained abilities hold back, with the game's own mark, and both arcs fill the way the party portraits' do. A character with a sustained ability on showed full stamina on its card when it was not.
- **End and Delay:** evenly spaced beside the portraits, and the bar's lettering is the paler colour of the place name under the minimap.
- **No dropped frame when a path appears:** the rings of the path to the cursor are kept and moved where they were placed anew each time a path came back.

To install: extract DAOLib's zip into the game's folder, then this one. Over 0.1.0, this zip alone.

## 0.1.0

The first release: turn-based combat for Dragon Age: Origins, Awakening and Witch Hunt, version 1.05.13263.0, with [DAOLib 0.1.0](https://github.com/Whocares66-Dev/DAOLib/releases/tag/v0.1.0) or later. The [guide](https://whocares66-dev.github.io/DAOTurnBased/) has how to install it and how it plays.

- **Fights in turns:** when a fight starts everyone rolls initiative and acts one at a time. A turn is 4 seconds to spend on attacks, talents, spells and movement, each at the time the game takes for it, and what is left rolls over. Talents, spells, enemies and difficulty are the game's own, unchanged. Cooldowns, effects and conditions count in turns.
- **A portrait bar:** everyone in the fight in the order they act, with health and stamina or mana, made of the game's own interface. Under it the turn's time, and what an attack or a move will cost before you order it: the path to the cursor is drawn on the ground, gold in reach and red beyond.
- **Your party, or its tactics:** play every companion's turn yourself, or tick "Use when turn-based" on a companion's Tactics page and its tactics play its turns. The choice is kept with each save.
- **In the game's own options:** a box to turn it on or off and a fight speed slider on the Game tab, and End turn (Enter), Delay and the on/off switch among the Controls. `turnbased.ini` beside the logs has the same seven settings.
- **Played through:** the main campaign, Awakening and Witch Hunt, on the GOG version. The Steam and EA versions need a large-address-aware `DAOrigins.exe`: as installed theirs can use 2 GB of memory, where GOG's has 4, and the game runs out of it with or without the mod.

To install: extract DAOLib's zip into the game's folder, then this one. Saves are not modified, and deleting `bin_ship\daolib\plugins\turnbased` removes the mod.
