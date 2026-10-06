#include "core/TurnBudget.h"

#include <algorithm>
#include <cmath>

namespace tb
{

TurnBudget TurnBudget::Time(const TurnTime &time, float paceMps, float freeMeters)
{
    TurnBudget budget;
    budget.bar_ = true;
    budget.time_ = time;
    budget.pace_ = paceMps > 0.0f ? paceMps : 1.0f;
    budget.free_ = std::max(0.0f, freeMeters);
    return budget;
}

bool TurnBudget::TimeLeft() const
{
    return bar_ && time_.CapacityLeft() > 0;
}

std::uint32_t TurnBudget::SpendAction(std::uint32_t costMs)
{
    if (!bar_)
    {
        return 0;
    }
    const TimePayment paid = time_.Spend(costMs);
    return costMs - paid.capacityMs;
}

void TurnBudget::GiveActionBack(std::uint32_t chargedMs)
{
    if (!bar_)
    {
        return;
    }
    time_.Refund(chargedMs);
}

void TurnBudget::SpendAllActions()
{
    if (!bar_)
    {
        return;
    }
    // The free meter and meters paid for go with it: a creature whose
    // action is pending may not walk (dev/REBALANCE.md, "Actions"). In the
    // random fights an AI walked its free meter after its action was kept,
    // and on into the next creature's turn (test_fights.cpp, seed 130).
    (void)time_.Spend(time_.CapacityLeft());
    freeWalked_ = std::max(freeWalked_, free_);
    credit_ = 0.0f;
    owedMeters_ = 0.0f;
}

void TurnBudget::Pend()
{
    if (!bar_)
    {
        return;
    }
    (void)time_.Spend(time_.CapacityLeft());
    freeWalked_ = std::max(freeWalked_, free_);
    owedMeters_ = 0.0f;
}

float TurnBudget::MovementLeft() const
{
    if (!bar_)
    {
        return 0.0f;
    }
    const float freeLeft = std::max(0.0f, free_ - freeWalked_);
    const float timed = (static_cast<float>(time_.CapacityLeft()) / 1000.0f * pace_) - owedMeters_;
    return credit_ + freeLeft + std::max(0.0f, timed);
}

float TurnBudget::MovementMax() const
{
    if (!bar_)
    {
        return 0.0f;
    }
    return walked_ - overwalked_ + MovementLeft();
}

float TurnBudget::Overwalked() const
{
    // (With no bar there is nothing to walk: every meter is one past it.)
    return bar_ ? overwalked_ : walked_;
}

void TurnBudget::Walk(float meters)
{
    if (!(meters > 0.0f))
    {
        return;
    }
    walked_ += meters;
    if (!bar_)
    {
        return;
    }
    // Meters paid for first, then the free meter, then time at the
    // creature's pace. A frame's step is a few centimeters, less than a
    // millisecond's walk: what is owed is kept until a whole millisecond is.
    const float credited = std::min(meters, credit_);
    credit_ -= credited;
    meters -= credited;
    if (!(meters > 0.0f))
    {
        return;
    }
    const float freed = std::min(meters, std::max(0.0f, free_ - freeWalked_));
    freeWalked_ += freed;
    owedMeters_ += meters - freed;
    const auto wholeMs = static_cast<std::uint32_t>(std::floor(owedMeters_ / pace_ * 1000.0f));
    if (wholeMs == 0)
    {
        return;
    }
    const TimePayment paid = time_.Spend(wholeMs);
    owedMeters_ -= static_cast<float>(wholeMs) / 1000.0f * pace_;
    if (paid.capacityMs < wholeMs)
    {
        // Walked past the time there was: the machine stops it.
        overwalked_ += static_cast<float>(wholeMs - paid.capacityMs) / 1000.0f * pace_;
    }
}

void TurnBudget::SettleOverwalk()
{
    if (!bar_ || !(overwalked_ > 0.0f))
    {
        return;
    }
    const auto ms = static_cast<std::uint32_t>(std::ceil(overwalked_ / pace_ * 1000.0f));
    const TimePayment paid = time_.Spend(ms);
    overwalked_ = static_cast<float>(ms - paid.capacityMs) / 1000.0f * pace_;
}

std::uint32_t TurnBudget::WalkCostMs(float meters) const
{
    if (!bar_)
    {
        return 0;
    }
    const float rest = meters - credit_ - std::max(0.0f, free_ - freeWalked_);
    if (!(rest > 0.0f))
    {
        return 0;
    }
    return static_cast<std::uint32_t>(std::ceil(rest / pace_ * 1000.0f));
}

void TurnBudget::Prepay(float meters)
{
    if (!bar_ || !(meters > credit_))
    {
        return;
    }
    // What is already paid for stays credit; the free meter is used next,
    // then time for the rest.
    const std::uint32_t costMs = WalkCostMs(meters);
    const float freed = std::min(meters - credit_, std::max(0.0f, free_ - freeWalked_));
    freeWalked_ += freed;
    (void)time_.Spend(costMs);
    credit_ = meters;
}

void TurnBudget::Credit(float meters)
{
    if (meters > 0.0f)
    {
        credit_ += meters;
    }
}

void TurnBudget::SetPace(float paceMps)
{
    if (!bar_ || !(paceMps > 0.0f) || !std::isfinite(paceMps))
    {
        return;
    }
    pace_ = paceMps;
}

float TurnBudget::PaidMeters() const
{
    return bar_ ? credit_ + std::max(0.0f, free_ - freeWalked_) : 0.0f;
}

std::uint32_t TurnBudget::HoldClock(std::uint32_t clockMs)
{
    return bar_ ? time_.Hold(clockMs) : 0;
}

std::uint32_t TurnBudget::ClockElapsedMs() const
{
    return bar_ ? time_.ClockElapsed() : 0;
}

std::uint32_t TurnBudget::CapacityMs() const
{
    return bar_ ? time_.Capacity() : 0;
}

std::uint32_t TurnBudget::CapacityLeftMs() const
{
    return bar_ ? time_.CapacityLeft() : 0;
}

std::uint32_t TurnBudget::Finish(std::uint32_t rolloverCapMs)
{
    if (!bar_)
    {
        return 0;
    }
    return time_.Finish(rolloverCapMs).rolloverMs;
}

} // namespace tb
