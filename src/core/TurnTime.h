// Capacity and elapsed time for dev/REBALANCE.md. Pure accounting: callers
// stop at timer events before spending more. No engine clock is advanced.
#pragma once
#include <cstdint>
namespace tb
{
struct TacticsTime
{
    std::uint32_t bonusMs = 0;
    std::uint32_t rolloverCapMs = 1000;
};
// Player ranks 0..4; enemy bonuses above that range remain configuration.
[[nodiscard]] TacticsTime TimeFromTactics(int rank);
struct TimePayment
{
    std::uint32_t capacityMs = 0;
    std::uint32_t clockMs = 0;
};
struct TurnTimeEnd
{
    std::uint32_t rolloverMs = 0;
    std::uint32_t clockMs = 0;
};
class TurnTime
{
  public:
    // Pending work spends this capacity first; it does NOT enlarge the bar.
    // The caller supplies rollover already limited by its skill cap.
    explicit TurnTime(std::uint32_t turnMs = 4000, std::uint32_t bonusMs = 0, std::uint32_t rolloverMs = 0);
    [[nodiscard]] std::uint32_t Capacity() const;
    [[nodiscard]] std::uint32_t CapacityLeft() const;
    [[nodiscard]] std::uint32_t ClockElapsed() const;
    [[nodiscard]] std::uint32_t ClockLeft() const;
    // Capacity needed to reach an upcoming whole-millisecond clock event,
    // capped at this turn's remaining spendable capacity.
    [[nodiscard]] std::uint32_t CapacityUntilClock(std::uint32_t clockMs) const;
    // Actions/movement spend capacity; any unpaid action remainder may pend.
    [[nodiscard]] TimePayment Spend(std::uint32_t capacityMs);
    // Holds consume elapsed time. Fractional capacity is retained internally.
    [[nodiscard]] std::uint32_t Hold(std::uint32_t clockMs);
    // An action that never ran gives back what it was charged this turn,
    // and the clock goes back with it: nothing has aged on the creature's
    // clock yet, the clock being the mod's count until the engine's is
    // moved by it. No more than has been spent.
    void Refund(std::uint32_t capacityMs);
    // Bank remaining whole milliseconds and advance the clock's final share.
    // A fully held turn banks nothing; calling twice is inert.
    [[nodiscard]] TurnTimeEnd Finish(std::uint32_t rolloverCapMs);

  private:
    // A capacity millisecond is turnMs_ units; a clock millisecond is
    // capacityMs_ units. Products of two uint32 values fit in uint64.
    std::uint32_t turnMs_;
    std::uint32_t capacityMs_;
    std::uint64_t progress_ = 0;
    [[nodiscard]] std::uint64_t Total() const;
};
} // namespace tb
