"""The descriptors the plugin registers with Eluvian are JSON with a description.

src/plugin/Plugin.cpp hands Eluvian (the development bench's MCP server) a
JSON descriptor for each tool, as C++ raw strings. One once had unescaped
quotes inside its description, and Eluvian refused it, which only eluvian.log
said (2026-09-30). This reads every one back out of the source and parses it,
so a broken one fails here rather than in play.
"""

import json
import re
import unittest
from pathlib import Path

PLUGIN = Path(__file__).resolve().parent.parent / "src" / "plugin" / "Plugin.cpp"

# A registration: the name, then one or more adjacent raw strings.
REGISTRATION = re.compile(
    r'RegisterTool\(\s*"(?P<name>[^"]+)",(?P<parts>(?:\s*R"\(.*?\)")+)',
    re.DOTALL,
)
PART = re.compile(r'R"\((.*?)\)"', re.DOTALL)


def descriptors() -> dict[str, str]:
    source = PLUGIN.read_text(encoding="utf-8")
    found = {}
    for match in REGISTRATION.finditer(source):
        found[match.group("name")] = "".join(PART.findall(match.group("parts")))
    return found


class Descriptors(unittest.TestCase):
    def test_every_registration_is_found(self) -> None:
        names = set(descriptors())
        self.assertEqual(
            names,
            {
                "turnbased_auto",
                "turnbased_delay",
                "turnbased_end_turn",
                "turnbased_portrait",
                "turnbased_seed",
                "turnbased_settings",
                "turnbased_state",
            },
        )

    def test_each_is_json_with_a_description(self) -> None:
        for name, text in descriptors().items():
            with self.subTest(name=name):
                parsed = json.loads(text)
                self.assertIsInstance(parsed.get("description"), str)
                self.assertTrue(parsed["description"])

    def test_each_says_its_own_arguments(self) -> None:
        # Since Eluvian's issue 110 a tool's schema is its own, and Eluvian
        # says of a call which arguments its tool does not have.
        for name, text in descriptors().items():
            with self.subTest(name=name):
                schema = json.loads(text).get("inputSchema")
                self.assertIsInstance(schema, dict)
                self.assertEqual(schema.get("type"), "object")
                self.assertIsInstance(schema.get("properties"), dict)
        state = json.loads(descriptors()["turnbased_state"])["inputSchema"]
        self.assertEqual(state["properties"], {})

    def test_tool_names_reach_a_model(self) -> None:
        # Letters, digits, '_' and '-' only (EluvianAPI.h).
        for name in descriptors():
            with self.subTest(name=name):
                self.assertRegex(name, r"^[A-Za-z0-9_-]+$")


if __name__ == "__main__":
    unittest.main()
