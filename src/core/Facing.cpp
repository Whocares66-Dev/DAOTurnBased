#include "core/Facing.h"

#include <cmath>
#include <numbers>

namespace tb
{

std::optional<float> TurnDegrees(float facingX, float facingY, float towardX, float towardY)
{
    // A direction shorter than this has no angle worth the name: the engine's
    // own turn takes a target nearer than a thousandth for one it stands on
    // (its 1e-6 of a length squared, TurnToward).
    constexpr float kNoLength = 1e-3f;
    if (!std::isfinite(facingX) || !std::isfinite(facingY) || !std::isfinite(towardX) || !std::isfinite(towardY) ||
        std::hypot(facingX, facingY) < kNoLength || std::hypot(towardX, towardY) < kNoLength)
    {
        return std::nullopt;
    }
    // The angle between the two by their cross and dot products: from -180
    // to 180 by itself, with nothing to wrap, and good near 0 and near 180
    // alike, where the difference of two bearings loses its last digits.
    const float cross = facingX * towardY - facingY * towardX;
    const float dot = facingX * towardX + facingY * towardY;
    return std::atan2(cross, dot) * (180.0f / std::numbers::pi_v<float>);
}

Facing FacingFor(float turnDegrees)
{
    const float off = std::abs(turnDegrees);
    // A measure that is no number is no reason to turn anyone.
    if (!(off < kFacedExactlyWithinDegrees))
    {
        return Facing::Leave;
    }
    return off < kFacesAlreadyDegrees ? Facing::Faces : Facing::Face;
}

bool TurnsToFace(CommandType type)
{
    return type == CommandType::Attack || type == CommandType::UseAbility;
}

} // namespace tb
