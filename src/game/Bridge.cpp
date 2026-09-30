#include "game/Bridge.h"

#include "game/Camera.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/ClientExoApp.h"
#include "dao/Effects.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/ScriptHooks.h"
#include "dao/core/CommandWord.h"
#include "dao/generated/ScriptApi.h"
#include "dao/generated/ScriptConstants.h"

#include <algorithm>
#include <unordered_set>

namespace tb::game
{

namespace api = dao::script::api;
namespace k = dao::script::constants;

// The core's numbers are the engine's.
static_assert(static_cast<int>(Mode::Explore) == static_cast<int>(dao::GameMode::Explore));
static_assert(static_cast<int>(Mode::Combat) == static_cast<int>(dao::GameMode::Combat));
static_assert(static_cast<int>(Mode::Dead) == static_cast<int>(dao::GameMode::Dead));
static_assert(static_cast<int>(Mode::Gui) == static_cast<int>(dao::GameMode::Gui));
static_assert(static_cast<int>(Mode::Dialog) == static_cast<int>(dao::GameMode::Dialog));
static_assert(static_cast<int>(Mode::Cutscene) == static_cast<int>(dao::GameMode::Cutscene));
static_assert(static_cast<int>(Mode::FlyCam) == static_cast<int>(dao::GameMode::FlyCam));
static_assert(static_cast<int>(Mode::Fixed) == static_cast<int>(dao::GameMode::Fixed));
static_assert(static_cast<int>(Mode::Pregame) == static_cast<int>(dao::GameMode::Pregame));
static_assert(static_cast<int>(Mode::Loading) == static_cast<int>(dao::GameMode::Loading));
static_assert(static_cast<int>(Mode::Movie) == static_cast<int>(dao::GameMode::Movie));
static_assert(static_cast<int>(Mode::CharGen) == static_cast<int>(dao::GameMode::CharGen));
static_assert(static_cast<int>(Mode::PartyPicker) == static_cast<int>(dao::GameMode::PartyPicker));
// Every command type, as DAOLib names it from the game's commands table.
static_assert(static_cast<int>(CommandType::Invalid) == static_cast<int>(dao::core::CommandType::Invalid));
static_assert(static_cast<int>(CommandType::Attack) == static_cast<int>(dao::core::CommandType::Attack));
static_assert(static_cast<int>(CommandType::Appear) == static_cast<int>(dao::core::CommandType::Appear));
static_assert(static_cast<int>(CommandType::Disappear) == static_cast<int>(dao::core::CommandType::Disappear));
static_assert(static_cast<int>(CommandType::ForceFollowObject) ==
              static_cast<int>(dao::core::CommandType::ForceFollowObject));
static_assert(static_cast<int>(CommandType::RandomWalk) == static_cast<int>(dao::core::CommandType::RandomWalk));
static_assert(static_cast<int>(CommandType::DoFunction) == static_cast<int>(dao::core::CommandType::DoFunction));
static_assert(static_cast<int>(CommandType::GiveItem) == static_cast<int>(dao::core::CommandType::GiveItem));
static_assert(static_cast<int>(CommandType::TakeItem) == static_cast<int>(dao::core::CommandType::TakeItem));
static_assert(static_cast<int>(CommandType::JumpToObject) == static_cast<int>(dao::core::CommandType::JumpToObject));
static_assert(static_cast<int>(CommandType::JumpToLocation) ==
              static_cast<int>(dao::core::CommandType::JumpToLocation));
static_assert(static_cast<int>(CommandType::SpeakString) == static_cast<int>(dao::core::CommandType::SpeakString));
static_assert(static_cast<int>(CommandType::SpeakStrref) == static_cast<int>(dao::core::CommandType::SpeakStrref));
static_assert(static_cast<int>(CommandType::Wait) == static_cast<int>(dao::core::CommandType::Wait));
static_assert(static_cast<int>(CommandType::PlayAnimation) == static_cast<int>(dao::core::CommandType::PlayAnimation));
static_assert(static_cast<int>(CommandType::PlaySound) == static_cast<int>(dao::core::CommandType::PlaySound));
static_assert(static_cast<int>(CommandType::MoveAwayFromObject) ==
              static_cast<int>(dao::core::CommandType::MoveAwayFromObject));
static_assert(static_cast<int>(CommandType::MoveAwayFromPoint) ==
              static_cast<int>(dao::core::CommandType::MoveAwayFromPoint));
static_assert(static_cast<int>(CommandType::StartConversation) ==
              static_cast<int>(dao::core::CommandType::StartConversation));
static_assert(static_cast<int>(CommandType::Examine) == static_cast<int>(dao::core::CommandType::Examine));
static_assert(static_cast<int>(CommandType::MoveToLocation) ==
              static_cast<int>(dao::core::CommandType::MoveToLocation));
static_assert(static_cast<int>(CommandType::MoveToObject) == static_cast<int>(dao::core::CommandType::MoveToObject));
static_assert(static_cast<int>(CommandType::MoveToMultipleLocations) ==
              static_cast<int>(dao::core::CommandType::MoveToMultipleLocations));
static_assert(static_cast<int>(CommandType::Lock) == static_cast<int>(dao::core::CommandType::Lock));
static_assert(static_cast<int>(CommandType::Unlock) == static_cast<int>(dao::core::CommandType::Unlock));
static_assert(static_cast<int>(CommandType::UseAbility) == static_cast<int>(dao::core::CommandType::UseAbility));
static_assert(static_cast<int>(CommandType::UseItem) == static_cast<int>(dao::core::CommandType::UseItem));
static_assert(static_cast<int>(CommandType::DropItem) == static_cast<int>(dao::core::CommandType::DropItem));
static_assert(static_cast<int>(CommandType::PickupItem) == static_cast<int>(dao::core::CommandType::PickupItem));
static_assert(static_cast<int>(CommandType::EquipItem) == static_cast<int>(dao::core::CommandType::EquipItem));
static_assert(static_cast<int>(CommandType::UnequipItem) == static_cast<int>(dao::core::CommandType::UnequipItem));
static_assert(static_cast<int>(CommandType::UseObject) == static_cast<int>(dao::core::CommandType::UseObject));
static_assert(static_cast<int>(CommandType::ExecuteEffect) == static_cast<int>(dao::core::CommandType::ExecuteEffect));
static_assert(static_cast<int>(CommandType::Drive) == static_cast<int>(dao::core::CommandType::Drive));
static_assert(static_cast<int>(CommandType::SellItem) == static_cast<int>(dao::core::CommandType::SellItem));
static_assert(static_cast<int>(CommandType::BuyItem) == static_cast<int>(dao::core::CommandType::BuyItem));
static_assert(static_cast<int>(CommandType::DoEvent) == static_cast<int>(dao::core::CommandType::DoEvent));
static_assert(static_cast<int>(CommandType::Interact) == static_cast<int>(dao::core::CommandType::Interact));
static_assert(static_cast<int>(CommandType::Deathblow) == static_cast<int>(dao::core::CommandType::Deathblow));
static_assert(static_cast<int>(CommandType::SheatheWeapons) ==
              static_cast<int>(dao::core::CommandType::SheatheWeapons));
static_assert(static_cast<int>(CommandType::SwitchWeaponSets) ==
              static_cast<int>(dao::core::CommandType::SwitchWeaponSets));
static_assert(static_cast<int>(CommandType::Turn) == static_cast<int>(dao::core::CommandType::Turn));
static_assert(static_cast<int>(CommandType::Fly) == static_cast<int>(dao::core::CommandType::Fly));
static_assert(kNoObject == dao::kInvalidObject);

namespace
{

// The scripts' MAX_CREATURES_IN_COMBAT (rules_h.nss): how many hostiles the
// game's own "who is fighting" asks for.
constexpr std::int32_t kMaxCreaturesInCombat = 24;
// EVENT_TYPE_DEBUG_KICKSTART_AI (events_h.nss): rules_core runs
// AI_DetermineCombatRound on it.
constexpr std::int32_t kKickstartAi = 9998;

bool OutOfWorld(Mode mode)
{
    return mode == Mode::Loading || mode == Mode::Pregame || mode == Mode::CharGen || mode == Mode::Unknown;
}

float Health(ObjectId id)
{
    const float current = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_HEALTH, k::PROPERTY_VALUE_CURRENT);
    const float total = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_HEALTH, k::PROPERTY_VALUE_TOTAL);
    return total > 0.0f ? std::clamp(current / total, 0.0f, 1.0f) : 1.0f;
}

} // namespace

Observation Bridge::Observe(const Needs &needs, std::uint32_t nowMs)
{
    Observation obs;
    obs.nowMs = nowMs;
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady())
    {
        return obs; // Mode::Unknown: no world yet
    }
    obs.mode = static_cast<Mode>(app->Mode());
    obs.paused = app->IsPaused();
    if (OutOfWorld(obs.mode))
    {
        // Ids are the save's: another game can reuse them.
        known_.clear();
        return obs;
    }
    if (needs.scan)
    {
        obs.scan = Scan();
    }
    if (needs.actor != kNoObject)
    {
        obs.actor = Actor(needs.actor);
    }
    for (const ObjectId id : needs.statuses)
    {
        obs.statuses.push_back(Status(id));
    }
    return obs;
}

std::vector<Combatant> Bridge::Scan()
{
    std::vector<Combatant> out;
    const ObjectId hero = api::GetHero();
    if (api::IsObjectValid(hero) == 0)
    {
        return out;
    }
    // Distances are the party leader's, whom the party follows; the hero if
    // there is none.
    ObjectId leader = api::GetPartyLeader();
    if (api::IsObjectValid(leader) == 0)
    {
        leader = hero;
    }
    std::unordered_set<ObjectId> seen;

    const dao::script::Array party = api::GetPartyList(hero);
    for (std::size_t i = 0; i < party.Size(); ++i)
    {
        const ObjectId id = party.Object(i);
        if (api::IsObjectValid(id) != 0 && seen.insert(id).second)
        {
            out.push_back(Describe(id, Side::Party, id == hero));
        }
    }
    // The game's own question for who is fighting the party (UT_Surrender,
    // utility_h.nss): the nearest living hostiles, those in combat.
    const auto fighting = [&](ObjectId id) {
        return api::IsObjectValid(id) != 0 && !seen.contains(id) && api::GetCombatState(id) != 0 &&
               api::GetDistanceBetween(leader, id) <= settings_.combatRadius;
    };
    const dao::script::Array hostiles =
        api::GetNearestObjectByHostility(leader, 1, k::OBJECT_TYPE_CREATURE, kMaxCreaturesInCombat, 1, 0, 0);
    for (std::size_t i = 0; i < hostiles.Size(); ++i)
    {
        const ObjectId id = hostiles.Object(i);
        if (fighting(id))
        {
            seen.insert(id);
            out.push_back(Describe(id, Side::Enemy, false));
        }
    }
    // Creatures in combat that are not hostile to the party fight beside it.
    const dao::script::Array others =
        api::GetNearestObjectByHostility(leader, 0, k::OBJECT_TYPE_CREATURE, kMaxCreaturesInCombat, 1, 0, 0);
    for (std::size_t i = 0; i < others.Size(); ++i)
    {
        const ObjectId id = others.Object(i);
        if (fighting(id))
        {
            seen.insert(id);
            out.push_back(Describe(id, Side::Ally, false));
        }
    }
    spdlog::debug("scan: {} in the party, {} hostiles near, {} others near; {} fighting", party.Size(), hostiles.Size(),
                  others.Size(), out.size());
    return out;
}

namespace
{

// Haste and slows, taken together (core/Rules.h, MovementRate): each
// effect's rate is its potency (EffectModifyMovementSpeed, effects_h.nss).
// From the scripts' thread; DAOLib's probe has checked GetEffects in play.
float EffectsRate(ObjectId id)
{
    std::vector<float> potencies;
    for (const std::int32_t type : {k::EFFECT_TYPE_MOVEMENT_RATE, k::EFFECT_TYPE_MOVEMENT_RATE_DEBUFF})
    {
        const dao::script::Array effects = api::GetEffects(id, type);
        for (std::size_t i = 0; i < effects.Size(); ++i)
        {
            if (const dao::CGameEffect *effect = effects.Effect(i))
            {
                potencies.push_back(api::GetEffectFloat(*effect, k::EFFECT_FLOAT_POTENCY));
            }
        }
    }
    return MovementRate(potencies);
}

} // namespace

const Bridge::Known &Bridge::Remember(ObjectId id)
{
    const auto found = known_.find(id);
    if (found != known_.end())
    {
        return found->second;
    }
    // Its pace comes with its appearance, which does not change: its combat
    // speed in the appearance table. Not the scripts' movement speed
    // property, which is the flanking angle in the shipped table (60 for
    // everyone in the first session, 2026-09-30). Haste and slows are
    // effects, not read yet (dev/DAOLIB_NEEDS.md).
    const float combatSpeed =
        dao::gda::GetFloat(dao::script::Table::Appearance, api::GetAppearanceType(id), "Game_CbtSpeed").value_or(0.0f);
    Known known{api::GetName(id), api::GetTag(id), api::GetCreatureRank(id), SpeedFactor(combatSpeed)};
    // Once per creature: what the rules will read of it, to check against
    // the game.
    spdlog::info("{:#010x} {} ({}): rank {}, dexterity {}, combat speed {:.2f} m/s, {:.2f} a person's", id,
                 Utf8(known.name), Utf8(known.tag), known.rank,
                 api::GetCreatureProperty(id, k::PROPERTY_ATTRIBUTE_DEXTERITY, k::PROPERTY_VALUE_TOTAL), combatSpeed,
                 known.speed);
    return known_.emplace(id, std::move(known)).first->second;
}

Combatant Bridge::Describe(ObjectId id, Side side, bool hero)
{
    const Known &known = Remember(id);
    Combatant c;
    c.id = id;
    c.side = side;
    c.hero = hero;
    c.name = known.name;
    c.tag = known.tag;
    c.rank = known.rank;
    c.dexterity = api::GetCreatureProperty(id, k::PROPERTY_ATTRIBUTE_DEXTERITY, k::PROPERTY_VALUE_TOTAL);
    const float rate = EffectsRate(id);
    c.speed = known.speed * rate;
    if (rate != 1.0f)
    {
        spdlog::info("{} moves at {:.2f} a person's pace: {:.2f} from its build, {:.2f} from haste or slows",
                     Utf8(known.name), c.speed, known.speed, rate);
    }
    c.health = Health(id);
    c.down = api::IsDead(id) != 0;
    return c;
}

ActorObservation Bridge::Actor(ObjectId id)
{
    ActorObservation actor;
    actor.id = id;
    actor.exists = api::IsObjectValid(id) != 0;
    if (!actor.exists)
    {
        return actor;
    }
    actor.down = api::IsDead(id) != 0;
    if (const dao::script::OwnedCommand current = api::GetCurrentCommand(id))
    {
        actor.current = static_cast<CommandType>(api::GetCommandType(*current.get()));
    }
    actor.busy = actor.current != CommandType::Invalid || api::GetCommandQueueSize(id) > 0;
    actor.moving = api::IsMoving(id) != 0;
    const dao::Vector position = api::GetPosition(id);
    actor.position = {position.x, position.y, position.z};
    return actor;
}

CombatantStatus Bridge::Status(ObjectId id)
{
    CombatantStatus status;
    status.id = id;
    status.exists = api::IsObjectValid(id) != 0;
    if (status.exists)
    {
        status.down = api::IsDead(id) != 0;
        status.health = Health(id);
        // The conditions that hold a creature for turns: the effects the
        // game's stuns, holds and sleeps apply (EffectStun, effect_stun_h;
        // Pinning Shot's EffectRoot, talent_singletarget.nss).
        if (api::GetHasEffects(id, k::EFFECT_TYPE_STUN) != 0 || api::GetHasEffects(id, k::EFFECT_TYPE_PARALYZE) != 0 ||
            api::GetHasEffects(id, k::EFFECT_TYPE_SLEEP) != 0)
        {
            status.condition = Condition::Disabled;
        }
        else if (api::GetHasEffects(id, k::EFFECT_TYPE_ROOT) != 0)
        {
            status.condition = Condition::Rooted;
        }
    }
    return status;
}

void Bridge::Apply(const std::vector<Effect> &effects)
{
    for (const Effect &e : effects)
    {
        spdlog::debug("effect: {} {:#010x}", Name(e.kind), e.target);
        switch (e.kind)
        {
        case EffectKind::Pause:
        case EffectKind::Unpause:
            if (auto *app = dao::CClientExoApp::Get())
            {
                // `second` as the pause key passes it (DAOLib's
                // dev/TURN_BASED.md; what it means is not read yet).
                app->SetPaused(e.kind == EffectKind::Pause, true);
            }
            break;
        case EffectKind::ClearCommands:
            api::ClearAllCommands(e.target, 1);
            break;
        case EffectKind::Select:
            api::SetPrimaryControlled(e.target);
            break;
        case EffectKind::PokeAi:
            Poke(e.target);
            break;
        case EffectKind::Explain:
            Explain(e.target);
            break;
        case EffectKind::HoldTime:
            // Every temporary effect's clock (DAOLib's dao/Effects.h): held
            // through a fight, moved on a round's worth at a time.
            dao::effects::Hold(e.a != 0);
            break;
        case EffectKind::AgeTime:
            dao::effects::Advance(static_cast<float>(e.b) / 1000.0f);
            break;
        case EffectKind::Cooldown:
            // Held, released, or back to seconds (core: "Cooldowns in turns").
            api::SetCooldown(e.target, e.a, static_cast<float>(e.b));
            break;
        case EffectKind::EndConditions:
            // Their turns are spent (core: "Conditions in turns"): the
            // engine's own timers are not waited for.
            for (const std::int32_t type :
                 {k::EFFECT_TYPE_STUN, k::EFFECT_TYPE_PARALYZE, k::EFFECT_TYPE_SLEEP, k::EFFECT_TYPE_ROOT})
            {
                api::RemoveEffectsByParameters(e.target, type);
            }
            break;
        case EffectKind::Floaty:
            api::DisplayFloatyMessage(e.target, e.text, k::FLOATY_MESSAGE, static_cast<std::int32_t>(e.colour), 2.0f);
            break;
        case EffectKind::Signal:
            Signal(e);
            break;
        case EffectKind::CenterCamera:
            camera_.TurnTo(e.target);
            break;
        }
    }
}

void Bridge::Poke(ObjectId id)
{
    // Processed at once (bProcessImmediate): the creature's scripts run
    // now, inside this call, and whatever its AI queues comes back through
    // the gate while it is the creature's turn (dev/DESIGN.md, "The AI's
    // turn").
    if (settings_.aiPoke == AiPoke::Kickstart)
    {
        if (const dao::script::OwnedEvent event = api::Event(kKickstartAi))
        {
            api::SignalEvent(id, *event.get(), 1);
        }
        return;
    }
    // What the engine sends when a command ends: the creature's own script
    // sees it first, then rules_core, which runs its AI (custom AI too).
    // A wait that succeeded, sub-command -1 (rules_core reads ints 0-2).
    dao::script::OwnedEvent event = api::Event(k::EVENT_TYPE_COMMAND_COMPLETE);
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 0, k::COMMAND_TYPE_WAIT);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 1, k::COMMAND_SUCCESSFUL);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 2, -1);
    }
    if (event)
    {
        api::SignalEvent(id, *event.get(), 1);
    }
    else
    {
        spdlog::warn("could not make the event to tell {:#010x} its turn has come", id);
    }
}

// What an AI saw when it chose nothing on its turn: the conditions under
// which rules_core and AI_DetermineCombatRound return without a command
// (rules_core.nss, EVENT_TYPE_COMMAND_COMPLETE; ai_main_h_2.nss), and the
// effects that disable a creature. Read in play, it says why.
void Bridge::Explain(ObjectId id)
{
    std::int32_t current = k::COMMAND_TYPE_INVALID;
    if (const dao::script::OwnedCommand command = api::GetCurrentCommand(id))
    {
        current = api::GetCommandType(*command.get());
    }
    spdlog::info("{} ({:#010x}) chose nothing: combat state {}, disabled flags {:#x}, stun {} paralyze {} "
                 "knockdown {} daze {} sleep {}, current command {}, {} queued, light AI {}, custom AI {}",
                 Utf8(Remember(id).name), id, api::GetCombatState(id), api::GetEffectsFlags(id),
                 api::GetHasEffects(id, k::EFFECT_TYPE_STUN), api::GetHasEffects(id, k::EFFECT_TYPE_PARALYZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_KNOCKDOWN), api::GetHasEffects(id, k::EFFECT_TYPE_DAZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_SLEEP), current, api::GetCommandQueueSize(id),
                 api::GetLocalInt(id, L"AI_LIGHT_ACTIVE"), api::GetLocalInt(id, L"AI_CUSTOM_AI_ACTIVE"));
}

void Bridge::Signal(const Effect &effect)
{
    dao::script::OwnedEvent event = api::Event(effect.event);
    if (event && effect.target != kNoObject)
    {
        event = api::SetEventObject(*event.get(), 0, effect.target);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 0, effect.a);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 1, effect.b);
    }
    if (event)
    {
        api::SignalEvent(api::GetModule(), *event.get(), 0);
    }
}

bool Bridge::PartyInCombat()
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady())
    {
        return false;
    }
    const auto mode = static_cast<Mode>(app->Mode());
    if (mode == Mode::Combat)
    {
        return true;
    }
    if (OutOfWorld(mode))
    {
        return false;
    }
    // module_core.nss sets the party's combat state on every change of mode
    // but a menu's (SetCombatStateParty), so it still says combat under one.
    const ObjectId hero = api::GetHero();
    return api::IsObjectValid(hero) != 0 && api::GetCombatState(hero) != 0;
}

void Bridge::Say(const std::wstring &text, std::uint32_t colour)
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady() ||
        OutOfWorld(static_cast<Mode>(app->Mode())))
    {
        return;
    }
    const ObjectId hero = api::GetHero();
    if (api::IsObjectValid(hero) != 0)
    {
        api::DisplayFloatyMessage(hero, text, k::FLOATY_MESSAGE, static_cast<std::int32_t>(colour), 2.0f);
    }
}

} // namespace tb::game
