#include "game/Camera.h"

#include "dao/Camera.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"

namespace tb::game
{

bool FollowWithCamera(ObjectId id)
{
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    dao::CameraController *camera = modes != nullptr ? modes->Camera() : nullptr;
    if (camera == nullptr)
    {
        return false;
    }
    if (camera->Target() != id)
    {
        camera->Follow(id);
    }
    return true;
}

} // namespace tb::game
