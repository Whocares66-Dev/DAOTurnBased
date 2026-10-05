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

#include "core/Portraits.h"
#include "core/Types.h"

#include <cstdint>
#include <optional>

namespace dao
{
class CameraController;
}

namespace tb::game
{

// A creature's body as the engine has it: where it stands, where its model's
// head node is (DAOLib's dao::portraits::HeadPosition, the point the game's
// own portrait camera aims at; nothing for a model with no node of that name,
// or one not set up yet), and from the one to the other (core's ExtentOf,
// with its reasons). The turn camera's distance and a portrait's framing are
// both by this one measure (issues 33 and 32). On the game's thread.
struct BodyMeasure
{
    Vec3 feet;
    std::optional<Vec3> head;
    HeadExtent extent; // nothing of it with no head node
};
[[nodiscard]] BodyMeasure MeasureBody(ObjectId id);

class CameraDirector
{
  public:
    // The turn is now `id`'s (or the player asked to look at it, clicking
    // its card).
    void TurnTo(ObjectId id);

    // Each frame: whether the last pan got there, and why it ended (DAOLib's
    // LastEnd). Twice, with an AI's turns at twice the speed, the pan to the hero's turn left
    // the view on an enemy's room (play tests 2 and 5, 2026-09-30): the
    // game's own pull toward the party, lib-agent found, taken by the pan
    // for the player's (DAOLib 79d2baa). A pan that ends short for any
    // reason but the player's is asked for once more, until play shows it
    // arrives; either is logged, with how far short and why.
    void Watch();

    // The game's speed (the fight's, [turns] speed): the camera's moves
    // are as much quicker. At 2 a swing was over in under a second and the
    // pan took about one, so the view never reached the dog on its turns
    // (the sixth play test, 2026-09-30).
    void SetSpeed(float speed)
    {
        speed_ = speed;
    }

    // The fight is over: the camera's distance is the game's own again.
    void Release();

  private:
    void Pan(ObjectId id, std::uint64_t nowMs);
    [[nodiscard]] float MoveSeconds() const;
    void StandBackFor(ObjectId id);

    // The distance the camera has been given for a big creature's turn;
    // nothing while it is the game's own.
    std::optional<float> standing_;

    int logged_ = 0;
    ObjectId panning_ = kNoObject;
    std::uint64_t panAt_ = 0;
    bool again_ = false;
    float speed_ = 1.0f;
};

} // namespace tb::game
