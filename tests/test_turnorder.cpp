#include "core/TurnOrder.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using tb::Combatant;
using tb::ObjectId;
using tb::Side;
using tb::TurnOrder;

namespace
{

Combatant Make(ObjectId id, Side side, int initiative, float dexterity = 10.0f)
{
    Combatant c;
    c.id = id;
    c.side = side;
    c.initiative = initiative;
    c.dexterity = dexterity;
    c.name = std::to_wstring(id);
    return c;
}

std::vector<ObjectId> Ids(const TurnOrder &order)
{
    std::vector<ObjectId> ids;
    for (const Combatant &c : order.All())
    {
        ids.push_back(c.id);
    }
    return ids;
}

std::vector<ObjectId> Upcoming(const TurnOrder &order, std::size_t count)
{
    std::vector<ObjectId> ids;
    for (const auto &slot : order.Upcoming(count))
    {
        ids.push_back(slot.who->id);
    }
    return ids;
}

} // namespace

TEST_CASE("the order is by initiative, then Dexterity, then side, then id", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Enemy, 10), Make(2, Side::Party, 15), Make(3, Side::Enemy, 12, 20.0f),
                 Make(4, Side::Party, 12, 20.0f), Make(5, Side::Ally, 12, 12.0f), Make(6, Side::Enemy, 12, 12.0f)});
    CHECK(Ids(order) == std::vector<ObjectId>{2, 4, 3, 5, 6, 1});
    CHECK(order.Round() == 1);
    CHECK(order.Current()->id == 2);
}

TEST_CASE("turns pass in order, and the round turns over at the end", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Party, 20), Make(2, Side::Enemy, 10), Make(3, Side::Enemy, 5)});
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 2);
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 3);
    CHECK(order.Advance());
    CHECK(order.Round() == 2);
    CHECK(order.Current()->id == 1);
}

TEST_CASE("the fallen lose their turns; fallen enemies are dropped at the round's end, the party kept", "[order]")
{
    TurnOrder order;
    order.Begin(
        {Make(1, Side::Party, 20), Make(2, Side::Party, 15), Make(3, Side::Enemy, 10), Make(4, Side::Enemy, 5)});
    order.Find(2)->down = true;
    order.Find(3)->down = true;
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 4);
    CHECK(order.Advance());
    CHECK(Ids(order) == std::vector<ObjectId>{1, 2, 4});
    CHECK(order.Current()->id == 1);
    CHECK(order.AnyStanding(Side::Enemy));
    order.Find(4)->down = true;
    CHECK_FALSE(order.AnyStanding(Side::Enemy));
}

TEST_CASE("a combatant whose place is still to come this round acts this round", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Party, 20), Make(2, Side::Enemy, 10)});
    order.Join(Make(3, Side::Enemy, 15));
    CHECK(order.Current()->id == 1);
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 3);
}

TEST_CASE("a combatant whose place has passed this round acts from the next", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Party, 20), Make(2, Side::Enemy, 10), Make(3, Side::Enemy, 5)});
    CHECK_FALSE(order.Advance()); // 2's turn
    order.Join(Make(4, Side::Enemy, 15));
    CHECK(order.Current()->id == 2); // still 2's turn
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 3);
    CHECK(order.Advance());
    CHECK(order.Current()->id == 1);
    CHECK_FALSE(order.Advance());
    CHECK(order.Current()->id == 4);
}

TEST_CASE("the upcoming turns are each combatant once, running on into the next round", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Party, 20), Make(2, Side::Party, 15), Make(3, Side::Enemy, 10)});
    CHECK(Upcoming(order, 12) == std::vector<ObjectId>{1, 2, 3});
    CHECK_FALSE(order.Advance());                                 // to 2, the same round
    CHECK(Upcoming(order, 12) == std::vector<ObjectId>{2, 3, 1}); // 1 again only next round
    const auto slots = order.Upcoming(12);
    CHECK(slots[0].current);
    CHECK(slots[0].round == 1);
    CHECK(slots[1].round == 1);
    CHECK(slots[2].round == 2);
    CHECK(Upcoming(order, 2) == std::vector<ObjectId>{2, 3}); // at most count

    order.Find(1)->down = true; // a party member down: still shown
    CHECK(Upcoming(order, 12) == std::vector<ObjectId>{2, 3, 1});
    order.Find(3)->down = true; // an enemy down: not
    CHECK(Upcoming(order, 12) == std::vector<ObjectId>{2, 1});
}

TEST_CASE("a party all fallen stays in the order, and advancing goes round without end", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Party, 20), Make(2, Side::Party, 10)});
    order.Find(1)->down = true;
    order.Find(2)->down = true;
    CHECK(order.Advance()); // two laps, nobody standing: a round passed
    CHECK(order.All().size() == 2);
    CHECK_FALSE(order.AnyStanding(Side::Party));
}

TEST_CASE("joining an empty order starts it; an empty order does nothing", "[order]")
{
    TurnOrder order;
    CHECK_FALSE(order.Advance());
    CHECK(order.Upcoming(5).empty());
    order.Join(Make(7, Side::Enemy, 12));
    CHECK(order.Round() == 1);
    CHECK(order.Current()->id == 7);
    CHECK(order.Upcoming(0).empty());
}

TEST_CASE("equal in all else, the lower id goes first", "[order]")
{
    TurnOrder order;
    order.Begin({Make(9, Side::Enemy, 10), Make(3, Side::Enemy, 10)});
    CHECK(Ids(order) == std::vector<ObjectId>{3, 9});
}

TEST_CASE("nobody standing leaves the order where it lands", "[order]")
{
    TurnOrder order;
    order.Begin({Make(1, Side::Enemy, 20), Make(2, Side::Enemy, 10)});
    order.Find(1)->down = true;
    order.Find(2)->down = true;
    CHECK(order.Advance());
    CHECK(order.Empty());
    CHECK(order.Current() == nullptr);
}
