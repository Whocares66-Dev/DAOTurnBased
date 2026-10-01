// The turn machine's hands and eyes in the engine: reads what the machine
// asks for (core/TurnMachine.h, Needs) through DAOLib, and does what it
// answers (Effect).
//
// Everything here is a script command called from C++ (dao::script::api)
// or a read DAOLib has checked in play, and it runs on the scripts' thread:
// src/plugin/Plugin.cpp calls it from the frame callback, which runs there
// (DAOLib's dev/LIBRARY.md, "Every frame"). Nothing here decides anything.
#pragma once

#include "core/Settings.h"
#include "core/TurnMachine.h"
#include "game/Camera.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tb::game
{

class Bridge
{
  public:
    explicit Bridge(const Settings &settings) : settings_(settings)
    {
    }
    void SetSettings(const Settings &settings)
    {
        settings_ = settings;
    }

    // The game's mode and pause, and what `needs` asks for. With no world
    // (no application yet, the main menu, loading) the mode says so and
    // nothing else is read.
    Observation Observe(const Needs &needs, std::uint32_t nowMs);

    // In order: an effect can queue a command (PokeAi does, through the
    // creature's AI), which comes back through the gate.
    void Apply(const std::vector<Effect> &effects);

    // Whether the party is fighting, by the game's own word (the hero's
    // combat state), menus open or not.
    bool PartyInCombat();

    // Floating text outside the machine's effects (the toggle key's answer),
    // over the hero if there is one.
    void Say(const std::wstring &text, std::uint32_t colour);

    // Each frame, after Apply: whether the camera got where it was sent.
    void WatchCamera()
    {
        camera_.Watch();
    }

    // A look at a combatant the player asked for (a card clicked).
    void LookAt(ObjectId id)
    {
        camera_.TurnTo(id);
    }

    // Whether the game took the last speed set (DAOLib's SetSpeed says);
    // nothing before any was set. For a test server's turnbased.state.
    std::optional<bool> SpeedTook() const
    {
        return speedTook_;
    }

  private:
    CameraDirector camera_;
    std::optional<bool> speedTook_;
    std::optional<bool> byRooms_; // whether the last scan went by the rooms seen (Scan)
    struct Known
    {
        std::wstring name;
        std::wstring tag;
        int rank = 0;
        float speed = 1.0f; // against a person's pace (core/Rules.h, SpeedFactor)
    };

    std::vector<Combatant> Scan();
    Combatant Describe(ObjectId id, Side side, bool hero);
    ActorObservation Actor(ObjectId id);
    CombatantStatus Status(ObjectId id);
    const Known &Remember(ObjectId id);
    void Poke(ObjectId id);
    void Explain(ObjectId id);
    void Signal(const Effect &effect);

    Settings settings_;
    std::unordered_map<ObjectId, Known> known_;
};

} // namespace tb::game
