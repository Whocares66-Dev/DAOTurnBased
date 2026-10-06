#include "game/Gate.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/Addresses.h"
#include "dao/Animation.h"
#include "dao/Attack.h"
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

// How far an attack must walk before it can strike: the attack command's own
// approach, asked of the engine for the pair as they stand (DAOLib's
// dao::attack, its issue 16). The mod keeps no reach rule of its own: the
// one it had, written from notes of the exe, went by the path's length where
// the engine goes by where the two stand, and cut off 67 archers' shots in
// one ambush (issue 11; the user, 2026-10-04: call the engine's functions).
// Script commands belong on the scripts' thread, which in play is the
// game's, where the engine's approach may be asked; so this answers only
// there, and elsewhere the gate measures the walk as it happens instead.
//
// Both questions have a "no" among their answers, and the gate passes it on
// as it is (issue 27, 2026-10-04; core/Types.h, NoAttack): the user's rule is
// that where the engine decides, the mod asks it and acts on what it says,
// and puts no figure of its own in its place. Until then a "no" was passed
// over with a guess and the attack let through:
// - no approach for a creature at a creature, the target's melee ring having
//   no place for the attacker: let through with its walk unknown, and failed
//   by the engine within 2 ms (COMMAND_FAILED_NO_SPACE_IN_MELEE_RING, 6 of 6
//   in the caverns above Haven);
// - out of reach, the path searched and none found, with no door in the way:
//   let through on a walk reckoned by the straight line, and failed 0.4 s on
//   (COMMAND_FAILED_INVALID_PATH, 15 times for a party member who stood at
//   one spot through four fights on the forest road; 4 more of his were
//   refused "Too far" by that walk, the wrong reason).
// The straight line is kept for what it was meant for: where no search was
// made (its creature at a command already, or path_check off), and where one
// was made and did not answer.
//
// Every attack is asked, each time it comes: a no is for where the two stood
// when it was asked, and nothing is kept of one. (A memory of targets "ruled
// out for the turn" was built, tried in play and withdrawn the same day:
// core/TurnMachine.cpp, NoAttackToMake.)
//
// Whether the engine's attack will find room on its target's melee ring is
// the engine's own to say as well, and DAOLib asks it by the engine's own
// code (its issue 19, 151e624: Approach::findsRoom). The first build here
// took a place answered by ApproachOf for the engine's promise, and play
// showed it was not one: in a crowd staged round one party member (twelve
// dragonlings, 2026-10-04) the engine failed 16 attacks the gate had let
// through as "in its reach" or "0.5 m to walk", 45 to 180 ms on, 7 at the
// blow (sub-action 1) and 9 with the command still at its approach (4). The
// cause, as DAOLib then read it in the exe: a ring nobody has claimed is
// emptied by its creature's update every frame, so the attack's first ask
// claims on an empty ring and finds room; the claim makes the ring active,
// the next frame's refresh fills it with whoever stands round the target
// and wipes the claim, and the approach's next ask or the blow's finds no
// free run of places. Only an attacker that strikes in its command's first
// update (in reach, facing its target already, the target near level and in
// sight) strikes on its fresh claim, however crowded the ring. So the
// approach says both now: strikesAtOnce, and roomOnceFilled, the ring asked
// as it stands a frame after the claim; findsRoom is either, and false too
// where the ring as it stands has no place, the approach failing at its
// first run. In the same crowd on that build (DAOLib's validation, 184
// attacks): 24 with no room, each then failed by the engine; 160 with room,
// none failed for want of it. What can still fail under way is a walker
// stuck against bodies (COMMAND_FAILED_INVALID_PATH, 3 of those 160), which
// by DAOLib's reading cannot be known before the walk: the engine's own
// stuck timer, under 2 cm an update for a second of the walker's time.
// Those are given back as the failure comes (WatchCommandEnds).
// What it found, for the gate and the log.
struct Reached
{
    // What it walks before it strikes (dao::attack::Walk; none in reach).
    // Nothing where the engine has no attack to make.
    std::optional<float> walk;
    // The engine's approach: a shot's or a blow's, in reach or not, whether
    // it finds room, where it goes.
    dao::attack::Approach approach;
    NoAttack none = NoAttack::None; // the engine's own "no", if that was its answer
    float line = 0.0f;              // between the two, for the log
    std::int32_t weaponType = -1;   // the main hand's base item type; -1 for none
    bool door = false;              // the game's path to the target ends at a shut door
};

std::int32_t MainHandType(ObjectId creature)
{
    const ObjectId weapon = api::GetItemInEquipSlot(k::INVENTORY_SLOT_MAIN, creature);
    return api::IsObjectValid(weapon) != 0 ? api::GetBaseItemType(weapon) : -1;
}

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
// And the rest of the pathfinder's answers (DAOLib's dao/World.h,
// PathSearch::result): 1 found, which with no path that arrives is a search
// that gave up part way, as the engine's own walker's would; 5 none; 8 no
// mesh there. Anything else is no answer: 0 not asked, others a search DAOLib
// stopped when its 40 ms ran out.
constexpr std::int32_t kSearchEnded = 1;
constexpr std::int32_t kNoPathThere = 5;
constexpr std::int32_t kNoMeshThere = 8;

bool Answered(std::int32_t result)
{
    return result == kSearchEnded || result == kNoPathThere || result == kNoMeshThere;
}

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

// What a blow's approach says of its target's ring, in DAOLib's own terms,
// for the log: a tester reads which of them an attack was let through or
// refused by. Nothing for a shot, which asks no ring.
std::string RoomOf(const dao::attack::Approach &approach)
{
    if (approach.ranged)
    {
        return {};
    }
    const auto said = [](bool yes) { return yes ? "yes" : "no"; };
    // No room is one of two things: the filled ring has no place and the
    // blow does not come before it is filled; or, with a place on the
    // filled ring all the same, none on the ring as it stands, where the
    // approach fails at its first run.
    const std::string_view room = approach.findsRoom        ? "finds room"
                                  : approach.roomOnceFilled ? "no place on its ring as it stands now"
                                                            : "no room once its ring is filled, and it does not "
                                                              "strike at once";
    return std::format("{} (in reach {}, strikes at once {}, room once its ring is filled {})", room,
                       said(approach.inReach), said(approach.strikesAtOnce), said(approach.roomOnceFilled));
}

// An attack out of its reach, walked by the engine's own path to where its
// approach goes (DAOLib's dao::attack::Walk): the walk's length in
// `reached`, and `by` saying so. With no walk to be had: a door in the way
// is said in `reached`, and the attack still judged by the straight line;
// so is a search that gave no answer; and where the engine itself has no
// walk for it, `reached` says which of its answers that was and this
// answers false: the attack is judged, and has no walk.
bool WalkByPath(Reached &reached, ObjectId attacker, ObjectId target, const dao::attack::Approach &approach,
                std::string_view kind, std::string_view &by)
{
    if (const std::optional<dao::attack::Walked> walk = dao::attack::Walk(attacker, target))
    {
        reached.walk = walk->metres;
        by = "the engine's path";
    }
    else
    {
        // No path reaches, or the engine would give a shot up on the
        // way for want of a line. The whole search, for why there is no
        // path: the pathfinder's
        // answer 6 is a door or the like in the way, what the game ends
        // a move at with COMMAND_FAILED_PATH_ACTION_REQUIRED (DAOLib's
        // Pathfind, seen in play behind the Elven Ruins' shut doors).
        const dao::world::PathSearch search = dao::world::Pathfind(attacker, api::GetPosition(target), target);
        const bool door = search.result == kDoorInTheWay;
        reached.door = door;
        // What is in the way, as the game finds it (DAOLib cb08d00: a
        // placeable within 5 m of where the path stopped). Only shut
        // doors have been seen; said, in case it is ever something else.
        const std::string across = search.blocker != dao::kInvalidObject
                                       ? std::format("{} ({:#x})", Utf8(api::GetName(search.blocker)), search.blocker)
                                       : std::string{};
        if (door)
        {
            spdlog::debug("attack on {:#010x}: the game finds no path to it ({}); {} is in the way", target,
                          search.result, across.empty() ? std::string{"a door"} : across);
        }
        else if (!Answered(search.result))
        {
            // Not asked, or stopped when its time ran out: the engine
            // has not said no, and the walk is not known.
            spdlog::debug("attack on {:#010x}: the search for a path to it gave no answer ({}); judged by the "
                          "straight line",
                          target, search.result);
        }
        else
        {
            // The engine's own "no": it has no walk for this attack
            // (dao::attack::Walk, which makes the engine's search to
            // where the approach goes, a blow's to its place on the
            // ring once filled; one with no such place was refused
            // above). A shot whose path to its target
            // does arrive is one the engine would give up on the way, no
            // line of sight to be had along it (Walk follows the move
            // update's own tests); anything else is no path there. With
            // the search's own account of itself: nobody has yet read
            // why a creature had no path from where it stood (issue 27's
            // second case).
            const bool shot = approach.ranged && search.path.has_value();
            reached.none = shot ? NoAttack::NoLine : NoAttack::NoPath;
            reached.walk.reset();
            spdlog::debug("attack on {:#010x}: {:.1f} m away, out of its reach by the engine's {} approach{}, and "
                          "the engine has no walk for it: {} (the search to its target answers {}{}); the "
                          "search: {}",
                          target, reached.line, kind, approach.lineOfSight ? "" : " (no line of sight)",
                          shot ? "its path arrives and its shot would be given up on the way, no line of sight "
                                 "to be had"
                               : "no path to where its approach goes",
                          search.result, across.empty() ? std::string{} : "; " + across + " lies across the way",
                          search.trace);
            return false;
        }
    }
    return true;
}

std::optional<Reached> WalkToTarget(const dao::CCObject &self, const dao::Command &command)
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
    // The approach the attack command would queue for the two as they stand:
    // in reach, it ends at once and the blow begins from where the attacker
    // is, whatever the path between them (issue 11's archers on the slopes,
    // 20 m from their target and 75 m by the path).
    const std::optional<dao::attack::Approach> approach = dao::attack::ApproachOf(attacker, *target);
    if (!approach)
    {
        // Not asked: not creatures both (an attack on a crate, a door), the
        // only nothing ApproachOf has since DAOLib 151e624. There is no ring
        // to ask, and the engine's approach for it is not read: let through
        // as before, its walk not known.
        spdlog::debug("attack on {:#010x}: not a creature's on a creature, so the engine has no approach to ask for; "
                      "its walk is not known",
                      *target);
        return std::nullopt;
    }
    Reached reached;
    reached.approach = *approach;
    reached.line = api::GetDistanceBetween(attacker, *target);
    reached.weaponType = MainHandType(attacker);
    const std::string_view kind = approach->ranged ? "ranged" : "melee";
    // What the blow will meet on the ring, said with every line of it.
    const std::string room = approach->ranged ? std::string{} : "; " + RoomOf(*approach);
    if (!approach->ranged && !approach->findsRoom)
    {
        // The engine's attack will find no room on its target's ring (above):
        // none on the ring as it stands, where its approach fails at its
        // first run (each of the six let through so in the caverns above
        // Haven, within 2 ms), or a place for its first ask and none once
        // the ring is filled, with no blow in its first update: the engine
        // would take the command and fail it a frame or more on, at its
        // approach's next ask or at the blow
        // (COMMAND_FAILED_NO_SPACE_IN_MELEE_RING either way).
        reached.none = NoAttack::NoPlace;
        spdlog::debug("attack on {:#010x}: {:.1f} m away, and the engine's attack will find no room for {:#x}: {}",
                      *target, reached.line, attacker, RoomOf(*approach));
        return reached;
    }
    if (approach->inReach)
    {
        reached.walk = 0.0f;
        spdlog::debug("attack on {:#010x}: {:.1f} m away, in its reach by the engine's {} approach: nothing to walk{}",
                      *target, reached.line, kind, room);
        return reached;
    }
    // Out of reach. Where the engine's path is not searched, or is searched
    // and gives no answer (below), the walk is the line to where the
    // approach goes, less how near it stops: the approach's own goal and
    // distance, on the ground as the engine measures them.
    const dao::Vector from = api::GetPosition(attacker);
    reached.walk =
        std::max(0.0f, std::hypot(approach->goal.x - from.x, approach->goal.y - from.y) - approach->stopWithin);
    std::string_view by = "the line to where its approach goes";
    // The engine's path there, for what stands between them. Only for a
    // creature at rest, as it is when the player gives an order.
    // The game's own second go at an attack comes while the creature is at
    // something else: with a shut door in the way the attack fails "path
    // action required", and the game queues the door's use and the attack
    // again. A search made then, a millisecond after the door's use was
    // queued, was followed by twelve seconds of the creature standing at
    // "use object", its turn lost (the ninth play test, 2026-10-01). Whether
    // the search disturbed the creature's own request for a path is not
    // proven; it is not made while one may be pending.
    const bool atRest = self.CurrentCommand() == nullptr && api::GetCommandQueueSize(attacker) == 0;
    if (g_pathCheck.load() && !atRest)
    {
        spdlog::debug("attack on {:#010x}: its creature is at a command already; judged by the straight line", *target);
    }
    if (g_pathCheck.load() && atRest && !WalkByPath(reached, attacker, *target, *approach, kind, by))
    {
        return reached;
    }
    // Said with what it was judged by: the log is where a wrong walk shows.
    // A shot in range with no line of sight walks for one, and how far the
    // engine does not say before it has.
    spdlog::debug("attack on {:#010x}: {:.1f} m away, out of its reach by the engine's {} approach{}: {:.1f} m to "
                  "walk, by {}{}",
                  *target, reached.line, kind, approach->lineOfSight ? "" : " (no line of sight)",
                  reached.walk.value_or(0.0f), by, room);
    return reached;
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
// first three attacks a fight, and every one out of reach or sight. By the
// line between the two, as the gate measured it for the command.
void SayAiAttack(const dao::CCObject &self, const dao::Command &command, const std::optional<Reached> &reached)
{
    const std::optional<ObjectId> target = command.Target();
    // Not one that will find no room on its target's ring: the machine says
    // that, as it refuses it, and in a crowd there are many.
    if (!reached || !target || reached->none == NoAttack::NoPlace)
    {
        return;
    }
    const dao::attack::Approach &approach = reached->approach;
    // With where the line is cut, which the aim's own answer leaves out.
    const dao::world::Sight sight = dao::world::LineOfSight(self.Id(), *target);
    // The same attacker on the same target at most every 3 s: a companion's
    // own AI tried one out of turn some 45 times in two seconds (the second
    // play test, 2026-09-30).
    static int said = 0;
    static std::map<std::pair<ObjectId, ObjectId>, std::uint64_t> lastSaid;
    const std::uint64_t now = GetTickCount64();
    std::uint64_t &last = lastSaid[{self.Id(), *target}];
    if ((said < 3 || !approach.inReach || !sight) && (last == 0 || now - last >= 3000))
    {
        ++said;
        last = now;
        std::string blocked;
        if (sight.blockedAt)
        {
            blocked =
                std::format(" at ({:.1f}, {:.1f}, {:.1f})", sight.blockedAt->x, sight.blockedAt->y, sight.blockedAt->z);
        }
        spdlog::info("{:#010x} attacks {:#010x}: {:.1f} m away, {} its reach by the engine's {} approach (base item "
                     "{}), line of sight {}{}",
                     self.Id(), *target, reached->line, approach.inReach ? "in" : "out of",
                     approach.ranged ? "ranged" : "melee", reached->weaponType, sight ? "clear" : "blocked", blocked);
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

// How long a script's wait waits, in seconds: the command's own number, read
// before it runs. CommandWait(fSeconds) keeps its seconds as milliseconds in
// the command's first integer (DAOLib's dao::Command::WaitMilliseconds, read
// from the script command's constructor, 9b9e067), and the game's AI waits
// with AI_DO_NOTHING_DELAY, 2 s, or its quick one, 1 s (ai_main_h_2.nss).
// Seen in play in DAOLib's example plugin, which logs what AddCommand is
// given: "type 13, ..., wait 2000 ms, player issued false" (example.log,
// 2026-10-02). With a turn a bar of time the wait costs that much of its
// creature's bar (core: TurnMachine::PayWait; issue 31). 0 where the command
// has no parameters or says no time: the wait then passes the turn, as every
// wait did before, and the machine says so. The first one read is said, once
// a session: the number the mod charges by has not been seen in play from
// this plugin (2026-10-04).
float WaitSecondsOf(const dao::CCObject &self, const dao::Command &command)
{
    const std::optional<std::int32_t> ms = command.WaitMilliseconds();
    static std::once_flag said;
    std::call_once(said, [&] {
        spdlog::info("the first script's wait at the gate ({:#x}): {}, by the command's own parameter", self.Id(),
                     ms ? std::format("{} ms", *ms) : std::string{"no length, the command has no parameters"});
    });
    return ms && *ms > 0 ? static_cast<float>(*ms) / 1000.0f : 0.0f;
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
// And every attack's, shot or swing: each creature's last (WatchAttacksBegin).
std::unordered_map<ObjectId, ShotBegun> g_attacksBegun;

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

// A cast that charges, kept by the gate to be given again when its turns
// are up (core: Verdict::Hold): what it is, and at whom or where.
void KeepCast(const dao::CCObject &self, const dao::Command &command, const CommandRequest &request)
{
    // A cast that charges: kept, to be given again when its
    // turns are up, and refused now as below.
    HeldCast kept;
    kept.type = request.type;
    kept.ability = request.ability;
    kept.target = command.Target().value_or(dao::kInvalidObject);
    if (const std::optional<dao::Vector> place = command.Position())
    {
        kept.placed = true;
        kept.x = place->x;
        kept.y = place->y;
        kept.z = place->z;
    }
    g_heldCasts[self.Id()] = kept;
    // A command aimed at a creature carries a place as well: both
    // are kept and given again; said by the one it is aimed at.
    if (kept.target != dao::kInvalidObject)
    {
        spdlog::debug("{:#x}: its cast of ability {} is kept while it charges (at {:#x})", self.Id(), kept.ability,
                      kept.target);
    }
    else
    {
        spdlog::debug("{:#x}: its cast of ability {} is kept while it charges (at {}a place)", self.Id(), kept.ability,
                      kept.placed ? "" : "nothing, not ");
    }
}

// A command on its way to an object's queue, put to the machine: whether
// it goes on to the queue. One the machine refuses, or keeps, does not, and
// is freed here.
bool LetThrough(dao::CCObject &self, dao::Command &command, const Caller &caller, std::uintptr_t from)
{
    const auto type = static_cast<CommandType>(static_cast<int>(command.Type()));
    const Source source = SourceOf(command, caller);
    NoteCaller(caller, from, type);
    const dao::CCCreature *creature = self.AsCreature();
    // The walk to an attack's target: the player's, to refuse one
    // out of reach; and an AI's, with a turn a bar of time, so that
    // a walk and a swing that do not both fit are told apart from
    // ones that do (core: TurnMachine::PayAction; an AI's walk
    // unknown, a spider 13.6 m off paid its swing, walked, was
    // stopped a step short and lost the turn: play test 25's duel).
    const std::optional<Reached> reached =
        type == CommandType::Attack && (source == Source::Player || source == Source::Script)
            ? WalkToTarget(self, command)
            : std::nullopt;
    if (source == Source::Script && type == CommandType::Attack)
    {
        SayAiAttack(self, command, reached);
    }
    std::optional<float> walk = reached ? reached->walk : std::nullopt;
    if (source == Source::Player && type == CommandType::MoveToLocation)
    {
        walk = MoveLength(self, command);
    }
    CommandRequest request;
    request.object = self.Id();
    request.target = command.Target().value_or(dao::kInvalidObject);
    request.type = type;
    request.source = source;
    // A script's wait is its AI's: how long it waits (core: PayWait).
    request.waitSeconds = type == CommandType::Wait && source == Source::Script ? WaitSecondsOf(self, command) : 0.0f;
    request.inCombat = creature != nullptr && creature->CombatState() != 0;
    request.walk = walk;
    request.doorInTheWay = reached && reached->door;
    // The engine's own "no", for the machine to refuse by: the gate
    // only passes on what it was answered (WalkToTarget).
    request.noAttack = reached ? reached->none : NoAttack::None;
    if (type == CommandType::UseAbility)
    {
        DescribeAbility(self, command, request);
    }
    if (type == CommandType::Attack)
    {
        request.actionSeconds = SwingSecondsOf(self.Id());
    }
    // (Nothing is asked here of the command's turn to face its
    // target: the engine asks that after the command's approach, and
    // so does the mod, as the command comes to it. Bridge.cpp,
    // TurnLookOf; issue 40.)
    Verdict verdict = Verdict::Allow;
    {
        const std::lock_guard lock(*g_mutex);
        verdict = g_machine->OnCommand(request);
    }
    if (verdict == Verdict::Block && source == Source::Player && reached && reached->walk && *reached->walk > 0.0f)
    {
        // Why an attack was too far, at info: what the engine's
        // approach for it was, and what it would walk.
        spdlog::info("the refused attack: {:.1f} m away, {:.1f} m to walk by the engine's {} approach, main "
                     "hand base item {} (bitm_base){}",
                     reached->line, *reached->walk, reached->approach.ranged ? "ranged" : "melee", reached->weaponType,
                     reached->approach.lineOfSight ? "" : ", no line of sight");
    }
    if (verdict == Verdict::Hold)
    {
        KeepCast(self, command, request);
    }
    if (verdict == Verdict::Conjure)
    {
        // The engine's own cast: let through, its conjure held until
        // its time is paid (ReleaseEngineConjure), its caster drawing
        // the spell in meanwhile, and the installed rules_core free to
        // interrupt it.
        dao::commands::HoldConjure(self.Id());
        g_engineConjures.insert(self.Id());
        spdlog::info("{:#x}: its spell (ability {}) conjures, held until its time is paid", self.Id(), request.ability);
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
        dao::script::DestroyStructure(dao::EngineStructure::Command, &command);
        return false;
    }
    return true;
}

int __fastcall QueueCommand(dao::CCObject *self, int edx, dao::Command *command, int addToFront, int overrideBehaviour)
{
    const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const Caller caller = dao::Module::Containing(from);
    if (self != nullptr && command != nullptr)
    {
        try
        {
            if (!LetThrough(*self, *command, caller, from))
            {
                return 0;
            }
        }
        catch (...)
        {
            Report("the gate, on a command"); // let through, as with no fight
        }
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
template <std::size_t... I> bool WatchOrders(std::index_sequence<I...> /*unused*/)
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
    // A caster that has fallen needs nothing cleared: the engine ends a dead
    // creature's commands itself. And clearing them in the frame it dies
    // took the victim's half out of a death blow: an ogre killed while a
    // spell of its own was conjuring left its killer standing at the death
    // blow 36 s of the game's time, the turn lost "took too long", twice in
    // two fights; a third ogre, with no spell under way, died as it should
    // (issue 28, the Brecilian Forest, 2026-10-04).
    if (api::IsDead(caster) != 0)
    {
        spdlog::debug("{:#x} has fallen with a spell conjuring: its hold let go, its commands left to the engine",
                      caster);
        return true;
    }
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
        // Any attack's, for what the engine says of it after (WatchAttacksBegin).
        g_attacksBegun[object] = ShotBegun{GetTickCount64(), dao::script::api::GetPosition(object)};
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
        // With what it was aimed at, for the machine's line of it ("X's
        // attack on Y failed"): in a crowd the names alone do not say which
        // attack on whom. Seen in play 2026-10-04: the target is still on
        // the command as it ends, the right one in each of 18 failures.
        const ObjectId target = command.Target().value_or(dao::kInvalidObject);
        const std::lock_guard lock(mutex);
        (void)machine.OnCommandFailed(object, type, ability, why, target);
    });
}

// When the engine says an attack is about to be struck: the scripts'
// EVENT_TYPE_COMMAND_PENDING, which rules_core answers with the blow's
// result (rules_core.nss, Combat_HandleCommandAttack). The swing sends it,
// once its target is alive, its creature faces the target within 60 degrees
// and has a line to it (DAOLib's reading of the exe, 2026-10-04, dao/
// Attack.h): so after the approach, and after the turn to face, which is why
// an archer's first shot of a turn was announced 0.4 s of the wall's time
// later than its others (the ambush replayed for issue 11: of 181 attacks,
// 174 announced, and the 8 that never were all still approaching). Only
// said, at debug, with the wall's time since the command began, the
// sub-action the command is at as the engine has it, and how far the
// attacker has come since. Nothing is decided by it: what the machine tells
// a stuck attacker by is the sub-action itself (Bridge's Scan).
void WatchAttacksBegin()
{
    dao::events::OnScriptEvent(dao::script::EventType::CommandPending, [](ObjectId, const dao::CScriptEvent &event) {
        try
        {
            if (!spdlog::should_log(spdlog::level::debug) || event.Integer(0) != k::COMMAND_TYPE_ATTACK)
            {
                return;
            }
            const ObjectId attacker = event.Object(0);
            const ObjectId target = event.Object(1);
            if (api::IsObjectValid(attacker) == 0 || api::IsObjectValid(target) == 0)
            {
                return;
            }
            std::optional<ShotBegun> began;
            {
                const std::lock_guard lock(g_swingMutex);
                if (const auto found = g_attacksBegun.find(attacker); found != g_attacksBegun.end())
                {
                    began = found->second;
                }
            }
            const dao::Vector now = api::GetPosition(attacker);
            const std::string since =
                began ? std::format("{} ms", GetTickCount64() - began->atMs) : std::string{"? ms"};
            const float walked = began ? std::hypot(now.x - began->where.x, now.y - began->where.y) : 0.0f;
            spdlog::debug("{:#x}'s attack on {:#x} is to be struck (EVENT_TYPE_COMMAND_PENDING), {} after its command "
                          "began: {:.1f} m away, at sub-action {}, {:.1f} m walked",
                          attacker, target, since, api::GetDistanceBetween(attacker, target),
                          dao::commands::RunningSubAction(attacker), walked);
        }
        catch (...)
        {
            Report("an attack about to be struck");
        }
    });
}

// The damage of one blow that interrupts a creature's spell, by the game's
// own rule (core/Rules.h, InterruptThreshold), from what the engine has of
// the creature.
float InterruptThresholdOf(ObjectId target)
{
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
    return InterruptThreshold(party, training, level);
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
            const float threshold = InterruptThresholdOf(target);
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
    WatchAttacksBegin();
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
