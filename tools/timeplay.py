"""A fight played in turns, with no hands on the game.

Through Eluvian, the development bench's MCP server (the sibling checkout
..\\DAOEluvianMCP), as tools/sweep.py does: its bridge launches the game
into a save, and its tools are called over HTTP in the game.

What it plays is a scenario by name, each a small script over the same
helpers: the player's orders go through Eluvian's `order` (the engine's own
click functions, so they pass the gate as the player's), the bar is read
from `turnbased_state` (`time_ms`, `time_left_ms`), and the turns
from the mod's events. It prints a transcript: what each order cost, what
each turn rolled over, what was kept for the next turn; and copies
turnbased.log beside the build for the write-up (dev/PLAYTEST-*.md).

    python tools\\timeplay.py                   # the smoke: a fight with the spiders
    python tools\\timeplay.py --scenario stun   # another, by name
    python tools\\timeplay.py --keep            # leave the game running at the end

It LAUNCHES THE GAME (Lyna's autosave 2, loaded only: nothing is saved) and
quits it at the end; it refuses to go on in a game it did not launch. The
object ids are that save's.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = "http://127.0.0.1:8930"
# forceLoad: the user removed the game's other mods (2026-10-02, "force load if
# necessary"); Eluvian presses Force Load only if the load asks for content, and
# refuses to save after it.
SAVE = {"character": "Lyna", "kind": "auto", "number": 2, "forceLoad": True}
HERO = "0x82"
TAMLEN = "0x10d"
DAGGER = "0x14b"  # Dar'Misu, in Lyna's pack
SPIDER = "bed110cr_weak_spider"


def documents() -> Path:
    """The user's Documents folder, which may not be under the profile (OneDrive)."""
    with winreg.OpenKey(
        winreg.HKEY_CURRENT_USER,
        r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders",
    ) as key:
        value, _ = winreg.QueryValueEx(key, "Personal")
    return Path(winreg.ExpandEnvironmentStrings(value))


def stamp() -> str:
    return time.strftime("%H:%M:%S")


class Game:
    """Eluvian: the bridge for the game's process, HTTP for the game."""

    def __init__(self, eluvian: Path, owner: str) -> None:
        self.bridge_script = eluvian / "tools" / "eluvian_bridge.py"
        self.owner = owner
        self.off = False  # the mod off for the run (--off): the game's real time

    def bridge(self, tool: str, arguments: dict | None = None) -> dict:
        done = subprocess.run(
            [
                sys.executable,
                str(self.bridge_script),
                "--call",
                tool,
                json.dumps(arguments or {}),
            ],
            capture_output=True,
            text=True,
            timeout=400,
            check=False,
        )
        try:
            answer = json.loads(done.stdout)
        except ValueError:
            return {"isError": True, "answer": {"error": done.stdout[-400:]}}
        return answer

    def tool(
        self, name: str, arguments: dict | None = None, timeout: float = 60
    ) -> dict:
        request = urllib.request.Request(
            f"{BASE}/api/tool/{name}",
            data=json.dumps(arguments or {}).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            try:
                return json.load(error)
            except ValueError:
                return {"error": f"HTTP {error.code}"}
        except (urllib.error.URLError, TimeoutError) as error:
            return {"error": str(error)}

    def state(self) -> dict:
        answer = self.tool("turnbased_state")
        return answer.get("state", answer)

    def setting(self, section: str, key: str, value: str) -> None:
        answer = self.tool(
            "turnbased_settings", {"section": section, "key": key, "value": value}
        )
        print(
            f"{stamp()} setting [{section}] {key} = {value}: {answer.get('warnings', answer)}"
        )

    def hostiles(self, near: str = HERO) -> list[dict]:
        answer = self.tool("creatures", {"side": "hostile", "near": near})
        return [c for c in answer.get("creatures", []) if not c.get("dead")]

    def events(self, since: int) -> tuple[list[dict], int]:
        out: list[dict] = []
        while True:
            with urllib.request.urlopen(
                f"{BASE}/api/events?since={since}&limit=1000", timeout=30
            ) as response:
                page = json.load(response)
            out += page["events"]
            since = page.get("next", since)
            if not page.get("truncated"):
                return out, since


def left(state: dict) -> str:
    return (
        f"{state.get('time_left_ms', 0) / 1000:.2f} of {state.get('time_ms', 0) / 1000:.2f} s, "
        f"{state.get('movement_left', 0):.1f} m"
    )


def settle(game: Game, who: str, seconds: float = 14.0) -> dict:
    """Waits until the turn waits on an order again, or is no longer `who`'s."""
    deadline = time.time() + seconds
    state = game.state()
    while time.time() < deadline:
        state = game.state()
        current = (state.get("current") or {}).get("id")
        if not state.get("active") or current != who:
            return state
        if state.get("phase") == "awaiting order":
            return state
        time.sleep(0.2)
    return state


def player_turn(game: Game, who: str, name: str, orders: int = 4) -> None:
    """One of the player's turns: attacks on the nearest hostile until the turn is over."""
    for n in range(orders):
        before = game.state()
        if (before.get("current") or {}).get("id") != who or not before.get("active"):
            return
        if before.get("phase") != "awaiting order":
            before = settle(game, who)
            if (before.get("current") or {}).get("id") != who:
                return
        targets = game.hostiles(who)
        if not targets:
            print(f"{stamp()}   {name}: no hostile left")
            return
        target = min(targets, key=lambda c: c.get("distance", 99.0))
        answer = game.tool(
            "order", {"action": "attack", "id": who, "target": target["id"]}
        )
        after = settle(game, who)
        still = (after.get("current") or {}).get("id") == who and after.get("active")
        spent = before.get("time_left_ms", 0) - (
            after.get("time_left_ms", 0) if still else 0
        )
        print(
            f"{stamp()}   {name} order {n + 1}: attack {target['name']} {target['id']} "
            f"({target.get('distance', -1):.1f} m), taken {answer.get('taken', answer.get('error'))}; "
            f"before {left(before)}; "
            + (
                f"after {left(after)} (spent {spent / 1000:.2f} s)"
                if still
                else f"the turn ended (last refusal: {after.get('last_refusal')!r})"
            )
        )
        if not still:
            return
    print(f"{stamp()}   {name}: {orders} orders given; ending the turn")
    game.tool("turnbased_end_turn", {})


SHOWN = (
    "turn_started",
    "turn_ended",
    "turn_lost",
    "refused",
    "action_pending",
    "action_landed",
    "action_dropped",
    "action_given_back",
    "fight_began",
    "fight_over",
    "condition_held",
    "condition_ended",
)


def show_events(game: Game, cursor: int, names: tuple | None = None) -> int:
    """Prints the mod's events since `cursor` (those about `names`, if given)."""
    events, cursor = game.events(cursor)
    for e in events:
        p = e.get("payload", {})
        shown = e.get("topic", "").startswith("turnbased.") and p.get("event") in SHOWN
        if shown and (names is None or p.get("name") in names):
            fields = ("round", "name", "detail", "command", "note", "value")
            print("   event", p.get("event"), {k: p[k] for k in fields if k in p})
    return cursor


def fight(game: Game, rounds: int, cursor: int) -> int:
    """Plays the fight on: the party's turns by `player_turn`, the rest watched."""
    party = {HERO: "Lyna", TAMLEN: "Tamlen"}
    deadline = time.time() + 60.0 * rounds
    last = None
    while time.time() < deadline:
        state = game.state()
        if not state.get("active"):
            print(f"{stamp()} the fight is over")
            break
        if state.get("round", 0) > rounds:
            print(f"{stamp()} round {state.get('round')}: enough")
            break
        current = state.get("current") or {}
        key = (state.get("round"), current.get("id"))
        if key != last and current:
            last = key
            print(
                f"{stamp()} round {state.get('round')}: {current.get('name')} ({current.get('side')}), "
                f"{state.get('phase')}, {left(state)}"
            )
        if current.get("yours") and current.get("id") in party:
            if state.get("phase") == "awaiting order":
                max_health(game, current["id"])
                player_turn(game, current["id"], party[current["id"]])
            else:
                time.sleep(0.2)
        else:
            time.sleep(0.3)
    cursor = show_events(game, cursor)
    return cursor


def max_health(game: Game, who: str) -> None:
    creature = game.tool("creature", {"id": who})
    creature = creature.get("creature", creature)
    most = (creature.get("health") or {}).get("max")
    if most:
        game.tool("stage_health", {"id": who, "value": most})


def end_fight(game: Game) -> None:
    for _ in range(20):
        if game.tool("health").get("mode") != "combat":
            return
        for c in game.hostiles():
            game.tool("stage_kill", {"id": c["id"]})
        time.sleep(1.5)


# What the duel adds to the hall's two spiders: (template, dx, dy) from (5, 0).
DUEL_EXTRA: list[tuple[str, float, float]] = [(SPIDER, 6, 3)]


def begin_spiders(game: Game, wait: bool = True) -> int:
    """Lyna on her dagger walks into the first hall, and its two spiders drop."""
    print(
        f"{stamp()} equip the dagger:",
        game.tool("script", {"command": "EquipItem", "args": [HERO, DAGGER]}),
    )
    cursor = game.tool("health").get("cursor", 0)
    print(
        f"{stamp()} walk in:",
        game.tool("order", {"action": "move", "id": HERO, "point": [5, 0, 0]}).get(
            "taken"
        ),
    )
    if not wait:
        # The mod off: the fight is the game's own, and no event says so.
        for _ in range(60):
            if game.tool("health").get("mode") == "combat":
                break
            time.sleep(0.25)
        print(f"{stamp()} the fight began (the game's own)")
        return cursor
    began = game.tool(
        "wait_for",
        {"topic": "turnbased.fight_began", "since": cursor, "timeoutMs": 30000},
        timeout=60,
    )
    print(f"{stamp()} the fight began: {began.get('found')}")
    return cursor


def scenario_smoke(game: Game) -> None:
    """A fight through: what each attack costs, what is kept, what rolls over."""
    cursor = begin_spiders(game)
    fight(game, rounds=5, cursor=cursor)
    end_fight(game)


def scenario_stun(game: Game) -> None:
    """Holds by their own time: a stun of 2 s on Lyna between her turns, one of 6 s on a spider."""
    cursor = begin_spiders(game)
    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    spiders = game.hostiles(HERO)
    victim = spiders[0]["id"]
    print(
        f"{stamp()} Lyna's turn: stun {victim} for 6 s (a turn lost, and half the next):",
        str(
            game.tool(
                "stage_effect",
                {"action": "apply", "id": victim, "effect": "stun", "seconds": 6},
            )
        )[:160],
    )
    time.sleep(1.2)
    held = game.state().get("held")
    print(f"{stamp()} held: {held}")
    attack(game, HERO, "one attack")
    game.tool("turnbased_end_turn", {})
    # On someone else's turn: Lyna stunned for 2 s, half her next bar.
    for _ in range(100):
        current = (game.state().get("current") or {}).get("id")
        if current and current != HERO:
            break
        time.sleep(0.1)
    print(
        f"{stamp()} on {current}'s turn: stun Lyna for 2 s:",
        str(
            game.tool(
                "stage_effect",
                {"action": "apply", "id": HERO, "effect": "stun", "seconds": 2},
            )
        )[:160],
    )
    time.sleep(1.2)
    print(f"{stamp()} held: {game.state().get('held')}")
    state = wait_turn(game, HERO)
    if state is None:
        return
    print(f"{stamp()} Lyna's next turn: {left(state)} (2.00 of 4.00 s expected)")
    max_health(game, HERO)
    attack(game, HERO, "one attack")
    game.tool("turnbased_end_turn", {})
    state = wait_turn(game, HERO)
    if state is not None:
        print(f"{stamp()} and the turn after: {left(state)}; held {state.get('held')}")
    show_events(game, cursor)
    end_fight(game)


def scenario_crowd(game: Game) -> None:
    """Six more spiders round the party: a long fight, for pace and the enemies' turns."""
    cursor = begin_spiders(game)
    here = game.tool("creature", {"id": HERO})
    here = here.get("creature", here).get("position", {"x": 5, "y": 0, "z": 0})
    for dx, dy in ((4, 0), (-4, 0), (0, 4), (0, -4), (3, 3), (-3, -3)):
        spawned = game.tool(
            "stage_spawn",
            {
                "template": SPIDER,
                "point": [here["x"] + dx, here["y"] + dy, here["z"]],
            },
        )
        print(f"{stamp()} spawn: {str(spawned)[:120]}")
    fight(game, rounds=6, cursor=cursor)
    end_fight(game)


def wait_turn(game: Game, who: str, seconds: float = 90.0) -> dict | None:
    """Waits for `who`'s turn to wait on an order; None if the fight ends first."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        state = game.state()
        if not state.get("active"):
            return None
        current = state.get("current") or {}
        if current.get("id") == who and state.get("phase") == "awaiting order":
            return state
        # Tamlen's turns are passed: this scenario is Lyna's.
        if (
            current.get("yours")
            and current.get("id") != who
            and state.get("phase") == "awaiting order"
        ):
            game.tool("turnbased_end_turn", {})
        time.sleep(0.25)
    return None


def attack(game: Game, who: str, label: str) -> dict:
    """One attack on the nearest hostile, and the state once it has settled."""
    before = game.state()
    targets = game.hostiles(who)
    if not targets:
        print(f"{stamp()}   {label}: no hostile left")
        return before
    target = min(targets, key=lambda c: c.get("distance", 99.0))
    game.tool("order", {"action": "attack", "id": who, "target": target["id"]})
    after = settle(game, who)
    print(
        f"{stamp()}   {label}: attack {target['name']} {target['id']} ({target.get('distance', -1):.1f} m); "
        f"before {left(before)}; after {left(after)}; current {(after.get('current') or {}).get('name')}, "
        f"{after.get('phase')}"
    )
    after["_target"] = target["id"]
    return after


def scenario_bar(game: Game) -> None:
    """Combat Tactics on Lyna, a turn ended early, a kept attack whose target falls, a move too far."""
    for rank in range(4):
        answer = game.tool(
            "script", {"command": "AddAbility", "args": [HERO, 100110 + rank]}
        )
        print(f"{stamp()} Combat Tactics {rank + 1}: {answer}")
    cursor = begin_spiders(game)
    # More spiders, so the fight outlasts the scenario.
    for dx, dy in ((6, 3), (6, -3), (-6, 3)):
        game.tool("stage_spawn", {"template": SPIDER, "point": [5 + dx, dy, 0]})

    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    print(
        f"{stamp()} turn 1, rank 4 of Combat Tactics: {left(state)} (5.00 s expected)"
    )
    attack(game, HERO, "one attack")
    state = game.state()
    print(
        f"{stamp()} ending the turn early with {left(state)}: 2.00 s at most rolls over"
    )
    game.tool("turnbased_end_turn", {})
    time.sleep(1.0)

    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    print(
        f"{stamp()} turn 2: {left(state)} (the bar 5.00 s and what rolled over, 7.00 s at most)"
    )
    kept = None
    for n in range(6):
        after = attack(game, HERO, f"attack {n + 1}")
        if (after.get("current") or {}).get("id") != HERO:
            kept = after.get("_target")
            break
    print(
        f"{stamp()} the turn ended on a kept attack at {kept}; killing its target before it lands"
    )
    if kept:
        print(
            f"{stamp()} kill:",
            str(game.tool("stage_kill", {"id": kept}))[:160],
        )

    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    print(
        f"{stamp()} turn 3, the kept attack's target gone: {left(state)}; last refusal {state.get('last_refusal')!r}"
    )
    here = game.tool("creature", {"id": HERO})
    here = here.get("creature", here).get("position", {"x": 5, "y": 0, "z": 0})
    far = game.tool(
        "order",
        {"action": "move", "id": HERO, "point": [here["x"] + 40, here["y"], here["z"]]},
    )
    time.sleep(1.0)
    state = game.state()
    print(
        f"{stamp()} a move 40 m off: {str(far)[:120]}; then {left(state)}; last refusal {state.get('last_refusal')!r}"
    )
    near = game.tool(
        "order",
        {"action": "move", "id": HERO, "point": [here["x"] + 3, here["y"], here["z"]]},
    )
    after = settle(game, HERO)
    print(f"{stamp()} a move 3 m off: {str(near)[:80]}; then {left(after)}")
    show_events(game, cursor, names=("Lyna", None))
    end_fight(game)


def scenario_abilities(game: Game) -> None:
    """Each of Lyna's talents once: that it passes the gate, and what it costs."""
    listed = game.tool("abilities", {"id": HERO})
    abilities = listed.get("talents", []) + listed.get("spells", [])
    print(
        f"{stamp()} Lyna's talents and spells ({listed.get('count')} abilities in all):"
    )
    for a in abilities:
        print("   ", a)
    cursor = begin_spiders(game)
    for dx, dy in ((6, 3), (6, -3), (-6, 3), (-6, -3)):
        game.tool("stage_spawn", {"template": SPIDER, "point": [5 + dx, dy, 0]})
    usable = [a for a in abilities if a.get("id")]
    for a in usable[:8]:
        state = wait_turn(game, HERO)
        if state is None:
            return
        max_health(game, HERO)
        targets = game.hostiles(HERO)
        if not targets:
            return
        target = min(targets, key=lambda c: c.get("distance", 99.0))
        answer = game.tool(
            "order",
            {
                "action": "ability",
                "id": HERO,
                "ability": a["id"],
                "target": target["id"],
            },
        )
        after = settle(game, HERO)
        print(
            f"{stamp()}   {a.get('name')} ({a['id']}) on {target['id']}: {str(answer)[:100]}; "
            f"before {left(state)}; after {left(after)}; current {(after.get('current') or {}).get('name')}; "
            f"last refusal {after.get('last_refusal')!r}"
        )
        if (after.get("current") or {}).get("id") == HERO:
            game.tool("turnbased_end_turn", {})
        time.sleep(0.5)
    fight(game, rounds=0, cursor=cursor)
    end_fight(game)


def party_health(game: Game) -> dict[str, float]:
    """Each party member's share of health left (0 when down)."""
    out = {}
    for who, name in ((HERO, "Lyna"), (TAMLEN, "Tamlen")):
        creature = game.tool("creature", {"id": who})
        creature = creature.get("creature", creature)
        health = creature.get("health") or {}
        dead = creature.get("dead")
        most = health.get("max") or 1.0
        out[name] = 0.0 if dead else round((health.get("current") or 0.0) / most, 2)
    return out


def scenario_duel(game: Game) -> None:
    """The same fight in turns and in the game's real time, measured: the party against six spiders.

    Every party member attacks the nearest hostile whenever it may, nobody is
    healed, and the fight is run to its end: how long it took on the clock,
    in rounds, and what it cost the party. With `--off` the mod is off and
    the fight is the game's own real time, the same orders given.
    """
    cursor = begin_spiders(game, wait=not game.off)
    for template, dx, dy in DUEL_EXTRA:
        game.tool("stage_spawn", {"template": template, "point": [5 + dx, dy, 0]})
    began = time.time()
    lowest = party_health(game)
    rounds = 0
    turns = 0
    last = None
    ordered: dict[str, str] = {}
    while time.time() - began < 420:
        health = game.tool("health")
        if health.get("mode") != "combat":
            break
        if health.get("paused") and not game.state().get("active"):
            game.tool("pause", {"paused": False})
        for name, share in party_health(game).items():
            lowest[name] = min(lowest[name], share)
        state = game.state()
        if state.get("active"):
            rounds = max(rounds, state.get("round", 0))
            current = state.get("current") or {}
            key = (state.get("round"), current.get("id"))
            if key != last:
                last = key
                turns += 1
            if current.get("yours") and state.get("phase") == "awaiting order":
                who = current["id"]
                targets = game.hostiles(who)
                if targets:
                    target = min(targets, key=lambda c: c.get("distance", 99.0))
                    game.tool(
                        "order", {"action": "attack", "id": who, "target": target["id"]}
                    )
                    settle(game, who)
                else:
                    game.tool("turnbased_end_turn", {})
            else:
                time.sleep(0.2)
        else:
            # Real time: an attack on the nearest for whoever has no living target.
            alive = {c["id"] for c in game.hostiles()}
            for who in (HERO, TAMLEN):
                if ordered.get(who) not in alive:
                    targets = game.hostiles(who)
                    if targets:
                        target = min(targets, key=lambda c: c.get("distance", 99.0))
                        game.tool(
                            "order",
                            {"action": "attack", "id": who, "target": target["id"]},
                        )
                        ordered[who] = target["id"]
            time.sleep(0.4)
    took = time.time() - began
    final = party_health(game)
    print(
        f"{stamp()} DUEL: {took:.0f} s on the clock, {rounds} rounds, {turns} turns; the party at the end {final}, "
        f"at its lowest {lowest}; hostiles left {len(game.hostiles())}"
    )
    show_events(game, cursor, names=())
    end_fight(game)


def scenario_mixed(game: Game) -> None:
    """A caster, an archer and something big beside the spiders: what each does with its bar.

    The party is kept alive (health to full on each of its turns), so the
    enemies' turns can be watched for six rounds: how many actions each takes,
    what is kept, whether any stalls or is stopped.
    """
    cursor = begin_spiders(game)
    for template, dx, dy in (
        ("hurlock_emissary_off", 10, 2),
        ("bandit_human_archer", 12, -2),
        ("ogre_default", 9, -5),
    ):
        spawned = game.tool(
            "stage_spawn",
            {"template": template, "point": [5 + dx, dy, 0]},
        )
        creature = spawned.get("creature") or {}
        print(
            f"{stamp()} spawn {template}: {creature.get('id')} {creature.get('name')} {spawned.get('error', '')}"
        )
    fight(game, rounds=6, cursor=cursor)
    end_fight(game)


# Spells whose conjuring and cast are longer than a 4 s bar, as the first
# sweep found them (2026-10-02): Chain Lightning 4.99 s, Inferno 3.99 s (kept
# once the bar has been spent on anything).
KEPT_SPELLS = (10211, 10002)
# Where the game's spells begin in its table of abilities, a dozen rows from each.
SPELL_ROWS = (10000, 10200, 10500, 11000, 11100, 12000, 13000, 14000)


def scenario_interrupt(game: Game) -> None:
    """A spell kept for the next turn, and its caster struck: is it interrupted?

    Lyna is no mage, so she is given spells (the game lets any creature with
    the ability and the mana or stamina cast): first the two found to conjure
    longer than a 4 s bar (KEPT_SPELLS), then others that conjure. She casts
    one at the start of her turn; it is kept and lands on her next turn, and
    between the two the spiders bite. A bite over her threshold (5 and half
    her level, with no Combat Training) loses it: the log's "struck for" says
    what the blow was and whether the spell held, and the events say
    action_dropped "struck" or action_landed.
    """
    cursor = begin_spiders(game)
    spells: list[dict] = []
    # Something that hits harder than a weak spider's 7 or 8: her threshold
    # is 16 with Combat Training's first rank.
    spawned = game.tool(
        "stage_spawn",
        {"template": "ogre_default", "point": [9, -2, 0]},
    )
    print(
        f"{stamp()} spawn an ogre: {(spawned.get('creature') or {}).get('id')} {spawned.get('error', '')}"
    )

    # And the game's own spells by their rows (ABI_base's ids: primal 10xxx,
    # entropy 11xxx, creation 12xxx and on), the ones whose type is a spell:
    # the emissary's are quick, and a conjuring longer than her bar is what
    # is looked for.
    # (GetM2DAInt on table 1, ABI_base: AbilityType 2 is a spell; Speed is
    # its conjuring speed's row, 0 for none.) The slowest first.
    def cell(column: str, row: int) -> int:
        answer = game.tool(
            "script", {"command": "GetM2DAInt", "args": [1, column, row]}
        )
        value = answer.get("value")
        return value if isinstance(value, int) else 0

    known = {a["id"] for a in spells}
    slow = []
    for first in SPELL_ROWS:
        for row in range(first, first + 12):
            if row in known or cell("AbilityType", row) != 2:
                continue
            speed = cell("Speed", row)
            if speed > 0 and cell("ConjureAnim", row) != 0:
                slow.append((speed, row))
    slow.sort(reverse=True)
    print(f"{stamp()} spells that conjure, by their speed's row: {slow}")
    rows = list(KEPT_SPELLS) + [row for _, row in slow if row not in KEPT_SPELLS]
    spells = [{"id": row, "name": f"spell {row}"} for row in rows[:5]]
    for a in spells:
        print(
            f"{stamp()} give Lyna {a.get('name')}:",
            str(
                game.tool("script", {"command": "AddAbility", "args": [HERO, a["id"]]})
            )[:120],
        )
    listed = game.tool("abilities", {"id": HERO})
    names = {a.get("id"): a.get("name") for a in listed.get("spells", [])}
    for a in spells:
        a["name"] = names.get(a["id"], a.get("name"))
        state = wait_turn(game, HERO)
        if state is None:
            return
        max_health(game, HERO)
        targets = game.hostiles(HERO)
        if not targets:
            return
        target = min(targets, key=lambda c: c.get("distance", 99.0))
        answer = game.tool(
            "order",
            {
                "action": "ability",
                "id": HERO,
                "ability": a["id"],
                "target": target["id"],
            },
        )
        after = settle(game, HERO)
        kept = [c for c in after.get("charging", []) if c.get("id") == HERO]
        if kept:
            # Her card carries the pending spell's icon.
            time.sleep(0.4)
            picture(game, f"interrupt-kept-{a['id']}")
        print(
            f"{stamp()}   {a.get('name')} ({a['id']}) on {target['name']}: {str(answer)[:100]}; "
            f"before {left(state)}; after {left(after)}; kept {kept}; "
            f"last refusal {after.get('last_refusal')!r}"
        )
        if (after.get("current") or {}).get("id") == HERO and after.get(
            "phase"
        ) == "awaiting order":
            game.tool("turnbased_end_turn", {})
        time.sleep(0.5)
        cursor = show_events(game, cursor, names=("Lyna",))
    end_fight(game)


def picture(game: Game, name: str) -> None:
    """The game's own frame, interface and all, kept beside the build."""
    shot = game.tool("screenshot", {"image": False, "source": "game"})
    source = shot.get("file")
    if not source:
        print(f"{stamp()} no picture: {str(shot)[:160]}")
        return
    out = ROOT / "build" / f"timeplay-{name}.png"
    shutil.copyfile(source, out)
    print(f"{stamp()} picture: {out}")


def scenario_look(game: Game) -> None:
    """What the player sees of the bar: pictures of a turn begun, after a swing, and on an enemy's turn."""
    cursor = begin_spiders(game)
    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    time.sleep(1.5)
    picture(game, "look-1-turn-begun")
    attack(game, HERO, "one attack")
    time.sleep(1.0)
    picture(game, "look-2-after-a-swing")
    game.tool("turnbased_end_turn", {})
    for _ in range(60):
        current = game.state().get("current") or {}
        if current and not current.get("yours"):
            break
        time.sleep(0.1)
    time.sleep(0.6)
    picture(game, "look-3-an-enemys-turn")
    show_events(game, cursor, names=("Lyna",))
    end_fight(game)
    time.sleep(3.0)
    picture(game, "look-4-the-fight-over")


def point_at(game: Game, where: dict, name: str) -> None:
    """Rests the game's cursor somewhere (Eluvian's pointer: the real mouse stays put) and takes a picture.

    A creature behind the hero, or out of view, has no pixel to point at: the
    camera is turned to face it, and then a sixth of the way round at a time,
    until one shows.
    """
    answer: dict = {}
    for turn in range(6):
        if "target" in where:
            if turn == 0:
                game.tool("camera", {"action": "face", "target": where["target"]})
            else:
                yaw = (game.tool("camera", {}).get("yawDegrees") or 0.0) + 60.0
                game.tool("camera", {"action": "turn", "yawDegrees": yaw % 360.0})
            time.sleep(0.4)
        answer = game.tool("pointer", {**where, "seconds": 20})
        if not answer.get("error"):
            break
    time.sleep(0.6)  # the preview follows the cursor at ten a second
    state = game.state()
    under = game.tool("cursor")
    print(
        f"{stamp()}   pointer {where}: {str(answer)[:200]}; under it {str(under)[:160]}; "
        f"{left(state)}; can_delay {state.get('can_delay')}"
    )
    picture(game, name)


def position_of(game: Game, who: str) -> list[float]:
    creature = game.tool("creature", {"id": who})
    creature = creature.get("creature", creature)
    at = creature.get("position") or [5.0, 0.0, 0.0]
    if isinstance(at, dict):
        return [
            float(at.get("x", 0.0)),
            float(at.get("y", 0.0)),
            float(at.get("z", 0.0)),
        ]
    return [float(v) for v in at]


def scenario_hover(game: Game) -> None:
    """What only shows under the cursor: the path, the caption's cost, the cost on the game's bar, Delay.

    On Lyna's turn the cursor is rested on a spider (an attack's walk and
    cost), on the ground near and far (a move's), and on Tamlen (nothing to
    walk); then a turn is put off with Delay and taken at the round's end.
    A picture of each, the game's own frame with its interface.
    """
    cursor = begin_spiders(game)
    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    time.sleep(1.0)
    picture(game, "hover-0-turn-begun")
    targets = game.hostiles(HERO)
    if targets:
        near = min(targets, key=lambda c: c.get("distance", 99.0))
        far = max(targets, key=lambda c: c.get("distance", 0.0))
        point_at(game, {"target": near["id"]}, "hover-1-attack-near")
        point_at(game, {"target": far["id"]}, "hover-2-attack-far")
    at = position_of(game, HERO)
    point_at(game, {"point": [at[0] + 3, at[1], at[2]]}, "hover-3-move-near")
    point_at(game, {"point": [at[0] + 14, at[1] + 6, at[2]]}, "hover-4-move-far")
    print(f"{stamp()}   release:", str(game.tool("pointer", {"release": True}))[:120])
    # Delay: the turn passes on, and comes back whole at the round's end.
    print(f"{stamp()}   delay:", str(game.tool("turnbased_delay", {}))[:160])
    time.sleep(1.0)
    picture(game, "hover-5-delayed")
    state = wait_turn(game, HERO)
    if state is not None:
        print(
            f"{stamp()}   her turn again: round {state.get('round')}, {left(state)}, can_delay {state.get('can_delay')}"
        )
        # A kept attack, and the cursor on the spider that has it coming.
        attack(game, HERO, "first attack")
        attack(game, HERO, "second attack")
        attack(game, HERO, "third attack (kept, if the bar is short)")
    cursor = show_events(game, cursor, names=("Lyna",))
    end_fight(game)


# What a run's log is read for afterwards: each line's count, and its first.
LOG_SIGNS = (
    "walk runs past its bar",
    " s taken now",
    "its icon is",
    "is dropped: struck",
    "with a spell pending",
    "walk to its kept attack's target ends",
    "Lands next turn",
    "owed to its next turn",
    "its own effect",
    "is held by an effect's command",
    "is free of the effect's command",
    "Out of movement",
    "turn is put off to the round's end",
    "[warning]",
    "[error]",
)


def summary(log: Path) -> None:
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    print(f"{stamp()} the log's signs ({len(lines)} lines):")
    for sign in LOG_SIGNS:
        found = [line for line in lines if sign in line]
        if found:
            print(f"    {len(found):4d}  {sign}: {found[0][:170]}")


STEALTH = 100075  # ABI_base: the skill Stealth, a mode (skill_stealth.ncs)


def scenario_stealth(game: Game) -> None:
    """A stealthed opening attack: does the blow that begins the fight land?

    An open question of dev/REBALANCE.md ("A stealthed opening attack"), and
    the user's way to try it, 2026-10-02: "stealth can be tested. the eluvian
    mcp can add talent to char, no?" As combat mode begins the mod clears every
    combatant's command, the attacker's with the rest, so that the first turn
    starts from a still world. If the game enters combat mode before the blow
    lands, the blow that began the fight is cancelled, and the free strike
    stealth is for is lost.

    Lyna is given Stealth and turns it on where she stands, before the hall's
    spiders have dropped. A spider is put down behind her, which should not
    see her, and she is told to attack it. What is read: whether the fight
    began before she was told to (the spider saw her: nothing learned),
    then ten times a second the game's mode and the spider's health, so the
    order of the two is seen, and the log's own lines.
    """
    print(
        f"{stamp()} equip the dagger:",
        game.tool("script", {"command": "EquipItem", "args": [HERO, DAGGER]}),
    )
    # Tamlen too: a spider that sees him begins the fight as well as one that
    # sees her (the first try, 2026-10-02: combat mode as the spider appeared).
    for who, name in ((HERO, "Lyna"), (TAMLEN, "Tamlen")):
        print(
            f"{stamp()} give {name} Stealth:",
            str(game.tool("script", {"command": "AddAbility", "args": [who, STEALTH]}))[
                :80
            ],
        )
    time.sleep(0.5)
    for who, name in ((HERO, "Lyna"), (TAMLEN, "Tamlen")):
        print(
            f"{stamp()} {name}'s Stealth on:",
            str(
                game.tool(
                    "order",
                    {"action": "ability", "id": who, "ability": STEALTH, "target": who},
                )
            )[:100],
        )
    time.sleep(3.0)
    lyna = game.tool("creature", {"id": HERO})
    lyna = lyna.get("creature", lyna)
    print(
        f"{stamp()} Lyna as she stands: at {lyna.get('position')}, effects "
        f"{sorted({e.get('type') for e in lyna.get('effects') or []})}"
    )
    at = lyna.get("position") or [0.0, 0.0, 0.0]
    if isinstance(at, dict):
        at = [at.get("x", 0.0), at.get("y", 0.0), at.get("z", 0.0)]
    cursor = game.tool("health").get("cursor", 0)
    # Behind her: the hall, and its spiders' trigger, is the other way.
    spawned = game.tool(
        "stage_spawn",
        {"template": SPIDER, "point": [at[0] - 9.0, at[1], at[2]]},
    )
    spider = (spawned.get("creature") or {}).get("id")
    print(f"{stamp()} a spider behind her: {spider} {spawned.get('error', '')}")
    if not spider:
        return
    time.sleep(2.0)
    mode = game.tool("health").get("mode")
    print(f"{stamp()} the game's mode with the spider there and Lyna stealthed: {mode}")
    if mode == "combat":
        print(
            f"{stamp()} the spider saw her: the fight began before her attack, nothing to learn here"
        )

    def health_of(who: str) -> float | None:
        creature = game.tool("creature", {"id": who})
        creature = creature.get("creature", creature)
        return (creature.get("health") or {}).get("current")

    before = health_of(spider)
    print(f"{stamp()} the spider's health before: {before}")
    began = time.time()
    print(
        f"{stamp()} attack:",
        str(game.tool("order", {"action": "attack", "id": HERO, "target": spider}))[
            :160
        ],
    )
    combat_at = None
    struck_at = None
    seen_second = -1
    for _ in range(100):
        now = time.time() - began
        if combat_at is None and game.tool("health").get("mode") == "combat":
            combat_at = now
            print(f"{stamp()}   combat mode at {now:.2f} s")
        health = health_of(spider)
        if (
            struck_at is None
            and before is not None
            and health is not None
            and health < before
        ):
            struck_at = now
            print(f"{stamp()}   the spider struck at {now:.2f} s: {before} to {health}")
        if combat_at is not None and (struck_at is not None or now - combat_at > 15.0):
            break
        if int(now) != seen_second:
            seen_second = int(now)
            health_now = game.tool("health")
            # The game's own auto-pause, half a second into a fight that is
            # not the mod's (the player's option): Space, as a player would.
            if health_now.get("paused") and not game.state().get("active"):
                print(
                    f"{stamp()}   the game's own auto-pause: unpaused, as Space would"
                )
                game.tool("pause", {"paused": False})
            lyna = game.tool("creature", {"id": HERO})
            lyna = lyna.get("creature", lyna)
            where = game.tool("geometry", {"id": HERO, "target": spider})
            print(
                f"{stamp()}   {now:4.1f} s: mode {health_now.get('mode')}, paused {health_now.get('paused')}; "
                f"Lyna's command {lyna.get('command')}; to the spider {where.get('distance')}; "
                f"turn-based {game.state().get('active')}"
            )
        time.sleep(0.1)
    if combat_at is None:
        print(
            f"{stamp()} no fight began: the attack did not reach, or the order was not taken"
        )
    elif struck_at is None:
        print(
            f"{stamp()} THE OPENING BLOW DID NOT LAND within 6 s of the fight's start "
            "(the mod cleared it, or it missed: see the log's lines below)"
        )
    else:
        print(
            f"{stamp()} the opening blow landed {struck_at - combat_at:+.2f} s from combat mode's start"
        )
    # And then: the struck spider fights, and the turns begin.
    for _ in range(40):
        if game.state().get("active"):
            break
        if game.tool("health").get("paused"):
            game.tool("pause", {"paused": False})
        time.sleep(0.25)
    show_events(game, cursor)
    state = game.state()
    print(
        f"{stamp()} the turn now: {(state.get('current') or {}).get('name')}, round {state.get('round')}, "
        f"last refusal {state.get('last_refusal')!r}"
    )
    end_fight(game)


WALKING_BOMB = 12005  # ABI_base: a damage over time of 20 s on its target


def scenario_dot(game: Game) -> None:
    """A damage over time on the turn clock: its ticks by the rounds, not by the wall's clock.

    The user, playing, 2026-10-02: "there's a environmental poison trap that
    keeps hitting the party even during turn-based combat." Its ticks are a
    script's own timer (EVENT_TYPE_DOT_TICK, re-armed every 1.5 s), which the
    mod's hold on effects' time did not reach; DAOLib 48fe011 holds and
    advances them with the clock (dao/Timers.h). The same timer under another
    effect is tried here, since it can be put on at will: Lyna is given Walking
    Bomb (20 s of damage over time) and casts it on an ogre with health to
    spare. Nobody attacks the ogre, so each drop of its health is a tick.

    Read five times a second: the ogre's health, the round and whose turn. The
    party's turns are held for some seconds before they are ended, with nobody
    acting: a tick in that wait is the fault the user saw. Wanted: no tick
    while a turn waits on an order, two or three a round of 4 s, the last
    about five rounds after the first.
    """
    begin_spiders(game)
    spawned = game.tool(
        "stage_spawn", {"template": "ogre_default", "point": [9, -2, 0]}
    )
    ogre = (spawned.get("creature") or {}).get("id")
    print(f"{stamp()} spawn an ogre: {ogre} {spawned.get('error', '')}")
    if not ogre:
        return
    print(
        f"{stamp()} give Lyna Walking Bomb:",
        str(
            game.tool("script", {"command": "AddAbility", "args": [HERO, WALKING_BOMB]})
        )[:120],
    )

    def health_of(who: str) -> float | None:
        creature = game.tool("creature", {"id": who})
        creature = creature.get("creature", creature)
        return (creature.get("health") or {}).get("current")

    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    answer = game.tool(
        "order",
        {"action": "ability", "id": HERO, "ability": WALKING_BOMB, "target": ogre},
    )
    after = settle(game, HERO)
    print(
        f"{stamp()} Walking Bomb on the ogre: {str(answer)[:100]}; before {left(state)}; after {left(after)}; "
        f"last refusal {after.get('last_refusal')!r}"
    )
    last = health_of(ogre)
    print(f"{stamp()} the ogre's health: {last}")
    began = time.time()
    ticks: list[dict] = []
    waited: dict = {}  # (round, id) of a party turn held: when it was first seen
    hold_seconds = 6.0
    while time.time() - began < 240.0:
        state = game.state()
        if not state.get("active"):
            print(f"{stamp()} the fight is over")
            break
        current = state.get("current") or {}
        waiting = bool(current.get("yours")) and state.get("phase") == "awaiting order"
        # A turn that ends early moves its creature's clock the rest of the
        # way at once, and what falls due comes on the queue's next update:
        # in the first moments of the next turn. Waiting is a second and a
        # half into a party turn with nobody acting.
        settled = (
            waiting
            and time.time()
            - waited.get((state.get("round"), current.get("id")), time.time())
            >= 1.5
        )
        health = health_of(ogre)
        if health is not None and last is not None and health < last - 0.01:
            tick = {
                "at": round(time.time() - began, 1),
                "round": state.get("round"),
                "turn": current.get("name"),
                "phase": state.get("phase"),
                "waiting": settled,
                "damage": round(last - health, 1),
            }
            ticks.append(tick)
            print(f"{stamp()}   tick {len(ticks)}: {tick}")
        if health is not None:
            last = health
        if waiting:
            key = (state.get("round"), current.get("id"))
            first = waited.setdefault(key, time.time())
            if time.time() - first >= hold_seconds:
                max_health(game, current["id"])
                game.tool("turnbased_end_turn", {})
        if ticks and state.get("round", 0) > ticks[0]["round"] + 9:
            print(f"{stamp()} ten rounds since the first tick: enough")
            break
        time.sleep(0.2)
    by_round: dict = {}
    for tick in ticks:
        by_round[tick["round"]] = by_round.get(tick["round"], 0) + 1
    while_waiting = [t for t in ticks if t["waiting"]]
    print(f"{stamp()} ticks by round: {by_round}")
    print(
        f"{stamp()} ticks in all: {len(ticks)}, damage {sum(t['damage'] for t in ticks):.1f}; "
        f"while a party turn waited on an order ({hold_seconds:.0f} s each, nobody acting): {len(while_waiting)}"
    )
    if while_waiting:
        print(f"{stamp()} TICKS WHILE NOBODY ACTED: {while_waiting}")
    end_fight(game)


DIRTY_FIGHTING = 603  # ABI_base


def scenario_conjure(game: Game) -> None:
    """Three casts in the game's own real time, for DAOLib's conjure probe.

    Run with `--off`: the mod keeps a spell it has not been paid for in
    its gate, so with it on the engine never conjures it. DAOLib's probe
    (its probe.log's `conjure:` lines) logs each sub-action of the command as
    it runs: which is the conjure, its times, IsConjuring, the mana. Wanted
    for holding a spell in its conjure on its caster's clock, so that the
    installed rules_core interrupts it (the user, 2026-10-02: an interrupted
    spell loses its mana; the caster animates mid-cast).

    1. Chain Lightning with only the hall's spiders near: their bites (7 or
       8) are under Lyna's threshold (16), so it is not interrupted.
    2. Chain Lightning again with an ogre at her side: its blows (about 23)
       are over it, so rules_core should interrupt it.
    3. A talent of hers, which does not conjure, for comparison.
    The times of each are printed, to set against the probe's lines.
    """
    cursor = begin_spiders(game, wait=False)
    for spell in KEPT_SPELLS:
        game.tool("script", {"command": "AddAbility", "args": [HERO, spell]})
    mana = lambda: (
        (game.tool("creature", {"id": HERO}).get("creature") or {}).get("mana") or {}
    ).get("current")

    def cast(label: str, ability: int, seconds: float = 9.0) -> None:
        if game.tool("health").get("paused"):
            game.tool("pause", {"paused": False})
        max_health(game, HERO)
        targets = game.hostiles(HERO)
        if not targets:
            print(f"{stamp()} {label}: nobody to cast at")
            return
        target = min(targets, key=lambda c: c.get("distance", 99.0))
        before = mana()
        print(
            f"{stamp()} {label}: ability {ability} at {target.get('name')} {target['id']}, mana {before}"
        )
        answer = game.tool(
            "order",
            {
                "action": "ability",
                "id": HERO,
                "ability": ability,
                "target": target["id"],
            },
        )
        print(f"{stamp()}   ordered: {str(answer)[:120]}")
        began = time.time()
        while time.time() - began < seconds:
            if game.tool("health").get("paused"):
                game.tool("pause", {"paused": False})
            lyna = game.tool("creature", {"id": HERO})
            lyna = lyna.get("creature", lyna)
            print(
                f"{stamp()}   {time.time() - began:4.1f} s: command {lyna.get('command')}, "
                f"mana {((lyna.get('mana') or {}).get('current'))}"
            )
            time.sleep(0.5)

    cast("1. Chain Lightning, spiders only", KEPT_SPELLS[0])
    spawned = game.tool("stage_spawn", {"template": "ogre_default", "point": [6, 1, 0]})
    print(
        f"{stamp()} an ogre at her side: {(spawned.get('creature') or {}).get('id')} {spawned.get('error', '')}"
    )
    time.sleep(4.0)  # let it come and swing
    # Chain Lightning again, its cooldown taken off: Inferno and the other
    # spells that conjure are aimed at the ground, not at a creature.
    game.tool(
        "stage_cooldown",
        {"id": HERO, "ability": KEPT_SPELLS[0], "seconds": 0},
    )
    cast("2. Chain Lightning, the ogre striking", KEPT_SPELLS[0], seconds=10.0)
    # A talent that is used, not a passive one: Dirty Fighting, given.
    game.tool("script", {"command": "AddAbility", "args": [HERO, DIRTY_FIGHTING]})
    cast("3. a talent, Dirty Fighting", DIRTY_FIGHTING, seconds=5.0)
    show_events(game, cursor)
    end_fight(game)


def scenario_lands(game: Game) -> None:
    """A spell held in its conjure through the others' turns, and cast as its time is paid.

    With Settings::engineConjure: Lyna's Chain Lightning, longer than her
    bar, goes to the engine at once and she conjures through the spiders'
    turns (their bites, 7 or 8, are under her threshold of 16, so the game
    does not interrupt it); on her next turn its time is paid, the conjure is
    let complete and the bolt flies. Pictures while she conjures in another's
    turn and as it lands; the log says "held until its time is paid" and "let
    complete". The target's health is read before and after.
    """
    cursor = begin_spiders(game)
    game.tool("script", {"command": "AddAbility", "args": [HERO, KEPT_SPELLS[0]]})

    def health_of(who: str) -> float | None:
        creature = game.tool("creature", {"id": who})
        creature = creature.get("creature", creature)
        return (creature.get("health") or {}).get("current")

    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    targets = game.hostiles(HERO)
    if not targets:
        return
    target = min(targets, key=lambda c: c.get("distance", 99.0))
    before = health_of(target["id"])
    answer = game.tool(
        "order",
        {
            "action": "ability",
            "id": HERO,
            "ability": KEPT_SPELLS[0],
            "target": target["id"],
        },
    )
    print(
        f"{stamp()} Chain Lightning at {target['name']} {target['id']} (health {before}): {str(answer)[:100]}"
    )
    # Another's turn: a picture of her conjuring through it.
    for _ in range(80):
        current = game.state().get("current") or {}
        if current and current.get("id") != HERO:
            break
        time.sleep(0.1)
    time.sleep(1.0)
    picture(game, "lands-1-conjuring-in-anothers-turn")
    # Her next turn: the conjure let complete, the bolt.
    state = wait_turn(game, HERO, seconds=120.0)
    max_health(game, HERO)
    time.sleep(0.6)
    picture(game, "lands-2-her-turn-the-bolt")
    time.sleep(2.0)
    after = health_of(target["id"])
    print(f"{stamp()} {target['name']}'s health: {before} before, {after} after")
    print(f"{stamp()} her turn now: {left(game.state())}")
    show_events(game, cursor, names=("Lyna",))
    end_fight(game)


SPIDER_OVERWHELM = 90133  # ABI_base: MONSTER_SPIDER_OVERWHELM


def scenario_overwhelm(game: Game) -> None:
    """A spider's Overwhelm on Lyna: it should end in its own seconds, and she owe that time.

    Play test 33: an Overwhelm never ended on own clocks, was stopped after
    36 s, and its victim owed 31.8 s. Its steps now move the shared clock too
    (what ends it may be on neither creature), and a stopped one owes
    nothing. On a spider's turn it is ordered to Overwhelm Lyna; the log says
    how long its own effect ran, whether it was stopped, and what Lyna owes
    ("held ... by another's action", or her turn lost).
    """
    cursor = begin_spiders(game)
    spider = None
    for _ in range(600):
        state = game.state()
        current = state.get("current") or {}
        if current and not current.get("yours") and current.get("side") == "enemy":
            spider = current.get("id")
            break
        if current.get("yours") and state.get("phase") == "awaiting order":
            max_health(game, current["id"])
            game.tool("turnbased_end_turn", {})
        time.sleep(0.2)
    if not spider:
        print(f"{stamp()} no spider's turn came")
        return
    answer = game.tool(
        "order",
        {
            "action": "ability",
            "id": spider,
            "ability": SPIDER_OVERWHELM,
            "target": HERO,
        },
    )
    print(f"{stamp()} {spider} told to Overwhelm Lyna: {str(answer)[:140]}")
    began = time.time()
    for _ in range(300):
        state = game.state()
        current = state.get("current") or {}
        if current.get("id") != spider:
            break
        time.sleep(0.2)
    print(
        f"{stamp()} the spider's turn lasted {time.time() - began:.1f} s of the wall's clock"
    )
    state = wait_turn(game, HERO, seconds=180.0)
    print(f"{stamp()} Lyna's turn: {left(state) if state else 'none came'}")
    show_events(game, cursor, names=("Lyna",))
    end_fight(game)


def scenario_bonus(game: Game) -> None:
    """A turn with bonus seconds (what rolled over): a picture of its bar, the bonus glowing at its edge.

    Lyna ends her first turn with all of it left; a second rolls over, and
    her next turn's bar is 5 s, its last second the bonus (the user: "we can
    make the bonus part glow gold edge or something", 2026-10-02).
    """
    cursor = begin_spiders(game)
    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    game.tool("turnbased_end_turn", {})
    time.sleep(1.0)
    state = wait_turn(game, HERO, seconds=120.0)
    if state is None:
        return
    max_health(game, HERO)
    time.sleep(1.2)
    picture(game, "bonus-glow")
    print(f"{stamp()} her turn: {left(state)}")
    show_events(game, cursor, names=("Lyna",))
    end_fight(game)


INFERNO = 10002  # ABI_base: a 30 s area on the ground, a pulse about every 4 s


def scenario_inferno(game: Game) -> None:
    """An Inferno on the ground: its pulses by the rounds, and how many rounds it lasts.

    The user, 2026-10-02: an area's time should count "4s for a full round
    (everyone's turn)... if it counted every turn it'd end too early." An
    area is on the shared clock, which moves 4 s as each round begins, its
    pulses (EVENT_TYPE_AOE_HEARTBEAT, held by DAOLib) with it. Lyna casts
    Inferno where a spider stands; then, five times a second, the spiders'
    health is read with the round and whose turn it is, and a spider struck
    is healed again to keep it there. Wanted: a pulse about once a round (its
    4.05 s), as the round begins; the area gone after about eight rounds (30
    s); none while a turn waits.
    """
    begin_spiders(game)
    game.tool("script", {"command": "AddAbility", "args": [HERO, INFERNO]})
    state = wait_turn(game, HERO)
    if state is None:
        return
    max_health(game, HERO)
    targets = game.hostiles(HERO)
    if not targets:
        return
    target = min(targets, key=lambda c: c.get("distance", 99.0))
    spot = game.tool("creature", {"id": target["id"]})
    spot = spot.get("creature", spot).get("position") or {}
    point = [spot.get("x", 0.0), spot.get("y", 0.0), spot.get("z", 0.0)]
    answer = game.tool(
        "order", {"action": "ability", "id": HERO, "ability": INFERNO, "point": point}
    )
    print(f"{stamp()} Inferno at {point}: {str(answer)[:120]}")
    spiders = [c["id"] for c in targets]

    def health_of(who: str) -> float | None:
        creature = game.tool("creature", {"id": who})
        creature = creature.get("creature", creature)
        return (creature.get("health") or {}).get("current")

    last = {who: health_of(who) for who in spiders}
    began = time.time()
    first_round = None
    pulses: list[dict] = []
    waited: dict = {}
    while time.time() - began < 300.0:
        state = game.state()
        if not state.get("active"):
            print(f"{stamp()} the fight is over")
            break
        current = state.get("current") or {}
        rnd = state.get("round", 0)
        if first_round is None:
            first_round = rnd
        for who in spiders:
            health = health_of(who)
            if (
                health is not None
                and last.get(who) is not None
                and health < last[who] - 0.01
            ):
                pulses.append(
                    {
                        "at": round(time.time() - began, 1),
                        "round": rnd,
                        "turn": current.get("name"),
                        "phase": state.get("phase"),
                        "who": who,
                        "damage": round(last[who] - health, 1),
                    }
                )
                print(f"{stamp()}   struck: {pulses[-1]}")
                max_health(game, who)
                health = health_of(who)
            last[who] = health
        if current.get("yours") and state.get("phase") == "awaiting order":
            key = (rnd, current.get("id"))
            first = waited.setdefault(key, time.time())
            if time.time() - first >= 3.0:
                max_health(game, current["id"])
                game.tool("turnbased_end_turn", {})
        if rnd - (first_round or rnd) >= 12:
            break
        time.sleep(0.2)
    by_round: dict = {}
    for p in pulses:
        by_round[p["round"]] = by_round.get(p["round"], 0) + 1
    print(f"{stamp()} strikes by round: {by_round}")
    rounds = sorted(by_round)
    if rounds:
        print(f"{stamp()} the area struck from round {rounds[0]} to round {rounds[-1]}")
    end_fight(game)


# The Elven Ruins' gas: the round plate before the mirror door, its
# PLC_TRAP_TYPE 207, TRAP_GAS_TOXIC in traps.gda (trap_gas_toxic.ncs: an area
# of 30 s, a damage over time on whoever is in it). Read 2026-10-02 from the
# area's placeables (GetLocalInt in the game) and the table in 2da.erf. The
# plates by the metal doors are fireballs (205), the two small ones flames
# (416).
GAS_PLATE = [-71.52, -0.01, 0.04]
# The ruins' triggers that start a talk or a cutscene: put in the statue room
# by SetPosition, the party stood in bed110tr_statue_room and Tamlen's talk at
# the statue began, which the user had to click through (2026-10-02). Their
# tags from the area (bed110ar_elven_ruins.are).
RUINS_TALKS = (
    "bed110tr_statue_room",
    "bed110tr_mirror_room",
    "bed110tr_mirror",
    "bed110tr_mirror_door",
    "bed110tr_skeleton",
    "bed110tr_bereskarn",
    "bed110tr_duncan",
)


def through_talk(game: Game, seconds: float = 20.0) -> None:
    """Goes through a conversation or cutscene on the screen, as the user would
    have to: a cutscene skipped, a conversation's first reply taken."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        mode = game.tool("health").get("mode")
        if mode == "cutscene":
            print(
                f"{stamp()} a cutscene: {game.tool('dialogue', {'action': 'skip_cutscene'})}"
            )
        elif mode == "dialog":
            shown = game.tool("dialogue", {"action": "look"})
            replies = (shown.get("dialogue") or shown).get("replies") or []
            if replies and (replies[0].get("text") or "").strip():
                game.tool("dialogue", {"action": "choose", "place": 0})
            else:
                game.tool("dialogue", {"action": "continue"})
            print(f"{stamp()} a conversation: gone through a line")
        else:
            return
        time.sleep(1.0)


def scenario_gastrap(game: Game) -> None:
    """The toxic gas the user met in the Elven Ruins, in a fight.

    The user, playing, 2026-10-02: "there's a environmental poison trap that
    keeps hitting the party even during turn-based combat... elven ruins, yes,
    near the undead". It is the statue room's plate (GAS_PLATE), and the
    cloud rises at the statue, the trap's target, not at the plate (the user
    saw it, 2026-10-02). Lyna and Tamlen are put in the room by SetPosition,
    Tamlen by the statue, in the cloud; an ogre is brought, not skeletons,
    which poison may not touch (the user: "not sure if skeletons are affected
    by poison"). On Lyna's turn she walks onto the plate. Then, five times a
    second, everyone's health is read with the round, whose turn and its
    phase; a party turn is held three seconds with nobody acting, then ended.
    A drop on the ogre's turn is a blow, and the victim is healed to keep the
    count clean. Wanted: no drop on a party member while a party turn waits
    on an order, the gas's on its victims' own turns, gone after about eight
    rounds (30 s).
    """
    for tag in RUINS_TALKS:
        found = game.tool("script", {"command": "GetObjectByTag", "args": [tag]})
        trigger = found.get("value")
        if trigger:
            game.tool("script", {"command": "SetObjectActive", "args": [trigger, 0]})
        print(f"{stamp()} off: {tag} {trigger}")
    for who, spot in ((HERO, [-66.0, 1.5, 0.0]), (TAMLEN, [-63.5, -1.0, 0.0])):
        answer = game.tool("script", {"command": "SetPosition", "args": [who, spot, 1]})
        print(f"{stamp()} put {who} at {spot}: {str(answer)[:100]}")
    time.sleep(2.0)
    through_talk(game)
    print(
        f"{stamp()} Lyna at {position_of(game, HERO)}, Tamlen at {position_of(game, TAMLEN)}"
    )
    cursor = game.tool("health").get("cursor", 0)
    enemies: list[str] = []
    for spot in ([-55.0, 0.0, 0.0],):
        spawned = game.tool("stage_spawn", {"template": "ogre_default", "point": spot})
        creature = spawned.get("creature") or {}
        if creature.get("id"):
            enemies.append(creature["id"])
        print(
            f"{stamp()} spawn an ogre at {spot}: {creature.get('id')} {creature.get('name')} "
            f"moved {spawned.get('moved')} {spawned.get('error', '')}"
        )
    began = game.tool(
        "wait_for",
        {"topic": "turnbased.fight_began", "since": cursor, "timeoutMs": 8000},
        timeout=60,
    )
    if not began.get("found"):
        # Put there by SetPosition, nobody has seen anybody yet (seen
        # 2026-10-02): Lyna's attack begins the fight.
        hostiles = game.hostiles(HERO)
        print(
            f"{stamp()} no fight yet; hostiles: {[(c['id'], round(c.get('distance', -1), 1)) for c in hostiles]}"
        )
        if hostiles:
            answer = game.tool(
                "order", {"action": "attack", "id": HERO, "target": hostiles[0]["id"]}
            )
            print(
                f"{stamp()} Lyna attacks: {str(answer)[:200]}; mode {game.tool('health').get('mode')}"
            )
        began = game.tool(
            "wait_for",
            {"topic": "turnbased.fight_began", "since": cursor, "timeoutMs": 20000},
            timeout=60,
        )
    print(f"{stamp()} the fight began: {began.get('found')}")
    state = wait_turn(game, HERO)
    if state is None:
        return
    answer = game.tool("order", {"action": "move", "id": HERO, "point": GAS_PLATE})
    print(f"{stamp()} Lyna onto the plate: {str(answer)[:120]}")
    # The statue's own script answers the plate, and it can talk (bed110ip_statue.ncs).
    time.sleep(2.0)
    through_talk(game)
    print(f"{stamp()} Lyna at {position_of(game, HERO)}")
    party = (HERO, TAMLEN)
    everyone = party + tuple(enemies)

    def read(who: str) -> dict:
        creature = game.tool("creature", {"id": who})
        return creature.get("creature", creature)

    last = {who: (read(who).get("health") or {}).get("current") for who in everyone}
    clock = time.time()
    drops: list[dict] = []
    waited: dict = {}
    seen_round = None
    while time.time() - clock < 360.0:
        state = game.state()
        if not state.get("active"):
            if game.tool("health").get("mode") in ("dialog", "cutscene"):
                through_talk(game)
                continue
            print(f"{stamp()} the fight is over")
            break
        current = state.get("current") or {}
        rnd = state.get("round", 0)
        waiting = bool(current.get("yours")) and state.get("phase") == "awaiting order"
        key = (rnd, current.get("id"))
        settled = waiting and time.time() - waited.get(key, time.time()) >= 1.5
        if rnd != seen_round:
            seen_round = rnd
            for who in everyone:
                creature = read(who)
                kinds = sorted(
                    {str(e.get("type")) for e in creature.get("effects") or []}
                )
                print(
                    f"{stamp()} round {rnd}: {who} {creature.get('name')} effects {kinds}"
                )
        for who in everyone:
            creature = read(who)
            health = (creature.get("health") or {}).get("current")
            if (
                health is not None
                and last.get(who) is not None
                and health < last[who] - 0.01
            ):
                drop = {
                    "at": round(time.time() - clock, 1),
                    "round": rnd,
                    "turn": current.get("id"),
                    "turn_name": current.get("name"),
                    "phase": state.get("phase"),
                    "waiting": settled,
                    "who": who,
                    "damage": round(last[who] - health, 1),
                }
                drops.append(drop)
                print(f"{stamp()}   drop: {drop}")
                # Everyone kept standing, to see the gas through to its end
                # (Lyna fell to it in round 8 when it was let be, 2026-10-02).
                max_health(game, who)
                health = (read(who).get("health") or {}).get("current")
            last[who] = health
        if waiting:
            first = waited.setdefault(key, time.time())
            if time.time() - first >= 3.0:
                game.tool("turnbased_end_turn", {})
        if drops and rnd - drops[0]["round"] >= 12:
            break
        time.sleep(0.2)
    # The party does nothing after Lyna's walk, so the ogre's every drop is
    # the gas's; a party member's on a party turn is the gas's too.
    gas = [d for d in drops if d["who"] in enemies or d["turn"] in party]
    own = [d for d in gas if d["turn"] == d["who"]]
    elsewhere = [d for d in gas if d["turn"] != d["who"]]
    leaks = [d for d in drops if d["waiting"]]
    by_round: dict = {}
    for d in gas:
        by_round[d["round"]] = by_round.get(d["round"], 0) + 1
    print(f"{stamp()} the gas's drops by round: {by_round}")
    print(f"{stamp()} on the victim's own turn: {len(own)}; on another's: {elsewhere}")
    print(f"{stamp()} drops while a party turn waited: {leaks}")
    end_fight(game)


def scenario_regen(game: Game) -> None:
    """Regeneration on each creature's own clock: when health comes back, by whose turn.

    In a fight a creature regains health once each 2 s of its clock
    (DAOLib's dao/Clock.h, UpdateRegeneration), and with own clocks that
    clock moves only as its own turn is spent. Lyna and Tamlen are put at half
    health in the first hall's fight; nobody in the party acts, each party
    turn held a second and a half, then ended. Five times a second their
    health is read with the round, whose turn, its phase and how far into it.
    A spider's bite is a fall; a rise is regeneration. One set back to half
    by this script is not counted. Wanted: each one's rises as its own turn
    ends (its clock run the rest of its 4 s at once: two of them), within a
    moment of it, none further into another's turn.
    """
    begin_spiders(game)
    party = (HERO, TAMLEN)
    names = {HERO: "Lyna", TAMLEN: "Tamlen"}

    def health_of(who: str) -> dict:
        creature = game.tool("creature", {"id": who})
        return creature.get("creature", creature).get("health") or {}

    def half(who: str) -> None:
        most = health_of(who).get("max") or 100.0
        game.tool("stage_health", {"id": who, "value": most / 2})

    for who in party:
        half(who)
        # What each regains, by properties.gda's NonCombatEffect and
        # CombatEffect for health (14 and 28; dao/Clock.h).
        regen = {
            prop: game.tool(
                "script", {"command": "GetCreatureProperty", "args": [who, prop, 1]}
            ).get("value")
            for prop in (14, 28)
        }
        print(
            f"{stamp()} {names[who]}'s health regeneration, out of and in combat: {regen}"
        )
    last = {who: health_of(who).get("current") for who in party}
    clock = time.time()
    first_round = None
    turn_key = None
    turn_began = time.time()
    rises: list[dict] = []
    waited: dict = {}
    while time.time() - clock < 300.0:
        state = game.state()
        if not state.get("active"):
            print(f"{stamp()} the fight is over")
            break
        current = state.get("current") or {}
        rnd = state.get("round", 0)
        if first_round is None:
            first_round = rnd
        key = (rnd, current.get("id"))
        if key != turn_key:
            # Each one's health at each turn's start, with whose turn it is
            # (DAOLib's session asked for it, 2026-10-02: the creature's
            # update tells the regeneration hook whose stats they are).
            at = {names[who]: health_of(who).get("current") for who in party}
            print(f"{stamp()} round {rnd}, {current.get('name')}'s turn begins: {at}")
            turn_key = key
            turn_began = time.time()
        for who in party:
            health = health_of(who)
            now = health.get("current")
            if now is not None and last.get(who) is not None and now > last[who] + 0.01:
                rise = {
                    "round": rnd,
                    "turn": current.get("name"),
                    "own": current.get("id") == who,
                    "phase": state.get("phase"),
                    "into": round(time.time() - turn_began, 1),
                    "who": names[who],
                    "gain": round(now - last[who], 2),
                }
                rises.append(rise)
                print(f"{stamp()}   rise: {rise}")
            if now is not None and now < (health.get("max") or 100.0) * 0.3:
                half(who)
                now = health_of(who).get("current")
            last[who] = now
        if current.get("yours") and state.get("phase") == "awaiting order":
            first = waited.setdefault(key, time.time())
            if time.time() - first >= 1.5:
                game.tool("turnbased_end_turn", {})
        if rnd - (first_round or rnd) >= 6:
            break
        time.sleep(0.2)
    late = [r for r in rises if not r["own"] and r["into"] > 1.0]
    for who in names.values():
        mine = [r for r in rises if r["who"] == who]
        print(
            f"{stamp()} {who}: {len(mine)} rises over {len({r['round'] for r in mine})} rounds"
        )
    print(f"{stamp()} rises more than a second into another's turn: {late}")
    end_fight(game)


SCENARIOS = {
    "regen": scenario_regen,
    "gastrap": scenario_gastrap,
    "inferno": scenario_inferno,
    "bonus": scenario_bonus,
    "overwhelm": scenario_overwhelm,
    "lands": scenario_lands,
    "conjure": scenario_conjure,
    "dot": scenario_dot,
    "stealth": scenario_stealth,
    "hover": scenario_hover,
    "interrupt": scenario_interrupt,
    "look": scenario_look,
    "mixed": scenario_mixed,
    "duel": scenario_duel,
    "smoke": scenario_smoke,
    "stun": scenario_stun,
    "crowd": scenario_crowd,
    "bar": scenario_bar,
    "abilities": scenario_abilities,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scenario", default="smoke", choices=sorted(SCENARIOS))
    parser.add_argument("--owner", default="turn-based-agent")
    parser.add_argument("--eluvian", type=Path, default=ROOT.parent / "DAOEluvianMCP")
    parser.add_argument(
        "--off",
        action="store_true",
        help="the mod off for the run: the game's real time",
    )
    parser.add_argument(
        "--set",
        action="append",
        default=[],
        metavar="SECTION.KEY=VALUE",
        help="another setting for the run, put back to --unset's value after",
    )
    parser.add_argument(
        "--keep", action="store_true", help="leave the game running at the end"
    )
    parser.add_argument(
        "--log", type=Path, default=None, help="where to copy turnbased.log"
    )
    args = parser.parse_args()

    eluvian = args.eluvian
    if not eluvian.exists():
        # From a worktree under .claude\worktrees, the sibling is further up.
        for parent in ROOT.parents:
            if (parent / "DAOEluvianMCP").exists():
                eluvian = parent / "DAOEluvianMCP"
                break
    game = Game(eluvian, args.owner)
    game.off = args.off

    launched = False
    put_back: list[tuple[str, str, str]] = []
    try:
        answer = game.bridge(
            "game_launch",
            {
                "owner": args.owner,
                "logLevel": "debug",
                "load": SAVE,
                "waitSeconds": 240,
            },
        )
        data = answer.get("answer", {})
        if not data.get("launched") or game.tool("health").get("owner") != args.owner:
            print(
                "not launched, or the running game is not this run's:",
                json.dumps(answer)[:600],
            )
            return 2
        launched = True
        print(f"{stamp()} launched into {SAVE}")
        if args.off:
            game.setting("turnbased", "enabled", "false")
            put_back.append(("turnbased", "enabled", "true"))
        for item in args.set:
            name, _, value = item.partition("=")
            section, _, key = name.partition(".")
            game.setting(section, key, value)
        time.sleep(2.5)
        SCENARIOS[args.scenario](game)
    finally:
        if launched:
            for section, key, value in put_back:
                game.setting(section, key, value)
            time.sleep(2.5)
        log = documents() / "BioWare" / "Dragon Age" / "daolib" / "turnbased.log"
        out = (
            args.log
            or ROOT
            / "build"
            / f"timeplay-{args.scenario}-{'off' if args.off else 'time'}-turnbased.log"
        )
        if log.exists():
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(log, out)
            print(f"{stamp()} log copied to {out}")
            summary(out)
        if launched and not args.keep:
            print(
                f"{stamp()} quit:",
                json.dumps(game.bridge("game_quit", {"owner": args.owner}))[:200],
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
