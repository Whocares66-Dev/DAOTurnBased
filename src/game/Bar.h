// The turn bar's side of the plugin (dev/UI.md, "The turn order bar"):
// what its movie asks and says, through DAOLib's dao/Gui.h.
//
//   ExternalInterface.call("TB_GetView")  the view, as core/Bar.h encodes it
//   fscommand("TB_EndTurn")               end the player's turn, as the pause key does
//   fscommand("TB_Delay")                 put the player's turn off to the round's end
//   fscommand("TB_Focus", "<id>")         centre the camera on a combatant
//   fscommand("TB_Scroll", "1" or "-1")   an arrow at the row's end: to later turns, or earlier
//   fscommand("TB_Cleared")               it has dropped its cards and their images
//   ExternalInterface.call("TB_GetShow")  a portrait to put up large, as core/Portraits.h encodes it
//   fscommand("TB_ShowCleared", "<n>")    it has drawn that one's serial, and let the one before go
//
// The handlers only read the view the frame callback last published and
// leave their requests for it; the frame callback acts on them, where
// script commands and the machine belong. The movies' calls were seen on
// the scripts' thread (DAOLib's eighth session, 2026-09-30), so this keeps
// order rather than safety: nothing touches the machine mid-frame.
// Installed only with the turn_bar setting, so that without it DAOLib's
// lookups of the game's own UI calls stay undetoured.
#pragma once

#include "core/BarWindow.h"
#include "core/Portraits.h"
#include "core/TurnMachine.h"
#include "core/Types.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb::game
{

class BarLink
{
  public:
    // Registers the movie's names with the game's UI. Once; later calls do
    // nothing.
    void Install();
    [[nodiscard]] bool Installed() const
    {
        return installed_;
    }

    // Every frame, before the portraits and the view: where the bar's row
    // stands among the turns to come (core/BarWindow.h), moved by what the
    // arrows asked since the last frame. True if an arrow was clicked: the
    // view is then published at once, not at the next tenth of a second, so
    // the row moves under the hand.
    [[nodiscard]] bool FollowRow(const TurnView &view);

    // The view the bar should show now, with the row as last followed and
    // the portraits got so far (the frame callback).
    void Publish(const TurnView &view);

    // Every frame: at a fight's end, forgets its portraits, and once the
    // movie has let the images go (TB_Cleared) or a while has passed,
    // releases the ones rendered for the bar (DAOLib's
    // CCCreature::ReleasePortrait), as the game's screens release theirs.
    void FollowFight(const TurnView &view, std::uint32_t nowMs);

    // One combatant's portrait a call, until each card of the row has one or
    // has been tried (core/Portraits.h): the table's ready-made picture, the
    // one the game already has (the party's), else, with `render`, one the
    // engine renders now (DAOLib's CCCreature::RenderPortrait), framed by the
    // table's own camera for it or by the kind of body it is and its size
    // (core's FramePortrait). Each said in the log, once a creature a fight.
    // From the frame callback: rendering is the scripts' thread's.
    void KeepPortraits(const TurnView &view, bool render);

    // One creature's portrait rendered now, by the rule as built or as a
    // tester asks (the turnbased_portrait tool, src/plugin/Plugin.cpp; the
    // user's sweep of every appearance the game has, issue 32): any creature,
    // in a fight or out of one, with a card in the bar or without. Its card,
    // if it has one, shows the new picture; with `show` the picture is put
    // up large in the bar's movie, with words under it, whether a fight is
    // on or not, and taken down by the next call that does not ask for one,
    // or after its seconds. Answers everything it saw and did.
    //
    // Whose the renderer is, which is what makes this safe under a movie
    // that is showing the picture:
    // - the game's own (a party member's, which its party bar shows) is
    //   never rendered again and never released: the answer says so;
    // - one the bar rendered for a card, or this rendered before, is
    //   rendered again in place, into the same texture, with nothing
    //   released under the movie;
    // - one made here for a creature of the fight is the bar's from then on,
    //   released with the fight's others; one made for a creature outside a
    //   fight is released once nothing shows it: half a second on if it was
    //   not put up, else once the movie has said it let the picture go
    //   (TB_ShowCleared) or a while has passed, as the bar's are;
    // - with no movie of the bar's to show one (the bar off, or its movie
    //   not found) nothing is rendered: the answer has what was measured
    //   and how the rule would frame.
    // On the scripts' thread, as the frame callback is.
    [[nodiscard]] PortraitReport TryPortrait(const PortraitAsk &ask, const TurnView &view, std::uint32_t nowMs);

    // What the movie asked for since the last frame, each once.
    [[nodiscard]] bool TakeEndTurn();
    [[nodiscard]] bool TakeDelay();
    [[nodiscard]] std::optional<ObjectId> TakeFocus();

    // The bar's screen: a GUI scene of ours, "TurnOrder", on the HUD's
    // layer, its movie turnorder.swf (DAOLib's dao::gui::AddScreen, which
    // has it served from the plugin's own folder, beside turnbased.dll,
    // where tools/deploy.ps1 puts it: issue 47, 2026-10-05; in the override
    // folder until then). Added once the GUI system is up,
    // shown while a fight holds the world or a portrait is put up large
    // (TryPortrait), hidden otherwise. From the frame callback: the GUI
    // system's calls belong on the scripts' thread.
    void KeepScreen(bool fighting);

  private:
    PortraitBook portraits_; // the frame callback's alone
    // The portrait put up large (TryPortrait's `show`): whose, since when and
    // for how long. The frame callback's alone, as all of this is but the
    // two under the mutex.
    struct Showing
    {
        ObjectId id = kNoObject;
        std::uint32_t sinceMs = 0;
        std::uint32_t forMs = 0;
    };
    std::optional<Showing> showing_;
    // How often a card's portrait was asked for and its creature had no head
    // node yet (KeepPortraits): a fight's, forgotten with its portraits.
    std::unordered_map<ObjectId, int> headTries_;
    // A creature whose picture the movie was showing large and may still be
    // drawing: until the movie says it has drawn a later serial, or a while
    // has passed, its renderer is not released.
    struct ShowLeaving
    {
        ObjectId id = kNoObject;
        std::uint32_t serial = 0; // the one that replaced it
        std::uint32_t sinceMs = 0;
    };
    std::vector<ShowLeaving> showLeaving_;
    // Creatures whose renderer TryPortrait made outside the bar's book, each
    // with when it last rendered: this releases them, once nothing shows
    // them and not in the frame they were rendered in (FollowShow).
    std::unordered_map<ObjectId, std::uint32_t> toolOwned_;
    std::uint32_t showSerial_ = 0;
    std::wstring showEncoded_;    // under mutex_: what TB_GetShow answers
    std::uint32_t showDrawn_ = 0; // under mutex_: the serial the movie last said it had drawn
    // Puts a picture up, or with no report takes what is up down.
    void SetShow(const PortraitReport *report, std::uint32_t seconds, std::uint32_t nowMs);
    // Every frame: the picture's time, the movie's word, and the release of
    // what TryPortrait rendered once nothing shows it.
    void FollowShow(std::uint32_t nowMs);
    [[nodiscard]] bool ShowHolds(ObjectId id) const;
    // The row and where it stands: the frame callback's alone. The arrows'
    // clicks wait in scroll_, under the mutex, for the frame to take.
    BarWindow window_;
    BarSpan row_;
    // An ability's icon and name, as found: the frame callback's alone.
    struct AbilityArt
    {
        std::wstring icon;
        std::wstring name;
    };
    std::unordered_map<std::int32_t, AbilityArt> abilityArt_;
    std::mutex mutex_;
    std::wstring encoded_;
    bool endTurn_ = false;
    bool delay_ = false;
    int scroll_ = 0; // the arrows' clicks not yet taken: later ones less earlier ones
    bool movieLetGo_ = false;
    // The game's own "Paused" banner (the floaty layer's PauseIndicator_mc,
    // top centre: where the turn bar is) hidden through a fight, as the
    // bar's movie asks for its view, and shown again on the first frame with
    // no fight (FollowFight).
    void HideGameBanner(bool hide);
    std::atomic<bool> fightShown_{false}; // the view last published is a fight's
    std::atomic<bool> bannerHidden_{false};
    bool bannerSaid_ = false;
    std::optional<ObjectId> focus_;
    bool installed_ = false;
    bool screenAdded_ = false;
    bool movieFound_ = false;
    bool screenShown_ = false;
};

} // namespace tb::game
