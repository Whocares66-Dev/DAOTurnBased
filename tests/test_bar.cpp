// The turn bar's string, read back the way the movie's ActionScript will:
// split on the card separator, then each part on the field separator.
#include "World.h"

#include "core/Bar.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

std::vector<std::wstring> Split(const std::wstring &text, wchar_t on)
{
    std::vector<std::wstring> parts;
    std::wstring part;
    for (const wchar_t c : text)
    {
        if (c == on)
        {
            parts.push_back(part);
            part.clear();
        }
        else
        {
            part += c;
        }
    }
    parts.push_back(part);
    return parts;
}

struct Decoded
{
    std::vector<std::wstring> head;
    std::vector<std::vector<std::wstring>> cards;
};

Decoded Decode(const std::wstring &text)
{
    Decoded out;
    auto records = Split(text, kBarCard);
    out.head = Split(records.front(), kBarField);
    for (std::size_t i = 1; i < records.size(); ++i)
    {
        out.cards.push_back(Split(records[i], kBarField));
    }
    return out;
}

} // namespace

TEST_CASE("the bar's string says whose turn it is and who comes after", "[bar]")
{
    World w;
    w.StartFight();
    const Decoded bar = Decode(EncodeBar(w.machine.View()));
    REQUIRE(bar.head.size() == 18);
    CHECK(bar.head[0] == std::to_wstring(kBarVersion));
    CHECK(bar.head[1] == L"1");                   // active
    CHECK(bar.head[2] == L"1");                   // round
    CHECK(bar.head[3] == std::to_wstring(kHero)); // current
    CHECK(bar.head[4] == L"1");                   // the player's turn
    CHECK(bar.head[5] == L"21.0");                // meters left
    CHECK(bar.head[6] == L"21.0");                // meters at most
    CHECK(bar.head[14] == L"0");                  // the row: nobody before it,
    CHECK(bar.head[15] == L"4");                  // all four in it,
    CHECK(bar.head[16] == L"0");                  // nobody after
    CHECK(bar.head[17].empty());                  // nothing for the caption to say
    REQUIRE(bar.cards.size() == 4);               // each combatant once
    for (const auto &card : bar.cards)
    {
        REQUIRE(card.size() == 19);
    }
    CHECK(bar.cards[0][0] == std::to_wstring(kHero));
    CHECK(bar.cards[0][1] == L"0");   // the party
    CHECK(bar.cards[0][3] == L"100"); // full health
    CHECK(bar.cards[0][5] == L"1");   // current
    CHECK(bar.cards[0][6] == L"1");   // the player's
    CHECK(bar.cards[0][8] == L"Hero");
    CHECK(bar.cards[0][11] == L"100"); // full stamina
    CHECK(bar.cards[0][12] == L"0");   // stamina, not mana
    CHECK(bar.cards[1][0] == std::to_wstring(kGenlock));
    CHECK(bar.cards[1][1] == L"2"); // an enemy
    CHECK(bar.cards[1][6] == L"0"); // the AI's
    // The hero's turn over: the genlock first, the hero last, next round's.
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    const Decoded after = Decode(EncodeBar(w.machine.View()));
    REQUIRE(after.cards.size() == 4);
    CHECK(after.cards.front()[0] == std::to_wstring(kGenlock));
    CHECK(after.cards.back()[0] == std::to_wstring(kHero));
    CHECK(after.cards.back()[7] == L"2");
}

TEST_CASE("a name cannot break the bar's string apart", "[bar]")
{
    TurnView view;
    CardView card;
    card.id = 7;
    card.name = std::wstring{L"Bad"} + kBarCard + L"Name" + kBarField + L"\n!";
    card.tag = L"tag";
    card.health = 1.7f;  // out of range: clamped
    card.energy = -0.5f; // the same
    card.mana = true;
    view.upcoming.push_back(card);
    const Decoded bar = Decode(EncodeBar(view));
    REQUIRE(bar.cards.size() == 1);
    REQUIRE(bar.cards[0].size() == 19);
    CHECK(bar.cards[0][8] == L"BadName!");
    CHECK(bar.cards[0][3] == L"100");
    CHECK(bar.cards[0][11] == L"0");
    CHECK(bar.cards[0][12] == L"1");
}

TEST_CASE("outside a fight the bar's string says so and has no cards", "[bar]")
{
    const Decoded bar = Decode(EncodeBar(TurnView{}));
    REQUIRE(bar.head.size() == 18);
    CHECK(bar.head[1] == L"0");
    CHECK(bar.cards.empty());
}

TEST_CASE("a creature's mana or stamina reaches its card as the statuses read it", "[bar]")
{
    // The user: the turn bar's portraits showed health, the party bar's
    // stamina too (2026-09-30).
    World w;
    w.Get(kGenlock).energy = 0.4f;
    w.Get(kGenlock).mana = true; // a genlock emissary, say
    w.StartFight();
    w.RunFor(600); // a status read
    const Decoded bar = Decode(EncodeBar(w.machine.View()));
    const auto genlock =
        std::ranges::find_if(bar.cards, [](const auto &card) { return card[0] == std::to_wstring(kGenlock); });
    REQUIRE(genlock != bar.cards.end());
    CHECK((*genlock)[11] == L"40");
    CHECK((*genlock)[12] == L"1");
}

TEST_CASE("the bar's string says, last on its card, that its creature's walk was cut short", "[bar][walkcut]")
{
    // Issue 83; what the machine's view says of it is test_walkcut.cpp's.
    World w;
    w.StartFight();
    // The hero walks her whole bar and more: stopped, and her turn over.
    REQUIRE(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 30 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    w.RunFor(500);
    REQUIRE(w.Current() != kHero);
    const Decoded bar = Decode(EncodeBar(w.machine.View()));
    REQUIRE(bar.cards.size() == 4);
    for (const auto &card : bar.cards)
    {
        REQUIRE(card.size() == 19);
        CHECK(card[18] == (card[0] == std::to_wstring(kHero) ? L"1" : L"0"));
    }
}

TEST_CASE("with a turn a bar of time, the bar's string has the bar and what is left of it", "[bar]")
{
    World w;
    w.StartFight();
    const Decoded bar = Decode(EncodeBar(w.machine.View()));
    REQUIRE(bar.head.size() == 18);
    CHECK(bar.head[7] == L"4.0"); // what is left of the bar
    CHECK(bar.head[8] == L"4.0"); // the bar
    CHECK(bar.head[9] == L"4.0"); // a turn's own seconds
}
