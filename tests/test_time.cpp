// A turn as a bar of time (Settings::turnClock, dev/REBALANCE.md, "Current
// specification"), driven through scripted fights in the stand-in for the
// game (World.h): actions spending their own seconds, walking at a pace,
// an action that does not fit kept for the next turn, rollover, Combat
// Tactics, an AI choosing while it has time.
#include "World.h"

#include "core/Bar.h"
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
            (void)w.machine.OnEndTurn();
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
    // Said over her, and her turn hers for a beat more, so that it is read
    // before the next turn's announcement (play test 27).
    CHECK(w.Said(L"Lands next turn"));
    CHECK(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Block);
    w.RunFor(900);
    CHECK(w.Current() != kHero);
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

TEST_CASE("with time, an attack whose swing does not fit walks to its target now and lands next turn", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    // 1 s of walk and 2 s of swing with 2 s left: the walk fits, so it is
    // not too far; the attack is kept, owing 1 s, and she walks to her
    // target now (the game layer's move, let through though it is a
    // script's on the player's turn).
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 6.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    CHECK(w.approaches == 1);
    REQUIRE(w.Current() == kHero);
    CHECK(w.Get(kHero).busy);
    CHECK(w.Get(kHero).current == CommandType::MoveToObject);
    CHECK_FALSE(w.paused); // the world runs while she walks
    Walk(w, kHero, 6.0f);
    CHECK(w.Get(kHero).busy); // not stopped: the walk was paid for
    // Nothing else this turn: its bar is spent.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Block);
    Done(w, kHero);
    CHECK(w.Current() != kHero);
    UntilTurnOf(w, kHero);
    CHECK(Released(w, kHero));
    CHECK(w.machine.View().timeLeftMs == 3000);
}

TEST_CASE("with time, an attack kept where its creature stands ends the turn after a beat", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
    w.RunFor(900);
    CHECK(w.approaches == 0);
    CHECK(w.Current() != kHero);
}

TEST_CASE("with time, a kept action whose target is gone gives back what it owed", "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold); // owes 1 s
    w.targetGone.insert(kHero); // its target falls before her next turn
    UntilTurnOf(w, kHero);
    w.RunFor(1200);
    // Not given, the second she paid for it back, and her turn hers.
    CHECK(w.released.empty());
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == "its target is gone";
    }));
}

TEST_CASE("with time, a kept action is said over the player's own, and nothing is interrupted as a fight ends",
          "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.onPoke = [&](ObjectId id) { (void)Act(w, id, CommandType::Attack, Source::Script, 3.0f, 0.0f); };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    CHECK(w.Said(L"Lands next turn"));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionPending && e.who == kHero && e.command == "attack";
    }));
    w.RunFor(900);
    // The genlock's second attack is kept too: not said over it.
    w.applied.clear();
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(300);
    Done(w, kGenlock);
    w.RunFor(1500);
    CHECK(w.heldCasts.contains(kGenlock));
    CHECK_FALSE(w.Said(L"Lands next turn"));
    // The enemies fall: the fight is over, her attack still pending.
    w.applied.clear();
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(2000);
    CHECK_FALSE(w.machine.Active());
    CHECK_FALSE(w.Said(L"Action lost"));
    CHECK_FALSE(w.Said(L"Spell lost"));
    CHECK(w.heldCasts.empty());
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
    (void)w.machine.OnEndTurn();
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
    (void)w.machine.OnEndTurn();
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
    // And every time it fails so: with time nothing is charged for what
    // did not happen (as built, once a turn: test_machine.cpp).
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "no room in the melee ring"));
    CHECK(w.machine.View().timeLeftMs == 4000);
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
    w.RunFor(1000); // and the beat her kept action's word is given
    CHECK(w.Current() == kGenlock);
    CHECK(std::ranges::any_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnStarted && e.who == kGenlock; }));
    CHECK(std::ranges::none_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnResumed && e.who == kGenlock; }));
}

TEST_CASE("a conversation longer than the kept action's beat: the turn ends as it is over, and the next is begun",
          "[time]")
{
    // The same, the conversation outlasting the beat: the turn ends in the
    // frame the fight comes back, before anything is resumed, and the next
    // creature's turn must begin, with its bar, not go on as if it had been
    // interrupted. The random fights held this alone until their course
    // changed (the mutants' run of 2026-10-02).
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
    w.mode = Mode::Dialog;
    w.RunFor(1500);
    w.mode = Mode::Combat;
    w.RunFor(400);
    CHECK(w.Current() == kGenlock);
    CHECK(std::ranges::any_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnStarted && e.who == kGenlock; }));
    CHECK(std::ranges::none_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnResumed && e.who == kGenlock; }));
    CHECK(w.machine.View().timeMs == 4000);
}

TEST_CASE("with time, a cooldown counts on its user's clock and lifts in the middle of a turn", "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    const auto left = [&]() -> std::optional<std::uint32_t> {
        const auto cooldowns = w.machine.State().cooldowns;
        return cooldowns.empty() ? std::nullopt : std::optional{cooldowns.front().leftMs};
    };
    constexpr std::int32_t kTalent = 617;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A talent of 5 s' cooldown (2 s, the stand-in): held as it ends, its
    // own time not counted against it.
    CHECK(w.UseAbility(kHero, Source::Player, kTalent, 5.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(left());
    CHECK(*left() == 5000);
    // A swing of 1.5 s moves her clock 1.5 s.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    CHECK(*left() == 3500);
    Done(w, kHero);
    // The last half second walked (a free meter, and 2.5 m at 5 m/s): the
    // bar is spent, and her turn ends by itself, nothing rolled over. (A
    // bar with rollover in it moves the clock by its share: 1.5 s of a
    // 4.5 s bar is a third of the turn's 4 s. test_turntime.cpp has that.)
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 3.5f) == Verdict::Allow);
    w.Frame();
    Walk(w, kHero, 3.5f);
    Done(w, kHero);
    UntilTurnOf(w, kHero);
    REQUIRE(left());
    CHECK(*left() >= 2990);
    CHECK(*left() <= 3010);
    REQUIRE(w.machine.View().timeMs == 4000);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(*left() >= 1490);
    CHECK(*left() <= 1510);
    // The second swing brings it to nothing: ready again, her turn still
    // hers with a second left.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
    CHECK_FALSE(left());
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs >= 990);
    CHECK(w.machine.View().timeLeftMs <= 1010);
    CHECK(std::ranges::count_if(events, [](const TurnEvent &e) {
              return e.kind == EventKind::CooldownReleased && e.who == kHero && e.value == 617;
          }) == 1);
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == 617 && e.b == 0;
    }));
}

TEST_CASE("with time, walking moves the clock too, and a turn lost moves it a whole turn", "[time]")
{
    World w(Timed());
    const auto left = [&](ObjectId who) -> std::optional<std::uint32_t> {
        for (const CooldownView &c : w.machine.State().cooldowns)
        {
            if (c.id == who)
            {
                return c.leftMs;
            }
        }
        return std::nullopt;
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, 617, 9.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(left(kHero));
    // 6 m: a meter free, a second at 5 m/s.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 6.0f) == Verdict::Allow);
    w.Frame();
    Walk(w, kHero, 6.0f);
    Done(w, kHero);
    CHECK(*left(kHero) >= 7990);
    CHECK(*left(kHero) <= 8010);
    // Stunned through her next turn: the turn is lost, and her clock moves
    // through it.
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    w.Get(kHero).condition = Condition::Disabled;
    for (int i = 0; i < 4000 && w.machine.Order().Round() < 3; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    // 9 s less the second walked, the rest of turn 1 (1 s more of a 4 s
    // turn's 2 s after the talent) and the whole of the lost turn 2.
    REQUIRE(left(kHero));
    CHECK(*left(kHero) >= 2990);
    CHECK(*left(kHero) <= 3010);
}

TEST_CASE("as built, a cooldown still counts its user's turns", "[time]")
{
    World w; // one action and meters
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
    Done(w, kHero);
    const auto cooldowns = w.machine.State().cooldowns;
    REQUIRE(cooldowns.size() == 1);
    CHECK(cooldowns.front().turns == 2);
    CHECK(cooldowns.front().ofMs == 0);
}

namespace
{
// A hold of so many seconds on a creature, as the game layer reads it.
void Stun(World &w, ObjectId id, float seconds)
{
    w.Get(id).condition = Condition::Disabled;
    w.Get(id).conditionSeconds = seconds;
}
} // namespace

TEST_CASE("with time, a hold shorter than a turn takes its share of its victim's bar", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // The genlock stunned for 2 s while the hero thinks: half its turn.
    Stun(w, kGenlock, 2.0f);
    w.RunFor(700); // conditions are read twice a second
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    // Not lost: begun with half its bar, the stun ended by the machine, and
    // its AI told to choose.
    CHECK(w.machine.View().timeMs == 4000);
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(w.Did(EffectKind::EndConditions, kGenlock));
    CHECK(w.Get(kGenlock).condition == Condition::None);
    CHECK(pokes == 1);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
}

TEST_CASE("with time, a hold of a turn and more is a turn lost and the rest of the next", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    Stun(w, kGenlock, 6.0f);
    w.RunFor(700);
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    // Its turn is lost whole, and it is Alistair's.
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK(w.Current() == kCompanion);
    const auto held = w.machine.State().held;
    const auto genlock = std::ranges::find(held, kGenlock, &HeldView::id);
    REQUIRE(genlock != held.end());
    CHECK(genlock->owedMs == 2000);
    // Round 2: the 2 s left are half its bar.
    for (int i = 0; i < 4000 && !(w.machine.Order().Round() == 2 && w.Current() == kGenlock); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(100);
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(w.Did(EffectKind::EndConditions, kGenlock));
}

TEST_CASE("with time, a hold landing in its victim's own turn takes the rest of that turn first", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A second spent, then stunned for 5 s: the turn's other 3 s are the
    // stun's, and 2 s are owed to the next.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    Stun(w, kHero, 5.0f);
    w.RunFor(700);
    CHECK(w.Current() != kHero); // the rest of her turn is lost
    const auto held = w.machine.State().held;
    const auto hero = std::ranges::find(held, kHero, &HeldView::id);
    REQUIRE(hero != held.end());
    CHECK(hero->owedMs == 2000);
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
}

TEST_CASE("with time, a hold whose seconds are not read costs condition_turns, and one taken off costs nothing",
          "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    Stun(w, kGenlock, 0.0f); // not read
    Stun(w, kHurlock, 8.0f);
    w.RunFor(700);
    auto held = w.machine.State().held;
    CHECK(std::ranges::find(held, kGenlock, &HeldView::id)->owedMs == 4000); // condition_turns 1: a turn
    CHECK(std::ranges::find(held, kHurlock, &HeldView::id)->owedMs == 8000);
    // The hurlock's is cured before its turn: nothing owed.
    w.Get(kHurlock).condition = Condition::None;
    w.RunFor(700);
    held = w.machine.State().held;
    const auto hurlock = std::ranges::find(held, kHurlock, &HeldView::id);
    CHECK((hurlock == held.end() || hurlock->owedMs == 0));
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    CHECK(w.Said(L"Genlock loses the turn"));
    // And the hurlock's turn, when it comes, is whole.
    for (int i = 0; i < 4000 && w.Current() != kHurlock; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kHurlock);
    w.RunFor(100);
    CHECK_FALSE(w.Said(L"Hurlock loses the turn"));
    CHECK(w.machine.View().timeLeftMs == 4000);
}

TEST_CASE("with time, an AI stunned since the last look is not told to choose again", "[time]")
{
    Settings s = Timed();
    s.settleMs = 0;
    World w(s);
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        CHECK(Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f) == Verdict::Allow);
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    // Its swing ends and it is stunned, both between two looks at the
    // statuses (they are read twice a second): with 2.5 s left it would be
    // told to choose again, on a look that still shows it free.
    w.Finish(kGenlock);
    w.Get(kGenlock).condition = Condition::Disabled;
    w.Get(kGenlock).conditionSeconds = 3.0f;
    w.RunFor(160);
    CHECK(pokes == 1);
    CHECK(w.Current() != kGenlock); // the rest of its turn is the stun's
}

TEST_CASE("with time, the view says a turn is a bar of time between two turns too", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Between the hero's turn and the genlock's the budget is reset: the
    // view still says the fight is on the bar, by the setting (by the
    // budget, the game's bar went back to experience for a third of a
    // second: play test 27).
    (void)w.machine.OnEndTurn();
    bool every = true;
    for (int i = 0; i < 40; ++i)
    {
        w.Frame();
        every = every && w.machine.View().timed;
    }
    CHECK(every);
    CHECK_FALSE(World{}.machine.View().timed);
}

TEST_CASE("with time, the walk to a kept attack's target has a little slack and stops without a word", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 6.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    REQUIRE(w.Get(kHero).current == CommandType::MoveToObject);
    // The game's walk runs a little past what was paid for: within a meter,
    // not stopped.
    Walk(w, kHero, 6.8f);
    CHECK(w.Get(kHero).busy);
    // Well past it, it is stopped: with no "Out of movement" over one that
    // did what was ordered.
    w.applied.clear();
    Walk(w, kHero, 2.0f);
    CHECK_FALSE(w.Get(kHero).busy);
    CHECK_FALSE(w.Said(L"Out of movement"));
}

TEST_CASE("with time, an action whose own effect holds its creature is charged the time it runs", "[time]")
{
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (w.Get(id).busy)
        {
            return;
        }
        // A grab: charged 2 s as it is let through.
        (void)Act(w, id, CommandType::UseAbility, Source::Script, 2.0f, 0.0f);
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    // Its ability's own effect then holds it for 7 s more (an Overwhelm).
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    w.RunFor(7000);
    // The 2 s charged covered the first of it, the 2 s left of its bar the
    // next, and 3 s are owed to its next turns.
    CHECK(w.machine.View().timeLeftMs == 0);
    const auto charging = w.machine.State().charging;
    const auto debt = std::ranges::find(charging, kGenlock, &ChargeView::id);
    REQUIRE(debt != charging.end());
    CHECK(debt->owedMs >= 2700);
    CHECK(debt->owedMs <= 3300);
    // The card says so.
    const TurnView view = w.machine.View();
    const auto card = std::ranges::find(view.upcoming, kGenlock, &CardView::id);
    REQUIRE(card != view.upcoming.end());
    CHECK(card->owedMs == debt->owedMs);
}

TEST_CASE("with time, a turn that began by paying for a pending action says what it paid", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold); // owes 1 s
    w.RunFor(100);
    const TurnView during = w.machine.View();
    const auto card = std::ranges::find(during.upcoming, kHero, &CardView::id);
    REQUIRE(card != during.upcoming.end());
    CHECK(card->owedMs == 1000);
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().pendingPaidMs == 1000);
    CHECK(w.machine.View().timeLeftMs == 3000);
}

TEST_CASE("with time, an action whose walk runs past the bar is charged when its creature gets there", "[time]")
{
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (!w.Get(id).busy)
        {
            // An AI's attack, its walk not known to the gate: 2 s, charged
            // as it is let through.
            (void)Act(w, id, CommandType::Attack, Source::Script, 2.0f);
        }
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    // It walks 15 m: a meter free and 2.8 s at 5 m/s, more than the 2 s
    // left. The swing's time goes to the walk; it is not stopped.
    Walk(w, kGenlock, 15.0f);
    CHECK(w.Get(kGenlock).busy);
    CHECK(w.Current() == kGenlock);
    // Standing at its attack: the 2 s are taken now, 1.2 s of them there
    // and 0.8 s owed to its next turn.
    w.RunFor(100);
    CHECK(w.machine.View().timeLeftMs == 0);
    const auto charging = w.machine.State().charging;
    const auto debt = std::ranges::find(charging, kGenlock, &ChargeView::id);
    REQUIRE(debt != charging.end());
    CHECK(debt->owedMs >= 780);
    CHECK(debt->owedMs <= 820);
    CHECK_FALSE(w.heldCasts.contains(kGenlock)); // it ran: nothing is kept
}

TEST_CASE("with time, a walk longer than the whole bar is stopped, and its action never charged", "[time]")
{
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (!w.Get(id).busy)
        {
            (void)Act(w, id, CommandType::Attack, Source::Script, 2.0f);
        }
    };
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    Walk(w, kGenlock, 25.0f); // 21 m is all its bar walks
    w.RunFor(100);
    CHECK(w.Current() != kGenlock); // out of movement: its turn is over
    CHECK(w.machine.State().charging.empty());
}

TEST_CASE("a blow over the threshold interrupts a pending spell, and nothing else", "[time]")
{
    const auto pending = [](World &w, std::int32_t abilityType) {
        CommandRequest spell;
        spell.object = kHero;
        spell.type = CommandType::UseAbility;
        spell.source = Source::Player;
        spell.inCombat = true;
        spell.ability = 13000;
        spell.abilityType = abilityType;
        spell.actionSeconds = 5.5f; // a conjuring longer than her bar
        return w.Queue(spell);
    };
    {
        World w(Timed());
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        REQUIRE(pending(w, kAbilitySpell) == Verdict::Hold);
        w.RunFor(100);
        // A scratch, and a burn however hot: not interrupted.
        CHECK_FALSE(w.machine.OnDamaged(kHero, 8.0f, true, 20.0f));
        CHECK_FALSE(w.machine.OnDamaged(kHero, 90.0f, false, 20.0f));
        CHECK(w.heldCasts.contains(kHero));
        // A blow over the threshold: lost.
        CHECK(w.machine.OnDamaged(kHero, 31.0f, true, 20.0f));
        w.Frame();
        CHECK_FALSE(w.heldCasts.contains(kHero));
        CHECK(w.Said(L"Spell lost")); // not the game's own "Interrupted!", which it floats for a broken swing
        CHECK(w.machine.State().charging.empty());
        CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
            return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == "struck";
        }));
    }
    {
        // A talent pending is not a spell being conjured.
        World w(Timed());
        w.StartFight();
        REQUIRE(pending(w, kAbilityTalent) == Verdict::Hold);
        w.RunFor(100);
        CHECK_FALSE(w.machine.OnDamaged(kHero, 99.0f, true, 20.0f));
        CHECK(w.heldCasts.contains(kHero));
    }
}

TEST_CASE("a card says what is pending: an ability by its id, an attack by none; the string its icon and name",
          "[time][bar]")
{
    World w(Timed());
    w.StartFight();
    const auto card = [&](ObjectId id) {
        const TurnView view = w.machine.View();
        const auto found = std::ranges::find(view.upcoming, id, &CardView::id);
        REQUIRE(found != view.upcoming.end());
        return *found;
    };
    CHECK(card(kHero).pendingAbility == -1);
    // A spell longer than her bar: kept, and her card says which.
    CommandRequest spell;
    spell.object = kHero;
    spell.type = CommandType::UseAbility;
    spell.source = Source::Player;
    spell.inCombat = true;
    spell.ability = 10211;
    spell.abilityType = kAbilitySpell;
    spell.actionSeconds = 5.0f;
    REQUIRE(w.Queue(spell) == Verdict::Hold);
    w.RunFor(100);
    CHECK(card(kHero).owedMs == 1000);
    CHECK(card(kHero).pendingAbility == 10211);
    // The game layer adds the icon and the name; the string carries the three.
    TurnView view = w.machine.View();
    for (CardView &c : view.upcoming)
    {
        if (c.id == kHero)
        {
            c.pendingIcon = L"ico_chain_lightning";
            c.pendingName = L"Chain Lightning";
        }
    }
    const std::wstring bar = EncodeBar(view);
    const std::wstring tail =
        std::wstring{kBarField} + L"10211" + kBarField + L"ico_chain_lightning" + kBarField + L"Chain Lightning";
    CHECK(bar.find(tail) != std::wstring::npos);
    // The others have nothing pending: -1 and two empty fields.
    CHECK(bar.ends_with(std::wstring{kBarField} + L"-1" + kBarField + kBarField));
}

TEST_CASE("a pending attack is no ability's", "[time][bar]")
{
    World w(Timed());
    w.StartFight();
    // Two swings of 1.5 s, and a third that her bar does not hold: kept.
    for (int swing = 0; swing < 2; ++swing)
    {
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f) == Verdict::Allow);
        Done(w, kHero);
    }
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f) == Verdict::Hold);
    w.RunFor(100);
    const TurnView view = w.machine.View();
    const auto hero = std::ranges::find(view.upcoming, kHero, &CardView::id);
    REQUIRE(hero != view.upcoming.end());
    CHECK(hero->owedMs == 500);
    CHECK(hero->pendingAbility == -1);
}

TEST_CASE("with time, an action over at once did not play: its time is given back, and its cooldown not held", "[time]")
{
    Settings on = Timed();
    on.giveBackUnplayed = true;
    constexpr std::int32_t kChainLightning = 10211;
    const auto cast = [&](World &w, float seconds) {
        CommandRequest spell;
        spell.object = kHero;
        spell.type = CommandType::UseAbility;
        spell.source = Source::Player;
        spell.inCombat = true;
        spell.ability = kChainLightning;
        spell.abilityType = kAbilitySpell;
        spell.cooldownSeconds = 40.0f;
        spell.actionSeconds = seconds;
        return w.Queue(spell);
    };
    SECTION("a spell of 3.9 s over in 0.4 s")
    {
        World w(on);
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        REQUIRE(cast(w, 3.9f) == Verdict::Allow);
        CHECK(w.machine.View().timeLeftMs == 100);
        w.RunFor(400);
        w.Finish(kHero);
        w.RunFor(600);
        // Her bar again, said over her, and the turn waits on her order.
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK(w.Said(L"Nothing happened"));
        CHECK(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
            return e.kind == EventKind::ActionGivenBack && e.who == kHero && e.detail == "over at once: it did nothing";
        }));
        // And the spell is not held on cooldown: it was never cast.
        CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::CooldownHeld; }));
        CHECK(w.machine.State().cooldowns.empty());
        // And again, every time it happens: a second and a third were
        // charged, with no swing and no word (play test 30).
        for (int again = 0; again < 2; ++again)
        {
            w.applied.clear();
            REQUIRE(cast(w, 3.9f) == Verdict::Allow);
            w.RunFor(400);
            w.Finish(kHero);
            w.RunFor(600);
            CHECK(w.machine.View().timeLeftMs == 4000);
            CHECK(w.Said(L"Nothing happened"));
        }
    }
    SECTION("one that played its time is charged")
    {
        World w(on);
        w.StartFight();
        REQUIRE(cast(w, 3.9f) == Verdict::Allow);
        w.RunFor(3300); // 0.84 of its animation, and a little
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.machine.View().timeLeftMs == 100);
        CHECK_FALSE(w.Said(L"Nothing happened"));
        CHECK_FALSE(w.machine.State().cooldowns.empty());
    }
    SECTION("one whose time was a stand-in's is charged whatever it took")
    {
        World w(on);
        w.StartFight();
        REQUIRE(cast(w, 0.0f) == Verdict::Allow); // nothing read: 2 s
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.machine.View().timeLeftMs == 2000);
        CHECK_FALSE(w.Said(L"Nothing happened"));
    }
    SECTION("with the setting off")
    {
        World w(Timed());
        w.StartFight();
        REQUIRE(cast(w, 3.9f) == Verdict::Allow);
        w.RunFor(400);
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.machine.View().timeLeftMs == 100);
    }
}

TEST_CASE("a pending action lost says so in the mod's own words, in gold", "[time]")
{
    World w(Timed());
    w.StartFight();
    CommandRequest spell;
    spell.object = kHero;
    spell.type = CommandType::UseAbility;
    spell.source = Source::Player;
    spell.inCombat = true;
    spell.ability = 13000;
    spell.abilityType = kAbilitySpell;
    spell.actionSeconds = 5.5f;
    REQUIRE(w.Queue(spell) == Verdict::Hold);
    w.RunFor(100);
    REQUIRE(w.machine.OnDamaged(kHero, 31.0f, true, 20.0f));
    w.Frame();
    const auto said = std::ranges::find_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == L"Spell lost"; });
    REQUIRE(said != w.applied.end());
    CHECK(said->target == kHero);
    CHECK(said->colour == kGold);
    // An enemy's too: the genlock's kept swing, lost to a hold.
    World e(Timed());
    e.onPoke = [&](ObjectId id) {
        if (!e.Get(id).busy)
        {
            (void)Act(e, id, CommandType::Attack, Source::Script, 5.0f);
        }
    };
    e.StartFight();
    (void)e.machine.OnEndTurn();
    e.RunFor(300);
    REQUIRE(e.heldCasts.contains(kGenlock));
    e.Get(kGenlock).condition = Condition::Disabled;
    e.RunFor(1200);
    const auto lost = std::ranges::find_if(
        e.applied, [](const Effect &f) { return f.kind == EffectKind::Floaty && f.text == L"Action lost"; });
    REQUIRE(lost != e.applied.end());
    CHECK(lost->target == kGenlock);
    CHECK(lost->colour == kGold);
}

TEST_CASE("an attack whose creature stands short of its target is stopped, its action given back", "[time]")
{
    constexpr std::uint32_t kStood = 1000; // the machine's patience with an attack standing short of its target

    Settings on = Timed();
    SECTION("an AI's: it chooses again, once")
    {
        World w(on);
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            ++pokes;
            // An attack with five meters to walk, as the gate was told.
            (void)Act(w, id, CommandType::Attack, Source::Script, 1.5f, 5.0f);
        };
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        (void)w.machine.OnEndTurn();
        w.RunFor(200);
        REQUIRE(w.Current() == kGenlock);
        REQUIRE(pokes == 1);
        // It walks a meter and stands there, others in its way: busy at its
        // attack, not a step more.
        Walk(w, kGenlock, 1.0f);
        w.Get(kGenlock).busy = true;
        w.Get(kGenlock).current = CommandType::Attack;
        w.applied.clear();
        w.RunFor(kStood + 200);
        CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
        CHECK(std::ranges::any_of(
            events, [](const TurnEvent &e) { return e.kind == EventKind::ActionGivenBack && e.who == kGenlock; }));
        // The world stops its command; told to choose again, it does.
        w.Finish(kGenlock);
        w.RunFor(1500);
        CHECK(pokes >= 2);
    }
    SECTION("the player's: said, and the turn goes on with its time")
    {
        World w(on);
        w.StartFight();
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 5.0f) == Verdict::Allow);
        w.Get(kHero).moving = false;
        w.applied.clear();
        w.RunFor(kStood - 300);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero)); // not yet
        w.RunFor(500);
        CHECK(w.Did(EffectKind::ClearCommands, kHero));
        CHECK(w.Said(L"No way through"));
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        // The swing's 1.5 s back; the walk it paid for stays paid.
        CHECK(w.machine.View().timeLeftMs == 4000 - 800);
    }
    SECTION("one that swings where it stands, in reach, is not stopped")
    {
        World w(on);
        w.StartFight();
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 0.4f) == Verdict::Allow);
        w.applied.clear();
        w.RunFor(kStood + 500);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
        CHECK_FALSE(w.Said(L"No way through"));
    }
    SECTION("nor one that has walked its walk and swings")
    {
        World w(on);
        w.StartFight();
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.5f, 5.0f) == Verdict::Allow);
        Walk(w, kHero, 4.6f);
        w.Get(kHero).busy = true;
        w.Get(kHero).current = CommandType::Attack;
        w.applied.clear();
        w.RunFor(kStood + 500);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
    }
}

TEST_CASE("each command of a turn has max_action_ms of its own", "[machine]")
{
    Settings s = Fixed();
    s.maxActionMs = 2000;
    s.maxCommandsPerTurn = 3;
    World w(s);
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        (void)w.Order(id, CommandType::MoveToLocation, Source::Script);
    };
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    // A walk of 1.5 s, and as it ends, in the same frame, its attack: 1.5 s
    // more. Each within its limit; from the turn's first command the second
    // would be over it.
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 90; ++i)
    {
        w.Get(kGenlock).position.x += 0.05f;
        w.Frame();
    }
    w.Get(kGenlock).moving = false;
    w.Finish(kGenlock);
    REQUIRE(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    w.RunFor(1500);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(std::ranges::any_of(
        w.log, [](const std::string &line) { return line.find("still at a") != std::string::npos; }));
    // Left at it, the attack is stopped at its own limit.
    w.record = true;
    w.RunFor(1000);
    CHECK(std::ranges::any_of(w.log,
                              [](const std::string &line) { return line.find("still at a") != std::string::npos; }));
}

TEST_CASE("an action given back is said over the player's own, by why, in gold", "[time]")
{
    const auto said = [](const World &w, const std::wstring &text) {
        return std::ranges::find_if(w.applied,
                                    [&](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == text; });
    };
    for (const auto &[why, words] : {std::pair{"COMMAND_FAILED_INVALID_PATH", L"No way through"},
                                     std::pair{"COMMAND_FAILED_NO_SPACE_IN_MELEE_RING", L"No way through"},
                                     std::pair{"COMMAND_FAILED_NO_LINE_OF_SIGHT", L"No line of sight"},
                                     std::pair{"COMMAND_FAILED_INVALID_DATA", L"Nothing happened"}})
    {
        World w(Timed());
        w.StartFight();
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
        REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, why));
        w.Frame();
        CAPTURE(why);
        const auto floaty = said(w, words);
        REQUIRE(floaty != w.applied.end());
        CHECK(floaty->target == kHero);
        CHECK(floaty->colour == kGold);
    }
    // Not over an enemy: its AI is told, and nobody needs the word.
    World w(Timed());
    w.onPoke = [&](ObjectId id) { (void)Act(w, id, CommandType::Attack, Source::Script, 2.0f, 0.0f); };
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH"));
    w.Frame();
    CHECK(said(w, L"No way through") == w.applied.end());
}

TEST_CASE("an AI whose actions were given back twice in a turn has nothing it can get to: its turn ends", "[time]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        (void)Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f);
    };
    std::vector<std::string> ends;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnEnded && e.who == kGenlock)
        {
            ends.push_back(e.detail);
        }
    });
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    // No room at its target: given back, and it chooses again.
    REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.RunFor(1500);
    REQUIRE(pokes == 2);
    REQUIRE(w.Current() == kGenlock);
    // The same again: it is not asked a third time.
    REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.RunFor(1500);
    CHECK(pokes == 2);
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.size() == 1);
    CHECK(ends.front() == "nothing it can get to");
}

TEST_CASE("a party member's lost turn is said in gold, an enemy's in grey", "[time]")
{
    for (const ObjectId who : {kCompanion, kGenlock})
    {
        World w(Timed());
        w.StartFight();
        w.Get(who).condition = Condition::Disabled;
        w.Get(who).conditionSeconds = 6.0f;
        w.RunFor(700);
        for (int i = 0; i < 2000 && w.machine.Order().Round() == 1; ++i)
        {
            if (w.machine.GetPhase() == Phase::AwaitingOrder)
            {
                (void)w.machine.OnEndTurn();
            }
            w.Frame();
        }
        const std::wstring words = std::wstring{w.Get(who).name} + L" loses the turn";
        const auto floaty = std::ranges::find_if(
            w.applied, [&](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == words; });
        CAPTURE(who);
        REQUIRE(floaty != w.applied.end());
        CHECK(floaty->colour == (who == kCompanion ? kGold : kGrey));
    }
}

TEST_CASE("an enemy's time by its rank: a lieutenant half a second, a boss a second, an elite boss one and a half",
          "[time]")
{
    // The user, 2026-10-02: "lieutenant gets 0.5s more, boss gets 1s more,
    // elite boss 1.5s more".
    const std::pair<int, std::uint32_t> ranks[] = {{1, 4000}, {2, 4000}, {3, 4500}, {4, 5000}, {5, 5500}};
    for (const auto &[rank, bar] : ranks)
    {
        std::vector<Creature> fight = StandardFight();
        fight.at(1).rank = rank;
        World w(Timed(), fight);
        w.StartFight();
        (void)w.machine.OnEndTurn();
        w.RunFor(300);
        REQUIRE(w.Current() == kGenlock);
        CAPTURE(rank);
        CHECK(w.machine.View().timeMs == bar);
    }
}

TEST_CASE("with own clocks, a creature's effects age as its bar is spent, a turn's seconds a turn, nobody else's",
          "[time]")
{
    // The user, 2026-10-02: "things on each creature's own clock seems like
    // the most important next thing".
    World w(Timed());
    w.StartFight();
    for (const ObjectId id : {kHero, kGenlock, kCompanion, kHurlock})
    {
        CAPTURE(id);
        CHECK(w.onOwnClock.contains(id));
    }
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    // A bar of 4 s: its clock is its bar, second for second.
    CHECK(w.agedOwn[kHero] == 2000);
    CHECK(w.agedOwn[kGenlock] == 0);
    const std::int64_t shared = w.agedMs;
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    // Ended early: the rest of the way to a turn's seconds, and no more.
    CHECK(w.agedOwn[kHero] == 4000);
    CHECK(w.agedOwn[kGenlock] == 0);
    CHECK(w.agedMs == shared); // the shared clock waits for the round
    // A round on: each the same, and the shared clock a round's worth, for
    // what is in no one's turn (an area on the ground).
    for (int i = 0; i < 4000 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == 2);
    w.RunFor(100);
    for (const ObjectId id : {kHero, kGenlock, kCompanion, kHurlock})
    {
        CAPTURE(id);
        CHECK(w.agedOwn[id] == 4000);
    }
    CHECK(w.agedMs == shared + 4000);
}

TEST_CASE("with own clocks, a bigger bar moves its clock by its share, and a lost turn moves it whole", "[time]")
{
    std::vector<Creature> fight = StandardFight();
    fight.at(0).tactics = 4; // the hero: a bar of 5 s
    World w(Timed(), fight);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(w.agedOwn[kHero] == 1600); // 2 of its 5 s: two fifths of 4 s
    // The genlock stunned for longer than a turn: its turn is lost, and its
    // clock moves a turn's seconds for it.
    Stun(w, kGenlock, 6.0f);
    w.RunFor(700);
    (void)w.machine.OnEndTurn();
    for (int i = 0; i < 400 && w.Current() != kCompanion; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    CHECK(w.agedOwn[kHero] == 4000);
    CHECK(w.agedOwn[kGenlock] == 4000);
}

TEST_CASE("with own clocks off, or with actions, everyone's effects age together as each round begins", "[time]")
{
    Settings off = Timed();
    off.ownClocks = false;
    for (const Settings &settings : {off, Fixed()})
    {
        World w(settings);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        CHECK(w.onOwnClock.empty());
        CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
        Done(w, kHero);
        for (int i = 0; i < 4000 && w.machine.Order().Round() < 2; ++i)
        {
            if (w.machine.GetPhase() == Phase::AwaitingOrder)
            {
                (void)w.machine.OnEndTurn();
            }
            w.Frame();
        }
        REQUIRE(w.machine.Order().Round() == 2);
        w.RunFor(100);
        CHECK(w.agedOwn.empty());
        CHECK(w.agedMs > 0);
    }
}

TEST_CASE("with own clocks, a turn put off moves nothing until it is taken", "[time]")
{
    std::vector<Creature> fight = StandardFight();
    World w(Timed(), fight);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.OnDelay());
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.agedOwn[kHero] == 0);
    UntilTurnOf(w, kHero);
    CHECK(w.agedOwn[kHero] == 0); // its turn, put off, comes now: whole
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    CHECK(w.agedOwn[kHero] == 4000);
}

TEST_CASE("with own clocks, an action's own effect moves its creature's clock and its target's, nobody else's",
          "[time]")
{
    // An ogre's grab holds the ogre and its victim, an effect on each; the
    // command ends when the victim's does, on the victim's clock.
    std::vector<Creature> fight = StandardFight();
    fight.at(1).dexterity = 30.0f; // the genlock first
    World w(Timed(), fight);
    bool poked = false;
    w.onPoke = [&](ObjectId id) {
        if (poked)
        {
            return;
        }
        poked = true;
        CommandRequest grab;
        grab.object = id;
        grab.target = kHero;
        grab.type = CommandType::UseAbility;
        grab.source = Source::Script;
        grab.ability = 90036;
        grab.inCombat = true;
        grab.actionSeconds = 2.0f;
        CHECK(w.Queue(grab) == Verdict::Allow);
    };
    w.StartFight();
    for (int i = 0; i < 20 && !poked; ++i)
    {
        w.Frame();
    }
    REQUIRE(poked);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(200);
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    const std::int64_t hero = w.agedOwn[kHero];
    const std::int64_t companion = w.agedOwn[kCompanion];
    int frames = 0;
    for (; frames < 2000 && w.Current() == kGenlock; ++frames)
    {
        // The grab lets go once three seconds of the victim's time have run.
        if (w.Get(kGenlock).busy && w.agedOwn[kHero] - hero >= 3000)
        {
            w.Finish(kGenlock);
        }
        w.Frame();
    }
    CHECK(w.Current() != kGenlock);
    CHECK(frames * 16 < 6000); // its three seconds and the turn's settling, not its patience (36 s)
    CHECK(w.agedOwn[kHero] - hero >= 3000);
    CHECK(w.agedOwn[kHero] - hero < 3600);
    CHECK(w.agedOwn[kCompanion] == companion); // nobody else's
}
