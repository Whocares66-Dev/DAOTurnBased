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
    dao::camera::PanTo(id, kMoveSeconds);
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
