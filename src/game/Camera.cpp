#include "game/Camera.h"

#include "core/Portraits.h"
#include "core/Text.h"
#include "dao/Camera.h"
#include "dao/CameraMoves.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/Portraits.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tb::game
{

namespace
{
// Most of the way in a few tenths of a second, settled by then.
constexpr float kMoveSeconds = 0.6f;
// Within this of the creature the camera is on it; a pan stopped short
// sooner than this after it began was not stopped by the player.
constexpr float kArrivedMeters = 3.0f;
constexpr std::uint64_t kTooSoonMs = 200;

std::uint64_t NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// The collision groups the camera's lines are asked of (DAOLib's
// dao/World.h: a shape is met when its groups share a bit with the mask):
// the ground's two, 0x8 and 0x20, and placeables, 0x4.
//
// The lines were first asked of the area's line of sight
// (dao::world::LineOfSight), which is made for creatures seeing creatures:
// its mask is the engine's 0x24, placeables and group 0x20, so a shape in
// group 0x8 alone is not there for it. At the Broodmother (commit aa280db,
// played 2026-10-05): "the camera comes round 0x00033f0d from its right at
// 14.0 m of the 14.0 m its size asks (its right clear to 14.0 m, its left to
// 14.0 m, behind to 14.0 m; zoom 1.00)", and the frame was taken from inside
// the cave's rock. All three lines clear, and none was. DAOLib's session read
// why, and that a level's rock may be group 0x8 alone or have no collision at
// all, which only play would tell: so the line says what cut it (WhatCut).
//
// Play told (commit 96d6445, 2026-10-05, which for that build also asked
// each line of every group but creatures' bodies and pick volumes where
// these groups met nothing):
// - in a built hall the measure works. A lone ogre's four turns: "from its
//   left at 10.0 m of the 10.0 m its size asks (its right clear to 3.2 m,
//   its left to 10.0 m, behind to 5.7 m; ...)", "from behind at 10.0 m ...
//   (its right clear to 8.8 m, its left to 2.9 m, behind to 10.0 m ...)",
//   "from behind ... (its right clear to 4.4 m, its left to 6.8 m, behind to
//   10.0 m ...)", "from its right ... (all three 10.0 m ...)"; the frame of
//   the third is the ogre from behind with the hall and the party member
//   before it, no wall in the way;
// - the Broodmother's cave has no collision in any group. Nine of her turns
//   read "its right clear to 14.0 m, its left to 14.0 m, behind to 14.0 m",
//   every group asked as well and nothing met, and the frame was from inside
//   the rock as before. No ray meets it: a creature set into scenery is a
//   case the line cannot see, and it is open (issue 33).
constexpr std::uint32_t kViewGroups = dao::world::groups::kGround | dao::world::groups::kPlaceables;
static_assert(kViewGroups == 0x2C);

Vec3 Of(const dao::Vector &point)
{
    return Vec3{point.x, point.y, point.z};
}

// The nearest shape between two points among `groups`, the creature's own
// passed over: DAOLib's dao::world::FirstHit, which has nobody to pass over
// where the line of sight had its `ignore`. A creature's body is group 0x10
// and its pick volume 0x20000, neither of them the ground's or a
// placeable's, so its own shape should not be the first met. Where it is all
// the same, the ray's whole list is asked (Raycast, nearest first) and cut
// at `to` as FirstHit cuts it. Other creatures are not passed over, as the
// line of sight did not pass them.
std::optional<dao::world::Hit> FirstCut(const dao::Vector &from, const dao::Vector &to, std::uint32_t groups,
                                        ObjectId self)
{
    std::optional<dao::world::Hit> first = dao::world::FirstHit(from, to, groups);
    if (!first || first->Object() != self)
    {
        return first;
    }
    const float whole = Distance(Of(from), Of(to));
    for (const dao::world::Hit &hit :
         dao::world::Raycast(from, dao::Vector{to.x - from.x, to.y - from.y, to.z - from.z}, groups))
    {
        if (Distance(Of(from), Of(hit.position)) > whole)
        {
            break;
        }
        if (hit.Object() != self)
        {
            return hit;
        }
    }
    return std::nullopt;
}

// What a line met, for the log: the shape's groups, and what the library
// says the shape is of.
std::string WhatCut(const dao::world::Hit &hit)
{
    const std::string groups = std::format("group {:#x}", hit.groups);
    if (hit.IsGround())
    {
        return groups + (hit.walkable ? " of walkable ground" : " of ground that is not walkable");
    }
    if (const std::optional<dao::ObjectId> object = hit.Object())
    {
        return std::format("{} of object {:#010x} \"{}\"", groups, *object, Utf8(dao::script::api::GetTag(*object)));
    }
    return groups + " of a shape neither ground nor an object's";
}

// What lies under a point: the first thing the engine's ray straight down
// from it meets among the ground's groups (DAOLib's dao::world::Raycast, to
// any distance, nearest first), and how far under the creature's feet that
// is. One raycast a place: three a turn of a creature not of the party
// beside its three lines, and three more with three more lines where the
// places in front of it are asked. A ray does not meet a mesh from inside it
// (DAOLib's session), so a point inside solid ground or under a hillside
// reads as no ground, or as what lies below it.
GroundUnder GroundBelow(const dao::Vector &point, const dao::Vector &feet)
{
    const std::vector<dao::world::Hit> hits =
        dao::world::Raycast(point, dao::Vector{0.0f, 0.0f, -1.0f}, dao::world::groups::kGround);
    if (hits.empty())
    {
        return GroundUnder{false, false, 0.0f};
    }
    const dao::world::Hit &first = hits.front();
    return GroundUnder{true, first.IsGround() && first.walkable, feet.z - first.position.z};
}

// What lies under a place, for the log, so that one look at a turn's line
// says what a ray down makes of the inside of scenery with no collision (the
// Broodmother's rock): no ground, ground nobody could walk on, or walkable
// ground and how far under the creature's feet, which past kPlaceDrop is the
// floor of somewhere else.
std::string GroundWords(const GroundUnder &under)
{
    if (!under.ground)
    {
        return "over no ground";
    }
    const std::string where = under.below < 0.0f ? std::format("{:.1f} m above its feet", -under.below)
                                                 : std::format("{:.1f} m below its feet", under.below);
    if (!under.walkable)
    {
        return "over ground that is not walkable " + where;
    }
    return "over walkable ground " + where + (IsPlace(under) ? "" : ", too far below");
}
} // namespace

void CameraDirector::TurnTo(ObjectId id, ObjectId own)
{
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    dao::CameraController *camera = modes != nullptr ? modes->Camera() : nullptr;
    if (camera == nullptr)
    {
        return;
    }
    // A move still running (the last turn's pan or swing) is stopped first:
    // once, the camera stayed on the archers' room for Eleanor's turn (the
    // second play test, 2026-09-30, 17:18:32). What it follows after is said,
    // to see whether the pan took.
    if (dao::camera::Moving())
    {
        dao::camera::Stop();
    }
    const std::uint64_t nowMs = NowMs();
    if (panning_ != kNoObject && panning_ != id)
    {
        // The turn was over before the camera was there: said, since no
        // other line would (two of 38 pans in the sixth play test).
        spdlog::debug("the camera's pan to {:#010x} was {} ms on its way when the next began", panning_,
                      nowMs - panAt_);
    }
    again_ = false;
    // In the tactical view the camera glides to whoever acts, and that is
    // all of it.
    if (camera->Tactical())
    {
        Pan(id, nowMs);
        spdlog::debug("camera to {:#010x}: following {:#010x}, tactical", id, camera->Target());
        return;
    }
    // Close in, where the camera goes is settled before it moves: to the
    // creature whose turn it is, by a yaw and at a distance; or, with no
    // place round an enemy for it to stand, not to the creature at all.
    // What a creature's size asks is said either way, and a creature with no
    // orientation is followed at that distance, the camera left as it is
    // turned.
    const std::optional<float> asked = SizeAsks(id);
    const std::optional<float> behind = dao::camera::YawBehind(id);
    dao::SceneCamera *scene = camera->Scene();
    const bool party = dao::script::api::IsHero(id) != 0 || dao::script::api::IsFollower(id) != 0;
    ObjectId on = id;
    std::optional<float> close = asked;
    std::optional<float> yaw = behind;
    std::string from = "behind";
    bool stays = false;
    // An enemy is looked at from one of six places, three behind it and
    // three in front, and from no further than the camera's line to it is
    // clear (core: ChooseStand, with the rule, the user's words for it and
    // what was seen without it); the party from behind, at the game's own
    // distance. Two things are asked of the engine for a place:
    // - how far its line is clear: what a line from the creature's head to
    //   where the camera would stand at the whole distance first meets
    //   (FirstCut, of kViewGroups, above). Anything of those groups cuts a
    //   line, a placeable that blocks no creature's sight too. Where the
    //   engine has no answer (no area, another thread than the game's: not
    //   where a turn begins) a line reads as clear, there being no telling
    //   that from nothing in the way;
    // - what lies under where the camera would then stand (GroundBelow,
    //   above), since scenery with no collision cuts no line: the
    //   Broodmother's cave.
    // One line and one ray down a place, with no stepping along either: six
    // raycasts a turn of a creature not of the party, and twelve on a turn
    // where no place behind it stands and the three in front are asked.
    if (!party && scene != nullptr && behind)
    {
        const dao::Vector feet = dao::script::api::GetPosition(id);
        const dao::Vector head = dao::portraits::HeadPosition(id).value_or(dao::Vector{feet.x, feet.y, feet.z + 1.5f});
        // Where the orbit puts the camera (dao/CameraMoves.h): at its
        // distance (core: OrbitDistance) about the focus, by the yaw and the
        // pitch from straight above.
        const float zoom = scene->Zoom();
        const float pitch = scene->Pitch();
        const float wanted = OrbitDistance(zoom, asked.value_or(kCloseGame));
        struct Measure
        {
            std::string_view name;
            std::optional<dao::world::Hit> cut;
            float clear;
            GroundUnder under;
        };
        const auto measure = [&](ViewSide side, std::string_view name) {
            const float angle = ViewYaw(*behind, side);
            const auto at = [&](float meters) {
                return dao::Vector{feet.x + meters * std::sin(pitch) * std::cos(angle),
                                   feet.y + meters * std::sin(pitch) * std::sin(angle),
                                   feet.z + meters * std::cos(pitch)};
            };
            const dao::Vector eye = at(wanted);
            const std::optional<dao::world::Hit> cut = FirstCut(head, eye, kViewGroups, id);
            const float clear = cut ? ClearDistance(Of(head), Of(eye), Of(cut->position), wanted) : wanted;
            return Measure{name, cut, clear, GroundBelow(at(StandDistance(zoom, asked, clear)), feet)};
        };
        using Three = std::array<Measure, 3>;
        const auto rowOf = [](const Three &three) {
            return ViewRow{ViewPlace{three[0].clear, IsPlace(three[0].under)},
                           ViewPlace{three[1].clear, IsPlace(three[1].under)},
                           ViewPlace{three[2].clear, IsPlace(three[2].under)}};
        };
        const Three back{measure(ViewSide::Right, "its right"), measure(ViewSide::Left, "its left"),
                         measure(ViewSide::Behind, "behind")};
        ViewStand stand = ChooseStand(zoom, asked, rowOf(back), std::nullopt);
        std::optional<Three> front;
        if (stand.answer == ViewAnswer::AskFront)
        {
            front = Three{measure(ViewSide::FrontRight, "its front right"),
                          measure(ViewSide::FrontLeft, "its front left"), measure(ViewSide::Front, "in front")};
            stand = ChooseStand(zoom, asked, rowOf(back), rowOf(*front));
        }
        // What was found, for the log: each place's line and the ground
        // under it, the zoom, and what cut each line that was cut. By the
        // cuts play told a hall's walls from a cave with no collision
        // (kViewGroups, above); by the ground it is to tell what a ray down
        // makes of the inside of such a cave.
        const auto said = [](const Three &three) {
            return std::format("{} clear to {:.1f} m {}, {} to {:.1f} m {}, {} to {:.1f} m {}", three[0].name,
                               three[0].clear, GroundWords(three[0].under), three[1].name, three[1].clear,
                               GroundWords(three[1].under), three[2].name, three[2].clear, GroundWords(three[2].under));
        };
        std::string found = said(back);
        if (front)
        {
            found += "; " + said(*front);
        }
        found += std::format("; zoom {:.2f}", zoom);
        const auto cuts = [&](const Three &three) {
            for (const Measure &place : three)
            {
                if (place.cut)
                {
                    found += std::format("; {} cut by {}", place.name, WhatCut(*place.cut));
                }
            }
        };
        cuts(back);
        if (front)
        {
            cuts(*front);
        }
        if (stand.answer == ViewAnswer::Stand)
        {
            close = stand.close;
            yaw = ViewYaw(*behind, stand.side);
            from = std::format("{} at {:.1f} m of the {:.1f} m its size asks ({})", ViewSideWords(stand.side),
                               stand.meters, wanted, found);
        }
        else
        {
            // No place round it for the camera to stand: the camera does not
            // go to it. It stays with the party, on the one the player acts
            // as and at the game's own distance, which is where the lines
            // below put it on a party member's turn, and turns to keep the
            // creature in the frame (the user, 2026-10-05; core's ChooseStand
            // has the words). With nobody of the party to stay with, it is
            // left where it is.
            stays = true;
            const bool with = own != kNoObject && dao::script::api::IsObjectValid(own) != 0;
            on = with ? own : kNoObject;
            close = std::nullopt;
            yaw = with ? YawToward(Of(dao::script::api::GetPosition(own)), Of(feet)) : std::nullopt;
            spdlog::debug("the camera stays with the party on {:#010x}'s turn and looks at it: no place round it to "
                          "stand ({})",
                          id, found);
        }
    }
    if (on != kNoObject)
    {
        Pan(on, nowMs);
        spdlog::debug("camera to {:#010x}: following {:#010x}", on, camera->Target());
    }
    Stand(close);
    if (!yaw)
    {
        return;
    }
    dao::camera::EaseYaw(*yaw, MoveSeconds());
    if (scene == nullptr || (logged_ >= 3 && party))
    {
        return;
    }
    ++logged_;
    if (stays)
    {
        spdlog::debug("the camera, with {:#010x}, turns to look at {:#010x}: yaw {:.2f} (it was {:.2f})", on, id, *yaw,
                      scene->Yaw());
        return;
    }
    spdlog::debug("the camera comes round {:#010x} from {}: yaw {:.2f} (it was {:.2f})", id, from, *yaw, scene->Yaw());
}

// How far its size asks the camera to stand on a creature's turn (core:
// CloseDistanceOf): a creature bigger than a person has it further back by
// its size, so that it does not fill the frame (an ogre's back hid Lyna and
// the "Spell lost" over her, play test 33; the high dragon's turns showed
// the inside of the dragon, and a giant spider's its abdomen, issue 33); a
// person's, the party's above all, has the game's own, which is nothing
// here. Where it then stands is TurnTo's, by what its lines are clear to.
// The zoom is the player's throughout and is not touched: the distance is
// apart from it (DAOLib's dao::camera::SetCloseDistance), so the tactical
// view, which the game flips to by the zoom, never comes of this.
//
// The size is the engine's, and core says which of its measures counts: the
// model's own box as the renderer keeps it (DAOLib's dao::world::ModelBoxOf,
// its issue 21), by its length or its height, whichever is more of a
// person's; for a model that gives none, from the creature's feet to its
// head, the node the game's own portrait camera aims at (MeasureBody, below;
// core's ExtentOf has the two readings of that point and why the shorter is
// taken); with neither, the game's own. Until 2026-10-05 a model with no
// head node went by the appearance table's personal space, which the box has
// taken the place of.
std::optional<float> CameraDirector::SizeAsks(ObjectId id)
{
    namespace api = dao::script::api;
    const bool party = api::IsHero(id) != 0 || api::IsFollower(id) != 0;
    CloseDistance close{std::nullopt, CloseBy::Nothing};
    std::string by = "one of the party";
    if (!party)
    {
        std::optional<Vec3> sides;
        std::string box = "its model gave no box";
        if (const std::optional<dao::world::ModelBox> model = dao::world::ModelBoxOf(id))
        {
            const dao::Vector size = model->Size();
            sides = Vec3{size.x, size.y, size.z};
            box = std::format("its model's box {:.2f} by {:.2f} and {:.2f} high", size.x, size.y, size.z);
        }
        const BodyMeasure body = MeasureBody(id);
        close = CloseDistanceOf(sides, body.head ? std::optional<float>{body.extent.meters} : std::nullopt);
        // A box there is that the distance is not by is one core took for
        // no measure.
        const std::string_view none = sides ? " is no measure" : "";
        switch (close.by)
        {
        case CloseBy::Length:
            by = box + "; by its length against a person's across";
            break;
        case CloseBy::Height:
            by = box + "; by its height against a person's height";
            break;
        case CloseBy::Head:
            by = std::format("{:.1f} m from feet to head, by its head, a point {}; {}{}", body.extent.meters,
                             body.extent.world ? "in the world" : "about its own origin", box, none);
            break;
        case CloseBy::Nothing:
            by = std::format("{}{}, and it has no head node", box, none);
            break;
        }
    }
    // Said when it changes, as it was when what the size asks was where the
    // camera stood: the line a play test looks for.
    if (close.meters != asked_)
    {
        asked_ = close.meters;
        spdlog::info("the camera's distance for {:#010x}: {} ({})", id,
                     close.meters ? std::format("{:.1f} m", *close.meters) : std::string{"the game's own"}, by);
    }
    return close.meters;
}

void CameraDirector::Stand(std::optional<float> close)
{
    if (close == standing_)
    {
        return;
    }
    standing_ = close;
    dao::camera::SetCloseDistance(close);
}

BodyMeasure MeasureBody(ObjectId id)
{
    BodyMeasure body;
    const dao::Vector feet = dao::script::api::GetPosition(id);
    body.feet = Vec3{feet.x, feet.y, feet.z};
    if (const std::optional<dao::Vector> head = dao::portraits::HeadPosition(id))
    {
        body.head = Vec3{head->x, head->y, head->z};
        body.extent = ExtentOf(*body.head, body.feet);
    }
    return body;
}

void CameraDirector::Release()
{
    asked_.reset();
    if (standing_)
    {
        standing_.reset();
        dao::camera::SetCloseDistance(std::nullopt);
        spdlog::debug("the camera's distance is the game's own again");
    }
}

float CameraDirector::MoveSeconds() const
{
    return kMoveSeconds / std::max(1.0f, speed_);
}

void CameraDirector::Pan(ObjectId id, std::uint64_t nowMs)
{
    dao::camera::PanTo(id, MoveSeconds());
    panning_ = id;
    panAt_ = nowMs;
}

void CameraDirector::Watch()
{
    if (panning_ == kNoObject || dao::camera::Moving())
    {
        return;
    }
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    dao::CameraController *camera = modes != nullptr ? modes->Camera() : nullptr;
    const ObjectId id = panning_;
    panning_ = kNoObject;
    if (camera == nullptr)
    {
        return;
    }
    // On the ground: the tactical view's focus keeps its own height.
    const dao::Vector focus = camera->Focus();
    const dao::Vector at = dao::script::api::GetPosition(id);
    const float shortBy = std::hypot(focus.x - at.x, focus.y - at.y);
    const std::uint64_t nowMs = NowMs();
    const std::uint64_t ran = nowMs - panAt_;
    // Why, by DAOLib's word (79d2baa): the player is seen by the game's own
    // pan, not guessed from the camera jumping.
    using dao::camera::MoveEnd;
    const std::optional<MoveEnd> why = dao::camera::LastEnd(dao::camera::Move::Pan);
    const std::string_view said = why ? dao::camera::Name(*why) : std::string_view{"?"};
    // DAOLib's "arrived" is the pan's own word that the focus is on the
    // creature. The distance measured here is the controller's focus from
    // the creature's feet, which in the user's play stood 3.1 m apart with
    // the pan arrived: a warning and a second pan for nothing (23:41:31).
    if (why == MoveEnd::Arrived || shortBy <= kArrivedMeters)
    {
        spdlog::debug("camera on {:#010x} after {} ms ({}, {:.1f} m from it)", id, ran, said, shortBy);
        return;
    }
    // Short of it and not the player's doing: once more. Another selection
    // (a party AI's turn selects the hero) is taken as the mod's own only
    // this soon after the pan began, since later it may be the player's.
    const bool theirs =
        why == MoveEnd::Player || why == MoveEnd::NoCamera || why == MoveEnd::Replaced || why == MoveEnd::Stopped;
    if (!theirs && !again_ && (why != MoveEnd::Retargeted || ran < kTooSoonMs))
    {
        spdlog::warn("the camera's pan to {:#010x} ended after {} ms, {:.1f} m short ({}): panned again", id, ran,
                     shortBy, said);
        again_ = true;
        Pan(id, nowMs);
        return;
    }
    spdlog::info("the camera's pan to {:#010x} ended after {} ms, {:.1f} m short ({}, following {:#010x}){}", id, ran,
                 shortBy, said, camera->Target(), again_ ? ", the second time" : "");
}

} // namespace tb::game
