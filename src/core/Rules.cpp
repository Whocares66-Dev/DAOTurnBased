#include "core/Rules.h"

#include <algorithm>
#include <cmath>

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
    // What is not fighting costs no action and does not end the turn (the
    // user's rule, 2026-10-01: a door, and the like; action points will put
    // a price on these): a door, a chest or a lever used, a lock, a thing
    // handed over or picked up. The walk to it is movement all the same.
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

float Reach(float weaponRange, float attackerSpace, float targetSpace)
{
    const auto space = [](float meters) { return std::isfinite(meters) && meters > 0.0f ? meters : 0.0f; };
    const float spaces = space(attackerSpace) + space(targetSpace);
    if (std::isfinite(weaponRange) && weaponRange > 0.0f)
    {
        return std::min(weaponRange, kLongestRange) + spaces;
    }
    return spaces + kMeleeGap + kSwingPlay;
}

float WalkToReach(float distance, float reach)
{
    return std::max(0.0f, distance - reach);
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
        // A distance or a reach that is none (the engine's answer for an
        // object it cannot place) is out of reach: the comparison fails.
        if (!std::isfinite(foe.meters) || !std::isfinite(foe.reach) || WalkToReach(foe.meters, foe.reach) > 0.0f)
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

float MovementBudget(const Settings &settings, float speedModifier, float dexterity)
{
    float meters = settings.movementMeters;
    if (settings.movementRule == MovementRule::Speed)
    {
        // A multiplier on a person's pace: an appearance's speed against a
        // person's, and a haste's 1.5 or a slow's 0.5 when those are read
        // (EffectModifyMovementSpeed, effects_h.nss). A value no multiplier
        // would be (0, a negative, something huge) is read as "normal".
        const bool sensible = std::isfinite(speedModifier) && speedModifier >= 0.1f && speedModifier <= 5.0f;
        meters *= sensible ? speedModifier : 1.0f;
    }
    meters += std::max(0.0f, dexterity - 10.0f) * settings.movementPerDexterity;
    return std::clamp(meters, settings.movementMin, settings.movementMax);
}

float MovementRate(std::span<const float> potencies)
{
    float rate = 1.0f;
    for (const float potency : potencies)
    {
        if (std::isfinite(potency) && potency > 0.0f)
        {
            rate *= potency;
        }
    }
    return rate;
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

bool FightsUnawares(bool partyInCombat, bool someoneToFight, bool drawnIn, const Sighting &sighting,
                    const Settings &settings)
{
    if (!partyInCombat)
    {
        return false;
    }
    if (drawnIn)
    {
        return true;
    }
    return !someoneToFight && sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;
}

bool AsksForTarget(std::int32_t targetType, std::int32_t useType)
{
    return useType != kUseTypeMode && (targetType & ~kTargetSelf) != 0;
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
    case WeaponStyle::Ranged:
        return 0.0f;
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
    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier), kNoObject};
}

LearnedSwings::Key LearnedSwings::ShotOf(ObjectId archer, float attackSpeedModifier)
{
    Key key = KeyOf(WeaponStyle::Ranged, 0.0f, attackSpeedModifier);
    key.archer = archer;
    return key;
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
    if ((key.style == WeaponStyle::Dual || key.style == WeaponStyle::Ranged) && read.before > 0.0f)
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
    return start + loop * static_cast<float>(loops);
}

float AttackSeconds(const AttackFacts &facts)
{
    if (facts.humanoid && facts.armed)
    {
        if (facts.ranged)
        {
            return facts.hero ? kShotSecondsHero : kShotSecondsFollower;
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
    else if (facts.talent && facts.ranged)
    {
        cast = facts.hero ? kShotSecondsHero : kShotSecondsFollower;
    }
    else
    {
        return 0.0f;
    }
    if (facts.loops > 0 && known(facts.conjure))
    {
        cast +=
            *facts.conjure + (known(facts.conjureLoop) ? *facts.conjureLoop : 0.0f) * static_cast<float>(facts.loops);
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
    return party ? 10.0f * ranks + 5.0f + levels / 2.0f : 10.0f * ranks + levels;
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
