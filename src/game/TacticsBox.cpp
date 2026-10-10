#include "game/TacticsBox.h"

#include "core/Text.h"
#include "game/Bridge.h"

#include "dao/Gui.h"
#include "dao/Log.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace tb::game
{

namespace
{

// The tactics screen as DAOLib's dao::gui names it (seen in play 2026-10-05
// through Eluvian: `gui` says fullScreen "Tactics", its movie
// "Tactics", on layer 3).
constexpr std::wstring_view kScreen = L"Tactics";
// The clip of that screen's root the mod's movie is loaded into, and the
// movie, turnbased_tactics.swf beside the dll. The box itself and its caption
// are the movie's to make, at the screen's root (ui/tactics: TurnBased_mc,
// TurnBasedCaption_txt), since only a clip of the screen's own movie can
// attach the screen's own symbols.
constexpr std::string_view kClip = "TurnBasedTactics_mc";
constexpr std::wstring_view kMovie = L"turnbased_tactics";
constexpr const char *kRefresh = "_root.TurnBasedTactics_mc.Refresh";
// How long the movie's script is looked for after its clip was made before
// the log says it has not run: the page's opening is some thirty frames of
// the screen's own, and the game's frames are many a second.
constexpr int kScriptFrames = 300;
// Who asked, in the machine's line of a choice.
constexpr std::string_view kBy = "the tactics page's box";

// Whom the movie asks about, from its call's one argument. The movie sends
// text; a number is read too, should ActionScript ever pass the id as one.
std::optional<ObjectId> AskedOf(std::span<const dao::gui::Value> args)
{
    if (args.empty())
    {
        return std::nullopt;
    }
    const dao::gui::Value &arg = args.front();
    if (const std::optional<std::string_view> text = arg.AsString())
    {
        return ReadBoxAsk(*text);
    }
    if (const std::optional<std::wstring_view> wide = arg.AsWString())
    {
        return ReadBoxAsk(Utf8(*wide));
    }
    if (const std::optional<double> number = arg.AsNumber();
        number && std::isfinite(*number) && *number == std::trunc(*number))
    {
        return ReadBoxAsk(std::format("{:.0f}", *number));
    }
    return std::nullopt;
}

// The open movie told to ask again. With no lock held: the movie asks from
// inside the call, on this thread. The call answers false while the movie's
// script has not run yet, which asks by itself when it does.
bool AskAgain()
{
    std::optional<dao::gui::Movie> movie = dao::gui::ScreenMovie(kScreen);
    return movie && movie->Invoke(kRefresh);
}

// What the game's UI code calls cannot take an exception: one that comes of
// `body` is said, with what it was at, and goes no further.
template <class Body> void Guarded(std::string_view what, const Body &body)
{
    try
    {
        body();
    }
    catch (const std::exception &e)
    {
        spdlog::error("{}: {}", what, e.what());
    }
    catch (...)
    {
        spdlog::error("{}: an exception of no known kind", what);
    }
}

} // namespace

void TacticsBox::Install(TurnMachine &machine, std::mutex &mutex)
{
    if (installed_)
    {
        return;
    }
    installed_ = true;
    machine_ = &machine;
    machineMutex_ = &mutex;
    // On the game's thread, at the first frame after the screen opened or
    // closed, its movie open (dao/Gui.h): the engine may be asked from here
    // as from the frame callback.
    dao::gui::OnFullScreen([this](std::wstring_view name, bool shown) {
        if (name != kScreen)
        {
            return;
        }
        Guarded(shown ? "the tactics screen opening" : "the tactics screen closing", [this, shown] {
            if (shown)
            {
                Opened();
            }
            else
            {
                Closed();
            }
        });
    });
    dao::gui::OnExternalInterface("TurnBased_TacticsBox", [this](const dao::gui::Call &call) {
        // With no answer the movie reads no number and shows no box.
        Guarded("TurnBased_TacticsBox", [this, &call] {
            called_.store(true);
            const std::optional<ObjectId> id = AskedOf(call.args);
            call.Return(dao::gui::Value::Number(id ? Answer(*id) : static_cast<int>(BoxState::None)));
            if (!id)
            {
                spdlog::warn("TurnBased_TacticsBox was asked with no creature's id: no box (the tactics screen's "
                             "GUI.SelectedChar.ID was not a number, or the movie is not the plugin's pair)");
            }
        });
    });
    dao::gui::OnFSCommand("TurnBased_UseWhenTurnBased", [this](void *, std::string_view args) {
        Guarded("TurnBased_UseWhenTurnBased", [this, args] {
            const std::optional<BoxClick> click = ReadBoxClick(args);
            if (!click)
            {
                spdlog::warn("TurnBased_UseWhenTurnBased with '{}': not a click of the tactics page's box (a "
                             "creature's id, a space, and 1 for ticked or 0 for not)",
                             args);
                return;
            }
            const std::lock_guard lock(mutex_);
            clicks_.push_back(*click);
        });
    });
    // The movie's own word of what it did, for whoever reads the log with
    // no eye on the screen: where it put the box, or why there is none.
    dao::gui::OnFSCommand("TurnBased_TacticsSays", [this](void *, std::string_view text) {
        Guarded("TurnBased_TacticsSays", [this, text] {
            called_.store(true);
            if (text.starts_with('!'))
            {
                spdlog::warn("the tactics page's movie: {}", text.substr(1));
            }
            else
            {
                spdlog::info("the tactics page's movie: {}", text);
            }
        });
    });
    spdlog::info("the game's tactics page gets a box of the mod's, \"Use when turn-based\", a companion "
                 "(turnbased_tactics.swf beside the plugin, loaded into the screen as it opens); its calls are "
                 "answered: TurnBased_TacticsBox, TurnBased_UseWhenTurnBased, TurnBased_TacticsSays");
}

void TacticsBox::Serve()
{
    if (!open_.load())
    {
        return;
    }
    try
    {
        Follow(false);
        if (loaded_ && !running_)
        {
            WatchScript();
        }
    }
    catch (const std::exception &e)
    {
        if (!std::exchange(failed_, true))
        {
            spdlog::error("the tactics page's box: {} (said once an opening of the screen)", e.what());
        }
    }
    catch (...)
    {
        if (!std::exchange(failed_, true))
        {
            spdlog::error("the tactics page's box: an exception of no known kind (said once an opening of the "
                          "screen)");
        }
    }
}

void TacticsBox::WatchScript()
{
    // LoadInto answers that the load was asked for, not that it came: the
    // file is opened by the game's loader with the movie's next advance. So
    // the movie is waited on until it has called: its script's first line
    // is a word to the log (in play, 2026-10-05, within two frames). Since
    // DAOLib 7a36c03 a clip whose load never came is made again at the
    // screen's next opening (its mark on the clip is still there), where at
    // a12afc4 it read as loaded for as long as the screen's movie lived;
    // this still says that it happened, which nothing else would.
    ++framesLoaded_;
    if (called_.load())
    {
        running_ = true;
        spdlog::info("the box's movie is running in the tactics screen: its first call came within {} frames of "
                     "its loading",
                     framesLoaded_);
    }
    else if (framesLoaded_ == kScriptFrames)
    {
        spdlog::warn("the box's movie was loaded into the tactics screen's clip {} {} frames ago and has not "
                     "called: its script has not run, and the page has no box. The load is asked for again as "
                     "the screen next opens",
                     kClip, kScriptFrames);
    }
}

void TacticsBox::Opened()
{
    failed_ = false;
    loaded_ = false;
    called_.store(false);
    {
        const std::lock_guard lock(mutex_);
        lastAnswer_.clear();
    }
    open_.store(true);
    // The boxes first: the movie's first question has its answer waiting.
    Follow(true);
    std::optional<dao::gui::Movie> movie = dao::gui::ScreenMovie(kScreen);
    if (!movie)
    {
        spdlog::warn("the tactics screen is open and DAOLib finds no movie of it (dao::gui::ScreenMovie): the page "
                     "has no box; turnbased_auto still chooses");
        return;
    }
    // Every time the screen opens: a clip already there with its movie
    // loaded is left alone, and one gone with a movie the game closed is
    // made again, as is one whose load never came (dao/Gui.h). The game
    // closes the tactics movie as the screen closes (seen 2026-10-05: each
    // opening loaded the movie anew, once), so in play it is made each time.
    if (!dao::gui::LoadInto(*movie, kClip, kMovie))
    {
        spdlog::warn("the tactics screen is open and the box's movie could not be loaded into it: no "
                     "turnbased_tactics.swf beside the plugin (python tools\\movie.py, a build and "
                     "tools\\deploy.ps1 put it in daolib\\plugins\\turnbased), or the screen's movie made no clip "
                     "for it. The page has no box; turnbased_auto still chooses");
        return;
    }
    // A clip kept from an opening before has its script running, and is told
    // to ask again, the page being whoever's it is now (not met in play: the
    // game keeps no tactics movie). One made now has no script yet: it asks
    // by itself once the screen has set itself up.
    const bool kept = AskAgain();
    loaded_ = true;
    running_ = kept;
    framesLoaded_ = 0;
    spdlog::info("the box's movie is in the tactics screen's clip {} ({})", kClip,
                 kept ? "kept from an opening before, and told to ask again" : "loading now");
}

void TacticsBox::Closed()
{
    open_.store(false);
    loaded_ = false;
    // A click of the screen's last frame is still made. Whatever that
    // meets, the screen is closed for this all the same: nobody has a box
    // until it opens again.
    std::exception_ptr failure;
    try
    {
        Follow(false);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    {
        const std::lock_guard lock(mutex_);
        boxes_ = TacticsBoxes{};
        party_.clear();
        lastAnswer_.clear();
    }
    spdlog::info("the tactics screen is closed");
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

void TacticsBox::Follow(bool opening)
{
    std::vector<BoxClick> clicks;
    {
        const std::lock_guard lock(mutex_);
        clicks.swap(clicks_);
    }
    // The engine is asked with no lock held, and the machine with its own
    // alone.
    const std::vector<Follower> followers = FollowersNow();
    for (const BoxClick &click : clicks)
    {
        Choose(click, followers);
    }
    TacticsBoxes boxes;
    {
        const std::lock_guard lock(*machineMutex_);
        boxes = machine_->BoxesOf(followers);
    }
    bool changed = false;
    {
        const std::lock_guard lock(mutex_);
        changed = boxes != boxes_;
        boxes_ = boxes;
        party_ = followers;
    }
    if (opening)
    {
        spdlog::info("the tactics screen is open; its boxes, \"Use when turn-based\": {}", Describe(followers, boxes));
        return;
    }
    if (changed)
    {
        spdlog::info("the tactics page's boxes are now: {}", Describe(followers, boxes));
    }
    // After a click too, changed or not: the box flipped itself as it was
    // clicked, and shows what is so once it has asked again (a click that
    // was refused goes back).
    if (changed || !clicks.empty())
    {
        (void)AskAgain();
    }
}

void TacticsBox::Choose(const BoxClick &click, std::span<const Follower> followers)
{
    // Whom the click names, and whether there is anything to choose for it,
    // is core's to say, as for the tool.
    const FollowerFound found = FindFollower(AutoAsk{click.id, {}, click.on}, followers);
    if (found.who == nullptr)
    {
        spdlog::warn("the tactics page's box was clicked for {:#x} ({}), and nothing is chosen: {}", click.id,
                     click.on ? "ticked" : "unticked", found.error);
        return;
    }
    TacticsAnswer answer;
    {
        const std::lock_guard lock(*machineMutex_);
        answer = machine_->ChooseTactics(*found.who, click.on, kBy);
    }
    if (!answer.changed)
    {
        // The machine says a choice once, when it changes what is kept.
        spdlog::info("the tactics page's box was clicked for {} ({}), which is what was chosen for it already: its "
                     "{}",
                     Utf8(found.who->name), click.on ? "ticked" : "unticked",
                     answer.on ? "tactics play its turns" : "turns wait for the player");
    }
}

int TacticsBox::Answer(ObjectId id)
{
    BoxState state = BoxState::None;
    std::string line;
    bool say = false;
    {
        const std::lock_guard lock(mutex_);
        state = boxes_.Of(id);
        const auto who = std::ranges::find(party_, id, &Follower::id);
        if (!open_.load())
        {
            line = std::format("the tactics page's box was asked of {:#x} before the screen was known to be up: no "
                               "box until the frame has looked",
                               id);
        }
        else if (who == party_.end())
        {
            line = std::format("the tactics page is of {:#x}, who is neither of the party nor of its pool: no box", id);
        }
        else if (state != BoxState::None)
        {
            line = std::format("the tactics page is of {} ({:#x}): its box is {}", Utf8(who->name), id,
                               state == BoxState::On ? "on" : "off");
        }
        else
        {
            line = std::format("the tactics page is of {} ({:#x}), {}: no box", Utf8(who->name), id,
                               who->hero ? "the hero" : "who has no tag a choice could be kept by");
        }
        say = line != lastAnswer_;
        if (say)
        {
            lastAnswer_ = line;
        }
    }
    if (say)
    {
        spdlog::info("{}", line);
    }
    return static_cast<int>(state);
}

} // namespace tb::game
