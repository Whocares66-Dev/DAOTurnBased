"""Who is in a fight, swept through an area with no hands on the game.

Through Eluvian, the development bench's MCP server (the sibling checkout
..\\DAOEluvianMCP): its bridge launches the game into a save, and its tools
are called over HTTP in the game. The party travels through the Elven Ruins
of the Dalish origin, and at every fight that begins the turn order
(turnbased.state) is set beside where each creature stands (Eluvian's
`inspect creatures`: its room, whether the party has seen it, whom it
attacks), which is dev/DESIGN.md's "Who is in the fight": a creature in the
order has a room the party is shown or a target in the party, and a hostile
in combat left out of it is in a room never seen. On up to two enemy turns
the camera's creature is asked for too: its room is never one never seen.
Each fight is ended with Eluvian's `stage kill`, a few seconds in.

eluvian-agent's script, taken into this tree at its word (2026-10-01): it
reads this mod's state and calls this mod's tool, so it is the mod's. Its
first run, on ec38eb8: four fights, nothing wrong (dev/TESTING.md).

    python tools\\sweep.py                    # Lyna's autosave 2, the Elven Ruins
    python tools\\sweep.py --owner my-session # the name the game is launched under

It LAUNCHES THE GAME and overwrites autosaves as the party travels (the
user, 2026-10-01: autosaves are no concern), and quits the game at the end.
It refuses to go on in a game another session launched. The object ids are
that save's: the creature the party walks to in room 1579, and the mirror
room's door.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = "http://127.0.0.1:8930"

# Lyna's AutoSave_2, the Elven Ruins.
SAVE = {"character": "Lyna", "kind": "auto", "number": 2}
ROOM_1579 = "0x2ec"
MIRROR_DOOR = "0x1f2"


def judge(order: list[str], creatures: list[dict]) -> dict:
    """The order against the creatures: who is in it wrongly, and who is left out wrongly.

    `order` is the ids of turnbased.state's order; `creatures` are Eluvian's, each with
    id, name, side, combatState, room {number, seen, shown} and attackTarget {id}.
    """
    party = {c["id"] for c in creatures if c["side"] == "party"}
    wrong_in, wrong_out, left_out = [], [], []
    for c in creatures:
        room = c.get("room") or {}
        target = (c.get("attackTarget") or {}).get("id")
        where = (c["id"], c["name"], room.get("number"), room.get("seen"))
        if c["id"] in order:
            if c["side"] != "party" and not (room.get("shown") or target in party):
                wrong_in.append((*where, target))
        elif c["side"] == "hostile" and c.get("combatState") == 1:
            left_out.append(where)
            if room.get("seen") != "never":
                wrong_out.append(where)
    return {
        "enemies": [i for i in order if i not in party],
        "left_out": left_out,
        "wrong_in": wrong_in,
        "wrong_out": wrong_out,
    }


def documents() -> Path:
    """The user's Documents folder, which may not be under the profile (OneDrive)."""
    with winreg.OpenKey(
        winreg.HKEY_CURRENT_USER,
        r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders",
    ) as key:
        value, _ = winreg.QueryValueEx(key, "Personal")
    return Path(winreg.ExpandEnvironmentStrings(value))


class Game:
    """Eluvian: the bridge for the game's process, HTTP for the game."""

    def __init__(self, eluvian: Path, owner: str) -> None:
        self.bridge_script = eluvian / "tools" / "eluvian_bridge.py"
        self.owner = owner

    def bridge(self, tool: str, arguments: dict | None = None) -> dict:
        """One call of a bridge tool: a bridge is started, asked and closed."""
        lines = [
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-06-18"},
            },
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {"name": tool, "arguments": arguments or {}},
            },
        ]
        # stdin stays open until the answer comes: at its end the bridge waits only 30 s
        # for calls still running, and a launch into a save takes longer.
        process = subprocess.Popen(
            [sys.executable, str(self.bridge_script)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        try:
            for line in lines:
                process.stdin.write(json.dumps(line) + "\n")
            process.stdin.flush()
            for line in process.stdout:
                try:
                    message = json.loads(line)
                except ValueError:
                    continue
                if message.get("id") == 2:
                    result = message.get("result", message.get("error"))
                    if isinstance(result, dict) and "structuredContent" in result:
                        return {
                            "isError": result.get("isError", False),
                            "data": result["structuredContent"],
                        }
                    return result
            return {"error": "the bridge ended with no answer"}
        finally:
            process.stdin.close()
            process.wait(timeout=60)

    def tool(
        self, name: str, arguments: dict | None = None, timeout: float = 60
    ) -> dict:
        """One call of a game tool: the answer, or {"error": ...} as the game refused."""
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

    def events(self, since: int) -> list[dict]:
        out = []
        while True:
            with urllib.request.urlopen(
                f"{BASE}/api/events?since={since}&limit=1000", timeout=30
            ) as response:
                page = json.load(response)
            out += page["events"]
            if not page.get("truncated"):
                return out
            since = page["next"]

    def launch(self, save: dict) -> None:
        """Starts the game into a save as this run's own, and refuses to go on in another's."""
        answer = self.bridge(
            "game_launch",
            {
                "owner": self.owner,
                "logLevel": "debug",
                "load": save,
                "waitSeconds": 240,
            },
        )
        data = answer.get("data", {}) if isinstance(answer, dict) else {}
        if not data.get("launched") or self.tool("health").get("owner") != self.owner:
            raise SystemExit(
                f"not launched, or the running game is not this run's: {json.dumps(answer)[:600]}"
            )


class Sweep:
    def __init__(self, game: Game) -> None:
        self.game = game
        self.fights: list[dict] = []

    def survey(self, label: str) -> None:
        """One fight: the order against the rooms, and the camera on up to two enemy turns."""
        tool = self.game.tool
        time.sleep(1.5)
        state = tool("inspect", {"kind": "turnbased.state"})
        order = [o["id"] for o in state.get("order", [])]
        creatures = tool(
            "inspect", {"kind": "creatures", "radius": 70, "includeDead": False}
        ).get("creatures", [])
        by_id = {c["id"]: c for c in creatures}
        party = {c["id"] for c in creatures if c["side"] == "party"}
        found = judge(order, creatures)
        area = (tool("inspect", {"kind": "game"}).get("area") or {}).get("name")
        line = (
            f"FIGHT {label} ({area}): active {state.get('active')}, order {len(order)} "
            f"({len(found['enemies'])} enemies: {[by_id.get(i, {}).get('name', i) for i in found['enemies']]}), "
            f"rooms of the order {sorted({(by_id.get(i, {}).get('room') or {}).get('number') or 0 for i in order})}; "
            f"hostiles in combat left out {found['left_out']}; in the order wrongly {found['wrong_in']}; "
            f"left out wrongly {found['wrong_out']}"
        )
        print(line)
        cameras = []
        for _ in range(2):
            state = tool("inspect", {"kind": "turnbased.state"})
            if not state.get("active"):
                break
            cursor = tool("health")["cursor"]
            if (state.get("current") or {}).get("yours"):
                ended = tool("turnbased_end_turn", {})
                if "error" in ended:
                    print("   end_turn:", ended["error"][:120])
                    break
            started = tool(
                "wait_for",
                {"topic": "turnbased.turn_started", "since": cursor, "timeoutMs": 9000},
                timeout=30,
            )
            if not started.get("found"):
                print(
                    "   no turn started:",
                    {k: started.get(k) for k in ("paused", "note")},
                )
                break
            who = started["event"]["payload"]
            if who.get("id") in party:
                continue
            time.sleep(0.7)
            followed = (tool("camera", {}).get("following") or {}).get("id")
            seen = None
            if followed:
                seen = (
                    tool("inspect", {"kind": "creature", "id": followed}).get("room")
                    or {}
                ).get("seen")
            cameras.append(
                {
                    "turn": who.get("name"),
                    "id": who.get("id"),
                    "camera": followed,
                    "seen": seen,
                }
            )
        print("   enemy turns:", cameras)
        self.fights.append(
            {
                "label": label,
                "area": area,
                "line": line,
                "found": found,
                "cameras": cameras,
            }
        )

    def clear_fight(self) -> None:
        tool = self.game.tool
        for _ in range(25):
            health = tool("health")
            if health.get("mode") != "combat":
                return
            if health.get("paused"):
                tool("game", {"action": "unpause"})
            hostiles = tool(
                "inspect",
                {
                    "kind": "creatures",
                    "side": "hostile",
                    "radius": 70,
                    "includeDead": False,
                },
            )
            for c in hostiles.get("creatures", []):
                if c["combatState"]:
                    tool("stage", {"action": "kill", "id": c["id"]})
            time.sleep(1.5)

    def talk(self) -> None:
        tool = self.game.tool
        for _ in range(60):
            dialogue = tool("inspect", {"kind": "dialogue"})
            if not dialogue.get("active"):
                return
            replies = dialogue.get("replies", [])
            if not replies or not replies[0]["text"]:
                tool("dialogue", {"action": "continue"})
            else:
                tool("dialogue", {"action": "choose", "place": 0})
            time.sleep(1.1)

    def go(self, arguments: dict, label: str, tries: int = 10) -> None:
        """Travels there, surveying and ending each fight on the way."""
        tool = self.game.tool
        for n in range(tries):
            answer = tool("travel", {**arguments, "budgetMs": 60000}, timeout=90)
            reason = answer.get("reason", answer.get("error", ""))
            print(
                f"travel {label} [{n}]: arrived {answer.get('arrived')} {reason!r} distance "
                f"{round(answer.get('distance', -1), 1)} opened {[d.get('id') for d in answer.get('opened', [])]}"
            )
            if answer.get("arrived") or "error" in answer:
                return
            if reason == "a fight began":
                self.survey(f"{label} #{len(self.fights) + 1}")
                self.clear_fight()
            elif reason == "the game is paused":
                tool("game", {"action": "unpause"})
            elif "conversation" in reason:
                self.talk()
            else:
                return

    def wrong(self) -> int:
        """How many things the design forbids were seen."""
        count = 0
        for fight in self.fights:
            count += len(fight["found"]["wrong_in"]) + len(fight["found"]["wrong_out"])
            count += sum(1 for camera in fight["cameras"] if camera["seen"] == "never")
        return count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--eluvian",
        type=Path,
        default=ROOT.parent / "DAOEluvianMCP",
        help="Eluvian's checkout",
    )
    parser.add_argument(
        "--owner",
        default="turn-based-sweep",
        help="the name the game is launched under",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=ROOT / "build" / "sweep.json",
        help="the fights, as JSON",
    )
    arguments = parser.parse_args()

    game = Game(arguments.eluvian, arguments.owner)
    game.launch(SAVE)
    tool = game.tool
    start = tool("health")["cursor"]
    sweep = Sweep(game)
    sweep.go({"target": ROOM_1579}, "to room 1579")
    sweep.go({"target": MIRROR_DOOR, "within": 3.0}, "to the mirror room's door")
    use = tool("order", {"action": "use", "target": MIRROR_DOOR})
    print("use the mirror room's door:", use.get("via"), use.get("error"))
    for _ in range(20):
        if tool("health").get("mode") == "combat":
            sweep.survey(f"the mirror room #{len(sweep.fights) + 1}")
            sweep.clear_fight()
            break
        time.sleep(1.0)

    log = documents() / "BioWare" / "Dragon Age" / "daolib" / "turnbased.log"
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    print("RULE LINES:")
    for line in lines:
        if "who is in a fight" in line or "party's own room" in line:
            print("  ", line[:230])
    troubles = [line for line in lines if "[warn" in line or "[error" in line]
    print("WARNINGS AND ERRORS:")
    for line in troubles:
        print("  ", line[:230])
    saved = [
        e["payload"] for e in game.events(start) if e["topic"] == "eluvian.game.saved"
    ]
    print("saves written:", saved)
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(json.dumps(sweep.fights, indent=1), encoding="utf-8")
    print(game.bridge("game_quit").get("data"))

    wrong = sweep.wrong()
    print(
        f"{len(sweep.fights)} fights; {wrong} wrong; {len(troubles)} warnings or errors in the log"
    )
    return 0 if sweep.fights and wrong == 0 and not troubles else 1


if __name__ == "__main__":
    sys.exit(main())
