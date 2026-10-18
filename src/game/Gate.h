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

// With a turn a bar of time, the seconds a plain attack of this creature's
// costs, as the gate will charge it (the swing by the game's formula, or
// the core's stand-in). On the scripts' thread.
[[nodiscard]] float AttackSeconds(ObjectId attacker);
// And an ability's, as the gate will charge its user for it.
[[nodiscard]] float AbilitySecondsOf(ObjectId user, std::int32_t ability);

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
// The same, left kept: for the walk to its target (EffectKind::Approach).
[[nodiscard]] std::optional<HeldCast> PeekHeldCast(ObjectId creature);
void DropHeldCast(ObjectId creature);
// Spells given to the engine to conjure, held (core: Verdict::Conjure;
// DAOLib's dao/Conjure.h): released, their conjure let complete; dropped,
// their command cleared (kNoObject: every one, let run on, the fight over).
bool ReleaseEngineConjure(ObjectId caster); // true: it was one
bool DropEngineConjure(ObjectId caster);
// One creature's animation stood still, or let go on (core: EffectKind::Freeze;
// DAOLib's dao::animation::Pause): a grab between its grabber's turns. Its end
// is the grabber's command's (TurnMachine::OnFrozenActionEnded).
void FreezeAnimation(ObjectId creature, bool frozen);

} // namespace tb::game
