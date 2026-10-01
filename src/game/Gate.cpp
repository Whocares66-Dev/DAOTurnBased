#include "game/Gate.h"

#include "core/Rules.h"

#include "dao/Addresses.h"
#include "dao/CommandEvents.h"
#include "dao/Gda.h"
#include "dao/Hook.h"
#include "dao/Log.h"
#include "dao/Messages.h"
#include "dao/Module.h"
#include "dao/Objects.h"
#include "dao/Script.h"
#include "dao/ScriptHooks.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

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
constexpr std::uint32_t kFromAddCommand = 0x0066E1B4;       // CVirtualMachineCommands' AddCommand
constexpr std::uint32_t kFromClientAddCommand = 0x006DA2C4; // CClientVirtualMachineCommands' AddCommand
constexpr std::array kKnownCallers{
    kFromAddCommand,           kFromClientAddCommand,     std::uint32_t{0x0073D054}, std::uint32_t{0x006A22AE},
    std::uint32_t{0x006A23C5}, std::uint32_t{0x006A3A19}, std::uint32_t{0x00661A7E}, std::uint32_t{0x00661B1B},
    std::uint32_t{0x006D694C}, std::uint32_t{0x006D69CC},
};

Source SourceOf(const dao::Command &command, std::uint32_t caller)
{
    if (command.PlayerIssued())
    {
        return Source::Player;
    }
    if (caller == kFromAddCommand || caller == kFromClientAddCommand)
    {
        return Source::Script;
    }
    return Source::Engine;
}

// A caller not among the ten, logged once each: DAOLib's reading missed it.
void NoteCaller(std::uint32_t caller, CommandType type)
{
    if (std::ranges::find(kKnownCallers, caller) != kKnownCallers.end())
    {
        return;
    }
    static std::mutex mutex;
    static std::set<std::uint32_t> seen;
    const std::lock_guard lock(mutex);
    if (seen.insert(caller).second)
    {
        spdlog::warn("QueueCommand called from {:#010x}, not one of the ten callers DAOLib knows (a {})", caller,
                     Name(type));
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
};

// path_check (Settings::pathCheck): the player's orders measured by the
// path the engine would walk, not a straight line. Read on the scripts'
// thread, set on reload.
std::atomic<bool> g_pathCheck{true};

// Whether DAOLib can give a path on demand. Not now: its FindPath answered a
// call late and left its search as the creature's own path, so the hero's
// path read 15.6 m all fight and every order of his was refused (the second
// play test, 2026-09-30, fight B), and DAOLib took it out (7cfe6bd: the
// engine's search keeps an old request until it ends). Until a search of
// its own comes, a straight line, and a move walks until its meters run
// out. A flag, not a constant, so the rest stays compiled and linted.
std::atomic<bool> g_pathOnDemand{false};

// The engine's path for an idle creature to a point, its length: nothing
// while DAOLib has no path on demand.
std::optional<float> PathLength(ObjectId /*creature*/, const dao::Vector & /*to*/)
{
    return std::nullopt;
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
    float weaponRange = 0.0f;
    std::int32_t weaponType = -1;
    const ObjectId weapon = api::GetItemInEquipSlot(k::INVENTORY_SLOT_MAIN, attacker);
    if (api::IsObjectValid(weapon) != 0)
    {
        weaponType = api::GetBaseItemType(weapon);
        weaponRange = dao::gda::GetFloat(dao::script::Table::Items, weaponType, "Range").value_or(0.0f);
    }
    const float space =
        dao::gda::GetFloat(dao::script::Table::Appearance, api::GetAppearanceType(*target), "PerSpace").value_or(0.0f);
    const float reach = Reach(weaponRange, space);
    // Around what stands between them: the path to where the target stands.
    std::optional<float> path;
    if (byPath && g_pathCheck.load())
    {
        path = PathLength(attacker, api::GetPosition(*target));
    }
    const float walked = path ? *path : distance;
    if (path)
    {
        spdlog::debug("attack on {:#010x}: {:.1f} m away, {:.1f} m of path, reach {:.1f} m", *target, distance, *path,
                      reach);
    }
    else
    {
        spdlog::debug("attack on {:#010x}: {:.1f} m away, reach {:.1f} m", *target, distance, reach);
    }
    return Reached{WalkToReach(walked, reach), walked, reach, weaponType};
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
void DescribeAbility(const dao::CCObject &self, const dao::Command &command, CommandRequest &request)
{
    const std::optional<std::int32_t> ability = command.Ability();
    if (!ability)
    {
        return;
    }
    request.ability = *ability;
    request.cooldownSeconds =
        dao::gda::GetFloat(dao::script::Table::AbilitiesTalents, *ability, "Cooldown").value_or(0.0f);
    constexpr std::int32_t kSustained = 2;
    request.sustained =
        dao::gda::GetInt(dao::script::Table::AbilitiesTalents, *ability, "UseType").value_or(0) == kSustained;
    // A script command: on the scripts' thread only, where the queue has
    // been seen called from (the player's orders and the AI's).
    if (request.sustained && GetCurrentThreadId() == dao::script::ScriptThreadId())
    {
        request.sustainedOn = api::IsModalAbilityActive(self.Id(), *ability) != 0;
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
    const std::uint32_t caller = dao::Module::Preferred(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if (self != nullptr && command != nullptr)
        try
        {
            const auto type = static_cast<CommandType>(static_cast<int>(command->Type()));
            const Source source = SourceOf(*command, caller);
            NoteCaller(caller, type);
            const dao::CCCreature *creature = self->AsCreature();
            const std::optional<Reached> reached = source == Source::Player && type == CommandType::Attack
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
            request.type = type;
            request.source = source;
            request.inCombat = creature != nullptr && creature->CombatState() != 0;
            request.walk = walk;
            if (type == CommandType::UseAbility)
            {
                DescribeAbility(*self, *command, request);
            }
            Verdict verdict = Verdict::Allow;
            {
                const std::lock_guard lock(*g_mutex);
                verdict = g_machine->OnCommand(request);
            }
            if (verdict == Verdict::Block && reached && reached->walk > 0.0f)
            {
                // Why an attack was too far, at info: whether a bow was read
                // as one is what the first refusals in play left open.
                spdlog::info("the refused attack: {:.1f} m away, reach {:.1f} m, main hand base item {} (bitm_base)",
                             reached->distance, reached->reach, reached->weaponType);
            }
            if (verdict == Verdict::Block)
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
        return true; // handled: the game neither pauses nor unpauses
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
    std::uintptr_t handler;
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

template <class Fn> bool Place(dao::Detour<Fn> &detour, std::uintptr_t address, Fn hook, const char *what)
{
    if (detour.Attach(address, hook))
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
    // On the game's thread, inside the command step: the machine only
    // counts, and queues or clears nothing.
    dao::commands::OnEnd([&machine, &mutex](ObjectId object, const dao::Command &command, std::int32_t result) {
        if (result == dao::commands::kSuccess || result == dao::commands::kCleared)
        {
            return;
        }
        const auto type = static_cast<CommandType>(static_cast<int>(command.Type()));
        // Every failure, to read what a command that seemed to do nothing
        // ended with (an archer's shot charged its cooldown for nothing, the
        // fourth play test).
        spdlog::debug("{:#x}: {} ended {}", object, Name(type), dao::commands::ResultName(result));
        if (!DidNothing(result))
        {
            return;
        }
        const std::int32_t ability = command.Ability().value_or(-1);
        const std::lock_guard lock(mutex);
        (void)machine.OnCommandFailed(object, type, ability, dao::commands::ResultName(result));
    });
}

} // namespace

bool InstallGate(TurnMachine &machine, std::mutex &mutex)
{
    WatchCommandEnds(machine, mutex);
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
