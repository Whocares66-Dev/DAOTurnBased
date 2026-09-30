// The camera through the turns: in the tactical view it glides to whoever
// acts and never turns (only the player turns it); close in, it follows
// whoever acts and comes round behind them, enemies too, seeing what they
// see (the user's asks, 2026-09-30).
//
// Through DAOLib's camera moves (dao/CameraMoves.h, f046605): PanTo follows
// a creature and eases the tactical focus onto it; EaseYaw turns the camera
// the short way round from wherever it is; YawBehind is the way the
// creature faces, from GetOrientation, turned round. A move stops as soon
// as the player turns or pans the camera: it is theirs then. The mod had
// its own easing first (core/Easing.h, gone); the library's takes the
// player's own turning into account, which the user asked for.
//
// Behind Settings::cameraFollow. On the scripts' thread: from the effects
// the frame callback applies.
#pragma once

#include "core/Types.h"

namespace tb::game
{

class CameraDirector
{
  public:
    // The turn is now `id`'s (or the player asked to look at it, clicking
    // its card).
    void TurnTo(ObjectId id);

  private:
    int logged_ = 0;
};

} // namespace tb::game
