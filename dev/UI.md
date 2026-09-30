# The turn-based UI: made of the game's own

The turn order bar, the movement preview and the on/off switch should look like Dragon Age, not like an overlay. So they are built from the game's own UI: its Scaleform movies, its art, and its live portraits. No ImGui, no overlay drawn over the frame. This page is what exists in the game to build them from, the data the mod already has for them, and what DAOLib must reach first (`dev/DAOLIB_NEEDS.md`, "The UI").

Until then the mod speaks through the game's floating text (`DisplayFloatyMessage`): "Alistair's turn", "Out of movement", "Not Morrigan's turn".

## What the game has

Read 2026-09-30 from the install's `packages\core\data\` (the ERF archives as `tools\gda.py` in DAOLib reads them).

- **The UI is Scaleform GFx**, Flash movies: 67 `.gfx` in `guiexport.erf`. Among them `portraits.gfx` (the party bar), `partypicker.gfx`, `quickbar.gfx`, `tactics.gfx`, `optionsmenu.gfx`, `floatylayer.gfx` (floating text), `tooltips.gfx`. They are compressed SWF version 8 (`CFX`): ActionScript 2, editable with JPEXS Free Flash Decompiler.
- **The art** is in texture atlases, `atl_shared_dxt1_00.dds` to `atl_shared_dxt5_02.dds`, which the movies share, and swatches in `gui.erf` (`red`, `green`, `blue`, `yellow` at three strengths, `white`, `transparent`). The fonts are `gui.erf`'s `nogard.ttf` and `nogard_text.ttf`, and each language's `fonts_*.gfx`.
- **Portraits are rendered live**, not stored. The data table `portraits` (DAOLib's `dev/gda_tables.csv`) has per creature tag a camera pitch, rotation, distance and offsets, a light and a tint, with a `Default` row for everyone else; the party bar asks for them through `GetPortraitByCharacterID` in `portraits.gfx`. So any creature can have a portrait (enemies too, as in BG3), framed by the camera the game would use.
- **Rank frames.** The table `creatureranks` has a `PortraitFrame` per rank and a colour: yellow for a lieutenant, orange for a boss or elite boss (`0xFFFF00`, `0xFF6600`). The bar can mark bosses the way the game's own target display does.
- **How movies talk to the engine**: `FSCommand:`s, the names in the movies' strings (`GetObjectNameByID`, `Get2DAValue`, `GetM2DAValue`, `GetTlkString`, `SendMessage`, `LoadMovie`, `SetValue`, `PlayGUISound`, `SetTimeScale`, ...), and a data tree the engine publishes and the movies bind to (`GUI.SelectedChar.Attributes.7.Current`, the selected character's health; `CORESTATS_HEALTHCURRENT`).
- **A ring that follows the cursor** already exists: area-of-effect spells show one while being aimed (explore mode's `ActivateAnalogTargetModeMessage`).

## The turn order bar

![The turn bar in the game, the first fight at Castle Cousland (2026-09-30, 10:04)](images/turnorder-ingame.jpg)

In the game, above: the Dog's turn, the Howe Archer and Howe Soldier with portraits the engine rendered for them, the party's from the game, round 2 and 3 after the dividers. It shows over the fight in the game's fonts, placed top centre, with the party bar and minimap still in view.

![The turn bar's sample fight, as Ruffle draws it (2026-09-30)](images/turnorder-sample.png)

The prototype above, drawing its sample fight in a Flash player: round 2, the Warden's turn (larger, gold frame, the ▲ of a character the player commands), a Hurlock Emissary with a boss's orange inner border, a Genlock Alpha with a lieutenant's yellow, Morrigan fallen and greyed, a soldier fighting beside the party in green, the divider into round 3, and End turn. In the game every card should carry its combatant's portrait (below); outside it they carry initials.

- Top centre, as BG3's. The current turn first, larger and lit; then the rest of this round; a divider with the next round's number; the next round. Twelve turns in all (`TurnView::upcoming`).
- A frame per side: blue for the party, red for enemies, green for allies. It takes the rank's frame and colour from `creatureranks` for lieutenants and bosses. Health is a thin bar under each portrait.
- A fallen party member stays, greyed; fallen enemies go.
- The party members the player commands have a ▲ under them, as BG3 marks them.
- Hovering a portrait shows its name, health, and mana or stamina (the game's tooltip movie); clicking centres the camera on it.
- An end-turn button (BG3's hourglass) at the right end, the same as the pause key.
- Under the current portrait on the player's turn: the action left (a pip) and the movement left (a bar, meters).

All of that is `tb::TurnView` (`src/core/TurnMachine.h`): per card the id, name, tag (for the portrait's camera), side, rank, health, fallen, current, whether the player commands it, and round; and the current turn's actions and movement left. `TurnMachine::View()` makes one on the scripts' thread. The portraits come from the game's side (`tb::PortraitBook`, below).

### The movie, built 2026-09-30

`turnorder.swf`, from `ui/turnorder/frame_1/DoAction.as` (ActionScript 2), by `python tools\movie.py`. No Flash editor is involved:

- The tool writes a SWF 8 shell, the game's own version. It holds a 1280 × 720 stage, the game's two fonts imported by name from its font library (`$TitleFont` and `$BodyFont` from `gfxfontlib.swf`, byte for byte as `portraits.gfx` imports them; `tools/test_movie.py` checks), and an empty frame script.
- JPEXS FFDec compiles the ActionScript into that script from the command line, then decompiles the result to prove the script is there. FFDec 26.3.0 and Ruffle were installed in `C:\Modding` for this, 2026-09-30.
- The movie draws everything with ActionScript's drawing API (cards, frames, health bars, the plate behind them), so it has no symbols to draw by hand.
- A plain SWF is what the game looks for first, and GFx 3 loads one; `tools/deploy.ps1` puts it in the override folder.

It asks `TB_GetView` three times a second. It builds the bar again only when the cards change; otherwise it sets the status line in place. A walk changes the meters left several times a second, and a rebuild would load every portrait again. The build fails unless every function and every `TB_` call of the source comes back out of the compiled movie. Outside the game (no `ExternalInterface`, or the parameter `sample=1`) it draws a sample fight, and `.\tools\snapshot.ps1` shows that in Ruffle, an open-source Flash player, and saves a PNG: the picture above. The preview uses a system font; the game's fonts need the game's font library. A stand-in, empty `gfxfontlib.swf` is written beside the build for the preview, since a player holds the first frame until an import resolves.

**Portraits.** The party's come from the game, the way its party bar gets them. Reading `portraits.gfx`'s scripts (FFDec, 2026-09-30) showed how. The party bar takes its members from the data tree, `Party.Members.0` to `3` and `Party.Summons.0` to `3`, each with an `ID` and a `Portrait`, an image's name, through the game's `ExternalInterface` calls `GetValue` and `AddTokenObserver`. It loads the picture with `loadMovie("img://<name>?Width=..&Height=..")`, which the game's image loader draws. The turn bar does the same for a party card, matching the member by id: no DAOLib work.

Enemies have no such entry. The game's creatures are a closed set, so they can have portraits too, three ways:

1. **Rendered live by the engine**, as the party's are: the portraits table's `Default` camera frames any creature. **Built**, on DAOLib `ce27567`. `CCCreature::RenderPortrait()` calls the engine's own `CCCreature::UpdatePortrait`, what the party bar, the paperdoll and character creation call. That makes the creature a portrait renderer the first time (a clone of its model, a 146 × 146 target, the portrait light rig) and returns the texture's `img://portrait_<n>` name. `PortraitImage()` gives the name without rendering. It is the least work, looks like the party's, and covers every creature, mods and DLC included.
2. **Rendered once in the game and shipped as images.** A capture pass spawns each creature template, renders it through the same renderer, and saves it with `D3DXSaveTextureToFileW`, which the game imports already. The same DAOLib work, plus the capture. Only worth it if live rendering is slow or fails for some.
3. **Rendered offline** from the game's models in Blender. The most work (DAO's mesh and morph formats, matching the portrait lighting), and the least like the game's own. A last resort.

How the plugin uses the first (`src/core/Portraits.h`, `src/game/Bar.cpp`):

- **One portrait with each view it publishes** (ten a second), in the order of the cards, so the current turn's and the next come first.
- **The engine's existing portrait first.** A creature that already has one, as the party does, gives it by `PortraitImage()`. Anyone else is rendered.
- **Each combatant once a fight.** An empty answer is remembered too, so a creature the engine makes no portrait for is not asked again.
- **Nothing rendered while the fight stands aside** (a conversation, a menu). Everything is forgotten when the fight ends, since object ids are reused once an area is left.
- **The URL travels in the card's eleventh field** (below). The movie loads it as it loads the party's, and falls back to the party tokens, then initials.
- **A way out.** `[feedback] portraits = false` keeps the party's portraits and gives everyone else initials, if rendering proves slow or unsafe.

Each portrait is logged at info, `Howe Soldier's portrait: img://portrait_bhn200cr_howe_landra43 (rendered)`. In play (2026-09-30, 09:54) every combatant got one, rendered or the game's, and the bar showed over the fight in the game's fonts. The cards' pictures came out grey: the game's movies add `.dds` to an image's name before loading it (`MovieUtils.GetImageUrl`), and ours did not. The plugin now sends the texture's name with it (`TextureName`), and the movie builds its URLs as the game's helper does. Not seen in play: whether a render into a paused world draws, and what it costs (`dev/DAOLIB_NEEDS.md`). The renderer is freed with its creature, by the creature's destructor, so rendering every enemy leaks nothing.

Not seen in the game yet: the fonts, `GetValue` and `img://` from a movie of ours, the rendered portraits, the screen itself.

What the movie needs from the plugin:

1. Loading, placing and showing (top centre, over the game, under menus), hidden outside fights: built (`src/game/Bar.cpp`, `KeepScreen`), through DAOLib's screens (below).
2. The cards, fresh enough to follow the fight: built (below).
3. A portrait per card: built, the party's from the game's data tree or the engine, everyone else's rendered by the engine (above).
4. Its clicks heard: built (below).

### The cards: the party bar's medallions (2026-09-30)

The user asked for the game's own portrait backgrounds and skeuomorphic frames in place of drawn boxes. The research (`build/research/portrait_art/README.md`, not in the repository) found every picture in the game's movies is an atlas texture named by a `DefineExternalImage`, which `img://<name>.dds` draws by name (the image loader looks names up in the UI atlases first, 0x516A20). So each card is the party bar's own stack (`portraits.gfx`, `Portrait1_mc`), scaled from 114 px to 60:

- the backplate, `Portraits_IC2`;
- a painted background orb, one of the twenty frames of the party bar's `Portrait2_back` (`levelUp_I158` and every third texture after, patch003's names). A party member's is its `PortraitProperties.PortraitTint` from the data tree (the portraits table's, by tag: Alistair 17, the dog 18, Morrigan 16); an enemy's or ally's is one of the twenty, fixed by its id;
- the portrait, masked round;
- the red health arc, `Portraits_I41`, filled from the foot to the share of health left;
- the gold ring, `Portraits_I3C`;
- for the current turn, the party bar's glow ring, `Portraits_I60`, behind, and the card larger;
- a rim of the side's colour outside the gold for enemies and allies, and a gem of the rank's colour for lieutenants and bosses.

End turn stands on the game's popups' gold-edged panel, `PopupLayer_I70`. Seen in play at 12:07 (`dev/images/turnorder-ingame-medallions.jpg`): every texture drew from the atlases by name, the party's orbs matching the game's own party bar. Outside the game the movie loads the same art from the research's PNGs, so `tools/snapshot.ps1` shows the real look (the portraits in the sample are two of the game's summon portraits).

### The protocol, built 2026-09-30

The movie pulls; the plugin never calls into it. A movie's calls come on its own thread, which may not be the scripts', and pulling makes that not matter: the handlers only read what the frame callback last published, and leave requests for it (`src/game/Bar.h`). They are installed with `[feedback] turn_bar = true`, off until there is a movie.

| From the movie | The plugin |
|---|---|
| `ExternalInterface.call("TB_GetView")` | Answers one string: the view, as `src/core/Bar.h` encodes it, at most a tenth of a second old |
| `fscommand("TB_EndTurn", "")` | Ends the player's turn, as the pause key does |
| `fscommand("TB_Focus", "<id>")` | Centres the camera on that combatant |

The string is made for ActionScript 2, which has no JSON: `String.split(String.fromCharCode(30))` gives the head then a record per card, and each splits on `String.fromCharCode(31)` into fields. No escaping: a name loses any control character it had. The head is the format's version, active, round, the current combatant's id, the player's turn, actions left, meters left, meters at most. A card is id, side (0 party, 1 ally, 2 enemy), rank, health 0 to 100, fallen, current, the player's, round, name, tag, and portrait (an `img://` URL, empty for none yet). Flags are 0 or 1, meters have one decimal. A movie that meets a version it does not know stays blank. Version 2 added the portrait. Polling three times a second is plenty: the view changes at most once a frame, and an `onEnterFrame` that asks every tenth frame is cheap.

This follows the game's own movies (DAOLib's `dao/Gui.h`): a question is an `ExternalInterface.call`, answered directly with a value; a click is an `fscommand`, fire and forget. The game's fscommand handlers answer, when they answer, by setting `_root.fsc_retval` in the movie (ints, through `SetVariableArray`), which a caller in ActionScript reads after the call; ours need no answer. Pushing instead (the plugin calling `SetVariable` or `Invoke` on the movie, which DAOLib now has, `8c6b9d7`) would save the polling, once which thread a movie may be called on is known.

### Opening the movie

What is left is DAOLib's item 1: opening a movie of our own. DAOLib has read how the game does it (`dev/GUI.md` there): `ScaleformManager::OpenMovie(scene, name, layer, flags)` finds or makes the movie's definition (`.swf` becomes `.gfx`, loaded through `GFxLoader::CreateMovie`), a view of it and a record, and shows it.

Every screen of the game is a `GUI::GUIScene(name, filename, layer, ..., flags)`, and `OpenMovie`'s first argument is the scene's id. The layer is the z-order: 0 floating text, 1 the HUD (`GUIMain`), 2-3 in-game menus, 4 item examine, 5 conversation, death and the main menu, 6 options, 7 popups, 8 loading. Flag 0x20 is modal (options, popups, the main menu) and 0x10 a loading screen. The game shows a screen by name with a `PushGUIScreenMessage`, which reaches a registered scene.

So the turn bar is a layer 1 scene, beside the HUD, not modal. DAOLib `da1c603` registers one as the GUI system registers its own 61: `dao::gui::AddScreen(L"TurnOrder", L"turnorder", dao::gui::kLayerHud)`, false until the GUI system exists (from the main menu on), so from a frame callback; then `ShowScreen`, `HideScreen` (kept loaded) and `CloseScreen` by name, the GUI system's own slots. Read, not yet seen in play; DAOLib's example plugin tries a round trip on a missing movie, and the probe logs the `OpenMovie` it makes.

**Where the file lives.** `GetMovieDef` puts `.swf` on a bare name, and the game's file opener (`EclipseGFxFileOpener`) looks the name up in the engine's resource system, falling back to `.gfx` (DAOLib `33636ba`). So the screen "turnorder" finds `turnorder.swf` wherever the game finds its own resources. `tools/deploy.ps1` puts it in the per-user override folder, `Documents\BioWare\Dragon Age\packages\core\override\`, and the plugin asks `dao::gui::HasMovie` once the screen is added and says in its log whether the movie was found.

Scene field `+0xA0` is 10 for full-screen menus and 0 for the HUD; `AddScreen` passes 0.

## The movement preview

On a party member's turn, while the cursor is over the ground:

- a ring around the character, its radius the movement left;
- the path to the cursor, gold while it is within that, red past it (the part beyond the budget, or the whole of it);
- the same for an attack: gold if the target can be reached this turn, red if not, before the player clicks.

It needs the cursor's point in the world (explore mode keeps it: the move-to-location order reads it from there, `MoveCharacterToLocationMessage` carrying nothing), the engine's pathfinder for the path's length and shape, and a way to draw in the world. The aiming ring's decal is the natural one, tinted gold or red; failing that, a line or a string of small effects along the path.

## The switch

The game's Options screen (`optionsmenu.gfx`) with a "Turn-based combat" toggle beside auto-pause, disabled while in combat. It needs the movie extended (an override of `optionsmenu.gfx`, or a page added through it) and the value read back. Until then, `Ctrl+T` and `turnbased.ini`.
