#include "game/Preview.h"

#include "core/Preview.h"

#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Script.h"
#include "dao/Vfx.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

#include <Windows.h>

#include <cmath>
#include <exception>
#include <optional>
#include <utility>

namespace tb::game
{

namespace
{

namespace api = dao::script::api;

// The cursor's point must move this far for the path to be searched again,
// and a search is not made more often than this: the pathfinder's search is
// capped at 30 ms (src/game/Gate.cpp's PathLength), which is a frame or
// two, and a straight walk costs nothing.
constexpr float kMoved = 0.25f;
constexpr std::uint32_t kSearchEveryMs = 150;

// The game's own marker for where a move goes: vfx_base's row 30000,
// "Movement Indicator" (its model SR_MOV, beside the selection rings'
// SR_PTY and SR_EMY; its art vfx_movetarget.dds, a four-pointed star,
// white at the heart and amber at the edge, read 2026-10-01 from the
// table and textures.erf). A lasting effect (the table's type D), laid on
// the ground (AlignToGround).
constexpr std::int32_t kMoveMarker = 30000;

// The rings on the ground, drawn by the movie where the game's marker does
// not belong: round a path's end that is too far, about as wide as a
// creature's own selection ring, and round where the movement runs out,
// half that. Each a polygon of so many points, which the movie rounds.
constexpr float kEndRing = 0.45f;
constexpr float kStopRing = 0.22f;
constexpr int kRingPoints = 16;

Vec3 Of(const dao::Vector &v)
{
    return Vec3{v.x, v.y, v.z};
}

dao::Vector Of(const Vec3 &v)
{
    return dao::Vector{v.x, v.y, v.z};
}

float Flat(const Vec3 &a, const Vec3 &b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

dao::GameModeExplore *ExploreMode()
{
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    return modes != nullptr ? modes->Explore() : nullptr;
}

// A creature told nothing: the pathfinder is asked only then, as the gate
// asks it (a search while the creature's own request for a path may be
// pending is not proven harmless).
bool AtRest(ObjectId creature)
{
    if (api::GetCommandQueueSize(creature) != 0)
    {
        return false;
    }
    const dao::script::OwnedCommand current = api::GetCurrentCommand(creature);
    return !current || api::GetCommandType(*current.get()) == dao::script::constants::COMMAND_TYPE_INVALID;
}

// The points' places on the screen, to the first one behind the camera.
std::vector<ScreenDot> OnScreen(const std::vector<Vec3> &points)
{
    std::vector<ScreenDot> dots;
    dots.reserve(points.size());
    for (const Vec3 &point : points)
    {
        const std::optional<dao::world::ScreenPoint> at = dao::world::ToScreen(Of(point));
        if (!at || !at->inFront)
        {
            break;
        }
        dots.push_back(ScreenDot{at->x, at->y});
    }
    return dots;
}

// A ring on the ground on the screen, whole or not at all.
std::vector<ScreenDot> RingOnScreen(const Vec3 &centre, float radius)
{
    std::vector<ScreenDot> dots = OnScreen(RingAround(centre, radius, kRingPoints));
    if (dots.size() != static_cast<std::size_t>(kRingPoints))
    {
        dots.clear();
    }
    return dots;
}

} // namespace

void Preview::Install()
{
    if (installed_)
    {
        return;
    }
    installed_ = true;
    encoded_ = EncodeNoPreview();
    dao::gui::OnExternalInterface("TB_GetPreview", [this](const dao::gui::Call &call) {
        // Called from the game's UI code, which cannot take an exception.
        try
        {
            std::wstring preview;
            {
                const std::lock_guard lock(mutex_);
                preview = encoded_;
            }
            // GFx copies the text as the call returns, so a local will do.
            call.Return(dao::gui::Value::WString(preview.c_str()));
        }
        catch (const std::exception &e)
        {
            spdlog::error("TB_GetPreview: {}", e.what());
        }
    });
}

void Preview::ShowMarker(const Vec3 &at)
{
    const dao::CCArea *area = dao::world::CurrentArea();
    if (area == nullptr)
    {
        marker_ = kNoObject;
        return;
    }
    if (marker_ != kNoObject && markerArea_ == area->Id() && dao::vfx::Move(marker_, Of(at)))
    {
        return;
    }
    // None yet, or it went with its area.
    marker_ = dao::vfx::Place(kMoveMarker, Of(at));
    markerArea_ = area->Id();
    if (!std::exchange(markerSaid_, true))
    {
        spdlog::debug("the preview's end is the game's movement marker (effect {:#x})", marker_);
    }
}

void Preview::HideMarker()
{
    if (marker_ == kNoObject)
    {
        return;
    }
    // In the area it was placed in only: in another it is gone with the
    // first, and its id may be another effect's.
    const dao::CCArea *area = dao::world::CurrentArea();
    if (area != nullptr && markerArea_ == area->Id())
    {
        dao::vfx::Remove(marker_);
    }
    marker_ = kNoObject;
}

void Preview::Clear()
{
    HideMarker();
    searched_ = false;
    path_.clear();
    if (!std::exchange(drawn_, false))
    {
        return;
    }
    const std::lock_guard lock(mutex_);
    encoded_ = EncodeNoPreview();
}

void Preview::Search(ObjectId walker, const Vec3 &from, const Vec3 &to, std::uint32_t nowMs)
{
    walker_ = walker;
    from_ = from;
    to_ = to;
    searched_ = true;
    searchedAt_ = nowMs;
    path_.clear();
    const ULONGLONG began = GetTickCount64();
    const std::optional<dao::world::WalkPath> found = dao::world::FindPath(walker, Of(to));
    const ULONGLONG took = GetTickCount64() - began;
    if (found && !found->waypoints.empty())
    {
        // From where it stands, by the waypoints, to the point itself: the
        // pathfinder's ends may be a step short of either.
        path_.push_back(from);
        for (const dao::Vector &waypoint : found->waypoints)
        {
            const Vec3 point = Of(waypoint);
            if (Flat(path_.back(), point) > 0.05f)
            {
                path_.push_back(point);
            }
        }
        if (Flat(path_.back(), to) > 0.05f)
        {
            path_.push_back(to);
        }
    }
    // The first few, and any slow one: what a search costs in play is not
    // known yet.
    if (logged_ < 5 || took >= 20)
    {
        ++logged_;
        spdlog::debug("the preview's path for {:#x}: {} points to ({:.1f}, {:.1f}, {:.1f}), {} ms", walker,
                      path_.size(), to.x, to.y, to.z, took);
    }
}

void Preview::Update(const TurnView &view, std::uint32_t nowMs, bool enabled)
{
    if (!installed_)
    {
        return;
    }
    const bool waiting = enabled && view.active && view.playerTurn && view.phase == Phase::AwaitingOrder &&
                         view.current != kNoObject && view.movementLeft > 0.0f;
    const dao::GameModeExplore *explore = waiting ? ExploreMode() : nullptr;
    // The ground only, for now: over a creature or a door the cursor's point
    // is the object's, and the preview of an attack's walk is still to do.
    // And ground a move would go to: a click on rubble is not an order at
    // all (the engine's own test, which its move click makes), and a path
    // drawn to it promised one. In play a red end there, clicked, was not
    // refused "Too far": no command came (the fifteenth play test,
    // 2026-10-01).
    const std::optional<dao::Vector> ground =
        explore != nullptr && !explore->CursorObject() && explore->CursorGroundWalkable() ? explore->CursorGround()
                                                                                          : std::nullopt;
    if (!ground || !AtRest(view.current))
    {
        Clear();
        return;
    }
    const Vec3 from = Of(api::GetPosition(view.current));
    const Vec3 to = Of(*ground);
    const bool stale = !searched_ || walker_ != view.current || Flat(from_, from) > 0.1f || Flat(to_, to) > kMoved;
    if (stale && (!searched_ || walker_ != view.current || nowMs - searchedAt_ >= kSearchEveryMs))
    {
        Search(view.current, from, to, nowMs);
    }
    if (path_.size() < 2)
    {
        // No path there as the game finds it: nothing is promised, so
        // nothing is drawn.
        HideMarker();
        if (std::exchange(drawn_, false))
        {
            const std::lock_guard lock(mutex_);
            encoded_ = EncodeNoPreview();
        }
        return;
    }
    const PathSplit split = SplitPath(path_, view.movementLeft);
    PreviewView shown;
    if (const std::optional<dao::world::ViewSize> size = dao::world::ScreenSize())
    {
        shown.width = size->width;
        shown.height = size->height;
    }
    shown.length = split.length;
    shown.meters = view.movementLeft;
    shown.reach = OnScreen(split.reach);
    // Only if the reach is whole: a line broken behind the camera has no
    // second half to join.
    if (shown.reach.size() == split.reach.size())
    {
        shown.beyond = OnScreen(split.beyond);
        if (split.TooFar())
        {
            shown.stopRing = RingOnScreen(split.reach.back(), kStopRing);
        }
        // A path too far ends in a ring of the movie's; one in reach in
        // the game's own marker, below.
        if (split.TooFar() && shown.beyond.size() == split.beyond.size())
        {
            shown.endRing = RingOnScreen(path_.back(), kEndRing);
        }
    }
    if (split.TooFar())
    {
        HideMarker();
    }
    else
    {
        ShowMarker(path_.back());
    }
    std::wstring encoded = EncodePreview(shown);
    drawn_ = true;
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
}

} // namespace tb::game
