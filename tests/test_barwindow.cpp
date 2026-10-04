// Which of the turns to come the bar's row shows (core/BarWindow.h; issue
// 23): the user's rules for the arrows at its ends, over lists made by hand,
// over the stand-in's fights, and over a long random run of everything that
// can happen to a list.
#include "World.h"

#include "core/Bar.h"
#include "core/BarWindow.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// A fight's view with these turns to come, the first the current one.
TurnView ViewOf(const std::vector<ObjectId> &ids)
{
    TurnView view;
    view.active = true;
    view.phase = Phase::AwaitingOrder;
    for (const ObjectId id : ids)
    {
        CardView card;
        card.id = id;
        card.current = view.upcoming.empty();
        view.upcoming.push_back(card);
    }
    return view;
}

std::vector<ObjectId> Count(ObjectId from, ObjectId to)
{
    std::vector<ObjectId> ids;
    for (ObjectId id = from; id <= to; ++id)
    {
        ids.push_back(id);
    }
    return ids;
}

// Whom the row shows.
std::vector<ObjectId> RowOf(const TurnView &view, const BarSpan &row)
{
    std::vector<ObjectId> ids;
    for (std::size_t i = row.before; i < row.before + row.shown && i < view.upcoming.size(); ++i)
    {
        ids.push_back(view.upcoming[i].id);
    }
    return ids;
}

void CheckSpan(const BarSpan &row, std::size_t before, std::size_t shown, std::size_t after)
{
    CHECK(row.before == before);
    CHECK(row.shown == shown);
    CHECK(row.after == after);
}

// The standard four and ten walking corpses: fourteen, in the order the
// hero, the genlock, Alistair, the hurlock, then the corpses by their ids.
constexpr ObjectId kCorpse = 0x300;
std::vector<Creature> FourteenFight()
{
    auto fight = StandardFight();
    for (ObjectId id = kCorpse; id < kCorpse + 10; ++id)
    {
        fight.push_back(Make(id, Side::Enemy, false, L"Walking Corpse", 10.0f));
    }
    return fight;
}

} // namespace

TEST_CASE("with ten or fewer fighting the row is the whole list and has no arrow", "[barwindow]")
{
    // The user: "No arrows should be shown if there are 10 or fewer portraits."
    BarWindow window;
    CheckSpan(window.Follow(ViewOf(Count(1, 4))), 0, 4, 0);
    CheckSpan(window.Follow(ViewOf(Count(1, 10))), 0, 10, 0);
    // And an arrow's click, should one come, moves nothing.
    window.Scroll(1);
    CheckSpan(window.Span(), 0, 10, 0);
    window.Scroll(-1);
    CheckSpan(window.Follow(ViewOf(Count(1, 10))), 0, 10, 0);
}

TEST_CASE("with more than ten the row is ten from the beginning, and only the right arrow shows", "[barwindow]")
{
    BarWindow window;
    CheckSpan(window.Follow(ViewOf(Count(1, 11))), 0, 10, 1);
    BarWindow larger;
    const TurnView view = ViewOf(Count(1, 14));
    const BarSpan row = larger.Follow(view);
    CheckSpan(row, 0, 10, 4);
    CHECK(RowOf(view, row) == Count(1, 10));
}

TEST_CASE("the right arrow moves the row a card down the list, the left one back", "[barwindow]")
{
    // "Clicking on the right arrow moves down the list and shows portraits
    // beyond 10. Left arrow moves back." A card a click (kStep).
    BarWindow window;
    const TurnView view = ViewOf(Count(1, 14));
    (void)window.Follow(view);
    window.Scroll(1);
    CheckSpan(window.Span(), 1, 10, 3);
    CHECK(RowOf(view, window.Span()) == Count(2, 11));
    window.Scroll(1);
    CheckSpan(window.Follow(view), 2, 10, 2);
    window.Scroll(-1);
    CheckSpan(window.Follow(view), 1, 10, 3);
    // Two clicks between two frames are two cards.
    window.Scroll(2);
    CheckSpan(window.Follow(view), 3, 10, 1);
}

TEST_CASE("the row stops at either end of the list, and the arrow there is hidden", "[barwindow]")
{
    // "If unable to move last or right, the corresponding arrow is hidden."
    BarWindow window;
    const TurnView view = ViewOf(Count(1, 14));
    (void)window.Follow(view);
    window.Scroll(-1);
    CheckSpan(window.Span(), 0, 10, 4);
    window.Scroll(9);
    CheckSpan(window.Span(), 4, 10, 0);
    CHECK(RowOf(view, window.Span()) == Count(5, 14));
    window.Scroll(1);
    CheckSpan(window.Span(), 4, 10, 0);
    // It did not go on past the end unseen: one click back is one card.
    window.Scroll(-1);
    CheckSpan(window.Span(), 3, 10, 1);
    window.Scroll(-9);
    CheckSpan(window.Follow(view), 0, 10, 4);
}

TEST_CASE("as turns pass the row keeps its places, not its fighters", "[barwindow]")
{
    // "As gameplay continues, don't want to constantly reset the position of
    // the portraits back to the beginning. If we're currently showing 3 to
    // 12 (starting at index 1), continue showing those positions, starting
    // at 3."
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(2);
    CheckSpan(window.Span(), 2, 10, 2);
    // 1's turn is over: it stands last, and everyone else a place earlier.
    std::vector<ObjectId> turned = Count(2, 14);
    turned.push_back(1);
    const TurnView next = ViewOf(turned);
    const BarSpan row = window.Follow(next);
    CheckSpan(row, 2, 10, 2);
    CHECK(RowOf(next, row) == Count(4, 13));
    // And again.
    turned = Count(3, 14);
    turned.push_back(1);
    turned.push_back(2);
    CheckSpan(window.Follow(ViewOf(turned)), 2, 10, 2);
}

TEST_CASE("when one before the row falls the row begins a place earlier, on the same fighters", "[barwindow]")
{
    // "If an enemy in position 1 or 2 has fallen, then the index is at 2. If
    // both enemies before has fallen, then the index is now 1."
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(2);
    const std::vector<ObjectId> shown = Count(3, 12);
    std::vector<ObjectId> ids = Count(1, 14);
    std::erase(ids, ObjectId{2});
    TurnView view = ViewOf(ids);
    BarSpan row = window.Follow(view);
    CheckSpan(row, 1, 10, 2);
    CHECK(RowOf(view, row) == shown);
    std::erase(ids, ObjectId{1});
    view = ViewOf(ids);
    row = window.Follow(view);
    CheckSpan(row, 0, 10, 2);
    CHECK(RowOf(view, row) == shown);
}

TEST_CASE("both before the row falling between two views move it two places", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(3);
    const TurnView view = ViewOf(Count(3, 14)); // 1 and 2 gone, 3 still before the row
    const BarSpan row = window.Follow(view);
    CheckSpan(row, 1, 10, 1);
    CHECK(RowOf(view, row) == Count(4, 13));
}

TEST_CASE("the current one falling as its turn passes moves the row down, not along", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(2);
    // 1 fell on its own turn and the turn is 2's: the list lost its first,
    // and nobody went round to its end.
    const TurnView view = ViewOf(Count(2, 14));
    const BarSpan row = window.Follow(view);
    CheckSpan(row, 1, 10, 2);
    CHECK(RowOf(view, row) == Count(3, 12));
}

TEST_CASE("one in the row or after it falling leaves the row where it is", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(2);
    std::vector<ObjectId> ids = Count(1, 14);
    std::erase(ids, ObjectId{5}); // in the row
    CheckSpan(window.Follow(ViewOf(ids)), 2, 10, 1);
    std::erase(ids, ObjectId{14}); // after it
    CheckSpan(window.Follow(ViewOf(ids)), 2, 10, 0);
}

TEST_CASE("the row never runs past the list's end, and is the beginning once ten are left", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(4);
    CheckSpan(window.Span(), 4, 10, 0);
    // One of the row gone: thirteen, and the row a place earlier to stay full.
    std::vector<ObjectId> ids = Count(1, 14);
    std::erase(ids, ObjectId{9});
    TurnView view = ViewOf(ids);
    BarSpan row = window.Follow(view);
    CheckSpan(row, 3, 10, 0);
    CHECK(RowOf(view, row).front() == 4);
    // Down to ten: the whole list, no arrow.
    std::erase(ids, ObjectId{10});
    std::erase(ids, ObjectId{11});
    std::erase(ids, ObjectId{12});
    CheckSpan(window.Follow(ViewOf(ids)), 0, 10, 0);
    // And where it stood is forgotten: two more joining find it at the
    // beginning, not two along.
    ids.push_back(20);
    ids.push_back(21);
    CheckSpan(window.Follow(ViewOf(ids)), 0, 10, 2);
}

TEST_CASE("one joining the fight leaves the row at its place", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(2);
    std::vector<ObjectId> ids = Count(1, 14);
    ids.insert(ids.begin() + 1, 30); // its place by initiative is before the row
    CheckSpan(window.Follow(ViewOf(ids)), 2, 10, 3);
    ids.push_back(31);
    CheckSpan(window.Follow(ViewOf(ids)), 2, 10, 4);
}

TEST_CASE("outside a fight the row is at the beginning, and the next fight's begins there", "[barwindow]")
{
    BarWindow window;
    (void)window.Follow(ViewOf(Count(1, 14)));
    window.Scroll(3);
    CheckSpan(window.Span(), 3, 10, 1);
    // No fight, whatever the view still holds.
    TurnView over = ViewOf(Count(1, 14));
    over.active = false;
    over.phase = Phase::Idle;
    CheckSpan(window.Follow(over), 0, 10, 4);
    CheckSpan(window.Follow(ViewOf(Count(1, 14))), 0, 10, 4);
    window.Scroll(3);
    CheckSpan(window.Follow(TurnView{}), 0, 0, 0);
    // A click with no fight on finds nothing to move.
    window.Scroll(1);
    CheckSpan(window.Span(), 0, 0, 0);
    CheckSpan(window.Follow(ViewOf(Count(1, 14))), 0, 10, 4);
}

TEST_CASE("a fight of fourteen: the row is moved, kept as turns pass, and brought down for the fallen",
          "[barwindow][machine]")
{
    World w(Fixed(), FourteenFight());
    w.StartFight();
    BarWindow window;
    TurnView view = w.machine.View();
    REQUIRE(view.upcoming.size() == 14);
    CheckSpan(window.Follow(view), 0, 10, 4);
    window.Scroll(2);
    BarSpan row = window.Follow(view);
    CheckSpan(row, 2, 10, 2);
    CHECK(RowOf(view, row).front() == kCompanion);
    CHECK(RowOf(view, row).back() == kCorpse + 7);

    // The genlock, second in the order and before the row, falls: the row
    // is still Alistair's to the eighth corpse's.
    w.Get(kGenlock).down = true;
    w.RunFor(600); // a status read
    view = w.machine.View();
    REQUIRE(view.upcoming.size() == 13);
    row = window.Follow(view);
    CheckSpan(row, 1, 10, 2);
    CHECK(RowOf(view, row).front() == kCompanion);
    CHECK(RowOf(view, row).back() == kCorpse + 7);

    // Alistair, now in the row, falls: a party member stays on the bar,
    // greyed, and nothing moves.
    w.Get(kCompanion).down = true;
    w.RunFor(600);
    view = w.machine.View();
    REQUIRE(view.upcoming.size() == 13);
    CheckSpan(window.Follow(view), 1, 10, 2);

    // The hero's turn over: the list turns, and the row shows the same
    // places, a fighter further on.
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    view = w.machine.View();
    REQUIRE(view.upcoming.front().id == kHurlock);
    row = window.Follow(view);
    CheckSpan(row, 1, 10, 2);
    CHECK(RowOf(view, row).front() == kCorpse);

    // A conversation in the middle of the fight: the row stays.
    w.mode = Mode::Dialog;
    w.Frame();
    CheckSpan(window.Follow(w.machine.View()), 1, 10, 2);
    w.mode = Mode::Combat;
    w.RunFor(500);
    CheckSpan(window.Follow(w.machine.View()), 1, 10, 2);
}

TEST_CASE("a fight's end sends the row back to the beginning", "[barwindow][machine]")
{
    World w(Fixed(), FourteenFight());
    w.StartFight();
    BarWindow window;
    (void)window.Follow(w.machine.View());
    window.Scroll(4);
    CheckSpan(window.Follow(w.machine.View()), 4, 10, 0);
    for (Creature &c : w.creatures)
    {
        c.down = c.side == Side::Enemy;
    }
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
    CheckSpan(window.Follow(w.machine.View()), 0, 0, 0);
}

TEST_CASE("the bar's string says where the row stands, and carries every turn to come", "[barwindow][bar]")
{
    World w(Fixed(), FourteenFight());
    w.StartFight();
    const TurnView view = w.machine.View();
    const auto fields = [](const std::wstring &text) {
        std::vector<std::wstring> head(1);
        std::size_t cards = 0;
        for (const wchar_t c : text)
        {
            if (c == kBarCard)
            {
                ++cards;
            }
            else if (cards == 0 && c == kBarField)
            {
                head.emplace_back();
            }
            else if (cards == 0)
            {
                head.back() += c;
            }
        }
        return std::pair{head, cards};
    };
    // As it begins: ten of fourteen, four after.
    auto [head, cards] = fields(EncodeBar(view));
    REQUIRE(head.size() == 19);
    CHECK(head[16] == L"0");
    CHECK(head[17] == L"10");
    CHECK(head[18] == L"4");
    CHECK(cards == 14);
    // Moved a card along: one before, three after.
    BarWindow window;
    (void)window.Follow(view);
    window.Scroll(1);
    std::tie(head, cards) = fields(EncodeBar(view, PortraitBook{}, window.Follow(view)));
    REQUIRE(head.size() == 19);
    CHECK(head[16] == L"1");
    CHECK(head[17] == L"10");
    CHECK(head[18] == L"3");
    CHECK(cards == 14);
}

TEST_CASE("portraits are got for the row's cards alone, and for the rest as the row comes to them",
          "[barwindow][portraits]")
{
    World w(Fixed(), FourteenFight());
    w.StartFight();
    const TurnView view = w.machine.View();
    BarWindow window;
    PortraitBook book;
    const auto fill = [&](const BarSpan &row) {
        std::vector<ObjectId> asked;
        while (const auto id = book.Next(view, row))
        {
            asked.push_back(*id);
            book.Got(*id, L"img://portrait_" + std::to_wstring(*id), true);
            REQUIRE(asked.size() <= view.upcoming.size());
        }
        return asked;
    };
    // The first ten, in the order of their turns; the four out of sight are
    // not rendered.
    const std::vector<ObjectId> first = fill(window.Follow(view));
    REQUIRE(first.size() == 10);
    CHECK(first.front() == kHero);
    CHECK(first.back() == kCorpse + 5);
    CHECK(book.Tried() == 10);
    // Two along: the two that came into the row, and nobody twice.
    window.Scroll(2);
    CHECK(fill(window.Follow(view)) == std::vector<ObjectId>{kCorpse + 6, kCorpse + 7});
    window.Scroll(-2);
    CHECK(fill(window.Follow(view)).empty());
    CHECK(book.Tried() == 12);
}

TEST_CASE("whatever happens to the order, the row holds to the user's rules", "[barwindow]")
{
    // Everything that can happen to the list, at random and for long, each
    // step checked against what the user asked for: a turn passing, one
    // falling (before the row, in it, after it), one joining, a click on
    // either arrow, the fight ending and another beginning.
    std::mt19937 rng{20261003};
    const auto below = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    BarWindow window;
    ObjectId nextId = 1;
    std::vector<ObjectId> ids;
    const auto begin = [&] {
        ids.clear();
        const std::size_t count = 6 + below(18);
        for (std::size_t i = 0; i < count; ++i)
        {
            ids.push_back(nextId++);
        }
    };
    begin();
    BarSpan row = window.Follow(ViewOf(ids));
    for (int step = 0; step < 4000; ++step)
    {
        const std::size_t was = row.before;
        const std::vector<ObjectId> shown = RowOf(ViewOf(ids), row);
        const std::size_t what = below(100);
        std::size_t want = was;
        bool same = false; // the row must show the fighters it showed
        if (what < 35 && ids.size() > 1)
        {
            std::rotate(ids.begin(), ids.begin() + 1, ids.end()); // a turn passes
        }
        else if (what < 55 && ids.size() > 2)
        {
            const std::size_t at = below(ids.size());
            ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(at));
            if (at < was)
            {
                want = was - 1;
                same = true;
            }
        }
        else if (what < 62 && ids.size() < 30)
        {
            ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(below(ids.size() + 1)), nextId++);
        }
        else if (what < 97)
        {
            const int clicks = what < 82 ? 1 : -1;
            window.Scroll(clicks);
            want = clicks < 0 ? was - std::min<std::size_t>(was, 1) : was + 1;
        }
        else
        {
            CheckSpan(window.Follow(TurnView{}), 0, 0, 0);
            begin();
            want = 0;
        }
        const TurnView view = ViewOf(ids);
        row = window.Follow(view);
        const std::size_t last = ids.size() > 10 ? ids.size() - 10 : 0;
        INFO("step " << step << ", " << ids.size() << " fighting, the row began at " << was);
        REQUIRE(row.before == std::min(want, last));
        REQUIRE(row.shown == std::min<std::size_t>(10, ids.size()));
        REQUIRE(row.before + row.shown + row.after == ids.size());
        if (same)
        {
            REQUIRE(RowOf(view, row) == shown);
        }
    }
}
