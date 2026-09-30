#include "game/Bar.h"

#include "core/Bar.h"
#include "core/Text.h"

#include "dao/ClientExoApp.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Objects.h"

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
        const std::lock_guard lock(mutex_);
        movieLetGo_ = true;
    });
    spdlog::info("the turn bar's calls are answered: TB_GetView, TB_EndTurn, TB_Focus, TB_Cleared");
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

void BarLink::Publish(const TurnView &view)
{
    std::wstring encoded = EncodeBar(view, portraits_);
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
}

void BarLink::FollowFight(const TurnView &view, std::uint32_t nowMs)
{
    const std::size_t known = portraits_.Tried();
    portraits_.Follow(view, nowMs);
    if (known > 0 && portraits_.Tried() == 0)
    {
        spdlog::debug("the fight is over: its {} portraits forgotten", known);
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
    spdlog::info("released {} of the fight's {} rendered portraits, {}", released, releases.size(),
                 letGo ? "the bar having let them go" : "the bar not having said it let them go");
}

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
        url = creature->PortraitImage();
        how = "the game's";
        if (url.empty() && render)
        {
            url = creature->RenderPortrait();
            how = "rendered";
            rendered = true;
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

std::optional<ObjectId> BarLink::TakeFocus()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(focus_, std::nullopt);
}

} // namespace tb::game
