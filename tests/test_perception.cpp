// What the engine's perception decides in a fight (issue 29), driven through
// scripted fights in the stand-in for the game (World.h), where a creature's
// perception is the case's to say as the game layer would read it of the
// engine: a hostile that has noticed nobody to fight has its turn skipped;
// and one in the fight stays in it while the party perceives it or it the
// party, however far it has gone, and has left when neither does.
#include "World.h"

#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// A turn a bar of time: 4 s, and a second may roll over.
Settings Timed()
{
    Settings s = Fixed();
    s.turnClock = TurnClock::Time;
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

int Floated(const World &w, const std::wstring &text)
{
    return static_cast<int>(std::ranges::count_if(
        w.applied, [&](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == text; }));
}

// Why each of a creature's turns ended, as its events said.
struct Ends
{
    Ends(World &w, ObjectId who)
    {
        w.machine.SetEventSink([this, who](const TurnEvent &e) {
            if (e.kind == EventKind::TurnEnded && e.who == who)
            {
                why.push_back(e.detail);
            }
        });
    }
    std::vector<std::string> why;
};

// The hero's turn ended, and the genlock's begun: it is second in the order.
void ToTheGenlock(World &w)
{
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
}

// The fight on for so many frames, the player's turns ended as they come.
void PlayOn(World &w, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
}

// The fight's events, and what they say of one creature.
struct Heard
{
    explicit Heard(World &w)
    {
        w.machine.SetEventSink([this](const TurnEvent &e) { events.push_back(e); });
    }
    [[nodiscard]] int Count(EventKind kind, ObjectId who) const
    {
        return static_cast<int>(
            std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == who; }));
    }
    [[nodiscard]] int Left(ObjectId who) const
    {
        return static_cast<int>(std::ranges::count_if(events, [&](const TurnEvent &e) {
            return e.kind == EventKind::Gone && e.who == who && e.detail == "no longer fighting";
        }));
    }
    std::vector<TurnEvent> events;
};

int Said(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

// In the order, and standing.
bool InTheFight(const World &w, ObjectId id)
{
    const Combatant *c = w.machine.Order().Find(id);
    return c != nullptr && !c->down;
}

// A door or the like used by a party member whose turn it is: free, and the
// turn waits again after it with a fresh scan.
void UseSomething(World &w, ObjectId id)
{
    REQUIRE(w.Order(id, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(id);
    w.RunFor(800);
}

// A scan put to the machine by hand, as the game layer never gives one: for
// what the machine does with a combatant it did not name.
void Scanned(World &w, const std::vector<Combatant> &scan)
{
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    Observation obs;
    obs.nowMs = w.now;
    obs.mode = Mode::Combat;
    obs.paused = w.paused;
    obs.scan = scan;
    (void)w.machine.Step(obs);
}

// The line said, once, of a combatant that stays in the fight by the
// engine's perception alone.
std::string Afar(std::string_view name, std::string_view why)
{
    return std::format("{} is not found fighting near the party any more (out of combat, or further than 50 m from "
                       "its nearest living member), and stays in the fight: {}",
                       name, why);
}

} // namespace

TEST_CASE("an unaware hostile's turn is skipped: Unaware over it, about a second, its AI not told", "[perception]")
{
    // The Brecilian ruins' lower level: three skeletons that never noticed
    // the party, drawn into its fight, took 54 turns, each told its turn had
    // come, waited on for ai_think_ms and ended "chose nothing" (issue 29).
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    // "Unaware" in grey, and not its name beside it; the world runs.
    const auto said = std::ranges::find_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == L"Unaware"; });
    REQUIRE(said != w.applied.end());
    CHECK(said->target == kGenlock);
    CHECK(said->colour == kGrey);
    CHECK_FALSE(w.Said(L"Genlock"));
    CHECK_FALSE(w.paused);
    CHECK(Logged(w, "Genlock has not noticed the party (not in combat, and it perceives nobody to fight): unaware, "
                    "its AI is not told, and its turn is skipped in 1.0 s"));
    // Most of a second on it is still its turn...
    w.RunFor(800);
    CHECK(w.Current() == kGenlock);
    CHECK(ends.why.empty());
    // ...and just past the second it is over, well short of the second and
    // a half an AI is given to choose in, with nothing asked of its AI.
    w.RunFor(200);
    CHECK(w.Current() == kCompanion);
    CHECK(ends.why == std::vector<std::string>{"unaware"});
    CHECK(pokes == 0);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::Explain, kGenlock));
    CHECK(Floated(w, L"Unaware") == 1);
    // The scripts are told of its turn as of any: a turn began and ended.
    CHECK(std::ranges::count_if(w.applied, [](const Effect &e) {
              return e.kind == EffectKind::Signal && e.event == events::kTurnStart && e.target == kGenlock;
          }) == 1);
}

TEST_CASE("an unaware hostile is skipped every round until it notices, and then plays its turn as anyone",
          "[perception]")
{
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            ++pokes;
        }
        (void)w.Order(id, CommandType::Wait, Source::Script);
    };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    for (int round = 0; round < 2; ++round)
    {
        ToTheGenlock(w);
        w.RunFor(1100);
        REQUIRE(w.Current() == kCompanion);
        (void)w.machine.OnEndTurn();
        w.RunFor(300); // the hurlock waits
        REQUIRE(w.Current() == kHero);
    }
    CHECK(ends.why == std::vector<std::string>{"unaware", "unaware"});
    CHECK(pokes == 0);
    // Struck between its turns, it perceives its attacker (the game's own
    // rules_core): the next is its AI's from its start.
    w.Get(kGenlock).perception = Perception{};
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    CHECK(pokes == 1);
    CHECK(w.Said(L"Genlock"));
    CHECK(Floated(w, L"Unaware") == 2);
    CHECK(ends.why == std::vector<std::string>{"unaware", "unaware", "waits"});
}

TEST_CASE("an unaware hostile that notices in its beat is told at once: it acts from that moment", "[perception]")
{
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        // Its script handles what it perceived: the game takes it into
        // combat, and its AI attacks (rules_core.nss,
        // EVENT_TYPE_PERCEPTION_APPEAR). In combat, the attack is its turn's.
        w.Get(id).perception.inCombat = true;
        CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
    };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    REQUIRE(pokes == 0);
    REQUIRE(w.Current() == kGenlock);
    // The party comes into its perception as the world runs. The scan that
    // began its turn said unaware; the frame's own look at it says not. The
    // game does not have it in combat yet (its script has not handled the
    // event): it perceives someone to fight all the same, has a turn, and is
    // told (core/Rules.h, NothingToFight).
    w.Get(kGenlock).perception.ofParty = true;
    w.Get(kGenlock).perception.ofFoes = true;
    // (Within two frames: it is told on a look at everyone taken with that
    // step, which the first asks for.)
    w.Frame();
    w.Frame();
    CHECK(pokes == 1);
    CHECK(Logged(w, "Genlock has noticed someone to fight: its turn is its AI's"));
    CHECK(w.machine.GetPhase() == Phase::Executing);
    // And the turn is not skipped under its attack when the beat would end.
    w.RunFor(1000);
    CHECK(w.Current() == kGenlock);
    CHECK(ends.why.empty());
}

TEST_CASE("an unaware hostile that notices in its beat is told only on a fresh look: stunned since, it is not",
          "[perception]")
{
    // The random fights' find (seed 135 of the long run with hostiles
    // unaware): struck in its beat by a blow that also stunned it, between
    // two looks at everyone, it was told its turn had come on what the last
    // look had said of it.
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(200); // well short of the half second between two looks
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).perception = Perception{};
    w.Get(kGenlock).condition = Condition::Disabled;
    w.RunFor(100);
    CHECK(pokes == 0);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.why == std::vector<std::string>{"stunned"});
}

TEST_CASE("a hostile that perceives a foe that is not the party's is not unaware", "[perception]")
{
    // Fighting an ally of the party's, the party not yet in its perception:
    // it has someone to choose, and its AI is told.
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    w.Get(kGenlock).perception.ofFoes = true;
    w.StartFight();
    ToTheGenlock(w);
    CHECK(pokes == 1);
    CHECK_FALSE(w.Said(L"Unaware"));
    CHECK(w.Said(L"Genlock"));
}

TEST_CASE("a hostile the game has in combat is told its turn has come, whatever it perceives", "[perception]")
{
    // The scripts run a creature's combat AI on a command's end while it is
    // in combat, and one with an AI of its own script's may act with nobody
    // in its perception (a boss between two phases): skipped as unaware, it
    // would stand for good. So it is told, as it was before there was such a
    // turn; at worst it chooses nothing, as before.
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    w.Get(kGenlock).perception.inCombat = true;
    w.StartFight();
    ToTheGenlock(w);
    CHECK(pokes == 1);
    CHECK_FALSE(w.Said(L"Unaware"));
    CHECK(w.Said(L"Genlock"));
}

TEST_CASE("an unaware hostile the game takes into combat in its beat is told at once", "[perception]")
{
    // Its combat state set (a script's, or the engine's as it perceives a
    // hostile): it has noticed, by the game's own word.
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    REQUIRE(pokes == 0);
    w.Get(kGenlock).perception.inCombat = true;
    w.Frame();
    w.Frame();
    CHECK(pokes == 1);
}

TEST_CASE("an ally the game has in combat is told its turn has come, whoever it perceives; one out of combat with "
          "nobody to fight has no turn",
          "[perception]")
{
    // Unaware is a hostile's: no word stands over an ally, and no beat is
    // waited for it. But a creature that is not of the party and that the
    // game does not have in combat has no turn (issue 43), an ally as an
    // enemy: until 2026-10-05 it was told all the same, its AI had nothing
    // to choose out of combat, and its turn waited ai_think_ms and ended
    // "chose nothing".
    const auto with = [](const Perception &perception) {
        auto fight = StandardFight();
        Creature soldier = Make(0x300, Side::Ally, false, L"Soldier", 19.0f); // after the hero, before the genlock
        soldier.perception = perception;
        fight.push_back(soldier);
        return fight;
    };
    SECTION("in combat, and perceiving nobody: told, as ever")
    {
        Perception alone = Unnoticing();
        alone.inCombat = true;
        World w(Fixed(), with(alone));
        int pokes = 0;
        w.onPoke = [&](ObjectId id) { pokes += id == 0x300 ? 1 : 0; };
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
        REQUIRE(w.Current() == 0x300);
        CHECK(pokes == 1);
        CHECK_FALSE(w.Said(L"Unaware"));
    }
    SECTION("out of combat but perceiving an enemy, its script not yet come to it: told")
    {
        Perception noticing = Unnoticing();
        noticing.ofFoes = true;
        World w(Fixed(), with(noticing));
        int pokes = 0;
        w.onPoke = [&](ObjectId id) { pokes += id == 0x300 ? 1 : 0; };
        w.StartFight();
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
        REQUIRE(w.Current() == 0x300);
        CHECK(pokes == 1);
    }
    SECTION("out of combat, nobody hostile in its perception: no turn, at once, and not told")
    {
        World w(Fixed(), with(Unnoticing()));
        int pokes = 0;
        w.onPoke = [&](ObjectId id) { pokes += id == 0x300 ? 1 : 0; };
        Ends ends(w, 0x300);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        (void)w.machine.OnEndTurn();
        w.RunFor(100); // a few frames: no thinking time is waited out for it
        CHECK(w.Current() == kGenlock);
        CHECK(pokes == 0);
        CHECK(ends.why == std::vector<std::string>{"out of combat"});
        // Its turn came in its place in the round, and was said to: the
        // scripts are told of it as of any.
        CHECK(Logged(w, "Soldier's turn (ally, AI)"));
        CHECK_FALSE(w.Said(L"Unaware"));
        CHECK(Logged(w, "Soldier is out of combat by the game's own word, and perceives nobody to fight: it has no "
                        "turn, and is the game's"));
    }
}

TEST_CASE("with time, an unaware hostile's skipped turn rolls nothing over", "[perception][time]")
{
    World w(Timed());
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    w.Get(kGenlock).perception = Unnoticing();
    std::vector<std::uint32_t> bars;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnStarted && e.who == kGenlock)
        {
            bars.push_back(w.machine.View().timeMs);
        }
    });
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(1100);
    REQUIRE(w.Current() == kCompanion);
    CHECK(Logged(w, "Genlock's turn ends: unaware; nothing rolls over, its actor not having ended it (1.00 s would "
                    "have)"));
    (void)w.machine.OnEndTurn();
    w.RunFor(300);
    REQUIRE(w.Current() == kHero);
    ToTheGenlock(w);
    // Its whole bar unspent, and none of it its own to save.
    CHECK(bars == std::vector<std::uint32_t>{4000, 4000});
}

TEST_CASE("a wait of its script's in an unaware hostile's beat is no choice of its turn", "[perception][time]")
{
    // Its AI was not told. An idle script's wait would else end the turn
    // "waits", as its actor's own doing, and roll its time over.
    World w(Timed());
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    SECTION("out of combat still: the game's, let through, and the beat goes on")
    {
        // One the game does not have in combat has no turn, and what its
        // script has it do is the game's (issue 43): until 2026-10-05 this
        // wait was refused, and one sent home by its script in its own beat
        // lost the wait it was sent with.
        CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Allow);
        CHECK(Logged(w, "Genlock: wait from script let through out of turn: the game does not have it in combat, and "
                        "it is the game's"));
        // Its turn is being skipped already: nothing more is said of it,
        // and the beat is not cut short.
        CHECK_FALSE(Logged(w, "is out of combat by the game's own word"));
    }
    SECTION("taken into combat since the machine last looked: its AI's, refused, and the beat goes on")
    {
        w.Get(kGenlock).perception.inCombat = true;
        CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Block);
        w.Get(kGenlock).perception.inCombat = false; // (and out again, for the beat's end to be seen)
    }
    w.RunFor(300);
    CHECK(w.Current() == kGenlock);
    CHECK(ends.why.empty());
    w.RunFor(500);
    CHECK(ends.why == std::vector<std::string>{"unaware"});
}

TEST_CASE("a wait of a length in an unaware hostile's beat is not taken of its bar either", "[perception][time]")
{
    // With time an AI's wait costs the time it waits and its AI chooses
    // again (issue 31). Not this one's: nobody told its AI to choose, and
    // paid for, its turn would settle and end "done" at once, rolling its
    // time over, for a wait an idle script queued.
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    const std::uint32_t before = w.machine.View().timeLeftMs;
    SECTION("out of combat still: the game's, let through, and nothing of the bar is its price")
    {
        CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Allow);
    }
    SECTION("taken into combat since the machine last looked: its AI's, refused, and not paid for")
    {
        w.Get(kGenlock).perception.inCombat = true;
        CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block);
        w.Get(kGenlock).perception.inCombat = false; // (and out again, for the beat's end to be seen)
    }
    CHECK(w.machine.View().timeLeftMs == before);
    CHECK_FALSE(Logged(w, "Genlock waits 2.00 s"));
    w.RunFor(300);
    CHECK(w.Current() == kGenlock);
    CHECK(ends.why.empty());
    w.RunFor(500);
    CHECK(ends.why == std::vector<std::string>{"unaware"});
    CHECK(pokes == 0);
}

TEST_CASE("a command of its script's in an unaware hostile's beat is the game's: it runs, and the turn is skipped all "
          "the same",
          "[perception][time]")
{
    // A script's order (an ally that has noticed calling it over, its walk
    // home) for one the game does not have in combat is the game's, in its
    // own beat as out of its turn (issue 43): let through, nothing of the
    // turn charged for it, and the beat ends as it would have. Until
    // 2026-10-05 it was taken for the turn's own: the turn waited the walk
    // out, measured it against its creature's movement and ended "done", its
    // time rolled over.
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    const TurnView before = w.machine.View();
    w.applied.clear();
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 20; ++i) // and it walks, further than a turn's movement would let it
    {
        w.Get(kGenlock).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.machine.View().timeLeftMs == before.timeLeftMs);
    CHECK(w.machine.View().movementLeft == before.movementLeft);
    w.RunFor(1000); // past the beat: its turn is skipped, and its walk goes on
    CHECK(w.Current() == kCompanion);
    CHECK(ends.why == std::vector<std::string>{"unaware"});
    CHECK(pokes == 0);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Get(kGenlock).busy);
    CHECK_FALSE(w.Said(L"Out of movement"));
}

TEST_CASE("one that notices under a command of its own is told to choose again once, as anyone", "[perception][time]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        (void)w.Order(id, CommandType::Wait, Source::Script);
    };
    w.Get(kGenlock).perception = Unnoticing();
    Ends ends(w, kGenlock);
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    // It notices, the game takes it into combat, and its script's command
    // comes in the same instant, before the machine has looked at it again:
    // in combat, that command is its turn's own, and the turn is not skipped
    // under it. (One whose command comes while it is still out of combat is
    // the case above: the game's, and the beat goes on.)
    w.Get(kGenlock).perception = Perception{};
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(kGenlock);
    w.RunFor(1200);
    CHECK(pokes == 1);
    CHECK(ends.why == std::vector<std::string>{"waits"});
    CHECK_FALSE(Logged(w, "Genlock has noticed someone to fight"));
}

TEST_CASE("an unaware hostile's beat is of the fight's clock: at twice the speed, half as long", "[perception]")
{
    Settings s = Fixed();
    s.fightSpeed = 2.0f;
    World w(s);
    w.Get(kGenlock).perception = Unnoticing();
    w.StartFight();
    ToTheGenlock(w);
    w.RunFor(300);
    CHECK(w.Current() == kGenlock);
    w.RunFor(300);
    CHECK(w.Current() == kCompanion);
}

// Leaving the fight is the engine's perception (issue 29). The user,
// 2026-10-04: "Once they're in the fight I think they should stay in the
// fight for the distance that the game uses. The game allows you to run away
// and exit a fight. Whatever distance that is, that's the one we should use
// for fight exit, for consistency." The game has no distance: it ends combat
// when the party perceives no hostile. In the stand-in, `fighting` false is
// what the scan's own rules say of a creature out of combat or further than
// combat_radius from every living party member.

TEST_CASE("a hostile the scan no longer finds fighting stays in the fight while the party perceives it or it the party",
          "[perception]")
{
    struct Case
    {
        Perception perception;
        const char *why;
    };
    Perception watching = Unperceived(); // the party has lost it; it has not lost the party
    watching.ofParty = true;
    const Case cases[] = {
        {Perception{}, "it and the party perceive each other"},
        {Unnoticing(), "the party perceives it"}, // and it has noticed nobody: out of combat, and still in the fight
        {watching, "it perceives the party"},
    };
    for (const Case &c : cases)
    {
        CAPTURE(c.why);
        World w;
        w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
        Heard heard(w);
        w.StartFight();
        // It runs past the radius, or drops out of combat.
        w.Get(kGenlock).fighting = false;
        w.Get(kGenlock).perception = c.perception;
        PlayOn(w, 1500); // 24 s, a hundred scans and more
        CHECK(InTheFight(w, kGenlock));
        CHECK(heard.Left(kGenlock) == 0);
        // It has its turns as before, and it is said once.
        CHECK(heard.Count(EventKind::TurnEnded, kGenlock) > 3);
        CHECK(Said(w, Afar("Genlock", c.why)) == 1);
        // Found fighting again, and then not: said again.
        w.Get(kGenlock).fighting = true;
        PlayOn(w, 100);
        w.Get(kGenlock).fighting = false;
        PlayOn(w, 100);
        CHECK(Said(w, Afar("Genlock", c.why)) == 2);
        CHECK(heard.Left(kGenlock) == 0);
        CHECK(heard.Count(EventKind::UpAgain, kGenlock) == 0);
        CHECK(heard.Count(EventKind::Joined, kGenlock) == 0);
    }
}

TEST_CASE("one in the fight that has noticed nobody, out of combat and in the party's view, has its turns skipped",
          "[perception]")
{
    // The skeletons again: not in combat by the game's reckoning, so no
    // rule that brings a creature in finds them once another has noticed;
    // the party perceives them, so they stay, unaware.
    World w;
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        pokes += id == kGenlock ? 1 : 0;
        (void)w.Order(id, CommandType::Wait, Source::Script);
    };
    w.Get(kGenlock).perception = Unnoticing();
    w.Get(kGenlock).fighting = false;
    Ends ends(w, kGenlock);
    w.StartFight();
    // (Not found fighting as the fight begins, it is not in it: nothing
    // brings it in but the rules that find it.)
    CHECK(w.machine.Order().Find(kGenlock) == nullptr);
    w.Get(kGenlock).fighting = true;
    PlayOn(w, 100);
    REQUIRE(InTheFight(w, kGenlock));
    w.Get(kGenlock).fighting = false;
    PlayOn(w, 600);
    CHECK(InTheFight(w, kGenlock));
    CHECK(pokes == 0);
    REQUIRE(ends.why.size() > 2);
    CHECK(std::ranges::all_of(ends.why, [](const std::string &why) { return why == "unaware"; }));
}

TEST_CASE("running from a fight: an enemy stays in it while either side perceives the other, and it is over when "
          "neither does",
          "[perception]")
{
    World w;
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    Heard heard(w);
    w.StartFight();
    // The party runs, the enemies after it: soon they are further from it
    // than combat_radius, where until now they were out of the fight and
    // stood frozen.
    w.Get(kGenlock).fighting = false;
    w.Get(kHurlock).fighting = false;
    PlayOn(w, 600);
    REQUIRE(w.machine.Active());
    CHECK(InTheFight(w, kGenlock));
    CHECK(InTheFight(w, kHurlock));
    // The genlock loses the party, and the party it. Not at once: one scan
    // or two without it is not that.
    w.Get(kGenlock).perception = Unperceived();
    PlayOn(w, 150); // 2.4 s
    CHECK(InTheFight(w, kGenlock));
    CHECK(heard.Left(kGenlock) == 0);
    PlayOn(w, 250); // past the four seconds
    CHECK_FALSE(InTheFight(w, kGenlock));
    CHECK(heard.Left(kGenlock) == 1);
    CHECK(Said(w, "Genlock is no longer fighting: out of combat or out of range, and neither it nor the party "
                  "perceives the other") == 1);
    // The hurlock is still after them, and the fight goes on.
    CHECK(w.machine.Active());
    CHECK(InTheFight(w, kHurlock));
    CHECK(heard.Left(kHurlock) == 0);
    // Lost too: nobody is left to fight, and the fight is over.
    w.Get(kHurlock).perception = Unperceived();
    PlayOn(w, 400);
    CHECK_FALSE(w.machine.Active());
    CHECK(heard.Left(kHurlock) == 1);
    CHECK(Said(w, "the turn-based fight is over after ") == 1);
    // Said as it was: they left it, nobody felled them.
    CHECK(Logged(w, ": no enemy left in the fight, 2 having left it alive"));
    CHECK_FALSE(Logged(w, ": no enemy left standing"));
    // And it does not begin again for their being about.
    PlayOn(w, 200);
    CHECK_FALSE(w.machine.Active());
}

TEST_CASE("a fight run from is over with its enemies alive, and getting close to them begins a new one", "[perception]")
{
    // The user, 2026-10-04, of issues 42 and 43: "If you run far enough away
    // from the enemies, the fight should end. That's what rtwp does. In that
    // case the enemies should go back to normal. Getting close to them
    // should retrigger a fight." In the stand-in, `fighting` is what the
    // scan's own rules say: in combat within combat_radius of a living party
    // member, whoever leads (issue 42).
    World w;
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    Heard heard(w);
    w.StartFight();
    const auto runFrom = [&] {
        for (const ObjectId id : {kGenlock, kHurlock})
        {
            w.Get(id).fighting = false;
            w.Get(id).perception = Unperceived();
        }
        PlayOn(w, 600);
    };
    // The whole party runs until no rule finds either enemy and neither side
    // perceives the other: the fight is over, and they are alive.
    runFrom();
    REQUIRE_FALSE(w.machine.Active());
    CHECK(Logged(w, ": no enemy left in the fight, 2 having left it alive"));
    CHECK(heard.Count(EventKind::FightOver, kNoObject) == 1);
    CHECK_FALSE(w.Get(kGenlock).down);
    CHECK_FALSE(w.Get(kHurlock).down);
    // Their commands are their own again, in real time.
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    // While the party stays away nothing begins, the game still saying
    // combat.
    PlayOn(w, 200);
    CHECK_FALSE(w.machine.Active());
    // One of the party comes back within reach and the genlock is in combat
    // again: a new fight, with turns, and the hurlock that has not been met
    // again is not in it.
    w.Get(kGenlock).fighting = true;
    w.Get(kGenlock).perception = Perception{};
    const int turnsBefore = heard.Count(EventKind::TurnEnded, kGenlock);
    PlayOn(w, 400);
    REQUIRE(w.machine.Active());
    CHECK(Logged(w, "hostiles are fighting again: turn-based"));
    CHECK(heard.Count(EventKind::FightBegan, kNoObject) == 2);
    CHECK(InTheFight(w, kGenlock));
    CHECK_FALSE(InTheFight(w, kHurlock));
    CHECK(heard.Count(EventKind::TurnEnded, kGenlock) > turnsBefore);
    // Run from again, and this time the game itself ends combat before
    // anyone comes back: the next fight begins as any does, when the game
    // says combat and a hostile is found fighting near a living member.
    runFrom();
    REQUIRE_FALSE(w.machine.Active());
    w.mode = Mode::Explore;
    PlayOn(w, 100);
    w.log.clear();
    w.mode = Mode::Combat;
    w.Get(kHurlock).fighting = true;
    w.Get(kHurlock).perception = Perception{};
    PlayOn(w, 100);
    REQUIRE(w.machine.Active());
    CHECK(Logged(w, "combat began: turn-based"));
    CHECK(heard.Count(EventKind::FightBegan, kNoObject) == 3);
    CHECK(InTheFight(w, kHurlock));
    CHECK_FALSE(InTheFight(w, kGenlock));
}

TEST_CASE("an ally the scan no longer finds stays while it perceives an enemy, and has left when it perceives none",
          "[perception]")
{
    auto fight = StandardFight();
    fight.push_back(Make(0x300, Side::Ally, false, L"Soldier", 12.0f));
    World w(Fixed(), fight);
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    Heard heard(w);
    w.StartFight();
    // After an enemy past the radius: left to the radius it would stand
    // frozen there, under the blows of an enemy that stays in the fight.
    w.Get(0x300).fighting = false;
    PlayOn(w, 600);
    CHECK(InTheFight(w, 0x300));
    CHECK(heard.Left(0x300) == 0);
    CHECK(Said(w, Afar("Soldier", "it perceives an enemy")) == 1);
    // Its enemy lost. That the party and it perceive each other is no fight.
    w.Get(0x300).perception.ofFoes = false;
    PlayOn(w, 400);
    CHECK_FALSE(InTheFight(w, 0x300));
    CHECK(heard.Left(0x300) == 1);
    CHECK(Said(w, "Soldier is no longer fighting: out of combat or out of range, and it perceives no enemy") == 1);
    CHECK(w.machine.Active());
}

TEST_CASE("being perceived brings nobody into a fight, nor back into one it has left", "[perception]")
{
    World w;
    Heard heard(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A hostile that is not in the fight, not found fighting, and in the
    // party's perception: a scan as the game layer does not give one (it
    // reads so only those the machine named).
    std::vector<Combatant> scan = w.machine.Order().All();
    Combatant stranger = *w.machine.Order().Find(kGenlock);
    stranger.id = 0x999;
    stranger.name = L"Stranger";
    stranger.fighting = false;
    scan.push_back(stranger);
    Scanned(w, scan);
    CHECK(w.machine.Order().Find(0x999) == nullptr);
    CHECK(heard.Count(EventKind::Joined, 0x999) == 0);
    // The genlock leaves the fight, within the hero's one long turn, so
    // that it is still in the order, down.
    w.Get(kGenlock).fighting = false;
    w.Get(kGenlock).perception = Unperceived();
    UseSomething(w, kHero);
    w.RunFor(4500);
    UseSomething(w, kHero);
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    REQUIRE(w.machine.Order().Find(kGenlock)->down);
    REQUIRE(heard.Left(kGenlock) == 1);
    // The party has it in view again, and it is still not found fighting:
    // it left, and is back only when a scan finds it fighting.
    scan = w.machine.Order().All();
    for (Combatant &c : scan)
    {
        if (c.id == kGenlock)
        {
            c.down = false;
            c.fighting = false;
            c.perception = Perception{};
        }
    }
    Scanned(w, scan);
    CHECK(w.machine.Order().Find(kGenlock)->down);
    CHECK(heard.Count(EventKind::UpAgain, kGenlock) == 0);
    w.Get(kGenlock).fighting = true;
    w.Get(kGenlock).perception = Perception{};
    UseSomething(w, kHero);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(heard.Count(EventKind::UpAgain, kGenlock) == 1);
}

TEST_CASE("one that left the fight and is back in it stops what it took up while it was out", "[perception]")
{
    // The random fights' find (seed 165 of the long run with combatants out
    // of reach): a conversation's scripts set one that had left walking,
    // the gate standing aside; back in the fight, it walked on through
    // another's turn. A joiner's commands are cleared as it joins; so are
    // those of one that comes back.
    World w;
    Heard heard(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kGenlock).fighting = false;
    w.Get(kGenlock).perception = Unperceived();
    UseSomething(w, kHero);
    w.RunFor(4500);
    UseSomething(w, kHero);
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    REQUIRE(w.machine.Order().Find(kGenlock)->down);
    // A conversation over the fight: its scripts move it.
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    REQUIRE(w.Get(kGenlock).busy);
    // It is found fighting again as the fight comes back.
    w.Get(kGenlock).fighting = true;
    w.Get(kGenlock).perception = Perception{};
    w.mode = Mode::Combat;
    w.RunFor(100);
    CHECK(heard.Count(EventKind::UpAgain, kGenlock) == 1);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK_FALSE(w.Get(kGenlock).busy);
    CHECK(w.Current() == kHero);
}
