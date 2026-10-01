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
#include "core/TurnOrder.h"
#include "core/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
};

struct Observation
{
    std::uint32_t nowMs = 0; // a millisecond clock (GetTickCount)
    Mode mode = Mode::Unknown;
    bool paused = false;
    std::optional<std::vector<Combatant>> scan; // initiative not rolled; the machine rolls it
    std::optional<ActorObservation> actor;
    std::vector<CombatantStatus> statuses;
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
    AgeTime,       // every temporary effect aged b milliseconds, once
    GameSpeed,     // the game runs at b thousandths of its speed (1000: as ever)
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
    // For a use of an ability: which, its cooldown in seconds (the ability
    // table's), and whether it is a sustained one (UseType 2) and already
    // on, so that this use turns it off.
    std::int32_t ability = -1;
    float cooldownSeconds = 0.0f;
    bool sustained = false;
    bool sustainedOn = false;
};

enum class Verdict : std::uint8_t
{
    Allow,
    Block,
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
};

struct CooldownView
{
    ObjectId id = kNoObject;
    std::int32_t ability = -1;
    int turns = 0; // its user's turns still to wait
};

struct MachineState
{
    TurnView view;
    std::uint32_t worldMs = 0;
    std::vector<HeldView> held;
    std::vector<CooldownView> cooldowns;
    std::string lastRefusal;
    float speed = 1.0f;
    // Whether the game took the last speed set (DAOLib's SetSpeed says):
    // filled in by the game side; nothing before any was set.
    std::optional<bool> speedTook;
    bool heldByEffect = false;
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

    // The game's pause key. True if the machine takes it (a turn-based
    // fight is on), in which case it ends a party member's turn and the
    // game must not pause or unpause.
    [[nodiscard]] bool OnPauseKey();

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
    // One frame's move longer than this is a teleport, not walking.
    static constexpr float kTeleport = 5.0f;
    // A target this much beyond the movement left still counts as in reach:
    // the reach is from centre to centre, and an approach stops a little
    // inside it.
    static constexpr float kReachSlack = 0.5f;
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

  private:
    struct Budget
    {
        int actions = 1;
        float movementMax = 0;
        float movementUsed = 0;
        int commands = 0;
        bool outOfMovement = false;
        bool refunded = false; // an action given back once already this turn
        CommandKind lastKind = CommandKind::Involuntary;

        [[nodiscard]] float MovementLeft() const
        {
            return movementUsed >= movementMax ? 0.0f : movementMax - movementUsed;
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
    void HoldUsed(std::vector<Effect> &fx);
    void TickCooldowns(const Combatant &who, std::vector<Effect> &fx);
    void ReleaseCooldowns(std::vector<Effect> &fx);
    void HoldTime(bool held, std::vector<Effect> &fx);
    void SetSpeed(float speed, std::vector<Effect> &fx);
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
    std::string lastRefusal_; // the last refusal logged, and when: repeats within a second are not
    std::uint32_t lastRefusalLogNow_ = 0;
    bool turnBegun_ = false;        // an interlude came in the middle of a turn, not between two
    bool startInterrupted_ = false; // an interlude came before the fight had started
    // Cooldowns in turns (dev/DESIGN.md): the abilities used in the action
    // under way, held once it has run; and those held, with the turns of
    // their user each still waits.
    struct Hold
    {
        ObjectId creature = kNoObject;
        std::int32_t ability = -1;
        int turns = 0;
    };
    std::vector<Hold> usedNow_;
    std::vector<Hold> holds_;
    // Effects' time in rounds (dev/DESIGN.md, "Everything in turns"):
    // whether the fight holds it.
    bool timeHeld_ = false;
    float speedSet_ = 1.0f; // the game's speed as the machine last set it
    Rng rng_;
    LogSink log_;
    LogLevel logLevel_ = LogLevel::Debug;

    Phase phase_ = Phase::Idle;
    TurnOrder order_;
    Budget budget_;
    ObjectId hero_ = kNoObject;

    bool latch_ = false; // a fight ended while the game still says combat: no new one until hostiles are seen
    bool wantScan_ = false;
    bool roundPending_ = false; // the order wrapped: the round's start is handled at the next turn's
    bool endTurnRequested_ = false;
    bool passRequested_ = false;
    bool orderPending_ = false;
    bool resume_ = false; // back from a conversation: the current turn goes on as it was
    std::uint32_t orderSeenNow_ = 0;
    std::uint32_t dispatchUntilNow_ = 0;
    int idleTicks_ = 0;
    std::optional<Vec3> lastPosition_;
    Mode lastMode_ = Mode::Unknown;

    bool haveNow_ = false;
    std::uint32_t lastNow_ = 0;
    std::uint32_t clock_ =
        0; // the world's milliseconds the fight has run unpaused (at the game's speed): deadlines count in this
    std::uint32_t deadline_ = 0;
    std::uint32_t executingSince_ = 0;
    std::uint32_t roundRunMs_ = 0; // milliseconds this round has run unpaused
    std::uint32_t lastStatusNow_ = 0;
    std::uint32_t lastLatchScanNow_ = 0;
    std::uint32_t lastRefusalNow_ = 0;
    bool anyRefusal_ = false;

    std::vector<Effect> pending_; // from the gate, handed out by the next Step
};

} // namespace tb
