// Cooldowns in turns, as the engine sets them (issue 57). Scripted cases in
// the stand-in for the game (World.h), whose engine keeps a list of what
// cools on each creature as the game's cooldown manager does, ticks it on
// the wall, and tells the machine of each one it sets as the game layer's
// callback does (DAOLib's dao::abilities::OnCooldownSet).
//
// In a turn-based fight an ability's cooldown passes on its user's own clock,
// which moves as its bar is spent and a turn's seconds with each of its
// turns, and never on the wall (dev/DESIGN.md, "Cooldowns in turns"). It was
// kept three ways, and the first two each missed what they were not looking
// at.
//
// Until 2026-10-06 the machine reckoned, from each command it let through,
// what the engine would start: the ability table's seconds; a mode turned
// off, a turn's worth; a mode turned on, nothing. A Cultist Overseer whose
// sustained spells would not stand ordered one 47 times in a turn. The user:
// "Shouldn't everything automatically keep its cooldown? If we do so in a
// general way, we shouldn't miss anything", and of a mode switched off, which
// waited for its user's next turn by a rule of the mod's: "It should obey the
// cooldown logic. Not just blindly wait for next turn."
//
// Until 2026-10-08 it read what the engine had cooling on every fighter at
// each look at everyone, a creature at a command of its own turn when the
// command was over. All else was right in play, and the Overseer looped as
// before: its spell's cooldown begins with the cast and is over before the
// command is, and its script orders again as each command ends, so its turn
// never came out of that command to be looked at. The user: "We should unify
// the logic so sustained doesn't slip through the cracks. Keep things
// standardized and simple. Always fight against the entropy."
//
// Now the machine is told as the engine sets one, whatever set it and
// whenever. What is promised:
//
//   - a cooldown the engine sets on one of a fight is taken onto its
//     creature's own clock as it is set: its seconds, whoever's turn it is,
//     whatever its creature is at, under a menu or in a conversation. The
//     machine reckons none, and looks for none;
//   - what was already cooling on a creature as it came into the fight is
//     read once, then, and taken at what is left of it;
//   - held, it does not come back by waiting, however little is left, and is
//     ready when its seconds have passed on its creature's clock, in the
//     middle of a turn too;
//   - the machine does not know or ask what kind of ability it is: a mode's,
//     an item's (by its item's tag, as the engine keeps it) and a talent's
//     are one thing to it;
//   - one the engine sets again while it is held is taken again at the
//     engine's new word, and one it sets to nothing is let go;
//   - as the fight ends, each still held has its own seconds back; and after
//     a fight a load took the world from under, as the world is there again
//     (issue 90), unless it is another game's.
#include "World.h"

#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

constexpr std::int32_t kShieldBash = 617;
constexpr std::int32_t kOverpower = 603;
// Spell Shield: a mode, half a second in the ability table.
constexpr std::int32_t kSpellShield = 10400;
// A health poultice's ability, and the tag of the item it comes from.
constexpr std::int32_t kPoultice = 200010;
constexpr std::wstring_view kPoulticeTag = L"gen_im_qck_health_101";
constexpr std::int32_t kHour = TurnMachine::kCooldownHoldSeconds;

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// How many lines of the log have the words.
std::ptrdiff_t Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// A creature's command over, having run, and the world settled after it.
void Done(World &w, ObjectId id)
{
    w.Finish(id);
    w.RunFor(600);
}

// What the machine holds of an ability's cooldown: the milliseconds left on
// its user's clock, and what they count down from; nothing held, none.
std::uint32_t Left(const World &w, ObjectId id, std::int32_t ability, std::wstring_view item = {})
{
    return w.Held(id, ability, item).value_or(CooldownView{}).leftMs;
}
std::uint32_t Of(const World &w, ObjectId id, std::int32_t ability, std::wstring_view item = {})
{
    return w.Held(id, ability, item).value_or(CooldownView{}).ofMs;
}

// Held in the engine, where waiting cannot reach: its record running, which
// is what the quickbar draws, an AI reads and the engine's own check refuses
// a use by, as the machine last set it to hold it, with all but the hold's
// hour left. By the stand-in's own word for whose it is (World::MachineHolds).
bool HeldInTheEngine(const World &w, ObjectId id, std::int32_t ability, std::wstring_view item = {})
{
    const RunningCooldown *record = w.Cooling(id, ability, item);
    return w.MachineHolds(id, ability, item) && record != nullptr &&
           record->remaining > static_cast<float>(kHour) - 900.0f;
}

// Everyone else's turns pass until this one's begins again: the player's
// ended as they come, an AI's choosing nothing.
void UntilTurnOf(World &w, ObjectId id)
{
    for (int i = 0; i < 4000 && w.Current() == id; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    for (int i = 0; i < 4000 && w.Current() != id; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.Current() == id);
}

} // namespace

TEST_CASE("a cooldown the engine sets is held for its seconds on its user's own clock, and released when they have "
          "passed",
          "[cooldowns]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    w.RunFor(100);
    // Nothing is reckoned as the command is let through: the engine has set
    // none yet, and the machine holds none.
    CHECK(w.machine.State().cooldowns.empty());
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
    // The engine sets its 20 s as the command ends, and says so: taken there
    // and then, all of them.
    w.Finish(kHero);
    CHECK(Left(w, kHero, kShieldBash) == 20000);
    CHECK(Of(w, kHero, kShieldBash) == 20000);
    CHECK(Logged(w, "Hero's ability 617 is cooling in the engine, 20.00 s left of 20.00: taken onto its own clock, and "
                    "held (set by the engine now)"));
    // And held in the engine with the next frame's effects.
    w.Frame();
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    // Waiting does not bring it back: ten seconds of the wall with her turn
    // open, and it is where it was.
    w.RunFor(10000);
    CHECK(Left(w, kHero, kShieldBash) == 20000);
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    // The rest of this turn, 2 s after the talent's own 2, and four turns
    // more of 4 s: 18 s of her clock.
    for (int turn = 0; turn < 5; ++turn)
    {
        UntilTurnOf(w, kHero);
    }
    CHECK(Left(w, kHero, kShieldBash) == 2000);
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    UntilTurnOf(w, kHero); // and the turn that brings the rest
    CHECK_FALSE(w.Held(kHero, kShieldBash));
    CHECK(w.Cooling(kHero, kShieldBash) == nullptr);
    CHECK(Logged(w, "Hero's ability 617 is ready again: its cooldown has run on its own clock"));
    CHECK(Lines(w, "is cooling in the engine") == 1);
}

TEST_CASE("an ability whose command the engine sets no cooldown for is held by nothing", "[cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 0.0f) == Verdict::Allow);
    Done(w, kHero);
    CHECK(w.machine.State().cooldowns.empty());
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
}

TEST_CASE("the machine looks for no cooldown: it reads a creature's once, as it comes into the fight", "[cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.coolingReads == 4);
    w.RunFor(20000); // forty looks at everyone, the hero's turn open
    CHECK(w.coolingReads == 4);
    // One that joins is read as it joins.
    w.creatures.push_back(Make(0x202, Side::Enemy, false, L"Shriek", 12.0f));
    w.Cool(0x202, kOverpower, 30.0f); // not of the fight yet: the engine's own, on the wall
    CHECK_FALSE(w.Held(0x202, kOverpower));
    w.RunFor(3000);
    CHECK(w.machine.State().cooldowns.empty());
    CHECK_FALSE(w.MachineHolds(0x202, kOverpower));
    // The round's next turn scans, and finds it: read then, and taken at
    // what is left of it.
    CHECK(w.machine.OnEndTurn());
    w.RunFor(200);
    REQUIRE(w.machine.Order().Find(0x202) != nullptr);
    CHECK(w.coolingReads == 5);
    CHECK(Of(w, 0x202, kOverpower) == 30000);
    CHECK(Left(w, 0x202, kOverpower) < 27000);
    CHECK(Left(w, 0x202, kOverpower) > 26000);
    CHECK(HeldInTheEngine(w, 0x202, kOverpower));
    CHECK(Logged(w, "Shriek's ability 603 is cooling in the engine, "));
    CHECK(Logged(w, " s left of 30.00: taken onto its own clock, and held (found running on it)"));
}

TEST_CASE("a cooldown running as the fight begins is taken onto its creature's clock", "[cooldowns]")
{
    World w;
    w.Cool(kHero, kShieldBash, 8.0f);
    w.Cool(kGenlock, kOverpower, 30.0f);
    w.RunFor(1000); // a second of exploring: they run on the wall, as they do with no fight on
    REQUIRE(w.Cooling(kHero, kShieldBash) != nullptr);
    CHECK(w.Cooling(kHero, kShieldBash)->remaining < 7.1f);
    CHECK(w.machine.State().cooldowns.empty());
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(Of(w, kHero, kShieldBash) == 8000);
    CHECK(Left(w, kHero, kShieldBash) > 6900);
    CHECK(Left(w, kHero, kShieldBash) < 7000);
    CHECK(Of(w, kGenlock, kOverpower) == 30000);
    CHECK(Left(w, kGenlock, kOverpower) > 28900);
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    CHECK(HeldInTheEngine(w, kGenlock, kOverpower));
    CHECK(Logged(w, "(found running on it)"));
    // And from there on its creature's clock alone: the hero's turn, ended,
    // moves the hero's four seconds and the genlock's none.
    const std::uint32_t heros = Left(w, kHero, kShieldBash);
    const std::uint32_t genlocks = Left(w, kGenlock, kOverpower);
    CHECK(w.machine.OnEndTurn());
    w.RunFor(200);
    REQUIRE(w.Current() == kGenlock);
    CHECK(Left(w, kHero, kShieldBash) == heros - 4000);
    CHECK(Left(w, kGenlock, kOverpower) == genlocks);
}

TEST_CASE("one the engine sets as the fight begins, before its creature's cooldowns are read, is taken once",
          "[cooldowns]")
{
    World w;
    w.mode = Mode::Combat;
    w.Frame();
    w.Frame(); // the fight's order is made; nobody's cooldowns are read yet
    REQUIRE(w.machine.Order().Find(kHero) != nullptr);
    REQUIRE(w.coolingReads == 0);
    w.Cool(kHero, kShieldBash, 12.0f);
    CHECK(Left(w, kHero, kShieldBash) == 12000);
    w.RunFor(600);
    CHECK(w.coolingReads == 4);
    CHECK(Left(w, kHero, kShieldBash) == 12000);
    CHECK(Lines(w, "is cooling in the engine") == 1);
}

TEST_CASE("a cooldown is taken as it is set, whoever's turn it is and whatever its creature is at", "[cooldowns]")
{
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    SECTION("on one whose turn it is not, by a script with no command, and it moves by its own turns alone")
    {
        // A trap's, a plot's: with the hero's turn open.
        w.Cool(kCompanion, kOverpower, 6.0f);
        CHECK(Left(w, kCompanion, kOverpower) == 6000);
        w.Frame();
        CHECK(HeldInTheEngine(w, kCompanion, kOverpower));
        w.RunFor(5000); // five seconds of the wall take nothing of it
        CHECK(Left(w, kCompanion, kOverpower) == 6000);
        UntilTurnOf(w, kCompanion); // nor do others' turns
        CHECK(Left(w, kCompanion, kOverpower) == 6000);
        UntilTurnOf(w, kHero); // its own does, a turn's seconds
        CHECK(Left(w, kCompanion, kOverpower) == 2000);
    }
    SECTION("in the middle of its creature's own command, and again as the engine sets it again")
    {
        // The scripts set a mode's cooldown twice in one command
        // (ability_core.nss and effect_upkeep_h.nss, read 2026-10-06): a
        // second and a half as an AI's cast starts, and the ability table's
        // own as the mode's upkeep effect comes off.
        w.Get(kHero).modes.insert(kSpellShield);
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        w.Cool(kHero, kSpellShield, 1.5f);
        CHECK(Of(w, kHero, kSpellShield) == 1500);
        w.RunFor(1000); // its command still running, a second of the wall
        CHECK(Left(w, kHero, kSpellShield) == 1500);
        CHECK(HeldInTheEngine(w, kHero, kSpellShield));
        w.cooldownOf[kSpellShield] = 30.0f;
        w.Finish(kHero);
        CHECK(Of(w, kHero, kSpellShield) == 30000);
        CHECK(Left(w, kHero, kSpellShield) == 30000);
        CHECK(Logged(w, "30.00 s left of 30.00, set anew by the game over the 1.50 s the machine held"));
        w.RunFor(600);
        CHECK(HeldInTheEngine(w, kHero, kSpellShield));
        CHECK(w.machine.State().cooldowns.size() == 1);
    }
    SECTION("on another while a command runs")
    {
        CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
        w.Cool(kGenlock, kOverpower, 10.0f);
        CHECK(Left(w, kGenlock, kOverpower) == 10000);
        w.Frame();
        CHECK(HeldInTheEngine(w, kGenlock, kOverpower));
    }
    SECTION("under a menu")
    {
        w.mode = Mode::Gui;
        w.RunFor(200);
        w.Cool(kHero, kShieldBash, 9.0f);
        CHECK(Left(w, kHero, kShieldBash) == 9000);
        w.Frame();
        CHECK(HeldInTheEngine(w, kHero, kShieldBash));
        w.RunFor(5000);
        w.mode = Mode::Combat;
        w.RunFor(200);
        CHECK(Left(w, kHero, kShieldBash) == 9000);
    }
    SECTION("in a conversation in the middle of the fight")
    {
        w.mode = Mode::Dialog;
        w.RunFor(200);
        w.Cool(kGenlock, kOverpower, 9.0f);
        CHECK(Left(w, kGenlock, kOverpower) == 9000);
        w.Frame();
        CHECK(HeldInTheEngine(w, kGenlock, kOverpower));
        w.RunFor(5000);
        w.mode = Mode::Combat;
        w.RunFor(200);
        CHECK(Left(w, kGenlock, kOverpower) == 9000);
    }
}

TEST_CASE("one that leaves the fight and comes back is read again, and what the engine set on it meanwhile is the "
          "engine's word",
          "[cooldowns]")
{
    World w;
    w.StartFight();
    w.Cool(kHurlock, kOverpower, 30.0f);
    REQUIRE(Left(w, kHurlock, kOverpower) == 30000);
    // It loses the party, and the party it: out of the fight four seconds
    // on, and out of its order as the round turns.
    w.Get(kHurlock).fighting = false;
    w.Get(kHurlock).perception = Unperceived();
    for (int round = 0; round < 8 && w.machine.Order().Find(kHurlock) != nullptr; ++round)
    {
        UntilTurnOf(w, kHero);
    }
    REQUIRE(w.machine.Order().Find(kHurlock) == nullptr);
    // Out of the fight the engine sets it again, twelve seconds: the engine's
    // own, on the wall, and no hold of the machine's any more.
    w.Cool(kHurlock, kOverpower, 12.0f);
    CHECK_FALSE(w.Held(kHurlock, kOverpower));
    w.RunFor(1000);
    CHECK_FALSE(w.MachineHolds(kHurlock, kOverpower));
    // Back in the fight, it is read as it comes in, and taken at what is
    // left of the engine's twelve.
    w.Get(kHurlock).fighting = true;
    w.Get(kHurlock).perception = Perception{};
    const int reads = w.coolingReads;
    for (int round = 0; round < 4 && w.machine.Order().Find(kHurlock) == nullptr; ++round)
    {
        UntilTurnOf(w, kHero);
    }
    REQUIRE(w.machine.Order().Find(kHurlock) != nullptr);
    w.RunFor(100);
    CHECK(w.coolingReads == reads + 1);
    CHECK(Of(w, kHurlock, kOverpower) == 12000);
    CHECK(Left(w, kHurlock, kOverpower) < 11000);
    CHECK(HeldInTheEngine(w, kHurlock, kOverpower));
}

TEST_CASE("the next fight reads what is cooling as it begins, as the first did", "[cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.coolingReads == 4);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    (void)w.machine.OnEndTurn();
    w.RunFor(500);
    REQUIRE_FALSE(w.machine.Active());
    w.mode = Mode::Explore;
    w.Frame();
    w.Cool(kHero, kShieldBash, 30.0f); // between two fights: nobody is told
    w.Get(kGenlock).down = false;
    w.Get(kHurlock).down = false;
    w.StartFight();
    REQUIRE(w.machine.Active());
    CHECK(w.coolingReads == 8);
    CHECK(Of(w, kHero, kShieldBash) == 30000);
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
}

TEST_CASE("one set on a creature of no fight is the engine's, on the wall as ever", "[cooldowns]")
{
    World w;
    w.Cool(kHero, kShieldBash, 2.0f);
    CHECK(w.machine.State().cooldowns.empty());
    w.RunFor(1000);
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
    REQUIRE(w.Cooling(kHero, kShieldBash) != nullptr);
    CHECK(w.Cooling(kHero, kShieldBash)->remaining < 1.1f);
    w.RunFor(1500);
    CHECK(w.Cooling(kHero, kShieldBash) == nullptr);
}

TEST_CASE("an AI whose sustained spells will not stand orders each once, until its creature's own clock has run "
          "their cooldown",
          "[cooldowns]")
{
    // The Cultist Overseer of the Wyrmling Lair (issue 57), as real time
    // showed it with the mod off (2026-10-08): six orders of three sustained
    // spells in ten seconds, none standing. Each failed order costs no mana
    // and leaves only the scripts' second and a half on that spell, set as
    // the cast starts (never the ability table's cooldown: the upkeep's
    // effect was never there to come off), and takes about 1.7 s of the
    // game's time; and its script chooses again as each command ends.
    //
    // In turns a mode's switch takes nothing of the bar. Predicted, a mode
    // turned on was held for nothing, and one spell was ordered 47 times in
    // a turn. Read after its command's end, its cooldown was over before the
    // command was, and the turn, at one command after another, was never
    // looked at: 83 orders in a turn.
    constexpr std::array<std::int32_t, 3> kSpells{10400, 17022, 10401};
    Settings quick = Fixed();
    quick.maxTurnMs = 30000;
    World w(quick);
    w.wontStand.insert(kSpells.begin(), kSpells.end());
    std::map<std::int32_t, std::vector<std::uint32_t>> orderedAt; // each spell's orders, by its creature's bar spent
    bool swings = false;
    const auto spent = [&] { return w.machine.View().timeMs - w.machine.View().timeLeftMs; };
    // Its tactics: the first of its spells that is not cooling; then, in one
    // of the two fights, a blow of a second while it has the time.
    const auto choose = [&] {
        if (w.Current() != kGenlock)
        {
            return;
        }
        for (const std::int32_t spell : kSpells)
        {
            if (w.Cooling(kGenlock, spell) == nullptr)
            {
                const std::uint32_t at = spent();
                if (w.Switch(kGenlock, Source::Script, spell) == Verdict::Allow)
                {
                    orderedAt[spell].push_back(at);
                }
                return;
            }
        }
        if (swings && w.machine.View().timeLeftMs >= 1000)
        {
            CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script, 0.0f, 1.0f) == Verdict::Allow);
        }
    };
    w.onPoke = [&](ObjectId) { choose(); };
    // The engine's side of its turn: each command runs its time and ends,
    // and its script chooses again there and then (COMMAND_COMPLETE).
    const auto play = [&] {
        int busy = 0;
        int frames = 0;
        for (; frames < 4000 && w.Current() == kGenlock; ++frames)
        {
            w.Frame();
            const Creature &c = w.Get(kGenlock);
            busy = c.busy ? busy + 1 : 0;
            const int length = c.current == CommandType::Attack ? 62 : 106; // a second; and 1.7 s
            if (busy >= length)
            {
                busy = 0;
                w.Finish(kGenlock);
                choose();
            }
        }
        return frames * 16;
    };
    w.StartFight();
    CHECK(w.machine.OnEndTurn()); // the hero passes
    w.RunFor(100);
    REQUIRE(w.Current() == kGenlock);
    SECTION("with nothing else to do, its turn is over when it has ordered each once")
    {
        const int ran = play();
        REQUIRE(w.Current() == kCompanion);
        for (const std::int32_t spell : kSpells)
        {
            CAPTURE(spell);
            CHECK(orderedAt[spell].size() == 1);
            CHECK_FALSE(w.Get(kGenlock).modes.contains(spell));
        }
        // Three commands of 1.7 s, and the time it then had to choose in.
        CHECK(ran > 5000);
        CHECK(ran < 9000);
        CHECK(Lines(w, "is cooling in the engine, 1.50 s left of 1.50: taken onto its own clock, and held (set by the "
                       "engine now)") == 3);
        CHECK(Logged(w, "Genlock's turn ends: chose nothing"));
        CHECK_FALSE(Logged(w, "took too long"));
    }
    SECTION("with blows to strike between, it orders one again only as its bar has run the second and a half")
    {
        swings = true;
        (void)play();
        REQUIRE(w.Current() == kCompanion);
        for (const std::int32_t spell : kSpells)
        {
            CAPTURE(spell);
            const std::vector<std::uint32_t> &at = orderedAt[spell];
            // Its four seconds have room for it twice at the least, a blow
            // of a second each time between, and never for it four times.
            CHECK(at.size() >= 2);
            CHECK(at.size() <= 3);
            for (std::size_t i = 1; i < at.size(); ++i)
            {
                CHECK(at[i] - at[i - 1] >= 1500);
            }
        }
        CHECK_FALSE(Logged(w, "took too long"));
    }
}

TEST_CASE("a mode switched off cools for what the engine set and no longer", "[cooldowns]")
{
    // The user, 2026-10-06, of the mod's own rule that a mode switched off
    // waited for its user's next turn: "It should obey the cooldown logic.
    // Not just blindly wait for next turn."
    World w;
    w.Get(kHero).modes.insert(kSpellShield);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    SECTION("a short one is back in the same turn once that much of the bar is spent, and not by waiting")
    {
        w.cooldownOf[kSpellShield] = 0.5f;
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        Done(w, kHero);
        CHECK(w.machine.View().timeLeftMs == 4000); // free, and the turn hers
        CHECK(Of(w, kHero, kSpellShield) == 500);
        CHECK(Left(w, kHero, kSpellShield) == 500);
        // Five seconds of the wall, ten times its cooldown, her turn open:
        // not a millisecond of it has passed.
        w.RunFor(5000);
        CHECK(Left(w, kHero, kSpellShield) == 500);
        CHECK(HeldInTheEngine(w, kHero, kSpellShield));
        // A swing of a second and a half moves her clock past it.
        CHECK(w.Order(kHero, CommandType::Attack, Source::Player, 0.0f, 1.5f) == Verdict::Allow);
        Done(w, kHero);
        CHECK_FALSE(w.Held(kHero, kSpellShield));
        CHECK(w.Cooling(kHero, kSpellShield) == nullptr);
        // And it is switched on again, in the turn it was switched off in.
        REQUIRE(w.Current() == kHero);
        CHECK(w.machine.Order().Round() == 1);
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        CHECK(w.Get(kHero).modes.contains(kSpellShield));
    }
    SECTION("a long one is out for that many seconds of its user's clock, several turns")
    {
        w.cooldownOf[kSpellShield] = 30.0f;
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        Done(w, kHero);
        CHECK(Left(w, kHero, kSpellShield) == 30000);
        for (int turn = 0; turn < 7; ++turn)
        {
            UntilTurnOf(w, kHero);
        }
        CHECK(Left(w, kHero, kSpellShield) == 2000);
        CHECK(HeldInTheEngine(w, kHero, kSpellShield));
        UntilTurnOf(w, kHero);
        CHECK_FALSE(w.Held(kHero, kSpellShield));
    }
    SECTION("one the engine sets no cooldown for is held by nothing")
    {
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        Done(w, kHero);
        CHECK(w.machine.State().cooldowns.empty());
        CHECK_FALSE(w.Did(EffectKind::Cooldown));
        CHECK(w.Switch(kHero, Source::Player, kSpellShield) == Verdict::Allow);
        CHECK(w.Get(kHero).modes.contains(kSpellShield));
    }
}

TEST_CASE("an item's ability's cooldown is held, moved and released by its item's tag", "[cooldowns]")
{
    World w;
    w.itemOf[kPoultice] = kPoulticeTag;
    // Whether the hold's total takes on an item's record or not, as it did
    // not in play on DAOLib 737cd89 (2026-10-06: a poultice held at 2,896 s
    // of 3,600 read back `2895.73 s left of 5.00`, and the machine, which
    // then knew its holds by reading them back, took its own for the game's
    // at every look, 12,460 times in twenty minutes). It reads none back now.
    w.itemTotalsStay = GENERATE(false, true);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.UseAbility(kHero, Source::Player, kPoultice, 5.0f) == Verdict::Allow);
    Done(w, kHero);
    // The engine keeps an item's ability's record by the item's tag, apart
    // from the same ability of the creature's own.
    CHECK(Of(w, kHero, kPoultice, kPoulticeTag) == 5000);
    CHECK(Left(w, kHero, kPoultice, kPoulticeTag) == 5000);
    CHECK_FALSE(w.Held(kHero, kPoultice));
    CHECK(HeldInTheEngine(w, kHero, kPoultice, kPoulticeTag));
    CHECK(w.Cooling(kHero, kPoultice) == nullptr);
    CHECK(Logged(w, "Hero's ability 200010 (item gen_im_qck_health_101) is cooling in the engine, "));
    // The creature's own of that number is another record, and another hold.
    w.Cool(kHero, kPoultice, 9.0f);
    w.RunFor(600);
    CHECK(Of(w, kHero, kPoultice) == 9000);
    CHECK(Of(w, kHero, kPoultice, kPoulticeTag) == 5000);
    // Twenty seconds with her turn open, forty looks at everyone: taken
    // once, and held as it was.
    w.RunFor(20000);
    CHECK(Left(w, kHero, kPoultice, kPoulticeTag) == 5000);
    CHECK(HeldInTheEngine(w, kHero, kPoultice, kPoulticeTag));
    CHECK(Lines(w, "(item gen_im_qck_health_101) is cooling in the engine") == 1);
    SECTION("released by its tag as its seconds have passed, and drunk again")
    {
        w.applied.clear();
        UntilTurnOf(w, kHero); // the rest of this turn, 2 s
        UntilTurnOf(w, kHero); // and four more
        CHECK_FALSE(w.Held(kHero, kPoultice, kPoulticeTag));
        CHECK(w.Cooling(kHero, kPoultice, kPoulticeTag) == nullptr);
        CHECK(std::ranges::any_of(w.applied, [](const Effect &e) {
            return e.kind == EffectKind::Cooldown && e.target == kHero && e.a == kPoultice && e.text == kPoulticeTag &&
                   e.b == 0;
        }));
        CHECK(HeldInTheEngine(w, kHero, kPoultice)); // its own, of 9 s, still held
        w.RunFor(200);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.UseAbility(kHero, Source::Player, kPoultice, 5.0f) == Verdict::Allow);
        Done(w, kHero);
        CHECK(Left(w, kHero, kPoultice, kPoulticeTag) == 5000);
    }
    SECTION("given back its own seconds by its tag as the fight ends")
    {
        w.Get(kGenlock).down = true;
        w.Get(kHurlock).down = true;
        (void)w.machine.OnEndTurn();
        w.RunFor(500);
        REQUIRE_FALSE(w.machine.Active());
        // The rest of the turn it was used in has passed, 2 s of its 5.
        const RunningCooldown *items = w.Cooling(kHero, kPoultice, kPoulticeTag);
        REQUIRE(items != nullptr);
        CHECK(items->remaining <= 3.0f);
        CHECK(items->remaining > 2.4f);
        CHECK_FALSE(w.MachineHolds(kHero, kPoultice, kPoulticeTag));
        const RunningCooldown *own = w.Cooling(kHero, kPoultice);
        REQUIRE(own != nullptr);
        CHECK(own->total == 9.0f);
    }
}

TEST_CASE("a held cooldown the engine sets again is taken again, at the engine's word", "[cooldowns]")
{
    World w;
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(Of(w, kHero, kShieldBash) == 20000);
    // A script sets it over the machine's hold: seven seconds, left and in
    // all, as the scripts' SetCooldown does.
    w.Cool(kHero, kShieldBash, 7.0f);
    CHECK(Of(w, kHero, kShieldBash) == 7000);
    CHECK(Left(w, kHero, kShieldBash) == 7000);
    CHECK(w.machine.State().cooldowns.size() == 1);
    CHECK(Logged(w, "7.00 s left of 7.00, set anew by the game over the 20.00 s the machine held"));
    w.Frame();
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
}

TEST_CASE("a held cooldown the engine sets to nothing is let go", "[cooldowns]")
{
    World w;
    std::vector<TurnEvent> events;
    w.machine.SetEventSink([&](const TurnEvent &e) { events.push_back(e); });
    w.StartFight();
    CHECK(w.UseAbility(kHero, Source::Player, kShieldBash, 20.0f) == Verdict::Allow);
    Done(w, kHero);
    REQUIRE(w.Held(kHero, kShieldBash));
    // A script's reset: SetCooldown at nothing, as the game's plot scripts
    // have it for a boss.
    w.applied.clear();
    w.Cool(kHero, kShieldBash, 0.0f);
    CHECK_FALSE(w.Held(kHero, kShieldBash));
    w.RunFor(600);
    CHECK(w.Cooling(kHero, kShieldBash) == nullptr);
    CHECK_FALSE(w.Did(EffectKind::Cooldown)); // nothing is left in the engine to set
    CHECK(Logged(w, "Hero's ability 617 was held with 20.00 s of its own clock to wait, and the game has ended its "
                    "cooldown: let go, it is ready"));
    CHECK(std::ranges::any_of(events, [](const TurnEvent &e) {
        return e.kind == EventKind::CooldownReleased && e.who == kHero && e.value == kShieldBash &&
               e.detail == "ended by the game";
    }));
    // And one that was not held, set to nothing: nothing.
    w.Cool(kHero, kOverpower, 0.0f);
    w.RunFor(100);
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
}

TEST_CASE("a hold the game ends as its sweep was to be moved is let go, and stays let go", "[cooldowns]")
{
    // An AI's wait takes its length of the bar at the gate, and the clock
    // moves there: a held cooldown's sweep is set again with an effect the
    // next step hands out. In the same moment a script ends the cooldown: the
    // machine lets go of it, and what it had still to set is not set, which
    // would hold in the engine what the book no longer has.
    World w;
    w.StartFight();
    w.Cool(kGenlock, kOverpower, 30.0f);
    w.RunFor(600);
    REQUIRE(w.Held(kGenlock, kOverpower));
    int waited = 0;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && waited++ == 0)
        {
            CHECK(w.Wait(kGenlock, 2.0f) == Verdict::Block); // paid for, of its bar
            CHECK(Left(w, kGenlock, kOverpower) == 28000);
            w.Cool(kGenlock, kOverpower, 0.0f);
        }
    };
    CHECK(w.machine.OnEndTurn());
    for (int i = 0; i < 200 && waited == 0; ++i)
    {
        w.Frame();
    }
    REQUIRE(waited == 1);
    w.RunFor(1000);
    CHECK_FALSE(w.Held(kGenlock, kOverpower));
    CHECK(w.Cooling(kGenlock, kOverpower) == nullptr);
    CHECK(Logged(w, "and the game has ended its cooldown"));
}

TEST_CASE("a hold is an hour in the engine however little is left, and its sweep shows what is left", "[cooldowns]")
{
    // What is left over the total is what the quickbar's sweep is drawn by,
    // and what is left is what the engine counts down on the wall. Until
    // 2026-10-08 the hold's hour was the total and what was left its share:
    // a third of a second of fifty minutes was none of it, and gone at once.
    World w;
    w.SetCooldown(kHero, kShieldBash, 0.3f, 3000.0f);
    w.StartFight();
    REQUIRE(w.Held(kHero, kShieldBash));
    CHECK(Left(w, kHero, kShieldBash) < 300);
    CHECK(Of(w, kHero, kShieldBash) == 3000000);
    const RunningCooldown *record = w.Cooling(kHero, kShieldBash);
    REQUIRE(record != nullptr);
    CHECK(record->remaining > static_cast<float>(kHour) - 1.0f);
    CHECK(record->remaining / record->total < 0.0002f); // all but none of it left to wait
    // Half a minute with her turn open, and it is held as it was: nothing
    // looks at it, and nothing need.
    w.applied.clear();
    w.RunFor(30000);
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
    CHECK(w.Held(kHero, kShieldBash));
}

TEST_CASE("a fight dropped by a load gives each cooldown it held its own seconds back as the world is there again",
          "[cooldowns][load]")
{
    // The Blackmarsh, 2026-10-09 (issue 90): the Baroness's scene took the
    // party out of the Fade by a load, from a conversation in the middle of
    // the fight with the First. The party's creatures go on as they are, ids
    // and cooldown records and all (Lyna 0x43ad4 before and after, her
    // record `3459 s left of 30673` two minutes on). The machine forgot its
    // book at the drop, and eleven abilities stood at the hold's hour through
    // two minutes of exploring, until the next fight found them and ended
    // them, a warning each.
    World w;
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Cool(kHero, kShieldBash, 20.0f);
    w.Cool(kCompanion, kOverpower, 30.0f);
    CHECK(w.machine.OnEndTurn()); // a turn of the hero's: four seconds of her clock, none of the companion's
    w.RunFor(200);
    REQUIRE(Left(w, kHero, kShieldBash) == 16000);
    REQUIRE(HeldInTheEngine(w, kHero, kShieldBash));
    REQUIRE(HeldInTheEngine(w, kCompanion, kOverpower));
    // From a turn, or from a conversation in the middle of the fight.
    const bool fromAScene = GENERATE(false, true);
    if (fromAScene)
    {
        w.mode = Mode::Dialog;
        w.RunFor(500);
    }
    w.applied.clear();
    w.mode = Mode::Loading;
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
    // Nothing is written while the game loads, the area's objects going: the
    // two are in the engine as they were held.
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
    CHECK(HeldInTheEngine(w, kHero, kShieldBash));
    CHECK(HeldInTheEngine(w, kCompanion, kOverpower));
    SECTION("with the first frame of the world, whatever its mode; then on the wall, and found by the next fight")
    {
        w.mode = fromAScene ? Mode::Cutscene : Mode::Explore;
        w.Frame();
        const RunningCooldown *heros = w.Cooling(kHero, kShieldBash);
        REQUIRE(heros != nullptr);
        CHECK(heros->remaining == 16.0f);
        CHECK(heros->total == 20.0f);
        const RunningCooldown *companions = w.Cooling(kCompanion, kOverpower);
        REQUIRE(companions != nullptr);
        CHECK(companions->remaining == 30.0f);
        CHECK(companions->total == 30.0f);
        CHECK_FALSE(w.MachineHolds(kHero, kShieldBash));
        CHECK_FALSE(w.MachineHolds(kCompanion, kOverpower));
        CHECK(w.machine.State().cooldowns.empty());
        CHECK(Logged(w, "the world is there again after the fight that was dropped: the 2 cooldowns it held have their "
                        "own seconds back (0x100's ability 617, 16.00 s left of 20.00; 0x101's ability 603, 30.00 s "
                        "left of 30.00)"));
        // Written once; and from there each runs on the wall, as any cooldown
        // with no fight on.
        w.applied.clear();
        w.mode = Mode::Explore;
        w.RunFor(2000);
        CHECK_FALSE(w.Did(EffectKind::Cooldown));
        REQUIRE(w.Cooling(kHero, kShieldBash) != nullptr);
        CHECK(w.Cooling(kHero, kShieldBash)->remaining < 14.1f);
        CHECK(Lines(w, "the world is there again") == 1);
        // The next fight finds what is left of each running, as it finds any.
        w.StartFight();
        REQUIRE(w.machine.Active());
        CHECK(Of(w, kHero, kShieldBash) == 20000);
        CHECK(Left(w, kHero, kShieldBash) < 14100);
        CHECK(Left(w, kHero, kShieldBash) > 13800);
        CHECK(Of(w, kCompanion, kOverpower) == 30000);
        CHECK(Lines(w, "(found running on it)") == 2);
    }
    SECTION("but not one the engine has set while the game loaded: that is the engine's")
    {
        // A scene's script, with no command: seven seconds, over the hold.
        w.Cool(kHero, kShieldBash, 7.0f);
        w.RunFor(1000);
        w.mode = Mode::Explore;
        w.Frame();
        const RunningCooldown *heros = w.Cooling(kHero, kShieldBash);
        REQUIRE(heros != nullptr);
        CHECK(heros->total == 7.0f);
        CHECK(heros->remaining < 6.0f);
        const RunningCooldown *companions = w.Cooling(kCompanion, kOverpower);
        REQUIRE(companions != nullptr);
        CHECK(companions->remaining == 30.0f);
        CHECK(Logged(w, "the 1 cooldown it held has its own seconds back (0x101's ability 603, 30.00 s left of "
                        "30.00)"));
    }
}

TEST_CASE("a game loaded over a dropped fight is another world: nothing of the fight's is written into it",
          "[cooldowns][load]")
{
    // A save loaded in the middle of a fight (the dragons', 2026-10-09,
    // 18:22), or a new game begun from the menu a fight was left for. Its
    // creatures are as the save has them, under the ids the dropped fight's
    // had as often as not, and a save keeps no cooldown (read of one written
    // while eleven records stood at the hold's totals, 2026-10-09: those
    // totals are not in its file). The game layer says so as DAOLib does
    // (dao::saves::OnGameLoaded), which comes while the mode still says
    // loading.
    World w;
    w.StartFight();
    w.Cool(kHero, kShieldBash, 20.0f);
    w.RunFor(600);
    REQUIRE(HeldInTheEngine(w, kHero, kShieldBash));
    w.mode = GENERATE(Mode::Loading, Mode::Pregame); // a save chosen in the game's menu, or from the main menu
    w.RunFor(1000);
    REQUIRE_FALSE(w.machine.Active());
    w.mode = Mode::Loading;
    w.Frame();
    w.Get(kHero).cooldowns.clear(); // the save's hero: the same id, and nothing cooling
    w.machine.OnGameLoaded();
    w.applied.clear();
    w.mode = Mode::Explore;
    w.RunFor(1000);
    CHECK_FALSE(w.Did(EffectKind::Cooldown));
    CHECK(w.Cooling(kHero, kShieldBash) == nullptr);
    CHECK(w.machine.State().cooldowns.empty());
    CHECK(Logged(w, "a game is loaded: the 1 cooldown the dropped fight held was another world's, and is let go"));
    CHECK_FALSE(Logged(w, "the world is there again"));
    // And said only where there was something to let go.
    w.machine.OnGameLoaded();
    CHECK(Lines(w, "a game is loaded") == 1);
}
