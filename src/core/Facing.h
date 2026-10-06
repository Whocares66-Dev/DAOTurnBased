// A creature all but facing what its attack is aimed at is faced at it
// exactly before the command is given (issue 40).
//
// What it is for. An attack, and an ability aimed at another, turns its
// creature to face the target before it acts: the command's sub-action 39.
// In a turn-based fight that turn could wait for good: a kept attack given
// back stood twelve seconds at sub-action 39, a degree off its target with
// nothing turning, until the action limit ended the turn (2026-10-05, a
// Hurlock with a bow at Ostagar, caught with a diagnostic build; before it
// two Carta Thugs, a Hurlock and one of the party were each seen at
// sub-action 39 until the limit, of ten stuck attacks in the kept logs).
//
// Why, as DAOLib's session read it in the game's executable (2026-10-05, its
// issue 25), and as the log of that day's play had it:
// - The command's test (CSSubActionHandler::TurnToward, 0x007A51F0): the
//   angle d between the creature's facing and the direction to the target's
//   point. Done when |d| <= 1.0 degree, a plain <= against the float 1.0, on
//   its first run as on every later one; within a degree on the first run it
//   answers done at once and touches nothing. While |d| > 1.0 it answers
//   "still turning".
// - The turn itself is the movement controller's: TurnToward gives it the
//   wanted facing, and its update turns the creature only while it is at a
//   turn or a walk (its states 3 and 4; the path follower leaves it in 3 as a
//   walk arrives). Idle, state 0, it copies the creature's facing and does
//   nothing; and being given a wanted facing begins a turn by itself only
//   when that is already within about 0.08 degrees of its own.
// - So a creature at rest, its controller idle, a little over a degree off
//   its target, waits at sub-action 39 for a turn nothing on that path
//   begins. Larger angles from rest are taken up some other way: by the
//   animation's own turn, as DAOLib's session takes it (not read; only the
//   edge has been seen to stick). Of 141 turns to face in that log, the 140
//   that ended began with the controller turning (each followed an
//   approach), the smallest at -1.02 degrees; the one that hung began at
//   -1.00 with it idle.
// - Kept attacks find the edge by themselves: the creature turned at this
//   same target a turn before, that turn was over as soon as the engine's
//   test was satisfied, a degree short, and neither has moved since.
//
// The rule: one within kFacedExactlyWithinDegrees of facing its target is
// faced at it exactly as the command is given, whatever its controller is
// at, which the mod does not read; the command's first run then finds
// |d| <= 1.0 and asks for no turn. At that or over it nothing is done: the
// engine's own turn takes it, as it always did. The turn itself is left
// alone: DAOLib's session advises against touching sub-action 39 or the
// controller's state. The game layer measures and sets (src/game/Gate.cpp,
// where every command reaches a creature, and where what the scripts' facing
// calls do is written); what is decided here needs no game.
#pragma once

#include "core/Types.h"

#include <optional>

namespace tb
{

// The engine's own test, for setting beside the rule's: a creature within
// this of facing its target is taken for facing it, and its command turns
// nothing (TurnToward, above). Not the rule's number: the mod's measure of
// the angle is its own sum over positions the scripts hand it, and is not
// held to the engine's last bit, so nothing is decided at one degree.
inline constexpr float kEngineFacesWithinDegrees = 1.0f;

// The rule's: under this many degrees off, faced exactly. Five is DAOLib's
// session's number (2026-10-05): up to five a creature set straight is not
// seen to move, and from there up the engine's own turn has always worked
// (in the log, every turn of 3 degrees or more ended, in 569 ms at most).
// The edge it is for is at one.
inline constexpr float kFacedExactlyWithinDegrees = 5.0f;

// One this near already faces its target as exactly as it can be set (a
// facing set and read back differs by a float's rounding): nothing to do,
// and nothing to say or count. Far under the engine's degree.
inline constexpr float kFacesAlreadyDegrees = 0.05f;

// How far a creature has to turn to face a point: the angle from its facing
// to the direction toward the point, in degrees on the ground, from -180 to
// 180 (positive and negative are the two ways round; which is which is the
// engine's axes' business, and only the size is used). Both are directions
// on the ground, of any length. Nothing where either has none: a facing
// never read, or a target where its creature stands.
[[nodiscard]] std::optional<float> TurnDegrees(float facingX, float facingY, float towardX, float towardY);

enum class Facing : std::uint8_t
{
    Leave, // at the rule's angle or over it: the engine's own turn takes it
    Faces, // it faces its target already
    Face,  // all but: faced at it exactly before the command is given
};
[[nodiscard]] Facing FacingFor(float turnDegrees);

// The commands that turn their creature to face a target before they act,
// and so the ones the rule is for: an attack, and an ability's use. Both run
// sub-action 39 (seen in play, DAOLib's dao/Conjure.h), by one handler
// (0x007A69E0, read 2026-10-05 for this): it takes the sub-action's target
// object, fails with none, and turns toward where the target stands for a
// creature with a ranged weapon (CCCreature::HasRangedWeapon), else toward
// the target's own point for an attacker standing where this one does (its
// slot 29: a point of one of its melee rings, which for a person is where it
// stands, by DAOLib's reading). An ability aimed at a place or at its own
// user has no other object to turn to, which the game layer tells by having
// no angle to give.
[[nodiscard]] bool TurnsToFace(CommandType type);

} // namespace tb
