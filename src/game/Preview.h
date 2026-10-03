// The movement preview's side of the plugin (dev/UI.md, "The movement
// preview"): on a party member's turn, while the game waits for its order,
// the path from it to the ground under the cursor, gold to where its
// movement runs out and red beyond. The turn bar's movie draws it: it asks
//
//   ExternalInterface.call("TB_GetPreview")   the path on the screen, as
//                                             core/Preview.h encodes it
//
// and gets what the frame callback last published, as it gets the bar's
// view (src/game/Bar.h). Through DAOLib: the cursor's point is explore
// mode's own (dao/GameModes.h, CursorGround), the path the game's
// pathfinder's (dao/World.h, FindPath, for a creature at rest), and a
// point's place on the screen the scene camera's (ToScreen). The path is
// searched for again only when the cursor's point has moved, and not every
// frame; its place on the screen is asked every frame, so the line stays on
// the ground as the camera moves.
//
// Where a move would go, the path ends in the game's own movement marker
// (the user, 2026-10-01: "the game has its own marker for indicating where
// to walk. Can we use that?"; and "on the ground, not flat on the screen"):
// the effect the game puts where a move click lands, placed in the world at
// the path's end and moved after the cursor (src/game/Preview.cpp). Past
// the movement left the path is red from where the movement runs out, so
// that how far the creature can go is seen (the user), and its end has a
// red ring and no marker: the marker could not be made red. The path
// itself is small rings on the ground, a dotted line of the game's own
// selection rings, or with the setting off a line the movie draws. Over an
// enemy the path is an attack's walk: to where the attacker can strike, by
// the reach the gate judges the attack by, gold if that is within the
// movement left and red if not.
#pragma once

#include "core/Preview.h"
#include "core/TurnMachine.h"
#include "core/Types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tb::game
{

class Preview
{
  public:
    // Registers the name with the game's UI. Once; later calls do nothing.
    void Install();
    [[nodiscard]] bool Installed() const
    {
        return installed_;
    }

    // Every frame, from the frame callback: finds, places and publishes the
    // path, or publishes nothing to draw. `enabled` is the setting, and
    // `onGround` its style: the path as marks laid in the world, not a line
    // the movie draws.
    void Update(const TurnView &view, std::uint32_t nowMs, bool enabled, bool onGround);

  private:
    void Clear();
    void ShowCost(const OrderCost &cost);
    void Search(ObjectId walker, const Vec3 &from, const Vec3 &to, ObjectId toward, std::uint32_t nowMs);
    void Compare(std::uint32_t nowMs);
    // Effects placed along the path, and where each lies.
    struct Marks
    {
        std::vector<ObjectId> ids;
        std::vector<Vec3> at;
    };

    void ShowMarker(const Vec3 &at);
    void HideMarker();
    void Lay(Marks &marks, std::int32_t row, float scale, const std::vector<Vec3> &at);
    void HideMarks();

    std::mutex mutex_;
    std::wstring encoded_; // what the movie is given
    bool installed_ = false;
    bool drawn_ = false;        // something is published to draw
    ObjectId over_ = kNoObject; // the living creature the cursor is over, while an order is waited on

    // The path last found, the frame callback's alone.
    ObjectId walker_ = kNoObject;
    ObjectId toward_ = kNoObject; // the creature the path is to, for an attack's; none for a move's
    Vec3 from_;                   // where the walker stood
    Vec3 to_;                     // the cursor's point it was found for
    bool searched_ = false;
    std::uint32_t searchedAt_ = 0;
    std::vector<Vec3> path_; // empty: the game finds no path there
    float route_ = 0;        // the waypoints' own length, as the gate measures an order
    Vec3 routeEnd_;          // the last of them
    int logged_ = 0;
    // The game's movement marker at the path's end, while one is placed,
    // and the area it was placed in: an effect goes with its area, and its
    // id is not asked after in another.
    ObjectId marker_ = kNoObject;
    ObjectId markerArea_ = kNoObject;
    bool markerSaid_ = false;

    // The path's marks on the ground, in reach and beyond, and the ring
    // round a too-far end: effects placed once and moved, of the marker's
    // area.
    Marks reachMarks_;
    Marks beyondMarks_;
    Marks endMarks_;
    bool marksSaid_ = false;

    // The path last shown, kept a moment after: if its creature then
    // walks, where it goes is measured against it (Compare).
    std::vector<Vec3> shown_;
    ObjectId shownFor_ = kNoObject;
    std::uint32_t shownAt_ = 0;
    bool walking_ = false; // the walk that came of it is under way
    Vec3 last_;            // where the walker was last frame
    float walked_ = 0;     // meters, so far
    float farthest_ = 0;   // from the path shown, at most
    float farthestAt_ = 0; // and how far along the walk that was
    int searches_ = 0;     // since the last fifty were told of
    std::uint64_t searchMs_ = 0;
    std::uint64_t slowestMs_ = 0;
};

} // namespace tb::game
