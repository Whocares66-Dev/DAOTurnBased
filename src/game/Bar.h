// The turn bar's side of the plugin (dev/UI.md, "The turn order bar"):
// what its movie asks and says, through DAOLib's dao/Gui.h.
//
//   ExternalInterface.call("TB_GetView")  the view, as core/Bar.h encodes it
//   fscommand("TB_EndTurn")               end the player's turn, as the pause key does
//   fscommand("TB_Delay")                 put the player's turn off to the round's end
//   fscommand("TB_Focus", "<id>")         centre the camera on a combatant
//   fscommand("TB_Cleared")               it has dropped its cards and their images
//
// The handlers only read the view the frame callback last published and
// leave their requests for it; the frame callback acts on them, where
// script commands and the machine belong. The movies' calls were seen on
// the scripts' thread (DAOLib's eighth session, 2026-09-30), so this keeps
// order rather than safety: nothing touches the machine mid-frame.
// Installed only with the turn_bar setting, so that without it DAOLib's
// lookups of the game's own UI calls stay undetoured.
#pragma once

#include "core/Portraits.h"
#include "core/TurnMachine.h"
#include "core/Types.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tb::game
{

class BarLink
{
  public:
    // Registers the three names with the game's UI. Once; later calls do
    // nothing.
    void Install();
    [[nodiscard]] bool Installed() const
    {
        return installed_;
    }

    // The view the bar should show now, with the portraits got so far (the
    // frame callback).
    void Publish(const TurnView &view);

    // Every frame: at a fight's end, forgets its portraits, and once the
    // movie has let the images go (TB_Cleared) or a while has passed,
    // releases the ones rendered for the bar (DAOLib's
    // CCCreature::ReleasePortrait), as the game's screens release theirs.
    void FollowFight(const TurnView &view, std::uint32_t nowMs);

    // One combatant's portrait a call, until each card has one or has been
    // tried (core/Portraits.h): the one the game already has (the party's),
    // else, with `render`, one the engine renders now (DAOLib's
    // CCCreature::RenderPortrait). From the frame callback: rendering is
    // the scripts' thread's.
    void KeepPortraits(const TurnView &view, bool render);

    // What the movie asked for since the last frame, each once.
    [[nodiscard]] bool TakeEndTurn();
    [[nodiscard]] bool TakeDelay();
    [[nodiscard]] std::optional<ObjectId> TakeFocus();

    // The bar's screen: a GUI scene of ours, "TurnOrder", on the HUD's
    // layer, its movie turnorder.swf (DAOLib's dao::gui::AddScreen, found by
    // the game's resource system where it finds its own: tools/deploy.ps1
    // puts it in the override folder). Added once the GUI system is up,
    // shown while a fight holds the world, hidden otherwise. From the frame
    // callback: the GUI system's calls belong on the scripts' thread.
    void KeepScreen(bool fighting);

  private:
    PortraitBook portraits_; // the frame callback's alone
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
    bool movieLetGo_ = false;
    // The game's own "Paused" banner (the floaty layer's PauseIndicator_mc,
    // top centre: where the turn bar is) hidden through a fight, and shown
    // again after: on the interface's thread, from the movie's own calls.
    void HideGameBanner(bool hide);
    std::atomic<bool> fightShown_{false}; // the view last published is a fight's
    bool bannerHidden_ = false;           // the interface's thread's
    bool bannerSaid_ = false;
    std::optional<ObjectId> focus_;
    bool installed_ = false;
    bool screenAdded_ = false;
    bool movieFound_ = false;
    bool screenShown_ = false;
};

} // namespace tb::game
