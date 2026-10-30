// Everything the landing page needs from the C++ core and the repo, computed at build time. Nothing here runs
// in the browser: the page ships no WebAssembly and makes no request to the bot.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { coreCall } from './core';
import { clipLine, decodeWorld, renderGlobeInner, toPath, viewer, type LonLat } from './globe';
import { site } from './site';
import { stationWeather, STALE_MS, utcStamp } from './weather';

type Span = { t: string; k?: string; h?: string; c?: string };

// ---- crypto: the toy chain (mined by the C++ core, op chain.demo) -----------------------------------------

export interface ChainBlock {
  index: number;
  data: string;
  prev: string;
  nonce: number;
  hash: string;
}
export interface ChainTamperBlock extends ChainBlock {
  meetsDifficulty: boolean;
  linked: boolean;
}
export interface ChainDemo {
  algorithm: string;
  difficulty: number;
  format: string;
  blocks: ChainBlock[];
  tamper: { block: number; data: string; blocks: ChainTamperBlock[] }[];
}

export const chainScene = () => coreCall<ChainDemo>('chain.demo', {});

// ---- finance: illustrative candles -----------------------------------------------------------------------

interface ChartResult {
  svg: string;
  points: { c: number; spy: number; mix: number }[];
}

/** The C++ chart engine's illustrative series, with a per-candle index (--i) so the candles can grow one by one. */
export async function financeScene() {
  const chart = await coreCall<ChartResult>('chart.illustrative', { seed: 11, days: 28, width: 560, height: 260, compact: true, idPrefix: 'ld' });
  let i = 0;
  const svg = chart.svg.replace(
    /<line class="fcz-wick ([a-z]+)" ([^>]*)\/><rect class="fcz-body ([a-z]+)" ([^>]*)\/>/g,
    (_m, wickDir: string, wick: string, bodyDir: string, body: string) => {
      const style = `style="--i:${i++}"`;
      return `<line class="fcz-wick ${wickDir}" ${style} ${wick}/><rect class="fcz-body ${bodyDir}" ${style} ${body}/>`;
    },
  );
  const last = chart.points[chart.points.length - 1];
  const ticker = [
    { name: 'AutoTradingBot', value: last.c },
    { name: 'SPY', value: last.spy },
    { name: '60/40', value: last.mix },
  ];
  return { svg, ticker, days: chart.points.length };
}

// ---- coding: a real excerpt and the real shell output ----------------------------------------------------

/** A function from this repo's C++ core, read from the file so it is always the real code. */
export function codeExcerpt() {
  const file = 'cpp/src/geo.cpp';
  const lines = readFileSync(join(process.cwd(), file), 'utf8').replace(/\r\n/g, '\n').split('\n');
  const start = lines.findIndex((l) => l.startsWith('double distanceNm('));
  if (start < 0) throw new Error(`landing: distanceNm not found in ${file}`);
  let end = start;
  while (end < lines.length && lines[end] !== '}') end++;
  return { file, lines: lines.slice(start, end + 1) };
}

/** Real output of two shell commands, from the same C++ shell the Terminal page runs: `projects` and `cat about.md`. */
export async function terminalPreview(modes: string[]) {
  await coreCall('term.init', { site, modes, metars: {}, files: false });
  const run = (line: string) => coreCall<{ lines: Span[][] }>('term.exec', { line, cwd: '~', history: [line], now: 0 });
  const [projects, about, licenses] = await Promise.all([run('projects'), run('cat about.md'), run('licenses')]);
  // The landing page has no shell behind it, so clickable commands become plain text.
  const plain = (lines: Span[][]) => lines.map((line) => line.map((s) => (s.k === 'cmd' ? { t: s.t } : s)));
  return { projects: plain(projects.lines.slice(0, 6)), about: plain(about.lines.slice(0, 3)), licenses: plain(licenses.lines.slice(0, 4)) };
}

// ---- aviation: the route and the METAR ------------------------------------------------------------------

interface RouteResult {
  view: { lat: number; lon: number };
  arcs: [number, number][][];
  legs: { from: string; to: string; nm: number; course: number }[];
  totalNm: number;
}

export async function flightScene() {
  const route = await coreCall<RouteResult>('geo.route', { airports: site.airports });
  const arcs = route.arcs.map((arc) => arc.map(([lat, lon]): LonLat => [lon, lat]));
  const SIZE = 360;
  const empty = decodeWorld({ v: 1, q: 10, src: '', land: [], borders: [], labels: [] });
  // The globe without land (the Aviation page has the full map): graticule, the route and the airports.
  let inner = renderGlobeInner(empty, { view: route.view, zoom: 1, width: SIZE, height: SIZE, airports: site.airports, arcs, simplify: 1.5 });
  inner = inner.replace(/<path class="globe-arc"/g, '<path class="globe-arc" pathLength="1"');
  // One path along the whole route, for the plane to follow (the first view shows all of it).
  const r = 0.46 * SIZE;
  const flat: LonLat[] = [...arcs[0], ...arcs.slice(1).flatMap((arc) => arc.slice(1))];
  const pieces = clipLine(flat, viewer(route.view.lat, route.view.lon), 5);
  const planePath = toPath(pieces.slice(0, 1), SIZE / 2, SIZE / 2, r, false);
  const svg = `<svg class="globe-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${SIZE} ${SIZE}" aria-hidden="true" focusable="false">${inner}</svg>`;
  return { route, svg, planePath };
}

export function metarPreview() {
  const wx = stationWeather('KABE');
  const age = Date.now() - Date.parse(wx.observedAt);
  return {
    raw: wx.raw,
    stamp: utcStamp(wx.observedAt),
    sample: wx.sample,
    // Real snapshots say so; one older than six hours when built is called the last snapshot, not the latest.
    label: wx.sample ? 'Sample METAR (format demo)' : age > STALE_MS ? 'Last snapshot' : 'Snapshot',
  };
}
