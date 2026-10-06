// A creature all but facing what its attack is aimed at is faced at it
// exactly before the command is given (issue 40; core/Facing.h has the
// engine's reading). The rule by itself first: the angle a creature has to
// turn, and what is done at each. Then the machine's say for each command the
// gate lets through, in the stand-in for the game (World.h), whose Queue
// measures and faces as the gate does; and the story the rule is for, a kept
// attack given back to a creature standing a degree off its unmoved target.
#include "World.h"

#include "core/Facing.h"
#include "core/Rules.h"
#include "core/TurnMachine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
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

// The direction a bearing in degrees is, a length of one.
struct Way
{
    float x = 0.0f;
    float y = 0.0f;
};
Way At(float degrees)
{
    const float radians = degrees * (std::numbers::pi_v<float> / 180.0f);
    return Way{std::cos(radians), std::sin(radians)};
}

// How far one facing the first bearing has to turn to face the second.
std::optional<float> Turn(float facing, float toward)
{
    const Way from = At(facing);
    const Way to = At(toward);
    return TurnDegrees(from.x, from.y, to.x, to.y);
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

constexpr const char *kFaced = "faced at it exactly";
constexpr const char *kCounted = "which was faced at it exactly first (issue 40)";

} // namespace

TEST_CASE("how far a creature has to turn is the angle from its facing to its target's bearing", "[facing]")
{
    CHECK(*Turn(30.0f, 30.0f) == Approx(0.0f).margin(1e-4));
    CHECK(*Turn(30.0f, 31.0f) == Approx(1.0f).margin(1e-4));
    CHECK(*Turn(31.0f, 30.0f) == Approx(-1.0f).margin(1e-4));
    CHECK(*Turn(0.0f, 90.0f) == Approx(90.0f).margin(1e-4));
    CHECK(*Turn(90.0f, 0.0f) == Approx(-90.0f).margin(1e-4));
    CHECK(std::abs(*Turn(10.0f, 190.0f)) == Approx(180.0f).margin(1e-3));
    // The engine's own numbers for the turn that hung (issue 40, the
    // diagnostic's line): facing 66.1 deg, its target at 65.1.
    CHECK(std::abs(*Turn(66.1f, 65.1f)) == Approx(1.0f).margin(1e-3));
}

TEST_CASE("the angle wraps at 180 degrees: the short way round, across the seam", "[facing]")
{
    // Two bearings two degrees apart with the seam between them are two
    // degrees apart, not 358.
    CHECK(*Turn(179.0f, -179.0f) == Approx(2.0f).margin(1e-3));
    CHECK(*Turn(-179.0f, 179.0f) == Approx(-2.0f).margin(1e-3));
    CHECK(*Turn(359.5f, 0.5f) == Approx(1.0f).margin(1e-3));
    CHECK(*Turn(-350.0f, 13.0f) == Approx(3.0f).margin(1e-3));
    // And never more than half a turn, whichever way.
    for (float facing = -720.0f; facing <= 720.0f; facing += 37.0f)
    {
        for (float toward = -720.0f; toward <= 720.0f; toward += 41.0f)
        {
            const std::optional<float> turn = Turn(facing, toward);
            REQUIRE(turn);
            CHECK(std::abs(*turn) <= 180.0f + 1e-3f);
        }
    }
}

TEST_CASE("the angle is of the directions, whatever their lengths", "[facing]")
{
    // A facing as the engine keeps it, a length of one; the way to a target
    // 16.83 m off, as it comes from two positions.
    const Way facing = At(66.1f);
    const Way toward = At(65.1f);
    const std::optional<float> near = TurnDegrees(facing.x, facing.y, toward.x, toward.y);
    const std::optional<float> far = TurnDegrees(facing.x, facing.y, toward.x * 16.83f, toward.y * 16.83f);
    const std::optional<float> both =
        TurnDegrees(facing.x * 0.25f, facing.y * 0.25f, toward.x * 40.0f, toward.y * 40.0f);
    REQUIRE((near && far && both));
    CHECK(*far == Approx(*near).margin(1e-4));
    CHECK(*both == Approx(*near).margin(1e-4));
}

TEST_CASE("there is no angle without a facing, or to a target where its creature stands", "[facing]")
{
    CHECK_FALSE(TurnDegrees(0.0f, 0.0f, 1.0f, 0.0f)); // a facing never read
    CHECK_FALSE(TurnDegrees(1.0f, 0.0f, 0.0f, 0.0f)); // a target on the same spot
    CHECK_FALSE(TurnDegrees(1.0f, 0.0f, 0.0004f, 0.0004f));
    CHECK(TurnDegrees(1.0f, 0.0f, 0.002f, 0.0f)); // two millimetres off is a direction
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(TurnDegrees(nan, 0.0f, 1.0f, 0.0f));
    CHECK_FALSE(TurnDegrees(1.0f, nan, 1.0f, 0.0f));
    CHECK_FALSE(TurnDegrees(1.0f, 0.0f, nan, 0.0f));
    CHECK_FALSE(TurnDegrees(1.0f, 0.0f, 1.0f, nan));
    CHECK_FALSE(TurnDegrees(inf, 0.0f, 1.0f, 0.0f));
    CHECK_FALSE(TurnDegrees(1.0f, 0.0f, 1.0f, inf));
}

TEST_CASE("under five degrees off its target a creature is faced at it exactly; at five and over it is left",
          "[facing]")
{
    // The rule's number, and the engine's beside it: the edge it is for is
    // inside the rule, with room to spare either side of it.
    CHECK(kFacedExactlyWithinDegrees == 5.0f);
    CHECK(kEngineFacesWithinDegrees == 1.0f);
    CHECK(kFacesAlreadyDegrees < kEngineFacesWithinDegrees);
    CHECK(kEngineFacesWithinDegrees < kFacedExactlyWithinDegrees);
    // The edge, either way round: the turn that hung began at -1.00, and the
    // smallest that ended at -1.02.
    for (const float edge : {1.0f, 1.0001f, 1.02f, 0.99f})
    {
        CHECK(FacingFor(edge) == Facing::Face);
        CHECK(FacingFor(-edge) == Facing::Face);
    }
    CHECK(FacingFor(0.5f) == Facing::Face);
    CHECK(FacingFor(2.97f) == Facing::Face);
    CHECK(FacingFor(4.99f) == Facing::Face);
    CHECK(FacingFor(-4.99f) == Facing::Face);
    // At the rule's angle and over it: the engine's own turn, untouched.
    CHECK(FacingFor(5.0f) == Facing::Leave);
    CHECK(FacingFor(-5.0f) == Facing::Leave);
    CHECK(FacingFor(5.01f) == Facing::Leave);
    CHECK(FacingFor(90.0f) == Facing::Leave);
    CHECK(FacingFor(-180.0f) == Facing::Leave);
    CHECK(FacingFor(180.0f) == Facing::Leave);
}

TEST_CASE("one that faces its target already is not turned", "[facing]")
{
    CHECK(FacingFor(0.0f) == Facing::Faces);
    CHECK(FacingFor(0.01f) == Facing::Faces);
    CHECK(FacingFor(-0.04f) == Facing::Faces);
    // From there up there is something to set straight.
    CHECK(FacingFor(0.05f) == Facing::Face);
    CHECK(FacingFor(-0.06f) == Facing::Face);
    // One faced exactly reads as facing: what is set is not set again.
    const Way toward = At(65.1f);
    const std::optional<float> after = TurnDegrees(toward.x, toward.y, toward.x * 16.83f, toward.y * 16.83f);
    REQUIRE(after);
    CHECK(FacingFor(*after) == Facing::Faces);
}

TEST_CASE("a measure that is no number turns nobody", "[facing]")
{
    CHECK(FacingFor(std::numeric_limits<float>::quiet_NaN()) == Facing::Leave);
    CHECK(FacingFor(std::numeric_limits<float>::infinity()) == Facing::Leave);
    CHECK(FacingFor(-std::numeric_limits<float>::infinity()) == Facing::Leave);
}

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

TEST_CASE("the player's attack at one she stands a degree off: faced at it first, said, and counted for the fight's "
          "end",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.FaceOff(kHero, kGenlock, 1.0f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    REQUIRE(w.facedFirst.size() == 1);
    CHECK(w.facedFirst.front() == std::pair{kHero, kGenlock});
    // What the command finds as it begins: its creature facing its target,
    // inside the engine's degree, so that it asks for no turn.
    CHECK(w.foundOff.at(kHero) < kFacesAlreadyDegrees);
    CHECK(Logged(w, "Hero stands 1.00 deg off Genlock as its attack is given: faced at it exactly (the engine's turn "
                    "to face does not begin at a degree, and its attack would wait for one: issue 40)"));
    // Said once as the fight ends.
    CHECK(Lines(w, kCounted) == 0);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.inCombat = false;
    w.mode = Mode::Explore;
    w.Finish(kHero);
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(Lines(w, "in this fight 1 attack or ability was given to a creature under 5 deg off its target, which was "
                   "faced at it exactly first (issue 40)") == 1);
}

TEST_CASE("the other way round is the same: a degree off to either side", "[facing]")
{
    World w = Placed();
    w.StartFight();
    w.FaceOff(kHero, kGenlock, -1.0f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.size() == 1);
    CHECK(w.foundOff.at(kHero) < kFacesAlreadyDegrees);
    CHECK(Logged(w, "Hero stands 1.00 deg off Genlock as its attack is given"));
}

TEST_CASE("five degrees off or more is left to the engine's own turn: not faced, nothing said", "[facing]")
{
    for (const float off : {5.5f, -5.5f, 30.0f, 179.0f})
    {
        World w = Placed();
        w.StartFight();
        w.FaceOff(kHero, kGenlock, off);
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
        INFO(off << " degrees off");
        CHECK(w.facedFirst.empty());
        // As it stood: the engine's turn takes it from there.
        CHECK(w.foundOff.at(kHero) == Approx(std::abs(off)).margin(1e-2));
        CHECK_FALSE(Logged(w, kFaced));
    }
}

TEST_CASE("one facing its target already is given its attack as it stands, and is not counted", "[facing]")
{
    World w = Placed();
    w.StartFight();
    w.FaceOff(kHero, kGenlock, 0.0f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK_FALSE(Logged(w, kFaced));
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.inCombat = false;
    w.mode = Mode::Explore;
    w.Finish(kHero);
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
    // A fight in which nobody was faced says nothing of it.
    CHECK(Lines(w, kCounted) == 0);
}

TEST_CASE("an ability aimed at another is as an attack: faced first; one aimed at nobody, or at its user, is not",
          "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.FaceOff(kHero, kGenlock, 2.0f);
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
        CHECK(w.foundOff.at(kHero) < kFacesAlreadyDegrees);
        CHECK(Logged(w, "Hero stands 2.00 deg off Genlock as its ability is given: faced at it exactly (the engine's "
                        "turn to face does not begin at a degree, and its ability would wait for one: issue 40)"));
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

TEST_CASE("a command that turns its creature to nobody is not the rule's, whatever angle comes with it", "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    // A walk to someone, the angle given all the same (the gate measures for
    // an attack and an ability only; the machine does not lean on that).
    CommandRequest walk;
    walk.object = kHero;
    walk.type = CommandType::MoveToObject;
    walk.source = Source::Player;
    walk.inCombat = true;
    walk.target = kGenlock;
    walk.turnDegrees = 1.0f;
    const Verdict verdict = w.machine.OnCommand(walk);
    REQUIRE(verdict == Verdict::Allow);
    CHECK_FALSE(w.machine.FaceFirst(walk, verdict));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("a command refused or kept is not given, and its creature is not turned for it", "[facing]")
{
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CommandRequest attack;
    attack.object = kGenlock;
    attack.type = CommandType::Attack;
    attack.source = Source::Script;
    attack.inCombat = true;
    attack.target = kHero;
    attack.turnDegrees = 1.0f;
    // Out of its turn: refused.
    const Verdict refused = w.machine.OnCommand(attack);
    REQUIRE(refused == Verdict::Block);
    CHECK_FALSE(w.machine.FaceFirst(attack, refused));
    // The same command, were it kept to be given again: not faced now; it is
    // measured again when it is given.
    CHECK_FALSE(w.machine.FaceFirst(attack, Verdict::Hold));
    CHECK_FALSE(Logged(w, kFaced));
    // Let through, it is; and so is a cast the engine conjures, held.
    attack.object = kHero;
    attack.target = kGenlock;
    attack.source = Source::Player;
    CHECK(w.machine.FaceFirst(attack, Verdict::Allow));
    CHECK(w.machine.FaceFirst(attack, Verdict::Conjure));
    CHECK(Lines(w, kFaced) == 2);
}

TEST_CASE("with no measure of the angle nobody is faced", "[facing]")
{
    World w = Placed();
    w.StartFight();
    // No facing read of her: the stand-in has no angle to give, as the gate
    // has none off the scripts' thread.
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK_FALSE(w.foundOff.contains(kHero));
    CHECK_FALSE(Logged(w, kFaced));
}

TEST_CASE("with no fight on the game is as it was: nobody is faced", "[facing]")
{
    World w = Placed();
    // Before any fight.
    w.FaceOff(kHero, kGenlock, 1.0f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kHero) == Approx(1.0f).margin(1e-3));
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
    w.FaceOff(kHero, kGenlock, 1.0f);
    REQUIRE(w.Attack(kHero, kGenlock, Source::Player) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kHero) == Approx(1.0f).margin(1e-3));
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
    // The fight's own creature, still in its order, a degree off whom the
    // scene has it strike.
    REQUIRE(w.machine.Order().Find(kGenlock) != nullptr);
    w.FaceOff(kGenlock, kHero, 1.0f);
    REQUIRE(w.Attack(kGenlock, kHero, Source::Script) == Verdict::Allow);
    CHECK(w.facedFirst.empty());
    CHECK(w.foundOff.at(kGenlock) == Approx(1.0f).margin(1e-3));
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
    w.FaceOff(0x300, kHero, 1.0f);
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
            w.FaceOff(kGenlock, kHero, -1.5f);
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
    CHECK(w.foundOff.at(kGenlock) < kFacesAlreadyDegrees);
    CHECK(Logged(w, "Genlock stands 1.50 deg off Hero as its attack is given: faced at it exactly"));
}

// The hang the rule is for (issue 40, 2026-10-05, Ostagar): a Hurlock's
// attack kept from the turn before was given back with its creature a degree
// off its unmoved target and nothing turning, and stood at its turn to face
// until the action limit.
TEST_CASE("a kept attack given back to a creature a degree off its unmoved target finds it facing", "[facing][time]")
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
    // It stands where its last turn at this target stopped: a degree short.
    w.FaceOff(kGenlock, kHero, -1.0f);
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
    CHECK(w.foundOff.at(kGenlock) < kFacesAlreadyDegrees);
    CHECK(Logged(w, "Genlock stands 1.00 deg off Hero as its attack is given: faced at it exactly"));
}

TEST_CASE("the count is the fight's own: the next fight starts from none", "[facing]")
{
    // Three swings of a second in one turn, of a bar of four: after
    // each she is left a little further off, as a target that stepped aside
    // would leave her.
    World w = Placed();
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (int given = 0; given < 3; ++given)
    {
        w.FaceOff(kHero, kGenlock, 1.0f + static_cast<float>(given));
        REQUIRE(w.Attack(kHero, kGenlock, Source::Player, NoAttack::None, 0.0f, 1.0f) == Verdict::Allow);
        w.Finish(kHero);
        w.RunFor(600);
    }
    CHECK(w.facedFirst.size() == 3);
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.inCombat = false;
    w.mode = Mode::Explore;
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(Lines(w, "in this fight 3 attacks and abilities were given to a creature under 5 deg off its target, which "
                   "was faced at it exactly first (issue 40)") == 1);
    // Another fight, with nobody faced in it: nothing said of the last one's.
    w.log.clear();
    w.Get(kGenlock).down = false;
    w.Get(kHurlock).down = false;
    w.inCombat = true;
    w.StartFight();
    REQUIRE(w.machine.Active());
    w.Get(kGenlock).down = true;
    w.Get(kHurlock).down = true;
    w.inCombat = false;
    w.mode = Mode::Explore;
    w.RunFor(3000);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(Lines(w, kCounted) == 0);
}
