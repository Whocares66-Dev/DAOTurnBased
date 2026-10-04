// The game's pause key in a turn-based fight: it pauses, as it does in the
// game (the user, 2026-10-02: "space bar should still pause the game i
// think, instead of end turn. that's what players would expect"). The
// machine holds the game's pause through a fight, so it takes the key and
// pauses for the player; End turn is its own request.
#include "World.h"

#include "core/Bar.h"
#include "core/State.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace tb;
using namespace tb::test;

namespace
{

// The world idles on while the player chooses, as the mod ships.
Settings Idling()
{
    Settings s = Fixed();
    s.pauseOnPlayerTurn = false;
    return s;
}

} // namespace

TEST_CASE("the pause key pauses an enemy's turn, and the turn waits until it is pressed again", "[pause]")
{
    World w;
    w.StartFight();
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE_FALSE(w.paused);
    // Taken: the machine pauses for the player.
    REQUIRE(w.machine.OnPauseKey());
    CHECK(w.machine.PlayerPaused());
    w.Frame();
    CHECK(w.paused);
    CHECK(ToJson(w.machine.State()).find(R"("player_paused":true)") != std::string::npos);
    // The bar says so: its head's field before the row's three (nobody
    // before the four cards, nobody after), which the movie turns into the
    // word. Nothing else on the screen did (play test 29).
    CHECK(w.machine.View().playerPaused);
    const std::wstring row = std::wstring{kBarField} + L"0" + kBarField + L"4" + kBarField + L"0";
    const std::wstring bar = EncodeBar(w.machine.View());
    CHECK(bar.substr(0, bar.find(kBarCard)).ends_with(std::wstring{kBarField} + L"1" + row));
    // The genlock chooses nothing, and its turn would be over in a second
    // or two of the world's time: paused, it is still its turn.
    w.RunFor(8000);
    CHECK(w.paused);
    CHECK(w.Current() == kGenlock);
    // Again: the game runs, and the turn goes its way.
    REQUIRE(w.machine.OnPauseKey());
    CHECK_FALSE(w.machine.PlayerPaused());
    CHECK_FALSE(w.machine.View().playerPaused);
    const std::wstring again = EncodeBar(w.machine.View());
    CHECK(again.substr(0, again.find(kBarCard)).ends_with(std::wstring{kBarField} + L"0" + row));
    w.Frame();
    CHECK_FALSE(w.paused);
    w.RunFor(8000);
    CHECK(w.Current() != kGenlock);
}

TEST_CASE("the pause key does not end a turn; End does", "[pause]")
{
    World w(Idling());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE_FALSE(w.paused); // the world idles on while she chooses
    REQUIRE(w.machine.OnPauseKey());
    w.RunFor(500);
    CHECK(w.paused);
    CHECK(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.OnPauseKey());
    w.RunFor(500);
    CHECK_FALSE(w.paused);
    CHECK(w.Current() == kHero);
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(200);
    CHECK(w.Current() == kGenlock);
}

TEST_CASE("an order given while the player has the game paused runs it again", "[pause]")
{
    World w(Idling());
    w.StartFight();
    REQUIRE(w.machine.OnPauseKey());
    w.Frame();
    REQUIRE(w.paused);
    // A refused order leaves it paused.
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Player) == Verdict::Block);
    CHECK(w.machine.PlayerPaused());
    // One taken has to play out.
    REQUIRE(w.Order(kHero, CommandType::Attack, Source::Player) == Verdict::Allow);
    CHECK_FALSE(w.machine.PlayerPaused());
    w.Frame();
    CHECK_FALSE(w.paused);
}

TEST_CASE("while the machine holds the world still the pause key changes nothing", "[pause]")
{
    World w; // Fixed: paused while the player chooses
    w.StartFight();
    REQUIRE(w.paused);
    REQUIRE(w.machine.OnPauseKey()); // taken all the same: the game must not unpause
    CHECK_FALSE(w.machine.PlayerPaused());
    w.RunFor(200);
    CHECK(w.paused);
    CHECK(w.Current() == kHero);
    // And it did not arm a pause for the enemy's turn that follows.
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    CHECK_FALSE(w.paused);
}

TEST_CASE("outside a fight, and in a conversation over one, the pause key is the game's", "[pause]")
{
    World w;
    CHECK_FALSE(w.machine.OnPauseKey());
    CHECK_FALSE(w.machine.OnEndTurn());
    w.StartFight();
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    // The player's pause does not outlive a conversation that comes over
    // the fight, and the key there is the game's.
    REQUIRE(w.machine.OnPauseKey());
    w.Frame();
    w.mode = Mode::Dialog;
    w.RunFor(200);
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    CHECK_FALSE(w.machine.PlayerPaused());
    CHECK_FALSE(w.machine.OnPauseKey());
}

TEST_CASE("the player's pause does not outlive the fight", "[pause]")
{
    World w;
    w.StartFight();
    REQUIRE(w.machine.OnEndTurn());
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    REQUIRE(w.machine.OnPauseKey());
    w.Frame();
    REQUIRE(w.paused);
    // The enemies fall (a script's doing): the fight is over, and the game
    // is not left paused.
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.RunFor(3000);
    CHECK_FALSE(w.machine.Active());
    CHECK_FALSE(w.machine.PlayerPaused());
    CHECK_FALSE(w.paused);
}
