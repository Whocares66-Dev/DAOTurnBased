// A turn as a bar of time (dev/REBALANCE.md, "Current
// specification"), driven through scripted fights in the stand-in for the
// game (World.h): actions spending their own seconds, walking at a pace,
// an action that does not fit kept for the next turn, rollover, Combat
// Tactics, an AI choosing while it has time.
#include "World.h"

#include "core/Bar.h"
#include "core/Rules.h"
#include "core/State.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// 4 s turns, a pace of 5 m/s for round numbers, a meter free.
Settings Timed()
{
    Settings s = Fixed();
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
    // As many half meters as it takes: counted, where a float counted them
    // before (the same steps: halves add up exactly).
    const int steps = static_cast<int>(std::ceil((meters - 0.001f) / 0.5f));
    for (int step = 0; step < steps; ++step)
    {
        const float done = 0.5f * static_cast<float>(step);
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

// An attack at someone, as the gate's request names its target: in reach,
// nothing to walk.
Verdict Strike(World &w, ObjectId id, ObjectId target, Source source, float seconds)
{
    CommandRequest request;
    request.object = id;
    request.type = CommandType::Attack;
    request.source = source;
    request.inCombat = w.Get(id).fighting;
    request.walk = 0.0f;
    request.actionSeconds = seconds;
    request.target = target;
    return w.Queue(request);
}

// Those a kept action's release named for it to be turned on (the last
// release for that creature).
std::vector<ObjectId> NamedFor(const World &w, ObjectId id)
{
    std::vector<ObjectId> others;
    for (const Effect &e : w.applied)
    {
        if (e.kind == EffectKind::ReleaseCast && e.target == id)
        {
            others = e.others;
        }
    }
    return others;
}

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("with time, a swing spends its seconds and the turn goes on until the bar is spent", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
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
    // No enemy within her reach to turn the attack on (issue 22): the
    // hurlock stands ten metres off.
    w.Get(kGenlock).position = {1.5f, 0.0f, 0.0f};
    w.Get(kHurlock).position = {10.0f, 0.0f, 0.0f};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 3.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 2.0f) == Verdict::Hold); // owes 1 s
    w.RunFor(900);
    REQUIRE(w.Current() != kHero);
    w.Get(kGenlock).down = true; // its target falls before her next turn
    w.targetGone.insert(kHero);
    UntilTurnOf(w, kHero);
    w.RunFor(1200);
    // Not given, the second she paid for it back, and her turn hers, handed
    // over as a fresh one is: she waits for an order.
    CHECK(NamedFor(w, kHero) == std::vector<ObjectId>{kHurlock});
    CHECK(w.released.empty());
    CHECK(w.turned.empty());
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == "its target is gone";
    }));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::AwaitingOrder && e.who == kHero && e.round == 2 &&
               e.detail == "the turn is its player's" && e.value == 4000;
    }));
    CHECK(Logged(w, "Hero's pending attack is dropped: its target is gone, and no enemy is in its reach; the 1.00 s it "
                    "owed given back"));
    // And the order she gives then is taken, at the hurlock she must walk to.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 8.0f) == Verdict::Allow);
}

TEST_CASE("with time, a kept attack whose target fell is turned on the nearest enemy in reach, for nothing", "[time]")
{
    // Issue 22, the user's choice of 2026-10-03: "as the game's own
    // auto-attack would". Dropped, the turn began with the decision lost.
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.Get(kGenlock).position = {1.5f, 0.0f, 0.0f};
    w.Get(kHurlock).position = {0.0f, 1.8f, 0.0f};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 3.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 2.0f) == Verdict::Hold); // owes 1 s
    w.RunFor(900);
    REQUIRE(w.Current() != kHero);
    w.Get(kGenlock).down = true; // to another's blow, before her next turn
    w.targetGone.insert(kHero);
    UntilTurnOf(w, kHero);
    // Named: the enemies still standing, not the fallen one, not her own
    // side. The hurlock is within her reach, and the attack is given at it.
    CHECK(NamedFor(w, kHero) == std::vector<ObjectId>{kHurlock});
    REQUIRE(w.turned.size() == 1);
    CHECK(w.turned.front() == std::pair{kHero, kHurlock});
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.Get(kHero).busy);
    CHECK(w.Get(kHero).current == CommandType::Attack);
    // For nothing more: the second it owed was paid as the turn began, and
    // stays paid.
    CHECK(w.machine.View().timeLeftMs == 3000);
    CHECK(w.machine.View().pendingPaidMs == 1000);
    CHECK(Logged(w, "Hero's pending attack is turned on Hurlock: its target has fallen"));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionLanded && e.who == kHero && e.detail == "turned on another" &&
               e.note == "Hurlock" && e.command == "attack";
    }));
    CHECK(std::ranges::none_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::ActionDropped && e.who == kHero; }));
    // The swing over, the rest of the turn is hers, as after any kept action.
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 3000);
    CHECK(Strike(w, kHero, kHurlock, Source::Player, 2.0f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 1000);
}

TEST_CASE("a kept attack given at its own target is not said to be turned", "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 3.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 2.0f) == Verdict::Hold);
    UntilTurnOf(w, kHero);
    REQUIRE(w.released.size() == 1);
    CHECK(w.turned.empty());
    CHECK_FALSE(Logged(w, "is turned on"));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionLanded && e.who == kHero && e.detail.empty() && e.note.empty();
    }));
}

TEST_CASE("an enemy's kept attack whose target fell is turned on the nearest of the party and its allies", "[time]")
{
    // For everyone (the user): "enemy" is the attacker's own.
    constexpr ObjectId kSoldier = 0x300;
    std::vector<Creature> fight = StandardFight();
    fight.push_back(Make(kSoldier, Side::Ally, false, L"Soldier", 12.0f)); // after Alistair, before the hurlock
    World w(Timed(), fight);
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    // The genlock among them: Alistair and the soldier at hand, the hero ten
    // metres off, the hurlock at its side.
    w.Get(kCompanion).position = {1.0f, 0.0f, 0.0f};
    w.Get(kSoldier).position = {0.0f, 1.5f, 0.0f};
    w.Get(kHero).position = {10.0f, 0.0f, 0.0f};
    w.Get(kHurlock).position = {0.0f, -1.0f, 0.0f};
    bool kept = false;
    w.onPoke = [&](ObjectId id) {
        // Its first choice: a swing too long for its bar, kept, owing 2 s.
        if (id == kGenlock && !std::exchange(kept, true))
        {
            CHECK(Strike(w, kGenlock, kCompanion, Source::Script, 6.0f) == Verdict::Hold);
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(kept);
    REQUIRE(w.heldCasts.contains(kGenlock));
    w.Get(kCompanion).down = true; // Alistair falls before it lands
    w.targetGone.insert(kGenlock);
    UntilTurnOf(w, kGenlock);
    // Named: the party and its allies still standing, in the order's order;
    // not Alistair, fallen, nor the hurlock, its own side.
    CHECK(NamedFor(w, kGenlock) == std::vector<ObjectId>{kHero, kSoldier});
    // The hero is out of its reach; the soldier is not.
    REQUIRE(w.turned.size() == 1);
    CHECK(w.turned.front() == std::pair{kGenlock, kSoldier});
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    CHECK(w.machine.View().timeLeftMs == 2000); // what it owed paid, and no more
    CHECK(Logged(w, "Genlock's pending attack is turned on Soldier: its target has fallen"));
    CHECK(std::ranges::any_of(events, [&](const TurnEvent &e) {
        return e.kind == EventKind::ActionLanded && e.who == kGenlock && e.detail == "turned on another" &&
               e.note == "Soldier";
    }));
}

TEST_CASE("a kept talent whose target fell is dropped, an enemy in reach or not: its target was chosen", "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.Get(kGenlock).position = {1.5f, 0.0f, 0.0f};
    w.Get(kHurlock).position = {0.0f, 1.8f, 0.0f};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Strike(w, kHero, kGenlock, Source::Player, 3.0f) == Verdict::Allow);
    Done(w, kHero);
    CommandRequest talent;
    talent.object = kHero;
    talent.type = CommandType::UseAbility;
    talent.source = Source::Player;
    talent.inCombat = true;
    talent.walk = 0.0f;
    talent.ability = 5;
    talent.abilityType = kAbilityTalent;
    talent.actionSeconds = 2.0f;
    talent.target = kGenlock;
    CHECK(w.Queue(talent) == Verdict::Hold); // owes 1 s
    w.RunFor(900);
    REQUIRE(w.Current() != kHero);
    w.Get(kGenlock).down = true;
    w.targetGone.insert(kHero);
    UntilTurnOf(w, kHero);
    w.RunFor(1200);
    // Nobody is named for it, the hurlock at her side or not: it is not
    // given, and what it owed is given back.
    CHECK(Released(w, kHero));
    CHECK(NamedFor(w, kHero).empty());
    CHECK(w.turned.empty());
    CHECK(w.released.empty());
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == "its target is gone" && e.value == 5;
    }));
    CHECK(Logged(w, "Hero's pending use ability is dropped: its target is gone; the 1.00 s it owed given back"));
}

TEST_CASE("an AI whose kept attack has nobody in reach to be turned on is told to choose at once, its bar whole",
          "[time]")
{
    // The user's question of issue 22, 2026-10-03: "what if there are no
    // enemies within range? Does the AI need some kind of a kick to continue
    // acting and not just stand there?" It gets one: what its turn paid is
    // given back and the turn is handed over as any fresh turn is, which for
    // an AI is being told to choose.
    World w(Timed());
    w.Get(kHero).position = {10.0f, 0.0f, 0.0f};
    w.Get(kCompanion).position = {1.0f, 0.0f, 0.0f};
    int pokes = 0;
    std::uint32_t barAsToldAgain = 0;
    Verdict chosenAnew = Verdict::Block;
    w.onPoke = [&](ObjectId id) {
        if (id != kGenlock)
        {
            return;
        }
        if (++pokes == 1)
        {
            // A swing too long for its bar, kept, owing 2 s.
            CHECK(Strike(w, kGenlock, kCompanion, Source::Script, 6.0f) == Verdict::Hold);
        }
        else if (pokes == 2)
        {
            // Told again: it chooses anew, the hero this time, and walks
            // the eight metres to him.
            barAsToldAgain = w.machine.View().timeLeftMs;
            chosenAnew = Act(w, kGenlock, CommandType::Attack, Source::Script, 2.0f, 8.0f);
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(pokes == 1);
    REQUIRE(w.heldCasts.contains(kGenlock));
    w.Get(kCompanion).down = true; // its target falls, and the hero is far
    w.targetGone.insert(kGenlock);
    UntilTurnOf(w, kGenlock);
    // Its turn has begun, 2 s of the bar paid for the attack that will not
    // come. Not given: nobody it names is in its reach.
    REQUIRE(w.Current() == kGenlock);
    CHECK(NamedFor(w, kGenlock) == std::vector<ObjectId>{kHero});
    CHECK(w.turned.empty());
    CHECK(w.released.empty());
    REQUIRE(pokes == 1);
    // Two frames and it is told to choose: the step that hears its attack
    // was not given, and the next, with a fresh look at everyone. (Were it
    // not handed over there, the settling that follows a kept action that
    // never came would still hand it over: a frame later, and two frames
    // later before an action's end was looked at at once, issue 23.)
    w.Frame();
    REQUIRE(pokes == 1);
    w.Frame();
    REQUIRE(pokes == 2);
    CHECK(barAsToldAgain == 4000); // what it paid given back: the bar whole
    CHECK(chosenAnew == Verdict::Allow);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    CHECK(Logged(w, "Genlock's pending attack is dropped: its target is gone, and no enemy is in its reach; the 2.00 s "
                    "it owed given back"));
    CHECK_FALSE(Logged(w, "Genlock chooses again"));
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

// Time rolls over only from a turn its actor ended itself (issue 29; the
// user, 2026-10-04: "When turn is skipped, due to unawareness, incapacity,
// etc, there should be no time rollover, because the actor did not explicitly
// save any time"). core/TurnEnd.h has each reason's class; these are the
// machine's turns ending for them, and the next turn's bar saying what came
// over. A turn is 4 s here and a second may roll over.
namespace
{
// Each turn's bar as it began, and why each turn ended, by whose it was.
struct Turns
{
    explicit Turns(World &w)
    {
        w.machine.SetEventSink([this, &w](const TurnEvent &e) {
            if (e.kind == EventKind::TurnStarted)
            {
                bars[e.who].push_back(w.machine.View().timeMs);
            }
            if (e.kind == EventKind::TurnEnded)
            {
                ends[e.who].push_back(e.detail);
            }
        });
    }
    std::unordered_map<ObjectId, std::vector<std::uint32_t>> bars;
    std::unordered_map<ObjectId, std::vector<std::string>> ends;
};

// The player's turn ended, and the fight on to the hero's next.
void RoundOn(World &w)
{
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    UntilTurnOf(w, kHero);
}
} // namespace

TEST_CASE("with time, an AI that waits or is done with time left rolls it over, as the player's End does", "[time]")
{
    World w(Timed());
    Turns turns(w);
    // The genlock swings once (1.5 s of its 4) and then waits, as its AI does
    // when a command of its ends with nothing more to choose; the hurlock
    // waits as it is told its turn has come.
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            CHECK(Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f) == Verdict::Allow);
        }
        else
        {
            CHECK(w.Order(id, CommandType::Wait, Source::Script) == Verdict::Block);
        }
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    w.Finish(kGenlock);
    CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Block);
    UntilTurnOf(w, kHero);
    CHECK(turns.ends[kHero] == std::vector<std::string>{"ended by the player"});
    CHECK(turns.ends[kGenlock] == std::vector<std::string>{"done"});
    CHECK(turns.ends[kHurlock] == std::vector<std::string>{"waits"});
    CHECK(Logged(w, "Genlock's turn ends: done; 1.00 s rolls over"));
    CHECK(Logged(w, "Hurlock's turn ends: waits; 1.00 s rolls over"));
    // Each begins its next turn with the second it saved.
    CHECK(w.machine.View().timeMs == 5000);
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().timeMs == 5000);
    w.Finish(kGenlock);
    CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Block);
    UntilTurnOf(w, kHero);
    CHECK(turns.bars[kHurlock] == std::vector<std::uint32_t>{4000, 5000});
}

TEST_CASE("with time, an AI that chose nothing rolls nothing over", "[time]")
{
    // The issue's own line: `Skeleton's turn ends: chose nothing; 1.00 s
    // rolls over`, every round, its bar a second longer for having done
    // nothing.
    World w(Timed());
    Turns turns(w);
    w.onPoke = [&](ObjectId id) {
        if (id == kHurlock)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    w.StartFight();
    RoundOn(w);
    RoundOn(w);
    REQUIRE(turns.ends[kGenlock].size() == 2);
    CHECK(turns.ends[kGenlock].front() == "chose nothing");
    CHECK(turns.bars[kGenlock] == std::vector<std::uint32_t>{4000, 4000});
    CHECK(Logged(w, "Genlock's turn ends: chose nothing; nothing rolls over, its actor not having ended it (1.00 s "
                    "would have)"));
    CHECK_FALSE(Logged(w, "Genlock's turn ends: chose nothing; 1.00 s rolls over"));
    // The others, who ended theirs themselves, have their second.
    CHECK(turns.bars[kHero].at(1) == 5000);
    CHECK(turns.bars[kCompanion].at(1) == 5000);
    CHECK(turns.bars[kHurlock].at(1) == 5000);
}

TEST_CASE("with time, a turn ended because its command never ended rolls nothing over", "[time]")
{
    Settings s = Timed();
    s.maxActionMs = 2000;
    World w(s);
    Turns turns(w);
    // A swing of a second that the engine never ends: stopped, 3 s unspent.
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            CHECK(Act(w, id, CommandType::Attack, Source::Script, 1.0f, 0.0f) == Verdict::Allow);
        }
        else
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    w.StartFight();
    RoundOn(w);
    REQUIRE_FALSE(turns.ends[kGenlock].empty());
    CHECK(turns.ends[kGenlock].front() == "took too long");
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().timeMs == 4000);
}

TEST_CASE("with time, a turn lost to an effect that holds its creature rolls nothing over", "[time]")
{
    // The issue's other line: `Alistair's turn ends: held by an effect; 1.00
    // s rolls over`. (Since issue 30 the hold has taken the bar by the time
    // the turn is lost to it, so there is nothing to roll over on either
    // count: not after two seconds with the bar whole, as that line was.)
    World w(Timed());
    Turns turns(w);
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // An effect's command no condition names (a grab), there as its turn comes.
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(4000 + 200); // the hold outlasts its bar
    REQUIRE(w.Current() != kGenlock);
    REQUIRE_FALSE(turns.ends[kGenlock].empty());
    CHECK(turns.ends[kGenlock].front() == "held by an effect");
    CHECK(Logged(w, "Genlock's turn ends: held by an effect; nothing rolls over"));
    // Let go, its next turn is a turn's own seconds (and that one, which it
    // ends itself by waiting, rolls its second over).
    w.Finish(kGenlock);
    UntilTurnOf(w, kHero);
    RoundOn(w);
    RoundOn(w);
    CHECK(turns.bars[kGenlock] == std::vector<std::uint32_t>{4000, 4000, 5000});
}

TEST_CASE("with time, a creature that falls in its own turn rolls nothing over", "[time]")
{
    World w(Timed());
    Turns turns(w);
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Her bar whole, and she falls (a poison's tick): the turn was not hers
    // to end.
    w.Get(kHero).down = true;
    w.RunFor(300);
    REQUIRE(w.Current() != kHero);
    REQUIRE_FALSE(turns.ends[kHero].empty());
    CHECK(turns.ends[kHero].front() == "fell");
    // Up again (revived), her next turn has no second more.
    w.Get(kHero).down = false;
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().timeMs == 4000);
}

TEST_CASE("with time, an AI stopped with nothing it can get to rolls nothing over, its time given back or not",
          "[time]")
{
    World w(Timed());
    Turns turns(w);
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            (void)Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f);
        }
        else
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    // No room at its target, twice: both swings' time is back in its bar,
    // and its turn is ended for it.
    for (int tries = 0; tries < 2; ++tries)
    {
        REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
        w.Finish(kGenlock);
        w.RunFor(1500);
    }
    REQUIRE(w.Current() != kGenlock);
    REQUIRE(turns.ends[kGenlock].size() == 1);
    CHECK(turns.ends[kGenlock].front() == "nothing it can get to");
    UntilTurnOf(w, kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().timeMs == 4000);
}

// Nothing counts the commands an AI starts in a turn (issue 38). Until
// 2026-10-04 max_commands_per_turn, six, was "a guard against an AI that
// loops". The user, shown a turn of play the guard had ended: "What is the
// command cap? Do we need to limit the number of commands that can be
// issued? I thought we relied on time as the limiting factor", and: "Yeah
// let's get rid of the cap. Most actions should have some kind of cooldown.
// And I don't think the AI itself is going to do things like swap weapons
// endlessly."
//
// That turn, in a fight on a road (the one turn of its 197 AI turns in which
// the guard spoke: an allied soldier's, as the last enemy fell):
//
//   round 14: Redcliffe Soldier's turn (ally, AI), a bar of 4.00 s, 0.84 s of
//       it paid for its pending use ability: 3.16 s and 15.7 m left
//   Redcliffe Soldier: use ability (action, from script); 3.16 s ...
//   Redcliffe Soldier: attack (action, from script); 1.07 s and 6.0 m left
//   Redcliffe Soldier: sheathe or draw (free, from engine); 1.07 s ...
//   Redcliffe Soldier's wait of 2.00 s is longer than the 1.07 s it has
//       left: its turn ends
//   (sheathe or draw let through, and the wait's line, twice more)
//   Redcliffe Soldier: sheathe or draw (free, from engine); 1.07 s ...
//   Redcliffe Soldier has started 6 commands this turn: no more  (11 times)
//   Redcliffe Soldier's turn ends: waits; 1.00 s rolls over
//
// Its actor had ended that turn with its wait. What the engine queued on it
// while it settled was let through, each time putting the turn back at its
// command, and the count was what stopped that. With the count gone a turn
// its AI has passed takes nothing more of its own: the wait is the turn's
// end, said once.
namespace
{
auto Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("with time, a turn its AI ended with a wait ends so, whatever the engine and its script queue as it settles",
          "[time][wait]")
{
    World w(Timed());
    Turns turns(w);
    // What its script does each time it runs with nobody to fight, as the
    // soldier's did: the engine puts its weapon away or draws it, and it
    // waits two seconds.
    const auto idles = [&w](Verdict weapon) {
        CHECK(w.Order(kGenlock, CommandType::SheatheWeapons, Source::Engine) == weapon);
        CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block);
    };
    bool kept = false;
    bool ended = false; // its second turn, its wait said: what its script queues from here is refused
    int toldAfter = 0;
    w.onPoke = [&](ObjectId id) {
        if (id != kGenlock)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
        else if (!std::exchange(kept, true))
        {
            // Its first turn: a swing of 4.84 s on a bar of 4, kept.
            CHECK(Strike(w, kGenlock, kHero, Source::Script, 4.84f) == Verdict::Hold);
        }
        else if (ended)
        {
            // Told as what it kept has landed and the turn settles: in a
            // turn it has ended by then.
            idles(Verdict::Block);
            ++toldAfter;
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(kept);
    // Its next turn: 0.84 s of its bar paid for what it kept, which lands.
    UntilTurnOf(w, kGenlock);
    REQUIRE(w.released.size() == 1);
    REQUIRE(w.released.front() == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 3160);
    // As that ends its script swings again, 2.09 s.
    w.Finish(kGenlock);
    REQUIRE(Strike(w, kGenlock, kHero, Source::Script, 2.09f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 1070);
    w.RunFor(500);
    // The swing ends. The engine's weapon command is let through, as on any
    // turn of its own, and its wait does not fit: its turn ends, by its own
    // doing.
    w.Finish(kGenlock);
    idles(Verdict::Allow);
    CHECK(Logged(w, "Genlock's wait of 2.00 s is longer than the 1.07 s it has left: its turn ends"));
    ended = true;
    // As each command of the engine's ends its script runs again. Nothing
    // more is let through, the engine's neither: the turn is over, and its
    // creature is not kept at one weapon command after another.
    for (int again = 0; again < 3; ++again)
    {
        w.Frame();
        w.Finish(kGenlock);
        idles(Verdict::Block);
        CHECK_FALSE(w.Get(kGenlock).busy);
    }
    CHECK(w.Current() == kGenlock);
    w.RunFor(300);
    CHECK(w.Current() != kGenlock);
    CHECK(toldAfter == 1);
    ended = false;
    // Ended as its actor ended it, and the second it saved is its own.
    // Nothing else is said: the wait's line once, the one weapon command
    // that came before it, and no word of a count.
    CHECK(turns.ends[kGenlock] == std::vector<std::string>{"its action pending", "waits"});
    CHECK(Logged(w, "Genlock's turn ends: waits; 1.00 s rolls over"));
    CHECK(Lines(w, "it has left: its turn ends") == 1);
    CHECK(Lines(w, "Genlock: sheathe or draw (free, from engine)") == 1);
    CHECK_FALSE(Logged(w, "commands this turn"));
    UntilTurnOf(w, kGenlock);
    CHECK(w.machine.View().timeMs == 5000);
    CHECK(w.machine.View().rolledMs == 1000);
}

TEST_CASE("with time, an AI that starts many cheap commands in a long bar is let, until its time is spent", "[time]")
{
    // Twelve swings of half a second in a bar of 6 s, its script queuing the
    // next as each ends: twice what max_commands_per_turn let through. Each
    // takes its time, and the bar is what ends the turn.
    Settings s = Timed();
    s.secondsPerTurn = 6.0f;
    World w(s);
    Turns turns(w);
    int swings = 0;
    const auto swing = [&w, &swings] {
        if (Act(w, kGenlock, CommandType::Attack, Source::Script, 0.5f, 0.0f) == Verdict::Allow)
        {
            ++swings;
        }
    };
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            swing();
        }
        else
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(swings == 1);
    std::vector<std::uint32_t> bar;
    for (int again = 0; again < 100 && w.Current() == kGenlock; ++again)
    {
        w.Finish(kGenlock);
        const int before = swings;
        swing();
        if (swings > before)
        {
            bar.push_back(w.machine.View().timeLeftMs);
        }
        w.Frame();
    }
    CHECK(swings == 12);
    CHECK(bar == std::vector<std::uint32_t>{5000, 4500, 4000, 3500, 3000, 2500, 2000, 1500, 1000, 500, 0});
    CHECK(w.Current() != kGenlock);
    CHECK(turns.ends[kGenlock] == std::vector<std::string>{"done"});
    CHECK(Logged(w, "Genlock's turn ends: done; 0.00 s rolls over"));
    CHECK_FALSE(Logged(w, "commands this turn"));
}

TEST_CASE("with time, what its creature kept is given though its script waited first, and so is the walk to its "
          "kept attack's target",
          "[time][wait]")
{
    // A turn its AI has passed takes nothing more of its script's or the
    // engine's. Not so what the mod itself gives its creature: neither is a
    // choice of its AI's that came after the wait.
    SECTION("a kept action, its script's wait come as the turn began, before the game layer gave it")
    {
        World w(Timed());
        bool kept = false;
        w.onPoke = [&](ObjectId id) {
            if (id != kGenlock)
            {
                (void)w.Order(id, CommandType::Wait, Source::Script);
            }
            else if (!std::exchange(kept, true))
            {
                CHECK(Strike(w, kGenlock, kHero, Source::Script, 4.5f) == Verdict::Hold); // owes half a second
            }
        };
        w.StartFight();
        UntilTurnOf(w, kGenlock);
        w.RunFor(200);
        REQUIRE(kept);
        int waited = 0;
        w.onEffect = [&](const Effect &e) {
            if (e.kind == EffectKind::ReleaseCast && e.target == kGenlock)
            {
                // Longer than the 3.5 s its bar has: the pass a wait was.
                CHECK(w.Wait(kGenlock, 9.0f) == Verdict::Block);
                ++waited;
            }
        };
        UntilTurnOf(w, kGenlock);
        REQUIRE(waited == 1);
        CHECK(Logged(w, "Genlock's wait of 9.00 s is longer than the 3.50 s it has left: its turn ends"));
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Allow);
        CHECK(w.Get(kGenlock).current == CommandType::Attack);
    }
    SECTION("the walk to its kept attack's target, its script's wait come in the frame the attack was kept")
    {
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            if (id != kGenlock)
            {
                (void)w.Order(id, CommandType::Wait, Source::Script);
            }
            else if (++pokes == 1)
            {
                // 4.5 s of swing and 5 m to walk on a bar of 4 s: kept, and
                // it walks to its target now; and its script, run again,
                // waits, with nothing left to wait with.
                CHECK(Act(w, id, CommandType::Attack, Source::Script, 4.5f, 5.0f) == Verdict::Hold);
                CHECK(w.Wait(id, 2.0f) == Verdict::Block);
            }
        };
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        (void)w.machine.OnEndTurn();
        w.RunFor(300);
        REQUIRE(pokes == 1);
        CHECK(Logged(w, "Genlock's wait of 2.00 s is longer than the 0.00 s it has left: its turn ends"));
        CHECK(w.approaches == 1);
        CHECK(w.Current() == kGenlock);
        CHECK(w.Get(kGenlock).busy);
        CHECK(w.Get(kGenlock).current == CommandType::MoveToObject);
    }
}

// A turn its AI plays has a time limit (issue 38): max_turn_ms, thirty
// seconds of the wall's time, counted from when the turn began. Still going
// then, it is ended whatever it is doing, "took too long", as an action past
// max_action_ms is. It bounds what nothing else does, a turn of steps that
// take nothing of its bar, which the count of a turn's commands ended until
// it went. The user, of the rules built for those first (2026-10-04): "How
// about for non-party members we apply a timeout? Don't we already have one?
// I'm skeptical about hacking in all these conditions. We should keep things
// simple."
namespace
{
// A genlock whose script answers every telling with `choose`; the others
// wait as their turns come. The hero's first turn is ended, and the genlock
// has been told.
void ToTheGenlocksTurn(World &w, int &told, std::function<void(ObjectId)> choose)
{
    w.onPoke = [&w, &told, choose = std::move(choose)](ObjectId id) {
        if (id != kGenlock)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
            return;
        }
        ++told;
        choose(id);
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(told == 1);
}

// What a script does in a turn that would not end by itself: when its
// creature is told to choose, and with every frame of its turn after.
struct Endless
{
    const char *what;
    std::function<void(World &, ObjectId)> told;
    std::function<void(World &, ObjectId, int)> frame;
};

std::vector<Endless> EndlessTurns()
{
    const auto nothing = [](World &, ObjectId, int) {};
    // A command let through; over at once, or left running.
    const auto queue = [](CommandType type, Source source, bool over) {
        return [=](World &w, ObjectId id) {
            CHECK(w.Order(id, type, source) == Verdict::Allow);
            if (over)
            {
                w.Finish(id);
            }
        };
    };
    // The last one over and the same queued again, every frame: the turn is
    // never without a command, and never settles.
    const auto again = [](CommandType type, Source source) {
        return [=](World &w, ObjectId id, int) {
            w.Finish(id);
            CHECK(w.Order(id, type, source) == Verdict::Allow);
        };
    };
    return {
        // Told again with time left, and answering each telling with what
        // takes nothing of its bar (215 times in two minutes, tried with the
        // count gone and no limit).
        {"a weapon set switched each time it is told", queue(CommandType::SwitchWeaponSets, Source::Script, true),
         nothing},
        {"a move over where it stands each time it is told", queue(CommandType::MoveToLocation, Source::Script, true),
         nothing},
        // It stands two and a half seconds at the walk, is stopped, and is
        // told again (39 times in two minutes).
        {"a walk it cannot make each time it is told", queue(CommandType::MoveToLocation, Source::Script, false),
         nothing},
        // Queued again by its script as each ends: nothing is told, nothing
        // stands and nothing fails.
        {"a weapon set switched again as each ends", queue(CommandType::SwitchWeaponSets, Source::Script, false),
         again(CommandType::SwitchWeaponSets, Source::Script)},
        {"a move that ends where it began, again as each ends",
         queue(CommandType::MoveToLocation, Source::Script, false), again(CommandType::MoveToLocation, Source::Script)},
        {"the engine's own weapon commands, one as each ends",
         queue(CommandType::SheatheWeapons, Source::Engine, false), again(CommandType::SheatheWeapons, Source::Engine)},
        // The engine failed attacks within 2 ms before issue 27 had the gate
        // refuse them unasked: no time runs between the two, each is given
        // back whole, and the turn never settles for the count of them to be
        // looked at.
        {"an attack the engine fails in the frame it was let through, and a weapon set switched, by turns",
         queue(CommandType::SwitchWeaponSets, Source::Script, false),
         [](World &w, ObjectId id, int) {
             w.Finish(id);
             CHECK(Strike(w, id, kHero, Source::Script, 1.0f) == Verdict::Allow);
             (void)w.Fail(id, "COMMAND_FAILED_INVALID_PATH");
             CHECK(w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
         }},
        // The engine's own stuck timer gives a walk up after a second, and
        // its script queues the same.
        {"a walk the engine fails after its own second, queued again as each fails",
         queue(CommandType::MoveToLocation, Source::Script, false),
         [](World &w, ObjectId id, int frame) {
             if (frame % 62 == 0)
             {
                 (void)w.Fail(id, "COMMAND_FAILED_INVALID_PATH");
                 CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
             }
         }},
    };
}
} // namespace

TEST_CASE("a turn an AI plays that would not end by itself is ended at max_turn_ms, whatever it is doing",
          "[time][machine][limit]")
{
    for (const Endless &endless : EndlessTurns())
    {
        CAPTURE(endless.what);
        World w(Timed());
        Turns turns(w);
        int told = 0;
        ToTheGenlocksTurn(w, told, [&](ObjectId id) { endless.told(w, id); });
        w.applied.clear();
        std::uint32_t ran = 0;
        for (int frame = 1; ran < 60000 && w.Current() == kGenlock; ran += 16, ++frame)
        {
            endless.frame(w, kGenlock, frame);
            w.Frame();
        }
        // Thirty seconds from when its turn began, which was within the
        // 0.3 s before this count of them.
        CHECK(ran > 29700 - 16);
        CHECK(ran <= 30000 + 16);
        CHECK(turns.ends[kGenlock] == std::vector<std::string>{"took too long"});
        CHECK(Lines(w, "Genlock's turn is still going after 30000 ms, ") == 1);
        // What it was at is stopped, and nothing of its bar is saved.
        CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
        CHECK_FALSE(w.Get(kGenlock).busy);
        CHECK(Logged(w, "Genlock's turn ends: took too long; nothing rolls over"));
        // The fight goes on, and the next one's time is its own: the
        // hurlock is told, and waits.
        UntilTurnOf(w, kGenlock);
        CHECK(turns.ends[kHurlock] == std::vector<std::string>{"waits"});
        CHECK(Lines(w, "turn is still going after") == 1);
    }
}

TEST_CASE("max_turn_ms is of the wall's time, as max_action_ms is: at three times the speed as long on the wall",
          "[time][limit]")
{
    for (const float speed : {1.0f, 3.0f})
    {
        CAPTURE(speed);
        Settings s = Timed();
        s.fightSpeed = speed;
        s.maxTurnMs = 3000;
        World w(s);
        Turns turns(w);
        int told = 0;
        ToTheGenlocksTurn(w, told, [&w](ObjectId id) {
            CHECK(w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
        });
        std::uint32_t ran = 0;
        for (; ran < 20000 && w.Current() == kGenlock; ran += 16)
        {
            w.Finish(kGenlock);
            CHECK(w.Order(kGenlock, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
            w.Frame();
        }
        // Three seconds of frames at either speed, less the 0.3 s (of the
        // wall's time at 1, a third of it at 3) its turn had run by then.
        CHECK(ran > 2700 - 16);
        CHECK(ran <= 3000 + 16);
        CHECK(turns.ends[kGenlock] == std::vector<std::string>{"took too long"});
        CHECK(Logged(w, std::format("Genlock's turn is still going after {} ms, ", speed > 1.0f ? 9000 : 3000)));
    }
}

TEST_CASE("a turn the player plays has no time limit; a companion's has one when its tactics play it", "[time][limit]")
{
    SECTION("the hero's, 33 s of it at three walks: still hers")
    {
        World w(Timed());
        Turns turns(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        // Three walks of eleven seconds each, creeping: none stands long
        // enough to be stopped, none is as long as max_action_ms.
        for (int walk = 0; walk < 3; ++walk)
        {
            REQUIRE(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 1.0f) == Verdict::Allow);
            w.Get(kHero).moving = true;
            for (int ms = 0; ms < 11000; ms += 16)
            {
                if (ms % 2000 == 0)
                {
                    w.Get(kHero).position.x += 0.06f;
                }
                w.Frame();
            }
            w.Get(kHero).moving = false;
            w.Finish(kHero);
            w.RunFor(600);
            REQUIRE(w.Current() == kHero);
        }
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(turns.ends[kHero].empty());
        CHECK_FALSE(Logged(w, "turn is still going after"));
    }
    SECTION("a companion's, played by its tactics: an AI's turn, and ended like one")
    {
        Settings s = Timed();
        s.partyControl = PartyControl::Tactics;
        World w(s);
        Turns turns(w);
        w.onPoke = [&w](ObjectId id) {
            if (id != kCompanion)
            {
                (void)w.Order(id, CommandType::Wait, Source::Script);
                return;
            }
            CHECK(w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
            w.Finish(id);
        };
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        std::uint32_t ran = 0;
        for (; ran < 60000 && w.Current() == kCompanion; ran += 16)
        {
            w.Frame();
        }
        CHECK(ran > 29000);
        CHECK(ran <= 30000 + 16);
        CHECK(turns.ends[kCompanion] == std::vector<std::string>{"took too long"});
        CHECK(Logged(w, "Alistair's turn is still going after 30000 ms, "));
    }
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
    // And every time it fails so: nothing is charged for what did not
    // happen.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "no room in the melee ring"));
    CHECK(w.machine.View().timeLeftMs == 4000);
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
    // Read with a value to fall back on: the analyser does not know that a
    // REQUIRE ends the case, and took the cooldown read after one for a
    // value nobody had set.
    REQUIRE(left(kHero));
    const std::uint32_t afterTheWalk = left(kHero).value_or(0);
    CHECK(afterTheWalk >= 7990);
    CHECK(afterTheWalk <= 8010);
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
    const std::uint32_t afterTheLostTurn = left(kHero).value_or(0);
    CHECK(afterTheLostTurn >= 2990);
    CHECK(afterTheLostTurn <= 3010);
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
    // told to choose again, on a look that still shows it free. (Since
    // issue 23 the look taken as its action ends sees this one; the case
    // after this is of a stun that lands later.)
    w.Finish(kGenlock);
    w.Get(kGenlock).condition = Condition::Disabled;
    w.Get(kGenlock).conditionSeconds = 3.0f;
    w.RunFor(160);
    CHECK(pokes == 1);
    CHECK(w.Current() != kGenlock); // the rest of its turn is the stun's
}

TEST_CASE("with time, an AI stunned while the world settles after its action is not told to choose again", "[time]")
{
    // The look taken as its action ends shows it free; the stun lands after
    // it, while the world settles (a riposte, an arrow in flight). It is told
    // to choose again only on a look taken with that step, which sees it.
    Settings s = Timed();
    s.settleMs = 0;
    s.aiFollowUpMs = 200; // well short of the half second's own look
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
    w.Finish(kGenlock);
    const int looks = w.statusReads;
    w.RunFor(48); // two frames idle, and the look at its action's end
    REQUIRE(w.statusReads == looks + 1);
    REQUIRE(w.machine.GetPhase() == Phase::Settling);
    w.Get(kGenlock).condition = Condition::Disabled;
    w.Get(kGenlock).conditionSeconds = 3.0f;
    w.RunFor(400);
    CHECK(pokes == 1);
    CHECK(w.Current() != kGenlock); // the rest of its turn is the stun's
}

TEST_CASE("with time, the view says a turn is a bar of time between two turns too", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Between the hero's turn and the genlock's the budget is reset, and
    // there is no bar: the view still says how long a turn's is, by the
    // setting (by the budget, the game's bar went back to experience for a
    // third of a second: play test 27).
    (void)w.machine.OnEndTurn();
    bool every = true;
    for (int i = 0; i < 40; ++i)
    {
        w.Frame();
        every = every && w.machine.View().turnMs == 4000;
    }
    CHECK(every);
    CHECK(World{}.machine.View().turnMs == 0); // no fight, no turn
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

// Stopped where its bar's walk ends on the way to an action, a creature has
// that action's time back once a turn (TrackMovement, budget_.refunded). On
// the bar the time given back is a second action's, let through while the
// first one's had already gone to its walk ("an action whose walk runs past
// the bar is charged when its creature gets there", above; onArrivalMs): the
// first is never taken, and the second, charged as it was let through, is
// what the stop finds to give back. The random fights have it of an AI, whose
// turn ends at the stop (test_fights.cpp's "[fight-seed]" with TB_SEED 62, 72
// and 75, run 2026-10-05; in 62: "c768 is out of movement (8.6 of 18.3 m);
// the action it was moving for is kept"). A turn the player plays goes on,
// and the same two orders again in it come to a second stop, which gives
// nothing back. TrackMovement has why: "Every later try to reach something
// takes a step before it is stopped, so a second refund would let it creep on
// a frame at a time".
// (Until issue 50 the case for the limit was the action clock's, one action a
// turn; on the bar no scripted case came to a second stop.)
TEST_CASE("with time, an action is given back at a walk stopped past the bar once a turn, and not at a second stop",
          "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    const auto pastTheBar = [&] { return Lines(w, "Hero's walk runs past its bar"); };
    const auto stopped = [&] { return Lines(w, "Hero is out of movement"); };
    const auto kept = [&] { return Lines(w, "the action it was moving for is kept"); };
    // Half a meter a frame until `seen`, and still walking then: Walk()
    // stands its creature as it ends, and one that stands at its attack has
    // got there, the time kept for its arrival taken (ChargeOnArrival).
    const auto walkUntil = [&](const auto &seen) {
        w.Get(kHero).moving = true;
        for (int i = 0; i < 100 && !seen(); ++i)
        {
            w.Get(kHero).position.x += 0.5f;
            w.Frame();
        }
        REQUIRE(seen());
    };
    // A swing of 3 s at one whose path the gate could not tell: a second is
    // left, 6 m with the free one, and the walk is longer. The swing's time
    // goes to the walk.
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 1000);
    w.Frame();
    walkUntil([&] { return pastTheBar() == 1; });
    REQUIRE(w.Get(kHero).busy);
    REQUIRE(stopped() == 0);
    // Sent at another on the way, a swing of a second, charged as it is let
    // through. That walk too is longer than what is left: stopped, and the
    // second it was charged is back in the bar.
    REQUIRE(w.machine.View().timeLeftMs > 1000);
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f) == Verdict::Allow);
    walkUntil([&] { return stopped() == 1; });
    CHECK_FALSE(w.Get(kHero).busy);
    CHECK(kept() == 1);
    CHECK(w.machine.View().timeLeftMs == 1000);
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    // The same again with what was given back: a swing whose time goes to
    // its walk, and another let through on the way, 0.3 s of the bar.
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 0.9f) == Verdict::Allow);
    w.Frame();
    walkUntil([&] { return pastTheBar() == 2; });
    REQUIRE(w.Get(kHero).busy);
    REQUIRE(stopped() == 1);
    REQUIRE(w.machine.View().timeLeftMs > 300);
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 0.3f) == Verdict::Allow);
    walkUntil([&] { return stopped() == 2; });
    // Stopped as before, and this time nothing comes back: the 0.3 s stay
    // spent, the bar is empty, and the turn is over as the world settles.
    CHECK_FALSE(w.Get(kHero).busy);
    CHECK(kept() == 1);
    CHECK(w.machine.View().timeLeftMs == 0);
    w.RunFor(600);
    CHECK(w.Current() != kHero);
    CHECK(Logged(w, "Hero's turn ends: nothing left to spend"));
}

TEST_CASE("without the engine's conjuring, a blow over the threshold interrupts a pending spell, and nothing else",
          "[time]")
{
    // The mod's own copy of the game's rule, with Settings::engineConjure
    // off; on, the installed rules_core judges the engine's own conjuring.
    Settings mine = Timed();
    mine.engineConjure = false;
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
        World w(mine);
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
        World w(mine);
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
    SECTION("one the gate let through and the engine has not yet begun is not over")
    {
        // A command is in the engine's queue before it is the current one:
        // its object's command step begins it. Busy means either
        // (ActorObservation::busy), so the frames between the gate's yes and
        // the engine's beginning are not taken for the command's end. Weighed
        // with issue 44, where busy by a queue the engine never ran cost a
        // turn twelve seconds: read by the current command alone, a spell
        // slow to begin would be "over at once", its time given back, and
        // then cast for nothing.
        World w(on);
        w.StartFight();
        REQUIRE(cast(w, 3.9f) == Verdict::Allow);
        w.Get(kHero).current = CommandType::Invalid; // queued, and not begun
        w.RunFor(100);
        CHECK(w.machine.GetPhase() == Phase::Executing);
        w.Get(kHero).current = CommandType::UseAbility; // begun
        w.RunFor(3300);
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.machine.View().timeLeftMs == 100);
        CHECK_FALSE(w.Said(L"Nothing happened"));
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
    Settings mine = Timed();
    mine.engineConjure = false; // the mod's own rule loses it here
    World w(mine);
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
        // attack, its command still at its approach, not a step more.
        Walk(w, kGenlock, 1.0f);
        w.Get(kGenlock).busy = true;
        w.Get(kGenlock).current = CommandType::Attack;
        w.Get(kGenlock).approaching = true;
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
        w.Get(kHero).approaching = true; // as the engine has it: its walk not made
        w.applied.clear();
        w.RunFor(kStood - 300);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero)); // not yet
        w.RunFor(500);
        CHECK(w.Did(EffectKind::ClearCommands, kHero));
        CHECK(w.Said(L"No way through"));
        CHECK(w.machine.View().notice == L"No way through: Hero's attack is given back"); // the caption too
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

namespace
{
// An attack as the gate's request has it since issue 11: the walk the core
// worked out for it, and the swing's or the shot's own time.
Verdict Attack(World &w, ObjectId id, Source source, float seconds, float walk)
{
    CommandRequest request;
    request.object = id;
    request.type = CommandType::Attack;
    request.source = source;
    request.inCombat = w.Get(id).fighting;
    request.walk = walk;
    request.actionSeconds = seconds;
    return w.Queue(request);
}

bool GivenBack(const std::vector<TurnEvent> &events, ObjectId who)
{
    return std::ranges::any_of(
        events, [&](const TurnEvent &e) { return e.kind == EventKind::ActionGivenBack && e.who == who; });
}
} // namespace

TEST_CASE("an attack that stands still is judged by where the engine has its command, not by a walk it was told",
          "[time]")
{
    // Issue 11, Zevran's ambush: 67 shots stopped a second in, each by an
    // archer in range of its target with a clear line, whose path round the
    // slope was the longer. The game layer says each frame whether the
    // attack's command is still at its approach, as the engine has it.
    constexpr std::uint32_t kStood = 1000;
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();

    SECTION("at its blow, an archer's shot runs its length, whatever walk the gate was told")
    {
        // A walk as the gate was told (wrongly, as it was until then), a
        // flat 1.4 s, and a shot that takes six and a half: an enemy
        // archer's, turning to face, drawing and aiming where it stands.
        REQUIRE(Attack(w, kHero, Source::Player, 1.4f, 5.0f) == Verdict::Allow);
        w.Get(kHero).approaching = false;
        w.applied.clear();
        w.RunFor(6500);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
        CHECK_FALSE(GivenBack(events, kHero));
        CHECK_FALSE(w.Said(L"No way through"));
        CHECK(w.Current() == kHero);
    }
    SECTION("still at its approach and not a step taken, it is stopped, whatever walk the gate was told")
    {
        // Told nothing to walk (the walk could not be told, or its place at
        // the target is past a doorway): the engine has it approaching, and
        // it stands.
        REQUIRE(Attack(w, kHero, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
        w.Get(kHero).approaching = true;
        w.applied.clear();
        w.RunFor(kStood - 300);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
        w.RunFor(500);
        CHECK(w.Did(EffectKind::ClearCommands, kHero));
        CHECK(GivenBack(events, kHero));
        CHECK(w.Said(L"No way through"));
        CHECK(Logged(w, "(its command still at its approach): stopped; it cannot get to its target"));
    }
    SECTION("its approach over as it stands, it is let be from then")
    {
        REQUIRE(Attack(w, kHero, Source::Player, 1.5f, 0.0f) == Verdict::Allow);
        w.Get(kHero).approaching = true;
        w.applied.clear();
        w.RunFor(kStood - 300);
        w.Get(kHero).approaching = false; // in place: the blow begins
        w.RunFor(3000);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
    }
    SECTION("how long it has stood is counted for the command it is at alone")
    {
        // The ambush replayed: an attacker that had stood through a swing on
        // the spot was sent at a target far off and stopped as it set out,
        // 157 ms into the new command with "3141 ms" stood.
        REQUIRE(Attack(w, kHero, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
        w.Get(kHero).approaching = true;
        w.applied.clear();
        w.RunFor(kStood - 300);
        // The next attack, let through while it still stands where it stood.
        REQUIRE(Attack(w, kHero, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
        w.Get(kHero).approaching = true;
        w.RunFor(500);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero)); // not 1.2 s at this one
        w.RunFor(kStood);
        CHECK(w.Did(EffectKind::ClearCommands, kHero));
    }
}

TEST_CASE("a kept attack is not given to a creature an effect holds as its turn begins", "[time]")
{
    // Issue 11's bandit archer: its kept shot was given while it stood at an
    // effect's command, queued behind it, and the effect was taken for the
    // shot's own: 26 s of the turn, at three times the patience.
    World w(Timed());
    bool kept = false;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        // Its first choice: a shot too long for its bar, kept, owing 2 s.
        if (id == kGenlock && !std::exchange(kept, true))
        {
            CHECK(Strike(w, kGenlock, kHero, Source::Script, 6.0f) == Verdict::Hold);
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(kept);
    REQUIRE(w.heldCasts.contains(kGenlock));
    // Something lays an effect on it that no condition names, before its
    // next turn: it stands at that effect's command.
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);

    SECTION("held past its bar: the turn is lost, and what it kept lands on its next")
    {
        UntilTurnOf(w, kGenlock);
        CHECK(w.released.empty()); // not given behind the effect
        CHECK(w.heldCasts.contains(kGenlock));
        CHECK(Logged(w, "Genlock's pending attack waits: an effect holds its creature"));
        // Its bar is the 2 s its shot's debt left of 4: the hold takes them.
        REQUIRE(w.machine.View().timeLeftMs <= 2000);
        w.RunFor(1700);
        CHECK(w.Current() == kGenlock);
        w.RunFor(500);
        CHECK(w.Current() != kGenlock);
        CHECK(w.Said(L"Genlock loses the turn"));
        CHECK(Logged(w, "Genlock is held by an effect's command past its bar"));
        CHECK_FALSE(Logged(w, "its action's own effect holds it"));
        CHECK_FALSE(Logged(w, "still at a"));
        CHECK(w.released.empty());
        // The effect over, its next turn: the shot lands, for nothing more.
        w.Finish(kGenlock);
        UntilTurnOf(w, kGenlock);
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Allow);
        CHECK(w.Get(kGenlock).current == CommandType::Attack);
        CHECK(w.machine.View().timeLeftMs == 4000); // paid for the turn before
    }
    SECTION("let go before its bar is spent: it lands then, and the turn is its own with the rest")
    {
        UntilTurnOf(w, kGenlock);
        REQUIRE(w.released.empty());
        w.RunFor(1000);
        REQUIRE(w.Current() == kGenlock);
        w.Finish(kGenlock); // the effect lets go
        w.RunFor(100);
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Allow);
        CHECK(w.Current() == kGenlock);
        CHECK(w.Get(kGenlock).current == CommandType::Attack);
        // The second its hold ran is gone from the 2 s its bar had.
        CHECK(w.machine.View().timeLeftMs <= 1050);
        CHECK(w.machine.View().timeLeftMs >= 950);
    }
}

TEST_CASE("its action kept for its next turn, a creature starts nothing more in this one", "[time]")
{
    // The turn is over, its end come with the next step, or after a beat
    // over the player's. A mode turned on in that frame ran on into the next
    // creature's turn (the random fights, seed 47 with time), and only the
    // random fights held it: when the arena's draws changed (issue 30) no
    // seed of the eighty reached it, and the mutant survived.
    SECTION("an AI's, in the frame it kept it")
    {
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            if (++pokes > 1)
            {
                return;
            }
            CHECK(Strike(w, id, kHero, Source::Script, 6.0f) == Verdict::Hold);
            // A mode, which costs nothing, and a step, in the same frame.
            CHECK(w.UseAbility(id, Source::Script, 300, 0.0f, true, false) == Verdict::Block);
            CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
        };
        w.StartFight();
        UntilTurnOf(w, kGenlock);
        w.RunFor(100);
        REQUIRE(pokes == 1);
        CHECK(w.Current() != kGenlock);
        CHECK_FALSE(w.Get(kGenlock).busy);
    }
    SECTION("the player's, in the beat before its turn ends")
    {
        World w(Timed());
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 3.0f, 0.0f) == Verdict::Allow);
        Done(w, kHero);
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Hold);
        CHECK(w.UseAbility(kHero, Source::Player, 300, 0.0f, true, false) == Verdict::Block);
        CHECK_FALSE(w.Get(kHero).busy);
    }
}

namespace
{
// The genlock thrown in the hero's turn: at an effect's command no condition
// names as its own turn comes, its first, with a bar of 4 s.
void HeldAsItsTurnComes(World &w)
{
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    UntilTurnOf(w, kGenlock);
    REQUIRE(w.machine.View().timeMs == 4000);
    REQUIRE(w.machine.View().timeLeftMs == 4000);
    w.applied.clear();
}
} // namespace

TEST_CASE("with time, a hold costs the time it holds: a second and a half of a four-second bar leaves two and a half",
          "[time][holds]")
{
    // Issue 30. The high dragon's knockback, 1.2 s by its effect's own
    // count, cost each fighter it threw the whole turn: two seconds waited,
    // then the bar and all taken, 37 of the party's 176 turns in one fight.
    // The user: "we should not make a special case for it, but fix the way
    // effects cost time by reading the actual amount of time they cost".
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).effects = "effect type 1057 of ability 90065 by 0x100, 1.2 of 1.2 s left";
    HeldAsItsTurnComes(w);
    // A second and a half by the machine's count: it takes the creature for
    // free on the second frame it sees it so.
    w.RunFor(1480);
    // Held: its turn its own still, its AI not told to choose behind the
    // effect's command.
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.State().heldByEffect);
    CHECK(pokes == 0);
    w.Finish(kGenlock); // on its feet
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(pokes == 1); // told now, with what the hold left
    CHECK(w.machine.View().timeLeftMs <= 2550);
    CHECK(w.machine.View().timeLeftMs >= 2450);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    CHECK(Logged(w, "Genlock is held by an effect's command, how long the engine does not say ahead: the time it "
                    "holds is taken of its bar as it runs (4.00 s and 21.0 m left) (effect type 1057 of ability "
                    "90065 by 0x100, 1.2 of 1.2 s left)"));
    CHECK(Logged(w, "Genlock is free of the effect's command after 1.50 s, 1.50 s of its bar: 2.50 s and "));
}

TEST_CASE("with time, a hold that outlasts the bar loses the turn, and only then", "[time][holds]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    // Not at two seconds, as it was: at the bar's four.
    w.RunFor(3800);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    CHECK(w.machine.View().timeLeftMs <= 250);
    w.RunFor(400);
    CHECK(w.Current() != kGenlock);
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK(pokes == 0);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock)); // the effect is the engine's
    CHECK(Logged(w, "Genlock is held by an effect's command past its bar (4.0"));
    // The hold took the bar: nothing of it is left that could roll over,
    // were a turn lost to a hold one that rolls anything (issue 29: it is
    // not, its actor not having ended it): the line names no time that would
    // have.
    CHECK(Logged(w, "Genlock's turn ends: held by an effect; nothing rolls over"));
    CHECK_FALSE(Logged(w, "its actor not having ended it ("));
    CHECK_FALSE(Logged(w, "still at a"));
}

TEST_CASE("what a hold costs is the time it ran, whatever the effect and whatever the engine's count said ahead",
          "[time][holds]")
{
    // No list of kinds (the user, of reading a knockback as one more kind of
    // hold: "we should not make a special case for it"): the machine is told
    // the effects on the creature only to say them. And the engine's count of
    // what its command has left (DAOLib's HeldFor) is said, not acted on: no
    // turn is lost on it. It is the whole of the hold only where every
    // sub-action left is timed, and "at least" so much where part is not
    // timed ahead. A knockback of the high dragon's is at least nothing: its
    // one sub-action spent as the turn begins, the fall and rise, 0.61 to
    // 3.19 s, on no list (2026-10-04).
    struct Case
    {
        const char *effects;
        float seconds;
        bool known;
        const char *said;
    };
    const Case cases[] = {
        // A dragon's knockback, as it is in play: at least nothing.
        {"effect type 1057 of ability 90065 by 0x100, 1.2 of 1.2 s left", 0.0f, false,
         "how long the engine does not say ahead"},
        // At least so much: a timed sub-action running, and after it one the
        // engine does not time ahead.
        {"effect type 1057 of ability 90065 by 0x100, 1.2 of 1.2 s left", 0.8f, false,
         "at least 0.80 s more by the engine's count"},
        // At least more than the bar holds, of a hold that lets go sooner.
        {"effect type 1057 of ability 90065 by 0x100, 1.2 of 1.2 s left", 9.0f, false,
         "at least 9.00 s more by the engine's count"},
        // The whole of it, every sub-action left timed, and right.
        {"effect type 1057 of ability 90065 by 0x100, 0.3 of 1.2 s left", 1.5f, true,
         "1.50 s more by the engine's count"},
        // The whole of it by the count, longer than the bar, and not kept to.
        {"effect type 1057 of ability 90065 by 0x100, 1.2 of 1.2 s left", 9.0f, true,
         "9.00 s more by the engine's count"},
        // A spider's Overwhelm on its prey, and nothing listed at all.
        {"effect type 1043 of ability 90133 by 0x201, 6.1 of 6.1 s left", 0.0f, false,
         "how long the engine does not say ahead"},
        {"no temporary effect stands on it", 0.0f, false, "how long the engine does not say ahead"},
    };
    for (const Case &c : cases)
    {
        CAPTURE(c.effects, c.seconds, c.known);
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId) { ++pokes; };
        w.Get(kGenlock).effects = c.effects;
        w.Get(kGenlock).heldSeconds = c.seconds;
        w.Get(kGenlock).heldKnown = c.known;
        HeldAsItsTurnComes(w);
        w.RunFor(1480);
        CHECK(w.Current() == kGenlock);
        w.Finish(kGenlock);
        w.RunFor(100);
        CHECK(w.Current() == kGenlock);
        CHECK(pokes == 1);
        CHECK_FALSE(w.Said(L"Genlock loses the turn"));
        CHECK(w.machine.View().timeLeftMs == 2496); // the same to the frame, each of them
        // Said as the engine gave it: the whole, at least so much, or nothing.
        CHECK(Logged(w, std::string{"Genlock is held by an effect's command, "} + c.said + ": the time it holds"));
        CHECK(Logged(w, std::string{"("} + c.effects + ")"));
    }
}

TEST_CASE("with own clocks, a hold moves its creature's clock as its bar pays for it: its effect wears off in the turn",
          "[time][holds]")
{
    // A hold that lasts while its effect does (a grab's victim, a pin not
    // named; not the dragon's knockback, whose own 1.2 s the engine times
    // with the world and had spent as the turn began). Effects' time is held
    // through a fight, each fighter's moving only as its own bar is spent:
    // while a turn waited on an effect with nothing of the bar taken, that
    // time never ran until the lost turn's end moved the clock.
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    const std::int64_t own = w.agedOwn[kGenlock];
    const std::int64_t shared = w.agedMs;
    for (int i = 0; i < 400 && w.Get(kGenlock).busy; ++i)
    {
        w.Frame();
        // The effect has 1.2 s of its victim's time left, and its command
        // lasts as long.
        if (w.agedOwn[kGenlock] - own >= 1200)
        {
            w.Finish(kGenlock);
        }
    }
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    CHECK(pokes == 1);
    CHECK(w.agedOwn[kGenlock] - own >= 1200);
    CHECK(w.agedOwn[kGenlock] - own < 1300);
    CHECK(w.agedMs == shared); // the one clock everything else is on is the rounds'
    CHECK(w.machine.View().timeLeftMs <= 2800);
    CHECK(w.machine.View().timeLeftMs >= 2700);
}

TEST_CASE("with time and one clock for everyone's effects, it runs with a hold and comes off the round's worth",
          "[time][holds]")
{
    Settings s = Timed();
    s.ownClocks = false;
    World w(s);
    w.onPoke = [&](ObjectId) {};
    HeldAsItsTurnComes(w);
    REQUIRE(w.timeHeld);
    const std::int64_t before = w.agedMs;
    for (int i = 0; i < 400 && w.Get(kGenlock).busy; ++i)
    {
        w.Frame();
        if (w.agedMs - before >= 1200)
        {
            w.Finish(kGenlock);
        }
    }
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    CHECK(w.agedMs - before >= 1200);
    CHECK(w.agedMs - before < 1500); // in quarter-second steps
    CHECK(w.agedOwn[kGenlock] == 0);
    CHECK(w.machine.View().timeLeftMs <= 2800);
    CHECK(w.machine.View().timeLeftMs >= 2450);
    // The round is still a turn's seconds of effects' time in all.
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
    CHECK(w.agedMs - before == 4000);
}

TEST_CASE("a hold is not stopped as an action that takes too long: its bound is the bar", "[time][holds]")
{
    // max_action_ms stops a command of the creature's own that does not end,
    // and clears it. A hold is the engine's: with the two seconds gone, a
    // short max_action_ms would have cleared the effect's command.
    Settings s = Timed();
    s.maxActionMs = 2000;
    World w(s);
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    w.RunFor(3000);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK_FALSE(Logged(w, "still at a"));
    w.Finish(kGenlock);
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    CHECK(pokes == 1);
    CHECK(w.machine.View().timeLeftMs <= 1050);
    CHECK(w.machine.View().timeLeftMs >= 950);
}

TEST_CASE("a turn ended at max_turn_ms while an effect holds its creature leaves the effect's command be",
          "[time][holds][limit]")
{
    // The limit ends the turn whatever its creature is at, and stops what it
    // is at; not a hold, which is the engine's to end. (A limit shorter than
    // a bar, for the case: at thirty seconds the bar ends a hold first.)
    Settings s = Timed();
    s.maxTurnMs = 2000;
    World w(s);
    Turns turns(w);
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    w.applied.clear();
    w.RunFor(3000);
    CHECK(w.Current() != kGenlock);
    CHECK(turns.ends[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Logged(w, "Genlock's turn is still going after 2000 ms, 0 of its commands let through: ended"));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Get(kGenlock).busy);
}

TEST_CASE("a second hold in a turn is charged from when it began, not from the first one's end", "[time][holds]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; }; // told, it thinks: the world runs
    HeldAsItsTurnComes(w);
    w.RunFor(1000);
    w.Finish(kGenlock);
    w.RunFor(500); // free, and thinking, half a second
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    const std::uint32_t afterTheFirst = w.machine.View().timeLeftMs;
    CHECK(afterTheFirst <= 3050);
    CHECK(afterTheFirst >= 2950);
    // Thrown again before it has chosen.
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.RunFor(1000);
    CHECK(w.machine.State().heldByEffect);
    w.Finish(kGenlock);
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    // The second's own second: not the half second it thought between them.
    CHECK(w.machine.View().timeLeftMs <= afterTheFirst - 950);
    CHECK(w.machine.View().timeLeftMs >= afterTheFirst - 1050);
}

TEST_CASE("thrown after it has acted, as its turn waits or its AI thinks, a creature is held all the same",
          "[time][holds]")
{
    // With a command of its own let through this turn, an effect's command
    // met while one of its own runs is taken for that action's own effect (a
    // grab's). Met while the turn waits on an order, or while its AI thinks,
    // it is no action's: something threw it, and it is held.
    SECTION("a party member, its turn waiting on an order")
    {
        World w(Timed());
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
        Done(w, kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        REQUIRE(w.machine.View().timeLeftMs == 3000);
        REQUIRE(w.Order(kHero, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
        w.RunFor(500);
        CHECK(w.machine.State().heldByEffect);
        CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Block);
        CHECK_FALSE(Logged(w, "its action's own effect holds it"));
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.View().timeLeftMs <= 2550); // the half second it was down
        CHECK(w.machine.View().timeLeftMs >= 2450);
    }
    SECTION("an AI, thinking of its next action")
    {
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            if (++pokes == 1)
            {
                CHECK(Act(w, id, CommandType::Attack, Source::Script, 1.0f, 0.0f) == Verdict::Allow);
            }
        };
        w.StartFight();
        UntilTurnOf(w, kGenlock);
        w.RunFor(200);
        REQUIRE(pokes == 1);
        w.Finish(kGenlock);
        for (int i = 0; i < 100 && pokes < 2; ++i)
        {
            w.Frame();
        }
        REQUIRE(pokes == 2); // told to choose again, with 3 s left
        REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
        REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
        w.RunFor(500);
        CHECK(w.machine.State().heldByEffect);
        CHECK_FALSE(Logged(w, "its action's own effect holds it"));
        w.Finish(kGenlock);
        w.RunFor(100);
        CHECK(pokes == 3); // and told again as it lets go
        CHECK(w.machine.View().timeLeftMs <= 2550);
        CHECK(w.machine.View().timeLeftMs >= 2450);
    }
}

// Where a hold's time (issue 30) and an AI's wait's (issue 31) meet. A wait
// is taken of the bar at the gate, at once and with the world still; a hold
// as it runs. Neither is taken for a stretch the other has: a held
// creature's wait is refused like any order behind the effect, and a wait
// paid before its creature was thrown is time before the hold.
TEST_CASE("an AI that waits and is then thrown pays for each once: the wait as it is taken, the hold as it runs",
          "[time][holds][wait]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        if (++pokes > 1)
        {
            return;
        }
        // It waits a second, and in that frame something throws it. A wait
        // is no command of its own let through: the effect's command is not
        // its action's own effect, and not nobody's either.
        CHECK(w.Wait(id, 1.0f) == Verdict::Block);
        CHECK(w.machine.View().timeLeftMs == 3000);
        CHECK(w.Order(id, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    REQUIRE(pokes == 1);
    w.RunFor(1000);
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.State().heldByEffect);
    CHECK(pokes == 1); // not told to choose again behind the effect
    CHECK_FALSE(Logged(w, "its action's own effect holds it"));
    w.Finish(kGenlock);
    w.RunFor(100);
    CHECK(pokes == 2); // told as it lets go
    // The wait's second and the hold's, each once, of the bar's four.
    CHECK(w.machine.View().timeLeftMs <= 2050);
    CHECK(w.machine.View().timeLeftMs >= 1950);
    // And of its own clock, which both moved.
    CHECK(w.agedOwn[kGenlock] >= 1950);
    CHECK(w.agedOwn[kGenlock] <= 2050);
    CHECK(Logged(w, "Genlock waits 1.00 s; 3.00 s and 16.0 m left"));
    CHECK(Logged(w, "Genlock is held by an effect's command"));
}

TEST_CASE("a held creature's AI's wait takes nothing and ends nothing: the hold has that time already",
          "[time][holds][wait]")
{
    SECTION("with time: refused behind the effect, and paid for only once it is free")
    {
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            ++pokes;
            CHECK(w.Wait(id, 2.0f) == Verdict::Block);
        };
        HeldAsItsTurnComes(w);
        w.RunFor(500);
        // Its AI, run by something that happened to it meanwhile, waits.
        CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block);
        w.RunFor(500);
        CHECK(w.Current() == kGenlock); // not passed
        CHECK(w.machine.State().heldByEffect);
        CHECK(w.machine.View().timeLeftMs >= 2950); // the hold's second, not the wait's two as well
        CHECK_FALSE(Logged(w, "Genlock waits "));
        // The effect's command ends, and the engine runs its AI in that frame
        // (COMMAND_COMPLETE), before the machine has seen it free: it waits.
        w.Finish(kGenlock);
        CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block);
        CHECK_FALSE(Logged(w, "Genlock waits "));
        CHECK(pokes == 0);
        w.RunFor(100);
        // Free, it is told, and the wait it answers with is paid for: 2 s of
        // the 3 the hold left. Told again, its next does not fit, and ends
        // the turn as a wait too long does, the rest rolling over as ever.
        CHECK(pokes == 2);
        CHECK(std::ranges::count_if(w.log, [](const std::string &line) {
                  return line.find("Genlock waits 2.00 s") != std::string::npos;
              }) == 1);
        CHECK(Logged(w, "Genlock's wait of 2.00 s is longer than the 0.9"));
        CHECK(Logged(w, "Genlock's turn ends: waits; 0.9"));
        CHECK(w.Current() != kGenlock);
    }
}

// Where a hold (issue 30) meets a hostile that has noticed nobody to fight
// (issue 29), whose turn is skipped after a beat. Its hold is not stood out
// for a turn that is skipped anyway: held first, as it was while a hold was
// given two seconds, it would stand out its bar and then its beat.
TEST_CASE("an unaware hostile at an effect's command is not stood out for: its turn is skipped after its beat",
          "[time][holds][perception]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    HeldAsItsTurnComes(w);
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(Logged(w, "Genlock has not noticed the party"));
    SECTION("unaware to the end of its beat")
    {
        w.RunFor(1200);
        CHECK(w.Current() != kGenlock);
        CHECK(pokes == 0);
        CHECK(Logged(w, "Genlock's turn ends: unaware"));
        CHECK_FALSE(Logged(w, "Genlock is held by an effect's command"));
        CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    }
    SECTION("noticing in its beat with the effect still on it: held from then, and told as it lets go")
    {
        w.RunFor(300);
        REQUIRE(w.Current() == kGenlock);
        w.Get(kGenlock).perception = Perception{};
        w.RunFor(1000);
        CHECK(w.Current() == kGenlock); // not skipped as its beat would have ended
        CHECK(w.machine.State().heldByEffect);
        CHECK(pokes == 0);
        w.Finish(kGenlock);
        w.RunFor(100);
        CHECK(pokes == 1);
        // The second it was held since it noticed, of its bar: not the beat
        // before, which took nothing.
        CHECK(w.machine.View().timeLeftMs <= 3050);
        CHECK(w.machine.View().timeLeftMs >= 2950);
    }
}

// A hold's time is the game's. Under a menu the game stands, the effect's
// command with it, and the machine takes nothing up (Step; and since issue
// 29's look at everyone, WhatToObserve takes none there): nothing of the bar
// goes, however long the menu is up, and the hold is not let go under it.
// The same for the player's own pause.
TEST_CASE("a hold is not charged while the game stands: a menu or a pause over it takes nothing of the bar",
          "[time][holds]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    w.RunFor(500);
    REQUIRE(w.machine.State().heldByEffect);
    const std::uint32_t left = w.machine.View().timeLeftMs;
    REQUIRE(left <= 3550);
    REQUIRE(left >= 3450);
    SECTION("a menu, longer than the bar")
    {
        w.mode = Mode::Gui;
        w.paused = true; // the menu pauses
        w.RunFor(6000);
        CHECK(w.machine.View().timeLeftMs == left);
        w.mode = Mode::Combat;
        w.paused = false; // leaving it unpauses
    }
    SECTION("the player's pause, longer than the bar")
    {
        REQUIRE(w.machine.OnPauseKey());
        w.Frame();
        REQUIRE(w.paused);
        const std::uint32_t paused = w.machine.View().timeLeftMs; // the frame that paused it ran
        w.RunFor(6000);
        CHECK(w.paused);
        CHECK(w.machine.View().timeLeftMs == paused);
        REQUIRE(w.machine.OnPauseKey());
        w.Frame();
        CHECK_FALSE(w.paused);
    }
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.State().heldByEffect);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    w.RunFor(500);
    w.Finish(kGenlock);
    w.RunFor(100);
    // A second of hold in all (and the frame or two in which the pause came
    // and went): the bar has the other three, and its AI is told with them.
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(pokes == 1);
    CHECK(w.machine.View().timeLeftMs <= 3050);
    CHECK(w.machine.View().timeLeftMs >= 2900);
    CHECK(Logged(w, "Genlock is free of the effect's command after 1.0"));
}

// Where a hold (issue 30) meets an attack the engine has no way to make
// (issue 27). What a creature kept is given as its hold lets go, through the
// gate as it is at a turn's start, and so is asked of the engine there: the
// target's ring filled meanwhile, it is not made, and what the turn paid for
// it as it began is given back. The hold's time is the hold's all the same.
TEST_CASE("a kept attack given as a hold lets go is asked of the engine like any: with no room it is not made",
          "[time][holds][noattack]")
{
    World w(Timed());
    bool kept = false;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        if (id == kGenlock && !std::exchange(kept, true))
        {
            CHECK(Strike(w, kGenlock, kHero, Source::Script, 4.5f) == Verdict::Hold); // owes half a second
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(kept);
    // Thrown before its next turn; and by then the engine has no place for
    // it at the hero.
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.heldCasts[kGenlock].noAttack = NoAttack::NoPlace;
    UntilTurnOf(w, kGenlock);
    const int told = pokes;
    REQUIRE(w.machine.View().timeLeftMs == 3500); // the half second its shot owed is paid
    w.RunFor(500);
    // Held, an attack of its script's is refused as any order behind the
    // effect is: not put to the engine, and not counted as one given back.
    CHECK(w.Attack(kGenlock, kHero, Source::Script, NoAttack::NoPlace, 0.0f, 1.0f) == Verdict::Block);
    CHECK_FALSE(Logged(w, "is given back before a step"));
    w.RunFor(500);
    CHECK(w.released.empty()); // what it kept is not given behind the effect
    w.Finish(kGenlock);
    w.RunFor(200);
    // Given as the hold lets go, and asked of the engine: not made.
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Block);
    CHECK_FALSE(w.Get(kGenlock).busy);
    CHECK(Logged(w, "Genlock's pending attack on Hero is not made: the engine has no place at its target for it; the "
                    "0.50 s it owed given back"));
    // The half second is back in the bar; the second the hold ran is not.
    CHECK(w.machine.View().timeLeftMs <= 3050);
    CHECK(w.machine.View().timeLeftMs >= 2950);
    // And the turn is handed to it with that: its AI is told, once.
    CHECK(w.Current() == kGenlock);
    CHECK(pokes == told + 1);
}

TEST_CASE("a frame between one command of an effect's and the next is not the hold's end", "[time][holds]")
{
    // A frame can fall between one command the engine queues and the next
    // (the machine waits two for any command's end). Taken for let go in
    // that frame, a creature thrown and then getting up, if those are two
    // commands, would be given what it kept into the gap, to queue behind
    // the rise: the stall of issue 11's archer.
    World w(Timed());
    bool kept = false;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        if (id == kGenlock && !std::exchange(kept, true))
        {
            CHECK(Strike(w, kGenlock, kHero, Source::Script, 4.5f) == Verdict::Hold); // owes half a second
        }
    };
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.RunFor(200);
    REQUIRE(kept);
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    UntilTurnOf(w, kGenlock);
    const int before = pokes;
    w.RunFor(800);
    // The fall's command ends, and a frame on the getting up's begins.
    w.Finish(kGenlock);
    w.Frame();
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.RunFor(700);
    CHECK(w.machine.State().heldByEffect);
    CHECK(w.released.empty()); // nothing given into the gap
    CHECK(pokes == before);
    w.Finish(kGenlock);
    w.RunFor(100);
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    // One hold, a second and a half of it, gap and all: of the 3.5 s its
    // bar had after the shot's debt.
    CHECK(w.machine.View().timeLeftMs <= 2050);
    CHECK(w.machine.View().timeLeftMs >= 1950);
}

TEST_CASE("thrown as it was to walk to its kept attack's target, a creature with no bar left is not walked",
          "[time][holds]")
{
    // The long run of random fights with time, as holds came to run on the
    // turn's clock: an attack kept, its bar spent on it, the walk to its
    // target asked for with the next step's effects, and its creature thrown
    // before that step. The hold had nothing of the bar to take, the turn
    // was lost in that step, and the walk was given after it, out of turn.
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // 4.5 s of swing and 5 m to walk on a bar of 4 s: kept, and she walks to
    // her target now.
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 4.5f, 5.0f) == Verdict::Hold);
    REQUIRE(w.Order(kHero, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.RunFor(100);
    CHECK(w.Current() != kHero);
    CHECK(w.Said(L"Hero loses the turn"));
    CHECK(w.approaches == 0);
    CHECK(w.heldCasts.contains(kHero)); // what she kept is hers still, for her next turn
}

TEST_CASE("let go as the last of its bar goes, an AI is not asked to choose with nothing", "[time][holds]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    HeldAsItsTurnComes(w);
    for (int i = 0; i < 400 && w.machine.View().timeLeftMs > 16; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.View().timeLeftMs == 16);
    w.Finish(kGenlock); // free in the frame that takes the last of it
    w.RunFor(1200);
    CHECK(pokes == 0);
    CHECK(w.Current() != kGenlock);
    CHECK_FALSE(Logged(w, "Genlock's turn ends: chose nothing"));
}

TEST_CASE("each command of a turn has max_action_ms of its own", "[machine]")
{
    Settings s = Fixed();
    s.maxActionMs = 2000;
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
                                     std::pair{"COMMAND_FAILED_NO_SPACE_IN_MELEE_RING", L"No room at the target"},
                                     std::pair{"COMMAND_FAILED_PATH_ACTION_REQUIRED", L"A door is in the way"},
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

TEST_CASE("an action given back is said on the bar's caption, for long enough to be read", "[time][givenback]")
{
    // Issue 21: over the character alone the word was missed, and the same
    // order was tried again and again (Sten at a doorway, eighteen times in
    // one fight). The caption has the reason, whose action it was, and that
    // it is given back.
    World w(Timed());
    w.StartFight();
    CHECK(w.machine.View().notice.empty());
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().notice.empty());
    REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kHero);
    const std::wstring line = L"No room at the target: Hero's attack is given back";
    CHECK(w.machine.View().notice == line);
    // In the bar's string, the head's last field, and in the state a tool reads.
    const std::wstring bar = EncodeBar(w.machine.View());
    CHECK(bar.substr(0, bar.find(kBarCard)).ends_with(std::wstring{kBarField} + line));
    CHECK(ToJson(w.machine.State()).find(R"("notice":"No room at the target: Hero's attack is given back")") !=
          std::string::npos);
    // The turn goes on and waits for an order, the world standing still:
    // the line stays five seconds of the real clock, and then goes.
    w.RunFor(4800);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.View().notice == line);
    w.RunFor(400);
    CHECK(w.machine.View().notice.empty());
    CHECK(ToJson(w.machine.State()).find(R"("notice":"")") != std::string::npos);
    const std::wstring after = EncodeBar(w.machine.View());
    CHECK(after.substr(0, after.find(kBarCard)).ends_with(std::wstring{kBarField}));
}

TEST_CASE("the caption's line of an action given back goes when the player's next order is let through",
          "[time][givenback]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH"));
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.View().notice == L"No way through: Hero's attack is given back");
    // Another try that comes to nothing has its own line, by its own reason.
    REQUIRE(Act(w, kHero, CommandType::UseAbility, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().notice.empty()); // the order before's is gone as this one begins
    REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::UseAbility, 617, "COMMAND_FAILED_NO_LINE_OF_SIGHT"));
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.machine.View().notice == L"No line of sight: Hero's ability is given back");
    // An order that is let through and runs: the line was of the order before.
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().notice.empty());
}

TEST_CASE("the caption's line is for the player: an enemy's action given back is not said, nor takes the line away",
          "[time][givenback]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        (void)Act(w, id, CommandType::Attack, Source::Script, 2.0f, 0.0f);
    };
    w.StartFight();
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kHero);
    w.RunFor(300);
    const std::wstring line = L"No room at the target: Hero's attack is given back";
    REQUIRE(w.machine.View().notice == line);
    // The hero's turn ended, the genlock's begun, its attack let through:
    // not the player's order, and the line is still there to be read.
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    CHECK(w.machine.View().notice == line);
    // The genlock's own attack comes to nothing: given back, and no word of
    // it on the caption, which is the player's.
    REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH"));
    w.Frame();
    CHECK(w.machine.View().notice == line);
}

TEST_CASE("a new fight begins with nothing on the caption from the last one", "[time][givenback]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 2.0f, 0.0f) == Verdict::Allow);
    REQUIRE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH"));
    w.Finish(kHero);
    w.RunFor(300);
    REQUIRE_FALSE(w.machine.View().notice.empty());
    // The enemies fall and the fight is over; the game leaves combat, and
    // within the line's five seconds another fight begins.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 100 && w.machine.GetPhase() != Phase::Idle; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
    CHECK(w.machine.View().notice.empty());
    w.mode = Mode::Explore;
    w.Frame();
    w.Get(kGenlock).down = false;
    w.Get(kHurlock).down = false;
    w.StartFight();
    REQUIRE(w.machine.Active());
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().notice.empty());
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

// An AI whose action came to nothing is told to choose again at once, its
// action given back, once a turn (StepSettling, budget_.retried). What it
// does then is a command of its turn like any: as that ends it is told as
// any AI with time left is, after the follow-up time (ai_follow_up_ms) and
// on a look at everyone taken with that step, and not "its action given
// back" again, at once, after everything it does for the rest of the turn.
// (Until issue 50, 2026-10-05, the case for this was the action clock's,
// where the action given back was the turn's one; on the bar every scripted
// case ended at the second failure, kGivenBackEnough.)
TEST_CASE("an AI whose action was given back is told so at once, once: what it does next is followed as any command",
          "[time]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        switch (++pokes)
        {
        case 1:
            (void)Act(w, id, CommandType::Attack, Source::Script, 1.5f, 0.0f);
            break;
        case 2:
            // No room at its target: a move, which is what the game's AI
            // answers a failure with (the comment at NoAttackToMake, of
            // ai_main_h_2.nss's AI_ExecuteAttack: "a move closer and the
            // same target again").
            CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script, 3.0f) == Verdict::Allow);
            break;
        default:
            (void)w.Order(id, CommandType::Wait, Source::Script);
            break;
        }
    };
    const auto givenBack = [&] { return Lines(w, "Genlock chooses again, its action given back"); };
    const auto asAny = [&] { return Lines(w, "Genlock chooses again: "); };
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    REQUIRE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    // Told at once: within the frames its command's end takes to be seen,
    // not after the follow-up time.
    w.RunFor(100);
    REQUIRE(pokes == 2);
    REQUIRE(givenBack() == 1);
    REQUIRE(asAny() == 0);
    REQUIRE(w.Get(kGenlock).busy);
    // Its walk, 3 m, and its end. Not told at once this time: its action is
    // not given back twice for one failure.
    Walk(w, kGenlock, 3.0f);
    w.Finish(kGenlock);
    w.RunFor(300);
    CHECK(pokes == 2);
    CHECK(givenBack() == 1);
    CHECK(w.Current() == kGenlock);
    // Told as any with time left, the follow-up time over.
    w.RunFor(400);
    CHECK(pokes == 3);
    CHECK(givenBack() == 1);
    CHECK(asAny() == 1);
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

TEST_CASE("with own clocks off, everyone's effects age together as each round begins", "[time]")
{
    Settings off = Timed();
    off.ownClocks = false;
    World w(off);
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

namespace
{
// A spell that conjures (its conjure animation and its speed's count, as
// DescribeAbility reads them), longer than a bar.
CommandRequest Conjured(ObjectId caster, float seconds = 5.5f, std::int32_t conjureAnim = 245)
{
    CommandRequest spell;
    spell.object = caster;
    spell.type = CommandType::UseAbility;
    spell.source = Source::Player;
    spell.inCombat = true;
    spell.ability = 10211;
    spell.abilityType = kAbilitySpell;
    spell.actionSeconds = seconds;
    spell.conjureAnim = conjureAnim;
    spell.conjureSeconds = conjureAnim > 0 ? 1.0f : 0.0f;
    return spell;
}
} // namespace

TEST_CASE("the engine's own conjuring: a spell that does not fit is begun at once, held, and lands as it is paid",
          "[time]")
{
    // The user, 2026-10-02: "Hooking into the game's own logic is the right
    // thing to do", "Animating the character midcast is also the right thing
    // to do".
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Walking into range first, the engine's own walk: no other order meanwhile,
    // which would queue behind the spell and run after its cast.
    REQUIRE(w.Queue(Conjured(kHero)) == Verdict::Conjure);
    w.Get(kHero).conjureHeld = false;
    w.Frame();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, CommandType::MoveToLocation, Source::Player, 0.0f, 2.0f) == Verdict::Block);
    w.Get(kHero).conjureHeld = true;
    CHECK(w.heldConjures.contains(kHero));
    CHECK_FALSE(w.heldCasts.contains(kHero)); // not kept in the gate: the engine's
    w.RunFor(100);
    // At its held conjure: the turn is over, 1.5 s of the spell owed.
    CHECK(w.Current() != kHero);
    REQUIRE(w.machine.State().charging.size() == 1);
    CHECK(w.machine.State().charging.front().owedMs == 1500);
    // A blow is the installed rules_core's to judge, not the mod's.
    CHECK_FALSE(w.machine.OnDamaged(kHero, 99.0f, true, 20.0f));
    CHECK(w.heldConjures.contains(kHero));
    // Its next turn pays what it owes and lets the conjure complete.
    UntilTurnOf(w, kHero);
    CHECK_FALSE(w.heldConjures.contains(kHero));
    CHECK_FALSE(w.Get(kHero).conjureHeld);
    CHECK(w.machine.GetPhase() == Phase::Executing); // the cast runs
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 2500); // 4 s, less the 1.5 s it owed
    CHECK(w.machine.State().charging.empty());
    // Nothing was waited for at the gate: the engine cast it.
    CHECK(std::ranges::none_of(w.log,
                               [](const std::string &line) { return line.find("did not come") != std::string::npos; }));
    // Landed, it is done with: the same spell again is a new cast and costs
    // its time, not let through for nothing as a kept one given again is.
    CHECK(w.Queue(Conjured(kHero, 2.0f)) == Verdict::Allow);
    w.RunFor(100);
    CHECK(w.machine.View().timeLeftMs == 500);
}

TEST_CASE("the engine's own conjuring cleared before its time is paid: the spell is lost, and nothing more is owed",
          "[time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Queue(Conjured(kHero)) == Verdict::Conjure);
    w.RunFor(100);
    REQUIRE(w.Current() != kHero);
    // Struck in the genlock's turn, rules_core clears it: its command ends.
    CHECK(w.machine.OnConjureCleared(kHero));
    w.Frame();
    CHECK(w.Said(L"Spell lost"));
    CHECK(w.machine.State().charging.empty());
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == "interrupted while conjuring";
    }));
    CHECK_FALSE(w.machine.OnConjureCleared(kHero)); // once
    // Its next turn is whole.
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().timeLeftMs == 4000);
    // Nobody else's conjure: nothing.
    CHECK_FALSE(w.machine.OnConjureCleared(kGenlock));
}

TEST_CASE("the engine's own conjuring is for spells that conjure; the rest are kept, and no blow interrupts them",
          "[time]")
{
    // A spell with no conjure, longer than what is left: kept in the gate as
    // before, and not interrupted by a blow (the game interrupts only a
    // conjuring).
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Queue(Conjured(kHero, 5.5f, 0)) == Verdict::Hold);
    // A speed's count with no conjure animation: the engine does not conjure
    // it either (most talents with a Speed have none).
    World speed(Timed());
    speed.StartFight();
    CommandRequest counted = Conjured(kHero, 5.5f, 0);
    counted.conjureSeconds = 1.0f;
    CHECK(speed.Queue(counted) == Verdict::Hold);
    w.RunFor(100);
    CHECK_FALSE(w.machine.OnDamaged(kHero, 99.0f, true, 20.0f));
    CHECK(w.heldCasts.contains(kHero));
    // With the setting off, a conjuring spell is kept as before.
    Settings mine = Timed();
    mine.engineConjure = false;
    World off(mine);
    off.StartFight();
    CHECK(off.Queue(Conjured(kHero)) == Verdict::Hold);
    // And a spell that fits is let through as ever.
    World fits(Timed());
    fits.StartFight();
    CHECK(fits.Queue(Conjured(kHero, 3.0f)) == Verdict::Allow);
}

TEST_CASE("the engine's own conjuring that fails before it conjures is not pending", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Queue(Conjured(kHero)) == Verdict::Conjure);
    // A shut door on the way to its target: the engine fails it before its
    // conjure.
    w.Get(kHero).conjureHeld = false;
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::UseAbility, 10211, "COMMAND_FAILED_PATH_ACTION_REQUIRED"));
    w.Finish(kHero);
    w.RunFor(300);
    CHECK(w.Said(L"A door is in the way"));
    CHECK(w.machine.View().notice == L"A door is in the way: Hero's ability is given back");
    CHECK(w.machine.State().charging.empty());
    CHECK(w.Current() == kHero); // its action given back, its turn goes on
}

namespace
{
// The genlock, first, grabs the hero for so many ms of the hero's own clock
// (an ogre's grab: an effect on each, the genlock's command ending when the
// hero's does).
void Grab(World &w, std::uint32_t heldMs)
{
    bool poked = false;
    w.onPoke = [&](ObjectId id) {
        if (poked || id != kGenlock)
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
        grab.actionSeconds = 1.0f;
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
    const std::int64_t before = w.agedOwn[kHero];
    for (int i = 0; i < 4000 && w.Current() == kGenlock; ++i)
    {
        if (w.Get(kGenlock).busy && w.agedOwn[kHero] - before >= heldMs)
        {
            w.Finish(kGenlock);
        }
        w.Frame();
    }
    w.onPoke = nullptr;
}

std::vector<Creature> GenlockFirst()
{
    std::vector<Creature> fight = StandardFight();
    fight.at(1).dexterity = 30.0f;
    return fight;
}
} // namespace

TEST_CASE("a creature held by another's action owes that time of its next turn's bar", "[time]")
{
    // The user, 2026-10-02: "yeah your proposal on grab makes sense" (in real
    // time it could do nothing while held).
    World w(Timed(), GenlockFirst());
    Grab(w, 2400);
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    // About 2.4 s of its 4 s bar gone; the grab's steps are a quarter second.
    CHECK(w.machine.View().timeLeftMs <= 1700);
    CHECK(w.machine.View().timeLeftMs >= 1300);
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Hero was held") != std::string::npos && line.find("by another's action") != std::string::npos;
    }));
    // Its effects moved with the grab, and not again: a turn's seconds in all
    // for the grab's span and this, its first turn.
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    CHECK(w.agedOwn[kHero] <= 4300);
    CHECK(w.agedOwn[kHero] >= 3700);
}

TEST_CASE("held by another's action for more than a turn: the turn is lost, the rest of the next is owed", "[time]")
{
    // A spider's Overwhelm, 9.3 s.
    World w(Timed(), GenlockFirst());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    Grab(w, 5000);
    for (int i = 0; i < 4000 && w.Current() != kHero; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    // Its turn this round lost.
    w.RunFor(100);
    CHECK(std::ranges::any_of(events,
                              [](const TurnEvent &e) { return e.kind == EventKind::TurnLost && e.who == kHero; }));
    CHECK(w.Said(L"Hero loses the turn"));
    // The next: a turn's bar less the second over.
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs <= 3200);
    CHECK(w.machine.View().timeLeftMs >= 2700);
}

TEST_CASE("a creature's own action's effect owes nobody else's time when it has no target in the fight", "[time]")
{
    World w(Timed(), GenlockFirst());
    bool poked = false;
    w.onPoke = [&](ObjectId id) {
        if (poked || id != kGenlock)
        {
            return;
        }
        poked = true;
        CHECK(w.UseAbility(id, Source::Script, 90133, 60.0f) == Verdict::Allow); // no target
    };
    w.StartFight();
    for (int i = 0; i < 20 && !poked; ++i)
    {
        w.Frame();
    }
    w.RunFor(200);
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    w.RunFor(2400);
    w.Finish(kGenlock);
    w.onPoke = nullptr;
    UntilTurnOf(w, kHero);
    CHECK(w.machine.View().timeLeftMs == 4000);
}

TEST_CASE("an action's own effect stopped as too long leaves its target owing nothing of it", "[time]")
{
    // Play test 33: a spider's Overwhelm never ended, was stopped after
    // 36 s, and its victim then owed 31.8 s, its turns lost.
    Settings s = Timed();
    s.maxActionMs = 3000; // its patience 9 s
    World w(s, GenlockFirst());
    Grab(w, 60000); // never lets go
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(std::ranges::any_of(
        w.log, [](const std::string &line) { return line.find("still at a execute effect") != std::string::npos; }));
    CHECK(w.machine.View().timeLeftMs == 4000);
}

TEST_CASE("an action's own effect ended with its turn at max_turn_ms leaves its target owing nothing of it",
          "[time][limit]")
{
    // The same stall, where the turn's limit comes before the action's (at
    // the defaults it does: thirty seconds, and three times twelve).
    Settings s = Timed();
    s.maxTurnMs = 5000; // and max_action_ms 12 s, its patience 36
    World w(s, GenlockFirst());
    Grab(w, 60000); // never lets go
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(Logged(w, "Genlock's turn is still going after 5000 ms, "));
    CHECK_FALSE(Logged(w, "still at a execute effect"));
    CHECK(w.machine.View().timeLeftMs == 4000);
}

TEST_CASE("with own clocks, an action's own effect leaves the shared clock to the round", "[time]")
{
    // The Elven Ruins' gas, an area of 30 s, ended in its fifth round when
    // an ogre's grab moved the shared clock 14 s in one (2026-10-02).
    World w(Timed(), GenlockFirst());
    const std::int64_t shared = w.agedMs;
    Grab(w, 2400);
    // Its steps moved the two creatures' clocks, not the shared one.
    CHECK(w.agedMs == shared);
    CHECK(w.agedOwn[kGenlock] >= 2400);
    CHECK(w.agedOwn[kHero] >= 2400);
    // The round's 4 s, on the shared clock, as the next round begins.
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
    CHECK(w.agedMs - shared == 4000);
}

TEST_CASE("getting up from a hold ended as its turn began takes its own time of the bar, and the turn goes on",
          "[time]")
{
    // The tester's re-check of play test 33: knocked down, Lyna's next turn
    // took the knockdown's 1.37 s, the condition ended, she got up (an
    // effect's command), and two seconds in her turn was lost to it. For a
    // while after, getting up cost nothing, for up to five seconds. It costs
    // the time the engine takes over it, as any hold does (issue 30: "A
    // knockdown's time is its fall and getting up", dev/REBALANCE.md).
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    Stun(w, kHero, 1.4f); // knocked down in the genlock's turn
    w.RunFor(700);
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(w.Get(kHero).condition == Condition::None); // ended as the turn began
    // A bar of 5 s (a second rolled over from the turn she passed), less the
    // knockdown's share of it: 1.4 s of her 4 s clock.
    const std::uint32_t afterTheKnockdown = w.machine.View().timeLeftMs;
    CHECK(afterTheKnockdown == 3250);
    // Getting up: the engine's command, a second and a half.
    w.Get(kHero).busy = true;
    w.Get(kHero).current = CommandType::ExecuteEffect;
    w.RunFor(1500);
    CHECK(w.Current() == kHero);
    CHECK_FALSE(w.Said(L"Hero loses the turn"));
    // Held meanwhile: an order would queue behind the rise.
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Block);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // And the rise's second and a half are gone with it: of her clock, so
    // that share of the bar, as the knockdown's own (1.5 of 4 s, of 5 s).
    CHECK(w.machine.View().timeLeftMs <= afterTheKnockdown - 1800);
    CHECK(w.machine.View().timeLeftMs >= afterTheKnockdown - 1950);
    CHECK(Act(w, kHero, CommandType::Attack, Source::Player, 1.0f, 0.0f) == Verdict::Allow);
}

TEST_CASE("with time, a door, a chest or a pickup costs its seconds from the settings file", "[time]")
{
    // The user, 2026-10-02: "let's make these cost 0.5s for now. we can adjust
    // later. maybe make it data driven".
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.machine.View().timeLeftMs == 3500);
    CHECK(w.Order(kHero, CommandType::PickupItem, Source::Player) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.machine.View().timeLeftMs == 3000);
    CHECK(w.Current() == kHero); // not an action: the turn goes on
    // Its cost is the file's.
    Settings free = Timed();
    free.objectSeconds = 0.0f;
    World f(free);
    f.StartFight();
    CHECK(f.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    f.Finish(kHero);
    f.RunFor(600);
    CHECK(f.machine.View().timeLeftMs == 4000);
    CHECK(tb::ParseSettings("[costs]\nobject = 1.25\n").settings.objectSeconds == 1.25f);
}

TEST_CASE("with time, a door used with no time left is owed of the next turn", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 3.8f, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(w.machine.View().timeLeftMs == 200);
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.machine.State().charging.size() == 1);
    CHECK(w.machine.State().charging.front().owedMs == 300);
}

TEST_CASE("a creature that falls while it owes time held owes nothing when it is up again", "[time]")
{
    // Play test 34: fallen in an ogre's grab, up again, Lyna then lost four
    // turns to a spider's Overwhelm, 9.9 s of them the grab's.
    World w(Timed(), GenlockFirst());
    Grab(w, 6000); // more than a turn owed
    w.Get(kHero).down = true;
    w.RunFor(1500); // seen fallen
    w.Get(kHero).down = false;
    w.RunFor(1500); // up again
    UntilTurnOf(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK_FALSE(w.Said(L"Hero loses the turn"));
}

TEST_CASE("with own clocks, a turn's end gives what its creature's clock brought due a beat to land, the world running",
          "[time]")
{
    // The Elven Ruins' gas, 2026-10-02: Lyna's poison, brought due as her
    // turn ended and her clock ran the rest of its seconds, came in the first
    // moment of Tamlen's turn, the world held still between the two.
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kHero).timersHeld = true;
    w.Frame();
    REQUIRE(w.paused); // the player's turn, held still
    (void)w.machine.OnEndTurn();
    w.Frame();
    // Over, the next not begun, the world running meanwhile.
    CHECK(w.machine.GetPhase() == Phase::TurnStart);
    CHECK_FALSE(w.paused);
    w.RunFor(200);
    CHECK(w.machine.GetPhase() == Phase::TurnStart);
    CHECK_FALSE(w.paused);
    w.RunFor(300);
    CHECK(w.machine.GetPhase() != Phase::TurnStart);
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("with time, a kept action dropped as its creature's turn comes does not take the next one with it", "[time]")
{
    // The long fights' seed 790 with time (2026-10-03, found as issue 16
    // changed the timings). A genlock with an attack kept is stunned for
    // less than its bar; the read of the statuses that sees it comes with
    // its turn; the kept attack is dropped, the hold takes its share, and
    // the genlock, told its turn has come in that same frame, keeps a new
    // attack. The drop went out a frame late, with the next step's effects,
    // and cleared the new one in the game layer's hands while the machine
    // still counted on it.
    const auto again = [](const World &w) {
        return std::ranges::any_of(
            w.log, [](const std::string &line) { return line.find("round 2: Genlock's turn") != std::string::npos; });
    };
    const auto step = [](World &w) {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn(); // the party's turns: nothing done
        }
        w.Frame();
    };
    const auto begin = [](World &w) {
        // Each time it is told its turn has come: an attack too long for
        // its bar, kept for its next.
        w.onPoke = [&w](ObjectId id) {
            if (id == kGenlock)
            {
                (void)Act(w, kGenlock, CommandType::Attack, Source::Script, 6.0f);
            }
        };
        w.StartFight();
    };
    // How many frames to the genlock's second turn, left alone.
    World alone(Timed());
    begin(alone);
    int frames = 0;
    for (; frames < 6000 && !again(alone); ++frames)
    {
        step(alone);
    }
    REQUIRE(again(alone));
    REQUIRE(frames > 45);
    // Stunned at each of the frames before it: at some of them the first
    // read to see it is the one its turn begins with.
    int dropped = 0;
    for (int before = 1; before <= 40; ++before)
    {
        CAPTURE(before);
        World w(Timed());
        begin(w);
        for (int i = 0; i < frames - before; ++i)
        {
            step(w);
        }
        REQUIRE(w.heldCasts.contains(kGenlock)); // its first, kept
        w.Get(kGenlock).condition = Condition::Disabled;
        w.Get(kGenlock).conditionSeconds = 0.8f;
        for (int i = 0; i < 400 && !again(w); ++i)
        {
            step(w);
        }
        REQUIRE(again(w));
        for (int i = 0; i < 4; ++i)
        {
            step(w);
        }
        dropped +=
            std::ranges::any_of(w.log,
                                [](const std::string &line) {
                                    return line.find("Genlock's pending attack is dropped: held") != std::string::npos;
                                })
                ? 1
                : 0;
        // Whatever it kept as its turn came is still the game layer's to
        // give it, as the machine believes.
        const MachineState state = w.machine.State();
        const bool counted =
            std::ranges::any_of(state.charging, [](const auto &charge) { return charge.id == kGenlock; });
        CHECK(w.heldCasts.contains(kGenlock) == counted);
    }
    CHECK(dropped == 40); // the hold cost it its first kept attack every time
}

TEST_CASE("with own clocks, a turn's end with nothing held for its creature goes on to the next at once", "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Frame();
    (void)w.machine.OnEndTurn();
    w.RunFor(64);
    CHECK(w.machine.GetPhase() != Phase::TurnStart);
    CHECK(w.Current() == kGenlock);
}

// Shifting its feet to strike what is in reach is not walking (kShuffle). The
// case for it was the action clock's, where a blow could be struck with the
// meters spent; on the bar it shows where the blow took the last of the time:
// nothing is left to walk, and the half meter is past what the bar walks. It
// is not stopped, and its time is not turned into a walk's (issue 50).
TEST_CASE("with time, a blow that took the last of the bar is struck with a shift of the feet, not taken for a walk",
          "[time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(Act(w, kHero, CommandType::MoveToLocation, Source::Player, 0.0f) == Verdict::Allow);
    // A frame before the walk: the first frame a turn is at a command is
    // where its walk is measured from (TrackMovement: lastPosition_ is none
    // until then), and a step taken in that frame is not counted. Without it
    // half the free meter was left, the shift was walked on that, and the
    // case passed whatever kShuffle was (the mutant "a creature in reach is
    // stopped for shifting its feet" survived it, 2026-10-05).
    w.Frame();
    Walk(w, kHero, 1.0f); // the free meter
    Done(w, kHero);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.View().timeLeftMs == 4000);
    REQUIRE(w.machine.View().movementLeft == 20.0f); // the free meter is walked, all of it
    REQUIRE(Act(w, kHero, CommandType::Attack, Source::Player, 4.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 0);
    REQUIRE(w.machine.View().movementLeft == 0.0f);
    Walk(w, kHero, 0.5f);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs == 0);
    CHECK(Lines(w, "runs past its bar") == 0);
    CHECK(Lines(w, "out of movement") == 0);
}
