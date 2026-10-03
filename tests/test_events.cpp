// The machine's typed events (core/Events.h): their JSON, and that a
// scripted fight emits them in the order a test would assert on.

#include "World.h"

#include "core/Events.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace tb;
using namespace tb::test;

TEST_CASE("an event is one line of JSON, its strings escaped", "[events]")
{
    TurnEvent event;
    event.kind = EventKind::Refused;
    event.worldMs = 1500;
    event.round = 2;
    event.who = 0x82;
    event.name = "Ae\"dan\\";
    event.detail = "Too far\n\x01";
    event.value = -3;
    CHECK(ToJson(event, 99) == R"({"ms":99,"world_ms":1500,"round":2,"event":"refused","id":"0x82",)"
                               R"("name":"Ae\"dan\\","detail":"Too far\n\u0001","value":-3})");
    event.command = "attack";
    event.note = "12.1 m to walk, 9.0 m left";
    CHECK(ToJson(event, 99).ends_with(R"("command":"attack","note":"12.1 m to walk, 9.0 m left","value":-3})"));
}

TEST_CASE("an event for no one leaves who out", "[events]")
{
    TurnEvent event;
    event.kind = EventKind::RoundStarted;
    event.round = 3;
    event.value = 3;
    CHECK(ToJson(event, 0) == R"({"ms":0,"world_ms":0,"round":3,"event":"round_started","value":3})");
}

TEST_CASE("a fight's events come in the order a test asserts on", "[events]")
{
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 30.0f) == Verdict::Block); // too far
    CHECK(w.machine.OnEndTurn());
    w.Frame();
    w.Frame();
    w.Get(kHurlock).down = true;
    w.Get(kGenlock).down = true;
    w.RunFor(1000);

    std::vector<std::string> kinds;
    for (const TurnEvent &e : events)
    {
        kinds.emplace_back(Name(e.kind));
    }
    REQUIRE(kinds.size() >= 7);
    CHECK(kinds[0] == "fight_began");
    CHECK(kinds[1] == "round_started");
    CHECK(events[1].value == 1);
    CHECK(kinds[2] == "turn_started");
    CHECK(events[2].who == kHero);
    CHECK(events[2].name == "Hero");
    CHECK(events[2].detail == "yours");
    CHECK(kinds[3] == "refused");
    CHECK(events[3].detail == "Too far");
    CHECK(events[3].command == "attack");
    CHECK(events[3].note == "30.0 m to walk, 9.0 m left");
    CHECK(kinds[4] == "turn_ended");
    CHECK(events[4].detail == "ended by the player");
    CHECK(std::ranges::count(kinds, std::string{"fell"}) == 2);
    CHECK(kinds.back() == "fight_over");
}

TEST_CASE("every event kind says what it means, for a test server's list", "[events]")
{
    for (int k = 0; k <= static_cast<int>(kLastEventKind); ++k)
    {
        const auto kind = static_cast<EventKind>(k);
        CAPTURE(Name(kind));
        const EventDescription d = Describe(kind);
        REQUIRE(d.meaning != nullptr);
        CHECK(std::string{d.meaning}.size() > 10);
        CHECK(std::string{Name(kind)} != "?");
    }
    CHECK(Describe(EventKind::Refused).command != nullptr);
    CHECK(Describe(EventKind::Refused).note != nullptr);
}
