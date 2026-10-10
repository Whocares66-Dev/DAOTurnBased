#include "core/Settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace tb
{

namespace
{

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0))
    {
        s.remove_suffix(1);
    }
    return s;
}

// All of `text` as a whole number, or false.
std::string Lower(std::string_view s)
{
    std::string out{s};
    std::ranges::transform(out, out.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return out;
}

// The value with an inline comment cut off.
std::string_view StripComment(std::string_view value)
{
    const std::size_t at = value.find_first_of(";#");
    return Trim(at == std::string_view::npos ? value : value.substr(0, at));
}

std::optional<bool> ToBool(std::string_view text)
{
    const std::string v = Lower(text);
    if (v == "true" || v == "1" || v == "yes" || v == "on")
    {
        return true;
    }
    if (v == "false" || v == "0" || v == "no" || v == "off")
    {
        return false;
    }
    return std::nullopt;
}

std::optional<float> ToFloat(std::string_view text)
{
    float value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

// One setting: where it is, and how its text becomes a field. The setter
// returns an empty string when it took the value, else what was wrong.
struct Entry
{
    std::string_view section;
    std::string_view key;
    std::function<std::string(Settings &, std::string_view)> set;
};

template <class T> std::string Clamped(T &field, T value, T lo, T hi)
{
    field = std::clamp(value, lo, hi);
    return field == value ? std::string{} : "out of range, clamped to " + std::to_string(field);
}

auto Bool(bool Settings::*field)
{
    return [field](Settings &s, std::string_view text) -> std::string {
        const auto v = ToBool(text);
        if (!v)
        {
            return "not true or false";
        }
        s.*field = *v;
        return {};
    };
}

auto Float(float Settings::*field, float lo, float hi)
{
    return [field, lo, hi](Settings &s, std::string_view text) -> std::string {
        const auto v = ToFloat(text);
        if (!v)
        {
            return "not a number";
        }
        return Clamped(s.*field, *v, lo, hi);
    };
}

// A key, by its name or its chord; `what` is said of a text that is none.
auto Chord(KeyChord Settings::*field, const char *what)
{
    return [field, what](Settings &s, std::string_view text) -> std::string {
        const auto chord = ParseKeyChord(text);
        if (!chord)
        {
            return what;
        }
        s.*field = *chord;
        return {};
    };
}

// [turns] party: who plays the party's turns.
std::string PartyPlay(Settings &s, std::string_view text)
{
    const std::string v = Lower(text);
    if (v == "manual")
    {
        s.partyControl = PartyControl::Manual;
    }
    else if (v == "tactics")
    {
        s.partyControl = PartyControl::Tactics;
    }
    else
    {
        return "not manual or tactics";
    }
    return {};
}

// Every setting the file may hold: its section, its key, and what takes its
// text. A table, and nothing but rows.
const std::vector<Entry> &Entries()
{
    static const std::vector<Entry> entries{
        {"turnbased", "enabled", Bool(&Settings::enabled)},
        {"turns", "party", PartyPlay},
        {"turns", "speed", Float(&Settings::fightSpeed, 0.25f, 4.0f)},
        {"feedback", "camera_follow", Bool(&Settings::cameraFollow)},
        {"keys", "toggle", Chord(&Settings::toggleKey, "not a key (None, Ctrl+T, F9, ...)")},
        {"keys", "end_turn", Chord(&Settings::endTurnKey, "not a key (Enter, E, F9, None, ...)")},
        {"keys", "delay", Chord(&Settings::delayKey, "not a key (None, D, Ctrl+D, F9, ...)")},
    };
    return entries;
}

// The lines of a text, each without its line ending.
std::vector<std::string_view> Lines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return lines;
}

// "[name]" -> "name", lowercased; nothing if the line is not a header.
std::optional<std::string> Header(std::string_view line)
{
    const std::string_view t = Trim(line);
    if (t.size() >= 2 && t.front() == '[')
    {
        const std::size_t close = t.find(']');
        if (close != std::string_view::npos)
        {
            return Lower(Trim(t.substr(1, close - 1)));
        }
    }
    return std::nullopt;
}

// The key of a "key = value" line, lowercased; nothing for a comment,
// a blank line or a header.
std::optional<std::string> KeyOf(std::string_view line)
{
    const std::string_view t = Trim(line);
    if (t.empty() || t.front() == ';' || t.front() == '#' || t.front() == '[')
    {
        return std::nullopt;
    }
    const std::size_t eq = t.find('=');
    if (eq == std::string_view::npos)
    {
        return std::nullopt;
    }
    return Lower(Trim(t.substr(0, eq)));
}

} // namespace

ParsedSettings ParseSettings(std::string_view ini)
{
    ParsedSettings out;
    std::string section;
    int number = 0;
    for (const std::string_view line : Lines(ini))
    {
        ++number;
        if (const auto header = Header(line))
        {
            section = *header;
            continue;
        }
        const auto key = KeyOf(line);
        if (!key)
        {
            const std::string_view t = Trim(line);
            if (!t.empty() && t.front() != ';' && t.front() != '#')
            {
                out.warnings.push_back(std::format("line {}: not key = value", number));
            }
            continue;
        }
        const std::string_view t = Trim(line);
        const std::string_view value = StripComment(t.substr(t.find('=') + 1));
        const auto &entries = Entries();
        const auto entry =
            std::ranges::find_if(entries, [&](const Entry &e) { return e.section == section && e.key == *key; });
        if (entry == entries.end())
        {
            out.warnings.push_back(std::format("line {}: unknown setting [{}] {}", number, section, *key));
            continue;
        }
        if (const std::string problem = entry->set(out.settings, value); !problem.empty())
        {
            out.warnings.push_back(std::format("line {}: [{}] {} = {}: {}", number, section, *key, value, problem));
        }
    }
    return out;
}

std::string DefaultSettingsFile()
{
    return R"(; Turn-based combat for Dragon Age: Origins (turnbased.dll).
; Read when the game starts, and again when it is changed.
; Unknown keys and bad values are named in turnbased.log, and the default kept.

[turnbased]
; Fights are turn-based. Changed only outside combat: by the box in the
; game's Options, Game, under "Turn-based", by the toggle key, or here.
enabled = true

[turns]
; Who commands the party on its turns:
;   manual   you, every party member
;   tactics  you the hero; the companions run their tactics
; This is what each companion starts as. Where a playthrough has its own
; choice for a companion (the box on its Tactics page, kept with its saves),
; that choice wins over this.
party = manual
; How fast the game runs through a turn-based fight, your turns and theirs
; alike: 0.5 half speed, 1 as ever, 2 twice as fast (0.25 to 4). It is the
; Fight speed slider in the game's Options (0.5x, 1x, 2x, 3x). Conversations
; and the world outside a fight run as ever.
speed = 1

[feedback]
; Centre the camera on whoever's turn it is.
camera_follow = true

[keys]
; Turn turn-based combat on or off, outside combat, as the box in the game's
; Options, Game, under "Turn-based" does. It has no key until one is bound.
; This key is one of the game's own: it is listed in the game's Options,
; Controls, under "Turn-based", and bound there like any other. What is written
; here is the key it has before that: the game copies it into its
; KeyBindings.ini the first time it starts with the mod, and goes by that
; file from then on (Restore Defaults in the controls puts this one back).
; A number pad key, Pause, or a key given by its code ("0xBA") is not the
; game's: it works as written here, read at start, and is not in the
; controls.
toggle = None
; Put a party member's turn off to later in the round, as the portrait bar's
; Delay does. One of the game's own keys too, in its controls under
; "Turn-based"; it has no key until one is bound there, or written here.
delay = None
; End a party member's turn, as the portrait bar's End does. One of the game's
; own keys too, bound in its controls under "Turn-based". The game's pause
; key (Space) is not this: in a turn-based fight it pauses the game and
; runs it again, as it does everywhere else.
end_turn = Enter
)";
}

namespace
{

// A setting as the default file has it: its section, its key, and its
// lines (the comment above it and the key's own).
struct DefaultEntry
{
    std::string section;
    std::string key;
    std::vector<std::string> lines;
};

std::vector<DefaultEntry> DefaultEntries()
{
    std::vector<DefaultEntry> entries;
    const std::string text = DefaultSettingsFile();
    std::string section;
    std::vector<std::string> comment;
    for (const std::string_view line : Lines(text))
    {
        if (const auto header = Header(line))
        {
            section = *header;
            comment.clear();
            continue;
        }
        const std::string_view trimmed = Trim(line);
        if (trimmed.starts_with(';'))
        {
            comment.emplace_back(line);
            continue;
        }
        const auto equals = trimmed.find('=');
        if (equals == std::string_view::npos)
        {
            comment.clear();
            continue;
        }
        DefaultEntry entry{section, Lower(Trim(trimmed.substr(0, equals))), comment};
        entry.lines.emplace_back(line);
        entries.push_back(std::move(entry));
        comment.clear();
    }
    return entries;
}

bool HasKey(std::string_view ini, std::string_view section, std::string_view key)
{
    std::string current;
    for (const std::string_view line : Lines(ini))
    {
        if (const auto header = Header(line))
        {
            current = *header;
            continue;
        }
        // A key the player commented out counts: leaving it out is theirs.
        std::string_view trimmed = Trim(line);
        while (trimmed.starts_with(';'))
        {
            trimmed = Trim(trimmed.substr(1));
        }
        const auto equals = trimmed.find('=');
        if (current == section && equals != std::string_view::npos && Lower(Trim(trimmed.substr(0, equals))) == key)
        {
            return true;
        }
    }
    return false;
}

// The lines inserted after the last line of a section (or a new section
// at the end).
std::string Inserted(std::string_view ini, std::string_view section, const std::vector<std::string> &add)
{
    const std::string eol = ini.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    for (const std::string_view line : Lines(ini))
    {
        lines.emplace_back(line);
    }
    if (!lines.empty() && lines.back().empty())
    {
        lines.pop_back();
    }
    std::string current;
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (const auto header = Header(lines[i]))
        {
            current = *header;
            if (current == section)
            {
                last = i;
            }
            continue;
        }
        if (current == section && !Trim(lines[i]).empty())
        {
            last = i;
        }
    }
    if (last)
    {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*last + 1), add.begin(), add.end());
    }
    else
    {
        if (!lines.empty())
        {
            lines.emplace_back();
        }
        lines.push_back("[" + std::string{section} + "]");
        lines.insert(lines.end(), add.begin(), add.end());
    }
    std::string out;
    for (const std::string &line : lines)
    {
        out += line;
        out += eol;
    }
    return out;
}

} // namespace

std::string WithMissingSettings(std::string_view ini)
{
    std::string out{ini};
    for (const DefaultEntry &entry : DefaultEntries())
    {
        if (!HasKey(out, entry.section, entry.key))
        {
            out = Inserted(out, entry.section, entry.lines);
        }
    }
    return out;
}

namespace
{

// Where a key is in a file's lines: its own line, if the file has one in
// the key's section; and the last line of that section that says anything
// (its header, if nothing else), which is where a line for the key goes
// after. Neither: the file has no such section.
struct KeyPlace
{
    std::optional<std::size_t> line;
    std::optional<std::size_t> lastInSection;
};

KeyPlace PlaceOf(const std::vector<std::string> &lines, const std::string &wantSection, const std::string &wantKey)
{
    KeyPlace place;
    std::string current;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (const auto header = Header(lines[i]))
        {
            current = *header;
            if (current == wantSection)
            {
                place.lastInSection = i;
            }
            continue;
        }
        if (current != wantSection)
        {
            continue;
        }
        if (!Trim(lines[i]).empty())
        {
            place.lastInSection = i;
        }
        if (KeyOf(lines[i]) == wantKey)
        {
            place.line = i;
            return place;
        }
    }
    return place;
}

} // namespace

std::string WithValue(std::string_view ini, std::string_view section, std::string_view key, std::string_view value)
{
    const std::string eol = ini.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    const std::string wantSection = Lower(section);
    const std::string wantKey = Lower(key);
    const std::string newLine = std::string{key} + " = " + std::string{value};

    std::vector<std::string> lines;
    for (const std::string_view line : Lines(ini))
    {
        lines.emplace_back(line);
    }
    // A text ending in a newline splits into a last empty line; it is put
    // back by the join. An empty text is no lines at all.
    const bool endsWithNewline = !ini.empty() && ini.back() == '\n';
    if (ini.empty() || (endsWithNewline && !lines.empty() && lines.back().empty()))
    {
        lines.pop_back();
    }

    const KeyPlace place = PlaceOf(lines, wantSection, wantKey);
    const bool replaced = place.line.has_value();
    if (place.line)
    {
        lines[*place.line] = newLine;
    }
    else if (place.lastInSection)
    {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*place.lastInSection + 1), newLine);
    }
    else
    {
        if (!lines.empty() && !Trim(lines.back()).empty())
        {
            lines.emplace_back();
        }
        lines.push_back("[" + std::string{section} + "]");
        lines.push_back(newLine);
    }

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        out += lines[i];
        if (i + 1 < lines.size() || endsWithNewline || !replaced)
        {
            out += eol;
        }
    }
    return out;
}

} // namespace tb
