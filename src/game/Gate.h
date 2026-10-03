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
#include <optional>

namespace tb::game
{

// Places the hooks, at plugin load. False if any could not be placed; the
// log says which.
bool InstallGate(TurnMachine &machine, std::mutex &mutex);

// Settings::pathCheck: whether the player's moves and attacks are measured
// by the engine's own path (DAOLib's world::FindPath).
void SetPathCheck(bool on);

// How near `attacker` must come to strike `target`: its weapon's range and
// the target's own space (core/Rules.h, Reach), from the game's tables, as
// the gate judges a player's attack by. `weaponType`, if asked for, is the
// main hand's base item type, -1 for none. Script calls: on the scripts'
// thread. The movement preview shows an attack's walk by the same number.
[[nodiscard]] float ReachOf(ObjectId attacker, ObjectId target, std::int32_t *weaponType = nullptr);

// A cast that charges (core: Verdict::Hold): the gate refuses the command
// and keeps what it was, the ability, its target and the place, for the
// bridge to give again when its turns are up (EffectKind::ReleaseCast) or
// to forget (DropCast; no creature: every one). On the scripts' thread.
struct HeldCast
{
    // With a turn a bar of time, an attack is kept too, until its time is
    // paid (core: TurnMachine::PayAction); the rest are ability uses.
    CommandType type = CommandType::UseAbility;
    std::int32_t ability = -1;
    ObjectId target = kNoObject;
    bool placed = false; // aimed at a place
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
[[nodiscard]] std::optional<HeldCast> TakeHeldCast(ObjectId creature);
void DropHeldCast(ObjectId creature);

} // namespace tb::game
