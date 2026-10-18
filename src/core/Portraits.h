// Which combatants the turn bar has a portrait for, and whose to get next
// (dev/UI.md, "Portraits"). The engine renders any creature's portrait as it
// renders the party's (DAOLib's CCCreature::RenderPortrait), into a texture
// a movie loads by its img:// name. Rendering clones the creature's model,
// so it is not free: one a frame, the cards nearest the front first, each
// combatant once a fight. What a try gave is kept, an empty answer too, so a
// creature the engine makes no portrait for is not asked every frame.
//
// Forgotten when a fight ends: object ids are reused once an area is left,
// and a portrait's name belongs to the creature it was rendered for. Kept
// through a conversation in the middle of one, when nothing is rendered.
//
// And released: the game's own screens release every portrait they render
// when they close (ReleasePortrait beside each UpdatePortrait's caller), and
// never leave a renderer, with its clone of the creature, to die with the
// area. The first area change after a fight in play crashed in the engine's
// model code (2026-09-30), which is why. The movie lets the images go
// first, then the ones this book rendered are released; the party's, which
// the game keeps, never are.
//
// And how a portrait is framed (issue 32, 2026-10-04): which picture a
// creature gets (ChoosePortrait), the kind of body it is (BodyKindOf), its
// framing looked up in the table of them (core/PortraitTable.h, which has
// every number: FramePortrait), what a tester gives in its place and is told
// of it (the turnbased_portrait tool: ReadPortraitAsk, PortraitReport), and
// the same said in the log. The engine's calls are src/game/Bar.cpp's.
#pragma once

#include "core/BarWindow.h"
#include "core/PortraitTable.h"
#include "core/TurnMachine.h"
#include "core/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb
{

// The name the game's image loader finds a texture by: DAOLib's
// "img://portrait_player0" becomes "portrait_player0.dds". The game's own
// movies add the extension to a name without one before loading it
// (portraits.gfx, MovieUtils.GetImageUrl); without it the loader finds
// nothing and draws grey (seen in play, 2026-09-30). Empty stays empty.
[[nodiscard]] std::wstring TextureName(std::wstring_view url);

// Where a creature's picture comes from. The engine renders a portrait
// with a camera it takes from the game's portraits table (portraits.gda) by
// the creature's tag or appearance, else the default, which is a person's
// face: a giant spider, with no row, came out as a close view of its
// abdomen (the user's picture, 2026-10-01). For some appearances the table
// names a ready-made picture instead (its Override column: a wolf, two
// bears, two spiders, the swarm, a golem, three forms of the Fade), and the
// engine renders nothing for those. So:
// - its appearance's ready-made picture, if the table has one;
// - a render by the table's own camera, for a creature the table has a row
//   for, by its appearance or its tag (a great bear, a blight wolf, the
//   followers): the game's own framing of it, left as it is;
// - a render, for a person (as the game layer says one is);
// - else the ready-made picture of its kin, a row of the same family,
//   which is the label up to its first comma ("Spider, Giant" is a
//   Spider), the plain one first ("Spider" before "Spider, Corrupted");
// - else a render all the same, framed by its appearance's own row of the
//   framing table, or by its kind's default there (FramePortrait, below).
// The last was none until 2026-10-04, "the card shows initials rather than a
// bad render": twelve dragonlings of twelve had initials, and the user, of
// the frames: "Looks like the portrait for dragon is not working" (issue
// 32). A card has a picture now. None is what cannot be rendered, which the
// game layer finds and says: no creature by the id, or a model with no head
// node for the camera to aim at.
struct PortraitRow
{
    std::int32_t appearance = 0; // 0: a row found by its tag
    std::wstring label;
    std::wstring tag;
    std::wstring made; // Override: a ready-made picture's texture, or empty
};

enum class PortraitSource : std::uint8_t
{
    None,
    Render,
    ReadyMade,
};

struct PortraitChoice
{
    PortraitSource source = PortraitSource::None;
    std::wstring made;        // for ReadyMade: the texture
    bool tableCamera = false; // for Render: the table has a camera for it, which is left as it is
};

[[nodiscard]] std::wstring PortraitFamily(std::wstring_view label);

// The engine's portrait camera, as its own code places it. Read 2026-10-04 in
// the game's exe with DAOLib's tools/daoexe.py (PortraitRenderer::PlaceCamera,
// the address library's ID 21341, 0x007F3130 in GOG's build; DAOLib's
// dao/Portraits.h says the outline of it in words):
// - it stands back from the model's head node by three times the table's
//   CameraDistance (the distance times 30.0 over 10.0, the 10 being the field
//   of view it sets a few lines on, in degrees), turned about the head by the
//   pitch;
// - the Z and X adjusts then move the camera along its own up and right, in
//   metres, and its aim with it: an adjust is so many metres of the picture
//   whatever the distance, not an angle;
// - the field of view is 10 degrees, so the picture is 2 x 3 x tan(5 degrees)
//   = 0.525 of the distance across where the head is: 0.35 m for the Default
//   row's 0.675 m, which is a face;
// - it draws from 0.15 m to 40 m (the two numbers it gives the camera's clip
//   planes): a head further than 40 m from it is not drawn at all.
// The last is why the high dragon's card was a blank disc (issue 32; the
// user, twice: "Dragon's portrait is clearly off"): by its personal space,
// 3.00, it was framed from 13.86 m, which is 41.6 m from its head. Seen in
// play the same day: from 12 m (36 m) it is drawn.
//
// And what play showed that the reading did not (2026-10-04, the
// turnbased_portrait tool on real creatures at the camp and on the mountain
// top; the pictures are the game's own frames):
// - the node the engine aims at is not always where the head is seen.
//   Straight on, a dragonling's and a drake's heads are above it by about
//   0.37 of their extent (0.5 m for 1.4, 0.9 m for 2.38), and the high
//   dragon's is at it;
// - the rotation turns the view round the creature (the high dragon at 25
//   degrees is seen three-quarter on, a wing behind its head), and the
//   subject slides sideways in the picture as it does: what the engine turns
//   about is not where the head is seen. An aside the other way brings it
//   back, which is what the table's Default row does for a person, and why a
//   person's rotation and offsets given to another body lose it: the high
//   dragon by a person's (23.5 degrees, its offsets grown with its size) was
//   a small figure at the picture's left edge, a dragonling at 25 degrees
//   with nothing aside off the edge altogether;
// - how far it slides is each model's own, and is not to be had from what
//   the engine measures of the creature (core/PortraitTable.h has the
//   numbers, with the rows they made).
// So how a creature is framed is a table's to say, a row a creature type,
// each found by eye: core/PortraitTable.h, which has every number of it. The
// code here looks a creature up in it (FramePortrait).
inline constexpr float kPortraitBackOff = 3.0f;
inline constexpr float kPortraitAcross = 0.525f;
inline constexpr float kPortraitFarPlane = 40.0f;
// The table's furthest is inside what the engine draws.
static_assert(kPortraitBackOff * kFramedFromMost < kPortraitFarPlane);

// A portrait's camera: the portraits table's camera columns (degrees and
// metres), which DAOLib's dao::portraits::SetCamera gives the renderer.
struct PortraitCamera
{
    float pitch = 0.0f;
    float rotation = 0.0f;
    float distance = 0.0f;
    float zAdjust = 0.0f;
    float xAdjust = 0.0f;
};

// The game's Default row as it ships (core/PortraitTable.h has it): what the
// game layer's reading of the running game's table stands in for.
inline constexpr PortraitCamera kShippedPerson{kGamesDefaultRow.pitch, kGamesDefaultRow.rotation,
                                               kGamesDefaultRow.distance, kGamesDefaultRow.up, kGamesDefaultRow.aside};

// A creature's size as the engine has it: from its feet (its position) to
// its head, the node of its model the engine's own portrait camera aims at
// (DAOLib's dao::portraits::HeadPosition). Whether that point is the world's
// or about the creature's own origin was not known as the turn camera's
// distance was first taken from it (issue 33): the shorter of the two
// readings is taken, the other being the creature's distance from the map's
// origin, and which it was is said. It is the world's (seen in play,
// 2026-10-04: the high dragon's head at (186.2, 165.4, -31.7), itself at
// (189.6, 163.3, -51.8)); both readings are kept, the other costing nothing.
// The portraits take this measure, and the turn camera where a creature's
// model gives no box (CloseDistanceOf, below). It is the measure of a pose:
// the same dragon read 9.21 m asleep on its ledge and 20.5 m in the air.
struct HeadExtent
{
    float meters = 0.0f;
    bool world = true; // the head read as a point in the world
};
[[nodiscard]] HeadExtent ExtentOf(const Vec3 &head, const Vec3 &feet);

// A person's extent, and how much more than it is bigger than a person: the
// turn camera's measure of the same where it goes by the feet to the head
// (CloseDistanceFor, below).
inline constexpr float kPersonExtent = 1.7f;
inline constexpr float kBiggerThanPerson = 1.25f;

// The kind of body a creature is (BodyKind, core/PortraitTable.h), which says
// whose default it takes where it has no row of its own (the user,
// 2026-10-04: "If different cases need to be handled differently, we can do
// that, w/ overloaded functions or something"). This is the one place that
// says which, by what the game's data and the engine say of the creature:
// - Person: a person's body, which the portraits table's Default row was made
//   for and frames well (people, darkspawn, the walking dead, werewolves: seen
//   in play since 2026-09-30). It is all three of: a model put together as a
//   person's is (the appearance table's ModelType H, or P for the races a
//   player is of: `personsModel`), standing up (its TerrainFollowPitch 0), and
//   no bigger than a person (its extent within kBiggerThanPerson of
//   kPersonExtent: the ogre and the high dragon are H models that stand up);
// - OnAllFours: its body pitches with the ground it stands on, which the
//   appearance table says of every four-legged thing and crawler it has and
//   of nothing that stands up (TerrainFollowPitch over 0: wolves, dogs,
//   bears, the bronto, deer, the dragonling and the drake, spiders and rats,
//   and the cat and the pig among its H models);
// - Upright: every other. It stands up and is not a person's body: bigger
//   than one, or a creature's model (a demon, a shade).
// A kind is not a creature's name. What one creature wants that its kind's
// default does not give is a row of the table.
[[nodiscard]] BodyKind BodyKindOf(bool personsModel, float terrainFollowPitch, float extent);
// Why it is that kind, in words.
[[nodiscard]] std::string_view BodyKindWhy(bool personsModel, float terrainFollowPitch, float extent);
// A kind's name as a tester gives it and reads it ("person", "upright",
// "on_all_fours"), and the kind of that name.
[[nodiscard]] std::string_view BodyKindName(BodyKind kind);
[[nodiscard]] std::optional<BodyKind> BodyKindNamed(std::string_view name);

// The table's row for an appearance, and a kind's default row; null for an
// appearance with no row, and for a person's body, whose default is the
// game's own.
[[nodiscard]] const FramingRow *RowOf(std::int32_t appearance);
[[nodiscard]] const FramingRow *DefaultRowOf(BodyKind kind);

// What framed a portrait.
enum class FramedBy : std::uint8_t
{
    Nothing,
    Row,            // its appearance's own row of the table
    PortraitsTable, // the game's own row for it (by its tag or its appearance), untouched
    Default,        // its kind's default row of the table
    Person,         // a person's body: the game's Default row, untouched
};
[[nodiscard]] std::string_view FramedByName(FramedBy by);

// A framing: the camera, what chose it, the table's row where it was one
// (its label and its note), a light where the row has its own, and whether a
// default's camera is held at the furthest the table lets it stand.
struct PortraitFrame
{
    PortraitCamera camera;
    FramedBy by = FramedBy::Nothing;
    std::string_view label;
    std::string_view note;
    bool lit = false;
    float lightX = 0.0f;
    float lightY = 0.0f;
    float lightZ = 0.0f;
    bool atMost = false;
};

// A row's camera as it is, and a default's for a creature of that extent:
// its distance, aside and up times the extent, held between the table's
// least and most (a measure that is no size is framed from the least).
[[nodiscard]] PortraitFrame FrameByRow(const FramingRow &row);
[[nodiscard]] PortraitFrame FrameByDefault(const FramingRow &row, float extent);
// A person's body: the game's Default row as it is.
[[nodiscard]] PortraitFrame FramePerson(const PortraitCamera &person);

// A creature's framing, looked up: its appearance's own row; else the game's
// own row for it, where the portraits table has one (`gamesOwn`, null for
// none); else its kind's default row, which for a person's body is the
// game's Default row (`person`). A kind a tester asked for is framed by that
// kind's default, the rows passed over: that is how a row is set beside its
// default. The one place a framing is chosen.
[[nodiscard]] PortraitFrame FramePortrait(std::int32_t appearance, BodyKind kind, bool kindAsked,
                                          const PortraitCamera *gamesOwn, float extent, const PortraitCamera &person);
// Which row that was, in the log's words: "row 66 Dragon, High (...)", "the
// default for a body on all fours (...)".
[[nodiscard]] std::string FramingRule(const PortraitFrame &frame);

// What a tester gives in place of the rule's (the turnbased_portrait tool,
// src/plugin/Plugin.cpp): a kind to frame the creature as, and any of the
// camera's values. One given replaces the rule's; one not given is the
// rule's. The light's three are the engine's rig's offset, which no rule
// here sets: given, they go to the engine as they are.
struct PortraitTuning
{
    std::optional<BodyKind> kind;
    std::optional<float> pitch;
    std::optional<float> rotation;
    std::optional<float> distance;
    std::optional<float> zAdjust;
    std::optional<float> xAdjust;
    std::optional<float> lightX;
    std::optional<float> lightY;
    std::optional<float> lightZ;

    // Whether anything of the camera is given (a kind is not).
    [[nodiscard]] bool AnyValue() const
    {
        return pitch || rotation || distance || zAdjust || xAdjust || lightX || lightY || lightZ;
    }
};
[[nodiscard]] PortraitCamera Tuned(const PortraitCamera &rule, const PortraitTuning &tuning);

// Whether a creature whose model has no head node is rendered all the same
// (the user, 2026-10-04, of the deepstalker's initials: "We should try to
// capture the lamprey like head"). The engine's camera then aims at the
// origin of the portrait's scene (core/PortraitTable.h, FromRoot), so only a
// camera that carries the whole framing from there can frame it: its
// appearance's own row, where that is marked as from the root (`row`: what
// RowOf found, or nothing), or values a tester gives to find such a row.
// Not a row that is about a head, whose creature's head is only late; not a
// kind's default, which is by the feet-to-head there is none of; and not a
// kind a tester asked for with no value, which is that default.
[[nodiscard]] bool RendersWithoutHead(const FramingRow *row, const PortraitTuning &tuning);

// Why a creature has no picture: what cannot be rendered, each said by name
// to a tester's script ("no_creature", "no_head_node", "no_render"). No head
// node is also what a creature not set up yet answers (one just made, or
// whose appearance was just changed, takes a few frames): asked again in a
// moment, it may have one.
enum class PortraitLack : std::uint8_t
{
    Nothing,
    Creature, // none by the id
    HeadNode, // its model has none for the camera to aim at
    Render,   // the engine made no portrait of it
};
[[nodiscard]] std::string_view PortraitLackName(PortraitLack lack);

// Everything about one portrait as it was got, for the log and for a tester
// who cannot see what the rule saw (the turnbased_portrait tool's answer).
// The game's own text is wide, as the engine gives it; what this mod says of
// it is plain ASCII.
struct PortraitReport
{
    ObjectId id = kNoObject;
    std::wstring name;
    std::wstring tag;
    std::int32_t appearance = 0;
    std::wstring label;
    float personalSpace = 0.0f;
    bool personsModel = true; // the appearance table's ModelType H or P
    float terrainFollowPitch = 0.0f;
    BodyKind kind = BodyKind::Person;
    std::string kindBy; // what said its kind
    std::string source; // "rendered", "the game's", "the portraits table's", "none"
    FramedBy framedBy = FramedBy::Nothing;
    std::string row;    // the table's row that framed it, by its label; empty for none of the table's
    std::string rule;   // what framed it, in words
    std::wstring image; // its img:// name, empty for none
    bool rendered = false;
    bool fresh = false;       // by a renderer made for it now, not one it had
    bool cameraGiven = false; // the camera below was given to the engine; else it is what the table has for it
    PortraitCamera camera;
    std::optional<float> lightX;
    std::optional<float> lightY;
    std::optional<float> lightZ;
    bool atMost = false; // held at the furthest a head is still drawn from
    Vec3 feet;
    std::optional<Vec3> head;
    float extent = 0.0f;
    std::string extentBy;
    bool shown = false;                        // put up large in the bar's movie
    PortraitLack lack = PortraitLack::Nothing; // why nothing could be rendered, if nothing could
    std::string problem;                       // that, or what else to know, in words; empty for nothing
};

// How wide the picture is where the head is, in metres.
[[nodiscard]] float PictureAcross(const PortraitCamera &camera);
// Whether the head is past the far plane from where the camera stands: not
// drawn.
[[nodiscard]] bool PastFarPlane(const PortraitCamera &camera);

// The log's line for a portrait, once a creature a fight: whose, its image
// and source, and for a render which rule framed it, the camera, the extent
// and where the head node is.
[[nodiscard]] std::string PortraitLine(const PortraitReport &report);
// The tool's answer: all of it, as JSON.
[[nodiscard]] std::string ToJson(const PortraitReport &report);

// What the turnbased_portrait tool is asked, in its arguments (a flat JSON
// object): whose portrait ("id", as Eluvian writes ids, "0x7d9", or a whole
// number), a tuning ("kind", and any of "pitch", "rotation", "distance",
// "zAdjust", "xAdjust", "lightX", "lightY", "lightZ", numbers), and whether
// to put the picture up large in the bar's movie ("show", true or false) and
// for how long ("seconds"). A picture shown is taken down by the next call
// that does not ask for one, and by itself after its seconds: its renderer
// must not be left to die with its creature's area (this file's head).
inline constexpr std::uint32_t kShowSeconds = 20;
inline constexpr std::uint32_t kShowSecondsMost = 600;
struct PortraitAsk
{
    ObjectId id = kNoObject;
    PortraitTuning tuning;
    bool show = false;
    std::uint32_t seconds = kShowSeconds;
};
// The ask, or what is wrong with the arguments: a sentence for whoever
// called, saying what to give.
struct PortraitAskRead
{
    std::optional<PortraitAsk> ask;
    std::string error;
};
[[nodiscard]] PortraitAskRead ReadPortraitAsk(std::string_view json);

// A portrait put up large with words under it, as the bar's movie reads it
// (TB_GetShow; ui/turnorder: drawShow): fields apart by U+001F, as the bar's
// own are (core/Bar.h). The version; a serial, which goes up with every
// change, and which the movie says back once it has drawn that one and let
// the one before go (TB_ShowCleared: only then is a renderer released);
// the texture's name, empty for no picture; a line saying what it is (the
// appearance's number and label, and its kind) and a line saying how it was
// framed. Empty text: nothing is shown and nothing was.
inline constexpr int kShowVersion = 1;
inline constexpr wchar_t kShowField = 0x1F;
[[nodiscard]] std::wstring EncodeShow(std::uint32_t serial, std::wstring_view url, std::wstring_view title,
                                      std::wstring_view detail);
// The two lines for a portrait as it was got.
[[nodiscard]] std::wstring ShowTitle(const PortraitReport &report);
[[nodiscard]] std::wstring ShowDetail(const PortraitReport &report);

// How far from its pivot the camera stands on a creature's turn, the camera
// following (the user, 2026-10-02: "shouldn't camera pull back depend on
// creature size? for ogre or dragon pull back more, for human sized enemies
// same as player"; and 2026-10-05, of the high dragon, whose turns showed the
// inside of the dragon: "Camera needs to be pulled back more for the dragon
// ... Should do so automatically depending on enemy's size"). The game's own
// distance at its closest zoom is kCloseGame; for a bigger creature it is
// that in proportion to the creature's size against a person's, never under
// the game's own and never past kCloseMost: the game's camera tests no wall
// and no ground at a distance it was not made for. Nothing is the game's own.
//
// Until 2026-10-05 this was the zoom drawn back by the creature's personal
// space, and no further than 0.78: under 0.75 the game flips to the tactical
// view, whose camera no longer follows (an ogre's 0.73 flipped it, 2026-10-02).
// That was enough for an ogre and not for a dragon. The distance is apart
// from the zoom (DAOLib's dao::camera::SetCloseDistance, its issue 17), so the
// zoom, and the view, are the player's own throughout.
//
// The size is by the model's own box (DAOLib's dao::world::ModelBoxOf, its
// issue 21: the box the renderer keeps for the model in its rest pose, in
// the model's own axes, as drawn), each of its two measures against a
// person's own: its length, the larger of its two sides across, against a
// person's across, and its height against a person's height. The larger of
// the two counts. One plain rule for every body, with no case for any
// creature (the user's principle: a plain rule first, an exception only for
// what it buys): a long, low body stands the camera back by its length, a
// tall one by its height.
//
// What came before, each seen in play:
// - the creature's feet to its head node alone, by which a giant spider,
//   2.3 m long and 1.2 m high, is no bigger than a person: the camera stood
//   as close as to a person and the spider's abdomen filled the frame,
//   hiding whom it struck (issue 33, Ortan Thaig, 2026-10-04);
// - the largest of the box's three sides against a person's height (commit
//   3f0f0f0), played 2026-10-05 in the Dead Trenches with the creatures
//   staged: a bronto at 6.3 m was good, all of it from behind and the party
//   member beside it in view, and a giant spider's 3.8 m did not clear it:
//   in one frame of its turn its abdomen was across half the picture, close
//   and low. A length against a person's height undersells a long body,
//   since a person is narrower than it is tall. Too far is the mild failure
//   (a creature small in the picture with the fight round it in view), too
//   close the bad one.
//
// kPersonAcross and kPersonHeight are a person's box as DAOLib saw it in
// play (dao/World.h, Ortan Thaig, 2026-10-04): 1.05 to 1.24 m across with
// its arms out, 1.60 to 1.83 m high, and a body's length is its "across".
// By them a person of those comes out at the game's own, or 10 cm past it
// at the widest. Bigger persons there are: of the four of the party at the
// high dragon's fight (DAOLib's example's log, 2026-10-05), Lyna's box was
// 1.05 m across and 1.68 high and Wynne's 1.14 and 1.79, both the game's
// own; Alistair's, a man in armour, 1.30 and 1.92, which as an enemy's is
// 3.25 m; and the fourth's 1.45 and 2.09, 3.6 m. A bigger person standing a
// little further back is the rule doing its work, and the constants stay.
// The boxes seen and what the rule makes of each are
// tests/test_portraits.cpp's.
//
// Seen in play 2026-10-05 (commit d1ff8f4): a lone giant spider's turns at
// 5.8 m showed all of it with its web and the two it faced in view, about a
// third of the frame wide; an ogre's turn at 10 m in a hall, all of it, its
// ram and the party member before it.
inline constexpr float kCloseGame = 3.0f;
inline constexpr float kCloseMost = 14.0f;
inline constexpr float kPersonAcross = 1.2f;
inline constexpr float kPersonHeight = 1.8f;

// Which of a creature's measures the camera's distance was taken by.
enum class CloseBy : std::uint8_t
{
    Nothing, // it has neither: the game's own
    Length,  // its model's box: its length, against a person's across
    Height,  // its model's box: its height, against a person's height
    Head,    // from its feet to its head node, its model giving no box
};
struct CloseDistance
{
    std::optional<float> meters; // nothing: the game's own
    CloseBy by = CloseBy::Nothing;
};
// `box` is the box's three sides in meters, x and y across and z up
// (DAOLib's ModelBox::Size), and `feetToHead` the creature's extent
// (ExtentOf, above), each nothing where the engine answered nothing. Which
// of the box's two measures was the larger is said for a box no bigger than
// a person too. A box that is no measure (a side that is not a number, or
// neither its length nor its height over zero: a model with no mesh) is as
// no box. Without a box it is by the feet to the head as it was before the
// box (CloseDistanceFor), and with neither it is the game's own.
[[nodiscard]] CloseDistance CloseDistanceOf(const std::optional<Vec3> &box, const std::optional<float> &feetToHead);
// By the feet to the head alone: the game's own distance in proportion to
// `extent` against a person's (kPersonExtent), for a creature bigger than a
// person (kBiggerThanPerson); nothing for one that is not, or an extent that
// is none.
[[nodiscard]] std::optional<float> CloseDistanceFor(float extent);

// From where the camera looks at an enemy on its turn, and from how far.
// Straight behind it, its own body hides what it does and whom it strikes
// (the high dragon's buffet seen from its tail; the user, 2026-10-05: "maybe
// don't be directly behind enemies. Perhaps offset it by 30 or 45 degrees to
// the right (or left if something is blocking the right side)"). The party's
// own turns keep the camera behind: the player looks where their character
// does.
//
// The camera may stand in six places about an enemy, liked in this order:
// behind it and round to its right by kViewOffset, behind it and round to its
// left, straight behind it; then in front of it to its right by the same
// offset mirrored, in front of it to its left, and straight in front. The
// three in front are asked only where none of the three behind is a place to
// stand at all: a view from behind that is shortened is still a view from
// behind. No creature has a case of its own: it is the order of the places,
// whatever stands there. The user, 2026-10-05, of the Broodmother, who is set
// into her cave's rock: "I think the broodmother is a special case. What if
// we just turn the camera front instead of back with an offset? Nothing
// blocking her from the front I think"; and "When is the creatures turn the
// camera should try to keep the creature in frame."
//
// A place to stand is one with ground a creature could walk on beneath
// where the camera would be (IsPlace, below). Among the three of a row the
// rule is one: the camera stands back by the creature's size
// (CloseDistanceOf), and no further than its line to the creature is clear.
// It takes the first place whose line is clear the whole distance the size
// asks; if none is, the one whose line is clear the furthest (the earlier of
// two alike), at that distance less kViewMargin, and never nearer than the
// game's own distance. How far each line is clear, and what lies under each
// place, are the engine's to say (what a line from the creature's head to
// where the camera would stand first meets, and what a ray straight down
// from there does), asked by the game layer: src/game/Camera.cpp has what is
// asked, and of which of the world's collision groups.
//
// Where no place is found at all, behind or in front, the camera does not
// go to the creature: it stays with the party, on the one the player acts
// as and at the game's own distance, as on a party member's turn, and turns
// so that it looks from there toward the creature (YawToward). The user,
// 2026-10-05: "Yeah don't move the camera stay with the party", and then the
// second of the sentences above.
//
// What came before, each seen in play 2026-10-05:
// - a side was either clear at the whole distance or not, and with neither
//   side clear the camera went behind, untested. On one of an ogre's turns
//   (commit d1ff8f4), 10 m asked, both sides were blocked, and 10 m straight
//   behind it was the far side of a wall: the frame was of the wall, the
//   ogre's name floating in the middle of it and the ogre not in the picture.
//   The engine's orbit tests no wall and no ground (DAOLib's
//   dao/Camera.h);
// - the three places behind, each as far as its line was clear (commits
//   aa280db and 96d6445). In a built hall that works. The Broodmother's cave
//   has no collision in any group: all three lines were clear to the whole
//   14 m, and the frame was from inside the rock. No line sees that, which
//   is what the ground under a place is asked for.
inline constexpr float kViewOffset = 0.70f; // radians: 40 degrees
enum class ViewSide : std::uint8_t
{
    Behind,
    Right,
    Left,
    Front,
    FrontRight,
    FrontLeft,
};
// The camera's yaw for that place, from the yaw straight behind the creature
// (the direction from the creature to the camera, from +x toward +y: more of
// it is round to the creature's right, and half a turn of it is in front).
[[nodiscard]] float ViewYaw(float behind, ViewSide side);
// The place in the log's words: "behind it to its right", "in front of it".
[[nodiscard]] std::string_view ViewSideWords(ViewSide side);

// The camera's yaw that looks from `from` toward `at`: the camera stands on
// the far side of `from`, so it is the direction from `at` to `from`, in the
// yaw's own measure (ViewYaw). Nothing where the two are one place over the
// ground, or a point is not a number: the camera is then left as it is
// turned.
[[nodiscard]] std::optional<float> YawToward(const Vec3 &from, const Vec3 &at);

// How far from its pivot the game's orbit puts the camera: the zoom's blend
// of the close distance and a far one, 20 m (DAOLib's issue 17, read in the
// exe: zoom x close + (1 - zoom) x far, each frame). The zoom is 1 close in
// and 0 in the tactical view; the close view is above about 0.75.
inline constexpr float kFarGame = 20.0f;
[[nodiscard]] float OrbitDistance(float zoom, float close);

// How far short of where its line is cut the camera stands. A line is not a
// picture: the view is as wide as the screen, and a camera put where its one
// line meets a wall has the wall across the side of the frame and its own
// near plane in the stone. Half a meter keeps it off; not seen in play yet.
inline constexpr float kViewMargin = 0.5f;

// How far from the creature the camera would stand at a place whose line is
// clear to `clear` meters: the whole distance the size asks at the player's
// zoom (OrbitDistance of the zoom and the close distance asked, or of the
// game's own where none is), or kViewMargin short of where the line is cut,
// and never nearer than the game's own distance. A distance that is none
// counts as nothing clear. The game layer asks what lies under that point.
[[nodiscard]] float StandDistance(float zoom, const std::optional<float> &asked, float clear);

// What lies under where the camera would stand at a place, as the engine's
// ray straight down from there says of the first thing it meets among the
// ground's collision groups.
struct GroundUnder
{
    bool ground = false;   // the ray met ground at all
    bool walkable = false; // and it is ground a creature could walk on
    float below = 0.0f;    // how far under the creature's feet it lies, in meters; under zero, above them
};
// How far under the creature's own feet the ground beneath a place may lie.
// The camera's height over the creature is the mod's own doing, by its size
// and the player's tilt, so the ground is measured against the creature's
// feet and not against the camera: ground further under them than a storey
// is not the floor the fight is on, but a pit's bottom, a level below, or the
// floor of somewhere else under scenery that has no collision. Not seen in
// play yet.
inline constexpr float kPlaceDrop = 5.0f;
// A place to stand: walkable ground beneath it, no further under the
// creature's feet than kPlaceDrop. A side without one is no place at all,
// whatever its line says. Ground above the camera is not met by a ray down,
// so a point under a hillside or inside solid ground reads as no ground, or
// as what lies far below it.
[[nodiscard]] bool IsPlace(const GroundUnder &under);

// One place: how far its line is clear, in meters of the camera's distance
// at the player's zoom and up to the whole distance, and whether where the
// camera would then stand is a place to stand (IsPlace of what is under
// StandDistance of it).
struct ViewPlace
{
    float clear = 0.0f;
    bool ground = false;
};
// The three places behind a creature, or the three in front of it.
struct ViewRow
{
    ViewPlace right;
    ViewPlace left;
    ViewPlace straight;
};
enum class ViewAnswer : std::uint8_t
{
    Stand,     // at `side`, `meters` from the creature
    AskFront,  // no place behind it: the three in front are to be measured, and the choice asked again with them
    WithParty, // no place round it at all: the camera stays with the party and looks at the creature
};
// Where the camera stands: the place, how far from the creature at the
// player's zoom, and the close distance that puts it there (nothing: the
// game's own), which for the whole distance is the one asked. For an answer
// that is no place, nothing of the three.
struct ViewStand
{
    ViewAnswer answer = ViewAnswer::WithParty;
    ViewSide side = ViewSide::Behind;
    float meters = 0.0f;
    std::optional<float> close;
};
// `front` is nothing until it has been measured, which it is only where the
// answer for the three behind alone is AskFront: the lines and the rays down
// of an ordinary turn are the three behind's.
[[nodiscard]] ViewStand ChooseStand(float zoom, const std::optional<float> &asked, const ViewRow &back,
                                    const std::optional<ViewRow> &front);

// How far a line is clear, in the camera's distance: the line runs from
// `from`, the creature's head, to `to`, where the camera would stand `wanted`
// from its pivot at the creature's feet, and is cut at `cut`. The camera's
// distance has the same share of `wanted` as the cut has of the line, since
// a wall cuts both at the same distance over the ground. Nothing clear for a
// line with no length or a point that is not a number.
[[nodiscard]] float ClearDistance(const Vec3 &from, const Vec3 &to, const Vec3 &cut, float wanted);

[[nodiscard]] PortraitChoice ChoosePortrait(std::int32_t appearance, std::wstring_view appearanceLabel, bool person,
                                            std::wstring_view tag, const std::vector<PortraitRow> &rows);

class PortraitBook
{
  public:
    // How long a movie that has not said it let the images go is waited
    // for before the portraits are released anyway.
    static constexpr std::uint32_t kReleaseWaitMs = 2000;

    // Once no fight is on, forgets them all, and what it rendered waits to
    // be released. Each frame, before Next.
    void Follow(const TurnView &view, std::uint32_t nowMs);

    // The combatant to get a portrait for this frame, if a card of the bar's
    // row lacks one: the first such card, so the row's first and the next
    // come first. Only the row's (core/BarWindow.h): a fight of thirty has
    // ten rendered until the row is moved to the others, and each then once.
    // None while the fight stands aside (a conversation, a menu).
    [[nodiscard]] std::optional<ObjectId> Next(const TurnView &view, const BarSpan &row) const;

    // What getting one gave: its URL, or empty for none (not asked again),
    // and whether it was rendered for the bar (to be released after) or
    // was the game's own.
    void Got(ObjectId id, std::wstring url, bool rendered);

    // The combatant's portrait, empty if it has none (or none yet).
    [[nodiscard]] std::wstring_view Of(ObjectId id) const;

    // Whether this fight has tried the combatant's portrait, and whether
    // what it has was rendered for the bar (so that it is the bar's to
    // render again, and to release). For the turnbased_portrait tool, which
    // frames a card's portrait anew under the bar's hands.
    [[nodiscard]] bool Has(ObjectId id) const
    {
        return urls_.contains(id);
    }
    [[nodiscard]] bool RenderedFor(ObjectId id) const
    {
        return rendered_.contains(id);
    }
    // Whether it waits to be released after a fight: its renderer is still
    // there, and the movie may still show it.
    [[nodiscard]] bool Leaving(ObjectId id) const;

    // The movie has let the images go: what waits may be released now.
    void MovieLetGo();

    // Whether the bar should stay up after a fight for the movie to let
    // its images go: until it has, or the wait is over.
    [[nodiscard]] bool AwaitingMovie() const
    {
        return !releasing_.empty() && !movieLetGo_;
    }

    // The creatures whose rendered portraits may be released now, each
    // once: after the movie let go, or kReleaseWaitMs. Not one a new fight
    // is showing again.
    [[nodiscard]] std::vector<ObjectId> TakeReleases(std::uint32_t nowMs);

    // How many were tried, and how many gave a portrait.
    [[nodiscard]] std::size_t Tried() const
    {
        return urls_.size();
    }
    [[nodiscard]] std::size_t Found() const;

  private:
    std::unordered_map<ObjectId, std::wstring> urls_;
    std::unordered_set<ObjectId> rendered_;
    std::vector<ObjectId> releasing_;
    std::uint32_t releasingSince_ = 0;
    bool movieLetGo_ = false;
};

} // namespace tb
