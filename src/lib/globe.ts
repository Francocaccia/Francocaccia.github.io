// The Aviation globe: an orthographic projection of the committed Natural Earth 1:110m data
// (src/data/world-110m.json), clipped at the horizon. Pure functions, no DOM and no data import, so the
// same code draws the static SVG at build time and the interactive globe in the browser.
//
// Deliberate exception to "logic in C++": the land data is far too big to pass through fc_call (48 KB
// input cap, JSON), and the wasm loads after the page does. The geometry facts (distances, courses, the
// great-circle sample points, the first view) still come from the C++ core.
//
// Conventions: points are [lon, lat] in degrees. On screen (the "unit disc") x points right and y points
// up, the globe has radius 1, and a third coordinate z is the depth: z >= 0 is the near side.

export const D2R = Math.PI / 180;

export type LonLat = [number, number];
export type Pt = [number, number];
export type Vec3 = [number, number, number];

export interface WorldLabel {
  n: string;
  lat: number;
  lon: number;
  /** Area of the country in steradians. */
  a: number;
}

/** The committed file: delta-encoded integers in units of 1/q degrees. */
export interface WorldFile {
  v: number;
  q: number;
  src: string;
  land: number[][][];
  borders: number[][];
  labels: WorldLabel[];
}

export interface World {
  /** Polygons: the exterior ring first, then its holes. */
  land: LonLat[][][];
  borders: LonLat[][];
  labels: WorldLabel[];
}

export interface View {
  lat: number;
  lon: number;
}

export interface Viewer {
  s0: number;
  c0: number;
  lon0: number;
  vec(p: LonLat): Vec3;
}

// ---- data ---------------------------------------------------------------------------------------------

function decodeFlat(flat: number[], q: number): LonLat[] {
  const out: LonLat[] = [];
  let x = 0;
  let y = 0;
  for (let i = 0; i + 1 < flat.length; i += 2) {
    x += flat[i];
    y += flat[i + 1];
    out.push([x / q, y / q]);
  }
  return out;
}

export function decodeWorld(file: WorldFile): World {
  return {
    land: file.land.map((polygon) => polygon.map((ring) => decodeFlat(ring, file.q))),
    borders: file.borders.map((line) => decodeFlat(line, file.q)),
    labels: file.labels,
  };
}

// ---- projection ---------------------------------------------------------------------------------------

export function viewer(lat0: number, lon0: number): Viewer {
  const s0 = Math.sin(lat0 * D2R);
  const c0 = Math.cos(lat0 * D2R);
  return {
    s0,
    c0,
    lon0,
    vec([lon, lat]: LonLat): Vec3 {
      const phi = lat * D2R;
      const dl = (lon - lon0) * D2R;
      const cp = Math.cos(phi);
      return [cp * Math.sin(dl), c0 * Math.sin(phi) - s0 * cp * Math.cos(dl), s0 * Math.sin(phi) + c0 * cp * Math.cos(dl)];
    },
  };
}

/** Where the far-side point's direction meets the limb: the unit-disc point on the horizon circle. */
function limbPoint(a: Vec3, b: Vec3): Pt {
  const t = a[2] / (a[2] - b[2]);
  const x = a[0] + (b[0] - a[0]) * t;
  const y = a[1] + (b[1] - a[1]) * t;
  const l = Math.hypot(x, y) || 1;
  return [x / l, y / l];
}

const dot = (a: Vec3, b: Vec3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

function slerp(a: Vec3, b: Vec3, t: number): Vec3 {
  const d = Math.acos(Math.max(-1, Math.min(1, dot(a, b))));
  if (d < 1e-9) return a;
  const s = Math.sin(d);
  const A = Math.sin((1 - t) * d) / s;
  const B = Math.sin(t * d) / s;
  return [A * a[0] + B * b[0], A * a[1] + B * b[1], A * a[2] + B * b[2]];
}

/** Adds points along each edge so no step is longer than maxStepDeg (the edge is a great-circle arc). */
function densify(pts: Vec3[], closed: boolean, maxStepDeg: number): Vec3[] {
  const out: Vec3[] = [];
  const edges = closed ? pts.length : pts.length - 1;
  for (let i = 0; i < edges; i++) {
    const a = pts[i];
    const b = pts[(i + 1) % pts.length];
    out.push(a);
    const d = Math.acos(Math.max(-1, Math.min(1, dot(a, b)))) / D2R;
    const k = Math.floor(d / maxStepDeg);
    for (let j = 1; j <= k; j++) out.push(slerp(a, b, j / (k + 1)));
  }
  if (!closed && pts.length) out.push(pts[pts.length - 1]);
  return out;
}

const sameLonLat = (a: LonLat, b: LonLat) => a[0] === b[0] && a[1] === b[1];

// ---- clipping -----------------------------------------------------------------------------------------

/** Which way round the limb a filled ring's boundary continues after it leaves the near side. */
export const EXTERIOR_LIMB_DIR = -1;

/**
 * Clips one filled ring to the near side. Returns closed loops in unit-disc coordinates: pieces of the
 * ring joined by arcs of the limb (the horizon circle). `dir` is the way round the limb, +1 counter-
 * clockwise: it follows the ring's winding, so exteriors and holes (wound oppositely) get opposite values.
 */
export function clipRing(ring: LonLat[], v: Viewer, dir: 1 | -1, maxStepDeg = 2): Pt[][] {
  const n = ring.length > 1 && sameLonLat(ring[0], ring[ring.length - 1]) ? ring.length - 1 : ring.length;
  if (n < 3) return [];
  const out = densify(ring.slice(0, n).map((p) => v.vec(p)), true, maxStepDeg);
  const m = out.length;
  if (out.every((p) => p[2] >= 0)) return [out.map((p): Pt => [p[0], p[1]])];
  if (out.every((p) => p[2] < 0)) return [];

  // Runs of visible points, each with the limb points where it enters and leaves.
  let start = -1;
  for (let i = 0; i < m; i++) {
    if (out[i][2] < 0 && out[(i + 1) % m][2] >= 0) {
      start = (i + 1) % m;
      break;
    }
  }
  if (start < 0) return [];
  interface Run {
    pts: Pt[];
    exit: Pt;
    ea: number;
    xa: number;
  }
  const runs: Run[] = [];
  let i = start;
  let count = 0;
  while (count < m) {
    const entry = limbPoint(out[(i - 1 + m) % m], out[i]);
    const pts: Pt[] = [entry];
    while (out[i][2] >= 0 && count < m) {
      pts.push([out[i][0], out[i][1]]);
      i = (i + 1) % m;
      count++;
    }
    const exit = limbPoint(out[(i - 1 + m) % m], out[i]);
    pts.push(exit);
    while (out[i][2] < 0 && count < m) {
      i = (i + 1) % m;
      count++;
    }
    runs.push({ pts, exit, ea: Math.atan2(entry[1], entry[0]), xa: Math.atan2(exit[1], exit[0]) });
  }

  const TAU = 2 * Math.PI;
  const used = new Set<Run>();
  const loops: Pt[][] = [];
  for (const first of runs) {
    if (used.has(first)) continue;
    const loop: Pt[] = [];
    let run: Run | undefined = first;
    while (run && !used.has(run)) {
      used.add(run);
      loop.push(...run.pts);
      // The next run is the first entry met going round the limb from this exit.
      let best: Run | undefined;
      let bestD = Infinity;
      for (const s of runs) {
        let d = (s.ea - run.xa) * dir;
        d = ((d % TAU) + TAU) % TAU;
        if (d < bestD) {
          bestD = d;
          best = s;
        }
      }
      const steps = Math.max(1, Math.ceil(bestD / (maxStepDeg * D2R)));
      for (let k = 1; k < steps; k++) {
        const a = run.xa + (dir * bestD * k) / steps;
        loop.push([Math.cos(a), Math.sin(a)]);
      }
      run = best;
    }
    loops.push(loop);
  }
  return loops;
}

/** Clips a polygon (exterior, then holes): the loops of every ring, ready for one even-odd path. */
export function clipPolygon(rings: LonLat[][], v: Viewer, maxStepDeg = 2): Pt[][] {
  const loops: Pt[][] = [];
  rings.forEach((ring, index) => {
    const dir = (index === 0 ? EXTERIOR_LIMB_DIR : -EXTERIOR_LIMB_DIR) as 1 | -1;
    loops.push(...clipRing(ring, v, dir, maxStepDeg));
  });
  return loops;
}

/** Clips an open line to the near side: the visible pieces, each cut exactly on the limb. */
export function clipLine(line: LonLat[], v: Viewer, maxStepDeg = 2): Pt[][] {
  if (line.length < 2) return [];
  const pts = densify(line.map((p) => v.vec(p)), false, maxStepDeg);
  const pieces: Pt[][] = [];
  let piece: Pt[] = [];
  for (let i = 0; i < pts.length; i++) {
    const p = pts[i];
    if (p[2] >= 0) {
      if (!piece.length && i > 0) piece.push(limbPoint(pts[i - 1], p));
      piece.push([p[0], p[1]]);
    } else if (piece.length) {
      piece.push(limbPoint(pts[i - 1], p));
      pieces.push(piece);
      piece = [];
    }
  }
  if (piece.length) pieces.push(piece);
  return pieces.filter((s) => s.length >= 2);
}

/** Even-odd point-in-loops test (the same rule the SVG fill uses). */
export function insideEvenOdd(loops: Pt[][], x: number, y: number): boolean {
  let c = false;
  for (const loop of loops) {
    for (let i = 0, j = loop.length - 1; i < loop.length; j = i++) {
      const [xi, yi] = loop[i];
      const [xj, yj] = loop[j];
      if (yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) c = !c;
    }
  }
  return c;
}

// ---- graticule ----------------------------------------------------------------------------------------

export interface Graticule {
  lines: LonLat[][];
  equator: LonLat[];
}

/** Meridians and parallels every `step` degrees (the equator is returned separately, to be drawn stronger). */
export function graticule(step = 15): Graticule {
  const lines: LonLat[][] = [];
  for (let lon = -180; lon < 180; lon += step) {
    const line: LonLat[] = [];
    for (let lat = -90; lat <= 90; lat += 5) line.push([lon, lat]);
    lines.push(line);
  }
  const parallel = (lat: number): LonLat[] => {
    const line: LonLat[] = [];
    for (let lon = -180; lon <= 180; lon += 5) line.push([lon, lat]);
    return line;
  };
  for (let lat = -90 + step; lat < 90; lat += step) if (lat !== 0) lines.push(parallel(lat));
  return { lines, equator: parallel(0) };
}

// ---- paths --------------------------------------------------------------------------------------------

const f1 = (n: number) => (Math.round(n * 10) / 10).toString();

/**
 * An SVG path from unit-disc points: scaled by r around (cx, cy), y flipped, in relative moves to keep it
 * short. `close` adds z to each piece. Points closer than `minStep` pixels to the last one kept are skipped
 * (the last point of a piece is always kept), which thins the static page's path without changing its shape.
 */
export function toPath(pieces: Pt[][], cx: number, cy: number, r: number, close: boolean, minStep = 0): string {
  let d = '';
  for (const piece of pieces) {
    let px = 0;
    let py = 0;
    let started = false;
    let lastX = 0;
    let lastY = 0;
    piece.forEach(([ux, uy], i) => {
      const x = cx + ux * r;
      const y = cy - uy * r;
      const isLast = i === piece.length - 1;
      if (started && !isLast && minStep > 0 && Math.hypot(x - lastX, y - lastY) < minStep) return;
      lastX = x;
      lastY = y;
      const rx = Math.round(x * 10) / 10;
      const ry = Math.round(y * 10) / 10;
      if (!started) {
        d += `M${rx} ${ry}l`;
        started = true;
      } else {
        d += `${f1(rx - px)} ${f1(ry - py)} `;
      }
      px = rx;
      py = ry;
    });
    d = d.trimEnd() + (close ? 'z' : '');
  }
  return d;
}

// ---- interaction math ---------------------------------------------------------------------------------

export const MAX_LAT = 85;

export function wrapLon(lon: number): number {
  const w = ((((lon + 180) % 360) + 360) % 360) - 180;
  return w === -180 ? 180 : w;
}

export function clampLat(lat: number): number {
  return Math.max(-MAX_LAT, Math.min(MAX_LAT, lat));
}

/** A drag of (dx, dy) screen pixels on a globe of radius r pixels: the surface follows the pointer. */
export function dragView(view: View, dx: number, dy: number, r: number): View {
  const k = 1 / D2R / r; // degrees per pixel
  return { lat: clampLat(view.lat + dy * k), lon: wrapLon(view.lon - dx * k) };
}

/** Turns the globe by a fixed number of degrees (the buttons and the arrow keys). */
export function nudgeView(view: View, dLon: number, dLat: number): View {
  return { lat: clampLat(view.lat + dLat), lon: wrapLon(view.lon + dLon) };
}

export const INERTIA_TAU_MS = 325;
export const INERTIA_STOP = 0.02; // degrees per frame

/** Velocity after dtMs of friction (exponential, time constant INERTIA_TAU_MS). */
export function decayVelocity(velocity: number, dtMs: number): number {
  return velocity * Math.exp(-dtMs / INERTIA_TAU_MS);
}

export const MIN_ZOOM = 1;
export const MAX_ZOOM = 3;
export const clampZoom = (z: number) => Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, z));

// ---- what is on screen --------------------------------------------------------------------------------

export interface Airport {
  icao: string;
  name: string;
  lat: number;
  lon: number;
}

export interface PlacedAirport {
  icao: string;
  /** Depth of the airport: > 0.15 is far enough round the near side to draw. */
  z: number;
  visible: boolean;
  x: number;
  y: number;
  /** Which side the label goes on. */
  side: 'left' | 'right';
  /** Offset of the label from the marker, in pixels. */
  dx: number;
  dy: number;
  /** A line from marker to label, when two airports are close together. */
  leader: boolean;
  /** Marker radius in pixels. */
  r: number;
  /** For a hidden airport: where on the limb its chip sits, and the way to rotate. */
  chip?: { x: number; y: number; angle: number };
}

export const AIRPORT_VISIBLE_Z = 0.15;
const CLOSE_PX = 26;
const TINY_PX = 14;

/** Places airport markers and labels on a globe of radius r pixels centered at (cx, cy). */
export function placeAirports(airports: Airport[], v: Viewer, cx: number, cy: number, r: number): PlacedAirport[] {
  const placed: PlacedAirport[] = airports.map((a) => {
    const [px, py, pz] = v.vec([a.lon, a.lat]);
    const x = cx + px * r;
    const y = cy - py * r;
    const visible = pz > AIRPORT_VISIBLE_Z;
    const angle = Math.atan2(-py, px); // screen angle (y down) of the direction round the limb
    return {
      icao: a.icao,
      z: pz,
      visible,
      x,
      y,
      side: px >= 0 ? 'right' : 'left',
      dx: px >= 0 ? 10 : -10,
      dy: py >= 0 ? -8 : 16,
      leader: false,
      r: 5,
      chip: visible ? undefined : { x: cx + (px / (Math.hypot(px, py) || 1)) * (r - 14), y: cy - (py / (Math.hypot(px, py) || 1)) * (r - 14), angle },
    };
  });
  const shown = placed.filter((p) => p.visible);
  // Close together: smaller markers, labels on opposite sides, and a hairline leader from marker to label.
  for (let i = 0; i < shown.length; i++) {
    for (let j = i + 1; j < shown.length; j++) {
      const gap = Math.hypot(shown[i].x - shown[j].x, shown[i].y - shown[j].y);
      if (gap < TINY_PX) shown[i].r = shown[j].r = 3.5;
      if (gap < CLOSE_PX) {
        const left = shown[i].x <= shown[j].x ? shown[i] : shown[j];
        const right = left === shown[i] ? shown[j] : shown[i];
        left.side = 'left';
        left.dx = -22;
        left.dy = -14;
        left.leader = true;
        right.side = 'right';
        right.dx = 22;
        right.dy = 20;
        right.leader = true;
      }
    }
  }
  return placed;
}

export interface PlacedLabel {
  n: string;
  x: number;
  y: number;
}

const CHAR_PX = 7; // average width of a 12 px label character

export interface Rect {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
}
const overlaps = (a: Rect, b: Rect) => a.x0 < b.x1 + 4 && a.x1 > b.x0 - 4 && a.y0 < b.y1 + 4 && a.y1 > b.y0 - 4;

/** The screen boxes taken by airport markers, codes and chips, so country names can keep clear of them. */
export function airportBoxes(placed: PlacedAirport[]): Rect[] {
  const boxes: Rect[] = [];
  for (const p of placed) {
    if (p.visible) {
      boxes.push({ x0: p.x - 8, y0: p.y - 8, x1: p.x + 8, y1: p.y + 8 });
      const w = p.icao.length * 8.5;
      const lx = p.x + p.dx;
      const ly = p.y + p.dy;
      boxes.push(p.side === 'right' ? { x0: lx, y0: ly - 12, x1: lx + w, y1: ly + 3 } : { x0: lx - w, y0: ly - 12, x1: lx, y1: ly + 3 });
    } else if (p.chip) {
      boxes.push({ x0: p.chip.x - 30, y0: p.chip.y - 10, x1: p.chip.x + 30, y1: p.chip.y + 8 });
    }
  }
  return boxes;
}

/**
 * Country names to draw: only those with an anchor on the near side (depth > 0.3) that are big enough on
 * screen to hold the name, largest first, at least 40 px apart, at most `max` at once.
 */
export function placeCountryLabels(labels: WorldLabel[], v: Viewer, cx: number, cy: number, r: number, max = 4, avoid: Rect[] = []): PlacedLabel[] {
  const candidates = labels
    .map((l) => {
      const [px, py, pz] = v.vec([l.lon, l.lat]);
      return { l, px, py, pz, size: Math.sqrt(l.a * pz) * r };
    })
    .filter((c) => c.pz > 0.3 && c.size >= 0.5 * c.l.n.length * CHAR_PX)
    .sort((a, b) => b.l.a * b.pz - a.l.a * a.pz);
  const out: PlacedLabel[] = [];
  for (const c of candidates) {
    const x = cx + c.px * r;
    const y = cy - c.py * r;
    if (out.some((o) => Math.hypot(o.x - x, o.y - y) < 40)) continue;
    // A country name never sits on top of an airport marker or code.
    const w = c.l.n.length * CHAR_PX * 1.05;
    const box: Rect = { x0: x - w / 2, y0: y - 10, x1: x + w / 2, y1: y + 3 };
    if (avoid.some((a) => overlaps(box, a))) continue;
    out.push({ n: c.l.n, x, y });
    if (out.length >= max) break;
  }
  return out;
}

/** A spoken summary of the view for the live region. */
export function describeView(view: View, airports: PlacedAirport[]): string {
  const ns = (lat: number) => `${Math.round(Math.abs(lat))}${lat >= 0 ? 'N' : 'S'}`;
  const ew = (lon: number) => `${Math.round(Math.abs(lon))}${lon >= 0 ? 'E' : 'W'}`;
  const on = airports.filter((a) => a.visible).map((a) => a.icao);
  const off = airports.filter((a) => !a.visible).map((a) => a.icao);
  const list = (names: string[]) => (names.length ? names.join(', ') : 'none');
  return `Centered near ${ns(view.lat)} ${ew(view.lon)}. Visible: ${list(on)}. Hidden: ${list(off)}.`;
}

/** Velocity (degrees per frame) after one more frame of coasting; zero for reduced motion or when too small to see. */
export function inertiaVelocity(velocity: number, dtMs: number, reducedMotion: boolean): number {
  if (reducedMotion) return 0;
  const next = decayVelocity(velocity, dtMs);
  return Math.abs(next) < INERTIA_STOP ? 0 : next;
}

// ---- drawing ------------------------------------------------------------------------------------------

export interface GlobeScene {
  view: View;
  zoom: number;
  /** Size of the drawing in CSS pixels, so labels keep a constant size. */
  width: number;
  height: number;
  airports: Airport[];
  /** One sampled great circle per leg, as [lon, lat] points. */
  arcs: LonLat[][];
  /** Skip outline points closer than this many pixels (the static page uses it to stay small). */
  simplify?: number;
}

const esc = (s: string) => s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);
const GRATICULE = graticule(15);
const ARROWS = ['→', '↗', '↑', '↖', '←', '↙', '↓', '↘']; // east, then counter-clockwise
const MIN_ARROW_PX = 40;

/** Radius in pixels of a globe filling 92% of the box at zoom 1. */
export const globeRadius = (width: number, height: number, zoom: number) => 0.46 * Math.min(width, height) * zoom;

function pathLength(pieces: Pt[][], r: number): number {
  let total = 0;
  for (const piece of pieces) for (let i = 1; i < piece.length; i++) total += Math.hypot(piece[i][0] - piece[i - 1][0], piece[i][1] - piece[i - 1][1]) * r;
  return total;
}

/** The inside of the globe's SVG (no <svg> wrapper): everything is classed, the colors live in CSS tokens. */
export function renderGlobeInner(world: World, scene: GlobeScene): string {
  const { width, height, zoom } = scene;
  const cx = width / 2;
  const cy = height / 2;
  const r = globeRadius(width, height, zoom);
  const v = viewer(scene.view.lat, scene.view.lon);
  const min = scene.simplify ?? 0;
  let svg = `<circle class="globe-sea" cx="${f1(cx)}" cy="${f1(cy)}" r="${f1(r)}"/>`;

  const grid = GRATICULE.lines.flatMap((line) => clipLine(line, v));
  svg += `<path class="globe-grid" d="${toPath(grid, cx, cy, r, false, min)}"/>`;
  svg += `<path class="globe-equator" d="${toPath(clipLine(GRATICULE.equator, v), cx, cy, r, false, min)}"/>`;

  const land = world.land.flatMap((polygon) => clipPolygon(polygon, v));
  svg += `<path class="globe-land" fill-rule="evenodd" d="${toPath(land, cx, cy, r, true, min)}"/>`;
  const borders = world.borders.flatMap((line) => clipLine(line, v));
  svg += `<path class="globe-border" d="${toPath(borders, cx, cy, r, false, min)}"/>`;

  // The route: each leg cut at the horizon, with a direction arrow where the leg is long enough on screen.
  for (const arc of scene.arcs) {
    const pieces = clipLine(arc, v, 5);
    svg += `<path class="globe-arc" d="${toPath(pieces, cx, cy, r, false)}"/>`;
    const mid = Math.floor(arc.length / 2);
    if (arc.length > 4 && pathLength(pieces, r) >= MIN_ARROW_PX) {
      const a = v.vec(arc[mid - 1]);
      const m = v.vec(arc[mid]);
      const b = v.vec(arc[mid + 1]);
      if (a[2] > 0 && m[2] > 0 && b[2] > 0) {
        const angle = (Math.atan2(-(b[1] - a[1]), b[0] - a[0]) * 180) / Math.PI;
        svg += `<path class="globe-arrow" d="M-6 -4L5 0L-6 4Z" transform="translate(${f1(cx + m[0] * r)} ${f1(cy - m[1] * r)}) rotate(${f1(angle)})"/>`;
      }
    }
  }

  const placedAirports = placeAirports(scene.airports, v, cx, cy, r);
  for (const l of placeCountryLabels(world.labels, v, cx, cy, r, 4, airportBoxes(placedAirports))) {
    svg += `<text class="globe-country" x="${f1(l.x)}" y="${f1(l.y)}" text-anchor="middle">${esc(l.n)}</text>`;
  }

  for (const p of placedAirports) {
    if (p.visible) {
      const lx = p.x + p.dx;
      const ly = p.y + p.dy;
      if (p.leader) svg += `<line class="globe-leader" x1="${f1(p.x)}" y1="${f1(p.y)}" x2="${f1(lx)}" y2="${f1(ly - 4)}"/>`;
      svg += `<circle class="globe-apt" cx="${f1(p.x)}" cy="${f1(p.y)}" r="${p.r}"/>`;
      svg += `<text class="globe-label" x="${f1(lx)}" y="${f1(ly)}" text-anchor="${p.side === 'right' ? 'start' : 'end'}">${esc(p.icao)}</text>`;
    } else if (p.chip) {
      const arrow = ARROWS[((Math.round((-p.chip.angle * 4) / Math.PI) % 8) + 8) % 8];
      svg += `<text class="globe-chip" x="${f1(p.chip.x)}" y="${f1(p.chip.y + 4)}" text-anchor="middle">${esc(p.icao)} ${arrow}</text>`;
    }
  }
  svg += `<circle class="globe-rim" cx="${f1(cx)}" cy="${f1(cy)}" r="${f1(r)}"/>`;
  return svg;
}

/** The whole <svg>, for the static page. At run time only the inside is replaced. */
export function renderGlobeSvg(world: World, scene: GlobeScene): string {
  return `<svg class="globe-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${scene.width} ${scene.height}" aria-hidden="true" focusable="false">${renderGlobeInner(world, scene)}</svg>`;
}
