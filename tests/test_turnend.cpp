// Why a turn ended, and whether what it left rolls over (core/TurnEnd.h,
// issue 29): each reason's class, said here a second time by someone who
// knows the answer. What the machine does with it is test_time.cpp's.
#include "core/TurnEnd.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

TEST_CASE("a turn its actor ended rolls over what it left; one taken from it does not", "[turnend]")
{
    // The actor's own choice, with time it did not spend: the player's End,
    // an AI that is done, an AI that waits.
    CHECK(tb::RollsOver("ended by the player"));
    CHECK(tb::RollsOver("done"));
    CHECK(tb::RollsOver("waits"));
    // The user, 2026-10-04: "When turn is skipped, due to unawareness,
    // incapacity, etc, there should be no time rollover, because the actor
    // did not explicitly save any time."
    // Skipped: a hostile that has noticed nobody to fight.
    CHECK_FALSE(tb::RollsOver("unaware"));
    // Its AI could not or did not act.
    CHECK_FALSE(tb::RollsOver("chose nothing"));
    CHECK_FALSE(tb::RollsOver("took too long"));
    CHECK_FALSE(tb::RollsOver("nothing it can get to"));
    CHECK_FALSE(tb::RollsOver("out of movement"));
    // It fell or left in its turn, or its turn came while it was down.
    CHECK_FALSE(tb::RollsOver("fell"));
    CHECK_FALSE(tb::RollsOver("gone"));
    CHECK_FALSE(tb::RollsOver("down"));
    // Lost to what holds it.
    CHECK_FALSE(tb::RollsOver("stunned"));
    CHECK_FALSE(tb::RollsOver("held by its condition"));
    CHECK_FALSE(tb::RollsOver("held by another's action"));
    CHECK_FALSE(tb::RollsOver("held by an effect"));
}

TEST_CASE("the ends that leave nothing in the bar are their actors' own", "[turnend]")
{
    // The actor spent it: nothing is left to roll over either way, and the
    // class says whose doing the end was.
    for (const std::string_view why :
         {"nothing left to spend", "its action pending", "charging", "its spell conjuring", "its spell cast at once"})
    {
        CAPTURE(why);
        CHECK(tb::EndedByItsActor(why) == true);
    }
}

TEST_CASE("a reason the table does not have rolls nothing over, and says it is not known", "[turnend]")
{
    CHECK_FALSE(tb::EndedByItsActor("no such reason").has_value());
    CHECK_FALSE(tb::RollsOver("no such reason"));
    CHECK_FALSE(tb::RollsOver(""));
    // By the whole of its words, not a part of them.
    CHECK_FALSE(tb::RollsOver("done for"));
    CHECK_FALSE(tb::RollsOver("ended"));
}

TEST_CASE("each reason is in the table once", "[turnend]")
{
    std::set<std::string_view> seen;
    for (const tb::TurnEnd &end : tb::TurnEnds())
    {
        CAPTURE(end.why);
        CHECK_FALSE(end.why.empty());
        CHECK(seen.insert(end.why).second);
        CHECK(tb::EndedByItsActor(end.why) == end.byItsActor);
    }
    CHECK_FALSE(seen.empty());
}
