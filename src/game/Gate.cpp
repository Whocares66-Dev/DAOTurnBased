#include "game/Gate.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/Addresses.h"
#include "dao/Animation.h"
#include "dao/Clock.h"
#include "dao/CommandEvents.h"
#include "dao/Conjure.h"
#include "dao/Events.h"
#include "dao/Gda.h"
#include "dao/Hook.h"
#include "dao/Log.h"
#include "dao/Messages.h"
#include "dao/Module.h"
#include "dao/Objects.h"
#include "dao/Resources.h"
#include "dao/Script.h"
#include "dao/ScriptHooks.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace tb::game
{

namespace api = dao::script::api;
namespace k = dao::script::constants;

namespace
{

TurnMachine *g_machine = nullptr;
std::mutex *g_mutex = nullptr;

// Whose a command is. The command says whether the player gave it
// (Command::PlayerIssued, +0x10: the player's clicks, quickbar and steering).
// Otherwise a script's AddCommand queued it, from one of the two command
// tables, or the engine did (effects, weapon draws, animations, jumps,
// SetActive). DAOLib's Addresses.h lists QueueCommand's ten callers
// (kCCObject_QueueCommand), read 2026-09-30.
//
// A caller is told by the function its return address lies in, by that
// function's id in DAOLib's address library (dao::Module::Containing,
// e9c65f2), which is the same in every build of the game: until 2026-10-01
// these were the ten return addresses themselves, numbers of GOG's build
// that EA's would have matched nothing. Ten sites, seven functions
// (DAOLib's tools/address_ids.py on each address; three have no name yet).
constexpr dao::AddressId kFromAddCommand{13455}; // ScriptCommand::ADDCOMMAND, the scripts' AddCommand
constexpr dao::AddressId kFromClientAddCommand = dao::addr::kCClientVirtualMachineCommands_AddCommand;
constexpr std::array kKnownCallers{
    kFromAddCommand,
    kFromClientAddCommand,
    dao::addr::kCCObject_QueueNewCommand,
    dao::AddressId{14452},
    dao::AddressId{14453},
    dao::AddressId{14488},
    dao::AddressId{13252},                               // ScriptCommand::SETACTIVE, two sites
    dao::addr::kCClientVirtualMachineCommands_SetActive, // two sites
};

// The function QueueCommand was called from, or nothing for a return
// address outside the game's code.
using Caller = std::optional<dao::Module::Place>;

Source SourceOf(const dao::Command &command, const Caller &caller)
{
    if (command.PlayerIssued())
    {
        return Source::Player;
    }
    if (caller && (caller->id == kFromAddCommand || caller->id == kFromClientAddCommand))
    {
        return Source::Script;
    }
    return Source::Engine;
}

// A caller not among the seven, logged once each: DAOLib's reading missed
// it.
void NoteCaller(const Caller &caller, std::uintptr_t from, CommandType type)
{
    if (caller && std::ranges::find(kKnownCallers, caller->id) != kKnownCallers.end())
    {
        return;
    }
    static std::mutex mutex;
    static std::set<std::uintptr_t> seen;
    const std::lock_guard lock(mutex);
    if (seen.insert(from).second)
    {
        spdlog::warn("QueueCommand called from {:#010x}, in none of the functions DAOLib knows to call it (a {})",
                     dao::Module::ImageAddress(from), Name(type));
    }
}

// How far a player's attack must walk before it can strike (core/Rules.h,
// Reach: the AI's own rule, with the weapon's range and the target's
// personal space from the data tables). Script commands belong on the
// scripts' thread, so this answers only there; elsewhere the gate measures
// the walk as it happens instead. Which thread the player's orders come on
// is not known yet: the first one says, once.
// What the reach check found, for the log when an attack is refused.
struct Reached
{
    float walk = 0.0f;
    float distance = 0.0f;
    float reach = 0.0f;
    std::int32_t weaponType = -1; // the main hand's base item type; -1 for none
    bool door = false;            // the game's path to the target ends at a shut door
};

// path_check (Settings::pathCheck): the player's orders measured by the
// path the engine would walk, not a straight line. Read on the scripts'
// thread, set on reload.
std::atomic<bool> g_pathCheck{true};

// Casts kept while they charge (Gate.h, HeldCast), by their caster. The
// gate and the bridge's effects both run on the scripts' thread.
std::unordered_map<ObjectId, HeldCast> g_heldCasts;
// Spells given to the engine to conjure while their time is unpaid (core:
// Verdict::Conjure), by their caster.
std::set<ObjectId> g_engineConjures;

// A move farther than its path can go this turn is refused ("Too far")
// rather than walked until its meters run out: the user's call, 2026-10-01
// ("refusing too far is right i think"). Off, a move walks until stopped.
// A flag, not a constant, so both ways stay compiled and linted.
std::atomic<bool> g_pathOnDemand{true};

// The walk the game's own pathfinder finds for a creature to a point, its
// length, by DAOLib's search of its own (dao::world::FindPath, 1fe01a3,
// seen in play in the Elven Ruins: round a corner 3.21 m straight was
// 3.42 m of path, outdoors 35.8 m was 46.6 m; a straight walk costs
// nothing, a search is capped at 30 ms, so once an order, never a frame).
// `toward` is the creature standing at the point: its own obstacle is
// lifted, and the path ends beside it, so the rest of the way to the
// point is added. Nothing where there is no path (a shut door, no mesh).
// The first FindPath drove the creature's own search and left it as its
// path: the hero's read 15.6 m all fight and every order of his was
// refused (the second play test, 2026-09-30), and DAOLib took it out.
constexpr std::int32_t kDoorInTheWay = 6; // PathSearch::result

std::optional<float> PathLength(ObjectId creature, const dao::Vector &to, ObjectId toward = dao::kInvalidObject)
{
    const std::optional<dao::world::WalkPath> path = dao::world::FindPath(creature, to, toward);
    if (!path || path->waypoints.empty())
    {
        return std::nullopt;
    }
    const dao::Vector &end = path->waypoints.back();
    return path->Length() + std::hypot(end.x - to.x, end.y - to.y);
}

std::optional<Reached> WalkToTarget(const dao::CCObject &self, const dao::Command &command, bool byPath)
{
    static std::once_flag said;
    const bool onScripts = GetCurrentThreadId() == dao::script::ScriptThreadId();
    std::call_once(said, [&] {
        spdlog::info("the player's orders reach the queue {} the scripts' thread", onScripts ? "on" : "off");
    });
    const std::optional<ObjectId> target = command.Target();
    if (!onScripts || !target || api::IsObjectValid(*target) == 0)
    {
        return std::nullopt;
    }
    const ObjectId attacker = self.Id();
    const float distance = api::GetDistanceBetween(attacker, *target);
    std::int32_t weaponType = -1;
    const float reach = ReachOf(attacker, *target, &weaponType);
    // Around what stands between them: the path to where the target stands.
    // Only for a creature at rest, as it is when the player gives an order.
    // The game's own second go at an attack comes while the creature is at
    // something else: with a shut door in the way the attack fails "path
    // action required", and the game queues the door's use and the attack
    // again. A search made then, a millisecond after the door's use was
    // queued, was followed by twelve seconds of the creature standing at
    // "use object", its turn lost (the ninth play test, 2026-10-01). Whether
    // the search disturbed the creature's own request for a path is not
    // proven; it is not made while one may be pending.
    std::optional<float> path;
    const bool atRest = self.CurrentCommand() == nullptr && api::GetCommandQueueSize(attacker) == 0;
    if (byPath && g_pathCheck.load() && !atRest)
    {
        spdlog::debug("attack on {:#010x}: its creature is at a command already; judged by the straight line", *target);
    }
    bool door = false;
    if (byPath && g_pathCheck.load() && atRest)
    {
        // The whole search, for why there is no path: the pathfinder's
        // answer 6 is a door or the like in the way, what the game ends a
        // move at with COMMAND_FAILED_PATH_ACTION_REQUIRED (DAOLib's
        // Pathfind, seen in play behind the Elven Ruins' shut doors).
        const dao::Vector to = api::GetPosition(*target);
        const dao::world::PathSearch search = dao::world::Pathfind(attacker, to, *target);
        if (search.path && !search.path->waypoints.empty())
        {
            const dao::Vector &end = search.path->waypoints.back();
            path = search.path->Length() + std::hypot(end.x - to.x, end.y - to.y);
        }
        else
        {
            door = search.result == kDoorInTheWay;
            // What is in the way, as the game finds it (DAOLib cb08d00: a
            // placeable within 5 m of where the path stopped). Only shut
            // doors have been seen; said, in case it is ever something else.
            const std::string what =
                door && search.blocker != dao::kInvalidObject
                    ? std::format("{} ({:#x}) is in the way", Utf8(api::GetName(search.blocker)), search.blocker)
                    : std::string{door ? "a door is in the way" : "judged by the straight line"};
            spdlog::debug("attack on {:#010x}: the game finds no path to it ({}); {}", *target, search.result, what);
        }
    }
    // Never less than the straight line: a path that ends short of it is
    // not a shorter walk.
    const float walked = path ? std::max(*path, distance) : distance;
    if (path)
    {
        spdlog::debug("attack on {:#010x}: {:.1f} m away, {:.1f} m of path, reach {:.1f} m", *target, distance, *path,
                      reach);
    }
    else
    {
        spdlog::debug("attack on {:#010x}: {:.1f} m away, reach {:.1f} m", *target, distance, reach);
    }
    return Reached{WalkToReach(walked, reach), walked, reach, weaponType, door};
}

// A player's move, measured as the engine would walk it.
std::optional<float> MoveLength(const dao::CCObject &self, const dao::Command &command)
{
    if (!g_pathOnDemand.load() || !g_pathCheck.load() || GetCurrentThreadId() != dao::script::ScriptThreadId())
    {
        return std::nullopt;
    }
    const std::optional<dao::Vector> to = command.Position();
    if (!to)
    {
        return std::nullopt;
    }
    // For a creature at rest, as for an attack: a click while it walks is
    // not searched for, and walks until stopped.
    if (self.CurrentCommand() != nullptr || api::GetCommandQueueSize(self.Id()) != 0)
    {
        return std::nullopt;
    }
    const std::optional<float> length = PathLength(self.Id(), *to);
    if (length)
    {
        spdlog::debug("move: {:.1f} m of path", *length);
    }
    else
    {
        spdlog::debug("move: no path found, it walks until stopped");
    }
    return length;
}

// An AI's attack, said with what it would take to land: the desktop play
// test (2026-09-30) saw archers walk their 9 m a turn for four rounds and
// never shoot. Whether they had a shot (the engine's own line of sight,
// DAOLib 859f2db, not yet seen in play) and their reach say why. Their
// first three attacks a fight, and every one out of reach or sight.
void SayAiAttack(const dao::CCObject &self, const dao::Command &command)
{
    const std::optional<Reached> reached = WalkToTarget(self, command, false);
    const std::optional<ObjectId> target = command.Target();
    if (!reached || !target)
    {
        return;
    }
    const dao::world::Sight sight = dao::world::LineOfSight(self.Id(), *target);
    // The same attacker on the same target at most every 3 s: a companion's
    // own AI tried one out of turn some 45 times in two seconds (the second
    // play test, 2026-09-30).
    static int said = 0;
    static std::map<std::pair<ObjectId, ObjectId>, std::uint64_t> lastSaid;
    const std::uint64_t now = GetTickCount64();
    std::uint64_t &last = lastSaid[{self.Id(), *target}];
    if ((said < 3 || reached->walk > 0.0f || !sight) && (last == 0 || now - last >= 3000))
    {
        ++said;
        last = now;
        std::string blocked;
        if (sight.blockedAt)
        {
            blocked =
                std::format(" at ({:.1f}, {:.1f}, {:.1f})", sight.blockedAt->x, sight.blockedAt->y, sight.blockedAt->z);
        }
        spdlog::info("{:#010x} attacks {:#010x}: {:.1f} m away, reach {:.1f} m (base item {}), line of sight {}{}",
                     self.Id(), *target, reached->distance, reached->reach, reached->weaponType,
                     sight ? "clear" : "blocked", blocked);
    }
}

// What cooldowns in turns and sustained abilities need of an ability used
// (core/TurnMachine.h, CommandRequest): its cooldown and use type from the
// ability table (UseType 2 is sustained: Shield Wall, Powerful Swings,
// Flaming Weapons, Stealth; 4 activated; read 2026-09-30), and whether a
// sustained one is on already, so that this use turns it off.
void DescribeAbilityOf(ObjectId user, std::int32_t ability, CommandRequest &request)
{
    request.ability = ability;
    request.cooldownSeconds =
        dao::gda::GetFloat(dao::script::Table::AbilitiesTalents, ability, "Cooldown").value_or(0.0f);
    constexpr std::int32_t kSustained = 2;
    request.sustained =
        dao::gda::GetInt(dao::script::Table::AbilitiesTalents, ability, "UseType").value_or(0) == kSustained;
    // Script commands: on the scripts' thread only, where the queue has
    // been seen called from (the player's orders and the AI's).
    const bool onScripts = GetCurrentThreadId() == dao::script::ScriptThreadId();
    if (request.sustained && onScripts)
    {
        request.sustainedOn = api::IsModalAbilityActive(user, ability) != 0;
    }
    // Its conjuring: the ability's Speed is a row of the game's table of
    // conjuring speeds (spellconjuringspeeds.gda, read 2026-10-01), whose
    // own Speed is a count of loops of the conjure animation (0 instant, 1
    // fast, 2 normal, 4 slow, 6 ritual), and there is no conjuring without
    // a conjure animation (dev/REBALANCE.md, "Conjuring time").
    const std::int32_t speed = dao::gda::GetInt(dao::script::Table::AbilitiesTalents, ability, "Speed").value_or(0);
    request.conjureSeconds = static_cast<float>(
        std::max(0, dao::gda::GetInt(dao::script::Table::SpellConjuringspeed, speed, "Speed").value_or(0)));
    request.abilityType = dao::gda::GetInt(dao::script::Table::AbilitiesTalents, ability, "AbilityType").value_or(0);
    request.conjureAnim = dao::gda::GetInt(dao::script::Table::AbilitiesTalents, ability, "ConjureAnim").value_or(0);
    if (!onScripts)
    {
        return;
    }
    constexpr std::int32_t kStaff = 16; // BASE_ITEM_TYPE_STAFF
    const ObjectId weapon = api::GetItemInEquipSlot(k::INVENTORY_SLOT_MAIN, user);
    const bool armed = api::IsObjectValid(weapon) != 0;
    const std::int32_t weaponType = armed ? api::GetBaseItemType(weapon) : -1;
    request.staff = armed && weaponType == kStaff;
    // With a turn a bar of time, what it takes (core: AbilitySeconds), by
    // its user's own animations (DAOLib's dao/Animation.h, a420055, and its
    // play test): the cast, the conjuring before it, or for an archery
    // talent with no cast animation a shot. An item's is its drink, and a
    // mode turned off nothing: the core's. Nothing known: the core's
    // stand-in.
    if (request.abilityType == kAbilityItem || (request.sustained && request.sustainedOn))
    {
        return;
    }
    const dao::animation::Ability lengths = dao::animation::OfAbility(user, ability);
    AbilityFacts facts;
    facts.cast = lengths.cast;
    facts.conjure = lengths.conjure;
    facts.conjureLoop = lengths.conjureLoop;
    facts.loops = request.conjureAnim != 0 ? static_cast<int>(request.conjureSeconds) : 0;
    facts.ranged = armed && dao::gda::GetFloat(dao::script::Table::Items, weaponType, "Range").value_or(0.0f) > 0.0f;
    facts.hero = api::IsHero(user) != 0;
    facts.talent = request.abilityType == kAbilityTalent;
    request.actionSeconds = AbilitySeconds(facts);
}

void DescribeAbility(const dao::CCObject &self, const dao::Command &command, CommandRequest &request)
{
    if (const std::optional<std::int32_t> ability = command.Ability())
    {
        DescribeAbilityOf(self.Id(), *ability, request);
    }
}

// With a turn a bar of time (core: ActionCostMs, AttackSeconds): what a
// plain attack of this creature's takes. A humanoid's melee swing by the
// game's own formula (CalculateAttackTiming, core_h.nss: the weapon style,
// the main hand's dspeed in the item stats table, the attack speed
// modifier, creature property 31), which the engine plays the swing to; a
// shot; a creature's, or an unarmed humanoid's, by its attack animation's
// length (DAOLib's dao::animation::Length). 0 where nothing is read: the
// core's stand-in. Script calls: on the scripts' thread only.
//
// The formula is the first guess only: what the scripts themselves set for
// a swing (SetAttackDuration, read as each attack ends: LearnSwing) is kept
// by weapon style, speed and attack speed (core: LearnedSwings) and used
// from then on, so a mod that mends the scripts' timing (Dain's Fixes: Haste
// with Momentum) is followed with nothing assumed. Not yet seen in play.
std::mutex g_swingMutex;
LearnedSwings g_swings;

// Combat Training's four abilities as the installed rules_core counts them
// (ReadInstalledRules), the shipped four until it has been read.
std::mutex g_rulesMutex;
std::array<std::int32_t, 4> g_combatTraining = kCombatTrainingAsShipped;
std::size_t g_rulesSize = 0; // the size last read, 0 before the first

std::array<std::int32_t, 4> CombatTrainingAbilities()
{
    const std::lock_guard lock(g_rulesMutex);
    return g_combatTraining;
}
struct AskedSwing
{
    LearnedSwings::Key key;
    float formula = 0.0f;
};
std::unordered_map<ObjectId, AskedSwing> g_askedSwings; // each creature's last

// A shot under way: when its command began (the wall's milliseconds) and
// where its archer stood, to measure what it took, on the game's clock,
// if the archer did not walk first (LearnShot).
struct ShotBegun
{
    std::uint64_t atMs = 0;
    dao::Vector where;
};
std::unordered_map<ObjectId, ShotBegun> g_shotsBegun;

float SwingSecondsOf(ObjectId attacker)
{
    if (GetCurrentThreadId() != dao::script::ScriptThreadId())
    {
        return 0.0f;
    }
    AttackFacts facts;
    facts.humanoid = api::IsHumanoid(attacker) != 0;
    facts.hero = api::IsHero(attacker) != 0;
    const ObjectId weapon = api::GetItemInEquipSlot(k::INVENTORY_SLOT_MAIN, attacker);
    facts.armed = api::IsObjectValid(weapon) != 0;
    if (facts.armed)
    {
        const std::int32_t type = api::GetBaseItemType(weapon);
        facts.ranged = dao::gda::GetFloat(dao::script::Table::Items, type, "Range").value_or(0.0f) > 0.0f;
        facts.weaponSpeed = dao::gda::GetFloat(dao::script::Table::Itemstats, type, "dspeed").value_or(0.0f);
    }
    switch (api::GetWeaponStyle(attacker))
    {
    case k::WEAPONSTYLE_SINGLE:
        facts.style = WeaponStyle::Single;
        break;
    case k::WEAPONSTYLE_DUAL:
        facts.style = WeaponStyle::Dual;
        break;
    case k::WEAPONSTYLE_TWOHANDED:
        facts.style = WeaponStyle::TwoHanded;
        break;
    default:
        facts.style = WeaponStyle::None;
        break;
    }
    constexpr std::int32_t kAttackSpeedModifier = 31; // PROPERTY_ATTRIBUTE_ATTACK_SPEED_MODIFIER
    facts.attackSpeedModifier = api::GetCreatureProperty(attacker, kAttackSpeedModifier, k::PROPERTY_VALUE_TOTAL);
    if (!(facts.humanoid && facts.armed))
    {
        facts.animation = dao::animation::Length(attacker, dao::animation::kAttack);
    }
    const float formula = tb::AttackSeconds(facts);
    if (facts.humanoid && facts.armed)
    {
        const LearnedSwings::Key key =
            facts.ranged ? LearnedSwings::ShotOf(attacker, facts.attackSpeedModifier)
                         : LearnedSwings::KeyOf(facts.style, facts.weaponSpeed, facts.attackSpeedModifier);
        const std::lock_guard lock(g_swingMutex);
        g_askedSwings[attacker] = AskedSwing{key, formula};
        return g_swings.Seconds(key).value_or(formula);
    }
    return formula;
}

// An attack is over: what the scripts set for it is kept for the next of its
// kind. Said once for each kind, and again when it changes, with the
// formula's figure beside it: where the two differ the scripts are not the
// shipped ones, or the formula here is wrong.
// A shot is over: what it took, the wall's time since its command began at
// the game's speed, kept for its archer's next (a shot whose archer walked
// into range first is not). Said once for each archer and speed, and as it
// changes.
void LearnShot(ObjectId archer, const AskedSwing &asked)
{
    const auto begun = g_shotsBegun.find(archer);
    if (begun == g_shotsBegun.end())
    {
        return;
    }
    const ShotBegun shot = begun->second;
    g_shotsBegun.erase(begun);
    const dao::Vector now = dao::script::api::GetPosition(archer);
    if (std::hypot(now.x - shot.where.x, now.y - shot.where.y) > 0.3f)
    {
        return; // it walked: the walk is not the shot's
    }
    const float seconds =
        static_cast<float>(GetTickCount64() - shot.atMs) / 1000.0f * std::max(0.1f, dao::clock::Speed());
    const std::optional<float> before = g_swings.Seconds(asked.key);
    if (!g_swings.Learn(asked.key, seconds))
    {
        return;
    }
    if (!before || std::abs(*before - seconds) > 0.05f)
    {
        spdlog::info("{:#x}: its shot took {:.2f} s of the game's time (the flat figure {:.2f} s): what its next costs",
                     archer, seconds, asked.formula);
    }
}

void LearnSwing(ObjectId attacker)
{
    {
        const std::lock_guard lock(g_swingMutex);
        if (const auto asked = g_askedSwings.find(attacker);
            asked != g_askedSwings.end() && asked->second.key.style == WeaponStyle::Ranged)
        {
            LearnShot(attacker, asked->second);
            return;
        }
    }
    const std::optional<float> set = dao::animation::AttackDuration(attacker);
    if (!set)
    {
        return;
    }
    const std::lock_guard lock(g_swingMutex);
    const auto asked = g_askedSwings.find(attacker);
    if (asked == g_askedSwings.end())
    {
        return;
    }
    const std::optional<float> before = g_swings.Seconds(asked->second.key);
    if (!g_swings.Learn(asked->second.key, *set))
    {
        return;
    }
    const float now = g_swings.Seconds(asked->second.key).value_or(*set);
    if (!before || std::abs(*before - now) > 0.0015f)
    {
        const bool differs = std::abs(asked->second.formula - now) > 0.0015f;
        spdlog::log(differs ? spdlog::level::info : spdlog::level::debug,
                    "{:#x}: the scripts set {:.3f} s for its swing, the formula {:.3f} s{}", attacker, *set,
                    asked->second.formula, differs ? ": the scripts' is what a swing of this kind costs now" : "");
    }
}

// The engine's frames have no unwind data, so no exception may leave a hook
// of ours into them (DAOLib's advice, 2026-09-30): each hook's own work is
// guarded, and on a failure the game carries on as if the mod were not
// there. Called inside a catch-all: which exception it was, for the log.
void Report(const char *where)
{
    try
    {
        throw;
    }
    catch (const std::exception &e)
    {
        spdlog::error("{}: {}; the game carries on without the mod here", where, e.what());
    }
    catch (...)
    {
        spdlog::error("{}: an exception of no known kind; the game carries on without the mod here", where);
    }
}

// CCObject::QueueCommand: __thiscall, the command, add-to-front and the
// override behaviour, `ret 0xC` (DAOLib's dao/Objects.h).
using QueueCommandFn = int(__fastcall *)(dao::CCObject *self, int edx, dao::Command *command, int addToFront,
                                         int overrideBehaviour);
dao::Detour<QueueCommandFn> g_queue;

int __fastcall QueueCommand(dao::CCObject *self, int edx, dao::Command *command, int addToFront, int overrideBehaviour)
{
    const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const Caller caller = dao::Module::Containing(from);
    if (self != nullptr && command != nullptr)
        try
        {
            const auto type = static_cast<CommandType>(static_cast<int>(command->Type()));
            const Source source = SourceOf(*command, caller);
            NoteCaller(caller, from, type);
            const dao::CCCreature *creature = self->AsCreature();
            // The walk to an attack's target: the player's, to refuse one
            // out of reach; and an AI's, with a turn a bar of time, so that
            // a walk and a swing that do not both fit are told apart from
            // ones that do (core: TurnMachine::PayAction; an AI's walk
            // unknown, a spider 13.6 m off paid its swing, walked, was
            // stopped a step short and lost the turn: play test 25's duel).
            const std::optional<Reached> reached =
                type == CommandType::Attack && (source == Source::Player || source == Source::Script)
                    ? WalkToTarget(*self, *command, true)
                    : std::nullopt;
            if (source == Source::Script && type == CommandType::Attack)
            {
                SayAiAttack(*self, *command);
            }
            std::optional<float> walk = reached ? std::optional<float>{reached->walk} : std::nullopt;
            if (source == Source::Player && type == CommandType::MoveToLocation)
            {
                walk = MoveLength(*self, *command);
            }
            CommandRequest request;
            request.object = self->Id();
            request.target = command->Target().value_or(dao::kInvalidObject);
            request.type = type;
            request.source = source;
            request.inCombat = creature != nullptr && creature->CombatState() != 0;
            request.walk = walk;
            request.doorInTheWay = reached && reached->door;
            if (type == CommandType::UseAbility)
            {
                DescribeAbility(*self, *command, request);
            }
            if (type == CommandType::Attack)
            {
                request.actionSeconds = SwingSecondsOf(self->Id());
            }
            Verdict verdict = Verdict::Allow;
            {
                const std::lock_guard lock(*g_mutex);
                verdict = g_machine->OnCommand(request);
            }
            if (verdict == Verdict::Block && source == Source::Player && reached && reached->walk > 0.0f)
            {
                // Why an attack was too far, at info: whether a bow was read
                // as one is what the first refusals in play left open.
                spdlog::info("the refused attack: {:.1f} m away, reach {:.1f} m, main hand base item {} (bitm_base)",
                             reached->distance, reached->reach, reached->weaponType);
            }
            if (verdict == Verdict::Hold)
            {
                // A cast that charges: kept, to be given again when its
                // turns are up, and refused now as below.
                HeldCast kept;
                kept.type = request.type;
                kept.ability = request.ability;
                kept.target = command->Target().value_or(dao::kInvalidObject);
                if (const std::optional<dao::Vector> place = command->Position())
                {
                    kept.placed = true;
                    kept.x = place->x;
                    kept.y = place->y;
                    kept.z = place->z;
                }
                g_heldCasts[self->Id()] = kept;
                // A command aimed at a creature carries a place as well: both
                // are kept and given again; said by the one it is aimed at.
                if (kept.target != dao::kInvalidObject)
                {
                    spdlog::debug("{:#x}: its cast of ability {} is kept while it charges (at {:#x})", self->Id(),
                                  kept.ability, kept.target);
                }
                else
                {
                    spdlog::debug("{:#x}: its cast of ability {} is kept while it charges (at {}a place)", self->Id(),
                                  kept.ability, kept.placed ? "" : "nothing, not ");
                }
            }
            if (verdict == Verdict::Conjure)
            {
                // The engine's own cast: let through, its conjure held until
                // its time is paid (ReleaseEngineConjure), its caster drawing
                // the spell in meanwhile, and the installed rules_core free to
                // interrupt it.
                dao::commands::HoldConjure(self->Id());
                g_engineConjures.insert(self->Id());
                spdlog::info("{:#x}: its spell (ability {}) conjures, held until its time is paid", self->Id(),
                             request.ability);
            }
            if (verdict != Verdict::Allow && verdict != Verdict::Conjure)
            {
                // The queue always takes a command it is given, and no caller
                // frees one after (DAOLib's reading of all ten, 2026-09-30), so
                // a refused one is the gate's to free: back to the commands'
                // pool, as a script's dropped command goes (0x0066DE30). A
                // player's refused order also never raises
                // EVENT_TYPE_PLAYER_COMMAND_ADDED, which the queue signals
                // before it adds one.
                dao::script::DestroyStructure(dao::EngineStructure::Command, command);
                return 0;
            }
        }
        catch (...)
        {
            Report("the gate, on a command"); // let through, as with no fight
        }
    return g_queue.Original()(self, edx, command, addToFront, overrideBehaviour);
}

using PauseKeyFn = dao::messages::TypedHandlerFn<dao::messages::UserTogglePauseMessage>;
dao::Detour<PauseKeyFn> g_pauseKey;

bool __fastcall PauseKey(void *controller, int edx, const dao::messages::UserTogglePauseMessage *message)
{
    bool taken = false;
    try
    {
        const std::lock_guard lock(*g_mutex);
        taken = g_machine->OnPauseKey();
    }
    catch (...)
    {
        Report("the pause key"); // the game's own pause
    }
    if (taken)
    {
        // Said with whether the game's window had the keyboard: a turn once
        // ended "end turn pressed" with no key pressed, a moment after the
        // window came back to the front (the twenty-second play test,
        // 2026-10-01), and three things can end a turn so. A pause message
        // with the window not in front is not the player's key.
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        spdlog::debug("the game's pause message taken: the pause through a fight is the machine's{}",
                      process == GetCurrentProcessId() ? "" : " (the game's window not in front)");
        return true; // handled: the machine pauses and unpauses for the player
    }
    return g_pauseKey.Original()(controller, edx, message);
}

// ToggleGamePause(int bPause = TRUE): no answer. During a fight its
// arguments are taken off the stack and nothing is done.
int ToggleGamePause(dao::script::CommandCall &call)
{
    bool active = false;
    try
    {
        const std::lock_guard lock(*g_mutex);
        active = g_machine->Active();
    }
    catch (...)
    {
        Report("ToggleGamePause");
    }
    if (!active)
    {
        return call.Proceed();
    }
    for (int i = 0; i < call.ParamCount(); ++i)
    {
        std::int32_t ignored = 0;
        if (!dao::script::Stack::PopInt(ignored))
        {
            return dao::script::kStackUnderflow;
        }
    }
    spdlog::info("a script's ToggleGamePause during a turn-based fight: ignored");
    return dao::script::kOk;
}

// The player's orders, as explore mode takes them (GameModeCombat is a
// GameModeExplore, so they are combat's too). Their messages carry the
// order's details or none (DAOLib's dev/messages.csv); only that one came
// is used.
struct Order
{
    const char *what;
    dao::AddressId handler; // an id of DAOLib's address library, not an address (8b027c6 on)
};
namespace handler = dao::messages::handler;
constexpr std::array kOrders{
    Order{"move", handler::GameModeExplore_MoveCharacterToLocationMessage},
    Order{"ability", handler::GameModeExplore_PerformAbilityMessage},
    Order{"ability on the player", handler::GameModeExplore_PerformAbilityOnPlayerMessage},
    Order{"ability on the selected target", handler::GameModeExplore_PerformAbilityOnSelectedTargetMessage},
    Order{"ability on the target under the cursor", handler::GameModeExplore_PerformAbilityOnTargetUnderCursorMessage},
    Order{"default action on a target", handler::GameModeExplore_PerformDefaultActionOnTargetMessage},
    Order{"primary action under the cursor", handler::GameModeExplore_PerformPrimaryActionOnTargetUnderCursorMessage},
    Order{"secondary action on a target", handler::GameModeExplore_PerformSecondaryActionOnTargetMessage},
    Order{"secondary action under the cursor",
          handler::GameModeExplore_PerformSecondaryActionOnTargetUnderCursorMessage},
    Order{"use object", handler::GameModeExplore_PerformUseObjectMessage},
};

// One detour per handler, each with its own trampoline to call on.
template <std::size_t I> struct OrderWatch
{
    static inline dao::Detour<dao::messages::HandlerFn> detour;

    static bool __fastcall Hook(void *mode, int edx, const void *message)
    {
        try
        {
            const std::lock_guard lock(*g_mutex);
            g_machine->OnPlayerOrder(kOrders[I].what);
        }
        catch (...)
        {
            Report("an order");
        }
        spdlog::debug("order: {}", kOrders[I].what);
        return detour.Original()(mode, edx, message);
    }
};

template <class Fn> bool Place(dao::Detour<Fn> &detour, dao::AddressId id, Fn hook, const char *what)
{
    // Where the id is in the game that is running: the GOG build's and
    // EA's differ.
    const std::uintptr_t address = dao::Module::Address(id);
    if (detour.Attach(id, hook))
    {
        spdlog::info("detoured {} at {:#010x}", what, address);
        return true;
    }
    spdlog::error("could not detour {} at {:#010x}", what, address);
    return false;
}

// Every one placed, whether or not another failed.
template <std::size_t... I> bool WatchOrders(std::index_sequence<I...>)
{
    const std::array placed{Place(OrderWatch<I>::detour, kOrders[I].handler, &OrderWatch<I>::Hook, kOrders[I].what)...};
    return std::ranges::all_of(placed, [](bool ok) { return ok; });
}

} // namespace

// The installed rules_core, as the game resolves it (DAOLib's
// dao::resources::Read, ea4e3f9: override, add-ins, patches, the game's own
// archives), read for the abilities its interruption rule counts. A mod that
// mends the script (Dain's Fixes names Master Combat Training, 100103, where
// the shipped script names 100104) is followed, and the shipped script is
// read as shipped: the user, 2026-10-02, "if player has the fix, they get the
// benefit. if not, they don't." At each fight's start, since an override can
// appear between one game and the next ("if the game itself reads the file
// each time, use that", the user through DAOLib's session, the same day):
// a quarter of a megabyte, read through the game's own resource manager.
// Said in the log when what it says changes.
void ReadInstalledRules()
{
    const std::optional<std::vector<std::byte>> script = dao::resources::Read(L"rules_core.ncs");
    if (!script)
    {
        spdlog::warn("rules_core.ncs could not be read: Combat Training counts as the shipped script counts it");
        return;
    }
    std::vector<std::uint8_t> bytes(script->size());
    std::ranges::transform(*script, bytes.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    const std::array<std::int32_t, 4> ids = CombatTrainingIds(bytes);
    {
        const std::lock_guard lock(g_rulesMutex);
        const bool same = g_rulesSize == bytes.size() && g_combatTraining == ids;
        g_combatTraining = ids;
        g_rulesSize = bytes.size();
        if (same)
        {
            return;
        }
    }
    constexpr std::int32_t kMasterCombatTraining = 100103;
    spdlog::info("rules_core.ncs as installed, {} bytes: Combat Training's ranks are abilities {}, {}, {} and {} ({})",
                 bytes.size(), ids[0], ids[1], ids[2], ids[3],
                 ids[3] == kMasterCombatTraining   ? "Master counts: the script is a mended one"
                 : ids == kCombatTrainingAsShipped ? "as shipped: Master Combat Training never counts"
                                                   : "neither the shipped four nor the mended");
}

std::optional<HeldCast> TakeHeldCast(ObjectId creature)
{
    const auto kept = g_heldCasts.find(creature);
    if (kept == g_heldCasts.end())
    {
        return std::nullopt;
    }
    const HeldCast cast = kept->second;
    g_heldCasts.erase(kept);
    return cast;
}

std::optional<HeldCast> PeekHeldCast(ObjectId creature)
{
    const auto kept = g_heldCasts.find(creature);
    return kept != g_heldCasts.end() ? std::optional<HeldCast>{kept->second} : std::nullopt;
}

bool ReleaseEngineConjure(ObjectId caster)
{
    if (!g_engineConjures.contains(caster))
    {
        return false;
    }
    // Fed what is left of it, and held still: the feed is set on its next
    // update while it is held, and it completes then; the cast and its
    // impact run in real time, in its caster's turn, and the hold goes as
    // its command ends (OnEnd). Let go at once, the feed was never set and
    // the whole conjure played in real time at the start of the turn (play
    // test 33: 3.87 s before the bolt).
    if (const std::optional<dao::commands::Conjuring> conjure = dao::commands::Conjure(caster))
    {
        // Its whole length: what is fed is a total that the held conjure's
        // elapsed time is set to, never past its duration. What had run
        // already is not counted in it: fed the rest only, it stood 0.04 s
        // short of done and never ended (a run with no hands, 2026-10-02).
        dao::commands::FeedConjure(caster, conjure->duration);
        spdlog::info("{:#x}: its held conjure let complete ({:.2f} of {:.2f} s had run)", caster, conjure->elapsed,
                     conjure->duration);
    }
    else
    {
        dao::commands::HoldConjure(caster, false); // nothing to feed: not at its conjure
        g_engineConjures.erase(caster);
    }
    return true;
}

bool DropEngineConjure(ObjectId caster)
{
    if (caster == kNoObject)
    {
        // The fight over: each runs on, in real time, as the game would.
        for (const ObjectId held : g_engineConjures)
        {
            dao::commands::HoldConjure(held, false);
        }
        g_engineConjures.clear();
        return false;
    }
    if (g_engineConjures.erase(caster) == 0)
    {
        return false;
    }
    dao::commands::HoldConjure(caster, false);
    api::ClearAllCommands(caster, 1);
    return true;
}

void DropHeldCast(ObjectId creature)
{
    if (creature == kNoObject)
    {
        g_heldCasts.clear();
    }
    else
    {
        g_heldCasts.erase(creature);
    }
}

float ReachOf(ObjectId attacker, ObjectId target, std::int32_t *weaponType)
{
    float weaponRange = 0.0f;
    std::int32_t type = -1;
    const ObjectId weapon = api::GetItemInEquipSlot(k::INVENTORY_SLOT_MAIN, attacker);
    if (api::IsObjectValid(weapon) != 0)
    {
        type = api::GetBaseItemType(weapon);
        weaponRange = dao::gda::GetFloat(dao::script::Table::Items, type, "Range").value_or(0.0f);
    }
    if (weaponType != nullptr)
    {
        *weaponType = type;
    }
    const auto space = [](ObjectId creature) {
        return dao::gda::GetFloat(dao::script::Table::Appearance, api::GetAppearanceType(creature), "PerSpace")
            .value_or(0.0f);
    };
    return Reach(weaponRange, space(attacker), space(target));
}

float AttackSeconds(ObjectId attacker)
{
    CommandRequest request;
    request.type = CommandType::Attack;
    request.actionSeconds = SwingSecondsOf(attacker);
    return static_cast<float>(ActionCostMs(request)) / 1000.0f;
}

float AbilitySecondsOf(ObjectId user, std::int32_t ability)
{
    CommandRequest request;
    request.type = CommandType::UseAbility;
    DescribeAbilityOf(user, ability, request);
    return static_cast<float>(ActionCostMs(request)) / 1000.0f;
}

void SetPathCheck(bool on)
{
    g_pathCheck.store(on);
}

namespace
{

// A command that failed without acting gives its action back (DAOLib's
// command events, 38eea39, not yet seen in play): the failures that did
// nothing. A target destroyed or a timeout may have acted, and a creature
// disabled is its condition's business.
bool DidNothing(std::int32_t result)
{
    // Not the result -1 a use of an object ends with. It ended so all five
    // times in the seventh play test (2026-10-01): three tries at a door
    // mid-fight that left it shut, and the two that opened it, the hero's
    // outside a fight and a soldier's. So -1 there is how the command ends,
    // not a failure, and giving the action back for it (3550cce did, for an
    // hour) made every object used in a fight free.
    switch (result)
    {
    case k::COMMAND_FAILED_INVALID_PATH:
    case k::COMMAND_FAILED_NO_LINE_OF_SIGHT:
    case k::COMMAND_FAILED_NO_SPACE_IN_MELEE_RING:
    case k::COMMAND_FAILED_PATH_ACTION_REQUIRED:
    case k::COMMAND_FAILED_INVALID_DATA:
        return true;
    default:
        return false;
    }
}

void WatchCommandEnds(TurnMachine &machine, std::mutex &mutex)
{
    // A shot's start, to measure it by (LearnShot): only noted, as the
    // callback's contract asks.
    dao::commands::OnStart([](ObjectId object, const dao::Command &command) {
        if (static_cast<CommandType>(static_cast<int>(command.Type())) != CommandType::Attack)
        {
            return;
        }
        const std::lock_guard lock(g_swingMutex);
        if (const auto asked = g_askedSwings.find(object);
            asked != g_askedSwings.end() && asked->second.key.style == WeaponStyle::Ranged)
        {
            g_shotsBegun[object] = ShotBegun{GetTickCount64(), dao::script::api::GetPosition(object)};
        }
    });
    // On the game's thread, inside the command step: the machine only
    // counts, and queues or clears nothing.
    dao::commands::OnEnd([&machine, &mutex](ObjectId object, const dao::Command &command, std::int32_t result) {
        const auto type = static_cast<CommandType>(static_cast<int>(command.Type()));
        if (result == dao::commands::kSuccess && type == CommandType::Attack)
        {
            LearnSwing(object);
        }
        // A held conjure's command over: cast (its time paid, released), or
        // cleared before (rules_core's damage rule, a knockdown): then the
        // spell is lost, as the game has it.
        if (type == CommandType::UseAbility && g_engineConjures.erase(object) > 0)
        {
            dao::commands::HoldConjure(object, false);
            if (result != dao::commands::kSuccess)
            {
                const std::lock_guard lock(mutex);
                if (machine.OnConjureCleared(object))
                {
                    spdlog::info("{:#x}: its held conjure ended {}: the spell is lost", object,
                                 dao::commands::ResultName(result));
                }
            }
        }
        if (result == dao::commands::kSuccess || result == dao::commands::kCleared)
        {
            return;
        }
        // Every failure, to read what a command that seemed to do nothing
        // ended with (an archer's shot charged its cooldown for nothing, the
        // fourth play test).
        // A result DAOLib has no name for by its number (-1 for a use of
        // an object, 2 for an animation): the line ended in nothing.
        const std::string_view named = dao::commands::ResultName(result);
        const std::string why = named.empty() ? std::format("result {}", result) : std::string{named};
        spdlog::debug("{:#x}: {} ended {}", object, Name(type), why);
        if (!DidNothing(result))
        {
            return;
        }
        const std::int32_t ability = command.Ability().value_or(-1);
        const std::lock_guard lock(mutex);
        (void)machine.OnCommandFailed(object, type, ability, why);
    });
}

// A blow that interrupts a spell (core: TurnMachine::OnDamaged): the
// scripts' EVENT_TYPE_DAMAGED, sent once the damage is done (events_h.nss,
// SendEventOnDamaged: the damage, its type, the attacker; DAOLib's
// dao/Events.h, read 2026-10-02), heard on
// the scripts' thread before the target's own script. Seen in play
// 2026-10-02 (timeplay's interrupt): spiders' bites of 7 and 8 on Lyna with
// Chain Lightning pending, her threshold 16, and the spell held. The threshold is the
// game's own (core/Rules.h, InterruptThreshold), worked out only for a
// creature with a spell pending.
void WatchDamage(TurnMachine &machine, std::mutex &mutex)
{
    dao::events::OnScriptEvent(dao::script::EventType::Damaged, [&machine, &mutex](ObjectId target,
                                                                                   const dao::CScriptEvent &event) {
        try
        {
            constexpr std::int32_t kPhysical = 1; // DAMAGE_TYPE_PHYSICAL
            const float damage = event.Float(0);
            const std::int32_t type = event.Integer(0);
            if (!(damage > 0.0f))
            {
                return;
            }
            if (type != kPhysical)
            {
                // Said too, at the log's finest: a staff's bolt and Drain
                // Life on a caster with a spell pending were not physical,
                // and nothing in the log said they had been heard (the first
                // run of timeplay's interrupt, 2026-10-02).
                if (spdlog::should_log(spdlog::level::debug))
                {
                    const std::lock_guard lock(mutex);
                    if (machine.Conjuring(target))
                    {
                        spdlog::debug("{:#x} takes {:.0f} of damage type {} with a spell pending: not a blow, it holds",
                                      target, damage, type);
                    }
                }
                return;
            }
            {
                const std::lock_guard lock(mutex);
                if (!machine.Conjuring(target))
                {
                    return;
                }
            }
            // Combat Training's ranks as the game's own rule counts them
            // (core/Rules.h, InterruptThreshold): a party member's by the
            // abilities the script names, anyone else's from the autoscale
            // table by its rank (rules_core.nss: Diff_GetAutoScaleTable is
            // TABLE_AUTOSCALE whatever the difficulty). The abilities are the
            // installed script's (ReadInstalledRules), the shipped script's
            // until it has been read.
            const bool party = api::IsHero(target) != 0 || api::IsFollower(target) != 0;
            int training = 0;
            if (party)
            {
                for (const std::int32_t ability : CombatTrainingAbilities())
                {
                    training += api::HasAbility(target, ability) != 0 ? 1 : 0;
                }
            }
            else
            {
                const std::int32_t rank = api::GetCreatureRank(target);
                training = dao::gda::GetInt(dao::script::Table::Autoscale, rank, "nCombatTraining")
                               .value_or(TrainingOfRankAsShipped(rank));
            }
            const int level =
                static_cast<int>(api::GetCreatureProperty(target, k::PROPERTY_SIMPLE_LEVEL, k::PROPERTY_VALUE_TOTAL));
            const float threshold = InterruptThreshold(party, training, level);
            bool interrupted = false;
            {
                const std::lock_guard lock(mutex);
                interrupted = machine.OnDamaged(target, damage, true, threshold);
            }
            spdlog::info("{:#x} struck for {:.0f} by {:#x} with a spell pending (threshold {:.0f}): {}", target, damage,
                         event.Creator(), threshold, interrupted ? "interrupted" : "it holds");
        }
        catch (...)
        {
            Report("a blow on a creature with a spell pending");
        }
    });
}

} // namespace

bool InstallGate(TurnMachine &machine, std::mutex &mutex)
{
    WatchCommandEnds(machine, mutex);
    WatchDamage(machine, mutex);
    g_machine = &machine;
    g_mutex = &mutex;
    const bool queue = Place(g_queue, dao::addr::kCCObject_QueueCommand, &QueueCommand, "CCObject::QueueCommand");
    const bool pauseKey = Place(g_pauseKey, dao::addr::kGameModeController_HandleUserTogglePause, &PauseKey,
                                "GameModeController::HandleMessage(UserTogglePauseMessage)");
    const bool autoPause = dao::script::InterceptCommand(dao::script::id::ToggleGamePause, &ToggleGamePause);
    if (!autoPause)
    {
        spdlog::error("could not intercept the scripts' ToggleGamePause");
    }
    // Watching the orders is a fallback: the gate holds without it.
    if (!WatchOrders(std::make_index_sequence<kOrders.size()>{}))
    {
        spdlog::warn("not every order handler is watched: an order the engine queues only while running may stall");
    }
    return queue && pauseKey && autoPause;
}

} // namespace tb::game
