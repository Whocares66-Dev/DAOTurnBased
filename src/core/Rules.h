// The turn-based rules as functions: what a command costs, how initiative
// is rolled, how far a combatant may move. dev/DESIGN.md has the reasons.
#pragma once

#include "core/Settings.h"
#include "core/Types.h"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
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

// What the game layer saw of a creature in combat within the radius, for
// whether it is in the party's fight (Combatant::engaged).
struct Sighting
{
    bool fightsParty = false; // it or a party member is the other's attack target
    bool seen = false;        // in a room the game shows; where there are none, in a party member's line of sight
    float meters = 0.0f;      // to the nearest party member
};

// Whether such a creature takes turns with the party (Settings::joinBySight
// and joinRadius): one fighting the party always; one only fighting near
// it, if it is seen and within joinRadius. Without joinBySight, everyone.
[[nodiscard]] bool InThePartysFight(const Sighting &sighting, const Settings &settings);

// Whether a hostile near the party that is not in combat itself is fighting
// all the same. The game counts a creature as in combat once it has noticed
// an enemy; one that has not (its back turned, a script keeping it at a
// door) left the party "in combat" with nobody to take turns against, the
// world running on in real time until someone struck it: three corpses at a
// cell's door in Redcliffe's dungeon, a second band 40 m down the road after
// an ambush was won (issue 20). So with the party in combat and nobody in its
// fight to fight (someoneToFight false), one that is seen and within
// joinRadius is fighting. Its first turn is its own AI's, which may be to go
// on with what it was doing. Sight is asked for whatever joinBySight says: a
// hostile behind a wall has not been met.
//
// Only then: for an evening (2026-10-03) every hostile seen within the
// radius was drawn in whenever the party was in combat (the user: "If
// they're within range and hostile they're fighting"), and in the open that
// made one fight of several bands: on the Fade's Darkspawn Invasion a pull
// of three became seven, three of them drawn in unawares, and the hero
// alone lost it five times running, where with the bands taken as they
// noticed her it was six fights of two or three, all won (issue 24). The
// user, of drawing them in only when there is nobody else to fight: "Yeah,
// let's do that". A fight that has an enemy grows as the game's own
// perception makes it grow.
//
// One already drawn in (drawnIn) stays in while the party is in combat,
// though another has since noticed and there is now someone to fight: a
// card that came would otherwise go and come again as its creature noticed.
[[nodiscard]] bool FightsUnawares(bool partyInCombat, bool someoneToFight, bool drawnIn, const Sighting &sighting,
                                  const Settings &settings);

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

// Whether two sides fight each other: the enemies against the party and
// its allies. An attacker's enemies are those of the other side, whichever
// it is on (for a hostile, the party and its allies).
[[nodiscard]] bool Opposed(Side a, Side b);

// A kept plain attack whose target has fallen by the turn it lands on is
// turned on the nearest enemy its creature can strike from where it stands,
// for nothing: the swing was begun and paid for (the user, 2026-10-03, issue
// 22: "as the game's own auto-attack would", for everyone, the party and the
// AI). Until then it was dropped, and the turn began with the decision lost:
// most turns end on a kept swing, and with several on one target it had
// often fallen first, 8 to 24 times a fight at Redcliffe in the playthrough
// (kept talents among them, which stay dropped: their target was chosen).
//
// One it might be turned on, as the game layer measured it against the
// attacker (src/game/Bridge.cpp): the machine names who (the standing
// combatants of the other side), the game says what is so of each, and
// which of them it is turned on is decided here.
struct Foe
{
    ObjectId id = kNoObject;
    bool standing = false; // there, and alive
    bool hostile = false;  // to the attacker, by the game's own word
    // The attacker perceives it and has a clear line to it. A shot at one out
    // of its line is not loosed: its archer walks for one, or stands (a bandit
    // 21.6 m from its target, inside its bow's reach, stood 26 s: issue 11).
    bool seen = false;
    float meters = 0.0f; // between the two
    float reach = 0.0f;  // the attacker's reach on it (Reach)
};
// The nearest of them the attacker can strike without a step (WalkToReach
// of none: what the gate judges an attack's walk by), the first named of two
// as near; kNoObject with none, and the attack is dropped as before, what
// its turn paid for it given back.
[[nodiscard]] ObjectId NearestInReach(std::span<const Foe> foes);

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

// What an attack takes, from what the game layer could read of the
// attacker (DAOLib's dao/Animation.h and its play test, 2026-10-02, two
// fights, the same in turns and in real time):
// - a humanoid with a melee weapon: the scripts' formula, which the engine
//   plays the swing to exactly (daggers' 1.000 s took 1.02 s, 46 times);
// - a bow, a crossbow or a staff: a shot, 1.4 s for a follower and 1.6 s
//   for the player's own character (the scripts' draw is shorter for
//   followers), which no animation's length gives;
// - anything else (a creature that is not a humanoid, a humanoid unarmed):
//   its attack animation's length less the blend, 0.81 of it (a giant
//   spider's 1.833 s took 1.49 s); the attack speed modifier does not
//   reach it (read in the exe: nothing on that path reads a property).
// 0 where nothing is known: the stand-in then.
struct AttackFacts
{
    bool humanoid = false;
    bool armed = false;  // a weapon in the main hand
    bool ranged = false; // and it is a bow, a crossbow or a staff
    bool hero = false;   // the player's own character
    WeaponStyle style = WeaponStyle::None;
    float weaponSpeed = 0.0f;         // the weapon's dspeed
    float attackSpeedModifier = 1.0f; // creature property 31
    std::optional<float> animation;   // its attack animation's length, seconds
};
// What the game's own scripts set for a swing, kept as it is read, so that
// a swing costs what the scripts in the game make it and not what this
// file's copy of their formula says. The user, 2026-10-02, of a mod that
// mends the scripts: "shouldn't need to assume dain's fixes? the fixes
// change the data. we should automatically be able to adapt to that."
// SwingSeconds is the scripts' formula as shipped, its faults with it (an
// attack speed under 0.5, Haste with Momentum, is thrown away); a mod that
// mends the script changes what the script sets (SetAttackDuration, which
// the engine plays the swing to), and that is read after each swing
// (DAOLib's dao::animation::AttackDuration) and kept here by what it
// depended on. The formula is then only the first guess, for a weapon and a
// speed not yet seen swung.
class LearnedSwings
{
  public:
    struct Key
    {
        WeaponStyle style = WeaponStyle::None;
        std::int32_t weaponSpeedMs = 0; // the main hand's dspeed
        std::int32_t modifierMilli = 0; // the attack speed modifier
        ObjectId archer = kNoObject;    // a shot's: its archer's own
        auto operator<=>(const Key &) const = default;
    };
    [[nodiscard]] static Key KeyOf(WeaponStyle style, float weaponSpeed, float attackSpeedModifier);
    // A bow's shot, kept by its archer and its attack speed: its time is the
    // game's draw and aim for that archer (combat_h.nss: 0.8 s drawing for a
    // follower, 1.5 s for the player's own character, 2 to 2.5 s in heavy
    // armour, 0.3 s with a staff; then an aim by the bow and the archer's
    // talents and speed), which the engine keeps where DAOLib could not read
    // it back: so what a shot took, measured, is what the next costs (the
    // user, 2026-10-02, of the flat 1.4 and 1.6 s: "we should read these").
    [[nodiscard]] static Key ShotOf(ObjectId archer, float attackSpeedModifier);
    // Kept if it is a time a swing could take (kLeast to kMost): true then.
    bool Learn(const Key &key, float seconds);
    // The last one set; for a weapon in each hand the mean of the last two,
    // since the hands take turns and each has its own time; for a shot the
    // mean of the last two too, measured shots differing by a tenth of a
    // second (Tamlen's 1.59 to 1.72 s, a run with no hands, 2026-10-02).
    [[nodiscard]] std::optional<float> Seconds(const Key &key) const;
    static constexpr float kLeast = 0.25f;
    static constexpr float kMost = 10.0f;

  private:
    struct Read
    {
        float last = 0.0f;
        float before = 0.0f; // 0: one read so far
    };
    std::map<Key, Read> reads_;
};

inline constexpr float kShotSecondsFollower = 1.4f;
inline constexpr float kShotSecondsHero = 1.6f;
inline constexpr float kSwingShareOfAnimation = 0.81f;
[[nodiscard]] float AttackSeconds(const AttackFacts &facts);

// What an ability takes, the same way: its cast animation's length less the
// blend, 0.84 of it (Dirty Fighting's 1.800 s took 1.51 s, a spider's Web's
// 2.433 s took 2.07 s), and before it its conjuring, the start and so many
// passes of the loop, by the creature's own animations where they are read
// (ConjureSeconds' table otherwise). One with no cast animation plays
// nothing of its own: an archery talent fires a shot (Pinning Shot's 1.7 s);
// a monster's special is its script's business, and the stand-in's. 0
// where nothing is known. An item's is its drink (kDrinkSeconds), not this.
struct AbilityFacts
{
    std::optional<float> cast;        // seconds
    std::optional<float> conjure;     // the conjure's start
    std::optional<float> conjureLoop; // one pass of its loop
    int loops = 0;                    // the conjuring speed's count
    bool ranged = false;              // its user holds a bow, a crossbow or a staff
    bool hero = false;
    bool talent = false; // AbilityType talent: with no cast and a bow, a shot
};
inline constexpr float kCastShareOfAnimation = 0.84f;
[[nodiscard]] float AbilitySeconds(const AbilityFacts &facts);

// The damage over which a blow interrupts a spell being conjured
// (rules_core.nss at EVENT_TYPE_DAMAGED, read 2026-10-01; dev/REBALANCE.md,
// "Interruption"): for a party member 10 a rank of Combat Training, and 5,
// and half its level; for anyone else 10 for each of the ranks its kind is
// given, and its level. Physical damage only.
//
// The ranks are the game layer's to read, from what is installed (the
// user, 2026-10-02, asked whether the mod should count Master Combat
// Training as a script mod does: "can we not just read it off the script?
// ... if player has the fix, they get the benefit. if not, they don't.
// each mod should do one thing & do it well"):
// - a party member's: how many it has of the four abilities the script
//   asks HasAbility of. The shipped script's fourth is 100104, which is no
//   ability, where Master Combat Training is 100103: so there the fourth
//   rank never counts, and with a mended script it does. Which four the
//   installed script names is read from its compiled form
//   (CombatTrainingIds);
// - anyone else's: its rank's nCombatTraining in the game's autoscale table
//   (TrainingOfRankAsShipped where that is not read).
[[nodiscard]] float InterruptThreshold(bool party, int combatTraining, int level);

// A thing that is not fighting done with an object: a door, a chest, a lever,
// a lock, a thing picked up, taken or handed over ([costs] object).
[[nodiscard]] bool UsesAnObject(CommandType type);
[[nodiscard]] int TrainingOfRankAsShipped(int creatureRank);

// The four abilities a compiled rules_core counts as Combat Training's
// ranks. In it (NCS V1.0B, read from the game's designerscripts.rim
// 2026-10-02, at 3779) a constant integer is `04 03` and four bytes, the
// high one first, and the four stand 17 to 19 bytes apart, each followed by
// its HasAbility call: 100100, 100101, 100102, and then 100104 as shipped.
// Found by the first three; the fourth is whatever constant follows them.
// The shipped four where the script is not given or they are not found.
inline constexpr std::array<std::int32_t, 4> kCombatTrainingAsShipped{100100, 100101, 100102, 100104};
[[nodiscard]] std::array<std::int32_t, 4> CombatTrainingIds(std::span<const std::uint8_t> compiledScript);

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
