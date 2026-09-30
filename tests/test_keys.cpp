#include "core/Keys.h"

#include <catch2/catch_test_macros.hpp>

using tb::FormatKeyChord;
using tb::KeyChord;
using tb::ParseKeyChord;

TEST_CASE("key chords parse with modifiers in any case", "[keys]")
{
    const auto chord = ParseKeyChord("ctrl + shift + t");
    REQUIRE(chord);
    CHECK(chord->key == 'T');
    CHECK(chord->ctrl);
    CHECK(chord->shift);
    CHECK_FALSE(chord->alt);
}

TEST_CASE("function keys, the number pad and named keys have their VK codes", "[keys]")
{
    CHECK(ParseKeyChord("F1")->key == 0x70);
    CHECK(ParseKeyChord("F24")->key == 0x87);
    CHECK(ParseKeyChord("NumPad7")->key == 0x67);
    CHECK(ParseKeyChord("PageDown")->key == 0x22);
    CHECK(ParseKeyChord("Alt+Space")->key == 0x20);
    CHECK(ParseKeyChord("5")->key == '5');
}

TEST_CASE("None is no key; nonsense is nothing", "[keys]")
{
    CHECK(ParseKeyChord("None") == KeyChord{});
    CHECK_FALSE(ParseKeyChord("Ctrl+"));
    CHECK_FALSE(ParseKeyChord("Hyper+T"));
    CHECK_FALSE(ParseKeyChord("F25"));
    CHECK_FALSE(ParseKeyChord("Ctrl+TT"));
}

TEST_CASE("any key can be named by its code in hex, and is written back so", "[keys]")
{
    const auto chord = ParseKeyChord("Ctrl+0xBA"); // VK_OEM_1, the US ';'
    REQUIRE(chord);
    CHECK(chord->key == 0xBA);
    CHECK(chord->ctrl);
    CHECK(FormatKeyChord(*chord) == "Ctrl+0xBA");
    CHECK(FormatKeyChord(KeyChord{0x05}) == "0x05");
    // A named key given by code reads back by its name.
    CHECK(FormatKeyChord(*ParseKeyChord("0x41")) == "A");
    CHECK_FALSE(ParseKeyChord("0x00"));
    CHECK_FALSE(ParseKeyChord("0xZZ"));
    CHECK_FALSE(ParseKeyChord("0x1"));
}

TEST_CASE("a chord reads back as it is written", "[keys]")
{
    for (const char *text : {"Ctrl+T", "Shift+F10", "Ctrl+Alt+PageUp", "NumPad0", "Space", "None", "9"})
    {
        const auto chord = ParseKeyChord(text);
        REQUIRE(chord);
        CHECK(FormatKeyChord(*chord) == text);
    }
}
