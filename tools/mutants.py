"""Break src/core on purpose, one way at a time, and see the tests fail.

SkyrimFollowerTactics' rule (its dev/PRINCIPLES.md, "Check that the tests
would catch a bug"): a green suite can pass while testing nothing, and
breaking the code on purpose shows what is never checked. Each mutant below
is one promise of dev/DESIGN.md broken by a small edit. The tool applies it,
builds the core preset, runs the tests, and puts the file back. A mutant is
caught if a test fails (or the run hangs past the timeout), and survives if
every test passes. A survivor is a promise no test holds.

Run from a shell where tools\\build.ps1 has imported the developer
environment, after one `.\\tools\\build.ps1 -Preset core`:

    python tools\\mutants.py            every mutant
    python tools\\mutants.py gate joiner   those whose name contains "gate" or "joiner"

Exits non-zero if any mutant survives or fails to compile.
"""

from __future__ import annotations

import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TESTS = REPO / "build" / "core" / "tests" / "tb_tests.exe"
TIMEOUT_S = 600


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
PORTRAITS = "src/core/Portraits.cpp"
JSON = "src/core/Json.cpp"
KEYS = "src/core/Keys.h"
PREVIEW = "src/core/Preview.cpp"

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
        '    if (!order_.AnyStanding(Side::Enemy))\n    {\n        End(fx, "no enemy left standing");',
        '    if (false)\n    {\n        End(fx, "no enemy left standing");',
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
        "        if (clock_ - executingSince_ > limit)",
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
    Mutant(
        "an AI may start commands without limit",
        MACHINE,
        "if (!player && budget_.commands >= settings_.maxCommandsPerTurn)",
        "if (false)",
    ),
    Mutant(
        "rounds are never padded", MACHINE, "if (roundRunMs_ < minMs)", "if (false)"
    ),
    Mutant(
        "a conversation does not stand the gate aside",
        MACHINE,
        "        phase_ = Phase::Suspended;\n        SetSpeed(1.0f, fx);\n        return fx;",
        "        SetSpeed(1.0f, fx);\n        return fx;",
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
        "reach ignores the attacker's size",
        RULES,
        "const float spaces = space(attackerSpace) + space(targetSpace);",
        "const float spaces = 0.0f * space(attackerSpace) + space(targetSpace);",
    ),
    Mutant(
        "reach forgets the swing's play",
        RULES,
        "    return spaces + kMeleeGap + kSwingPlay;",
        "    return spaces + kMeleeGap;",
    ),
    Mutant(
        "a ranged weapon reaches past 60 m",
        RULES,
        "        return std::min(weaponRange, kLongestRange) + spaces;",
        "        return weaponRange + spaces;",
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
        "    const bool owes = held.kind == Condition::Disabled && held.turns > 0;",
        "    const bool owes = held.kind == Condition::Disabled && held.turns > 99;",
    ),
    Mutant(
        "a condition that has had its turns is never ended",
        MACHINE,
        "if (held.turns <= 0 && held.seen != Condition::None)",
        "if (held.turns < 0 && held.seen != Condition::None)",
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
        "    const bool holds = (settings_.conditionTurns == 0 || held.onClock) && held.seen == Condition::Disabled;",
        "    const bool holds = false;",
    ),
    Mutant(
        "a turn waits out an effect no condition names",
        MACHINE,
        " && clock_ - executingSince_ >= kEffectGraceMs)",
        " && clock_ - executingSince_ >= kEffectGraceMs * 10)",
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
        "        heldByEffect_ = false; // let go within the grace: the turn is its own again",
        "        // let go",
    ),
    Mutant(
        "an AI held by an effect is told its turn has come",
        MACHINE,
        " && seenActor_->current == CommandType::ExecuteEffect)",
        " && seenActor_->current == CommandType::Invalid)",
    ),
    Mutant(
        "an AI an effect let go within the grace is never told again",
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
        "an enemy's turn runs at the game's own speed",
        MACHINE,
        "    return PlayerControls(who) ? 1.0f : settings_.aiTurnSpeed;",
        "    return PlayerControls(who) ? 1.0f : 1.0f;",
    ),
    Mutant(
        "the player's turn runs at the enemies' speed",
        MACHINE,
        "    return PlayerControls(who) ? 1.0f : settings_.aiTurnSpeed;",
        "    return PlayerControls(who) ? settings_.aiTurnSpeed : settings_.aiTurnSpeed;",
    ),
    Mutant(
        "a conversation plays at the enemies' speed",
        MACHINE,
        "        phase_ = Phase::Suspended;\n        SetSpeed(1.0f, fx);",
        "        phase_ = Phase::Suspended;",
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
        "    GiveActionBack();\n    budget_.refunded = true;\n    Log(LogLevel::Info,\n        std::format(\"{}'s {} failed",
        "    budget_.refunded = true;\n    Log(LogLevel::Info,\n        std::format(\"{}'s {} failed",
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
        '        if (!order_.AnyStanding(Side::Enemy))\n        {\n            End(fx, "no enemy left standing");',
        '        if (false)\n        {\n            End(fx, "no enemy left standing");',
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
        "a turn's camera moves before its speed is set",
        MACHINE,
        "    // whether a pan got there (src/game/Camera.cpp).\n    SetSpeed(TurnSpeed(who), fx);",
        "    // whether a pan got there (src/game/Camera.cpp).",
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
        "a beast with no camera is rendered as a person is",
        PORTRAITS,
        "    if (person)\n    {\n        return {PortraitSource::Render, {}};",
        "    if (person || appearance > 0)\n    {\n        return {PortraitSource::Render, {}};",
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
        "            roundAgedMs_ += ran;",
        "            ;",
    ),
    Mutant(
        "an effect that held a creature before its turn runs the clock",
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
        "    wantScan_ = true;\n    phase_ = Phase::AwaitingOrder;\n}",
        "    phase_ = Phase::AwaitingOrder;\n}",
    ),
    Mutant(
        "an attack through a shut door is let through",
        MACHINE,
        "        if (player && asked && request.doorInTheWay)",
        "        if (player && asked && request.doorInTheWay && request.inCombat && !request.inCombat)",
    ),
    Mutant(
        "a creature standing at a walk is left to max_action_ms",
        MACHINE,
        "        else if (clock_ - stoodSince_ >= kStoodMs)",
        "        else if (clock_ - stoodSince_ >= kStoodMs * 1000)",
    ),
    Mutant(
        "a swing on the spot is taken for standing at a walk",
        MACHINE,
        "(at == CommandKind::Move || at == CommandKind::Free))",
        "(at == CommandKind::Move || at == CommandKind::Free || at == CommandKind::Action))",
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
        "kBarField, view.line ? 1 : 0);",
        "kBarField, view.line ? 1 : 1);",
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
        "        if (turns > 0)\n        {\n            usedNow_.push_back(Hold{who->id, request.ability, turns, turns});",
        "        if (turns > 99)\n        {\n            usedNow_.push_back(Hold{who->id, request.ability, turns, turns});",
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
        "    if (player && !landing && request.source == Source::Script &&",
        "    if (player && !landing && request.source == Source::Unknown &&",
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
        "    budget_.spend.Prepay(walk);",
        "",
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
        "        (void)budget_.spend.SpendAction(owed);\n        budget_.spend.Credit(charge->walk);",
        "        budget_.spend.Credit(charge->walk);",
    ),
    Mutant(
        "time: a kept action's walk is paid again as it lands",
        MACHINE,
        "        budget_.spend.Credit(charge->walk);",
        "",
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
    Mutant(
        "time: enemies have no Combat Tactics by rank",
        MACHINE,
        "    if (who.side != Side::Party && who.rank >= kCritter && who.rank <= kEliteBoss)",
        "    if (who.side == Side::Ally && who.rank >= kCritter && who.rank <= kEliteBoss)",
    ),
    Mutant(
        "time: an AI is not told to choose again with time left",
        MACHINE,
        "        if (Timed() && budget_.spend.ActionsLeft() > 0 && !budget_.outOfMovement &&",
        "        if (Timed() && budget_.spend.ActionsLeft() > 99 && !budget_.outOfMovement &&",
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
]


def run(
    command: list[str], timeout: int | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, cwd=REPO, capture_output=True, text=True, timeout=timeout, check=False
    )


def build() -> bool:
    return run(["cmake", "--build", "--preset", "core"]).returncode == 0


def test(mutant: Mutant) -> str:
    path = REPO / mutant.file
    original = path.read_text(encoding="utf-8")
    if original.count(mutant.old) != 1:
        return f"not applied: the text is in {mutant.file} {original.count(mutant.old)} times"
    try:
        path.write_text(original.replace(mutant.old, mutant.new), encoding="utf-8")
        if not build():
            return "did not compile"
        try:
            result = run([str(TESTS)], timeout=TIMEOUT_S)
        except subprocess.TimeoutExpired:
            return "caught (the tests hung)"
        return "caught" if result.returncode != 0 else "SURVIVED"
    finally:
        path.write_text(original, encoding="utf-8")


def main() -> int:
    wanted = sys.argv[1:]
    chosen = [m for m in MUTANTS if not wanted or any(w in m.name for w in wanted)]
    if not build() or run([str(TESTS)]).returncode != 0:
        print("the unmutated core does not build or pass its tests; fix that first")
        return 1
    bad = 0
    for mutant in chosen:
        start = time.monotonic()
        verdict = test(mutant)
        print(
            f"{verdict:<32} {mutant.name} ({time.monotonic() - start:.0f} s)",
            flush=True,
        )
        bad += 0 if verdict.startswith("caught") else 1
    build()  # the unmutated core again
    print(f"{len(chosen) - bad} of {len(chosen)} caught")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
