// Which combatants the turn bar has a portrait for, and whose to get next
// (dev/UI.md, "Portraits"). The engine renders any creature's portrait as it
// renders the party's (DAOLib's CCCreature::RenderPortrait), into a texture
// a movie loads by its img:// name. Rendering clones the creature's model,
// so it is not free: one a frame, the cards nearest the front first, each
// combatant once a fight. What a try gave is kept, an empty answer too, so a
// creature the engine makes no portrait for is not asked every frame.
//
// Forgotten when a fight ends: object ids are reused once an area is left,
// and a portrait's name belongs to the creature it was rendered for. Kept
// through a conversation in the middle of one, when nothing is rendered.
//
// And released: the game's own screens release every portrait they render
// when they close (ReleasePortrait beside each UpdatePortrait's caller), and
// never leave a renderer, with its clone of the creature, to die with the
// area. The first area change after a fight in play crashed in the engine's
// model code (2026-09-30), which is why. The movie lets the images go
// first, then the ones this book rendered are released; the party's, which
// the game keeps, never are.
#pragma once

#include "core/TurnMachine.h"
#include "core/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb
{

// The name the game's image loader finds a texture by: DAOLib's
// "img://portrait_player0" becomes "portrait_player0.dds". The game's own
// movies add the extension to a name without one before loading it
// (portraits.gfx, MovieUtils.GetImageUrl); without it the loader finds
// nothing and draws grey (seen in play, 2026-09-30). Empty stays empty.
[[nodiscard]] std::wstring TextureName(std::wstring_view url);

class PortraitBook
{
  public:
    // How long a movie that has not said it let the images go is waited
    // for before the portraits are released anyway.
    static constexpr std::uint32_t kReleaseWaitMs = 2000;

    // Once no fight is on, forgets them all, and what it rendered waits to
    // be released. Each frame, before Next.
    void Follow(const TurnView &view, std::uint32_t nowMs);

    // The combatant to get a portrait for this frame, if a card lacks one:
    // the first such card, so the current turn's and the next come first.
    // None while the fight stands aside (a conversation, a menu).
    [[nodiscard]] std::optional<ObjectId> Next(const TurnView &view) const;

    // What getting one gave: its URL, or empty for none (not asked again),
    // and whether it was rendered for the bar (to be released after) or
    // was the game's own.
    void Got(ObjectId id, std::wstring url, bool rendered);

    // The combatant's portrait, empty if it has none (or none yet).
    [[nodiscard]] std::wstring_view Of(ObjectId id) const;

    // The movie has let the images go: what waits may be released now.
    void MovieLetGo();

    // Whether the bar should stay up after a fight for the movie to let
    // its images go: until it has, or the wait is over.
    [[nodiscard]] bool AwaitingMovie() const
    {
        return !releasing_.empty() && !movieLetGo_;
    }

    // The creatures whose rendered portraits may be released now, each
    // once: after the movie let go, or kReleaseWaitMs. Not one a new fight
    // is showing again.
    [[nodiscard]] std::vector<ObjectId> TakeReleases(std::uint32_t nowMs);

    // How many were tried, and how many gave a portrait.
    [[nodiscard]] std::size_t Tried() const
    {
        return urls_.size();
    }
    [[nodiscard]] std::size_t Found() const;

  private:
    std::unordered_map<ObjectId, std::wstring> urls_;
    std::unordered_set<ObjectId> rendered_;
    std::vector<ObjectId> releasing_;
    std::uint32_t releasingSince_ = 0;
    bool movieLetGo_ = false;
};

} // namespace tb
