#include "core/Easing.h"

#include <cmath>
#include <numbers>

namespace tb
{

float Remaining(float dtSeconds, float halfLifeSeconds)
{
    if (!(dtSeconds > 0.0f))
    {
        return 1.0f;
    }
    if (!(halfLifeSeconds > 0.0f))
    {
        return 0.0f;
    }
    return std::exp2(-dtSeconds / halfLifeSeconds);
}

Vec3 EaseToward(const Vec3 &from, const Vec3 &to, float dtSeconds, float halfLifeSeconds)
{
    const float left = Remaining(dtSeconds, halfLifeSeconds);
    return {to.x + (from.x - to.x) * left, to.y + (from.y - to.y) * left, to.z + (from.z - to.z) * left};
}

float TurnBetween(float fromRadians, float toRadians)
{
    constexpr float kPi = std::numbers::pi_v<float>;
    float turn = std::remainder(toRadians - fromRadians, 2.0f * kPi);
    if (turn <= -kPi)
    {
        turn += 2.0f * kPi;
    }
    return turn;
}

float EaseAngle(float fromRadians, float toRadians, float dtSeconds, float halfLifeSeconds)
{
    // From where it is, not from a wrapped copy: the yaw is continuous.
    return fromRadians + TurnBetween(fromRadians, toRadians) * (1.0f - Remaining(dtSeconds, halfLifeSeconds));
}

float YawBehind(const Vec3 &forward)
{
    return std::atan2(-forward.y, -forward.x);
}

} // namespace tb
