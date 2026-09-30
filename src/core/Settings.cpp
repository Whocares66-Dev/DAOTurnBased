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
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

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

std::optional<int> ToInt(std::string_view text)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
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

auto Int(int Settings::*field, int lo, int hi)
{
    return [field, lo, hi](Settings &s, std::string_view text) -> std::string {
        const auto v = ToInt(text);
        if (!v)
        {
            return "not a whole number";
        }
        return Clamped(s.*field, *v, lo, hi);
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

const std::vector<Entry> &Entries()
{
    static const std::vector<Entry> entries{
        {"turnbased", "enabled", Bool(&Settings::enabled)},
        {"initiative", "die", Int(&Settings::initiativeDie, 1, 100)},
        {"initiative", "per_dexterity", Float(&Settings::initiativePerDexterity, 0.0f, 5.0f)},
        {"movement", "rule",
         [](Settings &s, std::string_view text) -> std::string {
             const std::string v = Lower(text);
             if (v == "speed")
             {
                 s.movementRule = MovementRule::Speed;
             }
             else if (v == "constant")
             {
                 s.movementRule = MovementRule::Constant;
             }
             else
             {
                 return "not speed or constant";
             }
             return {};
         }},
        {"movement", "meters", Float(&Settings::movementMeters, 0.0f, 100.0f)},
        {"movement", "per_dexterity", Float(&Settings::movementPerDexterity, 0.0f, 5.0f)},
        {"movement", "min", Float(&Settings::movementMin, 0.0f, 100.0f)},
        {"movement", "max", Float(&Settings::movementMax, 0.0f, 100.0f)},
        {"turns", "party",
         [](Settings &s, std::string_view text) -> std::string {
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
         }},
        {"turns", "end_turn_when_action_used", Bool(&Settings::endTurnWhenActionUsed)},
        {"turns", "round_min_seconds", Float(&Settings::roundMinSeconds, 0.0f, 60.0f)},
        {"turns", "settle_ms", Int(&Settings::settleMs, 0, 5000)},
        {"turns", "ai_think_ms", Int(&Settings::aiThinkMs, 100, 10000)},
        {"turns", "ai_follow_up_ms", Int(&Settings::aiFollowUpMs, 0, 5000)},
        {"turns", "max_action_ms", Int(&Settings::maxActionMs, 1000, 60000)},
        {"turns", "max_commands_per_turn", Int(&Settings::maxCommandsPerTurn, 1, 50)},
        {"turns", "order_run_ms", Int(&Settings::orderRunMs, 0, 2000)},
        {"turns", "condition_turns", Int(&Settings::conditionTurns, 0, 5)},
        {"turns", "cooldown_seconds_per_turn", Float(&Settings::cooldownSecondsPerTurn, 0.0f, 60.0f)},
        {"turns", "pause_on_player_turn", Bool(&Settings::pauseOnPlayerTurn)},
        {"turns", "combat_radius", Float(&Settings::combatRadius, 5.0f, 200.0f)},
        {"turns", "ai_poke",
         [](Settings &s, std::string_view text) -> std::string {
             const std::string v = Lower(text);
             if (v == "command_complete")
             {
                 s.aiPoke = AiPoke::CommandComplete;
             }
             else if (v == "kickstart")
             {
                 s.aiPoke = AiPoke::Kickstart;
             }
             else
             {
                 return "not command_complete or kickstart";
             }
             return {};
         }},
        {"feedback", "floaties", Bool(&Settings::floaties)},
        {"feedback", "script_events", Bool(&Settings::scriptEvents)},
        {"feedback", "camera_follow", Bool(&Settings::cameraFollow)},
        {"feedback", "turn_bar", Bool(&Settings::turnBar)},
        {"feedback", "portraits", Bool(&Settings::portraits)},
        {"keys", "toggle",
         [](Settings &s, std::string_view text) -> std::string {
             const auto chord = ParseKeyChord(text);
             if (!chord)
             {
                 return "not a key (Ctrl+T, F9, None, ...)";
             }
             s.toggleKey = *chord;
             return {};
         }},
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
    if (out.settings.movementMin > out.settings.movementMax)
    {
        out.warnings.push_back("[movement] min is above max; max used for both");
        out.settings.movementMin = out.settings.movementMax;
    }
    return out;
}

std::string DefaultSettingsFile()
{
    return R"(; Turn-based combat for Dragon Age: Origins (turnbased.dll).
; Read when the game starts. The toggle key rewrites `enabled` here.
; Unknown keys and bad values are named in turnbased.log, and the default kept.

[turnbased]
; Fights are turn-based. Changed only outside combat, by the toggle key or here.
enabled = true

[initiative]
; Each combatant rolls 1..die at the start of a fight, plus per_dexterity for
; each point of Dexterity above 10. Highest goes first.
die = 20
per_dexterity = 0.25

[movement]
; How far a combatant may move in its turn, in meters.
;   speed     meters times how fast it moves against a person: a shriek
;             farther than a genlock, an ogre less far
;   constant  meters for everyone
rule = speed
meters = 9
; Extra meters per point of Dexterity above 10.
per_dexterity = 0
min = 3
max = 20

[turns]
; Who commands the party on its turns:
;   manual   you, every party member
;   tactics  you the hero; the companions run their tactics
party = manual
; End a party member's turn as soon as its action is spent, movement left or
; not: the next combatant's turn comes at once. False keeps the turn open to
; walk what is left, until the pause key ends it.
end_turn_when_action_used = true
; A round lasts at least this many seconds of game time, so effects and
; cooldowns tick about as fast in every fight. 0 is off.
round_min_seconds = 0
; How long the world runs on after an action (arrows landing, bodies falling).
settle_ms = 400
; An AI creature that has chosen nothing after this long passes its turn.
ai_think_ms = 1500
; After its action, how long an AI creature has to choose a move.
ai_follow_up_ms = 500
; An action still running after this long is stopped and the turn ends.
max_action_ms = 12000
; An AI creature that starts more commands than this in one turn is stopped
; there: a guard against an AI that loops.
max_commands_per_turn = 6
; An order that queued no command while paused runs the world this long, in
; case the engine queues it only when the game runs. In play the engine
; queued orders at once while paused (2026-09-30), and the orders that queue
; nothing (an ability picked before its target) would run the world for
; nothing: 0, off.
order_run_ms = 0
; Conditions count in turns, not seconds. A stun, paralysis or sleep costs
; the creature this many of its turns, the world still while it loses them;
; a pin, its movement for as many. When they are spent the mod ends the
; condition at the start of the creature's turn, and it acts. 0 leaves the
; game's timers in seconds, which run only while someone acts.
condition_turns = 1
; Cooldowns count in turns: an ability used is held until the start of its
; user's turn this many seconds of its cooldown later, rounded up (at 10,
; Shield Bash's 20 s is 2 turns, the mabari howl's 40 s 4). Waiting in real
; time no longer brings it back. A sustained ability costs nothing to turn
; on, and turned off it waits for its user's next turn. 0 leaves the game's
; timers in seconds, which run in real time, paused or not.
cooldown_seconds_per_turn = 10
; Pause the game while a party member's turn waits on your order. Off, the
; world idles on (breathing, torches, creatures shifting on their feet) and
; nobody acts out of turn, but what runs on the game's clock runs: damage
; over time and buffs tick, and stamina, mana and health come back, while
; you choose. Stuns, pins and cooldowns count turns either way.
pause_on_player_turn = false
; Creatures fighting within this many meters of the party leader take turns.
combat_radius = 50
; How an AI creature is told its turn has come: command_complete or kickstart.
ai_poke = command_complete

[feedback]
; The game's floating text: whose turn it is, out of movement, not your turn.
floaties = true
; Signal the module script events 77000-77004 (dev/DESIGN.md) for script mods.
script_events = true
; Experimental: centre the camera on whoever's turn it is.
camera_follow = false
; The turn order bar along the top of the screen: whose turn it is and who
; comes next. Its movie, turnorder.swf, goes in the override folder
; (tools/deploy.ps1). Off until it is seen in play (dev/UI.md).
turn_bar = false
; With the bar: the engine renders a portrait for each combatant the game
; has none for (enemies, allies), once a fight, as it renders the party's.
; Off: their cards show initials.
portraits = true

[keys]
; Turn turn-based combat on or off, outside combat. The game's pause key
; ends a party member's turn during a turn-based fight.
toggle = Ctrl+T
)";
}

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

    std::string current;
    std::optional<std::size_t> lastInSection;
    bool replaced = false;
    for (std::size_t i = 0; i < lines.size() && !replaced; ++i)
    {
        if (const auto header = Header(lines[i]))
        {
            current = *header;
            if (current == wantSection)
            {
                lastInSection = i;
            }
            continue;
        }
        if (current != wantSection)
        {
            continue;
        }
        if (!Trim(lines[i]).empty())
        {
            lastInSection = i;
        }
        if (KeyOf(lines[i]) == wantKey)
        {
            lines[i] = newLine;
            replaced = true;
        }
    }
    if (!replaced)
    {
        if (lastInSection)
        {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*lastInSection + 1), newLine);
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
