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
    Action,      // the one per turn, a combat action: attack, ability, spell, using an item
    Move,        // spends movement, measured as the creature goes
    Free,        // costs nothing: a door, a chest, a lock, a thing picked up; one's own gear; turning to face
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
// above 10 (rounded down; below 10 subtracts), and initiativePerSurvival a
// rank of Survival.
[[nodiscard]] int RollInitiative(Rng &rng, float dexterity, const Settings &settings, int survival = 0);

// Meters a combatant may move in one turn. speedModifier is how fast it
// moves against a person (SpeedFactor below; Haste and slows would multiply
// it); anything not a sensible multiplier counts as 1.
[[nodiscard]] float MovementBudget(const Settings &settings, float speedModifier, float dexterity);

// A person's combat move speed, meters a second: the appearance table's
// Game_CbtSpeed for humans, elves, dwarves, genlocks and hurlocks (apr_base,
// read 2026-09-30).
inline constexpr float kPersonCombatSpeed = 4.66f;

// How fast a creature moves in combat against a person, from its
// appearance's Game_CbtSpeed: a wild mabari 1.07, the party's dog 1.50 (an
// appearance of its own, 6.99 m/s, seen in play 2026-09-30), a shriek 1.58,
// a dragon 1.46, an ogre 0.84. 1 when there is no speed to read. The scripts' movement
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

// How far apart, centre to centre, a creature's walk to strike ends: the
// engine's, read in the exe by DAOLib (2026-10-02, its dev/TURN_BASED.md,
// "How close a melee attack strikes from"). In melee, the attacker's
// personal space and the target's (the appearance table's PerSpace: a
// person 0.35, a child 0.30, a giant spider 0.70) and 1.2 m, the
// creature's in-reach test (ID 14499); the swing's set-up stands the
// attacker there with 0.15 m of play (ID 17720), and the walk ends that
// much further out. Lyna on a giant spider: 0.35 + 0.70 + 1.2 + 0.15 =
// 2.4 m, as play test 23 measured; two people, 2.05 m. A ranged weapon
// reaches its Range (bitm_base: 45 m for bows, crossbows and staves; 0 for
// every melee weapon, which so adds nothing), at most 60 m, and both
// personal spaces. Until then this was the AI's judgement of a melee
// target, AI_MELEE_RANGE's 3.5 m, and the game walked a metre further
// than the gate and the preview said.
inline constexpr float kMeleeGap = 1.2f;
inline constexpr float kSwingPlay = 0.15f;
inline constexpr float kLongestRange = 60.0f;
[[nodiscard]] float Reach(float weaponRange, float attackerSpace, float targetSpace);

// Meters a creature must walk before it can strike a target this far away.
[[nodiscard]] float WalkToReach(float distance, float reach);

// How long things take, with a turn a bar of time (Settings::turnClock,
// dev/REBALANCE.md, "Current specification": every action costs its own
// time).
//
// A melee swing is the game's own formula (CalculateAttackTiming, core_h.nss,
// read 2026-10-02): a base by weapon style (BASE_TIMING_*: 2.0 s one weapon,
// with a shield or none, 1.5 s dual, 2.5 s two-handed) plus the weapon's
// dspeed (itemstats: a dagger -0.5, a longsword -0.1, a maul 0.05), times the
// attack speed modifier (creature property 31, at rest 1.0; Haste -0.25),
// clamped to 0.5 to 1.5 and taken as 1.0 below 0.5. A bow's and a creature's
// that is not a humanoid's are their animations', 0 here: a stand-in is used.
enum class WeaponStyle : std::uint8_t
{
    None,      // a humanoid unarmed: timed as one weapon
    Single,    // one weapon, with a shield or not
    Dual,      // a weapon in each hand
    TwoHanded, // a greatsword, a maul, a battleaxe
    Ranged,    // a bow, a crossbow, a staff: its animation's
};
[[nodiscard]] float SwingSeconds(WeaponStyle style, float weaponSpeed, float attackSpeedModifier);

// A spell's conjuring before its cast (dev/REBALANCE.md, "Conjuring time",
// read from the executable and the animation files 2026-10-01): its start
// animation and loops of its loop animation, as many as the conjuring
// speed's count (spellconjuringspeeds: 0 instant, 1 fast, 2 normal, 4
// slow, 6 ritual); nothing without a conjure animation (ConjureAnim), as
// most talents with a Speed have none. The two the game's spells use are
// known (245 builds up, 244 draws in); another stands in at 0.6 s and 3 s.
[[nodiscard]] float ConjureSeconds(std::int32_t conjureAnim, int loops, bool staff);

// Until the engine gives animations' lengths, by kind (dev/REBALANCE.md,
// "What no rule reaches"): a swing that is not a formula's (the play tests'
// median attack was 1.74 s), a talent's or a spell's cast, a potion's drink
// (1.23 s, its animation).
inline constexpr float kStandInSwingSeconds = 2.0f;
inline constexpr float kStandInAbilitySeconds = 2.0f;
inline constexpr float kDrinkSeconds = 1.23f;

// The ability table's AbilityType (ABILITY_TYPE_*, 2da_constants_h.nss).
inline constexpr std::int32_t kAbilityTalent = 1;
inline constexpr std::int32_t kAbilitySpell = 2;
inline constexpr std::int32_t kAbilitySkill = 3;
inline constexpr std::int32_t kAbilityItem = 4;

} // namespace tb
