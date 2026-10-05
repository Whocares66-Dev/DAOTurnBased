// Haste and slows in a turn's meters (issue 49), driven through scripted
// fights in the stand-in for the game (World.h): the pace a turn's meters are
// made from is its creature's build times its movement rate effects as they
// are when the turn begins, with either clock; an effect put on or ended in
// the middle of a fight counts from its creature's next turn; and the log
// says what was read, and what it came to.
//
// The game's own figures, from its scripts (read 2026-10-05): a Swift Salve
// 1.2 and Haste 1.3, each an EFFECT_TYPE_MOVEMENT_RATE; a caltrop trap 0.6
// and grease 0.5, each a _DEBUFF. In play until then none of them moved a
// creature's meters: the game layer read each one's potency where the engine
// does not keep it (src/game/Bridge.cpp, EngineParameters).
#include "World.h"

#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>

using namespace tb;
using namespace tb::test;
using Catch::Approx;

namespace
{

constexpr float kSalve = 1.2f;
constexpr float kHaste = 1.3f;
constexpr float kCaltrops = 0.6f;
constexpr float kGrease = 0.5f;

// 4 s turns, a pace of 5 m/s for round numbers, a meter free.
Settings Timed()
{
    Settings s = Fixed();
    s.turnClock = TurnClock::Time;
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

// The fight on to the beginning of `id`'s next turn: the player's turns
// ended as they come, and every AI but `id` passing as it is told to choose
// (`id`'s own turn, an AI's, then stands while its AI is waited on).
void UntilTurnOf(World &w, ObjectId id)
{
    w.onPoke = [&w, id](ObjectId who) {
        if (who != id)
        {
            (void)w.Order(who, CommandType::Wait, Source::Script);
        }
    };
    const auto endIfWaiting = [&w] {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
    };
    for (int i = 0; i < 4000 && w.Current() == id; ++i)
    {
        endIfWaiting();
        w.Frame();
    }
    for (int i = 0; i < 4000 && w.Current() != id; ++i)
    {
        endIfWaiting();
        w.Frame();
    }
    REQUIRE(w.Current() == id);
    // Its turn begun, not only come.
    for (int i = 0;
         i < 100 && (w.machine.GetPhase() == Phase::TurnStart || w.machine.GetPhase() == Phase::RoundPadding); ++i)
    {
        w.Frame();
    }
}

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

bool Logged(const World &w, std::string_view text)
{
    return Lines(w, text) > 0;
}

// What a turn's bar would walk as it stands: its time at the pace, and the
// free meter.
float BarMeters(const TurnView &view, float pace, float freeMeters)
{
    return static_cast<float>(view.timeLeftMs) / 1000.0f * pace + freeMeters;
}

} // namespace

TEST_CASE("with time, a turn's bar walks at its creature's pace as the turn begins: Haste, a salve, a slow", "[pace]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // As she is: a person's pace, and nothing said of it at info.
    CHECK(w.machine.View().paceMps == 5.0f);
    CHECK(w.machine.View().movementLeft == 21.0f); // 4 s at 5 m/s, and the free meter
    CHECK_FALSE(Logged(w, "a person's pace this turn"));

    // She drinks a Swift Salve in the middle of the fight. Her next turn's
    // meters are made at 1.2 her pace.
    w.Get(kHero).movement.rates = {kSalve};
    UntilTurnOf(w, kHero);
    TurnView view = w.machine.View();
    CHECK(view.paceMps == Approx(6.0f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 6.0f, 1.0f)));
    CHECK(view.movementLeft > 21.0f);

    // A mage's Haste beside it: 1.2 times 1.3.
    w.Get(kHero).movement.rates = {kSalve, kHaste};
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(7.8f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 7.8f, 1.0f)));

    // A walk is charged at that pace: 7.8 m after the free meter is a second.
    const std::uint32_t before = view.timeLeftMs;
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 8.8f) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 88; ++i)
    {
        w.Get(kHero).position.x += 0.1f;
        w.Frame();
    }
    w.Get(kHero).moving = false;
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    CHECK(before - w.machine.View().timeLeftMs >= 995);
    CHECK(before - w.machine.View().timeLeftMs <= 1005);

    // Into a caltrop trap, the salve and Haste still on: 1.56 times 0.6.
    w.Get(kHero).movement.debuffs = {kCaltrops};
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(5.0f * 1.56f * 0.6f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 5.0f * 1.56f * 0.6f, 1.0f)));

    // The salve and Haste end, the trap's slow stays: slower than she is.
    w.Get(kHero).movement.rates.clear();
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(3.0f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 3.0f, 1.0f)));
    CHECK(view.movementLeft < 21.0f);

    // Out of the trap: her own pace again.
    w.Get(kHero).movement.debuffs.clear();
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == 5.0f);
    CHECK(view.movementLeft == Approx(BarMeters(view, 5.0f, 1.0f)));
}

TEST_CASE("as built, a turn's meters are its creature's pace as the turn begins: Haste, a salve, a slow", "[pace]")
{
    World w; // one action and 9 m a turn for a person
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().movementMax == 9.0f);

    w.Get(kHero).movement.rates = {kSalve};
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().movementMax == Approx(10.8f));
    CHECK(w.machine.View().movementLeft == Approx(10.8f));

    w.Get(kHero).movement.rates = {kSalve, kHaste};
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().movementMax == Approx(14.04f));

    // She may walk that far and no further.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    const float from = w.Get(kHero).position.x;
    for (int i = 0; i < 400 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 0.1f;
        w.Frame();
    }
    CHECK(w.Said(L"Out of movement"));
    CHECK(w.Get(kHero).position.x - from > 13.9f);
    CHECK(w.Get(kHero).position.x - from < 14.04f + TurnMachine::kOvershoot + 0.3f);

    // Grease alone: half her meters.
    w.Get(kHero).movement.rates.clear();
    w.Get(kHero).movement.debuffs = {kGrease};
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().movementMax == Approx(4.5f));

    // And as she was when it ends.
    w.Get(kHero).movement.debuffs.clear();
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().movementMax == 9.0f);
}

TEST_CASE("an effect put on or ended within a turn counts from its creature's next turn", "[pace]")
{
    // The plain rule: one pace a turn, read as the turn begins. The scan
    // that follows an action within the turn reads the new effect, and the
    // turn's meters stay as they were made.
    for (const bool timed : {false, true})
    {
        CAPTURE(timed);
        World w(timed ? Timed() : Fixed());
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        const TurnView began = w.machine.View();

        // She drinks the salve: an action of her turn, and the effect is on
        // her as it ends.
        const int scans = w.scans;
        CHECK(w.Order(kHero, CommandType::UseItem, Source::Player, std::nullopt, 1.0f) == Verdict::Allow);
        w.RunFor(300);
        w.Get(kHero).movement.rates = {kSalve};
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.scans > scans); // the machine has looked since
        const TurnView after = w.machine.View();
        CHECK(after.movementMax == began.movementMax - (timed ? 5.0f : 0.0f)); // less only the second she drank for
        if (timed)
        {
            CHECK(after.paceMps == 5.0f);
        }
        CHECK_FALSE(Logged(w, "a person's pace this turn"));

        // Her next turn has it.
        UntilTurnOf(w, kHero);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == Approx(6.0f));
        }
        else
        {
            CHECK(w.machine.View().movementMax == Approx(10.8f));
        }
        CHECK(Lines(w, "Hero moves at 1.20 a person's pace this turn") == 1);

        // It runs out in the middle of that turn: the turn keeps its pace.
        const TurnView hasted = w.machine.View();
        w.Get(kHero).movement.rates.clear();
        CHECK(w.Order(kHero, CommandType::UseItem, Source::Player, std::nullopt, 1.0f) == Verdict::Allow);
        w.RunFor(300);
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == Approx(6.0f));
        }
        else
        {
            CHECK(w.machine.View().movementMax == hasted.movementMax);
        }
        // And the one after is her own again.
        UntilTurnOf(w, kHero);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == 5.0f);
        }
        else
        {
            CHECK(w.machine.View().movementMax == 9.0f);
        }
    }
}

TEST_CASE("an enemy slowed in the middle of a fight has fewer meters from its next turn, and its build still counts",
          "[pace]")
{
    for (const bool timed : {false, true})
    {
        CAPTURE(timed);
        // The genlock a shriek for its pace: 1.5 a person's by its build.
        std::vector<Creature> fight = StandardFight();
        fight[1].speed = 1.5f;
        World w(timed ? Timed() : Fixed(), fight);
        w.StartFight();
        UntilTurnOf(w, kGenlock);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == Approx(7.5f));
        }
        else
        {
            CHECK(w.machine.View().movementMax == Approx(13.5f));
        }
        // Its build alone is not said as a pace of this turn's: it is what
        // it always moves at.
        CHECK_FALSE(Logged(w, "Genlock moves at"));

        // It walks onto the hero's caltrop trap, and stays in it.
        w.Get(kGenlock).movement.debuffs = {kCaltrops};
        UntilTurnOf(w, kGenlock);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == Approx(4.5f)); // 5 m/s, 1.5 its build, 0.6 the trap
        }
        else
        {
            CHECK(w.machine.View().movementMax == Approx(8.1f));
        }
        CHECK(Logged(w, "Genlock moves at 0.90 a person's pace this turn"));
        CHECK(Logged(w, "1.50 from its build, 0.60 from haste or slows (1.00 at its last turn)"));

        // The hero, whom nothing slows, is as she was all along.
        UntilTurnOf(w, kHero);
        if (timed)
        {
            CHECK(w.machine.View().paceMps == 5.0f);
        }
        else
        {
            CHECK(w.machine.View().movementMax == 9.0f);
        }
        CHECK_FALSE(Logged(w, "Hero moves at"));
    }
}

TEST_CASE("one on its way out of the fight has its turn at the pace it has then", "[pace]")
{
    // Not found fighting by the scan, and neither it nor the party
    // perceiving the other: it is out of the fight kAbsentMs on (issue 29),
    // and until then the machine keeps what it knew of it. Not its pace: a
    // turn of its that comes before it is gone is made like anyone's, of the
    // Haste and slows the scan read on it (the random fights with strays,
    // seed 16, 2026-10-05).
    for (const bool timed : {false, true})
    {
        CAPTURE(timed);
        World w(timed ? Timed() : Fixed());
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        Creature &genlock = w.Get(kGenlock);
        genlock.fighting = false;
        genlock.perception = Unperceived();
        genlock.movement.debuffs = {kCaltrops};
        w.onPoke = [](ObjectId) {}; // its AI is waited on: the turn stands
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
        REQUIRE(w.Current() == kGenlock);
        REQUIRE(Logged(w, "Genlock's turn (enemy, AI)"));
        if (timed)
        {
            CHECK(w.machine.View().paceMps == Approx(3.0f));
        }
        else
        {
            CHECK(w.machine.View().movementMax == Approx(5.4f));
        }
        CHECK(Logged(w, "Genlock moves at 0.60 a person's pace this turn"));
    }
}

TEST_CASE("under [movement] rule = constant, as built, no pace counts, and the log says so", "[pace]")
{
    Settings s = Fixed();
    s.movementRule = MovementRule::Constant;
    World w(s);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().movementMax == 9.0f);
    w.Get(kHero).movement.rates = {kHaste};
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().movementMax == 9.0f);
    CHECK(Logged(w, "Hero moves at 1.30 a person's pace this turn (9.0 m to move all the same: [movement] rule = "
                    "constant)"));
}

TEST_CASE("a turn's pace is said once as it begins while Haste or a slow changes it, and once more as they end",
          "[pace]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Every turn's start says what was read of its creature, at debug: here
    // nothing, of either type.
    CHECK(Lines(w, "Hero's movement rate effects as its turn begins: none of type 9 (EFFECT_TYPE_MOVEMENT_RATE), "
                   "none of type 11 (its _DEBUFF): 1.00 of its build's pace") == 1);
    CHECK(Lines(w, "moves at") == 0);

    // Haste on: said as her next turn begins, with what it was before.
    w.Get(kHero).movement.rates = {kHaste};
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "Hero's movement rate effects as its turn begins: 1 (1.30) of type 9 (EFFECT_TYPE_MOVEMENT_RATE), "
                   "none of type 11 (its _DEBUFF): 1.30 of its build's pace") == 1);
    CHECK(Lines(w, "Hero moves at 1.30 a person's pace this turn (6.50 m a second of its bar): 1.00 from its build, "
                   "1.30 from haste or slows (1.00 at its last turn)") == 1);
    CHECK(Lines(w, "moves at") == 1);

    // Still on: said again, once, and not as a change.
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "moves at") == 2);
    CHECK(Lines(w, "at its last turn") == 1);
    CHECK(Lines(w, "1.00 from its build, 1.30 from haste or slows") == 2);

    // A salve too, and a trap: each kind's count and potencies are said.
    w.Get(kHero).movement.rates = {kHaste, kSalve};
    w.Get(kHero).movement.debuffs = {kCaltrops};
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "Hero's movement rate effects as its turn begins: 2 (1.30, 1.20) of type 9 "
                   "(EFFECT_TYPE_MOVEMENT_RATE), 1 (0.60) of type 11 (its _DEBUFF): 0.94 of its build's pace") == 1);
    CHECK(Lines(w, "Hero moves at 0.94 a person's pace this turn (4.68 m a second of its bar): 1.00 from its build, "
                   "0.94 from haste or slows (1.30 at its last turn)") == 1);

    // All of it ends: said once more, that she is back at her own pace.
    w.Get(kHero).movement = {};
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "Hero moves at 1.00 a person's pace this turn (5.00 m a second of its bar): 1.00 from its build, "
                   "1.00 from haste or slows (0.94 at its last turn)") == 1);
    const int said = Lines(w, "moves at");
    CHECK(said == 4);

    // And then no more, turn after turn; the debug line goes on.
    const int read = Lines(w, "Hero's movement rate effects as its turn begins");
    UntilTurnOf(w, kHero);
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "moves at") == said);
    CHECK(Lines(w, "Hero's movement rate effects as its turn begins") == read + 2);

    // Nobody else's turn said a pace: nothing was on them.
    CHECK(Lines(w, "Genlock moves at") + Lines(w, "Alistair moves at") + Lines(w, "Hurlock moves at") == 0);
    CHECK(Logged(w, "Genlock's movement rate effects as its turn begins: none of type 9"));
}

TEST_CASE("an effect whose potency is no rate is said, and left out of the pace", "[pace]")
{
    // What the game layer read where the engine keeps no potency, as it did
    // of every movement rate effect until 2026-10-05 (issue 49): the effect
    // is there, and its potency reads 0. The log shows it; the pace is the
    // creature's own.
    World w(Timed());
    w.Get(kHero).movement.rates = {0.0f};
    w.Get(kHero).movement.debuffs = {0.0f, kGrease};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().paceMps == Approx(2.5f)); // the grease alone
    CHECK(Logged(w, "Hero's movement rate effects as its turn begins: 1 (0.00 is no rate: left out) of type 9 "
                    "(EFFECT_TYPE_MOVEMENT_RATE), 2 (0.00 is no rate: left out, 0.50) of type 11 (its _DEBUFF): 0.50 "
                    "of its build's pace"));
}

TEST_CASE("what a fight said of a creature's pace is not the next fight's", "[pace]")
{
    World w(Timed());
    w.Get(kHero).movement.rates = {kHaste};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Hasted as the fight begins: said at her first turn, against her own
    // pace, which is all the fight knows of before.
    CHECK(Lines(w, "1.30 from haste or slows (1.00 at its last turn)") == 1);

    // The fight ends with Haste on her, and Haste ends after it.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    (void)w.machine.OnEndTurn();
    w.RunFor(1000);
    REQUIRE_FALSE(w.machine.Active());
    w.mode = Mode::Explore;
    w.Frame();
    w.Get(kHero).movement = {};
    w.Get(kGenlock).down = false;
    w.Get(kHurlock).down = false;
    w.log.clear();

    // The next fight finds her at her own pace, and has nothing to say of
    // it: not that she was hasted at a turn of the last one's.
    w.StartFight();
    REQUIRE(w.machine.Active());
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().paceMps == 5.0f);
    CHECK(Lines(w, "moves at") == 0);
}
