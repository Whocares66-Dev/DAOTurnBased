#include "core/Keys.h"

#include <array>
#include <cctype>
#include <format>
#include <string>

namespace tb
{

namespace
{

struct Named
{
    std::string_view name;
    std::uint8_t key;
};

// winuser.h's values.
constexpr std::array kNamed{
    Named{"Space", 0x20},  Named{"Tab", 0x09},      Named{"Enter", 0x0D}, Named{"Backspace", 0x08},
    Named{"Insert", 0x2D}, Named{"Delete", 0x2E},   Named{"Home", 0x24},  Named{"End", 0x23},
    Named{"PageUp", 0x21}, Named{"PageDown", 0x22}, Named{"Pause", 0x13}, Named{"ScrollLock", 0x91},
    Named{"Up", 0x26},     Named{"Down", 0x28},     Named{"Left", 0x25},  Named{"Right", 0x27},
};

constexpr std::uint8_t kF1 = 0x70;
constexpr std::uint8_t kNumPad0 = 0x60;

std::string Lower(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c != ' ' && c != '\t')
        {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::optional<int> Number(std::string_view text)
{
    if (text.empty() || text.size() > 2)
    {
        return std::nullopt;
    }
    int n = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return std::nullopt;
        }
        n = (n * 10) + (c - '0');
    }
    return n;
}

// A key of one character: a letter or a digit.
std::optional<std::uint8_t> OneCharacter(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        return static_cast<std::uint8_t>('A' + (c - 'a'));
    }
    if (c >= '0' && c <= '9')
    {
        return static_cast<std::uint8_t>(c);
    }
    return std::nullopt;
}

// A function key, "f1" to "f24".
std::optional<std::uint8_t> FunctionKey(std::string_view name)
{
    if (name[0] != 'f')
    {
        return std::nullopt;
    }
    if (const auto n = Number(name.substr(1)); n && *n >= 1 && *n <= 24)
    {
        return static_cast<std::uint8_t>(kF1 + *n - 1);
    }
    return std::nullopt;
}

// A key by its VK_* code, as FormatKeyChord writes one it has no name for:
// "0xBA". Two hex digits, and not the code 0.
std::optional<std::uint8_t> ByCode(std::string_view name)
{
    const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    const int high = digit(name[2]);
    const int low = digit(name[3]);
    if (high >= 0 && low >= 0 && (high != 0 || low != 0))
    {
        return static_cast<std::uint8_t>((high * 16) + low);
    }
    return std::nullopt;
}

// A key of the number pad, "numpad0" to "numpad9".
std::optional<std::uint8_t> NumPadKey(std::string_view name)
{
    if (!name.starts_with("numpad"))
    {
        return std::nullopt;
    }
    if (const auto n = Number(name.substr(6)); n && *n <= 9)
    {
        return static_cast<std::uint8_t>(kNumPad0 + *n);
    }
    return std::nullopt;
}

// A key by the name the table has for it.
std::optional<std::uint8_t> ByName(std::string_view name)
{
    for (const auto &named : kNamed)
    {
        if (Lower(named.name) == name)
        {
            return named.key;
        }
    }
    return std::nullopt;
}

// A key by its name, in lower case: one character, a function key, a code,
// a key of the number pad, or one the table names. A name that begins as a
// function key's or the number pad's and is not one may still be the
// table's.
std::optional<std::uint8_t> Key(const std::string &name)
{
    if (name.empty())
    {
        return std::nullopt;
    }
    if (name.size() == 1)
    {
        return OneCharacter(name[0]);
    }
    if (const auto function = FunctionKey(name))
    {
        return function;
    }
    if (name.size() == 4 && name.starts_with("0x"))
    {
        return ByCode(name);
    }
    if (const auto pad = NumPadKey(name))
    {
        return pad;
    }
    return ByName(name);
}

} // namespace

std::optional<KeyChord> ParseKeyChord(std::string_view text)
{
    const std::string all = Lower(text);
    if (all == "none" || all.empty())
    {
        return KeyChord{};
    }
    KeyChord chord;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t plus = all.find('+', start);
        const std::string part = all.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        if (plus == std::string::npos)
        {
            const auto key = Key(part);
            if (!key)
            {
                return std::nullopt;
            }
            chord.key = *key;
            return chord;
        }
        if (part == "ctrl" || part == "control")
        {
            chord.ctrl = true;
        }
        else if (part == "shift")
        {
            chord.shift = true;
        }
        else if (part == "alt")
        {
            chord.alt = true;
        }
        else
        {
            return std::nullopt;
        }
        start = plus + 1;
    }
}

std::string FormatKeyChord(const KeyChord &chord)
{
    if (chord.key == 0)
    {
        return "None";
    }
    std::string out;
    if (chord.ctrl)
    {
        out += "Ctrl+";
    }
    if (chord.shift)
    {
        out += "Shift+";
    }
    if (chord.alt)
    {
        out += "Alt+";
    }
    const std::uint8_t k = chord.key;
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'))
    {
        out += static_cast<char>(k);
    }
    else if (k >= kF1 && k < kF1 + 24)
    {
        out += "F" + std::to_string(k - kF1 + 1);
    }
    else if (k >= kNumPad0 && k <= kNumPad0 + 9)
    {
        out += "NumPad" + std::to_string(k - kNumPad0);
    }
    else
    {
        for (const auto &named : kNamed)
        {
            if (named.key == k)
            {
                out += named.name;
                return out;
            }
        }
        out += std::format("0x{:02X}", k);
    }
    return out;
}

} // namespace tb
