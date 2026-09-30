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
#include "core/Keys.h"
#include "core/Settings.h"
#include "core/TurnMachine.h"
#include "game/Bar.h"
#include "game/Bridge.h"
#include "game/Camera.h"
#include "game/Gate.h"
#include "game/SettingsFile.h"

#include "dao/Frame.h"
#include "dao/Log.h"
#include "dao/Plugin.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <exception>
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
    std::uint32_t lastTick = 0;
    tb::game::BarLink bar;
    bool turnBar = false;
    bool portraits = true;
    std::uint32_t lastBarPublish = 0;
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
        const auto card = std::ranges::find(view.upcoming, *focus, &tb::CardView::id);
        g.bridge->LookAt(*focus, card != view.upcoming.end() && card->side == tb::Side::Party);
    }
}

void Tick()
{
    const auto now = static_cast<std::uint32_t>(GetTickCount64());
    // Real time, not the fight's: the camera eases while the world is paused.
    const float dt = g.lastTick == 0 ? 0.0f : static_cast<float>(now - g.lastTick) / 1000.0f;
    g.lastTick = now;
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
    g.bridge->UpdateCamera(dt);
    ServeBar(now);
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
    g.bridge = std::make_unique<tb::game::Bridge>(settings);
    g.toggleKey = settings.toggleKey;
    UseTurnBar(settings);

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
    });
    spdlog::info("ready; turn-based fights begin when combat does");
    return true;
}
