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

TEST_CASE("a key has the scan code the game's bindings know it by", "[keys]")
{
    using tb::ScanCode;
    CHECK(ScanCode(ParseKeyChord("Ctrl+T")->key) == 0x14); // the toggle's default
    CHECK(ScanCode('Q') == 0x10);
    CHECK(ScanCode('P') == 0x19);
    CHECK(ScanCode('A') == 0x1E);
    CHECK(ScanCode('L') == 0x26);
    CHECK(ScanCode('Z') == 0x2C);
    CHECK(ScanCode('M') == 0x32);
    CHECK(ScanCode('1') == 0x02);
    CHECK(ScanCode('9') == 0x0A);
    CHECK(ScanCode('0') == 0x0B); // after 9, as on the keyboard
    CHECK(ScanCode(ParseKeyChord("F1")->key) == 0x3B);
    CHECK(ScanCode(ParseKeyChord("F5")->key) == 0x3F); // the game's quicksave
    CHECK(ScanCode(ParseKeyChord("F10")->key) == 0x44);
    CHECK(ScanCode(ParseKeyChord("F11")->key) == 0x57); // not next to F10
    CHECK(ScanCode(ParseKeyChord("F12")->key) == 0x58);
    CHECK(ScanCode(ParseKeyChord("Space")->key) == 0x39); // the game's pause
    CHECK(ScanCode(ParseKeyChord("Enter")->key) == 0x1C);
    CHECK(ScanCode(ParseKeyChord("Delete")->key) == 0xD3);
    CHECK(ScanCode(ParseKeyChord("PageUp")->key) == 0xC9);
    CHECK(ScanCode(ParseKeyChord("Left")->key) == 0xCB);
}

TEST_CASE("a key the game's bindings are not known to take has no scan code", "[keys]")
{
    using tb::ScanCode;
    CHECK(ScanCode(0) == 0); // no key
    CHECK(ScanCode(ParseKeyChord("NumPad7")->key) == 0);
    CHECK(ScanCode(ParseKeyChord("Pause")->key) == 0);
    CHECK(ScanCode(ParseKeyChord("F13")->key) == 0);
    CHECK(ScanCode(ParseKeyChord("0xBA")->key) == 0);
    CHECK(ScanCode('a') == 0); // 0x61 is the number pad's 1, not a letter
}
