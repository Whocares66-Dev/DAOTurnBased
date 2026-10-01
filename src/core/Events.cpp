#include "core/Events.h"

#include "core/Json.h"

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
    case EventKind::TurnHandedOver:
        return "turn_handed_over";
    case EventKind::SettingsChanged:
        return "settings_changed";
    case EventKind::ConditionExpired:
        return "condition_expired";
    case EventKind::ActionGivenBack:
        return "action_given_back";
    }
    return "?";
}

std::string ToJson(const TurnEvent &event, std::uint64_t realMs)
{
    std::string out = std::format(R"({{"ms":{},"world_ms":{},"round":{},"event":"{}")", realMs, event.worldMs,
                                  event.round, Name(event.kind));
    if (event.who != kNoObject)
    {
        out += std::format(R"(,"id":"{:#x}","name":")", event.who);
        AppendJsonString(out, event.name);
        out += '"';
    }
    if (!event.detail.empty())
    {
        out += R"(,"detail":")";
        AppendJsonString(out, event.detail);
        out += '"';
    }
    if (!event.command.empty())
    {
        out += R"(,"command":")";
        AppendJsonString(out, event.command);
        out += '"';
    }
    if (!event.note.empty())
    {
        out += R"(,"note":")";
        AppendJsonString(out, event.note);
        out += '"';
    }
    out += std::format(R"(,"value":{}}})", event.value);
    return out;
}

} // namespace tb
