// The turn bar's string, read back the way the movie's ActionScript will:
// split on the card separator, then each part on the field separator.
#include "World.h"

#include "core/Bar.h"

#include <catch2/catch_test_macros.hpp>

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
    REQUIRE(bar.head.size() == 8);
    CHECK(bar.head[0] == std::to_wstring(kBarVersion));
    CHECK(bar.head[1] == L"1");                   // active
    CHECK(bar.head[2] == L"1");                   // round
    CHECK(bar.head[3] == std::to_wstring(kHero)); // current
    CHECK(bar.head[4] == L"1");                   // the player's turn
    CHECK(bar.head[5] == L"1");                   // an action left
    CHECK(bar.head[6] == L"9.0");                 // meters left
    CHECK(bar.head[7] == L"9.0");                 // meters at most
    REQUIRE(bar.cards.size() >= 5);
    for (const auto &card : bar.cards)
    {
        REQUIRE(card.size() == 11);
    }
    CHECK(bar.cards[0][0] == std::to_wstring(kHero));
    CHECK(bar.cards[0][1] == L"0");   // the party
    CHECK(bar.cards[0][3] == L"100"); // full health
    CHECK(bar.cards[0][5] == L"1");   // current
    CHECK(bar.cards[0][6] == L"1");   // the player's
    CHECK(bar.cards[0][8] == L"Hero");
    CHECK(bar.cards[1][0] == std::to_wstring(kGenlock));
    CHECK(bar.cards[1][1] == L"2"); // an enemy
    CHECK(bar.cards[1][6] == L"0"); // the AI's
    CHECK(bar.cards[4][7] == L"2"); // the next round's
}

TEST_CASE("a name cannot break the bar's string apart", "[bar]")
{
    TurnView view;
    CardView card;
    card.id = 7;
    card.name = std::wstring{L"Bad"} + kBarCard + L"Name" + kBarField + L"\n!";
    card.tag = L"tag";
    card.health = 1.7f; // out of range: clamped
    view.upcoming.push_back(card);
    const Decoded bar = Decode(EncodeBar(view));
    REQUIRE(bar.cards.size() == 1);
    REQUIRE(bar.cards[0].size() == 11);
    CHECK(bar.cards[0][8] == L"BadName!");
    CHECK(bar.cards[0][3] == L"100");
}

TEST_CASE("outside a fight the bar's string says so and has no cards", "[bar]")
{
    const Decoded bar = Decode(EncodeBar(TurnView{}));
    REQUIRE(bar.head.size() == 8);
    CHECK(bar.head[1] == L"0");
    CHECK(bar.cards.empty());
}
