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
BAR = "src/core/Bar.cpp"
PORTRAITS = "src/core/Portraits.cpp"
EASING = "src/core/Easing.cpp"

MUTANTS = [
    Mutant(
        "the gate lets anyone act",
        MACHINE,
        "if (current == nullptr || current->id != request.object)",
        "if (current == nullptr)",
    ),
    Mutant(
        "the gate never spends an action", MACHINE, "        --budget_.actions;\n", "\n"
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
        "if (!actor.moving || budget_.movementUsed < budget_.movementMax + kOvershoot)",
        "if (true)",
    ),
    Mutant(
        "the game is never paused for a turn",
        MACHINE,
        "return phase == Phase::Starting || phase == Phase::TurnStart || phase == Phase::AwaitingOrder;",
        "(void)phase;\n    return false;",
    ),
    Mutant(
        "an action is given back every time",
        MACHINE,
        "&& budget_.actions == 0 && !budget_.refunded;",
        "&& budget_.actions == 0;",
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
        "if (clock_ - executingSince_ > static_cast<std::uint32_t>(settings_.maxActionMs))",
        "if (false)",
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
        "        phase_ = Phase::Suspended;\n        return fx;",
        "        return fx;",
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
        "if (player && asked && request.walk && *request.walk > budget_.MovementLeft() + kReachSlack)",
        "if (false)",
    ),
    Mutant(
        "reach ignores a big target",
        RULES,
        "if (targetPersonalSpace > 1.0f)",
        "if (targetPersonalSpace > 100.0f)",
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
        "found->second.kind != Condition::Disabled || found->second.turns <= 0)",
        "found->second.kind != Condition::Disabled || found->second.turns <= 99)",
    ),
    Mutant(
        "a condition that has had its turns is never ended",
        MACHINE,
        "if (held.turns <= 0 && held.seen != Condition::None)",
        "if (held.turns < 0 && held.seen != Condition::None)",
    ),
    Mutant(
        "a pin takes no movement",
        MACHINE,
        "        budget_.movementMax = 0.0f;\n",
        "",
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
        "        if (turns > 0)\n        {\n            usedNow_.push_back(Hold{who->id, request.ability, turns});",
        "        if (turns > 99)\n        {\n            usedNow_.push_back(Hold{who->id, request.ability, turns});",
    ),
    Mutant(
        "a held cooldown is released a turn early",
        MACHINE,
        "if (h.creature == who.id && --h.turns <= 0)",
        "if (h.creature == who.id && --h.turns <= 1)",
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
        "the camera turns the long way round",
        EASING,
        "float turn = std::remainder(toRadians - fromRadians, 2.0f * kPi);",
        "float turn = toRadians - fromRadians;",
    ),
    Mutant(
        "an eased move goes at a rate of its own, whatever the frame rate",
        EASING,
        "return std::exp2(-dtSeconds / halfLifeSeconds);",
        "return 1.0f - 0.5f * dtSeconds / halfLifeSeconds;",
    ),
    Mutant(
        "a character's own AI spends the player's turn",
        MACHINE,
        "if (player && request.source == Source::Script && (kind == CommandKind::Action",
        "if (player && request.source == Source::Unknown && (kind == CommandKind::Action",
    ),
    Mutant(
        "a refused order still runs the world",
        MACHINE,
        "        orderPending_ = false;\n        if (verdict == Verdict::Block && phase_ == Phase::Dispatching)",
        "        if (verdict == Verdict::Block && phase_ == Phase::Dispatching)",
    ),
    Mutant(
        "a refusal while an order runs the world lets it run on",
        MACHINE,
        "if (verdict == Verdict::Block && phase_ == Phase::Dispatching)",
        "if (verdict == Verdict::Block && phase_ == Phase::Idle)",
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
