"""Break src/core on purpose, one way at a time, and see the tests fail.

SkyrimFollowerTactics' rule (its dev/PRINCIPLES.md, "Check that the tests
would catch a bug"): a green suite can pass while testing nothing, and
breaking the code on purpose shows what is never checked. Each mutant below
is one promise of dev/DESIGN.md broken by a small edit. The tool applies it,
builds the core and its tests, and runs the tests. A mutant is caught if a
test fails (or the run hangs past the timeout), and survives if every test
passes. A survivor is a promise no test holds.

A mutant's time was its build, not its tests (issue 67, 2026-10-09: 85% of
23.5 s; the user: "I think the way we currently test mutants is way too
slow. We apply a mutant, then build, then run all the tests"), so the tool
builds as little as it can and runs as few tests as say the verdict. A
worker's build has no debug information (FLAGS). The mutants go to the
workers file by file, each written straight over the last, so a mutant's
build is of its own file alone (Pool). And the test that caught a mutant
last time is run first, the rest only if it passes: tools\\mutants.caught.json
has it for each, and a run brings that file up to date, saying of a mutant
whose kept test did not catch it that another did (it is news: a test may
have stopped holding that promise). Six at a time on a machine at full load,
a sample of 42 mutants was 26.0 s a mutant and is 7.9, and all 786 are 11
minutes: 3.4 s for a .cpp's mutant, most of it the compiler, 23 for a
header's.

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

Whether each mutant earns its keep is another run's to say (the user: "Do we
need all the mutants? Make sure each mutant earns its keep"):

    python tools\\mutants.py --matrix build\\matrix.json

runs every test against each mutant, without stopping at the first that
fails or asking the kept one first, and writes to the file what failed for
each: every assertion that failed, by its test case and its place (for a
random fight, the promise that was broken). It ends with what that says of
the list (report): the mutants that failed the same assertions in the same
tests as another, for a reader who knows the promises to judge; the mutants
that failed all that another of the same function did and more, which go,
each the same promise broken more crudely than the one that stands for it
and caught whenever that one is; and the mutants only the fights caught. A
new mutant that is the crude twin of a fine one is not wanted.

The file is written as the run goes and a mutant it has is not run again, so
a stopped run goes on where it was and a finished one only says its report;
a new record is a new file, and a record is of the tree it was made over.
A kept test that is not among those that failed is replaced by the
first that did: the way to see that a mutant the fights alone caught now has
a scripted case.
"""

from __future__ import annotations

import argparse
import json
import msvcrt
import os
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from html import unescape
from pathlib import Path, PureWindowsPath
from typing import IO, NamedTuple

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
PARTYTACTICS = "src/core/PartyTactics.cpp"
FACING = "src/core/Facing.cpp"
TIMEDSTEP = "src/core/TimedStep.cpp"
TEXT = "src/core/Text.h"
STATE = "src/core/State.cpp"

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
        "an AI may think forever",
        MACHINE,
        "    if (clock_ >= deadline_)\n    {\n        // What its AI saw",
        "    if (false)\n    {\n        // What its AI saw",
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
    # A turn an AI plays has a time limit, max_turn_ms (issue 38): what ends
    # a turn of steps that take nothing, with nothing counting its commands.
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
        "a condition that has had its time is never ended",
        MACHINE,
        "    if (held.owedMs == 0 && held.seen != Condition::None)",
        "    if (held.owedMs > 4000000000U && held.seen != Condition::None)",
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
        "    if (budget_.spend.CapacityLeftMs() > 0)\n    {\n        return false;\n    }\n    Log(LogLevel::Info,",
        "    if (budget_.heldMs < 2000)\n    {\n        return false;\n    }\n    Log(LogLevel::Info,",
    ),
    Mutant(
        "a turn waits out a hold however long it runs",
        MACHINE,
        "    if (budget_.spend.CapacityLeftMs() > 0)\n    {\n        return false;\n    }\n    Log(LogLevel::Info,",
        "    if (budget_.heldMs < 4000000000U)\n    {\n        return false;\n    }\n    Log(LogLevel::Info,",
    ),
    Mutant(
        "time: a hold takes its time of the bar as an action does, not of the clock by the turn's ratio",
        MACHINE,
        "    (void)budget_.spend.HoldClock(ran);",
        "    (void)budget_.spend.SpendAction(ran);",
    ),
    Mutant(
        "time: a hold does not move its creature's own clock: an effect on it cannot wear off within the turn",
        MACHINE,
        "    budget_.holdBarMs += before - budget_.spend.CapacityLeftMs();\n    MoveClock(fx);",
        "    budget_.holdBarMs += before - budget_.spend.CapacityLeftMs();",
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
        "    ChargeHold(fx, false);\n    budget_.heldSeen.reset();",
        "    ChargeHold(fx, false);",
    ),
    Mutant(
        "let go straight into another command, a creature is not charged what its step still had to run",
        MACHINE,
        "    ChargeHold(fx, false);\n    budget_.heldSeen.reset();",
        "    (void)fx;\n    budget_.heldSeen.reset();",
    ),
    Mutant(
        "a frame between one command of an effect's and the next lets its creature go: what it kept is given into the gap",
        MACHINE,
        "        if (!actor.busy && ++idleTicks_ < 2)\n        {\n            ChargeHold(fx, false);\n            return;\n        }\n        heldByEffect_ = false;",
        "        heldByEffect_ = false;",
    ),
    # A hold costs what the engine held its creature (issue 64; the owner,
    # 2026-10-08: "knock down should last for the amount of time the game makes
    # it last"): the engine's own count of its command's step where it times
    # it, the fight's clock where it does not, and nothing for the looks that
    # find the creature free. In play a getting up of 2,133 ms cost 2.30 to
    # 2.81 s, and a knockdown of under four seconds the whole turn.
    Mutant(
        "hold: charged by the fight's clock, whatever the engine counted of its step",
        MACHINE,
        "    const std::uint32_t ran = counted.value_or(held ? since : 0);",
        "    const std::uint32_t ran = held ? since : 0;",
    ),
    Mutant(
        "hold: the looks that find its creature free are charged the fight's clock",
        MACHINE,
        "    const std::uint32_t ran = counted.value_or(held ? since : 0);",
        "    const std::uint32_t ran = counted.value_or(since);\n    (void)held;",
    ),
    Mutant(
        "hold: a look that still finds its creature held, the engine timing nothing, is charged nothing",
        MACHINE,
        "    const std::uint32_t ran = counted.value_or(held ? since : 0);",
        "    const std::uint32_t ran = counted.value_or(0);\n    (void)held;\n    (void)since;",
    ),
    Mutant(
        "hold: what a step ran while a conversation played is taken of the bar as the fight goes on",
        MACHINE,
        '    budget_.step = StepWatch{};\n    if (seenActor_ && seenActor_->id == who.id)\n    {\n        (void)budget_.step.Look(seenActor_->step, clock_);\n    }\n    Log(LogLevel::Info, std::format("{}\'s turn goes on: {}"',
        '    Log(LogLevel::Info, std::format("{}\'s turn goes on: {}"',
    ),
    Mutant(
        "hold: what its step ran between the turn's beginning and the machine's first look at the hold is not charged",
        MACHINE,
        "    if (seenActor_ && seenActor_->id == who.id)\n    {\n        (void)budget_.step.Look(seenActor_->step, clock_);\n    }\n    // The turn begins: it is played to its end",
        "    // The turn begins: it is played to its end",
    ),
    Mutant(
        "hold: one that notices in its beat is charged what its step ran while it stood unaware",
        MACHINE,
        "        (void)budget_.step.Look(seenActor_->step, clock_);\n        return;",
        "        return;",
    ),
    Mutant(
        "hold: a step found further on ran the watcher's time since, not what its elapsed time moved by",
        TIMEDSTEP,
        "        return static_cast<std::uint32_t>(now.elapsedMs - before.elapsedMs);",
        "        return since;",
    ),
    Mutant(
        "hold: a step spent at the look before, its length pushed ahead since, ran nothing",
        TIMEDSTEP,
        "    const bool further = before.durationMs > 0 && now.durationMs > 0 && now.elapsedMs > before.elapsedMs;",
        "    const bool further = HasTimeLeft(before) && now.durationMs > 0 && now.elapsedMs > before.elapsedMs;",
    ),
    Mutant(
        "hold: a step that stands ran nothing by the engine's count, where the engine counted nothing",
        TIMEDSTEP,
        "    const bool further = before.durationMs > 0 && now.durationMs > 0 && now.elapsedMs > before.elapsedMs;",
        "    const bool further = before.durationMs > 0 && now.durationMs > 0 && now.elapsedMs >= before.elapsedMs;",
    ),
    Mutant(
        "hold: a step the engine does not time ran what its count moved by",
        TIMEDSTEP,
        "    const bool further = before.durationMs > 0 && now.durationMs > 0 && now.elapsedMs > before.elapsedMs;",
        "    const bool further = now.elapsedMs > before.elapsedMs;",
    ),
    Mutant(
        "hold: a step gone ran all it had left, however little of the watcher's time went by",
        TIMEDSTEP,
        "        return std::min(static_cast<std::uint32_t>(before.durationMs - before.elapsedMs), since);",
        "        (void)since;\n        return static_cast<std::uint32_t>(before.durationMs - before.elapsedMs);",
    ),
    Mutant(
        "hold: a step begun anew is not one gone: the one before it ran nothing more",
        TIMEDSTEP,
        "    const bool gone = now.durationMs <= 0 || now.elapsedMs < before.elapsedMs;",
        "    const bool gone = now.durationMs <= 0;",
    ),
    Mutant(
        "hold: a step spent and gone ran the watcher's time since",
        TIMEDSTEP,
        "    if (HasTimeLeft(before) && gone)\n    {\n        return std::min(static_cast<std::uint32_t>(before.durationMs - before.elapsedMs), since);\n    }\n    return std::nullopt;",
        "    if (HasTimeLeft(before) && gone)\n    {\n        return std::min(static_cast<std::uint32_t>(before.durationMs - before.elapsedMs), since);\n    }\n    return gone && before.durationMs > 0 ? std::optional<std::uint32_t>{since} : std::nullopt;",
    ),
    Mutant(
        "max_action_ms stops a hold, and clears the effect's command",
        MACHINE,
        "        if (!heldByEffect_ && counted > limit)",
        "        if (counted > limit)",
    ),
    Mutant(
        "an AI whose bar a hold spent as it let go is asked to choose with nothing",
        MACHINE,
        "        if (!actor.busy && current != nullptr && !PlayerControls(*current) && budget_.spend.TimeLeft())",
        "        if (!actor.busy && current != nullptr && !PlayerControls(*current))",
    ),
    Mutant(
        "an AI getting up from a condition the machine ended is told to choose behind the rise",
        MACHINE,
        "&& AtWhatItDidNotChoose(*seenActor_))\n    {\n        heldByEffect_ = true;",
        "&& AtWhatItDidNotChoose(*seenActor_) && endedFor_ != who.id)\n    {\n        heldByEffect_ = true;",
    ),
    # Where a hold (issue 30) meets a hostile that has noticed nobody (issue 29).
    Mutant(
        "an unaware hostile at an effect's command stands out its hold, and then its beat",
        MACHINE,
        "    if (!UnawareNow(who) && seenActor_ && seenActor_->id == who.id && AtWhatItDidNotChoose(*seenActor_))",
        "    if (seenActor_ && seenActor_->id == who.id && AtWhatItDidNotChoose(*seenActor_))",
    ),
    Mutant(
        "a party member thrown as its turn waits, after it has acted, is taken for one at its own action's effect",
        MACHINE,
        '": " + obs.actor->effects));\n        }\n        heldByEffect_ = AtWhatItDidNotChoose(*obs.actor);',
        '": " + obs.actor->effects));\n        }\n        heldByEffect_ = false;',
    ),
    Mutant(
        "an AI thrown as it thinks, after it has acted, is taken for one at its own action's effect",
        MACHINE,
        "        // executing: busy here is something else's.\n        heldByEffect_ = AtWhatItDidNotChoose(*obs.actor);",
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
        "        if (!heldByEffect_ && AtWhatItDidNotChoose(actor) && budget_.commands == 0)",
        "        if (!heldByEffect_ && AtWhatItDidNotChoose(actor) && budget_.commands == 0 &&\n            budget_.spend.ClockElapsedMs() == 0)",
    ),
    # Where a hold meets an attack the engine has no way to make (issue 27).
    Mutant(
        "a held creature's attack the engine has no way to make is one given back, not one refused as held",
        MACHINE,
        "    if (heldByEffect_ && kind != CommandKind::Free)",
        "    if (heldByEffect_ && kind != CommandKind::Free && request.noAttack == NoAttack::None)",
    ),
    Mutant(
        "a creature an effect let go stays held",
        MACHINE,
        "        heldByEffect_ = false; // let go: the turn is its own again, with what the hold left of it",
        "        // let go",
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
        "the speed is written at 1 with none of the mod's own to take back",
        MACHINE,
        "    if (speed == 1.0f && speedSet_ == 1.0f)\n    {\n        return;\n    }\n    speedSet_ = speed;",
        "    speedSet_ = speed;",
    ),
    Mutant(
        "the speed is said only as the machine's belief of it changes: put back to 1 by the game, it stays there (issue 71)",
        MACHINE,
        "    if (speed == 1.0f && speedSet_ == 1.0f)",
        "    if (speed == speedSet_)",
    ),
    Mutant(
        "the fight's speed is said every frame, over the game's own slow motion (issue 71)",
        MACHINE,
        "        clock_ += worldDt;\n        roundRunMs_ += worldDt;",
        "        clock_ += worldDt;\n        roundRunMs_ += worldDt;\n        SetSpeed(TurnSpeed(), fx);",
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
        "a fall is heard as a creature gone",
        MACHINE,
        "            Emit(EventKind::Fell, c->id);",
        "            Emit(EventKind::Gone, c->id);",
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
        "        if (held.seen == Condition::None && held.owedMs == 0)",
        "        if (held.seen == Condition::None || held.owedMs < 4000000000U)",
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
        "            wantScan_ = true;\n            return;\n        }\n        End(fx, NoEnemyLeft());\n        return;",
        "            wantScan_ = true;\n            return;\n        }\n        return;",
    ),
    Mutant(
        "the state's last refusal is anyone's",
        MACHINE,
        "    state.lastRefusal = lastPlayerRefusal_;",
        "    state.lastRefusal = lastRefusal_;",
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
        "    // there (src/game/Camera.cpp).\n    SetSpeed(TurnSpeed(), fx);",
        "    // there (src/game/Camera.cpp).",
    ),
    Mutant(
        "a conversation's end leaves the fight's speed to a turn's beginning, which a turn handed over at once never comes to (issue 71)",
        MACHINE,
        "        // own speed).\n        SetSpeed(TurnSpeed(), fx);",
        "        // own speed).",
    ),
    Mutant(
        "a fight stood down begins again while the game still says combat",
        MACHINE,
        "    return settings_.enabled && !stoodDown_;",
        "    return settings_.enabled;",
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
        "an enemy not fighting the party is in the fight from its start",
        MACHINE,
        "    std::erase_if(roster, [&](const Combatant &c) { return !in(c); });",
        "    std::erase_if(roster, [&](const Combatant &c) { return !in(c) && c.down; });",
    ),
    Mutant(
        "an enemy not fighting the party begins the turns again",
        MACHINE,
        "return c.side == Side::Enemy && InRotation(InTheRotation(Scanned(c, false)));",
        "return c.side == Side::Enemy && !c.down;",
    ),
    Mutant(
        "one that left the fight is up again by its health",
        MACHINE,
        "        const bool wasDown = std::exchange(c->down, c->down || status.down);",
        "        const bool wasDown = std::exchange(c->down, status.down);",
    ),
    # Issue 29: leaving the fight is the engine's perception. One the scan's
    # own rules no longer find fighting (out of combat, or beyond the radius)
    # stays while the party perceives it or it the party.
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
        "    if (!seen.found && !(seen.member && KeptByPerception(seen.side, seen.perception)))",
        "    if (!(seen.member && KeptByPerception(seen.side, seen.perception)))",
    ),
    Mutant(
        "one neither found fighting nor perceived stays in the fight for being read",
        MACHINE,
        "(where == Rotation::NotFighting && partyStands);",
        "(where == Rotation::NotFighting && partyStands && putOut);",
    ),
    Mutant(
        "being perceived brings one that is not in the fight into it",
        RULES,
        "    if (!seen.found && !(seen.member && KeptByPerception(seen.side, seen.perception)))",
        "    if (!seen.found && !KeptByPerception(seen.side, seen.perception))",
    ),
    Mutant(
        "being perceived brings one that left the fight back into it",
        MACHINE,
        "Scanned(seen, known != nullptr && !known->down && !putOut));",
        "Scanned(seen, known != nullptr && !putOut));",
    ),
    Mutant(
        "a fallen party member has no place in the rotation",
        RULES,
        "    case Rotation::Fallen:\n    case Rotation::Unaware:\n    case Rotation::OutOfCombat:\n        return true;",
        "    case Rotation::Unaware:\n    case Rotation::OutOfCombat:\n        return true;",
    ),
    Mutant(
        "one not found fighting the party, and not kept in its fight by perception, is in its rotation",
        RULES,
        "    case Rotation::Dead:\n    case Rotation::OutOfTheWorld:\n    case Rotation::NotFighting:\n        return false;",
        "    case Rotation::Dead:\n    case Rotation::OutOfTheWorld:\n        return false;\n    case Rotation::NotFighting:\n        return true;",
    ),
    Mutant(
        "a dead creature that is not the party's has a place in the rotation, as a fallen party member has",
        RULES,
        "        return seen.side == Side::Party ? Rotation::Fallen : Rotation::Dead;",
        "        return Rotation::Fallen;",
    ),
    Mutant(
        "one back in the fight goes on with what it took up while it was out of it",
        MACHINE,
        "                // scripts while it was out). And its clock is its own again.\n                fx.push_back(Effect::Of(EffectKind::ClearCommands, seen.id));",
        "                // scripts while it was out). And its clock is its own again.",
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
        "an AI whose action was given back is not told to choose again",
        MACHINE,
        "    if (!player && budget_.spend.TimeLeft() && budget_.refunded && !budget_.retried && !budget_.outOfMovement)",
        "    if (!player && budget_.spend.TimeLeft() && !budget_.refunded && !budget_.retried && !budget_.outOfMovement)",
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
        "            if (ability == request.ability)\n            {\n                cost = static_cast<std::uint32_t>(turns) * TurnMs();",
        "            if (ability != request.ability)\n            {\n                cost = static_cast<std::uint32_t>(turns) * TurnMs();",
    ),
    Mutant(
        "an AI whose bar was spent as its kept action landed is told again",
        MACHINE,
        "        if (player || budget_.spend.TimeLeft())\n        {\n            Handover(*current, fx);",
        "        if (player || budget_.spend.CapacityLeftMs() < 4000000000U)\n        {\n            Handover(*current, fx);",
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
        "centre.y + (std::sin(angle) * radius), centre.z}",
        "centre.y + (std::cos(angle) * radius), centre.z}",
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
        "        along = std::clamp(along, 0.0f, 1.0f);",
        "        along = std::max(along, 0.0f);",
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
    # Cooldowns in turns, as the engine sets them (issue 57): the machine
    # reckons none and looks for none. It is told as the engine sets one, and
    # reads what is cooling on a creature once, as it comes into a fight.
    Mutant(
        "the machine is not told as the engine sets a cooldown: the Overseer orders its spell again and again",
        MACHINE,
        "    const Combatant *who = phase_ != Phase::Idle ? order_.Find(creature) : nullptr;",
        "    const Combatant *who = phase_ == Phase::Idle ? order_.Find(creature) : nullptr;",
    ),
    Mutant(
        "a hold of one gone from the fight's order outlives the engine's setting its cooldown again",
        MACHINE,
        "        std::erase_if(holds_,\n"
        "                      [&](const Hold &h) { return h.creature == creature && h.ability == ability && h.item == item; });\n"
        "        return;",
        "        return;",
    ),
    Mutant(
        "what was cooling on a creature as it came into the fight is never read",
        MACHINE,
        "            if (!cooled_.contains(fighter.id))",
        "            if (!cooled_.contains(fighter.id) && fighter.id == kNoObject)",
    ),
    Mutant(
        "a creature's cooldowns are read at every look, and not once",
        MACHINE,
        "    cooled_.insert(look.id);",
        "    (void)cooled_;",
    ),
    Mutant(
        "one that comes back into the fight is not read again",
        MACHINE,
        "            cooled_.erase(joined); // what cools on it is read as it comes in, this time too\n",
        "",
    ),
    Mutant(
        "the next fight reads nobody the last one read",
        MACHINE,
        "    holds_.clear();\n    cooled_.clear();",
        "    holds_.clear();",
    ),
    Mutant(
        "a cooldown the machine holds already is taken again as its creature's are read",
        MACHINE,
        "        if (held)\n        {\n            continue;",
        "        if (held && r.total < 0.0f)\n        {\n            continue;",
    ),
    Mutant(
        "a cooldown found running is taken at what it was set to, not at what the engine has left of it",
        MACHINE,
        "    Hold taken{who.id, r.ability, r.item, leftMs, std::max(leftMs, totalMs)};",
        "    Hold taken{who.id, r.ability, r.item, std::max(leftMs, totalMs), std::max(leftMs, totalMs)};",
    ),
    Mutant(
        "a cooldown shorter than a turn waits a turn's worth all the same",
        MACHINE,
        "    Hold taken{who.id, r.ability, r.item, leftMs, std::max(leftMs, totalMs)};",
        "    Hold taken{who.id, r.ability, r.item, std::max(leftMs, TurnMs()), std::max({leftMs, totalMs, TurnMs()})};",
    ),
    Mutant(
        "a cooldown taken is not held in the engine",
        MACHINE,
        "    fx.push_back(HoldIt(taken));",
        "    (void)fx;",
    ),
    Mutant(
        "a hold is its hour for the total and what is left its share: one with little left is back by waiting",
        MACHINE,
        "    return CooldownOf(h.creature, h.ability, h.item, kCooldownHoldSeconds,\n"
        "                      static_cast<std::int32_t>(std::clamp<std::uint64_t>(total, kCooldownHoldSeconds, kMost)));",
        "    return CooldownOf(h.creature, h.ability, h.item,\n"
        "                      static_cast<std::int32_t>(std::max<std::uint64_t>(\n"
        "                          2, static_cast<std::uint64_t>(kCooldownHoldSeconds) * h.leftMs / std::max(h.ofMs, 1u))),\n"
        "                      kCooldownHoldSeconds + static_cast<std::int32_t>((total * 0) + (kMost * 0)));",
    ),
    Mutant(
        "a held cooldown's sweep stays full",
        MACHINE,
        "static_cast<std::uint64_t>(kCooldownHoldSeconds) * h.ofMs / std::max(h.leftMs, 1u);",
        "static_cast<std::uint64_t>(kCooldownHoldSeconds) * h.ofMs / std::max(h.ofMs, 1u);",
    ),
    Mutant(
        "a held cooldown the engine set again keeps the seconds the machine had",
        MACHINE,
        "    if (anew)\n    {\n        *held = std::move(taken);",
        "    if (anew)\n    {\n        (void)taken;",
    ),
    Mutant(
        "a held cooldown the engine set to nothing stays on the machine's book",
        MACHINE,
        "            holds_.erase(held);\n",
        "",
    ),
    Mutant(
        "a hold the engine ended has the sweep the machine had still to set, set all the same",
        MACHINE,
        "            std::erase_if(fx, [&](const Effect &e) {\n"
        "                return e.kind == EffectKind::Cooldown && e.target == who.id && e.a == r.ability && e.text == r.item;\n"
        "            });",
        "            (void)fx;",
    ),
    # A fight dropped by a load (issue 90): the book of holds is kept through
    # the load and given back as the world is there again, unless a game was
    # loaded meanwhile, whose world is another. (Two mutants went with what
    # they broke, the recoveries for a hold no fight wrote back: one left at
    # its hour, and one that grows.)
    Mutant(
        "load: a fight dropped by a load forgets the cooldowns it holds, and they stand at their hour",
        MACHINE,
        "            // whoever goes on with the party: GiveBackDropped.)\n            order_.Clear();\n",
        "            // whoever goes on with the party: GiveBackDropped.)\n            order_.Clear();\n"
        "            holds_.clear();\n",
    ),
    Mutant(
        "load: a game loaded is written the cooldowns the dropped fight held",
        MACHINE,
        '                                    holds_.size(), one ? "" : "s", one ? "was" : "were", one ? "is" : "are"));\n'
        "    holds_.clear();\n",
        '                                    holds_.size(), one ? "" : "s", one ? "was" : "were", one ? "is" : "are"));\n',
    ),
    Mutant(
        "an item's ability's cooldown is set with no item's tag",
        MACHINE,
        "    e.a = ability;\n    e.text = item;",
        "    e.a = ability;\n    (void)item;",
    ),
    Mutant(
        "an item's ability's cooldown is taken for the creature's own of that number",
        MACHINE,
        "holds_, [&](const Hold &h) { return h.creature == who.id && h.ability == r.ability && h.item == r.item; });",
        "holds_, [&](const Hold &h) { return h.creature == who.id && h.ability == r.ability; });",
    ),
    Mutant(
        "an item's ability's cooldown is released with no item's tag",
        MACHINE,
        "            fx.push_back(CooldownOf(h.creature, h.ability, h.item, 0, 0));",
        "            fx.push_back(CooldownOf(h.creature, h.ability, {}, 0, 0));",
    ),
    Mutant(
        "the fight's end gives an item's ability's cooldown back with no item's tag",
        MACHINE,
        "        fx.push_back(CooldownOf(h.creature, h.ability, h.item, static_cast<std::int32_t>((h.leftMs + 999) / 1000),",
        "        fx.push_back(CooldownOf(h.creature, h.ability, {}, static_cast<std::int32_t>((h.leftMs + 999) / 1000),",
    ),
    Mutant(
        "the state says an item's ability's cooldown without its item",
        STATE,
        "        if (!cooldown.item.empty())",
        "        if (cooldown.item.empty())",
    ),
    Mutant(
        "a sustained ability's toggle costs the action",
        MACHINE,
        "    {\n        kind = CommandKind::Free;\n    }",
        "    {\n        kind = CommandKind::Action;\n    }",
    ),
    Mutant(
        "held cooldowns stay held after the fight",
        MACHINE,
        "    approach_ = false;\n    ReleaseCooldowns(fx);",
        "    approach_ = false;",
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
        "    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
        "    return (partyInCombat || true) && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn in unseen",
        RULES,
        "    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
        "    return partyInCombat && !someoneToFight && (sighting.seen || true) && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn in from beyond the join radius",
        RULES,
        "    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
        "    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.combatRadius;",
    ),
    Mutant(
        "a hostile that has not noticed is drawn into a fight that has an enemy",
        RULES,
        "    return partyInCombat && !someoneToFight && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
        "    return partyInCombat && (!someoneToFight || true) && sighting.seen && std::isfinite(sighting.meters) &&\n           sighting.meters <= settings.joinRadius;",
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
        "        if (meters < nearest) // NOLINT(readability-use-std-min-max)\n        {\n            nearest = meters;",
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
        "        presence.perception = seenActor_->perception;\n",
        "",
    ),
    Mutant(
        "one that perceives nobody any more goes on with its turn until its script says so, or its thinking runs out",
        MACHINE,
        "        const bool seen = looked && NothingToFightNow(*current);",
        "        const bool seen = looked && NothingToFightNow(*current) && false;",
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
        "an ally that perceives nobody has its turn skipped as unaware",
        RULES,
        "        return seen.side == Side::Enemy ? Rotation::Unaware : Rotation::OutOfCombat;",
        "        return Rotation::Unaware;",
    ),
    Mutant(
        "effects are never aged a round",
        MACHINE,
        "        e.b = static_cast<std::int32_t>(left);\n        fx.push_back(e);",
        "        e.b = static_cast<std::int32_t>(left);\n        (void)fx;",
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
    # A turn as a bar of time (dev/REBALANCE.md).
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
        "time: what a kept action owes is not paid as its turn begins",
        MACHINE,
        "        (void)budget_.spend.SpendAction(owed);\n        budget_.pendingPaidMs = owed;",
        "        budget_.pendingPaidMs = owed;",
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
        "        if (budget_.spend.TimeLeft() && !budget_.outOfMovement)",
        "        if (budget_.spend.CapacityLeftMs() > 4000000000U && !budget_.outOfMovement)",
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
    # A move that goes nowhere is its AI's pass (issue 59; OnCommandDone):
    # the rule, and each of the things it is not.
    Mutant(
        "nowhere: a move done where it began is let through again for as long as the turn may last",
        MACHINE,
        "    if (!itsMove || budget_.spend.Walked() > budget_.movementAtCommand)",
        "    if (!itsMove || budget_.spend.Walked() >= budget_.movementAtCommand)",
    ),
    Mutant(
        "nowhere: a move that walked is its AI's pass too",
        MACHINE,
        "    if (!itsMove || budget_.spend.Walked() > budget_.movementAtCommand)",
        "    if (!itsMove)",
    ),
    Mutant(
        "nowhere: the end of another command of its creature's passes the turn its move is in",
        MACHINE,
        "budget_.lastKind == CommandKind::Move && type == budget_.lastType && !budget_.approachWalk;",
        "budget_.lastKind == CommandKind::Move && !budget_.approachWalk;",
    ),
    Mutant(
        "nowhere: an attack or a weapon set done with nothing walked is its AI's pass",
        MACHINE,
        "budget_.lastKind == CommandKind::Move && type == budget_.lastType && !budget_.approachWalk;",
        "type == budget_.lastType && !budget_.approachWalk;",
    ),
    Mutant(
        "nowhere: the mod's own walk to a kept attack's target, done at once, is its AI's pass",
        MACHINE,
        "budget_.lastKind == CommandKind::Move && type == budget_.lastType && !budget_.approachWalk;",
        "budget_.lastKind == CommandKind::Move && type == budget_.lastType;",
    ),
    Mutant(
        "nowhere: the player's own move that goes nowhere is said to be a pass",
        MACHINE,
        "    if (PlayerControls(*current))\n    {\n        return;\n    }\n    const bool itsMove =",
        "    const bool itsMove =",
    ),
    Mutant(
        "nowhere: another creature's move done where it began passes the current one's turn",
        MACHINE,
        "current == nullptr || current->id != who)\n    {\n        return;\n    }\n    // An action whose time went",
        "current == nullptr || who == kNoObject)\n    {\n        return;\n    }\n    // An action whose time went",
    ),
    Mutant(
        "nowhere: a story's walk in a conversation over the fight passes the turn it stands over",
        MACHINE,
        "    if (!Active() || current == nullptr || current->id != who)\n    {\n        return;\n    }\n    // An action whose time went",
        "    if (current == nullptr || current->id != who)\n    {\n        return;\n    }\n    // An action whose time went",
    ),
    # A creature at a command its turn did not order, an action's time that
    # went to its walk, and the count of an AI's actions given back (issue 88;
    # tests/test_notordered.cpp).
    Mutant(
        "notordered: only an effect's command holds a creature: at an animation its script gave it, its turn stands out the action's limit",
        MACHINE,
        "    return actor.busy && Classify(actor.current) == CommandKind::Involuntary;",
        "    return actor.busy && actor.current == CommandType::ExecuteEffect;",
    ),
    Mutant(
        "notordered: an action's time that went to its walk is taken as any command of its creature's is done",
        MACHINE,
        "    if (Classify(type) == CommandKind::Action)\n    {\n        ChargeOnArrival(type);",
        "    if (Classify(type) != CommandKind::Involuntary)\n    {\n        ChargeOnArrival(type);",
    ),
    Mutant(
        "notordered: an action's time that went to its walk is taken at the first frame its creature stands, and goes to the walk again with its next step",
        MACHINE,
        "    if (AtItsOwnMode(actor))\n    {\n        ownEffectSince_.reset();\n        stoodAt_.reset();",
        "    if (budget_.onArrivalMs > 0 && actor.busy && !actor.moving && Classify(actor.current) == CommandKind::Action)\n    {\n        ChargeOnArrival(actor.current);\n    }\n    if (AtItsOwnMode(actor))\n    {\n        ownEffectSince_.reset();\n        stoodAt_.reset();",
    ),
    Mutant(
        "notordered: on a turn the player plays, an action's time that went to its walk is never taken",
        MACHINE,
        "    if (Classify(type) == CommandKind::Action)\n    {\n        ChargeOnArrival(type);",
        "    if (Classify(type) == CommandKind::Action && !PlayerControls(*current))\n    {\n        ChargeOnArrival(type);",
    ),
    Mutant(
        "notordered: an AI two of whose actions were given back is let a third, its script ordering it in the failure's own event",
        MACHINE,
        "    const bool nothingToGetTo = !player && budget_.givenBack >= kGivenBackEnough;",
        "    const bool nothingToGetTo = false;",
    ),
    Mutant(
        "notordered: the player's own third order after two given back is refused, the turn taken for over",
        MACHINE,
        "    const bool nothingToGetTo = !player && budget_.givenBack >= kGivenBackEnough;",
        "    const bool nothingToGetTo = budget_.givenBack >= kGivenBackEnough;",
    ),
    Mutant(
        "notordered: an AI one of whose actions was given back is refused its second try",
        MACHINE,
        "    const bool nothingToGetTo = !player && budget_.givenBack >= kGivenBackEnough;",
        "    const bool nothingToGetTo = !player && budget_.givenBack >= kGivenBackEnough - 1;",
    ),
    Mutant(
        "time: a failed action's time is not given back",
        MACHINE,
        "    return budget_.lastCostMs > 0;",
        "    return budget_.lastCostMs > 4000000000U;",
    ),
    Mutant(
        "time: a turn with its bar spent waits on its free meter",
        MACHINE,
        "    return !budget_.spend.TimeLeft();",
        "    return !budget_.spend.TimeLeft() && (budget_.outOfMovement || budget_.MovementLeft() < kMinStep);",
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
        "    if (charge->owedMs <= bar.CapacityLeftMs())",
        "    if (true)",
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
        "    return start + (loop * static_cast<float>(loops));",
        "    return start + loop;",
    ),
    Mutant(
        "rules: Survival adds nothing to initiative",
        RULES,
        "    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus) + static_cast<int>(ranks);",
        "    return rng.Roll(settings.initiativeDie) + static_cast<int>(bonus) + static_cast<int>(ranks) * 0;",
    ),
    Mutant(
        "time: a kept attack's creature does not walk to its target",
        MACHINE,
        "        const bool approach = walk >= kMinStep;",
        "        const bool approach = walk >= 9999.0f;",
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
        '    if (why == "held" || why == "struck" || why == kConjureCleared)',
        '    if (why == "held" || why == "struck" || why == kConjureCleared || why == "the fight over")',
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
        "held.kind == Condition::Disabled && held.owedMs >= turn;",
        "held.kind == Condition::Disabled && held.owedMs > turn;",
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
        "    const bool fresh = player || lastStatusNow_ == lastNow_;",
        "    const bool fresh = player || lastStatusNow_ <= lastNow_;",
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
        "    if (budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 && budget_.onArrivalMs == 0)",
        "    if (budget_.lastKind == CommandKind::Action && budget_.lastCostMs > 0 && budget_.onArrivalMs == 99)",
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
        "rules: a boss is interrupted as a critter is",
        RULES,
        "    return party ? (10.0f * ranks) + 5.0f + (levels / 2.0f) : (10.0f * ranks) + levels;",
        "    return party ? (10.0f * ranks) + 5.0f + (levels / 2.0f) : (0.0f * ranks) + 10.0f + levels;",
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
        "        budget_.rolledMs = last->second.rolledMs;",
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
        "        Field(out, std::wstring_view{card.icon});",
        "        Field(out, std::wstring_view{});",
    ),
    Mutant(
        "bar: a card's string leaves out the pending ability's name",
        BAR,
        "        Field(out, std::wstring_view{card.pendingName});",
        "        Field(out, std::wstring_view{});",
    ),
    Mutant(
        "walkcut: a turn the meters stopped a walk in leaves no word of it for its card",
        MACHINE,
        "            lastTurn_[current->id] = {rolled, budget_.outOfMovement};",
        "            lastTurn_[current->id] = {rolled, false};",
    ),
    Mutant(
        "walkcut: a walk cut short stays on its card through its creature's next turn",
        MACHINE,
        "        last->second.walkCut = false;\n",
        "",
    ),
    Mutant(
        "walkcut: a walk cut short stays on its card past a turn its creature lost",
        MACHINE,
        "            lastTurn_.erase(current->id);\n",
        "",
    ),
    Mutant(
        "walkcut: a fallen creature's card says its walk was cut short",
        MACHINE,
        "        const bool walkCut = !c.down && last != lastTurn_.end() && last->second.walkCut;",
        "        const bool walkCut = last != lastTurn_.end() && last->second.walkCut;",
    ),
    Mutant(
        "bar: a card's string leaves out that its walk was cut short",
        BAR,
        "        Field(out, card.walkCut);",
        "        Field(out, false);",
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
        "    if (current == nullptr || !settings_.giveBackUnplayed || budget_.lastKind != CommandKind::Action ||",
        "    if (current == nullptr || budget_.lastKind != CommandKind::Action ||",
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
    # Issue 11: an attack that stands still is judged by whether the engine
    # has its command still at its approach, as the game layer reads it each
    # frame, and by how long it has stood at that command alone; and a kept
    # action is not given to a creature an effect holds. (An attack's reach
    # and walk are the engine's own, asked in src/game: no rule of the core's
    # to break.)
    Mutant(
        "a kept action is given to a creature an effect holds as its turn begins",
        MACHINE,
        ': HeldBy(seenActor_->current, "a")));\n        return false;\n    }\n    landing_ = *charge;',
        ': HeldBy(seenActor_->current, "a")));\n    }\n    landing_ = *charge;',
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
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack())",
        "    if (Classify(type) != CommandKind::Action || !ActionToGiveBack() || budget_.refunded)",
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
        "an attack standing short of its target is stopped at once",
        MACHINE,
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs : kStoodMs);",
        "        const std::uint32_t limit = Patience(short_ ? kStuckMs / 4 : kStoodMs);",
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
        "own clocks: a creature's effects do not move as its bar is spent",
        MACHINE,
        "    if (clockMs == 0 || !OwnClocks() || !timeHeld_ || ownEffectSince_)\n    {\n        return;\n    }\n    Effect age = Effect::Of(EffectKind::AgeTime, who);",
        "    if (clockMs < 100000 || !OwnClocks() || !timeHeld_ || ownEffectSince_)\n    {\n        return;\n    }\n    Effect age = Effect::Of(EffectKind::AgeTime, who);",
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
        "    if (!OwnClocks() || !timeHeld_)\n    {\n        return;\n    }\n    Effect clock",
        "    if (OwnClocks() || !timeHeld_)\n    {\n        return;\n    }\n    Effect clock",
    ),
    Mutant(
        "own clocks: a creature that joins is not put on one",
        MACHINE,
        "            SetOwnClock(joined, true, fx);\n",
        "",
    ),
    Mutant(
        "own clocks: the setting is not asked",
        MACHINE,
        "    return settings_.ownClocks;",
        "    return true;",
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
        "           !request.sustained && request.conjureAnim > 0 && request.conjureSeconds > 0.0f;",
        "           !request.sustained && request.conjureAnim >= 0 && request.conjureSeconds > 0.0f;",
    ),
    Mutant(
        "the engine's conjuring: the setting is not asked",
        MACHINE,
        "    return settings_.engineConjure && request.type == CommandType::UseAbility &&",
        "    return request.type == CommandType::UseAbility &&",
    ),
    Mutant(
        "the engine's conjuring: the mod's own rule still interrupts",
        MACHINE,
        "    if (settings_.engineConjure)\n    {\n        return false;\n    }",
        "    if (settings_.engineConjure && false)\n    {\n        return false;\n    }",
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
        "grabbed != grabbedMs_.end() && grabbed->second >= TurnMs())",
        "grabbed != grabbedMs_.end() && grabbed->second >= TurnMs() * 100)",
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
    # The turn camera's distance by the model's box (issue 33; CloseDistanceOf):
    # which of its sides against which of a person's, between which two
    # distances, which measure is said to have decided, and what is left for
    # a model with no box.
    Mutant(
        "the turn camera stands back by a creature's height alone: a giant spider is smaller than a person",
        PORTRAITS,
        "        const float size = std::max(length, height);",
        "        const float size = height;",
    ),
    Mutant(
        "the turn camera stands back by a creature's length alone, however tall it is",
        PORTRAITS,
        "        const float size = std::max(length, height);",
        "        const float size = length;",
    ),
    Mutant(
        "a body's length is its box's first side across, whichever way the model lies in it",
        PORTRAITS,
        "        const float length = std::max(box->x, box->y) / kPersonAcross;",
        "        const float length = box->x / kPersonAcross;",
    ),
    Mutant(
        "a body's length is its box's second side across, whichever way the model lies in it",
        PORTRAITS,
        "        const float length = std::max(box->x, box->y) / kPersonAcross;",
        "        const float length = box->y / kPersonAcross;",
    ),
    Mutant(
        "a box stands the turn camera past the furthest it goes: the high dragon's wings, 78 m",
        PORTRAITS,
        "std::min(kCloseMost, kCloseGame * size)",
        "kCloseGame * size",
    ),
    Mutant(
        "a box smaller than a person brings the turn camera in closer than the game's own",
        PORTRAITS,
        "            if (size > 1.0f)",
        "            if (size > 0.0f)",
    ),
    Mutant(
        "a box with no size is taken for a creature's measure, and its head is not asked",
        PORTRAITS,
        "        if (size > 0.0f)\n        {\n            const CloseBy by",
        "        if (true)\n        {\n            const CloseBy by",
    ),
    Mutant(
        "which of a box's two measures decided the turn camera's distance is said the wrong way round",
        PORTRAITS,
        "length > height ? CloseBy::Length : CloseBy::Height",
        "length > height ? CloseBy::Height : CloseBy::Length",
    ),
    Mutant(
        "a creature with a head node goes by its feet to its head, whatever its box: the giant spider's turn again",
        PORTRAITS,
        "    if (box && std::isfinite(box->x) && std::isfinite(box->y) && std::isfinite(box->z))",
        "    if (box && !feetToHead && std::isfinite(box->x) && std::isfinite(box->y) && std::isfinite(box->z))",
    ),
    Mutant(
        "a model that gives no box has the game's own distance, whatever its feet to its head",
        PORTRAITS,
        "        return CloseDistance{CloseDistanceFor(*feetToHead), CloseBy::Head};",
        "        return CloseDistance{std::nullopt, CloseBy::Head};",
    ),
    Mutant(
        "a distance by the feet to the head is said to be by the box's height",
        PORTRAITS,
        "        return CloseDistance{CloseDistanceFor(*feetToHead), CloseBy::Head};",
        "        return CloseDistance{CloseDistanceFor(*feetToHead), CloseBy::Height};",
    ),
    # Where the turn camera stands about an enemy (issue 33; ChooseStand,
    # StandDistance, IsPlace, OrbitDistance, ClearDistance, ViewYaw, YawToward):
    # of a row's three places the first with ground whose line is clear the
    # whole distance, else the one with ground clear the furthest and a margin
    # short of it, never nearer than the game's own, all at the player's zoom;
    # the three in front only with no place behind; with none at all, with
    # the party, looking at it.
    Mutant(
        "a place clear the whole way is taken with no ground under it: the frame from inside the Broodmother's rock",
        PORTRAITS,
        "        if (place.ground && place.clear >= whole)",
        "        if (place.clear >= whole)",
    ),
    Mutant(
        "the camera's left is liked before its right",
        PORTRAITS,
        "    const std::array<Place, 3> places{Place{sides[0], ClearOf(row.right.clear), row.right.ground},\n                                      Place{sides[1], ClearOf(row.left.clear), row.left.ground},",
        "    const std::array<Place, 3> places{Place{sides[1], ClearOf(row.left.clear), row.left.ground},\n                                      Place{sides[0], ClearOf(row.right.clear), row.right.ground},",
    ),
    Mutant(
        "with a side clear the whole distance the camera stands at the game's own all the same",
        PORTRAITS,
        "            return ViewStand{ViewAnswer::Stand, place.side, whole, asked};",
        "            return ViewStand{ViewAnswer::Stand, place.side, whole, std::nullopt};",
    ),
    Mutant(
        "of two lines clear alike the later side is taken",
        PORTRAITS,
        "        if (place.ground && (best == nullptr || place.clear > best->clear))",
        "        if (place.ground && (best == nullptr || place.clear >= best->clear))",
    ),
    Mutant(
        "the place clear the furthest is taken with no ground under it",
        PORTRAITS,
        "        if (place.ground && (best == nullptr || place.clear > best->clear))",
        "        if (best == nullptr || place.clear > best->clear)",
    ),
    Mutant(
        "the camera stands where its line is cut, in the wall's face: no margin",
        PORTRAITS,
        "    return std::max(least, sane - kViewMargin); // never nearer than the game's own",
        "    return std::max(least, sane); // never nearer than the game's own",
    ),
    Mutant(
        "a wall close by brings the camera in nearer than the game's own",
        PORTRAITS,
        "    return std::max(least, sane - kViewMargin); // never nearer than the game's own",
        "    return std::min(least, sane - kViewMargin); // never nearer than the game's own",
    ),
    Mutant(
        "a place clear the whole way has the camera a margin short of it: the ground is asked where it does not stand",
        PORTRAITS,
        "    if (sane >= wanted)\n    {\n        return wanted;",
        "    if (sane >= wanted + 100.0f)\n    {\n        return wanted;",
    ),
    Mutant(
        "a stand at the game's own distance is given a close distance of its own",
        PORTRAITS,
        "    if (!(meters > OrbitDistance(zoom, kCloseGame)))",
        "    if (!(meters > 0.0f))",
    ),
    Mutant(
        "the close distance for where the camera stands forgets the player's zoom",
        PORTRAITS,
        "    return ViewStand{ViewAnswer::Stand, best->side, meters, (meters - ((1.0f - zoom) * kFarGame)) / zoom};",
        "    return ViewStand{ViewAnswer::Stand, best->side, meters, meters};",
    ),
    Mutant(
        "a clear distance that is not a number is taken for one",
        PORTRAITS,
        "    return std::isfinite(meters) && meters > 0.0f ? meters : 0.0f;",
        "    return meters;",
    ),
    Mutant(
        "with no place behind a creature the camera gives up, the front not asked",
        PORTRAITS,
        "        return ViewStand{ViewAnswer::AskFront, ViewSide::Behind, 0.0f, std::nullopt};",
        "        return ViewStand{ViewAnswer::WithParty, ViewSide::Behind, 0.0f, std::nullopt};",
    ),
    Mutant(
        "with no place in front either the front is asked for again, and the camera never stays with the party",
        PORTRAITS,
        "    return ViewStand{ViewAnswer::WithParty, ViewSide::Behind, 0.0f, std::nullopt};",
        "    return ViewStand{ViewAnswer::AskFront, ViewSide::Behind, 0.0f, std::nullopt};",
    ),
    Mutant(
        "a place in front of a creature is said to be behind it",
        PORTRAITS,
        "            ChooseOf(zoom, asked, *front, {ViewSide::FrontRight, ViewSide::FrontLeft, ViewSide::Front}))",
        "            ChooseOf(zoom, asked, *front, {ViewSide::Right, ViewSide::Left, ViewSide::Behind}))",
    ),
    Mutant(
        "a place over no ground is a place to stand",
        PORTRAITS,
        "    return under.ground && under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;",
        "    return under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;",
    ),
    Mutant(
        "a place over ground nobody could walk on is a place to stand",
        PORTRAITS,
        "    return under.ground && under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;",
        "    return under.ground && std::isfinite(under.below) && under.below <= kPlaceDrop;",
    ),
    Mutant(
        "a place over the floor of somewhere far below is a place to stand",
        PORTRAITS,
        "    return under.ground && under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;",
        "    return under.ground && under.walkable && std::isfinite(under.below);",
    ),
    Mutant(
        "ground above a creature's feet is no place to stand",
        PORTRAITS,
        "    return under.ground && under.walkable && std::isfinite(under.below) && under.below <= kPlaceDrop;",
        "    return under.ground && under.walkable && std::isfinite(under.below) && std::abs(under.below) <= 1.0f;",
    ),
    Mutant(
        "in front of a creature to its right is to its left",
        PORTRAITS,
        "        return behind + kHalfTurn - kViewOffset;",
        "        return behind + kHalfTurn + kViewOffset;",
    ),
    Mutant(
        "in front of a creature is behind it",
        PORTRAITS,
        "    case ViewSide::Front:\n        return behind + kHalfTurn;",
        "    case ViewSide::Front:\n        return behind;",
    ),
    Mutant(
        "a place's words in the log are another's: in front is said as behind",
        PORTRAITS,
        '        return "in front of it to its right";',
        '        return "behind it to its right";',
    ),
    Mutant(
        "staying with the party the camera looks away from the creature",
        PORTRAITS,
        "    const float x = from.x - at.x;\n    const float y = from.y - at.y;",
        "    const float x = at.x - from.x;\n    const float y = at.y - from.y;",
    ),
    Mutant(
        "with the creature straight over the player's own the camera is turned all the same",
        PORTRAITS,
        "    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0.0f && y == 0.0f))",
        "    if (!std::isfinite(x) || !std::isfinite(y))",
    ),
    Mutant(
        "the camera's distance forgets the far one: zoomed out, its lines are asked short of where it stands",
        PORTRAITS,
        "    return (zoom * close) + ((1.0f - zoom) * kFarGame);",
        "    return zoom * close;",
    ),
    Mutant(
        "a line that is cut is taken for clear the whole way",
        PORTRAITS,
        "    return wanted * std::min(1.0f, clear / whole);",
        "    return wanted;",
    ),
    Mutant(
        "a line's clear length is taken for the camera's distance, the head's height forgotten",
        PORTRAITS,
        "    return wanted * std::min(1.0f, clear / whole);",
        "    return std::min(wanted, clear);",
    ),
    Mutant(
        "a line cut past its end is clear further than the camera stands",
        PORTRAITS,
        "    return wanted * std::min(1.0f, clear / whole);",
        "    return wanted * clear / whole;",
    ),
    Mutant(
        "a line with no length is clear the whole way",
        PORTRAITS,
        "    if (!std::isfinite(whole) || !std::isfinite(clear) || !(whole > 0.0f))",
        "    if (!std::isfinite(whole) || !std::isfinite(clear) || !(whole >= 0.0f))",
    ),
    Mutant(
        "a line cut at a point that is not a number is clear the whole way",
        PORTRAITS,
        "    if (!std::isfinite(whole) || !std::isfinite(clear) || !(whole > 0.0f))",
        "    if (!(whole > 0.0f))",
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
        "            grabbedMs_.erase(c->id);\n",
        "",
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
        "        if (side != Side::Party)\n        {\n            owed.push_back(id);",
        "        if (side != Side::Party && id == kNoObject)\n        {\n            owed.push_back(id);",
    ),
    Mutant(
        "those that left the fight, and those the gate turned away outside it, are not told as it ends",
        MACHINE,
        "    LetGo(fx);\n    order_.Clear();",
        "    order_.Clear();",
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
        "a kept attack given to its creature is taken for the game's sending it something",
        MACHINE,
        "        !(itsTurn && (landing || approaching)))",
        "        !(itsTurn && approaching))",
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
        "    if (seen.side == Side::Party)\n    {\n        return Rotation::Fights;",
        "    if (seen.side == Side::Party && seen.perception.inCombat)\n    {\n        return Rotation::Fights;",
    ),
    Mutant(
        "one the game has in combat that perceives nobody has nothing to fight: no turn, and a hostile's skipped as unaware",
        RULES,
        "    if (perception.inCombat || perception.ofFoes)",
        "    if (perception.ofFoes)",
    ),
    Mutant(
        "one out of combat that perceives someone hostile to it has nothing to fight: no turn, and a hostile's skipped as unaware",
        RULES,
        "    if (perception.inCombat || perception.ofFoes)",
        "    if (perception.inCombat)",
    ),
    Mutant(
        "an enemy out of combat that perceives a party member has nothing to fight, and no turn",
        RULES,
        "    return side != Side::Enemy || !perception.ofParty;",
        "    return side != Side::Enemy || side == Side::Enemy;",
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
        "a creature the game has made inactive is in the fight, and told its turn has come like anyone",
        RULES,
        "    if (seen.inactive)\n    {\n        return Rotation::OutOfTheWorld;",
        "    if (seen.inactive && seen.down)\n    {\n        return Rotation::OutOfTheWorld;",
    ),
    Mutant(
        "a party member the game has made inactive is waited on for an order",
        MACHINE,
        "        presence.inactive = seenActor_->inactive;",
        "        presence.inactive = seenActor_->inactive && who.side != Side::Party;",
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
        "one that leaves the fight stays on a clock of its own, which nothing moves: what the game has waiting on it never comes",
        MACHINE,
        "    lastTurn_.erase(who.id);\n    SetOwnClock(who.id, false, fx);",
        "    lastTurn_.erase(who.id);\n    SetOwnClock(who.id, true, fx);",
    ),
    Mutant(
        "a turn ended as inactive rolls its time over, as if its actor had saved it",
        TURNEND,
        '    TurnEnd{"inactive", false},',
        '    TurnEnd{"inactive", true},',
    ),
    Mutant(
        "the fight's own party is not named to the scan: who the game has put out of it is never read again",
        MACHINE,
        "            if (c.side == Side::Party || !c.down)",
        "            if (c.side != Side::Party && !c.down)",
    ),
    Mutant(
        "what the scan read of who is in the world does not reach the rule",
        MACHINE,
        "    presence.inactive = seen.inactive;",
        "    presence.inactive = false;",
    ),
    Mutant(
        "one that left the fight out of the world, or out of the party, is said to leave with every scan",
        MACHINE,
        "            if (known != nullptr && TurnOrder::Shown(*known) && gone)",
        "            if (known != nullptr && gone)",
    ),
    Mutant(
        "the party fallen, its enemies leave the fight for being found by nobody: its end is said as theirs",
        MACHINE,
        "std::ranges::any_of(scan, [](const Combatant &c) { return c.side == Side::Party && !c.down; });",
        "std::ranges::any_of(scan, [](const Combatant &c) { return c.side == Side::Party; });",
    ),
    Mutant(
        "one put out of the party keeps the party's side in the order after it has left: a fallen card of the party's",
        MACHINE,
        "                known->side = seen.side;\n",
        "",
    ),
    Mutant(
        "one of the party taken out of the world keeps a card, greyed as the fallen's",
        MACHINE,
        "                known->inactive = seen.inactive;\n",
        "",
    ),
    Mutant(
        "one out of the world is shown on the bar while it is down in the order, a party member for good",
        ORDER,
        "    return !c.inactive && (!c.down || c.side == Side::Party);",
        "    return !c.down || c.side == Side::Party;",
    ),
    Mutant(
        "one that left the fight in the middle of its own turn plays the turn out",
        MACHINE,
        "current != nullptr && InTurn() && current->down && !ActorGone(obs))",
        "current != nullptr && InTurn() && current->down && !ActorGone(obs) && false)",
    ),
    Mutant(
        "the end of a turn whose creature has left the fight moves its own clock, which is the shared one's again",
        MACHINE,
        "            rest > 0 && OwnClocks() && timeHeld_ && !current->down)",
        "            rest > 0 && OwnClocks() && timeHeld_)",
    ),
    Mutant(
        "one back in the fight in the round it left in stays on the shared clock",
        MACHINE,
        "                SetOwnClock(seen.id, true, fx);\n",
        "",
    ),
    Mutant(
        "what its last turn saved rolls over into the first turn of one that left the fight and came back",
        MACHINE,
        "    lastTurn_.erase(who.id);\n",
        "",
    ),
    Mutant(
        "the party's own that left the fight out of the world are told to choose as it ends",
        MACHINE,
        "        if (side != Side::Party)\n        {\n            owed.push_back(id);",
        "        if (side != Side::Party || id != kNoObject)\n        {\n            owed.push_back(id);",
    ),
    Mutant(
        "the line for one out of the world says the game put it out of the party",
        RULES,
        '        return "the game has taken it out of the world";',
        '        return "the game has put it out of the party";',
    ),
    Mutant(
        "a change of side by the game's word is said of one whose side did not change",
        MACHINE,
        '            if (known->side != seen.side)\n            {\n                Log(LogLevel::Info, std::format("{} is {} now',
        '            if (known->side == seen.side || known->side != seen.side)\n            {\n                Log(LogLevel::Info, std::format("{} is {} now',
    ),
    Mutant(
        "one of the party whose object is gone from the world keeps a card, greyed as the fallen's",
        MACHINE,
        "                c->inactive = true;\n",
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
    # Issue 46: whose turns their tactics play, chosen a companion; a choice
    # applies from its next turn; never the hero; the setting is what a
    # companion starts as; and the file a save keeps the choices in.
    Mutant(
        "a choice made in a companion's own turn changes who plays the turn under way",
        MACHINE,
        "    const bool underWay = budget_.begun && budget_.begun->who == c.id;",
        "    const bool underWay = false;",
    ),
    Mutant(
        "the turn under way is everyone's: the others' cards go by what was chosen for its creature",
        MACHINE,
        "    const bool underWay = budget_.begun && budget_.begun->who == c.id;",
        "    const bool underWay = budget_.begun.has_value();",
    ),
    Mutant(
        "a turn is played by the setting alone, whatever was chosen for its creature",
        MACHINE,
        "    budget_.begun = Budget::Begun{who.id, partyTactics_.Chosen(who.tag)};",
        "    budget_.begun = Budget::Begun{who.id, std::nullopt};",
    ),
    Mutant(
        "a companion nothing was chosen for does not go by the setting",
        PARTYTACTICS,
        "    return !hero && chosen.value_or(start == PartyControl::Tactics);",
        "    return !hero && chosen.value_or(start != PartyControl::Tactics && start != PartyControl::Manual);",
    ),
    Mutant(
        "a choice of off does not win over party = tactics",
        PARTYTACTICS,
        "    return !hero && chosen.value_or(start == PartyControl::Tactics);",
        "    return !hero && (chosen.value_or(false) || start == PartyControl::Tactics);",
    ),
    Mutant(
        "a choice of on does not win over party = manual",
        PARTYTACTICS,
        "    return !hero && chosen.value_or(start == PartyControl::Tactics);",
        "    return !hero && chosen.value_or(true) && start == PartyControl::Tactics;",
    ),
    Mutant(
        "the hero's tactics play its turns where a choice is kept for its tag",
        PARTYTACTICS,
        "    return !hero && chosen.value_or(start == PartyControl::Tactics);",
        "    return chosen.value_or(!hero && start == PartyControl::Tactics);",
    ),
    Mutant(
        "under party = tactics the hero's tactics play its turns",
        PARTYTACTICS,
        "    return !hero && chosen.value_or(start == PartyControl::Tactics);",
        "    return chosen ? !hero && *chosen : start == PartyControl::Tactics;",
    ),
    Mutant(
        "a choice is kept for the hero",
        MACHINE,
        "    const bool changed = !who.hero && partyTactics_.Choose(who.tag, on);",
        "    const bool changed = partyTactics_.Choose(who.tag, on);",
    ),
    Mutant(
        "what a save holds for the hero's tag is answered as chosen for it",
        MACHINE,
        "    const std::optional<bool> chosen = who.hero ? std::nullopt : partyTactics_.Chosen(who.tag);",
        "    const std::optional<bool> chosen = partyTactics_.Chosen(who.tag);",
    ),
    Mutant(
        "a choice is said to be made in its creature's own turn while another's is under way",
        MACHINE,
        "        current != nullptr && current->id == who.id && budget_.begun && budget_.begun->who == who.id)",
        "        current != nullptr && budget_.begun)",
    ),
    Mutant(
        "a choice made in a turn under way is never said to be",
        MACHINE,
        "        current != nullptr && current->id == who.id && budget_.begun && budget_.begun->who == who.id)",
        "        current != nullptr && current->id == who.id && budget_.begun && budget_.begun->who == who.id && false)",
    ),
    Mutant(
        "the turn under way is said to be its tactics' when it is the player's",
        MACHINE,
        "        answer.turnIsTactics = !PlayerControls(*current);",
        "        answer.turnIsTactics = answer.on;",
    ),
    Mutant(
        "a choice that changed nothing is said in the log all the same",
        MACHINE,
        "    answer.changed = changed;\n    if (changed)",
        "    answer.changed = changed;\n    if (changed || !who.hero)",
    ),
    Mutant(
        "a choice is not said to have changed anything",
        MACHINE,
        "    answer.changed = changed;\n    if (changed)",
        "    answer.changed = false;\n    if (changed)",
    ),
    Mutant(
        "a choice taken back is said in the log as one made",
        MACHINE,
        '                        on ? "tactics play its turns" : "turns wait for the player", by,',
        '                        "tactics play its turns", by,',
    ),
    Mutant(
        "a save's choices are added to the session's, not put in their place",
        MACHINE,
        "    partyTactics_ = std::move(choices);",
        "    for (const auto &[tag, on] : choices.All())\n    {\n        (void)partyTactics_.Choose(tag, on);\n    }",
    ),
    Mutant(
        "a card's word of whose tactics play its next turn is of the turn under way",
        MACHINE,
        "    return c.side == Side::Party && tb::TacticsPlay(partyTactics_.Chosen(c.tag), c.hero, settings_.partyControl);",
        "    return c.side == Side::Party && !PlayerControls(c);",
    ),
    Mutant(
        "an enemy's card says its tactics play its turns",
        MACHINE,
        "    return c.side == Side::Party && tb::TacticsPlay(partyTactics_.Chosen(c.tag), c.hero, settings_.partyControl);",
        "    return tb::TacticsPlay(partyTactics_.Chosen(c.tag), c.hero, PartyControl::Tactics);",
    ),
    Mutant(
        "the cards do not say whose tactics play its turns",
        MACHINE,
        "                                 pending, std::wstring{}, std::wstring{}, OnTactics(c), walkCut});",
        "                                 pending, std::wstring{}, std::wstring{}, false, walkCut});",
    ),
    Mutant(
        "the state says auto of every card, an enemy's too",
        "src/core/State.cpp",
        '        if (card.side == Side::Party)\n        {\n            out += std::format(R"(,"auto":{})", Bool(card.tacticsPlay));',
        '        if (card.side != Side::Ally)\n        {\n            out += std::format(R"(,"auto":{})", Bool(card.tacticsPlay));',
    ),
    Mutant(
        "the state's auto is the card's yours",
        "src/core/State.cpp",
        '            out += std::format(R"(,"auto":{})", Bool(card.tacticsPlay));',
        '            out += std::format(R"(,"auto":{})", Bool(!card.yours));',
    ),
    Mutant(
        "a first choice of what nothing chosen meant is no change",
        PARTYTACTICS,
        "        chosen_.emplace(found, std::wstring{tag}, on);\n        return true;",
        "        chosen_.emplace(found, std::wstring{tag}, on);\n        return on;",
    ),
    Mutant(
        "the choices are kept in the order they were made, and so written and said",
        PARTYTACTICS,
        "        chosen_.emplace(found, std::wstring{tag}, on);\n        return true;",
        "        chosen_.emplace(chosen_.end(), std::wstring{tag}, on);\n        return true;",
    ),
    Mutant(
        "a tag that begins another's is taken for it",
        PARTYTACTICS,
        "    if (found == chosen_.end() || found->first != tag)\n    {\n        return std::nullopt;",
        "    if (found == chosen_.end() || !found->first.starts_with(tag))\n    {\n        return std::nullopt;",
    ),
    Mutant(
        "the same choice again is a change",
        PARTYTACTICS,
        "    return std::exchange(found->second, on) != on;",
        "    return std::exchange(found->second, on) == on;",
    ),
    Mutant(
        "a choice is kept for a creature with no tag",
        PARTYTACTICS,
        "    return !tag.empty() && std::ranges::none_of(tag, [](wchar_t c) { return c < 0x20 || c == 0x7F; });",
        "    return std::ranges::none_of(tag, [](wchar_t c) { return c < 0x20 || c == 0x7F; });",
    ),
    Mutant(
        "a tag with a line's end in it is kept, and written as two lines",
        PARTYTACTICS,
        "    return !tag.empty() && std::ranges::none_of(tag, [](wchar_t c) { return c < 0x20 || c == 0x7F; });",
        "    return !tag.empty() && std::ranges::none_of(tag, [](wchar_t c) { return c == 0x7F; });",
    ),
    Mutant(
        "the log's word of the choices leaves out whether each is on or off",
        PARTYTACTICS,
        '        whom += std::format("{}{} {}", whom.empty() ? "" : ", ", Utf8(tag), on ? "on" : "off");',
        '        whom += std::format("{}{} {}", whom.empty() ? "" : ", ", Utf8(tag), "on");',
    ),
    Mutant(
        "a choice of off is written into the save as on",
        PARTYTACTICS,
        "        out += on ? kOn : kOff;",
        "        out += kOn;",
    ),
    Mutant(
        "a file of another version is read as this build's",
        PARTYTACTICS,
        "    if (*version != static_cast<std::uint64_t>(kPartyTacticsVersion))",
        "    if (*version == 0)",
    ),
    Mutant(
        "a file that is not the mod's is read as one, by its second word",
        PARTYTACTICS,
        "    if (!head.starts_with(kFileName) || head.size() <= kFileName.size() || head[kFileName.size()] != ' ')",
        "    if (head.size() <= kFileName.size() || head[kFileName.size()] != ' ')",
    ),
    Mutant(
        "a file cut short before its end is read as whole",
        PARTYTACTICS,
        "    if (!ended)\n    {",
        "    if (!ended && false)\n    {",
    ),
    Mutant(
        "what comes after a file's end is read with the rest",
        PARTYTACTICS,
        "        if (ended)\n        {",
        "        if (ended && false)\n        {",
    ),
    Mutant(
        "a line that is no choice is passed over, and the rest of a damaged file believed",
        PARTYTACTICS,
        '            return damaged(\n                std::format(R"(line {} is not a choice ("{}<tag>" or "{}<tag>"): damaged)", number, kOn, kOff));',
        "            continue;",
    ),
    Mutant(
        "a choice read from a save for off is taken as on",
        PARTYTACTICS,
        "        (void)read.choices.Choose(*tag, on);",
        "        (void)read.choices.Choose(*tag, true);",
    ),
    Mutant(
        "a follower chosen for twice in a file has the second line's word",
        PARTYTACTICS,
        "        if (read.choices.Chosen(*tag))\n        {",
        "        if (read.choices.Chosen(*tag) && false)\n        {",
    ),
    Mutant(
        "a line with no tag, or a tag that is no text, is passed over",
        PARTYTACTICS,
        '            return damaged(std::format("line {} names no follower\'s tag: damaged", number));',
        "            continue;",
    ),
    Mutant(
        "the tool's on is false for anything but true, with no word of it",
        PARTYTACTICS,
        '        if (*on != "true" && *on != "false")',
        '        if (*on != "true" && *on != "false" && false)',
    ),
    Mutant(
        "the tool takes a name where an id was given and is wrong",
        PARTYTACTICS,
        "        if (!ask.id || *ask.id == kNoObject)\n        {\n            return wrong(",
        "        if ((!ask.id || *ask.id == kNoObject) && !name)\n        {\n            return wrong(",
    ),
    Mutant(
        "the tool takes half an id for the whole of it",
        PARTYTACTICS,
        "    if (text.empty() || got.ec != std::errc{} || got.ptr != text.data() + text.size())",
        "    if (text.empty() || got.ec != std::errc{})",
    ),
    Mutant(
        "the tool finds a companion by its name only in the letters' own case",
        PARTYTACTICS,
        "    return std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });",
        "    return std::ranges::equal(a, b);",
    ),
    Mutant(
        "of two of the party with one name the tool takes the first",
        PARTYTACTICS,
        "        if (named.size() > 1)",
        "        if (named.size() > 2)",
    ),
    Mutant(
        "the tool chooses for the hero",
        PARTYTACTICS,
        "    if (who->hero && ask.on)",
        "    if (who->hero && ask.on && false)",
    ),
    Mutant(
        "the tool does not answer about the hero either",
        PARTYTACTICS,
        "    if (who->hero && ask.on)",
        "    if (who->hero)",
    ),
    Mutant(
        "the tool chooses for one with no tag, and nothing is kept",
        PARTYTACTICS,
        "    if (ask.on && !Keepable(who->tag))",
        "    if (ask.on && !Keepable(who->tag) && false)",
    ),
    Mutant(
        "the tool's answer says a choice was the setting's, and the setting's a choice",
        PARTYTACTICS,
        '                       answer.chosen ? "chosen" : "setting", flag(answer.changed));',
        '                       answer.chosen ? "setting" : "chosen", flag(answer.changed));',
    ),
    Mutant(
        "the tool's answer says a turn its tactics play out is yours",
        PARTYTACTICS,
        '    const char *underWay = answer.turnIsTactics ? "AI" : "yours";',
        '    const char *underWay = "yours";',
    ),
    # The tactics page's box, "Use when turn-based" (issue 46's second step):
    # what it shows of whom, what its movie asks and says.
    Mutant(
        "the hero's tactics page has the box",
        PARTYTACTICS,
        "    if (who.hero || !Keepable(who.tag))",
        "    if (!Keepable(who.tag))",
    ),
    Mutant(
        "one whose tag no choice could be kept by has a box all the same",
        PARTYTACTICS,
        "    if (who.hero || !Keepable(who.tag))",
        "    if (who.hero)",
    ),
    Mutant(
        "a box shows whether anything was chosen for its companion, not what its turns go by",
        PARTYTACTICS,
        "    return answer.on ? BoxState::On : BoxState::Off;",
        "    return answer.chosen ? BoxState::On : BoxState::Off;",
    ),
    Mutant(
        "one whose box is gone keeps the box it had",
        PARTYTACTICS,
        "    if (state == BoxState::None)\n    {\n        if (found != states_.end())\n        {\n            states_.erase(found);\n        }\n        return;\n    }",
        "    if (state == BoxState::None)\n    {\n        return;\n    }",
    ),
    Mutant(
        "a box that changed is kept as it was, and the open page never told",
        PARTYTACTICS,
        "        found->second = state;\n        return;",
        "        return;",
    ),
    Mutant(
        "the boxes are the same only when the party was read in the same order",
        PARTYTACTICS,
        "    states_.emplace(std::ranges::lower_bound(states_, id, {}, &std::pair<ObjectId, BoxState>::first), id, state);",
        "    states_.emplace_back(id, state);",
    ),
    Mutant(
        "anyone the boxes do not hold has one, not ticked",
        PARTYTACTICS,
        "    return found != states_.end() ? found->second : BoxState::None;",
        "    return found != states_.end() ? found->second : BoxState::Off;",
    ),
    Mutant(
        "the log's word of the boxes says every one is on",
        PARTYTACTICS,
        '            std::format("{}{} {}", with.empty() ? "" : ", ", Utf8(member.name), state == BoxState::On ? "on" : "off");',
        '            std::format("{}{} {}", with.empty() ? "" : ", ", Utf8(member.name), "on");',
    ),
    Mutant(
        "the log's word of the boxes does not say which of those with none is the hero",
        PARTYTACTICS,
        '                std::format("{}{}{}", without.empty() ? "" : ", ", Utf8(member.name), member.hero ? " (the hero)" : "");',
        '                std::format("{}{}{}", without.empty() ? "" : ", ", Utf8(member.name), "");',
    ),
    Mutant(
        "the log's word of the boxes leaves out those with none",
        PARTYTACTICS,
        '    return std::format("{}{}none for {}", with, with.empty() ? "" : "; ", without);',
        "    return with;",
    ),
    Mutant(
        "the box's movie is answered about no object at all",
        PARTYTACTICS,
        "    return id && *id != kNoObject ? id : std::nullopt;",
        "    return id;",
    ),
    Mutant(
        "a click with anything after its id is a click, one that unticks",
        PARTYTACTICS,
        '    if (!id || (state != "0" && state != "1"))',
        "    if (!id)",
    ),
    Mutant(
        "a click that unticks the box is read as one that ticks it",
        PARTYTACTICS,
        '    return BoxClick{*id, state == "1"};',
        "    return BoxClick{*id, true};",
    ),
    Mutant(
        "a click with no id is read as one for the creature its state numbers",
        PARTYTACTICS,
        "    if (space == std::string_view::npos)\n    {\n        return std::nullopt;\n    }\n    const std::optional<ObjectId> id = ReadBoxAsk(args.substr(0, space));",
        "    const std::optional<ObjectId> id = ReadBoxAsk(args.substr(0, space));",
    ),
    Mutant(
        "the tactics page's boxes are what the setting starts everyone as, whatever is chosen",
        MACHINE,
        "        boxes.Set(who.id, BoxFor(who, TacticsOf(who)));",
        "        boxes.Set(who.id, BoxFor(who, TacticsAnswer{}));",
    ),
    Mutant(
        "text written longer than it need be is read from a file as text",
        TEXT,
        "        if (c < lead->least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))",
        "        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))",
    ),
    Mutant(
        "half a surrogate pair is read from a file as text",
        TEXT,
        "        if (c < lead->least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))",
        "        if (c < lead->least || c > 0x10FFFF)",
    ),
    Mutant(
        "what is past the last character there is is read from a file as text",
        TEXT,
        "        if (c < lead->least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))",
        "        if (c < lead->least || (c >= 0xD800 && c <= 0xDFFF))",
    ),
    Mutant(
        "a byte that continues nothing is read from a file as text",
        TEXT,
        "            if ((next & 0xC0) != 0x80)",
        "            if ((next & 0xC0) == 0x40)",
    ),
    Mutant(
        "a character above the first plane is read from a file as one unit",
        TEXT,
        "        if (c < 0x10000)\n        {\n            out += static_cast<wchar_t>(c);\n        }\n        else",
        "        if (c < 0x110000)\n        {\n            out += static_cast<wchar_t>(c);\n        }\n        else",
    ),
    # Haste and slows in a turn's meters (issue 49).
    Mutant(
        "a creature's slows are left out of its pace",
        RULES,
        "    return MovementRate(effects.rates) * MovementRate(effects.debuffs);",
        "    return MovementRate(effects.rates);",
    ),
    Mutant(
        "a creature's Haste is left out of its pace",
        RULES,
        "    return MovementRate(effects.rates) * MovementRate(effects.debuffs);",
        "    return MovementRate(effects.debuffs);",
    ),
    Mutant(
        "movement rate effects add up, where the engine multiplies them",
        RULES,
        "            rate *= potency;",
        "            rate += potency - 1.0f;",
    ),
    Mutant(
        "a potency that is no rate is taken for one",
        RULES,
        "    return std::isfinite(potency) && potency > 0.0f;",
        "    return std::isfinite(potency);",
    ),
    Mutant(
        "a turn's pace forgets its creature's Haste and slows",
        RULES,
        "    return build * MovementRate(effects);",
        "    return build + 0.0f * MovementRate(effects);",
    ),
    Mutant(
        "a turn's pace forgets its creature's build",
        RULES,
        "    return build * MovementRate(effects);",
        "    return MovementRate(effects) + 0.0f * build;",
    ),
    Mutant(
        "with time, a turn's bar walks at a person's pace whatever its creature's",
        MACHINE,
        "settings_.paceMps * BarPace(who),",
        "settings_.paceMps,",
    ),
    Mutant(
        "the look at everyone's word of a creature's Haste and slows is not taken",
        MACHINE,
        "        c->movement = status.movement;",
        "        (void)status.movement;",
    ),
    Mutant(
        "a scan wipes what the last look read of a creature's Haste and slows",
        MACHINE,
        "            known->movement = std::move(movement);",
        "            (void)movement;",
    ),
    # The pace read again within a turn, with a turn a bar of time.
    Mutant(
        "with time, a pace that changes in a turn is the bar's only from the next turn",
        MACHINE,
        "    ApplyStatuses(obs.statuses, fx);\n    FollowPace();\n",
        "    ApplyStatuses(obs.statuses, fx);\n",
    ),
    Mutant(
        "a pace set within a turn is not the bar's",
        "src/core/TurnBudget.cpp",
        "    pace_ = paceMps;",
        "    (void)paceMps;",
    ),
    Mutant(
        "what is no pace is taken for one within a turn",
        "src/core/TurnBudget.cpp",
        "    if (!bar_ || !(paceMps > 0.0f) || !std::isfinite(paceMps))",
        "    if (!bar_)",
    ),
    Mutant(
        "a pace changed within a turn is said again as a change when the next turn begins",
        MACHINE,
        "    (void)RememberRate(current->id, rate);\n",
        "",
    ),
    Mutant(
        "a pace changed within a turn is said as a turn's beginning is",
        MACHINE,
        "a person's pace from now in this turn (",
        "a person's pace this turn (",
    ),
    Mutant(
        "what was read as a pace changed within a turn is not said",
        MACHINE,
        '    SayMovementEffects(*current, "as its pace changes in its turn");\n',
        "",
    ),
    Mutant(
        "[turns] pace changed in a turn is that turn's as its creature's pace changes",
        MACHINE,
        "    budget_.spend.SetPace(budget_.personMps * pace);",
        "    budget_.spend.SetPace(settings_.paceMps * pace);",
    ),
    Mutant(
        "a turn begun hasted is said to change its pace at its first look",
        MACHINE,
        "    budget_.pace = BarPace(who);\n",
        "",
    ),
    Mutant(
        "a turn does not keep a person's pace as it began: nothing changes within it",
        MACHINE,
        "    budget_.personMps = settings_.paceMps;\n",
        "",
    ),
    Mutant(
        "a turn's pace is never said",
        MACHINE,
        "    SayPace(who);\n",
        "",
    ),
    Mutant(
        "a turn's pace is said every turn, with Haste or with nothing",
        MACHINE,
        "    if (rate == 1.0f && before == 1.0f)",
        "    if (rate == 1.0f && before == 1.0f && TurnMs() != TurnMs())",
    ),
    Mutant(
        "a pace back at its creature's own is not said",
        MACHINE,
        "    if (rate == 1.0f && before == 1.0f)",
        "    if (rate == 1.0f)",
    ),
    Mutant(
        "what a creature's Haste came to at its last turn is forgotten",
        MACHINE,
        "        paceRates_[who] = rate;",
        "        (void)rate;",
    ),
    Mutant(
        "a pace is said as changed turn after turn, though it is as it was",
        MACHINE,
        'rate != before ? std::format(" ({:.2f} at its last turn)", before) : std::string{}));',
        'rate != 1.0f ? std::format(" ({:.2f} at its last turn)", before) : std::string{}));',
    ),
    Mutant(
        "a fight says a creature's pace against what the last fight said of it",
        MACHINE,
        "    paceRates_.clear(); // what was said of a creature's Haste in the last fight is not this one's\n",
        "",
    ),
    Mutant(
        "an effect whose potency is no rate is not said to be left out",
        MACHINE,
        'IsMovementRate(potencies[i]) ? "" : " is no rate: left out");',
        'IsMovementRate(potencies[i]) ? "" : "");',
    ),
    Mutant(
        "nothing read of a creature's movement rate effects is not said as none",
        MACHINE,
        '    if (potencies.empty())\n    {\n        return "none";\n    }\n',
        "",
    ),
    # A creature the engine would not turn to face what its attack is aimed
    # at is faced at it exactly, as its command comes to its turn to face
    # (core/Facing.h; issue 40): for which commands, whom the game layer is
    # asked to look at, what the machine does with each look, and what it
    # says of what came of a facing.
    Mutant(
        "an ability aimed at another is not the rule's",
        FACING,
        "    return type == CommandType::Attack || type == CommandType::UseAbility;",
        "    return type == CommandType::Attack;",
    ),
    Mutant(
        "an attack is not the rule's",
        FACING,
        "    return type == CommandType::Attack || type == CommandType::UseAbility;",
        "    return type == CommandType::UseAbility;",
    ),
    Mutant(
        "a walk to someone faces its creature at them, as an attack does",
        FACING,
        "    return type == CommandType::Attack || type == CommandType::UseAbility;",
        "    return type == CommandType::Attack || type == CommandType::UseAbility || "
        "type == CommandType::MoveToObject;",
    ),
    Mutant(
        "the game layer is not asked to look at anyone's turn to face",
        MACHINE,
        "                needs.turning.push_back(standing.id);\n",
        "",
    ),
    Mutant(
        "the machine faces a creature for a command that turns it to nobody",
        MACHINE,
        "        if (!TurnsToFace(look.type) || order_.Find(look.who) == nullptr)",
        "        if (order_.Find(look.who) == nullptr)",
    ),
    Mutant(
        "one the fight does not hold is faced by it",
        MACHINE,
        "        if (!TurnsToFace(look.type) || order_.Find(look.who) == nullptr)",
        "        if (!TurnsToFace(look.type))",
    ),
    Mutant(
        "a facing that did not come off is asked for again with every frame",
        MACHINE,
        "            if (!std::exchange(turning.asked, true))",
        "            if (!turning.asked)",
    ),
    Mutant(
        "what was asked of one command stands for the next: its turn to face is not faced",
        MACHINE,
        "        if (turning.command != look.command)",
        "        if (turning.command != look.command && turning.command == 0)",
    ),
    Mutant(
        "a creature is to be faced at itself, not at whom its command is aimed",
        MACHINE,
        "                face.others.push_back(look.target);",
        "                face.others.push_back(look.who);",
    ),
    Mutant(
        "a facing is asked for without saying what the command is",
        MACHINE,
        "                face.a = static_cast<std::int32_t>(look.type);\n",
        "",
    ),
    Mutant(
        "a creature that waits at its turn to face is said with every frame",
        MACHINE,
        "        else if (look.turn.waits && !std::exchange(turning.waited, true) && Logs(LogLevel::Debug))",
        "        else if (look.turn.waits && !turning.waited && Logs(LogLevel::Debug))",
    ),
    Mutant(
        "a creature that waits at its turn to face is not said",
        MACHINE,
        "        else if (look.turn.waits && !std::exchange(turning.waited, true) && Logs(LogLevel::Debug))",
        "        else if (look.turn.done && !std::exchange(turning.waited, true) && Logs(LogLevel::Debug))",
    ),
    Mutant(
        "a facing the engine would not make is said and counted as made",
        MACHINE,
        "    if (!faced.taken || !faced.now || !faced.now->done)",
        "    if (!faced.now || !faced.now->done)",
    ),
    Mutant(
        "a facing that did not come off is said with the angle it stood at, and not what the engine has now",
        MACHINE,
        ': std::format("still has it {:.2f} deg off: not done", std::abs(faced.now->degrees)),',
        ': std::format("still has it {:.2f} deg off: not done", faced.stood),',
    ),
    Mutant(
        "a facing made that the engine does not have for done after is said not to have been made",
        MACHINE,
        '                faced.taken ? "it was"\n',
        '                !faced.now ? "it was"\n',
    ),
    Mutant(
        "what the engine has after a facing is not said: the angle before it twice",
        MACHINE,
        "Aimed(faced.who), faced.stood, Aimed(faced.target), what, what, std::abs(faced.now->degrees)));",
        "Aimed(faced.who), faced.stood, Aimed(faced.target), what, what, faced.stood));",
    ),
    Mutant(
        "a creature faced is not counted for the fight's end",
        MACHINE,
        "    ++facedFirst_;\n",
        "",
    ),
    Mutant(
        "a fight says how many were faced in the fights before it too",
        MACHINE,
        "    facedFirst_ = 0;  // the last fight's, said as it ended (End) or dropped with the world unsaid\n",
        "",
    ),
    Mutant(
        "a fight in which nobody was faced says so all the same",
        MACHINE,
        "    if (facedFirst_ > 0)",
        "    if (facedFirst_ + 1 > 0)",
    ),
    # No clock on the player's turn, and a mode that keeps its user at a
    # command (issue 51). How its own mode's command is told from another's
    # hold, and from an action in a mode's form: four things of an effect
    # standing on the creature, all of them. The last two are the game's own
    # word (issue 94: the animation its table of effects gives the kind, and
    # the upkeep its table of abilities gives the mode), where the mod had a
    # list of two kinds and three mutants of the list.
    Mutant(
        "ownmode: an effect another put on a creature reads as its own mode's",
        RULES,
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
        "        if (effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
    ),
    Mutant(
        "ownmode: a mode that is off still has its creature at its command",
        RULES,
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
        "        if (effect.creator == creature && effect.animation != 0 && effect.upkeep > 0.0f)",
    ),
    Mutant(
        "ownmode: any effect of a mode that is on reads as the mode's command: a warrior with Shield Wall on, thrown, is not held",
        RULES,
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
        "        if (effect.creator == creature && effect.modeOn && effect.upkeep > 0.0f)",
    ),
    Mutant(
        "ownmode: the kind is judged by the mod's list of two again, and the Tentacle Ward is no mode's command",
        RULES,
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
        "        if (effect.creator == creature && effect.modeOn && (effect.type == 1060 || effect.type == 1041))",
    ),
    Mutant(
        "ownmode: a mode the game does not keep up is its user's stance: an Unending Flurry's turn is spent at once, and it strikes in others' turns",
        RULES,
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0 && effect.upkeep > 0.0f)",
        "        if (effect.creator == creature && effect.modeOn && effect.animation != 0)",
    ),
    Mutant(
        "ownmode: any creature at an effect's command is at its own mode's, whatever the game layer said: no hold is a hold",
        MACHINE,
        "    return actor.exists && !actor.down && actor.busy && actor.current == CommandType::ExecuteEffect &&\n           actor.ownMode != kNoMode;",
        "    return actor.exists && !actor.down && actor.busy && actor.current == CommandType::ExecuteEffect;",
    ),
    # A turn the player plays, its creature at its own mode's command.
    Mutant(
        "ownmode: a turn the player plays takes its creature's own mode's command for a hold, and is lost to it",
        MACHINE,
        "    if (obs.actor && AtItsOwnMode(*obs.actor))\n    {\n        if (const Combatant *current = order_.Current())\n        {\n            SayOwnMode(*current, *obs.actor);",
        "    if (obs.actor && AtItsOwnMode(*obs.actor) && !obs.actor->busy)\n    {\n        if (const Combatant *current = order_.Current())\n        {\n            SayOwnMode(*current, *obs.actor);",
    ),
    Mutant(
        "ownmode: nothing says why a turn waits at its creature's own mode's command",
        MACHINE,
        "        if (const Combatant *current = order_.Current())\n        {\n            SayOwnMode(*current, *obs.actor);\n        }\n        return;",
        "        return;",
    ),
    Mutant(
        "ownmode: why a turn waits at its creature's own mode's command is said every frame",
        MACHINE,
        "    if (std::exchange(budget_.ownModeSaid, actor.ownMode) == actor.ownMode)",
        "    if (false)",
    ),
    Mutant(
        "ownmode: End at its creature's own mode's command ends the turn as any End, its second rolled over",
        MACHINE,
        "    if (const Combatant *current = order_.Current(); current != nullptr && obs.actor && AtItsOwnMode(*obs.actor))\n    {\n        SpentOnItsMode(*current, obs.actor->ownMode, fx);",
        "    if (const Combatant *current = order_.Current();\n        current != nullptr && obs.actor && AtItsOwnMode(*obs.actor) && !obs.actor->busy)\n    {\n        SpentOnItsMode(*current, obs.actor->ownMode, fx);",
    ),
    Mutant(
        "ownmode: a turn spent on its creature's mode keeps what its bar had, and a second rolls over",
        MACHINE,
        '    budget_.spend.SpendAllActions();\n    EndTurn(fx, "keeping up its mode");',
        '    EndTurn(fx, "keeping up its mode");',
    ),
    Mutant(
        "ownmode: a turn spent on its creature's mode is not its actor's doing by the table",
        TURNEND,
        'TurnEnd{"keeping up its mode", true},',
        'TurnEnd{"keeping up its mode", false},',
    ),
    Mutant(
        "ownmode: a mode turned on in its own turn is taken for the turn's action, and the turn stands at executing",
        MACHINE,
        "    if (AtItsOwnMode(actor))\n    {\n        ownEffectSince_.reset();",
        "    if (AtItsOwnMode(actor) && !actor.busy)\n    {\n        ownEffectSince_.reset();",
    ),
    Mutant(
        "ownmode: End pressed as its mode's command begins waits out the settling",
        MACHINE,
        "        if (endTurnRequested_)\n        {\n            SpentOnItsMode(*current, actor.ownMode, fx);\n            return;\n        }\n",
        "",
    ),
    Mutant(
        "ownmode: held by another's effect, a creature that turns its own mode on stays held",
        MACHINE,
        "        if (std::exchange(heldByEffect_, false))\n        {\n            HoldLetGo(*current, fx);\n        }\n        if (endTurnRequested_)",
        "        if (endTurnRequested_)",
    ),
    Mutant(
        "ownmode: a turn settling at its creature's own mode's command goes back to executing, and never waits",
        MACHINE,
        "    if (obs.actor && obs.actor->busy && !ownMode)",
        "    if (obs.actor && obs.actor->busy)",
    ),
    Mutant(
        "ownmode: an order that queues nothing is taken for the mode's command, and the turn stands at executing",
        MACHINE,
        "    if (obs.actor && obs.actor->busy && !AtItsOwnMode(*obs.actor))",
        "    if (obs.actor && obs.actor->busy)",
    ),
    # A turn its tactics play.
    Mutant(
        "ownmode: a turn its tactics play is not spent on its creature's mode: its AI is told to choose behind the song",
        MACHINE,
        "    if (ownMode && !player)\n    {\n        SpentOnItsMode(*current, obs.actor->ownMode, fx);",
        "    if (ownMode && !player && !obs.actor->busy)\n    {\n        SpentOnItsMode(*current, obs.actor->ownMode, fx);",
    ),
    # The gate, at its own mode's command.
    Mutant(
        "ownmode: a move is let through at its creature's own mode's command, to wait behind it",
        MACHINE,
        "mode != kNoMode && !landing && !approaching && (kind == CommandKind::Action || kind == CommandKind::Move))",
        "mode != kNoMode && !landing && !approaching && (kind == CommandKind::Action))",
    ),
    Mutant(
        "ownmode: what is free is refused at its creature's own mode's command, and the mode cannot be turned off",
        MACHINE,
        "mode != kNoMode && !landing && !approaching && (kind == CommandKind::Action || kind == CommandKind::Move))",
        "mode != kNoMode && !landing && !approaching)",
    ),
    Mutant(
        "ownmode: its own AI's order refused at its creature's mode is said to the player",
        MACHINE,
        "        if (player && asked)\n        {\n            Refuse(who->id, std::wstring{kModeFirst}, request.type,",
        "        if (player)\n        {\n            Refuse(who->id, std::wstring{kModeFirst}, request.type,",
    ),
    Mutant(
        "ownmode: the bar's caption does not say an order at its creature's own mode's command is refused",
        MACHINE,
        "            notice_ = Notice{RefusedLine(kModeFirst, who->name, request.type), lastNow_};\n",
        "",
    ),
    # No clock of the mod's ends a turn the player plays.
    Mutant(
        "limit: a turn the player plays is stopped at max_action_ms, as an AI's is",
        MACHINE,
        "        if (const Combatant *current = order_.Current(); current != nullptr && PlayerControls(*current))\n        {\n            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))",
        "        if (const Combatant *current = order_.Current();\n            current != nullptr && PlayerControls(*current) && !actor.busy)\n        {\n            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))",
    ),
    Mutant(
        "limit: a turn an AI plays has no limit on an action either",
        MACHINE,
        "        if (const Combatant *current = order_.Current(); current != nullptr && PlayerControls(*current))\n        {\n            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))",
        "        if (const Combatant *current = order_.Current(); current != nullptr)\n        {\n            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))",
    ),
    Mutant(
        "limit: a player's action past max_action_ms is said every frame it stands",
        MACHINE,
        "            if (!heldByEffect_ && counted > limit && !std::exchange(budget_.longSaid, true))",
        "            if (!heldByEffect_ && counted > limit)",
    ),
    Mutant(
        "limit: a player's second action that stands past max_action_ms is not said, the first having been",
        MACHINE,
        "    budget_.longSaid = false;\n",
        "",
    ),
    # Whatever ends a turn by force stops what its creature was at.
    Mutant(
        "End: an action End stopped is left running into the next creature's turn",
        MACHINE,
        "                fx.push_back(Effect::Of(EffectKind::ClearCommands, actor.id));\n                const Combatant *current = order_.Current();\n                Log(LogLevel::Info,\n                    std::format(\"{}'s {} is stopped",
        "                const Combatant *current = order_.Current();\n                Log(LogLevel::Info,\n                    std::format(\"{}'s {} is stopped",
    ),
    Mutant(
        "End: a command let through and not yet begun is left to begin in the next creature's turn",
        MACHINE,
        "            if (queued || Classify(actor.current) != CommandKind::Involuntary)",
        "            if (Classify(actor.current) != CommandKind::Involuntary)",
    ),
    Mutant(
        "End: End clears what the engine holds the creature in",
        MACHINE,
        "            if (queued || Classify(actor.current) != CommandKind::Involuntary)",
        "            if (true)",
    ),
    # The time the engine has an action at a timed step that advances is not
    # counted by the limits (issue 28; core/TimedStep.h). The rule: between
    # two looks, the step had time left and its elapsed time moved.
    Mutant(
        "long: a step the engine does not time has time left",
        TIMEDSTEP,
        "    return step.durationMs > 0 && step.elapsedMs < step.durationMs;",
        "    return step.elapsedMs < step.durationMs || step.durationMs <= 0;",
    ),
    Mutant(
        "long: a step at its end still has time left",
        TIMEDSTEP,
        "    return step.durationMs > 0 && step.elapsedMs < step.durationMs;",
        "    return step.durationMs > 0 && step.elapsedMs <= step.durationMs;",
    ),
    Mutant(
        "long: a step with no time left advances if its count moves",
        TIMEDSTEP,
        "    const bool advanced = HasTimeLeft(last_) && step.elapsedMs != last_.elapsedMs;",
        "    const bool advanced = step.elapsedMs != last_.elapsedMs;",
    ),
    Mutant(
        "long: a length pushed ahead under an elapsed time that stands is a move",
        TIMEDSTEP,
        "    const bool advanced = HasTimeLeft(last_) && step.elapsedMs != last_.elapsedMs;",
        "    const bool advanced = HasTimeLeft(last_) && (step.elapsedMs != last_.elapsedMs || step.durationMs != last_.durationMs);",
    ),
    Mutant(
        "long: the time since the look before is counted from the watch's first look, not its last",
        TIMEDSTEP,
        "    const std::uint32_t since = clock - std::exchange(lookedAt_, clock);",
        "    const std::uint32_t since = clock - lookedAt_;",
    ),
    # What the limits do with it.
    Mutant(
        "long: the time an action's timed step advances is counted by max_action_ms",
        MACHINE,
        "        const std::uint32_t counted = ran - notCounted;",
        "        const std::uint32_t counted = ran;",
    ),
    Mutant(
        "long: the time an action's timed step advances is counted by max_turn_ms",
        MACHINE,
        "    const std::uint32_t counted = ran - std::min(ran, budget_.turnFreeMs);",
        "    const std::uint32_t counted = ran;",
    ),
    Mutant(
        "long: a hold's time is not counted while its command's timed step advances",
        MACHINE,
        "    if (!heldByEffect_)\n    {\n        budget_.stepFreeMs = std::min(budget_.stepFreeMs + advanced,",
        "    if (!heldByEffect_ || advanced > 0)\n    {\n        budget_.stepFreeMs = std::min(budget_.stepFreeMs + advanced,",
    ),
    Mutant(
        "long: an action whose step is renewed for ever is never stopped",
        MACHINE,
        "Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * kStepFree);",
        "Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * kStepFree * 1000);",
    ),
    Mutant(
        "long: what is left uncounted of an action is once its limit at most, not four times",
        MACHINE,
        "Patience(static_cast<std::uint32_t>(settings_.maxActionMs)) * kStepFree);",
        "Patience(static_cast<std::uint32_t>(settings_.maxActionMs)));",
    ),
    Mutant(
        "long: an AI's turn in an action whose step is renewed for ever never ends",
        MACHINE,
        "Patience(static_cast<std::uint32_t>(settings_.maxTurnMs)) * kStepFree);",
        "Patience(static_cast<std::uint32_t>(settings_.maxTurnMs)) * kStepFree * 1000);",
    ),
    Mutant(
        "long: what one action's step left uncounted is the next action's too",
        MACHINE,
        "        budget_.stepFreeMs = 0;\n",
        "",
    ),
    Mutant(
        "long: that a limit's time went by uncounted is said with every step",
        MACHINE,
        "        if (ran > limit && counted <= limit && !std::exchange(budget_.stepSaid, true))",
        "        if (ran > limit && counted <= limit && !std::exchange(budget_.stepSaid, false))",
    ),
    Mutant(
        "long: that a limit's time went by uncounted is said once a turn, not of each action",
        MACHINE,
        "        budget_.stepFreeMs = 0;\n        budget_.stepSaid = false;\n",
        "        budget_.stepFreeMs = 0;\n",
    ),
    Mutant(
        "long: that a turn's limit went by uncounted is said with every step",
        MACHINE,
        "        if (ran > limit && !std::exchange(budget_.turnStepSaid, true))",
        "        if (ran > limit && !std::exchange(budget_.turnStepSaid, false))",
    ),
    Mutant(
        "long: the look after a command's end is not taken, and its step advances into the next command",
        MACHINE,
        "    const std::uint32_t notCounted = StepTimeNotCounted(actor);\n",
        "    const std::uint32_t notCounted = actor.busy ? StepTimeNotCounted(actor) : 0;\n",
    ),
]


# The test that caught each mutant last time, by the mutant's name: run first,
# alone, and the rest only if it passes. Of a mutant's 23.5 s, 3.6 were the
# scripted tests run to the one that catches it, in whatever order Catch2 drew
# (issue 67, 2026-10-09: caught after 10 cases or after 603), and a mutant only
# the fights catch ran every scripted test first and then the fights. In the
# repository, so that a fresh clone has it; sorted and written only when it
# changes, so that a run that catches everything as before leaves no diff.
CAUGHT = Path(__file__).with_name("mutants.caught.json")
# What a worker's build has over the core preset's: no debug information and
# no listing of the tests after the link. Nobody reads a mutant's symbols, and
# CTest, which the listing is for, is not what runs a mutant's tests: of the
# turn machine's 20 s a mutant, 14 were the library and the link (issue 67,
# 2026-10-09, one worker on a machine at full load; with these the build was
# 7.0 s where it was 11.6). The tree's own presets are as they were.
FLAGS = {
    "CMAKE_CXX_FLAGS_DEBUG": "/Ob0 /Od /RTC1",
    "CMAKE_EXE_LINKER_FLAGS_DEBUG": "/INCREMENTAL:NO",
    "CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE": "PRE_TEST",
}
# The random fights' tag, and where a test case failed that a crash or a hang
# left without a result.
FIGHTS = "[fights]"
STOPPED = "stopped"


def run(
    command: list[str], cwd: Path, timeout: int | None = None
) -> subprocess.CompletedProcess[str]:
    # What a failing test prints may hold any bytes (a file's damaged text, in
    # the cases of core/PartyTactics.h): read as UTF-8 with what is not
    # replaced, where the console's code page would stop the reader on one.
    return subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
        creationflags=LOW,
    )


def sync(worker: Path, tree: Path = REPO) -> None:
    """Brings a worker's copy up to the tree: a file is written only where it
    differs, so what did not change is not built again. By what the files
    hold, not their dates: a copy a stopped run left with a mutant in it is
    mended here, at the next run's start."""
    wanted: set[Path] = set()
    sources = [tree / name for name in FILES if (tree / name).exists()]
    for part in TREES:
        sources += [f for f in (tree / part).rglob("*") if f.is_file()]
    for source in sources:
        target = worker / source.relative_to(tree)
        wanted.add(target)
        data = source.read_bytes()
        if not target.exists() or target.read_bytes() != data:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    for part in TREES:
        for stale in (worker / part).rglob("*"):
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


def has_flags(cache: str) -> bool:
    """Whether a build, by its CMakeCache.txt, was configured with the
    workers' flags: a copy an earlier tool configured is configured again."""
    found = {}
    for line in cache.splitlines():
        name, _, value = line.partition("=")
        found[name.partition(":")[0]] = value
    return all(found.get(name) == value for name, value in FLAGS.items())


def prepare(worker: Path, jobs: int) -> str:
    """A worker's copy, configured and built. Answers what went wrong, or
    nothing."""
    sync(worker)
    made = worker / "build" / "core"
    cache = made / "CMakeCache.txt"
    if not (
        (made / "build.ninja").exists()
        and cache.exists()
        and has_flags(cache.read_text(encoding="utf-8", errors="replace"))
    ):
        command = [
            "cmake",
            "--preset",
            "core",
            f"-DVCPKG_INSTALLED_DIR={PACKAGES.as_posix()}",
            "-DVCPKG_MANIFEST_INSTALL=OFF",
            *(f"-D{name}={value}" for name, value in FLAGS.items()),
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


def ready(workers: list[Path], jobs: int) -> str:
    """The workers' copies made ready side by side, each with so many
    compilers. Answers what went wrong, or nothing."""
    problems: dict[Path, str] = {}

    def one(worker: Path) -> None:
        problems[worker] = prepare(worker, jobs)

    threads = [threading.Thread(target=one, args=(w,)) for w in workers]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    for worker, problem in problems.items():
        if problem:
            return f"{worker.name} does not configure or build:\n{problem}"
    if run([str(tests_of(workers[0]))], workers[0]).returncode != 0:
        return "the unmutated core does not pass its tests; fix that first"
    return ""


class Pool:
    """The mutants still to run, handed to the workers file by file. A worker
    is given the next mutant of the file it is at, and writes it straight
    over the last: a file is put back, and compiled once more beside the next
    mutant's, only as its worker leaves it. Put back after every mutant, each
    mutant compiled two files, and the one after a header's paid for the
    header's includers again (issue 67, 2026-10-09).

    The headers' mutants are one lot, header by header: a header's build is
    every file that includes it (Settings.h: 33 steps and 132 s with one
    compiler on a machine at full load, 2026-10-09; a .cpp's is three steps
    and 6 s), so one worker at them, with its share of the cores, is what the
    machine has to spare, and from one header to the next the files both
    have are built once.

    A worker whose lot is done takes the lot with the most left that no
    worker is at, and when every lot has a worker, the one with the most
    left."""

    HEADERS = "the headers"

    def __init__(self, mutants: list[Mutant]) -> None:
        self.left: dict[str, list[Mutant]] = {}
        for mutant in mutants:
            lot = mutant.file if mutant.file.endswith(".cpp") else self.HEADERS
            self.left.setdefault(lot, []).append(mutant)
        self.left.get(self.HEADERS, []).sort(key=lambda mutant: mutant.file)
        self.at: dict[Path, str] = {}
        self.lock = threading.Lock()

    def take(self, worker: Path) -> Mutant | None:
        with self.lock:
            lot = self.at.get(worker, "")
            if lot not in self.left:
                if not self.left:
                    return None
                taken = set(self.at.values())
                lot = max(self.left, key=lambda n: (n not in taken, len(self.left[n])))
                self.at[worker] = lot
            mutant = self.left[lot].pop(0)
            if not self.left[lot]:
                del self.left[lot]
            return mutant


class Failure(NamedTuple):
    case: str  # the test case
    where: str  # an assertion of it that failed: file:line, and what a FAIL said
    fight: bool  # whether it is one of the random fights


CASE = re.compile(r'<TestCase name="([^"]*)"(?: tags="([^"]*)")?')
RESULT = re.compile(r'<OverallResult success="(true|false)"')
# Whatever the reporter writes of an assertion is one that failed: it is not
# asked for those that passed (-s).
ASSERTION = re.compile(
    r"<(?:Expression|Failure|Exception|FatalErrorCondition)\b[^>]*?"
    r'filename="([^"]*)" line="(\d+)"[^>]*>\s*([^<\r\n]*)'
)


def failures(xml: str) -> list[Failure]:
    """The assertions that failed, by what Catch2's XML reporter wrote: of
    each test case that failed, the place of every assertion that did, once
    each, in the order they failed.

    Every one and not the first alone: of the 31 groups of mutants that
    failed their cases at the same first assertions, 18 failed different
    ones after it, which a test that goes on past a failed CHECK tells
    apart (issue 67, 2026-10-09). An assertion with words of its own (a
    FAIL) has them beside its place, since the random fights say every
    broken promise from one line (Must, tests/test_fights.cpp): the words up
    to the round it was broken in, which are the promise's. A case with no
    result is where a crash or a hang stopped the run, and is said after
    what had failed in it; a reader that waited for the document's end would
    lose it."""
    found = []
    parts = CASE.split(xml)
    for name, tags, body in zip(parts[1::3], parts[2::3], parts[3::3], strict=True):
        result = RESULT.search(body)
        if result and result[1] == "true":
            continue
        places = {}
        for at in ASSERTION.finditer(body):
            where = f"{PureWindowsPath(at[1]).name}:{at[2]}"
            if words := unescape(at[3]).partition(" (round ")[0].strip():
                where += f": {words}"
            places[where] = None
        if not (result and places):
            places[STOPPED] = None
        fight = FIGHTS in (tags or "")
        found += [Failure(unescape(name), where, fight) for where in places]
    return found


def spec_of(case: str) -> str:
    """Catch2's test spec for one test case by its name: a comma would part
    the name into two specs and a bracket begin a tag (380 of the 626
    scripted cases' names have one, 2026-10-09, each found alone by this)."""
    return re.sub(r'([\\,\[\]"])', r"\\\1", case)


def tests(worker: Path, spec: str, *more: str) -> tuple[int | None, list[Failure]]:
    """Runs what a spec names of a worker's tests. Answers the program's exit
    code, none if it hung past the timeout, and what failed.

    In the order the tests are written: Catch2's own order is drawn anew each
    run (3.11, whatever its help says of the default; seen 2026-10-09), and
    the first test to fail is the one kept. A spec that names no test is no
    failure, so a kept test that was renamed catches nothing and the whole
    run finds the mutant another."""
    command = [str(tests_of(worker)), spec, "--order", "decl", "-r", "xml"]
    try:
        result = run(
            [*command, "--allow-running-no-tests", *more], worker, timeout=TIMEOUT_S
        )
    except subprocess.TimeoutExpired as hung:
        return None, failures(hung.stdout or "")
    return result.returncode, failures(result.stdout)


def test(
    mutant: Mutant, text: str, worker: Path, jobs: int, first: str, every: bool
) -> tuple[str, list[Failure]]:
    """One mutant, written into a worker's copy over the text its file had as
    the copy was made: an edit to the tree while mutants run is not taken up
    half way (it was, read from the tree each time: a run whose mutants "did
    not compile" by the dozen, 2026-10-02). Answers the verdict and what
    failed: the first assertion to fail, or with `every` all that did.

    The test that caught it last time first, if one is named, then the
    scripted cases, and the random fights only for a mutant those let pass:
    of a mutant's 30 s, 22
    were the fights' tests, run to their end whatever had failed before them
    (timed 2026-10-04, when three gates at once made a full run an hour; the
    user: "Why does a full mutant run take an hour?"). The verdict is the
    same whichever catches it: caught if any test fails, and the two halves
    together are every test the binary runs by default. Which half caught it
    is said, since a mutant only the fights catch is a promise with no
    scripted case of its own."""
    if text.count(mutant.old) != 1:
        return (
            f"not applied: the text is in {mutant.file} {text.count(mutant.old)} times",
            [],
        )
    path = worker / mutant.file
    path.write_text(text.replace(mutant.old, mutant.new), encoding="utf-8")
    if build(worker, jobs).returncode != 0:
        return "did not compile", []
    for spec in [*([spec_of(first)] if first else []), f"~{FIGHTS}", FIGHTS]:
        code, failed = tests(worker, spec, *(() if every else ("--abort",)))
        if code is None:
            return "caught (the tests hung)", failed
        if code != 0:
            by = " (by the fights)" if failed and failed[0].fight else ""
            return f"caught{by}", failed
    return "SURVIVED", []


def kept(file: Path = CAUGHT) -> dict[str, str]:
    """The test that caught each mutant last time."""
    return json.loads(file.read_text(encoding="utf-8")) if file.exists() else {}


def keep(caught: dict[str, str], file: Path = CAUGHT) -> None:
    """Writes the record of which test caught each mutant, if it changed: in
    the mutants' order by name, a line each, so that a run's changes are the
    lines of the mutants it changed. A mutant the list has no more, or that
    nothing caught, has no line."""
    names = {mutant.name for mutant in MUTANTS}
    caught = {name: case for name, case in caught.items() if case and name in names}
    if caught != kept(file):
        text = json.dumps(caught, indent=0, sort_keys=True, ensure_ascii=False)
        file.write_text(text + "\n", encoding="utf-8", newline="\n")


def prints(rows: dict[str, dict]) -> dict[str, frozenset[tuple[str, str]]]:
    """Of a record of every test against each mutant, what each mutant
    failed: the assertions, each by its test case and its place. A mutant
    nothing failed for has none."""
    return {
        name: frozenset((case, where) for case, where in row["failed"])
        for name, row in rows.items()
        if row["failed"]
    }


def same(failed: dict[str, frozenset[tuple[str, str]]]) -> list[list[str]]:
    """The mutants that failed the same assertions in the same tests as
    another, in their groups: by the tests, each group checks one thing."""
    groups: dict[frozenset[tuple[str, str]], list[str]] = {}
    for name, places in failed.items():
        groups.setdefault(places, []).append(name)
    return [names for names in groups.values() if len(names) > 1]


def above(
    failed: dict[str, frozenset[tuple[str, str]]], place: dict[str, object]
) -> dict[str, str]:
    """Each mutant that failed all that another of the same place did and
    more, with the one of those that failed the least, which is above none
    of them itself. The tests that catch the lesser one catch this one."""
    found = {}
    for name, places in failed.items():
        less = [
            other
            for other, theirs in failed.items()
            if theirs < places and place[other] == place[name]
        ]
        if less:
            found[name] = min(less, key=lambda other: len(failed[other]))
    return found


def place(text: str, old: str) -> int:
    """Where in a file an edit is, for saying that two are in one function:
    the first line of the function it is in, which is the last line before
    it that begins a declaration at the margin, if that one opens a
    definition with a parenthesis. An edit in no function (a table's row, a
    header's constant, a structure's member) is at its own line: two rows of
    a table are two promises."""
    at = text.index(old) + len(old) - len(old.lstrip())
    own = head = text.rfind("\n", 0, at) + 1
    while head and not text[head].isalpha():
        head = text.rfind("\n", 0, head - 1) + 1
    line = text[head : text.find("\n", head)]
    if line[:1].isalpha() and "(" in line and not line.endswith(";"):
        return head
    return own


def within(mutants: list[Mutant]) -> dict[str, object]:
    """The file and the place in it of each mutant's edit, by the tree."""
    sources = {
        file: (REPO / file).read_text(encoding="utf-8")
        for file in {mutant.file for mutant in mutants}
    }
    return {
        mutant.name: (mutant.file, place(sources[mutant.file], mutant.old))
        for mutant in mutants
    }


def report(rows: dict[str, dict]) -> list[str]:
    """What a record of every test against each mutant says of whether each
    earns its keep, of the mutants the list still has, in the tree the
    record was made over.

    Two things are said to go by. Mutants that failed the same assertions in
    the same tests: evidence for a reader, who judges by the names and the
    design's promises, since two mutants one broad assertion catches may be
    two promises, and a coarse test. And a rule a reader can check (the
    user's, 2026-10-09: everything earns its keep, and what does not is
    removed): a mutant that failed all that another of the same function did
    and more is the same promise broken more crudely, caught whenever the
    finer one is, and goes; the finer one stands for it. One above a mutant
    of another function only is another promise that trips some of the same
    assertions, and stays; and so does one that stops the test program,
    whose failures are not known whole."""
    edits = {mutant.name: mutant for mutant in MUTANTS}
    rows = {name: row for name, row in rows.items() if name in edits}
    failed = prints(rows)

    def size(name: str) -> int:
        return len(edits[name].old) + len(edits[name].new)

    groups = same(failed)
    lines = [
        f"{sum(map(len, groups))} mutants in {len(groups)} groups failed the same"
        + " assertions in the same tests as another (the smallest edit first):"
    ]
    for group in groups:
        case, where = min(failed[group[0]])
        lines.append(f"  {len(failed[group[0]])} failed, as {where} ({case})")
        lines += [
            f"    {size(name):5} {edits[name].file}: {name}"
            for name in sorted(group, key=size)
        ]
    whole = {
        name: places
        for name, places in failed.items()
        if STOPPED not in {where for _, where in places}
    }
    crude = above(whole, within([edits[name] for name in whole]))
    lines.append(
        f"{len(crude)} failed all that another mutant of the same function did and"
        + " more, the same promise broken more crudely: each goes, for the one named:"
    )
    lines += [
        f"  {edits[name].file}: {name} ({len(whole[name])}), for {finer} ({len(whole[finer])})"
        for name, finer in crude.items()
    ]
    elsewhere = len(above(whole, dict.fromkeys(whole))) - len(crude)
    lines.append(
        f"{elsewhere} failed all that a mutant of another function did and more:"
        + " another promise that trips some of the same assertions, and stays"
    )
    lines.append(
        f"{len(failed) - len(whole)} stop the test program: what they fail is not"
        + " known whole, and they stay"
    )
    fights = [name for name, row in rows.items() if "fights" in row["verdict"]]
    lines.append(
        f"{len(fights)} caught by the random fights alone, with no scripted case:"
    )
    for name in fights:
        lines.append(f"  {name}")
        lines += [f"    {where} ({case})" for case, where in rows[name]["failed"]]
    return lines


def alone(work: Path = WORK) -> IO[str] | None:
    """The lock on a tree's copies, held while it is open or this process
    lives, or nothing if another run holds it. A second run in a tree wrote
    into the copies the first was building, and neither's verdicts were of
    the mutants they named (2026-10-09: a record's reprint, asked for while
    the record was being made, ran the mutants it lacked)."""
    work.mkdir(parents=True, exist_ok=True)
    lock = (work / "lock").open("w")
    try:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        lock.close()
        return None
    return lock


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
    parser.add_argument(
        "--matrix",
        type=Path,
        metavar="FILE",
        help="run every test against each mutant, and record in FILE what"
        " failed; a mutant FILE already has is not run again",
    )
    args = parser.parse_args()
    chosen = [
        m for m in MUTANTS if not args.names or any(w in m.name for w in args.names)
    ]
    rows: dict[str, dict] = {}
    if args.matrix and args.matrix.exists():
        rows = json.loads(args.matrix.read_text(encoding="utf-8"))
        chosen = [m for m in chosen if m.name not in rows]
        if not chosen:
            print("\n".join(report(rows)))
            return 0
    if not PACKAGES.exists():
        print(
            f"no {PACKAGES}: build the core preset once (tools\\build.ps1 -Preset core)"
        )
        return 1
    # Held until this returns, however it does.
    lock = alone()
    if lock is None:
        print(f"another run is using {WORK}: one at a time in a tree")
        return 1
    workers = [WORK / f"w{n}" for n in range(max(1, min(args.jobs, len(chosen))))]
    # A worker's share of the cores, for its builds: a mutant's own uses one
    # compiler, or two as its worker leaves a file, unless it is a header's.
    share = max(1, (os.cpu_count() or 2) // len(workers))
    started = time.monotonic()
    if problem := ready(workers, share):
        print(problem)
        return 1
    print(
        f"{len(workers)} at a time, in {WORK} ({time.monotonic() - started:.0f} s to make ready)",
        flush=True,
    )

    pool = Pool(chosen)
    before = kept()
    caught = dict(before)
    verdicts: dict[str, str] = {}
    said = threading.Lock()

    def work(worker: Path) -> None:
        # The file of the copy that holds a mutant, and what it held before.
        file, held = "", b""
        try:
            while mutant := pool.take(worker):
                start = time.monotonic()
                was = before.get(mutant.name, "")
                # Whatever goes wrong is said, and counted as not caught.
                try:
                    if mutant.file != file:
                        if file:
                            (worker / file).write_bytes(held)
                        file = ""
                        held = (worker / mutant.file).read_bytes()
                        file = mutant.file
                    text = held.decode("utf-8").replace("\r\n", "\n")
                    first = "" if args.matrix else was
                    verdict, failed = test(
                        mutant, text, worker, share, first, bool(args.matrix)
                    )
                except Exception as error:  # noqa: BLE001
                    verdict, failed = f"failed: {error}", []
                cases = [failure.case for failure in failed]
                with said:
                    verdicts[mutant.name] = verdict
                    caught[mutant.name] = was if was in cases else "".join(cases[:1])
                    print(
                        f"{verdict:<32} {mutant.name} ({time.monotonic() - start:.0f} s)",
                        flush=True,
                    )
                    if args.matrix:
                        rows[mutant.name] = {
                            "file": mutant.file,
                            "verdict": verdict,
                            "failed": [[f.case, f.where] for f in failed],
                        }
                        # As it goes: a run that is stopped keeps what it has.
                        args.matrix.write_text(
                            json.dumps(rows, indent=1), encoding="utf-8"
                        )
        finally:
            if file:
                (worker / file).write_bytes(held)

    threads = [threading.Thread(target=work, args=(w,)) for w in workers]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    keep(caught)
    if args.matrix:
        print("\n".join(report(rows)))
    bad = [
        name for name, verdict in verdicts.items() if not verdict.startswith("caught")
    ]
    hung = [name for name, verdict in verdicts.items() if "hung" in verdict]
    for name in hung:
        print(f"hung, and counted as caught: {name}")
    # News: a test that held a promise may have stopped holding it.
    for name, case in before.items():
        if caught.get(name) not in ("", case):
            print(f"caught, but not by the test kept for it ({case}): {name}")
    fights = sum(1 for verdict in verdicts.values() if "by the fights" in verdict)
    print(f"{fights} caught by the random fights alone, with no scripted case")
    print(
        f"{len(chosen) - len(bad)} of {len(chosen)} caught in {time.monotonic() - started:.0f} s"
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
