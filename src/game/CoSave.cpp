#include "game/CoSave.h"

#include "core/PartyTactics.h"
#include "core/Text.h"

#include "dao/Log.h"
#include "dao/Saves.h"

#include <exception>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace tb::game
{

namespace
{

// DAOLib's name for the plugin's file: <the save's folder>\turnbased.daolib.
constexpr std::string_view kPlugin = "turnbased";

TurnMachine *g_machine = nullptr;
std::mutex *g_mutex = nullptr;

// "Lyna's quick save, slot 1": the save as the log names it, and as the
// game's own folders do (Slot_1, AutoSave_1, QuickSave_1).
std::string Named(const dao::saves::Slot &save)
{
    return std::format("{}'s {} save, slot {}", Utf8(save.character), dao::saves::Name(save.kind), save.number);
}

// What every companion starts as with nothing chosen for it, for the log.
std::string_view Start()
{
    const std::lock_guard lock(*g_mutex);
    return g_machine->GetSettings().partyControl == PartyControl::Tactics ? "tactics" : "manual";
}

// The game has written a save: the playthrough's choices go into it.
void Write(const dao::saves::Slot &save)
{
    PartyTactics choices;
    {
        const std::lock_guard lock(*g_mutex);
        choices = g_machine->GetPartyTactics();
    }
    if (save.Write(kPlugin, WriteChoices(choices)))
    {
        spdlog::info("whose tactics play its turns, written with {}: {}", Named(save), Describe(choices));
        return;
    }
    spdlog::warn("whose tactics play its turns could not be written with {} ({}): that save keeps whatever file of "
                 "the mod's its slot had, or none",
                 Named(save), Describe(choices));
}

// A playthrough is in the world: its choices take the place of whatever was
// chosen before, which was another playthrough's. And the machine is told
// that the world is another (issue 90): what a fight dropped with the last
// one still held of its fighters' cooldowns is not this one's to be given.
// DAOLib calls while the game's mode still says loading (dao/Saves.h), so
// before the frame that would write it.
void Read(const std::optional<dao::saves::Slot> &save)
{
    PartyTactics choices;
    if (!save)
    {
        spdlog::info("a new game: nothing is chosen of whose tactics play its turns, and every companion starts as "
                     "[turns] party says ({})",
                     Start());
    }
    else if (const std::optional<std::string> file = save->Read(kPlugin); !file)
    {
        spdlog::info("{} has no file of the mod's (a save from before it kept one, or one written without the mod "
                     "hearing of it): nothing is chosen of whose tactics play its turns, and every companion starts "
                     "as [turns] party says ({})",
                     Named(*save), Start());
    }
    else if (ChoicesRead read = ReadChoices(*file); !read.problem.empty())
    {
        spdlog::warn("the mod's file in {} is not read: {}. Nothing is chosen of whose tactics play its turns, and "
                     "every companion starts as [turns] party says ({})",
                     Named(*save), read.problem, Start());
    }
    else
    {
        choices = std::move(read.choices);
        spdlog::info("whose tactics play its turns, read from {}: {}", Named(*save), Describe(choices));
    }
    const std::lock_guard lock(*g_mutex);
    g_machine->SetPartyTactics(std::move(choices));
    g_machine->OnGameLoaded();
}

} // namespace

void InstallCoSave(TurnMachine &machine, std::mutex &mutex)
{
    g_machine = &machine;
    g_mutex = &mutex;
    // Neither may throw: an exception has no way back through the game's
    // code (dao/Saves.h).
    dao::saves::OnGameLoaded([](const std::optional<dao::saves::Slot> &save) {
        try
        {
            Read(save);
        }
        catch (const std::exception &e)
        {
            spdlog::error("reading the mod's file from a save: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("reading the mod's file from a save: an exception of no known kind");
        }
    });
    dao::saves::OnSaved([](const dao::saves::Slot &save) {
        try
        {
            Write(save);
        }
        catch (const std::exception &e)
        {
            spdlog::error("writing the mod's file into a save: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("writing the mod's file into a save: an exception of no known kind");
        }
    });
}

} // namespace tb::game
