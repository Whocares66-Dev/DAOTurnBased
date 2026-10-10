// A creature at a command its turn did not order, and an action's time that
// went to its walk (issue 88). Three turns their AI played stood out the whole
// limit on a turn's one action, twelve seconds of the wall at three times the
// speed, in the Blackmarsh's Fade (2026-10-09):
//
//   - a Desire Demon that summons, at a `play animation` its script had given
//     it: `round 1: Desire Demon's turn (enemy, AI), a bar of 4.00 s`, and
//     twelve seconds on `still at a play animation after 36000 ms: stopped`,
//     five times in twenty minutes. The game's AI chooses nothing for a
//     creature that is at a command (ai_main_h_2.nss, AI_DetermineCombatRound:
//     "Do not run any AI if the creature is doing something right now"), the
//     command was no effect's, so no hold, and nothing but the limit bounded
//     it;
//   - Velanna in a bear's shape, creeping round a target whose ring was full:
//     `Velanna's walk runs past its bar: its attack's 2.11 s goes to the walk,
//     and is taken when it gets there`, and 39 ms on `Velanna is at its
//     attack: its 2.11 s taken now, 0.08 s of it owed to its next turn`, the
//     pair six times over in three and a half seconds, the attack's seconds
//     2.11, 2.02, 1.91, 1.75, 1.45, 1.30 and 1.19, then the limit; and her
//     next turn began `0.92 s of it paid for its pending attack`, for a blow
//     that was never struck;
//   - Oghren, whose talent the engine failed twice for want of a path, each
//     given back, and whose script's third order of it, a millisecond after
//     the second failure, was let through and stood to the limit.
//
// Scripted cases in the stand-in for the game (World.h). What is promised:
//
//   - a creature found at a command of the engine's or the story's (what the
//     gate lets through unjudged: core/Rules.h, CommandKind::Involuntary) with
//     nothing of its own let through this turn is held by it, as by an
//     effect's: the time it holds is taken of its bar as it runs, its AI is
//     not told, nothing of it is cleared, and its turn is lost when the hold
//     outlasts the bar, in the bar's time;
//   - let go within the bar, it has the rest of its turn;
//   - what the engine has it at after a command of its own (a death blow
//     after its attack) is its action's, and no hold;
//   - an action's time that went to its walk is taken when the engine ends
//     its command as done, once: a creature whose feet stop and start on the
//     way has not got there, the walk takes that time once, and what never
//     struck leaves nothing owed;
//   - an AI two of whose actions were given back has nothing it can get to,
//     and what its script orders in answer to the second failure is refused:
//     its turn ends as it settles.
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

// 4 s turns, a pace of 5 m/s for round numbers, a meter free.
Settings Timed()
{
    Settings s = Fixed();
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

auto Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
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

// The area's script gives a creature its ritual: an animation that loops,
// which the gate does not judge and nothing of the mod's ends.
void AtItsRitual(World &w, ObjectId id)
{
    REQUIRE(w.Order(id, CommandType::PlayAnimation, Source::Script) == Verdict::Allow);
    REQUIRE(w.Get(id).current == CommandType::PlayAnimation);
}

// The hero's turn ended, and the frames until the next creature's turn has
// begun.
void EndTheHerosTurn(World &w)
{
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 100 && w.Current() == kHero; ++i)
    {
        w.Frame();
    }
}

// How long a creature's turn goes on from here, a minute at the most.
std::uint32_t TurnLasts(World &w, ObjectId id)
{
    std::uint32_t ms = 0;
    for (; ms < 60000 && w.Current() == id; ms += 16)
    {
        w.Frame();
    }
    return ms;
}

// A creature's feet, on its way to its target with others in the way: a step
// of half a meter and a frame standing, so many steps of it.
void Creeps(World &w, ObjectId id, int steps)
{
    for (int step = 0; step < steps && w.Current() == id && w.Get(id).busy; ++step)
    {
        w.Get(id).moving = true;
        w.Get(id).position.x += 0.5f;
        w.Frame();
        w.Get(id).moving = false;
        w.Frame();
    }
}

std::uint32_t Owes(const World &w, ObjectId id)
{
    const auto charging = w.machine.State().charging;
    const auto debt = std::ranges::find(charging, id, &ChargeView::id);
    return debt != charging.end() ? debt->owedMs : 0;
}

} // namespace

TEST_CASE("a creature at an animation its script gave it, as its turn comes: held by it, and its turn lost in its "
          "bar's time",
          "[notordered][time]")
{
    // The Fade's summoning demons (2026-10-09): `round 1: Desire Demon's turn
    // (enemy, AI), a bar of 4.00 s: 4.00 s and 18.2 m left`, nothing for
    // twelve seconds of the wall, `still at a play animation after 36000 ms:
    // stopped`, `Desire Demon's turn ends: took too long`.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    int told = 0;
    w.onPoke = [&](ObjectId id) { told += id == kGenlock ? 1 : 0; };
    w.StartFight();
    AtItsRitual(w, kGenlock);
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    const std::uint32_t lasted = TurnLasts(w, kGenlock);
    // Its bar's four seconds, and not max_action_ms' twelve.
    CHECK(lasted >= 3900);
    CHECK(lasted <= 4300);
    CHECK(ends == std::vector<std::string>{"held by an effect"});
    CHECK(Lines(w, "took too long") == 0);
    CHECK(Lines(w, "still at a") == 0);
    // Said by what holds it, with what the engine times of it.
    CHECK(Lines(w, "Genlock is held by a play animation it did not order, how long the engine does not say ahead: "
                   "the time it holds is taken of its bar as it runs") == 1);
    CHECK(Lines(w, "its step is one the engine does not time") == 1);
    CHECK(Lines(w, "Genlock is held by a play animation it did not order past its bar (4.0") == 1);
    // Its AI was not told: the game's own chooses nothing for a creature at a
    // command. And the animation is the engine's to end: nothing is cleared.
    CHECK(told == 0);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Get(kGenlock).current == CommandType::PlayAnimation);
    // Its own clock moved a turn's seconds with the hold, as with any: what
    // its script times on it (the ritual's end) comes due by its turns.
    CHECK(w.agedOwn[kGenlock] >= 3900);
}

TEST_CASE("found at an animation only after its AI was told its turn had come, a creature is held all the same",
          "[notordered][time]")
{
    // Its script gives it the animation in answer to being told, as a
    // creature's own script may before the game's AI is run.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            AtItsRitual(w, id);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    const std::uint32_t lasted = TurnLasts(w, kGenlock);
    CHECK(lasted <= 4300);
    CHECK(ends == std::vector<std::string>{"held by an effect"});
    CHECK(Lines(w, "took too long") == 0);
}

TEST_CASE("an animation that ends within the bar lets its creature go, with the rest of its turn", "[notordered][time]")
{
    World w(Timed());
    int told = 0;
    w.onPoke = [&](ObjectId id) { told += id == kGenlock ? 1 : 0; };
    w.StartFight();
    AtItsRitual(w, kGenlock);
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(1000);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.State().heldByEffect);
    // Meanwhile its script's attack is refused, as one held by an effect's
    // is: it would wait behind the animation, and run in another's turn.
    CHECK(w.Attack(kGenlock, kHero, Source::Script) == Verdict::Block);
    CHECK(told == 0);
    // The animation runs out, by the engine's own doing.
    w.Finish(kGenlock);
    w.RunFor(100);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.machine.State().heldByEffect);
    // The second it stood there was its turn's, and its AI is told now.
    CHECK(Lines(w, "Genlock is free of the play animation it did not order after ") == 1);
    CHECK(w.machine.View().timeLeftMs <= 3100);
    CHECK(w.machine.View().timeLeftMs >= 2900);
    CHECK(told == 1);
    CHECK(w.Attack(kGenlock, kHero, Source::Script) == Verdict::Allow);
}

TEST_CASE("the player's character at an animation the story gave it is held by it, and has its turn back after",
          "[notordered][time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    AtItsRitual(w, kHero);
    w.RunFor(1000);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.State().heldByEffect);
    // An order now would wait behind it: refused, with why.
    CHECK(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Held"));
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs <= 3100);
    CHECK(w.machine.View().timeLeftMs >= 2900);
    CHECK(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
}

TEST_CASE("what the engine has a creature at after a command of its own is its action's, and no hold",
          "[notordered][time]")
{
    // A death blow after its attack (issue 28's ogre, struck on a turn
    // Alistair's tactics played): the engine's command, and the turn's own.
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && !w.Get(id).busy)
        {
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, 0.0f, 1.5f) == Verdict::Allow);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(200);
    // Its blow kills, and the engine goes on to the death blow with no frame
    // between.
    REQUIRE(w.Order(kGenlock, CommandType::Deathblow, Source::Engine) == Verdict::Allow);
    w.RunFor(3000);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(Lines(w, "is held by") == 0);
    // Nothing of its bar is taken for it beyond the attack's own time.
    CHECK(w.machine.View().timeLeftMs == 2500);
}

TEST_CASE("an action's time that went to its walk is taken once, as the engine ends its command as done, however its "
          "creature's feet stop and start on the way",
          "[notordered][time]")
{
    // Velanna's turn (2026-10-09): the pair `walk runs past its bar` and `is
    // at its attack: ... taken now` six times over in three and a half seconds.
    // A frame without a step was taken for "it got there".
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && !w.Get(id).busy)
        {
            // A swing of 2 s, its walk not known to the gate: charged as it
            // is let through, with 2 s and 11 m left.
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, std::nullopt, 2.0f) == Verdict::Allow);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(200);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    // It creeps 16 m: 11.5 of them bring it past its bar, where the swing's
    // 2 s go to the walk, and the rest are walked on that time.
    Creeps(w, kGenlock, 32);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.Get(kGenlock).busy);
    CHECK(Lines(w, "Genlock's walk runs past its bar") == 1);
    CHECK(Lines(w, " s taken now") == 0);
    CHECK(Owes(w, kGenlock) == 0);
    CHECK(w.machine.View().timeLeftMs >= 900);
    CHECK(w.machine.View().timeLeftMs <= 1100);
    // It strikes: the engine ends its command as done, and the swing's time
    // is taken, a second of it owed to its next turn.
    w.Finish(kGenlock);
    CHECK(Lines(w, "Genlock's attack is done: its 2.00 s taken now, 1.") == 1);
    CHECK(w.machine.View().timeLeftMs == 0);
    CHECK(Owes(w, kGenlock) >= 900);
    CHECK(Owes(w, kGenlock) <= 1100);
}

TEST_CASE("another command of its creature's that the engine ends as done on the way is not its action's: nothing is "
          "taken for it",
          "[notordered][time]")
{
    // The engine draws its weapons before an attack (`sheathe or draw (free,
    // from engine)`), a command of its own that is done before the walk.
    World w(Timed());
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && !w.Get(id).busy)
        {
            // 30 m to walk, more than its bar holds: it closes in, and its
            // swing's time is taken if it gets there and strikes.
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, 30.0f, 2.0f) == Verdict::Allow);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(100);
    REQUIRE(w.machine.View().timeLeftMs == 4000);
    w.machine.OnCommandDone(kGenlock, CommandType::SheatheWeapons);
    CHECK(Lines(w, " s taken now") == 0);
    CHECK(w.machine.View().timeLeftMs == 4000);
    // Nor another creature's attack.
    w.machine.OnCommandDone(kHurlock, CommandType::Attack);
    CHECK(Lines(w, " s taken now") == 0);
    // Its own, done: taken.
    w.Finish(kGenlock);
    CHECK(Lines(w, "Genlock's attack is done: its 2.00 s taken now; 2.00 s") == 1);
}

TEST_CASE("on a turn the player plays too, an action's time that went to its walk is taken as its command is done",
          "[notordered][time]")
{
    World w(Timed());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A swing of 3 s at one whose path the gate could not tell: a second is
    // left, 6 m with the free one, and she walks eight.
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, std::nullopt, 3.0f) == Verdict::Allow);
    w.Frame();
    Creeps(w, kHero, 16);
    REQUIRE(Lines(w, "Hero's walk runs past its bar") == 1);
    CHECK(Lines(w, " s taken now") == 0);
    CHECK(w.machine.View().timeLeftMs > 2000);
    w.Finish(kHero);
    CHECK(Lines(w, "Hero's attack is done: its 3.00 s taken now, 0.") == 1);
    CHECK(w.machine.View().timeLeftMs == 0);
    CHECK(Owes(w, kHero) > 0);
}

TEST_CASE("an action kept for its turn is not given to a creature at an animation its script gave it: it waits, as "
          "behind an effect's command",
          "[notordered][time]")
{
    World w(Timed());
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && ++told == 1)
        {
            // 4.5 s of swing on a bar of 4 s, its target in reach: kept.
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, 0.0f, 4.5f) == Verdict::Hold);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    w.RunFor(100);
    REQUIRE(told == 1);
    // Round the table to its next turn, the ritual given it on the way.
    for (int i = 0; i < 4000 && (w.Current() != kHero || w.machine.GetPhase() != Phase::AwaitingOrder); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kHero);
    AtItsRitual(w, kGenlock);
    w.released.clear();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    const std::uint32_t lasted = TurnLasts(w, kGenlock);
    CHECK(Lines(w, "Genlock's pending attack waits: a play animation it did not order holds its creature") == 1);
    CHECK(w.released.empty());
    CHECK(lasted <= 4300);
    CHECK(Lines(w, "took too long") == 0);
}

TEST_CASE("a creature that creeps on its action's time until its bar is out never struck, and owes nothing for it",
          "[notordered][time]")
{
    // Velanna's next turn began `0.92 s of it paid for its pending attack`:
    // what each of the six "arrivals" had not found room for in her bar,
    // owed for a blow that was never struck.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && !w.Get(id).busy)
        {
            CHECK(w.Attack(id, kHero, Source::Script, NoAttack::None, std::nullopt, 2.0f) == Verdict::Allow);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(200);
    Creeps(w, kGenlock, 80); // forty meters: 21 m is all its bar walks
    w.RunFor(100);
    CHECK(w.Current() != kGenlock);
    CHECK(ends == std::vector<std::string>{"out of movement"});
    CHECK(Lines(w, "Genlock's walk runs past its bar") == 1);
    CHECK(Lines(w, " s taken now") == 0);
    CHECK(Owes(w, kGenlock) == 0);
}

TEST_CASE("an AI two of whose actions the engine failed is let no third, though its script orders it in the "
          "failure's own event: its turn ends, nothing it can get to",
          "[notordered][time]")
{
    // Oghren in the Fade's crypt (2026-10-09): `use ability (action, from
    // script); 1.79 s and 9.3 m left`, `failed (COMMAND_FAILED_INVALID_PATH):
    // its action is given back`, `use ability ...; 0.07 s and 0.3 m left`,
    // failed and given back 1.07 s on, a third `use ability ...; 0.07 s and
    // 0.3 m left` a millisecond after, and twelve seconds on `still at a use
    // ability after 36000 ms: stopped`. The turn was over at the second
    // give-back (StepSettling), and no step came between that and the third
    // order to say so.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, kGenlock, ends);
    const auto swing = [&] { return w.Attack(kGenlock, kHero, Source::Script, NoAttack::None, std::nullopt, 2.0f); };
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && !w.Get(id).busy)
        {
            CHECK(swing() == Verdict::Allow);
        }
    };
    w.StartFight();
    EndTheHerosTurn(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(200);
    // No path: given back, and its script orders the same at once, as the
    // game's does. Once more is its to try.
    REQUIRE(w.Fail(kGenlock, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(swing() == Verdict::Allow);
    w.RunFor(200);
    // And again: given back twice, it has nothing it can get to, and the
    // third is refused there and then, with whatever else its script orders.
    REQUIRE(w.Fail(kGenlock, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(swing() == Verdict::Block);
    CHECK(w.Order(kGenlock, CommandType::MoveToObject, Source::Script) == Verdict::Block);
    const std::uint32_t lasted = TurnLasts(w, kGenlock);
    CHECK(lasted <= 700);
    CHECK(ends == std::vector<std::string>{"nothing it can get to"});
    CHECK(Lines(w, "took too long") == 0);
}
