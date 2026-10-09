"""tools/mutants.py's own logic, where it needs no build: what a wrong answer here would cost.

The tool's verdict is the gate's (every mutant caught), and since issue 67 it
takes shortcuts to it: a mutant's kept test is run before the rest, the
mutants go to the workers file by file with a file put back only as its
worker leaves it, and a worker's copy is kept from run to run. Each could
make a mutant look caught that is not, or leave a copy broken: a name that
Catch2 reads as two specs, a reader of its report that takes a passed case
for a failed one, a copy a stopped run left with a mutant in it. And the
record of what each mutant fails (--matrix) is what a mutant is removed on,
so the grouping is held here too.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutants
from mutants import Failure, Mutant, Pool

A, B, C = "src/core/A.cpp", "src/core/B.cpp", "src/core/C.cpp"
H, K = "src/core/H.h", "src/core/K.h"


def of(file: str, count: int) -> list[Mutant]:
    return [Mutant(f"{file} {n}", file, "", "") for n in range(count)]


def interleaved(*lots: list[Mutant]) -> list[Mutant]:
    """The lots' mutants in turn, as the list has a file's mutants here and there."""
    longest = max(len(lot) for lot in lots)
    return [lot[n] for n in range(longest) for lot in lots if n < len(lot)]


class ByFile(unittest.TestCase):
    def test_one_worker_goes_through_a_file_before_the_next_the_fullest_first(
        self,
    ) -> None:
        pool = Pool(interleaved(of(A, 2), of(B, 4), of(C, 3)))
        worker = Path("w0")
        taken = [pool.take(worker) for _ in range(9)]
        self.assertEqual([m.file for m in taken], [B] * 4 + [C] * 3 + [A] * 2)
        # In the list's order within a file.
        self.assertEqual([m.name for m in taken[:4]], [f"{B} {n}" for n in range(4)])
        self.assertIsNone(pool.take(worker))

    def test_the_headers_are_one_lot_header_by_header(self) -> None:
        pool = Pool(interleaved(of(H, 2), of(A, 3), of(K, 2)))
        worker = Path("w0")
        taken = [pool.take(worker).file for _ in range(7)]
        self.assertEqual(taken, [H, H, K, K, A, A, A])

    def test_a_second_worker_takes_a_file_nobody_is_at(self) -> None:
        pool = Pool(of(A, 5) + of(B, 2))
        first, second = Path("w0"), Path("w1")
        self.assertEqual(pool.take(first).file, A)
        self.assertEqual(pool.take(second).file, B)
        self.assertEqual(pool.take(first).file, A)
        self.assertEqual(pool.take(second).file, B)
        # Its file done, and the only one left has a worker: it joins it.
        self.assertEqual(pool.take(second).file, A)
        self.assertEqual(pool.take(first).file, A)

    def test_every_mutant_is_handed_out_once(self) -> None:
        chosen = interleaved(of(A, 7), of(B, 3), of(H, 2), of(C, 1))
        pool = Pool(chosen)
        workers = [Path(f"w{n}") for n in range(3)]
        taken = []
        turn = 0
        while mutant := pool.take(workers[turn % 3]):
            taken.append(mutant)
            turn += 1
        self.assertEqual(sorted(m.name for m in taken), sorted(m.name for m in chosen))
        self.assertTrue(all(pool.take(worker) is None for worker in workers))

    def test_the_tools_own_list_is_all_handed_out(self) -> None:
        pool = Pool(mutants.MUTANTS)
        worker = Path("w0")
        files = []
        count = 0
        while mutant := pool.take(worker):
            count += 1
            if not files or files[-1] != mutant.file:
                files.append(mutant.file)
        self.assertEqual(count, len(mutants.MUTANTS))
        # One worker is never at a file twice: each is put back once.
        self.assertEqual(len(files), len(set(files)))


class KeptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.file = Path(self.folder.name) / "caught.json"
        self.names = sorted(m.name for m in mutants.MUTANTS[:3])

    def test_no_file_is_nothing_kept(self) -> None:
        self.assertEqual(mutants.kept(self.file), {})

    def test_what_is_kept_is_read_back(self) -> None:
        caught = {self.names[1]: "a test, with a comma", self.names[0]: "another"}
        mutants.keep(caught, self.file)
        self.assertEqual(mutants.kept(self.file), caught)

    def test_the_file_is_a_line_a_mutant_in_their_names_order(self) -> None:
        caught = {name: f"test {n}" for n, name in enumerate(reversed(self.names))}
        mutants.keep(caught, self.file)
        lines = self.file.read_bytes().decode("utf-8").split("\n")
        self.assertEqual(lines[0], "{")
        self.assertEqual(lines[4:], ["}", ""])
        self.assertEqual(
            [line.partition('"')[2].partition('"')[0] for line in lines[1:4]],
            self.names,
        )

    def test_a_mutant_nothing_caught_or_that_is_gone_has_no_line(self) -> None:
        caught = {self.names[0]: "a test", self.names[1]: "", "no such mutant": "x"}
        mutants.keep(caught, self.file)
        self.assertEqual(mutants.kept(self.file), {self.names[0]: "a test"})

    def test_the_same_record_is_not_written_again(self) -> None:
        caught = {self.names[0]: "a test"}
        mutants.keep(caught, self.file)
        self.file.write_bytes(self.file.read_bytes().replace(b"\n", b"\r\n"))
        as_checked_out = self.file.read_bytes()
        mutants.keep(dict(caught), self.file)
        self.assertEqual(self.file.read_bytes(), as_checked_out)


class Specs(unittest.TestCase):
    def test_a_plain_name_is_its_own_spec(self) -> None:
        self.assertEqual(mutants.spec_of("a turn ends"), "a turn ends")

    def test_what_catch2_would_read_as_its_own_is_escaped(self) -> None:
        self.assertEqual(mutants.spec_of("held, and told"), "held\\, and told")
        self.assertEqual(mutants.spec_of("[turns] a list"), "\\[turns\\] a list")
        self.assertEqual(mutants.spec_of('a "word"'), 'a \\"word\\"')
        self.assertEqual(mutants.spec_of("a\\b"), "a\\\\b")


REPORT = """<?xml version="1.0" encoding="UTF-8"?>
<Catch2TestRun name="tb_tests.exe" rng-seed="1" xml-format-version="3" catch2-version="3.11.0">
  <TestCase name="a turn ends" tags="[machine]" filename="C:\\somewhere\\tests\\test_machine.cpp" line="10">
    <OverallResult success="true" skips="0"/>
  </TestCase>
  <TestCase name="held, and it&apos;s told" tags="[machine][turnedaway]" filename="C:\\somewhere\\tests\\test_machine.cpp" line="20">
    <Section name="first" filename="C:\\somewhere\\tests\\test_machine.cpp" line="22">
      <Expression success="false" type="CHECK" filename="C:\\somewhere\\tests\\World.h" line="77">
        <Original>
          w.Paused()
        </Original>
        <Expanded>
          false
        </Expanded>
      </Expression>
      <OverallResults successes="3" failures="1" expectedFailures="0" skipped="false"/>
    </Section>
    <Section name="second" filename="C:\\somewhere\\tests\\test_machine.cpp" line="26">
      <Expression success="false" type="CHECK" filename="C:\\somewhere\\tests\\World.h" line="77">
        <Original>
          w.Paused()
        </Original>
      </Expression>
      <OverallResults successes="3" failures="1" expectedFailures="0" skipped="false"/>
    </Section>
    <Expression success="false" type="REQUIRE" filename="C:\\somewhere\\tests\\test_machine.cpp" line="30">
      <Original>
        x == 1
      </Original>
    </Expression>
    <OverallResult success="false" skips="0"/>
  </TestCase>
  <TestCase name="no tags at all" filename="C:/somewhere/tests/test_rules.cpp" line="5">
    <Exception filename="C:/somewhere/tests/test_rules.cpp" line="9">
      bad optional access
    </Exception>
    <OverallResult success="false" skips="0"/>
  </TestCase>
  <TestCase name="random fights keep every promise" tags="[fights]" filename="C:\\somewhere\\tests\\test_fights.cpp" line="3443">
    <Info>
      seed := 7
    </Info>
    <Failure filename="C:\\somewhere\\tests\\test_fights.cpp" line="797">
      nobody acts out of turn &amp; unpaid (round 3, phase Turn, current 0x7); the machine's last lines:
          a line of the log
    </Failure>
    <OverallResult success="false" skips="0"/>
  </TestCase>
  <TestCase name="the last one, in which it stopped" tags="[machine]" filename="C:\\somewhere\\tests\\test_machine.cpp" line="40">
    <Expression success="false" type="CHECK" filename="C:\\somewhere\\tests\\test_machine.cpp" line="44">
"""


class Failures(unittest.TestCase):
    def setUp(self) -> None:
        self.found = mutants.failures(REPORT)

    def of(self, case: str) -> list[Failure]:
        return [failure for failure in self.found if failure.case == case]

    def test_a_case_that_passed_is_not_among_them(self) -> None:
        self.assertEqual(self.of("a turn ends"), [])
        self.assertEqual(len({failure.case for failure in self.found}), 4)

    def test_a_failed_case_has_every_failed_assertions_place_once_as_they_failed(
        self,
    ) -> None:
        case = "held, and it's told"
        self.assertEqual(self.found[0].case, case)
        self.assertEqual(
            self.of(case),
            [
                Failure(case, "World.h:77", False),
                Failure(case, "test_machine.cpp:30", False),
            ],
        )

    def test_an_exception_has_its_place_and_words(self) -> None:
        self.assertEqual(
            self.of("no tags at all"),
            [Failure("no tags at all", "test_rules.cpp:9: bad optional access", False)],
        )

    def test_a_fight_has_the_promise_that_was_broken_not_where_in_the_fight(
        self,
    ) -> None:
        case = "random fights keep every promise"
        promise = "test_fights.cpp:797: nobody acts out of turn & unpaid"
        self.assertEqual(self.of(case), [Failure(case, promise, True)])

    def test_a_case_with_no_result_is_where_the_run_stopped_after_what_failed(
        self,
    ) -> None:
        case = "the last one, in which it stopped"
        self.assertEqual(
            self.of(case),
            [
                Failure(case, "test_machine.cpp:44", False),
                Failure(case, mutants.STOPPED, False),
            ],
        )

    def test_a_run_in_which_nothing_failed_or_nothing_ran(self) -> None:
        self.assertEqual(
            mutants.failures(REPORT.partition('  <TestCase name="held')[0]), []
        )
        self.assertEqual(mutants.failures(""), [])


def row(*failed: tuple[str, str], verdict: str = "caught") -> dict:
    return {"file": A, "verdict": verdict, "failed": [list(pair) for pair in failed]}


ROWS = {
    "one": row(("t1", "a.cpp:1"), ("t2", "a.cpp:9")),
    "the same in another order": row(("t2", "a.cpp:9"), ("t1", "a.cpp:1")),
    "the same tests at another assertion": row(("t1", "a.cpp:1"), ("t2", "a.cpp:8")),
    "less": row(("t1", "a.cpp:1")),
    "less again": row(("t1", "a.cpp:1")),
    "more": row(("t1", "a.cpp:1"), ("t2", "a.cpp:9"), ("t3", "b.cpp:2")),
    "apart": row(("t4", "c.cpp:3")),
    "survived": row(verdict="SURVIVED"),
    "did not compile": row(verdict="did not compile"),
}


class EarnsItsKeep(unittest.TestCase):
    def setUp(self) -> None:
        self.failed = mutants.prints(ROWS)

    def test_a_mutant_nothing_failed_for_is_in_no_group(self) -> None:
        self.assertNotIn("survived", self.failed)
        self.assertNotIn("did not compile", self.failed)

    def test_the_same_assertions_in_the_same_tests_are_one_group(self) -> None:
        self.assertEqual(
            sorted(map(sorted, mutants.same(self.failed))),
            [["less", "less again"], ["one", "the same in another order"]],
        )

    def test_all_that_another_failed_and_more_wherever_the_other_is(self) -> None:
        over = mutants.above(self.failed, dict.fromkeys(self.failed))
        self.assertEqual(
            set(over),
            {
                "one",
                "the same in another order",
                "the same tests at another assertion",
                "more",
            },
        )

    def test_of_a_chain_in_one_place_the_least_stands_for_the_rest(self) -> None:
        # more > one > less, and "the same in another order" beside one.
        place = dict.fromkeys(self.failed, "f")
        over = mutants.above(self.failed, place)
        self.assertIn(over["more"], ("less", "less again"))
        self.assertIn(over["one"], ("less", "less again"))
        self.assertNotIn("less", over)
        self.assertNotIn("less again", over)

    def test_one_above_a_mutant_of_another_place_only_is_not_said(self) -> None:
        place = dict.fromkeys(self.failed, "f") | {"more": "g", "one": "g"}
        over = mutants.above(self.failed, place)
        # more is above one, of its own place; one is above nothing there.
        self.assertEqual(over.get("more"), "one")
        self.assertNotIn("one", over)
        self.assertIn("the same in another order", over)


SOURCE = """// A file of the core's, as the formatter leaves one.
#include <array>

namespace tb
{

inline constexpr float kLeast = 0.3f;
inline constexpr float kMost = Twice(6.0f);

inline constexpr std::array kTable{
    Row{1, 2.0f},
    Row{2, 3.0f},
};

struct Settings
{
    int rounds = 3;
    bool on = true;
};

int First(int a);

int First(int a)
{
    if (a > 0)
    {
        return a;
    }
    return 0;
}

std::optional<int> Second(int a,
                          int b)
{
    const auto twice = [](int x) {
        return 2 * x;
    };
    return twice(a) + b;
}

} // namespace tb
"""


class Places(unittest.TestCase):
    def of(self, old: str) -> str:
        return SOURCE[mutants.place(SOURCE, old) :].partition("\n")[0]

    def test_an_edit_in_a_function_is_at_the_functions_first_line(self) -> None:
        self.assertEqual(self.of("if (a > 0)"), "int First(int a)")
        self.assertEqual(self.of("        return a;"), "int First(int a)")
        self.assertEqual(self.of("    return 0;"), "int First(int a)")

    def test_a_declaration_before_it_is_not_the_function(self) -> None:
        first = mutants.place(SOURCE, "if (a > 0)")
        self.assertEqual(first, SOURCE.index("int First(int a)\n{"))

    def test_a_head_over_two_lines_and_a_lambda_inside_are_one_function(self) -> None:
        head = "std::optional<int> Second(int a,"
        self.assertEqual(self.of("return 2 * x;"), head)
        self.assertEqual(self.of("return twice(a) + b;"), head)
        self.assertEqual(self.of("int b)"), head)

    def test_two_functions_are_two_places(self) -> None:
        self.assertNotEqual(
            mutants.place(SOURCE, "return 0;"), mutants.place(SOURCE, "return 2 * x;")
        )

    def test_a_rows_a_constants_and_a_members_edit_is_at_its_own_line(self) -> None:
        self.assertEqual(self.of("Row{1, 2.0f}"), "    Row{1, 2.0f},")
        self.assertEqual(self.of("Row{2, 3.0f}"), "    Row{2, 3.0f},")
        self.assertEqual(self.of("0.3f"), "inline constexpr float kLeast = 0.3f;")
        self.assertEqual(self.of("6.0f"), "inline constexpr float kMost = Twice(6.0f);")
        self.assertEqual(self.of("rounds = 3"), "    int rounds = 3;")
        self.assertEqual(self.of("on = true"), "    bool on = true;")

    def test_an_edit_that_begins_with_the_line_before_its_own_is_at_its_own(
        self,
    ) -> None:
        self.assertEqual(self.of("\n    bool on = true;"), "    bool on = true;")


class Report(unittest.TestCase):
    """Over the tool's own list and the tree's sources: two mutants of one
    function, and one of another."""

    def setUp(self) -> None:
        within = mutants.within(mutants.MUTANTS)
        names = list(within)
        self.fine, self.crude = next(
            (a, b)
            for n, a in enumerate(names)
            for b in names[n + 1 :]
            if within[a] == within[b]
        )
        rest = [name for name in names if within[name] != within[self.fine]]
        self.elsewhere, self.stopped, self.fight = rest[:3]

    def test_what_goes_what_stays_and_the_fights_last(self) -> None:
        one, two, three = ("t1", "a.cpp:1"), ("t2", "a.cpp:9"), ("t3", "b.cpp:2")
        by_fight = ("random fights", "test_fights.cpp:797: a promise")
        rows = {
            self.fine: row(one),
            self.crude: row(one, two),
            self.elsewhere: row(one, three),
            self.stopped: row(one, two, ("t9", mutants.STOPPED)),
            self.fight: row(by_fight, verdict="caught (by the fights)"),
            "removed since": row(one),
        }
        lines = mutants.report(rows)
        self.assertIn("0 mutants in 0 groups", lines[0])
        self.assertFalse(any("removed since" in line for line in lines))
        goes = lines.index(next(line for line in lines if line.startswith("1 failed")))
        self.assertIn("of the same function", lines[goes])
        self.assertIn(f": {self.crude} (2), for {self.fine} (1)", lines[goes + 1])
        self.assertIn("of another function", lines[goes + 2])
        self.assertTrue(lines[goes + 2].startswith("1 failed"))
        self.assertTrue(lines[goes + 3].startswith("1 stop the test program"))
        self.assertEqual(
            lines[-3:],
            [
                "1 caught by the random fights alone, with no scripted case:",
                f"  {self.fight}",
                "    test_fights.cpp:797: a promise (random fights)",
            ],
        )

    def test_mutants_that_fail_alike_are_a_group_and_none_goes_for_another(
        self,
    ) -> None:
        rows = {
            self.fine: row(("t1", "a.cpp:1")),
            self.crude: row(("t1", "a.cpp:1")),
        }
        lines = mutants.report(rows)
        self.assertIn("2 mutants in 1 groups", lines[0])
        self.assertTrue(any(line.endswith(self.fine) for line in lines))
        self.assertTrue(any(line.startswith("0 failed all") for line in lines))


class Copies(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.tree = Path(folder.name) / "tree"
        self.worker = Path(folder.name) / "w0"
        for name, text in (
            ("CMakeLists.txt", "project(x)\n"),
            ("src/core/A.cpp", "int a = 1;\r\n"),
            ("src/core/B.cpp", "int b = 2;\n"),
            ("tests/test_a.cpp", "// a test\n"),
        ):
            (self.tree / name).parent.mkdir(parents=True, exist_ok=True)
            (self.tree / name).write_bytes(text.encode())
        mutants.sync(self.worker, self.tree)

    def test_a_copy_is_the_tree(self) -> None:
        for name in ("CMakeLists.txt", "src/core/A.cpp", "tests/test_a.cpp"):
            self.assertEqual(
                (self.worker / name).read_bytes(), (self.tree / name).read_bytes()
            )

    def test_a_mutant_a_stopped_run_left_is_mended_and_nothing_else_written(
        self,
    ) -> None:
        mutated = self.worker / "src/core/A.cpp"
        untouched = self.worker / "src/core/B.cpp"
        mutated.write_bytes(b"int a = 2;\r\n")
        before = untouched.stat().st_mtime_ns
        mutants.sync(self.worker, self.tree)
        self.assertEqual(mutated.read_bytes(), b"int a = 1;\r\n")
        # Not built again: its date is as it was.
        self.assertEqual(untouched.stat().st_mtime_ns, before)

    def test_a_file_the_tree_has_no_more_is_taken_out(self) -> None:
        (self.tree / "src/core/B.cpp").unlink()
        mutants.sync(self.worker, self.tree)
        self.assertFalse((self.worker / "src/core/B.cpp").exists())

    def test_a_second_run_in_a_tree_is_refused_while_the_first_holds_it(self) -> None:
        work = self.worker.parent / "mutants"
        first = mutants.alone(work)
        self.assertIsNotNone(first)
        self.assertIsNone(mutants.alone(work))
        first.close()
        after = mutants.alone(work)
        self.assertIsNotNone(after)
        after.close()

    def test_a_copy_configured_before_the_workers_flags_is_configured_again(
        self,
    ) -> None:
        old = (
            "CMAKE_CXX_FLAGS_DEBUG:STRING=/Zi /Ob0 /Od /RTC1\n"
            "CMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=/debug /INCREMENTAL\n"
        )
        self.assertFalse(mutants.has_flags(old))
        self.assertFalse(mutants.has_flags(""))
        new = "".join(
            f"{name}:UNINITIALIZED={value}\n" for name, value in mutants.FLAGS.items()
        )
        self.assertTrue(mutants.has_flags("# a comment\n" + new + "OTHER:BOOL=ON\n"))


if __name__ == "__main__":
    unittest.main()
