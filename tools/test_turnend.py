"""Every reason the machine ends a turn for has its class in core/TurnEnd.cpp.

Whether a turn's unspent time rolls over goes by why the turn ended (issue 29:
only a turn its actor ended itself rolls anything over), and the reason is the
words src/core/TurnMachine.cpp gives EndTurn. src/core/TurnEnd.cpp classes
each; one it does not have rolls nothing over, with a warning in the log. A
reason written in the machine and never classed would be that warning in
play, so this reads both sources and fails here instead: a new reason is
classed when it is written. And one the table still has that the machine no
longer gives is said too.
"""

import re
import unittest
from pathlib import Path

CORE = Path(__file__).resolve().parent.parent / "src" / "core"

# A turn's end in the machine: EndTurn(fx, "why"), or a choice of two.
END = re.compile(r"\bEndTurn\(fx,(.*?)\);", re.DOTALL)
WORDS = re.compile(r'"([^"]*)"')
# A reason's line in the table: TurnEnd{"why", true}.
CLASSED = re.compile(r'TurnEnd\{"([^"]*)",\s*(true|false)\}')


def given() -> set[str]:
    source = (CORE / "TurnMachine.cpp").read_text(encoding="utf-8")
    reasons: set[str] = set()
    for call in END.finditer(source):
        reasons.update(WORDS.findall(call.group(1)))
    return reasons


def classed() -> dict[str, bool]:
    source = (CORE / "TurnEnd.cpp").read_text(encoding="utf-8")
    return {why: by == "true" for why, by in CLASSED.findall(source)}


class TurnEnds(unittest.TestCase):
    def test_the_machine_ends_turns_and_the_table_has_reasons(self) -> None:
        # The patterns still find what they are for.
        self.assertGreater(len(given()), 10)
        self.assertGreater(len(classed()), 10)
        self.assertIn("ended by the player", given())
        self.assertTrue(classed()["ended by the player"])
        self.assertFalse(classed()["chose nothing"])

    def test_every_reason_the_machine_gives_is_classed(self) -> None:
        self.assertEqual(given() - set(classed()), set())

    def test_the_table_has_no_reason_the_machine_never_gives(self) -> None:
        self.assertEqual(set(classed()) - given(), set())

    def test_every_end_names_its_reason_in_words(self) -> None:
        # A reason passed as a variable could not be read here.
        source = (CORE / "TurnMachine.cpp").read_text(encoding="utf-8")
        for call in END.finditer(source):
            with self.subTest(call=call.group(0)):
                self.assertTrue(WORDS.findall(call.group(1)))


if __name__ == "__main__":
    unittest.main()
