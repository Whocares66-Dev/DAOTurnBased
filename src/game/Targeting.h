// A talent chosen on the quickbar in a turn-based fight asks for its
// target, as in BG3 (the user, 2026-09-30: with a target selected the
// game used it on that target at once, "sometimes it allows targeting and
// sometimes it doesn't").
//
// The quickbar sends PerformAbilityMessage, whose handler uses the ability
// on the caster, the selected target or the caster's attack target, and
// asks only failing all three (DAOLib's dao/Messages.h, read 2026-09-30);
// a character keeps its attack target from turn to turn. So during a
// fight, for the current creature's talent that asks (core's
// AsksForTarget), the message is taken (DAOLib's InterceptMessage) and the
// game asked for the target instead (GameModeExplore::RequestTarget,
// DAOLib 1090803); the click then uses the ability, through the gate as
// any order. Behind Settings::pickTargets. Not yet seen in play.
#pragma once

#include <mutex>

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
    // mutex, whose turn it is.
    void Install(const TurnMachine &machine, std::mutex &mutex);

  private:
    bool installed_ = false;
    int logged_ = 0; // the game's thread's alone: messages are delivered there
};

} // namespace tb::game
