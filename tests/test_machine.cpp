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

    // The world stops, everyone's real-time commands are dropped, and the
    // highest initiative goes first: the hero, waiting for an order.
    CHECK(w.paused);
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
    // The swing done, the world settles, then pauses for the next order.
    w.RunFor(500);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);

    // Its auto-attack carrying on is a second action: no.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Script) == Verdict::Block);
    // It may still move, until its movement runs out.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 12 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.Said(L"Out of movement"));
    CHECK(w.Get(kHero).position.x < 11.0f);

    // Action and movement spent: the turn ends by itself, and the genlock's
    // AI is told to choose.
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
    // Its command done, it chooses again (COMMAND_COMPLETE): a second attack is held.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    w.RunFor(600);

    // Alistair, the player's too.
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);
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
    World w; // 9 m to move
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 12.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Too far"));
    CHECK(w.machine.View().actionsLeft == 1);
    // Past the movement left by a little: refused too, since the walk is a
    // straight line and the path is never shorter (the third play test: 9.3
    // m with 9.0 left, let through and cut short). Within it: let through.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 9.3f) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 9.0f) == Verdict::Allow);
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

TEST_CASE("once out of movement, the player can only strike what is in reach", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 12 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 1.0f) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
}

TEST_CASE("stopped short on the way to an attack, a party member keeps its action", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 15 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    w.RunFor(500);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().actionsLeft == 1);
    CHECK(w.machine.View().movementLeft == 0.0f);
    // It may not walk, but may still attack what is in reach.
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    // With both spent, the turn ends by itself.
    w.Finish(kHero);
    w.RunFor(500);
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an action stopped short is given back once a turn, not twice", "[machine]")
{
    World w;
    w.StartFight();
    // Attacks whose walk the game layer could not tell (an order off the
    // scripts' thread): each walks until it is stopped.
    const auto walkUntilStopped = [&] {
        w.Frame();
        w.Get(kHero).moving = true;
        for (int i = 0; i < 15 && w.Get(kHero).moving; ++i)
        {
            w.Get(kHero).position.x += 1.0f;
            w.Frame();
        }
        w.RunFor(500);
    };
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    walkUntilStopped();
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().actionsLeft == 1); // given back
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    walkUntilStopped();
    // Stopped short again: spent this time, and with nothing left the turn
    // ends by itself.
    CHECK(w.Current() == kGenlock);
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
    w.Get(kGenlock).moving = true;
    for (int i = 0; i < 15 && w.Current() == kGenlock; ++i)
    {
        w.Get(kGenlock).position.x += 1.0f;
        w.Frame();
    }
    CHECK(w.Current() == kCompanion);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
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

TEST_CASE("an action that never ends is stopped after max_action_ms", "[machine]")
{
    Settings settings = Fixed();
    settings.maxActionMs = 2000;
    World w{settings};
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::UseAbility, Source::Player) == Verdict::Allow);
    w.RunFor(1900);
    CHECK(w.Current() == kHero);
    w.RunFor(200);
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.Current() == kGenlock);
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
        CHECK(w.Order(kHero, CommandType::UseAbility, Source::Player) == Verdict::Allow);
        w.applied.clear(); // the fight's beginning cleared everyone's commands
        w.RunFor(1900);
        CHECK(w.Current() == kHero);
        CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero));
        w.RunFor(200);
        CHECK(w.Did(EffectKind::ClearCommands, kHero));
        CHECK(w.Current() == kGenlock);
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

TEST_CASE("the player's commands are not limited in number, an AI's are", "[machine]")
{
    Settings settings = Fixed();
    settings.maxCommandsPerTurn = 2;
    World w{settings};
    w.StartFight();
    for (int i = 0; i < 5; ++i)
    {
        CHECK(w.Order(kHero, CommandType::Drive, Source::Player) == Verdict::Allow);
        w.Finish(kHero);
    }
    w.RunFor(500); // settled, awaiting the next order
    CHECK(w.machine.OnEndTurn());
    int allowed = 0;
    w.onPoke = [&](ObjectId id) {
        allowed += w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow ? 1 : 0;
        w.Finish(id);
        allowed += w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow ? 1 : 0;
        w.Finish(id);
        allowed += w.Order(id, CommandType::SwitchWeaponSets, Source::Script) == Verdict::Allow ? 1 : 0;
    };
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kGenlock);
    CHECK(allowed == 2);
    w.RunFor(100);
    CHECK(w.Current() == kCompanion); // stopped there: its turn is over
}

namespace
{
// The fallback for an engine that queues orders only when the game runs,
// off by default since the game was seen queuing them while paused.
Settings WithOrderRun()
{
    Settings s = Fixed();
    s.orderRunMs = 500;
    return s;
}
} // namespace

TEST_CASE("an order that queues nothing while paused gets a moment of running time", "[machine]")
{
    World w(WithOrderRun());
    w.StartFight();
    // A command straight away: no dispatching.
    w.machine.OnPlayerOrder("move");
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.RunFor(200);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    w.Finish(kHero);
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);

    // An order and nothing: the game runs until the engine queues it.
    w.machine.OnPlayerOrder("attack");
    w.RunFor(50);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    w.RunFor(100);
    CHECK(w.machine.GetPhase() == Phase::Dispatching);
    CHECK_FALSE(w.paused);
    // Nobody else moves meanwhile.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::Executing);
}

TEST_CASE("an order that never queues anything goes back to waiting, paused", "[machine]")
{
    World w(WithOrderRun());
    w.StartFight();
    w.machine.OnPlayerOrder("use object");
    w.RunFor(200);
    CHECK(w.machine.GetPhase() == Phase::Dispatching);
    w.RunFor(600);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);
    CHECK(w.Current() == kHero);
}

TEST_CASE("by default an order that queues nothing leaves the world still", "[machine]")
{
    World w;
    w.StartFight();
    w.applied.clear();
    // An ability picked on the quickbar, its target not yet clicked.
    w.machine.OnPlayerOrder("ability");
    w.RunFor(1000);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);
    CHECK_FALSE(w.Did(EffectKind::Unpause));
    // The target clicked: the command, queued while paused.
    CHECK(w.Order(kHero, CommandType::UseAbility, Source::Player) == Verdict::Allow);
}

TEST_CASE("an order the gate refuses does not run the world", "[machine]")
{
    // Seen in play (2026-09-30): every click after the action was spent ran
    // the world for half a second.
    World w(WithOrderRun());
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.Current() == kHero);
    w.applied.clear();
    w.machine.OnPlayerOrder("attack");
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Block);
    w.RunFor(1000);
    CHECK(w.Said(L"No action left"));
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);
    CHECK_FALSE(w.Did(EffectKind::Unpause));
}

TEST_CASE("a refusal while an order runs the world stops it", "[machine]")
{
    World w(WithOrderRun());
    w.StartFight();
    w.machine.OnPlayerOrder("attack");
    w.RunFor(150);
    REQUIRE(w.machine.GetPhase() == Phase::Dispatching);
    CHECK_FALSE(w.paused);
    // The command the order made, once the game ran: at a target too far.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block);
    w.Frame();
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.paused);
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
    // The action it spent before the conversation stays spent, and the turn
    // did not start twice.
    CHECK(w.machine.View().actionsLeft == 0);
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

TEST_CASE("an AI an effect holds a turn's worth loses its turn", "[machine][conditions]")
{
    // As built there is no bar to take the hold's time of: the turn waits,
    // and is lost once the hold has run a turn's worth of seconds, what a
    // round spends on effects (6 s).
    World w;
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    w.applied.clear();
    w.RunFor(5700);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    w.RunFor(500);
    CHECK(w.Current() != kGenlock);
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock)); // the effect is the engine's
}

TEST_CASE("with effects on the game's clock, a hold's turn's worth is a turn's own seconds", "[machine][conditions]")
{
    // effect_seconds_per_round 0: no round's worth of effects' time to go by.
    Settings s = Fixed();
    s.effectSecondsPerRound = 0.0f;
    s.secondsPerTurn = 3.0f;
    World w(s);
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    const std::int64_t before = w.agedMs;
    w.RunFor(2700);
    CHECK(w.Current() == kGenlock);
    w.RunFor(500);
    CHECK(w.Current() != kGenlock);
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK(w.agedMs == before); // effects' time is not the fight's to move
}

TEST_CASE("a stun that runs out as a hold's time runs has run out on the clock: it keeps the turn it owes",
          "[machine][conditions]")
{
    // Effects' time run by a hold wears a stun off as a round's does. Taken
    // for a cure (a condition gone while effects' time stands still), the
    // turn it owed was given back: a stun shorter than the hold cost nothing.
    World w;
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.Get(kHurlock).condition = Condition::Disabled;
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.timeHeld);
    w.RunFor(600); // the hurlock's stun is seen, and the hold's first steps run
    const std::int64_t before = w.agedMs;
    REQUIRE(before > 0);
    for (int i = 0; i < 100 && w.agedMs == before; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.agedMs > before);
    w.Get(kHurlock).condition = Condition::None; // run out, with that step
    w.RunFor(600);                               // and seen gone while the hold still runs
    REQUIRE(w.Current() == kGenlock);
    w.Finish(kGenlock);
    for (int i = 0; i < 400 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.RunFor(100);
    }
    CHECK(w.Said(L"Hurlock loses the turn"));
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("condition was taken off") != std::string::npos; }));
}

TEST_CASE("a hold is not stopped by max_action_ms: its bound is a turn's worth", "[machine][conditions]")
{
    // The limit on a creature's own command is not a hold's: stopped by it,
    // the effect's command was cleared, which is the engine's to end.
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.RunFor(5000);
    CHECK(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kGenlock));
    CHECK(std::ranges::none_of(w.log,
                               [](const std::string &line) { return line.find("still at a") != std::string::npos; }));
    w.Finish(kGenlock);
    w.RunFor(100);
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
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
    CHECK(w.paused);
}

TEST_CASE("a creature joining mid-fight waits for its first turn", "[machine]")
{
    World w;
    w.StartFight();
    constexpr ObjectId kShriek = 0x202;
    // Not yet in the order: held, and a scan asked for.
    CHECK(w.machine.OnCommand({kShriek, CommandType::Attack, Source::Script, true, std::nullopt}) == Verdict::Block);
    // Not fighting at all (a townsperson walking): let be.
    CHECK(w.machine.OnCommand({0x300, CommandType::MoveToLocation, Source::Script, false, std::nullopt}) ==
          Verdict::Allow);
    w.creatures.push_back(Make(kShriek, Side::Enemy, false, L"Shriek", 30.0f)); // initiative 21, first next round
    w.Frame();
    REQUIRE(w.machine.Order().Find(kShriek) != nullptr);
    CHECK(w.machine.Order().Find(kShriek)->initiative == 21);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.Order().All().front().id == kShriek);
}

TEST_CASE("a round shorter than round_min_seconds is padded with the world running", "[machine]")
{
    Settings settings = Fixed();
    settings.roundMinSeconds = 3.0f;
    World w{settings};
    w.onPoke = [&](ObjectId id) { w.Order(id, CommandType::Wait, Source::Script); };
    w.StartFight();
    for (int turn = 0; turn < 4; ++turn)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.RunFor(50);
    }
    CHECK(w.machine.GetPhase() == Phase::RoundPadding);
    CHECK_FALSE(w.paused);
    CHECK(w.machine.Order().Round() == 2);
    // Nobody acts while it runs.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Block);
    w.RunFor(3000);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Current() == kHero);
}

TEST_CASE("a conversation while the world runs on between two rounds does not lose the round's start", "[machine]")
{
    Settings settings = Fixed();
    settings.roundMinSeconds = 3.0f;
    World w{settings};
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.onPoke = [&](ObjectId id) { w.Order(id, CommandType::Wait, Source::Script); };
    w.StartFight();
    for (int turn = 0; turn < 4; ++turn)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnEndTurn());
        }
        w.RunFor(50);
    }
    REQUIRE(w.machine.GetPhase() == Phase::RoundPadding);
    w.applied.clear();
    w.mode = Mode::Dialog;
    w.RunFor(200);
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    w.mode = Mode::Combat;
    w.RunFor(4000);
    // Round 2 began as a round does: the scripts and the events told, the
    // effects' round spent, and then the hero's turn.
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(std::ranges::count_if(w.applied, [](const Effect &e) {
              return e.kind == EffectKind::Signal && e.event == events::kRoundStart && e.a == 2;
          }) == 1);
    CHECK(std::ranges::count_if(
              events, [](const TurnEvent &e) { return e.kind == EventKind::RoundStarted && e.value == 2; }) == 1);
}

TEST_CASE("the view lists the turns to come, whose it is and what is left", "[machine]")
{
    World w;
    w.StartFight();
    const TurnView view = w.machine.View();
    CHECK(view.active);
    CHECK(view.playerTurn);
    CHECK(view.current == kHero);
    CHECK(view.actionsLeft == 1);
    CHECK(view.movementMax == 9.0f);
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
    CHECK(w.machine.View().movementMax == 9.0f);
    Settings wider = Fixed();
    wider.movementMeters = 14.0f;
    w.machine.SetSettings(wider);
    CHECK(w.machine.View().movementMax == 9.0f); // this turn's stands
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::Wait, Source::Script) == Verdict::Block); };
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().movementMax == 14.0f);
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
    for (const Phase phase :
         {Phase::Idle, Phase::Starting, Phase::TurnStart, Phase::AwaitingOrder, Phase::Dispatching, Phase::AiThinking,
          Phase::Executing, Phase::Settling, Phase::RoundPadding, Phase::Suspended})
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

TEST_CASE("script events and floating text can be turned off", "[machine]")
{
    Settings settings = Fixed();
    settings.scriptEvents = false;
    settings.floaties = false;
    World w{settings};
    w.StartFight();
    CHECK_FALSE(w.Did(EffectKind::Signal));
    CHECK_FALSE(w.Did(EffectKind::Floaty));
}

TEST_CASE("by default a party member's turn passes on as soon as its action is spent", "[machine]")
{
    // The user's choice after the first session in play (2026-09-30).
    Settings s = Fixed();
    s.endTurnWhenActionUsed = Settings{}.endTurnWhenActionUsed;
    REQUIRE(s.endTurnWhenActionUsed);
    World w(s);
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    // The swing lands and the world settles; no end turn pressed.
    w.RunFor(600);
    CHECK(w.Current() == kGenlock);
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
    // A turn's worth of it (6 s as built), and the turn is lost: not twelve.
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
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
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
    w.RunFor(3200);
    CHECK(w.Current() != kCompanion);
    CHECK(w.Said(L"Alistair loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kCompanion)); // the effect is the engine's
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Alistair is held by an effect's command past a turn's 6.0 s: its turn is lost") !=
               std::string::npos;
    }));
}

TEST_CASE("a creature an effect lets go keeps its turn, however long it held", "[machine][conditions]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Order(kCompanion, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.Finish(kGenlock);
    for (int i = 0; i < 100 && w.Current() != kCompanion; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    // Four seconds on the ground and getting up: twice what it was once
    // given before the turn was taken (issue 30).
    w.RunFor(4000);
    REQUIRE(w.Current() == kCompanion);
    w.Finish(kCompanion); // up again
    w.RunFor(1000);
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Alistair is free of the effect's command after ") != std::string::npos;
    }));
}

TEST_CASE("End pressed during the creature's own action waits for it", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.RunFor(100);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    CHECK(w.machine.OnEndTurn());
    w.RunFor(200);
    CHECK(w.Current() == kHero); // still swinging
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() != kHero);
}

TEST_CASE("on the game's clock a held creature loses each turn while its condition lasts", "[machine][conditions]")
{
    // condition_turns 0: the condition lasts its own seconds (spent a round
    // at a time while effects' time is held), and every turn that comes
    // while it holds its creature is lost: the creature cannot act, and the
    // effect's command would not end within the turn.
    Settings s = Fixed();
    s.conditionTurns = 0;
    World w{s};
    w.StartFight();
    w.Get(kGenlock).condition = Condition::Disabled;
    const auto lost = [&] {
        return std::ranges::count_if(w.applied, [](const Effect &e) {
            return e.kind == EffectKind::Floaty && e.text == L"Genlock loses the turn";
        });
    };
    for (int i = 0; i < 40 && lost() < 2; ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(lost() == 2);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::EndConditions, kGenlock)); // the clock ends it, not the machine
    // Worn off: its turn is its own again.
    w.Get(kGenlock).condition = Condition::None;
    for (int i = 0; i < 40 && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
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
constexpr std::int32_t kShieldBash = 617; // 20 s: two turns at 10 s a turn (Fixed's rate)
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
    for (int i = 0; i < 2000 && !(w.Current() == kHero && w.machine.GetPhase() == Phase::AwaitingOrder); ++i)
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

TEST_CASE("an ability's cooldown is held for its turns, then released at the start of its user's turn",
          "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    w.RunFor(100);
    CHECK_FALSE(Cooled(w, kHero, kShieldBash, TurnMachine::kCooldownHoldSeconds)); // not before it has run
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(Cooled(w, kHero, kShieldBash, TurnMachine::kCooldownHoldSeconds));
    w.applied.clear();
    UntilHerosTurn(w); // one turn waited
    CHECK_FALSE(Cooled(w, kHero, kShieldBash, 0));
    UntilHerosTurn(w); // two: ready as the turn begins
    CHECK(Cooled(w, kHero, kShieldBash, 0));
}

TEST_CASE("a sustained ability turned on costs nothing and keeps the turn", "[machine][cooldowns]")
{
    Settings s = Fixed();
    s.endTurnWhenActionUsed = true;
    World w(s);
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldWall, 15.0f, true, false) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(Cooled(w, kHero, kShieldWall, TurnMachine::kCooldownHoldSeconds));
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow); // the action is still there
}

TEST_CASE("a sustained ability turned off waits for its user's next turn", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldWall, 15.0f, true, true) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(w.Current() == kHero); // free: the turn goes on
    CHECK(Cooled(w, kHero, kShieldWall, TurnMachine::kCooldownHoldSeconds));
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(Cooled(w, kHero, kShieldWall, 0));
}

TEST_CASE("at the fight's end, cooldowns still held go back to seconds", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 90015, 60.0f) == Verdict::Allow); // Overwhelm: six turns
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
    CHECK(Cooled(w, kHero, 90015, 60));
}

TEST_CASE("with cooldown_seconds_per_turn 0 the game's timers hold", "[machine][cooldowns]")
{
    Settings s = Fixed();
    s.cooldownSecondsPerTurn = 0.0f;
    World w(s);
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    CHECK(std::ranges::none_of(w.applied, [](const Effect &e) { return e.kind == EffectKind::Cooldown; }));
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

TEST_CASE("by default the world idles on while the player chooses, and nobody else acts", "[machine]")
{
    // The user: not pausing on a party member's turn lets the others idle.
    Settings s = Fixed();
    s.pauseOnPlayerTurn = Settings{}.pauseOnPlayerTurn;
    REQUIRE_FALSE(s.pauseOnPlayerTurn);
    World w(s);
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
    CHECK(w.agedMs == 6000);
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

TEST_CASE("with effect_seconds_per_round 0 effects keep the game's clock", "[machine]")
{
    Settings s = Fixed();
    s.effectSecondsPerRound = 0.0f;
    World w(s);
    w.StartFight();
    CHECK_FALSE(w.timeHeld);
    CHECK_FALSE(w.Did(EffectKind::HoldTime));
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
        w.RunFor(static_cast<std::uint32_t>(s.aiThinkMs / 2 + 100));
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

TEST_CASE("a command that failed without acting gives its action back, once a turn", "[machine]")
{
    // The third play test: an attack the engine failed with an invalid path,
    // its action spent all the same.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().actionsLeft == 0);
    CHECK_FALSE(
        w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH")); // not its turn
    CHECK_FALSE(w.machine.OnCommandFailed(kHero, CommandType::MoveToLocation, -1, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH"));
    CHECK(w.machine.View().actionsLeft == 1);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    CHECK_FALSE(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_INVALID_PATH")); // once
    CHECK(w.machine.View().actionsLeft == 0);
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
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 803, 20.0f, false, false) == Verdict::Allow);
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::UseAbility, 803, "COMMAND_FAILED_INVALID_DATA"));
    w.Finish(kHero);
    w.RunFor(1200);
    CHECK_FALSE(w.Did(EffectKind::Cooldown, kHero));
}

TEST_CASE("the state's last refusal is the player's, with its command and numbers", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block);
    CHECK(w.Order(kGenlock, CommandType::Wait, Source::Script) == Verdict::Block); // the AI's, not the player's
    CHECK(w.machine.State().lastRefusal == "Too far (attack: 30.0 m to walk, 9.0 m left)");
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

TEST_CASE("a hold run out as a round ages effects keeps the turns it owes", "[machine][conditions]")
{
    Settings s = Fixed();
    s.conditionTurns = 2;
    World w{s};
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    // Round 1's turn lost; then, as round 2 begins and ages effects, it runs out.
    w.applied.clear();
    for (int i = 0; i < 200 && !w.Did(EffectKind::AgeTime); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::AgeTime));
    CHECK(w.Said(L"Genlock loses the turn"));
    w.Get(kGenlock).condition = Condition::None;
    w.RunFor(600);
    w.applied.clear();
    for (int i = 0; i < 80 && !w.Said(L"Genlock loses the turn") && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        (void)w.machine.OnEndTurn();
        w.RunFor(100);
    }
    CHECK(w.Said(L"Genlock loses the turn")); // the second it owed
}

TEST_CASE("the game's speed is set for a turn before its camera moves", "[machine][speed]")
{
    // Play tests 2 and 5, when an AI's turn had a speed of its own: at twice
    // the speed the pan to the hero, issued before the speed went back to 1,
    // never arrived. A fight has one speed now, and it does not change
    // between two turns; what is held is that nothing writes it after the
    // camera is moved.
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
    // Nor between the turns before it: the speed was the fight's all along.
    CHECK_FALSE(w.Did(EffectKind::GameSpeed));
}

namespace
{
void PlayARound(World &w)
{
    const int round = w.machine.Order().Round();
    for (int i = 0; i < 4000 && w.machine.Order().Round() == round; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == round + 1);
    w.Frame(); // the new round begins with the next turn
    w.Frame();
}
} // namespace

TEST_CASE("effects' time turned on mid-fight is held from the round's end, turned off let go", "[machine]")
{
    // The review's TB-2: a fight begun at 0 never took the setting up.
    Settings s = Fixed();
    s.effectSecondsPerRound = 0.0f;
    World w(s);
    w.StartFight();
    REQUIRE_FALSE(w.timeHeld);
    s.effectSecondsPerRound = 6.0f;
    w.machine.SetSettings(s);
    PlayARound(w);
    CHECK(w.timeHeld);
    CHECK(w.agedMs == 0); // that round ran on the game's clock
    PlayARound(w);
    CHECK(w.agedMs == 6000);
    s.effectSecondsPerRound = 0.0f;
    w.machine.SetSettings(s);
    PlayARound(w);
    CHECK_FALSE(w.timeHeld);
    CHECK(w.agedMs == 6000);
    s.effectSecondsPerRound = 6.0f;
    w.machine.SetSettings(s);
    PlayARound(w);
    CHECK(w.timeHeld);
    CHECK(w.agedMs == 6000);
    PlayARound(w);
    CHECK(w.agedMs == 12000);
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
    // On the player's turn the machine has the speed at 1 and, with effects
    // on the game's clock, nothing held: a lost batch may have left either
    // otherwise.
    Settings s = Fixed();
    s.effectSecondsPerRound = 0.0f;
    World w{s};
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
    for (int i = 0; i < 15 && w.Current() == kGenlock; ++i)
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
    for (int i = 0; i < 15 && !w.Did(EffectKind::ClearCommands, kHero); ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::ClearCommands, kHero));
    w.Get(kHero).moving = false;
    w.Frame();
    REQUIRE(w.Current() == kHero); // the player's turn goes on, the action kept
    // Another talent, in reach, runs: it alone is held.
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

TEST_CASE("an enemy missing from a scan or two is still in the fight; missing for seconds, it has left",
          "[machine][engaged]")
{
    // The user's eight fights, read off Eluvian's bus: a guard and a
    // soldier "up again" every three seconds for ten minutes, never having
    // fallen, losing the turns that came while they were gone.
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    // Neither it nor the party perceives the other, all along: whether it
    // is in the fight then goes by whether the scan finds it fighting
    // (perceived, it would stay: issue 29, test_perception.cpp).
    w.Get(kGenlock).perception = Unperceived();
    w.StartFight();
    for (int flap = 0; flap < 6; ++flap)
    {
        w.Get(kGenlock).fighting = false;
        PassTurns(w, 100); // 1.6 s, several scans
        CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
        w.Get(kGenlock).fighting = true;
        PassTurns(w, 100);
    }
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::none_of(events, [](const TurnEvent &e) { return e.kind == EventKind::UpAgain; }));
    // Gone for good.
    w.Get(kGenlock).fighting = false;
    for (int i = 0; i < 1500; ++i) // 24 s: out, and it stays out, alive as it is
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
        if (i > 400)
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
    // Fighting again: back in, said once, as one that joins or as one back.
    w.Get(kGenlock).fighting = true;
    PassTurns(w, 200);
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    CHECK_FALSE(w.machine.Order().Find(kGenlock)->down);
    CHECK(std::ranges::count_if(events, [](const TurnEvent &e) {
              return e.who == kGenlock && (e.kind == EventKind::UpAgain || e.kind == EventKind::Joined);
          }) == 1);
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
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.5f) == Verdict::Allow);
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

TEST_CASE("a held cooldown's sweep shows its turns: what is left over what it was held for", "[machine][cooldowns]")
{
    // The user, 2026-09-30: with 2 of 3 turns left to wait, a third
    // recovered. Fixed() has 10 s a turn: 30 s is three turns.
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 30.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    const auto shown = [&](std::int32_t left, std::int32_t of) {
        return std::ranges::any_of(w.applied, [&](const Effect &e) {
            return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kShieldBash && e.b == left &&
                   e.c == of;
        });
    };
    CHECK(shown(3600, 3600)); // held: none of it recovered
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(shown(2400, 3600)); // 2 of 3 left: a third recovered
    REQUIRE(w.machine.State().cooldowns.size() == 1);
    CHECK(w.machine.State().cooldowns[0].turns == 2);
    CHECK(w.machine.State().cooldowns[0].of == 3);
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(shown(1200, 3600));
    w.applied.clear();
    UntilHerosTurn(w);
    CHECK(Cooled(w, kHero, kShieldBash, 0)); // ready
}

TEST_CASE("a fight's end gives a held cooldown back in seconds, left and in all", "[machine][cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 30.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    UntilHerosTurn(w); // 2 of 3 turns left
    w.applied.clear();
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    (void)w.machine.OnEndTurn();
    w.RunFor(500);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kShieldBash && e.b == 20 && e.c == 30;
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
    CHECK(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.Frame();
    w.Frame();
    w.Frame();
    CHECK(w.Current() == kGenlock); // its turn goes on
    CHECK(pokes == 2);              // told again: it chose again
    // The second fails too: no third try, the turn ends.
    CHECK_FALSE(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
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

TEST_CASE("an AI told to choose again that only moves is not told a third time", "[machine]")
{
    // Its action is still unspent after the move: without the once, it
    // would be told again after every step.
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn(); // the hero passes
    int pokes = 0;
    w.onPoke = [&](ObjectId id) {
        ++pokes;
        const CommandType choice = pokes == 1 ? CommandType::Attack : CommandType::MoveToLocation;
        CHECK(w.Order(id, choice, Source::Script) == Verdict::Allow);
    };
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.Frame();
    REQUIRE(pokes == 1);
    CHECK(w.machine.OnCommandFailed(kGenlock, CommandType::Attack, -1, "COMMAND_FAILED_NO_SPACE_IN_MELEE_RING"));
    w.Finish(kGenlock);
    w.Frame();
    w.Frame();
    w.Frame();
    REQUIRE(pokes == 2); // it moves instead
    w.RunFor(200);
    w.Finish(kGenlock);
    w.RunFor(2000);
    CHECK(pokes == 2);
    CHECK(w.Current() != kGenlock); // its turn ended
}

TEST_CASE("with the movement spent, a talent on something in reach is used, not stopped", "[machine]")
{
    // The seventh play test: Eleanor, 0.0 m left, a soldier 2.0 m away
    // (reach 3.5): her Shield Bash was cleared 9 ms after it was let
    // through, "out of movement", the action gone and nothing struck.
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
    w.Frame();
    w.Get(kHero).moving = true;
    for (int i = 0; i < 12 && w.Get(kHero).moving; ++i)
    {
        w.Get(kHero).position.x += 1.0f;
        w.Frame();
    }
    w.RunFor(500);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.View().movementLeft == 0.0f);
    w.applied.clear();
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
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
    for (int i = 0; i < 200 && !w.Did(EffectKind::ClearCommands, kGenlock); ++i)
    {
        w.Get(kGenlock).position.x += 0.1f;
        walked += 0.1f;
        w.Frame();
    }
    REQUIRE(w.Did(EffectKind::ClearCommands, kGenlock));
    // Its 9 m, the overshoot, and no more than a frame or two.
    CHECK(walked < 9.0f + TurnMachine::kOvershoot + 0.5f);
}

TEST_CASE("a door used costs no action and does not end the turn; an attack after it does", "[machine]")
{
    // The user, 2026-10-01: only combat actions (an attack, a spell, a
    // potion) end the turn. Before, a door cost the action.
    Settings s = Fixed();
    s.endTurnWhenActionUsed = true;
    World w(s);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    CHECK(w.machine.View().actionsLeft == 1);
    w.RunFor(300);
    w.Finish(kHero);
    w.RunFor(2000);
    CHECK(w.Current() == kHero); // still his turn
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // And again: a chest after the door.
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Player) == Verdict::Allow);
    w.RunFor(300);
    w.Finish(kHero);
    w.RunFor(1000);
    REQUIRE(w.Current() == kHero);
    // The combat action is still his, and it ends the turn.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    CHECK(w.machine.View().actionsLeft == 0);
    w.RunFor(300);
    w.Finish(kHero);
    w.RunFor(2000);
    CHECK(w.Current() != kHero);
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
    CHECK(walked < 9.0f + TurnMachine::kOvershoot + 0.5f);
    CHECK(w.machine.View().actionsLeft == 1); // nothing was spent but the walk
}

TEST_CASE("an action whose own effect holds its creature gets that effect's time, out of the round's", "[machine]")
{
    // The eighth play test: a giant spider's Overwhelm, an effect on the
    // spider and one on its prey, each ended by its time, with effects'
    // time held: twelve seconds, then "took too long".
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn(); // the hero passes
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
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.timeHeld);
    const std::int64_t before = w.agedMs;
    w.RunFor(200); // the leap
    // The pounce: an effect's command follows the talent's, and ends when
    // four seconds of the effect's time have run.
    REQUIRE(w.Current() == kGenlock);
    w.Get(kGenlock).busy = true;
    w.Get(kGenlock).current = CommandType::ExecuteEffect;
    int frames = 0;
    for (; frames < 2000 && w.Current() == kGenlock; ++frames)
    {
        if (w.Get(kGenlock).busy && w.agedMs - before >= 4000)
        {
            w.Finish(kGenlock);
        }
        w.Frame();
    }
    CHECK(w.Current() != kGenlock);
    CHECK(frames * 16 < 6000); // about its four seconds, not max_action_ms
    CHECK(w.agedMs - before >= 4000);
    CHECK(w.agedMs - before < 4600);
    CHECK(std::ranges::none_of(
        w.log, [](const std::string &line) { return line.find("took too long") != std::string::npos; }));
    // The round is still six seconds of effects' time in all.
    for (int i = 0; i < 4000 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == 2);
    w.Frame();
    w.Frame();
    CHECK(w.agedMs - before == 6000);
}

TEST_CASE("held by an effect as its turn begins, effects' time runs with the hold and comes off the round's worth",
          "[machine]")
{
    // An effect that keeps its creature while it lasts can wear off within
    // its victim's turn only if effects' time runs while it holds. Held
    // still, as it was while a hold cost the turn after two seconds whatever
    // it was, such an effect outlasted any wait (issue 30). Where the
    // fight holds one clock for everyone's effects it runs with the hold, a
    // step at a time, as it does with an action's own effect.
    Settings s = Fixed();
    s.partyControl = PartyControl::Manual;
    World w{s};
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    (void)w.machine.OnEndTurn();
    REQUIRE(w.timeHeld);
    const std::int64_t before = w.agedMs;
    bool turned = false;
    int heldFrames = 0;
    for (int i = 0; i < 400 && w.machine.Order().Round() < 2 && w.Current() != kCompanion; ++i)
    {
        // Held, it may still turn to face (a free command, let through):
        // that is not an action of its own, and the effect still holds it.
        if (!turned && w.Current() == kGenlock && w.machine.State().heldByEffect)
        {
            turned = true;
            CHECK(w.Order(kGenlock, CommandType::Turn, Source::Script) == Verdict::Allow);
            w.Get(kGenlock).current = CommandType::ExecuteEffect;
        }
        // The effect wears off once two seconds of its time have run.
        if (w.Get(kGenlock).busy && w.agedMs - before >= 2000)
        {
            w.Finish(kGenlock);
        }
        heldFrames += w.Current() == kGenlock && w.Get(kGenlock).busy ? 1 : 0;
        w.Frame();
    }
    CHECK(turned);
    CHECK_FALSE(w.Get(kGenlock).busy); // it wore off, within the turn
    CHECK_FALSE(w.Said(L"Genlock loses the turn"));
    CHECK(w.agedMs - before >= 2000);
    CHECK(w.agedMs - before < 2300); // in quarter-second steps
    // Once over, with the world: its two seconds took two seconds, not one
    // (the hold taken for its creature's own action's effect as well, the
    // turn to face being a command of its own, ran effects' time twice).
    CHECK(heldFrames * 16 >= 1900);
    // The round is still six seconds of effects' time in all.
    for (int i = 0; i < 4000 && w.machine.Order().Round() < 2; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.machine.Order().Round() == 2);
    w.Frame();
    w.Frame();
    CHECK(w.agedMs - before == 6000);
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
    CHECK(w.machine.View().actionsLeft == 1);
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
    CHECK(w.machine.OnCommandFailed(kHero, CommandType::Attack, -1, "COMMAND_FAILED_PATH_ACTION_REQUIRED"));
    CHECK(w.Order(kHero, CommandType::UseObject, Source::Script) == Verdict::Allow);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Allow);
    REQUIRE(w.machine.View().actionsLeft == 0);
    w.Get(kHero).current = CommandType::UseObject; // the door first, the attack behind it
    w.applied.clear();
    w.RunFor(2000);
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kHero)); // not yet
    w.RunFor(1000);
    CHECK(w.Did(EffectKind::ClearCommands, kHero));
    CHECK(w.machine.View().actionsLeft == 1); // the attack never began
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

TEST_CASE("an AI standing at a move it does not take is stopped, and its turn ends", "[machine]")
{
    World w;
    w.StartFight();
    (void)w.machine.OnEndTurn();
    w.onPoke = [&](ObjectId id) { CHECK(w.Order(id, CommandType::MoveToObject, Source::Script) == Verdict::Allow); };
    for (int i = 0; i < 20 && w.Current() != kGenlock; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kGenlock);
    w.applied.clear();
    w.RunFor(4000);
    CHECK(w.Did(EffectKind::ClearCommands, kGenlock));
    w.Finish(kGenlock);
    w.RunFor(1500);
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

TEST_CASE("a creature that begins to fight near, but not the party, mid-fight is said to wait outside, once",
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
    const auto said = [&] {
        return std::ranges::count_if(w.log, [](const std::string &line) {
            return line.find("Shriek is fighting near, but not the party") != std::string::npos;
        });
    };
    CHECK(said() == 0);
    w.Get(0x202).fighting = true; // a guard has found it, rooms away
    UseSomething(w, kHero);
    UseSomething(w, kHero);
    CHECK(said() == 1);
    CHECK(w.machine.Order().Find(0x202) == nullptr);
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
// The user's turn: four seconds (cooldown_seconds_per_turn's default).
Settings FourSecondTurns()
{
    Settings s = Fixed();
    s.cooldownSecondsPerTurn = 4.0f;
    return s;
}

constexpr std::int32_t kBlizzard = 13000;

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

TEST_CASE("instant, fast and normal are cast on the turn; slow charges one, ritual two", "[machine][charge]")
{
    // The user's rule, 2026-10-01, a turn being 4 s.
    const auto verdict = [](float conjureSeconds) {
        World w(FourSecondTurns());
        w.StartFight();
        return w.Cast(kHero, Source::Player, kBlizzard, conjureSeconds);
    };
    CHECK(verdict(0.0f) == Verdict::Allow);
    CHECK(verdict(1.0f) == Verdict::Allow);
    CHECK(verdict(2.0f) == Verdict::Allow);
    CHECK(verdict(4.0f) == Verdict::Hold);
    CHECK(verdict(6.0f) == Verdict::Hold);
    const auto turns = [](float conjureSeconds) {
        World w(FourSecondTurns());
        w.StartFight();
        (void)w.Cast(kHero, Source::Player, kBlizzard, conjureSeconds);
        const MachineState state = w.machine.State();
        return state.charging.empty() ? 0 : state.charging.front().turns;
    };
    CHECK(turns(4.0f) == 1);
    CHECK(turns(6.0f) == 2);
    CHECK(turns(2.0f) == 0);
}

TEST_CASE("a slow spell is begun on one turn and cast at the start of the next, for nothing", "[machine][charge]")
{
    World w(FourSecondTurns());
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Cast(kHero, Source::Player, kBlizzard, 4.0f) == Verdict::Hold);
    CHECK(w.machine.View().actionsLeft == 0);
    CHECK_FALSE(w.Get(kHero).busy); // not queued: kept
    CHECK(w.heldCasts.contains(kHero));
    w.Frame();
    w.Frame();
    CHECK(w.Current() != kHero); // the turn is over: it does nothing else
    CHECK(w.Said(L"Casting"));
    CHECK_FALSE(Released(w, kHero));
    // No cooldown yet: it has not been cast.
    CHECK(std::ranges::none_of(w.applied, [](const Effect &e) { return e.kind == EffectKind::Cooldown; }));
    UntilHeroAgain(w);
    // Cast as his turn begins.
    CHECK(Released(w, kHero));
    REQUIRE(w.released.size() == 1);
    CHECK(w.released.front() == Verdict::Allow);
    CHECK(w.Get(kHero).busy);
    CHECK(w.machine.View().actionsLeft == 1); // it cost this turn nothing
    w.RunFor(300);
    w.Finish(kHero);
    w.RunFor(1500);
    // And his turn follows it: the player's to play.
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().actionsLeft == 1);
    CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
        return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kBlizzard;
    })); // cast, it is on cooldown
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    const auto count = [&](EventKind kind) {
        return std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == kHero; });
    };
    CHECK(count(EventKind::CastCharging) == 1);
    CHECK(count(EventKind::CastLanded) == 1);
    CHECK(count(EventKind::CastInterrupted) == 0);
}

TEST_CASE("a turn opened by a kept cast is the player's though its own AI waits in a loop", "[machine][charge]")
{
    // Issue 10: Lyna's kept shot landed as her turn began and felled the
    // fight's last enemy. Not yet selected, she was a follower whose full AI
    // ran as each of her commands ended, and with no enemy to choose it
    // queued a wait of a second, and another as that one ended. Each was let
    // through, so the turn stood at executing for eleven minutes: never
    // handed over, never waiting on an order.
    World w(FourSecondTurns());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Cast(kHero, Source::Player, kBlizzard, 4.0f) == Verdict::Hold);
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

TEST_CASE("a ritual charges two turns and is cast on the third", "[machine][charge]")
{
    World w(FourSecondTurns());
    w.StartFight();
    CHECK(w.Cast(kHero, Source::Player, kBlizzard, 6.0f) == Verdict::Hold);
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() != kHero);
    // The second turn: spent charging, nothing for the player to do.
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
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Hero's turn is spent charging ability 13000 (1 more)") != std::string::npos;
    }));
    // The third: cast.
    for (int i = 0; i < 20 && !Released(w, kHero); ++i)
    {
        w.Frame();
    }
    CHECK(Released(w, kHero));
    CHECK(w.Current() == kHero);
}

TEST_CASE("a caster held, fallen or gone loses its charge: nothing cast, nothing on cooldown", "[machine][charge]")
{
    const auto interrupted = [](const std::function<void(Creature &)> &how, const char *why) {
        World w(FourSecondTurns());
        std::vector<TurnEvent> events;
        w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
        w.StartFight();
        REQUIRE(w.Cast(kHero, Source::Player, kBlizzard, 4.0f) == Verdict::Hold);
        w.Frame();
        w.Frame();
        how(w.Get(kHero));
        w.RunFor(700); // conditions are read twice a second, and as each turn begins
        CAPTURE(why);
        CHECK(w.machine.State().charging.empty());
        CHECK_FALSE(w.heldCasts.contains(kHero)); // the game layer forgets it
        CHECK(w.Said(L"Action lost"));
        CHECK(std::ranges::count_if(events, [&](const TurnEvent &e) {
                  return e.kind == EventKind::CastInterrupted && e.who == kHero && e.detail == why &&
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
    interrupted([](Creature &c) { c.condition = Condition::Disabled; }, "held");
    interrupted([](Creature &c) { c.down = true; }, "fell");
    interrupted([](Creature &c) { c.exists = false; }, "gone");
}

TEST_CASE("an enemy's slow spell is said a turn ahead, cast on its next, and its turn follows", "[machine][charge]")
{
    World w(FourSecondTurns());
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
            CHECK(w.Cast(id, Source::Script, kBlizzard, 4.0f) == Verdict::Hold);
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
    CHECK(w.Current() != kGenlock); // its turn ended on the charge
    CHECK(w.machine.State().charging.size() == 1);
    // Round 2: the genlock's turn begins with the cast, then its AI is told.
    for (int i = 0; i < 4000 && !Released(w, kGenlock); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(Released(w, kGenlock));
    CHECK(pokes == 1); // not told while the cast is given
    w.RunFor(300);
    w.Finish(kGenlock);
    w.RunFor(1000);
    CHECK(pokes == 2); // its turn after the cast
}

TEST_CASE("an ability named in [turns] charge charges, whatever its conjuring time", "[machine][charge]")
{
    Settings s = FourSecondTurns();
    s.charge = {{kBlizzard, 1}, {617, 0}};
    World w(s);
    w.StartFight();
    CHECK(w.Cast(kHero, Source::Player, kBlizzard, 1.0f) == Verdict::Hold); // fast, and made to charge
    World other(s);
    other.StartFight();
    CHECK(other.Cast(kHero, Source::Player, 617, 6.0f) == Verdict::Allow); // a ritual, and made not to
}

TEST_CASE("the fight over, a charge is lost", "[machine][charge]")
{
    World w(FourSecondTurns());
    w.StartFight();
    REQUIRE(w.Cast(kHero, Source::Player, kBlizzard, 4.0f) == Verdict::Hold);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(2000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(w.machine.State().charging.empty());
    CHECK_FALSE(w.heldCasts.contains(kHero));
}

TEST_CASE("an enemy that goes on by itself after its charged cast is not told its turn has come", "[machine][charge]")
{
    // The twelfth play test: the spider's own attack followed its cast, its
    // action spent; told then, it chose nothing for a second and a half.
    World w(FourSecondTurns());
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
            CHECK(w.Cast(id, Source::Script, kBlizzard, 4.0f) == Verdict::Hold);
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
    // The cast ends, and its own script attacks at once.
    w.Finish(kGenlock);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Allow);
    w.RunFor(300);
    w.Finish(kGenlock);
    w.RunFor(1200);
    CHECK(pokes == 1);              // not told: it had acted
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
    CHECK(frames * 16 >= 5900); // a turn's worth of the hold (6 s as built)
    CHECK(frames * 16 < 7000);  // not max_action_ms' twelve seconds
    CHECK(std::ranges::any_of(w.log, [](const std::string &line) {
        return line.find("Genlock is held by an effect's command past a turn's 6.0 s: its turn is lost") !=
               std::string::npos;
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

TEST_CASE("the fight's speed is set as a turn begins, before the camera moves to it", "[machine]")
{
    // Twice, the pan to the hero's turn, issued before the speed changed,
    // never arrived (play tests 2 and 5, 2026-09-30): the speed first.
    Settings s = Fixed();
    s.fightSpeed = 2.0f;
    s.cameraFollow = true;
    World w(s);
    w.StartFight();
    w.Frame();
    const auto speed = std::ranges::find_if(w.applied, [](const Effect &e) { return e.kind == EffectKind::GameSpeed; });
    const auto camera =
        std::ranges::find_if(w.applied, [](const Effect &e) { return e.kind == EffectKind::CenterCamera; });
    REQUIRE(speed != w.applied.end());
    REQUIRE(camera != w.applied.end());
    CHECK(speed < camera);
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
