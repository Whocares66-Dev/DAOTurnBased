// The turn-based rules as functions: what a command costs, how initiative
// is rolled, how far a combatant may move. dev/DESIGN.md has the reasons.
#pragma once

#include "core/Settings.h"
#include "core/Types.h"

#include <cstdint>
#include <span>

namespace tb
{

// What a command is worth in a turn.
enum class CommandKind : std::uint8_t
{
    Action,      // the one per turn: attack, ability, spell, using an object or item
    Move,        // spends movement, measured as the creature goes
    Free,        // costs nothing: switching weapon sets, drawing them, equipping, dropping, turning to face
    Pass,        // the AI choosing to wait: its turn ends
    Involuntary, // what happens to a creature, not what it chooses: an effect applying, a death
                 // blow's animation, a scripted jump, bark or conversation; always let through
};

// Where a command came from (src/game/Gate.cpp): the command's own
// player-issued flag, else who called the queue.
enum class Source : std::uint8_t
{
    Script, // a script's AddCommand: the AI, tactics, a controlled creature's auto-attack
    Player, // the player's order: clicked, chosen from the quickbar, steered
    Engine, // the engine's own queueing: effects, weapon draws, animations, jumps, SetActive
    Unknown,
};

// What a command is worth, by its type alone.
[[nodiscard]] CommandKind Classify(CommandType type);
// And by where it came from: a wait is an AI passing only from a script.
[[nodiscard]] CommandKind Classify(CommandType type, Source source);

[[nodiscard]] const char *Name(CommandType type);
[[nodiscard]] const char *Name(CommandKind kind);
[[nodiscard]] const char *Name(Source source);
[[nodiscard]] const char *Name(Side side);

// A small, fast, seedable generator (SplitMix64), so a fight replays the
// same in a test.
class Rng
{
  public:
    explicit Rng(std::uint64_t seed) : state_(seed)
    {
    }
    [[nodiscard]] std::uint64_t Next()
    {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // 1..sides.
    [[nodiscard]] int Roll(int sides)
    {
        return sides <= 1 ? 1 : static_cast<int>(Next() % static_cast<std::uint64_t>(sides)) + 1;
    }

  private:
    std::uint64_t state_;
};

// A roll of 1..die plus initiativePerDexterity for each point of Dexterity
// above 10 (rounded down; below 10 subtracts).
[[nodiscard]] int RollInitiative(Rng &rng, float dexterity, const Settings &settings);

// Meters a combatant may move in one turn. speedModifier is how fast it
// moves against a person (SpeedFactor below; Haste and slows would multiply
// it); anything not a sensible multiplier counts as 1.
[[nodiscard]] float MovementBudget(const Settings &settings, float speedModifier, float dexterity);

// A person's combat move speed, meters a second: the appearance table's
// Game_CbtSpeed for humans, elves, dwarves, genlocks and hurlocks (apr_base,
// read 2026-09-30).
inline constexpr float kPersonCombatSpeed = 4.66f;

// How fast a creature moves in combat against a person, from its
// appearance's Game_CbtSpeed: a mabari 1.07, a shriek 1.58, a dragon 1.46,
// an ogre 0.84. 1 when there is no speed to read. The scripts' movement
// speed property (20) is the flanking angle in the shipped properties
// table, 60 for everyone in play (2026-09-30), so it is not used.
[[nodiscard]] float SpeedFactor(float combatSpeed);

// A creature's movement rate effects taken together: Haste's 1.5, a
// slow's 0.5 (EFFECT_TYPE_MOVEMENT_RATE and _DEBUFF, their potency;
// EffectModifyMovementSpeed, effects_h.nss). Multiplied, as the engine is
// taken to; a potency that is no rate (0, negative, not a number) is left
// out. 1 with none.
[[nodiscard]] float MovementRate(std::span<const float> potencies);

// The ability table's TargetType is a mask (TARGET_TYPE_*, the scripts'
// constants): self 1, friendly 2, hostile 4, placeable 8, area 16, ground
// 32, body 64. UseType 2 is a mode (Shield Wall), turned on and off.
inline constexpr std::int32_t kTargetSelf = 1;
inline constexpr std::int32_t kUseTypeMode = 2;

// Whether a talent chosen on the quickbar in a fight asks the player for
// its target rather than going straight to the selected target or the
// caster's attack target (the quickbar's handler, read by DAOLib: a
// character keeps its attack target from turn to turn, so a talent asked
// or not by chance). Not one that can target only its user, nor a mode:
// the game uses those on the caster, and so do they here.
[[nodiscard]] bool AsksForTarget(std::int32_t targetType, std::int32_t useType);

// How far a creature strikes from, as the game's AI judges it
// (_AI_IsTargetInMeleeRange, ai_main_h_2.nss): AI_MELEE_RANGE, 3.5 m
// (core_h.nss), and for a big target half its personal space more (the
// appearance table's PerSpace, 1-2 for bears, drakes, the broodmother),
// or 12 m past 2 (a high dragon's 3). A ranged weapon reaches its range in
// the items table instead (Range: 45 m for bows, crossbows and staves; 0
// for every melee weapon). Read 2026-09-30 from the scripts and
// tools/gda.py's dumps of bitm_base and apr_base.
inline constexpr float kMeleeReach = 3.5f;
inline constexpr float kBigTargetReach = 12.0f;
[[nodiscard]] float Reach(float weaponRange, float targetPersonalSpace);

// Meters a creature must walk before it can strike a target this far away.
[[nodiscard]] float WalkToReach(float distance, float reach);

} // namespace tb
