// A turn's budget: one action and meters as built, or a bar of time that
// actions and walking both spend (dev/REBALANCE.md).
#include "core/TurnBudget.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using tb::TurnBudget;
using tb::TurnTime;

TEST_CASE("as built, a turn has one action and its meters", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Actions(1, 9.0f);
    CHECK_FALSE(budget.Timed());
    CHECK(budget.ActionsLeft() == 1);
    CHECK(budget.MovementLeft() == Approx(9.0f));
    CHECK(budget.SpendAction(1900) == 0); // no time owed as built
    CHECK(budget.ActionsLeft() == 0);
    budget.GiveActionBack(1900);
    CHECK(budget.ActionsLeft() == 1);
    budget.Walk(4.0f);
    CHECK(budget.MovementLeft() == Approx(5.0f));
    CHECK(budget.Overwalked() == 0.0f);
    budget.Walk(5.5f);
    CHECK(budget.MovementLeft() == 0.0f);
    CHECK(budget.Overwalked() == Approx(0.5f));
    CHECK(budget.MovementMax() == Approx(9.0f));
    budget.SpendAllActions();
    CHECK(budget.ActionsLeft() == 0);
    CHECK(budget.CapacityMs() == 0);
}

TEST_CASE("with time, an action spends its own time and the turn goes on", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 4.66f, 1.0f);
    CHECK(budget.Timed());
    CHECK(budget.CapacityMs() == 4000);
    CHECK(budget.ActionsLeft() == 1);
    CHECK(budget.SpendAction(1500) == 0); // a dagger's swing
    CHECK(budget.CapacityLeftMs() == 2500);
    CHECK(budget.ActionsLeft() == 1); // time left: another may begin
    CHECK(budget.SpendAction(1500) == 0);
    CHECK(budget.CapacityLeftMs() == 1000);
    // The third does not fit: begun, and what is left of it is owed.
    CHECK(budget.SpendAction(1500) == 500);
    CHECK(budget.CapacityLeftMs() == 0);
    CHECK(budget.ActionsLeft() == 0);
}

TEST_CASE("with time, walking is free for a meter and then costs time at the pace", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 5.0f, 1.0f);
    // 4 s at 5 m/s and a free meter: 21 m.
    CHECK(budget.MovementLeft() == Approx(21.0f));
    budget.Walk(1.0f); // the free meter
    CHECK(budget.CapacityLeftMs() == 4000);
    CHECK(budget.MovementLeft() == Approx(20.0f));
    budget.Walk(5.0f); // a second's walk
    CHECK(budget.CapacityLeftMs() == 3000);
    CHECK(budget.MovementLeft() == Approx(15.0f));
    CHECK(budget.Walked() == Approx(6.0f));
    // An action after the walk has what is left.
    CHECK(budget.SpendAction(2000) == 0);
    CHECK(budget.MovementLeft() == Approx(5.0f));
}

TEST_CASE("with time, a walk of many small steps pays for every step", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 4.0f, 0.0f);
    for (int i = 0; i < 400; ++i)
    {
        budget.Walk(0.01f); // a frame's few centimeters: less than 3 ms each
    }
    // 4 m at 4 m/s is a second, to the millisecond or so.
    CHECK(budget.CapacityLeftMs() >= 2999);
    CHECK(budget.CapacityLeftMs() <= 3001);
    CHECK(budget.MovementLeft() == Approx(12.0f).margin(0.01f));
}

TEST_CASE("with time, a walk past the time left is overwalked", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 2.0f, 1.0f);
    CHECK(budget.MovementLeft() == Approx(9.0f));
    budget.Walk(9.0f);
    CHECK(budget.MovementLeft() == Approx(0.0f).margin(0.01f));
    CHECK(budget.Overwalked() == Approx(0.0f).margin(0.01f));
    budget.Walk(0.6f);
    CHECK(budget.Overwalked() == Approx(0.6f).margin(0.01f));
    CHECK(budget.ActionsLeft() == 0);
}

TEST_CASE("with time, an action that never ran has its time back", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 4.66f, 1.0f);
    CHECK(budget.SpendAction(1900) == 0);
    budget.GiveActionBack(1900);
    CHECK(budget.CapacityLeftMs() == 4000);
    // No more than was spent.
    budget.GiveActionBack(5000);
    CHECK(budget.CapacityLeftMs() == 4000);
}

TEST_CASE("with time, Combat Tactics and rollover enlarge the bar, and the end rolls over what is left", "[turnbudget]")
{
    // Rank 4's half second twice, and a second rolled over.
    TurnBudget budget = TurnBudget::Time(TurnTime(4000, 1000, 1000), 4.66f, 1.0f);
    CHECK(budget.CapacityMs() == 6000);
    CHECK(budget.SpendAction(4500) == 0);
    CHECK(budget.Finish(2000) == 1500); // under the cap: all of it
    TurnBudget another = TurnBudget::Time(TurnTime(4000), 4.66f, 1.0f);
    CHECK(another.Finish(1000) == 1000); // nothing spent: the cap
    TurnBudget held = TurnBudget::Time(TurnTime(4000), 4.66f, 1.0f);
    held.Prepay(3.0f);
    held.SpendAllActions();
    CHECK(held.MovementLeft() == 0.0f); // not the free meter, nor what was paid for
    CHECK(held.Finish(1000) == 0);
}

TEST_CASE("a bar of no pace walks nothing, rather than dividing by it", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 0.0f, 0.0f);
    CHECK(budget.MovementLeft() == Approx(4.0f)); // taken as a meter a second
    budget.Walk(-1.0f);
    CHECK(budget.Walked() == 0.0f);
}

TEST_CASE("with time, an attack's walk is paid as it is let through, and walked for nothing", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 5.0f, 1.0f);
    // 6 m: the free meter and a second.
    CHECK(budget.WalkCostMs(6.0f) == 1000);
    budget.Prepay(6.0f);
    CHECK(budget.CapacityLeftMs() == 3000);
    CHECK(budget.MovementLeft() == Approx(6.0f + 15.0f));
    CHECK(budget.SpendAction(2000) == 0); // the swing after it
    budget.Walk(6.0f);                    // the walk itself costs nothing more
    CHECK(budget.CapacityLeftMs() == 1000);
    CHECK(budget.Overwalked() == 0.0f);
    CHECK(budget.MovementLeft() == Approx(5.0f));
}

TEST_CASE("with time, meters paid for and not walked stay walkable", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 5.0f, 0.0f);
    budget.Prepay(5.0f);
    CHECK(budget.CapacityLeftMs() == 3000);
    budget.Walk(3.0f);
    CHECK(budget.CapacityLeftMs() == 3000);
    CHECK(budget.WalkCostMs(2.0f) == 0);
    CHECK(budget.WalkCostMs(7.0f) == 1000);
    budget.Prepay(1.0f); // within what is paid for: nothing more
    CHECK(budget.CapacityLeftMs() == 3000);
    budget.Walk(4.5f); // 2 m paid for, 2.5 m at the pace
    CHECK(budget.CapacityLeftMs() == 2500);
}

TEST_CASE("a pending attack's walk is credited as it lands", "[turnbudget]")
{
    TurnBudget budget = TurnBudget::Time(TurnTime(4000), 5.0f, 1.0f);
    budget.Credit(4.0f);
    budget.Walk(4.0f);
    CHECK(budget.CapacityLeftMs() == 4000);
    CHECK(budget.MovementLeft() == Approx(21.0f));
    TurnBudget built = TurnBudget::Actions(1, 9.0f);
    CHECK(built.WalkCostMs(5.0f) == 0);
    built.Prepay(5.0f); // as built, a walk is paid as it is walked
    CHECK(built.MovementLeft() == Approx(9.0f));
}
