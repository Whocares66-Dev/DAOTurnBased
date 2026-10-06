#include "core/TurnTime.h"
#include <algorithm>
#include <limits>
namespace tb
{
TacticsTime TimeFromTactics(int rank)
{
    const auto bounded = static_cast<std::uint32_t>(std::clamp(rank, 0, kMostTactics));
    return {500 * (bounded / 2), 1000 + (500 * ((bounded + 1) / 2))};
}
TurnTime::TurnTime(std::uint32_t turnMs, std::uint32_t bonusMs, std::uint32_t rolloverMs)
    : turnMs_(std::max(1u, turnMs)),
      capacityMs_(static_cast<std::uint32_t>(std::min<std::uint64_t>(
          static_cast<std::uint64_t>(turnMs_) + bonusMs + rolloverMs, std::numeric_limits<std::uint32_t>::max())))
{
}
std::uint64_t TurnTime::Total() const
{
    return static_cast<std::uint64_t>(capacityMs_) * turnMs_;
}
std::uint32_t TurnTime::Capacity() const
{
    return capacityMs_;
}
std::uint32_t TurnTime::CapacityLeft() const
{
    return static_cast<std::uint32_t>((Total() - progress_) / turnMs_);
}
std::uint32_t TurnTime::ClockElapsed() const
{
    return static_cast<std::uint32_t>(progress_ / capacityMs_);
}
std::uint32_t TurnTime::ClockLeft() const
{
    return turnMs_ - ClockElapsed();
}
std::uint32_t TurnTime::CapacityUntilClock(std::uint32_t clockMs) const
{
    if (clockMs == 0)
    {
        return 0;
    }
    const auto target = ClockElapsed() + std::min(clockMs, ClockLeft());
    const auto boundary = static_cast<std::uint64_t>(target) * capacityMs_;
    // Capacity is at least turnMs_, so one capacity millisecond cannot
    // skip over a whole clock millisecond. Round up to reach the event.
    const auto needed = (boundary - progress_ + turnMs_ - 1) / turnMs_;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(needed, CapacityLeft()));
}
TimePayment TurnTime::Spend(std::uint32_t capacityMs)
{
    const auto before = ClockElapsed();
    const auto paid = std::min(capacityMs, CapacityLeft());
    progress_ += static_cast<std::uint64_t>(paid) * turnMs_;
    return {paid, ClockElapsed() - before};
}
std::uint32_t TurnTime::Hold(std::uint32_t clockMs)
{
    const auto before = ClockElapsed();
    progress_ += std::min(static_cast<std::uint64_t>(clockMs) * capacityMs_, Total() - progress_);
    return ClockElapsed() - before;
}
void TurnTime::Refund(std::uint32_t capacityMs)
{
    progress_ -= std::min(static_cast<std::uint64_t>(capacityMs) * turnMs_, progress_);
}
TurnTimeEnd TurnTime::Finish(std::uint32_t rolloverCapMs)
{
    const TurnTimeEnd result{std::min(rolloverCapMs, CapacityLeft()), ClockLeft()};
    progress_ = Total();
    return result;
}
} // namespace tb
