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

// OBJECT_INVALID, dao::kInvalidObject.
inline constexpr ObjectId kNoObject = 0x7F000000;

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

} // namespace tb
