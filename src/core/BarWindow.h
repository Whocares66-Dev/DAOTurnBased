// Which of the turns to come the bar's row shows (dev/UI.md, "More fighting
// than the row holds"; issue 23). The row has room for TurnView::kShown
// cards. With more fighting than that, those past the tenth were not on the
// bar at all and nothing said there were more: at Redcliffe castle one
// fighter was always off its end. So an arrow stands at each end of the row,
// the game's own golden ones, and a click moves the row along the list.
//
// The user, 2026-10-03: "Clicking on the right arrow moves down the list and
// shows portraits beyond 10. Left arrow moves back. No arrows should be shown
// if there are 10 or fewer portraits. If unable to move last or right, the
// corresponding arrow is hidden. As gameplay continues, don't want to
// constantly reset the position of the portraits back to the beginning. If
// we're currently showing 3 to 12 (starting at index 1), continue showing
// those positions, starting at 3. If an enemy in position 1 or 2 has fallen,
// then the index is at 2. If both enemies before has fallen, then the index
// is now 1."
//
// So the row's first place in the list is kept from one view to the next.
// The list turns as turns pass (the current one first), and the row goes on
// showing the same places, not the same fighters. Only when one standing
// before the row leaves the list (a fallen enemy goes from it at once) does
// the row begin a place earlier, which keeps it on the fighters it showed.
// It never runs past the list's end, so with a row's worth or fewer it is
// the beginning, and a fight's end sends it back there.
//
// The game layer keeps one (src/game/Bar.h), follows the machine's view with
// it each frame and gives it the arrows' clicks; what it answers goes to the
// movie in the bar's string (core/Bar.h) and says whose portraits to get
// (core/Portraits.h).
#pragma once

#include "core/TurnMachine.h"
#include "core/Types.h"

#include <cstddef>
#include <vector>

namespace tb
{

// The row among the turns to come: how many stand before it, how many it
// shows, how many come after. An arrow shows at an end with any beyond it.
struct BarSpan
{
    std::size_t before = 0;
    std::size_t shown = 0;
    std::size_t after = 0;
};

// The row beginning at `first` of `turns` turns to come: a row's worth from
// there, or what is left.
[[nodiscard]] BarSpan SpanOf(std::size_t turns, std::size_t first);

class BarWindow
{
  public:
    // How far a click on an arrow moves the row, in cards. One: the user
    // gave no number, and a step of one never skips a card.
    static constexpr std::size_t kStep = 1;

    // The view as it is now: the row's place kept, moved down for those gone
    // from before it, held within the list; the beginning again once no
    // fight is on. Answers the row.
    BarSpan Follow(const TurnView &view);

    // Clicks on the arrows since: to later turns (positive) or to earlier
    // ones, a step each, as far as the list seen last goes.
    void Scroll(int clicks);

    [[nodiscard]] BarSpan Span() const
    {
        return SpanOf(ids_.size(), first_);
    }

  private:
    std::size_t first_ = 0;
    std::vector<ObjectId> ids_; // the turns to come when last followed, in their order
};

} // namespace tb
