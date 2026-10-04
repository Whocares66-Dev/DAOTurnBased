// Whose portrait the game side renders next (core/Portraits.h), over the
// stand-in's fights, and the bar's string carrying them.
#include "World.h"
#include <catch2/catch_approx.hpp>
#include <limits>

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

// The bar's row as a fight begins: the first ten turns to come (core/BarWindow.h;
// its own cases are test_barwindow.cpp's).
BarSpan Row(const TurnView &view)
{
    return SpanOf(view.upcoming.size(), 0);
}

// What the plugin does each frame: one combatant asked for, and answered
// with a name made from its id (or none, for those in `without`).
std::vector<ObjectId> FillBook(PortraitBook &book, const TurnView &view, const std::vector<ObjectId> &without = {})
{
    std::vector<ObjectId> asked;
    book.Follow(view, 0);
    while (const auto id = book.Next(view, Row(view)))
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
    CHECK_FALSE(book.Next(w.machine.View(), Row(w.machine.View())).has_value());
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
    CHECK(w.machine.OnEndTurn()); // the next turn's scan finds it
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
    CHECK_FALSE(fresh.Next(w.machine.View(), Row(w.machine.View())).has_value());
    w.mode = Mode::Combat;
    w.RunFor(500);
    REQUIRE(w.machine.Active());
    CHECK(FillBook(book, w.machine.View()).empty());

    // The enemies fall; at the next turn the fight is over.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnEndTurn());
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
    const auto records = Split(EncodeBar(w.machine.View(), book, Row(w.machine.View())), kBarCard);
    REQUIRE(records.size() >= 3);
    const auto hero = Split(records[1], kBarField);
    const auto genlock = Split(records[2], kBarField);
    REQUIRE(hero.size() == 18);
    REQUIRE(genlock.size() == 18);
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
    CHECK(w.machine.OnEndTurn());
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
    REQUIRE(book.Next(next, Row(next)) == kGenlock);
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

TEST_CASE("a bar turned off mid-fight lets its portraits go as a fight's end does", "[portraits]")
{
    // The review's TB-1: turned off, the plugin stopped serving the bar,
    // the screen stayed up and the rendered portraits were never released.
    // It now serves an empty view, the fight still on.
    World w;
    w.StartFight();
    PortraitBook book;
    FillBook(book, w.machine.View());
    REQUIRE(w.machine.Active());
    const TurnView off; // what the plugin serves with turn_bar = false
    book.Follow(off, 1000);
    CHECK(book.Tried() == 0);
    CHECK(book.AwaitingMovie()); // the screen stays up until the movie has let go
    CHECK(book.TakeReleases(1100).empty());
    book.Follow(off, 1200); // served every frame; nothing new
    book.MovieLetGo();
    CHECK_FALSE(book.AwaitingMovie());
    auto released = book.TakeReleases(1300);
    std::ranges::sort(released);
    CHECK(released == std::vector<ObjectId>{kGenlock, kHurlock});
    // Turned on again in the same fight: asked for anew, nothing released
    // under the cards.
    FillBook(book, w.machine.View());
    CHECK(book.Tried() > 0);
    CHECK(book.TakeReleases(9000).empty());
}

namespace
{
// The game's portraits table, the rows that matter (portraits.gda, read
// from the game's files 2026-10-01).
std::vector<PortraitRow> GameRows()
{
    return {
        {0, L"Default", L"default", L""},
        {0, L"Dog", L"gen00fl_dog", L""},
        {9, L"Bear", L"", L"portrait_09"},
        {59, L"Spider", L"", L"portrait_59"},
        {14, L"Swarm", L"", L"portrait_14"},
        {6, L"Golem", L"", L"portrait_06"},
        {49, L"Wolf", L"", L"portrait_49"},
        {68, L"Bear, Beareskan", L"", L"portrait_68"},
        {57, L"Spider, Corrupted", L"", L"portrait_57"},
        {8, L"Bear, Great", L"", L""},
        {74, L"Wolf, Blight", L"", L""},
    };
}
} // namespace

TEST_CASE("a creature's picture: the table's own, a render where there is a camera, its kin's, or none", "[portraits]")
{
    const std::vector<PortraitRow> rows = GameRows();
    const auto choose = [&](std::int32_t appearance, std::wstring_view label, bool person, std::wstring_view tag) {
        return ChoosePortrait(appearance, label, person, tag, rows);
    };
    // The table's own picture: a wolf (the user's "Wo"), a corrupted spider.
    CHECK(choose(49, L"Wolf", false, L"wolf").source == PortraitSource::ReadyMade);
    CHECK(choose(49, L"Wolf", false, L"wolf").made == L"portrait_49");
    CHECK(choose(57, L"Spider, Corrupted", false, L"").made == L"portrait_57");
    // A person is rendered, whatever its tag.
    CHECK(choose(15, L"Human", true, L"bhn200cr_howe").source == PortraitSource::Render);
    CHECK(choose(0, L"", true, L"").source == PortraitSource::Render);
    // A creature the table has a camera for, by appearance or by tag.
    CHECK(choose(8, L"Bear, Great", false, L"").source == PortraitSource::Render);
    CHECK(choose(74, L"Wolf, Blight", false, L"").source == PortraitSource::Render);
    CHECK(choose(35, L"Dog, Party Member", false, L"gen00fl_dog").source == PortraitSource::Render);
    // A giant spider has no row: its kin's, the plain spider's, not a render
    // of its abdomen (the user's picture, 2026-10-01).
    CHECK(choose(58, L"Spider, Giant", false, L"spider_giant").source == PortraitSource::ReadyMade);
    CHECK(choose(58, L"Spider, Giant", false, L"spider_giant").made == L"portrait_59");
    CHECK(choose(53, L"Witherfang", false, L"").source == PortraitSource::None); // no wolf by name
    // No kin in the table: initials, not a bad render.
    CHECK(choose(34, L"Dog, Mabari", false, L"bhn200cr_mabari").source == PortraitSource::None);
    CHECK(choose(27, L"Rage Demon", false, L"").source == PortraitSource::None);
    CHECK(choose(0, L"", false, L"").source == PortraitSource::None);
}

TEST_CASE("a family is the label up to its first comma, in small letters", "[portraits]")
{
    CHECK(PortraitFamily(L"Spider, Giant") == L"spider");
    CHECK(PortraitFamily(L"Spider") == L"spider");
    CHECK(PortraitFamily(L"Bear , Great") == L"bear");
    CHECK(PortraitFamily(L"Arcane Spirit") == L"arcane spirit");
    CHECK(PortraitFamily(L"").empty());
    // With only a family's lesser member ready-made, that one is its kin's.
    const std::vector<PortraitRow> rows = {{57, L"Spider, Corrupted", L"", L"portrait_57"}};
    CHECK(ChoosePortrait(58, L"Spider, Giant", false, L"", rows).made == L"portrait_57");
}

TEST_CASE("a creature bigger than a person is framed from further back, by its size", "[portraits]")
{
    using Catch::Approx;
    // The user, 2026-10-02: "portrait for ogre looks off" (its arm and chest).
    // An ogre's personal space 0.95, a person's distance 0.675 m.
    REQUIRE(tb::PortraitDistance(0.95f, 0.675f));
    CHECK(*tb::PortraitDistance(0.95f, 0.675f) == Approx(2.471f).margin(0.01f)); // its face (2.56 m seen)
    CHECK(*tb::PortraitDistance(0.5f, 0.675f) == Approx(0.943f).margin(0.01f));  // a shriek
    CHECK(*tb::PortraitDistance(0.41f, 0.675f) == Approx(0.700f).margin(0.01f)); // no jump past a person
    // A person's size or smaller: the table's camera as ever.
    CHECK_FALSE(tb::PortraitDistance(0.4f, 0.675f));
    CHECK_FALSE(tb::PortraitDistance(0.3f, 0.675f)); // a genlock
    CHECK_FALSE(tb::PortraitDistance(0.95f, 0.0f));
    CHECK_FALSE(tb::PortraitDistance(std::numeric_limits<float>::quiet_NaN(), 0.675f));
}

TEST_CASE("a big creature's portrait camera moves with its size: distance and offsets", "[portraits]")
{
    using Catch::Approx;
    // The user: "ogre portrait isn't quite centered". The table's person:
    // 0.675 m, x 0.007, z 0.038.
    const auto ogre = tb::FrameBySize(0.95f, 0.675f, 0.007f, 0.038f);
    REQUIRE(ogre);
    CHECK(ogre->distance == Approx(2.471f).margin(0.01f));
    CHECK(ogre->xAdjust == Approx(0.25f).margin(0.01f)); // as centred its face by sight
    CHECK(ogre->zAdjust == Approx(-0.15f).margin(0.01f));
    CHECK(tb::SizeOverPerson(0.95f) == Approx(3.66f).margin(0.01f));
    CHECK(tb::SizeOverPerson(0.4f) == 1.0f);
    CHECK(tb::SizeOverPerson(0.2f) == 1.0f);
    // A person: the table's camera as it is.
    CHECK_FALSE(tb::FrameBySize(0.4f, 0.675f, 0.007f, 0.038f));
    // Just over a person's size: hardly moved.
    const auto shriek = tb::FrameBySize(0.42f, 0.675f, 0.007f, 0.038f);
    REQUIRE(shriek);
    CHECK(shriek->xAdjust == Approx(0.007f).margin(0.01f));
    CHECK(shriek->zAdjust == Approx(0.038f).margin(0.01f));
}

TEST_CASE("the camera pulls back on a big creature's turn, by its size, never into the tactical view", "[portraits]")
{
    using Catch::Approx;
    // The user: "for ogre or dragon pull back more, for human sized enemies
    // same as player".
    CHECK_FALSE(tb::FollowZoom(1.0f, 0.4f)); // a person: the player's zoom
    CHECK_FALSE(tb::FollowZoom(1.0f, 0.3f)); // a genlock
    REQUIRE(tb::FollowZoom(1.0f, 0.95f));
    CHECK(*tb::FollowZoom(1.0f, 0.95f) == Approx(0.80f).margin(0.01f)); // an ogre
    CHECK(*tb::FollowZoom(1.0f, 0.70f) == Approx(0.90f).margin(0.01f)); // a giant spider
    CHECK(*tb::FollowZoom(1.0f, 1.4f) == Approx(0.78f));                // a dragon: no further than the least
    // The player already far back: never closer than the player's own, and
    // never into the tactical view.
    CHECK(*tb::FollowZoom(0.8f, 0.95f) == Approx(0.78f));
    CHECK_FALSE(tb::FollowZoom(0.7f, 0.95f)); // already past the least: left as it is
    CHECK_FALSE(tb::FollowZoom(0.3f, 0.95f)); // the tactical view
    CHECK_FALSE(tb::FollowZoom(std::numeric_limits<float>::quiet_NaN(), 0.95f));
}
