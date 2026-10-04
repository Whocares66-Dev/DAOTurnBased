// What the player is told of an action given back (core/GivenBack.h; issue
// 21): the words for each of the engine's reasons, and the caption's line.
// The machine's side of it, when it says them and for how long, is
// test_time.cpp's, beside the giving back itself.
#include "core/GivenBack.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace tb;

TEST_CASE("each of the engine's reasons for refusing an order has its words", "[givenback]")
{
    // The command results that did nothing, as DAOLib names them
    // (dao::commands::ResultName; src/game/Gate.cpp, DidNothing).
    CHECK(WhyGivenBack("COMMAND_FAILED_PATH_ACTION_REQUIRED") == L"A door is in the way");
    CHECK(WhyGivenBack("COMMAND_FAILED_NO_SPACE_IN_MELEE_RING") == L"No room at the target");
    CHECK(WhyGivenBack("COMMAND_FAILED_INVALID_PATH") == L"No way through");
    CHECK(WhyGivenBack("COMMAND_FAILED_NO_LINE_OF_SIGHT") == L"No line of sight");
    // No reason to give: the engine's own shrug, the machine's for an action
    // over at once, a result with no name, nothing at all.
    CHECK(WhyGivenBack("COMMAND_FAILED_INVALID_DATA") == L"Nothing happened");
    CHECK(WhyGivenBack("over at once: it did nothing") == L"Nothing happened");
    CHECK(WhyGivenBack("result -11") == L"Nothing happened");
    CHECK(WhyGivenBack("").size() == std::wstring{L"Nothing happened"}.size());
}

TEST_CASE("the caption's line says why, whose it was, and that it is given back", "[givenback]")
{
    CHECK(GivenBackLine(L"No room at the target", L"Sten", CommandType::Attack) ==
          L"No room at the target: Sten's attack is given back");
    CHECK(GivenBackLine(L"A door is in the way", L"Morrigan", CommandType::UseAbility) ==
          L"A door is in the way: Morrigan's ability is given back");
    // Anything else that costs the action (a use of an object, an item).
    CHECK(GivenBackLine(L"No way through", L"Lyna", CommandType::UseObject) ==
          L"No way through: Lyna's action is given back");
    CHECK(GivenBackLine(L"Nothing happened", L"Lyna", CommandType::UseItem) ==
          L"Nothing happened: Lyna's action is given back");
}
