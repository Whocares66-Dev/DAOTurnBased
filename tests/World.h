// A stand-in for the game, for driving the turn machine through fights:
// creatures with positions and a busy flag, a pause flag, a game mode, and
// the machine's effects applied to them the way src/game/Bridge.cpp applies
// them to the engine. What a creature does is the test's to script (a
// command runs until the test finishes it) or test_fights.cpp's arena's to
// simulate.
#pragma once

#include "core/Facing.h"
#include "core/TurnMachine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tb::test
{

struct Creature
{
    ObjectId id = kNoObject;
    Side side = Side::Enemy;
    bool hero = false;
    std::wstring name;
    // Its tag, as the scan reads it (the scripts' GetTag): what a choice of
    // whose tactics play its turns is kept by (core/PartyTactics.h). None
    // unless a case gives one.
    std::wstring tag;
    float dexterity = 10.0f;
    float speed = 1.0f; // its build's pace against a person's
    // The movement rate effects standing on it, each one's potency, as the
    // game layer reads them with every look at everyone, and not with a scan
    // (Haste, a salve, a slow: issue 49). A case puts them on and takes them
    // off.
    MovementEffects movement;
    bool down = false;
    bool exists = true;   // gone from the world: destroyed, or vanished by a script
    bool fighting = true; // in combat: the game's scan finds it (party members always)
    // In combat where the game's scan does not look: further than
    // combat_radius from where it measures, or past the nearest it asks for
    // (src/game/Bridge.cpp, Scan). The gate hears its commands all the same,
    // and holds them; no scan brings it into the order or names it as waiting
    // outside (issue 43).
    bool unfound = false;
    // Made inactive by the game (the scripts' SetObjectActive(FALSE): a
    // tentacle gone under): out of the world. The game layer reads it of
    // the acting creature every frame (the engine's GetObjectActive,
    // ActorObservation::inactive); the scan's own rules do not find it
    // fighting, as in play (the Broodmother's fight, 2026-10-05: four
    // tentacles in combat 14 to 16 m from the party, "not found fighting"),
    // and its perception is what it was. SetActive() makes it so as the
    // engine does (issue 44).
    bool inactive = false;
    // What is queued on it while it is inactive, which the engine does not
    // begin: busy, with no current command, until it is made active again.
    CommandType queued = CommandType::Invalid;
    bool engaged = true; // fighting the party: in its sight, or attacking or attacked by it
    bool busy = false;
    Condition condition = Condition::None; // stunned or pinned, as the game side reads it
    float conditionSeconds = 0.0f;         // what the engine has left of it (0: not read)
    float energy = 1.0f;                   // mana or stamina
    bool mana = false;                     // a mage's
    bool moving = false;
    CommandType current = CommandType::Invalid;
    Vec3 position;
    bool conjureHeld = false; // at a conjure the game layer holds
    bool timersHeld = false;  // its scripts' timers held (a poison's ticks)
    int rank = 0;             // CREATURE_RANK_*: an enemy's Combat Tactics is by it, with time
    int tactics = 0;          // Combat Tactics' rank, a party member's
    int survival = 0;         // Survival's rank
    float health = 1.0f;      // current over maximum, while it stands
    // At an attack: its command is still at its approach, as the game layer
    // reads it of the engine each frame (ActorObservation::approaching). A
    // case says so for an attacker that cannot get to its target.
    bool approaching = false;
    // The engine's perception between it and the party, and of anyone it
    // would fight, as the game layer reads it with every scan and, of the
    // acting creature, every frame (issue 29). Everything, unless a case
    // says otherwise: Unnoticing() for a hostile that has not noticed.
    Perception perception;
    // At an effect's command: how long the engine's count has it still to
    // run, and whether the engine can say (ActorObservation::heldSeconds,
    // heldKnown), and the effects standing on it in the game layer's words
    // (ActorObservation::effects). A case sets them; the machine only says
    // them.
    float heldSeconds = 0.0f;
    bool heldKnown = false;
    std::string effects;
    // The way it faces, on the ground, as the engine keeps it and its turn
    // to face a target is reckoned from (DAOLib's dao::attack::TurnTo;
    // src/game/Gate.cpp, TurnOf). None unless a case gives one
    // (World::FaceOff): with none the engine has no turn to answer, and
    // nobody is faced (issue 40).
    float facingX = 0.0f;
    float facingY = 0.0f;
    // Where the engine aims whoever strikes at it, from where it stands: a
    // person's is where it stands; a big creature's is a point of its melee
    // ring, which a case gives (in play a Great Bear's was 3.95 degrees from
    // its place for a striker 2.58 m off, and facing the striker at the
    // place made the hang the rule was for: issue 40).
    float aimX = 0.0f;
    float aimY = 0.0f;
    // The engine would begin no turn for it, whatever it is asked to face:
    // its movement controller idle, with its own facing where the aim is
    // (the dead case's half that is the controller's; the other is the
    // creature not facing the aim, which its facing above says). A fact a
    // case gives, as DAOLib's TurnTo gives the game layer its `stalls`.
    bool stalled = false;
};

// A hostile the party perceives that the game does not have in combat and
// that perceives nobody to fight: it has not noticed (core/Rules.h, Unaware).
inline Perception Unnoticing()
{
    Perception perception;
    perception.ofParty = false;
    perception.ofFoes = false;
    perception.inCombat = false;
    return perception;
}

// One the party has lost, and that has lost the party: neither perceives the
// other (it may perceive whom else it fights). With `fighting` false, that
// is one out of the fight by the game's own reckoning (core/Rules.h,
// StaysInTheFight); with `fighting` false alone it is still in it.
inline Perception Unperceived()
{
    Perception perception;
    perception.byParty = false;
    perception.ofParty = false;
    return perception;
}

inline Creature Make(ObjectId id, Side side, bool hero, std::wstring name, float dexterity)
{
    Creature c;
    c.id = id;
    c.side = side;
    c.hero = hero;
    c.name = std::move(name);
    c.dexterity = dexterity;
    return c;
}

// The fight most scripted tests use. Under Fixed(), initiative is
// 1 + (Dexterity - 10), so the order is the hero, the genlock, Alistair, the
// hurlock.
inline constexpr ObjectId kHero = 0x100;
inline constexpr ObjectId kCompanion = 0x101;
inline constexpr ObjectId kGenlock = 0x200;
inline constexpr ObjectId kHurlock = 0x201;

inline Settings Fixed()
{
    Settings s;
    s.initiativeDie = 1;
    s.initiativePerDexterity = 1.0f;
    // The scripted cases hold the world still while the player chooses,
    // which they check; one of their own has the default, the world idling
    // on.
    s.pauseOnPlayerTurn = true;
    // Their actions are ended as soon as they are begun, to get on with the
    // case: that reads as an action that did not play, whose time is given
    // back. A case of its own has that rule on (test_time.cpp).
    s.giveBackUnplayed = false;
    // A turn of 4 s walked at 5 m a second, for round numbers, with a meter
    // free: 21 m in a whole turn.
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    s.freeStepMeters = 1.0f;
    return s;
}

inline std::vector<Creature> StandardFight()
{
    return {
        Make(kHero, Side::Party, true, L"Hero", 20.0f),           // initiative 11
        Make(kGenlock, Side::Enemy, false, L"Genlock", 18.0f),    // 9
        Make(kCompanion, Side::Party, false, L"Alistair", 15.0f), // 6
        Make(kHurlock, Side::Enemy, false, L"Hurlock", 10.0f),    // 1
    };
}

class World
{
  public:
    explicit World(const Settings &settings = Fixed(), std::vector<Creature> fight = StandardFight(),
                   std::uint64_t seed = 1)
        : machine(settings, seed,
                  [this](LogLevel, std::string_view line) {
                      if (record)
                      {
                          log.emplace_back(line);
                      }
                      if (onLog)
                      {
                          onLog(line);
                      }
                  }),
          creatures(std::move(fight))
    {
    }

    Creature &Get(ObjectId id)
    {
        return *std::ranges::find(creatures, id, &Creature::id);
    }
    const Creature &Get(ObjectId id) const
    {
        return *std::ranges::find(creatures, id, &Creature::id);
    }

    // The game's own word for whether it has a creature in combat, as the gate
    // reads it of the creature with each command (its combat state): in
    // combat where the scan would look for it or not, and not one that has
    // noticed nobody (Unnoticing) or that the game has stood down. The fight
    // holds what the game has in combat (issue 43).
    bool InCombat(ObjectId id) const
    {
        return Get(id).perception.inCombat;
    }

    // One frame: the machine asks, the world answers, the machine steps, the
    // world does what it said.
    void Frame(std::uint32_t ms = 16)
    {
        now += ms;
        const Needs needs = machine.WhatToObserve(now);
        Observation obs;
        obs.nowMs = now;
        obs.mode = mode;
        obs.paused = paused;
        if (needs.scan)
        {
            ++scans;
            std::vector<Combatant> seen;
            for (const Creature &c : creatures)
            {
                // As the game's scan: the party, and living creatures in
                // combat. And of those the machine named as in the fight
                // (Needs::inFight), the living ones that are not found so:
                // read all the same, with their perception, which is what
                // keeps them in it or does not (issue 29).
                const bool found =
                    c.side == Side::Party || (!c.down && c.fighting && !c.unfound && !c.inactive && inCombat);
                const bool named = !c.down && std::ranges::find(needs.inFight, c.id) != needs.inFight.end();
                if (!c.exists || (!found && !named))
                {
                    continue;
                }
                Combatant x;
                x.id = c.id;
                x.side = c.side;
                x.hero = c.hero;
                x.name = c.name;
                x.tag = c.tag;
                x.dexterity = c.dexterity;
                x.speed = c.speed;
                x.down = c.down;
                x.engaged = c.engaged;
                x.rank = c.rank;
                x.tactics = c.tactics;
                x.survival = c.survival;
                // Its health as the scan reads it; not its mana or stamina,
                // which the game's scan does not read (Bridge::Describe).
                x.health = c.down ? 0.0f : c.health;
                x.perception = c.perception;
                x.fighting = found;
                seen.push_back(x);
            }
            obs.scan = seen;
        }
        if (needs.actor != kNoObject)
        {
            const Creature &c = Get(needs.actor);
            obs.actor = ActorObservation{c.id,         c.exists,   c.down,        c.busy,       c.moving,
                                         c.current,    c.position, c.conjureHeld, c.timersHeld, c.approaching,
                                         c.perception, c.effects,  c.heldSeconds, c.heldKnown,  c.inactive};
        }
        for (const ObjectId id : needs.statuses)
        {
            const Creature &c = Get(id);
            obs.statuses.push_back({id, c.exists, c.down, c.down ? 0.0f : c.health, c.condition, c.energy, c.mana,
                                    c.condition != Condition::None ? c.conditionSeconds : 0.0f, c.movement});
        }
        statusReads += needs.statuses.empty() ? 0 : 1;
        obs.keptVoid = std::exchange(keptVoid, {});
        Apply(machine.Step(obs));
    }

    void RunFor(std::uint32_t ms)
    {
        for (std::uint32_t t = 0; t < ms; t += 16)
        {
            Frame();
        }
    }

    // A command queued on a creature, through the gate as the queue hook
    // puts it; it starts running if let through. walk: for an attack, the
    // meters to its target's reach, when the game layer could tell.
    // seconds: with a turn a bar of time, how long the action takes (0: the
    // machine's stand-in).
    Verdict Order(ObjectId id, CommandType type, Source source, std::optional<float> walk = std::nullopt,
                  float seconds = 0.0f)
    {
        CommandRequest request;
        request.object = id;
        request.type = type;
        request.source = source;
        request.inCombat = InCombat(id);
        request.walk = walk;
        request.actionSeconds = seconds;
        return Queue(request);
    }

    // A wait, as the scripts' CommandWait(seconds) comes to the gate: its
    // length the command's own, as the game layer reads it (0: not read).
    Verdict Wait(ObjectId id, float seconds, Source source = Source::Script)
    {
        CommandRequest request;
        request.object = id;
        request.type = CommandType::Wait;
        request.source = source;
        request.inCombat = InCombat(id);
        request.waitSeconds = seconds;
        return Queue(request);
    }

    // An ability used: its cooldown in seconds, and whether it is a
    // sustained one and on (so that this turns it off).
    // An ability with a conjuring time, which it costs of the bar: what does
    // not fit what is left is kept.
    Verdict Cast(ObjectId id, Source source, std::int32_t ability, float conjureSeconds, float cooldownSeconds = 20.0f)
    {
        CommandRequest request;
        request.object = id;
        request.type = CommandType::UseAbility;
        request.source = source;
        request.inCombat = InCombat(id);
        request.ability = ability;
        request.cooldownSeconds = cooldownSeconds;
        request.conjureSeconds = conjureSeconds;
        return Queue(request);
    }

    Verdict UseAbility(ObjectId id, Source source, std::int32_t ability, float cooldownSeconds, bool sustained = false,
                       bool on = false)
    {
        CommandRequest request;
        request.object = id;
        request.type = CommandType::UseAbility;
        request.source = source;
        request.inCombat = InCombat(id);
        request.ability = ability;
        request.cooldownSeconds = cooldownSeconds;
        request.sustained = sustained;
        request.sustainedOn = on;
        return Queue(request);
    }

    // The way from a creature to where the engine aims it at another: the
    // other's place, moved by its aim (Creature::aimX).
    std::pair<float, float> AimFrom(ObjectId id, ObjectId target) const
    {
        const Creature &c = Get(id);
        const Creature &aimed = Get(target);
        return {aimed.position.x + aimed.aimX - c.position.x, aimed.position.y + aimed.aimY - c.position.y};
    }

    // The turn a command would make to face its target, as the engine
    // answers the game layer (DAOLib's dao::attack::TurnTo, through
    // src/game/Gate.cpp's TurnOf): how far from the creature's facing to the
    // engine's aim, the engine's own test of it, a degree, and whether the
    // turn would stall: not done, and the engine would begin none
    // (Creature::stalled). Nothing for a command that turns its creature to
    // nobody, with nobody else aimed at, with no facing, or with the aim
    // where the creature stands.
    std::optional<TurnToFace> Turn(CommandType type, ObjectId id, ObjectId target) const
    {
        if (!TurnsToFace(type) || target == id ||
            std::ranges::find(creatures, target, &Creature::id) == creatures.end())
        {
            return std::nullopt;
        }
        const Creature &c = Get(id);
        const auto [x, y] = AimFrom(id, target);
        if (std::hypot(c.facingX, c.facingY) < 1e-3f || std::hypot(x, y) < 1e-3f)
        {
            return std::nullopt;
        }
        const float degrees = std::atan2(c.facingX * y - c.facingY * x, c.facingX * x + c.facingY * y) *
                              (180.0f / std::numbers::pi_v<float>);
        // The engine's own test: within a degree, a plain <= (core/Facing.h).
        const bool done = std::abs(degrees) <= 1.0f;
        return TurnToFace{degrees, done, !done && c.stalled};
    }

    // A creature turned to stand so many degrees off the engine's aim at
    // another, one way round or the other: where a case wants it as its
    // command comes.
    void FaceOff(ObjectId id, ObjectId target, float degrees)
    {
        Creature &c = Get(id);
        const auto [x, y] = AimFrom(id, target);
        const float facing = std::atan2(y, x) - degrees * (std::numbers::pi_v<float> / 180.0f);
        c.facingX = std::cos(facing);
        c.facingY = std::sin(facing);
    }

    // The dead case: a creature so many degrees off the engine's aim at
    // another, for which the engine would begin no turn.
    void Stall(ObjectId id, ObjectId target, float degrees)
    {
        FaceOff(id, target, degrees);
        Get(id).stalled = true;
    }

    Verdict Queue(const CommandRequest &asked)
    {
        // As the gate asks the engine with every command, afresh: a kept
        // action given again is asked of as its creature stands now.
        CommandRequest request = asked;
        request.turn = Turn(request.type, request.object, request.target);
        const ObjectId id = request.object;
        const CommandType type = request.type;
        const Verdict verdict = machine.OnCommand(request);
        // And as the gate has the engine face one the machine says to, before
        // the command is given: at the engine's own aim (DAOLib's
        // FaceExactly), the engine asked again, and the machine told what
        // came of it (Gate.cpp, QueueCommand; issue 40).
        if (machine.FaceFirst(request, verdict))
        {
            const bool taken = !faceRefused;
            if (taken)
            {
                FaceOff(id, request.target, 0.0f);
                facedFirst.emplace_back(id, request.target);
            }
            machine.Faced(request, taken, Turn(type, id, request.target));
        }
        if (verdict == Verdict::Allow || verdict == Verdict::Conjure)
        {
            // What the command finds as it begins: its creature's turn to
            // face its target, which the engine's own test is asked of.
            if (const std::optional<TurnToFace> found = Turn(type, id, request.target))
            {
                foundOff[id] = std::abs(found->degrees);
            }
            else
            {
                foundOff.erase(id);
            }
        }
        if (verdict == Verdict::Hold)
        {
            heldCasts[id] = request; // as the game layer keeps it
        }
        if (verdict == Verdict::Conjure)
        {
            // The engine's own cast, at once at its held conjure.
            Creature &conjuring = Get(id);
            conjuring.busy = true;
            conjuring.current = type;
            conjuring.conjureHeld = true;
            heldConjures.insert(id);
        }
        if (verdict == Verdict::Allow)
        {
            Creature &started = Get(id);
            started.busy = true;
            running[id] = request;
            if (started.inactive)
            {
                started.queued = type; // queued, and not begun (SetActive)
            }
            else
            {
                started.current = type;
            }
        }
        return verdict;
    }

    // The scripts' SetObjectActive, as the engine carries it out (script.ldf):
    // "all commands will be cleared upon disabling", and what is queued on an
    // inactive object after that waits ("Messages will continue to queue up
    // so you are able to assign actions before enabling the object"), with no
    // current command, until the object is made active again.
    void SetActive(ObjectId id, bool active)
    {
        Creature &c = Get(id);
        if (c.inactive != active)
        {
            return; // as it is already
        }
        c.inactive = !active;
        if (!active)
        {
            Finish(id);
            running.erase(id);
            c.conjureHeld = false;
            heldConjures.erase(id);
        }
        else if (c.queued != CommandType::Invalid)
        {
            c.current = std::exchange(c.queued, CommandType::Invalid);
        }
    }

    // Whether the action kept for a creature is aimed at one the game has
    // made inactive since: not there for it to be given at, as one fallen is
    // not (ReleaseCast).
    bool KeptAtInactive(ObjectId id) const
    {
        const auto kept = heldCasts.find(id);
        if (kept == heldCasts.end())
        {
            return false;
        }
        const auto target = std::ranges::find(creatures, kept->second.target, &Creature::id);
        return target != creatures.end() && target->inactive;
    }

    // Something queued on a creature by the engine itself with no word to the
    // gate's rules (an effect's command as the effect lands on it): on an
    // inactive one it waits, busy with no current command.
    void QueueOnInactive(ObjectId id, CommandType type)
    {
        Creature &c = Get(id);
        c.busy = true;
        c.queued = type;
    }

    // An attack at someone, as the gate's request names its target, with
    // what the engine answered when the game layer asked it for the attack's
    // approach and walk (src/game/Gate.cpp, WalkToTarget): nothing to walk by
    // default, the target in reach; or its own "no" (NoAttack: no place on
    // the target's ring, no path, no line for a shot).
    Verdict Attack(ObjectId id, ObjectId target, Source source, NoAttack none = NoAttack::None,
                   std::optional<float> walk = 0.0f, float seconds = 0.0f)
    {
        CommandRequest request;
        request.object = id;
        request.type = CommandType::Attack;
        request.source = source;
        request.inCombat = InCombat(id);
        request.walk = none == NoAttack::None ? walk : std::nullopt;
        request.actionSeconds = seconds;
        request.target = target;
        request.noAttack = none;
        return Queue(request);
    }

    void Finish(ObjectId id)
    {
        Creature &c = Get(id);
        c.busy = false;
        c.moving = false;
        c.current = CommandType::Invalid;
        c.queued = CommandType::Invalid;
    }

    // The engine fails a creature's command without acting, by its own name
    // for why (COMMAND_FAILED_...): the game layer hears it end and tells
    // the machine, with what the command was and what it was aimed at
    // (Gate.cpp's OnEnd), and the creature is idle. Answers what the machine
    // answered: whether its action was given back.
    bool Fail(ObjectId id, std::string_view why)
    {
        const auto found = running.find(id);
        const CommandRequest failed = found != running.end() ? found->second : CommandRequest{};
        const bool givenBack = machine.OnCommandFailed(id, failed.type, failed.ability, why, failed.target);
        Finish(id);
        return givenBack;
    }

    bool Did(EffectKind kind, ObjectId target = kNoObject) const
    {
        return std::ranges::any_of(
            applied, [&](const Effect &e) { return e.kind == kind && (target == kNoObject || e.target == target); });
    }
    bool Signalled(std::int32_t event) const
    {
        return std::ranges::any_of(applied,
                                   [&](const Effect &e) { return e.kind == EffectKind::Signal && e.event == event; });
    }
    bool Said(const std::wstring &text) const
    {
        return std::ranges::any_of(applied,
                                   [&](const Effect &e) { return e.kind == EffectKind::Floaty && e.text == text; });
    }

    // Combat begins, the first scan comes in, and with the next frame's
    // statuses the first turn has started.
    void StartFight()
    {
        mode = Mode::Combat;
        Frame();
        Frame();
        Frame();
    }

    // An error in the plugin's frame: what it does about it.
    void StandDown(std::string_view why)
    {
        Apply(machine.StandDown(why));
    }

    ObjectId Current() const
    {
        const Combatant *c = machine.Order().Current();
        return c != nullptr ? c->id : kNoObject;
    }

    Foe Measure(ObjectId attacker, ObjectId other) const
    {
        if (onMeasure)
        {
            return onMeasure(attacker, other);
        }
        const Creature &from = Get(attacker);
        const Creature &to = Get(other);
        Foe foe;
        foe.id = other;
        foe.standing = to.exists && !to.down && !to.inactive; // nor one out of the world (Bridge.cpp, NotThere)
        foe.hostile = true;
        foe.seen = true;
        foe.meters = Distance(from.position, to.position);
        foe.inReach = foe.meters <= reach;
        return foe;
    }

    TurnMachine machine;
    std::vector<Creature> creatures;
    std::vector<Effect> applied;
    std::vector<std::string> log;
    bool timeHeld = false;                                  // effects' time, as DAOLib's dao::effects::Hold leaves it
    std::int32_t speed = 1000;                              // the game's, in thousandths
    std::int64_t agedMs = 0;                                // the shared clock's
    std::unordered_map<ObjectId, std::int64_t> agedOwn;     // each creature's own
    std::unordered_set<ObjectId> onOwnClock;                // as DAOLib's dao::clock::OwnClock leaves it
    std::unordered_map<ObjectId, CommandRequest> heldCasts; // casts the game layer keeps while they charge
    std::unordered_map<ObjectId, CommandRequest> running;   // each creature's last command let through (Fail)
    std::unordered_set<ObjectId> heldConjures;              // the engine's casts, held in their conjure
    std::function<void(ObjectId)> onConjureReleased;        // a held conjure let complete
    std::vector<Verdict> released;                          // what each cast given again was answered
    std::function<void(ObjectId)> onPoke;                   // what an AI does when told to choose
    std::function<void(const Effect &)> onEffect;           // sees every effect as it is applied
    std::function<void(std::string_view)> onLog;            // sees every line the machine logs
    std::function<void(ObjectId, const CommandRequest &, Verdict)> onReleased; // a kept cast or action given again
    std::function<void(ObjectId)> onApproach; // the walk to a kept attack's target, when not World's own
    int approaches = 0;
    std::unordered_set<ObjectId> targetGone; // whose kept action's target is gone: not given
    std::vector<ObjectId> keptVoid;          // told to the machine with the next frame
    // What the game layer measures of one a kept attack may be turned on,
    // its own target gone (src/game/Bridge.cpp, ReleaseCast): a test's own,
    // or from where the two stand, hostile and in sight, in reach within this.
    std::function<Foe(ObjectId attacker, ObjectId other)> onMeasure;
    float reach = 2.05f;                               // the stand-in's own: the game layer asks the engine
    std::vector<std::pair<ObjectId, ObjectId>> turned; // kept attacks turned on another: whose, and on whom
    // Whom the gate faced at its target before its command, the engine
    // having it for facing after, and at whom (TurnMachine::FaceFirst,
    // Faced); and how many degrees off the engine's aim each creature's
    // last command aimed at another found it as it began, after that
    // (issue 40).
    std::vector<std::pair<ObjectId, ObjectId>> facedFirst;
    std::unordered_map<ObjectId, float> foundOff;
    // The engine will not face a creature it is asked to (DAOLib's
    // FaceExactly answers no where the movement controller is not idle, and
    // sets nothing): a case says so for one whose controller has begun
    // something since the engine was asked.
    bool faceRefused = false;
    bool record = true; // keep the log and the effects (a long random fight does not)
    Mode mode = Mode::Explore;
    bool paused = false;
    bool inCombat = true;
    std::uint32_t now = 1000;
    int scans = 0;
    int statusReads = 0; // frames in which the machine asked for everyone's health and standing

  private:
    void Apply(const std::vector<Effect> &effects)
    {
        for (const Effect &e : effects)
        {
            if (record)
            {
                applied.push_back(e);
            }
            if (onEffect)
            {
                onEffect(e);
            }
            switch (e.kind)
            {
            case EffectKind::Pause:
                paused = true;
                break;
            case EffectKind::Unpause:
                paused = false;
                break;
            case EffectKind::ClearCommands:
                // (Not what waits on an inactive creature: in play the
                // machine's ClearAllCommands, eleven turns running, left what
                // was queued on a tentacle under ground where it was. Issue
                // 44; why is not read.)
                if (Get(e.target).inactive && Get(e.target).queued != CommandType::Invalid)
                {
                    break;
                }
                Finish(e.target);
                if (heldConjures.erase(e.target) > 0)
                {
                    // A held conjure's command cleared: the game layer hears
                    // it end (Gate.cpp's OnEnd) and says so.
                    Get(e.target).conjureHeld = false;
                    (void)machine.OnConjureCleared(e.target);
                }
                break;
            case EffectKind::EndConditions:
                Get(e.target).condition = Condition::None;
                break;
            case EffectKind::HoldTime:
                timeHeld = e.a != 0;
                if (!timeHeld)
                {
                    onOwnClock.clear(); // let go, every object is the shared clock's again
                }
                break;
            case EffectKind::OwnClock:
                if (e.a != 0)
                {
                    onOwnClock.insert(e.target);
                }
                else
                {
                    onOwnClock.erase(e.target);
                }
                break;
            case EffectKind::GameSpeed:
                speed = e.b;
                break;
            case EffectKind::AgeTime:
                if (e.target != kNoObject)
                {
                    agedOwn[e.target] += e.b;
                }
                else
                {
                    agedMs += e.b;
                }
                break;
            case EffectKind::PokeAi:
                if (onPoke)
                {
                    onPoke(e.target);
                }
                break;
            case EffectKind::Approach:
                // The game layer walks the creature to its kept attack's
                // target, as a script's move.
                ++approaches;
                if (onApproach)
                {
                    onApproach(e.target);
                }
                else
                {
                    (void)Order(e.target, CommandType::MoveToObject, Source::Script);
                }
                break;
            case EffectKind::ReleaseCast:
                if (heldConjures.erase(e.target) > 0)
                {
                    // Its conjure let complete: the cast runs, and ends as a
                    // test or the arena finishes it.
                    Get(e.target).conjureHeld = false;
                    if (onConjureReleased)
                    {
                        onConjureReleased(e.target);
                    }
                    break;
                }
                // The game layer gives the cast it kept, as a script's; not
                // one whose target is gone, which it says with the next
                // frame's observation. A plain attack is given all the same,
                // at the nearest of those the machine named that its creature
                // can strike from where it stands, if there is one.
                // Gone as a test says (targetGone), or made inactive by the
                // game since the action was kept, which is the same to the
                // game layer (Bridge.cpp, NotThere; issue 44).
                if (const bool said = targetGone.erase(e.target) > 0; said || KeptAtInactive(e.target))
                {
                    const auto kept = heldCasts.find(e.target);
                    ObjectId next = kNoObject;
                    if (kept != heldCasts.end() && kept->second.type == CommandType::Attack)
                    {
                        std::vector<Foe> foes;
                        for (const ObjectId other : e.others)
                        {
                            foes.push_back(Measure(e.target, other));
                        }
                        next = NearestInReach(foes);
                    }
                    if (next != kNoObject)
                    {
                        CommandRequest again = kept->second;
                        heldCasts.erase(kept);
                        again.source = Source::Script;
                        again.inCombat = InCombat(e.target); // as the gate reads it now, not as it was kept
                        again.target = next;
                        again.walk = 0.0f; // in reach: nothing to walk
                        turned.emplace_back(e.target, next);
                        released.push_back(Queue(again));
                        if (onReleased)
                        {
                            onReleased(e.target, again, released.back());
                        }
                    }
                    else
                    {
                        heldCasts.erase(e.target);
                        keptVoid.push_back(e.target);
                    }
                }
                else if (const auto kept = heldCasts.find(e.target); kept != heldCasts.end())
                {
                    CommandRequest again = kept->second;
                    heldCasts.erase(kept);
                    again.source = Source::Script;
                    again.inCombat = InCombat(e.target); // as the gate reads it now, not as it was kept
                    released.push_back(Queue(again));
                    if (onReleased)
                    {
                        onReleased(e.target, again, released.back());
                    }
                }
                break;
            case EffectKind::DropCast:
                if (e.target == kNoObject)
                {
                    heldCasts.clear();
                    for (const ObjectId held : heldConjures)
                    {
                        Get(held).conjureHeld = false; // let run on
                    }
                    heldConjures.clear();
                }
                else
                {
                    heldCasts.erase(e.target);
                    if (heldConjures.erase(e.target) > 0)
                    {
                        Get(e.target).conjureHeld = false;
                        Finish(e.target); // its command cleared
                    }
                }
                break;
            default:
                break;
            }
        }
    }
};

} // namespace tb::test
