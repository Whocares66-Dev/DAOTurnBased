// A talent chosen on the quickbar asks for its target, even with one
// selected, as in BG3 (the user, 2026-09-30: with a target selected, the
// game used the talent on it at once, "sometimes it allows targeting and
// sometimes it doesn't"; a character keeps its target from turn to turn).
//
// The quickbar sends PerformAbilityMessage {ability, caster, target}
// (DAOLib's dao/Messages.h). With a target in it, explore mode's handler
// uses the ability on that target; with none, it asks the player for one
// (the example plugin's log in play: the message, then a click on the
// target under the cursor). So during a turn-based fight a message that
// names a target other than its caster is taken (DAOLib's InterceptMessage,
// at the game mode controller) and sent again on the next frame with no
// target. The one sent again passes. Not yet seen in play.
//
// Behind Settings::pickTargets.
#pragma once

#include "core/Types.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace tb
{
class TurnMachine;
}

namespace tb::game
{

class Targeting
{
  public:
    // Once, at load: the interceptor. It asks the machine, under its
    // mutex, whether a turn-based fight holds the world.
    void Install(const TurnMachine &machine, std::mutex &mutex);
    void SetEnabled(bool on)
    {
        enabled_ = on;
    }

    // From the frame callback: a talent taken since the last frame is
    // sent again, asking for its target.
    void Serve();

  private:
    struct Taken
    {
        std::int32_t ability = 0;
        ObjectId caster = kNoObject;
    };
    bool installed_ = false;
    bool enabled_ = true;
    std::optional<Taken> taken_; // the game's thread's alone: the interceptor and the frame
    int logged_ = 0;
};

} // namespace tb::game
