// The camera through the turns (core/Easing.h has the why and the numbers):
// in the tactical view it pans to whoever acts and never turns; close in,
// it stays with the player's character through others' turns and comes
// round behind whoever acts as their turn begins, enemies included.
//
// Through DAOLib's camera (dao/Camera.h, a24efda to 6c26174): Follow is the
// engine's own SetTarget, which in the tactical view keeps the old focus's
// offset from the new target, so the pan is that offset eased to nothing
// each frame; close in, Follow pans by itself within 15 m, and the yaw is
// eased round. Following an enemy close in put the camera 3.8 m behind the
// enemy, zooming in and back (the user, 2026-09-30): close in, enemies are
// not followed. Seen in play (11:29): SetYaw and the yaw behind a party
// member; the tactical pan (Follow, SetFocusOffset) still to see.
//
// Behind Settings::cameraFollow. On the scripts' thread: from the frame
// callback and the effects it applies.
#pragma once

#include "core/Types.h"

#include <optional>

namespace tb::game
{

class CameraDirector
{
  public:
    // The turn is now `id`'s (or the player asked to look at it, clicking
    // its card).
    void TurnTo(ObjectId id);

    // Each frame: the move a turn set going, eased a frame further.
    void Update(float dtSeconds);

  private:
    void Watch(float dtSeconds);

    bool panning_ = false;         // tactical: the offset eased to nothing
    std::optional<float> behind_;  // close in: the yaw eased round to this
    std::optional<float> settled_; // where the last swing left the yaw, watched a while
    float settledSeconds_ = 0.0f;
    int driftLogged_ = 0;
    float easingSeconds_ = 0.0f; // given up after a while: the player may be turning it
    int behindLogged_ = 0;
};

} // namespace tb::game
