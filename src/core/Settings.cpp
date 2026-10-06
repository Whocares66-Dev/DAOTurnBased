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
bool WholeInt(std::string_view text, int &value)
{
    const std::from_chars_result got = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && got.ec == std::errc{} && got.ptr == text.data() + text.size();
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

// [turns] ai_poke: how an AI is told its turn has come.
std::string PokedBy(Settings &s, std::string_view text)
{
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
}

// An item of a list, without the spaces round it.
std::string_view Unspaced(std::string_view item)
{
    while (!item.empty() && item.front() == ' ')
    {
        item.remove_prefix(1);
    }
    while (!item.empty() && item.back() == ' ')
    {
        item.remove_suffix(1);
    }
    return item;
}

// [turns] charge, "10002:1, 13000:2": an ability's id and its turns.
std::string Charge(Settings &s, std::string_view text)
{
    std::vector<std::pair<std::int32_t, int>> list;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find(',', at);
        end = end == std::string_view::npos ? text.size() : end;
        const std::string_view item = Unspaced(text.substr(at, end - at));
        at = end + 1;
        if (item.empty())
        {
            continue;
        }
        const std::size_t colon = item.find(':');
        int ability = 0;
        int turns = 0;
        if (colon == std::string_view::npos || !WholeInt(item.substr(0, colon), ability) ||
            !WholeInt(item.substr(colon + 1), turns) || ability < 0 || turns < 0 || turns > 9)
        {
            return "not a list of ability:turns, turns 0 to 9 (10002:1, 13000:2)";
        }
        list.emplace_back(ability, turns);
    }
    s.charge = std::move(list);
    return {};
}

// [turns] enemy_tactics, "0, 0, 2, 4, 6": by rank, critter to elite boss.
std::string EnemyTactics(Settings &s, std::string_view text)
{
    std::array<int, 5> ranks{};
    std::size_t count = 0;
    std::size_t at = 0;
    while (at <= text.size())
    {
        std::size_t end = text.find(',', at);
        end = end == std::string_view::npos ? text.size() : end;
        const std::string_view item = Unspaced(text.substr(at, end - at));
        at = end + 1;
        int rank = 0;
        if (count >= ranks.size() || !WholeInt(item, rank) || rank < 0 || rank > 8)
        {
            return "not five ranks 0 to 8, critter to elite boss (0, 0, 2, 4, 6)";
        }
        ranks.at(count++) = rank;
        if (end == text.size())
        {
            break;
        }
    }
    if (count != ranks.size())
    {
        return "not five ranks 0 to 8, critter to elite boss (0, 0, 2, 4, 6)";
    }
    s.enemyTactics = ranks;
    return {};
}

// Every setting the file may hold: its section, its key, and what takes its
// text. A table, and nothing but rows.
const std::vector<Entry> &Entries()
{
    static const std::vector<Entry> entries{
        {"turnbased", "enabled", Bool(&Settings::enabled)},
        {"initiative", "die", Int(&Settings::initiativeDie, 1, 100)},
        {"initiative", "per_dexterity", Float(&Settings::initiativePerDexterity, 0.0f, 5.0f)},
        {"initiative", "per_survival", Float(&Settings::initiativePerSurvival, 0.0f, 10.0f)},
        {"turns", "party", PartyPlay},
        {"turns", "round_min_seconds", Float(&Settings::roundMinSeconds, 0.0f, 60.0f)},
        {"turns", "settle_ms", Int(&Settings::settleMs, 0, 5000)},
        {"turns", "ai_think_ms", Int(&Settings::aiThinkMs, 100, 10000)},
        {"turns", "ai_follow_up_ms", Int(&Settings::aiFollowUpMs, 0, 5000)},
        {"turns", "max_action_ms", Int(&Settings::maxActionMs, 1000, 60000)},
        {"turns", "max_turn_ms", Int(&Settings::maxTurnMs, 1000, 600000)},
        {"turns", "order_run_ms", Int(&Settings::orderRunMs, 0, 2000)},
        {"turns", "condition_turns", Int(&Settings::conditionTurns, 0, 5)},
        {"turns", "pause_on_player_turn", Bool(&Settings::pauseOnPlayerTurn)},
        {"turns", "pick_targets", Bool(&Settings::pickTargets)},
        {"turns", "speed", Float(&Settings::fightSpeed, 0.25f, 4.0f)},
        {"turns", "delay", Bool(&Settings::delay)},
        {"turns", "own_clocks", Bool(&Settings::ownClocks)},
        {"turns", "engine_conjure", Bool(&Settings::engineConjure)},
        {"turns", "give_back_unplayed", Bool(&Settings::giveBackUnplayed)},
        {"movement", "path_check", Bool(&Settings::pathCheck)},
        {"turns", "script_timers_from", Float(&Settings::scriptTimersFrom, 0.0f, 60.0f)},
        {"turns", "script_timers_trace", Bool(&Settings::scriptTimersTrace)},
        {"turns", "combat_radius", Float(&Settings::combatRadius, 5.0f, 200.0f)},
        {"turns", "join_by_sight", Bool(&Settings::joinBySight)},
        {"turns", "join_radius", Float(&Settings::joinRadius, 5.0f, 200.0f)},
        {"turns", "charge", Charge},
        {"turns", "ai_poke", PokedBy},
        {"turns", "seconds_per_turn", Float(&Settings::secondsPerTurn, 1.0f, 30.0f)},
        {"turns", "free_step", Float(&Settings::freeStepMeters, 0.0f, 10.0f)},
        {"turns", "pace", Float(&Settings::paceMps, 0.5f, 20.0f)},
        {"costs", "object", Float(&Settings::objectSeconds, 0.0f, 10.0f)},
        {"turns", "enemy_tactics", EnemyTactics},
        {"feedback", "floaties", Bool(&Settings::floaties)},
        {"feedback", "script_events", Bool(&Settings::scriptEvents)},
        {"feedback", "camera_follow", Bool(&Settings::cameraFollow)},
        {"feedback", "turn_bar", Bool(&Settings::turnBar)},
        {"feedback", "portraits", Bool(&Settings::portraits)},
        {"feedback", "move_preview", Bool(&Settings::movePreview)},
        {"feedback", "move_preview_ground", Bool(&Settings::movePreviewGround)},
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
; And per_survival for each rank of Survival.
per_survival = 2

[movement]
; How far a combatant may walk in its turn is the time it has left ([turns]
; seconds_per_turn) at its pace ([turns] pace), and a free step.
; Judge how far an attack or a move must walk by the game's own path,
; around walls and tables, not a straight line: an enemy 5 m off round a
; corner may be 14 m of walking. What the walk cannot reach this turn is
; refused, "Too far", before a step is taken, a click on the ground too.
; Off: a straight line for attacks, and a move walks until its time runs
; out.
path_check = true

[turns]
; Who commands the party on its turns:
;   manual   you, every party member
;   tactics  you the hero; the companions run their tactics
; This is what each companion starts as. Where a playthrough has its own
; choice for a companion (kept with its saves), that choice wins over this.
party = manual
; A round lasts at least this many seconds of game time, so effects and
; cooldowns tick about as fast in every fight. 0 is off.
round_min_seconds = 0
; How long the world runs on after an action (arrows landing, bodies falling).
settle_ms = 400
; An AI creature that has chosen nothing after this long passes its turn.
ai_think_ms = 1500
; After its action, how long an AI creature has to choose a move.
ai_follow_up_ms = 500
; On a turn an AI plays, an action still running after this long is stopped
; and the turn ends: one that stands still, not one the game is still playing
; out (a long finishing blow). Your own turns have no such limit: End stops
; an action that never ends.
max_action_ms = 12000
; A turn an AI plays that is still going after this long is ended, whatever
; it is doing. In play such a turn takes two or three seconds, and the
; longest seen thirteen.
max_turn_ms = 30000
; An order that queued no command while paused runs the world this long, in
; case the engine queues it only when the game runs. In play the engine
; queued orders at once while paused (2026-09-30), and the orders that queue
; nothing (an ability picked before its target) would run the world for
; nothing: 0, off.
order_run_ms = 0
; A stun, paralysis, sleep, pin or knockdown takes its own seconds of its
; victim's time: a turn's worth or more is a turn lost, the world still
; while it is lost, and less is that much of its next turn's bar. When they
; are spent the mod ends the condition at the start of the creature's turn,
; and it acts. Where the game does not say how long one lasts, it counts as
; this many turns; at 0 such a one costs every turn that comes while it
; lasts.
condition_turns = 1
; Pause the game while a party member's turn waits on your order. Off, the
; world idles on (breathing, torches, creatures shifting on their feet) and
; nobody acts out of turn, but what runs on the game's clock runs: damage
; over time and buffs tick, and stamina, mana and health come back, while
; you choose. Stuns, pins and cooldowns go by the turns either way.
pause_on_player_turn = false
; A talent chosen on the quickbar in a turn-based fight asks for its target,
; even with one selected. Off, the game uses it on the selected target at
; once, as in real time (a character keeps its target from turn to turn).
; Talents for their user alone, and modes, are used as ever.
pick_targets = true
; How fast the game runs through a turn-based fight, your turns and theirs
; alike: 0.5 half speed, 1 as ever, 2 twice as fast (0.25 to 4). It is the
; Fight speed slider in the game's options (0.5x, 1x, 2x, 3x). Conversations and the world
; outside a fight run as ever. At 1 the mod leaves the game's speed alone.
speed = 1
; A party member may put its turn off to the end of the round (Delay, on the
; turn bar beside End), once a round and only before it has moved or acted:
; a healer after the fighter, a mage after the enemy has closed. The turn it
; then takes is whole, and its cooldowns and effects are none the older.
delay = true
; What wears off, burns or regenerates on a creature does so on that
; creature's own clock, as its turn is spent: a poison ticks on its victim's
; turn, a buff of 20 seconds lasts five of its owner's turns. false:
; everyone's effects age together, a turn's seconds as each round begins.
own_clocks = true
; A spell that conjures (Inferno, Chain Lightning) and has not the time left
; this turn is begun at once, and its caster conjures through the other
; turns until its time is paid: the game's own conjuring, which a blow can
; interrupt by the game's own rule. false: the spell waits unbegun until its
; time is paid.
engine_conjure = true
; An action that ended at once, in under a quarter of the time it takes, did
; not happen (a spell at a target the game would not take), and its time is
; given back.
give_back_unplayed = true
; The scripts' own timers count rounds: through a fight, a script event
; sent with a delay of at least this many seconds waits on that clock and
; comes as the rounds spend it (a boss's timed attack, a plot's countdown),
; so that everyone has a turn before it runs out. Shorter delays, by which
; the scripts put one thing after another, run as they do. 0: only damage
; over time and lasting areas count rounds.
script_timers_from = 1
; Every script event the game delays said in the log, as it is sent and as
; it comes: for finding what times something.
script_timers_trace = false
; A creature fighting within this many meters of a living party member is
; taken into the turns. It takes nobody out of them: once in a fight a
; creature stays in it, however far it goes, for as long as the game's own
; perception keeps it there (an enemy while the party perceives it or it
; perceives the party, an ally while it perceives an enemy). That is the test
; the game itself ends a fight by when you run from one.
combat_radius = 50
; Of those, only the ones you can see fighting: an enemy or ally joins the
; turns once it stands in a room the party has seen (the game draws the
; rest black), or it and a party member attack each other. Until then it
; waits where it is, outside the fight, so a battle rooms away (Howe's men
; and the castle's guards) neither fills the turn order nor takes the
; camera to black rooms. Once in, it stays in. false: everyone in combat
; within combat_radius takes turns.
join_by_sight = true
; And of those you can see, one that is not fighting the party (neither
; attacking a party member nor attacked by one) joins only within this many
; meters of a party member: out of doors a battle of wolves and darkspawn
; 45 m away is in plain sight, and is not yours. It waits as it is until
; your fight is over, or until it comes for you.
join_radius = 30
; An ability made to cost whole turns of its caster's time, by its id and
; its turns: charge = 10002:1, 13000:2 (Inferno a turn's worth, Blizzard
; two). It is begun as it is ordered and lands when its caster's turns have
; paid for it; a stun, a knockdown, paralysis, sleep or death before then
; loses it. Empty: every ability costs the time the game takes for it.
charge =
; How an AI creature is told its turn has come: command_complete or kickstart.
ai_poke = command_complete
; What a turn is: a bar of time this many seconds long, that actions and
; walking both spend, each at its own length. A quick dagger strikes more
; often than a greatsword, a long spell lands on the next turn, and what is
; not spent rolls over a little. It is also what everything timed counts
; in. A creature's turn is this many seconds of its own clock: of its
; cooldowns (Shield Bash's 20 s is five turns at 4; waiting in real time
; does not bring it back; a sustained ability costs nothing to turn on, and
; turned off it waits a turn) and of what wears off, burns or regenerates
; on it. And a round is this many seconds of everything else: lasting
; areas, the scripts' timers.
seconds_per_turn = 4
; Meters a turn walked for nothing, to shuffle in a melee ring.
free_step = 1
; A person's walk in a fight, meters a second. A creature walks at this
; times its own pace: a shriek faster than a genlock, an ogre slower, and
; faster or slower again for Haste, a salve or a slow on it.
pace = 4.66
; The Combat Tactics rank enemies fight at, by their rank:
; critter, normal, lieutenant, boss, elite boss. Each even rank adds half
; a second to a turn; each odd one lets half a second more roll over. So a
; lieutenant has half a second more a turn, a boss a second, an elite boss a
; second and a half. 0 to 8.
enemy_tactics = 0, 0, 2, 4, 6

[costs]
; What the things that are not fighting cost of a turn's bar, in seconds: a
; door, a chest or a lever used, a lock, a thing picked up, taken or handed
; over. What does not fit is owed of the next turn. 0 for nothing.
object = 0.5

[feedback]
; The game's floating text: whose turn it is, out of movement, not your turn.
floaties = true
; Signal the module script events 77000-77004 (dev/DESIGN.md) for script mods.
script_events = true
; Experimental: centre the camera on whoever's turn it is.
camera_follow = false
; The turn order bar along the top of the screen: whose turn it is and who
; comes next. Its movie, turnorder.swf, is in the plugin's own folder,
; beside turnbased.dll (tools/deploy.ps1). Off until it is seen in play
; (dev/UI.md).
turn_bar = false
; With the bar: the engine renders a portrait for each combatant the game
; has none for (enemies, allies), once a fight, as it renders the party's.
; Off: their cards show initials.
portraits = true
; With the bar: on a party member's turn, the path to the ground under the
; cursor, gold as far as the movement left reaches and red beyond, before a
; click. The path is the game's own pathfinder's.
move_preview = true
; The path as small rings laid on the ground, the game's own selection
; rings made small: gold in reach, red beyond. They lie in the world,
; behind whoever stands in front of them. Off: a glowing line, drawn over
; the scene. The marker at the path's end is on the ground either way.
move_preview_ground = true

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
; Put a party member's turn off to later in the round, as the turn bar's
; Delay does. One of the game's own keys too, in its controls under
; "Turn-based"; it has no key until one is bound there, or written here.
delay = None
; End a party member's turn, as the turn bar's End does. One of the game's
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
