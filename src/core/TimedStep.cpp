#include "core/TimedStep.h"

#include <utility>

namespace tb
{

bool HasTimeLeft(const StepTime &step)
{
    return step.durationMs > 0 && step.elapsedMs < step.durationMs;
}

std::uint32_t StepWatch::Look(const StepTime &step, std::uint32_t clock)
{
    // Its elapsed time is what moves: a length pushed ahead under an elapsed
    // time that stands is a step standing.
    const bool advanced = HasTimeLeft(last_) && step.elapsedMs != last_.elapsedMs;
    const std::uint32_t since = clock - std::exchange(lookedAt_, clock);
    last_ = step;
    return advanced ? since : 0;
}

} // namespace tb
