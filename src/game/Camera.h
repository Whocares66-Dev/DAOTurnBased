// The camera through the turns (core/Easing.h has the why and the numbers):
// in the tactical view it pans to whoever acts and never turns; close in,
// it stays with the player's character through others' turns and comes
// round behind each party member as their turn begins.
//
// Through DAOLib's camera (dao/Camera.h, a24efda to 6c26174): Follow is the
// engine's own SetTarget, which in the tactical view keeps the old focus's
// offset from the new target, so the pan is that offset eased to nothing
// each frame; close in, Follow pans by itself within 15 m, and the yaw is
// eased round. Following an enemy close in put the camera 3.8 m behind the
// enemy, zooming in and back (the user, 2026-09-30): close in, enemies are
// not followed. Follow, SetFocusOffset and SetYaw are not yet seen in
// play; the yaw's reading is.
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
    // The turn is now `id`'s, a party member's or not (or the player asked
    // to look at it, clicking its card).
    void TurnTo(ObjectId id, bool party);

    // Each frame: the move a turn set going, eased a frame further.
    void Update(float dtSeconds);

  private:
    bool panning_ = false;        // tactical: the offset eased to nothing
    std::optional<float> behind_; // close in: the yaw eased round to this
    float easingSeconds_ = 0.0f;  // given up after a while: the player may be turning it
    int behindLogged_ = 0;
};

} // namespace tb::game
