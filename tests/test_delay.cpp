// Delay (Settings::delay; dev/REBALANCE.md, "Pillars as the reference"): a
// party member puts its turn off to the round's end before it has spent
// anything of it. What is promised: the turn it then takes is whole, the
// round after is in initiative's order again, and nothing counted its turn
// twice (the scripts' turn start, its cooldowns, its own clock).
#include "World.h"

#include "core/Bar.h"
#include "core/State.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

// Turns pass, the party's own ended by the pause key, until `id` waits on an
// order.
void UntilOrderOf(World &w, ObjectId id)
{
    for (int i = 0; i < 6000; ++i)
    {
        const bool awaiting = w.machine.GetPhase() == Phase::AwaitingOrder;
        if (w.Current() == id && awaiting)
        {
            return;
        }
        if (awaiting && w.machine.View().playerTurn)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    FAIL("its turn never came");
}

// The turn in hand ends, and turns pass until `id` waits on an order again.
void NextOrderOf(World &w, ObjectId id)
{
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 200 && w.Current() == id && w.machine.GetPhase() == Phase::AwaitingOrder; ++i)
    {
        w.Frame();
    }
    UntilOrderOf(w, id);
}

int TurnStartsToldOf(const World &w, ObjectId id)
{
    return static_cast<int>(std::ranges::count_if(w.applied, [&](const Effect &e) {
        return e.kind == EffectKind::Signal && e.event == events::kTurnStart && e.target == id;
    }));
}

Settings Timed()
{
    Settings s = Fixed();
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

} // namespace

TEST_CASE("a party member puts its turn off to the round's end, and takes it whole", "[delay]")
{
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.CanDelay());
    CHECK(w.machine.View().canDelay);
    REQUIRE(w.machine.OnDelay());
    w.Frame();
    w.Frame();
    // The genlock's turn, in the same round; the hero is last in it now.
    CHECK(w.Current() == kGenlock);
    CHECK(w.machine.Order().Round() == 1);
    CHECK(w.Said(L"Hero waits"));
    CHECK(std::ranges::any_of(events,
                              [](const TurnEvent &e) { return e.kind == EventKind::TurnDelayed && e.who == kHero; }));
    CHECK(w.machine.Order().All().back().id == kHero);
    // Her turn comes after the hurlock's, still in round 1, with all of it.
    UntilOrderOf(w, kHero);
    CHECK(w.machine.Order().Round() == 1);
    const TurnView late = w.machine.View();
    CHECK(late.timeLeftMs == late.timeMs);
    CHECK(late.timeMs == 4000);
    CHECK(late.movementLeft == 21.0f);
    // Once a round: she is last, and has waited already.
    CHECK_FALSE(late.canDelay);
    CHECK_FALSE(w.machine.OnDelay());
    // The round after is in initiative's order again: hers first.
    REQUIRE(w.machine.OnEndTurn());
    for (int i = 0; i < 400 && w.machine.Order().Round() == 1; ++i)
    {
        w.Frame();
    }
    UntilOrderOf(w, kHero);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(w.machine.Order().All().front().id == kHero);
    CHECK(w.machine.CanDelay());
    // One turn_started more than turn_ended for her, the one the delay closed.
    const auto count = [&](EventKind kind) {
        return std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == kHero; });
    };
    CHECK(count(EventKind::TurnStarted) == 3);
    CHECK(count(EventKind::TurnEnded) == 1);
    CHECK(count(EventKind::TurnDelayed) == 1);
}

TEST_CASE("a turn is not put off once anything of it is spent", "[delay]")
{
    SECTION("after a walk")
    {
        World w;
        w.StartFight();
        REQUIRE(w.Order(kHero, CommandType::MoveToLocation, Source::Player) == Verdict::Allow);
        w.Get(kHero).moving = true;
        for (int i = 0; i < 4; ++i)
        {
            w.Get(kHero).position.x += 0.5f;
            w.Frame();
        }
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        CHECK_FALSE(w.machine.CanDelay());
        CHECK_FALSE(w.machine.OnDelay());
    }
    SECTION("after an action")
    {
        World w;
        w.StartFight();
        REQUIRE(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK_FALSE(w.machine.CanDelay());
        CHECK_FALSE(w.machine.OnDelay());
    }
    SECTION("while its action plays out")
    {
        World w;
        w.StartFight();
        REQUIRE(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
        w.RunFor(100);
        CHECK_FALSE(w.machine.OnDelay());
    }
    // A command that takes nothing of the bar is a command of the turn all
    // the same: a mode switched on, which is free, and the turn is no longer
    // one to put off whole. (Until issue 50 the case for this was the action
    // clock's, where an action spent the action and no time: on the bar
    // every action moves the clock, and only what is free shows the rule.)
    SECTION("after a command that cost nothing")
    {
        World w;
        w.StartFight();
        const std::uint32_t whole = w.machine.View().timeLeftMs;
        REQUIRE(w.UseAbility(kHero, Source::Player, 604, 0.0f, true, false) == Verdict::Allow);
        w.RunFor(100);
        w.Finish(kHero);
        w.RunFor(600);
        REQUIRE(w.Current() == kHero);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        REQUIRE(w.machine.View().timeLeftMs == whole);
        CHECK_FALSE(w.machine.CanDelay());
        CHECK_FALSE(w.machine.OnDelay());
    }
}

TEST_CASE("nobody to wait for, an AI's turn, the setting off: no delay", "[delay]")
{
    SECTION("the last standing in the round has nobody to wait for")
    {
        World w;
        w.Get(kHurlock).down = true; // the only one after Alistair
        w.StartFight();
        UntilOrderOf(w, kCompanion);
        REQUIRE(w.Current() == kCompanion);
        CHECK_FALSE(w.machine.CanDelay());
        CHECK_FALSE(w.machine.OnDelay());
    }
    SECTION("an enemy's turn is not the player's to put off")
    {
        World w;
        w.StartFight();
        REQUIRE(w.machine.OnEndTurn());
        w.RunFor(100);
        REQUIRE(w.Current() == kGenlock);
        CHECK_FALSE(w.machine.OnDelay());
        w.RunFor(100);
        CHECK(w.Current() == kGenlock);
    }
    SECTION("with the setting off")
    {
        Settings s = Fixed();
        s.delay = false;
        World w(s);
        w.StartFight();
        CHECK_FALSE(w.machine.View().canDelay);
        CHECK_FALSE(w.machine.OnDelay());
        w.RunFor(100);
        CHECK(w.Current() == kHero);
    }
    SECTION("outside a fight")
    {
        World w;
        CHECK_FALSE(w.machine.OnDelay());
    }
}

TEST_CASE("a turn put off is one turn: the scripts are told once, and a cooldown counts it once", "[delay]")
{
    constexpr std::int32_t kShieldBash = 617; // 20 s of its user's own clock, 4 s of it a turn
    World w;
    w.StartFight();
    REQUIRE(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    const auto cooling = [&]() -> std::uint32_t {
        for (const CooldownView &c : w.machine.State().cooldowns)
        {
            if (c.id == kHero && c.ability == kShieldBash)
            {
                return c.leftMs;
            }
        }
        return 0;
    };
    // Round 2: she puts her turn off, and it comes again at the round's end.
    NextOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 2);
    const int told = TurnStartsToldOf(w, kHero);
    const std::uint32_t left = cooling();
    REQUIRE(left > 4000);
    REQUIRE(w.machine.OnDelay());
    w.RunFor(50);
    UntilOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 2);
    CHECK(TurnStartsToldOf(w, kHero) == told); // not told again
    CHECK(cooling() == left);                  // nor counted again: the turn has not passed yet
    // Round 3: that one turn has passed, once.
    NextOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 3);
    CHECK(cooling() == left - 4000);
    CHECK(TurnStartsToldOf(w, kHero) == told + 1);
}

TEST_CASE("with time, a turn put off keeps its bar, and its creature's clock moves once", "[delay][time]")
{
    constexpr std::int32_t kTalent = 617;
    World w(Timed());
    w.StartFight();
    // A talent of 2 s with 30 s to cool, and the rest of the turn banked: a
    // second rolls over.
    CommandRequest talent;
    talent.object = kHero;
    talent.type = CommandType::UseAbility;
    talent.source = Source::Player;
    talent.inCombat = true;
    talent.ability = kTalent;
    talent.cooldownSeconds = 30.0f;
    talent.actionSeconds = 2.0f;
    REQUIRE(w.Queue(talent) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    const auto cooling = [&]() -> std::uint32_t {
        for (const CooldownView &c : w.machine.State().cooldowns)
        {
            if (c.id == kHero && c.ability == kTalent)
            {
                return c.leftMs;
            }
        }
        return 0;
    };
    NextOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 2);
    const std::uint32_t bar = w.machine.View().timeMs;
    CHECK(bar == 5000); // 4 s and the second that rolled over
    const std::uint32_t left = cooling();
    REQUIRE(left > 8000);
    REQUIRE(w.machine.OnDelay());
    w.RunFor(50);
    UntilOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 2);
    // The bar she had, and a cooldown none the older.
    CHECK(w.machine.View().timeMs == bar);
    CHECK(w.machine.View().timeLeftMs == bar);
    CHECK(cooling() == left);
    // The turn ended unspent: her clock has moved its 4 s, once.
    NextOrderOf(w, kHero);
    REQUIRE(w.machine.Order().Round() == 3);
    CHECK(cooling() == left - 4000);
    CHECK(w.machine.View().timeMs == 5000);
}

TEST_CASE("one that put its turn off and falls before it comes takes no turn", "[delay]")
{
    World w;
    w.StartFight();
    REQUIRE(w.machine.OnDelay());
    w.RunFor(50);
    REQUIRE(w.Current() == kGenlock);
    w.Get(kHero).down = true;
    for (int i = 0; i < 4000 && w.machine.Order().Round() == 1; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder && w.machine.View().playerTurn)
        {
            CHECK(w.Current() != kHero);
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    // Round 2, in initiative's order: she is first again, and fallen, so
    // the turn is the genlock's.
    CHECK(w.machine.Order().Round() == 2);
    CHECK(w.machine.Order().All().front().id == kHero);
    CHECK_FALSE(w.machine.Order().Delayed(kHero));
}

TEST_CASE("the bar's string and the state say whether the turn may be put off", "[delay][bar]")
{
    World w;
    w.StartFight();
    const auto split = [](const std::wstring &text) {
        std::vector<std::wstring> fields;
        std::size_t at = 0;
        while (true)
        {
            const std::size_t next = text.find(kBarField, at);
            fields.push_back(text.substr(at, next == std::wstring::npos ? next : next - at));
            if (next == std::wstring::npos)
            {
                break;
            }
            at = next + 1;
        }
        return fields;
    };
    const auto head = [&] {
        const std::wstring bar = EncodeBar(w.machine.View());
        return split(bar.substr(0, bar.find(kBarCard)));
    };
    // Each card's fifteenth field: whether its creature waits for a turn put off.
    const auto waits = [&] {
        std::vector<std::wstring> last;
        const std::wstring bar = EncodeBar(w.machine.View());
        std::size_t at = bar.find(kBarCard);
        while (at != std::wstring::npos)
        {
            const std::size_t next = bar.find(kBarCard, at + 1);
            last.push_back(split(bar.substr(at + 1, next == std::wstring::npos ? next : next - at - 1))[14]);
            at = next;
        }
        return last;
    };
    REQUIRE(head().size() == 18);
    CHECK(head()[11] == L"1"); // to be had
    CHECK(waits() == std::vector<std::wstring>{L"0", L"0", L"0", L"0"});
    CHECK(ToJson(w.machine.State()).find(R"("can_delay":true)") != std::string::npos);
    REQUIRE(w.machine.OnDelay());
    w.RunFor(50);
    CHECK(head()[11] == L"0"); // the genlock's turn: not offered
    CHECK(ToJson(w.machine.State()).find(R"("can_delay":false)") != std::string::npos);
    // The hero's card is last in the round, and says she waits.
    CHECK(w.machine.View().upcoming.back().id == kHero);
    CHECK(waits() == std::vector<std::wstring>{L"0", L"0", L"0", L"1"});
    // Her turn come, offered and not to be had: the button keeps its place,
    // greyed. And she waits no longer.
    UntilOrderOf(w, kHero);
    CHECK(head()[11] == L"2");
    CHECK(w.machine.View().delayOffered);
    CHECK(waits().front() == L"0");
    CHECK(std::ranges::none_of(w.machine.View().upcoming, &CardView::waits));
}

TEST_CASE("with the setting off Delay is not offered", "[delay][bar]")
{
    Settings s = Fixed();
    s.delay = false;
    World w(s);
    w.StartFight();
    CHECK_FALSE(w.machine.View().delayOffered);
    // Delay, what rolled over and the pause, then the row's three (nobody
    // before the four, nobody after) and the caption's line, none, which
    // end the head.
    CHECK(EncodeBar(w.machine.View())
              .find(std::wstring{kBarField} + L"0" + kBarField + L"0.0" + kBarField + L"0" + kBarField + L"0" +
                    kBarField + L"4" + kBarField + L"0" + kBarField + kBarCard) != std::wstring::npos);
}

TEST_CASE("with time, the view says what of the bar rolled over", "[delay][time][bar]")
{
    World w(Timed());
    w.StartFight();
    CHECK(w.machine.View().rolledMs == 0);
    // A turn ended unspent: a second of it rolls over, and the next bar says so.
    NextOrderOf(w, kHero);
    const TurnView view = w.machine.View();
    CHECK(view.timeMs == 5000);
    CHECK(view.rolledMs == 1000);
    const std::wstring bar = EncodeBar(view);
    // What rolled over and the pause, before the row's three and the
    // caption's line, which is none.
    CHECK(bar.substr(0, bar.find(kBarCard))
              .ends_with(std::wstring{kBarField} + L"1.0" + kBarField + L"0" + kBarField + L"0" + kBarField + L"4" +
                         kBarField + L"0" + kBarField));
    CHECK(ToJson(w.machine.State()).find(R"("rolled_ms":1000)") != std::string::npos);
    // A turn that spends its bar rolls nothing over.
    CommandRequest swing;
    swing.object = kHero;
    swing.type = CommandType::Attack;
    swing.source = Source::Player;
    swing.inCombat = true;
    swing.actionSeconds = 5.0f;
    REQUIRE(w.Queue(swing) == Verdict::Allow);
    w.RunFor(100);
    w.Finish(kHero);
    w.RunFor(600);
    UntilOrderOf(w, kHero);
    CHECK(w.machine.View().timeMs == 4000);
    CHECK(w.machine.View().rolledMs == 0);
}
