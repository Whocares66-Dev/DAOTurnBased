#include "core/Rules.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

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

TEST_CASE("a swing's time is the game's formula by weapon style, speed and attack speed", "[rules][time]")
{
    using Catch::Approx;
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, -0.5f, 1.0f) == Approx(1.5f));   // a dagger
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, -0.1f, 1.0f) == Approx(1.9f));   // a longsword
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, 0.0f, 1.0f) == Approx(2.0f));    // a mace
    CHECK(tb::SwingSeconds(tb::WeaponStyle::None, 0.0f, 1.0f) == Approx(2.0f));      // unarmed
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Dual, -0.5f, 1.0f) == Approx(1.0f));     // a dagger's hand
    CHECK(tb::SwingSeconds(tb::WeaponStyle::TwoHanded, 0.1f, 1.0f) == Approx(2.6f)); // a battleaxe
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Ranged, 0.0f, 1.0f) == 0.0f);            // its animation's
    // Haste, and the clamps: over 1.5 is 1.5, under 0.5 is thrown away.
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, 0.0f, 0.75f) == Approx(1.5f));
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, 0.0f, 2.0f) == Approx(3.0f));
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, 0.0f, 0.45f) == Approx(2.0f));
}

TEST_CASE("a spell's conjuring is its start and its loops, and nothing without a conjure animation", "[rules][time]")
{
    using Catch::Approx;
    CHECK(tb::ConjureSeconds(245, 1, true) == Approx(3.47f));
    CHECK(tb::ConjureSeconds(245, 1, false) == Approx(2.94f));
    CHECK(tb::ConjureSeconds(244, 1, true) == Approx(3.87f));
    CHECK(tb::ConjureSeconds(245, 4, true) == Approx(11.57f));
    CHECK(tb::ConjureSeconds(0, 2, true) == 0.0f);            // a talent with a Speed and no animation
    CHECK(tb::ConjureSeconds(245, 0, true) == 0.0f);          // instant
    CHECK(tb::ConjureSeconds(300, 1, false) == Approx(3.6f)); // one not known: the stand-in
}

TEST_CASE("Survival's ranks add to initiative", "[rules]")
{
    tb::Settings s;
    s.initiativeDie = 1;
    tb::Rng rng{1};
    CHECK(tb::RollInitiative(rng, 10.0f, s) == 1);
    CHECK(tb::RollInitiative(rng, 10.0f, s, 3) == 7);
    CHECK(tb::RollInitiative(rng, 10.0f, s, 9) == 9); // four ranks at most
}

TEST_CASE("an attack's time: a person's swing by the formula, a shot, a creature's by its animation", "[rules][time]")
{
    tb::AttackFacts lyna;
    lyna.humanoid = true;
    lyna.armed = true;
    lyna.style = tb::WeaponStyle::Single;
    lyna.weaponSpeed = -0.5f;
    CHECK(tb::AttackSeconds(lyna) == Approx(1.5f)); // her dagger
    lyna.attackSpeedModifier = 0.75f;               // hasted
    CHECK(tb::AttackSeconds(lyna) == Approx(1.125f));
    tb::AttackFacts archer;
    archer.humanoid = true;
    archer.armed = true;
    archer.ranged = true;
    CHECK(tb::AttackSeconds(archer) == Approx(1.4f)); // a follower's shot
    archer.hero = true;
    CHECK(tb::AttackSeconds(archer) == Approx(1.6f)); // the player's own
    tb::AttackFacts spider;
    spider.animation = 1.833f;
    spider.attackSpeedModifier = 0.75f; // does not reach an animation's swing
    CHECK(tb::AttackSeconds(spider) == Approx(1.485f).margin(0.01f));
    tb::AttackFacts unarmed;
    unarmed.humanoid = true;
    unarmed.animation = 2.0f;
    CHECK(tb::AttackSeconds(unarmed) == Approx(1.62f));
    CHECK(tb::AttackSeconds(tb::AttackFacts{}) == 0.0f); // nothing known: the stand-in's
}

TEST_CASE("an ability's time: its cast less the blend, its conjuring, or a shot", "[rules][time]")
{
    tb::AbilityFacts dirty;
    dirty.cast = 1.8f;
    dirty.talent = true;
    CHECK(tb::AbilitySeconds(dirty) == Approx(1.512f));
    tb::AbilityFacts inferno;
    inferno.cast = 1.03f;
    inferno.conjure = 0.77f;
    inferno.conjureLoop = 2.70f;
    inferno.loops = 1;
    CHECK(tb::AbilitySeconds(inferno) == Approx(1.03f * 0.84f + 3.47f));
    inferno.loops = 0; // instant: no conjuring
    CHECK(tb::AbilitySeconds(inferno) == Approx(1.03f * 0.84f));
    tb::AbilityFacts pinning; // no cast animation, a bow in hand: a shot
    pinning.talent = true;
    pinning.ranged = true;
    CHECK(tb::AbilitySeconds(pinning) == Approx(1.4f));
    tb::AbilityFacts overwhelm; // a monster's special: nothing known
    CHECK(tb::AbilitySeconds(overwhelm) == 0.0f);
}

TEST_CASE("the damage that interrupts a conjuring: by Combat Training and level, or by rank and level", "[rules]")
{
    CHECK(tb::InterruptThreshold(true, 0, 6) == Approx(8.0f));   // 5 and half her level
    CHECK(tb::InterruptThreshold(true, 2, 10) == Approx(30.0f)); // 20, 5, 5
    CHECK(tb::InterruptThreshold(true, 3, 10) == Approx(40.0f));
    CHECK(tb::InterruptThreshold(true, 4, 10) == Approx(50.0f)); // the fourth, where the script counts it
    CHECK(tb::InterruptThreshold(true, -1, 10) == Approx(10.0f));
    // Anyone else: its kind's ranks and its level; as shipped where the
    // game's table is not read.
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(2), 7) == Approx(27.0f));  // a normal creature
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(3), 7) == Approx(37.0f));  // a lieutenant
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(4), 10) == Approx(50.0f)); // a boss
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(5), 10) == Approx(70.0f)); // an elite boss
    // (autoscale.gda's NCombatTraining, read 2026-10-02: 0 for a critter, 1
    // for a weak normal creature, 2, 3, 4, and 6 for an elite boss.)
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(1), 3) == Approx(3.0f));   // a critter
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(11), 3) == Approx(13.0f)); // a weak one
    CHECK(tb::InterruptThreshold(false, tb::TrainingOfRankAsShipped(99), 3) == Approx(3.0f));
    CHECK(tb::InterruptThreshold(false, 5, 10) == Approx(60.0f)); // a table a mod changed
}

namespace
{
// rules_core.ncs as shipped, from 3773 (the game's designerscripts.rim, read
// 2026-10-02): the four HasAbility calls of the interruption rule.
std::vector<std::uint8_t> ShippedRules()
{
    return {0x00, 0x8c, 0x2d, 0x00, 0x04, 0x03, 0x00, 0x01, 0x87, 0x04, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x05,
            0x00, 0x00, 0xfc, 0x02, 0x04, 0x03, 0x00, 0x01, 0x87, 0x05, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x05,
            0x00, 0x00, 0xfc, 0x02, 0x14, 0x20, 0x04, 0x03, 0x00, 0x01, 0x87, 0x06, 0x04, 0x06, 0x00, 0x00, 0x00,
            0x00, 0x05, 0x00, 0x00, 0xfc, 0x02, 0x14, 0x20, 0x04, 0x03, 0x00, 0x01, 0x87, 0x08, 0x04, 0x06, 0x00,
            0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0xfc, 0x02, 0x14, 0x20, 0x01, 0x01, 0xff, 0xff, 0xff, 0xf0, 0x00,
            0x04, 0x1b, 0x00, 0xff, 0xff, 0xff, 0xfc, 0x04, 0x03, 0x00, 0x00, 0x00, 0x05, 0x02, 0x03};
}
} // namespace

TEST_CASE("Combat Training's four ranks are the abilities the installed script names", "[rules]")
{
    using Ids = std::array<std::int32_t, 4>;
    // As shipped: the fourth is 100104, which is no ability, so Master
    // Combat Training (100103) never counts.
    const std::vector<std::uint8_t> shipped = ShippedRules();
    CHECK(tb::CombatTrainingIds(shipped) == Ids{100100, 100101, 100102, 100104});
    CHECK(tb::kCombatTrainingAsShipped == Ids{100100, 100101, 100102, 100104});
    // A mended script: the fourth is the ability's own id, and is read.
    std::vector<std::uint8_t> mended = shipped;
    REQUIRE(mended.at(64) == 0x08);
    mended.at(64) = 0x07;
    CHECK(tb::CombatTrainingIds(mended) == Ids{100100, 100101, 100102, 100103});
    // A constant is `04 03` and its four bytes: bytes that only look like
    // one from the second on are passed over.
    std::vector<std::uint8_t> decoy = mended;
    const std::uint8_t looksLike[] = {0x07, 0x03, 0x00, 0x01, 0x87, 0x09};
    decoy.insert(decoy.begin() + 50, std::begin(looksLike), std::end(looksLike));
    CHECK(tb::CombatTrainingIds(decoy) == Ids{100100, 100101, 100102, 100103});
    // All four bytes of it, the high one first: a mod's own ability may have
    // a large id.
    std::vector<std::uint8_t> large = shipped;
    large.at(61) = 0x01;
    CHECK(tb::CombatTrainingIds(large) == Ids{100100, 100101, 100102, 0x01018708});
    // Further into a script, and after a first 100100 that is not the rule's.
    std::vector<std::uint8_t> later(300, 0x00);
    const std::uint8_t alone[] = {0x04, 0x03, 0x00, 0x01, 0x87, 0x04};
    later.insert(later.begin() + 20, std::begin(alone), std::end(alone));
    later.insert(later.end(), mended.begin(), mended.end());
    CHECK(tb::CombatTrainingIds(later) == Ids{100100, 100101, 100102, 100103});
}

TEST_CASE("a script that does not hold the rule, or none: the shipped four", "[rules]")
{
    using Ids = std::array<std::int32_t, 4>;
    const Ids shipped{100100, 100101, 100102, 100104};
    CHECK(tb::CombatTrainingIds({}) == shipped);
    CHECK(tb::CombatTrainingIds(std::vector<std::uint8_t>(64, 0x04)) == shipped);
    // Cut off after the third: no fourth to read.
    std::vector<std::uint8_t> cut = ShippedRules();
    cut.resize(58);
    CHECK(tb::CombatTrainingIds(cut) == shipped);
    // The first three are not the three: something else's constants.
    std::vector<std::uint8_t> other = ShippedRules();
    other.at(26) = 0x09; // the second is 100105
    other.at(64) = 0x07;
    CHECK(tb::CombatTrainingIds(other) == shipped);
    std::vector<std::uint8_t> third = ShippedRules();
    third.at(45) = 0x09;
    third.at(64) = 0x07;
    CHECK(tb::CombatTrainingIds(third) == shipped);
    // The fourth too far from the third to be the rule's.
    std::vector<std::uint8_t> far = ShippedRules();
    far.at(64) = 0x07;
    far.insert(far.begin() + 46, 60, 0x00);
    CHECK(tb::CombatTrainingIds(far) == shipped);
    // A fourth that is no id.
    std::vector<std::uint8_t> nothing = ShippedRules();
    nothing.at(61) = 0x00;
    nothing.at(62) = 0x00;
    nothing.at(63) = 0x00;
    nothing.at(64) = 0x00;
    CHECK(tb::CombatTrainingIds(nothing) == shipped);
    // The very end of a script: nothing is read past it.
    const std::vector<std::uint8_t> end{0x04, 0x03, 0x00, 0x01, 0x87};
    CHECK(tb::CombatTrainingIds(end) == shipped);
}

TEST_CASE("a swing's time is what the scripts last set for it, once one has been seen", "[rules][time]")
{
    using Catch::Approx;
    tb::LearnedSwings swings;
    const auto sword = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, -0.1f, 1.0f);
    // Nothing seen: the formula is the caller's first guess.
    CHECK_FALSE(swings.Seconds(sword));
    REQUIRE(swings.Learn(sword, 1.9f));
    CHECK(*swings.Seconds(sword) == Approx(1.9f));
    // Scripts that are not the shipped ones (a mod's): theirs is what counts,
    // and the last one set.
    REQUIRE(swings.Learn(sword, 1.7f));
    CHECK(*swings.Seconds(sword) == Approx(1.7f));
    // By what it depended on: another weapon, another speed, another style.
    CHECK_FALSE(swings.Seconds(tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, -0.5f, 1.0f)));
    CHECK_FALSE(swings.Seconds(tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, -0.1f, 0.75f)));
    CHECK_FALSE(swings.Seconds(tb::LearnedSwings::KeyOf(tb::WeaponStyle::TwoHanded, -0.1f, 1.0f)));
    // Haste with Momentum, 0.45: the shipped script throws it away and the
    // mended one does not. Whichever is in the game is what is read.
    const auto hasted = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, 0.0f, 0.45f);
    CHECK(tb::SwingSeconds(tb::WeaponStyle::Single, 0.0f, 0.45f) == Approx(2.0f));
    REQUIRE(swings.Learn(hasted, 1.0f));
    CHECK(*swings.Seconds(hasted) == Approx(1.0f));
    CHECK(*swings.Seconds(sword) == Approx(1.7f));
}

TEST_CASE("a weapon in each hand: the mean of the last two swings, the hands taking turns", "[rules][time]")
{
    using Catch::Approx;
    tb::LearnedSwings swings;
    const auto pair = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Dual, -0.5f, 1.0f);
    REQUIRE(swings.Learn(pair, 1.0f)); // the dagger's hand
    CHECK(*swings.Seconds(pair) == Approx(1.0f));
    REQUIRE(swings.Learn(pair, 1.4f)); // the longsword's
    CHECK(*swings.Seconds(pair) == Approx(1.2f));
    REQUIRE(swings.Learn(pair, 1.0f));
    CHECK(*swings.Seconds(pair) == Approx(1.2f));
    // One weapon: the last, not a mean.
    const auto one = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, -0.5f, 1.0f);
    REQUIRE(swings.Learn(one, 1.5f));
    REQUIRE(swings.Learn(one, 1.3f));
    CHECK(*swings.Seconds(one) == Approx(1.3f));
}

TEST_CASE("a time no swing could take is not kept", "[rules][time]")
{
    using Catch::Approx;
    tb::LearnedSwings swings;
    const auto key = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, 0.0f, 1.0f);
    CHECK_FALSE(swings.Learn(key, 0.0f));
    CHECK_FALSE(swings.Learn(key, 0.2f));
    CHECK_FALSE(swings.Learn(key, 10.5f));
    CHECK_FALSE(swings.Learn(key, -1.0f));
    CHECK_FALSE(swings.Learn(key, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(swings.Learn(key, std::numeric_limits<float>::infinity()));
    CHECK_FALSE(swings.Seconds(key));
    REQUIRE(swings.Learn(key, 0.25f));
    REQUIRE(swings.Learn(key, 10.0f));
    CHECK(*swings.Seconds(key) == Approx(10.0f));
    // A key from numbers that are not: nothing odd.
    const auto odd = tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, std::numeric_limits<float>::quiet_NaN(),
                                              std::numeric_limits<float>::infinity());
    CHECK(odd == tb::LearnedSwings::KeyOf(tb::WeaponStyle::Single, 0.0f, 0.0f));
}

TEST_CASE("a shot costs what its archer's last shot took, by its attack speed", "[rules][time]")
{
    using Catch::Approx;
    // The user, of the flat 1.4 and 1.6 s: "we should read these".
    tb::LearnedSwings shots;
    const auto tamlen = tb::LearnedSwings::ShotOf(0x10d, 1.0f);
    CHECK_FALSE(shots.Seconds(tamlen)); // the first: the flat figure, the caller's
    REQUIRE(shots.Learn(tamlen, 1.41f));
    CHECK(*shots.Seconds(tamlen) == Approx(1.41f));
    // Another archer, or the same one hasted: their own.
    CHECK_FALSE(shots.Seconds(tb::LearnedSwings::ShotOf(0x82, 1.0f)));
    CHECK_FALSE(shots.Seconds(tb::LearnedSwings::ShotOf(0x10d, 0.75f)));
    // Not a swing's key, whatever its numbers.
    CHECK_FALSE(shots.Seconds(tb::LearnedSwings::KeyOf(tb::WeaponStyle::Ranged, 0.0f, 1.0f)));
    // The mean of the last two: measured shots differ by a tenth.
    REQUIRE(shots.Learn(tamlen, 1.81f));
    CHECK(*shots.Seconds(tamlen) == Approx(1.61f));
    REQUIRE(shots.Learn(tamlen, 1.61f));
    CHECK(*shots.Seconds(tamlen) == Approx(1.71f));
}
