#include "core/TimedStep.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace tb
{

bool HasTimeLeft(const StepTime &step)
{
    return step.durationMs > 0 && step.elapsedMs < step.durationMs;
}

namespace
{
// What a step ran from one look (`before`) to the next (`now`), `since` of the
// watcher's time apart, by the engine's own count (StepWatch::Ran).
std::optional<std::uint32_t> RanBetween(const StepTime &before, const StepTime &now, std::uint32_t since)
{
    const bool further = before.durationMs > 0 && now.durationMs > 0 && now.elapsedMs > before.elapsedMs;
    if (further)
    {
        return static_cast<std::uint32_t>(now.elapsedMs - before.elapsedMs);
    }
    const bool gone = now.durationMs <= 0 || now.elapsedMs < before.elapsedMs;
    if (HasTimeLeft(before) && gone)
    {
        return std::min(static_cast<std::uint32_t>(before.durationMs - before.elapsedMs), since);
    }
    return std::nullopt;
}
} // namespace

std::uint32_t StepWatch::Look(const StepTime &step, std::uint32_t clock)
{
    // Its elapsed time is what moves: a length pushed ahead under an elapsed
    // time that stands is a step standing.
    const bool advanced = HasTimeLeft(last_) && step.elapsedMs != last_.elapsedMs;
    const std::uint32_t since = clock - std::exchange(lookedAt_, clock);
    ran_ = RanBetween(last_, step, since);
    last_ = step;
    return advanced ? since : 0;
}

} // namespace tb
