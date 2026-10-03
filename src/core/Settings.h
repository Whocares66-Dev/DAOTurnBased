// The player's settings, turnbased.ini: parsed, checked and written back.
//
// The file lives beside the logs (Documents\BioWare\Dragon Age\daolib\),
// not in the game's folder, which is under Program Files and may not be
// writable. src/game reads it at load and writes it when the toggle key
// changes `enabled`; WithValue keeps the rest of the player's file as it
// was, comments included. dev/DESIGN.md says what each setting is for.
#pragma once

#include "core/Keys.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tb
{

// How far a combatant may move in a turn (core/Rules.h).
enum class MovementRule : std::uint8_t
{
    Constant, // everyone: meters
    Speed,    // meters times the creature's movement speed (haste, slows, ...)
};

// Who commands the party on its turns.
enum class PartyControl : std::uint8_t
{
    Manual,  // the player, every party member
    Tactics, // the player the hero; the companions run their tactics
};

// How an AI creature is told its turn has come (dev/DESIGN.md, "The AI's
// turn").
// What a turn is: one action and meters (as built), or a bar of time.
enum class TurnClock : std::uint8_t
{
    Actions,
    Time,
};

enum class AiPoke : std::uint8_t
{
    CommandComplete, // a COMMAND_COMPLETE event, as when a command ends: the creature's own script sees it first
    Kickstart,       // the scripts' EVENT_TYPE_DEBUG_KICKSTART_AI: rules_core runs AI_DetermineCombatRound
};

struct Settings
{
    // [turnbased]
    bool enabled = true;

    // [initiative]: a roll of 1..die, plus perDexterity for each point of
    // Dexterity above 10.
    int initiativeDie = 20;
    // Initiative a rank of Survival (dev/REBALANCE.md, "Initiative").
    float initiativePerSurvival = 2.0f;
    float initiativePerDexterity = 0.25f;

    // [movement]
    MovementRule movementRule = MovementRule::Speed;
    float movementMeters = 9.0f;
    float movementPerDexterity = 0.0f; // meters per point of Dexterity above 10
    float movementMin = 3.0f;
    float movementMax = 20.0f;

    // [turns]
    PartyControl partyControl = PartyControl::Manual;
    bool endTurnWhenActionUsed = true; // a party member's turn ends when its action is spent, movement or not
    float roundMinSeconds = 0.0f;      // a round lasts at least this much game time (0: as long as its turns)
    int settleMs = 400;                // the world runs on this long after an action ends
    int aiThinkMs = 1500;              // an AI creature that has chosen nothing by then passes
    int aiFollowUpMs = 500;            // after its action, how long an AI creature has to choose a move
    int maxActionMs = 12000;           // an action still running then is stopped
    int maxCommandsPerTurn = 6;        // commands an AI creature may start in one turn
    int orderRunMs = 0;                // an order that queued nothing while paused: run the world this long (0: off)
    int conditionTurns = 1;            // turns a stun costs, a pin's turns without movement (0: the game's timers)
    // An ability's cooldown in turns: its seconds over this (0: the game's timers).
    // 4: a hold's share of the time about what it is in real time (the user,
    // 2026-09-30: at 10 the archers' stun came back every other turn).
    float cooldownSecondsPerTurn = 4.0f;
    // The world holds still while the player chooses (otherwise it idles on).
    bool pauseOnPlayerTurn = false;
    // A talent chosen on the quickbar asks for its target, even with one
    // selected (otherwise the game uses it on the selected or attack target
    // at once); not one for its user alone, nor a mode (AsksForTarget).
    bool pickTargets = true;
    // The game's speed through a turn-based fight, everyone's turns alike
    // (the user, 2026-10-02: "a setting to change game speed, from 0.5x to
    // 1X to 2x"; then, having played: "default fight speed should be 1x,
    // make the options 0.5x, 1x, 2x, 3x"). The one setting for it: `ai_turn_speed`, which ran an AI's
    // turns faster than the player's, was there before and is gone (the
    // user, the same day: "shouldn't turn speed apply to everyone? we have
    // one setting for combat speed right?").
    float fightSpeed = 1.0f;
    // A party member may put its turn off to the round's end, before it has
    // spent anything of it (Delay: dev/REBALANCE.md).
    bool delay = true;
    // With time: each creature's effects, its regeneration and the scripts'
    // timers on it (a damage over time's ticks) run on its own clock, which
    // moves as its bar is spent and a turn's seconds a turn, as its
    // cooldowns do (dev/REBALANCE.md, "The creature's own clock"; the user,
    // 2026-10-02: "things on each creature's own clock seems like the most
    // important next thing"). Off: everyone's age together, a round's worth
    // as each round begins, as before DAOLib could move one creature's.
    bool ownClocks = true;
    // With time: a spell that conjures and does not fit is the engine's own
    // cast from the start, held in its conjure (DAOLib's HoldConjure) while
    // its time is unpaid, its caster drawing the spell in through the others'
    // turns, and the installed rules_core interrupts it as the game does,
    // shipped or mended (the user, 2026-10-02: "Hooking into the game's own
    // logic is the right thing to do", "Animating the character midcast is
    // also the right thing to do"). Off: the gate keeps it and gives it
    // again when its time is paid, and the mod's own copy of the game's rule
    // interrupts it.
    bool engineConjure = true;
    // With time, [costs]: what the things that are not fighting cost of the
    // bar, in seconds, read from the settings file rather than set here (the
    // user, 2026-10-02: doors, chests and pickups "cost 0.5s for now. we can
    // adjust later. maybe make it data driven so we can change a csv or yaml
    // (or gda) somewhere instead of buried in source code"). A door, a
    // chest, a lever used; a lock opened or locked; a thing picked up, taken
    // or handed over.
    float objectSeconds = 0.5f;
    // With time: an action that was over in a fraction of the time the
    // engine was read to take for it did not play, and its time is given
    // back (TurnMachine::OverAtOnce). A switch, should it ever give back
    // what did play.
    bool giveBackUnplayed = true;
    // Judge an attack's or a move's walk by the game's own path (DAOLib's
    // FindPath) rather than a straight line, and refuse a move it cannot
    // finish this turn (the user's call, 2026-10-01).
    bool pathCheck = true;
    // Effects' and regeneration's time in rounds: held through a fight, and
    // this many seconds spent on them at each new round (0: the game's clock).
    float effectSecondsPerRound = 6.0f;
    float combatRadius = 50.0f; // meters from the party leader a creature may fight from and take turns
    // Abilities that charge for so many turns whatever their conjuring time
    // (ability id, turns): the game's own slow and ritual spells charge by
    // the rule (TurnMachine::ChargeTurns), and the base game has none.
    std::vector<std::pair<std::int32_t, int>> charge;
    // Of those, only who the player can see fighting takes turns: in a room
    // the party has seen (the game draws the rest black), or attacking or
    // attacked by a party member. In Castle Cousland a fight took in nine
    // of Howe's men, most fighting guards rooms away, and the camera went
    // to black rooms (the user, 2026-09-30, who did not want a rule of
    // distance alone, and whose own rule this is). Off: everyone in combat
    // within the radius, as before.
    bool joinBySight = true;
    // And of those seen, one that neither attacks a party member nor is
    // attacked by one joins only this near to the party. In the open every
    // room in the radius is one the party has seen: in the Korcari Wilds a
    // hurlock, an archer and four wolves fighting each other 45 m off
    // joined a fight of four darkspawn, thirteen in the order and eighteen
    // rounds (issue 6, the playthrough; the user, 2026-10-03: "50/30 split
    // seems reasonable to me"). Who attacks the party joins from anywhere
    // in combatRadius, as an archer must.
    float joinRadius = 30.0f;
    AiPoke aiPoke = AiPoke::CommandComplete;

    // What a turn is (dev/REBALANCE.md, "Current specification"): a bar of
    // time that actions and walking both spend, each at its own length, or
    // one action and meters of movement, as the mod first played (`[turns]
    // clock = actions`). Time since it was seen in play (play tests 25 to
    // 28, 2026-10-02).
    TurnClock turnClock = TurnClock::Time;
    // With time: a turn's bar, in seconds (the user, 2026-10-02: "a turn is
    // four seconds of time"). It is also what a cooldown's and an effect's
    // seconds are counted in, where effect_seconds_per_round and
    // cooldown_seconds_per_turn are as built.
    float secondsPerTurn = 4.0f;
    // With time: meters a turn walked for nothing, for shuffling in a melee
    // ring (the user: "about a metre a turn is free").
    float freeStepMeters = 1.0f;
    // With time: a person's walk in a fight, meters a second (appearance
    // combat speed, read: 4.66), times the creature's movement speed, until
    // each creature's own is read from the game.
    float paceMps = 4.66f;
    // With time: Combat Tactics' rank an enemy fights at, by its creature
    // rank: critter, normal, lieutenant, boss, elite boss (the user: "it
    // makes bosses more dangerous"). A party member's is its own skill's.
    // The user, 2026-10-02: "lieutenant gets 0.5s more, boss gets 1s more,
    // elite boss 1.5s more": ranks 2, 4 and 6.
    std::array<int, 5> enemyTactics{0, 0, 2, 4, 6};

    // [feedback]
    bool floaties = true;          // the game's floating text: whose turn, out of movement, ...
    bool scriptEvents = true;      // signal the module the turn-based events (core/TurnMachine.h)
    bool cameraFollow = false;     // experimental: centre the camera on whoever's turn it is
    bool turnBar = false;          // show the turn order bar (dev/UI.md); off until it is seen in play
    bool portraits = true;         // with the bar: render portraits for combatants the game has none for
    bool movePreview = true;       // with the bar: draw the path to the cursor, gold in reach and red beyond
    bool movePreviewGround = true; // the path as marks on the ground, not a line over the scene (the user's choice)

    // [keys]
    // The toggle's key: none until the player binds one. It was Ctrl+T until
    // 2026-10-03; the user then: "I don't think we necessarily need one for
    // toggling turn-based on / off. There's already a checkbox in game
    // settings", and of the row with no key in the controls: "Yeah we can
    // keep it unbound I guess".
    KeyChord toggleKey{0, false, false, false}; // None
    // End turn's key. Not the pause key's: that pauses, as in the game.
    KeyChord endTurnKey{0x0D, false, false, false}; // Enter
    // Delay's key: none until the player binds one (the user, 2026-10-03:
    // "Delay should have no key binding by default").
    KeyChord delayKey{0, false, false, false}; // None
};

struct ParsedSettings
{
    Settings settings;
    std::vector<std::string> warnings; // a line each: unknown key, bad value (the default is kept)
};

// Reads an INI: [section], key = value, ';' or '#' starting a comment.
// Values out of range are clamped with a warning; unknown keys warned of.
[[nodiscard]] ParsedSettings ParseSettings(std::string_view ini);

// A whole file with every setting and a comment on each: what is written
// when there is none.
[[nodiscard]] std::string DefaultSettingsFile();

// The same text with one value replaced (or added under its section, or
// the section added at the end), everything else as it was.
[[nodiscard]] std::string WithValue(std::string_view ini, std::string_view section, std::string_view key,
                                    std::string_view value);

// The player's file with every setting it lacks added, at its default and
// with the comment that explains it, under its section (or the section
// added at the end): a file written by an older build shows the newer
// settings. Everything the player has is kept as it is.
[[nodiscard]] std::string WithMissingSettings(std::string_view ini);

} // namespace tb
