// An attack the engine itself has no way to make (issue 27): refused before
// a step by what the engine answered when the game layer asked it (no room
// on the target's melee ring, no path to where its approach goes, no line to
// be had for a shot). The player is told why; an AI's is given back and its
// AI told to choose again. And every attack is asked afresh: nothing is
// remembered of a no, nor of a failure under way. Scripted fights in the
// stand-in for the game (World.h), whose Attack says what the engine
// answered and whose Fail ends a command as the engine fails one.
//
// A memory of targets "ruled out for the rest of the turn" was built with
// this, tried in play and withdrawn (2026-10-04): the game's own AI answers
// a failure at the blow with a move closer and the same target again, and in
// 7 turns of 17 read in earlier logs that second attack went through and
// landed. The cases below from "an attack failed at the blow" on hold what
// replaced it: the engine is asked again, and what ends a turn that keeps
// failing is the count of actions given back, a wait's own time, and where
// the turn never settles for those to be looked at, the limit on a turn an
// AI plays (max_turn_ms; the cap on a turn's commands, which ended that one
// before, is gone: issue 38). Those failures were attacks
// the gate's question had called good; DAOLib has since found why and the
// gate asks the whole of it (Approach::findsRoom), which is the game
// layer's and comes here as the same "no place". What still fails under way
// is what nothing foresees: a walker stuck against bodies, no path after
// all.
#include "World.h"

#include "core/Events.h"
#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

constexpr const char *kNoSpace = "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING";
constexpr const char *kNoPath = "COMMAND_FAILED_INVALID_PATH";

// A turn a bar of time, as test_time.cpp's: 4 s turns, 5 m/s, a meter free.
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

auto Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// The hero passes, and the genlock's turn has begun: its AI has been told,
// and has answered (a turn of two refusals is over three frames on).
void ToGenlock(World &w)
{
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    w.Frame();
}

// Why each of the genlock's turns ended.
void KeepEnds(World &w, std::vector<std::string> &ends)
{
    w.machine.SetEventSink([&ends](const TurnEvent &e) {
        if (e.kind == EventKind::TurnEnded && e.who == kGenlock)
        {
            ends.push_back(e.detail);
        }
    });
}

// A swing of a second at someone in reach, as the genlock's own script
// queues one.
Verdict Swing(World &w, ObjectId target, NoAttack none = NoAttack::None)
{
    return w.Attack(kGenlock, target, Source::Script, none, 0.0f, 1.0f);
}

// On to a creature's next turn, begun: the player's turns ended as they
// come, an AI's as its script has it.
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
    for (int i = 0;
         i < 100 && (w.machine.GetPhase() == Phase::TurnStart || w.machine.GetPhase() == Phase::RoundPadding); ++i)
    {
        w.Frame();
    }
}

} // namespace

TEST_CASE("the player's attack on a target with no place left at it is refused before a step, and told why",
          "[noattack]")
{
    // The caverns above Haven: dragonlings round one party member, every
    // place on the ring taken. Let through "its walk not known", the attack
    // was failed by the engine within 2 ms and given back.
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    const TurnView before = w.machine.View();
    CHECK(w.Attack(kHero, kGenlock, Source::Player, NoAttack::NoPlace) == Verdict::Block);
    CHECK_FALSE(w.Get(kHero).busy);
    w.Frame();
    // Over the character, in grey, as "Too far" is.
    const auto floaty = std::ranges::find_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == L"No room at the target"; });
    REQUIRE(floaty != w.applied.end());
    CHECK(floaty->target == kHero);
    CHECK(floaty->colour == kGrey);
    // On the bar's caption, as an action given back is; refused, not
    // given back: nothing was taken.
    CHECK(w.machine.View().notice == L"No room at the target: Hero's attack is refused");
    // And for a tool that drives the game: the refusal's event, the state.
    REQUIRE(!events.empty());
    CHECK(events.back().kind == EventKind::Refused);
    CHECK(events.back().who == kHero);
    CHECK(events.back().detail == "No room at the target");
    CHECK(events.back().command == "attack");
    CHECK(events.back().note == "the engine has no place at its target for it");
    CHECK(w.machine.State().lastRefusal ==
          "No room at the target (attack: the engine has no place at its target for it)");
    CHECK(Logged(w, "Hero: attack on Genlock refused, the engine has no place at its target for it"));
    // Nothing spent, and the turn still waits.
    const TurnView after = w.machine.View();
    CHECK(after.timeLeftMs == before.timeLeftMs);
    CHECK(after.movementLeft == before.movementLeft);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::ActionGivenBack; }));
    // A place free again, the same order is taken: each order is put to
    // the engine as it comes. The caption's line goes with it.
    CHECK(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.machine.View().notice.empty());
}

TEST_CASE("the player's attack with no path to its target is refused as no way through, a shot with no line to be "
          "had as no line of sight",
          "[noattack]")
{
    // The forest road: a party member stood where the pathfinder found no
    // way to anyone. His attacks went through on a walk reckoned by the
    // straight line and failed 0.4 s on, 15 times; 4 were refused "Too far"
    // by that walk.
    struct Case
    {
        NoAttack none;
        const wchar_t *words;
        const char *note;
    };
    for (const Case &c : {Case{NoAttack::NoPath, L"No way through", "the engine finds no path to its target"},
                          Case{NoAttack::NoLine, L"No line of sight",
                               "the engine finds no line of sight to its target on the way to it"}})
    {
        World w(Timed());
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        // With a walk reckoned for it too, far beyond the turn's: the
        // engine's answer is the reason said, not the reckoning.
        CommandRequest attack;
        attack.object = kHero;
        attack.type = CommandType::Attack;
        attack.source = Source::Player;
        attack.inCombat = true;
        attack.walk = 30.0f;
        attack.actionSeconds = 1.0f;
        attack.target = kGenlock;
        attack.noAttack = c.none;
        CHECK(w.Queue(attack) == Verdict::Block);
        w.Frame();
        CHECK(w.Said(c.words));
        CHECK_FALSE(w.Said(L"Too far"));
        CHECK(w.machine.View().notice == std::wstring{c.words} + L": Hero's attack is refused");
        REQUIRE(!events.empty());
        CHECK(events.back().kind == EventKind::Refused);
        CHECK(events.back().note == c.note);
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    }
}

TEST_CASE("an AI's attack the engine has no way to make is given back before a step, and it chooses again at once",
          "[noattack]")
{
    // Seen in play, 2026-10-04 (twelve dragonlings staged round one party
    // member): three attacks given back so, each followed by "chooses
    // again", with nothing let through and no failure of the engine's.
    for (const NoAttack none : {NoAttack::NoPlace, NoAttack::NoPath, NoAttack::NoLine})
    {
        World w(Timed());
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        std::vector<Verdict> answers;
        w.onPoke = [&](ObjectId) {
            // Its first choice is the hero, whom the engine has no
            // attack for; told again, Alistair, in reach.
            answers.push_back(answers.empty() ? Swing(w, kHero, none) : Swing(w, kCompanion));
        };
        ToGenlock(w);
        // Told again within a few frames, not when its thinking time
        // (ai_think_ms, a second and a half) has run out.
        w.RunFor(100);
        REQUIRE(answers.size() == 2);
        CHECK(answers[0] == Verdict::Block);
        CHECK(answers[1] == Verdict::Allow);
        CHECK(w.Current() == kGenlock);
        CHECK(w.Get(kGenlock).current == CommandType::Attack);
        const std::string why = none == NoAttack::NoPlace  ? "the engine has no place at its target for it"
                                : none == NoAttack::NoPath ? "the engine finds no path to its target"
                                                           : "the engine finds no line of sight to its target on "
                                                             "the way to it";
        CHECK(Logged(w, "Genlock's attack on Hero is given back before a step (" + why + ")"));
        CHECK(Logged(w, "Genlock chooses again, its action given back"));
        CHECK_FALSE(Logged(w, "chose nothing"));
        const auto given = std::ranges::find_if(
            events, [](const TurnEvent &e) { return e.kind == EventKind::ActionGivenBack && e.who == kGenlock; });
        REQUIRE(given != events.end());
        CHECK(given->detail == why);
        CHECK(given->command == "attack");
        CHECK(given->note == "Hero");
        // Not said to the player: it is not the player's.
        CHECK_FALSE(w.Said(L"No room at the target"));
        CHECK_FALSE(w.Said(L"No way through"));
        CHECK_FALSE(w.Said(L"No line of sight"));
        CHECK(w.machine.View().notice.empty());
    }
}

TEST_CASE("every attack is put to the engine afresh: a target it refused is let through when it answers yes",
          "[noattack]")
{
    // The engine's no is for where the two stood when it was asked. Nothing
    // of it is kept: the same target a frame on, the ring with a place now.
    World w(Timed());
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId) {
        answers.push_back(Swing(w, kHero, answers.empty() ? NoAttack::NoPlace : NoAttack::None));
    };
    ToGenlock(w);
    w.RunFor(100);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0] == Verdict::Block);
    CHECK(answers[1] == Verdict::Allow);
    CHECK(w.Current() == kGenlock);
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    CHECK(Lines(w, "is given back before a step") == 1);
    CHECK_FALSE(Logged(w, "ruled out"));
}

TEST_CASE("an AI the engine refuses twice in a turn has nothing it can get to: its turn ends", "[noattack]")
{
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, ends);
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            // Its script names the same target each time it is asked,
            // and each time the engine has no place there.
            answers.push_back(Swing(w, kHero, NoAttack::NoPlace));
        }
    };
    ToGenlock(w);
    w.RunFor(200);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0] == Verdict::Block);
    CHECK(answers[1] == Verdict::Block);
    CHECK_FALSE(w.Get(kGenlock).busy);
    CHECK(Lines(w, "Genlock's attack on Hero is given back before a step (the engine has no place at its target "
                   "for it)") == 2);
    // Over at the second, with nothing started: not at the cap on a
    // turn's commands, nor after its thinking time.
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.size() == 1);
    CHECK(ends.front() == "nothing it can get to");
    CHECK_FALSE(Logged(w, "commands this turn"));
    CHECK_FALSE(Logged(w, "chose nothing"));
}

TEST_CASE("an attack failed at the blow, its script moves closer, and the same target is let through again and lands",
          "[noattack]")
{
    // What the withdrawn rule refused. The engine fails an attack the gate
    // let through (then, one whose ring filled a frame after its claim;
    // still, a walker stuck against bodies), and the game's own AI answers
    // that with a move to its target and the attack again (ai_main_h_2.nss,
    // AI_ExecuteAttack). In 7 turns of 17 that second attack went through
    // and did not fail.
    for (const char *why : {kNoSpace, kNoPath})
    {
        World w(Timed());
        std::vector<std::string> ends;
        KeepEnds(w, ends);
        int pokes = 0;
        w.onPoke = [&](ObjectId id) {
            if (id == kGenlock && ++pokes == 1)
            {
                CHECK(Swing(w, kHero) == Verdict::Allow);
            }
        };
        ToGenlock(w);
        REQUIRE(pokes == 1);
        CHECK(w.machine.View().timeLeftMs == 3000);
        CHECK(w.Fail(kGenlock, why)); // given back
        CHECK(w.machine.View().timeLeftMs == 4000);
        // The log's line of it says whom it was at: in a crowd the names
        // alone do not.
        CHECK(Logged(w, std::string{"Genlock's attack on Hero failed ("} + why + "): its action is given back"));
        // Its script's own answer, in the same instant: the move, which is
        // its own to make, and then the same target, asked of the engine
        // again and let through. The move is a walk it takes, most of the
        // meter a turn has free: one done where it began would be its pass
        // (issue 59; test_nowhere.cpp).
        CHECK(w.Order(kGenlock, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
        w.Walk(kGenlock, 0.8f);
        w.Finish(kGenlock);
        CHECK(Swing(w, kHero) == Verdict::Allow);
        CHECK(w.Get(kGenlock).current == CommandType::Attack);
        CHECK_FALSE(Logged(w, "given back before a step"));
        CHECK_FALSE(Logged(w, "ruled out"));
        // It swings its second and lands: the one swing is what the turn
        // paid, and the turn goes on as after any attack.
        w.RunFor(1000);
        w.Finish(kGenlock);
        w.RunFor(50);
        CHECK(w.Current() == kGenlock);
        CHECK(w.machine.View().timeLeftMs == 3000);
        CHECK(ends.empty());
    }
}

TEST_CASE("the failure's line names no target where the game layer gave none", "[noattack]")
{
    World w(Timed());
    w.onPoke = [&](ObjectId) { CHECK(Swing(w, kHero) == Verdict::Allow); };
    ToGenlock(w);
    CHECK(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, kNoSpace));
    CHECK(Logged(w, "Genlock's attack failed (COMMAND_FAILED_NO_SPACE_IN_MELEE_RING): its action is given back"));
}

TEST_CASE("a turn that keeps failing at the blow, its script moving and trying again as each fails, is ended by the "
          "limit on a turn an AI plays",
          "[noattack][limit]")
{
    // Nothing is remembered of a failure, and nothing counts a turn's
    // commands (issue 38: until 2026-10-04 max_commands_per_turn ended this
    // turn at three attacks and three moves, "done"). An AI whose script
    // moves and tries the same target again as each attack fails never lets
    // the turn settle, so the count of its actions given back, two, is not
    // looked at, and each failed swing is given back whole: its bar never
    // moves. What ends it is its turn's time, max_turn_ms. (The move is one
    // the engine fails too, for want of a path, as it often did in play:
    // DAOLib's dao/CommandEvents.h. A move done where it began would be its
    // AI's pass, and the turn over at the first: issue 59, test_nowhere.cpp.)
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, ends);
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && ++pokes == 1)
        {
            CHECK(Swing(w, kHero) == Verdict::Allow);
        }
    };
    ToGenlock(w);
    w.applied.clear();
    int swings = 1; // its first, by its AI's choice
    std::uint32_t ran = 0;
    for (; ran < 60000 && w.Current() == kGenlock; ran += 32)
    {
        (void)w.Fail(kGenlock, kNoSpace);
        if (w.Order(kGenlock, CommandType::MoveToObject, Source::Script) != Verdict::Allow)
        {
            break;
        }
        w.Frame();
        (void)w.Fail(kGenlock, kNoPath);
        if (Swing(w, kHero) == Verdict::Allow)
        {
            ++swings;
        }
        w.Frame();
    }
    // Thirty seconds of it, two frames a round: the attack begun in the
    // frame the limit was passed is stopped, and nothing of the bar went to
    // any of them.
    CHECK(w.Current() != kGenlock);
    CHECK(ran >= 30000);
    CHECK(ran <= 30100);
    CHECK(swings > 900);
    CHECK(Lines(w, "Genlock's attack on Hero failed (COMMAND_FAILED_NO_SPACE_IN_MELEE_RING): its action is given "
                   "back") > 900);
    CHECK(Logged(w, "Genlock's turn is still going after 30000 ms, "));
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    REQUIRE(ends.size() == 1);
    CHECK(ends.front() == "took too long");
    CHECK_FALSE(Logged(w, "commands this turn"));
}

TEST_CASE("given back before a step and then at the blow: twice in a turn, and its turn ends", "[noattack]")
{
    // The engine's no beforehand and its failure under way are counted
    // together (kGivenBackEnough).
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, ends);
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            answers.push_back(Swing(w, kHero, answers.empty() ? NoAttack::NoPlace : NoAttack::None));
        }
    };
    ToGenlock(w);
    w.RunFor(100);
    REQUIRE(answers.size() == 2);
    REQUIRE(answers[1] == Verdict::Allow);
    CHECK(w.Fail(kGenlock, kNoSpace));
    w.RunFor(100);
    CHECK(answers.size() == 2); // not asked a third time
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.size() == 1);
    CHECK(ends.front() == "nothing it can get to");
}

TEST_CASE("its script answers each failure with a wait: the wait costs its time, and the second failure ends the turn",
          "[noattack]")
{
    // Issue 31's wait, after a failure (_AI_DoNothing): a second of the bar
    // each, and the turn is still ended by the second action given back.
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, ends);
    int swings = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && Swing(w, kHero) == Verdict::Allow)
        {
            ++swings;
        }
    };
    ToGenlock(w);
    REQUIRE(swings == 1);
    CHECK(w.Fail(kGenlock, kNoSpace));
    CHECK(w.Wait(kGenlock, 1.0f) == Verdict::Block); // refused at the queue, and paid for
    CHECK(Logged(w, "Genlock waits 1.00 s; 3.00 s"));
    w.RunFor(100);
    REQUIRE(swings == 2); // told again as the turn settled
    CHECK(Logged(w, "Genlock chooses again, its action given back"));
    CHECK(w.Fail(kGenlock, kNoSpace));
    CHECK(w.Wait(kGenlock, 1.0f) == Verdict::Block);
    CHECK(Logged(w, "Genlock waits 1.00 s; 2.00 s"));
    w.RunFor(100);
    CHECK(swings == 2);
    CHECK(w.Current() != kGenlock);
    REQUIRE(ends.size() == 1);
    CHECK(ends.front() == "nothing it can get to");
    CHECK_FALSE(Logged(w, "commands this turn"));
}

// Where this meets issue 29's rules: a hostile that has noticed nobody to
// fight is not told its turn has come, and time rolls over only from a turn
// its actor ended.

TEST_CASE("an unaware hostile is never told, so it has no attack to refuse; its script's in its beat is the game's, "
          "and no more",
          "[noattack][perception]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            ++pokes;
        }
    };
    w.Get(kGenlock).perception = Unnoticing();
    ToGenlock(w);
    REQUIRE(Logged(w, "Genlock is passed: it has not noticed the party"));
    // Its AI is not told, so nothing of its choosing comes to the gate.
    CHECK(pokes == 0);
    CHECK(Lines(w, "before a step") == 0);
    // An attack its own script queues in the beat, at a target the engine
    // has no room at. The game does not have it in combat: it has no turn,
    // and what its script has it do is the game's (issue 43), let through
    // and left to the engine's own answer. Not refused by the mod, not
    // given back (its AI chose nothing this turn), and its AI not told,
    // which would wake it. (Until 2026-10-05 the mod refused it by the
    // engine's answer, "it is unaware, and its AI has not been told its turn
    // has come": that is still so of one the game has taken into combat
    // since the machine last looked, the next case.)
    CHECK(Swing(w, kHero, NoAttack::NoPlace) == Verdict::Allow);
    CHECK(Lines(w, "is refused before a step") == 0);
    CHECK(Lines(w, "is given back before a step") == 0);
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::ActionGivenBack; }));
    w.RunFor(100);
    CHECK(pokes == 0);
    CHECK(w.Current() == kGenlock);
    // The beat goes on to its end: skipped as unaware, nothing rolled over.
    // Settled from the refusal it would have ended "done", a second saved.
    w.RunFor(1000);
    CHECK(w.Current() != kGenlock);
    const auto ended = std::ranges::find_if(
        events, [](const TurnEvent &e) { return e.kind == EventKind::TurnEnded && e.who == kGenlock; });
    REQUIRE(ended != events.end());
    CHECK(ended->detail == "unaware");
    CHECK(Logged(w, "Genlock's turn ends: unaware; nothing rolls over"));
    CHECK(pokes == 0);
}

TEST_CASE("an unaware hostile that notices in its beat and attacks at once is asked of the engine like any",
          "[noattack][perception]")
{
    World w(Timed());
    std::vector<std::string> ends;
    KeepEnds(w, ends);
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            answers.push_back(Swing(w, kHero, answers.empty() ? NoAttack::NoPlace : NoAttack::None));
        }
    };
    w.Get(kGenlock).perception = Unnoticing();
    ToGenlock(w);
    w.RunFor(200);
    REQUIRE(answers.empty());
    REQUIRE(w.Current() == kGenlock);
    // The party comes into its perception, and its own script attacks in the
    // same instant, before the machine has looked at it again: the engine
    // has no room. Refused; not yet counted, its AI not yet told.
    w.Get(kGenlock).perception = Perception{};
    CHECK(Swing(w, kHero, NoAttack::NoPlace) == Verdict::Block);
    CHECK(Logged(w, "Genlock's attack on Hero is refused before a step (the engine has no place at its target for "
                    "it): it is unaware, and its AI has not been told its turn has come"));
    CHECK(Lines(w, "is given back before a step") == 0);
    // The next look sees it has noticed, and its AI is told. From there its
    // choice is asked of the engine like anyone's: no room, given back
    // before a step and told again; room, let through.
    w.RunFor(100);
    CHECK(Logged(w, "Genlock has noticed someone to fight: its turn is its AI's"));
    REQUIRE(answers.size() == 2);
    CHECK(answers[0] == Verdict::Block);
    CHECK(answers[1] == Verdict::Allow);
    CHECK(Lines(w, "Genlock's attack on Hero is given back before a step (the engine has no place at its target for "
                   "it)") == 1);
    CHECK(Logged(w, "Genlock chooses again, its action given back"));
    CHECK(w.Get(kGenlock).current == CommandType::Attack);
    // And its turn is not skipped under its attack when the beat would end.
    w.RunFor(1000);
    CHECK(w.Current() == kGenlock);
    CHECK(ends.empty());
}

TEST_CASE("a turn ended for an AI the engine refused twice rolls nothing over", "[noattack][time]")
{
    // "Nothing it can get to" is the mod ending its turn for it (issue 29:
    // time rolls over only from a turn its actor ended). Its bar was whole,
    // nothing taken by either refusal, and a second of it would have rolled
    // over had it ended the turn itself.
    World w(Timed());
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            answers.push_back(Swing(w, kHero, answers.size() < 2 ? NoAttack::NoPlace : NoAttack::None));
        }
    };
    ToGenlock(w);
    w.RunFor(200);
    REQUIRE(answers.size() == 2);
    REQUIRE(w.Current() != kGenlock);
    CHECK(Logged(w, "Genlock's turn ends: nothing it can get to; nothing rolls over, its actor not having ended it "
                    "(1.00 s would have)"));
    // Its next turn: a bar of the turn's 4 s, and none of it rolled over.
    for (int i = 0; i < 4000 && w.Current() != kGenlock; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(100);
    REQUIRE(answers.size() == 3);
    CHECK(answers[2] == Verdict::Allow);
    CHECK(w.machine.View().timeMs == 4000);
    CHECK(w.machine.View().rolledMs == 0);
}

// A kept attack is asked of the engine too. With a turn a bar of time an
// attack that does not fit is kept and given to its creature as its next
// turn begins, through the gate. It was let through there whatever the
// engine answered: two in a staged crowd (2026-10-04) were given with the
// gate's own line saying no room, failed 50 ms on, and what their turns had
// paid for them was not given back.

TEST_CASE("the player's kept attack the engine has no room for as its turn comes is not made, and said why",
          "[noattack][time]")
{
    World w(Timed());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Two swings of a second and a half, and a third that does not fit the
    // second left: kept, owing half a second.
    for (int swing = 0; swing < 2; ++swing)
    {
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.5f) == Verdict::Allow);
        w.Finish(kHero);
        w.RunFor(600);
    }
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.5f) == Verdict::Hold);
    REQUIRE(w.heldCasts.contains(kHero));
    // By her next turn the genlock's ring has filled: asked as the kept
    // attack is given, the engine has no room for her.
    w.heldCasts[kHero].noAttack = NoAttack::NoPlace;
    UntilTurnOf(w, kHero);
    w.RunFor(100);
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Block);
    CHECK_FALSE(w.Get(kHero).busy);
    CHECK(Logged(w, "Hero's pending attack on Genlock is not made: the engine has no place at its target for it; the "
                    "0.50 s it owed given back"));
    // What this turn paid for it is back: the bar is whole.
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(w.machine.View().pendingPaidMs == 0);
    // Said over her and on the caption, as a refused order is.
    const auto floaty = std::ranges::find_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == L"No room at the target"; });
    REQUIRE(floaty != w.applied.end());
    CHECK(floaty->target == kHero);
    CHECK(floaty->colour == kGrey);
    CHECK(w.machine.View().notice == L"No room at the target: Hero's attack is not made");
    const auto dropped = std::ranges::find_if(
        events, [](const TurnEvent &e) { return e.kind == EventKind::ActionDropped && e.who == kHero; });
    REQUIRE(dropped != events.end());
    CHECK(dropped->detail == "the engine has no place at its target for it");
    CHECK(dropped->command == "attack");
    CHECK(dropped->note == "Genlock");
    // And the turn is hers, waiting on an order: the same one is asked of
    // the engine again, and with room it is taken.
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.5f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 2500);
}

TEST_CASE("an AI's kept attack the engine has no room for is not made: its turn is handed to it, the bound of two "
          "kept",
          "[noattack][time]")
{
    for (const bool room : {false, true})
    {
        World w(Timed());
        std::vector<std::string> ends;
        KeepEnds(w, ends);
        std::vector<Verdict> answers;
        w.onPoke = [&](ObjectId id) {
            if (id != kGenlock)
            {
                return;
            }
            // Its first choice: a swing longer than its bar, kept, two
            // seconds owed. Told again: someone it has room at, or the hero
            // once more, the engine still with no room there.
            answers.push_back(answers.empty() ? w.Attack(id, kHero, Source::Script, NoAttack::None, 0.0f, 6.0f)
                              : room          ? Swing(w, kCompanion)
                                              : Swing(w, kHero, NoAttack::NoPlace));
        };
        ToGenlock(w);
        REQUIRE(answers.size() == 1);
        REQUIRE(answers[0] == Verdict::Hold);
        REQUIRE(w.heldCasts.contains(kGenlock));
        w.heldCasts[kGenlock].noAttack = NoAttack::NoPlace;
        UntilTurnOf(w, kGenlock);
        w.RunFor(200);
        REQUIRE(w.released.size() == 1);
        CHECK(w.released.front() == Verdict::Block);
        CHECK(Logged(w, "Genlock's pending attack on Hero is not made: the engine has no place at its target for "
                        "it; the 2.00 s it owed given back"));
        // Not said to the player: it is not the player's.
        CHECK_FALSE(w.Said(L"No room at the target"));
        CHECK(w.machine.View().notice.empty());
        // Handed its turn, its AI is told once, and what it chooses is asked
        // of the engine like any.
        REQUIRE(answers.size() == 2);
        if (room)
        {
            CHECK(answers[1] == Verdict::Allow);
            CHECK(w.Current() == kGenlock);
            CHECK(w.Get(kGenlock).current == CommandType::Attack);
            // The two seconds back, and the one swing taken.
            CHECK(w.machine.View().timeLeftMs == 3000);
        }
        else
        {
            // The kept attack not made counts as one given back: the next
            // refusal is the second, and the turn ends.
            CHECK(answers[1] == Verdict::Block);
            CHECK(Lines(w, "is given back before a step") == 1);
            CHECK(w.Current() != kGenlock);
            REQUIRE(!ends.empty());
            CHECK(ends.back() == "nothing it can get to");
        }
    }
}
