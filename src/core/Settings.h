// The player's settings, turnbased.ini: parsed, checked and written back.
//
// The file lives beside the logs (Documents\BioWare\Dragon Age\daolib\),
// not in the game's folder, which is under Program Files and may not be
// writable. src/game reads it at load and writes it when the toggle key
// changes `enabled`; WithValue keeps the rest of the player's file as it
// was, comments included.
//
// Seven settings are the file's: whether fights are in turns, who plays the
// companions, the fight's speed, the turn camera and three keys. The rest of
// the struct is the rules' numbers, built in and in no file (the user,
// 2026-10-10, of a file of 43: "strip it down to only essential configs.
// stuff like initiative are built-in, don't need to be configured"; issue
// 104). They are fields and not constants so that a test can play at round
// numbers (a pace of 5 m a second).
#pragma once

#include "core/Keys.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tb
{

// Who commands the party on its turns: what every companion's turns start
// as. A choice made for one companion in the playthrough wins over it, either
// way, and the hero's turns are the player's whatever it says
// (core/PartyTactics.h; issue 46).
enum class PartyControl : std::uint8_t
{
    Manual,  // the player, every party member
    Tactics, // the player the hero; the companions run their tactics
};

struct Settings
{
    // ---- the player's: turnbased.ini --------------------------------------

    // [turnbased] enabled.
    bool enabled = true;
    // [turns] party.
    PartyControl partyControl = PartyControl::Manual;
    // [turns] speed: the game's speed through a turn-based fight, everyone's
    // turns alike (the user, 2026-10-02: "a setting to change game speed,
    // from 0.5x to 1X to 2x"; then, having played: "default fight speed
    // should be 1x, make the options 0.5x, 1x, 2x, 3x").
    float fightSpeed = 1.0f;
    // [feedback] camera_follow: the camera goes to whoever's turn it is.
    bool cameraFollow = true;
    // [keys] toggle: none until the player binds one. It was Ctrl+T until
    // 2026-10-03; the user then: "I don't think we necessarily need one for
    // toggling turn-based on / off. There's already a checkbox in game
    // settings", and of the row with no key in the controls: "Yeah we can
    // keep it unbound I guess".
    KeyChord toggleKey{0, false, false, false}; // None
    // [keys] end_turn. Not the pause key's: that pauses, as in the game.
    KeyChord endTurnKey{0x0D, false, false, false}; // Enter
    // [keys] delay: none until the player binds one (the user, 2026-10-03:
    // "Delay should have no key binding by default").
    KeyChord delayKey{0, false, false, false}; // None

    // ---- the rules' numbers: built in, in no file -------------------------

    // Initiative: a roll of 1..die, plus perDexterity for each point of
    // Dexterity above 10 and perSurvival a rank of Survival
    // (dev/REBALANCE.md, "Initiative").
    int initiativeDie = 20;
    float initiativePerSurvival = 2.0f;
    float initiativePerDexterity = 0.25f;

    // What a turn is (dev/REBALANCE.md, "Current specification"): a bar of
    // time that actions and walking both spend, each at its own length, this
    // many seconds long (the user, 2026-10-02: "a turn is four seconds of
    // time"). It is also what a cooldown's and an effect's seconds are
    // counted in: a turn of a creature's is this much of its own clock, a
    // round this much of the shared one.
    float secondsPerTurn = 4.0f;
    // Meters a turn walked for nothing, for shuffling in a melee ring (the
    // user: "about a metre a turn is free").
    float freeStepMeters = 1.0f;
    // A person's walk in a fight, meters a second (appearance combat speed,
    // read: 4.66), times the creature's own pace (core/Rules.h, Pace).
    float paceMps = 4.66f;
    // Combat Tactics' rank an enemy fights at, by its creature
    // rank: critter, normal, lieutenant, boss, elite boss (the user: "it
    // makes bosses more dangerous"). A party member's is its own skill's.
    // The user, 2026-10-02: "lieutenant gets 0.5s more, boss gets 1s more,
    // elite boss 1.5s more": ranks 2, 4 and 6.
    std::array<int, 5> enemyTactics{0, 0, 2, 4, 6};
    // What a thing that is not fighting costs of the bar, in seconds: a
    // door, a chest, a lever used; a lock opened or locked; a thing picked
    // up, taken or handed over (the user, 2026-10-02: "cost 0.5s for now").
    float objectSeconds = 0.5f;

    int settleMs = 400;      // the world runs on this long after an action ends
    int aiThinkMs = 1500;    // an AI creature that has chosen nothing by then passes
    int aiFollowUpMs = 500;  // after its action, how long an AI creature has to choose a move
    int maxActionMs = 12000; // on a turn an AI plays, an action still running then is stopped (issue 51)
    int maxTurnMs = 30000;   // a turn its AI plays still going then is ended (issue 38)

    // Meters from the party within which a creature that is fighting is
    // taken into the turns: from the nearest of its living members, not from
    // its leader alone (issue 42; core/Rules.h, WithinReach). Not a way out
    // of them since 2026-10-04 (issue 29): one in the fight stays in it
    // beyond this while the engine's perception keeps it there (core/Rules.h,
    // InTheRotation), as the game itself ends combat by perception and not
    // by a distance.
    float combatRadius = 50.0f;
    // Of those, only who the player can see fighting takes turns: in a room
    // the party has seen (the game draws the rest black), or attacking or
    // attacked by a party member. In Castle Cousland a fight took in nine
    // of Howe's men, most fighting guards rooms away, and the camera went
    // to black rooms (the user, 2026-09-30, who did not want a rule of
    // distance alone, and whose own rule this is).
    // And of those seen, one that neither attacks a party member nor is
    // attacked by one joins only this near to the party. In the open every
    // room in the radius is one the party has seen: in the Korcari Wilds a
    // hurlock, an archer and four wolves fighting each other 45 m off
    // joined a fight of four darkspawn, thirteen in the order and eighteen
    // rounds (issue 6, the playthrough; the user, 2026-10-03: "50/30 split
    // seems reasonable to me"). Who attacks the party joins from anywhere
    // in combatRadius, as an archer must.
    float joinRadius = 30.0f;

    // An action that was over in a fraction of the time the engine was read
    // to take for it did not play, and its time is given back
    // (TurnMachine::OverAtOnce). Off only in the scripted tests' world, whose
    // actions are ended the moment they are begun to get on with the case.
    bool giveBackUnplayed = true;
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
