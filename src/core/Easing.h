// The camera's moves between turns, as numbers (src/game/Camera.cpp moves
// it). The user's asks after the first sessions in play (2026-09-30): in
// the tactical view a smooth pan from one character to the next, never
// turning the camera (only the player turns it); close in, the camera
// behind the character whose turn it is, looking where they look.
//
// Both ease by half-lives: each frame closes a share of what is left, so a
// move starts quick and settles, the same whatever the frame rate.
#pragma once

#include "core/Types.h"

namespace tb
{

// What is left of a distance after dtSeconds, closing half of it every
// halfLifeSeconds: 1 for no time, towards 0 as time goes on.
[[nodiscard]] float Remaining(float dtSeconds, float halfLifeSeconds);

// A point moved towards another, eased.
[[nodiscard]] Vec3 EaseToward(const Vec3 &from, const Vec3 &to, float dtSeconds, float halfLifeSeconds);

// The shortest turn from one angle to another, in radians, in (-pi, pi]:
// the camera's yaw is not wrapped (it runs past 2 pi as the player turns;
// DAOLib 6c26174).
[[nodiscard]] float TurnBetween(float fromRadians, float toRadians);

// An angle turned towards another the short way, eased.
[[nodiscard]] float EaseAngle(float fromRadians, float toRadians, float dtSeconds, float halfLifeSeconds);

// The camera's yaw to sit behind a creature whose forward direction is so
// (GetOrientation's world vector, as scripts step in front of a creature:
// position plus distance times it, utility_h.nss). The yaw is the direction
// from the focus to the camera (DAOLib's dao/Camera.h): the backward one.
// Not GetFacing's degrees: in play they were -(the forward vector's angle)
// - 90 (three creatures, 2026-09-30), and taken as the yaw's measure they
// put the camera at the creature's side.
[[nodiscard]] float YawBehind(const Vec3 &forward);

} // namespace tb
