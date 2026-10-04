// An AI's wait with a turn a bar of time (issue 31). The game's AI waits a
// second or two between two things it does (CommandWait(AI_DO_NOTHING_DELAY),
// 2 s, or its quick one, 1 s) and chooses again as the wait ends. The mod
// took a wait for a pass, and the turn was over with the rest of its bar
// unspent. The user: "Shouldn't this cost the time equal to how long it
// waits? If it's less than the amount of time it has to take actions in a
// turn, it can still take more actions". Scripted cases in the stand-in for
// the game (World.h). What is promised:
//
//   - a wait takes its own length of the bar, and its creature's clock moves
//     by it;
//   - with time left its AI is told to choose again, at once;
//   - a wait longer than what is left takes nothing and ends the turn, as
//     every wait did;
//   - so does one of no length, or whose length the game layer did not read;
//   - a turn's waits and its commands together stop at max_commands_per_turn;
//   - a turn the player plays is untouched, and so is a turn of one action
//     and its meters (`clock = actions`).
#include "World.h"

#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// Turns of 5 s for everyone in the standard fight (nobody has Combat Tactics
// or a rank), a pace of 5 m/s for round numbers, a meter free.
Settings Timed()
{
    Settings s = Fixed();
    s.turnClock = TurnClock::Time;
    s.secondsPerTurn = 5.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// An attack at something in reach: nothing to walk.
Verdict Swing(World &w, ObjectId id, Source source, float seconds)
{
    CommandRequest request;
    request.object = id;
    request.type = CommandType::Attack;
    request.source = source;
    request.inCombat = w.Get(id).fighting;
    request.walk = 0.0f;
    request.actionSeconds = seconds;
    return w.Queue(request);
}

// The fight begun and the hero's turn ended: the genlock's is next, and its
// AI answers as the case's onPoke has it.
void EndTheHerosTurn(World &w)
{
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(300);
}

std::uint32_t BarLeft(const World &w)
{
    return w.machine.View().timeLeftMs;
}

} // namespace

TEST_CASE("with time, an AI's wait costs the time it waits, and its creature chooses again with what is left",
          "[time][wait]")
{
    World w(Timed());
    std::vector<std::uint32_t> barAsTold;
    std::int64_t clockAsToldAgain = -1;
    w.onPoke = [&](ObjectId id) {
        barAsTold.push_back(BarLeft(w));
        if (barAsTold.size() == 1)
        {
            // CommandWait(AI_DO_NOTHING_DELAY): refused at the queue, as a
            // wait always was, and 2 s of its 5 taken.
            CHECK(w.Wait(id, 2.0f) == Verdict::Block);
            CHECK(BarLeft(w) == 3000);
            return;
        }
        // Told again: the 2 s have passed on its own clock by now (its
        // cooldowns, its effects), before it chooses.
        clockAsToldAgain = w.agedOwn[id];
        CHECK(Swing(w, id, Source::Script, 1.5f) == Verdict::Allow);
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    // Within the 300 ms: its AI is not left thinking (ai_think_ms, 1.5 s).
    CHECK(barAsTold == std::vector<std::uint32_t>{5000, 3000});
    CHECK(clockAsToldAgain == 2000);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    CHECK(BarLeft(w) == 1500);
    CHECK(Logged(w, "Genlock waits 2.00 s; 3.00 s and 16.0 m left"));
    CHECK(Logged(w, "Genlock chooses again: 3.00 s and 16.0 m left"));
    CHECK_FALSE(Logged(w, "Genlock's turn ends"));
}

TEST_CASE("with time, a wait longer than what is left takes nothing and ends the turn", "[time][wait]")
{
    // A creature with nothing to do: it waits, is told again, waits again,
    // and the third wait does not fit. No rule of its own bounds it: its bar
    // does.
    World w(Timed());
    std::vector<std::uint32_t> barAfterWait;
    w.onPoke = [&](ObjectId id) {
        CHECK(w.Wait(id, 2.0f) == Verdict::Block);
        barAfterWait.push_back(BarLeft(w));
    };
    EndTheHerosTurn(w);
    // 5 s, 3 s, and with 1 s left the wait of 2 is the pass it was: the
    // second left is not taken by it.
    CHECK(barAfterWait == std::vector<std::uint32_t>{3000, 1000, 1000});
    CHECK(w.Current() == kCompanion); // all of it within the 300 ms
    CHECK(Logged(w, "Genlock's wait of 2.00 s is longer than the 1.00 s it has left: its turn ends"));
    CHECK(Logged(w, "Genlock's turn ends: waits"));
}

TEST_CASE("with time, a wait that takes the last of the bar ends the turn with nothing left", "[time][wait]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        CHECK(w.Wait(id, 2.5f) == Verdict::Block);
    };
    EndTheHerosTurn(w);
    // Twice 2.5 s is its 5: both paid for, and nothing left to choose with.
    CHECK(pokes == 2);
    CHECK(w.Current() == kCompanion);
    CHECK(Logged(w, "Genlock waits 2.50 s; 0.00 s and"));
    CHECK_FALSE(Logged(w, "is longer than"));
}

TEST_CASE("with time, the wait an AI chooses as its command ends costs its time, and the turn goes on", "[time][wait]")
{
    World w(Timed());
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        CHECK(Swing(w, id, Source::Script, 1.5f) == Verdict::Allow);
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 1);
    REQUIRE(BarLeft(w) == 3500);
    // Its swing ends, and the engine's COMMAND_COMPLETE runs its AI, which
    // waits: the turn was over here ("done"), 3.5 s unspent.
    w.Finish(kGenlock);
    CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block);
    CHECK(BarLeft(w) == 1500);
    // It settles as after any command, and its AI is told to choose again.
    w.RunFor(700);
    CHECK(pokes == 2);
    CHECK(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    CHECK(BarLeft(w) == 0);
    CHECK(Logged(w, "Genlock chooses again: 1.50 s and"));
}

TEST_CASE("with time, a wait of no length, or one whose length was not read, ends the turn as before", "[time][wait]")
{
    // It would cost nothing, and could be chosen for ever.
    for (const float seconds :
         {0.0f, -2.0f, 0.0004f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        CAPTURE(seconds);
        World w(Timed());
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            ++pokes;
            CHECK(w.Wait(id, seconds) == Verdict::Block);
            CHECK(BarLeft(w) == 5000);
        };
        EndTheHerosTurn(w);
        CHECK(pokes == 1);
        CHECK(w.Current() == kCompanion);
        CHECK(Logged(w, "Genlock's wait has no length that was read: it passes its turn"));
        CHECK(Logged(w, "Genlock's turn ends: waits"));
    }
}

TEST_CASE("with time, a turn's waits and its commands together stop at max_commands_per_turn", "[time][wait]")
{
    Settings s = Timed();
    s.maxCommandsPerTurn = 3;
    World w(s);
    std::vector<std::uint32_t> barAfter;
    w.onPoke = [&](ObjectId id) {
        if (barAfter.empty())
        {
            CHECK(Swing(w, id, Source::Script, 0.5f) == Verdict::Allow);
        }
        else
        {
            CHECK(w.Wait(id, 1.0f) == Verdict::Block);
        }
        barAfter.push_back(BarLeft(w));
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.Finish(kGenlock);
    w.RunFor(900);
    // A swing and two waits are its three: the third wait, with 2.5 s still
    // to wait with, is not paid for and ends the turn.
    CHECK(barAfter == std::vector<std::uint32_t>{4500, 3500, 2500, 2500});
    CHECK(w.Current() == kCompanion);
    CHECK(Logged(w, "Genlock has started 3 commands this turn, its 2 waits among them: no more"));
    CHECK(Logged(w, "Genlock's turn ends: waits"));
}

TEST_CASE("with time, the camera goes to an AI's creature at its first command let through, a wait before it or not",
          "[time][wait]")
{
    // A wait is refused and shows nothing: counted as a command let through,
    // the camera would never go to a creature that waited first.
    Settings s = Timed();
    s.cameraFollow = true;
    World w(s);
    int pokes = 0;
    bool cameraAtTheWait = false;
    w.onPoke = [&](ObjectId id) {
        if (++pokes == 1)
        {
            CHECK(w.Wait(id, 2.0f) == Verdict::Block);
            return;
        }
        cameraAtTheWait = w.Did(EffectKind::CenterCamera, id);
        CHECK(Swing(w, id, Source::Script, 1.5f) == Verdict::Allow);
    };
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(pokes == 2);
    CHECK_FALSE(cameraAtTheWait);
    CHECK(w.Did(EffectKind::CenterCamera, kGenlock));
}

TEST_CASE("with time, a turn the player plays is untouched by its character's own AI's wait", "[time][wait]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    // A follower's full AI, with no enemy to choose, waits a second (issue
    // 10): held, nothing of her bar taken, the turn still hers to order.
    CHECK(w.Wait(kHero, 1.0f) == Verdict::Block);
    w.RunFor(300);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(BarLeft(w) == 5000);
    // And as her own order's command ends, when that AI runs again.
    CHECK(Swing(w, kHero, Source::Player, 1.5f) == Verdict::Allow);
    w.Finish(kHero);
    CHECK(w.Wait(kHero, 1.0f) == Verdict::Block);
    w.RunFor(700);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(BarLeft(w) == 3500);
    CHECK_FALSE(Logged(w, "Hero waits"));
    CHECK_FALSE(Logged(w, "Hero's wait"));
}

TEST_CASE("with time and party = tactics, a companion's turn is its AI's, and its wait costs its time", "[time][wait]")
{
    Settings s = Timed();
    s.partyControl = PartyControl::Tactics;
    World w(s);
    std::vector<std::uint32_t> companionsBar;
    w.onPoke = [&](ObjectId id) {
        if (id != kCompanion)
        {
            (void)w.Wait(id, 0.0f); // the genlock passes
            return;
        }
        CHECK(w.Wait(id, 2.0f) == Verdict::Block);
        companionsBar.push_back(BarLeft(w));
    };
    EndTheHerosTurn(w);
    w.RunFor(300);
    // As the genlock's above: 3 s, 1 s, and the third wait ends its turn.
    CHECK(companionsBar == std::vector<std::uint32_t>{3000, 1000, 1000});
    CHECK(Logged(w, "Alistair waits 2.00 s; 3.00 s and"));
    CHECK(Logged(w, "Alistair's turn ends: waits"));
}

TEST_CASE("with a turn one action and its meters, a wait passes the turn whatever its length", "[machine][wait]")
{
    World w; // clock = actions
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        CHECK(w.Wait(id, 2.0f) == Verdict::Block);
    };
    EndTheHerosTurn(w);
    CHECK(pokes == 1);
    CHECK(w.Current() == kCompanion);
    CHECK(Logged(w, "Genlock's turn ends: waits"));
    // Nothing is said of time where a turn has none.
    CHECK_FALSE(Logged(w, "Genlock waits"));
    CHECK_FALSE(Logged(w, "Genlock's wait"));
}
