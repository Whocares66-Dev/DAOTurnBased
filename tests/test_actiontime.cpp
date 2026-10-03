#include "core/ActionTime.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using tb::ActionTimeState;

TEST_CASE("action payment yields at each timer before completion", "[actiontime]")
{
    tb::TurnTime turn(4000, 1000);
    tb::ActionTime action(4500);
    const auto first = action.Advance(turn, 1000);
    CHECK(first.payment.capacityMs == 1250);
    CHECK(first.payment.clockMs == 1000);
    REQUIRE(first.state == ActionTimeState::AwaitingEvent);
    CHECK(action.Remaining() == 3250);
    // The caller has not applied the event yet: no further payment is allowed.
    CHECK(action.Advance(turn).payment.capacityMs == 0);
    CHECK(action.State() == ActionTimeState::AwaitingEvent);
    action.ResolveEvent(false);
    const auto second = action.Advance(turn, 500);
    CHECK(second.payment.capacityMs == 625);
    CHECK(second.payment.clockMs == 500);
    REQUIRE(second.state == ActionTimeState::AwaitingEvent);
    action.ResolveEvent(false);
    const auto last = action.Advance(turn);
    CHECK(last.state == ActionTimeState::Ready);
    CHECK(last.payment.capacityMs == 2625);
    CHECK(last.payment.clockMs == 2100);
    CHECK(turn.ClockElapsed() == 3600);
    CHECK(turn.CapacityLeft() == 500);
    CHECK(action.Advance(turn).payment.capacityMs == 0);
}

TEST_CASE("a lethal tick cancels the remaining action cost", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(3000);
    REQUIRE(action.Advance(turn, 1000).state == ActionTimeState::AwaitingEvent);
    action.ResolveEvent(true);
    CHECK(action.State() == ActionTimeState::Interrupted);
    CHECK(action.Remaining() == 0);
    CHECK(turn.ClockElapsed() == 1000);
    CHECK(turn.CapacityLeft() == 3000);
    CHECK(action.Advance(turn).payment.capacityMs == 0);
    action.ResolveEvent(false); // cannot revive an interrupted action
    CHECK(action.State() == ActionTimeState::Interrupted);
}

TEST_CASE("a timer simultaneous with impact is resolved before readiness", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(1500);
    const auto step = action.Advance(turn, 1500);
    REQUIRE(step.state == ActionTimeState::AwaitingEvent);
    CHECK(action.Remaining() == 0);
    SECTION("a lethal tick prevents impact")
    {
        action.ResolveEvent(true);
        CHECK(action.State() == ActionTimeState::Interrupted);
    }
    SECTION("a harmless event allows impact")
    {
        action.ResolveEvent(false);
        CHECK(action.State() == ActionTimeState::Ready);
    }
}

TEST_CASE("pending actions pay over several turns with different capacities", "[actiontime]")
{
    tb::ActionTime action(10000);
    tb::TurnTime first;
    const auto started = action.Advance(first);
    CHECK(started.turnExhausted);
    CHECK(started.state == ActionTimeState::Pending);
    CHECK(action.Remaining() == 6000);
    tb::TurnTime second(4000, 1000);
    const auto continued = action.Advance(second);
    CHECK(continued.turnExhausted);
    CHECK(continued.payment.clockMs == 4000);
    CHECK(action.Remaining() == 1000);
    tb::TurnTime third;
    const auto landed = action.Advance(third);
    CHECK(landed.state == ActionTimeState::Ready);
    CHECK_FALSE(landed.turnExhausted);
    CHECK(third.CapacityLeft() == 3000);
}

TEST_CASE("an invalid pending target releases only unpaid cost before resuming", "[actiontime]")
{
    tb::ActionTime action(6000);
    tb::TurnTime first;
    CHECK(action.Advance(first).payment.capacityMs == 4000);
    tb::TurnTime next;
    CHECK(action.Interrupt() == 2000);
    CHECK(action.Interrupt() == 0);
    CHECK(action.Advance(next).state == ActionTimeState::Interrupted);
    CHECK(first.ClockElapsed() == 4000);
    CHECK(next.ClockElapsed() == 0);
    CHECK(next.CapacityLeft() == 4000);
}

TEST_CASE("events due now do not consume capacity and precede new payment", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(1000);
    const auto step = action.Advance(turn, 0);
    CHECK(step.state == ActionTimeState::AwaitingEvent);
    CHECK(step.payment.capacityMs == 0);
    CHECK(step.payment.clockMs == 0);
    action.ResolveEvent(false);
    CHECK(action.Advance(turn, 0).state == ActionTimeState::AwaitingEvent);
    action.ResolveEvent(false);
    CHECK(action.Advance(turn, 2000).state == ActionTimeState::Ready);
    CHECK(turn.ClockElapsed() == 1000);
}

TEST_CASE("event boundaries retain fractions across small movement payments", "[actiontime]")
{
    for (const auto capacity : {4000u, 4001u, 5000u, 6300u, 7000u})
    {
        tb::TurnTime turn(4000, capacity - 4000);
        (void)turn.Spend(1);
        const auto before = turn.ClockElapsed();
        tb::ActionTime action(capacity);
        const auto step = action.Advance(turn, 17);
        CHECK(step.state == ActionTimeState::AwaitingEvent);
        CHECK(step.payment.clockMs == 17);
        CHECK(turn.ClockElapsed() == before + 17);
    }
}

TEST_CASE("events beyond the turn do not fire at the turn boundary", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(8000);
    const auto step = action.Advance(turn, 5000);
    CHECK(step.state == ActionTimeState::Pending);
    CHECK(step.turnExhausted);
    CHECK(step.payment.clockMs == 4000);
    CHECK(action.Advance(turn, 1000).payment.clockMs == 0);
}

TEST_CASE("zero-cost actions and large event distances do not overflow", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime free(0);
    CHECK(free.State() == ActionTimeState::Ready);
    CHECK(free.Advance(turn).payment.capacityMs == 0);
    tb::ActionTime action(1000);
    CHECK(action.Advance(turn, std::numeric_limits<std::uint32_t>::max()).state == ActionTimeState::Ready);
    CHECK(turn.ClockElapsed() == 1000);
}

TEST_CASE("a paid action grants dispatch once and only after its timer", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(1000);
    CHECK_FALSE(action.TakeReady());
    CHECK(action.Advance(turn, 1000).state == ActionTimeState::AwaitingEvent);
    CHECK_FALSE(action.TakeReady());
    action.ResolveEvent(false);
    CHECK(action.TakeReady());
    CHECK_FALSE(action.TakeReady());
    CHECK(action.Advance(turn).state == ActionTimeState::Dispatched);
    CHECK(action.Interrupt() == 0);
    CHECK(action.State() == ActionTimeState::Dispatched);
    CHECK(turn.ClockElapsed() == 1000);
}

TEST_CASE("a timer on the turn boundary is handled before resumption", "[actiontime]")
{
    tb::TurnTime turn;
    tb::ActionTime action(6000);
    const auto step = action.Advance(turn, 4000);
    CHECK(step.turnExhausted);
    CHECK(step.state == ActionTimeState::AwaitingEvent);
    tb::TurnTime next;
    CHECK(action.Advance(next).payment.capacityMs == 0);
    action.ResolveEvent(false);
    CHECK(action.Advance(next).state == ActionTimeState::Ready);
    CHECK(next.CapacityLeft() == 2000);
}
