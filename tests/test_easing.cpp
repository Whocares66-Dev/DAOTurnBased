// The camera's moves as numbers (core/Easing.h): eased pans and turns, the
// short way round, and the yaw behind a creature.
#include "core/Easing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using Catch::Approx;

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
}

TEST_CASE("an eased move closes half of what is left each half-life", "[easing]")
{
    CHECK(tb::Remaining(0.0f, 0.25f) == Approx(1.0f));
    CHECK(tb::Remaining(0.25f, 0.25f) == Approx(0.5f));
    CHECK(tb::Remaining(0.5f, 0.25f) == Approx(0.25f));
    CHECK(tb::Remaining(0.1f, 0.0f) == Approx(0.0f)); // no half-life: at once
    // The same whatever the frame rate: two frames of 1/60 s make one of 1/30.
    const tb::Vec3 offset{10.0f, -4.0f, 0.0f};
    const tb::Vec3 twice = tb::EaseToward(tb::EaseToward(offset, {}, 1.0f / 60, 0.25f), {}, 1.0f / 60, 0.25f);
    const tb::Vec3 once = tb::EaseToward(offset, {}, 1.0f / 30, 0.25f);
    CHECK(twice.x == Approx(once.x));
    CHECK(twice.y == Approx(once.y));
    // A second of it, four half-lives: a sixteenth left.
    const tb::Vec3 settled = tb::EaseToward(offset, {}, 1.0f, 0.25f);
    CHECK(settled.x == Approx(10.0f / 16));
}

TEST_CASE("the camera turns the short way round, from a yaw that is not wrapped", "[easing]")
{
    CHECK(tb::TurnBetween(0.0f, kPi / 2) == Approx(kPi / 2));
    CHECK(tb::TurnBetween(0.1f, 2 * kPi - 0.1f) == Approx(-0.2f));               // across 0, not the long way
    CHECK(tb::TurnBetween(9.29f, 9.29f - 2 * kPi) == Approx(0.0f).margin(1e-5)); // a yaw past 2 pi, as seen in play
    CHECK(tb::TurnBetween(0.0f, kPi) == Approx(kPi));                            // half round: one way, not flickering
    // Eased from 9.29 towards what is 0.3 ahead of it, wrapped: it moves
    // forward by a share of 0.3, and stays near 9.29.
    const float eased = tb::EaseAngle(9.29f, 9.29f + 0.3f - 2 * kPi, 0.25f, 0.25f);
    CHECK(eased == Approx(9.29f + 0.15f));
}

TEST_CASE("behind a creature is the way it faces, turned round", "[easing]")
{
    // Facing east (+x): the camera west of it, a yaw of pi.
    CHECK(tb::TurnBetween(tb::YawBehind({1, 0, 0}), kPi) == Approx(0.0f).margin(1e-5));
    // Facing north (+y): the camera south, -pi/2.
    CHECK(tb::TurnBetween(tb::YawBehind({0, 1, 0}), -kPi / 2) == Approx(0.0f).margin(1e-5));
    // Its height does not matter.
    CHECK(tb::TurnBetween(tb::YawBehind({0, 1, 0.5f}), -kPi / 2) == Approx(0.0f).margin(1e-5));
}
