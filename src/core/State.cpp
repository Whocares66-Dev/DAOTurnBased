#include "core/State.h"

#include "core/Json.h"
#include "core/Text.h"

#include <format>

namespace tb
{

namespace
{

void Id(std::string &out, ObjectId id)
{
    if (id == kNoObject)
    {
        out += "null";
        return;
    }
    out += std::format(R"("{:#x}")", id);
}

void Text(std::string &out, std::string_view text)
{
    out += '"';
    AppendJsonString(out, text);
    out += '"';
}

const char *Bool(bool value)
{
    return value ? "true" : "false";
}

const char *ConditionName(Condition condition)
{
    switch (condition)
    {
    case Condition::None:
        return "none";
    case Condition::Disabled:
        return "disabled";
    }
    return "?";
}

} // namespace

std::string ToJson(const MachineState &state)
{
    const TurnView &view = state.view;
    std::string out = std::format(R"({{"active":{},"phase":"{}","round":{},"world_ms":{},"current":)",
                                  Bool(view.active), Name(view.phase), view.round, state.worldMs);
    // Whose turn, whole: a script that starts mid-fight reads it here rather
    // than replay the events (eluvian-agent). The current card is the first.
    const CardView *current =
        !view.upcoming.empty() && view.upcoming.front().current ? &view.upcoming.front() : nullptr;
    if (view.current == kNoObject || current == nullptr)
    {
        out += "null";
    }
    else
    {
        out += R"({"id":)";
        Id(out, current->id);
        out += R"(,"name":)";
        Text(out, Utf8(current->name));
        out += std::format(R"(,"side":"{}","yours":{}}})", Name(current->side), Bool(current->yours));
    }
    out += std::format(R"(,"player_turn":{},"movement_left":{:.1f},"movement_max":{:.1f})", Bool(view.playerTurn),
                       view.movementLeft, view.movementMax);
    out += std::format(R"(,"time_left_ms":{},"time_ms":{})", view.timeLeftMs, view.timeMs);
    out += std::format(R"(,"can_delay":{},"rolled_ms":{},"player_paused":{})", Bool(view.canDelay), view.rolledMs,
                       Bool(state.playerPaused));
    out += std::format(R"(,"speed":{:.2f},"speed_took":{},"held_by_effect":{},"last_refusal":)", state.speed,
                       state.speedTook ? Bool(*state.speedTook) : "null", Bool(state.heldByEffect));
    Text(out, state.lastRefusal);
    // What the bar's caption says just now of an action given back, empty
    // for nothing: a tool reads why its order came to nothing here, where
    // the player reads it on the bar (issue 21).
    out += R"(,"notice":)";
    Text(out, Utf8(view.notice));
    out += R"(,"order":[)";
    for (std::size_t i = 0; i < view.upcoming.size(); ++i)
    {
        const CardView &card = view.upcoming[i];
        out += i == 0 ? R"({"id":)" : R"(,{"id":)";
        Id(out, card.id);
        out += R"(,"name":)";
        Text(out, Utf8(card.name));
        out += std::format(
            R"(,"side":"{}","rank":{},"health":{:.2f},"energy":{:.2f},"mana":{},"down":{},"current":{},"yours":{},"round":{})",
            Name(card.side), card.rank, card.health, card.energy, Bool(card.mana), Bool(card.down), Bool(card.current),
            Bool(card.yours), card.round);
        // A party member's: whether its tactics play its turns (issue 46),
        // from its next one. Beside `yours`, which on the current card is of
        // the turn under way: a choice made in that turn shows here at once
        // and there when the next begins.
        if (card.side == Side::Party)
        {
            out += std::format(R"(,"auto":{})", Bool(card.tacticsPlay));
        }
        out += '}';
    }
    out += R"(],"held":[)";
    for (std::size_t i = 0; i < state.held.size(); ++i)
    {
        const HeldView &held = state.held[i];
        out += i == 0 ? R"({"id":)" : R"(,{"id":)";
        Id(out, held.id);
        out += R"(,"name":)";
        Text(out, Utf8(held.name));
        out += std::format(R"(,"condition":"{}","on_clock":{},"owed_ms":{}}})", ConditionName(held.seen),
                           Bool(held.onClock), held.owedMs);
    }
    out += R"(],"cooldowns":[)";
    for (std::size_t i = 0; i < state.cooldowns.size(); ++i)
    {
        const CooldownView &cooldown = state.cooldowns[i];
        out += i == 0 ? R"({"id":)" : R"(,{"id":)";
        Id(out, cooldown.id);
        out +=
            std::format(R"(,"ability":{},"left_ms":{},"of_ms":{})", cooldown.ability, cooldown.leftMs, cooldown.ofMs);
        if (!cooldown.item.empty())
        {
            out += R"(,"item":)";
            Text(out, Utf8(cooldown.item));
        }
        out += '}';
    }
    out += R"(],"charging":[)";
    for (std::size_t i = 0; i < state.charging.size(); ++i)
    {
        const ChargeView &charge = state.charging[i];
        out += i == 0 ? R"({"id":)" : R"(,{"id":)";
        Id(out, charge.id);
        out += std::format(R"(,"ability":{},"owed_ms":{}}})", charge.ability, charge.owedMs);
    }
    out += "]}";
    return out;
}

} // namespace tb
