"""The catalogs are in step with the source (dev/I18N.md, tools/i18n.py).

A line marked in the source and missing from a catalog reads in English in
that language, silently; a translation whose {} fields are not the English's
is passed over, silently. So the gate fails here instead: run
`python tools/i18n.py` to bring the catalogs in step.
"""

import json
import tempfile
import unittest
from pathlib import Path

import i18n


class Extract(unittest.TestCase):
    def lines_of(self, folder: str, name: str, text: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / folder
            path.mkdir()
            (path / name).write_text(text, encoding="utf-8")
            return i18n.extract(Path(tmp))

    def test_cpp_marks(self):
        found = self.lines_of(
            "src",
            "a.cpp",
            'Said(N_(L"One")); i18n::Tr(L"Two"); TrFormat(L"{} of "\n    L"{}", a, b);',
        )
        self.assertEqual(found, ["One", "Two", "{} of {}"])

    def test_comments_and_strings_are_not_marks(self):
        found = self.lines_of(
            "src",
            "a.cpp",
            '// Tr(L"In a comment")\n/* Tr(L"Nor this") */\nlog("Tr(L\\"in a string\\")"); Tr(L"Real");',
        )
        self.assertEqual(found, ["Real"])

    def test_a_mark_with_no_literal_is_none(self):
        self.assertEqual(
            self.lines_of("src", "a.cpp", "Tr(text); Tr(std::wstring_view english)"), []
        )

    def test_escapes(self):
        self.assertEqual(
            self.lines_of("src", "a.cpp", r'Tr(L"it\'s \"x\"\n")'), ['it\'s "x"\n']
        )

    def test_actionscript_marks(self):
        found = self.lines_of(
            "ui", "a.as", 'x = tr("Delay"); y = trf("{0} s", n); // tr("no")'
        )
        self.assertEqual(found, ["Delay", "{0} s"])

    def test_each_line_once_in_first_order(self):
        self.assertEqual(
            self.lines_of("src", "a.cpp", 'Tr(L"b"); Tr(L"a"); Tr(L"b");'), ["b", "a"]
        )


class Fields(unittest.TestCase):
    def test_numbered_as_they_come_or_as_written(self):
        self.assertEqual(i18n.fields("{} of {}"), ["0", "1"])
        self.assertEqual(i18n.fields("{1} de {0}"), ["0", "1"])
        self.assertEqual(i18n.fields("{} of {}"), i18n.fields("{1} de {0}"))
        self.assertNotEqual(i18n.fields("{} of {}"), i18n.fields("{}"))


class Catalogs(unittest.TestCase):
    def test_in_step_with_the_source(self):
        lines = i18n.extract()
        self.assertTrue(lines, "nothing is marked")
        for path in sorted(i18n.CATALOGS.glob("*.json")):
            existing = json.loads(path.read_text(encoding="utf-8"))
            made = i18n.wanted(lines, path.stem, existing)
            self.assertEqual(
                list(existing),
                list(made),
                f"{path.name} is out of step: python tools/i18n.py",
            )
            if path.stem == i18n.SOURCE_LOCALE:
                self.assertEqual(existing, made)
            for english, translated in existing.items():
                if translated:
                    self.assertEqual(
                        i18n.fields(english),
                        i18n.fields(translated),
                        f"{path.name}: {english!r} -> {translated!r}",
                    )

    def test_source_locale_exists(self):
        self.assertTrue((i18n.CATALOGS / "en-US.json").exists())


if __name__ == "__main__":
    unittest.main()
