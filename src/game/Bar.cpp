#include "game/Bar.h"

#include "core/Bar.h"
#include "core/Text.h"

#include "dao/Abilities.h"
#include "dao/ClientExoApp.h"
#include "dao/Gda.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Objects.h"
#include "dao/core/GdaHash.h"
#include "dao/generated/ScriptApi.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tb::game
{

void BarLink::Install()
{
    if (installed_)
    {
        return;
    }
    installed_ = true;
    dao::gui::OnExternalInterface("TB_GetView", [this](const dao::gui::Call &call) {
        // Called from the game's UI code, which cannot take an exception.
        try
        {
            std::wstring view;
            {
                const std::lock_guard lock(mutex_);
                view = encoded_;
            }
            // GFx copies the text as the call returns, so a local will do.
            call.Return(dao::gui::Value::WString(view.c_str()));
            HideGameBanner(fightShown_.load());
        }
        catch (const std::exception &e)
        {
            spdlog::error("TB_GetView: {}", e.what());
        }
    });
    dao::gui::OnFSCommand("TB_EndTurn", [this](void *, std::string_view) {
        const std::lock_guard lock(mutex_);
        endTurn_ = true;
    });
    dao::gui::OnFSCommand("TB_Delay", [this](void *, std::string_view) {
        const std::lock_guard lock(mutex_);
        delay_ = true;
    });
    dao::gui::OnFSCommand("TB_Focus", [this](void *, std::string_view args) {
        ObjectId id = kNoObject;
        const auto [end, error] = std::from_chars(args.data(), args.data() + args.size(), id);
        if (error != std::errc{} || end != args.data() + args.size())
        {
            spdlog::warn("TB_Focus with '{}': not an object id", args);
            return;
        }
        const std::lock_guard lock(mutex_);
        focus_ = id;
    });
    dao::gui::OnFSCommand("TB_Cleared", [this](void *, std::string_view) {
        {
            const std::lock_guard lock(mutex_);
            movieLetGo_ = true;
        }
        // The fight is over: the game's banner is the game's again.
        try
        {
            HideGameBanner(false);
        }
        catch (const std::exception &e)
        {
            spdlog::error("TB_Cleared: {}", e.what());
        }
    });
    spdlog::info("the turn bar's calls are answered: TB_GetView, TB_EndTurn, TB_Delay, TB_Focus, TB_Cleared");
}

namespace
{
constexpr std::wstring_view kScreen = L"TurnOrder";
constexpr std::wstring_view kMovie = L"turnorder";
} // namespace

void BarLink::KeepScreen(bool fighting)
{
    if (!screenAdded_)
    {
        // False until the GUI system exists, which is from the main menu on.
        if (!dao::gui::AddScreen(kScreen, kMovie, dao::gui::kLayerHud))
        {
            return;
        }
        screenAdded_ = true;
        // A plain SWF is looked for first, the game's own GFX after. Which
        // one the resource system found says whether the override folder
        // is searched for movies (its search order is not read yet).
        const bool swf = dao::gui::HasMovie(L"turnorder.swf");
        const bool gfx = !swf && dao::gui::HasMovie(L"turnorder.gfx");
        movieFound_ = swf || gfx;
        if (movieFound_)
        {
            spdlog::info("the turn bar's screen is added, and its movie found as turnorder.{}", swf ? "swf" : "gfx");
        }
        else
        {
            spdlog::warn("the turn bar's screen is added, but no turnorder.swf or .gfx is where the game looks "
                         "(tools/deploy.ps1 puts it in the override folder): the bar stays hidden");
        }
    }
    if (!movieFound_)
    {
        return;
    }
    if (fighting && !screenShown_)
    {
        screenShown_ = dao::gui::ShowScreen(kScreen);
        spdlog::info("the turn bar {}", screenShown_ ? "is shown" : "could not be shown");
    }
    else if (!fighting && screenShown_ && !portraits_.AwaitingMovie())
    {
        // After a fight it stays up, drawing nothing, until its movie has
        // let the portraits go (FollowFight): a hidden movie may not run.
        screenShown_ = false;
        if (!dao::gui::HideScreen(kScreen))
        {
            spdlog::warn("the turn bar could not be hidden");
        }
    }
}

// The bar of time is the movie's own, under the medallions, made of the
// experience bar's art (ui/turnorder: drawTime). For half a day it was the
// game's experience bar itself, filled through the quickbar's movie with an
// order's cost marked on it by a script added to that movie; the user,
// 2026-10-02, having seen it: "the action bar seems to replace the
// experience bar? that's confusing. we should have it separate." The game's
// bar is the game's again, and nothing of its movie is touched.
void BarLink::Publish(const TurnView &view)
{
    // A pending ability's icon and name, as the game's quickbar would show
    // them: the abilities table's `icon` (a texture's name, which the
    // quickbar's movie loads as img://<name>.dds) and the name by its
    // string reference (DAOLib's dao::abilities::Name). Asked once an
    // ability, from the frame callback.
    TurnView shown = view;
    for (CardView &card : shown.upcoming)
    {
        if (card.owedMs == 0 || card.pendingAbility < 0)
        {
            continue;
        }
        auto known = abilityArt_.find(card.pendingAbility);
        if (known == abilityArt_.end())
        {
            AbilityArt found;
            for (const char *column : {"icon", "Icon"})
            {
                if (found.icon.empty())
                {
                    found.icon = dao::gda::GetString(dao::script::Table::AbilitiesTalents, card.pendingAbility, column)
                                     .value_or(std::wstring{});
                }
            }
            found.name = dao::abilities::Name(card.pendingAbility).value_or(std::wstring{});
            spdlog::info("ability {}: its icon is '{}', its name '{}'", card.pendingAbility, Utf8(found.icon),
                         Utf8(found.name));
            known = abilityArt_.emplace(card.pendingAbility, std::move(found)).first;
        }
        card.pendingIcon = known->second.icon;
        card.pendingName = known->second.name;
    }
    fightShown_.store(view.active);
    std::wstring encoded = EncodeBar(shown, portraits_);
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
}

// The game writes "Paused" across the top of the screen when it is paused
// (floatylayer.gfx, read 2026-10-02 with FFDec: PauseIndicator_mc, aligned
// top and centre, played in and out by frames as Client.Paused changes),
// which is where the turn bar is: its ends showed beside the cards and its
// tail between them (play test 30). For a day the bar's plate was made solid
// black over it while paused; the user, seeing that: "pausing makes the
// background turn black?" So the banner is hidden while a turn-based fight
// has the bar up, whose caption says "Paused", and shown again as the fight
// ends. Its `_visible` is not touched by the game's script, which only
// plays its frames; the border the game draws round the screen while paused
// (PauseBorder_mc) is left as it is.
void BarLink::HideGameBanner(bool hide)
{
    if (bannerHidden_ == hide)
    {
        return;
    }
    constexpr const char *kBanner = "_root.PauseIndicator_mc";
    void *owner = dao::gui::FindOwner(L"FloatyLayer");
    dao::gui::Movie movie{owner != nullptr ? dao::gui::MovieOf(owner) : nullptr};
    if (!movie || !movie.IsAvailable(kBanner))
    {
        if (!std::exchange(bannerSaid_, true))
        {
            spdlog::warn("the game's Paused banner is not there to hide (the floaty layer's {})", kBanner);
        }
        return;
    }
    const bool set = movie.SetVariable("_root.PauseIndicator_mc._visible", dao::gui::Value::Bool(!hide),
                                       dao::gui::Movie::SetType::Normal);
    bannerHidden_ = hide;
    spdlog::info("the game's Paused banner is {}: {}", hide ? "hidden behind the turn bar" : "shown again",
                 set ? "set" : "the call did not run");
}

void BarLink::FollowFight(const TurnView &view, std::uint32_t nowMs)
{
    const std::size_t known = portraits_.Tried();
    portraits_.Follow(view, nowMs);
    if (known > 0 && portraits_.Tried() == 0)
    {
        spdlog::debug("nothing to show (the fight over, or the bar turned off): its {} portraits forgotten", known);
    }
    bool letGo = false;
    {
        const std::lock_guard lock(mutex_);
        letGo = std::exchange(movieLetGo_, false);
    }
    if (letGo)
    {
        portraits_.MovieLetGo();
    }
    const std::vector<ObjectId> releases = portraits_.TakeReleases(nowMs);
    if (releases.empty())
    {
        return;
    }
    // As the game's screens do when they close: the renderer and its clone
    // of the creature go now, not with the area (core/Portraits.h).
    auto *app = dao::CClientExoApp::Get();
    std::size_t released = 0;
    for (const ObjectId id : releases)
    {
        if (dao::CCCreature *creature = app != nullptr ? app->GetCreatureById(id) : nullptr)
        {
            creature->ReleasePortrait();
            ++released;
        }
    }
    // A creature the game has already destroyed (a body gone) cannot be
    // asked: its renderer went with it, or not (dev/DAOLIB_NEEDS.md, check
    // 16's memory question).
    spdlog::info("released {} of the fight's {} rendered portraits ({} of their creatures gone already), {}", released,
                 releases.size(), releases.size() - released,
                 letGo ? "the bar having let them go" : "the bar not having said it let them go");
}

namespace
{
// The game's portraits table (portraits.gda, table 7), which gives the
// portrait renderer its camera by tag or appearance, and for some
// appearances names a ready-made picture instead (Override: portrait_49 a
// wolf, 09 and 68 bears, 59 and 57 spiders, 14 the swarm, 06 the golem, 12,
// 28 and 37 forms of the Fade, textures in gui.erf, as the party bar shows
// a ranger's summons). Read once: the table does not change in a game.
// Which picture a creature gets from it is core's (ChoosePortrait).
constexpr std::int32_t kPortraitsTable = 7;

const std::vector<PortraitRow> &PortraitRows()
{
    static std::vector<PortraitRow> rows;
    if (!rows.empty())
    {
        return rows;
    }
    const std::int32_t count = dao::gda::Rows(kPortraitsTable).value_or(0);
    const auto text = [](std::int32_t row, std::string_view column) {
        return dao::gda::GetString(kPortraitsTable, row, dao::core::GdaColumnHash(column)).value_or(std::wstring{});
    };
    for (std::int32_t row = 0; row < count; ++row)
    {
        PortraitRow r;
        r.appearance = dao::gda::GetInt(kPortraitsTable, row, dao::core::GdaColumnHash("Appearance")).value_or(0);
        r.label = text(row, "Label");
        r.tag = text(row, "Tag");
        r.made = text(row, "Override");
        rows.push_back(std::move(r));
    }
    return rows;
}

// A person (the appearance table's ModelType H, or P for the races a player
// is of) has a face the default camera frames; W and S are creatures. An
// appearance the table does not know is taken for a person's, as before.
struct Looks
{
    std::int32_t appearance = 0;
    std::wstring label;
    bool person = true;
};

Looks LooksOf(ObjectId id)
{
    Looks looks;
    looks.appearance = dao::script::api::GetAppearanceType(id);
    const auto type = dao::gda::GetString(dao::script::Table::Appearance, looks.appearance, "ModelType");
    looks.label =
        dao::gda::GetString(dao::script::Table::Appearance, looks.appearance, "Label").value_or(std::wstring{});
    looks.person = !type || type->empty() || *type == L"H" || *type == L"P";
    return looks;
}
} // namespace

void BarLink::KeepPortraits(const TurnView &view, bool render)
{
    const auto id = portraits_.Next(view);
    if (!id)
    {
        return;
    }
    auto *app = dao::CClientExoApp::Get();
    dao::CCCreature *creature = app != nullptr ? app->GetCreatureById(*id) : nullptr;
    std::wstring url;
    const char *how = "none";
    bool rendered = false;
    if (creature != nullptr)
    {
        const Looks looks = LooksOf(*id);
        const PortraitChoice choice =
            ChoosePortrait(looks.appearance, looks.label, looks.person, dao::script::api::GetTag(*id), PortraitRows());
        switch (choice.source)
        {
        case PortraitSource::ReadyMade:
            url = L"img://" + choice.made;
            how = "the portraits table's";
            break;
        case PortraitSource::Render:
            url = creature->PortraitImage();
            how = "the game's";
            if (url.empty() && render)
            {
                url = creature->RenderPortrait();
                how = "rendered";
                rendered = !url.empty();
            }
            break;
        case PortraitSource::None:
            // No picture and no camera for it: a render would be a person's
            // framing of a beast.
            how = "no picture or camera for its kind";
            spdlog::debug("{:#010x} is a {} (appearance {}): the game has no picture of it or its kin, and no camera",
                          *id, Utf8(looks.label), looks.appearance);
            break;
        }
    }
    const auto card = std::ranges::find(view.upcoming, *id, &CardView::id);
    const std::string name = card != view.upcoming.end() ? Utf8(card->name) : std::string{};
    if (url.empty())
    {
        spdlog::info("{} ({:#010x}) has no portrait{}: its card shows initials", name, *id,
                     creature == nullptr ? ", nor a creature by its id"
                     : render            ? ""
                                         : " (portraits = false)");
    }
    else
    {
        spdlog::info("{}'s portrait: {} ({})", name, Utf8(url), how);
    }
    portraits_.Got(*id, std::move(url), rendered);
}

bool BarLink::TakeEndTurn()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(endTurn_, false);
}

bool BarLink::TakeDelay()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(delay_, false);
}

std::optional<ObjectId> BarLink::TakeFocus()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(focus_, std::nullopt);
}

} // namespace tb::game
