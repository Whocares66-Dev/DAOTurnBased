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
#include <vector>

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
// moves against a person (Pace below: its build's pace times its Haste and
// slows); anything not a sensible multiplier counts as 1.
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

// A creature's movement rate effects taken together: Haste's 1.3, a Swift
// Salve's 1.2, a caltrop trap's 0.6 (EFFECT_TYPE_MOVEMENT_RATE and _DEBUFF,
// their potency; EffectModifyMovementSpeed, effects_h.nss). Multiplied, every
// one of both kinds, which is the engine's own sum: as one is put on or taken
// off it sets the creature's rate to the product of the potencies of all of
// the two types on it (read in the executable 2026-10-05, issue 49: the two
// types share one handler for an effect put on, 0x007A0610, and one for an
// effect taken off, 0x007A0690, registered at 0x007A1820, and each
// multiplies over the object's effects of type 9 and then of type 11,
// 0x0079F910). So Haste with a Swift Salve is 1.56, and Haste in a caltrop
// trap 0.78. A potency that is no rate (0, negative, not a number) is left
// out. 1 with none.
[[nodiscard]] float MovementRate(std::span<const float> potencies);
[[nodiscard]] float MovementRate(const MovementEffects &effects);
// Whether a potency is a rate at all, and so counted.
[[nodiscard]] bool IsMovementRate(float potency);

// The pace a turn's meters are made from, against a person's: the creature's
// build (SpeedFactor) times its movement rate effects (MovementRate).
//
// With a turn a bar of time it is what a second of the bar walks
// (Settings::paceMps times it), and it is the creature's pace as it is now:
// read as the turn begins and again with every look at everyone in it, so
// that what is left to walk is the time left times the pace now
// (TurnMachine::FollowPace; issue 49). It was one pace a turn at first, read
// as the turn began; the user, 2026-10-05, of reading it again within the
// turn: "I think that would be more accurate". The bar is the same length
// hasted or not, and what is hasted is what a thing costs of it: the user,
// the same day, "keeping the bar constant and reducing the cost makes
// sense. I think that also fits better mechanically".
//
// As built (one action and meters of movement) it is one pace a turn, read
// as the turn begins (MovementBudget): the meters are an allowance of their
// own there, and a pace that changed within a turn would want a second rule
// for the meters already walked.
[[nodiscard]] float Pace(float build, const MovementEffects &effects);

// Where the party is: where its living members are (issue 42).
//
// Until 2026-10-04 everything about who is fighting was asked of one
// creature, the party's leader: who is near (the nearest hostiles, round
// it), how far each is (combat_radius, from it), whether the party is in
// combat (its combat state), which rule an area goes by (its room). A party
// is not where one of it is. Twice in one evening in Ortan Thaig a fight
// drifted and three hostiles stood 2 to 9 m from a party member, in combat
// and perceiving him, and none was found: the fight the mod did have ended
// "no enemy left standing", and the game stayed in combat with no turns.
// - The first time the leader had fallen, 83 m from the last of the party
//   standing, and all of it was asked of a body, whose combat state is 0.
// - The second, from the same save, nobody was dead: the leader, alive and
//   the one controlled, stood 70 m from the three others.
// The game's own measure is the party's: it is in combat while any member
// perceives a hostile (combat_h.nss, IsPartyPerceivingHostiles), whoever
// leads.
//
// Who the leader is, as the engine has it (read by DAOLib's session,
// 2026-10-04): the scripts' GetPartyLeader answers the party's leader field,
// the one conversations take for PLAYER, which only SetPartyLeader changes.
// Death does not change it, and neither does the player selecting someone
// else; the creature the player controls is GetMainControlled's. Both answer
// as designed, and neither is where the party is.
//
// So a distance is from the nearest living member, who is near is asked
// round each of them, and nothing is asked of one creature that may be dead.
// One of the party, as the game layer reads it for that (src/game/
// Bridge.cpp):
struct PartyMember
{
    ObjectId id = kNoObject;
    bool alive = false;    // there, and not dead
    bool inCombat = false; // the game has it in combat (its combat state)
};

// Who stands for the party where one creature must (whose room is the
// party's own, by whose word a creature is hostile): one of its living. Its
// leader, if alive; else the one the player controls, if alive and of the
// party; else the first of it living, in the party's own order. kNoObject
// with nobody living.
[[nodiscard]] ObjectId StandsForTheParty(std::span<const PartyMember> party, ObjectId leader, ObjectId controlled);

// Whom the player acts as, for a word said to the player (the toggle's
// answer floats over it): the one controlled, if alive and of the party,
// wherever the leader is; else who stands for the party, above.
[[nodiscard]] ObjectId ActsForThePlayer(std::span<const PartyMember> party, ObjectId leader, ObjectId controlled);

// The party's living members, each once: who a scan measures from, and whom
// it asks the engine round. The one who stands for the party first, then the
// rest in the party's own order: with the leader alive a scan begins with
// the leader's own answers, as it always did.
[[nodiscard]] std::vector<ObjectId> LivingMembers(std::span<const PartyMember> party, ObjectId leader,
                                                  ObjectId controlled);

// Whether the party is in combat, by the game's own word: any living
// member's combat state. A fallen one's is 0 whatever the rest are at, and
// was the whole answer while the leader's was asked.
[[nodiscard]] bool PartyIsInCombat(std::span<const PartyMember> party);

// How far a creature is from the party: from the nearest of its living
// members, given its distance to each. Infinity with nobody living, or with
// no distance that is one (the engine's answer for an object it cannot
// place).
[[nodiscard]] float MetersFromTheParty(std::span<const float> toLiving);

// Whether a creature that far from the party is within a fight's reach
// (Settings::combatRadius): within the radius of any living member. What
// takes a creature in combat into the turns, and nothing else (who stays is
// StaysInTheFight's, below). The user, 2026-10-04, of issues 42 and 43: "If
// you run far enough away from the enemies, the fight should end. That's
// what rtwp does. In that case the enemies should go back to normal. Getting
// close to them should retrigger a fight." The fight's end is the engine's
// perception, below, with every living member out of this reach; and it is
// any living member back within it, of hostiles in combat, that begins a
// fight again, whoever leads and wherever the leader stands.
[[nodiscard]] bool WithinReach(float metersFromTheParty, const Settings &settings);

// What the game layer saw of a creature in combat within the radius, for
// whether it is in the party's fight (Combatant::engaged). The party here is
// its living members (above): a fallen one sees nobody, and nobody is near
// the party for being near its body.
struct Sighting
{
    bool fightsParty = false; // it or a living party member is the other's attack target
    bool seen = false;   // in a room the game shows; where there are none, in a living party member's line of sight
    float meters = 0.0f; // to the nearest living party member
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

// Whether a creature in the fight has not noticed anyone to fight: a hostile
// that the game does not have in combat, and that perceives no party member
// and nobody else hostile to it, by the engine's own words (core/Types.h,
// Perception). Its turn is skipped: "Unaware" over it, about a second, and
// the next turn, its AI not told (TurnMachine::kUnawareMs). It acts by its
// own AI from the moment it notices: struck, or the party come into its
// perception.
//
// All three, so that a turn is skipped only where the engine says on every
// count that there is nobody for it to fight. The game's scripts run a
// creature's combat AI when a command of its ends only while it is in
// combat (rules_core.nss, EVENT_TYPE_COMMAND_COMPLETE: "For normal creatures
// this should run only during combat"), so one not in combat has nothing to
// choose when told its turn has come; and its combat state is set as it
// perceives a hostile. But one the game has in combat is told as before,
// whatever its perception lists hold at that moment: a creature with an AI
// of its own script's (a boss between two phases, the party gone from its
// view) may act on being told all the same, and skipped it would stand for
// good. At worst that is the turn it had before this rule.
//
// One drawn into a fight unawares (FightsUnawares, above) was told its turn
// had come like anyone, and had nothing to choose: its AI picks among the
// hostiles it perceives. In the Brecilian ruins' lower level three skeletons
// that never noticed the party took a turn each round, 54 of them in one
// fight, each poked, waited on for ai_think_ms, and ended "chose nothing"
// (issue 29). Making them notice as they are drawn in was proposed, and the
// user, 2026-10-04: "If they don't notice the party that gives the party the
// initiative to close in and attack. I think that's a feature, not a bug";
// and of their turns: "We can show text unaware above the enemy's head, pause
// for about a second, then skip their turn."
//
// Only an enemy: an ally that perceives no hostile has its own AI's answer
// for that, and nobody has asked for its turn to be skipped. And one that
// perceives a foe that is not the party's (an ally of the party's it is
// fighting) is not unaware: it has someone to choose.
[[nodiscard]] bool Unaware(Side side, const Perception &perception);

// A creature that is not of the party, that the game does not have in combat
// and that perceives nobody to fight has no turn (issue 43): it is the
// game's, and what its script has it do is no choice of a turn's. An enemy
// so is one that is unaware (above); an ally is so when it perceives nobody
// hostile to it, which is when the game takes it out of combat
// (combat_h.nss, Combat_HandleCreatureDisappear). As the turn machine looks
// at the creature whose turn it is (TurnMachine::Step).
//
// The game's combat state alone is not asked here, as it is at the gate,
// where a script's command for a creature out of combat says of itself that
// it is not its combat AI's. A look can fall in a frame in which a creature
// has perceived an enemy and its script has not yet set its combat state
// (rules_core.nss sets it as it handles EVENT_TYPE_PERCEPTION_APPEAR, which
// need not be in the frame its perception changed; read in the scripts, not
// measured in play): that one is about to fight, and "acts from the moment
// it notices" (issue 29). So, as for unaware, the engine must say on every
// count that there is nobody for it to fight.
[[nodiscard]] bool NothingToFight(Side side, const Perception &perception);

// Whether a creature that is in the fight stays in it (issue 29).
//
// The game itself ends combat by perception, not by a distance. When a
// hostile drops out of a party member's perception the scripts ask whether
// the party still perceives any hostile (IsPartyPerceivingHostiles), and if
// not the game is back in explore mode three seconds later; a creature that
// is not the party's leaves combat when it perceives no hostile (combat_h.nss,
// Combat_HandleCreatureDisappear; module_core.nss,
// EVENT_TYPE_DELAYED_GM_CHANGE). The mod had a distance of its own both
// ways: a combatant further than combat_radius from the party's leader was
// out of the fight, and stood frozen as one outside the order does, whoever
// it was chasing. The user, 2026-10-04: "Once they're in the fight I think
// they should stay in the fight for the distance that the game uses. The
// game allows you to run away and exit a fight. Whatever distance that is,
// that's the one we should use for fight exit, for consistency."
//
// So one in the fight that the scan's own rules still find fighting
// (`fighting`: in combat within combat_radius, or drawn in unawares) stays,
// as it always did: those rules bring it in, and it would come again at
// once. One they no longer find stays by the engine's perception alone:
// - an enemy, while a party member perceives it or it perceives a party
//   member: either keeps the two in one fight by the game's own reckoning;
// - an ally of the party's, while it perceives someone hostile to it: the
//   test the game keeps it in combat by. Left to the radius, an ally that
//   followed an enemy past it would stand frozen under the blows of an
//   enemy that now stays in the fight.
// What brings a creature into a fight is not this: one that is not in the
// fight does not join for being perceived.
[[nodiscard]] bool StaysInTheFight(Side side, bool fighting, const Perception &perception);

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
    // Not in stealth, and the attacker has a clear line to it. A shot at one
    // out of its line is not loosed: its archer walks for one, or stands
    // (issue 11, whose bandit, 21.6 m from its target and inside its bow's
    // reach, stood 26 s; whether that one lacked a line was not read).
    bool seen = false;
    float meters = 0.0f; // between the two
    // The attacker would strike it from where it stands: the engine's own
    // answer for the pair (DAOLib's dao::attack::ApproachOf, the attack
    // command's approach asked whether it would end at once), which the
    // game layer asks. The mod keeps no reach rule of its own (issue 11).
    bool inReach = false;
};
// The nearest of them the attacker can strike without a step (Foe::inReach:
// what the gate judges an attack's walk by), the first named of two
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
