#include "core/Text.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using tb::Utf8;

TEST_CASE("names in the log come out as UTF-8, whatever their script", "[text]")
{
    CHECK(Utf8(L"Alistair") == "Alistair");
    CHECK(Utf8(L"") == "");
    CHECK(Utf8(L"Zévran") == "Z\xc3\xa9vran"); // two bytes
    CHECK(Utf8(L"€5") == "\xe2\x82\xac"
                         "5");                        // three
    CHECK(Utf8(L"\U0001F409") == "\xf0\x9f\x90\x89"); // a surrogate pair, four
    // A lone half of a pair is not a character: the replacement one.
    CHECK(Utf8(std::wstring{L'a', static_cast<wchar_t>(0xD83D), L'b'}) == "a\xef\xbf\xbd"
                                                                          "b");
    CHECK(Utf8(std::wstring{static_cast<wchar_t>(0xDC00)}) == "\xef\xbf\xbd");
}
