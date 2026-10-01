// turnbased.dll: turn-based combat for Dragon Age: Origins, a DAOLib plugin.
//
// At load it reads the settings, makes the turn machine (core/TurnMachine.h),
// places the gate's hooks (game/Gate.h) and adds a frame callback that runs
// the machine: observe what it asks for, step it, apply what it answers
// (game/Bridge.h). The frame callback runs on the scripts' thread, paused
// or not, which is what lets it call script commands and keep the clock.
//
// dev/DESIGN.md is the design, dev/TESTING.md what to check in play and
// read in turnbased.log (Documents\BioWare\Dragon Age\daolib\).
#include "core/Json.h"
#include "core/Keys.h"
#include "core/Settings.h"
#include "core/State.h"
#include "core/TurnMachine.h"
#include "game/Bar.h"
#include "game/Bridge.h"
#include "game/Camera.h"
#include "game/Gate.h"
#include "game/SettingsFile.h"
#include "game/Targeting.h"
#include "plugin/EluvianAPI.h"

#include "dao/Frame.h"
#include "dao/Log.h"
#include "dao/Plugin.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace
{

struct State
{
    std::mutex mutex; // the machine's: Tick and the gate's hooks take turns
    std::unique_ptr<tb::TurnMachine> machine;
    std::unique_ptr<tb::game::Bridge> bridge; // the scripts' thread's alone
    tb::KeyChord toggleKey;
    bool toggleWasDown = false;
    std::uint32_t lastSettingsCheck = 0;
    tb::game::BarLink bar;
    tb::game::Targeting targeting;
    bool turnBar = false;
    bool portraits = true;
    std::uint32_t lastBarPublish = 0;
    std::ofstream events; // turnbased.events.jsonl: the machine's events, a JSON line each
    // Eluvian, the development bench's MCP server, when installed: looked
    // for once, on the first frame.
    const EluvianInterface1 *eluvian = nullptr;
    bool eluvianLooked = false;
};
State g;

bool KeyDown(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

// Keys pressed in another window are not the player talking to the game.
bool GameInFront()
{
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

bool ChordDown(const tb::KeyChord &chord)
{
    return chord.key != 0 && KeyDown(chord.key) && KeyDown(VK_CONTROL) == chord.ctrl &&
           KeyDown(VK_SHIFT) == chord.shift && KeyDown(VK_MENU) == chord.alt;
}

void PollToggle()
{
    const bool down = GameInFront() && ChordDown(g.toggleKey);
    const bool pressed = down && !g.toggleWasDown;
    g.toggleWasDown = down;
    if (!pressed)
    {
        return;
    }
    const bool inCombat = g.bridge->PartyInCombat();
    tb::ToggleResult result = tb::ToggleResult::RefusedInCombat;
    {
        const std::lock_guard lock(g.mutex);
        result = g.machine->RequestToggle(inCombat);
    }
    switch (result)
    {
    case tb::ToggleResult::Enabled:
        tb::game::SaveEnabled(true);
        g.bridge->Say(L"Turn-based combat on", tb::kGold);
        break;
    case tb::ToggleResult::Disabled:
        tb::game::SaveEnabled(false);
        g.bridge->Say(L"Turn-based combat off", tb::kGrey);
        break;
    case tb::ToggleResult::RefusedInCombat:
        spdlog::info("toggle refused: in combat");
        g.bridge->Say(L"Turn-based combat can be changed only out of combat", tb::kRed);
        break;
    }
}

// An edit to turnbased.ini takes effect without a restart: rules from the
// next turn, `enabled` from the next fight.
void UseTurnBar(const tb::Settings &settings)
{
    g.turnBar = settings.turnBar;
    g.portraits = settings.portraits;
    g.targeting.SetEnabled(settings.pickTargets);
    tb::game::SetPathCheck(settings.pathCheck);
    if (g.turnBar)
    {
        g.bar.Install();
    }
}

void ReloadIfChanged(std::uint32_t now)
{
    if (now - g.lastSettingsCheck < 2000)
    {
        return;
    }
    g.lastSettingsCheck = now;
    if (!tb::game::SettingsFileChanged())
    {
        return;
    }
    const tb::Settings settings = tb::game::LoadSettings();
    {
        const std::lock_guard lock(g.mutex);
        g.machine->SetSettings(settings);
    }
    g.bridge->SetSettings(settings);
    g.toggleKey = settings.toggleKey;
    UseTurnBar(settings);
    spdlog::info("settings reloaded from {}", tb::game::SettingsPath().string());
}

// The turn bar's movie reads the view a tenth of a second old at most, and
// what it asked for is done here, on the scripts' thread (game/Bar.h). A
// portrait is got with each view, so a fight's cards fill in at ten a
// second, the current turn's first.
void ServeBar(std::uint32_t now)
{
    if (!g.turnBar)
    {
        return;
    }
    bool fighting = false;
    tb::TurnView view;
    {
        const std::lock_guard lock(g.mutex);
        fighting = g.machine->Active();
        view = g.machine->View();
    }
    // Before the screen: at a fight's end it stays up until the movie has
    // let the portraits go, and they are released after.
    g.bar.FollowFight(view, now);
    g.bar.KeepScreen(fighting);
    if (now - g.lastBarPublish >= 100)
    {
        g.lastBarPublish = now;
        g.bar.KeepPortraits(view, g.portraits);
        g.bar.Publish(view);
    }
    if (g.bar.TakeEndTurn())
    {
        const std::lock_guard lock(g.mutex);
        if (!g.machine->OnPauseKey())
        {
            spdlog::debug("the bar's end turn outside a turn-based fight");
        }
    }
    if (const auto focus = g.bar.TakeFocus())
    {
        g.bridge->LookAt(*focus);
    }
}

void LookForEluvian();

void Tick()
{
    if (!g.eluvianLooked)
    {
        LookForEluvian();
    }
    const auto now = static_cast<std::uint32_t>(GetTickCount64());
    ReloadIfChanged(now);
    PollToggle();
    tb::Needs needs;
    {
        const std::lock_guard lock(g.mutex);
        needs = g.machine->WhatToObserve(now);
    }
    const tb::Observation observation = g.bridge->Observe(needs, now);
    std::vector<tb::Effect> effects;
    {
        const std::lock_guard lock(g.mutex);
        effects = g.machine->Step(observation);
    }
    // Unlocked: applying an effect can queue a command, which the gate
    // puts to the machine.
    g.bridge->Apply(effects);
    ServeBar(now);
}

// The machine's typed events (core/Events.h) as JSON lines beside
// turnbased.log, for a tester to assert on (eluvian-agent's MCP will carry
// them on its bus). Appended, a line marking each game's start, so that a
// second game started by mistake cannot wipe the first's (a playtest's did
// its logs, 2026-09-30); begun afresh once past 5 MB.
void OpenEvents()
{
    const std::filesystem::path path = dao::LogDirectory() / "turnbased.events.jsonl";
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    constexpr std::uintmax_t kMaxBytes = 5ull * 1024 * 1024;
    const bool fresh = error || size > kMaxBytes;
    g.events.open(path, fresh ? std::ios::trunc : std::ios::app);
    if (!g.events)
    {
        spdlog::warn("could not open {} for the machine's events", path.string());
        return;
    }
    g.events << std::format(R"({{"ms":{},"event":"session_started","pid":{}}})", GetTickCount64(),
                            GetCurrentProcessId())
             << '\n'
             << std::flush;
    g.machine->SetEventSink([](const tb::TurnEvent &event) {
        const std::string json = tb::ToJson(event, GetTickCount64());
        g.events << json << '\n' << std::flush;
        if (g.eluvian != nullptr)
        {
            g.eluvian->EmitEvent((std::string{"turnbased."} + tb::Name(event.kind)).c_str(), json.c_str());
        }
    });
    spdlog::info("the machine's events go to {}", path.string());
}

// Eluvian's tools (C:\project\DAOEluvianMCP; eluvian-agent, 2026-09-30): an
// agent reads the machine's state, ends the player's turn, sets settings
// and reseeds initiative, and hears the events above as turnbased.<kind>.
// Handlers come on the frame thread, as the hooks do: the machine's lock.
void InspectState(void * /*context*/, const char * /*args*/, EluvianReply *reply)
{
    tb::MachineState state;
    {
        const std::lock_guard lock(g.mutex);
        state = g.machine->State();
    }
    // The bridge is the frame thread's, as this handler is.
    state.speedTook = g.bridge->SpeedTook();
    const std::string json = tb::ToJson(state);
    reply->ok(reply, json.c_str());
}

void EndTurnTool(void * /*context*/, const char * /*args*/, EluvianReply *reply)
{
    bool ended = false;
    {
        const std::lock_guard lock(g.mutex);
        ended = g.machine->OnPauseKey();
    }
    if (ended)
    {
        reply->ok(reply, R"({"ended":true})");
        return;
    }
    reply->error(reply, "There is no turn of the player's to end: this ends a party member's turn the player plays. "
                        "Wait for turnbased.turn_started with detail \"yours\", or read inspect turnbased.state.");
}

void SeedTool(void * /*context*/, const char *args, EluvianReply *reply)
{
    const std::optional<std::string> seed = tb::JsonField(args != nullptr ? args : "", "seed");
    std::uint64_t value = 0;
    if (!seed || std::from_chars(seed->data(), seed->data() + seed->size(), value).ec != std::errc{})
    {
        reply->error(reply, R"(Give the seed as a whole number: {"seed": 42}.)");
        return;
    }
    {
        const std::lock_guard lock(g.mutex);
        g.machine->Reseed(value);
    }
    reply->ok(reply, std::format(R"({{"seed":{}}})", value).c_str());
}

void SettingsTool(void * /*context*/, const char *args, EluvianReply *reply)
{
    const std::string text = args != nullptr ? args : "{}";
    const auto section = tb::JsonField(text, "section");
    const auto key = tb::JsonField(text, "key");
    const auto value = tb::JsonField(text, "value");
    if (!section && !key && !value)
    {
        std::string out = R"({"ini":")";
        tb::AppendJsonString(out, tb::game::SettingsText());
        out += R"("})";
        reply->ok(reply, out.c_str());
        return;
    }
    if (!section || !key || !value)
    {
        reply->error(reply, R"(Give section, key and value, e.g. {"section": "turns", "key": "ai_turn_speed", )"
                            R"("value": "2"}; or nothing, to read turnbased.ini.)");
        return;
    }
    const std::vector<std::string> warnings = tb::game::SaveSetting(*section, *key, *value);
    g.lastSettingsCheck = 0;
    ReloadIfChanged(static_cast<std::uint32_t>(GetTickCount64()) + 2000);
    std::string out = R"({"set":")";
    tb::AppendJsonString(out, std::format("[{}] {} = {}", *section, *key, *value));
    out += R"(","warnings":[)";
    for (std::size_t i = 0; i < warnings.size(); ++i)
    {
        out += i == 0 ? "\"" : ",\"";
        tb::AppendJsonString(out, warnings[i]);
        out += '"';
    }
    out += "]}";
    reply->ok(reply, out.c_str());
}

// Once, on the first frame: the loader loads plugins by name, so eluvian.dll
// comes after this one and is not there yet in DAOLib_Load. Absent from a
// player's install, and then nothing is registered.
void LookForEluvian()
{
    g.eluvianLooked = true;
    g.eluvian = EluvianFind();
    if (g.eluvian == nullptr)
    {
        return;
    }
    const EluvianInterface1 &e = *g.eluvian;
    // Each answers -1 when refused, the reason in eluvian.log: the state's
    // descriptor once was not JSON, and only eluvian.log said so (2026-09-30).
    std::vector<std::string> refused;
    const auto check = [&refused](int result, const char *name) {
        if (result < 0)
        {
            refused.emplace_back(name);
        }
    };
    check(
        e.RegisterInspectKind(
            "turnbased.state",
            R"({"description": "The turn-based combat mod's fight now. Top: active, phase (awaiting order, AI )"
            R"(thinking, executing, settling, turn start, idle...), round, world_ms; current {id, name, side, yours} )"
            R"(of whose turn it is (null between turns; yours when the player plays it); actions_left, )"
            R"(movement_left, movement_max; speed (the game's, as the mod set it) and speed_took; held_by_effect; )"
            R"(last_refusal. order: the turns to come, current first, each {id, name, side, rank, health, energy, )"
            R"(mana, down, current, yours, round}. held: conditions holding creatures {id, name, condition, )"
            R"(turns_owed, on_clock}. cooldowns: abilities held for turns {id, ability, turns}. Ids are 0x... )"
            R"(strings."})",
            &InspectState, nullptr, ELUVIAN_READ_ONLY),
        "turnbased.state");
    check(e.RegisterTool(
              "turnbased_end_turn",
              R"({"description": "End the current party member's turn, as Space or the bar's End does: what it has )"
              R"(left is given up and the next creature's turn starts (turnbased.turn_ended, then )"
              R"(turnbased.turn_started). Only on a turn the player plays (turnbased.state's current.yours); an error )"
              R"(otherwise.", "inputSchema": {"type": "object", "properties": {}}})",
              &EndTurnTool, nullptr, 0),
          "turnbased_end_turn");
    check(e.RegisterTool(
              "turnbased_seed",
              R"({"description": "Seed initiative's rolls from now on (a joiner's, the next fight's), so a )"
              R"(test repeats.", "inputSchema": {"type": "object", "properties": {"seed": {"type": "integer"}}, )"
              R"("required": ["seed"]}})",
              &SeedTool, nullptr, 0),
          "turnbased_seed");
    check(
        e.RegisterTool(
            "turnbased_settings",
            R"({"description": "Read turnbased.ini (no arguments), or set one setting in it and apply it now: section, )"
            R"(key and value as the ini has them, e.g. turns / ai_turn_speed / 2 (the game's speed in AI turns), )"
            R"(turns / party / tactics, turns / condition_turns / 1. Applied at once; answers the parse's warnings.", )"
            R"("inputSchema": {"type": "object", "properties": {"section": {"type": "string"}, "key": {"type": )"
            R"("string"}, "value": {"type": "string"}}}})",
            &SettingsTool, nullptr, 0),
        "turnbased_settings");
    for (const std::string &name : refused)
    {
        spdlog::warn("Eluvian refused {}: its reason is in eluvian.log", name);
    }
    spdlog::info("registered with Eluvian (build {}): {} of 4 (inspect turnbased.state; turnbased_end_turn, "
                 "turnbased_seed, turnbased_settings); events turnbased.<kind>",
                 e.build, 4 - refused.size());
}

void Log(tb::LogLevel level, std::string_view line)
{
    switch (level)
    {
    case tb::LogLevel::Debug:
        spdlog::debug("{}", line);
        break;
    case tb::LogLevel::Info:
        spdlog::info("{}", line);
        break;
    case tb::LogLevel::Warn:
        spdlog::warn("{}", line);
        break;
    }
}

} // namespace

extern "C" __declspec(dllexport) bool __cdecl DAOLib_Load(const dao::LoaderInfo *info)
{
    dao::OpenLog("turnbased");
    if (info == nullptr || info->apiVersion != dao::kPluginApiVersion || !info->expectedExecutable)
    {
        spdlog::error("not the loader or the game this was built for");
        return false;
    }
    spdlog::info("turnbased {} on DAOLib {}", TB_VERSION_STRING, DAO_VERSION_STRING);

    const tb::Settings settings = tb::game::LoadSettings();
    spdlog::info("settings from {}: turn-based {}, movement {} m ({}), party {}, toggle key {}",
                 tb::game::SettingsPath().string(), settings.enabled ? "on" : "off", settings.movementMeters,
                 settings.movementRule == tb::MovementRule::Speed ? "times speed" : "constant",
                 settings.partyControl == tb::PartyControl::Manual ? "manual" : "tactics",
                 tb::FormatKeyChord(settings.toggleKey));

    const std::uint64_t seed = GetTickCount64() ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32);
    g.machine = std::make_unique<tb::TurnMachine>(settings, seed, &Log);
    // The gate hears every command every creature tries; below debug, what
    // it would say about each is not even put together.
    g.machine->SetLogLevel(spdlog::default_logger()->should_log(spdlog::level::debug) ? tb::LogLevel::Debug
                                                                                      : tb::LogLevel::Info);
    OpenEvents();
    g.bridge = std::make_unique<tb::game::Bridge>(settings);
    g.toggleKey = settings.toggleKey;
    UseTurnBar(settings);

    g.targeting.Install(*g.machine, g.mutex);
    if (!tb::game::InstallGate(*g.machine, g.mutex))
    {
        // Without every hook a turn-based fight cannot hold: nobody runs the
        // machine, so it stays idle and the hooks that were placed let
        // everything through.
        spdlog::error("the gate is not whole: turn-based combat is off for this session");
        return true;
    }
    dao::OnFrame([] {
        // Inside the game's frame: nothing may escape into the engine.
        try
        {
            Tick();
        }
        catch (const std::exception &e)
        {
            spdlog::error("frame: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("frame: an exception of no known kind");
        }
    });
    spdlog::info("ready; turn-based fights begin when combat does");
    return true;
}
