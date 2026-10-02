#include "core/Rules.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <set>
#include <string>
#include <vector>

using Catch::Approx;
using tb::Classify;
using tb::CommandKind;
using tb::CommandType;
using tb::Settings;

TEST_CASE("commands cost what the design says", "[rules]")
{
    CHECK(Classify(CommandType::Attack) == CommandKind::Action);
    CHECK(Classify(CommandType::UseAbility) == CommandKind::Action);
    CHECK(Classify(CommandType::UseItem) == CommandKind::Action); // a potion
    // What is not fighting is free (the user's rule, 2026-10-01).
    CHECK(Classify(CommandType::UseObject) == CommandKind::Free); // a door, a chest, a lever
    CHECK(Classify(CommandType::Interact) == CommandKind::Free);
    CHECK(Classify(CommandType::Unlock) == CommandKind::Free);
    CHECK(Classify(CommandType::Lock) == CommandKind::Free);
    CHECK(Classify(CommandType::PickupItem) == CommandKind::Free);
    CHECK(Classify(CommandType::GiveItem) == CommandKind::Free);
    CHECK(Classify(CommandType::TakeItem) == CommandKind::Free);
    CHECK(Classify(CommandType::MoveToLocation) == CommandKind::Move);
    CHECK(Classify(CommandType::MoveToObject) == CommandKind::Move);
    CHECK(Classify(CommandType::Drive) == CommandKind::Move);
    CHECK(Classify(CommandType::SwitchWeaponSets) == CommandKind::Free);
    CHECK(Classify(CommandType::SheatheWeapons) == CommandKind::Free);
    CHECK(Classify(CommandType::Turn) == CommandKind::Free);
    CHECK(Classify(CommandType::Wait) == CommandKind::Pass);
    CHECK(Classify(CommandType::MoveToMultipleLocations) == CommandKind::Move); // script.ldf's "recover"
    CHECK(Classify(CommandType::MoveAwayFromObject) == CommandKind::Move);
    CHECK(Classify(CommandType::UseItem) == CommandKind::Action);
    CHECK(Classify(CommandType::DropItem) == CommandKind::Free);
    CHECK(Classify(CommandType::Deathblow) == CommandKind::Involuntary);
    CHECK(Classify(CommandType::ExecuteEffect) == CommandKind::Involuntary);
    CHECK(Classify(CommandType::SpeakString) == CommandKind::Involuntary);
    CHECK(Classify(CommandType::StartConversation) == CommandKind::Involuntary);
    // A number no one has named is let through.
    CHECK(Classify(static_cast<CommandType>(77)) == CommandKind::Involuntary);
}

TEST_CASE("a wait is an AI passing only when a script chose it", "[rules]")
{
    CHECK(Classify(CommandType::Wait, tb::Source::Script) == CommandKind::Pass);
    // The scripts' SetActive queues one with an animation: the engine's doing.
    CHECK(Classify(CommandType::Wait, tb::Source::Engine) == CommandKind::Involuntary);
    CHECK(Classify(CommandType::Wait, tb::Source::Player) == CommandKind::Involuntary);
    // Nothing else depends on where it came from.
    CHECK(Classify(CommandType::Attack, tb::Source::Engine) == CommandKind::Action);
    CHECK(Classify(CommandType::Drive, tb::Source::Player) == CommandKind::Move);
}

TEST_CASE("every command type in the game's table has a name of its own", "[rules]")
{
    std::set<std::string> names;
    for (std::int32_t n = 0; n <= tb::kLastCommandType; ++n)
    {
        const std::string name = tb::Name(static_cast<CommandType>(n));
        CHECK(name != "unknown");
        CHECK(names.insert(name).second);
    }
    CHECK(std::string{tb::Name(static_cast<CommandType>(tb::kLastCommandType + 1))} == "unknown");
}

TEST_CASE("initiative is a die roll plus Dexterity above 10", "[rules]")
{
    Settings settings;
    settings.initiativeDie = 20;
    settings.initiativePerDexterity = 0.25f;
    tb::Rng rng{42};
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i)
    {
        const int roll = tb::RollInitiative(rng, 30.0f, settings); // +5
        REQUIRE(roll >= 6);
        REQUIRE(roll <= 25);
        seen.insert(roll);
    }
    CHECK(seen.size() == 20); // every face comes up

    settings.initiativeDie = 1;
    CHECK(tb::RollInitiative(rng, 10.0f, settings) == 1);
    CHECK(tb::RollInitiative(rng, 6.0f, settings) == 0); // below 10 takes away
}

TEST_CASE("the same seed rolls the same fight", "[rules]")
{
    tb::Rng a{7};
    tb::Rng b{7};
    for (int i = 0; i < 100; ++i)
    {
        CHECK(a.Roll(20) == b.Roll(20));
    }
}

TEST_CASE("movement is meters, times speed under the speed rule, clamped", "[rules]")
{
    Settings settings;
    settings.movementMeters = 9.0f;
    settings.movementMin = 3.0f;
    settings.movementMax = 20.0f;

    settings.movementRule = tb::MovementRule::Speed;
    CHECK(tb::MovementBudget(settings, 1.0f, 10.0f) == Approx(9.0f));
    CHECK(tb::MovementBudget(settings, 1.3f, 10.0f) == Approx(11.7f)); // hasted
    CHECK(tb::MovementBudget(settings, 0.2f, 10.0f) == Approx(3.0f));  // slowed to the floor
    // Not a multiplier: read as normal.
    CHECK(tb::MovementBudget(settings, 0.0f, 10.0f) == Approx(9.0f));
    CHECK(tb::MovementBudget(settings, 100.0f, 10.0f) == Approx(9.0f));
    CHECK(tb::MovementBudget(settings, std::numeric_limits<float>::quiet_NaN(), 10.0f) == Approx(9.0f));

    settings.movementRule = tb::MovementRule::Constant;
    CHECK(tb::MovementBudget(settings, 1.3f, 10.0f) == Approx(9.0f));

    settings.movementPerDexterity = 0.1f;
    CHECK(tb::MovementBudget(settings, 1.0f, 30.0f) == Approx(11.0f));
    CHECK(tb::MovementBudget(settings, 1.0f, 5.0f) == Approx(9.0f)); // low Dexterity takes nothing away
    settings.movementPerDexterity = 1.0f;
    CHECK(tb::MovementBudget(settings, 1.0f, 40.0f) == Approx(20.0f));
}

TEST_CASE("reach is the engine's: both personal spaces and 1.2 m, or a ranged weapon's range", "[rules]")
{
    CHECK(tb::Reach(0.0f, 0.35f, 0.70f) == Approx(2.4f));  // Lyna on a giant spider, as play test 23 measured
    CHECK(tb::Reach(0.0f, 0.35f, 0.35f) == Approx(2.05f)); // two people
    CHECK(tb::Reach(0.0f, 0.35f, 3.0f) == Approx(4.7f));   // a high dragon
    CHECK(tb::Reach(0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()) == Approx(1.35f)); // no space to read
    CHECK(tb::Reach(45.0f, 0.35f, 0.70f) == Approx(46.05f));                                 // a bow, and both spaces
    CHECK(tb::Reach(500.0f, 0.0f, 0.0f) == Approx(60.0f));                                   // at most 60 m
    CHECK(tb::WalkToReach(10.0f, 2.4f) == Approx(7.6f));
    CHECK(tb::WalkToReach(2.0f, 2.4f) == Approx(0.0f));
}

TEST_CASE("a creature's pace is its appearance's combat speed against a person's", "[rules]")
{
    // apr_base's Game_CbtSpeed, read 2026-09-30.
    CHECK(tb::SpeedFactor(4.66f) == Approx(1.0f));                  // human, elf, dwarf, genlock, hurlock
    CHECK(tb::SpeedFactor(7.35f) == Approx(1.577f).epsilon(0.001)); // shriek, werewolf
    CHECK(tb::SpeedFactor(3.914f) == Approx(0.84f).epsilon(0.001)); // ogre
    // Nothing to read: a person's pace.
    CHECK(tb::SpeedFactor(0.0f) == Approx(1.0f));
    CHECK(tb::SpeedFactor(-1.0f) == Approx(1.0f));
    CHECK(tb::SpeedFactor(std::numeric_limits<float>::quiet_NaN()) == Approx(1.0f));

    tb::Settings settings;
    settings.movementRule = tb::MovementRule::Speed;
    CHECK(tb::MovementBudget(settings, tb::SpeedFactor(7.35f), 10.0f) == Approx(14.2f).epsilon(0.01));
    CHECK(tb::MovementBudget(settings, tb::SpeedFactor(3.914f), 10.0f) == Approx(7.56f).epsilon(0.01));
    // The flanking angle the mod once read as a speed would have been 60
    // times a person's: not a multiplier, so a person's pace.
    CHECK(tb::MovementBudget(settings, 60.0f, 10.0f) == Approx(9.0f));
}

TEST_CASE("haste and slows multiply a creature's pace", "[rules]")
{
    CHECK(tb::MovementRate({}) == Approx(1.0f));
    const std::vector<float> haste{1.5f};
    CHECK(tb::MovementRate(haste) == Approx(1.5f));
    const std::vector<float> hasteAndGrease{1.5f, 0.5f};
    CHECK(tb::MovementRate(hasteAndGrease) == Approx(0.75f));
    // What no rate is: left out.
    const std::vector<float> junk{0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), 0.5f};
    CHECK(tb::MovementRate(junk) == Approx(0.5f));

    tb::Settings settings;
    settings.movementRule = tb::MovementRule::Speed;
    // A hasted person: 13.5 m; a hasted shriek is held at the maximum.
    CHECK(tb::MovementBudget(settings, tb::SpeedFactor(4.66f) * tb::MovementRate(haste), 10.0f) == Approx(13.5f));
    CHECK(tb::MovementBudget(settings, tb::SpeedFactor(7.35f) * tb::MovementRate(haste), 10.0f) == Approx(20.0f));
}

TEST_CASE("a talent in a fight asks for its target unless it is for its user alone, or a mode", "[rules]")
{
    CHECK(tb::AsksForTarget(4, 4));       // Shield Bash: a hostile creature
    CHECK(tb::AsksForTarget(2 | 1, 4));   // Heal: a friend or oneself
    CHECK(tb::AsksForTarget(16 | 32, 4)); // an area on the ground
    CHECK_FALSE(tb::AsksForTarget(1, 4)); // the mabari howl: its user
    CHECK_FALSE(tb::AsksForTarget(1, 2)); // Shield Wall, a mode
    CHECK_FALSE(tb::AsksForTarget(4, 2)); // a mode whatever its mask says
    CHECK_FALSE(tb::AsksForTarget(0, 4)); // no target at all
}
