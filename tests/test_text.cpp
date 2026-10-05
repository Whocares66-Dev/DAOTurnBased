#include "core/Text.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

using tb::FromUtf8;
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

TEST_CASE("text read back from a file of the mod's is what was written, or nothing", "[text]")
{
    for (const std::wstring_view text :
         {L"", L"gen00fl_alistair", L"Zévran", L"€5", L"\U0001F409", L"a\U0010FFFFz", L"߿ࠀ￿"})
    {
        CAPTURE(Utf8(text));
        CHECK(FromUtf8(Utf8(text)) == std::optional{std::wstring{text}});
    }
    // The four lengths, and the first character of each.
    CHECK(FromUtf8("\x7f") == std::optional{std::wstring{static_cast<wchar_t>(0x7F)}});
    CHECK(FromUtf8("\xc2\x80") == std::optional{std::wstring{static_cast<wchar_t>(0x80)}});
    CHECK(FromUtf8("\xe0\xa0\x80") == std::optional{std::wstring{static_cast<wchar_t>(0x800)}});
    CHECK(FromUtf8("\xf0\x90\x80\x80") ==
          std::optional{std::wstring{static_cast<wchar_t>(0xD800), static_cast<wchar_t>(0xDC00)}});
    // Not UTF-8: none of it is read.
    for (const std::string_view bytes : {
             "a\x80z",                // a byte that continues nothing
             "a\xff",                 // a byte that begins nothing
             "a\xf8\x88\x80\x80\x80", // five bytes
             "a\xc3",                 // cut short
             "a\xe2\x82",             //
             "a\xf0\x9f\x90",         //
             "a\xc3(",                // a byte that does not continue
             "a\xe2\x28\xac",         //
             "\xc0\x80",              // longer than it need be: two bytes for one
             "\xc1\xbf",              //
             "\xe0\x9f\xbf",          // three for two
             "\xf0\x8f\xbf\xbf",      // four for three
             "\xed\xa0\x80",          // half a surrogate pair
             "\xed\xbf\xbf",          //
             "\xf4\x90\x80\x80",      // past U+10FFFF
         })
    {
        CAPTURE(bytes);
        CHECK_FALSE(FromUtf8(bytes));
    }
}
