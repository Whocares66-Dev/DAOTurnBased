// A stand-in for the game, for driving the turn machine through fights:
// creatures with positions and a busy flag, a pause flag, a game mode, and
// the machine's effects applied to them the way src/game/Bridge.cpp applies
// them to the engine. What a creature does is the test's to script (a
// command runs until the test finishes it) or test_fights.cpp's arena's to
// simulate.
#pragma once

#include "core/TurnMachine.h"

#include <algorithm>
#include <cstdint>
#include <functional>
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
    float dexterity = 10.0f;
    float speed = 1.0f;
    bool down = false;
    bool exists = true;   // gone from the world: destroyed, or vanished by a script
    bool fighting = true; // in combat: the game's scan finds it (party members always)
    bool engaged = true;  // fighting the party: in its sight, or attacking or attacked by it
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
};

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
    // The scripted cases walk after an action; one of their own has the
    // default, which ends the turn there.
    s.endTurnWhenActionUsed = false;
    // And hold the world still while the player chooses, which they check;
    // one of their own has the default, the world idling on.
    s.pauseOnPlayerTurn = true;
    // And the cooldowns' first rate, the numbers they were written with
    // (Shield Bash's 20 s two turns); the default is a test of its own.
    s.cooldownSecondsPerTurn = 10.0f;
    // And a turn of one action and meters, which they were written for; a
    // turn as a bar of time has its own cases (test_time.cpp), which set it.
    s.turnClock = TurnClock::Actions;
    // Their actions are ended as soon as they are begun, to get on with the
    // case: with time that reads as an action that did not play, whose time
    // is given back. A case of its own has that rule on (test_time.cpp).
    s.giveBackUnplayed = false;
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
                // As the game's scan: the party, and living creatures in combat.
                if (!c.exists || (c.side != Side::Party && (c.down || !c.fighting || !inCombat)))
                {
                    continue;
                }
                Combatant x;
                x.id = c.id;
                x.side = c.side;
                x.hero = c.hero;
                x.name = c.name;
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
                seen.push_back(x);
            }
            obs.scan = seen;
        }
        if (needs.actor != kNoObject)
        {
            const Creature &c = Get(needs.actor);
            obs.actor = ActorObservation{c.id,      c.exists,   c.down,        c.busy,      c.moving,
                                         c.current, c.position, c.conjureHeld, c.timersHeld};
        }
        for (const ObjectId id : needs.statuses)
        {
            const Creature &c = Get(id);
            obs.statuses.push_back({id, c.exists, c.down, c.down ? 0.0f : c.health, c.condition, c.energy, c.mana,
                                    c.condition != Condition::None ? c.conditionSeconds : 0.0f});
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
        request.inCombat = Get(id).fighting;
        request.walk = walk;
        request.actionSeconds = seconds;
        return Queue(request);
    }

    // An ability used: its cooldown in seconds, and whether it is a
    // sustained one and on (so that this turns it off).
    // An ability with a conjuring time: a turn's worth or more charges.
    Verdict Cast(ObjectId id, Source source, std::int32_t ability, float conjureSeconds, float cooldownSeconds = 20.0f)
    {
        CommandRequest request;
        request.object = id;
        request.type = CommandType::UseAbility;
        request.source = source;
        request.inCombat = Get(id).fighting;
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
        request.inCombat = Get(id).fighting;
        request.ability = ability;
        request.cooldownSeconds = cooldownSeconds;
        request.sustained = sustained;
        request.sustainedOn = on;
        return Queue(request);
    }

    Verdict Queue(const CommandRequest &request)
    {
        const ObjectId id = request.object;
        const CommandType type = request.type;
        const Verdict verdict = machine.OnCommand(request);
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
            started.current = type;
        }
        return verdict;
    }

    void Finish(ObjectId id)
    {
        Creature &c = Get(id);
        c.busy = false;
        c.moving = false;
        c.current = CommandType::Invalid;
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
        foe.standing = to.exists && !to.down;
        foe.hostile = true;
        foe.seen = true;
        foe.meters = Distance(from.position, to.position);
        foe.reach = reach;
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
    // or from where the two stand, hostile and in sight, with this reach.
    std::function<Foe(ObjectId attacker, ObjectId other)> onMeasure;
    float reach = 2.05f;                               // two people's (core/Rules.h, Reach)
    std::vector<std::pair<ObjectId, ObjectId>> turned; // kept attacks turned on another: whose, and on whom
    bool record = true;                                // keep the log and the effects (a long random fight does not)
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
                if (targetGone.erase(e.target) > 0)
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
