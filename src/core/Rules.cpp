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

int RollInitiative(Rng &rng, float dexterity, const Settings &settings)
{
    const float bonus = std::floor((dexterity - 10.0f) * settings.initiativePerDexterity);
    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus);
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

} // namespace tb
