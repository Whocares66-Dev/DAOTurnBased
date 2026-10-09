// Haste and slows in a turn's meters (issue 49), driven through scripted
// fights in the stand-in for the game (World.h): the pace a turn's meters are
// made from is its creature's build times its movement rate effects. It is
// the pace as it is now: as the turn begins, and from each look at everyone
// in it, so that what is left to walk is the time left times the pace now.
// And the log says what was read, and what it came to.
//
// The game's own figures, from its scripts (read 2026-10-05): a Swift Salve
// 1.2 and Haste 1.3, each an EFFECT_TYPE_MOVEMENT_RATE; a caltrop trap 0.6
// and grease 0.5, each a _DEBUFF. In play until then none of them moved a
// creature's meters: the game layer read each one's potency where the engine
// does not keep it (src/game/Bridge.cpp, EngineParameters).
#include "World.h"

#include "core/Preview.h"
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
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

void EndIfWaiting(World &w)
{
    if (w.machine.GetPhase() == Phase::AwaitingOrder)
    {
        (void)w.machine.OnEndTurn();
    }
}

// Every AI but `id` passes as it is told to choose (`id`'s own turn, an
// AI's, then stands while its AI is waited on).
void OthersPass(World &w, ObjectId id)
{
    w.onPoke = [&w, id](ObjectId who) {
        if (who != id)
        {
            (void)w.Order(who, CommandType::Wait, Source::Script);
        }
    };
}

// The fight on until it is somebody else's turn: what is then put on `id` or
// taken off it is found as its next turn begins, and in no turn of its own.
void UntilNotTurnOf(World &w, ObjectId id)
{
    OthersPass(w, id);
    for (int i = 0; i < 4000 && w.Current() == id; ++i)
    {
        EndIfWaiting(w);
        w.Frame();
    }
    REQUIRE(w.Current() != id);
}

// The fight on to the beginning of `id`'s next turn: the player's turns
// ended as they come.
void UntilTurnOf(World &w, ObjectId id)
{
    OthersPass(w, id);
    for (int i = 0; i < 4000 && w.Current() == id; ++i)
    {
        EndIfWaiting(w);
        w.Frame();
    }
    for (int i = 0; i < 4000 && w.Current() != id; ++i)
    {
        EndIfWaiting(w);
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

// Where in the log a line with this text first is; past its end with none.
std::size_t At(const World &w, std::string_view text)
{
    const auto found =
        std::ranges::find_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
    return static_cast<std::size_t>(found - w.log.begin());
}

// What a turn's bar would walk as it stands: its time at the pace, and the
// free meter.
float BarMeters(const TurnView &view, float pace, float freeMeters)
{
    return (static_cast<float>(view.timeLeftMs) / 1000.0f * pace) + freeMeters;
}

// A creature under its command walks a tenth of a meter a frame, until it has
// gone `meters` or the machine has stopped it. Answers how far it went.
float WalkOn(World &w, ObjectId id, float meters)
{
    const float from = w.Get(id).position.x;
    w.Get(id).moving = true;
    while (w.Get(id).moving && w.Get(id).position.x - from < meters - 0.001f)
    {
        w.Get(id).position.x += 0.1f;
        w.Frame();
    }
    return w.Get(id).position.x - from;
}

// Its command ends, and the world settles.
void Done(World &w, ObjectId id)
{
    w.Get(id).moving = false;
    w.Finish(id);
    w.RunFor(600);
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
    CHECK_FALSE(Logged(w, "a person's pace"));

    // A Swift Salve is put on her between two of her turns. Her next turn's
    // meters are made at 1.2 her pace.
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.rates = {kSalve};
    UntilTurnOf(w, kHero);
    TurnView view = w.machine.View();
    CHECK(view.paceMps == Approx(6.0f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 6.0f, 1.0f)));
    CHECK(view.movementLeft > 21.0f);

    // A mage's Haste beside it: 1.2 times 1.3.
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.rates = {kSalve, kHaste};
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(7.8f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 7.8f, 1.0f)));

    // A walk is charged at that pace: 7.8 m after the free meter is a second.
    const std::uint32_t before = view.timeLeftMs;
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 8.8f) == Verdict::Allow);
    w.Frame();
    CHECK(WalkOn(w, kHero, 8.8f) == Approx(8.8f).margin(0.01f));
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(before - w.machine.View().timeLeftMs >= 995);
    CHECK(before - w.machine.View().timeLeftMs <= 1005);

    // Into a caltrop trap, the salve and Haste still on: 1.56 times 0.6.
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.debuffs = {kCaltrops};
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(5.0f * 1.56f * 0.6f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 5.0f * 1.56f * 0.6f, 1.0f)));

    // The salve and Haste end, the trap's slow stays: slower than she is.
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.rates.clear();
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == Approx(3.0f));
    CHECK(view.movementLeft == Approx(BarMeters(view, 3.0f, 1.0f)));
    CHECK(view.movementLeft < 21.0f);

    // Out of the trap: her own pace again.
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.debuffs.clear();
    UntilTurnOf(w, kHero);
    view = w.machine.View();
    CHECK(view.paceMps == 5.0f);
    CHECK(view.movementLeft == Approx(BarMeters(view, 5.0f, 1.0f)));
    // And nothing of it was a change within a turn.
    CHECK_FALSE(Logged(w, "from now in this turn"));
}

TEST_CASE("with time, a salve drunk in a turn lengthens what is left of that turn's walk, not what was charged",
          "[pace]")
{
    // The user, 2026-10-05, of reading the pace again within the turn: "I
    // think that would be more accurate". What is left to walk is the time
    // left times the pace now.
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // She walks 6 m: the free meter, and a second of her bar at 5 m/s.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 6.0f) == Verdict::Allow);
    w.Frame();
    (void)WalkOn(w, kHero, 6.0f);
    Done(w, kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    TurnView view = w.machine.View();
    REQUIRE(view.timeLeftMs >= 2995);
    REQUIRE(view.timeLeftMs <= 3005);
    CHECK(view.movementLeft == Approx(15.0f).margin(0.03f));
    // 16 m is past what she has, and refused.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 16.0f) == Verdict::Block);

    // She drinks a Swift Salve, a second of her bar: the effect is on her as
    // the drink ends, and the look taken then finds it.
    CHECK(w.Order(kHero, CommandType::UseItem, Source::Player, std::nullopt, 1.0f) == Verdict::Allow);
    const std::uint32_t afterDrink = w.machine.View().timeLeftMs;
    CHECK(view.timeLeftMs - afterDrink == 1000);
    w.RunFor(300);
    w.Get(kHero).movement.rates = {kSalve};
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    view = w.machine.View();
    // The bar walks at 6 m/s from here. Nothing of what was spent came back:
    // the second walked at 5 m/s stays a second, and the drink its own.
    CHECK(view.paceMps == Approx(6.0f));
    CHECK(view.timeLeftMs == afterDrink);
    // (Less the last few millimeters of her walk, not yet a millisecond's
    // worth and so not yet paid.)
    CHECK(view.movementLeft == Approx(static_cast<float>(afterDrink) / 1000.0f * 6.0f).margin(0.01f));
    CHECK(view.movementLeft > 11.9f); // 2 s at 6 m/s, where 2 s at 5 m/s was 10 m
    CHECK(Lines(w, "Hero moves at 1.20 a person's pace from now in this turn (6.00 m a second of its bar; 1.00 from "
                   "its build, 1.20 from haste or slows): it was 1.00, and it has ") == 1);
    CHECK(Lines(w, "Hero's movement rate effects as its pace changes in its turn: 1 (1.20) of type 9 "
                   "(EFFECT_TYPE_MOVEMENT_RATE), none of type 11 (its _DEBUFF): 1.20 of its build's pace") == 1);
    // What an order would cost is of the same pace: the preview's 6 m is a
    // second now.
    CHECK(CostOf(view, 6.0f, 0.0f).seconds == Approx(1.0f));
    // And so is "Too far": 11 m, which the 10 m of a moment ago would have
    // refused, is let through, and its walk is charged at the new pace.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 11.0f) == Verdict::Allow);
    w.Frame();
    (void)WalkOn(w, kHero, 6.0f);
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(afterDrink - w.machine.View().timeLeftMs >= 995);
    CHECK(afterDrink - w.machine.View().timeLeftMs <= 1005);
    // Said once, however many looks follow.
    w.RunFor(2000);
    CHECK(Lines(w, "from now in this turn") == 1);

    // Her next turn begins at that pace, and its line does not say again
    // that it changed.
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().paceMps == Approx(6.0f));
    CHECK(Lines(w, "Hero moves at 1.20 a person's pace this turn (6.00 m a second of its bar): 1.00 from its build, "
                   "1.20 from haste or slows") == 1);
    CHECK(Lines(w, "at its last turn") == 0);

    // The salve runs out in the middle of that turn: back at her own pace
    // from the look that finds it gone, said then, and not again as her next
    // turn begins.
    CHECK(w.Order(kHero, CommandType::UseItem, Source::Player, std::nullopt, 1.0f) == Verdict::Allow);
    w.RunFor(300);
    w.Get(kHero).movement.rates.clear();
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    view = w.machine.View();
    CHECK(view.paceMps == 5.0f);
    CHECK(view.movementLeft == Approx((static_cast<float>(view.timeLeftMs) / 1000.0f * 5.0f) + 1.0f).margin(0.01f));
    CHECK(Lines(w, "Hero moves at 1.00 a person's pace from now in this turn (5.00 m a second of its bar; 1.00 from "
                   "its build, 1.00 from haste or slows): it was 1.20, and it has ") == 1);
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().paceMps == 5.0f);
    CHECK(Lines(w, "a person's pace this turn") == 1);
    CHECK(Lines(w, "from now in this turn") == 2);
}

TEST_CASE("with time, Haste another turns on takes in a turn under way at the next look, with no action of its own",
          "[pace]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().movementLeft == 21.0f);
    // A mage's Haste comes over the party while she waits for her order.
    // Everyone is looked at every half second.
    w.Get(kHero).movement.rates = {kHaste};
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().paceMps == Approx(6.5f));
    CHECK(w.machine.View().movementLeft == Approx(27.0f)); // 4 s at 6.5 m/s, and the free meter
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(Lines(w, "Hero moves at 1.30 a person's pace from now in this turn (6.50 m a second of its bar; 1.00 from "
                   "its build, 1.30 from haste or slows): it was 1.00, and it has 4.00 s and 27.0 m left") == 1);
    // One line as it changes, not one a look.
    w.RunFor(3000);
    CHECK(Lines(w, "from now in this turn") == 1);
    // Whoever is not at its turn has no such line: its next turn's has it.
    CHECK(Lines(w, "moves at") == 1);
    w.Get(kCompanion).movement.rates = {kHaste};
    w.RunFor(1200);
    CHECK(Lines(w, "moves at") == 1);
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.View().paceMps == Approx(6.5f));
    CHECK(Lines(w, "Alistair moves at 1.30 a person's pace this turn (6.50 m a second of its bar): 1.00 from its "
                   "build, 1.30 from haste or slows (1.00 at its last turn)") == 1);
}

TEST_CASE("with time, a slow that lands in the middle of a walk is paid from the look that finds it, and the walk "
          "is cut where its time runs out",
          "[pace]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // 18 m of her 21: let through.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 18.0f) == Verdict::Allow);
    w.Frame();
    const float from = w.Get(kHero).position.x;
    CHECK(WalkOn(w, kHero, 6.0f) == Approx(6.0f).margin(0.01f));
    REQUIRE(w.machine.View().paceMps == 5.0f);
    // She walks into caltrops. The engine slows her at once; the machine
    // finds the slow with its next look at everyone, half a second on at the
    // latest, and what she walked until then was charged at the pace till
    // then.
    w.Get(kHero).movement.debuffs = {kCaltrops};
    int frames = 0;
    while (w.machine.View().paceMps == 5.0f && frames < 40)
    {
        w.Get(kHero).position.x += 0.1f;
        w.Frame();
        ++frames;
    }
    CHECK(frames <= 32); // half a second of frames
    const TurnView slowed = w.machine.View();
    REQUIRE(slowed.paceMps == Approx(3.0f));
    CHECK(Lines(w, "Hero moves at 0.60 a person's pace from now in this turn (3.00 m a second of its bar; 1.00 from "
                   "its build, 0.60 from haste or slows): it was 1.00, and it has ") == 1);
    // What is left is the time left at the new pace: the 18 m that fitted no
    // longer do, and the walk goes on until its time runs out.
    const float at = w.Get(kHero).position.x - from;
    CHECK(slowed.movementLeft == Approx(static_cast<float>(slowed.timeLeftMs) / 1000.0f * 3.0f).margin(0.2f));
    CHECK(at + slowed.movementLeft < 18.0f);
    const float more = WalkOn(w, kHero, 18.0f - at);
    CHECK(w.Said(L"Out of movement"));
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(more == Approx(slowed.movementLeft).margin(TurnMachine::kOvershoot + 0.25f));
    CHECK(at + more < 17.0f);
    CHECK(w.machine.View().timeLeftMs == 0);
}

TEST_CASE("with time, an AI slowed in its own action is told to choose again with the meters of its pace now", "[pace]")
{
    World w(Timed());
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    REQUIRE(w.machine.View().movementLeft == 21.0f);
    // Its swing, a second and a half; grease lands on it as it swings.
    REQUIRE(w.Order(kGenlock, CommandType::Attack, Source::Script, 0.0f, 1.5f) == Verdict::Allow);
    w.RunFor(300);
    w.Get(kGenlock).movement.debuffs = {kCaltrops};
    w.Finish(kGenlock);
    w.RunFor(1500);
    // The look taken as its action ended found the slow, before it was told
    // to choose again: 2.5 s at 3 m/s, and the free meter.
    REQUIRE(Logged(w, "Genlock chooses again: 2.50 s and 8.5 m left"));
    REQUIRE(Logged(w, "Genlock moves at 0.60 a person's pace from now in this turn"));
    CHECK(At(w, "Genlock moves at 0.60 a person's pace from now in this turn") <
          At(w, "Genlock chooses again: 2.50 s and 8.5 m left"));
}

TEST_CASE("an enemy slowed in the middle of a fight has fewer meters from its next turn, and its build still counts",
          "[pace]")
{
    // The genlock a shriek for its pace: 1.5 a person's by its build.
    std::vector<Creature> fight = StandardFight();
    fight[1].speed = 1.5f;
    World w(Timed(), fight);
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    CHECK(w.machine.View().paceMps == Approx(7.5f));
    // Its build alone is not said as a pace of this turn's: it is what it
    // always moves at.
    CHECK_FALSE(Logged(w, "Genlock moves at"));

    // It walks onto the hero's caltrop trap in her turn, and stays in it.
    UntilNotTurnOf(w, kGenlock);
    w.Get(kGenlock).movement.debuffs = {kCaltrops};
    UntilTurnOf(w, kGenlock);
    CHECK(w.machine.View().paceMps == Approx(4.5f)); // 5 m/s, 1.5 its build, 0.6 the trap
    CHECK(Logged(w, "Genlock moves at 0.90 a person's pace this turn"));
    CHECK(Logged(w, "1.50 from its build, 0.60 from haste or slows (1.00 at its last turn)"));

    // The hero, whom nothing slows, is as she was all along.
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().paceMps == 5.0f);
    CHECK_FALSE(Logged(w, "Hero moves at"));
}

TEST_CASE("one that joins in the middle of a fight has its first turn at the pace it has", "[pace]")
{
    // A scan brings it into the order and reads nothing of its Haste and
    // slows: the look at everyone its first turn begins on does.
    constexpr ObjectId kShriek = 0x202;
    std::vector<Creature> fight = StandardFight();
    fight.push_back(Make(kShriek, Side::Enemy, false, L"Shriek", 12.0f));
    fight.back().fighting = false;
    fight.back().speed = 1.5f;
    fight.back().movement.debuffs = {kGrease};
    World w(Timed(), fight);
    w.StartFight();
    REQUIRE(w.machine.Order().Find(kShriek) == nullptr);
    w.Get(kShriek).fighting = true;
    UntilTurnOf(w, kShriek);
    CHECK(w.machine.View().paceMps == Approx(3.75f)); // 5 m/s, 1.5 its build, 0.5 the grease
    CHECK(Logged(w, "Shriek moves at 0.75 a person's pace this turn"));
}

TEST_CASE("a scan in the middle of a turn leaves the bar at its creature's pace", "[pace]")
{
    // A scan reads who is fighting, not their Haste and slows, and the
    // machine keeps what the last look read of those through it.
    World w(Timed());
    w.Get(kHero).movement.rates = {kHaste};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.View().paceMps == Approx(6.5f));
    // A door opened: free of an action, and the machine scans as the turn
    // waits again (who fights in the room it showed joins now).
    const int scans = w.scans;
    REQUIRE(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(kHero);
    w.RunFor(800);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.scans > scans);
    CHECK(w.machine.View().paceMps == Approx(6.5f));
    CHECK_FALSE(Logged(w, "from now in this turn"));
}

TEST_CASE("[turns] pace changed in a turn is the next turn's, whatever its creature's own pace comes to in it",
          "[pace]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    Settings faster = Timed();
    faster.paceMps = 10.0f;
    w.machine.SetSettings(faster);
    CHECK(w.machine.View().paceMps == 5.0f); // this turn's stands
    // Her own pace changes in the turn: by the turn's own meters a second.
    w.Get(kHero).movement.rates = {kSalve};
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().paceMps == Approx(6.0f));
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().paceMps == Approx(12.0f));
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

    // Haste on, between two of her turns: said as her next turn begins, with
    // what it was before.
    UntilNotTurnOf(w, kHero);
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
    UntilNotTurnOf(w, kHero);
    w.Get(kHero).movement.rates = {kHaste, kSalve};
    w.Get(kHero).movement.debuffs = {kCaltrops};
    UntilTurnOf(w, kHero);
    CHECK(Lines(w, "Hero's movement rate effects as its turn begins: 2 (1.30, 1.20) of type 9 "
                   "(EFFECT_TYPE_MOVEMENT_RATE), 1 (0.60) of type 11 (its _DEBUFF): 0.94 of its build's pace") == 1);
    CHECK(Lines(w, "Hero moves at 0.94 a person's pace this turn (4.68 m a second of its bar): 1.00 from its build, "
                   "0.94 from haste or slows (1.30 at its last turn)") == 1);

    // All of it ends: said once more, that she is back at her own pace.
    UntilNotTurnOf(w, kHero);
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

    // Nobody else's turn said a pace: nothing was on them. And none of it
    // was a change within a turn.
    CHECK(Lines(w, "Genlock moves at") + Lines(w, "Alistair moves at") + Lines(w, "Hurlock moves at") == 0);
    CHECK(Logged(w, "Genlock's movement rate effects as its turn begins: none of type 9"));
    CHECK_FALSE(Logged(w, "from now in this turn"));
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
