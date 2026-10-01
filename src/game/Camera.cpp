#include "game/Camera.h"

#include "dao/Camera.h"
#include "dao/CameraMoves.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Log.h"

namespace tb::game
{

namespace
{
// Most of the way in a few tenths of a second, settled by then.
constexpr float kMoveSeconds = 0.6f;
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
    dao::camera::PanTo(id, kMoveSeconds);
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

} // namespace tb::game
