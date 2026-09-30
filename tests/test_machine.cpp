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
    // Within reach of the movement left, with a little slack for the approach.
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 9.4f) == Verdict::Allow);
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
    REQUIRE(view.upcoming.size() >= 4);
    CHECK(view.upcoming[0].id == kHero);
    CHECK(view.upcoming[0].current);
    CHECK(view.upcoming[1].id == kGenlock);
    CHECK(view.upcoming[4].round == 2);
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
constexpr std::int32_t kShieldBash = 617; // 20 s: two turns at 10 s a turn
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
