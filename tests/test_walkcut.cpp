// A walk cut short, on its creature's card (issue 83). A creature whose turn
// ran out while it was walking showed nothing on its card, where one whose
// attack or ability ran past its turn has that action's icon at the card's
// shoulder until it lands. The user, 2026-10-09: "if out of time when moving
// should show moving icon top right of portrait, as we do attack etc." In
// every fight's log: `Velanna is out of movement (1.7 of 1.4 m)`, `Velanna's
// turn ends: out of movement; nothing rolls over`. Scripted cases in the
// stand-in for the game (World.h). What is promised of the machine's view
// (CardView::walkCut; the icon and the words are the game layer's and the
// movie's):
//
//   - a turn in which the meters stopped its creature's walk says so on its
//     card from that turn's end: a turn its AI plays, which ends there, and
//     one the player plays, however it then ends;
//   - a walk that got where it was sent says nothing;
//   - the card says it until its creature's next turn begins, and not after:
//     not through that turn, nor while a turn put off waits;
//   - nor past a turn that came and was lost to what holds its creature;
//   - nor of one that has fallen.
#include "World.h"

#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>

using namespace tb;
using namespace tb::test;

namespace
{

CardView Card(const World &w, ObjectId id)
{
    const TurnView view = w.machine.View();
    const auto found = std::ranges::find(view.upcoming, id, &CardView::id);
    REQUIRE(found != view.upcoming.end());
    return *found;
}

auto Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// An AI told to choose waits, which passes its turn.
void Waits(World &w, ObjectId id)
{
    (void)w.Order(id, CommandType::Wait, Source::Script);
}

// A creature at a move walks a meter a frame until the machine stops it or
// its turn is over: further than a whole bar walks (4 s at 5 m a second and
// a meter free, 21 m).
void WalkUntilStopped(World &w, ObjectId id)
{
    w.Get(id).moving = true;
    for (int i = 0; i < 30 && w.Get(id).moving && w.Current() == id; ++i)
    {
        w.Get(id).position.x += 1.0f;
        w.Frame();
    }
    REQUIRE_FALSE(w.Get(id).moving);
}

// The player's turn in hand is ended, and the turns its AI plays after it
// pass, to the next turn that waits for an order.
void EndTurn(World &w)
{
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(300);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
}

// The fight begun, the hero's turn ended, and the genlock, told to choose,
// sent on a walk longer than its bar: stopped, its turn over, and Alistair's
// waiting for an order.
void TheGenlocksWalkIsCut(World &w)
{
    bool walks = true;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && walks)
        {
            (void)w.Order(id, CommandType::MoveToLocation, Source::Script);
            return;
        }
        Waits(w, id);
    };
    w.StartFight();
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    WalkUntilStopped(w, kGenlock);
    walks = false;
    w.RunFor(300);
    REQUIRE(Lines(w, "Genlock's turn ends: out of movement") == 1);
    REQUIRE(w.Current() == kCompanion);
    w.onPoke = [&](ObjectId id) { Waits(w, id); };
}

} // namespace

TEST_CASE("a creature its AI plays, stopped by the meters, has its walk cut short on its card until its next turn "
          "begins",
          "[walkcut][bar]")
{
    World w;
    TheGenlocksWalkIsCut(w);
    CHECK(Card(w, kGenlock).walkCut);
    // Nobody else's.
    CHECK_FALSE(Card(w, kHero).walkCut);
    CHECK_FALSE(Card(w, kCompanion).walkCut);
    CHECK_FALSE(Card(w, kHurlock).walkCut);
    // Through the rest of the round and into the next, while its turn is
    // still to come.
    EndTurn(w);
    REQUIRE(w.machine.Order().Round() == 2);
    REQUIRE(w.Current() == kHero);
    CHECK(Card(w, kGenlock).walkCut);
    // Its next turn begins, and it is sent on another walk: the card says
    // nothing of the last one while this turn lasts.
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::MoveToLocation, Source::Script); };
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.Get(kGenlock).busy);
    CHECK_FALSE(Card(w, kGenlock).walkCut);
}

TEST_CASE("a turn the player plays that the meters stopped a walk in says so too; a walk that got where it was sent "
          "says nothing, and a turn put off has begun",
          "[walkcut][bar][delay]")
{
    World w;
    w.onPoke = [&](ObjectId id) { Waits(w, id); };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    WalkUntilStopped(w, kHero);
    REQUIRE(w.Said(L"Out of movement"));
    // Her bar spent on the walk, the turn ends by itself; the genlock waits,
    // and Alistair's turn is the next to wait for an order.
    w.RunFor(500);
    REQUIRE(Lines(w, "Hero's turn ends: nothing left to spend") == 1);
    REQUIRE(w.Current() == kCompanion);
    CHECK(Card(w, kHero).walkCut);
    // Alistair walks ten meters of his twenty-one and gets there.
    REQUIRE(w.Order(kCompanion, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Walk(kCompanion, 10.0f);
    w.Finish(kCompanion);
    w.RunFor(500);
    EndTurn(w);
    REQUIRE(w.machine.Order().Round() == 2);
    REQUIRE(w.Current() == kHero);
    CHECK_FALSE(Card(w, kCompanion).walkCut);
    // The hero's next turn has begun: nothing of the last one's walk. Nor as
    // she puts the turn off and her card waits for it.
    CHECK_FALSE(Card(w, kHero).walkCut);
    REQUIRE(w.machine.OnDelay());
    w.RunFor(300);
    REQUIRE(w.Current() == kCompanion);
    REQUIRE(Card(w, kHero).waits);
    CHECK_FALSE(Card(w, kHero).walkCut);
}

TEST_CASE("a walk cut short is off its creature's card as a turn of its comes and is lost to what holds it",
          "[walkcut][bar]")
{
    World w;
    TheGenlocksWalkIsCut(w);
    // Stunned for longer than a turn before its next comes.
    w.Get(kGenlock).condition = Condition::Disabled;
    w.Get(kGenlock).conditionSeconds = 10.0f;
    EndTurn(w);
    REQUIRE(w.Current() == kHero);
    CHECK(Card(w, kGenlock).walkCut);
    EndTurn(w);
    REQUIRE(Lines(w, "Genlock's turn is lost to its condition") == 1);
    REQUIRE(w.Current() == kCompanion);
    CHECK_FALSE(Card(w, kGenlock).walkCut);
}

TEST_CASE("a fallen party member's card, greyed, says nothing of a walk cut short", "[walkcut][bar]")
{
    World w;
    w.onPoke = [&](ObjectId id) { Waits(w, id); };
    w.StartFight();
    REQUIRE(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    WalkUntilStopped(w, kHero);
    w.RunFor(500);
    REQUIRE(w.Current() == kCompanion);
    REQUIRE(Card(w, kHero).walkCut);
    w.Get(kHero).down = true;
    w.RunFor(600); // a look at everyone finds her fallen
    REQUIRE(Card(w, kHero).down);
    CHECK_FALSE(Card(w, kHero).walkCut);
}
