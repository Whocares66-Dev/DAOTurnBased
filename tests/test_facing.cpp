// A creature the engine would not turn to face what its attack is aimed at
// is faced at it exactly before the command is given (issue 40; core/Facing.h
// has the engine's reading). Which commands the rule is for; then the
// machine's say for each command the gate lets through, in the stand-in for
// the game (World.h), whose Queue asks its engine for the turn, has it face
// the creature, asks again and tells the machine, as the gate does; the
// story the rule is for, a kept attack given back to a creature standing a
// little over a degree off its unmoved target with nothing to turn it; what
// play showed of the rule's first form, a blow at a big creature, whose aim
// is not where it stands; and what the machine says of a facing that did
// not come off.
#include "World.h"

#include "core/Facing.h"
#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

using namespace tb;
using namespace tb::test;
using Catch::Approx;

namespace
{

bool Logged(const World &w, std::string_view text)
{
    return std::ranges::any_of(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

auto Lines(const World &w, std::string_view text)
{
    return std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; });
}

// The standard fight with everyone given a place: the party to the west, the
// two enemies three meters east of them, each side two meters apart.
World Placed()
{
    World w;
    w.Get(kHero).position = Vec3{0.0f, 0.0f, 0.0f};
    w.Get(kCompanion).position = Vec3{0.0f, 2.0f, 0.0f};
    w.Get(kGenlock).position = Vec3{3.0f, 0.0f, 0.0f};
    w.Get(kHurlock).position = Vec3{3.0f, 2.0f, 0.0f};
    return w;
}

// The genlock made a big creature: whoever strikes at it is aimed by the
// engine at a point of its ring a fifth of a meter from where it stands,
// which for the hero three meters off is 3.95 degrees from its place: the
// Great Bear's numbers in play (2026-10-05).
void MakeBig(World &w)
{
    w.Get(kGenlock).aimY = 0.2071f;
}

// How far the hero's facing is from the way to where the genlock stands.
float OffItsPlace(const World &w)
{
    const Creature &hero = w.Get(kHero);
    const Creature &genlock = w.Get(kGenlock);
    const float x = genlock.position.x - hero.position.x;
    const float y = genlock.position.y - hero.position.y;
    return std::abs(std::atan2(hero.facingX * y - hero.facingY * x, hero.facingX * x + hero.facingY * y)) *
           (180.0f / std::numbers::pi_v<float>);
}

// The fight ended, its enemies fallen.
void EndFight(World &w)
{
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.inCombat = false;
    w.mode = Mode::Explore;
    w.Finish(kHero);
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
}

// The hero's attack on the genlock, as the gate's request has it, with the
// turn the engine answered for it.
CommandRequest HerAttack(const TurnToFace &turn)
{
    CommandRequest attack;
    attack.object = kHero;
    attack.type = CommandType::Attack;
    attack.source = Source::Player;
    attack.inCombat = true;
    attack.target = kGenlock;
    attack.turn = turn;
    return attack;
}

constexpr const char *kFaced = "is given: faced at it exactly";
constexpr const char *kNotFaced = "was to be faced at it exactly";
constexpr const char *kCounted = "which was faced at it exactly first (issue 40)";

} // namespace

TEST_CASE("an attack and an ability's use turn their creature to face a target; nothing else does", "[facing]")
{
    CHECK(TurnsToFace(CommandType::Attack));
    CHECK(TurnsToFace(CommandType::UseAbility));
    for (int type = 0; type <= 40; ++type)
    {
        const auto command = static_cast<CommandType>(type);
        if (command != CommandType::Attack && command != CommandType::UseAbility)
        {
            INFO("command type " << type);
            CHECK_FALSE(TurnsToFace(command));
        }
    }
}

TEST_CASE("the player's attack at one the engine would not turn her to: faced at it first, the engine asked again, "
          "said, and counted for the fight's end",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Stall(kHero, kGenlock, 1.5f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    REQUIRE(w.facedFirst.size() == 1);
    CHECK(w.facedFirst.front() == std::pair{kHero, kGenlock});
    // What the command finds as it begins: its creature facing its target,
    // inside the engine's degree, so that it asks for no turn.
    CHECK(w.foundOff.at(kHero) < 0.01f);
    CHECK(Logged(w, "Hero stands 1.50 deg off Genlock as its attack is given: faced at it exactly (the engine would "
                    "begin no turn for it, and its attack would wait for one: issue 40); the engine now has it 0.00 "
                    "deg off: done"));
    CHECK_FALSE(Logged(w, kNotFaced));
    // Said once as the fight ends.
    CHECK(Lines(w, kCounted) == 0);
    EndFight(w);
    CHECK(Lines(w, "in this fight 1 attack or ability was given to a creature the engine would not have turned to "
                   "its target, which was faced at it exactly first (issue 40)") == 1);
}

TEST_CASE("at whatever angle, and either way round: the dead case is the engine's to name, not an angle's", "[facing]")
{
    // The first hang's edge; the angle the first form's own hang stood at;
    // one past the five degrees the rule once stopped at, where a creature
    // faced twice as it stood could have been left; and a long way round.
    for (const float off : {1.02f, -1.02f, 3.95f, -6.5f, 40.0f, -170.0f})
    {
        World w = Placed();
        w.StartFight();
        w.Stall(kHero, kGenlock, off);
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
        INFO(off << " degrees off");
        CHECK(w.facedFirst.size() == 1);
        CHECK(w.foundOff.at(kHero) < 0.01f);
        CHECK(Lines(w, kFaced) == 1);
    }
}

TEST_CASE("one the engine would turn is left to its turn, however little it is off: not faced, nothing said",
          "[facing]")
{
    for (const float off : {1.02f, -1.5f, 3.95f, 30.0f, 179.0f})
    {
        World w = Placed();
        w.StartFight();
        // Off its target, and its movement controller with a turn to make.
        w.FaceOff(kHero, kGenlock, off);
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
        INFO(off << " degrees off");
        CHECK(w.facedFirst.empty());
        // As it stood: the engine's turn takes it from there.
        CHECK(w.foundOff.at(kHero) == Approx(std::abs(off)).margin(1e-2));
        CHECK_FALSE(Logged(w, kFaced));
        CHECK_FALSE(Logged(w, kNotFaced));
    }
}

TEST_CASE("one the engine has for facing its target is given its attack as it stands, and is not counted", "[facing]")
{
    for (const float off : {0.0f, 0.5f, -0.9f})
    {
        World w = Placed();
        w.StartFight();
        // Its controller as dead as can be: with the creature facing, the
        // engine asks no turn of anyone.
        w.Stall(kHero, kGenlock, off);
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
        INFO(off << " degrees off");
        CHECK(w.facedFirst.empty());
        // Not set straight either.
        CHECK(w.foundOff.at(kHero) == Approx(std::abs(off)).margin(1e-3));
        CHECK_FALSE(Logged(w, kFaced));
        EndFight(w);
        // A fight in which nobody was faced says nothing of it.
        CHECK(Lines(w, kCounted) == 0);
    }
}

TEST_CASE("the rule is on the engine's word that the turn would stall, and on nothing else of its answer", "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.FaceFirst(HerAttack(TurnToFace{1.02f, false, true}), Verdict::Allow));
    CHECK(w.machine.FaceFirst(HerAttack(TurnToFace{-90.0f, false, true}), Verdict::Allow));
    // Not done, and the engine would turn it: its own turn.
    CHECK_FALSE(w.machine.FaceFirst(HerAttack(TurnToFace{1.02f, false, false}), Verdict::Allow));
    CHECK_FALSE(w.machine.FaceFirst(HerAttack(TurnToFace{3.95f, false, false}), Verdict::Allow));
    // Done: nothing to do.
    CHECK_FALSE(w.machine.FaceFirst(HerAttack(TurnToFace{0.5f, true, false}), Verdict::Allow));
    // Asking is not facing: nothing is said or counted until the game layer
    // has had the engine face it and asked again.
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("an ability aimed at another is as an attack: faced first; one aimed at nobody, or at its user, is not",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Stall(kHero, kGenlock, 2.0f);
    CommandRequest cast;
    cast.object = kHero;
    cast.type = CommandType::UseAbility;
    cast.source = Source::Player;
    cast.inCombat = true;
    cast.ability = 11;
    cast.cooldownSeconds = 10.0f;
    SECTION("at another")
    {
        cast.target = kGenlock;
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        REQUIRE(w.facedFirst.size() == 1);
        CHECK(w.foundOff.at(kHero) < 0.01f);
        CHECK(Logged(w, "Hero stands 2.00 deg off Genlock as its ability is given: faced at it exactly (the engine "
                        "would begin no turn for it, and its ability would wait for one: issue 40); the engine now "
                        "has it 0.00 deg off: done"));
    }
    SECTION("at a place, with nobody aimed at")
    {
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        CHECK(w.facedFirst.empty());
        CHECK_FALSE(Logged(w, kFaced));
    }
    SECTION("at its own user")
    {
        cast.target = kHero;
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        CHECK(w.facedFirst.empty());
        CHECK_FALSE(Logged(w, kFaced));
    }
}

TEST_CASE("a command that turns its creature to nobody is not the rule's, whatever turn comes with it", "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A walk to someone, a stalled turn given all the same (the gate asks
    // for an attack and an ability only; the machine does not lean on that).
    CommandRequest walk = HerAttack(TurnToFace{1.5f, false, true});
    walk.type = CommandType::MoveToObject;
    const Verdict verdict = w.machine.OnCommand(walk);
    REQUIRE(verdict == Verdict::Allow);
    CHECK_FALSE(w.machine.FaceFirst(walk, verdict));
}

TEST_CASE("a command refused or kept is not given, and its creature is not turned for it", "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CommandRequest attack = HerAttack(TurnToFace{1.5f, false, true});
    attack.object = kGenlock;
    attack.source = Source::Script;
    attack.target = kHero;
    // Out of its turn: refused.
    const Verdict refused = w.machine.OnCommand(attack);
    REQUIRE(refused == Verdict::Block);
    CHECK_FALSE(w.machine.FaceFirst(attack, refused));
    // The same command, were it kept to be given again: not faced now; the
    // engine is asked again when it is given.
    CHECK_FALSE(w.machine.FaceFirst(attack, Verdict::Hold));
    // Let through, it is; and so is a cast the engine conjures, held.
    const CommandRequest hers = HerAttack(TurnToFace{1.5f, false, true});
    CHECK(w.machine.FaceFirst(hers, Verdict::Allow));
    CHECK(w.machine.FaceFirst(hers, Verdict::Conjure));
}

TEST_CASE("with no answer from the engine nobody is faced", "[facing]")
{
    World w = Placed();
    w.StartFight();
    // No facing of hers: the stand-in's engine has no turn to answer, as the
    // gate has none off the game's thread.
    w.Get(kHero).stalled = true;
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK_FALSE(w.foundOff.contains(kHero));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("with no fight on the game is as it was: nobody is faced", "[facing]")
{
    World w = Placed();
    // Before any fight.
    w.Stall(kHero, kGenlock, 1.5f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kHero) == Approx(1.5f).margin(1e-3));
    CHECK_FALSE(Logged(w, kFaced));
    w.Finish(kHero);
    // And in a fight played in real time, the mod turned off.
    w.Frame();
    REQUIRE(w.machine.RequestToggle() == ToggleResult::Disabled);
    w.mode = Mode::Combat;
    w.Frame();
    w.Frame();
    w.Frame();
    REQUIRE_FALSE(w.machine.Active());
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kHero) == Approx(1.5f).margin(1e-3));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("a fight standing aside for a conversation faces nobody: what its scene has them do is the game's",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.mode = Mode::Dialog;
    w.Frame();
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
    // The fight's own creature, still in its order, off whom the scene has
    // it strike.
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    w.Stall(kGenlock, kHero, 1.5f);
    REQUIRE(w.Attack(kGenlock, kHero, Source::Script) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kGenlock) == Approx(1.5f).margin(1e-3));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("one the fight does not hold is the game's: not faced", "[facing]")
{
    World w = Placed();
    // A bystander the game has out of combat, its script's own command let
    // through as the game's.
    Creature bystander = Make(0x300, Side::Enemy, false, L"Bystander", 10.0f);
    bystander.fighting = false;
    bystander.engaged = false;
    bystander.perception = Unnoticing();
    bystander.position = Vec3{10.0f, 0.0f, 0.0f};
    w.creatures.push_back(bystander);
    w.StartFight();
    REQUIRE(w.machine.Order().Find(0x300) == nullptr);
    w.Stall(0x300, kHero, 1.5f);
    REQUIRE(w.Attack(0x300, kHero, Source::Script) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("an AI's own attack in its turn is faced first like the player's", "[facing]")
{
    World w = Placed();
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && answers.empty())
        {
            w.Stall(kGenlock, kHero, -2.5f);
            answers.push_back(w.Attack(kGenlock, kHero, Source::Script));
        }
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    for (int i = 0; i < 200 && answers.empty(); ++i)
    {
        w.Frame();
    }
    REQUIRE(answers.size() == 1);
    REQUIRE(answers.front() == Verdict::Allow);
    REQUIRE(w.facedFirst.size() == 1);
    CHECK(w.facedFirst.front() == std::pair{kGenlock, kHero});
    CHECK(w.foundOff.at(kGenlock) < 0.01f);
    CHECK(Logged(w, "Genlock stands 2.50 deg off Hero as its attack is given: faced at it exactly"));
}

// The hang the rule is for (issue 40, 2026-10-05, 17:01, Ostagar): a
// Hurlock's attack kept from the turn before was given back with its
// creature a degree off its unmoved target and nothing turning, and stood at
// its turn to face until the action limit.
TEST_CASE("a kept attack given back to a creature a little over a degree off its unmoved target finds it facing",
          "[facing][time]")
{
    World w = Placed();
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && answers.empty())
        {
            // A swing longer than its bar: kept, to be given at its next turn.
            answers.push_back(w.Attack(id, kHero, Source::Script, NoAttack::None, 0.0f, 6.0f));
        }
    };
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    (void)w.machine.OnEndTurn();
    for (int i = 0; i < 200 && answers.empty(); ++i)
    {
        w.Frame();
    }
    REQUIRE(answers.size() == 1);
    REQUIRE(answers.front() == Verdict::Hold);
    REQUIRE(w.heldCasts.contains(kGenlock));
    CHECK(w.facedFirst.empty()); // kept, not given: nobody is turned for it
    // It stands where its last turn at this target stopped: a degree short,
    // and a hair, its controller at rest.
    w.Stall(kGenlock, kHero, -1.02f);
    for (int i = 0; i < 6000 && w.released.empty(); ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    REQUIRE(w.released.size() == 1);
    REQUIRE(w.released.front() == Verdict::Allow);
    REQUIRE(w.facedFirst.size() == 1);
    CHECK(w.facedFirst.front() == std::pair{kGenlock, kHero});
    CHECK(w.foundOff.at(kGenlock) < 0.01f);
    CHECK(Logged(w, "Genlock stands 1.02 deg off Hero as its attack is given: faced at it exactly"));
}

TEST_CASE("the count is the fight's own: the next fight starts from none", "[facing]")
{
    // Three swings of a second in one turn, of a bar of four: after each she
    // is left off her target again, as one that stepped aside would leave
    // her.
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (int given = 0; given < 3; ++given)
    {
        w.Stall(kHero, kGenlock, 1.5f + static_cast<float>(given));
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.0f) == Verdict::Allow);
        w.Finish(kHero);
        w.RunFor(600);
    }
    CHECK(w.facedFirst.size() == 3);
    EndFight(w);
    CHECK(Lines(w, "in this fight 3 attacks and abilities were given to a creature the engine would not have turned "
                   "to its target, which was faced at it exactly first (issue 40)") == 1);
    // Another fight, with nobody faced in it: nothing said of the last one's.
    w.log.clear();
    w.Get(kGenlock).down = false;
    w.Get(kHurlock).down = false;
    w.inCombat = true;
    w.StartFight();
    REQUIRE(w.machine.Active());
    EndFight(w);
    CHECK(Lines(w, kCounted) == 0);
}

// What play showed of the rule's first form (2026-10-05, 19:41): `Zevran
// stands 3.95 deg off Great Bear as its attack is given: faced at it
// exactly`, and his attack stood at its turn to face until the action limit.
// He had been facing the engine's own aim for a blow at the bear, a point of
// its ring; the mod's own measure was to where the bear stood, 3.95 degrees
// from it, and it turned him there.
TEST_CASE("a blow at a big creature: one facing the engine's aim is left alone, however far that is from where its "
          "target stands",
          "[facing]")
{
    World w = Placed();
    MakeBig(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // As Zevran stood: on the engine's aim, his controller at rest there.
    w.Stall(kHero, kGenlock, 0.0f);
    REQUIRE(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kHero) < 0.01f);
    // Still on the engine's aim, not turned to the bear's place.
    CHECK(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    CHECK_FALSE(Logged(w, kFaced));
    CHECK_FALSE(Logged(w, kNotFaced));
}

TEST_CASE("a blow at a big creature: one the engine would not turn is faced at the engine's aim, not at where its "
          "target stands",
          "[facing]")
{
    World w = Placed();
    MakeBig(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // Facing where the bear stands, 3.95 degrees off the engine's aim, with
    // nothing to turn her: what the first form left Zevran as.
    w.Stall(kHero, kGenlock, 3.95f);
    REQUIRE(OffItsPlace(w) < 0.02f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    REQUIRE(w.facedFirst.size() == 1);
    CHECK(w.foundOff.at(kHero) < 0.01f);
    CHECK(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    CHECK(Logged(w, "Hero stands 3.95 deg off Genlock as its attack is given: faced at it exactly"));
    CHECK(Logged(w, "the engine now has it 0.00 deg off: done"));
}

TEST_CASE("the engine will not face a creature whose movement controller is not idle: said at warning, not counted",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Stall(kHero, kGenlock, -2.0f);
    w.faceRefused = true;
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(Lines(w, "Hero stood 2.00 deg off Genlock as its attack was given, the engine with no turn to begin for "
                   "it, and was to be faced at it exactly: the engine would not face it (its movement controller is "
                   "not idle), nothing was set, and the engine still has it 2.00 deg off: not done; its attack is "
                   "given as it stands (issue 40)") == 1);
    CHECK_FALSE(Logged(w, kFaced));
    // As it stood: what the command would have found with no rule.
    CHECK(w.foundOff.at(kHero) == Approx(2.0f).margin(1e-3));
    EndFight(w);
    CHECK(Lines(w, kCounted) == 0);
}

TEST_CASE("a facing the engine does not have for done after is said at warning with both angles, and not counted",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CommandRequest cast = HerAttack(TurnToFace{-1.25f, false, true});
    cast.type = CommandType::UseAbility;
    // Faced, and the engine still not done.
    w.machine.Faced(cast, true, TurnToFace{3.95f, false, true});
    CHECK(Lines(w, "Hero stood 1.25 deg off Genlock as its ability was given, the engine with no turn to begin for "
                   "it, and was to be faced at it exactly: it was, and the engine still has it 3.95 deg off: not "
                   "done; its ability is given as it stands (issue 40)") == 1);
    // Faced, and the engine with no answer after.
    w.machine.Faced(cast, true, std::nullopt);
    CHECK(Lines(w, "and was to be faced at it exactly: it was, and the engine has no turn to say for it now; its "
                   "ability is given as it stands (issue 40)") == 1);
    // Not faced, by the engine's own word, though it has it for done now: a
    // warning all the same, since what was asked was not done by asking.
    w.machine.Faced(cast, false, TurnToFace{0.5f, true, false});
    CHECK(Lines(w, "nothing was set, and the engine has it 0.50 deg off now: done; its ability is given as it "
                   "stands (issue 40)") == 1);
    CHECK(Lines(w, kNotFaced) == 3);
    CHECK_FALSE(Logged(w, kFaced));
    // And one faced and done is said, with what the engine has now, and
    // counted: the only one of the four.
    w.machine.Faced(cast, true, TurnToFace{0.25f, true, false});
    CHECK(Logged(w, "Hero stands 1.25 deg off Genlock as its ability is given: faced at it exactly (the engine would "
                    "begin no turn for it, and its ability would wait for one: issue 40); the engine now has it 0.25 "
                    "deg off: done"));
    EndFight(w);
    CHECK(Lines(w, "in this fight 1 attack or ability was") == 1);
}
