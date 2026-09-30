// The camera following whoever acts, as the game follows the selected party
// member: DAOLib's CameraController::Follow (a24efda), the engine's own
// SetTarget. It pans when the new creature is within 15 m of the last and
// cuts when farther, and never turns the camera: only the player does.
//
// Not CenterOnObjectMessage, which the mod sent first: that selects the
// creature if it is the party's and snaps the camera back to what it
// already follows, so on an enemy's turn it only jumped to the player's
// character, and sent just before an enemy's AI was told its turn had
// come, the AI chose nothing (the first sessions in play, 2026-09-30).
//
// Behind Settings::cameraFollow, off by default until seen in play.
#pragma once

#include "core/Types.h"

namespace tb::game
{

// False when there is no explore or combat camera to follow with.
[[nodiscard]] bool FollowWithCamera(ObjectId id);

} // namespace tb::game
