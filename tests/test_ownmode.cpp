// No clock on the player's turn, and a mode that keeps its user at a command
// (issue 51). Scripted cases in the stand-in for the game (World.h).
//
// The Bard's Captivating Song is a mode in form, turned on and kept up, and
// the one that takes its singer's every other action while it is on: the
// game keeps the singer at a command of the song's own effect. The mod read
// that command as any hold, a knockdown's or a stun's: the time it held was
// taken of the singer's bar as the world ran, and the turn was lost four
// seconds of the game's time on, before the player had chosen anything. In
// the ability sweep of 2026-10-05 a party member given the song lost ninety
// turns in a row. And turned on by the player in its singer's own turn, the
// song's command was taken for that turn's action and never ended: the turn
// stood at "executing" to the action's limit, `still at a execute effect
// after 108000 ms: stopped`.
//
// The user, that day: "I think there probably should not be a time out for
// the player, unless they're letting tactics run the actor. This will give
// the player the opportunity to turn off the song at their leisure, or end
// their turn."; "timeout should end any actions that are still being taken
// when it forces an end"; and "Yeah, sustained abilities should not be
// ended". What is promised:
//
//   - a turn the player plays is never ended by a clock of the mod's: not at
//     max_action_ms, as it never was at max_turn_ms. A turn its tactics play
//     keeps both;
//   - a creature at its own mode's command is not held. On a turn the player
//     plays the turn waits, its bar not running, for as long as the player
//     likes; End goes on with the mode and the turn is spent on it; the mode
//     turned off is free and leaves the turn to act with; an action or a move
//     ordered meanwhile is refused before anything is spent. On a turn its
//     tactics play the turn is spent on the mode at once, its AI not told;
//   - a hold that is another's doing stays a hold, on both kinds of turn,
//     whatever modes its creature has on;
//   - whatever ends a turn by force stops what its creature was at: its
//     commands are cleared, at a time limit on a turn its tactics play and at
//     the player's End pressed while an action still runs. What the action
//     had spent stays spent; and no mode is switched off by it.
#include "World.h"

#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// A mode that leaves its user free to act, as most do (Shield Wall): the
// stand-in binds nobody by it.
constexpr std::int32_t kShieldWall = 3041;

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

int Effects(const World &w, EffectKind kind, ObjectId target)
{
    return static_cast<int>(
        std::ranges::count_if(w.applied, [&](const Effect &e) { return e.kind == kind && e.target == target; }));
}

bool InATurn(const World &w)
{
    const Phase phase = w.machine.GetPhase();
    return phase == Phase::AwaitingOrder || phase == Phase::AiThinking || phase == Phase::Executing ||
           phase == Phase::Settling;
}

// What was heard of the turns: each turn's bar as it began and why each
// ended, by whose it was; the clock as each began and ended; whom the AI was
// told to choose for; and what was refused.
struct Heard
{
    explicit Heard(World &w)
    {
        w.machine.SetEventSink([this, &w](const TurnEvent &e) {
            if (e.kind == EventKind::TurnStarted)
            {
                bars[e.who].push_back(w.machine.View().timeMs);
                beganAt[e.who] = e.worldMs;
            }
            if (e.kind == EventKind::TurnEnded)
            {
                ends[e.who].push_back(e.detail);
                took[e.who].push_back(e.worldMs - beganAt[e.who]);
            }
            if (e.kind == EventKind::Refused)
            {
                refused.push_back(e);
            }
            if (e.kind == EventKind::AwaitingOrder)
            {
                awaited.push_back(e.detail);
            }
        });
    }
    std::unordered_map<ObjectId, std::vector<std::uint32_t>> bars;
    std::unordered_map<ObjectId, std::vector<std::string>> ends;
    std::unordered_map<ObjectId, std::uint32_t> beganAt;
    std::unordered_map<ObjectId, std::vector<std::uint32_t>> took; // of the fight's clock, each turn
    std::vector<TurnEvent> refused;
    std::vector<std::string> awaited;
};

// Everyone the AI plays waits when told (a wait of no length: a pass), but
// for what a case has one of them do; each telling is counted.
struct Tellings
{
    explicit Tellings(World &w)
    {
        w.onPoke = [this, &w](ObjectId id) {
            who.push_back(id);
            if (const auto does = choice.find(id); does != choice.end())
            {
                does->second();
                return;
            }
            (void)w.Wait(id, 0.0f);
        };
    }
    [[nodiscard]] int Of(ObjectId id) const
    {
        return static_cast<int>(std::ranges::count(who, id));
    }
    std::vector<ObjectId> who;
    std::unordered_map<ObjectId, std::function<void()>> choice;
};

// The turns go by, the player's ended as they come, until it is `id`'s and
// being played.
void UntilTurnOf(World &w, ObjectId id)
{
    for (int frames = 0; frames < 4000 && w.Current() == id && InATurn(w); ++frames)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    for (int frames = 0; frames < 4000 && (w.Current() != id || !InATurn(w)); ++frames)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == id);
    REQUIRE(InATurn(w));
}

// An action of so many seconds at something in reach.
Verdict Act(World &w, ObjectId id, Source source, float seconds)
{
    return w.Attack(id, id == kHero || id == kCompanion ? kGenlock : kHero, source, NoAttack::None, 0.0f, seconds);
}

// At an effect's command the engine put on it (a knockback, a grab): none of
// the five conditions, and nothing of its own doing.
void Throw(World &w, ObjectId id)
{
    REQUIRE(w.Order(id, CommandType::ExecuteEffect, Source::Engine) == Verdict::Allow);
}

} // namespace

TEST_CASE("the player's singer's turn waits with its bar whole, however long the world runs, and is never ended for it",
          "[ownmode]")
{
    // The issue's own fight: `Lyna is held by an effect's command past its
    // bar (4.08 s held this turn): its turn is lost`, ninety turns running.
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    Heard heard(w);
    w.Get(kHero).modes.insert(kSong);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Get(kHero).current == CommandType::ExecuteEffect);
    w.applied.clear();
    // Two minutes: thirty of the turns it lost.
    for (int ms = 0; ms < 120000; ms += 16)
    {
        w.Frame();
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    }
    CHECK(w.Current() == kHero);
    CHECK_FALSE(w.paused); // the world idles on while the player chooses
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(heard.ends[kHero].empty());
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(w.Get(kHero).modes.contains(kSong));
    // Nothing of its clock was moved for the song, and nothing of its
    // was cleared.
    CHECK(Effects(w, EffectKind::AgeTime, kHero) == 0);
    CHECK(Effects(w, EffectKind::AgeTime, kNoObject) == 0);
    CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 0);
    // Said once why the turn waits, and not as a hold.
    CHECK(Lines(w, "Hero is at its own mode's command (ability 1000), not held: its turn waits for the player") == 1);
    CHECK(Lines(w, "is held by an effect's command") == 0);
    CHECK(Lines(w, "the gate did not see") == 0);
    CHECK(Lines(w, "took too long") == 0);
    CHECK(Lines(w, "still at a") == 0);
}

TEST_CASE("End goes on singing: the turn is spent on the mode, and nothing rolls over", "[ownmode]")
{
    World w;
    Heard heard(w);
    Tellings told(w);
    w.Get(kHero).modes.insert(kSong);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.RunFor(1000);
    w.applied.clear();
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(heard.ends[kHero] == std::vector<std::string>{"keeping up its mode"});
    CHECK(Lines(w, "Hero's turn is spent on its own mode (ability 1000), which keeps it at its command: End was "
                   "pressed, and the mode goes on") == 1);
    CHECK(Lines(w, "Hero's turn ends: keeping up its mode; 0.00 s rolls over") == 1);
    // The song goes on: the mode is on, its command still has her, and
    // nothing of hers was cleared.
    CHECK(w.Get(kHero).modes.contains(kSong));
    CHECK(w.Get(kHero).current == CommandType::ExecuteEffect);
    CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 0);
    // Her own clock moved a turn's seconds, as for any turn that ends: her
    // cooldowns, the song's upkeep.
    CHECK(w.agedOwn[kHero] == 4000);
    // And her next turn has a turn's own seconds: where a turn ended with
    // its bar whole rolls a second over (test_time.cpp), this one was spent.
    UntilTurnOf(w, kHero);
    CHECK(heard.bars[kHero] == std::vector<std::uint32_t>{4000, 4000});
    CHECK(w.machine.View().rolledMs == 0);
    // The song over (its stamina gone, say), the same End with the bar whole
    // rolls its second over, as any End does.
    w.Get(kHero).modes.erase(kSong);
    w.Finish(kHero);
    REQUIRE(w.machine.OnEndTurn());
    UntilTurnOf(w, kHero);
    CHECK(heard.ends[kHero] == std::vector<std::string>{"keeping up its mode", "ended by the player"});
    CHECK(heard.bars[kHero] == std::vector<std::uint32_t>{4000, 4000, 5000});
}

TEST_CASE("the mode switched off is free, and the turn then acts with its whole bar", "[ownmode]")
{
    // In play, ordered off at the very start of the turn: `Lyna is free of
    // the effect's command after 1.36 s, 1.30 s of its bar`, and what was
    // left was spent by then: stopping the song cost the turn too.
    Settings s = Fixed();
    World w(s);
    Heard heard(w);
    w.Get(kHero).modes.insert(kSong);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.RunFor(3000);
    REQUIRE(w.machine.View().timeLeftMs == 4000);
    CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK_FALSE(w.Get(kHero).modes.contains(kSong));
    w.RunFor(200);
    w.Finish(kHero); // the song's end played out
    w.RunFor(600);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(Lines(w, "is free of the effect's command") == 0);
    // And the turn is hers to act with: a swing of a second and a half.
    CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 2500);
    CHECK(heard.refused.empty());
}

TEST_CASE("the mode switched on in its own turn leaves the turn waiting for the player, not executing", "[ownmode]")
{
    // `Lyna: use ability (free, from player); 4.00 s and 19.6 m left`, the
    // song's command began, and the turn stood at executing until `still at
    // a execute effect after 108000 ms: stopped`, `Lyna's turn ends: took too
    // long`.
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    Heard heard(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    heard.awaited.clear();
    CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
    CHECK(w.machine.View().timeLeftMs == 4000);
    w.applied.clear();
    w.RunFor(1000);
    // From the frame after her own order the song's command has her, and
    // the turn waits again as after any command of hers.
    CHECK(w.Get(kHero).current == CommandType::ExecuteEffect);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(heard.awaited == std::vector<std::string>{"after its action"});
    // And goes on waiting, past any limit an action has.
    for (int ms = 0; ms < 120000; ms += 16)
    {
        w.Frame();
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    }
    CHECK(w.Current() == kHero);
    CHECK(w.machine.View().timeLeftMs == 4000);
    CHECK(heard.ends[kHero].empty());
    // It was never taken for her action's own effect, whose time runs with
    // it and comes off her bar.
    CHECK(Lines(w, "its action's own effect holds it") == 0);
    CHECK(Effects(w, EffectKind::AgeTime, kHero) == 0);
    CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 0);
    CHECK(Lines(w, "still at a") == 0);
    CHECK(Lines(w, "Hero is at its own mode's command (ability 1000), not held") == 1);
}

TEST_CASE("the mode switched on and End pressed before the song has begun: the turn is spent on it all the same",
          "[ownmode]")
{
    World w;
    Heard heard(w);
    Tellings told(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(heard.ends[kHero] == std::vector<std::string>{"keeping up its mode"});
    CHECK(w.Get(kHero).modes.contains(kSong));
    UntilTurnOf(w, kHero);
    CHECK(heard.bars[kHero] == std::vector<std::uint32_t>{4000, 4000});
}

TEST_CASE("an action or a move ordered at its own mode's command is refused before anything is spent", "[ownmode]")
{
    // In real time, the song on: "A move order then moved her 0.0 m and an
    // attack order struck nothing", and the attack was made as the song was
    // ordered off. Let through in turns, it would take its time of the bar
    // for nothing and leave a command to arrive later.
    World w;
    Heard heard(w);
    w.Get(kHero).modes.insert(kSong);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.applied.clear();
    SECTION("an attack: told why, over her and on the bar's caption")
    {
        CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Block);
        w.Frame();
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK(w.Said(L"Turn its mode off first"));
        CHECK(w.machine.View().notice == L"Turn its mode off first: Hero's attack is refused");
        REQUIRE(heard.refused.size() == 1);
        CHECK(heard.refused.front().who == kHero);
        CHECK(heard.refused.front().detail == "Turn its mode off first");
        CHECK(heard.refused.front().command == "attack");
        CHECK(heard.refused.front().note == "ability 1000 is on, and keeps it at its command");
        CHECK(w.machine.State().lastRefusal ==
              "Turn its mode off first (attack: ability 1000 is on, and keeps it at its command)");
        CHECK(Lines(w, "Hero: attack from player refused, it is at its own mode's command (ability 1000)") == 1);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    }
    SECTION("a move, and an item used")
    {
        CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 3.0f) == Verdict::Block);
        CHECK(w.Order(kHero, CommandType::UseItem, Source::Player) == Verdict::Block);
        w.Frame();
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK(w.machine.View().movementLeft == 21.0f);
        CHECK(w.Said(L"Turn its mode off first"));
        CHECK(heard.refused.size() == 2);
    }
    SECTION("her own AI's attack, as on any turn the player plays, with no word")
    {
        CHECK(Act(w, kHero, Source::Script, 1.5f) == Verdict::Block);
        w.Frame();
        CHECK_FALSE(w.Said(L"Turn its mode off first"));
        CHECK(heard.refused.empty());
    }
    SECTION("what is free goes through, and the turn waits again with its bar whole")
    {
        CHECK(w.Order(kHero, CommandType::SwitchWeaponSets, Source::Player) == Verdict::Allow);
        w.RunFor(1000);
        REQUIRE(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.View().timeLeftMs == 4000);
        CHECK(heard.refused.empty());
    }
    SECTION("the mode turned off, the same order is taken")
    {
        CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Block);
        CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Allow);
        CHECK(w.machine.View().timeLeftMs == 2500);
    }
}

TEST_CASE("an AI's singer's turn is spent on the mode at once, each turn, and its AI is never told to choose",
          "[ownmode]")
{
    SECTION("an enemy")
    {
        Settings s = Fixed();
        s.maxActionMs = 2000;
        s.maxTurnMs = 3000;
        World w(s);
        Heard heard(w);
        Tellings told(w);
        w.Get(kGenlock).modes.insert(kSong);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        w.applied.clear();
        for (int round = 0; round < 3; ++round)
        {
            UntilTurnOf(w, kHero);
        }
        CHECK(heard.ends[kGenlock] ==
              std::vector<std::string>{"keeping up its mode", "keeping up its mode", "keeping up its mode"});
        CHECK(heard.bars[kGenlock] == std::vector<std::uint32_t>{4000, 4000, 4000});
        // At once: a frame or two of the fight's clock, no bar run down in
        // real time, and no limit met.
        for (const std::uint32_t took : heard.took[kGenlock])
        {
            CHECK(took <= 2 * 16);
        }
        CHECK(told.Of(kGenlock) == 0);
        CHECK(told.Of(kHurlock) == 3);
        CHECK(Lines(w, "Genlock's turn is spent on its own mode (ability 1000), which keeps it at its command: its "
                       "tactics have it on, and its AI is not told to choose") == 3);
        CHECK(Lines(w, "Genlock's turn ends: keeping up its mode; 0.00 s rolls over") == 3);
        CHECK(Lines(w, "is held by an effect's command") == 0);
        CHECK(Lines(w, "took too long") == 0);
        CHECK(Lines(w, "chose nothing") == 0);
        // Its own clock a turn's seconds a turn; the song on, and nothing
        // of its cleared.
        CHECK(w.agedOwn[kGenlock] == std::int64_t{3} * 4000);
        CHECK(w.Get(kGenlock).modes.contains(kSong));
        CHECK(w.Get(kGenlock).current == CommandType::ExecuteEffect);
        CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
    }
    SECTION("a companion whose tactics play its turns")
    {
        Settings s = Fixed();
        s.partyControl = PartyControl::Tactics;
        World w(s);
        Heard heard(w);
        Tellings told(w);
        w.Get(kCompanion).modes.insert(kSong);
        w.StartFight();
        UntilTurnOf(w, kHero);
        CHECK(heard.ends[kCompanion] == std::vector<std::string>{"keeping up its mode"});
        CHECK(heard.took[kCompanion].front() <= 2 * 16);
        CHECK(told.Of(kCompanion) == 0);
        CHECK(Lines(w, "round 1: Alistair's turn (party, AI)") == 1);
    }
}

TEST_CASE("an AI that turns its mode on has that turn spent as its creature is found at the mode's command",
          "[ownmode]")
{
    SECTION("by its own command, in its own turn")
    {
        World w;
        Heard heard(w);
        Tellings told(w);
        // Its tactics open with the song, which costs nothing.
        told.choice[kGenlock] = [&w] { CHECK(w.Switch(kGenlock, Source::Script, kSong) == Verdict::Allow); };
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        UntilTurnOf(w, kGenlock);
        w.RunFor(100);
        CHECK(w.Current() != kGenlock);
        CHECK(heard.ends[kGenlock] == std::vector<std::string>{"keeping up its mode"});
        CHECK(heard.took[kGenlock].front() <= 3 * 16);
        // Told once, as its turn came; not again with the song on, on this
        // turn or the next.
        CHECK(told.Of(kGenlock) == 1);
        UntilTurnOf(w, kHero);
        UntilTurnOf(w, kHero);
        CHECK(told.Of(kGenlock) == 1);
        CHECK(heard.ends[kGenlock] == std::vector<std::string>{"keeping up its mode", "keeping up its mode"});
    }
    SECTION("found so while its AI is thinking")
    {
        Settings s = Fixed();
        s.aiThinkMs = 1500;
        World w(s);
        Heard heard(w);
        Tellings told(w);
        told.choice[kGenlock] = [] {}; // told, it queues nothing
        w.StartFight();
        UntilTurnOf(w, kGenlock);
        w.RunFor(200);
        REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
        w.Get(kGenlock).modes.insert(kSong); // its script's own doing, not a command of the turn's
        w.RunFor(3 * 16);
        CHECK(w.Current() != kGenlock);
        CHECK(heard.ends[kGenlock] == std::vector<std::string>{"keeping up its mode"});
        CHECK(Lines(w, "is held by an effect's command") == 0);
        CHECK(Lines(w, "chose nothing") == 0);
    }
}

TEST_CASE("another's hold on a creature with a mode on is a hold all the same, on both kinds of turn", "[ownmode]")
{
    // Every mode puts effects of its own making on its user. Read as "at its
    // own mode's command" for those, a warrior with Shield Wall on, thrown
    // by a dragon, would have a turn its tactics play spent at once (the
    // loss issue 30 mended), and on the player's the knockback would cost
    // nothing.
    SECTION("a turn its AI plays: the hold runs on its bar, and the turn is lost once it outlasts it")
    {
        World w;
        Heard heard(w);
        Tellings told(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        w.Get(kGenlock).modes.insert(kShieldWall);
        Throw(w, kGenlock);
        UntilTurnOf(w, kGenlock);
        w.RunFor(2000);
        CHECK(w.Current() == kGenlock); // not spent at once: held, its bar running
        CHECK(Lines(w, "Genlock is held by an effect's command, ") == 1);
        w.RunFor(2200);
        CHECK(w.Current() != kGenlock);
        CHECK(heard.ends[kGenlock] == std::vector<std::string>{"held by an effect"});
        CHECK(told.Of(kGenlock) == 0);
        CHECK(w.Get(kGenlock).modes.contains(kShieldWall));
    }
    SECTION("a turn the player plays: its orders are refused as held, and the time it held is gone from the bar")
    {
        World w;
        Heard heard(w);
        w.Get(kHero).modes.insert(kShieldWall);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        Throw(w, kHero);
        w.RunFor(1500);
        CHECK(w.machine.GetPhase() == Phase::Executing);
        CHECK(w.machine.State().heldByEffect);
        CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Block);
        w.Frame();
        CHECK(w.Said(L"Held"));
        CHECK_FALSE(w.Said(L"Turn its mode off first"));
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Lines(w, "Hero is free of the effect's command after ") == 1);
        CHECK(w.machine.View().timeLeftMs < 2600);
        CHECK(w.machine.View().timeLeftMs > 2300);
        CHECK(Lines(w, "at its own mode's command") == 0);
    }
    SECTION("a turn the player plays, the hold outlasting the bar: lost, as it is the game taking the turn")
    {
        World w;
        Heard heard(w);
        w.Get(kHero).modes.insert(kShieldWall);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        Throw(w, kHero);
        w.RunFor(4300);
        CHECK(w.Current() != kHero);
        CHECK(heard.ends[kHero] == std::vector<std::string>{"held by an effect"});
        CHECK(w.Get(kHero).modes.contains(kShieldWall));
    }
}

TEST_CASE("held by another's effect, a creature that turns its own mode on is free of the hold and at its mode",
          "[ownmode]")
{
    // A mode is free to turn, held or not. From then the engine has her at
    // the mode's command: what the hold ran is the hold's, and the turn
    // waits on the mode.
    World w;
    Heard heard(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    Throw(w, kHero);
    w.RunFor(1000);
    REQUIRE(w.machine.State().heldByEffect);
    CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
    w.RunFor(1000);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.machine.State().heldByEffect);
    CHECK(Lines(w, "Hero is free of the effect's command after ") == 1);
    const std::uint32_t left = w.machine.View().timeLeftMs;
    CHECK(left < 3100);
    CHECK(left > 2800);
    // And the bar stands there, however long she sings.
    w.RunFor(10000);
    CHECK(w.machine.View().timeLeftMs == left);
    w.applied.clear();
    CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Block);
    w.Frame();
    CHECK(w.Said(L"Turn its mode off first"));
    CHECK_FALSE(w.Said(L"Held"));
}

TEST_CASE("a singer a condition holds loses its turn to the condition, as anyone", "[ownmode]")
{
    // The five conditions are read apart from what command a creature is
    // at, and take its turn before anything is asked of its mode.
    World w;
    Heard heard(w);
    w.Get(kHero).modes.insert(kSong);
    w.Get(kHero).condition = Condition::Disabled;
    w.StartFight();
    CHECK(w.Said(L"Hero loses the turn"));
    CHECK(w.Current() != kHero);
    CHECK(heard.ends[kHero] == std::vector<std::string>{"held by its condition"});
    CHECK(w.Get(kHero).modes.contains(kSong));
}

TEST_CASE("a player's action that never ends is not stopped at max_action_ms: End stops it, and ends the turn",
          "[ownmode][limit]")
{
    // Until issue 51 `still at a attack after 36000 ms: stopped` and `took
    // too long` ended the player's turn as an AI's (issue 40's stuck attack).
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    Heard heard(w);
    Tellings told(w);
    w.Get(kHero).modes.insert(kShieldWall);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Act(w, kHero, Source::Player, 3.5f) == Verdict::Allow);
    REQUIRE(w.machine.View().timeLeftMs == 500);
    w.applied.clear();
    w.RunFor(60000);
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::Executing);
    CHECK(w.Get(kHero).busy);
    CHECK(heard.ends[kHero].empty());
    CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 0);
    CHECK(Lines(w, "took too long") == 0);
    CHECK(Lines(w, "Hero is still at a attack after 2000 ms: its turn is the player's, and is not ended for it") == 1);
    // End: the swing is stopped, and the turn over with that step.
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(heard.ends[kHero] == std::vector<std::string>{"ended by the player"});
    CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 1);
    CHECK_FALSE(w.Get(kHero).busy);
    CHECK(Lines(w, "Hero's attack is stopped: its turn was ended while it ran") == 1);
    // No mode was switched off by it.
    CHECK(w.Get(kHero).modes.contains(kShieldWall));
    // What the swing had spent stays spent: the half second left rolls
    // over, and no more (given back, a whole second would).
    UntilTurnOf(w, kHero);
    CHECK(heard.bars[kHero] == std::vector<std::uint32_t>{4000, 4500});
}

TEST_CASE("that a player's action stands past max_action_ms is said once an action, and of each that does",
          "[ownmode][limit]")
{
    Settings s = Fixed();
    s.maxActionMs = 1000;
    World w(s);
    Heard heard(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (int swing = 1; swing <= 2; ++swing)
    {
        CAPTURE(swing);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Act(w, kHero, Source::Player, 1.0f) == Verdict::Allow);
        w.RunFor(5000);
        REQUIRE(w.Current() == kHero);
        CHECK(Lines(w, "Hero is still at a attack after 1000 ms: its turn is the player's") == swing);
        w.Finish(kHero);
        w.RunFor(600);
    }
    CHECK(heard.ends[kHero].empty());
}

TEST_CASE("End pressed in the middle of an action stops it and ends the turn at once", "[ownmode]")
{
    SECTION("a walk: she stands where she was stopped, and nothing of the turn arrives later")
    {
        World w;
        Heard heard(w);
        Tellings told(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        CHECK(w.Order(kHero, CommandType::MoveToLocation, Source::Player, 10.0f) == Verdict::Allow);
        w.Get(kHero).moving = true;
        for (int step = 0; step < 6; ++step)
        {
            w.Get(kHero).position.x += 0.5f;
            w.Frame();
        }
        w.applied.clear();
        REQUIRE(w.machine.OnEndTurn());
        w.Frame();
        CHECK(w.Current() != kHero);
        CHECK(heard.ends[kHero] == std::vector<std::string>{"ended by the player"});
        CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 1);
        CHECK_FALSE(w.Get(kHero).busy);
        CHECK_FALSE(w.Get(kHero).moving);
        CHECK(Lines(w, "Hero's move to location is stopped: its turn was ended while it ran") == 1);
    }
    SECTION("a command let through and not yet begun is cleared with it")
    {
        World w;
        Heard heard(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        CHECK(Act(w, kHero, Source::Player, 1.5f) == Verdict::Allow);
        w.Get(kHero).current = CommandType::Invalid; // queued, and the engine has not begun it
        w.applied.clear();
        REQUIRE(w.machine.OnEndTurn());
        w.Frame();
        CHECK(w.Current() != kHero);
        CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 1);
        CHECK_FALSE(w.Get(kHero).busy);
        CHECK(Lines(w, "Hero's command, not yet begun, is stopped") == 1);
    }
    SECTION("at what the engine put on her, the turn ends at once and the command is the engine's to end")
    {
        World w;
        Heard heard(w);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        Throw(w, kHero);
        w.RunFor(100);
        w.applied.clear();
        REQUIRE(w.machine.OnEndTurn());
        w.Frame();
        CHECK(w.Current() != kHero);
        CHECK(heard.ends[kHero] == std::vector<std::string>{"ended by the player"});
        CHECK(Effects(w, EffectKind::ClearCommands, kHero) == 0);
        CHECK(w.Get(kHero).current == CommandType::ExecuteEffect);
        CHECK(Lines(w, "is stopped: its turn was ended") == 0);
    }
}

TEST_CASE("an AI's action that never ends is stopped at max_action_ms, its commands cleared and its modes left on",
          "[ownmode][limit]")
{
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    Heard heard(w);
    Tellings told(w);
    told.choice[kGenlock] = [&w] { CHECK(Act(w, kGenlock, Source::Script, 1.0f) == Verdict::Allow); };
    w.Get(kGenlock).modes.insert(kShieldWall);
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.applied.clear();
    w.RunFor(1800);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
    w.RunFor(400);
    CHECK(w.Current() != kGenlock);
    CHECK(heard.ends[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "still at a attack after 2000 ms: stopped") == 1);
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 1);
    CHECK_FALSE(w.Get(kGenlock).busy);
    CHECK(w.Get(kGenlock).modes.contains(kShieldWall));
}

TEST_CASE("an AI's turn that never ends is stopped at max_turn_ms, its commands cleared and its modes left on",
          "[ownmode][limit]")
{
    Settings s = Fixed();
    s.maxTurnMs = 3000; // and max_action_ms twelve seconds
    World w(s);
    Heard heard(w);
    Tellings told(w);
    told.choice[kGenlock] = [&w] { CHECK(Act(w, kGenlock, Source::Script, 1.0f) == Verdict::Allow); };
    w.Get(kGenlock).modes.insert(kShieldWall);
    w.StartFight();
    UntilTurnOf(w, kGenlock);
    w.applied.clear();
    w.RunFor(2600);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 0);
    w.RunFor(600);
    CHECK(w.Current() != kGenlock);
    CHECK(heard.ends[kGenlock] == std::vector<std::string>{"took too long"});
    CHECK(Lines(w, "Genlock's turn is still going after 3000 ms, ") == 1);
    CHECK(Effects(w, EffectKind::ClearCommands, kGenlock) == 1);
    CHECK_FALSE(w.Get(kGenlock).busy);
    CHECK(w.Get(kGenlock).modes.contains(kShieldWall));
}

TEST_CASE("a companion's turn its tactics play keeps the action's limit; chosen back, its next turn has none",
          "[ownmode][limit]")
{
    // "unless they're letting tactics run the actor": by whose turn it is as
    // the machine began it (issue 46).
    Settings s = Fixed();
    s.maxActionMs = 2000;
    World w(s);
    Heard heard(w);
    Tellings told(w);
    w.Get(kCompanion).tag = L"gen00fl_alistair";
    const Follower alistair{kCompanion, L"Alistair", L"gen00fl_alistair", false};
    (void)w.machine.ChooseTactics(alistair, true, "the case");
    told.choice[kCompanion] = [&w] { CHECK(Act(w, kCompanion, Source::Script, 1.0f) == Verdict::Allow); };
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    w.RunFor(2400);
    CHECK(w.Current() != kCompanion);
    CHECK(heard.ends[kCompanion] == std::vector<std::string>{"took too long"});
    // The player takes its turns back: the same swing, never ending, is
    // theirs to end.
    (void)w.machine.ChooseTactics(alistair, false, "the case");
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(Act(w, kCompanion, Source::Player, 1.0f) == Verdict::Allow);
    w.RunFor(20000);
    CHECK(w.Current() == kCompanion);
    CHECK(heard.ends[kCompanion] == std::vector<std::string>{"took too long"});
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    CHECK(heard.ends[kCompanion] == std::vector<std::string>{"took too long", "ended by the player"});
}
