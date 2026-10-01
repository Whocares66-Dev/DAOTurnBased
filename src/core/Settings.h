// The player's settings, turnbased.ini: parsed, checked and written back.
//
// The file lives beside the logs (Documents\BioWare\Dragon Age\daolib\),
// not in the game's folder, which is under Program Files and may not be
// writable. src/game reads it at load and writes it when the toggle key
// changes `enabled`; WithValue keeps the rest of the player's file as it
// was, comments included. dev/DESIGN.md says what each setting is for.
#pragma once

#include "core/Keys.h"

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
    // The game's speed during an AI's turn (1: as ever; 2: twice as fast).
    float aiTurnSpeed = 1.0f;
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
    AiPoke aiPoke = AiPoke::CommandComplete;

    // [feedback]
    bool floaties = true;           // the game's floating text: whose turn, out of movement, ...
    bool scriptEvents = true;       // signal the module the turn-based events (core/TurnMachine.h)
    bool cameraFollow = false;      // experimental: centre the camera on whoever's turn it is
    bool turnBar = false;           // show the turn order bar (dev/UI.md); off until it is seen in play
    bool portraits = true;          // with the bar: render portraits for combatants the game has none for
    bool movePreview = true;        // with the bar: draw the path to the cursor, gold in reach and red beyond
    bool movePreviewGround = false; // the path as marks on the ground, not a line over the scene

    // [keys]
    KeyChord toggleKey{0x54, true, false, false}; // Ctrl+T
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
