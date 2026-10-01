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
    case EffectKind::HoldTime:
        return "hold time";
    case EffectKind::AgeTime:
        return "age time";
    case EffectKind::GameSpeed:
        return "game speed";
    case EffectKind::ReleaseCast:
        return "release cast";
    case EffectKind::DropCast:
        return "drop cast";
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
        latch_ = false;
        wantScan_ = false;
        const bool charging = !charges_.empty() || landing_.has_value();
        charges_.clear();
        landing_.reset();
        chargeBegan_ = false;
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
        // with it. In real time they held ai_turn_speed 2 to the same 3 s
        // an enemy's turn took at 1 (the desktop play test, 2026-09-30).
        const auto worldDt = static_cast<std::uint32_t>(std::lround(static_cast<float>(dt) * speedSet_));
        clock_ += worldDt;
        roundRunMs_ += worldDt;
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
    // A cast began to charge (Judge): its caster does nothing else this
    // turn.
    if (std::exchange(chargeBegan_, false) && Active() && order_.Current() != nullptr)
    {
        EndTurn(fx, "charging");
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
    currentStunned_ = false;
    usedNow_.clear();
    holds_.clear();
    absentSince_.clear();
    outside_.clear();
    left_.clear();
    charges_.clear();
    landing_.reset();
    chargeBegan_ = false;
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
        c.initiative = RollInitiative(rng_, c.dexterity, settings_);
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
    HoldTime(settings_.effectSecondsPerRound > 0.0f, fx);
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
    budget_.movementMax = MovementBudget(settings_, who.speed, who.dexterity);
    idleTicks_ = 0;
    lastPosition_.reset();
    endTurnRequested_ = false;
    passRequested_ = false;
    orderPending_ = false;

    HoldAtTurnStart(who, fx);
    // The game's speed for this turn before the camera moves: twice at
    // ai_turn_speed 2 the camera's pan to the hero, issued before the speed
    // went back to 1, never arrived (play tests 2 and 5, 2026-09-30).
    // Whether the order was why is not known yet: the camera now says
    // whether a pan got there (src/game/Camera.cpp).
    SetSpeed(TurnSpeed(who), fx);

    Log(LogLevel::Info, std::format("round {}: {}'s turn ({}, {}), {:.1f} m to move", order_.Round(), Utf8(who.name),
                                    Name(who.side), PlayerControls(who) ? "yours" : "AI", budget_.MovementLeft()));
    Emit(EventKind::TurnStarted, who.id, PlayerControls(who) ? "yours" : "AI",
         static_cast<std::int32_t>(std::lround(budget_.MovementLeft() * 10.0f)));
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
    const float turn = settings_.cooldownSecondsPerTurn;
    if (!(turn > 0.0f) || request.conjureSeconds < turn)
    {
        return 0;
    }
    return static_cast<int>(std::ceil(request.conjureSeconds / turn));
}

// A turn of a charging caster's: spent on the charge, unless this is the
// turn it is cast on (LandCast, from BeginTurn).
bool TurnMachine::ChargingTurn(const Combatant &who, std::vector<Effect> &fx)
{
    const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
    if (charge == charges_.end() || --charge->turns <= 0)
    {
        return false;
    }
    budget_ = Budget{};
    budget_.actions = 0;
    Log(LogLevel::Info, std::format("round {}: {}'s turn is spent charging ability {} ({} more)", order_.Round(),
                                    Utf8(who.name), charge->ability, charge->turns));
    Emit(EventKind::CastCharging, who.id, std::format("{} more", charge->turns), charge->ability);
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
    TickCooldowns(who, fx);
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
    Log(LogLevel::Info, std::format("{} casts ability {}: charged", Utf8(who.name), landing_->ability));
    Effect release = Effect::Of(EffectKind::ReleaseCast, who.id);
    release.a = landing_->ability;
    fx.push_back(release);
    phase_ = Phase::Executing;
    executingSince_ = clock_;
    idleTicks_ = 0;
    return true;
}

void TurnMachine::Interrupt(ObjectId who, std::string_view why, std::vector<Effect> &fx)
{
    std::int32_t ability = -1;
    if (const auto charge = std::ranges::find(charges_, who, &Charge::caster); charge != charges_.end())
    {
        ability = charge->ability;
        charges_.erase(charge);
    }
    else if (landing_ && landing_->caster == who)
    {
        ability = landing_->ability;
        landing_.reset();
    }
    else
    {
        return;
    }
    const Combatant *c = order_.Find(who);
    Log(LogLevel::Info, std::format("{}'s cast of ability {} is interrupted: {}",
                                    c != nullptr ? Utf8(c->name) : std::string{"?"}, ability, why));
    Emit(EventKind::CastInterrupted, who, std::string{why}, ability);
    fx.push_back(Effect::Of(EffectKind::DropCast, who));
    Floaty(fx, who, L"Interrupted", kGrey);
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
    Emit(EventKind::TurnResumed, who.id);
    Handover(who, fx);
}

// The turn to whoever plays it: the player waits for an order; an AI is
// told to choose.
// A turn a condition costs: it is taken at once, the world still, as the
// condition's own time is the turns (dev/DESIGN.md, "Conditions in turns").
bool TurnMachine::LoseTurn(const Combatant &who, std::vector<Effect> &fx)
{
    const auto found = held_.find(who.id);
    if (found == held_.end())
    {
        return false;
    }
    Held &held = found->second;
    // Owed turns; or, the condition on the game's clock (condition_turns 0),
    // every turn it still holds the creature: it cannot act, and with
    // effects' time held its command cannot end within the turn.
    const bool owes = held.kind == Condition::Disabled && held.turns > 0;
    const bool holds = (settings_.conditionTurns == 0 || held.onClock) && held.seen == Condition::Disabled;
    if (!owes && !holds)
    {
        return false;
    }
    if (owes)
    {
        --held.turns;
    }
    budget_ = Budget{};
    budget_.actions = 0;
    Log(LogLevel::Info, std::format("round {}: {}'s turn is lost to its condition ({} more)", order_.Round(),
                                    Utf8(who.name), held.turns));
    Emit(EventKind::TurnLost, who.id, {}, held.turns);
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
    TickCooldowns(who, fx);
    Floaty(fx, who.id, who.name + L" loses the turn", kGrey);
    EndTurn(fx, "held by its condition");
    return true;
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
    if (held.turns <= 0 && held.seen != Condition::None)
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
void TurnMachine::TickCooldowns(const Combatant &who, std::vector<Effect> &fx)
{
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

// The fight is over: what still waits goes back to seconds, its turns' worth.
void TurnMachine::ReleaseCooldowns(std::vector<Effect> &fx)
{
    for (const Hold &h : holds_)
    {
        fx.push_back(CooldownOf(
            h.creature, h.ability,
            static_cast<std::int32_t>(std::ceil(static_cast<float>(h.turns) * settings_.cooldownSecondsPerTurn)),
            static_cast<std::int32_t>(std::ceil(static_cast<float>(h.of) * settings_.cooldownSecondsPerTurn))));
    }
    holds_.clear();
}

// Effects' time in rounds: the clock every temporary effect ages on (DAOLib's
// dao/Effects.h) held through a fight, so none wears off while someone
// thinks or the world idles, and moved on a round's worth at each new round.
// The game's speed (DAOLib's dao::clock::SetSpeed): an AI's turn at
// ai_turn_speed, as BG3 speeds enemies' turns up; everything else at 1.
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
    Log(LogLevel::Info, held
                            ? std::format("effects' time held: {:.0f} s of it a round", settings_.effectSecondsPerRound)
                            : std::string{"effects' time runs again"});
}

void TurnMachine::SpendRound(std::vector<Effect> &fx)
{
    if (!(settings_.effectSecondsPerRound > 0.0f))
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
    const auto worth = static_cast<std::uint32_t>(settings_.effectSecondsPerRound * 1000.0f);
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

void TurnMachine::Handover(const Combatant &who, std::vector<Effect> &fx)
{
    // Set already as the turn began; again for a turn handed over mid-way.
    SetSpeed(TurnSpeed(who), fx);
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
    if (seenActor_ && seenActor_->id == who.id && seenActor_->busy && seenActor_->current == CommandType::ExecuteEffect)
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
        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect;
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
        // Its choice would have come through the gate, which moves on to
        // executing: busy here is something else's.
        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect;
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
            ownEffectSince_ = clock_;
            roundAgedMs_ += ran;
            Effect age = Effect::Of(EffectKind::AgeTime);
            age.b = static_cast<std::int32_t>(ran);
            fx.push_back(age);
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
    const CommandKind at = Classify(actor.current);
    if (actor.busy && !heldByEffect_ && (at == CommandKind::Move || at == CommandKind::Free))
    {
        if (!stoodAt_ || Distance(*stoodAt_, actor.position) > kStoodStill)
        {
            stoodAt_ = actor.position;
            stoodSince_ = clock_;
        }
        else if (clock_ - stoodSince_ >= kStoodMs)
        {
            const Combatant *current = order_.Current();
            const bool waited =
                budget_.lastKind == CommandKind::Action && budget_.actions == 0 && budget_.lastType != actor.current;
            Log(LogLevel::Info,
                std::format("{} stood {} ms at a {} without a step: stopped{}",
                            current != nullptr ? Utf8(current->name) : std::string{"?"}, clock_ - stoodSince_,
                            Name(actor.current), waited ? "; the action waiting behind it is given back" : ""));
            if (waited)
            {
                budget_.actions = 1;
                Emit(EventKind::ActionGivenBack, actor.id, std::format("stood still at a {}", Name(actor.current)), 0,
                     Name(budget_.lastType));
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
        // Held in an effect's command past the grace: the turn is lost, as
        // to a stun, and the effect left to the engine (its time, spent a
        // round at a time, frees the creature).
        if (heldByEffect_ && actor.current == CommandType::ExecuteEffect && clock_ - executingSince_ >= kEffectGraceMs)
        {
            if (const Combatant *current = order_.Current())
            {
                Log(LogLevel::Info, std::format("{} is held by an effect: its turn is lost", Utf8(current->name)));
                Floaty(fx, current->id, current->name + L" loses the turn", kGrey);
            }
            EndTurn(fx, "held by an effect");
            return;
        }
        const std::uint32_t limit =
            static_cast<std::uint32_t>(settings_.maxActionMs) * (ownEffectSince_ ? kOwnEffectPatience : 1);
        if (clock_ - executingSince_ > limit)
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
            Log(LogLevel::Warn, std::format("still at a {} after {} ms: stopped", Name(actor.current), limit));
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
    // The charged cast is done (or never came): the turn it began is its
    // creature's now, handed over as any turn is: an AI told, the player waited on.
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
        if (player || budget_.actions > 0)
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
    if (!player && budget_.actions > 0 && budget_.refunded && !budget_.retried && !budget_.outOfMovement)
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
    // What the order did may have shown the party a room: a door opened, a
    // walk through one. Who fights there joins now, with their cards, not
    // when the turn ends: for the rest of Lyna's turn the room stood open,
    // its spiders frozen and not on the bar (the eighth play test,
    // 2026-10-01).
    wantScan_ = true;
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
    // Shifting its feet to strike what is in reach, or to open the door it
    // stands at, is not walking (kShuffle).
    if ((budget_.lastKind == CommandKind::Action || budget_.lastKind == CommandKind::Free) &&
        budget_.movementUsed - budget_.movementAtCommand < kShuffle)
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
    // Nor is the talent it was on its way to use held on cooldown: chosen,
    // walked towards and never reached, it was wasted (the user,
    // 2026-09-30). Given back or not, the command never ran.
    const bool unused = budget_.lastKind == CommandKind::Action && budget_.lastAbility >= 0 &&
                        std::erase_if(usedNow_, [&](const Hold &h) {
                            return h.creature == actor.id && h.ability == budget_.lastAbility;
                        }) > 0;
    budget_.outOfMovement = true;
    const Combatant *current = order_.Current();
    Floaty(fx, actor.id, L"Out of movement", kGrey);
    Log(LogLevel::Info,
        std::format("{} is out of movement ({:.1f} of {:.1f} m){}",
                    current != nullptr ? Utf8(current->name) : std::string{"?"}, budget_.movementUsed,
                    budget_.movementMax, refund ? "; the action it was moving for is kept" : "") +
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
    if (const Combatant *current = order_.Current())
    {
        Log(LogLevel::Info, std::format("{}'s turn ends: {}", Utf8(current->name), why));
        Emit(EventKind::TurnEnded, current->id, std::string{why});
        Signal(fx, events::kTurnEnd, current->id, order_.Round(), 0);
    }
    if (order_.Advance())
    {
        roundPending_ = true;
    }
    budget_ = Budget{};
    heldByEffect_ = false;
    endedFor_ = kNoObject;
    budget_.actions = 0;
    phase_ = Phase::TurnStart;
    wantScan_ = true;
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
    while (!charges_.empty())
    {
        Interrupt(charges_.front().caster, "the fight over", fx);
    }
    if (landing_)
    {
        Interrupt(landing_->caster, "the fight over", fx);
    }
    chargeBegan_ = false;
    HoldUsed(fx);
    ReleaseCooldowns(fx);
    HoldTime(false, fx);
    SetSpeed(1.0f, fx);
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
            const int initiative = known->initiative;
            *known = seen;
            known->initiative = initiative;
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
            joining.initiative = RollInitiative(rng_, seen.dexterity, settings_);
            Log(LogLevel::Info, std::format("{} joins the fight ({}, initiative {})", Utf8(joining.name),
                                            Name(joining.side), joining.initiative));
            // Whatever it was doing as it came (a patrol, an attack queued as
            // its combat state turned, a cutscene's walk) stops: it waits for
            // its first turn like everyone else.
            fx.push_back(Effect::Of(EffectKind::ClearCommands, joining.id));
            const ObjectId joined = joining.id;
            const int initiative = joining.initiative;
            order_.Join(std::move(joining));
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

void TurnMachine::ApplyStatuses(const std::vector<CombatantStatus> &statuses)
{
    for (const CombatantStatus &status : statuses)
    {
        if (!status.exists || status.down || status.condition == Condition::Disabled)
        {
            Interrupt(status.id, !status.exists ? "gone" : status.down ? "fell" : "held", pending_);
        }
        Combatant *c = order_.Find(status.id);
        if (c == nullptr)
        {
            continue;
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

float TurnMachine::TurnSpeed(const Combatant &who) const
{
    return PlayerControls(who) ? 1.0f : settings_.aiTurnSpeed;
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
    // The cast its creature charged, given again by the game layer as its
    // turn began (LandCast): not an order of this turn's.
    const bool landing = landing_ && landing_->caster == who->id && request.type == CommandType::UseAbility &&
                         request.ability == landing_->ability;
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
    if (player && !landing && request.source == Source::Script &&
        (kind == CommandKind::Action || kind == CommandKind::Move))
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
            Emit(EventKind::CastLanded, who->id, {}, request.ability);
            landing_.reset();
            break; // for nothing: its turns were its price
        }
        if (budget_.actions <= 0)
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
        --budget_.actions;
        // A cast that charges is not queued now: its caster's turn is spent
        // beginning it, and the game layer keeps it to give again.
        if (const int charge = ChargeTurns(request); charge > 0)
        {
            std::erase_if(charges_, [&](const Charge &c) { return c.caster == who->id; });
            charges_.push_back(Charge{who->id, request.ability, charge});
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
            usedNow_.push_back(Hold{who->id, request.ability, turns, turns});
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
    budget_.lastAbility = request.type == CommandType::UseAbility ? request.ability : -1;
    budget_.movementAtCommand = budget_.movementUsed;
    budget_.lastType = request.type;
    budget_.lastAt = lastNow_;
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
        state.held.push_back({id, c != nullptr ? c->name : std::wstring{}, held.seen, held.turns, held.onClock});
    }
    for (const Hold &h : holds_)
    {
        state.cooldowns.push_back({h.creature, h.ability, h.turns, h.of});
    }
    for (const Charge &c : charges_)
    {
        state.charging.push_back({c.caster, c.ability, c.turns});
    }
    state.lastRefusal = lastPlayerRefusal_;
    state.speed = speedSet_;
    state.heldByEffect = heldByEffect_;
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
    // A talent that never ran is not held on cooldown: an archer's was, for
    // a shot nobody took (the fourth play test, 19:21:52).
    if (type == CommandType::UseAbility &&
        std::erase_if(usedNow_, [&](const Hold &h) { return h.creature == who && h.ability == ability; }) > 0)
    {
        Log(LogLevel::Info,
            std::format("{}'s ability {} failed ({}): not held on cooldown", Utf8(current->name), ability, why));
    }
    if (Classify(type) != CommandKind::Action || budget_.actions != 0 || budget_.refunded)
    {
        return false;
    }
    budget_.actions = 1;
    budget_.refunded = true;
    Log(LogLevel::Info,
        std::format("{}'s {} failed ({}): its action is given back", Utf8(current->name), Name(type), why));
    Emit(EventKind::ActionGivenBack, who, std::string{why}, 0, Name(type));
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
        view.actionsLeft = budget_.actions;
        view.movementLeft = budget_.MovementLeft();
        view.movementMax = budget_.movementMax;
    }
    for (const TurnOrder::Slot &slot : order_.Upcoming(12))
    {
        const Combatant &c = *slot.who;
        view.upcoming.push_back({c.id, c.name, c.tag, c.side, c.rank, c.health, c.down, slot.current, PlayerControls(c),
                                 slot.round, c.energy, c.mana});
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
