#include "game/Bridge.h"

#include "game/Camera.h"
#include "game/Gate.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/Abilities.h"
#include "dao/ClientExoApp.h"
#include "dao/Clock.h"
#include "dao/Conjure.h"
#include "dao/Effects.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/ScriptHooks.h"
#include "dao/Timers.h"
#include "dao/World.h"
#include "dao/core/CommandWord.h"
#include "dao/generated/ScriptApi.h"
#include "dao/generated/ScriptConstants.h"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

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
// A charged cast's conjuring when it is given at last: a moment for the
// gesture, the turns it charged having been its casting time.
constexpr float kChargedConjureSeconds = 1.0f;

// The game's mode as the core names it; none yet (no game modes) is
// Unknown, as before the world (DAOLib 3c8cccf made it an optional).
Mode ModeOf(const dao::CClientExoApp &app)
{
    const auto mode = app.Mode();
    return mode ? static_cast<Mode>(*mode) : Mode::Unknown;
}

bool OutOfWorld(Mode mode)
{
    return mode == Mode::Loading || mode == Mode::Pregame || mode == Mode::CharGen || mode == Mode::Unknown;
}

// Mana or stamina, as the party bar's right arc shows it: the one
// depletable property both are (PROPERTY_DEPLETABLE_MANA_STAMINA).
float Energy(ObjectId id)
{
    const float current = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_MANA_STAMINA, k::PROPERTY_VALUE_CURRENT);
    const float total = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_MANA_STAMINA, k::PROPERTY_VALUE_TOTAL);
    return total > 0.0f ? std::clamp(current / total, 0.0f, 1.0f) : 0.0f;
}

// A mage's is mana: its core class, as core_h's GetCreatureCoreClass reads
// it (the current class, or the classes table's BaseClass for a
// specialisation's), is CLASS_WIZARD.
bool Mage(ObjectId id)
{
    constexpr std::int32_t kPropertyCurrentClass = 27; // PROPERTY_SIMPLE_CURRENT_CLASS
    constexpr std::int32_t kClassWizard = 2;           // CLASS_WIZARD
    const auto current =
        static_cast<std::int32_t>(api::GetCreatureProperty(id, kPropertyCurrentClass, k::PROPERTY_VALUE_TOTAL));
    const std::int32_t base = dao::gda::GetInt(dao::script::Table::RulesClasses, current, "BaseClass").value_or(0);
    return (base != 0 ? base : current) == kClassWizard;
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
    obs.mode = ModeOf(*app);
    obs.paused = app->IsPaused();
    if (OutOfWorld(obs.mode))
    {
        // Ids are the save's: another game can reuse them.
        known_.clear();
        keptVoid_.clear();
        return obs;
    }
    obs.keptVoid = std::exchange(keptVoid_, {});
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

namespace
{
// Whether a creature in combat near the party is one the player can see
// fighting (core: Combatant::engaged), by the game's own answers. Not by
// distance: Howe's men fighting guards rooms away were in the turn order,
// and the camera went to rooms drawn black (the user, 2026-09-30, whose own
// rule was to leave out those in areas that are not loaded).
// - It or a party member is the other's attack target.
// - Else, it stands in a room the game shows (dao::world::RoomOf, DAOLib
//   1d99f54). An area is made of rooms, and the game hides every room no
//   party member has yet seen, its creatures inactive, even through an
//   open door: in lib-agent's play test the room behind a door stayed
//   black, door open, until the hero walked in. What is seen goes by the
//   party, not the camera, so the mod's camera going there shows nothing.
//   A line of sight alone, the first rule here, is clear through that
//   door. The room's `seen` is read from the function that hides rooms,
//   not yet watched in play.
// - Where the area has no room for it, a party member's line of sight to
//   it (CheckLineOfSightObject's question: walls and doors block).
bool Engaged(ObjectId id, const std::vector<ObjectId> &party, const std::optional<dao::world::Room> &room)
{
    const ObjectId target = api::GetAttackTarget(id);
    for (const ObjectId member : party)
    {
        if (target == member || api::GetAttackTarget(member) == id)
        {
            return true;
        }
    }
    if (room)
    {
        return room->Shown();
    }
    return std::ranges::any_of(party,
                               [&](ObjectId member) { return static_cast<bool>(dao::world::LineOfSight(member, id)); });
}
} // namespace

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
    const std::vector<ObjectId> members(seen.begin(), seen.end());
    std::size_t outside = 0;
    // Those the player cannot see fighting, each with its room as the game
    // has it (seen: 0 never, 1 in view, 2 before; load: 0 loading, 1
    // loaded, 2 not), for a play test to set beside what the screen shows.
    std::string rooms;
    // The rooms are gone by only where they can be: the leader's own room,
    // which the game shows if it shows any, must read as shown. An area
    // with rooms where the game never marks one seen would read every
    // creature as hidden, and no fight there would ever be in turns
    // (lib-agent's caution, 2026-10-01: not seen, and this would say). A
    // party member's line of sight is the rule there, as where an area has
    // no rooms.
    const std::optional<dao::world::Room> ours = settings_.joinBySight ? dao::world::RoomOf(leader) : std::nullopt;
    const bool byRooms = ours && ours->Shown();
    // Said when it changes, at info: which rule an area goes by is read
    // out of a player's own log. Outdoors (the Dalish forest, 2026-10-01,
    // by Eluvian's inspect) every creature's room, the party's own too,
    // reads in view and not loaded, so sight is the rule there.
    if (settings_.joinBySight && byRooms_ != byRooms)
    {
        byRooms_ = byRooms;
        if (byRooms)
        {
            spdlog::info("who is in a fight here goes by the rooms the party has seen");
        }
        else if (ours)
        {
            spdlog::info("who is in a fight here goes by sight: the leader's own room {} reads seen {} load {}, not "
                         "shown",
                         ours->number, ours->seen, ours->load);
        }
        else
        {
            spdlog::info("who is in a fight here goes by sight: the area has no rooms");
        }
    }
    const auto add = [&](ObjectId id, Side side) {
        Combatant c = Describe(id, side, false);
        if (settings_.joinBySight)
        {
            const std::optional<dao::world::Room> room = byRooms ? dao::world::RoomOf(id) : std::nullopt;
            c.engaged = Engaged(id, members, room);
            if (!c.engaged)
            {
                ++outside;
                if (spdlog::should_log(spdlog::level::debug))
                {
                    rooms +=
                        room ? std::format(" {:#x} room {} seen {} load {}", id, room->number, room->seen, room->load)
                             : std::format(" {:#x} no room, out of sight", id);
                }
            }
        }
        out.push_back(std::move(c));
    };
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
            add(id, Side::Enemy);
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
            add(id, Side::Ally);
        }
    }
    spdlog::debug("scan: {} in the party, {} hostiles near, {} others near; {} fighting, {} of them not the "
                  "party{}{}",
                  party.Size(), hostiles.Size(), others.Size(), out.size(), outside, rooms.empty() ? "" : ":", rooms);
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

// A skill's rank, 0 to 4: the highest of its four abilities the creature
// has. The skills' abilities are numbered by rank from the first
// (ABILITY_SKILL_COMBAT_TACTICS_1 100110 to _4 100113, ABILITY_SKILL_
// SURVIVAL_1 100051 to _4 100054; 2da_constants_h.nss, read 2026-10-02).
constexpr std::int32_t kCombatTactics1 = 100110;
constexpr std::int32_t kSurvival1 = 100051;

int SkillRank(ObjectId id, std::int32_t first)
{
    constexpr int kTopRank = 4;
    for (int rank = kTopRank; rank >= 1; --rank)
    {
        if (api::HasAbility(id, first + rank - 1) != 0)
        {
            return rank;
        }
    }
    return 0;
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
    // Its skills' ranks (with a turn a bar of time, Combat Tactics' time
    // and rollover; Survival's initiative), which a fight does not change.
    Known known{api::GetName(id),
                api::GetTag(id),
                api::GetCreatureRank(id),
                SpeedFactor(combatSpeed),
                SkillRank(id, kCombatTactics1),
                SkillRank(id, kSurvival1)};
    // Once per creature: what the rules will read of it, to check against
    // the game.
    spdlog::info("{:#010x} {} ({}): rank {}, dexterity {}, combat speed {:.2f} m/s, {:.2f} a person's, Combat "
                 "Tactics {}, Survival {}",
                 id, Utf8(known.name), Utf8(known.tag), known.rank,
                 api::GetCreatureProperty(id, k::PROPERTY_ATTRIBUTE_DEXTERITY, k::PROPERTY_VALUE_TOTAL), combatSpeed,
                 known.speed, known.tactics, known.survival);
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
    c.tactics = known.tactics;
    c.survival = known.survival;
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
    actor.conjureHeld = dao::commands::ConjureHeld(id) && dao::commands::Conjure(id).has_value();
    return actor;
}

namespace
{
// The effects that hold a creature for turns, and that the machine ends
// once their turns are spent (Status, EndConditions).
constexpr std::array kHolding{k::EFFECT_TYPE_STUN, k::EFFECT_TYPE_PARALYZE, k::EFFECT_TYPE_SLEEP, k::EFFECT_TYPE_ROOT,
                              k::EFFECT_TYPE_KNOCKDOWN};
constexpr std::array<const char *, kHolding.size()> kHoldingNames{"stun", "paralysis", "sleep", "pin", "knockdown"};

// Which of them hold a creature, by name: "pin, stun", or "nothing".
std::string Holding(ObjectId id)
{
    std::string out;
    for (std::size_t i = 0; i < kHolding.size(); ++i)
    {
        if (api::GetHasEffects(id, kHolding[i]) != 0)
        {
            out += out.empty() ? "" : ", ";
            out += kHoldingNames[i];
        }
    }
    return out.empty() ? "nothing" : out;
}
} // namespace

CombatantStatus Bridge::Status(ObjectId id)
{
    CombatantStatus status;
    status.id = id;
    status.exists = api::IsObjectValid(id) != 0;
    if (status.exists)
    {
        status.down = api::IsDead(id) != 0;
        status.health = Health(id);
        status.energy = Energy(id);
        status.mana = Mage(id);
        // The conditions that hold a creature for turns: the effects whose
        // command the engine keeps at the front of its queue while they
        // last (EffectStun, effect_stun_h; Pinning Shot's EffectRoot,
        // talent_singletarget.nss; Shield Bash's EffectKnockdown). Daze is
        // only a penalty (effect_daze_h). Seen for stuns and pins
        // 2026-09-30; knockdowns not yet.
        for (const std::int32_t type : kHolding)
        {
            if (api::GetHasEffects(id, type) != 0)
            {
                status.condition = Condition::Disabled;
                break;
            }
        }
        // The seconds it has left, the longest of what holds it: with a
        // turn a bar of time a hold takes its own time of its victim's
        // turns (core: Held::owedMs). DAOLib's dao::effects::On (a893f20),
        // first used here: not yet seen in play.
        if (status.condition == Condition::Disabled)
        {
            for (const dao::effects::Standing &effect : dao::effects::On(id))
            {
                if (std::ranges::find(kHolding, static_cast<std::int32_t>(effect.type)) != kHolding.end())
                {
                    status.conditionSeconds = std::max(status.conditionSeconds, effect.remaining);
                }
            }
        }
    }
    return status;
}

void Bridge::Apply(const std::vector<Effect> &effects)
{
    for (const Effect &e : effects)
    {
        spdlog::debug("effect: {} {:#010x} ({}, {})", Name(e.kind), e.target, e.a, e.b);
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
        case EffectKind::ReleaseCast:
            if (ReleaseEngineConjure(e.target))
            {
                break; // the engine's own cast, let complete (Gate.cpp): nothing kept to give
            }
            // The cast its creature charged (core: "Casts that charge"),
            // given to it as a script gives a command: it comes back
            // through the gate, which lets it through for nothing. Its
            // conjuring is short now, the turns having been its time.
            if (const std::optional<HeldCast> kept = TakeHeldCast(e.target);
                kept && kept->target != dao::kInvalidObject &&
                (api::IsObjectValid(kept->target) == 0 || api::IsDead(kept->target) != 0))
            {
                // Its target fell while it charged: not cast at nothing. In
                // play the cast was given all the same, struck nothing, and
                // put the talent on cooldown for five turns (the twelfth
                // play test, 2026-10-01). Not given, nothing is spent: the
                // machine says its cast did not come, and the turn goes on.
                spdlog::info(
                    "{:#010x}: the target of its charged cast of ability {} ({:#x}) is gone: not cast, nothing "
                    "spent",
                    e.target, kept->ability, kept->target);
                api::DisplayFloatyMessage(e.target, L"Its target is gone", k::FLOATY_MESSAGE,
                                          static_cast<std::int32_t>(kGrey), 2.0f);
                // Told to the machine with the next observation: with a turn
                // a bar of time, what the turn paid for it is given back.
                keptVoid_.push_back(e.target);
            }
            else if (kept && kept->type == CommandType::Attack)
            {
                // With a turn a bar of time, an attack kept for the turn its
                // time is paid on (core: TurnMachine::PayAction), given as a
                // script gives one.
                if (kept->target == dao::kInvalidObject)
                {
                    spdlog::warn("{:#010x}: its kept attack has no target: not given", e.target);
                }
                else if (const dao::script::OwnedCommand attack = api::CommandAttack(kept->target))
                {
                    api::AddCommand(e.target, *attack.get(), 1);
                    spdlog::debug("{:#010x}: its kept attack on {:#x} is given to it", e.target, kept->target);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the command for its kept attack", e.target);
                }
            }
            else if (kept)
            {
                const dao::Vector place = kept->placed ? dao::Vector{kept->x, kept->y, kept->z} : dao::Vector{};
                if (const dao::script::OwnedCommand cast =
                        api::CommandUseAbility(kept->ability, kept->target, place, kChargedConjureSeconds))
                {
                    api::AddCommand(e.target, *cast.get(), 1);
                    spdlog::debug("{:#010x}: its charged cast of ability {} is given to it", e.target, kept->ability);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the command for its charged cast of ability {}", e.target,
                                 kept->ability);
                }
            }
            else
            {
                spdlog::warn("{:#010x}: no cast kept for it to be given (ability {})", e.target, e.a);
            }
            break;
        case EffectKind::DropCast:
            DropHeldCast(e.target);
            (void)DropEngineConjure(e.target);
            break;
        case EffectKind::Approach:
            // With a turn a bar of time, an attack that did not fit is kept
            // for its creature's next turn, and the creature walks to where
            // it can strike now (core: TurnMachine::PayAction): a move to
            // its target, given as a script gives one, to end inside the
            // reach its attack will be judged by.
            if (const std::optional<HeldCast> kept = PeekHeldCast(e.target);
                kept && kept->target != dao::kInvalidObject && api::IsObjectValid(kept->target) != 0)
            {
                constexpr float kInside = 0.3f; // meters inside the reach, so that the attack need not step
                const float within = std::max(0.5f, ReachOf(e.target, kept->target) - kInside);
                if (const dao::script::OwnedCommand walk = api::CommandMoveToObject(kept->target, 1, within))
                {
                    api::AddCommand(e.target, *walk.get(), 0);
                    spdlog::debug("{:#010x}: it walks to within {:.1f} m of {:#x}, where its kept action lands",
                                  e.target, within, kept->target);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the walk to its kept action's target", e.target);
                }
            }
            else
            {
                spdlog::debug("{:#010x}: no kept action with a target to walk to", e.target);
            }
            break;
        case EffectKind::Explain:
            Explain(e.target);
            break;
        case EffectKind::HoldTime:
            // The clock temporary effects wear off on and regeneration ticks
            // on (DAOLib's dao/Clock.h, a42be67): held through a fight,
            // moved on a round's worth at a time.
            dao::clock::Hold(e.a != 0);
            if (e.a != 0)
            {
                ReadInstalledRules(); // what the installed scripts count (Gate.cpp)
            }
            break;
        case EffectKind::OwnClock:
            // A combatant's effects, regeneration and the scripts' timers on
            // it on a clock of its own (DAOLib's dao/Clock.h, 04b3be3), moved
            // by the targeted AgeTime below as its turn is spent; what is not
            // in the fight stays on the shared one. Given back as the hold is
            // let go.
            dao::clock::OwnClock(e.target, e.a != 0);
            break;
        case EffectKind::GameSpeed:
            // Game logic and animation together (DAOLib's dao/Clock.h,
            // 4e0032e); the Flash UI keeps its own clock.
            // Whether the world's timer took it and the frame's hook is placed
            // (DAOLib ed55ab8): an archer's shot took as long at 2 as at 1 in
            // the second play test, the mod's own waits aside.
            speedTook_ = dao::clock::SetSpeed(static_cast<float>(e.b) / 1000.0f);
            camera_.SetSpeed(static_cast<float>(e.b) / 1000.0f);
            if (!*speedTook_)
            {
                spdlog::warn("the game's speed {:.2f} did not take", static_cast<float>(e.b) / 1000.0f);
            }
            else
            {
                spdlog::debug("the game's speed is {:.2f}", static_cast<float>(e.b) / 1000.0f);
            }
            break;
        case EffectKind::AgeTime:
            if (e.target != kNoObject)
            {
                // One creature's own clock (core: AgeOwnClock), in steps as
                // small as a frame's walk: DAOLib keeps the fractions.
                dao::clock::Advance(e.target, static_cast<float>(e.b) / 1000.0f);
                break;
            }
            // With the effects and regeneration, since DAOLib 48fe011, the
            // scripts' own timers that pace them (dao/Timers.h): a damage
            // over time's ticks and a lasting area's pulses wait while the
            // clock is held and come due as it is moved, where before they
            // ran on in real time (the user, playing, 2026-10-02: "there's a
            // environmental poison trap that keeps hitting the party even
            // during turn-based combat"). How many wait is said, to read a
            // fight's ticks against its rounds.
            dao::clock::Advance(static_cast<float>(e.b) / 1000.0f);
            if (const std::size_t waiting = dao::timers::Waiting(); waiting > 0)
            {
                spdlog::debug("the clock moved {:.2f} s: {} of the scripts' timers wait on it",
                              static_cast<float>(e.b) / 1000.0f, waiting);
            }
            break;
        case EffectKind::Cooldown:
            // Held, released, or back to seconds (core: "Cooldowns in turns").
            // With what the sweep is drawn against (c), through DAOLib
            // (dao/Abilities.h, b51d676): the scripts' SetCooldown sets what
            // is left and what it was set to as one, so a hold showed a full
            // sweep that never moved, where the user wants the turns shown.
            // DAOLib's refuses a creature with no record for the ability
            // (an item's, by its tag): the scripts' then, as before.
            if (e.c <= 0 ||
                !dao::abilities::SetCooldown(e.target, e.a, static_cast<float>(e.b), static_cast<float>(e.c)))
            {
                if (e.c > 0)
                {
                    spdlog::debug(
                        "ability {} of {:#010x}: no cooldown record to set {} of {} s on; set as the scripts do", e.a,
                        e.target, e.b, e.c);
                }
                api::SetCooldown(e.target, e.a, static_cast<float>(e.b));
            }
            break;
        case EffectKind::EndConditions:
            // Their turns are spent (core: "Conditions in turns"): the
            // engine's own timers are not waited for.
            {
                // Said before and after: the desktop play test (2026-09-30) saw
                // a pin the machine ended hold its creature still.
                const std::string before = Holding(e.target);
                for (const std::int32_t type : kHolding)
                {
                    api::RemoveEffectsByParameters(e.target, type);
                }
                const std::string after = Holding(e.target);
                if (after != "nothing")
                {
                    spdlog::warn("{:#010x}: ended {}, and {} holds it still", e.target, before, after);
                }
                else
                {
                    spdlog::debug("{:#010x}: ended {}", e.target, before);
                }
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
    const auto mode = ModeOf(*app);
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
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady() || OutOfWorld(ModeOf(*app)))
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
