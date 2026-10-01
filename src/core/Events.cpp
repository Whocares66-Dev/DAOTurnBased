#include "core/Events.h"

#include <format>

namespace tb
{

const char *Name(EventKind kind)
{
    switch (kind)
    {
    case EventKind::FightBegan:
        return "fight_began";
    case EventKind::FightOver:
        return "fight_over";
    case EventKind::RoundStarted:
        return "round_started";
    case EventKind::TurnStarted:
        return "turn_started";
    case EventKind::TurnResumed:
        return "turn_resumed";
    case EventKind::TurnEnded:
        return "turn_ended";
    case EventKind::TurnLost:
        return "turn_lost";
    case EventKind::Refused:
        return "refused";
    case EventKind::Joined:
        return "joined";
    case EventKind::Fell:
        return "fell";
    case EventKind::Gone:
        return "gone";
    case EventKind::UpAgain:
        return "up_again";
    case EventKind::ConditionHeld:
        return "condition_held";
    case EventKind::ConditionEnded:
        return "condition_ended";
    case EventKind::ConditionStillHeld:
        return "condition_still_held";
    case EventKind::CooldownHeld:
        return "cooldown_held";
    case EventKind::CooldownReleased:
        return "cooldown_released";
    case EventKind::SpeedSet:
        return "speed_set";
    }
    return "?";
}

namespace
{

// A JSON string's body: quotes, backslashes and control characters
// escaped; the rest, UTF-8 included, as it is.
void Escaped(std::string &out, std::string_view text)
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

} // namespace

std::string ToJson(const TurnEvent &event, std::uint64_t realMs)
{
    std::string out = std::format(R"({{"ms":{},"world_ms":{},"round":{},"event":"{}")", realMs, event.worldMs,
                                  event.round, Name(event.kind));
    if (event.who != kNoObject)
    {
        out += std::format(R"(,"who":"{:#010x}","name":")", event.who);
        Escaped(out, event.name);
        out += '"';
    }
    if (!event.detail.empty())
    {
        out += R"(,"detail":")";
        Escaped(out, event.detail);
        out += '"';
    }
    out += std::format(R"(,"value":{}}})", event.value);
    return out;
}

} // namespace tb
