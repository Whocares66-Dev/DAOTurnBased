// The box on the game's tactics page, "Use when turn-based": the plugin's
// side of it (dev/UI.md, "The tactics page's box"; issue 46's second step).
// The user, 2026-10-05: "Should probably add a checkbox to the tactics page,
// 'use when turn-based', off by default, per character. Should be changeable
// even during battle, applies to character's next turn."; and of the player's
// own character, "We can skip the hero for now".
//
// The box is the mod's own element beside the game's, of the game's own art
// (the user's rule for the mod's interface): a movie of the mod's,
// turnbased_tactics.swf beside turnbased.dll (ui/tactics, built by
// tools/movie.py), loaded into a clip of the tactics screen's own movie each
// time that screen opens (DAOLib's dao::gui::OnFullScreen and LoadInto, its
// issue 27, 7a36c03). There its script attaches the screen's own toggle, the
// one each tactic's row has, and writes a caption beside it. Nothing of the
// game's screen is changed.
//
//   ExternalInterface.call("TurnBased_TacticsBox", "<id>")    -1, 0 or 1 (core's BoxState): what to show
//                                                             for the character the page is of
//   fscommand("TurnBased_UseWhenTurnBased", "<id> <0|1>")     a click: for whom, and what the box shows now
//   fscommand("TurnBased_TacticsSays", "<text>")              a line for the log: what the movie did, or
//                                                             why it could not ("!" first for a warning)
//   Invoke("_root.TurnBasedTactics_mc.Refresh")               from the plugin: ask again
//
// As the turn bar's handlers do (game/Bar.h), the movie's calls only read
// what the frame last published and leave their requests for it: the frame
// callback asks the engine who the page can be of (Bridge.h, FollowersNow)
// and the machine what each one's box shows, under the machine's mutex and
// never while the engine or the movie is called; and it makes a click's
// choice (TurnMachine::ChooseTactics, the same choice the turnbased_auto tool
// makes, kept with the saves by game/CoSave.h). When a box's state changes
// from anywhere (a click, the tool, `[turns] party` set while the page is
// up) the open movie is told to ask again.
//
// Built 2026-10-05 from what DAOLib's session read and what was read in the
// running game that day (the screen's name "Tactics", its clips, the data
// tree's GUI.SelectedChar.ID), and seen in play the same day: the screen's
// opening told, the movie loaded and calling within two frames, the box
// placed beside the AI switch, the page's change heard (no box for the
// hero, each companion's as the save had it) and a click made the choice.
// The game closes the tactics movie as the screen closes, so each opening
// loads the movie anew. Not among what that play reported: a click in a
// fight, a choice changed from elsewhere with the page up, the camp's pool.
#pragma once

#include "core/PartyTactics.h"
#include "core/TurnMachine.h"

#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace tb::game
{

class TacticsBox
{
  public:
    // Once, from DAOLib_Load: asks DAOLib to say when a full-screen
    // interface opens and closes, and to hand over the movie's three calls.
    // The machine and its mutex are the plugin's, and outlive this.
    void Install(TurnMachine &machine, std::mutex &mutex);

    // Every frame, from the frame callback (the scripts' thread). Does
    // nothing while the tactics screen is not up. Nothing it meets is let
    // out into the frame: a failure here is said in the log, once, and the
    // fight's machine never stands down for it.
    void Serve();

  private:
    // The tactics screen has opened: the boxes published, and the mod's
    // movie loaded into it. And closed: nobody has a box.
    void Opened();
    void Closed();
    // The clicks made, the engine and the machine asked, the boxes published
    // and, where one changed or was clicked, the open movie told to ask
    // again. `opening`: the screen's first look, which says the boxes
    // whatever they are and tells no movie.
    void Follow(bool opening);
    void Choose(const BoxClick &click, std::span<const Follower> followers);
    // Each frame after the movie was loaded into its clip, until it has
    // called from there; said once if it never does.
    void WatchScript();
    // The movie's question: what to show for this one, a BoxState's number.
    [[nodiscard]] int Answer(ObjectId id);

    TurnMachine *machine_ = nullptr;
    std::mutex *machineMutex_ = nullptr;
    bool installed_ = false;
    std::atomic<bool> open_{false};
    bool failed_ = false; // a failure in Serve was said: not said again until the screen next opens
    // The frame's alone: the movie's clip is in the open screen, the movie
    // has been heard from there, and how many frames it has been waited on.
    bool loaded_ = false;
    bool running_ = false;
    int framesLoaded_ = 0;
    std::atomic<bool> called_{false}; // one of the movie's calls has come since the screen opened
    // What the movie's calls share with the frame.
    std::mutex mutex_;
    TacticsBoxes boxes_;           // under mutex_: empty while the screen is not up
    std::vector<Follower> party_;  // under mutex_: whom the boxes are of, for the log's names
    std::vector<BoxClick> clicks_; // under mutex_: clicks the frame has not made yet
    std::string lastAnswer_;       // under mutex_: the last answer's line, not said twice running
};

} // namespace tb::game
