// The movement preview (dev/UI.md, "The movement preview"): the path from
// whoever's turn it is to the cursor, and how far along it the movement left
// reaches, gold to there and red beyond.
//
// The game layer finds the path and where its points are on the screen
// (src/game/Preview.cpp, through DAOLib); what is decided without the game
// is here: where along the path the movement runs out, and the string the
// turn bar's movie draws from. The movie asks with
// ExternalInterface.call("TB_GetPreview") and takes the answer apart as it
// does the bar's (core/Bar.h): records apart by U+001E, fields by U+001F.
//
//   the head:   version, the view's width and height in pixels, the state
//               (0 nothing to draw, 1 all of it in reach, 2 too far), the
//               path's length and the movement left, in meters with one
//               decimal, and whether the movie draws the line (1) or the
//               path is marked in the world and only the length is the
//               movie's to show (0)
//   the reach:  x, y, x, y, ...: the points to where the movement runs out,
//               in pixels of the view from its top left
//   the beyond: the same, from there to the path's end; empty in reach
//   two rings:  the same again, each a ring lying on the ground as the
//               game's selection ring does: round where the movement runs
//               out (empty in reach), and round the path's end
//
// With nothing to draw the head stands alone. Version 2 added the rings,
// version 3 the head's last field.
#pragma once

#include "core/Types.h"

#include <span>
#include <string>
#include <vector>

namespace tb
{

inline constexpr int kPreviewVersion = 5;

struct PathSplit
{
    // From the path's start to where the movement runs out, or to its end.
    std::vector<Vec3> reach;
    // From where the movement runs out to the path's end: its first point
    // is the reach's last. Empty when the whole path is in reach.
    std::vector<Vec3> beyond;
    float length = 0; // the whole path's, in meters

    [[nodiscard]] bool TooFar() const
    {
        return !beyond.empty();
    }
};

// `path` as the creature would walk it, its first point where it stands;
// `meters` the movement it has left. The machine refuses a move whose walk
// is longer than that by any amount (TurnMachine's "Too far"), so the
// preview is red past exactly there.
[[nodiscard]] PathSplit SplitPath(std::span<const Vec3> path, float meters);

// A path's length along its points.
[[nodiscard]] float PathLength(std::span<const Vec3> path);

// How far a walk strays from a path shown: the farthest that any of the
// walk's points, or the middle of any of its steps, lies from the shown
// path. For setting the walk the game makes against the preview it was
// ordered by (the user, 2026-10-01: "does a path represent a path that the
// character will actually take? If not, it might be misleading"). Nothing
// (0) for a walk of no points or a path shown of none.
[[nodiscard]] float PathApart(std::span<const Vec3> walked, std::span<const Vec3> shown);

// Points along a path, one every `spacing` meters, from `from` meters
// along it to `to` (no further than its end): where marks are laid on the
// ground for a path shown in the world. None for a spacing of nothing.
[[nodiscard]] std::vector<Vec3> AlongPath(std::span<const Vec3> path, float spacing, float from, float to);

// `count` points of a ring lying flat round `centre`, for the game layer to
// place on the screen: seen from the camera it is the ellipse a ring on the
// ground is.
[[nodiscard]] std::vector<Vec3> RingAround(const Vec3 &centre, float radius, int count);

// A point of the path on the screen, in pixels of the view.
struct ScreenDot
{
    float x = 0;
    float y = 0;
};

struct PreviewView
{
    int width = 0; // the view's, in pixels
    int height = 0;
    float length = 0; // the path's, in meters
    float meters = 0; // the movement left
    std::vector<ScreenDot> reach;
    std::vector<ScreenDot> beyond;
    std::vector<ScreenDot> stopRing; // round where the movement runs out; empty in reach
    std::vector<ScreenDot> endRing;  // round the path's end
    bool line = true;                // the movie draws the line and the rings; false, the world has them
    // With a turn a bar of time: what the order under the cursor would cost
    // of the bar, in seconds (0: nothing to say), and that it does not fit
    // what is left, and would land on the next turn.
    float cost = 0;
    bool spills = false;
    // The creature the cursor is over, if any (version 5): the movie says
    // when its pending action lands, as its card does.
    ObjectId over = kNoObject;
};

// What an order would cost of the current creature's bar: its walk at the
// creature's pace, after the meters that cost nothing, and its action's own
// seconds. Nothing between two turns, when there is no bar.
struct OrderCost
{
    float seconds = 0;
    bool spills = false; // more than is left: it would land on the next turn
};
struct TurnView;
[[nodiscard]] OrderCost CostOf(const TurnView &view, float walkMeters, float actionSeconds);

// Nothing to draw: no path, no turn of the player's, the cursor off the
// ground.
[[nodiscard]] std::wstring EncodeNoPreview();
// No path to draw, and a cost to say: an attack on what is in reach.
[[nodiscard]] std::wstring EncodeNoPreview(const OrderCost &cost, ObjectId over = kNoObject);
[[nodiscard]] std::wstring EncodePreview(const PreviewView &view);

} // namespace tb
