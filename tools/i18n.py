"""The mod's catalogs (dev/I18N.md), kept in step with the source.

    python tools/i18n.py            # rewrite assets/Translations/*.json
    python tools/i18n.py --check    # say what is out of step; exit 1 if any

Every line marked in the source is a line to translate: under src/,
Tr(L"..."), TrFormat(L"...", ...) and N_(L"...") in C++; under ui/,
tr("...") and trf("...", ...) in the movies' ActionScript. Adjacent literals
are one line, as the compiler joins them. en-US.json is the list itself, each
line its own translation, for a translator to start from. Every other
catalog keeps what it has for a line still marked, gains an empty entry for a
new one (read in English until filled) and loses the lines no longer marked.
Lines are in the order the source first marks them, file by file.

--check also says where a translation's {} fields differ from the English's:
TrFormat (and trf) would pass such a translation over for the English, or
show a hole, so it would never be seen.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOGS = ROOT / "assets" / "Translations"
SOURCE_LOCALE = "en-US"

# (folder, extension, marker): where a kind of source marks its lines.
SOURCES = [
    ("src", ".cpp", re.compile(r"\b(?:Tr|TrFormat|N_)\s*\(\s*")),
    ("src", ".h", re.compile(r"\b(?:Tr|TrFormat|N_)\s*\(\s*")),
    ("ui", ".as", re.compile(r"\b(?:tr|trf)\s*\(\s*")),
]
LITERAL = re.compile(r'L?"((?:[^"\\\n]|\\.)*)"')
BETWEEN = re.compile(r"\s*")  # comments are blanked before the scan
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}
FIELD = re.compile(r"\{([0-9]*)\}")
CODE = re.compile(
    r'L?"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'|//[^\n]*|/\*.*?\*/', re.DOTALL
)


def unescape(text: str, where: str) -> str:
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == "\\":
            nxt = text[i + 1]
            if nxt not in ESCAPES:
                sys.exit(f"{where}: escape \\{nxt} is not read by this tool")
            out.append(ESCAPES[nxt])
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def without_comments(text: str) -> str:
    """The code alone: a comment's example, Tr(L"Rules"), is no line. Its
    characters are blanked, not removed, so positions and line numbers stay."""

    def blank(match: re.Match) -> str:
        found = match.group(0)
        if found.startswith(("//", "/*")):
            return "".join(c if c == "\n" else " " for c in found)
        return found

    return CODE.sub(blank, text)


def extract(root: Path = ROOT) -> list[str]:
    """The marked lines, each once, in the order the source first marks them."""
    seen: dict[str, None] = {}
    for folder, extension, marker in SOURCES:
        base = root / folder
        if not base.is_dir():
            continue
        for path in sorted(base.rglob(f"*{extension}")):
            text = without_comments(path.read_text(encoding="utf-8"))
            for start in marker.finditer(text):
                at = start.end()
                parts = []
                while True:
                    found = LITERAL.match(text, at)
                    if not found:
                        break
                    where = f"{path.relative_to(root)}:{text.count(chr(10), 0, at) + 1}"
                    parts.append(unescape(found.group(1), where))
                    at = BETWEEN.match(text, found.end()).end()
                if parts:
                    seen.setdefault("".join(parts), None)
    return list(seen)


def fields(text: str) -> list[str]:
    """A line's {} fields as the indexes they fill, in sorted order: {} are
    numbered as they come, {0} and {1} are as written."""
    out, auto = [], 0
    for match in FIELD.finditer(text):
        if match.group(1) == "":
            out.append(str(auto))
            auto += 1
        else:
            out.append(match.group(1))
    return sorted(out)


def load(path: Path) -> dict[str, str]:
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}


def render(catalog: dict[str, str]) -> str:
    return json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"


def wanted(lines: list[str], locale: str, existing: dict[str, str]) -> dict[str, str]:
    if locale == SOURCE_LOCALE:
        return {line: line for line in lines}
    return {line: existing.get(line, "") for line in lines}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "--check", action="store_true", help="change nothing; exit 1 if out of step"
    )
    args = parser.parse_args()

    lines = extract()
    if not lines:
        sys.exit("no marked lines found under src/ and ui/")
    CATALOGS.mkdir(parents=True, exist_ok=True)
    locales = sorted({p.stem for p in CATALOGS.glob("*.json")} | {SOURCE_LOCALE})
    problems = []
    for locale in locales:
        path = CATALOGS / f"{locale}.json"
        existing = load(path)
        made = wanted(lines, locale, existing)
        if args.check:
            if existing != made or list(existing) != list(made):
                new = [line for line in made if line not in existing]
                gone = [line for line in existing if line not in made]
                problems.append(
                    f"{path.name}: out of step ({len(new)} to add, {len(gone)} to remove)"
                )
            for english, translated in existing.items():
                if translated and fields(english) != fields(translated):
                    problems.append(
                        f"{path.name}: {english!r}: fields {fields(english)} read {fields(translated)}"
                    )
        else:
            path.write_text(render(made), encoding="utf-8", newline="\n")
            left = sum(1 for v in made.values() if not v)
            print(
                f"{path.name}: {len(made)} lines"
                + (f", {left} not translated" if left else "")
            )
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
