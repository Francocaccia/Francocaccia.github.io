// Dev-time script: builds src/data/world-110m.json, the land and border data for the Aviation globe,
// from Natural Earth 1:110m (public domain, via the world-atlas package). The result is committed;
// world-atlas, topojson-client and d3-geo are devDependencies and are never shipped.
//
// Format (all coordinates are integers in tenths of a degree, delta-encoded: the first pair is absolute,
// every later pair is the step from the previous point):
//   { v, q, src, land: [[ring, ring...]...], borders: [line...], labels: [{n, lat, lon, a}...] }
// `land` is one entry per polygon: the exterior ring first, then its holes. Exteriors are wound one way
// and holes the opposite way (asserted below), which is what the globe's horizon clipper relies on.
// `borders` are the shared country borders only (coasts are already in `land`).
// `labels` are hand-placed anchors (a centroid would put Chile in the Pacific); `a` is the country's area
// in steradians, so a label can be skipped when the country is too small on screen.
import { mkdirSync, writeFileSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { geoArea } from 'd3-geo';
import { feature, mesh } from 'topojson-client';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const Q = 10;
const read = async (name) => JSON.parse(await readFile(join(root, 'node_modules', 'world-atlas', name), 'utf8'));

const landTopo = await read('land-110m.json');
const countriesTopo = await read('countries-110m.json');

const q = (v) => Math.round(v * Q);

// Quantize, drop a repeated closing point and consecutive duplicates.
function quantize(points, closed) {
  const out = [];
  for (const [lon, lat] of points) {
    const p = [q(lon), q(lat)];
    const last = out[out.length - 1];
    if (!last || last[0] !== p[0] || last[1] !== p[1]) out.push(p);
  }
  if (closed && out.length > 1) {
    const a = out[0];
    const b = out[out.length - 1];
    if (a[0] === b[0] && a[1] === b[1]) out.pop();
  }
  return out;
}

const delta = (pts) => {
  const flat = [];
  let x = 0;
  let y = 0;
  for (const [px, py] of pts) {
    flat.push(px - x, py - y);
    x = px;
    y = py;
  }
  return flat;
};

// Planar signed area in lon/lat; only its sign is used. Longitudes are unwrapped first, so a ring that
// crosses the antimeridian (Fiji, Wrangel Island) is not read as a ring that spans the whole world.
const signedArea = (pts) => {
  const unwrapped = [];
  let lon = pts[0][0];
  for (const [px, y] of pts) {
    let x = px;
    while (x - lon > 1800) x -= 3600;
    while (x - lon < -1800) x += 3600;
    lon = x;
    unwrapped.push([x, y]);
  }
  let s = 0;
  for (let i = 0, j = unwrapped.length - 1; i < unwrapped.length; j = i++) {
    s += unwrapped[j][0] * unwrapped[i][1] - unwrapped[i][0] * unwrapped[j][1];
  }
  return s / 2;
};

const land = [];
let exteriorSign = 0;
const landFeature = feature(landTopo, landTopo.objects.land);
const geometry = landFeature.features ? landFeature.features[0].geometry : landFeature.geometry;
const polygons = geometry.type === 'MultiPolygon' ? geometry.coordinates : [geometry.coordinates];
for (const polygon of polygons) {
  const rings = [];
  polygon.forEach((ring, index) => {
    const pts = quantize(ring, true);
    if (pts.length < 3) return;
    const sign = Math.sign(signedArea(pts));
    if (sign === 0) return;
    if (index === 0) {
      if (!exteriorSign) exteriorSign = sign;
      if (sign !== exteriorSign) throw new Error('exterior rings are not wound the same way');
    } else if (sign !== -exteriorSign) {
      throw new Error('a hole is wound the same way as the exteriors');
    }
    rings.push(delta(pts));
  });
  if (rings.length) land.push(rings);
}

const borderMesh = mesh(countriesTopo, countriesTopo.objects.countries, (a, b) => a !== b);
const borders = [];
for (const line of borderMesh.coordinates) {
  const pts = quantize(line, false);
  if (pts.length >= 2) borders.push(delta(pts));
}

// Anchors by hand: where the name reads best, not the centroid. Country names are those in world-atlas.
const anchors = [
  ['Argentina', 'Argentina', -44, -69.5],
  ['Brazil', 'Brazil', -10.5, -53],
  ['Chile', 'Chile', -33.5, -70.7],
  ['Bolivia', 'Bolivia', -16.8, -64.5],
  ['Peru', 'Peru', -9.5, -75],
  ['Colombia', 'Colombia', 3.8, -73],
  ['Venezuela', 'Venezuela', 7, -66],
  ['Paraguay', 'Paraguay', -23.3, -58.4],
  ['Mexico', 'Mexico', 23.5, -102],
  ['United States of America', 'United States', 33, -109],
  ['Canada', 'Canada', 58, -102],
  ['Cuba', 'Cuba', 21.7, -79],
];
const countries = feature(countriesTopo, countriesTopo.objects.countries).features;
const labels = anchors.map(([name, shown, lat, lon]) => {
  const country = countries.find((c) => c.properties.name === name);
  if (!country) throw new Error(`no country named ${name} in world-atlas`);
  return { n: shown, lat, lon, a: Math.round(geoArea(country) * 10000) / 10000 };
});

const out = { v: 1, q: Q, src: 'Natural Earth 1:110m via world-atlas 2.0.2 (public domain)', land, borders, labels };
const text = JSON.stringify(out);
const gz = gzipSync(text).length;
if (gz > 20 * 1024) throw new Error(`world-110m.json is ${gz} bytes gzipped; the limit is 20 KB`);
mkdirSync(join(root, 'src', 'data'), { recursive: true });
writeFileSync(join(root, 'src', 'data', 'world-110m.json'), text);
console.log(`world-110m.json: ${land.length} polygons, ${borders.length} border lines, ${labels.length} labels, ${text.length} bytes (${gz} gzipped)`);
