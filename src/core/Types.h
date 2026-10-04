// The vocabulary the turn-based rules speak: object ids, positions, sides,
// the engine's command types and game modes, as plain values.
//
// Nothing under src/core touches the game, Windows or DAOLib. src/game
// translates DAOLib's types into these; its static_asserts keep the numbers
// here equal to the ones DAOLib generated from the game's script.ldf.
#pragma once

#include <cmath>
#include <cstdint>

namespace tb
{

// An engine object id, as the scripts pass it (dao::ObjectId).
using ObjectId = std::uint32_t;

// OBJECT_INVALID, dao::kInvalidObject: 0xFFFFFFFF in DAO. It was taken as
// NWN's 0x7F000000 until DAOLib 703bc92 (2026-09-30): the generated script
// calls' default "any creator" then named no object, and ending a pin
// removed nothing (the desktop play test's dog).
inline constexpr ObjectId kNoObject = 0xFFFFFFFF;

struct Vec3
{
    float x = 0;
    float y = 0;
    float z = 0;
};

[[nodiscard]] inline float Distance(const Vec3 &a, const Vec3 &b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Whose side a combatant fights on, from the party's point of view: the
// party (the player's to command), allies of the party that are not in it
// (soldiers at Ostagar, say: the AI's), and enemies (the AI's).
enum class Side : std::uint8_t
{
    Party,
    Ally,
    Enemy,
};

// What the engine's perception says of a creature that is not the party's,
// as the game layer reads it (src/game/Bridge.cpp: the scripts' IsPerceiving,
// asked either way of each party member, and IsPerceivingHostiles). The
// engine keeps for each creature a list of those it perceives, and the game's
// own rules go by it: a creature's AI chooses among the hostiles it
// perceives, and the party's combat ends when it perceives none
// (combat_h.nss, Combat_HandleCreatureDisappear). The mod asks, and keeps no
// figure of its own for what a creature has noticed (issue 29). Everything
// true where nothing was read: the party's own, and a test that says nothing.
struct Perception
{
    bool byParty = true; // a party member perceives it
    bool ofParty = true; // it perceives a party member
    bool ofFoes = true;  // it perceives someone hostile to it, of the party or not
    // And the game has it in combat (its combat state), which is the game's
    // own word for its having noticed an enemy: the scripts set it as a
    // creature perceives a hostile, drop it when it perceives none, and run a
    // creature's combat AI on a command's end only while it is set
    // (rules_core.nss, EVENT_TYPE_PERCEPTION_APPEAR and
    // EVENT_TYPE_COMMAND_COMPLETE, read 2026-10-04).
    bool inCombat = true;
};

// The game's modes, the scripts' GM_* (dao::GameMode).
enum class Mode : std::int32_t
{
    Unknown = -1,
    Explore = 0,
    Combat = 1,
    Dead = 2,
    Gui = 3,
    Dialog = 4,
    Cutscene = 5,
    FlyCam = 6,
    Fixed = 7,
    Pregame = 8,
    Loading = 9,
    Movie = 10,
    CharGen = 11,
    PartyPicker = 12,
};

// Every command type the game has: the commands table (commands.gda), as
// DAOLib names them (dao/core/CommandWord.h, 2026-09-30), the numbers in
// bits 16-23 of a command's first word. src/game/Bridge.cpp checks each
// against DAOLib's. script.ldf calls 22 RECOVER; the table and the command
// that makes it say a move through several points.
enum class CommandType : std::int32_t
{
    Invalid = 0,
    Attack = 1,
    Appear = 2,
    Disappear = 3,
    ForceFollowObject = 4,
    RandomWalk = 5,
    DoFunction = 6,
    GiveItem = 7,
    TakeItem = 8,
    JumpToObject = 9,
    JumpToLocation = 10,
    SpeakString = 11,
    SpeakStrref = 12,
    Wait = 13,
    PlayAnimation = 14,
    PlaySound = 15,
    MoveAwayFromObject = 16,
    MoveAwayFromPoint = 17,
    StartConversation = 18,
    Examine = 19,
    MoveToLocation = 20,
    MoveToObject = 21,
    MoveToMultipleLocations = 22,
    Lock = 23,
    Unlock = 24,
    UseAbility = 25,
    UseItem = 26,
    DropItem = 27,
    PickupItem = 28,
    EquipItem = 29,
    UnequipItem = 30,
    UseObject = 31,
    ExecuteEffect = 32, // the engine's, as an effect applies
    Drive = 33,         // the player steering the character
    SellItem = 34,
    BuyItem = 35,
    DoEvent = 36,
    Interact = 37,
    Deathblow = 38,
    SheatheWeapons = 39, // sheathe or draw
    SwitchWeaponSets = 40,
    Turn = 41, // turn to face
    Fly = 42,
};

// The highest number the table has.
inline constexpr std::int32_t kLastCommandType = 42;

// Why the engine itself has no attack to make for a pair as they stand: its
// own answer, asked before the command is let through (src/game/Gate.cpp,
// WalkToTarget, of DAOLib's dao::attack::ApproachOf and Walk; issue 27,
// 2026-10-04). The user's rule: where the engine decides, ask it and act on
// its answer, with no figure of the mod's in its place. Until then two of
// these were passed over with a guess, the attack let through, failed by the
// engine and given back. It is the answer for where the two stood when it
// was asked, and each attack is asked anew: nothing of it is remembered.
enum class NoAttack : std::uint8_t
{
    None,    // it has one, or was not asked
    NoPlace, // no room on the target's melee ring: none as it stands, or none once filled and no blow at once
    NoPath,  // out of reach, and the engine's search finds no path to where its approach goes
    NoLine,  // a shot out of range or sight that the engine would give up on the way: no line to be had
};

} // namespace tb
