#include "core/I18n.h"

#include "core/Text.h"

#include <algorithm>
#include <cstdint>

namespace tb::i18n
{
namespace
{

// Replaced before anything is shown and read after; never changed in place.
Catalog g_catalog;

// A flat JSON object of strings, which is all a catalog is: nothing nested,
// every value a string. Anything else is said and left out.
class Reader
{
  public:
    Reader(std::string_view text, std::vector<std::string> &notes) : text_(text), notes_(notes)
    {
    }

    Catalog Read()
    {
        Catalog out;
        // A byte order mark, which an editor on Windows may add.
        if (text_.starts_with("\xEF\xBB\xBF"))
        {
            pos_ = 3;
        }
        Skip();
        if (!Take('{'))
        {
            notes_.emplace_back("not a JSON object");
            return out;
        }
        Skip();
        if (Take('}'))
        {
            return out;
        }
        while (true)
        {
            Skip();
            std::string key;
            if (!String(key))
            {
                notes_.emplace_back("not JSON: a key was expected");
                return out;
            }
            Skip();
            if (!Take(':'))
            {
                notes_.emplace_back("not JSON: a colon was expected after \"" + key + "\"");
                return out;
            }
            Skip();
            if (pos_ < text_.size() && text_[pos_] == '"')
            {
                std::string value;
                if (!String(value))
                {
                    notes_.emplace_back("not JSON: the string for \"" + key + "\" does not end");
                    return out;
                }
                const auto wideKey = FromUtf8(key);
                const auto wideValue = FromUtf8(value);
                if (!wideKey || !wideValue)
                {
                    notes_.push_back("\"" + key + "\" is not UTF-8");
                }
                else if (!wideValue->empty())
                {
                    out.insert_or_assign(*wideKey, *wideValue);
                }
            }
            else
            {
                notes_.push_back("\"" + key + "\" is not a string");
                if (!SkipValue())
                {
                    notes_.emplace_back("not JSON: a value does not end");
                    return out;
                }
            }
            Skip();
            if (Take(','))
            {
                continue;
            }
            if (!Take('}'))
            {
                notes_.emplace_back("not JSON: a comma or the closing brace was expected");
            }
            return out;
        }
    }

  private:
    void Skip()
    {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
        {
            ++pos_;
        }
    }

    bool Take(char c)
    {
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    static void Append(std::string &out, std::uint32_t c)
    {
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

    bool Hex4(std::uint32_t &out)
    {
        if (pos_ + 4 > text_.size())
        {
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9')
            {
                out |= static_cast<std::uint32_t>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    // A string's text, unescaped, into UTF-8. False if it does not end or
    // holds an escape JSON does not have.
    bool String(std::string &out)
    {
        if (!Take('"'))
        {
            return false;
        }
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
            {
                return true;
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
            {
                return false;
            }
            const char e = text_[pos_++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                std::uint32_t code = 0;
                if (!Hex4(code))
                {
                    return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(pos_, 2) == "\\u")
                {
                    const std::size_t before = pos_;
                    pos_ += 2;
                    std::uint32_t low = 0;
                    if (Hex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                    {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else
                    {
                        pos_ = before;
                    }
                }
                Append(out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    // A value that is not a string, to the comma or brace after it: a number,
    // a word, or a nested value, whose own strings and brackets are passed.
    bool SkipValue()
    {
        int depth = 0;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if (c == '"')
            {
                std::string ignored;
                if (!String(ignored))
                {
                    return false;
                }
                continue;
            }
            if (c == '{' || c == '[')
            {
                ++depth;
            }
            else if ((c == '}' || c == ']') && depth > 0)
            {
                --depth;
            }
            else if (depth == 0 && (c == ',' || c == '}'))
            {
                return true;
            }
            ++pos_;
        }
        return false;
    }

    std::string_view text_;
    std::vector<std::string> &notes_;
    std::size_t pos_ = 0;
};

char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string LowerCase(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), Lower);
    return out;
}

std::string_view Primary(std::string_view locale)
{
    return locale.substr(0, locale.find_first_of("-_"));
}

// A Chinese locale's script, which two catalogs of one language tell apart:
// "hant" for the traditional, else "hans".
std::string ChineseScript(std::string_view lower)
{
    for (const std::string_view traditional : {"hant", "tw", "hk", "mo"})
    {
        for (std::size_t at = lower.find(traditional); at != std::string_view::npos;
             at = lower.find(traditional, at + 1))
        {
            const bool starts = at == 0 || lower[at - 1] == '-' || lower[at - 1] == '_';
            const bool ends = at + traditional.size() == lower.size() || lower[at + traditional.size()] == '-' ||
                              lower[at + traditional.size()] == '_';
            if (starts && ends)
            {
                return "hant";
            }
        }
    }
    return "hans";
}

} // namespace

Catalog ParseCatalog(std::string_view json, std::vector<std::string> &notes)
{
    return Reader(json, notes).Read();
}

void Use(Catalog catalog)
{
    g_catalog = std::move(catalog);
}

std::size_t Lines()
{
    return g_catalog.size();
}

std::wstring_view Tr(std::wstring_view english)
{
    const auto found = g_catalog.find(english);
    return found == g_catalog.end() ? english : std::wstring_view{found->second};
}

std::wstring Words()
{
    std::wstring out;
    for (const auto &[english, translated] : g_catalog)
    {
        out += english;
        out += L'\x1F';
        out += translated;
        out += L'\x1E';
    }
    return out;
}

std::string PickLocale(std::string_view wanted, const std::vector<std::string> &available)
{
    const std::string want = LowerCase(wanted);
    if (want.empty())
    {
        return {};
    }
    for (const std::string &name : available)
    {
        if (LowerCase(name) == want)
        {
            return name;
        }
    }
    const std::string_view language = Primary(want);
    for (const std::string &name : available)
    {
        const std::string lower = LowerCase(name);
        if (Primary(lower) != language)
        {
            continue;
        }
        if (language == "zh" && ChineseScript(lower) != ChineseScript(want))
        {
            continue;
        }
        return name;
    }
    return {};
}

} // namespace tb::i18n
