// The little JSON the machine speaks to a test server (core/Json.h,
// core/State.h): fields read out of a tool's flat arguments, and the
// machine's whole state written.

#include "World.h"

#include "core/Json.h"
#include "core/State.h"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>

using namespace tb;
using namespace tb::test;

TEST_CASE("a flat object's fields are read, strings unescaped", "[json]")
{
    const std::string args = R"( { "section" : "turns", "key":"speed", "value": 2.5, "on": true,
                                   "text": "a \"quote\"\nA" } )";
    CHECK(JsonField(args, "section") == "turns");
    CHECK(JsonField(args, "key") == "speed");
    CHECK(JsonField(args, "value") == "2.5");
    CHECK(JsonField(args, "on") == "true");
    CHECK(JsonField(args, "text") == "a \"quote\"\nA");
    CHECK_FALSE(JsonField(args, "missing"));
}

TEST_CASE("what is not a flat object reads as nothing", "[json]")
{
    CHECK_FALSE(JsonField("", "a"));
    CHECK_FALSE(JsonField("[1, 2]", "a"));
    CHECK_FALSE(JsonField(R"({"a": {"b": 1}})", "a"));
    CHECK_FALSE(JsonField(R"({"a": "unterminated)", "a"));
    CHECK_FALSE(JsonField(R"({"a": "\uZZZZ"})", "a"));
    CHECK(JsonField("{}", "a") == std::nullopt);
}

TEST_CASE("a string is escaped as JSON wants", "[json]")
{
    std::string out;
    AppendJsonString(out, "a\"b\\c\n\t\x01"
                          "é");
    CHECK(out == "a\\\"b\\\\c\\n\\t\\u0001"
                 "é");
}

TEST_CASE("the machine's state says whose turn, what is held and what cools down", "[json]")
{
    World w;
    w.Get(kGenlock).condition = Condition::Disabled;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, 617, 20.0f, false, false) == Verdict::Allow);
    w.Finish(kHero);
    w.RunFor(600);
    const MachineState state = w.machine.State();
    const std::string json = ToJson(state);
    CHECK(json.starts_with(R"({"active":true,)"));
    CHECK(json.find(R"("current":{"id":"0x100","name":"Hero","side":"party","yours":true})") != std::string::npos);
    CHECK(json.find(R"("player_turn":true)") != std::string::npos);
    CHECK(json.find(R"({"id":"0x100","name":"Hero","side":"party")") != std::string::npos);
    // A hold whose seconds were not read: a turn's worth of its time. And the
    // talent's 20 s as the engine had them when the machine took them: all
    // but the frames since its command ended.
    CHECK(json.find(
              R"("held":[{"id":"0x200","name":"Genlock","condition":"disabled","on_clock":false,"owed_ms":4000}])") !=
          std::string::npos);
    REQUIRE(state.cooldowns.size() == 1);
    CHECK(state.cooldowns[0].leftMs > 19900);
    CHECK(json.find(std::format(R"("cooldowns":[{{"id":"0x100","ability":617,"left_ms":{},"of_ms":20000}}],)"
                                R"("charging":[]}})",
                                state.cooldowns[0].leftMs)) != std::string::npos);
    CHECK(json.ends_with("]}"));
}

TEST_CASE("an item's ability's cooldown is said with its item's tag", "[json]")
{
    MachineState state;
    state.cooldowns.push_back({0x100, 617, 12000, 20000, {}});
    state.cooldowns.push_back({0x100, 500, 3000, 5000, L"gen_im_qck_health_101"});
    CHECK(ToJson(state).find(R"("cooldowns":[{"id":"0x100","ability":617,"left_ms":12000,"of_ms":20000},)"
                             R"({"id":"0x100","ability":500,"left_ms":3000,"of_ms":5000,)"
                             R"("item":"gen_im_qck_health_101"}],"charging":[]})") != std::string::npos);
}

TEST_CASE("outside a fight the state says so", "[json]")
{
    World w;
    const std::string json = ToJson(w.machine.State());
    CHECK(json.starts_with(R"({"active":false,"phase":"idle","round":0,"world_ms":0,"current":null,)"));
    CHECK(json.find(R"("order":[],"held":[],"cooldowns":[],"charging":[]})") != std::string::npos);
    CHECK(json.find(R"("speed":1.00,"speed_took":null,)") != std::string::npos);
    MachineState taken = w.machine.State();
    taken.speedTook = false;
    CHECK(ToJson(taken).find(R"("speed_took":false,)") != std::string::npos);
}

TEST_CASE("a whole number is all of its text or nothing", "[json]")
{
    CHECK(WholeNumber("42") == std::uint64_t{42});
    CHECK(WholeNumber("0") == std::uint64_t{0});
    CHECK(WholeNumber("18446744073709551615") == std::uint64_t{18446744073709551615ULL});
    CHECK_FALSE(WholeNumber("18446744073709551616")); // one past what fits
    CHECK_FALSE(WholeNumber("42.5"));
    CHECK_FALSE(WholeNumber("42e2"));
    CHECK_FALSE(WholeNumber("42 or so"));
    CHECK_FALSE(WholeNumber("-1"));
    CHECK_FALSE(WholeNumber("+1"));
    CHECK_FALSE(WholeNumber(" 1"));
    CHECK_FALSE(WholeNumber(""));
    CHECK_FALSE(WholeNumber("true"));
    // As a tool's argument arrives: a number's own text, or a string's.
    CHECK(WholeNumber(*JsonField(R"({"seed": 7})", "seed")) == std::uint64_t{7});
    CHECK_FALSE(WholeNumber(*JsonField(R"({"seed": 7.5})", "seed")));
}
