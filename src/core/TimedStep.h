// The step a creature's command is at, where the engine itself times it, and
// how much of the watcher's time went by with that step advancing: the time
// the machine's limits do not count (issue 28).
//
// The limits (max_action_ms, max_turn_ms) are for actions that stand still:
// a walk that got nowhere, an attack waiting on something that never comes.
// They were cutting actions that were only long. Measured in the game
// (2026-10-05, staged, the fight at three times the speed): an ogre's death
// blow is the engine's "play an animation" sub-action, 37, with a length of
// 12,500 ms; its elapsed time climbs to that over about twelve seconds of
// the wall (three in the first second, then about one a second, slower in
// the middle: the engine's own pacing of it) and the blow ends by itself.
// The action limit, twelve seconds of the wall, came half a second before
// its end: on a turn its tactics played the blow was stopped and the turn
// lost, `took too long`, an ogre's twice and the high dragon's once in the
// playthrough. A person's death blow is 2,000 to 2,300 ms. The user had
// asked whether those blows hung or were only long; they were long.
//
// So the time the engine has an action at a timed step that is advancing is
// not counted by the limits at all: their counts stand still while it
// advances and go on from where they were when it stops or ends. The first
// build of this deferred the limit while the step advanced and judged it
// "as before" after, and play showed the hole (2026-10-06): the command has
// a tail after its timed step (2.7 s on one death blow, at a sub-action the
// engine does not time), and a frame into it the limit found its count long
// spent, stopped the blow and ended the turn. Not counted, the tail has what
// the action had left of its allowance when the step began.
//
// The game layer reads the step each frame it looks at the turn's creature
// (DAOLib's dao::commands::SubActions, the running one first) and hands the
// machine its elapsed time and its length, whatever the command: the rule is
// the limit's, not a death blow's.
//
// "Advancing", between one look and the next: the step was timed with time
// left at the first, and its elapsed time is another at the second. By
// movement and not by the length alone, so that a step whose clock stands
// is counted from the look it stands at: one the engine has stopped, or one
// spent and still there. A step put back to its start, or its length pushed
// ahead as it runs (a looping animation: DAOLib's reading of sub-action 37
// has a mode that does so while a flag holds it), moves all the same and
// would never end; the machine bounds what it leaves uncounted
// (TurnMachine::kStepFree).
#pragma once

#include <cstdint>

namespace tb
{

struct StepTime
{
    std::int32_t elapsedMs = 0;
    // 0 for a step the engine does not time, or one it times that has not
    // begun (an animation takes its length only as it begins); and for no
    // step at all.
    std::int32_t durationMs = 0;
};

// Timed, and not yet at its end.
[[nodiscard]] bool HasTimeLeft(const StepTime &step);

// The step as it was at the last look, and when that was.
class StepWatch
{
  public:
    // A look at the step, at `clock` of the watcher's own time. Answers how
    // much of that time since the look before went by with the step
    // advancing: all of it, where the step had time left then and its
    // elapsed time has moved since; none otherwise, and none at the first
    // look, which has no step before it.
    [[nodiscard]] std::uint32_t Look(const StepTime &step, std::uint32_t clock);
    [[nodiscard]] const StepTime &Last() const
    {
        return last_;
    }

  private:
    StepTime last_;
    std::uint32_t lookedAt_ = 0;
};

} // namespace tb
