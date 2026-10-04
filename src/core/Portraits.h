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
#pragma once

#include "core/BarWindow.h"
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
// - a render, for a person, or a creature the table has a camera for (a
//   great bear, a blight wolf, the followers by their tags);
// - else the ready-made picture of its kin, a row of the same family,
//   which is the label up to its first comma ("Spider, Giant" is a
//   Spider), the plain one first ("Spider" before "Spider, Corrupted");
// - else none: the card shows initials rather than a bad render.
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
    std::wstring made; // for ReadyMade: the texture
};

[[nodiscard]] std::wstring PortraitFamily(std::wstring_view label);
// How far back the portrait renderer's camera stands from a creature's head
// for a creature bigger than a person, which the game's portraits table does
// not frame (it frames the party, and its summons and forms): the table's
// own distance for a person (its Default row, 0.675 m) times how much bigger
// the creature is, by the appearance table's personal space (a person's
// 0.35 to 0.4, an ogre's 0.95), to the power 1.5: a big creature's head is
// bigger still. The renderer aims at the head; at a person's distance from
// an ogre's it stood inside it, and the user saw "portrait for ogre looks
// off": an arm and a chest (2026-10-02; DAOLib's reading of
// PortraitRenderer::PlaceCamera). Seen the same day: at 1.60 m (in
// proportion) the ogre's card showed a horn; at 2.56 m its face, horns and
// teeth. Nothing for a creature a person's size or smaller: the table's
// camera as ever.
inline constexpr float kPortraitPersonSpace = 0.4f;
inline constexpr float kPortraitSizePower = 1.5f;
[[nodiscard]] std::optional<float> PortraitDistance(float personalSpace, float personDistance);

// How much bigger than a person a creature is, by its personal space, to the
// power above: 1 for a person or smaller, an ogre's 3.66.
[[nodiscard]] float SizeOverPerson(float personalSpace);

// The whole of a big creature's portrait camera: its distance (above), and
// its offsets, which move with its size from a person's (the table's Default
// row) as a big creature's face hangs forward and down from its head bone:
// at an ogre's size the camera 0.25 m to the side and 0.15 m down centred
// its face, where at the table's offsets it sat low and to the right (the
// user: "ogre portrait isn't quite centered", 2026-10-02, and tried by sight
// the same evening).
struct PortraitFraming
{
    float distance = 0.0f;
    float xAdjust = 0.0f;
    float zAdjust = 0.0f;
};
inline constexpr float kPortraitXPerSize = 0.09f;
inline constexpr float kPortraitZPerSize = -0.07f;
[[nodiscard]] std::optional<PortraitFraming> FrameBySize(float personalSpace, float personDistance, float personX,
                                                         float personZ);

// The camera's zoom on a creature's turn, the camera following (the user:
// "shouldn't camera pull back depend on creature size? for ogre or dragon pull
// back more, for human sized enemies same as player"): on the scene camera's
// scale, 1 close in, 0 the tactical view high above, back from the player's
// own by kZoomPerSize for each person's size more than one, never into the
// tactical view, whose camera no longer follows: the game flips to it under
// 0.75 on this scale (0.5 on the controller's -1..1: an ogre's 0.73 flipped
// it, and the camera stayed high above for the rest of the fight, a run with
// no hands, 2026-10-02). Nothing for a
// creature a person's size or smaller, nor with the player's own zoom in the
// tactical view already: the player's zoom as it is. An ogre's turn at the
// player's 1 (close in) is 0.80.
inline constexpr float kZoomPerSize = 0.075f;
inline constexpr float kZoomLeast = 0.78f;
[[nodiscard]] std::optional<float> FollowZoom(float playerZoom, float personalSpace);

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
