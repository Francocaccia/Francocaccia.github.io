# Landing page: design notes

The landing page is the front door; the three views (Aviation, Terminal, Finance) keep their own looks.

## What the page has to do
Say in the first screen what Franco does and what he is looking for, give one main action (the resume) and one second one (the work), and prove it with numbers that come from `src/data/site.json`. Then let the visitor ask, or pick a view.

## Base
- Dark neutral gateway tokens from `src/styles/themes.css` (`--bg`, `--bg-raised`, `--fg`, `--fg-muted`, `--line`, `--link`). Each scene (`finance`, `terminal`, `chain`, `aviation`) takes its own token set through `[data-scene]`, so a chapter looks like its view.
- Fonts: IBM Plex Sans and IBM Plex Mono only, self-hosted. Display type is Plex Sans semibold with tight tracking.
- Radius: one value (`--radius`, 10px), pills only for the question chips. No shadows except the faint ring on the terminal.

## Type scale
Hero words 52 to 96px, section titles 30 to 52px, body 17 to 22px, running small text 14px, labels and tags 13px (never smaller).

## Components
- One button style, `.btn`, with a filled variant, `.btn-primary`. Chapter links use the same style with the scene's accent. `.chip` is only for the question chips and the "continue" link.
- Numbers strip: five facts, big number, one-line label.
- Ask bar: a text field, nine question chips and one answer panel. The answers are written ahead of time from site.json; nothing is generated.
- Doors: three cards with a real preview each (candles from the C++ chart engine, the shell's own output, the latest METAR).

## Motion
Everything moves with the page: the typing terminal runs once (about 4 seconds), chapters use scroll-driven CSS animations where supported, and a pause switch at the top of the page stops all of it. Reduced motion shows the finished frames.

## Honesty
Illustrative data is labeled where it appears. The bot's numbers are never shown as results; the page says it is too early. No claims that are not in the data file.
