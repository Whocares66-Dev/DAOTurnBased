#include "core/TurnTime.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("tactics alternates rollover and capacity", "[turntime]")
{
    const tb::TacticsTime expected[] = {{0, 1000}, {0, 1500}, {500, 1500}, {500, 2000}, {1000, 2000}};
    for (int rank = 0; rank <= 4; ++rank)
    {
        CHECK(tb::TimeFromTactics(rank).bonusMs == expected[rank].bonusMs);
        CHECK(tb::TimeFromTactics(rank).rolloverCapMs == expected[rank].rolloverCapMs);
    }
    CHECK(tb::TimeFromTactics(-1).bonusMs == 0);
    CHECK(tb::TimeFromTactics(-1).rolloverCapMs == 1000);
    // Past the player's ranks, an enemy's: an elite boss at 6.
    CHECK(tb::TimeFromTactics(6).bonusMs == 1500);
    CHECK(tb::TimeFromTactics(6).rolloverCapMs == 2500);
    CHECK(tb::TimeFromTactics(8).bonusMs == 2000);
    CHECK(tb::TimeFromTactics(99).bonusMs == 2000);
    CHECK(tb::TimeFromTactics(99).rolloverCapMs == 3000);
}
TEST_CASE("rebalance example keeps cooldown and rollover separate", "[turntime]")
{
    tb::TurnTime first(4000, 1000);
    CHECK(first.Spend(1800).clockMs == 1440);
    // Cooldown starts at impact, after the talent's payment.
    auto cooldown = 10000u;
    cooldown -= first.Spend(1900).clockMs;
    const auto ended = first.Finish(2000);
    cooldown -= ended.clockMs;
    CHECK(ended.rolloverMs == 1300);
    CHECK(cooldown == 7440);
    CHECK(first.ClockElapsed() == 4000);
    tb::TurnTime second(4000, 1000, ended.rolloverMs);
    CHECK(second.Capacity() == 6300);
    CHECK(second.Spend(3150).clockMs == 2000); // poison expires here
    cooldown -= 2000;
    cooldown -= second.Spend(2550).clockMs;
    const auto next = second.Finish(2000);
    cooldown -= next.clockMs;
    CHECK(next.rolloverMs == 600);
    CHECK(cooldown == 3440);
    tb::TurnTime passing(4000, 1000, 1300);
    const auto passed = passing.Finish(2000);
    CHECK(passed.rolloverMs == 2000);
    CHECK(passed.clockMs == 4000);
    CHECK(passing.Finish(2000).rolloverMs == 0);
    CHECK(passing.Finish(2000).clockMs == 0);
}
TEST_CASE("pending work spends rather than enlarges the next bar", "[turntime]")
{
    auto remaining = 10000u;
    for (int turn = 0; turn < 2; ++turn)
    {
        tb::TurnTime time;
        const auto paid = time.Spend(remaining);
        remaining -= paid.capacityMs;
        CHECK(paid.capacityMs == 4000);
        CHECK(paid.clockMs == 4000);
        CHECK(time.Capacity() == 4000);
    }
    tb::TurnTime landing;
    REQUIRE(remaining == 2000);
    const auto paid = landing.Spend(remaining);
    remaining -= paid.capacityMs;
    CHECK(remaining == 0);
    CHECK(landing.CapacityLeft() == 2000);
    CHECK(landing.ClockElapsed() == 2000);
    // Cancellation never refunds time already paid or changes the ratio.
    CHECK(landing.Spend(1000).clockMs == 1000);
    CHECK(landing.Finish(1000).rolloverMs == 1000);
}
TEST_CASE("holds consume clock time at the fixed capacity ratio", "[turntime]")
{
    tb::TurnTime partial(4000, 1000, 2000);
    CHECK(partial.Hold(2000) == 2000);
    CHECK(partial.CapacityLeft() == 3500);
    CHECK(partial.Spend(1750).clockMs == 1000);
    CHECK(partial.Finish(2000).rolloverMs == 1750);
    tb::TurnTime held(4000, 1000, 2000);
    CHECK(held.Hold(6000) == 4000);
    CHECK(held.CapacityLeft() == 0);
    CHECK(held.Finish(2000).rolloverMs == 0);
    tb::TurnTime following(4000, 1000);
    CHECK(following.Hold(2000) == 2000);
    CHECK(following.CapacityLeft() == 2500);
}
TEST_CASE("interruption charges only progress up to its event", "[turntime]")
{
    tb::TurnTime time;
    // The future scheduler splits a 3 s action at an event due in 1 s.
    CHECK(time.Spend(1000).clockMs == 1000);
    CHECK(time.Hold(1000) == 1000);
    // The cancelled action's remaining 2 s are never charged.
    CHECK(time.CapacityLeft() == 2000);
    CHECK(time.Spend(1500).capacityMs == 1500);
    const auto ended = time.Finish(1000);
    CHECK(ended.rolloverMs == 500);
    CHECK(ended.clockMs == 500);
}
TEST_CASE("small movement payments retain fractional clock progress", "[turntime]")
{
    tb::TurnTime whole(4000, 1000, 1300);
    tb::TurnTime frames(4000, 1000, 1300);
    const auto once = whole.Spend(5700);
    auto elapsed = 0u;
    for (int i = 0; i < 5700; ++i)
    {
        elapsed += frames.Spend(1).clockMs;
    }
    CHECK(elapsed == once.clockMs);
    CHECK(frames.CapacityLeft() == whole.CapacityLeft());
    elapsed += frames.Finish(2000).clockMs;
    CHECK(elapsed == 4000);
}
TEST_CASE("fractional hold capacity is retained without per-tick rounding", "[turntime]")
{
    tb::TurnTime time(4000, 1);
    for (int i = 0; i < 2000; ++i)
    {
        CHECK(time.Hold(1) == 1);
    }
    CHECK(time.CapacityLeft() == 2000); // 2000.5 ms internally
    CHECK(time.Spend(2000).clockMs == 1999);
    CHECK(time.CapacityLeft() == 0);
    CHECK(time.Finish(1000).clockMs == 1);
}
TEST_CASE("time accounting bounds invalid and oversized inputs", "[turntime]")
{
    constexpr auto max = std::numeric_limits<std::uint32_t>::max();
    tb::TurnTime zero(0);
    CHECK(zero.Capacity() == 1);
    CHECK(zero.Spend(0).clockMs == 0);
    CHECK(zero.Hold(0) == 0);
    CHECK(zero.Spend(max).capacityMs == 1);
    CHECK(zero.Spend(max).clockMs == 0);
    tb::TurnTime large(max, max, max);
    CHECK(large.Capacity() == max);
    CHECK(large.Spend(max - 1).clockMs == max - 1);
    CHECK(large.Hold(max) == 1);
    CHECK(large.ClockLeft() == 0);
    CHECK(large.Finish(max).rolloverMs == 0);
}
