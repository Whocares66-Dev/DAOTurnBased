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

var CARD_W = 52;
var CARD_H = 64;
var GAP = 6;
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
        [256, 0, 100, 100, 0, 1, 1, 2, "Warden", "player", ""],
        [512, 2, 4, 80, 0, 0, 0, 2, "Hurlock Emissary", "e", ""],
        [257, 0, 0, 65, 0, 0, 1, 2, "Alistair", "gen00fl_alistair", ""],
        [513, 2, 2, 40, 0, 0, 0, 2, "Genlock", "g", ""],
        [258, 0, 0, 0, 1, 0, 1, 2, "Morrigan", "gen00fl_morrigan", ""],
        [768, 1, 2, 90, 0, 0, 0, 2, "Soldier", "s", ""],
        [514, 2, 3, 100, 0, 0, 0, 2, "Genlock Alpha", "a", ""],
        [256, 0, 100, 100, 0, 0, 1, 3, "Warden", "player", ""],
        [512, 2, 4, 80, 0, 0, 0, 3, "Hurlock Emissary", "e", ""],
        [257, 0, 0, 65, 0, 0, 1, 3, "Alistair", "gen00fl_alistair", ""]
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
// found as the game's party bar finds it (portraits.gfx:
// SharedLibrary.Portrait): the members are the data tree's
// Party.Members.0-3 and Party.Summons.0-3, each with its ID and its
// Portrait, an image's name.
function portraitOf(card)
{
    if (card.portrait.length > 0)
    {
        return card.portrait;
    }
    return card.side == 0 ? partyPortrait(card.id) : null;
}

// An image's URL as the game's own movies make it (portraits.gfx:
// MovieUtils.GetImageUrl): "img://", the texture's name with ".dds" added
// when it has none, and the size wanted. Without the extension the loader
// finds nothing and draws grey (seen in play, 2026-09-30).
function imageUrl(name, width, height)
{
    if (name.indexOf("img://") == 0)
    {
        name = name.substr(6);
    }
    if (name.lastIndexOf(".") == -1)
    {
        name += ".dds";
    }
    return "img://" + name + "?Width=" + width + "&Height=" + height;
}

var portraitNames = {};
function partyPortrait(id)
{
    if (!inGame)
    {
        return null;
    }
    if (portraitNames[id] != undefined)
    {
        return portraitNames[id];
    }
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
                    portraitNames[id] = String(name);
                    return portraitNames[id];
                }
            }
        }
    }
    return null;
}

function drawCard(bar, card, index, x)
{
    var clip = bar.createEmptyMovieClip("card" + index, 100 + index);
    clip._x = x;
    clip._y = TOP;
    clip.id = card.id;
    clip.fullName = card.name;
    var colour = SIDE_COLOURS[card.side];
    box(clip, 0, 0, CARD_W, CARD_H, INK, 85);
    box(clip, 3, 3, CARD_W - 6, CARD_H - 16, colour, 35);
    outline(clip, 0, 0, CARD_W, CARD_H, 2, colour);
    if (RANK_COLOURS[card.rank])
    {
        outline(clip, 3, 3, CARD_W - 6, CARD_H - 16, 1, RANK_COLOURS[card.rank]);
    }
    if (card.current)
    {
        outline(clip, -3, -3, CARD_W + 6, CARD_H + 6, 3, GOLD);
    }
    var portrait = inGame ? portraitOf(card) : null;
    if (portrait != null)
    {
        var picture = clip.createEmptyMovieClip("picture", 2);
        picture._x = 3;
        picture._y = 3;
        picture.loadMovie(imageUrl(portrait, CARD_W - 6, CARD_H - 16));
    }
    else
    {
        var text = label(clip, "initials", 1, 0, 12, CARD_W, 28, 18, PARCHMENT, "center", true);
        text.text = initials(card.name);
    }
    // Health, in the card's foot.
    box(clip, 4, CARD_H - 9, CARD_W - 8, 5, 0x000000, 100);
    var healthColour = card.health > 50 ? 0x4CB84C : card.health > 25 ? 0xE0B030 : 0xD03020;
    box(clip, 4, CARD_H - 9, (CARD_W - 8) * card.health / 100, 5, healthColour, 100);
    if (card.yours)
    {
        clip.beginFill(PARCHMENT, 100);
        clip.moveTo(CARD_W / 2 - 5, CARD_H + 9);
        clip.lineTo(CARD_W / 2 + 5, CARD_H + 9);
        clip.lineTo(CARD_W / 2, CARD_H + 3);
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
    box(clip, 0, 0, 58, 44, INK, 85);
    outline(clip, 0, 0, 58, 44, 2, GOLD);
    var text = label(clip, "caption", 1, 0, 11, 58, 24, 13, GOLD, "center", true);
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
