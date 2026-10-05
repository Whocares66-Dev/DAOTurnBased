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
#include <unordered_set>
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

    // Whether the party is fighting, by the game's own word (any living
    // member's combat state: a fallen hero's is 0, issue 42), menus open or
    // not.
    bool PartyInCombat();

    // Floating text outside the machine's effects (the toggle key's answer),
    // over the one the player acts as (core/Rules.h, ActsForThePlayer): the
    // creature controlled while it lives, where the player is looking.
    void Say(const std::wstring &text, std::uint32_t colour);

    // Each frame, after Apply: whether the camera got where it was sent.
    void WatchCamera()
    {
        camera_.Watch();
    }

    // A look at a combatant the player asked for (a card clicked).
    void LookAt(ObjectId id);

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
    bool leaderStands_ = true;    // whether the party's leader stood for it at the last scan (SayWhoStands)
    std::unordered_set<ObjectId>
        unawares_; // hostiles in the fight though they have not noticed, as of the last scan (Scan)
    // The party as the last scan found it: whose perception of the acting
    // creature, and its of them, is read each frame (Actor).
    std::vector<ObjectId> party_;
    // Creatures whose kept cast or action was not given, its target gone
    // and, for a plain attack, no enemy in its reach to turn it on (Apply,
    // ReleaseCast): told to the machine with the next observation.
    std::vector<ObjectId> keptVoid_;
    struct Known
    {
        std::wstring name;
        std::wstring tag;
        int rank = 0;
        float speed = 1.0f; // against a person's pace (core/Rules.h, SpeedFactor)
        int tactics = 0;    // Combat Tactics' rank, 0 to 4
        int survival = 0;   // Survival's
    };

    std::vector<Combatant> Scan(const std::vector<ObjectId> &inFight);
    void SayWhoStands(ObjectId stands, ObjectId leader, ObjectId controlled);
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

// The party as the game has it now (the scripts' GetPartyList), each member
// with its name and its tag, and which of them is the hero: for a tool that
// names one of the party (core/PartyTactics.h, FindFollower), and what a
// choice for a companion is kept by. Nobody where there is no world to ask
// (the main menu, loading). On the scripts' thread.
[[nodiscard]] std::vector<Follower> PartyNow();

// Everyone the game's tactics page can be of: the party as above, then the
// rest of the party's pool (the scripts' GetPartyPoolList: the companions
// left at the camp). For the page's box (game/TacticsBox.h), which is a
// companion's whether or not it is one of the four out: the page's own
// arrows are on in the camp with a party of one (tactics.gfx's
// SharedLibrary.CharSelect asks Party.InCamp, read 2026-10-05), which is
// taken to mean that it goes through the pool there; not seen. Nobody where
// there is no world. On the scripts' thread.
[[nodiscard]] std::vector<Follower> FollowersNow();

} // namespace tb::game
