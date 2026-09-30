// Random fights, and what must hold in every one of them. SkyrimFollowerTactics'
// rule (its dev/PRINCIPLES.md): random sequences, with something that knows
// the answer. Here the answer is the design's promises (dev/DESIGN.md),
// checked on every frame and every command:
//
//   1. the gate: while a fight holds the world, only the combatant whose turn
//      it is may start anything of its own; with none held, everything goes;
//   2. only that combatant has anything of its own running;
//   3. the game is paused exactly when a turn waits on the player or between
//      turns, and runs otherwise;
//   4. nobody walks further in a turn than its movement, give or take the
//      frame it is stopped in;
//   5. nobody spends two actions in a turn, unless stopped short of the first
//      and given it back;
//   6. each combatant starts at most one turn a round;
//   7. an AI's turn ends, and a fight ends when no enemy is left fighting;
//   8. every effect names who it is for, the AI is told to choose only on its
//      own turn, and only party members are selected.
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
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
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
    bool story = false; // given during a conversation: the cutscene's, not a turn's
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
    int stunned = 0;     // creatures stunned or pinned
    int lostTurns = 0;   // turns taken by a stun
    int pinnedTurns = 0; // turns taken without movement by a pin
    int conditionsEnded = 0;
    int cooldownsHeld = 0;
    int cooldownsReleased = 0;
};

class Arena
{
  public:
    Arena(std::uint64_t seed, Coverage &coverage)
        : rng_(seed), settings_(RandomSettings(rng_)), world_(settings_, RandomFight(rng_), seed), coverage_(coverage)
    {
        world_.record = false;
        world_.machine.SetLogLevel(LogLevel::Info); // the turns, for a failure's message
        world_.onPoke = [this](ObjectId id) { Decide(id, false); };
        world_.onEffect = [this](const Effect &e) { Saw(e); };
        // The machine's last words, for a failure's message.
        world_.onLog = [this](std::string_view line) {
            if (line.find("the action it was moving for is kept") != std::string_view::npos)
            {
                ++coverage_.refunds;
            }
            if (line.find("took too long") != std::string_view::npos)
            {
                ++coverage_.timeouts;
            }
            if (line.find(" m to walk with ") != std::string_view::npos)
            {
                ++coverage_.tooFar;
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

  private:
    static Settings RandomSettings(Rng &rng)
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
        s.maxCommandsPerTurn = Chance(rng, 0.3) ? 2 : 6;
        s.floaties = Chance(rng, 0.5);
        s.scriptEvents = true; // the round check reads the turn-start events
        s.cameraFollow = Chance(rng, 0.2);
        s.orderRunMs = Chance(rng, 0.5) ? 500 : 0;
        return s;
    }

    static std::vector<Creature> RandomFight(Rng &rng)
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
        }
    }

    // A command put to the gate, with the gate's promises checked (1, 5).
    void Try(ObjectId id, CommandType type, Source source)
    {
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
        const bool told = type == CommandType::Attack && source == Source::Player && Chance(rng_, 0.7);
        const float left = told && active ? world_.machine.View().movementLeft : 0.0f;
        const Verdict verdict = type == CommandType::UseAbility
                                    ? world_.UseAbility(id, source, ability, cooldown, sustained, on)
                                    : world_.Order(id, type, source, told ? std::optional{approach} : std::nullopt);
        if (told && active && verdict == Verdict::Allow && approach > left + TurnMachine::kReachSlack)
        {
            Must(false, "the player's attack let through though it cannot reach this turn");
        }
        if (!active)
        {
            Must(verdict == Verdict::Allow, "the gate refused a command with no fight holding the world");
        }
        else if (verdict == Verdict::Allow && kind != CommandKind::Involuntary)
        {
            Must(id == current, "the gate let through a command out of turn");
        }
        if (active && verdict == Verdict::Block && id != current)
        {
            ++coverage_.blockedOutOfTurn;
        }
        if (verdict != Verdict::Allow)
        {
            return;
        }
        if (active && kind == CommandKind::Action)
        {
            ++actions_;
            Must(actions_ <= 1 + clears_, "a second action in one turn");
        }
        Task task{type};
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
        tasks_[id] = task;
    }

    // An AI choosing, as rules_core does on COMMAND_COMPLETE (afterCommand)
    // and on the event the machine sends to start its turn.
    void Decide(ObjectId id, bool afterCommand)
    {
        const Creature &c = world_.Get(id);
        if (c.down || c.condition == Condition::Disabled)
        {
            return; // a stunned creature's AI chooses nothing (_AI_IsCommandValid)
        }
        if (PlayerControls(id))
        {
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
            Must(e.target == world_.Current(), "an AI told to choose out of its turn");
            Must(!PlayerControls(e.target), "the AI told to choose for a creature the player controls");
            Must(world_.Get(e.target).condition != Condition::Disabled, "a stunned creature's AI told to act");
            break;
        case EffectKind::Select:
            Must(world_.Get(e.target).side == Side::Party, "a creature not in the party selected");
            Must(e.target != world_.Current() || world_.Get(e.target).condition != Condition::Disabled,
                 "a stunned party member handed its turn");
            break;
        case EffectKind::Cooldown:
            Must(e.target != kNoObject && e.a >= 0 && e.b >= 0, "a cooldown for nobody, or for no ability");
            // Ready again only as its user's turn begins.
            if (e.b == 0)
            {
                Must(e.target == lastTurnStart_, "a cooldown released out of its user's turn");
                ++coverage_.cooldownsReleased;
            }
            else if (e.b == TurnMachine::kCooldownHoldSeconds)
            {
                ++coverage_.cooldownsHeld;
            }
            break;
        case EffectKind::EndConditions:
            Must(e.target == world_.Current(), "a condition ended out of its creature's turn");
            ++coverage_.conditionsEnded;
            conditionLeftMs_.erase(e.target);
            break;
        case EffectKind::ClearCommands:
        case EffectKind::Floaty:
        case EffectKind::CenterCamera:
        case EffectKind::Explain:
            Must(e.target != kNoObject, "an effect for nobody");
            if (e.kind == EffectKind::Floaty && e.text.ends_with(L" is stunned"))
            {
                ++coverage_.lostTurns;
            }
            if (e.kind == EffectKind::Floaty && e.text == L"Pinned")
            {
                ++coverage_.pinnedTurns;
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
            // A cutscene's scripts move people about.
            if (Chance(rng_, 0.05))
            {
                Try(Pick(rng_, Living(&Arena::Anyone)), CommandType::MoveToLocation, Source::Script);
            }
            Run();
            return;
        }
        if (world_.mode != Mode::Combat)
        {
            return;
        }
        if (!world_.paused)
        {
            Run();
            OutOfTurn();
            Conditions();
        }
        Player();
        Happenings();
    }

    // Commands progress while the game runs.
    void Run()
    {
        std::vector<ObjectId> ids;
        for (const auto &[id, task] : tasks_)
        {
            ids.push_back(id);
        }
        if (world_.machine.Active() && world_.Current() != kNoObject)
        {
            turnRunMs_ += kFrameMs;
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
                    world_.mode == Mode::Combat && !task.story)
                {
                    walked_ += step;
                }
                continue;
            }
            c.moving = false;
            task.workMs -= static_cast<std::int32_t>(kFrameMs);
            if (task.workMs > 0)
            {
                continue;
            }
            const Task done = task;
            tasks_.erase(found);
            world_.Finish(id);
            if (done.type == CommandType::Attack && done.target != kNoObject)
            {
                Creature &victim = world_.Get(done.target);
                const double kill = victim.side == Side::Party ? 0.05 : 0.35;
                if (!victim.down && Chance(rng_, kill))
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

    // Stuns and pins land (a howl, Pinning Shot), and wear off on the
    // engine's timer in running time, unless the machine ends them first.
    void Conditions()
    {
        for (auto it = conditionLeftMs_.begin(); it != conditionLeftMs_.end();)
        {
            it->second -= 16;
            if (it->second <= 0)
            {
                world_.Get(it->first).condition = Condition::None;
                it = conditionLeftMs_.erase(it);
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
                world_.Get(id).condition = Chance(rng_, 0.7) ? Condition::Disabled : Condition::Rooted;
                if (world_.Get(id).condition == Condition::Disabled)
                {
                    // A stun interrupts what its victim was doing.
                    tasks_.erase(id);
                    world_.Finish(id);
                }
                conditionLeftMs_[id] = static_cast<std::int32_t>(Between(rng_, 2000.0f, 10000.0f));
                ++coverage_.stunned;
            }
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
                Try(Pick(rng_, fighting), Chance(rng_, 0.7) ? CommandType::Attack : CommandType::MoveToLocation,
                    Source::Script);
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
        Must(world_.machine.OnPauseKey() == holding,
             "the pause key taken when no fight holds the world, or not when one does");
    }

    // The player, on a turn that is theirs, thinks a while and then acts.
    void Player()
    {
        const ObjectId current = world_.Current();
        const Phase phase = world_.machine.GetPhase();
        if (current == kNoObject || !PlayerControls(current) || !Chance(rng_, 0.1))
        {
            return;
        }
        if (phase != Phase::AwaitingOrder && Chance(rng_, 0.05))
        {
            // End turn pressed while something plays out.
            PressPause();
            return;
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

    // After each frame (2, 3, 4, 7).
    void Check()
    {
        FollowTurn();
        const Phase phase = world_.machine.GetPhase();
        if (world_.mode == Mode::Combat)
        {
            const bool wantsPause =
                phase == Phase::Starting || phase == Phase::TurnStart || phase == Phase::AwaitingOrder;
            const bool wantsRun = phase == Phase::AiThinking || phase == Phase::Executing || phase == Phase::Settling ||
                                  phase == Phase::RoundPadding || phase == Phase::Dispatching;
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
                if (id != current && !c.down && Classify(task.type) != CommandKind::Involuntary)
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
                const float budget = MovementBudget(settings_, c.speed, c.dexterity);
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
                const float slack =
                    TurnMachine::kOvershoot + (4.0f + 2.0f * static_cast<float>(interruptions_)) * frame;
                if (walked_ > budget + slack + 0.01f)
                {
                    const std::string what =
                        "walked past its movement: " + std::to_string(walked_) + " of " + std::to_string(budget);
                    Must(false, what.c_str());
                }
                if (!PlayerControls(current))
                {
                    // An AI's turn: thinking, then at most every command it may
                    // start, each to its limit and settled after.
                    const int bound = settings_.aiThinkMs +
                                      settings_.maxCommandsPerTurn *
                                          (settings_.maxActionMs + settings_.settleMs + settings_.aiFollowUpMs + 200) +
                                      static_cast<int>(settings_.roundMinSeconds * 1000.0f) + 1000;
                    Must(turnRunMs_ <= static_cast<std::uint32_t>(bound), "an AI's turn did not end");
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
    }

    Rng rng_;
    Settings settings_;
    std::unordered_map<ObjectId, std::int32_t> conditionLeftMs_; // the engine's timers, in running time
    ObjectId lastTurnStart_ = kNoObject;                         // whose turn began last (its event)
    World world_;
    Coverage &coverage_;
    std::map<ObjectId, Task> tasks_;
    std::set<std::pair<std::int32_t, ObjectId>> started_;
    std::pair<int, ObjectId> turn_{0, kNoObject};
    float walked_ = 0.0f;
    int actions_ = 0;
    int clears_ = 0;
    int interruptions_ = 0; // conversations during this turn
    std::uint32_t turnRunMs_ = 0;
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
Coverage PlayFights(std::uint64_t count)
{
    Coverage coverage;
    for (std::uint64_t seed = 1; seed <= count; ++seed)
    {
        CAPTURE(seed);
        Arena arena{seed, coverage};
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
    CHECK(coverage.bledOut > 60);
    CHECK(coverage.vanished > 25);
    CHECK(coverage.tooFar > 30);
    // Conditions in turns (2026-09-30: 803 stuns and pins landed, 200 turns
    // lost, 91 pinned, 136 conditions ended by the machine).
    CHECK(coverage.stunned > 260);
    CHECK(coverage.lostTurns > 65);
    CHECK(coverage.pinnedTurns > 30);
    CHECK(coverage.conditionsEnded > 45);
    // Cooldowns in turns (2026-09-30: 142 held, 68 released on their
    // users' turns).
    CHECK(coverage.cooldownsHeld > 45);
    CHECK(coverage.cooldownsReleased > 22);
}

// A thousand more, for a change to the machine: `tb_tests "[fights-long]"`.
TEST_CASE("a thousand random fights keep them too", "[.][fights-long]")
{
    const Coverage coverage = PlayFights(1000);
    CHECK(coverage.fights == 1000);
}
