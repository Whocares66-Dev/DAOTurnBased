// The state machine driven through scripted fights, a case each, in the
// stand-in for the game (World.h). test_fights.cpp drives it through random
// ones.
#include "World.h"

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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey()); // the next turn's scan finds it
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
    CHECK(w.machine.OnPauseKey()); // the hero passes
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
    CHECK(w.machine.OnPauseKey());
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

TEST_CASE("under tactics the companions play their own turns, the hero is still the player's", "[machine]")
{
    Settings settings = Fixed();
    settings.partyControl = PartyControl::Tactics;
    World w{settings};
    w.StartFight();
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder); // the hero
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock)); // it would choose and queue behind the effect
    w.RunFor(500);
    w.Finish(kGenlock); // up again within the grace
    w.RunFor(100);
    CHECK(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an AI an effect holds past the grace loses its turn", "[machine][conditions]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kGenlock, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    w.RunFor(static_cast<int>(TurnMachine::kEffectGraceMs) + 200);
    CHECK(w.Current() != kGenlock);
    CHECK(w.Said(L"Genlock loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
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
            CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.machine.View().movementMax == 14.0f);
}

TEST_CASE("the pause key is the game's outside a turn-based fight", "[machine]")
{
    World w;
    w.Frame();
    CHECK_FALSE(w.machine.OnPauseKey());
    w.StartFight();
    CHECK(w.machine.OnPauseKey());
    w.mode = Mode::Dialog;
    w.Frame();
    CHECK_FALSE(w.machine.OnPauseKey()); // a conversation's own
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

TEST_CASE("a combatant gone from the world loses its turns", "[machine]")
{
    World w;
    w.StartFight();
    w.Get(kGenlock).exists = false; // a script destroyed it
    w.RunFor(600);                  // the statuses come in
    CHECK(w.machine.Order().Find(kGenlock)->down);
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey()); // the hero passes; the genlock's AI stays silent
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
    CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey()); // the hero's turn ends
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
            CHECK(w.machine.OnPauseKey());
        }
        w.RunFor(100);
    }
    w.applied.clear();
    for (int i = 0; i < 400 && w.Current() != kGenlock; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    CHECK(w.Current() != kHero);
}

TEST_CASE("a creature an effect holds as its turn comes loses the turn after a grace, and no order queues behind it",
          "[machine][conditions]")
{
    // A condition the statuses do not name (a grab, say): the engine keeps
    // its creature in the effect's command, and with effects' time held
    // that command outlasts any turn.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.OnPauseKey());
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
    w.RunFor(static_cast<int>(TurnMachine::kEffectGraceMs) + 200);
    CHECK(w.Said(L"Held"));
    CHECK(w.Current() != kCompanion);
    CHECK(w.Said(L"Alistair loses the turn"));
    CHECK_FALSE(w.Did(EffectKind::ClearCommands, kCompanion)); // the effect is the engine's
}

TEST_CASE("a creature an effect lets go within the grace keeps its turn", "[machine][conditions]")
{
    World w;
    w.StartFight();
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.Order(kCompanion, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
    w.Finish(kGenlock);
    for (int i = 0; i < 100 && w.Current() != kCompanion; ++i)
    {
        w.Frame();
    }
    REQUIRE(w.Current() == kCompanion);
    w.RunFor(500);
    w.Finish(kCompanion); // up again
    w.RunFor(1000);
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
}

TEST_CASE("End pressed during the creature's own action waits for it", "[machine]")
{
    World w;
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.RunFor(100);
    REQUIRE(w.machine.GetPhase() == Phase::Executing);
    CHECK(w.machine.OnPauseKey());
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
        (void)w.machine.OnPauseKey();
        w.RunFor(100);
    }
    CHECK(lost() == 2);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kGenlock));
    CHECK_FALSE(w.Did(EffectKind::EndConditions, kGenlock)); // the clock ends it, not the machine
    // Worn off: its turn is its own again.
    w.Get(kGenlock).condition = Condition::None;
    for (int i = 0; i < 40 && !w.Did(EffectKind::PokeAi, kGenlock); ++i)
    {
        (void)w.machine.OnPauseKey();
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
            CHECK(w.machine.OnPauseKey());
        }
        w.Frame();
    }
    for (int i = 0; i < 2000 && !(w.Current() == kHero && w.machine.GetPhase() == Phase::AwaitingOrder); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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
    w.RunFor(500);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // The player's own order still goes through.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
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
            CHECK(w.machine.OnPauseKey());
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
    CHECK(w.machine.OnPauseKey());
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

TEST_CASE("an AI's turn runs at ai_turn_speed, the player's turns and the rest at the game's own", "[machine][speed]")
{
    Settings s = Fixed();
    s.aiTurnSpeed = 2.0f;
    World w{s};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.speed == 1000);
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kGenlock);
    CHECK(w.speed == 2000);
    w.RunFor(3000); // it chooses nothing, and the companion's turn comes
    REQUIRE(w.Current() == kCompanion);
    CHECK(w.speed == 1000);
    // A conversation plays at the game's own speed.
    CHECK(w.machine.OnPauseKey());
    w.Frame();
    w.Frame();
    REQUIRE(w.Current() == kHurlock);
    CHECK(w.speed == 2000);
    w.mode = Mode::Dialog;
    w.Frame();
    CHECK(w.speed == 1000);
    // Back to the hurlock's turn, fast again; and the fight's end, not.
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

TEST_CASE("at ai_turn_speed 1 the game's speed is never written", "[machine][speed]")
{
    // Another plugin may be setting it (DAOLib's examples/speed).
    World w;
    w.StartFight();
    CHECK(w.machine.OnPauseKey());
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

TEST_CASE("at ai_turn_speed 2 an AI's waits pass in half the time", "[machine][speed]")
{
    // The desktop play test: an enemy's turn took 3 s at speed 1 and at 2,
    // the machine's own waits counting real time.
    for (const float speed : {1.0f, 2.0f})
    {
        Settings s = Fixed();
        s.aiTurnSpeed = speed;
        World w{s};
        w.StartFight();
        CHECK(w.machine.OnPauseKey());
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
        (void)w.machine.OnPauseKey();
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
        (void)w.machine.OnPauseKey();
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
        (void)w.machine.OnPauseKey();
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
    CHECK(w.machine.OnPauseKey()); // the hero's
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
    CHECK(w.machine.OnPauseKey());
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
        (void)w.machine.OnPauseKey();
        w.RunFor(100);
    }
    REQUIRE(w.Current() == kCompanion);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion)); // held
    w.applied.clear();
    w.Finish(kCompanion); // let go within the grace
    w.RunFor(100);
    CHECK(w.Did(EffectKind::Select, kHero));
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
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
        (void)w.machine.OnPauseKey();
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
        (void)w.machine.OnPauseKey();
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
            (void)w.machine.OnPauseKey();
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
        (void)w.machine.OnPauseKey();
        w.RunFor(100);
    }
    CHECK(w.Said(L"Genlock loses the turn")); // the second it owed
}

TEST_CASE("the game's speed is set for a turn before its camera moves", "[machine][speed]")
{
    // Play tests 2 and 5: at ai_turn_speed 2 the pan to the hero, issued
    // before the speed went back to 1, never arrived.
    Settings s = Fixed();
    s.aiTurnSpeed = 2.0f;
    s.cameraFollow = true;
    World w{s};
    w.StartFight();
    CHECK(w.machine.OnPauseKey()); // the hero's turn: the genlock's at 2
    for (int i = 0; i < 80 && w.Current() != kCompanion; ++i)
    {
        (void)w.machine.OnPauseKey();
        w.RunFor(100);
    }
    w.applied.clear();
    for (int i = 0; i < 200 && w.Current() != kHero; ++i)
    {
        (void)w.machine.OnPauseKey();
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
    CHECK(w.machine.State().speed == 1.0f);
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
            (void)w.machine.OnPauseKey();
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
    s.aiTurnSpeed = 2.0f;
    World w{s};
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f) == Verdict::Allow);
    w.RunFor(200);
    w.Finish(kHero);
    (void)w.machine.OnPauseKey();
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
    CHECK(w.machine.OnPauseKey()); // the hero passes
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
    (void)w.machine.OnPauseKey();
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
            (void)w.machine.OnPauseKey();
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
            (void)w.machine.OnPauseKey();
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
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    PassTurns(w, 200);
    CHECK_FALSE(w.machine.Active());
    // The one outside is the game's again, in real time.
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
            (void)w.machine.OnPauseKey();
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
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Allow);
    w.Frame();
    w.Frame();
    w.Frame(); // 48 ms
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Block);
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
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 2.0f) == Verdict::Block);
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
    (void)w.machine.OnPauseKey();
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
    (void)w.machine.OnPauseKey(); // the hero passes
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
    (void)w.machine.OnPauseKey(); // the hero passes
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
