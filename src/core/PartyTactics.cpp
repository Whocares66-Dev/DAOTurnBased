#include "core/PartyTactics.h"

#include "core/Json.h"
#include "core/Text.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>
#include <vector>

namespace tb
{

namespace
{

constexpr std::string_view kFileName = "turnbased";
constexpr std::string_view kOn = "tactics on ";
constexpr std::string_view kOff = "tactics off ";
constexpr std::string_view kEnd = "end";

// An id as Eluvian writes them ("0x7d9"), or a whole number; all of the
// text, or nothing.
std::optional<ObjectId> IdOf(std::string_view text)
{
    const bool hex = text.starts_with("0x") || text.starts_with("0X");
    if (hex)
    {
        text.remove_prefix(2);
    }
    ObjectId id = 0;
    const std::from_chars_result got = std::from_chars(text.data(), text.data() + text.size(), id, hex ? 16 : 10);
    if (text.empty() || got.ec != std::errc{} || got.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return id;
}

// Where a follower's choice is kept, or would be: they are kept in the tags'
// order, so that what is written of them and said of them does not go by the
// order they were made in.
template <class Kept> auto PlaceOf(Kept &choices, std::wstring_view tag)
{
    return std::lower_bound(choices.begin(), choices.end(), tag,
                            [](const auto &kept, std::wstring_view wanted) { return kept.first < wanted; });
}

// The letters' case aside, for the letters a name is typed in (ASCII's): a
// name's other characters are compared as they are.
bool SameName(std::string_view a, std::string_view b)
{
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
}

// "Lyna (0x82, the hero), Alistair (0x7d9)": whom a caller may name.
std::string PartyList(std::span<const Follower> party)
{
    std::string out;
    for (const Follower &member : party)
    {
        out += std::format("{}{} ({:#x}{})", out.empty() ? "" : ", ", Utf8(member.name), member.id,
                           member.hero ? ", the hero" : "");
    }
    return out;
}

} // namespace

std::optional<bool> PartyTactics::Chosen(std::wstring_view tag) const
{
    const auto found = PlaceOf(chosen_, tag);
    if (found == chosen_.end() || found->first != tag)
    {
        return std::nullopt;
    }
    return found->second;
}

bool PartyTactics::Choose(std::wstring_view tag, bool on)
{
    if (!Keepable(tag))
    {
        return false;
    }
    const auto found = PlaceOf(chosen_, tag);
    if (found == chosen_.end() || found->first != tag)
    {
        chosen_.emplace(found, std::wstring{tag}, on);
        return true;
    }
    return std::exchange(found->second, on) != on;
}

bool Keepable(std::wstring_view tag)
{
    return !tag.empty() && std::ranges::none_of(tag, [](wchar_t c) { return c < 0x20 || c == 0x7F; });
}

bool TacticsPlay(std::optional<bool> chosen, bool hero, PartyControl start)
{
    return !hero && chosen.value_or(start == PartyControl::Tactics);
}

std::string Describe(const PartyTactics &choices)
{
    if (choices.All().empty())
    {
        return "none";
    }
    std::string whom;
    for (const auto &[tag, on] : choices.All())
    {
        whom += std::format("{}{} {}", whom.empty() ? "" : ", ", Utf8(tag), on ? "on" : "off");
    }
    return std::format("{} ({})", choices.All().size(), whom);
}

std::string WriteChoices(const PartyTactics &choices)
{
    std::string out = std::format("{} {}\n", kFileName, kPartyTacticsVersion);
    for (const auto &[tag, on] : choices.All())
    {
        out += on ? kOn : kOff;
        out += Utf8(tag);
        out += '\n';
    }
    out += kEnd;
    out += '\n';
    return out;
}

namespace
{

// What is wrong with the file's first line, "<name> <version>", or nothing:
// not the mod's file, a version that is no number, or another build's.
std::optional<std::string> WrongHead(std::string_view head)
{
    if (!head.starts_with(kFileName) || head.size() <= kFileName.size() || head[kFileName.size()] != ' ')
    {
        return std::format("it does not begin with \"{} <version>\": not the mod's file, or damaged", kFileName);
    }
    head.remove_prefix(kFileName.size() + 1);
    const std::optional<std::uint64_t> version = WholeNumber(head);
    if (!version)
    {
        return "its version is not a number: damaged";
    }
    if (*version != static_cast<std::uint64_t>(kPartyTacticsVersion))
    {
        return std::format("it is of version {}, and this build reads version {}: written by another build", *version,
                           kPartyTacticsVersion);
    }
    return std::nullopt;
}

} // namespace

ChoicesRead ReadChoices(std::string_view file)
{
    const auto damaged = [](std::string why) { return ChoicesRead{PartyTactics{}, std::move(why)}; };
    // A line at a time, the line feed taken off and a carriage return before
    // it; `file` is what is left.
    const auto line = [&file] {
        const std::size_t end = file.find('\n');
        std::string_view text = file.substr(0, end);
        file.remove_prefix(end == std::string_view::npos ? file.size() : end + 1);
        if (text.ends_with('\r'))
        {
            text.remove_suffix(1);
        }
        return text;
    };
    if (std::optional<std::string> wrong = WrongHead(line()))
    {
        return damaged(std::move(*wrong));
    }
    ChoicesRead read;
    bool ended = false;
    for (int number = 2; !file.empty(); ++number)
    {
        std::string_view text = line();
        if (text.empty())
        {
            continue;
        }
        if (ended)
        {
            return damaged(std::format("line {} comes after its end: damaged", number));
        }
        if (text == kEnd)
        {
            ended = true;
            continue;
        }
        const bool on = text.starts_with(kOn);
        if (!on && !text.starts_with(kOff))
        {
            return damaged(
                std::format(R"(line {} is not a choice ("{}<tag>" or "{}<tag>"): damaged)", number, kOn, kOff));
        }
        text.remove_prefix(on ? kOn.size() : kOff.size());
        const std::optional<std::wstring> tag = FromUtf8(text);
        if (!tag || !Keepable(*tag))
        {
            return damaged(std::format("line {} names no follower's tag: damaged", number));
        }
        if (read.choices.Chosen(*tag))
        {
            return damaged(std::format("line {} chooses for a follower a second time: damaged", number));
        }
        (void)read.choices.Choose(*tag, on);
    }
    if (!ended)
    {
        return damaged(std::format("it has no \"{}\" after its last line: cut short", kEnd));
    }
    return read;
}

AutoAskRead ReadAutoAsk(std::string_view json)
{
    constexpr std::string_view kHow =
        R"(Give a companion's id or its name, and on (true: its tactics play its turns; false: they wait for the )"
        R"(player) to choose, or neither to ask: {"name": "Alistair", "on": true}, {"id": "0x7d9"}.)";
    const auto wrong = [&](std::string_view what) {
        return AutoAskRead{std::nullopt, std::string{what} + " " + std::string{kHow}};
    };
    AutoAsk ask;
    const std::optional<std::string> id = JsonField(json, "id");
    const std::optional<std::string> name = JsonField(json, "name");
    if (id)
    {
        ask.id = IdOf(*id);
        if (!ask.id || *ask.id == kNoObject)
        {
            return wrong("The id is not a creature's id.");
        }
    }
    else if (name && !name->empty())
    {
        ask.name = *name;
    }
    else
    {
        return wrong("There is no id and no name.");
    }
    if (const std::optional<std::string> on = JsonField(json, "on"))
    {
        if (*on != "true" && *on != "false")
        {
            return wrong("on is neither true nor false.");
        }
        ask.on = *on == "true";
    }
    return AutoAskRead{ask, {}};
}

FollowerFound FindFollower(const AutoAsk &ask, std::span<const Follower> party)
{
    const auto nobody = [](std::string why) { return FollowerFound{nullptr, std::move(why)}; };
    if (party.empty())
    {
        return nobody("There is no party in the world now: at the main menu, or loading.");
    }
    const Follower *who = nullptr;
    if (ask.id)
    {
        const auto found = std::ranges::find(party, *ask.id, &Follower::id);
        if (found == party.end())
        {
            return nobody(std::format("{:#x} is not one of the party: {}.", *ask.id, PartyList(party)));
        }
        who = &*found;
    }
    else
    {
        std::vector<const Follower *> named;
        for (const Follower &member : party)
        {
            if (SameName(Utf8(member.name), ask.name))
            {
                named.push_back(&member);
            }
        }
        if (named.empty())
        {
            return nobody(std::format("Nobody of the party is named \"{}\": {}.", ask.name, PartyList(party)));
        }
        if (named.size() > 1)
        {
            return nobody(std::format("{} of the party are named \"{}\": give the id of the one meant, of {}.",
                                      named.size(), ask.name, PartyList(party)));
        }
        who = named.front();
    }
    if (who->hero && ask.on)
    {
        return nobody(std::format("{} is the hero, the player's own character: its turns always wait for the player, "
                                  "and there is nothing to choose for it (issue 46: companions only).",
                                  Utf8(who->name)));
    }
    if (ask.on && !Keepable(who->tag))
    {
        return nobody(std::format("{} has no tag a choice could be kept by.", Utf8(who->name)));
    }
    return FollowerFound{who, {}};
}

std::string ToJson(const Follower &who, const TacticsAnswer &answer)
{
    const auto text = [](std::string &out, std::string_view value) {
        out += '"';
        AppendJsonString(out, value);
        out += '"';
    };
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    const char *underWay = answer.turnIsTactics ? "AI" : "yours";
    std::string out = std::format(R"({{"id":"{:#x}","name":)", who.id);
    text(out, Utf8(who.name));
    out += R"(,"tag":)";
    text(out, Utf8(who.tag));
    out += std::format(R"(,"hero":{},"on":{},"by":"{}","changed":{},"applies":)", flag(who.hero), flag(answer.on),
                       answer.chosen ? "chosen" : "setting", flag(answer.changed));
    text(out, answer.turnUnderWay
                  ? std::format("from its next turn; the turn under way is played out as it began ({})", underWay)
                  : std::string{"from its next turn"});
    out += R"(,"turn_under_way":)";
    if (answer.turnUnderWay)
    {
        text(out, underWay);
    }
    else
    {
        out += "null";
    }
    out += '}';
    return out;
}

BoxState BoxFor(const Follower &who, const TacticsAnswer &answer)
{
    if (who.hero || !Keepable(who.tag))
    {
        return BoxState::None;
    }
    return answer.on ? BoxState::On : BoxState::Off;
}

void TacticsBoxes::Set(ObjectId id, BoxState state)
{
    const auto found = std::ranges::find(states_, id, &std::pair<ObjectId, BoxState>::first);
    if (state == BoxState::None)
    {
        if (found != states_.end())
        {
            states_.erase(found);
        }
        return;
    }
    if (found != states_.end())
    {
        found->second = state;
        return;
    }
    // By id, so that two are equal whatever order the party was read in.
    states_.emplace(std::ranges::lower_bound(states_, id, {}, &std::pair<ObjectId, BoxState>::first), id, state);
}

BoxState TacticsBoxes::Of(ObjectId id) const
{
    const auto found = std::ranges::find(states_, id, &std::pair<ObjectId, BoxState>::first);
    return found != states_.end() ? found->second : BoxState::None;
}

std::string Describe(std::span<const Follower> party, const TacticsBoxes &boxes)
{
    std::string with;
    std::string without;
    for (const Follower &member : party)
    {
        const BoxState state = boxes.Of(member.id);
        if (state == BoxState::None)
        {
            without +=
                std::format("{}{}{}", without.empty() ? "" : ", ", Utf8(member.name), member.hero ? " (the hero)" : "");
            continue;
        }
        with +=
            std::format("{}{} {}", with.empty() ? "" : ", ", Utf8(member.name), state == BoxState::On ? "on" : "off");
    }
    if (with.empty() && without.empty())
    {
        return "nobody";
    }
    if (without.empty())
    {
        return with;
    }
    return std::format("{}{}none for {}", with, with.empty() ? "" : "; ", without);
}

std::optional<ObjectId> ReadBoxAsk(std::string_view text)
{
    const std::optional<ObjectId> id = IdOf(text);
    return id && *id != kNoObject ? id : std::nullopt;
}

std::optional<BoxClick> ReadBoxClick(std::string_view args)
{
    const std::size_t space = args.find(' ');
    if (space == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::optional<ObjectId> id = ReadBoxAsk(args.substr(0, space));
    const std::string_view state = args.substr(space + 1);
    if (!id || (state != "0" && state != "1"))
    {
        return std::nullopt;
    }
    return BoxClick{*id, state == "1"};
}

} // namespace tb
