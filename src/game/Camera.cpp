#include "game/Camera.h"

#include "dao/Camera.h"
#include "dao/CameraMoves.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/generated/ScriptApi.h"

#include <chrono>
#include <cmath>

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
    Pan(id, NowMs());
    again_ = false;
    spdlog::debug("camera to {:#010x}: following {:#010x}{}", id, camera->Target(),
                  camera->Tactical() ? ", tactical" : "");
    if (camera->Tactical())
    {
        return;
    }
    const std::optional<float> behind = dao::camera::YawBehind(id);
    if (!behind)
    {
        return;
    }
    dao::camera::EaseYaw(*behind, kMoveSeconds);
    if (dao::SceneCamera *scene = camera->Scene(); scene != nullptr && logged_ < 3)
    {
        ++logged_;
        spdlog::info("the camera comes round behind {:#010x}: yaw {:.2f} (it was {:.2f})", id, *behind, scene->Yaw());
    }
}

void CameraDirector::Pan(ObjectId id, std::uint64_t nowMs)
{
    dao::camera::PanTo(id, kMoveSeconds);
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
    if (shortBy <= kArrivedMeters)
    {
        spdlog::debug("camera on {:#010x} after {} ms", id, ran);
        return;
    }
    if (ran < kTooSoonMs && !again_)
    {
        spdlog::warn("the camera's pan to {:#010x} stopped after {} ms, {:.1f} m short: panned again", id, ran,
                     shortBy);
        again_ = true;
        Pan(id, nowMs);
        return;
    }
    spdlog::info("the camera's pan to {:#010x} ended after {} ms, {:.1f} m short (following {:#010x}){}", id, ran,
                 shortBy, camera->Target(), again_ ? ", the second time" : ": the player's, taken as");
}

} // namespace tb::game
