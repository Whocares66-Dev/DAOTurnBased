// What a turn has to spend, and what it has spent. Two kinds:
//
// - One action and meters of movement (dev/DESIGN.md, as built): an attack,
//   an ability or an item spends the action, walking spends meters.
// - A bar of time (dev/REBALANCE.md, "Current specification": `[turns]
//   clock = time`): everything spends the same bar, an action its own time
//   and walking its distance at the creature's pace, with about a meter a
//   turn free. An action longer than what is left is begun and its rest is
//   owed to the next turn (Overflow); TurnTime keeps the bar and the
//   creature's clock apart.
//
// The machine asks it the same questions either way: is there an action
// left, how far can it walk, has it walked past what it had. So the rules
// that do not care which kind it is (who may act, when a walk is stopped,
// what a turn shows) are written once.
#pragma once

#include "core/TurnTime.h"

#include <cstdint>

namespace tb
{

class TurnBudget
{
  public:
    // As built: `actions` actions (one) and `meters` of movement.
    [[nodiscard]] static TurnBudget Actions(int actions, float meters);
    // A bar of time: `time` (its length, Combat Tactics' time and what
    // rolled over), walking at `paceMps` meters a second after the first
    // `freeMeters`.
    [[nodiscard]] static TurnBudget Time(const TurnTime &time, float paceMps, float freeMeters);

    [[nodiscard]] bool Timed() const
    {
        return timed_;
    }

    // Actions: one, or none. With time, there is one while any time is
    // left: an action that does not fit is begun and finished next turn.
    [[nodiscard]] int ActionsLeft() const;
    // Spends an action: as built one of them; with time `costMs` of the
    // bar, and answers what did not fit, owed to the next turn.
    [[nodiscard]] std::uint32_t SpendAction(std::uint32_t costMs);
    // An action that never ran is given back: as built the action; with
    // time what it was charged this turn.
    void GiveActionBack(std::uint32_t chargedMs);
    // Nothing more to act with this turn (a held turn, a charge begun, an
    // action kept for the next): with time, nothing more to walk either.
    void SpendAllActions();

    // Movement, in meters: with time, what the time left (and the free
    // meter) would walk at the creature's pace.
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
    // With time: what walking `meters` from here would cost of the bar, after
    // the meters already paid for and the free meter. Nothing as built.
    [[nodiscard]] std::uint32_t WalkCostMs(float meters) const;
    // With time: pays now for a walk about to be taken (an attack's walk to
    // its target, paid as the attack is let through so that its swing
    // cannot leave its walk short), and lets that many meters be walked
    // without paying again. Meters paid for and not walked stay walkable
    // this turn. Nothing as built.
    void Prepay(float meters);
    // Meters to walk without paying: a pending attack's walk, paid for in
    // what it owed, as it lands.
    void Credit(float meters);

    // The bar, with time; zeros as built.
    [[nodiscard]] std::uint32_t CapacityMs() const;
    [[nodiscard]] std::uint32_t CapacityLeftMs() const;
    // The turn's end: what rolls over, up to `rolloverCapMs`, with time.
    [[nodiscard]] std::uint32_t Finish(std::uint32_t rolloverCapMs);

  private:
    TurnBudget() = default;

    bool timed_ = false;
    int actions_ = 0;
    float movementMax_ = 0; // as built
    TurnTime time_{};       // with time
    float pace_ = 1.0f;     // meters a second
    float free_ = 0;        // meters walked without time, with time
    float walked_ = 0;      // meters walked this turn
    float freeWalked_ = 0;  // of which free
    float owedMeters_ = 0;  // walked and not yet paid: less than a millisecond's worth
    float overwalked_ = 0;  // walked past the time left
    float credit_ = 0;      // meters paid for and not yet walked
};

} // namespace tb
