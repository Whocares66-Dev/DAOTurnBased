#include "core/Rules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace tb
{

CommandKind Classify(CommandType type)
{
    switch (type)
    {
    // The one per turn: a combat action, what a creature does to someone
    // in the fight or to itself for it. An attack, a talent or a spell, an
    // item used (a potion, a bomb).
    case CommandType::Attack:
    case CommandType::UseAbility:
    case CommandType::UseItem:
    case CommandType::Fly:
        return CommandKind::Action;
    // What is not fighting is no action and does not end the turn (the
    // user's rule, 2026-10-01: a door, and the like): a door, a chest or a
    // lever used, a lock, a thing handed over or picked up. It costs its own
    // seconds of the bar ([costs] object), and the walk to it is movement
    // all the same.
    case CommandType::UseObject:
    case CommandType::Interact:
    case CommandType::Lock:
    case CommandType::Unlock:
    case CommandType::GiveItem:
    case CommandType::TakeItem:
    case CommandType::PickupItem:
        return CommandKind::Free;
    // Walking, however it is asked for.
    case CommandType::MoveToLocation:
    case CommandType::MoveToObject:
    case CommandType::MoveToMultipleLocations:
    case CommandType::MoveAwayFromObject:
    case CommandType::MoveAwayFromPoint:
    case CommandType::ForceFollowObject:
    case CommandType::RandomWalk:
    case CommandType::Drive:
        return CommandKind::Move;
    // Handling one's own gear, and turning to face (free as well).
    case CommandType::SwitchWeaponSets:
    case CommandType::SheatheWeapons:
    case CommandType::EquipItem:
    case CommandType::UnequipItem:
    case CommandType::DropItem:
    case CommandType::Turn:
        return CommandKind::Free;
    case CommandType::Wait:
        return CommandKind::Pass;
    // What the engine and the story do: effects, death blows, jumps, barks,
    // animations, conversations, appearing and vanishing, shops.
    case CommandType::Invalid:
    case CommandType::Appear:
    case CommandType::Disappear:
    case CommandType::DoFunction:
    case CommandType::JumpToObject:
    case CommandType::JumpToLocation:
    case CommandType::SpeakString:
    case CommandType::SpeakStrref:
    case CommandType::PlayAnimation:
    case CommandType::PlaySound:
    case CommandType::StartConversation:
    case CommandType::Examine:
    case CommandType::ExecuteEffect:
    case CommandType::SellItem:
    case CommandType::BuyItem:
    case CommandType::DoEvent:
    case CommandType::Deathblow:
        return CommandKind::Involuntary;
    }
    // A number the table does not have: let it through rather than freeze a
    // creature on something unknown. The gate logs it.
    return CommandKind::Involuntary;
}

CommandKind Classify(CommandType type, Source source)
{
    const CommandKind kind = Classify(type);
    // A wait nobody chose: the scripts' SetActive puts one at the front of a
    // creature's queue with an animation (DAOLib's Addresses.h, the callers
    // of QueueCommand). Only a script's wait is an AI passing.
    if (kind == CommandKind::Pass && source != Source::Script)
    {
        return CommandKind::Involuntary;
    }
    return kind;
}

const char *Name(CommandType type)
{
    switch (type)
    {
    case CommandType::Invalid:
        return "invalid";
    case CommandType::Attack:
        return "attack";
    case CommandType::Appear:
        return "appear";
    case CommandType::Disappear:
        return "disappear";
    case CommandType::ForceFollowObject:
        return "follow";
    case CommandType::RandomWalk:
        return "random walk";
    case CommandType::DoFunction:
        return "do function";
    case CommandType::GiveItem:
        return "give item";
    case CommandType::TakeItem:
        return "take item";
    case CommandType::JumpToObject:
        return "jump to object";
    case CommandType::JumpToLocation:
        return "jump to location";
    case CommandType::SpeakString:
        return "speak";
    case CommandType::SpeakStrref:
        return "speak string";
    case CommandType::Wait:
        return "wait";
    case CommandType::PlayAnimation:
        return "play animation";
    case CommandType::PlaySound:
        return "play sound";
    case CommandType::MoveAwayFromObject:
        return "move away from object";
    case CommandType::MoveAwayFromPoint:
        return "move away from point";
    case CommandType::StartConversation:
        return "start conversation";
    case CommandType::Examine:
        return "examine";
    case CommandType::MoveToLocation:
        return "move to location";
    case CommandType::MoveToObject:
        return "move to object";
    case CommandType::MoveToMultipleLocations:
        return "move through points";
    case CommandType::Lock:
        return "lock";
    case CommandType::Unlock:
        return "unlock";
    case CommandType::UseAbility:
        return "use ability";
    case CommandType::UseItem:
        return "use item";
    case CommandType::DropItem:
        return "drop item";
    case CommandType::PickupItem:
        return "pick up item";
    case CommandType::EquipItem:
        return "equip";
    case CommandType::UnequipItem:
        return "unequip";
    case CommandType::UseObject:
        return "use object";
    case CommandType::ExecuteEffect:
        return "execute effect";
    case CommandType::Drive:
        return "drive";
    case CommandType::SellItem:
        return "sell";
    case CommandType::BuyItem:
        return "buy";
    case CommandType::DoEvent:
        return "do event";
    case CommandType::Interact:
        return "interact";
    case CommandType::Deathblow:
        return "death blow";
    case CommandType::SheatheWeapons:
        return "sheathe or draw";
    case CommandType::SwitchWeaponSets:
        return "switch weapons";
    case CommandType::Turn:
        return "turn";
    case CommandType::Fly:
        return "fly";
    }
    return "unknown";
}

const char *Name(CommandKind kind)
{
    switch (kind)
    {
    case CommandKind::Action:
        return "action";
    case CommandKind::Move:
        return "move";
    case CommandKind::Free:
        return "free";
    case CommandKind::Pass:
        return "pass";
    case CommandKind::Involuntary:
        return "involuntary";
    }
    return "?";
}

const char *Name(Source source)
{
    switch (source)
    {
    case Source::Script:
        return "script";
    case Source::Player:
        return "player";
    case Source::Engine:
        return "engine";
    case Source::Unknown:
        return "unknown";
    }
    return "?";
}

const char *Name(Side side)
{
    switch (side)
    {
    case Side::Party:
        return "party";
    case Side::Ally:
        return "ally";
    case Side::Enemy:
        return "enemy";
    }
    return "?";
}

int RollInitiative(Rng &rng, float dexterity, const Settings &settings, int survival)
{
    constexpr int kTopRank = 4;
    const float bonus = std::floor((dexterity - 10.0f) * settings.initiativePerDexterity);
    const float ranks =
        std::floor(static_cast<float>(std::clamp(survival, 0, kTopRank)) * settings.initiativePerSurvival);
    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus) + static_cast<int>(ranks);
}

bool Opposed(Side a, Side b)
{
    return (a == Side::Enemy) != (b == Side::Enemy);
}

ObjectId NearestInReach(std::span<const Foe> foes)
{
    const Foe *nearest = nullptr;
    for (const Foe &foe : foes)
    {
        if (!foe.standing || !foe.hostile || !foe.seen)
        {
            continue;
        }
        // A distance that is none (the engine's answer for an object it
        // cannot place) is no nearest: the comparison would fail.
        if (!foe.inReach || !std::isfinite(foe.meters))
        {
            continue;
        }
        if (nearest == nullptr || foe.meters < nearest->meters)
        {
            nearest = &foe;
        }
    }
    return nearest != nullptr ? nearest->id : kNoObject;
}

bool IsMovementRate(float potency)
{
    return std::isfinite(potency) && potency > 0.0f;
}

float MovementRate(std::span<const float> potencies)
{
    float rate = 1.0f;
    for (const float potency : potencies)
    {
        if (IsMovementRate(potency))
        {
            rate *= potency;
        }
    }
    return rate;
}

float MovementRate(const MovementEffects &effects)
{
    return MovementRate(effects.rates) * MovementRate(effects.debuffs);
}

float Pace(float build, const MovementEffects &effects)
{
    return build * MovementRate(effects);
}

ObjectId StandsForTheParty(std::span<const PartyMember> party, ObjectId leader, ObjectId controlled)
{
    const auto living = [&](ObjectId id) {
        return std::ranges::any_of(party, [&](const PartyMember &member) { return member.id == id && member.alive; });
    };
    if (living(leader))
    {
        return leader;
    }
    if (living(controlled))
    {
        return controlled;
    }
    const auto first = std::ranges::find_if(party, [](const PartyMember &member) { return member.alive; });
    return first != party.end() ? first->id : kNoObject;
}

ObjectId ActsForThePlayer(std::span<const PartyMember> party, ObjectId leader, ObjectId controlled)
{
    // The same choice among the living, the one controlled put first: the
    // two change places on purpose.
    return StandsForTheParty(party, controlled, leader); // NOLINT(readability-suspicious-call-argument)
}

std::vector<ObjectId> LivingMembers(std::span<const PartyMember> party, ObjectId leader, ObjectId controlled)
{
    std::vector<ObjectId> living;
    if (const ObjectId stands = StandsForTheParty(party, leader, controlled); stands != kNoObject)
    {
        living.push_back(stands);
    }
    for (const PartyMember &member : party)
    {
        if (member.alive && std::ranges::find(living, member.id) == living.end())
        {
            living.push_back(member.id);
        }
    }
    return living;
}

bool PartyIsInCombat(std::span<const PartyMember> party)
{
    return std::ranges::any_of(party, [](const PartyMember &member) { return member.alive && member.inCombat; });
}

float MetersFromTheParty(std::span<const float> toLiving)
{
    float nearest = std::numeric_limits<float>::infinity();
    for (const float meters : toLiving)
    {
        // A distance that is none (not a number) is never the nearer: the
        // comparison fails, and it is passed over. Not std::min, which
        // clang-tidy would have here and once wrote: it answers its first
        // argument unless the second is less, so a distance that is no
        // number became the nearest (issue 55; the tests said so).
        if (meters < nearest) // NOLINT(readability-use-std-min-max)
        {
            nearest = meters;
        }
    }
    return nearest;
}

bool WithinReach(float metersFromTheParty, const Settings &settings)
{
    // Infinity (nobody living to be near) and a distance that is none are
    // within no radius: the comparison says so of both.
    return metersFromTheParty <= settings.combatRadius;
}

bool InThePartysFight(const Sighting &sighting, const Settings &settings)
{
    if (!settings.joinBySight || sighting.fightsParty)
    {
        return true;
    }
    // A distance that is no distance (the engine's answer for an object it
    // cannot place) keeps the creature out, as an unseen one is kept.
    return sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;
}

bool FightsUnawares(bool partyInCombat, bool someoneToFight, const Sighting &sighting, const Settings &settings)
{
    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&
           sighting.meters <= settings.joinRadius;
}

namespace
{

// Nobody for it to fight, by the engine's own words on every count: the game
// does not have it in combat, and it perceives nobody hostile to it; nor, an
// enemy, a party member, its foes being the party's first. (Not asked of the
// party's own.)
bool PerceivesNobodyToFight(Side side, const Perception &perception)
{
    if (perception.inCombat || perception.ofFoes)
    {
        return false;
    }
    return side != Side::Enemy || !perception.ofParty;
}

// Kept in a fight it is of by the engine's perception alone: an enemy while
// the party perceives it or it the party; an ally while it perceives someone
// hostile to it.
bool KeptByPerception(Side side, const Perception &perception)
{
    if (side == Side::Enemy)
    {
        return perception.byParty || perception.ofParty;
    }
    return perception.ofFoes;
}

} // namespace

Rotation InTheRotation(const Presence &seen)
{
    // Out of the world, whoever it is, standing or fallen, and whatever its
    // perception lists hold: nobody is there to order, to read or to show.
    if (seen.inactive)
    {
        return Rotation::OutOfTheWorld;
    }
    // Dead: one of the party keeps its place, and a body that is not the
    // party's has none.
    if (seen.down)
    {
        return seen.side == Side::Party ? Rotation::Fallen : Rotation::Dead;
    }
    // The party's own are always in its fight.
    if (seen.side == Side::Party)
    {
        return Rotation::Fights;
    }
    // Not found fighting the party: one of the fight is in it while the
    // engine's perception keeps it there, and anyone else is not.
    if (!seen.found && !(seen.member && KeptByPerception(seen.side, seen.perception)))
    {
        return Rotation::NotFighting;
    }
    if (PerceivesNobodyToFight(seen.side, seen.perception))
    {
        return seen.side == Side::Enemy ? Rotation::Unaware : Rotation::OutOfCombat;
    }
    return Rotation::Fights;
}

bool InRotation(Rotation where)
{
    switch (where)
    {
    case Rotation::Fights:
    case Rotation::Fallen:
    case Rotation::Unaware:
    case Rotation::OutOfCombat:
        return true;
    case Rotation::Dead:
    case Rotation::OutOfTheWorld:
    case Rotation::NotFighting:
        return false;
    }
    return false;
}

std::string_view Why(Rotation where, Side side)
{
    switch (where)
    {
    case Rotation::Fights:
        return "it is fighting";
    case Rotation::Fallen:
    case Rotation::Dead:
        return "it has fallen";
    case Rotation::Unaware:
        return "it has not noticed the party";
    case Rotation::OutOfCombat:
        return "out of combat by the game's own word, and perceives nobody to fight";
    case Rotation::OutOfTheWorld:
        return "the game has taken it out of the world";
    case Rotation::NotFighting:
        return side == Side::Enemy ? "not found fighting the party, and neither it nor the party perceives the other"
                                   : "not found fighting beside the party, and it perceives no enemy";
    }
    return "?";
}

bool NobodyToFight(Rotation where)
{
    return where == Rotation::Unaware || where == Rotation::OutOfCombat;
}

bool AsksForTarget(std::int32_t targetType, std::int32_t useType)
{
    return useType != kUseTypeMode && (targetType & ~kTargetSelf) != 0;
}

std::int32_t OwnModeAtItsCommand(ObjectId creature, std::span<const EffectOn> standing)
{
    if (creature == kNoObject)
    {
        return kNoMode;
    }
    for (const EffectOn &effect : standing)
    {
        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)
        {
            return effect.ability;
        }
    }
    return kNoMode;
}

float SpeedFactor(float combatSpeed)
{
    if (!std::isfinite(combatSpeed) || combatSpeed <= 0.0f)
    {
        return 1.0f;
    }
    return combatSpeed / kPersonCombatSpeed;
}

float SwingSeconds(WeaponStyle style, float weaponSpeed, float attackSpeedModifier)
{
    float base = 0.0f;
    switch (style)
    {
    case WeaponStyle::None:
    case WeaponStyle::Single:
        base = 2.0f;
        break;
    case WeaponStyle::Dual:
        base = 1.5f;
        break;
    case WeaponStyle::TwoHanded:
        base = 2.5f;
        break;
    }
    const bool sensible = std::isfinite(attackSpeedModifier) && attackSpeedModifier >= 0.5f;
    const float modifier = sensible ? std::min(1.5f, attackSpeedModifier) : 1.0f;
    const float seconds = (base + (std::isfinite(weaponSpeed) ? weaponSpeed : 0.0f)) * modifier;
    return seconds > 0.0f ? seconds : 0.0f;
}

LearnedSwings::Key LearnedSwings::KeyOf(WeaponStyle style, float weaponSpeed, float attackSpeedModifier)
{
    const auto milli = [](float value) {
        return std::isfinite(value)
                   ? static_cast<std::int32_t>(std::lround(std::clamp(value, -100.0f, 100.0f) * 1000.0f))
                   : 0;
    };
    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier)};
}

bool LearnedSwings::Learn(const Key &key, float seconds)
{
    if (!std::isfinite(seconds) || seconds < kLeast || seconds > kMost)
    {
        return false;
    }
    Read &read = reads_[key];
    read.before = read.last;
    read.last = seconds;
    return true;
}

std::optional<float> LearnedSwings::Seconds(const Key &key) const
{
    const auto found = reads_.find(key);
    if (found == reads_.end())
    {
        return std::nullopt;
    }
    const Read &read = found->second;
    if (key.style == WeaponStyle::Dual && read.before > 0.0f)
    {
        return (read.last + read.before) / 2.0f;
    }
    return read.last;
}

float ConjureSeconds(std::int32_t conjureAnim, int loops, bool staff)
{
    if (conjureAnim <= 0 || loops <= 0)
    {
        return 0.0f;
    }
    constexpr std::int32_t kBuildsUp = 245;
    constexpr std::int32_t kDrawsIn = 244;
    float start = 0.6f;
    float loop = 3.0f;
    if (conjureAnim == kBuildsUp)
    {
        start = staff ? 0.77f : 0.67f;
        loop = staff ? 2.70f : 2.27f;
    }
    else if (conjureAnim == kDrawsIn)
    {
        start = 0.47f;
        loop = 3.40f;
    }
    return start + (loop * static_cast<float>(loops));
}

float DrawSeconds(const ShotFacts &facts)
{
    if (facts.staff)
    {
        return 0.3f;
    }
    const bool slowed = facts.massive || (facts.heavy && !facts.masterArcher);
    if (slowed)
    {
        return facts.follower ? 2.0f : 2.5f;
    }
    return facts.follower ? 0.8f : 1.5f;
}

float HoldSeconds(float aimSpeed, float weaponAimDelay)
{
    const float weapon = std::isfinite(weaponAimDelay) ? std::max(weaponAimDelay, 0.0f) : 0.0f;
    const float hold = (std::isfinite(aimSpeed) ? aimSpeed : 0.0f) + weapon;
    return std::max(hold, 0.0f);
}

float AttackSeconds(const AttackFacts &facts)
{
    if (facts.humanoid && facts.armed)
    {
        if (facts.ranged)
        {
            return facts.shot && std::isfinite(*facts.shot) && *facts.shot > 0.0f ? *facts.shot : 0.0f;
        }
        return SwingSeconds(facts.style, facts.weaponSpeed, facts.attackSpeedModifier);
    }
    if (facts.animation && std::isfinite(*facts.animation) && *facts.animation > 0.0f)
    {
        return *facts.animation * kSwingShareOfAnimation;
    }
    return 0.0f;
}

float AbilitySeconds(const AbilityFacts &facts)
{
    const auto known = [](const std::optional<float> &seconds) {
        return seconds && std::isfinite(*seconds) && *seconds > 0.0f;
    };
    float cast = 0.0f;
    if (known(facts.cast))
    {
        cast = *facts.cast * kCastShareOfAnimation;
    }
    else if (known(facts.shot))
    {
        cast = *facts.shot;
    }
    else
    {
        return 0.0f;
    }
    if (facts.loops > 0 && known(facts.conjure))
    {
        cast +=
            *facts.conjure + ((known(facts.conjureLoop) ? *facts.conjureLoop : 0.0f) * static_cast<float>(facts.loops));
    }
    return cast;
}

bool UsesAnObject(CommandType type)
{
    switch (type)
    {
    case CommandType::UseObject:
    case CommandType::Interact:
    case CommandType::Lock:
    case CommandType::Unlock:
    case CommandType::GiveItem:
    case CommandType::TakeItem:
    case CommandType::PickupItem:
        return true;
    default:
        return false;
    }
}

float InterruptThreshold(bool party, int combatTraining, int level)
{
    const float levels = static_cast<float>(std::max(0, level));
    const float ranks = static_cast<float>(std::clamp(combatTraining, 0, 20));
    return party ? (10.0f * ranks) + 5.0f + (levels / 2.0f) : (10.0f * ranks) + levels;
}

int TrainingOfRankAsShipped(int creatureRank)
{
    switch (creatureRank)
    {
    case 2: // normal
        return 2;
    case 3: // lieutenant
        return 3;
    case 4: // boss
        return 4;
    case 5: // elite boss
        return 6;
    case 11: // a weak normal creature
        return 1;
    default: // a critter, and what has no rank
        return 0;
    }
}

namespace
{
constexpr std::size_t kConstantBytes = 6; // 04 03, and four bytes, the high one first

std::optional<std::int32_t> ConstantAt(std::span<const std::uint8_t> script, std::size_t at)
{
    if (at + kConstantBytes > script.size() || script[at] != 0x04 || script[at + 1] != 0x03)
    {
        return std::nullopt;
    }
    const std::uint32_t value = (static_cast<std::uint32_t>(script[at + 2]) << 24) |
                                (static_cast<std::uint32_t>(script[at + 3]) << 16) |
                                (static_cast<std::uint32_t>(script[at + 4]) << 8) | script[at + 5];
    return static_cast<std::int32_t>(value);
}
} // namespace

std::array<std::int32_t, 4> CombatTrainingIds(std::span<const std::uint8_t> compiledScript)
{
    constexpr std::size_t kNear = 48; // the four stand 17 to 19 bytes apart as shipped
    for (std::size_t first = 0; first + kConstantBytes <= compiledScript.size(); ++first)
    {
        if (ConstantAt(compiledScript, first) != kCombatTrainingAsShipped[0])
        {
            continue;
        }
        std::array<std::int32_t, 4> ids{kCombatTrainingAsShipped[0], 0, 0, 0};
        std::size_t from = first + kConstantBytes;
        std::size_t found = 1;
        while (found < ids.size())
        {
            std::optional<std::int32_t> next;
            for (std::size_t at = from; at < from + kNear && !next; ++at)
            {
                next = ConstantAt(compiledScript, at);
                if (next)
                {
                    from = at + kConstantBytes;
                }
            }
            if (!next)
            {
                break;
            }
            ids.at(found++) = *next;
        }
        if (found == ids.size() && ids[1] == kCombatTrainingAsShipped[1] && ids[2] == kCombatTrainingAsShipped[2] &&
            ids[3] > 0)
        {
            return ids;
        }
    }
    return kCombatTrainingAsShipped;
}

} // namespace tb
