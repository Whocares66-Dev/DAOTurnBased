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

var BAR_VERSION = 2;
var CARD = String.fromCharCode(30);
var FIELD = String.fromCharCode(31);

// A card is a medallion, the party bar's own (portraits.gfx, Portrait1_mc,
// drawn at 114 px; here at CARD_W): its backplate, a painted background, the
// portrait, the red health arc and the gold ring, all the game's textures
// by name (build/research/portrait_art/README.md, 2026-09-30).
var CARD_W = 60;
var CARD_H = 60;
var GAP = 5;
var ART = CARD_W / 114;
var DIVIDER = 22;
var TOP = 14;
var CURRENT_SCALE = 118;

var GOLD = 0xF0C850;
var PARCHMENT = 0xE8DFC8;
var INK = 0x14100C;
var SIDE_COLOURS = [0x3C8CD8, 0x4CA84C, 0xC83C32]; // party, ally, enemy
var RANK_COLOURS = [0, 0, 0, 0xFFFF00, 0xFF6600, 0xFF6600]; // creatureranks.gda: lieutenant, boss, elite boss

// Whether the plugin is there to ask. In the game the call answers; in a
// Flash player on its own, ExternalInterface is not available, or the
// player is told to show the sample (the parameter sample=1, as
// tools/snapshot.ps1 passes it).
var inGame = flash.external.ExternalInterface.available && _root.sample != "1";
var lastView = null;
var lastCards = null;
var ticks = 0;

// A fight of four against three, round 2, the hero's turn, for a player
// with no game behind it.
function sampleView()
{
    var head = [BAR_VERSION, 1, 2, 256, 1, 1, "6.5", "9.0"].join(FIELD);
    var cards = [
        [256, 0, 100, 100, 0, 1, 1, 2, "Warden", "player", "gui_portrait_09"],
        [512, 2, 4, 80, 0, 0, 0, 2, "Hurlock Emissary", "e", "gui_portrait_49"],
        [257, 0, 0, 65, 0, 0, 1, 2, "Alistair", "gen00fl_alistair", "gui_portrait_09"],
        [513, 2, 2, 40, 0, 0, 0, 2, "Genlock", "g", ""],
        [258, 0, 0, 0, 1, 0, 1, 2, "Morrigan", "gen00fl_morrigan", "gui_portrait_49"],
        [768, 1, 2, 90, 0, 0, 0, 2, "Soldier", "s", "gui_portrait_09"],
        [514, 2, 3, 100, 0, 0, 0, 2, "Genlock Alpha", "a", "gui_portrait_49"],
        [256, 0, 100, 100, 0, 0, 1, 3, "Warden", "player", "gui_portrait_09"],
        [512, 2, 4, 80, 0, 0, 0, 3, "Hurlock Emissary", "e", "gui_portrait_49"],
        [257, 0, 0, 65, 0, 0, 1, 3, "Alistair", "gen00fl_alistair", "gui_portrait_09"]
    ];
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
                    name: f[8], tag: f[9], portrait: f[10] == undefined ? "" : f[10]});
    }
    return {version: Number(head[0]), active: head[1] == "1", round: Number(head[2]), current: head[3],
            playerTurn: head[4] == "1", actions: Number(head[5]), movementLeft: head[6], movementMax: head[7],
            cards: cards};
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
    clip.fullName = card.name;
    var mid = CARD_W / 2;
    var orb = 74 * ART;
    var orbAt = 20 * ART;
    // The current turn's: the party bar's glow ring, behind.
    if (card.current)
    {
        art(clip, "Portraits_I60", 1, -CARD_W * 0.12, -CARD_H * 0.12, CARD_W * 1.24, CARD_H * 1.24);
    }
    art(clip, "Portraits_IC2", 2, 0, 0, CARD_W, CARD_H);
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
    art(clip, "Portraits_I3C", 8, 0, 0, CARD_W, CARD_H);
    // Whose side: a rim of colour outside the gold for those not the party's.
    if (card.side != 0)
    {
        ring(clip, mid, mid, mid + 1.5, 2, SIDE_COLOURS[card.side], 85);
    }
    // A lieutenant's or a boss's gem, at the foot.
    if (RANK_COLOURS[card.rank])
    {
        var gem = clip.createEmptyMovieClip("gem", 9);
        circle(gem, mid, CARD_H - 3, 4, RANK_COLOURS[card.rank], 100);
        ring(gem, mid, CARD_H - 3, 4, 1, INK, 100);
    }
    if (card.yours)
    {
        clip.beginFill(PARCHMENT, 100);
        clip.moveTo(mid - 5, CARD_H + 9);
        clip.lineTo(mid + 5, CARD_H + 9);
        clip.lineTo(mid, CARD_H + 3);
        clip.endFill();
    }
    if (card.down)
    {
        clip._alpha = 40;
    }
    if (card.current)
    {
        clip._xscale = CURRENT_SCALE;
        clip._yscale = CURRENT_SCALE;
        clip._y = TOP - 4;
    }
    clip.onRelease = function()
    {
        fscommand("TB_Focus", String(this.id));
    };
    clip.onRollOver = function()
    {
        _root.bar.hover.text = this.fullName;
    };
    clip.onRollOut = function()
    {
        _root.bar.hover.text = "";
    };
    return clip;
}

function drawDivider(bar, round, index, x)
{
    var clip = bar.createEmptyMovieClip("divider" + index, 300 + index);
    clip._x = x;
    clip._y = TOP;
    clip.lineStyle(2, GOLD, 80);
    clip.moveTo(DIVIDER / 2, 0);
    clip.lineTo(DIVIDER / 2, CARD_H);
    var text = label(clip, "round", 1, 0, CARD_H + 2, DIVIDER, 18, 12, GOLD, "center", false);
    text.text = String(round);
}

function drawEndTurn(bar, x)
{
    var clip = bar.createEmptyMovieClip("endTurn", 500);
    clip._x = x;
    clip._y = TOP + 8;
    // The game's popups' gold-edged panel (PopupLayer_I70).
    art(clip, "PopupLayer_I70", 1, 0, 0, 58, 44);
    var text = label(clip, "caption", 2, 0, 11, 58, 24, 13, GOLD, "center", true);
    text.text = "End turn";
    clip.onRelease = function()
    {
        fscommand("TB_EndTurn", "");
    };
}

// The cards, the dividers and the button, laid out left to right and
// centred on the stage.
function build(state)
{
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
    if (state.playerTurn)
    {
        drawEndTurn(bar, x + GAP);
        x += 58 + 2 * GAP;
    }
    bar._x = Math.round((Stage.width - x) / 2);
    // A dark plate behind it all, edged in gold, so the bar reads over a
    // bright scene as the game's own panels do.
    var plate = bar.createEmptyMovieClip("plate", 5);
    box(plate, -12, TOP - 10, x + 24 - GAP, CARD_H + 58, INK, 60);
    plate.lineStyle(1, GOLD, 70);
    plate.moveTo(-12, TOP - 10);
    plate.lineTo(x + 12 - GAP, TOP - 10);
    plate.moveTo(-12, TOP + CARD_H + 48);
    plate.lineTo(x + 12 - GAP, TOP + CARD_H + 48);
    label(bar, "status", 6, 0, TOP + CARD_H + 16, x, 20, 13, PARCHMENT, "center", false);
    label(bar, "hover", 7, 0, TOP + CARD_H + 30, x, 20, 12, GOLD, "center", false);
}

// Whose turn, and on the player's what is left.
function describe(state)
{
    var line = "Round " + state.round;
    for (var i = 0; i < state.cards.length; i++)
    {
        if (state.cards[i].current)
        {
            line += "  -  " + state.cards[i].name;
            break;
        }
    }
    if (state.playerTurn)
    {
        line += "  -  " + (state.actions > 0 ? "action ready" : "action spent") + ", " + state.movementLeft + " of "
            + state.movementMax + " m to move";
    }
    return line;
}

// The bar is built again only when its cards change: a walk changes the
// meters left several times a second, and building again would load every
// portrait again with it. The status line is set in place.
function draw(view)
{
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
        // The cards and their images are gone: the plugin may release the
        // portraits it rendered for them (core/Portraits.h).
        if (inGame)
        {
            fscommand("TB_Cleared", "");
        }
        return;
    }
    var at = view.indexOf(CARD);
    var cards = (at < 0 ? "" : view.substr(at)) + (state.playerTurn ? "+" : "-") + state.round;
    if (cards != lastCards)
    {
        lastCards = cards;
        build(state);
    }
    _root.bar.status.text = describe(state);
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
};
draw(ask());
