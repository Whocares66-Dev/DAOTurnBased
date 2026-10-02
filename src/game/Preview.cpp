#include "game/Preview.h"

#include "core/Preview.h"
#include "core/Rules.h"
#include "game/Gate.h"

#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Script.h"
#include "dao/Vfx.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <span>
#include <utility>

namespace tb::game
{

namespace
{

namespace api = dao::script::api;
namespace k = dao::script::constants;

// The cursor's point must move this far for the path to be searched again,
// and a search is not made more often than this: the pathfinder's search is
// capped at 30 ms (src/game/Gate.cpp's PathLength), which is a frame or
// two, and a straight walk costs nothing. In play a sweep of forty places
// kept 57 to 61 frames a second and the searches were 0 to 16 ms (the
// seventeenth play test), so a tenth of a meter, where it was a quarter:
// between searches the path's end follows the cursor anyway.
constexpr float kMoved = 0.1f;
constexpr std::uint32_t kSearchEveryMs = 150;

// The game's own marker for where a move goes (the user, 2026-10-01: "the
// game has its own marker for indicating where to walk. Can we use that?",
// and of a star the movie drew flat over the scene, "we should draw the
// indicator on the ground, not flat on the screen, which looks weird"):
// vfx_base's row 30000, "Movement Indicator", the star the game puts where
// a move click lands (model SR_MOV, beside the selection rings' SR_PTY and
// SR_EMY; its art vfx_movetarget.dds; a lasting effect laid on the ground).
// Placed in the world at an end in reach through DAOLib's dao/Vfx.h, moved
// after the cursor, taken away with the preview.
//
// It is left out of the cursor's pick (SetPickable): SR_MOV is a selection
// reticle and has a shape the cursor's ray meets, and it sits under the
// cursor. The first try had it picked, and the preview blinked and moves
// were lost; most of that proved to be the movie's own drawing under the
// cursor (ui/turnorder, hitTestDisable; the sixteenth play test), and what
// part the effect had was not told apart.
constexpr std::int32_t kMoveMarker = 30000;

// Past the movement left there is no marker. The user asked for a red one
// there ("how about red rings and a red marker after the move limit? Then
// it's easy to see how long player can move"), and it was tried: the same
// row placed in a colour (DAOLib's tint for a placed effect, 61e471e). The
// call was made and the star stayed gold (the twentieth play test,
// 2026-10-01): its material, GeneralGui.mat, takes no tint. A gold star at
// an end too far says "it will walk here" of a move the machine refuses,
// so there is none: the end has a red ring, and a red star wants a texture
// and a row of our own.

// The path on the ground (`[feedback] move_preview_ground`; the user,
// 2026-10-01: "should the line be drawn on the ground as well?"). A line
// in the world is not to be had: an effect is scaled by one number, so no
// row can be stretched along a step, and a model of our own is days of
// work (lib-agent's reading). So marks: the game's own selection rings
// made small, one every so often along the path, the party's gold ring in
// reach and the enemy's red one beyond, each left out of the cursor's
// pick. They begin outside the walker's own ring and stop short of the
// marker; a too-far end has the red ring at its size. If a path is long
// the marks are spread to keep their number down.
constexpr std::int32_t kPartyRing = 30005; // "Party Member", SR_PTY
// The red is the enemy's ring as it is when targeted: by their textures
// the plain enemy ring (30011, vfx_selbad.dds) is two and a half times
// dimmer than the party's gold one and thin, and as small marks it was a
// faint smudge (the twentieth play test); the targeted one (vfx_selbad2.dds)
// is half as bright again, wider, and has pale highlights.
constexpr std::int32_t kEnemyRing = 30012; // "Enemy Targeted", SR_EMYt
// Small and many, a dotted line (the user, 2026-10-01, of rings 0.21 of a
// creature's every 0.5 m: "wonder if we can make the rings smaller. A
// larger number of smaller rings is basically a dotted line"). At 0.14
// every 0.4 m the trail was too slight far off (the nineteenth play test);
// closer together, it is the line of them that is seen, not each.
constexpr float kMarkScale = 0.10f;
constexpr float kRedMarkScale = 0.13f; // the red ones a little larger, being dimmer
constexpr float kMarkEvery = 0.2f;
constexpr float kMarksFrom = 0.7f;    // meters along: clear of the walker's ring
constexpr float kMarksShortOf = 0.4f; // of the end: clear of the marker there
// The ring round a too-far end, which has no marker: half a creature's.
// At a creature's size it was seen, but it is an enemy's ring, pointed
// mark and all, and at a glance an enemy stood there; and it hid the red
// marks leading to it (the nineteenth play test's second look).
constexpr float kEndRingScale = 0.5f;
constexpr std::size_t kMostMarks = 150;

// The movie's line begins at the edge of the walker's own selection ring,
// not at its feet: drawn over the scene it lay across the walker's shin
// (the eighteenth play test's close picture).
constexpr float kLineFrom = 0.6f;

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

// The game's marker at the path's end, where the move would go.
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
    if (marker_ != kNoObject)
    {
        dao::vfx::SetPickable(marker_, false);
    }
    if (!std::exchange(markerSaid_, true))
    {
        spdlog::debug("the preview's end is the game's movement marker (effect {:#x}), left out of the cursor's pick",
                      marker_);
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

// `marks` made to lie at `at`: those there are moved if their place has
// changed, more are placed, the rest are taken away.
void Preview::Lay(Marks &marks, std::int32_t row, float scale, const std::vector<Vec3> &at)
{
    for (std::size_t i = 0; i < at.size(); ++i)
    {
        if (i < marks.ids.size())
        {
            // Most frames nothing has moved: the cursor rests.
            if (Distance(marks.at[i], at[i]) < 0.005f)
            {
                continue;
            }
            if (dao::vfx::Move(marks.ids[i], Of(at[i])))
            {
                marks.at[i] = at[i];
                continue;
            }
        }
        const ObjectId placed = dao::vfx::Place(row, Of(at[i]), scale);
        if (placed != kNoObject)
        {
            dao::vfx::SetPickable(placed, false);
        }
        if (i < marks.ids.size())
        {
            marks.ids[i] = placed;
            marks.at[i] = at[i];
        }
        else
        {
            marks.ids.push_back(placed);
            marks.at.push_back(at[i]);
        }
    }
    while (marks.ids.size() > at.size())
    {
        dao::vfx::Remove(marks.ids.back());
        marks.ids.pop_back();
        marks.at.pop_back();
    }
}

void Preview::HideMarks()
{
    if (reachMarks_.ids.empty() && beyondMarks_.ids.empty() && endMarks_.ids.empty())
    {
        return;
    }
    // As the marker: in their own area only.
    const dao::CCArea *area = dao::world::CurrentArea();
    const bool here = area != nullptr && markerArea_ == area->Id();
    for (Marks *marks : {&reachMarks_, &beyondMarks_, &endMarks_})
    {
        if (here)
        {
            for (const ObjectId mark : marks->ids)
            {
                dao::vfx::Remove(mark);
            }
        }
        marks->ids.clear();
        marks->at.clear();
    }
}

// Does the path shown stand for the walk the game makes? Shown, the path
// is the pathfinder's whole answer then; ordered, the creature asks the
// pathfinder itself, a stretch at a time as it goes (its own path, DAOLib's
// CurrentPath, is only the next waypoint or two: lib-agent, 2026-10-01), so
// the two are not set point against point. Instead, when the creature a
// path was shown for walks within a moment of it, where it is each frame is
// measured against the path shown, and when it stands again the log says
// how far from it the walk went at most. More than half a meter, with
// nobody else moving, is a stray, said at info. A walk that ends elsewhere
// than the path's end is not judged: the click was elsewhere, or it was a
// walk to something's reach.
void Preview::Compare(std::uint32_t nowMs)
{
    constexpr std::uint32_t kStillFresh = 1500;
    constexpr float kSameEnd = 1.0f;
    constexpr float kStray = 0.5f;
    if (shown_.size() < 2)
    {
        return;
    }
    const bool atRest = AtRest(shownFor_);
    if (!walking_)
    {
        if (atRest)
        {
            if (nowMs - shownAt_ > kStillFresh)
            {
                shown_.clear(); // shown, and no walk came of it
            }
            return;
        }
        walking_ = true;
        walked_ = 0.0f;
        farthest_ = 0.0f;
        farthestAt_ = 0.0f;
        last_ = Of(api::GetPosition(shownFor_));
    }
    const Vec3 here = Of(api::GetPosition(shownFor_));
    walked_ += Distance(last_, here);
    last_ = here;
    if (const float apart = PathApart(std::span<const Vec3>(&here, 1), shown_); apart > farthest_)
    {
        farthest_ = apart;
        farthestAt_ = walked_;
    }
    if (!atRest)
    {
        return;
    }
    // It stands again: the walk is over.
    const float ends = Flat(here, shown_.back());
    if (ends > kSameEnd)
    {
        spdlog::debug("{:#x} walked {:.1f} m to another place than the path shown ({:.1f} m from its end): not "
                      "judged against it",
                      shownFor_, walked_, ends);
    }
    else
    {
        const bool stray = farthest_ > kStray;
        spdlog::log(stray ? spdlog::level::info : spdlog::level::debug,
                    "the walk taken against the path shown for {:#x}: {:.1f} m walked, {:.1f} m shown; at most "
                    "{:.2f} m from the path, {:.1f} m along{}",
                    shownFor_, walked_, PathLength(shown_), farthest_, farthestAt_,
                    stray ? ": the walk strays from what was shown" : "");
    }
    walking_ = false;
    shown_.clear();
}

void Preview::Clear()
{
    HideMarker();
    HideMarks();
    searched_ = false;
    path_.clear();
    if (!std::exchange(drawn_, false))
    {
        return;
    }
    const std::lock_guard lock(mutex_);
    encoded_ = EncodeNoPreview();
}

void Preview::Search(ObjectId walker, const Vec3 &from, const Vec3 &to, ObjectId toward, std::uint32_t nowMs)
{
    walker_ = walker;
    toward_ = toward;
    from_ = from;
    to_ = to;
    searched_ = true;
    searchedAt_ = nowMs;
    path_.clear();
    const ULONGLONG began = GetTickCount64();
    // To a creature, with its own obstacle lifted, as the gate searches
    // for an attack's walk.
    const std::optional<dao::world::WalkPath> found = dao::world::FindPath(walker, Of(to), toward);
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
        // Its last point is the cursor's, which Update keeps there.
        if (Flat(path_.back(), to) > 0.05f)
        {
            path_.push_back(to);
        }
        else
        {
            path_.back() = to;
        }
        route_ = found->Length();
        routeEnd_ = Of(found->waypoints.back());
    }
    // The first few, and any slow one; and every fifty, what they cost: a
    // sweep of the cursor is many searches.
    if (logged_ < 5 || took >= 20)
    {
        ++logged_;
        spdlog::debug("the preview's path for {:#x}: {} points to ({:.1f}, {:.1f}, {:.1f}), {} ms", walker,
                      path_.size(), to.x, to.y, to.z, took);
    }
    ++searches_;
    searchMs_ += took;
    slowestMs_ = std::max(slowestMs_, took);
    if (searches_ == 50)
    {
        spdlog::debug("the preview's last 50 searches: {} ms in all, the slowest {} ms", searchMs_, slowestMs_);
        searches_ = 0;
        searchMs_ = 0;
        slowestMs_ = 0;
    }
}

void Preview::Update(const TurnView &view, std::uint32_t nowMs, bool enabled, bool onGround)
{
    if (!installed_)
    {
        return;
    }
    Compare(nowMs);
    // With less than a step left a move is held whatever its length
    // ("no movement left"), and a red line to everywhere says nothing: in
    // play it read "8.4 m of 0.0" wherever the cursor lay (the seventeenth
    // play test).
    const bool waiting = enabled && view.active && view.playerTurn && view.phase == Phase::AwaitingOrder &&
                         view.current != kNoObject && view.movementLeft >= TurnMachine::kMinStep;
    const dao::GameModeExplore *explore = waiting ? ExploreMode() : nullptr;
    const std::optional<ObjectId> object = explore != nullptr ? explore->CursorObject() : std::nullopt;
    // Over an enemy, with an action left: the walk an attack on it would
    // take (the user's first list: "the same for an attack: gold if the
    // target can be reached this turn, red if not, before the player
    // clicks"). Over anything else that is an object (a door, a chest, a
    // friend) the click is no walk the preview knows, and nothing is drawn.
    const ObjectId target = object && view.actionsLeft > 0 && api::GetObjectType(*object) == k::OBJECT_TYPE_CREATURE &&
                                    api::IsDead(*object) == 0 && api::IsObjectHostile(view.current, *object) != 0
                                ? *object
                                : kNoObject;
    // Otherwise the ground, and ground a move would go to: a click on
    // rubble is not an order at all (the engine's own test, which its move
    // click makes), and a path drawn to it promised one. In play a red end
    // there, clicked, was not refused "Too far": no command came (the
    // fifteenth play test, 2026-10-01).
    std::optional<dao::Vector> point;
    if (target != kNoObject)
    {
        point = api::GetPosition(target);
    }
    else if (explore != nullptr && !object && explore->CursorGroundWalkable())
    {
        point = explore->CursorGround();
    }
    if (!point || !AtRest(view.current))
    {
        Clear();
        return;
    }
    const Vec3 from = Of(api::GetPosition(view.current));
    const Vec3 to = Of(*point);
    const bool another = !searched_ || walker_ != view.current || toward_ != target;
    const bool stale = another || Flat(from_, from) > 0.1f || Flat(to_, to) > kMoved;
    if (stale && (another || nowMs - searchedAt_ >= kSearchEveryMs))
    {
        Search(view.current, from, to, target, nowMs);
    }
    if (path_.size() < 2)
    {
        // No path there as the game finds it: nothing is promised, so
        // nothing is drawn.
        HideMarker();
        HideMarks();
        if (std::exchange(drawn_, false))
        {
            const std::lock_guard lock(mutex_);
            encoded_ = EncodeNoPreview();
        }
        return;
    }
    path_.back() = to;
    // Too far or not is the gate's to say when the click comes, by the
    // length it measures (src/game/Gate.cpp's PathLength: the waypoints'
    // length and the rest of the way to the point), so the preview measures
    // the same and says the same. The line drawn begins at the creature's
    // feet and so may be a little longer than that; by its own length the
    // plate read "4.2 m of 4.0" in red and the click walked 4.0 m (the
    // seventeenth play test). The difference is taken off where the line
    // is cut, and the plate gives the gate's number.
    const float order = route_ + Flat(routeEnd_, to);
    const float drawn = PathLength(path_);
    // What is walked: all of a move's path; of an attack's, as far as where
    // the attacker can strike, by the gate's own sum (WalkToTarget: the
    // path or the straight line, whichever is longer, less the reach).
    float walk = order;
    std::vector<Vec3> walked = path_;
    if (target != kNoObject)
    {
        const float distance = api::GetDistanceBetween(view.current, target);
        walk = WalkToReach(std::max(order, distance), ReachOf(view.current, target));
        walked = SplitPath(path_, walk + (drawn - order)).reach;
        if (walk < 0.05f || walked.size() < 2)
        {
            // In reach where it stands: no walk to show.
            HideMarker();
            HideMarks();
            if (std::exchange(drawn_, false))
            {
                const std::lock_guard lock(mutex_);
                encoded_ = EncodeNoPreview();
            }
            return;
        }
    }
    const bool attack = target != kNoObject;
    const PathSplit split = SplitPath(walked, view.movementLeft + (PathLength(walked) - walk));
    PreviewView shown;
    if (const std::optional<dao::world::ViewSize> size = dao::world::ScreenSize())
    {
        shown.width = size->width;
        shown.height = size->height;
    }
    shown.length = walk;
    shown.meters = view.movementLeft;
    // The drawn line leaves out its first stretch, inside the walker's ring.
    const PathSplit outside = SplitPath(split.reach, kLineFrom);
    const std::vector<Vec3> &lineReach = outside.TooFar() ? outside.beyond : split.reach;
    shown.reach = OnScreen(lineReach);
    // Where the move would go, the game's marker; past the movement left
    // none, since it says "it will walk here" and it would not. An attack's
    // walk ends at its target, which has its own ring.
    if (split.TooFar() || attack)
    {
        HideMarker();
    }
    else
    {
        ShowMarker(walked.back());
    }
    // The path in the world: marks along it, in the marker's area (which
    // ShowMarker keeps; with no marker placed the area is asked here).
    const dao::CCArea *area = dao::world::CurrentArea();
    shown.line = !onGround || area == nullptr;
    if (shown.line)
    {
        HideMarks();
    }
    else
    {
        if (markerArea_ != area->Id())
        {
            // Another area than the marks were laid in: they went with it.
            for (Marks *marks : {&reachMarks_, &beyondMarks_, &endMarks_})
            {
                marks->ids.clear();
                marks->at.clear();
            }
            markerArea_ = area->Id();
        }
        const float reachEnd = PathLength(split.reach);
        const float beyondEnd = PathLength(split.beyond);
        // Spread, if they would be too many.
        const float every = std::max(kMarkEvery, (reachEnd + beyondEnd) / static_cast<float>(kMostMarks));
        // Short of the marker at a move's end, and of the ring round a
        // too-far one; an attack's walk has neither.
        const float reachTo = split.TooFar() || attack ? reachEnd : reachEnd - kMarksShortOf;
        const float beyondTo = attack ? beyondEnd : beyondEnd - kMarksShortOf;
        Lay(reachMarks_, kPartyRing, kMarkScale, AlongPath(split.reach, every, kMarksFrom, reachTo));
        // The red ones go on in step from where the gold ones end.
        const float into = every - std::fmod(std::max(0.0f, reachEnd - kMarksFrom), every);
        Lay(beyondMarks_, kEnemyRing, kRedMarkScale,
            split.TooFar() ? AlongPath(split.beyond, every, into, beyondTo) : std::vector<Vec3>{});
        Lay(endMarks_, kEnemyRing, kEndRingScale,
            split.TooFar() && !attack ? std::vector<Vec3>{walked.back()} : std::vector<Vec3>{});
        if (!std::exchange(marksSaid_, true))
        {
            spdlog::debug("the preview's path is marked on the ground: {} in reach, {} beyond, one every {:.2f} m",
                          reachMarks_.ids.size(), beyondMarks_.ids.size(), every);
        }
    }
    // Only if the reach is whole: a line broken behind the camera has no
    // second half to join.
    if (shown.reach.size() == lineReach.size())
    {
        shown.beyond = OnScreen(split.beyond);
        if (shown.line && split.TooFar())
        {
            shown.stopRing = RingOnScreen(split.reach.back(), kStopRing);
        }
        if (shown.line && !attack && shown.beyond.size() == split.beyond.size() &&
            (split.TooFar() || marker_ == kNoObject))
        {
            shown.endRing = RingOnScreen(walked.back(), kEndRing);
        }
    }
    std::wstring encoded = EncodePreview(shown);
    drawn_ = true;
    if (!walking_)
    {
        shown_ = walked;
        shownFor_ = view.current;
        shownAt_ = nowMs;
    }
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
}

} // namespace tb::game
