#include "core/ActionTime.h"

#include <algorithm>

namespace tb
{

ActionTime::ActionTime(std::uint32_t costMs)
    : remainingMs_(costMs), state_(costMs == 0 ? ActionTimeState::Ready : ActionTimeState::Pending)
{
}

ActionTimeState ActionTime::State() const
{
    return state_;
}

std::uint32_t ActionTime::Remaining() const
{
    return remainingMs_;
}

ActionTimeStep ActionTime::Advance(TurnTime &turn, std::optional<std::uint32_t> untilEventMs)
{
    TimePayment payment;
    if (state_ == ActionTimeState::Pending)
    {
        const auto limit = untilEventMs ? turn.CapacityUntilClock(*untilEventMs) : turn.CapacityLeft();
        payment = turn.Spend(std::min(remainingMs_, limit));
        remainingMs_ -= payment.capacityMs;
        if (untilEventMs && payment.clockMs == *untilEventMs)
        {
            state_ = ActionTimeState::AwaitingEvent;
        }
        else if (remainingMs_ == 0)
        {
            state_ = ActionTimeState::Ready;
        }
    }
    return {payment, state_, turn.CapacityLeft() == 0};
}

void ActionTime::ResolveEvent(bool interrupted)
{
    if (state_ != ActionTimeState::AwaitingEvent)
    {
        return;
    }
    if (interrupted)
    {
        (void)Interrupt();
    }
    else
    {
        state_ = remainingMs_ == 0 ? ActionTimeState::Ready : ActionTimeState::Pending;
    }
}

bool ActionTime::TakeReady()
{
    if (state_ != ActionTimeState::Ready)
    {
        return false;
    }
    state_ = ActionTimeState::Dispatched;
    return true;
}
std::uint32_t ActionTime::Interrupt()
{
    if (state_ == ActionTimeState::Dispatched)
    {
        return 0;
    }
    const auto unpaid = remainingMs_;
    remainingMs_ = 0;
    state_ = ActionTimeState::Interrupted;
    return unpaid;
}

} // namespace tb
