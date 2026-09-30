#include "game/Camera.h"

#include "core/Easing.h"

#include "dao/Camera.h"
#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/generated/ScriptApi.h"

#include <cmath>

namespace tb::game
{

namespace
{

// Half-lives: most of the way in a few tenths of a second.
constexpr float kPanHalfLife = 0.18f;
constexpr float kTurnHalfLife = 0.15f;
constexpr float kGiveUpSeconds = 2.0f;

dao::CameraController *Camera()
{
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    return modes != nullptr ? modes->Camera() : nullptr;
}

} // namespace

void CameraDirector::TurnTo(ObjectId id, bool party)
{
    dao::CameraController *camera = Camera();
    if (camera == nullptr)
    {
        return;
    }
    easingSeconds_ = 0.0f;
    if (camera->Tactical())
    {
        if (camera->Target() != id)
        {
            camera->Follow(id);
        }
        panning_ = true;
        behind_.reset();
        return;
    }
    panning_ = false;
    behind_.reset();
    if (!party)
    {
        // Close in, the camera stays with the player's character and turns
        // to watch the enemy's turn from behind it: following the enemy put
        // the camera 3.8 m behind the enemy, zooming in and out.
        const ObjectId watcher = camera->Target();
        if (watcher == kNoObject || watcher == id)
        {
            return;
        }
        const dao::Vector from = dao::script::api::GetPosition(watcher);
        const dao::Vector at = dao::script::api::GetPosition(id);
        behind_ = YawWatching({from.x, from.y, from.z}, {at.x, at.y, at.z});
        return;
    }
    if (camera->Target() != id)
    {
        camera->Follow(id);
    }
    const float facing = dao::script::api::GetFacing(id);
    behind_ = YawBehind(facing);
    if (dao::SceneCamera *scene = camera->Scene(); scene != nullptr && behindLogged_ < 3)
    {
        // To check in play that behind is behind: the facing, the yaw it
        // gives, and where the camera was.
        ++behindLogged_;
        spdlog::info("the camera comes round behind {:#010x}: facing {:.0f} degrees, yaw {:.2f} (it was {:.2f})", id,
                     facing, *behind_, scene->Yaw());
    }
}

void CameraDirector::Update(float dtSeconds)
{
    if (!panning_ && !behind_)
    {
        return;
    }
    dao::CameraController *camera = Camera();
    easingSeconds_ += dtSeconds;
    if (camera == nullptr || easingSeconds_ > kGiveUpSeconds)
    {
        panning_ = false;
        behind_.reset();
        return;
    }
    if (panning_)
    {
        const dao::Vector offset = camera->FocusOffset();
        const Vec3 eased = EaseToward({offset.x, offset.y, offset.z}, {}, dtSeconds, kPanHalfLife);
        if (!camera->Tactical() || std::hypot(eased.x, eased.y, eased.z) < 0.05f)
        {
            camera->SetFocusOffset(dao::Vector{});
            panning_ = false;
        }
        else
        {
            camera->SetFocusOffset({eased.x, eased.y, eased.z});
        }
    }
    if (behind_)
    {
        dao::SceneCamera *scene = camera->Scene();
        if (scene == nullptr || camera->Tactical())
        {
            behind_.reset();
            return;
        }
        const float yaw = scene->Yaw();
        if (std::fabs(TurnBetween(yaw, *behind_)) < 0.005f)
        {
            behind_.reset();
            return;
        }
        scene->SetYaw(EaseAngle(yaw, *behind_, dtSeconds, kTurnHalfLife));
    }
}

} // namespace tb::game
