// The machine's whole state as JSON (TurnMachine::State): what a test server
// answers an agent's inspect turnbased.state with (eluvian-agent's MCP,
// 2026-09-30). Creature ids are unpadded "0x82" strings, as the log and the server's
// own tools write them, so an agent can match them across the two.
#pragma once

#include "core/TurnMachine.h"

#include <string>

namespace tb
{

// {"active":true,"phase":"awaiting order","round":2,"world_ms":12345,
//  "current":{"id":"0x82","name":"Aedan","side":"party","yours":true} (null
//  between turns), "player_turn":true (current's yours, for one build more),
//  "actions_left":1,
//  "movement_left":7.3,"movement_max":9.0,"speed":1.0,"speed_took":true,
//  "held_by_effect":false,
//  "last_refusal":"...","notice":"" (the caption's line of an action given
//  back, while it shows),"order":[{"id":"0x...","name":"...","side":"party",
//  "rank":100,"health":0.85,"energy":1.0,"mana":false,"down":false,
//  "current":true,"yours":true,"round":2},...],"held":[{"id":"0x...",
//  "name":"...","condition":"disabled","turns_owed":1,"on_clock":false}],
//  "cooldowns":[{"id":"0x...","ability":617,"turns":2}]}
[[nodiscard]] std::string ToJson(const MachineState &state);

} // namespace tb
