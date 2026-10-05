// A creature the game has made inactive has no turn (issue 44). The engine's
// word is GetObjectActive, which the game layer reads of everyone with a scan
// and of the acting creature every frame; its scripts make a creature so with
// SetObjectActive(FALSE), and the Broodmother's tentacles go under ground by
// it. In the fight this is from (the Dead Trenches, 2026-10-05) a tentacle
// under ground was told its turn had come like anyone: 146 of the tentacles'
// 205 turns ended "chose nothing" after the wait for a choice, and one on
// which a pin had landed after it went under stood eleven turns running at
// "still at a invalid after 36000 ms: stopped", something queued on it that
// the engine does not run for an inactive object. Scripted cases in the
// stand-in for the game (World.h). What is promised:
//
//   - inactive as its turn comes, it is passed over at once: not told to
//     choose, not waited on, no beat and no word over it, no bar, nothing
//     rolled over and nothing cleared; the scripts are told of the turn, and
//     its creature's own clock moves a turn's seconds, which is what brings a
//     tentacle back up;
//   - with something queued on it, the same: a queue the engine does not run
//     is not an action;
//   - made inactive in the middle of its turn, an action running, the turn
//     ends there, and nothing rolls over;
//   - it stays in the order, and has its turns again when it is active;
//   - whoever it is: a party member is not waited on for an order, and an
//     unaware hostile has no beat stood out for it;
//   - what the mod kept for it is not given while it is so, and lands on its
//     first turn back;
//   - what the mod kept for another is not given at it while it is so, as it
//     is not at one fallen: a plain attack is turned on the nearest enemy in
//     reach, anything else dropped and what it owed given back;
//   - a hold on it takes its turns as on anyone, and is paid down as they
//     pass.
#include "World.h"

#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// A turn a bar of four seconds, for what is charged and what rolls over.
Settings WithABar()
{
    Settings s = Fixed();
    s.turnClock = TurnClock::Time;
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

int Effects(const World &w, EffectKind kind, ObjectId target)
{
    return static_cast<int>(
        std::ranges::count_if(w.applied, [&](const Effect &e) { return e.kind == kind && e.target == target; }));
}

int Signals(const World &w, std::int32_t event, ObjectId target)
{
    return static_cast<int>(std::ranges::count_if(w.applied, [&](const Effect &e) {
        return e.kind == EffectKind::Signal && e.event == event && e.target == target;
    }));
}

bool InATurn(const World &w)
{
    const Phase phase = w.machine.GetPhase();
    return phase == Phase::AwaitingOrder || phase == Phase::Dispatching || phase == Phase::AiThinking ||
           phase == Phase::Executing || phase == Phase::Settling;
}

// The turns go by, the player's ended as they come, until it is `id`'s and
// being played; the frames it took, or -1 if it never came.
int UntilTurnOf(World &w, ObjectId id, int most = 2000)
{
    for (int frames = 0; frames < most; ++frames)
    {
        if (w.Current() == id && InATurn(w))
        {
            return frames;
        }
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    return -1;
}

// Everyone but the party passes when told, and each telling is counted.
struct Tellings
{
    explicit Tellings(World &w)
    {
        w.onPoke = [this, &w](ObjectId id) {
            who.push_back(id);
            (void)w.Wait(id, 2.0f);
        };
    }
    [[nodiscard]] int Of(ObjectId id) const
    {
        return static_cast<int>(std::ranges::count(who, id));
    }
    std::vector<ObjectId> who;
};

struct Heard
{
    explicit Heard(World &w)
    {
        w.machine.SetEventSink([this](const TurnEvent &e) { events.push_back(e); });
    }
    [[nodiscard]] int Of(EventKind kind, ObjectId who) const
    {
        return static_cast<int>(
            std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == who; }));
    }
    [[nodiscard]] std::vector<std::string> EndsOf(ObjectId who) const
    {
        std::vector<std::string> why;
        for (const TurnEvent &e : events)
        {
            if (e.kind == EventKind::TurnEnded && e.who == who)
            {
                why.push_back(e.detail);
            }
        }
        return why;
    }
    std::vector<TurnEvent> events;
};
} // namespace

TEST_CASE("inactive as its turn comes, a creature is passed over at once; active again, it has its turns",
          "[machine][inactive]")
{
    World w;
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // The genlock goes under ground on the hero's turn.
    w.SetActive(kGenlock, false);
    w.applied.clear();
    REQUIRE(w.machine.OnEndTurn());
    // Its turn comes and is gone within a few frames, where a creature told
    // and choosing nothing is waited on for ai_think_ms, 94 of them.
    const int frames = UntilTurnOf(w, kCompanion);
    REQUIRE(frames >= 0);
    CHECK(frames < 10);
    CHECK(Lines(w, "round 1: Genlock is inactive by the game's own word, out of the world: it has no turn") == 1);
    CHECK(Lines(w, "Genlock's turn ends: inactive") == 1);
    CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
    // No turn was begun for it: nothing told, nothing said or shown over it,
    // nothing of its cleared, and no line of a turn's beginning.
    CHECK(told.Of(kGenlock) == 0);
    CHECK(Lines(w, "Genlock's turn (") == 0);
    CHECK(heard.Of(EventKind::TurnStarted, kGenlock) == 0);
    CHECK(heard.Of(EventKind::TurnLost, kGenlock) == 0);
    CHECK(Effects(w, EffectKind::PokeAi, kGenlock) == 0);
    CHECK(Effects(w, EffectKind::Floaty, kGenlock) == 0);
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
    CHECK(Effects(w, EffectKind::CenterCamera, kGenlock) == 0);
    CHECK(Effects(w, EffectKind::Explain, kGenlock) == 0);
    // The scripts are told of the turn all the same, its start and its end:
    // a turn passed, as a lost one does.
    CHECK(Signals(w, events::kTurnStart, kGenlock) == 1);
    CHECK(Signals(w, events::kTurnEnd, kGenlock) == 1);
    // It stays in the order, standing, and is passed over again next round.
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(Lines(w, "round 2: Genlock is inactive by the game's own word") == 1);
    CHECK(told.Of(kGenlock) == 0);
    // Up again: its next turn is its own, told like anyone.
    w.SetActive(kGenlock, true);
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    CHECK(w.machine.Order().Round() == 3);
    CHECK(told.Of(kGenlock) == 1);
    CHECK(Lines(w, "round 3: Genlock's turn (enemy, AI)") == 1);
    CHECK(Lines(w, "Genlock is inactive") == 2);
    CHECK(heard.Of(EventKind::TurnStarted, kGenlock) == 1);
}

TEST_CASE("an inactive creature's turn has no bar: nothing rolls over, and its own clock moves a turn's seconds",
          "[machine][inactive]")
{
    World w{WithABar()};
    std::vector<std::uint32_t> bars;
    std::vector<std::string> ends;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnStarted && e.who == kGenlock)
        {
            bars.push_back(w.machine.View().timeMs);
        }
        if (e.kind == EventKind::TurnEnded && e.who == kGenlock)
        {
            ends.push_back(e.detail);
        }
    });
    // Three of its four seconds waited, and then a wait that does not fit:
    // it passes, and the second left rolls over.
    w.onPoke = [&](ObjectId id) { (void)w.Wait(id, 3.0f); };
    w.StartFight();
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    REQUIRE(Lines(w, "Genlock's turn ends: waits; 1.00 s rolls over") == 1);
    // Under ground for a round.
    w.SetActive(kGenlock, false);
    const std::int64_t aged = w.agedOwn[kGenlock];
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    // No bar, so no second half to the line; and what it had saved is gone,
    // as from any turn its actor did not end.
    CHECK(std::ranges::count(w.log, std::string{"Genlock's turn ends: inactive"}) == 1);
    CHECK(ends == std::vector<std::string>{"waits", "inactive"});
    // Its clock moved a turn's seconds: what the game has waiting on it (the
    // delayed event that brings a tentacle back up) comes by that clock.
    CHECK(w.agedOwn[kGenlock] - aged == 4000);
    w.SetActive(kGenlock, true);
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    // A turn's own four seconds, where the turn after the one it saved from
    // would have had five.
    CHECK(bars == std::vector<std::uint32_t>{4000, 4000});
}

TEST_CASE("an inactive creature with something queued on it is not waited on: a queue the engine does not run is not "
          "an action",
          "[machine][inactive]")
{
    // The Broodmother's tentacle 0x33f17, rounds 9 to 19: a party member's
    // kept Pinning Shot was given five seconds after the tentacle went under,
    // and what that put on it stayed queued, the engine not running an
    // inactive object's commands. Busy with no current command, its turn
    // stood out max_action_ms each round, and ClearAllCommands did not take
    // it off.
    World w;
    Tellings told{w};
    w.StartFight();
    w.SetActive(kGenlock, false);
    w.QueueOnInactive(kGenlock, CommandType::ExecuteEffect);
    w.applied.clear();
    for (int round = 1; round <= 3; ++round)
    {
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.OnEndTurn());
        const int frames = UntilTurnOf(w, kCompanion);
        REQUIRE(frames >= 0);
        CHECK(frames < 10);
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
    }
    CHECK(Lines(w, "Genlock's turn ends: inactive") == 3);
    CHECK(Lines(w, "still at a ") == 0);
    CHECK(Lines(w, "took too long") == 0);
    CHECK(Lines(w, "chose nothing") == 0);
    CHECK(told.Of(kGenlock) == 0);
    // Nothing of its is cleared, and what waits on it waits still.
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
    CHECK(w.Get(kGenlock).busy);
    CHECK(w.Get(kGenlock).current == CommandType::Invalid);
    // Up again, the engine begins it: an effect's command, which holds the
    // creature as its turn comes like any other.
    w.SetActive(kGenlock, true);
    CHECK(w.Get(kGenlock).current == CommandType::ExecuteEffect);
}

TEST_CASE("made inactive in the middle of its turn, its turn ends there: nothing rolls over and nothing is cleared",
          "[machine][inactive]")
{
    World w{WithABar()};
    Heard heard{w};
    SECTION("its action running")
    {
        w.onPoke = [&](ObjectId id) {
            if (id == kGenlock)
            {
                CHECK(w.Order(id, CommandType::Attack, Source::Script, 0.0f, 1.5f) == Verdict::Allow);
            }
            else
            {
                (void)w.Wait(id, 2.0f);
            }
        };
        w.StartFight();
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        w.RunFor(100);
        REQUIRE(w.machine.GetPhase() == Phase::Executing);
        REQUIRE(w.machine.View().timeLeftMs == 2500);
        // It goes under as it strikes; the engine clears its commands as it
        // does.
        w.SetActive(kGenlock, false);
        REQUIRE_FALSE(w.Get(kGenlock).busy);
        w.applied.clear();
        w.RunFor(50);
        CHECK(w.Current() == kCompanion);
        CHECK(Lines(w, "Genlock is inactive by the game's own word, out of the world: it has no turn, and what is "
                       "left of this one is nobody's") == 1);
        CHECK(Lines(w, "Genlock's turn ends: inactive; nothing rolls over, its actor not having ended it (1.00 s "
                       "would have)") == 1);
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
        CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
        CHECK(Effects(w, EffectKind::Explain, kGenlock) == 0);
    }
    SECTION("told, and not yet chosen: its thinking is not waited out")
    {
        w.onPoke = [&](ObjectId id) {
            if (id != kGenlock)
            {
                (void)w.Wait(id, 2.0f);
            }
        };
        w.StartFight();
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
        w.SetActive(kGenlock, false);
        w.RunFor(50);
        CHECK(w.Current() == kCompanion);
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
        CHECK(Lines(w, "chose nothing") == 0);
    }
    SECTION("while a conversation played in its turn: the turn does not go on after, and nobody is told that it does")
    {
        // Found by the random fights (the long run with creatures out of the
        // world, seed 455): a turn a conversation came in the middle of is
        // gone on with as the fight comes back, its AI told again.
        int told = 0;
        w.onPoke = [&](ObjectId id) {
            if (id == kGenlock)
            {
                ++told;
            }
            else
            {
                (void)w.Wait(id, 2.0f);
            }
        };
        w.StartFight();
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        REQUIRE(told == 1);
        w.mode = Mode::Dialog;
        w.RunFor(50);
        REQUIRE(w.machine.GetPhase() == Phase::Suspended);
        w.SetActive(kGenlock, false);
        w.mode = Mode::Combat;
        w.RunFor(100);
        CHECK(told == 1);
        CHECK(w.Current() == kCompanion);
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
        CHECK(Lines(w, "Genlock's turn goes on") == 0);
        CHECK(Lines(w, "Genlock is inactive by the game's own word, out of the world: it has no turn, and what is "
                       "left of this one is nobody's") == 1);
    }
    SECTION("fallen in the same frame: its turn ends as the fallen's do")
    {
        w.onPoke = [&](ObjectId id) {
            if (id != kGenlock)
            {
                (void)w.Wait(id, 2.0f);
            }
        };
        w.StartFight();
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        w.SetActive(kGenlock, false);
        w.Get(kGenlock).down = true;
        w.RunFor(50);
        CHECK(w.Current() == kCompanion);
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"fell"});
        CHECK(Lines(w, "Genlock is inactive") == 0);
    }
}

TEST_CASE("a party member the game has made inactive is not waited on for an order", "[machine][inactive]")
{
    // The game makes the party's own inactive too (module_core.nss, a
    // follower dropped from the active party; the Fade's and the Landsmeet's
    // scripts). Whether one is ever so in the middle of a fight is not known:
    // the rule is the same for it, with no case of its own.
    World w;
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    SECTION("as its turn comes")
    {
        w.SetActive(kCompanion, false);
        w.applied.clear();
        REQUIRE(w.machine.OnEndTurn());
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
        CHECK(w.machine.Order().Round() == 2);
        CHECK(Lines(w, "round 1: Alistair is inactive by the game's own word, out of the world: it has no turn") == 1);
        CHECK(heard.EndsOf(kCompanion) == std::vector<std::string>{"inactive"});
        CHECK(heard.Of(EventKind::AwaitingOrder, kCompanion) == 0);
        CHECK(heard.Of(EventKind::TurnStarted, kCompanion) == 0);
        CHECK(Effects(w, EffectKind::Select, kCompanion) == 0);
        CHECK(Effects(w, EffectKind::Floaty, kCompanion) == 0);
        // The enemies had theirs on either side of it.
        CHECK(told.Of(kGenlock) == 1);
        CHECK(told.Of(kHurlock) == 1);
    }
    SECTION("while its turn waits on an order")
    {
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        w.SetActive(kHero, false);
        w.RunFor(50);
        CHECK(w.Current() != kHero);
        CHECK(heard.EndsOf(kHero) == std::vector<std::string>{"inactive"});
        CHECK(Lines(w, "Hero is inactive by the game's own word, out of the world: it has no turn, and what is left "
                       "of this one is nobody's") == 1);
    }
}

TEST_CASE("the fight's first turn is passed over like any, its creature inactive as the fight begins",
          "[machine][inactive]")
{
    // The first turn begins a frame after the fight does, on a look at its
    // creature like every turn's.
    World w;
    Tellings told{w};
    Heard heard{w};
    w.Get(kHero).inactive = true;
    w.StartFight();
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    CHECK(Lines(w, "round 1: Hero is inactive by the game's own word, out of the world: it has no turn") == 1);
    CHECK(Lines(w, "what is left of this one is nobody's") == 0);
    CHECK(heard.EndsOf(kHero) == std::vector<std::string>{"inactive"});
    CHECK(heard.Of(EventKind::AwaitingOrder, kHero) == 0);
    CHECK(heard.Of(EventKind::TurnStarted, kHero) == 0);
    CHECK(told.Of(kGenlock) == 1);
}

TEST_CASE("an unaware hostile the game has made inactive has no beat: nobody is there to be read",
          "[machine][inactive]")
{
    World w;
    Tellings told{w};
    Heard heard{w};
    w.Get(kGenlock).perception = Unnoticing();
    w.StartFight();
    SECTION("as its turn comes")
    {
        w.SetActive(kGenlock, false);
        w.applied.clear();
        REQUIRE(w.machine.OnEndTurn());
        const int frames = UntilTurnOf(w, kCompanion);
        REQUIRE(frames >= 0);
        CHECK(frames < 10); // not the second its beat would have been
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
        CHECK_FALSE(w.Said(L"Unaware"));
        CHECK(Lines(w, "has not noticed the party") == 0);
    }
    SECTION("in its beat")
    {
        REQUIRE(w.machine.OnEndTurn());
        REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
        REQUIRE(w.Said(L"Unaware"));
        w.SetActive(kGenlock, false);
        w.RunFor(50);
        CHECK(w.Current() == kCompanion);
        CHECK(heard.EndsOf(kGenlock) == std::vector<std::string>{"inactive"});
    }
}

TEST_CASE("what the mod kept for a creature is not given while it is inactive, and lands on its first turn back",
          "[machine][inactive]")
{
    World w{WithABar()};
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && ++told == 1)
        {
            // Five seconds of swing in a bar of four: kept for its next turn.
            CHECK(w.Order(id, CommandType::Attack, Source::Script, 0.0f, 5.0f) == Verdict::Hold);
        }
        else
        {
            (void)w.Wait(id, 2.0f);
        }
    };
    w.StartFight();
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    REQUIRE(told == 1);
    REQUIRE(Lines(w, "it lands on its next turn") == 1);
    // Under ground through its next turn, and the one after.
    w.SetActive(kGenlock, false);
    for (int round = 0; round < 2; ++round)
    {
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
        REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    }
    CHECK(Lines(w, "Genlock's turn ends: inactive") == 2);
    CHECK(w.released.empty());
    CHECK(Lines(w, "Genlock's pending attack lands") == 0);
    CHECK(Lines(w, "is dropped") == 0);
    // Up again: it lands as that turn begins, paid for as it was to be.
    w.SetActive(kGenlock, true);
    for (int i = 0; i < 2000 && w.released.empty(); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.Current() == kGenlock);
    CHECK(Lines(w, "Genlock's pending attack lands") == 1);
}

TEST_CASE("a kept action is not given at a target the game has made inactive since, as it is not at one fallen",
          "[machine][inactive]")
{
    // The cause of the eleven turns: an archer's Pinning Shot, kept at
    // 05:14:42 for her next turn, was given at 05:14:57 at a tentacle that
    // had gone under at 05:14:51, and what the pin put on an object out of
    // the world sat in its queue. The game's own script stops the party
    // attacking a tentacle as it goes under; what the mod keeps for a
    // creature is outside the engine's queue, where that does not reach. So
    // the game layer finds an inactive target not there, as it finds a dead
    // one (src/game/Bridge.cpp, NotThere; the stand-in's ReleaseCast), and
    // the machine's own path for "its target is gone" does the rest. Its
    // word for why is the machine's: the target still stands in the fight.
    World w{WithABar()};
    Tellings told{w};
    Heard heard{w};
    w.Get(kGenlock).position = {1.5f, 0.0f, 0.0f};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Three of her four seconds on a swing, and a second action of two: it
    // does not fit, is kept, and owes a second of her next turn.
    const auto first = [&] {
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 3.0f) == Verdict::Allow);
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(700);
        REQUIRE(w.Current() == kHero);
    };
    const auto toHerNextTurn = [&] {
        w.RunFor(900);
        REQUIRE(w.Current() != kHero);
        w.SetActive(kGenlock, false); // under ground before it lands
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
        w.RunFor(100);
    };
    SECTION("a plain attack is turned on the nearest enemy in her reach")
    {
        w.Get(kHurlock).position = {0.0f, 1.8f, 0.0f};
        first();
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 2.0f) == Verdict::Hold);
        toHerNextTurn();
        REQUIRE(w.turned.size() == 1);
        CHECK(w.turned.front() == std::pair{kHero, kHurlock});
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Allow);
        CHECK(w.Get(kHero).current == CommandType::Attack);
        CHECK(Lines(w, "Hero's pending attack is turned on Hurlock: its target is out of the world") == 1);
        CHECK(Lines(w, "its target has fallen") == 0);
        // Paid for as it was to be, whoever it lands on.
        CHECK(w.machine.View().timeLeftMs == 3000);
        // And nothing was put on the one under ground.
        CHECK_FALSE(w.Get(kGenlock).busy);
    }
    SECTION("with no enemy in her reach it is dropped, and what it owed given back")
    {
        w.Get(kHurlock).position = {10.0f, 0.0f, 0.0f};
        first();
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 2.0f) == Verdict::Hold);
        toHerNextTurn();
        CHECK(w.released.empty());
        CHECK(w.turned.empty());
        CHECK(Lines(w, "Hero's pending attack is dropped: its target is out of the world, and no enemy is in its "
                       "reach; the 1.00 s it owed given back") == 1);
        REQUIRE(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.View().timeLeftMs == 4000);
    }
    SECTION("a talent is dropped, an enemy in reach or not: its target was chosen")
    {
        // The Pinning Shot itself.
        w.Get(kHurlock).position = {0.0f, 1.8f, 0.0f};
        first();
        CommandRequest talent;
        talent.object = kHero;
        talent.type = CommandType::UseAbility;
        talent.source = Source::Player;
        talent.inCombat = true;
        talent.walk = 0.0f;
        talent.ability = 803;
        talent.abilityType = kAbilityTalent;
        talent.actionSeconds = 2.0f;
        talent.target = kGenlock;
        REQUIRE(w.Queue(talent) == Verdict::Hold);
        toHerNextTurn();
        CHECK(w.released.empty());
        CHECK(w.turned.empty());
        CHECK(Lines(w, "Hero's pending use ability is dropped: its target is out of the world; the 1.00 s it owed "
                       "given back") == 1);
        CHECK(heard.Of(EventKind::ActionDropped, kHero) == 1);
        REQUIRE(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK_FALSE(w.Get(kGenlock).busy);
    }
    SECTION("up again before it lands, the target is struck as ever")
    {
        first();
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 2.0f) == Verdict::Hold);
        w.RunFor(900);
        REQUIRE(w.Current() != kHero);
        w.SetActive(kGenlock, false);
        w.RunFor(200);
        w.SetActive(kGenlock, true);
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
        w.RunFor(100);
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Allow);
        CHECK(w.turned.empty());
        CHECK(Lines(w, "is turned on") == 0);
        CHECK(Lines(w, "is dropped") == 0);
    }
}

TEST_CASE("a hold on an inactive creature takes its turns as on anyone, and what is left of it waits for its first "
          "turn back",
          "[machine][inactive]")
{
    // The same tentacle: pinned for 10.2 s after it had gone under. Rounds 7
    // and 8 were lost to the pin, as they are here; in round 9 the machine
    // took the 2.2 s left of the bar, ended the pin, and told a creature that
    // was not in the world.
    World w{WithABar()};
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    w.SetActive(kGenlock, false);
    w.Get(kGenlock).condition = Condition::Disabled;
    w.Get(kGenlock).conditionSeconds = 10.2f;
    w.applied.clear();
    for (int round = 0; round < 3; ++round)
    {
        REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
        REQUIRE(UntilTurnOf(w, kHero) >= 0);
    }
    CHECK(heard.EndsOf(kGenlock) ==
          std::vector<std::string>{"held by its condition", "held by its condition", "inactive"});
    CHECK(Lines(w, "Genlock's condition has had its turns: ended") == 0);
    CHECK(Effects(w, EffectKind::EndConditions, kGenlock) == 0);
    CHECK(told.Of(kGenlock) == 0);
}
