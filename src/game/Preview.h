// The movement preview's side of the plugin (dev/UI.md, "The movement
// preview"): on a party member's turn, while the game waits for its order,
// the path from it to the ground under the cursor, gold to where its
// movement runs out and red beyond. The turn bar's movie draws it: it asks
//
//   ExternalInterface.call("TB_GetPreview")   the path on the screen, as
//                                             core/Preview.h encodes it
//
// and gets what the frame callback last published, as it gets the bar's
// view (src/game/Bar.h). Through DAOLib: the cursor's point is explore
// mode's own (dao/GameModes.h, CursorGround), the path the game's
// pathfinder's (dao/World.h, FindPath, for a creature at rest), and a
// point's place on the screen the scene camera's (ToScreen). The path is
// searched for again only when the cursor's point has moved, and not every
// frame; its place on the screen is asked every frame, so the line stays on
// the ground as the camera moves.
//
// Where a move would go, the path ends in the game's own movement marker
// (the user, 2026-10-01: "the game has its own marker for indicating where
// to walk. Can we use that?"): the star the game puts where a move click
// lands, drawn by the movie from the game's texture and laid on the ground
// by a ring this gives it (src/game/Preview.cpp has why it is not the
// effect itself). Past the movement left there is no marker: it says "it
// will walk here", and it would not.
#pragma once

#include "core/TurnMachine.h"
#include "core/Types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tb::game
{

class Preview
{
  public:
    // Registers the name with the game's UI. Once; later calls do nothing.
    void Install();
    [[nodiscard]] bool Installed() const
    {
        return installed_;
    }

    // Every frame, from the frame callback: finds, places and publishes the
    // path, or publishes nothing to draw. `enabled` is the setting.
    void Update(const TurnView &view, std::uint32_t nowMs, bool enabled);

  private:
    void Clear();
    void Search(ObjectId walker, const Vec3 &from, const Vec3 &to, std::uint32_t nowMs);

    std::mutex mutex_;
    std::wstring encoded_; // what the movie is given
    bool installed_ = false;
    bool drawn_ = false; // something is published to draw

    // The path last found, the frame callback's alone.
    ObjectId walker_ = kNoObject;
    Vec3 from_; // where the walker stood
    Vec3 to_;   // the cursor's point it was found for
    bool searched_ = false;
    std::uint32_t searchedAt_ = 0;
    std::vector<Vec3> path_; // empty: the game finds no path there
    int logged_ = 0;
    int searches_ = 0; // since the last fifty were told of
    std::uint64_t searchMs_ = 0;
    std::uint64_t slowestMs_ = 0;
};

} // namespace tb::game
