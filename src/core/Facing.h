// A creature the engine would not turn to face what its attack is aimed at
// is faced at it exactly before the command is given (issue 40).
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
// issue 25; this is its reading as corrected after that day's two captures
// of a hang, 17:01 and 19:41), and as the logs had it:
// - The engine keeps two facings and has two tests. The command's test
//   (CSSubActionHandler::TurnToward, 0x007A51F0) is of the creature's facing
//   (its +0x130) against the direction to the target's point, each an angle
//   by an arc tangent, their difference wrapped: done when it is at most 1.0
//   degree, a plain <= against the float 1.0, on its first run as on every
//   later one; within a degree on the first run it answers done at once and
//   touches nothing. Over a degree it asks the movement controller for the
//   turn and answers "still turning".
// - The controller begins a turn (SetDesiredFacing, 0x00A638B0: its state 0
//   to its state 4) only when the facing wanted is more than about 1.0
//   degree from the controller's own facing, which is another one, kept in
//   the controller, and measured another way (the arc cosine of a dot
//   product, in float). The controller's own facing follows the creature's
//   only while a flag of the controller's is set, which two functions of
//   the engine clear and set, so the two facings can differ by the engine's
//   own doing, and then the two tests can disagree for ever: the command
//   not done, the controller with nothing to turn. That is the dead case.
// - The hang of 17:01 (a Hurlock's kept attack given back): both facings the
//   same, the aim "1.00 degree" off, the arc tangent and the arc cosine in
//   float falling on opposite sides of one degree. Of 141 turns to face in
//   that log, the 140 that ended began with the controller turning (each
//   followed an approach), the smallest at -1.02 degrees; the one that hung
//   began at -1.00 with it idle. Kept attacks find that edge by themselves:
//   the creature turned at this same target a turn before, that turn was
//   over as soon as a test was satisfied, a degree short, and neither has
//   moved since.
// - The hang of 19:41 was this rule's own first form's doing (below): it
//   set a creature's facing 3.95 degrees off the engine's aim while the
//   controller's own stayed on the aim. The controller saw nothing to turn,
//   and the command never saw its creature facing. So the dead case is not
//   the edge alone: it is any creature not within the command's degree
//   whose controller is idle with its own facing within a degree of the aim
//   (or with its motion record busy, a second thing SetDesiredFacing begins
//   no turn for, which the address library's session found in it).
//
// The rule: a creature whose attack or ability has come to its turn to face,
// that the engine would not turn, and that is not facing its aim, is faced
// at it, at whatever angle. Whether that is so is the engine's to say, and
// DAOLib asks it (dao::attack::TurnTo, its issue 32: the turn left, the
// engine's own test's "done", and "stalls", which is the dead case by the
// engine's own measures); the game layer looks at each of the fight's
// creatures every frame and hands the answer here for those at that turn
// (src/game/Bridge.cpp). The facing is DAOLib's too
// (dao::attack::FaceExactly): the creature's facing, the facing its
// controller wants and the controller's own, all three, so that nothing is
// left apart. Everything else is the engine's own turn, as it always was:
// one it has for facing is left as it stands, and so is one it would turn,
// however little.
//
// It is asked where the engine asks it: at sub-action 39, not as the
// command is given. An attack's command begins with its approach, and the
// turn to face comes after it, by which time a walk has turned its creature
// and left the controller in the state every turn to face in the logs ended
// from by itself. And the engine has two refusals to begin a turn, which
// DAOLib tells apart (its issue 32, after the third form's spin below):
// "stalls", the controller's own facing within its degree of the aim, which
// never passes by itself, and "waits", the controller's motion record busy
// (its creature's own animation still playing: one just out of an ability,
// say), which does, since TurnToward asks the controller again on every
// update. One that waits is left, and looked at again with the next frame;
// one that stalls is faced at the first look.
//
// The rule's earlier forms, all of 2026-10-05, and why each went:
// - The first measured by itself, from the scripts' GetOrientation and two
//   positions, to where the target stands, and set its creature's facing
//   there with the scripts' SetOrientation when that was under 5 degrees. In
//   play it made the hang it was for: a blow at a big creature is aimed by
//   the engine at a point of its ring, not at where it stands, and the mod
//   turned a creature that faced the engine's aim 3.95 degrees off it
//   (`Zevran stands 3.95 deg off Great Bear as its attack is given: faced at
//   it exactly`, 19:41, and his attack at sub-action 39 until the limit).
// - The second took the engine's measure and aim (TurnTo) and still set the
//   creature's facing alone, under 5 degrees. A facing set on the creature
//   alone leaves the controller's own behind; a creature that stands and is
//   faced more than once gathers a gap; and then an aim 5 degrees or more
//   from the creature and within a degree of the controller's stale facing
//   begins no turn and is outside the bound: the hang again, made possible
//   by the rule itself. The user asked why the set left the controller's
//   facing behind, and the answer was that it must not. It was not gated or
//   played.
// - The third was this rule asked as the command was given, at the gate. It
//   was three hours in the game, about twenty fights, and mended what it was
//   for twice (an ally's blow 13.42 degrees off, struck 7 ms after; a kept
//   attack at an Ogre, 2.94 degrees off the point of its ring, 0.15 m from
//   where it stood), with nothing stuck at a turn to face in any fight. And
//   once it spun a creature for nothing (22:15:13): `Alistair: attack with
//   27.1 m to walk`, `Alistair stands 159.43 deg off Dragonling as its
//   attack is given: faced at it exactly`, `0x1b88's attack is at
//   sub-action 4 (its approach)`: turned 159 degrees in a frame for a
//   command that began with a walk, which would have turned him. And he
//   was no dead case: just out of an ability's animation, his controller's
//   motion record was busy, which DAOLib's "stalls" then took in and now
//   calls "waits".
// So the angle the mod chose is gone (the 5 degrees, and the first form's
// floor of a twentieth of one under which a creature "faced already"), with
// the mod's own measure, its own set, and the asking at the gate. What is
// decided here needs no game.
#pragma once

#include "core/Types.h"

#include <cstdint>
#include <optional>

namespace tb
{

// The turn a command would make to face its target, as the engine has it
// when the game layer asks (DAOLib's dao::attack::FacingTurn): how far, in
// degrees from -180 to 180 (which way round is which is the engine's
// business; only the size is said); whether the engine's own test takes it
// for done, which is the command going past its turn at its next run;
// whether the turn stalls, the dead case: not done, and the engine will
// begin no turn for it, now or later; and whether it waits: not done, and
// the engine begins no turn for it yet, its creature's own motion still
// playing, which passes by itself.
struct TurnToFace
{
    float degrees = 0.0f;
    bool done = false;
    bool stalls = false;
    bool waits = false;
};

// The game layer's look at one of the fight's creatures whose current
// command is at its turn to face a target (sub-action 39): whose, at whom,
// what the command is and which (the engine's own count of commands, so
// that looks at one command are not counted with another's), and the turn
// as the engine has it.
struct TurnLook
{
    ObjectId who = kNoObject;
    ObjectId target = kNoObject;
    CommandType type = CommandType::Invalid;
    std::uint32_t command = 0;
    TurnToFace turn;
};

// What came of facing a creature the machine said to (EffectKind::
// FaceTarget), for the machine to say and count: how far the engine had it
// off as the facing was asked for (0: the engine had no answer then),
// whether the engine took the facing (DAOLib's FaceExactly answers no for a
// creature whose movement controller is not idle, and sets nothing), and
// the turn the engine has for it after, asked again (nothing: no answer).
struct FacedLook
{
    ObjectId who = kNoObject;
    ObjectId target = kNoObject;
    CommandType type = CommandType::Invalid;
    float stood = 0.0f;
    bool taken = false;
    std::optional<TurnToFace> now;
};

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
// no look to give.
[[nodiscard]] bool TurnsToFace(CommandType type);

} // namespace tb
