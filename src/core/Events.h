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
    TurnLost,           // who: its condition took it; value: turns still owed
    Refused,            // who; detail: what the player was told ("Too far", "No action left", ...)
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
};

// snake_case, as the bus's topics take it: "turn_started".
[[nodiscard]] const char *Name(EventKind kind);

struct TurnEvent
{
    EventKind kind = EventKind::FightBegan;
    std::uint32_t worldMs = 0; // the fight's clock, the world's milliseconds
    int round = 0;
    ObjectId who = kNoObject;
    std::string name; // who's, UTF-8
    std::string detail;
    std::int32_t value = 0;
};

using EventSink = std::function<void(const TurnEvent &event)>;

// One JSON object on one line: {"ms":..,"world_ms":..,"round":..,"event":"turn_started",
// "id":"0x82","name":"Aedan","detail":"yours","value":90}. realMs is the
// caller's clock. Ids unpadded and named "id", as the log and Eluvian write
// them, so an agent's id matches everywhere (eluvian-agent). Strings are
// escaped as JSON wants; the id is left out for no one.
[[nodiscard]] std::string ToJson(const TurnEvent &event, std::uint64_t realMs);

} // namespace tb
