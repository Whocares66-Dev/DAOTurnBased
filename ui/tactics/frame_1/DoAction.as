// The tactics page's box, "Use when turn-based" (dev/UI.md, "The tactics
// page's box"; issue 46), ActionScript 2 for the game's Scaleform GFx 3.
// Built into turnbased_tactics.swf by tools/movie.py.
//
// The user, 2026-10-05: "Should probably add a checkbox to the tactics page,
// 'use when turn-based', off by default, per character. Should be changeable
// even during battle, applies to character's next turn."
//
// The plugin loads this movie into a clip at the root of the game's tactics
// screen each time that screen opens (src/game/TacticsBox.cpp, DAOLib's
// dao::gui::LoadInto): this script's own clip is _root.TurnBasedTactics_mc,
// and its _root is the tactics movie's. So it can attach that movie's own
// symbols: the box is the screen's own toggle, "OnOffButton", the one each
// tactic's row has and the screen's AI switch is (its class the game's
// SharedLibrary.ToggleButton), attached at the screen's root as
// _root.TurnBased_mc, with a caption in a text field of its own beside it,
// _root.TurnBasedCaption_txt. Nothing of the screen's own is changed: the
// mod's elements stand beside the game's.
//
//   ExternalInterface.call("TurnBased_TacticsBox", "<id>")   what to show for the character the page is
//                                                            of: -1 no box (the hero), 0 not ticked, 1 ticked
//   fscommand("TurnBased_UseWhenTurnBased", "<id> <0|1>")    a click: for whom, and what the box shows now
//   fscommand("TurnBased_TacticsSays", "<text>")             a line for the plugin's log ("!" first: a warning)
//   Refresh()                                                the plugin calls it: ask again
//
// The character the page is of is the data tree's GUI.SelectedChar, which
// the whole screen is bound to: its ID is asked through the game's own
// GetValue, and its changing is heard through the game's own TokenManager,
// as the screen's own character selector hears it.
//
// Everything is guarded: where the screen is not as it was read, there is no
// box and the plugin's log says why; the game's screen is left as it is.
//
// Outside the game there is no tactics screen to stand on, and the movie
// draws a stand-in of that screen's bottom row, so that where the box goes
// can be looked at in any Flash player (tools/snapshot.ps1 -Movie ...).

// ---------------------------------------------------------------------------
// Where the box stands and how its caption looks: every number of it is here.
//
// Read from the game's tactics.gfx with FFDec, 2026-10-05 (a stage of 1024 by
// 768, its root's own places):
// - The AI switch, _root.AIOnOffButton_mc, is the symbol OnOffButton at 208.05,
//   627.05. That symbol's art is a box 31.9 square drawn to the LEFT of its own
//   origin, from -110 to -78.1 across and -4 to 27.9 down: on the stage, 98 to
//   130 across and 623 to 655 down.
// - The switch's own caption (the game's string LOC_DISABLE_AI_TACTICS) is to
//   the right of it: the title font at 16 px, 0xDFD3AA, left-aligned, its
//   letters from 131, 628, which is 1 right of the art and 5 below its top. Its
//   field runs to 491; how far its words run was not read.
// - Over that row a rule from 120 to 471: the AI switch begins 22 left of it.
//   The left page ends at 510; the right page's row is the Save preset button,
//   from 542 to 905.
// So the one free stretch of the AI switch's row is from the end of its
// caption's words to the right end of the row, and the box with its caption is
// put at that end: the caption's words end ROW_END from the AI switch's _x, as
// far right of the rule as the switch stands left of it (493), and the box
// stands before them, however long the caption comes out in the game's font.
//
// A guess made without seeing the screen, and seen in the game the same day
// (2026-10-05): the box at 303 to 335 with its caption's words to 493, in the
// screen's own art and lettering inside the page, the AI switch's caption
// ("Disable tactics" in English) ending at 234, 69 clear of it. It was left
// as it is. The movie measures the AI switch's own caption as it places the
// box and says in the plugin's log how far clear of the box it ends, or that
// it runs under it, as it may in another language. If it does, or the box
// belongs elsewhere, these are what to change (then python tools\movie.py, a
// build, and tools\deploy.ps1).
var ROW_END = 285;             // from the AI switch's _x to where the caption's words end: 493 on the stage
var ROW_DY = 0;                // from the AI switch's _y to the box's: 0 is the same row
var CAPTION_GAP = 1;           // from the box's art to its caption's words, as the AI switch's own caption stands
var CAPTION_DY = 5;            // from the art's top down to the words' line, as the AI switch's own
var CAPTION_SIZE = 16;         // the screen's own captions' size
var CAPTION_COLOUR = 0xDFD3AA; // and their colour
var CAPTION_TEXT = "Use when turn-based";
// The caption's font is the one a field of the screen's own has, asked of it
// as the movie runs (the character's name, _root.charSelect_mc.Name_mc). The
// name here is used only if that field is not there to ask: it is the font
// every field of the screen imports (tactics.gfx, the same reading).
var FALLBACK_FONT = "$TitleFont";
// ---------------------------------------------------------------------------

var BOX = "TurnBased_mc";             // the box, at the screen's root
var CAPTION = "TurnBasedCaption_txt"; // its caption, at the screen's root
var SYMBOL = "OnOffButton";           // the screen's own toggle, as its movie exports it
// This movie's own number for its observer of GUI.SelectedChar. The screen's
// own are 2303, 2308 and 2600 to 2609 (its _OBSERVER_ID), the game's table
// ends under 63000, the engine's own count down from 0xFFFFFFFF and DAOLib's
// begin at 0x40000000: an observer added with a number another has would take
// that one's place (the engine's 0x0051A4A0, read 2026-10-05).
var OBSERVER = 46046001;
var LOAD_FRAMES = 120; // how long an attached box is waited on to set itself up, in frames
var POLL_FRAMES = 15;  // how often the page's character is asked for, beside the observer

// Whether the game is there. In a Flash player on its own ExternalInterface is
// not available, or the player is told to show the stand-in (sample=1).
var inGame = flash.external.ExternalInterface.available && _root.sample != "1";

var host = this;     // this movie's clip: _root.TurnBasedTactics_mc in the game
var box = null;      // the box, once attached
var caption = null;  // its caption, once made
var ready = false;   // the box has set itself up and has its clicks
var gaveUp = false;  // there is no box to be had on this screen: said once, and left
var away = true;     // the box is put away: the screen is not set up, or is being opened again
var state = -1;      // what the plugin last answered: -1 no box, 0 not ticked, 1 ticked
var shownFor = "";   // whose page that answer was for: the id as the data tree gives it
var captionFont = ""; // the font the caption was given, for the log's line of where the box stands
var watching = false;
var waited = 0;
var ticks = 0;
var said = {};
// The stand-in's answer where there is no plugin: ticked, or by the player's
// parameter tick=0 not ticked, tick=none no box. (Not `state`: outside the
// game this movie's own clip is the root, and its variables are the root's.)
var sampleState = _root.tick == "0" ? 0 : (_root.tick == "none" ? -1 : 1);

// A line for the plugin's log, each once: where the box was put, or why
// there is none.
function say(text)
{
    if (said[text] == true)
    {
        return;
    }
    said[text] = true;
    trace("turnbased_tactics: " + text);
    if (inGame)
    {
        fscommand("TurnBased_TacticsSays", text);
    }
    else if (_root.StandIn_mc.Said_txt != undefined)
    {
        _root.StandIn_mc.Said_txt.text += text + "\n";
    }
}

// The character the page is of, as the data tree has it: "238717". Empty
// where it has none.
function selectedId()
{
    if (!inGame)
    {
        return "257";
    }
    var id = flash.external.ExternalInterface.call("GetValue", "GUI.SelectedChar.ID");
    return id == undefined || id == null ? "" : String(id);
}

// What the box shows for that character: the plugin's word, -1, 0 or 1.
// Anything else, and no answer at all, is no box.
function ask(id)
{
    if (!inGame)
    {
        return sampleState;
    }
    if (id == "")
    {
        return -1;
    }
    var answer = Number(flash.external.ExternalInterface.call("TurnBased_TacticsBox", id));
    return answer == 0 || answer == 1 ? answer : -1;
}

function shown(clip)
{
    return clip != undefined && clip != null && clip._visible == true;
}

// Shown or put away, as the game's own MovieUtils shows and hides a clip:
// what is not seen takes no click either.
function put(visible)
{
    if (box != null)
    {
        box._visible = visible;
        box.hitTestDisable = !visible;
    }
    if (caption != null)
    {
        caption._visible = visible;
    }
}

// The box as the plugin answered: ticked or not, or not there. Put away too
// while one of the screen's own pickers is open: the box is drawn over them,
// being attached after the screen was made, and would lie on an open list and
// take a click meant to close it.
function show()
{
    if (!ready)
    {
        return;
    }
    var picking = shown(_root.Overlay_mc) || shown(_root.PresetPicker_mc.PickerList_mc) ||
                  shown(_root.BehaviourPicker_mc.PickerList_mc);
    if (state < 0 || away || picking)
    {
        put(false);
        return;
    }
    if (state == 1)
    {
        box.Select();
    }
    else
    {
        box.Unselect();
    }
    put(true);
}

// Ask again, and show the answer. The plugin calls this when a choice
// changed from elsewhere (a click it made, its test tool, a setting) and each
// time the screen opens with this movie still in it; the observer of
// GUI.SelectedChar calls it when the page goes to another character.
function Refresh()
{
    shownFor = selectedId();
    state = ask(shownFor);
    show();
}

// A click. The box has flipped itself already (the game's ToggleButton), and
// `ticked` is what it shows now. The id sent is the one the box's state was
// asked for, so the plugin never guesses whom a click was for; and if the
// page has gone to another character since, the click is no choice at all:
// the box asks again and shows that one's.
function clicked(ticked)
{
    if (shownFor == "" || selectedId() != shownFor)
    {
        Refresh();
        return;
    }
    state = ticked ? 1 : 0;
    if (inGame)
    {
        fscommand("TurnBased_UseWhenTurnBased", shownFor + " " + (ticked ? "1" : "0"));
    }
    else
    {
        sampleState = state;
    }
}

// The page going to another character, heard as the screen's own character
// selector hears it (SharedLibrary.CharSelect: TokenManager's observer of
// GUI.SelectedChar). The engine takes observers added at any time: it reads
// TokenManager's list after each advance of the movie (0x0051ADA0, read
// 2026-10-05, and seen in play the same day: the page's going to another
// character was told).
function observe()
{
    if (watching || !inGame)
    {
        return;
    }
    if (TokenManager == undefined || typeof(TokenManager.AddTokenObserverWithID) != "function")
    {
        say("!the tactics screen has no TokenManager.AddTokenObserverWithID: the box follows the page to " +
            "another character by asking for it " + POLL_FRAMES + " frames apart only");
        watching = true;
        return;
    }
    TokenManager.AddTokenObserverWithID(OBSERVER, "GUI.SelectedChar", function()
    {
        host.Refresh();
    });
    watching = true;
}

// The caption: a text field of its own at the screen's root, in the font a
// field of the screen's own has and the size and colour of the screen's own
// captions. Made at the screen's root, not in this movie's clip, so that the
// font is found as the screen's own fields find it.
function makeCaption()
{
    _root.createTextField(CAPTION, _root.getNextHighestDepth(), 0, 0, 10, 10);
    var field = _root[CAPTION];
    if (field == undefined || field == null)
    {
        return null;
    }
    var font = undefined;
    var model = _root.charSelect_mc.Name_mc.TextField;
    if (model != undefined && typeof(model.getTextFormat) == "function")
    {
        var theirs = model.getTextFormat();
        if (theirs != undefined && theirs != null)
        {
            font = theirs.font;
        }
    }
    if (font == undefined || font == null || font == "")
    {
        font = FALLBACK_FONT;
        if (inGame)
        {
            say("the screen's name field (charSelect_mc.Name_mc.TextField) gave no font: the caption is in " +
                FALLBACK_FONT);
        }
    }
    captionFont = inGame ? font : "Georgia";
    var format = new TextFormat();
    format.font = captionFont;
    format.size = CAPTION_SIZE;
    format.color = CAPTION_COLOUR;
    format.align = "left";
    field.selectable = false;
    field.embedFonts = inGame;
    field.autoSize = "left";
    field.setNewTextFormat(format);
    field.text = CAPTION_TEXT;
    field._visible = false;
    return field;
}

// The box and its caption made, both put away until the plugin has said what
// to show. False where the screen's movie will not give one, with why said.
function make()
{
    if (_root[BOX] != undefined || _root[CAPTION] != undefined)
    {
        say("!the tactics screen has a " + BOX + " or a " + CAPTION + " already, not this movie's: no box");
        return false;
    }
    var made = inGame ? _root.attachMovie(SYMBOL, BOX, _root.getNextHighestDepth()) : standInBox();
    if (made == undefined || made == null)
    {
        say("!the tactics screen's movie attached no " + SYMBOL + ": no box");
        return false;
    }
    made._visible = false;
    made.hitTestDisable = true;
    box = made;
    caption = makeCaption();
    if (caption == null)
    {
        say("!no text field could be made at the tactics screen's root for the caption: no box");
        box.removeMovieClip();
        box = null;
        return false;
    }
    return true;
}

// What was made is let go, whatever is left of it taken off the screen's
// root, for both to be made again.
function forget()
{
    if (_root[BOX] != undefined)
    {
        _root[BOX].removeMovieClip();
    }
    if (_root[CAPTION] != undefined)
    {
        _root[CAPTION].removeTextField();
    }
    box = null;
    caption = null;
    ready = false;
    waited = 0;
    away = true;
}

// The box beside the AI switch, on its row (the numbers at the top). By each
// clip's own bounds on the stage, since the symbol's art is not at its
// origin.
function place(ai)
{
    var theirs = ai.getBounds(_root);
    var artWidth = theirs.xMax - theirs.xMin;
    var artLeft = ai._x + ROW_END - caption.textWidth - CAPTION_GAP - artWidth;
    box._x = ai._x + (artLeft - theirs.xMin);
    box._y = ai._y + ROW_DY;
    var ours = box.getBounds(_root);
    // A text field's words begin 2 inside it.
    caption._x = ours.xMax + CAPTION_GAP - 2;
    caption._y = ours.yMin + CAPTION_DY - 2;
    return ours;
}

// Where a clip's words end on the stage, if it is a caption of the screen's
// own standing by the AI switch on its row: one of the game's text elements
// (SharedLibrary.TextField, a clip with a field named TextField in it) that
// begins at the right edge of the switch's art. -1 for anything else.
function wordsEnd(clip, art)
{
    var field = clip.TextField;
    if (field == undefined || field == null || typeof(field.text) != "string" || field.text == "")
    {
        return -1;
    }
    var theirs = clip.getBounds(_root);
    if (theirs.xMin < art.xMax - 6 || theirs.xMin > art.xMax + 30 || theirs.yMin < art.yMin - 6 ||
        theirs.yMin > art.yMax - 8)
    {
        return -1;
    }
    return theirs.xMin + 2 + field.textWidth;
}

// How far the AI switch's own caption runs. It is a clip with no name of its
// own, in another with none, so it is looked for among the root's clips and
// theirs. -1 if it is not found.
function theirCaptionEnd(art)
{
    var end = -1;
    for (var a in _root)
    {
        var first = _root[a];
        if (typeof(first) != "movieclip" || first == host || first == box || first._parent != _root)
        {
            continue;
        }
        end = Math.max(end, wordsEnd(first, art));
        for (var b in first)
        {
            var second = first[b];
            if (typeof(second) == "movieclip" && second._parent == first)
            {
                end = Math.max(end, wordsEnd(second, art));
            }
        }
    }
    return end;
}

// The log's line of where the box was put, with whether the AI switch's own
// caption is clear of it: nobody who places it can see the screen, and
// whoever reads the log should not have to measure a picture.
function sayPlace(ai, at)
{
    var art = ai.getBounds(_root);
    var where = "the box " + BOX + " stands at " + Math.round(at.xMin) + ", " + Math.round(at.yMin) + " to " +
                Math.round(at.xMax) + ", " + Math.round(at.yMax) + " with its caption " + CAPTION + " to " +
                Math.round(caption._x + 2 + caption.textWidth) + " in the font " + captionFont +
                ", on the row of the AI switch at " + Math.round(art.xMin) + ", " + Math.round(art.yMin);
    var theirs = theirCaptionEnd(art);
    if (theirs < 0)
    {
        say(where + "; the AI switch's own caption was not found to measure: whether it runs under the box is " +
            "for the eye");
    }
    else if (theirs + 4 > at.xMin)
    {
        say("!" + where + "; the AI switch's own caption runs to " + Math.round(theirs) +
            ", UNDER the box: it wants another place (ui/tactics, ROW_END)");
    }
    else
    {
        say(where + "; the AI switch's own caption ends at " + Math.round(theirs) + ", " +
            Math.round(at.xMin - theirs) + " clear of the box");
    }
}

// Every frame. The screen sets its own clips up after this movie's first
// frame (the AI switch comes in the fourteenth frame of the root's timeline,
// clear, and is whole by the twenty-first; it goes in the thirty-first, and
// is a new clip if a screen opened again with its movie kept plays its
// timeline again), so the box waits for the AI switch, stands by it, fades
// in with it, and is put away without it.
function follow()
{
    var ai = _root.AIOnOffButton_mc;
    if (ai == undefined || ai == null || ai.m_bLoaded != true)
    {
        if (!away)
        {
            away = true;
            put(false);
        }
        return;
    }
    if (gaveUp)
    {
        return;
    }
    if (box != null && (_root[BOX] == undefined || _root[CAPTION] == undefined))
    {
        // The screen's movie has let one of them go (it was begun again, and
        // did not keep what was attached to it): both are made afresh.
        forget();
    }
    if (box == null && !make())
    {
        gaveUp = true;
        return;
    }
    if (!ready)
    {
        // An element of the game's sets itself up as it loads (GUI.Element:
        // its behaviours, Select and Unselect among them, are put on it
        // then), a frame or so after it is attached.
        if (box.m_bLoaded != true || typeof(box.Select) != "function" || typeof(box.Unselect) != "function")
        {
            waited++;
            if (waited > LOAD_FRAMES)
            {
                gaveUp = true;
                put(false);
                say("!the " + SYMBOL + " attached as " + BOX + " did not set itself up in " + LOAD_FRAMES +
                    " frames (no Select on it): no box");
            }
            return;
        }
        // As the screen gives its own switch its two actions: the first for
        // a click that ticks, the second for one that unticks.
        box.OnButtonAction = function()
        {
            clicked(true);
        };
        box.OnSecondaryButtonAction = function()
        {
            clicked(false);
        };
        ready = true;
        observe();
    }
    if (away)
    {
        // The screen is set up, now or again: stand by its AI switch, and
        // ask whose page it is.
        away = false;
        var at = place(ai);
        Refresh();
        sayPlace(ai, at);
    }
    // The page's opening fades its clips in; the box comes with the switch.
    if (box._alpha != ai._alpha)
    {
        box._alpha = ai._alpha;
        caption._alpha = ai._alpha;
    }
    // A picker opened or closed; and, beside the observer, the page gone to
    // another character.
    ticks++;
    if (ticks % POLL_FRAMES == 0 && selectedId() != shownFor)
    {
        Refresh();
    }
    else
    {
        show();
    }
}

// ---------------------------------------------------------------------------
// Outside the game: a stand-in for what of the tactics screen this script
// touches, drawn where tactics.gfx has it, so that the box's place can be
// looked at. None of it runs in the game.

function rect(clip, x, y, w, h, colour, alpha)
{
    clip.beginFill(colour, alpha);
    clip.moveTo(x, y);
    clip.lineTo(x + w, y);
    clip.lineTo(x + w, y + h);
    clip.lineTo(x, y + h);
    clip.lineTo(x, y);
    clip.endFill();
}

function standInWords(clip, name, x, y, text, colour)
{
    clip.createTextField(name, clip.getNextHighestDepth(), x, y, 10, 10);
    var field = clip[name];
    var format = new TextFormat();
    format.font = "Georgia";
    format.size = CAPTION_SIZE;
    format.color = colour;
    field.selectable = false;
    field.autoSize = "left";
    field.setNewTextFormat(format);
    field.text = text;
}

// The screen's bottom row: the two pages, the rules over the row, the AI
// switch with its caption, and the Save preset button. What the box's place
// was found to be is written under them, as the plugin's log has it in the
// game.
function standIn()
{
    var page = _root.createEmptyMovieClip("StandIn_mc", _root.getNextHighestDepth());
    rect(page, 0, 0, 1024, 768, 0x0C0A08, 100);
    rect(page, 87, 79, 423, 578, 0x2B2118, 100);
    rect(page, 526, 80, 408, 592, 0x2B2118, 100);
    rect(page, 120, 601, 351, 12, 0x6A5636, 100);
    rect(page, 545, 601, 351, 12, 0x6A5636, 100);
    rect(page, 542, 616, 363, 45, 0x4A3826, 100);
    standInWords(page, "Save_txt", 660, 628, "Save preset", 0xCEB874);
    // The AI switch's own caption, as the game has it: a clip two down from
    // the root with its field named TextField. Its words are a guess (the
    // parameter `theirs` gives others, to see a longer one run under the box).
    var theirs = page.createEmptyMovieClip("AiCaption_mc", page.getNextHighestDepth());
    theirs._x = 129;
    theirs._y = 626;
    standInWords(theirs, "TextField", 0, 0, _root.theirs == undefined ? "Disable tactics" : _root.theirs,
                 CAPTION_COLOUR);
    page.createTextField("Said_txt", page.getNextHighestDepth(), 87, 700, 850, 60);
    page.Said_txt.multiline = true;
    page.Said_txt.wordWrap = true;
    page.Said_txt.selectable = false;
    page.Said_txt.textColor = 0x9A9080;
    page.Said_txt.text = "";
    var ai = _root.createEmptyMovieClip("AIOnOffButton_mc", _root.getNextHighestDepth());
    rect(ai, -110, -4, 31.9, 31.9, 0x8C7A52, 100);
    rect(ai, -106, 0, 23.9, 23.9, 0x1A140E, 100);
    ai._x = 208.05;
    ai._y = 627.05;
    ai.m_bLoaded = true;
}

// A box with what this script uses of the game's toggle: Select, Unselect,
// and a click that flips it and calls one of its two actions.
function standInBox()
{
    var made = _root.createEmptyMovieClip(BOX, _root.getNextHighestDepth());
    rect(made, -110, -4, 31.9, 31.9, 0x8C7A52, 100);
    rect(made, -106, 0, 23.9, 23.9, 0x1A140E, 100);
    var tick = made.createEmptyMovieClip("Down_mc", 1);
    rect(tick, -102, 4, 15.9, 15.9, 0xF0C850, 100);
    tick._visible = false;
    made.selected = false;
    made.Select = function()
    {
        this.selected = true;
        this.Down_mc._visible = true;
    };
    made.Unselect = function()
    {
        this.selected = false;
        this.Down_mc._visible = false;
    };
    made.onRelease = function()
    {
        if (this.selected)
        {
            this.Unselect();
            this.OnSecondaryButtonAction();
        }
        else
        {
            this.Select();
            this.OnButtonAction();
        }
    };
    made.m_bLoaded = true;
    return made;
}

if (!inGame)
{
    standIn();
}
// The first word to the plugin: that the movie was loaded and its script
// runs, and where. What comes after waits on the screen.
say("its script runs in " + host + " and waits for the screen's AI switch, _root.AIOnOffButton_mc");
// On this movie's own clip, never the screen's root: the root's handlers are
// the game's.
this.onEnterFrame = function()
{
    follow();
};
