// Whose portrait the game side renders next (core/Portraits.h), over the
// stand-in's fights, and the bar's string carrying them.
#include "World.h"

#include "core/Bar.h"
#include "core/Portraits.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// What the plugin does each frame: one combatant asked for, and answered
// with a name made from its id (or none, for those in `without`).
std::vector<ObjectId> FillBook(PortraitBook &book, const TurnView &view, const std::vector<ObjectId> &without = {})
{
    std::vector<ObjectId> asked;
    book.Follow(view, 0);
    while (const auto id = book.Next(view))
    {
        asked.push_back(*id);
        const bool none = std::ranges::find(without, *id) != without.end();
        // The party's are the game's own; everyone else's rendered.
        const bool party =
            std::ranges::any_of(view.upcoming, [&](const CardView &c) { return c.id == *id && c.side == Side::Party; });
        book.Got(*id, none ? std::wstring{} : L"img://portrait_" + std::to_wstring(*id), !party);
        REQUIRE(asked.size() <= view.upcoming.size());
    }
    return asked;
}

std::vector<std::wstring> Split(const std::wstring &text, wchar_t on)
{
    std::vector<std::wstring> parts(1);
    for (const wchar_t c : text)
    {
        if (c == on)
        {
            parts.emplace_back();
        }
        else
        {
            parts.back() += c;
        }
    }
    return parts;
}

} // namespace

TEST_CASE("portraits are asked for in the order of the turns, each combatant once", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    const auto asked = FillBook(book, w.machine.View());
    // The hero's turn first, then the genlock's, Alistair's, the hurlock's;
    // the next round's cards are the same four and are not asked again.
    CHECK(asked == std::vector<ObjectId>{kHero, kGenlock, kCompanion, kHurlock});
    CHECK(book.Tried() == 4);
    CHECK(book.Found() == 4);
    CHECK(std::wstring{book.Of(kGenlock)} == L"img://portrait_" + std::to_wstring(kGenlock));
}

TEST_CASE("a combatant the engine made no portrait for is not asked again", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View(), {kHurlock});
    CHECK(book.Tried() == 4);
    CHECK(book.Found() == 3);
    CHECK(book.Of(kHurlock).empty());
    w.RunFor(2000);
    CHECK_FALSE(book.Next(w.machine.View()).has_value());
}

TEST_CASE("a creature joining the fight is asked for when its card comes", "[portraits]")
{
    auto fight = StandardFight();
    Creature late = Make(0x202, Side::Enemy, false, L"Shriek", 12.0f);
    late.fighting = false;
    fight.push_back(late);
    World w(Fixed(), fight);
    w.StartFight();
    PortraitBook book;
    CHECK(FillBook(book, w.machine.View()).size() == 4);
    w.Get(0x202).fighting = true;
    CHECK(w.machine.OnPauseKey()); // the next turn's scan finds it
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.Order().Find(0x202) != nullptr);
    CHECK(FillBook(book, w.machine.View()) == std::vector<ObjectId>{0x202});
}

TEST_CASE("portraits are kept through a conversation and forgotten when the fight ends", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View());
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE_FALSE(w.machine.Active());
    // Nothing is rendered while the fight stands aside, and nothing lost.
    CHECK(FillBook(book, w.machine.View()).empty());
    CHECK(book.Tried() == 4);
    PortraitBook fresh;
    CHECK_FALSE(fresh.Next(w.machine.View()).has_value());
    w.mode = Mode::Combat;
    w.RunFor(500);
    REQUIRE(w.machine.Active());
    CHECK(FillBook(book, w.machine.View()).empty());

    // The enemies fall; at the next turn the fight is over.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
    book.Follow(w.machine.View(), 0);
    CHECK(book.Tried() == 0);
    CHECK(book.Of(kHero).empty());
}

TEST_CASE("the bar's string carries each card's portrait, empty for none yet", "[portraits][bar]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    book.Got(kGenlock, L"img://portrait_7", true);
    const auto records = Split(EncodeBar(w.machine.View(), book), kBarCard);
    REQUIRE(records.size() >= 3);
    const auto hero = Split(records[1], kBarField);
    const auto genlock = Split(records[2], kBarField);
    REQUIRE(hero.size() == 11);
    REQUIRE(genlock.size() == 11);
    CHECK(hero[10].empty());
    CHECK(genlock[0] == std::to_wstring(kGenlock));
    CHECK(genlock[10] == L"portrait_7.dds");
}

TEST_CASE("a portrait is sent by the name the game's image loader finds it by", "[portraits]")
{
    // As the game's movies name images (MovieUtils.GetImageUrl): the
    // extension added to a name without one.
    CHECK(TextureName(L"img://portrait_player0") == L"portrait_player0.dds");
    CHECK(TextureName(L"img://portrait_bhn200cr_howe_landra43") == L"portrait_bhn200cr_howe_landra43.dds");
    CHECK(TextureName(L"portrait_gen00fl_dog1") == L"portrait_gen00fl_dog1.dds");
    CHECK(TextureName(L"img://already.dds") == L"already.dds");
    CHECK(TextureName(L"").empty());
    CHECK(TextureName(L"img://").empty());
}

namespace
{
// A fight to its end: the enemies fall, the hero passes, the machine is idle.
void EndFight(World &w)
{
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
}
} // namespace

TEST_CASE("the portraits the bar rendered are released once the movie has let them go", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View());
    CHECK(book.TakeReleases(0).empty()); // nothing while the fight is on
    EndFight(w);
    book.Follow(w.machine.View(), 1000);
    CHECK(book.Tried() == 0);
    CHECK(book.AwaitingMovie());
    CHECK(book.TakeReleases(1500).empty()); // the movie may still show them
    book.MovieLetGo();
    CHECK_FALSE(book.AwaitingMovie());
    auto released = book.TakeReleases(1500);
    std::ranges::sort(released);
    // The enemies'; never the party's, which are the game's own.
    CHECK(released == std::vector<ObjectId>{kGenlock, kHurlock});
    CHECK(book.TakeReleases(9000).empty()); // each once
}

TEST_CASE("a movie that never says it let go is waited for a while, not forever", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View());
    EndFight(w);
    book.Follow(w.machine.View(), 1000);
    CHECK(book.TakeReleases(1000 + PortraitBook::kReleaseWaitMs - 1).empty());
    CHECK(book.TakeReleases(1000 + PortraitBook::kReleaseWaitMs).size() == 2);
    CHECK_FALSE(book.AwaitingMovie());
}

TEST_CASE("a portrait a new fight shows again is not released under it", "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View());
    EndFight(w);
    book.Follow(w.machine.View(), 1000);
    // Before the release, the genlock is in a new fight's view: its
    // renderer is still there, so it comes back as the game's.
    TurnView next;
    next.active = true;
    next.phase = Phase::AwaitingOrder;
    CardView genlock;
    genlock.id = kGenlock;
    next.upcoming.push_back(genlock);
    book.Follow(next, 1100);
    REQUIRE(book.Next(next) == kGenlock);
    book.Got(kGenlock, L"img://portrait_genlock", false);
    book.MovieLetGo();
    CHECK(book.TakeReleases(1200) == std::vector<ObjectId>{kHurlock});
    // It is released after the new fight instead.
    book.Follow(TurnView{}, 5000);
    book.MovieLetGo();
    CHECK(book.TakeReleases(5000) == std::vector<ObjectId>{kGenlock});
}

TEST_CASE("nothing waits for release when nothing was rendered", "[portraits]")
{
    PortraitBook book;
    book.Got(kHero, L"img://portrait_player0", false);
    book.Follow(TurnView{}, 0);
    CHECK_FALSE(book.AwaitingMovie());
    CHECK(book.TakeReleases(100000).empty());
}
