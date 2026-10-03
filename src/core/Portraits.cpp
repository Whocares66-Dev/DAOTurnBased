#include "core/Portraits.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tb
{

std::wstring TextureName(std::wstring_view url)
{
    constexpr std::wstring_view kScheme = L"img://";
    if (url.starts_with(kScheme))
    {
        url.remove_prefix(kScheme.size());
    }
    std::wstring name{url};
    if (!name.empty() && name.find(L'.') == std::wstring::npos)
    {
        name += L".dds";
    }
    return name;
}

void PortraitBook::Follow(const TurnView &view, std::uint32_t nowMs)
{
    if (view.phase != Phase::Idle || urls_.empty())
    {
        return;
    }
    if (releasing_.empty())
    {
        releasingSince_ = nowMs;
        movieLetGo_ = false;
    }
    releasing_.insert(releasing_.end(), rendered_.begin(), rendered_.end());
    rendered_.clear();
    urls_.clear();
}

std::optional<ObjectId> PortraitBook::Next(const TurnView &view) const
{
    if (view.phase == Phase::Idle || view.phase == Phase::Suspended)
    {
        return std::nullopt;
    }
    for (const CardView &card : view.upcoming)
    {
        if (card.id != kNoObject && !urls_.contains(card.id))
        {
            return card.id;
        }
    }
    return std::nullopt;
}

void PortraitBook::Got(ObjectId id, std::wstring url, bool rendered)
{
    // One waiting to be released that a new fight shows again comes back
    // as the game's (its renderer is still there): it is still the bar's.
    if (!url.empty() && (rendered || std::ranges::find(releasing_, id) != releasing_.end()))
    {
        rendered_.insert(id);
    }
    urls_.insert_or_assign(id, std::move(url));
}

std::wstring_view PortraitBook::Of(ObjectId id) const
{
    const auto found = urls_.find(id);
    return found == urls_.end() ? std::wstring_view{} : std::wstring_view{found->second};
}

void PortraitBook::MovieLetGo()
{
    movieLetGo_ = true;
}

std::vector<ObjectId> PortraitBook::TakeReleases(std::uint32_t nowMs)
{
    if (releasing_.empty() || (!movieLetGo_ && nowMs - releasingSince_ < kReleaseWaitMs))
    {
        return {};
    }
    std::vector<ObjectId> out;
    for (const ObjectId id : releasing_)
    {
        // A new fight showing it again got the same renderer back as the
        // game's (PortraitImage): its movie holds the image now.
        if (!urls_.contains(id) && std::ranges::find(out, id) == out.end())
        {
            out.push_back(id);
        }
    }
    releasing_.clear();
    movieLetGo_ = false;
    return out;
}

std::size_t PortraitBook::Found() const
{
    return static_cast<std::size_t>(
        std::ranges::count_if(urls_, [](const auto &entry) { return !entry.second.empty(); }));
}

std::wstring PortraitFamily(std::wstring_view label)
{
    std::wstring family{label.substr(0, label.find(L','))};
    while (!family.empty() && family.back() == L' ')
    {
        family.pop_back();
    }
    for (wchar_t &c : family)
    {
        if (c >= L'A' && c <= L'Z')
        {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return family;
}

float SizeOverPerson(float personalSpace)
{
    if (!std::isfinite(personalSpace) || personalSpace <= kPortraitPersonSpace)
    {
        return 1.0f;
    }
    return std::pow(personalSpace / kPortraitPersonSpace, kPortraitSizePower);
}

std::optional<PortraitFraming> FrameBySize(float personalSpace, float personDistance, float personX, float personZ)
{
    const std::optional<float> distance = PortraitDistance(personalSpace, personDistance);
    if (!distance)
    {
        return std::nullopt;
    }
    const float more = SizeOverPerson(personalSpace) - 1.0f;
    return PortraitFraming{*distance, personX + kPortraitXPerSize * more, personZ + kPortraitZPerSize * more};
}

std::optional<float> FollowZoom(float playerZoom, float personalSpace)
{
    const float more = SizeOverPerson(personalSpace) - 1.0f;
    if (!(more > 0.0f) || !std::isfinite(playerZoom) || playerZoom < kZoomLeast)
    {
        return std::nullopt;
    }
    return std::max(kZoomLeast, std::min(playerZoom, playerZoom - kZoomPerSize * more));
}

std::optional<float> PortraitDistance(float personalSpace, float personDistance)
{
    if (!std::isfinite(personalSpace) || !std::isfinite(personDistance) || personDistance <= 0.0f ||
        personalSpace <= kPortraitPersonSpace)
    {
        return std::nullopt;
    }
    return personDistance * std::pow(personalSpace / kPortraitPersonSpace, kPortraitSizePower);
}

PortraitChoice ChoosePortrait(std::int32_t appearance, std::wstring_view appearanceLabel, bool person,
                              std::wstring_view tag, const std::vector<PortraitRow> &rows)
{
    const auto own = [&](const PortraitRow &row) { return appearance > 0 && row.appearance == appearance; };
    for (const PortraitRow &row : rows)
    {
        if (own(row) && !row.made.empty())
        {
            return {PortraitSource::ReadyMade, row.made};
        }
    }
    if (person)
    {
        return {PortraitSource::Render, {}};
    }
    for (const PortraitRow &row : rows)
    {
        if (own(row) || (!tag.empty() && row.tag == tag))
        {
            return {PortraitSource::Render, {}}; // the table has its camera
        }
    }
    const std::wstring family = PortraitFamily(appearanceLabel);
    const PortraitRow *kin = nullptr;
    for (const PortraitRow &row : rows)
    {
        if (family.empty() || row.made.empty() || PortraitFamily(row.label) != family)
        {
            continue;
        }
        if (row.label.find(L',') == std::wstring::npos)
        {
            return {PortraitSource::ReadyMade, row.made}; // the plain one of its family
        }
        if (kin == nullptr)
        {
            kin = &row;
        }
    }
    if (kin != nullptr)
    {
        return {PortraitSource::ReadyMade, kin->made};
    }
    return {};
}

} // namespace tb
