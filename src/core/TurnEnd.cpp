#include "core/TurnEnd.h"

#include <algorithm>
#include <array>

namespace tb
{

namespace
{

// One line a reason, so that a class is changed in one place (and a mutant
// of tools/mutants.py breaks one). Where each is given is
// src/core/TurnMachine.cpp's EndTurn calls.
constexpr std::array kTurnEnds{
    // Ended by its actor: what the bar still holds rolls over, to the cap.
    //
    // End on the turn bar, its key, a tool.
    TurnEnd{"ended by the player", true},
    // An AI that has acted and has nothing more it will do this turn: it
    // chose to wait after its action, or its settling passed with nothing
    // more chosen.
    TurnEnd{"done", true},
    // An AI told its turn had come that chose to wait, its bar whole.
    TurnEnd{"waits", true},
    // These leave nothing in the bar to roll over, the actor having spent
    // it: the player's bar spent (as built, the action and the movement); an
    // action that did not fit kept for the next turn, the rest of the bar
    // gone to it; a cast that charges begun, or a turn spent on one; a spell
    // given to the engine to conjure, or cast at once.
    TurnEnd{"nothing left to spend", true},
    TurnEnd{"its action pending", true},
    TurnEnd{"charging", true},
    TurnEnd{"its spell conjuring", true},
    TurnEnd{"its spell cast at once", true},

    // Not its actor's doing: nothing rolls over.
    //
    // A hostile that has noticed nobody to fight: its turn is skipped
    // (core/Rules.h, Unaware).
    TurnEnd{"unaware", false},
    // Told its turn had come, its AI queued nothing within ai_think_ms.
    TurnEnd{"chose nothing", false},
    // Its command ran past max_action_ms and was stopped; or, a turn its AI
    // played, the turn itself ran past max_turn_ms and was ended (issue 38).
    TurnEnd{"took too long", false},
    // An AI whose actions were given back twice: stopped by the mod, the
    // time of what it could not do back in its bar.
    TurnEnd{"nothing it can get to", false},
    // An AI stopped where its movement ran out. With time the bar is spent
    // by then; an action given back as it was stopped is not time it saved.
    TurnEnd{"out of movement", false},
    // It fell, or left the world, in its own turn; its turn came while it
    // was down.
    TurnEnd{"fell", false},
    TurnEnd{"gone", false},
    TurnEnd{"down", false},
    // Lost to what holds it: a stun landing in its own turn, a condition's
    // turns, another's action that held it (a grab), an effect's command no
    // condition names.
    TurnEnd{"stunned", false},
    TurnEnd{"held by its condition", false},
    TurnEnd{"held by another's action", false},
    TurnEnd{"held by an effect", false},
};

} // namespace

std::span<const TurnEnd> TurnEnds()
{
    return kTurnEnds;
}

std::optional<bool> EndedByItsActor(std::string_view why)
{
    const auto found = std::ranges::find(kTurnEnds, why, &TurnEnd::why);
    if (found == kTurnEnds.end())
    {
        return std::nullopt;
    }
    return found->byItsActor;
}

bool RollsOver(std::string_view why)
{
    return EndedByItsActor(why).value_or(false);
}

} // namespace tb
