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
    // And a turn spent on a mode of its creature's own that keeps it at a
    // command while it is on (Captivating Song; issue 51): the player pressed
    // End and the song goes on, or its tactics have the mode on. Its actor's
    // doing either way, and the machine spends the bar on the mode as it
    // ends the turn so (TurnMachine::SpentOnItsMode): nothing is left.
    TurnEnd{"keeping up its mode", true},

    // Not its actor's doing: nothing rolls over.
    //
    // A hostile that has noticed nobody to fight: its turn is skipped
    // (core/Rules.h, Rotation::Unaware).
    TurnEnd{"unaware", false},
    // One that is not of the party, taken out of combat by the game in its
    // own turn, or found so as its turn came: it has no turn, and what is
    // left of this one is nobody's to save (issue 43).
    TurnEnd{"out of combat", false},
    // One the game has made inactive in the middle of its turn: out of the
    // world, it has no turn (issue 44). (One found so as its turn comes has
    // left the fight by then, and that turn ends "down": issue 66.)
    TurnEnd{"inactive", false},
    // Told its turn had come, its AI queued nothing within ai_think_ms.
    TurnEnd{"chose nothing", false},
    // A turn its AI played: its command ran past max_action_ms and was
    // stopped, or the turn itself ran past max_turn_ms and was ended (issue
    // 38). Never a turn the player plays, which no clock of the mod's ends
    // (issue 51).
    TurnEnd{"took too long", false},
    // An AI whose actions were given back twice: stopped by the mod, the
    // time of what it could not do back in its bar.
    TurnEnd{"nothing it can get to", false},
    // An AI stopped where its movement ran out. With time the bar is spent
    // by then; an action given back as it was stopped is not time it saved.
    TurnEnd{"out of movement", false},
    // It fell, or left the world, in its own turn; a scan in the middle of
    // its turn found it gone from the fight (issue 66); its turn came while
    // it was down, fallen or gone from the fight.
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
