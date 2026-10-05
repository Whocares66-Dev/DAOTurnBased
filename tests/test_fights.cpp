// Random fights, and what must hold in every one of them. SkyrimFollowerTactics'
// rule (its dev/PRINCIPLES.md): random sequences, with something that knows
// the answer. Here the answer is the design's promises (dev/DESIGN.md),
// checked on every frame and every command:
//
//   1. the gate: while a fight holds the world, only the combatant whose turn
//      it is may start anything of its own; with none held, everything goes.
//      The fight holds what the game has in combat: one it does not, that is
//      not of the party, is the game's, and what its script has it do goes
//      through in anyone's turn, always (issue 43);
//   2. only that combatant, and those the game's so, have anything of their
//      own running;
//   3. the game is paused exactly when a turn waits on the player or between
//      turns, and runs otherwise;
//   4. nobody walks further in a turn than its movement, give or take the
//      frame it is stopped in (with a turn a bar of time, than the bar would
//      walk as the turn began);
//   5. nobody spends two actions in a turn, unless stopped short of the first
//      and given it back (with time: no action begins with no time left);
//   6. each combatant starts at most one turn a round;
//   7. an AI's turn ends, and a fight ends when no enemy is left fighting;
//   8. every effect names who it is for, the AI is told to choose only on its
//      own turn, and only party members are selected;
//   9. the current combatant's wait is never let through, and a wait takes its
//      own length of the bar or nothing: only an AI's on its own turn, with a
//      turn a bar of time, and only one that fits what is left (issue 31);
//  10. with a turn a bar of time, a turn begins with time rolled over only
//      if its creature's last turn was ended by its actor: the player's End,
//      an AI that was done or waited (issue 29). A turn skipped, lost or
//      ended for its creature rolls nothing over, and every turn ends for a
//      reason that has its class (core/TurnEnd.h);
//  11. a hostile that has noticed nobody to fight is never told to choose:
//      its turn is skipped after its beat, not before, and never waited out
//      on its AI (issue 29);
//  12. nobody is taken out of a fight while the engine's perception keeps it
//      there (an enemy the party perceives or that perceives the party, an
//      ally that perceives an enemy), however far it has gone; and nobody
//      the scan's rules do not find fighting is brought into one (issue 29);
//  13. a creature an effect's command holds as its turn comes waits no longer
//      than a turn's worth of time on it, and, let go before, has its turn
//      with the time the hold ran gone from its bar (issue 30); one that has
//      noticed nobody is not held, and is from the moment it notices;
//  14. a turn an AI plays is over within max_turn_ms of the running game's
//      time, whatever it is doing, and is ended for that only then, never a
//      turn the player plays; no count of its commands ends a turn (issue
//      38);
//  15. as the fight lets the world go, whoever it held with no turn to come
//      is told to choose, once, if it still lives: everyone gone from the
//      fight alive, and everyone in combat that the gate turned away outside
//      the order; no party member is told then but one turned away as combat
//      began, with no fight following (issue 43);
//  16. a creature that is not of the party, out of the game's combat and with
//      nobody to fight, has no turn: it is never told to choose, the turn it
//      is found so in ends "out of combat" within a frame or two (the beat
//      that skips an unaware hostile's turn aside: 11), and only such a
//      creature's turn ends so (issue 43);
//  17. a creature the game has made inactive has no turn, whoever it is: it
//      is never told to choose and never waited on for an order, no turn is
//      begun for it, the turn it is made so in is over with that frame's
//      step, and only such a creature's turn ends "inactive" (issue 44).
//
// The arena is World.h's stand-in with the creatures made to act. An AI
// chooses when told to and whenever its command ends, as COMMAND_COMPLETE
// makes it; a command walks, then works; attacks kill now and then; others
// try to act out of turn; the player gives orders, gives orders nothing comes
// of, opens menus and ends turns; conversations interrupt; reinforcements
// arrive. Fixed seeds, so a failure is a fight to replay: its seed is in the
// message.
#include "World.h"

#include "core/Rules.h"
#include "core/TurnEnd.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

constexpr std::uint32_t kFrameMs = 16;
constexpr float kRunSpeed = 5.0f;  // meters a second, about a character running
constexpr int kMaxFrames = 200000; // 53 minutes of fight: far past any that works

double Unit(Rng &rng)
{
    return static_cast<double>(rng.Next() >> 11) * (1.0 / 9007199254740992.0);
}
bool Chance(Rng &rng, double p)
{
    return Unit(rng) < p;
}
float Between(Rng &rng, float lo, float hi)
{
    return lo + static_cast<float>(Unit(rng)) * (hi - lo);
}
template <class T> const T &Pick(Rng &rng, const std::vector<T> &from)
{
    return from[static_cast<std::size_t>(rng.Next() % from.size())];
}

// What a creature's command does in the arena: walk (an approach, or the
// move itself), then work (a swing, a cast), then end.
struct Task
{
    CommandType type = CommandType::Invalid;
    float walk = 0.0f;
    std::int32_t workMs = 0;
    ObjectId target = kNoObject;
    bool story = false;     // given during a conversation: the cutscene's, not a turn's
    bool gamesOwn = false;  // let through out of turn, its creature out of combat: the game's, not a turn's
    bool held = false;      // a condition's command: it lasts as long as the condition
    bool conjuring = false; // the engine's own spell, held in its conjure until released
    bool grab = false;      // an ability whose own effect holds its creature and its target, after it
};

// Counts over every fight, so a run shows the generator reached the states
// that matter rather than passing by never getting there.
struct Coverage
{
    int fights = 0;
    long long frames = 0;
    int turns = 0;
    int rounds = 0;
    int blockedOutOfTurn = 0;
    int movementCuts = 0;
    int refunds = 0;
    int dispatches = 0;
    int interludes = 0;
    int menus = 0;
    int joiners = 0;
    int timeouts = 0;
    int partyWipes = 0;
    int bledOut = 0;
    int tooFar = 0;
    int vanished = 0;
    int stunned = 0;      // creatures stunned or pinned
    int lostTurns = 0;    // turns taken by a condition
    int lostOnClock = 0;  // of them, with conditions on the game's clock
    int heldByEffect = 0; // turns lost to an effect's command that held past the bar (a turn's worth, as built)
    int heldFreed = 0;    // holds that let go within the turn, which went on with the rest
    int heldPaid = 0;     // of them, with time, those held from the turn's start: their time checked against the bar
    int speedChanges = 0; // the game's speed set for an AI's turn and back
    long long events = 0; // typed events heard
    int conditionsEnded = 0;
    int cooldownsHeld = 0;
    int cooldownsReleased = 0;
    int roundsAged = 0;
    int ownAged = 0; // a creature's own clock moved (Settings::ownClocks)
    // With a turn a bar of time.
    int kept = 0;            // actions that did not fit, kept for a later turn
    int landed = 0;          // kept actions given again and let through
    int conjured = 0;        // spells given to the engine to conjure, held
    int grabs = 0;           // abilities whose own effect held their creature and their target
    int conjureLanded = 0;   // of them, let complete as their time was paid
    int conjureCleared = 0;  // of them, cleared by the game before (rules_core, a knockdown)
    int wholeTurns = 0;      // turns spent whole on what a kept action owed
    int owed = 0;            // actions that ran and owed time to the next turn
    int choseAgain = 0;      // an AI told to choose again, its bar not spent
    int rolledOver = 0;      // turns that left time to the next
    int forfeited = 0;       // turns not ended by their actors, with time left that did not roll over
    int unawareTurns = 0;    // turns of hostiles that had noticed nobody, skipped
    int noticed = 0;         // of those hostiles, how many noticed: struck, or by themselves
    int noticedInBeat = 0;   // and of them, in the beat their turn was to be skipped after
    int ranOff = 0;          // combatants gone where the scan's rules for who fights no longer find them
    int stayedAfar = 0;      // of them, kept in the fight by the engine's perception
    int leftUnperceived = 0; // and out of it, neither perceiving the other
    int cameBack = 0;        // found fighting again
    int delays = 0;          // a party member's turn put off
    int delayedTurns = 0;    // and taken at the round's end, as it was
    int unplayed = 0;        // an action over at once, its time given back
    int walkedOnAction = 0;  // a walk past the bar took its action's time
    int arrivals = 0;        // an action charged as its creature got there
    int heldShares = 0;      // a hold shorter than a turn took its share of a bar
    int approaches = 0;      // a kept attack's creature walked to its target
    int targetsGone = 0;     // a kept action not given, its target gone
    int turned = 0;          // a kept attack given at another in its reach, its target gone
    int waitsPaid = 0;       // an AI's wait taken of its bar, its length (issue 31)
    int waitsTooLong = 0;    // an AI's wait longer than what was left: its turn ended
    int turnLimits = 0;      // a turn an AI played ended at max_turn_ms (issue 38)
    int turnedAway = 0;      // commands the gate turned away of creatures outside the order (issue 43)
    int toldTurnedAway = 0;  // those turned away so, or gone from the fight alive, that lived as it let go: each told
    int gamesOwn = 0;        // commands let through out of turn: their creatures out of combat, the game's own
    int noTurns = 0;         // turns ended "out of combat": their creatures the game's, with no turn (issue 43)
    int wentUnder = 0;       // creatures the game made inactive for a while (issue 44)
    int cameUp = 0;          // and made active again
    int inactiveTurns = 0;   // turns ended "inactive": passed over as they came, or ended as their creature went
    int inactiveMidTurn = 0; // of them, those of a creature made inactive in its own turn
    int inactiveQueued = 0;  // and those of one with something queued on it that the engine did not run
};

class Arena
{
  public:
    // strays: the fight has hostiles that have not noticed the party, and
    // combatants that go where the scan's own rules no longer find them
    // fighting (issue 29). A choice of the test's, not a draw, and all of it
    // drawn on a stream of its own: a fight without them is draw for draw the
    // fight it was before there were any, and what the eighty default fights
    // reach, which the floors and the mutants were set against, is as it was.
    // With them in three fights of ten of the default eighty, the fights
    // took other courses and one floor was no longer reached (an action over
    // at once, given back: 2 where 8 had been).
    Arena(std::uint64_t seed, Coverage &coverage, TurnClock clock = TurnClock::Actions, bool strays = false)
        : rng_(seed), reach_(seed ^ 0x22EAC4ull), waits_(seed ^ 0x3A175ull), notice_(seed ^ 0x29A11ull),
          asks_(seed ^ 0x45C5ull), burrows_(seed ^ 0x44B0ull), settings_(RandomSettings(rng_, clock)),
          world_(settings_, RandomFight(rng_, clock), seed), coverage_(coverage), runners_(strays)
    {
        // The engine's perception says of these that they perceive nobody
        // to fight; the party perceives them.
        for (Creature &c : world_.creatures)
        {
            if (strays && c.side == Side::Enemy && Chance(notice_, 0.5))
            {
                c.perception = Unnoticing();
            }
        }
        world_.record = false;
        world_.machine.SetLogLevel(LogLevel::Info); // the turns, for a failure's message
        world_.onPoke = [this](ObjectId id) {
            // One the gate turned away, told as the fight lets the world go
            // (15): the command it then asks for is its own again. The fallen
            // and the gone the game layer passes over. With no draw: nobody
            // was told so before there was such a telling, and a fight is
            // draw for draw the fight it was.
            if (!world_.machine.Active() && owed_.contains(id))
            {
                if (const Creature &c = world_.Get(id); c.exists && !c.down)
                {
                    Must(world_.Order(id, CommandType::Attack, Source::Script) == Verdict::Allow,
                         "one told the fight is over had its command refused");
                    world_.Finish(id);
                }
                return;
            }
            Decide(id, false);
        };
        world_.onEffect = [this](const Effect &e) { Saw(e); };
        // A kept attack whose target is gone is turned on one of those the
        // machine named for it, if the game layer finds one in its creature's
        // reach (issue 22). Each must be of the other side, and it is turned
        // only as its creature's turn begins. Whether one is in reach and in
        // sight is drawn on a stream of its own, so that a fight in which no
        // attack is turned is the fight it was before any could be.
        world_.onMeasure = [this](ObjectId attacker, ObjectId other) {
            const Creature &to = world_.Get(other);
            Must(Opposed(world_.Get(attacker).side, to.side), "a kept attack offered one of its creature's own side");
            Must(attacker == world_.Current(), "a kept attack turned on another out of its creature's turn");
            Foe foe;
            foe.id = other;
            foe.standing = to.exists && !to.down && !to.inactive; // nor one out of the world (issue 44)
            foe.hostile = true;
            foe.seen = Chance(reach_, 0.8);
            foe.meters = Between(reach_, 0.0f, 5.0f);
            foe.inReach = foe.meters <= world_.reach;
            return foe;
        };
        // A kept action given again as its creature's turn begins: it walks
        // a little (its target may have stepped away), then works.
        // The walk to a kept attack's target, given by the game layer: now
        // and then it cannot be made, and now and then it is longer than
        // was paid for.
        world_.onApproach = [this](ObjectId id) {
            if (Chance(rng_, 0.1))
            {
                return;
            }
            if (world_.Order(id, CommandType::MoveToObject, Source::Script) != Verdict::Allow)
            {
                return;
            }
            ++coverage_.approaches;
            Task task{CommandType::MoveToObject};
            task.walk = Between(rng_, 0.5f, 8.0f);
            tasks_[id] = task;
        };
        world_.onConjureReleased = [this](ObjectId id) {
            const auto found = tasks_.find(id);
            Must(found != tasks_.end() && found->second.conjuring, "a conjure let complete that was not held");
            Must(id == world_.Current(), "a conjure let complete out of its caster's turn");
            found->second.conjuring = false;
            ++coverage_.conjureLanded;
        };
        world_.onReleased = [this](ObjectId id, const CommandRequest &request, Verdict verdict) {
            if (verdict != Verdict::Allow)
            {
                return;
            }
            ++coverage_.landed;
            Task task{request.type};
            task.walk = Chance(rng_, 0.5) ? 0.0f : Between(rng_, 0.5f, 6.0f);
            task.workMs = 600;
            tasks_[id] = task;
        };
        world_.machine.SetEventSink([this](const TurnEvent &e) { Heard(e); });
        // The machine's last words, for a failure's message.
        world_.onLog = [this](std::string_view line) {
            if (echo)
            {
                std::printf("%7u %.*s\n", world_.now, static_cast<int>(line.size()), line.data());
            }
            if (line.find("the action it was moving for is kept") != std::string_view::npos)
            {
                ++coverage_.refunds;
            }
            if (line.find("took too long") != std::string_view::npos)
            {
                ++coverage_.timeouts;
            }
            if (line.find("'s turn is still going after ") != std::string_view::npos)
            {
                // Ended at its time limit (14): an AI's, and not before the
                // limit, by the arena's own count of the turn's frames.
                ++coverage_.turnLimits;
                FollowTurn();
                Must(!PlayerControls(world_.Current()), "a turn the player plays ended at max_turn_ms");
                Must(turnFromMs_ &&
                         turnRunMs_ - *turnFromMs_ + 4 * kFrameMs >= static_cast<std::uint32_t>(settings_.maxTurnMs),
                     "an AI's turn ended as too long before max_turn_ms");
            }
            if (line.find("is held by an effect's command past ") != std::string_view::npos)
            {
                ++coverage_.heldByEffect;
                // Lost only once the hold has outlasted the bar (13).
                if (settings_.turnClock == TurnClock::Time)
                {
                    Must(world_.machine.View().timeLeftMs == 0, "a turn lost to a hold with time left on its bar");
                }
            }
            // A hold let go within the turn (13): with time, what it ran is
            // gone from the bar, whatever else the turn paid as it began (the
            // debt of a kept action). On the machine's clock, which runs at
            // the fight's speed; less the frames between the turn's beginning
            // and the first look at its creature.
            if (line.find(" is free of the effect's command after ") != std::string_view::npos)
            {
                ++coverage_.heldFreed;
                if (settings_.turnClock == TurnClock::Time && std::exchange(heldPaidDue_, false))
                {
                    const TurnView view = world_.machine.View();
                    const auto ran = static_cast<std::uint32_t>(
                        std::lround(static_cast<float>(heldAtStartMs_) * settings_.fightSpeed));
                    const auto slack = static_cast<std::uint32_t>(std::lround(4.0f * kFrameMs * settings_.fightSpeed));
                    Must(view.timeMs - view.timeLeftMs + slack >= ran,
                         "a hold let go took less of the bar than it ran");
                    ++coverage_.heldPaid;
                }
            }
            // At an effect's command no condition names as the machine begins
            // its turn: the hold runs on the turn's clock, and the turn is
            // lost only once it has run a turn's worth.
            if (line.starts_with("round ") && line.find("'s turn (") != std::string_view::npos)
            {
                FollowTurn();
                turnMeters_ = world_.machine.View().movementLeft;
                const auto found = tasks_.find(world_.Current());
                heldAtStart_ = found != tasks_.end() && found->second.type == CommandType::ExecuteEffect &&
                               world_.Get(world_.Current()).condition == Condition::None;
                heldAtStartMs_ = 0;
                heldPaidDue_ = heldAtStart_; // until the machine says it free: a frame or two after it is
                turnBeganAtMs_ = turnRunMs_; // a padded round's wait came before
                turnFromMs_ = turnRunMs_;
            }
            if (line.find(" m to walk with ") != std::string_view::npos)
            {
                ++coverage_.tooFar;
            }
            if (line.find("it lands on its next turn") != std::string_view::npos)
            {
                ++coverage_.kept;
                // Now and then its target is gone by the time it would land.
                if (Chance(rng_, 0.1))
                {
                    world_.targetGone.insert(world_.Current());
                }
            }
            if (line.find("'s pending attack is turned on ") != std::string_view::npos)
            {
                ++coverage_.turned;
            }
            // (Gone, or out of the world: one the game has made inactive
            // since the action was kept, issue 44.)
            if (line.find("is dropped: its target is ") != std::string_view::npos)
            {
                ++coverage_.targetsGone;
                // What it owed is back in the bar: the turn may walk that
                // much further than it could as it began.
                turnMeters_ = std::max(turnMeters_, walked_ + world_.machine.View().movementLeft);
            }
            if (line.find("'s turn is spent on its pending") != std::string_view::npos)
            {
                ++coverage_.wholeTurns;
            }
            if (line.find("over at once: it did nothing") != std::string_view::npos)
            {
                ++coverage_.unplayed;
            }
            if (line.find("'s walk runs past its bar") != std::string_view::npos)
            {
                ++coverage_.walkedOnAction;
            }
            if (line.find(" s taken now") != std::string_view::npos)
            {
                ++coverage_.arrivals;
            }
            if (line.find("'s condition takes ") != std::string_view::npos)
            {
                ++coverage_.heldShares;
            }
            if (line.find(" runs now and owes ") != std::string_view::npos)
            {
                ++coverage_.owed;
            }
            if (line.find(" chooses again: ") != std::string_view::npos)
            {
                ++coverage_.choseAgain;
            }
            if (line.find("'s wait of ") != std::string_view::npos && line.ends_with(" it has left: its turn ends"))
            {
                ++coverage_.waitsTooLong;
            }
            if (line.ends_with(" s rolls over") && !line.ends_with("; 0.00 s rolls over"))
            {
                ++coverage_.rolledOver;
            }
            if (line.find("; nothing rolls over, its actor not having ended it") != std::string_view::npos)
            {
                ++coverage_.forfeited;
            }
            // An unaware hostile's beat begins: its turn is skipped when it
            // is out, not before (11).
            if (line.find(": unaware, its AI is not told") != std::string_view::npos)
            {
                FollowTurn();
                beatOf_ = world_.Current();
                beatFromMs_ = turnRunMs_;
                // One that has noticed nobody is not held, whatever the
                // engine has it at: its beat, and its turn skipped (13).
                heldAtStart_ = false;
                heldPaidDue_ = false;
            }
            if (line.find(" has noticed someone to fight") != std::string_view::npos)
            {
                ++coverage_.noticedInBeat;
                // Noticing with an effect's command still on it, it is held
                // from now: the beat before took nothing of its bar (13).
                FollowTurn();
                const auto found = tasks_.find(world_.Current());
                heldAtStart_ = found != tasks_.end() && found->second.type == CommandType::ExecuteEffect &&
                               world_.Get(world_.Current()).condition == Condition::None;
                heldAtStartMs_ = 0;
                heldPaidDue_ = heldAtStart_;
                turnBeganAtMs_ = turnRunMs_;
            }
            if (line.find(", and stays in the fight: ") != std::string_view::npos)
            {
                ++coverage_.stayedAfar;
            }
            recent_.emplace_back(line);
            if (recent_.size() > 40)
            {
                recent_.pop_front();
            }
        };
    }

    void Play()
    {
        ++coverage_.fights;
        world_.mode = Mode::Combat;
        bool held = false;
        int frame = 0;
        for (; frame < kMaxFrames; ++frame)
        {
            Tick();
            grabbedBefore_ = world_.machine.OwnEffectTarget();
            world_.Frame(kFrameMs);
            Check();
            held = held || world_.machine.Active();
            if (held && world_.machine.GetPhase() == Phase::Idle && world_.mode != Mode::Combat &&
                world_.mode != Mode::Gui)
            {
                break;
            }
        }
        coverage_.frames += frame;
        Must(held, "the fight was never turn-based");
        Must(frame < kMaxFrames, "the fight never ended");
    }

    bool echo = false; // every line the machine logs, to the standard output

  private:
    static Settings RandomSettings(Rng &rng, TurnClock clock)
    {
        Settings s;
        s.movementRule = Chance(rng, 0.5) ? MovementRule::Speed : MovementRule::Constant;
        s.movementMeters = Between(rng, 4.0f, 12.0f);
        s.movementPerDexterity = Chance(rng, 0.3) ? 0.1f : 0.0f;
        s.partyControl = Chance(rng, 0.3) ? PartyControl::Tactics : PartyControl::Manual;
        s.endTurnWhenActionUsed = Chance(rng, 0.3);
        s.roundMinSeconds = Chance(rng, 0.3) ? Between(rng, 1.0f, 6.0f) : 0.0f;
        s.settleMs = static_cast<int>(Between(rng, 0.0f, 800.0f));
        s.aiThinkMs = static_cast<int>(Between(rng, 200.0f, 2000.0f));
        s.aiFollowUpMs = static_cast<int>(Between(rng, 0.0f, 1000.0f));
        s.maxActionMs = Chance(rng, 0.3) ? 2000 : 12000;
        // The draw max_commands_per_turn took until that setting went (issue
        // 38: a cap of two in three fights of ten), so that what is drawn
        // after it is what it was. In those three a turn an AI plays now has
        // three seconds, where the default is thirty: its time limit ends
        // turns there among everything else that goes on in a fight (14).
        s.maxTurnMs = Chance(rng, 0.3) ? 3000 : 30000;
        s.floaties = Chance(rng, 0.5);
        s.scriptEvents = true; // the round check reads the turn-start events
        s.cameraFollow = Chance(rng, 0.2);
        s.orderRunMs = Chance(rng, 0.5) ? 500 : 0;
        s.pauseOnPlayerTurn = Chance(rng, 0.5);
        s.effectSecondsPerRound = Chance(rng, 0.2) ? 0.0f : 6.0f;
        s.conditionTurns = Chance(rng, 0.2) ? 0 : (Chance(rng, 0.7) ? 1 : 2);
        // Faster only: the arena's bounds on how long a turn may take are
        // in real milliseconds, and a slower game stretches every one of
        // the machine's waits past them.
        s.fightSpeed = Chance(rng, 0.3) ? 2.0f : 1.0f;
        // Drawn after the rest, so that a fight as built is the one it was.
        s.turnClock = clock;
        if (clock == TurnClock::Time)
        {
            s.secondsPerTurn = Chance(rng, 0.7) ? 4.0f : Between(rng, 2.0f, 6.0f);
            s.freeStepMeters = Chance(rng, 0.8) ? 1.0f : Between(rng, 0.0f, 2.0f);
            s.paceMps = kRunSpeed;
            // (A draw kept from when a fight had two speeds, so that the
            // fights after it are the ones they were.)
            if (Chance(rng, 0.3))
            {
                s.fightSpeed = 2.0f;
            }
        }
        return s;
    }

    static std::vector<Creature> RandomFight(Rng &rng, TurnClock clock)
    {
        std::vector<Creature> fight;
        const auto add = [&](ObjectId id, Side side, bool hero) {
            Creature c = Make(id, side, hero, L"c" + std::to_wstring(id), Between(rng, 8.0f, 40.0f));
            c.speed = Chance(rng, 0.6) ? 1.0f : Between(rng, 0.6f, 1.4f);
            c.position = {Between(rng, -20.0f, 20.0f), Between(rng, -20.0f, 20.0f), 0.0f};
            fight.push_back(std::move(c));
        };
        const int party = 1 + static_cast<int>(rng.Next() % 4);
        const int enemies = 1 + static_cast<int>(rng.Next() % 6);
        const int allies = static_cast<int>(rng.Next() % 3);
        for (int i = 0; i < party; ++i)
        {
            add(0x100 + static_cast<ObjectId>(i), Side::Party, i == 0);
        }
        for (int i = 0; i < enemies; ++i)
        {
            add(0x200 + static_cast<ObjectId>(i), Side::Enemy, false);
        }
        for (int i = 0; i < allies; ++i)
        {
            add(0x300 + static_cast<ObjectId>(i), Side::Ally, false);
        }
        // Combat Tactics: a party member's own, an enemy's by its rank.
        if (clock == TurnClock::Time)
        {
            for (Creature &c : fight)
            {
                c.tactics = static_cast<int>(rng.Next() % 5);
                c.rank = 1 + static_cast<int>(rng.Next() % 5);
            }
        }
        return fight;
    }

    void Must(bool ok, const char *what) const
    {
        if (!ok)
        {
            std::string lines;
            for (const std::string &line : recent_)
            {
                lines += "\n    " + line;
            }
            FAIL(what << " (round " << world_.machine.Order().Round() << ", phase " << Name(world_.machine.GetPhase())
                      << ", current " << world_.Current() << "); the machine's last lines:" << lines);
        }
    }

    bool PlayerControls(ObjectId id) const
    {
        const Creature &c = world_.Get(id);
        return c.side == Side::Party && (settings_.partyControl == PartyControl::Manual || c.hero);
    }

    std::vector<ObjectId> Living(bool (*which)(const Creature &)) const
    {
        std::vector<ObjectId> ids;
        for (const Creature &c : world_.creatures)
        {
            if (!c.down && which(c))
            {
                ids.push_back(c.id);
            }
        }
        return ids;
    }
    static bool Anyone(const Creature &c)
    {
        return c.fighting;
    }

    // The turn now, by round and whose; a change starts the per-turn counts.
    void FollowTurn()
    {
        const auto key = std::pair{world_.machine.Order().Round(), world_.Current()};
        if (key != turn_)
        {
            turn_ = key;
            walked_ = 0.0f;
            actions_ = 0;
            clears_ = 0;
            interruptions_ = 0;
            turnRunMs_ = 0;
            turnFromMs_.reset();  // set as the machine begins the turn (onLog)
            turnMeters_ = 0.0f;   // with time, read as the machine begins the turn (onLog)
            heldAtStart_ = false; // judged as the machine begins the turn (onLog)
            heldAtStartMs_ = 0;
            heldPaidDue_ = false;
            turnBeganAtMs_ = 0;
            told_ = kNoObject;
        }
    }

    // A command put to the gate, with the gate's promises checked (1, 5).
    void Try(ObjectId id, CommandType type, Source source)
    {
        // One the game has made inactive is out of the world (17): its AI is
        // given no updates, the player has nobody to order, and nothing lands
        // on it but what Burrowing queues. With no draw: no fight had one
        // before there were any.
        if (world_.Get(id).inactive)
        {
            return;
        }
        FollowTurn();
        const bool active = world_.machine.Active();
        const ObjectId current = world_.Current();
        // An ability: one of a few, with a cooldown, now and then a sustained
        // one turned on or off, which costs nothing.
        const std::int32_t ability = static_cast<std::int32_t>(rng_.Next() % 4);
        const bool sustained = type == CommandType::UseAbility && Chance(rng_, 0.25);
        const bool on = Chance(rng_, 0.5);
        const float cooldown = Chance(rng_, 0.3) ? 0.0f : Between(rng_, 5.0f, 60.0f);
        const CommandKind kind = sustained ? CommandKind::Free : Classify(type);
        // An attack's walk to its target is chosen now: the game layer tells
        // the gate how far a player's attack must walk (src/game/Gate.cpp).
        const float approach = Chance(rng_, 0.4) ? 0.0f : Between(rng_, 0.5f, 16.0f);
        // Now and then the walk cannot be told (an order off the scripts'
        // thread): the attack walks until it is stopped, as an AI's does.
        // A move's path, as the gate measures it (path_check): its length.
        const float path = Between(rng_, 1.0f, 16.0f);
        const bool told = (type == CommandType::Attack || type == CommandType::MoveToLocation) &&
                          source == Source::Player && Chance(rng_, 0.7);
        const float walk = type == CommandType::Attack ? approach : path;
        const float left = told && active ? world_.machine.View().movementLeft : 0.0f;
        // With time, an attack's swing: a weapon's, or the machine's stand-in.
        // Now and then one longer than a turn (a ritual's conjuring), and an
        // item used, which is not kept but owes what does not fit.
        const bool timed = settings_.turnClock == TurnClock::Time;
        float seconds = 0.0f;
        bool item = false;
        if (timed && type == CommandType::Attack)
        {
            seconds = Chance(rng_, 0.08) ? Between(rng_, 4.0f, 10.0f)
                                         : (Chance(rng_, 0.7) ? Between(rng_, 1.0f, 2.6f) : 0.0f);
        }
        else if (timed && type == CommandType::UseAbility && !sustained && Chance(rng_, 0.25))
        {
            item = true;
            seconds = Between(rng_, 1.0f, 6.0f);
        }
        // A spell that conjures (Settings::engineConjure): one longer than
        // what is left goes to the engine at once, held in its conjure.
        bool conjures = false;
        if (timed && !item && type == CommandType::UseAbility && !sustained && Chance(rng_, 0.5))
        {
            conjures = true;
            seconds = Between(rng_, 2.5f, 7.0f);
        }
        // An ability aimed at an opponent whose own effect holds its creature
        // and its target after it (an ogre's grab, a spider's Overwhelm).
        ObjectId grabbed = kNoObject;
        if (timed && !item && !conjures && type == CommandType::UseAbility && !sustained && Chance(rng_, 0.2))
        {
            const Creature &self = world_.Get(id);
            const auto opponents = Living(self.side == Side::Enemy
                                              ? +[](const Creature &c) { return c.side != Side::Enemy; }
                                              : +[](const Creature &c) { return c.side == Side::Enemy && c.fighting; });
            if (!opponents.empty())
            {
                grabbed = Pick(rng_, opponents);
            }
        }
        CommandRequest grab;
        grab.object = id;
        grab.type = CommandType::UseAbility;
        grab.source = source;
        grab.inCombat = world_.InCombat(id);
        grab.ability = 90036;
        grab.cooldownSeconds = cooldown;
        grab.actionSeconds = 1.0f;
        grab.target = grabbed;
        CommandRequest spell;
        spell.object = id;
        spell.type = CommandType::UseAbility;
        spell.source = source;
        spell.inCombat = world_.InCombat(id);
        spell.ability = ability;
        spell.cooldownSeconds = cooldown;
        spell.abilityType = kAbilitySpell;
        spell.conjureAnim = 245;
        spell.conjureSeconds = 1.0f;
        spell.actionSeconds = seconds;
        // A wait's length, the command's own as the game layer reads it: the
        // AI's two (AI_DO_NOTHING_DELAY, 2 s, and its quick one, 1 s), and
        // now and then none read. Drawn on a stream of its own, so that a
        // fight of one action a turn, where a wait is a pass whatever its
        // length, is the fight it was before a wait had one (issue 31).
        const float wait = type != CommandType::Wait ? 0.0f
                           : Chance(waits_, 0.15)    ? 0.0f
                           : Chance(waits_, 0.7)     ? 2.0f
                                                     : 1.0f;
        const std::uint32_t timeLeft = active ? world_.machine.View().timeLeftMs : 0;
        const Verdict verdict =
            item                        ? world_.Order(id, CommandType::UseItem, source, std::nullopt, seconds)
            : conjures                  ? world_.Queue(spell)
            : grabbed != kNoObject      ? world_.Queue(grab)
            : type == CommandType::Wait ? world_.Wait(id, wait, source)
            : type == CommandType::UseAbility
                ? world_.UseAbility(id, source, ability, cooldown, sustained, on)
                : world_.Order(id, type, source, told ? std::optional{walk} : std::nullopt, seconds);
        if (type == CommandType::Wait && active)
        {
            // A wait takes its own length of its creature's bar, or nothing:
            // only an AI's on its own turn, with time, and only one that
            // fits what was left. Paid for or not, it is never let through.
            const std::uint32_t after = world_.machine.View().timeLeftMs;
            const auto waitMs = static_cast<std::uint32_t>(std::lround(wait * 1000.0f));
            const bool paid = after != timeLeft;
            Must(!paid || (timed && id == current && !PlayerControls(id)),
                 "a wait paid for that is not an AI's on its own turn, with time");
            Must(!paid || (waitMs > 0 && waitMs <= timeLeft && after == timeLeft - waitMs),
                 "a wait took other than its own length of the bar");
            Must(id != current || verdict == Verdict::Block, "the current combatant's wait let through");
            coverage_.waitsPaid += paid ? 1 : 0;
        }
        if (told && active && verdict == Verdict::Allow && walk > left + TurnMachine::kReachSlack &&
            id == world_.Current())
        {
            Must(false, "the player's order let through though it cannot reach this turn");
        }
        if (!active)
        {
            Must(verdict == Verdict::Allow, "the gate refused a command with no fight holding the world");
        }
        // The fight holds what the game has in combat (1): one it does not,
        // that is not of the party, is the game's, and what its script has
        // it do goes through, always, whoever's turn it is: it has no turn
        // of its own (16).
        const bool gamesOwn = active && source == Source::Script && !world_.InCombat(id) &&
                              world_.Get(id).side != Side::Party && kind != CommandKind::Involuntary;
        if (gamesOwn)
        {
            Must(verdict == Verdict::Allow, "one the game does not have in combat held out of its turn");
            ++coverage_.gamesOwn;
        }
        else if (active && verdict == Verdict::Allow && kind != CommandKind::Involuntary)
        {
            Must(id == current, "the gate let through a command out of turn");
        }
        if (active && verdict == Verdict::Block && id != current)
        {
            ++coverage_.blockedOutOfTurn;
            // Turned away outside the order (15): owed a telling as the
            // fight lets go, unless it joins the order first (Heard).
            if (world_.machine.Order().Find(id) == nullptr)
            {
                owed_.insert(id);
                ++coverage_.turnedAway;
            }
        }
        if (verdict == Verdict::Conjure)
        {
            Must(active && timed && conjures, "a spell given to the engine to conjure with no time to pay");
            Must(id == current, "a spell given to the engine to conjure out of turn");
            ++coverage_.conjured;
            Task task{CommandType::UseAbility};
            task.walk = Chance(rng_, 0.7) ? 0.0f : Between(rng_, 0.5f, 6.0f);
            task.workMs = 600; // its cast, once let complete
            task.conjuring = true;
            // At its conjure once it has walked into range: the turn waits.
            world_.Get(id).conjureHeld = task.walk <= 0.0f;
            tasks_[id] = task;
            return;
        }
        if (verdict != Verdict::Allow)
        {
            return;
        }
        if (active && kind == CommandKind::Action && timed)
        {
            Must(id != current || timeLeft > 0, "an action let through with no time left");
        }
        else if (active && kind == CommandKind::Action && !gamesOwn)
        {
            ++actions_;
            Must(actions_ <= 1 + clears_, "a second action in one turn");
        }
        Task task{item ? CommandType::UseItem : type};
        switch (type)
        {
        case CommandType::Attack:
            task.walk = approach;
            task.workMs = 600;
            break;
        case CommandType::UseAbility:
            task.walk = Chance(rng_, 0.7) ? 0.0f : Between(rng_, 0.5f, 6.0f);
            task.workMs = static_cast<std::int32_t>(Between(rng_, 500.0f, 4000.0f));
            break;
        case CommandType::MoveToLocation:
            task.walk = path;
            break;
        case CommandType::Drive:
            task.walk = Between(rng_, 1.0f, 16.0f);
            break;
        case CommandType::ExecuteEffect:
            // A knockdown, a stun: the victim gets up after a while, and may
            // still be at it when its own turn comes.
            task.workMs = static_cast<std::int32_t>(Between(rng_, 300.0f, 2500.0f));
            break;
        default:
            task.workMs = 400;
            break;
        }
        const Creature &self = world_.Get(id);
        const auto opponents =
            Living(self.side == Side::Enemy ? +[](const Creature &c) { return c.side != Side::Enemy; }
                                            : +[](const Creature &c) { return c.side == Side::Enemy && c.fighting; });
        if (!opponents.empty())
        {
            task.target = Pick(rng_, opponents);
        }
        task.story = world_.mode == Mode::Dialog;
        task.gamesOwn = gamesOwn;
        if (grabbed != kNoObject)
        {
            task.grab = true;
            task.target = grabbed;
            task.workMs = 400;
        }
        // What the engine puts in front of a spell held in its conjure (a
        // knockdown's effect) clears it, as the game does.
        ConjureBroken(id);
        tasks_[id] = task;
    }

    // An AI choosing, as rules_core does on COMMAND_COMPLETE (afterCommand)
    // and on the event the machine sends to start its turn.
    void Decide(ObjectId id, bool afterCommand)
    {
        const Creature &c = world_.Get(id);
        if (c.down || c.condition == Condition::Disabled || c.inactive)
        {
            // A stunned creature's AI chooses nothing (_AI_IsCommandValid);
            // an inactive one's is given no updates (script.ldf).
            return;
        }
        if (Unaware(c.side, c.perception))
        {
            return; // nor one that perceives nobody to fight: it has nobody to choose
        }
        if (c.side != Side::Party && !world_.InCombat(id))
        {
            // Nor one the game does not have in combat, enemy or ally: the
            // scripts run a creature's combat AI on a command's end only
            // while it is in combat (rules_core.nss,
            // EVENT_TYPE_COMMAND_COMPLETE). Until issue 43 an ally the party
            // had lost for good chose all the same here, and was held; let
            // through as the game's own, its attack was nobody's turn's.
            return;
        }
        if (PlayerControls(id))
        {
            // One not yet selected has its full AI run, which waits when it
            // has no enemy to choose: never let through on a turn the player
            // plays, where each wait put the turn back at its command for
            // good (issue 10). Asked without a draw; held, it changes nothing,
            // and takes none of the turn's time (issue 31): a second, the
            // quick wait that AI makes.
            if (afterCommand && world_.machine.Active() && world_.Current() == id)
            {
                const std::uint32_t bar = world_.machine.View().timeLeftMs;
                Must(world_.Wait(id, 1.0f) == Verdict::Block,
                     "its own AI's wait let through on a turn the player plays");
                Must(world_.machine.View().timeLeftMs == bar, "its own AI's wait took time of a turn the player plays");
            }
            // A controlled creature's AI only carries on attacking.
            if (afterCommand && Chance(rng_, 0.5))
            {
                Try(id, CommandType::Attack, Source::Script);
            }
            return;
        }
        const double roll = Unit(rng_);
        const double nothing = afterCommand ? 0.3 : 0.08;
        if (roll < nothing)
        {
            return;
        }
        if (roll < nothing + 0.45)
        {
            Try(id, CommandType::Attack, Source::Script);
        }
        else if (roll < nothing + 0.55)
        {
            Try(id, CommandType::UseAbility, Source::Script);
        }
        else if (roll < nothing + 0.75)
        {
            Try(id, CommandType::MoveToLocation, Source::Script);
        }
        else if (roll < nothing + 0.8)
        {
            Try(id, CommandType::SwitchWeaponSets, Source::Script);
        }
        else
        {
            Try(id, CommandType::Wait, Source::Script);
        }
    }

    // A spell held in its conjure broken by what the engine did to its
    // caster: its command ends, and the game layer says so (Gate.cpp's
    // OnEnd, OnConjureCleared).
    void ConjureBroken(ObjectId id)
    {
        const auto prior = tasks_.find(id);
        if (prior == tasks_.end() || !prior->second.conjuring)
        {
            return;
        }
        world_.heldConjures.erase(id);
        world_.Get(id).conjureHeld = false;
        if (world_.machine.OnConjureCleared(id))
        {
            ++coverage_.conjureCleared;
        }
    }

    // Every effect as the world applies it (8), and what the arena keeps of it.
    void Saw(const Effect &e)
    {
        switch (e.kind)
        {
        case EffectKind::Pause:
        case EffectKind::Unpause:
            break;
        case EffectKind::Signal:
            if (e.event == events::kTurnStart)
            {
                lastTurnStart_ = e.target;
                Must(started_.insert({e.a, e.target}).second, "a combatant started two turns in one round");
                ++coverage_.turns;
            }
            else if (e.event == events::kRoundStart)
            {
                ++coverage_.rounds;
            }
            break;
        case EffectKind::PokeAi:
            // The fight over, those of it still standing that are not the
            // party's are told so, as the bystanders are (issue 12): an AI
            // whose wait was blocked has nothing queued to run it again.
            // And whoever the gate turned away with no turn to come (15),
            // the only way a party member is told with no fight on: turned
            // away as combat began, and no fight followed.
            if (!world_.machine.Active())
            {
                Must(world_.Get(e.target).side != Side::Party || owed_.contains(e.target),
                     "a party member's AI told the fight is over");
                Must(toldIdle_.insert(e.target).second, "an AI told twice as the fight let the world go");
                break;
            }
            Must(e.target == world_.Current(), "an AI told to choose out of its turn");
            Must(!PlayerControls(e.target), "the AI told to choose for a creature the player controls");
            Must(world_.Get(e.target).condition != Condition::Disabled, "a stunned creature's AI told to act");
            Must(!Unaware(world_.Get(e.target).side, world_.Get(e.target).perception),
                 "a hostile that has noticed nobody to fight told to choose");
            Must(!NothingToFight(world_.Get(e.target).side, world_.Get(e.target).perception),
                 "one the game does not have in combat, nobody for it to fight, told to choose: it has no turn");
            Must(!world_.Get(e.target).inactive, "one the game has made inactive told to choose: it has no turn");
            FollowTurn();
            told_ = e.target;
            break;
        case EffectKind::Select:
            Must(world_.Get(e.target).side == Side::Party, "a creature not in the party selected");
            Must(e.target != world_.Current() || world_.Get(e.target).condition != Condition::Disabled,
                 "a stunned party member handed its turn");
            break;
        case EffectKind::HoldTime:
            Must((e.a != 0) != timeHeld_, "effects' time held twice, or let go twice");
            timeHeld_ = e.a != 0;
            if (!timeHeld_)
            {
                ownClock_.clear();
            }
            break;
        case EffectKind::OwnClock:
            Must(timeHeld_, "a creature put on its own clock while effects' time runs anyway");
            Must(settings_.turnClock == TurnClock::Time && settings_.ownClocks, "own clocks with own clocks off");
            if (e.a != 0)
            {
                ownClock_.insert(e.target);
            }
            else
            {
                ownClock_.erase(e.target);
            }
            break;
        case EffectKind::GameSpeed:
            Must(settings_.fightSpeed != 1.0f, "the game's speed written at speed 1");
            ++coverage_.speedChanges;
            break;
        case EffectKind::AgeTime:
            Must(timeHeld_, "effects aged by the fight while their time runs anyway");
            if (e.target != kNoObject)
            {
                // A creature's own clock: only one put on its own, only in
                // its own turn (this frame's, or one that opened or closed
                // in it), and by something.
                Must(settings_.turnClock == TurnClock::Time && settings_.ownClocks,
                     "a creature's own clock moved with own clocks off");
                Must(ownClock_.contains(e.target), "a creature's own clock moved though it was never put on one");
                // (Or the target of the acting creature's own effect: a grab
                // holds both, and both clocks move by its steps. As the
                // machine has it now, or had it as this frame began: a step
                // of the effect and the turn's end, stopped as too long, came
                // in one frame, and the turn ended forgets its target: the
                // long run with combatants out of reach, seed 86 with time,
                // 2026-10-04.)
                Must(e.target == world_.Current() || e.target == lastCurrent_ || turnsThisFrame_.contains(e.target) ||
                         e.target == world_.machine.OwnEffectTarget() || e.target == grabbedBefore_,
                     "a creature's own clock moved outside its own turn");
                Must(e.b > 0, "a creature's own clock moved by nothing");
                ++coverage_.ownAged;
                // Its own clock's worth off the timer of what holds it: an
                // effect on a creature on its own clock wears off as that
                // clock moves (DAOLib's dao::clock::Advance for one object),
                // so a hold its turn pays for ends within the turn.
                if (const auto left = conditionLeftMs_.find(e.target); left != conditionLeftMs_.end())
                {
                    left->second -= e.b;
                }
                break;
            }
            // A round's worth, or a step of an action's own effect, or the
            // round's worth less those.
            // With time, a round is a turn's seconds.
            Must(e.b > 0 && e.b <= static_cast<std::int32_t>((settings_.turnClock == TurnClock::Time
                                                                  ? settings_.secondsPerTurn
                                                                  : settings_.effectSecondsPerRound) *
                                                             1000.0f),
                 "a round aged wrong");
            ++coverage_.roundsAged;
            // A round's worth off every condition's own timer; not one on a
            // creature on a clock of its own, which the shared clock passes
            // by (DAOLib's dao::effects::Advance).
            for (auto &[id, left] : conditionLeftMs_)
            {
                if (!ownClock_.contains(id))
                {
                    left -= e.b;
                }
            }
            break;
        case EffectKind::Cooldown:
            Must(e.target != kNoObject && e.a >= 0 && e.b >= 0, "a cooldown for nobody, or for no ability");
            // Ready again only as its user's turn begins: the one last told
            // its turn had come, or the one whose turn is beginning in this
            // frame. With time, what a hold or a grab took of the bar as the
            // turn began moves its clock before the scripts are told of the
            // turn, and a cooldown that runs out in that share lifts there
            // (the long run with combatants out of reach, seed 206 with
            // time, 2026-10-04: the check went by the turn-start signal
            // alone, which comes a few effects later in the same step).
            if (e.b == 0)
            {
                Must(e.target == lastTurnStart_ || e.target == world_.Current(),
                     "a cooldown released out of its user's turn");
                ++coverage_.cooldownsReleased;
            }
            else if (e.b == TurnMachine::kCooldownHoldSeconds)
            {
                ++coverage_.cooldownsHeld;
            }
            break;
        case EffectKind::EndConditions:
            Must(e.target == world_.Current(), "a condition ended out of its creature's turn");
            // Or one whose seconds the machine was given: it ends what it
            // has taken the time of. (A second hold landing between two
            // looks, its seconds not read, is the first's to the machine.)
            Must(settings_.conditionTurns > 0 || secondsKnown_.erase(e.target) > 0,
                 "a condition on the game's clock ended by the machine");
            ++coverage_.conditionsEnded;
            conditionLeftMs_.erase(e.target);
            Release(e.target);
            break;
        case EffectKind::ReleaseCast:
        case EffectKind::DropCast:
            break; // with time, kept actions (World's onReleased); nothing charges as built
        case EffectKind::Approach:
            Must(e.target != kNoObject, "an approach for nobody");
            Must(e.target == world_.Current(), "an approach for someone whose turn it is not");
            break;
        case EffectKind::ClearCommands:
        case EffectKind::Floaty:
        case EffectKind::CenterCamera:
        case EffectKind::Explain:
            Must(e.target != kNoObject, "an effect for nobody");
            if (e.kind == EffectKind::Floaty && e.text.ends_with(L" loses the turn"))
            {
                ++coverage_.lostTurns;
                coverage_.lostOnClock += settings_.conditionTurns == 0 ? 1 : 0;
            }
            if (e.kind == EffectKind::ClearCommands)
            {
                tasks_.erase(e.target);
                if (e.target == world_.Current() && world_.machine.Active())
                {
                    ++clears_;
                }
            }
            if (e.kind == EffectKind::Floaty && e.text == L"Out of movement")
            {
                ++coverage_.movementCuts;
            }
            break;
        }
    }

    // What happens in the world before the machine looks: commands run, AIs
    // react, the player acts, menus and conversations come and go.
    void Tick()
    {
        FollowTurn();
        if (interlude_ > 0 && --interlude_ == 0)
        {
            world_.mode = Mode::Combat;
        }
        if (menu_ > 0 && --menu_ == 0)
        {
            world_.mode = Mode::Combat;
            world_.paused = false; // leaving a menu unpauses (GameModeGUI's slot 8)
        }
        if (world_.mode == Mode::Dialog)
        {
            // A cutscene's scripts move people about. (Nobody, where the last
            // of the party has just fallen and the enemy left is one the
            // scan no longer finds fighting.)
            if (Chance(rng_, 0.05))
            {
                if (const auto about = Living(&Arena::Anyone); !about.empty())
                {
                    Try(Pick(rng_, about), CommandType::MoveToLocation, Source::Script);
                }
            }
            Run();
            return;
        }
        if (world_.mode != Mode::Combat)
        {
            return;
        }
        Surfacing();
        if (!world_.paused)
        {
            Run();
            OutOfTurn();
            Conditions();
            Noticing();
            Wandering();
            Burrowing();
        }
        Player();
        Happenings();
    }

    // Now and then one of the fight that is not the party's goes where the
    // scan's own rules no longer find it fighting (past combat_radius of
    // every living party member, or out of combat), the engine's perception
    // between it and the party as it may be (issue 29). After a while it is
    // found fighting again, or it is lost for good, neither perceiving the
    // other.
    // On the stream of what is noticed, not the fight's own.
    // One gone so asks for a command now and then, its own script's, as it
    // goes about what it went after (1, 15): an attack if the game has it in
    // combat, a walk if not. Held if the game has it in combat, wherever the
    // fight has it; let through, the game's own, if not. Held with no turn
    // to come (out of the fight by then: down in the order until the round
    // turns, or dropped from it), it is owed a telling as the fight lets go.
    // On a stream of its own, and none of the fight's draws: a fight in
    // which nobody asks is the fight it was.
    void Asks(ObjectId id)
    {
        const Creature &c = world_.Get(id);
        const Combatant *in = world_.machine.Order().Find(id);
        // (Not the creature whose turn it is while the game has it in combat:
        // what it asks for then is its AI's choice of that turn, Decide's.)
        if (c.down || !c.exists || c.inactive || (id == world_.Current() && c.perception.inCombat))
        {
            return;
        }
        CommandRequest request;
        request.object = id;
        request.source = Source::Script;
        request.inCombat = c.perception.inCombat;
        request.type = request.inCombat ? CommandType::Attack : CommandType::MoveToLocation;
        const Verdict verdict = world_.Queue(request);
        Must((verdict == Verdict::Block) == request.inCombat,
             "one gone where the scan does not look: held when the game does not have it in combat, or let through "
             "when it does");
        if (verdict == Verdict::Allow)
        {
            // (Whatever of its own it was at goes on under what it is sent
            // to do: the stand-in's word for it is put back.)
            if (const auto task = tasks_.find(id); task != tasks_.end())
            {
                world_.Get(id).busy = true;
                world_.Get(id).current = task->second.type;
            }
            else
            {
                world_.Finish(id);
            }
            coverage_.gamesOwn += in != nullptr ? 1 : 0; // of the order still, and the game's all the same
            return;
        }
        if (in == nullptr || in->down)
        {
            owed_.insert(id);
            ++coverage_.turnedAway;
        }
    }

    void Wandering()
    {
        if (world_.machine.Active())
        {
            for (const auto &[id, frames] : away_)
            {
                if (Chance(asks_, 0.01))
                {
                    Asks(id);
                }
            }
        }
        for (auto it = away_.begin(); it != away_.end();)
        {
            Creature &c = world_.Get(it->first);
            if (!c.down && --it->second > 0)
            {
                ++it;
                continue;
            }
            if (!c.down && Chance(notice_, 0.5))
            {
                c.fighting = true;
                c.perception = Perception{};
                ++coverage_.cameBack;
            }
            else if (!c.down)
            {
                c.perception.byParty = false;
                c.perception.ofParty = false;
                c.perception.ofFoes = false;
                c.perception.inCombat = false;
            }
            it = away_.erase(it);
        }
        if (!runners_ || !world_.machine.Active() || !Chance(notice_, 0.002))
        {
            return;
        }
        std::vector<ObjectId> standing;
        for (const Creature &c : world_.creatures)
        {
            const Combatant *in = world_.machine.Order().Find(c.id);
            if (c.side != Side::Party && c.exists && !c.down && c.fighting && in != nullptr && !in->down)
            {
                standing.push_back(c.id);
            }
        }
        if (standing.empty())
        {
            return;
        }
        Creature &c = world_.Get(Pick(notice_, standing));
        c.fighting = false;
        if (c.side == Side::Enemy)
        {
            c.perception.byParty = Chance(notice_, 0.7);
            c.perception.ofParty = Chance(notice_, 0.5);
            c.perception.ofFoes = c.perception.ofParty || Chance(notice_, 0.3);
            // In combat by the game's word while it perceives a foe; else
            // as it may be (its combat state not yet dropped).
            c.perception.inCombat = c.perception.ofFoes || Chance(notice_, 0.5);
        }
        else
        {
            c.perception.ofFoes = Chance(notice_, 0.7);
        }
        // Six seconds or more of running time, past the four a combatant must
        // be missing to have left, and not so long that the fight drags on
        // around those the party cannot get at.
        away_[c.id] = 400 + static_cast<int>(notice_.Next() % 400);
        ++coverage_.ranOff;
        // The creature whose turn it is, and the game has it out of combat:
        // its script sends it home in the same instant, before the machine
        // has looked at it again (16). With no draw.
        if (c.id == world_.Current() && !c.perception.inCombat)
        {
            Asks(c.id);
        }
    }

    // Now and then the game makes one of the fight inactive for a while (17;
    // issue 44), anyone of it, a party member too: a tentacle gone under
    // ground (orz550_broodmother_h.nss: WR_SetObjectActive(oTentacle,
    // FALSE)). Out of the world: its commands are cleared as it goes, its AI
    // is given no updates, nobody orders it, and it does nothing until it is
    // made active again (Try, Decide, Player). Three times in ten something
    // is queued on it after it has gone, which the engine does not run (a
    // pin's command, from a shot the mod had kept for its archer and gave
    // five seconds on, in the fight this is from): busy, with no current
    // command, which is what a turn used to stand out max_action_ms for. Up
    // again, what waited on it begins: an effect's command, in front of
    // whatever its turn would have it do.
    // On a stream of its own, and only in the fights that have combatants
    // out of reach: a fight without it is draw for draw the fight it was.
    //
    // It comes up again whether or not the world runs meanwhile (Tick): what
    // brings a creature back in the game is a timer of its scripts', which in
    // a fight is on a clock the turns move. Counted only while the world ran,
    // the last of the party and the last enemy, both under, were passed over
    // from round 340 to past round 680 of one fight with nothing to end it
    // (seed 3 of these, as built), and past the 2,480th of another (seed 21,
    // with time): with nobody in the world nobody has a turn, no turn runs
    // the world, and the rounds go by a frame or two each. That is the
    // machine doing as it should, and the design says what it rests on.
    void Surfacing()
    {
        for (auto it = under_.begin(); it != under_.end();)
        {
            Creature &c = world_.Get(it->first);
            if (c.exists && !c.down && --it->second > 0)
            {
                ++it;
                continue;
            }
            const bool waited = c.queued != CommandType::Invalid;
            world_.SetActive(c.id, true);
            world_.Finish(c.id);
            if (waited && c.exists && !c.down)
            {
                Try(c.id, CommandType::ExecuteEffect, Source::Engine);
            }
            ++coverage_.cameUp;
            it = under_.erase(it);
        }
    }
    void Burrowing()
    {
        if (!runners_ || !world_.machine.Active() || !Chance(burrows_, 0.0015))
        {
            return;
        }
        // (Not the last of the party left in the world, so that the rounds
        // with nobody in it, Surfacing, are few.)
        const auto partyLeft = std::ranges::count_if(world_.creatures, [](const Creature &c) {
            return c.side == Side::Party && c.exists && !c.down && !c.inactive;
        });
        std::vector<ObjectId> standing;
        for (const Creature &c : world_.creatures)
        {
            const Combatant *in = world_.machine.Order().Find(c.id);
            if (c.exists && !c.down && !c.inactive && in != nullptr && !in->down &&
                (c.side != Side::Party || partyLeft > 1))
            {
                standing.push_back(c.id);
            }
        }
        if (standing.empty())
        {
            return;
        }
        // Oftener the one whose turn it is, so that some go in the middle of
        // their own turn, an action running.
        const ObjectId current = world_.Current();
        const bool own = std::ranges::find(standing, current) != standing.end() && Chance(burrows_, 0.4);
        const ObjectId id = own ? current : Pick(burrows_, standing);
        // "All commands will be cleared upon disabling" (script.ldf).
        ConjureBroken(id);
        tasks_.erase(id);
        world_.SetActive(id, false);
        if (Chance(burrows_, 0.3))
        {
            world_.QueueOnInactive(id, CommandType::ExecuteEffect);
        }
        // One to twelve seconds of running time: a turn or two of its own
        // passed over, most times.
        under_[id] = 60 + static_cast<int>(burrows_.Next() % 700);
        ++coverage_.wentUnder;
    }

    // Whether the engine's perception keeps a creature in the fight, the
    // arena's own word for it (12): an enemy the party perceives or that
    // perceives the party; an ally that perceives an enemy.
    static bool Perceived(const Creature &c)
    {
        return c.side == Side::Enemy ? c.perception.byParty || c.perception.ofParty : c.perception.ofFoes;
    }

    // A hostile that had noticed nobody notices: struck (the game's own
    // rules_core makes a creature perceive its attacker), or the party come
    // into its perception as the world runs.
    void Notices(Creature &c)
    {
        if (Unaware(c.side, c.perception))
        {
            c.perception = Perception{};
            ++coverage_.noticed;
        }
    }
    void Noticing()
    {
        for (Creature &c : world_.creatures)
        {
            // Seldom, so that turns are skipped; oftener in its own turn, so
            // that some notice inside the beat.
            if (!c.down && Unaware(c.side, c.perception) && Chance(notice_, c.id == world_.Current() ? 0.004 : 0.0003))
            {
                Notices(c);
            }
        }
    }

    // Commands progress while the game runs.
    void Run()
    {
        // One taken into combat stops what it was at as the game's own: its
        // script clears its commands as it perceives a hostile (rules_core.nss,
        // EVENT_TYPE_PERCEPTION_APPEAR: SetCombatState, WR_ClearAllCommands),
        // and from then the fight holds it.
        std::erase_if(tasks_, [&](const auto &entry) {
            if (!entry.second.gamesOwn || !world_.InCombat(entry.first))
            {
                return false;
            }
            world_.Finish(entry.first);
            return true;
        });
        std::vector<ObjectId> ids;
        for (const auto &[id, task] : tasks_)
        {
            ids.push_back(id);
        }
        if (world_.machine.Active() && world_.Current() != kNoObject)
        {
            turnRunMs_ += kFrameMs;
            // Seen in play (2026-09-30, 13:25): a pinned character's turn
            // waited 12 s on the pin's command.
            const Phase phase = world_.machine.GetPhase();
            const bool turn = phase == Phase::AwaitingOrder || phase == Phase::Dispatching ||
                              phase == Phase::AiThinking || phase == Phase::Executing || phase == Phase::Settling;
            heldInTurnMs_ =
                turn && world_.Get(world_.Current()).condition == Condition::Disabled ? heldInTurnMs_ + kFrameMs : 0;
            Must(heldInTurnMs_ <= kHeldTurnMs, "a held creature's turn waited on its condition");
            FollowTurn();
            const auto found = tasks_.find(world_.Current());
            const bool stillHeld = found != tasks_.end() && found->second.type == CommandType::ExecuteEffect;
            if (!stillHeld)
            {
                heldAtStart_ = false; // let go (a hold after is another's)
            }
            else if (turn && heldAtStart_)
            {
                heldAtStartMs_ += kFrameMs;
            }
            // A turn's worth (13): with time the turn's seconds, which a hold
            // takes of the bar as it runs; as built what a round spends on
            // effects, or a turn's seconds with effects on the game's clock.
            // Of the machine's clock, which runs at the fight's speed.
            const float worth = settings_.turnClock == TurnClock::Time || !(settings_.effectSecondsPerRound > 0.0f)
                                    ? settings_.secondsPerTurn
                                    : settings_.effectSecondsPerRound;
            const auto bound = static_cast<std::uint32_t>(worth * 1000.0f / settings_.fightSpeed) + kHoldSlackMs;
            Must(!(turn && heldAtStart_ && stillHeld && turnRunMs_ - turnBeganAtMs_ > bound),
                 "a turn waited past a turn's worth on an effect no condition named");
        }
        for (const ObjectId id : ids)
        {
            const auto found = tasks_.find(id);
            if (found == tasks_.end())
            {
                continue;
            }
            Task &task = found->second;
            Creature &c = world_.Get(id);
            if (c.down)
            {
                world_.Finish(id);
                tasks_.erase(found);
                continue;
            }
            if (task.walk > 0.0f)
            {
                const float step = std::min(task.walk, kRunSpeed * c.speed * static_cast<float>(kFrameMs) / 1000.0f);
                c.position.x += step;
                task.walk -= step;
                c.moving = true;
                // A turn's walking; in a conversation it is the story's.
                if (id == world_.Current() && Classify(task.type) != CommandKind::Involuntary &&
                    world_.mode == Mode::Combat && !task.story && !task.gamesOwn)
                {
                    walked_ += step;
                }
                continue;
            }
            c.moving = false;
            if (task.held)
            {
                continue;
            }
            if (task.conjuring)
            {
                // At its held conjure; a blow over its caster's threshold, or
                // a knockdown, clears it now and then in another's turn, as
                // the game's own rules_core would.
                c.conjureHeld = true;
                if (id != world_.Current() && world_.machine.Active() && Chance(rng_, 0.0004))
                {
                    tasks_.erase(found);
                    world_.heldConjures.erase(id);
                    c.conjureHeld = false;
                    world_.Finish(id);
                    Must(world_.machine.OnConjureCleared(id), "a held conjure cleared that the machine did not know");
                    ++coverage_.conjureCleared;
                }
                continue;
            }
            task.workMs -= static_cast<std::int32_t>(kFrameMs);
            if (task.workMs > 0)
            {
                continue;
            }
            const Task done = task;
            tasks_.erase(found);
            world_.Finish(id);
            // A grab: its own effect holds it (and its target) a while, the
            // engine's command in front; the machine runs both clocks by it.
            if (done.grab && id == world_.Current() && world_.machine.Active() && done.target != kNoObject &&
                !world_.Get(done.target).down)
            {
                Task held{CommandType::ExecuteEffect};
                held.workMs = static_cast<std::int32_t>(Between(rng_, 1000.0f, 9000.0f));
                tasks_[id] = held;
                world_.Get(id).busy = true;
                world_.Get(id).current = CommandType::ExecuteEffect;
                ++coverage_.grabs;
                continue;
            }
            if (done.type == CommandType::Attack && done.target != kNoObject)
            {
                Creature &victim = world_.Get(done.target);
                // (Nor at one the game has made inactive since: nobody is
                // there to strike. The draws as they were.)
                const bool there = !victim.inactive;
                if (there)
                {
                    Notices(victim);
                }
                const double kill = victim.side == Side::Party ? 0.05 : 0.35;
                if (!victim.down && Chance(rng_, kill) && there)
                {
                    victim.down = true;
                }
                else if (!victim.down && Chance(rng_, 0.2))
                {
                    Try(done.target, CommandType::ExecuteEffect, Source::Engine);
                }
            }
            // COMMAND_COMPLETE: the creature's AI chooses again.
            if (world_.machine.Order().Find(id) != nullptr)
            {
                Decide(id, true);
            }
        }
    }

    // Stuns and pins land (a howl, Pinning Shot) and hold their creature in
    // the effect's command, at the front of its queue, while they last (seen
    // in play, 2026-09-30: a pinned character's attack queued behind it and
    // never ran). They wear off on the effects' timer, which runs only while
    // effects' time does and moves on a round's worth as a round ages it,
    // unless the machine ends them first.
    void Conditions()
    {
        for (auto it = conditionLeftMs_.begin(); it != conditionLeftMs_.end();)
        {
            if (!world_.timeHeld)
            {
                it->second -= static_cast<std::int32_t>(kFrameMs);
            }
            if (it->second <= 0)
            {
                const ObjectId id = it->first;
                it = conditionLeftMs_.erase(it);
                Release(id);
            }
            else
            {
                ++it;
            }
        }
        if (Chance(rng_, 0.003))
        {
            const auto fighting = Living(&Arena::Anyone);
            if (!fighting.empty())
            {
                const ObjectId id = Pick(rng_, fighting);
                // It interrupts what its victim was doing. Now and then one
                // the statuses do not name (an ogre's grab, say): the
                // creature is held all the same.
                ConjureBroken(id);
                tasks_.erase(id);
                world_.Finish(id);
                const bool already = world_.Get(id).condition == Condition::Disabled;
                const float before = world_.Get(id).conditionSeconds;
                world_.Get(id).condition = Chance(rng_, 0.2) ? Condition::None : Condition::Disabled;
                Try(id, CommandType::ExecuteEffect, Source::Engine);
                if (const auto found = tasks_.find(id); found != tasks_.end())
                {
                    found->second.held = true;
                }
                conditionLeftMs_[id] = static_cast<std::int32_t>(Between(rng_, 2000.0f, 10000.0f));
                // With time, the game layer reads its seconds, most times.
                world_.Get(id).conditionSeconds =
                    settings_.turnClock == TurnClock::Time && Chance(rng_, 0.7)
                        ? (Chance(rng_, 0.5) ? Between(rng_, 0.5f, 3.9f)
                                             : static_cast<float>(conditionLeftMs_[id]) / 1000.0f)
                        : 0.0f;
                if (world_.Get(id).conditionSeconds > 0.0f && world_.Get(id).condition == Condition::Disabled)
                {
                    secondsKnown_.insert(id);
                }
                // Held already: the longer of the two is what there is to read.
                if (already)
                {
                    world_.Get(id).conditionSeconds = std::max(before, world_.Get(id).conditionSeconds);
                }
                ++coverage_.stunned;
                if (echo)
                {
                    std::printf("%7u (arena: %u held, condition %d, %.1f s read, was held %d)\n", world_.now, id,
                                static_cast<int>(world_.Get(id).condition),
                                static_cast<double>(world_.Get(id).conditionSeconds), already ? 1 : 0);
                }
            }
        }
    }

    // A condition ends (worn off, or ended by the machine): its command
    // with it, and its creature's AI chooses again (COMMAND_COMPLETE).
    void Release(ObjectId id)
    {
        world_.Get(id).condition = Condition::None;
        if (id == world_.Current())
        {
            heldAtStart_ = false; // a hold after this one is another's
        }
        const auto found = tasks_.find(id);
        if (found == tasks_.end() || !found->second.held)
        {
            return;
        }
        tasks_.erase(found);
        world_.Finish(id);
        if (world_.machine.Order().Find(id) != nullptr)
        {
            Decide(id, true);
        }
    }

    // Others, hit or seeing an enemy, try to act (ATTACKED, PERCEPTION_APPEAR),
    // and the engine knocks someone down.
    void OutOfTurn()
    {
        if (Chance(rng_, 0.08))
        {
            const auto fighting = Living(&Arena::Anyone);
            if (!fighting.empty())
            {
                // (One the game does not have in combat, in whoever's turn,
                // only walks: its script chooses no attack for it, the
                // combat AI being for those in combat. The same draws.)
                const ObjectId id = Pick(rng_, fighting);
                const bool attacks = Chance(rng_, 0.7);
                const bool gamesOwn =
                    world_.machine.Active() && !world_.InCombat(id) && world_.Get(id).side != Side::Party;
                Try(id, attacks && !gamesOwn ? CommandType::Attack : CommandType::MoveToLocation, Source::Script);
            }
        }
        if (Chance(rng_, 0.004))
        {
            const auto fighting = Living(&Arena::Anyone);
            if (!fighting.empty())
            {
                Try(Pick(rng_, fighting), CommandType::ExecuteEffect, Source::Engine);
            }
        }
    }

    // The pause key: taken exactly while a fight holds the world, the game's
    // otherwise (in the frame a conversation ends, say, before the machine
    // has seen it end).
    void PressPause()
    {
        const bool holding = world_.machine.Active();
        Must(world_.machine.OnEndTurn() == holding,
             "End turn taken when no fight holds the world, or not when one does");
    }

    // The player, on a turn that is theirs, thinks a while and then acts.
    void Player()
    {
        const ObjectId current = world_.Current();
        const Phase phase = world_.machine.GetPhase();
        // (Nobody to order where the game has made the character inactive.)
        if (current == kNoObject || !PlayerControls(current) || world_.Get(current).inactive || !Chance(rng_, 0.1))
        {
            return;
        }
        if (phase != Phase::AwaitingOrder && Chance(rng_, 0.05))
        {
            // End turn pressed while something plays out.
            PressPause();
            return;
        }
        if (Chance(rng_, 0.12))
        {
            // Delay pressed: taken exactly when the view says it may be.
            const TurnView view = world_.machine.View();
            const bool taken = world_.machine.OnDelay();
            Must(taken == view.canDelay, "a delay taken when the view said no, or refused when it said yes");
            if (taken)
            {
                Must(delayed_.emplace(current, DelayAsked{view.timeMs, view.round, false}).second,
                     "a second delay in a round");
                return;
            }
        }
        if (phase == Phase::Dispatching && Chance(rng_, 0.5))
        {
            // The command the order made, queued once the game runs.
            Try(current, CommandType::Attack, Source::Player);
            return;
        }
        if (phase != Phase::AwaitingOrder)
        {
            return;
        }
        const double roll = Unit(rng_);
        if (roll < 0.33)
        {
            Try(current, CommandType::Attack, Source::Player);
        }
        else if (roll < 0.55)
        {
            Try(current, Chance(rng_, 0.8) ? CommandType::MoveToLocation : CommandType::Drive, Source::Player);
        }
        else if (roll < 0.8)
        {
            PressPause();
        }
        else if (roll < 0.87)
        {
            world_.machine.OnPlayerOrder("an order that queues nothing yet");
            ++coverage_.dispatches;
        }
        else
        {
            // Another party member, out of turn.
            const auto party = Living(+[](const Creature &c) { return c.side == Side::Party; });
            Try(Pick(rng_, party), CommandType::Attack, Source::Player);
        }
    }

    // Menus, conversations, reinforcements, and the party falling.
    void Happenings()
    {
        if (world_.machine.Active() && Chance(rng_, 0.0015))
        {
            world_.mode = Mode::Gui;
            world_.paused = true; // entering a menu pauses (GameModeGUI's slot 7)
            menu_ = 10 + static_cast<int>(rng_.Next() % 50);
            ++coverage_.menus;
            return;
        }
        if (world_.machine.Active() && Chance(rng_, 0.0008))
        {
            world_.mode = Mode::Dialog;
            interlude_ = 10 + static_cast<int>(rng_.Next() % 100);
            ++interruptions_;
            ++coverage_.interludes;
            return;
        }
        if (world_.machine.Active() && !world_.paused && Chance(rng_, 0.0008))
        {
            // Damage over time, whoever it is, whatever they are doing: the
            // one whose turn it is falls mid-turn now and then.
            const auto fighting = Living(&Arena::Anyone);
            if (!fighting.empty())
            {
                world_.Get(Pick(rng_, fighting)).down = true;
                ++coverage_.bledOut;
            }
        }
        if (world_.machine.Active() && Chance(rng_, 0.0003))
        {
            // A script takes a creature out of the world (fled, destroyed).
            const auto others = Living(+[](const Creature &c) { return c.side != Side::Party && c.exists; });
            if (!others.empty())
            {
                Creature &gone = world_.Get(Pick(rng_, others));
                gone.exists = false;
                gone.down = true;
                ++coverage_.vanished;
            }
        }
        if (world_.machine.Active() && joiners_ < 2 && Chance(rng_, 0.002))
        {
            const auto id = static_cast<ObjectId>(0x280 + joiners_++);
            Creature c = Make(id, Side::Enemy, false, L"joiner", Between(rng_, 8.0f, 40.0f));
            world_.creatures.push_back(std::move(c));
            ++coverage_.joiners;
        }
        if (std::ranges::all_of(world_.creatures, [](const Creature &c) { return c.side != Side::Party || c.down; }))
        {
            world_.mode = Mode::Dead;
            ++coverage_.partyWipes;
            return;
        }
        // The game leaves combat a moment after the last enemy falls.
        if (world_.machine.GetPhase() == Phase::Idle && world_.machine.Order().Empty() &&
            Living(+[](const Creature &c) { return c.side == Side::Enemy && c.fighting; }).empty())
        {
            if (++leaving_ > 30)
            {
                world_.mode = Mode::Explore;
            }
        }
    }

    // The machine's typed events (core/Events.h) make a well-formed story:
    // a fight begins with its first round, a turn opens and ends before the
    // next opens, and it is the current creature's.
    void Heard(const TurnEvent &e)
    {
        ++coverage_.events;
        switch (e.kind)
        {
        case EventKind::FightBegan:
            Must(!turnOpen_, "a fight began inside a turn");
            roundStarted_ = false;
            delayed_.clear();
            endedWhy_.clear(); // nothing rolls over from one fight to the next
            // And its rounds are counted anew: a second fight in the same
            // combat (the last enemy lost, and then found fighting again)
            // has a round 1 of its own.
            started_.clear();
            // Whoever was turned away as combat began and is of the fight
            // has its turns to come (15).
            std::erase_if(owed_, [&](ObjectId id) { return world_.machine.Order().Find(id) != nullptr; });
            break;
        case EventKind::RoundStarted:
            roundStarted_ = true;
            // Who put a turn off took it before the round turned, unless it
            // could not (fallen, gone, the fight suspended and begun again).
            for (const auto &[id, asked] : delayed_)
            {
                const auto c = std::ranges::find(world_.creatures, id, &Creature::id);
                Must(c == world_.creatures.end() || c->down || !c->exists || !asked.awaited,
                     "a turn put off never came before the round turned");
            }
            delayed_.clear();
            break;
        case EventKind::TurnDelayed:
            Must(turnOpen_, "a turn put off that was not open");
            Must(delayed_.contains(e.who), "a turn put off that nobody asked for");
            if (const auto asked = delayed_.find(e.who); asked != delayed_.end())
            {
                Must(e.round == asked->second.round, "a turn put off in another round than it was asked in");
                Must(!asked->second.awaited, "a turn put off twice in a round");
                asked->second.awaited = true;
            }
            turnOpen_ = false;
            ++coverage_.delays;
            break;
        case EventKind::TurnStarted:
        case EventKind::TurnLost:
            turnsThisFrame_.insert(e.who);
            Must(roundStarted_, "a turn before the fight's first round");
            Must(!turnOpen_, "a turn opened while another was open");
            Must(e.who == world_.Current(), "a turn opened for someone not current");
            // No turn is begun for one the game has made inactive (17). One a
            // condition holds loses its turn to that first, as anyone does.
            Must(e.kind == EventKind::TurnLost || !world_.Get(e.who).inactive,
                 "a turn begun for one the game has made inactive");
            turnOpen_ = true;
            // Time rolled over into this turn only from one its actor ended
            // (10). The arena's own word for which those are: the three ends
            // that can leave time unspent by the actor's choice.
            if (e.kind == EventKind::TurnStarted && world_.machine.View().rolledMs > 0)
            {
                const auto last = endedWhy_.find(e.who);
                const bool saved = last != endedWhy_.end() && (last->second == "ended by the player" ||
                                                               last->second == "done" || last->second == "waits");
                if (!saved)
                {
                    const std::string what =
                        "time rolled over from a turn its actor did not end: " +
                        (last != endedWhy_.end() ? last->second : std::string{"it had no turn before"});
                    Must(false, what.c_str());
                }
            }
            if (const auto asked = delayed_.find(e.who); asked != delayed_.end() && asked->second.awaited)
            {
                // The turn it put off: in the same round, and with the bar
                // it had.
                asked->second.awaited = false;
                Must(e.round == asked->second.round, "a turn put off came in another round");
                if (e.kind == EventKind::TurnStarted)
                {
                    Must(world_.machine.View().timeMs == asked->second.bar, "a turn put off came with another bar");
                    ++coverage_.delayedTurns;
                }
            }
            break;
        case EventKind::TurnResumed:
            // The turn a conversation came in the middle of, still open.
            Must(turnOpen_, "a turn resumed that was never open");
            Must(e.who == world_.Current(), "a turn resumed for someone not current");
            break;
        case EventKind::AwaitingOrder:
            // Only an open turn waits on an order, and only its own creature's.
            Must(turnOpen_, "a turn waits on an order that is not open");
            Must(e.who == world_.Current(), "a turn waits on an order for someone not current");
            Must(world_.machine.GetPhase() == Phase::AwaitingOrder, "a turn said to wait on an order that does not");
            Must(!world_.Get(e.who).inactive, "a turn waits on an order for one the game has made inactive");
            break;
        case EventKind::TurnEnded: {
            turnsThisFrame_.insert(e.who);
            const bool wasOpen = std::exchange(turnOpen_, false);
            Must(EndedByItsActor(e.detail).has_value(), "a turn ended for a reason core/TurnEnd.h does not class");
            endedWhy_[e.who] = e.detail;
            // An unaware hostile's turn (11): skipped only while it has
            // noticed nobody, only after its beat was said and is out (of the
            // fight's clock, which runs with its speed), and never waited out
            // on an AI that had nobody to choose.
            if (e.detail == "unaware")
            {
                const Creature &c = world_.Get(e.who);
                Must(Unaware(c.side, c.perception), "a turn skipped as unaware though its creature has noticed");
                Must(beatOf_ == e.who, "a turn skipped as unaware with no beat said for it");
                const float ran = static_cast<float>(turnRunMs_ - beatFromMs_ + 2 * kFrameMs) * settings_.fightSpeed;
                Must(ran >= static_cast<float>(TurnMachine::kUnawareMs),
                     "an unaware hostile's turn skipped before its beat was out");
                ++coverage_.unawareTurns;
            }
            // Only an AI that was told to choose is waited on: and one that
            // had noticed nobody is never told (PokeAi, above).
            if (e.detail == "chose nothing")
            {
                Must(told_ == e.who, "a turn waited out on an AI that was never told to choose");
            }
            // One the game has made inactive (17): only such a creature's
            // turn ends so. Passed over as it came (no turn was open), or
            // ended in the middle, its creature gone as it played. What it
            // had put off this round (Delay) has come, and gone the same way.
            if (e.detail == "inactive")
            {
                const Creature &c = world_.Get(e.who);
                Must(c.inactive, "a turn ended as inactive for one the game has active");
                ++coverage_.inactiveTurns;
                coverage_.inactiveMidTurn += wasOpen ? 1 : 0;
                coverage_.inactiveQueued += c.queued != CommandType::Invalid ? 1 : 0;
                if (const auto asked = delayed_.find(e.who); asked != delayed_.end())
                {
                    asked->second.awaited = false;
                }
            }
            // One with no turn (16): only one that is not of the party and
            // that the game does not have in combat.
            if (e.detail == "out of combat")
            {
                const Creature &c = world_.Get(e.who);
                Must(c.side != Side::Party && !world_.InCombat(e.who),
                     "a turn ended as out of combat for one of the party, or one the game has in combat");
                ++coverage_.noTurns;
                // What it was at is the engine's to finish, and the game's
                // from here: nothing clears it, the game's scripts queue its
                // way home behind it (sys_rubberband_h.nss, Rubber_GoHome:
                // WR_AddCommand, and no clearing), and its walk is no turn's
                // to measure, though the order names it again before it is
                // done. An attack begun in its turn, its creature gone out of
                // the scan's reach on the way to its target, walked on into
                // the next round's padding and was counted as that turn's
                // (the long run with combatants out of reach, seed 73,
                // 2026-10-05). Taken into combat again, it stops as anything
                // of the game's own does (Run).
                if (const auto task = tasks_.find(e.who); task != tasks_.end())
                {
                    task->second.gamesOwn = true;
                }
            }
            beatOf_ = kNoObject;
            break;
        }
        case EventKind::FightOver:
            turnOpen_ = false;
            delayed_.clear();
            break;
        case EventKind::Gone:
            Must(e.who != kNoObject, "an event about someone names no one");
            // Out of the fight alive (12): only one the scan's rules do not
            // find fighting and that the engine's perception does not keep
            // in it, as this frame's scan read it.
            if (e.detail == "no longer fighting")
            {
                const Creature &c = world_.Get(e.who);
                Must(!c.exists || c.down || !c.fighting, "taken out of the fight while the scan finds it fighting");
                Must(!c.exists || c.down || !Perceived(c),
                     "taken out of the fight while the engine's perception keeps it there");
                ++coverage_.leftUnperceived;
                // Out of it alive, it has no turn to come: owed a telling as
                // the fight lets go, unless it is found fighting again (15).
                owed_.insert(e.who);
            }
            break;
        case EventKind::UpAgain:
        case EventKind::Joined:
            Must(e.who != kNoObject, "an event about someone names no one");
            // Into the fight, or back into it (12): only one the scan's
            // rules find fighting. Being perceived brings nobody in.
            if (e.kind == EventKind::Joined || e.detail == "fighting again")
            {
                Must(world_.Get(e.who).fighting, "one the scan does not find fighting brought into the fight");
                owed_.erase(e.who); // its turns are to come: told at each (15)
            }
            break;
        case EventKind::ConditionHeld:
        case EventKind::ConditionEnded:
        case EventKind::ConditionStillHeld:
        case EventKind::Fell:
        case EventKind::CooldownHeld:
        case EventKind::CooldownReleased:
        case EventKind::Refused:
        case EventKind::TurnHandedOver:
        case EventKind::ConditionExpired:
        case EventKind::ActionGivenBack:
        case EventKind::CastCharging:
        case EventKind::CastLanded:
        case EventKind::CastInterrupted:
        case EventKind::ActionPending:
        case EventKind::ActionLanded:
        case EventKind::ActionDropped:
            Must(e.who != kNoObject, "an event about someone names no one");
            break;
        case EventKind::SpeedSet:
        case EventKind::SettingsChanged:
            break;
        }
    }

    // After each frame (2, 3, 4, 7).
    void Check()
    {
        turnsThisFrame_.clear();
        lastCurrent_ = world_.Current(); // a turn closed by the fight's end, next frame, has no event of its own
        FollowTurn();
        const Phase phase = world_.machine.GetPhase();
        if (phase == Phase::Idle || phase == Phase::Suspended)
        {
            // Dropped with the world, or a conversation's: a turn may be
            // left open (to resume), never one that starts meanwhile.
            turnOpen_ = phase == Phase::Suspended && turnOpen_;
        }
        // Effects' time is held exactly while a fight is on (and the setting
        // asks for it): never left held once the fight is over or dropped.
        if (phase == Phase::Idle)
        {
            Must(!timeHeld_, "effects' time left held with no fight on");
        }
        else if ((settings_.effectSecondsPerRound > 0.0f || settings_.turnClock == TurnClock::Time) &&
                 phase != Phase::Starting && !(phase == Phase::Suspended && world_.machine.Order().Round() == 0))
        {
            Must(timeHeld_, "effects' time running in a fight");
        }
        // The game runs at the fight's speed through its turns, anyone's, and
        // at its own through a conversation and with no fight on.
        const bool aiTurn = (phase == Phase::AiThinking || phase == Phase::Executing || phase == Phase::Settling) &&
                            world_.Current() != kNoObject && !PlayerControls(world_.Current());
        const bool ownSpeed = phase == Phase::Idle || phase == Phase::Suspended;
        const bool playersTurn = phase == Phase::AwaitingOrder || phase == Phase::Dispatching ||
                                 (world_.Current() != kNoObject && PlayerControls(world_.Current()) &&
                                  (phase == Phase::Executing || phase == Phase::Settling));
        const auto thousandths = [](float speed) { return static_cast<std::int32_t>(std::lround(speed * 1000.0f)); };
        if (aiTurn)
        {
            Must(world_.speed == thousandths(settings_.fightSpeed), "an AI's turn not at the fight's speed");
        }
        else if (playersTurn)
        {
            Must(world_.speed == thousandths(settings_.fightSpeed), "the player's turn not at the fight's speed");
        }
        else if (ownSpeed)
        {
            Must(world_.speed == 1000, "the game not at its own speed outside a fight's turns");
        }
        if (world_.mode == Mode::Combat)
        {
            const bool choosing = phase == Phase::AwaitingOrder;
            const bool wantsPause =
                phase == Phase::Starting || phase == Phase::TurnStart || (choosing && settings_.pauseOnPlayerTurn);
            const bool wantsRun = phase == Phase::AiThinking || phase == Phase::Executing || phase == Phase::Settling ||
                                  phase == Phase::RoundPadding || phase == Phase::Dispatching ||
                                  (choosing && !settings_.pauseOnPlayerTurn);
            Must(!wantsPause || world_.paused, "the game running while it should wait");
            Must(!wantsRun || !world_.paused, "the game paused while a turn plays out");
        }
        if (world_.machine.Active() && world_.mode == Mode::Combat)
        {
            const ObjectId current = world_.Current();
            // Running, not merely queued: paused, nothing runs, and a
            // creature not yet seen fighting (a joiner before its scan)
            // cannot be stopped before it is seen.
            for (const auto &[id, task] : tasks_)
            {
                if (world_.paused)
                {
                    break;
                }
                const Creature &c = world_.Get(id);
                // One that is not in this fight, the scan's rules not
                // finding it fighting, is the game's own: what it took up in
                // real time before the fight began goes on (an ally told
                // the last fight was over, far from where the next begins:
                // the long run with combatants out of reach, seed 409 with
                // time); and so does what one that has left the fight was
                // set to by a conversation's scripts (seed 215). Found
                // fighting, it joins or is back, and is stopped.
                const Combatant *in = world_.machine.Order().Find(id);
                if (!c.fighting && (in == nullptr || in->down))
                {
                    continue;
                }
                // (A spell held in its conjure, the engine's own, waits
                // through the others' turns: its caster draws it in.)
                // (Nor is one the game does not have in combat held: what it is
                // at is the game's, in anyone's turn (1, 2). A walk its script
                // set it on, until the frame after the game takes it into
                // combat (Run). Or what it began while it fought: a spell
                // kept through its turns landed as its own turn began, the
                // party lost to it by then, and that turn was skipped as
                // unaware with the cast still playing, as the machine has
                // skipped it since issue 29 "whatever the engine has it at":
                // the long run with combatants out of reach, seed 103 with
                // time, 2026-10-05, which none of the fights before the
                // game's own were let through had reached.)
                const bool gamesOwn = task.gamesOwn || (c.side != Side::Party && !world_.InCombat(id));
                if (id != current && !c.down && Classify(task.type) != CommandKind::Involuntary &&
                    !(task.conjuring && c.conjureHeld) && !gamesOwn)
                {
                    const std::string what =
                        "someone other than the current combatant is acting: " + std::to_string(id) + " at a " +
                        Name(task.type);
                    Must(false, what.c_str());
                }
            }
            if (current != kNoObject)
            {
                const Creature &c = world_.Get(current);
                const float budget = settings_.turnClock == TurnClock::Time
                                         ? turnMeters_
                                         : MovementBudget(settings_, c.speed, c.dexterity);
                // Past the budget by the overshoot and the frame the cut is
                // seen in, and the turn's first step, which the machine
                // measures from. Then two tries to reach something, each a
                // step before it is stopped: with the action a move left
                // unspent, and with the action given back (once a turn). A
                // conversation costs two frames more: the one it starts in,
                // which the machine sees as the conversation, and the first
                // after, which it measures from. Seed 624 of the long run
                // took all four.
                const float frame = kRunSpeed * c.speed * static_cast<float>(kFrameMs) / 1000.0f;
                // And each of those two tries may shift its feet a way before
                // it is stopped (kShuffle).
                const float slack = TurnMachine::kOvershoot + 2.0f * TurnMachine::kShuffle +
                                    (4.0f + 2.0f * static_cast<float>(interruptions_)) * frame;
                if (walked_ > budget + slack + 0.01f)
                {
                    const std::string what =
                        "walked past its movement: " + std::to_string(walked_) + " of " + std::to_string(budget);
                    Must(false, what.c_str());
                }
                if (!PlayerControls(current))
                {
                    // An AI's turn has a time limit (14): max_turn_ms of the
                    // running game's time from when the machine began it,
                    // whatever it has been doing; nothing counts its commands
                    // (issue 38: max_commands_per_turn did, and this bound was
                    // by that count, each command to its own limit). Give or
                    // take the frames the arena and the machine count apart,
                    // and two more for a conversation.
                    const std::uint32_t bound = static_cast<std::uint32_t>(settings_.maxTurnMs) +
                                                (4 + 2 * static_cast<std::uint32_t>(interruptions_)) * kFrameMs;
                    Must(!turnFromMs_ || turnRunMs_ - *turnFromMs_ <= bound, "an AI's turn went on past max_turn_ms");
                }
            }
        }
        if (phase == Phase::Idle && ended_ == false && world_.machine.Order().Empty() && coverage_.turns > 0 &&
            world_.mode == Mode::Combat)
        {
            ended_ = true;
            Must(Living(+[](const Creature &c) { return c.side == Side::Enemy && c.fighting; }).empty(),
                 "the fight ended with an enemy still fighting");
        }
        // The fight let the world go with this frame (15): whoever left it
        // alive, or was turned away outside it, and still lives, was told in
        // it. The fallen and the gone are the game layer's to pass over.
        if (phaseBefore_ != Phase::Idle && phase == Phase::Idle)
        {
            for (const ObjectId id : owed_)
            {
                const Creature &c = world_.Get(id);
                if (c.exists && !c.down)
                {
                    Must(toldIdle_.contains(id), "one gone from the fight alive, or turned away outside it, was not "
                                                 "told as the fight let the world go");
                    ++coverage_.toldTurnedAway;
                }
            }
            owed_.clear();
        }
        toldIdle_.clear();
        phaseBefore_ = phase;
        // One that is not of the party, out of the game's combat and with
        // nobody to fight, has no turn (16): the turn it is found so in is
        // over within a frame or two, the frame it is found in and the one
        // that closes it. Not the beat that skips an unaware hostile's turn,
        // which is its own promise (11); nor a frame the machine does not
        // step in (a menu, a conversation).
        const bool played = phase == Phase::AwaitingOrder || phase == Phase::Dispatching ||
                            phase == Phase::AiThinking || phase == Phase::Executing || phase == Phase::Settling;
        const ObjectId whose = world_.Current();
        const bool noTurn = played && world_.mode == Mode::Combat && whose != kNoObject && beatOf_ != whose &&
                            NothingToFight(world_.Get(whose).side, world_.Get(whose).perception);
        noTurnFrames_ = noTurn && whose == noTurnOf_ ? noTurnFrames_ + 1 : (noTurn ? 1 : 0);
        noTurnOf_ = noTurn ? whose : kNoObject;
        Must(noTurnFrames_ <= 2, "a turn went on for one the game does not have in combat, nobody for it to fight");
        // And one the game has made inactive has none (17): no turn of its
        // is being played as a step ends, whoever it is and whatever it was
        // at: the step that finds it so ends it. (One fallen with it ends as
        // the fallen do, a frame on.)
        if (played && world_.mode == Mode::Combat && whose != kNoObject)
        {
            const Creature &c = world_.Get(whose);
            Must(!(c.inactive && c.exists && !c.down), "a turn went on for one the game has made inactive");
        }
    }

    Rng rng_;
    Rng reach_;   // what the game layer measures for a kept attack to be turned (onMeasure): its own stream
    Rng waits_;   // how long an AI's wait is, as the game layer reads it of the command (Try): its own stream
    Rng notice_;  // who has not noticed the party, and when it does (issue 29): its own stream
    Rng asks_;    // when one gone from the fight asks for a command (Asks; issue 43): its own stream
    Rng burrows_; // whom the game makes inactive, and for how long (Burrowing; issue 44): its own stream
    Settings settings_;
    std::unordered_map<ObjectId, std::int32_t> conditionLeftMs_; // the engine's timers, in running time
    std::set<ObjectId> secondsKnown_;             // held with seconds the machine could read, and not yet ended by it
    ObjectId lastTurnStart_ = kNoObject;          // whose turn began last (its event)
    bool timeHeld_ = false;                       // effects' time, as the machine left it
    std::unordered_set<ObjectId> ownClock_;       // creatures put on their own clocks
    std::unordered_set<ObjectId> turnsThisFrame_; // whose turn opened or closed in this frame
    ObjectId lastCurrent_ = kNoObject;            // whose turn it was as the last frame ended
    ObjectId grabbedBefore_ = kNoObject;          // whom the acting creature's own effect held as this frame began
    World world_;
    Coverage &coverage_;
    std::map<ObjectId, Task> tasks_;
    std::set<std::pair<std::int32_t, ObjectId>> started_;
    std::pair<int, ObjectId> turn_{0, kNoObject};
    float walked_ = 0.0f;
    float turnMeters_ = 0.0f; // with time: what the bar would walk as the turn began
    int actions_ = 0;
    int clears_ = 0;
    int interruptions_ = 0; // conversations during this turn
    std::uint32_t turnRunMs_ = 0;
    std::uint32_t heldInTurnMs_ = 0; // the current creature held by a condition, running
    static constexpr std::uint32_t kHeldTurnMs = 1000;
    bool heldAtStart_ = false; // the current creature was at an effect's command no condition names as its turn came
    std::uint32_t heldAtStartMs_ = 0; // and how long that command has held it since, running
    bool heldPaidDue_ = false;  // and its time is still to be checked against the bar, as the machine says it free
    bool turnOpen_ = false;     // a turn's start heard, its end not yet
    bool roundStarted_ = false; // this fight's first round heard
    // Who asked to put a turn off this round: the bar it had, the round,
    // and whether the turn is still to come.
    struct DelayAsked
    {
        std::uint32_t bar = 0;
        int round = 0;
        bool awaited = false;
    };
    std::unordered_map<ObjectId, DelayAsked> delayed_;
    std::unordered_map<ObjectId, std::string> endedWhy_; // why each creature's last turn ended (its event's detail)
    std::map<ObjectId, int> under_;   // those the game has made inactive, and the frames until it makes them active
    ObjectId beatOf_ = kNoObject;     // the unaware hostile whose beat was last said, its turn still open
    std::uint32_t beatFromMs_ = 0;    // turnRunMs_ as it was said
    ObjectId told_ = kNoObject;       // whose AI was told to choose this turn
    std::set<ObjectId> owed_;         // turned away by the gate with no turn to come: owed a telling (15)
    std::set<ObjectId> toldIdle_;     // told to choose with no fight holding the world, in this frame
    Phase phaseBefore_ = Phase::Idle; // the machine's phase as the last frame ended
    ObjectId noTurnOf_ = kNoObject;   // the creature whose turn it is, found with no turn to have (16)
    int noTurnFrames_ = 0;            // and for how many frames running
    bool runners_ = false;            // this fight has combatants that go out of the scan's reach (Wandering)
    std::map<ObjectId, int> away_;    // those gone so, and the frames until each is back or lost for good
    std::uint32_t turnBeganAtMs_ = 0; // turnRunMs_ as the machine began the turn
    // The same, for the turn's time limit (14): not moved as one that had
    // noticed nobody notices, and none until the machine has begun the turn.
    std::optional<std::uint32_t> turnFromMs_;
    static constexpr std::uint32_t kHoldSlackMs = 600;
    int interlude_ = 0;
    int menu_ = 0;
    int joiners_ = 0;
    int leaving_ = 0;
    bool ended_ = false;
    std::deque<std::string> recent_;
};

} // namespace

namespace
{

// Plays fights from seed 1 up, and says what they reached.
Coverage PlayFights(std::uint64_t count, TurnClock clock = TurnClock::Actions, bool strays = false)
{
    Coverage coverage;
    for (std::uint64_t seed = 1; seed <= count; ++seed)
    {
        CAPTURE(seed);
        CAPTURE(strays);
        Arena arena{seed, coverage, clock, strays};
        arena.Play();
    }
    return coverage;
}

} // namespace

TEST_CASE("random fights keep every promise the design makes", "[fights]")
{
    // Eighty fights, about three and a half hours of play, in seconds: enough
    // to reach every state below with every build.
    const Coverage coverage = PlayFights(80);
    // What they reached, each floor about a third of what seeds 1-80 reach
    // (2026-09-30, after the reach check made fights shorter), so a change to the arena that stops reaching a state
    // fails here rather than passing by never getting there.
    CHECK(coverage.fights == 80);
    CHECK(coverage.turns > 700);
    CHECK(coverage.rounds > 150);
    CHECK(coverage.blockedOutOfTurn > 5000);
    CHECK(coverage.movementCuts > 130);
    CHECK(coverage.dispatches > 25);
    CHECK(coverage.menus > 120);
    CHECK(coverage.interludes > 65);
    CHECK(coverage.joiners > 40);
    CHECK(coverage.partyWipes > 7);
    CHECK(coverage.refunds > 85);
    CHECK(coverage.timeouts > 30);
    // Turns lost to an effect no condition named (2026-09-30: 17, each one
    // found so, after two seconds; 2026-10-04: 8, those that held a turn's
    // worth), and holds that let go before, the turn going on (153).
    CHECK(coverage.heldByEffect > 2);
    CHECK(coverage.heldFreed > 45);
    // The game's speed set for an AI's turn and back (2026-09-30: 398).
    CHECK(coverage.speedChanges > 150);
    CHECK(coverage.bledOut > 60);
    CHECK(coverage.vanished > 25);
    CHECK(coverage.tooFar > 30);
    // Conditions in turns (2026-09-30, before a pin was one: 803 landed,
    // 200 turns lost, 136 conditions ended by the machine).
    CHECK(coverage.stunned > 260);
    CHECK(coverage.lostTurns > 65);
    CHECK(coverage.lostOnClock > 5);
    CHECK(coverage.conditionsEnded > 45);
    // Cooldowns in turns (2026-09-30: 142 held, 68 released on their
    // users' turns).
    CHECK(coverage.cooldownsHeld > 45);
    CHECK(coverage.cooldownsReleased > 22);
    // (2026-10-02: 88 turns put off, 74 of them taken; the rest fell or were held first.)
    CHECK(coverage.delays > 40);
    CHECK(coverage.delayedTurns > 30);
    // With one action a turn a wait is a pass, whatever its length (issue 31).
    CHECK(coverage.waitsPaid == 0);
    CHECK(coverage.waitsTooLong == 0);
    // A turn an AI played ended at max_turn_ms (issue 38, 2026-10-04: 43, in
    // the three fights of ten where it has three seconds).
    CHECK(coverage.turnLimits > 14);
}

// With a turn a bar of time (Settings::turnClock), the same fights' kinds
// with their own draws: the promises, 4 and 5 as time has them, and what
// time adds reached.
TEST_CASE("random fights with a turn a bar of time keep the promises too", "[fights]")
{
    const Coverage coverage = PlayFights(80, TurnClock::Time);
    // Each floor about a third of what seeds 1-80 reach (2026-10-02: 3417
    // turns, 722 rounds, 778 kept, 519 landed, 68 whole turns, 19 owed, 74
    // told to choose again, 1627 rollovers, 267 cuts, 166 refunds, 19 too
    // far, 455 lost turns, 118 cooldowns).
    CHECK(coverage.fights == 80);
    CHECK(coverage.turns > 1100);
    CHECK(coverage.rounds > 240);
    CHECK(coverage.kept > 250);
    CHECK(coverage.landed > 170);
    // Spells the engine conjured, held: some let complete as their time was
    // paid, some cleared by the game before.
    // (2026-10-02: 65 given to the engine to conjure, 14 let complete as
    // their time was paid, 32 cleared by the game before.)
    // Grabs, their targets owing the time (2026-10-02: see the run's count).
    CHECK(coverage.grabs > 0);
    CHECK(coverage.conjured > 22);
    CHECK(coverage.conjureLanded > 4);
    CHECK(coverage.conjureCleared > 10);
    CHECK(coverage.wholeTurns > 20);
    CHECK(coverage.owed > 5);
    CHECK(coverage.choseAgain > 24);
    // Time rolls over only from a turn its actor ended (issue 29,
    // 2026-10-04: 1003 turns rolled time over, where 1627 had when every end
    // did; 429 ended with time left that was not their actor's to save.
    // With issue 30's holds, and a limit on an AI's turn where a cap on its
    // commands was, issue 38: 636 and 361).
    CHECK(coverage.rolledOver > 330);
    CHECK(coverage.forfeited > 140);
    // Each creature's own clock moved as its turns were spent.
    CHECK(coverage.ownAged > 1000);
    // A kept attack's creature walking to its target, and a kept action's
    // target gone by the time it would land (2026-10-02: 49 and 61).
    CHECK(coverage.approaches > 15);
    // And of those, a kept attack turned on another in its creature's reach,
    // and a kept action dropped, nobody to turn it on or not an attack
    // (2026-10-03, issue 22: 40 and 38).
    CHECK(coverage.turned > 13);
    CHECK(coverage.targetsGone > 12);
    // A hold shorter than a turn taking its share of a bar (2026-10-02: 304).
    CHECK(coverage.heldShares > 100);
    // An effect's command no condition names, its time taken of the bar as it
    // ran: let go with the turn going on, and held past the bar, the turn
    // lost (issue 30; 2026-10-04, with the waits of issue 31: 170 let go,
    // 148 of them held from the turn's start and checked against the bar, 91
    // turns lost).
    CHECK(coverage.heldFreed > 50);
    CHECK(coverage.heldPaid > 45);
    CHECK(coverage.heldByEffect > 25);
    CHECK(coverage.movementCuts > 85);
    // With time a walk past the bar takes its action's time, and the action
    // is charged on arriving: seldom given back.
    // (2026-10-02: 182 walks on an action's time, 102 arrivals.)
    CHECK(coverage.unplayed > 2); // an action over at once, given back (8)
    CHECK(coverage.walkedOnAction > 60);
    CHECK(coverage.arrivals > 30);
    CHECK(coverage.tooFar > 5);
    CHECK(coverage.lostTurns > 60); // whole turns: most holds are shorter (193)
    CHECK(coverage.cooldownsHeld > 35);
    CHECK(coverage.delays > 40); // 95, and 84 taken
    CHECK(coverage.delayedTurns > 30);
    // An AI's wait taken of its bar, its creature told to choose again, and
    // one longer than what was left, its turn ended (2026-10-04, issue 31:
    // 176 and 27; 273 told to choose again, where 107 were before a wait
    // cost its time. With issue 30's holds taking their time of the bar,
    // the arena's draws other for it: 156 and 49, and 264 told again. With
    // a limit on an AI's turn where a cap on its commands was, issue 38: 137
    // and 50, and 253).
    CHECK(coverage.waitsPaid > 55);
    CHECK(coverage.waitsTooLong > 8);
    // A turn an AI played ended at max_turn_ms (issue 38, 2026-10-04: 84, in
    // the three fights of ten where it has three seconds).
    CHECK(coverage.turnLimits > 28);
}

// The same fights with what the engine's perception decides in them (issue
// 29): about half the hostiles have not noticed the party, and notice when
// struck or by themselves; and now and then a combatant goes where the scan's
// own rules no longer find it fighting, perceived or not, and comes back or
// is lost for good; and now and then the game makes one of the fight inactive
// for a while, anyone of it (issue 44). Promises 10 to 12, and 15 to 17, are
// checked in every fight; here they have something to hold against.
// Twenty-five fights a clock.
TEST_CASE("random fights with hostiles unaware and combatants out of reach keep the promises", "[fights]")
{
    for (const TurnClock clock : {TurnClock::Actions, TurnClock::Time})
    {
        CAPTURE(clock == TurnClock::Time);
        const Coverage coverage = PlayFights(25, clock, true);
        CHECK(coverage.fights == 25);
        // Each floor about a third of what seeds 1-25 reach (2026-10-04, as
        // built and with time: 53 and 47 turns skipped as unaware, 57 and 70
        // hostiles that noticed, 13 and 21 of them in their beat; 122 and
        // 148 combatants out of the scan's reach, 98 and 120 kept in the
        // fight by perception, 43 and 44 out of it, 46 and 59 found fighting
        // again. With issue 43, those gone asking for commands and those the
        // game does not have in combat let through out of turn, the fights
        // take other courses: 62 and 59, 61 and 54, 21 and 11; 140 and 120,
        // 108 and 95, 58 and 48, 57 and 47. And again with issue 44, those
        // the game makes inactive for a while among them: 59 and 75, 48 and
        // 61, 20 and 16; 116 and 162, 133 and 163, 55 and 69, 43 and 75).
        CHECK(coverage.unawareTurns > 15);
        CHECK(coverage.noticed > 19);
        CHECK(coverage.noticedInBeat > 4);
        CHECK(coverage.ranOff > 40);
        CHECK(coverage.stayedAfar > 32);
        CHECK(coverage.leftUnperceived > 14);
        CHECK(coverage.cameBack > 15);
        // A turn an AI played ended at max_turn_ms, among hostiles whose
        // turns are skipped and combatants that come and go (issue 38: 19 as
        // built, 32 with time; 23 and 29 since issue 43; 10 and 30 since
        // issue 44, whose creatures out of the world shorten the others'
        // rounds).
        CHECK(coverage.turnLimits > (clock == TurnClock::Time ? 10 : 3));
        // What the fight holds and what it lets go (1, 15). The commands let
        // through out of turn as the game's own, their creatures not in
        // combat by the game's word: hostiles that have not noticed, and
        // those the game has stood down (384 and 391). The commands the gate
        // turned away of creatures in combat with no turn to come, outside
        // the order or gone from the fight (35 and 60). And those gone from
        // the fight alive, or turned away outside it, that lived as a fight
        // let the world go, each told then (47 and 47). The hundred and sixty
        // default fights, where the game has everyone in combat, let nothing
        // through out of turn, turn 67 and 58 commands away of creatures
        // that then join or fall, and reach no end with one owed.
        CHECK(coverage.gamesOwn > 90);
        // And the turns of those with no turn (16), ended "out of combat":
        // one gone out of the scan's reach in its own turn, the game having
        // stood it down; an ally the party has lost for good, as its turn
        // comes (20 and 29).
        CHECK(coverage.noTurns > 4);
        // And those the game makes inactive for a while (17; issue 44): 132
        // and 152 went under and 122 and 142 were up again before their
        // fight was over; 700 and 422 turns ended "inactive", 73 and 68 of
        // them in the middle, their creatures gone as they played, and 72
        // and 250 with something queued on their creatures that the engine
        // did not run, which is what a turn stood out max_action_ms for.
        CHECK(coverage.wentUnder > 40);
        CHECK(coverage.cameUp > 40);
        CHECK(coverage.inactiveTurns > 140);
        CHECK(coverage.inactiveMidTurn > 23);
        CHECK(coverage.inactiveQueued > 24);
        CHECK(coverage.turnedAway > 13);
        CHECK(coverage.toldTurnedAway > 8);
        // And with time, what rolls over where so many turns are not their
        // actors' to end (272 turns rolled time over, 162 left time that was
        // not saved; 233 and 182 with issue 30's holds and a limit on an AI's
        // turn where a cap on its commands was, issue 38; 155 and 165 since
        // issue 43; 230 and 279 since issue 44).
        if (clock == TurnClock::Time)
        {
            CHECK(coverage.rolledOver > 90);
            CHECK(coverage.forfeited > 54);
        }
    }
}

// One fight by its seed, every line the machine logs printed: a failure's
// replay. `set TB_SEED=63& set TB_CLOCK=time& tb_tests "[fight-seed]"`; with
// `set TB_STRAYS=1` too for one of the fights with hostiles unaware and
// combatants out of reach.
TEST_CASE("one random fight, by the seed in TB_SEED", "[.][fight-seed]")
{
    const auto env = [](const char *name) {
#pragma warning(suppress : 4996) // the C library's getenv: a test's knob, read once
        const char *value = std::getenv(name);
        return std::string{value != nullptr ? value : ""};
    };
    const std::string seed = env("TB_SEED");
    REQUIRE_FALSE(seed.empty());
    Coverage coverage;
    Arena arena{std::strtoull(seed.c_str(), nullptr, 10), coverage,
                env("TB_CLOCK") == "time" ? TurnClock::Time : TurnClock::Actions, !env("TB_STRAYS").empty()};
    arena.echo = true;
    arena.Play();
}

// A thousand more, for a change to the machine: `tb_tests "[fights-long]"`.
// And five hundred a clock with hostiles unaware and combatants out of reach.
TEST_CASE("a thousand random fights keep them too", "[.][fights-long]")
{
    const Coverage coverage = PlayFights(1000);
    CHECK(coverage.fights == 1000);
    const Coverage timed = PlayFights(1000, TurnClock::Time);
    CHECK(timed.fights == 1000);
    const Coverage strays = PlayFights(500, TurnClock::Actions, true);
    CHECK(strays.fights == 500);
    const Coverage timedStrays = PlayFights(500, TurnClock::Time, true);
    CHECK(timedStrays.fights == 500);
}
