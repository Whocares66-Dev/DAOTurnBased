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

    // Each frame: whether the last pan got there, and why it ended (DAOLib's
    // LastEnd). Twice, at ai_turn_speed 2, the pan to the hero's turn left
    // the view on an enemy's room (play tests 2 and 5, 2026-09-30): the
    // game's own pull toward the party, lib-agent found, taken by the pan
    // for the player's (DAOLib 79d2baa). A pan that ends short for any
    // reason but the player's is asked for once more, until play shows it
    // arrives; either is logged, with how far short and why.
    void Watch();

  private:
    void Pan(ObjectId id, std::uint64_t nowMs);

    int logged_ = 0;
    ObjectId panning_ = kNoObject;
    std::uint64_t panAt_ = 0;
    bool again_ = false;
};

} // namespace tb::game
