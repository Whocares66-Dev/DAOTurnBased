#include "game/Bar.h"

#include "core/Bar.h"
#include "core/Sheet.h"
#include "core/Text.h"
#include "game/Camera.h"
#include "game/Sheet.h"

#include "dao/Abilities.h"
#include "dao/ClientExoApp.h"
#include "dao/Gda.h"
#include "dao/Gui.h"
#include "dao/Log.h"
#include "dao/Objects.h"
#include "dao/Portraits.h"
#include "dao/Resources.h"
#include "dao/World.h"
#include "dao/core/GdaHash.h"
#include "dao/generated/ScriptApi.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <string>
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
    // An arrow at an end of the row (issue 23): 1 to later turns, -1 to
    // earlier. Only which way: how far a click goes is core's.
    dao::gui::OnFSCommand("TB_Scroll", [this](void *, std::string_view args) {
        int way = 0;
        const auto [end, error] = std::from_chars(args.data(), args.data() + args.size(), way);
        if (error != std::errc{} || end != args.data() + args.size() || way == 0)
        {
            spdlog::warn("TB_Scroll with '{}': not a way to move the row (1 to later turns, -1 to earlier)", args);
            return;
        }
        const std::lock_guard lock(mutex_);
        scroll_ += way > 0 ? 1 : -1;
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
    // A portrait put up large (TryPortrait's `show`): what to draw, and the
    // movie's word that it has drawn it and let the one before go.
    dao::gui::OnExternalInterface("TB_GetShow", [this](const dao::gui::Call &call) {
        try
        {
            std::wstring show;
            {
                const std::lock_guard lock(mutex_);
                show = showEncoded_;
            }
            call.Return(dao::gui::Value::WString(show.c_str()));
        }
        catch (const std::exception &e)
        {
            spdlog::error("TB_GetShow: {}", e.what());
        }
    });
    dao::gui::OnFSCommand("TB_ShowCleared", [this](void *, std::string_view args) {
        std::uint32_t serial = 0;
        const auto [end, error] = std::from_chars(args.data(), args.data() + args.size(), serial);
        if (error != std::errc{} || end != args.data() + args.size())
        {
            spdlog::warn("TB_ShowCleared with '{}': not the serial of a picture shown", args);
            return;
        }
        const std::lock_guard lock(mutex_);
        showDrawn_ = std::max(showDrawn_, serial);
    });
    // A card under the pointer (issue 112): whose sheet to read, and the
    // sheet as last read. An empty argument, or anything not an id, is the
    // pointer off every card.
    dao::gui::OnFSCommand("TB_Inspect", [this](void *, std::string_view args) {
        ObjectId id = kNoObject;
        const auto [end, error] = std::from_chars(args.data(), args.data() + args.size(), id);
        const std::lock_guard lock(mutex_);
        inspectAsked_ = error == std::errc{} && end == args.data() + args.size() ? id : kNoObject;
    });
    dao::gui::OnExternalInterface("TB_GetSheet", [this](const dao::gui::Call &call) {
        try
        {
            std::wstring sheet;
            {
                const std::lock_guard lock(mutex_);
                sheet = sheetEncoded_;
            }
            call.Return(dao::gui::Value::WString(sheet.c_str()));
        }
        catch (const std::exception &e)
        {
            spdlog::error("TB_GetSheet: {}", e.what());
        }
    });
    spdlog::info("the turn bar's calls are answered: TB_GetView, TB_EndTurn, TB_Delay, TB_Focus, TB_Scroll, "
                 "TB_Cleared, TB_GetShow, TB_ShowCleared, TB_Inspect, TB_GetSheet");
}

bool BarLink::FollowRow(const TurnView &view)
{
    int clicks = 0;
    {
        const std::lock_guard lock(mutex_);
        clicks = std::exchange(scroll_, 0);
    }
    const BarSpan was = row_;
    window_.Scroll(clicks);
    row_ = window_.Follow(view);
    // Said when the row has moved in a fight: by an arrow, or with the order
    // (one before it gone, or the list too short for where it stood). Not
    // at a fight's end, when it goes back to the beginning with the bar.
    if (view.active && was.shown > 0 && row_.before != was.before)
    {
        spdlog::info("the turn bar's row shows turns {} to {} of {} ({}); it began at turn {}", row_.before + 1,
                     row_.before + row_.shown, row_.before + row_.shown + row_.after,
                     clicks > 0   ? "its right arrow"
                     : clicks < 0 ? "its left arrow"
                                  : "moved with the order: one before it gone, or the list shorter",
                     was.before + 1);
    }
    else if (clicks != 0)
    {
        spdlog::debug("an arrow of the turn bar's was clicked and the row stays at turns {} to {} of {}",
                      row_.before + 1, row_.before + row_.shown, row_.before + row_.shown + row_.after);
    }
    return clicks != 0;
}

namespace
{
constexpr std::wstring_view kScreen = L"TurnOrder";
constexpr std::wstring_view kMovie = L"turnorder";
// The game's icon for a plain attack: what its party bar shows beside a
// portrait while its creature attacks. Read from the running game,
// 2026-10-03: the data tree's Party.Members.<n>.CurrentActionIcon is
// "ic_attack" with CurrentAction "Attacking" (a walk's is "ico_MovetoPoint",
// a talent's its own "tlt_ico_..."), loaded by portraits.gfx as any icon.
constexpr std::wstring_view kAttackIcon = L"ic_attack";
// And its icon for a move, the party bar's for a creature walking (above):
// for a card whose creature's walk the meters stopped (issue 83). Read again
// in the game's files, 2026-10-09: of the 43 rows of its table of commands
// (commands.gda, in 2da.erf) two have an `icon`, row 1, Attack, "ic_attack",
// and row 20, Move To Point, "ico_MovetoPoint"; and both are textures of 42 x
// 42 in the shared atlas the game's movies draw from (guiexport.erf's
// atl_shared_dxt1_dat.xml), the attack's a crosshair, the move's four darts
// about a four-pointed star, as the movement marker on the ground is.
constexpr std::wstring_view kMoveIcon = L"ico_MovetoPoint";
} // namespace

// The bar's screen added to the game's, once, and its movie looked for:
// said in the log, with where the movie was found. False while the game has
// no GUI system to add it to.
bool BarLink::AddScreen()
{
    // False until the GUI system exists, which is from the main menu on.
    if (!dao::gui::AddScreen(kScreen, kMovie, dao::gui::kLayerHud))
    {
        return false;
    }
    // A plain SWF is looked for first, the game's own GFX after. The
    // movie is the plugin's own, beside turnbased.dll, since issue 47
    // (2026-10-05; DAOLib's issue 26, f442300): AddScreen has it served
    // from the plugin's folder where the game's resource system has none
    // by the name, and HasMovie answers for both places. The game's
    // comes first, and the one it could have is a copy a deploy before
    // then left in the override folder: an older movie shown with this
    // plugin, when the two are a pair (TB_GetView's fields), so that is
    // a warning. tools/deploy.ps1 takes the copy out.
    const bool swf = dao::gui::HasMovie(L"turnorder.swf");
    const bool gfx = !swf && dao::gui::HasMovie(L"turnorder.gfx");
    movieFound_ = swf || gfx;
    const bool gameSwf = dao::resources::Exists(L"turnorder.swf");
    const bool gameGfx = !gameSwf && dao::resources::Exists(L"turnorder.gfx");
    if (gameSwf || gameGfx)
    {
        spdlog::warn("the turn bar's screen is added, and its movie found as turnorder.{} by the game's own "
                     "resource system, which comes before the plugin's folder: a copy in the override folder "
                     "is the one shown, not the one beside turnbased.dll (tools/deploy.ps1 takes it out)",
                     gameSwf ? "swf" : "gfx");
    }
    else if (movieFound_)
    {
        spdlog::info("the turn bar's screen is added, and its movie found as turnorder.{} beside the plugin",
                     swf ? "swf" : "gfx");
    }
    else
    {
        spdlog::warn("the turn bar's screen is added, but no turnorder.swf or .gfx is beside the plugin "
                     "(tools/deploy.ps1 puts it there, in daolib\\plugins\\turnbased): the bar stays hidden");
    }
    return true;
}

void BarLink::KeepScreen(bool fighting)
{
    if (!screenAdded_)
    {
        if (!AddScreen())
        {
            return;
        }
        screenAdded_ = true;
    }
    if (!movieFound_)
    {
        return;
    }
    // Up for a fight, and for a portrait put up large outside one
    // (TryPortrait's `show`), whose movie it is too: with no fight it draws
    // no bar, only that picture.
    const bool up = fighting || showing_.has_value() || !showLeaving_.empty();
    if (up && !screenShown_)
    {
        screenShown_ = dao::gui::ShowScreen(kScreen);
        spdlog::info("the turn bar {}{}", screenShown_ ? "is shown" : "could not be shown",
                     fighting ? "" : " (no fight: for a portrait put up large)");
    }
    else if (!up && screenShown_ && !portraits_.AwaitingMovie())
    {
        // After a fight it stays up, drawing nothing, until its movie has
        // let the portraits go (FollowFight): a hidden movie may not run.
        // And so for a picture taken down (showLeaving_).
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
        if (card.owedMs == 0)
        {
            // Nothing pending, and its walk stopped by the meters in its
            // last turn: the game's own icon for a move in the same place
            // (issue 83). With something pending the card shows that.
            if (card.walkCut)
            {
                card.icon = std::wstring{kMoveIcon};
            }
            continue;
        }
        if (card.pendingAbility < 0)
        {
            // A plain attack kept for its creature's next turn (core keeps
            // attacks and abilities, nothing else): the game's own icon for
            // an attack, the one its party bar shows beside a portrait for
            // the command being carried out. Left off after play test 28
            // ("with four spiders every card had a badge"); the user,
            // 2026-10-03, with a turn a bar of time: "There's an icon for
            // plain attack. What actor has that queued up for the next
            // round, that should also indicate in the top right hand corner
            // of the portrait, as with a pending talent" (issue 15).
            card.icon = std::wstring{kAttackIcon};
            card.pendingName = L"Attack";
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
        card.icon = known->second.icon;
        card.pendingName = known->second.name;
    }
    fightShown_.store(view.active);
    // The view's size, by which the movie lays the bar out (issue 118): the
    // engine's own word, read with each view, so that a window resized or a
    // device reset reaches the bar as new numbers.
    ViewPixels screen;
    if (const std::optional<dao::world::ViewSize> size = dao::world::ScreenSize())
    {
        screen = {.width = size->width, .height = size->height};
    }
    std::wstring encoded = EncodeBar(shown, portraits_, row_, screen);
    const std::lock_guard lock(mutex_);
    encoded_ = std::move(encoded);
}

void BarLink::KeepSheet(const TurnView &view, std::uint32_t nowMs)
{
    ObjectId asked = kNoObject;
    {
        const std::lock_guard lock(mutex_);
        asked = inspectAsked_;
    }
    constexpr std::uint32_t kSheetEveryMs = 250;
    if (asked == inspected_ && (asked == kNoObject || nowMs - sheetReadMs_ < kSheetEveryMs))
    {
        return;
    }
    std::wstring encoded;
    const auto card = std::ranges::find(view.upcoming, asked, &CardView::id);
    if (asked != kNoObject && card != view.upcoming.end())
    {
        const CreatureSheet sheet = BuildSheet(ReadSheet(asked, card->name, card->mana, asked != inspected_));
        if (sheet.id != kNoObject)
        {
            encoded = EncodeSheet(sheet);
        }
        if (asked != inspected_)
        {
            spdlog::debug("{}'s sheet is asked for: {}", Utf8(card->name),
                          sheet.id != kNoObject ? Utf8(sheet.level) : std::string{"the engine has no such creature"});
        }
    }
    inspected_ = asked;
    sheetReadMs_ = nowMs;
    const std::lock_guard lock(mutex_);
    sheetEncoded_ = std::move(encoded);
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
//
// The layer is not always there. With no layer to hide the banner in while a
// fight shows its bar, something is not as it was read, and that is said,
// once. With none to show it again in, the game has closed the layer over
// what ended the fight, which is its ordinary way: a load from the middle of
// one (a scene that moves the party on, a save loaded) and the party fallen.
// Nothing is said then, and the frame asks again until the layer is back
// (FollowFight), where `shown again` says it was done. Until 2026-10-09 that
// was the same warning, the session's one: three of a day's eleven warnings,
// each mended by itself a moment or a minute later (issue 89; the
// Blackmarsh, 22:13:18 and `shown again: set` at 22:14:20).
void BarLink::HideGameBanner(bool hide)
{
    if (bannerHidden_.load() == hide)
    {
        return;
    }
    constexpr const char *kBanner = "_root.PauseIndicator_mc";
    void *owner = dao::gui::FindOwner(L"FloatyLayer");
    dao::gui::Movie movie{owner != nullptr ? dao::gui::MovieOf(owner) : nullptr};
    if (!movie || !movie.IsAvailable(kBanner))
    {
        if (hide && !std::exchange(bannerSaid_, true))
        {
            spdlog::warn("the game's Paused banner is not there to hide (the floaty layer's {})", kBanner);
        }
        return;
    }
    const bool set = movie.SetVariable("_root.PauseIndicator_mc._visible", dao::gui::Value::Bool(!hide),
                                       dao::gui::Movie::SetType::Normal);
    bannerHidden_.store(hide);
    spdlog::info("the game's Paused banner is {}: {}", hide ? "hidden behind the turn bar" : "shown again",
                 set ? "set" : "the call did not run");
}

void BarLink::FollowFight(const TurnView &view, std::uint32_t nowMs)
{
    // The fight over (or a conversation in it): the game's banner is the
    // game's again. Here, on the frame, since the bar's movie stops asking
    // for its view as the bar goes, and its "cleared" call did not come:
    // after a fight a pause in real time showed no banner at all (play test
    // 32).
    if (!view.active && bannerHidden_.load())
    {
        HideGameBanner(false);
    }
    FollowShow(nowMs);
    const std::size_t known = portraits_.Tried();
    portraits_.Follow(view, nowMs);
    if (known > 0 && portraits_.Tried() == 0)
    {
        spdlog::debug("nothing to show (the fight over, or the bar turned off): its {} portraits forgotten", known);
        headTries_.clear();
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
    std::size_t kept = 0;
    for (const ObjectId id : releases)
    {
        // One the picture put up large is of, or was until a moment ago: the
        // movie may be drawing it yet. It is released with that picture
        // (FollowShow), not under it.
        if (toolOwned_.contains(id) || ShowHolds(id))
        {
            toolOwned_.try_emplace(id, nowMs);
            ++kept;
            continue;
        }
        if (dao::CCCreature *creature = app != nullptr ? app->GetCreatureById(id) : nullptr)
        {
            creature->ReleasePortrait();
            ++released;
        }
        // The camera it was framed by goes with it: ids are reused once an
        // area is left, and DAOLib keeps a camera by its creature's id.
        dao::portraits::ClearCamera(id);
    }
    // A creature the game has already destroyed (a body gone) cannot be
    // asked: its renderer went with it, or not (dev/DAOLIB_NEEDS.md, check
    // 16's memory question).
    spdlog::info("released {} of the fight's {} rendered portraits ({} of their creatures gone already{}), {}",
                 released, releases.size(), releases.size() - released - kept,
                 kept > 0 ? std::format(", {} kept for the picture put up large", kept) : std::string{},
                 letGo ? "the bar having let them go" : "the bar not having said it let them go");
}

bool BarLink::ShowHolds(ObjectId id) const
{
    return (showing_ && showing_->id == id) ||
           std::ranges::find(showLeaving_, id, &ShowLeaving::id) != showLeaving_.end();
}

void BarLink::SetShow(const PortraitReport *report, std::uint32_t seconds, std::uint32_t nowMs)
{
    if (report == nullptr && !showing_)
    {
        return; // nothing up, nothing to take down
    }
    ++showSerial_;
    if (showing_)
    {
        showLeaving_.push_back(ShowLeaving{showing_->id, showSerial_, nowMs});
    }
    std::wstring encoded;
    if (report != nullptr)
    {
        showing_ = Showing{report->id, nowMs, seconds * 1000};
        encoded = EncodeShow(showSerial_, report->image, ShowTitle(*report), ShowDetail(*report));
    }
    else
    {
        showing_.reset();
        encoded = EncodeShow(showSerial_, {}, {}, {});
    }
    const std::lock_guard lock(mutex_);
    showEncoded_ = std::move(encoded);
}

void BarLink::FollowShow(std::uint32_t nowMs)
{
    auto *app = dao::CClientExoApp::Get();
    const auto creatureOf = [app](ObjectId id) { return app != nullptr ? app->GetCreatureById(id) : nullptr; };
    // Taken down by itself after its time, and when its creature is gone: a
    // picture left up would keep its renderer until its creature's area
    // went, which is what the bar's own are released to avoid
    // (core/Portraits.h).
    if (showing_)
    {
        const bool over = nowMs - showing_->sinceMs >= showing_->forMs;
        if (over || creatureOf(showing_->id) == nullptr)
        {
            spdlog::info("the portrait put up large of {:#010x} is taken down: {}", showing_->id,
                         over ? "its time is over" : "its creature is gone");
            SetShow(nullptr, 0, nowMs);
        }
    }
    std::uint32_t drawn = 0;
    {
        const std::lock_guard lock(mutex_);
        drawn = showDrawn_;
    }
    std::erase_if(showLeaving_, [&](const ShowLeaving &leaving) {
        return drawn >= leaving.serial || nowMs - leaving.sinceMs >= PortraitBook::kReleaseWaitMs;
    });
    // Not in the frame it was rendered in: the engine's render marks the
    // picture to be drawn and returns (PortraitRenderer::Render, read
    // 2026-10-04), so a renderer released in the same call would go with its
    // picture still to draw. Half a second on, with nothing showing it.
    constexpr std::uint32_t kToolKeepMs = 500;
    for (auto owned = toolOwned_.begin(); owned != toolOwned_.end();)
    {
        const ObjectId id = owned->first;
        if (ShowHolds(id) || nowMs - owned->second < kToolKeepMs)
        {
            ++owned;
            continue;
        }
        if (!portraits_.Of(id).empty())
        {
            // A fight's card has a picture of it now: the renderer is the
            // bar's, released with the fight's others.
            portraits_.Got(id, std::wstring{portraits_.Of(id)}, true);
        }
        else
        {
            if (dao::CCCreature *creature = creatureOf(id))
            {
                creature->ReleasePortrait();
            }
            dao::portraits::ClearCamera(id);
            spdlog::debug(
                "{:#010x}'s portrait, rendered for the turnbased_portrait tool, is released: nothing shows it", id);
        }
        owned = toolOwned_.erase(owned);
    }
}

namespace
{
// The game's portraits table (portraits.gda, table 7), which gives the
// portrait renderer its camera by tag or appearance, and for some
// appearances names a ready-made picture instead (Override: portrait_49 a
// wolf, 09 and 68 bears, 59 and 57 spiders, 14 the swarm, 06 the golem, 12,
// 28 and 37 forms of the Fade, textures in gui.erf, as the party bar shows
// a ranger's summons). Read once: the table does not change in a game.
// Every row of it, by the row's own number, which the engine gives for its
// place (the scripts' GetM2DARowIdFromRowIndex): the table is every file
// whose name begins with "portraits", and a row of another file's has
// whatever number its maker gave it. The mod's own is 1,000,000, a Veil
// Tear's picture (issue 103, ui\portraits); read by counting from 0, as this
// was until then, it was passed over and the Tear's card kept its initials.
// Which picture a creature gets from it is core's (ChoosePortrait), and so
// is how a render is framed (FramePortrait, which looks the creature up in
// core/PortraitTable.h's table): a person's body by this table's Default row.
constexpr std::int32_t kPortraitsTable = 7;

struct PortraitTable
{
    std::vector<PortraitRow> rows;
    std::vector<PortraitCamera> cameras;    // each row's, at its place in `rows`
    PortraitCamera person = kShippedPerson; // its Default row: as the game ships it, until read
};

const PortraitTable &Table()
{
    static PortraitTable table;
    if (!table.rows.empty())
    {
        return table;
    }
    const std::int32_t count = dao::gda::Rows(kPortraitsTable).value_or(0);
    const auto text = [](std::int32_t row, std::string_view column) {
        return dao::gda::GetString(kPortraitsTable, row, dao::core::GdaColumnHash(column)).value_or(std::wstring{});
    };
    const auto number = [](std::int32_t row, std::string_view column) {
        return dao::gda::GetFloat(kPortraitsTable, row, dao::core::GdaColumnHash(column)).value_or(0.0f);
    };
    for (std::int32_t place = 0; place < count; ++place)
    {
        const std::int32_t row = dao::script::api::GetM2DARowIdFromRowIndex(kPortraitsTable, place);
        PortraitRow r;
        r.appearance = dao::gda::GetInt(kPortraitsTable, row, dao::core::GdaColumnHash("Appearance")).value_or(0);
        r.label = text(row, "Label");
        r.tag = text(row, "Tag");
        r.made = text(row, "Override");
        const PortraitCamera camera{number(row, "CameraPitch"), number(row, "CameraRotation"),
                                    number(row, "CameraDistance"), number(row, "CameraZAdjust"),
                                    number(row, "CameraXAdjust")};
        // The row a person's body is framed by, by its label: the table's
        // own where it has one with a distance, else the game's as shipped.
        if (camera.distance > 0.0f && r.label == L"Default")
        {
            table.person = camera;
        }
        table.rows.push_back(std::move(r));
        table.cameras.push_back(camera);
    }
    return table;
}

// The camera the table has for a creature, as the renderer finds its row:
// by its tag, else by its appearance (DAOLib's dao/Portraits.h). Null for a
// creature the table has none for.
const PortraitCamera *TableCameraOf(const PortraitTable &table, std::int32_t appearance, std::wstring_view tag)
{
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    {
        if (!tag.empty() && table.rows[i].tag == tag)
        {
            return &table.cameras[i];
        }
    }
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    {
        if (appearance > 0 && table.rows[i].appearance == appearance)
        {
            return &table.cameras[i];
        }
    }
    return nullptr;
}

// What the appearance table says of a creature's looks. `person` is its
// ModelType H, or P for the races a player is of, and an appearance the
// table does not know: a model put together as a person's is. Since
// 2026-09-30 it is what is rendered before its kin's ready-made picture is
// looked for (core's ChoosePortrait). It is not by itself a kind of body: the
// table's H has the ogre, the high dragon, the cat and the pig beside people
// (read 2026-10-04). A person's body is that, standing up
// (TerrainFollowPitch 0) and no bigger than a person (core's BodyKindOf).
struct Looks
{
    std::int32_t appearance = 0;
    std::wstring label;
    std::wstring modelType;
    bool person = true;
    float personalSpace = 0.0f;
    float terrainFollowPitch = 0.0f;
};

Looks LooksOf(ObjectId id)
{
    Looks looks;
    looks.appearance = dao::script::api::GetAppearanceType(id);
    const auto type = dao::gda::GetString(dao::script::Table::Appearance, looks.appearance, "ModelType");
    looks.label =
        dao::gda::GetString(dao::script::Table::Appearance, looks.appearance, "Label").value_or(std::wstring{});
    looks.modelType = type.value_or(std::wstring{});
    looks.person = !type || type->empty() || *type == L"H" || *type == L"P";
    looks.personalSpace =
        dao::gda::GetFloat(dao::script::Table::Appearance, looks.appearance, "PerSpace").value_or(0.0f);
    looks.terrainFollowPitch =
        dao::gda::GetFloat(dao::script::Table::Appearance, looks.appearance, "TerrainFollowPitch").value_or(0.0f);
    return looks;
}

// One creature's portrait: what to do for it.
struct ShotOrder
{
    PortraitTuning tuning;   // nothing given: the rule as built
    bool render = true;      // false: only what there is already, and `whyNot` says why nothing is rendered
    std::string_view whyNot; // that nothing would show a render
    bool again = false;      // the renderer it has is the mod's own, to be rendered again in place
};

// What a portrait's creature is, for the report: its name and tag, its
// appearance and what the table says of its model, where its head is, and
// the kind of body that makes it (asked for, or by the rule).
void Describe(PortraitReport &report, ObjectId id, const Looks &looks, const BodyMeasure &body, const ShotOrder &order)
{
    namespace api = dao::script::api;
    report.name = api::GetName(id);
    report.tag = api::GetTag(id);
    report.appearance = looks.appearance;
    report.label = looks.label;
    report.personalSpace = looks.personalSpace;
    report.personsModel = looks.person;
    report.terrainFollowPitch = looks.terrainFollowPitch;
    report.feet = body.feet;
    report.head = body.head;
    report.extent = body.extent.meters;
    report.extentBy = !body.head          ? "no head node"
                      : body.extent.world ? "its head a point in the world"
                                          : "its head a point about its own origin";
    report.kind = order.tuning.kind.value_or(BodyKindOf(looks.person, looks.terrainFollowPitch, report.extent));
    report.kindBy = order.tuning.kind
                        ? std::string{"asked for"}
                        : std::format("{} (ModelType {}, TerrainFollowPitch {:.0f}, {:.2f} m from feet to head)",
                                      BodyKindWhy(looks.person, looks.terrainFollowPitch, report.extent),
                                      looks.modelType.empty() ? std::string{"none"} : Utf8(looks.modelType),
                                      looks.terrainFollowPitch, report.extent);
}

// A portrait's framing, by the rule or as a tester tuned it: the camera,
// its lights, whether the engine is given any of it, and the rule in words.
void Frame(PortraitReport &report, const Looks &looks, const BodyMeasure &body, const ShotOrder &order,
           const PortraitTable &table, const PortraitChoice &choice)
{
    // Its framing, looked up (core's FramePortrait, in core/PortraitTable.h's
    // table): its appearance's own row, else the game's own row for it, else
    // its kind's default; with what a tester gave over whichever it is.
    const PortraitCamera *gamesOwn = choice.tableCamera ? TableCameraOf(table, looks.appearance, report.tag) : nullptr;
    const PortraitFrame frame = FramePortrait(looks.appearance, report.kind, order.tuning.kind.has_value(), gamesOwn,
                                              body.extent.meters, table.person);
    const auto light = [&frame](const std::optional<float> &given, float rows) {
        return given ? given : frame.lit ? std::optional<float>{rows} : std::nullopt;
    };
    report.camera = Tuned(frame.camera, order.tuning);
    report.framedBy = frame.by;
    report.row = frame.label;
    report.atMost = frame.atMost && !order.tuning.distance;
    report.lightX = light(order.tuning.lightX, frame.lightX);
    report.lightY = light(order.tuning.lightY, frame.lightY);
    report.lightZ = light(order.tuning.lightZ, frame.lightZ);
    // What the game's own table frames by itself, its own row for the
    // creature or its Default row for a person's body, is given nothing: the
    // engine reads its table as it has, and people's portraits are as they
    // have been. A row of the mod's table, and anything a tester gave, is
    // given whole.
    const bool games = frame.by == FramedBy::PortraitsTable || frame.by == FramedBy::Person;
    report.cameraGiven = !games || order.tuning.AnyValue();
    report.rule = FramingRule(frame);
    if (order.tuning.AnyValue())
    {
        report.rule += ", with the values given";
    }
    if (!body.head)
    {
        report.rule += ", about its root at the scene's origin, its model having no head node";
    }
}

// A portrait rendered by the engine with the camera the report has (or the
// table's own, where none is given), and what came of it. `fresh`: it had
// no picture before, so a renderer that drew nothing is not left behind.
void Render(PortraitReport &report, ObjectId id, dao::CCCreature &creature, bool fresh)
{
    if (report.cameraGiven)
    {
        dao::portraits::Camera camera;
        camera.pitch = report.camera.pitch;
        camera.rotation = report.camera.rotation;
        camera.distance = report.camera.distance;
        camera.zAdjust = report.camera.zAdjust;
        camera.xAdjust = report.camera.xAdjust;
        camera.lightX = report.lightX;
        camera.lightY = report.lightY;
        camera.lightZ = report.lightZ;
        dao::portraits::SetCamera(id, camera);
    }
    else
    {
        // The table's, and not one given it before (ids are reused).
        dao::portraits::ClearCamera(id);
    }
    report.fresh = fresh;
    report.image = creature.RenderPortrait();
    report.rendered = !report.image.empty();
    if (!report.rendered)
    {
        // A renderer made for it now that drew nothing is not left behind
        // (core/Portraits.h: none is left to die with its creature's area).
        if (fresh)
        {
            creature.ReleasePortrait();
            dao::portraits::ClearCamera(id);
        }
        report.lack = PortraitLack::Render;
        report.problem = "the engine made no portrait of it";
        return;
    }
    report.source = "rendered";
    if (PastFarPlane(report.camera))
    {
        report.problem = std::format("the camera stands {:.1f} m from its head, past the {:.0f} m it draws to: "
                                     "nothing of it is drawn",
                                     kPortraitBackOff * report.camera.distance, kPortraitFarPlane);
    }
}

// One creature's portrait, got: its ready-made picture, the one it has, or a
// render framed by the rule (or as `order` tunes it), with everything that
// was seen on the way, for the log and for a tester. The one place a
// portrait's camera is given to the engine.
PortraitReport Shoot(ObjectId id, const ShotOrder &order)
{
    PortraitReport report;
    report.id = id;
    report.source = "none";
    auto *app = dao::CClientExoApp::Get();
    dao::CCCreature *creature = app != nullptr ? app->GetCreatureById(id) : nullptr;
    if (creature == nullptr)
    {
        report.lack = PortraitLack::Creature;
        report.problem = "no creature by that id";
        return report;
    }
    const PortraitTable &table = Table();
    const Looks looks = LooksOf(id);
    const BodyMeasure body = MeasureBody(id);
    Describe(report, id, looks, body, order);
    // A tester who gives a kind or a value wants that framing rendered, where
    // the rule would give its kin's ready-made picture. Not where the table
    // names a picture for its own appearance: the engine renders nothing for
    // one of those. Its render looks the appearance up in the table and,
    // finding a picture named, gives the creature that name and returns
    // before it draws (PortraitRenderer::Render, 0x007F46E0 in GOG's build,
    // read 2026-10-04 with DAOLib's tools/daoexe.py).
    const bool tuned = order.tuning.kind.has_value() || order.tuning.AnyValue();
    const PortraitChoice choice = ChoosePortrait(looks.appearance, looks.label, looks.person, report.tag, table.rows);
    const bool ownPicture = std::ranges::any_of(table.rows, [&](const PortraitRow &row) {
        return looks.appearance > 0 && row.appearance == looks.appearance && !row.made.empty();
    });
    if (choice.source == PortraitSource::ReadyMade && (!tuned || ownPicture))
    {
        report.image = L"img://" + choice.made;
        report.source = "the portraits table's";
        report.rule = "the portraits table's ready-made picture of its appearance, or of its kin";
        if (tuned)
        {
            report.problem = "the engine renders no portrait of an appearance the portraits table names a picture "
                             "for: not rendered, and its picture is the table's";
        }
        return report;
    }
    const std::wstring had = creature->PortraitImage();
    if (!had.empty() && !order.again)
    {
        // The party's, which the game's party bar shows: never framed anew.
        report.image = had;
        report.source = "the game's";
        report.rule = "the portrait it has already";
        if (tuned)
        {
            report.problem = "its portrait is the game's own, which its party bar shows: not rendered again";
        }
        return report;
    }
    report.image = had;
    if (!body.head)
    {
        if (!order.render)
        {
            report.problem = order.whyNot;
            return report;
        }
        // The engine's camera then aims at its scene's origin, the clone's
        // root (a giant spider's came out as its abdomen, 2026-10-01): no
        // picture rather than that one, but for a camera that carries the
        // framing from there, a row of the table marked so or a tester's
        // values (core's RendersWithoutHead; core/PortraitTable.h, FromRoot).
        report.lack = PortraitLack::HeadNode;
        if (!RendersWithoutHead(RowOf(looks.appearance), order.tuning))
        {
            report.problem = "its model has no head node for a portrait's camera to aim at, or is not set up yet (a "
                             "creature just made or changed takes a few frames), and the framing table has no row "
                             "from its root for it";
            return report;
        }
    }
    Frame(report, looks, body, order, table, choice);
    if (!order.render)
    {
        // What the rule would frame it by is said all the same; nothing is
        // given to the engine.
        report.cameraGiven = false;
        report.problem = order.whyNot;
        return report;
    }
    Render(report, id, *creature, had.empty());
    return report;
}
} // namespace

void BarLink::KeepPortraits(const TurnView &view)
{
    const auto id = portraits_.Next(view, row_);
    if (!id)
    {
        return;
    }
    PortraitReport report = Shoot(*id, ShotOrder{});
    // A creature come into the fight this moment may have no model yet, and
    // so no head node: asked again with the next views for a while, before
    // its card is left with initials for the fight (each is tried once).
    // The cards after it wait meanwhile, which is why not for long.
    constexpr int kHeadTries = 10;
    if (report.lack == PortraitLack::HeadNode && report.image.empty())
    {
        if (++headTries_[*id] < kHeadTries)
        {
            if (headTries_[*id] == 1)
            {
                spdlog::debug("{:#010x} has no head node yet: its portrait is asked for again with the next views",
                              *id);
            }
            return;
        }
    }
    headTries_.erase(*id);
    // One the turnbased_portrait tool rendered before the fight had its
    // card comes as the one it has: the bar's from now on, released with the
    // fight's others.
    const bool tools = toolOwned_.erase(*id) > 0 && !report.image.empty();
    if (tools && !report.rendered)
    {
        report.source = "rendered before, by the turnbased_portrait tool";
    }
    if (const auto card = std::ranges::find(view.upcoming, *id, &CardView::id); card != view.upcoming.end())
    {
        report.name = card->name;
    }
    spdlog::info("{}", PortraitLine(report));
    portraits_.Got(*id, report.image, report.rendered || tools);
}

PortraitReport BarLink::TryPortrait(const PortraitAsk &ask, const TurnView &view, std::uint32_t nowMs)
{
    // With no movie of the bar's (the bar off since the game started, or its
    // movie not found) nothing would show a render, and nothing here would
    // run to release one: none is made, and the answer still says what the
    // rule measured and how it would frame.
    const bool movie = installed_ && movieFound_;
    ShotOrder order;
    order.tuning = ask.tuning;
    order.render = movie;
    order.whyNot = "not rendered: the turn bar's movie is not there to show it ("
                   "turnorder.swf was not found)";
    // Rendered again in place where the renderer is the mod's own: nothing
    // is released under a movie that may be showing it.
    order.again = portraits_.RenderedFor(ask.id) || portraits_.Leaving(ask.id) || toolOwned_.contains(ask.id);
    PortraitReport report = Shoot(ask.id, order);
    if (report.lack == PortraitLack::Creature)
    {
        // A wrong id changes nothing: what is up stays up.
        spdlog::info("turnbased_portrait for {:#x}: no creature by that id", ask.id);
        return report;
    }
    const auto also = [&report](std::string_view more) {
        report.problem += std::string{report.problem.empty() ? "" : "; "} + std::string{more};
    };
    const bool card = portraits_.Has(ask.id);
    if (report.rendered)
    {
        const bool fights =
            view.phase != Phase::Idle && std::ranges::find(view.upcoming, ask.id, &CardView::id) != view.upcoming.end();
        if (card || fights)
        {
            // The fight's: its card shows it, and it goes with the fight's.
            portraits_.Got(ask.id, report.image, true);
            toolOwned_.erase(ask.id);
        }
        else if (!portraits_.Leaving(ask.id))
        {
            // This tool's, released once nothing shows it (FollowShow). Not
            // one the last fight's cards may still be showing: that one is
            // released in its turn, once the bar's movie has let go.
            toolOwned_.insert_or_assign(ask.id, nowMs);
        }
    }
    else if (card && report.lack == PortraitLack::Nothing && !report.image.empty())
    {
        // The rule as built gave a picture that is not a render (the
        // table's): its card goes back to it.
        portraits_.Got(ask.id, report.image, false);
    }
    if (!movie)
    {
        if (ask.show)
        {
            also("not put up: the turn bar's movie is not there");
        }
    }
    else if (ask.show)
    {
        SetShow(&report, ask.seconds, nowMs);
        report.shown = true;
    }
    else
    {
        SetShow(nullptr, 0, nowMs);
    }
    if (report.rendered && toolOwned_.contains(ask.id) && !ShowHolds(ask.id))
    {
        also("nothing shows it (no card of a fight, and show not asked for): its renderer is released in a moment");
    }
    spdlog::info("turnbased_portrait for {:#x}{}: {}", ask.id, report.shown ? ", put up large" : "",
                 PortraitLine(report));
    return report;
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
