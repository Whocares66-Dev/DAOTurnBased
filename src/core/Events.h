// The machine's events, typed: what a test asserts on instead of reading the
// log's prose, and what the plugin writes as JSON lines beside
// turnbased.log (turnbased.events.jsonl). Asked for by eluvian-agent
// (2026-09-30) for the MCP server that lets agents drive the game: there
// they go out on its bus as turnbased.<kind>. Every one is also a log line.
#pragma once

#include "core/Types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace tb
{

enum class EventKind : std::uint8_t
{
    FightBegan,         // combat began, turn-based
    FightOver,          // detail: why
    RoundStarted,       // value: the round
    TurnStarted,        // who; detail: "yours" or "AI"; value: tenths of a meter to move
    TurnResumed,        // who: after a conversation, the same turn
    TurnEnded,          // who; detail: why ("done", "ended by the player", ...)
    TurnLost,           // who: its condition took it (a turn turn_ended closes); value: turns still owed
    Refused,            // who; detail: what the player was told ("Too far", ...); command; note: the numbers
    Joined,             // who; value: its initiative
    Fell,               // who
    Gone,               // who: vanished from the world
    UpAgain,            // who
    ConditionHeld,      // who; value: turns it costs (0: as long as it lasts)
    ConditionEnded,     // who: its turns spent, the machine ends it
    ConditionStillHeld, // who: ended, and the engine holds it still
    CooldownHeld,       // who; value: the ability; detail: the turns, as text
    CooldownReleased,   // who; value: the ability
    SpeedSet,           // value: the game's speed, thousandths
    TurnHandedOver,     // who: party control changed mid-turn; detail: "the player's" or "its AI's"
    SettingsChanged,    // the settings were set anew (a reload, a tool)
    ConditionExpired,   // who: a hold ran out by itself, the machine not ending it
    ActionGivenBack,    // who: its command failed without acting; detail: how; command
    CastCharging,       // who: a slow cast begun or a turn spent on it; value: the ability; detail: turns still to go
    CastLanded,         // who: the charged cast is cast, at its turn's start; value: the ability
    CastInterrupted,    // who: the charge is lost; value: the ability; detail: why
    // With a turn a bar of time (Settings::turnClock): an action that did
    // not fit what was left of its creature's bar.
    ActionPending, // who: begun, or a turn spent on it; detail: the time it still owes; command; value: its ability
    ActionLanded,  // who: its time paid, it is given to its creature as its turn begins; command; value: its ability
    ActionDropped, // who: it will not land; detail: why; command; value: its ability
    // A party member's turn put off to the round's end, nothing of it spent
    // (Settings::delay): the turn_started before it is closed by this, and
    // another turn_started follows when its turn comes.
    TurnDelayed, // who
};

// snake_case, as the bus's topics take it: "turn_started".
[[nodiscard]] const char *Name(EventKind kind);

// What an event means and what its fields say, for a test server's list of
// topics (Eluvian's `events describe=true`): an agent reads these before
// waiting on one. A field it does not carry is null.
struct EventDescription
{
    const char *meaning = nullptr;
    bool creature = false; // carries id and name
    const char *detail = nullptr;
    const char *value = nullptr;
    const char *command = nullptr;
    const char *note = nullptr;
};
[[nodiscard]] EventDescription Describe(EventKind kind);

// Every kind, in order, for listing them.
inline constexpr EventKind kLastEventKind = EventKind::TurnDelayed;

struct TurnEvent
{
    EventKind kind = EventKind::FightBegan;
    std::uint32_t worldMs = 0; // the fight's clock, the world's milliseconds
    int round = 0;
    ObjectId who = kNoObject;
    std::string name; // who's, UTF-8
    std::string detail;
    std::int32_t value = 0;
    std::string command; // a refusal's: the command refused ("attack")
    std::string note;    // a refusal's numbers: "12.1 m to walk, 9.0 m left"
};

using EventSink = std::function<void(const TurnEvent &event)>;

// One JSON object on one line: {"ms":..,"world_ms":..,"round":..,"event":"turn_started",
// "id":"0x82","name":"Aedan","detail":"yours","value":90}. realMs is the
// caller's clock. Ids unpadded and named "id", as the log and Eluvian write
// them, so an agent's id matches everywhere (eluvian-agent). Strings are
// escaped as JSON wants; the id is left out for no one.
[[nodiscard]] std::string ToJson(const TurnEvent &event, std::uint64_t realMs);

} // namespace tb
