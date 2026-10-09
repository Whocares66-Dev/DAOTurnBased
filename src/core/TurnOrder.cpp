#include "core/TurnOrder.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace tb
{

bool TurnOrder::Shown(const Combatant &c)
{
    return !c.inactive && (!c.down || c.side == Side::Party);
}

bool TurnOrder::Before(const Combatant &a, const Combatant &b)
{
    if (a.initiative != b.initiative)
    {
        return a.initiative > b.initiative;
    }
    if (a.dexterity != b.dexterity)
    {
        return a.dexterity > b.dexterity;
    }
    if (a.side != b.side)
    {
        return static_cast<int>(a.side) < static_cast<int>(b.side);
    }
    return a.id < b.id;
}

void TurnOrder::Clear()
{
    order_.clear();
    delayed_.clear();
    current_ = 0;
    round_ = 0;
}

void TurnOrder::Begin(std::vector<Combatant> combatants)
{
    order_ = std::move(combatants);
    delayed_.clear();
    std::ranges::sort(order_, &TurnOrder::Before);
    round_ = 1;
    current_ = 0;
    const auto first = std::ranges::find_if(order_, [](const Combatant &c) { return !c.down; });
    if (first != order_.end())
    {
        current_ = static_cast<std::size_t>(first - order_.begin());
    }
}

const Combatant *TurnOrder::Current() const
{
    return current_ < order_.size() ? &order_[current_] : nullptr;
}

Combatant *TurnOrder::Current()
{
    return current_ < order_.size() ? &order_[current_] : nullptr;
}

const Combatant *TurnOrder::Find(ObjectId id) const
{
    const auto it = std::ranges::find(order_, id, &Combatant::id);
    return it != order_.end() ? &*it : nullptr;
}

Combatant *TurnOrder::Find(ObjectId id)
{
    const auto it = std::ranges::find(order_, id, &Combatant::id);
    return it != order_.end() ? &*it : nullptr;
}

void TurnOrder::DropFallenOthers()
{
    std::erase_if(order_, [](const Combatant &c) { return !Shown(c); });
}

bool TurnOrder::Advance()
{
    if (order_.empty())
    {
        return false;
    }
    bool newRound = false;
    // Two laps at most: the second would find nobody standing either.
    for (std::size_t step = 0; step < (2 * order_.size()) + 2; ++step)
    {
        ++current_;
        if (current_ >= order_.size())
        {
            current_ = 0;
            ++round_;
            newRound = true;
            DropFallenOthers();
            // Who put a turn off stands at the end: the new round is in
            // initiative's order again.
            if (!std::exchange(delayed_, {}).empty())
            {
                std::ranges::sort(order_, &TurnOrder::Before);
            }
            if (order_.empty())
            {
                return true;
            }
        }
        if (!order_[current_].down)
        {
            return newRound;
        }
    }
    return newRound;
}

bool TurnOrder::Delayed(ObjectId id) const
{
    return std::ranges::find(delayed_, id) != delayed_.end();
}

bool TurnOrder::CanDelay() const
{
    if (current_ >= order_.size() || Delayed(order_[current_].id))
    {
        return false;
    }
    return std::any_of(order_.begin() + static_cast<std::ptrdiff_t>(current_) + 1, order_.end(),
                       [](const Combatant &c) { return !c.down; });
}

bool TurnOrder::Delay()
{
    if (!CanDelay())
    {
        return false;
    }
    const auto at = order_.begin() + static_cast<std::ptrdiff_t>(current_);
    delayed_.push_back(at->id);
    std::rotate(at, at + 1, order_.end());
    // The turn is the next standing one's: there is one before the end,
    // where the one that waits now stands.
    while (order_[current_].down)
    {
        ++current_;
    }
    return true;
}

void TurnOrder::Join(Combatant combatant)
{
    if (order_.empty())
    {
        order_.push_back(std::move(combatant));
        current_ = 0;
        round_ = std::max(round_, 1);
        return;
    }
    const auto at = std::ranges::find_if(order_, [&](const Combatant &c) { return Before(combatant, c); });
    const auto index = static_cast<std::size_t>(at - order_.begin());
    order_.insert(at, std::move(combatant));
    // Its place is at or before the current turn's: this round has passed
    // it, and the current turn stays the current combatant's.
    if (index <= current_)
    {
        ++current_;
    }
}

bool TurnOrder::AnyStanding(Side side) const
{
    return std::ranges::any_of(order_, [side](const Combatant &c) { return c.side == side && !c.down; });
}

std::vector<TurnOrder::Slot> TurnOrder::Upcoming(std::size_t count) const
{
    std::vector<Slot> slots;
    if (order_.empty() || count == 0)
    {
        return slots;
    }
    slots.push_back({&order_[current_], round_, true});
    std::size_t index = current_;
    int round = round_;
    for (std::size_t step = 1; step < order_.size() && slots.size() < count; ++step)
    {
        ++index;
        if (index >= order_.size())
        {
            index = 0;
            ++round;
        }
        const Combatant &c = order_[index];
        // The next round's fallen others will have been dropped by then.
        if (Shown(c) && (round <= round_ || !c.down || c.side == Side::Party))
        {
            slots.push_back({&c, round, false});
        }
    }
    return slots;
}

} // namespace tb
