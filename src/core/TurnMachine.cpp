#include "core/TurnMachine.h"

#include "core/Facing.h"
#include "core/GivenBack.h"
#include "core/Text.h"
#include "core/TurnEnd.h"

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
    case EffectKind::FaceTarget:
        return "face target";
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
    // With a scan, those of the fight that are not the party's and still
    // stand: read whether or not the scan's own rules still find them
    // fighting, so that one out of combat or beyond the radius stays in the
    // fight while the engine's perception keeps it there (Merge; issue 29).
    if (needs.scan)
    {
        for (const Combatant &c : order_.All())
        {
            if (c.side != Side::Party && !c.down)
            {
                needs.inFight.push_back(c.id);
            }
        }
    }
    if (phase_ == Phase::Suspended)
    {
        return needs;
    }
    if (const Combatant *current = order_.Current())
    {
        needs.actor = current->id;
    }
    // Whose commands to look at for a turn to face that stalls: everyone of
    // the fight that stands, every frame (WatchTurnsToFace). Not under a
    // menu, where the world stands and the machine does nothing with what
    // it is shown.
    if (lastMode_ != Mode::Gui)
    {
        for (const Combatant &standing : order_.All())
        {
            if (!standing.down)
            {
                needs.turning.push_back(standing.id);
            }
        }
        // And whose running cooldowns to read, once each as they come into
        // the fight (TakeRunning; issue 57).
        for (const Combatant &fighter : order_.All())
        {
            if (!cooled_.contains(fighter.id))
            {
                needs.cooling.push_back(fighter.id);
            }
        }
    }
    // Every half second; fresh at every turn start: whether a creature is
    // stunned decides whether its turn is taken at all; and when asked for
    // (statusWanted_): as an action ends and before the turn goes on after
    // it, so that the cards and the state are what the action left.
    // Not under a menu, where the machine does nothing with what it is shown
    // (Step): a look that fell there was lost, and the next came half a
    // second after it. With two menus in a second, a creature stunned in its
    // own turn stood more than a second of the game's time before the
    // machine saw it (test_fights.cpp's long run with hostiles unaware, seed
    // 59, 2026-10-04). The look is taken as the menu goes.
    if (lastMode_ != Mode::Gui &&
        (nowMs - lastStatusNow_ >= 500 || phase_ == Phase::TurnStart || std::exchange(statusWanted_, false)))
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
            holds_.clear();
            phase_ = Phase::Idle;
        }
        turnedAway_.clear(); // gone with the world, as everyone is: nobody to tell
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
    // What came of the facings asked for with the last step (FaceTarget;
    // issue 40): said whatever the fight has come to since.
    for (const FacedLook &faced : obs.faced)
    {
        Faced(faced);
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
        // A look at everyone taken with the menu's first frame is not taken
        // up, and is asked for again for when the menu has gone
        // (WhatToObserve takes none under one).
        statusWanted_ = statusWanted_ || !obs.statuses.empty();
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
    for (const CoolingLook &look : obs.cooling)
    {
        TakeRunning(look, fx);
    }
    ApplyStatuses(obs.statuses, fx);
    FollowPace();
    WatchTurnsToFace(obs, fx);
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
            // The rest of this turn is the first of what the hold takes.
            if (const auto held = held_.find(current->id); held != held_.end())
            {
                const std::uint32_t turn = TurnMs();
                const std::uint32_t rest = turn - std::min(turn, budget_.spend.ClockElapsedMs());
                held->second.owedMs -= std::min(held->second.owedMs, rest);
                // (Taken by the hold, not banked: nothing of it rolls over,
                // as from any turn its actor did not end. core/TurnEnd.h.)
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
        EndTurn(fx, "its action pending");
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
    // A creature the game has made inactive has no turn (InactiveTurn; issue
    // 44). Made so while its turn is being played (a tentacle that goes under
    // on its own turn), that turn ends here, as this frame's look finds it:
    // nothing rolls over, and nothing is cleared, the engine having cleared
    // its commands as it went ("all commands will be cleared upon disabling":
    // script.ldf). Anyone's turn, a party member's and an unaware hostile's
    // beat too: nobody is there to order, or to be read. One that fell in the
    // same frame ends as the fallen do.
    if (const Combatant *current = order_.Current();
        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current))
    {
        EndInactive(*current, fx);
        EnforcePause(obs, fx);
        return fx;
    }
    // A creature that is not of the party and that the game does not have in
    // combat has no turn (outOfCombat_; issue 43). Found so while its turn is
    // being played, that turn ends here: asked for by the gate, where a
    // script's command came for it with its combat state off (Judge); or by
    // this frame's look at it, where the engine says on every count that it
    // has nobody to fight (NothingToFight), whether or not its script has
    // anything to tell it. Whatever of its own is still running is the
    // engine's to finish, and nothing is cleared. Not in the beat that skips
    // an unaware hostile's turn, which goes on to its own end (issue 29).
    //
    // Until 2026-10-05 such a turn went on by the turn's own rules: its
    // script's wait home read as its AI's wait, its walk home held behind a
    // turn that had ended (issue 38) or measured as the turn's move, and one
    // that only perceived nobody any more was waited on at its settling and
    // its turn ended "done", its time rolled over.
    if (const Combatant *current = order_.Current(); current != nullptr && InTurn() && !unaware_)
    {
        const bool asked = std::exchange(outOfCombat_, false);
        const bool looked = obs.actor && obs.actor->id == current->id && obs.actor->exists && !obs.actor->down;
        const bool seen = looked && NothingToFight(current->side, obs.actor->perception);
        if (asked || seen)
        {
            if (!asked)
            {
                Log(LogLevel::Info, std::format("{} is out of combat by the game's own word, and perceives nobody to "
                                                "fight: it has no turn, and is the game's",
                                                Utf8(current->name)));
            }
            EndTurn(fx, "out of combat");
            EnforcePause(obs, fx);
            return fx;
        }
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
    // A turn its AI plays has run past max_turn_ms: ended, whatever it is at.
    if (TurnTooLong(fx))
    {
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
    holds_.clear();
    cooled_.clear();
    absentSince_.clear();
    outside_.clear();
    afar_.clear();
    left_.clear();
    charges_.clear();
    rollover_.clear();
    grabbedMs_.clear();
    paceRates_.clear(); // what was said of a creature's Haste in the last fight is not this one's
    landing_.reset();
    chargeBegan_ = false;
    approach_ = false;
    roundAgedMs_ = 0;
    ownEffectSince_.reset();
    facedFirst_ = 0;  // the last fight's, said as it ended (End) or dropped with the world unsaid
    turning_.clear(); // and whoever stood at a turn to face in it
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
        // No fight after all, and the gate was shut while the look was taken:
        // whoever it turned away then, of the party or not, is told.
        LetGo(fx);
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
    // Whoever the gate turned away as combat began and is of the fight has
    // its turns to come, and is told at each (turnedAway_).
    std::erase_if(turnedAway_, [&](ObjectId id) { return order_.Find(id) != nullptr; });
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
    HoldTime(true, fx);
    // Over the hero, while standing; over another of the party where a fight
    // begins with the hero down (hostiles fighting again in a combat the
    // game has not ended), and not over a body the fight has left behind.
    Floaty(fx, PartysOwn(), L"Turn-based combat", kGold);
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
        End(fx, NoEnemyLeft());
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
        // Made inactive while the conversation played: there is no turn to
        // go on with, and nobody to tell that it does (issue 44; the long run
        // of random fights with creatures out of the world, seed 455).
        if (InactiveNow(*current))
        {
            EndInactive(*current, fx);
            return;
        }
        ResumeTurn(*current, fx);
        return;
    }
    if (LoseTurn(*current, fx))
    {
        return;
    }
    if (InactiveTurn(*current, fx))
    {
        return;
    }
    if (ChargingTurn(*current, fx))
    {
        return;
    }
    BeginTurn(*current, fx);
}

// Whether the game has made the creature whose turn it is inactive
// (ActorObservation::inactive), by this frame's look at it. The look is all
// that is asked: every turn begins on a frame that has looked at its creature
// (WhatToObserve names the order's current one, and the fight's first turn
// begins a frame after the fight does), and it is looked at every frame of
// its turn. A scan's word of everyone would have no reader.
bool TurnMachine::InactiveNow(const Combatant &who) const
{
    return seenActor_ && seenActor_->id == who.id && seenActor_->exists && seenActor_->inactive;
}

// A creature the game has made inactive has no turn (issue 44). As its turn
// comes it is passed over at once: no beat, since nobody is there to be seen;
// no bar, nothing charged and nothing rolled over; not told to choose, not
// waited on for an order, and nothing of its cleared or given (an action kept
// for this turn stays kept for the next it has). It stays in the order, and
// has its turns again when the game makes it active.
//
// The turn passes as a lost one does: the scripts are told of it, a cooldown
// that counts its user's turns counts it, and its creature's own clock moves a
// turn's seconds (EndTurn). That clock is what brings it back: the game's own
// timers on a creature run on it through a fight, and the Broodmother's
// tentacles come up by one (orz550_broodmother_h.nss,
// ORZ_Broodmother_Tentacle_Burrow: a surface event delayed 2.5 s, then
// WR_SetObjectActive(oTentacle, FALSE)). In the fight this was read from
// (the Dead Trenches, 2026-10-05) a tentacle's attack as it surfaced came
// five milliseconds after its turn's end had moved its clock.
//
// After LoseTurn: a hold on one that is out of the world takes its turns as
// on anyone, and is paid down as they pass.
//
// Until 2026-10-05 it was told its turn had come like anyone. With nothing
// queued it chose nothing, and the turn waited ai_think_ms (146 of the
// tentacles' 205 turns in that fight). With something queued, which the
// engine does not run for an inactive object, it read busy with no current
// command, and its turn stood out max_action_ms: "still at a invalid after
// 36000 ms: stopped", twelve seconds of the wall's time at three times the
// speed, eleven turns running for one tentacle on which a pin had landed
// after it went under.
bool TurnMachine::InactiveTurn(const Combatant &who, std::vector<Effect> &fx)
{
    if (!InactiveNow(who))
    {
        return false;
    }
    budget_ = Budget{};
    budget_.spend.SpendAllActions();
    Log(LogLevel::Info, std::format("round {}: {} is inactive by the game's own word, out of the world: it has no turn",
                                    order_.Round(), Utf8(who.name)));
    TellTurnStart(who, fx);
    clockSeenMs_ = 0;
    clockRuns_ = true;
    EndTurn(fx, "inactive");
    return true;
}

// A turn already begun, its creature made inactive in the middle of it (a
// tentacle that goes under on its own turn; one taken out of the world while
// a conversation played in it): ended where it stands. What its bar had left
// does not roll over, and nothing is cleared: the engine cleared its commands
// as it went.
void TurnMachine::EndInactive(const Combatant &who, std::vector<Effect> &fx)
{
    Log(LogLevel::Info, std::format("{} is inactive by the game's own word, out of the world: it has no turn, and what "
                                    "is left of this one is nobody's",
                                    Utf8(who.name)));
    EndTurn(fx, "inactive");
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
    // The turn begins: it is played to its end by what was chosen for its
    // creature as of now (PlayerControls; issue 46).
    budget_.begun = Budget::Begun{who.id, partyTactics_.Chosen(who.tag)};
    heldByEffect_ = false;
    unaware_ = false;
    budget_.spend = NewSpend(who);
    budget_.pace = BarPace(who);
    budget_.personMps = settings_.paceMps;
    if (const auto rolled = rollover_.find(who.id); rolled != rollover_.end())
    {
        budget_.rolledMs = rolled->second;
    }
    clockSeenMs_ = 0;
    clockRuns_ = true;
    std::string pending;
    if (const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
        charge != charges_.end() && charge->owedMs > 0)
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
    // (Whatever the gate said of this creature before its turn began, in the
    // frames the order already named it: the turn is judged as it begins.)
    outOfCombat_ = false;
    orderPending_ = false;
    turnSince_ = clock_;

    HoldAtTurnStart(who, fx);
    GrabAtTurnStart(who, fx);
    // The fight's speed, set as its first turn begins and again after a
    // conversation, before the camera moves: when an AI's turn had a speed
    // of its own, twice the pan to the hero, issued before the speed went
    // back to 1, never arrived (play tests 2 and 5, 2026-09-30). The camera
    // says whether a pan got there (src/game/Camera.cpp).
    SetSpeed(TurnSpeed(), fx);

    Log(LogLevel::Info, std::format("round {}: {}'s turn ({}, {}), a bar of {:.2f} s{}: {}", order_.Round(),
                                    Utf8(who.name), Name(who.side), PlayerControls(who) ? "yours" : "AI",
                                    static_cast<float>(budget_.spend.CapacityMs()) / 1000.0f, pending, BudgetLeft()));
    SayPace(who);
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
    else if (!UnawareNow(who))
    {
        // (Over one that has noticed nobody to fight, "Unaware" alone:
        // Handover.)
        Floaty(fx, who.id, who.name, who.side == Side::Enemy ? kRed : kGrey);
    }
    if (LandCast(who, fx))
    {
        return;
    }
    Handover(who, fx);
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

std::uint32_t TurnMachine::TurnMs() const
{
    return static_cast<std::uint32_t>(std::lround(settings_.secondsPerTurn * 1000.0f));
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

// A creature's pace as a bar walks at it, against a person's: its build's
// times its Haste and slows (core/Rules.h, Pace); what is no pace (nothing, a
// negative, not a number) is a person's.
float TurnMachine::BarPace(const Combatant &who)
{
    const float pace = Pace(who.speed, who.movement);
    return std::isfinite(pace) && pace > 0.0f ? pace : 1.0f;
}

// A turn's budget: a bar of seconds_per_turn and Combat Tactics' time and
// what the last turn rolled over, walked at the creature's pace (a person's
// times its own) after a free meter.
//
// Its own pace is its build's times its Haste and slows as the look at
// everyone this turn begins on found them (core/Rules.h, Pace; issue 49):
// every turn begins on a frame that has looked (WhatToObserve), so an effect
// put on or ended since its creature's last turn is in the meters of this
// one. The bar's pace then follows its creature's through the turn
// (FollowPace).
TurnBudget TurnMachine::NewSpend(const Combatant &who) const
{
    const TacticsTime tactics = TimeFromTactics(TacticsOf(who));
    const auto rolled = rollover_.find(who.id);
    const std::uint32_t rollover = rolled != rollover_.end() ? rolled->second : 0;
    return TurnBudget::Time(TurnTime(TurnMs(), tactics.bonusMs, rollover), settings_.paceMps * BarPace(who),
                            settings_.freeStepMeters);
}

namespace
{

// What was read of one kind of movement rate effect, for the log: how many,
// and each one's potency; one that is no rate said so.
std::string Potencies(std::span<const float> potencies)
{
    if (potencies.empty())
    {
        return "none";
    }
    std::string out = std::format("{} (", potencies.size());
    for (std::size_t i = 0; i < potencies.size(); ++i)
    {
        out += std::format("{}{:.2f}{}", i > 0 ? ", " : "", potencies[i],
                           IsMovementRate(potencies[i]) ? "" : " is no rate: left out");
    }
    return out + ")";
}

} // namespace

// What was read of a creature's Haste and slows, at debug: by the effects'
// two types, each one's potency, and what they come to. "None" is the game
// giving nothing: no effect of that type stands on it, or the game layer's
// read found none.
void TurnMachine::SayMovementEffects(const Combatant &who, std::string_view when) const
{
    if (!Logs(LogLevel::Debug))
    {
        return;
    }
    Log(LogLevel::Debug,
        std::format("{}'s movement rate effects {}: {} of type 9 (EFFECT_TYPE_MOVEMENT_RATE), {} of type 11 (its "
                    "_DEBUFF): {:.2f} of its build's pace",
                    Utf8(who.name), when, Potencies(who.movement.rates), Potencies(who.movement.debuffs),
                    MovementRate(who.movement)));
}

// What a creature's movement rate effects came to when its pace was last
// said (1 where nothing was on it), with what they come to now kept in its
// place.
float TurnMachine::RememberRate(ObjectId who, float rate)
{
    const auto last = paceRates_.find(who);
    const float before = last != paceRates_.end() ? last->second : 1.0f;
    if (rate == 1.0f)
    {
        paceRates_.erase(who);
    }
    else
    {
        paceRates_[who] = rate;
    }
    return before;
}

// The pace a turn's meters were made from, said as the turn begins (issue
// 49: a play test's log is read for it, and until 2026-10-05 no line said
// what a creature's Haste had come to, or that nothing had been read of it).
//
// At debug, every turn: what was found on the creature (SayMovementEffects).
//
// At info, once a turn, only while its effects change its pace, and once
// more as they stop: what it moves at and what that is made of, and what its
// effects came to at its last turn if that was something else. A creature
// nothing hastes or slows has no such line. (A change within a turn, with
// time, has been said as it came: FollowPace.)
void TurnMachine::SayPace(const Combatant &who)
{
    const float rate = MovementRate(who.movement);
    const float before = RememberRate(who.id, rate);
    SayMovementEffects(who, "as its turn begins");
    if (rate == 1.0f && before == 1.0f)
    {
        return;
    }
    Log(LogLevel::Info,
        std::format("{} moves at {:.2f} a person's pace this turn ({:.2f} m a second of its bar): {:.2f} from its "
                    "build, {:.2f} from haste or slows{}",
                    Utf8(who.name), Pace(who.speed, who.movement), budget_.spend.PaceMps(), who.speed, rate,
                    rate != before ? std::format(" ({:.2f} at its last turn)", before) : std::string{}));
}

// With a turn a bar of time, the bar walks at its creature's pace as it is
// now, not as it was when the turn began (issue 49; the user, 2026-10-05, of
// reading it again within the turn: "I think that would be more accurate").
// What is left to walk is the time left times the pace now. A Swift Salve
// drunk in the middle of a turn lengthens what can still be walked in that
// turn; Haste turned on by another takes in the hasted one's turn under way;
// a creature that walks into caltrops pays the slow for the rest of its walk.
//
// "Now" is the last look at everyone (CombatantStatus::movement), which is
// where a creature's Haste and slows are read: as each action ends and
// before the turn goes on (an AI told to choose again, the player waited on,
// each only on a look taken with that step), and every half second between,
// a walk under way included. So a slow that lands in the middle of a walk is
// paid from the look that finds it, half a second on at the latest, and what
// was walked until then was charged at the pace till then. No read of the
// engine was added for it: the look was there, for the cards.
//
// Nothing is given back and nothing is taken: the time spent stays spent,
// and meters paid for and not yet walked (an attack's walk, paid as it was
// let through) stay paid for. A walk judged to fit that no longer does is
// cut where its time runs out, as any walk past its bar is (TrackMovement).
// The bar's caption, the path's preview, what an order would cost and "Too
// far" all read this one pace, each from the turn's budget (View, Judge).
//
// Said once as it changes, at info, with what the turn has left; and what was
// read, at debug.
void TurnMachine::FollowPace()
{
    const Combatant *current = order_.Current();
    if (current == nullptr || !budget_.begun || budget_.begun->who != current->id)
    {
        return;
    }
    const float pace = BarPace(*current);
    if (pace == budget_.pace)
    {
        return;
    }
    const float before = std::exchange(budget_.pace, pace);
    budget_.spend.SetPace(budget_.personMps * pace);
    const float rate = MovementRate(current->movement);
    (void)RememberRate(current->id, rate);
    SayMovementEffects(*current, "as its pace changes in its turn");
    Log(LogLevel::Info,
        std::format("{} moves at {:.2f} a person's pace from now in this turn ({:.2f} m a second of its bar; {:.2f} "
                    "from its build, {:.2f} from haste or slows): it was {:.2f}, and it has {}",
                    Utf8(current->name), pace, budget_.spend.PaceMps(), current->speed, rate, before, BudgetLeft()));
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

// With time, an AI's wait costs the time it waits, and no more (issue 31;
// the user: "Shouldn't this cost the time equal to how long it waits? If
// it's less than the amount of time it has to take actions in a turn, it can
// still take more actions"). The game's AI waits a second or two between two
// things it does (CommandWait(AI_DO_NOTHING_DELAY), 2 s, or
// AI_DO_NOTHING_DELAY_QUICK, 1 s: ai_main_h_2.nss, ai_constants_h.nss) and
// chooses again as the wait ends. Taken for a pass, it ended the turn with
// the rest of the bar unspent: the high dragon above Haven ended 20 of 77
// turns so, 5 to 7 s left each time (the playthrough of issue 3).
//
// The wait is refused at the queue all the same: there is nothing of it to
// see, and the world does not run two seconds for a creature standing. Its
// length, the command's own as the game layer read it
// (CommandRequest::waitSeconds), comes off the bar as an action's time does,
// so that its creature's clock moves by it (its cooldowns, its effects); and
// with time left its AI is told to choose again as after any command of its
// turn (StepSettling), by the event the engine sends as a wait ends
// (EffectKind::PokeAi).
//
// Answers whether the wait was paid for. It is not, and is the pass it was,
// its turn ending: when it is longer than what is left; and when it has no
// length or its length was not read, since it would cost nothing and could
// be chosen for ever. Built 2026-10-04, not yet seen in play.
//
// Nothing counts a turn's waits (issue 38: until then they were counted
// with its commands against max_commands_per_turn, which is gone). Each
// takes its own time of the bar, a millisecond at the least, and the bar
// bounds them. And a wait that passes the turn is said once: it is the last
// thing of its creature's that is judged in that turn (Judge), where its
// script's next wait, a frame or a command later, was told "its turn ends"
// again each time.
bool TurnMachine::PayWait(const Combatant &who, const CommandRequest &request)
{
    constexpr float kLongest = 60.0f; // as an action's (ActionCostMs)
    const bool read = std::isfinite(request.waitSeconds) && request.waitSeconds > 0.0f;
    const auto cost =
        read ? static_cast<std::uint32_t>(std::lround(std::min(request.waitSeconds, kLongest) * 1000.0f)) : 0u;
    const std::uint32_t left = budget_.spend.CapacityLeftMs();
    if (cost == 0)
    {
        Log(LogLevel::Info, std::format("{}'s wait has no length that was read: it passes its turn", Utf8(who.name)));
        return false;
    }
    if (cost > left)
    {
        Log(LogLevel::Info,
            std::format("{}'s wait of {:.2f} s is longer than the {:.2f} s it has left: its turn ends", Utf8(who.name),
                        static_cast<float>(cost) / 1000.0f, static_cast<float>(left) / 1000.0f));
        return false;
    }
    (void)budget_.spend.SpendAction(cost); // it fits: nothing of it is owed
    Log(LogLevel::Info,
        std::format("{} waits {:.2f} s; {}", Utf8(who.name), static_cast<float>(cost) / 1000.0f, BudgetLeft()));
    MoveClock(pending_);
    // Its answer to being told to choose: nothing was queued, so nothing
    // will end and run its AI again, and thinking on, its turn would end
    // "chose nothing". The turn settles at once, and there it is told to
    // choose again, or ends, its bar spent. One that came as a command of
    // its turn ended (the engine's COMMAND_COMPLETE) settles as that does.
    if (phase_ == Phase::AiThinking)
    {
        phase_ = Phase::Settling;
        deadline_ = clock_;
        statusWanted_ = true;
    }
    return true;
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
    if (settings_.engineConjure)
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
    return settings_.engineConjure && request.type == CommandType::UseAbility && request.abilityType == kAbilitySpell &&
           !request.sustained && request.conjureAnim > 0 && request.conjureSeconds > 0.0f;
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
// the fight). Its time is given back, as a failed command's is. (Whether it
// cooled is the engine's to say, as of any command: OnCooldownSet.)
void TurnMachine::OverAtOnce(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr || !settings_.giveBackUnplayed || budget_.lastKind != CommandKind::Action ||
        !budget_.lastMeasured || budget_.lastCostMs < kLeastToldMs ||
        clock_ - budget_.lastClock >= budget_.lastCostMs / kOverAtOnce)
    {
        return;
    }
    // (OnCommandFailed says "Nothing happened" over the player's own.)
    (void)fx;
    (void)OnCommandFailed(current->id, budget_.lastType, "over at once: it did nothing");
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
    return budget_.lastCostMs > 0;
}

void TurnMachine::GiveActionBack()
{
    budget_.spend.GiveActionBack(budget_.lastCostMs);
    budget_.lastCostMs = 0;
}

std::string TurnMachine::BudgetLeft() const
{
    return std::format("{:.2f} s and {:.1f} m left", static_cast<float>(budget_.spend.CapacityLeftMs()) / 1000.0f,
                       budget_.MovementLeft());
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
    // Paid on this turn and landed on it (BeginTurn), or this whole turn is
    // spent on it.
    TurnBudget bar = NewSpend(who);
    if (charge->owedMs <= bar.CapacityLeftMs())
    {
        return false;
    }
    charge->owedMs -= bar.CapacityLeftMs();
    budget_ = Budget{};
    budget_.spend = bar;
    budget_.spend.SpendAllActions();
    Log(LogLevel::Info, std::format("round {}: {}'s turn is spent on its pending {} ({:.2f} s more)", order_.Round(),
                                    Utf8(who.name), Name(charge->type), static_cast<float>(charge->owedMs) / 1000.0f));
    Emit(EventKind::ActionPending, who.id, std::format("{:.1f} s more", static_cast<float>(charge->owedMs) / 1000.0f),
         charge->ability, Name(charge->type));
    TellTurnStart(who, fx);
    clockSeenMs_ = 0;
    clockRuns_ = true;
    Floaty(fx, who.id, L"Casting", kGold);
    EndTurn(fx, "charging");
    return true;
}

// The turn a kept action lands on, its time paid: the game layer gives its
// creature what it kept (ReleaseCast), which comes back through the gate and is
// let through for nothing (Judge); the creature's turn follows it.
bool TurnMachine::LandCast(const Combatant &who, std::vector<Effect> &fx)
{
    const auto charge = std::ranges::find(charges_, who.id, &Charge::caster);
    if (charge == charges_.end() || charge->turns > 0)
    {
        return false;
    }
    // Not to a creature at an effect's command: given then, it queues behind
    // the effect, and the machine took the effect for the action's own (a
    // command of its creature's had been let through) and ran its time with
    // it, at three times the patience. A bandit archer whose kept shot was
    // given so stood 26 s before "still at a attack after 36000 ms", its
    // target's clock aged with it (issue 11, the playthrough, 2026-10-03
    // 13:07: the gate said "its creature is at a command already" as the
    // shot was given, and 45 ms on "its action's own effect holds it"). It is
    // held by that effect, as any creature found so when its turn comes
    // (Handover), and what it kept waits: for the effect to let go, its time
    // taken of the bar as it runs (HoldRuns), or, the hold outlasting the
    // bar, for its next turn.
    const bool atAnEffect =
        seenActor_ && seenActor_->id == who.id && seenActor_->busy && seenActor_->current == CommandType::ExecuteEffect;
    if (atAnEffect)
    {
        Log(LogLevel::Info,
            std::format("{}'s pending {} waits: an effect holds its creature", Utf8(who.name), Name(charge->type)));
        return false;
    }
    landing_ = *charge;
    charges_.erase(charge);
    budget_.landing = true;
    Log(LogLevel::Info, std::format("{}'s pending {} lands: its time is paid", Utf8(who.name), Name(landing_->type)));
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
    Log(LogLevel::Info, std::format("{}'s pending {} is dropped: {}", name, Name(type), why));
    Emit(EventKind::ActionDropped, who, std::string{why}, ability, Name(type));
    fx.push_back(Effect::Of(EffectKind::DropCast, who));
    // Not as the fight ends: nothing interrupted it (play test 25); nor over
    // one that fell or is gone, where every attacker has an action pending
    // most of the time.
    // In the mod's own words: the game floats a white "Interrupted!" of its
    // own over a creature whose swing a blow breaks, three times in one
    // fight over ordinary bites, and the two could not be told apart (play
    // test 28).
    if (why == "held" || why == "struck" || why == kConjureCleared)
    {
        // In the gold of the mod's other words: small and white it was found
        // over the ogre's pale body only because the log said when to look
        // (play test 29), and in red, under the game's own red number for
        // the blow, it read worse (play test 30).
        Floaty(fx, who, spell ? L"Spell lost" : L"Action lost", kGold);
    }
}

// Why a kept action's target was not there for it, as far as the machine can
// say: fallen, or gone from the fight; or still standing in the fight by the
// machine's own word, which is one the game has made inactive, out of the
// world (issue 44). The game layer is who finds a target not there (dead,
// gone, or inactive: src/game/Bridge.cpp, NotThere) and turns or drops the
// action by the same rule either way; this is only the word the log has for
// it, so that the two can be told apart in play. In the Broodmother's fight
// (2026-10-05) a Pinning Shot kept for an archer was given at a tentacle
// that had gone under five seconds before, and what it put on the tentacle
// sat in its queue for eleven turns.
bool TurnMachine::KeptTargetStands() const
{
    const Combatant *target = landing_ ? order_.Find(landing_->target) : nullptr;
    return target != nullptr && !target->down;
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
        std::format("{}'s pending {} is dropped: its target is {}{}{}", c != nullptr ? Utf8(c->name) : std::string{"?"},
                    Name(landing_->type), KeptTargetStands() ? "out of the world" : "gone",
                    landing_->type == CommandType::Attack ? ", and no enemy is in its reach" : "",
                    paid > 0 ? std::format("; the {:.2f} s it owed given back", static_cast<float>(paid) / 1000.0f)
                             : std::string{}));
    Emit(EventKind::ActionDropped, who, "its target is gone", landing_->ability, Name(landing_->type));
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
    if (const auto grabbed = grabbedMs_.find(who.id); grabbed != grabbedMs_.end() && grabbed->second >= TurnMs())
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
    // The hold's own seconds: a turn's worth or more is this turn; less is
    // taken of the turn's bar as it begins (BeginTurn). Or, the condition on
    // the game's clock (condition_turns 0), every turn it still holds the
    // creature: it cannot act, and with effects' time held its command
    // cannot end within the turn.
    const std::uint32_t turn = TurnMs();
    const bool owes = held.kind == Condition::Disabled && held.owedMs >= turn;
    // On the game's clock (condition_turns 0, and its seconds not read
    // either): nothing is owed, and each turn it still holds is lost.
    const bool holds = (held.onClock || (settings_.conditionTurns == 0 && held.kind == Condition::None)) &&
                       held.seen == Condition::Disabled;
    if (!owes && !holds)
    {
        return false;
    }
    if (owes)
    {
        held.owedMs -= turn;
    }
    budget_ = Budget{};
    budget_.spend.SpendAllActions();
    Log(LogLevel::Info, std::format("round {}: {}'s turn is lost to its condition ({:.1f} s more)", order_.Round(),
                                    Utf8(who.name), static_cast<float>(held.owedMs) / 1000.0f));
    Emit(EventKind::TurnLost, who.id, {}, static_cast<std::int32_t>(held.owedMs));
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
    if (found == grabbedMs_.end())
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
    if (held.kind == Condition::Disabled && held.owedMs > 0)
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
    if (held.owedMs == 0 && held.seen != Condition::None)
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
// An ability of a creature's, by the engine's own key for its cooldown's
// record: the ability, and the tag of the item it comes from, if it is an
// item's.
Effect CooldownOf(ObjectId creature, std::int32_t ability, const std::wstring &item, std::int32_t seconds,
                  std::int32_t of)
{
    Effect e = Effect::Of(EffectKind::Cooldown, creature);
    e.a = ability;
    e.text = item;
    e.b = seconds;
    e.c = of;
    return e;
}

// For the log: "ability 10400", or "ability 500012 (item gen_im_qck_health_101)".
std::string Named(std::int32_t ability, const std::wstring &item)
{
    return item.empty() ? std::format("ability {}", ability) : std::format("ability {} (item {})", ability, Utf8(item));
}

std::uint32_t Milliseconds(float seconds)
{
    constexpr float kLongest = 86400.0f; // a day: more than any cooldown, and far from the 32 bits' end
    return std::isfinite(seconds)
               ? static_cast<std::uint32_t>(std::lround(std::clamp(seconds, 0.0f, kLongest) * 1000.0f))
               : 0u;
}
} // namespace

// What a held cooldown is set to in the engine. Left, the hold's own
// seconds, an hour, whatever its creature's clock has left of it: so that
// no hold comes back by waiting, however little is left. And the total as
// many times that as the cooldown is of what is left of it, since what the
// quickbar's sweep shows is the one over the other: with 8 s of 12 left the
// total is an hour and a half, and the sweep a third recovered. (Until
// 2026-10-08 it was the other way about, the total the hour and what was
// left its share of it: a cooldown with a hundredth of itself to wait was
// held for 36 s of the wall, which the looks that read every record then
// set again. Nothing reads them now.)
Effect TurnMachine::HoldIt(const Hold &h)
{
    constexpr std::uint64_t kMost = 2000000000; // seconds: what the effect's number holds
    const std::uint64_t total = static_cast<std::uint64_t>(kCooldownHoldSeconds) * h.ofMs / std::max(h.leftMs, 1u);
    return CooldownOf(h.creature, h.ability, h.item, kCooldownHoldSeconds,
                      static_cast<std::int32_t>(std::clamp<std::uint64_t>(total, kCooldownHoldSeconds, kMost)));
}

// Cooldowns in turns (dev/DESIGN.md, "Cooldowns in turns"; issue 57): the
// engine has just set a cooldown on a creature of the fight, and the game
// layer says so (DAOLib's dao::abilities::OnCooldownSet). It is taken onto
// its creature's own clock there and then: its seconds go into the book, and
// the record is held in the engine, where waiting in real time cannot bring
// it back, with the next step's effects. The engine says what cools, and
// when; the machine keeps no list of why, and looks for nothing. Every
// cooldown of the engine's and of the scripts' goes through the one function
// of the engine's that the callback is on, so the one rule covers whatever
// started it and whenever: the ability's own use, a mode the engine would
// not let stand, a mode switched off, a script's SetCooldown with no command
// at all, a trap, an item's, in its creature's turn or out of it, in the
// middle of its command or after it.
//
// It had two forms before this one, and each missed what it was not looking
// at.
// - Until 2026-10-06 the machine reckoned what the engine would start from
//   each command it let through (the ability table's seconds; a mode turned
//   off, a turn's worth; a mode turned on, nothing), and a cooldown started
//   for any reason not on that list ran on the wall. A Cultist Overseer
//   ordered a sustained spell that would not stand 47 times in one turn. The
//   user: "Shouldn't everything automatically keep its cooldown? If we do so
//   in a general way, we shouldn't miss anything."
// - Until 2026-10-08 it read every fighter's running cooldowns at each look
//   at everyone, and took what it found; not of a creature at a command of
//   its own turn, which was looked at when the command was over. Everything
//   else was right in play, and the Overseer looped as before, 83 orders in
//   a turn and not one record ever found: its spell's cooldown, the second
//   and a half the scripts set as an AI's cast of a mode starts, is over
//   before the command of some 1.7 s is, and its script orders again as each
//   command ends, so that its turn never came out of that command to be
//   looked at (seen in real time with the mod off, 2026-10-08). Looking
//   oftener would have found that one; a cooldown set by what is no command,
//   and over between two looks, it would not. The engine's own word as it
//   sets one is exact where a look is not.
//
// A cooldown the machine holds that the engine sets again (a script; the
// ability run again by one; a mode's, which the scripts set twice in one
// command: ability_core.nss, a second and a half as an AI's cast starts,
// then the ability table's own as its upkeep effect comes off,
// effect_upkeep_h.nss) is taken again at the engine's new word, the book's
// seconds replaced. One the engine sets to nothing is ended by the game (a
// script's reset): the book lets go of it, and whatever the machine had
// still to say of it is not said.
void TurnMachine::OnCooldownSet(ObjectId creature, std::int32_t ability, std::wstring_view item, float seconds)
{
    const Combatant *who = phase_ != Phase::Idle ? order_.Find(creature) : nullptr;
    if (who == nullptr)
    {
        // Of no fight, or gone from this one's order: the engine's, on the
        // wall as ever. A hold the book still had of it, from while its
        // creature was of the fight, the engine has just written over, and
        // is no hold any more.
        std::erase_if(holds_,
                      [&](const Hold &h) { return h.creature == creature && h.ability == ability && h.item == item; });
        return;
    }
    TakeCooldown(*who, RunningCooldown{ability, std::wstring{item}, seconds, seconds}, "set by the engine now",
                 pending_);
}

// And what was cooling on a creature before the fight held it, read once as
// it comes into the fight (Needs::cooling). A record the machine holds
// already was set by the engine since its creature came in, and taken then.
// One with an hour or more for its total is no cooldown of the game's: a
// hold no fight wrote back (the world dropped under one), with nothing to
// say what it had left; ended, and said.
//
// It is the one read there is. Until 2026-10-09 a creature's were read once
// more after each command of its turn, for an item's ability's, whose
// cooldown the exe did not plainly say would be told; play showed a drink
// calling back as any other does (Wynne's lyrium potion, `(set by the engine
// now)`, that day), and the read went.
void TurnMachine::TakeRunning(const CoolingLook &look, std::vector<Effect> &fx)
{
    cooled_.insert(look.id);
    const Combatant *who = order_.Find(look.id);
    if (who == nullptr)
    {
        return;
    }
    for (const RunningCooldown &r : look.running)
    {
        const bool held = std::ranges::any_of(
            holds_, [&](const Hold &h) { return h.creature == look.id && h.ability == r.ability && h.item == r.item; });
        if (held)
        {
            continue;
        }
        if (r.total >= static_cast<float>(kCooldownHoldSeconds))
        {
            Log(LogLevel::Warn,
                std::format("{}'s {} is held in the engine at the machine's own scale ({:.0f} s left of {:.0f}), and "
                            "the machine holds no such cooldown: left by a fight that did not end (the world "
                            "dropped, a save made in one), what it had left is not known, and it is ended: ready",
                            Utf8(who->name), Named(r.ability, r.item), r.remaining, r.total));
            fx.push_back(CooldownOf(look.id, r.ability, r.item, 0, 0));
            continue;
        }
        if (r.remaining > 0.0f)
        {
            TakeCooldown(*who, r, "found running on it", fx);
        }
    }
}

// One cooldown of the engine's, onto its creature's own clock: what the
// engine has left of it, of what it was set to. Never for more than that
// total: the engine sets the two as one and only takes from what is left, so
// more left than the total is the machine's own writing met by a machine
// that does not know it (a hold no fight wrote back, on a record that kept
// its own total: an item's did, in play, 2026-10-06), taken at the total and
// said at warning. Nothing left ends it.
void TurnMachine::TakeCooldown(const Combatant &who, const RunningCooldown &r, std::string_view when,
                               std::vector<Effect> &fx)
{
    const auto held = std::ranges::find_if(
        holds_, [&](const Hold &h) { return h.creature == who.id && h.ability == r.ability && h.item == r.item; });
    const bool anew = held != holds_.end();
    const std::uint32_t totalMs = Milliseconds(r.total);
    std::uint32_t leftMs = Milliseconds(r.remaining);
    if (leftMs == 0)
    {
        if (anew)
        {
            Log(LogLevel::Info,
                std::format("{}'s {} was held with {:.2f} s of its own clock to wait, and the game has ended its "
                            "cooldown: let go, it is ready",
                            Utf8(who.name), Named(r.ability, r.item), static_cast<float>(held->leftMs) / 1000.0f));
            Emit(EventKind::CooldownReleased, who.id, "ended by the game", r.ability, {}, Utf8(r.item));
            holds_.erase(held);
            std::erase_if(fx, [&](const Effect &e) {
                return e.kind == EffectKind::Cooldown && e.target == who.id && e.a == r.ability && e.text == r.item;
            });
        }
        return;
    }
    if (totalMs > 0 && leftMs > totalMs)
    {
        Log(LogLevel::Warn,
            std::format("{}'s {} has {:.2f} s left in the engine of the {:.2f} s it was set to, which the engine "
                        "does not do: a hold of the machine's that it does not know for its own; taken at what "
                        "it was set to, and no more",
                        Utf8(who.name), Named(r.ability, r.item), r.remaining, r.total));
        leftMs = totalMs;
    }
    Hold taken{who.id, r.ability, r.item, leftMs, std::max(leftMs, totalMs)};
    Log(LogLevel::Info,
        std::format("{}'s {} is cooling in the engine, {:.2f} s left of {:.2f}{}: taken onto its own clock, and "
                    "held ({})",
                    Utf8(who.name), Named(r.ability, r.item), r.remaining, r.total,
                    anew ? std::format(", set anew by the game over the {:.2f} s the machine held",
                                       static_cast<float>(held->leftMs) / 1000.0f)
                         : std::string{},
                    when));
    fx.push_back(HoldIt(taken));
    Emit(EventKind::CooldownHeld, who.id, std::format("{:.1f} s", static_cast<float>(leftMs) / 1000.0f), r.ability, {},
         Utf8(r.item));
    if (anew)
    {
        *held = std::move(taken);
    }
    else
    {
        holds_.push_back(std::move(taken));
    }
}

// A turn of its user passes (taken or lost): what has waited its turns is
// ready now, before the creature is told its turn has come.
// A turn's beginning, for what counts turns: the scripts are told. A turn
// that was put off (Delay) begins a second time in its round, or is then lost
// to a hold: told once, the first time. (Cooldowns count on their user's own
// clock: MoveClock.)
void TurnMachine::TellTurnStart(const Combatant &who, std::vector<Effect> &fx)
{
    if (order_.Delayed(who.id))
    {
        return;
    }
    Signal(fx, events::kTurnStart, who.id, order_.Round(), static_cast<std::int32_t>(who.side));
}

// The current creature's clock moved on by its share of the bar spent since
// the last look, or (a turn over) the rest of the way to a turn's seconds.
void TurnMachine::MoveClock(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr)
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
        if (h.creature != who)
        {
            continue;
        }
        const std::uint32_t before = h.leftMs;
        h.leftMs -= std::min(h.leftMs, clockMs);
        if (h.leftMs == 0)
        {
            fx.push_back(CooldownOf(h.creature, h.ability, h.item, 0, 0));
            const Combatant *c = order_.Find(who);
            Log(LogLevel::Info, std::format("{}'s {} is ready again: its cooldown has run on its own clock",
                                            c != nullptr ? Utf8(c->name) : "?", Named(h.ability, h.item)));
            Emit(EventKind::CooldownReleased, who, {}, h.ability, {}, Utf8(h.item));
        }
        else if (static_cast<std::uint64_t>(before) * kSweepSteps / h.ofMs !=
                 static_cast<std::uint64_t>(h.leftMs) * kSweepSteps / h.ofMs)
        {
            fx.push_back(HoldIt(h));
        }
    }
    std::erase_if(holds_, [&](const Hold &h) { return h.creature == who && h.leftMs == 0; });
}

// The fight is over: what still waits goes back to seconds, what it has
// left. The machine held them, so it is the machine's to write: left as
// they are, each would wait out the hold's hour on the wall.
void TurnMachine::ReleaseCooldowns(std::vector<Effect> &fx)
{
    for (const Hold &h : holds_)
    {
        fx.push_back(CooldownOf(h.creature, h.ability, h.item, static_cast<std::int32_t>((h.leftMs + 999) / 1000),
                                static_cast<std::int32_t>((h.ofMs + 999) / 1000)));
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
    Log(LogLevel::Info, held ? std::format("effects' time held: {:.0f} s of it a round{}", settings_.secondsPerTurn,
                                           OwnClocks() ? ", each fighter's on its own clock" : "")
                             : std::string{"effects' time runs again"});
    if (held)
    {
        PutOnOwnClocks(fx);
    }
}

// What holds a creature at an effect's command costs the time the engine
// holds it, whatever the effect: a dragon's knockback, a grab, the getting up
// from a stun the machine ended (issue 30; the user, of reading a knockback
// as one more kind of hold: "we should not make a special case for it, but
// fix the way effects cost time by reading the actual amount of time they
// cost"). The hold runs on its creature's turn, the world running, and the
// time it runs is the turn's: that share of its bar by the turn's ratio, as a
// condition's seconds are taken (HoldAtTurnStart), and the creature's own
// clock moves with it, so that an effect which lasts while its time lasts
// wears off as its creature pays for it. Let go, the creature has the rest of
// its turn (HoldLetGo), and what it kept is given then. The turn is lost only
// once the hold has outlasted the bar: answered true.
//
// Until then a creature found so was given two seconds (kEffectGraceMs) and
// then lost the turn whole, bar and all, its clock not moved meanwhile.
// Against the high dragon a knockback of 1.2 s cost the party 37 of 176 turns
// in one fight and 19 in 25 rounds of another. And one getting up from a hold
// the machine had ended was let be for five seconds (kGetUpMs), its rise
// costing nothing: a knockdown's time is its fall and its getting up.
//
// The time is counted as it runs, not taken from the engine's count ahead
// (ActorObservation::heldSeconds), which is said beside it and decides
// nothing: no turn is lost on it. The engine times only part of a hold
// ahead. A knockback's command has one sub-action, timed to the effect's
// 1.2 s and found spent as the turn begins; the fall and the rise that then
// hold the fighter, 0.6 to 3.2 s, are on no list (DAOLib's reading of the
// fight below, its 5e0108a), so the count is "at least" nothing.
//
// Seen in play, the high dragon above Haven again (2026-10-04, 63 rounds):
// 3 of the party's 141 turns lost to a hold, where 19 had been in 25 rounds;
// of 50 holds 44 let go with time to spare, 3 outlasted the bar and 3 were
// cut short by the fight's end.
//
// An AI's wait (PayWait, issue 31) does not meet a hold's time: the gate
// refuses a held creature's with its other orders, before the wait's rule,
// and one paid before its creature was thrown is time before the hold. A
// wait is no command of its own let through, so one that has only waited
// and is then thrown is held here, not taken for one at its action's own
// effect.
bool TurnMachine::HoldRuns(const ActorObservation &actor, std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr)
    {
        return false;
    }
    const std::string effects = actor.effects.empty() ? std::string{} : " (" + actor.effects + ")";
    if (!budget_.heldSeen)
    {
        budget_.heldSeen = clock_;
        budget_.holdRanMs = 0;
        budget_.holdBarMs = 0;
        // The engine's count, as far as it goes: the whole of it where every
        // sub-action left is timed; "at least" where part of what holds the
        // creature is not timed ahead; nothing to say where none of it is.
        const std::string count =
            actor.heldKnown            ? std::format("{:.2f} s more by the engine's count", actor.heldSeconds)
            : actor.heldSeconds > 0.0f ? std::format("at least {:.2f} s more by the engine's count", actor.heldSeconds)
                                       : std::string{"how long the engine does not say ahead"};
        Log(LogLevel::Info,
            std::format("{} is held by an effect's command, {}: the time it holds is taken of its bar as it runs "
                        "({}){}",
                        Utf8(current->name), count, BudgetLeft(), effects));
    }
    ChargeHold(fx);
    if (budget_.spend.CapacityLeftMs() > 0)
    {
        return false;
    }
    Log(LogLevel::Info,
        std::format("{} is held by an effect's command past its bar ({:.2f} s held this turn): its turn is lost{}",
                    Utf8(current->name), static_cast<float>(budget_.heldMs) / 1000.0f, effects));
    Floaty(fx, current->id, current->name + L" loses the turn", current->side == Side::Party ? kGold : kGrey);
    EndTurn(fx, "held by an effect");
    return true;
}

// The time the hold has run since it was last charged, taken of the turn.
void TurnMachine::ChargeHold(std::vector<Effect> &fx)
{
    if (!budget_.heldSeen)
    {
        return;
    }
    const std::uint32_t ran = clock_ - std::exchange(*budget_.heldSeen, clock_);
    budget_.heldMs += ran;
    budget_.holdRanMs += ran;
    // Of its own clock, and with it that share of the bar: its cooldowns
    // move with it, and with own clocks its effects (MoveClock).
    const std::uint32_t before = budget_.spend.CapacityLeftMs();
    (void)budget_.spend.HoldClock(ran);
    budget_.holdBarMs += before - budget_.spend.CapacityLeftMs();
    MoveClock(fx);
    if (!timeHeld_ || OwnClocks())
    {
        return;
    }
    // Where effects are on the one clock the fight holds, effects' time runs
    // with the hold, a step at a time and off the round's worth, as it does
    // with an action's own effect (roundAgedMs_): held still, an effect that
    // keeps its creature while it lasts could not wear off within the turn,
    // and every such hold cost the turn whole.
    budget_.heldUnagedMs += ran;
    if (budget_.heldUnagedMs >= kOwnEffectStepMs)
    {
        Effect age = Effect::Of(EffectKind::AgeTime);
        age.b = static_cast<std::int32_t>(std::exchange(budget_.heldUnagedMs, 0));
        roundAgedMs_ += static_cast<std::uint32_t>(age.b);
        fx.push_back(age);
        agedAtNow_ = lastNow_;
    }
}

// The effect's command is over, or another has taken its place: the last of
// the hold is charged, and what it took is said. The turn is its creature's
// with what is left.
void TurnMachine::HoldLetGo(const Combatant &who, std::vector<Effect> &fx)
{
    if (!budget_.heldSeen)
    {
        return; // over before a frame of it was seen: nothing ran
    }
    ChargeHold(fx);
    budget_.heldSeen.reset();
    Log(LogLevel::Info, std::format("{} is free of the effect's command after {:.2f} s, {:.2f} s of its bar: {}",
                                    Utf8(who.name), static_cast<float>(budget_.holdRanMs) / 1000.0f,
                                    static_cast<float>(budget_.holdBarMs) / 1000.0f, BudgetLeft()));
}

// At its own mode's command (issue 51; the header has the rule): at an
// effect's command, and the game layer has said it is a mode of its own that
// keeps it there (ActorObservation::ownMode; core/Rules.h,
// OwnModeAtItsCommand).
bool TurnMachine::AtItsOwnMode(const ActorObservation &actor)
{
    return actor.exists && !actor.down && actor.busy && actor.current == CommandType::ExecuteEffect &&
           actor.ownMode != kNoMode;
}

std::int32_t TurnMachine::OwnModeSeen() const
{
    const Combatant *current = order_.Current();
    const bool seen = current != nullptr && seenActor_ && seenActor_->id == current->id && AtItsOwnMode(*seenActor_);
    return seen ? seenActor_->ownMode : kNoMode;
}

// Said once a turn (and again for another mode): nothing on the screen says
// yet why a singer's turn waits with nothing running.
void TurnMachine::SayOwnMode(const Combatant &who, const ActorObservation &actor)
{
    if (std::exchange(budget_.ownModeSaid, actor.ownMode) == actor.ownMode)
    {
        return;
    }
    Log(LogLevel::Info,
        std::format("{} is at its own mode's command (ability {}), not held: its turn waits for the player, its bar "
                    "not running ({}); End goes on with the mode, the turn spent on it, and the mode turned off is "
                    "free{}",
                    Utf8(who.name), actor.ownMode, BudgetLeft(),
                    actor.effects.empty() ? std::string{} : " (" + actor.effects + ")"));
}

// A turn spent on its creature's own mode: the mode goes on, and the turn's
// time is the mode's, as the game spends a bard's time on its song. The bar
// is spent, so nothing is left to roll over, and the turn's end moves its
// creature's own clock the rest of a turn's seconds as any turn's does
// (EndTurn): its cooldowns, the mode's upkeep. Nothing is cleared: the mode's
// command is all it is at, and the engine's to keep.
void TurnMachine::SpentOnItsMode(const Combatant &who, std::int32_t mode, std::vector<Effect> &fx)
{
    Log(LogLevel::Info,
        std::format("{}'s turn is spent on its own mode (ability {}), which keeps it at its command: {}",
                    Utf8(who.name), mode,
                    PlayerControls(who) ? "End was pressed, and the mode goes on"
                                        : "its tactics have it on, and its AI is not told to choose"));
    budget_.spend.SpendAllActions();
    EndTurn(fx, "keeping up its mode");
}

void TurnMachine::EndedByThePlayer(const Observation &obs, std::vector<Effect> &fx)
{
    if (const Combatant *current = order_.Current(); current != nullptr && obs.actor && AtItsOwnMode(*obs.actor))
    {
        SpentOnItsMode(*current, obs.actor->ownMode, fx);
        return;
    }
    EndTurn(fx, "ended by the player");
}

// A creature's effects, its regeneration and the scripts' timers on it run on
// its own clock (Settings::ownClocks).
bool TurnMachine::OwnClocks() const
{
    return settings_.ownClocks;
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
    // The round's worth, a turn's seconds, less what its actions' own
    // effects took of it. (Effects' time is held from the fight's start to
    // its end, StartFight and End: until issue 50 a setting could turn the
    // hold on in the middle of a fight, and a round that began unheld was
    // held from here and not aged.)
    const auto worth = static_cast<std::uint32_t>(settings_.secondsPerTurn * 1000.0f);
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
    const auto left = static_cast<std::int32_t>(budget_.spend.CapacityLeftMs());
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
    // (AI_DetermineCombatRound_Partial, ai_main_h_2.nss). The hero is the
    // one selected then, while standing: a fallen hero was selected all the
    // same until 2026-10-04 (issue 42's fault, read in the code and not seen
    // in play), where the game itself hands the player's control to another
    // when its creature falls. With the hero down it is another of the party
    // standing; a companion standing alone stays the one controlled, with
    // what its AI then does.
    if (const ObjectId other = who.side == Side::Party ? PartysOwn(who.id) : kNoObject; other != kNoObject)
    {
        fx.push_back(Effect::Of(EffectKind::Select, other));
    }
    // (Not one that has noticed nobody to fight, below: its turn is skipped
    // after its beat whatever holds it, and a hold is not stood out for a
    // turn that is skipped anyway. Held first, as it was while a hold was
    // given two seconds, it would stand out its bar and then its beat.)
    // (One at its own mode's command comes this way too, and is not told to
    // choose either: with the next step it is found at its mode and not
    // held, nothing charged, and its turn is spent on the mode as it settles.
    // StepExecuting, AtItsOwnMode; issue 51.)
    if (!UnawareNow(who) && seenActor_ && seenActor_->id == who.id && seenActor_->busy &&
        seenActor_->current == CommandType::ExecuteEffect)
    {
        heldByEffect_ = true;
        phase_ = Phase::Executing;
        executingSince_ = clock_;
        idleTicks_ = 0;
        return;
    }
    // A hostile that has noticed nobody to fight is not told its turn has
    // come: its AI chooses among the hostiles it perceives, and has nothing
    // to choose. "Unaware" stands over it for a beat with the world running,
    // and its turn is skipped (StepThinking). Until 2026-10-04 it was told
    // like anyone, waited on for ai_think_ms, and its turn ended "chose
    // nothing": 54 such turns in one fight with three skeletons that never
    // noticed the party (issue 29). It is not woken either: the user, "If
    // they don't notice the party that gives the party the initiative to
    // close in and attack."
    if (UnawareNow(who))
    {
        unaware_ = true;
        Log(LogLevel::Info,
            std::format("{} has not noticed the party (not in combat, and it perceives nobody to fight): unaware, its "
                        "AI is not told, and its turn is skipped in {:.1f} s",
                        Utf8(who.name), static_cast<float>(kUnawareMs) / 1000.0f));
        Floaty(fx, who.id, L"Unaware", kGrey);
        deadline_ = clock_ + kUnawareMs;
        phase_ = Phase::AiThinking;
        return;
    }
    unaware_ = false;
    // Nor is anyone else told that has no turn (outOfCombat_; issue 43): an
    // ally the game has out of combat, nobody hostile in its perception. Its
    // turn ends with the next step, with no beat: nothing is to be read over
    // an ally that stands by. Until 2026-10-05 it was told, its AI had
    // nothing to choose out of combat, and its turn waited ai_think_ms and
    // ended "chose nothing".
    if (NothingToFightNow(who))
    {
        Log(LogLevel::Info, std::format("{} is out of combat by the game's own word, and perceives nobody to fight: "
                                        "it has no turn, and is the game's",
                                        Utf8(who.name)));
        outOfCombat_ = true;
        deadline_ = clock_;
        phase_ = Phase::AiThinking;
        return;
    }
    fx.push_back(Effect::Of(EffectKind::PokeAi, who.id));
    deadline_ = clock_ + static_cast<std::uint32_t>(settings_.aiThinkMs);
    phase_ = Phase::AiThinking;
}

// Whether the creature whose turn it is has noticed nobody to fight
// (core/Rules.h, Unaware), by the freshest word there is: this frame's look
// at it, where the frame looked at it (the game layer reads the acting
// creature's perception every frame), else what the last scan read.
bool TurnMachine::UnawareNow(const Combatant &who) const
{
    const bool looked = seenActor_ && seenActor_->id == who.id && seenActor_->exists;
    return Unaware(who.side, looked ? seenActor_->perception : who.perception);
}

// Whether this frame's look at the creature whose turn it is finds one that
// has no turn: not of the party, out of the game's combat and perceiving
// nobody to fight (core/Rules.h, NothingToFight). By the look alone: a turn
// is not taken from a creature on a scan's older word, and the frame a turn
// begins in has looked at it.
bool TurnMachine::NothingToFightNow(const Combatant &who) const
{
    return seenActor_ && seenActor_->id == who.id && seenActor_->exists &&
           NothingToFight(who.side, seenActor_->perception);
}

bool TurnMachine::InTurn() const
{
    return phase_ == Phase::AwaitingOrder || phase_ == Phase::Dispatching || phase_ == Phase::AiThinking ||
           phase_ == Phase::Executing || phase_ == Phase::Settling;
}

// Why a fight with no enemy standing is over: they fell; or the party ran
// from them, or was lost to them, and they left the fight alive (issue 29's
// leaving), those that did not having fallen.
std::string TurnMachine::NoEnemyLeft() const
{
    const auto left = std::ranges::count_if(left_, [](const auto &gone) { return gone.second == Side::Enemy; });
    if (left == 0)
    {
        return "no enemy left standing";
    }
    return std::format("no enemy left in the fight, {} having left it alive", left);
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
            End(fx, NoEnemyLeft());
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
        EndedByThePlayer(obs, fx);
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
    // At its own mode's command (AtItsOwnMode; issue 51): the turn waits for
    // the player as on any turn, for as long as the player likes, and
    // nothing of its bar is taken meanwhile. Until 2026-10-05 this was "busy
    // with a execute effect the gate did not see", a hold, and the singer's
    // turn was lost four seconds of the game's time later, turn after turn:
    // ninety of Lyna's in a row in one fight.
    if (obs.actor && AtItsOwnMode(*obs.actor))
    {
        if (const Combatant *current = order_.Current())
        {
            SayOwnMode(*current, *obs.actor);
        }
        return;
    }
    if (obs.actor && obs.actor->busy)
    {
        // Something the gate did not pass: a recovery from a knockdown, say.
        // It runs, and the player gets the turn back after.
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug,
                std::format("busy with a {} the gate did not see; letting it run{}", Name(obs.actor->current),
                            obs.actor->effects.empty() ? std::string{} : ": " + obs.actor->effects));
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
        EndedByThePlayer(obs, fx);
        return;
    }
    // (Its own mode's command is not the order's: the turn goes on waiting
    // for what the order queues, AtItsOwnMode.)
    if (obs.actor && obs.actor->busy && !AtItsOwnMode(*obs.actor))
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
    // An unaware hostile's turn (Handover): nothing is waited for but the
    // beat "Unaware" is read in, and then its turn is skipped. Its AI was
    // not told, so a wait of its script's is no choice of this turn's (the
    // gate refuses it, and it neither passes the turn nor is paid for), and
    // what the engine puts on it meanwhile is not waited out. It acts from
    // the moment it notices (struck, or the party come into its perception
    // as the world runs): its turn is then its AI's, told at once.
    if (unaware_)
    {
        const Combatant *current = order_.Current();
        if (current != nullptr && !UnawareNow(*current))
        {
            // Told only on a look at everyone taken with this step, as an AI
            // is told to choose again in mid-turn (StepSettling): on a look
            // up to half a second old, one stunned in its beat was told all
            // the same (test_fights.cpp's long run with hostiles unaware,
            // seed 135). Stunned, its turn ends above with the next step.
            if (lastStatusNow_ != lastNow_)
            {
                statusWanted_ = true;
                return;
            }
            unaware_ = false;
            Log(LogLevel::Info,
                std::format("{} has noticed someone to fight: its turn is its AI's", Utf8(current->name)));
            Handover(*current, fx);
            return;
        }
        if (clock_ >= deadline_)
        {
            EndTurn(fx, "unaware");
        }
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
    // The step its command is at, as the engine times it: looked at with
    // every step while it acts, its command over or not, so that the look
    // after a command's end sees no step and nothing is taken to advance
    // from it into the next (issue 28).
    const std::uint32_t notCounted = StepTimeNotCounted(actor);
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
    // At its own mode's command (AtItsOwnMode; issue 51): a mode turned on
    // in its own turn, say, whose command the engine then keeps it at. That
    // command is not an action the turn waits to see the end of: taken for
    // its action's own effect, below, a song turned on by the player held
    // the turn at "executing" until the action's limit, `still at a execute
    // effect after 108000 ms: stopped`, twelve seconds of the wall's time at
    // three times the speed with the fight standing still (2026-10-05). Nor
    // is it a hold, though a creature found at an effect's command as its
    // turn came, or while its AI thought, is taken for held until this look
    // (Handover, StepThinking): nothing was charged for it. The turn settles
    // from here as after any command of its own, and there a turn its
    // tactics play is spent on the mode, with the next step, and a turn the
    // player plays waits for the next order (StepSettling, StepAwaiting).
    // End pressed already, the player's is spent on the mode here and now.
    if (AtItsOwnMode(actor))
    {
        ownEffectSince_.reset();
        stoodAt_.reset();
        const Combatant *current = order_.Current();
        if (current == nullptr)
        {
            return;
        }
        // (Held until now by another's effect, and its own mode on: what the
        // hold ran is the hold's.)
        if (std::exchange(heldByEffect_, false))
        {
            HoldLetGo(*current, fx);
        }
        if (endTurnRequested_)
        {
            SpentOnItsMode(*current, actor.ownMode, fx);
            return;
        }
        deadline_ = clock_ + static_cast<std::uint32_t>(settings_.settleMs);
        phase_ = Phase::Settling;
        statusWanted_ = true;
        return;
    }
    // Its own action's effect keeps it there (roundAgedMs_): that effect's
    // time runs with the action. Not one it did not choose, which is a hold
    // and costs its own time (HoldRuns), nor a stun, which ends the turn.
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
            if (const Combatant *current = order_.Current(); current != nullptr)
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
        // Idle one frame is not yet let go: a frame can fall between one
        // command the engine queues and the next (a fall, say, and then the
        // getting up, if they are two), and what its creature kept, given
        // into that frame, would queue behind the second.
        if (!actor.busy && ++idleTicks_ < 2)
        {
            return;
        }
        heldByEffect_ = false; // let go: the turn is its own again, with what the hold left of it
        const Combatant *current = order_.Current();
        if (current != nullptr)
        {
            HoldLetGo(*current, fx);
        }
        // What it kept for this turn, and was not given while the effect
        // held it (LandCast), is given now; its turn follows it.
        if (!actor.busy && current != nullptr && LandCast(*current, fx))
        {
            return;
        }
        // (With time, not one whose bar the hold spent to the last as it let
        // go: it has nothing to choose with, and its turn ends as it settles.)
        if (!actor.busy && current != nullptr && !PlayerControls(*current) && budget_.spend.TimeLeft())
        {
            // Its AI was not told, or told while it could not choose: handed
            // over with what the hold left, the hero selected first for a
            // companion. Poked while still the one selected (party control
            // turned to tactics while it was held), the dog's AI chose nothing
            // (the fourth play test).
            Handover(*current, fx);
            return;
        }
    }
    // Standing at a walk it does not take (kStoodMs): stopped. The player's
    // turn goes on, and an action that waited behind the walk and never
    // began is given back; an AI's turn ends as its command does.
    // An attack too, while its command is still at its approach
    // (ActorObservation::approaching, the engine's own word): it is not
    // swinging, it cannot get there (others stand in the way, or its place
    // at the target is past a doorway), and the engine gives up only after
    // ten seconds of the world's time. A spider stood five seconds of a
    // fight at twice the speed so (play test 29); stopped, its action is
    // given back and its AI chooses again, once. One at its blow is the
    // engine's to end, however long it stands: a watch for "a swing that
    // does not begin" stopped fourteen swings that were landing, in one
    // fight (issue 11).
    const CommandKind at = Classify(actor.current);
    const bool attacking = actor.current == CommandType::Attack && budget_.lastType == CommandType::Attack;
    const bool short_ = attacking && actor.approaching;
    if (actor.busy && !heldByEffect_ && (at == CommandKind::Move || at == CommandKind::Free || short_))
    {
        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs);
        if (!stoodAt_ || Distance(*stoodAt_, actor.position) > kStoodStill)
        {
            stoodAt_ = actor.position;
            stoodSince_ = clock_;
        }
        else if (clock_ - stoodSince_ >= limit)
        {
            const Combatant *current = order_.Current();
            const bool waited = budget_.lastKind == CommandKind::Action && ActionToGiveBack() &&
                                (short_ || budget_.lastType != actor.current);
            // With what it was judged by, for an attack.
            std::string where;
            std::string why;
            if (short_)
            {
                where = " (its command still at its approach)";
                why = "; it cannot get to its target";
            }
            if (waited)
            {
                why += short_ ? ", and its action is given back" : "; the action waiting behind it is given back";
            }
            Log(LogLevel::Info, std::format("{} stood {} ms at a {} without a step{}: stopped{}",
                                            current != nullptr ? Utf8(current->name) : std::string{"?"},
                                            clock_ - stoodSince_, Name(actor.current), where, why));
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
        // End pressed while its creature is at a command: the turn ends now,
        // not when the command does. A command of its own (an attack, a
        // talent, a walk) is stopped, its commands cleared, so that nothing
        // of the turn is left to arrive in another's (issue 51; the user,
        // 2026-10-05: "timeout should end any actions that are still being
        // taken when it forces an end"). What it spent on the action stays
        // spent, and a cooldown it set is held: stopped, it is not given
        // back. A mode it has on stays on, since only commands are cleared
        // (the user, the same day: "Yeah, sustained abilities should not be
        // ended"). Something the engine put there (an effect's command) is
        // the engine's to end, and is left. A command let through and not
        // yet begun (nothing current, something queued) is cleared with the
        // rest: left, it began in the next creature's turn.
        //
        // Until then its own action was finished first: End was taken up
        // only at a command it did not choose, and one the engine never
        // finished held the turn until max_action_ms, which a turn the
        // player plays no longer has (below).
        if (endTurnRequested_)
        {
            const bool queued = actor.current == CommandType::Invalid;
            if (queued || Classify(actor.current) != CommandKind::Involuntary)
            {
                fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
                const Combatant *current = order_.Current();
                Log(LogLevel::Info,
                    std::format("{}'s {} is stopped: its turn was ended while it ran, and what it spent on it stays "
                                "spent",
                                current != nullptr ? Utf8(current->name) : std::string{"?"},
                                queued ? "command, not yet begun," : Name(actor.current)));
            }
            EndedByThePlayer(obs, fx);
            return;
        }
        // At an effect's command with nothing of its own let through this
        // turn: held by it, though it was not seen so as the turn began. The
        // fight's first turn begins in the frame the fight does, before its
        // creature has been looked at: a spider still dropping from the
        // ceiling had the first turn and stood out max_action_ms, twelve
        // seconds of the game's time, before anyone moved (a run with no
        // hands, 2026-10-02).
        // One getting up from a hold the machine ended as its turn began is
        // held like any other, by the engine's command for the rise or by
        // whatever was laid on it beside the stun: a hurlock alpha, its stun
        // ended and let be for the whole turn, stood twelve seconds of the
        // game's time at "execute effect" (issue 11, the highway to
        // Lothering, 2026-10-03), and the five seconds it was then let be
        // for were the mod's number, not the engine's (issue 30).
        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0)
        {
            heldByEffect_ = true;
        }
        // Held in an effect's command: the time it is held is its turn's,
        // and the turn is lost only once the hold has outlasted the bar.
        if (heldByEffect_ && HoldRuns(actor, fx))
        {
            return;
        }
        // (A hold is not the creature's own to be stopped at: its bound is
        // the bar, above, and the effect is the engine's to end.)
        const std::uint32_t limit =
            Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * (ownEffectSince_ ? kOwnEffectPatience : 1);
        // The time the engine has the action at a timed step that is
        // advancing is not counted by the limit (issue 28; core/TimedStep.h:
        // an ogre's death blow, a step of 12.5 s by the engine's own count,
        // was stopped half a second before its end and its turn lost). Not
        // deferred and judged after: so it was first built, and in play the
        // blow was stopped a frame after its timed step ended, the command
        // still at its tail and the count already spent (2026-10-06). What
        // the limit counts is the time the action stood: before the step,
        // after it, and while its clock did not move.
        const std::uint32_t ran = clock_ - executingSince_;
        const std::uint32_t counted = ran - notCounted;
        if (ran > limit && counted <= limit && !std::exchange(budget_.stepSaid, true))
        {
            const Combatant *current = order_.Current();
            const StepTime &step = budget_.step.Last();
            Log(LogLevel::Info,
                std::format("{} is still at a {} after {} ms: {} ms of that at the engine's own step of {:.2f} s while "
                            "it advanced ({:.1f} s of it done), which the limit does not count (issue 28)",
                            current != nullptr ? Utf8(current->name) : std::string{"?"}, Name(actor.current), limit,
                            notCounted, static_cast<float>(step.durationMs) / 1000.0f,
                            static_cast<float>(step.elapsedMs) / 1000.0f));
        }
        // A turn the player plays is never ended by a clock of the mod's
        // (issue 51; the user, 2026-10-05: "I think there probably should
        // not be a time out for the player, unless they're letting tactics
        // run the actor"): not at max_turn_ms (TurnTooLong), and not here.
        // An action the engine never finishes is the player's to end, with
        // End (above), which stops it. Said once, so that a log shows where a
        // turn stood.
        if (const Combatant *current = order_.Current(); current != nullptr && PlayerControls(*current))
        {
            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))
            {
                Log(LogLevel::Info,
                    std::format("{} is still at a {} after {} ms: its turn is the player's, and is not ended for it "
                                "(End stops it)",
                                Utf8(current->name), Name(actor.current), limit));
            }
            return;
        }
        // On a turn its tactics play, what it is at is stopped and the turn
        // ended, as at max_turn_ms (TurnTooLong): its commands cleared, and
        // no mode switched off by it.
        if (!heldByEffect_ && counted > limit)
        {
            fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));
            Log(LogLevel::Warn, std::format("still at a {} after {} ms: stopped", Name(actor.current), limit));
            ForgiveGrab();
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
    if (ActorGone(obs))
    {
        EndTurn(fx, "fell");
        return;
    }
    // (At its own mode's command it is at nothing the turn waits for:
    // AtItsOwnMode, issue 51.)
    const bool ownMode = obs.actor && AtItsOwnMode(*obs.actor);
    if (obs.actor && obs.actor->busy && !ownMode)
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
    // A turn its tactics play, its creature at its own mode's command as the
    // world settles: spent on the mode. The player's settles, and waits for
    // an order with the mode on (below).
    if (ownMode && !player)
    {
        SpentOnItsMode(*current, obs.actor->ownMode, fx);
        return;
    }
    // An AI is told to choose in mid-turn only on a look taken with this
    // step: on one half a second old, a creature stunned since was told
    // (test_fights.cpp, seeds 35 and 126 with time). The statuses are asked
    // for with the next step, which ends the turn of one that is held.
    const bool fresh = player || lastStatusNow_ == lastNow_;
    if (!fresh)
    {
        statusWanted_ = true;
    }
    // The kept action is done (or never came): the turn it began is its
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
        if (player || budget_.spend.TimeLeft())
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
    // turn at once; after that it is told as any AI with time left is, on a
    // fresh look (below).
    // Twice given back: it has nothing it can get to, and would stand at a
    // third try as long as at the first two (a blocked spider's turn was six
    // seconds: play test 31).
    if (!player && budget_.givenBack >= kGivenBackEnough)
    {
        EndTurn(fx, "nothing it can get to");
        return;
    }
    // (A hostile that perceives nobody to fight any more is not told to
    // choose again, issue 29: until 2026-10-05 that was said here, and its
    // turn ended "done" as it settled. It has no turn now, and that turn has
    // ended before this step is reached: Step, outOfCombat_.)
    if (!player && budget_.spend.TimeLeft() && budget_.refunded && !budget_.retried && !budget_.outOfMovement)
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
        // With time, an AI with time left chooses again, as often as it has
        // time: its turn ends as its bar is spent, as it waits, or as it
        // chooses nothing. No count of its commands ends it (issue 38: until
        // 2026-10-04 one that had started max_commands_per_turn was not told
        // again, whatever its bar still held). One that answers each telling
        // with what takes nothing of its bar is told for as long as its turn
        // may last (max_turn_ms, Step).
        if (budget_.spend.TimeLeft() && !budget_.outOfMovement)
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
        EndedByThePlayer(obs, fx);
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
    if (budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 && budget_.onArrivalMs == 0)
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
    // (Nor is the talent it was on its way to use on cooldown: chosen,
    // walked towards and never reached, it never ran, and the engine started
    // none. The user, 2026-09-30, of one wasted so while the machine
    // reckoned cooldowns itself.)
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
                    budget_.spend.MovementMax(), refund ? "; the action it was moving for is kept" : ""));
    if (current != nullptr && !PlayerControls(*current))
    {
        EndTurn(fx, "out of movement");
    }
}

void TurnMachine::EndTurn(std::vector<Effect> &fx, std::string_view why)
{
    // A walk to its kept attack's target, asked for with this step's effects
    // and not yet given, is not given: the turn is over (its creature thrown
    // as the walk was to begin, its bar spent on the attack: the long run of
    // random fights with time, issue 30). Given, the walk came to the gate
    // out of its creature's turn.
    if (const Combatant *current = order_.Current(); current != nullptr && approach_)
    {
        std::erase_if(fx, [&](const Effect &e) { return e.kind == EffectKind::Approach && e.target == current->id; });
    }
    bool land = false;
    if (const Combatant *current = order_.Current(); current != nullptr && std::exchange(clockRuns_, false))
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
        // cap; a turn lost whole, or spent, leaves none. And only from a turn
        // its actor ended itself (core/TurnEnd.h has every reason's class):
        // one skipped, lost or ended for it carries nothing over, whatever
        // its bar still held. The user, 2026-10-04 (issue 29): "the actor did
        // not explicitly save any time". Until then a skeleton that chose
        // nothing, turn after turn, began each with a second more.
        // (A turn passed over had no bar, and leaves nothing: TurnBudget.)
        if (budget_.spend.HasBar())
        {
            // What would roll over, to the cap, were the turn its actor's to end.
            const std::uint32_t unspent = budget_.spend.Finish(TimeFromTactics(TacticsOf(*current)).rolloverCapMs);
            if (!EndedByItsActor(why))
            {
                Log(LogLevel::Warn, std::format("a turn ended for a reason core/TurnEnd.h does not class (\"{}\"): "
                                                "nothing rolls over",
                                                why));
            }
            const bool saved = RollsOver(why);
            const std::uint32_t rolled = saved ? unspent : 0;
            rollover_[current->id] = rolled;
            if (saved)
            {
                Log(LogLevel::Info, std::format("{}'s turn ends: {}; {:.2f} s rolls over", Utf8(current->name), why,
                                                static_cast<float>(rolled) / 1000.0f));
            }
            else
            {
                Log(LogLevel::Info,
                    std::format("{}'s turn ends: {}; nothing rolls over{}", Utf8(current->name), why,
                                unspent > 0 ? std::format(", its actor not having ended it ({:.2f} s would have)",
                                                          static_cast<float>(unspent) / 1000.0f)
                                            : std::string{}));
            }
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
    unaware_ = false;
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
    // How often the fight's creatures were faced at their targets (Faced;
    // issue 40): once, here, and only if any were. The count is the
    // fight's, begun again with the next (StartFight).
    if (facedFirst_ > 0)
    {
        Log(LogLevel::Info,
            std::format("in this fight {} turn to face with a creature the engine would not have turned, which was "
                        "faced at its target exactly (issue 40)",
                        facedFirst_ == 1 ? std::string{"1 attack or ability came to its"}
                                         : std::format("{} attacks and abilities came to their", facedFirst_)));
    }
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
    ReleaseCooldowns(fx);
    HoldTime(false, fx);
    SetSpeed(1.0f, fx);
    resume_ = false;
    Signal(fx, events::kCombatEnd, kNoObject, order_.Round(), 0);
    fx.push_back(Effect::Of(EffectKind::Unpause));
    // Whoever the fight held is given back to the game, so that it goes on
    // as it would have with no mod there. The user, 2026-10-04, of a fight
    // the party runs from: "If you run far enough away from the enemies, the
    // fight should end. That's what rtwp does. In that case the enemies
    // should go back to normal. Getting close to them should retrigger a
    // fight." (issues 42 and 43). One the game has taken out of combat was
    // the game's again there and then, its script's commands let through
    // (Judge), so it has stood down already, by the game's own rules. What
    // is left to give back is the AI of those the game still has in combat,
    // which a held command leaves waiting for good: they are told to choose.
    //
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
    // Not the party's (issue 12 left them out and gave no reason; read
    // 2026-10-04 with issue 43): the game's own scripts run a party member's
    // AI again where they never run another creature's. Attacked in combat
    // with nothing to do, it chooses at once; and each time one of the party
    // is attacked, or the one the player controls attacks, every party
    // member doing nothing is given a wait of a second, whose end runs its
    // AI (rules_core.nss, EVENT_TYPE_ATTACKED; ai_main_h_2.nss,
    // AI_SetPartyAllowedToAttack: "giving a wait command if the follower is
    // doing nothing (to enable combat commands again)"). So with its enemies
    // told, the party is woken by the first blow, and until one comes it
    // waits on the player, as it has through the fight. Told here, every
    // companion would run its tactics in the seconds after each fight,
    // before the game has left combat: the user's to choose.
    for (const Combatant &c : order_.All())
    {
        const auto hold = held_.find(c.id);
        const bool held = hold != held_.end() && hold->second.seen != Condition::None;
        if (c.side != Side::Party && !c.down && !held)
        {
            fx.push_back(Effect::Of(EffectKind::PokeAi, c.id));
        }
    }
    // And whoever else the fight held, with no turn of its own to mend it:
    // those that left it alive, and one in combat that no scan found.
    LetGo(fx);
    outside_.clear();
    order_.Clear();
    held_.clear();
    budget_ = Budget{};
    phase_ = Phase::Idle;
    latch_ = true;
    wantScan_ = false;
}

// The gate is where the fight breaks a creature's AI: a command turned away
// never ends, and a command's end is what runs the AI again (turnedAway_ has
// what was read of the game's scripts). A combatant is told at its turn.
// Those with no turn to come are told here, as the fight lets the world go,
// the machine idle by the time the game layer tells them: the same telling,
// once each.
// - Those that left the fight alive (left_), in the order as gone or dropped
//   from it: all of them, in a fight the party ran from. Each was held like
//   any combatant while the game had it in combat, and no turn has told it
//   since. Core cannot say which of them the game still has in combat (they
//   are no longer read), so each is told, and for one that is not the
//   telling is nothing much (below).
// - Those in combat that the gate turned away outside the order
//   (turnedAway_).
//
// What one told then does is the game's: in combat its AI chooses, and it
// fights on; out of combat its ambient behaviour goes on, unless it is busy,
// as one walking home is (rules_core.nss, EVENT_TYPE_COMMAND_COMPLETE;
// sys_ambient_h.nss, Ambient_CommandComplete: "Busy - command(s) in queue");
// disabled, the game's script discards the telling; and dead or gone it is
// passed over by the game layer, which alone can ask (Bridge.cpp, PokeAi:
// outside the order nobody reads whether a creature still stands). A telling
// to one that is busy does nothing either way (ai_main_h_2.nss,
// AI_DetermineCombatRound: "Do not run any AI if the creature is doing
// something right now").
//
// Not one told with this step already: a combatant still standing, one that
// waited outside (End). In order of their ids, so that a fight is told the
// same way each time it is played.
//
// Whoever was turned away is told, wherever it stands: the gate is asked
// about every creature of the area, so a battle that is nobody's 200 m off
// was stopped by this fight too, a command each, and is started again.
void TurnMachine::LetGo(std::vector<Effect> &fx)
{
    std::vector<ObjectId> owed(turnedAway_.begin(), turnedAway_.end());
    for (const auto &[id, side] : left_)
    {
        owed.push_back(id);
    }
    std::ranges::sort(owed);
    owed.erase(std::ranges::unique(owed).begin(), owed.end());
    turnedAway_.clear();
    std::string said;
    int told = 0;
    for (const ObjectId id : owed)
    {
        if (std::ranges::any_of(fx, [&](const Effect &e) { return e.kind == EffectKind::PokeAi && e.target == id; }))
        {
            continue;
        }
        fx.push_back(Effect::Of(EffectKind::PokeAi, id));
        said += std::format("{}{:#x}", told++ == 0 ? "" : ", ", id);
    }
    if (told > 0)
    {
        Log(LogLevel::Info,
            std::format("{} that left the fight alive, or that the gate turned away outside it, {} told to choose, "
                        "the world theirs again: {}",
                        told, told == 1 ? "is" : "are", said));
    }
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
    // Leaving the fight is the engine's perception (core/Rules.h,
    // StaysInTheFight; issue 29): one of the fight that the scan's own rules
    // no longer find fighting (out of combat, or beyond combat_radius of
    // every living party member) was read all the same (Needs::inFight), and
    // stays while a party member perceives it or it perceives one (an ally,
    // while it perceives an enemy). Until 2026-10-04 the radius took it out,
    // and it stood frozen outside the order, whoever it was chasing.
    const auto stays = [](const Combatant &c) { return StaysInTheFight(c.side, c.fighting, c.perception); };
    for (const Combatant &seen : scan)
    {
        if (seen.hero)
        {
            hero_ = seen.id;
        }
        if (seen.fighting)
        {
            afar_.erase(seen.id);
        }
        else
        {
            // As one the scan did not find: where neither perceives the
            // other; and where it is not in the fight, or has left it, since
            // being perceived brings nobody in (who joins is as it was).
            const Combatant *in = order_.Find(seen.id);
            if (in == nullptr || in->down || !stays(seen))
            {
                // (What the machine knows of it stays as it was, until it is
                // out of the fight kAbsentMs on. Not its Haste and slows,
                // which are the statuses' to read, of everyone in the order:
                // a turn of its that comes meanwhile is made at the pace it
                // has then, as anyone's is. Read with the scan, as they first
                // were, they went stale here with the rest: the random
                // fights with strays, seed 16, as issue 49 gave them effects
                // that come and go.)
                continue;
            }
            if (afar_.insert(seen.id).second)
            {
                const std::string_view why = seen.side != Side::Enemy   ? "it perceives an enemy"
                                             : !seen.perception.ofParty ? "the party perceives it"
                                             : !seen.perception.byParty ? "it perceives the party"
                                                                        : "it and the party perceive each other";
                Log(LogLevel::Info,
                    std::format("{} is not found fighting near the party any more (out of combat, or further than "
                                "{:.0f} m from its nearest living member), and stays in the fight: {}",
                                Utf8(seen.name), settings_.combatRadius, why));
            }
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
            // Its Haste and slows too (CombatantStatus::movement; issue 49):
            // a scan in the middle of its turn, which reads none, would
            // otherwise put the bar back at its build's pace until the next
            // look.
            const int initiative = known->initiative;
            const float energy = known->energy;
            const bool mana = known->mana;
            MovementEffects movement = std::move(known->movement);
            *known = seen;
            known->initiative = initiative;
            known->energy = energy;
            known->mana = mana;
            known->movement = std::move(movement);
            absentSince_.erase(seen.id);
            if (left_.erase(seen.id) > 0 && !seen.down)
            {
                Log(LogLevel::Info, std::format("{} is fighting again", Utf8(seen.name)));
                Emit(EventKind::UpAgain, seen.id, "fighting again");
                // Whatever it took up while it was out of the fight stops, as
                // a joiner's does: it waits for its turn like everyone else.
                // One that had left and was set walking by a conversation's
                // scripts (the gate stands aside for those, and the fight's
                // return stops only those still in it) came back into the
                // fight with its walk running on through another's turn
                // (test_fights.cpp's long run with combatants out of reach,
                // seed 165, 2026-10-04).
                fx.push_back(Effect::Of(EffectKind::ClearCommands, seen.id));
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
            left_.erase(seen.id);       // dropped from the order since it left: back as one that joins
            turnedAway_.erase(seen.id); // its turns are to come: told at each, and not as one with none
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
            cooled_.erase(joined); // what cools on it is read as it comes in, this time too
            if (OwnClocks() && timeHeld_)
            {
                Effect own = Effect::Of(EffectKind::OwnClock, joined);
                own.a = 1;
                fx.push_back(own);
            }
            Emit(EventKind::Joined, joined, {}, initiative);
        }
    }
    // An enemy or ally the scan no longer has in the fight has left it: dead
    // and gone, or neither found fighting nor held in the fight by the
    // engine's perception (it fled, and the party lost it). Once it has been
    // so a while: one scan without it is not that (absentSince_).
    std::vector<ObjectId> absent;
    for (const Combatant &c : order_.All())
    {
        if (c.side != Side::Party && !c.down &&
            std::ranges::none_of(scan, [&](const Combatant &s) { return s.id == c.id && stays(s); }))
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
        afar_.erase(id);
        left_.emplace(id, c->side);
        // Why, where the scan read it: alive, and out of perception.
        const bool read = std::ranges::any_of(scan, [&](const Combatant &s) { return s.id == id; });
        Log(LogLevel::Info,
            std::format("{} is no longer fighting{}", Utf8(c->name),
                        !read                    ? ""
                        : c->side == Side::Enemy ? ": out of combat or out of range, and neither it nor the party "
                                                   "perceives the other"
                                                 : ": out of combat or out of range, and it perceives no enemy"));
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
        // Its Haste and slows as they stand now: what its next turn's meters
        // are made of, and with time what its turn under way walks at from
        // here (FollowPace; issue 49).
        c->movement = status.movement;
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
            else if (status.conditionSeconds > 0.0f || settings_.conditionTurns > 0)
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
            // out on the clock, it keeps the time it owes: effects' time
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

// A party member's turns are the player's unless its tactics play them
// (core/PartyTactics.h: as chosen for it, else as `[turns] party` says; never
// the hero's). Of the one whose turn is under way, by what was chosen for it
// as that turn began: a choice made since is its next turn's (issue 46; the
// user, 2026-10-05: "applies to character's next turn"). The setting is read
// as it is now, in a turn under way too.
bool TurnMachine::PlayerControls(const Combatant &c) const
{
    const bool underWay = budget_.begun && budget_.begun->who == c.id;
    const std::optional<bool> chosen = underWay ? budget_.begun->chosen : partyTactics_.Chosen(c.tag);
    return c.side == Side::Party && !tb::TacticsPlay(chosen, c.hero, settings_.partyControl);
}

bool TurnMachine::OnTactics(const Combatant &c) const
{
    return c.side == Side::Party && tb::TacticsPlay(partyTactics_.Chosen(c.tag), c.hero, settings_.partyControl);
}

TacticsAnswer TurnMachine::TacticsOf(const Follower &who) const
{
    TacticsAnswer answer;
    // Nothing kept for the hero's tag is asked: its turns are the player's.
    const std::optional<bool> chosen = who.hero ? std::nullopt : partyTactics_.Chosen(who.tag);
    answer.chosen = chosen.has_value();
    answer.on = tb::TacticsPlay(chosen, who.hero, settings_.partyControl);
    if (const Combatant *current = order_.Current();
        current != nullptr && current->id == who.id && budget_.begun && budget_.begun->who == who.id)
    {
        answer.turnUnderWay = true;
        answer.turnIsTactics = !PlayerControls(*current);
    }
    return answer;
}

TacticsAnswer TurnMachine::ChooseTactics(const Follower &who, bool on, std::string_view by)
{
    const bool changed = !who.hero && partyTactics_.Choose(who.tag, on);
    TacticsAnswer answer = TacticsOf(who);
    answer.changed = changed;
    if (changed)
    {
        Log(LogLevel::Info,
            std::format("{}'s {} from its next turn (set by {}){}", Utf8(who.name),
                        on ? "tactics play its turns" : "turns wait for the player", by,
                        answer.turnUnderWay ? std::format("; the turn under way is played out as it began, by {}",
                                                          answer.turnIsTactics ? "its tactics" : "the player")
                                            : std::string{}));
    }
    return answer;
}

TacticsBoxes TurnMachine::BoxesOf(std::span<const Follower> party) const
{
    TacticsBoxes boxes;
    for (const Follower &who : party)
    {
        boxes.Set(who.id, BoxFor(who, TacticsOf(who)));
    }
    return boxes;
}

void TurnMachine::SetPartyTactics(PartyTactics choices)
{
    partyTactics_ = std::move(choices);
}

ObjectId TurnMachine::PartysOwn(ObjectId except) const
{
    const auto standing = [&](const Combatant &c) { return c.side == Side::Party && !c.down && c.id != except; };
    if (const Combatant *hero = hero_ != kNoObject ? order_.Find(hero_) : nullptr; hero != nullptr && standing(*hero))
    {
        return hero_;
    }
    const auto &all = order_.All();
    const auto other = std::ranges::find_if(all, standing);
    return other != all.end() ? other->id : kNoObject;
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

// A turn its AI plays has a time limit, max_turn_ms: still going then, it is
// ended, whatever it is doing and whether or not it spends anything, "took
// too long" as an action past max_action_ms is, and of the same clock (the
// wall's, Patience). Counted from when the turn began (turnSince_). A turn
// the player plays has none: a companion's too, unless its tactics play it
// (PlayerControls).
//
// It is what bounds a turn made of steps that take nothing (issue 38). The
// machine's other waits each bound one thing: ai_think_ms, an AI's choosing;
// ai_follow_up_ms, its next move after an action; max_action_ms, one action;
// the stuck rules, one standing. Until 2026-10-04 a count of the turn's
// commands (max_commands_per_turn, six) bounded the rest, and cut turns that
// had time to spend: the user, "I thought we relied on time as the limiting
// factor". With the count gone, a script that queues a weapon set switched
// as each one ends, a move that is over where it stands each time it is
// told, a walk it cannot make again and again, each went on for as long as
// its script did (tried in the tests' stand-in for the game: 215 switches in
// two minutes); none is known in the game's scripts.
//
// Tried before this and dropped, all of that day, so that nobody builds
// them again. "Told again only if something was spent": it took the turn of
// a boss who opens by turning a sustained ability on, which costs nothing,
// and acts only when told again. Its exception, "but for once a turn". And
// "standing costs the time stood", a command that got nowhere charged the
// time its creature stood at it: it charged a party member for a path the
// engine itself gave up ("Wynne stood 1.03 s at a move to location that got
// nowhere: 1.03 s of its bar"). The user, of the first two: "How about for
// non-party members we apply a timeout? Don't we already have one? I'm
// skeptical about hacking in all these conditions. We should keep things
// simple."; and of the third: "Drop it then."
//
// The default, 30 s, is to cut nothing a turn does in play: in 54 logs
// (5,303 turns AIs played, 4,253 of them at three times the speed) half took
// 2.2 s of the wall's time and ninety-nine in a hundred under 7.4 s; the
// longest that was not one action stopped at max_action_ms was a boss's,
// 13.1 s (five commands, and a walk to her kept attack's target that stood
// and was stopped), and ten more took over 10 s. Twelve seconds,
// max_action_ms's own, would have cut that one and stood within two seconds
// of the ten.
// A look at the step the acting creature's command is at, and what of the
// action's time so far its limit does not count: the time the engine had it
// at a timed step that advanced (issue 28; core/TimedStep.h). The same time
// is kept off the turn's limit (TurnTooLong).
//
// Each command's own, as its limit is (executingSince_). Not a creature an
// effect's command holds: a knockback's command has a timed step too, and a
// hold's bound is the bar. And not without end: a step renewed as it runs (a
// looping animation, its length pushed ahead again and again) advances for
// ever, so no more than kStepFree times the limit's own length is left
// uncounted, of the action and of the turn each.
std::uint32_t TurnMachine::StepTimeNotCounted(const ActorObservation &actor)
{
    if (budget_.stepOf != executingSince_)
    {
        budget_.stepOf = executingSince_;
        budget_.stepFreeMs = 0;
        budget_.stepSaid = false;
    }
    const std::uint32_t advanced = budget_.step.Look(actor.step, clock_);
    if (!heldByEffect_)
    {
        budget_.stepFreeMs = std::min(budget_.stepFreeMs + advanced,
                                      Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * kStepFree);
        budget_.turnFreeMs = std::min(budget_.turnFreeMs + advanced,
                                      Patience(static_cast<std::uint32_t>(settings_.maxTurnMs)) * kStepFree);
    }
    // (A look can reach back before this command began, to the one before
    // it: never more than the command has run.)
    return std::min(budget_.stepFreeMs, clock_ - executingSince_);
}

bool TurnMachine::TurnTooLong(std::vector<Effect> &fx)
{
    const Combatant *current = order_.Current();
    if (current == nullptr || PlayerControls(*current) ||
        (phase_ != Phase::AiThinking && phase_ != Phase::Executing && phase_ != Phase::Settling))
    {
        return false;
    }
    const std::uint32_t limit = Patience(static_cast<std::uint32_t>(settings_.maxTurnMs));
    // Not counted: the time the engine had an action of the turn's at a timed
    // step that advanced (issue 28, as at max_action_ms: StepExecuting,
    // StepTimeNotCounted). Said once, as the turn first runs past the limit
    // by the clock and is not ended for it.
    const std::uint32_t ran = clock_ - turnSince_;
    const std::uint32_t counted = ran - std::min(ran, budget_.turnFreeMs);
    if (counted <= limit)
    {
        if (ran > limit && !std::exchange(budget_.turnStepSaid, true))
        {
            Log(LogLevel::Info,
                std::format("{}'s turn is still going after {} ms: {} ms of that at a step of the engine's own timing "
                            "while it advanced, which the limit does not count (issue 28)",
                            Utf8(current->name), limit, budget_.turnFreeMs));
        }
        return false;
    }
    Log(LogLevel::Warn, std::format("{}'s turn is still going after {} ms, {} of its commands let through: ended",
                                    Utf8(current->name), limit, budget_.commands));
    // What it is at is stopped, as an action past max_action_ms is. Not an
    // effect's command that holds it: that is the engine's to end.
    if (!heldByEffect_)
    {
        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));
        ForgiveGrab();
    }
    EndTurn(fx, "took too long");
    return true;
}

// Its target did not wait so long for anything the game meant: what the
// action's steps added to its debt is not owed (play test 33: a victim owing
// 31.8 s of a stall, its turns lost while an ogre killed it).
void TurnMachine::ForgiveGrab()
{
    const auto debt = grabbedMs_.find(budget_.lastTarget);
    if (budget_.grabbedNowMs == 0 || debt == grabbedMs_.end())
    {
        return;
    }
    debt->second -= std::min(debt->second, std::exchange(budget_.grabbedNowMs, 0));
    if (debt->second == 0)
    {
        grabbedMs_.erase(debt);
    }
}

bool TurnMachine::TurnSpent() const
{
    return !budget_.spend.TimeLeft();
}

bool TurnMachine::ActorGone(const Observation &obs)
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

// An attack the engine itself has no way to make (issue 27, 2026-10-04). The
// game layer asks the engine for an attack's approach and its walk before
// the command is let through, and says which "no" it had for an answer
// (CommandRequest::noAttack): no room for the attacker on its target's melee
// ring, as the engine's attack will find it; no path to where its approach
// goes; or for a shot no line of sight to be had on the way. Until then the
// gate went on past the first two with a guess. Dragonlings swarming one party member in the caverns above Haven
// had 6 attacks let through "its walk not known" that the engine failed
// within 2 ms (COMMAND_FAILED_NO_SPACE_IN_MELEE_RING), and a party member
// standing where the pathfinder found no way to anyone had 15 let through on
// a walk reckoned by the straight line that failed 0.4 s on
// (COMMAND_FAILED_INVALID_PATH), and 4 refused "Too far" by that walk, the
// wrong reason. Each was given back, and an AI's own script chose the same
// again as each ended, until the count of a turn's commands there was then
// (max_commands_per_turn, gone since issue 38): 11 turns of that session
// ended "has started 6 commands this turn".
//
// So the attack is refused here, nothing spent. The player's own order is
// told why, in the words the failure would have had: over the character, as
// "Too far" is, and on the bar's caption, as an action given back is (issue
// 21: over the character alone the word was missed). An AI's is given back
// before a step, and what its AI does about it is its own: it is told to
// choose again once, and its turn ends at the second give-back, as after a
// failure under way (StepSettling, kGivenBackEnough). Seen in play
// (2026-10-04, twelve dragonlings staged round one party member): three
// times an attack given back so, each followed by its AI choosing again,
// with no attack let through and no engine failure between.
//
// Every attack is put to the engine afresh, an AI's as the player's. The
// first build of this kept a memory beside it: a target the engine had
// refused, or failed under way for want of a place or a path, was "ruled out
// for the rest of its turn" and its AI's next attack on it refused unasked.
// It was withdrawn after that play. The game's own AI answers a failure
// with a move closer and the same target again (ai_main_h_2.nss,
// AI_ExecuteAttack), and that second attack is often good: in three logs
// from before, 17 turns had a failure for want of a place, and in 7 of them
// a later attack on the same target that turn went through and landed. The
// memory refused exactly those, 9 turns of 49 in the staged fight, the
// gate's own line just above the refusal reading "in its reach by the
// engine's melee approach: nothing to walk". And it did not get the AI onto
// another target, its script choosing by threat: the same one 13 times of
// 16. The engine's no is for where the two stood when it was asked. What
// that play was short of was a better question, not a memory: the failures
// under way were attacks the gate's question had called good, and DAOLib has
// since found why and asks the engine the whole of it (src/game/Gate.cpp,
// WalkToTarget: Approach::findsRoom).
bool TurnMachine::NoAttackToMake(const Combatant &who, const CommandRequest &request, bool player)
{
    if (request.noAttack == NoAttack::None)
    {
        return false;
    }
    const std::string_view note = NoAttackNote(request.noAttack);
    const std::string aimed = Aimed(request.target);
    const std::string on = aimed.empty() ? std::string{} : " on " + aimed;
    if (player)
    {
        const std::wstring_view words = WhyNoAttack(request.noAttack);
        Refuse(who.id, std::wstring{words}, request.type, std::string{note});
        // The caption's whatever the floaties' setting, and for as long as
        // an action given back has it.
        notice_ = Notice{RefusedLine(words, who.name, request.type), lastNow_};
        Log(LogLevel::Info, std::format("{}: {}{} refused, {}", Utf8(who.name), Name(request.type), on, note));
        return true;
    }
    // In an unaware hostile's beat (Handover; issue 29) its AI has not been
    // told its turn has come, and an attack its script queues there is no
    // choice of this turn's, as its wait there is none (Judge, Pass).
    // Refused by the engine's answer all the same, and nothing more: not
    // counted as given back, and its AI not told again, which would wake one
    // that is not to be woken. Settled from here its turn would end "done"
    // with its time rolled over, for an attack that never began. The beat
    // goes on: to its end, the turn skipped "unaware"; or, if the attack
    // came because its creature has noticed, to the step that sees so and
    // tells its AI (StepThinking), whose choice is then asked of the engine
    // like any.
    if (unaware_ && phase_ == Phase::AiThinking)
    {
        Refused(LogLevel::Info,
                std::format("{}'s {}{} is refused before a step ({}): it is unaware, and its AI has not been told its "
                            "turn has come",
                            Utf8(who.name), Name(request.type), on, note));
        return true;
    }
    // Given back as a failure under way is, with nothing to give: counted,
    // and its AI told again.
    budget_.refunded = true;
    ++budget_.givenBack;
    Log(LogLevel::Info,
        std::format("{}'s {}{} is given back before a step ({})", Utf8(who.name), Name(request.type), on, note));
    Emit(EventKind::ActionGivenBack, who.id, std::string{note}, 0, Name(request.type), aimed);
    // Its AI was thinking and its choice came to nothing: the gate frees a
    // refused command, so nothing of its own will end and wake it, and its
    // turn would stand out its thinking time and end "chose nothing". It
    // settles from here.
    if (phase_ == Phase::AiThinking)
    {
        phase_ = Phase::Settling;
    }
    return true;
}

// A kept attack is asked of the engine as any attack is (issue 27). With a
// turn a bar of time an attack that does not fit is kept, and given to its
// creature as its next turn begins (LandCast), by the game layer, through the
// gate. It was let through there whatever the engine answered, the one way
// left for an attack to go to the engine and fail for want of room: in the
// staged crowd on the build before this (2026-10-04, 328 attacks let through
// with room, none of them failed for it) two kept attacks were given with the
// gate's own line above them saying "no room once its ring is filled", failed
// 50 ms on, and what their turns had paid for them, 0.01 s and 0.84 s, was
// not given back: a landed attack's time is its turns', not this command's.
//
// So where the engine has no attack to make, the kept one is not made. It is
// dropped as one whose target is gone is (KeptVoid): what this turn paid for
// it as it began is given back, and the turn is handed over whole, with the
// step that next looks at everyone. What the turn before spent on it is
// spent, as for a target gone. The player is told why, over the character
// and on the caption; an AI's counts as an action given back, so that the
// same answer to what it chooses next ends its turn (kGivenBackEnough), and
// its being handed its turn is its being told.
void TurnMachine::KeptAttackNotMade(const Combatant &who, const CommandRequest &request, bool player)
{
    if (!landing_)
    {
        return;
    }
    const std::string_view note = NoAttackNote(request.noAttack);
    const std::string aimed = Aimed(request.target);
    std::string back;
    if (const std::uint32_t paid = std::exchange(budget_.pendingPaidMs, 0); paid > 0)
    {
        budget_.spend.GiveActionBack(paid);
        back = std::format("; the {:.2f} s it owed given back", static_cast<float>(paid) / 1000.0f);
    }
    Log(LogLevel::Info, std::format("{}'s pending {}{} is not made: {}{}", Utf8(who.name), Name(request.type),
                                    aimed.empty() ? std::string{} : " on " + aimed, note, back));
    Emit(EventKind::ActionDropped, who.id, std::string{note}, landing_->ability, Name(landing_->type), aimed);
    landing_.reset();
    if (player)
    {
        const std::wstring_view words = WhyNoAttack(request.noAttack);
        Floaty(pending_, who.id, std::wstring{words}, kGrey);
        notice_ = Notice{NotMadeLine(words, who.name, request.type), lastNow_};
    }
    else
    {
        ++budget_.givenBack;
    }
    // Nothing is coming to wait for: handed over as a turn whose kept action
    // will not come is (Step).
    if (budget_.landing)
    {
        handoverWanted_ = who.id;
    }
}

// Whom a command was aimed at, for a line about it: by name, by its id where
// the fight does not know it, and nothing where it was aimed at no one.
std::string TurnMachine::Aimed(ObjectId target) const
{
    if (target == kNoObject)
    {
        return {};
    }
    const Combatant *aimed = order_.Find(target);
    return aimed != nullptr ? Utf8(aimed->name) : std::format("{:#x}", target);
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

// A creature the engine would not turn to face what its attack is aimed at
// is faced at it exactly, as its command comes to its turn to face
// (core/Facing.h has the engine's reading, the hang it is for and why it is
// asked there and not as the command is given: issue 40). For any of the
// fight's creatures, the turn's own or not: the dead case is the engine's
// and no one path's.
void TurnMachine::WatchTurnsToFace(const Observation &obs, std::vector<Effect> &fx)
{
    // One no longer at its turn to face is forgotten: what was said and
    // asked of a command is that command's.
    std::erase_if(turning_, [&](const auto &entry) {
        return std::ranges::find(obs.turns, entry.first, &TurnLook::who) == obs.turns.end();
    });
    for (const TurnLook &look : obs.turns)
    {
        if (!TurnsToFace(look.type) || order_.Find(look.who) == nullptr)
        {
            continue;
        }
        Turning &turning = turning_[look.who];
        if (turning.command != look.command)
        {
            turning = Turning{look.command, false, false};
        }
        if (look.turn.stalls)
        {
            // The dead case: faced, asked for once a command. A facing
            // that did not come off is said (Faced), and not tried again
            // with every frame.
            if (!std::exchange(turning.asked, true))
            {
                Effect face = Effect::Of(EffectKind::FaceTarget, look.who);
                face.a = static_cast<std::int32_t>(look.type);
                face.others.push_back(look.target);
                fx.push_back(std::move(face));
            }
        }
        else if (look.turn.waits && !std::exchange(turning.waited, true) && Logs(LogLevel::Debug))
        {
            // Not yet a stall, by the engine's own word: left, and looked at
            // again with the next frame. Said once a command, for a play
            // test's log to show it pass.
            Log(LogLevel::Debug,
                std::format("{} waits at its {}'s turn to face {}, {:.2f} deg off: the engine begins no turn for it "
                            "yet, its own motion still playing, which passes by itself; left to the engine (issue 40)",
                            Aimed(look.who), look.type == CommandType::Attack ? "attack" : "ability",
                            Aimed(look.target), std::abs(look.turn.degrees)));
        }
    }
}

void TurnMachine::Faced(const FacedLook &faced)
{
    const char *what = faced.type == CommandType::Attack ? "attack" : "ability";
    if (!faced.taken || !faced.now || !faced.now->done)
    {
        Log(LogLevel::Warn,
            std::format(
                "{} stood {:.2f} deg off {} as its {} came to its turn to face, the engine with no turn to "
                "begin for it, and was to be faced at it exactly: {}, and the engine {}; its {} is left as "
                "it stands (issue 40)",
                Aimed(faced.who), faced.stood, Aimed(faced.target), what,
                faced.taken ? "it was"
                            : "the engine would not face it (its movement controller is not idle), nothing was set",
                !faced.now        ? std::string{"has no turn to say for it now"}
                : faced.now->done ? std::format("has it {:.2f} deg off now: done", std::abs(faced.now->degrees))
                                  : std::format("still has it {:.2f} deg off: not done", std::abs(faced.now->degrees)),
                what));
        return;
    }
    ++facedFirst_;
    if (Logs(LogLevel::Debug))
    {
        Log(LogLevel::Debug,
            std::format("{} stands {:.2f} deg off {} as its {} comes to its turn to face: faced at it exactly (the "
                        "engine would begin no turn for it, and its {} would wait for one: issue 40); the engine "
                        "now has it {:.2f} deg off: done",
                        Aimed(faced.who), faced.stood, Aimed(faced.target), what, what, std::abs(faced.now->degrees)));
    }
}

Verdict TurnMachine::Judge(const CommandRequest &request)
{
    CommandKind kind = Classify(request.type, request.source);
    // A sustained ability is turned on or off for nothing (the user's rule,
    // 2026-09-30); action points will put a price on it.
    if (request.type == CommandType::UseAbility && request.sustained)
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
            // Whether or not that scan finds it: turned away, it is owed the
            // telling that a combatant has at its turn, and one that joins
            // is forgotten there (turnedAway_; issue 43).
            turnedAway_.insert(request.object);
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
    const bool itsTurn = current != nullptr && current->id == request.object;
    // The cast its creature charged, or with time the action it kept, given
    // again by the game layer as its turn began (LandCast): not an order of
    // this turn's.
    const bool landing = landing_ && landing_->caster == who->id && request.type == landing_->type &&
                         request.ability == landing_->ability;
    // The walk to its kept attack's target, given by the game layer
    // (EffectKind::Approach): the mod's own doing, as a kept action given
    // again is (landing), and no choice of its creature's.
    const bool approaching = approach_ && kind == CommandKind::Move && request.source == Source::Script;
    // The fight holds what the game has in combat. One the game has taken
    // out of combat, or has not taken into it, is the game's, there and
    // then: what its script has it do goes through, as it does for every
    // such creature outside the order (above). The user, 2026-10-04: "If you
    // run far enough away from the enemies, the fight should end. That's
    // what rtwp does. In that case the enemies should go back to normal."
    // Going back to normal is the game's own doing, two commands: an enemy
    // that perceives no hostile any more is taken out of combat and sent
    // home by its script, a wait of one to three seconds and a walk to where
    // it was placed (combat_h.nss, Combat_HandleCreatureDisappear;
    // creature_core.nss, EVENT_TYPE_COMBAT_END; sys_rubberband_h.nss,
    // Rubber_GoHome). Held as out of turn, they were lost: in Ortan Thaig,
    // 2026-10-04, `Stone Golem: wait from script held, not its turn` and
    // `Stone Golem: move to location from script held, not its turn` as the
    // last party member its fight's enemies perceived fell, the fight over
    // seven seconds on with seven alive, standing where they had lost the
    // party (issue 43).
    //
    // Whoever's turn it is, its own included: this is asked before the
    // turn's own rules, since such a creature has no turn (outOfCombat_). If
    // its turn is being played, that turn ends with the next step, and
    // nothing of it is charged for what is let through here; in the beat
    // that skips an unaware hostile's turn the beat goes on. The first build
    // (2026-10-05) asked it only of a creature out of its turn, and one the
    // game took out of combat in its own turn stood where it was (the Dead
    // Trenches' shriek: outOfCombat_, in the header), its walk home refused
    // by "a turn that has ended accepts nothing more of its creature's own"
    // (issue 38), which is asked of one that has a turn.
    //
    // Not its combat AI's choices, which the scripts make only for one in
    // combat (rules_core.nss, EVENT_TYPE_COMMAND_COMPLETE), and as it is
    // taken into combat its script clears what it was at (the same,
    // EVENT_TYPE_PERCEPTION_APPEAR: SetCombatState, WR_ClearAllCommands):
    // from then it is held like anyone, and told at its turn. Not the
    // party's own, the party being the player's; nor the player's order or
    // the engine's own command, which are not its script's; nor what the mod
    // itself gives the creature whose turn it is (landing, approaching).
    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&
        !(itsTurn && (landing || approaching)))
    {
        if (itsTurn && !unaware_ && !outOfCombat_)
        {
            outOfCombat_ = true;
            Log(LogLevel::Info, std::format("{} is out of combat by the game's own word, its script's {} coming for "
                                            "it: it has no turn, and is the game's",
                                            Utf8(who->name), Name(request.type)));
        }
        if (Logs(LogLevel::Debug))
        {
            Log(LogLevel::Debug, std::format("{}: {} from script let through out of turn: the game does not have it "
                                             "in combat, and it is the game's",
                                             Utf8(who->name), Name(request.type)));
        }
        return Verdict::Allow;
    }
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
    // When a turn has ended, nothing more of its creature's own is accepted:
    // not its script's, not the engine's, not an order. It stays the current
    // turn only until what its creature already has in hand is over and the
    // world has settled.
    //
    // Two things end a turn before the machine closes it. An action kept for
    // its next turn, or a cast begun to charge, with nothing else running:
    // the end comes with the next step (chargeBegan_) or after the beat over
    // the player's (pendingEndNow_). And an AI's own wait that is longer
    // than what it has left, or has no length (passRequested_; PayWait): a
    // wait is its AI passing, and what is left of its bar rolls over. Every
    // other end is said as the turn is closed (EndTurn), and from then its
    // creature's commands are refused as anyone's out of turn, above. What
    // the mod itself gives a creature that has passed is still given: the
    // action it kept, as its turn began, and the walk to its kept attack's
    // target.
    //
    // The kept action's half is of 2026-10-01 (a mode turned on in the frame
    // an action was kept ran on into the next creature's turn:
    // test_fights.cpp, seed 47 with time). The wait's is of issue 38, and
    // the mod's own doing was why it was needed. In a fight on a road
    // (2026-10-04) an allied soldier's attack ended as the last enemy fell;
    // its script answered with a wait of 2.00 s and the engine queued
    // "sheathe or draw" on it. The wait did not fit the 1.07 s it had left
    // and was refused, "its turn ends"; the weapon command was let through,
    // free. Both were over at once, so its script was asked again at once,
    // and the same pair came round every 40 to 150 ms: the game's AI paces
    // itself by how long its commands take, and a wait refused takes no
    // time. That is what play showed, and what stopped it there was the
    // count of a turn's commands there was then (max_commands_per_turn): the
    // fourth weapon command, a quarter of a second after the first, was the
    // turn's sixth, "has started 6 commands this turn: no more" was said
    // eleven times, and the turn ended half a second after its actor had
    // ended it. That count is gone. Not from play but from the tests'
    // stand-in for the game, with this rule off and no count: the same turn
    // went on to its time limit, 1,844 weapon commands in 29.5 s, and "took
    // too long" where its actor had ended it, "waits", the second it saved
    // rolled over. The user, shown the turn (2026-10-05): "Why are we
    // blocking a wait? Should we not allow it and then deduct the extra time
    // from the next turn? Or we can just end the turn without allowing
    // another command." The second: allowed, every pass would be watched in
    // real time and what did not fit would be a debt against a turn that
    // rolls time over.
    const bool actionKept = chargeBegan_ || pendingEndNow_;
    const bool aiPassed = !player && passRequested_ && !landing && !approaching;
    if (actionKept || aiPassed)
    {
        if (Logs(refusals))
        {
            Refused(refusals,
                    std::format("{}: {} from {} held, its turn is over: {}", Utf8(who->name), Name(request.type),
                                Name(request.source), actionKept ? "its action is pending" : "it has passed"));
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
    // At its own mode's command (AtItsOwnMode; issue 51), by the last look
    // at it: an action or a move is refused before anything is spent, and
    // the player told why, over the character and on the bar's caption. The
    // game itself queues such an order behind the mode's command and carries
    // it out only when the mode ends (in real time, 2026-10-05, the song on:
    // "A move order then moved her 0.0 m and an attack order struck
    // nothing", and the attack was made as the song was ordered off). Let
    // through, it would take its time of the bar for nothing and leave a
    // command to arrive later, in whoever's turn the mode ends in. What is
    // free goes through: the mode turned off above all, which is what the
    // player is told to do first. (A rule of the mod's making, 2026-10-05,
    // not the user's words: dev/DESIGN.md.) Not what the mod itself gives the
    // creature, which is given by its own rules (LandCast: not to one at an
    // effect's command).
    if (const std::int32_t mode = OwnModeSeen();
        mode != kNoMode && !landing && !approaching && (kind == CommandKind::Action || kind == CommandKind::Move))
    {
        if (player && asked)
        {
            Refuse(who->id, std::wstring{kModeFirst}, request.type,
                   std::format("ability {} is on, and keeps it at its command", mode));
            notice_ = Notice{RefusedLine(kModeFirst, who->name, request.type), lastNow_};
        }
        if (Logs(refusals))
        {
            Refused(refusals, std::format("{}: {} from {} refused, it is at its own mode's command (ability {}): the "
                                          "mode is to be turned off first",
                                          Utf8(who->name), Name(request.type), Name(request.source), mode));
        }
        return Verdict::Block;
    }
    // On a turn the player plays, the character's own AI does not spend it:
    // a controlled character carries on attacking by itself (in real time,
    // AI_DetermineCombatRound_Partial), and in play that ended a pinned
    // character's turn before the player had chosen (2026-09-30, 11:30).
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
    switch (kind)
    {
    case CommandKind::Action:
        // A kept attack the engine has no way to make now is not made
        // (CommandRequest::noAttack, as for any attack). Only a plain
        // attack: a kept talent or spell carries no answer of the engine's.
        if (landing && request.type == CommandType::Attack && request.noAttack != NoAttack::None)
        {
            KeptAttackNotMade(*who, request, player);
            return Verdict::Block;
        }
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
                Log(LogLevel::Info, std::format("{}'s pending attack is turned on {}: its target {}", Utf8(who->name),
                                                onto, KeptTargetStands() ? "is out of the world" : "has fallen"));
            }
            Emit(EventKind::ActionLanded, who->id, turned ? "turned on another" : "", request.ability,
                 Name(request.type), std::move(onto));
            landing_.reset();
            break; // for nothing: its time was its price
        }
        if (!budget_.spend.TimeLeft())
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
            Refuse(who->id, L"A door is in the way", request.type, "open it first");
            Log(LogLevel::Info,
                std::format("{}: {} refused, a door is in the way of the walk", Utf8(who->name), Name(request.type)));
            return Verdict::Block;
        }
        // An attack the engine has no way to make as the two stand
        // (CommandRequest::noAttack): refused, whoever's.
        if (NoAttackToMake(*who, request, player))
        {
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
        if (const std::optional<Verdict> kept = PayAction(*who, request))
        {
            return *kept;
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
        // A wait in an unaware hostile's beat is no choice of its turn: its
        // AI was not told the turn had come (Handover; issue 29). Neither
        // taken of its bar nor a pass: refused, and the beat goes on to its
        // end. Paid for, it would settle the turn and end it "done", its
        // time rolled over, for an idle script's wait.
        if (!player && unaware_)
        {
            return Verdict::Block;
        }
        // With time a wait costs the time it waits, and its AI chooses
        // again with what is left (PayWait); else it passes the turn.
        if (!player && PayWait(*who, request))
        {
            return Verdict::Block;
        }
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
        if (UsesAnObject(request.type) && settings_.objectSeconds > 0.0f)
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
    // (Nothing is reckoned here of the cooldown an ability's use will start:
    // the engine says what it started, once the command has run
    // (OnCooldownSet; issue 57).)
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
    // How long its creature has stood is counted for this command alone
    // (StepActing): one that stood through the command before, a swing on
    // the spot say, and is then sent at a target far off was stopped as it
    // set out, 157 ms into the new command with "3141 ms" stood (issue 11's
    // replay of the ambush, 2026-10-04).
    stoodAt_.reset();
    budget_.lastMeasured = std::isfinite(request.actionSeconds) && request.actionSeconds > 0.0f;
    orderPending_ = false;
    // Something of its own runs after all in an unaware hostile's beat (a
    // script's order): its turn is not skipped under it.
    unaware_ = false;
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
    // 29, "still at a attack after 12000 ms"). (And what is said of one the
    // player's turn has stood at past it, once a command: issue 51.)
    budget_.longSaid = false;
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
        if (held.seen == Condition::None && held.owedMs == 0)
        {
            continue;
        }
        const Combatant *c = order_.Find(id);
        state.held.push_back({id, c != nullptr ? c->name : std::wstring{}, held.seen, held.onClock, held.owedMs});
    }
    for (const Hold &h : holds_)
    {
        state.cooldowns.push_back({h.creature, h.ability, h.leftMs, h.ofMs, h.item});
    }
    for (const Charge &c : charges_)
    {
        state.charging.push_back({c.caster, c.ability, c.owedMs});
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
bool TurnMachine::OnCommandFailed(ObjectId who, CommandType type, std::string_view why, ObjectId target)
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
    // (A talent that never ran started no cooldown in the engine, and there
    // is none to take: until issue 57 the machine had reckoned one as the
    // command was let through, and took it back here. Before that an
    // archer's was held for a shot nobody took: the fourth play test,
    // 19:21:52.)
    // Every time, not once a turn (as it was with one action a turn):
    // three dagger attacks at a spider she could not get to, each over at
    // once, and the second and third were charged 1.5 s each with no swing
    // and no word (play test 30). An AI still chooses again but once for a
    // failed action (StepSettling), and then while it has time.
    if (Classify(type) != CommandKind::Action || !ActionToGiveBack())
    {
        return false;
    }
    GiveActionBack();
    budget_.refunded = true;
    ++budget_.givenBack;
    // With whom it was at, where the game layer says (issue 27): what a
    // crowd's log is read by, a dozen of one name attacking three of the
    // party. Nothing is decided by it: the next attack on the same target is
    // put to the engine like any other.
    const std::string aimed = Aimed(target);
    Log(LogLevel::Info, std::format("{}'s {}{} failed ({}): its action is given back", Utf8(current->name), Name(type),
                                    aimed.empty() ? std::string{} : " on " + aimed, why));
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
        view.movementLeft = budget_.MovementLeft();
        view.movementMax = budget_.spend.MovementMax();
        view.pendingPaidMs = budget_.pendingPaidMs;
        view.timeMs = budget_.spend.CapacityMs();
        view.timeLeftMs = budget_.spend.CapacityLeftMs();
        view.turnMs = TurnMs();
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
                                 pending, std::wstring{}, std::wstring{}, OnTactics(c)});
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
