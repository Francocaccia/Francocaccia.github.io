# Franco Caccia: portfolio

One static site with three presentation modes (aviation, terminal, finance) over a single data file.
Logic is written in C++20 and compiled to WebAssembly; Astro pre-renders the pages.

## How it is put together

- `src/data/site.json` is the only source of content. The three views (aviation, terminal, finance) and the landing page all render it.
- `cpp/` holds the logic: a weather decoder (METAR and TAF), a shell, a chart engine, route geometry, a toy blockchain. It is compiled to WebAssembly with Emscripten and also runs at build time, so the pages are pre-rendered from it.
- `src/lib/globe.ts` draws the Aviation globe from committed Natural Earth data (public domain); `tests/ts` checks it against d3-geo.
- The bot's results are shown only as percentages, and any illustrative data says so on the page. Weather comes from a snapshot that a scheduled workflow refreshes.

## Requirements

- Node 22.12 or newer
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) in `~/emsdk` (or `$EMSDK`)

## Use

```bash
npm install
npm test            # C++ unit tests (Emscripten, run under Node)
npm run dev         # http://localhost:4321
npm run build       # output in dist/ (builds the C++ core, validates the data first)
npm run check:links # broken internal links and anchors in dist/ (add -- --external for outside URLs)
npm run fetch:metar # refresh src/data/weather.json from aviationweather.gov
```

Names that must never appear on the site go in an untracked `.private-terms` file (one per line); `npm run build` fails if one shows up in the data.

Edit the content in `src/data/site.json`. Entries that start with `TODO` are placeholders.

Paper trading only. Nothing here is investment advice.
