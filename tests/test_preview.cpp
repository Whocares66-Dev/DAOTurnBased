// The movement preview: where a path's movement runs out, and the string
// the movie draws from, read back the way its ActionScript will.
#include "core/Bar.h"
#include "core/Preview.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace tb;
using Catch::Approx;

namespace
{

std::vector<std::wstring> Split(const std::wstring &text, wchar_t on)
{
    std::vector<std::wstring> parts;
    std::wstring part;
    for (const wchar_t c : text)
    {
        if (c == on)
        {
            parts.push_back(part);
            part.clear();
        }
        else
        {
            part += c;
        }
    }
    parts.push_back(part);
    return parts;
}

// An L: 6 m east, then 4 m north.
const std::vector<Vec3> kPath{Vec3{0, 0, 0}, Vec3{6, 0, 0}, Vec3{6, 4, 0}};

} // namespace

TEST_CASE("a path within the movement left is all in reach", "[preview]")
{
    const PathSplit split = SplitPath(kPath, 10.5f);
    CHECK(split.length == Approx(10.0f));
    CHECK_FALSE(split.TooFar());
    REQUIRE(split.reach.size() == 3);
    CHECK(split.reach.back().y == Approx(4.0f));
    CHECK(split.beyond.empty());
}

TEST_CASE("a path as long as the movement left, to the hair, is in reach", "[preview]")
{
    // The machine refuses a walk longer than the movement left, not one as long.
    const PathSplit split = SplitPath(kPath, 10.0f);
    CHECK_FALSE(split.TooFar());
    CHECK(split.reach.size() == 3);
}

TEST_CASE("a path longer than the movement left is cut where it runs out", "[preview]")
{
    const PathSplit split = SplitPath(kPath, 7.5f); // 6 m east, then 1.5 of the 4 north
    CHECK(split.length == Approx(10.0f));
    REQUIRE(split.TooFar());
    REQUIRE(split.reach.size() == 3);
    CHECK(split.reach[1].x == Approx(6.0f));
    CHECK(split.reach[2].x == Approx(6.0f));
    CHECK(split.reach[2].y == Approx(1.5f));
    // The beyond begins where the reach ends, and goes to the path's end.
    REQUIRE(split.beyond.size() == 2);
    CHECK(split.beyond[0].y == Approx(1.5f));
    CHECK(split.beyond[1].y == Approx(4.0f));
}

TEST_CASE("the movement running out on the first step cuts that step", "[preview]")
{
    const PathSplit split = SplitPath(kPath, 2.0f);
    REQUIRE(split.reach.size() == 2);
    CHECK(split.reach[1].x == Approx(2.0f));
    REQUIRE(split.beyond.size() == 3);
    CHECK(split.beyond[0].x == Approx(2.0f));
    CHECK(split.beyond[1].x == Approx(6.0f));
    CHECK(split.beyond[2].y == Approx(4.0f));
}

TEST_CASE("the movement running out at a corner puts the corner in both halves", "[preview]")
{
    const PathSplit split = SplitPath(kPath, 6.0f);
    REQUIRE(split.TooFar());
    REQUIRE(split.reach.size() == 2);
    CHECK(split.reach[1].x == Approx(6.0f));
    REQUIRE(split.beyond.size() == 2);
    CHECK(split.beyond[0].x == Approx(6.0f));
    CHECK(split.beyond[0].y == Approx(0.0f));
}

TEST_CASE("with no movement left the whole path is beyond", "[preview]")
{
    for (const float meters : {0.0f, -1.0f})
    {
        const PathSplit split = SplitPath(kPath, meters);
        REQUIRE(split.TooFar());
        CHECK(split.reach.size() == 1); // where it stands: nothing to draw in gold
        REQUIRE(split.beyond.size() == 3);
        CHECK(split.beyond[0].x == Approx(0.0f));
        CHECK(split.beyond[2].y == Approx(4.0f));
    }
}

TEST_CASE("a path's height counts in its length", "[preview]")
{
    const std::vector<Vec3> stairs{Vec3{0, 0, 0}, Vec3{3, 0, 4}}; // 5 m up a slope
    CHECK(SplitPath(stairs, 9.0f).length == Approx(5.0f));
    const PathSplit split = SplitPath(stairs, 2.5f);
    REQUIRE(split.reach.size() == 2);
    CHECK(split.reach[1].x == Approx(1.5f));
    CHECK(split.reach[1].z == Approx(2.0f));
}

TEST_CASE("no path, a path of one point and a step of no length split to nothing odd", "[preview]")
{
    CHECK(SplitPath({}, 5.0f).reach.empty());
    const std::vector<Vec3> one{Vec3{1, 2, 3}};
    CHECK(SplitPath(one, 5.0f).reach.size() == 1);
    CHECK_FALSE(SplitPath(one, 0.0f).TooFar());
    const std::vector<Vec3> twice{Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{2, 0, 0}};
    const PathSplit split = SplitPath(twice, 1.0f);
    CHECK(split.length == Approx(2.0f));
    REQUIRE(split.TooFar());
    CHECK(split.reach.back().x == Approx(1.0f));
    CHECK(split.beyond.back().x == Approx(2.0f));
}

TEST_CASE("the preview's string has a head and two lines of points", "[preview]")
{
    PreviewView view;
    view.width = 1920;
    view.height = 1080;
    view.length = 10.04f;
    view.meters = 7.5f;
    view.reach = {ScreenDot{100.4f, 200.6f}, ScreenDot{300, 250}, ScreenDot{310, 180}};
    view.beyond = {ScreenDot{310, 180}, ScreenDot{330, 90}};
    view.stopRing = {ScreenDot{300, 180}, ScreenDot{310, 185}, ScreenDot{320, 180}, ScreenDot{310, 175}};
    view.endRing = {ScreenDot{320, 90}, ScreenDot{330, 95}, ScreenDot{340, 90}};
    const std::vector<std::wstring> records = Split(EncodePreview(view), kBarCard);
    REQUIRE(records.size() == 5);
    const std::vector<std::wstring> head = Split(records[0], kBarField);
    REQUIRE(head.size() == 6);
    CHECK(head[0] == std::to_wstring(kPreviewVersion));
    CHECK(head[1] == L"1920");
    CHECK(head[2] == L"1080");
    CHECK(head[3] == L"2"); // too far
    CHECK(head[4] == L"10.0");
    CHECK(head[5] == L"7.5");
    const std::vector<std::wstring> reach = Split(records[1], kBarField);
    REQUIRE(reach.size() == 6);
    CHECK(reach[0] == L"100"); // whole pixels
    CHECK(reach[1] == L"201");
    CHECK(reach[4] == L"310");
    const std::vector<std::wstring> beyond = Split(records[2], kBarField);
    REQUIRE(beyond.size() == 4);
    CHECK(beyond[3] == L"90");
    // The rings after, where the movement runs out and then at the end.
    const std::vector<std::wstring> stop = Split(records[3], kBarField);
    REQUIRE(stop.size() == 8);
    CHECK(stop[0] == L"300");
    const std::vector<std::wstring> end = Split(records[4], kBarField);
    REQUIRE(end.size() == 6);
    CHECK(end[5] == L"90");
}

TEST_CASE("a preview all in reach says so and has an empty beyond", "[preview]")
{
    PreviewView view;
    view.width = 1280;
    view.height = 720;
    view.length = 3.0f;
    view.meters = 9.0f;
    view.reach = {ScreenDot{10, 20}, ScreenDot{30, 40}};
    view.endRing = {ScreenDot{25, 40}, ScreenDot{30, 43}, ScreenDot{35, 40}};
    const std::vector<std::wstring> records = Split(EncodePreview(view), kBarCard);
    REQUIRE(records.size() == 5);
    CHECK(Split(records[0], kBarField)[3] == L"1");
    CHECK(records[2].empty());
    CHECK(records[3].empty()); // no ring where the movement runs out: it does not
    CHECK_FALSE(records[4].empty());
}

TEST_CASE("nothing to draw is a head alone, of the same fields", "[preview]")
{
    const std::wstring nothing = EncodeNoPreview();
    CHECK(Split(nothing, kBarCard).size() == 1);
    const std::vector<std::wstring> head = Split(nothing, kBarField);
    REQUIRE(head.size() == 6);
    CHECK(head[0] == std::to_wstring(kPreviewVersion));
    CHECK(head[3] == L"0");
    // A path of one point on the screen is nothing to draw either.
    PreviewView view;
    view.width = 800;
    view.height = 600;
    view.reach = {ScreenDot{1, 2}};
    CHECK(EncodePreview(view) == nothing);
}

TEST_CASE("a ring lies flat round its centre, its points evenly about it", "[preview]")
{
    const Vec3 centre{3, -2, 1.5f};
    const std::vector<Vec3> ring = RingAround(centre, 0.5f, 16);
    REQUIRE(ring.size() == 16);
    for (const Vec3 &point : ring)
    {
        CHECK(point.z == Approx(1.5f)); // on the ground's level
        CHECK(Distance(point, centre) == Approx(0.5f));
    }
    CHECK(ring[0].x == Approx(3.5f));  // begins to the east
    CHECK(ring[4].y == Approx(-1.5f)); // a quarter round: to the north
    CHECK(ring[8].x == Approx(2.5f));
    CHECK(Distance(ring[0], ring[1]) == Approx(Distance(ring[7], ring[8])));
    CHECK(RingAround(centre, 0.5f, 0).empty());
}

TEST_CASE("a walk along the path shown is not apart from it", "[preview]")
{
    CHECK(PathLength(kPath) == Approx(10.0f));
    CHECK(PathApart(kPath, kPath) == Approx(0.0f));
    // The same way in more points, and a walk that begins a step in.
    const std::vector<Vec3> finer{Vec3{0, 0, 0}, Vec3{3, 0, 0}, Vec3{6, 0, 0}, Vec3{6, 2, 0}, Vec3{6, 4, 0}};
    CHECK(PathApart(finer, kPath) == Approx(0.0f));
    const std::vector<Vec3> later{Vec3{2, 0, 0}, Vec3{6, 0, 0}, Vec3{6, 4, 0}};
    CHECK(PathApart(later, kPath) == Approx(0.0f));
}

TEST_CASE("a walk that leaves the path shown is as far apart as it strays", "[preview]")
{
    // Round the corner by another way: out to (8, 0) and back in.
    const std::vector<Vec3> wide{Vec3{0, 0, 0}, Vec3{8, 0, 0}, Vec3{6, 4, 0}};
    CHECK(PathApart(wide, kPath) == Approx(2.0f));
    // Its points on the path, a step of it across the corner: the step's middle is off.
    const std::vector<Vec3> across{Vec3{0, 0, 0}, Vec3{2, 0, 0}, Vec3{6, 4, 0}};
    CHECK(PathApart(across, kPath) == Approx(2.0f)); // (4, 2) is 2 m from either leg
    // Past the path's end.
    const std::vector<Vec3> beyond{Vec3{6, 4, 0}, Vec3{6, 7, 0}};
    CHECK(PathApart(beyond, kPath) == Approx(3.0f));
    // Height counts: a walk a floor above is not on the path.
    const std::vector<Vec3> above{Vec3{3, 0, 3}};
    CHECK(PathApart(above, kPath) == Approx(3.0f));
}

TEST_CASE("no walk, or no path shown, is not apart", "[preview]")
{
    CHECK(PathApart({}, kPath) == 0.0f);
    CHECK(PathApart(kPath, {}) == 0.0f);
    CHECK(PathLength({}) == 0.0f);
    const std::vector<Vec3> one{Vec3{1, 1, 0}};
    CHECK(PathLength(one) == 0.0f);
    CHECK(PathApart(kPath, one) == Approx(Distance(Vec3{6, 4, 0}, Vec3{1, 1, 0}))); // from its one point
}
