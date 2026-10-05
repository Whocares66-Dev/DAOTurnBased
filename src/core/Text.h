// UTF-16 to UTF-8, for the log: the engine's names are wide strings. Here
// rather than DAOLib's dao::Utf8 so that src/core stays free of DAOLib. And
// back, for text of the engine's that the mod wrote into a file of its own
// and reads again (a follower's tag in a save: core/PartyTactics.h).
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace tb
{

inline std::string Utf8(std::wstring_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            ++i;
        }
        else if (c >= 0xD800 && c <= 0xDFFF)
        {
            c = 0xFFFD; // a lone surrogate
        }
        if (c < 0x80)
        {
            out += static_cast<char>(c);
        }
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// UTF-8 to UTF-16, all of it or nothing: nothing for bytes that are not
// UTF-8 as Utf8 writes it (a sequence cut short, a byte that begins or
// continues none, a character written longer than it need be, half a
// surrogate pair, anything past U+10FFFF). What is read is a file that may
// be damaged, and a damaged one is not half believed.
inline std::optional<std::wstring> FromUtf8(std::string_view text)
{
    // A sequence's length by its first byte, the bits of the character that
    // byte carries, and the least character that needs the length.
    struct Lead
    {
        unsigned char mask;
        unsigned char bits;
        std::size_t more;
        char32_t least;
    };
    constexpr std::array<Lead, 4> kLeads{
        {{0x80, 0x00, 0, 0x0}, {0xE0, 0xC0, 1, 0x80}, {0xF0, 0xE0, 2, 0x800}, {0xF8, 0xF0, 3, 0x10000}}};
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i]);
        const Lead *lead = nullptr;
        for (const Lead &known : kLeads)
        {
            if ((first & known.mask) == known.bits)
            {
                lead = &known;
                break;
            }
        }
        if (lead == nullptr || lead->more >= text.size() - i)
        {
            return std::nullopt;
        }
        auto c = static_cast<char32_t>(first & static_cast<unsigned char>(~lead->mask));
        for (std::size_t k = 1; k <= lead->more; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80)
            {
                return std::nullopt;
            }
            c = (c << 6) | (next & 0x3F);
        }
        if (c < lead->least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        {
            return std::nullopt;
        }
        if (c < 0x10000)
        {
            out += static_cast<wchar_t>(c);
        }
        else
        {
            c -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (c >> 10));
            out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
        }
        i += lead->more + 1;
    }
    return out;
}

} // namespace tb
