// The framing of the cards' portraits, all of it in one place: a table, a row
// a creature type (dev/UI.md, "Portraits"). The user, 2026-10-04, having seen
// the dragons turned: "the angles look good. if we can put the portrait
// angles and stuff in a table somewhere by creature type, we'd have it all in
// one place."
//
// Every number about how a portrait is framed is here and nowhere else. The
// rule's code (core/Portraits.h, FramePortrait) only looks a creature up:
// its own row; else, where the game's portraits table frames the creature
// itself, the game's; else its kind's default row; and a person's body is
// the game's own Default row, which is no row of this table. What the
// engine's camera does with the numbers (it stands three times the distance
// from the head, draws to 40 m) is the engine's, and is in core/Portraits.h.
//
// A row is a camera somebody looked at. It is filled from the sweep
// (tools/portraits.py: a real creature of each appearance stood beside the
// party, rendered and pictured; --row tries a camera for one, --search a
// grid of them, and each prints its line for this table, as --print-row does
// from numbers alone). A kind's default is for what nobody has looked at yet.
//
// The columns, as the game's own portraits table has them (portraits.gda):
//   the row     the appearance table's row and its label there ("64
//               Dragonling"); for a default, the bodies it is for
//   distance    metres, CameraDistance. The engine's camera stands three
//               times as far from the head node, and the picture is 0.525 of
//               the distance across
//   rotation    degrees, CameraRotation: the turn of a side profile. The user
//               asked for "a 30 to 45° angle", as people have; 35 is what was
//               looked at. The head slides sideways in the picture as the
//               view turns, by an amount and to a side that are each model's
//               own, and the aside brings it back: a rotation without its
//               aside loses it, for all but the few models that have none
//   pitch       degrees, CameraPitch
//   aside, up   metres, CameraXAdjust and CameraZAdjust: the camera moved to
//               its own right and up, and the picture's middle with it
//   the note    where the row came from: who saw it, where and when, and
//               what of it was seen as it is and what lies between two seen
// and, where a row needs its own, the light rig's offset (Lit).
//
// A default's distance, aside and up are per metre of the creature's extent
// (its feet to its head node, as the engine measures it: core's ExtentOf),
// and the camera it gives is held between the two limits below.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace tb
{

// The kind of body a portrait frames, which says whose default a creature
// with no row of its own takes (core/Portraits.h: BodyKindOf decides it).
enum class BodyKind : std::uint8_t
{
    Person,     // a person's body: framed by the game's own Default row
    Upright,    // stands up, and is not a person's: bigger, or a creature's model
    OnAllFours, // pitches with the ground it stands on
};

struct FramingRow
{
    std::int32_t appearance = 0;       // the appearance table's row; 0 for a kind's default
    BodyKind kind = BodyKind::Upright; // a default's: whose it is
    std::string_view label;
    float distance = 0.0f;
    float rotation = 0.0f;
    float pitch = 0.0f;
    float aside = 0.0f;
    float up = 0.0f;
    std::string_view note;
    bool lit = false; // the light's three are the row's own, else the game's table's
    float lightX = 0.0f;
    float lightY = 0.0f;
    float lightZ = 0.0f;
    bool fromRoot = false; // its model has no head node: the camera is about its root (FromRoot)
};

// An appearance's own row.
[[nodiscard]] constexpr FramingRow Row(std::int32_t appearance, std::string_view label, float distance, float rotation,
                                       float pitch, float aside, float up, std::string_view note)
{
    return FramingRow{
        appearance, BodyKind::Upright, label, distance, rotation, pitch, aside, up, note, false, 0.0f, 0.0f, 0.0f,
        false};
}

// A kind's default: for any body of the kind with no row of its own. Its
// distance, aside and up are per metre of the creature's extent.
[[nodiscard]] constexpr FramingRow DefaultFor(BodyKind kind, std::string_view label, float distancePerExtent,
                                              float rotation, float pitch, float asidePerExtent, float upPerExtent,
                                              std::string_view note)
{
    return FramingRow{
        0,    kind, label, distancePerExtent, rotation, pitch, asidePerExtent, upPerExtent, note, false, 0.0f,
        0.0f, 0.0f, false};
}

// A row for a model with no head node, which the engine's camera has nothing
// to aim at: a deepstalker, a halla, a pig. The engine then aims at the
// origin of the portrait's own scene. DAOLib's session read it in the exe,
// 2026-10-04: PortraitRenderer::PlaceCamera (0x007F3130 in GOG's build) sets
// the point it hands CCCreature::HeadTransform to (0, 0, 0, 1) just before
// the call, HeadTransform writes it only where it finds a node "head", and
// what it returns is not looked at; the rest is as for any creature (the
// distance, the clone turned by the rotation, the pitch, the two adjusts
// moving camera and aim together). That origin is the clone's, its root, at
// its feet: seen in play the same day, a deepstalker rendered from 4 m with
// the camera half a metre up standing whole on the picture's lower third.
// So such a row's camera carries the whole framing from there: up is the
// height of what is wanted above its feet, and the aside how far before it
// that is, as the turn shows it. Marked, since a row that
// is about a head must not frame a creature whose head is only late (a
// creature just made has no head node for some frames, and a werewolf's
// took more than four seconds once): unmarked rows wait for the head.
[[nodiscard]] constexpr FramingRow FromRoot(FramingRow row)
{
    row.fromRoot = true;
    return row;
}

// A row with a light of its own (LightXAdjust, LightYAdjust, LightZAdjust).
[[nodiscard]] constexpr FramingRow Lit(FramingRow row, float lightX, float lightY, float lightZ)
{
    row.lit = true;
    row.lightX = lightX;
    row.lightY = lightY;
    row.lightZ = lightZ;
    return row;
}

// What a default's camera is held between, in metres of distance. No nearer:
// a measure next to nothing (a rat's) would put the camera inside what it
// looks at. No further: the engine's camera draws to 40 m and stands three
// times the distance from the head, so from 12 m the head and 4 m of the
// creature behind it are still drawn (from 13.86 m the high dragon's card was
// a blank disc, issue 32); the picture is then 6.3 m across.
inline constexpr float kFramedFromLeast = 0.3f;
inline constexpr float kFramedFromMost = 12.0f;

// The game's own Default row, as it ships (portraits.gda, read 2026-10-04
// with DAOLib's tools/gda.py): a person's face a little from its left. Not a
// row of the table below: a person's body is framed by the game's own, and
// the game layer reads the running game's, which a mod may have changed.
// This stands for it until then, and in the tests.
inline constexpr FramingRow kGamesDefaultRow =
    Row(0, "the game's Default row", 0.675f, 23.5f, -7.5f, 0.007f, 0.038f, "portraits.gda, as the game ships it");

// The table. The appearances' rows in their order, then the kinds' defaults.
//
// 47, the ogre, is the one row framed with a person's pitch and rotation: the
// user, 2026-10-02, of its portrait from further back at a person's offsets,
// "ogre portrait isn't quite centered"; centred by sight that evening. It
// was a sum over its personal space until 2026-10-04, and is its row since
// the sum went.
//
// 64, 66 and 80 are the tester's, 2026-10-04, with the turnbased_portrait
// tool on real creatures (the game's own frames kept), each turned 35
// degrees with the aside that brings its head back to the middle; the user,
// seeing them: "the angles look good". The high dragon's row is also there
// because its measure is a pose's: 9.21 m from feet to head asleep, 20.5 m
// in the air, and some 7 m standing at the camp (6.95 and 7.20 in two
// sweeps), where the row still has its head and horns whole, in the middle
// and a little low (the sweep by this table, the same day).
//
// The rest are from the sweep of that day (tools/portraits.py, a real
// creature of each appearance spawned at the camp): first the rows whose
// picture missed the head or showed skin (4, 7, 11, 23, 27, 31, 32, 65),
// then those that were good straight on and had no turn (the dogs, the
// small animals, the broodmother, the pride demon, the archdemons). Each was
// found with --search, a grid of asides and ups at 35 degrees, and the one
// chosen looked at as it is, a cell of the grid or rendered again; then all
// of them once more in a sweep by this table (--from-table). The sheets are
// kept (build/portraits, not in the repository).
//
// What the looking showed of the turn: the dragons' and the archdemon's
// asides are to one side (-0.5 to -1.7 m), the other bodies' turned as they
// are to the other (0.03 to 0.6 m), and for two, the cat and the rage demon,
// the head stays in the middle with none: each model's own, again.
//
// Four are persons' bodies by the kind's rule (BodyKindOf), which the game's
// own Default row framed until they had rows: 7 and 23, the steel golem and
// the shriek, whose heads it missed, are turned as the creatures are; 17 and
// 22, the hurlock alpha and the werewolf, keep a person's pitch and rotation
// from further back, where the alpha's horns and the werewolf's muzzle are
// whole.
//
// Nine are from the root (FromRoot, above), their models having no head
// node: 33, the deepstalker, the one of them that fights (the user, of its
// initials: "We should try to capture the lamprey like head"), and 73, 76,
// 77, 78, 81, 85, 86 and 87, which came easily after it. Each was rendered
// from far first, to see it whole and where its head is from its feet, and
// then with the camera reckoned from that; the deepstalker's turn was
// chosen among six, since its head is turned on its neck.
//
// 65, the wild sylvan, is the one not got right: no face was found on it,
// and its creature stays hostile whatever its group, so each look began a
// fight and its row was not itself rendered.
//
// The defaults are straight on. A turn is what is wanted of them too, and
// needs its aside, which is each model's own: for 35 degrees it was 0.36 of
// the extent for the dragonling and the drake (one body at two sizes) and
// 0.18 for the high dragon, in a picture a third of the extent across; and
// what the engine measures of a creature does not give it (its head node is
// 0.35, 0.65 and 7.07 m from where it stands, level). Guessed half wrong, the
// aside puts the head out of the picture; straight on, what the engine aims
// at is in the middle whatever the model. Their distance, 0.6 of the extent,
// is what the same three wanted by eye straight on (0.8 m for 1.4, 1.4 for
// 2.38, 6 for 9.21). A head well above what the engine aims at is still cut
// off (the dragonling's was, 0.5 m above it): a row's work.
inline constexpr std::array kPortraitTable{
    // The row and its label, then distance, rotation, pitch, aside, up, and where it came from.
    Row(4, "4 Dragon, Normal", 1.5f, 35.0f, 0.0f, -0.78f, 1.05f,
        "by eye at the camp, 2026-10-04: its head and horns in profile; from searches of asides -0.98 to -0.5 and "
        "ups 0.67 to 1.15; seen as it is"),
    Row(7, "7 Golem, Steel", 1.8f, 35.0f, 0.0f, 0.27f, -0.12f,
        "by eye at the camp, 2026-10-04: its head in the ring of its shoulders, which it is low between; from a "
        "search of asides 0.1 to 0.4 and ups -0.25 to 0.05 at 1.5 m, where it was plates and no head to the eye, "
        "then back; seen as it is"),
    Row(10, "10 Broodmother", 2.1f, 35.0f, 0.0f, 0.3f, -0.2f,
        "by eye at the camp, 2026-10-04: its face in profile, its mouth open; from a search of asides -1 to 1 and "
        "ups -1 to 1; seen as it is"),
    Row(11, "11 Bronto", 3.0f, 35.0f, 0.0f, 0.35f, -0.45f,
        "by eye at the camp, 2026-10-04: its head with its horns and tusks, which is low before its body; from "
        "searches of asides -0.6 to 0.6 and ups -0.65 to 0.6; seen as it is"),
    Row(17, "17 Hurlock - Alpha", 0.9f, 23.5f, -7.5f, 0.007f, 0.06f,
        "by eye at the camp, 2026-10-04: its helm and horns whole, at a person's pitch and rotation from further "
        "back; from a search of asides -0.06 to 0.08 and ups -0.01 to 0.13; seen as it is"),
    Row(20, "20 Nug", 1.0f, 35.0f, 0.0f, 0.03f, 0.02f,
        "by eye at the camp, 2026-10-04: its head, its ears whole; from a search of asides -0.2 to 0.2 and ups -0.2 "
        "to 0.2; seen as it is"),
    Row(22, "22 Werewolf A", 1.05f, 23.5f, -7.5f, 0.15f, 0.1f,
        "by eye at the camp, 2026-10-04: its head whole, its ears and its muzzle, at a person's pitch and rotation "
        "from further back (a person's body: by the game's own row its muzzle was cut off); from a search of asides "
        "-0.03 to 0.15 and ups -0.05 to 0.13; seen as it is"),
    Row(23, "23 Shriek A", 1.2f, 35.0f, 0.0f, 0.2f, 0.1f,
        "by eye at the camp, 2026-10-04: its face and ear before its crest; from a search of asides -0.2 to 0.2 and "
        "ups -0.2 to 0.2; seen as it is"),
    Row(27, "27 Rage Demon", 1.8f, 35.0f, 0.0f, 0.0f, -0.15f,
        "by eye at the camp, 2026-10-04: its head and eye; from searches of asides -0.45 to 0.45 and ups -0.45 to "
        "0.45; seen as it is (turned, its head stays in the middle: no aside)"),
    Row(31, "31 Shade", 1.3f, 23.5f, 0.0f, 0.04f, -0.26f,
        "by eye in the Blackmarsh, 2026-10-09: its hood and the eye under it, turned toward the camera at a "
        "person's turn and to the right of the middle; the user, of the Awakening shade framed as this one was "
        "at 35 degrees from 1.6 m, 0.2 aside and -0.25 up (by eye at the camp, 2026-10-04): 'it should be moved a "
        "little bit to the right and then turn towards the camera a little bit. Right now it's too much "
        "profile'; this row by 20003's, for its size; seen as it is"),
    Row(32, "32 Ash Wraith", 1.7f, 35.0f, 0.0f, 0.4f, -0.3f,
        "by eye at the camp, 2026-10-04: its skull under its crest; from searches of asides -0.5 to 0.5 and ups -0.5 "
        "to 0.5; seen as it is"),
    FromRoot(Row(33, "33 Deepstalker", 0.9f, 20.0f, 0.0f, 0.29f, 0.83f,
                 "by eye at the camp, 2026-10-04, from its root: its head at the end of its neck, its eye and the "
                 "round toothed mouth, three-quarter on. Turned 20 degrees and no more: its head is turned on its "
                 "neck, and at 35 its mouth is edge on (10, 15, 20, 25, 35 and 50 were looked at). Nine renders "
                 "with one camera had its head in one place; seen as it is")),
    Row(34, "34 Dog, Mabari", 0.95f, 35.0f, 0.0f, 0.15f, 0.04f,
        "by eye at the camp, 2026-10-04: its head in three-quarter profile, its ears whole; from a search of asides "
        "-0.3 to 0.3 and ups -0.3 to 0.3; seen as it is"),
    Row(35, "35 Dog, Party Member", 1.08f, 35.0f, 0.0f, 0.17f, 0.045f,
        "by eye at the camp, 2026-10-04: the mabari's row for a dog an eighth bigger; seen as it is"),
    Row(36, "36 Squirrel", 0.3f, 35.0f, 0.0f, 0.05f, 0.0f,
        "by eye at the camp, 2026-10-04: its head and forepaws; from a search of asides -0.1 to 0.1 and ups -0.1 to "
        "0.1; seen as it is"),
    Row(41, "41 Rat, Small", 0.62f, 35.0f, 0.0f, 0.08f, -0.01f,
        "by eye at the camp, 2026-10-04: its head in profile, its ears whole; from a search of asides -0.12 to 0.12 "
        "and ups -0.12 to 0.12 at 0.5 m, then back for its ears; seen as it is"),
    Row(47, "47 Ogre A", 2.47f, 23.5f, -7.5f, 0.25f, -0.15f,
        "by eye in a fight, 2026-10-02: its face, horns and teeth; seen as it is"),
    Row(53, "53 Witherfang", 1.2f, 35.0f, 0.0f, 0.17f, 0.03f,
        "by eye at the camp, 2026-10-04: its head in three-quarter profile; from a search of asides -0.3 to 0.3 and "
        "ups -0.3 to 0.3; seen as it is"),
    Row(54, "54 Ambient, Goat", 1.1f, 35.0f, 0.0f, 0.08f, -0.15f,
        "by eye at the camp, 2026-10-04: its head and horns; from a search of asides -0.25 to 0.25 and ups -0.25 to "
        "0.25; seen as it is"),
    Row(55, "55 Ambient, Mutt", 0.75f, 35.0f, 0.0f, 0.03f, 0.07f,
        "by eye at the camp, 2026-10-04: its head in three-quarter profile; from a search of asides -0.2 to 0.2 and "
        "ups -0.2 to 0.2; seen as it is"),
    Row(63, "63 Cat", 0.4f, 35.0f, 0.0f, 0.0f, 0.0f,
        "by eye at the camp, 2026-10-04: its head in profile; from a search of asides -0.14 to 0.14 and ups -0.14 "
        "to 0.14; seen as it is (turned, its head stays in the middle: no aside)"),
    Row(64, "64 Dragonling", 0.9f, 35.0f, 0.0f, -0.5f, 0.45f,
        "by eye at the camp, 2026-10-04: its head in profile; the aside interpolated between -0.4 (the head a "
        "little left of the middle) and -0.6 (a little right), the rest seen; then seen as it is in the sweep "
        "that day, its head a little right of the middle"),
    Row(65, "65 Wild Sylvan", 4.0f, 35.0f, 0.0f, -0.5f, 0.0f,
        "by eye at the camp, 2026-10-04: its trunk and limbs among its leaves; no face was found on it in searches "
        "of asides -1 to 1 and ups -1 to 2.1. The aside interpolated between -1 and 0, both seen, and not itself "
        "seen: its creature stays hostile, and each look began a fight"),
    Row(66, "66 Dragon, High", 6.0f, 35.0f, 0.0f, -1.7f, -0.8f,
        "by eye above Haven, 2026-10-04, asleep: its head in three-quarter profile, its horns, its neck and a wing "
        "behind; seen as it is, the head a little right of the middle"),
    Row(72, "72 Pride Demon", 2.4f, 35.0f, 0.0f, 0.6f, -0.25f,
        "by eye at the camp, 2026-10-04: its face among its spines, its horns' roots; from a search of asides -0.9 "
        "to 0.9 and ups -0.9 to 0.9; seen as it is"),
    FromRoot(Row(73, "73 Broodmother Tentacle", 2.8f, 35.0f, 0.0f, -0.3f, 3.4f,
                 "by eye at the camp, 2026-10-04, from its root: it has no head; its upper curl and its tip, 3.4 m "
                 "up; seen as it is")),
    FromRoot(Row(76, "76 Pig", 1.1f, 35.0f, 0.0f, 0.27f, 0.46f,
                 "by eye at the camp, 2026-10-04, from its root: its head, its ears and its snout, three-quarter "
                 "on; seen as it is")),
    FromRoot(Row(77, "77 Deer", 1.1f, 35.0f, 0.0f, 0.285f, 1.18f,
                 "by eye at the camp, 2026-10-04, from its root: its head in three-quarter profile, its ears whole; "
                 "seen as it is")),
    FromRoot(Row(78, "78 Ox", 2.4f, 35.0f, 0.0f, 0.75f, 1.2f,
                 "by eye at the camp, 2026-10-04, from its root: its head and both horns whole; from a search of "
                 "asides 0.5 to 1 and ups 0.95 to 1.45; seen as it is")),
    Row(79, "79 Ram", 1.5f, 35.0f, 0.0f, 0.1f, -0.15f,
        "by eye at the camp, 2026-10-04: its head and horns whole; from a search of asides -0.3 to 0.3 and ups -0.3 "
        "to 0.3; seen as it is"),
    Row(80, "80 Dragon, Drake", 1.5f, 35.0f, 0.0f, -0.85f, 0.8f,
        "by eye at the camp, 2026-10-04: its head in profile; the aside interpolated between -0.7 (the head left "
        "of the middle) and -1.0 (right of it), the rest seen; then seen as it is in the sweep that day, its "
        "head whole, right of the middle and high"),
    FromRoot(Row(81, "81 Spirit Apparatus Head", 8.0f, 35.0f, 0.0f, 0.85f, 2.4f,
                 "by eye at the camp, 2026-10-04, from its root: the carved stone head whole, which is all there "
                 "is of it, 2.4 m up; seen as it is")),
    Row(82, "82 Archdemon  (Wounded)", 6.0f, 35.0f, 0.0f, -1.2f, -1.45f,
        "by eye at the camp, 2026-10-04: its head, which hangs low before it, in three-quarter profile; from a "
        "search of asides -2 to 2 and ups -2 to 2; seen as it is"),
    FromRoot(Row(85, "85 Halla", 2.1f, 35.0f, 0.0f, 0.49f, 1.72f,
                 "by eye at the camp, 2026-10-04, from its root: its head in profile and its antlers whole above "
                 "it; seen as it is")),
    FromRoot(Row(86, "86 Chicken", 0.4f, 35.0f, 0.0f, 0.07f, 0.45f,
                 "by eye at the camp, 2026-10-04, from its root: its head, its comb and its beak; seen as it is")),
    FromRoot(Row(87, "87 Owl", 0.5f, 35.0f, 0.0f, 0.08f, 0.39f,
                 "by eye at the camp, 2026-10-04, from its root: its face; seen as it is")),
    Row(89, "89 Archdemon", 6.0f, 35.0f, 0.0f, -1.2f, -1.45f,
        "by eye at the camp, 2026-10-04: the wounded archdemon's row, the same body; seen as it is"),
    // Awakening's own appearances (its apr_gxa, rows 20002 on), looked at in
    // the Silverite Mine's last room, 2026-10-09 (issue 84: the user, of a
    // Dragon Thrall's card, "dragon thrall portrait looks off", and "we should
    // inspect portraits for awakenings creatures as we did origins"). Until
    // then each had a default, which frames by the creature's extent as it
    // stands: a thrall was pictured as it flew in, measured 4.8 to 17 m from
    // feet to head where its body is 2.4, and its card was its belly from
    // 4.6 m. Most are a body of the main campaign's in other colours, and its
    // row frames them, tried and seen; the Children are new.
    Row(20002, "20002 (GXA) Pride Demon B", 2.4f, 35.0f, 0.0f, 0.6f, -0.25f,
        "by eye in the Silverite Mine, 2026-10-09: its face and eyes, the pride demon's row (72), the same body; "
        "seen as it is"),
    Row(20003, "20003 (GXA) Shade B", 1.5f, 23.5f, 0.0f, 0.05f, -0.3f,
        "by eye in the Blackmarsh, 2026-10-09: its hood and the eye under it, turned toward the camera at a "
        "person's turn and to the right of the middle, as the user asked of its card ('too much profile'); at 35 "
        "degrees from 1.85 m, 0.23 aside, it was the arch of its back; from a search of asides -0.3 to 0.3 and "
        "ups -0.59 to 0.01 at 1.85 m and three looks nearer; seen as it is"),
    Row(20004, "20004 (GXA) Lightning Dragon Spirit", 6.0f, 35.0f, 0.0f, -1.7f, -0.8f,
        "by eye in the Silverite Mine, 2026-10-09: its head in profile, pale as the spirit is, the high dragon's "
        "row (66), the same body; seen as it is"),
    Row(20006, "20006 (GXA) Giant Golem, Fire", 3.5f, 23.5f, 0.0f, 0.55f, -0.1f,
        "by eye in the Silverite Mine, 2026-10-09: its head in the hollow between its shoulders, over the runes "
        "of its collar, at a person's turn: at 35 degrees a shoulder hides it (searches of asides -0.2 to 1.26 "
        "and ups -0.95 to 0.65 there were plates and runes); from a search of asides -0.25 to 0.55 and ups -0.5 "
        "to 0.3; seen as it is"),
    Row(20007, "20007 (GXA) Childer Grub", 1.5f, 35.0f, 0.0f, 0.3f, -1.5f,
        "by eye in the Silverite Mine, 2026-10-09: its face in profile; it lies a metre and a half under what "
        "the engine aims at, nothing of it in the picture until looked for from 3 m; from a search of asides 0 "
        "to 0.3 and ups -1.65 to -1.35; seen as it is"),
    Row(20008, "20008 (GXA) Childer Hatchling", 1.5f, 35.0f, -7.5f, 0.2f, -0.1f,
        "by eye in the Silverite Mine, 2026-10-09: its face in profile (a person's row was a close-up of its "
        "skin); from a search of asides -0.4 to 0.4 and ups -0.4 to 0.4, then between the two best; seen as it "
        "is"),
    Row(20009, "20009 (GXA) Childer Adult", 1.5f, 35.0f, -7.5f, 0.2f, -0.05f,
        "by eye in the Silverite Mine, 2026-10-09: its face and jaws in profile under its legs (a person's row "
        "was a close-up of its skin); from a search of asides -0.4 to 0.4 and ups -0.4 to 0.4, then between the "
        "two best; seen as it is"),
    Row(20014, "20014 (GXA) Armored Ogre", 2.8f, 23.5f, -7.5f, 0.28f, -0.17f,
        "by eye in the Silverite Mine, 2026-10-09: its helm, horns and jaw, the ogre's row (47) by its size, "
        "3.1 m for 2.74; seen as it is"),
    Row(20015, "20015 (GXA) The Architect", 1.1f, 23.5f, -7.5f, 0.0f, 0.15f,
        "by eye in the Silverite Mine, 2026-10-09: his face and crown whole over his chest, at a person's pitch "
        "and rotation; at 1 m and 0.05 up the crown was cut; seen as it is"),
    Row(20016, "20016 (GXA) The Mother", 1.6f, 23.5f, -7.5f, 0.05f, -0.18f,
        "by eye in her own fight in the Nest, 2026-10-10, on the creature that fights: her face in the middle, at a "
        "person's pitch and rotation. The row of 2026-10-09 (0.15 aside, -0.05 up) was found on the sweep's creature "
        "standing in the Silverite Mine, and on her card in the fight her face was at its lower edge with her hair "
        "over it (the user: \"mother portrait camera is too high\"); of ups -0.05 to -0.65 and then -0.14 to -0.26 "
        "about the best; seen as it is"),
    FromRoot(Row(20017, "20017 (GXA) The Mother - Tentacle", 2.8f, 35.0f, 0.0f, -0.3f, 3.4f,
                 "by eye in the Silverite Mine, 2026-10-09, from its root: its length, the broodmother's "
                 "tentacle's row (73); it has no head node; seen as it is")),
    Row(20019, "20019 (GXA) Dragon B", 1.5f, 35.0f, 0.0f, -0.78f, 1.05f,
        "by eye in the Silverite Mine, 2026-10-09: its head and frill in profile, the dragon's row (4), the same "
        "body at 2.41 m; seen as it is"),
    Row(20020, "20020 (GXA) Wild Sylvan, Charred", 4.0f, 35.0f, 0.0f, -0.5f, 0.0f,
        "by eye in the Silverite Mine, 2026-10-09: its trunk and boughs, the wild sylvan's row (65), with no face "
        "found on it either; seen as it is"),
    FromRoot(Row(43000, "43000 (STR) Strider", 3.6f, 30.0f, 0.0f, 1.1f, 4.0f,
                 "by eye in the Dragonbone Wastes, 2026-10-10, from its root: Witch Hunt's Varterral, its hooded "
                 "head and beak in three-quarter profile over its neck and a foreleg; it has no head node, and its "
                 "card had its initials until this (issue 103). Its head stands forward of its root, 1.1 m aside "
                 "of it at this turn: with nothing aside it was out of the picture; at 4.35 up it sat in the "
                 "disc's lower half; seen as it is")),
    DefaultFor(BodyKind::OnAllFours, "a body on all fours", 0.6f, 0.0f, 0.0f, 0.0f, 0.0f,
               "straight on, from 0.6 of its extent: not yet looked at"),
    DefaultFor(BodyKind::Upright, "an upright body that is not a person's", 0.6f, 0.0f, 0.0f, 0.0f, 0.0f,
               "straight on, from 0.6 of its extent: not yet looked at"),
};

} // namespace tb
