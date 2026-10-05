"""Break src/core on purpose, one way at a time, and see the tests fail.

SkyrimFollowerTactics' rule (its dev/PRINCIPLES.md, "Check that the tests
would catch a bug"): a green suite can pass while testing nothing, and
breaking the code on purpose shows what is never checked. Each mutant below
is one promise of dev/DESIGN.md broken by a small edit. The tool applies it,
builds the core preset, runs the tests, and puts the file back. A mutant is
caught if a test fails (or the run hangs past the timeout), and survives if
every test passes. A survivor is a promise no test holds.

The mutants run side by side (the user, 2026-10-02: "do the mutants all run
serially? can we parallelize them? on low-priority threads like the build?").
Each worker has a copy of the core and its tests under build\\mutants\\wN, with
a build of its own, and breaks that copy: the working tree is read and never
written, so the core can be edited while they run and a run that is stopped
leaves nothing broken. The copies are kept between runs and brought up to
the tree file by file, so that only what changed is built again. Every core
is used, a mutant to each, and everything runs below normal priority, as the
build does, so that whatever else the machine is doing comes first (the
user: "use as many cores as available, but keep the threads low priority so
they don't interfere w/ others"). One at a time and in the tree itself, 262
mutants took 55 minutes; eight at a time, ten and a half; twenty at a time
(a core each, on this machine), seven.

Run from a shell where tools\\build.ps1 has imported the developer
environment, after one `.\\tools\\build.ps1 -Preset core` (the workers use its
vcpkg packages):

    python tools\\mutants.py               every mutant
    python tools\\mutants.py gate joiner   those whose name contains "gate" or "joiner"
    python tools\\mutants.py -j 4          four at a time (by default one a core)

Exits non-zero if any mutant survives or fails to compile.
"""

from __future__ import annotations

import argparse
import os
import queue
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORK = REPO / "build" / "mutants"
# What a worker's copy holds: the core preset builds from these and nothing else.
FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
    "vcpkg-configuration.json",
)
TREES = ("src/core", "tests")
# The core preset's packages (Catch2), installed once by the tree's own build.
PACKAGES = REPO / "build" / "core" / "vcpkg_installed"
TIMEOUT_S = 600
# Below normal, as tools\\build.ps1 runs the build: the machine stays usable.
LOW = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)


@dataclass(frozen=True)
class Mutant:
    name: str
    file: str
    old: str
    new: str


MACHINE = "src/core/TurnMachine.cpp"
ORDER = "src/core/TurnOrder.cpp"
RULES = "src/core/Rules.cpp"
SETTINGS = "src/core/Settings.cpp"
BAR = "src/core/Bar.cpp"
BARWINDOW = "src/core/BarWindow.cpp"
GIVENBACK = "src/core/GivenBack.cpp"
PORTRAITS = "src/core/Portraits.cpp"
PORTRAIT_TABLE = "src/core/PortraitTable.h"
JSON = "src/core/Json.cpp"
KEYS = "src/core/Keys.h"
PREVIEW = "src/core/Preview.cpp"
TURNEND = "src/core/TurnEnd.cpp"

MUTANTS = [
    Mutant(
        "actiontime pays through an unresolved event",
        "src/core/ActionTime.cpp",
        "if (state_ == ActionTimeState::Pending)",
        "if (true)",
    ),
    Mutant(
        "actiontime ignores interruption at an event",
        "src/core/ActionTime.cpp",
        "if (interrupted)",
        "if (interrupted && false)",
    ),
    Mutant(
        "actiontime lands before a simultaneous timer",
        "src/core/ActionTime.cpp",
        "if (untilEventMs && payment.clockMs == *untilEventMs)",
        "if (untilEventMs && payment.clockMs == *untilEventMs && remainingMs_ > 0)",
    ),
    Mutant(
        "actiontime drops cost that should pend",
        "src/core/ActionTime.cpp",
        "remainingMs_ -= payment.capacityMs;",
        "remainingMs_ = 0;",
    ),
    Mutant(
        "actiontime dispatches the same action repeatedly",
        "src/core/ActionTime.cpp",
        "state_ = ActionTimeState::Dispatched;",
        "state_ = ActionTimeState::Ready;",
    ),
    Mutant(
        "actiontime interruption leaves unpaid cost live",
        "src/core/ActionTime.cpp",
        "remainingMs_ = 0;",
        "remainingMs_ += 0;",
    ),
    Mutant(
        "actiontime lets an unpaid action dispatch",
        "src/core/ActionTime.cpp",
        "if (state_ != ActionTimeState::Ready)",
        "if (state_ == ActionTimeState::Ready)",
    ),
    Mutant(
        "turntime rounds event payment before reaching the event",
        "src/core/TurnTime.cpp",
        "boundary - progress_ + turnMs_ - 1",
        "boundary - progress_",
    ),
    Mutant(
        "turntime ignores the bonus capacity",
        "src/core/TurnTime.cpp",
        "static_cast<std::uint64_t>(turnMs_) + bonusMs + rolloverMs",
        "static_cast<std::uint64_t>(turnMs_) + bonusMs * 0 + rolloverMs",
    ),
    Mutant(
        "turntime ignores rollover capacity",
        "src/core/TurnTime.cpp",
        "static_cast<std::uint64_t>(turnMs_) + bonusMs + rolloverMs",
        "static_cast<std::uint64_t>(turnMs_) + bonusMs + rolloverMs * 0",
    ),
    Mutant(
        "turntime advances a bonus creature clock too fast",
        "src/core/TurnTime.cpp",
        "progress_ / capacityMs_",
        "progress_ / turnMs_",
    ),
    Mutant(
        "turntime holds consume capacity instead of clock time",
        "src/core/TurnTime.cpp",
        "static_cast<std::uint64_t>(clockMs) * capacityMs_",
        "static_cast<std::uint64_t>(clockMs) * turnMs_",
    ),
    Mutant(
        "turntime passing never advances the remaining clock",
        "src/core/TurnTime.cpp",
        "std::min(rolloverCapMs, CapacityLeft()), ClockLeft()",
        "std::min(rolloverCapMs, CapacityLeft()), 0",
    ),
    Mutant(
        "turntime ending a turn can bank the same capacity twice",
        "src/core/TurnTime.cpp",
        "progress_ = Total();",
        "progress_ += 0;",
    ),
    Mutant(
        "turntime loses fractional progress between payments",
        "src/core/TurnTime.cpp",
        "return {paid, ClockElapsed() - before};",
        "progress_ = static_cast<std::uint64_t>(ClockElapsed()) * capacityMs_;\n    return {paid, ClockElapsed() - before};",
    ),
    Mutant(
        "turntime tactics increases capacity at the wrong ranks",
        "src/core/TurnTime.cpp",
        "500 * (bounded / 2)",
        "500 * ((bounded + 1) / 2)",
    ),
    Mutant(
        "the gate lets anyone act",
        MACHINE,
        "if (current == nullptr || current->id != request.object)",
        "if (current == nullptr)",
    ),
    Mutant(
        "the gate never spends an action",
        MACHINE,
        "        (void)budget_.spend.SpendAction(0);\n",
        "\n",
    ),
    Mutant(
        "the gate never checks movement",
        MACHINE,
        "if (budget_.outOfMovement || budget_.MovementLeft() < kMinStep)",
        "if (false)",
    ),
    Mutant(
        "nobody is stopped for want of movement",
        MACHINE,
        "if (!actor.moving || budget_.spend.Overwalked() < kOvershoot)",
        "if (true)",
    ),
    Mutant(
        "the game is never paused for a turn",
        MACHINE,
        "return phase == Phase::Starting || phase == Phase::TurnStart ||",
        "(void)settings;\n    return phase == Phase::Idle &&",
    ),
    Mutant(
        "the world idles on the player's turn though pause_on_player_turn says hold it",
        MACHINE,
        "(phase == Phase::AwaitingOrder && settings.pauseOnPlayerTurn);",
        "(phase == Phase::AwaitingOrder && !settings.pauseOnPlayerTurn);",
    ),
    Mutant(
        "an action is given back every time",
        MACHINE,
        "budget_.lastKind == CommandKind::Action && ActionToGiveBack() && !budget_.refunded;",
        "budget_.lastKind == CommandKind::Action && ActionToGiveBack();",
    ),
    Mutant(
        "the turn never passes on",
        MACHINE,
        "    if (order_.Advance())",
        "    if (false)",
    ),
    Mutant(
        "the fight never ends when its enemies fall",
        MACHINE,
        "    if (!order_.AnyStanding(Side::Enemy))\n    {\n        End(fx, NoEnemyLeft());",
        "    if (false)\n    {\n        End(fx, NoEnemyLeft());",
    ),
    Mutant(
        "the fight's start leaves everyone's commands running",
        MACHINE,
        "    for (const Combatant &c : order_.All())\n    {\n        if (!c.down)\n",
        "    for (const Combatant &c : order_.All())\n    {\n        if (false)\n",
    ),
    Mutant(
        "resuming after a conversation leaves commands running",
        MACHINE,
        "        for (const Combatant &c : order_.All())\n        {\n            if (!c.down)\n",
        "        for (const Combatant &c : order_.All())\n        {\n            if (false)\n",
    ),
    Mutant(
        "a joiner keeps what it was doing",
        MACHINE,
        "            fx.push_back(Effect::Of(EffectKind::ClearCommands, joining.id));\n",
        "            (void)fx;\n",
    ),
    Mutant(
        "an action may run forever",
        MACHINE,
        "        if (!heldByEffect_ && clock_ - executingSince_ > limit)",
        "        if (false)",
    ),
    Mutant(
        "an AI may think forever",
        MACHINE,
        "    if (clock_ >= deadline_)\n    {\n        // What its AI saw",
        "    if (false)\n    {\n        // What its AI saw",
    ),
    Mutant(
        "an AI's wait does not end its turn",
        MACHINE,
        "        if (!player)\n        {\n            passRequested_ = true;\n",
        "        if (!player)\n        {\n",
    ),
    Mutant(
        "what happens to a creature is refused out of turn",
        MACHINE,
        "if (!Active() || kind == CommandKind::Involuntary)",
        "if (!Active())",
    ),
    # When a turn has ended, nothing more of its creature's own is accepted:
    # here, a turn its AI ended with a wait (issue 38: the count of a turn's
    # commands, which was what stopped a soldier's weapon commands of the
    # engine's after its wait, is gone). The kept action's half of the rule
    # has its mutant further down.
    Mutant(
        "what comes after an AI has passed is judged like anything: the engine's weapon commands are let through, and its wait is said again",
        MACHINE,
        "    const bool aiPassed = !player && passRequested_ && !landing && !approaching;",
        "    const bool aiPassed = player && passRequested_ && !landing && !approaching;",
    ),
    Mutant(
        "a kept action is refused its creature whose script waited as the turn began",
        MACHINE,
        "    const bool aiPassed = !player && passRequested_ && !landing && !approaching;",
        "    const bool aiPassed = !player && passRequested_ && !approaching;",
    ),
    Mutant(
        "the walk to a kept attack's target is refused a creature whose script waited in the frame it was kept",
        MACHINE,
        "    const bool aiPassed = !player && passRequested_ && !landing && !approaching;",
        "    const bool aiPassed = !player && passRequested_ && !landing;",
    ),
    Mutant(
        "a settings file that still has max_commands_per_turn is told nothing",
        SETTINGS,
        '             return "no longer a setting: nothing counts the commands an AI starts in a turn; its time is the limit, "\n                    "and max_turn_ms its turn\'s";',
        "             return {};",
    ),
    # A turn an AI plays has a time limit, max_turn_ms (issue 38): what ends
    # a turn of steps that take nothing, with nothing counting its commands.
    Mutant(
        "limit: a turn an AI plays may go on for ever",
        MACHINE,
        "    if (clock_ - turnSince_ <= limit)",
        "    if (true)",
    ),
    Mutant(
        "limit: a turn the player plays is ended at max_turn_ms too",
        MACHINE,
        "    if (current == nullptr || PlayerControls(*current) ||\n        (phase_ != Phase::AiThinking",
        "    if (current == nullptr ||\n        (phase_ != Phase::AiThinking",
    ),
    Mutant(
        "limit: max_turn_ms is of the fight's clock, not the wall's: a third as long at three times the speed",
        MACHINE,
        "    const std::uint32_t limit = Patience(static_cast<std::uint32_t>(settings_.maxTurnMs));",
        "    const std::uint32_t limit = static_cast<std::uint32_t>(settings_.maxTurnMs);",
    ),
    Mutant(
        "limit: max_turn_ms is counted from the fight's start, not the turn's",
        MACHINE,
        "    orderPending_ = false;\n    turnSince_ = clock_;\n",
        "    orderPending_ = false;\n",
    ),
    Mutant(
        "limit: a turn ended at max_turn_ms leaves its creature at what it was doing",
        MACHINE,
        "        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));\n        ForgiveGrab();",
        "        ForgiveGrab();",
    ),
    Mutant(
        "limit: a turn ended at max_turn_ms clears an effect's command that holds its creature",
        MACHINE,
        "    if (!heldByEffect_)\n    {\n        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));\n        ForgiveGrab();",
        "    if (true)\n    {\n        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));\n        ForgiveGrab();",
    ),
    Mutant(
        "limit: an action's own effect ended with its turn at max_turn_ms leaves its target owing it",
        MACHINE,
        "        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));\n        ForgiveGrab();",
        "        fx.push_back(Effect::Of(EffectKind::ClearCommands, current->id));",
    ),
    Mutant(
        "limit: max_turn_ms in the settings file is not the turn's limit",
        SETTINGS,
        '        {"turns", "max_turn_ms", Int(&Settings::maxTurnMs, 1000, 600000)},',
        '        {"turns", "max_turn_ms", Int(&Settings::maxActionMs, 1000, 600000)},',
    ),
    Mutant(
        "the fight's clock runs through the player's pause: a hold is charged for the pause, and a turn's limit runs under it",
        MACHINE,
        "    if (!obs.paused)\n    {\n        // The world's time:",
        "    if (true)\n    {\n        // The world's time:",
    ),
    Mutant(
        "rounds are never padded", MACHINE, "if (roundRunMs_ < minMs)", "if (false)"
    ),
    Mutant(
        "a conversation does not stand the gate aside",
        MACHINE,
        "        phase_ = Phase::Suspended;\n        playerPaused_ = false;\n        SetSpeed(1.0f, fx);\n        return fx;",
        "        playerPaused_ = false;\n        SetSpeed(1.0f, fx);\n        return fx;",
    ),
    Mutant(
        "fallen enemies stay in the order",
        ORDER,
        "std::erase_if(order_, [](const Combatant &c) { return !Shown(c); });",
        "(void)0;",
    ),
    Mutant(
        "a joiner acts this round though its place has passed",
        ORDER,
        "if (index <= current_)",
        "if (index <= current_ && false)",
    ),
    Mutant(
        "the player's attack out of reach is let through",
        MACHINE,
        "closing in is its turn.\n        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)",
        "closing in is its turn.\n        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + 999.0f)",
    ),
    Mutant(
        "the bar's string keeps a name's separators",
        BAR,
        "if (c >= 0x20)",
        "if (c >= 0x01)",
    ),
    Mutant(
        "movement ignores speed",
        RULES,
        "meters *= sensible ? speedModifier : 1.0f;",
        "(void)sensible;",
    ),
    Mutant(
        "an ogre's pace is taken for a shriek's",
        RULES,
        "return combatSpeed / kPersonCombatSpeed;",
        "return kPersonCombatSpeed / combatSpeed;",
    ),
    Mutant(
        "the camera goes to an enemy before its AI is told its turn has come",
        MACHINE,
        "if (settings_.cameraFollow && PlayerControls(who))",
        "if (settings_.cameraFollow)",
    ),
    Mutant(
        "a stun costs no turn",
        MACHINE,
        "    const bool owes = timed ? held.owedMs >= turn : !Timed() && held.kind == Condition::Disabled && held.turns > 0;",
        "    const bool owes = timed ? held.owedMs >= turn : !Timed() && held.kind == Condition::Disabled && held.turns > 99;",
    ),
    Mutant(
        "a condition that has had its turns is never ended",
        MACHINE,
        "if (held.turns <= 0 && held.owedMs == 0 && held.seen != Condition::None)",
        "if (held.turns < 0 && held.owedMs == 0 && held.seen != Condition::None)",
    ),
    Mutant(
        "End waits out what the engine holds the creature in",
        MACHINE,
        "        if (endTurnRequested_ && Classify(actor.current) == CommandKind::Involuntary)",
        "        if (endTurnRequested_ && Classify(actor.current) == CommandKind::Pass)",
    ),
    Mutant(
        "a condition on the game's clock costs no turn",
        MACHINE,
        "    const bool holds = (held.onClock || (settings_.conditionTurns == 0 && held.kind == Condition::None)) &&",
        "    const bool holds = (false) &&",
    ),
    # What holds a creature at an effect's command costs the time it holds it
    # (issue 30): the two seconds a turn once waited before it was taken, and
    # the five a creature getting up was let be for, are gone.
    Mutant(
        "a hold takes the turn at two seconds, whatever is left of the bar",
        MACHINE,
        "    if (bar ? budget_.spend.CapacityLeftMs() > 0 : budget_.heldMs < HoldTurnMs())",
        "    if (budget_.heldMs < 2000)",
    ),
    Mutant(
        "a turn waits out a hold however long it runs",
        MACHINE,
        "    if (bar ? budget_.spend.CapacityLeftMs() > 0 : budget_.heldMs < HoldTurnMs())",
        "    if (budget_.heldMs < 4000000000U)",
    ),
    Mutant(
        "time: a hold takes nothing of the bar",
        MACHINE,
        "        (void)budget_.spend.HoldClock(ran);",
        "        (void)budget_.spend.HoldClock(0);",
    ),
    Mutant(
        "time: a hold takes its time of the bar as an action does, not of the clock by the turn's ratio",
        MACHINE,
        "        (void)budget_.spend.HoldClock(ran);",
        "        (void)budget_.spend.SpendAction(ran);",
    ),
    Mutant(
        "time: a hold does not move its creature's own clock: an effect on it cannot wear off within the turn",
        MACHINE,
        "        budget_.holdBarMs += before - budget_.spend.CapacityLeftMs();\n        MoveClock(fx);",
        "        budget_.holdBarMs += before - budget_.spend.CapacityLeftMs();",
    ),
    Mutant(
        "one clock for everyone's effects stands still for a hold",
        MACHINE,
        "    if (!timeHeld_ || OwnClocks())\n    {\n        return;\n    }\n    // Where effects are on the one clock",
        "    if (!timeHeld_ || !OwnClocks())\n    {\n        return;\n    }\n    // Where effects are on the one clock",
    ),
    Mutant(
        "with own clocks, a hold moves the clock everything else is on too",
        MACHINE,
        "    if (!timeHeld_ || OwnClocks())\n    {\n        return;\n    }\n    // Where effects are on the one clock",
        "    if (!timeHeld_)\n    {\n        return;\n    }\n    // Where effects are on the one clock",
    ),
    Mutant(
        "effects' time run by a hold does not come off the round's worth",
        MACHINE,
        "        roundAgedMs_ += static_cast<std::uint32_t>(age.b);\n        fx.push_back(age);\n        agedAtNow_ = lastNow_;",
        "        fx.push_back(age);\n        agedAtNow_ = lastNow_;",
    ),
    Mutant(
        "a stun that runs out as a hold's time runs is taken for one cured: the turn it owed is given back",
        MACHINE,
        "        roundAgedMs_ += static_cast<std::uint32_t>(age.b);\n        fx.push_back(age);\n        agedAtNow_ = lastNow_;",
        "        roundAgedMs_ += static_cast<std::uint32_t>(age.b);\n        fx.push_back(age);",
    ),
    Mutant(
        "a second hold in a turn is charged from the first one's end",
        MACHINE,
        "    ChargeHold(fx);\n    budget_.heldSeen.reset();",
        "    ChargeHold(fx);",
    ),
    Mutant(
        "the frame a hold lets go in is not charged",
        MACHINE,
        "    ChargeHold(fx);\n    budget_.heldSeen.reset();",
        "    (void)fx;\n    budget_.heldSeen.reset();",
    ),
    Mutant(
        "a frame between one command of an effect's and the next lets its creature go: what it kept is given into the gap",
        MACHINE,
        "        if (!actor.busy && ++idleTicks_ < 2)\n        {\n            return;\n        }\n        heldByEffect_ = false;",
        "        heldByEffect_ = false;",
    ),
    Mutant(
        "max_action_ms stops a hold, and clears the effect's command",
        MACHINE,
        "        if (!heldByEffect_ && clock_ - executingSince_ > limit)",
        "        if (clock_ - executingSince_ > limit)",
    ),
    Mutant(
        "an AI whose bar a hold spent as it let go is asked to choose with nothing",
        MACHINE,
        "        if (!actor.busy && current != nullptr && !PlayerControls(*current) &&\n            (!budget_.spend.Timed() || budget_.spend.ActionsLeft() > 0))",
        "        if (!actor.busy && current != nullptr && !PlayerControls(*current))",
    ),
    Mutant(
        "as built with effects on the game's clock, a hold has no turn's worth: the turn is lost at once",
        MACHINE,
        "    const float seconds = EffectRoundSeconds() > 0.0f ? EffectRoundSeconds() : settings_.secondsPerTurn;",
        "    const float seconds = EffectRoundSeconds();",
    ),
    Mutant(
        "as built, a hold's turn's worth is a turn's seconds, not what a round spends on effects",
        MACHINE,
        "    const float seconds = EffectRoundSeconds() > 0.0f ? EffectRoundSeconds() : settings_.secondsPerTurn;",
        "    const float seconds = settings_.secondsPerTurn;",
    ),
    Mutant(
        "an AI getting up from a condition the machine ended is told to choose behind the rise",
        MACHINE,
        "        seenActor_->current == CommandType::ExecuteEffect)\n    {\n        heldByEffect_ = true;",
        "        seenActor_->current == CommandType::ExecuteEffect && endedFor_ != who.id)\n    {\n        heldByEffect_ = true;",
    ),
    # Where a hold (issue 30) meets a hostile that has noticed nobody (issue 29).
    Mutant(
        "an unaware hostile at an effect's command stands out its hold, and then its beat",
        MACHINE,
        "    if (!UnawareNow(who) && seenActor_ && seenActor_->id == who.id && seenActor_->busy &&",
        "    if (seenActor_ && seenActor_->id == who.id && seenActor_->busy &&",
    ),
    Mutant(
        "a party member thrown as its turn waits, after it has acted, is taken for one at its own action's effect",
        MACHINE,
        '": " + obs.actor->effects));\n        }\n        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect;',
        '": " + obs.actor->effects));\n        }\n        heldByEffect_ = false;',
    ),
    Mutant(
        "an AI thrown as it thinks, after it has acted, is taken for one at its own action's effect",
        MACHINE,
        "        // executing: busy here is something else's.\n        heldByEffect_ = obs.actor->current == CommandType::ExecuteEffect;",
        "        // executing: busy here is something else's.\n        heldByEffect_ = false;",
    ),
    Mutant(
        "a walk to a kept attack's target is given after its creature's turn is lost",
        MACHINE,
        "    if (const Combatant *current = order_.Current(); current != nullptr && approach_)",
        "    if (const Combatant *current = order_.Current(); current != nullptr && approach_ && false)",
    ),
    Mutant(
        "the engine's count, where it is only at least so much, is said as the whole of the hold",
        MACHINE,
        '            actor.heldKnown            ? std::format("{:.2f} s more by the engine\'s count", actor.heldSeconds)',
        '            actor.heldKnown || actor.heldSeconds > 0.0f ? std::format("{:.2f} s more by the engine\'s count", actor.heldSeconds)',
    ),
    # Where a hold (issue 30) and an AI's wait (issue 31) meet.
    Mutant(
        "a held creature's wait is paid for behind the effect, or passes its turn",
        MACHINE,
        "    if (heldByEffect_ && kind != CommandKind::Free)",
        "    if (heldByEffect_ && kind != CommandKind::Free && kind != CommandKind::Pass)",
    ),
    Mutant(
        "an AI that has waited and is then thrown is nobody's: neither held nor at its own action's effect",
        MACHINE,
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0)",
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0 &&\n            budget_.spend.ClockElapsedMs() == 0)",
    ),
    # Where a hold meets an attack the engine has no way to make (issue 27).
    Mutant(
        "a held creature's attack the engine has no way to make is one given back, not one refused as held",
        MACHINE,
        "    if (heldByEffect_ && kind != CommandKind::Free)",
        "    if (heldByEffect_ && kind != CommandKind::Free && request.noAttack == NoAttack::None)",
    ),
    Mutant(
        "an order queues behind the effect that holds its creature",
        MACHINE,
        "    if (heldByEffect_ && kind != CommandKind::Free)",
        "    if (heldByEffect_ && kind == CommandKind::Free)",
    ),
    Mutant(
        "a creature an effect let go stays held",
        MACHINE,
        "        heldByEffect_ = false; // let go: the turn is its own again, with what the hold left of it",
        "        // let go",
    ),
    Mutant(
        "an AI held by an effect is told its turn has come",
        MACHINE,
        "        seenActor_->current == CommandType::ExecuteEffect)\n    {\n        heldByEffect_ = true;",
        "        seenActor_->current == CommandType::Invalid)\n    {\n        heldByEffect_ = true;",
    ),
    Mutant(
        "an AI an effect let go is never told",
        MACHINE,
        "            Handover(*current, fx);\n            return;\n        }\n    }\n    // Standing at a walk it does not take (kStoodMs)",
        "            return;\n        }\n    }\n    // Standing at a walk it does not take (kStoodMs)",
    ),
    Mutant(
        "a conversation before the fight has started skips its start",
        MACHINE,
        "        phase_ = std::exchange(startInterrupted_, false) ? Phase::Starting : Phase::TurnStart;",
        "        phase_ = std::exchange(startInterrupted_, false) ? Phase::TurnStart : Phase::TurnStart;",
    ),
    Mutant(
        "a conversation plays at the enemies' speed",
        MACHINE,
        "        playerPaused_ = false;\n        SetSpeed(1.0f, fx);\n        return fx;\n    }\n    if (obs.mode == Mode::Gui)",
        "        playerPaused_ = false;\n        return fx;\n    }\n    if (obs.mode == Mode::Gui)",
    ),
    Mutant(
        "the speed is written though it has not changed",
        MACHINE,
        "    if (speed == speedSet_)\n    {\n        return;\n    }\n    speedSet_ = speed;",
        "    speedSet_ = speed;",
    ),
    Mutant(
        "a creature's mana or stamina never reaches the bar",
        MACHINE,
        "        c->energy = status.energy;",
        "",
    ),
    Mutant(
        "the bar's string leaves out whether it is mana",
        BAR,
        "        Field(out, card.mana);",
        "        Field(out, false);",
    ),
    Mutant(
        "a creature falls unsaid",
        MACHINE,
        "        if (c->down != wasDown)",
        "        if (c->down != wasDown && c->down == wasDown)",
    ),
    Mutant(
        "an AI's waits count real time at an enemy's speed",
        MACHINE,
        "static_cast<float>(dt) * speedSet_",
        "static_cast<float>(dt)",
    ),
    Mutant(
        "a condition the engine still holds after it was ended owes a fresh turn",
        MACHINE,
        "            if (status.id == endedFor_)",
        "            if (status.id == endedFor_ && false)",
    ),
    Mutant(
        "a turn's end goes unheard",
        MACHINE,
        "        Emit(EventKind::TurnEnded, current->id, std::string{why});",
        "",
    ),
    Mutant(
        "a refusal goes unheard",
        MACHINE,
        "    Emit(EventKind::Refused, target, Utf8(text), 0, Name(command), std::move(note));",
        "    Emit(EventKind::SpeedSet, target, Utf8(text), 0, Name(command), std::move(note));",
    ),
    Mutant(
        "a fall is heard as a creature gone",
        MACHINE,
        "(status.exists ? EventKind::Fell : EventKind::Gone)",
        "(status.exists ? EventKind::Gone : EventKind::Gone)",
    ),
    Mutant(
        "a JSON string is not escaped",
        "src/core/Json.cpp",
        "        case '\"':",
        "        case '~':",
    ),
    Mutant(
        "a move its path cannot finish is let walk",
        MACHINE,
        "where it ran out.\n        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)",
        "where it ran out.\n        if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + 999.0f)",
    ),
    Mutant(
        "a tool's argument is read from the wrong field",
        "src/core/Json.cpp",
        "        if (*name == key)",
        "        if (*name != key)",
    ),
    Mutant(
        "the state leaves out the conditions held",
        "src/core/TurnMachine.cpp",
        "        if (held.seen == Condition::None && held.turns == 0)",
        "        if (held.seen == Condition::None || held.turns >= 0)",
    ),
    Mutant(
        "a command that failed without acting keeps its action spent",
        MACHINE,
        "    GiveActionBack();\n    budget_.refunded = true;\n    ++budget_.givenBack;",
        "    budget_.refunded = true;\n    ++budget_.givenBack;",
    ),
    Mutant(
        "the hero is not the one controlled after a fight",
        MACHINE,
        "        fx.push_back(Effect::Of(EffectKind::Select, hero_));\n    }\n    Emit(EventKind::FightOver",
        "    }\n    Emit(EventKind::FightOver",
    ),
    Mutant(
        "the bar shows each combatant twice",
        "src/core/TurnOrder.cpp",
        "    for (std::size_t step = 1; step < order_.size() && slots.size() < count; ++step)",
        "    for (std::size_t step = 1; step < 2 * order_.size() && slots.size() < count; ++step)",
    ),
    Mutant(
        "an AI's turn let go by an effect pokes it while still selected",
        MACHINE,
        "            Handover(*current, fx);\n            return;\n        }\n    }\n    // Standing at a walk it does not take (kStoodMs)",
        "            fx.push_back(Effect::Of(EffectKind::PokeAi, current->id));\n            return;\n        }\n    }\n    // Standing at a walk it does not take (kStoodMs)",
    ),
    Mutant(
        "the last enemy fallen outside an attack leaves the fight waiting",
        MACHINE,
        "        if (!order_.AnyStanding(Side::Enemy))\n        {\n            End(fx, NoEnemyLeft());",
        "        if (false)\n        {\n            End(fx, NoEnemyLeft());",
    ),
    Mutant(
        "a talent that never ran is held on cooldown",
        MACHINE,
        "return h.creature == who && h.ability == ability;",
        "return h.creature == who && h.ability == -12345;",
    ),
    Mutant(
        "the state's last refusal is anyone's",
        MACHINE,
        "    state.lastRefusal = lastPlayerRefusal_;",
        "    state.lastRefusal = lastRefusal_;",
    ),
    Mutant(
        "a hold taken off before its victim's turn still costs the turn",
        MACHINE,
        "            if (takenOff && held.turns > 0)",
        "            if (takenOff && held.turns > 99)",
    ),
    Mutant(
        "a hold run out on the clock gives its turns back",
        MACHINE,
        "(!agedAtNow_ || lastNow_ - *agedAtNow_ > kAgeSettleMs)",
        "(!agedAtNow_ || lastNow_ - *agedAtNow_ > 0u)",
    ),
    Mutant(
        "a turn does not set the fight's speed",
        MACHINE,
        "    // says whether a pan got there (src/game/Camera.cpp).\n    SetSpeed(TurnSpeed(), fx);",
        "    // says whether a pan got there (src/game/Camera.cpp).",
    ),
    Mutant(
        "effects' time turned on mid-fight is never held",
        MACHINE,
        "        HoldTime(true, fx);",
        "        ;",
    ),
    Mutant(
        "a fight stood down begins again while the game still says combat",
        MACHINE,
        "    if (!settings_.enabled || stoodDown_)",
        "    if (!settings_.enabled)",
    ),
    Mutant(
        "standing down trusts what the machine believes of the speed",
        MACHINE,
        "    speedSet_ = 0.0f;",
        "    ;",
    ),
    Mutant(
        "standing down trusts what the machine believes of the clock",
        MACHINE,
        "    timeHeld_ = true;",
        "    ;",
    ),
    Mutant(
        "a whole number takes the digits off the front",
        JSON,
        " || got.ptr != text.data() + text.size()",
        "",
    ),
    Mutant(
        "a talent never reached is held on cooldown",
        MACHINE,
        "budget_.lastAbility >= 0 &&",
        "budget_.lastAbility >= 999999 &&",
    ),
    Mutant(
        "an enemy not fighting the party joins the fight mid-way",
        MACHINE,
        "        else if (!seen.down && !seen.engaged)",
        "        else if (!seen.down && !seen.engaged && seen.down)",
    ),
    Mutant(
        "an enemy not fighting the party is in the fight from its start",
        MACHINE,
        "return c.side != Side::Party && !c.engaged; });",
        "return c.side != Side::Party && !c.engaged && c.down; });",
    ),
    Mutant(
        "an enemy not fighting the party begins the turns again",
        MACHINE,
        "c.side == Side::Enemy && !c.down && c.engaged;",
        "c.side == Side::Enemy && !c.down;",
    ),
    Mutant(
        "one scan without a combatant puts it out of the fight",
        MACHINE,
        "            if (lastNow_ - since >= kAbsentMs)",
        "            if (lastNow_ - since >= kAbsentMs / 1000)",
    ),
    Mutant(
        "one that left the fight is up again by its health",
        MACHINE,
        "        else if (!left_.contains(c->id) || status.down)",
        "        else if (!left_.contains(kNoObject) || status.down)",
    ),
    # Issue 29: leaving the fight is the engine's perception. One the scan's
    # own rules no longer find fighting (out of combat, or beyond the radius)
    # stays while the party perceives it or it the party.
    Mutant(
        "one the scan no longer finds fighting is out of the fight, perceived or not: nobody is named for it",
        MACHINE,
        "                needs.inFight.push_back(c.id);",
        "",
    ),
    Mutant(
        "an enemy beyond the radius leaves the fight though the party perceives it",
        RULES,
        "        return perception.byParty || perception.ofParty;",
        "        return perception.ofParty;",
    ),
    Mutant(
        "an enemy beyond the radius leaves the fight though it perceives the party",
        RULES,
        "        return perception.byParty || perception.ofParty;",
        "        return perception.byParty;",
    ),
    Mutant(
        "an enemy stays in the party's fight for perceiving someone else",
        RULES,
        "        return perception.byParty || perception.ofParty;",
        "        return perception.byParty || perception.ofParty || perception.ofFoes;",
    ),
    Mutant(
        "an ally beyond the radius leaves the fight though it perceives an enemy",
        RULES,
        "    return perception.ofFoes;",
        "    return false;",
    ),
    Mutant(
        "an ally stays in the fight for being in the party's perception",
        RULES,
        "    if (side == Side::Enemy)\n    {\n        return perception.byParty || perception.ofParty;",
        "    if (side != Side::Party)\n    {\n        return perception.byParty || perception.ofParty;",
    ),
    Mutant(
        "one the scan finds fighting leaves the fight when nobody perceives it",
        RULES,
        "    if (side == Side::Party || fighting)",
        "    if (side == Side::Party || (fighting && side == Side::Party))",
    ),
    Mutant(
        "one neither found fighting nor perceived stays in the fight for being read",
        MACHINE,
        "return s.id == c.id && stays(s); }))",
        "return s.id == c.id; }))",
    ),
    Mutant(
        "being perceived brings one that is not in the fight into it",
        MACHINE,
        "            if (in == nullptr || in->down || !stays(seen))",
        "            if (in != nullptr && (in->down || !stays(seen)))",
    ),
    Mutant(
        "being perceived brings one that left the fight back into it",
        MACHINE,
        "            if (in == nullptr || in->down || !stays(seen))",
        "            if (in == nullptr || !stays(seen))",
    ),
    Mutant(
        "one kept in the fight by perception is said so with every scan",
        MACHINE,
        "            if (afar_.insert(seen.id).second)",
        "            if (!seen.fighting)",
    ),
    Mutant(
        "one back in the fight goes on with what it took up while it was out of it",
        MACHINE,
        "                // seed 165, 2026-10-04).\n                fx.push_back(Effect::Of(EffectKind::ClearCommands, seen.id));",
        "                // seed 165, 2026-10-04).",
    ),
    Mutant(
        "one found fighting again and lost again is not said to stay a second time",
        MACHINE,
        "        if (seen.fighting)\n        {\n            afar_.erase(seen.id);",
        "        if (seen.fighting)\n        {",
    ),
    Mutant(
        "the game's repeat of an order is told to the player",
        MACHINE,
        "lastNow_ - budget_.lastAt < kEchoMs;",
        "lastNow_ - budget_.lastAt < kEchoMs / 1000;",
    ),
    Mutant(
        "a creature outside the fight asks for a scan with every command",
        MACHINE,
        "            if (!outsiderScanNow_ || lastNow_ - *outsiderScanNow_ >= kOutsiderScanMs)",
        "            if (!outsiderScanNow_ || lastNow_ - *outsiderScanNow_ >= kOutsiderScanMs / 1000)",
    ),
    Mutant(
        "a held cooldown's sweep stays full",
        MACHINE,
        "kCooldownHoldSeconds * h.turns / h.of, kCooldownHoldSeconds));",
        "kCooldownHoldSeconds, kCooldownHoldSeconds));",
    ),
    Mutant(
        "an AI whose action was given back is not told to choose again",
        MACHINE,
        "    if (!player && budget_.spend.ActionsLeft() > 0 && budget_.refunded && !budget_.retried &&",
        "    if (!player && budget_.spend.ActionsLeft() > 9 && budget_.refunded && !budget_.retried &&",
    ),
    Mutant(
        "an AI whose action was given back is told again and again",
        MACHINE,
        "        budget_.retried = true;",
        "        ;",
    ),
    Mutant(
        "every refusal is an event",
        MACHINE,
        "lastNow_ - refusedEvent_->at < kRefusedEventMs;",
        "lastNow_ - refusedEvent_->at < kRefusedEventMs / 1000;",
    ),
    Mutant(
        "a creature in reach is stopped for shifting its feet",
        MACHINE,
        "budget_.spend.Walked() - budget_.movementAtCommand < kShuffle)",
        "budget_.spend.Walked() - budget_.movementAtCommand < 0.0f)",
    ),
    Mutant(
        "an action's walk goes on past its movement",
        MACHINE,
        "    budget_.movementAtCommand = budget_.spend.Walked();",
        "    budget_.movementAtCommand = budget_.spend.Walked() + 999.0f;",
    ),
    Mutant(
        "a door costs the action",
        RULES,
        "    case CommandType::PickupItem:\n        return CommandKind::Free;",
        "    case CommandType::PickupItem:\n        return CommandKind::Action;",
    ),
    Mutant(
        "a beast whose kin has a picture is rendered as a person is",
        PORTRAITS,
        "    if (person)\n    {\n        return {PortraitSource::Render, {}, false};",
        "    if (person || appearance > 0)\n    {\n        return {PortraitSource::Render, {}, false};",
    ),
    Mutant(
        "a creature gets the picture of another family",
        PORTRAITS,
        "PortraitFamily(row.label) != family)",
        "PortraitFamily(row.label) == family)",
    ),
    Mutant(
        "an action's own effect never gets its time",
        MACHINE,
        "ran >= kOwnEffectStepMs)",
        "ran >= kOwnEffectStepMs * 100000)",
    ),
    Mutant(
        "an action's effect time is spent again at the round's end",
        MACHINE,
        "            else\n            {\n                roundAgedMs_ += ran;",
        "            else\n            {\n                ;",
    ),
    Mutant(
        "a hold is taken for its creature's own action's effect too: effects' time runs twice with it",
        MACHINE,
        "!heldByEffect_ && timeHeld_ &&",
        "timeHeld_ &&",
    ),
    Mutant(
        "an action on its own effect's time is stopped at max_action_ms",
        MACHINE,
        "(ownEffectSince_ ? kOwnEffectPatience : 1);",
        "(ownEffectSince_ ? 1 : 1);",
    ),
    Mutant(
        "an action on its own effect's time is never stopped",
        MACHINE,
        "(ownEffectSince_ ? kOwnEffectPatience : 1);",
        "(ownEffectSince_ ? 1000 : 1);",
    ),
    Mutant(
        "a room shown by an order is looked into only at the turn's end",
        MACHINE,
        '    wantScan_ = true;\n    AwaitOrder("after its action");\n}',
        '    AwaitOrder("after its action");\n}',
    ),
    Mutant(
        "an attack through a shut door is let through",
        MACHINE,
        "        if (player && asked && request.doorInTheWay)",
        "        if (player && asked && request.doorInTheWay && request.inCombat && !request.inCombat)",
    ),
    # What the engine answers no to is refused before a step (issue 27;
    # tests/test_noattack.cpp): the game layer says which no it had
    # (CommandRequest::noAttack: no place, no path, no line for a shot), the
    # player is told why, and an AI's is given back and its AI told again.
    # Nothing of a no is remembered: the rule that did ("ruled out for the
    # rest of its turn") was withdrawn after play, and its mutants with it.
    Mutant(
        "an attack on a target with no place left at it is let through",
        MACHINE,
        "    if (request.noAttack == NoAttack::None)\n",
        "    if (request.noAttack == NoAttack::None || request.noAttack == NoAttack::NoPlace)\n",
    ),
    Mutant(
        "an attack with no path to its target is let through",
        MACHINE,
        "    if (request.noAttack == NoAttack::None)\n",
        "    if (request.noAttack == NoAttack::None || request.noAttack == NoAttack::NoPath)\n",
    ),
    Mutant(
        "a shot with no line to be had on the way is let through",
        MACHINE,
        "    if (request.noAttack == NoAttack::None)\n",
        "    if (request.noAttack == NoAttack::None || request.noAttack == NoAttack::NoLine)\n",
    ),
    Mutant(
        "an attack refused before a step is refused with no word over the character and no event",
        MACHINE,
        "        Refuse(who.id, std::wstring{words}, request.type, std::string{note});\n",
        "",
    ),
    Mutant(
        "an attack refused before a step does not say the engine's answer in its event's note",
        MACHINE,
        "        Refuse(who.id, std::wstring{words}, request.type, std::string{note});\n",
        "        Refuse(who.id, std::wstring{words}, request.type);\n",
    ),
    Mutant(
        "an attack refused before a step is not said on the bar's caption",
        MACHINE,
        "        notice_ = Notice{RefusedLine(words, who.name, request.type), lastNow_};\n",
        "",
    ),
    Mutant(
        "an AI refused before a step stands out its thinking time",
        MACHINE,
        "    if (phase_ == Phase::AiThinking)\n    {\n        phase_ = Phase::Settling;\n    }\n    return true;\n",
        "    return true;\n",
    ),
    Mutant(
        "an AI refused before a step is not told to choose again",
        MACHINE,
        "    budget_.refunded = true;\n    ++budget_.givenBack;\n    Log(LogLevel::Info,\n",
        "    ++budget_.givenBack;\n    Log(LogLevel::Info,\n",
    ),
    Mutant(
        "an AI refused before a step again and again never has its turn ended for it",
        MACHINE,
        "    budget_.refunded = true;\n    ++budget_.givenBack;\n    Log(LogLevel::Info,\n",
        "    budget_.refunded = true;\n    Log(LogLevel::Info,\n",
    ),
    Mutant(
        "an attack refused in an unaware hostile's beat is counted as given back, and its turn settles",
        MACHINE,
        "    if (unaware_ && phase_ == Phase::AiThinking)\n    {\n        Refused(LogLevel::Info,",
        "    if (unaware_ && phase_ == Phase::Idle)\n    {\n        Refused(LogLevel::Info,",
    ),
    # A kept attack is asked of the engine too, as it is given at its turn's
    # start (TurnMachine::KeptAttackNotMade).
    Mutant(
        "a kept attack the engine has no room for is given to its creature all the same",
        MACHINE,
        "        if (landing && request.type == CommandType::Attack && request.noAttack != NoAttack::None)\n",
        "        if (landing && request.type != CommandType::Attack && request.noAttack != NoAttack::None)\n",
    ),
    Mutant(
        "a kept attack not made keeps what its turn paid for it",
        MACHINE,
        "    if (const std::uint32_t paid = std::exchange(budget_.pendingPaidMs, 0); paid > 0)\n    {\n        budget_.spend.GiveActionBack(paid);\n",
        "    if (const std::uint32_t paid = std::exchange(budget_.pendingPaidMs, 0); paid > 0)\n    {\n",
    ),
    Mutant(
        "the player's kept attack is not made with no word over the character",
        MACHINE,
        "        Floaty(pending_, who.id, std::wstring{words}, kGrey);\n",
        "",
    ),
    Mutant(
        "the player's kept attack is not made with no word on the bar's caption",
        MACHINE,
        "        notice_ = Notice{NotMadeLine(words, who.name, request.type), lastNow_};\n",
        "",
    ),
    Mutant(
        "an AI's kept attack not made is not counted as given back: it is refused once more before its turn ends",
        MACHINE,
        "    else\n    {\n        ++budget_.givenBack;\n    }\n",
        "    else\n    {\n    }\n",
    ),
    Mutant(
        "a failed attack's line in the log does not say whom it was at",
        MACHINE,
        '                                    aimed.empty() ? std::string{} : " on " + aimed, why));\n',
        '                                    aimed.empty() ? " on " + aimed : std::string{}, why));\n',
    ),
    Mutant(
        "a creature standing at a walk is left to max_action_ms",
        MACHINE,
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs);",
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs * 1000);",
    ),
    Mutant(
        "a swing on the spot is taken for standing at a walk",
        MACHINE,
        "(at == CommandKind::Move || at == CommandKind::Free || short_))",
        "(at == CommandKind::Move || at == CommandKind::Free || short_ || at == CommandKind::Action))",
    ),
    Mutant(
        "an action that waited behind a walk is lost when it is stopped",
        MACHINE,
        "            if (waited)\n            {\n                GiveActionBack();",
        "            if (waited)\n            {\n",
    ),
    Mutant(
        "a fight made is not said to begin",
        MACHINE,
        '    Emit(EventKind::FightBegan);\n    Log(LogLevel::Info, std::format("the fight: {} combatants; {}"',
        '    Log(LogLevel::Info, std::format("the fight: {} combatants; {}"',
    ),
    Mutant(
        "a slow spell is cast on the turn",
        MACHINE,
        "    if (!(turn > 0.0f) || request.conjureSeconds < turn)",
        "    if (!(turn > 0.0f) || request.conjureSeconds <= turn)",
    ),
    Mutant(
        "a ritual is cast a turn early",
        MACHINE,
        "        if (--charge->turns <= 0)",
        "        if (--charge->turns <= 1)",
    ),
    Mutant(
        "a charged cast given again is taken for an order of the turn's",
        MACHINE,
        "    const bool landing = landing_ && landing_->caster == who->id && request.type == landing_->type &&",
        "    const bool landing = landing_ && landing_->caster != who->id && request.type == landing_->type &&",
    ),
    Mutant(
        "a caster held keeps its charge",
        MACHINE,
        "        if (!status.exists || status.down || status.condition == Condition::Disabled)",
        "        if (!status.exists || status.down)",
    ),
    Mutant(
        "a caster fallen keeps its charge",
        MACHINE,
        "        if (!status.exists || status.down || status.condition == Condition::Disabled)",
        "        if (!status.exists || status.condition == Condition::Disabled)",
    ),
    Mutant(
        "a cast that charges does not end its caster's turn",
        MACHINE,
        "    if (std::exchange(chargeBegan_, false) && Active() && order_.Current() != nullptr)",
        "    if (std::exchange(chargeBegan_, false) && !Active() && order_.Current() != nullptr)",
    ),
    Mutant(
        "an ability made to charge by the settings does not",
        MACHINE,
        "        if (ability == request.ability)\n        {\n            return turns;",
        "        if (ability != request.ability)\n        {\n            return turns;",
    ),
    Mutant(
        "an AI that spent its action after its charged cast is told again",
        MACHINE,
        "        if (player || budget_.spend.ActionsLeft() > 0)\n        {\n            Handover(*current, fx);",
        "        if (player || budget_.spend.ActionsLeft() > -9)\n        {\n            Handover(*current, fx);",
    ),
    Mutant(
        "the 0 key is given to the game as another key",
        KEYS,
        "    case '0':\n        return 0x0B;",
        "    case '0':\n        return 0x01;",
    ),
    Mutant(
        "F11 is given to the game as the key after F10",
        KEYS,
        "    case 0x7A: // F11\n        return 0x57;",
        "    case 0x7A: // F11\n        return 0x45;",
    ),
    Mutant(
        "a row of letters is given to the game a key off",
        KEYS,
        "constexpr std::uint32_t firsts[] = {0x10, 0x1E, 0x2C};",
        "constexpr std::uint32_t firsts[] = {0x10, 0x1D, 0x2C};",
    ),
    Mutant(
        "a key the game's bindings are not known to take is given to them",
        KEYS,
        "    default:\n        return 0;\n    }\n}",
        "    default:\n        return key;\n    }\n}",
    ),
    Mutant(
        "a path as long as the movement left is shown too far",
        PREVIEW,
        "        if (!out && split.length + step > reach)",
        "        if (!out && split.length + step >= reach)",
    ),
    Mutant(
        "the preview's cut is put past where the movement runs out",
        PREVIEW,
        "Between(from, to, (reach - split.length) / step)",
        "Between(from, to, (reach + split.length) / step)",
    ),
    Mutant(
        "the preview's red part does not begin where the gold ends",
        PREVIEW,
        "            split.beyond.push_back(cut);",
        "            split.beyond.push_back(to);",
    ),
    Mutant(
        "the preview's ring is not round its centre",
        PREVIEW,
        "centre.y + std::sin(angle) * radius, centre.z}",
        "centre.y + std::cos(angle) * radius, centre.z}",
    ),
    Mutant(
        "the preview's rings are left out of its string",
        PREVIEW,
        "    AppendDots(out, view.stopRing);\n",
        "",
    ),
    Mutant(
        "a walk's step across a corner is not seen to leave the path shown",
        PREVIEW,
        "            const float middle = ToPath(Between(walked[i - 1], walked[i], 0.5f), shown);",
        "            const float middle = ToPath(walked[i], shown);",
    ),
    Mutant(
        "a walk is measured from the path's lines, not its steps",
        PREVIEW,
        "        along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);",
        "        along = along < 0.0f ? 0.0f : along;",
    ),
    Mutant(
        "marks are laid past the reach asked for",
        PREVIEW,
        "        while (step > 0.0f && next <= walked + step && next <= to)",
        "        while (step > 0.0f && next <= walked + step)",
    ),
    Mutant(
        "marks do not go round a corner",
        PREVIEW,
        "            marks.push_back(Between(path[i - 1], path[i], (next - walked) / step));",
        "            marks.push_back(Between(path[0], path[1], (next - walked) / step));",
    ),
    Mutant(
        "a preview marked in the world still has the movie draw its line",
        PREVIEW,
        "view.line ? 1 : 0,",
        "view.line ? 1 : 1,",
    ),
    Mutant(
        "a preview too far is said to be in reach",
        PREVIEW,
        "view.beyond.empty() ? 1 : 2",
        "view.beyond.empty() ? 2 : 1",
    ),
    Mutant(
        "a stun in a creature's own turn is waited out",
        MACHINE,
        "                currentStunned_ = true;\n",
        "",
    ),
    Mutant(
        "a conversation between two turns resumes a turn never begun",
        MACHINE,
        "resume_ = turnBegun_;",
        "resume_ = true;",
    ),
    Mutant(
        "the first turn begins before anyone's conditions are read",
        MACHINE,
        "stunned as the fight began is held.\n    phase_ = Phase::TurnStart;\n    wantScan_ = true;",
        "stunned as the fight began is held.\n    phase_ = Phase::TurnStart;\n    StartNextTurn(fx);",
    ),
    Mutant(
        "an ability's cooldown is never held",
        MACHINE,
        "        if (turns > 0)\n        {\n            // With time, its own seconds",
        "        if (turns > 99)\n        {\n            // With time, its own seconds",
    ),
    Mutant(
        "a held cooldown is released a turn early",
        MACHINE,
        "        if (--h.turns <= 0)",
        "        if (--h.turns <= 1)",
    ),
    Mutant(
        "a sustained ability's toggle costs the action",
        MACHINE,
        "    {\n        kind = CommandKind::Free;\n    }",
        "    {\n        kind = CommandKind::Action;\n    }",
    ),
    Mutant(
        "a sustained ability turned off can be turned on again in the same turn",
        MACHINE,
        "? (request.sustainedOn ? 1 : 0)",
        "? 0",
    ),
    Mutant(
        "held cooldowns stay held after the fight",
        MACHINE,
        "    HoldUsed(fx);\n    ReleaseCooldowns(fx);",
        "    HoldUsed(fx);",
    ),
    Mutant(
        "a character's own AI spends the player's turn",
        MACHINE,
        "    if (player && !landing && !approaching && request.source == Source::Script &&",
        "    if (player && !landing && !approaching && request.source == Source::Unknown &&",
    ),
    Mutant(
        "a character's own AI changes its weapons on the player's turn",
        MACHINE,
        "        request.type == CommandType::SwitchWeaponSets || "
        "(request.type == CommandType::UseAbility && request.sustained);",
        "        (request.type == CommandType::UseAbility && request.sustained);",
    ),
    Mutant(
        "a character's own AI turns its modes on and off on the player's turn",
        MACHINE,
        "        request.type == CommandType::SwitchWeaponSets || "
        "(request.type == CommandType::UseAbility && request.sustained);",
        "        request.type == CommandType::SwitchWeaponSets;",
    ),
    Mutant(
        "a character's own AI waits on the player's turn, and the turn never waits on an order",
        MACHINE,
        "(kind == CommandKind::Action || kind == CommandKind::Move || kind == CommandKind::Pass || gear))",
        "(kind == CommandKind::Action || kind == CommandKind::Move || gear))",
    ),
    Mutant(
        "a kept action dropped by a hold is dropped a frame late, and takes the next one with it",
        MACHINE,
        'Interrupt(status.id, !status.exists ? "gone" : status.down ? "fell" : "held", fx);',
        # (fx named, so that the mutant compiles under /W4 /WX.)
        'Interrupt(status.id, !status.exists ? "gone" : status.down ? "fell" : "held", (static_cast<void>(fx), pending_));',
    ),
    Mutant(
        "the machine's patience with the engine shortens as the fight's speed rises",
        MACHINE,
        "std::lround(static_cast<float>(ms) * std::max(1.0f, speedSet_))",
        "std::lround(static_cast<float>(ms))",
    ),
    Mutant(
        "an action that never ends is given the fast clock's patience, not the wall's",
        MACHINE,
        "Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * (ownEffectSince_ ? kOwnEffectPatience : 1);",
        "static_cast<std::uint32_t>(settings_.maxActionMs) * (ownEffectSince_ ? kOwnEffectPatience : 1);",
    ),
    Mutant(
        "those that waited outside a fight are never told it is over",
        MACHINE,
        "    for (const ObjectId id : outside_)\n    {\n        fx.push_back(Effect::Of(EffectKind::PokeAi, id));\n    }\n",
        "",
    ),
    Mutant(
        "the toggle has Ctrl+T again before the player gives it a key",
        "src/core/Settings.h",
        "KeyChord toggleKey{0, false, false, false}; // None",
        "KeyChord toggleKey{0x54, true, false, false}; // None",
    ),
    Mutant(
        "a plot's timers run on the world's clock unless the player asks",
        "src/core/Settings.h",
        "    float scriptTimersFrom = 1.0f;",
        "    float scriptTimersFrom = 0.0f;",
    ),
    Mutant(
        "every delayed script event is said in the log unasked",
        "src/core/Settings.h",
        "    bool scriptTimersTrace = false;",
        "    bool scriptTimersTrace = true;",
    ),
    Mutant(
        "Delay has a key before the player gives it one",
        "src/core/Settings.h",
        "KeyChord delayKey{0, false, false, false}; // None",
        "KeyChord delayKey{0x44, false, false, false}; // None",
    ),
    Mutant(
        "the bar's row holds twelve again, its buttons under the minimap",
        "src/core/TurnMachine.h",
        "static constexpr std::size_t kShown = 10;",
        "static constexpr std::size_t kShown = 12;",
    ),
    # The bar's row among the turns to come, and its arrows (issue 23).
    Mutant(
        "the view is cut to ten turns again: those past them are on no card and in no count",
        MACHINE,
        "order_.Upcoming(order_.All().size())",
        "order_.Upcoming(TurnView::kShown)",
    ),
    Mutant(
        "one before the row falls and the row stays: it shows other fighters",
        BARWINDOW,
        "    first_ -= std::min(first_, gone);\n",
        "    first_ -= std::min(first_, gone * 0);\n",
    ),
    Mutant(
        "one whose turn has passed counts as gone: the row follows its fighters round the list",
        BARWINDOW,
        "return std::ranges::find(view.upcoming, id, &CardView::id) == view.upcoming.end();",
        "return view.upcoming.empty() || view.upcoming.front().id != id;",
    ),
    Mutant(
        "the row runs past the list's end when the list gets shorter",
        BARWINDOW,
        "    first_ = std::min(first_, LastFirst(view.upcoming.size()));\n",
        "",
    ),
    Mutant(
        "outside a fight the row keeps its place while the view still holds cards",
        BARWINDOW,
        "if (!view.active)",
        "if (!view.active && view.upcoming.empty())",
    ),
    Mutant(
        "an arrow's click moves the row two cards",
        "src/core/BarWindow.h",
        "static constexpr std::size_t kStep = 1;",
        "static constexpr std::size_t kStep = 2;",
    ),
    Mutant(
        "the left arrow takes the row past the list's beginning",
        BARWINDOW,
        "first_ - std::min(first_, by)",
        "first_ - by",
    ),
    Mutant(
        "the right arrow takes the row past the list's end, unseen",
        BARWINDOW,
        "std::min(LastFirst(ids_.size()), first_ + by)",
        "first_ + by",
    ),
    Mutant(
        "the right arrow shows with nobody after the row",
        BARWINDOW,
        "span.after = turns - span.before - span.shown;",
        "span.after = turns - span.before;",
    ),
    Mutant(
        "portraits are rendered for every fighter, the row's or not",
        PORTRAITS,
        "    const std::size_t to = std::min(from + row.shown, view.upcoming.size());\n",
        "    const std::size_t to = view.upcoming.size();\n",
    ),
    Mutant(
        "portraits are got for the list's first ten wherever the row stands",
        PORTRAITS,
        "    const std::size_t from = std::min(row.before, view.upcoming.size());\n",
        "    const std::size_t from = 0;\n",
    ),
    Mutant(
        "the bar's string says the row is at the beginning wherever it stands",
        BAR,
        "    Field(out, static_cast<long long>(row.before));\n",
        "    Field(out, 0LL);\n",
    ),
    Mutant(
        "the bar's string does not say how many come after the row",
        BAR,
        "    Field(out, static_cast<long long>(row.after));\n",
        "    Field(out, 0LL);\n",
    ),
    Mutant(
        "those of the fight still standing when it ends are never told it is over",
        MACHINE,
        "        if (c.side != Side::Party && !c.down && !held)",
        "        if (c.side != Side::Party && c.down && !held)",
    ),
    Mutant(
        "the party's own AI is told the fight is over",
        MACHINE,
        "        if (c.side != Side::Party && !c.down && !held)",
        "        if (!c.down && !held)",
    ),
    Mutant(
        "a turn that goes on after its action does not say it waits on an order",
        MACHINE,
        '    AwaitOrder("after its action");',
        "    phase_ = Phase::AwaitingOrder;",
    ),
    Mutant(
        "a turn handed to the player does not say it waits on an order",
        MACHINE,
        '        AwaitOrder("the turn is its player\'s");',
        "        phase_ = Phase::AwaitingOrder;",
    ),
    Mutant(
        "a battle in sight that is not the party's joins its fight however far",
        RULES,
        "    return sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
        "    return sighting.seen;",
    ),
    Mutant(
        "a creature near the party joins its fight unseen",
        RULES,
        "    return sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
        "    return std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn into a fight with the party at peace",
        RULES,
        "    if (!partyInCombat)\n    {\n        return false;\n    }\n    if (drawnIn)",
        "    if (!partyInCombat && (drawnIn || someoneToFight))\n    {\n        return false;\n    }\n    if (drawnIn)",
    ),
    Mutant(
        "one drawn in unawares stays in past the party's own combat",
        RULES,
        "    if (!partyInCombat)\n    {\n        return false;\n    }\n    if (drawnIn)",
        "    if (!partyInCombat && !drawnIn)\n    {\n        return false;\n    }\n    if (drawnIn)",
    ),
    Mutant(
        "a hostile that has not noticed is drawn in unseen",
        RULES,
        "    return !someoneToFight && sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
        "    return !someoneToFight && (sighting.seen || true) && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn in from beyond the join radius",
        RULES,
        "    return !someoneToFight && sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
        "    return !someoneToFight && sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.combatRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn into a fight that has an enemy",
        RULES,
        "    return !someoneToFight && sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius;",
        "    return sighting.seen && std::isfinite(sighting.meters) && sighting.meters <= settings.joinRadius + (someoneToFight ? 0.0f : 0.0f);",
    ),
    Mutant(
        "one drawn in unawares is dropped as another notices",
        RULES,
        "    if (drawnIn)\n    {\n        return true;\n    }\n    return !someoneToFight",
        "    if (drawnIn && !someoneToFight)\n    {\n        return true;\n    }\n    return !someoneToFight",
    ),
    Mutant(
        "a creature attacking the party from afar waits outside its fight",
        RULES,
        "    if (!settings.joinBySight || sighting.fightsParty)",
        "    if (!settings.joinBySight)",
    ),
    # Issue 42: the party is where its living members are. Who stands for it
    # where one creature must, who a scan measures from, whether it is in
    # combat, and how far a creature is from it.
    Mutant(
        "a fallen leader stands for the party",
        RULES,
        "return member.id == id && member.alive; });",
        "return member.id == id; });",
    ),
    Mutant(
        "a living leader gives way to the one the player controls",
        RULES,
        "    if (living(leader))\n    {\n        return leader;",
        "    if (living(leader) && !living(controlled))\n    {\n        return leader;",
    ),
    Mutant(
        "with the leader down the first of the party living stands for it, whoever the player controls",
        RULES,
        "    if (living(controlled))\n    {\n        return controlled;",
        "    if (living(controlled) && false)\n    {\n        return controlled;",
    ),
    Mutant(
        "with the leader and the one controlled down a fallen member stands for the party",
        RULES,
        "std::ranges::find_if(party, [](const PartyMember &member) { return member.alive; });",
        "std::ranges::find_if(party, [](const PartyMember &member) { return member.id != kNoObject; });",
    ),
    Mutant(
        "a word to the player is over the leader, whoever the player controls",
        RULES,
        "    return StandsForTheParty(party, controlled, leader);",
        "    return StandsForTheParty(party, leader, controlled);",
    ),
    Mutant(
        "the party is measured from its fallen too",
        RULES,
        "        if (member.alive && std::ranges::find(living, member.id) == living.end())",
        "        if (std::ranges::find(living, member.id) == living.end())",
    ),
    Mutant(
        "the party is measured from the one who stands for it alone",
        RULES,
        "            living.push_back(member.id);",
        "            (void)member;",
    ),
    Mutant(
        "the one who stands for the party is not the first a scan asks",
        RULES,
        "        living.push_back(stands);",
        "        (void)stands;",
    ),
    Mutant(
        "a fallen member's combat state is the party's",
        RULES,
        "return member.alive && member.inCombat; });",
        "return member.inCombat; });",
    ),
    Mutant(
        "the party is in combat only when every member of it is",
        RULES,
        "    return std::ranges::any_of(party, [](const PartyMember &member) { return member.alive && member.inCombat; });",
        "    return std::ranges::all_of(party, [](const PartyMember &member) { return member.alive && member.inCombat; });",
    ),
    Mutant(
        "a creature is as far from the party as from the first of its living",
        RULES,
        "        if (meters < nearest)\n        {\n            nearest = meters;",
        "        if (meters < nearest && !std::isfinite(nearest))\n        {\n            nearest = meters;",
    ),
    Mutant(
        "a fight's reach is the join radius",
        RULES,
        "    return metersFromTheParty <= settings.combatRadius;",
        "    return metersFromTheParty <= settings.joinRadius;",
    ),
    Mutant(
        "a creature beyond combat_radius of every living member is within the fight's reach",
        RULES,
        "    return metersFromTheParty <= settings.combatRadius;",
        "    return metersFromTheParty <= settings.combatRadius || metersFromTheParty > 0.0f;",
    ),
    # And where the machine reaches for the hero, who may have fallen.
    Mutant(
        "a fallen hero is the one selected while a companion's tactics play its turn",
        MACHINE,
        "hero != nullptr && standing(*hero))\n    {\n        return hero_;",
        "hero != nullptr && hero->id != except)\n    {\n        return hero_;",
    ),
    Mutant(
        "with the hero down nobody is selected in a companion's place, and a fight's banner is over nobody",
        MACHINE,
        "    return other != all.end() ? other->id : kNoObject;",
        "    return kNoObject;",
    ),
    Mutant(
        "with the hero down the companion whose tactics play it stays the one selected",
        MACHINE,
        "return c.side == Side::Party && !c.down && c.id != except; };",
        "return c.side == Side::Party && !c.down && (c.id != except || true); };",
    ),
    Mutant(
        "with the hero down an enemy is selected in her place",
        MACHINE,
        "return c.side == Side::Party && !c.down && c.id != except; };",
        "return !c.down && c.id != except; };",
    ),
    Mutant(
        "a fight's banner floats over the hero's body",
        MACHINE,
        '    Floaty(fx, PartysOwn(), L"Turn-based combat", kGold);',
        '    Floaty(fx, hero_, L"Turn-based combat", kGold);',
    ),
    Mutant(
        "the hero is selected for a companion's tactics whether she stands or not",
        MACHINE,
        "    if (const ObjectId other = who.side == Side::Party ? PartysOwn(who.id) : kNoObject; other != kNoObject)",
        "    if (const ObjectId other = who.side == Side::Party && hero_ != who.id ? hero_ : kNoObject; other != kNoObject)",
    ),
    # Issue 29: a hostile that has noticed nobody to fight (the engine's
    # perception) is not told its turn has come: "Unaware", a beat, the next
    # turn.
    Mutant(
        "an unaware hostile is told its turn has come, and waited on",
        MACHINE,
        "    if (UnawareNow(who))\n    {\n        unaware_ = true;",
        "    if (false)\n    {\n        unaware_ = true;",
    ),
    Mutant(
        "an unaware hostile's turn is skipped at once, with no beat",
        MACHINE,
        "        deadline_ = clock_ + kUnawareMs;",
        "        deadline_ = clock_;",
    ),
    Mutant(
        "an unaware hostile's turn never ends",
        MACHINE,
        '        if (clock_ >= deadline_)\n        {\n            EndTurn(fx, "unaware");',
        '        if (false)\n        {\n            EndTurn(fx, "unaware");',
    ),
    Mutant(
        "Unaware is not said over a hostile whose turn is skipped",
        MACHINE,
        '        Floaty(fx, who.id, L"Unaware", kGrey);',
        "",
    ),
    Mutant(
        "an unaware hostile's name floats beside Unaware",
        MACHINE,
        "    else if (!UnawareNow(who))",
        "    else",
    ),
    Mutant(
        "an unaware hostile that notices in its beat is not told until its next turn",
        MACHINE,
        "        if (current != nullptr && !UnawareNow(*current))",
        "        if (current != nullptr && !UnawareNow(*current) && current == nullptr)",
    ),
    Mutant(
        "an unaware hostile that notices in its beat is told on a look half a second old",
        MACHINE,
        "            if (lastStatusNow_ != lastNow_)\n            {\n                statusWanted_ = true;\n                return;",
        "            if (lastStatusNow_ != lastNow_ && current == nullptr)\n            {\n                statusWanted_ = true;\n                return;",
    ),
    Mutant(
        "whether the acting creature has noticed is the scan's word, not the frame's",
        MACHINE,
        "    return Unaware(who.side, looked ? seenActor_->perception : who.perception);",
        "    return Unaware(who.side, looked ? who.perception : who.perception);",
    ),
    Mutant(
        "one that perceives nobody any more goes on with its turn until its script says so, or its thinking runs out",
        MACHINE,
        "        const bool seen = looked && NothingToFight(current->side, obs.actor->perception);",
        "        const bool seen = looked && NothingToFight(current->side, obs.actor->perception) && false;",
    ),
    Mutant(
        "a command of its own in an unaware hostile's beat leaves its turn to be skipped under it",
        MACHINE,
        "    // script's order): its turn is not skipped under it.\n    unaware_ = false;",
        "    // script's order): its turn is not skipped under it.",
    ),
    Mutant(
        "a wait of its script's ends an unaware hostile's turn as its own choice",
        MACHINE,
        "        if (!player && unaware_)\n        {\n            return Verdict::Block;",
        "        if (!player && unaware_ && who == nullptr)\n        {\n            return Verdict::Block;",
    ),
    Mutant(
        "a hostile that perceives a foe that is not the party's is taken for unaware",
        RULES,
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofParty && !perception.ofFoes;",
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofParty;",
    ),
    Mutant(
        "a hostile that perceives the party is taken for unaware, perceiving no other foe",
        RULES,
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofParty && !perception.ofFoes;",
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofFoes;",
    ),
    Mutant(
        "a hostile the game has in combat is skipped as unaware when it perceives nobody",
        RULES,
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofParty && !perception.ofFoes;",
        "    return side == Side::Enemy && !perception.ofParty && !perception.ofFoes;",
    ),
    Mutant(
        "an ally that perceives nobody has its turn skipped as unaware",
        RULES,
        "    return side == Side::Enemy && !perception.inCombat && !perception.ofParty && !perception.ofFoes;",
        "    return side != Side::Party && !perception.inCombat && !perception.ofParty && !perception.ofFoes;",
    ),
    Mutant(
        "effects are never aged a round",
        MACHINE,
        "        e.b = static_cast<std::int32_t>(left);\n        fx.push_back(e);",
        "        e.b = static_cast<std::int32_t>(left);",
    ),
    Mutant(
        "effects' time stays held after the fight",
        MACHINE,
        "    ReleaseCooldowns(fx);\n    HoldTime(false, fx);",
        "    ReleaseCooldowns(fx);",
    ),
    Mutant(
        "a file from an older build never gets the newer settings",
        SETTINGS,
        "        if (!HasKey(out, entry.section, entry.key))",
        "        if (!HasKey(out, entry.section, entry.key) && entry.key.empty())",
    ),
    Mutant(
        "a setting the player commented out is added back",
        SETTINGS,
        "            trimmed = Trim(trimmed.substr(1));",
        "            break;",
    ),
    Mutant(
        "a talent for its user alone asks for a target",
        RULES,
        "    return useType != kUseTypeMode && (targetType & ~kTargetSelf) != 0;",
        "    return useType != kUseTypeMode && targetType != 0;",
    ),
    Mutant(
        "a mode asks for a target",
        RULES,
        "    return useType != kUseTypeMode && (targetType & ~kTargetSelf) != 0;",
        "    return useType != kUseTypeMode + 99 && (targetType & ~kTargetSelf) != 0;",
    ),
    Mutant(
        "a refused order still runs the world",
        MACHINE,
        "        orderPending_ = false;\n        if (verdict != Verdict::Allow && phase_ == Phase::Dispatching)",
        "        if (verdict != Verdict::Allow && phase_ == Phase::Dispatching)",
    ),
    Mutant(
        "a refusal while an order runs the world lets it run on",
        MACHINE,
        "if (verdict != Verdict::Allow && phase_ == Phase::Dispatching)",
        "if (verdict != Verdict::Allow && phase_ == Phase::Idle)",
    ),
    Mutant(
        "an order runs the world though order_run_ms is off",
        MACHINE,
        "if (settings_.orderRunMs <= 0)",
        "if (settings_.orderRunMs < 0)",
    ),
    Mutant(
        "a portrait is sent without its extension, and drawn grey",
        PORTRAITS,
        "if (!name.empty() && name.find(L'.') == std::wstring::npos)",
        "if (name.empty())",
    ),
    Mutant(
        "rendered portraits are never released",
        PORTRAITS,
        "    releasing_.insert(releasing_.end(), rendered_.begin(), rendered_.end());\n",
        "",
    ),
    Mutant(
        "the party's own portraits are released too",
        PORTRAITS,
        "if (!url.empty() && (rendered || std::ranges::find(releasing_, id) != releasing_.end()))",
        "if (!url.empty() && (rendered || !rendered))",
    ),
    Mutant(
        "portraits are released while the movie may still show them",
        PORTRAITS,
        "if (releasing_.empty() || (!movieLetGo_ && nowMs - releasingSince_ < kReleaseWaitMs))",
        "if (releasing_.empty() || (!movieLetGo_ && nowMs < releasingSince_))",
    ),
    Mutant(
        "a portrait a new fight shows again is released under it",
        PORTRAITS,
        "if (!urls_.contains(id) && std::ranges::find(out, id) == out.end())",
        "if (std::ranges::find(out, id) == out.end())",
    ),
    Mutant(
        "portraits are asked for again and again",
        PORTRAITS,
        "if (card.id != kNoObject && !urls_.contains(card.id))",
        "if (card.id != kNoObject)",
    ),
    Mutant(
        "portraits are rendered in a conversation",
        PORTRAITS,
        "if (view.phase == Phase::Idle || view.phase == Phase::Suspended)",
        "if (view.phase == Phase::Idle)",
    ),
    Mutant(
        "portraits outlive their fight",
        PORTRAITS,
        "    rendered_.clear();\n    urls_.clear();\n",
        "    rendered_.clear();\n",
    ),
    Mutant(
        "the bar's string leaves the portraits out",
        BAR,
        "        Field(out, std::wstring_view{TextureName(portraits.Of(card.id))});\n",
        "        (void)portraits;\n",
    ),
    # A turn as a bar of time (Settings::turnClock, dev/REBALANCE.md).
    Mutant(
        "turnbudget lets a kept action's creature walk its free meter",
        "src/core/TurnBudget.cpp",
        "    freeWalked_ = std::max(freeWalked_, free_);\n    credit_ = 0.0f;",
        "    credit_ = 0.0f;",
    ),
    Mutant(
        "turnbudget charges an attack's prepaid walk again as it is walked",
        "src/core/TurnBudget.cpp",
        "    const float credited = std::min(meters, credit_);\n    credit_ -= credited;",
        "    const float credited = 0.0f;\n    credit_ -= credited;",
    ),
    Mutant(
        "turnbudget walks without paying past the free meter",
        "src/core/TurnBudget.cpp",
        "    const TimePayment paid = time_.Spend(wholeMs);",
        "    const TimePayment paid{};",
    ),
    Mutant(
        "time: an attack's walk is not paid as it is let through",
        MACHINE,
        "    budget_.spend.Prepay(walk);\n    const std::uint32_t owed = budget_.spend.SpendAction(cost);",
        "    const std::uint32_t owed = budget_.spend.SpendAction(cost);",
    ),
    Mutant(
        "time: an action is kept by its own time alone, its walk forgotten",
        MACHINE,
        "    if (walkMs + cost > left && keepable)",
        "    if (cost > left && keepable)",
    ),
    Mutant(
        "time: a kept action ends its turn under a command still running",
        MACHINE,
        "        chargeBegan_ = phase_ != Phase::Executing;",
        "        chargeBegan_ = true;",
    ),
    Mutant(
        "time: a kept attack is not known as it lands",
        MACHINE,
        "request.type == landing_->type &&",
        "request.type == CommandType::UseAbility &&",
    ),
    Mutant(
        "time: what a kept action owes is not paid as its turn begins",
        MACHINE,
        "        (void)budget_.spend.SpendAction(owed);\n        budget_.pendingPaidMs = owed;",
        "        budget_.pendingPaidMs = owed;",
    ),
    Mutant(
        "time: nothing rolls over",
        MACHINE,
        "            rollover_[current->id] = rolled;",
        "            rollover_[current->id] = 0;",
    ),
    Mutant(
        "time: rollover's cap ignores Combat Tactics",
        MACHINE,
        "budget_.spend.Finish(TimeFromTactics(TacticsOf(*current)).rolloverCapMs);",
        "budget_.spend.Finish(1000);",
    ),
    # Issue 29: time rolls over only from a turn its actor ended itself; each
    # reason a turn ends for has its class in core/TurnEnd.cpp.
    Mutant(
        "time: a turn its actor did not end rolls its time over",
        MACHINE,
        "            const std::uint32_t rolled = saved ? unspent : 0;",
        "            const std::uint32_t rolled = unspent;",
    ),
    Mutant(
        "time: an unaware hostile's skipped turn rolls its time over",
        TURNEND,
        'TurnEnd{"unaware", false},',
        'TurnEnd{"unaware", true},',
    ),
    Mutant(
        "time: an AI that chose nothing rolls its time over",
        TURNEND,
        'TurnEnd{"chose nothing", false},',
        'TurnEnd{"chose nothing", true},',
    ),
    Mutant(
        "time: a turn stopped as too long rolls its time over",
        TURNEND,
        'TurnEnd{"took too long", false},',
        'TurnEnd{"took too long", true},',
    ),
    Mutant(
        "time: an AI with nothing it can get to rolls its time over",
        TURNEND,
        'TurnEnd{"nothing it can get to", false},',
        'TurnEnd{"nothing it can get to", true},',
    ),
    Mutant(
        "time: an AI stopped where its movement ran out rolls time over",
        TURNEND,
        'TurnEnd{"out of movement", false},',
        'TurnEnd{"out of movement", true},',
    ),
    Mutant(
        "time: a creature that fell in its own turn rolls its time over",
        TURNEND,
        'TurnEnd{"fell", false},',
        'TurnEnd{"fell", true},',
    ),
    Mutant(
        "time: a turn lost to an effect that holds its creature rolls its time over",
        TURNEND,
        'TurnEnd{"held by an effect", false},',
        'TurnEnd{"held by an effect", true},',
    ),
    Mutant(
        "time: an AI that is done with time left rolls nothing over",
        TURNEND,
        'TurnEnd{"done", true},',
        'TurnEnd{"done", false},',
    ),
    Mutant(
        "time: an AI that waits rolls nothing over",
        TURNEND,
        'TurnEnd{"waits", true},',
        'TurnEnd{"waits", false},',
    ),
    Mutant(
        "time: a turn the player ended rolls nothing over",
        TURNEND,
        'TurnEnd{"ended by the player", true},',
        'TurnEnd{"ended by the player", false},',
    ),
    Mutant(
        "time: a turn ended for a reason with no class rolls its time over",
        TURNEND,
        "    return EndedByItsActor(why).value_or(false);",
        "    return EndedByItsActor(why).value_or(true);",
    ),
    Mutant(
        "time: enemies have no Combat Tactics by rank",
        MACHINE,
        "    if (who.side != Side::Party && who.rank >= kCritter && who.rank <= kEliteBoss)",
        "    if (who.side == Side::Ally && who.rank >= kCritter && who.rank <= kEliteBoss)",
    ),
    Mutant(
        "time: an AI is not told to choose again with time left",
        MACHINE,
        "        if (Timed() && budget_.spend.ActionsLeft() > 0 && !budget_.outOfMovement)",
        "        if (Timed() && budget_.spend.ActionsLeft() > 99 && !budget_.outOfMovement)",
    ),
    Mutant(
        "time: an AI's wait ends its turn, whatever is left of its bar",
        MACHINE,
        "        if (!player && PayWait(*who, request))",
        "        if (false)",
    ),
    Mutant(
        "time: a wait paid for is let through, and runs as well",
        MACHINE,
        "        if (!player && PayWait(*who, request))\n        {\n            return Verdict::Block;",
        "        if (!player && PayWait(*who, request))\n        {\n            return Verdict::Allow;",
    ),
    Mutant(
        "time: a wait takes nothing of the bar",
        MACHINE,
        "    (void)budget_.spend.SpendAction(cost); // it fits: nothing of it is owed\n",
        "\n",
    ),
    Mutant(
        "time: a wait longer than what is left is paid for with what there is",
        MACHINE,
        "    if (cost > left)\n    {",
        "    if (false)\n    {",
    ),
    Mutant(
        "time: an AI that waited is left thinking, and its turn ends having chosen nothing",
        MACHINE,
        "    if (phase_ == Phase::AiThinking)\n    {\n        phase_ = Phase::Settling;\n        deadline_ = clock_;",
        "    if (false)\n    {\n        phase_ = Phase::Settling;\n        deadline_ = clock_;",
    ),
    Mutant(
        "time: the time a wait takes has not passed on its creature's clock as it chooses again",
        MACHINE,
        "BudgetLeft()));\n    MoveClock(pending_);\n    // Its answer to being told to choose",
        "BudgetLeft()));\n    // Its answer to being told to choose",
    ),
    Mutant(
        "time: a wait of no length is paid for, and chosen until the turn's commands run out",
        MACHINE,
        "    if (cost == 0)\n    {\n        Log(LogLevel::Info, std::format(\"{}'s wait has no length",
        "    if (false)\n    {\n        Log(LogLevel::Info, std::format(\"{}'s wait has no length",
    ),
    Mutant(
        "time: a wait counts as a command let through, and the camera never goes to a creature that waited first",
        MACHINE,
        '        std::format("{} waits {:.2f} s; {}", Utf8(who.name), static_cast<float>(cost) / 1000.0f, BudgetLeft()));\n',
        '        std::format("{} waits {:.2f} s; {}", Utf8(who.name), static_cast<float>(cost) / 1000.0f, BudgetLeft()));\n    ++budget_.commands;\n',
    ),
    Mutant(
        "a wait is reckoned against a bar with one action a turn, where there is none",
        MACHINE,
        "    if (!Timed())\n    {\n        return false; // as built a turn is one action",
        "    if (false)\n    {\n        return false; // as built a turn is one action",
    ),
    Mutant(
        "time: a failed action's time is not given back",
        MACHINE,
        "    return budget_.spend.Timed() ? budget_.lastCostMs > 0 : budget_.spend.ActionsLeft() == 0;",
        "    return budget_.spend.ActionsLeft() == 0;",
    ),
    Mutant(
        "time: a turn with its bar spent waits on its free meter",
        MACHINE,
        "    if (settings_.endTurnWhenActionUsed || budget_.spend.Timed())",
        "    if (settings_.endTurnWhenActionUsed)",
    ),
    Mutant(
        "time: a turn ended over a conversation is resumed for the next creature",
        MACHINE,
        "    // next is begun, not resumed (test_fights.cpp, seed 594 with time).\n    resume_ = false;",
        "    // next is begun, not resumed (test_fights.cpp, seed 594 with time).",
    ),
    Mutant(
        "time: a turn spent whole on what a kept action owes is not",
        MACHINE,
        "        if (charge->owedMs <= bar.CapacityLeftMs())",
        "        if (true)",
    ),
    Mutant(
        "time: a mode turned off costs time",
        MACHINE,
        "                return 0; // a mode turned off",
        "                seconds = kStandInAbilitySeconds;\n                break; // a mode turned off",
    ),
    Mutant(
        "time: a spell's conjuring is free",
        MACHINE,
        "ConjureSeconds(request.conjureAnim, static_cast<int>(request.conjureSeconds), request.staff);",
        "0.0f * ConjureSeconds(request.conjureAnim, static_cast<int>(request.conjureSeconds), request.staff);",
    ),
    Mutant(
        "rules: a swing ignores the attack speed modifier",
        RULES,
        "    const float modifier = sensible ? std::min(1.5f, attackSpeedModifier) : 1.0f;",
        "    const float modifier = sensible ? 1.0f : 1.0f;",
    ),
    Mutant(
        "rules: a conjuring ignores its loops",
        RULES,
        "    return start + loop * static_cast<float>(loops);",
        "    return start + loop;",
    ),
    Mutant(
        "rules: Survival adds nothing to initiative",
        RULES,
        "    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus) + static_cast<int>(ranks);",
        "    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus) + static_cast<int>(ranks) * 0;",
    ),
    Mutant(
        "settings: a bar of time is not read",
        SETTINGS,
        '             else if (v == "time")\n             {\n                 s.turnClock = TurnClock::Time;',
        '             else if (v == "time")\n             {\n                 s.turnClock = TurnClock::Actions;',
    ),
    Mutant(
        "time: a kept attack's creature does not walk to its target",
        MACHINE,
        "        const bool approach = walk >= kMinStep;",
        "        const bool approach = walk >= 9999.0f;",
    ),
    Mutant(
        "time: the walk to a kept attack's target is held as a script's on the player's turn",
        MACHINE,
        "    const bool approaching = approach_ && kind == CommandKind::Move && request.source == Source::Script;",
        "    const bool approaching = approach_ && kind == CommandKind::Action && request.source == Source::Script;",
    ),
    Mutant(
        "time: a kept action whose target is gone keeps what it was paid",
        MACHINE,
        "        budget_.spend.GiveActionBack(paid);\n        budget_.pendingPaidMs = 0;",
        "        budget_.pendingPaidMs = 0;",
    ),
    # Issue 22: a kept plain attack whose target fell is turned on the nearest
    # enemy its creature can strike from where it stands, for nothing.
    Mutant(
        "time: a kept attack is turned on one out of its reach",
        RULES,
        "        if (!foe.inReach || !std::isfinite(foe.meters))",
        "        if (!std::isfinite(foe.meters))",
    ),
    Mutant(
        "time: a kept attack is turned on one at a distance that is none",
        RULES,
        "        if (!foe.inReach || !std::isfinite(foe.meters))",
        "        if (!foe.inReach)",
    ),
    Mutant(
        "time: a kept attack is turned on one its creature cannot see",
        RULES,
        "        if (!foe.standing || !foe.hostile || !foe.seen)",
        "        if (!foe.standing || !foe.hostile)",
    ),
    Mutant(
        "time: a kept attack is turned on the fallen",
        RULES,
        "        if (!foe.standing || !foe.hostile || !foe.seen)",
        "        if (!foe.hostile || !foe.seen)",
    ),
    Mutant(
        "time: a kept attack is turned on one the game does not call hostile",
        RULES,
        "        if (!foe.standing || !foe.hostile || !foe.seen)",
        "        if (!foe.standing || !foe.seen)",
    ),
    Mutant(
        "time: a kept attack is turned on the first in reach, not the nearest",
        RULES,
        "        if (nearest == nullptr || foe.meters < nearest->meters)",
        "        if (nearest == nullptr)",
    ),
    Mutant(
        "time: of two as near a kept attack is turned on the last named",
        RULES,
        "        if (nearest == nullptr || foe.meters < nearest->meters)",
        "        if (nearest == nullptr || foe.meters <= nearest->meters)",
    ),
    Mutant(
        "an attacker's enemies are its own side",
        RULES,
        "    return (a == Side::Enemy) != (b == Side::Enemy);",
        "    return (a == Side::Enemy) == (b == Side::Enemy);",
    ),
    Mutant(
        "time: a kept talent whose target fell may be turned on another",
        MACHINE,
        "    if (landing_->type == CommandType::Attack)\n    {\n        for (const Combatant &c : order_.All())",
        "    if (landing_->type != CommandType::Invalid)\n    {\n        for (const Combatant &c : order_.All())",
    ),
    Mutant(
        "time: a kept attack may be turned on the fallen of the fight",
        MACHINE,
        "            if (!c.down && Opposed(who.side, c.side))",
        "            if (Opposed(who.side, c.side))",
    ),
    Mutant(
        "time: a kept attack may be turned on anyone standing, friend or foe",
        MACHINE,
        "            if (!c.down && Opposed(who.side, c.side))",
        "            if (!c.down && c.id != who.id)",
    ),
    Mutant(
        "time: a kept attack turned on another is not said",
        MACHINE,
        "            const bool turned = request.type == CommandType::Attack && request.target != kNoObject &&\n"
        "                                request.target != landing_->target;",
        "            const bool turned = false;",
    ),
    Mutant(
        "time: a kept attack given at its own target is said to be turned",
        MACHINE,
        "            const bool turned = request.type == CommandType::Attack && request.target != kNoObject &&\n"
        "                                request.target != landing_->target;",
        "            const bool turned = request.type == CommandType::Attack && request.target != kNoObject;",
    ),
    Mutant(
        "time: whom a kept attack was aimed at is forgotten",
        MACHINE,
        "                                  engine, request.target});",
        "                                  engine});",
    ),
    Mutant(
        "time: a turn whose kept action will not come is handed over only after a settling",
        MACHINE,
        "        handoverWanted_ = who;",
        "        (void)who;",
    ),
    Mutant(
        "time: Next turn is said over every creature",
        MACHINE,
        '        if (PlayerControls(who))\n        {\n            Floaty(pending_, who.id, L"Lands next turn", kGold);',
        '        if (who.id != kNoObject)\n        {\n            Floaty(pending_, who.id, L"Lands next turn", kGold);',
    ),
    Mutant(
        "time: Interrupted is said as a fight ends",
        MACHINE,
        '    if (why == "held" || why == "struck" || why == kConjureCleared || (!Timed() && why != "the fight over"))',
        '    if (why == "held" || why == "struck" || why == kConjureCleared || why == "the fight over" || (!Timed() && why != "the fight over"))',
    ),
    Mutant(
        "time: a cooldown does not move with its user's clock",
        MACHINE,
        "        h.leftMs -= std::min(h.leftMs, clockMs);",
        "        h.leftMs -= std::min(h.leftMs, clockMs) * 0;",
    ),
    Mutant(
        "time: a turn's end does not move the clock the rest of the way",
        MACHINE,
        "        AgeCooldowns(current->id, turn - std::min(turn, clockSeenMs_), fx);",
        "        AgeCooldowns(current->id, 0 * (turn - std::min(turn, clockSeenMs_)), fx);",
    ),
    Mutant(
        "time: walking does not move the clock",
        MACHINE,
        "            budget_.spend.Walk(step);\n            MoveClock(fx);",
        "            budget_.spend.Walk(step);",
    ),
    Mutant(
        "time: a short hold takes nothing of its victim's bar",
        MACHINE,
        "        const std::uint32_t clock = budget_.spend.HoldClock(std::exchange(held.owedMs, 0));",
        "        const std::uint32_t clock = budget_.spend.HoldClock(std::exchange(held.owedMs, 0) * 0);",
    ),
    Mutant(
        "time: a hold of exactly a turn is not a turn lost",
        MACHINE,
        "    const bool owes = timed ? held.owedMs >= turn :",
        "    const bool owes = timed ? held.owedMs > turn :",
    ),
    Mutant(
        "time: a hold in its victim's own turn does not count the rest of that turn",
        MACHINE,
        "                held->second.owedMs -= std::min(held->second.owedMs, rest);",
        "                held->second.owedMs -= std::min(held->second.owedMs, rest) * 0;",
    ),
    Mutant(
        "time: a turn cut short by a hold rolls its time over",
        TURNEND,
        'TurnEnd{"stunned", false},',
        'TurnEnd{"stunned", true},',
    ),
    Mutant(
        "time: a hold taken off still costs its time",
        MACHINE,
        "            if (takenOff && held.owedMs > 0)",
        "            if (takenOff && held.owedMs > 999999)",
    ),
    Mutant(
        "preview: an order's cost forgets its walk",
        PREVIEW,
        "    const float walk = std::max(0.0f, walkMeters - view.paidMeters) / view.paceMps;",
        "    const float walk = 0.0f * std::max(0.0f, walkMeters - view.paidMeters) / view.paceMps;",
    ),
    Mutant(
        "preview: an order that does not fit is not said to spill",
        PREVIEW,
        "    cost.spills = cost.seconds * 1000.0f > static_cast<float>(view.timeLeftMs) + 0.5f;",
        "    cost.spills = cost.seconds * 1000.0f > static_cast<float>(view.timeLeftMs) + 99999.0f;",
    ),
    Mutant(
        "bar: a turn's own seconds are not in the string",
        BAR,
        "    Meters(out, static_cast<float>(view.turnMs) / 1000.0f);",
        "    Meters(out, 0.0f);",
    ),
    Mutant(
        "time: an AI is told to choose again on a look half a second old",
        MACHINE,
        "    const bool fresh = player || !Timed() || lastStatusNow_ == lastNow_;",
        "    const bool fresh = player || !Timed() || lastStatusNow_ <= lastNow_;",
    ),
    # Issue 23, its second half: the cards follow a heal or a hit at once.
    Mutant(
        "nobody is looked at as an action ends: the cards wait for the half second",
        MACHINE,
        "    // three times in one turn (the playthrough).\n    statusWanted_ = true;",
        "    // three times in one turn (the playthrough).",
    ),
    Mutant(
        "a turn waits again on a look as old as its action's end",
        MACHINE,
        "    if (lastStatusNow_ != lastNow_)\n    {\n        statusWanted_ = true;\n        return;\n    }",
        "    if (false)\n    {\n        statusWanted_ = true;\n        return;\n    }",
    ),
    Mutant(
        "a turn about to wait again is looked at every frame, and never waits",
        MACHINE,
        "    if (lastStatusNow_ != lastNow_)\n    {\n        statusWanted_ = true;\n        return;\n    }",
        "    if (lastStatusNow_ <= lastNow_)\n    {\n        statusWanted_ = true;\n        return;\n    }",
    ),
    Mutant(
        "everyone is looked at under a menu, where nothing is done with it: the look after is half a second on",
        MACHINE,
        "    if (lastMode_ != Mode::Gui &&\n",
        "    if (lastMode_ != Mode::Unknown &&\n",
    ),
    Mutant(
        "a look that came with a menu's first frame is not asked for again",
        MACHINE,
        "        statusWanted_ = statusWanted_ || !obs.statuses.empty();",
        "",
    ),
    Mutant(
        "a scan fills every card's mana or stamina",
        MACHINE,
        "            known->energy = energy;",
        "            known->energy = energy * 0.0f + 1.0f;",
    ),
    Mutant(
        "a scan makes every mage's mana stamina",
        MACHINE,
        "            known->mana = mana;",
        "            known->mana = mana && false;",
    ),
    Mutant(
        "the fight's speed does not reach a turn",
        MACHINE,
        "    return std::clamp(settings_.fightSpeed, 0.1f, 8.0f);",
        "    return std::clamp(1.0f, 0.1f, 8.0f);",
    ),
    Mutant(
        "rules: a creature's swing is its whole animation",
        RULES,
        "        return *facts.animation * kSwingShareOfAnimation;",
        "        return *facts.animation;",
    ),
    Mutant(
        "rules: the player's own shot is a follower's",
        RULES,
        "            return facts.hero ? kShotSecondsHero : kShotSecondsFollower;",
        "            return kShotSecondsFollower;",
    ),
    Mutant(
        "rules: an ability's conjuring is not counted",
        RULES,
        "    if (facts.loops > 0 && known(facts.conjure))",
        "    if (facts.loops > 99 && known(facts.conjure))",
    ),
    Mutant(
        "rules: an archery talent with no cast animation costs nothing known",
        RULES,
        "    else if (facts.talent && facts.ranged)",
        "    else if (facts.talent && facts.ranged && facts.loops > 99)",
    ),
    Mutant(
        "time: between two turns the view says the turn is not a bar of time",
        MACHINE,
        "        view.timed = Timed();",
        "        view.timed = budget_.spend.Timed();",
    ),
    Mutant(
        "time: the walk to a kept attack's target has no slack",
        MACHINE,
        "            budget_.spend.Credit(kApproachSlack);",
        "            budget_.spend.Credit(0.0f);",
    ),
    Mutant(
        "time: Out of movement is said over a walk to a kept attack's target",
        MACHINE,
        "    if (budget_.approachWalk)",
        "    if (budget_.approachWalk && budget_.commands > 99)",
    ),
    Mutant(
        "time: the player's kept action ends the turn with no beat",
        MACHINE,
        "                pendingEndNow_ = lastNow_ + kPendingSayMs;",
        "                pendingEndNow_ = lastNow_;",
    ),
    Mutant(
        "time: an action whose own effect holds its creature costs only what it was charged",
        MACHINE,
        "if (const std::uint32_t over = ran - free; over > 0)",
        "if (const std::uint32_t over = ran - free; over > 999999)",
    ),
    Mutant(
        "time: the view does not say what the turn paid for a pending action",
        MACHINE,
        "        view.pendingPaidMs = budget_.pendingPaidMs;",
        "        view.pendingPaidMs = 0;",
    ),
    Mutant(
        "time: a card does not say what its creature's action owes",
        MACHINE,
        "owes != charges_.end() ? owes->owedMs : 0",
        "owes != charges_.end() ? owes->owedMs * 0 : 0",
    ),
    Mutant(
        "time: a walk past the bar stops an action that was charged",
        MACHINE,
        "    if (budget_.spend.Timed() && budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 &&\n        budget_.onArrivalMs == 0)",
        "    if (budget_.spend.Timed() && budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 &&\n        budget_.onArrivalMs == 99)",
    ),
    Mutant(
        "time: an action walked for on its own time is never charged",
        MACHINE,
        "    const std::uint32_t owed = budget_.spend.SpendAction(cost);\n    budget_.lastCostMs = cost - owed;\n    if (owed > 0)\n    {\n        Owe(current->id",
        "    const std::uint32_t owed = budget_.spend.SpendAction(0);\n    budget_.lastCostMs = cost - owed;\n    if (owed > 0)\n    {\n        Owe(current->id",
    ),
    Mutant(
        "a burn interrupts a pending spell",
        MACHINE,
        "    if (!physical || !(damage > threshold) || !Conjuring(target))",
        "    if ((!physical && damage < 0.0f) || !(damage > threshold) || !Conjuring(target))",
    ),
    Mutant(
        "a scratch interrupts a pending spell",
        MACHINE,
        "    if (!physical || !(damage > threshold) || !Conjuring(target))",
        "    if (!physical || !(damage > 0.0f) || !Conjuring(target))",
    ),
    Mutant(
        "a blow interrupts a pending talent",
        MACHINE,
        "    return Active() && ((charge != charges_.end() && charge->kept && charge->spell) ||",
        "    return Active() && ((charge != charges_.end() && charge->kept) ||",
    ),
    Mutant(
        "rules: a party member's threshold forgets Combat Training",
        RULES,
        "    return party ? 10.0f * ranks + 5.0f + levels / 2.0f : 10.0f * ranks + levels;",
        "    return party ? 0.0f * ranks + 5.0f + levels / 2.0f : 10.0f * ranks + levels;",
    ),
    Mutant(
        "rules: a boss is interrupted as a critter is",
        RULES,
        "    return party ? 10.0f * ranks + 5.0f + levels / 2.0f : 10.0f * ranks + levels;",
        "    return party ? 10.0f * ranks + 5.0f + levels / 2.0f : 0.0f * ranks + 10.0f + levels;",
    ),
    Mutant(
        "delay: one that waited may wait again in its round",
        ORDER,
        "    delayed_.push_back(at->id);\n    std::rotate(at, at + 1, order_.end());",
        "    std::rotate(at, at + 1, order_.end());",
    ),
    Mutant(
        "delay: the fallen count as someone to wait for",
        ORDER,
        "                       [](const Combatant &c) { return !c.down; });\n}\n\nbool TurnOrder::Delay()",
        "                       [](const Combatant &c) { return c.id != 0; });\n}\n\nbool TurnOrder::Delay()",
    ),
    Mutant(
        "delay: the turn passes to a fallen combatant",
        ORDER,
        "    while (order_[current_].down)\n    {\n        ++current_;\n    }\n    return true;",
        "    return true;",
    ),
    Mutant(
        "delay: the round after stays in the order the waiting left",
        ORDER,
        "            if (!std::exchange(delayed_, {}).empty())\n            {\n                std::ranges::sort(order_, &TurnOrder::Before);\n            }",
        "            delayed_.clear();",
    ),
    Mutant(
        "delay: a turn is put off after a command of it was let through",
        MACHINE,
        "           budget_.commands == 0 && budget_.pendingPaidMs == 0 && !budget_.landing &&",
        "           budget_.pendingPaidMs == 0 && !budget_.landing &&",
    ),
    Mutant(
        "delay: the setting off is not heeded",
        MACHINE,
        "    return settings_.delay && phase_ == Phase::AwaitingOrder && current != nullptr && PlayerControls(*current) &&",
        "    return phase_ == Phase::AwaitingOrder && current != nullptr && PlayerControls(*current) &&",
    ),
    Mutant(
        "delay: a turn put off is told to the scripts and counted twice",
        MACHINE,
        "    if (order_.Delayed(who.id))\n    {\n        return;\n    }\n    Signal(fx, events::kTurnStart",
        "    if (order_.Delayed(who.id) && who.id == kNoObject)\n    {\n        return;\n    }\n    Signal(fx, events::kTurnStart",
    ),
    Mutant(
        "delay: a turn put off is ended",
        MACHINE,
        "    if (std::exchange(delayRequested_, false) && CanDelay())\n    {\n        DelayTurn(fx);",
        '    if (std::exchange(delayRequested_, false) && CanDelay())\n    {\n        EndTurn(fx, "put off");',
    ),
    Mutant(
        "delay: the view never says a turn may be put off",
        MACHINE,
        "        view.canDelay = CanDelay();",
        "        view.canDelay = false;",
    ),
    Mutant(
        "delay: the bar's string leaves out whether the turn may be put off",
        BAR,
        "    Field(out, static_cast<long long>(view.canDelay ? 1 : view.delayOffered ? 2 : 0));",
        "    Field(out, static_cast<long long>(view.delayOffered ? 2 : 0));",
    ),
    Mutant(
        "a conversation between two rounds loses the round's start",
        MACHINE,
        "        const auto minMs = static_cast<std::uint32_t>(settings_.roundMinSeconds * 1000.0f);\n        if (roundRunMs_ < minMs)\n        {",
        "        const auto minMs = static_cast<std::uint32_t>(settings_.roundMinSeconds * 1000.0f);\n        if (roundPending_ = false; roundRunMs_ < minMs)\n        {",
    ),
    Mutant(
        "preview: a path's string names no creature under the cursor",
        PREVIEW,
        "                    view.spills ? 1 : 0, kBarField, OverField(view.over));",
        "                    view.spills ? 1 : 0, kBarField, OverField(kNoObject));",
    ),
    Mutant(
        "preview: with no path, the string names no creature under the cursor",
        PREVIEW,
        "                       cost.spills ? 1 : 0, kBarField, OverField(over));",
        "                       cost.spills ? 1 : 0, kBarField, OverField(over == 0 ? over : kNoObject));",
    ),
    Mutant(
        "preview: no creature under the cursor is the invalid object's number",
        PREVIEW,
        "    return over == kNoObject ? 0 : over;",
        "    return over;",
    ),
    Mutant(
        "delay: the bar's string does not say that Delay is offered and not to be had",
        BAR,
        "    Field(out, static_cast<long long>(view.canDelay ? 1 : view.delayOffered ? 2 : 0));",
        "    Field(out, static_cast<long long>(view.canDelay ? 1 : 0));",
    ),
    Mutant(
        "delay: offered with the setting off",
        MACHINE,
        "        view.delayOffered = settings_.delay && view.playerTurn;",
        "        view.delayOffered = view.playerTurn;",
    ),
    Mutant(
        "delay: nobody's card says it waits",
        MACHINE,
        "        const bool waits = !slot.current && slot.round == order_.Round() && order_.Delayed(c.id);",
        "        const bool waits = !slot.current && slot.round != order_.Round() && order_.Delayed(c.id);",
    ),
    Mutant(
        "delay: a card waits on the turn it waited for",
        MACHINE,
        "        const bool waits = !slot.current && slot.round == order_.Round() && order_.Delayed(c.id);",
        "        const bool waits = slot.round == order_.Round() && order_.Delayed(c.id);",
    ),
    Mutant(
        "bar: a card's string leaves out that it waits",
        BAR,
        "        Field(out, card.waits);",
        "        Field(out, false);",
    ),
    Mutant(
        "time: the view does not say what rolled over",
        MACHINE,
        "        budget_.rolledMs = rolled->second;",
        "        budget_.rolledMs = 0;",
    ),
    Mutant(
        "bar: the string leaves out what rolled over",
        BAR,
        "    Meters(out, static_cast<float>(view.rolledMs) / 1000.0f);",
        "    Meters(out, 0.0f);",
    ),
    Mutant(
        "a spell lost is called an action, and an action a spell",
        MACHINE,
        '        Floaty(fx, who, spell ? L"Spell lost" : L"Action lost", kGold);',
        '        Floaty(fx, who, spell ? L"Action lost" : L"Spell lost", kGold);',
    ),
    Mutant(
        "bar: a card does not say which ability is pending",
        MACHINE,
        "            owes != charges_.end() && owes->type == CommandType::UseAbility ? owes->ability : -1;",
        "            owes != charges_.end() && owes->type == CommandType::UseAbility ? -1 : -1;",
    ),
    Mutant(
        "bar: a card's string leaves out the pending ability's id",
        BAR,
        "        Field(out, static_cast<long long>(card.pendingAbility));",
        "        Field(out, static_cast<long long>(-1));",
    ),
    Mutant(
        "bar: a card's string leaves out the pending ability's icon",
        BAR,
        "        Field(out, std::wstring_view{card.pendingIcon});",
        "        Field(out, std::wstring_view{});",
    ),
    Mutant(
        "bar: a card's string leaves out the pending ability's name",
        BAR,
        "        Field(out, std::wstring_view{card.pendingName});",
        "        Field(out, std::wstring_view{});",
    ),
    Mutant(
        "pause: the key pauses and never runs the game again",
        MACHINE,
        "    playerPaused_ = !playerPaused_;",
        "    playerPaused_ = true;",
    ),
    Mutant(
        "pause: the player's pause is not kept by the machine",
        MACHINE,
        "    const bool want = (WantsPaused(phase_, settings_) && !Landing()) || playerPaused_;",
        "    const bool want = WantsPaused(phase_, settings_) && !Landing();",
    ),
    Mutant(
        "pause: an order given while paused leaves the game paused",
        MACHINE,
        "        if (verdict == Verdict::Allow && std::exchange(playerPaused_, false))",
        "        if (verdict == Verdict::Allow && playerPaused_)",
    ),
    Mutant(
        "pause: the key arms a pause while the machine holds the world still",
        MACHINE,
        "    if (!playerPaused_ && WantsPaused(phase_, settings_))\n    {\n        return true; // held still",
        "    if (!playerPaused_ && WantsPaused(phase_, settings_) && phase_ == Phase::Idle)\n    {\n        return true; // held still",
    ),
    Mutant(
        "pause: the player's pause outlives the fight",
        MACHINE,
        "    Emit(EventKind::FightOver, kNoObject, std::string{why});\n    playerPaused_ = false;",
        "    Emit(EventKind::FightOver, kNoObject, std::string{why});",
    ),
    Mutant(
        "pause: the player's pause outlives a conversation over the fight",
        MACHINE,
        "        phase_ = Phase::Suspended;\n        playerPaused_ = false;\n        SetSpeed(1.0f, fx);",
        "        phase_ = Phase::Suspended;\n        SetSpeed(1.0f, fx);",
    ),
    Mutant(
        "time: an action that played most of its time is given back",
        MACHINE,
        "clock_ - budget_.lastClock >= budget_.lastCostMs / kOverAtOnce)",
        "clock_ - budget_.lastClock >= budget_.lastCostMs)",
    ),
    Mutant(
        "time: an action on a stand-in's time is given back when it ends at once",
        MACHINE,
        "    budget_.lastMeasured = std::isfinite(request.actionSeconds) && request.actionSeconds > 0.0f;",
        "    budget_.lastMeasured = true;",
    ),
    Mutant(
        "time: the setting off, an action over at once is given back all the same",
        MACHINE,
        "    if (current == nullptr || !Timed() || !settings_.giveBackUnplayed || budget_.lastKind != CommandKind::Action ||",
        "    if (current == nullptr || !Timed() || budget_.lastKind != CommandKind::Action ||",
    ),
    Mutant(
        "time: an action over at once is charged",
        MACHINE,
        "    OverAtOnce(fx);\n    const Combatant *current = order_.Current();",
        "    const Combatant *current = order_.Current();",
    ),
    Mutant(
        "pause: the view does not say the player has the game paused",
        MACHINE,
        "        view.playerPaused = playerPaused_;",
        "        view.playerPaused = false;",
    ),
    Mutant(
        "pause: the bar's string does not say the game is paused",
        BAR,
        "    Field(out, view.playerPaused);",
        "    Field(out, false);",
    ),
    Mutant(
        "a lost spell is said in grey",
        MACHINE,
        '        Floaty(fx, who, spell ? L"Spell lost" : L"Action lost", kGold);',
        '        Floaty(fx, who, spell ? L"Spell lost" : L"Action lost", kGrey);',
    ),
    Mutant(
        "an attack standing short of its target is left to stand",
        MACHINE,
        "    if (actor.busy && !heldByEffect_ && (at == CommandKind::Move || at == CommandKind::Free || short_))",
        "    if (actor.busy && !heldByEffect_ && (at == CommandKind::Move || at == CommandKind::Free))",
    ),
    Mutant(
        "an attack swinging where it stands is taken for one that cannot get there",
        MACHINE,
        "    const bool short_ = attacking && actor.approaching;",
        "    const bool short_ = attacking;",
    ),
    # Issue 11: an attack that stands still is judged by whether the engine
    # has its command still at its approach, as the game layer reads it each
    # frame, and by how long it has stood at that command alone; and a kept
    # action is not given to a creature an effect holds. (An attack's reach
    # and walk are the engine's own, asked in src/game: no rule of the core's
    # to break.)
    Mutant(
        "a kept action is given to a creature an effect holds as its turn begins",
        MACHINE,
        "Name(charge->type)));\n        return false;\n    }\n    landing_ = *charge;",
        "Name(charge->type)));\n    }\n    landing_ = *charge;",
    ),
    Mutant(
        "a kept action that waited on an effect is not given as the effect lets go",
        MACHINE,
        "        if (!actor.busy && current != nullptr && LandCast(*current, fx))",
        "        if (!actor.busy && current != nullptr && current->down && LandCast(*current, fx))",
    ),
    Mutant(
        "time: an action that did nothing is given back once a turn only",
        MACHINE,
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || (budget_.refunded && !Timed()))",
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || budget_.refunded)",
    ),
    Mutant(
        "as built, a failed action is given back every time",
        MACHINE,
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || (budget_.refunded && !Timed()))",
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || (budget_.refunded && Timed()))",
    ),
    Mutant(
        "a turn's commands share one limit, counted from the first",
        MACHINE,
        "    executingSince_ = clock_;\n    return Verdict::Allow;",
        "    if (began)\n    {\n        executingSince_ = clock_;\n    }\n    return Verdict::Allow;",
    ),
    Mutant(
        "an action given back is not said over the player's own",
        MACHINE,
        "    if (PlayerControls(*current))\n    {\n        SayGivenBack(pending_, *current, type, WhyGivenBack(why));\n    }\n    return true;\n}",
        "    if (PlayerControls(*current) && who == kNoObject)\n    {\n        SayGivenBack(pending_, *current, type, WhyGivenBack(why));\n    }\n    return true;\n}",
    ),
    Mutant(
        "an action given back is said over an enemy too",
        MACHINE,
        "    if (PlayerControls(*current))\n    {\n        SayGivenBack(pending_, *current, type, WhyGivenBack(why));\n    }\n    return true;\n}",
        "    if (current != nullptr)\n    {\n        SayGivenBack(pending_, *current, type, WhyGivenBack(why));\n    }\n    return true;\n}",
    ),
    # What the player is told of an action given back, by the engine's
    # reason (core/GivenBack.h; issue 21): over the character, and on the
    # bar's caption for a while.
    Mutant(
        "a line of sight's refusal is said as nothing happened, and no reason as a line of sight",
        GIVENBACK,
        'if (why.find("LINE_OF_SIGHT") != std::string_view::npos)',
        'if (why.find("LINE_OF_SIGHT") == std::string_view::npos)',
    ),
    Mutant(
        "a door in the way is said as nothing happened",
        GIVENBACK,
        'if (why.find("PATH_ACTION_REQUIRED") != std::string_view::npos)',
        'if (why.find("PATH_ACTION_REQUIRED") != std::string_view::npos && why.empty())',
    ),
    Mutant(
        "no room at the target is said as nothing happened",
        GIVENBACK,
        'if (why.find("MELEE_RING") != std::string_view::npos)',
        'if (why.find("MELEE_RING") != std::string_view::npos && why.empty())',
    ),
    Mutant(
        "no path is said as nothing happened",
        GIVENBACK,
        'if (why.find("INVALID_PATH") != std::string_view::npos)',
        'if (why.find("INVALID_PATH") != std::string_view::npos && why.empty())',
    ),
    Mutant(
        "the caption's line calls an ability an attack",
        GIVENBACK,
        'type == CommandType::UseAbility ? L"ability"',
        'type == CommandType::UseAbility ? L"attack"',
    ),
    Mutant(
        "the caption's line does not say whose action was given back",
        GIVENBACK,
        'return std::format(L"{}: {}\'s {} is given back", words, name, what);',
        'return std::format(L"{}: {}\'s {} is given back", words, name.substr(0, 0), what);',
    ),
    # And of an attack the engine has no way to make, refused before a step
    # (issue 27): the words its failure would have had, and "is refused".
    Mutant(
        "no place when asked is said as no way through",
        GIVENBACK,
        'case NoAttack::NoPlace:\n        return L"No room at the target";',
        'case NoAttack::NoPlace:\n        return L"No way through";',
    ),
    Mutant(
        "no path when asked is said as no room at the target",
        GIVENBACK,
        'case NoAttack::NoPath:\n        return L"No way through";',
        'case NoAttack::NoPath:\n        return L"No room at the target";',
    ),
    Mutant(
        "a shot with no line to be had is said as no way through",
        GIVENBACK,
        'case NoAttack::NoLine:\n        return L"No line of sight";',
        'case NoAttack::NoLine:\n        return L"No way through";',
    ),
    Mutant(
        "a kept attack not made is said on the caption as refused",
        GIVENBACK,
        'return std::format(L"{}: {}\'s {} is not made", words, name, Named(type));',
        'return std::format(L"{}: {}\'s {} is refused", words, name, Named(type));',
    ),
    Mutant(
        "an attack refused before a step is said on the caption as given back",
        GIVENBACK,
        'return std::format(L"{}: {}\'s {} is refused", words, name, Named(type));',
        'return std::format(L"{}: {}\'s {} is given back", words, name, Named(type));',
    ),
    Mutant(
        "an action given back is not said on the bar's caption: its line is past before it is seen",
        MACHINE,
        "    notice_ = Notice{GivenBackLine(words, who.name, type), lastNow_};\n",
        "    notice_ = Notice{GivenBackLine(words, who.name, type), lastNow_ - kNoticeMs};\n",
    ),
    Mutant(
        "the caption's line of an action given back never goes",
        MACHINE,
        "        if (notice_ && lastNow_ - notice_->at < kNoticeMs)\n",
        "        if (notice_)\n",
    ),
    Mutant(
        "the caption's line is gone in half a second: not long enough to read",
        "src/core/TurnMachine.h",
        "static constexpr std::uint32_t kNoticeMs = 5000;",
        "static constexpr std::uint32_t kNoticeMs = 500;",
    ),
    Mutant(
        "the caption's line outlives the player's next order",
        MACHINE,
        "    if (request.source == Source::Player)\n    {\n        notice_.reset();\n    }\n",
        "",
    ),
    Mutant(
        "another's command takes the caption's line away before it is read",
        MACHINE,
        "    if (request.source == Source::Player)\n    {\n        notice_.reset();\n    }\n",
        "    notice_.reset();\n",
    ),
    Mutant(
        "a new fight begins with the last fight's line on its caption",
        MACHINE,
        "    notice_.reset(); // what the caption said of the last fight's order is not this one's\n",
        "",
    ),
    Mutant(
        "a creature that cannot get to its target is stopped with no word on the caption",
        MACHINE,
        '                    SayGivenBack(fx, *current, budget_.lastType, L"No way through");\n',
        '                    Floaty(fx, actor.id, L"No way through", kGold);\n',
    ),
    Mutant(
        "a spell that failed before its conjure is given back with no word",
        MACHINE,
        "        if (PlayerControls(*current))\n        {\n            SayGivenBack(pending_, *current, type, WhyGivenBack(why));\n        }\n        return true;\n",
        "        return true;\n",
    ),
    Mutant(
        "the bar's string leaves the caption's line out",
        BAR,
        "    Field(out, std::wstring_view{view.notice});\n",
        "    Field(out, std::wstring_view{});\n",
    ),
    Mutant(
        "the state a tool reads leaves the caption's line out",
        "src/core/State.cpp",
        "    Text(out, Utf8(view.notice));\n",
        '    Text(out, "");\n',
    ),
    Mutant(
        "an AI given its action back twice is asked a third time",
        MACHINE,
        "    if (!player && budget_.givenBack >= kGivenBackEnough)",
        "    if (!player && budget_.givenBack >= kGivenBackEnough + 100)",
    ),
    Mutant(
        "an AI given its action back once has its turn ended",
        MACHINE,
        "    if (!player && budget_.givenBack >= kGivenBackEnough)",
        "    if (!player && budget_.givenBack >= kGivenBackEnough - 1)",
    ),
    Mutant(
        "an attack standing short of its target is given as long as a walk",
        MACHINE,
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs);",
        "        const std::uint32_t limit = Patience(short_ ? kStoodMs + 1000 : kStoodMs);",
    ),
    Mutant(
        "an attack standing short of its target is stopped at once",
        MACHINE,
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs);",
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs / 4 : kStoodMs);",
    ),
    Mutant(
        "an attack still at its approach is taken for one at its blow",
        MACHINE,
        "    const bool short_ = attacking && actor.approaching;",
        "    const bool short_ = attacking && !actor.approaching;",
    ),
    Mutant(
        "how long a creature has stood is carried over from the command before",
        MACHINE,
        "    // replay of the ambush, 2026-10-04).\n    stoodAt_.reset();\n",
        "    // replay of the ambush, 2026-10-04).\n",
    ),
    Mutant(
        "a party member's lost turn is said in grey",
        MACHINE,
        '    Floaty(fx, who.id, who.name + L" loses the turn", who.side == Side::Party ? kGold : kGrey);\n    EndTurn(fx, "held by its condition");',
        '    Floaty(fx, who.id, who.name + L" loses the turn", kGrey);\n    EndTurn(fx, "held by its condition");',
    ),
    Mutant(
        "an elite boss's Combat Tactics stops at a player's ranks",
        "src/core/TurnTime.cpp",
        "    const auto bounded = static_cast<std::uint32_t>(std::clamp(rank, 0, kMostTactics));",
        "    const auto bounded = static_cast<std::uint32_t>(std::clamp(rank, 0, 4));",
    ),
    Mutant(
        "an elite boss fights at a boss's rank",
        "src/core/Settings.h",
        "    std::array<int, 5> enemyTactics{0, 0, 2, 4, 6};",
        "    std::array<int, 5> enemyTactics{0, 0, 2, 4, 4};",
    ),
    Mutant(
        "a swing's time is kept whatever the weapon",
        "src/core/Rules.cpp",
        "    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier), kNoObject};",
        "    return Key{style, milli(weaponSpeed) * 0, milli(attackSpeedModifier), kNoObject};",
    ),
    Mutant(
        "a swing's time is kept whatever the attack speed",
        "src/core/Rules.cpp",
        "    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier), kNoObject};",
        "    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier) * 0, kNoObject};",
    ),
    Mutant(
        "a swing's time is kept whatever the weapon style",
        "src/core/Rules.cpp",
        "    return Key{style, milli(weaponSpeed), milli(attackSpeedModifier), kNoObject};",
        "    return Key{style == WeaponStyle::Dual ? style : WeaponStyle::Single, milli(weaponSpeed), milli(attackSpeedModifier), kNoObject};",
    ),
    Mutant(
        "a time no swing could take is kept",
        "src/core/Rules.cpp",
        "    if (!std::isfinite(seconds) || seconds < kLeast || seconds > kMost)\n    {\n        return false;\n    }\n    Read &read",
        "    if (!std::isfinite(seconds))\n    {\n        return false;\n    }\n    Read &read",
    ),
    Mutant(
        "a swing of the least time is refused",
        "src/core/Rules.cpp",
        "    if (!std::isfinite(seconds) || seconds < kLeast || seconds > kMost)",
        "    if (!std::isfinite(seconds) || seconds <= kLeast || seconds >= kMost)",
    ),
    Mutant(
        "a swing's time is the first one seen, not the last",
        "src/core/Rules.cpp",
        "    read.before = read.last;\n    read.last = seconds;",
        "    read.before = read.last;\n    read.last = read.last > 0.0f ? read.last : seconds;",
    ),
    Mutant(
        "one weapon's swing is a mean of the last two",
        "src/core/Rules.cpp",
        "    if ((key.style == WeaponStyle::Dual || key.style == WeaponStyle::Ranged) && read.before > 0.0f)",
        "    if (read.before > 0.0f)",
    ),
    Mutant(
        "a weapon in each hand: the last swing, not the mean",
        "src/core/Rules.cpp",
        "    if ((key.style == WeaponStyle::Dual || key.style == WeaponStyle::Ranged) && read.before > 0.0f)",
        "    if ((key.style == WeaponStyle::TwoHanded || key.style == WeaponStyle::Ranged) && read.before > 0.0f)",
    ),
    Mutant(
        "the fourth rank of Combat Training is the shipped script's whatever is installed",
        "src/core/Rules.cpp",
        "            ids.at(found++) = *next;",
        "            ids.at(found) = found == 3 ? kCombatTrainingAsShipped[3] : *next;\n            ++found;",
    ),
    Mutant(
        "the script's four are taken though the second is not Combat Training's",
        "src/core/Rules.cpp",
        "        if (found == ids.size() && ids[1] == kCombatTrainingAsShipped[1] && ids[2] == kCombatTrainingAsShipped[2] &&",
        "        if (found == ids.size() && ids[2] == kCombatTrainingAsShipped[2] &&",
    ),
    Mutant(
        "the script's four are taken though the third is not Combat Training's",
        "src/core/Rules.cpp",
        "        if (found == ids.size() && ids[1] == kCombatTrainingAsShipped[1] && ids[2] == kCombatTrainingAsShipped[2] &&",
        "        if (found == ids.size() && ids[1] == kCombatTrainingAsShipped[1] &&",
    ),
    Mutant(
        "a fourth rank that is no ability is taken",
        "src/core/Rules.cpp",
        "            ids[3] > 0)",
        "            ids[3] >= 0)",
    ),
    Mutant(
        "a constant far from the three is taken for the fourth",
        "src/core/Rules.cpp",
        "            for (std::size_t at = from; at < from + kNear && !next; ++at)",
        "            for (std::size_t at = from; at < from + kNear * 100 && !next; ++at)",
    ),
    Mutant(
        "a constant is read by its second byte alone",
        "src/core/Rules.cpp",
        "    if (at + kConstantBytes > script.size() || script[at] != 0x04 || script[at + 1] != 0x03)",
        "    if (at + kConstantBytes > script.size() || script[at + 1] != 0x03)",
    ),
    Mutant(
        "a constant's high bytes are dropped",
        "src/core/Rules.cpp",
        "    const std::uint32_t value = (static_cast<std::uint32_t>(script[at + 2]) << 24) |",
        "    const std::uint32_t value = (static_cast<std::uint32_t>(script[at + 2]) << 8) |",
    ),
    Mutant(
        "a party member's fourth rank of Combat Training never counts",
        "src/core/Rules.cpp",
        "    const float ranks = static_cast<float>(std::clamp(combatTraining, 0, 20));",
        "    const float ranks = static_cast<float>(std::clamp(combatTraining, 0, party ? 3 : 20));",
    ),
    Mutant(
        "a critter is given a rank of Combat Training",
        "src/core/Rules.cpp",
        "    default: // a critter, and what has no rank\n        return 0;",
        "    default: // a critter, and what has no rank\n        return 1;",
    ),
    Mutant(
        "an effect not seen as the turn began holds its creature for max_action_ms",
        MACHINE,
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0)",
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands < 0)",
    ),
    Mutant(
        "its own action's effect is taken for one that holds it",
        MACHINE,
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect && budget_.commands == 0)",
        "        if (!heldByEffect_ && actor.current == CommandType::ExecuteEffect)",
    ),
    Mutant(
        "own clocks: a creature's effects do not move as its bar is spent",
        MACHINE,
        "    if (clockMs == 0 || !OwnClocks() || !timeHeld_ || ownEffectSince_)\n    {\n        return;\n    }\n    Effect age = Effect::Of(EffectKind::AgeTime, who);",
        "    if (clockMs < 100000 || !OwnClocks() || !timeHeld_ || ownEffectSince_)\n    {\n        return;\n    }\n    Effect age = Effect::Of(EffectKind::AgeTime, who);",
    ),
    Mutant(
        "own clocks: a turn's end does not move its creature's effects the rest of the way",
        MACHINE,
        "; rest > 0 && OwnClocks() && timeHeld_)",
        "; rest > 0 && OwnClocks() && timeHeld_ && rest > turn)",
    ),
    Mutant(
        "own clocks: a turn's end moves its creature's effects a whole turn again",
        MACHINE,
        "        if (const std::uint32_t rest = turn - std::min(turn, budget_.agedOwnMs);",
        "        if (const std::uint32_t rest = turn;",
    ),
    Mutant(
        "own clocks: what the bar moved is not counted toward the turn",
        MACHINE,
        "    age.b = static_cast<std::int32_t>(clockMs);\n    fx.push_back(age);\n    budget_.agedOwnMs += clockMs;",
        "    age.b = static_cast<std::int32_t>(clockMs);\n    fx.push_back(age);",
    ),
    Mutant(
        "own clocks: nobody is put on one",
        MACHINE,
        "    if (!OwnClocks())\n    {\n        return;\n    }\n    for (const Combatant &c : order_.All())",
        "    if (OwnClocks())\n    {\n        return;\n    }\n    for (const Combatant &c : order_.All())",
    ),
    Mutant(
        "own clocks: a creature that joins is not put on one",
        MACHINE,
        "            if (OwnClocks() && timeHeld_)\n            {\n                Effect own = Effect::Of(EffectKind::OwnClock, joined);",
        "            if (OwnClocks() && !timeHeld_)\n            {\n                Effect own = Effect::Of(EffectKind::OwnClock, joined);",
    ),
    Mutant(
        "own clocks: the setting is not asked",
        MACHINE,
        "    return Timed() && settings_.ownClocks;",
        "    return Timed();",
    ),
    Mutant(
        "own clocks: its action's own effect moves everyone's",
        MACHINE,
        "            const bool own = OwnClocks() && acting != nullptr;",
        "            const bool own = false && acting != nullptr;",
    ),
    Mutant(
        "own clocks: an action's own effect does not move its target's clock",
        MACHINE,
        "                if (held)\n                {\n                    Effect aged = Effect::Of(EffectKind::AgeTime, victim);",
        "                if (false)\n                {\n                    Effect aged = Effect::Of(EffectKind::AgeTime, victim);",
    ),
    Mutant(
        "the engine's conjuring: a spell that conjures is kept in the gate",
        MACHINE,
        "        return engine ? Verdict::Conjure : Verdict::Hold;",
        "        return Verdict::Hold;",
    ),
    Mutant(
        "the engine's conjuring: a spell with no conjure is given to the engine",
        MACHINE,
        "           request.abilityType == kAbilitySpell && !request.sustained && request.conjureAnim > 0 &&",
        "           request.abilityType == kAbilitySpell && !request.sustained && request.conjureAnim >= 0 &&",
    ),
    Mutant(
        "the engine's conjuring: the setting is not asked",
        MACHINE,
        "    return Timed() && settings_.engineConjure && request.type == CommandType::UseAbility &&",
        "    return Timed() && request.type == CommandType::UseAbility &&",
    ),
    Mutant(
        "the engine's conjuring: the mod's own rule still interrupts",
        MACHINE,
        "    if (Timed() && settings_.engineConjure)\n    {\n        return false;\n    }",
        "    if (Timed() && settings_.engineConjure && false)\n    {\n        return false;\n    }",
    ),
    Mutant(
        "the engine's conjuring: the turn does not end at the held conjure",
        MACHINE,
        "    if (budget_.conjureWaiting && (actor.conjureHeld || !actor.busy))",
        "    if (budget_.conjureWaiting && (!actor.busy))",
    ),
    Mutant(
        "the engine's conjuring: a cleared conjure is not lost",
        MACHINE,
        "    Interrupt(caster, kConjureCleared, pending_);\n    return true;",
        "    return true;",
    ),
    Mutant(
        "the engine's conjuring: a failure before the conjure leaves it pending",
        MACHINE,
        "        std::erase_if(charges_, [&](const Charge &c) { return c.caster == who && c.engine; });",
        "        std::erase_if(charges_, [&](const Charge &c) { return c.caster == who && c.engine && false; });",
    ),
    Mutant(
        "the engine's conjuring: its landing waits at the gate",
        MACHINE,
        "    if (landing_->engine)\n    {",
        "    if (landing_->engine && false)\n    {",
    ),
    Mutant(
        "the engine's conjuring: another order is let through while its spell goes to its conjure",
        MACHINE,
        "    if (budget_.conjureWaiting && kind != CommandKind::Free)",
        "    if (budget_.conjureWaiting && kind == CommandKind::Involuntary)",
    ),
    Mutant(
        "a creature held by another's action owes nothing",
        MACHINE,
        "                grabbedMs_[victim] += ran;\n                budget_.grabbedNowMs += ran;",
        "                grabbedMs_[victim] += 0;\n                budget_.grabbedNowMs += ran;",
    ),
    Mutant(
        "held by another's action, a turn's worth or more is not a lost turn",
        MACHINE,
        "        Timed() && grabbed != grabbedMs_.end() && grabbed->second >= TurnMs())",
        "        Timed() && grabbed != grabbedMs_.end() && grabbed->second >= TurnMs() * 100)",
    ),
    Mutant(
        "held by another's action, its share is not taken of its bar",
        MACHINE,
        "    const std::uint32_t clock = budget_.spend.HoldClock(owed);\n    AgeCooldowns(who.id, clock, fx);",
        "    const std::uint32_t clock = budget_.spend.HoldClock(0);\n    AgeCooldowns(who.id, clock, fx);",
    ),
    Mutant(
        "held by another's action, its effects are moved again at its turn",
        MACHINE,
        "    clockSeenMs_ = budget_.spend.ClockElapsedMs();\n    budget_.agedOwnMs += clock;",
        "    budget_.agedOwnMs += 0;",
    ),
    Mutant(
        "own clocks: an action's own effect moves the shared clock, areas' with it",
        MACHINE,
        "                // its fifth round (no hands, 2026-10-02).\n                budget_.agedOwnMs += ran;",
        "                // its fifth round (no hands, 2026-10-02).\n                budget_.agedOwnMs += ran;\n"
        "                Effect shared = Effect::Of(EffectKind::AgeTime);\n"
        "                shared.b = static_cast<std::int32_t>(ran);\n                fx.push_back(shared);\n"
        "                roundAgedMs_ += ran;",
    ),
    Mutant(
        "an action stopped as too long leaves its target owing it",
        MACHINE,
        "    debt->second -= std::min(debt->second, std::exchange(budget_.grabbedNowMs, 0));",
        "    debt->second -= std::min(debt->second, std::exchange(budget_.grabbedNowMs, 0) * 0);",
    ),
    # A picture for every card (issue 32): which creatures are rendered, the
    # kind of body each is, the one table of framings and how a creature is
    # looked up in it, the engine's far plane, and what a tester gives and is
    # told.
    Mutant(
        "a creature with no picture, kin or camera in the table gets no portrait",
        PORTRAITS,
        "    // framed by its kind and size.\n    return {PortraitSource::Render, {}, false};",
        "    // framed by its kind and size.\n    return {};",
    ),
    Mutant(
        "a creature the table has a camera for is framed anew",
        PORTRAITS,
        "            return {PortraitSource::Render, {}, true}; // the table has its camera",
        "            return {PortraitSource::Render, {}, false}; // the table has its camera",
    ),
    Mutant(
        "a creature's model, standing up and a person's size, is taken for a person's body",
        PORTRAITS,
        "    return personsModel && personSized ? BodyKind::Person : BodyKind::Upright;",
        "    return (personsModel || personSized) && personSized ? BodyKind::Person : BodyKind::Upright;",
    ),
    Mutant(
        "a body on four legs is taken for one that stands up",
        PORTRAITS,
        "    if (terrainFollowPitch > 0.0f)\n    {\n        return BodyKind::OnAllFours;",
        "    if (terrainFollowPitch > 100.0f)\n    {\n        return BodyKind::OnAllFours;",
    ),
    Mutant(
        "a person's model bigger than a person is taken for a person's body",
        PORTRAITS,
        "    const bool personSized = extent <= kPersonExtent * kBiggerThanPerson;",
        "    const bool personSized = extent <= kPersonExtent * kBiggerThanPerson * 100.0f;",
    ),
    Mutant(
        "the framing table's row for an appearance is not used",
        PORTRAITS,
        "        if (const FramingRow *row = RowOf(appearance))",
        "        if (const FramingRow *row = RowOf(-appearance))",
    ),
    Mutant(
        "the framing table's row is found for another appearance",
        PORTRAITS,
        "[appearance](const FramingRow &r) { return appearance > 0 && r.appearance == appearance; });",
        "[appearance](const FramingRow &r) { return appearance > 0 && r.appearance == appearance + 1; });",
    ),
    Mutant(
        "a kind's default row is taken for an appearance's own",
        PORTRAITS,
        "[appearance](const FramingRow &r) { return appearance > 0 && r.appearance == appearance; });",
        "[appearance](const FramingRow &r) { return r.appearance == appearance; });",
    ),
    Mutant(
        "a kind's default is another kind's",
        PORTRAITS,
        "[kind](const FramingRow &r) { return r.appearance == 0 && r.kind == kind; });",
        "[kind](const FramingRow &r) { return r.appearance == 0 && r.kind != kind; });",
    ),
    Mutant(
        "a kind a tester asks for is passed over for the appearance's row",
        PORTRAITS,
        "    if (!kindAsked)\n    {\n        if (const FramingRow *row = RowOf(appearance))",
        "    if (!kindAsked || appearance > 0)\n    {\n        if (const FramingRow *row = RowOf(appearance))",
    ),
    Mutant(
        "the game's own row for a creature is passed over for its kind's default",
        PORTRAITS,
        "        if (gamesOwn != nullptr)\n        {\n            PortraitFrame frame;",
        "        if (gamesOwn != nullptr && extent < 0.0f)\n        {\n            PortraitFrame frame;",
    ),
    Mutant(
        "a row's aside and up are taken one for the other",
        PORTRAITS,
        "    return PortraitFrame{PortraitCamera{row.pitch, row.rotation, row.distance, row.up, row.aside},",
        "    return PortraitFrame{PortraitCamera{row.pitch, row.rotation, row.distance, row.aside, row.up},",
    ),
    Mutant(
        "a default's distance does not follow the creature's size",
        PORTRAITS,
        "    const float bySize = std::isfinite(extent) ? row.distance * std::max(extent, 0.0f) : 0.0f;",
        "    const float bySize = std::isfinite(extent) ? row.distance : 0.0f;",
    ),
    Mutant(
        "a default frames from past the far plane, where nothing is drawn",
        PORTRAITS,
        "    const float distance = std::clamp(bySize, kFramedFromLeast, kFramedFromMost);",
        "    const float distance = std::max(bySize, kFramedFromLeast);",
    ),
    Mutant(
        "a default frames a creature with next to no measure from inside it",
        PORTRAITS,
        "    const float distance = std::clamp(bySize, kFramedFromLeast, kFramedFromMost);",
        "    const float distance = std::min(bySize, kFramedFromMost);",
    ),
    Mutant(
        "a default's aside and up do not go by the creature's size",
        PORTRAITS,
        "    const float size = row.distance > 0.0f ? distance / row.distance : 0.0f;",
        "    const float size = row.distance > 0.0f ? 1.0f : 0.0f;",
    ),
    Mutant(
        "a creature held at the furthest its head is drawn from is not said to be",
        PORTRAITS,
        "                         bySize > kFramedFromMost};",
        "                         bySize < 0.0f};",
    ),
    Mutant(
        "the distance a tester gives is not used",
        PORTRAITS,
        "tuning.distance.value_or(rule.distance)",
        "rule.distance",
    ),
    Mutant(
        "a creature's extent is the longer of the head's two readings",
        PORTRAITS,
        "    return HeadExtent{std::min(world, own), world <= own};",
        "    return HeadExtent{std::max(world, own), world <= own};",
    ),
    Mutant(
        "a camera past the far plane is not said to be",
        PORTRAITS,
        "    return kPortraitBackOff * camera.distance > kPortraitFarPlane;",
        "    return camera.distance > kPortraitFarPlane;",
    ),
    Mutant(
        "a camera value that is not a number is taken for the rule's",
        PORTRAITS,
        '                return wrong(std::string{key} + " is not a number.");',
        "                continue;",
    ),
    Mutant(
        "a kind there is none of is taken for the rule's",
        PORTRAITS,
        '            return wrong("The kind is not one there is.");',
        "            (void)0;",
    ),
    Mutant(
        "the picture put up large loses its serial",
        PORTRAITS,
        "    out += std::to_wstring(serial);",
        "    out += std::to_wstring(serial + 1);",
    ),
    Mutant(
        "the portrait tool's answer leaves out where the head node is",
        PORTRAITS,
        "        AppendPoint(out, *report.head);",
        '        out += "null";',
    ),
    Mutant(
        "the portrait tool's answer does not say which row of the table framed the picture",
        PORTRAITS,
        '    AppendText(out, "row", report.row);',
        '    AppendText(out, "row", std::string_view{});',
    ),
    Mutant(
        "the portrait tool's answer says nothing of the camera against the creature's size",
        PORTRAITS,
        "                       report.atMost, sized ? report.camera.distance / report.extent : 0.0f,",
        "                       report.atMost, sized ? 0.0f : 0.0f,",
    ),
    Mutant(
        "the log's line for a portrait leaves out what was measured of its creature",
        PORTRAITS,
        "                           MeasureWords(report));\n        if (!report.problem.empty())",
        "                           std::string{});\n        if (!report.problem.empty())",
    ),
    Mutant(
        "the log does not say which row of the table framed a portrait",
        PORTRAITS,
        '        return std::format("row {} ({})", frame.label, frame.note);',
        '        return std::format("row ({})", frame.note);',
    ),
    Mutant(
        "a portrait waiting to be released is not known to be",
        PORTRAITS,
        "    return std::ranges::find(releasing_, id) != releasing_.end();",
        "    return std::ranges::find(releasing_, id) == releasing_.end();",
    ),
    # A creature with no head node (issue 32, the deepstalker): rendered by a
    # row from its root or a tester's values, and by nothing else.
    Mutant(
        "a row about a head frames a creature whose head is only late",
        PORTRAITS,
        "    const bool rooted = row != nullptr && row->fromRoot;",
        "    const bool rooted = row != nullptr;",
    ),
    Mutant(
        "a creature with no head node and no row is rendered about its root all the same",
        PORTRAITS,
        "    const bool rooted = row != nullptr && row->fromRoot;",
        "    const bool rooted = row == nullptr || row->fromRoot;",
    ),
    Mutant(
        "a tester's values do not render a creature with no head node",
        PORTRAITS,
        "    return tuning.AnyValue() || (rooted && !tuning.kind.has_value());",
        "    return rooted && !tuning.kind.has_value();",
    ),
    Mutant(
        "a kind's default, asked for, frames a body with no feet-to-head",
        PORTRAITS,
        "    return tuning.AnyValue() || (rooted && !tuning.kind.has_value());",
        "    return tuning.AnyValue() || rooted;",
    ),
    Mutant(
        "a creature rendered from its root is said not to be ready",
        PORTRAITS,
        "                       report.lack == PortraitLack::Nothing || report.rendered, PortraitLackName(report.lack),",
        "                       report.lack == PortraitLack::Nothing, PortraitLackName(report.lack),",
    ),
    Mutant(
        "what a portrait's camera was set about is said the wrong way round",
        PORTRAITS,
        '    const std::string_view aimedAt = !report.rendered ? "" : report.head ? "head_node" : "scene_origin";',
        '    const std::string_view aimedAt = !report.rendered ? "" : report.head ? "scene_origin" : "head_node";',
    ),
    Mutant(
        "the deepstalker is turned as the rest are, and its round mouth is edge on",
        PORTRAIT_TABLE,
        '    FromRoot(Row(33, "33 Deepstalker", 0.9f, 20.0f, 0.0f, 0.29f, 0.83f,',
        '    FromRoot(Row(33, "33 Deepstalker", 0.9f, 35.0f, 0.0f, 0.29f, 0.83f,',
    ),
    Mutant(
        "the deepstalker's row is about a head it does not have, and its card keeps its initials",
        PORTRAIT_TABLE,
        '    FromRoot(Row(33, "33 Deepstalker", 0.9f, 20.0f, 0.0f, 0.29f, 0.83f,',
        '    (Row(33, "33 Deepstalker", 0.9f, 20.0f, 0.0f, 0.29f, 0.83f,',
    ),
    Mutant(
        "the halla's picture is at its feet",
        PORTRAIT_TABLE,
        '    FromRoot(Row(85, "85 Halla", 2.1f, 35.0f, 0.0f, 0.49f, 1.72f,',
        '    FromRoot(Row(85, "85 Halla", 2.1f, 35.0f, 0.0f, 0.49f, 0.0f,',
    ),
    Mutant(
        "the mark of a row from the root is lost in its making",
        PORTRAIT_TABLE,
        "    row.fromRoot = true;",
        "    row.fromRoot = false;",
    ),
    # The table's own numbers (src/core/PortraitTable.h): a row moved off what
    # was seen, a default turned with no aside, the limits.
    Mutant(
        "the high dragon's row is not the camera that was seen",
        PORTRAIT_TABLE,
        '    Row(66, "66 Dragon, High", 6.0f, 35.0f, 0.0f, -1.7f, -0.8f,',
        '    Row(66, "66 Dragon, High", 6.0f, 35.0f, 0.0f, 1.7f, -0.8f,',
    ),
    Mutant(
        "a row is turned with no aside, and its head slides out of the picture",
        PORTRAIT_TABLE,
        '    Row(64, "64 Dragonling", 0.9f, 35.0f, 0.0f, -0.5f, 0.45f,',
        '    Row(64, "64 Dragonling", 0.9f, 35.0f, 0.0f, 0.0f, 0.45f,',
    ),
    # The rows of the sweep, 2026-10-04: a side profile of each, a person's
    # turn for a person's body, and the aside to the side it was seen at.
    Mutant(
        "a row is left straight on: no side profile of a bronto",
        PORTRAIT_TABLE,
        '    Row(11, "11 Bronto", 3.0f, 35.0f, 0.0f, 0.35f, -0.45f,',
        '    Row(11, "11 Bronto", 3.0f, 0.0f, 0.0f, 0.35f, -0.45f,',
    ),
    Mutant(
        "a row is turned further than was asked for",
        PORTRAIT_TABLE,
        '    Row(41, "41 Rat, Small", 0.62f, 35.0f, 0.0f, 0.08f, -0.01f,',
        '    Row(41, "41 Rat, Small", 0.62f, 53.0f, 0.0f, 0.08f, -0.01f,',
    ),
    Mutant(
        "a dog's aside is to the dragons' side, and its head is out of the picture",
        PORTRAIT_TABLE,
        '    Row(34, "34 Dog, Mabari", 0.95f, 35.0f, 0.0f, 0.15f, 0.04f,',
        '    Row(34, "34 Dog, Mabari", 0.95f, 35.0f, 0.0f, -0.15f, 0.04f,',
    ),
    Mutant(
        "the hurlock alpha, a person's body, is turned as a creature is",
        PORTRAIT_TABLE,
        '    Row(17, "17 Hurlock - Alpha", 0.9f, 23.5f, -7.5f, 0.007f, 0.06f,',
        '    Row(17, "17 Hurlock - Alpha", 0.9f, 35.0f, -7.5f, 0.007f, 0.06f,',
    ),
    Mutant(
        "the werewolf is framed from a person's distance, its muzzle cut off",
        PORTRAIT_TABLE,
        '    Row(22, "22 Werewolf A", 1.05f, 23.5f, -7.5f, 0.15f, 0.1f,',
        '    Row(22, "22 Werewolf A", 0.675f, 23.5f, -7.5f, 0.15f, 0.1f,',
    ),
    Mutant(
        "the archdemon's picture is raised off its head, which hangs low",
        PORTRAIT_TABLE,
        '    Row(89, "89 Archdemon", 6.0f, 35.0f, 0.0f, -1.2f, -1.45f,',
        '    Row(89, "89 Archdemon", 6.0f, 35.0f, 0.0f, -1.2f, 1.45f,',
    ),
    Mutant(
        "the cat is given an aside it does not have",
        PORTRAIT_TABLE,
        '    Row(63, "63 Cat", 0.4f, 35.0f, 0.0f, 0.0f, 0.0f,',
        '    Row(63, "63 Cat", 0.4f, 35.0f, 0.0f, 0.14f, 0.0f,',
    ),
    Mutant(
        "a default is turned with no aside to go with it",
        PORTRAIT_TABLE,
        '    DefaultFor(BodyKind::OnAllFours, "a body on all fours", 0.6f, 0.0f, 0.0f, 0.0f, 0.0f,',
        '    DefaultFor(BodyKind::OnAllFours, "a body on all fours", 0.6f, 35.0f, 0.0f, 0.0f, 0.0f,',
    ),
    Mutant(
        "the table's furthest is past what the engine draws",
        PORTRAIT_TABLE,
        "inline constexpr float kFramedFromMost = 12.0f;",
        "inline constexpr float kFramedFromMost = 13.0f;",
    ),
    Mutant(
        "after its action is kept, a creature may still start something",
        MACHINE,
        "    const bool actionKept = chargeBegan_ || pendingEndNow_;",
        "    const bool actionKept = (chargeBegan_ || pendingEndNow_) && kind == CommandKind::Action;",
    ),
    Mutant(
        "a door costs nothing with time",
        MACHINE,
        "        if (Timed() && UsesAnObject(request.type) && settings_.objectSeconds > 0.0f)",
        "        if (Timed() && UsesAnObject(request.type) && settings_.objectSeconds > 100.0f)",
    ),
    Mutant(
        "a door's cost that does not fit is not owed",
        MACHINE,
        "            if (const std::uint32_t owed = budget_.spend.SpendAction(cost); owed > 0)\n            {\n                Owe(who->id, owed, request.type, -1);",
        "            if (const std::uint32_t owed = budget_.spend.SpendAction(cost); owed > 100000)\n            {\n                Owe(who->id, owed, request.type, -1);",
    ),
    Mutant(
        "a pickup is not an object's use",
        "src/core/Rules.cpp",
        "    case CommandType::GiveItem:\n    case CommandType::TakeItem:\n    case CommandType::PickupItem:\n        return true;",
        "    case CommandType::GiveItem:\n    case CommandType::TakeItem:\n        return true;",
    ),
    Mutant(
        "a shot is kept whoever its archer",
        "src/core/Rules.cpp",
        "    key.archer = archer;",
        "    key.archer = archer & 0U;",
    ),
    Mutant(
        "a shot is the last one's, not a mean",
        "src/core/Rules.cpp",
        "    if ((key.style == WeaponStyle::Dual || key.style == WeaponStyle::Ranged) && read.before > 0.0f)",
        "    if ((key.style == WeaponStyle::Dual) && read.before > 0.0f)",
    ),
    Mutant(
        "a creature that falls still owes the time it was held",
        MACHINE,
        "            if (c->down)\n            {\n                grabbedMs_.erase(c->id);\n            }",
        "            if (c->down && false)\n            {\n                grabbedMs_.erase(c->id);\n            }",
    ),
    Mutant(
        "a turn's end gives what its creature's clock brought due no beat",
        MACHINE,
        "land = seenActor_ && seenActor_->id == current->id && seenActor_->timersHeld;",
        "land = seenActor_ && seenActor_->id == current->id && seenActor_->timersHeld && false;",
    ),
    Mutant(
        "every turn's end waits a beat, whatever is held",
        MACHINE,
        "land = seenActor_ && seenActor_->id == current->id && seenActor_->timersHeld;",
        "land = true;",
    ),
    Mutant(
        "the beat between turns holds the world still",
        MACHINE,
        "(WantsPaused(phase_, settings_) && !Landing()) || playerPaused_",
        "WantsPaused(phase_, settings_) || playerPaused_",
    ),
    Mutant(
        "the beat between turns never ends",
        MACHINE,
        "lastNow_ - *landFromNow_ < kLandMs",
        "lastNow_ - *landFromNow_ < kLandMs * 1000",
    ),
    # Issue 43: the fight holds what the game has in combat, and as it lets
    # the world go, whoever it held with no turn to come is told to choose.
    Mutant(
        "one in combat that the gate turns away outside the order is forgotten: nobody tells it the fight is over",
        MACHINE,
        "            turnedAway_.insert(request.object);\n",
        "",
    ),
    Mutant(
        "those that left the fight alive are not told when it is over",
        MACHINE,
        "    for (const auto &[id, side] : left_)\n    {\n        owed.push_back(id);\n    }\n",
        "",
    ),
    Mutant(
        "those that left the fight, and those the gate turned away outside it, are not told as it ends",
        MACHINE,
        "    LetGo(fx);\n    outside_.clear();",
        "    outside_.clear();",
    ),
    Mutant(
        "those the gate turned away while the look was taken are not told when no fight follows",
        MACHINE,
        "        LetGo(fx);\n        return;\n",
        "        return;\n",
    ),
    Mutant(
        "one the game has taken out of combat is still held at the gate: its walk home is lost",
        MACHINE,
        "    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&\n        !(itsTurn && (landing || approaching)))",
        "    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&\n        !(itsTurn && (landing || approaching)) && false)",
    ),
    Mutant(
        "one the game has in combat has its script's commands let through as the game's as well",
        MACHINE,
        "    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&",
        "    if (request.source == Source::Script && who->side != Side::Party &&",
    ),
    Mutant(
        "a party member out of combat has its own AI's commands let through as the game's",
        MACHINE,
        "    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&",
        "    if (request.source == Source::Script && !request.inCombat &&",
    ),
    Mutant(
        "the player's order, and the engine's own command, for one out of combat are let through as the game's",
        MACHINE,
        "    if (request.source == Source::Script && !request.inCombat && who->side != Side::Party &&",
        "    if (!request.inCombat && who->side != Side::Party &&",
    ),
    # A creature that is not of the party and that the game does not have in
    # combat has no turn: found so in its own turn, that turn ends.
    Mutant(
        "the walk to a kept attack's target is taken for the game's sending its creature somewhere",
        MACHINE,
        "        !(itsTurn && (landing || approaching)))",
        "        !(itsTurn && landing))",
    ),
    Mutant(
        "a kept attack given to its creature is taken for the game's sending it something",
        MACHINE,
        "        !(itsTurn && (landing || approaching)))",
        "        !(itsTurn && approaching))",
    ),
    Mutant(
        "a script's command for one out of combat in its own turn does not end that turn",
        MACHINE,
        "        if (itsTurn && !unaware_ && !outOfCombat_)\n        {\n            outOfCombat_ = true;",
        "        if (itsTurn && !unaware_ && !outOfCombat_)\n        {",
    ),
    Mutant(
        "a script's command for one out of combat ends the turn of whoever's turn it is",
        MACHINE,
        "        if (itsTurn && !unaware_ && !outOfCombat_)",
        "        if (!unaware_ && !outOfCombat_)",
    ),
    Mutant(
        "a script's command in an unaware hostile's beat is said to end its turn, which is being skipped",
        MACHINE,
        "        if (itsTurn && !unaware_ && !outOfCombat_)",
        "        if (itsTurn && !outOfCombat_)",
    ),
    Mutant(
        "the gate says that a creature is out of combat once for each of its script's commands",
        MACHINE,
        "        if (itsTurn && !unaware_ && !outOfCombat_)",
        "        if (itsTurn && !unaware_)",
    ),
    Mutant(
        "what the gate said of a creature before its turn began takes the turn that then begins",
        MACHINE,
        "    outOfCombat_ = false;\n    orderPending_ = false;",
        "    orderPending_ = false;",
    ),
    Mutant(
        "one fallen as it lost the party has its turn ended as out of combat, not as fallen",
        MACHINE,
        "        const bool looked = obs.actor && obs.actor->id == current->id && obs.actor->exists && !obs.actor->down;",
        "        const bool looked = obs.actor && obs.actor->id == current->id && obs.actor->exists;",
    ),
    Mutant(
        "the gate's word that a creature is out of combat is not taken up: its turn goes on",
        MACHINE,
        "        const bool asked = std::exchange(outOfCombat_, false);",
        "        const bool asked = std::exchange(outOfCombat_, false) && false;",
    ),
    Mutant(
        "an unaware hostile's beat is cut short: its turn ends out of combat at once",
        MACHINE,
        "current != nullptr && InTurn() && !unaware_)",
        "current != nullptr && InTurn())",
    ),
    Mutant(
        "one with nothing to fight has its turn ended before it has begun",
        MACHINE,
        "current != nullptr && InTurn() && !unaware_)",
        "current != nullptr && !unaware_)",
    ),
    Mutant(
        "an ally out of combat with nobody to fight is told its turn has come",
        MACHINE,
        "    if (NothingToFightNow(who))\n    {",
        "    if (NothingToFightNow(who) && false)\n    {",
    ),
    Mutant(
        "a party member out of combat has nothing to fight, and no turn",
        RULES,
        "    if (side == Side::Party || perception.inCombat || perception.ofFoes)",
        "    if (perception.inCombat || perception.ofFoes)",
    ),
    Mutant(
        "one the game has in combat that perceives nobody has nothing to fight, and no turn",
        RULES,
        "    if (side == Side::Party || perception.inCombat || perception.ofFoes)",
        "    if (side == Side::Party || perception.ofFoes)",
    ),
    Mutant(
        "one out of combat that perceives someone hostile to it has nothing to fight, and no turn",
        RULES,
        "    if (side == Side::Party || perception.inCombat || perception.ofFoes)",
        "    if (side == Side::Party || perception.inCombat)",
    ),
    Mutant(
        "an enemy out of combat that perceives a party member has nothing to fight, and no turn",
        RULES,
        "    return side != Side::Enemy || !perception.ofParty;",
        "    return true;",
    ),
    Mutant(
        "a turn ended out of combat rolls its time over, as if its actor had saved it",
        TURNEND,
        '    TurnEnd{"out of combat", false},',
        '    TurnEnd{"out of combat", true},',
    ),
    Mutant(
        "a fight its enemies left alive is said to have ended with none standing",
        MACHINE,
        "[](const auto &gone) { return gone.second == Side::Enemy; }",
        "[](const auto &gone) { return gone.second == Side::Enemy && false; }",
    ),
    Mutant(
        "an ally that left a fight alive is counted among the enemies that left it",
        MACHINE,
        "[](const auto &gone) { return gone.second == Side::Enemy; }",
        "[](const auto &gone) { return gone.second != Side::Party; }",
    ),
    # Issue 44: a creature the game has made inactive has no turn.
    Mutant(
        "a creature the game has made inactive is told its turn has come like anyone",
        MACHINE,
        "    if (!InactiveNow(who))\n    {\n        return false;\n    }",
        "    if (!InactiveNow(who) || order_.Round() > 0)\n    {\n        return false;\n    }",
    ),
    Mutant(
        "a party member the game has made inactive is waited on for an order",
        MACHINE,
        "    return seenActor_ && seenActor_->id == who.id && seenActor_->exists && seenActor_->inactive;",
        "    return who.side != Side::Party && seenActor_ && seenActor_->id == who.id && seenActor_->exists &&\n           seenActor_->inactive;",
    ),
    Mutant(
        "made inactive in the middle of its turn, a creature's turn goes on",
        MACHINE,
        "        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current))",
        "        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current) && false)",
    ),
    Mutant(
        "a turn a conversation came in the middle of goes on, its creature made inactive meanwhile: it is told again",
        MACHINE,
        "        if (InactiveNow(*current))\n        {\n            EndInactive(*current, fx);\n            return;\n        }\n        ResumeTurn(*current, fx);",
        "        ResumeTurn(*current, fx);",
    ),
    Mutant(
        "one fallen as it was made inactive has its turn ended as inactive, not as fallen",
        MACHINE,
        "        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current))",
        "        current != nullptr && InTurn() && InactiveNow(*current))",
    ),
    Mutant(
        "an inactive creature's turn is ended before it has come, with no word of it to the scripts",
        MACHINE,
        "        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current))",
        "        current != nullptr && !ActorGone(obs) && InactiveNow(*current))",
    ),
    Mutant(
        "an unaware hostile made inactive in its beat has the beat stood out",
        MACHINE,
        "        current != nullptr && InTurn() && !ActorGone(obs) && InactiveNow(*current))",
        "        current != nullptr && InTurn() && !ActorGone(obs) && !unaware_ && InactiveNow(*current))",
    ),
    Mutant(
        "a hold on an inactive creature is not paid down as its turns pass",
        MACHINE,
        "    if (LoseTurn(*current, fx))\n    {\n        return;\n    }\n    if (InactiveTurn(*current, fx))",
        "    if (InactiveTurn(*current, fx) || LoseTurn(*current, fx))\n    {\n        return;\n    }\n    if (InactiveTurn(*current, fx))",
    ),
    Mutant(
        "the scripts are not told of an inactive creature's turn",
        MACHINE,
        '    TellTurnStart(who, fx);\n    clockSeenMs_ = 0;\n    clockRuns_ = true;\n    EndTurn(fx, "inactive");',
        '    clockSeenMs_ = 0;\n    clockRuns_ = true;\n    EndTurn(fx, "inactive");',
    ),
    Mutant(
        "an inactive creature's own clock does not move with its turn: what the game has waiting on it never comes",
        MACHINE,
        '    clockSeenMs_ = 0;\n    clockRuns_ = true;\n    EndTurn(fx, "inactive");',
        '    clockSeenMs_ = 0;\n    EndTurn(fx, "inactive");',
    ),
    Mutant(
        "a turn ended as inactive rolls its time over, as if its actor had saved it",
        TURNEND,
        '    TurnEnd{"inactive", false},',
        '    TurnEnd{"inactive", true},',
    ),
    Mutant(
        "a kept action turned or dropped for a target out of the world is said to be for one fallen or gone",
        MACHINE,
        "    return target != nullptr && !target->down;\n}\n\n// The game layer did not give a kept cast",
        "    return target != nullptr && !target->down && false;\n}\n\n// The game layer did not give a kept cast",
    ),
    Mutant(
        "a kept action turned or dropped for a fallen target is said to be for one out of the world",
        MACHINE,
        "    return target != nullptr && !target->down;\n}\n\n// The game layer did not give a kept cast",
        "    return target != nullptr;\n}\n\n// The game layer did not give a kept cast",
    ),
    Mutant(
        "a command the gate let through and the engine has not yet begun is taken for over",
        MACHINE,
        "    if (actor.busy)\n    {\n        idleTicks_ = 0;",
        "    if (actor.busy && actor.current != CommandType::Invalid)\n    {\n        idleTicks_ = 0;",
    ),
    Mutant(
        "one that waited outside and was turned away is told twice as the fight ends",
        MACHINE,
        "        if (std::ranges::any_of(fx, [&](const Effect &e) { return e.kind == EffectKind::PokeAi && e.target == id; }))\n        {\n            continue;\n        }\n",
        "",
    ),
    Mutant(
        "one turned away as combat began is owed a telling though the fight has it: fallen, it is told",
        MACHINE,
        "    std::erase_if(turnedAway_, [&](ObjectId id) { return order_.Find(id) != nullptr; });\n",
        "",
    ),
    Mutant(
        "one turned away that joins the fight is owed a telling still: fallen, it is told",
        MACHINE,
        "            turnedAway_.erase(seen.id); // its turns are to come: told at each, and not as one with none\n",
        "",
    ),
    Mutant(
        "a fight dropped with the world owes its tellings in the next",
        MACHINE,
        "        turnedAway_.clear(); // gone with the world, as everyone is: nobody to tell\n",
        "",
    ),
    Mutant(
        "those told as a fight ends are owed the telling again at the next fight's end",
        MACHINE,
        "    owed.erase(std::ranges::unique(owed).begin(), owed.end());\n    turnedAway_.clear();\n",
        "    owed.erase(std::ranges::unique(owed).begin(), owed.end());\n",
    ),
    Mutant(
        "those the fight lets go are told in whatever order they are kept in",
        MACHINE,
        "    std::ranges::sort(owed);\n",
        "",
    ),
]


def run(
    command: list[str], cwd: Path, timeout: int | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
        creationflags=LOW,
    )


def sync(worker: Path) -> None:
    """Brings a worker's copy up to the tree: a file is written only where it
    differs, so what did not change is not built again."""
    wanted: set[Path] = set()
    sources = [REPO / name for name in FILES if (REPO / name).exists()]
    for tree in TREES:
        sources += [f for f in (REPO / tree).rglob("*") if f.is_file()]
    for source in sources:
        target = worker / source.relative_to(REPO)
        wanted.add(target)
        data = source.read_bytes()
        if not target.exists() or target.read_bytes() != data:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    for tree in TREES:
        for stale in (worker / tree).rglob("*"):
            if stale.is_file() and stale not in wanted:
                stale.unlink()


def made_with() -> str:
    """The build program the tree's own core build was configured with."""
    cache = REPO / "build" / "core" / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_MAKE_PROGRAM:"):
                return line.partition("=")[2].strip()
    return ""


def tests_of(worker: Path) -> Path:
    return worker / "build" / "core" / "tests" / "tb_tests.exe"


def prepare(worker: Path, jobs: int) -> str:
    """A worker's copy, configured and built as the tree is. Answers what went
    wrong, or nothing."""
    sync(worker)
    if not (worker / "build" / "core" / "build.ninja").exists():
        command = [
            "cmake",
            "--preset",
            "core",
            f"-DVCPKG_INSTALLED_DIR={PACKAGES.as_posix()}",
            "-DVCPKG_MANIFEST_INSTALL=OFF",
        ]
        # Ninja as the tree's own build found it: it need not be on the path
        # of the shell this runs in.
        if ninja := made_with():
            command.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")
        configured = run(command, worker)
        if configured.returncode != 0:
            return (configured.stdout + configured.stderr)[-2000:]
    built = build(worker, jobs)
    if built.returncode != 0:
        return (built.stdout + built.stderr)[-2000:]
    return ""


def build(worker: Path, jobs: int) -> subprocess.CompletedProcess[str]:
    return run(
        ["cmake", "--build", "--preset", "core", "--parallel", str(jobs)], worker
    )


def test(mutant: Mutant, worker: Path) -> str:
    """One mutant, in a worker's copy, from the copy as it was made: an edit to
    the tree while mutants run is not taken up half way (it was, read from the
    tree each time: a run whose mutants "did not compile" by the dozen,
    2026-10-02)."""
    original = (worker / mutant.file).read_text(encoding="utf-8")
    if original.count(mutant.old) != 1:
        return f"not applied: the text is in {mutant.file} {original.count(mutant.old)} times"
    path = worker / mutant.file
    try:
        path.write_text(original.replace(mutant.old, mutant.new), encoding="utf-8")
        # One file changed: one compiler.
        if build(worker, 1).returncode != 0:
            return "did not compile"
        # The scripted cases first, and the random fights only for a mutant
        # they let pass: of a mutant's 30 s, 22 were the two random fights'
        # tests, run to their end whatever had failed before them, and the
        # 533 scripted ones take under 3 (timed 2026-10-04, when three gates
        # at once made a full run an hour; the user: "Why does a full mutant
        # run take an hour?"). The verdict is the same: caught if any test
        # fails, and both halves together are every test the binary runs by
        # default. Which half caught it is said, since a mutant only the
        # fights catch is a promise with no scripted case of its own.
        for spec, by in (("~[fights]", ""), ("[fights]", " (by the fights)")):
            try:
                result = run(
                    [str(tests_of(worker)), spec, "--abort"], worker, timeout=TIMEOUT_S
                )
            except subprocess.TimeoutExpired:
                return "caught (the tests hung)"
            if result.returncode != 0:
                return f"caught{by}"
        return "SURVIVED"
    finally:
        path.write_text(original, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "names", nargs="*", help="only mutants whose name contains one of these"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=os.cpu_count() or 2,
        help="how many mutants at a time (by default one a core)",
    )
    args = parser.parse_args()
    chosen = [
        m for m in MUTANTS if not args.names or any(w in m.name for w in args.names)
    ]
    if not PACKAGES.exists():
        print(
            f"no {PACKAGES}: build the core preset once (tools\\build.ps1 -Preset core)"
        )
        return 1
    count = max(1, min(args.jobs, len(chosen)))
    workers = [WORK / f"w{n}" for n in range(count)]
    started = time.monotonic()

    # The copies, built side by side: each with its share of the cores.
    share = max(1, (os.cpu_count() or 2) // count)
    problems: dict[Path, str] = {}

    def ready(worker: Path) -> None:
        problems[worker] = prepare(worker, share)

    threads = [threading.Thread(target=ready, args=(w,)) for w in workers]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    for worker, problem in problems.items():
        if problem:
            print(f"{worker.name} does not configure or build:\n{problem}")
            return 1
    if run([str(tests_of(workers[0]))], workers[0]).returncode != 0:
        print("the unmutated core does not pass its tests; fix that first")
        return 1
    print(
        f"{count} at a time, in {WORK} ({time.monotonic() - started:.0f} s to make ready)",
        flush=True,
    )

    todo: queue.Queue[Mutant] = queue.Queue()
    for mutant in chosen:
        todo.put(mutant)
    verdicts: dict[str, str] = {}
    said = threading.Lock()

    def work(worker: Path) -> None:
        while True:
            try:
                mutant = todo.get_nowait()
            except queue.Empty:
                return
            start = time.monotonic()
            # Whatever goes wrong is said, and counted as not caught.
            try:
                verdict = test(mutant, worker)
            except Exception as error:  # noqa: BLE001
                verdict = f"failed: {error}"
            with said:
                verdicts[mutant.name] = verdict
                print(
                    f"{verdict:<32} {mutant.name} ({time.monotonic() - start:.0f} s)",
                    flush=True,
                )

    threads = [threading.Thread(target=work, args=(w,)) for w in workers]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    bad = [
        name for name, verdict in verdicts.items() if not verdict.startswith("caught")
    ]
    hung = [name for name, verdict in verdicts.items() if "hung" in verdict]
    for name in hung:
        print(f"hung, and counted as caught: {name}")
    fights = sum(1 for verdict in verdicts.values() if "by the fights" in verdict)
    print(f"{fights} caught by the random fights alone, with no scripted case")
    print(
        f"{len(chosen) - len(bad)} of {len(chosen)} caught in {time.monotonic() - started:.0f} s"
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
