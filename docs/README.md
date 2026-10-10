# The guide

The player's guide to the mod, a Jekyll site in `/docs` beside the source, as SkyrimFollowerTactics' and the other DAO repositories' are. Developer notes are in `/dev`.

## Preview

```sh
cd docs
bundle install
bundle exec jekyll serve
```

Open the address Jekyll prints (add `--livereload` to have the page follow an edit). The first build downloads the pinned Just the Docs v0.12.0 theme from GitHub. A change to `_config.yml` wants the server started again.

That is one version of the guide, and the header's selector reads "Current". Every version, as the site is published: `python tools/build_docs.py --serve` from the repository's root, which builds what is in the working tree at the site's root and each release tag `vX.Y.Z` that has a `docs` folder under `/X.Y.Z/`, and serves them at `http://127.0.0.1:4000/DAOTurnBased/`.

## Looks

Gold on black, the game's own: `_sass/turnbased_palette.scss` has the colours, each measured from the game's interface (its menus' warm black, its buttons' brown, the pale gold of its lettering, the gilt of a card's ring), `_sass/color_schemes/turnbased.scss` gives them to the theme, and `_sass/custom/custom.scss` has the rest: no line round a picture or round code, a table with no boxes and no rules, every other row shaded, the scrollbar in the text's colour, the code blocks' copy button (`_includes/icons/code_copy.html`), and the header's right (`_includes/components/aux_nav.html`: the version selector and GitHub's mark, as DAOEluvianMCP's guide has them).

## Editing

`index.md` and `guide/*.md` are the pages, written by hand; each page's `nav_order` is its place in the sidebar. Markdown here is one line a paragraph, as everywhere in this repository.

The guide says what a player sees and does, in plain words. How a rule came to be, and what the engine does underneath, is `dev/DESIGN.md`'s to say; when a rule changes there, the page that states it here changes with it (`guide/turns.md` has the numbers: a turn's 4 seconds, the rollover, Combat Tactics, the pace).

## Pictures

`assets/img/` holds the pictures, each cropped from the game's own window as Eluvian's `screenshot` tool takes it (never the desktop), at 1600x1000:

| Picture | What it shows | Taken in |
|---|---|---|
| `fight.jpg` | A fight in turns, the whole frame | The Dragonbone Wastes (Witch Hunt), the hero's turn |
| `portrait-bar.jpg` | The portrait bar alone | the same frame |
| `your-turn.jpg`, `time-bar.jpg` | A party member's turn: the bar, the time bar and its caption | the same frame, cropped |
| `cost-preview.jpg` | The pointer on an enemy: "Costs 2.9 s, leaving 0.1 s" | the Wastes' turn again, the pointer held on a cultist |
| `move-preview.jpg` | The pointer on the ground: the path's rings and "4.6 m, 0.8 s" | The Dead Trenches, Leliana's turn |
| `move-preview-red.jpg` | The pointer on ground out of reach: gold rings, then red, and "12.7 m of 6.6" | The Dragonbone Wastes, the hero's turn after a walk, the camera zoomed out |
| `options.jpg`, `controls.jpg` | The game's Options: the Turn-based section and its keys | out of a fight |
| `tactics.jpg` | The Tactics page with "Use when turn-based" | out of a fight |

To take one again: play to the scene, `screenshot` with `source` `game`, and crop the file it names. Look at every picture before committing it.

## Publishing

`.github/workflows/docs.yml` builds the site with `tools/build_docs.py` on every push to `master` that touches `docs/`, and on every release tag, and publishes it to GitHub Pages. The repository's Pages source must be set to "GitHub Actions" once, in its settings.
