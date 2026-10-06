#include "core/Settings.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using Catch::Approx;

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
    CHECK(parsed.settings.initiativeDie == defaults.initiativeDie);
    CHECK(parsed.settings.initiativePerDexterity == defaults.initiativePerDexterity);
    CHECK(parsed.settings.secondsPerTurn == defaults.secondsPerTurn);
    CHECK(parsed.settings.freeStepMeters == defaults.freeStepMeters);
    CHECK(parsed.settings.paceMps == defaults.paceMps);
    CHECK(parsed.settings.conditionTurns == defaults.conditionTurns);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.settleMs == defaults.settleMs);
    CHECK(parsed.settings.aiThinkMs == defaults.aiThinkMs);
    CHECK(parsed.settings.aiFollowUpMs == defaults.aiFollowUpMs);
    CHECK(parsed.settings.maxActionMs == defaults.maxActionMs);
    CHECK(parsed.settings.combatRadius == defaults.combatRadius);
    CHECK(parsed.settings.joinRadius == defaults.joinRadius);
    CHECK(parsed.settings.orderRunMs == defaults.orderRunMs);
    CHECK(parsed.settings.aiPoke == defaults.aiPoke);
    CHECK(parsed.settings.floaties == defaults.floaties);
    CHECK(parsed.settings.scriptEvents == defaults.scriptEvents);
    CHECK(parsed.settings.cameraFollow == defaults.cameraFollow);
    CHECK(parsed.settings.turnBar == defaults.turnBar);
    CHECK(parsed.settings.portraits == defaults.portraits);
    CHECK(parsed.settings.pickTargets == defaults.pickTargets);
    CHECK(parsed.settings.pathCheck == defaults.pathCheck);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
}

TEST_CASE("values are read by section, with comments and case ignored", "[settings]")
{
    const auto parsed = ParseSettings("[TurnBased]\r\n"
                                      "Enabled = off   ; the player's own note\r\n"
                                      "[Movement]\r\n"
                                      "Path_Check = No\r\n"
                                      "[turns]\r\n"
                                      "party = Tactics\r\n"
                                      "pace = 12.5\r\n"
                                      "[keys]\r\n"
                                      "toggle = F9\r\n");
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.settings.enabled);
    CHECK_FALSE(parsed.settings.pathCheck);
    CHECK(parsed.settings.paceMps == 12.5f);
    CHECK(parsed.settings.partyControl == tb::PartyControl::Tactics);
    CHECK(parsed.settings.toggleKey.key == 0x78);
}

TEST_CASE("a bad value keeps the default and says so; out of range is clamped", "[settings]")
{
    const auto parsed = ParseSettings("[initiative]\n"
                                      "die = twenty\n"
                                      "[turns]\n"
                                      "settle_ms = 999999\n"
                                      "nonsense = 1\n"
                                      "pace = 0.1\n"
                                      "just words\n");
    CHECK(parsed.settings.initiativeDie == Settings{}.initiativeDie);
    CHECK(parsed.settings.settleMs == 5000);
    CHECK(parsed.settings.paceMps == 0.5f);
    CHECK(parsed.warnings.size() == 5);
}

TEST_CASE("each kind of value says what it wanted, and the default stands", "[settings]")
{
    const auto parsed = ParseSettings("[turnbased]\n"
                                      "enabled = maybe\n"
                                      "[initiative]\n"
                                      "per_dexterity = lots\n"
                                      "[turns]\n"
                                      "party = anarchy\n"
                                      "ai_poke = gently\n"
                                      "[keys]\n"
                                      "toggle = Hyper+Q\n");
    const Settings defaults;
    REQUIRE(parsed.warnings.size() == 5);
    CHECK(parsed.warnings[0].find("not true or false") != std::string::npos);
    CHECK(parsed.warnings[1].find("not a number") != std::string::npos);
    CHECK(parsed.warnings[2].find("not manual or tactics") != std::string::npos);
    CHECK(parsed.warnings[3].find("not command_complete or kickstart") != std::string::npos);
    CHECK(parsed.warnings[4].find("not a key") != std::string::npos);
    CHECK(parsed.settings.enabled == defaults.enabled);
    CHECK(parsed.settings.initiativePerDexterity == defaults.initiativePerDexterity);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.aiPoke == defaults.aiPoke);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
}

TEST_CASE("the other choices of each enumerated setting read", "[settings]")
{
    const auto parsed = ParseSettings("[turns]\nai_poke = Kickstart\n[turns]\nparty = manual\n");
    CHECK(parsed.warnings.empty());
    CHECK(parsed.settings.aiPoke == tb::AiPoke::Kickstart);
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

TEST_CASE("a file from an older build gets the settings it lacks, with their comments, its own values kept",
          "[settings]")
{
    const std::string older = "[turnbased]\nenabled = false\n\n[turns]\nsettle_ms = 250\n";
    const std::string whole = tb::WithMissingSettings(older);
    const auto parsed = tb::ParseSettings(whole);
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.settings.enabled);                                   // the player's
    CHECK(parsed.settings.settleMs == 250);                                 // the player's
    CHECK(parsed.settings.secondsPerTurn == tb::Settings{}.secondsPerTurn); // added
    CHECK(whole.find("seconds_per_turn = ") != std::string::npos);
    CHECK(whole.find("; What a turn is: a bar of time this many seconds long") != std::string::npos); // its comment
    // Added once: a whole file is left as it is.
    CHECK(tb::WithMissingSettings(whole) == whole);
    CHECK(tb::WithMissingSettings(tb::DefaultSettingsFile()) == tb::DefaultSettingsFile());
}

TEST_CASE("a setting the player commented out is left commented out", "[settings]")
{
    std::string text = tb::DefaultSettingsFile();
    const auto at = text.find("turn_bar = ");
    REQUIRE(at != std::string::npos);
    text.insert(at, "; ");
    const std::string whole = tb::WithMissingSettings(text);
    // The player's choice: not added back.
    CHECK(whole == text);
    CHECK(tb::WithMissingSettings(whole) == whole);
}

TEST_CASE("[turns] charge is a list of an ability and its turns", "[settings]")
{
    const auto parsed = ParseSettings("[turns]\ncharge = 10002:1, 13000:2\n");
    REQUIRE(parsed.settings.charge.size() == 2);
    CHECK(parsed.settings.charge[0] == std::pair<std::int32_t, int>{10002, 1});
    CHECK(parsed.settings.charge[1] == std::pair<std::int32_t, int>{13000, 2});
    CHECK(ParseSettings("[turns]\ncharge =\n").settings.charge.empty());
    CHECK(ParseSettings(DefaultSettingsFile()).settings.charge.empty());
    for (const char *bad : {"10002", "10002:x", "a:1", "10002:1 13000:2", "10002:12", "-1:1"})
    {
        CAPTURE(bad);
        const auto wrong = ParseSettings(std::string{"[turns]\ncharge = "} + bad + "\n");
        CHECK(wrong.settings.charge.empty());
        CHECK_FALSE(wrong.warnings.empty());
    }
}

TEST_CASE("the movement preview is on by default and can be turned off", "[settings]")
{
    CHECK(tb::Settings{}.movePreview);
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.movePreview);
    const tb::ParsedSettings off = tb::ParseSettings("[feedback]\nmove_preview = false\n");
    CHECK_FALSE(off.settings.movePreview);
    CHECK(off.warnings.empty());
}

TEST_CASE("the preview's path is marked on the ground by default, and can be a line instead", "[settings]")
{
    // The user, 2026-10-01, of the two seen in play: "let's try the rings".
    CHECK(tb::Settings{}.movePreviewGround);
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.movePreviewGround);
    const tb::ParsedSettings line = tb::ParseSettings("[feedback]\nmove_preview_ground = false\n");
    CHECK_FALSE(line.settings.movePreviewGround);
    CHECK(line.warnings.empty());
}

TEST_CASE("a turn is a bar of time, four seconds by default, and its numbers are read", "[settings]")
{
    const tb::Settings defaults;
    CHECK(defaults.secondsPerTurn == 4.0f);
    CHECK(defaults.freeStepMeters == 1.0f);
    CHECK(defaults.enemyTactics == std::array<int, 5>{0, 0, 2, 4, 6});
    const tb::ParsedSettings timed = tb::ParseSettings("[turns]\nseconds_per_turn = 5\nfree_step = 0.5\n"
                                                       "pace = 5\nenemy_tactics = 0, 1, 2, 3, 4\n");
    CHECK(timed.warnings.empty());
    CHECK(timed.settings.secondsPerTurn == 5.0f);
    CHECK(timed.settings.freeStepMeters == 0.5f);
    CHECK(timed.settings.paceMps == 5.0f);
    CHECK(timed.settings.enemyTactics == std::array<int, 5>{0, 1, 2, 3, 4});
}

// The clock of one action and meters a turn is gone (issue 50), and its
// settings with it, with nothing put in their place (the user: "No need to
// maintain backward compatibility, this is not released yet"). A file
// written before then still has them: each is a key the mod does not know,
// said so by its line and name, and the rest of the file is read. The two
// keys retired before it had a notice each, which went the same way.
TEST_CASE("a file that still has the action clock's settings is told of each as unknown, and plays on the bar",
          "[settings]")
{
    const tb::ParsedSettings old = tb::ParseSettings("[movement]\n"
                                                     "rule = constant\n"
                                                     "meters = 12\n"
                                                     "per_dexterity = 0.1\n"
                                                     "min = 3\n"
                                                     "max = 20\n"
                                                     "path_check = false\n"
                                                     "[turns]\n"
                                                     "clock = actions\n"
                                                     "end_turn_when_action_used = false\n"
                                                     "cooldown_seconds_per_turn = 6\n"
                                                     "effect_seconds_per_round = 6\n"
                                                     "max_commands_per_turn = 6\n"
                                                     "ai_turn_speed = 2\n"
                                                     "seconds_per_turn = 5\n");
    REQUIRE(old.warnings.size() == 11);
    CHECK(old.warnings[0] == "line 2: unknown setting [movement] rule");
    CHECK(old.warnings[4] == "line 6: unknown setting [movement] max");
    CHECK(old.warnings[5] == "line 9: unknown setting [turns] clock");
    CHECK(old.warnings[6] == "line 10: unknown setting [turns] end_turn_when_action_used");
    CHECK(old.warnings[7] == "line 11: unknown setting [turns] cooldown_seconds_per_turn");
    CHECK(old.warnings[8] == "line 12: unknown setting [turns] effect_seconds_per_round");
    CHECK(old.warnings[9] == "line 13: unknown setting [turns] max_commands_per_turn");
    CHECK(old.warnings[10] == "line 14: unknown setting [turns] ai_turn_speed");
    // What the bar reads of that file is read.
    CHECK_FALSE(old.settings.pathCheck);
    CHECK(old.settings.secondsPerTurn == 5.0f);
    // And the file the mod writes names none of them.
    const std::string written = tb::DefaultSettingsFile();
    for (const char *gone : {"\nrule =", "\nmeters =", "\nmin =", "\nmax =", "\nclock =", "end_turn_when_action_used",
                             "cooldown_seconds_per_turn", "effect_seconds_per_round"})
    {
        CAPTURE(gone);
        CHECK(written.find(gone) == std::string::npos);
    }
}

TEST_CASE("bad rank lists are refused, the defaults kept", "[settings]")
{
    const tb::ParsedSettings bad = tb::ParseSettings("[turns]\nenemy_tactics = 0, 0, 2\n");
    CHECK(bad.warnings.size() == 1);
    CHECK(bad.settings.enemyTactics == std::array<int, 5>{0, 0, 2, 4, 6});
    CHECK(tb::ParseSettings("[turns]\nenemy_tactics = 0, 0, 2, 4, 9\n").warnings.size() == 1);
    CHECK(tb::ParseSettings("[turns]\nenemy_tactics = 0, 0, 2, 4, 4, 4\n").warnings.size() == 1);
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

TEST_CASE("the scripts' timers of a second or more count rounds unless the file says otherwise", "[settings]")
{
    // Issue 26: Uldred's turning of the captive mages, a 35 s cycle and ten
    // seconds from the light to the death, ran on the world's clock through
    // every fighter's turn. The user, 2026-10-04: "a round is supposed to be
    // the same 4 seconds for everyone for things that apply over time or are
    // environmental".
    const tb::Settings defaults;
    CHECK(defaults.scriptTimersFrom == Approx(1.0f));
    CHECK_FALSE(defaults.scriptTimersTrace);
    const tb::ParsedSettings file = tb::ParseSettings(tb::DefaultSettingsFile());
    CHECK(file.warnings.empty());
    CHECK(file.settings.scriptTimersFrom == Approx(1.0f));
    CHECK_FALSE(file.settings.scriptTimersTrace);
    CHECK(tb::DefaultSettingsFile().find("\nscript_timers_from = 1\n") != std::string::npos);
    // Off, as before the rule: only damage over time and lasting areas.
    const tb::ParsedSettings off = tb::ParseSettings("[turns]\nscript_timers_from = 0\nscript_timers_trace = true\n");
    CHECK(off.warnings.empty());
    CHECK(off.settings.scriptTimersFrom == Approx(0.0f));
    CHECK(off.settings.scriptTimersTrace);
    // A longer floor is the player's to set; a negative one is no floor.
    CHECK(tb::ParseSettings("[turns]\nscript_timers_from = 2.5\n").settings.scriptTimersFrom == Approx(2.5f));
    CHECK(tb::ParseSettings("[turns]\nscript_timers_from = -3\n").settings.scriptTimersFrom == Approx(0.0f));
    // An older file gets it, with its comment.
    const std::string whole = tb::WithMissingSettings("[turns]\nsettle_ms = 250\n");
    CHECK(whole.find("script_timers_from = 1") != std::string::npos);
    CHECK(whole.find("; The scripts' own timers count rounds") != std::string::npos);
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

TEST_CASE("a turn an AI plays has a time limit of its own setting: thirty seconds unless the file says", "[settings]")
{
    CHECK(tb::Settings{}.maxTurnMs == 30000);
    CHECK(tb::DefaultSettingsFile().find("\nmax_turn_ms = 30000\n") != std::string::npos);
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.maxTurnMs == 30000);
    const tb::ParsedSettings set = tb::ParseSettings("[turns]\nmax_turn_ms = 45000\n");
    CHECK(set.warnings.empty());
    CHECK(set.settings.maxTurnMs == 45000);
    // Out of its range: told, and the nearest it may be. There is no "off":
    // a second at the least.
    const tb::ParsedSettings none = tb::ParseSettings("[turns]\nmax_turn_ms = 0\n");
    CHECK(none.warnings.size() == 1);
    CHECK(none.settings.maxTurnMs == 1000);
}

TEST_CASE("own clocks are on by default, and can be turned off", "[settings]")
{
    CHECK(tb::Settings{}.ownClocks);
    const tb::ParsedSettings off = tb::ParseSettings("[turns]\nown_clocks = false\n");
    CHECK(off.warnings.empty());
    CHECK_FALSE(off.settings.ownClocks);
    CHECK(tb::ParseSettings(tb::DefaultSettingsFile()).settings.ownClocks);
}
