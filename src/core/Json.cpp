#include "core/Json.h"

#include <cctype>
#include <charconv>
#include <format>
#include <system_error>

namespace tb
{

void AppendJsonString(std::string &out, std::string_view text)
{
    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else
            {
                out += c;
            }
        }
    }
}

namespace
{

void SkipSpace(std::string_view json, std::size_t &at)
{
    while (at < json.size() && std::isspace(static_cast<unsigned char>(json[at])) != 0)
    {
        ++at;
    }
}

// A string at json[at] (its opening quote), unescaped; at moves past it.
std::optional<std::string> ReadString(std::string_view json, std::size_t &at)
{
    if (at >= json.size() || json[at] != '"')
    {
        return std::nullopt;
    }
    std::string out;
    for (++at; at < json.size(); ++at)
    {
        const char c = json[at];
        if (c == '"')
        {
            ++at;
            return out;
        }
        if (c != '\\')
        {
            out += c;
            continue;
        }
        if (++at >= json.size())
        {
            return std::nullopt;
        }
        switch (json[at])
        {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case 'u':
            // \uXXXX below 0x80 only: the arguments are ASCII keys and values.
            if (at + 4 < json.size())
            {
                const std::string_view digits = json.substr(at + 1, 4);
                unsigned code = 0;
                if (std::from_chars(digits.data(), digits.data() + digits.size(), code, 16).ec != std::errc{})
                {
                    return std::nullopt;
                }
                out += code < 0x80 ? static_cast<char>(code) : '?';
                at += 4;
            }
            break;
        default:
            out += json[at]; // \" \\ \/
        }
    }
    return std::nullopt;
}

// A number's, true's, false's or null's text at json[at]; at moves past it.
std::optional<std::string> ReadBare(std::string_view json, std::size_t &at)
{
    const std::size_t start = at;
    while (at < json.size() && (std::isalnum(static_cast<unsigned char>(json[at])) != 0 || json[at] == '-' ||
                                json[at] == '+' || json[at] == '.'))
    {
        ++at;
    }
    if (at == start)
    {
        return std::nullopt;
    }
    return std::string{json.substr(start, at - start)};
}

} // namespace

std::optional<std::string> JsonField(std::string_view json, std::string_view key)
{
    std::size_t at = 0;
    SkipSpace(json, at);
    if (at >= json.size() || json[at] != '{')
    {
        return std::nullopt;
    }
    ++at;
    while (true)
    {
        SkipSpace(json, at);
        if (at < json.size() && json[at] == '}')
        {
            return std::nullopt;
        }
        const std::optional<std::string> name = ReadString(json, at);
        SkipSpace(json, at);
        if (!name || at >= json.size() || json[at] != ':')
        {
            return std::nullopt;
        }
        ++at;
        SkipSpace(json, at);
        if (at >= json.size() || json[at] == '{' || json[at] == '[')
        {
            return std::nullopt; // nested: not this reader's
        }
        std::optional<std::string> value = json[at] == '"' ? ReadString(json, at) : ReadBare(json, at);
        if (!value)
        {
            return std::nullopt;
        }
        if (*name == key)
        {
            return value;
        }
        SkipSpace(json, at);
        if (at < json.size() && json[at] == ',')
        {
            ++at;
            continue;
        }
        return std::nullopt;
    }
}

} // namespace tb
