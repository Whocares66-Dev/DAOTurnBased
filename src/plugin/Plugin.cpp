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
#include "core/Events.h"
#include "core/Json.h"
#include "core/Keys.h"
#include "core/Settings.h"
#include "core/State.h"
#include "core/TurnMachine.h"
#include "game/Bar.h"
#include "game/Bridge.h"
#include "game/Camera.h"
#include "game/Gate.h"
#include "game/Preview.h"
#include "game/SettingsFile.h"
#include "game/Targeting.h"
#include "plugin/EluvianAPI.h"

#include "dao/Frame.h"
#include "dao/Input.h"
#include "dao/Log.h"
#include "dao/Options.h"
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
#include <utility>
#include <vector>

namespace
{

// Who hears the toggle's key (BindToggle).
enum class ToggleKey
{
    Polled,   // this, each frame
    Asked,    // given to the game, which has not registered its actions yet
    TheGames, // the game: one of its own keys
};

struct State
{
    std::mutex mutex; // the machine's: Tick and the gate's hooks take turns
    std::unique_ptr<tb::TurnMachine> machine;
    std::unique_ptr<tb::game::Bridge> bridge; // the scripts' thread's alone
    tb::KeyChord toggleKey;
    bool toggleWasDown = false;
    bool inFront = true; // the game's window had the keyboard at the last frame
    ToggleKey toggle = ToggleKey::Polled;
    bool optionsAsked = false; // the options' section was given to DAOLib, and what became of it is not known yet
    std::uint32_t lastSettingsCheck = 0;
    tb::game::BarLink bar;
    tb::game::Preview preview;
    bool movePreview = true;
    bool movePreviewGround = false;
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

bool TurnBasedOn()
{
    const std::lock_guard lock(g.mutex);
    return g.machine->GetSettings().enabled;
}

// The player asks for turn-based combat on or off: by the key, or by the
// box in the game's options. Answers whether it is on after.
bool Toggle()
{
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
        return true;
    case tb::ToggleResult::Disabled:
        tb::game::SaveEnabled(false);
        g.bridge->Say(L"Turn-based combat off", tb::kGrey);
        return false;
    case tb::ToggleResult::RefusedInCombat:
        spdlog::info("toggle refused: in combat");
        g.bridge->Say(L"Turn-based combat can be changed only out of combat", tb::kRed);
        break;
    }
    return TurnBasedOn();
}

// The section's title in the game's options, and what dao::options knows
// it by.
constexpr wchar_t kOptionsSection[] = L"Turn-based";

// The switch in the game's own options: a section "Turn-based" on the Game
// tab with one box, "Turn-based combat" (the user, 2026-10-01: "not
// controls, gameplay tab i think, its own section w/ a toggle"; DAOLib's
// dao/Options.h, 3628d67). The box is the game's own checkbox; the library
// asks `get` when the screen opens and `set` at a click, both on the
// frame's thread as Tick is. `set` answers what the setting now is, and
// the box is drawn by the answer: in a fight the setting does not change,
// so the box goes back, and the tooltip says why. The change is made at
// the click, not at the screen's OK, and is kept in turnbased.ini as the
// key's is. From DAOLib_Load only.
bool AddOptions()
{
    dao::options::Toggle toggle;
    toggle.name = L"TurnBasedCombat";
    toggle.label = L"Turn-based combat";
    toggle.tooltip = L"Changes outside a fight only";
    toggle.get = [] {
        try
        {
            return TurnBasedOn();
        }
        catch (const std::exception &e)
        {
            spdlog::error("the options' box, asked: {}", e.what());
            return false;
        }
    };
    toggle.set = [](bool wanted) {
        try
        {
            return TurnBasedOn() == wanted ? wanted : Toggle();
        }
        catch (const std::exception &e)
        {
            spdlog::error("the options' box, clicked: {}", e.what());
            return !wanted;
        }
    };
    std::vector<dao::options::Toggle> toggles;
    toggles.push_back(std::move(toggle));
    return dao::options::AddSection(kOptionsSection, std::move(toggles));
}

// What became of the section, said once: known when the game first opens
// its options.
void WatchOptions()
{
    if (!g.optionsAsked)
    {
        return;
    }
    switch (dao::options::StateOf(kOptionsSection))
    {
    case dao::options::SectionState::Waiting:
        return;
    case dao::options::SectionState::Shown:
        spdlog::info("the options' Game tab has the Turn-based section");
        break;
    case dao::options::SectionState::NoRoom:
        spdlog::warn("the options' Game tab had no room for the Turn-based section: other plugins' took its rows");
        break;
    case dao::options::SectionState::Failed:
        spdlog::warn("the options' movie was not as DAOLib read it: no Turn-based section, and the screen left alone");
        break;
    case dao::options::SectionState::Unknown:
        spdlog::warn("DAOLib does not know the Turn-based section it was given");
        break;
    }
    g.optionsAsked = false;
}

// The game's name for the toggle: its key in KeyBindings.ini
// (TurnBasedToggle_0, TurnBasedToggle_1).
constexpr wchar_t kToggleAction[] = L"TurnBasedToggle";

// core/Keys.h's scan codes are DAOLib's.
static_assert(tb::ScanCode('T') == static_cast<std::uint32_t>(dao::input::Key::T));
static_assert(tb::ScanCode('Q') == static_cast<std::uint32_t>(dao::input::Key::Q));
static_assert(tb::ScanCode('A') == static_cast<std::uint32_t>(dao::input::Key::A));
static_assert(tb::ScanCode('M') == static_cast<std::uint32_t>(dao::input::Key::M));
static_assert(tb::ScanCode('1') == static_cast<std::uint32_t>(dao::input::Key::Digit1));
static_assert(tb::ScanCode('0') == static_cast<std::uint32_t>(dao::input::Key::Digit0));
static_assert(tb::ScanCode(VK_F1) == static_cast<std::uint32_t>(dao::input::Key::F1));
static_assert(tb::ScanCode(VK_F10) == static_cast<std::uint32_t>(dao::input::Key::F10));
static_assert(tb::ScanCode(VK_F11) == static_cast<std::uint32_t>(dao::input::Key::F11));
static_assert(tb::ScanCode(VK_F12) == static_cast<std::uint32_t>(dao::input::Key::F12));
static_assert(tb::ScanCode(VK_SPACE) == static_cast<std::uint32_t>(dao::input::Key::Space));
static_assert(tb::ScanCode(VK_TAB) == static_cast<std::uint32_t>(dao::input::Key::Tab));
static_assert(tb::ScanCode(VK_RETURN) == static_cast<std::uint32_t>(dao::input::Key::Enter));
static_assert(tb::ScanCode(VK_BACK) == static_cast<std::uint32_t>(dao::input::Key::Backspace));
static_assert(tb::ScanCode(VK_INSERT) == static_cast<std::uint32_t>(dao::input::Key::Insert));
static_assert(tb::ScanCode(VK_DELETE) == static_cast<std::uint32_t>(dao::input::Key::Delete));
static_assert(tb::ScanCode(VK_HOME) == static_cast<std::uint32_t>(dao::input::Key::Home));
static_assert(tb::ScanCode(VK_END) == static_cast<std::uint32_t>(dao::input::Key::End));
static_assert(tb::ScanCode(VK_PRIOR) == static_cast<std::uint32_t>(dao::input::Key::PageUp));
static_assert(tb::ScanCode(VK_NEXT) == static_cast<std::uint32_t>(dao::input::Key::PageDown));
static_assert(tb::ScanCode(VK_UP) == static_cast<std::uint32_t>(dao::input::Key::Up));
static_assert(tb::ScanCode(VK_DOWN) == static_cast<std::uint32_t>(dao::input::Key::Down));
static_assert(tb::ScanCode(VK_LEFT) == static_cast<std::uint32_t>(dao::input::Key::Left));
static_assert(tb::ScanCode(VK_RIGHT) == static_cast<std::uint32_t>(dao::input::Key::Right));

// The toggle's key as one of the game's own (dao/Input.h; DAOLib's
// examples/input seen in play 2026-10-01, a4a7020): listed in the options'
// Controls tab under "Turn-based", rebound there like any key, and kept in
// KeyBindings.ini. `[keys] toggle` is then the key it has until the player
// binds another: the game writes it into KeyBindings.ini at the first
// start, and reads that from then on. From DAOLib_Load only, since the
// game registers its actions once. False for a key the game's bindings are
// not known to take (core/Keys.h), or if the registration is refused: the
// key is then polled, as every key was before.
bool BindToggle(const tb::KeyChord &chord)
{
    const std::uint32_t code = tb::ScanCode(chord.key);
    if (chord.key != 0 && code == 0)
    {
        return false;
    }
    dao::input::Action action;
    action.name = kToggleAction;
    action.label = L"Toggle turn-based combat"; // the row shrinks its text to fit, so short
    action.section = kOptionsSection;           // its heading in the controls, as the options' section's
    action.key = static_cast<dao::input::Key>(code);
    action.modifiers = (chord.ctrl ? dao::input::kControl : 0U) | (chord.shift ? dao::input::kShift : 0U) |
                       (chord.alt ? dao::input::kAlt : 0U);
    // The callback runs at the next frame, on the frame's thread, as Tick
    // does; under a menu or in a conversation the engine keeps the key.
    return dao::input::RegisterAction(std::move(action), [] {
        try
        {
            (void)Toggle();
        }
        catch (const std::exception &e)
        {
            spdlog::error("the toggle's key: {}", e.what());
        }
    });
}

void PollToggle()
{
    if (g.toggle == ToggleKey::TheGames)
    {
        return;
    }
    if (g.toggle == ToggleKey::Asked)
    {
        // Final once the game's mode controller has started, a few seconds
        // after load (lib-agent, 2026-10-01): asked until then, not after.
        const dao::input::ActionState state = dao::input::StateOf(kToggleAction);
        if (state == dao::input::ActionState::Waiting)
        {
            return;
        }
        if (state == dao::input::ActionState::Registered)
        {
            g.toggle = ToggleKey::TheGames;
            spdlog::debug("the game has taken the toggle's key");
            return;
        }
        spdlog::warn("the game did not take the toggle's key ({}), so it is not in the controls: polling {} instead",
                     state == dao::input::ActionState::NameTaken ? "it has an action named TurnBasedToggle already"
                                                                 : "the action is not known to it",
                     tb::FormatKeyChord(g.toggleKey));
        g.toggle = ToggleKey::Polled;
    }
    const bool down = GameInFront() && ChordDown(g.toggleKey);
    const bool pressed = down && !g.toggleWasDown;
    g.toggleWasDown = down;
    if (pressed)
    {
        (void)Toggle();
    }
}

// An edit to turnbased.ini takes effect without a restart: rules from the
// next turn, `enabled` from the next fight.
void UseTurnBar(const tb::Settings &settings)
{
    g.turnBar = settings.turnBar;
    g.portraits = settings.portraits;
    g.movePreview = settings.movePreview;
    g.movePreviewGround = settings.movePreviewGround;
    g.targeting.SetEnabled(settings.pickTargets);
    tb::game::SetPathCheck(settings.pathCheck);
    if (g.turnBar)
    {
        g.bar.Install();
        g.preview.Install();
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
    if (!g.bar.Installed())
    {
        return; // never on this session: nothing shown, nothing rendered
    }
    // Turned off, the bar is served as between fights: an empty view, so the
    // movie lets its portraits go, they are released and the screen is taken
    // down, as at a fight's end. Returning here instead left the screen up
    // and the fight's rendered portraits unreleased (the review's TB-1,
    // 2026-09-30).
    bool fighting = false;
    tb::TurnView view;
    if (g.turnBar)
    {
        const std::lock_guard lock(g.mutex);
        fighting = g.machine->Active();
        view = g.machine->View();
    }
    // Before the screen: at a fight's end it stays up until the movie has
    // let the portraits go, and they are released after.
    g.bar.FollowFight(view, now);
    g.bar.KeepScreen(fighting);
    // Every frame, not with the bar's tenth of a second: the path's place
    // on the screen moves with the camera.
    g.preview.Update(view, now, g.turnBar && g.movePreview, g.movePreviewGround);
    if (now - g.lastBarPublish >= 100)
    {
        g.lastBarPublish = now;
        g.bar.KeepPortraits(view, g.portraits);
        g.bar.Publish(view);
    }
    if (g.bar.TakeEndTurn())
    {
        const std::lock_guard lock(g.mutex);
        spdlog::debug(g.machine->OnPauseKey() ? "the turn was ended by the turn bar's End"
                                              : "the bar's end turn outside a turn-based fight");
    }
    if (const auto focus = g.bar.TakeFocus())
    {
        g.bridge->LookAt(*focus);
    }
}

void LookForEluvian();

// A frame threw. The machine may have stepped and its effects not reached
// the game (Bridge::Apply stopped part-way, or never ran), which the
// machine, believing them said, would not say again: the game left paused,
// its effects' clock held, at an enemy's speed. The machine stands down
// (core's StandDown) and what lets the game go is applied, itself guarded;
// turns begin again with the next fight (the review's TB-3, 2026-09-30).
void StandDown(const char *what) noexcept
{
    try
    {
        spdlog::error("frame: {}", what);
        std::vector<tb::Effect> effects;
        {
            const std::lock_guard lock(g.mutex);
            effects = g.machine->StandDown(std::string{"an error in the frame: "} + what);
        }
        g.bridge->Apply(effects);
    }
    catch (...)
    {
        // Nothing more to try, and the log may be what threw: the next
        // frame's error comes here again.
        OutputDebugStringA("turnbased: standing down after a frame's error failed\n");
    }
}

void Tick()
{
    if (!g.eluvianLooked)
    {
        LookForEluvian();
    }
    const auto now = static_cast<std::uint32_t>(GetTickCount64());
    ReloadIfChanged(now);
    // When the game's window loses and gains the front, for setting beside
    // what follows in the log: a pause the game makes of itself then, taken
    // for the pause key, would end a turn.
    if (const bool inFront = GameInFront(); inFront != g.inFront)
    {
        g.inFront = inFront;
        spdlog::debug("the game's window {} the front", inFront ? "has" : "has lost");
    }
    PollToggle();
    WatchOptions();
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
    g.bridge->WatchCamera();
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
        spdlog::debug("the turn was ended by the turnbased_end_turn tool");
        reply->ok(reply, R"({"ended":true})");
        return;
    }
    reply->error(reply, "There is no turn of the player's to end: this ends a party member's turn the player plays. "
                        "Wait for turnbased.turn_started with detail \"yours\", or read inspect turnbased.state.");
}

void SeedTool(void * /*context*/, const char *args, EluvianReply *reply)
{
    // All of it a whole number: 42.5 or 42e2 taken as 42 would answer with
    // a seed that was not the one asked for (the review's TB-4).
    const std::optional<std::string> seed = tb::JsonField(args != nullptr ? args : "", "seed");
    const std::optional<std::uint64_t> value = seed ? tb::WholeNumber(*seed) : std::nullopt;
    if (!value)
    {
        reply->error(reply, R"(Give the seed as a whole number from 0 to 18446744073709551615: {"seed": 42}.)");
        return;
    }
    {
        const std::lock_guard lock(g.mutex);
        g.machine->Reseed(*value);
    }
    reply->ok(reply, std::format(R"({{"seed":{}}})", *value).c_str());
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
    // What each turnbased.* topic means, for `events describe=true` (Eluvian
    // 0.2.0; an older one's interface ends before DescribeEvent).
    int described = 0;
    if (ELUVIAN_HAS(g.eluvian, DescribeEvent))
    {
        for (int k = 0; k <= static_cast<int>(tb::kLastEventKind); ++k)
        {
            const auto kind = static_cast<tb::EventKind>(k);
            const tb::EventDescription d = tb::Describe(kind);
            std::string json = R"({"description":")";
            tb::AppendJsonString(json, d.meaning != nullptr ? d.meaning : "");
            json += R"(","fields":{"world_ms":"the fight's clock, the world's milliseconds","round":"the round")";
            const auto field = [&json](const char *name, const char *text) {
                if (text != nullptr)
                {
                    json += std::format(R"(,"{}":")", name);
                    tb::AppendJsonString(json, text);
                    json += '"';
                }
            };
            if (d.creature)
            {
                field("id", "the creature, as 0x... (inspect's ids)");
                field("name", "its name");
            }
            field("detail", d.detail);
            field("value", d.value);
            field("command", d.command);
            field("note", d.note);
            json += "}}";
            const std::string topic = std::string{"turnbased."} + tb::Name(kind);
            if (e.DescribeEvent(topic.c_str(), json.c_str()) < 0)
            {
                refused.push_back(topic);
            }
            else
            {
                ++described;
            }
        }
    }
    for (const std::string &name : refused)
    {
        spdlog::warn("Eluvian refused {}: its reason is in eluvian.log", name);
    }
    spdlog::info("registered with Eluvian (build {}): inspect turnbased.state; turnbased_end_turn, turnbased_seed, "
                 "turnbased_settings; events turnbased.<kind>, {} described; {} refused",
                 e.build, described, refused.size());
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
    if (info == nullptr || info->apiVersion != dao::kPluginApiVersion || !info->knownExecutable)
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
    g.toggle = BindToggle(settings.toggleKey) ? ToggleKey::Asked : ToggleKey::Polled;
    if (g.toggle == ToggleKey::Asked)
    {
        spdlog::info(
            "the toggle's key is one of the game's own (Options, Controls, under Turn-based; TurnBasedToggle in "
            "KeyBindings.ini): {} until bound there",
            tb::FormatKeyChord(settings.toggleKey));
    }
    else
    {
        spdlog::info("the toggle's key, {}, is not one the game's bindings are known to take: polled, and not in "
                     "the game's controls",
                     tb::FormatKeyChord(settings.toggleKey));
    }
    g.optionsAsked = AddOptions();
    if (!g.optionsAsked)
    {
        spdlog::warn("DAOLib refused the Turn-based section for the game's options: the key and turnbased.ini remain");
    }
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
            StandDown(e.what());
        }
        catch (...)
        {
            StandDown("an exception of no known kind");
        }
    });
    spdlog::info("ready; turn-based fights begin when combat does");
    return true;
}
