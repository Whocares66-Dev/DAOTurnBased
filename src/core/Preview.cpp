#include "core/Preview.h"

#include "core/Bar.h"

#include <cmath>
#include <format>

namespace tb
{

namespace
{

Vec3 Between(const Vec3 &a, const Vec3 &b, float part)
{
    return Vec3{a.x + (b.x - a.x) * part, a.y + (b.y - a.y) * part, a.z + (b.z - a.z) * part};
}

// From a point to the nearest point of a step.
float ToStep(const Vec3 &point, const Vec3 &from, const Vec3 &to)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    const float square = dx * dx + dy * dy + dz * dz;
    float along = 0.0f;
    if (square > 0.0f)
    {
        along = ((point.x - from.x) * dx + (point.y - from.y) * dy + (point.z - from.z) * dz) / square;
        along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
    }
    return Distance(point, Between(from, to, along));
}

float ToPath(const Vec3 &point, std::span<const Vec3> path)
{
    float nearest = Distance(point, path.front());
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const float d = ToStep(point, path[i - 1], path[i]);
        nearest = d < nearest ? d : nearest;
    }
    return nearest;
}

void AppendDots(std::wstring &out, const std::vector<ScreenDot> &dots)
{
    out += kBarCard;
    bool first = true;
    for (const ScreenDot &dot : dots)
    {
        if (!first)
        {
            out += kBarField;
        }
        first = false;
        out += std::format(L"{}{}{}", std::lround(dot.x), kBarField, std::lround(dot.y));
    }
}

} // namespace

PathSplit SplitPath(std::span<const Vec3> path, float meters)
{
    PathSplit split;
    if (path.empty())
    {
        return split;
    }
    const float reach = meters > 0.0f ? meters : 0.0f;
    split.reach.push_back(path.front());
    bool out = false; // the movement has run out: what follows is beyond
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const Vec3 &from = path[i - 1];
        const Vec3 &to = path[i];
        const float step = Distance(from, to);
        if (!out && split.length + step > reach)
        {
            // The movement runs out on this step: both halves meet there.
            const Vec3 cut = step > 0.0f ? Between(from, to, (reach - split.length) / step) : from;
            if (reach > split.length)
            {
                split.reach.push_back(cut);
            }
            split.beyond.push_back(cut);
            out = true;
        }
        split.length += step;
        (out ? split.beyond : split.reach).push_back(to);
    }
    return split;
}

float PathLength(std::span<const Vec3> path)
{
    float length = 0.0f;
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        length += Distance(path[i - 1], path[i]);
    }
    return length;
}

float PathApart(std::span<const Vec3> walked, std::span<const Vec3> shown)
{
    if (walked.empty() || shown.empty())
    {
        return 0.0f;
    }
    float apart = 0.0f;
    for (std::size_t i = 0; i < walked.size(); ++i)
    {
        const float d = ToPath(walked[i], shown);
        apart = d > apart ? d : apart;
        if (i > 0)
        {
            // A step may leave the path between two points that are on it.
            const float middle = ToPath(Between(walked[i - 1], walked[i], 0.5f), shown);
            apart = middle > apart ? middle : apart;
        }
    }
    return apart;
}

std::vector<Vec3> RingAround(const Vec3 &centre, float radius, int count)
{
    constexpr float kTurn = 6.28318530718f;
    std::vector<Vec3> ring;
    for (int i = 0; i < count; ++i)
    {
        const float angle = kTurn * static_cast<float>(i) / static_cast<float>(count);
        ring.push_back(Vec3{centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius, centre.z});
    }
    return ring;
}

std::wstring EncodeNoPreview()
{
    return std::format(L"{}{}0{}0{}0{}0.0{}0.0", kPreviewVersion, kBarField, kBarField, kBarField, kBarField,
                       kBarField);
}

std::wstring EncodePreview(const PreviewView &view)
{
    if (view.reach.size() + view.beyond.size() < 2)
    {
        return EncodeNoPreview();
    }
    std::wstring out =
        std::format(L"{}{}{}{}{}{}{}{}{:.1f}{}{:.1f}", kPreviewVersion, kBarField, view.width, kBarField, view.height,
                    kBarField, view.beyond.empty() ? 1 : 2, kBarField, view.length, kBarField, view.meters);
    AppendDots(out, view.reach);
    AppendDots(out, view.beyond);
    AppendDots(out, view.stopRing);
    AppendDots(out, view.endRing);
    return out;
}

} // namespace tb
