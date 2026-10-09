// A move that goes nowhere (issue 59). The game's scripts answer the end of
// a command with the next, at once, and a move that is done the moment it
// begins is ordered again: it takes nothing of its creature's bar, and in its
// own turn nothing else happens that would give its script something else to
// do. A summoned wolf's script ordered 633 moves to a location in a turn
// (2026-10-06, staged) and Arl Eamon's 865 moves to an object in the
// Archdemon's fight (2026-10-09), each turn ended by the limit on a turn an
// AI plays, thirty seconds of the wall with the fight standing. Scripted
// cases in the stand-in for the game (World.h), whose Finish ends a command
// as done and tells the machine so, as the game layer does (Gate.cpp's
// OnEnd). What is promised:
//
//   - a move its AI ordered that is done with none of its bar's meters
//     walked is its AI's pass: the turn ends as it settles, "done", what is
//     left of its bar rolling over, and what its script orders after it is
//     refused;
//   - one that walked, however little, is a move like any;
//   - one the engine failed is not a pass: its script is told of the failure
//     and answers with something else (in the kept logs a weapon set or a
//     mode and then a blow or a talent, fifteen turns of sixteen);
//   - nor is the end of another command of its creature's, nor the mod's own
//     walk to a kept attack's target, which its AI did not order;
//   - a turn the player plays is untouched.
#include "World.h"

#include "core/Events.h"
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

constexpr ObjectId kWolf = 0x102;
constexpr ObjectId kEamon = 0x300;

// 4 s turns, a pace of 5 m/s for round numbers, a meter free.
Settings Timed()
{
    Settings s = Fixed();
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// Why each of a creature's turns ended.
void KeepEnds(World &w, ObjectId id, std::vector<std::string> &ends)
{
    w.machine.SetEventSink([&ends, id](const TurnEvent &e) {
        if (e.kind == EventKind::TurnEnded && e.who == id)
        {
            ends.push_back(e.detail);
        }
    });
}

// The fight begun and the hero's turn ended: the next one's AI has been told.
void EndTheHerosTurn(World &w)
{
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(300);
}

// A swing of a second and a half at someone in reach, as a script queues one.
Verdict Swing(World &w, ObjectId id, ObjectId target)
{
    return w.Attack(id, target, Source::Script, NoAttack::None, 0.0f, 1.5f);
}

// The game's scripts while a creature's turn lasts, for a minute at the
// most: with every frame the command it is at is done, and its script orders
// the same move again, as rules_core does on the engine's COMMAND_COMPLETE.
// Answers how many of those the gate let through, and how long the turn went
// on.
struct Ordered
{
    int moves = 0;
    std::uint32_t ranMs = 0;
};
Ordered OrderedAgainAsEachEnds(World &w, ObjectId id, CommandType move)
{
    Ordered ordered;
    for (; ordered.ranMs < 60000 && w.Current() == id; ordered.ranMs += 16)
    {
        if (w.Get(id).busy)
        {
            w.Finish(id);
        }
        ordered.moves += w.Order(id, move, Source::Script) == Verdict::Allow ? 1 : 0;
        w.Frame();
    }
    return ordered;
}

} // namespace

TEST_CASE("a summon with nobody to attack, its script walking it to its place by its leader as each walk ends: its "
          "turn is passed at the first walk that goes nowhere",
          "[nowhere][time]")
{
    // The staged wolf (2026-10-06): `Wolf: move to location (move, from
    // script); 4.00 s and 19.6 m left`, then the same with 19.5 m left every
    // 47 ms, 633 of them, and `took too long`. A follower with no target
    // walks to its place in the party's formation while it stands further
    // than melee range from its leader (ai_main_h_2.nss, _AI_MoveToControlled)
    // and its last command was done: the first walk a step, each after it
    // done where it began.
    Settings s = Timed();
    s.partyControl = PartyControl::Tactics;
    World w(s, {Make(kHero, Side::Party, true, L"Hero", 20.0f), Make(kWolf, Side::Party, false, L"Wolf", 19.0f),
                Make(kGenlock, Side::Enemy, false, L"Genlock", 10.0f)});
    std::vector<std::string> ends;
    KeepEnds(w, kWolf, ends);
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id != kWolf)
        {
            (void)w.Wait(id, 0.0f);
            return;
        }
        ++told;
        CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kWolf);
    REQUIRE(told == 1);
    // The step to its place: a walk like any, and no pass.
    w.Walk(kWolf, 0.1f);
    const Ordered again = OrderedAgainAsEachEnds(w, kWolf, CommandType::MoveToLocation);
    // One more was let through, which was done where it began; and its turn
    // was over a few frames on, not thirty seconds.
    CHECK(again.moves == 1);
    CHECK(again.ranMs <= 160);
    CHECK(told == 1);
    CHECK(ends == std::vector<std::string>{"done"});
    CHECK(Logged(w, "Wolf's move to location is done where it began, nothing of its bar walked: what costs nothing "
                    "could be ordered for ever, and it has passed (4.00 s and 20.9 m left)"));
    CHECK_FALSE(Logged(w, "turn is still going after"));
    // Its AI's own doing, as a wait is: what is left rolls over, to its cap.
    CHECK(Logged(w, "Wolf's turn ends: done; 1.00 s rolls over"));
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an ally with nobody on its threat table, its script walking it to its leader as each command ends: its "
          "turn is passed at the first walk that goes nowhere",
          "[nowhere][time]")
{
    // Arl Eamon on Fort Drakon's roof (2026-10-09): an attack, its target
    // felled by another; `move to object (move, from script); 2.21 s and 10.5
    // m left`, a walk of 5.7 m; and then the same with `1.04 s and 4.8 m
    // left` every frame, 864 of them, until `Arl Eamon's turn is still going
    // after 90000 ms, 866 of its commands let through: ended`. The army's
    // script walks one with an empty threat table to within 8 m of the
    // party's leader, whatever its last command came to
    // (cli000cr_army_member.nss, MoveToLeader): within 8 m, each is done as
    // it begins.
    World w(Timed(),
            {Make(kHero, Side::Party, true, L"Hero", 20.0f), Make(kEamon, Side::Ally, false, L"Arl Eamon", 19.0f),
             Make(kGenlock, Side::Enemy, false, L"Genlock", 10.0f)});
    std::vector<std::string> ends;
    KeepEnds(w, kEamon, ends);
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id != kEamon)
        {
            (void)w.Wait(id, 0.0f);
            return;
        }
        ++told;
        CHECK(Swing(w, id, kGenlock) == Verdict::Allow);
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kEamon);
    REQUIRE(told == 1);
    // His blow is struck; and with nobody left on his threat table his script
    // walks him to his leader, five and a half meters of it.
    w.RunFor(200);
    w.Finish(kEamon);
    REQUIRE(w.Order(kEamon, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
    w.Walk(kEamon, 5.5f);
    const std::uint32_t left = w.machine.View().timeLeftMs;
    CHECK(left < 2500);
    const Ordered again = OrderedAgainAsEachEnds(w, kEamon, CommandType::MoveToObject);
    CHECK(again.moves == 1);
    CHECK(again.ranMs <= 160);
    CHECK(told == 1);
    CHECK(ends == std::vector<std::string>{"done"});
    CHECK(Logged(w, "Arl Eamon's move to object is done where it began, nothing of its bar walked: what costs nothing "
                    "could be ordered for ever, and it has passed"));
    CHECK_FALSE(Logged(w, "turn is still going after"));
    CHECK_FALSE(Logged(w, "Arl Eamon chooses again"));
    // The fight goes on with the next one's turn.
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an AI that answers being told its turn has come with a move done where it stands has passed, and is not "
          "told again",
          "[nowhere][time]")
{
    // Until issue 59 it was told again with its bar whole for as long as its
    // turn might last (215 times in two minutes, tried with no limit).
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id != kGenlock)
        {
            (void)w.Wait(id, 0.0f);
            return;
        }
        ++told;
        CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
        w.Finish(id);
        // And what its script orders as that ends is refused, as after any
        // pass: nothing of its creature's is let through in a turn that is
        // over.
        CHECK(w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Block);
        CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
    };
    EndTheHerosTurn(w);
    CHECK(told == 1);
    CHECK(ends == std::vector<std::string>{"done"});
    CHECK(w.Current() == kCompanion);
    CHECK_FALSE(Logged(w, "Genlock chooses again"));
}

TEST_CASE("a move the engine fails is no pass: its script is told of the failure, and what it answers with is let "
          "through",
          "[nowhere][time]")
{
    // A Carta thug (2026-10-04): `move to object (move, from script); 3.89 s
    // and 19.1 m left`, `move to object ended COMMAND_FAILED_INVALID_PATH`
    // a millisecond on, `switch weapons (free, from script)`, and then an
    // attack and a talent. Fifteen such turns in the kept logs: passed at the
    // move, each would have stood through its turn, and through the next.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            CHECK(w.Order(id, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
            (void)w.Fail(id, "COMMAND_FAILED_INVALID_PATH");
            CHECK(w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
        }
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.Finish(kGenlock);
    CHECK(Swing(w, kGenlock, kHero) == Verdict::Allow);
    w.RunFor(200);
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.View().timeLeftMs == 2500);
    CHECK(ends.empty());
    CHECK_FALSE(Logged(w, "has passed"));
}

TEST_CASE("the end of another command of its creature's is not its move's: no pass", "[nowhere][time]")
{
    // What the engine puts in front of a walk not yet begun (an effect's
    // command, which the gate does not judge) ends as done before the walk
    // has taken a step.
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
        }
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.machine.OnCommandDone(kGenlock, CommandType::ExecuteEffect);
    CHECK_FALSE(Logged(w, "has passed"));
    // Nor is another creature's move the current one's.
    w.machine.OnCommandDone(kHurlock, CommandType::MoveToLocation);
    CHECK_FALSE(Logged(w, "has passed"));
    CHECK(w.Order(kGenlock, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow);
}

TEST_CASE("a story's walk for the creature whose turn a conversation stands over is no pass of that turn's",
          "[nowhere][time]")
{
    World w(Timed());
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && ++told == 1)
        {
            CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
        }
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    // Its walk let through and no step of it taken as a conversation begins
    // over the fight: the gate stands aside, and the story walks it, a move
    // done where it stands.
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    CHECK_FALSE(Logged(w, "has passed"));
    // The fight is back, and its turn goes on: its AI is told again.
    w.mode = Mode::Combat;
    w.RunFor(300);
    CHECK(w.Current() == kGenlock);
    CHECK(told == 2);
}

TEST_CASE("the mod's own walk to a kept attack's target, done where it began, is no pass of its AI's",
          "[nowhere][time]")
{
    // A hurlock (2026-10-04): `begins attack (2.00 s and 0.16 s of walk)
    // ...; it walks to its target now`, `move to object (move, from script);
    // 0.00 s and 2.8 m left`, and then its script's own `use ability (free,
    // from script)`, a mode, let through. The walk is the game layer's, given
    // for the machine (EffectKind::Approach): a short one is over within a
    // frame at three times the speed, before a step of it is seen.
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            // 4.5 s of swing and 5 m to walk on a bar of 4 s: kept, and it
            // walks to its target now.
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, 5.0f, 4.5f) == Verdict::Hold);
        }
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.approaches == 1);
    REQUIRE(w.Get(kGenlock).current == CommandType::MoveToObject);
    w.Finish(kGenlock);
    CHECK_FALSE(Logged(w, "has passed"));
    CHECK(w.Switch(kGenlock, Source::Script, 7) == Verdict::Allow);
}

TEST_CASE("a turn the player plays is untouched by a move of her own that goes nowhere", "[nowhere][time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A click on the ground she stands on.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 0.0f) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK_FALSE(Logged(w, "has passed"));
    CHECK(Swing(w, kHero, kGenlock) == Verdict::Block); // (a script's, held: her turn is the player's)
    CHECK(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.5f) == Verdict::Allow);
}
