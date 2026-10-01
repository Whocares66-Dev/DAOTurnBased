"""tools/sweep.py's judgement of a fight: who is in the order wrongly, who is left out wrongly."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sweep


def creature(
    id_: str,
    side: str,
    *,
    seen: str = "in view",
    shown: bool = True,
    target: str | None = None,
    fighting: bool = True,
) -> dict:
    return {
        "id": id_,
        "name": id_,
        "side": side,
        "combatState": 1 if fighting else 0,
        "room": {"number": 1574, "seen": seen, "shown": shown},
        "attackTarget": {"id": target} if target else None,
    }


PARTY = [creature("0x82", "party"), creature("0x10d", "party")]


class Judge(unittest.TestCase):
    def test_a_fight_in_one_seen_room_is_right(self) -> None:
        creatures = [*PARTY, creature("0x304", "hostile"), creature("0x305", "hostile")]
        found = sweep.judge(["0x82", "0x10d", "0x304", "0x305"], creatures)
        self.assertEqual(found["enemies"], ["0x304", "0x305"])
        self.assertEqual(found["left_out"], [])
        self.assertEqual(found["wrong_in"], [])
        self.assertEqual(found["wrong_out"], [])

    def test_a_hostile_fighting_in_a_room_never_seen_is_rightly_left_out(self) -> None:
        creatures = [
            *PARTY,
            creature("0x304", "hostile"),
            creature("0x2fe", "hostile", seen="never", shown=False),
        ]
        found = sweep.judge(["0x82", "0x10d", "0x304"], creatures)
        self.assertEqual(len(found["left_out"]), 1)
        self.assertEqual(found["wrong_out"], [])

    def test_a_hostile_fighting_in_a_seen_room_and_left_out_is_wrong(self) -> None:
        creatures = [*PARTY, creature("0x304", "hostile"), creature("0x305", "hostile")]
        found = sweep.judge(["0x82", "0x10d", "0x304"], creatures)
        self.assertEqual([w[0] for w in found["wrong_out"]], ["0x305"])

    def test_one_in_the_order_in_a_room_not_shown_and_attacking_nobody_is_wrong(
        self,
    ) -> None:
        creatures = [*PARTY, creature("0x2fe", "hostile", seen="never", shown=False)]
        found = sweep.judge(["0x82", "0x10d", "0x2fe"], creatures)
        self.assertEqual([w[0] for w in found["wrong_in"]], ["0x2fe"])

    def test_one_in_a_room_not_shown_who_attacks_the_party_belongs(self) -> None:
        creatures = [
            *PARTY,
            creature("0x2fe", "hostile", seen="never", shown=False, target="0x10d"),
        ]
        found = sweep.judge(["0x82", "0x10d", "0x2fe"], creatures)
        self.assertEqual(found["wrong_in"], [])

    def test_a_hostile_not_in_combat_is_not_missed(self) -> None:
        creatures = [
            *PARTY,
            creature("0x304", "hostile"),
            creature("0x262", "hostile", fighting=False),
        ]
        found = sweep.judge(["0x82", "0x10d", "0x304"], creatures)
        self.assertEqual(found["left_out"], [])

    def test_the_party_is_never_wrongly_in(self) -> None:
        creatures = [
            creature("0x82", "party", shown=False),
            creature("0x304", "hostile"),
        ]
        found = sweep.judge(["0x82", "0x304"], creatures)
        self.assertEqual(found["wrong_in"], [])


if __name__ == "__main__":
    unittest.main()
