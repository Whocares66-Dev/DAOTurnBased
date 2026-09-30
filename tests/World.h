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
    bool busy = false;
    Condition condition = Condition::None; // stunned or pinned, as the game side reads it
    float energy = 1.0f;                   // mana or stamina
    bool mana = false;                     // a mage's
    bool moving = false;
    CommandType current = CommandType::Invalid;
    Vec3 position;
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
                seen.push_back(x);
            }
            obs.scan = seen;
        }
        if (needs.actor != kNoObject)
        {
            const Creature &c = Get(needs.actor);
            obs.actor = ActorObservation{c.id, c.exists, c.down, c.busy, c.moving, c.current, c.position};
        }
        for (const ObjectId id : needs.statuses)
        {
            const Creature &c = Get(id);
            obs.statuses.push_back({id, c.exists, c.down, c.down ? 0.0f : 1.0f, c.condition, c.energy, c.mana});
        }
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
    Verdict Order(ObjectId id, CommandType type, Source source, std::optional<float> walk = std::nullopt)
    {
        CommandRequest request;
        request.object = id;
        request.type = type;
        request.source = source;
        request.inCombat = Get(id).fighting;
        request.walk = walk;
        return Queue(request);
    }

    // An ability used: its cooldown in seconds, and whether it is a
    // sustained one and on (so that this turns it off).
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

    ObjectId Current() const
    {
        const Combatant *c = machine.Order().Current();
        return c != nullptr ? c->id : kNoObject;
    }

    TurnMachine machine;
    std::vector<Creature> creatures;
    std::vector<Effect> applied;
    std::vector<std::string> log;
    bool timeHeld = false;     // effects' time, as DAOLib's dao::effects::Hold leaves it
    std::int32_t speed = 1000; // the game's, in thousandths
    std::int64_t agedMs = 0;
    std::function<void(ObjectId)> onPoke;         // what an AI does when told to choose
    std::function<void(const Effect &)> onEffect; // sees every effect as it is applied
    std::function<void(std::string_view)> onLog;  // sees every line the machine logs
    bool record = true;                           // keep the log and the effects (a long random fight does not)
    Mode mode = Mode::Explore;
    bool paused = false;
    bool inCombat = true;
    std::uint32_t now = 1000;
    int scans = 0;

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
                break;
            case EffectKind::EndConditions:
                Get(e.target).condition = Condition::None;
                break;
            case EffectKind::HoldTime:
                timeHeld = e.a != 0;
                break;
            case EffectKind::GameSpeed:
                speed = e.b;
                break;
            case EffectKind::AgeTime:
                agedMs += e.b;
                break;
            case EffectKind::PokeAi:
                if (onPoke)
                {
                    onPoke(e.target);
                }
                break;
            default:
                break;
            }
        }
    }
};

} // namespace tb::test
