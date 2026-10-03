// A turn as a bar of time (Settings::turnClock, dev/REBALANCE.md, "Current
// specification"), driven through scripted fights in the stand-in for the
// game (World.h): actions spending their own seconds, walking at a pace,
// an action that does not fit kept for the next turn, rollover, Combat
// Tactics, an AI choosing while it has time.
#include "World.h"

#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

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

Verdict Act(World &w, ObjectId id, CommandType type, Source source, float seconds,
            std::optional<float> walk = std::nullopt)
{
    CommandRequest request;
    request.object = id;
    request.type = type;
    request.source = source;
    request.inCombat = w.Get(id).fighting;
    request.walk = walk;
    request.actionSeconds = seconds;
    return w.Queue(request);
}

// Walks a creature in half meters a frame, and stands it.
void Walk(World &w, ObjectId id, float meters)
{
    w.Get(id).moving = true;
    for (float done = 0.0f; done < meters - 0.001f; done += 0.5f)
    {
        w.Get(id).position.x += std::min(0.5f, meters - done);
        w.Frame();
    }
    w.Get(id).moving = false;
    w.Frame();
}

// An action ends and the world settles.
void Done(World &w, ObjectId id)
{
    w.Finish(id);
    w.RunFor(600);
}

void UntilTurnOf(World &w, ObjectId id)
{
    for (int i = 0; i < 4000 && w.Current() == id; ++i)
    {
        w.Frame();
    }
    for (int i = 0; i < 4000 && w.Current() != id; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnPauseKey();
        }
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

bool Released(const World &w, ObjectId id)
{
    return std::ranges::any_of(w.applied,
                               [&](const Effect &e) { return e.kind == EffectKind::ReleaseCast && e.target == id; });
}

} // namespace

TEST_CASE("with time, a swing spends its seconds and the turn goes on until the bar is spent", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().timed);
    CHECK(w.machine.View().timeMs == 4000);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 2500);
    Done(w, kHero);
    // Not ended by the action: the player's turn waits for the next.
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 1000);
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    // The third does not fit: kept, and the turn is over.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Hold);
    CHECK(w.heldCasts.contains(kHero));
    CHECK_FALSE(w.Get(kHero).busy);
    w.Frame();
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(w.Said(L"Next turn"));
    // It lands as the hero's next turn begins, and that turn has what is
    // left after the half second it owed.
    UntilTurnOf(w, kHero);
    CHECK(Released(w, kHero));
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 3500);
}

TEST_CASE("with time, walking costs time at the pace after a free meter, and a move must fit", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // 1 m free and 4 s at 5 m/s: 21 m.
    CHECK(w.machine.View().movementLeft == 21.0f);
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 6.0f) == Verdict::Allow);
    w.Frame();
    Walk(w, kHero, 6.0f);
    Done(w, kHero);
    const TurnView view = w.machine.View();
    CHECK(view.timeLeftMs >= 2999);
    CHECK(view.timeLeftMs <= 3001);
    // 16 m left: 22 is too far, and refused before a step; a move never
    // pends.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 22.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Too far"));
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 15.0f) == Verdict::Allow);
}

TEST_CASE("with time, an attack's walk is paid as it is let through, and its swing cannot cut it short", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A walk of 6 m (1 s after the free meter) and a 2 s swing: 1 s left,
    // and walking the 6 m costs nothing more.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 6.0f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 1000);
    w.Frame();
    Walk(w, kHero, 6.0f);
    CHECK(w.Get(kHero).busy); // not stopped
    CHECK(w.machine.View().timeLeftMs == 1000);
    Done(w, kHero);
    CHECK(w.machine.View().timeLeftMs == 1000);
}

TEST_CASE("with time, an attack whose walk and swing do not fit lands next turn, its walk paid for", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    // 1 s of walk and 2 s of swing with 2 s left: the walk fits, so it is
    // not too far; it is kept, owing 1 s.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 6.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    CHECK(w.Current() != kHero);
    UntilTurnOf(w, kHero);
    CHECK(Released(w, kHero));
    CHECK(w.machine.View().timeLeftMs == 3000);
    w.Frame();
    Walk(w, kHero, 6.0f); // paid for in what it owed
    CHECK(w.machine.View().timeLeftMs == 3000);
}

TEST_CASE("with time, an action longer than a turn takes each turn it needs whole", "[time]")
{
    Settings s = Timed();
    constexpr std::int32_t kRitual = 13000;
    s.charge = {{kRitual, 2}}; // 8 s
    World w(s);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    // 3 s left of 8: owes 5, a whole turn and 1 s of the next.
    CHECK(w.Cast(kHero, Source::Player, kRitual, 0.0f) == Verdict::Hold);
    UntilTurnOf(w, kHero);
    CHECK_FALSE(Released(w, kHero)); // this turn is spent on it whole
    UntilTurnOf(w, kHero);
    CHECK(Released(w, kHero));
    CHECK(w.machine.View().timeLeftMs == 3000);
}

TEST_CASE("with time, what is left rolls over to the cap, and Combat Tactics adds to the bar", "[time]")
{
    std::vector<Creature> fight = StandardFight();
    fight.at(0).tactics = 4; // the hero: 1 s more a turn, and 2 s may roll over
    fight.at(1).rank = 3;    // the genlock a lieutenant: rank 2, half a second more
    World w(Timed(), fight);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().timeMs == 5000);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    // Ended with 3 s left: 2 roll over.
    (void)w.machine.OnPauseKey();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().timeMs == 4500);
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().timeMs == 7000);
}

TEST_CASE("with time, an AI chooses again while it has time, and its turn ends when its bar is spent", "[time]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        const Verdict v = Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f);
        CHECK(v != Verdict::Block);
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnPauseKey();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    CHECK(pokes == 1);
    // Two swings fit 4 s; the third is kept for its next turn.
    for (int i = 0; i < 2; ++i)
    {
        w.RunFor(200);
        Done(w, kGenlock);
        w.RunFor(1000);
    }
    w.RunFor(1000);
    CHECK(pokes == 3);
    CHECK(w.heldCasts.contains(kGenlock));
    CHECK(w.Current() != kGenlock);
}

TEST_CASE("with time, an action that failed without acting gives its time back", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "no room in the melee ring"));
    CHECK(w.machine.View().timeLeftMs == 4000);
    // Once a turn.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK_FALSE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "no room in the melee ring"));
}

TEST_CASE("as built, a turn is still one action and its meters", "[time]")
{
    World w; // the default clock
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK_FALSE(w.machine.View().timed);
    CHECK(w.machine.View().timeMs == 0);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().actionsLeft == 0);
}

TEST_CASE("an action's cost: its own seconds, or a stand-in by kind", "[time]")
{
    CommandRequest attack;
    attack.type = CommandType::Attack;
    CHECK(ActionCostMs(attack) == 2000);
    attack.actionSeconds = 1.9f;
    CHECK(ActionCostMs(attack) == 1900);
    attack.actionSeconds = 1000.0f;
    CHECK(ActionCostMs(attack) == 60000); // no action is longer than a minute

    CommandRequest ability;
    ability.type = CommandType::UseAbility;
    ability.ability = 10000;
    CHECK(ActionCostMs(ability) == 2000); // a talent
    ability.conjureSeconds = 2.0f;        // a Speed, without a conjure animation: none
    CHECK(ActionCostMs(ability) == 2000);
    ability.abilityType = kAbilitySpell;
    ability.conjureAnim = 245;
    ability.conjureSeconds = 1.0f; // fast, with a staff: 3.47 s of conjuring
    ability.staff = true;
    CHECK(ActionCostMs(ability) == 5470);
    ability.abilityType = kAbilityItem; // a potion
    CHECK(ActionCostMs(ability) == 1230);

    CommandRequest mode;
    mode.type = CommandType::UseAbility;
    mode.sustained = true;
    mode.sustainedOn = true;
    CHECK(ActionCostMs(mode) == 0); // turned off: free
    mode.sustainedOn = false;
    CHECK(ActionCostMs(mode) == 2000);
}

TEST_CASE("with time, a turn whose bar is spent ends by itself, its free meter unwalked", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("with time, a creature whose action is kept for its next turn walks no more", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.5f, 0.0f) == Verdict::Allow);
    // Kept as the first still runs (an AI queues its next as one ends):
    // the turn ends as the first settles, and meanwhile not a step, not
    // even the free meter (test_fights.cpp, seed 130).
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
    CHECK(w.machine.View().movementLeft == 0.0f);
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 0.8f) == Verdict::Block);
    w.Frame();
    CHECK(w.Current() == kHero); // its attack still runs
    Done(w, kHero);
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an action kept just before a conversation ends its turn after it, and the next is begun", "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
    // A conversation before the machine's next look (test_fights.cpp, seed
    // 594 with time).
    w.mode = Mode::Dialog;
    w.RunFor(200);
    w.mode = Mode::Combat;
    w.RunFor(200);
    CHECK(w.Current() == kGenlock);
    CHECK(std::ranges::any_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnStarted && e.who == kGenlock; }));
    CHECK(std::ranges::none_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnResumed && e.who == kGenlock; }));
}
