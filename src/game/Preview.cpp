#include "game/Preview.h"

#include "core/Preview.h"
#include "core/Rules.h"
#include "core/Text.h"
#include "game/Gate.h"

#include "dao/Attack.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Resources.h"
#include "dao/Saves.h"
#include "dao/Script.h"
#include "dao/Vfx.h"
#include "dao/World.h"
#include "dao/core/GdaHash.h"
#include "dao/generated/ScriptApi.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
// An attack's walk shorter than this is not drawn: it is the shuffle of a
// strike, within the meter that costs nothing.
constexpr float kNoWalk = 0.5f;
constexpr std::uint32_t kSearchEveryMs = 150;

// The game's own marker for where a move goes (the user, 2026-10-01: "the
// game has its own marker for indicating where to walk. Can we use that?",
// and of a star the movie drew flat over the scene, "we should draw the
// indicator on the ground, not flat on the screen, which looks weird"):
// vfx_base's row 30000, "Movement Indicator", the star the game puts where
// a move click lands (model SR_MOV, beside the selection rings' SR_PTY and
// SR_EMY; its art vfx_movetarget.dds; a lasting effect laid on the ground).
// Placed in the world at the path's end through DAOLib's dao/Vfx.h, moved
// after the cursor, taken away with the preview.
//
// It is left out of the cursor's pick (SetPickable): SR_MOV is a selection
// reticle and has a shape the cursor's ray meets, and it sits under the
// cursor. The first try had it picked, and the preview blinked and moves
// were lost; most of that proved to be the movie's own drawing under the
// cursor (ui/turnorder, hitTestDisable; the sixteenth play test), and what
// part the effect had was not told apart.
constexpr std::int32_t kMoveMarker = 30000;

// The path on the ground (`[feedback] move_preview_ground`; the user,
// 2026-10-01: "should the line be drawn on the ground as well?"). A line
// in the world is not to be had: an effect is scaled by one number, so no
// row can be stretched along a step, and a model of our own is days of
// work (lib-agent's reading). So marks: the party's selection ring made
// small, one every so often along the path, each left out of the cursor's
// pick. They begin outside the walker's own ring and stop short of the
// marker. If a path is long the marks are spread to keep their number
// down.
//
// Past the movement left the marks and the marker are the same two in red
// (the user, 2026-10-01: "how about red rings and a red marker after the
// move limit? Then it's easy to see how long player can move"; and
// 2026-10-09, of the enemy's ring that was both until then: "replace the
// red walk circles with the regular gold circle by red color instead of
// the enemy circle indicator", and "the end circle with a red version of
// the regular gold compass marker to indicate you can't walk there"). No
// reticle takes a colour from its caller: the marker placed with DAOLib's
// tint stayed gold, twice (the twentieth and twenty-first play tests,
// 2026-10-01; dao/Vfx.h has what was read). So each is the game's own made
// again, the way the game makes an effect: a row of vfx_base of the mod's
// own names the game's model under a name of ours, which names the game's
// material object under a name of ours, which names the game's texture
// with its gold turned to the red of the enemy's ring (issue 72).
//
// And the gold marks are a row of the mod's own too, not the game's 30005:
// a mark is the ring at a tenth of its size, its band a tenth as wide,
// under a pixel on the screen (the user, 2026-10-09, of the marks beside
// the rings under the characters: "thin and faint"), so the marks' ring,
// gold and red, is the game's art with its band widened: twice as wide
// (tools/reticles.py's BAND; seen in play that day).
//
// tools/reticles.py makes the files from the game's, the build puts them
// in the plugin's folder's override subfolder, and DAOLib's loader has the
// game read that as it reads its own override folder (DAOLib's issue 43).
constexpr std::int32_t kRedRing = 1000000;   // row 30005 again, its models tb_red and its two larger
constexpr std::int32_t kRedMarker = 1000001; // row 30000 again, its model tb_mov
constexpr std::int32_t kGoldRing = 1000002;  // row 30005 again, its models tb_gld and its two larger
constexpr std::wstring_view kRedRingModel = L"tb_red";
constexpr std::wstring_view kRedMarkerModel = L"tb_mov";
constexpr std::wstring_view kGoldRingModel = L"tb_gld";
// Every file the three rows come to, as tools/reticles.py names them
// (tools/test_reticles.py holds the two lists together).
constexpr std::array<std::wstring_view, 20> kArt = {
    L"tb_gold_selparty.dds",
    L"tb_gold_selparty_med.dds",
    L"tb_red_selparty.dds",
    L"tb_red_selparty_med.dds",
    L"tb_red_movetarget.dds",
    L"tb_red_movearrow.dds",
    L"tbgld.mao",
    L"tbgld_med.mao",
    L"tbred.mao",
    L"tbred_med.mao",
    L"tbred_movemark.mao",
    L"tbred_arw.mao",
    L"tb_gld.mmh",
    L"tb_gld_m.mmh",
    L"tb_gld_l.mmh",
    L"tb_red.mmh",
    L"tb_red_m.mmh",
    L"tb_red_l.mmh",
    L"tb_mov.mmh",
    L"vfx_base_turnbased.gda",
};
constexpr std::int32_t kVfxTable = 8; // "Visual Effects", vfx_base and all named after it (m2da_base.gda)

// Small and many, a dotted line (the user, 2026-10-01, of rings 0.21 of a
// creature's every 0.5 m: "wonder if we can make the rings smaller. A
// larger number of smaller rings is basically a dotted line"). At 0.14
// every 0.4 m the trail was too slight far off (the nineteenth play test);
// closer together, it is the line of them that is seen, not each.
constexpr float kMarkScale = 0.10f;
constexpr float kMarkEvery = 0.2f;
constexpr float kMarksFrom = 0.7f;    // meters along: clear of the walker's ring
constexpr float kMarksShortOf = 0.4f; // of the end: clear of the marker there
constexpr std::size_t kMostMarks = 150;
// Placing a mark costs the engine 130 to 230 microseconds and moving one
// under 1 (measured 2026-10-10, issue 109: a path's 150 placed in one frame
// were 20 to 25 ms, a frame dropped each time a path came back from
// nothing). So a mark is placed once and kept, moved this far under its
// place while no path needs it, and no more than these are placed in one
// frame: a first path is whole a sixth of a second after it begins.
constexpr float kOutOfSight = 1000.0f;
constexpr int kPlacedAFrame = 15;

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

// Whether the marks' rings and the red marker are the game's to place:
// every file of theirs found by its resource system, and their three rows
// in its table naming their models. Looked for once and said, since they
// are built apart from the dll, as the movies are, and a plugin deployed
// without them, or under a loader from before DAOLib's issue 43, would
// place rows the game has not.
bool FindArt()
{
    std::string found;
    std::string missing;
    for (const std::wstring_view file : kArt)
    {
        std::string &list = dao::resources::Exists(file) ? found : missing;
        list += (list.empty() ? "" : ", ") + Utf8(file);
    }
    std::string rows;
    bool named = true;
    for (const auto &[row, wanted] : {std::pair{kRedRing, kRedRingModel}, std::pair{kRedMarker, kRedMarkerModel},
                                      std::pair{kGoldRing, kGoldRingModel}})
    {
        const std::wstring model =
            dao::gda::GetString(kVfxTable, row, dao::core::GdaColumnHash("ModelName")).value_or(std::wstring{});
        named = named && model == wanted;
        rows +=
            std::format("{}row {} names '{}' ({} wanted)", rows.empty() ? "" : ", ", row, Utf8(model), Utf8(wanted));
    }
    if (missing.empty() && named)
    {
        spdlog::info("the preview's rings and red marker are the game's to place: all {} of their files found by "
                     "the game ({}), and of vfx_base {}",
                     kArt.size(), found, rows);
        return true;
    }
    spdlog::warn("the preview's rings and red marker are not the game's to place: of their {} files the game finds "
                 "[{}] and not [{}], and of vfx_base {}. python tools\\reticles.py makes them, a build puts them in "
                 "override\\ beside turnbased.dll and DAOLib's loader of its issue 43 has the game read them there: "
                 "without them a path is the movie's line, not marks on the ground",
                 kArt.size(), found, missing, rows);
    return false;
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
    dao::saves::OnGameLoaded([this](const std::optional<dao::saves::Slot> & /*save*/) { loaded_ = true; });
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

// The marker at the path's end, where the move would go: `row` is the
// game's, or the red one for an end the move would not reach.
void Preview::ShowMarker(std::int32_t row, const Vec3 &at)
{
    const dao::CCArea *area = dao::world::CurrentArea();
    if (area == nullptr)
    {
        marker_ = kNoObject;
        return;
    }
    if (marker_ != kNoObject && markerRow_ == row && markerArea_ == area->Id() && dao::vfx::Move(marker_, Of(at)))
    {
        return;
    }
    // None yet, or it went with its area, or the other of the two stands.
    HideMarker();
    marker_ = dao::vfx::Place(row, Of(at));
    markerRow_ = row;
    markerArea_ = area->Id();
    if (marker_ != kNoObject)
    {
        dao::vfx::SetPickable(marker_, false);
    }
    spdlog::debug("the preview's end is vfx_base's row {} (effect {:#x}), {}, left out of the cursor's pick", row,
                  marker_,
                  row == kRedMarker ? "the movement marker in red: beyond reach" : "the game's movement marker");
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

// `marks` made to lie at `at`: those kept are moved to it, more are placed
// where there are too few (kPlacedAFrame of them a frame, the rest with
// the frames that follow, a path being laid every frame it shows), and
// those the path does not need go out of sight, kept for the next.
void Preview::Lay(Marks &marks, std::int32_t row, const std::vector<Vec3> &at)
{
    std::size_t laid = at.size();
    int placed = 0;
    for (std::size_t i = 0; i < at.size(); ++i)
    {
        // Most frames nothing has moved: the cursor rests.
        if (i < marks.at.size() && Distance(marks.at[i], at[i]) < 0.005f)
        {
            continue;
        }
        if (i >= marks.ids.size() || !dao::vfx::Move(marks.ids[i], Of(at[i])))
        {
            if (placed == kPlacedAFrame)
            {
                laid = i;
                break;
            }
            ++placed;
            const ObjectId mark = dao::vfx::Place(row, Of(at[i]), kMarkScale);
            if (mark != kNoObject)
            {
                dao::vfx::SetPickable(mark, false);
            }
            if (i < marks.ids.size())
            {
                marks.ids[i] = mark;
            }
            else
            {
                marks.ids.push_back(mark);
            }
        }
        if (i < marks.at.size())
        {
            marks.at[i] = at[i];
        }
        else
        {
            marks.at.push_back(at[i]);
        }
    }
    if (placed > 0)
    {
        spdlog::debug("the preview places {} mark(s) of vfx_base's row {}, {}: {} are kept", placed, row,
                      row == kRedRing ? "the party's ring in red: beyond reach" : "the party's ring in gold",
                      marks.ids.size());
    }
    for (std::size_t i = at.size(); i < marks.at.size(); ++i)
    {
        Vec3 under = marks.at[i];
        under.z -= kOutOfSight;
        dao::vfx::Move(marks.ids[i], Of(under));
    }
    marks.at.resize(std::min({marks.at.size(), at.size(), laid}));
}

// No path to mark: the marks go out of sight, kept.
void Preview::HideMarks()
{
    if (reachMarks_.at.empty() && beyondMarks_.at.empty())
    {
        return;
    }
    // In their own area only: in another they are gone with the first, and
    // their ids may be other effects'.
    const dao::CCArea *area = dao::world::CurrentArea();
    if (area == nullptr || marksArea_ != area->Id())
    {
        reachMarks_ = {};
        beyondMarks_ = {};
        return;
    }
    Lay(reachMarks_, kGoldRing, {});
    Lay(beyondMarks_, kRedRing, {});
}

// The fight is over: the marks kept through it are taken out of the world.
void Preview::RemoveMarks()
{
    if (reachMarks_.ids.empty() && beyondMarks_.ids.empty())
    {
        return;
    }
    const dao::CCArea *area = dao::world::CurrentArea();
    const bool here = area != nullptr && marksArea_ == area->Id();
    for (Marks *marks : {&reachMarks_, &beyondMarks_})
    {
        if (here)
        {
            for (const ObjectId mark : marks->ids)
            {
                dao::vfx::Remove(mark);
            }
        }
        *marks = {};
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

// Nothing to draw, and a cost to say (an attack on what is in reach): the
// movie shows it on the bar of time.
void Preview::ShowCost(const OrderCost &cost)
{
    HideMarker();
    HideMarks();
    std::wstring encoded = EncodeNoPreview(cost, over_);
    // Something to take back when the cursor leaves (Clear).
    drawn_ = cost.seconds > 0.0f || over_ != kNoObject;
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
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
    // To a creature: the walk the engine's own approach for an attack on it
    // would make (DAOLib's dao::attack::Walk: none in reach, else its path
    // to where the approach goes), which is what the gate will charge. Asked
    // here, as often as the path is searched and no oftener: it is a search
    // of its own. (The call gives that path's points too; the line drawn is
    // still the game's path to the target, cut at the walk's length.)
    strike_.reset();
    if (toward != kNoObject)
    {
        if (const std::optional<dao::attack::Walked> walk = dao::attack::Walk(walker, toward))
        {
            strike_ = walk->metres;
        }
    }
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

namespace
{
// Where an order under the cursor would go: to the enemy it would attack,
// or to the ground there if that is ground a move would go to and no object
// is under the cursor. Nothing: no order the preview knows.
std::optional<dao::Vector> WhereTo(ObjectId target, const dao::GameModeExplore *explore, bool overAnObject)
{
    if (target != kNoObject)
    {
        return api::GetPosition(target);
    }
    if (explore != nullptr && !overAnObject && explore->CursorGroundWalkable())
    {
        return explore->CursorGround();
    }
    return std::nullopt;
}

// A creature, and alive: what the cursor may name to the movie, or to an
// attack.
bool LivingCreature(ObjectId object)
{
    return api::GetObjectType(object) == k::OBJECT_TYPE_CREATURE && api::IsDead(object) == 0;
}
} // namespace

// The path marked in the world: gold marks along what can be walked this
// turn, and red ones on from where they end.
void Preview::MarkOnGround(ObjectId area, const PathSplit &split, bool attack)
{
    if (marksArea_ != area)
    {
        // Another area than the marks were placed in: they went with it.
        reachMarks_ = {};
        beyondMarks_ = {};
        marksArea_ = area;
    }
    const float reachEnd = PathLength(split.reach);
    const float beyondEnd = PathLength(split.beyond);
    // Spread, if they would be too many.
    const float every = std::max(kMarkEvery, (reachEnd + beyondEnd) / static_cast<float>(kMostMarks));
    // Short of the marker at a move's end, gold or red; an attack's walk
    // has none.
    const float reachTo = split.TooFar() || attack ? reachEnd : reachEnd - kMarksShortOf;
    const float beyondTo = attack ? beyondEnd : beyondEnd - kMarksShortOf;
    Lay(reachMarks_, kGoldRing, AlongPath(split.reach, every, kMarksFrom, reachTo));
    // The red ones go on in step from where the gold ones end.
    const float into = every - std::fmod(std::max(0.0f, reachEnd - kMarksFrom), every);
    Lay(beyondMarks_, kRedRing, split.TooFar() ? AlongPath(split.beyond, every, into, beyondTo) : std::vector<Vec3>{});
}

// The walk drawn: the plate's numbers, the line on the screen or the marks
// on the ground, the game's marker where a move would end, and all of it
// for the movie. `walk` is the order's length as the gate will measure it,
// `walked` the path it is drawn along.
void Preview::Draw(const TurnView &view, std::uint32_t nowMs, float walk, const std::vector<Vec3> &walked, bool attack)
{
    const PathSplit split = SplitPath(walked, view.movementLeft + (PathLength(walked) - walk));
    PreviewView shown;
    if (const std::optional<dao::world::ViewSize> size = dao::world::ScreenSize())
    {
        shown.width = size->width;
        shown.height = size->height;
    }
    shown.length = walk;
    shown.meters = view.movementLeft;
    shown.over = over_;
    // With a turn a bar of time, what the order would cost: a move its walk,
    // an attack its walk and its swing; nothing for one too far, which is
    // refused.
    if (!split.TooFar())
    {
        const OrderCost cost = CostOf(view, walk, attack ? AttackSeconds(view.current) : 0.0f);
        shown.cost = cost.seconds;
        shown.spills = cost.spills;
    }
    // The drawn line leaves out its first stretch, inside the walker's ring.
    const PathSplit outside = SplitPath(split.reach, kLineFrom);
    const std::vector<Vec3> &lineReach = outside.TooFar() ? outside.beyond : split.reach;
    shown.reach = OnScreen(lineReach);
    // Where the move would go, the game's marker; past the movement left
    // the red one, since the gold says "it will walk here" and it would
    // not. An attack's walk ends at its target, which has its own ring.
    const bool art = art_.value_or(false);
    if (attack || (split.TooFar() && !art))
    {
        HideMarker();
    }
    else
    {
        ShowMarker(split.TooFar() ? kRedMarker : kMoveMarker, walked.back());
    }
    // The path in the world: marks along it, in the marker's area (which
    // ShowMarker keeps; with no marker placed the area is asked here).
    // Without the mod's rings the path is the movie's line, which is whole
    // by itself, gold and red.
    const dao::CCArea *area = dao::world::CurrentArea();
    shown.line = !art || area == nullptr;
    if (shown.line)
    {
        HideMarks();
    }
    else
    {
        MarkOnGround(area->Id(), split, attack);
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
        if (shown.line && !attack && shown.beyond.size() == split.beyond.size() && marker_ == kNoObject)
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

// Nowhere to walk to, or a creature not at rest: nothing of a walk is
// shown. Over a friend, or an enemy with no action left to attack it with,
// who it is is said all the same; over nothing, all of it is cleared.
void Preview::NoWalk()
{
    if (over_ != kNoObject)
    {
        searched_ = false;
        path_.clear();
        ShowCost(OrderCost{});
        return;
    }
    Clear();
}

// No path there as the game finds it: nothing is promised, so nothing is
// drawn.
void Preview::NoPath()
{
    HideMarker();
    HideMarks();
    if (std::exchange(drawn_, false))
    {
        const std::lock_guard lock(mutex_);
        encoded_ = EncodeNoPreview();
    }
}

void Preview::Update(const TurnView &view, std::uint32_t nowMs)
{
    if (!installed_)
    {
        return;
    }
    if (loaded_.exchange(false))
    {
        reachMarks_ = {};
        beyondMarks_ = {};
        marker_ = kNoObject;
    }
    if (!view.active)
    {
        RemoveMarks(); // nothing of the mod's is left in the world after a fight
    }
    // Once there is an area, so a game loaded and its tables read.
    if (!art_.has_value() && dao::world::CurrentArea() != nullptr)
    {
        art_ = FindArt();
    }
    Compare(nowMs);
    // With less than a step left a move is held whatever its length
    // ("no movement left"), and a red line to everywhere says nothing: in
    // play it read "8.4 m of 0.0" wherever the cursor lay (the seventeenth
    // play test).
    const bool waiting = view.active && view.playerTurn && view.phase == Phase::AwaitingOrder &&
                         view.current != kNoObject && view.movementLeft >= TurnMachine::kMinStep;
    const dao::GameModeExplore *explore = waiting ? ExploreMode() : nullptr;
    const std::optional<ObjectId> object = explore != nullptr ? explore->CursorObject() : std::nullopt;
    // Any living creature under the cursor is named to the movie, which says
    // when an action it has pending lands (dev/REBALANCE.md, "The bar on
    // screen": "a pending action on hovering over its creature or its card").
    const bool creature = object && LivingCreature(*object);
    over_ = creature ? *object : kNoObject;
    // Over an enemy, with an action left: the walk an attack on it would
    // take (the user's first list: "the same for an attack: gold if the
    // target can be reached this turn, red if not, before the player
    // clicks"). Over anything else that is an object (a door, a chest, a
    // friend) the click is no walk the preview knows, and nothing is drawn.
    const ObjectId target =
        creature && view.timeLeftMs > 0 && api::IsObjectHostile(view.current, *object) != 0 ? *object : kNoObject;
    // Otherwise the ground, and ground a move would go to: a click on
    // rubble is not an order at all (the engine's own test, which its move
    // click makes), and a path drawn to it promised one. In play a red end
    // there, clicked, was not refused "Too far": no command came (the
    // fifteenth play test, 2026-10-01).
    // An ability being aimed (a quickbar talent pressed, the targeting
    // cursor up): what it would cost, on the bar and in the caption, and no
    // walk drawn: its range is its own, not the attack's reach. Quoted the
    // attack's price before (play test 27: Dirty Fighting shown 1.5 s,
    // charged 2.0 s). DAOLib's GameModeExplore::Targeting, first used here.
    if (const std::optional<dao::GameModeExplore::Aim> aim = waiting ? dao::GameModeExplore::Targeting() : std::nullopt;
        aim && aim->caster == view.current)
    {
        searched_ = false;
        path_.clear();
        ShowCost(CostOf(view, 0.0f, AbilitySecondsOf(view.current, aim->ability)));
        return;
    }
    const std::optional<dao::Vector> point = WhereTo(target, explore, object.has_value());
    if (!point || !AtRest(view.current))
    {
        NoWalk();
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
        NoPath();
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
    // the attacker can strike, which is the engine's to say and the gate's
    // own number (WalkToTarget: nothing with the target in its reach; else
    // the engine's path to where its approach goes). Where the engine has no
    // answer (no creature, no path), the whole way is shown.
    float walk = order;
    std::vector<Vec3> walked = path_;
    if (target != kNoObject)
    {
        walk = strike_.value_or(order);
        walked = SplitPath(path_, walk + (drawn - order)).reach;
        // Under half a meter is the shuffle of a strike, not a walk: a plate
        // "0.1 m, 1.5 s" lay at Lyna's feet with the spider beside her (seen
        // in play, 2026-10-02).
        if (walk < kNoWalk || walked.size() < 2)
        {
            // In reach where it stands: no walk to show; with a turn a bar
            // of time, what the swing would cost.
            ShowCost(CostOf(view, walk, AttackSeconds(view.current)));
            return;
        }
    }
    Draw(view, nowMs, walk, walked, target != kNoObject);
}

} // namespace tb::game
