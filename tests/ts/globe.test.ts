// Tests for src/lib/globe.ts: projection, horizon clipping against d3-geo as an oracle, interaction math.
// Run with `npm run test:ts` (Node 24 strips the types itself).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { geoContains } from 'd3-geo';
import { feature } from 'topojson-client';
import {
  EXTERIOR_LIMB_DIR,
  MAX_LAT,
  clampLat,
  clipLine,
  clipPolygon,
  decodeWorld,
  decayVelocity,
  describeView,
  dragView,
  graticule,
  inertiaVelocity,
  insideEvenOdd,
  nudgeView,
  placeAirports,
  placeCountryLabels,
  toPath,
  viewer,
  wrapLon,
  type LonLat,
  type Pt,
} from '../../src/lib/globe.ts';

const here = (p: string) => new URL(p, import.meta.url);
const world = decodeWorld(JSON.parse(readFileSync(here('../../src/data/world-110m.json'), 'utf8')));
const atlas = JSON.parse(readFileSync(here('../../node_modules/world-atlas/land-110m.json'), 'utf8'));
const landFeature = feature(atlas, atlas.objects.land);

const D2R = Math.PI / 180;
const allLoops = (v: ReturnType<typeof viewer>) => world.land.flatMap((polygon) => clipPolygon(polygon, v));

// A small deterministic generator, so the "random" points are the same on every run.
function lcg(seed: number) {
  let s = seed >>> 0;
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

/** Inverse orthographic projection: the [lon, lat] under the disc point (x, y), or null outside the disc. */
function unproject(x: number, y: number, lat0: number, lon0: number): LonLat | null {
  const r2 = x * x + y * y;
  if (r2 > 1) return null;
  const z = Math.sqrt(1 - r2);
  const s0 = Math.sin(lat0 * D2R);
  const c0 = Math.cos(lat0 * D2R);
  const lat = Math.asin(Math.max(-1, Math.min(1, z * s0 + y * c0))) / D2R;
  const lon = lon0 + Math.atan2(x, z * c0 - y * s0) / D2R;
  return [lon, lat];
}

// ---- projection ---------------------------------------------------------------------------------------

test('projection: golden points (the same formulas as the C++ Ortho)', () => {
  const near = (a: number[], b: number[]) => a.forEach((v, i) => assert.ok(Math.abs(v - b[i]) < 1e-9, `${a} vs ${b}`));
  const v0 = viewer(0, 0);
  near(v0.vec([0, 0]), [0, 0, 1]); // the center faces the viewer
  near(v0.vec([90, 0]), [1, 0, 0]); // 90 degrees east is on the right limb
  near(v0.vec([0, 90]), [0, 1, 0]); // the pole is on the top limb
  near(v0.vec([180, 0]), [0, 0, -1]); // the antipode is straight behind
  // Buenos Aires seen from the route's first view, checked against the formulas written out by hand.
  const v = viewer(3.1, -66.6);
  const [lon, lat] = [-58.4156, -34.5592];
  const phi = lat * D2R;
  const dl = (lon + 66.6) * D2R;
  const p0 = 3.1 * D2R;
  near(v.vec([lon, lat]), [
    Math.cos(phi) * Math.sin(dl),
    Math.cos(p0) * Math.sin(phi) - Math.sin(p0) * Math.cos(phi) * Math.cos(dl),
    Math.sin(p0) * Math.sin(phi) + Math.cos(p0) * Math.cos(phi) * Math.cos(dl),
  ]);
});

// ---- land against d3-geo ------------------------------------------------------------------------------

function mismatch(lat0: number, lon0: number, samples = 4000): number {
  const v = viewer(lat0, lon0);
  const loops = allLoops(v);
  const next = lcg(Math.round((lat0 + 100) * 1000 + (lon0 + 200) * 7));
  let bad = 0;
  let n = 0;
  while (n < samples) {
    const x = next() * 2 - 1;
    const y = next() * 2 - 1;
    const point = unproject(x, y, lat0, lon0);
    if (!point) continue;
    n++;
    const truth = geoContains(landFeature, point);
    if (truth !== insideEvenOdd(loops, x, y)) bad++;
  }
  return bad / samples;
}

const views: [number, number][] = [
  [3.1, -66.6], [0, 0], [30, 10], [-30, 150], [60, -100], [-60, 20], [10, 100], [45, -60], [-10, -60], [20, 80],
  [5, 135], [-40, -70], [35, -95], [50, 10], [-20, 30], [15, -15], [70, 90], [-45, 170], [25, -150], [0, -120],
];

test('land mask matches d3-geo on 20 views (under 0.5% of sampled points differ)', () => {
  let worst = 0;
  for (const [lat, lon] of views) {
    const m = mismatch(lat, lon);
    worst = Math.max(worst, m);
    assert.ok(m < 0.005, `view ${lat}, ${lon}: ${(m * 100).toFixed(2)}% differ`);
  }
  console.log(`worst mismatch ${(worst * 100).toFixed(3)}%`);
});

test('named regression views: the Caspian Sea hole at the limb', () => {
  // These two views put the Caspian (a hole in Eurasia) across the horizon, where the first draft filled it in.
  for (const [lat, lon] of [[-28.9, -3.3], [-19, 121.2]] as const) {
    const m = mismatch(lat, lon, 8000);
    assert.ok(m < 0.005, `view ${lat}, ${lon}: ${(m * 100).toFixed(2)}% differ`);
  }
});

test('polar and antimeridian views clip cleanly', () => {
  for (const [lat, lon] of [[89, 0], [-89, 0], [85, 170], [-85, -170], [0, 180], [0, -180], [45, 179.9], [-0.01, 180]] as const) {
    const m = mismatch(lat, lon, 3000);
    assert.ok(m < 0.007, `view ${lat}, ${lon}: ${(m * 100).toFixed(2)}% differ`);
  }
});

test('every clipped point lies inside the globe disc', () => {
  for (const [lat, lon] of views) {
    const v = viewer(lat, lon);
    for (const loop of allLoops(v)) for (const [x, y] of loop) assert.ok(Math.hypot(x, y) <= 1 + 1e-9, `${lat},${lon}: ${Math.hypot(x, y)}`);
    for (const border of world.borders) for (const piece of clipLine(border, v)) for (const [x, y] of piece) assert.ok(Math.hypot(x, y) <= 1 + 1e-9);
  }
});

test('the data winds every exterior one way and every hole the other (the clipper relies on it)', () => {
  // Planar signed area in lon/lat with longitudes unwrapped, the same rule the build script asserts.
  const sign = (ring: LonLat[]) => {
    const pts: LonLat[] = [];
    let lon = ring[0][0];
    for (const [px, y] of ring) {
      let x = px;
      while (x - lon > 180) x -= 360;
      while (x - lon < -180) x += 360;
      lon = x;
      pts.push([x, y]);
    }
    let s = 0;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) s += pts[j][0] * pts[i][1] - pts[i][0] * pts[j][1];
    return Math.sign(s);
  };
  let holes = 0;
  for (const polygon of world.land) {
    polygon.forEach((ring, index) => {
      assert.equal(sign(ring), index === 0 ? EXTERIOR_LIMB_DIR : -EXTERIOR_LIMB_DIR, 'exteriors are clockwise in lon/lat, holes the opposite');
      if (index > 0) holes++;
    });
  }
  assert.ok(holes >= 1); // the Caspian Sea
});

// ---- lines and the horizon ----------------------------------------------------------------------------

test('lines are cut exactly at the limb', () => {
  const v = viewer(0, 0);
  const meridian: LonLat[] = [];
  for (let lon = 0; lon <= 180; lon += 5) meridian.push([lon, 0]);
  const pieces = clipLine(meridian, v);
  assert.equal(pieces.length, 1);
  const end = pieces[0][pieces[0].length - 1];
  assert.ok(Math.abs(Math.hypot(end[0], end[1]) - 1) < 1e-9);
  assert.deepEqual(clipLine([[170, 0], [175, 0], [180, 0]], v), []);
  // A line that leaves the near side and comes back is two pieces.
  const out: LonLat[] = [[-60, 0], [-100, 0], [-150, 0], [-170, 10], [170, 10], [150, 0], [100, 0], [60, 0]];
  assert.ok(clipLine(out, v).length >= 2);
});

test('the graticule is closed at the limb and has an equator of its own', () => {
  const g = graticule(15);
  assert.ok(g.lines.length > 20);
  assert.equal(g.equator[0][1], 0);
  const v = viewer(20, 30);
  const pieces = g.lines.flatMap((l) => clipLine(l, v));
  assert.ok(pieces.length > 10);
  for (const piece of pieces) for (const [x, y] of piece) assert.ok(Math.hypot(x, y) <= 1 + 1e-9);
});

test('toPath scales, flips y and closes', () => {
  const d = toPath([[[0, 0], [1, 0], [0, 1]]], 100, 100, 50, true);
  assert.equal(d, 'M100 100l50 0 -50 -50z');
  // Points closer than minStep are thinned, the last point of a piece is kept.
  const dense: Pt[][] = [[[0, 0], [0.01, 0], [0.02, 0], [1, 0]]];
  assert.equal(toPath(dense, 0, 0, 100, false, 5), 'M0 0l100 0');
});

// ---- airports and labels ------------------------------------------------------------------------------

const route = [
  { icao: 'SABE', name: 'Buenos Aires Aeroparque', lat: -34.5592, lon: -58.4156 },
  { icao: 'KLYH', name: 'Lynchburg Regional', lat: 37.3267, lon: -79.2004 },
  { icao: 'KABE', name: 'Lehigh Valley Intl', lat: 40.6522, lon: -75.4408 },
];

test('airports: all three show on the first view, close ones get opposite sides and leaders', () => {
  const v = viewer(3.1, -66.6);
  const placed = placeAirports(route, v, 170, 170, 150);
  assert.ok(placed.every((p) => p.visible));
  const lyh = placed.find((p) => p.icao === 'KLYH')!;
  const abe = placed.find((p) => p.icao === 'KABE')!;
  assert.notEqual(lyh.side, abe.side);
  assert.ok(lyh.leader && abe.leader);
});

test('airports: hidden ones get a limb chip and no marker', () => {
  const v = viewer(10, 100); // looking at Asia
  const placed = placeAirports(route, v, 170, 170, 150);
  for (const p of placed) {
    assert.equal(p.visible, false);
    assert.ok(p.chip && Math.abs(Math.hypot(p.chip.x - 170, p.chip.y - 170) - 136) < 1e-6);
  }
});

test('airports: markers shrink when two are under 14 px apart, and not when they are apart', () => {
  const v = viewer(3.1, -66.6);
  const tiny = placeAirports(route, v, 170, 170, 40).find((p) => p.icao === 'KLYH')!;
  assert.equal(tiny.r, 3.5);
  const wide = placeAirports(route, v, 170, 170, 400);
  assert.equal(wide.find((p) => p.icao === 'SABE')!.r, 5);
  // Zoomed in on the pair, they are far enough apart that the leaders are no longer needed.
  const zoomed = placeAirports(route.slice(1), viewer(39, -77), 170, 170, 1500);
  assert.ok(zoomed.every((p) => !p.leader));
});

test('country labels: Argentina, Brazil and the United States show on the first view', () => {
  const v = viewer(3.1, -66.6);
  const placed = placeCountryLabels(world.labels, v, 170, 170, 150);
  const names = placed.map((l) => l.n);
  assert.ok(names.length <= 4);
  for (const n of ['Argentina', 'Brazil', 'United States']) assert.ok(names.includes(n), `${n} missing from ${names}`);
  assert.deepEqual(placeCountryLabels(world.labels, viewer(0, 170), 170, 170, 150), []);
  for (let i = 0; i < placed.length; i++) {
    for (let j = i + 1; j < placed.length; j++) assert.ok(Math.hypot(placed[i].x - placed[j].x, placed[i].y - placed[j].y) >= 40);
  }
});

test('describeView names what is visible and what is hidden', () => {
  const v = viewer(3.1, -66.6);
  assert.equal(
    describeView({ lat: 3.1, lon: -66.6 }, placeAirports(route, v, 170, 170, 150)),
    'Centered near 3N 67W. Visible: SABE, KLYH, KABE. Hidden: none.',
  );
  const text = describeView({ lat: 10, lon: 100 }, placeAirports(route, viewer(10, 100), 170, 170, 150));
  assert.match(text, /Centered near 10N 100E\. Visible: none\. Hidden: SABE, KLYH, KABE\./);
});

// ---- drag, inertia, reduced motion --------------------------------------------------------------------

test('drag: the surface follows the pointer, latitude is clamped and longitude wraps', () => {
  const r = 150;
  const start = { lat: 0, lon: 0 };
  const right = dragView(start, 50, 0, r);
  assert.ok(right.lon < 0 && right.lat === 0); // dragging right shows what was to the west
  assert.ok(dragView(start, 0, 50, r).lat > 0); // dragging down brings the north into view
  assert.equal(dragView({ lat: 80, lon: 0 }, 0, 5000, r).lat, MAX_LAT);
  assert.equal(dragView({ lat: -80, lon: 0 }, 0, -5000, r).lat, -MAX_LAT);
  const wrapped = dragView({ lat: 0, lon: 179 }, -500, 0, r);
  assert.ok(wrapped.lon >= -180 && wrapped.lon <= 180);
  assert.ok(Math.abs(dragView(start, r, 0, r).lon + 180 / Math.PI) < 1e-9); // one radius of drag is one radian
  assert.equal(wrapLon(190), -170);
  assert.equal(wrapLon(-190), 170);
  assert.equal(wrapLon(180), 180);
  assert.equal(clampLat(95), MAX_LAT);
  const nudged = nudgeView({ lat: 84, lon: 175 }, 10, 10);
  assert.equal(nudged.lat, MAX_LAT);
  assert.equal(nudged.lon, -175);
});

test('inertia: decays to zero, and reduced motion gives none', () => {
  let v = 3;
  let frames = 0;
  while (v !== 0 && frames < 10000) {
    v = inertiaVelocity(v, 16, false);
    frames++;
  }
  assert.equal(v, 0);
  assert.ok(frames > 30 && frames < 400, `${frames} frames`);
  assert.equal(inertiaVelocity(3, 16, true), 0);
  assert.ok(Math.abs(decayVelocity(1, 325) - Math.exp(-1)) < 1e-12);
});
