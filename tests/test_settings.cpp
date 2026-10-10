#include "core/Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using tb::DefaultSettingsFile;
using tb::ParseSettings;
using tb::Settings;
using tb::WithValue;

TEST_CASE("the default file parses to the defaults, with nothing to warn of", "[settings]")
{
    const auto parsed = ParseSettings(DefaultSettingsFile());
    CHECK(parsed.warnings.empty());
    const Settings defaults;
    CHECK(parsed.settings.enabled == defaults.enabled);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.fightSpeed == defaults.fightSpeed);
    CHECK(parsed.settings.cameraFollow == defaults.cameraFollow);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
    CHECK(parsed.settings.endTurnKey == defaults.endTurnKey);
    CHECK(parsed.settings.delayKey == defaults.delayKey);
}

TEST_CASE("the file has the seven settings a player sets, and none of the rules' numbers", "[settings]")
{
    // The user, 2026-10-10, of a file of 43: "strip it down to only
    // essential configs. stuff like initiative are built-in, don't need to
    // be configured" (issue 104).
    const std::string written = DefaultSettingsFile();
    int keys = 0;
    std::size_t at = 0;
    while (at < written.size())
    {
        const std::size_t end = written.find('\n', at);
        const std::string line = written.substr(at, end - at);
        keys += !line.empty() && line.front() != ';' && line.front() != '[' ? 1 : 0;
        at = end == std::string::npos ? written.size() : end + 1;
    }
    CHECK(keys == 7);
    // One that was a setting is a key the mod does not know: said so, by its
    // line and its name, and the rest of the file is read.
    const tb::ParsedSettings old = ParseSettings("[initiative]\n"
                                                 "die = 12\n"
                                                 "[turns]\n"
                                                 "seconds_per_turn = 6\n"
                                                 "party = tactics\n"
                                                 "[feedback]\n"
                                                 "turn_bar = false\n");
    REQUIRE(old.warnings.size() == 3);
    CHECK(old.warnings[0] == "line 2: unknown setting [initiative] die");
    CHECK(old.warnings[1] == "line 4: unknown setting [turns] seconds_per_turn");
    CHECK(old.warnings[2] == "line 7: unknown setting [feedback] turn_bar");
    CHECK(old.settings.partyControl == tb::PartyControl::Tactics);
    CHECK(old.settings.initiativeDie == Settings{}.initiativeDie);
    CHECK(old.settings.secondsPerTurn == Settings{}.secondsPerTurn);
}

TEST_CASE("values are read by section, with comments and case ignored", "[settings]")
{
    const auto parsed = ParseSettings("[TurnBased]\r\n"
                                      "Enabled = off   ; the player's own note\r\n"
                                      "[Feedback]\r\n"
                                      "Camera_Follow = No\r\n"
                                      "[turns]\r\n"
                                      "party = Tactics\r\n"
                                      "speed = 2\r\n"
                                      "[keys]\r\n"
                                      "toggle = F9\r\n");
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.settings.enabled);
    CHECK_FALSE(parsed.settings.cameraFollow);
    CHECK(parsed.settings.fightSpeed == 2.0f);
    CHECK(parsed.settings.partyControl == tb::PartyControl::Tactics);
    CHECK(parsed.settings.toggleKey.key == 0x78);
}

TEST_CASE("each kind of value says what it wanted, and the default stands", "[settings]")
{
    const auto parsed = ParseSettings("[turnbased]\n"
                                      "enabled = maybe\n"
                                      "[turns]\n"
                                      "speed = fast\n"
                                      "party = anarchy\n"
                                      "nonsense = 1\n"
                                      "just words\n"
                                      "[keys]\n"
                                      "toggle = Hyper+Q\n");
    const Settings defaults;
    REQUIRE(parsed.warnings.size() == 6);
    CHECK(parsed.warnings[0].find("not true or false") != std::string::npos);
    CHECK(parsed.warnings[1].find("not a number") != std::string::npos);
    CHECK(parsed.warnings[2].find("not manual or tactics") != std::string::npos);
    CHECK(parsed.warnings[3] == "line 6: unknown setting [turns] nonsense");
    CHECK(parsed.warnings[4] == "line 7: not key = value");
    CHECK(parsed.warnings[5].find("not a key") != std::string::npos);
    CHECK(parsed.settings.enabled == defaults.enabled);
    CHECK(parsed.settings.fightSpeed == defaults.fightSpeed);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
}

TEST_CASE("party = manual reads as well as tactics", "[settings]")
{
    const auto parsed = ParseSettings("[turns]\nparty = tactics\n[turns]\nparty = manual\n");
    CHECK(parsed.warnings.empty());
    CHECK(parsed.settings.partyControl == tb::PartyControl::Manual);
}

TEST_CASE("WithValue replaces one value and leaves the rest as it was", "[settings]")
{
    const std::string before = "; mine\r\n[turnbased]\r\n; note\r\nenabled = true ; x\r\n\r\n[keys]\r\ntoggle = F9\r\n";
    const std::string after = WithValue(before, "turnbased", "enabled", "false");
    CHECK(after == "; mine\r\n[turnbased]\r\n; note\r\nenabled = false\r\n\r\n[keys]\r\ntoggle = F9\r\n");
    CHECK_FALSE(ParseSettings(after).settings.enabled);
}

TEST_CASE("WithValue adds a missing key under its section, or the section at the end", "[settings]")
{
    CHECK(WithValue("[turnbased]\n\n[keys]\ntoggle = F9\n", "turnbased", "enabled", "false") ==
          "[turnbased]\nenabled = false\n\n[keys]\ntoggle = F9\n");
    CHECK(WithValue("[keys]\ntoggle = F9\n", "turnbased", "enabled", "false") ==
          "[keys]\ntoggle = F9\n\n[turnbased]\nenabled = false\n");
    CHECK(WithValue("", "turnbased", "enabled", "true") == "[turnbased]\nenabled = true\n");
}

TEST_CASE("a file that lacks a setting gets it, with its comment, its own values kept", "[settings]")
{
    const std::string older = "[turnbased]\nenabled = false\n\n[turns]\nspeed = 2\n";
    const std::string whole = tb::WithMissingSettings(older);
    const auto parsed = tb::ParseSettings(whole);
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.settings.enabled);                               // the player's
    CHECK(parsed.settings.fightSpeed == 2.0f);                          // the player's
    CHECK(parsed.settings.cameraFollow == tb::Settings{}.cameraFollow); // added
    CHECK(whole.find("camera_follow = ") != std::string::npos);
    CHECK(whole.find("; Centre the camera on whoever's turn it is.") != std::string::npos); // its comment
    // Added once: a whole file is left as it is.
    CHECK(tb::WithMissingSettings(whole) == whole);
    CHECK(tb::WithMissingSettings(tb::DefaultSettingsFile()) == tb::DefaultSettingsFile());
}

TEST_CASE("a setting the player commented out is left commented out", "[settings]")
{
    std::string text = tb::DefaultSettingsFile();
    const auto at = text.find("camera_follow = ");
    REQUIRE(at != std::string::npos);
    text.insert(at, "; ");
    const std::string whole = tb::WithMissingSettings(text);
    // The player's choice: not added back.
    CHECK(whole == text);
    CHECK(tb::WithMissingSettings(whole) == whole);
}

TEST_CASE("the fight's speed is read, and kept within what the game takes", "[settings]")
{
    CHECK(tb::Settings{}.fightSpeed == 1.0f);
    CHECK(tb::ParseSettings("[turns]\nspeed = 2\n").settings.fightSpeed == 2.0f);
    CHECK(tb::ParseSettings("[turns]\nspeed = 0.5\n").settings.fightSpeed == 0.5f);
    const tb::ParsedSettings fast = tb::ParseSettings("[turns]\nspeed = 40\n");
    CHECK(fast.warnings.size() == 1);
    CHECK(fast.settings.fightSpeed == 4.0f); // kept within what the game takes
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.fightSpeed == 1.0f);
}

TEST_CASE("the turn camera is on until the file turns it off", "[settings]")
{
    CHECK(tb::Settings{}.cameraFollow);
    const tb::ParsedSettings off = tb::ParseSettings("[feedback]\ncamera_follow = false\n");
    CHECK(off.warnings.empty());
    CHECK_FALSE(off.settings.cameraFollow);
}

TEST_CASE("End turn's key is Enter until the file names another", "[settings]")
{
    const tb::Settings defaults;
    CHECK(defaults.endTurnKey.key == 0x0D);
    CHECK_FALSE(defaults.endTurnKey.ctrl);
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.endTurnKey.key == 0x0D);
    const tb::ParsedSettings other = tb::ParseSettings("[keys]\nend_turn = Ctrl+E\n");
    CHECK(other.warnings.empty());
    CHECK(other.settings.endTurnKey.key == 0x45);
    CHECK(other.settings.endTurnKey.ctrl);
    const tb::ParsedSettings bad = tb::ParseSettings("[keys]\nend_turn = Whenever\n");
    CHECK(bad.warnings.size() == 1);
    CHECK(bad.settings.endTurnKey.key == 0x0D);
    // None is a key's value too: End turn is then bound in the game's
    // controls, where it is listed all the same.
    const tb::ParsedSettings none = tb::ParseSettings("[keys]\nend_turn = None\n");
    CHECK(none.warnings.empty());
    CHECK(none.settings.endTurnKey.key == 0);
}

TEST_CASE("the toggle has no key until the file names one", "[settings]")
{
    // Ctrl+T until 2026-10-03. The user then, of the box in the game's own
    // options doing the same: "maybe we don't need that key binding", and
    // of its row left in the controls with no key: "Yeah we can keep it
    // unbound I guess".
    const tb::Settings defaults;
    CHECK(defaults.toggleKey.key == 0);
    CHECK_FALSE(defaults.toggleKey.ctrl);
    const tb::ParsedSettings file = tb::ParseSettings(tb::DefaultSettingsFile());
    CHECK(file.warnings.empty());
    CHECK(file.settings.toggleKey.key == 0);
    CHECK(tb::DefaultSettingsFile().find("\ntoggle = None\n") != std::string::npos);
    // A file that names one keeps it: a player who had Ctrl+T written has it.
    const tb::ParsedSettings old = tb::ParseSettings("[keys]\ntoggle = Ctrl+T\n");
    CHECK(old.warnings.empty());
    CHECK(old.settings.toggleKey.key == 0x54);
    CHECK(old.settings.toggleKey.ctrl);
}

TEST_CASE("Delay has no key until the file names one", "[settings]")
{
    // The user, 2026-10-03: "Delay should have no key binding by default,
    // end turn is enter".
    const tb::Settings defaults;
    CHECK(defaults.delayKey.key == 0);
    const tb::ParsedSettings file = tb::ParseSettings(tb::DefaultSettingsFile());
    CHECK(file.warnings.empty());
    CHECK(file.settings.delayKey.key == 0);
    CHECK(file.settings.endTurnKey.key == 0x0D); // and End turn's is Enter still
    CHECK(tb::DefaultSettingsFile().find("\ndelay = None\n") != std::string::npos);
    const tb::ParsedSettings other = tb::ParseSettings("[keys]\ndelay = Shift+D\n");
    CHECK(other.warnings.empty());
    CHECK(other.settings.delayKey.key == 0x44);
    CHECK(other.settings.delayKey.shift);
    CHECK_FALSE(other.settings.delayKey.ctrl);
    CHECK(other.settings.endTurnKey.key == 0x0D); // each its own
    const tb::ParsedSettings bad = tb::ParseSettings("[keys]\ndelay = Sometime\n");
    CHECK(bad.warnings.size() == 1);
    CHECK(bad.settings.delayKey.key == 0);
}
