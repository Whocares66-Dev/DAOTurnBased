#include "core/Portraits.h"

#include "core/Json.h"
#include "core/Text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <system_error>
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

std::optional<ObjectId> PortraitBook::Next(const TurnView &view, const BarSpan &row) const
{
    if (view.phase == Phase::Idle || view.phase == Phase::Suspended)
    {
        return std::nullopt;
    }
    const std::size_t from = std::min(row.before, view.upcoming.size());
    const std::size_t to = std::min(from + row.shown, view.upcoming.size());
    for (std::size_t i = from; i < to; ++i)
    {
        const CardView &card = view.upcoming[i];
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

bool PortraitBook::Leaving(ObjectId id) const
{
    return std::ranges::find(releasing_, id) != releasing_.end();
}

BodyKind BodyKindOf(bool personsModel, float terrainFollowPitch, float extent)
{
    if (terrainFollowPitch > 0.0f)
    {
        return BodyKind::OnAllFours;
    }
    const bool personSized = extent <= kPersonExtent * kBiggerThanPerson;
    return personsModel && personSized ? BodyKind::Person : BodyKind::Upright;
}

std::string_view BodyKindWhy(bool personsModel, float terrainFollowPitch, float extent)
{
    switch (BodyKindOf(personsModel, terrainFollowPitch, extent))
    {
    case BodyKind::OnAllFours:
        return "its body pitches with the ground it stands on, by the appearance table's TerrainFollowPitch";
    case BodyKind::Upright:
        return personsModel ? "it stands up, and is bigger than a person from its feet to its head"
                            : "it stands up, and its model is a creature's by the appearance table's ModelType";
    case BodyKind::Person:
        break;
    }
    return "a person's model, standing up, no bigger than a person";
}

std::string_view BodyKindName(BodyKind kind)
{
    switch (kind)
    {
    case BodyKind::Upright:
        return "upright";
    case BodyKind::OnAllFours:
        return "on_all_fours";
    case BodyKind::Person:
        break;
    }
    return "person";
}

std::optional<BodyKind> BodyKindNamed(std::string_view name)
{
    for (const BodyKind kind : {BodyKind::Person, BodyKind::Upright, BodyKind::OnAllFours})
    {
        if (name == BodyKindName(kind))
        {
            return kind;
        }
    }
    return std::nullopt;
}

HeadExtent ExtentOf(const Vec3 &head, const Vec3 &feet)
{
    const float world = Distance(head, feet);
    const float own = Distance(head, Vec3{});
    return HeadExtent{std::min(world, own), world <= own};
}

const FramingRow *RowOf(std::int32_t appearance)
{
    // An appearance's row: a default has no appearance, and none is 0.
    const auto row = std::ranges::find_if(
        kPortraitTable, [appearance](const FramingRow &r) { return appearance > 0 && r.appearance == appearance; });
    return row != kPortraitTable.end() ? &*row : nullptr;
}

const FramingRow *DefaultRowOf(BodyKind kind)
{
    const auto row = std::ranges::find_if(kPortraitTable,
                                          [kind](const FramingRow &r) { return r.appearance == 0 && r.kind == kind; });
    return row != kPortraitTable.end() ? &*row : nullptr;
}

std::string_view FramedByName(FramedBy by)
{
    switch (by)
    {
    case FramedBy::Row:
        return "row";
    case FramedBy::PortraitsTable:
        return "portraits_table";
    case FramedBy::Default:
        return "default";
    case FramedBy::Person:
        return "person";
    case FramedBy::Nothing:
        break;
    }
    return "";
}

PortraitFrame FrameByRow(const FramingRow &row)
{
    return PortraitFrame{PortraitCamera{row.pitch, row.rotation, row.distance, row.up, row.aside},
                         FramedBy::Row,
                         row.label,
                         row.note,
                         row.lit,
                         row.lightX,
                         row.lightY,
                         row.lightZ,
                         false};
}

PortraitFrame FrameByDefault(const FramingRow &row, float extent)
{
    // What is not a size is framed from the least.
    const float bySize = std::isfinite(extent) ? row.distance * std::max(extent, 0.0f) : 0.0f;
    const float distance = std::clamp(bySize, kFramedFromLeast, kFramedFromMost);
    // The aside and the up go by the size the distance was let: the same
    // picture at any size, and at the limits too.
    const float size = row.distance > 0.0f ? distance / row.distance : 0.0f;
    return PortraitFrame{PortraitCamera{row.pitch, row.rotation, distance, row.up * size, row.aside * size},
                         FramedBy::Default,
                         row.label,
                         row.note,
                         row.lit,
                         row.lightX,
                         row.lightY,
                         row.lightZ,
                         bySize > kFramedFromMost};
}

PortraitFrame FramePerson(const PortraitCamera &person)
{
    PortraitFrame frame;
    frame.camera = person;
    frame.by = FramedBy::Person;
    return frame;
}

PortraitFrame FramePortrait(std::int32_t appearance, BodyKind kind, bool kindAsked, const PortraitCamera *gamesOwn,
                            float extent, const PortraitCamera &person)
{
    if (!kindAsked)
    {
        if (const FramingRow *row = RowOf(appearance))
        {
            return FrameByRow(*row);
        }
        if (gamesOwn != nullptr)
        {
            PortraitFrame frame;
            frame.camera = *gamesOwn;
            frame.by = FramedBy::PortraitsTable;
            return frame;
        }
    }
    if (const FramingRow *row = DefaultRowOf(kind))
    {
        return FrameByDefault(*row, extent);
    }
    return FramePerson(person);
}

bool RendersWithoutHead(const FramingRow *row, const PortraitTuning &tuning)
{
    const bool rooted = row != nullptr && row->fromRoot;
    return tuning.AnyValue() || (rooted && !tuning.kind.has_value());
}

std::string FramingRule(const PortraitFrame &frame)
{
    switch (frame.by)
    {
    case FramedBy::Row:
        return std::format("row {} ({})", frame.label, frame.note);
    case FramedBy::PortraitsTable:
        return "the portraits table's own row for it";
    case FramedBy::Default:
        return std::format("the default for {} ({})", frame.label, frame.note);
    case FramedBy::Person:
        return "a person's body: the portraits table's Default row";
    case FramedBy::Nothing:
        break;
    }
    return {};
}

PortraitCamera Tuned(const PortraitCamera &rule, const PortraitTuning &tuning)
{
    return PortraitCamera{tuning.pitch.value_or(rule.pitch), tuning.rotation.value_or(rule.rotation),
                          tuning.distance.value_or(rule.distance), tuning.zAdjust.value_or(rule.zAdjust),
                          tuning.xAdjust.value_or(rule.xAdjust)};
}

std::string_view PortraitLackName(PortraitLack lack)
{
    switch (lack)
    {
    case PortraitLack::Creature:
        return "no_creature";
    case PortraitLack::HeadNode:
        return "no_head_node";
    case PortraitLack::Render:
        return "no_render";
    case PortraitLack::Nothing:
        break;
    }
    return "";
}

float PictureAcross(const PortraitCamera &camera)
{
    return kPortraitAcross * camera.distance;
}

bool PastFarPlane(const PortraitCamera &camera)
{
    return kPortraitBackOff * camera.distance > kPortraitFarPlane;
}

namespace
{

void AppendPoint(std::string &out, const Vec3 &at)
{
    out += std::format("[{:.2f},{:.2f},{:.2f}]", at.x, at.y, at.z);
}

void AppendText(std::string &out, std::string_view key, std::string_view text)
{
    out += std::format(R"(,"{}":")", key);
    AppendJsonString(out, text);
    out += '"';
}

void AppendLight(std::string &out, std::string_view key, const std::optional<float> &light)
{
    out += light ? std::format(R"(,"{}":{:.2f})", key, *light) : std::format(R"(,"{}":null)", key);
}

// The camera in the log's words.
std::string CameraWords(const PortraitCamera &camera)
{
    return std::format("from {:.2f} m, pitch {:.1f}, rotation {:.1f}, {:.2f} m aside, {:.2f} m up: a picture {:.2f} m "
                       "across",
                       camera.distance, camera.pitch, camera.rotation, camera.xAdjust, camera.zAdjust,
                       PictureAcross(camera));
}

// What the engine measured of it, in the log's words.
std::string MeasureWords(const PortraitReport &report)
{
    std::string out = std::format("appearance {} \"{}\"", report.appearance, Utf8(report.label));
    if (report.head)
    {
        out += std::format("; {:.2f} m from feet to head ({}), its head node at ({:.1f}, {:.1f}, {:.1f}), itself at "
                           "({:.1f}, {:.1f}, {:.1f})",
                           report.extent, report.extentBy, report.head->x, report.head->y, report.head->z,
                           report.feet.x, report.feet.y, report.feet.z);
    }
    else
    {
        out += std::format("; no head node, itself at ({:.1f}, {:.1f}, {:.1f})", report.feet.x, report.feet.y,
                           report.feet.z);
    }
    return out;
}

} // namespace

std::string PortraitLine(const PortraitReport &report)
{
    if (report.image.empty())
    {
        std::string out =
            std::format("{} ({:#010x}) has no portrait: its card shows initials", Utf8(report.name), report.id);
        if (!report.problem.empty())
        {
            // What was measured of it, where there was a creature to measure.
            const bool known = report.appearance != 0 || !report.label.empty() || report.head.has_value();
            out += known ? std::format(" ({}; {})", report.problem, MeasureWords(report))
                         : std::format(" ({})", report.problem);
        }
        return out;
    }
    std::string out = std::format("{}'s portrait: {} ({}", Utf8(report.name), Utf8(report.image), report.source);
    if (report.rendered)
    {
        out += std::format("; {}: {}{}{}; {}", report.rule, report.cameraGiven ? "" : "the table's own, ",
                           CameraWords(report.camera), report.atMost ? ", held where its head is still drawn" : "",
                           MeasureWords(report));
        if (!report.problem.empty())
        {
            out += "; " + report.problem;
        }
    }
    out += ')';
    return out;
}

std::string ToJson(const PortraitReport &report)
{
    std::string out = std::format(R"({{"id":"{:#x}")", report.id);
    AppendText(out, "name", Utf8(report.name));
    AppendText(out, "tag", Utf8(report.tag));
    out += std::format(R"(,"appearance":{})", report.appearance);
    AppendText(out, "label", Utf8(report.label));
    out += std::format(R"(,"personal_space":{:.2f},"persons_model":{},"terrain_follow_pitch":{:.1f})",
                       report.personalSpace, report.personsModel, report.terrainFollowPitch);
    AppendText(out, "kind", BodyKindName(report.kind));
    AppendText(out, "kind_by", report.kindBy);
    AppendText(out, "source", report.source);
    AppendText(out, "framed_by", FramedByName(report.framedBy));
    AppendText(out, "row", report.row);
    AppendText(out, "rule", report.rule);
    AppendText(out, "image", Utf8(report.image));
    AppendText(out, "texture", Utf8(TextureName(report.image)));
    // ready: false where nothing could be rendered, with which of the three
    // it was by name; `problem`, at the end, says it in words. A creature
    // with no head node that was rendered all the same (RendersWithoutHead)
    // is ready and keeps its lack, and `aimed_at` says what its camera was
    // set about: the scene's origin, where another's is its head node.
    const std::string_view aimedAt = !report.rendered ? "" : report.head ? "head_node" : "scene_origin";
    out += std::format(R"(,"ready":{},"lack":"{}","rendered":{},"aimed_at":"{}","fresh":{},"shown":{})",
                       report.lack == PortraitLack::Nothing || report.rendered, PortraitLackName(report.lack),
                       report.rendered, aimedAt, report.fresh, report.shown);
    // The camera by the tool's own names for its values, so that an answer
    // can be given back as the next call's arguments.
    out += std::format(R"(,"camera":{{"given":{},"pitch":{:.2f},"rotation":{:.2f},"distance":{:.3f},"zAdjust":{:.3f},)"
                       R"("xAdjust":{:.3f})",
                       report.cameraGiven, report.camera.pitch, report.camera.rotation, report.camera.distance,
                       report.camera.zAdjust, report.camera.xAdjust);
    AppendLight(out, "lightX", report.lightX);
    AppendLight(out, "lightY", report.lightY);
    AppendLight(out, "lightZ", report.lightZ);
    // The camera against the creature's size, as a row of the framing table
    // is reckoned when one is found by eye: nothing of it with no extent.
    const bool sized = report.extent > 0.0f;
    out += std::format(R"(}},"at_most":{},"distance_per_extent":{:.2f},"z_per_extent":{:.2f},"picture_m":{:.2f},)"
                       R"("camera_from_head_m":{:.2f},"past_far_plane":{},"position":)",
                       report.atMost, sized ? report.camera.distance / report.extent : 0.0f,
                       sized ? report.camera.zAdjust / report.extent : 0.0f, PictureAcross(report.camera),
                       kPortraitBackOff * report.camera.distance, PastFarPlane(report.camera));
    AppendPoint(out, report.feet);
    out += R"(,"head":)";
    if (report.head)
    {
        AppendPoint(out, *report.head);
    }
    else
    {
        out += "null";
    }
    out += std::format(R"(,"extent":{:.2f})", report.extent);
    AppendText(out, "extent_by", report.extentBy);
    AppendText(out, "problem", report.problem);
    out += '}';
    return out;
}

namespace
{

// All of the text as a number of that kind, or nothing: "8 m" and "0x" are
// not numbers, and a tester who gave one is told so, not given the rule's.
template <typename Number> std::optional<Number> Whole(std::string_view text, int base = 10)
{
    Number value{};
    const std::from_chars_result got = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || got.ec != std::errc{} || got.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<float> Real(std::string_view text)
{
    float value = 0.0f;
    const std::from_chars_result got = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || got.ec != std::errc{} || got.ptr != text.data() + text.size() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

// An id as Eluvian writes them ("0x7d9"), or a whole number.
std::optional<ObjectId> IdOf(std::string_view text)
{
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        return Whole<ObjectId>(text.substr(2), 16);
    }
    return Whole<ObjectId>(text);
}

std::wstring Wide(std::string_view ascii)
{
    return std::wstring{ascii.begin(), ascii.end()};
}

} // namespace

PortraitAskRead ReadPortraitAsk(std::string_view json)
{
    constexpr std::string_view kHow =
        R"(Give the creature's id, and to tune its framing any of kind ("person", "upright" or "on_all_fours"), pitch, )"
        R"(rotation, distance, zAdjust, xAdjust, lightX, lightY, lightZ (numbers: degrees and metres), and show )"
        R"((true to put the picture up large, with seconds for how long): {"id": "0x7d9", "distance": 8, "show": true}.)";
    const auto wrong = [&](std::string_view what) {
        return PortraitAskRead{std::nullopt, std::string{what} + " " + std::string{kHow}};
    };
    PortraitAsk ask;
    const std::optional<std::string> id = JsonField(json, "id");
    const std::optional<ObjectId> object = id ? IdOf(*id) : std::nullopt;
    if (!object || *object == kNoObject)
    {
        return wrong(id ? "The id is not a creature's id." : "There is no id.");
    }
    ask.id = *object;
    if (const std::optional<std::string> kind = JsonField(json, "kind"))
    {
        ask.tuning.kind = BodyKindNamed(*kind);
        if (!ask.tuning.kind)
        {
            return wrong("The kind is not one there is.");
        }
    }
    using Value = std::pair<std::string_view, std::optional<float> PortraitTuning::*>;
    const std::array<Value, 8> values{{
        {"pitch", &PortraitTuning::pitch},
        {"rotation", &PortraitTuning::rotation},
        {"distance", &PortraitTuning::distance},
        {"zAdjust", &PortraitTuning::zAdjust},
        {"xAdjust", &PortraitTuning::xAdjust},
        {"lightX", &PortraitTuning::lightX},
        {"lightY", &PortraitTuning::lightY},
        {"lightZ", &PortraitTuning::lightZ},
    }};
    for (const auto &[key, member] : values)
    {
        if (const std::optional<std::string> text = JsonField(json, key))
        {
            ask.tuning.*member = Real(*text);
            if (!(ask.tuning.*member))
            {
                return wrong(std::string{key} + " is not a number.");
            }
        }
    }
    // A camera no nearer than its near plane would be inside the head.
    if (ask.tuning.distance && !(*ask.tuning.distance > 0.0f))
    {
        return wrong("The distance is not more than nothing.");
    }
    if (const std::optional<std::string> show = JsonField(json, "show"))
    {
        if (*show != "true" && *show != "false")
        {
            return wrong("show is neither true nor false.");
        }
        ask.show = *show == "true";
    }
    if (const std::optional<std::string> seconds = JsonField(json, "seconds"))
    {
        const std::optional<std::uint32_t> whole = Whole<std::uint32_t>(*seconds);
        if (!whole || *whole == 0)
        {
            return wrong("seconds is not a whole number of them.");
        }
        ask.seconds = std::min(*whole, kShowSecondsMost);
    }
    return PortraitAskRead{ask, {}};
}

std::wstring EncodeShow(std::uint32_t serial, std::wstring_view url, std::wstring_view title, std::wstring_view detail)
{
    std::wstring out = std::to_wstring(kShowVersion);
    out += kShowField;
    out += std::to_wstring(serial);
    const std::wstring texture = TextureName(url);
    for (const std::wstring_view text : {std::wstring_view{texture}, title, detail})
    {
        out += kShowField;
        // As the bar's own fields: nothing in one may split it.
        for (const wchar_t c : text)
        {
            if (c >= 0x20)
            {
                out += c;
            }
        }
    }
    return out;
}

std::wstring ShowTitle(const PortraitReport &report)
{
    std::wstring out = std::to_wstring(report.appearance) + L" " + report.label + L": ";
    if (report.image.empty())
    {
        return out + L"no picture";
    }
    if (!report.rendered)
    {
        return out + Wide(report.source);
    }
    // What framed it: its own row of the table, the game's, or its kind's
    // default.
    std::wstring kind = Wide(BodyKindName(report.kind));
    std::ranges::replace(kind, L'_', L' ');
    switch (report.framedBy)
    {
    case FramedBy::Row:
        return out + L"its own row";
    case FramedBy::PortraitsTable:
        return out + L"the game's own row";
    case FramedBy::Default:
        return out + L"the default, " + kind;
    case FramedBy::Person:
    case FramedBy::Nothing:
        break;
    }
    return out + kind;
}

std::wstring ShowDetail(const PortraitReport &report)
{
    if (!report.rendered)
    {
        return Wide(report.problem);
    }
    return Wide(std::format("{:.2f} m, pitch {:.1f}, rotation {:.1f}, {:.2f} aside, {:.2f} up; {:.2f} m across; feet "
                            "to head {:.2f} m{}",
                            report.camera.distance, report.camera.pitch, report.camera.rotation, report.camera.xAdjust,
                            report.camera.zAdjust, PictureAcross(report.camera), report.extent,
                            report.atMost ? "; at the furthest" : ""));
}

std::optional<float> CloseDistanceFor(float extent)
{
    if (!std::isfinite(extent) || !(extent > kPersonExtent * kBiggerThanPerson))
    {
        return std::nullopt;
    }
    return std::min(kCloseMost, kCloseGame * extent / kPersonExtent);
}

CloseDistance CloseDistanceOf(const std::optional<Vec3> &box, const std::optional<float> &feetToHead)
{
    if (box && std::isfinite(box->x) && std::isfinite(box->y) && std::isfinite(box->z))
    {
        // How many persons long it is, and how many high.
        const float length = std::max(box->x, box->y) / kPersonAcross;
        const float height = box->z / kPersonHeight;
        const float size = std::max(length, height);
        if (size > 0.0f)
        {
            const CloseBy by = length > height ? CloseBy::Length : CloseBy::Height;
            if (size > 1.0f)
            {
                return CloseDistance{std::min(kCloseMost, kCloseGame * size), by};
            }
            return CloseDistance{std::nullopt, by}; // never under the game's own
        }
    }
    if (feetToHead)
    {
        return CloseDistance{CloseDistanceFor(*feetToHead), CloseBy::Head};
    }
    return CloseDistance{std::nullopt, CloseBy::Nothing};
}

float ViewYaw(float behind, ViewSide side)
{
    constexpr float kHalfTurn = std::numbers::pi_v<float>;
    switch (side)
    {
    case ViewSide::Right:
        return behind + kViewOffset;
    case ViewSide::Left:
        return behind - kViewOffset;
    case ViewSide::Front:
        return behind + kHalfTurn;
    case ViewSide::FrontRight:
        return behind + kHalfTurn - kViewOffset;
    case ViewSide::FrontLeft:
        return behind + kHalfTurn + kViewOffset;
    case ViewSide::Behind:
        break;
    }
    return behind;
}

std::string_view ViewSideWords(ViewSide side)
{
    switch (side)
    {
    case ViewSide::Right:
        return "behind it to its right";
    case ViewSide::Left:
        return "behind it to its left";
    case ViewSide::Front:
        return "in front of it";
    case ViewSide::FrontRight:
        return "in front of it to its right";
    case ViewSide::FrontLeft:
        return "in front of it to its left";
    case ViewSide::Behind:
        break;
    }
    return "behind it";
}

std::optional<float> YawToward(const Vec3 &from, const Vec3 &at)
{
    const float x = from.x - at.x;
    const float y = from.y - at.y;
    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0.0f && y == 0.0f))
    {
        return std::nullopt;
    }
    return std::atan2(y, x);
}

float OrbitDistance(float zoom, float close)
{
    return zoom * close + (1.0f - zoom) * kFarGame;
}

namespace
{

// A clear distance that is none is nothing clear.
float ClearOf(float meters)
{
    return std::isfinite(meters) && meters > 0.0f ? meters : 0.0f;
}

// The choice among a row's three places, named by `sides` in the order they
// are liked; nothing where none of the three is a place to stand.
std::optional<ViewStand> ChooseOf(float zoom, const std::optional<float> &asked, const ViewRow &row,
                                  const std::array<ViewSide, 3> &sides)
{
    struct Place
    {
        ViewSide side;
        float clear;
        bool ground;
    };
    const std::array<Place, 3> places{Place{sides[0], ClearOf(row.right.clear), row.right.ground},
                                      Place{sides[1], ClearOf(row.left.clear), row.left.ground},
                                      Place{sides[2], ClearOf(row.straight.clear), row.straight.ground}};
    const float whole = OrbitDistance(zoom, asked.value_or(kCloseGame));
    for (const Place &place : places)
    {
        if (place.ground && place.clear >= whole)
        {
            return ViewStand{ViewAnswer::Stand, place.side, whole, asked};
        }
    }
    // None the whole way: the one clear the furthest, the earlier of two
    // alike.
    const Place *best = nullptr;
    for (const Place &place : places)
    {
        if (place.ground && (best == nullptr || place.clear > best->clear))
        {
            best = &place;
        }
    }
    if (best == nullptr)
    {
        return std::nullopt;
    }
    const float meters = StandDistance(zoom, asked, best->clear);
    if (!(meters > OrbitDistance(zoom, kCloseGame)))
    {
        return ViewStand{ViewAnswer::Stand, best->side, meters, std::nullopt}; // the game's own
    }
    // The close distance that puts the camera there: the orbit's blend
    // undone. The zoom is not nothing here, since only then is the distance
    // asked another than the game's own, and this one between the two.
    return ViewStand{ViewAnswer::Stand, best->side, meters, (meters - (1.0f - zoom) * kFarGame) / zoom};
}

} // namespace

float StandDistance(float zoom, const std::optional<float> &asked, float clear)
{
    const float wanted = OrbitDistance(zoom, asked.value_or(kCloseGame));
    const float least = OrbitDistance(zoom, kCloseGame);
    const float sane = ClearOf(clear);
    if (sane >= wanted)
    {
        return wanted;
    }
    return std::max(least, sane - kViewMargin); // never nearer than the game's own
}

bool IsPlace(const GroundUnder &under)
{
    return under.ground && under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;
}

ViewStand ChooseStand(float zoom, const std::optional<float> &asked, const ViewRow &back,
                      const std::optional<ViewRow> &front)
{
    if (const std::optional<ViewStand> behind =
            ChooseOf(zoom, asked, back, {ViewSide::Right, ViewSide::Left, ViewSide::Behind}))
    {
        return *behind;
    }
    if (!front)
    {
        return ViewStand{ViewAnswer::AskFront, ViewSide::Behind, 0.0f, std::nullopt};
    }
    if (const std::optional<ViewStand> before =
            ChooseOf(zoom, asked, *front, {ViewSide::FrontRight, ViewSide::FrontLeft, ViewSide::Front}))
    {
        return *before;
    }
    return ViewStand{ViewAnswer::WithParty, ViewSide::Behind, 0.0f, std::nullopt};
}

float ClearDistance(const Vec3 &from, const Vec3 &to, const Vec3 &cut, float wanted)
{
    const float whole = Distance(from, to);
    const float clear = Distance(from, cut);
    if (!std::isfinite(whole) || !std::isfinite(clear) || !(whole > 0.0f))
    {
        return 0.0f;
    }
    return wanted * std::min(1.0f, clear / whole);
}

PortraitChoice ChoosePortrait(std::int32_t appearance, std::wstring_view appearanceLabel, bool person,
                              std::wstring_view tag, const std::vector<PortraitRow> &rows)
{
    const auto own = [&](const PortraitRow &row) { return appearance > 0 && row.appearance == appearance; };
    for (const PortraitRow &row : rows)
    {
        if (own(row) && !row.made.empty())
        {
            return {PortraitSource::ReadyMade, row.made, false};
        }
    }
    for (const PortraitRow &row : rows)
    {
        if (own(row) || (!tag.empty() && row.tag == tag))
        {
            return {PortraitSource::Render, {}, true}; // the table has its camera
        }
    }
    if (person)
    {
        return {PortraitSource::Render, {}, false};
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
            return {PortraitSource::ReadyMade, row.made, false}; // the plain one of its family
        }
        if (kin == nullptr)
        {
            kin = &row;
        }
    }
    if (kin != nullptr)
    {
        return {PortraitSource::ReadyMade, kin->made, false};
    }
    // No picture of it or its kin, and no camera of the table's: rendered,
    // framed by its kind and size.
    return {PortraitSource::Render, {}, false};
}

} // namespace tb
