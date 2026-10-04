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
    case EventKind::CastCharging:
        return "cast_charging";
    case EventKind::CastLanded:
        return "cast_landed";
    case EventKind::CastInterrupted:
        return "cast_interrupted";
    case EventKind::ActionPending:
        return "action_pending";
    case EventKind::ActionLanded:
        return "action_landed";
    case EventKind::ActionDropped:
        return "action_dropped";
    case EventKind::TurnDelayed:
        return "turn_delayed";
    case EventKind::AwaitingOrder:
        return "awaiting_order";
    }
    return "?";
}

EventDescription Describe(EventKind kind)
{
    switch (kind)
    {
    case EventKind::FightBegan:
        return {"A turn-based fight began: the turn order is made. Not said when the game enters combat with "
                "no enemy fighting the party.",
                false,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::FightOver:
        return {"The turn-based fight is over; real time runs again.",
                false,
                "why: no enemy left standing, the game ended combat, the game went to the main menu...",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::RoundStarted:
        return {"A round began: every combatant gets a turn, in initiative order.",
                false,
                nullptr,
                "the round",
                nullptr,
                nullptr};
    case EventKind::TurnStarted:
        return {"A combatant's turn began.",
                true,
                "yours (the player plays it) or AI",
                "tenths of a meter it may move this turn",
                nullptr,
                nullptr};
    case EventKind::TurnResumed:
        return {"A turn a conversation interrupted goes on, with what it had left.",
                true,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::TurnEnded:
        return {"A turn ended.",
                true,
                "why: ended by the player, nothing left to spend, done, out of movement, chose nothing, waits, fell, "
                "took too long, held by its condition, held by an effect, stunned, unaware (a hostile that has "
                "noticed nobody to fight: its turn is skipped)",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::TurnLost:
        return {"A held creature's turn was taken at once (a turn_ended follows).",
                true,
                nullptr,
                "turns it still owes",
                nullptr,
                nullptr};
    case EventKind::Refused:
        return {"The player's order was refused; nothing happened.",
                true,
                "what the player was told: Too far, Not <name>'s turn, No action left, No movement left, Held",
                nullptr,
                "the command refused (attack, move to location, use ability...)",
                "its numbers, when there are some: 12.1 m to walk, 9.0 m left"};
    case EventKind::Joined:
        return {"A creature joined the fight mid-way; it waits for its first turn.",
                true,
                nullptr,
                "its initiative",
                nullptr,
                nullptr};
    case EventKind::Fell:
        return {"A combatant fell (died, or a party member down).", true, nullptr, nullptr, nullptr, nullptr};
    case EventKind::Gone:
        return {"A combatant left the world (destroyed, vanished) or the fight.",
                true,
                "no longer fighting, for one that left the fight alive (out of combat or out of range, and neither "
                "it nor the party perceiving the other)",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::UpAgain:
        return {"A fallen party member is up again, or one that left the fight is back.",
                true,
                "fighting again, for one that had left the fight",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::ConditionHeld:
        return {"A stun, pin, knockdown, paralysis or sleep holds a creature: it loses turns.",
                true,
                nullptr,
                "turns it costs (0: as long as it lasts)",
                nullptr,
                nullptr};
    case EventKind::ConditionEnded:
        return {"A condition's turns are spent: the mod ends it at the creature's turn, and it acts.",
                true,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::ConditionStillHeld:
        return {"A condition the mod ended still holds its creature: it runs out on the game's clock.",
                true,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::ConditionExpired:
        return {"A condition ended by itself.",
                true,
                "ran out (on the clock: the turns it owes stand) or taken off (a cure: they are given back)",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::CooldownHeld:
        return {"An ability used is held on cooldown for its user's turns.",
                true,
                "how many turns",
                "the ability",
                nullptr,
                nullptr};
    case EventKind::CooldownReleased:
        return {"An ability held on cooldown is ready again, as its user's turn begins.",
                true,
                nullptr,
                "the ability",
                nullptr,
                nullptr};
    case EventKind::SpeedSet:
        return {"The game's speed was set: the fight's speed ([turns] speed) through its turns; 1 outside them.",
                false,
                nullptr,
                "the speed, thousandths",
                nullptr,
                nullptr};
    case EventKind::TurnHandedOver:
        return {"Party control changed mid-turn: whoever plays it now takes it.",
                true,
                "the player's or its AI's",
                nullptr,
                nullptr,
                nullptr};
    case EventKind::SettingsChanged:
        return {"turnbased.ini's settings were applied anew (a reload, a tool).",
                false,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::ActionGivenBack:
        return {"A command failed without acting (an invalid path, no line of sight): its action is given back.",
                true,
                "the failure (COMMAND_FAILED_...)",
                nullptr,
                "the command",
                nullptr};
    case EventKind::CastCharging:
        return {"A slow cast charges: begun (its action spent, its turn over), or a turn of its caster's spent on it.",
                true,
                "the turns still to go before it is cast, as text",
                "the ability",
                nullptr,
                nullptr};
    case EventKind::CastLanded:
        return {"A charged cast is cast, at the start of its caster's turn; the caster then has its turn.",
                true,
                nullptr,
                "the ability",
                nullptr,
                nullptr};
    case EventKind::CastInterrupted:
        return {"A charging cast is lost: nothing spent, nothing on cooldown.",
                true,
                "why: held (a stun, a knockdown, paralysis, sleep), fell, gone, the fight over",
                "the ability",
                nullptr,
                nullptr};
    case EventKind::ActionPending:
        return {"With a turn a bar of time: an action did not fit what was left of its creature's bar. It is kept, "
                "its creature walks to where it can do it, and it lands as a later turn has paid what it owes; or "
                "a whole turn was spent on what it owes.",
                true,
                "the time it still owes, as text (\"0.6 s more\")",
                "its ability (-1: an attack)",
                "the command",
                nullptr};
    case EventKind::ActionLanded:
        return {"A pending action's time is paid: it is given to its creature as its turn begins, which then has "
                "the rest of its bar. A plain attack whose target has fallen is given at the nearest enemy its "
                "creature can strike from where it stands, for nothing more.",
                true,
                "turned on another, for an attack given at another than it was kept for; none otherwise",
                "its ability (-1: an attack)",
                "the command",
                "whom it was turned on, by name"};
    case EventKind::ActionDropped:
        return {"A pending action will not land: what it still owed is not charged.",
                true,
                "why: held, struck, fell, gone, its target is gone (and for an attack, no enemy in its reach), the "
                "fight over",
                "its ability (-1: an attack)",
                "the command",
                nullptr};
    case EventKind::TurnDelayed:
        return {"A party member's turn was put off to the round's end, nothing of it spent: the turn_started before "
                "it has no turn_ended, and another turn_started follows when its turn comes.",
                true,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
    case EventKind::AwaitingOrder:
        return {"A turn the player plays has started waiting on an order: give one now. It comes as the turn is "
                "handed over (after anything kept from the turn before has landed) and again each time an action or "
                "a walk has played out and the turn goes on. After an order, one of these follows: this again, "
                "refused (the turn still waits), action_pending (kept for the next turn: the turn ends by itself), "
                "turn_ended, or fight_over.",
                true,
                "why it waits now: the turn is its player's, after its action, the order queued no command, the "
                "order was refused",
                "milliseconds of its bar left with a turn a bar of time; the actions left without",
                nullptr,
                "the movement left, in meters"};
    }
    return {};
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
