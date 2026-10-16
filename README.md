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
npm test          # C++ unit tests
npm run dev       # http://localhost:4321
npm run build     # output in dist/
```

Edit the content in `src/data/site.json`. Entries that start with `TODO` are placeholders.

Paper trading only. Nothing here is investment advice.
