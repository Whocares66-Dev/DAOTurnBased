#include "core/TurnMachine.h"

#include "core/Text.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace tb
{

namespace
{

// Modes in which there is no world to fight in: whatever fight there was is
// gone, objects and all.
bool OutOfWorld(Mode mode)
{
    return mode == Mode::Loading || mode == Mode::Pregame || mode == Mode::CharGen || mode == Mode::Unknown;
}

// Modes that interrupt a fight and give it back: a conversation, a
// cutscene. Their scripts move creatures about, so the gate stands aside.
bool Interlude(Mode mode)
{
    return mode == Mode::Dialog || mode == Mode::Cutscene || mode == Mode::Movie || mode == Mode::FlyCam ||
           mode == Mode::Fixed || mode == Mode::PartyPicker;
}

// Between turns the world holds still; while the player chooses, only with
// pause_on_player_turn (the user asked for the world to idle on, 2026-09-30).
bool WantsPaused(Phase phase, const Settings &settings)
{
    return phase == Phase::Starting || phase == Phase::TurnStart ||
           (phase == Phase::AwaitingOrder && settings.pauseOnPlayerTurn);
}

const char *ModeName(Mode mode)
{
    switch (mode)
    {
    case Mode::Unknown:
        return "unknown";
    case Mode::Explore:
        return "explore";
    case Mode::Combat:
        return "combat";
    case Mode::Dead:
        return "dead";
    case Mode::Gui:
        return "menu";
    case Mode::Dialog:
        return "dialogue";
    case Mode::Cutscene:
        return "cutscene";
    case Mode::FlyCam:
        return "fly camera";
    case Mode::Fixed:
        return "fixed camera";
    case Mode::Pregame:
        return "pregame";
    case Mode::Loading:
        return "loading";
    case Mode::Movie:
        return "movie";
    case Mode::CharGen:
        return "character creation or main menu";
    case Mode::PartyPicker:
        return "party picker";
    }
    return "?";
}

} // namespace

const char *Name(Phase phase)
{
    switch (phase)
    {
    case Phase::Idle:
        return "idle";
    case Phase::Starting:
        return "starting";
    case Phase::TurnStart:
        return "turn start";
    case Phase::AwaitingOrder:
        return "awaiting order";
    case Phase::Dispatching:
        return "dispatching an order";
    case Phase::AiThinking:
        return "AI thinking";
    case Phase::Executing:
        return "executing";
    case Phase::Settling:
        return "settling";
    case Phase::RoundPadding:
        return "round padding";
    case Phase::Suspended:
        return "suspended";
    }
    return "?";
}

const char *Name(EffectKind kind)
{
    switch (kind)
    {
    case EffectKind::Pause:
        return "pause";
    case EffectKind::Unpause:
        return "unpause";
    case EffectKind::ClearCommands:
        return "clear commands";
    case EffectKind::Select:
        return "select";
    case EffectKind::PokeAi:
        return "poke AI";
    case EffectKind::Floaty:
        return "floaty";
    case EffectKind::Signal:
        return "signal";
    case EffectKind::CenterCamera:
        return "centre camera";
    case EffectKind::Explain:
        return "explain";
    case EffectKind::EndConditions:
        return "end conditions";
    case EffectKind::Cooldown:
        return "cooldown";
    }
    return "?";
}

TurnMachine::TurnMachine(Settings settings, std::uint64_t seed, LogSink log)
    : settings_(settings), rng_(seed), log_(std::move(log))
{
}

void TurnMachine::SetSettings(const Settings &settings)
{
    settings_ = settings;
}

bool TurnMachine::Active() const
{
    return phase_ != Phase::Idle && phase_ != Phase::Suspended;
}

// The same refusal again within a second is not logged again: the player
// steering with the keys out of turn is refused every frame (sixty lines a
// second in play, 2026-09-30).
void TurnMachine::Refused(LogLevel level, std::string text)
{
    if (text == lastRefusal_ && lastNow_ - lastRefusalLogNow_ < 1000)
    {
        return;
    }
    lastRefusalLogNow_ = lastNow_;
    Log(level, text);
    lastRefusal_ = std::move(text);
}

void TurnMachine::Log(LogLevel level, std::string_view text) const
{
    if (Logs(level))
    {
        log_(level, text);
    }
}

std::uint32_t TurnMachine::Delta(std::uint32_t nowMs)
{
    if (!haveNow_)
    {
        haveNow_ = true;
        lastNow_ = nowMs;
        return 0;
    }
    // Unsigned, so GetTickCount's wrap every 49 days is a small step too. A
    // long frame (a hitch, the game in the background) counts as a quarter
    // second, so no deadline passes in one bound.
    const std::uint32_t delta = nowMs - lastNow_;
    lastNow_ = nowMs;
    return std::min<std::uint32_t>(delta, 250);
}

Needs TurnMachine::WhatToObserve(std::uint32_t nowMs)
{
    Needs needs;
    needs.scan = std::exchange(wantScan_, false);
    if (phase_ == Phase::Idle)
    {
        // A fight ended while the game still said combat: look now and then
        // for hostiles fighting again.
        if (latch_ && lastMode_ == Mode::Combat && nowMs - lastLatchScanNow_ >= 1000)
        {
            needs.scan = true;
            lastLatchScanNow_ = nowMs;
        }
        return needs;
    }
    if (phase_ == Phase::Suspended)
    {
        return needs;
    }
    if (const Combatant *current = order_.Current())
    {
        needs.actor = current->id;
    }
    // Fresh at every turn start: whether a creature is stunned decides
    // whether its turn is taken at all.
    if (nowMs - lastStatusNow_ >= 500 || phase_ == Phase::TurnStart)
    {
        lastStatusNow_ = nowMs;
        for (const Combatant &c : order_.All())
        {
            needs.statuses.push_back(c.id);
        }
    }
    return needs;
}

std::vector<Effect> TurnMachine::Step(const Observation &obs)
{
    std::vector<Effect> fx;
    fx.swap(pending_);
    const std::uint32_t dt = Delta(obs.nowMs);
    lastMode_ = obs.mode;

    if (OutOfWorld(obs.mode))
    {
        if (phase_ != Phase::Idle)
        {
            Log(LogLevel::Info, std::format("the game went to {}: the fight is dropped", ModeName(obs.mode)));
            order_.Clear();
            usedNow_.clear();
            holds_.clear();
            phase_ = Phase::Idle;
        }
        latch_ = false;
        wantScan_ = false;
        fx.clear(); // their objects may be gone with the world
        return fx;
    }
    if (phase_ == Phase::Idle)
    {
        StepIdle(obs, fx);
        return fx;
    }
    if (obs.scan && phase_ != Phase::Starting && phase_ != Phase::TurnStart)
    {
        Merge(*obs.scan, fx);
    }
    if (phase_ == Phase::Suspended)
    {
        StepSuspended(obs, fx);
        return fx;
    }
    if (obs.mode == Mode::Explore || obs.mode == Mode::Dead)
    {
        End(fx, obs.mode == Mode::Dead ? "the party fell" : "the game ended combat");
        return fx;
    }
    if (Interlude(obs.mode))
    {
        Log(LogLevel::Info,
            std::format("{} in the middle of the fight: standing aside until it ends", ModeName(obs.mode)));
        // Between two turns (or before a round's first) there is no turn to
        // go on with after: the next one begins afresh, a lost one lost.
        // A resume still pending (two in a row) is a turn begun.
        turnBegun_ =
            resume_ || (phase_ != Phase::TurnStart && phase_ != Phase::Starting && phase_ != Phase::RoundPadding);
        phase_ = Phase::Suspended;
        return fx;
    }
    if (obs.mode == Mode::Gui)
    {
        // A menu over the fight pauses the game itself; everything waits.
        return fx;
    }

    if (!obs.paused)
    {
        clock_ += dt;
        roundRunMs_ += dt;
    }
    ApplyStatuses(obs.statuses);
    if (currentStunned_ && (phase_ == Phase::AwaitingOrder || phase_ == Phase::Dispatching ||
                            phase_ == Phase::AiThinking || phase_ == Phase::Executing || phase_ == Phase::Settling))
    {
        currentStunned_ = false;
        // Stunned in its own turn (a riposte, say): the rest of it is lost
        // now, not waited out, and its next turn with it. Anything queued
        // for it since the stun landed would run when the stun wears off,
        // in someone else's turn.
        if (const Combatant *current = order_.Current())
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));
        }
        EndTurn(fx, "stunned");
        EnforcePause(obs, fx);
        return fx;
    }

    switch (phase_)
    {
    case Phase::Starting:
        StepStarting(obs, fx);
        break;
    case Phase::TurnStart:
        StepTurnStart(obs, fx);
        break;
    case Phase::AwaitingOrder:
        StepAwaiting(obs, fx);
        break;
    case Phase::Dispatching:
        StepDispatching(obs, fx);
        break;
    case Phase::AiThinking:
        StepThinking(obs, fx);
        break;
    case Phase::Executing:
        StepExecuting(obs, fx);
        break;
    case Phase::Settling:
        StepSettling(obs, fx);
        break;
    case Phase::RoundPadding:
        StepPadding(fx);
        break;
    case Phase::Idle:
    case Phase::Suspended:
        break;
    }
    EnforcePause(obs, fx);
    return fx;
}

void TurnMachine::StepIdle(const Observation &obs, std::vector<Effect> &fx)
{
    if (obs.mode != Mode::Combat)
    {
        latch_ = false;
        return;
    }
    if (!settings_.enabled)
    {
        return;
    }
    if (!latch_)
    {
        Log(LogLevel::Info, "combat began: turn-based");
        phase_ = Phase::Starting;
        wantScan_ = true;
        clock_ = 0;
        roundRunMs_ = 0;
        fx.push_back(Effect::Of(EffectKind::Pause));
        return;
    }
    if (obs.scan && std::ranges::any_of(*obs.scan, [](const Combatant &c) { return c.side == Side::Enemy && !c.down; }))
    {
        Log(LogLevel::Info, "hostiles are fighting again: turn-based");
        latch_ = false;
        phase_ = Phase::Starting;
        clock_ = 0;
        roundRunMs_ = 0;
        fx.push_back(Effect::Of(EffectKind::Pause));
        StartFight(*obs.scan, fx);
    }
}

void TurnMachine::StepSuspended(const Observation &obs, std::vector<Effect> &fx)
{
    if (obs.mode == Mode::Combat)
    {
        Log(LogLevel::Info, "back to the fight: the current turn goes on");
        // The gate stood aside while it played: whatever anyone was left
        // doing stops now, as at the fight's start, the one whose turn it is
        // included (a cutscene's orders are not its choices). Its turn then
        // goes on with what it had left.
        for (const Combatant &c : order_.All())
        {
            if (!c.down)
            {
                fx.push_back(Effect::Of(EffectKind::ClearCommands, c.id));
            }
        }
        resume_ = turnBegun_;
        phase_ = Phase::TurnStart;
        wantScan_ = true;
        fx.push_back(Effect::Of(EffectKind::Pause));
    }
    else if (obs.mode == Mode::Explore || obs.mode == Mode::Dead)
    {
        End(fx, std::format("the fight ended in {}", obs.mode == Mode::Dead ? "the party's death" : "exploring"));
    }
}

void TurnMachine::StepStarting(const Observation &obs, std::vector<Effect> &fx)
{
    if (!obs.scan)
    {
        wantScan_ = true;
        return;
    }
    StartFight(*obs.scan, fx);
}

void TurnMachine::StartFight(std::vector<Combatant> roster, std::vector<Effect> &fx)
{
    held_.clear();
    currentStunned_ = false;
    usedNow_.clear();
    holds_.clear();
    if (std::ranges::none_of(roster, [](const Combatant &c) { return c.side == Side::Enemy && !c.down; }))
    {
        Log(LogLevel::Info, std::format("combat, but no enemy fighting within {:.0f} m: real-time until one is",
                                        settings_.combatRadius));
        phase_ = Phase::Idle;
        latch_ = true;
        fx.push_back(Effect::Of(EffectKind::Unpause));
        return;
    }
    for (Combatant &c : roster)
    {
        c.initiative = RollInitiative(rng_, c.dexterity, settings_);
        if (c.hero)
        {
            hero_ = c.id;
        }
    }
    order_.Begin(std::move(roster));
    Log(LogLevel::Info, std::format("the fight: {} combatants; {}", order_.All().size(), DescribeOrder()));

    // Whatever anyone was doing in real time stops: the first turn starts
    // from a still world.
    for (const Combatant &c : order_.All())
    {
        if (!c.down)
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, c.id));
        }
    }
    Signal(fx, events::kCombatStart, kNoObject, static_cast<std::int32_t>(order_.All().size()), 0);
    Signal(fx, events::kRoundStart, kNoObject, 1, 0);
    Floaty(fx, hero_, L"Turn-based combat", kGold);
    roundRunMs_ = 0;
    roundPending_ = false;
    // The first turn begins with the next frame, which reads everyone's
    // conditions first: one pinned or stunned as the fight began is held.
    phase_ = Phase::TurnStart;
    wantScan_ = true;
}

void TurnMachine::StepTurnStart(const Observation &obs, std::vector<Effect> &fx)
{
    if (!obs.scan)
    {
        wantScan_ = true;
        return;
    }
    Merge(*obs.scan, fx);
    StartNextTurn(fx);
}

void TurnMachine::StartNextTurn(std::vector<Effect> &fx)
{
    if (!order_.AnyStanding(Side::Enemy))
    {
        End(fx, "no enemy left standing");
        return;
    }
    if (roundPending_)
    {
        roundPending_ = false;
        const auto minMs = static_cast<std::uint32_t>(settings_.roundMinSeconds * 1000.0f);
        if (roundRunMs_ < minMs)
        {
            deadline_ = clock_ + (minMs - roundRunMs_);
            if (Logs(LogLevel::Debug))
            {
                Log(LogLevel::Debug,
                    std::format("the round ran {} ms of {}: the world runs on, nobody acting", roundRunMs_, minMs));
            }
            phase_ = Phase::RoundPadding;
            return;
        }
        roundRunMs_ = 0;
        Signal(fx, events::kRoundStart, kNoObject, order_.Round(), 0);
        Log(LogLevel::Info, std::format("round {}: {}", order_.Round(), DescribeOrder()));
    }
    const Combatant *current = order_.Current();
    if (current == nullptr)
    {
        End(fx, "nobody left");
        return;
    }
    if (current->down)
    {
        resume_ = false;
        EndTurn(fx, "down");
        return;
    }
    // Stunned as its turn was interrupted (a conversation came between):
    // the rest of it is lost, not resumed. A turn yet to begin is taken
    // by LoseTurn instead.
    const bool stunned = std::exchange(currentStunned_, false);
    if (std::exchange(resume_, false))
    {
        if (stunned)
        {
            EndTurn(fx, "stunned");
            return;
        }
        ResumeTurn(*current, fx);
        return;
    }
    if (LoseTurn(*current, fx))
    {
        return;
    }
    BeginTurn(*current, fx);
}

void TurnMachine::StepPadding(std::vector<Effect> &fx)
{
    if (clock_ < deadline_)
    {
        return;
    }
    roundRunMs_ = 0;
    Signal(fx, events::kRoundStart, kNoObject, order_.Round(), 0);
    Log(LogLevel::Info, std::format("round {}: {}", order_.Round(), DescribeOrder()));
    phase_ = Phase::TurnStart;
    wantScan_ = true;
}

void TurnMachine::BeginTurn(const Combatant &who, std::vector<Effect> &fx)
{
    budget_ = Budget{};
    budget_.movementMax = MovementBudget(settings_, who.speed, who.dexterity);
    idleTicks_ = 0;
    lastPosition_.reset();
    endTurnRequested_ = false;
    passRequested_ = false;
    orderPending_ = false;

    HoldAtTurnStart(who, fx);

    Log(LogLevel::Info, std::format("round {}: {}'s turn ({}, {}), {:.1f} m to move", order_.Round(), Utf8(who.name),
                                    Name(who.side), PlayerControls(who) ? "yours" : "AI", budget_.MovementLeft()));
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
    TickCooldowns(who, fx);
    // An AI's turn is shown once it has chosen (Judge): centring the camera
    // on an enemy just before its AI was told its turn had come left the AI
    // choosing nothing, every turn, in play (2026-09-30).
    if (settings_.cameraFollow && PlayerControls(who))
    {
        fx.push_back(Effect::Of(EffectKind::CenterCamera, who.id));
    }
    if (PlayerControls(who))
    {
        Floaty(fx, who.id, who.name + L"'s turn", kGold);
    }
    else
    {
        Floaty(fx, who.id, who.name, who.side == Side::Enemy ? kRed : kGrey);
    }
    Handover(who, fx);
}

// After a conversation or cutscene: the same turn, with what it had left
// (a fresh one would give its action back), and no second turn-start event.
void TurnMachine::ResumeTurn(const Combatant &who, std::vector<Effect> &fx)
{
    idleTicks_ = 0;
    lastPosition_.reset();
    orderPending_ = false;
    Log(LogLevel::Info, std::format("{}'s turn goes on: {} action, {:.1f} m left", Utf8(who.name), budget_.actions,
                                    budget_.MovementLeft()));
    Handover(who, fx);
}

// The turn to whoever plays it: the player waits for an order; an AI is
// told to choose.
// A turn a condition costs: it is taken at once, the world still, as the
// condition's own time is the turns (dev/DESIGN.md, "Conditions in turns").
bool TurnMachine::LoseTurn(const Combatant &who, std::vector<Effect> &fx)
{
    const auto found = held_.find(who.id);
    if (found == held_.end() || found->second.kind != Condition::Disabled || found->second.turns <= 0)
    {
        return false;
    }
    Held &held = found->second;
    --held.turns;
    budget_ = Budget{};
    budget_.actions = 0;
    Log(LogLevel::Info,
        std::format("round {}: {}'s turn is lost to a stun ({} more)", order_.Round(), Utf8(who.name), held.turns));
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
    TickCooldowns(who, fx);
    Floaty(fx, who.id, who.name + L" is stunned", kGrey);
    EndTurn(fx, "stunned");
    return true;
}

// At the start of a turn that is taken: a pin that still owes turns takes
// the movement; a condition whose turns are spent but that the engine still
// holds is ended now, before the creature is told its turn has come.
void TurnMachine::HoldAtTurnStart(const Combatant &who, std::vector<Effect> &fx)
{
    const auto found = held_.find(who.id);
    if (found == held_.end())
    {
        return;
    }
    Held &held = found->second;
    if (held.kind == Condition::Rooted && held.turns > 0)
    {
        --held.turns;
        budget_.movementMax = 0.0f;
        Log(LogLevel::Info, std::format("{} is pinned: no movement this turn ({} more)", Utf8(who.name), held.turns));
        Floaty(fx, who.id, L"Pinned", kGrey);
        return;
    }
    if (held.turns <= 0 && held.seen != Condition::None)
    {
        Log(LogLevel::Info, std::format("{}'s condition has had its turns: ended", Utf8(who.name)));
        fx.push_back(Effect::Of(EffectKind::EndConditions, who.id));
        held.kind = Condition::None;
        // Gone once the effect is applied: a new stun after it is new.
        held.seen = Condition::None;
    }
}

namespace
{
Effect CooldownOf(ObjectId creature, std::int32_t ability, std::int32_t seconds)
{
    Effect e = Effect::Of(EffectKind::Cooldown, creature);
    e.a = ability;
    e.b = seconds;
    return e;
}
} // namespace

// Cooldowns in turns (dev/DESIGN.md, "Cooldowns in turns"): the abilities
// the last action used are held, in the engine, where waiting in real time
// cannot bring them back.
void TurnMachine::HoldUsed(std::vector<Effect> &fx)
{
    for (const Hold &used : usedNow_)
    {
        std::erase_if(holds_, [&](const Hold &h) { return h.creature == used.creature && h.ability == used.ability; });
        holds_.push_back(used);
        fx.push_back(CooldownOf(used.creature, used.ability, kCooldownHoldSeconds));
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug, std::format("ability {} of {:#010x} held for {} of its user's turns", used.ability,
                                             used.creature, used.turns));
        }
    }
    usedNow_.clear();
}

// A turn of its user passes (taken or lost): what has waited its turns is
// ready now, before the creature is told its turn has come.
void TurnMachine::TickCooldowns(const Combatant &who, std::vector<Effect> &fx)
{
    for (Hold &h : holds_)
    {
        if (h.creature == who.id && --h.turns <= 0)
        {
            fx.push_back(CooldownOf(h.creature, h.ability, 0));
            Log(LogLevel::Info, std::format("{}'s ability {} is ready again", Utf8(who.name), h.ability));
        }
    }
    std::erase_if(holds_, [](const Hold &h) { return h.turns <= 0; });
}

// The fight is over: what still waits goes back to seconds, its turns' worth.
void TurnMachine::ReleaseCooldowns(std::vector<Effect> &fx)
{
    for (const Hold &h : holds_)
    {
        fx.push_back(CooldownOf(
            h.creature, h.ability,
            static_cast<std::int32_t>(std::ceil(static_cast<float>(h.turns) * settings_.cooldownSecondsPerTurn))));
    }
    holds_.clear();
}

void TurnMachine::Handover(const Combatant &who, std::vector<Effect> &fx)
{
    if (PlayerControls(who))
    {
        fx.push_back(Effect::Of(EffectKind::Select, who.id));
        phase_ = Phase::AwaitingOrder;
        return;
    }
    // A companion whose tactics play it must not be the one the player
    // controls: a controlled creature's AI only carries on attacking
    // (AI_DetermineCombatRound_Partial, ai_main_h_2.nss).
    if (who.side == Side::Party && hero_ != kNoObject && hero_ != who.id)
    {
        fx.push_back(Effect::Of(EffectKind::Select, hero_));
    }
    fx.push_back(Effect::Of(EffectKind::PokeAi, who.id));
    deadline_ = clock_ + static_cast<std::uint32_t>(settings_.aiThinkMs);
    phase_ = Phase::AiThinking;
}

void TurnMachine::StepAwaiting(const Observation &obs, std::vector<Effect> &fx)
{
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    if (endTurnRequested_)
    {
        EndTurn(fx, "ended by the player");
        return;
    }
    if (orderPending_ && obs.nowMs - orderSeenNow_ >= kDispatchAfterMs)
    {
        orderPending_ = false;
        if (settings_.orderRunMs <= 0)
        {
            // Seen in play (2026-09-30): an ability picked before its
            // target, say. The world stays still.
            Log(LogLevel::Debug, "the order queued no command");
            return;
        }
        // An order and, paused, no command: the engine may queue it only
        // when the game runs. The gate still holds everyone else.
        dispatchUntilNow_ = obs.nowMs + static_cast<std::uint32_t>(settings_.orderRunMs);
        Log(LogLevel::Info, "an order but no command while paused: running a moment for it");
        phase_ = Phase::Dispatching;
        return;
    }
    if (obs.actor && obs.actor->busy)
    {
        // Something the gate did not pass: a recovery from a knockdown, say.
        // It runs, and the player gets the turn back after.
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug,
                std::format("busy with a {} the gate did not see; letting it run", Name(obs.actor->current)));
        }
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
    }
}

void TurnMachine::StepDispatching(const Observation &obs, std::vector<Effect> &fx)
{
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    if (endTurnRequested_)
    {
        EndTurn(fx, "ended by the player");
        return;
    }
    if (obs.actor && obs.actor->busy)
    {
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
        return;
    }
    // Real time, not the fight's clock: this is about the engine, not the
    // world.
    if (static_cast<std::int32_t>(obs.nowMs - dispatchUntilNow_) >= 0)
    {
        Log(LogLevel::Warn, "the order queued no command: waiting for another");
        phase_ = Phase::AwaitingOrder;
    }
}

void TurnMachine::StepThinking(const Observation &obs, std::vector<Effect> &fx)
{
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    if (passRequested_)
    {
        EndTurn(fx, "waits");
        return;
    }
    if (obs.actor && obs.actor->busy)
    {
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
        return;
    }
    if (clock_ >= deadline_)
    {
        // What its AI saw says why. In the first fights in play every
        // enemy chose nothing, turn after turn; camera follow's centring
        // just before the poke was the cause (2026-09-30).
        if (const Combatant *current = order_.Current())
        {
            fx.push_back(Effect::Of(EffectKind::Explain, current->id));
        }
        EndTurn(fx, "chose nothing");
    }
}

void TurnMachine::StepExecuting(const Observation &obs, std::vector<Effect> &fx)
{
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    if (!obs.actor)
    {
        return;
    }
    const ActorObservation &actor = *obs.actor;
    TrackMovement(actor, fx);
    if (phase_ != Phase::Executing)
    {
        return;
    }
    if (actor.busy)
    {
        idleTicks_ = 0;
        if (clock_ - executingSince_ > static_cast<std::uint32_t>(settings_.maxActionMs))
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
            Log(LogLevel::Warn,
                std::format("still at a {} after {} ms: stopped", Name(actor.current), settings_.maxActionMs));
            EndTurn(fx, "took too long");
        }
        return;
    }
    // Idle two frames running: one can fall between one command and the
    // next the engine queues after it.
    if (++idleTicks_ < 2)
    {
        return;
    }
    const Combatant *current = order_.Current();
    const bool player = current != nullptr && PlayerControls(*current);
    const int wait = player ? settings_.settleMs : std::max(settings_.settleMs, settings_.aiFollowUpMs);
    deadline_ = clock_ + static_cast<std::uint32_t>(wait);
    phase_ = Phase::Settling;
}

void TurnMachine::StepSettling(const Observation &obs, std::vector<Effect> &fx)
{
    HoldUsed(fx); // the action has run: its abilities' cooldowns are the turns'
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    if (obs.actor && obs.actor->busy)
    {
        // A follow-up the gate let through (a move after an attack).
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
        return;
    }
    const Combatant *current = order_.Current();
    if (current == nullptr)
    {
        EndTurn(fx, "gone");
        return;
    }
    const bool player = PlayerControls(*current);
    if (!player && passRequested_)
    {
        EndTurn(fx, "done");
        return;
    }
    if (clock_ < deadline_)
    {
        return;
    }
    if (!player)
    {
        EndTurn(fx, "done");
        return;
    }
    if (endTurnRequested_)
    {
        EndTurn(fx, "ended by the player");
        return;
    }
    if (TurnSpent())
    {
        EndTurn(fx, "nothing left to spend");
        return;
    }
    if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug,
            std::format("awaiting the next order: {} action, {:.1f} m left", budget_.actions, budget_.MovementLeft()));
    }
    phase_ = Phase::AwaitingOrder;
}

void TurnMachine::TrackMovement(const ActorObservation &actor, std::vector<Effect> &fx)
{
    if (lastPosition_ && actor.moving)
    {
        const float step = Distance(*lastPosition_, actor.position);
        if (step <= kTeleport)
        {
            budget_.movementUsed += step;
        }
    }
    lastPosition_ = actor.position;
    if (!actor.moving || budget_.movementUsed < budget_.movementMax + kOvershoot)
    {
        return;
    }

    fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
    // Stopped on the way to an attack or ability, it never used it: it is
    // given back, once a turn. Every later try to reach something takes a
    // step before it is stopped, so a second refund would let it creep on
    // a frame at a time (test_fights.cpp found it).
    const bool refund = budget_.lastKind == CommandKind::Action && budget_.actions == 0 && !budget_.refunded;
    if (refund)
    {
        budget_.actions = 1;
        budget_.refunded = true;
    }
    budget_.outOfMovement = true;
    const Combatant *current = order_.Current();
    Floaty(fx, actor.id, L"Out of movement", kGrey);
    Log(LogLevel::Info, std::format("{} is out of movement ({:.1f} of {:.1f} m){}",
                                    current != nullptr ? Utf8(current->name) : std::string{"?"}, budget_.movementUsed,
                                    budget_.movementMax, refund ? "; the action it was moving for is kept" : ""));
    if (current != nullptr && !PlayerControls(*current))
    {
        EndTurn(fx, "out of movement");
    }
}

void TurnMachine::EndTurn(std::vector<Effect> &fx, std::string_view why)
{
    HoldUsed(fx);
    if (const Combatant *current = order_.Current())
    {
        Log(LogLevel::Info, std::format("{}'s turn ends: {}", Utf8(current->name), why));
        Signal(fx, events::kTurnEnd, current->id, order_.Round(), 0);
    }
    if (order_.Advance())
    {
        roundPending_ = true;
    }
    budget_ = Budget{};
    budget_.actions = 0;
    phase_ = Phase::TurnStart;
    wantScan_ = true;
}

void TurnMachine::End(std::vector<Effect> &fx, std::string_view why)
{
    Log(LogLevel::Info, std::format("the turn-based fight is over after {} rounds: {}", order_.Round(), why));
    HoldUsed(fx);
    ReleaseCooldowns(fx);
    resume_ = false;
    Signal(fx, events::kCombatEnd, kNoObject, order_.Round(), 0);
    fx.push_back(Effect::Of(EffectKind::Unpause));
    order_.Clear();
    held_.clear();
    budget_ = Budget{};
    phase_ = Phase::Idle;
    latch_ = true;
    wantScan_ = false;
}

void TurnMachine::Merge(const std::vector<Combatant> &scan, std::vector<Effect> &fx)
{
    for (const Combatant &seen : scan)
    {
        if (seen.hero)
        {
            hero_ = seen.id;
        }
        if (Combatant *known = order_.Find(seen.id))
        {
            const int initiative = known->initiative;
            *known = seen;
            known->initiative = initiative;
        }
        else if (!seen.down)
        {
            Combatant joining = seen;
            joining.initiative = RollInitiative(rng_, seen.dexterity, settings_);
            Log(LogLevel::Info, std::format("{} joins the fight ({}, initiative {})", Utf8(joining.name),
                                            Name(joining.side), joining.initiative));
            // Whatever it was doing as it came (a patrol, an attack queued as
            // its combat state turned, a cutscene's walk) stops: it waits for
            // its first turn like everyone else.
            fx.push_back(Effect::Of(EffectKind::ClearCommands, joining.id));
            order_.Join(std::move(joining));
        }
    }
    // An enemy or ally no longer seen fighting has left it: dead and gone,
    // fled out of range, or out of combat.
    std::vector<ObjectId> absent;
    for (const Combatant &c : order_.All())
    {
        if (c.side != Side::Party && !c.down &&
            std::ranges::none_of(scan, [&](const Combatant &s) { return s.id == c.id; }))
        {
            absent.push_back(c.id);
        }
    }
    for (const ObjectId id : absent)
    {
        Combatant *c = order_.Find(id);
        c->down = true;
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug, std::format("{} is no longer fighting", Utf8(c->name)));
        }
    }
}

void TurnMachine::ApplyStatuses(const std::vector<CombatantStatus> &statuses)
{
    for (const CombatantStatus &status : statuses)
    {
        Combatant *c = order_.Find(status.id);
        if (c == nullptr)
        {
            continue;
        }
        c->health = status.health;
        Held &held = held_[status.id];
        if (status.condition != Condition::None && status.condition != held.seen && settings_.conditionTurns > 0)
        {
            // Newly held (or held worse): it owes that many of its turns,
            // whatever the condition's timer in seconds says.
            if (status.condition > held.kind || held.turns == 0)
            {
                held.kind = status.condition;
            }
            held.turns = std::max(held.turns, settings_.conditionTurns);
            Log(LogLevel::Info, std::format("{} is {}: {} of its turns {}", Utf8(c->name),
                                            status.condition == Condition::Disabled ? "stunned" : "pinned", held.turns,
                                            status.condition == Condition::Disabled ? "lost" : "without movement"));
            const Combatant *current = order_.Current();
            if (status.condition == Condition::Disabled && current != nullptr && current->id == status.id)
            {
                currentStunned_ = true;
            }
        }
        held.seen = status.condition;
        if (!status.exists)
        {
            c->down = c->down || c->side != Side::Party;
        }
        else
        {
            c->down = status.down;
        }
    }
}

void TurnMachine::EnforcePause(const Observation &obs, std::vector<Effect> &fx) const
{
    if (phase_ == Phase::Idle || phase_ == Phase::Suspended)
    {
        return;
    }
    const bool want = WantsPaused(phase_, settings_);
    if (want != obs.paused)
    {
        fx.push_back(Effect::Of(want ? EffectKind::Pause : EffectKind::Unpause));
    }
}

bool TurnMachine::PlayerControls(const Combatant &c) const
{
    return c.side == Side::Party && (settings_.partyControl == PartyControl::Manual || c.hero);
}

bool TurnMachine::TurnSpent() const
{
    if (budget_.actions > 0)
    {
        return false;
    }
    if (settings_.endTurnWhenActionUsed)
    {
        return true;
    }
    return budget_.outOfMovement || budget_.MovementLeft() < kMinStep;
}

bool TurnMachine::ActorGone(const Observation &obs) const
{
    return obs.actor && (!obs.actor->exists || obs.actor->down);
}

void TurnMachine::Floaty(std::vector<Effect> &fx, ObjectId target, std::wstring text, std::uint32_t colour) const
{
    if (!settings_.floaties || target == kNoObject)
    {
        return;
    }
    Effect effect = Effect::Of(EffectKind::Floaty, target);
    effect.text = std::move(text);
    effect.colour = colour;
    fx.push_back(std::move(effect));
}

void TurnMachine::Signal(std::vector<Effect> &fx, std::int32_t event, ObjectId object, std::int32_t a,
                         std::int32_t b) const
{
    if (!settings_.scriptEvents)
    {
        return;
    }
    Effect effect = Effect::Of(EffectKind::Signal, object);
    effect.event = event;
    effect.a = a;
    effect.b = b;
    fx.push_back(std::move(effect));
}

void TurnMachine::Refuse(ObjectId target, std::wstring text)
{
    // One a second: a player clicking again and again gets one message.
    if (anyRefusal_ && lastNow_ - lastRefusalNow_ < 1000)
    {
        return;
    }
    anyRefusal_ = true;
    lastRefusalNow_ = lastNow_;
    Floaty(pending_, target, std::move(text), kGrey);
}

Verdict TurnMachine::OnCommand(const CommandRequest &request)
{
    const Verdict verdict = Judge(request);
    if (request.source == Source::Player)
    {
        // The order has its command, taken or refused: nothing to run the
        // world for. A refusal ran it for half a second in play, every
        // click after an action was spent (2026-09-30).
        orderPending_ = false;
        if (verdict == Verdict::Block && phase_ == Phase::Dispatching)
        {
            phase_ = Phase::AwaitingOrder;
        }
    }
    return verdict;
}

Verdict TurnMachine::Judge(const CommandRequest &request)
{
    CommandKind kind = Classify(request.type, request.source);
    // A sustained ability is turned on or off for nothing (the user's rule,
    // 2026-09-30); action points will put a price on it.
    if (request.type == CommandType::UseAbility && request.sustained && settings_.cooldownSecondsPerTurn > 0.0f)
    {
        kind = CommandKind::Free;
    }
    // The player's own clicks refused are few and worth reading in play
    // (an order that seemed to need several clicks, 2026-09-30); anyone
    // else's are many.
    const LogLevel refusals = request.source == Source::Player ? LogLevel::Info : LogLevel::Debug;
    if (!Active() || kind == CommandKind::Involuntary)
    {
        return Verdict::Allow;
    }
    Combatant *who = order_.Find(request.object);
    if (who == nullptr)
    {
        if (request.inCombat && kind != CommandKind::Free)
        {
            // A creature joining the fight: it waits for its first turn.
            wantScan_ = true;
            if (Logs(LogLevel::Debug))
            {
                Log(LogLevel::Debug, std::format("{:#010x}, fighting but not yet in the order: its {} waits",
                                                 request.object, Name(request.type)));
            }
            return Verdict::Block;
        }
        return Verdict::Allow;
    }
    const Combatant *current = order_.Current();
    if (current == nullptr || current->id != request.object)
    {
        if (request.source == Source::Player)
        {
            Refuse(who->id, L"Not " + who->name + L"'s turn");
        }
        if (Logs(refusals))
        {
            Refused(refusals, std::format("{}: {} from {} held, not its turn", Utf8(who->name), Name(request.type),
                                          Name(request.source)));
        }
        return Verdict::Block;
    }
    if (phase_ != Phase::AwaitingOrder && phase_ != Phase::Dispatching && phase_ != Phase::AiThinking &&
        phase_ != Phase::Executing && phase_ != Phase::Settling)
    {
        if (Logs(refusals))
        {
            Refused(refusals,
                    std::format("{}: {} held, between turns ({})", Utf8(who->name), Name(request.type), Name(phase_)));
        }
        return Verdict::Block;
    }
    const bool player = PlayerControls(*current);
    const bool asked = request.source == Source::Player; // the player's own order
    // On a turn the player plays, the character's own AI does not spend it:
    // a controlled character carries on attacking by itself (in real time,
    // AI_DetermineCombatRound_Partial), and in play that ended a pinned
    // character's turn before the player had chosen (2026-09-30, 11:30).
    if (player && request.source == Source::Script && (kind == CommandKind::Action || kind == CommandKind::Move))
    {
        // At debug: the AI tries again each time a command of its ends.
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug, std::format("{}: {} from its own AI held: its turn is the player's", Utf8(who->name),
                                             Name(request.type)));
        }
        return Verdict::Block;
    }
    // A guard against an AI that loops; the player steering with the keys
    // starts a command a step.
    if (!player && budget_.commands >= settings_.maxCommandsPerTurn)
    {
        passRequested_ = true;
        Log(LogLevel::Info,
            std::format("{} has started {} commands this turn: no more", Utf8(who->name), budget_.commands));
        return Verdict::Block;
    }
    switch (kind)
    {
    case CommandKind::Action:
        if (budget_.actions <= 0)
        {
            if (player && asked)
            {
                Refuse(who->id, L"No action left");
            }
            if (Logs(refusals))
            {
                Refused(refusals, std::format("{}: {} held, no action left", Utf8(who->name), Name(request.type)));
            }
            return Verdict::Block;
        }
        // The player's order at something out of reach this turn is
        // refused before a step is taken, as BG3 refuses a red target. An
        // AI's goes on and walks what it can: closing in is its turn.
        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)
        {
            Refuse(who->id, L"Too far");
            Log(LogLevel::Info, std::format("{}: {} refused, {:.1f} m to walk with {:.1f} m left", Utf8(who->name),
                                            Name(request.type), *request.walk, budget_.MovementLeft()));
            return Verdict::Block;
        }
        --budget_.actions;
        break;
    case CommandKind::Move:
        if (budget_.outOfMovement || budget_.MovementLeft() < kMinStep)
        {
            if (player && asked)
            {
                Refuse(who->id, L"No movement left");
            }
            if (Logs(refusals))
            {
                Refused(refusals, std::format("{}: {} held, no movement left", Utf8(who->name), Name(request.type)));
            }
            return Verdict::Block;
        }
        break;
    case CommandKind::Pass:
        if (!player)
        {
            passRequested_ = true;
            if (Logs(LogLevel::Debug))
            {
                Log(LogLevel::Debug, std::format("{} chose to wait", Utf8(who->name)));
            }
            return Verdict::Block;
        }
        break;
    case CommandKind::Free:
    case CommandKind::Involuntary:
        break;
    }
    if (request.type == CommandType::UseAbility && request.ability >= 0 && settings_.cooldownSecondsPerTurn > 0.0f)
    {
        // Held once the action has run (HoldUsed): a cooldown set before
        // the ability runs might stop it running.
        const int turns = request.sustained
                              ? (request.sustainedOn ? 1 : 0)
                              : static_cast<int>(std::ceil(request.cooldownSeconds / settings_.cooldownSecondsPerTurn));
        if (turns > 0)
        {
            usedNow_.push_back(Hold{who->id, request.ability, turns});
        }
    }
    if (settings_.cameraFollow && !player && budget_.commands == 0)
    {
        // Applied with the next frame's effects, after the AI's call into
        // the gate has returned.
        pending_.push_back(Effect::Of(EffectKind::CenterCamera, who->id));
    }
    ++budget_.commands;
    budget_.lastKind = kind;
    orderPending_ = false;
    Log(LogLevel::Info,
        std::format("{}: {} ({}, from {}); {} action and {:.1f} m left", Utf8(who->name), Name(request.type),
                    Name(kind), Name(request.source), budget_.actions, budget_.MovementLeft()));
    if (phase_ != Phase::Executing)
    {
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
    }
    return Verdict::Allow;
}

void TurnMachine::OnPlayerOrder(std::string_view what)
{
    if (phase_ != Phase::AwaitingOrder)
    {
        return;
    }
    orderPending_ = true;
    orderSeenNow_ = lastNow_;
    if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug, std::format("the player's order: {}", what));
    }
}

bool TurnMachine::OnPauseKey()
{
    if (!Active())
    {
        return false;
    }
    const Combatant *current = order_.Current();
    if (current != nullptr && PlayerControls(*current) &&
        (phase_ == Phase::AwaitingOrder || phase_ == Phase::Dispatching || phase_ == Phase::Executing ||
         phase_ == Phase::Settling))
    {
        endTurnRequested_ = true;
        Log(LogLevel::Info, std::format("{}'s turn: end turn pressed", Utf8(current->name)));
    }
    return true;
}

ToggleResult TurnMachine::RequestToggle(bool partyInCombat)
{
    if (phase_ != Phase::Idle || lastMode_ == Mode::Combat || partyInCombat)
    {
        return ToggleResult::RefusedInCombat;
    }
    settings_.enabled = !settings_.enabled;
    Log(LogLevel::Info, std::format("turn-based combat {}", settings_.enabled ? "on" : "off"));
    return settings_.enabled ? ToggleResult::Enabled : ToggleResult::Disabled;
}

TurnView TurnMachine::View() const
{
    TurnView view;
    view.active = phase_ != Phase::Idle;
    view.phase = phase_;
    view.round = order_.Round();
    if (const Combatant *current = order_.Current(); current != nullptr && phase_ != Phase::Idle)
    {
        view.current = current->id;
        view.playerTurn = PlayerControls(*current);
        view.actionsLeft = budget_.actions;
        view.movementLeft = budget_.MovementLeft();
        view.movementMax = budget_.movementMax;
    }
    for (const TurnOrder::Slot &slot : order_.Upcoming(12))
    {
        const Combatant &c = *slot.who;
        view.upcoming.push_back(
            {c.id, c.name, c.tag, c.side, c.rank, c.health, c.down, slot.current, PlayerControls(c), slot.round});
    }
    return view;
}

std::string TurnMachine::DescribeOrder() const
{
    std::string out;
    const Combatant *current = order_.Current();
    for (const Combatant &c : order_.All())
    {
        if (!out.empty())
        {
            out += ", ";
        }
        out += std::format("{}{} {} ({}){}", &c == current ? "*" : "", Utf8(c.name), c.initiative, Name(c.side),
                           c.down ? " down" : "");
    }
    return out;
}

} // namespace tb
