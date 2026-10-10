// The state machine driven through scripted fights, a case each, in the
// stand-in for the game (World.h). test_fights.cpp drives it through random
// ones.
#include "World.h"

#include "core/BarWindow.h"
#include "core/State.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace tb;
using namespace tb::test;

TEST_CASE("a fight goes turn by turn: the party's orders, the AI's choices, rounds, the end", "[machine]")
{
    World w;
    w.StartFight();

    // The world stopped as the fight began, everyone's real-time commands
    // are dropped, and the highest initiative goes first: the hero, waiting
    // for an order, the world idling on while she chooses.
    CHECK(w.Did(EffectKind::Pause));
    CHECK_FALSE(w.paused);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Signalled(events::kCombatStart));
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Did(EffectKind::Select, kHero));
    CHECK(w.Said(L"Hero's turn"));

    // Nobody else may act.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kCompanion, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Not Alistair's turn"));
    // What happens to a creature is not its choice, and goes through.
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.Finish(kGenlock);

    // The hero attacks: the world runs while it does.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.Frame();
    CHECK_FALSE(w.paused);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    w.RunFor(300);
    w.Finish(kHero);
    // The swing done, the world settles, and the next order is waited for.
    w.RunFor(500);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.paused);

    // Its auto-attack carrying on is its own AI's, not the player's order: no.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Script) == Verdict::Block);
    // It may still move, until its movement runs out: the 2 s its swing left
    // walk 10 m, and a meter is free.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 14 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.Said(L"Out of movement"));
    CHECK(w.Get(kHero).position.x < 13.0f);
    // And no further: it may not walk on.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Block);

    // Its bar spent: the turn ends by itself, and the genlock's AI is told
    // to choose.
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow); };
    w.RunFor(500);
    CHECK(w.Current() == kGenlock);
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK(w.machine.GetPhase() == Phase::Executing);
    w.Frame();
    CHECK_FALSE(w.paused);
    w.Finish(kGenlock);
    w.Frame();
    w.Frame();
    // Its command done, it chooses again (COMMAND_COMPLETE): a second attack
    // is let through, there being time for it, and with its bar spent a
    // third is held.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    w.RunFor(600);

    // Alistair, the player's too.
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.paused);
    // The pause key ends the turn.
    CHECK(w.machine.OnEndTurn());
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Wait, Source::Script) == Verdict::Block); };
    w.Frame();
    w.Frame();

    // The hurlock waits, which passes its turn, and round 2 begins.
    CHECK(w.Did(EffectKind::PokeAi, kHurlock));
    w.Frame();
    w.Frame();
    w.Frame();
    CHECK(w.machine.Order().Round() == 2);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);

    // The enemies fall; at the next turn the fight is over.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle);
    CHECK_FALSE(w.paused);
    CHECK(w.Signalled(events::kCombatEnd));

    // The game still says combat for a moment: no new fight starts.
    w.RunFor(200);
    CHECK(w.machine.GetPhase() == Phase::Idle);
    w.mode = Mode::Explore;
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle);
}

TEST_CASE("the player's attack at something out of reach this turn is refused before a step", "[machine]")
{
    World w; // 21 m to move
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Too far"));
    CHECK(w.machine.View().timeLeftMs == 4000);
    // Past the movement left by a little: refused too, since the walk is a
    // straight line and the path is never shorter (the third play test: 9.3
    // m with 9.0 left, let through and cut short). Within it: taken. (With
    // the whole bar walked its swing does not fit, and is kept for the next
    // turn: test_time.cpp.)
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 21.3f) == Verdict::Block);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 21.0f) == Verdict::Hold);
}

TEST_CASE("the hero is the one the player controls once the fight is over", "[machine]")
{
    // The third play test: the last turn's companion stayed selected, and
    // the first click after the fight moved her.
    World w;
    w.StartFight();
    w.applied.clear();
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(600); // the fight ends at the status read that sees them fallen
    REQUIRE_FALSE(w.machine.Active());
    CHECK(w.Did(EffectKind::Select, kHero));
}

TEST_CASE("a hold that runs out by itself is heard as expired, a settings change and a handover too", "[events]")
{
    World w;
    std::vector<std::string> kinds;
    w.machine.SetEventSink([&](const TurnEvent &e) { kinds.emplace_back(Name(e.kind)); });
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    w.Get(kGenlock).condition = Condition::None; // worn off
    w.RunFor(600);
    CHECK(std::ranges::count(kinds, std::string{"condition_expired"}) == 1);
    w.machine.SetSettings(Fixed());
    CHECK(std::ranges::count(kinds, std::string{"settings_changed"}) == 1);
}

TEST_CASE("an AI's attack at something far away goes on and closes in", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Attack, Source::Script, 30.0f) == Verdict::Allow); };
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.GetPhase() == Phase::Executing);
}

TEST_CASE("a creature that joins the fight stops what it was doing", "[machine]")
{
    World w;
    w.StartFight();
    constexpr ObjectId kShriek = 0x202;
    // Walking as it comes: a patrol, or a cutscene's order.
    Creature shriek = Make(kShriek, Side::Enemy, false, L"Shriek", 12.0f);
    shriek.busy = true;
    shriek.current = CommandType::MoveToLocation;
    w.creatures.push_back(shriek);
    CHECK(w.machine.OnEndTurn()); // the next turn's scan finds it
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.Order().Find(kShriek) != nullptr);
    CHECK(w.Did(EffectKind::ClearCommands, kShriek));
    CHECK_FALSE(w.Get(kShriek).busy);
}

TEST_CASE("an AI creature out of movement ends its turn", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero passes
    w.onPoke = [&](ObjectId id) { w.Order(id, CommandType::Attack, Source::Script); };
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    // Its bar walks 21 m, its swing's time gone to the walk.
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 25 && w.Current() == kGenlock; ++i)
    {
        w.Get(kGenlock).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.Current() == kCompanion);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Get(kGenlock).position.x > 20.0f);
}

TEST_CASE("an AI creature that chooses nothing passes after ai_think_ms", "[machine]")
{
    Settings settings = Fixed();
    settings.aiThinkMs = 1000;
    World w{settings};
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    w.RunFor(900);
    CHECK(w.Current() == kGenlock);
    w.RunFor(200);
    CHECK(w.Current() != kGenlock);
}

TEST_CASE("an AI's action that never ends is stopped after max_action_ms", "[machine]")
{
    // An AI's: a turn the player plays has no such limit (issue 51;
    // test_ownmode.cpp), and the case was the hero's until then.
    Settings settings = Fixed();
    settings.maxActionMs = 2000;
    World w{settings};
    w.StartFight();
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::UseAbility, Source::Script) == Verdict::Allow); };
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear(); // the fight's beginning cleared everyone's commands
    w.RunFor(1900);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    w.RunFor(200);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(w.Current() == kCompanion);
}

TEST_CASE("the machine's patience with the engine is of the wall's time, whatever the fight's speed", "[machine]")
{
    // Issue 16: the machine's clock runs with the fight's speed, so at three
    // times the speed max_action_ms was a third as long on the wall, and a
    // creature working its way through a crowd, which the speed does not
    // hurry, was cut off: 122 of a night's 1,394 turns at Redcliffe.
    for (const float speed : {1.0f, 3.0f})
    {
        CAPTURE(speed);
        Settings settings = Fixed();
        settings.maxActionMs = 2000;
        settings.fightSpeed = speed;
        World w{settings};
        w.StartFight();
        // (An AI's action: a turn the player plays has no such limit, issue
        // 51.)
        w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::UseAbility, Source::Script) == Verdict::Allow); };
        CHECK(w.machine.OnEndTurn());
        w.Frame();
        w.Frame();
        REQUIRE(w.Current() == kGenlock);
        w.applied.clear(); // the fight's beginning cleared everyone's commands
        w.RunFor(1900);
        CHECK(w.Current() == kGenlock);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
        w.RunFor(200);
        CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
        CHECK(w.Current() == kCompanion);
    }
}

TEST_CASE("under tactics the companions play their own turns, the hero is still the player's", "[machine]")
{
    Settings settings = Fixed();
    settings.partyControl = PartyControl::Tactics;
    World w{settings};
    w.StartFight();
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder); // the hero
    CHECK(w.machine.OnEndTurn());
    // The genlock waits; the companion's tactics are left thinking.
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    for (int i = 0; i < 10 && w.Current() != kCompanion; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
    // The companion's AI plays only if it is not the one the player
    // controls: the hero is selected.
    const auto select = std::ranges::find_if(w.applied | std::views::reverse,
                                             [](const Effect &e) { return e.kind == EffectKind::Select; });
    REQUIRE(select != (w.applied | std::views::reverse).end());
    CHECK(select->target == kHero);
}

TEST_CASE("the toggle works outside combat and is refused in it", "[machine]")
{
    World w;
    w.Frame();
    CHECK(w.machine.RequestToggle() == ToggleResult::Disabled);
    w.mode = Mode::Combat;
    w.Frame();
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle); // off: real time
    CHECK_FALSE(w.paused);
    CHECK(w.machine.RequestToggle() == ToggleResult::RefusedInCombat);
    w.mode = Mode::Explore;
    w.Frame();
    CHECK(w.machine.RequestToggle() == ToggleResult::Enabled);
    w.StartFight();
    CHECK(w.machine.RequestToggle() == ToggleResult::RefusedInCombat);
}

TEST_CASE("the toggle is refused under a menu opened over a real-time fight", "[machine]")
{
    World w;
    w.mode = Mode::Gui;
    w.Frame();
    CHECK(w.machine.RequestToggle(true) == ToggleResult::RefusedInCombat);
    CHECK(w.machine.RequestToggle(false) == ToggleResult::Disabled);
}

TEST_CASE("nobody's commands are limited in number: the player's are not, and an AI's are not either", "[machine]")
{
    // Until 2026-10-04 an AI's were, by max_commands_per_turn, "a guard
    // against an AI that loops". The user (issue 38): "What is the command
    // cap? Do we need to limit the number of commands that can be issued? I
    // thought we relied on time as the limiting factor", and: "Yeah let's
    // get rid of the cap."
    World w{Fixed()};
    w.StartFight();
    for (int i = 0; i < 5; ++i)
    {
        CHECK(w.Order(kHero, CommandType::Drive, Source::Player) == Verdict::Allow);
        w.Finish(kHero);
    }
    w.RunFor(500); // settled, awaiting the next order
    CHECK(w.machine.OnEndTurn());
    // Eight weapon switches of its script's, one as each ends: more than the
    // six the setting let through, and all of them are.
    int allowed = 0;
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (++told > 1)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
            return;
        }
        for (int i = 0; i < 8; ++i)
        {
            allowed += w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow ? 1 : 0;
            w.Finish(id);
        }
    };
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kGenlock);
    CHECK(allowed == 8);
    CHECK_FALSE(std::ranges::any_of(
        w.log, [](const std::string &line) { return line.find("commands this turn") != std::string::npos; }));
    // Its script queues no more. Told again as it settles, nothing of its bar
    // spent, it passes, and its turn ends.
    w.RunFor(1000);
    CHECK(told == 2);
    CHECK(w.Current() == kCompanion);
}

TEST_CASE("a conversation in the middle of a fight stands the gate aside, then the turn goes on as it was", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    const auto turnStarts = std::ranges::count_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::Signal && e.event == events::kTurnStart; });
    w.mode = Mode::Dialog;
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Suspended);
    CHECK_FALSE(w.machine.Active());
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    w.mode = Mode::Combat;
    w.Frame();
    CHECK(w.paused);
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Current() == kHero);
    // The time it spent before the conversation stays spent, and the turn
    // did not start twice.
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(std::ranges::count_if(w.applied, [](const Effect &e) {
              return e.kind == EffectKind::Signal && e.event == events::kTurnStart;
          }) == turnStarts);
}

TEST_CASE("after a conversation between two turns the next one begins afresh", "[machine]")
{
    // Nothing had begun to go on with: the genlock's turn starts with its
    // own movement and its turn-start event, not as a resumed one. The
    // random fights held this only by a rare sequence (TESTING.md).
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::TurnStart);
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    w.applied.clear();
    w.mode = Mode::Combat;
    for (int i = 0; i < 10 && w.Current() == kGenlock && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        w.Frame();
    }
    CHECK(w.Current() == kGenlock);
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Signal && e.event == events::kTurnStart && e.target == kGenlock;
    }));
    CHECK(w.machine.View().movementLeft > 0.0f);
}

TEST_CASE("an AI an effect holds as its turn comes is not told until it is let go", "[machine][conditions]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock)); // it would choose and queue behind the effect
    // Three seconds: past the two it was once given before its turn was
    // taken (issue 30). The hold is the engine's, and so is its length.
    w.RunFor(3000);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    w.Finish(kGenlock); // up again
    w.RunFor(100);
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("a menu over the fight holds everything as it is", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.mode = Mode::Gui;
    w.paused = true; // the menu pauses
    w.RunFor(2000);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    w.mode = Mode::Combat;
    w.paused = false; // leaving it unpauses
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Executing);
}

TEST_CASE("everyone is looked at as a menu goes, not half a second after a look that fell under it", "[machine]")
{
    // The random fights' find (the long run with hostiles unaware, seed 59,
    // 2026-10-04): the half second's look at everyone came in a menu's
    // frame, where the machine takes nothing up, and the next was due half a
    // second after it. With two menus in a second, a creature stunned in
    // its own turn stood more than a second before its turn ended.
    World w;
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow); };
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    // Stunned in its turn, and a menu opened before the machine has looked.
    w.Get(kGenlock).condition = Condition::Disabled;
    w.mode = Mode::Gui;
    w.paused = true;
    const int looks = w.statusReads;
    w.RunFor(2000);
    // At most the look that came with the menu's first frame: none under it.
    CHECK(w.statusReads <= looks + 1);
    REQUIRE(w.Current() == kGenlock);
    w.mode = Mode::Combat;
    w.paused = false;
    w.Frame();
    w.Frame();
    w.Frame();
    // Seen within a frame or two of the menu's going: the rest of its turn
    // is the stun's.
    CHECK(w.Current() != kGenlock);
}

TEST_CASE("a look at everyone that came with a menu's first frame is asked for again", "[machine]")
{
    // The other half of the same: the look is taken in the frame the menu
    // opens (the machine learns of the menu with that frame), nothing is
    // done with it, and a menu shorter than half a second leaves the next
    // look that long away.
    World w;
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow); };
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    // To the frame before the next half second's look: a look, and 31 frames
    // of 16 ms after it.
    const int looks = w.statusReads;
    for (int i = 0; i < 100 && w.statusReads == looks; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.statusReads == looks + 1);
    for (int i = 0; i < 31; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.statusReads == looks + 1);
    // Stunned, and a menu open for the one frame the look comes with.
    w.Get(kGenlock).condition = Condition::Disabled;
    w.mode = Mode::Gui;
    w.paused = true;
    w.Frame();
    REQUIRE(w.statusReads == looks + 2);
    REQUIRE(w.Current() == kGenlock);
    w.mode = Mode::Combat;
    w.paused = false;
    w.Frame();
    w.Frame();
    w.Frame();
    CHECK(w.Current() != kGenlock);
}

TEST_CASE("loading a game drops the fight without touching its objects", "[machine]")
{
    World w;
    w.StartFight();
    w.applied.clear();
    w.mode = Mode::Loading;
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle);
    // Only the clock, which is no object's, let go.
    CHECK(std::ranges::all_of(w.applied, [](const Effect &e) { return e.kind == EffectKind::HoldTime && e.a == 0; }));
}

TEST_CASE("combat with no enemy in reach stays real-time until one fights", "[machine]")
{
    World w;
    w.inCombat = false; // the enemies are not fighting yet
    w.StartFight();
    CHECK(w.machine.GetPhase() == Phase::Idle);
    CHECK_FALSE(w.paused);
    const int before = w.scans;
    w.RunFor(1000);
    CHECK(w.scans > before); // it looks again, once a second
    w.inCombat = true;
    w.RunFor(1100);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Did(EffectKind::Pause)); // the fight began, the world stopped for it
}

TEST_CASE("a creature joining mid-fight waits for its first turn", "[machine]")
{
    World w;
    w.StartFight();
    constexpr ObjectId kShriek = 0x202;
    // Not yet in the order: held, and a scan asked for.
    // A script's command for a creature, in combat or not, as the gate
    // has it.
    const auto scripted = [](ObjectId object, CommandType type, bool inCombat) {
        CommandRequest request;
        request.object = object;
        request.type = type;
        request.source = Source::Script;
        request.inCombat = inCombat;
        return request;
    };
    CHECK(w.machine.OnCommand(scripted(kShriek, CommandType::Attack, true)) == Verdict::Block);
    // Not fighting at all (a townsperson walking): let be.
    CHECK(w.machine.OnCommand(scripted(0x300, CommandType::MoveToLocation, false)) == Verdict::Allow);
    w.creatures.push_back(Make(kShriek, Side::Enemy, false, L"Shriek", 30.0f)); // initiative 21, first next round
    w.Frame();
    REQUIRE(w.machine.Order().Find(kShriek) != nullptr);
    CHECK(w.machine.Order().Find(kShriek)->initiative == 21);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.Order().All().front().id == kShriek);
}

TEST_CASE("the view lists the turns to come, whose it is and what is left", "[machine]")
{
    World w;
    w.StartFight();
    const TurnView view = w.machine.View();
    CHECK(view.active);
    CHECK(view.playerTurn);
    CHECK(view.current == kHero);
    CHECK(view.timeMs == 4000);
    CHECK(view.timeLeftMs == 4000);
    CHECK(view.movementMax == 21.0f);
    REQUIRE(view.upcoming.size() == 4); // each combatant once
    CHECK(view.upcoming[0].id == kHero);
    CHECK(view.upcoming[0].current);
    CHECK(view.upcoming[1].id == kGenlock);
    CHECK(view.upcoming[3].round == 1);
}

TEST_CASE("new settings apply from the next turn", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.View().movementMax == 21.0f);
    Settings longer = Fixed();
    longer.secondsPerTurn = 6.0f;
    w.machine.SetSettings(longer);
    CHECK(w.machine.View().movementMax == 21.0f); // this turn's stands
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Wait, Source::Script) == Verdict::Block); };
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().timeMs == 6000);
    CHECK(w.machine.View().movementMax == 31.0f);
}

TEST_CASE("the view lists every turn to come, and the bar's row shows ten of them", "[machine]")
{
    // Issue 14: twelve portraits put the bar's buttons under the minimap.
    // The user, 2026-10-03: "let's make it 10 instead of 12 ... Large fights
    // like these are going to overflow anyway". And issue 23: the view was
    // cut to those ten, so the rest were on no card and in no count, and a
    // tool that read the order lost a party member for four rounds. The
    // view has them all; the row is ten of them (core/BarWindow.h).
    auto fight = StandardFight();
    for (ObjectId id = 0x300; id < 0x30a; ++id)
    {
        fight.push_back(Make(id, Side::Enemy, false, L"Walking Corpse", 10.0f));
    }
    World w(Fixed(), fight);
    w.StartFight();
    const TurnView view = w.machine.View();
    REQUIRE(w.machine.Order().All().size() == 14);
    REQUIRE(view.upcoming.size() == 14);
    CHECK(view.upcoming.front().current);
    CHECK(view.upcoming.back().id == 0x309);
    const BarSpan row = BarWindow{}.Follow(view);
    CHECK(row.before == 0);
    CHECK(row.shown == 10);
    CHECK(row.after == 4);
    // And the state a tool reads has all fourteen in its order.
    const std::string state = ToJson(w.machine.State());
    CHECK(state.find(R"("id":"0x309")") != std::string::npos);
}

TEST_CASE("the pause key is the game's outside a turn-based fight", "[machine]")
{
    World w;
    w.Frame();
    CHECK_FALSE(w.machine.OnEndTurn());
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.mode = Mode::Dialog;
    w.Frame();
    CHECK_FALSE(w.machine.OnEndTurn()); // a conversation's own
}

TEST_CASE("a conversation that ends the fight ends it", "[machine]")
{
    World w;
    w.StartFight();
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    w.mode = Mode::Explore; // a surrender, say
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle);
    CHECK(w.Signalled(events::kCombatEnd));
}

TEST_CASE("those of the fight still standing when it ends are told it is over", "[machine]")
{
    // Issue 12: at Redcliffe's windmill the game ended combat with one
    // walking corpse alive up the path. Its AI had chosen to wait, turn after
    // turn: a wait is its pass, blocked, so nothing was queued and nothing
    // would end to run its AI again. With the turns over nobody told it, and
    // it stood in combat and commandless while the story waited on its death.
    World w;
    w.StartFight();
    w.Get(kHurlock).down = true; // one enemy has fallen; the genlock stands
    w.RunFor(600);
    REQUIRE(w.machine.Active());
    std::vector<ObjectId> told;
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        told.push_back(id);
        // Told with the machine idle, its wait is its own again: its loop.
        answers.push_back(w.Order(id, CommandType::Wait, Source::Script));
    };
    w.mode = Mode::Explore; // the game ends combat: the party sees no enemy
    w.Frame();
    w.Frame();
    CHECK_FALSE(w.machine.Active());
    CHECK(told == std::vector<ObjectId>{kGenlock}); // not the fallen, not the party
    CHECK(answers == std::vector<Verdict>{Verdict::Allow});
}

TEST_CASE("a combatant gone from the world loses its turns", "[machine]")
{
    World w;
    w.StartFight();
    w.Get(kGenlock).exists = false; // a script destroyed it
    w.RunFor(600);                  // the statuses come in
    CHECK(w.machine.Order().Find(kGenlock)->down);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kCompanion); // the genlock's turn skipped
}

TEST_CASE("every phase and effect has a name of its own", "[machine]")
{
    std::vector<std::string> names;
    for (const Phase phase : {Phase::Idle, Phase::Starting, Phase::TurnStart, Phase::AwaitingOrder, Phase::AiThinking,
                              Phase::Executing, Phase::Settling, Phase::Suspended})
    {
        names.emplace_back(Name(phase));
    }
    for (const EffectKind kind :
         {EffectKind::Pause, EffectKind::Unpause, EffectKind::ClearCommands, EffectKind::Select, EffectKind::PokeAi,
          EffectKind::Floaty, EffectKind::Signal, EffectKind::CenterCamera, EffectKind::Explain,
          EffectKind::EndConditions, EffectKind::Cooldown, EffectKind::HoldTime, EffectKind::AgeTime})
    {
        names.emplace_back(Name(kind));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(std::ranges::find(names, std::string{"?"}) == names.end());
}

TEST_CASE("an AI that chooses nothing has what it saw explained", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero passes; the genlock's AI stays silent
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.RunFor(2000);
    CHECK(w.Did(EffectKind::Explain, kGenlock));
    CHECK(w.Current() == kCompanion);
}

TEST_CASE("camera follow shows an AI's turn once it has chosen, not before its AI is told", "[machine]")
{
    // Centring the camera on an enemy just before the poke left every enemy
    // choosing nothing, in play (2026-09-30).
    Settings s = Fixed();
    s.cameraFollow = true;
    World w(s);
    w.StartFight();
    CHECK(w.Did(EffectKind::CenterCamera, kHero)); // the player's turn: at once
    w.applied.clear();
    bool centredBeforePoke = false;
    w.onPoke = [&](ObjectId id) {
        centredBeforePoke = w.Did(EffectKind::CenterCamera, id);
        CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
    };
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    CHECK_FALSE(centredBeforePoke);
    CHECK(w.Did(EffectKind::CenterCamera, kGenlock));
}

TEST_CASE("a creature stunned between its turns loses the next one, the world still", "[machine][conditions]")
{
    World w;
    w.StartFight();
    w.Get(kGenlock).condition = Condition::Disabled; // the hero's howl
    w.RunFor(600);
    w.applied.clear();
    CHECK(w.machine.OnEndTurn()); // the hero's turn ends
    w.Frame();
    w.Frame();
    // The genlock's turn is taken at once: no AI told to act, nothing run.
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::Unpause));
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK(w.Current() == kCompanion);
}

TEST_CASE("a condition that has had its turns is ended at the start of the creature's turn, and it acts",
          "[machine][conditions]")
{
    World w;
    w.StartFight();
    w.Get(kGenlock).condition = Condition::Disabled; // a long stun: the engine's timer would hold it
    w.RunFor(600);
    bool stunnedWhenPoked = false;
    w.onPoke = [&](ObjectId id) {
        stunnedWhenPoked = w.Get(id).condition == Condition::Disabled;
        CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
    };
    // Round 1: the genlock's turn lost; round 2: its condition ended, it acts.
    for (int i = 0; i < 400 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.RunFor(100);
    }
    w.applied.clear();
    for (int i = 0; i < 400 && w.Current() != kGenlock; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    CHECK(w.Did(EffectKind::EndConditions, kGenlock));
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(stunnedWhenPoked);
    CHECK(w.Get(kGenlock).condition == Condition::None);
}

TEST_CASE("still at an effect's command after its condition was ended, a creature is held like any other",
          "[machine][conditions]")
{
    // Issue 11: a hurlock alpha's stun was ended as its turn began, and
    // something else held it at an effect's command still. Getting up from
    // an ended hold was exempt from "held by an effect" for the whole turn,
    // so it stood max_action_ms, twelve seconds of the game's time; then for
    // five seconds of the mod's own choosing. It is held by the engine's
    // command like any creature found at one (issue 30): its turn waits, and
    // is lost at a turn's worth.
    World w;
    w.StartFight();
    w.Get(kGenlock).condition = Condition::Disabled;
    w.RunFor(600);
    w.onPoke = [&](ObjectId) {}; // held: its AI has nothing to choose
    for (int i = 0; i < 400 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.RunFor(100);
    }
    // The engine has it at an effect's command the statuses do not name.
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.applied.clear();
    for (int i = 0; i < 400 && w.Current() != kGenlock; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    REQUIRE(w.Did(EffectKind::EndConditions, kGenlock));
    // Held from the first: its AI is not told to choose behind the command.
    CHECK(w.machine.State().heldByEffect);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    // A knockdown's rise is over two seconds: the turn waits for it.
    w.RunFor(2500);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    // Its bar's worth of it, four seconds, and the turn is lost: not twelve.
    w.RunFor(3700);
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK(w.Current() != kGenlock);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock)); // the effect is the engine's
    CHECK(std::ranges::none_of(w.log,
                               [](const std::string &line) { return line.find("still at a") != std::string::npos; }));
}

TEST_CASE("a creature held as the fight begins loses its first turn", "[machine][conditions]")
{
    // Pinning Shot or a howl before the fight's first turn: its statuses are
    // read before anyone's turn starts.
    World w;
    w.Get(kHero).condition = Condition::Disabled;
    w.StartFight();
    CHECK(w.Said(L"Hero loses the turn"));
    CHECK(w.Current() != kHero);
    CHECK_FALSE(w.Did(EffectKind::Select, kHero));
}

TEST_CASE("End pressed while something the engine put there holds the creature ends the turn at once",
          "[machine][conditions]")
{
    // Seen in play (2026-09-30, 13:25): a pinned character, End pressed,
    // and the turn waited 12 s for the pin's command to end.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.RunFor(100);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    w.applied.clear();
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    // The pin's command is the engine's to end: not cleared, as an action
    // of her own would be (issue 51).
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.Get(kHero).current == CommandType::ExecuteEffect);
}

TEST_CASE(
    "a creature an effect holds as its turn comes loses the turn at a turn's worth, and no order queues behind it",
    "[machine][conditions]")
{
    // A condition the statuses do not name (a grab, say): the engine keeps
    // its creature in the effect's command, for as long as the engine does.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(100);
    // Its companion's turn comes with the companion held.
    CHECK(w.Order(kCompanion, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.Finish(kGenlock);
    for (int i = 0; i < 100 && w.Current() != kCompanion; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    w.RunFor(100);
    w.applied.clear();
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 0.0f) == Verdict::Block);
    w.RunFor(3000);
    CHECK(w.Said(L"Held"));
    CHECK(w.Current() == kCompanion); // no two seconds of the mod's: the hold is the engine's
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 0.0f) == Verdict::Block);
    w.RunFor(1200);
    CHECK(w.Current() != kCompanion);
    CHECK(w.Said(L"Alistair loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kCompanion)); // the effect is the engine's
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Alistair is held by an effect's command past its bar (4.0") != std::string::npos;
    }));
}

TEST_CASE("End pressed during the creature's own action stops it, and the turn ends at once", "[machine]")
{
    // Until issue 51 End waited for the action ("its own action it finishes
    // first"), and one the engine never finished held the turn to
    // max_action_ms. A turn the player plays has no such limit now, so End
    // is its way out (test_ownmode.cpp has the rest).
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.RunFor(100);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    w.applied.clear();
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK_FALSE(w.Get(kHero).busy); // the swing stopped: nothing of it arrives in the genlock's turn
}

TEST_CASE("a creature stunned in its own turn loses the rest of it at once, and what it had queued",
          "[machine][conditions]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.RunFor(100);
    w.applied.clear();
    w.Get(kHero).condition = Condition::Disabled; // a riposte
    w.RunFor(600);
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.Current() == kGenlock);
}

namespace
{
constexpr std::int32_t kShieldBash = 617; // 20 s: five turns of its user's clock, at 4 s a turn
constexpr std::int32_t kShieldWall = 12;  // sustained

bool Cooled(const World &w, ObjectId id, std::int32_t ability, std::int32_t seconds)
{
    return std::ranges::any_of(w.applied, [&](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == id && e.a == ability && e.b == seconds;
    });
}

// Everyone else's turns pass until the hero's begins again.
void UntilHerosTurn(World &w)
{
    for (int i = 0; i < 2000 && w.Current() == kHero; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.Frame();
    }
    for (int i = 0; i < 2000 && (w.Current() != kHero || w.machine.GetPhase() != Phase::AwaitingOrder); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kHero);
}
} // namespace

// (What an ability cools for, and that it does so on its user's own clock,
// whatever started it: test_cooldowns.cpp, issue 57.)

TEST_CASE("a sustained ability turned on or off costs nothing and keeps the turn", "[machine][cooldowns]")
{
    for (const bool on : {false, true})
    {
        World w;
        w.StartFight();
        CHECK(w.UseAbility(kHero, Source::Player, kShieldWall, 15.0f, true, on) == Verdict::Allow);
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(600);
        CHECK(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.View().timeLeftMs == 4000); // nothing of the bar went to it
        CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    }
}

TEST_CASE("at the fight's end, cooldowns still held go back to seconds", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 90015, 60.0f) == Verdict::Allow); // Overwhelm: a minute
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    w.applied.clear();
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
    // What it has left: the rest of the turn it was used in, 2 s, has passed.
    CHECK(Cooled(w, kHero, 90015, 58));
}

TEST_CASE("on the player's turn the character's own AI does not spend it", "[machine]")
{
    // Seen in play: a pinned character's AI carried on attacking its last
    // target and ended the turn before the player had chosen.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
    // Nor its wait, with no enemy to choose (issue 10).
    CHECK(w.Order(kHero, CommandType::Wait, Source::Script) == Verdict::Block);
    w.RunFor(500);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // The player's own order still goes through.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
}

TEST_CASE("on the player's turn the character's own AI does not change its weapons", "[machine]")
{
    // Seen in play (issue 4): after a kept shot landed with a spider at
    // hand, the follower AI put Lyna on her daggers and turned Tamlen's bow
    // mode off, both free and both let through.
    constexpr std::int32_t kRapidShot = 3071;
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Block);
    CHECK(w.UseAbility(kHero, Source::Script, kRapidShot, 0.0f, true, true) == Verdict::Block);
    w.RunFor(500);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // The player's own are free as ever, by the weapon button and the quickbar.
    CHECK(w.Order(kHero, CommandType::SwitchWeaponSets, Source::Player) == Verdict::Allow);
    w.Finish(kHero);
    CHECK(w.UseAbility(kHero, Source::Player, kRapidShot, 0.0f, true, false) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(500);
    CHECK(w.Current() == kHero);
    // And an AI's on its own turn is its own to make.
    CHECK(w.machine.OnEndTurn());
    int allowed = 0;
    w.onPoke = [&](ObjectId id) {
        allowed += w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow ? 1 : 0;
    };
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(allowed == 1);
}

TEST_CASE("a turn the player plays says when it waits on an order", "[machine][events]")
{
    // Issue 7: a script playing a turn through a test server read the state
    // several times a second between orders, for want of this.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    const auto waits = [&](ObjectId who) {
        return std::ranges::count_if(
            events, [&](const TurnEvent &e) { return e.kind == EventKind::AwaitingOrder && e.who == who; });
    };
    const auto last = [&] {
        const auto found =
            std::ranges::find(events.rbegin(), events.rend(), EventKind::AwaitingOrder, &TurnEvent::kind);
        REQUIRE(found != events.rend());
        return *found;
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    // As the turn is handed to the player, once, with what it has to spend.
    CHECK(waits(kHero) == 1);
    CHECK(last().detail == "the turn is its player's");
    CHECK(last().value > 0);
    CHECK(last().note.ends_with(" m"));
    // Frames go by while it waits: said at the step into waiting, not while it lasts.
    w.RunFor(500);
    CHECK(waits(kHero) == 1);
    // A walk played out and the turn goes on: it waits again, and says so.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(waits(kHero) == 2);
    CHECK(last().detail == "after its action");
    // The turn ended: nothing waits; and an AI's turn never does.
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(300);
    CHECK(waits(kHero) == 2);
    CHECK(waits(kGenlock) == 0);
}

TEST_CASE("a refusal repeated every frame is logged once a second", "[machine]")
{
    // The player steering with the keys out of turn: a drive a frame.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    int lines = 0;
    w.onLog = [&](std::string_view line) {
        if (line.find("Alistair: drive from player held, not its turn") != std::string_view::npos)
        {
            ++lines;
        }
    };
    for (int i = 0; i < 60; ++i) // a second of frames
    {
        CHECK(w.Order(kCompanion, CommandType::Drive, Source::Player) == Verdict::Block);
        w.Frame();
    }
    CHECK(lines == 1);
    w.RunFor(1100);
    CHECK(w.Order(kCompanion, CommandType::Drive, Source::Player) == Verdict::Block);
    CHECK(lines == 2);
}

TEST_CASE("the world idles on while the player chooses, and nobody else acts", "[machine]")
{
    // The user: not pausing on a party member's turn lets the others idle.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.RunFor(500);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.paused);
    // The enemies are still held.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
}

TEST_CASE("a fight holds effects' time, spends a round's worth each round, and lets it go at its end", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.timeHeld);
    CHECK(w.agedMs == 0); // the first round has not been spent
    // A round passes: everyone's turn.
    for (int i = 0; i < 4000 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == 2);
    w.Frame(); // the new round begins with the next turn
    w.Frame();
    CHECK(w.agedMs == 4000); // a turn's seconds a round
    CHECK(w.timeHeld);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Idle);
    CHECK_FALSE(w.timeHeld);
}

TEST_CASE("a fight dropped by a load lets effects' time go", "[machine]")
{
    World w;
    w.StartFight();
    REQUIRE(w.timeHeld);
    w.mode = Mode::Loading;
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Idle);
    CHECK_FALSE(w.timeHeld);
}

TEST_CASE("a fight's turns run at its speed, everyone's alike; a conversation and the rest at the game's own",
          "[machine][speed]")
{
    Settings s = Fixed();
    s.fightSpeed = 2.0f;
    World w{s};
    CHECK(w.speed == 1000);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.speed == 2000); // her turn
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.speed == 2000); // an enemy's: the same
    w.RunFor(3000);         // it chooses nothing, and the companion's turn comes
    REQUIRE(w.Current() == kCompanion);
    CHECK(w.speed == 2000);
    // A conversation plays at the game's own speed.
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kHurlock);
    w.mode = Mode::Dialog;
    w.Frame();
    CHECK(w.speed == 1000);
    // Back to the hurlock's turn, at the fight's speed again; and the
    // fight's end, not.
    w.mode = Mode::Combat;
    w.RunFor(100);
    CHECK(w.speed == 2000);
    for (const ObjectId id : {kGenlock, kHurlock})
    {
        w.Get(id).down = true;
    }
    w.RunFor(1000);
    CHECK_FALSE(w.machine.Active());
    CHECK(w.speed == 1000);
}

TEST_CASE("at speed 1 the game's speed is never written", "[machine][speed]")
{
    // Another plugin may be setting it (DAOLib's examples/speed).
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.RunFor(3000);
    CHECK_FALSE(w.Did(EffectKind::GameSpeed));
}

TEST_CASE("the speed is the game's to change in a turn, and the fight's again as the next turn comes",
          "[machine][speed]")
{
    // Issue 71 (Denerim's market, 2026-10-09): a death blow's animation
    // writes the game's speed for its slow motion, and the engine puts it to
    // 1 as the blow's command ends. The machine had said 3 as the fight
    // began and, remembering that, never said it again: the fight ran at the
    // game's own speed from the first death blow to its end.
    Settings s = Fixed();
    s.fightSpeed = 3.0f;
    World w{s};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.speed == 3000);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.RunFor(300);
    w.speed = 1000; // the game's own doing, in the middle of her blow
    w.applied.clear();
    w.RunFor(300);
    // Left to the game while the blow lasts: nothing is said over it.
    CHECK_FALSE(w.Did(EffectKind::GameSpeed));
    CHECK(w.speed == 1000);
    w.Finish(kHero);
    w.RunFor(500);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.speed == 3000);
}

TEST_CASE("a conversation as combat begins: the fight starts afresh after it", "[machine]")
{
    // Seen in the long run of random fights (seed 256): the fight's start
    // was skipped, and effects' time never held.
    World w;
    w.mode = Mode::Combat;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Starting);
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    w.mode = Mode::Combat;
    for (int i = 0; i < 10 && w.Current() == kNoObject; ++i)
    {
        w.Frame();
    }
    w.Frame();
    CHECK(w.timeHeld);
    CHECK(w.machine.Order().Round() == 1);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
}

TEST_CASE("at speed 2 an AI's waits pass in half the time", "[machine][speed]")
{
    // The desktop play test: an enemy's turn took 3 s at speed 1 and at 2,
    // the machine's own waits counting real time.
    for (const float speed : {1.0f, 2.0f})
    {
        Settings s = Fixed();
        s.fightSpeed = speed;
        World w{s};
        w.StartFight();
        CHECK(w.machine.OnEndTurn());
        w.Frame();
        w.Frame();
        REQUIRE(w.Current() == kGenlock); // its AI chooses nothing
        w.RunFor(static_cast<std::uint32_t>((s.aiThinkMs / 2) + 100));
        if (speed == 2.0f)
        {
            CHECK(w.Current() != kGenlock); // its thinking time, in the world's time, is over
        }
        else
        {
            CHECK(w.Current() == kGenlock);
        }
    }
}

TEST_CASE("a creature falling is said in the log", "[machine]")
{
    World w;
    std::vector<std::string> lines;
    w.onLog = [&](std::string_view line) { lines.emplace_back(line); };
    w.StartFight();
    w.Get(kHurlock).down = true;
    w.RunFor(600); // a status read
    CHECK(std::ranges::count(lines, std::string{"Hurlock falls"}) == 1);
}

namespace
{
// Ends turns until the genlock's conditions are ended at its turn's start.
void UntilEnded(World &w)
{
    for (int i = 0; i < 80 && !w.Did(EffectKind::EndConditions, kGenlock); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
}
} // namespace

TEST_CASE("a condition the engine still holds after it was ended owes no fresh turn", "[machine][conditions]")
{
    // The desktop play test (2026-09-30): the dog's pin, ended at its turn,
    // was read again half a second later and charged a turn more.
    World w;
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    UntilEnded(w);
    REQUIRE(w.Did(EffectKind::EndConditions, kGenlock));
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).condition = Condition::Disabled; // the engine holds it still
    w.RunFor(600);                                   // a status read: the rest of this turn is lost
    CHECK(w.Current() != kGenlock);
    // It runs out before the genlock's next turn: that turn is its own.
    w.Get(kGenlock).condition = Condition::None;
    w.RunFor(600);
    w.applied.clear();
    for (int i = 0; i < 80 && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
}

TEST_CASE(
    "a condition the engine still holds after it was ended costs each turn while it lasts, and is not ended again",
    "[machine][conditions]")
{
    World w;
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    UntilEnded(w);
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).condition = Condition::Disabled;
    w.RunFor(600);
    w.applied.clear();
    for (int i = 0; i < 80 && !w.Said(L"Genlock loses the turn"); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::EndConditions, kGenlock)); // the game's clock ends it
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
}

TEST_CASE("a condition that lands once the ended one was seen gone is a new one", "[machine][conditions]")
{
    // Witch Hunt, the Dragonbone Wastes (2026-10-10, issue 100): Ariane's
    // knockdown was paid and ended as her turn began, she was free and
    // ordered a talent, and a second later she was knocked down again. Taken
    // for the first still standing, it was left to the game's clock with a
    // warning, and her next turn went whole.
    World w;
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    UntilEnded(w);
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(600); // a status read finds it free
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).condition = Condition::Disabled; // held anew
    w.RunFor(600);
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("the engine still holds it") != std::string::npos; }));
    // Owed as any condition is, and ended by the machine when it is paid.
    w.applied.clear();
    UntilEnded(w);
    CHECK(w.Did(EffectKind::EndConditions, kGenlock));
}

TEST_CASE("a move its path cannot finish this turn is refused before a step", "[machine]")
{
    // The engine's path to the click (DAOLib FindPath), as the gate measures
    // it: longer than the meters left, "Too far", as for an attack.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 30.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Too far"));
    CHECK(w.machine.View().movementLeft == w.machine.View().movementMax); // not a step taken
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 4.0f) == Verdict::Allow);
    // A move with no path told (path_check off, or no path found) walks as before.
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
}

TEST_CASE("party control changed in a companion's turn hands the turn to whoever plays it now", "[machine]")
{
    // The second play test (2026-09-30): manual to tactics in Eleanor's
    // turn, and the fight stood waiting on an order the player could no
    // longer give.
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero's
    for (int i = 0; i < 80 && w.Current() != kCompanion; ++i)
    {
        w.RunFor(100);
    }
    REQUIRE(w.Current() == kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics;
    w.applied.clear();
    w.machine.SetSettings(tactics);
    w.Frame();
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    // And back: the player's again, selected.
    Settings manual = tactics;
    manual.partyControl = PartyControl::Manual;
    w.applied.clear();
    w.machine.SetSettings(manual);
    w.Frame();
    CHECK(w.Did(EffectKind::Select, kCompanion));
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.OnEndTurn());
}

TEST_CASE("a command that failed without acting gives its action back: the acting creature's, and no move's",
          "[machine]")
{
    // The third play test: an attack the engine failed with an invalid path,
    // its action spent all the same. (And each time one fails so:
    // test_time.cpp.)
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    CHECK_FALSE(
        w.machine.OnCommandFailed(kGenlock, CommandType::Attack, "COMMAND_FAILED_INVALID_PATH")); // not its turn
    CHECK_FALSE(w.machine.OnCommandFailed(kHero, CommandType::MoveToLocation, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(w.machine.View().timeLeftMs == 2000);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(w.machine.View().timeLeftMs == 4000);
}

TEST_CASE("a companion's AI turn let go by an effect selects the hero before its AI is told", "[machine][conditions]")
{
    // The fourth play test: the dog held at its turn's start, party control
    // turned to tactics meanwhile, and poked while still the one selected,
    // its AI chose nothing.
    Settings s = Fixed();
    s.partyControl = PartyControl::Tactics;
    World w{s};
    w.StartFight();
    CHECK(w.Order(kCompanion, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    for (int i = 0; i < 80 && w.Current() != kCompanion; ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    REQUIRE(w.Current() == kCompanion);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion)); // held
    w.applied.clear();
    w.Finish(kCompanion); // let go
    w.RunFor(100);
    CHECK(w.Did(EffectKind::Select, kHero));
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
}

TEST_CASE("with the hero down the machine reaches for another of the party standing, not for her body",
          "[machine][party]")
{
    // Issue 42's fault, one creature standing for the party though it is
    // dead, where the machine reaches for the hero: whom it selects while a
    // companion's tactics play its turn, and over whom a fight's banner
    // floats. Read in the code, not seen in play.
    Settings s = Fixed();
    s.partyControl = PartyControl::Tactics;
    constexpr ObjectId kMage = 0x102;
    std::vector<Creature> fight = StandardFight();
    fight.push_back(Make(kMage, Side::Party, false, L"Morrigan", 12.0f)); // initiative 3: after Alistair
    const auto banner = [](const World &w) {
        const auto said = std::ranges::find_if(
            w.applied, [](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == L"Turn-based combat"; });
        return said != w.applied.end() ? said->target : kNoObject;
    };
    // Who was selected as a companion's AI was told its turn had come: the
    // last one selected before it, or nobody.
    const auto selectedFor = [](const World &w, ObjectId told) {
        ObjectId selected = kNoObject;
        for (const Effect &e : w.applied)
        {
            if (e.kind == EffectKind::Select)
            {
                selected = e.target;
            }
            if (e.kind == EffectKind::PokeAi && e.target == told)
            {
                return selected;
            }
        }
        return kNoObject;
    };
    {
        // The hero standing: hers, as it always was.
        World w{s, fight};
        w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
        w.StartFight();
        CHECK(banner(w) == kHero);
        for (int i = 0; i < 80 && !w.Did(EffectKind::PokeAi, kCompanion); ++i)
        {
            (void)w.machine.OnEndTurn();
            w.RunFor(100);
        }
        CHECK(selectedFor(w, kCompanion) == kHero);
    }
    World w{s, fight};
    w.onPoke = [&](ObjectId id) { (void)w.Order(id, CommandType::Wait, Source::Script); };
    w.Get(kHero).down = true;
    w.StartFight();
    // The banner is over the first of the party standing in the order.
    CHECK(banner(w) == kCompanion);
    // Alistair's turn is his tactics', and Morrigan the one selected
    // meanwhile; on hers, Alistair. Never the fallen hero.
    for (int i = 0; i < 80 && !w.Did(EffectKind::PokeAi, kMage); ++i)
    {
        w.RunFor(100);
    }
    REQUIRE(w.Did(EffectKind::PokeAi, kCompanion));
    REQUIRE(w.Did(EffectKind::PokeAi, kMage));
    CHECK(selectedFor(w, kCompanion) == kMage);
    CHECK(selectedFor(w, kMage) == kCompanion);
    CHECK_FALSE(w.Did(EffectKind::Select, kHero));
    // The last of the party standing has nobody to be selected in its place:
    // it stays the one controlled, and nothing is selected.
    w.Get(kMage).down = true;
    w.RunFor(1500); // the statuses read it, wherever the round is
    w.applied.clear();
    for (int i = 0; i < 80 && !w.Did(EffectKind::PokeAi, kCompanion); ++i)
    {
        w.RunFor(100);
    }
    REQUIRE(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK_FALSE(w.Did(EffectKind::Select));
}

TEST_CASE("party control changed mid-command is said, and taken as the command ends", "[machine][events]")
{
    World w;
    std::vector<std::string> handed;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnHandedOver)
        {
            handed.push_back(e.detail);
        }
    });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow); // executing
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics; // the hero stays the player's
    w.machine.SetSettings(tactics);
    CHECK(handed.empty());
    // Alistair's turn, mid-command, handed to his AI.
    w.Finish(kHero);
    for (int i = 0; i < 80 && w.Current() != kCompanion; ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    REQUIRE(w.Current() == kCompanion);
    w.machine.SetSettings(Fixed()); // back to manual: the player's now
    REQUIRE(handed.size() == 1);
    CHECK(handed[0] == "the player's");
}

TEST_CASE("the last enemy fallen outside anyone's attack ends the fight on the player's turn at once", "[machine]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(600); // a status read, the hero still choosing
    CHECK_FALSE(w.machine.Active());
}

TEST_CASE("a talent that failed without running is not held on cooldown", "[machine]")
{
    // The engine started none (it never ran), and the machine holds only
    // what the engine has cooling: until issue 57 it had reckoned one as the
    // command was let through, and took it back as the command failed.
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 803, 20.0f, false, false) == Verdict::Allow);
    CHECK(w.Fail(kHero, "COMMAND_FAILED_INVALID_DATA"));
    w.RunFor(1200);
    CHECK_FALSE(w.Did(EffectKind::Cooldown, kHero));
    CHECK(w.machine.State().cooldowns.empty());
}

TEST_CASE("the state's last refusal is the player's, with its command and numbers", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block);
    CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Block); // the AI's, not the player's
    CHECK(w.machine.State().lastRefusal == "Too far (attack: 30.0 m to walk, 21.0 m left)");
}

TEST_CASE("a hold taken off before its victim's turn gives that turn back", "[machine][conditions]")
{
    // The fourth play test's addendum: a stun lifted (stage remove_effects)
    // still cost its victim the turn.
    World w;
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    REQUIRE(w.machine.State().held.size() == 1);
    w.Get(kGenlock).condition = Condition::None; // cured, mid-round
    w.RunFor(600);
    CHECK(w.machine.State().held.empty());
    for (int i = 0; i < 80 && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(w.Did(EffectKind::PokeAi, kGenlock)); // its turn, played
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
}

TEST_CASE("the game's speed is set for a turn before its camera moves", "[machine][speed]")
{
    // Play tests 2 and 5, when an AI's turn had a speed of its own: at twice
    // the speed the pan to the hero, issued before the speed went back to 1,
    // never arrived. A fight has one speed now, said as each turn comes
    // (issue 71); what is held is that nothing writes it after the camera is
    // moved.
    Settings s = Fixed();
    s.fightSpeed = 2.0f;
    s.cameraFollow = true;
    World w{s};
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero's turn, then the genlock's
    for (int i = 0; i < 80 && w.Current() != kCompanion; ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    w.applied.clear();
    for (int i = 0; i < 200 && w.Current() != kHero; ++i)
    {
        (void)w.machine.OnEndTurn();
        w.Frame();
    }
    REQUIRE(w.Current() == kHero);
    for (int i = 0; i < 20 && w.machine.GetPhase() != Phase::AwaitingOrder; ++i)
    {
        w.Frame(); // the turn-start frame begins it
    }
    const auto camera = std::ranges::find_if(
        w.applied, [](const Effect &e) { return e.kind == EffectKind::CenterCamera && e.target == kHero; });
    REQUIRE(camera != w.applied.end());
    CHECK(std::none_of(camera, w.applied.end(), [](const Effect &e) { return e.kind == EffectKind::GameSpeed; }));
    CHECK(w.machine.State().speed == 2.0f);
}

TEST_CASE("an error in a frame stands the fight down: the game let go, no turns until the next fight", "[machine]")
{
    // The review's TB-3: effects lost to an exception left the machine
    // ahead of the game.
    Settings s = Fixed();
    s.fightSpeed = 2.0f;
    World w{s};
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(kHero);
    (void)w.machine.OnEndTurn();
    for (int i = 0; i < 200 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    REQUIRE(w.timeHeld);
    REQUIRE(w.speed == 2000);
    // The engine as a lost batch could leave it, the machine none the wiser.
    w.applied.clear();
    w.StandDown("frame: a test's error");
    CHECK_FALSE(w.machine.Active());
    CHECK_FALSE(w.timeHeld);
    CHECK(w.speed == 1000);
    CHECK_FALSE(w.paused);
    CHECK(w.Did(EffectKind::Unpause));
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == 617 && e.b < 3600;
    }));
    // The game still says combat, the enemies stand: no turns, every order
    // allowed, and standing down again says nothing.
    w.applied.clear();
    w.RunFor(3000);
    CHECK_FALSE(w.machine.Active());
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Allow);
    CHECK(w.machine.StandDown("again").empty());
    // The next fight is in turns again.
    w.mode = Mode::Explore;
    w.Frame();
    w.StartFight();
    CHECK(w.machine.Active());
}

TEST_CASE("standing down says the clock and the speed again, whatever the machine believes", "[machine]")
{
    // On the player's turn the machine has the speed at 1: a lost batch may
    // have left it otherwise, and the clock held or not.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.timeHeld = true;
    w.speed = 2000;
    w.StandDown("frame: a test's error");
    CHECK_FALSE(w.timeHeld);
    CHECK(w.speed == 1000);
    // Outside a fight too.
    World idle;
    idle.timeHeld = true;
    idle.speed = 2000;
    idle.StandDown("frame: a test's error");
    CHECK_FALSE(idle.timeHeld);
    CHECK(idle.speed == 1000);
}

TEST_CASE("a talent its user ran out of movement before reaching is not held on cooldown", "[machine][cooldowns]")
{
    // The user, 2026-09-30: a skill chosen, the walk to its target using
    // the movement up, the turn over: it went on cooldown unused.
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero passes
    w.onPoke = [&](ObjectId id) { w.UseAbility(id, Source::Script, 603, 20.0f); };
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 25 && w.Current() == kGenlock; ++i)
    {
        w.Get(kGenlock).position.x += 1.0f;
        w.Frame();
    }
    REQUIRE(w.Current() != kGenlock);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::Cooldown, kGenlock));
    CHECK(w.machine.State().cooldowns.empty());
}

TEST_CASE("the player's talent cut short by movement is not held; the next one used is", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
    w.applied.clear();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 25 && !w.Did(EffectKind::ClearCommands, kHero); ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::ClearCommands, kHero));
    w.Get(kHero).moving = false;
    w.Frame();
    // Her bar went to the walk, and the turn is over with it. On her next,
    // another talent, in reach, runs: it alone is held.
    UntilHerosTurn(w);
    CHECK(w.UseAbility(kHero, Source::Player, 618, 20.0f) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(kHero);
    w.RunFor(2000);
    (void)w.machine.OnEndTurn();
    w.RunFor(200);
    const auto held = [&](std::int32_t ability) {
        return std::ranges::any_of(w.applied, [&](const Effect &e) {
            return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == ability;
        });
    };
    CHECK(held(618));
    CHECK_FALSE(held(617));
}

namespace
{
// The standard fight and a third enemy, fighting near but not the party:
// a soldier at a guard's throat rooms away.
World WithOutsider()
{
    auto fight = StandardFight();
    Creature far = Make(0x202, Side::Enemy, false, L"Shriek", 12.0f);
    far.engaged = false;
    fight.push_back(far);
    return World(Fixed(), fight);
}

void PassTurns(World &w, int frames)
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
} // namespace

TEST_CASE("an enemy fighting near, but not the party, waits outside the fight until it is", "[machine][engaged]")
{
    // The user, 2026-09-30: fights took in Howe's men rooms away, and the
    // camera went to rooms not drawn.
    World w = WithOutsider();
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.machine.Active());
    CHECK(w.machine.Order().All().size() == 4);
    CHECK(w.machine.Order().Find(0x202) == nullptr);
    // It is held where it stands: its commands wait, as a joiner's do.
    CHECK(w.Order(0x202, CommandType::Attack, Source::Script) == Verdict::Block);
    PassTurns(w, 600); // rounds go by without it
    CHECK(w.machine.Order().Find(0x202) == nullptr);
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.who == 0x202; }));
    // The party comes into its sight: it joins, and waits for its turn.
    w.Get(0x202).engaged = true;
    PassTurns(w, 200);
    REQUIRE(w.machine.Order().Find(0x202) != nullptr);
    CHECK(
        std::ranges::any_of(events, [](const TurnEvent &e) { return e.kind == EventKind::Joined && e.who == 0x202; }));
}

TEST_CASE("once in the fight, an enemy out of the party's sight again stays in it", "[machine][engaged]")
{
    World w;
    w.StartFight();
    w.Get(kGenlock).engaged = false; // behind a pillar
    bool genlockPlayed = false;
    for (int i = 0; i < 2000 && w.machine.Order().Round() < 4; ++i)
    {
        genlockPlayed = genlockPlayed || (w.Current() == kGenlock && w.machine.Order().Round() >= 2);
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(genlockPlayed);
}

TEST_CASE("with no enemy fighting the party there are no turns, until one is", "[machine][engaged]")
{
    auto fight = StandardFight();
    for (Creature &c : fight)
    {
        c.engaged = c.side == Side::Party;
    }
    World w(Fixed(), fight);
    w.StartFight();
    CHECK_FALSE(w.machine.Active());
    CHECK_FALSE(w.paused);
    // Real time: everyone's orders go through.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Allow);
    w.RunFor(3000);
    CHECK_FALSE(w.machine.Active());
    w.Get(kHurlock).engaged = true; // it rounds the corner
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    CHECK(w.machine.Order().Find(kHurlock) != nullptr);
    CHECK(w.machine.Order().Find(kGenlock) == nullptr);
}

TEST_CASE("the fight is over when the last enemy fighting the party falls, others fighting near or not",
          "[machine][engaged]")
{
    World w = WithOutsider();
    w.StartFight();
    // Told to choose, the one outside finds the machine idle and its
    // command its own again: checked at the moment it is told.
    std::vector<ObjectId> told;
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == 0x202)
        {
            told.push_back(id);
            answers.push_back(w.Order(id, CommandType::Attack, Source::Script));
        }
    };
    // In combat and not in the order, the one command it tries is held.
    CHECK(w.Order(0x202, CommandType::Attack, Source::Script) == Verdict::Block);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    CHECK_FALSE(w.machine.Active());
    // The one outside is the game's again, in real time, and its AI is
    // told so once: its held command was its only one (the Korcari Wilds'
    // wolves and darkspawn stood commandless after the party's fight).
    CHECK(told == std::vector<ObjectId>{0x202});
    CHECK(answers == std::vector<Verdict>{Verdict::Allow});
    w.onPoke = nullptr;
    CHECK(w.Order(0x202, CommandType::Attack, Source::Script) == Verdict::Allow);
    w.applied.clear();
    w.RunFor(3000);
    CHECK_FALSE(w.machine.Active());
    // Nor is a fight begun and dropped with every scan, the game paused a
    // frame each time.
    CHECK_FALSE(w.Did(EffectKind::Pause));
    // And begins turns again when it finds the party.
    w.Get(0x202).engaged = true;
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    CHECK(w.machine.Order().Find(0x202) != nullptr);
}

// Issue 43: whoever the gate turned away with no turn to come is told when
// the fight lets the world go. A command turned away never ends, and for a
// creature that is not of the party a command's end is the one thing that
// runs its AI again (rules_core.nss); a combatant has that mended at its
// turn, and these have no turn.
namespace
{
// The standard fight and a spirit in combat where the scan does not look
// (Ortan Thaig, 2026-10-04: beside a party member 70 m from the party's
// leader, whom the scan measured from).
constexpr ObjectId kSpirit = 0x203;
World WithUnfound(const Settings &settings = Fixed())
{
    auto fight = StandardFight();
    Creature spirit = Make(kSpirit, Side::Enemy, false, L"Forgotten Spirit", 12.0f);
    spirit.unfound = true;
    fight.push_back(spirit);
    return World(settings, fight);
}

// Who is told to choose with no fight holding the world, and what the gate
// answers the command each then asks for: its own again.
struct ToldIdle
{
    explicit ToldIdle(World &w)
    {
        w.onPoke = [this, &w](ObjectId id) {
            if (!w.machine.Active())
            {
                who.push_back(id);
                answers.push_back(w.Order(id, CommandType::Attack, Source::Script));
            }
        };
    }
    std::vector<ObjectId> who;
    std::vector<Verdict> answers;
};

// The genlock goes after someone else, past where the scan looks and out of
// the party's perception, in combat still. An AI has five seconds to choose
// here, so that the scan at the companion's turn finds it missing for the
// four that take it out of the fight: it is then down in the order, alive,
// until the round turns, and the turn is the companion's, waiting.
Settings SlowToChoose()
{
    Settings s = Fixed();
    s.aiThinkMs = 5000;
    return s;
}
void GenlockLeaves(World &w)
{
    w.Get(kGenlock).perception = Unperceived();
    w.StartFight();
    w.Get(kGenlock).unfound = true;
    REQUIRE(w.machine.OnEndTurn()); // the hero passes; the genlock's own turn goes by, nothing chosen
    for (int i = 0; i < 1000 && (w.Current() != kCompanion || w.machine.GetPhase() != Phase::AwaitingOrder); ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    const Combatant *gone = w.machine.Order().Find(kGenlock);
    REQUIRE(gone != nullptr);
    REQUIRE(gone->down);
}
} // namespace

TEST_CASE("one in combat that no scan finds is held by the gate, and told when the fight is over",
          "[machine][turnedaway]")
{
    World w = WithUnfound();
    w.StartFight();
    REQUIRE(w.machine.Active());
    REQUIRE(w.machine.Order().Find(kSpirit) == nullptr);
    // It perceives a party member, the game takes it into combat and its AI
    // asks for its first attack, and for a move as that is held: both wait,
    // as a joiner's do, and its AI asks for nothing more.
    CHECK(w.Order(kSpirit, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kSpirit, CommandType::MoveToObject, Source::Script) == Verdict::Block);
    PassTurns(w, 200); // the scans the gate asked for come, and none finds it
    REQUIRE(w.machine.Active());
    CHECK(w.machine.Order().Find(kSpirit) == nullptr);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kSpirit)); // no turn of its own to tell it
    ToldIdle told{w};
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    // The fight over, it is told, once, and the command it asks for is its own.
    CHECK(told.who == std::vector<ObjectId>{kSpirit});
    CHECK(told.answers == std::vector<Verdict>{Verdict::Allow});
    CHECK(std::ranges::count(w.log, std::string{"1 that left the fight alive, or that the gate turned away outside it, "
                                                "is told to choose, the world theirs again: 0x203"}) == 1);
    // And nothing more is owed it: a second fight in the same combat, in
    // which it asks for nothing, does not tell it again as it ends.
    told.who.clear();
    w.Finish(kSpirit);
    w.Get(kGenlock).down = false; // up again, say, and at the party
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    w.Get(kGenlock).down = true;
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who.empty());
}

TEST_CASE("one that waited outside the fight and was turned away is told once when it is over, not twice",
          "[machine][turnedaway]")
{
    World w = WithOutsider();
    w.StartFight();
    CHECK(w.Order(0x202, CommandType::Attack, Source::Script) == Verdict::Block);
    ToldIdle told{w};
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who == std::vector<ObjectId>{0x202});
}

TEST_CASE("one turned away that then joined the fight and fell in it is not told when it is over",
          "[machine][turnedaway]")
{
    World w = WithUnfound();
    w.StartFight();
    CHECK(w.Order(kSpirit, CommandType::Attack, Source::Script) == Verdict::Block);
    // The party comes within the scan's reach of it: it joins, as anyone
    // does, and has its turns.
    w.Get(kSpirit).unfound = false;
    PassTurns(w, 100);
    REQUIRE(w.machine.Order().Find(kSpirit) != nullptr);
    ToldIdle told{w};
    for (const ObjectId id : {kGenlock, kHurlock, kSpirit})
    {
        w.Get(id).down = true;
    }
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who.empty()); // nobody stands, and the fallen are not told
}

TEST_CASE("one turned away in the frame combat began is of the fight once it begins: fallen, it is not told",
          "[machine][turnedaway]")
{
    World w;
    w.mode = Mode::Combat;
    w.Frame(); // combat began: the gate is shut while the look is taken
    REQUIRE(w.machine.GetPhase() == Phase::Starting);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    w.Frame();
    w.Frame();
    REQUIRE(w.machine.Active());
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    ToldIdle told{w};
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who.empty());
}

TEST_CASE("combat with nobody to fight: whoever the gate turned away while the look was taken is told",
          "[machine][turnedaway]")
{
    // Between "combat began" and the look that finds nobody fighting the
    // party the gate is shut, a frame or two (40 to 75 ms in the playthrough's
    // log), and what it turns away then would be held for a fight that never
    // was.
    auto fight = StandardFight();
    // And four more of the battle rooms away, their ids of no order.
    const std::vector<ObjectId> others{0x9001, 0x12, 0x777, 0x3004};
    for (const ObjectId id : others)
    {
        fight.push_back(Make(id, Side::Enemy, false, L"Soldier", 10.0f));
    }
    for (Creature &c : fight)
    {
        c.engaged = c.side == Side::Party;
    }
    World w(Fixed(), fight);
    w.mode = Mode::Combat;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Starting);
    // A genlock at a guard's throat asks for its next blow, a companion's
    // own AI for its wait, a hurlock for a move, and the rest for theirs.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Wait(kCompanion, 1.0f) == Verdict::Block);
    CHECK(w.Order(kHurlock, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
    for (const ObjectId id : others)
    {
        CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Block);
    }
    ToldIdle told{w};
    w.Frame();
    REQUIRE_FALSE(w.machine.Active());
    // All of them, the party's too: no fight will give any of them a turn.
    // In the order of their ids, not the one they asked in, nor whatever
    // order they happen to be kept in.
    CHECK(told.who == std::vector<ObjectId>{0x12, kCompanion, kGenlock, kHurlock, 0x777, 0x3004, 0x9001});
    CHECK(std::ranges::all_of(told.answers, [](Verdict v) { return v == Verdict::Allow; }));
}

namespace
{
// One the party has run from: it perceives nobody to fight, nobody of the
// party perceives it, and the game has taken it out of combat and stood it
// down (combat_h.nss, Combat_HandleCreatureDisappear).
Perception StoodDown()
{
    Perception perception;
    perception.byParty = false;
    perception.ofParty = false;
    perception.ofFoes = false;
    perception.inCombat = false;
    return perception;
}
} // namespace

TEST_CASE("the party runs from a fight: its enemies stand down as the game orders it, and coming back begins another",
          "[machine][turnedaway]")
{
    // The user, 2026-10-04: "If you run far enough away from the enemies,
    // the fight should end. That's what rtwp does. In that case the enemies
    // should go back to normal. Getting close to them should retrigger a
    // fight."
    World w{SlowToChoose()};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // The enemies lose the party. The game takes each out of combat and its
    // script sends it home, a wait and a walk (Rubber_GoHome): on the hero's
    // turn, with both still in the order. They are the game's again there
    // and then, and both commands go through.
    for (const ObjectId id : {kGenlock, kHurlock})
    {
        w.Get(id).perception = StoodDown();
        w.Get(id).unfound = true;
        CHECK(w.Wait(id, 2.0f) == Verdict::Allow);
        w.Finish(id);
        CHECK(w.Order(id, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
        w.Get(id).moving = true;
    }
    std::vector<ObjectId> told;
    w.onPoke = [&](ObjectId id) { told.push_back(id); };
    w.applied.clear();
    // With the next look both have left, and the fight is over: no turn of
    // theirs comes meanwhile.
    PassTurns(w, 1500);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("Genlock's turn (") != std::string::npos; }));
    CHECK(std::ranges::count_if(w.log, [](const std::string &line) {
              return line.ends_with("rounds: no enemy left in the fight, 2 having left it alive");
          }) == 1);
    // Nothing of theirs was stopped, through the fight or at its end: each
    // is still on its way home.
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHurlock));
    CHECK(w.Get(kGenlock).busy);
    CHECK(w.Get(kHurlock).busy);
    // Each is told once as the fight ends, having left it alive: to one out
    // of combat and walking, the telling is nothing (the game layer's
    // scripts: "Busy - command(s) in queue").
    CHECK(told == std::vector<ObjectId>{kGenlock, kHurlock});
    // And the party comes back: the enemies found fighting again, a fight
    // with turns begins like any other.
    for (const ObjectId id : {kGenlock, kHurlock})
    {
        w.Finish(id);
        w.Get(id).unfound = false;
        w.Get(id).perception = Perception{};
    }
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK(w.machine.Order().Find(kHurlock) != nullptr);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    // That one is fought and won, and its end says so: they fell, nobody
    // left it.
    w.onPoke = nullptr;
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 400);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(std::ranges::count_if(
              w.log, [](const std::string &line) { return line.ends_with("rounds: no enemy left standing"); }) == 1);
}

TEST_CASE("the fight holds what the game has in combat: out of turn, a script's command for one out of combat goes "
          "through",
          "[machine][turnedaway]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // In combat, the genlock is held out of its turn, as ever.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
    // Out of combat by the game's own word, and in the order still (one that
    // has not noticed, drawn in; one the game has stood down): what its
    // script has it do is the game's, and goes through.
    w.Get(kGenlock).perception.inCombat = false;
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Allow);
    w.Finish(kGenlock);
    // Not the player's order for it, nor the engine's own command: neither
    // is its script's.
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Engine) == Verdict::Block);
    // Nor a party member's own AI's, in combat or out: the party is the
    // player's.
    w.Get(kCompanion).perception.inCombat = false;
    CHECK(w.Wait(kCompanion, 1.0f) == Verdict::Block);
    CHECK(w.Order(kCompanion, CommandType::MoveToLocation, Source::Script) == Verdict::Block);
    // Taken into combat, it is held again from that moment.
    w.Get(kGenlock).perception.inCombat = true;
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    // And the turn is still the hero's, nothing of it spent by any of that,
    // and not ended by it: it was the genlock that the game has out of
    // combat, not the hero.
    w.RunFor(100);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
}

// A creature that is not of the party and that the game does not have in
// combat has no turn (issue 43): found so while its turn is being played,
// that turn ends there and then, "out of combat", and what its script has it
// do is the game's.
namespace
{
// A turn a bar of time, for what is charged and what rolls over.
Settings WithABar()
{
    Settings s = Fixed();
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

// The fight begun and the hero's turn passed: the genlock's turn, its AI
// told. What it does when told is the case's (onPoke, set before).
void ToTheGenlocksTurn(World &w)
{
    w.StartFight();
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 10 && (w.Current() != kGenlock || w.machine.GetPhase() == Phase::TurnStart); ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
}
} // namespace

TEST_CASE("taken out of combat in its own turn, its turn ends there: its script's wait and walk home are the game's",
          "[machine][turnedaway]")
{
    // The Dead Trenches, 2026-10-05: the game stood a shriek down while the
    // turn was the shriek's own, and the turn's rules answered: "Shriek's
    // wait of 3.00 s is longer than the 0.19 s it has left: its turn ends",
    // "Shriek: move to location from script held, its turn is over: it has
    // passed". Fifteen seconds after the fight it stood 17.5 m from where it
    // began.
    World w{WithABar()};
    std::vector<std::uint32_t> bars;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnStarted && e.who == kGenlock)
        {
            bars.push_back(w.machine.View().timeMs);
        }
    });
    int attacks = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            ++attacks;
            CHECK(w.Order(id, CommandType::Attack, Source::Script, 0.0f, 1.5f) == Verdict::Allow);
        }
        else
        {
            (void)w.Wait(id, 2.0f); // the hurlock passes
        }
    };
    ToTheGenlocksTurn(w);
    REQUIRE(attacks == 1);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    // Its blow lands, the swing still playing, and the game takes it out of
    // combat and sends it home: a wait of three seconds, longer than the two
    // and a half its bar has left, and a walk.
    w.Get(kGenlock).perception = StoodDown();
    const TurnView before = w.machine.View();
    w.applied.clear();
    CHECK(w.Wait(kGenlock, 3.0f) == Verdict::Allow);
    CHECK(w.Order(kGenlock, CommandType::MoveToLocation, Source::Script) == Verdict::Allow);
    CHECK(
        Lines(
            w,
            "Genlock is passed: out of combat by the game's own word, its script's wait coming for it: it has no turn, "
            "and is the game's") == 1);
    CHECK(Lines(w, "Genlock: wait from script let through out of turn: the game does not have it in combat, and it "
                   "is the game's") == 1);
    CHECK(Lines(w, "Genlock: move to location from script let through out of turn") == 1);
    // Said once, of the first of them: the walk behind it says nothing new.
    CHECK(Lines(w, "Genlock is passed: out of combat by the game's own word") == 1);
    // Neither is its turn's: no wait read as its AI's, nothing taken of its
    // bar, nothing of its movement.
    CHECK(Lines(w, "Genlock's wait of") == 0);
    CHECK(Lines(w, "Genlock waits") == 0);
    CHECK(w.machine.View().timeLeftMs == before.timeLeftMs);
    CHECK(w.machine.View().movementLeft == before.movementLeft);
    // The turn ends with the next step, for that reason and no other, and
    // nothing rolls over though its bar had time: nobody saved it.
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 30; ++i) // it walks home meanwhile, further than its turn could have taken it
    {
        w.Get(kGenlock).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.Current() == kCompanion);
    CHECK(Lines(w, "Genlock's turn ends: ") == 1);
    CHECK(Lines(w, "Genlock's turn ends: out of combat; nothing rolls over, its actor not having ended it") == 1);
    CHECK(Lines(w, "Genlock is passed: out of combat by the game's own word") == 1); // and not again by the look
    // What it was at is the engine's to finish, and its walk is not the
    // turn's to stop.
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK_FALSE(w.Said(L"Out of movement"));
    CHECK(w.Get(kGenlock).busy);
    // The party comes back into its perception before its next turn: in
    // combat again, it has that turn like anyone, its bar a turn's own.
    w.Finish(kGenlock);
    w.Get(kGenlock).perception = Perception{};
    for (int i = 0; i < 2000 && attacks < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    CHECK(attacks == 2);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(bars == std::vector<std::uint32_t>{4000, 4000});
}

TEST_CASE("found out of combat with nobody to fight as its turn is looked at, its turn ends there, whatever its script "
          "says or does not",
          "[machine][turnedaway]")
{
    World w;
    ToTheGenlocksTurn(w); // told, and choosing
    REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
    SECTION("nobody in its perception any more: no turn")
    {
        // No home to be sent to, say: nothing comes to the gate. Until
        // 2026-10-05 its turn stood out its thinking time and ended "chose
        // nothing".
        w.Get(kGenlock).perception = StoodDown();
        w.RunFor(100);
        CHECK(w.Current() == kCompanion);
        CHECK(Lines(w,
                    "Genlock is passed: out of combat by the game's own word, and perceives nobody to fight: it has no "
                    "turn, and is the game's") == 1);
        CHECK(Lines(w, "Genlock's turn ends: out of combat") == 1);
        CHECK_FALSE(w.Did(EffectKind::Explain, kGenlock));
    }
    SECTION("out of combat, and a party member in its perception: about to fight, and its turn is its own")
    {
        // The frame in which it has perceived an enemy and its script has
        // not yet set its combat state (core/Rules.h, NobodyToFight). Its
        // turn goes on, and its AI's attack, a frame later and in combat, is
        // that turn's.
        Perception noticing = StoodDown();
        noticing.ofParty = true;
        w.Get(kGenlock).perception = noticing;
        w.RunFor(100);
        CHECK(w.Current() == kGenlock);
        CHECK(Lines(w, "is passed: out of combat by the game's own word") == 0);
        w.Get(kGenlock).perception = Perception{};
        CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
        w.Frame();
        CHECK(w.machine.GetPhase() == Phase::Executing);
        CHECK(w.Current() == kGenlock);
    }
    SECTION("at the gate its combat state alone is asked: a script's command for one out of combat ends its turn")
    {
        // A command its script queues with its combat state off is not its
        // combat AI's, whoever it perceives (a plot's order, an ally calling
        // it over): the game's, and with it the turn's end. The frame's look
        // would not have said so of this one.
        Perception noticing = StoodDown();
        noticing.ofParty = true;
        w.Get(kGenlock).perception = noticing;
        w.RunFor(100);
        REQUIRE(w.Current() == kGenlock);
        CHECK(w.Order(kGenlock, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
        CHECK(
            Lines(w,
                  "Genlock is passed: out of combat by the game's own word, its script's move to object coming for it: "
                  "it has no turn, and is the game's") == 1);
        w.RunFor(100);
        CHECK(w.Current() == kCompanion);
        CHECK(Lines(w, "Genlock's turn ends: out of combat") == 1);
    }
    SECTION("fallen as it lost them: its turn ends as any fallen creature's")
    {
        w.Get(kGenlock).perception = StoodDown();
        w.Get(kGenlock).down = true;
        w.RunFor(100);
        CHECK(w.Current() == kCompanion);
        CHECK(Lines(w, "Genlock's turn ends: fell") == 1);
        CHECK(Lines(w, "is passed: out of combat by the game's own word") == 0);
    }
}

TEST_CASE("a script's command for one out of combat before its turn has begun does not take the turn that then begins",
          "[machine][turnedaway]")
{
    // Between two turns the order already names the next creature. What its
    // script has it do then is the game's like anything else of one out of
    // combat, and says nothing of the turn to come, which is judged as it
    // begins: by then the game may have it in combat.
    World w;
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock)
        {
            ++told;
            w.Get(id).perception = Perception{}; // in combat by now
            CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
        }
    };
    w.StartFight();
    Perception noticing = StoodDown();
    noticing.ofParty = true;
    w.Get(kGenlock).perception = noticing;
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 10 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.GetPhase() == Phase::TurnStart);
    CHECK(w.Order(kGenlock, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
    w.Finish(kGenlock);
    w.RunFor(200);
    CHECK(told == 1);
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    CHECK(Lines(w, "Genlock's turn ends: out of combat") == 0);
}

TEST_CASE("an ally that left the fight alive is no enemy that left it: the fight's end says its enemies fell",
          "[machine][turnedaway]")
{
    auto fight = StandardFight();
    fight.push_back(Make(0x300, Side::Ally, false, L"Soldier", 12.0f));
    World w{SlowToChoose(), fight};
    w.StartFight();
    // The soldier loses sight of everyone hostile to it and goes its way.
    w.Get(0x300).perception = StoodDown();
    w.Get(0x300).unfound = true;
    PassTurns(w, 600);
    REQUIRE(w.machine.Active());
    REQUIRE(std::ranges::count_if(
                w.log, [](const std::string &line) { return line.starts_with("Soldier leaves the fight"); }) == 1);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 600);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(std::ranges::count_if(
              w.log, [](const std::string &line) { return line.ends_with("rounds: no enemy left standing"); }) == 1);
}

TEST_CASE("what the mod itself gives the creature whose turn it is stays the mod's, though the game has it out of "
          "combat",
          "[machine][turnedaway]")
{
    // With a turn a bar of time an attack that does not fit is kept, its
    // creature walks to its target now, and the attack is given to it as its
    // next turn begins: both through the gate, as a script's. Neither is the
    // game's sending it anywhere, whatever its combat state by then.
    World w{WithABar()};
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && ++told == 1)
        {
            // Five seconds of swing in a bar of four, three metres off.
            CHECK(w.Order(id, CommandType::Attack, Source::Script, 3.0f, 5.0f) == Verdict::Hold);
            // And in that instant the game has it out of combat, an enemy
            // still in its perception: it has its turns.
            Perception noticing = StoodDown();
            noticing.ofParty = true;
            w.Get(id).perception = noticing;
        }
        else if (id != kGenlock)
        {
            (void)w.Wait(id, 2.0f);
        }
    };
    ToTheGenlocksTurn(w);
    REQUIRE(told == 1);
    // The walk to its kept attack's target, and then the attack itself as
    // its next turn begins.
    for (int i = 0; i < 3000 && w.released.empty(); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        if (w.Get(kGenlock).current == CommandType::MoveToObject)
        {
            w.Finish(kGenlock); // it is there
        }
        w.Frame();
    }
    CHECK(w.approaches == 1);
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(Lines(w, "Genlock's pending attack lands") == 1);
    CHECK(Lines(w, "is passed: out of combat by the game's own word") == 0);
    CHECK(Lines(w, "let through out of turn") == 0);
    CHECK(Lines(w, "Genlock's turn ends: out of combat") == 0);
}

TEST_CASE("one that left the fight, came back and fell in it is not told when it is over", "[machine][turnedaway]")
{
    World w{SlowToChoose()};
    GenlockLeaves(w);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    // Found fighting again before the round turns: back in the order, its
    // turns to come.
    w.Get(kGenlock).unfound = false;
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 100 && w.machine.Order().Find(kGenlock) != nullptr && w.machine.Order().Find(kGenlock)->down;
         ++i)
    {
        w.Frame();
    }
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    REQUIRE_FALSE(w.machine.Order().Find(kGenlock)->down);
    REQUIRE(std::ranges::count(w.log, std::string{"Genlock is fighting again"}) == 1);
    ToldIdle told{w};
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 600);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who.empty());
    // And the fight's end says they fell: the one that had left came back.
    CHECK(std::ranges::count_if(
              w.log, [](const std::string &line) { return line.ends_with("rounds: no enemy left standing"); }) == 1);
}

TEST_CASE("a fight dropped with the world owes nobody a telling in the next", "[machine][turnedaway]")
{
    World w = WithUnfound();
    w.StartFight();
    CHECK(w.Order(kSpirit, CommandType::Attack, Source::Script) == Verdict::Block);
    w.applied.clear();
    w.mode = Mode::Loading; // an area's transition: everyone is gone with the world
    w.Frame();
    REQUIRE_FALSE(w.machine.Active());
    CHECK_FALSE(w.Did(EffectKind::PokeAi)); // nobody is there to tell
    w.mode = Mode::Explore;
    w.Frame();
    // Another fight, in which the gate turns nobody away.
    w.StartFight();
    REQUIRE(w.machine.Active());
    ToldIdle told{w};
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(told.who.empty());
}

TEST_CASE("an enemy the scan finds and loses by turns is in the fight all along, while the engine's perception holds",
          "[machine][engaged]")
{
    // The user's eight fights (2026-09-30), read off Eluvian's bus: a guard
    // and a soldier "up again" every three seconds for ten minutes, never
    // having fallen, losing the turns that came while they were gone. Who
    // was in a fight then went by the scan's finding alone, and a wait of
    // four seconds was put over it. Since issue 29 one of the fight that the
    // scan does not find is in it while the engine's perception keeps it
    // there, which between two that are fighting it does: the finding may
    // come and go, and nothing is said or lost.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    for (int flap = 0; flap < 6; ++flap)
    {
        w.Get(kGenlock).fighting = false;
        PassTurns(w, 100); // 1.6 s, several scans
        REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
        CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
        w.Get(kGenlock).fighting = true;
        PassTurns(w, 100);
    }
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::none_of(
        events, [](const TurnEvent &e) { return e.kind == EventKind::UpAgain || e.kind == EventKind::Gone; }));
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("leaves the fight") != std::string::npos; }));
    // Neither found nor perceived, it has left with that look, and stays
    // out, alive as it is: its health does not bring it back.
    w.Get(kGenlock).fighting = false;
    w.Get(kGenlock).perception = Unperceived();
    for (int i = 0; i < 1500; ++i) // 24 s
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
        if (i > 100)
        {
            // Down in the order, or dropped from it with the next round.
            const Combatant *genlock = w.machine.Order().Find(kGenlock);
            REQUIRE((genlock == nullptr || genlock->down));
            REQUIRE(w.Current() != kGenlock);
        }
    }
    CHECK(std::ranges::count_if(events, [](const TurnEvent &e) {
              return e.kind == EventKind::Gone && e.who == kGenlock && e.detail == "no longer fighting";
          }) == 1);
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::UpAgain; }));
    // Fighting again: back in, said once, as one that joins.
    w.Get(kGenlock).fighting = true;
    PassTurns(w, 200);
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::count_if(
              events, [](const TurnEvent &e) { return e.kind == EventKind::Joined && e.who == kGenlock; }) == 1);
}

TEST_CASE("a creature outside the fight trying a command every frame is looked at twice a second", "[machine][engaged]")
{
    World w = WithOutsider();
    w.StartFight();
    REQUIRE(w.Current() == kHero); // the player thinks; nothing else asks for a scan
    w.RunFor(1000);
    const int before = w.scans;
    for (int i = 0; i < 125; ++i) // 2 s
    {
        CHECK(w.Order(0x202, CommandType::Attack, Source::Script) == Verdict::Block);
        w.Frame();
    }
    CHECK(w.scans - before >= 3);
    CHECK(w.scans - before <= 6);
}

TEST_CASE("the game's repeat of an order just taken is refused without a word", "[machine]")
{
    // The user, 2026-09-30: "No action left" over Eleanor, told to attack
    // and attacking. Her attack came a second time 49 ms after the first.
    // (Half a meter to its target: a swing where she stands. With more to
    // walk, a second standing still is an attack that cannot get there.)
    // A swing that takes the whole of her bar: with time left the repeat
    // would be one more swing, and let through.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.5f, 4.0f) == Verdict::Allow);
    w.Frame();
    w.Frame();
    w.Frame(); // 48 ms
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.5f) == Verdict::Block);
    w.Frame();
    CHECK_FALSE(w.Said(L"No action left"));
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::Refused; }));
    // Another order, or the same one a moment later, is the player's: said.
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"No action left"));
    w.applied.clear();
    events.clear();
    w.RunFor(1500); // a second click, long after
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.5f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"No action left"));
    CHECK(std::ranges::count_if(events, [](const TurnEvent &e) { return e.kind == EventKind::Refused; }) == 1);
}

TEST_CASE("a held cooldown's sweep shows what is left of it over what it was held for", "[machine][cooldowns]")
{
    // The user, 2026-09-30: with 2 of 3 turns left to wait, a third
    // recovered. 12 s is three turns of its user's clock at 4 s a turn, and
    // the talent is the last thing of its turn (a swing before it): none of
    // its 12 s has passed as that turn ends.
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 12.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    // The sweep as the game layer was last told to draw it, in thousandths:
    // what is left over the total. What is left is the hold's own hour,
    // whatever its creature's clock has left of it, and the total as many
    // times that as the cooldown is of what is left of it.
    const auto shown = [&]() {
        std::int32_t share = -1;
        for (const Effect &e : w.applied)
        {
            if (e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kShieldBash)
            {
                CHECK((e.b == 0 || e.b == TurnMachine::kCooldownHoldSeconds));
                share = e.b == 0 ? 0 : static_cast<std::int32_t>(1000ll * e.b / e.c);
            }
        }
        return share;
    };
    // Held as the engine set it, its command ending: all of its 12 s.
    REQUIRE(w.Held(kHero, kShieldBash));
    CHECK(w.Held(kHero, kShieldBash).value_or(CooldownView{}).leftMs == 12000);
    CHECK(w.Held(kHero, kShieldBash).value_or(CooldownView{}).ofMs == 12000);
    CHECK(shown() == 1000); // none of it recovered
    UntilHerosTurn(w);      // her bar spent, that turn ended by itself
    w.applied.clear();
    UntilHerosTurn(w);
    // 2 of 3 turns left: a third recovered.
    CHECK(w.Held(kHero, kShieldBash).value_or(CooldownView{}).leftMs == 8000);
    CHECK(shown() == 666);
    REQUIRE(w.Cooling(kHero, kShieldBash) != nullptr);
    CHECK(w.Cooling(kHero, kShieldBash)->remaining > 3500.0f); // and an hour of the wall to wait, as ever
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(shown() == 333);
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(shown() == 0); // ready
    CHECK_FALSE(w.Held(kHero, kShieldBash));
    CHECK(w.Cooling(kHero, kShieldBash) == nullptr);
}

TEST_CASE("a fight's end gives a held cooldown back in seconds, left and in all", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 30.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    UntilHerosTurn(w); // 28 of its 30 s left: the rest of the turn it was used in has passed
    w.applied.clear();
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    (void)w.machine.OnEndTurn();
    w.RunFor(500);
    REQUIRE_FALSE(w.machine.Active());
    // And the turn she ended: 24 s left.
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kShieldBash && e.b == 24 && e.c == 30;
    }));
}

TEST_CASE("an AI whose action failed without acting chooses again, once", "[machine]")
{
    // The user, 2026-09-30: an ally guard stood through a fight. His attack
    // failed for want of room at his target each turn, his action was given
    // back, and his turn ended 48 ms later all the same.
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn(); // the hero passes
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
    };
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    REQUIRE(pokes == 1);
    // No room at its target: the command ends at once, nothing done.
    CHECK(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.Frame();
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kGenlock); // its turn goes on
    CHECK(pokes == 2);              // told again: it chose again
    // The second fails too, and is given back too: no third try, the turn
    // ends.
    CHECK(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.RunFor(1000);
    CHECK(w.Current() != kGenlock);
    CHECK(pokes == 2);
}

TEST_CASE("the same refusal again and again is one event a second", "[machine]")
{
    // The user's eight fights: 1,097 refusals on Eluvian's bus, up to 64 in
    // a second.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::Refused)
        {
            events.push_back(e);
        }
    });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (int i = 0; i < 60; ++i) // a follower's move, asked for every frame: 0.96 s
    {
        CHECK(w.Order(kCompanion, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
        w.Frame();
    }
    CHECK(events.size() == 1);
    // Another refusal is its own event, and so is the first again after it.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block);
    CHECK(events.size() == 2);
    CHECK(w.Order(kCompanion, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
    CHECK(events.size() == 3);
    // And the same one a second on.
    w.RunFor(1100);
    CHECK(w.Order(kCompanion, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
    CHECK(events.size() == 4);
}

TEST_CASE("with the movement spent, a talent on something in reach is used, not stopped", "[machine]")
{
    // The seventh play test: Eleanor, 0.0 m left, a soldier 2.0 m away
    // (reach 3.5): her Shield Bash was cleared 9 ms after it was let
    // through, "out of movement", the action gone and nothing struck.
    // Her free meter and two seconds walked, 11 m, and the talent takes the
    // two she has left: nothing more to walk as she uses it.
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 11.0f) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 11 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    w.Get(kHero).moving = false;
    w.Finish(kHero);
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.View().timeLeftMs == 2000);
    w.applied.clear();
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().movementLeft == 0.0f);
    // It turns and steps into the blow: moving, a little.
    w.Get(kHero).moving = true;
    for (int i = 0; i < 10; ++i)
    {
        w.Get(kHero).position.x += 0.05f;
        w.Frame();
    }
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
    w.Get(kHero).moving = false;
    w.Finish(kHero);
    w.RunFor(1000);
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == 617;
    })); // it ran: held on cooldown
}

TEST_CASE("with the movement spent, an action that walks on is still stopped", "[machine]")
{
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.onPoke = [&](ObjectId id) { w.Order(id, CommandType::Attack, Source::Script); };
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.Get(kGenlock).moving = true;
    float walked = 0.0f;
    for (int i = 0; i < 300 && !w.Did(EffectKind::ClearCommands, kGenlock); ++i)
    {
        w.Get(kGenlock).position.x += 0.1f;
        walked += 0.1f;
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::ClearCommands, kGenlock));
    // Its 21 m (its swing's time gone to the walk), the overshoot, and no
    // more than a frame or two.
    CHECK(walked > 20.0f);
    CHECK(walked < 21.0f + TurnMachine::kOvershoot + 0.5f);
}

TEST_CASE("a door is not for someone else's turn", "[machine]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kCompanion, CommandType::UseObject, Source::Player) == Verdict::Block);
}

TEST_CASE("the walk to a door is movement: out of it, the walk is stopped", "[machine]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.applied.clear();
    w.Get(kHero).moving = true;
    float walked = 0.0f;
    for (int i = 0; i < 200 && !w.Did(EffectKind::ClearCommands, kHero); ++i)
    {
        w.Get(kHero).position.x += 0.1f;
        walked += 0.1f;
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::ClearCommands, kHero));
    // The door's half second, and the 3.5 s left walk 17.5 m after the free
    // meter.
    CHECK(walked > 17.5f);
    CHECK(walked < 18.5f + TurnMachine::kOvershoot + 0.5f);
}

TEST_CASE("an action on its own effect's time may outlast max_action_ms, not three times it", "[machine]")
{
    Settings s = Fixed();
    s.maxActionMs = 3000;
    World w{s};
    w.StartFight();
    (void)w.machine.OnEndTurn();
    bool poked = false;
    w.onPoke = [&](ObjectId id) {
        poked = true;
        CHECK(w.UseAbility(id, Source::Script, 90133, 60.0f) == Verdict::Allow);
    };
    for (int i = 0; i < 40 && !poked; ++i)
    {
        w.Frame();
    }
    REQUIRE(poked);
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    w.RunFor(5000); // past max_action_ms: its effect's time is running
    CHECK(w.Current() == kGenlock);
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
    w.RunFor(5000); // past three times it: stopped
    CHECK(w.Current() != kGenlock);
    CHECK(std::ranges::any_of(w.log,
                              [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
}

TEST_CASE("a door opened shows a room: who fights there joins as the order ends, not the turn", "[machine][engaged]")
{
    // The eighth play test: the room's spiders joined 2.2 s after the door
    // opened, when Lyna's turn ended.
    World w = WithOutsider();
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.RunFor(1000); // the player thinks: nobody asks for a scan
    REQUIRE(w.machine.Order().Find(0x202) == nullptr);
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.RunFor(300);
    w.Get(0x202).engaged = true; // the door is open: its room is seen
    w.Finish(kHero);
    w.RunFor(1500);
    REQUIRE(w.Current() == kHero); // still his turn
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.Order().Find(0x202) != nullptr);
    CHECK(
        std::ranges::any_of(events, [](const TurnEvent &e) { return e.kind == EventKind::Joined && e.who == 0x202; }));
}

TEST_CASE("the player's attack with a shut door in the way is refused, with why", "[machine]")
{
    // The ninth play test: told to attack through a shut door from 12 m,
    // the game's own answer left the creature standing at "use object".
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CommandRequest attack;
    attack.object = kHero;
    attack.type = CommandType::Attack;
    attack.source = Source::Player;
    attack.inCombat = true;
    attack.walk = 2.0f;
    attack.doorInTheWay = true;
    CHECK(w.Queue(attack) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"A door is in the way"));
    CHECK(w.machine.View().timeLeftMs == 4000);
    REQUIRE(!events.empty());
    CHECK(events.back().kind == EventKind::Refused);
    CHECK(events.back().detail == "A door is in the way");
    // The door opened, the same attack is taken.
    attack.doorInTheWay = false;
    CHECK(w.Queue(attack) == Verdict::Allow);
    // An AI's is not refused for it: the game opens doors for its own.
    World ai;
    ai.StartFight();
    (void)ai.machine.OnEndTurn();
    for (int i = 0; i < 20 && ai.Current() != kGenlock; ++i)
    {
        ai.Frame();
    }
    REQUIRE(ai.Current() == kGenlock);
    ai.Frame(); // its turn begins
    ai.Frame();
    attack.object = kGenlock;
    attack.source = Source::Script;
    attack.doorInTheWay = true;
    CHECK(ai.Queue(attack) == Verdict::Allow);
}

TEST_CASE("standing at a door's use without a step, a creature is stopped; the player's turn goes on", "[machine]")
{
    // The game's own answer to an attack through a shut door: the door's
    // use, the attack behind it, and the creature standing for good.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Allow);
    w.RunFor(100);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, "COMMAND_FAILED_PATH_ACTION_REQUIRED"));
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Script) == Verdict::Allow);
    const std::uint32_t before = w.machine.View().timeLeftMs;
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == before - 2000);
    w.Get(kHero).current = CommandType::UseObject; // the door first, the attack behind it
    w.applied.clear();
    w.RunFor(2000);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero)); // not yet
    w.RunFor(1000);
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.machine.View().timeLeftMs == before); // the attack never began
    w.RunFor(2000);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::ActionGivenBack && e.detail == "stood still at a use object";
    }));
}

TEST_CASE("a creature walking to a door, or swinging where it stands, is not taken for stuck", "[machine]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Walking: a step every frame.
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.applied.clear();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 250; ++i) // 4 s, 4 m
    {
        w.Get(kHero).position.x += 0.016f;
        w.Frame();
    }
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
    w.Get(kHero).moving = false;
    w.Finish(kHero);
    w.RunFor(1000);
    REQUIRE(w.Current() == kHero);
    // Swinging: four seconds on the spot at an attack.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.applied.clear();
    w.RunFor(4000);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
}

TEST_CASE("an AI standing at a move it does not take is stopped, and chooses again with its bar whole", "[machine]")
{
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn();
    int told = 0;
    w.onPoke = [&](ObjectId id) {
        if (++told == 1)
        {
            CHECK(w.Order(id, CommandType::MoveToObject, Source::Script) == Verdict::Allow);
            return;
        }
        // Told again, nothing of its bar spent: this time it passes.
        (void)w.Order(id, CommandType::Wait, Source::Script);
    };
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.RunFor(2300);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock)); // not yet
    w.RunFor(1700);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(told == 2);
    CHECK(w.Current() != kGenlock);
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
}

TEST_CASE("a fight is said to begin when there is one, each time, and to end once for each", "[machine][engaged]")
{
    // eluvian-agent's staged wolf: the game said combat with no enemy
    // fighting the party, and "began" came with no fight and no "over".
    auto fight = StandardFight();
    for (Creature &c : fight)
    {
        c.engaged = c.side == Side::Party;
    }
    World w(Fixed(), fight);
    std::vector<EventKind> kinds;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::FightBegan || e.kind == EventKind::FightOver)
        {
            kinds.push_back(e.kind);
        }
    });
    w.StartFight();
    w.RunFor(2000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(kinds.empty()); // combat, and no fight: nothing said
    // An enemy comes to fight the party: begun, in the same combat.
    w.Get(kHurlock).engaged = true;
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    CHECK(kinds == std::vector<EventKind>{EventKind::FightBegan});
    w.Get(kHurlock).down = true;
    for (int i = 0; i < 200 && w.machine.Active(); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE_FALSE(w.machine.Active());
    CHECK(kinds == std::vector<EventKind>{EventKind::FightBegan, EventKind::FightOver});
    // And the other, still in the same combat: begun again, and said.
    w.Get(kGenlock).engaged = true;
    w.RunFor(3000);
    REQUIRE(w.machine.Active());
    CHECK(kinds == std::vector<EventKind>{EventKind::FightBegan, EventKind::FightOver, EventKind::FightBegan});
}

namespace
{
// One of the player's orders that costs nothing, run to its end: the
// machine looks again at who is fighting as it settles.
void UseSomething(World &w, ObjectId id)
{
    REQUIRE(w.Order(id, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(id);
    w.RunFor(800);
}
} // namespace

TEST_CASE("one that left the fight and is back before the round is out is said to be fighting again",
          "[machine][engaged]")
{
    // Within one long turn of the player's, so the order has not dropped it.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kGenlock).fighting = false;
    w.Get(kGenlock).perception = Unperceived(); // and out of perception: else it would stay (issue 29)
    UseSomething(w, kHero);                     // missed, from now
    w.RunFor(4500);
    UseSomething(w, kHero); // missed for more than four seconds: it has left
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::count_if(
              events, [](const TurnEvent &e) { return e.kind == EventKind::Gone && e.who == kGenlock; }) == 1);
    w.RunFor(1000); // alive as it is, its health does not bring it back
    CHECK(w.machine.Order().Find(kGenlock)->down);
    w.Get(kGenlock).fighting = true;
    UseSomething(w, kHero);
    REQUIRE(w.Current() == kHero);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::count_if(events, [](const TurnEvent &e) {
              return e.kind == EventKind::UpAgain && e.who == kGenlock && e.detail == "fighting again";
          }) == 1);
}

TEST_CASE("a creature that begins to fight near, but not the party, mid-fight is not in the fight",
          "[machine][engaged]")
{
    auto fight = StandardFight();
    Creature far = Make(0x202, Side::Enemy, false, L"Shriek", 12.0f);
    far.engaged = false;
    far.fighting = false; // not fighting anyone yet
    fight.push_back(far);
    World w(Fixed(), fight);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(0x202).fighting = true; // a guard has found it, rooms away
    UseSomething(w, kHero);
    UseSomething(w, kHero);
    CHECK(w.machine.Order().Find(0x202) == nullptr);
    CHECK(Lines(w, "Shriek") == 0);
}

TEST_CASE("the same seed gives the same initiative", "[machine]")
{
    // turnbased_seed, for a test run that repeats: default settings, the
    // dice real.
    const auto order = [](std::uint64_t seed) {
        World w{Settings{}};
        w.machine.Reseed(seed);
        w.StartFight();
        std::vector<ObjectId> ids;
        for (const Combatant &c : w.machine.Order().All())
        {
            ids.push_back(c.id);
        }
        return ids;
    };
    CHECK(order(42) == order(42));
    CHECK(order(7) == order(7));
    // Twenty seeds do not all roll the one order.
    std::vector<std::vector<ObjectId>> seen;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        seen.push_back(order(seed));
    }
    CHECK(std::ranges::any_of(seen, [&](const auto &ids) { return ids != seen.front(); }));
}

namespace
{
constexpr std::int32_t kBlizzard = 13000;

// An ability that takes `seconds`, as the game layer read it of the engine:
// with less than that left of its user's bar it is kept for the next turn
// (PayAction). A talent, not a spell that conjures, which the engine holds
// itself (test_time.cpp).
Verdict Use(World &w, ObjectId id, Source source, std::int32_t ability, float seconds)
{
    CommandRequest request;
    request.object = id;
    request.type = CommandType::UseAbility;
    request.source = source;
    request.inCombat = w.InCombat(id);
    request.ability = ability;
    w.cooldownOf[ability] = 20.0f;
    request.actionSeconds = seconds;
    return w.Queue(request);
}

bool Released(const World &w, ObjectId id)
{
    return std::ranges::any_of(w.applied,
                               [&](const Effect &e) { return e.kind == EffectKind::ReleaseCast && e.target == id; });
}

// Everyone's turns pass until the hero's comes again.
void UntilHeroAgain(World &w)
{
    for (int i = 0; i < 4000 && w.Current() == kHero; ++i)
    {
        w.Frame();
    }
    for (int i = 0; i < 4000 && w.Current() != kHero; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == kHero);
    w.Frame();
    w.Frame();
}
} // namespace

TEST_CASE("an ability longer than what is left is kept on one turn and used at the start of the next, held on "
          "cooldown only then",
          "[machine][charge]")
{
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Five seconds of it on a bar of four: a second is owed.
    CHECK(Use(w, kHero, Source::Player, kBlizzard, 5.0f) == Verdict::Hold);
    CHECK(w.machine.View().timeLeftMs == 0);
    CHECK_FALSE(w.Get(kHero).busy); // not queued: kept
    CHECK(w.heldCasts.contains(kHero));
    w.Frame();
    w.Frame();
    CHECK(w.Said(L"Lands next turn"));
    w.RunFor(900);
    CHECK(w.Current() != kHero); // the turn is over: it does nothing else
    CHECK_FALSE(Released(w, kHero));
    // No cooldown yet: it has not been used.
    CHECK(std::ranges::none_of(w.applied, [](const Effect &e) { return e.kind == EffectKind::Cooldown; }));
    UntilHeroAgain(w);
    // Used as his turn begins.
    CHECK(Released(w, kHero));
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.Get(kHero).busy);
    CHECK(w.machine.View().timeLeftMs == 3000); // the second it owed, and no more
    w.RunFor(300);
    w.Finish(kHero);
    w.RunFor(1500);
    // And his turn follows it: the player's to play.
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 3000);
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kBlizzard;
    })); // used, it is on cooldown
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    const auto count = [&](EventKind kind) {
        return std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == kHero; });
    };
    CHECK(count(EventKind::ActionPending) == 1);
    CHECK(count(EventKind::ActionLanded) == 1);
    CHECK(count(EventKind::ActionDropped) == 0);
}

TEST_CASE("a turn opened by a kept action is the player's though its own AI waits in a loop", "[machine][charge]")
{
    // Issue 10: Lyna's kept shot landed as her turn began and felled the
    // fight's last enemy. Not yet selected, she was a follower whose full AI
    // ran as each of her commands ended, and with no enemy to choose it
    // queued a wait of a second, and another as that one ended. Each was let
    // through, so the turn stood at executing for eleven minutes: never
    // handed over, never waiting on an order.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(Use(w, kHero, Source::Player, kBlizzard, 5.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    UntilHeroAgain(w);
    REQUIRE(Released(w, kHero));
    REQUIRE(w.Get(kHero).busy);
    w.RunFor(300);
    w.Finish(kHero);
    // Its AI, as the scripts run it: a wait whenever it has no command, each
    // one over after a few frames.
    int asked = 0;
    int through = 0;
    int at = 0;
    for (int frame = 0; frame < 400 && w.machine.GetPhase() != Phase::AwaitingOrder; ++frame)
    {
        if (!w.Get(kHero).busy)
        {
            ++asked;
            at = 0;
            through += w.Order(kHero, CommandType::Wait, Source::Script) == Verdict::Allow ? 1 : 0;
        }
        else if (++at >= 6)
        {
            w.Finish(kHero);
        }
        w.Frame();
    }
    CHECK(asked > 0);
    CHECK(through == 0); // its own AI's, held: the turn is the player's
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // And the player's order goes through as on any turn.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
}

TEST_CASE("an action that owes more than a turn has that turn spent on it whole, and is used on the one after",
          "[machine][charge]")
{
    World w;
    w.StartFight();
    // Ten seconds of it on a bar of four: six are owed, a whole turn and half
    // of the next.
    CHECK(Use(w, kHero, Source::Player, kBlizzard, 10.0f) == Verdict::Hold);
    w.RunFor(900);
    REQUIRE(w.Current() != kHero);
    w.applied.clear();
    // The second turn: spent on it, nothing for the player to do.
    for (int i = 0; i < 8000 && w.machine.Order().Round() < 3; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            REQUIRE(w.Current() != kHero); // his turn never waits on an order
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == 3);
    CHECK_FALSE(Released(w, kHero)); // not on the second
    CHECK(w.Said(L"Casting"));
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("round 2: Hero's turn is spent on its pending use ability (2.00 s more)") != std::string::npos;
    }));
    // The third: used, with what its bar has left after the two seconds.
    for (int i = 0; i < 20 && !Released(w, kHero); ++i)
    {
        w.Frame();
    }
    CHECK(Released(w, kHero));
    CHECK(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs == 2000);
}

TEST_CASE("one held, fallen or gone loses the action kept for it: nothing used, nothing on cooldown",
          "[machine][charge]")
{
    const auto interrupted = [](const std::function<void(Creature &)> &how, const char *why, bool said) {
        World w;
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        REQUIRE(Use(w, kHero, Source::Player, kBlizzard, 5.0f) == Verdict::Hold);
        w.Frame();
        w.Frame();
        how(w.Get(kHero));
        w.RunFor(700); // conditions are read twice a second, and as each turn begins
        CAPTURE(why);
        CHECK(w.machine.State().charging.empty());
        CHECK_FALSE(w.heldCasts.contains(kHero)); // the game layer forgets it
        // Said over one that is held; not over one that fell or is gone.
        CHECK(w.Said(L"Action lost") == said);
        CHECK(std::ranges::count_if(events, [&](const TurnEvent &e) {
                  return e.kind == EventKind::ActionDropped && e.who == kHero && e.detail == why &&
                         e.value == kBlizzard;
              }) == 1);
        // Never cast after.
        for (int i = 0; i < 3000; ++i)
        {
            if (w.machine.GetPhase() == Phase::AwaitingOrder)
            {
                (void)w.machine.OnEndTurn();
            }
            w.Frame();
        }
        CHECK_FALSE(Released(w, kHero));
        CHECK(std::ranges::none_of(w.applied,
                                   [](const Effect &e) { return e.kind == EffectKind::Cooldown && e.a == kBlizzard; }));
    };
    interrupted([](Creature &c) { c.condition = Condition::Disabled; }, "held", true);
    interrupted([](Creature &c) { c.down = true; }, "fell", false);
    interrupted([](Creature &c) { c.exists = false; }, "gone", false);
}

TEST_CASE("an enemy's kept action is used as its next turn begins, and its turn follows", "[machine][charge]")
{
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn();
    int pokes = 0; // the genlock's
    w.onPoke = [&](ObjectId id) {
        if (id != kGenlock)
        {
            (void)w.Order(id, CommandType::Attack, Source::Script);
            return;
        }
        ++pokes;
        if (pokes == 1)
        {
            // Six seconds of it on a bar of four: two are owed.
            CHECK(Use(w, id, Source::Script, kBlizzard, 6.0f) == Verdict::Hold);
        }
        else
        {
            CHECK(w.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow);
        }
    };
    for (int i = 0; i < 40 && pokes == 0; ++i)
    {
        w.Frame();
    }
    REQUIRE(pokes == 1);
    w.Frame();
    w.Frame();
    CHECK(w.Current() != kGenlock); // its turn ended on what it kept
    CHECK(w.machine.State().charging.size() == 1);
    // Round 2: the genlock's turn begins with what it kept, then its AI is
    // told, with the two seconds its bar has left.
    for (int i = 0; i < 4000 && !Released(w, kGenlock); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(Released(w, kGenlock));
    CHECK(pokes == 1); // not told while it is given
    w.RunFor(300);
    w.Finish(kGenlock);
    w.RunFor(1000);
    CHECK(pokes == 2); // its turn after it
}

TEST_CASE("the fight over, an action kept is lost", "[machine][charge]")
{
    World w;
    w.StartFight();
    REQUIRE(Use(w, kHero, Source::Player, kBlizzard, 5.0f) == Verdict::Hold);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(2000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(w.machine.State().charging.empty());
    CHECK_FALSE(w.heldCasts.contains(kHero));
}

TEST_CASE("an enemy that goes on by itself after its kept action, its bar spent, is not told its turn has come",
          "[machine][charge]")
{
    // The twelfth play test: the spider's own attack followed its cast, its
    // action spent; told then, it chose nothing for a second and a half.
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn();
    int pokes = 0; // the genlock's
    w.onPoke = [&](ObjectId id) {
        if (id != kGenlock)
        {
            (void)w.Order(id, CommandType::Attack, Source::Script);
            return;
        }
        ++pokes;
        if (pokes == 1)
        {
            // Six seconds of it on a bar of four: two are owed.
            CHECK(Use(w, id, Source::Script, kBlizzard, 6.0f) == Verdict::Hold);
        }
    };
    for (int i = 0; i < 4000 && !Released(w, kGenlock); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(Released(w, kGenlock));
    REQUIRE(pokes == 1);
    w.RunFor(300);
    // What it kept ends, and its own script attacks at once: the two seconds
    // its bar had left.
    w.Finish(kGenlock);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 0);
    w.RunFor(300);
    w.Finish(kGenlock);
    w.RunFor(1200);
    CHECK(pokes == 1);              // not told: it had nothing left to choose with
    CHECK(w.Current() != kGenlock); // and its turn is over, without the wait for a choice
    CHECK(std::ranges::none_of(w.log, [](const std::string &line) {
        return line.find("Genlock's turn ends: chose nothing") != std::string::npos;
    }));
}

TEST_CASE("held by an effect on the fight's first turn, a creature loses it at a turn's worth, not max_action_ms",
          "[machine]")
{
    // A spider that drops from the ceiling as the fight begins and has the
    // first turn: the effect's command is seen a frame after the turn began.
    std::vector<Creature> fight = StandardFight();
    fight.at(1).dexterity = 30.0f; // the genlock first
    World w(Fixed(), fight);
    int pokes = 0;
    w.onPoke = [&](ObjectId) { ++pokes; }; // its AI is asked, and has nothing to say: it is held
    w.StartFight();
    // The turn has begun, and its AI been asked, before the effect's command
    // is seen: it was not known for held as the turn began.
    for (int i = 0; i < 10 && pokes == 0; ++i)
    {
        w.Frame();
    }
    REQUIRE(pokes == 1);
    REQUIRE(w.Current() == kGenlock);
    // Busy first at something of the engine's that is not an effect's (in
    // play nothing was logged for it), then at the effect's command.
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::Wait;
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    REQUIRE_FALSE(w.machine.State().heldByEffect);
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    int frames = 0;
    for (; frames < 2000 && w.Current() == kGenlock; ++frames)
    {
        w.Frame();
    }
    CHECK(w.Current() != kGenlock);
    CHECK(frames * 16 >= 3900); // a turn's worth of the hold: its bar's four seconds
    CHECK(frames * 16 < 5000);  // not max_action_ms' twelve seconds
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Genlock is held by an effect's command past its bar (4.0") != std::string::npos;
    }));
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
    // Its own action's effect is still its own: a command of its own let
    // through, and then the effect's, is not a hold on its turn.
    World own(Fixed(), fight);
    own.onPoke = [&](ObjectId id) { CHECK(own.UseAbility(id, Source::Script, 90133, 60.0f) == Verdict::Allow); };
    own.StartFight();
    REQUIRE(own.Current() == kGenlock);
    own.RunFor(200);
    own.Get(kGenlock).busy = true;
    own.Get(kGenlock).current = CommandType::ExecuteEffect;
    own.RunFor(7000);
    CHECK(own.Current() == kGenlock);
    CHECK_FALSE(own.machine.State().heldByEffect);
    CHECK(std::ranges::none_of(own.log, [](const std::string &line) {
        return line.find("is held by an effect's command") != std::string::npos;
    }));
}

namespace
{
// A combatant's card on the turn bar, as the view has it now.
CardView CardOf(const World &w, ObjectId id)
{
    const TurnView view = w.machine.View();
    const auto card = std::ranges::find(view.upcoming, id, &CardView::id);
    REQUIRE(card != view.upcoming.end());
    return *card;
}
} // namespace

TEST_CASE("everyone is looked at as an action ends: the cards follow a heal at once", "[machine][status]")
{
    // Issue 23, its second half (the user, 2026-10-03: "Yeah we should do
    // that, reads should be pretty cheap"). A poultice drunk showed on its
    // card only at the next look, up to half a second on, and a script that
    // read the state right after a drink saw the old health and drank again,
    // three times in one turn.
    World w;
    w.Get(kHero).health = 0.4f;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(CardOf(w, kHero).health == 0.4f);
    const int looks = w.statusReads;
    // She drinks, and it heals her as she does.
    CHECK(w.Order(kHero, CommandType::UseItem, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).health = 0.9f;
    w.Finish(kHero);
    // Two frames idle say her command is over; nobody has looked yet.
    w.Frame();
    w.Frame();
    CHECK(w.statusReads == looks);
    CHECK(CardOf(w, kHero).health == 0.4f);
    // The next step looks: 64 ms after the turn's own look, not 500.
    w.Frame();
    CHECK(w.statusReads == looks + 1);
    CHECK(CardOf(w, kHero).health == 0.9f);
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Hero's health on its card: 40% to 90%") != std::string::npos;
    }));
}

TEST_CASE("a turn waits again on a look taken as the world has settled: what landed late is on the cards",
          "[machine][status]")
{
    // An arrow still in flight as its shot's command ends lands while the
    // world settles: the look at the command's end is before it.
    World w;
    std::vector<float> asItWaits; // the genlock's health on its card as the turn starts waiting again
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::AwaitingOrder && e.detail == "after its action")
        {
            asItWaits.push_back(CardOf(w, kGenlock).health);
        }
    });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Finish(kHero);
    w.RunFor(64); // her command over, and its look taken: the genlock whole
    REQUIRE(w.machine.GetPhase() == Phase::Settling);
    CHECK(CardOf(w, kGenlock).health == 1.0f);
    const int looks = w.statusReads;
    w.Get(kGenlock).health = 0.3f; // the arrow lands
    w.RunFor(600);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(asItWaits.size() == 1);
    CHECK(asItWaits.front() == 0.3f);
    // One look for it, as the settling ended; not one a frame while it ran.
    CHECK(w.statusReads == looks + 1);
}

TEST_CASE("a scan of who is fighting leaves the cards' mana and stamina as the statuses read them", "[machine][status]")
{
    // A scan does not read them (the game layer's Describe), and wrote its
    // blank over what the statuses had read: full, and nobody's mana, from
    // each turn's start and each time a turn waited again after an action,
    // until the next look.
    World w;
    w.Get(kGenlock).energy = 0.4f;
    w.Get(kGenlock).mana = true; // an emissary
    w.Get(kHero).energy = 0.7f;
    w.StartFight();
    // The first turn has begun: its scan came in the step that looked at
    // everyone, after the look.
    REQUIRE(w.Current() == kHero);
    CHECK(CardOf(w, kGenlock).energy == 0.4f);
    CHECK(CardOf(w, kGenlock).mana);
    CHECK(CardOf(w, kHero).energy == 0.7f);
    CHECK_FALSE(CardOf(w, kHero).mana);
    // A swing costs her stamina; the turn waits again, which asks for a scan
    // (who her action showed the party), and no look follows it for half a
    // second.
    const int scans = w.scans;
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).energy = 0.5f;
    w.Finish(kHero);
    w.RunFor(600);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.scans > scans);
    CHECK(CardOf(w, kHero).energy == 0.5f);
    CHECK(CardOf(w, kGenlock).energy == 0.4f);
    CHECK(CardOf(w, kGenlock).mana);
}
