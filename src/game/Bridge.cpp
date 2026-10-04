#include "game/Bridge.h"

#include "game/Camera.h"
#include "game/Gate.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/Abilities.h"
#include "dao/Attack.h"
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
#include <limits>
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
// An attack command's approach, by the type of the sub-action it queues
// first (DAOLib's reading of the exe, 2026-10-04, dao/Attack.h and dao/
// Conjure.h): 4 for a blow, a walk to the attacker's place on its target's
// melee ring, and 15 for a shot (a bow, a crossbow, a staff), a walk to
// within range and sight. Each ends at once when its creature is in reach
// already; the attack itself comes after (50, and the swing, 1).
constexpr std::uint32_t kMeleeApproach = 4;
constexpr std::uint32_t kRangedApproach = 15;

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

bool Gone(ObjectId id)
{
    return api::IsObjectValid(id) == 0 || api::IsDead(id) != 0;
}

// One a kept attack might be turned on, its own target fallen, as the game
// has it now (core/Rules.h, Foe; issue 22):
// - there and alive;
// - hostile to the attacker by the game's own word: the fight's sides are
//   the party's view, and one fighting beside the party need not be an
//   enemy's enemy;
// - seen: not in stealth (the effect the scripts' IsStealthy asks for; its
//   AI could not have chosen it either), and with a clear line from the
//   attacker (the scripts' CheckLineOfSightObject's question, DAOLib's
//   dao::world::LineOfSight: walls and doors block). A shot with no line is
//   not loosed (issue 11), and a blow at one behind a door is a walk round;
// - how far, and whether in the attacker's reach: the engine's own answer
//   for the pair (DAOLib's dao::attack::ApproachOf, what the gate judges an
//   attack's walk by, so "in reach" here is a walk of none there). And
//   whether the engine's attack will find room on its ring (the approach's
//   findsRoom, DAOLib issue 19): in reach is by the ring as it stands, and
//   in a crowd a blow that does not come in its first update meets the ring
//   filled a frame on, with no place left (issue 27). A kept attack is
//   asked of the engine again as it is given (the gate; core's
//   KeptAttackNotMade), and one turned on a foe it finds no room at would
//   be not made there, where another in reach might have had it: so it is
//   turned only on one it will land on.
// Not yet seen in play.
Foe Measure(ObjectId attacker, ObjectId other)
{
    Foe foe;
    foe.id = other;
    foe.standing = !Gone(other);
    if (!foe.standing)
    {
        return foe;
    }
    foe.hostile = api::IsObjectHostile(attacker, other) != 0;
    foe.meters = api::GetDistanceBetween(attacker, other);
    const std::optional<dao::attack::Approach> approach = dao::attack::ApproachOf(attacker, other);
    foe.inReach = approach && approach->inReach && approach->findsRoom;
    if (approach && approach->inReach && !approach->findsRoom)
    {
        // Said apart: the line of each one measured reads it as out of reach.
        spdlog::debug("{:#010x}: {:#x} is in its reach, and the engine's attack would find no room at it: not one "
                      "to turn a kept attack on",
                      attacker, other);
    }
    foe.seen = api::GetHasEffects(other, k::EFFECT_TYPE_STEALTH) == 0 &&
               static_cast<bool>(dao::world::LineOfSight(attacker, other));
    return foe;
}

// Which of those the machine named a kept attack is turned on (core's
// NearestInReach decides), each as it was measured said at debug: why one
// was passed over is read there.
ObjectId TurnedOn(ObjectId attacker, const std::vector<ObjectId> &others)
{
    std::vector<Foe> foes;
    foes.reserve(others.size());
    for (const ObjectId other : others)
    {
        const Foe &foe = foes.emplace_back(Measure(attacker, other));
        spdlog::debug("{:#010x}: its kept attack on {:#x}? {}{:.1f} m away, {} its reach, {}, {}", attacker, other,
                      foe.standing ? "" : "fallen; ", foe.meters, foe.inReach ? "in" : "out of",
                      foe.hostile ? "hostile" : "not hostile", foe.seen ? "seen" : "not seen");
    }
    return NearestInReach(foes);
}

// An attack given as a script gives one: it comes back through the gate.
bool GiveAttack(ObjectId attacker, ObjectId target)
{
    const dao::script::OwnedCommand attack = api::CommandAttack(target);
    if (!attack)
    {
        return false;
    }
    api::AddCommand(attacker, *attack.get(), 1);
    return true;
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
        party_.clear();
        return obs;
    }
    obs.keptVoid = std::exchange(keptVoid_, {});
    if (needs.scan)
    {
        obs.scan = Scan(needs.inFight);
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
// And how far it is from the nearest party member: seen is not enough out
// of doors, where every room in the radius is shown and a battle of wolves
// and darkspawn 45 m off joined the party's (issue 6). What these make of
// it is core's (InThePartysFight).
Sighting Sight(ObjectId id, const std::vector<ObjectId> &party, const std::optional<dao::world::Room> &room)
{
    Sighting sighting;
    const ObjectId target = api::GetAttackTarget(id);
    sighting.meters = std::numeric_limits<float>::infinity();
    for (const ObjectId member : party)
    {
        if (target == member || api::GetAttackTarget(member) == id)
        {
            sighting.fightsParty = true;
        }
        sighting.meters = std::min(sighting.meters, api::GetDistanceBetween(member, id));
    }
    sighting.seen = room ? room->Shown() : std::ranges::any_of(party, [&](ObjectId member) {
        return static_cast<bool>(dao::world::LineOfSight(member, id));
    });
    return sighting;
}

// What the engine's perception says of a creature that is not the party's
// (core: Perception; issue 29): whether a party member perceives it, whether
// it perceives a party member, and whether it perceives anyone hostile to
// it. The engine's own lists, asked through the scripts' calls: IsPerceiving
// (A, B) is whether B is on A's perception list, "a cheap check ... it does
// not perform expensive line of sight checking" (script.ldf), and
// IsPerceivingHostiles is the test the game's own scripts take a creature
// out of combat by (combat_h.nss, Combat_HandleCreatureDisappear). What a
// creature has noticed is the engine's to say; the mod keeps no distance or
// line of sight of its own for it. Not yet seen in play.
Perception Perceived(ObjectId id, const std::vector<ObjectId> &party)
{
    Perception perception;
    perception.byParty =
        std::ranges::any_of(party, [&](ObjectId member) { return api::IsPerceiving(member, id) != 0; });
    perception.ofParty =
        std::ranges::any_of(party, [&](ObjectId member) { return api::IsPerceiving(id, member) != 0; });
    perception.ofFoes = api::IsPerceivingHostiles(id) != 0;
    // And the game's own word for its having noticed an enemy: its combat
    // state, which the scripts set as it perceives a hostile and run its
    // combat AI by (rules_core.nss).
    perception.inCombat = api::GetCombatState(id) != 0;
    return perception;
}
} // namespace

// Who is fighting near the party, by the rules that bring a creature into a
// fight; and, of those the machine has in the fight already (inFight), the
// living ones those rules no longer find, each with what the engine's
// perception says of it, which is what keeps it in the fight or does not
// (core: StaysInTheFight; issue 29).
std::vector<Combatant> Bridge::Scan(const std::vector<ObjectId> &inFight)
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
    party_ = members; // for the acting creature's perception, read every frame (Actor)
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
    const auto add = [&](ObjectId id, Side side, bool unaware = false) {
        Combatant c = Describe(id, side, false);
        // What it has noticed, and whether the party has noticed it, by the
        // engine's perception: a hostile that perceives nobody to fight has
        // its turn skipped (core: Unaware). Said at debug for one that
        // perceives nobody, each scan: a play test reads there when it
        // noticed.
        c.perception = Perceived(id, members);
        if (!c.perception.ofParty && !c.perception.ofFoes)
        {
            spdlog::debug("{:#x} perceives no party member and nobody hostile to it, and is {} combat; the party "
                          "{} it",
                          id, c.perception.inCombat ? "in" : "not in",
                          c.perception.byParty ? "perceives" : "does not perceive");
        }
        // One drawn in though it has not noticed is in the party's fight by
        // the rule that drew it in (seen, within the join radius).
        if (settings_.joinBySight && !unaware)
        {
            const std::optional<dao::world::Room> room = byRooms ? dao::world::RoomOf(id) : std::nullopt;
            const Sighting sighting = Sight(id, members, room);
            c.engaged = InThePartysFight(sighting, settings_);
            if (!c.engaged)
            {
                ++outside;
                if (spdlog::should_log(spdlog::level::debug))
                {
                    // Seen and still outside: fighting someone else, too far
                    // off to be the party's.
                    rooms +=
                        sighting.seen
                            ? std::format(" {:#x} seen, {:.0f} m off, not fighting the party", id, sighting.meters)
                        : room ? std::format(" {:#x} room {} seen {} load {}", id, room->number, room->seen, room->load)
                               : std::format(" {:#x} no room, out of sight", id);
                }
            }
        }
        out.push_back(std::move(c));
    };
    // The game's own question for who is fighting the party (UT_Surrender,
    // utility_h.nss): the nearest living hostiles, those in combat.
    // combat_radius is how far from the leader a fight takes a creature in
    // from. It takes nobody out any more: one in the fight that this no
    // longer finds is read further down, and stays by the engine's
    // perception (issue 29).
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
    // And a hostile that has not noticed the party, with the party in combat
    // and nobody in its fight to fight (core: FightsUnawares; issues 20 and
    // 24): seen and within the join radius it is in the fight, and stays in
    // it once drawn in. Sight is a party member's line here whatever the
    // rooms say: a room the game shows may hold a hostile behind a pillar,
    // and this one is not fighting anyone to be seen by.
    const bool partyInCombat = api::GetCombatState(leader) != 0 || api::GetCombatState(hero) != 0;
    const bool someoneToFight =
        std::ranges::any_of(out, [](const Combatant &c) { return c.side == Side::Enemy && c.engaged; });
    const auto unawares = [&](ObjectId id) {
        if (!partyInCombat || api::IsObjectValid(id) == 0 || seen.contains(id) || api::GetCombatState(id) != 0 ||
            api::IsDead(id) != 0 || api::GetDistanceBetween(leader, id) > settings_.combatRadius)
        {
            return false;
        }
        const bool drawnIn = unawares_.contains(id);
        // The lines of sight are asked only where the answer hangs on them.
        Sighting sighting;
        if (!drawnIn && !someoneToFight)
        {
            sighting = Sight(id, members, std::nullopt);
        }
        return FightsUnawares(partyInCombat, someoneToFight, drawnIn, sighting, settings_);
    };
    std::unordered_set<ObjectId> drawn;
    for (std::size_t i = 0; i < hostiles.Size(); ++i)
    {
        const ObjectId id = hostiles.Object(i);
        if (!unawares(id))
        {
            continue;
        }
        seen.insert(id);
        add(id, Side::Enemy, true);
        if (!unawares_.contains(id))
        {
            spdlog::info("{} ({:#x}) has not noticed the party and is in sight within {:.0f} m, with nobody else to "
                         "fight: in the fight",
                         Utf8(Remember(id).name), id, settings_.joinRadius);
        }
        drawn.insert(id);
    }
    unawares_ = std::move(drawn); // who is in the fight unawares now: each said once, and kept in while it is
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
    // Those the machine has in the fight that nothing above found (issue
    // 29): out of combat, or further than combat_radius from the leader.
    // Until 2026-10-04 that was the end of them: left out of the scan, they
    // were out of the fight four seconds later, and stood where they were as
    // any creature outside the order does, whoever they were chasing. The
    // game itself keeps a fight by perception, not by a distance
    // (combat_h.nss, Combat_HandleCreatureDisappear), so each is read with
    // the engine's perception between it and the party, and core decides
    // whether it stays (StaysInTheFight). Its side is the game's word as of
    // now: one that has surrendered is no enemy. Not yet seen in play.
    std::size_t afar = 0;
    for (const ObjectId id : inFight)
    {
        if (seen.contains(id) || api::IsObjectValid(id) == 0 || api::IsDead(id) != 0)
        {
            continue;
        }
        seen.insert(id);
        Combatant c = Describe(id, api::IsObjectHostile(leader, id) != 0 ? Side::Enemy : Side::Ally, false);
        c.fighting = false;
        c.perception = Perceived(id, members);
        ++afar;
        spdlog::debug("{:#x} is in the fight and not found fighting (combat state {}, {:.0f} m from the leader): "
                      "the party {} it; it perceives {}, {}",
                      id, api::GetCombatState(id), api::GetDistanceBetween(leader, id),
                      c.perception.byParty ? "perceives" : "does not perceive",
                      c.perception.ofParty ? "a party member" : "no party member",
                      c.perception.ofFoes ? "someone hostile to it" : "nobody hostile to it");
        out.push_back(std::move(c));
    }
    spdlog::debug("scan: {} in the party, {} hostiles near, {} others near; {} fighting, {} of them not the "
                  "party{}{}; {} of the fight not found fighting",
                  party.Size(), hostiles.Size(), others.Size(), out.size() - afar, outside, rooms.empty() ? "" : ":",
                  rooms, afar);
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
        // At an attack: whether its command is still at its approach, as the
        // engine itself has it (the sub-action the command is at). The
        // machine judges an attacker that stands still by this (core:
        // ActorObservation::approaching), not by a walk worked out as the
        // command was let through: an archer in range, or a swordsman in
        // reach with others to walk round, was "short of where its walk was
        // to end" by the path's length and stopped as it shot or swung
        // (issue 11). A read of the command's own list: nothing is searched
        // or cast each frame.
        if (actor.current == CommandType::ExecuteEffect)
        {
            // What holds it, as the engine has it: every temporary effect
            // standing on the creature, with the ability that applied it,
            // who did and the seconds it has left (DAOLib's
            // dao::effects::On). For the machine's log; what a hold costs
            // is to go by these and not by a grace of the mod's (issue 30).
            for (const dao::effects::Standing &effect : dao::effects::On(id))
            {
                if (!(effect.remaining > 0.0f))
                {
                    continue;
                }
                if (!actor.effects.empty())
                {
                    actor.effects += "; ";
                }
                actor.effects += std::format("effect type {} of ability {} by {:#x}, {:.1f} of {:.1f} s left",
                                             static_cast<std::int32_t>(effect.type), effect.ability, effect.creator,
                                             effect.remaining, effect.duration);
            }
            if (actor.effects.empty())
            {
                actor.effects = "no temporary effect stands on it";
            }
        }
        if (actor.current == CommandType::Attack)
        {
            const std::uint32_t at = dao::commands::RunningSubAction(id);
            actor.approaching = at == kMeleeApproach || at == kRangedApproach;
            // Said as it changes, at debug: which sub-action a standing
            // attacker is at is what a play test's log is read for.
            static ObjectId lastWho = dao::kInvalidObject;
            static std::uint32_t lastAt = 0;
            if (id != lastWho || at != lastAt)
            {
                lastWho = id;
                lastAt = at;
                spdlog::debug("{:#x}'s attack is at sub-action {}{}", id, at,
                              actor.approaching ? " (its approach)" : "");
            }
        }
        else if (actor.current == CommandType::Deathblow)
        {
            // A death blow that stands (issue 28: twice an ogre's killer
            // stood at one 36 s of the game's time): which sub-action it is
            // at, and its victim's command, said as either changes, are what
            // the next one's log is read for.
            const std::uint32_t at = dao::commands::RunningSubAction(id);
            const std::optional<ObjectId> victim = current.get()->Target();
            std::int32_t victimAt = -1;
            if (victim && api::IsObjectValid(*victim) != 0)
            {
                if (const dao::script::OwnedCommand theirs = api::GetCurrentCommand(*victim))
                {
                    victimAt = api::GetCommandType(*theirs.get());
                }
            }
            static std::uint32_t lastAt = ~0U;
            static std::int32_t lastVictimAt = -2;
            if (at != lastAt || victimAt != lastVictimAt)
            {
                lastAt = at;
                lastVictimAt = victimAt;
                spdlog::debug("{:#x}'s death blow on {:#x} is at sub-action {}; its victim's command is {} ({})", id,
                              victim.value_or(dao::kInvalidObject), at, victimAt,
                              victim && api::IsDead(*victim) != 0 ? "dead" : "alive");
            }
        }
    }
    actor.busy = actor.current != CommandType::Invalid || api::GetCommandQueueSize(id) > 0;
    actor.moving = api::IsMoving(id) != 0;
    const dao::Vector position = api::GetPosition(id);
    actor.position = {position.x, position.y, position.z};
    actor.conjureHeld = dao::commands::ConjureHeld(id) && dao::commands::Conjure(id).has_value();
    actor.timersHeld = dao::timers::Waiting(id) > 0;
    // What it has noticed, as of this frame (core: ActorObservation::
    // perception): a hostile that perceives nobody to fight is not told its
    // turn has come, and one that notices in its turn is told then (issue
    // 29). The party's own is not asked about. A few reads of the engine's
    // perception lists: IsPerceivingHostiles, and IsPerceiving of each party
    // member until one answers yes.
    if (std::ranges::find(party_, id) == party_.end())
    {
        actor.perception = Perceived(id, party_);
        // Said as it changes, at debug: when a creature noticed is what a
        // play test's log is read for.
        static ObjectId lastWho = dao::kInvalidObject;
        static bool lastUnaware = false;
        const bool unaware = !actor.perception.inCombat && !actor.perception.ofParty && !actor.perception.ofFoes;
        if (id != lastWho || unaware != lastUnaware)
        {
            lastWho = id;
            lastUnaware = unaware;
            spdlog::debug("{:#x} on its turn: it is {} combat, and perceives {}, {}; the party {} it", id,
                          actor.perception.inCombat ? "in" : "not in",
                          actor.perception.ofParty ? "a party member" : "no party member",
                          actor.perception.ofFoes ? "someone hostile to it" : "nobody hostile to it",
                          actor.perception.byParty ? "perceives" : "does not perceive");
        }
    }
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
                kept && kept->target != dao::kInvalidObject && Gone(kept->target))
            {
                // A plain attack whose target fell is turned on the nearest
                // enemy its creature can strike from where it stands, and
                // given at that one: through the gate as the kept one would
                // have come, for nothing, its time paid as the turn began
                // (the user, 2026-10-03, issue 22: "as the game's own
                // auto-attack would", for everyone). Who it may be is the
                // machine's to name (Effect::others) and which the core's to
                // choose; here they are measured and the attack given.
                const ObjectId next = kept->type == CommandType::Attack ? TurnedOn(e.target, e.others) : kNoObject;
                if (next != kNoObject)
                {
                    if (GiveAttack(e.target, next))
                    {
                        spdlog::info(
                            "{:#010x}: the target of its kept attack ({:#x}) is gone: turned on {:#x}, the nearest of "
                            "{} enemies standing that it can strike from where it is ({:.1f} m away)",
                            e.target, kept->target, next, e.others.size(), api::GetDistanceBetween(e.target, next));
                        break;
                    }
                    spdlog::warn("{:#010x}: could not make the command for its kept attack, turned on {:#x}", e.target,
                                 next);
                }
                // Its target fell while it charged: not cast at nothing. In
                // play the cast was given all the same, struck nothing, and
                // put the talent on cooldown for five turns (the twelfth
                // play test, 2026-10-01). Not given, nothing is spent: the
                // machine says its cast did not come, and the turn goes on.
                // A talent or a spell, always: its target was chosen. An
                // attack, with no enemy in its reach: its creature's turn is
                // handed over whole, and an AI chooses anew.
                spdlog::info(
                    "{:#010x}: the target of its charged cast of ability {} ({:#x}) is gone: not cast, nothing "
                    "spent{}",
                    e.target, kept->ability, kept->target,
                    kept->type == CommandType::Attack
                        ? std::format(" (an attack: none of {} enemies standing is in its reach and sight)",
                                      e.others.size())
                        : std::string{});
                // Logged, not shown: "Its target is gone" floated here in
                // grey, and with a turn a bar of time most turns end on a
                // kept attack whose target has often fallen by the next,
                // eight to twelve times a fight in the Korcari Wilds. Nothing
                // is lost by it, so nothing is asked of the player (the user,
                // watching, 2026-10-03: "That can just be a thing we log and
                // not show"; issue 8).
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
            // its target, given as a script gives one, to end inside what
            // the engine's own approach for the attack asks (DAOLib's
            // dao::attack::ApproachOf: for a shot the distance its approach
            // stops at, for a blow the centres' furthest, its range).
            if (const std::optional<HeldCast> kept = PeekHeldCast(e.target);
                kept && kept->target != dao::kInvalidObject && api::IsObjectValid(kept->target) != 0)
            {
                constexpr float kInside = 0.3f; // meters inside the reach, so that the attack need not step
                constexpr float kBeside = 1.5f; // where the engine has no approach to say: beside its target
                const std::optional<dao::attack::Approach> approach = dao::attack::ApproachOf(e.target, kept->target);
                // A blow's: how far from its target the engine's own place
                // for the attacker on the target's ring is (the approach's
                // goal), not a distance of the mod's. A ring with no place
                // for it at all has the target itself for a goal (DAOLib
                // 151e624; it had no approach to say before): beside its
                // target then too.
                const dao::Vector at = api::GetPosition(kept->target);
                const float toPlace =
                    approach && !approach->ranged ? std::hypot(approach->goal.x - at.x, approach->goal.y - at.y) : 0.0f;
                const float reach = !approach          ? kBeside
                                    : approach->ranged ? approach->stopWithin
                                    : toPlace > 0.0f   ? toPlace
                                                       : kBeside;
                const float within = std::max(0.5f, reach - kInside);
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
            // moved on a round's worth at a time. And with it the scripts'
            // own timers of a second or more, whatever their kind (issue 26,
            // Settings::scriptTimersFrom): named before the hold, which takes
            // what is already in the engine's queue by the same rule.
            if (e.a != 0)
            {
                dao::timers::HoldFrom(settings_.scriptTimersFrom);
                dao::timers::Trace(settings_.scriptTimersTrace);
            }
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
    // And whether it perceives anyone to choose (issue 29): one not in
    // combat that perceives no party member and no hostile is not told its
    // turn has come at all, so this line with "combat state 0" and
    // "perceiving hostiles 0" is worth a look.
    spdlog::info("{} ({:#010x}) chose nothing: combat state {}, disabled flags {:#x}, stun {} paralyze {} "
                 "knockdown {} daze {} sleep {}, current command {}, {} queued, light AI {}, custom AI {}, "
                 "perceiving hostiles {}",
                 Utf8(Remember(id).name), id, api::GetCombatState(id), api::GetEffectsFlags(id),
                 api::GetHasEffects(id, k::EFFECT_TYPE_STUN), api::GetHasEffects(id, k::EFFECT_TYPE_PARALYZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_KNOCKDOWN), api::GetHasEffects(id, k::EFFECT_TYPE_DAZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_SLEEP), current, api::GetCommandQueueSize(id),
                 api::GetLocalInt(id, L"AI_LIGHT_ACTIVE"), api::GetLocalInt(id, L"AI_CUSTOM_AI_ACTIVE"),
                 api::IsPerceivingHostiles(id));
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
