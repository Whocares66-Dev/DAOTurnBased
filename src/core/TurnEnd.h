// Why a turn ended, and whether what it left unspent rolls over (issue 29).
//
// With a turn a bar of time, what is left of the bar as a turn ends rolls
// over to its creature's next, up to its cap (dev/REBALANCE.md, "Current
// specification"). Until 2026-10-04 it rolled over whoever had ended the
// turn: `Skeleton's turn ends: chose nothing; 1.00 s rolls over`, `Alistair's
// turn ends: held by an effect; 1.00 s rolls over`. The user, that day:
// "When turn is skipped, due to unawareness, incapacity, etc, there should be
// no time rollover, because the actor did not explicitly save any time".
//
// So time rolls over only from a turn its actor ended itself, with time it
// chose not to spend: the player pressing End, an AI that is done or waits.
// A turn that was skipped, lost to what held its creature, or ended for it
// because it could not act, carries nothing over.
//
// Every reason the machine ends a turn for is here, in one of those two
// classes, and nowhere else: the reason is the words TurnMachine::EndTurn is
// given, which the log's "X's turn ends: ..." and the turn_ended event's
// detail carry. A reason that is not here rolls nothing over, and the machine
// says so with a warning; tools/test_turnend.py reads the machine's source
// and fails on one this table does not class, so a new reason is classed
// when it is written.
#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace tb
{

struct TurnEnd
{
    std::string_view why;
    // Its actor ended it, and what it left unspent was its own to save. Not:
    // the turn was taken from it (skipped, lost, cut short), and whatever
    // its bar still held is gone.
    bool byItsActor = false;
};

// Every reason, the turns their actors end first.
[[nodiscard]] std::span<const TurnEnd> TurnEnds();

// The class of a reason; nothing for one the table does not have.
[[nodiscard]] std::optional<bool> EndedByItsActor(std::string_view why);

// Whether what a turn that ended so left of its bar rolls over: only one its
// actor ended. A reason the table does not have rolls nothing over.
[[nodiscard]] bool RollsOver(std::string_view why);

} // namespace tb
