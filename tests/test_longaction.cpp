// The time the engine has an action at a timed step that is advancing is not
// counted by the machine's limits (issue 28; core/TimedStep.h has what was
// measured: an ogre's death blow, a step of 12.5 s by the engine's own count,
// stopped half a second before its end and its striker's turn lost; and,
// with the first build of this, stopped a frame after that step ended, at
// the command's tail). The limits are for actions that stand still. What is
// promised:
//
//   - the rule itself: between two looks a step advanced if it had time left
//     at the first and its elapsed time is another at the second; one that
//     stands, one that is spent and one the engine does not time did not;
//   - on a turn its tactics play, an action whose timed step outlasts
//     max_action_ms plays out, and so does the tail its command has after
//     that step: nothing is stopped, the turn goes on and keeps its rollover.
//     The same of max_turn_ms, for a turn made long by such a step;
//   - the time an action stands is counted as ever: a timed step that
//     stands, a tail that never ends, a step that stops advancing, each is
//     stopped when it has stood the limit;
//   - what is not counted is each action's own, and bounded: a step renewed
//     as it runs is stopped at five times the limit, and an AI's turn in one
//     ended at five times its;
//   - an effect's command that holds a creature is not the rule's: a hold's
//     bound is the bar;
//   - on a turn the player plays, which no limit ends, what is said of an
//     action past the limit by the clock is the truth of it.
#include "World.h"

#include "core/TimedStep.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

int Cleared(const World &w, ObjectId target)
{
    return static_cast<int>(std::ranges::count_if(
        w.applied, [&](const Effect &e) { return e.kind == EffectKind::ClearCommands && e.target == target; }));
}

bool InATurn(const World &w)
{
    const Phase phase = w.machine.GetPhase();
    return phase == Phase::AwaitingOrder || phase == Phase::Dispatching || phase == Phase::AiThinking ||
           phase == Phase::Executing || phase == Phase::Settling;
}

// Why each turn ended, by whose it was.
struct Ends
{
    explicit Ends(World &w)
    {
        w.machine.SetEventSink([this](const TurnEvent &e) {
            if (e.kind == EventKind::TurnEnded)
            {
                of[e.who].push_back(e.detail);
            }
        });
    }
    std::unordered_map<ObjectId, std::vector<std::string>> of;
};

// The genlock's AI, told its turn has come: each time it does the next thing
// the case has it do, and when those are done it passes (a wait of no
// length). Everyone else the AI plays passes.
struct Tellings
{
    Tellings(World &w, std::vector<std::function<void()>> does) : does_(std::move(does))
    {
        w.onPoke = [this, &w](ObjectId id) {
            if (id == kGenlock && static_cast<std::size_t>(genlock++) < does_.size())
            {
                does_[static_cast<std::size_t>(genlock) - 1]();
                return;
            }
            (void)w.Wait(id, 0.0f);
        };
    }
    int genlock = 0;

  private:
    std::vector<std::function<void()>> does_;
};

// The turns go by, the player's ended as they come, until it is the
// genlock's and being played.
void UntilTheGenlocksTurn(World &w)
{
    for (int frames = 0; frames < 4000 && !(w.Current() == kGenlock && InATurn(w)); ++frames)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(InATurn(w));
}

// A blow of a second of the bar at something in reach, its command at a step
// the engine times (0: at none).
void Blow(World &w, ObjectId id, std::int32_t stepMs)
{
    const ObjectId at = id == kHero ? kGenlock : kHero;
    REQUIRE(w.Attack(id, at, id == kHero ? Source::Player : Source::Script, NoAttack::None, 0.0f, 1.0f) ==
            Verdict::Allow);
    w.TimedStep(id, stepMs);
}

// The line of the genlock's turn's end, as the log has it.
std::string TurnsEnd(const World &w)
{
    const auto found = std::ranges::find_if(
        w.log, [](const std::string &line) { return line.find("Genlock's turn ends: ") != std::string::npos; });
    return found != w.log.end() ? *found : std::string{};
}

constexpr const char *kNotCounted = "which the limit does not count (issue 28)";

} // namespace

TEST_CASE("between two looks a step advanced if it had time left and its elapsed time moved", "[longaction]")
{
    CHECK_FALSE(HasTimeLeft(StepTime{}));
    CHECK_FALSE(HasTimeLeft(StepTime{500, 0})); // not timed
    CHECK(HasTimeLeft(StepTime{0, 12500}));
    CHECK(HasTimeLeft(StepTime{12499, 12500}));
    CHECK_FALSE(HasTimeLeft(StepTime{12500, 12500})); // at its end
    CHECK_FALSE(HasTimeLeft(StepTime{13000, 12500})); // spent, and still there

    StepWatch watch;
    // Seen for the first time: nothing before it to have advanced from.
    CHECK(watch.Look(StepTime{4000, 12500}, 10000) == 0);
    CHECK(watch.Last().elapsedMs == 4000);
    CHECK(watch.Last().durationMs == 12500);
    // It climbs: all the watcher's time since the look before.
    CHECK(watch.Look(StepTime{5000, 12500}, 11000) == 1000);
    // However little, and however long the look before was ago: the engine
    // paces it, not the watcher.
    CHECK(watch.Look(StepTime{5001, 12500}, 13500) == 2500);
    // It stands: none of it.
    CHECK(watch.Look(StepTime{5001, 12500}, 14000) == 0);
    // Its length pushed ahead under an elapsed time that stands is a step
    // standing.
    CHECK(watch.Look(StepTime{5001, 20000}, 15000) == 0);
    // It moves again.
    CHECK(watch.Look(StepTime{5017, 20000}, 15016) == 16);
    // Put back to its start, as a looping animation is: a move.
    CHECK(watch.Look(StepTime{0, 20000}, 15032) == 16);
    // To its end: it advanced until then.
    CHECK(watch.Look(StepTime{20000, 20000}, 15048) == 16);
    // At its end it has no time left, and nothing after advances from it,
    // whatever its count then does.
    CHECK(watch.Look(StepTime{20016, 20000}, 15064) == 0);
    // Nor from a step the engine does not time.
    CHECK(watch.Look(StepTime{700, 0}, 15080) == 0);
    CHECK(watch.Look(StepTime{716, 0}, 15096) == 0);
    // A step begun after it advances from its second look on.
    CHECK(watch.Look(StepTime{0, 3000}, 15112) == 0);
    CHECK(watch.Look(StepTime{16, 3000}, 15128) == 16);
}

TEST_CASE("an AI's action whose timed step outlasts max_action_ms plays out, and its turn goes on", "[longaction]")
{
    // The ogre's death blow, in small: a step of eight seconds under a limit
    // of four.
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] { Blow(w, kGenlock, 8000); }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(3900);
    CHECK(Lines(w, kNotCounted) == 0); // nothing said before the limit's time
    w.RunFor(500);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).busy);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, "Genlock is still at a attack after 4000 ms: ") == 1);
    CHECK(Lines(w, " ms of that at the engine's own step of 8.00 s while it advanced (4.0 s of it done), which the "
                   "limit does not count (issue 28)") == 1);
    // Said once of the action, however long it then runs.
    w.RunFor(3000);
    CHECK(Lines(w, kNotCounted) == 1);
    CHECK(w.Get(kGenlock).busy);
    // It ends by itself at its length, and the turn goes on as after any
    // action: its AI told again, with what the blow left of its bar.
    w.RunFor(1500);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, "stopped") == 0);
    CHECK(told.genlock == 2);
    w.RunFor(2000);
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.of[kGenlock].size() == 1);
    CHECK(ends.of[kGenlock].front() != "took too long");
}

// What play showed of the rule's first form (2026-10-06 01:45, Alistair on a
// turn his tactics played): `Alistair is still at a death blow after 36000
// ms, the engine's own step of 12.50 s still running (12.1 s of it done):
// left to end`, then `death blow ... is at sub-action 0`, and with the next
// step `still at a death blow after 36000 ms: stopped`, `turn ends: took too
// long; nothing rolls over`. The command has a tail after its timed step
// (2.7 s on Lyna's own turn in the same run), and the limit, only deferred
// while the step ran, was judged a frame into it with its count long spent.
TEST_CASE("the tail a command has after its timed step plays out too: the step's time was never counted",
          "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 6000);
                      w.Get(kGenlock).stepTailMs = 3000;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    // The step has ended and the command is at its tail, nine seconds in all
    // under a limit of four.
    w.RunFor(6500);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).busy);
    CHECK(w.Get(kGenlock).atTail);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(2300); // 8.8 s
    CHECK(w.Get(kGenlock).busy);
    CHECK(Cleared(w, kGenlock) == 0);
    // It ends by itself; the turn goes on, its AI told again, and when it
    // passes the turn is its own to have ended: what it left rolls over.
    w.RunFor(3000);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, "stopped") == 0);
    CHECK(told.genlock == 2);
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.of[kGenlock].size() == 1);
    CHECK(ends.of[kGenlock].front() != "took too long");
    const std::string end = TurnsEnd(w);
    CHECK(end.find(" rolls over") != std::string::npos);
    CHECK(end.find("nothing rolls over") == std::string::npos);
}

TEST_CASE("a tail that never ends is stopped when it has stood the limit, counted from the timed step's end",
          "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 6000);
                      w.Get(kGenlock).stepTailMs = 600000;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(9800); // 6 s of the step, 3.8 of the tail
    REQUIRE(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(500);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "still at a attack after 4000 ms: stopped") == 1);
    CHECK(Cleared(w, kGenlock) == 1);
}

TEST_CASE("a timed step that stands is stopped at the limit as any action is", "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 8000);
                      w.TimedStep(kGenlock, 8000, 1000);
                      w.Get(kGenlock).stepStands = true;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(3900);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(300);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "still at a attack after 4000 ms: stopped") == 1);
    CHECK(Lines(w, kNotCounted) == 0);
    CHECK(Cleared(w, kGenlock) == 1);
}

TEST_CASE("a step that advanced and then stands is stopped when it has stood the limit", "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] { Blow(w, kGenlock, 20000); }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(5000);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Lines(w, kNotCounted) == 1);
    // The engine stops its clock.
    w.Get(kGenlock).stepStands = true;
    w.RunFor(3900);
    CHECK(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(300);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "still at a attack after 4000 ms: stopped") == 1);
    CHECK(Cleared(w, kGenlock) == 1);
}

TEST_CASE("what is not counted is each action's own: the next one that stands is stopped at its own limit",
          "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    World w(s);
    Ends ends(w);
    // A blow of six seconds by the engine's count, which plays out; then one
    // the engine never ends, at no timed step.
    Tellings told(w, {[&w] { Blow(w, kGenlock, 6000); }, [&w] { Blow(w, kGenlock, 0); }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    for (int frames = 0; frames < 1000 && told.genlock < 2; ++frames)
    {
        w.Frame();
    }
    REQUIRE(told.genlock == 2);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(3900);
    CHECK(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(300);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Cleared(w, kGenlock) == 1);
}

TEST_CASE("a step that is renewed as it runs is not counted for four times the limit, and stopped at five",
          "[longaction]")
{
    // A looping animation: at its end the engine pushes its length ahead, and
    // it advances for ever.
    Settings s = Fixed();
    s.maxActionMs = 4000;
    s.maxTurnMs = 60000;
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 3000);
                      w.Get(kGenlock).stepRenews = 3000;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(19800);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).busy);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, kNotCounted) == 1);
    w.RunFor(400);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "still at a attack after 4000 ms: stopped") == 1);
    CHECK(Cleared(w, kGenlock) == 1);
    CHECK_FALSE(w.Get(kGenlock).busy);
}

TEST_CASE("an AI's turn made long by a timed step and its tail is not ended at max_turn_ms: it goes on, and keeps "
          "its rollover",
          "[longaction]")
{
    Settings s = Fixed();
    s.maxTurnMs = 6000; // and max_action_ms twelve seconds
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 7000);
                      w.Get(kGenlock).stepTailMs = 2000;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(5900);
    CHECK(Lines(w, kNotCounted) == 0);
    w.RunFor(500);
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).busy);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, "Genlock's turn is still going after 6000 ms: ") == 1);
    CHECK(Lines(w, " ms of that at a step of the engine's own timing while it advanced, which the limit does not "
                   "count (issue 28)") == 1);
    w.RunFor(2400); // 8.8 s: the step ended, the tail still playing
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).busy);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(Lines(w, kNotCounted) == 1);
    // The command ends by itself, nine seconds into a turn whose limit is
    // six: two of them counted. Its AI is told again, and passes.
    w.RunFor(3000);
    CHECK(Cleared(w, kGenlock) == 0);
    CHECK(told.genlock == 2);
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.of[kGenlock].size() == 1);
    CHECK(ends.of[kGenlock].front() != "took too long");
    CHECK(Lines(w, "Genlock's turn is still going after 6000 ms, ") == 0);
    const std::string end = TurnsEnd(w);
    CHECK(end.find(" rolls over") != std::string::npos);
    CHECK(end.find("nothing rolls over") == std::string::npos);
}

TEST_CASE("an AI's turn in an action whose step is renewed for ever is ended at five times max_turn_ms", "[longaction]")
{
    Settings s = Fixed();
    s.maxTurnMs = 6000;
    s.maxActionMs = 60000; // the action's own bound far off: the turn's is the one met
    World w(s);
    Ends ends(w);
    Tellings told(w, {[&w] {
                      Blow(w, kGenlock, 3000);
                      w.Get(kGenlock).stepRenews = 3000;
                  }});
    w.StartFight();
    UntilTheGenlocksTurn(w);
    w.applied.clear();
    w.RunFor(29700);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Cleared(w, kGenlock) == 0);
    w.RunFor(500);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "Genlock's turn is still going after 6000 ms, 1 of its commands let through: ended") == 1);
    CHECK(Cleared(w, kGenlock) == 1);
    CHECK_FALSE(w.Get(kGenlock).busy);
}

TEST_CASE("an effect's command that holds a creature is counted whatever its timed step does: a hold is not the "
          "rule's",
          "[longaction]")
{
    // A knockback's command has a timed step too (sub-action 51, in play
    // `2700 of 6233 ms`). What bounds a hold is the bar, and the turn's limit
    // where that comes first: as before.
    Settings s = Fixed();
    s.maxTurnMs = 3500; // under the bar's four seconds
    World w(s);
    Ends ends(w);
    Tellings told(w, {});
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.TimedStep(kGenlock, 20000);
    UntilTheGenlocksTurn(w);
    w.RunFor(3400);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(300);
    CHECK(w.Current() != kGenlock);
    CHECK(ends.of[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, kNotCounted) == 0);
    CHECK(told.genlock == 0);
}

TEST_CASE("on a turn the player plays, an action past the limit by the clock is said for what it is", "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    SECTION("at a timed step that advances: not counted, and it ends by itself")
    {
        World w(s);
        Ends ends(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        w.applied.clear();
        Blow(w, kHero, 8000);
        w.RunFor(4400);
        CHECK(Lines(w, "Hero is still at a attack after 4000 ms: ") == 1);
        CHECK(Lines(w, kNotCounted) == 1);
        CHECK(Lines(w, "its turn is the player's, and is not ended for it") == 0);
        w.RunFor(4400);
        CHECK(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Cleared(w, kHero) == 0);
        CHECK(ends.of[kHero].empty());
        CHECK(Lines(w, "its turn is the player's, and is not ended for it") == 0);
    }
    SECTION("at a timed step that stands: the turn waits on the player, who is told End stops it")
    {
        World w(s);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        Blow(w, kHero, 8000);
        w.Get(kHero).stepStands = true;
        w.RunFor(4400);
        CHECK(Lines(w, kNotCounted) == 0);
        CHECK(Lines(w, "Hero is still at a attack after 4000 ms: its turn is the player's, and is not ended for it "
                       "(End stops it)") == 1);
        CHECK(w.Current() == kHero);
    }
}

TEST_CASE("that a limit's time went by uncounted is said of each action it did for, once", "[longaction]")
{
    Settings s = Fixed();
    s.maxActionMs = 4000;
    s.secondsPerTurn = 10.0f; // room for two blows of a second in one turn
    World w(s);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (int blow = 1; blow <= 2; ++blow)
    {
        Blow(w, kHero, 6000);
        w.RunFor(7000);
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Lines(w, kNotCounted) == blow);
    }
}
