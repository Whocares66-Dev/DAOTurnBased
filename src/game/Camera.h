// The camera through the turns: in the tactical view it glides to whoever
// acts and never turns (only the player turns it); close in, it follows
// whoever acts and comes round behind them, enemies too, seeing what they
// see (the user's asks, 2026-09-30). An enemy is looked at from a place
// where the camera has ground to stand over and a clear line, behind it or
// else in front; with no such place it is not gone to, and the camera stays
// with the party and turns to it (issue 33; core/Portraits.h, ChooseStand).
//
// Through DAOLib's camera moves (dao/Camera.h, f046605): PanTo follows
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
// with its reasons). A portrait's framing is by this measure (issue 32), and
// the turn camera's distance where the creature's model gives no box (issue
// 33; SizeAsks). On the game's thread.
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
    // its card). `own` is the one of the party the player acts as, with whom
    // the camera stays where there is no place round an enemy for it to
    // stand (core: ChooseStand); kNoObject for nobody.
    void TurnTo(ObjectId id, ObjectId own);

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
    // How far its size asks the camera to stand on `id`'s turn, and the
    // camera put at a distance: each nothing for the game's own.
    [[nodiscard]] std::optional<float> SizeAsks(ObjectId id);
    void Stand(std::optional<float> close);

    // The distance the camera has been given for a creature's turn, and the
    // one its size last asked, which is further where a wall stood in the
    // way; each nothing while it is the game's own.
    std::optional<float> standing_;
    std::optional<float> asked_;

    int logged_ = 0;
    ObjectId panning_ = kNoObject;
    std::uint64_t panAt_ = 0;
    bool again_ = false;
    float speed_ = 1.0f;
};

} // namespace tb::game
