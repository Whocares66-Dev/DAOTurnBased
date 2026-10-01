// The hooks that put the engine's decisions to the turn machine:
//
//   - CCObject::QueueCommand, where every command any object is given goes
//     (DAOLib's dev/TURN_BASED.md, "The queue in play"): the gate. A
//     command the machine refuses is never queued.
//   - The pause key (GameModeController's UserTogglePauseMessage handler):
//     during a turn-based fight it ends a party member's turn instead.
//   - The scripts' ToggleGamePause: the game's auto-pause on combat
//     (module_core.nss, EVENT_TYPE_AUTOPAUSE) would unpause a turn-based
//     fight half a second in; during one it does nothing.
//   - Explore mode's handlers for the player's orders (a click to move, an
//     ability chosen, a target clicked): watched, not changed, so the
//     machine knows an order was given even if the engine queues nothing
//     for it while paused (core/TurnMachine.h, Phase::Dispatching), and so
//     the log ties each order to the QueueCommand caller that follows it.
//
// Each locks the machine's mutex for as long as the machine takes to
// answer, and never while calling the engine.
#pragma once

#include "core/TurnMachine.h"

#include <mutex>

namespace tb::game
{

// Places the hooks, at plugin load. False if any could not be placed; the
// log says which.
bool InstallGate(TurnMachine &machine, std::mutex &mutex);

// Settings::pathCheck: whether the player's moves and attacks are measured
// by the engine's own path (DAOLib's world::FindPath).
void SetPathCheck(bool on);

} // namespace tb::game
