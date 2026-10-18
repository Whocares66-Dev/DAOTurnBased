"""The game played through in turn mode, a step at a time, with no hands on it.

GitHub issue 3: play the game from the Dalish ruins on, turn-based, to find
what breaks. An agent runs this a verb at a time and decides what comes next
from what each prints; the fights it plays itself. Everything goes through
Eluvian (the sibling checkout ..\\DAOEluvianMCP): its bridge for the game's
process, its tools over HTTP in the game, as tools/timeplay.py does. Unlike timeplay's scenarios nothing here is staged: no health set, no
creature spawned or killed, since the point is the game as a player meets it.

    python tools\\playthrough.py launch [auto:2 | manual:5]   start, load
    python tools\\playthrough.py status                       mode, area, party, quest, the fight
    python tools\\playthrough.py look [radius]                creatures, things to use, exits
    python tools\\playthrough.py go 0x1f0 | x,y,z | area:<tag> | exit:<id> | map:<pin's tag>
    python tools\\playthrough.py rest [seconds]              wait for health and stamina to come back
    python tools\\playthrough.py use 0x1f0 [objectAction]
    python tools\\playthrough.py loot 0x3ec [0x1fd ...]         walk there, open, take all
    python tools\\playthrough.py talk [place]                 read the conversation, or answer it
    python tools\\playthrough.py fight [minutes]              play the fight to its end
    python tools\\playthrough.py save <name>                  a new manual save
    python tools\\playthrough.py call <tool> [json]           any tool, raw
    python tools\\playthrough.py quit

Each run appends what it printed to build\\playthrough\\transcript.log.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = os.environ.get("PLAYTHROUGH_BASE", "http://127.0.0.1:8930")
BRIDGE = ROOT.parent / "DAOEluvianMCP" / "tools" / "eluvian_bridge.py"
OWNER = "turn-based-agent"
# The one the player controls: the hero, but for stretches the story gives to others (Fort Drakon's rescue).
HERO = os.environ.get("PLAYTHROUGH_HERO", "0x82")
OUT = ROOT / "build" / "playthrough"

# How long, in seconds, a screen the driver works is left up before it is
# acted on (a loot panel before Take All, a conversation's replies before one
# is chosen, the shop before a sale): nothing needs it but whoever watches.
# PLAYTHROUGH_PACE=0 for none.
PACE = float(os.environ.get("PLAYTHROUGH_PACE", "1.2"))

# Enemies to strike first, by a word of their name: those that cast.
CASTERS = ("Emissary", "Mage", "Shaman", "Arcane", "Witch")

# The game's own lesser health poultice: its ability and the item's tag.
POULTICE = (200010, "gen_im_qck_health_101")


def say(*parts: object, shown: bool = True) -> None:
    line = " ".join(str(p) for p in parts)
    if shown:
        print(line)
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "transcript.log").open("a", encoding="utf-8") as log:
        log.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {line}\n")


# A fight's every order goes to the transcript; the screen gets a line a round
# and whatever went wrong, unless the fight is asked for in full (`fight 10
# full`): nine wolves printed two hundred lines.
VERBOSE = False


def note(*parts: object) -> None:
    say(*parts, shown=VERBOSE)


def bridge(tool: str, arguments: dict | None = None) -> dict:
    done = subprocess.run(
        [sys.executable, str(BRIDGE), "--call", tool, json.dumps(arguments or {})],
        capture_output=True,
        text=True,
        timeout=400,
        check=False,
    )
    try:
        return json.loads(done.stdout)
    except ValueError:
        return {"isError": True, "answer": {"error": done.stdout[-400:]}}


def tool(name: str, arguments: dict | None = None, timeout: float = 90) -> dict:
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
    except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
        return {"error": str(error)}


def state() -> dict:
    answer = tool("turnbased_state")
    return answer.get("state", answer)


def creatures(
    side: str | None = None, radius: float | None = None, near: str | None = None
) -> list[dict]:
    arguments: dict = {"includeDead": False}
    if side:
        arguments["side"] = side
    if radius:
        arguments["radius"] = radius
    if near:
        arguments["near"] = near
    found = [
        c
        for c in tool("creatures", arguments).get("creatures", [])
        if not c.get("dead")
    ]
    if found or side != "hostile":
        return found
    # Fort Drakon's rescue, 2026-10-06: with the hero a captive outside the party, Eluvian's `side` called
    # the guards fighting Leliana and Dog neutral, and the driver passed every turn with "no hostile left".
    # The fight's own order knows who the enemies are.
    foes = {
        o.get("id")
        for o in state().get("order", [])
        if o.get("side") == "enemy" and not o.get("down")
    }
    if not foes:
        return []
    del arguments["side"]
    return [
        c
        for c in tool("creatures", arguments).get("creatures", [])
        if c.get("id") in foes and not c.get("dead")
    ]


def health_of(c: dict) -> str:
    h = c.get("health") or {}
    return f"{h.get('current', 0):.0f}/{h.get('max', 0):.0f}"


def where(c: dict) -> str:
    p = c.get("position") or {}
    if isinstance(p, dict):
        return f"({p.get('x', 0):.1f}, {p.get('y', 0):.1f}, {p.get('z', 0):.1f})"
    return "(" + ", ".join(f"{v:.1f}" for v in p) + ")"


# --- the verbs -------------------------------------------------------------


def launch(save: str) -> int:
    kind, _, number = save.partition(":")
    answer = bridge(
        "game_launch",
        {
            "owner": OWNER,
            "logLevel": "debug",
            "load": {
                "character": "Lyna",
                "kind": kind,
                "number": int(number),
                "forceLoad": True,
            },
            "waitSeconds": 240,
        },
    )
    say("launch:", json.dumps(answer.get("answer"))[:1500])
    return 1 if answer.get("isError") else 0


def status(quests: bool = False) -> int:
    health = tool("health")
    if "error" in health:
        say("the game does not answer:", health["error"])
        return 1
    game = tool("state")
    say(
        f"mode {health.get('mode')}, paused {health.get('paused')}, state {health.get('state')}, "
        f"area {(game.get('area') or {}).get('tag')}"
    )
    for c in creatures("party"):
        mana = c.get("manaStamina") or {}
        doing = (c.get("command") or {}).get("current")
        say(
            f"  party {c['id']} {c['name']}: health {health_of(c)}, "
            f"mana or stamina {mana.get('current', 0):.0f}/{mana.get('max', 0):.0f}, "
            f"at {where(c)}{', doing ' + json.dumps(doing) if doing else ''}"
        )
    journal = tool("journal") if quests else {}
    active = (journal.get("active") or {}).get("plot")
    for quest in journal.get("quests") or []:
        if quest.get("completed"):
            continue
        text = " / ".join((quest.get("description") or "").splitlines())
        say(
            f"  quest{' *' if quest.get('plot') == active else ''} {quest.get('plot')}: {quest.get('name')}: {text[:260]}"
        )
    s = state()
    if s.get("active"):
        current = s.get("current") or {}
        say(
            f"  fight: round {s.get('round')}, {current.get('name')} ({current.get('side')}), {s.get('phase')}, "
            f"{s.get('time_left_ms', 0) / 1000:.2f} of {s.get('time_ms', 0) / 1000:.2f} s"
        )
    return 0


def look(radius: float) -> int:
    for c in creatures(radius=radius):
        if c.get("side") == "party":
            continue
        say(
            f"  {c.get('side', '?'):8} {c['id']} {c['name']} [{c.get('tag')}] health {health_of(c)}, "
            f"{c.get('distance', 0):.1f} m at {where(c)}, room seen {((c.get('room') or {}).get('seen'))}"
        )
    things = tool("placeables", {"radius": radius})
    for p in things.get("placeables", []):
        if not p.get("interactive") and not p.get("name"):
            continue
        leads = (p.get("leadsTo") or {}).get("area")
        say(
            f"  thing    {p['id']} {p.get('name') or '-'} [{p.get('tag')}] state {p.get('state')}"
            f"{'' if p.get('interactive') else ', not to be used'}{', pick ' + str((p.get('lock') or {}).get('pickLevel')) if p.get('state') == 1 else ''}"
            f"{', to ' + leads if leads else ''}, {p.get('distance', 0):.1f} m at {where(p)}"
        )
    exits = tool("exits")
    for e in exits.get("exits", []):
        say(
            f"  exit     {e['id']} {e.get('name')} [{e.get('tag')}] {e.get('kind')} to {(e.get('leadsTo') or {}).get('area')}, "
            f"{e.get('distance', 0):.1f} m{'' if e.get('active') else ', not active'}"
        )
    return 0


def by_map(pin: str) -> int:
    """Across the world map to a pin (its tag), from an area's way out or with the map already up.

    Walks to the nearest real way to the map (not a "Return Path", which is a
    sign and leads nowhere), answers the game's "Gather your party" with Yes,
    and travels. With the map up it first sets the map movie's own
    closed-by-travel flag, as a click on a pin does: without it the map
    closes as if the player had backed out, and at camp that sends the party
    chosen back to their places while the hero leaves alone (Eluvian's issue
    30, found 2026-10-03: four trips from camp to Redcliffe arrived alone,
    the game's own click and this flag both arrived whole).
    """
    for _ in range(3):
        if tool("gui").get("fullScreen") == "WorldMap":
            tool(
                "movie",
                {
                    "screen": "WorldMap",
                    "set": "WorldMap.m_bClosedByTravel",
                    "value": True,
                },
            )
            answer = tool("travel", {"to": pin, "budgetMs": 300000}, timeout=360)
            say(
                f"  by the map to {pin}: {answer.get('arrived')} {(answer.get('reason') or '')[:140]}"
            )
            break
        if tool("health").get("mode") == "partypicker":
            say("  the party picker is up: choose the party first (party <names>)")
            return 1
        ways = [
            e
            for e in tool("exits").get("exits", [])
            if (e.get("leadsTo") or {}).get("area") == "world_map"
            and e.get("active")
            and "blocker" not in (e.get("tag") or "")
        ]
        if not ways:
            say("  no way to the world map from here")
            return 1
        walk = tool(
            "travel",
            {"target": ways[0]["id"], "within": 4, "budgetMs": 240000},
            timeout=300,
        )
        if tool("gui").get("popup"):
            # Walking into the way out asked the game's own question already.
            tool("press", {"button": "Yes"})
            time.sleep(5)
            continue
        if tool("health").get("mode") != "explore":
            say(
                f"  the walk to the map's way out stopped: {(walk.get('reason') or '')[:120]}"
            )
            return 1
        tool("order", {"action": "use", "id": HERO, "target": ways[0]["id"]})
        time.sleep(4)
        if tool("gui").get("popup"):
            tool("press", {"button": "Yes"})
            time.sleep(5)
    for _ in range(60):
        if tool("health").get("mode") in ("explore", "dialog", "combat"):
            break
        time.sleep(1)
    time.sleep(1.5)
    return status()


def go(target: str) -> int:
    arguments: dict = {"budgetMs": 120000}
    if target.startswith("map:"):
        return by_map(target[4:])
    if target.startswith("area:"):
        arguments["area"] = target[5:]
    elif target.startswith("exit:"):
        arguments["exit"] = target[5:]
    elif target.startswith("0x"):
        arguments["target"] = target
    else:
        arguments["point"] = [float(v) for v in target.split(",")]
    answer = tool("travel", arguments, timeout=180)
    answer.pop("steps", None)
    say("travel:", json.dumps(answer)[:900])
    if tool("gui").get("popup"):
        # An area's way out can ask "Gather your party and venture forth?" (the
        # Brecilian forest's, 2026-10-05): Yes, as by_map answers it.
        say("  a popup asks: Yes")
        tool("press", {"button": "Yes"})
        time.sleep(5)
    return status()


def use(target: str, action: str | None) -> int:
    arguments: dict = {"action": "use", "target": target}
    if action:
        arguments["via"] = "command"
        arguments["objectAction"] = action
    answer = tool("order", arguments)
    say("use:", json.dumps(answer)[:600])
    cursor = answer.get("cursor")
    if cursor is not None:
        end = tool(
            "wait_for",
            {
                "topic": "eluvian.command.ended",
                "match": {"type": "UseObject"},
                "since": cursor,
                "timeoutMs": 15000,
            },
        )
        say("  ended:", json.dumps((end.get("event") or {}).get("payload", end))[:300])
    return 0


def loot(target: str) -> int:
    """Walks to a body or a chest, opens it and takes everything, as Take All does.

    Eluvian's own `loot` (its issue 1, deployed 2026-10-03): it walks, uses the
    thing, and presses the panel's Take All. Until then this sent the panel's
    messages itself through `input` ("GUI.SelectedChar.AcquireAllItems <id>").

    In one call the panel was up for a frame, which a viewer never sees (the
    user, 2026-10-03: "taking a little bit more time would give the viewer or
    reviewer some idea of what's happening"). Eluvian's loot lingers on the
    panel itself since its issue 14 (`showMs`); PACE is how long.
    """
    answer = tool("loot", {"target": target, "showMs": int(PACE * 1000)}, timeout=90)
    if not (answer.get("looted") or answer.get("empty")) and tool("gui").get("loot"):
        # The walk there opened it (a chest the rogue picked, pick 10; a small
        # crate in the way): its panel came up as the walk ended, which the
        # tool took for the game paused or its own use refused (the tower,
        # 2026-10-03; told to Eluvian's session). The panel is there: take.
        answer = tool("loot", {"showMs": int(PACE * 1000)}, timeout=90)
    walk_failed = any(
        w in str(answer.get("reason") or "")
        for w in ("INVALID_PATH", "no walk there", "the time ran out")
    )
    if (
        not (answer.get("looted") or answer.get("empty"))
        and walk_failed
        and tool("health").get("mode") == "explore"
    ):
        # The tool's walk to the thing failed where the game's own click on
        # it walks up and opens it: the high dragon's remains on their slope,
        # a sarcophagus in the Brecilian ruins (2026-10-05; told to Eluvian's
        # session). The click, then the panel it opens.
        tool("order", {"action": "use", "id": HERO, "target": target}, timeout=60)
        for _ in range(40):
            time.sleep(0.5)
            if tool("gui").get("loot"):
                answer = tool("loot", {"showMs": int(PACE * 1000)}, timeout=90)
                break
            if tool("health").get("mode") not in ("explore", "gui"):
                break
    if "error" in answer:
        say(f"  loot {target}: {answer['error']}")
        return 1
    names = [
        f"{i.get('name')}{' x' + str(i['stack']) if i.get('stack', 1) > 1 else ''}"
        for i in answer.get("taken") or []
    ]
    left = [i.get("name") for i in answer.get("left") or []]
    say(
        f"  loot {target} {(answer.get('container') or {}).get('name', '')}: "
        + ("empty" if answer.get("empty") else f"took {names}")
        + (f", left {left}" if left else "")
        + (", the pack is full" if answer.get("full") else "")
        + (
            ""
            if answer.get("looted") or answer.get("empty")
            else f"; not looted: {answer.get('reason')}"
        )
        + ("" if answer.get("closed", True) else "; the panel is still up")
    )
    return 0 if answer.get("looted") or answer.get("empty") else 1


def shop(orders: list[str]) -> int:
    """Buys and sells with a merchant's Store screen up: `sell:0x3e7`, `buy:0x1d71:8`, `close`.

    Eluvian's `store` and its `gui`'s `store` (its issue 9, deployed
    2026-10-03; before it this sent store.gfx's own messages through `input`).
    With no orders it lists the stock with each price and the pack with what
    each would fetch, in copper.
    """
    gui = tool("gui")
    if gui.get("fullScreen") != "Store":
        say("  shop: no Store screen is up (talk to the merchant first)")
        return 1
    if not orders:
        store = gui.get("store") or {}
        for i in store.get("stock", []):
            say(
                f"  sells {i['id']} {i['name']} x{i.get('stack', 1)} for {i.get('price')}"
            )
        for i in store.get("pack", []):
            if i.get("sellsFor"):
                say(
                    f"  buys  {i['id']} {i['name']} x{i.get('stack', 1)} for {i.get('sellsFor')}"
                )
        say(f"  money {store.get('money')}")
        return 0
    for order in orders:
        if order == "close":
            tool("input", {"message": "GUI.HideFullScreenGUI"})
            time.sleep(0.5)
            say(f"  shop: closed, mode {tool('health').get('mode')}")
            continue
        what, item, *count = order.split(":")
        n = int(count[0]) if count else 1
        answer = tool("store", {"action": what, "item": item, "count": n})
        money = answer.get("money") or {}
        say(
            f"  {what} {item} x{n}: money {money.get('before')} -> {money.get('after')}"
            + ("" if answer.get("taken", True) else " (not taken)")
            + (f" [{answer['error']}]" if "error" in answer else "")
        )
        # A beat between two, for whoever watches.
        time.sleep(max(PACE / 2, 0.3))
    return 0


# What `lootall` leaves alone: ways out, signs, and what a use would start
# rather than open.
NOT_LOOT = ("_to_", "_sign", "door", "gate", "trap", "plate", "statue", "lever")


def recover(seconds: float) -> int:
    """Stands until the party has its breath back, or `seconds` have gone.

    Health and stamina come back by themselves out of a fight. Walking on
    at a tenth of the party's stamina into the next room cost the castle's
    main floor once (2026-10-03: seven corpses on a party that had just
    fought two fights).
    """
    began = time.time()
    worst = 0.0
    while time.time() - began < seconds:
        if tool("health").get("mode") != "explore":
            say("  rest: stopped, the game is not exploring")
            return 1
        worst = 1.0
        for c in creatures("party"):
            health = c.get("health") or {}
            mana = c.get("manaStamina") or {}
            for pool in (health, mana):
                if pool.get("max"):
                    worst = min(worst, pool.get("current", 0) / pool["max"])
        if worst >= 0.9:
            break
        time.sleep(3.0)
    say(
        f"  rested {time.time() - began:.0f} s: the lowest of the party's health and stamina is at {worst:.0%}"
    )
    return 0


def lootall(radius: float) -> int:
    """Loots every body and container within `radius`, nearest first; herbs are picked."""
    things = tool("placeables", {"radius": radius}).get("placeables", [])
    worst = 0
    for p in things:
        tag = (p.get("tag") or "").lower()
        if (
            not p.get("interactive")
            or p.get("leadsTo")
            or any(w in tag for w in NOT_LOOT)
        ):
            continue
        # The panel of the one before takes a moment to go (mode gui): wait
        # for the world, and stop only if it stays away (a fight, a talk).
        for _ in range(16):
            mode = tool("health").get("mode")
            if mode == "explore":
                break
            time.sleep(0.25)
        if mode != "explore":
            say(f"  lootall: stopped, the game is in mode {mode}")
            return 1
        if "autoloot" in tag:
            # A herb or a flower: a use picks it, with no panel.
            tool("travel", {"target": p["id"], "budgetMs": 45000}, timeout=60)
            tool("order", {"action": "use", "target": p["id"]})
            time.sleep(1.5)
            say(f"  picked {p['id']} {p.get('name')}")
            continue
        if p.get("state") == 1 and (p.get("lock") or {}).get("pickLevel", 0) > 1:
            # Locked: tried all the same, since the one controlled may pick it
            # (Lyna opens a 10, not a 20), and not counted against the sweep.
            loot(p["id"])
            continue
        worst = max(worst, loot(p["id"]))
    return worst


def pick_party(names: list[str]) -> int:
    """Chooses the party in the picker that is up: `names` beside the hero, then accepts.

    Eluvian has no tool for the picker yet (its issue 25), so this is the
    game's own interface: the cursor held at a pixel of the picker's scene
    (`pointer`), `PartyPicker.MouseOverName` read for who stands there, and the
    key message a click there sends. Found at Sten's recruitment, 2026-10-03.
    """

    def chosen() -> list[str]:
        paths = [f"PartyPicker.SelectedPartyMember.{n}.Name" for n in range(4)]
        values = tool("data", {"paths": paths}).get("values", [])
        return [v.get("text") for v in values if v.get("found")]

    def under() -> str | None:
        values = tool("data", {"paths": ["PartyPicker.MouseOverName"]}).get(
            "values", []
        )
        return next((v.get("text") for v in values if v.get("found")), None)

    if tool("health").get("mode") != "partypicker":
        say("  party: the picker is not up")
        return 1
    client = tool("window").get("client") or {}
    width, height = client.get("width", 1600), client.get("height", 1000)
    have = chosen()
    # Those not wanted leave first: a portrait's own number, the hero's never.
    for place in range(len(have) - 1, 0, -1):
        if have[place] not in names:
            tool(
                "input", {"message": f"GameModeController.UnselectPartyMember {place}"}
            )
            time.sleep(0.4)
    where_: dict[str, tuple[int, int]] = {}
    for fy in (0.45, 0.6, 0.5, 0.7):
        for i in range(1, 32):
            if all(n in where_ for n in names):
                break
            x, y = int(width * i / 32), int(height * fy)
            tool("pointer", {"pixel": [x, y], "seconds": 2})
            time.sleep(0.3)
            name = under()
            if name and name not in where_:
                where_[name] = (x, y)
    for name in names:
        if name in chosen():
            continue
        if name not in where_:
            say(
                f"  party: {name} is not in the picker's scene (found {sorted(where_)})"
            )
            continue
        tool("pointer", {"pixel": list(where_[name]), "seconds": 4})
        time.sleep(0.35)
        if under() == name:
            tool("input", {"binding": "SelectPartyMemberUnderCursorMessage"})
            time.sleep(PACE)
    tool("pointer", {"release": True})
    have = chosen()
    say(f"  party: {have}")
    if not all(n in have for n in names):
        return 1
    tool("input", {"message": "GameModeController.SubmitPartyChanges"})
    return 0


def talk(place: int | None) -> int:
    """Reads the conversation; with `place`, answers it first. Runs on to the next real choice.

    One call of Eluvian's dialogue with `until: "choice"` (its issue 17,
    deployed 2026-10-03): it answers the lines spoken on the way and the next
    replies, or that the conversation ended and into what. Before it this was
    a loop of reads, continues and waits. Lines are cut short (`skipLines`),
    as a player skipping through; cutscenes are skipped.
    """
    arguments: dict = {
        "action": "continue",
        "until": "choice",
        "skipLines": True,
        "budgetMs": 120000,
    }
    if place is not None:
        # The replies stay up a beat before one is taken, for whoever watches.
        time.sleep(PACE)
        arguments.update({"action": "choose", "place": place})
    for _ in range(12):
        answer = tool("dialogue", arguments, timeout=150)
        for line in answer.get("lines") or []:
            say(f"  {line.get('speaker') or ''}: {line.get('text') or ''}")
        replies = answer.get("replies") or []
        if replies:
            for r in replies:
                say(f"    [{r.get('place')}] {r.get('text')} {r.get('icon') or ''}")
            return 0
        mode = answer.get("mode") or tool("health").get("mode")
        if mode in ("cutscene", "movie"):
            say("  (cutscene: skipping)")
            tool("dialogue", {"action": "skip_cutscene"})
            time.sleep(1.5)
        elif mode != "dialog":
            say(
                f"  the conversation is over: mode {mode}"
                + (f" [{answer['error']}]" if "error" in answer else "")
            )
            return 0
        # Still (or again) in a conversation, with nothing to choose yet: on.
        arguments = {
            "action": "continue",
            "until": "choice",
            "skipLines": True,
            "budgetMs": 120000,
        }
    say("  still in the conversation after a dozen runs to a choice")
    return 1


def save(name: str) -> int:
    answer = tool("saves", {"action": "save", "name": name})
    say("save:", json.dumps(answer)[:400])
    cursor = answer.get("cursor")
    if cursor is None:
        return 1
    done = tool(
        "wait_for", {"topic": "eluvian.game.saved", "since": cursor, "timeoutMs": 30000}
    )
    say("  saved:", json.dumps((done.get("event") or {}).get("payload", done))[:300])
    return 0 if done.get("found") else 1


# --- the fight -------------------------------------------------------------


# What says an order has come to something, on the mod's bus.
AFTER_AN_ORDER = [
    "turnbased.awaiting_order",
    "turnbased.turn_ended",
    "turnbased.fight_over",
    "turnbased.refused",
    "turnbased.action_pending",
]


def settle(who: str, cursor: int | None = None, seconds: float = 16.0) -> dict:
    """Waits until the turn waits on an order again, or is no longer `who`'s; answers the state then.

    From the `cursor` of the order just given, by the mod's own events (its
    `awaiting_order`, issue 7): one wait in place of the state read seven times
    a second. With no cursor, or a mod without the event, the old way.
    """
    deadline = time.time() + seconds
    since = cursor
    first = True
    while since is not None and time.time() < deadline:
        # An order that did nothing brings no event: look at the state after a
        # moment of silence and take a turn still waiting for its answer.
        timeout = 2500 if first else int(max(200, (deadline - time.time()) * 1000))
        got = tool(
            "wait_for",
            {"topic": AFTER_AN_ORDER, "since": since, "timeoutMs": timeout},
            timeout=30,
        )
        if not got.get("found"):
            s = state()
            current = (s.get("current") or {}).get("id")
            if (
                not s.get("active")
                or current != who
                or s.get("phase") == "awaiting order"
            ):
                return s
            first = False
            continue
        since = got.get("next", since)
        payload = (got.get("event") or {}).get("payload") or {}
        kind = payload.get("event")
        if kind == "fight_over":
            break
        # Its own action kept for its next turn is an answer too: the bar is
        # spent and the turn waits for End, with no awaiting_order to say so
        # (waiting on for one cost five seconds a turn at the tower's door).
        if payload.get("id") == who and kind in (
            "awaiting_order",
            "turn_ended",
            "refused",
            "action_pending",
        ):
            break
        # Another's event: on to the next.
    else:
        s = state()
        while time.time() < deadline:
            s = state()
            current = (s.get("current") or {}).get("id")
            if (
                not s.get("active")
                or current != who
                or s.get("phase") == "awaiting order"
            ):
                return s
            time.sleep(0.15)
        return s
    return state()


def left(s: dict) -> str:
    return f"{s.get('time_left_ms', 0) / 1000:.2f}/{s.get('time_ms', 0) / 1000:.2f} s"


# What the party's talents and spells are for, by ability id: whom each is
# aimed at and from how far. One not listed here is never used by `fight`.
#   enemy   on the turn's target, within `reach` meters (0: any range); with
#           `clear`, only with no friend within that many meters of the
#           target (a cone or a blast: Merrill's Flame Blast burned Lyna down
#           beside the genlock it was aimed at)
#   mode    a sustained ability, turned on once a fight and left on
#   friend  on whoever of the party is most hurt, under `under` of its health
#   friends on the caster, for all, when `count` of the party are under `under`
ABILITIES: dict[int, dict] = {
    603: {"name": "Dirty Fighting", "use": "enemy", "reach": 3.0},
    3044: {"name": "Dual-Weapon Sweep", "use": "enemy", "reach": 3.0},
    9: {"name": "Riposte", "use": "enemy", "reach": 3.0},
    803: {"name": "Pinning Shot", "use": "enemy", "reach": 0},
    3071: {"name": "Rapid Shot", "use": "mode"},
    # Alistair's at level 5 (the tower's first floor): the enemies come to
    # the shield and not to the mage.
    808: {"name": "Threaten", "use": "mode"},
    704: {"name": "Shield Defense", "use": "mode"},
    # Sten's, from Lothering on (his Threaten is left off: one shield draws
    # the enemy, and it is not the one in a borrowed chainmail).
    718: {"name": "Powerful Swings", "use": "mode"},
    617: {"name": "Shield Bash", "use": "enemy", "reach": 3.0},
    1: {"name": "Shield Pummel", "use": "enemy", "reach": 3.0},
    3028: {"name": "Mighty Blow", "use": "enemy", "reach": 3.5},
    3024: {"name": "Pommel Strike", "use": "enemy", "reach": 3.5},
    3025: {"name": "Sunder Arms", "use": "enemy", "reach": 3.5},
    200254: {"name": "Arcane Bolt", "use": "enemy", "reach": 0},
    11106: {"name": "Weakness", "use": "enemy", "reach": 0},
    10001: {
        "name": "Flame Blast",
        "use": "enemy",
        "reach": 6.0,
        "clear": 6.0,
        "fire": True,
    },
    10204: {"name": "Flaming Weapons", "use": "mode"},
    14001: {"name": "Lightning", "use": "enemy", "reach": 0},
    200255: {"name": "Arcane Shield", "use": "mode"},
    3072: {"name": "Shattering Shot", "use": "enemy", "reach": 0},
    805: {"name": "Crippling Shot", "use": "enemy", "reach": 0},
    # Morrigan's, from Flemeth's hut on (her Mind Blast is a burst about
    # herself and her Spider Shape a change of all she can do: neither yet).
    13001: {"name": "Winter's Grasp", "use": "enemy", "reach": 0},
    15002: {"name": "Drain Life", "use": "enemy", "reach": 0},
    11108: {"name": "Horror", "use": "enemy", "reach": 0},
    11112: {"name": "Vulnerability Hex", "use": "enemy", "reach": 0},
    11115: {"name": "Disorient", "use": "enemy", "reach": 0},
    11007: {"name": "Frost Weapons", "use": "mode"},
    # Wynne's, from the Circle Tower on: the party's first healer.
    10509: {"name": "Group Heal", "use": "friends", "under": 0.6, "count": 2},
    10104: {"name": "Heal", "use": "friend", "under": 0.6},
    10210: {"name": "Regeneration", "use": "friend", "under": 0.75},
    12004: {"name": "Stonefist", "use": "enemy", "reach": 0},
    11002: {"name": "Rock Armor", "use": "mode"},
    # The Fade's spirit form (skill 100084, taken on oneself): its spells are
    # Winter's Grasp and Regeneration above, and this.
    # On a caster first: the Darkspawn Invasion's emissary killed her in
    # every form while the prison went on a hurlock.
    11123: {
        "name": "Crushing Prison",
        "use": "enemy",
        "reach": 0,
        "prefer": ("Emissary", "Mage", "Horror"),
    },
    # The Fade's golem (skill 100080).
    90055: {"name": "Slam", "use": "enemy", "reach": 3.0},
    # The Fade's burning man (skill 100083): fire she is immune to in the
    # form, so only when nobody else of the party is there to be burnt.
    # A burst on the ground: ordered at a point (at a creature the engine
    # refused it 41 times in one fight, COMMAND_FAILED_PATH_ACTION_REQUIRED).
    10003: {
        "name": "Fireball",
        "use": "enemy",
        "reach": 0,
        "alone": True,
        "ground": True,
        "fire": True,
    },
    # The mabari's, from the highway on (its Growl and Dread Howl are bursts
    # about itself: not yet).
    90016: {"name": "Charge", "use": "enemy", "reach": 12.0},
}


def apart(a: dict, b: dict) -> float:
    """Meters between two creatures as `creatures` gives their positions."""
    p, q = a.get("position") or {}, b.get("position") or {}
    return sum((p.get(k, 0) - q.get(k, 0)) ** 2 for k in "xyz") ** 0.5


# Whom fire does nothing to, by name: the burning man's form is left when
# only these stand (Lyna burnt two rage demons for twelve rounds and died of
# the mages behind them, the Mages Asunder, 2026-10-03).
FIREPROOF = ("Rage", "Burning", "Fire", "Flaming")
BURNING_MAN = 100083
BURNING_MAN_LOOKS = 28


def turning() -> list[str]:
    """Uldred's captive mages with the light of the turning on them now."""
    lit = []
    for c in tool("creatures", {"radius": 60}).get("creatures", []):
        tag = c.get("tag") or ""
        if c.get("dead") or not (
            tag.startswith("cir240cr_mage") or tag == "cir240cr_irving"
        ):
            continue
        effects = tool("creature", {"id": c["id"]}).get("effects") or []
        if any(e.get("type") == "EFFECT_TYPE_VISUAL_EFFECT" for e in effects):
            lit.append(c["id"])
    return lit


def leaves_the_fire(who: str, targets: list[dict]) -> bool:
    """In the burning man's form with only the fireproof left: leave it."""
    if not all(any(word in t.get("name", "") for word in FIREPROOF) for t in targets):
        return False
    looks = tool("creature", {"id": who}).get("appearance")
    if looks != BURNING_MAN_LOOKS:
        return False
    tool(
        "order", {"action": "ability", "id": who, "ability": BURNING_MAN, "target": who}
    )
    return True


# Coverage (the user, 2026-10-04: "Try to exercise different party members,
# use different talents, etc. We're trying to get broad coverage"). The table
# above names 36 abilities; the party knew 53, and the 19 the table had of
# those were all that were ever used. So an ability the table does not name is
# used by what the game says of it (its target type and range), and of the
# ones ready the least used so far comes first; the count is kept across runs.
COVERAGE = OUT / "coverage.json"
# Not to be tried blind: Revival wants a fallen friend (raise_fallen's). And a
# shape, once covered: turned on as a mode, Morrigan stayed a bear through
# four fights at Caridin's Cross (2026-10-04; the abilities list has no word
# for a mode that is on), casting nothing, with Heal just learnt; the game
# opens no level-up screen for her in it either. Bear and spider were used
# seven times each by then, and their own (Slam, Rage, Web, Poison Spit).
# Captivating Song (1000) binds its singer in the game itself, and until issue
# 51 is built the singer's turns run out before anything can switch it off:
# Lyna lost 90 turns to it in the sweep (2026-10-05). Leliana has it now.
NOT_BLIND = {10504, 17010, 17008, 17007, 1000}


def coverage() -> dict:
    try:
        return json.loads(COVERAGE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def covered(who: str, ability: dict) -> None:
    counts = coverage()
    key = f"{who}:{ability.get('id')} {ability.get('name')}"
    counts[key] = counts.get(key, 0) + 1
    try:
        COVERAGE.write_text(
            json.dumps(counts, indent=1, sort_keys=True), encoding="utf-8"
        )
    except OSError:
        pass


def by_the_games_word(ability: dict) -> dict | None:
    """How to use an ability the table does not name, from its target type:
    0 none (a passive), 1 self, 2 a friend, 4 an enemy, 8 and up the ground."""
    kind = ability.get("targetType") or 0
    name = ability.get("name") or str(ability.get("id"))
    if kind == 0 or name.startswith("HIDDEN") or ability.get("id") in NOT_BLIND:
        return None
    if kind == 1:
        return {"name": name, "use": "mode"}
    if kind in (2, 3):
        return {"name": name, "use": "friend", "under": 2.0}
    if kind == 4:
        return {
            "name": name,
            "use": "enemy",
            "reach": 0 if ability.get("range") else 3.0,
        }
    return {"name": name, "use": "enemy", "reach": 0, "clear": 6.0}


def ability_to_use(who: str, target: dict, notes: dict) -> tuple[int, dict] | None:
    """A talent ready now that suits the target, each tried once a turn; the
    least used so far first."""
    known = tool("abilities", {"id": who})
    counts = coverage()
    every = (known.get("talents") or []) + (known.get("spells") or [])
    every.sort(key=lambda a: counts.get(f"{who}:{a.get('id')} {a.get('name')}", 0))
    for a in every:
        what = ABILITIES.get(a.get("id")) or by_the_games_word(a)
        if not what or not a.get("ready") or skipped(a):
            continue
        key = f"{who}:{a['id']}"
        if a["id"] == 808 and who != TANK:
            continue
        if what["use"] == "mode":
            if key in notes.setdefault("modes", []) or a.get("active"):
                continue
            notes["modes"].append(key)
            covered(who, a)
            return a["id"], what
        if key in notes.setdefault("tried", []):
            continue
        if what.get("alone") and len(creatures("party")) > 1:
            continue
        if what.get("fire") and any(
            word in target.get("name", "") for word in FIREPROOF
        ):
            continue
        if what["use"] in ("friend", "friends"):
            hurt = sorted(
                (
                    (c["health"]["current"] / c["health"]["max"], c)
                    for c in creatures("party")
                    if (c.get("health") or {}).get("max") and c["health"]["current"] > 0
                ),
                key=lambda pair: pair[0],
            )
            hurt = [pair for pair in hurt if pair[0] < what["under"]]
            if len(hurt) < what.get("count", 1):
                continue
            notes["tried"].append(key)
            friend = hurt[0][1] if what["use"] == "friend" else {"id": who}
            covered(who, a)
            return a["id"], dict(what, on=friend["id"])
        if what["reach"] and target.get("distance", 99) > what["reach"]:
            continue
        if what.get("clear") and any(
            friend["id"] != who and apart(friend, target) < what["clear"]
            for friend in creatures("party")
        ):
            continue
        notes["tried"].append(key)
        if what.get("prefer"):
            for foe in creatures("hostile", near=who):
                if (
                    not foe.get("dead")
                    and foe.get("distance", 99) < 30
                    and any(word in foe.get("name", "") for word in what["prefer"])
                    and in_sight(who, foe["id"])
                ):
                    covered(who, a)
                    return a["id"], dict(what, on=foe["id"])
        covered(who, a)
        return a["id"], what
    return None


# What the pack holds to use in a fight besides poultices, by the start of the
# item's tag (the user, 2026-10-03: "Are you using poisons, flasks, etc.
# combat?"; until then they were carried and never used):
#   coat      a coating or a poison on the user's own weapons, once a fight,
#             with an enemy at hand
#   grenade   a flask thrown at the target when two or more enemies stand
#             within its burst and no friend does
CONSUMABLES = (
    ("gen_im_qck_coating", "coat"),
    ("gen_im_qck_poison", "coat"),
    ("gen_im_qck_grenade", "grenade"),
)


def item_to_use(
    who: str, target: dict, targets: list[dict], notes: dict
) -> tuple[int, dict] | None:
    """A coating or a flask worth using now, for one with the skill to (Poison-Making)."""
    known = tool("abilities", {"id": who})
    if not any(
        "Poison-Making" in (s.get("name") or "") for s in known.get("skills") or []
    ):
        return None
    pack = {i.get("name"): i for i in tool("inventory").get("items", [])}
    for a in known.get("items") or []:
        item = pack.get(a.get("name"))
        if not item or not a.get("ready"):
            continue
        tag = item.get("tag") or ""
        kind = next((k for prefix, k in CONSUMABLES if tag.startswith(prefix)), None)
        if kind == "coat":
            if f"coat:{who}" in notes or target.get("distance", 99) > 4.0:
                continue
            notes[f"coat:{who}"] = a["name"]
            return a["id"], {"name": a["name"], "use": "mode", "itemTag": tag}
        if kind == "grenade":
            crowd = [t for t in targets if apart(t, target) < 4.0]
            if len(crowd) < 2 or any(
                apart(f, target) < 5.5 for f in creatures("party")
            ):
                continue
            notes["flasks"] = notes.get("flasks", 0) + 1
            return a["id"], {"name": a["name"], "use": "enemy", "itemTag": tag}
    return None


# What is eaten raw when the potions are gone: elfroot for health, a deep
# mushroom for mana or stamina.
RAW = {"health": "gen_im_cft_reg_elfroot", "mana": "gen_im_cft_reg_mushroom"}


def potion_in_pack(who: str, kind: str = "health") -> tuple[int, str] | None:
    """A health poultice (or, kind "mana", a lyrium potion) `who` can drink now: its ability and the item's tag, the weakest first."""
    known = tool("abilities", {"id": who})
    pack = {i.get("name"): i for i in tool("inventory").get("items", [])}
    found = []
    for a in known.get("items") or []:
        item = pack.get(a.get("name"))
        tag = (item or {}).get("tag") or ""
        if not item or not a.get("ready"):
            continue
        if tag.startswith(f"gen_im_qck_{kind}"):
            found.append((tag, a["id"]))
        elif tag == RAW[kind]:
            # The herb itself, eaten: a little, and all there was on the
            # tower's third floor ("~" sorts it after every potion).
            found.append(("~" + tag, a["id"]))
    if not found:
        return None
    tag, ability = min(found)
    return ability, tag.lstrip("~")


def refusal_since(cursor: int | None, who: str) -> str:
    """What the mod refused `who` since `cursor`, as it told the player; nothing: ''.

    The state's last_refusal is the last one logged, whoever's and whenever:
    read for an order's answer it sent Merrill walking after Lyna's "Too far".
    """
    if cursor is None:
        return ""
    answer = tool(
        "events",
        {
            "topic": "turnbased.refused",
            "match": {"id": who},
            "since": cursor,
            "limit": 5,
        },
    )
    events = answer.get("events") or []
    if not events:
        return ""
    told = events[-1].get("payload", {})
    return f"{told.get('detail')} ({told.get('command')}: {told.get('note')})"


def step_toward(who: str, target: str, meters: float) -> tuple[str, int | None]:
    """Orders a walk of `meters` along the game's own path to `target`; says where to, with the order's cursor."""
    if meters < 1:
        return "no movement left", None
    found = tool(
        "geometry",
        {"id": who, "target": target, "walk": True, "waypoints": True},
    )
    points = [
        (p["x"], p["y"], p["z"]) for p in (found.get("walk") or {}).get("waypoints", [])
    ]
    if len(points) < 2:
        return f"no path ({(found.get('walk') or {}).get('result')})", None
    at = points[0]
    for nxt in points[1:]:
        leg = sum((a - b) ** 2 for a, b in zip(at, nxt)) ** 0.5
        if leg >= meters:
            share = meters / leg if leg else 0
            at = tuple(a + (b - a) * share for a, b in zip(at, nxt))
            break
        meters -= leg
        at = nxt
    answer = tool("order", {"action": "move", "id": who, "point": list(at)})
    taken = answer.get("taken", answer.get("given"))
    return f"to ({at[0]:.1f}, {at[1]:.1f}, {at[2]:.1f}), taken {taken}", answer.get(
        "cursor"
    )


def in_sight(who: str, target: str) -> bool:
    """Whether `who` has a line of sight to `target`, as the engine answers it."""
    sight = tool("geometry", {"id": who, "target": target}).get("lineOfSight")
    return bool((sight or {}).get("clear", True))


# Against a big one. The tower's ogre (815 health on Hard) felled the party
# twice while all four stood at its feet, the two who shoot dead in one round
# each of its sweeps: the shield holds it (Threaten), those who shoot stand
# off, and the rogue, on her blades, goes to its back, where a rogue's blows
# are backstabs.
BIG = 400.0
# Who holds a big one's eye: Alistair in the main game, by his id there. Another
# party (Awakening's has no Alistair) names its own, or has none.
TANK = os.environ.get("PLAYTHROUGH_TANK", "0x1b88")
STAND_OFF = 14.0


def big(targets: list[dict]) -> dict | None:
    """The boss among `targets`, by its health."""
    return next(
        (t for t in targets if (t.get("health") or {}).get("max", 0) >= BIG), None
    )


def spot(c: dict) -> tuple[float, float, float]:
    p = c.get("position") or {}
    return (p.get("x", 0.0), p.get("y", 0.0), p.get("z", 0.0))


def walk_to(who: str, point: tuple, budget: float) -> int | None:
    """Orders a walk to `point` if the game has a path there no longer than `budget`; the order's cursor."""
    found = (
        tool("geometry", {"id": who, "point": list(point), "walk": True}).get("walk")
        or {}
    )
    length = found.get("length", found.get("lengthSoFar"))
    if not found.get("found") or length is None or length > budget:
        return None
    answer = tool("order", {"action": "move", "id": who, "point": list(point)})
    return answer.get("cursor") if answer.get("taken", answer.get("given")) else None


def shoots(who: str, notes: dict) -> bool:
    """Whether `who` fights from range: a mage, or a bow in hand; read once a fight."""
    key = f"shoots:{who}"
    if key not in notes:
        sheet = tool("character", {"id": who})
        slots = (sheet.get("equipment") or {}).get("slots") or {}
        main = (slots.get("main") or {}).get("name", "").lower()
        notes[key] = (sheet.get("class") or {}).get("name") == "Mage" or any(
            w in main for w in ("bow", "staff")
        )
    return notes[key]


def place_for_boss(who: str, name: str, boss: dict, before: dict, notes: dict) -> bool:
    """Walks `who` to its place against a boss, if it is not there; whether it walked."""
    if who == TANK:
        return False
    budget = before.get("movement_left", 0)
    mine, its = spot(tool("creature", {"id": who})), spot(boss)
    off = math.dist(mine[:2], its[:2])
    if shoots(who, notes):
        if off >= STAND_OFF - 2 or budget < 5:
            return False
        step = min(budget * 0.6, STAND_OFF - off)
        away = ((mine[0] - its[0]) / max(off, 0.1), (mine[1] - its[1]) / max(off, 0.1))
        for degrees in (0, 45, -45, 90, -90):
            r = math.radians(degrees)
            d = (
                away[0] * math.cos(r) - away[1] * math.sin(r),
                away[0] * math.sin(r) + away[1] * math.cos(r),
            )
            point = (mine[0] + d[0] * step, mine[1] + d[1] * step, mine[2])
            cursor = walk_to(who, point, budget * 0.7)
            if cursor is not None:
                settle(who, cursor)
                note(
                    f"    {name} stands off from {boss.get('name')}: {off:.1f} m, {step:.1f} m more"
                )
                return True
        return False
    tank = tool("creature", {"id": TANK})
    # No such creature in this party: nobody holds the boss, so it has no back to
    # go to (in Awakening the question was refused 43 times in twenty minutes and
    # the rogue sent "behind" the boss as seen from the map's origin, 2026-10-09).
    if tank.get("error"):
        return False
    theirs = spot(tank)
    gap = math.dist(theirs[:2], its[:2])
    if tank.get("dead") or gap < 0.5:
        return False
    behind = (
        its[0] + (its[0] - theirs[0]) / gap * 2.5,
        its[1] + (its[1] - theirs[1]) / gap * 2.5,
        its[2],
    )
    if math.dist(mine[:2], behind[:2]) < 1.5:
        return False
    cursor = walk_to(who, behind, budget * 0.8)
    if cursor is None:
        return False
    settle(who, cursor)
    note(f"    {name} goes to {boss.get('name')}'s back")
    return True


# Issue 11's evidence, asked for with PLAYTHROUGH_PROBE11=1: once a run, the
# shot at a target out of sight is ordered all the same, and what its archer
# is at is read every three quarters of a second while it stands.
PROBE11 = os.environ.get("PLAYTHROUGH_PROBE11") == "1"
PROBED: list[str] = []


def probe11(who: str, name: str, target: dict) -> None:
    PROBED.append(who)
    sight = tool("geometry", {"id": who, "target": target["id"]})
    say(
        f"  probe 11: {name} is ordered at {target['name']} {target['id']}, {target.get('distance', 0):.1f} m off, "
        f"line of sight {json.dumps(sight.get('lineOfSight'))[:160]}"
    )
    answer = tool("order", {"action": "attack", "id": who, "target": target["id"]})
    for n in range(10):
        time.sleep(0.75)
        c = tool("creature", {"id": who})
        s = state()
        path = c.get("path") or {}
        say(
            f"  probe 11: +{0.75 * (n + 1):.2f} s {where(c)} command {json.dumps(c.get('command'))[:150]} "
            f"walking {path.get('beingWalked')} points {len(path.get('waypoints') or [])}; "
            f"phase {s.get('phase')}, {s.get('time_left_ms')} ms left, current {(s.get('current') or {}).get('name')}"
        )
        if (s.get("current") or {}).get("id") != who or s.get(
            "phase"
        ) == "awaiting order":
            break
    told = refusal_since(answer.get("cursor"), who)
    say(f"  probe 11: over; refusal {told!r}")


# Where the party falls back to as a fight begins, "x,y,z", when the fight
# would otherwise be fought in a doorway: Redcliffe castle's corpse room was
# lost three times with the melee jammed in its door (NO_SPACE_IN_MELEE_RING
# eighteen times in one fight), the enemies coming at it seven abreast.
RALLY = os.environ.get("PLAYTHROUGH_RALLY", "")

# The health, as a part of its whole, under which a party member drinks a
# poultice on its turn: a third with few in the pack, nearer a half with many.
DRINK = float(os.environ.get("PLAYTHROUGH_DRINK", "0.34"))
LITANY = os.environ.get("PLAYTHROUGH_LITANY", "") == "1"
# Abilities not to use in this run, by id or by how their name begins ("Summon,1000"): the Landsmeet's duel,
# 2026-10-08, where the hero spent her turns calling a bear and a wolf that could not get into the ring.
SKIP = [
    w.strip() for w in os.environ.get("PLAYTHROUGH_SKIP", "").split(",") if w.strip()
]


def skipped(ability: dict) -> bool:
    name = ability.get("name") or ""
    return any(w == str(ability.get("id")) or name.startswith(w) for w in SKIP)


def rally(who: str, name: str, notes: dict) -> bool:
    """On its first turn of a fight, walks `who` back to the rally point and ends the turn there."""
    if not RALLY or notes.get(f"rallied_{who}"):
        return False
    notes[f"rallied_{who}"] = True
    x, y, z = (float(v) for v in RALLY.split(","))
    # Each a little apart, the mage furthest back.
    place = len([k for k in notes if k.startswith("rallied_")])
    back = 6.0 if who == "0x66c2" else 0.0
    point = (x - back, y + (place - 2) * 1.6, z)
    me = next((c for c in creatures("party") if c["id"] == who), {})
    at = me.get("position") or {}
    if math.dist((at.get("x", 0), at.get("y", 0)), point[:2]) < 3.0:
        return False
    # As far as a turn's walk goes; with no such path the turn is played where it stands.
    cursor = walk_to(who, point, 18.0)
    if cursor is None:
        return False
    note(f"    {name} falls back to ({point[0]:.0f}, {point[1]:.0f})")
    settle(who, cursor, 8.0)
    if (state().get("current") or {}).get("id") == who:
        tool("turnbased_end_turn", {})
    return True


def player_turn(who: str, name: str, notes: dict) -> None:
    """One party member's turn: a poultice if hurt, a talent if one is ready, else attacks until the bar is spent."""
    refused = 0
    placed = False
    drank = False
    notes["tried"] = []
    if rally(who, name, notes):
        return
    for n in range(8):
        before = state()
        if (before.get("current") or {}).get("id") != who or not before.get("active"):
            return
        if before.get("phase") != "awaiting order":
            before = settle(who)
            if (before.get("current") or {}).get("id") != who or not before.get(
                "active"
            ):
                return
            if before.get("phase") != "awaiting order":
                say(f"    {name}: phase {before.get('phase')} for 16 s on its own turn")
                notes["stuck"] = notes.get("stuck", 0) + 1
                break
        if before.get("time_left_ms", 0) <= 0:
            break
        me = next((o for o in before.get("order", []) if o.get("id") == who), {})
        targets = creatures("hostile", near=who)
        if not targets:
            say(f"    {name}: no hostile left in sight")
            break
        # The Litany of Adralla against Uldred's turning of the mages: read
        # by whoever's turn it is while a captive mage has the light on it
        # (a visual effect, and not yet dead): what a player watching would
        # do. Read blindly at the hero's turn's start, thirteen times in a
        # fight, it saved nobody: the light came on other turns.
        if (
            LITANY
            and not notes.get(f"litany_{who}_{before.get('round')}")
            and turning()
        ):
            notes[f"litany_{who}_{before.get('round')}"] = True
            # The item's ability by the item itself: the list names it
            # "Unique Item Power", not the Litany (found 2026-10-03, after a
            # whole fight in which the name was looked for and never read).
            litany = next(
                (
                    i
                    for i in tool("inventory").get("items", [])
                    if i.get("tag") == "cir000im_litany"
                ),
                None,
            )
            power = (
                tool(
                    "script", {"command": "GetItemAbilityId", "args": [litany["id"]]}
                ).get("value")
                if litany
                else None
            )
            known = tool("abilities", {"id": who})
            read = next(
                (
                    a
                    for a in known.get("items") or []
                    if a.get("id") == power and a.get("ready")
                ),
                None,
            )
            if read:
                answer = tool(
                    "order",
                    {
                        "action": "ability",
                        "id": who,
                        "ability": read["id"],
                        "itemTag": "cir000im_litany",
                        "target": who,
                    },
                )
                settle(who, answer.get("cursor"))
                notes["litany"] = notes.get("litany", 0) + 1
                continue
        # Those in the fight, not every hostile of the area: with mages 130 m
        # off still standing she burnt two rage demons for sixty rounds.
        foes = [o for o in before.get("order", []) if o.get("side") == "enemy"]
        if foes and not notes.get("left_the_fire") and leaves_the_fire(who, foes):
            notes["left_the_fire"] = True
            say(f"    {name} leaves the burning man's form: only the fireproof stand")
            time.sleep(2.5)
            continue
        # In the form, those fire can hurt first.
        burnable = [
            t
            for t in targets
            if not any(word in t.get("name", "") for word in FIREPROOF)
        ]
        if (
            burnable
            and len(burnable) < len(targets)
            and tool("creature", {"id": who}).get("appearance") == BURNING_MAN_LOOKS
        ):
            targets = burnable
        # One a turn: the turn's own reading of its health is the turn's
        # start's, and Sten drank three at "49%" in one turn (2026-10-03).
        # At a third, not a half: with Threaten up the shield is under a half
        # every turn, and Alistair drank eleven of thirteen in one fight on
        # the road, a quarter of his health each (2026-10-03).
        if (
            me.get("health", 1) < DRINK
            and not drank
            and not notes.get(f"no_poultice_{who}")
        ):
            # Whichever health poultice the pack holds, the lesser first (the
            # hurlock alpha's two were of the next kind, and went undrunk).
            poultice = potion_in_pack(who)
            answer = (
                tool(
                    "order",
                    {
                        "action": "ability",
                        "id": who,
                        "ability": poultice[0],
                        "itemTag": poultice[1],
                        "target": who,
                    },
                )
                if poultice
                else {"error": "no health poultice in the pack"}
            )
            if answer.get("taken") or answer.get("given"):
                after = settle(who, answer.get("cursor"))
                note(
                    f"    {name} drinks a poultice at {me.get('health', 0):.0%}: {left(before)} -> {left(after)}"
                )
                notes["poultices"] = notes.get("poultices", 0) + 1
                drank = True
                continue
            notes[f"no_poultice_{who}"] = True
            note(f"    {name} has no poultice to drink: {json.dumps(answer)[:200]}")
        # A mage out of mana only swings its staff: lyrium while the pack has
        # it (three potions sat unused through the tower's first floor, lost
        # on Hard, 2026-10-03). Not for a fight nearly won: the last potion
        # went on three genlocks at the stairs.
        if (
            me.get("mana")
            and me.get("energy", 1) < 0.25
            and (len(targets) >= 4 or big(targets))
            and not notes.get(f"no_lyrium_{who}")
            # Two a fight: Morrigan drank all five at Redcliffe's windmill,
            # each gone in a turn's spells.
            and notes.get("lyrium", 0) < 2
        ):
            lyrium = potion_in_pack(who, "mana")
            if lyrium:
                answer = tool(
                    "order",
                    {
                        "action": "ability",
                        "id": who,
                        "ability": lyrium[0],
                        "itemTag": lyrium[1],
                        "target": who,
                    },
                )
                settle(who, answer.get("cursor"))
                notes["lyrium"] = notes.get("lyrium", 0) + 1
                note(f"    {name} drinks lyrium at {me.get('energy', 0):.0%} mana")
                continue
            notes[f"no_lyrium_{who}"] = True
        boss = big(targets)
        if boss and not placed:
            placed = True
            if place_for_boss(who, name, boss, before, notes):
                continue
        # A caster first (the ruins' emissary alone felled the party while it
        # shot at the archers), then the weakest of those near enough to
        # reach this turn, else the nearest.
        reach = before.get("movement_left", 0) + 3
        near = [t for t in targets if t.get("distance", 99) <= reach]
        casters = [t for t in near if any(w in t.get("name", "") for w in CASTERS)]
        target = (
            min(
                casters or near,
                key=lambda c: (c.get("health") or {}).get("current", 999),
            )
            if near
            else min(targets, key=lambda c: c.get("distance", 99))
        )
        # A shot at one it has no line to is let through and never fired: its
        # archer stands twelve seconds of the game's time and loses the turn
        # (issue 11). The weakest it can see, if the one chosen is hidden.
        if target.get("distance", 0) > 3 and not in_sight(who, target["id"]):
            if PROBE11 and not PROBED and shoots(who, notes):
                probe11(who, name, target)
                continue
            seen = [
                t
                for t in sorted(
                    near or targets,
                    key=lambda c: (c.get("health") or {}).get("current", 999),
                )
                if t["id"] != target["id"] and in_sight(who, t["id"])
            ]
            if seen:
                note(
                    f"    {name} has no line to {target['name']} {target['id']}: {seen[0]['name']} {seen[0]['id']} instead"
                )
                target = seen[0]
            else:
                notes["no_line"] = notes.get("no_line", 0) + 1
        talent = item_to_use(who, target, targets, notes) or ability_to_use(
            who, target, notes
        )
        if talent:
            on = (
                who if talent[1]["use"] == "mode" else talent[1].get("on", target["id"])
            )
            order = {"action": "ability", "id": who, "ability": talent[0], "target": on}
            if talent[1].get("ground") and target.get("position"):
                del order["target"]
                order["point"] = target["position"]
            if talent[1].get("itemTag"):
                order["itemTag"] = talent[1]["itemTag"]
            answer = tool("order", order)
            did = f"uses {talent[1]['name']} on"
            if talent[1].get("itemTag"):
                # An item used is worth the screen: it is what the pack is for.
                say(
                    f"    {name} uses {talent[1]['name']} ({target['name']} {target.get('distance', 0):.1f} m off)"
                )
        else:
            answer = tool(
                "order", {"action": "attack", "id": who, "target": target["id"]}
            )
            did = "attacks"
        after = settle(who, answer.get("cursor"))
        still = (after.get("current") or {}).get("id") == who and after.get("active")
        spent = before.get("time_left_ms", 0) - (
            after.get("time_left_ms", 0) if still else 0
        )
        told = refusal_since(answer.get("cursor"), who)
        taken = answer.get("taken", answer.get("given", True))
        # A refusal other than Too far (which the walk below answers) and an
        # order the bench did not take are worth the screen; the rest is the
        # transcript's.
        say(
            f"    {name} {did} {target['name']} {target['id']} ({target.get('distance', -1):.1f} m, {health_of(target)}): "
            f"{left(before)} -> "
            + (f"{left(after)}" if still else "turn over")
            + (f"; refused: {told}" if told else "")
            + ("" if taken else f" [{json.dumps(answer)[:160]}]"),
            shown=VERBOSE or not taken or bool(told and "Too far" not in told),
        )
        if talent and spent <= 0 and still:
            if talent[1]["use"] != "mode":
                # Once a fight on the screen for each: Jory out of stamina
                # tried Mighty Blow every turn (Eluvian's `ready` does not
                # count the cost).
                key = f"nothing:{who}:{talent[0]}"
                say(
                    f"      {name}: {talent[1]['name']} spent nothing",
                    shown=VERBOSE or key not in notes,
                )
                notes[key] = notes.get(key, 0) + 1
            continue
        if not still:
            return
        if spent <= 0:
            refused += 1
            notes["nothing_spent"] = notes.get("nothing_spent", 0) + 1
            if "Too far" in told and refused == 1:
                closer, walk_cursor = step_toward(
                    who, target["id"], after.get("movement_left", 0) - 0.5
                )
                moved = settle(who, walk_cursor)
                note(
                    f"      {name} walks toward it instead: {closer}; {left(after)} -> {left(moved)}"
                )
                if (moved.get("current") or {}).get("id") != who or not moved.get(
                    "active"
                ):
                    return
                continue
            if refused >= 2:
                break
    s = state()
    if (s.get("current") or {}).get("id") == who and s.get("active"):
        answer = tool("turnbased_end_turn", {})
        note(
            f"    {name} ends the turn with {left(s)} left: {json.dumps(answer)[:120]}"
        )


def raise_fallen(notes: dict) -> None:
    """Walks the nearest of the living to each fallen party member, which is what ends combat."""
    party = tool("creatures", {"side": "party", "includeDead": True}).get(
        "creatures", []
    )
    living = [c for c in party if not c.get("dead")]
    for down in (c for c in party if c.get("dead")):
        if not living:
            return
        walker = min(living, key=lambda c: apart(c, down))
        if apart(walker, down) < 7.0:
            continue
        p = down.get("position") or {}
        tool(
            "order",
            {
                "action": "move",
                "id": walker["id"],
                "point": [p.get("x"), p.get("y"), p.get("z")],
            },
        )
        notes["walked_to_the_fallen"] = notes.get("walked_to_the_fallen", 0) + 1
        say(
            f"  combat is not over while {down['name']} lies {apart(walker, down):.0f} m off: {walker['name']} walks to them"
        )


def fight(minutes: float) -> int:
    """Plays the fight on: the party's turns by `player_turn`, the rest watched."""
    deadline = time.time() + 60 * minutes
    began = time.time()
    notes: dict = {}
    last = None
    turn_began = time.time()
    idle = 0
    rounds = 0
    shown_round = None
    log_from = tool("log", {"file": "turnbased", "since": "end"}).get("next", 0)
    # Called straight after an attack ordered out of combat: the fight is a
    # walk and a blow away.
    for _ in range(40):
        if tool("health").get("mode") != "explore":
            break
        time.sleep(0.25)
    while time.time() < deadline:
        s = state()
        if "error" in s:
            say("  the game stopped answering:", s["error"])
            return 2
        if s.get("phase") == "suspended":
            # A conversation in the middle of the fight (the highwaymen's
            # leader yielding at Lothering's bridge): the mod stands aside
            # until it ends, and it is whoever plays to answer it. Waited on
            # as a turn, it was six "long turns".
            mode = tool("health").get("mode")
            if mode in ("dialog", "cutscene"):
                say(
                    f"  the fight stands aside for a {mode}: answer it (talk), then fight on"
                )
                break
        if not s.get("active"):
            health = tool("health")
            mode = health.get("mode")
            if mode == "combat" and idle < 60:
                # Combat with no enemy on the party yet: the mod leaves the
                # game in real time, and the game's own auto-pause then holds
                # it until the player's Space (the forest's genlocks, 30 m
                # off). Press it for them.
                if health.get("paused"):
                    tool("pause", {"paused": False})
                    notes["unpaused_before_the_fight"] = (
                        notes.get("unpaused_before_the_fight", 0) + 1
                    )
                    say(
                        "  combat, no turn-based fight yet, and the game paused: unpaused, as Space would"
                    )
                # The fight played and combat still on: the game keeps it until
                # a living party member stands within 7.5 m of each fallen one
                # (the scripts' CheckResurrection; six minutes in the Wilds
                # with Jory down 20 m from where the fight ended).
                if rounds and idle in (6, 30):
                    raise_fallen(notes)
                idle += 1
                time.sleep(0.5)
                continue
            say(f"  no fight: mode {mode}")
            break
        idle = 0
        rounds = max(rounds, s.get("round") or 0)
        current = s.get("current") or {}
        key = (s.get("round"), current.get("id"))
        if key != last and current:
            last = key
            turn_began = time.time()
            order = ", ".join(
                f"{o.get('name')} {o.get('health', 0):.0%}{' down' if o.get('down') else ''}"
                for o in s.get("order", [])
            )
            note(
                f"  round {s.get('round')}: {current.get('name')} {current.get('id')} ({current.get('side')}), {left(s)} | {order}"
            )
            if s.get("round") != shown_round:
                shown_round = s.get("round")
                party = ", ".join(
                    f"{o.get('name')} {o.get('health', 0):.0%}{' down' if o.get('down') else ''}"
                    for o in s.get("order", [])
                    if o.get("side") == "party"
                )
                foes = [
                    o
                    for o in s.get("order", [])
                    if o.get("side") != "party" and not o.get("down")
                ]
                say(
                    f"  round {shown_round}: {party} | {len(foes)} against: "
                    + ", ".join(
                        f"{o.get('name')} {o.get('health', 0):.0%}" for o in foes
                    ),
                    shown=not VERBOSE,
                )
        if time.time() - turn_began > 45:
            say(
                f"  !! {current.get('name')}'s turn has lasted {time.time() - turn_began:.0f} s in phase {s.get('phase')}"
            )
            notes["long_turns"] = notes.get("long_turns", 0) + 1
            turn_began = time.time()
            # Issue 10, until its fix is in the game: the hero's turn, opened
            # by a kept shot with no enemy left, loops at its own AI's waits;
            # selecting the hero ends them (the F1 key's message).
            if current.get("id") == HERO and s.get("phase") == "executing":
                tool("input", {"binding": "SelectPartyMemberIndexMessage 0 1"})
                say("     the hero selected by hand")
        if current.get("yours"):
            if s.get("phase") == "awaiting order":
                player_turn(current["id"], current.get("name", "?"), notes)
            else:
                time.sleep(0.15)
        else:
            time.sleep(0.25)
    notes.pop("tried", None)
    say(
        f"  the fight took {time.time() - began:.0f} s of wall clock, {rounds} rounds; notes {json.dumps(notes)}"
    )
    signs(log_from)
    return status()


# What a fight's log is read for besides warnings: lines the mod writes at info
# when something did not go as ordered, each counted with its first line. A
# fight that ends well can still be full of these (the user, 2026-10-03: "are
# you also reading the game's log?"; until then only warnings were).
SIGNS = (
    ("stood still", "without a step"),
    ("given back", "is given back"),
    ("target gone", "is gone: not cast"),
    ("chose nothing", "chose nothing"),
    ("too many commands", "commands this turn: no more"),
    ("walk strays", "strays from what was shown"),
    ("unknown caller", "not one of the"),
    ("no command", "queued no command"),
    ("left the fight", "is no longer fighting"),
    ("up again", "is up again"),
    ("pan ended short", "ended after"),
    ("dialogue mid-fight", "standing aside"),
    ("own AI's weapons held", "switch weapons from its own AI held"),
)


def signs(since: int | str = 0) -> None:
    """The fight's log from a byte cursor on: warnings and errors in full, the SIGNS counted."""
    # Eluvian's log tool gives five thousand lines a call, and a long fight at
    # debug writes more: read on from `next` until a call comes back short.
    lines: list[str] = []
    for _ in range(20):
        answer = tool("log", {"file": "turnbased", "since": since, "limit": 5000})
        page = [
            x if isinstance(x, str) else json.dumps(x) for x in answer.get("lines", [])
        ]
        lines += page
        since = answer.get("next", since)
        if len(page) < 5000:
            break
    bad = [x for x in lines if "[warning]" in x or "[error]" in x or "[critical]" in x]
    for line in bad[:20]:
        say("  log:", line[:300])
    if not bad:
        say(f"  log: no warning or error in {len(lines)} lines")
    for name, text in SIGNS:
        found = [x for x in lines if text in x]
        if found:
            say(f"  log: {len(found)} x {name}: {found[0].split('] ', 3)[-1][:200]}")
    # The longest turns by the mod's own events: a turn that hangs shows here
    # before it shows anywhere else.
    turns: list[tuple[float, str]] = []
    began: dict[str, float] = {}
    for line in lines:
        stamp = line[1:13]
        try:
            at = int(stamp[0:2]) * 3600 + int(stamp[3:5]) * 60 + float(stamp[6:])
        except ValueError:
            continue
        text = line.split("] ", 3)[-1]
        if "'s turn (" in text and text.startswith("round "):
            who = text.split(": ", 1)[1].split("'s turn (")[0]
            began[who] = at
        elif "'s turn ends:" in text:
            who = text.split("'s turn ends:")[0]
            if who in began:
                turns.append(
                    (
                        at - began.pop(who),
                        f"{who} ({text.split('turn ends: ')[1].split(';')[0]})",
                    )
                )
    if turns:
        turns.sort(reverse=True)
        mean = sum(t for t, _ in turns) / len(turns)
        say(
            f"  log: {len(turns)} turns, {mean:.1f} s each on average; the longest: "
            + ", ".join(f"{who} {t:.1f} s" for t, who in turns[:3])
        )


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    verb, rest = args[0], args[1:]
    say(f"> {' '.join(args)}")
    if verb == "launch":
        return launch(rest[0] if rest else "auto:2")
    if verb == "status":
        return status(quests=True)
    if verb == "look":
        return look(float(rest[0]) if rest else 30)
    if verb == "go":
        return go(rest[0])
    if verb == "use":
        return use(rest[0], rest[1] if len(rest) > 1 else None)
    if verb == "loot":
        return max(loot(target) for target in rest)
    if verb == "lootall":
        return lootall(float(rest[0]) if rest else 30)
    if verb == "rest":
        return recover(float(rest[0]) if rest else 120)
    if verb == "shop":
        return shop(rest)
    if verb == "party":
        return pick_party(rest)
    if verb == "talk":
        # Several places answer the choices as they come, one each.
        for place in rest or [None]:
            if tool("health").get("mode") != "dialog" and place is not None:
                break
            talk(int(place) if place is not None else None)
        return 0
    if verb == "fight":
        # The one switch, set once from the command line.
        global VERBOSE
        VERBOSE = "full" in rest
        minutes = [r for r in rest if r != "full"]
        return fight(float(minutes[0]) if minutes else 10)
    if verb == "save":
        return save(rest[0])
    if verb == "call":
        answer = tool(rest[0], json.loads(rest[1]) if len(rest) > 1 else {})
        print(json.dumps(answer, indent=1)[: int(rest[2]) if len(rest) > 2 else 6000])
        return 0
    if verb == "quit":
        say("quit:", json.dumps(bridge("game_quit", {"owner": OWNER}).get("answer")))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
