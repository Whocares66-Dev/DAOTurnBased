// Incremental payment of one action, without invoking the game. A timer
// boundary yields control before the action is allowed to become ready.
#pragma once

#include "core/TurnTime.h"

#include <cstdint>
#include <optional>

namespace tb
{

enum class ActionTimeState : std::uint8_t
{
    Pending,
    AwaitingEvent,
    Ready,
    Dispatched,
    Interrupted,
};

struct ActionTimeStep
{
    TimePayment payment;
    ActionTimeState state = ActionTimeState::Pending;
    bool turnExhausted = false;
};

class ActionTime
{
  public:
    explicit ActionTime(std::uint32_t costMs);

    [[nodiscard]] ActionTimeState State() const;
    [[nodiscard]] std::uint32_t Remaining() const;

    // The next event's distance on the creature's clock, not capacity.
    // No event means payment can run until the action or this turn ends.
    // At a boundary, apply the event outside the core and call ResolveEvent
    // before continuing. An event simultaneous with completion wins first.
    [[nodiscard]] ActionTimeStep Advance(TurnTime &turn, std::optional<std::uint32_t> untilEventMs = std::nullopt);
    void ResolveEvent(bool interrupted);
    // After final target/resource validation, grant dispatch exactly once.
    [[nodiscard]] bool TakeReady();

    // For a dead/out-of-reach target, call before any payment on resumption.
    // Returns the unpaid cost once; already paid turn time is never refunded.
    [[nodiscard]] std::uint32_t Interrupt();

  private:
    std::uint32_t remainingMs_;
    ActionTimeState state_;
};

} // namespace tb
