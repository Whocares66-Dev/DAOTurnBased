#include "core/TurnMachine.h"

#include "core/GivenBack.h"
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
    case EffectKind::HoldTime:
        return "hold time";
    case EffectKind::AgeTime:
        return "age time";
    case EffectKind::OwnClock:
        return "own clock";
    case EffectKind::GameSpeed:
        return "game speed";
    case EffectKind::ReleaseCast:
        return "release cast";
    case EffectKind::DropCast:
        return "drop cast";
    case EffectKind::Approach:
        return "approach";
    }
    return "?";
}

TurnMachine::TurnMachine(Settings settings, std::uint64_t seed, LogSink log)
    : settings_(std::move(settings)), rng_(seed), log_(std::move(log))
{
}

void TurnMachine::SetSettings(const Settings &settings)
{
    const Combatant *current = order_.Current();
    const bool playerBefore = current != nullptr && PlayerControls(*current);
    settings_ = settings;
    Emit(EventKind::SettingsChanged);
    if (current == nullptr || !Active() || PlayerControls(*current) == playerBefore)
    {
        return;
    }
    const char *now = PlayerControls(*current) ? "the player's" : "its AI's";
    Emit(EventKind::TurnHandedOver, current->id, now);
    // Who plays the current turn changed with the settings (party manual to
    // tactics in a companion's turn): it goes to whoever plays it now, with
    // what it has left. Waiting on the player's order for a companion its AI
    // now played stalled the fight, End gone from the bar and Space doing
    // nothing (the second play test, 2026-09-30).
    if (phase_ == Phase::AwaitingOrder || phase_ == Phase::Dispatching || phase_ == Phase::AiThinking)
    {
        Log(LogLevel::Info, std::format("{}'s turn is {} now", Utf8(current->name), now));
        Handover(*current, pending_);
        return;
    }
    // Mid-command (an AI acts within milliseconds of being told, the fourth
    // play test): the turn goes on, and whoever plays it now takes it as the
    // command ends.
    Log(LogLevel::Info, std::format("{}'s turn is {} now, from the end of its command", Utf8(current->name), now));
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

void TurnMachine::Emit(EventKind kind, ObjectId who, std::string detail, std::int32_t value, std::string command,
                       std::string note) const
{
    if (!events_)
    {
        return;
    }
    TurnEvent event;
    event.kind = kind;
    event.worldMs = clock_;
    event.round = order_.Round();
    event.who = who;
    if (const Combatant *c = who != kNoObject ? order_.Find(who) : nullptr)
    {
        event.name = Utf8(c->name);
    }
    event.detail = std::move(detail);
    event.value = value;
    event.command = std::move(command);
    event.note = std::move(note);
    events_(event);
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
    // Every half second; fresh at every turn start: whether a creature is
    // stunned decides whether its turn is taken at all; and when asked for
    // (statusWanted_): as an action ends and before the turn goes on after
    // it, so that the cards and the state are what the action left.
    if (nowMs - lastStatusNow_ >= 500 || phase_ == Phase::TurnStart || std::exchange(statusWanted_, false))
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
    seenActor_ = obs.actor;
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
        playerPaused_ = false;
        latch_ = false;
        wantScan_ = false;
        const bool charging = !charges_.empty() || landing_.has_value();
        charges_.clear();
        rollover_.clear();
        grabbedMs_.clear();
        landing_.reset();
        chargeBegan_ = false;
        approach_ = false;
        fx.clear(); // their objects may be gone with the world
        if (charging)
        {
            fx.push_back(Effect::Of(EffectKind::DropCast)); // names no object: every held cast
        }
        // Not the clock, which is no object's: it must not stay held, nor
        // the game at an enemy's speed.
        HoldTime(false, fx);
        SetSpeed(1.0f, fx);
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
        // Before the fight had started: it starts afresh after (the arena,
        // seed 256 of the long run, 2026-09-30: a conversation in combat's
        // first frame skipped the fight's start, effects' time never held).
        startInterrupted_ = startInterrupted_ || phase_ == Phase::Starting;
        phase_ = Phase::Suspended;
        playerPaused_ = false;
        SetSpeed(1.0f, fx);
        return fx;
    }
    if (obs.mode == Mode::Gui)
    {
        // A menu over the fight pauses the game itself; everything waits.
        return fx;
    }

    if (!obs.paused)
    {
        // The world's time: at an enemy's speed it runs faster, and the
        // waits that count in it (settling, an AI's thinking and follow-up)
        // with it. In real time they held an AI's turn at twice the speed to the same 3 s
        // an enemy's turn took at 1 (the desktop play test, 2026-09-30).
        const auto worldDt = static_cast<std::uint32_t>(std::lround(static_cast<float>(dt) * speedSet_));
        clock_ += worldDt;
        roundRunMs_ += worldDt;
    }
    ApplyStatuses(obs.statuses, fx);
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
            // With time, the rest of this turn is the first of what the
            // hold takes.
            if (const auto held = held_.find(current->id); Timed() && held != held_.end())
            {
                const std::uint32_t turn = TurnMs();
                const std::uint32_t rest = turn - std::min(turn, budget_.spend.ClockElapsedMs());
                held->second.owedMs -= std::min(held->second.owedMs, rest);
                // Taken by the hold, not banked: nothing of it rolls over.
                budget_.spend.SpendAllActions();
            }
        }
        EndTurn(fx, "stunned");
        EnforcePause(obs, fx);
        return fx;
    }
    // A cast began to charge (Judge): its caster does nothing else this
    // turn.
    if (std::exchange(chargeBegan_, false) && Active() && order_.Current() != nullptr)
    {
        EndTurn(fx, Timed() ? "its action pending" : "charging");
        EnforcePause(obs, fx);
        return fx;
    }
    if (pendingEndNow_ && Active() && order_.Current() != nullptr && lastNow_ >= *pendingEndNow_ &&
        phase_ != Phase::Executing && phase_ != Phase::Settling)
    {
        pendingEndNow_.reset();
        EndTurn(fx, "its action pending");
        EnforcePause(obs, fx);
        return fx;
    }
    for (const ObjectId id : obs.keptVoid)
    {
        KeptVoid(id);
    }
    // A turn whose kept action will not come is handed over as any turn is,
    // on a look taken with this step: its creature may have been stunned
    // since its turn began (test_fights.cpp's long run, seed 744 with
    // time: a stunned party member handed its turn). Stunned, its turn has
    // ended above.
    if (handoverWanted_)
    {
        const Combatant *current = order_.Current();
        if (!Active() || current == nullptr || current->id != *handoverWanted_)
        {
            handoverWanted_.reset();
        }
        else if (lastStatusNow_ == lastNow_)
        {
            handoverWanted_.reset();
            if (std::exchange(budget_.landing, false))
            {
                Handover(*current, fx);
            }
        }
        else
        {
            statusWanted_ = true;
        }
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
        stoodDown_ = false;
        return;
    }
    if (!settings_.enabled || stoodDown_)
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
    if (obs.scan && std::ranges::any_of(
                        *obs.scan, [](const Combatant &c) { return c.side == Side::Enemy && !c.down && c.engaged; }))
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
        // (A spell held in its conjure with them: the conversation may have
        // given its caster orders of its own, and the spell is lost, said as
        // its command ends, OnConjureCleared.)
        for (const Combatant &c : order_.All())
        {
            if (!c.down)
            {
                fx.push_back(Effect::Of(EffectKind::ClearCommands, c.id));
            }
        }
        resume_ = turnBegun_;
        phase_ = std::exchange(startInterrupted_, false) ? Phase::Starting : Phase::TurnStart;
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
    notice_.reset(); // what the caption said of the last fight's order is not this one's
    currentStunned_ = false;
    usedNow_.clear();
    holds_.clear();
    absentSince_.clear();
    outside_.clear();
    left_.clear();
    charges_.clear();
    rollover_.clear();
    grabbedMs_.clear();
    landing_.reset();
    chargeBegan_ = false;
    approach_ = false;
    roundAgedMs_ = 0;
    ownEffectSince_.reset();
    // Only who is fighting the party (Combatant::engaged): the rest wait
    // outside the order, and join when they are.
    for (const Combatant &c : roster)
    {
        if (c.side != Side::Party && !c.engaged && !c.down)
        {
            outside_.insert(c.id);
        }
    }
    std::erase_if(roster, [](const Combatant &c) { return c.side != Side::Party && !c.engaged; });
    if (std::ranges::none_of(roster, [](const Combatant &c) { return c.side == Side::Enemy && !c.down; }))
    {
        Log(LogLevel::Info,
            std::format("combat, but no enemy fighting the party within {:.0f} m{}: real-time until "
                        "one is",
                        settings_.combatRadius,
                        outside_.empty() ? std::string{}
                                         : std::format(" ({} fighting out of its sight)", outside_.size())));
        outside_.clear();
        phase_ = Phase::Idle;
        latch_ = true;
        fx.push_back(Effect::Of(EffectKind::Unpause));
        return;
    }
    for (Combatant &c : roster)
    {
        c.initiative = RollInitiative(rng_, c.dexterity, settings_, c.survival);
        if (c.hero)
        {
            hero_ = c.id;
        }
    }
    order_.Begin(std::move(roster));
    // Said here, where there is a fight: when the game said combat and no
    // enemy was fighting the party, "began" came with no fight and no
    // "over" after it (eluvian-agent's staged wolf, 2026-10-01), and a fight
    // begun again in the same combat (hostiles fighting again) was not
    // said at all.
    Emit(EventKind::FightBegan);
    Log(LogLevel::Info, std::format("the fight: {} combatants; {}", order_.All().size(), DescribeOrder()));
    if (!outside_.empty())
    {
        Log(LogLevel::Info, std::format("{} more fighting near, but not the party: they wait outside the fight until "
                                        "they see it or attack it",
                                        outside_.size()));
    }

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
    Emit(EventKind::RoundStarted, kNoObject, {}, 1);
    HoldTime(EffectRoundSeconds() > 0.0f, fx);
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
    if (Landing())
    {
        return;
    }
    landFromNow_.reset();
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
        // Pending until the round has begun: a conversation that came while
        // the world ran on between two rounds (RoundPadding) brought the
        // fight back to here with the round's start forgotten, its effects
        // not aged and nobody told (the random fights, seed 27, 2026-10-02).
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
        roundPending_ = false;
        roundRunMs_ = 0;
        SpendRound(fx);
        Signal(fx, events::kRoundStart, kNoObject, order_.Round(), 0);
        Log(LogLevel::Info, std::format("round {}: {}", order_.Round(), DescribeOrder()));
        Emit(EventKind::RoundStarted, kNoObject, {}, order_.Round());
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
    if (ChargingTurn(*current, fx))
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
    roundPending_ = false;
    roundRunMs_ = 0;
    SpendRound(fx);
    Signal(fx, events::kRoundStart, kNoObject, order_.Round(), 0);
    Log(LogLevel::Info, std::format("round {}: {}", order_.Round(), DescribeOrder()));
    Emit(EventKind::RoundStarted, kNoObject, {}, order_.Round());
    phase_ = Phase::TurnStart;
    wantScan_ = true;
}

void TurnMachine::BeginTurn(const Combatant &who, std::vector<Effect> &fx)
{
    budget_ = Budget{};
    heldByEffect_ = false;
    budget_.spend = NewSpend(who);
    if (const auto rolled = rollover_.find(who.id); Timed() && rolled != rollover_.end())
    {
        budget_.rolledMs = rolled->second;
    }
    clockSeenMs_ = 0;
    clockRuns_ = true;
    std::string pending;
    if (const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
        Timed() && charge != charges_.end() && charge->owedMs > 0)
    {
        // What its pending action still owes is the first of its bar (it
        // fits: ChargingTurn), and the walk paid for in it is walked for
        // nothing. A kept one lands now (LandCast).
        const std::uint32_t owed = std::exchange(charge->owedMs, 0);
        (void)budget_.spend.SpendAction(owed);
        budget_.pendingPaidMs = owed;
        pending = std::format(", {:.2f} s of it paid for its pending {}", static_cast<float>(owed) / 1000.0f,
                              Name(charge->type));
        if (charge->kept)
        {
            charge->turns = 0;
        }
        else
        {
            charges_.erase(charge);
        }
    }
    idleTicks_ = 0;
    lastPosition_.reset();
    endTurnRequested_ = false;
    passRequested_ = false;
    orderPending_ = false;

    HoldAtTurnStart(who, fx);
    GrabAtTurnStart(who, fx);
    // The fight's speed, set as its first turn begins and again after a
    // conversation, before the camera moves: when an AI's turn had a speed
    // of its own, twice the pan to the hero, issued before the speed went
    // back to 1, never arrived (play tests 2 and 5, 2026-09-30). The camera
    // says whether a pan got there (src/game/Camera.cpp).
    SetSpeed(TurnSpeed(), fx);

    if (Timed())
    {
        Log(LogLevel::Info,
            std::format("round {}: {}'s turn ({}, {}), a bar of {:.2f} s{}: {}", order_.Round(), Utf8(who.name),
                        Name(who.side), PlayerControls(who) ? "yours" : "AI",
                        static_cast<float>(budget_.spend.CapacityMs()) / 1000.0f, pending, BudgetLeft()));
    }
    else
    {
        Log(LogLevel::Info,
            std::format("round {}: {}'s turn ({}, {}), {:.1f} m to move", order_.Round(), Utf8(who.name),
                        Name(who.side), PlayerControls(who) ? "yours" : "AI", budget_.MovementLeft()));
    }
    Emit(EventKind::TurnStarted, who.id, PlayerControls(who) ? "yours" : "AI",
         static_cast<std::int32_t>(std::lround(budget_.MovementLeft() * 10.0f)));
    TellTurnStart(who, fx);
    // With time, what a pending action took of the bar has moved the clock:
    // what counts on it moves now, the turn begun and said.
    MoveClock(fx);
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
    if (LandCast(who, fx))
    {
        return;
    }
    Handover(who, fx);
}

int TurnMachine::ChargeTurns(const CommandRequest &request) const
{
    if (request.type != CommandType::UseAbility || request.sustained || request.ability < 0)
    {
        return 0;
    }
    for (const auto &[ability, turns] : settings_.charge)
    {
        if (ability == request.ability)
        {
            return turns;
        }
    }
    // Whole turns of its conjuring time; less than a turn's worth is cast
    // on the turn (fast and normal, 1 and 2 s of a 4 s turn).
    const float turn = CooldownTurnSeconds();
    if (!(turn > 0.0f) || request.conjureSeconds < turn)
    {
        return 0;
    }
    return static_cast<int>(std::ceil(request.conjureSeconds / turn));
}

std::uint32_t ActionCostMs(const CommandRequest &request)
{
    float seconds = request.actionSeconds;
    if (!(seconds > 0.0f) || !std::isfinite(seconds))
    {
        switch (request.type)
        {
        case CommandType::Attack:
            seconds = kStandInSwingSeconds;
            break;
        case CommandType::UseItem:
            seconds = kDrinkSeconds;
            break;
        case CommandType::UseAbility:
            if (request.sustained && request.sustainedOn)
            {
                return 0; // a mode turned off
            }
            seconds =
                request.abilityType == kAbilityItem
                    ? kDrinkSeconds
                    : kStandInAbilitySeconds +
                          ConjureSeconds(request.conjureAnim, static_cast<int>(request.conjureSeconds), request.staff);
            break;
        default:
            seconds = kStandInAbilitySeconds;
            break;
        }
    }
    constexpr float kLongest = 60.0f; // a minute: anything longer is no action's
    return static_cast<std::uint32_t>(std::lround(std::min(seconds, kLongest) * 1000.0f));
}

bool TurnMachine::Timed() const
{
    return settings_.turnClock == TurnClock::Time;
}

std::uint32_t TurnMachine::TurnMs() const
{
    return static_cast<std::uint32_t>(std::lround(settings_.secondsPerTurn * 1000.0f));
}

float TurnMachine::CooldownTurnSeconds() const
{
    return Timed() ? settings_.secondsPerTurn : settings_.cooldownSecondsPerTurn;
}

float TurnMachine::EffectRoundSeconds() const
{
    return Timed() ? settings_.secondsPerTurn : settings_.effectSecondsPerRound;
}

// Combat Tactics: a party member's own; an enemy's or an ally's by its
// creature rank, critter to elite boss (Settings::enemyTactics).
int TurnMachine::TacticsOf(const Combatant &who) const
{
    constexpr int kCritter = 1;
    constexpr int kEliteBoss = 5;
    if (who.side != Side::Party && who.rank >= kCritter && who.rank <= kEliteBoss)
    {
        return settings_.enemyTactics.at(static_cast<std::size_t>(who.rank - kCritter));
    }
    return who.tactics;
}

// A turn's budget: one action and meters as built; with time, a bar of
// seconds_per_turn and Combat Tactics' time and what the last turn rolled
// over, walked at the creature's pace (a person's times its speed) after a
// free meter.
TurnBudget TurnMachine::NewSpend(const Combatant &who) const
{
    if (!Timed())
    {
        return TurnBudget::Actions(1, MovementBudget(settings_, who.speed, who.dexterity));
    }
    const TacticsTime tactics = TimeFromTactics(TacticsOf(who));
    const auto rolled = rollover_.find(who.id);
    const std::uint32_t rollover = rolled != rollover_.end() ? rolled->second : 0;
    const float speed = std::isfinite(who.speed) && who.speed > 0.0f ? who.speed : 1.0f;
    return TurnBudget::Time(TurnTime(TurnMs(), tactics.bonusMs, rollover), settings_.paceMps * speed,
                            settings_.freeStepMeters);
}

// With time, an action pays as it is let through: its walk to its target
// first (an attack's), so that its swing cannot leave the walk short, then
// its own time. One that does not fit what is left is kept, if the game
// layer can keep it (an attack, an ability not an item's), and lands as its
// creature's next turn has paid what it owes; one it cannot runs now, and
// what it owes is paid next turn (dev/REBALANCE.md, "Actions": "begun now
// and lands partway into the next turn").
std::optional<Verdict> TurnMachine::PayAction(const Combatant &who, const CommandRequest &request)
{
    std::uint32_t cost = ActionCostMs(request);
    if (request.type == CommandType::UseAbility && !request.sustained)
    {
        for (const auto &[ability, turns] : settings_.charge)
        {
            if (ability == request.ability)
            {
                cost = static_cast<std::uint32_t>(turns) * TurnMs();
            }
        }
    }
    budget_.lastCostMs = 0;
    const float walk = request.walk.value_or(0.0f);
    if (walk > budget_.MovementLeft() + kReachSlack)
    {
        // An AI's walk longer than its time (the player's is refused): it
        // closes in, paying as it walks, and is stopped where its time runs
        // out, its swing never begun.
        Log(LogLevel::Info, std::format("{}: {} with {:.1f} m to walk and {}: it closes in", Utf8(who.name),
                                        Name(request.type), walk, BudgetLeft()));
        budget_.onArrivalMs = cost; // taken if it gets there after all
        return std::nullopt;
    }
    const std::uint32_t walkMs = budget_.spend.WalkCostMs(walk);
    const std::uint32_t left = budget_.spend.CapacityLeftMs();
    const bool keepable =
        request.type == CommandType::Attack ||
        (request.type == CommandType::UseAbility && request.abilityType != kAbilityItem && request.ability >= 0);
    if (walkMs + cost > left && keepable)
    {
        const std::uint32_t owed = walkMs + cost - left;
        // A spell that conjures is the engine's own cast from now: it walks
        // into range itself, and conjures, held, through the others' turns.
        const bool engine = EngineConjures(request);
        // A walk to its target is taken now, paid out of this turn (it
        // fits: the walk alone was judged above), and the action lands
        // from there: a creature that stood where it was while its attack
        // pended closed on nobody (play test 25's duel: a spider 13.6 m off
        // walked, was stopped a step short and lost its turn's time).
        const bool approach = walk >= kMinStep;
        if (approach)
        {
            budget_.spend.Prepay(walk);
            if (!engine)
            {
                budget_.spend.Pend();
                budget_.spend.Credit(kApproachSlack);
            }
        }
        else if (!engine)
        {
            budget_.spend.SpendAllActions();
        }
        // (The engine's own: the rest of the bar is spent as its caster
        // reaches the held conjure, StepExecuting; failed before, nothing.)
        std::erase_if(charges_, [&](const Charge &c) { return c.caster == who.id; });
        charges_.push_back(Charge{who.id, request.ability, 1, owed, request.type, true,
                                  request.type == CommandType::UseAbility && request.abilityType == kAbilitySpell,
                                  engine, request.target});
        if (engine)
        {
            // The turn is at it until its caster stands at the conjure the
            // game layer holds (StepExecuting), its walk the engine's.
            budget_.conjureWaiting = true;
            phase_ = Phase::Executing;
            executingSince_ = clock_;
            idleTicks_ = 0;
        }
        else if (approach)
        {
            // The walk comes with the next frame's effects, as a charged
            // cast's release does (LandCast): the turn is at it from now,
            // the world running, and ends as it settles, or as a turn does
            // whose command never came.
            pending_.push_back(Effect::Of(EffectKind::Approach, who.id));
            approach_ = true;
            if (phase_ != Phase::Executing)
            {
                phase_ = Phase::Executing;
                executingSince_ = clock_;
                idleTicks_ = 0;
            }
        }
        else
        {
            // The turn is over: at once, or, with a command of its
            // creature's still running (an AI queues its next as one ends,
            // and with time it may have time for it), as that command
            // settles, its bar spent (StepSettling). Ended at once, the
            // running one went on into the next creature's turn
            // (test_fights.cpp, seed 1).
            chargeBegan_ = phase_ != Phase::Executing;
            if (chargeBegan_ && PlayerControls(who))
            {
                chargeBegan_ = false;
                pendingEndNow_ = lastNow_ + kPendingSayMs;
            }
        }
        Log(LogLevel::Info,
            std::format("{} begins {} ({:.2f} s{}): {:.2f} s of it is over what is left, and it lands on its next "
                        "turn{}",
                        Utf8(who.name), Name(request.type), static_cast<float>(cost) / 1000.0f,
                        walkMs > 0 ? std::format(" and {:.2f} s of walk", static_cast<float>(walkMs) / 1000.0f) : "",
                        static_cast<float>(owed) / 1000.0f, approach ? "; it walks to its target now" : ""));
        Emit(EventKind::ActionPending, who.id, std::format("{:.1f} s more", static_cast<float>(owed) / 1000.0f),
             request.ability, Name(request.type));
        // Said over the player's own, whose order it was; an enemy's every
        // round was noise (play test 25).
        if (PlayerControls(who))
        {
            Floaty(pending_, who.id, L"Lands next turn", kGold);
        }
        MoveClock(pending_);
        return engine ? Verdict::Conjure : Verdict::Hold;
    }
    budget_.spend.Prepay(walk);
    const std::uint32_t owed = budget_.spend.SpendAction(cost);
    budget_.lastCostMs = cost - owed;
    MoveClock(pending_);
    if (owed > 0)
    {
        std::erase_if(charges_, [&](const Charge &c) { return c.caster == who.id; });
        charges_.push_back(Charge{who.id, request.ability, 1, owed, request.type, false});
        Log(LogLevel::Info, std::format("{}: {} runs now and owes {:.2f} s to its next turn", Utf8(who.name),
                                        Name(request.type), static_cast<float>(owed) / 1000.0f));
    }
    else if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug,
            std::format("{}: {} costs {:.2f} s{}", Utf8(who.name), Name(request.type),
                        static_cast<float>(cost) / 1000.0f,
                        walkMs > 0 ? std::format(" and its walk {:.2f} s", static_cast<float>(walkMs) / 1000.0f) : ""));
    }
    return std::nullopt;
}

void TurnMachine::ChargeOnArrival(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    const std::uint32_t cost = std::exchange(budget_.onArrivalMs, 0);
    if (current == nullptr || cost == 0)
    {
        return;
    }
    const std::uint32_t owed = budget_.spend.SpendAction(cost);
    budget_.lastCostMs = cost - owed;
    if (owed > 0)
    {
        Owe(current->id, owed, budget_.lastType, budget_.lastAbility);
    }
    Log(LogLevel::Info,
        std::format("{} is at its {}: its {:.2f} s taken now{}; {}", Utf8(current->name), Name(budget_.lastType),
                    static_cast<float>(cost) / 1000.0f,
                    owed > 0 ? std::format(", {:.2f} s of it owed to its next turn", static_cast<float>(owed) / 1000.0f)
                             : std::string{},
                    BudgetLeft()));
    MoveClock(fx);
}

// Whether the mod's own copy of the game's interruption rule judges a blow
// on this creature: a spell of its pending that the gate keeps. Not with the
// engine's own conjuring (Settings::engineConjure), where the installed
// rules_core judges it, and a spell kept for want of time that does not
// conjure is not interruptible in the game either.
bool TurnMachine::Conjuring(ObjectId who) const
{
    if (Timed() && settings_.engineConjure)
    {
        return false;
    }
    const auto charge = std::ranges::find(charges_, who, &Charge::caster);
    return Active() && ((charge != charges_.end() && charge->kept && charge->spell) ||
                        (landing_ && landing_->caster == who && landing_->spell));
}

// A spell that conjures, with time and the setting: the engine's own cast,
// held in its conjure. Its conjuring is read from its conjure animation and
// its speed's count (DescribeAbility); a spell with neither does not conjure
// (sub-action 7 of no length, DAOLib's probe, 2026-10-02).
bool TurnMachine::EngineConjures(const CommandRequest &request) const
{
    return Timed() && settings_.engineConjure && request.type == CommandType::UseAbility &&
           request.abilityType == kAbilitySpell && !request.sustained && request.conjureAnim > 0 &&
           request.conjureSeconds > 0.0f;
}

bool TurnMachine::OnConjureCleared(ObjectId caster)
{
    const auto charge = std::ranges::find(charges_, caster, &Charge::caster);
    if (charge == charges_.end() || !charge->engine)
    {
        return false;
    }
    const Combatant *c = order_.Find(caster);
    Log(LogLevel::Info, std::format("{}'s conjuring was cleared before its time was paid",
                                    c != nullptr ? Utf8(c->name) : std::string{"?"}));
    Interrupt(caster, kConjureCleared, pending_);
    return true;
}

bool TurnMachine::OnDamaged(ObjectId target, float damage, bool physical, float threshold)
{
    if (!physical || !(damage > threshold) || !Conjuring(target))
    {
        return false;
    }
    const Combatant *c = order_.Find(target);
    Log(LogLevel::Info, std::format("{} is struck for {:.0f} (over {:.0f}) with a spell pending",
                                    c != nullptr ? Utf8(c->name) : std::string{"?"}, damage, threshold));
    Interrupt(target, "struck", pending_);
    return true;
}

// With time: an action that the engine was read to take seconds, and whose
// command was over in a fraction of them, did not play, whatever the engine
// said of it (nothing: no failure came). Lyna's Chain Lightning, 4.99 s on a
// bar of 5.0, was over in 0.4 s with no bolt, no stamina spent, and her
// whole turn charged for it (play test 29: its target had not yet joined
// the fight). Its time is given back, once a turn, as a failed command's
// is, and its cooldown not held.
void TurnMachine::OverAtOnce(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr || !Timed() || !settings_.giveBackUnplayed || budget_.lastKind != CommandKind::Action ||
        !budget_.lastMeasured || budget_.lastCostMs < kLeastToldMs ||
        clock_ - budget_.lastClock >= budget_.lastCostMs / kOverAtOnce)
    {
        return;
    }
    // (OnCommandFailed says "Nothing happened" over the player's own.)
    (void)fx;
    (void)OnCommandFailed(current->id, budget_.lastType, budget_.lastAbility, "over at once: it did nothing");
}

void TurnMachine::Owe(ObjectId who, std::uint32_t ms, CommandType type, std::int32_t ability)
{
    if (const auto charge = std::ranges::find(charges_, who, &Charge::caster); charge != charges_.end())
    {
        charge->owedMs += ms;
        return;
    }
    charges_.push_back(Charge{who, ability, 1, ms, type, false});
}

bool TurnMachine::ActionToGiveBack() const
{
    return budget_.spend.Timed() ? budget_.lastCostMs > 0 : budget_.spend.ActionsLeft() == 0;
}

void TurnMachine::GiveActionBack()
{
    budget_.spend.GiveActionBack(budget_.lastCostMs);
    budget_.lastCostMs = 0;
}

std::string TurnMachine::BudgetLeft() const
{
    if (budget_.spend.Timed())
    {
        return std::format("{:.2f} s and {:.1f} m left", static_cast<float>(budget_.spend.CapacityLeftMs()) / 1000.0f,
                           budget_.MovementLeft());
    }
    return std::format("{} action and {:.1f} m left", budget_.spend.ActionsLeft(), budget_.MovementLeft());
}

// A turn of a charging caster's: spent on the charge, unless this is the
// turn it is cast on (LandCast, from BeginTurn).
bool TurnMachine::ChargingTurn(const Combatant &who, std::vector<Effect> &fx)
{
    const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
    if (charge == charges_.end())
    {
        return false;
    }
    if (Timed())
    {
        // Paid on this turn and landed on it (BeginTurn), or this whole turn
        // is spent on it.
        TurnBudget bar = NewSpend(who);
        if (charge->owedMs <= bar.CapacityLeftMs())
        {
            return false;
        }
        charge->owedMs -= bar.CapacityLeftMs();
        budget_ = Budget{};
        budget_.spend = bar;
        budget_.spend.SpendAllActions();
        Log(LogLevel::Info,
            std::format("round {}: {}'s turn is spent on its pending {} ({:.2f} s more)", order_.Round(),
                        Utf8(who.name), Name(charge->type), static_cast<float>(charge->owedMs) / 1000.0f));
        Emit(EventKind::ActionPending, who.id,
             std::format("{:.1f} s more", static_cast<float>(charge->owedMs) / 1000.0f), charge->ability,
             Name(charge->type));
    }
    else
    {
        if (--charge->turns <= 0)
        {
            return false;
        }
        budget_ = Budget{};
        budget_.spend.SpendAllActions();
        Log(LogLevel::Info, std::format("round {}: {}'s turn is spent charging ability {} ({} more)", order_.Round(),
                                        Utf8(who.name), charge->ability, charge->turns));
        Emit(EventKind::CastCharging, who.id, std::format("{} more", charge->turns), charge->ability);
    }
    TellTurnStart(who, fx);
    clockSeenMs_ = 0;
    clockRuns_ = true;
    Floaty(fx, who.id, L"Casting", kGold);
    EndTurn(fx, "charging");
    return true;
}

// The turn a charged cast is cast on: the game layer gives its creature
// the cast it kept (ReleaseCast), which comes back through the gate and is
// let through for nothing (Judge); the creature's turn follows it.
bool TurnMachine::LandCast(const Combatant &who, std::vector<Effect> &fx)
{
    const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
    if (charge == charges_.end() || charge->turns > 0)
    {
        return false;
    }
    landing_ = *charge;
    charges_.erase(charge);
    budget_.landing = true;
    if (Timed())
    {
        Log(LogLevel::Info,
            std::format("{}'s pending {} lands: its time is paid", Utf8(who.name), Name(landing_->type)));
    }
    else
    {
        Log(LogLevel::Info, std::format("{}'s charged cast of ability {} is due", Utf8(who.name), landing_->ability));
    }
    Effect release = Effect::Of(EffectKind::ReleaseCast, who.id);
    release.a = landing_->ability;
    // A plain attack whose target has fallen is turned on the nearest enemy
    // its creature can strike from where it stands (core/Rules.h,
    // NearestInReach; issue 22). Who its enemies are is the fight's to say:
    // those of the other side still standing, as this turn's own look found
    // them. Which of them is in its reach is the game layer's to measure. A
    // talent or a spell names none and is dropped, as before: its target was
    // chosen.
    if (landing_->type == CommandType::Attack)
    {
        for (const Combatant &c : order_.All())
        {
            if (!c.down && Opposed(who.side, c.side))
            {
                release.others.push_back(c.id);
            }
        }
    }
    fx.push_back(std::move(release));
    if (landing_->engine)
    {
        // The engine's own cast, its conjure let complete: nothing comes back
        // through the gate, and the turn waits for the cast to end.
        landing_.reset();
    }
    phase_ = Phase::Executing;
    executingSince_ = clock_;
    idleTicks_ = 0;
    return true;
}

void TurnMachine::Interrupt(ObjectId who, std::string_view why, std::vector<Effect> &fx)
{
    std::int32_t ability = -1;
    CommandType type = CommandType::UseAbility;
    bool spell = false;
    if (const auto charge = std::ranges::find(charges_, who, &Charge::caster); charge != charges_.end())
    {
        spell = charge->spell;
        // What an action that ran (not kept) still owed: forgiven, as what
        // interrupts takes the creature's turn whole.
        if (!charge->kept)
        {
            charges_.erase(charge);
            return;
        }
        ability = charge->ability;
        type = charge->type;
        charges_.erase(charge);
    }
    else if (landing_ && landing_->caster == who)
    {
        ability = landing_->ability;
        type = landing_->type;
        spell = landing_->spell;
        landing_.reset();
    }
    else
    {
        return;
    }
    const Combatant *c = order_.Find(who);
    const std::string name = c != nullptr ? Utf8(c->name) : std::string{"?"};
    if (Timed())
    {
        Log(LogLevel::Info, std::format("{}'s pending {} is dropped: {}", name, Name(type), why));
        Emit(EventKind::ActionDropped, who, std::string{why}, ability, Name(type));
    }
    else
    {
        Log(LogLevel::Info, std::format("{}'s cast of ability {} is interrupted: {}", name, ability, why));
        Emit(EventKind::CastInterrupted, who, std::string{why}, ability);
    }
    fx.push_back(Effect::Of(EffectKind::DropCast, who));
    // Not as the fight ends: nothing interrupted it (play test 25); nor,
    // with time, over one that fell or is gone, where every attacker has
    // an action pending most of the time.
    // In the mod's own words: the game floats a white "Interrupted!" of its
    // own over a creature whose swing a blow breaks, three times in one
    // fight over ordinary bites, and the two could not be told apart (play
    // test 28).
    if (why == "held" || why == "struck" || why == kConjureCleared || (!Timed() && why != "the fight over"))
    {
        // In the gold of the mod's other words: small and white it was found
        // over the ogre's pale body only because the log said when to look
        // (play test 29), and in red, under the game's own red number for
        // the blow, it read worse (play test 30).
        Floaty(fx, who, spell ? L"Spell lost" : L"Action lost", kGold);
    }
}

// The game layer did not give a kept cast or action when asked to: its
// target was gone, and for a plain attack none of those it might have been
// turned on was in its creature's reach (LandCast). What its creature's turn
// paid for it as it began is given back, and the turn is its creature's, whole.
void TurnMachine::KeptVoid(ObjectId who)
{
    if (!landing_ || landing_->caster != who)
    {
        return;
    }
    const Combatant *current = order_.Current();
    const std::uint32_t paid = current != nullptr && current->id == who ? budget_.pendingPaidMs : 0;
    if (paid > 0)
    {
        budget_.spend.GiveActionBack(paid);
        budget_.pendingPaidMs = 0;
    }
    const Combatant *c = order_.Find(who);
    Log(LogLevel::Info,
        std::format("{}'s pending {} is dropped: its target is gone{}{}",
                    c != nullptr ? Utf8(c->name) : std::string{"?"}, Name(landing_->type),
                    landing_->type == CommandType::Attack ? ", and no enemy is in its reach" : "",
                    paid > 0 ? std::format("; the {:.2f} s it owed given back", static_cast<float>(paid) / 1000.0f)
                             : std::string{}));
    Emit(Timed() ? EventKind::ActionDropped : EventKind::CastInterrupted, who, "its target is gone", landing_->ability,
         Name(landing_->type));
    landing_.reset();
    // Nothing is coming to wait for: the turn is its creature's now, handed
    // over as any turn is (Step): an AI is told to choose, and chooses anew
    // with its bar whole; a party member the player plays waits for an order
    // (the user's question of issue 22, 2026-10-03: "Does the AI need some
    // kind of a kick to continue acting and not just stand there?"; this is
    // the kick). Left at "executing", a creature held by an effect no
    // condition names waited out its whole turn there, the grace never
    // counted (test_fights.cpp's long run, seed 129 with time).
    if (current != nullptr && current->id == who && budget_.landing)
    {
        handoverWanted_ = who;
    }
}

// After a conversation or cutscene: the same turn, with what it had left
// (a fresh one would give its action back), and no second turn-start event.
void TurnMachine::ResumeTurn(const Combatant &who, std::vector<Effect> &fx)
{
    idleTicks_ = 0;
    lastPosition_.reset();
    orderPending_ = false;
    Log(LogLevel::Info, std::format("{}'s turn goes on: {}", Utf8(who.name), BudgetLeft()));
    Emit(EventKind::TurnResumed, who.id);
    Handover(who, fx);
}

// The turn to whoever plays it: the player waits for an order; an AI is
// told to choose.
// A turn a condition costs: it is taken at once, the world still, as the
// condition's own time is the turns (dev/DESIGN.md, "Conditions in turns").
bool TurnMachine::LoseTurn(const Combatant &who, std::vector<Effect> &fx)
{
    // Held by another's action a turn's worth or more (a spider's Overwhelm,
    // 9.3 s): this turn is lost, the rest owed of the next.
    if (const auto grabbed = grabbedMs_.find(who.id);
        Timed() && grabbed != grabbedMs_.end() && grabbed->second >= TurnMs())
    {
        grabbed->second -= TurnMs();
        const std::uint32_t more = grabbed->second;
        if (more == 0)
        {
            grabbedMs_.erase(grabbed);
        }
        budget_ = Budget{};
        budget_.spend.SpendAllActions();
        budget_.agedOwnMs = TurnMs(); // its effects ran with the action that held it
        Log(LogLevel::Info, std::format("round {}: {}'s turn is lost: held by another's action ({:.1f} s more)",
                                        order_.Round(), Utf8(who.name), static_cast<float>(more) / 1000.0f));
        Emit(EventKind::TurnLost, who.id, {}, static_cast<std::int32_t>(more));
        TellTurnStart(who, fx);
        clockSeenMs_ = 0;
        clockRuns_ = true;
        Floaty(fx, who.id, who.name + L" loses the turn", who.side == Side::Party ? kGold : kGrey);
        EndTurn(fx, "held by another's action");
        return true;
    }
    const auto found = held_.find(who.id);
    if (found == held_.end())
    {
        return false;
    }
    Held &held = found->second;
    // Owed turns; or, the condition on the game's clock (condition_turns 0),
    // every turn it still holds the creature: it cannot act, and with
    // effects' time held its command cannot end within the turn.
    // With time, the hold's own seconds: a turn's worth or more is this
    // turn; less is taken of the turn's bar as it begins (BeginTurn).
    const std::uint32_t turn = TurnMs();
    const bool timed = Timed() && held.kind == Condition::Disabled && held.owedMs > 0;
    const bool owes = timed ? held.owedMs >= turn : !Timed() && held.kind == Condition::Disabled && held.turns > 0;
    // On the game's clock (condition_turns 0, and with time its seconds not
    // read either): nothing is owed, and each turn it still holds is lost.
    const bool holds = (held.onClock || (settings_.conditionTurns == 0 && held.kind == Condition::None)) &&
                       held.seen == Condition::Disabled;
    if (!owes && !holds)
    {
        return false;
    }
    if (owes && timed)
    {
        held.owedMs -= turn;
    }
    else if (owes)
    {
        --held.turns;
    }
    budget_ = Budget{};
    budget_.spend.SpendAllActions();
    if (Timed())
    {
        Log(LogLevel::Info, std::format("round {}: {}'s turn is lost to its condition ({:.1f} s more)", order_.Round(),
                                        Utf8(who.name), static_cast<float>(held.owedMs) / 1000.0f));
    }
    else
    {
        Log(LogLevel::Info, std::format("round {}: {}'s turn is lost to its condition ({} more)", order_.Round(),
                                        Utf8(who.name), held.turns));
    }
    Emit(EventKind::TurnLost, who.id, {}, Timed() ? static_cast<std::int32_t>(held.owedMs) : held.turns);
    TellTurnStart(who, fx);
    clockSeenMs_ = 0;
    clockRuns_ = true;
    Floaty(fx, who.id, who.name + L" loses the turn", who.side == Side::Party ? kGold : kGrey);
    EndTurn(fx, "held by its condition");
    return true;
}

// At the start of a turn that is taken: what another's action held it for,
// less than a turn (LoseTurn takes the whole turns), comes off its bar as a
// hold's share does. Its clock's cooldowns move with it; its effects already
// moved, with the steps of the action that held it (ObserveExecuting).
void TurnMachine::GrabAtTurnStart(const Combatant &who, std::vector<Effect> &fx)
{
    const auto found = grabbedMs_.find(who.id);
    if (found == grabbedMs_.end() || !Timed())
    {
        return;
    }
    const std::uint32_t owed = std::min(found->second, TurnMs());
    grabbedMs_.erase(found);
    if (owed == 0)
    {
        return;
    }
    MoveClock(fx); // a hold's share first, its effects with it
    const std::uint32_t before = budget_.spend.CapacityLeftMs();
    const std::uint32_t clock = budget_.spend.HoldClock(owed);
    AgeCooldowns(who.id, clock, fx);
    clockSeenMs_ = budget_.spend.ClockElapsedMs();
    budget_.agedOwnMs += clock;
    Log(LogLevel::Info,
        std::format("{} was held {:.2f} s by another's action: {:.2f} s of its turn's clock, {:.2f} s "
                    "of its bar",
                    Utf8(who.name), static_cast<float>(owed) / 1000.0f, static_cast<float>(clock) / 1000.0f,
                    static_cast<float>(before - budget_.spend.CapacityLeftMs()) / 1000.0f));
}

// At the start of a turn that is taken: a condition whose turns are spent
// but that the engine still holds is ended now, before the creature is told
// its turn has come.
void TurnMachine::HoldAtTurnStart(const Combatant &who, std::vector<Effect> &fx)
{
    const auto found = held_.find(who.id);
    if (found == held_.end())
    {
        return;
    }
    Held &held = found->second;
    if (Timed() && held.kind == Condition::Disabled && held.owedMs > 0)
    {
        // Less than a turn's worth (LoseTurn took the whole turns): that
        // share of this turn's bar, whatever the bar holds.
        const std::uint32_t before = budget_.spend.CapacityLeftMs();
        const std::uint32_t clock = budget_.spend.HoldClock(std::exchange(held.owedMs, 0));
        Log(LogLevel::Info,
            std::format("{}'s condition takes {:.2f} s of its turn's clock, {:.2f} s of its bar: {}", Utf8(who.name),
                        static_cast<float>(clock) / 1000.0f,
                        static_cast<float>(before - budget_.spend.CapacityLeftMs()) / 1000.0f, BudgetLeft()));
    }
    if (held.turns <= 0 && held.owedMs == 0 && held.seen != Condition::None)
    {
        Log(LogLevel::Info, std::format("{}'s condition has had its turns: ended", Utf8(who.name)));
        Emit(EventKind::ConditionEnded, who.id);
        fx.push_back(Effect::Of(EffectKind::EndConditions, who.id));
        endedFor_ = who.id;
        held.kind = Condition::None;
        // Gone once the effect is applied: a new stun after it is new.
        held.seen = Condition::None;
    }
}

namespace
{
Effect CooldownOf(ObjectId creature, std::int32_t ability, std::int32_t seconds, std::int32_t of = 0)
{
    Effect e = Effect::Of(EffectKind::Cooldown, creature);
    e.a = ability;
    e.b = seconds;
    e.c = of;
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
        fx.push_back(CooldownOf(used.creature, used.ability, kCooldownHoldSeconds, kCooldownHoldSeconds));
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug, std::format("ability {} of {:#010x} held for {} of its user's turns", used.ability,
                                             used.creature, used.turns));
        }
        Emit(EventKind::CooldownHeld, used.creature, std::format("{} turns", used.turns), used.ability);
    }
    usedNow_.clear();
}

// A turn of its user passes (taken or lost): what has waited its turns is
// ready now, before the creature is told its turn has come.
// A turn's beginning, for what counts turns: the scripts are told, and the
// cooldowns that count their user's turns count this one. A turn that was
// put off (Delay) begins a second time in its round, or is then lost to a
// hold: told and counted once, the first time.
void TurnMachine::TellTurnStart(const Combatant &who, std::vector<Effect> &fx)
{
    if (order_.Delayed(who.id))
    {
        return;
    }
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
    TickCooldowns(who, fx);
}

void TurnMachine::TickCooldowns(const Combatant &who, std::vector<Effect> &fx)
{
    if (Timed())
    {
        return; // on its user's clock: MoveClock
    }
    for (Hold &h : holds_)
    {
        if (h.creature != who.id)
        {
            continue;
        }
        if (--h.turns <= 0)
        {
            fx.push_back(CooldownOf(h.creature, h.ability, 0));
            Log(LogLevel::Info, std::format("{}'s ability {} is ready again", Utf8(who.name), h.ability));
            Emit(EventKind::CooldownReleased, who.id, {}, h.ability);
        }
        else if (h.of > 0)
        {
            // The quickbar's sweep shows the turns: with 2 of 3 left to
            // wait, a third recovered (the user, 2026-09-30). Held as it
            // was, the slot showed a full sweep that never moved.
            fx.push_back(
                CooldownOf(h.creature, h.ability, kCooldownHoldSeconds * h.turns / h.of, kCooldownHoldSeconds));
        }
    }
    std::erase_if(holds_, [](const Hold &h) { return h.turns <= 0; });
}

// With time: the current creature's clock moved on by its share of the bar
// spent since the last look, or (a turn over) the rest of the way to a
// turn's seconds.
void TurnMachine::MoveClock(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (!Timed() || current == nullptr)
    {
        return;
    }
    const std::uint32_t now = budget_.spend.ClockElapsedMs();
    if (now > clockSeenMs_)
    {
        AgeOwnClock(current->id, now - clockSeenMs_, fx);
        clockSeenMs_ = now;
    }
}

void TurnMachine::AgeCooldowns(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx)
{
    constexpr std::uint32_t kSweepSteps = 16; // the quickbar's sweep is redrawn this many times over a cooldown
    for (Hold &h : holds_)
    {
        if (h.creature != who || h.ofMs == 0)
        {
            continue;
        }
        const std::uint32_t before = h.leftMs;
        h.leftMs -= std::min(h.leftMs, clockMs);
        if (h.leftMs == 0)
        {
            fx.push_back(CooldownOf(h.creature, h.ability, 0));
            const Combatant *c = order_.Find(who);
            Log(LogLevel::Info,
                std::format("{}'s ability {} is ready again", c != nullptr ? Utf8(c->name) : "?", h.ability));
            Emit(EventKind::CooldownReleased, who, {}, h.ability);
        }
        else if (static_cast<std::uint64_t>(before) * kSweepSteps / h.ofMs !=
                 static_cast<std::uint64_t>(h.leftMs) * kSweepSteps / h.ofMs)
        {
            fx.push_back(CooldownOf(
                h.creature, h.ability,
                static_cast<std::int32_t>(static_cast<std::uint64_t>(kCooldownHoldSeconds) * h.leftMs / h.ofMs),
                kCooldownHoldSeconds));
        }
    }
    std::erase_if(holds_, [&](const Hold &h) { return h.creature == who && h.ofMs > 0 && h.leftMs == 0; });
}

// The fight is over: what still waits goes back to seconds, its turns' worth.
void TurnMachine::ReleaseCooldowns(std::vector<Effect> &fx)
{
    for (const Hold &h : holds_)
    {
        if (h.ofMs > 0)
        {
            fx.push_back(CooldownOf(h.creature, h.ability, static_cast<std::int32_t>((h.leftMs + 999) / 1000),
                                    static_cast<std::int32_t>((h.ofMs + 999) / 1000)));
            continue;
        }
        fx.push_back(
            CooldownOf(h.creature, h.ability,
                       static_cast<std::int32_t>(std::ceil(static_cast<float>(h.turns) * CooldownTurnSeconds())),
                       static_cast<std::int32_t>(std::ceil(static_cast<float>(h.of) * CooldownTurnSeconds()))));
    }
    holds_.clear();
}

// Effects' time in rounds: the clock every temporary effect ages on (DAOLib's
// dao/Effects.h) held through a fight, so none wears off while someone
// thinks or the world idles, and moved on a round's worth at each new round.
// The game's speed (DAOLib's dao::clock::SetSpeed): a fight's turns at the
// fight's speed ([turns] speed), conversations and everything else at 1.
// Written only when it changes, so at the default of 1 never: another
// plugin setting it (DAOLib's examples/speed) is left alone.
void TurnMachine::SetSpeed(float speed, std::vector<Effect> &fx)
{
    if (speed == speedSet_)
    {
        return;
    }
    speedSet_ = speed;
    Emit(EventKind::SpeedSet, kNoObject, {}, static_cast<std::int32_t>(std::lround(speed * 1000.0f)));
    Effect e = Effect::Of(EffectKind::GameSpeed);
    e.b = static_cast<std::int32_t>(std::lround(speed * 1000.0f));
    fx.push_back(e);
}

void TurnMachine::HoldTime(bool held, std::vector<Effect> &fx)
{
    if (held == timeHeld_)
    {
        return;
    }
    timeHeld_ = held;
    Effect e = Effect::Of(EffectKind::HoldTime);
    e.a = held ? 1 : 0;
    fx.push_back(e);
    Log(LogLevel::Info, held ? std::format("effects' time held: {:.0f} s of it a round{}", EffectRoundSeconds(),
                                           OwnClocks() ? ", each fighter's on its own clock" : "")
                             : std::string{"effects' time runs again"});
    if (held)
    {
        PutOnOwnClocks(fx);
    }
}

// With a turn a bar of time, a creature's effects, its regeneration and the
// scripts' timers on it run on its own clock (Settings::ownClocks).
// The creature whose turn it is gets up from a hold the machine ended as its
// turn began (HoldAtTurnStart: its share of the bar already paid): the
// engine's command for it is not an effect that holds it. Taken for one, the
// turn was offered and then lost two seconds later as she got up from a
// knockdown (the tester's re-check of play test 33).
bool TurnMachine::GettingUp() const
{
    const Combatant *current = order_.Current();
    return current != nullptr && endedFor_ == current->id;
}

bool TurnMachine::OwnClocks() const
{
    return Timed() && settings_.ownClocks;
}

// Everyone in the fight, once the time is held (the game layer's own clocks
// are given back as the hold is let go, so again after each hold). What is
// not in the fight, an area on the ground or a bystander, stays on the shared
// clock, which the rounds move.
void TurnMachine::PutOnOwnClocks(std::vector<Effect> &fx)
{
    if (!OwnClocks())
    {
        return;
    }
    for (const Combatant &c : order_.All())
    {
        Effect own = Effect::Of(EffectKind::OwnClock, c.id);
        own.a = 1;
        fx.push_back(own);
    }
}

void TurnMachine::AgeOwnClock(ObjectId who, std::uint32_t clockMs, std::vector<Effect> &fx)
{
    AgeCooldowns(who, clockMs, fx);
    // Not while its action's own effect runs: that effect's time is moved
    // by its own steps (ObserveExecuting), which are this clock's too.
    if (clockMs == 0 || !OwnClocks() || !timeHeld_ || ownEffectSince_)
    {
        return;
    }
    Effect age = Effect::Of(EffectKind::AgeTime, who);
    age.b = static_cast<std::int32_t>(clockMs);
    fx.push_back(age);
    budget_.agedOwnMs += clockMs;
}

void TurnMachine::SpendRound(std::vector<Effect> &fx)
{
    if (!(EffectRoundSeconds() > 0.0f))
    {
        HoldTime(false, fx); // turned off in the settings mid-fight
        return;
    }
    if (!timeHeld_)
    {
        // Turned on mid-fight: held from here. The round just played ran on
        // the game's clock, so it is not aged again. Before, a fight begun
        // at 0 never took the setting up (the review's TB-2, 2026-09-30).
        HoldTime(true, fx);
        return;
    }
    // The round's worth, less what its actions' own effects took of it.
    const auto worth = static_cast<std::uint32_t>(EffectRoundSeconds() * 1000.0f);
    const std::uint32_t left = worth - std::min(roundAgedMs_, worth);
    roundAgedMs_ = 0;
    if (left > 0)
    {
        Effect e = Effect::Of(EffectKind::AgeTime);
        e.b = static_cast<std::int32_t>(left);
        fx.push_back(e);
    }
    agedAtNow_ = lastNow_;
}

// A script playing a party member's turn through a test server (Eluvian's
// order and wait_for) had no event to wait on between orders: the turn's
// start is said (TurnStarted) and its end, and nothing when an action had
// played out and the turn went on, so the playthrough's driver read the
// state five times a second until its phase said "awaiting order" (issue 7,
// 2026-10-03). Said at each step into the phase, not while it lasts: an
// order refused while the turn already waits has its own event (Refused).
void TurnMachine::AwaitOrder(std::string_view why)
{
    phase_ = Phase::AwaitingOrder;
    const Combatant *current = order_.Current();
    if (current == nullptr)
    {
        return;
    }
    const std::int32_t left =
        Timed() ? static_cast<std::int32_t>(budget_.spend.CapacityLeftMs()) : budget_.spend.ActionsLeft();
    Emit(EventKind::AwaitingOrder, current->id, std::string{why}, left, {},
         std::format("{:.1f} m", budget_.MovementLeft()));
}

void TurnMachine::Handover(const Combatant &who, std::vector<Effect> &fx)
{
    // Set already as the turn began; again for a turn handed over mid-way.
    SetSpeed(TurnSpeed(), fx);
    if (PlayerControls(who))
    {
        fx.push_back(Effect::Of(EffectKind::Select, who.id));
        AwaitOrder("the turn is its player's");
        return;
    }
    // A companion whose tactics play it must not be the one the player
    // controls: a controlled creature's AI only carries on attacking
    // (AI_DetermineCombatRound_Partial, ai_main_h_2.nss).
    if (who.side == Side::Party && hero_ != kNoObject && hero_ != who.id)
    {
        fx.push_back(Effect::Of(EffectKind::Select, hero_));
    }
    if (seenActor_ && seenActor_->id == who.id && seenActor_->busy &&
        seenActor_->current == CommandType::ExecuteEffect && !GettingUp())
    {
        heldByEffect_ = true;
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
        return;
    }
    fx.push_back(Effect::Of(EffectKind::PokeAi, who.id));
    deadline_ = clock_ + static_cast<std::uint32_t>(settings_.aiThinkMs);
    phase_ = Phase::AiThinking;
}

void TurnMachine::StepAwaiting(const Observation &obs, std::vector<Effect> &fx)
{
    // The last enemies fallen outside anyone's attack (a script, a trap):
    // the fight is over now, not when the game ends combat by itself (2.8 s
    // later in the fourth play test).
    // A fresh scan first: one joining may not be in the order yet (the long
    // run of random fights, seed 234).
    if (!order_.AnyStanding(Side::Enemy))
    {
        if (!obs.scan)
        {
            wantScan_ = true;
            return;
        }
        Merge(*obs.scan, fx);
        if (!order_.AnyStanding(Side::Enemy))
        {
            End(fx, "no enemy left standing");
            return;
        }
    }
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
    if (std::exchange(delayRequested_, false) && CanDelay())
    {
        DelayTurn(fx);
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
        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect && !GettingUp();
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
        AwaitOrder("the order queued no command");
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
        // Its choice would have come through the gate, which moves on to
        // executing: busy here is something else's.
        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect && !GettingUp();
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
    // A spell given to the engine to conjure: the turn is over once its
    // caster stands at the held conjure, the rest of its time owed to its
    // next turn. Over before it got there (cast at once, a conjure of no
    // length): what it owed is still paid as its next turn begins, and the
    // release then has nothing to let go.
    if (budget_.conjureWaiting && (actor.conjureHeld || !actor.busy))
    {
        budget_.conjureWaiting = false;
        budget_.spend.SpendAllActions();
        MoveClock(fx);
        EndTurn(fx, actor.conjureHeld ? "its spell conjuring" : "its spell cast at once");
        return;
    }
    if (budget_.onArrivalMs > 0 && actor.busy && !actor.moving && Classify(actor.current) == CommandKind::Action)
    {
        ChargeOnArrival(fx);
    }
    // Its own action's effect keeps it there (roundAgedMs_): that effect's
    // time runs with the action. Not one it was held by as its turn began,
    // which costs the turn, nor a stun, which ends it.
    if (actor.busy && actor.current == CommandType::ExecuteEffect && !heldByEffect_ && timeHeld_ &&
        budget_.commands > 0)
    {
        if (!ownEffectSince_)
        {
            ownEffectSince_ = clock_;
            if (Logs(LogLevel::Debug))
            {
                Log(LogLevel::Debug, "its action's own effect holds it: effects' time runs with it");
            }
        }
        if (const std::uint32_t ran = clock_ - *ownEffectSince_; ran >= kOwnEffectStepMs)
        {
            // With time, the action costs the time it runs: its own cost
            // covers the first of it, the bar the next, and what is over
            // the bar is owed to its creature's next turns. A spider's
            // Overwhelm ran 9.3 s for the 2 s it was charged (play test 27:
            // "the one moment the fight stopped feeling like turns").
            if (const Combatant *current = order_.Current(); Timed() && current != nullptr)
            {
                if (!budget_.ownEffectCover)
                {
                    budget_.ownEffectCover = budget_.lastCostMs;
                }
                const std::uint32_t free = std::min(*budget_.ownEffectCover, ran);
                *budget_.ownEffectCover -= free;
                if (const std::uint32_t over = ran - free; over > 0)
                {
                    if (const std::uint32_t owed = budget_.spend.SpendAction(over); owed > 0)
                    {
                        Owe(current->id, owed, budget_.lastType, budget_.lastAbility);
                    }
                    MoveClock(fx);
                }
            }
            ownEffectSince_ = clock_;
            // With own clocks the effect is on its creature, whose clock
            // this moves; the rest of the fight's stands. Without, everyone's
            // moves, and the round's worth is that much the less.
            const Combatant *acting = order_.Current();
            const bool own = OwnClocks() && acting != nullptr;
            Effect age = Effect::Of(EffectKind::AgeTime, own ? acting->id : kNoObject);
            age.b = static_cast<std::int32_t>(ran);
            fx.push_back(age);
            // Its target held by it (a grab, Overwhelm: an effect on each)
            // owes that time of its own turns, as it would in real time,
            // where it could do nothing while held (the user, 2026-10-02:
            // "yeah your proposal on grab makes sense").
            const ObjectId victim = acting != nullptr ? budget_.lastTarget : kNoObject;
            const bool held = victim != kNoObject && victim != acting->id && order_.Find(victim) != nullptr;
            if (held)
            {
                grabbedMs_[victim] += ran;
                budget_.grabbedNowMs += ran;
            }
            if (own)
            {
                // Not the shared clock, which is areas' and moves 4 s as each
                // round begins. It moved here too for an evening, since a
                // spider's Overwhelm, both creatures' clocks moved by its
                // steps, never ended (play test 33): no effect had aged by
                // its owner's own clock, DAOLib reading owners wrongly (its
                // 10ac778). With it an ogre's grab moved the shared clock
                // 14 s in a round, and the Elven Ruins' gas, 30 s, ended in
                // its fifth round (no hands, 2026-10-02).
                budget_.agedOwnMs += ran;
                // And its target's clock, by the same step: its command ends
                // only when the effect on its victim does, on the victim's
                // clock. Moved by the acting creature's clock alone, an ogre's
                // grab held its victim and everyone waiting 36 s, its
                // patience (a run with no hands, 2026-10-02).
                if (held)
                {
                    Effect aged = Effect::Of(EffectKind::AgeTime, victim);
                    aged.b = static_cast<std::int32_t>(ran);
                    fx.push_back(aged);
                }
            }
            else
            {
                roundAgedMs_ += ran;
            }
            agedAtNow_ = lastNow_;
        }
    }
    else
    {
        ownEffectSince_.reset();
    }
    if (heldByEffect_ && (!actor.busy || actor.current != CommandType::ExecuteEffect))
    {
        heldByEffect_ = false; // let go within the grace: the turn is its own again
        const Combatant *current = order_.Current();
        if (!actor.busy && current != nullptr && !PlayerControls(*current))
        {
            // Its AI was not told, or told while it could not choose: handed
            // over whole, the hero selected first for a companion. Poked while
            // still the one selected (party control turned to tactics while
            // it was held), the dog's AI chose nothing (the fourth play test).
            Handover(*current, fx);
            return;
        }
    }
    // Standing at a walk it does not take (kStoodMs): stopped. The player's
    // turn goes on, and an action that waited behind the walk and never
    // began is given back; an AI's turn ends as its command does.
    // An attack too, while its creature stands well short of where its walk
    // was to end: it is not swinging, it cannot get there (others stand in
    // the way), and the engine gives up only after ten seconds of the
    // world's time. A spider stood five seconds of a fight at twice the
    // speed so (play test 29); stopped, its action is given back and its AI
    // chooses again, once.
    const CommandKind at = Classify(actor.current);
    const bool short_ = actor.current == CommandType::Attack && budget_.lastType == CommandType::Attack &&
                        budget_.lastWalk - (budget_.spend.Walked() - budget_.movementAtCommand) > kStuckShort;
    if (actor.busy && !heldByEffect_ && (at == CommandKind::Move || at == CommandKind::Free || short_))
    {
        if (!stoodAt_ || Distance(*stoodAt_, actor.position) > kStoodStill)
        {
            stoodAt_ = actor.position;
            stoodSince_ = clock_;
        }
        else if (clock_ - stoodSince_ >= Patience(short_ ? kStuckMs : kStoodMs))
        {
            const Combatant *current = order_.Current();
            const bool waited = budget_.lastKind == CommandKind::Action && ActionToGiveBack() &&
                                (short_ || budget_.lastType != actor.current);
            Log(LogLevel::Info, std::format("{} stood {} ms at a {} without a step: stopped{}",
                                            current != nullptr ? Utf8(current->name) : std::string{"?"},
                                            clock_ - stoodSince_, Name(actor.current),
                                            !waited  ? ""
                                            : short_ ? "; it cannot get to its target, and its action is given back"
                                                     : "; the action waiting behind it is given back"));
            if (waited)
            {
                GiveActionBack();
                Emit(EventKind::ActionGivenBack, actor.id, std::format("stood still at a {}", Name(actor.current)), 0,
                     Name(budget_.lastType));
            }
            if (short_)
            {
                // What was to be taken on arriving is not; an AI chooses
                // again (StepSettling), and the player is told.
                budget_.onArrivalMs = 0;
                budget_.refunded = true;
                ++budget_.givenBack;
                if (current != nullptr && PlayerControls(*current))
                {
                    SayGivenBack(fx, *current, budget_.lastType, L"No way through");
                }
            }
            fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
            stoodAt_.reset();
            return;
        }
    }
    else
    {
        stoodAt_.reset();
    }
    if (actor.busy)
    {
        idleTicks_ = 0;
        // End pressed while something the engine put there holds the
        // creature (an effect's command, say): the turn ends now, not when
        // it does. Its own action it finishes first.
        if (endTurnRequested_ && Classify(actor.current) == CommandKind::Involuntary)
        {
            EndTurn(fx, "ended by the player");
            return;
        }
        // At an effect's command with nothing of its own let through this
        // turn: held by it, though it was not seen so as the turn began. The
        // fight's first turn begins in the frame the fight does, before its
        // creature has been looked at: a spider still dropping from the
        // ceiling had the first turn and stood out max_action_ms, twelve
        // seconds of the game's time, before anyone moved (a run with no
        // hands, 2026-10-02).
        // Getting up from a hold the machine ended has a length (kGetUpMs).
        // Still at an effect's command past it, the creature is held by that
        // effect, whatever it is: one the statuses do not name, laid on it
        // beside the stun that was ended, or the engine's command for the
        // ended one not over. With no length, nothing bounded the turn but
        // max_action_ms: a hurlock alpha, its stun ended as its turn began,
        // stood twelve seconds of the game's time at "execute effect" (issue
        // 11, the highway to Lothering, 2026-10-03).
        const bool rising = GettingUp() && clock_ - executingSince_ < kGetUpMs;
        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0 && !rising)
        {
            heldByEffect_ = true;
        }
        // Held in an effect's command past the grace: the turn is lost, as
        // to a stun, and the effect left to the engine (its time, spent a
        // round at a time, frees the creature).
        if (heldByEffect_ && actor.current == CommandType::ExecuteEffect && clock_ - executingSince_ >= kEffectGraceMs)
        {
            if (const Combatant *current = order_.Current())
            {
                Log(LogLevel::Info, std::format("{} is held by an effect: its turn is lost", Utf8(current->name)));
                Floaty(fx, current->id, current->name + L" loses the turn",
                       current->side == Side::Party ? kGold : kGrey);
            }
            EndTurn(fx, "held by an effect");
            return;
        }
        const std::uint32_t limit =
            Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * (ownEffectSince_ ? kOwnEffectPatience : 1);
        if (clock_ - executingSince_ > limit)
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
            Log(LogLevel::Warn, std::format("still at a {} after {} ms: stopped", Name(actor.current), limit));
            // Its target did not wait so long for anything the game meant:
            // what this action's steps added to its debt is not owed (play
            // test 33: a victim owing 31.8 s of a stall, its turns lost while
            // an ogre killed it).
            if (const auto debt = grabbedMs_.find(budget_.lastTarget);
                budget_.grabbedNowMs > 0 && debt != grabbedMs_.end())
            {
                debt->second -= std::min(debt->second, std::exchange(budget_.grabbedNowMs, 0));
                if (debt->second == 0)
                {
                    grabbedMs_.erase(debt);
                }
            }
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
    // Its command is over and did not fail (OnCommandFailed drops what was
    // to be taken): the action it walked for ran.
    if (budget_.onArrivalMs > 0)
    {
        ChargeOnArrival(fx);
    }
    OverAtOnce(fx);
    const Combatant *current = order_.Current();
    const bool player = current != nullptr && PlayerControls(*current);
    const int wait = player ? settings_.settleMs : std::max(settings_.settleMs, settings_.aiFollowUpMs);
    deadline_ = clock_ + static_cast<std::uint32_t>(wait);
    phase_ = Phase::Settling;
    // An action has ended: everyone is looked at with the next step, not
    // when the half second comes round, so that the cards follow a heal or a
    // hit at once (the user, 2026-10-03, issue 23: "Yeah we should do that,
    // reads should be pretty cheap"). A poultice drunk showed on its card
    // only at the next look, up to half a second on, and a script that read
    // the state right after a drink saw the old health and drank again,
    // three times in one turn (the playthrough).
    statusWanted_ = true;
    if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug, "its command is over: everyone is looked at with the next step");
    }
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
    // An AI is told to choose in mid-turn only on a look taken with this
    // step: on one half a second old, a creature stunned since was told
    // (test_fights.cpp, seeds 35 and 126 with time). The statuses are asked
    // for with the next step, which ends the turn of one that is held.
    const bool fresh = player || !Timed() || lastStatusNow_ == lastNow_;
    if (!fresh)
    {
        statusWanted_ = true;
    }
    // The charged cast is done (or never came): the turn it began is its
    // creature's now, handed over as any turn is: an AI told, the player waited on.
    if (budget_.landing && !fresh)
    {
        return;
    }
    if (std::exchange(budget_.landing, false))
    {
        if (landing_)
        {
            Log(LogLevel::Info,
                std::format("{}'s charged cast of ability {} did not come", Utf8(current->name), landing_->ability));
            landing_.reset();
        }
        // An AI may have gone on by itself as its cast ended, and spent its
        // action: told its turn had come then, it had nothing left to
        // choose, and its turn ended "chose nothing" a second and a half
        // late (the twelfth play test, 2026-10-01). Its turn ends as any.
        if (player || budget_.spend.ActionsLeft() > 0)
        {
            Handover(*current, fx);
            return;
        }
    }
    if (!player && passRequested_)
    {
        EndTurn(fx, "done");
        return;
    }
    // An AI whose action failed without acting has it back (OnCommandFailed)
    // and is told its turn has come once more: it may choose something
    // else. Its turn ended here before, the action unused: an ally guard,
    // no room at his target every turn (COMMAND_FAILED_NO_SPACE_IN_MELEE_
    // RING), stood through a whole fight (the user, 2026-09-30). Once a
    // turn, as the action is given back once.
    // Twice given back: it has nothing it can get to, and would stand at a
    // third try as long as at the first two (a blocked spider's turn was six
    // seconds: play test 31).
    if (!player && budget_.givenBack >= kGivenBackEnough)
    {
        EndTurn(fx, "nothing it can get to");
        return;
    }
    if (!player && budget_.spend.ActionsLeft() > 0 && budget_.refunded && !budget_.retried && !budget_.outOfMovement)
    {
        budget_.retried = true;
        Log(LogLevel::Info, std::format("{} chooses again, its action given back", Utf8(current->name)));
        fx.push_back(Effect::Of(EffectKind::PokeAi, current->id));
        deadline_ = clock_ + static_cast<std::uint32_t>(settings_.aiThinkMs);
        phase_ = Phase::AiThinking;
        return;
    }
    if (clock_ < deadline_)
    {
        return;
    }
    if (!player)
    {
        // With time, an AI with time left chooses again, as many times as
        // its commands allow (max_commands_per_turn): its turn ends as its
        // bar is spent, as it waits, or as it chooses nothing.
        if (Timed() && budget_.spend.ActionsLeft() > 0 && !budget_.outOfMovement &&
            budget_.commands < settings_.maxCommandsPerTurn)
        {
            if (!fresh)
            {
                return;
            }
            Log(LogLevel::Info, std::format("{} chooses again: {}", Utf8(current->name), BudgetLeft()));
            fx.push_back(Effect::Of(EffectKind::PokeAi, current->id));
            deadline_ = clock_ + static_cast<std::uint32_t>(settings_.aiThinkMs);
            phase_ = Phase::AiThinking;
            return;
        }
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
    // The turn waits again only on a look at everyone taken with this step,
    // as an AI is told to choose again only on one: what landed while the
    // world settled (an arrow in flight as its shot's command ended) is on
    // the cards and in the state as the turn waits, for whoever reads them
    // then (issue 23). A frame more of settling when the last look is older.
    if (lastStatusNow_ != lastNow_)
    {
        statusWanted_ = true;
        return;
    }
    if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug, std::format("awaiting the next order: {}", BudgetLeft()));
    }
    // What the order did may have shown the party a room: a door opened, a
    // walk through one. Who fights there joins now, with their cards, not
    // when the turn ends: for the rest of Lyna's turn the room stood open,
    // its spiders frozen and not on the bar (the eighth play test,
    // 2026-10-01).
    wantScan_ = true;
    AwaitOrder("after its action");
}

void TurnMachine::TrackMovement(const ActorObservation &actor, std::vector<Effect> &fx)
{
    if (lastPosition_ && actor.moving)
    {
        const float step = Distance(*lastPosition_, actor.position);
        if (step <= kTeleport)
        {
            budget_.spend.Walk(step);
            MoveClock(fx);
        }
    }
    lastPosition_ = actor.position;
    if (!actor.moving || budget_.spend.Overwalked() < kOvershoot)
    {
        return;
    }
    // Shifting its feet to strike what is in reach, or to open the door it
    // stands at, is not walking (kShuffle).
    if ((budget_.lastKind == CommandKind::Action || budget_.lastKind == CommandKind::Free) &&
        budget_.spend.Walked() - budget_.movementAtCommand < kShuffle)
    {
        return;
    }
    // With time, an action charged as it was let through, whose walk then
    // runs past the bar: its time goes to the walk, and the action is
    // charged when its creature gets there (ChargeOnArrival), what does not
    // fit then owed to the next turn. Stopped here, a bandit that walked
    // for a line of sight lost the turn, its shot never fired (play test
    // 26), as a spider did whose path was longer than the gate could tell.
    if (budget_.spend.Timed() && budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 &&
        budget_.onArrivalMs == 0)
    {
        budget_.onArrivalMs = budget_.lastCostMs;
        budget_.spend.GiveActionBack(std::exchange(budget_.lastCostMs, 0));
        budget_.spend.SettleOverwalk();
        const Combatant *walker = order_.Current();
        Log(LogLevel::Info,
            std::format("{}'s walk runs past its bar: its {}'s {:.2f} s goes to the walk, and is "
                        "taken when it gets there ({})",
                        walker != nullptr ? Utf8(walker->name) : std::string{"?"}, Name(budget_.lastType),
                        static_cast<float>(budget_.onArrivalMs) / 1000.0f, BudgetLeft()));
        MoveClock(fx);
        if (budget_.spend.Overwalked() < kOvershoot)
        {
            return;
        }
    }
    budget_.onArrivalMs = 0; // stopped: the action never began

    fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
    // Stopped on the way to an attack or ability, it never used it: it is
    // given back, once a turn. Every later try to reach something takes a
    // step before it is stopped, so a second refund would let it creep on
    // a frame at a time (test_fights.cpp found it).
    const bool refund = budget_.lastKind == CommandKind::Action && ActionToGiveBack() && !budget_.refunded;
    if (refund)
    {
        GiveActionBack();
        budget_.refunded = true;
    }
    // Nor is the talent it was on its way to use held on cooldown: chosen,
    // walked towards and never reached, it was wasted (the user,
    // 2026-09-30). Given back or not, the command never ran.
    const bool unused = budget_.lastKind == CommandKind::Action && budget_.lastAbility >= 0 &&
                        std::erase_if(usedNow_, [&](const Hold &h) {
                            return h.creature == actor.id && h.ability == budget_.lastAbility;
                        }) > 0;
    budget_.outOfMovement = true;
    const Combatant *current = order_.Current();
    if (budget_.approachWalk)
    {
        // The mod's own walk, to where its creature's kept attack lands:
        // it did what was ordered. "Out of movement" over it said it had
        // failed (play test 27).
        Log(LogLevel::Info,
            std::format("{}'s walk to its kept attack's target ends where the meters paid for end ({:.1f} m)",
                        current != nullptr ? Utf8(current->name) : std::string{"?"}, budget_.spend.Walked()));
        if (current != nullptr && !PlayerControls(*current))
        {
            EndTurn(fx, "its action pending");
        }
        return;
    }
    Floaty(fx, actor.id, L"Out of movement", kGrey);
    Log(LogLevel::Info,
        std::format("{} is out of movement ({:.1f} of {:.1f} m){}",
                    current != nullptr ? Utf8(current->name) : std::string{"?"}, budget_.spend.Walked(),
                    budget_.spend.MovementMax(), refund ? "; the action it was moving for is kept" : "") +
            (unused ? std::format("; ability {}, not reached, is not held on cooldown", budget_.lastAbility)
                    : std::string{}));
    if (current != nullptr && !PlayerControls(*current))
    {
        EndTurn(fx, "out of movement");
    }
}

void TurnMachine::EndTurn(std::vector<Effect> &fx, std::string_view why)
{
    HoldUsed(fx);
    bool land = false;
    if (const Combatant *current = order_.Current(); current != nullptr && Timed() && std::exchange(clockRuns_, false))
    {
        // Its clock moves a turn's seconds a turn, whatever was spent,
        // banked or lost: the rest of the way now.
        const std::uint32_t turn = TurnMs();
        AgeCooldowns(current->id, turn - std::min(turn, clockSeenMs_), fx);
        // And its effects: a turn's seconds in all, whatever part of them
        // the bar's spending and its own action's effect moved already.
        if (const std::uint32_t rest = turn - std::min(turn, budget_.agedOwnMs); rest > 0 && OwnClocks() && timeHeld_)
        {
            Effect age = Effect::Of(EffectKind::AgeTime, current->id);
            age.b = static_cast<std::int32_t>(rest);
            fx.push_back(age);
            budget_.agedOwnMs += rest;
            land = seenActor_ && seenActor_->id == current->id && seenActor_->timersHeld;
        }
    }
    clockSeenMs_ = 0;
    if (const Combatant *current = order_.Current())
    {
        // With time, what is left of the bar rolls over, to the creature's
        // cap; a turn lost whole, or spent, leaves none.
        if (budget_.spend.Timed())
        {
            const std::uint32_t rolled = budget_.spend.Finish(TimeFromTactics(TacticsOf(*current)).rolloverCapMs);
            rollover_[current->id] = rolled;
            Log(LogLevel::Info, std::format("{}'s turn ends: {}; {:.2f} s rolls over", Utf8(current->name), why,
                                            static_cast<float>(rolled) / 1000.0f));
        }
        else
        {
            rollover_.erase(current->id);
            Log(LogLevel::Info, std::format("{}'s turn ends: {}", Utf8(current->name), why));
        }
        Emit(EventKind::TurnEnded, current->id, std::string{why});
        Signal(fx, events::kTurnEnd, current->id, order_.Round(), 0);
    }
    if (order_.Advance())
    {
        roundPending_ = true;
    }
    LeaveTurn();
    if (land)
    {
        landFromNow_ = lastNow_;
    }
}

bool TurnMachine::Landing() const
{
    return phase_ == Phase::TurnStart && landFromNow_ && lastNow_ - *landFromNow_ < kLandMs;
}

void TurnMachine::LeaveTurn()
{
    budget_ = Budget{};
    heldByEffect_ = false;
    endedFor_ = kNoObject;
    approach_ = false;
    handoverWanted_.reset();
    pendingEndNow_.reset();
    delayRequested_ = false;
    landFromNow_.reset();
    budget_.spend.SpendAllActions();
    // Nothing to resume: a turn left open by a conversation and ended as
    // the fight came back (a cast kept just before it) is over, and the
    // next is begun, not resumed (test_fights.cpp, seed 594 with time).
    resume_ = false;
    phase_ = Phase::TurnStart;
    wantScan_ = true;
}

// Delay: nothing of the turn was spent, so nothing of it is settled. Its
// creature's clock has not moved and does not (no cooldown the older, no
// rollover made or lost), and the bar it is given when its turn comes is
// the one it had. The turn passes to the next standing combatant.
bool TurnMachine::CanDelay() const
{
    const Combatant *current = order_.Current();
    return settings_.delay && phase_ == Phase::AwaitingOrder && current != nullptr && PlayerControls(*current) &&
           budget_.commands == 0 && budget_.pendingPaidMs == 0 && !budget_.landing &&
           budget_.spend.Walked() < kMinStep && budget_.spend.ClockElapsedMs() == 0 && order_.CanDelay();
}

bool TurnMachine::OnDelay()
{
    if (!CanDelay())
    {
        return false;
    }
    delayRequested_ = true;
    return true;
}

void TurnMachine::DelayTurn(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr)
    {
        return;
    }
    const ObjectId who = current->id;
    const std::wstring name = current->name;
    if (!order_.Delay())
    {
        return;
    }
    Log(LogLevel::Info, std::format("{}'s turn is put off to the round's end, nothing of it spent", Utf8(name)));
    Emit(EventKind::TurnDelayed, who);
    Floaty(fx, who, name + L" waits", kGrey);
    clockRuns_ = false; // not this turn's end: its clock stays where it is
    clockSeenMs_ = 0;
    LeaveTurn();
}

void TurnMachine::End(std::vector<Effect> &fx, std::string_view why)
{
    Log(LogLevel::Info, std::format("the turn-based fight is over after {} rounds: {}", order_.Round(), why));
    // The hero, the player's own, is who they control after: the last turn's
    // companion stayed selected, and the first click after the fight moved
    // her (the third play test, 2026-09-30).
    if (const Combatant *hero = hero_ != kNoObject ? order_.Find(hero_) : nullptr; hero != nullptr && !hero->down)
    {
        fx.push_back(Effect::Of(EffectKind::Select, hero_));
    }
    Emit(EventKind::FightOver, kNoObject, std::string{why});
    playerPaused_ = false;
    while (!charges_.empty())
    {
        Interrupt(charges_.front().caster, "the fight over", fx);
    }
    if (landing_)
    {
        Interrupt(landing_->caster, "the fight over", fx);
    }
    chargeBegan_ = false;
    approach_ = false;
    HoldUsed(fx);
    ReleaseCooldowns(fx);
    HoldTime(false, fx);
    SetSpeed(1.0f, fx);
    resume_ = false;
    Signal(fx, events::kCombatEnd, kNoObject, order_.Round(), 0);
    fx.push_back(Effect::Of(EffectKind::Unpause));
    // Those that waited outside are the game's again, and are told so: the
    // one command each tried was held, and an AI whose command was held
    // tries no other by itself. In the Korcari Wilds a hurlock, an archer
    // and four wolves at each other's throats 45 m off stood as they were,
    // in combat and commandless, half a minute after the party's fight was
    // over (issue 6, the playthrough, 2026-10-03). Their AI chooses now, in
    // real time, the machine idle by the time the game layer tells them.
    for (const ObjectId id : outside_)
    {
        fx.push_back(Effect::Of(EffectKind::PokeAi, id));
    }
    // And those of the fight still standing that are not the party's, enemy
    // or ally. An AI with nothing to do waits, and the wait's end runs it
    // again: its own loop. In a fight the wait is its pass and is blocked,
    // so nothing is queued and nothing ends, and it chooses again only when
    // told its turn has come. With the turns over nobody tells it. At
    // Redcliffe's windmill the game ended combat with one walking corpse
    // alive up the path, seven rounds of "chose to wait" behind it: it stood
    // there in combat, commandless, and the night's second battle, which
    // waits on the first's dead, never began (issue 12, 2026-10-03).
    // Not one a condition holds: it cannot choose, and its own effect's
    // end runs its AI.
    for (const Combatant &c : order_.All())
    {
        const auto hold = held_.find(c.id);
        const bool held = hold != held_.end() && hold->second.seen != Condition::None;
        if (c.side != Side::Party && !c.down && !held)
        {
            fx.push_back(Effect::Of(EffectKind::PokeAi, c.id));
        }
    }
    outside_.clear();
    order_.Clear();
    held_.clear();
    budget_ = Budget{};
    phase_ = Phase::Idle;
    latch_ = true;
    wantScan_ = false;
}

// A frame threw between the machine's step and the engine's taking its
// effects (the review's TB-3, 2026-09-30): the machine is ahead of the
// game, which may be left paused, its effects' clock held, at an enemy's
// speed, its cooldowns held for an hour, and the machine, believing it said
// so already, would not say again. Everything is let go, said whatever the
// machine believes, and this fight is left to the game: turns begin again
// with the next one.
std::vector<Effect> TurnMachine::StandDown(std::string_view why)
{
    std::vector<Effect> fx;
    if (stoodDown_ && phase_ == Phase::Idle)
    {
        return fx; // already, and nothing since
    }
    Log(LogLevel::Warn, std::format("standing down until the next fight: {}", why));
    pending_.clear();
    timeHeld_ = true;
    speedSet_ = 0.0f;
    if (phase_ != Phase::Idle)
    {
        End(fx, why);
    }
    else
    {
        HoldTime(false, fx);
        SetSpeed(1.0f, fx);
    }
    stoodDown_ = true;
    return fx;
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
            // A scan says who fights and how it stands; its mana or stamina
            // is the statuses' to read (CombatantStatus), and is kept as
            // they last read it, as its initiative is kept. Written over
            // with the scan's blank (full, and nobody's mana), every card
            // showed full from each scan to the next look at the statuses:
            // from each turn's start, and from each time a turn waited again
            // after an action, for up to half a second (read in the code
            // with issue 23's second half, 2026-10-03; not looked for in
            // play).
            const int initiative = known->initiative;
            const float energy = known->energy;
            const bool mana = known->mana;
            *known = seen;
            known->initiative = initiative;
            known->energy = energy;
            known->mana = mana;
            absentSince_.erase(seen.id);
            if (left_.erase(seen.id) > 0 && !seen.down)
            {
                Log(LogLevel::Info, std::format("{} is fighting again", Utf8(seen.name)));
                Emit(EventKind::UpAgain, seen.id, "fighting again");
            }
        }
        else if (!seen.down && !seen.engaged)
        {
            if (outside_.insert(seen.id).second)
            {
                Log(LogLevel::Info,
                    std::format("{} is fighting near, but not the party: it waits outside the fight", Utf8(seen.name)));
            }
        }
        else if (!seen.down)
        {
            outside_.erase(seen.id);
            left_.erase(seen.id); // dropped from the order since it left: back as one that joins
            Combatant joining = seen;
            joining.initiative = RollInitiative(rng_, seen.dexterity, settings_, seen.survival);
            Log(LogLevel::Info, std::format("{} joins the fight ({}, initiative {})", Utf8(joining.name),
                                            Name(joining.side), joining.initiative));
            // Whatever it was doing as it came (a patrol, an attack queued as
            // its combat state turned, a cutscene's walk) stops: it waits for
            // its first turn like everyone else.
            fx.push_back(Effect::Of(EffectKind::ClearCommands, joining.id));
            const ObjectId joined = joining.id;
            const int initiative = joining.initiative;
            order_.Join(std::move(joining));
            if (OwnClocks() && timeHeld_)
            {
                Effect own = Effect::Of(EffectKind::OwnClock, joined);
                own.a = 1;
                fx.push_back(own);
            }
            Emit(EventKind::Joined, joined, {}, initiative);
        }
    }
    // An enemy or ally no longer seen fighting has left it: dead and gone,
    // fled out of range, or out of combat. Once it has been missing a
    // while: one scan without it is not that (absentSince_).
    std::vector<ObjectId> absent;
    for (const Combatant &c : order_.All())
    {
        if (c.side != Side::Party && !c.down &&
            std::ranges::none_of(scan, [&](const Combatant &s) { return s.id == c.id; }))
        {
            const std::uint32_t since = absentSince_.try_emplace(c.id, lastNow_).first->second;
            if (lastNow_ - since >= kAbsentMs)
            {
                absent.push_back(c.id);
            }
        }
    }
    for (const ObjectId id : absent)
    {
        Combatant *c = order_.Find(id);
        c->down = true;
        absentSince_.erase(id);
        left_.insert(id);
        Log(LogLevel::Info, std::format("{} is no longer fighting", Utf8(c->name)));
        Emit(EventKind::Gone, id, "no longer fighting");
    }
}

void TurnMachine::ApplyStatuses(const std::vector<CombatantStatus> &statuses, std::vector<Effect> &fx)
{
    for (const CombatantStatus &status : statuses)
    {
        if (!status.exists || status.down || status.condition == Condition::Disabled)
        {
            // Dropped with this step's own effects, ahead of what the step
            // goes on to say. Put off to the next step's (pending_, which
            // this step had already taken), the drop came a frame late: a
            // creature held as its turn came, its hold short enough to leave
            // it time, was told to choose in this frame and began a new
            // cast, and the drop of its old one cleared the new (the long
            // fights' seed 790 with time, found as issue 16 changed the
            // timings, 2026-10-03).
            Interrupt(status.id, !status.exists ? "gone" : status.down ? "fell" : "held", fx);
        }
        Combatant *c = order_.Find(status.id);
        if (c == nullptr)
        {
            continue;
        }
        // Said at debug as a look finds it changed, so that when the cards
        // learned of a heal or a hit can be read off the log against the
        // action's own lines (issue 23: at once as the action ends, not up
        // to half a second on).
        if (Logs(LogLevel::Debug) && status.exists && std::abs(status.health - c->health) >= 0.005f)
        {
            Log(LogLevel::Debug, std::format("{}'s health on its card: {:.0f}% to {:.0f}%", Utf8(c->name),
                                             c->health * 100.0f, status.health * 100.0f));
        }
        c->health = status.health;
        c->energy = status.energy;
        c->mana = status.mana;
        Held &held = held_[status.id];
        if (status.condition != Condition::None && status.condition != held.seen)
        {
            // Newly held: it owes that many of its turns, whatever the
            // condition's timer in seconds says; or, on the game's clock,
            // it loses each turn that comes while it lasts.
            if (status.id == endedFor_)
            {
                // Ended at this turn's start, and the engine holds it still:
                // not a new condition. It runs out on the game's clock.
                held.kind = status.condition;
                held.onClock = true;
                Log(LogLevel::Warn, std::format("{}'s condition was ended, and the engine still holds it: its turns "
                                                "are lost while it lasts",
                                                Utf8(c->name)));
                Emit(EventKind::ConditionStillHeld, c->id);
            }
            else if (Timed() && (status.conditionSeconds > 0.0f || settings_.conditionTurns > 0))
            {
                // Its own seconds, as the engine holds them now; where they
                // cannot be read, condition_turns' worth (at 0, below: its
                // turns lost while it lasts).
                constexpr float kLongest = 600.0f;
                const std::uint32_t known = static_cast<std::uint32_t>(
                    std::lround(std::clamp(status.conditionSeconds, 0.0f, kLongest) * 1000.0f));
                const std::uint32_t owed =
                    known > 0 ? known : static_cast<std::uint32_t>(settings_.conditionTurns) * TurnMs();
                held.kind = status.condition;
                held.owedMs = std::max(held.owedMs, owed);
                Log(LogLevel::Info, std::format("{} is held (stunned, pinned, knocked down): {:.1f} s of its time{}",
                                                Utf8(c->name), static_cast<float>(held.owedMs) / 1000.0f,
                                                known > 0 ? "" : " (its seconds not read: condition_turns' worth)"));
                Emit(EventKind::ConditionHeld, c->id, known > 0 ? "its seconds" : "condition_turns",
                     static_cast<std::int32_t>(held.owedMs));
            }
            else if (settings_.conditionTurns > 0)
            {
                held.kind = status.condition;
                held.turns = std::max(held.turns, settings_.conditionTurns);
                Log(LogLevel::Info, std::format("{} is held (stunned, pinned, knocked down): {} of its turns lost",
                                                Utf8(c->name), held.turns));
                Emit(EventKind::ConditionHeld, c->id, {}, held.turns);
            }
            else
            {
                // On the game's clock: what an earlier hold still owed is
                // this one's to take, for as long as it lasts (with an old
                // debt kept, the machine ended this one as the debt was
                // paid: test_fights.cpp, seed 9 with time).
                held.kind = Condition::None;
                held.owedMs = 0;
                Log(LogLevel::Info, std::format("{} is held (stunned, pinned, knocked down): its turns lost while it "
                                                "lasts",
                                                Utf8(c->name)));
                Emit(EventKind::ConditionHeld, c->id, {}, 0);
            }
            const Combatant *current = order_.Current();
            if (status.condition == Condition::Disabled && current != nullptr && current->id == status.id)
            {
                currentStunned_ = true;
            }
        }
        if (held.seen != Condition::None && status.condition == Condition::None)
        {
            // Gone by itself (the machine's ending forgets it at once). Run
            // out on the clock, it keeps the turns it owes: effects' time
            // moves only as a round begins, and a stun shorter than a round
            // would cost nothing otherwise. Taken off by something else (a
            // cure, a dispel), with effects' time held, it gives them back:
            // a stun lifted before its victim's turn still cost the turn
            // (the fourth play test's addendum, 2026-09-30).
            const bool takenOff = timeHeld_ && (!agedAtNow_ || lastNow_ - *agedAtNow_ > kAgeSettleMs);
            if (takenOff && held.owedMs > 0)
            {
                Log(LogLevel::Info, std::format("{}'s condition was taken off: the {:.1f} s it owed given back",
                                                Utf8(c->name), static_cast<float>(held.owedMs) / 1000.0f));
                held.owedMs = 0;
                held.kind = Condition::None;
                held.onClock = false;
            }
            if (takenOff && held.turns > 0)
            {
                Log(LogLevel::Info, std::format("{}'s condition was taken off: the {} turn{} it owed given back",
                                                Utf8(c->name), held.turns, held.turns == 1 ? "" : "s"));
                held.turns = 0;
                held.kind = Condition::None;
                held.onClock = false;
            }
            Emit(EventKind::ConditionExpired, c->id, takenOff ? "taken off" : "ran out");
        }
        held.seen = status.condition;
        if (status.condition == Condition::None)
        {
            held.onClock = false; // run out
        }
        const bool wasDown = c->down;
        if (!status.exists)
        {
            c->down = c->down || c->side != Side::Party;
        }
        else if (!left_.contains(c->id) || status.down)
        {
            // One that left the fight alive stays out until a scan finds it
            // fighting again (Merge), whatever its health says.
            c->down = status.down;
        }
        // Said, not only seen as a gap in the next round's order (the desktop
        // play test, 2026-09-30).
        if (c->down != wasDown)
        {
            // Fallen, it owes nothing of what held it before: its turns are
            // passed over while it is down, and the debt outlived it. Lyna,
            // fallen in an ogre's grab and up again, lost four turns to a
            // spider's Overwhelm of 9.3 s, 9.9 s of them the grab's (play test
            // 34).
            if (c->down)
            {
                grabbedMs_.erase(c->id);
            }
            Log(LogLevel::Info, c->down ? std::format("{} {}", Utf8(c->name), status.exists ? "falls" : "is gone")
                                        : std::format("{} is up again", Utf8(c->name)));
            Emit(c->down ? (status.exists ? EventKind::Fell : EventKind::Gone) : EventKind::UpAgain, c->id);
        }
    }
}

void TurnMachine::EnforcePause(const Observation &obs, std::vector<Effect> &fx) const
{
    if (phase_ == Phase::Idle || phase_ == Phase::Suspended)
    {
        return;
    }
    const bool want = (WantsPaused(phase_, settings_) && !Landing()) || playerPaused_;
    if (want != obs.paused)
    {
        fx.push_back(Effect::Of(want ? EffectKind::Pause : EffectKind::Unpause));
    }
}

bool TurnMachine::PlayerControls(const Combatant &c) const
{
    return c.side == Side::Party && (settings_.partyControl == PartyControl::Manual || c.hero);
}

float TurnMachine::TurnSpeed() const
{
    // The fight's one speed, everyone's turns alike; within what the game's
    // clock is known to take.
    return std::clamp(settings_.fightSpeed, 0.1f, 8.0f);
}

// The machine's patience with the engine, on its own clock. That clock runs
// with the fight's speed (clock_, Step), so that the waits that are the
// machine's own (settling, an AI's thinking) shorten with it. What it waits
// for the engine to do does not all shorten: a walk in the open goes 1.9
// times as fast at twice the speed and 2.7 at three (the playthrough's logs,
// 2026-10-03), but a creature working its way through a crowd crept on at
// about the wall clock's pace at any speed. Counted on the fast clock,
// max_action_ms was four seconds of wall clock at three times the speed, and
// 122 of a night's 1,394 turns at Redcliffe ended "took too long" where none
// of 1,220 had at twice (issue 16). So these are of the wall's time: at a
// speed of s, s times as long on the machine's clock; never less than given.
std::uint32_t TurnMachine::Patience(std::uint32_t ms) const
{
    return static_cast<std::uint32_t>(std::lround(static_cast<float>(ms) * std::max(1.0f, speedSet_)));
}

bool TurnMachine::TurnSpent() const
{
    if (budget_.spend.ActionsLeft() > 0)
    {
        return false;
    }
    if (settings_.endTurnWhenActionUsed || budget_.spend.Timed())
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

void TurnMachine::SayGivenBack(std::vector<Effect> &fx, const Combatant &who, CommandType type, std::wstring_view words)
{
    Floaty(fx, who.id, std::wstring{words}, kGold);
    // The caption's whatever the floaties' setting: it is the bar's own.
    notice_ = Notice{GivenBackLine(words, who.name, type), lastNow_};
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

void TurnMachine::Refuse(ObjectId target, std::wstring text, CommandType command, std::string note)
{
    lastPlayerRefusal_ = std::format("{} ({}{}{})", Utf8(text), Name(command), note.empty() ? "" : ": ", note);
    // The same refusal again within a second is the same event.
    const bool again = refusedEvent_ && refusedEvent_->who == target && refusedEvent_->command == command &&
                       refusedEvent_->text == text && lastNow_ - refusedEvent_->at < kRefusedEventMs;
    if (!again)
    {
        refusedEvent_ = RefusedEvent{target, command, text, lastNow_};
        Emit(EventKind::Refused, target, Utf8(text), 0, Name(command), std::move(note));
    }
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
        if (verdict != Verdict::Allow && phase_ == Phase::Dispatching)
        {
            AwaitOrder("the order was refused");
        }
        // An order taken while the player had the game paused runs it
        // again: paused, the command would sit unbegun, and the turn be
        // judged over a creature doing nothing.
        if (verdict == Verdict::Allow && std::exchange(playerPaused_, false))
        {
            Log(LogLevel::Info, "the player's pause is over: an order was given");
        }
    }
    return verdict;
}

Verdict TurnMachine::Judge(const CommandRequest &request)
{
    CommandKind kind = Classify(request.type, request.source);
    // A sustained ability is turned on or off for nothing (the user's rule,
    // 2026-09-30); action points will put a price on it.
    if (request.type == CommandType::UseAbility && request.sustained && CooldownTurnSeconds() > 0.0f)
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
            // One waiting outside the fight (Combatant::engaged) tries a
            // command every frame: looked at again twice a second.
            if (!outsiderScanNow_ || lastNow_ - *outsiderScanNow_ >= kOutsiderScanMs)
            {
                outsiderScanNow_ = lastNow_;
                wantScan_ = true;
            }
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
            Refuse(who->id, L"Not " + who->name + L"'s turn", request.type);
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
    // The cast its creature charged, or with time the action it kept, given
    // again by the game layer as its turn began (LandCast): not an order of
    // this turn's.
    const bool landing = landing_ && landing_->caster == who->id && request.type == landing_->type &&
                         request.ability == landing_->ability;
    // Its action kept for its next turn: this turn is over, the end come
    // with the next step (chargeBegan_) or after the beat over the player's
    // (pendingEndNow_). A mode turned on in the same frame ran on into the
    // next creature's turn (test_fights.cpp, seed 47 with time).
    if ((chargeBegan_ || pendingEndNow_) && kind != CommandKind::Involuntary)
    {
        if (Logs(refusals))
        {
            Refused(refusals, std::format("{}: {} held, its turn is over with its action pending", Utf8(who->name),
                                          Name(request.type)));
        }
        return Verdict::Block;
    }
    // Its spell given to the engine and on its way to its conjure: an order
    // now would queue behind it and run after its cast, in another's turn.
    if (budget_.conjureWaiting && kind != CommandKind::Free)
    {
        if (player && asked)
        {
            Refuse(who->id, L"Casting", request.type);
        }
        if (Logs(refusals))
        {
            Refused(refusals, std::format("{}: {} held, its spell is on its way to its conjure", Utf8(who->name),
                                          Name(request.type)));
        }
        return Verdict::Block;
    }
    // Held in an effect's command: an order would queue behind it and run
    // in someone else's turn (Eleanor's ability, pinned, 2026-09-30).
    if (heldByEffect_ && kind != CommandKind::Free)
    {
        if (player && asked)
        {
            Refuse(who->id, L"Held", request.type);
        }
        if (Logs(refusals))
        {
            Refused(refusals, std::format("{}: {} held, it is held by an effect", Utf8(who->name), Name(request.type)));
        }
        return Verdict::Block;
    }
    // On a turn the player plays, the character's own AI does not spend it:
    // a controlled character carries on attacking by itself (in real time,
    // AI_DetermineCombatRound_Partial), and in play that ended a pinned
    // character's turn before the player had chosen (2026-09-30, 11:30).
    const bool approaching = approach_ && kind == CommandKind::Move && request.source == Source::Script;
    // Nor does it choose the character's weapons, free as they are. A
    // follower that is not the controlled one changes to melee with an
    // enemy at hand and turns its bow's mode off for it
    // (AI_DetermineCombatRound, ai_main_h_2.nss: the controlled one's
    // partial AI never does), and a turn that opens with a kept action
    // landing has that full AI run before the character is selected: in the
    // playthrough Lyna and Tamlen left their bows unasked, and every order
    // after walked into melee (issue 4, 2026-10-02). The game's own rules
    // turn a mode off by its script's event, not by a command.
    const bool gear =
        request.type == CommandType::SwitchWeaponSets || (request.type == CommandType::UseAbility && request.sustained);
    // Nor does it wait. With no enemy left to choose, that full AI queues a
    // wait of a second (AI_ExecuteDefaultAction: _AI_DoNothing, quick), and
    // another as each ends; the controlled one's never waits ("Controled
    // follower - NOT WAITING"). Let through, each put the turn back at its
    // command with max_action_ms begun again: Lyna's turn, opened by a kept
    // shot that felled the fight's last enemy, stood eleven minutes at
    // executing, never handed over, End doing nothing (issue 10, the Tower
    // of Ishal, 2026-10-03). Held, the queue is empty and the turn settles.
    if (player && !landing && !approaching && request.source == Source::Script &&
        (kind == CommandKind::Action || kind == CommandKind::Move || kind == CommandKind::Pass || gear))
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
        if (landing)
        {
            // Given at another than it was kept for: its target had fallen,
            // and the game layer turned it on the nearest enemy in its reach
            // (LandCast). For nothing all the same: the swing was begun and
            // paid for, whoever it lands on.
            const bool turned = request.type == CommandType::Attack && request.target != kNoObject &&
                                request.target != landing_->target;
            std::string onto;
            if (turned)
            {
                const Combatant *other = order_.Find(request.target);
                onto = other != nullptr ? Utf8(other->name) : std::format("{:#x}", request.target);
                Log(LogLevel::Info,
                    std::format("{}'s pending attack is turned on {}: its target has fallen", Utf8(who->name), onto));
            }
            if (Timed())
            {
                Emit(EventKind::ActionLanded, who->id, turned ? "turned on another" : "", request.ability,
                     Name(request.type), std::move(onto));
            }
            else
            {
                Emit(EventKind::CastLanded, who->id, {}, request.ability);
            }
            landing_.reset();
            break; // for nothing: its turns were its price
        }
        if (budget_.spend.ActionsLeft() <= 0)
        {
            // The order just taken, come again (kEchoMs): held like any
            // other, but not the player's to be told about.
            const bool echo = budget_.commands > 0 && request.type == budget_.lastType &&
                              (request.type != CommandType::UseAbility || request.ability == budget_.lastAbility) &&
                              lastNow_ - budget_.lastAt < kEchoMs;
            if (player && asked && !echo)
            {
                Refuse(who->id, L"No action left", request.type);
            }
            if (const LogLevel level = echo ? LogLevel::Debug : refusals; Logs(level))
            {
                Refused(level, std::format("{}: {} held, no action left{}", Utf8(who->name), Name(request.type),
                                           echo ? " (the order just taken, again)" : ""));
            }
            return Verdict::Block;
        }
        // A shut door between the player's creature and its target
        // (CommandRequest::doorInTheWay): refused, with why.
        if (player && asked && request.doorInTheWay)
        {
            Refuse(who->id, L"A door is in the way", request.type, "open it first: that costs no action");
            Log(LogLevel::Info,
                std::format("{}: {} refused, a door is in the way of the walk", Utf8(who->name), Name(request.type)));
            return Verdict::Block;
        }
        // The player's order at something out of reach this turn is
        // refused before a step is taken, as BG3 refuses a red target. An
        // AI's goes on and walks what it can: closing in is its turn.
        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)
        {
            Refuse(who->id, L"Too far", request.type,
                   std::format("{:.1f} m to walk, {:.1f} m left", *request.walk, budget_.MovementLeft()));
            Log(LogLevel::Info, std::format("{}: {} refused, {:.1f} m to walk with {:.1f} m left", Utf8(who->name),
                                            Name(request.type), *request.walk, budget_.MovementLeft()));
            return Verdict::Block;
        }
        if (Timed())
        {
            if (const std::optional<Verdict> kept = PayAction(*who, request))
            {
                return *kept;
            }
            break;
        }
        budget_.lastCostMs = 0;
        (void)budget_.spend.SpendAction(0);
        // A cast that charges is not queued now: its caster's turn is spent
        // beginning it, and the game layer keeps it to give again.
        if (const int charge = ChargeTurns(request); charge > 0)
        {
            std::erase_if(charges_, [&](const Charge &c) { return c.caster == who->id; });
            Charge begun{who->id, request.ability, charge};
            begun.spell = request.abilityType == kAbilitySpell;
            charges_.push_back(begun);
            chargeBegan_ = true;
            Log(LogLevel::Info, std::format("{} begins to cast ability {}: it charges {} turn{} and is cast on the "
                                            "next",
                                            Utf8(who->name), request.ability, charge, charge == 1 ? "" : "s"));
            Emit(EventKind::CastCharging, who->id, std::format("{} more", charge), request.ability);
            Floaty(pending_, who->id, L"Casting", kGold);
            return Verdict::Hold;
        }
        break;
    case CommandKind::Move:
        if (budget_.outOfMovement || budget_.MovementLeft() < kMinStep)
        {
            if (player && asked)
            {
                Refuse(who->id, L"No movement left", request.type,
                       std::format("{:.1f} m left", budget_.MovementLeft()));
            }
            if (Logs(refusals))
            {
                Refused(refusals, std::format("{}: {} held, no movement left", Utf8(who->name), Name(request.type)));
            }
            return Verdict::Block;
        }
        // A ground click whose path is longer than the movement left is
        // refused before a step, as an attack out of reach is: the walk the
        // engine's path finder says (src/game/Gate.cpp, path_check), not a
        // walk cut short where it ran out.
        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)
        {
            Refuse(who->id, L"Too far", request.type,
                   std::format("{:.1f} m to walk, {:.1f} m left", *request.walk, budget_.MovementLeft()));
            Log(LogLevel::Info, std::format("{}: {} refused, {:.1f} m of path with {:.1f} m left", Utf8(who->name),
                                            Name(request.type), *request.walk, budget_.MovementLeft()));
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
        // With time, a door, a chest, a pickup: its cost of the bar
        // ([costs] object), what does not fit owed of the next turn, as an
        // item's use. Free before (the user, 2026-10-01, until "action points
        // will put a price on these").
        if (Timed() && UsesAnObject(request.type) && settings_.objectSeconds > 0.0f)
        {
            const auto cost = static_cast<std::uint32_t>(std::lround(settings_.objectSeconds * 1000.0f));
            if (const std::uint32_t owed = budget_.spend.SpendAction(cost); owed > 0)
            {
                Owe(who->id, owed, request.type, -1);
            }
            Log(LogLevel::Info, std::format("{}: {} costs {:.2f} s; {}", Utf8(who->name), Name(request.type),
                                            static_cast<float>(cost) / 1000.0f, BudgetLeft()));
            MoveClock(pending_);
        }
        break;
    case CommandKind::Involuntary:
        break;
    }
    if (request.type == CommandType::UseAbility && request.ability >= 0 && CooldownTurnSeconds() > 0.0f)
    {
        // Held once the action has run (HoldUsed): a cooldown set before
        // the ability runs might stop it running.
        const int turns = request.sustained
                              ? (request.sustainedOn ? 1 : 0)
                              : static_cast<int>(std::ceil(request.cooldownSeconds / CooldownTurnSeconds()));
        if (turns > 0)
        {
            // With time, its own seconds on its user's clock; a mode turned
            // off, a turn's worth, as built.
            const auto ms =
                !Timed() ? 0u
                : request.sustained
                    ? TurnMs()
                    : static_cast<std::uint32_t>(std::lround(std::min(request.cooldownSeconds, 3600.0f) * 1000.0f));
            usedNow_.push_back(Hold{who->id, request.ability, turns, turns, ms, ms});
        }
    }
    if (settings_.cameraFollow && !player && budget_.commands == 0)
    {
        // Applied with the next frame's effects, after the AI's call into
        // the gate has returned.
        pending_.push_back(Effect::Of(EffectKind::CenterCamera, who->id));
    }
    if (approaching)
    {
        approach_ = false; // the walk to its kept attack's target has come
    }
    budget_.approachWalk = approaching;
    budget_.ownEffectCover.reset();
    ++budget_.commands;
    budget_.lastKind = kind;
    budget_.lastAbility = request.type == CommandType::UseAbility ? request.ability : -1;
    budget_.movementAtCommand = budget_.spend.Walked();
    budget_.lastWalk = request.type == CommandType::Attack ? request.walk.value_or(0.0f) : 0.0f;
    budget_.lastType = request.type;
    budget_.lastTarget = request.target;
    budget_.lastAt = lastNow_;
    budget_.lastClock = clock_;
    budget_.lastMeasured = std::isfinite(request.actionSeconds) && request.actionSeconds > 0.0f;
    orderPending_ = false;
    // What the caption says of an order given back is of the order before
    // this one: the player has gone on.
    if (request.source == Source::Player)
    {
        notice_.reset();
    }
    Log(LogLevel::Info, std::format("{}: {} ({}, from {}); {}", Utf8(who->name), Name(request.type), Name(kind),
                                    Name(request.source), BudgetLeft()));
    const bool began = phase_ != Phase::Executing;
    if (began)
    {
        phase_ = Phase::Executing;
        idleTicks_ = 0;
    }
    // Each command has max_action_ms of its own: counted from the turn's
    // first, a spider's second attack was stopped a second after it began,
    // the first having stood ten at a target it could not reach (play test
    // 29, "still at a attack after 12000 ms").
    executingSince_ = clock_;
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
        return false; // no fight, or a conversation over it (not Active): the game's own
    }
    if (!playerPaused_ && WantsPaused(phase_, settings_))
    {
        return true; // held still by the machine already: nothing to pause
    }
    playerPaused_ = !playerPaused_;
    Log(LogLevel::Info, playerPaused_ ? "paused by the player" : "the player's pause is over");
    return true;
}

bool TurnMachine::OnEndTurn()
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

MachineState TurnMachine::State() const
{
    MachineState state;
    state.view = View();
    state.worldMs = clock_;
    for (const auto &[id, held] : held_)
    {
        if (held.seen == Condition::None && held.turns == 0)
        {
            continue;
        }
        const Combatant *c = order_.Find(id);
        state.held.push_back(
            {id, c != nullptr ? c->name : std::wstring{}, held.seen, held.turns, held.onClock, held.owedMs});
    }
    for (const Hold &h : holds_)
    {
        state.cooldowns.push_back({h.creature, h.ability, h.turns, h.of, h.leftMs, h.ofMs});
    }
    for (const Charge &c : charges_)
    {
        state.charging.push_back({c.caster, c.ability, c.turns, c.owedMs});
    }
    state.lastRefusal = lastPlayerRefusal_;
    state.speed = speedSet_;
    state.heldByEffect = heldByEffect_;
    state.playerPaused = playerPaused_;
    return state;
}

// A companion's AI spent her action twice on an attack the engine then
// failed with COMMAND_FAILED_INVALID_PATH, in a doorway (the third play
// test, 2026-09-30).
bool TurnMachine::OnCommandFailed(ObjectId who, CommandType type, std::int32_t ability, std::string_view why)
{
    const Combatant *current = order_.Current();
    if (!Active() || current == nullptr || current->id != who)
    {
        return false;
    }
    if (Classify(type) == CommandKind::Action)
    {
        budget_.onArrivalMs = 0; // it never ran: nothing to take
    }
    // A spell given to the engine to conjure that failed before it got there
    // (no path, no line of sight): nothing of it is pending.
    if (budget_.conjureWaiting && type == CommandType::UseAbility)
    {
        budget_.conjureWaiting = false;
        std::erase_if(charges_, [&](const Charge &c) { return c.caster == who && c.engine; });
        // Nothing of the bar was spent on it yet: the turn goes on.
        Log(LogLevel::Info, std::format("{}'s spell failed before its conjure ({}): nothing of it is pending",
                                        Utf8(current->name), why));
        Emit(EventKind::ActionGivenBack, who, std::string{why}, 0, Name(type));
        if (PlayerControls(*current))
        {
            SayGivenBack(pending_, *current, type, WhyGivenBack(why));
        }
        return true;
    }
    // A talent that never ran is not held on cooldown: an archer's was, for
    // a shot nobody took (the fourth play test, 19:21:52).
    if (type == CommandType::UseAbility &&
        std::erase_if(usedNow_, [&](const Hold &h) { return h.creature == who && h.ability == ability; }) > 0)
    {
        Log(LogLevel::Info,
            std::format("{}'s ability {} failed ({}): not held on cooldown", Utf8(current->name), ability, why));
    }
    // As built, once a turn: the turn's one action. With time, every time:
    // three dagger attacks at a spider she could not get to, each over at
    // once, and the second and third were charged 1.5 s each with no swing
    // and no word (play test 30). An AI still chooses again but once for a
    // failed action (StepSettling), and then as often as its commands allow.
    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || (budget_.refunded && !Timed()))
    {
        return false;
    }
    GiveActionBack();
    budget_.refunded = true;
    ++budget_.givenBack;
    Log(LogLevel::Info,
        std::format("{}'s {} failed ({}): its action is given back", Utf8(current->name), Name(type), why));
    Emit(EventKind::ActionGivenBack, who, std::string{why}, 0, Name(type));
    // Said over the player's own: the bar dropped and filled again with no
    // word, three tries running (play test 31). And on the bar's caption,
    // by the engine's reason: over the character alone it was missed, and
    // the same order tried again and again (issue 21).
    if (PlayerControls(*current))
    {
        SayGivenBack(pending_, *current, type, WhyGivenBack(why));
    }
    return true;
}

void TurnMachine::Reseed(std::uint64_t seed)
{
    rng_ = Rng{seed};
    Log(LogLevel::Info, std::format("initiative reseeded: {}", seed));
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
        view.actionsLeft = budget_.spend.ActionsLeft();
        view.movementLeft = budget_.MovementLeft();
        view.movementMax = budget_.spend.MovementMax();
        // By the setting: the budget is reset between two turns, and by it
        // the game's bar went back to experience for a third of a second
        // (play test 27).
        view.timed = Timed();
        view.pendingPaidMs = budget_.pendingPaidMs;
        view.timeMs = budget_.spend.CapacityMs();
        view.timeLeftMs = budget_.spend.CapacityLeftMs();
        view.turnMs = view.timed ? TurnMs() : 0;
        view.paceMps = budget_.spend.PaceMps();
        view.paidMeters = budget_.spend.PaidMeters();
        view.canDelay = CanDelay();
        view.delayOffered = settings_.delay && view.playerTurn;
        view.playerPaused = playerPaused_;
        view.rolledMs = budget_.rolledMs;
        if (notice_ && lastNow_ - notice_->at < kNoticeMs)
        {
            view.notice = notice_->text;
        }
    }
    // All of them: the bar's row is a window on these (core/BarWindow.h).
    for (const TurnOrder::Slot &slot : order_.Upcoming(order_.All().size()))
    {
        const Combatant &c = *slot.who;
        const auto owes = std::ranges::find(charges_, c.id, &Charge::caster);
        // One that put its turn off waits for it while its place in this
        // round is still to come.
        const bool waits = !slot.current && slot.round == order_.Round() && order_.Delayed(c.id);
        // What is pending is an ability's or an attack's (-1); the icon and
        // the name are the game layer's to add.
        const std::int32_t pending =
            owes != charges_.end() && owes->type == CommandType::UseAbility ? owes->ability : -1;
        view.upcoming.push_back({c.id, c.name, c.tag, c.side, c.rank, c.health, c.down, slot.current, PlayerControls(c),
                                 slot.round, c.energy, c.mana, owes != charges_.end() ? owes->owedMs : 0, waits,
                                 pending, std::wstring{}, std::wstring{}});
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
