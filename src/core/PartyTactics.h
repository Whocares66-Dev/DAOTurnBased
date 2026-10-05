// Whose turns their tactics play: the player's choice for each companion
// (issue 46), the mod's file in a save that keeps it, and the test tool that
// makes the choice until the game's tactics page has a box for it.
//
// The user, 2026-10-05: "what if there was an auto option for a party member
// so that instead of the player having to pick they just let the tactics do
// the work for that turn (or every turn of that character as the case may
// be). ... the general combat can be done by the combat AI, just bound by a
// turn." Of where the choice lives: "Should probably add a checkbox to the
// tactics page, 'use when turn-based', off by default, per character. Should
// be changeable even during battle, applies to character's next turn." Of
// the player's own character, whose full round the game's scripts do not run
// while the player controls it (AI_DetermineCombatRound_Partial): "We can
// skip the hero for now if that requires additional development." And of
// where it is kept: "I think we have functionality for a cosave right?",
// "This is also a good opportunity to exercise the cosave".
//
// So there is a choice a companion, by the follower's tag, and it belongs to
// the playthrough: written into each save and read from the one loaded
// (src/game/CoSave.cpp, over DAOLib's dao::saves). A companion nothing was
// chosen for goes by `[turns] party`, which is what every companion's turns
// start as (manual by default: they wait for the player); a choice wins over
// it, either way. The hero's turns are the player's, whatever is chosen or
// set.
//
// The machine asks at the start of each turn, and a turn under way is played
// out by the choice it began with (TurnMachine::PlayerControls): a choice
// made in a fight is the companion's next turn's.
#pragma once

#include "core/Settings.h"
#include "core/Types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tb
{

// The choices of the playthrough loaded now: for each follower one was made
// for, by its tag (the scripts' GetTag: "gen00fl_alistair"), whether its
// tactics play its turns. A follower not here goes by the setting.
class PartyTactics
{
  public:
    // A follower's tag and what was chosen for it, in the tags' order: a
    // handful at most. (Not a std::map: MSVC's allocates as it is moved, and
    // may throw there, which lint will not have of a move.)
    using Choices = std::vector<std::pair<std::wstring, bool>>;

    // What was chosen for this follower; nothing where nothing was.
    [[nodiscard]] std::optional<bool> Chosen(std::wstring_view tag) const;
    // Chooses. True if that changed what is kept: a first choice does, even
    // of what the setting already said, since from then the setting no
    // longer speaks for this follower. False, and nothing kept, for a tag
    // that cannot be told apart or written down (Keepable).
    bool Choose(std::wstring_view tag, bool on);
    [[nodiscard]] const Choices &All() const
    {
        return chosen_;
    }
    [[nodiscard]] bool operator==(const PartyTactics &) const = default;

  private:
    Choices chosen_;
};

// Whether a choice can be kept by this tag: it has one, and no control
// character in it (the file is a line a follower).
[[nodiscard]] bool Keepable(std::wstring_view tag);

// Whether a party member's tactics play its turns, by what was chosen for it
// (nothing: never chosen) and what `[turns] party` starts every companion
// as. Never the hero's: the player's own character (Combatant::hero, the
// scripts' GetHero), whoever the player controls at the time.
[[nodiscard]] bool TacticsPlay(std::optional<bool> chosen, bool hero, PartyControl start);

// "none", or "2 (gen00fl_alistair on, gen00fl_wynne off)": how many, and for
// whom, for the log's line as a save is read or written.
[[nodiscard]] std::string Describe(const PartyTactics &choices);

// The mod's file in a save, as text:
//
//   turnbased 1
//   tactics on gen00fl_alistair
//   tactics off gen00fl_wynne
//   end
//
// The first line is the file's name for itself and the format's version;
// then a line a choice, the tag last and whole to the line's end, in UTF-8;
// then "end", so that a file cut short anywhere is known for one. Lines end
// in a line feed (a carriage return before it is passed over, and so are
// empty lines). A file with no choice is the first line and the last, and is
// written all the same: a save made over another in the same slot must not
// be left with the older one's file.
inline constexpr int kPartyTacticsVersion = 1;
[[nodiscard]] std::string WriteChoices(const PartyTactics &choices);

// Read all of it or none of it: a file of another version, or one that is
// not what WriteChoices writes (cut short, a line of some other kind, a tag
// twice, bytes that are no text, anything after its end), is read as
// nothing, with why in `problem`. Half of a damaged file is not believed,
// and a newer build's file is not guessed at.
struct ChoicesRead
{
    PartyTactics choices;
    std::string problem; // empty: read whole
};
[[nodiscard]] ChoicesRead ReadChoices(std::string_view file);

// One of the party, as the game layer reads it for a tool that names one
// (src/game/Bridge.cpp, PartyNow).
struct Follower
{
    ObjectId id = kNoObject;
    std::wstring name;
    std::wstring tag;
    bool hero = false;
};

// What the machine answers of a follower (TurnMachine::TacticsOf,
// ChooseTactics).
struct TacticsAnswer
{
    bool on = false;      // its tactics play its turns, from its next turn
    bool chosen = false;  // chosen for it in this playthrough; else what the setting starts it as
    bool changed = false; // this call changed what is kept
    // Its turn is under way, and is played out the way it began: by its
    // tactics, or by the player.
    bool turnUnderWay = false;
    bool turnIsTactics = false;
};

// The test tool turnbased_auto (src/plugin/Plugin.cpp): whom it names, by
// `id` (as Eluvian writes ids, "0x7d9") or by `name`, and with `on` what to
// choose; with no `on` it asks.
struct AutoAsk
{
    std::optional<ObjectId> id;
    std::string name; // UTF-8; empty where an id was given
    std::optional<bool> on;
};
struct AutoAskRead
{
    std::optional<AutoAsk> ask;
    std::string error; // for the caller, where there is no ask
};
[[nodiscard]] AutoAskRead ReadAutoAsk(std::string_view json);

// Whom the ask means, of the party: by its id; or by its name, whatever the
// letters' case, where one of the party alone has it. And where something is
// to be chosen, not the hero, for whom there is nothing to choose, nor one
// whose tag a choice cannot be kept by (Keepable).
struct FollowerFound
{
    const Follower *who = nullptr;
    std::string error; // for the caller, where nobody was found
};
[[nodiscard]] FollowerFound FindFollower(const AutoAsk &ask, std::span<const Follower> party);

// The tool's answer: {"id":"0x7d9","name":"Alistair","tag":"gen00fl_alistair",
// "hero":false,"on":true,"by":"chosen" (or "setting": nothing was chosen for
// it, and [turns] party says),"changed":true,"applies":"from its next turn",
// "turn_under_way":null} (or "yours" or "AI": how the turn it has now began,
// and is played out; `applies` then says so too).
[[nodiscard]] std::string ToJson(const Follower &who, const TacticsAnswer &answer);

} // namespace tb
