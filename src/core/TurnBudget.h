// What a turn has to spend, and what it has spent: a bar of time
// (dev/REBALANCE.md, "Current specification"). Everything spends the same
// bar, an action its own time and walking its distance at the creature's
// pace, with about a meter a turn free. An action longer than what is left is
// begun and its rest is owed to the next turn (Overflow); TurnTime keeps the
// bar and the creature's clock apart.
//
// A turn that is passed over (lost to a hold, spent on a pending action, its
// creature out of the world) has no bar: a budget made of nothing, with
// nothing to spend, to walk or to roll over.
//
// Until issue 50 (2026-10-05) there was a second kind, the one the mod first
// played with: one action and an allowance of meters, `[turns] clock =
// actions`. The time bar replaced it in play, and it went.
#pragma once

#include "core/TurnTime.h"

#include <cstdint>

namespace tb
{

class TurnBudget
{
  public:
    // No bar: a turn passed over.
    TurnBudget() = default;
    // A bar of time: `time` (its length, Combat Tactics' time and what
    // rolled over), walking at `paceMps` meters a second after the first
    // `freeMeters`.
    [[nodiscard]] static TurnBudget Time(const TurnTime &time, float paceMps, float freeMeters);

    [[nodiscard]] bool HasBar() const
    {
        return bar_;
    }

    // Whether any time is left: an action may begin while there is, and one
    // that does not fit is begun and finished next turn.
    [[nodiscard]] bool TimeLeft() const;
    // Spends an action's time: `costMs` of the bar, and answers what did not
    // fit, owed to the next turn.
    [[nodiscard]] std::uint32_t SpendAction(std::uint32_t costMs);
    // An action that never ran is given back what it was charged this turn.
    void GiveActionBack(std::uint32_t chargedMs);
    // Nothing more to act with this turn (a held turn, an action kept for
    // the next), and nothing more to walk either.
    void SpendAllActions();
    // The bar and the free meter spent, and the meters paid for kept: an
    // action is kept for the next turn, and its creature walks to where it
    // can do it.
    void Pend();

    // Movement, in meters: what the time left (and the free meter) would
    // walk at the creature's pace.
    [[nodiscard]] float MovementLeft() const;
    [[nodiscard]] float MovementMax() const;
    [[nodiscard]] float Walked() const
    {
        return walked_;
    }
    // Meters walked past what there was to walk (the machine stops a walk
    // a little way past, kOvershoot).
    [[nodiscard]] float Overwalked() const;
    void Walk(float meters);
    // The meters walked past the bar are paid now, out of time given back
    // since (an action's, taken on arriving instead).
    void SettleOverwalk();
    // What walking `meters` from here would cost of the bar, after the
    // meters already paid for and the free meter.
    [[nodiscard]] std::uint32_t WalkCostMs(float meters) const;
    // Pays now for a walk about to be taken (an attack's walk to its target,
    // paid as the attack is let through so that its swing cannot leave its
    // walk short), and lets that many meters be walked without paying again.
    // Meters paid for and not walked stay walkable this turn.
    void Prepay(float meters);
    // Meters to walk without paying: a pending attack's walk, paid for in
    // what it owed, as it lands.
    void Credit(float meters);

    // The pace walked at, and the meters that cost nothing (the free meter
    // left, and meters paid for). Zeros with no bar.
    [[nodiscard]] float PaceMps() const
    {
        return bar_ ? pace_ : 0.0f;
    }
    // The pace from here on, its creature hasted or slowed in the middle of
    // its turn (issue 49). What is left to walk is the time left at the new
    // pace; the time already spent stays spent, and meters paid for and not
    // yet walked stay paid for, whatever they would cost now. What is no
    // pace (nothing, a negative, not a number) is not taken.
    void SetPace(float paceMps);
    [[nodiscard]] float PaidMeters() const;

    // A hold takes `clockMs` of the creature's clock, and with it that share
    // of the bar (a 2 s stun half a 4 s turn's, whatever the bar holds).
    // Answers the clock it took.
    std::uint32_t HoldClock(std::uint32_t clockMs);

    // How far the creature's own clock has moved this turn, of the turn's
    // seconds: by its share of the bar spent (TurnTime).
    [[nodiscard]] std::uint32_t ClockElapsedMs() const;

    // The bar; zeros with none.
    [[nodiscard]] std::uint32_t CapacityMs() const;
    [[nodiscard]] std::uint32_t CapacityLeftMs() const;
    // The turn's end: what rolls over, up to `rolloverCapMs`.
    [[nodiscard]] std::uint32_t Finish(std::uint32_t rolloverCapMs);

  private:
    bool bar_ = false;
    TurnTime time_{};
    float pace_ = 1.0f;    // meters a second
    float free_ = 0;       // meters walked without time
    float walked_ = 0;     // meters walked this turn
    float freeWalked_ = 0; // of which free
    float owedMeters_ = 0; // walked and not yet paid: less than a millisecond's worth
    float overwalked_ = 0; // walked past the time left
    float credit_ = 0;     // meters paid for and not yet walked
};

} // namespace tb
