// A creature the engine would not turn to face what its attack is aimed at
// is faced at it exactly, as its command comes to its turn to face (issue 40;
// core/Facing.h has the engine's reading and the rule's earlier forms). Which
// commands the rule is for; then the machine's say on the looks the game
// layer takes of the fight's creatures at their turn to face, in the stand-in
// for the game (World.h), whose engine takes a turn to face a frame at a time
// (past it, turned and past it, standing at it for good where it stalls, or
// for a while where it waits), faces a
// creature when the machine says to and says what came of it; the stories
// the rule is for and from: a kept attack given back to a creature a little
// over a degree off its unmoved target, a blow at a big creature, whose aim
// is not where it stands, and an attack that begins with a walk, whose
// creature is not spun round as the command is given.
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
void Place(World &w)
{
    w.Get(kHero).position = Vec3{0.0f, 0.0f, 0.0f};
    w.Get(kCompanion).position = Vec3{0.0f, 2.0f, 0.0f};
    w.Get(kGenlock).position = Vec3{3.0f, 0.0f, 0.0f};
    w.Get(kHurlock).position = Vec3{3.0f, 2.0f, 0.0f};
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
    return std::abs(std::atan2((hero.facingX * y) - (hero.facingY * x), (hero.facingX * x) + (hero.facingY * y))) *
           (180.0f / std::numbers::pi_v<float>);
}

// How far the engine has a creature off its aim at another: what its
// command's own test would find.
float Off(const World &w, ObjectId id, ObjectId target)
{
    const std::optional<TurnToFace> turn = w.Turn(CommandType::Attack, id, target);
    REQUIRE(turn);
    return std::abs(turn->degrees);
}

// The hero's attack on the genlock, let through, with the world running
// again after her order; not yet at its turn to face.
void HerAttack(World &w, float seconds = 0.0f)
{
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, seconds) == Verdict::Allow);
    for (int i = 0; i < 10 && w.paused; ++i)
    {
        w.Frame();
    }
    REQUIRE_FALSE(w.paused);
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

constexpr const char *kFaced = "comes to its turn to face: faced at it exactly";
constexpr const char *kNotFaced = "was to be faced at it exactly";
constexpr const char *kWaits = "waits at its";
constexpr const char *kCounted = "which was faced at its target exactly (issue 40)";

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

TEST_CASE("the player's attack come to its turn to face, the engine with no turn to begin: faced at its target, the "
          "engine asked again, said, and counted for the fight's end",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    w.Stall(kHero, kGenlock, 1.5f);
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(200);
    REQUIRE(w.faced.size() == 1);
    CHECK(w.faced.front() == std::pair{kHero, kGenlock});
    // On the engine's aim, and its command past its turn to face.
    CHECK(Off(w, kHero, kGenlock) < 0.01f);
    CHECK_FALSE(w.Get(kHero).atTurnToFace);
    CHECK(w.turnedByEngine == 0);
    CHECK(Lines(w, "Hero stands 1.50 deg off Genlock as its attack comes to its turn to face: faced at it exactly "
                   "(the engine would begin no turn for it, and its attack would wait for one: issue 40); the engine "
                   "now has it 0.00 deg off: done") == 1);
    CHECK_FALSE(Logged(w, kNotFaced));
    CHECK_FALSE(Logged(w, kWaits));
    // Said once as the fight ends.
    CHECK(Lines(w, kCounted) == 0);
    EndFight(w);
    CHECK(Lines(w, "in this fight 1 attack or ability came to its turn to face with a creature the engine would not "
                   "have turned, which was faced at its target exactly (issue 40)") == 1);
}

TEST_CASE("at whatever angle, and either way round: the dead case is the engine's to name, not an angle's", "[facing]")
{
    // The first hang's edge; the angle the first form's own hang stood at;
    // one past the five degrees the rule once stopped at; and a long way
    // round.
    for (const float off : {1.02f, -1.02f, 3.95f, -6.5f, 40.0f, -170.0f})
    {
        World w;
        Place(w);
        w.StartFight();
        w.Stall(kHero, kGenlock, off);
        HerAttack(w);
        w.AtTurnToFace(kHero, kGenlock);
        w.RunFor(200);
        INFO(off << " degrees off");
        CHECK(w.faced.size() == 1);
        CHECK(Off(w, kHero, kGenlock) < 0.01f);
        CHECK(Lines(w, kFaced) == 1);
    }
}

TEST_CASE("a turn to face that stalls is faced at the first look, and asked for once", "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    w.Stall(kHero, kGenlock, 2.0f);
    HerAttack(w);
    REQUIRE_FALSE(w.Did(EffectKind::FaceTarget));
    w.AtTurnToFace(kHero, kGenlock);
    w.Frame();
    REQUIRE(w.faced.size() == 1);
    CHECK(w.Did(EffectKind::FaceTarget, kHero));
    // The frames after ask nothing more.
    w.RunFor(200);
    CHECK(std::ranges::count(w.applied, EffectKind::FaceTarget, &Effect::kind) == 1);
    CHECK(w.faced.size() == 1);
}

// What the third form's spin was (2026-10-05, 22:15:13, `Alistair stands
// 159.43 deg off Dragonling`): no dead case, by DAOLib's reading after it.
// Just out of an ability's animation, his controller's motion record was
// busy, and the engine begins no turn while it is; it asks again on every
// update, and turns him when his own motion is over.
TEST_CASE("one whose turn to face waits is left to the engine, which turns it when its own motion is over: said "
          "once, not faced",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    w.FaceOff(kHero, kGenlock, 159.43f);
    w.Get(kHero).waitsFrames = 12;
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(100);
    // Still waiting, and left as it stands.
    CHECK(w.Get(kHero).atTurnToFace);
    CHECK(Off(w, kHero, kGenlock) == Approx(159.43f).margin(0.01));
    CHECK(w.turnedByEngine == 0);
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    // Turned by the engine's own turn, and past it.
    CHECK(w.turnedByEngine == 1);
    CHECK(Off(w, kHero, kGenlock) < 0.01f);
    CHECK_FALSE(w.Get(kHero).atTurnToFace);
    CHECK(Lines(w, "Hero waits at its attack's turn to face Genlock, 159.43 deg off: the engine begins no turn for "
                   "it yet, its own motion still playing, which passes by itself; left to the engine (issue 40)") == 1);
    CHECK_FALSE(Logged(w, kFaced));
    EndFight(w);
    CHECK(Lines(w, kCounted) == 0);
}

TEST_CASE("a turn that waits and then stalls is faced then: waiting is the engine saying it is not yet a stall",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    // Its own motion playing for a few frames, and its controller's own
    // facing on the aim all the while: when the motion is over nothing
    // will turn it.
    w.FaceOff(kHero, kGenlock, 3.0f);
    w.Get(kHero).waitsFrames = 4;
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.Frame();
    w.Frame();
    CHECK(w.faced.empty());
    CHECK(Lines(w, kWaits) == 1);
    // The dead case now, by the engine's word.
    w.Get(kHero).waitsFrames = 0;
    w.Get(kHero).stalled = true;
    w.Frame();
    REQUIRE(w.faced.size() == 1);
    w.RunFor(100);
    CHECK(Lines(w, kFaced) == 1);
    CHECK(Lines(w, kWaits) == 1);
}

TEST_CASE("one the engine would turn is left to its turn, however little it is off: not faced, nothing said",
          "[facing]")
{
    for (const float off : {1.02f, -1.5f, 3.95f, 30.0f, 179.0f})
    {
        World w;
        Place(w);
        w.StartFight();
        // Off its target, and its movement controller with a turn to make.
        w.FaceOff(kHero, kGenlock, off);
        HerAttack(w);
        w.AtTurnToFace(kHero, kGenlock);
        w.RunFor(200);
        INFO(off << " degrees off");
        CHECK(w.faced.empty());
        CHECK(w.turnedByEngine == 1);
        CHECK_FALSE(w.Did(EffectKind::FaceTarget));
        CHECK_FALSE(Logged(w, kFaced));
        CHECK_FALSE(Logged(w, kNotFaced));
        CHECK_FALSE(Logged(w, kWaits));
    }
}

TEST_CASE("one the engine has for facing its target goes past its turn as it stands, and is not counted", "[facing]")
{
    for (const float off : {0.0f, 0.5f, -0.9f})
    {
        World w;
        Place(w);
        w.StartFight();
        // Its controller as dead as can be: with the creature facing, the
        // engine asks no turn of anyone.
        w.Stall(kHero, kGenlock, off);
        HerAttack(w);
        w.AtTurnToFace(kHero, kGenlock);
        w.RunFor(200);
        INFO(off << " degrees off");
        CHECK(w.faced.empty());
        CHECK(w.turnedByEngine == 0);
        // Not set straight either.
        CHECK(Off(w, kHero, kGenlock) == Approx(std::abs(off)).margin(1e-3));
        CHECK_FALSE(Logged(w, kFaced));
        EndFight(w);
        // A fight in which nobody was faced says nothing of it.
        CHECK(Lines(w, kCounted) == 0);
    }
}

// What play showed of the rule asked as the command was given (2026-10-05,
// 22:15:13): `Alistair: attack with 27.1 m to walk`, `Alistair stands 159.43
// deg off Dragonling as its attack is given: faced at it exactly`,
// `0x1b88's attack is at sub-action 4 (its approach)`. Spun round in a frame,
// for a command whose walk would have turned him.
TEST_CASE("an attack that begins with a walk: its creature is not turned as the command is given, and the walk "
          "leaves the engine its own turn to make",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    // As the command is given: far round from his target, the engine with
    // no turn to begin for him.
    w.Stall(kHero, kGenlock, 159.43f);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 3.0f) == Verdict::Allow);
    // At its approach: nothing is asked, and nothing faced, however long.
    w.RunFor(1000);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(Off(w, kHero, kGenlock) == Approx(159.43f).margin(0.01));
    // The walk has turned him, to within some degrees, and left his
    // controller with a turn it will make.
    w.FaceOff(kHero, kGenlock, 6.0f);
    w.Get(kHero).stalled = false;
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(200);
    CHECK(w.faced.empty());
    CHECK(w.turnedByEngine == 1);
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("an ability aimed at another is as an attack: faced at its turn to face; one aimed at nobody, or at its "
          "user, has none",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Stall(kHero, kGenlock, 2.0f);
    CommandRequest cast;
    cast.object = kHero;
    cast.type = CommandType::UseAbility;
    cast.source = Source::Player;
    cast.inCombat = true;
    cast.ability = 11;
    w.cooldownOf[cast.ability] = 10.0f;
    SECTION("at another")
    {
        cast.target = kGenlock;
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        w.AtTurnToFace(kHero, kGenlock);
        w.RunFor(300);
        REQUIRE(w.faced.size() == 1);
        CHECK(Logged(w, "Hero stands 2.00 deg off Genlock as its ability comes to its turn to face: faced at it "
                        "exactly (the engine would begin no turn for it, and its ability would wait for one: issue "
                        "40); the engine now has it 0.00 deg off: done"));
    }
    SECTION("at a place, with nobody aimed at")
    {
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        w.AtTurnToFace(kHero, kNoObject);
        w.RunFor(300);
        CHECK(w.faced.empty());
        CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    }
    SECTION("at its own user")
    {
        cast.target = kHero;
        REQUIRE(w.Queue(cast) == Verdict::Allow);
        w.AtTurnToFace(kHero, kHero);
        w.RunFor(300);
        CHECK(w.faced.empty());
        CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    }
}

TEST_CASE("a look at a command that turns its creature to nobody is not the rule's, whatever turn comes with it",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    HerAttack(w);
    // A walk to someone, handed the machine as a stalled turn all the same
    // (the game layer looks at attacks and abilities only; the machine does
    // not lean on that).
    w.looks.push_back(TurnLook{kHero, kGenlock, CommandType::MoveToObject, 7, TurnToFace{1.5f, false, true, false}});
    w.RunFor(300);
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(w.faced.empty());
    // The same look of an attack is.
    w.looks.front().type = CommandType::Attack;
    w.RunFor(300);
    CHECK(w.Did(EffectKind::FaceTarget, kHero));
}

TEST_CASE("one the fight does not hold is the game's: not faced", "[facing]")
{
    World w;
    Place(w);
    // A bystander the game has out of combat, its script's own attack at a
    // turn to face that stalls.
    Creature bystander = Make(0x300, Side::Enemy, false, L"Bystander", 10.0f);
    bystander.fighting = false;
    bystander.engaged = false;
    bystander.perception = Unnoticing();
    bystander.position = Vec3{10.0f, 0.0f, 0.0f};
    w.creatures.push_back(bystander);
    w.StartFight();
    REQUIRE(w.machine.Order().Find(0x300) == nullptr);
    HerAttack(w);
    // The game layer is not asked to look at it; and a look at it handed
    // the machine all the same is not taken up.
    w.Stall(0x300, kHero, 1.5f);
    REQUIRE(w.Attack(0x300, kHero, Source::Script) == Verdict::Allow);
    w.AtTurnToFace(0x300, kHero);
    w.looks.push_back(TurnLook{0x300, kHero, CommandType::Attack, 9, TurnToFace{1.5f, false, true, false}});
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(w.Get(0x300).atTurnToFace);
}

TEST_CASE("one of the fight that is not the turn's own is faced all the same: a command let through out of turn "
          "turns its creature too",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    HerAttack(w);
    // In the hero's turn, the hurlock at an attack of its own that the gate
    // let through (the game's, for one it has out of combat; or one running
    // from before): at its turn to face, with nothing to turn it.
    Creature &hurlock = w.Get(kHurlock);
    hurlock.current = CommandType::Attack;
    hurlock.command = 77;
    w.Stall(kHurlock, kCompanion, -2.5f);
    w.AtTurnToFace(kHurlock, kCompanion);
    w.RunFor(200);
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.faced.size() == 1);
    CHECK(w.faced.front() == std::pair{kHurlock, kCompanion});
    CHECK(Logged(w, "Hurlock stands 2.50 deg off Alistair as its attack comes to its turn to face: faced at it "
                    "exactly"));
}

TEST_CASE("with no fight on the game is as it was: nobody is looked at, and nobody faced", "[facing]")
{
    World w;
    Place(w);
    // Before any fight.
    w.Stall(kHero, kGenlock, 1.5f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK(Off(w, kHero, kGenlock) == Approx(1.5f).margin(1e-3));
    w.Finish(kHero);
    // And in a fight played in real time, the mod turned off.
    REQUIRE(w.machine.RequestToggle() == ToggleResult::Disabled);
    w.mode = Mode::Combat;
    w.Frame();
    w.Frame();
    w.Frame();
    REQUIRE_FALSE(w.machine.Active());
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(Off(w, kHero, kGenlock) == Approx(1.5f).margin(1e-3));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("a fight standing aside for a conversation faces nobody: what its scene has them do is the game's",
          "[facing]")
{
    World w;
    Place(w);
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
    w.AtTurnToFace(kGenlock, kHero);
    w.looks.push_back(TurnLook{kGenlock, kHero, CommandType::Attack, 9, TurnToFace{1.5f, false, true, false}});
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(Off(w, kGenlock, kHero) == Approx(1.5f).margin(1e-3));
}

TEST_CASE("an AI's own attack in its turn is faced at its turn to face like the player's", "[facing]")
{
    World w;
    Place(w);
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && answers.empty())
        {
            w.Stall(kGenlock, kHero, -2.5f);
            answers.push_back(w.Attack(kGenlock, kHero, Source::Script));
            w.AtTurnToFace(kGenlock, kHero);
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
    w.RunFor(200);
    REQUIRE(w.faced.size() == 1);
    CHECK(w.faced.front() == std::pair{kGenlock, kHero});
    CHECK(Off(w, kGenlock, kHero) < 0.01f);
    CHECK(Logged(w, "Genlock stands 2.50 deg off Hero as its attack comes to its turn to face: faced at it exactly"));
}

// The hang the rule is for (issue 40, 2026-10-05, 17:01, Ostagar): a
// Hurlock's attack kept from the turn before was given back with its
// creature a degree off its unmoved target and nothing turning, and stood at
// its turn to face until the action limit.
TEST_CASE("a kept attack given back to a creature a little over a degree off its unmoved target, its turn to face "
          "at once: faced",
          "[facing][time]")
{
    World w;
    Place(w);
    std::vector<Verdict> answers;
    w.onPoke = [&](ObjectId id) {
        if (id == kGenlock && answers.empty())
        {
            // A swing longer than its bar: kept, to be given at its next turn.
            answers.push_back(w.Attack(id, kHero, Source::Script, NoAttack::None, 0.0f, 6.0f));
        }
    };
    // In reach as it is given back, it has no approach to make: at its turn
    // to face from the first.
    w.onReleased = [&](ObjectId id, const CommandRequest &again, Verdict verdict) {
        if (verdict == Verdict::Allow)
        {
            w.AtTurnToFace(id, again.target);
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
    CHECK(w.faced.empty()); // not as it is given
    w.RunFor(200);
    REQUIRE(w.faced.size() == 1);
    CHECK(w.faced.front() == std::pair{kGenlock, kHero});
    CHECK(Off(w, kGenlock, kHero) < 0.01f);
    CHECK(Logged(w, "Genlock stands 1.02 deg off Hero as its attack comes to its turn to face: faced at it exactly"));
}

TEST_CASE("what is asked and said of a command is that command's: the next one at its turn to face is asked for "
          "anew",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    w.Stall(kHero, kGenlock, 2.0f);
    w.faceRefused = true;
    HerAttack(w, 1.0f);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(300);
    REQUIRE(std::ranges::count(w.applied, EffectKind::FaceTarget, &Effect::kind) == 1);
    // That swing is over where it stood (stopped, say), and another is
    // given and comes to its turn to face in the very next frame.
    w.Finish(kHero);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.0f) == Verdict::Allow);
    w.AtTurnToFace(kHero, kGenlock);
    w.faceRefused = false;
    w.RunFor(300);
    CHECK(std::ranges::count(w.applied, EffectKind::FaceTarget, &Effect::kind) == 2);
    CHECK(w.faced.size() == 1);
    CHECK(Lines(w, kNotFaced) == 1);
    CHECK(Lines(w, kFaced) == 1);
}

TEST_CASE("the count is the fight's own: the next fight starts from none", "[facing]")
{
    // Three swings of a second in one turn, of a bar of four: at each she is
    // off her target again with nothing to turn her, as one that stepped
    // aside would leave her.
    World w;
    Place(w);
    w.StartFight();
    for (int given = 0; given < 3; ++given)
    {
        w.Stall(kHero, kGenlock, 1.5f + static_cast<float>(given));
        HerAttack(w, 1.0f);
        w.AtTurnToFace(kHero, kGenlock);
        w.RunFor(200);
        w.Finish(kHero);
        w.RunFor(600);
    }
    CHECK(w.faced.size() == 3);
    EndFight(w);
    CHECK(Lines(w, "in this fight 3 attacks and abilities came to their turn to face with a creature the engine would "
                   "not have turned, which was faced at its target exactly (issue 40)") == 1);
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
    World w;
    Place(w);
    MakeBig(w);
    w.StartFight();
    // As Zevran stood: on the engine's aim, his controller at rest there.
    w.Stall(kHero, kGenlock, 0.0f);
    REQUIRE(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(200);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Get(kHero).atTurnToFace);
    // Still on the engine's aim, not turned to the bear's place.
    CHECK(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    CHECK_FALSE(Logged(w, kFaced));
    CHECK_FALSE(Logged(w, kNotFaced));
}

TEST_CASE("a blow at a big creature: one the engine would not turn is faced at the engine's aim, not at where its "
          "target stands",
          "[facing]")
{
    World w;
    Place(w);
    MakeBig(w);
    w.StartFight();
    // Facing where the bear stands, 3.95 degrees off the engine's aim, with
    // nothing to turn her: what the first form left Zevran as.
    w.Stall(kHero, kGenlock, 3.95f);
    REQUIRE(OffItsPlace(w) < 0.02f);
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.RunFor(200);
    REQUIRE(w.faced.size() == 1);
    CHECK(Off(w, kHero, kGenlock) < 0.01f);
    CHECK(OffItsPlace(w) == Approx(3.95f).margin(0.01));
    CHECK(Logged(w, "Hero stands 3.95 deg off Genlock as its attack comes to its turn to face: faced at it exactly"));
    CHECK(Logged(w, "the engine now has it 0.00 deg off: done"));
}

TEST_CASE("the engine will not face a creature whose movement controller is not idle: said at warning, once, and "
          "not counted",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    w.Stall(kHero, kGenlock, -2.0f);
    w.faceRefused = true;
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    // Asked for once a command: not again every frame it still stands so.
    w.RunFor(500);
    CHECK(w.faced.empty());
    CHECK(std::ranges::count(w.applied, EffectKind::FaceTarget, &Effect::kind) == 1);
    CHECK(Lines(w, "Hero stood 2.00 deg off Genlock as its attack came to its turn to face, the engine with no turn "
                   "to begin for it, and was to be faced at it exactly: the engine would not face it (its movement "
                   "controller is not idle), nothing was set, and the engine still has it 2.00 deg off: not done; "
                   "its attack is left as it stands (issue 40)") == 1);
    CHECK(Lines(w, kNotFaced) == 1);
    CHECK_FALSE(Logged(w, kFaced));
    // As it stood: what the command would have had with no rule.
    CHECK(Off(w, kHero, kGenlock) == Approx(2.0f).margin(1e-3));
    EndFight(w);
    CHECK(Lines(w, kCounted) == 0);
}

TEST_CASE("a facing the engine does not have for done after is said at warning with both angles, and not counted",
          "[facing]")
{
    World w;
    Place(w);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    FacedLook cast;
    cast.who = kHero;
    cast.target = kGenlock;
    cast.type = CommandType::UseAbility;
    cast.stood = 1.25f;
    // Faced, and the engine still not done.
    cast.taken = true;
    cast.now = TurnToFace{-3.95f, false, true, false};
    w.facedLooks.push_back(cast);
    w.Frame();
    CHECK(Lines(w, "Hero stood 1.25 deg off Genlock as its ability came to its turn to face, the engine with no turn "
                   "to begin for it, and was to be faced at it exactly: it was, and the engine still has it 3.95 deg "
                   "off: not done; its ability is left as it stands (issue 40)") == 1);
    // Faced, and the engine with no answer after.
    cast.now.reset();
    w.facedLooks.push_back(cast);
    w.Frame();
    CHECK(Lines(w, "and was to be faced at it exactly: it was, and the engine has no turn to say for it now; its "
                   "ability is left as it stands (issue 40)") == 1);
    // Not faced, by the engine's own word, though it has it for done now: a
    // warning all the same, since what was asked for was not done by asking.
    cast.taken = false;
    cast.now = TurnToFace{0.5f, true, false, false};
    w.facedLooks.push_back(cast);
    w.Frame();
    CHECK(Lines(w, "nothing was set, and the engine has it 0.50 deg off now: done; its ability is left as it stands "
                   "(issue 40)") == 1);
    CHECK(Lines(w, kNotFaced) == 3);
    CHECK_FALSE(Logged(w, kFaced));
    // And one faced and done is said, with what the engine has now, and
    // counted: the only one of the four.
    cast.taken = true;
    cast.now = TurnToFace{-0.25f, true, false, false};
    w.facedLooks.push_back(cast);
    w.Frame();
    CHECK(Logged(w, "Hero stands 1.25 deg off Genlock as its ability comes to its turn to face: faced at it exactly "
                    "(the engine would begin no turn for it, and its ability would wait for one: issue 40); the "
                    "engine now has it 0.25 deg off: done"));
    EndFight(w);
    CHECK(Lines(w, "in this fight 1 attack or ability came to its") == 1);
}

// Where this rule meets issue 51's two (2026-10-05, the tree of issues 50, 40
// and 51 together). Neither changes the other; these hold that it stays so.

// A creature at its own mode's command (a bard singing) is refused an attack
// before anything is spent, "Turn its mode off first" (issue 51). Refused is
// not given, so no command of it comes to a turn to face, and its creature is
// not turned, whatever the engine would say of the turn.
TEST_CASE("an attack refused a creature at its own mode's command turns nobody, though the engine would not turn it",
          "[facing][ownmode]")
{
    World w;
    Place(w);
    w.Get(kHero).modes.insert(kSong);
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Stall(kHero, kGenlock, 1.5f);
    CHECK(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Block);
    w.RunFor(300);
    CHECK(w.faced.empty());
    CHECK_FALSE(w.Did(EffectKind::FaceTarget));
    CHECK(OffItsPlace(w) == Approx(1.5f).margin(0.01f));
    CHECK_FALSE(Logged(w, kFaced));
    CHECK_FALSE(Logged(w, kNotFaced));
    CHECK(Logged(w, "Hero: attack from player refused, it is at its own mode's command (ability 1000)"));
    // The song switched off is given: aimed at nobody, it turns nobody.
    CHECK(w.Switch(kHero, Source::Player, kSong) == Verdict::Allow);
    w.RunFor(300);
    CHECK(w.faced.empty());
}

// End pressed while an attack of the turn runs stops it (issue 51). One that
// was faced at its turn to face was faced all the same: the count said as
// the fight ends is of the facings done, not of the attacks that then played
// out.
TEST_CASE("an attack faced at its turn to face and then stopped by End is counted for the fight's end all the same",
          "[facing][ownmode]")
{
    World w;
    Place(w);
    w.StartFight();
    w.Stall(kHero, kGenlock, 1.5f);
    HerAttack(w);
    w.AtTurnToFace(kHero, kGenlock);
    w.Frame();
    REQUIRE(w.faced.size() == 1);
    w.Frame();
    w.applied.clear();
    REQUIRE(w.machine.OnEndTurn());
    w.Frame();
    CHECK(w.Current() != kHero);
    CHECK(std::ranges::count_if(w.applied, [](const Effect &e) {
              return e.kind == EffectKind::ClearCommands && e.target == kHero;
          }) == 1);
    CHECK(Lines(w, "Hero's attack is stopped: its turn was ended while it ran") == 1);
    EndFight(w);
    CHECK(Lines(w, "in this fight 1 attack or ability came to its turn to face with a creature the engine would not "
                   "have turned, which was faced at its target exactly (issue 40)") == 1);
}
