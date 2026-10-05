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

namespace
{
// Ortan Thaig, 2026-10-04 (issue 42), in the party's own order: the hero, the
// party's leader by the game's word alive or dead, and three companions.
constexpr tb::ObjectId kLyna = 0x82;
constexpr tb::ObjectId kOghren = 0x2df9e;
constexpr tb::ObjectId kMorrigan = 0x66c2;
constexpr tb::ObjectId kZevran = 0x2511f;

// Who of them lives; each living one in combat, as in a fight.
std::vector<tb::PartyMember> Party(bool lyna, bool oghren, bool morrigan, bool zevran)
{
    return {tb::PartyMember{kLyna, lyna, lyna}, tb::PartyMember{kOghren, oghren, oghren},
            tb::PartyMember{kMorrigan, morrigan, morrigan}, tb::PartyMember{kZevran, zevran, zevran}};
}
} // namespace

TEST_CASE("the leader stands for the party while alive, then the one the player controls, then whoever lives",
          "[rules][party]")
{
    // The leader, alive: whoever the player controls (with the party played
    // by hand the mod itself moves control with each member's turn).
    CHECK(tb::StandsForTheParty(Party(true, true, true, true), kLyna, kLyna) == kLyna);
    CHECK(tb::StandsForTheParty(Party(true, true, true, true), kLyna, kZevran) == kLyna);
    // A companion leads (a script's SetPartyLeader: the player selecting
    // someone does not change who leads): it stands for the party.
    CHECK(tb::StandsForTheParty(Party(true, true, true, true), kMorrigan, kLyna) == kMorrigan);
    // Issue 42: the hero fell and the game went on naming her the leader,
    // the player's control gone to the last one standing.
    CHECK(tb::StandsForTheParty(Party(false, false, false, true), kLyna, kZevran) == kZevran);
    // The one controlled, though another of the party comes first in its
    // order: where the player is looking.
    CHECK(tb::StandsForTheParty(Party(false, true, true, true), kLyna, kZevran) == kZevran);
    // The one controlled has fallen too, or the game names nobody, or names
    // one that is not of the party: the first of it living.
    CHECK(tb::StandsForTheParty(Party(false, false, true, true), kLyna, kOghren) == kMorrigan);
    CHECK(tb::StandsForTheParty(Party(false, true, true, true), kLyna, tb::kNoObject) == kOghren);
    CHECK(tb::StandsForTheParty(Party(false, true, true, true), kLyna, 0x3220e) == kOghren);
    // A leader that is not of the party stands for nothing.
    CHECK(tb::StandsForTheParty(Party(true, true, true, true), 0x3220e, kZevran) == kZevran);
    CHECK(tb::StandsForTheParty(Party(true, true, true, true), tb::kNoObject, tb::kNoObject) == kLyna);
    // Nobody lives: nobody.
    CHECK(tb::StandsForTheParty(Party(false, false, false, false), kLyna, kLyna) == tb::kNoObject);
    CHECK(tb::StandsForTheParty({}, kLyna, kLyna) == tb::kNoObject);
}

TEST_CASE("the player acts as the one controlled while it lives, wherever the leader is", "[rules][party]")
{
    // The leader is a fixed one of the party (the engine's leader field,
    // which the player selecting someone does not change), so a word to the
    // player goes over the one controlled: the companion the player has gone
    // on with, the leader alive elsewhere.
    CHECK(tb::ActsForThePlayer(Party(true, true, true, true), kLyna, kZevran) == kZevran);
    CHECK(tb::ActsForThePlayer(Party(true, true, true, true), kLyna, kLyna) == kLyna);
    // The one controlled has fallen, or is nobody, or is not of the party:
    // who stands for the party, the leader first.
    CHECK(tb::ActsForThePlayer(Party(true, true, true, false), kLyna, kZevran) == kLyna);
    CHECK(tb::ActsForThePlayer(Party(true, true, true, true), kLyna, tb::kNoObject) == kLyna);
    CHECK(tb::ActsForThePlayer(Party(true, true, true, true), kMorrigan, 0x3220e) == kMorrigan);
    CHECK(tb::ActsForThePlayer(Party(false, true, true, false), kLyna, kZevran) == kOghren);
    CHECK(tb::ActsForThePlayer(Party(false, false, false, false), kLyna, kZevran) == tb::kNoObject);
}

TEST_CASE("the party is measured from its living members, the one who stands for it first", "[rules][party]")
{
    using Ids = std::vector<tb::ObjectId>;
    // Everyone alive: the leader first, as a scan always began, then the
    // rest in the party's own order, each once.
    CHECK(tb::LivingMembers(Party(true, true, true, true), kLyna, kLyna) == Ids{kLyna, kOghren, kMorrigan, kZevran});
    CHECK(tb::LivingMembers(Party(true, true, true, true), kMorrigan, kMorrigan) ==
          Ids{kMorrigan, kLyna, kOghren, kZevran});
    // The fallen are not measured from: nobody is near the party for being
    // near a body (issue 42, the leader's 83 m from the last one standing).
    CHECK(tb::LivingMembers(Party(false, false, false, true), kLyna, kZevran) == Ids{kZevran});
    CHECK(tb::LivingMembers(Party(false, true, false, true), kLyna, kZevran) == Ids{kZevran, kOghren});
    CHECK(tb::LivingMembers(Party(false, true, true, false), kLyna, kZevran) == Ids{kOghren, kMorrigan});
    // Nobody lives: nobody to measure from.
    CHECK(tb::LivingMembers(Party(false, false, false, false), kLyna, kLyna).empty());
}

TEST_CASE("the party is in combat while any living member is", "[rules][party]")
{
    // Issue 42: the fallen leader's combat state was 0 and was the party's
    // whole answer, with the last one standing in combat beside three
    // hostiles: the rule that draws in a hostile that has not noticed was off.
    std::vector<tb::PartyMember> party = Party(false, false, false, true);
    CHECK(tb::PartyIsInCombat(party));
    // One of several is enough, whoever it is: the game takes each into
    // combat as it perceives a hostile.
    party = Party(true, true, true, true);
    for (tb::PartyMember &member : party)
    {
        member.inCombat = member.id == kMorrigan;
    }
    CHECK(tb::PartyIsInCombat(party));
    // Nobody living is in combat: at peace.
    for (tb::PartyMember &member : party)
    {
        member.inCombat = false;
    }
    CHECK_FALSE(tb::PartyIsInCombat(party));
    // A fallen member's combat state is nobody's: read as set, it is still
    // not the party's.
    party = Party(false, true, true, true);
    for (tb::PartyMember &member : party)
    {
        member.inCombat = member.id == kLyna;
    }
    CHECK_FALSE(tb::PartyIsInCombat(party));
    CHECK_FALSE(tb::PartyIsInCombat({}));
}

TEST_CASE("a creature is as far from the party as from the nearest of its living members", "[rules][party]")
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const tb::Settings settings;
    REQUIRE(settings.combatRadius == Approx(50.0f));
    // Issue 42, the second time, nobody dead: a Forgotten Spirit 70 m from
    // the leader and 1.9 m from a companion, the two others near him. It is
    // 1.9 m from the party, wherever the leader comes in the list.
    const std::vector<float> split{70.4f, 6.2f, 1.9f, 4.4f};
    CHECK(tb::MetersFromTheParty(split) == Approx(1.9f));
    CHECK(tb::WithinReach(tb::MetersFromTheParty(split), settings));
    const std::vector<float> leaderLast{6.2f, 1.9f, 4.4f, 70.4f};
    CHECK(tb::MetersFromTheParty(leaderLast) == Approx(1.9f));
    // The first time, the leader fallen 83.3 m off: of the living there was
    // one, 1.9 m from it. From her body alone it was out of every reach.
    CHECK(tb::WithinReach(tb::MetersFromTheParty(std::vector<float>{1.9f}), settings));
    CHECK_FALSE(tb::WithinReach(tb::MetersFromTheParty(std::vector<float>{83.3f}), settings));
    // The radius is from the nearest, to the meter, and the setting's.
    CHECK(tb::WithinReach(tb::MetersFromTheParty(std::vector<float>{83.3f, 50.0f}), settings));
    CHECK_FALSE(tb::WithinReach(tb::MetersFromTheParty(std::vector<float>{83.3f, 50.5f}), settings));
    tb::Settings wide;
    wide.combatRadius = 90.0f;
    CHECK(tb::WithinReach(tb::MetersFromTheParty(std::vector<float>{83.3f, 95.0f}), wide));
    // Past the join radius and within this one: it is the fight's reach
    // that is asked, not how near a bystander must be.
    REQUIRE(settings.joinRadius < 45.0f);
    CHECK(tb::WithinReach(45.0f, settings));
    // Nobody living to be near, or no distance that is one: infinitely far,
    // and within no reach. A distance that is none beside one that is one
    // is passed over.
    CHECK(tb::MetersFromTheParty({}) == inf);
    CHECK_FALSE(tb::WithinReach(tb::MetersFromTheParty({}), settings));
    CHECK(tb::MetersFromTheParty(std::vector<float>{nan}) == inf);
    CHECK(tb::MetersFromTheParty(std::vector<float>{nan, 12.0f, inf}) == Approx(12.0f));
    CHECK(tb::MetersFromTheParty(std::vector<float>{12.0f, nan}) == Approx(12.0f));
    CHECK_FALSE(tb::WithinReach(nan, settings));
    CHECK_FALSE(tb::WithinReach(inf, settings));
}

TEST_CASE("a battle near the party that is not the party's waits outside its fight", "[rules]")
{
    // The Korcari Wilds, issue 6: a hurlock and four wolves fighting each
    // other 45 m off, in plain sight, joined the party's fight of four.
    const tb::Settings settings;
    REQUIRE(settings.joinBySight);
    REQUIRE(settings.joinRadius == Approx(30.0f));
    const auto sighting = [](bool fightsParty, bool seen, float meters) {
        tb::Sighting s;
        s.fightsParty = fightsParty;
        s.seen = seen;
        s.meters = meters;
        return s;
    };
    // Seen, fighting someone else: by how near.
    CHECK_FALSE(tb::InThePartysFight(sighting(false, true, 45.0f), settings));
    CHECK_FALSE(tb::InThePartysFight(sighting(false, true, 30.5f), settings));
    CHECK(tb::InThePartysFight(sighting(false, true, 30.0f), settings));
    CHECK(tb::InThePartysFight(sighting(false, true, 12.0f), settings));
    // Fighting the party: from anywhere the radius reaches (an archer), seen
    // or not (through a door, from a room still black).
    CHECK(tb::InThePartysFight(sighting(true, true, 45.0f), settings));
    CHECK(tb::InThePartysFight(sighting(true, false, 45.0f), settings));
    // Not seen and not on the party: outside, however near (the next room).
    CHECK_FALSE(tb::InThePartysFight(sighting(false, false, 6.0f), settings));
    // A distance that is none keeps it out.
    CHECK_FALSE(tb::InThePartysFight(sighting(false, true, std::numeric_limits<float>::quiet_NaN()), settings));
    CHECK_FALSE(tb::InThePartysFight(sighting(false, true, std::numeric_limits<float>::infinity()), settings));
    // The setting moves the line.
    tb::Settings wide;
    wide.joinRadius = 50.0f;
    CHECK(tb::InThePartysFight(sighting(false, true, 45.0f), wide));
    // Without the rule of sight, everyone in the radius.
    tb::Settings everyone;
    everyone.joinBySight = false;
    CHECK(tb::InThePartysFight(sighting(false, false, 45.0f), everyone));
}

TEST_CASE("with the party in combat and nobody to fight, a hostile seen within the join radius is fighting though it "
          "has not noticed",
          "[rules]")
{
    // Issue 20: three corpses clawing at a cell's door 20 m from the party
    // in Redcliffe's dungeon, the game in combat and nobody to take turns
    // against until one was struck.
    const tb::Settings settings;
    REQUIRE(settings.joinRadius == Approx(30.0f));
    const auto sighting = [](bool seen, float meters) {
        tb::Sighting s;
        s.seen = seen;
        s.meters = meters;
        return s;
    };
    const bool inCombat = true;
    const bool nobody = false; // nobody in the party's fight to fight
    const bool fresh = false;  // not drawn in before
    CHECK(tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, 20.0f), settings));
    CHECK(tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, 30.0f), settings));
    // Not beyond the radius: a second band 40 m down the road is not in
    // the fight just won until the party nears it.
    CHECK_FALSE(tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, 30.5f), settings));
    // Not unseen: the next room's, behind its door.
    CHECK_FALSE(tb::FightsUnawares(inCombat, nobody, fresh, sighting(false, 6.0f), settings));
    // Not with the party at peace: walking past a hostile that has not
    // noticed is the game's to make a fight of, not the mod's.
    CHECK_FALSE(tb::FightsUnawares(false, nobody, fresh, sighting(true, 6.0f), settings));
    // A distance that is none keeps it out.
    CHECK_FALSE(
        tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, std::numeric_limits<float>::quiet_NaN()), settings));
    // Sight is asked for whatever joinBySight says.
    tb::Settings everyone;
    everyone.joinBySight = false;
    CHECK_FALSE(tb::FightsUnawares(inCombat, nobody, fresh, sighting(false, 6.0f), everyone));
    CHECK(tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, 6.0f), everyone));
    // The radius is the setting's.
    tb::Settings wide;
    wide.joinRadius = 50.0f;
    CHECK(tb::FightsUnawares(inCombat, nobody, fresh, sighting(true, 45.0f), wide));
}

TEST_CASE("a hostile that has not noticed is left out of a fight that has an enemy, and one drawn in stays in",
          "[rules]")
{
    // Issue 24: the Fade's Darkspawn Invasion, a hurlock fighting the hero
    // and three more darkspawn in sight within 30 m that had not noticed:
    // drawn in, a pull of three became seven. The user, 2026-10-03, of
    // drawing them in only when there is nobody else to fight: "Yeah, let's
    // do that".
    const tb::Settings settings;
    tb::Sighting near;
    near.seen = true;
    near.meters = 12.0f;
    const bool inCombat = true;
    const bool someone = true; // an enemy is in the party's fight already
    CHECK_FALSE(tb::FightsUnawares(inCombat, someone, false, near, settings));
    // One drawn in while there was nobody stays in as another notices: its
    // card does not go and come again. Its sighting is not asked again.
    CHECK(tb::FightsUnawares(inCombat, someone, true, near, settings));
    CHECK(tb::FightsUnawares(inCombat, someone, true, tb::Sighting{}, settings));
    CHECK(tb::FightsUnawares(inCombat, false, true, tb::Sighting{}, settings));
    // But not past the party's own combat.
    CHECK_FALSE(tb::FightsUnawares(false, someone, true, near, settings));
    CHECK_FALSE(tb::FightsUnawares(false, false, true, near, settings));
}

TEST_CASE("a hostile not in combat that perceives no party member and nobody else hostile to it is unaware", "[rules]")
{
    // Issue 29: unaware is the engine's word, read of its perception and of
    // its combat state. The skeletons of the Brecilian ruins' lower level,
    // in the fight and never having noticed the party ("chose nothing:
    // combat state 0").
    tb::Perception nobody;
    nobody.ofParty = false;
    nobody.ofFoes = false;
    nobody.inCombat = false;
    CHECK(tb::Unaware(tb::Side::Enemy, nobody));
    // One the game has in combat is told its turn has come as before,
    // whatever it perceives at this moment: the scripts run its combat AI
    // then, and one with an AI of its own may act all the same.
    tb::Perception fighting = nobody;
    fighting.inCombat = true;
    CHECK_FALSE(tb::Unaware(tb::Side::Enemy, fighting));
    // Whether the party perceives it is not the question: the party's seeing
    // it is how it came to be in the fight.
    nobody.byParty = false;
    CHECK(tb::Unaware(tb::Side::Enemy, nobody));
    // It perceives a party member: it has noticed.
    tb::Perception party = nobody;
    party.ofParty = true;
    CHECK_FALSE(tb::Unaware(tb::Side::Enemy, party));
    // It perceives a foe that is not the party's (an ally of the party's it
    // is fighting): it has someone to choose.
    tb::Perception foe = nobody;
    foe.ofFoes = true;
    CHECK_FALSE(tb::Unaware(tb::Side::Enemy, foe));
    // Nothing read: everything, and not unaware.
    CHECK_FALSE(tb::Unaware(tb::Side::Enemy, tb::Perception{}));
    // A hostile's only: an ally's turn, or a party member's, is not skipped
    // for what it perceives.
    CHECK_FALSE(tb::Unaware(tb::Side::Ally, nobody));
    CHECK_FALSE(tb::Unaware(tb::Side::Party, nobody));
}

TEST_CASE("one in the fight stays in it while the engine's perception keeps it there", "[rules]")
{
    // Issue 29: the game ends combat by perception, not by a distance, and
    // the mod's 50 m is no longer a way out of a fight.
    const bool found = true; // by the scan's own rules: in combat within combat_radius, or drawn in unawares
    const tb::Perception everything;
    tb::Perception nothing;
    nothing.byParty = false;
    nothing.ofParty = false;
    nothing.ofFoes = false;
    // Found fighting, it stays, perceived or not, as it always did: those
    // rules bring it in, and it would come again at once.
    CHECK(tb::StaysInTheFight(tb::Side::Enemy, found, nothing));
    CHECK(tb::StaysInTheFight(tb::Side::Ally, found, nothing));
    // An enemy not found fighting (out of combat, or beyond the radius):
    // while a party member perceives it, or it perceives a party member.
    tb::Perception watched = nothing;
    watched.byParty = true;
    tb::Perception watching = nothing;
    watching.ofParty = true;
    CHECK(tb::StaysInTheFight(tb::Side::Enemy, !found, everything));
    CHECK(tb::StaysInTheFight(tb::Side::Enemy, !found, watched));
    CHECK(tb::StaysInTheFight(tb::Side::Enemy, !found, watching));
    // Neither: it has left. Whom else it perceives does not make it the
    // party's fight.
    CHECK_FALSE(tb::StaysInTheFight(tb::Side::Enemy, !found, nothing));
    tb::Perception elsewhere = nothing;
    elsewhere.ofFoes = true;
    CHECK_FALSE(tb::StaysInTheFight(tb::Side::Enemy, !found, elsewhere));
    // An ally of the party's: while it perceives someone hostile to it, the
    // test the game keeps it in combat by. That the party and it perceive
    // each other is no fight.
    CHECK(tb::StaysInTheFight(tb::Side::Ally, !found, elsewhere));
    CHECK(tb::StaysInTheFight(tb::Side::Ally, !found, everything));
    CHECK_FALSE(tb::StaysInTheFight(tb::Side::Ally, !found, nothing));
    tb::Perception friendly = nothing;
    friendly.byParty = true;
    friendly.ofParty = true;
    CHECK_FALSE(tb::StaysInTheFight(tb::Side::Ally, !found, friendly));
    // The party's own are always in its fight.
    CHECK(tb::StaysInTheFight(tb::Side::Party, !found, nothing));
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

TEST_CASE("the sides that fight each other: the enemies, and the party with its allies", "[rules]")
{
    using tb::Side;
    CHECK(tb::Opposed(Side::Party, Side::Enemy));
    CHECK(tb::Opposed(Side::Ally, Side::Enemy));
    CHECK(tb::Opposed(Side::Enemy, Side::Party));
    CHECK(tb::Opposed(Side::Enemy, Side::Ally));
    CHECK_FALSE(tb::Opposed(Side::Party, Side::Party));
    CHECK_FALSE(tb::Opposed(Side::Party, Side::Ally));
    CHECK_FALSE(tb::Opposed(Side::Ally, Side::Party));
    CHECK_FALSE(tb::Opposed(Side::Ally, Side::Ally));
    CHECK_FALSE(tb::Opposed(Side::Enemy, Side::Enemy));
}

TEST_CASE("a kept attack whose target fell is turned on the nearest enemy its creature can strike without a step",
          "[rules][time]")
{
    // Issue 22, the user's choice of 2026-10-03. Each as the game layer
    // measured it: standing, hostile, seen, how far, and whether in the
    // attacker's reach, which is the engine's own answer for the pair
    // (issue 11: the mod has no reach rule of its own).
    const auto foe = [](tb::ObjectId id, float meters, bool inReach = true) {
        tb::Foe f;
        f.id = id;
        f.standing = true;
        f.hostile = true;
        f.seen = true;
        f.meters = meters;
        f.inReach = inReach;
        return f;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // Nobody to turn it on: dropped, as before.
    CHECK(tb::NearestInReach({}) == tb::kNoObject);
    // The nearest of those in reach, wherever it stands in the list.
    const std::vector<tb::Foe> three{foe(0x201, 1.9f), foe(0x202, 1.2f), foe(0x203, 1.6f)};
    CHECK(tb::NearestInReach(three) == 0x202);
    // Out of reach is a walk, and no walk was paid for: not that one, however
    // few the others; and with all of them out of reach, none.
    const std::vector<tb::Foe> oneNear{foe(0x201, 6.0f, false), foe(0x202, 2.0f), foe(0x203, 30.0f, false)};
    CHECK(tb::NearestInReach(oneNear) == 0x202);
    const std::vector<tb::Foe> allFar{foe(0x201, 6.0f, false), foe(0x202, 2.1f, false)};
    CHECK(tb::NearestInReach(allFar) == tb::kNoObject);
    // In reach is the engine's word and not the distance: one nearer and out
    // of reach (its place at that target past a doorway, say) is passed over
    // for one further off and in it.
    const std::vector<tb::Foe> ring{foe(0x201, 1.4f, false), foe(0x202, 2.2f)};
    CHECK(tb::NearestInReach(ring) == 0x202);
    // Two as near: the first named.
    const std::vector<tb::Foe> tie{foe(0x201, 1.5f), foe(0x202, 1.5f)};
    CHECK(tb::NearestInReach(tie) == 0x201);

    // A nearer one that has fallen, is not hostile or is not seen is passed
    // over for one that is all three.
    std::vector<tb::Foe> flawed{foe(0x201, 0.8f), foe(0x202, 1.0f), foe(0x203, 1.2f), foe(0x204, 1.8f)};
    flawed[0].standing = false;
    flawed[1].hostile = false;
    flawed[2].seen = false;
    CHECK(tb::NearestInReach(flawed) == 0x204);
    flawed[3].seen = false;
    CHECK(tb::NearestInReach(flawed) == tb::kNoObject);

    // A bow reaches far. One in reach and not seen (in stealth) is not shot
    // at; the next that is seen is.
    std::vector<tb::Foe> archer{foe(0x101, 21.6f), foe(0x102, 38.0f), foe(0x103, 50.0f, false)};
    CHECK(tb::NearestInReach(archer) == 0x101);
    archer[0].seen = false;
    CHECK(tb::NearestInReach(archer) == 0x102);
    archer[1].seen = false;
    CHECK(tb::NearestInReach(archer) == tb::kNoObject); // the third is past the bow's reach

    // A distance that is none is no nearest.
    CHECK(tb::NearestInReach(std::vector<tb::Foe>{foe(0x201, nan)}) == tb::kNoObject);
    CHECK(tb::NearestInReach(std::vector<tb::Foe>{foe(0x201, inf)}) == tb::kNoObject);
    CHECK(tb::NearestInReach(std::vector<tb::Foe>{foe(0x201, nan), foe(0x202, 1.0f)}) == 0x202);
}
