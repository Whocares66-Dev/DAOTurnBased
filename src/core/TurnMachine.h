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
#include "core/PartyTactics.h"
#include "core/Rules.h"
#include "core/Settings.h"
#include "core/TurnBudget.h"
#include "core/TurnOrder.h"
#include "core/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
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
    // With a scan: the enemies and allies standing in the fight. Each is to
    // be in the scan while it lives, whether or not the scan's own rules for
    // who is fighting still find it (Combatant::fighting says which), with
    // the engine's perception between it and the party: one out of combat or
    // beyond combat_radius stays in the fight while that perception holds
    // (core/Rules.h, StaysInTheFight; issue 29). The machine names who, the
    // game says what is so of each, and core decides.
    std::vector<ObjectId> inFight;
};

struct ActorObservation
{
    ObjectId id = kNoObject;
    bool exists = false;
    bool down = false;
    // It has a current command, or one queued. The queue's half is for a
    // command the gate has let through and the engine has not yet begun: a
    // command becomes the current one when its object's command step starts
    // it (DAOLib's reading of the queue, dao/Addresses.h), and the machine
    // takes two frames with nothing for "its command is over". Read by the
    // current command alone, a command slow to begin would be over before it
    // began, and with a turn a bar of time "over at once: it did nothing",
    // its time given back (OverAtOnce). How many frames lie between is only
    // roughly measured (dev/DESIGN.md: in one fight 227 of 234 attacks were
    // at their command within a frame and a half, seven took up to 161 ms),
    // and the game layer now says at debug when it was two or more. A queue
    // the engine does not run at all is another matter, and the engine has a
    // word for the one case seen: inactive, below (issue 44, where this was
    // weighed).
    bool busy = false;
    bool moving = false; // the engine's IsMoving
    CommandType current = CommandType::Invalid;
    Vec3 position;
    bool conjureHeld = false; // at a conjure the game layer holds (Verdict::Conjure)
    bool timersHeld = false;  // its scripts' timers held for it (a damage over time's ticks)
    // At an attack: its command is still at its approach, the walk to where
    // it strikes from, as the engine itself says (the sub-action its command
    // is at: DAOLib's dao::commands::RunningSubAction, 4 for a melee
    // approach and 15 for a ranged one; the blow comes after). False at the
    // blow, and at no attack. What the machine tells an attacker that
    // cannot get to its target from one that is striking by (StepActing):
    // until 2026-10-04 it worked that out for itself, from a walk it had
    // reckoned, and stopped 67 shots in one fight (issue 11).
    bool approaching = false;
    // The engine's perception between it and the party, and of anyone it
    // would fight, as of this frame (core/Types.h; the scan reads the same
    // of everyone as a turn begins, Combatant::perception): a hostile that
    // perceives nobody to fight is unaware, and is not told its turn has
    // come (core/Rules.h, Unaware). Read each frame so that it acts from the
    // moment it notices.
    Perception perception;
    // At an effect's command: the effects standing on the creature as the
    // engine has them (each one's type, the ability that applied it, who
    // did, and the seconds it has left), in words for the log. Until
    // 2026-10-05 the machine said only "an effect" of what held a creature,
    // and against the high dragon 37 of the party's 176 turns were lost to
    // effects nobody had looked at (issue 30; the user: "We should read the
    // data since that's available").
    std::string effects;
    // At an effect's command: how long the engine's own count has that
    // command still to run, in seconds, and whether that is the whole of it
    // (DAOLib's dao::commands::HeldFor: the sub-actions left on the command,
    // their delays and timed time left). Not known, the seconds are "at
    // least": a sub-action left is untimed or has not begun (an animation
    // takes its length only as it begins), or is timed and spent yet still
    // there, or none is left of a command still current. Both zero where
    // the engine says nothing of it.
    //
    // Said in the machine's lines beside what the hold then took, and
    // nothing is decided by it. What a hold costs is the time it ran
    // (TurnMachine::HoldRuns), which needs no count ahead; and the engine
    // times only part of a hold ahead. Against the high dragon (2026-10-04)
    // a knockback's one sub-action, timed to the effect's 1.2 s, was spent
    // as the turn began and the fall and rise then held 0.61 to 3.19 s, on
    // no list (at least 0); after the roar the count was some 2.0 s where
    // the hold ran 2.25 to 2.53. And a creature whose stun the machine has
    // just ended stands a frame more at a command whose count means nothing.
    float heldSeconds = 0.0f;
    bool heldKnown = false;
    // The game has made it inactive, as of this frame: its scripts'
    // SetObjectActive(FALSE), and the engine's own word for it,
    // GetObjectActive, which the game layer reads here as it reads a
    // creature's combat state. Out of the world: not drawn, given no AI
    // updates, its commands cleared as it went, and whatever is queued on it
    // since not run until it is made active again ("Messages will continue
    // to queue up so you are able to assign actions before enabling the
    // object": script.ldf). It has no turn while it is so, and one made so in
    // the middle of its turn has that turn end there (issue 44;
    // TurnMachine::InactiveNow). Anyone's: a party member's is read the same.
    bool inactive = false;
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
    // Its movement rate effects as they stand on it now: Haste, a salve, a
    // slow (core/Types.h). This look is where they are read, a scan does not
    // read them: it comes as every turn begins, as each action ends and
    // every half second between, so a creature's pace is never older than
    // that (core/Rules.h, Pace; issue 49).
    MovementEffects movement;
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
    // asked to (ReleaseCast): its target was gone, and for a plain attack
    // nobody it could be turned on was in reach (Effect::others). What it
    // owed is given back.
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
    ReleaseCast,   // target's held cast of ability a is given to it now: it has charged its turns (others: Effect)
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
    // A kept plain attack's release (ReleaseCast): those it may be turned on
    // if its target has fallen, the standing combatants of the other side,
    // in the order's order (core/Rules.h, NearestInReach; issue 22). None
    // for a kept talent or spell, which is dropped then: its target was its
    // caster's choice.
    std::vector<ObjectId> others;

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
    // For an attack: the meters its creature must walk before it strikes,
    // as the engine's own approach would walk them (DAOLib's
    // dao::attack::Walk: none with the target in its reach), when the game
    // layer could tell. A player's attack that cannot reach this turn is
    // refused before a step is taken.
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
    // For a wait: how long it waits, in seconds, the command's own number as
    // the game layer read it of the engine (the scripts' CommandWait(2.0):
    // DAOLib's dao::Command::WaitMilliseconds); 0 where it could not be
    // read. With a turn a bar of time an AI's wait costs that much of its
    // creature's bar (TurnMachine::PayWait; issue 31).
    float waitSeconds = 0.0f;
    // The object it is aimed at, if any (an attack's, an ability's): last,
    // since tests fill the fields above by place.
    ObjectId target = kNoObject;
    // For an attack: the engine itself has no attack to make for the two as
    // they stand, and which of its answers that was (NoAttack, core/Types.h;
    // src/game/Gate.cpp asks, of every attack as it comes). Refused before
    // a step, nothing spent: the player is told why, and an AI's is given
    // back and its AI told to choose again (TurnMachine::NoAttackToMake;
    // issue 27). After the target, for the same reason.
    NoAttack noAttack = NoAttack::None;
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
    // Let through now, the engine's own cast, and its conjure held by the
    // game layer (DAOLib's HoldConjure) until ReleaseCast lets it complete,
    // or DropCast clears it: a spell that conjures, with time
    // (Settings::engineConjure).
    Conjure,
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
    // A party member's: its tactics play its turns, as chosen for it or as
    // `[turns] party` says (core/PartyTactics.h; issue 46). Of its next turn:
    // `yours` on the current card is of the turn under way, which is played
    // out the way it began, so the two may differ there until that turn ends.
    bool tacticsPlay = false;
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
    // What the bar's caption says for a while in place of what the turn has
    // left: an action of a party member's that came to nothing and was given
    // back, and why (core/GivenBack.h; issue 21); or an attack of one's the
    // engine has no way to make, refused before a step (issue 27). Empty
    // when there is nothing to say.
    std::wstring notice;
    // Every turn to come, the current one first, each fighter once: all of
    // them, however many fight. Cut to a row's worth until 2026-10-03, when
    // those past it were on no card and in no count, and a tool that read
    // the order lost a party member for four rounds (issue 23). Which of
    // them the bar's row shows is core/BarWindow.h's.
    std::vector<CardView> upcoming;
    // How many cards the bar's row has room for. Twelve until 2026-10-03:
    // with twelve the bar's buttons lay under the minimap, and the user, of
    // the strip made to give way for them: "Maybe instead of shifting the
    // portraits we have 10 instead of 12? ... Large fights like these are
    // going to overflow anyway, 12 is not even enough to show everything",
    // then "let's make it 10" (issue 14).
    static constexpr std::size_t kShown = 10;
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

    // Whose turns their tactics play (core/PartyTactics.h; issue 46): a
    // choice a companion, by its tag, kept for the playthrough; where none
    // was made, `[turns] party` says, as it always did.
    //
    // A choice applies from that companion's next turn (the user,
    // 2026-10-05: "Should be changeable even during battle, applies to
    // character's next turn"). A turn begins where the machine hands it out
    // (BeginTurn: the log's "round N: X's turn (party, yours)" or "(party,
    // AI)"), and from there to its end it is played by the choice as it
    // stood then (Budget::begun), whatever is chosen meanwhile. A turn put off
    // (Delay) is handed out again when it comes at the round's end, and
    // begins then: nothing of it was spent. One a conversation interrupted
    // goes on as it began.
    //
    // `[turns] party` itself is read as ever, in a turn under way too: a
    // companion nothing was chosen for as its turn began has that turn handed
    // over at once when the setting changes (SetSettings), as since the
    // second play test.
    //
    // Never the hero (Follower::hero, Combatant::hero): nothing is kept for
    // it, and nothing kept for its tag is asked.
    //
    // ChooseTactics says once in the log what was chosen and from when, with
    // who asked (`by`: "the turnbased_auto tool", "the tactics page's box");
    // TacticsOf only answers.
    TacticsAnswer ChooseTactics(const Follower &who, bool on, std::string_view by);
    [[nodiscard]] TacticsAnswer TacticsOf(const Follower &who) const;
    // What the tactics page's box shows for each of these (BoxFor): for
    // everyone the page can be of, asked in one go by the frame while the
    // page is open.
    [[nodiscard]] TacticsBoxes BoxesOf(std::span<const Follower> party) const;
    // The choices of a playthrough that has come into the world (a save
    // loaded: its own, or none for one from before this; a new game: none),
    // in place of whatever was chosen before; and the choices now, for the
    // save being written.
    void SetPartyTactics(PartyTactics choices);
    [[nodiscard]] const PartyTactics &GetPartyTactics() const
    {
        return partyTactics_;
    }

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
    // once a turn, as for an approach cut short. True if it was. `target` is
    // what the failed command was aimed at, when the game layer says: for
    // the log's line alone ("X's attack on Y failed"), and nothing is
    // decided by it (issue 27).
    bool OnCommandFailed(ObjectId who, CommandType type, std::int32_t ability, std::string_view why,
                         ObjectId target = kNoObject);

    // How long the bar's caption says that a party member's action was
    // given back and why (TurnView::notice), of the real clock: the world
    // may stand still while the player reads it. Long enough to read a line
    // of fifty letters twice; gone sooner if the player's next order is let
    // through, since it is of the order before.
    static constexpr std::uint32_t kNoticeMs = 5000;

    // A creature was struck (the scripts' EVENT_TYPE_DAMAGED, once the
    // damage is done): a spell it has pending is interrupted by physical
    // damage over `threshold` (core/Rules.h, InterruptThreshold), as the
    // game interrupts one being conjured. Answers whether it was.
    bool OnDamaged(ObjectId target, float damage, bool physical, float threshold);
    // A spell held in its conjure by the engine (Verdict::Conjure) whose
    // command ended before its time was paid: cleared by the installed
    // rules_core's damage rule, or by something that holds its caster. The
    // spell is lost: what its time paid stays spent, the rest is not owed.
    // Answers whether there was one.
    bool OnConjureCleared(ObjectId caster);
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
    // (No length of the mod's for a creature at an effect's command: until
    // 2026-10-04 a turn waited two seconds on one, kEffectGraceMs, and was
    // then lost whole, and a creature getting up from a hold the machine had
    // ended was let be for five, kGetUpMs. What holds a creature costs the
    // time the engine holds it: HoldRuns, issue 30.)
    // How long "Unaware" stands over a hostile that has noticed nobody to
    // fight before its turn is skipped (unaware_), of the fight's clock (the
    // world's time, as an AI's thinking is: at twice the speed half as long
    // on the wall). The user, 2026-10-04 (issue 29): "We can show text
    // unaware above the enemy's head, pause for about a second, then skip
    // their turn."
    static constexpr std::uint32_t kUnawareMs = 1000;
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
        // Whose turn was handed out (BeginTurn), and what was chosen for it
        // then, of whose tactics play its turns (nothing chosen: it began by
        // the setting, and goes by the setting). The turn is played out by
        // that, whatever is chosen meanwhile (PlayerControls; issue 46).
        // None in a turn passed over, and gone with the turn, as all of this
        // is.
        struct Begun
        {
            ObjectId who = kNoObject;
            std::optional<bool> chosen;
        };
        std::optional<Begun> begun;
        // With time: the pace the bar walks at now, its creature's against a
        // person's (core/Rules.h, Pace), and a person's meters a second as
        // the turn began ([turns] pace: a setting changed in a turn is the
        // next turn's). The first follows its creature's Haste and slows
        // through the turn (FollowPace; issue 49).
        float pace = 1.0f;
        float personMps = 0.0f;
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
        // Held at an effect's command it did not choose (heldByEffect_,
        // HoldRuns): the world's clock as the hold was last charged (none:
        // no hold is being charged), how long holds have run this turn, how
        // much of that the shared effects' clock is still to be moved by, and
        // of the hold under way how long it has run and what it took of the
        // bar, for the log.
        std::optional<std::uint32_t> heldSeen;
        std::uint32_t heldMs = 0;
        std::uint32_t heldUnagedMs = 0;
        std::uint32_t holdRanMs = 0;
        std::uint32_t holdBarMs = 0;
        // Commands of the turn's own let through. Nothing bounds the number
        // (issue 38: max_commands_per_turn is gone, and a turn's time is its
        // limit); it is kept for whoever asks whether anything was: an
        // effect's command is a hold on a creature that has started nothing
        // and its action's own effect on one that has (StepExecuting), a
        // turn is put off only before its first (CanDelay), an order just
        // taken that comes again is its echo (Judge), and the camera goes to
        // an AI's creature at the first. A wait is not one: paid for or not,
        // it is refused, and none is counted.
        int commands = 0;
        bool outOfMovement = false;
        // With own clocks: how far this turn has moved its creature's
        // effects on (AgeOwnClock, and the steps of its action's own
        // effect), so the turn's end moves them the rest of a turn's
        // seconds and no further.
        std::uint32_t agedOwnMs = 0;
        // A spell let through to conjure (Verdict::Conjure): the turn ends
        // as its caster reaches the conjure the game layer holds.
        bool conjureWaiting = false;
        // What this turn's own-effect steps added to its target's debt
        // (grabbedMs_): taken back if the action is stopped as too long.
        std::uint32_t grabbedNowMs = 0;
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
    // A creature the game has made inactive has no turn (issue 44): whether
    // it is so now, and its turn passed over as it comes. True if it was.
    [[nodiscard]] bool InactiveNow(const Combatant &who) const;
    [[nodiscard]] bool InactiveTurn(const Combatant &who, std::vector<Effect> &fx);
    // And a turn already begun ended there, its creature made inactive in the
    // middle of it.
    void EndInactive(const Combatant &who, std::vector<Effect> &fx);
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
    // As a turn begins: the pace its meters were made from, and what was
    // read of its creature's Haste and slows.
    void SayPace(const Combatant &who);
    // With time, in a turn under way: the bar walks at its creature's pace
    // as the last look at everyone found it, said once as it changes.
    void FollowPace();
    // What was read of a creature's Haste and slows, at debug.
    void SayMovementEffects(const Combatant &who, std::string_view when) const;
    float RememberRate(ObjectId who, float rate);
    [[nodiscard]] static float BarPace(const Combatant &who);
    [[nodiscard]] std::optional<Verdict> PayAction(const Combatant &who, const CommandRequest &request);
    // With time, an AI's wait takes its own length of its creature's bar:
    // whether it was paid for (then its AI chooses again), or is the pass a
    // wait was before, which ends the turn.
    [[nodiscard]] bool PayWait(const Combatant &who, const CommandRequest &request);
    // What a cooldown's and an effect's seconds are counted in.
    [[nodiscard]] float CooldownTurnSeconds() const;
    [[nodiscard]] float EffectRoundSeconds() const;
    // There is an action spent this turn to give back (its time, with time).
    [[nodiscard]] bool ActionToGiveBack() const;
    void GiveActionBack();
    [[nodiscard]] std::string BudgetLeft() const;
    void KeptVoid(ObjectId who);
    // Whether the target of the kept action being given stands in the fight
    // by the machine's own word, though the game layer did not find it there
    // to be struck: one the game has made inactive (issue 44). For the log's
    // word of why the action was turned or dropped, and nothing else.
    [[nodiscard]] bool KeptTargetStands() const;
    // With time: an action that has run owes `ms` more to its creature's
    // next turns.
    void Owe(ObjectId who, std::uint32_t ms, CommandType type, std::int32_t ability);
    void ChargeOnArrival(std::vector<Effect> &fx);
    void DelayTurn(std::vector<Effect> &fx);
    void OverAtOnce(std::vector<Effect> &fx);
    void TellTurnStart(const Combatant &who, std::vector<Effect> &fx);
    void LeaveTurn();
    // Between two turns, the world running for what the last one's clock
    // brought due to land on its creature (landFromNow_).
    [[nodiscard]] bool Landing() const;
    // With time: the current creature's clock has moved on (its bar spent,
    // or its turn over); what counts on it is moved with it.
    void MoveClock(std::vector<Effect> &fx);
    void AgeCooldowns(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx);
    // A creature's own clock moved on: its cooldowns, and with own clocks
    // its effects, regeneration and timers (the game layer's to move).
    void AgeOwnClock(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx);
    [[nodiscard]] bool OwnClocks() const;
    // What holds the current creature at an effect's command costs the time
    // it holds it (issue 30): charged as it runs, the turn lost only once it
    // has outlasted the bar; and the hold over, said with what it took.
    [[nodiscard]] bool HoldRuns(const ActorObservation &actor, std::vector<Effect> &fx);
    void HoldLetGo(const Combatant &who, std::vector<Effect> &fx);
    void ChargeHold(std::vector<Effect> &fx);
    [[nodiscard]] std::uint32_t HoldTurnMs() const;
    [[nodiscard]] std::uint32_t Patience(std::uint32_t ms) const;
    // A turn its AI plays that is still going after max_turn_ms: ended.
    [[nodiscard]] bool TurnTooLong(std::vector<Effect> &fx);
    // What an action stopped as too long added to its target's debt.
    void ForgiveGrab();
    [[nodiscard]] bool EngineConjures(const CommandRequest &request) const;

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
    void GrabAtTurnStart(const Combatant &who, std::vector<Effect> &fx);

    void StartFight(std::vector<Combatant> roster, std::vector<Effect> &fx);
    void StartNextTurn(std::vector<Effect> &fx);
    void BeginTurn(const Combatant &who, std::vector<Effect> &fx);
    void ResumeTurn(const Combatant &who, std::vector<Effect> &fx);
    void Handover(const Combatant &who, std::vector<Effect> &fx);
    // The turn waits on the player's order from here, and says so
    // (EventKind::AwaitingOrder): `why` is the event's detail.
    void AwaitOrder(std::string_view why);
    void EndTurn(std::vector<Effect> &fx, std::string_view why);
    void End(std::vector<Effect> &fx, std::string_view why);
    // The fight lets the world go: those that left it alive (left_) and
    // whoever the gate turned away outside it (turnedAway_) are told to
    // choose, once each, unless told with this step already.
    void LetGo(std::vector<Effect> &fx);
    void Merge(const std::vector<Combatant> &scan, std::vector<Effect> &fx);
    void ApplyStatuses(const std::vector<CombatantStatus> &statuses, std::vector<Effect> &fx);
    void TrackMovement(const ActorObservation &actor, std::vector<Effect> &fx);
    void EnforcePause(const Observation &obs, std::vector<Effect> &fx) const;
    // Whether the player plays this combatant's turns: a party member's,
    // unless its tactics do (OnTactics, below). Of the one whose turn is
    // under way, by the choice as that turn began (Budget::begun).
    bool PlayerControls(const Combatant &c) const;
    // Whether its tactics play a combatant's turns from its next one: a
    // companion's, as chosen for it, else as `[turns] party` says.
    [[nodiscard]] bool OnTactics(const Combatant &c) const;
    // The party's own where the machine reaches for the hero and the hero
    // may have fallen (issue 42, whose fault was one creature standing for
    // the party though it was dead): the hero, while standing; else the
    // first of the party standing, in the order's order. `except` is passed
    // over; kNoObject with nobody.
    [[nodiscard]] ObjectId PartysOwn(ObjectId except = kNoObject) const;
    bool TurnSpent() const;
    bool ActorGone(const Observation &obs) const;
    void Floaty(std::vector<Effect> &fx, ObjectId target, std::wstring text, std::uint32_t colour) const;
    void Signal(std::vector<Effect> &fx, std::int32_t event, ObjectId object, std::int32_t a, std::int32_t b) const;
    void Refuse(ObjectId target, std::wstring text, CommandType command, std::string note = {});
    // An attack the engine itself has no way to make as the two stand:
    // refused before a step (issue 27). True if it is.
    [[nodiscard]] bool NoAttackToMake(const Combatant &who, const CommandRequest &request, bool player);
    // The same for an attack kept from the turn before, as it is given to
    // its creature: not made, what this turn paid for it given back.
    void KeptAttackNotMade(const Combatant &who, const CommandRequest &request, bool player);
    [[nodiscard]] std::string Aimed(ObjectId target) const;
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
    // The current combatant is at an effect's command it did not choose:
    // whatever the effect is (a knockback, a grab, the getting up from a stun
    // the machine ended), the engine holds it, as it says by where it has
    // the creature. Its orders are refused meanwhile, its AI not told, what
    // it kept not given, and the time it is held is its turn's (HoldRuns).
    bool heldByEffect_ = false;
    // The current combatant is a hostile that has noticed nobody to fight
    // (core/Rules.h, Unaware), in the beat its turn is skipped after
    // (kUnawareMs): "Unaware" is over it, its AI has not been told its turn
    // has come, and the world runs. Over as it notices (its AI is told
    // then), as a command of its own is let through all the same (a
    // script's order), or as the beat ends and its turn with it.
    bool unaware_ = false;
    [[nodiscard]] bool UnawareNow(const Combatant &who) const;
    // A creature that is not of the party and that the game does not have in
    // combat has no turn (issue 43; the user, 2026-10-04, of a fight the
    // party runs from: "the enemies should go back to normal"). At a turn's
    // start that was so already for a hostile that has noticed nobody, whose
    // turn is skipped after its beat (unaware_). It is so through the turn:
    // found out of combat while its turn is being played, by a script's
    // command for it at the gate (Judge) or by the frame's look at it (Step;
    // core/Rules.h, NothingToFight), its turn ends there and then, "out of
    // combat", nothing rolled over; and what its script has it do is the
    // game's, let through and charged to nothing. An action of its own still
    // running is the engine's to finish. Back in combat by its next turn, it
    // has that turn like anyone.
    //
    // In the Dead Trenches (2026-10-05) the game took a shriek out of combat
    // while the turn was the shriek's own: "Shriek's wait of 3.00 s is longer
    // than the 0.19 s it has left: its turn ends", "Shriek: move to location
    // from script held, its turn is over: it has passed". The wait and the
    // walk were the game's sending it home (sys_rubberband_h.nss,
    // Rubber_GoHome), read as its AI's pass and as a command after it;
    // fifteen seconds after the fight it stood 17.5 m from where it began. A
    // genlock had the same line in the same fight.
    //
    // Asked for by the gate, taken up with the next step.
    bool outOfCombat_ = false;
    [[nodiscard]] bool NothingToFightNow(const Combatant &who) const;
    // A turn is being played: handed to its player or its AI, at its command,
    // or settling after it. Not between two turns.
    [[nodiscard]] bool InTurn() const;
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
    // A party member's action that came to nothing and was given back: the
    // words over the character, as ever, and the bar's caption's line for
    // kNoticeMs from `at` (lastNow_'s clock), which the view carries (issue
    // 21: with the floaty alone the same order was tried again and again).
    void SayGivenBack(std::vector<Effect> &fx, const Combatant &who, CommandType type, std::wstring_view words);
    struct Notice
    {
        std::wstring text;
        std::uint32_t at = 0;
    };
    std::optional<Notice> notice_;
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
    // With own clocks: a turn's end ran its creature's clock the rest of
    // the way at this time (the real clock's), and something is held for
    // it (a poison). What that brought due comes on the game's next update;
    // with the world held still between turns, that was the next turn's
    // first moment, on another's turn: Lyna's poison from the Elven Ruins'
    // gas landed as Tamlen's turn began (2026-10-02). The next turn waits
    // kLandMs with the world running, and it lands on its own creature,
    // the camera still there.
    std::optional<std::uint32_t> landFromNow_;
    static constexpr std::uint32_t kLandMs = 300;
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
        bool spell = false;  // a spell being conjured: a blow may interrupt it (OnDamaged)
        bool engine = false; // the engine's own command, held in its conjure (Verdict::Conjure)
        // What it was aimed at as it was kept: an attack given again at
        // another was turned on it by the game layer, its own target fallen
        // (Effect::others), and the log and its event say so.
        ObjectId target = kNoObject;
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
    // Whose turns their tactics play, as chosen in this playthrough (issue
    // 46). What the turn under way began by is the turn's own
    // (Budget::begun).
    PartyTactics partyTactics_;

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
    // With time: how long each creature was held by another's action
    // while that action ran (a grab, Overwhelm), owed of its own turns
    // (GrabAtTurnStart, LoseTurn).
    std::unordered_map<ObjectId, std::uint32_t> grabbedMs_;
    // What each creature's movement rate effects came to when its pace was
    // last said, where that was not 1: as its last turn began, or, with
    // time, as they changed in it. Its pace is said as it changes, and once
    // more as its effects end (SayPace, FollowPace; issue 49). This fight's.
    std::unordered_map<ObjectId, float> paceRates_;
    static constexpr std::uint32_t kAbsentMs = 4000;
    // Those that left the fight alive (missing from the scans for
    // kAbsentMs): down in the order, and only a scan that finds one again
    // brings it back. Its status, read every frame, said "alive", which
    // brought it up again until the next scan put it down: the flapping.
    // Each with the side it left as: a fight that ends with no enemy
    // standing says whether they fell or left it (NoEnemyLeft). In the Dead
    // Trenches (2026-10-05) "the turn-based fight is over after 11 rounds: no
    // enemy left standing" was said of six that the party had run from, all
    // alive. One dropped from the order as the round turns has no side there
    // any more.
    std::unordered_map<ObjectId, Side> left_;
    [[nodiscard]] std::string NoEnemyLeft() const;
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
    // Those in combat whose command the gate turned away while they were not
    // in the order: one no scan has found, or found yet; one dropped from it
    // since it left. A command turned away never ends, and for a creature
    // that is not of the party a command's end is the one thing that runs
    // its AI again (rules_core.nss: on first perceiving a hostile "Initial
    // call - all others should be when commands are completed";
    // EVENT_TYPE_COMMAND_COMPLETE runs AI_DetermineCombatRound; struck in
    // combat, EVENT_TYPE_ATTACKED and EVENT_TYPE_DAMAGED move its threat and
    // run no AI). A combatant has that mended at its turn, when it is told to
    // choose; these have no turn, so the fight owes each the same telling as
    // it lets the world go (LetGo; issue 43). In Ortan Thaig (2026-10-04)
    // two spirits and a golem came into combat beside a party member 70 m
    // from the party's leader, where the scan did not look (issue 42): two
    // commands each were turned away, the scans asked for found none of
    // them, and with the fight over they stood in combat with nothing queued
    // for more than eight minutes, one of them cut from 230 health to 97
    // with no answer. One that joins the order is forgotten here: its turns
    // are to come.
    std::unordered_set<ObjectId> turnedAway_;
    // In the fight by the engine's perception alone, the scan's rules for
    // who is fighting no longer finding them (Combatant::fighting false,
    // StaysInTheFight): said once each, and again if it happens again.
    std::unordered_set<ObjectId> afar_;
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
    // (WhatToObserve): as an action ends, so that the cards follow what it
    // did at once (issue 23); and before a turn goes on after one, the
    // player waited on again or an AI told to choose again in mid-turn.
    bool statusWanted_ = false;
    std::uint32_t lastNow_ = 0;
    std::uint32_t clock_ =
        0; // the world's milliseconds the fight has run unpaused (at the game's speed): deadlines count in this
    std::uint32_t deadline_ = 0;
    std::uint32_t executingSince_ = 0;
    // The clock as the turn began: what max_turn_ms is counted from
    // (TurnTooLong).
    std::uint32_t turnSince_ = 0;
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
    // Why a spell the engine conjured, held, was lost (OnConjureCleared).
    static constexpr std::string_view kConjureCleared = "interrupted while conjuring";
    // An attack whose command is still at its approach
    // (ActorObservation::approaching) and whose creature takes no step is
    // not swinging: it cannot get there. An attack at its blow stands still
    // while it turns to face, draws, aims and strikes, and is never judged
    // so: that it will strike is the engine's to say, and it says it by
    // where the command is.
    //
    // Until 2026-10-04 the machine told the two apart by a walk the gate had
    // reckoned from the path's length: an archer in range whose path was the
    // longer, or a skeleton in reach with others to walk round, stood "short
    // of where its walk was to end" while it shot or swung, and was stopped
    // a second in (issue 11: 67 shots in one fight).
    //
    // It is given this long standing there, not kStoodMs: nobody else moves
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
