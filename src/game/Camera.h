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

#include <cstdint>

namespace tb::game
{

class CameraDirector
{
  public:
    // The turn is now `id`'s (or the player asked to look at it, clicking
    // its card).
    void TurnTo(ObjectId id);

    // Each frame: whether the last pan got there. Twice, at ai_turn_speed
    // 2, the pan to the hero's turn left the view on an enemy's room (play
    // tests 2 and 5, 2026-09-30) and the log said only that it was asked
    // for. A pan that stops short this soon is not the player's doing (no
    // one turns the camera within a fifth of a second of a turn's start),
    // so it is asked for once more; one that stops short later is taken
    // as the player's. Either is logged, with how far short.
    void Watch();

  private:
    void Pan(ObjectId id, std::uint64_t nowMs);

    int logged_ = 0;
    ObjectId panning_ = kNoObject;
    std::uint64_t panAt_ = 0;
    bool again_ = false;
};

} // namespace tb::game
