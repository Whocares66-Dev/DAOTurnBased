// The turn-based fight as a state machine that never touches the game.
//
// Each frame the game layer (src/game/Bridge.cpp) asks what to observe
// (WhatToObserve), reads that from the engine, hands it to Step, and does
// what Step answers: pause, unpause, clear a creature's commands, select a
// party member, tell a creature's AI to choose, show floating text, signal
// a script event. Between frames, every command any creature is about to
// queue is put to OnCommand, which says whether it may: that is the gate
// that holds everyone but the current combatant still, and spends the
// current one's action and movement.
//
// The machine keeps no lock and calls nothing; the game layer serialises
// Step and OnCommand and applies Step's effects after Step returns, so a
// command queued while an effect is applied (an AI choosing, told to by
// PokeAi) comes back through OnCommand like any other.
//
// dev/DESIGN.md is the design this implements, phase by phase.
#pragma once

#include "core/Events.h"
#include "core/Rules.h"
#include "core/Settings.h"
#include "core/TurnBudget.h"
#include "core/TurnOrder.h"
#include "core/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb
{

// The script events a fight signals the module, for script mods (when
// Settings::scriptEvents). A range no script of the game's uses: its own
// are below 10000, plot events 50000-60000, the run database 80000-81000
// (events_h.nss).
namespace events
{
inline constexpr std::int32_t kCombatStart = 77000; // int 0: combatants
inline constexpr std::int32_t kRoundStart = 77001;  // int 0: round
inline constexpr std::int32_t kTurnStart =
    77002; // object 0: whose turn; int 0: round; int 1: side (0 party, 1 ally, 2 enemy)
inline constexpr std::int32_t kTurnEnd = 77003;   // object 0: whose turn; int 0: round
inline constexpr std::int32_t kCombatEnd = 77004; // int 0: rounds
} // namespace events

enum class Phase : std::uint8_t
{
    Idle,          // no turn-based fight: the game runs as it always does
    Starting,      // combat began: paused, waiting to see who is fighting
    TurnStart,     // between turns: paused, waiting to see who is fighting now
    AwaitingOrder, // a party member's turn: paused, waiting for the player's order
    Dispatching,  // the player gave an order and no command came while paused: running a moment for the engine to queue
                  // it
    AiThinking,   // an AI creature's turn: running, waiting for it to choose
    Executing,    // the current combatant's command is running
    Settling,     // its command ended; the world runs on a moment
    RoundPadding, // the round was short of round_min_seconds: the world runs on, nobody acting
    Suspended,    // a conversation or cutscene mid-fight: hands off until it ends
};

[[nodiscard]] const char *Name(Phase phase);

// What the game layer should read this frame.
struct Needs
{
    bool scan = false;              // everyone fighting near the party (a Combatant each)
    ObjectId actor = kNoObject;     // the one whose turn it is, closely
    std::vector<ObjectId> statuses; // health and standing of these
};

struct ActorObservation
{
    ObjectId id = kNoObject;
    bool exists = false;
    bool down = false;
    bool busy = false;   // it has a current command, or one queued
    bool moving = false; // the engine's IsMoving
    CommandType current = CommandType::Invalid;
    Vec3 position;
};

// What holds a creature, as far as turns go: disabled, it cannot act
// (stunned, paralysed, asleep, knocked down, pinned). A pin is one: the
// engine holds its creature in an effect command, as a stun does, and with
// effects' time held that command outlasts any turn (seen 2026-09-30,
// 13:25: a pinned character's attack queued behind it and never ran).
enum class Condition : std::uint8_t
{
    None,
    Disabled,
};

struct CombatantStatus
{
    ObjectId id = kNoObject;
    bool exists = false;
    bool down = false;
    float health = 1.0f;
    Condition condition = Condition::None;
    float energy = 1.0f; // mana or stamina, current over maximum
    bool mana = false;   // a mage's is mana, anyone else's stamina
    // The seconds its condition has left, as the engine holds them when the
    // game layer can read them (0: not known): with a turn a bar of time, a
    // hold takes its own time of its victim's turns.
    float conditionSeconds = 0.0f;
};

struct Observation
{
    std::uint32_t nowMs = 0; // a millisecond clock (GetTickCount)
    Mode mode = Mode::Unknown;
    bool paused = false;
    std::optional<std::vector<Combatant>> scan; // initiative not rolled; the machine rolls it
    std::optional<ActorObservation> actor;
    std::vector<CombatantStatus> statuses;
    // Creatures whose kept cast or action the game layer did not give when
    // asked to (ReleaseCast): its target was gone. What it owed is given
    // back.
    std::vector<ObjectId> keptVoid;
};

enum class EffectKind : std::uint8_t
{
    Pause,
    Unpause,
    ClearCommands, // target's queue and current command
    Select,        // target becomes the character the player controls
    PokeAi,        // target's AI chooses its next command now (Settings::aiPoke says how)
    Floaty,        // text over target, in colour
    Signal,        // event to the module, target as its object 0, a and b as its ints 0 and 1
    CenterCamera,  // the camera turns to target (game/Camera.h)
    Explain,       // log what target's AI sees: it chose nothing on its turn
    EndConditions, // take target's stuns, paralysis, sleep and pins off: their turns are spent
    Cooldown,      // target's ability a gets b seconds of cooldown (held, released, or back to seconds)
    HoldTime,      // every temporary effect's time held still (a 1) or let run (a 0)
    AgeTime,       // every temporary effect aged b milliseconds, once; with a target, that creature's alone
    OwnClock,      // target's effects, regeneration and timers on a clock of its own (a 1), or the shared one (a 0)
    GameSpeed,     // the game runs at b thousandths of its speed (1000: as ever)
    ReleaseCast,   // target's held cast of ability a is given to it now: it has charged its turns
    DropCast,      // target's held cast is forgotten (interrupted); no target: everyone's
    Approach,      // target walks to where it can do the action kept for it (within reach of its target)
};

[[nodiscard]] const char *Name(EffectKind kind);

struct Effect
{
    EffectKind kind = EffectKind::Pause;
    ObjectId target = kNoObject;
    std::wstring text;
    std::uint32_t colour = 0xFFFFFF;
    std::int32_t event = 0;
    std::int32_t a = 0;
    std::int32_t b = 0;
    // A cooldown's: the seconds its sweep is drawn against, b the seconds
    // left of them (0: as the scripts set one, both the same).
    std::int32_t c = 0;

    [[nodiscard]] static Effect Of(EffectKind kind, ObjectId target = kNoObject)
    {
        Effect effect;
        effect.kind = kind;
        effect.target = target;
        return effect;
    }
};

// Floaty colours, 0xRRGGBB as DisplayFloatyMessage takes them.
inline constexpr std::uint32_t kGold = 0xF0C850;
inline constexpr std::uint32_t kRed = 0xE04838;
inline constexpr std::uint32_t kGrey = 0xC0C0C0;

// A command about to be queued, as the gate sees it.
struct CommandRequest
{
    ObjectId object = kNoObject;
    CommandType type = CommandType::Invalid;
    Source source = Source::Unknown;
    // For an object the fight does not know: whether it is in combat (its
    // combat state), so that a creature joining the fight waits its turn.
    bool inCombat = false;
    // For an attack: the meters its creature must walk to reach the target
    // (WalkToReach), when the game layer could tell. A player's attack that
    // cannot reach this turn is refused before a step is taken.
    std::optional<float> walk;
    // For an attack: the game's own path to the target ends at a shut door
    // (src/game/Gate.cpp). The player's is refused: told to attack through
    // one from a way off, the game queues the door's use and the attack
    // again, and its creature walks a few metres and stands at "use
    // object" for good, the mod on or off (the ninth play test,
    // 2026-10-01). Opening the door first costs no action.
    bool doorInTheWay = false;
    // For a use of an ability: which, its cooldown in seconds (the ability
    // table's), and whether it is a sustained one (UseType 2) and already
    // on, so that this use turns it off.
    std::int32_t ability = -1;
    float cooldownSeconds = 0.0f;
    bool sustained = false;
    bool sustainedOn = false;
    // And its conjuring time in seconds (the ability's Speed, through the
    // game's table of conjuring speeds: 0 instant, 1 fast, 2 normal, 4
    // slow, 6 ritual): a turn's worth or more of it is charged in turns.
    float conjureSeconds = 0.0f;
    // With a turn a bar of time (dev/REBALANCE.md, "Actions"): how long the
    // action takes when the game layer could tell, an attack's swing by the
    // game's formula (SwingSeconds), 0 for a stand-in by kind
    // (ActionCostMs). And of an ability: its table's AbilityType (a spell, an
    // item's: a potion), its conjure animation (ConjureAnim) and whether its
    // caster holds a staff, for its conjuring (ConjureSeconds), the
    // conjuring speed's count being conjureSeconds above.
    float actionSeconds = 0.0f;
    std::int32_t abilityType = 0;
    std::int32_t conjureAnim = 0;
    bool staff = false;
    // The object it is aimed at, if any (an attack's, an ability's): last,
    // since tests fill the fields above by place.
    ObjectId target = kNoObject;
};

// With a turn a bar of time: what an action costs of it, in milliseconds.
// An attack its swing, an ability its cast and its conjuring, an item's its
// drink; turning a mode off nothing (dev/REBALANCE.md, "Actions").
[[nodiscard]] std::uint32_t ActionCostMs(const CommandRequest &request);

enum class Verdict : std::uint8_t
{
    Allow,
    Block,
    // Not queued now, and kept by the game layer to give again: a cast
    // that charges (TurnMachine::ChargeTurns). EffectKind::ReleaseCast asks
    // for it, DropCast forgets it.
    Hold,
};

enum class ToggleResult : std::uint8_t
{
    Enabled,
    Disabled,
    RefusedInCombat,
};

// What a turn bar would show (dev/UI.md): the turns to come, whose it is,
// what they have left. A copy, safe to hand to another thread.
struct CardView
{
    ObjectId id = kNoObject;
    std::wstring name;
    std::wstring tag;
    Side side = Side::Enemy;
    int rank = 0;
    float health = 1.0f;
    bool down = false;
    bool current = false;
    bool yours = false; // the player commands it on its turns
    int round = 0;
    float energy = 1.0f; // mana or stamina
    bool mana = false;
    // With a turn a bar of time: what an action of its still owes, kept for
    // its next turn or run already (0: none). The card says so.
    std::uint32_t owedMs = 0;
    // It has put its turn off (Delay), and the turn is still to come this
    // round: the card says "waits". The floaty over it was never seen, the
    // camera leaving for the next turn in the same frame (play test 28).
    bool waits = false;
    // What is pending, where owedMs says something is: the ability's id, or
    // -1 for an attack. The game layer adds the ability's icon (a texture's
    // name) and its name as the player reads it, for the card to show: the
    // icon in the round marker's ring, in place of the seconds, which "does
    // not really tell us anything" (the user, 2026-10-02).
    std::int32_t pendingAbility = -1;
    std::wstring pendingIcon;
    std::wstring pendingName;
};

struct TurnView
{
    bool active = false;
    Phase phase = Phase::Idle;
    int round = 0;
    ObjectId current = kNoObject;
    bool playerTurn = false;
    int actionsLeft = 0;
    float movementLeft = 0;
    float movementMax = 0;
    // With a turn a bar of time: the bar, and what is left of it.
    bool timed = false;
    std::uint32_t timeMs = 0;
    std::uint32_t timeLeftMs = 0;
    // And a turn's own seconds (the bar's are those, Combat Tactics' and
    // what rolled over); the pace the current creature walks at, and the
    // meters it may walk for nothing (the free meter left, and meters
    // paid for): what a walk would cost (core/Preview.h, CostOf).
    std::uint32_t turnMs = 0;
    float paceMps = 0;
    float paidMeters = 0;
    // What this turn paid, as it began, for an action begun on the turn
    // before: why its bar starts short.
    std::uint32_t pendingPaidMs = 0;
    // The turn may be put off to the round's end now (TurnMachine::CanDelay);
    // and whether Delay is offered on this turn at all (the setting, a
    // party member's turn): the bar keeps the button's place, greyed, when
    // it is offered and cannot be had, so that End does not move under the
    // hand (play test 28).
    bool canDelay = false;
    bool delayOffered = false;
    // The player has the game paused (the pause key): the bar says so.
    // Nothing else on the screen did (play test 29).
    bool playerPaused = false;
    // What of this turn's bar rolled over from the turn before: why it is
    // longer than a turn's seconds.
    std::uint32_t rolledMs = 0;
    std::vector<CardView> upcoming; // the current turn first
};

// The machine's whole state, as a test server reads it (eluvian-agent's
// inspect kind turnbased.state; its JSON is core/State.h's).
struct HeldView
{
    ObjectId id = kNoObject;
    std::wstring name;
    Condition seen = Condition::None;
    int turnsOwed = 0;
    bool onClock = false; // ended, and the engine held it still: it runs out on the clock
    // With a turn a bar of time: the time of its own clock it still owes.
    std::uint32_t owedMs = 0;
};

struct CooldownView
{
    ObjectId id = kNoObject;
    std::int32_t ability = -1;
    int turns = 0; // its user's turns still to wait
    int of = 0;    // of this many in all
    // With a turn a bar of time: what is left of it on its user's own clock,
    // and the whole it counts down from, in milliseconds (0 as built).
    std::uint32_t leftMs = 0;
    std::uint32_t ofMs = 0;
};

struct ChargeView
{
    ObjectId id = kNoObject;
    std::int32_t ability = -1;
    int turns = 0; // its caster's turns still to go before it is cast
    // With time: what it still owes, paid out of its creature's next turns.
    std::uint32_t owedMs = 0;
};

struct MachineState
{
    TurnView view;
    std::uint32_t worldMs = 0;
    std::vector<HeldView> held;
    std::vector<CooldownView> cooldowns;
    std::vector<ChargeView> charging;
    std::string lastRefusal;
    float speed = 1.0f;
    // Whether the game took the last speed set (DAOLib's SetSpeed says):
    // filled in by the game side; nothing before any was set.
    std::optional<bool> speedTook;
    bool heldByEffect = false;
    bool playerPaused = false; // the player has the game paused (the pause key)
};

enum class LogLevel : std::uint8_t
{
    Debug,
    Info,
    Warn,
};
using LogSink = std::function<void(LogLevel, std::string_view)>;

class TurnMachine
{
  public:
    TurnMachine(Settings settings, std::uint64_t seed, LogSink log = {});

    [[nodiscard]] const Settings &GetSettings() const
    {
        return settings_;
    }
    // New rules apply from the next turn; `enabled` from the next fight.
    void SetSettings(const Settings &settings);

    [[nodiscard]] Needs WhatToObserve(std::uint32_t nowMs);
    [[nodiscard]] std::vector<Effect> Step(const Observation &observation);

    // The gate.
    [[nodiscard]] Verdict OnCommand(const CommandRequest &request);

    // The player gave an order through explore mode (a click to move, an
    // ability chosen). With `order_run_ms`, if no command follows while the
    // game is paused, it runs a moment so the engine can queue one
    // (Phase::Dispatching). A command of the player's at the gate, taken or
    // refused, answers the order.
    void OnPlayerOrder(std::string_view what);

    // The game's pause key (Space). It pauses, as it does in the game: the
    // user, 2026-10-02, "space bar should still pause the game i think,
    // instead of end turn. that's what players would expect" (it ended the
    // turn until then). True if the machine takes it (a turn-based fight is
    // on): the game must not toggle its pause itself, since the machine
    // holds the game's pause through a fight and would undo it; the machine
    // pauses and unpauses for the player instead (PlayerPaused). While the
    // machine holds the world still for its own reasons (between two turns,
    // or through the player's choosing with pause_on_player_turn) the key
    // changes nothing. An order given while paused runs the game again: its
    // action has to play out.
    [[nodiscard]] bool OnPauseKey();
    [[nodiscard]] bool PlayerPaused() const
    {
        return playerPaused_;
    }

    // End turn: the turn bar's End, its key, a tool. True if a turn-based
    // fight is on; a party member's turn that the player plays ends, its
    // own action first if one is playing out.
    [[nodiscard]] bool OnEndTurn();

    // Delay (Settings::delay): the party member whose turn it is puts it
    // off to the round's end. Only before it has spent anything, once a
    // round, and with somebody standing after it. True if taken; the turn
    // passes on with the next frame.
    [[nodiscard]] bool OnDelay();
    [[nodiscard]] bool CanDelay() const;

    // Turn-based on or off: refused while fighting. partyInCombat is the
    // game's own word for it (the hero's combat state), which covers a menu
    // opened over a real-time fight, when the mode is the menu's.
    [[nodiscard]] ToggleResult RequestToggle(bool partyInCombat = false);

    // The least important line the log wants. Lines below it are not even
    // formatted: the gate is asked about every command every creature
    // tries, and most of what it says about them is debug.
    void SetLogLevel(LogLevel minimum)
    {
        logLevel_ = minimum;
    }

    // A fight is on and the gate is shut to all but the current combatant.
    [[nodiscard]] bool Active() const;
    [[nodiscard]] Phase GetPhase() const
    {
        return phase_;
    }
    [[nodiscard]] const TurnOrder &Order() const
    {
        return order_;
    }
    [[nodiscard]] TurnView View() const;
    [[nodiscard]] MachineState State() const;

    // Initiative's rolls from now on (a joiner's, the next fight's): a test
    // run repeats with the same seed.
    void Reseed(std::uint64_t seed);

    // After an error between the machine's step and the engine's taking its
    // effects: the effects that let the game go (unpaused, its clock and
    // speed its own, cooldowns released), said whatever the machine believes
    // it said before, and no turns until the next fight. Nothing if it
    // stood down already.
    [[nodiscard]] std::vector<Effect> StandDown(std::string_view why);

    // From the game (DAOLib's command events): a creature's command ended
    // in a failure that did nothing, `why` the failure's name. The current
    // creature's action, spent when the gate let it through, is given back
    // once a turn, as for an approach cut short. True if it was.
    bool OnCommandFailed(ObjectId who, CommandType type, std::int32_t ability, std::string_view why);

    // A creature was struck (the scripts' EVENT_TYPE_DAMAGED, once the
    // damage is done): a spell it has pending is interrupted by physical
    // damage over `threshold` (core/Rules.h, InterruptThreshold), as the
    // game interrupts one being conjured. Answers whether it was.
    bool OnDamaged(ObjectId target, float damage, bool physical, float threshold);
    // Whether a blow could interrupt anything of this creature's: asked
    // before the threshold is worked out, which costs script calls.
    [[nodiscard]] bool Conjuring(ObjectId who) const;

    // Typed events, as they happen (core/Events.h): called under the same
    // lock as everything else, from inside Step and the gate's calls.
    void SetEventSink(EventSink sink)
    {
        events_ = std::move(sink);
    }

    // Movement closer than this to the end of the budget is none.
    static constexpr float kMinStep = 0.5f;
    // How far past its budget a creature may go before it is stopped: the
    // frame it is noticed in, and the engine's stopping distance.
    static constexpr float kOvershoot = 0.3f;
    // An action's own approach is stopped for want of movement only after
    // this much of it. An action taken with the movement spent is at
    // something in reach (the gate refuses the player's otherwise), and its
    // creature still shifts its feet to strike: Eleanor, 0.0 m left, a
    // soldier 2.0 m away, had her Shield Bash cleared 9 ms after it was
    // let through, "out of movement", the action gone (the seventh play
    // test, 2026-10-01).
    static constexpr float kShuffle = 1.0f;
    // One frame's move longer than this is a teleport, not walking.
    static constexpr float kTeleport = 5.0f;
    // A target this much beyond the movement left still counts as in reach.
    // None: the walk is a straight line, and the path is never shorter. At
    // 0.5 m an attack needing 9.3 m with 9.0 left was let through and cut
    // short 4 m from its target, the movement's own cut being 0.3 m past
    // (the third play test, 2026-09-30).
    static constexpr float kReachSlack = 0.0f;
    // An order with no command after this long, paused, has queued nothing
    // (and with `order_run_ms` is dispatched: the game runs until a command
    // comes, or that long has passed).
    static constexpr std::uint32_t kDispatchAfterMs = 100;
    // An ability held on cooldown is given this many seconds: longer than any
    // turn waits in real time. Released on its user's turn, or turned back
    // into seconds when the fight ends.
    static constexpr std::int32_t kCooldownHoldSeconds = 3600;
    // How long a turn waits on an effect's command it found its creature
    // in before the turn is lost (heldByEffect_).
    static constexpr std::uint32_t kEffectGraceMs = 2000;
    // An action whose time the engine was read to take, a second or more,
    // and whose command was over in under a quarter of it, did not play:
    // its time is given back (OverAtOnce).
    static constexpr std::uint32_t kLeastToldMs = 1000;
    static constexpr std::uint32_t kOverAtOnce = 4;
    // An action running on its own effect's time ends when that time has
    // run, and has this many times max_action_ms to do it in: the pounce
    // took 9.7 s of the 12 (seen in play, 2026-10-01), and a longer one
    // would have been cut short.
    static constexpr std::uint32_t kOwnEffectPatience = 3;
    // The same order again this soon after it was taken is the game's own
    // repeat of it, not the player's: Eleanor's attack, and the dog's, came
    // a second time 31 to 78 ms after the first, was refused for want of an
    // action and said so over her head, though she attacked (the user,
    // 2026-09-30; twelve times in that session's log). A second click is
    // slower than this.
    static constexpr std::uint32_t kEchoMs = 250;

  private:
    struct Budget
    {
        // What the turn has to spend and has spent (core/TurnBudget.h): one
        // action and meters as built, or a bar of time.
        TurnBudget spend = TurnBudget::Actions(1, 0.0f);
        std::uint32_t lastCostMs = 0;    // what the last action let through was charged
        std::uint32_t pendingPaidMs = 0; // what this turn paid, as it began, for an action kept for it
        std::uint32_t rolledMs = 0;      // what of its bar rolled over from the turn before
        // The walk in hand is the mod's own, to a kept attack's target
        // (EffectKind::Approach): where the meters paid for end, it stops
        // without a word.
        bool approachWalk = false;
        // An action's time, to be taken when its creature gets there: its
        // walk ran past the bar on the time the action had been charged (an
        // archer walking for its shot, a path longer than the gate could
        // tell), and that time went to the walk.
        std::uint32_t onArrivalMs = 0;
        // An action whose own effect holds its creature (a spider's
        // Overwhelm, an ogre's grab) is charged the time it runs: what of
        // its own cost still covers that time (none: no such effect yet).
        std::optional<std::uint32_t> ownEffectCover;
        int commands = 0;
        bool outOfMovement = false;
        // With own clocks: how far this turn has moved its creature's
        // effects on (AgeOwnClock, and the steps of its action's own
        // effect), so the turn's end moves them the rest of a turn's
        // seconds and no further.
        std::uint32_t agedOwnMs = 0;
        bool refunded = false; // an action given back once already this turn
        int givenBack = 0;     // how many times this turn
        bool retried = false;  // an AI told to choose again, its failed action given back
        bool landing = false;  // the turn began with a charged cast: its creature's turn follows it
        CommandKind lastKind = CommandKind::Involuntary;
        std::int32_t lastAbility = -1; // the ability the last command was to use; -1 for any other command
        float movementAtCommand = 0;   // meters walked as the last command was let through
        float lastWalk = 0; // and the meters it had to walk to its target (an attack's), as the gate was told
        ObjectId lastTarget = kNoObject;             // what the last command let through was aimed at
        CommandType lastType = CommandType::Invalid; // the last command let through, and when
        std::uint32_t lastAt = 0;
        // And when on the world's clock, and whether its time was the
        // engine's own reading (CommandRequest::actionSeconds), not a
        // stand-in: one over in a fraction of that time did not play.
        std::uint32_t lastClock = 0;
        bool lastMeasured = false;

        [[nodiscard]] float MovementLeft() const
        {
            return spend.MovementLeft();
        }
    };

    std::uint32_t Delta(std::uint32_t nowMs);
    void StepIdle(const Observation &obs, std::vector<Effect> &fx);
    void StepSuspended(const Observation &obs, std::vector<Effect> &fx);
    void StepStarting(const Observation &obs, std::vector<Effect> &fx);
    void StepTurnStart(const Observation &obs, std::vector<Effect> &fx);
    void StepAwaiting(const Observation &obs, std::vector<Effect> &fx);
    void StepDispatching(const Observation &obs, std::vector<Effect> &fx);
    void StepThinking(const Observation &obs, std::vector<Effect> &fx);
    void StepExecuting(const Observation &obs, std::vector<Effect> &fx);
    void StepSettling(const Observation &obs, std::vector<Effect> &fx);
    void StepPadding(std::vector<Effect> &fx);
    [[nodiscard]] Verdict Judge(const CommandRequest &request);
    [[nodiscard]] bool LoseTurn(const Combatant &who, std::vector<Effect> &fx);
    // A cast that charges (the user's rule, 2026-10-01): the turns an
    // ability charges before it is cast; a turn of its caster's spent on
    // one; the cast given again at the start of the turn it is cast on;
    // and its loss.
    [[nodiscard]] int ChargeTurns(const CommandRequest &request) const;
    [[nodiscard]] bool ChargingTurn(const Combatant &who, std::vector<Effect> &fx);
    // With a turn a bar of time (Settings::turnClock).
    [[nodiscard]] bool Timed() const;
    [[nodiscard]] std::uint32_t TurnMs() const;
    [[nodiscard]] int TacticsOf(const Combatant &who) const;
    [[nodiscard]] TurnBudget NewSpend(const Combatant &who) const;
    [[nodiscard]] std::optional<Verdict> PayAction(const Combatant &who, const CommandRequest &request);
    // What a cooldown's and an effect's seconds are counted in.
    [[nodiscard]] float CooldownTurnSeconds() const;
    [[nodiscard]] float EffectRoundSeconds() const;
    // There is an action spent this turn to give back (its time, with time).
    [[nodiscard]] bool ActionToGiveBack() const;
    void GiveActionBack();
    [[nodiscard]] std::string BudgetLeft() const;
    void KeptVoid(ObjectId who);
    // With time: an action that has run owes `ms` more to its creature's
    // next turns.
    void Owe(ObjectId who, std::uint32_t ms, CommandType type, std::int32_t ability);
    void ChargeOnArrival(std::vector<Effect> &fx);
    void DelayTurn(std::vector<Effect> &fx);
    void OverAtOnce(std::vector<Effect> &fx);
    void TellTurnStart(const Combatant &who, std::vector<Effect> &fx);
    void LeaveTurn();
    // With time: the current creature's clock has moved on (its bar spent,
    // or its turn over); what counts on it is moved with it.
    void MoveClock(std::vector<Effect> &fx);
    void AgeCooldowns(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx);
    // A creature's own clock moved on: its cooldowns, and with own clocks
    // its effects, regeneration and timers (the game layer's to move).
    void AgeOwnClock(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx);
    [[nodiscard]] bool OwnClocks() const;

  public:
    // What the acting creature's last command let through was aimed at, while
    // its own effect runs (a grab's victim): its clock moves with the
    // creature's. For the tests' arena.
    [[nodiscard]] ObjectId OwnEffectTarget() const
    {
        return ownEffectSince_ ? budget_.lastTarget : kNoObject;
    }

  private:
    void PutOnOwnClocks(std::vector<Effect> &fx);
    [[nodiscard]] bool LandCast(const Combatant &who, std::vector<Effect> &fx);
    void Interrupt(ObjectId who, std::string_view why, std::vector<Effect> &fx);
    void HoldUsed(std::vector<Effect> &fx);
    void TickCooldowns(const Combatant &who, std::vector<Effect> &fx);
    void ReleaseCooldowns(std::vector<Effect> &fx);
    void HoldTime(bool held, std::vector<Effect> &fx);
    void SetSpeed(float speed, std::vector<Effect> &fx);
    // The fight's speed ([turns] speed), anyone's turn.
    [[nodiscard]] float TurnSpeed() const;
    void SpendRound(std::vector<Effect> &fx);
    void HoldAtTurnStart(const Combatant &who, std::vector<Effect> &fx);

    void StartFight(std::vector<Combatant> roster, std::vector<Effect> &fx);
    void StartNextTurn(std::vector<Effect> &fx);
    void BeginTurn(const Combatant &who, std::vector<Effect> &fx);
    void ResumeTurn(const Combatant &who, std::vector<Effect> &fx);
    void Handover(const Combatant &who, std::vector<Effect> &fx);
    void EndTurn(std::vector<Effect> &fx, std::string_view why);
    void End(std::vector<Effect> &fx, std::string_view why);
    void Merge(const std::vector<Combatant> &scan, std::vector<Effect> &fx);
    void ApplyStatuses(const std::vector<CombatantStatus> &statuses);
    void TrackMovement(const ActorObservation &actor, std::vector<Effect> &fx);
    void EnforcePause(const Observation &obs, std::vector<Effect> &fx) const;
    bool PlayerControls(const Combatant &c) const;
    bool TurnSpent() const;
    bool ActorGone(const Observation &obs) const;
    void Floaty(std::vector<Effect> &fx, ObjectId target, std::wstring text, std::uint32_t colour) const;
    void Signal(std::vector<Effect> &fx, std::int32_t event, ObjectId object, std::int32_t a, std::int32_t b) const;
    void Refuse(ObjectId target, std::wstring text, CommandType command, std::string note = {});
    void Log(LogLevel level, std::string_view text) const;
    void Refused(LogLevel level, std::string text);
    [[nodiscard]] bool Logs(LogLevel level) const
    {
        return log_ && level >= logLevel_;
    }
    std::string DescribeOrder() const;

    Settings settings_;
    // Conditions in turns (dev/DESIGN.md, "Conditions in turns"): what each
    // creature was last seen held by, and how many of its turns it still
    // owes to it. Kept apart from the order, which each scan rewrites.
    struct Held
    {
        Condition seen = Condition::None;
        Condition kind = Condition::None;
        int turns = 0;
        // With a turn a bar of time (dev/REBALANCE.md, "Holds"): a hold
        // uses up its victim's own time, its seconds as the engine had them
        // when it landed. What is still owed: a turn's seconds or more is a
        // turn lost, less is that share of the next turn's bar.
        std::uint32_t owedMs = 0;
        // Ended by the machine, and the engine held it still (the dog's pin
        // in the desktop play test, 2026-09-30): it runs out on the game's
        // clock, each turn lost while it lasts, no fresh turns owed.
        bool onClock = false;
    };
    ObjectId endedFor_ = kNoObject; // whose conditions the machine ended this turn
    std::unordered_map<ObjectId, Held> held_;
    bool currentStunned_ = false; // the current combatant was stunned in its own turn
    // The current combatant was found in an effect's command it did not
    // choose, before acting: a condition the statuses do not name (a grab,
    // say) holds it as a stun does, and with effects' time held its command
    // may outlast the turn (a pin's did, 2026-09-30).
    bool heldByEffect_ = false;
    // The current creature as the frame saw it: at a turn's start, the one
    // whose turn begins (the turn-start frame observes it). An AI held by
    // an effect is not told its turn has come, since it would choose at
    // once and queue behind the effect (the arena, seed 127 of the long
    // run, 2026-09-30).
    std::optional<ActorObservation> seenActor_;
    EventSink events_;
    void Emit(EventKind kind, ObjectId who = kNoObject, std::string detail = {}, std::int32_t value = 0,
              std::string command = {}, std::string note = {}) const;
    std::string lastPlayerRefusal_; // what the player was last told, for the state: "Too far (attack: ...)"
    // When a round last aged effects (real ms, lastNow_'s clock): a hold
    // gone soon after ran out on the clock; gone any other time while
    // effects' time is held, something took it off (a cure, a dispel).
    std::optional<std::uint32_t> agedAtNow_; // none: no round has aged effects yet
    // Whose turn is to be handed over once the statuses are fresh: its kept
    // action will not come (KeptVoid).
    std::optional<ObjectId> handoverWanted_;
    // The player's turn ends at this time (the real clock's), an action of
    // its creature's kept for the next: a beat for "Lands next turn" to be
    // read before the next turn's announcement lands on it (play test 27).
    std::optional<std::uint32_t> pendingEndNow_;
    static constexpr std::uint32_t kPendingSayMs = 800;
    // Meters a walk to a kept attack's target may run past what was paid
    // for: the game's walk ends a little inside the reach, by its own path.
    static constexpr float kApproachSlack = 1.0f;
    static constexpr std::uint32_t kAgeSettleMs = 1500;
    std::string lastRefusal_; // the last refusal logged, and when: repeats within a second are not
    std::uint32_t lastRefusalLogNow_ = 0;
    // With time: how far the current creature's clock had moved this turn
    // when what counts on it was last moved (MoveClock).
    std::uint32_t clockSeenMs_ = 0;
    bool turnBegun_ = false;        // an interlude came in the middle of a turn, not between two
    bool startInterrupted_ = false; // an interlude came before the fight had started
    // With time, a kept attack's creature walks to its target this turn
    // (EffectKind::Approach): the walk the game layer gives it is let
    // through, whoever plays the creature, and the turn ends as it settles.
    bool approach_ = false;
    // With time: a turn is on whose creature's clock runs through it (taken,
    // lost or spent on a pending action; not a fallen one's, passed over).
    bool clockRuns_ = false;
    // Cooldowns in turns (dev/DESIGN.md): the abilities used in the action
    // under way, held once it has run; and those held, with the turns of
    // their user each still waits.
    struct Hold
    {
        ObjectId creature = kNoObject;
        std::int32_t ability = -1;
        int turns = 0;
        int of = 0; // the turns it was held for in all: the sweep is turns over this
        // With a turn a bar of time, the cooldown counts on its user's own
        // clock, which moves a turn's seconds a turn of its user's, as its
        // bar is spent (dev/REBALANCE.md, "The creature's own clock"): it
        // lifts at the moment it runs out, mid-turn included.
        std::uint32_t leftMs = 0;
        std::uint32_t ofMs = 0;
    };
    std::vector<Hold> usedNow_;
    std::vector<Hold> holds_;
    // Casts that charge. The user's rule: a spell whose conjuring time is a
    // turn's worth or more charges that many of its caster's turns, the one
    // it is ordered on the first, and is cast at the start of the turn
    // after: slow (4 s at 4 s a turn) one turn and cast on the second,
    // ritual (6 s) two and on the third; instant, fast and normal on the
    // turn. A caster held (a stun, a knockdown, paralysis, sleep), fallen
    // or gone before then loses it, nothing spent. Cast, it costs no action
    // of that turn: the caster then has its turn.
    //
    // With a turn a bar of time, the same keeps an action that does not fit
    // what is left of its creature's bar (PayAction): it lands as a later
    // turn has paid what it owes, and its walk to its target (an attack's),
    // paid for in that, is walked for nothing. An action the game layer
    // cannot keep (an item's use) runs at once, and only what it owes is
    // kept, to be paid.
    struct Charge
    {
        ObjectId caster = kNoObject;
        std::int32_t ability = -1;
        int turns = 0; // its caster's turns still to go, the next included
        std::uint32_t owedMs = 0;
        CommandType type = CommandType::UseAbility;
        bool kept = true;
        bool spell = false; // a spell being conjured: a blow may interrupt it (OnDamaged)
    };
    std::vector<Charge> charges_;
    std::optional<Charge> landing_; // given again this turn, and not yet come through the gate
    bool chargeBegan_ = false;      // a cast began to charge: its caster's turn ends with the next step
    // Effects' time in rounds (dev/DESIGN.md, "Everything in turns"):
    // whether the fight holds it.
    bool timeHeld_ = false;
    float speedSet_ = 1.0f; // the game's speed as the machine last set it
    Rng rng_;
    LogSink log_;
    LogLevel logLevel_ = LogLevel::Debug;

    Phase phase_ = Phase::Idle;
    TurnOrder order_;
    ObjectId hero_ = kNoObject;
    Budget budget_;

    bool latch_ = false;     // a fight ended while the game still says combat: no new one until hostiles are seen
    bool stoodDown_ = false; // after an error (StandDown): no turns until the game has left combat
    // Combatants a scan did not find, and since when: one is out of the
    // fight only after kAbsentMs of it. In the user's eight fights a guard
    // and a soldier were gone from one scan and back in the next every three
    // seconds for ten minutes (311 "up again" against 39 falls, read off
    // Eluvian's bus, 2026-09-30), losing whatever turn came while gone.
    std::unordered_map<ObjectId, std::uint32_t> absentSince_;
    // With time: what each creature's last turn left over, to its cap.
    std::unordered_map<ObjectId, std::uint32_t> rollover_;
    static constexpr std::uint32_t kAbsentMs = 4000;
    // Those that left the fight alive (missing from the scans for
    // kAbsentMs): down in the order, and only a scan that finds one again
    // brings it back. Its status, read every frame, said "alive", which
    // brought it up again until the next scan put it down: the flapping.
    std::unordered_set<ObjectId> left_;
    // The last refusal an event was emitted for, and when: the same one
    // again within a second is not another event. In the user's eight
    // fights 1,097 came, up to 64 in a second (a follower's move asked for
    // every frame, a movement key held with no movement left), which
    // drowned a test agent's wait and filled Eluvian's bus (2026-09-30).
    struct RefusedEvent
    {
        ObjectId who = kNoObject;
        CommandType command = CommandType::Invalid;
        std::wstring text;
        std::uint32_t at = 0;
    };
    std::optional<RefusedEvent> refusedEvent_;
    static constexpr std::uint32_t kRefusedEventMs = 1000;
    // Fighting near, but not the party (Combatant::engaged): said once each.
    std::unordered_set<ObjectId> outside_;
    // A scan asked for by a creature outside the order, and when: one
    // waiting outside the fight asks with every command it tries.
    std::optional<std::uint32_t> outsiderScanNow_;
    static constexpr std::uint32_t kOutsiderScanMs = 500;
    bool wantScan_ = false;
    bool roundPending_ = false; // the order wrapped: the round's start is handled at the next turn's
    bool endTurnRequested_ = false;
    bool delayRequested_ = false;
    bool playerPaused_ = false; // the player's own pause (OnPauseKey), over the machine's
    bool passRequested_ = false;
    bool orderPending_ = false;
    bool resume_ = false; // back from a conversation: the current turn goes on as it was
    std::uint32_t orderSeenNow_ = 0;
    std::uint32_t dispatchUntilNow_ = 0;
    int idleTicks_ = 0;
    std::optional<Vec3> lastPosition_;
    Mode lastMode_ = Mode::Unknown;

    bool haveNow_ = false;
    bool anyRefusal_ = false;
    // The statuses are wanted with the next step, whatever their time
    // (WhatToObserve): before an AI is told to choose again in mid-turn.
    bool statusWanted_ = false;
    std::uint32_t lastNow_ = 0;
    std::uint32_t clock_ =
        0; // the world's milliseconds the fight has run unpaused (at the game's speed): deadlines count in this
    std::uint32_t deadline_ = 0;
    std::uint32_t executingSince_ = 0;
    std::uint32_t roundRunMs_ = 0; // milliseconds this round has run unpaused
    // Effects' time spent inside the round by an action that needed it: an
    // effect of the action's own that keeps its creature at it. A giant
    // spider's Overwhelm is one effect on the spider and one on its prey,
    // each ended by its time, and effects' time is held through a fight: the
    // pounce never ended, the world stood twelve seconds until
    // max_action_ms stopped it, and the turn ended "took too long" (the
    // eighth play test, 2026-10-01). While the acting creature is at an
    // effect's command after its own action, effects' time runs with the
    // world, a step at a time, and comes off the round's worth, so a round
    // is still effect_seconds_per_round of it (more only if an action took
    // more).
    std::uint32_t roundAgedMs_ = 0;
    std::optional<std::uint32_t> ownEffectSince_; // the world's clock as the action's own effect was last aged
    static constexpr std::uint32_t kOwnEffectStepMs = 250;
    // A creature at a walk it does not take: at a move, or at the use of a
    // door or the like (whose walk is a step of the command), and not a
    // step taken in this long. It is stopped: the engine would leave it
    // there (the door above), and max_action_ms is twelve seconds of the
    // world standing and the turn lost. An attack or a talent stands still
    // while it swings, and is not judged so.
    std::optional<Vec3> stoodAt_;
    std::uint32_t stoodSince_ = 0;
    static constexpr std::uint32_t kStoodMs = 2500;
    static constexpr float kStoodStill = 0.05f;
    // An attack whose creature stands this far short of where its walk was
    // to end is not swinging: it cannot get there.
    static constexpr float kStuckShort = 1.0f;
    // And is given this long standing there, not kStoodMs: nobody else moves
    // in its turn, so what blocks it will not clear, and the engine's own
    // answer comes only after seconds (blocked spiders' turns were 4 to 6 s
    // still: play test 31).
    static constexpr std::uint32_t kStuckMs = 1000;
    // An AI whose actions have been given back this many times in a turn
    // has nothing it can get to: its turn ends.
    static constexpr int kGivenBackEnough = 2;
    std::uint32_t lastStatusNow_ = 0;
    std::uint32_t lastLatchScanNow_ = 0;
    std::uint32_t lastRefusalNow_ = 0;

    std::vector<Effect> pending_; // from the gate, handed out by the next Step
};

} // namespace tb
