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
    CHECK(parsed.settings.initiativeDie == defaults.initiativeDie);
    CHECK(parsed.settings.initiativePerDexterity == defaults.initiativePerDexterity);
    CHECK(parsed.settings.movementRule == defaults.movementRule);
    CHECK(parsed.settings.movementMeters == defaults.movementMeters);
    CHECK(parsed.settings.movementMin == defaults.movementMin);
    CHECK(parsed.settings.movementMax == defaults.movementMax);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.settleMs == defaults.settleMs);
    CHECK(parsed.settings.aiThinkMs == defaults.aiThinkMs);
    CHECK(parsed.settings.aiFollowUpMs == defaults.aiFollowUpMs);
    CHECK(parsed.settings.maxActionMs == defaults.maxActionMs);
    CHECK(parsed.settings.combatRadius == defaults.combatRadius);
    CHECK(parsed.settings.orderRunMs == defaults.orderRunMs);
    CHECK(parsed.settings.aiPoke == defaults.aiPoke);
    CHECK(parsed.settings.floaties == defaults.floaties);
    CHECK(parsed.settings.scriptEvents == defaults.scriptEvents);
    CHECK(parsed.settings.cameraFollow == defaults.cameraFollow);
    CHECK(parsed.settings.turnBar == defaults.turnBar);
    CHECK(parsed.settings.portraits == defaults.portraits);
    CHECK(parsed.settings.pickTargets == defaults.pickTargets);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
}

TEST_CASE("values are read by section, with comments and case ignored", "[settings]")
{
    const auto parsed = ParseSettings("[TurnBased]\r\n"
                                      "Enabled = off   ; the player's own note\r\n"
                                      "[movement]\r\n"
                                      "rule = Constant\r\n"
                                      "meters = 12.5\r\n"
                                      "[turns]\r\n"
                                      "party = tactics\r\n"
                                      "[keys]\r\n"
                                      "toggle = F9\r\n");
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.settings.enabled);
    CHECK(parsed.settings.movementRule == tb::MovementRule::Constant);
    CHECK(parsed.settings.movementMeters == 12.5f);
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
                                      "[movement]\n"
                                      "min = 30\n"
                                      "max = 10\n"
                                      "just words\n");
    CHECK(parsed.settings.initiativeDie == Settings{}.initiativeDie);
    CHECK(parsed.settings.settleMs == 5000);
    CHECK(parsed.settings.movementMin == 10.0f);
    CHECK(parsed.warnings.size() == 5);
}

TEST_CASE("each kind of value says what it wanted, and the default stands", "[settings]")
{
    const auto parsed = ParseSettings("[turnbased]\n"
                                      "enabled = maybe\n"
                                      "[initiative]\n"
                                      "per_dexterity = lots\n"
                                      "[movement]\n"
                                      "rule = teleport\n"
                                      "[turns]\n"
                                      "party = anarchy\n"
                                      "ai_poke = gently\n"
                                      "[keys]\n"
                                      "toggle = Hyper+Q\n");
    const Settings defaults;
    REQUIRE(parsed.warnings.size() == 6);
    CHECK(parsed.warnings[0].find("not true or false") != std::string::npos);
    CHECK(parsed.warnings[1].find("not a number") != std::string::npos);
    CHECK(parsed.warnings[2].find("not speed or constant") != std::string::npos);
    CHECK(parsed.warnings[3].find("not manual or tactics") != std::string::npos);
    CHECK(parsed.warnings[4].find("not command_complete or kickstart") != std::string::npos);
    CHECK(parsed.warnings[5].find("not a key") != std::string::npos);
    CHECK(parsed.settings.enabled == defaults.enabled);
    CHECK(parsed.settings.initiativePerDexterity == defaults.initiativePerDexterity);
    CHECK(parsed.settings.movementRule == defaults.movementRule);
    CHECK(parsed.settings.partyControl == defaults.partyControl);
    CHECK(parsed.settings.aiPoke == defaults.aiPoke);
    CHECK(parsed.settings.toggleKey == defaults.toggleKey);
}

TEST_CASE("the other choices of each enumerated setting read", "[settings]")
{
    const auto parsed =
        ParseSettings("[turns]\nai_poke = Kickstart\n[movement]\nrule = speed\n[turns]\nparty = manual\n");
    CHECK(parsed.warnings.empty());
    CHECK(parsed.settings.aiPoke == tb::AiPoke::Kickstart);
    CHECK(parsed.settings.movementRule == tb::MovementRule::Speed);
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
    CHECK_FALSE(parsed.settings.enabled);                                                 // the player's
    CHECK(parsed.settings.settleMs == 250);                                               // the player's
    CHECK(parsed.settings.effectSecondsPerRound == tb::Settings{}.effectSecondsPerRound); // added
    CHECK(whole.find("effect_seconds_per_round = ") != std::string::npos);
    CHECK(whole.find("; Effects and regeneration count rounds") != std::string::npos); // with its comment
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
