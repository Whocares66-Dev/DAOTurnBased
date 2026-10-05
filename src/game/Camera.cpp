#include "game/Camera.h"

#include "core/Portraits.h"
#include "dao/Camera.h"
#include "dao/CameraMoves.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/Portraits.h"
#include "dao/World.h"
#include "dao/generated/ScriptApi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

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
} // namespace

void CameraDirector::TurnTo(ObjectId id)
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
    Pan(id, nowMs);
    again_ = false;
    spdlog::debug("camera to {:#010x}: following {:#010x}{}", id, camera->Target(),
                  camera->Tactical() ? ", tactical" : "");
    if (camera->Tactical())
    {
        return;
    }
    StandBackFor(id);
    const std::optional<float> behind = dao::camera::YawBehind(id);
    if (!behind)
    {
        return;
    }
    // An enemy is looked at from its right, or its left where the right is
    // blocked (core: ChooseViewSide; the user, 2026-10-05); the party from
    // behind. Blocked or not is the engine's own line from where the camera
    // would stand to the creature (DAOLib's dao::world::LineOfSight between
    // two points, the creature itself not counted).
    dao::SceneCamera *scene = camera->Scene();
    float yaw = *behind;
    std::string_view from = "behind";
    const bool party = dao::script::api::IsHero(id) != 0 || dao::script::api::IsFollower(id) != 0;
    if (!party && scene != nullptr)
    {
        const dao::Vector feet = dao::script::api::GetPosition(id);
        const dao::Vector at = dao::portraits::HeadPosition(id).value_or(dao::Vector{feet.x, feet.y, feet.z + 1.5f});
        // Where the orbit puts the camera (dao/CameraMoves.h): the zoom's
        // blend of the close distance and the game's far one, about the
        // focus, by the yaw and the pitch from straight above.
        constexpr float kFar = 20.0f;
        const float zoom = scene->Zoom();
        const float radius = zoom * dao::camera::CloseDistance() + (1.0f - zoom) * kFar;
        const float pitch = scene->Pitch();
        const auto clear = [&](ViewSide side) {
            const float angle = ViewYaw(*behind, side);
            const dao::Vector eye{feet.x + radius * std::sin(pitch) * std::cos(angle),
                                  feet.y + radius * std::sin(pitch) * std::sin(angle),
                                  feet.z + radius * std::cos(pitch)};
            return static_cast<bool>(dao::world::LineOfSight(eye, at, id));
        };
        const ViewSide side = ChooseViewSide(clear(ViewSide::Right), clear(ViewSide::Left));
        yaw = ViewYaw(*behind, side);
        from = side == ViewSide::Right  ? "its right"
               : side == ViewSide::Left ? "its left, the right being blocked"
                                        : "behind, both sides being blocked";
    }
    dao::camera::EaseYaw(yaw, MoveSeconds());
    if (scene != nullptr && (logged_ < 3 || !party))
    {
        ++logged_;
        spdlog::debug("the camera comes round {:#010x} from {}: yaw {:.2f} (it was {:.2f})", id, from, yaw,
                      scene->Yaw());
    }
}

// How far the camera stands by whose turn it is (core: CloseDistanceFor): a
// creature bigger than a person has it further back by its size, so that it
// does not fill the frame (an ogre's back hid Lyna and the "Spell lost" over
// her, play test 33; the high dragon's turns showed the inside of the dragon,
// issue 33); a person's, the party's above all, has the game's own. The zoom
// is the player's throughout and is not touched: the distance is apart from
// it (DAOLib's dao::camera::SetCloseDistance), so the tactical view, which
// the game flips to by the zoom, never comes of this.
//
// The size is the engine's: from the creature's feet to its head, the node
// the game's own portrait camera aims at (MeasureBody, below; core's
// ExtentOf has the two readings of that point and why the shorter is taken).
// A model with no node of that name answers nothing, and then it is the
// appearance table's personal space against a person's.
void CameraDirector::StandBackFor(ObjectId id)
{
    namespace api = dao::script::api;
    const bool party = api::IsHero(id) != 0 || api::IsFollower(id) != 0;
    std::optional<float> close;
    float extent = 0.0f;
    std::string_view by = "one of the party";
    if (!party)
    {
        const BodyMeasure body = MeasureBody(id);
        if (body.head)
        {
            extent = body.extent.meters;
            by = body.extent.world ? "its head, a point in the world" : "its head, a point about its own origin";
        }
        else
        {
            const float space =
                dao::gda::GetFloat(dao::script::Table::Appearance, api::GetAppearanceType(id), "PerSpace")
                    .value_or(kPortraitPersonSpace);
            extent = kPersonExtent * space / kPortraitPersonSpace;
            by = "its personal space: its model has no head node";
        }
        close = CloseDistanceFor(extent);
    }
    if (close == standing_)
    {
        return;
    }
    standing_ = close;
    dao::camera::SetCloseDistance(close);
    spdlog::info("the camera's distance for {:#010x}: {} ({:.1f} m from feet to head, by {})", id,
                 close ? std::format("{:.1f} m", *close) : std::string{"the game's own"}, extent, by);
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
