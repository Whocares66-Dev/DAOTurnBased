// The turn order bar (dev/UI.md, "The turn order bar"), ActionScript 2 for
// the game's Scaleform GFx 3. Built into turnorder.swf by tools/movie.py.
//
// Three times a second it asks the plugin for the view (TB_GetView, one
// string: src/core/Bar.h) and, when it changed, draws it: a card per turn to
// come along the top of the screen, the current one first and larger, a
// divider before the next round, and under them whose turn it is and what
// the player has left. A card clicked centres the camera on its combatant
// (TB_Focus); the button at the end ends the player's turn (TB_EndTurn).
// When the fight is over it drops the cards, their portraits with them, and
// says so (TB_Cleared): the plugin releases the portraits it rendered only
// then, as the game's screens let go of theirs before releasing them.
//
// Outside the game there is no plugin to ask, and it draws a sample fight,
// so the look can be checked in any Flash player.

var BAR_VERSION = 8;
var CARD = String.fromCharCode(30);
var FIELD = String.fromCharCode(31);

// A card is a medallion, the party bar's own (portraits.gfx, Portrait1_mc,
// drawn at 114 px; here at CARD_W): its backplate, a painted background, the
// portrait, the red health arc and the gold ring, all the game's textures
// by name (build/research/portrait_art/README.md, 2026-09-30).
var CARD_W = 60;
var CARD_H = 60;
// 5 until 2026-10-03: with twelve in the order (Redcliffe's square) Delay
// and End lay under the minimap, and the user asked for the portraits "just
// a little bit" closer, "not too much though or else it'll look too cramped"
// (issue 14). What the gap alone cannot do the strip does by giving way to
// the left (build).
var GAP = 3;
// What the minimap takes of the stage's right end, with a little air: its
// frame's left edge measured at 1405 of 1600 screen pixels (1124 of the
// stage's 1280) in that fight's picture.
var MINIMAP = 164;
// Not into the party's own portraits down the left side either.
var PARTY_BAR = 96;
var ART = CARD_W / 114;
var END_W = 78; // End, on the inventory's leather button (190 x 32 at full size)
var END_H = 30;
var BUTTON_GAP = 6; // between End and Delay, one over the other
var CAPTION_W = 230; // the caption's strip under the bar
var CAPTION_H = 30;
var DIVIDER = 30;
var TOP = 14;
// The current card, the size of the party bar's portraits, selector and all
// (the user): in play their arcs measure 59 px against a card's 41 at 100,
// so the frame 72 against 50 (play test 5's capture, 2026-09-30).
var CURRENT_SCALE = 143;

var GOLD = 0xF0C850;
var PARCHMENT = 0xE8DFC8;
var INK = 0x14100C;
// A glow behind a medallion by its side (issue 13; the user, 2026-10-03:
// "a red glow around the portraits of enemies ... and a blue glow around
// allies that are not the party"): none for the party, whose mark is the
// gold arrow under its cards. The red and the blue are those of the rings
// the game draws on the ground under an enemy and under an ally.
var SIDE_GLOW = [null, 0x3C9CF0, 0xE82C1C]; // party, ally, enemy (core's Side)
var RANK_WORDS = ["", "", "", "a lieutenant", "a boss", "an elite boss"];

// Whether the plugin is there to ask. In the game the call answers; in a
// Flash player on its own, ExternalInterface is not available, or the
// player is told to show the sample (the parameter sample=1, as
// tools/snapshot.ps1 passes it).
var inGame = flash.external.ExternalInterface.available && _root.sample != "1" && _root.sample != "time" &&
             _root.sample != "many";
var lastView = null;
var lastCards = null;
var ticks = 0;

// A fight of four against three, round 2, the hero's turn, for a player
// with no game behind it.
function sampleView()
{
    // Each combatant once, the current first: the rest of round 2, then
    // round 3's up to the current (core/TurnOrder.h, Upcoming).
    // With a turn a bar of time (sample=time): 3.1 s left of a bar of 5.5, a
    // turn being 4.
    var timed = _root.sample == "time" || _root.sample == "many" || _root.time == "1";
    var head = (timed ? [BAR_VERSION, 1, 2, 257, 1, 1, "14.4", "14.4", 1, "3.1", "5.5", "4.0", "0.0", 2, "1.0", 0]
                      : [BAR_VERSION, 1, 2, 257, 1, 1, "9.0", "9.0", 0, "0.0", "0.0", "4.0", "0.0", 1, "0.0", 0])
                   .join(FIELD);
    var cards = [
        [257, 0, 0, 65, 0, 1, 1, 2, "Alistair", "gen00fl_alistair", "gui_portrait_09", 90, 0, "0.0"],
        [513, 2, 2, 40, 0, 0, 0, 2, "Genlock", "g", "", 100, 0, timed ? "1.6" : "0.0", 0, timed ? 617 : -1,
         timed ? "ico_shield_bash" : "", timed ? "Shield Bash" : ""],
        [258, 0, 0, 0, 1, 0, 1, 2, "Morrigan", "gen00fl_morrigan", "gui_portrait_49", 30, 1],
        [768, 1, 2, 90, 0, 0, 0, 2, "Soldier", "s", "gui_portrait_09", 80, 0, "0.0", 1],
        [514, 2, 3, 100, 0, 0, 0, 2, "Genlock Alpha", "a", "gui_portrait_49", 45, 0],
        [256, 0, 100, 100, 0, 0, 1, 3, "Warden", "player", "gui_portrait_09", 70, 0],
        [512, 2, 4, 80, 0, 0, 0, 3, "Hurlock Emissary", "e", "gui_portrait_49", 55, 1]
    ];
    if (_root.sample == "many")
    {
        // A village's defence (sample=many): twelve in the order, militia
        // beside the party and walking corpses against them, as Redcliffe's
        // square had them when Delay and End lay under the minimap (issue
        // 14); one of them with a plain attack kept (issue 15).
        cards = [
            [257, 0, 0, 65, 0, 1, 1, 2, "Alistair", "gen00fl_alistair", "gui_portrait_09", 90, 0, "0.0"],
            [256, 0, 100, 100, 0, 0, 1, 2, "Warden", "player", "gui_portrait_09", 70, 0],
            [513, 2, 2, 40, 0, 0, 0, 2, "Walking Corpse", "c", "gui_portrait_49", 100, 0, "0.0"],
            [259, 0, 0, 80, 0, 0, 1, 2, "Sten", "gen00fl_sten", "gui_portrait_09", 60, 0, "1.2", 0, -1, "ico_attack", "Attack"],
            [514, 2, 2, 100, 0, 0, 0, 2, "Walking Corpse", "c", "gui_portrait_49", 100, 0, "0.8", 0, -1, "ico_attack", "Attack"],
            [768, 1, 2, 90, 0, 0, 0, 2, "Militia", "m", "gui_portrait_09", 80, 0],
            [769, 1, 2, 75, 0, 0, 0, 2, "Tomas", "t", "gui_portrait_09", 80, 0],
            [515, 2, 2, 60, 0, 0, 0, 2, "Walking Corpse", "c", "gui_portrait_49", 100, 0],
            [770, 1, 2, 100, 0, 0, 0, 2, "Dwyn", "d", "gui_portrait_09", 80, 0],
            [258, 0, 0, 100, 0, 0, 1, 2, "Morrigan", "gen00fl_morrigan", "gui_portrait_49", 30, 1],
            [771, 1, 2, 55, 0, 0, 0, 2, "Militiaman", "m", "gui_portrait_09", 80, 0],
            [516, 2, 2, 100, 0, 0, 0, 3, "Walking Corpse", "c", "gui_portrait_49", 100, 0]
        ];
    }
    var text = head;
    for (var i = 0; i < cards.length; i++)
    {
        text += CARD + cards[i].join(FIELD);
    }
    return text;
}

function ask()
{
    if (!inGame)
    {
        return sampleView();
    }
    var view = flash.external.ExternalInterface.call("TB_GetView");
    return view == undefined || view == null ? "" : String(view);
}

// The view as fields: the head, and a record per card.
function parse(text)
{
    var records = text.split(CARD);
    var head = records[0].split(FIELD);
    var cards = [];
    for (var i = 1; i < records.length; i++)
    {
        var f = records[i].split(FIELD);
        cards.push({id: f[0], side: Number(f[1]), rank: Number(f[2]), health: Number(f[3]),
                    down: f[4] == "1", current: f[5] == "1", yours: f[6] == "1", round: Number(f[7]),
                    name: f[8], tag: f[9], portrait: f[10] == undefined ? "" : f[10],
                    energy: Number(f[11]), mana: f[12] == "1", owed: Number(f[13]), waits: f[14] == "1",
                    icon: f[16] == undefined ? "" : f[16], ability: f[17] == undefined ? "" : f[17]});
    }
    return {version: Number(head[0]), active: head[1] == "1", round: Number(head[2]), current: head[3],
            playerTurn: head[4] == "1", actions: Number(head[5]), movementLeft: head[6], movementMax: head[7],
            timed: head[8] == "1", timeLeft: head[9], time: head[10], turn: head[11], paid: Number(head[12]),
            delay: Number(head[13]), rolled: Number(head[14]), paused: head[15] == "1", cards: cards};
}

// Text as the game's own movies set it: the fonts they import from the
// game's font library, which this movie imports too. Outside the game those
// are not there, and a font of the system's stands in.
function label(clip, name, depth, x, y, w, h, size, colour, align, title)
{
    clip.createTextField(name, depth, x, y, w, h);
    var field = clip[name];
    field.selectable = false;
    field.embedFonts = inGame;
    var format = new TextFormat();
    format.font = inGame ? (title ? "$TitleFont" : "$BodyFont") : "Georgia";
    format.size = size;
    format.color = colour;
    format.align = align;
    field.setNewTextFormat(format);
    return field;
}

function box(clip, x, y, w, h, fill, alpha)
{
    clip.beginFill(fill, alpha);
    clip.moveTo(x, y);
    clip.lineTo(x + w, y);
    clip.lineTo(x + w, y + h);
    clip.lineTo(x, y + h);
    clip.lineTo(x, y);
    clip.endFill();
}

function outline(clip, x, y, w, h, thickness, colour)
{
    clip.lineStyle(thickness, colour, 100);
    clip.moveTo(x, y);
    clip.lineTo(x + w, y);
    clip.lineTo(x + w, y + h);
    clip.lineTo(x, y + h);
    clip.lineTo(x, y);
    clip.lineStyle();
}

// "Hurlock Emissary" is "HE", "Alistair" "Al": what fits a card with no
// portrait (dev/UI.md).
function initials(name)
{
    var words = name.split(" ");
    if (words.length > 1)
    {
        return (words[0].charAt(0) + words[1].charAt(0)).toUpperCase();
    }
    return name.substr(0, 2);
}

// A card's portrait, a texture's name, or null. The plugin sends one for
// every combatant it could get one for: the engine renders enemies' as it
// renders the party's (core/Portraits.h). Until it has, a party member's is
// found as the game's party bar finds it (partyMember, below).
function portraitOf(card)
{
    if (card.portrait.length > 0)
    {
        return card.portrait;
    }
    return card.side == 0 ? partyMember(card.id).portrait : null;
}

// A texture's URL as the game's own movies make it (portraits.gfx:
// MovieUtils.GetImageUrl): "img://", the name with ".dds" added when it has
// none, and the size wanted. Without the extension the loader finds nothing
// and draws grey (seen in play, 2026-09-30). The game's image loader looks
// the name up among its UI atlases first, then its textures (0x516A20).
// Outside the game, the same art as extracted to PNGs, for a look in a
// Flash player (tools/snapshot.ps1).
function imageUrl(name, width, height)
{
    if (name.indexOf("img://") == 0)
    {
        name = name.substr(6);
    }
    if (!inGame)
    {
        var dot = name.lastIndexOf(".");
        return "../research/portrait_art/png/" + (dot == -1 ? name : name.substr(0, dot)) + ".png";
    }
    if (name.lastIndexOf(".") == -1)
    {
        name += ".dds";
    }
    return "img://" + name + "?Width=" + Math.round(width) + "&Height=" + Math.round(height);
}

// The party bar's backgrounds, a painted orb per character: the frames of
// its Portrait2_back, 1 to 20, chosen by a member's PortraitTint (the
// portraits table's, by tag: Alistair 17, the dog 18, Morrigan 16, the rest
// 1 unless named). The textures are the atlas's levelUp_I158 and every third
// after (patch003's names; the unpatched game's differ).
function backgroundTexture(tint)
{
    return "levelUp_I" + (0x158 + 3 * (tint - 1)).toString(16).toUpperCase();
}

// An enemy's or ally's background: one of the twenty, the same for it every
// time the bar is drawn.
function strangerTint(id)
{
    return (Number(id) * 7) % 20 + 1;
}

// A party member as the game's party bar finds it (portraits.gfx:
// SharedLibrary.Portrait): the members are the data tree's Party.Members.0-3
// and Party.Summons.0-3, each with its ID, its Portrait (an image's name)
// and its PortraitProperties.PortraitTint.
var members = {};
function partyMember(id)
{
    if (members[id] != undefined)
    {
        return members[id];
    }
    var found = {portrait: null, tint: 1};
    if (inGame)
    {
        var groups = ["Party.Members.", "Party.Summons."];
        for (var g = 0; g < groups.length; g++)
        {
            for (var n = 0; n < 4; n++)
            {
                var token = groups[g] + n;
                if (String(flash.external.ExternalInterface.call("GetValue", token + ".ID")) == String(id))
                {
                    var name = flash.external.ExternalInterface.call("GetValue", token + ".Portrait");
                    if (name != null && name != undefined && String(name).length > 0 && String(name).charAt(String(name).length - 1) != "?")
                    {
                        found.portrait = String(name);
                    }
                    var tint = Number(flash.external.ExternalInterface.call("GetValue", token + ".PortraitProperties.PortraitTint"));
                    if (tint >= 1 && tint <= 20)
                    {
                        found.tint = tint;
                    }
                }
            }
        }
    }
    else
    {
        // The sample's: the Warden 1, Alistair 17, Morrigan 16.
        found.tint = id == 257 ? 17 : id == 258 ? 16 : 1;
    }
    if (found.portrait != null || !inGame)
    {
        members[id] = found;
    }
    return found;
}

// A texture into a new clip, placed and sized.
function art(clip, name, depth, x, y, w, h)
{
    var holder = clip.createEmptyMovieClip("art" + depth, depth);
    holder._x = x;
    holder._y = y;
    var image = holder.createEmptyMovieClip("image", 1);
    image.loadMovie(imageUrl(name, w, h));
    if (!inGame)
    {
        // A PNG comes at its own size: scaled to fit once it has loaded.
        holder.onEnterFrame = function()
        {
            if (this.image._width > 0)
            {
                this.image._width = w;
                this.image._height = h;
                delete this.onEnterFrame;
            }
        };
    }
    return holder;
}

function circle(clip, cx, cy, r, fill, alpha)
{
    var k = 0.4142 * r;
    var d = 0.7071 * r;
    clip.beginFill(fill, alpha);
    clip.moveTo(cx + r, cy);
    clip.curveTo(cx + r, cy + k, cx + d, cy + d);
    clip.curveTo(cx + k, cy + r, cx, cy + r);
    clip.curveTo(cx - k, cy + r, cx - d, cy + d);
    clip.curveTo(cx - r, cy + k, cx - r, cy);
    clip.curveTo(cx - r, cy - k, cx - d, cy - d);
    clip.curveTo(cx - k, cy - r, cx, cy - r);
    clip.curveTo(cx + k, cy - r, cx + d, cy - d);
    clip.curveTo(cx + r, cy - k, cx + r, cy);
    clip.endFill();
}

function ring(clip, cx, cy, r, thickness, colour, alpha)
{
    clip.lineStyle(thickness, colour, alpha);
    circle(clip, cx, cy, r, 0, 0);
    clip.lineStyle();
}

function drawCard(bar, card, index, x)
{
    var clip = bar.createEmptyMovieClip("card" + index, 100 + index);
    clip._x = x;
    clip._y = TOP;
    clip.id = card.id;
    // Under the pointer: its name, and its rank (a gem of the rank's colour
    // said it until 2026-10-02: "a yellow dot under it I did not
    // understand", play test 29).
    clip.fullName = card.name + (RANK_WORDS[card.rank] ? ", " + RANK_WORDS[card.rank] : "");
    var mid = CARD_W / 2;
    var orb = 74 * ART;
    var orbAt = 20 * ART;
    // Whose side it is on, by a glow behind the medallion: under all of its
    // art, so the gold ring and the arcs are as they were and the colour
    // stands about them. GFx 3 has no filters for shapes: the glow is rings
    // drawn wider and fainter outwards, as the movement preview's line is.
    var hue = SIDE_GLOW[card.side];
    if (hue != null && hue != undefined)
    {
        var halo = clip.createEmptyMovieClip("side", 0);
        for (var g = 0; g < 5; g++)
        {
            ring(halo, mid, CARD_H / 2, 24.5 + g * 1.1, 2 + g, hue, 52 - g * 10);
        }
    }
    art(clip, "Portraits_IC2", 1, 0, 0, CARD_W, CARD_H);
    // The current turn's: the party bar's own glow (SelectedPlayer), at the
    // frame's own size as the party bar has it, over the backplate's rim and
    // under the ring, so its circle hugs the medallion; the card is the
    // bigger for it (CURRENT_SCALE). Drawn a quarter larger behind the
    // backplate, the portrait sat small inside it (the user, 2026-09-30).
    if (card.current)
    {
        art(clip, "Portraits_I60", 2, 0, 0, CARD_W, CARD_H);
    }
    var tint = card.side == 0 ? partyMember(card.id).tint : strangerTint(card.id);
    art(clip, backgroundTexture(tint), 3, orbAt, orbAt, orb, orb);
    var portrait = portraitOf(card);
    if (portrait != null)
    {
        // Round, as the party bar shows it.
        var picture = art(clip, portrait, 4, 22 * ART, orbAt, 72 * ART, 72 * ART);
        var round = clip.createEmptyMovieClip("round", 5);
        circle(round, mid, orbAt + orb / 2, orb / 2, 0xFFFFFF, 100);
        picture.setMask(round);
    }
    else
    {
        var text = label(clip, "initials", 5, 0, mid - 13, CARD_W, 26, 17, PARCHMENT, "center", true);
        text.text = initials(card.name);
    }
    // Health: the party bar's red arc, filled from the foot to the share left.
    var health = art(clip, "Portraits_I41", 6, 0, 0, CARD_W, CARD_H);
    var level = clip.createEmptyMovieClip("level", 7);
    var share = Math.max(0, Math.min(100, card.health)) / 100;
    box(level, 0, CARD_H * (1 - share), CARD_W, CARD_H * share + 1, 0xFFFFFF, 100);
    health.setMask(level);
    // Mana (a mage's, blue) or stamina (gold): the party bar's right arc,
    // filled the same way (Portraits_I52 and I5B, portraits.gfx's ManaBar).
    var energy = art(clip, card.mana ? "Portraits_I52" : "Portraits_I5B", 11, 0, 0, CARD_W, CARD_H);
    var energyLevel = clip.createEmptyMovieClip("energyLevel", 12);
    var energyShare = Math.max(0, Math.min(100, card.energy)) / 100;
    box(energyLevel, 0, CARD_H * (1 - energyShare), CARD_W, CARD_H * energyShare + 1, 0xFFFFFF, 100);
    energy.setMask(energyLevel);
    art(clip, "Portraits_I3C", 16, 0, 0, CARD_W, CARD_H);
    // No rank gem at the foot: a dot of the game's rank colour (yellow, a
    // lieutenant's) asked what it was of the tester and of the user, who
    // took it out (2026-10-02: "let's remove the dot. later we'll add a card
    // on hover that shows stats of actor"). The caption under the pointer
    // still says the rank.
    if (card.yours)
    {
        // The game's gold arrow (the inventory's sort button's, inventory_I2F1),
        // turned to point up at the card.
        // Close under the portrait's ring: six pixels lower its tip lay on
        // the bar of time under the current card (the user, 2026-10-02: "should
        // be higher up closer to portrait, it's overlapping w/ the action
        // time bar").
        var pointer = art(clip, "inventory_I2F1", 18, mid - 7, CARD_H + 5, 14, 8);
        pointer._yscale = -100;
    }
    if (card.owed > 0)
    {
        // An action of its creature's is pending (core: Charge), and lands
        // on its next turn: said in words under the pointer. For a talent or
        // a spell its own icon stands at the card's shoulder, the picture
        // the quickbar shows for it, with a soft light behind it to lift it
        // off the portrait. First a number of seconds stood here in a ring
        // drawn by the script; the user, 2026-10-02: "crudely drawn", then
        // "what if we used the icon of the talent / spell instead of a
        // number? the number doesn't really tell us anything", and "no gold
        // ring needed i think, just icon. maybe icon should have a slight
        // glowing border". A pending attack has no icon: with four spiders
        // every card had a badge, noise and not news (play test 28).
        clip.fullName = pendingWords(card);
        if (card.icon.length > 0)
        {
            // Three quarters of the first size: at 24 it was "too big now"
            // (the user, 2026-10-02).
            var badge = 18;
            var badgeX = CARD_W - badge + 5;
            var badgeY = -5;
            var glow = clip.createEmptyMovieClip("pendingGlow", 19);
            for (var layer = 0; layer < 4; layer++)
            {
                roundedBox(glow, badgeX - layer * 0.75, badgeY - layer * 0.75, badge + 1.5 * layer, badge + 1.5 * layer,
                           2 + layer, 0xFFE9A8, 16);
            }
            if (inGame)
            {
                art(clip, card.icon, 20, badgeX, badgeY, badge, badge);
            }
            else
            {
                // Outside the game the icon's texture is not there: a dark
                // square with the ability's first letter stands in.
                var stand = clip.createEmptyMovieClip("pendingStand", 20);
                roundedBox(stand, badgeX, badgeY, badge, badge, 2, INK, 100);
                var letter = label(clip, "pendingLetter", 21, badgeX, badgeY, badge, badge, 11, GOLD, "center", true);
                letter.text = card.ability.length > 0 ? card.ability.charAt(0) : "?";
                centre(letter, badgeY, badge);
            }
        }
    }
    if (card.waits)
    {
        // It has put its turn off (Delay) and waits for it at the round's
        // end: said under its card, the floaty over it being gone with the
        // camera in the frame it was shown (play test 28).
        // Under the party's arrow, which stands close under the ring.
        var waits = label(clip, "waits", 22, -10, CARD_H + 4, CARD_W + 20, 16, 11, PARCHMENT, "center", false);
        waits.text = "waits";
        clip.fullName = card.name + " waits: its turn comes at the round's end";
    }
    if (card.down)
    {
        clip._alpha = 40;
    }
    if (card.current)
    {
        clip._xscale = CURRENT_SCALE;
        clip._yscale = CURRENT_SCALE;
        clip._y = TOP - CARD_H * (CURRENT_SCALE - 100) / 200; // centred on the others
    }
    clip.onRelease = function()
    {
        fscommand("TB_Focus", String(this.id));
    };
    clip.onRollOver = function()
    {
        _root.hovered = this.fullName;
        caption();
    };
    clip.onRollOut = function()
    {
        _root.hovered = "";
        caption();
    };
    return clip;
}

// Between rounds: the game's gold rule, fading at its end (PopupLayer_I4D),
// stood on end twice from the middle, and the next round's number in the
// party bar's small gold ring (Portraits_IF1) there.
function drawDivider(bar, round, index, x)
{
    var clip = bar.createEmptyMovieClip("divider" + index, 300 + index);
    clip._x = x;
    clip._y = TOP;
    var badge = 30;
    var reach = CARD_H / 2 + 4;
    var down = art(clip, "PopupLayer_I4D", 1, DIVIDER / 2 + 1.5, CARD_H / 2, reach, 3);
    down._rotation = 90;
    var up = art(clip, "PopupLayer_I4D", 2, DIVIDER / 2 - 1.5, CARD_H / 2, reach, 3);
    up._rotation = -90;
    var disc = clip.createEmptyMovieClip("disc", 3);
    circle(disc, DIVIDER / 2, CARD_H / 2, badge / 2 - 3, INK, 90);
    art(clip, "Portraits_IF1", 4, (DIVIDER - badge) / 2, (CARD_H - badge) / 2, badge, badge);
    var text = label(clip, "round", 5, 0, 0, DIVIDER, badge, 14, GOLD, "center", true);
    text.text = String(round);
    centre(text, (CARD_H - badge) / 2, badge);
}

// End and Delay: the inventory's leather button, a word on it, a command
// to the plugin on release. Delay stands over End while the turn may be
// put off to the round's end (nothing of it spent, once a round, someone
// standing after it): `y` is the button's top.
function drawButton(bar, name, depth, word, command, x, y, live)
{
    var clip = bar.createEmptyMovieClip(name, depth);
    clip._x = x;
    clip._y = y;
    clip.command = command;
    var faces = ["inventory_I2EC", "inventory_I2EE", "inventory_I2F4"];
    for (var i = 0; i < faces.length; i++)
    {
        art(clip, faces[i], 1 + i, 0, 0, END_W, END_H);
    }
    clip.face = function(n)
    {
        for (var f = 0; f < 3; f++)
        {
            this["art" + (1 + f)]._visible = f == n;
        }
    };
    clip.face(0);
    var text = label(clip, "caption", 5, 0, 0, END_W, END_H, 15, GOLD, "center", true);
    text.text = word;
    centreWord(text, 0, END_H);
    if (!live)
    {
        // Not to be had now: greyed, in its place. It still takes the click,
        // which would otherwise be an order in the world behind it.
        clip._alpha = 35;
        clip.useHandCursor = false;
        clip.onRelease = function()
        {
        };
        return;
    }
    clip.onRollOver = function()
    {
        this.face(1);
    };
    clip.onRollOut = function()
    {
        this.face(0);
    };
    clip.onDragOut = clip.onRollOut;
    clip.onPress = function()
    {
        this.face(2);
    };
    clip.onRelease = function()
    {
        this.face(1);
        fscommand(this.command, "");
    };
}

// The cards, the dividers and the button, laid out left to right and
// centred on the stage.
function build(state)
{
    // A card taken from under the pointer has no roll-out: its name stayed
    // in the caption through a whole turn (play test 30).
    _root.hovered = "";
    _root.bar.removeMovieClip();
    var bar = _root.createEmptyMovieClip("bar", 10);
    var x = 0;
    var round = state.round;
    for (var i = 0; i < state.cards.length; i++)
    {
        var card = state.cards[i];
        if (card.round != round)
        {
            drawDivider(bar, card.round, i, x);
            x += DIVIDER + GAP;
            round = card.round;
        }
        drawCard(bar, card, i, x);
        x += CARD_W + GAP + (card.current ? CARD_W * (CURRENT_SCALE - 100) / 100 : 0);
    }
    // The medallions are centred on the screen whether End is there or not;
    // End stands after them on the player's turn (the user: End made part of
    // the strip shifted its centring).
    var width = x - GAP;
    if (state.playerTurn)
    {
        // Where Delay is offered the two stand one over the other about the
        // medallions' middle through the whole turn, Delay greyed once it
        // is not to be had: End dropped to the middle as Delay went, and a
        // click where it had been missed it (play test 28). With no Delay
        // in the game (the setting) End stands alone at the middle.
        // Delay above, End below (the user, 2026-10-03, issue 9: "Maybe
        // the delay button should be on top of the end button"): End is
        // pressed nearly every turn and the lower place is the nearer to
        // the battlefield; Delay, to be had only before anything is spent,
        // is the turn's first choice.
        var middle = TOP + (CARD_H - END_H) / 2;
        var apart = (END_H + BUTTON_GAP) / 2;
        var offered = state.delay == 1 || state.delay == 2;
        // Clear of the last card's shoulder, where a pending ability's icon
        // stands: it lay against End's edge (play test 29).
        var buttonsAt = x + GAP + 10;
        drawButton(bar, "endTurn", 500, "End", "TB_EndTurn", buttonsAt, offered ? middle + apart : middle, true);
        if (offered)
        {
            drawButton(bar, "delay", 501, "Delay", "TB_Delay", buttonsAt, middle - apart, state.delay == 1);
        }
    }
    // Centred, unless its buttons would then reach under the minimap: with
    // a long order the strip stands as far left of the middle as keeps them
    // clear (issue 14), and no farther than the party's own portraits. By
    // the buttons' place whether they are shown or not, so that the strip
    // does not shift as the turn passes from the player to an AI.
    var centred = Math.round((Stage.width - width) / 2);
    var clear = Math.round(Stage.width - MINIMAP - (x + GAP + 10 + END_W));
    bar._x = Math.max(Math.min(centred, clear), Math.min(centred, PARTY_BAR));
    // Behind the medallions alone, a translucent black panel, square with
    // rounded corners and edges that fade out (the user: no tapering ends,
    // as the top menu's bar had); a small one of the same under the
    // medallions' middle for the caption. End and Delay stand outside it:
    // taken in, the panel grew and shrank as they came and went with the
    // player's turns (the user, 2026-10-02: "background only covers the
    // portraits").
    // (The game's own "Paused", written across the top of the screen behind
    // the bar, is hidden by the plugin through a fight: src/game/Bar.cpp.
    // The plate was solid black over it while paused, for a day.)
    panel(bar.createEmptyMovieClip("plate", 3), -16, TOP - 14, width + 32, CARD_H + 34, 10);
    // With a turn a bar of time, on the player's turn: the bar, and the
    // caption under it.
    var captionTop = TOP + CARD_H + 20;
    var barMiddle = width / 2;
    if (state.timed && state.playerTurn)
    {
        // Clear of the current card's pointer and of "waits" under a card:
        // eight pixels higher the pointer's tip lay on the bar's left end
        // (seen in play, 2026-10-02).
        captionTop += 8;
        // Centred under the medallions, as the strip is on the screen. For
        // an evening it began under the current card, the first (play test
        // 31: with nine cards it lay under cards three to six); the user,
        // 2026-10-02: "time bar and text below isn't horizontally centered".
        buildTime(bar, barMiddle, captionTop);
        captionTop += TIME_H + 8;
    }
    var strip = bar.createEmptyMovieClip("strip", 4);
    strip.top = captionTop;
    strip.middle = barMiddle;
    panel(strip.createEmptyMovieClip("plate", 1), barMiddle - CAPTION_W / 2, captionTop, CAPTION_W, CAPTION_H, 8);
    var text = label(strip, "text", 2, barMiddle - CAPTION_W / 2, captionTop, CAPTION_W, CAPTION_H, 14, PARCHMENT,
                     "center", false);
    strip._visible = false;
}

// With a turn a bar of time: the bar, the mod's own, under the medallions on
// the player's turn. It is made of the game's experience bar's art (its dark
// ground, CharacterRecord_I114, and its gold fill, _I117, a strip stretched
// from the left; quickbar.gfx, read 2026-10-02), a fifth wider and half as
// tall again, with a tick of its own at every second and a longer one where
// a turn's own seconds end, past which the bar is what rolled over and
// Combat Tactics'. What the order under the cursor would cost is shaded on
// it: the part of the gold it would spend pale, all that is left red when
// it does not fit and would land on the next turn.
//
// For half a day (2026-10-02) this was drawn on the game's experience bar
// itself, which the plugin filled; the user, having seen it: "the action
// bar seems to replace the experience bar? that's confusing. we should have
// it separate."
var TIME_W = 300;
var TIME_H = 12;
var TIME_INSET = 1.5; // the ground's frame, inside which the fill lies
function fillRect(clip, x, y, w, h, colour, alpha)
{
    if (!(w > 0) || !(h > 0))
    {
        return;
    }
    clip.beginFill(colour, alpha);
    clip.moveTo(x, y);
    clip.lineTo(x + w, y);
    clip.lineTo(x + w, y + h);
    clip.lineTo(x, y + h);
    clip.lineTo(x, y);
    clip.endFill();
}

function buildTime(bar, middle, top)
{
    var clip = bar.createEmptyMovieClip("time", 6);
    clip._x = Math.round(middle - TIME_W / 2);
    clip._y = top;
    if (inGame)
    {
        art(clip, "CharacterRecord_I114", 1, 0, 0, TIME_W, TIME_H);
        art(clip, "CharacterRecord_I117", 2, TIME_INSET, TIME_INSET, TIME_W - 2 * TIME_INSET,
            TIME_H - 2 * TIME_INSET);
    }
    else
    {
        // Outside the game its art is not there: plain rectangles stand in.
        clip.createEmptyMovieClip("plain", 2);
    }
    clip.createEmptyMovieClip("mark", 3);
    clip.createEmptyMovieClip("ticks", 4);
    // Under the rest: a gold glow about the bar's bonus seconds, past a
    // turn's own (Combat Tactics' and what rolled over).
    clip.createEmptyMovieClip("bonus", 0);
}

function drawTime(state)
{
    var clip = _root.bar.time;
    if (clip == undefined)
    {
        return;
    }
    var whole = Number(state.time);
    var left = Math.min(whole, Math.max(0, Number(state.timeLeft)));
    clip._visible = whole > 0;
    if (!(whole > 0))
    {
        return;
    }
    var inner = TIME_W - 2 * TIME_INSET;
    var tall = TIME_H - 2 * TIME_INSET;
    var share = left / whole;
    if (inGame)
    {
        clip.art2._visible = share > 0.002;
        clip.art2._xscale = Math.max(0.2, share * 100);
    }
    else
    {
        clip.plain.clear();
        fillRect(clip.plain, 0, 0, TIME_W, TIME_H, INK, 100);
        fillRect(clip.plain, TIME_INSET, TIME_INSET, inner * share, tall, 0xD9A441, 100);
    }
    clip.mark.clear();
    if (timeCost > 0 && left > 0)
    {
        var from = timeSpills ? 0 : Math.max(0, left - timeCost) / whole;
        fillRect(clip.mark, TIME_INSET + inner * from, TIME_INSET, inner * (share - from), tall,
                 timeSpills ? 0xD8402A : 0xFFFFFF, timeSpills ? 70 : 62);
    }
    clip.ticks.clear();
    for (var second = 1; second < whole - 0.05; second++)
    {
        fillRect(clip.ticks, TIME_INSET + inner * second / whole - 0.75, TIME_INSET, 1.5, tall, INK, 85);
    }
    // The seconds past a turn's own glow gold about their edge, as a pending
    // action's icon does: bonus time. A white mark stood there before, which
    // the user found unclear: "we already have segments that show where 4s
    // is. if we want to show clearly it's bonus, we can make the bonus part
    // glow gold edge or something" (2026-10-02).
    clip.bonus.clear();
    var turn = Number(state.turn);
    if (turn > 0 && whole > turn + 0.05)
    {
        var from = TIME_INSET + inner * turn / whole;
        for (var layer = 1; layer <= 4; layer++)
        {
            roundedBox(clip.bonus, from - layer * 0.5, -layer, TIME_W - from + layer * 1.5, TIME_H + 2 * layer,
                       2 + layer, 0xF0C850, 22);
        }
    }
}

// What a pending action's badge says under the pointer, and the caption over
// its creature: when it lands. ("An action lands in 1.2 s of its time" took
// a second read: play test 28.)
function pendingWords(card)
{
    var what = card.ability.length > 0 ? card.ability : (card.icon.length > 0 ? "an ability" : "an attack");
    return card.name + ": " + what + " lands " + tenths(card.owed) + " s into its next turn";
}

var timeCost = 0;
var timeSpills = false;
var timeOver = "0"; // the creature the cursor is over, by its id (the preview's)
var timeState = null;

// A translucent black panel with rounded corners, its edges fading out: a
// rounded rectangle a little smaller each layer, the layers' black adding
// up towards the middle, so the rim is feathered, not a line.
var PANEL_ALPHA = 7; // each layer's black, percent
var PANEL_LAYERS = 9;
function panel(clip, x, y, w, h, radius)
{
    for (var i = 0; i < PANEL_LAYERS; i++)
    {
        var inset = i * 1.2;
        roundedBox(clip, x + inset, y + inset, w - 2 * inset, h - 2 * inset, Math.max(2, radius - inset), 0x000000,
                   PANEL_ALPHA);
    }
}

function roundedBox(clip, x, y, w, h, r, fill, alpha)
{
    clip.beginFill(fill, alpha);
    clip.moveTo(x + r, y);
    clip.lineTo(x + w - r, y);
    clip.curveTo(x + w, y, x + w, y + r);
    clip.lineTo(x + w, y + h - r);
    clip.curveTo(x + w, y + h, x + w - r, y + h);
    clip.lineTo(x + r, y + h);
    clip.curveTo(x, y + h, x, y + h - r);
    clip.lineTo(x, y + r);
    clip.curveTo(x, y, x + r, y);
    clip.endFill();
}

// A number of seconds or meters with one decimal, always: "2.0", not "2".
function tenths(value)
{
    var whole = Math.round(Number(value) * 10);
    return Math.floor(whole / 10) + "." + (whole % 10);
}

// The caption under the bar: a card's name while the pointer is on it,
// otherwise on the player's turn the movement left, and nothing else (the
// user: not the round, not the name, not "action ready", which nearly
// always is, the turn ending once the action is spent).
var captionText = "";
function caption()
{
    var strip = _root.bar.strip;
    var line = _root.hovered != undefined && _root.hovered.length > 0 ? _root.hovered : captionText;
    strip.text.text = line;
    var w = Math.max(CAPTION_W, strip.text.textWidth + 44);
    strip.plate.removeMovieClip();
    panel(strip.createEmptyMovieClip("plate", 1), strip.middle - w / 2, strip.top, w, CAPTION_H, 8);
    strip.text._x = strip.middle - w / 2;
    strip.text._width = w;
    centre(strip.text, strip.top, CAPTION_H);
    strip._visible = line.length > 0;
}

// A field's text centred up and down in a band, by its line: GFx puts a
// line at the field's top, two pixels in. The round's number sat centred in
// its ring this way in play; raised by the letters' measure it sat high
// (the user, 2026-09-30).
function centre(field, top, height)
{
    field._height = field.textHeight + 4;
    field._y = top + (height - field.textHeight) / 2 - 2;
}

// A word centred on a button by its letters, not its line: End sat low on
// its button by the line in play (the user, 2026-09-30, twice), the game's
// title font leaving a deep descent under the baseline. The caps' middle is
// taken at two fifths of the ascent above the baseline; without the font's
// extent, the line raised by a sixth of its height.
function centreWord(field, top, height)
{
    field._height = field.textHeight + 4;
    var extent = field.getNewTextFormat().getTextExtent(field.text);
    if (extent != undefined && extent.ascent > 0)
    {
        field._y = top + height / 2 - 2 - extent.ascent * 0.8;
        return;
    }
    field._y = top + (height - field.textHeight) / 2 - 2 - field.textHeight / 6;
}

// On the player's turn, the movement left, or with a turn a bar of time
// (core: TurnBudget) the time left and how far it would walk; nothing on
// anyone else's.
// The creature under the cursor has an action pending: when it lands, in
// its own time (the same words as its card's, under the pointer).
function pendingOf(state)
{
    if (timeOver == "0" || timeOver == undefined)
    {
        return "";
    }
    for (var i = 0; i < state.cards.length; i++)
    {
        var card = state.cards[i];
        if (card.id == timeOver && card.owed > 0)
        {
            return pendingWords(card);
        }
    }
    return "";
}

// The caption's words: that the player has the game paused, on anyone's
// turn (the pause key pauses; nothing else on the screen said so: play test
// 29), else what the turn has left.
function words(state)
{
    return state.paused ? "Paused: Space runs the game again" : describe(state);
}

function describe(state)
{
    if (!state.playerTurn)
    {
        return "";
    }
    if (state.timed)
    {
        var pending = pendingOf(state);
        // What the cursor is on would cost, when the preview says; else what
        // is left.
        if (timeCost > 0)
        {
            var after = Math.max(0, Number(state.timeLeft) - timeCost);
            // The cost alone: with the creature's pending action after it
            // the line was two thoughts and a third of the screen wide
            // (play test 28). That is said when no order is priced.
            return timeSpills ? "Costs " + tenths(timeCost) + " s: lands next turn"
                              : "Costs " + tenths(timeCost) + " s, leaving " + tenths(after) + " s";
        }
        if (pending.length > 0)
        {
            return pending;
        }
        // Nothing left. With an action of its own pending, its walk is under
        // way and the turn about to pass: no word. Else the turn waits, and
        // says what can still be done: with 0.01 s left the bar stood dark
        // and silent for half a minute (play test 29).
        if (!(Number(state.timeLeft) > 0))
        {
            for (var mine = 0; mine < state.cards.length; mine++)
            {
                if (state.cards[mine].current && state.cards[mine].owed > 0)
                {
                    return "";
                }
            }
            return "No time left: an action begun now lands next turn";
        }
        // A turn that began by paying for an action begun the turn before
        // says so, while nothing else has been spent.
        var paid = state.paid > 0 && Math.abs(Number(state.time) - state.paid - Number(state.timeLeft)) < 0.05;
        // And one longer than a turn by what rolled over says that, while it
        // is whole: a turn ended early gave the next 5.0 s with no word of
        // why (play test 28).
        var saved = state.rolled > 0 && Math.abs(Number(state.time) - Number(state.timeLeft)) < 0.05;
        return state.timeLeft + " s left, " + state.movementLeft + " m to walk" +
               (paid ? " (" + tenths(state.paid) + " s paid for last turn's action)"
                     : saved ? " (" + tenths(state.rolled) + " s saved from last turn)" : "");
    }
    return state.movementLeft + " of " + state.movementMax + " m to move";
}

// Outside the game: a dark, uneven backdrop like a scene's, so the bar's
// translucent black can be judged in a snapshot.
function drawBackdrop()
{
    if (inGame || _root.backdrop != undefined)
    {
        return;
    }
    var backdrop = _root.createEmptyMovieClip("backdrop", 1);
    var matrix = {matrixType: "box", x: 0, y: 0, w: Stage.width, h: 300, r: Math.PI / 3};
    backdrop.beginGradientFill("linear", [0x5A4A3A, 0x2A3444, 0x6A5A40], [100, 100, 100], [0, 128, 255], matrix);
    backdrop.moveTo(0, 0);
    backdrop.lineTo(Stage.width, 0);
    backdrop.lineTo(Stage.width, 300);
    backdrop.lineTo(0, 300);
    backdrop.lineTo(0, 0);
    backdrop.endFill();
}

function draw(view)
{
    drawBackdrop();
    if (view == lastView)
    {
        return;
    }
    lastView = view;
    var state = view == "" ? null : parse(view);
    if (state == null || state.version != BAR_VERSION || !state.active)
    {
        _root.bar.removeMovieClip();
        lastCards = null;
        timeState = null;
        // The cards and their images are gone: the plugin may release the
        // portraits it rendered for them (core/Portraits.h).
        if (inGame)
        {
            fscommand("TB_Cleared", "");
        }
        return;
    }
    var at = view.indexOf(CARD);
    // Rebuilt when the cards change, or whose turn it is, or whether Delay is
    // to be had (it goes as soon as anything of the turn is spent).
    var cards = (at < 0 ? "" : view.substr(at)) + (state.playerTurn ? "+" : "-") + state.delay +
                (state.timed ? "t" : "") + state.round;
    if (cards != lastCards)
    {
        lastCards = cards;
        build(state);
    }
    timeState = state;
    drawTime(state);
    captionText = words(state);
    caption();
}

// The movement preview (dev/UI.md, "The movement preview"): on the player's
// turn, the path to the ground under the cursor, gold as far as the movement
// left reaches and red beyond, a ring on the ground where the movement runs
// out and where the path ends, and its length over the end. The plugin gives
// the path's points where they are on the screen (TB_GetPreview, one string:
// src/core/Preview.h), in pixels of the game's view, and the view's size
// with them: the stage is laid over the view whole and centred (showAll),
// so a pixel is a stage point by one scale and an offset (seen so in play).
// Asked every frame, since the points move with the camera.
var PREVIEW_VERSION = 5;
// The stage as tools/movie.py makes it (tools/test_movie.py holds the two
// equal). Not Stage.width and Stage.height: in the game Stage.height is the
// view's height in stage units, not the stage's (800 for a 1600 x 1000 view,
// where the stage is 720), and by it the line was drawn 49 pixels low at
// every point (the fifteenth play test, 2026-10-01). The bar, by its cards'
// place in the same picture, lies where showAll puts this stage: scaled by
// one number, centred.
var STAGE_W = 1280;
var STAGE_H = 720;
var lastPreview = null;

// For a player with no game behind it: a walk round a corner, a third of it
// past the movement left.
function samplePreview()
{
    // Too far, which shows the most: in reach the line ends in the game's
    // own marker, which is the world's and not this movie's to draw.
    // With a turn a bar of time (sample=time): what the order would cost,
    // and that it fits.
    var timed = _root.sample == "time" || _root.sample == "many" || _root.time == "1";
    var head = [PREVIEW_VERSION, STAGE_W, STAGE_H, 2, "13.6", "6.5", 1, timed ? "1.9" : "0.0", 0, timed ? 513 : 0]
                   .join(FIELD);
    var reach = [300, 262, 470, 250, 560, 214, 640, 200].join(FIELD);
    var beyond = [640, 200, 720, 186, 800, 204, 900, 196].join(FIELD);
    return head + CARD + reach + CARD + beyond + CARD + sampleRing(640, 200, 15).join(FIELD) + CARD +
           sampleRing(900, 196, 26).join(FIELD);
}

// A ring on the ground as the camera sees it: flattened.
function sampleRing(x, y, radius)
{
    var points = [];
    for (var i = 0; i < 16; i++)
    {
        var angle = i * Math.PI / 8;
        // To the east first, then the north, which is up the screen.
        points.push(Math.round(x + Math.cos(angle) * radius), Math.round(y - Math.sin(angle) * radius * 0.42));
    }
    return points;
}

function askPreview()
{
    if (!inGame)
    {
        return samplePreview();
    }
    var preview = flash.external.ExternalInterface.call("TB_GetPreview");
    return preview == undefined || preview == null ? "" : String(preview);
}

// A record's numbers as stage points.
function previewPoints(record, scale, offsetX, offsetY)
{
    var points = [];
    if (record == undefined || record.length == 0)
    {
        return points;
    }
    var f = record.split(FIELD);
    for (var i = 0; i + 1 < f.length; i += 2)
    {
        points.push({x: (Number(f[i]) - offsetX) / scale, y: (Number(f[i + 1]) - offsetY) / scale});
    }
    return points;
}

// The line's look (the user, 2026-10-01: "a glowing golden line that fits
// the game UI"). GFx 3 has no filters for shapes, so the glow is the line
// drawn again and again, wide and faint to narrow and bright: a soft halo
// in the colour, the colour itself, and a pale thread of light down its
// middle, as the bar's gold rings have. Smoothed, since a path's corners
// are the pathfinder's, not a walk's: a few pixels at each corner only, so
// the line stays on the route.
//
// The gold is the selection ring's, the circle round the character whose
// turn it is, which the line starts from (the user, 2026-10-01, of a line
// in the movement marker's colours, white at its heart: "it still looks
// kind of yellowish rather than the same gold as the circle around the
// character selected"). Measured in the game's own picture (play test 16,
// the ring under Lyna): its body 0xE9BA5F to 0xEBC67C, its dim side
// 0xC48840 to 0xCF9B52, its highlights 0xFEEFA2; the line beside it read
// 0xCBB475, paler and greener, its near-white heart being half its width.
// The ring's art (vfx_selparty.dds) is a deeper orange-gold still, 0xAA7423
// with highlights 0xCEA24A, lit by the scene. So: an orange-gold body a
// little richer than the ring reads in play, since a thin line takes more
// of the ground's colour than a wide ring; a thin pale-gold highlight, not
// white; an orange glow. The red is the same made red.
var MARKER_HALO = 0xD98C28;
var MARKER_EDGE = 0xEFB045;
var MARKER_HEART = 0xFFEBA2;
var RED_HALO = 0xFF3A14;
var RED_EDGE = 0xD8402C;
var RED_HEART = 0xFFE6DA;
// Each pass: width in stage pixels, which colour (0 shadow, 1 halo, 2 the
// edge, 3 the heart), alpha.
var GLOW = [[9, 0, 14], [24, 1, 5], [17, 1, 8], [11.5, 1, 13], [7.5, 1, 24], [4.8, 2, 100], [1.5, 3, 80]];

// A path through its points, its corners rounded a little. Closed, it is a
// ring: curved from the middle of each side to the middle of the next, all
// the way round.
var CORNER = 7; // stage pixels of a corner that are rounded
var CLEAR = 14; // stage pixels the line ends short of the cursor: half its widest glow, and two
function tracePath(clip, points, closed)
{
    var n = points.length;
    if (closed)
    {
        var from = points[n - 1];
        clip.moveTo((from.x + points[0].x) / 2, (from.y + points[0].y) / 2);
        for (var i = 0; i < n; i++)
        {
            var next = points[(i + 1) % n];
            clip.curveTo(points[i].x, points[i].y, (points[i].x + next.x) / 2, (points[i].y + next.y) / 2);
        }
        return;
    }
    // The route itself, straight from point to point, and only its
    // corners rounded, within CORNER pixels of each: curved from middle to
    // middle, a long step's corner was drawn a meter inside the route, as
    // if the walk cut through what it goes round (the user, 2026-10-01:
    // "does a path represent a path that the character will actually
    // take? If not, it might be misleading").
    clip.moveTo(points[0].x, points[0].y);
    for (var j = 1; j < n - 1; j++)
    {
        var a = points[j - 1];
        var b = points[j];
        var c = points[j + 1];
        var before = Math.sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        var after = Math.sqrt((c.x - b.x) * (c.x - b.x) + (c.y - b.y) * (c.y - b.y));
        var r = Math.min(CORNER, Math.min(before, after) / 2);
        if (!(r > 0.5))
        {
            clip.lineTo(b.x, b.y);
            continue;
        }
        clip.lineTo(b.x + (a.x - b.x) * r / before, b.y + (a.y - b.y) * r / before);
        clip.curveTo(b.x, b.y, b.x + (c.x - b.x) * r / after, b.y + (c.y - b.y) * r / after);
    }
    clip.lineTo(points[n - 1].x, points[n - 1].y);
}

// A line ended `by` pixels short of its last point: its glow is as wide as
// it is long, and nothing of the movie's is to lie on the cursor's own
// pixel (drawPreview has why). The last steps go if they are shorter.
function shortened(points, by)
{
    var out = points.slice();
    while (out.length >= 2)
    {
        var a = out[out.length - 2];
        var b = out[out.length - 1];
        var step = Math.sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        if (step > by)
        {
            out[out.length - 1] = {x: b.x + (a.x - b.x) * by / step, y: b.y + (a.y - b.y) * by / step};
            return out;
        }
        by -= step;
        out.pop();
    }
    return out;
}

// `thin` for a ring: the same glow at about half the width.
function glow(clip, points, red, closed, thin)
{
    if (points.length < (closed ? 3 : 2))
    {
        return;
    }
    var colours = red ? [INK, RED_HALO, RED_EDGE, RED_HEART] : [INK, MARKER_HALO, MARKER_EDGE, MARKER_HEART];
    for (var p = 0; p < GLOW.length; p++)
    {
        clip.lineStyle(GLOW[p][0] * (thin ? 0.6 : 1), colours[GLOW[p][1]], GLOW[p][2]);
        tracePath(clip, points, closed);
    }
}

// The length, on a small dark plate as the bar's caption is, so it reads
// on any ground.
function previewLabel(clip, text, colour, at)
{
    var field = clip.length;
    field.text = text;
    field.textColor = colour;
    var w = field.textWidth + 18;
    var h = 22;
    var x = at.x - w / 2;
    var y = at.y - 44;
    clip.lineStyle(undefined); // the glow's last line would outline the plate
    // Two soft layers, not the bar's nine: at this size those showed as
    // rings inside one another (the sixteenth play test's pictures).
    roundedBox(clip, x - 1, y - 1, w + 2, h + 2, 9, 0x000000, 28);
    roundedBox(clip, x, y, w, h, 8, 0x000000, 46);
    field._x = x;
    field._width = w;
    centre(field, y, h);
    field._visible = true;
}

function drawPreview(text)
{
    if (text == lastPreview)
    {
        return;
    }
    lastPreview = text;
    var clip = _root.preview;
    if (clip == undefined)
    {
        // Under the bar, over the sample's backdrop.
        clip = _root.createEmptyMovieClip("preview", 5);
        label(clip, "length", 1, 0, 0, 160, 24, 15, GOLD, "center", false);
        // Not there for the mouse. The preview's end is under the cursor,
        // and the game asks its interface first whether the mouse is over a
        // movie's shape: if so the cursor has no ground and a click is the
        // interface's. With the end drawn on the cursor's pixel the preview
        // blinked and eight clicks of eight to move were lost (the
        // sixteenth play test, 2026-10-01; the cause read by lib-agent from
        // the mode's pick, 0x0070EAA0). hitTestDisable is Scaleform's own,
        // an extension, which the game's movies turn on as here.
        _global.gfxExtensions = true;
        clip.hitTestDisable = true;
        clip.length.hitTestDisable = true;
    }
    clip.clear();
    clip.length._visible = false;
    var records = text.split(CARD);
    var head = records[0].split(FIELD);
    var viewW = Number(head[1]);
    var viewH = Number(head[2]);
    // What the order under the cursor would cost of the bar of time, shown
    // on the bar (drawTime) and said in the caption.
    var cost = Number(head[0]) == PREVIEW_VERSION && Number(head[7]) > 0 ? Number(head[7]) : 0;
    var spills = head[8] == "1";
    // And the creature the cursor is over (version 5), for its pending action.
    var over = Number(head[0]) == PREVIEW_VERSION && head[9] != undefined ? head[9] : "0";
    if (cost != timeCost || spills != timeSpills || over != timeOver)
    {
        timeCost = cost;
        timeSpills = spills;
        timeOver = over;
        if (timeState != null)
        {
            drawTime(timeState);
            captionText = words(timeState);
            caption();
        }
    }
    if (Number(head[0]) != PREVIEW_VERSION || head[3] == "0" || head[3] == undefined || !(viewW > 0) ||
        !(viewH > 0))
    {
        return;
    }
    var scale = Math.min(viewW / STAGE_W, viewH / STAGE_H);
    var offsetX = (viewW - STAGE_W * scale) / 2;
    var offsetY = (viewH - STAGE_H * scale) / 2;
    var reach = previewPoints(records[1], scale, offsetX, offsetY);
    var beyond = previewPoints(records[2], scale, offsetX, offsetY);
    // The rings lie on the ground, as the game's own selection ring does:
    // the plugin gives each as its points on the screen.
    var stopRing = previewPoints(records[3], scale, offsetX, offsetY);
    var endRing = previewPoints(records[4], scale, offsetX, offsetY);
    var tooFar = head[3] == "2";
    var end = beyond.length > 0 ? beyond[beyond.length - 1] : reach[reach.length - 1];
    // The line stops short of its end, which is the cursor's pixel: the
    // clip is not there for the mouse (above), and if that did not hold in
    // some build of the game, a shape on that pixel would take the cursor's
    // ground again. The game's marker lies there in reach, a ring round it
    // too far, and the plate above it.
    if (tooFar)
    {
        beyond = shortened(beyond, CLEAR);
    }
    else
    {
        reach = shortened(reach, CLEAR);
    }
    // The line is the movie's unless the path is marked in the world, on
    // the ground (the head's last field): then only the length is drawn.
    if (head[6] != "0")
    {
        // Red under gold, so the gold's glow lies over where they meet.
        glow(clip, beyond, true, false, false);
        glow(clip, reach, false, false, false);
        if (tooFar)
        {
            glow(clip, endRing, true, true, true);
            glow(clip, stopRing, false, true, true);
        }
        else
        {
            // In reach the end is the game's own movement marker, which
            // the plugin places in the world, on the ground; a ring comes
            // for the end only if that could not be placed.
            glow(clip, endRing, false, true, true);
        }
    }
    if (end != undefined)
    {
        var plate = tooFar ? head[4] + " m of " + head[5] : head[4] + " m";
        if (!tooFar && cost > 0)
        {
            plate += spills ? ", " + tenths(cost) + " s: next turn" : ", " + tenths(cost) + " s";
        }
        previewLabel(clip, plate, tooFar ? RED_HEART : GOLD, end);
    }
}

Stage.scaleMode = "showAll";
_root.onEnterFrame = function()
{
    // Three times a second is enough to follow a turn; the plugin's view is
    // at most a tenth of a second old anyway.
    if (ticks++ % 10 == 0)
    {
        draw(ask());
    }
    drawPreview(askPreview());
};
draw(ask());
