#include "core/BarWindow.h"

#include <algorithm>
#include <cstdlib>

namespace tb
{

namespace
{

// The last place a row may begin at: a row's worth from the list's end.
std::size_t LastFirst(std::size_t turns)
{
    return turns > TurnView::kShown ? turns - TurnView::kShown : 0;
}

} // namespace

BarSpan SpanOf(std::size_t turns, std::size_t first)
{
    BarSpan span;
    span.before = std::min(first, turns);
    span.shown = std::min(TurnView::kShown, turns - span.before);
    span.after = turns - span.before - span.shown;
    return span;
}

BarSpan BarWindow::Follow(const TurnView &view)
{
    if (!view.active)
    {
        // The fight is over: the next one's row begins at the beginning.
        first_ = 0;
        ids_.clear();
        return SpanOf(view.upcoming.size(), 0);
    }
    // Those that stood before the row and are in the list no more. One whose
    // turn has passed is still in it, at its end: the list has only turned.
    const auto gone = static_cast<std::size_t>(std::count_if(
        ids_.begin(), ids_.begin() + static_cast<std::ptrdiff_t>(std::min(first_, ids_.size())),
        [&](ObjectId id) { return std::ranges::find(view.upcoming, id, &CardView::id) == view.upcoming.end(); }));
    first_ -= std::min(first_, gone);
    first_ = std::min(first_, LastFirst(view.upcoming.size()));
    ids_.clear();
    for (const CardView &card : view.upcoming)
    {
        ids_.push_back(card.id);
    }
    return Span();
}

void BarWindow::Scroll(int clicks)
{
    const std::size_t by = static_cast<std::size_t>(std::abs(clicks)) * kStep;
    first_ = clicks < 0 ? first_ - std::min(first_, by) : std::min(LastFirst(ids_.size()), first_ + by);
}

} // namespace tb
