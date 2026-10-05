// Whose portrait the game side renders next (core/Portraits.h), over the
// stand-in's fights, and the bar's string carrying them.
#include "World.h"
#include <catch2/catch_approx.hpp>
#include <limits>

#include "core/Bar.h"
#include "core/Portraits.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

TEST_CASE("a creature's picture: the table's own, a render by the table's camera, its kin's, or a render by its kind",
          "[portraits]")
{
    const std::vector<PortraitRow> rows = GameRows();
    const auto choose = [&](std::int32_t appearance, std::wstring_view label, bool person, std::wstring_view tag) {
        return ChoosePortrait(appearance, label, person, tag, rows);
    };
    // The table's own picture: a wolf (the user's "Wo"), a corrupted spider.
    CHECK(choose(49, L"Wolf", false, L"wolf").source == PortraitSource::ReadyMade);
    CHECK(choose(49, L"Wolf", false, L"wolf").made == L"portrait_49");
    CHECK(choose(57, L"Spider, Corrupted", false, L"").made == L"portrait_57");
    // A person is rendered, whatever its tag, and framed by its kind and size.
    CHECK(choose(15, L"Human", true, L"bhn200cr_howe").source == PortraitSource::Render);
    CHECK_FALSE(choose(15, L"Human", true, L"bhn200cr_howe").tableCamera);
    CHECK(choose(0, L"", true, L"").source == PortraitSource::Render);
    // A creature the table has a camera for, by appearance or by tag: a
    // render, and the table's camera left as it is. A person with a row of
    // its own too (a follower fought).
    CHECK(choose(8, L"Bear, Great", false, L"").source == PortraitSource::Render);
    CHECK(choose(8, L"Bear, Great", false, L"").tableCamera);
    CHECK(choose(74, L"Wolf, Blight", false, L"").tableCamera);
    CHECK(choose(35, L"Dog, Party Member", false, L"gen00fl_dog").source == PortraitSource::Render);
    CHECK(choose(35, L"Dog, Party Member", false, L"gen00fl_dog").tableCamera);
    CHECK(choose(15, L"Human", true, L"gen00fl_dog").tableCamera);
    // A giant spider has no row: its kin's, the plain spider's, not a render
    // of its abdomen (the user's picture, 2026-10-01).
    CHECK(choose(58, L"Spider, Giant", false, L"spider_giant").source == PortraitSource::ReadyMade);
    CHECK(choose(58, L"Spider, Giant", false, L"spider_giant").made == L"portrait_59");
    // No picture of it or its kin, and no camera of the table's: a render
    // all the same (issue 32: twelve dragonlings of twelve had initials). No
    // card ends at none for what its kind is.
    for (const PortraitChoice &choice :
         {choose(64, L"Dragonling", false, L"urn220cr_dragonling"), choose(53, L"Witherfang", false, L""),
          choose(34, L"Dog, Mabari", false, L"bhn200cr_mabari"), choose(27, L"Rage Demon", false, L""),
          choose(0, L"", false, L"")})
    {
        CHECK(choice.source == PortraitSource::Render);
        CHECK_FALSE(choice.tableCamera);
        CHECK(choice.made.empty());
    }
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

TEST_CASE("a body is a person's, or stands upright and is not, or goes on all fours", "[portraits]")
{
    // The appearance table (APR_base.gda, 2026-10-04): ModelType H or P is a
    // model put together as a person's is; TerrainFollowPitch is over 0 for
    // what goes on four legs or more. Extents are the engine's, feet to head.
    CHECK(BodyKindOf(true, 0.0f, 1.7f) == BodyKind::Person);  // a human
    CHECK(BodyKindOf(true, 0.0f, 1.2f) == BodyKind::Person);  // a genlock
    CHECK(BodyKindOf(true, 0.0f, 2.1f) == BodyKind::Person);  // a hurlock: tall, not bigger than a person
    CHECK(BodyKindOf(true, -1.0f, 1.7f) == BodyKind::Person); // what is not over 0 stands up
    CHECK(BodyKindOf(true, 0.0f, 2.2f) == BodyKind::Upright);
    CHECK(BodyKindOf(true, 0.0f, 3.25f) == BodyKind::Upright);     // the ogre: an H model that stands up
    CHECK(BodyKindOf(true, 0.0f, 9.21f) == BodyKind::Upright);     // the high dragon, the same
    CHECK(BodyKindOf(false, 0.0f, 1.6f) == BodyKind::Upright);     // a rage demon: a person's size, no person
    CHECK(BodyKindOf(true, 16.0f, 0.4f) == BodyKind::OnAllFours);  // the cat: an H model on four legs
    CHECK(BodyKindOf(false, 16.0f, 1.4f) == BodyKind::OnAllFours); // a dragonling
    CHECK(BodyKindOf(false, 90.0f, 0.6f) == BodyKind::OnAllFours); // a spider
    CHECK(BodyKindOf(true, 0.0f, std::numeric_limits<float>::quiet_NaN()) == BodyKind::Upright);
    // Why, in words.
    CHECK(BodyKindWhy(true, 0.0f, 1.7f) == "a person's model, standing up, no bigger than a person");
    CHECK(BodyKindWhy(false, 16.0f, 9.0f) ==
          "its body pitches with the ground it stands on, by the appearance table's TerrainFollowPitch");
    CHECK(BodyKindWhy(true, 0.0f, 9.0f) == "it stands up, and is bigger than a person from its feet to its head");
    CHECK(BodyKindWhy(false, 0.0f, 1.6f) ==
          "it stands up, and its model is a creature's by the appearance table's ModelType");
    // A tester names a kind, and reads it, by one word.
    CHECK(BodyKindName(BodyKind::Person) == "person");
    CHECK(BodyKindName(BodyKind::Upright) == "upright");
    CHECK(BodyKindName(BodyKind::OnAllFours) == "on_all_fours");
    CHECK(BodyKindNamed("person") == BodyKind::Person);
    CHECK(BodyKindNamed("upright") == BodyKind::Upright);
    CHECK(BodyKindNamed("on_all_fours") == BodyKind::OnAllFours);
    CHECK_FALSE(BodyKindNamed("dragon"));
    CHECK_FALSE(BodyKindNamed("creature"));
    CHECK_FALSE(BodyKindNamed(""));
}

TEST_CASE("a creature's extent is from its feet to its head, whichever way the engine gives the head", "[portraits]")
{
    using Catch::Approx;
    // The high dragon as seen in play (2026-10-04): its head a point in the
    // world, 20 m above its feet.
    const HeadExtent dragon = ExtentOf(Vec3{186.2f, 165.4f, -31.7f}, Vec3{189.6f, 163.3f, -51.8f});
    CHECK(dragon.meters == Approx(20.49f).margin(0.01f));
    CHECK(dragon.world);
    // Were the head about the creature's own origin: its distance from that.
    const HeadExtent own = ExtentOf(Vec3{0.1f, 0.0f, 1.6f}, Vec3{189.6f, 163.3f, -51.8f});
    CHECK(own.meters == Approx(1.603f).margin(0.01f));
    CHECK_FALSE(own.world);
}

TEST_CASE("the framing table: a row a creature type, then the kinds' defaults, each saying where it came from",
          "[portraits]")
{
    // The user, 2026-10-04: "if we can put the portrait angles and stuff in a
    // table somewhere by creature type, we'd have it all in one place."
    std::int32_t last = 0;
    bool defaults = false;
    for (const FramingRow &row : kPortraitTable)
    {
        CAPTURE(row.label);
        CHECK_FALSE(row.label.empty());
        CHECK_FALSE(row.note.empty());
        CHECK(row.distance > 0.0f);
        if (row.appearance == 0)
        {
            // A kind's default: after the appearances' rows, and found for
            // its kind. A person's body has none: the game's own frames it.
            defaults = true;
            CHECK(row.kind != BodyKind::Person);
            CHECK(DefaultRowOf(row.kind) == &row);
            continue;
        }
        CHECK_FALSE(defaults);
        CHECK(row.appearance > last); // in order, each appearance once
        last = row.appearance;
        CHECK(RowOf(row.appearance) == &row);
        // Its label is the appearance table's row and its label there.
        CHECK(row.label.starts_with(std::to_string(row.appearance) + " "));
        // A camera somebody looked at: by eye, with what of it was seen.
        CHECK(row.note.starts_with("by eye "));
        CHECK(row.note.find("seen") != std::string_view::npos);
        // The engine draws to 40 m: no row is framed from past it.
        CHECK(kPortraitBackOff * row.distance < kPortraitFarPlane);
        // A side profile, as people have (the user: "Like maybe a 30 to 45°
        // angle"): no row is straight on, and none is turned further than
        // was asked. The least is a person's own turn, the game's; but for
        // the deepstalker, whose head is itself turned on its neck: at 35
        // degrees its round mouth is edge on, at 20 it is seen with its eye.
        CHECK(row.rotation >= (row.appearance == 33 ? 20.0f : kGamesDefaultRow.rotation));
        CHECK(row.rotation <= 45.0f);
        // A row from the root says so in its note, where a reader looks.
        CHECK(row.fromRoot == (row.note.find("from its root") != std::string_view::npos));
    }
    CHECK(defaults);
    REQUIRE(DefaultRowOf(BodyKind::OnAllFours) != nullptr);
    REQUIRE(DefaultRowOf(BodyKind::Upright) != nullptr);
    CHECK(DefaultRowOf(BodyKind::Person) == nullptr);
    CHECK(DefaultRowOf(BodyKind::OnAllFours)->label == "a body on all fours");
    CHECK(DefaultRowOf(BodyKind::Upright)->label == "an upright body that is not a person's");
    // No row: a person, an appearance the table does not have, none at all.
    CHECK(RowOf(15) == nullptr);
    CHECK(RowOf(26) == nullptr);
    CHECK(RowOf(0) == nullptr); // a default is not an appearance's row
    CHECK(RowOf(-1) == nullptr);
    // The rows of the sweep, 2026-10-04: each found by a search at 35
    // degrees and then looked at. A few of them, as they were seen.
    const FramingRow *mabari = RowOf(34);
    REQUIRE(mabari != nullptr);
    CHECK(mabari->label == "34 Dog, Mabari");
    CHECK(mabari->distance == 0.95f);
    CHECK(mabari->rotation == 35.0f);
    CHECK(mabari->aside == 0.15f);
    CHECK(mabari->up == 0.04f);
    const FramingRow *bronto = RowOf(11);
    REQUIRE(bronto != nullptr);
    CHECK(bronto->distance == 3.0f);
    CHECK(bronto->aside == 0.35f);
    CHECK(bronto->up == -0.45f);
    const FramingRow *archdemon = RowOf(89);
    REQUIRE(archdemon != nullptr);
    CHECK(archdemon->distance == 6.0f);
    CHECK(archdemon->aside == -1.2f);
    CHECK(archdemon->up == -1.45f);
    // The turn's aside is each model's own: to one side for the dragons, to
    // the other for the dog and the bronto, and none for the cat, whose head
    // stays in the middle as it is turned.
    REQUIRE(RowOf(63) != nullptr);
    CHECK(RowOf(63)->rotation == 35.0f);
    CHECK(RowOf(63)->aside == 0.0f);
    CHECK(RowOf(64)->aside < 0.0f);
    CHECK(mabari->aside > 0.0f);
    // A person's body with a row of its own keeps a person's pitch and
    // rotation, from further back: the hurlock alpha for its helm and horns,
    // the werewolf for its muzzle, which the game's own row cut off.
    for (const std::int32_t appearance : {17, 22})
    {
        CAPTURE(appearance);
        const FramingRow *persons = RowOf(appearance);
        REQUIRE(persons != nullptr);
        CHECK(persons->rotation == kGamesDefaultRow.rotation);
        CHECK(persons->pitch == kGamesDefaultRow.pitch);
        CHECK(persons->distance > kGamesDefaultRow.distance);
        // And is framed by it, where a person's body with none is the game's.
        const PortraitFrame frame = FramePortrait(appearance, BodyKind::Person, false, nullptr, 1.9f, kShippedPerson);
        CHECK(frame.by == FramedBy::Row);
        CHECK(frame.camera.distance == persons->distance);
    }
    CHECK(RowOf(22)->distance == 1.05f);
    CHECK(RowOf(22)->aside == 0.15f);
    CHECK(RowOf(22)->up == 0.1f);
    // The one row not itself seen says so: the wild sylvan's.
    REQUIRE(RowOf(65) != nullptr);
    CHECK(RowOf(65)->note.find("not itself seen") != std::string_view::npos);
    // The three the user saw and approved, 2026-10-04 ("the angles look
    // good"), turned as asked ("a 30 to 45° angle"; 35 is what was looked
    // at), each row marked with what of it was seen and what interpolated.
    for (const std::int32_t appearance : {64, 66, 80})
    {
        const FramingRow *row = RowOf(appearance);
        REQUIRE(row != nullptr);
        CHECK(row->rotation >= 30.0f);
        CHECK(row->rotation <= 45.0f);
        CHECK(row->pitch == 0.0f);
    }
    const FramingRow *dragonling = RowOf(64);
    CHECK(dragonling->label == "64 Dragonling");
    CHECK(dragonling->distance == 0.9f);
    CHECK(dragonling->rotation == 35.0f);
    CHECK(dragonling->aside == -0.5f);
    CHECK(dragonling->up == 0.45f);
    CHECK(dragonling->note.find("interpolated between -0.4") != std::string_view::npos);
    const FramingRow *drake = RowOf(80);
    CHECK(drake->label == "80 Dragon, Drake");
    CHECK(drake->distance == 1.5f);
    CHECK(drake->rotation == 35.0f);
    CHECK(drake->aside == -0.85f);
    CHECK(drake->up == 0.8f);
    CHECK(drake->note.find("interpolated between -0.7") != std::string_view::npos);
    const FramingRow *dragon = RowOf(66);
    CHECK(dragon->label == "66 Dragon, High");
    CHECK(dragon->distance == 6.0f);
    CHECK(dragon->rotation == 35.0f);
    CHECK(dragon->aside == -1.7f);
    CHECK(dragon->up == -0.8f);
    CHECK(dragon->note.find("seen as it is") != std::string_view::npos);
    // The ogre, as centred by sight on 2026-10-02 at a person's pitch and
    // rotation (the user: "ogre portrait isn't quite centered").
    const FramingRow *ogre = RowOf(47);
    REQUIRE(ogre != nullptr);
    CHECK(ogre->distance == 2.47f);
    CHECK(ogre->aside == 0.25f);
    CHECK(ogre->up == -0.15f);
    CHECK(ogre->pitch == kGamesDefaultRow.pitch);
    CHECK(ogre->rotation == kGamesDefaultRow.rotation);
    // The game's own Default row, as it ships: what a person's body is framed by.
    CHECK(kShippedPerson.distance == 0.675f);
    CHECK(kShippedPerson.rotation == 23.5f);
    CHECK(kShippedPerson.pitch == -7.5f);
    CHECK(kShippedPerson.xAdjust == 0.007f);
    CHECK(kShippedPerson.zAdjust == 0.038f);
}

TEST_CASE("a creature with no head node is rendered where a row frames it from its root", "[portraits]")
{
    // The user, 2026-10-04, of the deepstalker's initials: "We should try to
    // capture the lamprey like head". Its model has no head node, and the
    // engine's camera then aims at its scene's origin.
    constexpr FramingRow headed = Row(900, "900 Headed", 1.0f, 35.0f, 0.0f, 0.1f, 0.4f, "by eye: seen");
    constexpr FramingRow rooted = FromRoot(headed);
    CHECK_FALSE(headed.fromRoot);
    CHECK(rooted.fromRoot);
    CHECK(rooted.appearance == headed.appearance); // the mark, and nothing else of the row
    CHECK(rooted.distance == headed.distance);
    CHECK(rooted.up == headed.up);
    const PortraitTuning none;
    CHECK(RendersWithoutHead(&rooted, none));
    // A row about a head waits for the head: its creature's is only late.
    CHECK_FALSE(RendersWithoutHead(&headed, none));
    // No row: initials, as before.
    CHECK_FALSE(RendersWithoutHead(nullptr, none));
    // A tester's values render it, with a row or with none: how such a row is found.
    PortraitTuning values;
    values.distance = 1.0f;
    CHECK(RendersWithoutHead(nullptr, values));
    CHECK(RendersWithoutHead(&headed, values));
    // A kind asked for with no value is that kind's default, which is by the
    // feet-to-head there is none of; with a value it is the tester's camera.
    PortraitTuning kind;
    kind.kind = BodyKind::Upright;
    CHECK_FALSE(RendersWithoutHead(&rooted, kind));
    CHECK_FALSE(RendersWithoutHead(nullptr, kind));
    kind.zAdjust = 0.4f;
    CHECK(RendersWithoutHead(nullptr, kind));
    // A default is never from the root: it is by a measure such a body lacks.
    for (const FramingRow &row : kPortraitTable)
    {
        if (row.appearance == 0)
        {
            CHECK_FALSE(row.fromRoot);
        }
    }
    // Such a row's camera is its columns as any row's are.
    CHECK(FrameByRow(rooted).camera.zAdjust == 0.4f);
    CHECK(FrameByRow(rooted).by == FramedBy::Row);
    // The deepstalker's, as it was seen at the camp that day: its eye and
    // its round mouth three-quarter on, 0.83 m above its feet.
    const FramingRow *deepstalker = RowOf(33);
    REQUIRE(deepstalker != nullptr);
    CHECK(deepstalker->fromRoot);
    CHECK(deepstalker->label == "33 Deepstalker");
    CHECK(deepstalker->distance == 0.9f);
    CHECK(deepstalker->rotation == 20.0f);
    CHECK(deepstalker->aside == 0.29f);
    CHECK(deepstalker->up == 0.83f);
    CHECK(RendersWithoutHead(deepstalker, none));
    // The nine appearances with no head node that were looked at have such
    // rows; a werewolf's, which has a head, is about its head, and waits.
    for (const std::int32_t appearance : {33, 73, 76, 77, 78, 81, 85, 86, 87})
    {
        CAPTURE(appearance);
        REQUIRE(RowOf(appearance) != nullptr);
        CHECK(RowOf(appearance)->fromRoot);
        // From the feet up: what is wanted of it is above its root.
        CHECK(RowOf(appearance)->up > 0.0f);
    }
    REQUIRE(RowOf(22) != nullptr);
    CHECK_FALSE(RendersWithoutHead(RowOf(22), none));
    CHECK_FALSE(RendersWithoutHead(RowOf(62), none)); // one nothing uses: no row, initials
}

TEST_CASE("a row's camera is its columns as they are", "[portraits]")
{
    const PortraitFrame dragonling = FrameByRow(*RowOf(64));
    CHECK(dragonling.by == FramedBy::Row);
    CHECK(dragonling.camera.distance == 0.9f);
    CHECK(dragonling.camera.rotation == 35.0f);
    CHECK(dragonling.camera.pitch == 0.0f);
    CHECK(dragonling.camera.xAdjust == -0.5f); // aside
    CHECK(dragonling.camera.zAdjust == 0.45f); // up
    CHECK(dragonling.label == "64 Dragonling");
    CHECK(dragonling.note == RowOf(64)->note);
    CHECK_FALSE(dragonling.lit);
    CHECK_FALSE(dragonling.atMost);
    // The ogre's pitch is a person's.
    CHECK(FrameByRow(*RowOf(47)).camera.pitch == -7.5f);
    // A row with a light of its own carries it.
    const PortraitFrame lit =
        FrameByRow(Lit(Row(9, "9 Bear, Black", 1.4f, 10.0f, -10.0f, 0.0f, -0.2f, "n"), 3.0f, 10.0f, 5.0f));
    CHECK(lit.lit);
    CHECK(lit.lightX == 3.0f);
    CHECK(lit.lightY == 10.0f);
    CHECK(lit.lightZ == 5.0f);
}

TEST_CASE("a default's camera goes by the creature's size, held between the table's least and most", "[portraits]")
{
    using Catch::Approx;
    const FramingRow &fours = *DefaultRowOf(BodyKind::OnAllFours);
    const FramingRow &upright = *DefaultRowOf(BodyKind::Upright);
    // What three bodies wanted by eye, straight on (2026-10-04): the
    // default's distance is within a tenth of each.
    CHECK(FrameByDefault(fours, 1.4f).camera.distance == Approx(0.8f).epsilon(0.1));    // a dragonling, 1.36 to 1.44 m
    CHECK(FrameByDefault(fours, 2.38f).camera.distance == Approx(1.4f).epsilon(0.1));   // a drake
    CHECK(FrameByDefault(upright, 9.21f).camera.distance == Approx(6.0f).epsilon(0.1)); // the high dragon asleep
    CHECK(FrameByDefault(fours, 2.38f).camera.distance == Approx(fours.distance * 2.38f));
    CHECK(FrameByDefault(fours, 2.38f).by == FramedBy::Default);
    CHECK(FrameByDefault(fours, 2.38f).label == "a body on all fours");
    CHECK_FALSE(FrameByDefault(fours, 2.38f).atMost);
    // The dragon in the air, 20.5 m: held at the furthest its head is still
    // drawn from (issue 32: framed from 13.86 m its card was a blank disc).
    const PortraitFrame flying = FrameByDefault(upright, 20.5f);
    CHECK(flying.atMost);
    CHECK(flying.camera.distance == kFramedFromMost);
    CHECK_FALSE(PastFarPlane(flying.camera));
    CHECK(PastFarPlane(PortraitCamera{-7.5f, 23.5f, 13.86f, -1.33f, 1.77f}));
    // A measure next to nothing, or none: not from inside what it looks at.
    for (const float extent :
         {0.2f, 0.0f, -3.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        CAPTURE(extent);
        CHECK(FrameByDefault(fours, extent).camera.distance == kFramedFromLeast);
        CHECK_FALSE(FrameByDefault(fours, extent).atMost);
    }
    // The table's defaults are straight on: a turn needs an aside that is
    // each model's own, and until one is looked at the aim is in the middle.
    for (const FramingRow *row : {&fours, &upright})
    {
        for (float extent = 0.1f; extent < 80.0f; extent += 0.37f)
        {
            const PortraitCamera camera = FrameByDefault(*row, extent).camera;
            CAPTURE(row->label, extent);
            CHECK(camera.rotation == 0.0f);
            CHECK(camera.xAdjust == 0.0f);
            CHECK(camera.distance >= kFramedFromLeast);
            CHECK(camera.distance <= kFramedFromMost);
            CHECK_FALSE(PastFarPlane(camera));
        }
    }
    // A default that turns: its aside and up go by the size as its distance
    // does, the same picture at any size, and at the limits too.
    const FramingRow turned = DefaultFor(BodyKind::OnAllFours, "a turned one", 0.5f, 35.0f, -5.0f, -0.3f, 0.2f, "n");
    const PortraitCamera twice = FrameByDefault(turned, 2.0f).camera;
    CHECK(twice.distance == Approx(1.0f));
    CHECK(twice.rotation == 35.0f);
    CHECK(twice.pitch == -5.0f);
    CHECK(twice.xAdjust == Approx(-0.6f));
    CHECK(twice.zAdjust == Approx(0.4f));
    const PortraitCamera huge = FrameByDefault(turned, 100.0f).camera;
    CHECK(huge.distance == kFramedFromMost);
    CHECK(huge.xAdjust == Approx(-0.3f * kFramedFromMost / 0.5f));
    CHECK(huge.zAdjust == Approx(0.2f * kFramedFromMost / 0.5f));
    const PortraitCamera tiny = FrameByDefault(turned, 0.1f).camera;
    CHECK(tiny.distance == kFramedFromLeast);
    CHECK(tiny.xAdjust == Approx(-0.3f * kFramedFromLeast / 0.5f));
    // The limits: no nearer than 0.3 m, no further than the engine draws.
    CHECK(kFramedFromLeast == 0.3f);
    CHECK(kFramedFromMost == 12.0f);
    CHECK(kPortraitBackOff * kFramedFromMost < kPortraitFarPlane);
}

TEST_CASE("a creature's framing is looked up: its own row, the game's own row, its kind's default", "[portraits]")
{
    using Catch::Approx;
    const PortraitCamera bear{-10.0f, 10.0f, 1.4f, -0.25f, 0.04f}; // the game's row for a great bear
    // Its appearance's own row first: a dragonling, whatever its size.
    const PortraitFrame dragonling = FramePortrait(64, BodyKind::OnAllFours, false, nullptr, 1.4f, kShippedPerson);
    CHECK(dragonling.by == FramedBy::Row);
    CHECK(dragonling.label == "64 Dragonling");
    CHECK(dragonling.camera.distance == 0.9f);
    CHECK(dragonling.camera.rotation == 35.0f);
    // The high dragon in the air: its row frames it as asleep, whatever its
    // pose measures (its default would hold it at the furthest: "small").
    const PortraitFrame dragon = FramePortrait(66, BodyKind::Upright, false, nullptr, 20.5f, kShippedPerson);
    CHECK(dragon.by == FramedBy::Row);
    CHECK(dragon.camera.distance == 6.0f);
    CHECK_FALSE(dragon.atMost);
    // A row of the table's wins over the game's own row for the creature.
    CHECK(FramePortrait(64, BodyKind::OnAllFours, false, &bear, 1.4f, kShippedPerson).by == FramedBy::Row);
    // The game's own row, where the portraits table frames the creature
    // itself: untouched, whatever its kind.
    const PortraitFrame greatBear = FramePortrait(8, BodyKind::OnAllFours, false, &bear, 2.0f, kShippedPerson);
    CHECK(greatBear.by == FramedBy::PortraitsTable);
    CHECK(greatBear.camera.distance == 1.4f);
    CHECK(greatBear.camera.rotation == 10.0f);
    CHECK(greatBear.label.empty());
    CHECK(FramePortrait(15, BodyKind::Person, false, &bear, 1.7f, kShippedPerson).by == FramedBy::PortraitsTable);
    // Else its kind's default row.
    // (Appearances with no row of the table's: a wolf, a revenant grown tall.)
    const PortraitFrame mabari = FramePortrait(49, BodyKind::OnAllFours, false, nullptr, 1.2f, kShippedPerson);
    CHECK(mabari.by == FramedBy::Default);
    CHECK(mabari.label == "a body on all fours");
    CHECK(mabari.camera.distance == Approx(0.72f));
    const PortraitFrame demon = FramePortrait(26, BodyKind::Upright, false, nullptr, 1.6f, kShippedPerson);
    CHECK(demon.by == FramedBy::Default);
    CHECK(demon.label == "an upright body that is not a person's");
    CHECK(demon.camera.distance == Approx(0.96f));
    // And a person's body, which has no default of the table's: the game's
    // Default row as it is.
    const PortraitFrame human = FramePortrait(15, BodyKind::Person, false, nullptr, 1.7f, kShippedPerson);
    CHECK(human.by == FramedBy::Person);
    CHECK(human.camera.distance == kShippedPerson.distance);
    CHECK(human.camera.rotation == kShippedPerson.rotation);
    CHECK(human.camera.xAdjust == kShippedPerson.xAdjust);
    CHECK(human.label.empty());
    CHECK(FramePerson(PortraitCamera{0.0f, 10.0f, 0.7f, 0.0f, 0.0f}).camera.rotation == 10.0f); // a mod's table
    // A kind a tester asks for is framed by that kind's default, the rows
    // passed over: the default beside the row.
    const PortraitFrame asked = FramePortrait(64, BodyKind::OnAllFours, true, nullptr, 1.4f, kShippedPerson);
    CHECK(asked.by == FramedBy::Default);
    CHECK(asked.camera.distance == Approx(0.84f));
    CHECK(asked.camera.rotation == 0.0f);
    CHECK(FramePortrait(8, BodyKind::Upright, true, &bear, 2.0f, kShippedPerson).by == FramedBy::Default);
    CHECK(FramePortrait(64, BodyKind::Person, true, &bear, 1.4f, kShippedPerson).by == FramedBy::Person);
    // Which row it was, in the log's words.
    CHECK(FramingRule(dragon) ==
          "row 66 Dragon, High (by eye above Haven, 2026-10-04, asleep: its head in three-quarter profile, its "
          "horns, its neck and a wing behind; seen as it is, the head a little right of the middle)");
    CHECK(FramingRule(mabari) ==
          "the default for a body on all fours (straight on, from 0.6 of its extent: not yet looked at)");
    CHECK(FramingRule(demon).starts_with("the default for an upright body that is not a person's ("));
    CHECK(FramingRule(greatBear) == "the portraits table's own row for it");
    CHECK(FramingRule(human) == "a person's body: the portraits table's Default row");
    CHECK(FramingRule(PortraitFrame{}).empty());
    CHECK(FramedByName(FramedBy::Row) == "row");
    CHECK(FramedByName(FramedBy::PortraitsTable) == "portraits_table");
    CHECK(FramedByName(FramedBy::Default) == "default");
    CHECK(FramedByName(FramedBy::Person) == "person");
    CHECK(FramedByName(FramedBy::Nothing).empty());
}

TEST_CASE("a tester's value replaces the rule's, and the rest are the rule's", "[portraits]")
{
    const PortraitCamera rule{-7.5f, 23.5f, 12.0f, -1.14f, 1.52f};
    PortraitTuning tuning;
    CHECK_FALSE(tuning.AnyValue());
    const PortraitCamera same = Tuned(rule, tuning);
    CHECK(same.distance == rule.distance);
    CHECK(same.pitch == rule.pitch);
    tuning.distance = 8.0f;
    tuning.xAdjust = 0.0f;
    CHECK(tuning.AnyValue());
    const PortraitCamera tuned = Tuned(rule, tuning);
    CHECK(tuned.distance == 8.0f);
    CHECK(tuned.xAdjust == 0.0f);
    CHECK(tuned.zAdjust == rule.zAdjust);
    CHECK(tuned.pitch == rule.pitch);
    CHECK(tuned.rotation == rule.rotation);
    PortraitTuning turned;
    turned.pitch = 10.0f;
    turned.rotation = -40.0f;
    turned.zAdjust = 0.5f;
    CHECK(Tuned(rule, turned).pitch == 10.0f);
    CHECK(Tuned(rule, turned).rotation == -40.0f);
    CHECK(Tuned(rule, turned).zAdjust == 0.5f);
    CHECK(Tuned(rule, turned).distance == rule.distance);
    // A kind alone is not a value, and neither kind nor a light moves the camera.
    PortraitTuning kind;
    kind.kind = BodyKind::OnAllFours;
    CHECK_FALSE(kind.AnyValue());
    PortraitTuning light;
    light.lightY = 80.0f;
    CHECK(light.AnyValue());
    CHECK(Tuned(rule, light).distance == rule.distance);
}

TEST_CASE("the picture is half the table's distance across, and nothing past 40 m of the camera is drawn",
          "[portraits]")
{
    using Catch::Approx;
    // The engine's own numbers (PortraitRenderer::PlaceCamera, 2026-10-04):
    // three times the distance back, ten degrees of view.
    CHECK(PictureAcross(kShippedPerson) == Approx(0.354f).margin(0.001f));
    CHECK(PictureAcross(PortraitCamera{0.0f, 0.0f, 12.0f, 0.0f, 0.0f}) == Approx(6.3f));
    CHECK_FALSE(PastFarPlane(PortraitCamera{0.0f, 0.0f, 13.3f, 0.0f, 0.0f}));
    CHECK(PastFarPlane(PortraitCamera{0.0f, 0.0f, 13.4f, 0.0f, 0.0f}));
}

TEST_CASE("the portrait tool's arguments: an id, and any of a kind, the camera's values and a showing", "[portraits]")
{
    // An id alone: the rule as built, nothing shown.
    const PortraitAskRead plain = ReadPortraitAsk(R"({"id": "0x28df3"})");
    REQUIRE(plain.ask);
    CHECK(plain.error.empty());
    CHECK(plain.ask->id == 0x28df3);
    CHECK_FALSE(plain.ask->tuning.kind);
    CHECK_FALSE(plain.ask->tuning.AnyValue());
    CHECK_FALSE(plain.ask->show);
    CHECK(plain.ask->seconds == kShowSeconds);
    // Everything given.
    const PortraitAskRead full = ReadPortraitAsk(
        R"({"id": 2013, "kind": "on_all_fours", "pitch": -10, "rotation": 5.5, "distance": 8, "zAdjust": -0.25, )"
        R"("xAdjust": 1.5, "lightX": -5, "lightY": 80, "lightZ": 5, "show": true, "seconds": 60})");
    REQUIRE(full.ask);
    CHECK(full.ask->id == 2013);
    CHECK(full.ask->tuning.kind == BodyKind::OnAllFours);
    CHECK(full.ask->tuning.pitch == -10.0f);
    CHECK(full.ask->tuning.rotation == 5.5f);
    CHECK(full.ask->tuning.distance == 8.0f);
    CHECK(full.ask->tuning.zAdjust == -0.25f);
    CHECK(full.ask->tuning.xAdjust == 1.5f);
    CHECK(full.ask->tuning.lightX == -5.0f);
    CHECK(full.ask->tuning.lightY == 80.0f);
    CHECK(full.ask->tuning.lightZ == 5.0f);
    CHECK(full.ask->show);
    CHECK(full.ask->seconds == 60);
    CHECK(ReadPortraitAsk(R"({"id": "0x7d9", "kind": "person"})").ask->tuning.kind == BodyKind::Person);
    CHECK(ReadPortraitAsk(R"({"id": "0x7d9", "kind": "upright"})").ask->tuning.kind == BodyKind::Upright);
    CHECK(ReadPortraitAsk(R"({"id": "0X7D9", "show": false})").ask->id == 0x7d9);
    CHECK_FALSE(ReadPortraitAsk(R"({"id": "0x7d9", "show": false})").ask->show);
    CHECK(ReadPortraitAsk(R"({"id": "0x7d9", "seconds": 5000})").ask->seconds == kShowSecondsMost);
    // What is wrong is said, with what to give: never taken for the rule's.
    for (const char *wrong :
         {R"({})", R"({"id": "the dragon"})", R"({"id": "0x"})", R"({"id": "0xFFFFFFFF"})", R"({"id": "7d9"})",
          R"({"id": "0x7d9", "kind": "dragon"})", R"({"id": "0x7d9", "kind": "creature"})",
          R"({"id": "0x7d9", "distance": "far"})", R"({"id": "0x7d9", "distance": 0})",
          R"({"id": "0x7d9", "distance": -2})", R"({"id": "0x7d9", "xAdjust": "1.5 m"})",
          R"({"id": "0x7d9", "pitch": "nan"})", R"({"id": "0x7d9", "show": "yes"})", R"({"id": "0x7d9", "seconds": 0})",
          R"({"id": "0x7d9", "seconds": 2.5})", R"(not json)"})
    {
        CAPTURE(wrong);
        const PortraitAskRead read = ReadPortraitAsk(wrong);
        CHECK_FALSE(read.ask);
        CHECK(read.error.find("Give the creature's id") != std::string::npos);
    }
    CHECK(ReadPortraitAsk(R"({"id": "0x7d9", "kind": "dragon"})").error.starts_with("The kind is not one there is."));
    CHECK(ReadPortraitAsk(R"({"id": "0x7d9", "zAdjust": "up"})").error.starts_with("zAdjust is not a number."));
    CHECK(ReadPortraitAsk(R"({})").error.starts_with("There is no id."));
}

namespace
{
// The high dragon's portrait as its row frames it while it is in the air, as
// the game layer would report it.
PortraitReport DragonReport()
{
    PortraitReport report;
    report.id = 0x28df3;
    report.name = L"High Dragon";
    report.tag = L"urn220cr_dragon";
    report.appearance = 66;
    report.label = L"Dragon, High";
    report.personalSpace = 3.0f;
    report.personsModel = true;
    report.terrainFollowPitch = 0.0f;
    report.kind = BodyKindOf(true, 0.0f, 20.49f);
    report.kindBy = std::string{BodyKindWhy(true, 0.0f, 20.49f)};
    report.source = "rendered";
    report.image = L"img://portrait_urn220cr_dragon4";
    report.rendered = true;
    report.fresh = true;
    report.cameraGiven = true;
    const PortraitFrame frame = FramePortrait(66, report.kind, false, nullptr, 20.49f, kShippedPerson);
    report.camera = frame.camera;
    report.framedBy = frame.by;
    report.row = frame.label;
    report.atMost = frame.atMost;
    report.rule = FramingRule(frame);
    report.feet = Vec3{189.6f, 163.3f, -51.8f};
    report.head = Vec3{186.2f, 165.4f, -31.7f};
    report.extent = 20.49f;
    report.extentBy = "its head a point in the world";
    return report;
}
} // namespace

TEST_CASE("the log says of a portrait its source, the rule, the camera, the extent and where the head is",
          "[portraits]")
{
    CHECK(PortraitLine(DragonReport()) ==
          "High Dragon's portrait: img://portrait_urn220cr_dragon4 (rendered; row 66 Dragon, High (by eye above "
          "Haven, 2026-10-04, asleep: its head in three-quarter profile, its horns, its neck and a wing behind; "
          "seen as it is, the head a little right of the middle): from 6.00 m, pitch 0.0, rotation 35.0, -1.70 m "
          "aside, -0.80 m up: a picture 3.15 m across; appearance 66 \"Dragon, High\"; 20.49 m from feet to head "
          "(its head a point in the world), its head node at (186.2, 165.4, -31.7), itself at (189.6, 163.3, "
          "-51.8))");
    // The game's own camera is said to be the table's; one held at the
    // furthest says so.
    PortraitReport bear = DragonReport();
    bear.cameraGiven = false;
    bear.rule = "the portraits table's own row for it";
    CHECK(PortraitLine(bear).find("the portraits table's own row for it: the table's own, from 6.00 m") !=
          std::string::npos);
    PortraitReport far = DragonReport();
    far.atMost = true;
    CHECK(PortraitLine(far).find("across, held where its head is still drawn; appearance 66") != std::string::npos);
    // What is not a render says its source and no camera.
    PortraitReport hero;
    hero.name = L"Lyna";
    hero.source = "the game's";
    hero.image = L"img://portrait_player0";
    CHECK(PortraitLine(hero) == "Lyna's portrait: img://portrait_player0 (the game's)");
    // And none says which of the things that cannot be rendered it is.
    PortraitReport none;
    none.id = 0x7d9;
    none.name = L"Wisp";
    none.appearance = 14;
    none.label = L"Wisp";
    none.lack = PortraitLack::HeadNode;
    none.problem = "its model has no head node for a portrait's camera to aim at";
    none.feet = Vec3{1.0f, 2.0f, 3.0f};
    CHECK(PortraitLine(none) ==
          "Wisp (0x000007d9) has no portrait: its card shows initials (its model has no head node for a portrait's "
          "camera to aim at; appearance 14 \"Wisp\"; no head node, itself at (1.0, 2.0, 3.0))");
    PortraitReport gone;
    gone.id = 0x7d9;
    gone.problem = "no creature by that id";
    CHECK(PortraitLine(gone) == " (0x000007d9) has no portrait: its card shows initials (no creature by that id)");
    PortraitReport off;
    off.id = 0x7d9;
    off.name = L"Genlock";
    CHECK(PortraitLine(off) == "Genlock (0x000007d9) has no portrait: its card shows initials");
}

TEST_CASE("the portrait tool's answer has everything a tester tunes by", "[portraits]")
{
    CHECK(ToJson(DragonReport()) ==
          R"({"id":"0x28df3","name":"High Dragon","tag":"urn220cr_dragon","appearance":66,"label":"Dragon, High",)"
          R"("personal_space":3.00,"persons_model":true,"terrain_follow_pitch":0.0,"kind":"upright","kind_by":)"
          R"("it stands up, and is bigger than a person from its feet to its head","source":"rendered",)"
          R"("framed_by":"row","row":"66 Dragon, High","rule":"row 66 Dragon, High (by eye above Haven, )"
          R"(2026-10-04, asleep: its head in three-quarter profile, its horns, its neck and a wing behind; seen as )"
          R"x(it is, the head a little right of the middle)","image":"img://portrait_urn220cr_dragon4",)x"
          R"("texture":"portrait_urn220cr_dragon4.dds",)"
          R"("ready":true,"lack":"","rendered":true,"aimed_at":"head_node","fresh":true,"shown":false,)"
          R"("camera":{"given":true,)"
          R"("pitch":0.00,"rotation":35.00,"distance":6.000,"zAdjust":-0.800,"xAdjust":-1.700,"lightX":null,)"
          R"("lightY":null,"lightZ":null},"at_most":false,"distance_per_extent":0.29,"z_per_extent":-0.04,)"
          R"("picture_m":3.15,"camera_from_head_m":18.00,"past_far_plane":false,"position":)"
          R"([189.60,163.30,-51.80],"head":[186.20,165.40,-31.70],"extent":20.49,"extent_by":)"
          R"("its head a point in the world","problem":""})");
    // The camera against the size, as a row of the table is reckoned: the
    // dragonling's straight on, by eye, 0.8 m and 0.5 m up for 1.4 m.
    PortraitReport dragonling;
    dragonling.camera = PortraitCamera{0.0f, 0.0f, 0.8f, 0.5f, 0.02f};
    dragonling.extent = 1.4f;
    CHECK(ToJson(dragonling).find(R"("distance_per_extent":0.57,"z_per_extent":0.36)") != std::string::npos);
    // No head node: null, and why nothing was rendered; a light given is said.
    PortraitReport none;
    none.id = 0x7d9;
    none.label = L"Wisp \"A\"";
    none.lack = PortraitLack::HeadNode;
    none.personsModel = false;
    none.source = "none";
    none.problem = "its model has no head node";
    none.lightY = 80.0f;
    const std::string json = ToJson(none);
    CHECK(json.find(R"("head":null)") != std::string::npos);
    CHECK(json.find(R"("ready":false,"lack":"no_head_node","rendered":false,"aimed_at":"")") != std::string::npos);
    CHECK(json.find(R"("persons_model":false)") != std::string::npos);
    // One with no head node that a row from its root framed: rendered and
    // ready, its lack kept, and what its camera was set about said.
    PortraitReport rooted = none;
    rooted.rendered = true;
    rooted.image = L"img://portrait_deepstalker1";
    rooted.source = "rendered";
    CHECK(ToJson(rooted).find(R"("ready":true,"lack":"no_head_node","rendered":true,"aimed_at":"scene_origin")") !=
          std::string::npos);
    const std::string line = PortraitLine(rooted);
    CHECK(line.find("no head node, itself at") != std::string::npos);
    CHECK(line.find("has no portrait") == std::string::npos);
    CHECK(json.find(R"("framed_by":"","row":"")") != std::string::npos);
    CHECK(json.find(R"("distance_per_extent":0.00,"z_per_extent":0.00)") != std::string::npos);
    // Each thing that cannot be rendered has its name, for a script to tell
    // "ask again in a moment" from "there is no such creature".
    CHECK(PortraitLackName(PortraitLack::Nothing).empty());
    CHECK(PortraitLackName(PortraitLack::Creature) == "no_creature");
    CHECK(PortraitLackName(PortraitLack::HeadNode) == "no_head_node");
    CHECK(PortraitLackName(PortraitLack::Render) == "no_render");
    CHECK(json.find(R"("label":"Wisp \"A\"")") != std::string::npos);
    CHECK(json.find(R"("lightY":80.00)") != std::string::npos);
    CHECK(json.find(R"("problem":"its model has no head node")") != std::string::npos);
    // A camera past the far plane is said to be.
    PortraitReport far = DragonReport();
    far.camera.distance = 13.86f;
    CHECK(ToJson(far).find(R"("camera_from_head_m":41.58,"past_far_plane":true)") != std::string::npos);
}

TEST_CASE("a portrait put up large goes to the movie as its texture and two lines, with a serial", "[portraits][bar]")
{
    const PortraitReport dragon = DragonReport();
    CHECK(ShowTitle(dragon) == L"66 Dragon, High: its own row");
    CHECK(ShowDetail(dragon) ==
          L"6.00 m, pitch 0.0, rotation 35.0, -1.70 aside, -0.80 up; 3.15 m across; feet to head 20.49 m");
    const auto fields = Split(EncodeShow(7, dragon.image, ShowTitle(dragon), ShowDetail(dragon)), kShowField);
    REQUIRE(fields.size() == 5);
    CHECK(fields[0] == std::to_wstring(kShowVersion));
    CHECK(fields[1] == L"7");
    CHECK(fields[2] == L"portrait_urn220cr_dragon4.dds"); // as the game's image loader finds it
    CHECK(fields[3] == L"66 Dragon, High: its own row");
    CHECK(fields[4] == ShowDetail(dragon));
    // The movie splits it as it splits the bar's own.
    CHECK(kShowField == kBarField);
    // Nothing in a field may split it.
    CHECK(Split(EncodeShow(8, L"", std::wstring{L"a"} + kShowField + L"b", L""), kShowField).size() == 5);
    // Taken down: the serial alone, for the movie to say back.
    const auto down = Split(EncodeShow(9, {}, {}, {}), kShowField);
    REQUIRE(down.size() == 5);
    CHECK(down[1] == L"9");
    CHECK(down[2].empty());
    CHECK(down[3].empty());
    // A render says what framed it: its own row, its kind's default by the
    // kind's name, the game's own row. A picture that is not a render says
    // whose it is, and none says so with why.
    PortraitReport wolf;
    wolf.appearance = 49;
    wolf.label = L"Wolf";
    wolf.kind = BodyKind::OnAllFours;
    wolf.source = "the portraits table's";
    wolf.image = L"img://portrait_49";
    CHECK(ShowTitle(wolf) == L"49 Wolf: the portraits table's");
    CHECK(ShowDetail(wolf).empty());
    wolf.rendered = true;
    wolf.framedBy = FramedBy::Default;
    CHECK(ShowTitle(wolf) == L"49 Wolf: the default, on all fours");
    wolf.kind = BodyKind::Upright;
    CHECK(ShowTitle(wolf) == L"49 Wolf: the default, upright");
    wolf.framedBy = FramedBy::PortraitsTable;
    CHECK(ShowTitle(wolf) == L"49 Wolf: the game's own row");
    wolf.kind = BodyKind::Person;
    wolf.framedBy = FramedBy::Person;
    CHECK(ShowTitle(wolf) == L"49 Wolf: person");
    wolf.atMost = true;
    wolf.camera.distance = 12.0f;
    CHECK(ShowDetail(wolf).ends_with(L"; at the furthest"));
    PortraitReport none;
    none.appearance = 14;
    none.label = L"Wisp";
    none.problem = "its model has no head node";
    CHECK(ShowTitle(none) == L"14 Wisp: no picture");
    CHECK(ShowDetail(none) == L"its model has no head node");
}

TEST_CASE("the book says whose a card's portrait is: tried, rendered for the bar, waiting to be released",
          "[portraits]")
{
    World w;
    w.StartFight();
    PortraitBook book;
    CHECK_FALSE(book.Has(kGenlock));
    FillBook(book, w.machine.View());
    CHECK(book.Has(kGenlock));
    CHECK(book.RenderedFor(kGenlock));
    CHECK(book.Has(kHero));
    CHECK_FALSE(book.RenderedFor(kHero)); // the party's is the game's own
    CHECK_FALSE(book.Leaving(kGenlock));
    EndFight(w);
    book.Follow(w.machine.View(), 1000);
    CHECK_FALSE(book.Has(kGenlock));
    CHECK(book.Leaving(kGenlock)); // its renderer is still there
    CHECK_FALSE(book.Leaving(kHero));
    book.MovieLetGo();
    (void)book.TakeReleases(1000);
    CHECK_FALSE(book.Leaving(kGenlock));
}

TEST_CASE("the camera stands back on a big creature's turn, by its size, the zoom left alone", "[portraits]")
{
    using Catch::Approx;
    // The user: "for ogre or dragon pull back more, for human sized enemies
    // same as player"; and of the high dragon: "Should do so automatically
    // depending on enemy's size". The size is the creature's feet to its
    // head, as the engine places them.
    CHECK_FALSE(tb::CloseDistanceFor(1.7f)); // a person: the game's own distance
    CHECK_FALSE(tb::CloseDistanceFor(1.2f)); // a genlock
    CHECK_FALSE(tb::CloseDistanceFor(2.0f)); // a tall person: not yet bigger than one
    REQUIRE(tb::CloseDistanceFor(3.4f));
    CHECK(*tb::CloseDistanceFor(3.4f) == Approx(6.0f));            // an ogre: twice a person, twice the distance
    CHECK(*tb::CloseDistanceFor(6.8f) == Approx(12.0f));           // a dragon's reach
    CHECK(*tb::CloseDistanceFor(20.0f) == Approx(tb::kCloseMost)); // never past the most
    CHECK(*tb::CloseDistanceFor(2.2f) > tb::kCloseGame);           // further than the game's own, whenever it is given
    CHECK_FALSE(tb::CloseDistanceFor(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(tb::CloseDistanceFor(std::numeric_limits<float>::infinity()));
    CHECK_FALSE(tb::CloseDistanceFor(-3.0f));
}

namespace
{
// The game's own distance, as the cases below read it: nothing set.
constexpr float kGamesOwn = 0.0f;

// How far the camera stands for a creature whose model's box is `across` by
// `along` and `high`, with no head node to go by; `by` is which of the box's
// two measures is to have decided it.
float ByBox(tb::CloseBy by, float across, float along, float high)
{
    const tb::CloseDistance close = tb::CloseDistanceOf(tb::Vec3{across, along, high}, std::nullopt);
    CHECK(close.by == by);
    return close.meters.value_or(kGamesOwn);
}
float ByLength(float across, float along, float high)
{
    return ByBox(tb::CloseBy::Length, across, along, high);
}
float ByHeight(float across, float along, float high)
{
    return ByBox(tb::CloseBy::Height, across, along, high);
}
} // namespace

TEST_CASE("the camera stands back by a box's length against a person's across, or its height against a person's",
          "[portraits]")
{
    using Catch::Approx;
    // Issue 33. The boxes are the engine's as seen in play (DAOLib's
    // dao/World.h: Ortan Thaig, 2026-10-04, and the high dragon on its
    // mountain top, 2026-10-05), each across by along and high. By its feet
    // to its head a giant spider was no bigger than a person; by the largest
    // of its box's sides against a person's height it stood 3.8 m off, which
    // in play did not clear it (the Dead Trenches, 2026-10-05).
    //
    // A person, the smallest and the largest seen there, arms out (how deep
    // a person's box is was not written down: 0.3 m here, as the four below
    // are): the game's own, or within 10 cm of it.
    CHECK(ByHeight(1.05f, 0.30f, 1.60f) == kGamesOwn);
    CHECK(ByLength(1.24f, 0.30f, 1.83f) == Approx(3.1f));
    CHECK(ByLength(1.24f, 0.30f, 1.83f) > tb::kCloseGame); // further than the game's own, whenever it is given
    // A person's own measures, either and both: the game's own.
    CHECK(ByHeight(tb::kPersonAcross, 0.30f, tb::kPersonHeight) == kGamesOwn);
    CHECK(ByHeight(1.00f, 0.30f, tb::kPersonHeight) == kGamesOwn);
    CHECK(ByLength(tb::kPersonAcross, 0.30f, 1.50f) == kGamesOwn);
    // The four of the party at the high dragon's fight (DAOLib's example's
    // log, 2026-10-05: Lyna, Wynne, Alistair and one more), as they would be
    // were they enemies: the party's own turns are the game's own whatever
    // its boxes. The two bigger are past a person's measures, and by their
    // arms more than by their height (which gives them 3.2 and 3.48 m).
    CHECK(ByHeight(1.05f, 0.28f, 1.68f) == kGamesOwn);
    CHECK(ByHeight(1.14f, 0.32f, 1.79f) == kGamesOwn);
    CHECK(ByLength(1.30f, 0.38f, 1.92f) == Approx(3.25f));
    CHECK(ByLength(1.45f, 0.43f, 2.09f) == Approx(3.625f));
    // A giant spider, long and low: by its length, against what a person is
    // across.
    CHECK(ByLength(1.76f, 2.30f, 1.19f) == Approx(5.75f));
    // Its length is the larger of its two sides across, whichever that is.
    CHECK(ByLength(2.30f, 1.76f, 1.19f) == Approx(5.75f));
    // A deepstalker, as low and longer.
    CHECK(ByLength(0.82f, 2.54f, 0.90f) == Approx(6.35f));
    // A bronto, taller than a person and longer than it is tall.
    CHECK(ByLength(1.37f, 3.80f, 2.44f) == Approx(9.5f));
    // An ogre, upright with its arms out: its arms are more of a person's
    // across than its height is of a person's height (which gives it 7.67 m).
    CHECK(ByLength(4.00f, 1.19f, 4.60f) == Approx(10.0f));
    // A body more of a person's height than of a person's across goes by its
    // height. No box seen that stands the camera back is like it: this one
    // is made up, a tree's shape.
    CHECK(ByHeight(1.20f, 0.80f, 3.60f) == Approx(6.0f));
    // The high dragon, whose box is its spread wings: never past the most.
    CHECK(ByLength(31.07f, 21.75f, 14.82f) == Approx(tb::kCloseMost));
    // Something small (a rat's size): never under the game's own.
    CHECK(ByLength(0.30f, 0.60f, 0.25f) == kGamesOwn);
}

TEST_CASE("with no box the camera stands back by the feet to the head, and with neither as the game has it",
          "[portraits]")
{
    using Catch::Approx;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    // The box comes first where there are both: a giant spider with a head
    // node as high as an ogre's is still a giant spider.
    const tb::CloseDistance both = tb::CloseDistanceOf(tb::Vec3{1.76f, 2.30f, 1.19f}, 3.4f);
    CHECK(both.by == tb::CloseBy::Length);
    CHECK(both.meters.value_or(kGamesOwn) == Approx(5.75f));
    // No box at all (a model of a kind DAOLib does not read): by the feet to
    // the head, as it was before the box.
    const tb::CloseDistance head = tb::CloseDistanceOf(std::nullopt, 3.4f);
    CHECK(head.by == tb::CloseBy::Head);
    CHECK(head.meters.value_or(kGamesOwn) == Approx(6.0f));
    const tb::CloseDistance person = tb::CloseDistanceOf(std::nullopt, 1.7f);
    CHECK(person.by == tb::CloseBy::Head);
    CHECK_FALSE(person.meters);
    // Neither: the game's own.
    const tb::CloseDistance neither = tb::CloseDistanceOf(std::nullopt, std::nullopt);
    CHECK(neither.by == tb::CloseBy::Nothing);
    CHECK_FALSE(neither.meters);
    // A box that is no measure is as no box: a model with no mesh, a box
    // turned inside out, a side that is not a number wherever it stands.
    for (const tb::Vec3 &none :
         {tb::Vec3{0.0f, 0.0f, 0.0f}, tb::Vec3{-1.0f, -2.0f, -3.0f}, tb::Vec3{nan, 2.30f, 1.19f},
          tb::Vec3{1.76f, nan, 1.19f}, tb::Vec3{1.76f, 2.30f, nan}, tb::Vec3{infinity, 2.30f, 1.19f},
          tb::Vec3{1.76f, infinity, 1.19f}, tb::Vec3{1.76f, 2.30f, infinity}})
    {
        const tb::CloseDistance byHead = tb::CloseDistanceOf(none, 3.4f);
        CHECK(byHead.by == tb::CloseBy::Head);
        CHECK(byHead.meters.value_or(kGamesOwn) == Approx(6.0f));
        const tb::CloseDistance byNothing = tb::CloseDistanceOf(none, std::nullopt);
        CHECK(byNothing.by == tb::CloseBy::Nothing);
        CHECK_FALSE(byNothing.meters);
    }
    // A head that is no measure is the game's own, said to be by the head.
    const tb::CloseDistance noHead = tb::CloseDistanceOf(std::nullopt, nan);
    CHECK(noHead.by == tb::CloseBy::Head);
    CHECK_FALSE(noHead.meters);
}

TEST_CASE("an enemy is looked at from 40 degrees round to its right or its left, behind it or in front of it",
          "[portraits]")
{
    using Catch::Approx;
    const float halfTurn = 3.14159265f;
    // The user: "maybe don't be directly behind enemies. Perhaps offset it by
    // 30 or 45 degrees to the right (or left if something is blocking the
    // right side)".
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::Right) == Approx(1.0f + tb::kViewOffset));
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::Left) == Approx(1.0f - tb::kViewOffset));
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::Behind) == Approx(1.0f));
    CHECK(tb::kViewOffset > 0.52f); // 30 degrees
    CHECK(tb::kViewOffset < 0.79f); // 45 degrees
    // And of the Broodmother: "What if we just turn the camera front instead
    // of back with an offset?" In front is half a turn from behind, and the
    // creature's right is the same side of it from in front as from behind:
    // the offset mirrored.
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::Front) == Approx(1.0f + halfTurn));
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::FrontRight) == Approx(1.0f + halfTurn - tb::kViewOffset));
    CHECK(tb::ViewYaw(1.0f, tb::ViewSide::FrontLeft) == Approx(1.0f + halfTurn + tb::kViewOffset));
    // A creature facing +y has +x on its right and the camera behind it at
    // -y: each place to its right is on the +x side of it.
    const float behind = -halfTurn / 2.0f;
    CHECK(std::cos(tb::ViewYaw(behind, tb::ViewSide::Right)) > 0.5f);
    CHECK(std::sin(tb::ViewYaw(behind, tb::ViewSide::Right)) < -0.5f);
    CHECK(std::cos(tb::ViewYaw(behind, tb::ViewSide::FrontRight)) > 0.5f);
    CHECK(std::sin(tb::ViewYaw(behind, tb::ViewSide::FrontRight)) > 0.5f);
    CHECK(std::cos(tb::ViewYaw(behind, tb::ViewSide::Left)) < -0.5f);
    CHECK(std::cos(tb::ViewYaw(behind, tb::ViewSide::FrontLeft)) < -0.5f);
    CHECK(std::sin(tb::ViewYaw(behind, tb::ViewSide::Front)) > 0.99f);
    // The log's words for each.
    CHECK(tb::ViewSideWords(tb::ViewSide::Right) == "behind it to its right");
    CHECK(tb::ViewSideWords(tb::ViewSide::Left) == "behind it to its left");
    CHECK(tb::ViewSideWords(tb::ViewSide::Behind) == "behind it");
    CHECK(tb::ViewSideWords(tb::ViewSide::FrontRight) == "in front of it to its right");
    CHECK(tb::ViewSideWords(tb::ViewSide::FrontLeft) == "in front of it to its left");
    CHECK(tb::ViewSideWords(tb::ViewSide::Front) == "in front of it");
}

namespace
{
// A row of three places with ground to stand over under each, their lines
// clear to so many meters.
tb::ViewRow Row(float right, float left, float straight)
{
    return tb::ViewRow{tb::ViewPlace{right, true}, tb::ViewPlace{left, true}, tb::ViewPlace{straight, true}};
}
// The same row with no ground under any of the three.
tb::ViewRow NoGround(float right, float left, float straight)
{
    return tb::ViewRow{tb::ViewPlace{right, false}, tb::ViewPlace{left, false}, tb::ViewPlace{straight, false}};
}
// Where the camera stands on an ogre's turn, its size asking 10 m, with the
// player's zoom close in (the game's own distance is 3 m then): the three
// lines behind it are clear to so many meters, with ground under each place.
tb::ViewStand OgreStand(float right, float left, float behind)
{
    const tb::ViewStand stand = tb::ChooseStand(1.0f, 10.0f, Row(right, left, behind), std::nullopt);
    CHECK(stand.answer == tb::ViewAnswer::Stand);
    return stand;
}
// A stand's close distance, with the game's own as the cases above read it.
float CloseOf(const tb::ViewStand &stand)
{
    return stand.close.value_or(kGamesOwn);
}
} // namespace

TEST_CASE("the camera stands on the first side whose line is clear the whole distance its size asks", "[portraits]")
{
    using Catch::Approx;
    // All clear: its right, and the distance asked, to the last bit.
    const tb::ViewStand all = OgreStand(10.0f, 10.0f, 10.0f);
    CHECK(all.side == tb::ViewSide::Right);
    CHECK(all.meters == Approx(10.0f));
    CHECK(CloseOf(all) == 10.0f);
    // Its right blocked whole, its left clear.
    const tb::ViewStand left = OgreStand(0.0f, 10.0f, 10.0f);
    CHECK(left.side == tb::ViewSide::Left);
    CHECK(left.meters == Approx(10.0f));
    CHECK(CloseOf(left) == 10.0f);
    // A line cut a little short of the distance is not clear.
    CHECK(OgreStand(9.9f, 10.0f, 10.0f).side == tb::ViewSide::Left);
    // Neither side, and behind it clear: behind, now that its line is asked
    // as the sides' are.
    const tb::ViewStand behind = OgreStand(2.1f, 6.9f, 10.0f);
    CHECK(behind.side == tb::ViewSide::Behind);
    CHECK(behind.meters == Approx(10.0f));
    CHECK(CloseOf(behind) == 10.0f);
    // A line said to be clear past the distance is clear, and the camera no
    // further off for it.
    const tb::ViewStand past = OgreStand(12.0f, 0.0f, 0.0f);
    CHECK(past.side == tb::ViewSide::Right);
    CHECK(past.meters == Approx(10.0f));
}

TEST_CASE("with no line clear the whole distance the camera stands where one is clear the furthest, a margin short",
          "[portraits]")
{
    using Catch::Approx;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    // An ogre's turn in a hall (issue 33, 2026-10-05): both sides blocked at
    // 10 m, and 10 m straight behind it the far side of a wall, where the
    // camera went untested and the frame was of the wall. All three short,
    // the longest taken.
    const tb::ViewStand hall = OgreStand(2.1f, 6.9f, 3.0f);
    CHECK(hall.side == tb::ViewSide::Left);
    CHECK(hall.meters == Approx(6.4f));
    CHECK(CloseOf(hall) == Approx(6.4f));
    const tb::ViewStand right = OgreStand(7.0f, 6.9f, 3.0f);
    CHECK(right.side == tb::ViewSide::Right);
    CHECK(right.meters == Approx(6.5f));
    const tb::ViewStand behind = OgreStand(2.1f, 3.0f, 8.0f);
    CHECK(behind.side == tb::ViewSide::Behind);
    CHECK(behind.meters == Approx(7.5f));
    CHECK(tb::kViewMargin > 0.0f);
    // Two alike: its right, then its left.
    CHECK(OgreStand(5.0f, 5.0f, 5.0f).side == tb::ViewSide::Right);
    CHECK(OgreStand(5.0f, 5.0f, 5.0f).meters == Approx(4.5f));
    CHECK(OgreStand(4.0f, 5.0f, 5.0f).side == tb::ViewSide::Left);
    CHECK(OgreStand(5.0f, 4.0f, 5.0f).side == tb::ViewSide::Right);
    // Nothing clear past the game's own distance: never nearer than it, on
    // the side clear the furthest all the same.
    const tb::ViewStand tight = OgreStand(2.1f, 1.0f, 3.0f);
    CHECK(tight.side == tb::ViewSide::Behind);
    CHECK(tight.meters == Approx(3.0f));
    CHECK_FALSE(tight.close);
    const tb::ViewStand margin = OgreStand(3.4f, 1.0f, 0.5f); // 2.9 m with the margin off
    CHECK(margin.side == tb::ViewSide::Right);
    CHECK(margin.meters == Approx(3.0f));
    CHECK_FALSE(margin.close);
    const tb::ViewStand none = OgreStand(0.0f, 0.0f, 0.0f);
    CHECK(none.side == tb::ViewSide::Right);
    CHECK(none.meters == Approx(3.0f));
    CHECK_FALSE(none.close);
    // A distance that is none is nothing clear.
    const tb::ViewStand noNumber = OgreStand(nan, 6.9f, -1.0f);
    CHECK(noNumber.side == tb::ViewSide::Left);
    CHECK(noNumber.meters == Approx(6.4f));
    CHECK(OgreStand(infinity, 6.9f, 3.0f).side == tb::ViewSide::Left);
}

TEST_CASE("a person-sized enemy is looked at from the game's own distance, from the side clear the furthest",
          "[portraits]")
{
    using Catch::Approx;
    const auto stand = [](float right, float left, float behind) {
        return tb::ChooseStand(1.0f, std::nullopt, Row(right, left, behind), std::nullopt);
    };
    const tb::ViewStand all = stand(3.0f, 3.0f, 3.0f);
    CHECK(all.answer == tb::ViewAnswer::Stand);
    CHECK(all.side == tb::ViewSide::Right);
    CHECK(all.meters == Approx(3.0f));
    CHECK_FALSE(all.close);
    CHECK(stand(1.0f, 3.0f, 3.0f).side == tb::ViewSide::Left);
    CHECK(stand(1.0f, 0.5f, 3.0f).side == tb::ViewSide::Behind);
    // None clear that far: the camera is the player's and comes no nearer.
    const tb::ViewStand hemmed = stand(1.0f, 2.0f, 1.5f);
    CHECK(hemmed.side == tb::ViewSide::Left);
    CHECK(hemmed.meters == Approx(3.0f));
    CHECK_FALSE(hemmed.close);
}

TEST_CASE("the stand is by the camera's distance at the player's zoom, and its close distance puts the camera there",
          "[portraits]")
{
    using Catch::Approx;
    // The game's orbit: the zoom's blend of the close distance and 20 m.
    CHECK(tb::OrbitDistance(1.0f, 3.0f) == Approx(3.0f));
    CHECK(tb::OrbitDistance(1.0f, 10.0f) == Approx(10.0f));
    CHECK(tb::OrbitDistance(0.8f, 10.0f) == Approx(12.0f));
    CHECK(tb::OrbitDistance(0.8f, 3.0f) == Approx(6.4f));
    CHECK(tb::OrbitDistance(0.0f, 10.0f) == Approx(tb::kFarGame)); // the tactical view
    // An ogre with the player zoomed out a little: its size asks 12 m from
    // it, and the game's own is 6.4.
    const float whole = tb::OrbitDistance(0.8f, 10.0f);
    const tb::ViewStand all = tb::ChooseStand(0.8f, 10.0f, Row(whole, whole, whole), std::nullopt);
    CHECK(all.side == tb::ViewSide::Right);
    CHECK(all.meters == Approx(12.0f));
    CHECK(CloseOf(all) == 10.0f);
    // 10 m clear is the whole distance close in, and not at this zoom.
    CHECK(tb::ChooseStand(0.8f, 10.0f, Row(10.0f, whole, 0.0f), std::nullopt).side == tb::ViewSide::Left);
    const tb::ViewStand cut = tb::ChooseStand(0.8f, 10.0f, Row(5.0f, 9.0f, 7.0f), std::nullopt);
    CHECK(cut.side == tb::ViewSide::Left);
    CHECK(cut.meters == Approx(8.5f));
    CHECK(CloseOf(cut) == Approx(5.625f));
    CHECK(tb::OrbitDistance(0.8f, CloseOf(cut)) == Approx(8.5f)); // where the game then puts it
    // Nothing clear past the game's own at this zoom.
    const tb::ViewStand tight = tb::ChooseStand(0.8f, 10.0f, Row(5.0f, 6.5f, 6.0f), std::nullopt);
    CHECK(tight.side == tb::ViewSide::Left);
    CHECK(tight.meters == Approx(6.4f));
    CHECK_FALSE(tight.close);
    // A person-sized enemy at this zoom: the game's own is the whole of it.
    const float own = tb::OrbitDistance(0.8f, tb::kCloseGame);
    const tb::ViewStand person = tb::ChooseStand(0.8f, std::nullopt, Row(own, own, own), std::nullopt);
    CHECK(person.side == tb::ViewSide::Right);
    CHECK(person.meters == Approx(6.4f));
    CHECK_FALSE(person.close);
    // Where the camera would stand at a place, which is where the ground
    // under it is asked: the whole distance, a margin short of the cut, never
    // nearer than the game's own.
    CHECK(tb::StandDistance(1.0f, 10.0f, 10.0f) == Approx(10.0f));
    CHECK(tb::StandDistance(1.0f, 10.0f, 12.0f) == Approx(10.0f));
    CHECK(tb::StandDistance(1.0f, 10.0f, 6.9f) == Approx(6.4f));
    CHECK(tb::StandDistance(1.0f, 10.0f, 9.9f) == Approx(9.4f));
    CHECK(tb::StandDistance(1.0f, 10.0f, 1.0f) == Approx(3.0f));
    CHECK(tb::StandDistance(1.0f, 10.0f, std::numeric_limits<float>::quiet_NaN()) == Approx(3.0f));
    CHECK(tb::StandDistance(1.0f, std::nullopt, 3.0f) == Approx(3.0f));
    CHECK(tb::StandDistance(1.0f, std::nullopt, 1.0f) == Approx(3.0f));
    CHECK(tb::StandDistance(0.8f, 10.0f, 9.0f) == Approx(8.5f));
    CHECK(tb::StandDistance(0.8f, 10.0f, 5.0f) == Approx(6.4f));
    // And it is where the choice puts it.
    CHECK(tb::StandDistance(1.0f, 10.0f, 6.9f) == OgreStand(2.1f, 6.9f, 3.0f).meters);
}

TEST_CASE("a place to stand has ground a creature could walk on beneath it, not far under the creature's feet",
          "[portraits]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // The floor the fight is on, a step down, a rise.
    CHECK(tb::IsPlace(tb::GroundUnder{true, true, 0.0f}));
    CHECK(tb::IsPlace(tb::GroundUnder{true, true, 1.2f}));
    CHECK(tb::IsPlace(tb::GroundUnder{true, true, -2.5f})); // above its feet
    CHECK(tb::IsPlace(tb::GroundUnder{true, true, tb::kPlaceDrop}));
    // No ground under it: a pit with no bottom, the inside of solid ground.
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{false, false, 0.0f}));
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{false, true, 0.0f}));
    // Ground nobody could walk on.
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{true, false, 0.0f}));
    // Walkable, and further under the creature's feet than a storey: the
    // floor of somewhere else.
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{true, true, tb::kPlaceDrop + 0.1f}));
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{true, true, 40.0f}));
    CHECK_FALSE(tb::IsPlace(tb::GroundUnder{true, true, nan}));
    CHECK(tb::kPlaceDrop > 2.0f);  // a flight of steps is no drop
    CHECK(tb::kPlaceDrop < 10.0f); // two storeys are
}

TEST_CASE("a side with no place to stand is no place at all, whatever its line says", "[portraits]")
{
    using Catch::Approx;
    const auto row = [](tb::ViewPlace right, tb::ViewPlace left, tb::ViewPlace straight) {
        return tb::ViewRow{right, left, straight};
    };
    // Its right clear the whole way over no ground, its left clear the whole
    // way over ground: its left.
    const tb::ViewStand left =
        tb::ChooseStand(1.0f, 10.0f, row({10.0f, false}, {10.0f, true}, {10.0f, true}), std::nullopt);
    CHECK(left.answer == tb::ViewAnswer::Stand);
    CHECK(left.side == tb::ViewSide::Left);
    CHECK(left.meters == Approx(10.0f));
    // The one clear the whole way has no ground, and the others are short:
    // the furthest of those with ground, a margin short.
    const tb::ViewStand shortOf =
        tb::ChooseStand(1.0f, 10.0f, row({10.0f, false}, {4.0f, true}, {6.9f, true}), std::nullopt);
    CHECK(shortOf.answer == tb::ViewAnswer::Stand);
    CHECK(shortOf.side == tb::ViewSide::Behind);
    CHECK(shortOf.meters == Approx(6.4f));
    // The furthest clear of the short ones has no ground: the next.
    const tb::ViewStand next =
        tb::ChooseStand(1.0f, 10.0f, row({8.0f, false}, {4.0f, true}, {6.9f, false}), std::nullopt);
    CHECK(next.answer == tb::ViewAnswer::Stand);
    CHECK(next.side == tb::ViewSide::Left);
    CHECK(next.meters == Approx(3.5f));
    // One place with ground and nothing clear from it is a place still: the
    // game's own distance.
    const tb::ViewStand only =
        tb::ChooseStand(1.0f, 10.0f, row({10.0f, false}, {10.0f, false}, {0.0f, true}), std::nullopt);
    CHECK(only.answer == tb::ViewAnswer::Stand);
    CHECK(only.side == tb::ViewSide::Behind);
    CHECK(only.meters == Approx(3.0f));
    CHECK_FALSE(only.close);
}

TEST_CASE("with no place behind a creature the camera looks at it from in front, and with none there stays with the "
          "party",
          "[portraits]")
{
    using Catch::Approx;
    // The Broodmother, set into her cave's rock, which has no collision:
    // every line clear to the whole 14 m, and no ground under the three
    // places behind her (issue 33, 2026-10-05).
    const tb::ViewRow rock = NoGround(14.0f, 14.0f, 14.0f);
    // Behind her alone has been measured: the front is to be asked.
    const tb::ViewStand ask = tb::ChooseStand(1.0f, 14.0f, rock, std::nullopt);
    CHECK(ask.answer == tb::ViewAnswer::AskFront);
    CHECK_FALSE(ask.close);
    // In front of her to her right stands: there, the whole distance.
    const tb::ViewStand front = tb::ChooseStand(1.0f, 14.0f, rock, Row(14.0f, 14.0f, 14.0f));
    CHECK(front.answer == tb::ViewAnswer::Stand);
    CHECK(front.side == tb::ViewSide::FrontRight);
    CHECK(front.meters == Approx(14.0f));
    CHECK(CloseOf(front) == 14.0f);
    // In front the rule is the same one. Her front right short and her front
    // left whole: her front left.
    const tb::ViewStand frontLeft = tb::ChooseStand(1.0f, 14.0f, rock, Row(6.0f, 14.0f, 14.0f));
    CHECK(frontLeft.answer == tb::ViewAnswer::Stand);
    CHECK(frontLeft.side == tb::ViewSide::FrontLeft);
    CHECK(frontLeft.meters == Approx(14.0f));
    // Neither side in front, straight in front whole.
    CHECK(tb::ChooseStand(1.0f, 14.0f, rock, Row(6.0f, 9.0f, 14.0f)).side == tb::ViewSide::Front);
    // All three in front short: the furthest, a margin short; two alike, her
    // right, then her left.
    const tb::ViewStand shortOf = tb::ChooseStand(1.0f, 14.0f, rock, Row(6.0f, 9.0f, 7.0f));
    CHECK(shortOf.answer == tb::ViewAnswer::Stand);
    CHECK(shortOf.side == tb::ViewSide::FrontLeft);
    CHECK(shortOf.meters == Approx(8.5f));
    CHECK(CloseOf(shortOf) == Approx(8.5f));
    CHECK(tb::ChooseStand(1.0f, 14.0f, rock, Row(9.0f, 9.0f, 9.0f)).side == tb::ViewSide::FrontRight);
    CHECK(tb::ChooseStand(1.0f, 14.0f, rock, Row(7.0f, 9.0f, 9.0f)).side == tb::ViewSide::FrontLeft);
    // A place in front with no ground is no place either.
    const tb::ViewRow frontRightGone{tb::ViewPlace{14.0f, false}, tb::ViewPlace{14.0f, true},
                                     tb::ViewPlace{14.0f, true}};
    CHECK(tb::ChooseStand(1.0f, 14.0f, rock, frontRightGone).side == tb::ViewSide::FrontLeft);
    // No ground in front either: the camera stays with the party.
    const tb::ViewStand party = tb::ChooseStand(1.0f, 14.0f, rock, NoGround(14.0f, 14.0f, 14.0f));
    CHECK(party.answer == tb::ViewAnswer::WithParty);
    CHECK_FALSE(party.close);
    // A place behind, however short, is taken before any in front: a view
    // from behind that is shortened is still a view from behind.
    const tb::ViewRow hemmed{tb::ViewPlace{14.0f, false}, tb::ViewPlace{1.0f, true}, tb::ViewPlace{14.0f, false}};
    const tb::ViewStand back = tb::ChooseStand(1.0f, 14.0f, hemmed, Row(14.0f, 14.0f, 14.0f));
    CHECK(back.answer == tb::ViewAnswer::Stand);
    CHECK(back.side == tb::ViewSide::Left);
    CHECK(back.meters == Approx(3.0f));
    // And with a place behind the front is never asked for.
    CHECK(tb::ChooseStand(1.0f, 14.0f, hemmed, std::nullopt).answer == tb::ViewAnswer::Stand);
    // Behind as today whenever a place behind stands, whatever is in front.
    const tb::ViewStand today = tb::ChooseStand(1.0f, 10.0f, Row(2.1f, 6.9f, 3.0f), Row(10.0f, 10.0f, 10.0f));
    CHECK(today.side == tb::ViewSide::Left);
    CHECK(today.meters == Approx(6.4f));
}

TEST_CASE("staying with the party, the camera turns to look from the player's own toward the creature", "[portraits]")
{
    using Catch::Approx;
    const float halfTurn = 3.14159265f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // The camera's yaw is the direction from what it looks at to itself: to
    // look from the player's own toward a creature it stands on the far side
    // of the player's own.
    const tb::Vec3 own{10.0f, 10.0f, 0.0f};
    REQUIRE(tb::YawToward(own, tb::Vec3{20.0f, 10.0f, 3.0f})); // the creature at +x: the camera at -x
    CHECK(std::cos(*tb::YawToward(own, tb::Vec3{20.0f, 10.0f, 3.0f})) == Approx(-1.0f));
    REQUIRE(tb::YawToward(own, tb::Vec3{0.0f, 10.0f, 0.0f})); // at -x: the camera at +x
    CHECK(*tb::YawToward(own, tb::Vec3{0.0f, 10.0f, 0.0f}) == Approx(0.0f).margin(1e-6));
    REQUIRE(tb::YawToward(own, tb::Vec3{10.0f, 20.0f, 0.0f})); // at +y: the camera at -y
    CHECK(*tb::YawToward(own, tb::Vec3{10.0f, 20.0f, 0.0f}) == Approx(-halfTurn / 2.0f));
    REQUIRE(tb::YawToward(own, tb::Vec3{10.0f, 0.0f, 0.0f})); // at -y: the camera at +y
    CHECK(*tb::YawToward(own, tb::Vec3{10.0f, 0.0f, 0.0f}) == Approx(halfTurn / 2.0f));
    // The two in one place over the ground, or a point that is no number:
    // the camera is left as it is turned.
    CHECK_FALSE(tb::YawToward(own, tb::Vec3{10.0f, 10.0f, 5.0f}));
    CHECK_FALSE(tb::YawToward(own, tb::Vec3{nan, 10.0f, 0.0f}));
    CHECK_FALSE(tb::YawToward(tb::Vec3{10.0f, nan, 0.0f}, own));
}

TEST_CASE("how far a line is clear is the share of the camera's distance that the line runs before it is cut",
          "[portraits]")
{
    using Catch::Approx;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    // From a head 2 m up to where the camera would stand, 10 m from the
    // feet as the orbit has it; the line itself is some 8.9 m.
    const tb::Vec3 head{0.0f, 0.0f, 2.0f};
    const tb::Vec3 eye{8.0f, 0.0f, 6.0f};
    CHECK(tb::ClearDistance(head, eye, tb::Vec3{4.0f, 0.0f, 4.0f}, 10.0f) == Approx(5.0f)); // cut half way
    CHECK(tb::ClearDistance(head, eye, tb::Vec3{2.0f, 0.0f, 3.0f}, 10.0f) == Approx(2.5f));
    CHECK(tb::ClearDistance(head, eye, head, 10.0f) == 0.0f);         // at the creature
    CHECK(tb::ClearDistance(head, eye, eye, 10.0f) == Approx(10.0f)); // at the camera
    // Past the camera, which is not the engine's to answer: no more than all.
    CHECK(tb::ClearDistance(head, eye, tb::Vec3{16.0f, 0.0f, 10.0f}, 10.0f) == Approx(10.0f));
    // A line with no length, a point that is not a number: nothing clear.
    CHECK(tb::ClearDistance(head, head, head, 10.0f) == 0.0f);
    CHECK(tb::ClearDistance(head, eye, tb::Vec3{nan, 0.0f, 4.0f}, 10.0f) == 0.0f);
    CHECK(tb::ClearDistance(head, tb::Vec3{infinity, 0.0f, 6.0f}, tb::Vec3{4.0f, 0.0f, 4.0f}, 10.0f) == 0.0f);
}
