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
//
// And what the step ran between two looks, by the engine's own count of it:
// what a hold at an effect's command costs (issue 64; TurnMachine::
// ChargeHold). A creature held so is charged the time the engine holds it
// (issue 30), and that time was the watcher's own, the wall's since the look
// before times the speed the fight asked for. In play (2026-10-05 to
// 2026-10-08, the fights at three times the speed) the getting up from a
// knockdown, a step of 2,133 ms by the engine's count, was charged 2.30 to
// 2.81 s: the watcher's clock ran ahead of the step over a long frame (0.61 s
// counted in one frame of a step that then had 1.5 s more to run), and the
// looks after the step's end were charged whole, each 0.14 to 0.37 s of the
// fight's time at that speed. A knockdown whose whole time by the engine was
// under a turn's four seconds lost the turn all the same. The owner, of what a
// knockdown should cost (2026-10-08): "knock down should last for the amount
// of time the game makes it last". The engine's count is its step's elapsed
// time: where that moved between two looks, it is what ran.
#pragma once

#include <cstdint>
#include <optional>

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
    // What the step ran between the last two looks, by the engine's own count
    // of it. A timed step found further on at the second look ran what its
    // elapsed time moved by, whatever the watcher's clock made of that time,
    // and whether or not its length was pushed ahead meanwhile (a knockdown's
    // step is spent at its fall's length while the effect stands, and takes
    // the getting up's length on as the effect ends). One that had time left
    // and is gone at the second look, or begun anew, ran to its end: what it
    // had left, and no more than the watcher's time between the looks, since
    // a step cut short reads the same. Nothing where the engine timed nothing
    // between the looks or its step stood: the first look, a step it does not
    // time, one spent and still there. The watcher's own clock is all there
    // is to go by then.
    [[nodiscard]] std::optional<std::uint32_t> Ran() const
    {
        return ran_;
    }

  private:
    StepTime last_;
    std::uint32_t lookedAt_ = 0;
    std::optional<std::uint32_t> ran_;
};

} // namespace tb
