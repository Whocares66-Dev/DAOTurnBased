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
//  "movement_left":19.6,"movement_max":19.6,"time_left_ms":4000,
//  "time_ms":4000 (the turn's bar of time, and what is left of it),
//  "can_delay":true,"rolled_ms":0,"player_paused":false,
//  "speed":1.0,"speed_took":true,"held_by_effect":false,
//  "last_refusal":"...","notice":"" (the caption's line of an action given
//  back, while it shows),"order":[{"id":"0x...","name":"...","side":"party",
//  "rank":100,"health":0.85,"energy":1.0,"reserved":0.15 (of the same arc, what its
//  sustained abilities hold back),"mana":false,"down":false,
//  "current":true,"yours":true,"round":2,"auto":false (a party member's
//  only: its tactics play its turns, from its next one; core/PartyTactics.h)},
//  ...],"held":[{"id":"0x...",
//  "name":"...","condition":"disabled","on_clock":false,"owed_ms":2500}],
//  "cooldowns":[{"id":"0x...","ability":617,"left_ms":12000,"of_ms":20000,
//  "item":"..." (an item's ability's only: the item's tag)}],
//  "charging":[{"id":"0x...","ability":10002,"owed_ms":1500}]}
// (Until issue 50 it also had "timed" and "actions_left", a held one's
// "turns_owed", a cooldown's "turns" and "of" and a charge's "turns": the
// action clock's, gone with it.)
[[nodiscard]] std::string ToJson(const MachineState &state);

} // namespace tb
