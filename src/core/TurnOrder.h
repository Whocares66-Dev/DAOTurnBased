// The order of turns in a fight: every combatant, sorted by initiative once
// at the start, the one whose turn it is, and the round.
//
// A combatant who falls stays in the order, marked down, and loses its
// turns: a party member may be revived, and the view shows the fallen
// greyed. Fallen enemies and allies are dropped when the next round begins,
// and with them whoever has left the fight alive (out of the world, lost to
// the party, put out of the party: core/Rules.h, InTheRotation), a party
// member the game has taken out of the world too: nobody is there to show.
// A combatant who joins mid-fight takes its place by initiative; if that
// place is before the current turn, its first turn is next round.
#pragma once

#include "core/Types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tb
{

struct Combatant
{
    ObjectId id = kNoObject;
    Side side = Side::Enemy;
    bool hero = false; // the player's own character
    std::wstring name; // as GetName gives it
    std::wstring tag;  // the object's tag: a portrait is found by it (the portraits table)
    int rank = 0;      // CREATURE_RANK_*: 1 critter, 2 normal, 3 lieutenant, 4 boss, 5 elite boss, 100 player
    float dexterity = 10.0f;
    // How fast its build moves against a person (core/Rules.h, SpeedFactor):
    // its appearance's, which nothing in a fight changes.
    float speed = 1.0f;
    // Its movement rate effects as the last look at everyone found them
    // (CombatantStatus::movement; a scan reads none, and they are kept
    // through one): Haste, a salve, a slow. With its build they are the pace
    // its turn's meters are made from (core/Rules.h, Pace).
    MovementEffects movement;
    float health = 1.0f; // current over maximum
    float energy = 1.0f; // mana or stamina, the same
    bool mana = false;   // a mage's
    bool down = false;   // dead, dying, or gone from the fight
    // Fighting where the player can see it, not only near (Settings::
    // joinBySight): in a room the party has seen, or it or a party member
    // the other's attack target. An enemy or ally that is not waits outside
    // the order until it is; one in the order stays in it. The party's own
    // always are.
    bool engaged = true;
    int initiative = 0;
    // Its ranks in two skills, 0 to 4, a party member's own (the game
    // layer's, read from its abilities): Combat Tactics, time a turn with
    // the turn a bar of time (an enemy's is by its rank, Settings::
    // enemyTactics), and Survival, initiative (dev/REBALANCE.md).
    int tactics = 0;
    int survival = 0;
    // The engine's perception between it and the party, and of anyone it
    // would fight, as the scan read it (core/Types.h): a hostile that
    // perceives nobody to fight has not noticed, and its turn is skipped
    // (core/Rules.h, Rotation::Unaware).
    Perception perception;
    // Found by the scan's own rules for who is fighting: in combat within
    // combat_radius of a living party member, or drawn in unawares
    // (src/game/Bridge.cpp, Scan). False only for one of the fight that
    // those rules no longer find, read all the same because the machine
    // named it (Needs::inFight): whether it stays is then the engine's
    // perception's to say (core/Rules.h, InTheRotation). It brings nobody
    // into a fight.
    bool fighting = true;
    // The game has made it inactive, as the scan read it (the engine's
    // GetObjectActive: src/game/Bridge.cpp, Inactive): out of the world, and
    // so not in the fight, whoever it is (core/Rules.h,
    // Rotation::OutOfTheWorld; issue 66). In the order it is true of one
    // that left the fight so, or whose object is gone, until a scan finds
    // it in the rotation again: it has no card meanwhile.
    bool inactive = false;
};

class TurnOrder
{
  public:
    struct Slot
    {
        const Combatant *who;
        int round;
        bool current;
    };

    void Clear();

    // Sorts the combatants (their initiative already rolled) and starts
    // round 1 with the first standing.
    void Begin(std::vector<Combatant> combatants);

    [[nodiscard]] bool Empty() const
    {
        return order_.empty();
    }
    [[nodiscard]] int Round() const
    {
        return round_;
    }

    [[nodiscard]] const Combatant *Current() const;
    [[nodiscard]] Combatant *Current();
    [[nodiscard]] const Combatant *Find(ObjectId id) const;
    [[nodiscard]] Combatant *Find(ObjectId id);
    [[nodiscard]] const std::vector<Combatant> &All() const
    {
        return order_;
    }

    // The turn passes to the next combatant standing. True if that began a
    // new round (the fallen enemies and allies are dropped as it does).
    // With nobody standing the current turn stays where it lands.
    [[nodiscard]] bool Advance();

    // The current combatant's turn put off to the end of this round
    // (Delay, as Deadfire's: dev/REBALANCE.md, "Pillars as the reference"):
    // the turn is the next standing combatant's, and the round after begins
    // in initiative's order again. False, and nothing changed, with nobody
    // standing after it this round, or if it has put a turn off this round
    // already.
    [[nodiscard]] bool Delay();
    [[nodiscard]] bool CanDelay() const;
    // Whether it has put its turn off this round.
    [[nodiscard]] bool Delayed(ObjectId id) const;

    // A combatant arriving mid-fight, its initiative rolled.
    void Join(Combatant combatant);
    // It has a place in the fight to show: standing, or a fallen party
    // member, who may be revived; not one that is out of the world.
    [[nodiscard]] static bool Shown(const Combatant &c);

    [[nodiscard]] bool AnyStanding(Side side) const;

    // The turns to come, the current first and each combatant once, as BG3's
    // bar shows them: the rest of this round, then the next round's up to
    // the current, at most `count`. Two laps had everyone twice on the bar
    // (the user, 2026-09-30). Fallen party members are included (they are
    // still in the fight), fallen others not.
    [[nodiscard]] std::vector<Slot> Upcoming(std::size_t count) const;

    // Initiative high to low; then Dexterity; then the party, allies,
    // enemies; then the id, so the order never depends on the input's.
    [[nodiscard]] static bool Before(const Combatant &a, const Combatant &b);

  private:
    void DropFallenOthers();

    std::vector<Combatant> order_;
    std::vector<ObjectId> delayed_; // who put a turn off this round: they stand at the order's end, out of place
    std::size_t current_ = 0;
    int round_ = 0;
};

} // namespace tb
