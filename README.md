# Franco Caccia: portfolio

One static site with three presentation modes (aviation, terminal, finance) over a single data file.
Logic is written in C++20 and compiled to WebAssembly; Astro pre-renders the pages.

See [PLAN.md](PLAN.md) for the architecture, rules and progress.

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
