"""tools/build_docs.py: which tags are releases and in what order, and what a
version is told of the others. The builds themselves are Jekyll's."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import build_docs


class Versions(unittest.TestCase):
    def test_release_tags_are_vxyz_alone_newest_first_by_number(self) -> None:
        tags = [
            "v0.9.0",
            "v0.10.0",
            "v1.0.0",
            "v0.10",
            "docs-1",
            "v0.2.11",
            "v0.2.9-rc1",
        ]
        self.assertEqual(
            build_docs.release_tags(tags), ["v1.0.0", "v0.10.0", "v0.9.0", "v0.2.11"]
        )

    def test_a_version_is_told_each_ones_label_place_and_pages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            current, old = Path(temporary) / "current", Path(temporary) / "old"
            for folder, pages in (
                (current, ("/", "/turns/", "/options/")),
                (old, ("/", "/turns/")),
            ):
                (folder / "guide").mkdir(parents=True)
                for i, permalink in enumerate(pages):
                    page = folder / "guide" / f"page{i}.md"
                    page.write_text(
                        f"---\ntitle: A page\npermalink: {permalink}\n---\n",
                        encoding="utf-8",
                    )
            # What a build left behind is no page of the guide's.
            (current / "_site").mkdir()
            (current / "_site" / "stale.md").write_text(
                "---\npermalink: /gone/\n---\n", encoding="utf-8"
            )
            versions = build_docs.versions_of(
                [
                    ("Current", current, "/DAOTurnBased"),
                    ("0.1.0", old, "/DAOTurnBased/0.1.0"),
                ]
            )
        self.assertEqual(
            versions,
            [
                {
                    "label": "Current",
                    "baseurl": "/DAOTurnBased",
                    "pages": ["/", "/options/", "/turns/"],
                },
                {
                    "label": "0.1.0",
                    "baseurl": "/DAOTurnBased/0.1.0",
                    "pages": ["/", "/turns/"],
                },
            ],
        )

    def test_the_selector_and_the_tool_agree_on_the_sites_place(self) -> None:
        config = (build_docs.DOCS / "_config.yml").read_text(encoding="utf-8")
        self.assertIn(f"baseurl: {build_docs.BASEURL}\n", config)
        # The theme draws the header's right, where the selector is, only with these set.
        self.assertIn("aux_links:", config)
        self.assertTrue(
            (build_docs.DOCS / "_includes" / "components" / "aux_nav.html").is_file()
        )


if __name__ == "__main__":
    unittest.main()
