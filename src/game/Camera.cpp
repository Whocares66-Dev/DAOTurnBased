#include "game/Camera.h"

#include "core/Portraits.h"
#include "dao/Camera.h"
#include "dao/CameraMoves.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/generated/ScriptApi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
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
    ZoomFor(id, *camera);
    const std::optional<float> behind = dao::camera::YawBehind(id);
    if (!behind)
    {
        return;
    }
    dao::camera::EaseYaw(*behind, MoveSeconds());
    if (dao::SceneCamera *scene = camera->Scene(); scene != nullptr && logged_ < 3)
    {
        ++logged_;
        spdlog::info("the camera comes round behind {:#010x}: yaw {:.2f} (it was {:.2f})", id, *behind, scene->Yaw());
    }
}

// The zoom by whose turn it is (core: FollowZoom): a creature bigger than a
// person pulls the camera back by its size, so that a big creature does not
// fill the frame (an ogre's back hid Lyna and the "Spell lost" over her, play
// test 33); a person's, the party's above all, has the player's own zoom,
// given back as it was unless the player zoomed meanwhile (DAOLib 752c52d's
// EaseZoom and PlayerZoomed: the player wins).
void CameraDirector::ZoomFor(ObjectId id, dao::CameraController &camera)
{
    dao::SceneCamera *scene = camera.Scene();
    if (scene == nullptr)
    {
        return;
    }
    const float now = scene->Zoom();
    if (!playerZoom_ || dao::camera::PlayerZoomed())
    {
        playerZoom_ = now; // the player's own, as they left it
    }
    const std::int32_t appearance = dao::script::api::GetAppearanceType(id);
    const float space =
        dao::gda::GetFloat(dao::script::Table::Appearance, appearance, "PerSpace").value_or(kPortraitPersonSpace);
    const bool party = dao::script::api::IsHero(id) != 0 || dao::script::api::IsFollower(id) != 0;
    const std::optional<float> back = party ? std::nullopt : FollowZoom(*playerZoom_, space);
    const float want = back.value_or(*playerZoom_);
    if (std::abs(want - now) < 0.01f)
    {
        return;
    }
    dao::camera::EaseZoom(want, MoveSeconds());
    spdlog::info("the camera's zoom for {:#010x} (personal space {:.2f}): {:.2f}, from {:.2f}{}", id, space, want, now,
                 back ? "" : " (the player's own)");
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
